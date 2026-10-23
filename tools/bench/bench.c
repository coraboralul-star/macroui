/* #39 measurement harness. Plain C so the tool is not the #40 language choice.
   A later run (#53) can pass --engine-exe and use the same pipe, hook, and serial probe.

   The AHK engine ignores injected keys (LLKHF_INJECTED). SendInput is still timed,
   and it does not start a macro. Macros are started from the board's HID keyboard,
   which the engine treats as a real finger. Output is watched with WH_KEYBOARD_LL
   in this process, not inside the engine tick. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define EV_CAP (1 << 21)
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define LL_INJECTED 0x12

typedef struct {
    long long qpc;
    unsigned short vk;
    unsigned short flags;
    unsigned char down;
} Ev;

static LARGE_INTEGER g_freq;
static Ev *g_ev;
static volatile long g_ev_n;
static volatile long g_ev_drop;
static HHOOK g_hook;
static HANDLE g_hook_thread;
static DWORD g_hook_tid;
static HANDLE g_hook_ready;
static HANDLE g_raise_done;
#define WM_BENCH_RAISE (WM_APP + 7)

static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static HANDLE g_write_ev;
static HANDLE g_reader;
static volatile long g_pipe_dead;
static CRITICAL_SECTION g_cs;
static long long g_ack_qpc;
static int g_ack_ok;
static char g_ack_for[32];
static volatile long g_hello;
static volatile long g_running;
static volatile long g_forward;
static HANDLE g_serial = INVALID_HANDLE_VALUE;
static HANDLE g_serial_ev;
static FILE *g_trace;
static char g_com[32];
static char g_chip[64];
static char g_front[128];
static char g_last_reply[64];

static HANDLE g_proc;
static DWORD g_pid;
static volatile long g_cleaning;

static PDH_HQUERY g_pdh;
static PDH_HCOUNTER g_pdh_id;
static PDH_HCOUNTER g_pdh_sw;

static FILE *g_csv;
static int g_run;
static int g_seconds = 60;
static int g_triggers = 10000;
static int g_smoke;
static char g_only[32];
static char g_repo[MAX_PATH];
static char g_ahk[MAX_PATH];
static char g_engine_exe[MAX_PATH];
static char g_profile_path[MAX_PATH];

static unsigned g_trig_vk;
static int g_trig_usage;
static char g_trig_name[8];

static long g_line_n;
static long g_report_n;
static double *g_line_ms;
static double *g_report_ms;
static long g_lines_done;
static long g_hid_seen;
static long g_reply_ok;
static long g_reply_busy;
static long g_reply_other;

static void trace(const char *fmt, ...) {
    va_list ap;
    if (!g_trace)
        return;
    va_start(ap, fmt);
    vfprintf(g_trace, fmt, ap);
    fputc('\n', g_trace);
    fflush(g_trace);
    va_end(ap);
}

static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

static long long qpc_now(void) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}

static double qpc_ms(long long a, long long b) {
    return (double)(b - a) * 1000.0 / (double)g_freq.QuadPart;
}

static double elapsed_ms(long long a) {
    return qpc_ms(a, qpc_now());
}

static void ev_push(unsigned vk, unsigned flags, int down) {
    long long t = qpc_now();
    long i = InterlockedExchangeAdd(&g_ev_n, 1);
    if (i < 0 || i >= EV_CAP) {
        InterlockedIncrement(&g_ev_drop);
        return;
    }
    g_ev[i].vk = (unsigned short)vk;
    g_ev[i].flags = (unsigned short)flags;
    g_ev[i].down = (unsigned char)down;
    MemoryBarrier();
    g_ev[i].qpc = t;
}

static long ev_count(void) {
    long n = g_ev_n;
    MemoryBarrier();
    return n;
}

static LRESULT CALLBACK hook_proc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
        unsigned vk = k->vkCode;
        int watch = (vk >= VK_F12 && vk <= 0x87) || (vk >= 0x88 && vk <= 0xA0);
        if (watch)
            ev_push(vk, k->flags, (k->flags & LLKHF_UP) ? 0 : 1);
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

static DWORD WINAPI hook_thread(LPVOID unused) {
    (void)unused;
    g_hook_tid = GetCurrentThreadId();
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, hook_proc, GetModuleHandleW(NULL), 0);
    SetEvent(g_hook_ready);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_BENCH_RAISE) {
            if (g_hook)
                UnhookWindowsHookEx(g_hook);
            g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, hook_proc, GetModuleHandleW(NULL), 0);
            SetEvent(g_raise_done);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_hook)
        UnhookWindowsHookEx(g_hook);
    return 0;
}

static int wait_ev(int *cursor, long long after, unsigned vk, int hardware, int down, int timeout_ms, Ev *out) {
    long long t0 = qpc_now();
    for (;;) {
        long n = ev_count();
        int i = *cursor;
        if (i < 0)
            i = 0;
        for (; i < n && i < EV_CAP; i++) {
            Ev e = g_ev[i];
            if (e.qpc == 0)
                break;
            if (e.qpc < after)
                continue;
            if (e.vk == vk && (int)e.down == down && (!hardware || (e.flags & LL_INJECTED) == 0)) {
                *cursor = i + 1;
                if (out)
                    *out = e;
                return 1;
            }
        }
        *cursor = i;
        if (elapsed_ms(t0) >= timeout_ms)
            return 0;
        Sleep(1);
    }
}

static void json_field(const char *s, const char *key, char *out, int cap) {
    char pat[64];
    const char *p;
    int n = 0;
    out[0] = 0;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(s, pat);
    if (!p)
        return;
    p = strchr(p + strlen(pat), ':');
    if (!p)
        return;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '"')
        return;
    p++;
    while (*p && *p != '"' && n + 1 < cap)
        out[n++] = *p++;
    out[n] = 0;
}

static int running_count(const char *s) {
    const char *p = strstr(s, "\"running\":[");
    const char *end;
    int n = 0;
    if (!p)
        return -1;
    end = strchr(p, ']');
    if (!end)
        return -1;
    for (; p < end; p++) {
        if (strncmp(p, "\"id\"", 4) == 0)
            n++;
    }
    return n;
}

static void on_hid(const char *s, long long t_msg);
static void on_report(const unsigned char *p, DWORD n, long long t_msg);

/* Native frames, docs/engine/ipc-protocol.md. The tests still build the AHK
   JSON messages. pipe_write turns them into frames once hello said MUI1. */
#define FRAME_HELLO 1
#define FRAME_STATE 2
#define FRAME_ACK 3
#define FRAME_REPORT 5
#define FRAME_PROFILE 6
#define FRAME_OUTPUT 7
#define FRAME_COMMAND 8

static volatile long g_native;

static unsigned rd16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned long rd32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* A string is a u16 count, then the bytes. Returns the bytes used, or -1. */
static int frame_string(const unsigned char *p, DWORD left, char *out, int cap) {
    unsigned n;
    if (left < 2)
        return -1;
    n = rd16(p);
    if (n + 2 > left)
        return -1;
    if (out && cap > 0) {
        int c = (int)n < cap - 1 ? (int)n : cap - 1;
        memcpy(out, p + 2, (size_t)c);
        out[c] = 0;
    }
    return (int)n + 2;
}

static void on_frame(const unsigned char *buf, DWORD n, long long t) {
    unsigned type;
    unsigned long len;
    const unsigned char *p = buf + 12;
    if (n < 12)
        return;
    type = rd16(buf + 6);
    len = rd32(buf + 8);
    if (len + 12 != n)
        return;
    if (type == FRAME_HELLO) {
        g_native = 1;
        g_hello = 1;
    } else if (type == FRAME_ACK && len >= 2) {
        const char *whom = p[1] == 1 ? "command" : (p[1] == 2 ? "profile" : "");
        EnterCriticalSection(&g_cs);
        g_ack_qpc = t;
        g_ack_ok = p[0] == 1;
        strncpy(g_ack_for, whom, sizeof g_ack_for - 1);
        g_ack_for[sizeof g_ack_for - 1] = 0;
        LeaveCriticalSection(&g_cs);
    } else if (type == FRAME_STATE && len >= 8) {
        char front[128];
        int used = frame_string(p + 8, len - 8, NULL, 0);
        int got;
        g_running = (long)rd16(p + 2);
        if (used < 0)
            return;
        got = frame_string(p + 8 + used, len - 8 - used, front, sizeof front);
        if (got > 0 && front[0]) {
            strncpy(g_front, front, sizeof g_front - 1);
            g_front[sizeof g_front - 1] = 0;
        }
    } else if (type == FRAME_REPORT && len == 156) {
        on_report(p, len, t);
    }
}

static DWORD WINAPI reader_thread(LPVOID unused) {
    char *buf = (char *)malloc(1024 * 1024);
    OVERLAPPED ov;
    (void)unused;
    if (!buf)
        return 0;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (;;) {
        DWORD n = 0;
        long long t;
        char type[32];
        BOOL ok;
        ResetEvent(ov.hEvent);
        ok = ReadFile(g_pipe, buf, 1024 * 1024 - 1, &n, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, INFINITE) != WAIT_OBJECT_0) {
                g_pipe_dead = 1;
                break;
            }
            ok = GetOverlappedResult(g_pipe, &ov, &n, FALSE);
        }
        if (!ok || n == 0) {
            g_pipe_dead = 1;
            break;
        }
        buf[n] = 0;
        t = qpc_now();
        if (n >= 4 && memcmp(buf, "MUI1", 4) == 0) {
            on_frame((const unsigned char *)buf, n, t);
            continue;
        }
        json_field(buf, "type", type, sizeof type);
        if (strcmp(type, "hello") == 0) {
            g_hello = 1;
        } else if (strcmp(type, "ack") == 0) {
            char whom[32];
            int ok = strstr(buf, "\"ok\":true") != NULL;
            json_field(buf, "for", whom, sizeof whom);
            EnterCriticalSection(&g_cs);
            g_ack_qpc = t;
            g_ack_ok = ok;
            strncpy(g_ack_for, whom, sizeof g_ack_for - 1);
            g_ack_for[sizeof g_ack_for - 1] = 0;
            LeaveCriticalSection(&g_cs);
        } else if (strcmp(type, "state") == 0) {
            int rc = running_count(buf);
            char front[128];
            if (rc >= 0)
                g_running = rc;
            json_field(buf, "front", front, sizeof front);
            if (front[0]) {
                strncpy(g_front, front, sizeof g_front - 1);
                g_front[sizeof g_front - 1] = 0;
            }
        } else if (strcmp(type, "hid") == 0) {
            on_hid(buf, t);
        }
    }
    free(buf);
    return 0;
}

static int pipe_write_bytes(const void *s, DWORD n) {
    OVERLAPPED ov;
    DWORD w = 0;
    if (g_pipe == INVALID_HANDLE_VALUE)
        return 0;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = g_write_ev;
    ResetEvent(g_write_ev);
    if (!WriteFile(g_pipe, s, n, &w, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING)
            return 0;
        if (WaitForSingleObject(g_write_ev, 3000) != WAIT_OBJECT_0) {
            CancelIoEx(g_pipe, &ov);
            return 0;
        }
        if (!GetOverlappedResult(g_pipe, &ov, &w, FALSE))
            return 0;
    }
    return w == n;
}

static int write_frame(unsigned type, const void *payload, DWORD len) {
    unsigned char *f = (unsigned char *)malloc((size_t)len + 12);
    int ok;
    if (!f)
        return 0;
    memcpy(f, "MUI1", 4);
    f[4] = 1;
    f[5] = 0;
    f[6] = (unsigned char)(type & 0xFF);
    f[7] = (unsigned char)(type >> 8);
    f[8] = (unsigned char)(len & 0xFF);
    f[9] = (unsigned char)((len >> 8) & 0xFF);
    f[10] = (unsigned char)((len >> 16) & 0xFF);
    f[11] = (unsigned char)(len >> 24);
    if (len)
        memcpy(f + 12, payload, len);
    ok = pipe_write_bytes(f, len + 12);
    free(f);
    return ok;
}

/* The bench's own JSON messages as frames. A profile message is
   {"v":1,"type":"profile","profile":DOC}, and the frame carries DOC. */
static int native_send(const char *s) {
    char type[32];
    char word[32];
    unsigned char b;
    json_field(s, "type", type, sizeof type);
    if (strcmp(type, "command") == 0) {
        json_field(s, "action", word, sizeof word);
        b = strcmp(word, "start") == 0 ? 1 : strcmp(word, "stop") == 0 ? 2
            : strcmp(word, "reload") == 0 ? 3 : strcmp(word, "quit") == 0 ? 4 : 0;
        return write_frame(FRAME_COMMAND, &b, 1);
    }
    if (strcmp(type, "output") == 0) {
        json_field(s, "mode", word, sizeof word);
        b = strcmp(word, "rp2040") == 0 ? 1 : strcmp(word, "rp2350") == 0 ? 2 : 0;
        return write_frame(FRAME_OUTPUT, &b, 1);
    }
    if (strcmp(type, "profile") == 0) {
        const char *doc = strstr(s, "\"profile\":{");
        const char *end = strrchr(s, '}');
        unsigned char *payload;
        DWORD n;
        int ok;
        if (!doc || !end)
            return 0;
        doc += strlen("\"profile\":");
        if (end <= doc)
            return 0;
        n = (DWORD)(end - doc);
        payload = (unsigned char *)malloc((size_t)n + 4);
        if (!payload)
            return 0;
        payload[0] = (unsigned char)(n & 0xFF);
        payload[1] = (unsigned char)((n >> 8) & 0xFF);
        payload[2] = (unsigned char)((n >> 16) & 0xFF);
        payload[3] = (unsigned char)(n >> 24);
        memcpy(payload + 4, doc, n);
        ok = write_frame(FRAME_PROFILE, payload, n + 4);
        free(payload);
        return ok;
    }
    return 0;
}

static int pipe_write(const char *s) {
    if (g_native)
        return native_send(s);
    return pipe_write_bytes(s, (DWORD)strlen(s));
}

static int wait_ack_after(long long after, const char *whom, int timeout_ms, long long *when, int *ok) {
    long long t0 = qpc_now();
    for (;;) {
        long long q = 0;
        int good = 0;
        char got[32];
        EnterCriticalSection(&g_cs);
        q = g_ack_qpc;
        good = g_ack_ok;
        strncpy(got, g_ack_for, sizeof got - 1);
        got[sizeof got - 1] = 0;
        LeaveCriticalSection(&g_cs);
        if (q > after && strcmp(got, whom) == 0) {
            if (when)
                *when = q;
            if (ok)
                *ok = good;
            return 1;
        }
        if (g_pipe_dead || elapsed_ms(t0) >= timeout_ms)
            return 0;
        Sleep(1);
    }
}

static int send_for_ack(const char *json, const char *whom, int timeout_ms, double *rtt) {
    long long t0 = qpc_now();
    long long when = 0;
    int ok = 0;
    if (!pipe_write(json))
        return 0;
    if (!wait_ack_after(t0, whom, timeout_ms, &when, &ok))
        return 0;
    if (rtt)
        *rtt = qpc_ms(t0, when);
    return ok ? 1 : -1;
}

static int connect_pipe(void) {
    int i;
    for (i = 0; i < 50; i++) {
        g_pipe = CreateFileA("\\\\.\\pipe\\MacroUI", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (g_pipe != INVALID_HANDLE_VALUE)
            break;
        WaitNamedPipeA("\\\\.\\pipe\\MacroUI", 200);
        Sleep(100);
    }
    if (g_pipe == INVALID_HANDLE_VALUE)
        return 0;
    {
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(g_pipe, &mode, NULL, NULL)) {
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
            return 0;
        }
    }
    g_pipe_dead = 0;
    g_hello = 0;
    g_native = 0;
    g_reader = CreateThread(NULL, 0, reader_thread, NULL, 0, NULL);
    {
        long long t0 = qpc_now();
        while (!g_hello && !g_pipe_dead && elapsed_ms(t0) < 5000)
            Sleep(10);
    }
    return g_hello ? 1 : 0;
}

static void wide_path(const char *src, wchar_t *dst, int cap) {
    MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, cap);
}

static int engine_alive(void) {
    DWORD code = 0;
    if (!g_proc)
        return 0;
    if (!GetExitCodeProcess(g_proc, &code))
        return 0;
    return code == STILL_ACTIVE;
}

static int mutex_held(void) {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\MacroUI.Engine");
    if (!m)
        return 0;
    CloseHandle(m);
    return 1;
}

static int start_engine(void) {
    wchar_t cmd[2048];
    wchar_t cwd[MAX_PATH];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t ahk[MAX_PATH];
    wchar_t script[MAX_PATH];
    wchar_t exe[MAX_PATH];
    char script_a[MAX_PATH];

    if (mutex_held()) {
        say("engine mutex is already held");
        return 0;
    }
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    wide_path(g_repo, cwd, MAX_PATH);
    if (g_engine_exe[0]) {
        /* The shell passes --profile, and reload reads it. AHK reads the
           same file without being told. */
        wide_path(g_engine_exe, exe, MAX_PATH);
        wide_path(g_profile_path, script, MAX_PATH);
        _snwprintf(cmd, 2048, L"\"%s\" --profile \"%s\"", exe, script);
    } else {
        snprintf(script_a, sizeof script_a, "%s\\engine\\MacroEngine.ahk", g_repo);
        wide_path(g_ahk, ahk, MAX_PATH);
        wide_path(script_a, script, MAX_PATH);
        _snwprintf(cmd, 2048, L"\"%s\" \"%s\"", ahk, script);
    }
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) {
        say("CreateProcess failed %lu", GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    g_proc = pi.hProcess;
    g_pid = pi.dwProcessId;
    say("engine pid %lu", g_pid);
    trace("connect");
    if (!connect_pipe()) {
        trace("connect failed");
        say("pipe did not say hello");
        return 0;
    }
    trace("hello");
    return 1;
}

static void stop_engine(void) {
    if (g_pipe != INVALID_HANDLE_VALUE && !g_pipe_dead)
        pipe_write("{\"v\":1,\"type\":\"command\",\"action\":\"quit\"}");
    if (g_proc) {
        if (WaitForSingleObject(g_proc, 3000) != WAIT_OBJECT_0)
            TerminateProcess(g_proc, 1);
        CloseHandle(g_proc);
        g_proc = NULL;
    }
    if (g_reader) {
        WaitForSingleObject(g_reader, 1000);
        CloseHandle(g_reader);
        g_reader = NULL;
    }
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

static int serial_write(const char *s) {
    OVERLAPPED ov;
    DWORD w = 0;
    DWORD n;
    if (g_serial == INVALID_HANDLE_VALUE)
        return 0;
    n = (DWORD)strlen(s);
    memset(&ov, 0, sizeof ov);
    ov.hEvent = g_serial_ev;
    ResetEvent(g_serial_ev);
    if (WriteFile(g_serial, s, n, &w, &ov))
        return w == n;
    if (GetLastError() != ERROR_IO_PENDING)
        return 0;
    if (WaitForSingleObject(g_serial_ev, 500) != WAIT_OBJECT_0) {
        CancelIo(g_serial);
        return 0;
    }
    return GetOverlappedResult(g_serial, &ov, &w, FALSE) && w == n;
}

static int serial_byte(char *out, int timeout_ms) {
    OVERLAPPED ov;
    DWORD got = 0;
    memset(&ov, 0, sizeof ov);
    ov.hEvent = g_serial_ev;
    ResetEvent(g_serial_ev);
    if (!ReadFile(g_serial, out, 1, &got, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING)
            return 0;
        if (WaitForSingleObject(g_serial_ev, (DWORD)timeout_ms) != WAIT_OBJECT_0) {
            CancelIo(g_serial);
            return 0;
        }
        if (!GetOverlappedResult(g_serial, &ov, &got, FALSE))
            return 0;
    }
    return got == 1;
}

static int serial_line(char *buf, int cap, int timeout_ms) {
    int n = 0;
    long long t0 = qpc_now();
    if (cap < 2)
        return -1;
    while (n + 1 < cap && elapsed_ms(t0) < timeout_ms) {
        char c = 0;
        int left = timeout_ms - (int)elapsed_ms(t0);
        if (left < 1)
            break;
        if (left > 50)
            left = 50;
        if (!serial_byte(&c, left))
            continue;
        if (c == '\r')
            continue;
        if (c == '\n') {
            buf[n] = 0;
            return n;
        }
        buf[n++] = c;
    }
    buf[n] = 0;
    return -1;
}

static int board_cmd(const char *line, char *reply, int reply_cap) {
    char buf[128];
    int n;
    EnterCriticalSection(&g_cs);
    if (!serial_write(line)) {
        LeaveCriticalSection(&g_cs);
        return 0;
    }
    n = serial_line(buf, sizeof buf, 800);
    LeaveCriticalSection(&g_cs);
    if (n < 0)
        return 0;
    if (reply && reply_cap > 0) {
        strncpy(reply, buf, reply_cap - 1);
        reply[reply_cap - 1] = 0;
    }
    strncpy(g_last_reply, buf, sizeof g_last_reply - 1);
    g_last_reply[sizeof g_last_reply - 1] = 0;
    return 1;
}

static int board_key(int usage, int down) {
    char line[64];
    char reply[64];
    snprintf(line, sizeof line, "K %d %d\n", usage, down);
    if (!board_cmd(line, reply, sizeof reply))
        return 0;
    return strncmp(reply, "ok", 2) == 0 || strncmp(reply, "hid-busy", 8) == 0;
}

static int open_one_com(const char *com) {
    char path[64];
    DCB dcb;
    COMMTIMEOUTS to;
    char reply[64];
    HANDLE h;
    snprintf(path, sizeof path, "\\\\.\\%s", com);
    h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return 0;
    }
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return 0;
    }
    memset(&to, 0, sizeof to);
    /* Return immediately when no byte is waiting. A blocking ReadFile on this
       USB serial driver ignores the total timeout and stalls the harness. */
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutConstant = 0;
    to.ReadTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 500;
    SetCommTimeouts(h, &to);
    EscapeCommFunction(h, SETDTR);
    EscapeCommFunction(h, SETRTS);
    Sleep(200);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    g_serial = h;
    if (!board_cmd("?\n", reply, sizeof reply) || strncmp(reply, "Vendetta", 8) != 0) {
        CloseHandle(h);
        g_serial = INVALID_HANDLE_VALUE;
        return 0;
    }
    strncpy(g_com, com, sizeof g_com - 1);
    strncpy(g_chip, reply, sizeof g_chip - 1);
    say("board %s on %s", g_chip, g_com);
    board_key(0x68, 0);
    board_key(0x45, 0);
    board_key(0x69, 0);
    return 1;
}

static int find_board(void) {
    HKEY key;
    DWORD i;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS)
        return 0;
    for (i = 0; i < 64; i++) {
        wchar_t name[256];
        wchar_t data[64];
        DWORD name_n = 256;
        DWORD data_n = sizeof data;
        DWORD type = 0;
        char com[32];
        if (RegEnumValueW(key, i, name, &name_n, NULL, &type, (LPBYTE)data, &data_n) != ERROR_SUCCESS)
            break;
        if (type != REG_SZ)
            continue;
        WideCharToMultiByte(CP_UTF8, 0, data, -1, com, sizeof com, NULL, NULL);
        if (open_one_com(com)) {
            RegCloseKey(key);
            return 1;
        }
    }
    RegCloseKey(key);
    return 0;
}

static void write_board_file(void) {
    char path[MAX_PATH];
    FILE *f;
    snprintf(path, sizeof path, "%s\\tools\\bench\\out\\board.txt", g_repo);
    f = fopen(path, "w");
    if (!f)
        return;
    fputs(g_com, f);
    fclose(f);
}

static int probe_trigger(void) {
    int cursor;
    long long t0;
    Ev e;
    if (g_serial == INVALID_HANDLE_VALUE)
        return 0;
    cursor = ev_count();
    t0 = qpc_now();
    if (board_key(0x68, 1)) {
        int ok = wait_ev(&cursor, t0, VK_F13, 1, 1, 500, &e);
        board_key(0x68, 0);
        Sleep(30);
        if (ok) {
            g_trig_vk = VK_F13;
            g_trig_usage = 0x68;
            strcpy(g_trig_name, "F13");
            say("trigger key F13 via the board");
            return 1;
        }
    }
    cursor = ev_count();
    t0 = qpc_now();
    if (board_key(0x45, 1)) {
        int ok = wait_ev(&cursor, t0, VK_F12, 1, 1, 500, &e);
        board_key(0x45, 0);
        Sleep(30);
        if (ok) {
            g_trig_vk = VK_F12;
            g_trig_usage = 0x45;
            strcpy(g_trig_name, "F12");
            say("trigger key F12 via the board");
            return 1;
        }
    }
    say("board key did not arrive as F13 or F12");
    return 0;
}

static void raise_our_hook(void) {
    if (!g_hook_tid || !g_raise_done)
        return;
    ResetEvent(g_raise_done);
    if (!PostThreadMessageW(g_hook_tid, WM_BENCH_RAISE, 0, 0))
        return;
    WaitForSingleObject(g_raise_done, 500);
}

static void serial_resync(void) {
    int i;
    if (g_serial == INVALID_HANDLE_VALUE)
        return;
    PurgeComm(g_serial, PURGE_RXCLEAR);
    for (i = 0; i < 4; i++) {
        char reply[64];
        if (board_cmd("?\n", reply, sizeof reply) && strncmp(reply, "Vendetta", 8) == 0)
            return;
    }
}

static int tap_trigger(void) {
    int cursor = ev_count();
    long long t0 = qpc_now();
    Ev e;
    raise_our_hook();
    board_key(g_trig_usage, 0);
    Sleep(20);
    if (!board_key(g_trig_usage, 1))
        return 0;
    wait_ev(&cursor, t0, g_trig_vk, 1, 1, 300, &e);
    Sleep(15);
    if (!board_key(g_trig_usage, 0))
        return 0;
    wait_ev(&cursor, t0, g_trig_vk, 1, 0, 300, NULL);
    Sleep(15);
    return 1;
}

static int usage_of(const char *name) {
    int n;
    if ((name[0] == 'f' || name[0] == 'F') && isdigit((unsigned char)name[1])) {
        n = atoi(name + 1);
        if (n >= 1 && n <= 12)
            return 0x3A + (n - 1);
        if (n >= 13 && n <= 24)
            return 0x68 + (n - 13);
    }
    if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z')
        return 0x04 + (name[0] - 'a');
    return -1;
}

static void forward_key(int usage, int down, long long *last_ok) {
    char line[64];
    char reply[64];
    long long t0;
    snprintf(line, sizeof line, "K %d %d\n", usage, down);
    t0 = qpc_now();
    if (board_cmd(line, reply, sizeof reply)) {
        double ms = qpc_ms(t0, qpc_now());
        if (g_line_n < 200000)
            g_line_ms[g_line_n++] = ms;
        g_lines_done++;
        if (strncmp(reply, "ok", 2) == 0)
            g_reply_ok++;
        else if (strncmp(reply, "hid-busy", 8) == 0)
            g_reply_busy++;
        else
            g_reply_other++;
        if (last_ok)
            *last_ok = qpc_now();
    }
}

static void forward_names(const char *json, const char *field, int down, long long *last_ok) {
    char pat[32];
    const char *p;
    const char *end;
    snprintf(pat, sizeof pat, "\"%s\":[", field);
    p = strstr(json, pat);
    if (!p)
        return;
    p += strlen(pat);
    end = strchr(p, ']');
    if (!end)
        return;
    while (p < end) {
        const char *q1;
        const char *q2;
        char name[32];
        int n;
        int usage;
        if (*p != '"') {
            p++;
            continue;
        }
        q1 = p + 1;
        q2 = strchr(q1, '"');
        if (!q2 || q2 > end)
            break;
        n = (int)(q2 - q1);
        if (n > 30)
            n = 30;
        memcpy(name, q1, n);
        name[n] = 0;
        usage = usage_of(name);
        if (usage >= 0)
            forward_key(usage, down, last_ok);
        p = q2 + 1;
    }
}

/* Native key ids are virtual keys (engine_key_id). Only the letters and F
   keys the bench presses are mapped, the same set as usage_of. */
static int usage_of_id(unsigned id) {
    if (id >= 0x41 && id <= 0x5A)
        return 0x04 + (int)(id - 0x41);
    if (id >= 0x70 && id <= 0x7B)
        return 0x3A + (int)(id - 0x70);
    if (id >= 0x7C && id <= 0x87)
        return 0x68 + (int)(id - 0x7C);
    return -1;
}

static void forward_ids(const unsigned char *list, int count, int down, long long *last_ok) {
    int i;
    if (count > 32)
        count = 32;
    for (i = 0; i < count; i++) {
        int usage = usage_of_id(rd16(list + i * 2));
        if (usage >= 0)
            forward_key(usage, down, last_ok);
    }
}

static void on_hid(const char *s, long long t_msg) {
    long long last = 0;
    if (!g_forward)
        return;
    g_hid_seen++;
    forward_names(s, "down", 1, &last);
    forward_names(s, "up", 0, &last);
    if (last && g_report_n < 100000)
        g_report_ms[g_report_n++] = qpc_ms(t_msg, last);
}

/* Downs, then ups, like shell/ReportBoard.cs. The bench macros never move
   the mouse. */
static void on_report(const unsigned char *p, DWORD n, long long t_msg) {
    long long last = 0;
    (void)n;
    if (!g_forward)
        return;
    g_hid_seen++;
    forward_ids(p + 4, p[0], 1, &last);
    forward_ids(p + 68, p[1], 0, &last);
    if (last && g_report_n < 100000)
        g_report_ms[g_report_n++] = qpc_ms(t_msg, last);
}

static int pdh_start(void) {
    PDH_STATUS st;
    st = PdhOpenQueryW(NULL, 0, &g_pdh);
    if (st != ERROR_SUCCESS) {
        trace("pdh open 0x%lx", (unsigned long)st);
        return 0;
    }
    st = PdhAddEnglishCounterW(g_pdh, L"\\Thread(*)\\ID Process", 0, &g_pdh_id);
    if (st != ERROR_SUCCESS) {
        trace("pdh id counter 0x%lx", (unsigned long)st);
        return 0;
    }
    st = PdhAddEnglishCounterW(g_pdh, L"\\Thread(*)\\Context Switches/sec", 0, &g_pdh_sw);
    if (st != ERROR_SUCCESS) {
        trace("pdh switch counter 0x%lx", (unsigned long)st);
        return 0;
    }
    st = PdhCollectQueryData(g_pdh);
    if (st != ERROR_SUCCESS)
        trace("pdh first collect 0x%lx", (unsigned long)st);
    return 1;
}

static int pdh_rate(double *out) {
    DWORD bytes = 0;
    DWORD count = 0;
    PDH_FMT_COUNTERVALUE_ITEM_W *ids = NULL;
    PDH_FMT_COUNTERVALUE_ITEM_W *sw = NULL;
    DWORD id_count = 0;
    DWORD i;
    int found = 0;
    PDH_STATUS st;
    st = PdhCollectQueryData(g_pdh);
    if (st != ERROR_SUCCESS) {
        static int logged;
        if (!logged) {
            trace("pdh collect 0x%lx", (unsigned long)st);
            logged = 1;
        }
        return 0;
    }
    st = PdhGetFormattedCounterArrayW(g_pdh_id, PDH_FMT_LARGE, &bytes, &count, NULL);
    if (st != (PDH_STATUS)PDH_MORE_DATA)
        return 0;
    ids = (PDH_FMT_COUNTERVALUE_ITEM_W *)malloc(bytes ? bytes : 1);
    if (!ids)
        return 0;
    st = PdhGetFormattedCounterArrayW(g_pdh_id, PDH_FMT_LARGE, &bytes, &count, ids);
    if (st != ERROR_SUCCESS) {
        free(ids);
        return 0;
    }
    id_count = count;
    bytes = 0;
    count = 0;
    st = PdhGetFormattedCounterArrayW(g_pdh_sw, PDH_FMT_DOUBLE, &bytes, &count, NULL);
    if (st != (PDH_STATUS)PDH_MORE_DATA) {
        free(ids);
        return 0;
    }
    sw = (PDH_FMT_COUNTERVALUE_ITEM_W *)malloc(bytes ? bytes : 1);
    if (!sw) {
        free(ids);
        return 0;
    }
    st = PdhGetFormattedCounterArrayW(g_pdh_sw, PDH_FMT_DOUBLE, &bytes, &count, sw);
    if (st == (PDH_STATUS)PDH_MORE_DATA) {
        PDH_FMT_COUNTERVALUE_ITEM_W *grown = (PDH_FMT_COUNTERVALUE_ITEM_W *)realloc(sw, bytes);
        if (!grown) {
            free(sw);
            free(ids);
            return 0;
        }
        sw = grown;
        st = PdhGetFormattedCounterArrayW(g_pdh_sw, PDH_FMT_DOUBLE, &bytes, &count, sw);
    }
    if (st != ERROR_SUCCESS) {
        free(sw);
        free(ids);
        return 0;
    }
    {
        double sum = 0;
        DWORD j;
        for (i = 0; i < id_count; i++) {
            if (ids[i].FmtValue.CStatus != ERROR_SUCCESS)
                continue;
            if ((DWORD)ids[i].FmtValue.largeValue != g_pid)
                continue;
            for (j = 0; j < count; j++) {
                if (sw[j].FmtValue.CStatus != ERROR_SUCCESS)
                    continue;
                if (wcscmp(sw[j].szName, ids[i].szName) != 0)
                    continue;
                sum += sw[j].FmtValue.doubleValue;
                found = 1;
                break;
            }
        }
        if (found)
            *out = sum;
    }
    free(sw);
    free(ids);
    return found;
}

typedef struct {
    long long k;
    long long u;
    long long qpc;
} Times;

static int read_times(Times *t) {
    FILETIME c, e, k, u;
    HANDLE h;
    if (!g_proc)
        return 0;
    h = g_proc;
    if (!GetProcessTimes(h, &c, &e, &k, &u))
        return 0;
    t->k = (((long long)k.dwHighDateTime) << 32) | (unsigned long)k.dwLowDateTime;
    t->u = (((long long)u.dwHighDateTime) << 32) | (unsigned long)u.dwLowDateTime;
    t->qpc = qpc_now();
    return 1;
}

static double cpu_between(Times a, Times b) {
    double cpu_s = (double)((b.k - a.k) + (b.u - a.u)) / 1e7;
    double wall = qpc_ms(a.qpc, b.qpc) / 1000.0;
    if (wall <= 0)
        return -1;
    return 100.0 * cpu_s / wall;
}

static void sleep_watch(int ms, double *cpu, double *switches) {
    Times a, b;
    double sum = 0;
    int n = 0;
    int left = ms;
    read_times(&a);
    while (left > 0) {
        int slice = left > 1000 ? 1000 : left;
        double rate = 0;
        Sleep(slice);
        left -= slice;
        if (pdh_rate(&rate)) {
            sum += rate;
            n++;
        }
    }
    read_times(&b);
    if (cpu)
        *cpu = cpu_between(a, b);
    if (switches)
        *switches = n ? sum / n : -1;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db)
        return -1;
    if (da > db)
        return 1;
    return 0;
}

static void stats_of(double *v, int n, double *p50, double *p95, double *p99, double *mx, double *mean) {
    int i;
    double sum = 0;
    *p50 = *p95 = *p99 = *mx = *mean = -1;
    if (n <= 0)
        return;
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    for (i = 0; i < n; i++)
        sum += v[i];
    *mean = sum / n;
    *mx = v[n - 1];
    *p50 = v[(int)((n - 1) * 0.50)];
    *p95 = v[(int)((n - 1) * 0.95)];
    *p99 = v[(int)((n - 1) * 0.99)];
}

static void num_or_blank(FILE *f, double v) {
    if (v < 0)
        fputc(',', f);
    else
        fprintf(f, "%.4f,", v);
}

static void note_clean(const char *src, char *dst, int cap) {
    int n = 0;
    if (!src)
        src = "";
    while (*src && n + 1 < cap) {
        char c = *src++;
        if (c == ',' || c == '"' || c == '\n' || c == '\r')
            c = ';';
        dst[n++] = c;
    }
    dst[n] = 0;
}

static void csv_row(const char *test, const char *kase, int n,
    double p50, double p95, double p99, double mx, double mean,
    double cpu, double sw, double lines, double ack, double gap, int events,
    const char *status, const char *note) {
    char clean[512];
    note_clean(note, clean, sizeof clean);
    fprintf(g_csv, "%d,%s,%s,", g_run, test, kase);
    if (n >= 0)
        fprintf(g_csv, "%d", n);
    fputc(',', g_csv);
    num_or_blank(g_csv, p50);
    num_or_blank(g_csv, p95);
    num_or_blank(g_csv, p99);
    num_or_blank(g_csv, mx);
    num_or_blank(g_csv, mean);
    num_or_blank(g_csv, cpu);
    num_or_blank(g_csv, sw);
    num_or_blank(g_csv, lines);
    num_or_blank(g_csv, ack);
    num_or_blank(g_csv, gap);
    if (events >= 0)
        fprintf(g_csv, "%d", events);
    fprintf(g_csv, ",%s,%s\n", status, clean);
    fflush(g_csv);
    say("%s %s [%s] n=%d p50=%.3f p95=%.3f p99=%.3f max=%.3f cpu=%.2f sw=%.1f ack=%.3f gap=%.3f %s",
        test, kase, status, n, p50, p95, p99, mx, cpu, sw, ack, gap, clean);
}

static int arm(int on) {
    const char *msg = on
        ? "{\"v\":1,\"type\":\"command\",\"action\":\"start\"}"
        : "{\"v\":1,\"type\":\"command\",\"action\":\"stop\"}";
    int rc = send_for_ack(msg, "command", 2000, NULL);
    return rc > 0;
}

static int send_profile(const char *json, double *rtt) {
    int rc = send_for_ack(json, "profile", 8000, rtt);
    return rc > 0;
}

static void set_output(const char *mode) {
    char msg[128];
    snprintf(msg, sizeof msg, "{\"v\":1,\"type\":\"output\",\"mode\":\"%s\"}", mode);
    pipe_write(msg);
    Sleep(50);
}

static char *profile_one(const char *id, const char *mode, int repeats, const char *steps) {
    char *s = (char *)malloc(4096);
    if (!s)
        return NULL;
    snprintf(s, 4096,
        "{\"v\":1,\"type\":\"profile\",\"profile\":{\"version\":1,\"name\":\"bench\",\"variables\":{},"
        "\"macros\":[{\"id\":\"%s\",\"name\":\"%s\",\"enabled\":true,\"speed\":1,"
        "\"playMode\":\"%s\",\"repeatCount\":%d,\"exclusive\":false,\"busy\":false,"
        "\"advanced\":false,\"basic\":false,\"focusExe\":\"\","
        "\"trigger\":{\"kind\":\"key\",\"button\":\"%s\"},\"steps\":[%s]}]}}",
        id, id, mode, repeats, g_trig_name, steps);
    return s;
}

static char *cpu_profile(int count) {
    size_t cap = 1024 + (size_t)count * 700;
    char *s = (char *)malloc(cap);
    size_t used;
    int i;
    if (!s)
        return NULL;
    used = (size_t)snprintf(s, cap,
        "{\"v\":1,\"type\":\"profile\",\"profile\":{\"version\":1,\"name\":\"bench\",\"variables\":{},\"macros\":[");
    /* F14 to F24, one key each. Native has no vkXX names, so 11 is the most
       both engines can press without sharing a key. */
    for (i = 0; i < count && i < 11; i++) {
        char key[16];
        snprintf(key, sizeof key, "F%d", 14 + i);
        used += (size_t)snprintf(s + used, cap - used,
            "%s{\"id\":\"cpu-%d\",\"name\":\"cpu-%d\",\"enabled\":true,\"speed\":1,"
            "\"playMode\":\"repeat\",\"repeatCount\":200000,\"exclusive\":false,\"busy\":false,"
            "\"advanced\":false,\"basic\":false,\"focusExe\":\"\","
            "\"trigger\":{\"kind\":\"key\",\"button\":\"%s\"},"
            "\"steps\":[{\"type\":\"key\",\"action\":\"down\",\"key\":\"%s\"},"
            "{\"type\":\"wait\",\"ms\":1},{\"type\":\"key\",\"action\":\"up\",\"key\":\"%s\"},"
            "{\"type\":\"wait\",\"ms\":2}]}",
            i ? "," : "", i + 1, i + 1, g_trig_name, key, key);
    }
    snprintf(s + used, cap - used, "]}}");
    return s;
}

static char *padded_profile(int wait_ms, size_t target) {
    char head[512];
    char tail[1024];
    size_t hlen;
    size_t tlen;
    size_t pad;
    char *s;
    snprintf(head, sizeof head,
        "{\"v\":1,\"type\":\"profile\",\"profile\":{\"version\":1,\"name\":\"bench\",\"variables\":{\"pad\":\"");
    snprintf(tail, sizeof tail,
        "\"},\"macros\":[{\"id\":\"jit\",\"name\":\"jit\",\"enabled\":true,\"speed\":1,"
        "\"playMode\":\"repeat\",\"repeatCount\":200000,\"exclusive\":false,\"busy\":false,"
        "\"advanced\":false,\"basic\":false,\"focusExe\":\"\","
        "\"trigger\":{\"kind\":\"key\",\"button\":\"%s\"},"
        "\"steps\":[{\"type\":\"key\",\"action\":\"down\",\"key\":\"F14\"},"
        "{\"type\":\"wait\",\"ms\":%d},{\"type\":\"key\",\"action\":\"up\",\"key\":\"F14\"},"
        "{\"type\":\"wait\",\"ms\":2}]}]}}",
        g_trig_name, wait_ms);
    hlen = strlen(head);
    tlen = strlen(tail);
    pad = target > hlen + tlen ? target - hlen - tlen : 0;
    s = (char *)malloc(hlen + pad + tlen + 1);
    if (!s)
        return NULL;
    memcpy(s, head, hlen);
    memset(s + hlen, 'a', pad);
    memcpy(s + hlen + pad, tail, tlen + 1);
    return s;
}

static char *wrap_file(const char *path, size_t *out_n) {
    FILE *f = fopen(path, "rb");
    long n;
    char *body;
    const char *pre = "{\"v\":1,\"type\":\"profile\",\"profile\":";
    size_t pre_n;
    char *msg;
    size_t body_off = 0;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 2) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    body = (char *)malloc((size_t)n + 1);
    if (!body) {
        fclose(f);
        return NULL;
    }
    if (fread(body, 1, (size_t)n, f) != (size_t)n) {
        free(body);
        fclose(f);
        return NULL;
    }
    fclose(f);
    body[n] = 0;
    if ((unsigned char)body[0] == 0xEF && n >= 3) {
        body_off = 3;
        n -= 3;
    }
    while (n > 0 && (body[body_off + n - 1] == '\n' || body[body_off + n - 1] == '\r' || body[body_off + n - 1] == ' '))
        n--;
    pre_n = strlen(pre);
    msg = (char *)malloc(pre_n + (size_t)n + 2);
    if (!msg) {
        free(body);
        return NULL;
    }
    memcpy(msg, pre, pre_n);
    memcpy(msg + pre_n, body + body_off, (size_t)n);
    msg[pre_n + (size_t)n] = '}';
    msg[pre_n + (size_t)n + 1] = 0;
    free(body);
    if (out_n)
        *out_n = strlen(msg);
    return msg;
}

static int wait_running(int want, int timeout_ms) {
    long long t0 = qpc_now();
    while (elapsed_ms(t0) < timeout_ms) {
        if (g_running >= want)
            return 1;
        if (!engine_alive() || g_pipe_dead)
            return 0;
        Sleep(5);
    }
    return g_running >= want;
}

static int wait_output(int timeout_ms) {
    int cursor = ev_count();
    return wait_ev(&cursor, 0, VK_F14, 0, 1, timeout_ms, NULL);
}

static int collect_down_down(int i0, int i1, unsigned vk, double **out) {
    int cap = 1024;
    int n = 0;
    int have = 0;
    long long prev = 0;
    int i;
    double *a = (double *)malloc((size_t)cap * sizeof(double));
    if (!a)
        return 0;
    if (i1 > EV_CAP)
        i1 = EV_CAP;
    for (i = i0; i < i1; i++) {
        Ev e = g_ev[i];
        int spins = 0;
        while (e.qpc == 0 && spins < 2000) {
            MemoryBarrier();
            e = g_ev[i];
            spins++;
        }
        if (e.qpc == 0 || e.vk != vk || !e.down)
            continue;
        if (have) {
            if (n == cap) {
                cap *= 2;
                a = (double *)realloc(a, (size_t)cap * sizeof(double));
            }
            a[n++] = qpc_ms(prev, e.qpc);
        }
        prev = e.qpc;
        have = 1;
    }
    *out = a;
    return n;
}

static int collect_down_up(int i0, int i1, unsigned vk, double **out) {
    int cap = 1024;
    int n = 0;
    int have_down = 0;
    long long down = 0;
    int i;
    double *a = (double *)malloc((size_t)cap * sizeof(double));
    if (!a)
        return 0;
    if (i1 > EV_CAP)
        i1 = EV_CAP;
    for (i = i0; i < i1; i++) {
        Ev e = g_ev[i];
        if (e.qpc == 0 || e.vk != vk)
            continue;
        if (e.down) {
            have_down = 1;
            down = e.qpc;
        } else if (have_down) {
            if (n == cap) {
                cap *= 2;
                a = (double *)realloc(a, (size_t)cap * sizeof(double));
            }
            a[n++] = qpc_ms(down, e.qpc);
            have_down = 0;
        }
    }
    *out = a;
    return n;
}

static int collect_up_down(int i0, int i1, unsigned vk, double **out) {
    int cap = 1024;
    int n = 0;
    int have_up = 0;
    long long up = 0;
    int i;
    double *a = (double *)malloc((size_t)cap * sizeof(double));
    if (!a)
        return 0;
    if (i1 > EV_CAP)
        i1 = EV_CAP;
    for (i = i0; i < i1; i++) {
        Ev e = g_ev[i];
        if (e.qpc == 0 || e.vk != vk)
            continue;
        if (!e.down) {
            have_up = 1;
            up = e.qpc;
        } else if (have_up) {
            if (n == cap) {
                cap *= 2;
                a = (double *)realloc(a, (size_t)cap * sizeof(double));
            }
            a[n++] = qpc_ms(up, e.qpc);
            have_up = 0;
        }
    }
    *out = a;
    return n;
}

static void report_stats(const char *test, const char *kase, double *v, int n, double cpu, double sw, const char *note) {
    double p50, p95, p99, mx, mean;
    stats_of(v, n, &p50, &p95, &p99, &mx, &mean);
    csv_row(test, kase, n, p50, p95, p99, mx, mean, cpu, sw, -1, -1, -1, n, n > 0 ? "ok" : "fail", note);
}

static int wait_output(int timeout_ms);

static int count_vk(int i0, unsigned vk, int hardware, int down) {
    int n = 0;
    int i;
    int i1 = ev_count();
    if (i1 > EV_CAP)
        i1 = EV_CAP;
    for (i = i0; i < i1; i++) {
        Ev e = g_ev[i];
        if (e.qpc == 0 || e.vk != vk || (int)e.down != down)
            continue;
        if (hardware && (e.flags & LL_INJECTED) != 0)
            continue;
        n++;
    }
    return n;
}

static int load_and_tap(const char *json, int want_running) {
    int tries;
    int mark;
    trace("arm");
    if (!arm(1)) {
        trace("arm failed");
        return 0;
    }
    trace("profile");
    if (!send_profile(json, NULL)) {
        trace("profile failed");
        return 0;
    }
    trace("profile ok");
    Sleep(200);
    g_running = 0;
    mark = ev_count();
    for (tries = 0; tries < 6; tries++) {
        trace("tap %d", tries);
        if (!tap_trigger()) {
            trace("tap failed reply=%s", g_last_reply);
            return 0;
        }
        if (wait_running(want_running, 500))
            return 1;
        Sleep(150);
    }
    say("start failed running=%ld front=%s pipe=%ld hw_down=%d f14=%d reply=%s",
        g_running, g_front, g_pipe_dead, count_vk(mark, g_trig_vk, 1, 1), count_vk(mark, VK_F14, 0, 1), g_last_reply);
    return 0;
}

static void test_idle(void) {
    char *json;
    double cpu = -1;
    double sw = -1;
    char steps[] = "{\"type\":\"wait\",\"ms\":1000}";
    json = profile_one("idle", "repeat", 1, steps);
    if (!json || !arm(1) || !send_profile(json, NULL)) {
        csv_row("idle", "armed", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "fail", "profile was not accepted");
        free(json);
        return;
    }
    free(json);
    Sleep(g_smoke ? 300 : 2000);
    say("idle %d s", g_seconds);
    sleep_watch(g_seconds * 1000, &cpu, &sw);
    csv_row("idle", "armed", 1, -1, -1, -1, -1, -1, cpu, sw, -1, -1, -1, 0, "ok",
        "armed; no runner; cpu_pct is one core = 100; switches_per_s is the wakeup proxy");
    arm(0);
}

static void test_jitter(int wait_ms) {
    char steps[256];
    char *json;
    char kase[32];
    char gap_name[32];
    int i0;
    int i1;
    double cpu = -1;
    double sw = -1;
    double *cycle = NULL;
    double *gap = NULL;
    int nc;
    int ng;
    char note[160];
    snprintf(steps, sizeof steps,
        "{\"type\":\"key\",\"action\":\"down\",\"key\":\"F14\"},{\"type\":\"wait\",\"ms\":%d},"
        "{\"type\":\"key\",\"action\":\"up\",\"key\":\"F14\"},{\"type\":\"wait\",\"ms\":2}",
        wait_ms);
    json = profile_one("jit", "repeat", 200000, steps);
    snprintf(kase, sizeof kase, "wait-%d", wait_ms);
    if (!json || !load_and_tap(json, 1)) {
        csv_row("jitter", kase, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "fail", "macro did not emit F14");
        free(json);
        arm(0);
        return;
    }
    free(json);
    Sleep(200);
    i0 = ev_count();
    say("jitter %d ms for %d s", wait_ms, g_seconds);
    sleep_watch(g_seconds * 1000, &cpu, &sw);
    i1 = ev_count();
    arm(0);
    nc = collect_down_down(i0, i1, VK_F14, &cycle);
    ng = collect_down_up(i0, i1, VK_F14, &gap);
    snprintf(note, sizeof note, "F14 down-to-down; down wait %d up wait 2; repeatCount 200000", wait_ms);
    report_stats("jitter", kase, cycle, nc, cpu, sw, note);
    snprintf(gap_name, sizeof gap_name, "wait-%d-down-to-up", wait_ms);
    snprintf(note, sizeof note, "F14 down to F14 up; this is the %d ms wait", wait_ms);
    report_stats("jitter", gap_name, gap, ng, cpu, sw, note);
    free(cycle);
    free(gap);
}

static void test_inject(void) {
    char *json;
    int limit = g_triggers;
    int timeout = 25;
    int hits = 0;
    int i;
    double *samples;
    int phase_miss = 0;
    char note[240];
    Times a, b;
    double cpu = -1;
    json = profile_one("trig", "once", 1,
        "{\"type\":\"key\",\"action\":\"down\",\"key\":\"F14\"},{\"type\":\"key\",\"action\":\"up\",\"key\":\"F14\"}");
    if (!json || !arm(1) || !send_profile(json, NULL)) {
        csv_row("trigger", "sendinput", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "fail", "profile was not accepted");
        free(json);
        return;
    }
    free(json);
    Sleep(100);
    samples = (double *)malloc((size_t)limit * sizeof(double));
    if (!samples)
        return;
    read_times(&a);
    say("sendinput trigger x%d", limit);
    for (i = 0; i < limit; i++) {
        INPUT in[2];
        int cursor;
        long long t0;
        Ev trig, out;
        int got;
        memset(in, 0, sizeof in);
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = (WORD)g_trig_vk;
        in[1].type = INPUT_KEYBOARD;
        in[1].ki.wVk = (WORD)g_trig_vk;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        cursor = ev_count();
        t0 = qpc_now();
        SendInput(1, &in[0], sizeof(INPUT));
        got = wait_ev(&cursor, t0, g_trig_vk, 0, 1, timeout, &trig);
        if (got)
            got = wait_ev(&cursor, trig.qpc, VK_F14, 0, 1, timeout, &out);
        SendInput(1, &in[1], sizeof(INPUT));
        if (got && samples)
            samples[hits++] = qpc_ms(trig.qpc, out.qpc);
        else
            phase_miss++;
        if (!g_smoke && i == 199 && hits == 0)
            timeout = 3;
        Sleep(timeout > 10 ? 5 : 1);
    }
    read_times(&b);
    cpu = cpu_between(a, b);
    snprintf(note, sizeof note,
        "SendInput %s; hits=%d misses=%d; engine drops LLKHF_INJECTED so a miss is the baseline; hook gap only when F14 arrived",
        g_trig_name, hits, phase_miss);
    if (hits == 0)
        csv_row("trigger", "sendinput", limit, -1, -1, -1, -1, -1, cpu, -1, -1, -1, -1, 0, "ok", note);
    else
        report_stats("trigger", "sendinput", samples, hits, cpu, -1, note);
    free(samples);
    arm(0);
}

static void test_board_trigger(void) {
    char *json;
    int limit = g_triggers;
    int hits = 0;
    int misses = 0;
    int no_trig = 0;
    int no_out = 0;
    int i;
    double *samples;
    char note[200];
    Times a, b;
    double cpu = -1;
    if (g_serial == INVALID_HANDLE_VALUE) {
        csv_row("trigger", "board", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "skip", "no board");
        return;
    }
    json = profile_one("trig", "once", 1,
        "{\"type\":\"key\",\"action\":\"down\",\"key\":\"F14\"},{\"type\":\"key\",\"action\":\"up\",\"key\":\"F14\"}");
    if (!json || !arm(1) || !send_profile(json, NULL)) {
        csv_row("trigger", "board", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "fail", "profile was not accepted");
        free(json);
        return;
    }
    free(json);
    serial_resync();
    Sleep(150);
    samples = (double *)malloc((size_t)limit * sizeof(double));
    read_times(&a);
    say("board trigger x%d", limit);
    for (i = 0; i < limit; i++) {
        int cursor = ev_count();
        long long t0 = qpc_now();
        Ev trig, out;
        int got;
        raise_our_hook();
        if (!board_key(g_trig_usage, 1)) {
            misses++;
            board_key(g_trig_usage, 0);
            continue;
        }
        got = wait_ev(&cursor, t0, g_trig_vk, 1, 1, 80, &trig);
        if (!got)
            no_trig++;
        else if (!wait_ev(&cursor, trig.qpc, VK_F14, 0, 1, 80, &out)) {
            no_out++;
            got = 0;
        } else
            samples[hits++] = qpc_ms(trig.qpc, out.qpc);
        if (!got)
            misses++;
        board_key(g_trig_usage, 0);
        wait_ev(&cursor, t0, g_trig_vk, 1, 0, 80, NULL);
        Sleep(20);
    }
    read_times(&b);
    cpu = cpu_between(a, b);
    snprintf(note, sizeof note,
        "hook F14 down minus hook %s down; misses=%d no_trigger=%d no_output=%d; serial time is before the trigger hook",
        g_trig_name, misses, no_trig, no_out);
    if (hits == 0)
        csv_row("trigger", "board", limit, -1, -1, -1, -1, -1, cpu, -1, -1, -1, -1, 0, "fail", note);
    else
        report_stats("trigger", "board", samples, hits, cpu, -1, note);
    free(samples);
    arm(0);
    board_key(g_trig_usage, 0);
}

static void test_cpu(int count) {
    char *json;
    char kase[16];
    double cpu = -1;
    double sw = -1;
    int i0;
    int i1;
    int events = 0;
    char note[160];
    snprintf(kase, sizeof kase, "macros-%d", count);
    json = cpu_profile(count);
    if (!json || !load_and_tap(json, count)) {
        snprintf(note, sizeof note, "running=%ld wanted=%d", g_running, count);
        csv_row("cpu", kase, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "fail", note);
        free(json);
        arm(0);
        return;
    }
    free(json);
    Sleep(200);
    i0 = ev_count();
    say("cpu %d macros for %d s", count, g_seconds);
    sleep_watch(g_seconds * 1000, &cpu, &sw);
    i1 = ev_count();
    events = i1 - i0;
    if (events < 0)
        events = 0;
    arm(0);
    snprintf(note, sizeof note, "repeat playMode; repeatCount 200000; shared %s trigger; cpu_pct is one core = 100", g_trig_name);
    csv_row("cpu", kase, count, -1, -1, -1, -1, -1, cpu, sw, -1, -1, -1, events, "ok", note);
}

static size_t g_disk_bytes;

static void test_pipe(void) {
    char *jit;
    char *pad;
    char *disk;
    char steps[] = "{\"type\":\"key\",\"action\":\"down\",\"key\":\"F14\"},{\"type\":\"wait\",\"ms\":5},"
        "{\"type\":\"key\",\"action\":\"up\",\"key\":\"F14\"},{\"type\":\"wait\",\"ms\":2}";
    double rtt = -1;
    double cpu = -1;
    double sw = -1;
    int i0;
    int i1;
    double *gaps = NULL;
    int ng;
    double p50, p95, p99, mx, mean;
    double stall = -1;
    char note[200];
    g_forward = 0;
    set_output("software");
    serial_resync();
    jit = profile_one("jit", "repeat", 200000, steps);
    if (!jit || !load_and_tap(jit, 1)) {
        csv_row("pipe", "profile-gap", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "fail", "jitter macro did not start");
        free(jit);
        return;
    }
    free(jit);
    Sleep(300);
    pad = padded_profile(5, g_disk_bytes ? g_disk_bytes : 100000);
    i0 = ev_count();
    if (!pad || !send_profile(pad, &rtt)) {
        csv_row("pipe", "profile-gap", 0, -1, -1, -1, -1, -1, -1, -1, -1, rtt, -1, 0, "fail", "padded profile was not acked");
        free(pad);
        arm(0);
        return;
    }
    Sleep(400);
    i1 = ev_count();
    ng = collect_down_down(i0, i1, VK_F14, &gaps);
    stats_of(gaps, ng, &p50, &p95, &p99, &mx, &mean);
    {
        long long ackq = 0;
        long long prev = 0;
        int have = 0;
        int i;
        EnterCriticalSection(&g_cs);
        ackq = g_ack_qpc;
        LeaveCriticalSection(&g_cs);
        for (i = i0 - 1; i >= 0; i--) {
            Ev e = g_ev[i];
            if (e.qpc == 0 || e.vk != VK_F14 || !e.down)
                continue;
            prev = e.qpc;
            have = 1;
            break;
        }
        for (i = i0; i < i1 && i < EV_CAP; i++) {
            Ev e = g_ev[i];
            if (e.qpc == 0 || e.vk != VK_F14 || !e.down)
                continue;
            if (have && prev <= ackq && e.qpc >= ackq)
                stall = qpc_ms(prev, e.qpc);
            prev = e.qpc;
            have = 1;
        }
        if (stall < 0 && have && prev <= ackq)
            stall = qpc_ms(prev, ackq);
        if (stall > mx)
            mx = stall;
    }
    snprintf(note, sizeof note, "gap_ms crosses the profile ack; padded profile %u bytes; ack_ms is the pipe round trip",
        pad ? (unsigned)strlen(pad) : 0);
    csv_row("pipe", "profile-gap", ng, p50, p95, p99, mx, mean, -1, -1, -1, rtt, stall, ng, ng > 0 ? "ok" : "fail", note);
    free(gaps);
    free(pad);
    arm(0);

    say("pipe reload");
    if (!send_for_ack("{\"v\":1,\"type\":\"command\",\"action\":\"reload\"}", "command", 8000, &rtt))
        csv_row("pipe", "reload", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "fail", "reload was not acked");
    else {
        snprintf(note, sizeof note, "disarmed command reload of profiles/default.json; ack includes the tick wait plus JSON.Parse");
        csv_row("pipe", "reload", 1, -1, -1, -1, -1, -1, -1, -1, -1, rtt, -1, 0, "ok", note);
    }

    say("pipe full profile");
    disk = wrap_file(g_profile_path, NULL);
    if (!disk || !send_profile(disk, &rtt))
        csv_row("pipe", "profile-file", 0, -1, -1, -1, -1, -1, -1, -1, -1, rtt, -1, 0, "fail", "default.json was not acked");
    else {
        snprintf(note, sizeof note, "disarmed type profile wrapping profiles/default.json (%u bytes); macros were not armed",
            (unsigned)strlen(disk));
        csv_row("pipe", "profile-file", 1, -1, -1, -1, -1, -1, -1, -1, -1, rtt, -1, 0, "ok", note);
    }
    free(disk);
    {
        char *quiet = profile_one("idle", "repeat", 1, "{\"type\":\"wait\",\"ms\":1000}");
        if (quiet) {
            send_profile(quiet, NULL);
            free(quiet);
        }
    }
    (void)cpu;
    (void)sw;
}

static void test_board_spam(void) {
    char *json;
    char steps[] = "{\"type\":\"key\",\"action\":\"down\",\"key\":\"F14\"},{\"type\":\"wait\",\"ms\":1},"
        "{\"type\":\"key\",\"action\":\"up\",\"key\":\"F14\"},{\"type\":\"wait\",\"ms\":2}";
    const char *mode = "rp2350";
    double cpu = -1;
    double sw = -1;
    double p50, p95, p99, mx, mean;
    double lp50, lp95, lp99, lmx, lmean;
    double lines = -1;
    char note[220];
    if (g_serial == INVALID_HANDLE_VALUE) {
        csv_row("board", "spam", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "skip", "no board");
        return;
    }
    if (strstr(g_chip, "RP2040"))
        mode = "rp2040";
    json = profile_one("spam", "repeat", 200000, steps);
    set_output(mode);
    if (!json || !load_and_tap(json, 1)) {
        csv_row("board", "spam", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "fail", "spam macro did not start");
        free(json);
        set_output("software");
        arm(0);
        return;
    }
    free(json);
    g_line_n = 0;
    g_report_n = 0;
    g_lines_done = 0;
    g_hid_seen = 0;
    g_reply_ok = 0;
    g_reply_busy = 0;
    g_reply_other = 0;
    g_forward = 1;
    say("board spam %d s", g_seconds);
    sleep_watch(g_seconds * 1000, &cpu, &sw);
    arm(0);
    Sleep(300);
    g_forward = 0;
    board_key(0x69, 0);
    board_key(g_trig_usage, 0);
    set_output("software");
    lines = g_seconds > 0 ? (double)g_lines_done / (double)g_seconds : -1;
    stats_of(g_report_ms, g_report_n, &p50, &p95, &p99, &mx, &mean);
    stats_of(g_line_ms, g_line_n, &lp50, &lp95, &lp99, &lmx, &lmean);
    snprintf(note, sizeof note, "hid read to last ok of that report; %s; hid=%ld", mode, g_hid_seen);
    csv_row("board", "emit-to-ok", g_report_n, p50, p95, p99, mx, mean, cpu, sw, lines, -1, -1, (int)g_hid_seen,
        g_report_n > 0 ? "ok" : "fail", note);
    snprintf(note, sizeof note, "one serial K line to its reply; ok=%ld hid-busy=%ld other=%ld",
        g_reply_ok, g_reply_busy, g_reply_other);
    say("board replies ok=%ld hid-busy=%ld other=%ld", g_reply_ok, g_reply_busy, g_reply_other);
    csv_row("board", "line-to-ok", g_line_n, lp50, lp95, lp99, lmx, lmean, cpu, sw, lines, -1, -1, (int)g_lines_done,
        g_line_n > 0 ? "ok" : "fail", note);
}

/* #51: k key downs on F15 up. Lockstep is what the shell does, one K line then
   its reply. Piped writes all k lines at once and reads the replies after. Span
   is the first write to the hook seeing the last down. Spread is first down to
   last down in the hook, so it is the HID report gap and not serial time. */
static void board_burst(int k, int piped) {
    int reps = g_triggers / 10;
    double *span;
    double *spread;
    double p50, p95, p99, mx, mean;
    double sp50, sp95, sp99, smx, smean;
    long ok = 0;
    long busy = 0;
    long other = 0;
    long miss = 0;
    int n = 0;
    int r;
    int j;
    char kase[32];
    char note[220];
    if (reps < 3)
        reps = 3;
    span = (double *)malloc(reps * sizeof(double));
    spread = (double *)malloc(reps * sizeof(double));
    if (!span || !spread) {
        free(span);
        free(spread);
        return;
    }
    snprintf(kase, sizeof kase, "%s-%d", piped ? "piped" : "lockstep", k);
    for (r = 0; r < reps; r++) {
        int cursor = ev_count();
        long long t0;
        long long first = 0;
        long long last = 0;
        int got = 1;
        if (piped) {
            char buf[128];
            int len = 0;
            for (j = 0; j < k; j++)
                len += snprintf(buf + len, sizeof buf - len, "K %d 1\n", 0x6A + j);
            EnterCriticalSection(&g_cs);
            t0 = qpc_now();
            if (serial_write(buf)) {
                for (j = 0; j < k; j++) {
                    char reply[64];
                    if (serial_line(reply, sizeof reply, 800) < 0)
                        other++;
                    else if (strncmp(reply, "ok", 2) == 0)
                        ok++;
                    else if (strncmp(reply, "hid-busy", 8) == 0)
                        busy++;
                    else
                        other++;
                }
            }
            LeaveCriticalSection(&g_cs);
        } else {
            t0 = qpc_now();
            for (j = 0; j < k; j++) {
                char line[32];
                char reply[64];
                snprintf(line, sizeof line, "K %d 1\n", 0x6A + j);
                if (!board_cmd(line, reply, sizeof reply))
                    other++;
                else if (strncmp(reply, "ok", 2) == 0)
                    ok++;
                else if (strncmp(reply, "hid-busy", 8) == 0)
                    busy++;
                else
                    other++;
            }
        }
        for (j = 0; j < k; j++) {
            int c = cursor;
            Ev e;
            if (!wait_ev(&c, t0, 0x7E + j, 1, 1, 200, &e)) {
                got = 0;
                break;
            }
            if (!first || e.qpc < first)
                first = e.qpc;
            if (e.qpc > last)
                last = e.qpc;
        }
        for (j = 0; j < k; j++)
            board_key(0x6A + j, 0);
        Sleep(5);
        if (!got) {
            miss++;
            continue;
        }
        span[n] = qpc_ms(t0, last);
        spread[n] = qpc_ms(first, last);
        n++;
    }
    stats_of(span, n, &p50, &p95, &p99, &mx, &mean);
    stats_of(spread, n, &sp50, &sp95, &sp99, &smx, &smean);
    snprintf(note, sizeof note, "first write to last down in the hook; ok=%ld hid-busy=%ld other=%ld miss=%ld",
        ok, busy, other, miss);
    csv_row("burst", kase, n, p50, p95, p99, mx, mean, -1, -1, -1, -1, -1, (int)miss, n > 0 ? "ok" : "fail", note);
    snprintf(kase, sizeof kase, "%s-%d-spread", piped ? "piped" : "lockstep", k);
    csv_row("burst", kase, n, sp50, sp95, sp99, smx, smean, -1, -1, -1, -1, -1, (int)miss, n > 0 ? "ok" : "fail",
        "first down to last down in the hook");
    free(span);
    free(spread);
}

static void test_board_burst(void) {
    static const int sizes[] = {1, 2, 4, 6};
    int i;
    if (g_serial == INVALID_HANDLE_VALUE) {
        csv_row("burst", "lockstep-1", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "skip", "no board");
        return;
    }
    say("board burst");
    for (i = 0; i < 4; i++) {
        board_burst(sizes[i], 0);
        board_burst(sizes[i], 1);
    }
}

static void cleanup(void) {
    if (InterlockedExchange(&g_cleaning, 1))
        return;
    g_forward = 0;
    if (g_serial != INVALID_HANDLE_VALUE) {
        board_key(0x68, 0);
        board_key(0x45, 0);
        board_key(0x69, 0);
    }
    stop_engine();
    if (g_serial != INVALID_HANDLE_VALUE) {
        CloseHandle(g_serial);
        g_serial = INVALID_HANDLE_VALUE;
    }
    if (g_hook_tid)
        PostThreadMessageW(g_hook_tid, WM_QUIT, 0, 0);
}

static BOOL WINAPI on_ctrl(DWORD type) {
    (void)type;
    cleanup();
    return FALSE;
}

static int arg_eq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

int main(int argc, char **argv) {
    int i;
    QueryPerformanceFrequency(&g_freq);
    InitializeCriticalSection(&g_cs);
    g_serial_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_write_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_ev = (Ev *)calloc(EV_CAP, sizeof(Ev));
    g_line_ms = (double *)malloc(200000 * sizeof(double));
    g_report_ms = (double *)malloc(100000 * sizeof(double));
    if (!g_ev || !g_line_ms || !g_report_ms) {
        say("out of memory");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (arg_eq(argv[i], "--only") && i + 1 < argc) {
            strncpy(g_only, argv[++i], sizeof g_only - 1);
        } else if (arg_eq(argv[i], "--smoke")) {
            g_smoke = 1;
            g_seconds = 2;
            g_triggers = 30;
        } else if (arg_eq(argv[i], "--run") && i + 1 < argc) {
            g_run = atoi(argv[++i]);
        } else if (arg_eq(argv[i], "--seconds") && i + 1 < argc) {
            g_seconds = atoi(argv[++i]);
        } else if (arg_eq(argv[i], "--triggers") && i + 1 < argc) {
            g_triggers = atoi(argv[++i]);
        } else if (arg_eq(argv[i], "--repo") && i + 1 < argc) {
            strncpy(g_repo, argv[++i], MAX_PATH - 1);
        } else if (arg_eq(argv[i], "--ahk") && i + 1 < argc) {
            strncpy(g_ahk, argv[++i], MAX_PATH - 1);
        } else if (arg_eq(argv[i], "--engine-exe") && i + 1 < argc) {
            strncpy(g_engine_exe, argv[++i], MAX_PATH - 1);
        } else if (arg_eq(argv[i], "--out") && i + 1 < argc) {
            g_csv = fopen(argv[++i], "a");
        }
    }
    if (!g_repo[0] || !g_csv) {
        fprintf(stderr, "usage: bench --repo PATH --out CSV [--ahk PATH] [--engine-exe PATH] [--run N] [--smoke]\n");
        return 2;
    }
    if (!g_engine_exe[0] && !g_ahk[0]) {
        say("--ahk or --engine-exe is required");
        return 2;
    }
    snprintf(g_profile_path, sizeof g_profile_path, "%s\\profiles\\default.json", g_repo);
    {
        char trace_path[MAX_PATH];
        snprintf(trace_path, sizeof trace_path, "%s\\tools\\bench\\out\\trace.txt", g_repo);
        g_trace = fopen(trace_path, "w");
    }
    {
        FILE *f = fopen(g_profile_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            g_disk_bytes = (size_t)ftell(f);
            fclose(f);
        }
    }
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    g_hook_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_raise_done = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hook_thread = CreateThread(NULL, 0, hook_thread, NULL, 0, NULL);
    WaitForSingleObject(g_hook_ready, 3000);
    if (!g_hook) {
        say("keyboard hook failed");
        return 1;
    }
    if (!pdh_start())
        trace("pdh start failed");
    if (!find_board())
        say("no Vendetta board; trigger and spam rows will skip or fail");
    else
        write_board_file();
    if (g_serial != INVALID_HANDLE_VALUE && !probe_trigger()) {
        say("board probe failed");
        cleanup();
        return 1;
    }
    if (!start_engine()) {
        cleanup();
        return 1;
    }
    set_output("software");
    if (!g_only[0] || strcmp(g_only, "idle") == 0)
        test_idle();
    if (g_only[0] && strcmp(g_only, "jitter") != 0 && strcmp(g_only, "trigger") != 0
        && strcmp(g_only, "cpu") != 0 && strcmp(g_only, "board") != 0 && strcmp(g_only, "pipe") != 0
        && strcmp(g_only, "idle") != 0 && strcmp(g_only, "burst") != 0) {
        say("unknown --only %s", g_only);
        cleanup();
        return 2;
    }
    if (g_serial == INVALID_HANDLE_VALUE) {
        csv_row("jitter", "wait-1", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "skip", "no board to press the trigger");
        csv_row("trigger", "board", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "skip", "no board");
        csv_row("cpu", "macros-1", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "skip", "no board");
        csv_row("board", "spam", 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, "skip", "no board");
    } else {
        if (!g_only[0] || strcmp(g_only, "jitter") == 0) {
            test_jitter(1);
            test_jitter(5);
            test_jitter(16);
        }
        if (!g_only[0] || strcmp(g_only, "trigger") == 0) {
            test_inject();
            test_board_trigger();
        }
        if (!g_only[0] || strcmp(g_only, "cpu") == 0) {
            test_cpu(1);
            test_cpu(4);
            test_cpu(11);
        }
        if (!g_only[0] || strcmp(g_only, "board") == 0)
            test_board_spam();
        if (strcmp(g_only, "burst") == 0)
            test_board_burst();
    }
    if (!g_only[0] || strcmp(g_only, "pipe") == 0)
        test_pipe();
    cleanup();
    say("done");
    return 0;
}
