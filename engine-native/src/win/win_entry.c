/* Resident process on Windows. Linux keeps the version printer.
   DPI, then the mutex, then the IPC thread, the hook thread, and the
   playback thread. --self-test returns before the mutex.
   Exit releases keys, unhooks, and closes the timer, the pipe, and the
   mutex, in that order. timeBeginPeriod and MMCSS are not called.
   See docs/engine/architecture.md. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clock_win.h"
#include "command.h"
#include "hook_win.h"
#include "output_win.h"
#include "process.h"
#include "profile.h"
#include "version.h"
#include "win_entry.h"

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

#define ENGINE_PIPE_NAME L"\\\\.\\pipe\\MacroUI"
#define ENGINE_PIPE_ACCESS 0x00000003u
#define ENGINE_PIPE_MODE 0x00000007u
#define ENGINE_PIPE_BYTES 1048576u
#define ENGINE_PIPE_TIMEOUT_MS 50u
#define ENGINE_JOIN_MS 5000
#define ENGINE_ARG_CAP 64
#define ENGINE_WINDOW_MS 500

#ifndef GA_ROOTOWNER
#define GA_ROOTOWNER 3
#endif

struct engine_focus {
    char name[ENGINE_PROFILE_FOCUS_CAP];
    char owner[ENGINE_PROFILE_FOCUS_CAP];
    int32_t mouse_x;
    int32_t mouse_y;
    int have_mouse;
    int have_window;
    int32_t win_x;
    int32_t win_y;
    int have_client;
    int32_t client_x;
    int32_t client_y;
    uint32_t dpi;
};

static const wchar_t k_session_class[] = L"MacroUIEngineSession";

struct engine_resident {
    struct engine_args args;
    struct engine_hook_state hooks;
    struct engine_win_hook hook;
    struct engine_win_clock clock;
    struct engine_output output;
    HANDLE mutex;
    HANDLE pipe;
    HANDLE ipc_wake;
    HANDLE release_pipe;
    HANDLE exited;
    HANDLE ipc_thread;
    HANDLE hook_thread;
    HANDLE playback_thread;
    HWND session;
    unsigned char *pipe_buf;
    atomic_int quit;
    atomic_int fail;
    atomic_int keys_done;
    atomic_int finished;
    atomic_int ipc_entered;
    atomic_int hook_entered;
    atomic_int play_entered;
    int client;
    struct engine_profile_slot profiles;
    struct engine_state_slot state_slot;
    struct engine_focus focus_pub;
    atomic_uint front_seq;
    char live_id[ENGINE_ID_CAP];
    char logged_config[ENGINE_ID_CAP];
    char live_name[ENGINE_PROFILE_NAME_CAP];
    char live_focus[ENGINE_PROFILE_FOCUS_CAP];
    char live_front[ENGINE_PROFILE_FOCUS_CAP];
    struct engine_focus focus_last;
    int focus_have;
    struct engine_play *play;
    struct engine_cmd_ring cmds;
    struct engine_trigger_mem triggers;
    struct engine_claim_mem claims;
    int output_changed;
    struct engine_fingers edge_fingers;
    int edge_ready;
    struct engine_fingers fingers;
    struct engine_report_queue reports;
    struct engine_state_window windows[ENGINE_STATE_WINDOW_CAP];
    int window_count;
    struct engine_state_snapshot sent_state;
    struct engine_state_window sent_windows[ENGINE_STATE_WINDOW_CAP];
    int sent_window_count;
    int sent_ready;
    DWORD windows_at;
    uint64_t profile_seq;
    int armed;
    HANDLE play_wake;
    int64_t finger_at[ENGINE_KEY_ID_MAX + 1];
    /* The hook's answer for the last down of each id, for the trigger line.
       repeat_logged is one "repeat ignored" line per hold, cleared on the up. */
    uint8_t last_swallow[ENGINE_KEY_ID_MAX + 1];
    uint8_t repeat_logged[ENGINE_KEY_ID_MAX + 1];
    int desktop_known;
    int desktop_open;
    int64_t log_beat;
    int64_t silence_at;
    wchar_t log_path[ENGINE_PROFILE_CAP];
};

static unsigned char g_state_buf[65536];

static struct engine_resident g;

static void drop_client(void);
static int load_resident_profile(void);

static void request_quit(void)
{
    atomic_store_explicit(&g.quit, 1, memory_order_release);
    if (g.ipc_wake)
        SetEvent(g.ipc_wake);
    if (g.play_wake)
        SetEvent(g.play_wake);
}

static void wake_playback(void)
{
    if (g.play_wake)
        SetEvent(g.play_wake);
}

static HANDLE take_handle(HANDLE *slot)
{
    if (!slot)
        return NULL;
    return InterlockedExchangePointer((PVOID *)slot, NULL);
}

static void close_mutex(void)
{
    HANDLE mutex = take_handle(&g.mutex);

    /* Close only. ReleaseMutex would drop a holder this process does not own,
       and the owner closes the same way AutoHotkey does: CloseHandle. */
    if (mutex)
        CloseHandle(mutex);
}

static void close_pipe(void)
{
    HANDLE pipe = take_handle(&g.pipe);

    if (!pipe)
        return;
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    g.client = 0;
}

static void close_timer(void)
{
    HANDLE timer = take_handle(&g.clock.timer);

    if (timer)
        CloseHandle(timer);
    g.clock.qpc_freq = 0;
}

int engine_win_release_output(struct engine_output *out)
{
    int sent = 0;
    int guard;

    if (!out)
        return 0;
    for (guard = 0; guard < 4 && out->applied_count > 0; guard++) {
        struct engine_report report;
        int active = engine_output_release(out, &report);

        if (out->mode != ENGINE_OUTPUT_SOFTWARE)
            continue;
        if (active)
            sent += engine_win_send_report(&report, out->swap_clicks);
    }
    return sent;
}

static void release_keys(void)
{
    struct engine_output *out;
    int guard;

    if (atomic_exchange_explicit(&g.keys_done, 1, memory_order_acq_rel) != 0)
        return;
    out = g.play ? &g.play->output : &g.output;
    if (out->mode == ENGINE_OUTPUT_SOFTWARE) {
        engine_win_release_output(out);
        return;
    }
    for (guard = 0; guard < 4 && out->applied_count > 0; guard++) {
        struct engine_report report;

        if (engine_output_release(out, &report))
            engine_report_queue_push(&g.reports, &report);
    }
}

static int write_frame_tries(HANDLE pipe, const unsigned char *bytes, int length, int tries)
{
    int i;

    if (!pipe || !bytes || length <= 0 || tries < 1)
        return 1;
    for (i = 0; i < tries; i++) {
        DWORD wrote = 0;

        if (WriteFile(pipe, bytes, (DWORD)length, &wrote, NULL))
            return 0;
        if (GetLastError() != ERROR_NO_DATA)
            return 1;
        Sleep(1);
    }
    return 1;
}

static int write_frame(HANDLE pipe, const unsigned char *bytes, int length)
{
    return write_frame_tries(pipe, bytes, length, 4);
}

static int write_state_frame(int tries)
{
    struct engine_state_snapshot snap;
    int n;

    engine_state_read(&g.state_slot, &snap);
    n = engine_state_frame(&snap, g.windows, g.window_count, g_state_buf, (int)sizeof g_state_buf);
    if (n < 0)
        return 1;
    if (write_frame_tries(g.pipe, g_state_buf, n, tries) != 0)
        return 1;
    g.sent_state = snap;
    g.sent_window_count = g.window_count;
    if (g.window_count > 0)
        memcpy(g.sent_windows, g.windows, (size_t)g.window_count * sizeof g.windows[0]);
    g.sent_ready = 1;
    return 0;
}

static void maybe_write_state(void)
{
    struct engine_state_snapshot snap;

    if (!g.client || !g.pipe)
        return;
    engine_state_read(&g.state_slot, &snap);
    if (g.sent_ready && g.sent_window_count == g.window_count &&
        memcmp(&g.sent_state, &snap, sizeof snap) == 0 &&
        (g.window_count <= 0 ||
         memcmp(g.sent_windows, g.windows, (size_t)g.window_count * sizeof g.windows[0]) == 0))
        return;
    write_state_frame(1);
}

static int64_t ipc_qpc(int64_t *freq)
{
    LARGE_INTEGER now;
    LARGE_INTEGER f;

    *freq = 1000;
    if (QueryPerformanceFrequency(&f) && f.QuadPart > 0)
        *freq = f.QuadPart;
    if (!QueryPerformanceCounter(&now))
        return 0;
    return now.QuadPart;
}

/* Every queued report, in order. The echo window starts when the frame is
   written, like AHK's QueueHidEcho after SendRaw. With no client the
   reports are dropped so the queue does not fill. */
static void flush_report(void)
{
    struct engine_report report;
    unsigned char buf[12 + ENGINE_REPORT_BYTES];
    int n;

    while (engine_report_queue_pop(&g.reports, &report)) {
        int64_t freq;
        int64_t now;

        if (!g.client || !g.pipe)
            continue;
        n = engine_report_frame(&report, buf, (int)sizeof buf);
        if (n <= 0)
            continue;
        if (write_frame(g.pipe, buf, n) != 0) {
            drop_client();
            continue;
        }
        now = ipc_qpc(&freq);
        engine_board_echo_written(&g.hooks, &report, now, freq);
    }
}

/* Hello, then one state. A failed state write is not a dropped client. */
static int greet_client(void)
{
    struct engine_state_snapshot snap;
    unsigned char hello[13] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
    };

    engine_state_read(&g.state_slot, &snap);
    hello[12] = snap.armed ? 1 : 0;
    if (write_frame(g.pipe, hello, (int)sizeof hello) != 0)
        return 1;
    write_state_frame(1);
    return 0;
}

static void drop_client(void)
{
    HANDLE pipe = g.pipe;

    g.client = 0;
    if (pipe)
        DisconnectNamedPipe(pipe);
}

static void on_pipe_message(const unsigned char *msg, int length)
{
    struct engine_pipe_frame frame;
    int ack_len = 0;
    const unsigned char *ack = NULL;
    int wake = 0;

    engine_frame_read(msg, length, &frame);
    if (frame.kind == ENGINE_FRAME_QUIT)
        ack = engine_ack_command(&ack_len);
    else if (frame.kind == ENGINE_FRAME_JSON || frame.kind == ENGINE_FRAME_BAD)
        ack = engine_ack_bad_frame(&ack_len);
    else if (frame.kind == ENGINE_FRAME_BAD_VERSION)
        ack = engine_ack_bad_version(&ack_len);
    else if (frame.kind == ENGINE_FRAME_START || frame.kind == ENGINE_FRAME_STOP ||
             frame.kind == ENGINE_FRAME_RELOAD || frame.kind == ENGINE_FRAME_COMMAND) {
        if (frame.kind == ENGINE_FRAME_START)
            engine_cmd_push(&g.cmds, ENGINE_CMD_START, 0);
        else if (frame.kind == ENGINE_FRAME_STOP)
            engine_cmd_push(&g.cmds, ENGINE_CMD_STOP, 0);
        else if (frame.kind == ENGINE_FRAME_RELOAD) {
            if (load_resident_profile() != 0) {
                const unsigned char *err;
                int err_len = 0;

                err = engine_error_profile_read(&err_len);
                if (write_frame(g.pipe, err, err_len) != 0) {
                    drop_client();
                    return;
                }
            }
            engine_cmd_push(&g.cmds, ENGINE_CMD_RELOAD, 0);
        }
        ack = engine_ack_command(&ack_len);
        wake = 1;
    } else if (frame.kind == ENGINE_FRAME_OUTPUT) {
        engine_cmd_push(&g.cmds, ENGINE_CMD_OUTPUT, frame.output_mode);
        wake = 1;
    } else if (frame.kind == ENGINE_FRAME_PROFILE) {
        const char *text = frame.profile_len ? (const char *)frame.profile : "";

        engine_profile_slot_apply_document(&g.profiles, text, frame.profile_len);
        ack = engine_ack_profile(&ack_len);
        wake = 1;
    } else
        return;
    if (ack && write_frame(g.pipe, ack, ack_len) != 0) {
        if (frame.kind == ENGINE_FRAME_QUIT) {
            atomic_store_explicit(&g.fail, 1, memory_order_release);
            request_quit();
            return;
        }
        drop_client();
        if (wake)
            wake_playback();
        return;
    }
    if (frame.kind == ENGINE_FRAME_BAD_VERSION)
        drop_client();
    if (frame.kind == ENGINE_FRAME_QUIT) {
        /* The ack is already written. The flag is next, then playback wakes. */
        request_quit();
        return;
    }
    if (wake)
        wake_playback();
}

static void poll_pipe(void)
{
    DWORD read = 0;
    BOOL ok;
    DWORD err;

    if (!g.pipe)
        return;
    if (!g.client) {
        BOOL connected = ConnectNamedPipe(g.pipe, NULL);

        err = GetLastError();
        if (connected || err == ERROR_PIPE_CONNECTED) {
            g.client = 1;
            if (greet_client() != 0) {
                drop_client();
                return;
            }
        } else
            return;
    }
    ok = ReadFile(g.pipe, g.pipe_buf, ENGINE_PIPE_BYTES, &read, NULL);
    if (!ok) {
        err = GetLastError();
        if (err == ERROR_NO_DATA)
            return;
        drop_client();
        return;
    }
    if (read > 0)
        on_pipe_message(g.pipe_buf, (int)read);
}

static void pump_window(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static LRESULT CALLBACK session_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_QUERYENDSESSION) {
        request_quit();
        return TRUE;
    }
    if (msg == WM_ENDSESSION && wparam) {
        request_quit();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static int open_session_window(void)
{
    WNDCLASSW wc;
    HWND hwnd;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = session_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = k_session_class;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;
    hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, k_session_class, L"",
                           WS_POPUP, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd)
        return 1;
    g.session = hwnd;
    return 0;
}

static void publish_focus(const struct engine_focus *focus)
{
    unsigned seq = atomic_load_explicit(&g.front_seq, memory_order_relaxed);

    atomic_store_explicit(&g.front_seq, seq + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    if (focus)
        g.focus_pub = *focus;
    else
        memset(&g.focus_pub, 0, sizeof g.focus_pub);
    atomic_store_explicit(&g.front_seq, seq + 2u, memory_order_release);
}

static void read_focus(struct engine_focus *dst)
{
    int tries;

    if (!dst)
        return;
    memset(dst, 0, sizeof *dst);
    for (tries = 0; tries < 8; tries++) {
        unsigned seq = atomic_load_explicit(&g.front_seq, memory_order_acquire);
        struct engine_focus tmp;

        if (seq & 1u)
            continue;
        tmp = g.focus_pub;
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&g.front_seq, memory_order_relaxed) != seq)
            continue;
        *dst = tmp;
        g.focus_last = tmp;
        g.focus_have = 1;
        return;
    }
    /* A torn read is not "no window". An empty name would miss every
       focusExe and fall through to the empty-focus config. */
    if (g.focus_have)
        *dst = g.focus_last;
}

static void copy_image_base(const wchar_t *path, char *dst, size_t cap)
{
    const wchar_t *base = path ? path : L"";
    const wchar_t *p;
    int n;

    for (p = base; *p; p++) {
        if (*p == L'\\' || *p == L'/')
            base = p + 1;
    }
    n = WideCharToMultiByte(CP_UTF8, 0, base, -1, dst, (int)cap, NULL, NULL);
    if (n <= 0 && cap > 0)
        dst[0] = '\0';
}

static void image_name(HWND hwnd, char *dst, size_t cap)
{
    DWORD pid = 0;
    HANDLE proc;
    wchar_t wide[1024];
    DWORD n = (DWORD)(sizeof wide / sizeof wide[0]);

    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    if (!hwnd || !GetWindowThreadProcessId(hwnd, &pid) || !pid)
        return;
    proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc)
        return;
    if (QueryFullProcessImageNameW(proc, 0, wide, &n))
        copy_image_base(wide, dst, cap);
    CloseHandle(proc);
}

static void wide_to_utf8(const wchar_t *wide, char *dst, size_t cap)
{
    int n;

    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    if (!wide || !wide[0])
        return;
    n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, dst, (int)cap, NULL, NULL);
    if (n <= 0)
        dst[0] = '\0';
}

/* Foreground image, owner, dpi, and rects for PickLive and goto.
   This stays on the IPC thread. */
static void refresh_front(void)
{
    struct engine_focus focus;
    HWND hwnd;
    HWND root;
    POINT cursor;
    POINT origin;
    RECT rect;
    UINT dpi;

    memset(&focus, 0, sizeof focus);
    focus.dpi = 96;
    hwnd = GetForegroundWindow();
    if (hwnd) {
        image_name(hwnd, focus.name, sizeof focus.name);
        root = GetAncestor(hwnd, GA_ROOTOWNER);
        if (!root)
            root = hwnd;
        image_name(root, focus.owner, sizeof focus.owner);
        dpi = GetDpiForWindow(hwnd);
        if (dpi)
            focus.dpi = dpi;
        if (GetWindowRect(hwnd, &rect)) {
            focus.have_window = 1;
            focus.win_x = rect.left;
            focus.win_y = rect.top;
        }
        origin.x = 0;
        origin.y = 0;
        if (ClientToScreen(hwnd, &origin)) {
            focus.have_client = 1;
            focus.client_x = origin.x;
            focus.client_y = origin.y;
        }
    }
    if (GetCursorPos(&cursor)) {
        focus.have_mouse = 1;
        focus.mouse_x = cursor.x;
        focus.mouse_y = cursor.y;
    }
    publish_focus(&focus);
}

struct window_collect {
    char front[ENGINE_PROFILE_FOCUS_CAP];
    char front_title[ENGINE_STATE_TITLE_CAP];
    int saw_front;
};

static int same_exe(const char *a, const char *b)
{
    int i;

    if (!a || !b)
        return 0;
    for (i = 0; a[i] && b[i]; i++) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return a[i] == b[i];
}

static BOOL CALLBACK collect_window(HWND hwnd, LPARAM param)
{
    struct window_collect *collect = (struct window_collect *)param;
    wchar_t title[ENGINE_STATE_TITLE_CAP];
    char exe[ENGINE_PROFILE_FOCUS_CAP];
    int i;

    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    title[0] = L'\0';
    if (GetWindowTextW(hwnd, title, ENGINE_STATE_TITLE_CAP) <= 0 || !title[0])
        return TRUE;
    image_name(hwnd, exe, sizeof exe);
    if (!exe[0])
        return TRUE;
    for (i = 0; i < g.window_count; i++) {
        if (same_exe(g.windows[i].exe, exe))
            return TRUE;
    }
    if (g.window_count >= ENGINE_STATE_WINDOW_CAP)
        return TRUE;
    snprintf(g.windows[g.window_count].exe, sizeof g.windows[0].exe, "%s", exe);
    wide_to_utf8(title, g.windows[g.window_count].title, sizeof g.windows[0].title);
    if (collect && same_exe(exe, collect->front))
        collect->saw_front = 1;
    g.window_count++;
    return TRUE;
}

static void refresh_windows(void)
{
    struct window_collect collect;
    struct engine_focus focus;
    wchar_t title[ENGINE_STATE_TITLE_CAP];
    HWND hwnd;

    read_focus(&focus);
    memset(&collect, 0, sizeof collect);
    snprintf(collect.front, sizeof collect.front, "%s", focus.name);
    g.window_count = 0;
    EnumWindows(collect_window, (LPARAM)&collect);
    if (!focus.name[0] || collect.saw_front)
        return;
    hwnd = GetForegroundWindow();
    title[0] = L'\0';
    if (hwnd)
        GetWindowTextW(hwnd, title, ENGINE_STATE_TITLE_CAP);
    wide_to_utf8(title, collect.front_title, sizeof collect.front_title);
    if (g.window_count >= ENGINE_STATE_WINDOW_CAP)
        g.window_count = ENGINE_STATE_WINDOW_CAP - 1;
    snprintf(g.windows[g.window_count].exe, sizeof g.windows[0].exe, "%s", focus.name);
    snprintf(g.windows[g.window_count].title, sizeof g.windows[0].title, "%s", collect.front_title);
    g.window_count++;
}

static int load_resident_profile(void)
{
    wchar_t wide[ENGINE_PROFILE_CAP];
    HANDLE file;
    LARGE_INTEGER size;
    char *buf;
    DWORD got = 0;
    int chars;
    int rc;

    /* No --profile is the empty document. There is no relative default. */
    if (!g.args.profile[0])
        return engine_profile_slot_load_missing(&g.profiles);
    chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, g.args.profile, -1, wide,
                                ENGINE_PROFILE_CAP);
    if (chars <= 0)
        return engine_profile_slot_fail(&g.profiles);
    file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();

        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return engine_profile_slot_load_missing(&g.profiles);
        return engine_profile_slot_fail(&g.profiles);
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (unsigned long long)size.QuadPart > ENGINE_PROFILE_TEXT_MAX) {
        CloseHandle(file);
        return engine_profile_slot_fail(&g.profiles);
    }
    buf = (char *)malloc((size_t)size.QuadPart + 1u);
    if (!buf) {
        CloseHandle(file);
        return engine_profile_slot_fail(&g.profiles);
    }
    if (size.QuadPart > 0 &&
        (!ReadFile(file, buf, (DWORD)size.QuadPart, &got, NULL) || got != (DWORD)size.QuadPart)) {
        CloseHandle(file);
        free(buf);
        return engine_profile_slot_fail(&g.profiles);
    }
    CloseHandle(file);
    buf[got] = '\0';
    rc = engine_profile_slot_load_text(&g.profiles, buf, got);
    free(buf);
    return rc;
}

static void send_play_report(const struct engine_report *report, int mode, int64_t now)
{
    if (!g.play || !report || !engine_report_active(report))
        return;
    if (mode == ENGINE_OUTPUT_SOFTWARE)
        engine_win_send_report(report, g.play->output.swap_clicks);
    else {
        /* Pico keys have no dwExtraInfo. The IPC thread adds the echo
           tokens once the frame is written. */
        (void)now;
        engine_report_queue_push(&g.reports, report);
        if (g.ipc_wake)
            SetEvent(g.ipc_wake);
    }
}

static void apply_commands(int64_t now)
{
    struct engine_cmd_item item;

    if (!g.play)
        return;
    if (engine_cmd_take_overflow(&g.cmds)) {
        g.armed = 0;
        engine_play_stop_all_why(g.play, ENGINE_WHY_OVERFLOW);
        while (engine_cmd_pop(&g.cmds, &item))
            ;
        return;
    }
    while (engine_cmd_pop(&g.cmds, &item)) {
        if (item.kind == ENGINE_CMD_START) {
            g.armed = 1;
            engine_trigger_reseed(&g.triggers);
        } else if (item.kind == ENGINE_CMD_STOP) {
            g.armed = 0;
            engine_play_stop_all_why(g.play, ENGINE_WHY_COMMAND);
        } else if (item.kind == ENGINE_CMD_RELOAD) {
            engine_trigger_reseed(&g.triggers);
        } else if (item.kind == ENGINE_CMD_OUTPUT) {
            struct engine_report release;
            int rebind = 0;
            int sent = ENGINE_OUTPUT_SOFTWARE;

            if (engine_output_set_mode(&g.play->output, item.mode, &release, &rebind, &sent))
                send_play_report(&release, sent, now);
            if (rebind)
                g.output_changed = 1;
        }
    }
}

/* engine.log lines. The playback thread fills a slot and the IPC thread
   writes it with one open handle, so a slow disk or a scanner never stalls
   playback. "engine start" is written before the threads exist, and the
   main thread drains once more after the IPC thread has left. */
#define ENGINE_LOG_LINES 256u
#define ENGINE_LOG_LINE 1024

struct engine_log_ring {
    char line[ENGINE_LOG_LINES][ENGINE_LOG_LINE];
    int len[ENGINE_LOG_LINES];
    atomic_uint head;
    atomic_uint tail;
    atomic_uint dropped;
    HANDLE file;
};

static struct engine_log_ring g_log;

static void engine_log(const char *text)
{
    SYSTEMTIME st;
    unsigned head;
    unsigned tail;
    unsigned at;
    int n;

    if (!g.log_path[0] || !text || !text[0])
        return;
    head = atomic_load_explicit(&g_log.head, memory_order_acquire);
    tail = atomic_load_explicit(&g_log.tail, memory_order_relaxed);
    if (tail - head >= ENGINE_LOG_LINES) {
        atomic_fetch_add_explicit(&g_log.dropped, 1u, memory_order_relaxed);
        return;
    }
    at = tail & (ENGINE_LOG_LINES - 1u);
    GetLocalTime(&st);
    n = snprintf(g_log.line[at], ENGINE_LOG_LINE - 2, "%04u-%02u-%02u %02u:%02u:%02u %s",
                 (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                 (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond, text);
    if (n <= 0)
        return;
    if (n > ENGINE_LOG_LINE - 3)
        n = ENGINE_LOG_LINE - 3;
    g_log.line[at][n++] = '\r';
    g_log.line[at][n++] = '\n';
    g_log.len[at] = n;
    atomic_store_explicit(&g_log.tail, tail + 1u, memory_order_release);
}

static void log_write(const char *bytes, int n)
{
    DWORD wrote = 0;

    if (!g_log.file) {
        HANDLE file = CreateFileW(g.log_path, FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

        if (file == INVALID_HANDLE_VALUE)
            return;
        g_log.file = file;
    }
    if (!WriteFile(g_log.file, bytes, (DWORD)n, &wrote, NULL)) {
        CloseHandle(g_log.file);
        g_log.file = NULL;
    }
}

/* One reader at a time: the IPC thread, then the main thread after it left. */
static void log_flush(void)
{
    unsigned head;
    unsigned tail;
    unsigned lost;

    if (!g.log_path[0])
        return;
    head = atomic_load_explicit(&g_log.head, memory_order_relaxed);
    tail = atomic_load_explicit(&g_log.tail, memory_order_acquire);
    while (head != tail) {
        unsigned at = head & (ENGINE_LOG_LINES - 1u);

        log_write(g_log.line[at], g_log.len[at]);
        head++;
        atomic_store_explicit(&g_log.head, head, memory_order_release);
    }
    lost = atomic_exchange_explicit(&g_log.dropped, 0u, memory_order_acq_rel);
    if (lost) {
        char line[96];
        SYSTEMTIME st;
        int n;

        GetLocalTime(&st);
        n = snprintf(line, sizeof line, "%04u-%02u-%02u %02u:%02u:%02u log dropped %u\r\n",
                     (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                     (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond, lost);
        if (n > 0 && n < (int)sizeof line)
            log_write(line, n);
    }
}

static void log_close(void)
{
    log_flush();
    if (g_log.file) {
        CloseHandle(g_log.file);
        g_log.file = NULL;
    }
}

static void set_engine_log(const char *profile)
{
    wchar_t wide[ENGINE_PROFILE_CAP];
    wchar_t *slash;
    size_t dir;
    int chars;

    g.log_path[0] = L'\0';
    if (!profile || !profile[0])
        return;
    chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, profile, -1, wide, ENGINE_PROFILE_CAP);
    if (chars <= 0)
        return;
    slash = wcsrchr(wide, L'\\');
    if (!slash)
        slash = wcsrchr(wide, L'/');
    if (!slash || slash == wide)
        return;
    dir = (size_t)(slash - wide + 1);
    if (dir + 11 >= ENGINE_PROFILE_CAP)
        return;
    memcpy(g.log_path, wide, dir * sizeof(wchar_t));
    memcpy(g.log_path + dir, L"engine.log", 11 * sizeof(wchar_t));
}

static void log_released(const uint16_t *ids, int n, const char *reason)
{
    char line[128];
    int i;
    int shown = n > 16 ? 16 : n;

    for (i = 0; i < shown; i++) {
        const char *name = engine_key_name(ids[i]);

        if (name)
            snprintf(line, sizeof line, "release %s reason=%s", name, reason);
        else
            snprintf(line, sizeof line, "release %u reason=%s", (unsigned)ids[i], reason);
        engine_log(line);
    }
    if (n > shown) {
        snprintf(line, sizeof line, "release more count=%d reason=%s", n - shown, reason);
        engine_log(line);
    }
}

static void log_repeat(const struct engine_live_config *live, uint16_t id);

static void drain_fingers(int64_t now, const struct engine_live_config *live)
{
    struct engine_finger ev;
    int pause;

    if (!g.play)
        return;
    pause = engine_fingers_down(&g.fingers, ENGINE_KEY_PAUSE);
    while (engine_event_pop(&g.hooks, &ev)) {
        if (ev.id == ENGINE_KEY_PAUSE && engine_panic_edge(ev.id, pause, ev.down))
            engine_play_stop_all_why(g.play, ENGINE_WHY_PANIC);
        /* Windows auto-repeat, or a second mouse 4 or 5 down, while the
           finger is down. It is not an edge, so a held trigger never starts
           again. One line per hold. */
        if (engine_finger_note(&g.fingers, ev.id, ev.down)) {
            if (!g.repeat_logged[ev.id] && engine_live_claims(live, ev.id)) {
                g.repeat_logged[ev.id] = 1;
                log_repeat(live, ev.id);
            }
        } else if (ev.down) {
            g.last_swallow[ev.id] = ev.swallowed ? 1 : 0;
        } else {
            g.repeat_logged[ev.id] = 0;
        }
        engine_finger_at_touch(g.finger_at, ENGINE_KEY_ID_MAX + 1, ev.id, ev.qpc ? ev.qpc : now);
        if (ev.id == ENGINE_KEY_PAUSE)
            pause = ev.down ? 1 : 0;
    }
    if (engine_event_overflow(&g.hooks)) {
        engine_event_clear_overflow(&g.hooks);
        engine_play_stop_all_why(g.play, ENGINE_WHY_OVERFLOW);
        engine_log("event ring overflow");
    }
    if (engine_report_queue_take_overflow(&g.reports)) {
        engine_play_stop_all_why(g.play, ENGINE_WHY_OVERFLOW);
        engine_log("report queue overflow");
    }
}

static int input_desktop_open(void)
{
    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);

    if (!desk)
        return 0;
    CloseDesktop(desk);
    return 1;
}

/* The first sample only records which desktop is current. Later, a closed
   input desktop is the lock screen. Coming back is a return. */
static int note_desktop(void)
{
    int open = input_desktop_open();
    int returned = 0;

    if (!g.desktop_known) {
        g.desktop_known = 1;
        g.desktop_open = open;
        return 0;
    }
    if (!g.desktop_open && open)
        returned = 1;
    g.desktop_open = open;
    return returned;
}

static void reconcile_fingers(void)
{
    uint16_t ids[16];
    int n;

    /* A swallowed trigger never updates GetAsyncKeyState, so that call would
       release it on the same tick as the press. The hook's own up is the
       release. The lock screen is the other one. */
    if (g.desktop_open)
        return;
    n = engine_fingers_release_down(&g.fingers, 1, 1, ids, 16);
    if (n > 0)
        log_released(ids, n, "desktop");
}

static const char *mode_name(int mode)
{
    switch (mode) {
    case ENGINE_MODE_ONCE: return "once";
    case ENGINE_MODE_REPEAT: return "repeat";
    case ENGINE_MODE_WHILE_HELD: return "whileHeld";
    case ENGINE_MODE_TOGGLE: return "toggle";
    case ENGINE_MODE_ON_RELEASE: return "onRelease";
    default: return "mode";
    }
}

static void log_config(void)
{
    char line[640];

    snprintf(line, sizeof line, "config name=\"%s\" id=%s focus=%s front=%s",
             g.live_name[0] ? g.live_name : "-",
             g.live_id[0] ? g.live_id : "-",
             g.live_focus[0] ? g.live_focus : "-",
             g.live_front[0] ? g.live_front : "-");
    engine_log(line);
}

static void log_state(int64_t now)
{
    char held[160];
    char runs[240];
    char muted[200];
    char line[900];
    int muted_pos = 0;
    int64_t freq = g.clock.qpc_freq > 0 ? g.clock.qpc_freq : 1000;
    int64_t beat = engine_ms_to_ticks(2000, freq);
    int64_t key_stamp;
    int64_t key_age = 0;
    int held_pos = 0;
    int run_pos = 0;
    int any = 0;
    uint16_t id;
    int i;

    if (!g.armed || !g.play)
        return;
    if (g.log_beat != 0 && now >= g.log_beat && (now - g.log_beat) < beat)
        return;
    held[0] = '\0';
    for (id = 1; id <= ENGINE_KEY_ID_MAX; id++) {
        const char *name;
        int wrote;

        if (!engine_fingers_down(&g.fingers, id))
            continue;
        any = 1;
        name = engine_key_name(id);
        wrote = snprintf(held + held_pos, sizeof held - (size_t)held_pos, "%s%s",
                         held_pos ? "," : "", name ? name : "?");
        if (wrote <= 0 || held_pos + wrote >= (int)sizeof held)
            break;
        held_pos += wrote;
    }
    runs[0] = '\0';
    for (i = 0; i < g.play->runner_count; i++) {
        const struct engine_runner *runner = &g.play->runners[i];
        int64_t age = 0;
        int wrote;

        if (runner->done)
            continue;
        any = 1;
        if (runner->freq > 0 && now >= runner->started_at)
            age = (now - runner->started_at) * 1000 / runner->freq;
        else if (freq > 0 && now >= runner->started_at)
            age = (now - runner->started_at) * 1000 / freq;
        wrote = snprintf(runs + run_pos, sizeof runs - (size_t)run_pos, "%s%s:%s:%lldms%s",
                         run_pos ? "," : "", runner->id[0] ? runner->id : "?",
                         mode_name(runner->mode), (long long)age, runner->scan_wait ? ":scan" : "");
        if (wrote <= 0 || run_pos + wrote >= (int)sizeof runs)
            break;
        run_pos += wrote;
    }
    muted[0] = '\0';
    for (i = 0; i < ENGINE_MUTE_SLOT_CAP; i++) {
        int wrote;

        if (!g.play->mute_slot_used[i] || g.play->mute_slot_count[i] <= 0)
            continue;
        wrote = snprintf(muted + muted_pos, sizeof muted - (size_t)muted_pos, "%s%s:%d",
                         muted_pos ? "," : "", g.play->mute_slot_id[i],
                         g.play->mute_slot_count[i]);
        if (wrote <= 0 || muted_pos + wrote >= (int)sizeof muted)
            break;
        muted_pos += wrote;
    }
    if (g.play->runner_count + g.play->pending_count >= ENGINE_RUNNER_CAP)
        any = 1;
    if (!any)
        return;
    key_stamp = (int64_t)atomic_load_explicit(&g.hooks.key_stamp, memory_order_acquire);
    if (key_stamp > 0 && now >= key_stamp && freq > 0)
        key_age = (now - key_stamp) * 1000 / freq;
    snprintf(line, sizeof line,
             "state config=\"%s\" id=%s focus=%s front=%s held=%s runners=%s pending=%d hookKeyAge=%lldms hookMouse=%s mouseNeed=%s arena=%u/%u mutes=%u/%u muted=%s",
             g.live_name[0] ? g.live_name : "-",
             g.live_id[0] ? g.live_id : "-",
             g.live_focus[0] ? g.live_focus : "-",
             g.live_front[0] ? g.live_front : "-",
             held_pos ? held : "-", run_pos ? runs : "-", g.play->pending_count, (long long)key_age,
             atomic_load_explicit(&g.hooks.mouse_installed, memory_order_acquire) ? "on" : "off",
             atomic_load_explicit(&g.hooks.need_mouse, memory_order_acquire) ? "on" : "off",
             (unsigned)g.play->arena_count, (unsigned)ENGINE_STEP_CAP,
             (unsigned)g.play->mute_count, (unsigned)ENGINE_MUTE_ID_CAP,
             muted_pos ? muted : "-");
    engine_log(line);
    g.log_beat = now;
}

static const char *macro_name(const struct engine_live_config *live, const char *id)
{
    int m;

    if (!live || !live->macros || !live->extras || !id)
        return "";
    for (m = 0; m < live->macro_count; m++) {
        if (strcmp(live->macros[m].id, id) == 0)
            return live->extras[m].name;
    }
    return "";
}

static void log_repeat(const struct engine_live_config *live, uint16_t id)
{
    const char *key = engine_key_name(id);
    int m;

    if (!live || !live->macros || !live->extras)
        return;
    for (m = 0; m < live->macro_count; m++) {
        char line[320];

        if (live->macros[m].trigger != id)
            continue;
        snprintf(line, sizeof line, "trigger %s repeat ignored macro=\"%s\" id=%s",
                 key ? key : "?", live->extras[m].name, live->macros[m].id);
        engine_log(line);
    }
}

/* Matched trigger edges, starts, stops, and refusals from this tick. */
static void log_notes(const struct engine_live_config *live)
{
    int64_t freq;
    int i;

    if (!g.play)
        return;
    freq = g.play->freq > 0 ? g.play->freq : 1000;
    for (i = 0; i < g.play->note_count; i++) {
        const struct engine_note *note = &g.play->notes[i];
        const char *name = macro_name(live, note->id);
        const char *why = engine_why_name(note->why);
        char line[512];

        if (note->kind == ENGINE_NOTE_EDGE) {
            const char *key = engine_key_name(note->key);

            /* swallow is what the hook did with the finger's down. A down
               the game saw is a trigger leaking through. */
            snprintf(line, sizeof line, "trigger %s %s macro=\"%s\" id=%s action=%s swallow=%s",
                     key ? key : "?", note->down ? "down" : "up", name, note->id, why,
                     note->key <= ENGINE_KEY_ID_MAX && g.last_swallow[note->key] ? "yes" : "no");
        } else if (note->kind == ENGINE_NOTE_START) {
            snprintf(line, sizeof line, "runner start macro=\"%s\" id=%s mode=%s reason=%s", name,
                     note->id, mode_name(note->mode), why);
        } else if (note->kind == ENGINE_NOTE_STOP) {
            snprintf(line, sizeof line,
                     "runner stop macro=\"%s\" id=%s mode=%s reason=%s age=%lldms", name,
                     note->id, mode_name(note->mode), why,
                     (long long)(note->age * 1000 / freq));
        } else if (note->kind == ENGINE_NOTE_BLOCK) {
            snprintf(line, sizeof line, "start blocked macro=\"%s\" id=%s mode=%s reason=%s",
                     name, note->id, mode_name(note->mode), why);
        } else {
            continue;
        }
        engine_log(line);
    }
    if (g.play->note_dropped) {
        char line[64];

        snprintf(line, sizeof line, "notes dropped %u", (unsigned)g.play->note_dropped);
        engine_log(line);
    }
    g.play->note_count = 0;
    g.play->note_dropped = 0;
}

static void log_unmatched(const struct engine_live_config *live)
{
    struct engine_fingers now;
    uint16_t id;

    if (!g.armed || !live || live->macro_count <= 0)
        return;
    now = g.fingers;
    if (!g.edge_ready) {
        g.edge_fingers = now;
        g.edge_ready = 1;
        return;
    }
    for (id = 1; id <= ENGINE_KEY_ID_MAX; id++) {
        char line[320];
        const char *name;

        if (!engine_fingers_down(&now, id) || engine_fingers_down(&g.edge_fingers, id))
            continue;
        if (id == ENGINE_KEY_LBUTTON || id == ENGINE_KEY_RBUTTON || id == ENGINE_KEY_PAUSE)
            continue;
        if (engine_live_claims(live, id))
            continue;
        name = engine_key_name(id);
        if (name)
            snprintf(line, sizeof line, "trigger %s unmatched config=\"%s\" id=%s",
                     name, g.live_name[0] ? g.live_name : "-",
                     g.live_id[0] ? g.live_id : "-");
        else
            snprintf(line, sizeof line, "trigger %u unmatched config=\"%s\" id=%s",
                     (unsigned)id, g.live_name[0] ? g.live_name : "-",
                     g.live_id[0] ? g.live_id : "-");
        engine_log(line);
    }
    g.edge_fingers = now;
}

static void watch_hooks(int64_t now, int need_mouse, int switched)
{
    struct engine_hook_watch_in in;
    struct engine_hook_watch_out watch;
    int64_t freq = g.clock.qpc_freq > 0 ? g.clock.qpc_freq : 1000;
    int i;

    if (!g.play)
        return;
    memset(&in, 0, sizeof in);
    in.key_installed = atomic_load_explicit(&g.hooks.key_installed, memory_order_acquire);
    in.mouse_installed = atomic_load_explicit(&g.hooks.mouse_installed, memory_order_acquire);
    in.need_mouse = need_mouse;
    in.armed = g.armed;
    in.running = engine_play_alive(g.play) > 0;
    in.pending = g.play->pending_count > 0;
    in.suspect = atomic_load_explicit(&g.hooks.suspect, memory_order_acquire);
    in.desktop = note_desktop();
    in.switched = switched;
    in.key_stamp = atomic_load_explicit(&g.hooks.key_stamp, memory_order_acquire);
    in.mouse_stamp = atomic_load_explicit(&g.hooks.mouse_stamp, memory_order_acquire);
    in.now = now;
    in.silence = engine_ms_to_ticks(8000, freq);
    in.silence_at = g.silence_at;
    engine_hook_set_need_mouse(&g.hooks, need_mouse);
    engine_hook_watch(&in, &watch);
    if (watch.silent)
        g.silence_at = now;
    if (watch.clear_suspect)
        atomic_store_explicit(&g.hooks.suspect, 0, memory_order_release);
    engine_hook_post_ops(&g.hooks, &watch);
    /* The hook thread applies a raise only after GetMessage returns.
       A keyboard hook is already installed. The mouse hook is not, so a
       mouse trigger never arrives unless this posts a message. */
    for (i = 0; i < watch.count; i++) {
        char line[420];
        int mouse = watch.op[i] == ENGINE_HOOK_RAISE_MOUSE;
        int64_t stamp = mouse ? in.mouse_stamp : in.key_stamp;
        int64_t age = 0;
        const char *reason = "missing";

        if (in.desktop)
            reason = "desktop";
        else if (in.suspect)
            reason = "suspect";
        else if (!mouse && watch.silent)
            reason = "silent";
        else if (mouse && !in.need_mouse)
            reason = "drop";
        else if (mouse && in.switched)
            reason = "switch";
        else if (mouse && !in.mouse_installed)
            reason = "missing";
        if (stamp > 0 && in.now >= stamp && freq > 0)
            age = (in.now - stamp) * 1000 / freq;
        snprintf(line, sizeof line, "hook %s %s reason=%s config=\"%s\" id=%s age=%lldms",
                 mouse ? "mouse" : "key",
                 (mouse && !in.need_mouse) ? "remove" : "reinstall", reason,
                 g.live_name[0] ? g.live_name : "-",
                 g.live_id[0] ? g.live_id : "-", (long long)age);
        engine_log(line);
    }
    if (watch.count > 0)
        engine_win_hook_kick(&g.hook);
}

static void fill_running(struct engine_state_snapshot *snap, const struct engine_live_config *live)
{
    int i;

    snap->running_count = 0;
    if (!g.play)
        return;
    for (i = 0; i < g.play->runner_count && snap->running_count < ENGINE_STATE_RUNNING_CAP; i++) {
        int row;
        int m;
        const char *name = "";

        if (g.play->runners[i].done)
            continue;
        row = snap->running_count++;
        snprintf(snap->running[row].id, sizeof snap->running[row].id, "%s", g.play->runners[i].id);
        if (live && live->macros && live->extras) {
            for (m = 0; m < live->macro_count; m++) {
                if (strcmp(live->macros[m].id, g.play->runners[i].id) == 0) {
                    name = live->extras[m].name;
                    break;
                }
            }
        }
        snprintf(snap->running[row].name, sizeof snap->running[row].name, "%s", name ? name : "");
    }
}

static void publish_play_state(const struct engine_live_config *live, const struct engine_focus *focus)
{
    struct engine_state_snapshot snap;
    int n;
    int i;

    memset(&snap, 0, sizeof snap);
    snap.armed = g.armed;
    if (live)
        snprintf(snap.active_id, sizeof snap.active_id, "%s", live->active_id);
    if (focus)
        snprintf(snap.front, sizeof snap.front, "%s", focus->name);
    fill_running(&snap, live);
    n = 0;
    if (g.play) {
        n = g.play->output.applied_count;
        if (n > ENGINE_STATE_HELD_CAP)
            n = ENGINE_STATE_HELD_CAP;
        if (n < 0)
            n = 0;
        for (i = 0; i < n; i++)
            snap.held[i] = g.play->output.applied[i];
    }
    snap.held_count = n;
    engine_state_publish_snapshot(&g.state_slot, &snap);
}

static int64_t next_deadline(int64_t now)
{
    int64_t next = now;
    int have = 0;
    int i;

    if (!g.play)
        return now;
    for (i = 0; i < g.play->runner_count; i++) {
        int64_t at;

        if (g.play->runners[i].done)
            continue;
        at = g.play->runners[i].wait_until;
        if (at <= now)
            continue;
        if (!have || at < next) {
            next = at;
            have = 1;
        }
    }
    return have ? next : now;
}

static void playback_tick(int64_t now)
{
    const struct engine_profile_image *image;
    struct engine_live_config live;
    struct engine_focus focus;
    struct engine_pointer pointer;
    uint64_t seq;
    int switched = 0;

    if (!g.play)
        return;
    memset(&live, 0, sizeof live);
    memset(&focus, 0, sizeof focus);
    image = engine_profile_slot_adopt(&g.profiles);
    /* Software swaps while building SendInput. The report image stays logical. */
    engine_output_set_swap(&g.play->output, image && image->swap_clicks);
    seq = image ? image->sequence : 0;
    read_focus(&focus);
    /* ApplyLiveConfig passes activeId every time. The last live id is not the tie-break. */
    engine_profile_pick(image, focus.name, image ? image->active_id : "", &live);
    engine_play_set_macros(g.play, live.macros, live.macro_count);
    if (seq != g.profile_seq) {
        g.profile_seq = seq;
        engine_reap_orphans(g.play, &live);
    }
    /* AHK sets liveId to Chr(1) on a profile, so SeedTriggers runs. prev is
       by macro index, and a new image can reorder or retrigger macros. */
    engine_trigger_note_profile(&g.triggers, seq);
    /* A new profile image is parsed on the IPC thread, so it does not stall
       the hook thread. Reseed only when the live config changes, which is
       when ApplyLiveConfig seeds. A reinstall must not clear a held trigger.
       The mouse hook is reinstalled on that same change when the new config
       has a middle or side trigger. */
    switched = strcmp(g.live_id, live.live_id) != 0;
    if (switched) {
        snprintf(g.live_id, sizeof g.live_id, "%s", live.live_id);
        engine_reap_orphans(g.play, &live);
        engine_trigger_reseed(&g.triggers);
    }
    snprintf(g.live_name, sizeof g.live_name, "%s", live.name);
    snprintf(g.live_focus, sizeof g.live_focus, "%s", live.focus_exe);
    if (strcmp(g.live_front, focus.name) != 0 || strcmp(g.logged_config, live.live_id) != 0) {
        snprintf(g.live_front, sizeof g.live_front, "%s", focus.name);
        snprintf(g.logged_config, sizeof g.logged_config, "%s", live.live_id);
        log_config();
    }
    g.play->dpi = focus.dpi ? focus.dpi : 96;
    memset(&pointer, 0, sizeof pointer);
    pointer.have_mouse = focus.have_mouse;
    pointer.mouse_x = focus.mouse_x;
    pointer.mouse_y = focus.mouse_y;
    pointer.have_window = focus.have_window;
    pointer.window_x = focus.win_x;
    pointer.window_y = focus.win_y;
    pointer.have_client = focus.have_client;
    pointer.client_x = focus.client_x;
    pointer.client_y = focus.client_y;
    engine_play_set_pointer(g.play, &pointer);

    apply_commands(now);
    watch_hooks(now, engine_live_needs_mouse(&live), switched);
    drain_fingers(now, &live);
    log_unmatched(&live);
    reconcile_fingers();
    {
        uint16_t gone[16];
        int n = engine_claims_sync(&g.claims, &live, focus.name, focus.owner, g.armed,
                                   g.output_changed, &g.fingers, gone, 16);

        g.output_changed = 0;
        if (n > 0)
            log_released(gone, n, "forget");
    }
    engine_play_set_fingers(g.play, &g.fingers);
    if (g.armed)
        engine_poll_triggers(g.play, &live, &g.triggers, &g.fingers, focus.name, focus.owner);
    engine_play_tick(g.play, now);
    log_notes(&live);
    log_state(now);
    send_play_report(&g.play->last, g.play->output.mode, now);
    publish_play_state(&live, &focus);
    engine_arm_swallow(&g.hooks, &live, focus.name, focus.owner, g.armed, g.play);
    engine_hook_set_wake(&g.hooks, g.armed || engine_play_alive(g.play) > 0 || g.play->pending_count > 0);
}

static DWORD WINAPI ipc_thread(LPVOID param)
{
    (void)param;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    load_resident_profile();
    refresh_front();
    g.pipe_buf = (unsigned char *)malloc(ENGINE_PIPE_BYTES);
    if (!g.pipe_buf)
        atomic_store_explicit(&g.fail, 1, memory_order_release);
    else {
        HANDLE pipe = CreateNamedPipeW(ENGINE_PIPE_NAME, ENGINE_PIPE_ACCESS, ENGINE_PIPE_MODE,
                                       1, ENGINE_PIPE_BYTES, ENGINE_PIPE_BYTES,
                                       ENGINE_PIPE_TIMEOUT_MS, NULL);

        if (!pipe || pipe == INVALID_HANDLE_VALUE) {
            atomic_store_explicit(&g.fail, 1, memory_order_release);
        } else {
            g.pipe = pipe;
            open_session_window();
        }
    }
    atomic_store_explicit(&g.ipc_entered, 1, memory_order_release);
    while (!atomic_load_explicit(&g.quit, memory_order_acquire)) {
        DWORD now = GetTickCount();

        /* Reports first: a down and its up must leave before the window
           walk or a pipe read can delay them. */
        flush_report();
        log_flush();
        pump_window();
        refresh_front();
        if (g.windows_at == 0 || now - g.windows_at >= ENGINE_WINDOW_MS) {
            refresh_windows();
            g.windows_at = now;
        }
        if (g.pipe)
            poll_pipe();
        if (g.client) {
            flush_report();
            maybe_write_state();
        }
        if (atomic_load_explicit(&g.quit, memory_order_acquire))
            break;
        if (g.ipc_wake)
            ResetEvent(g.ipc_wake);
        if (atomic_load_explicit(&g.quit, memory_order_acquire))
            break;
        if (g.ipc_wake)
            WaitForSingleObject(g.ipc_wake, 8);
    }
    if (g.session) {
        DestroyWindow(g.session);
        g.session = NULL;
    }
    if (g.release_pipe)
        WaitForSingleObject(g.release_pipe, ENGINE_JOIN_MS);
    flush_report();
    log_flush();
    engine_profile_slot_shutdown(&g.profiles);
    close_pipe();
    free(g.pipe_buf);
    g.pipe_buf = NULL;
    return 0;
}

static DWORD WINAPI hook_thread(LPVOID param)
{
    (void)param;
    atomic_store_explicit(&g.hook_entered, 1, memory_order_release);
    engine_win_hook_run(&g.hook);
    return 0;
}

/* Read once, like AHK caches KeyDelayMs and KeyRepeatMs. A failed call
   keeps AHK's starting values, delay 1 and speed 31. */
static void read_key_timing(void)
{
    UINT delay = 1;
    DWORD speed = 31;

    SystemParametersInfoW(SPI_GETKEYBOARDDELAY, 0, &delay, 0);
    SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0);
    engine_play_set_key_timing(g.play, engine_key_delay_from_setting((int)delay),
                               engine_key_repeat_from_setting((int)speed));
}

static DWORD WINAPI playback_thread(LPVOID param)
{
    struct engine_slice_input slice;

    (void)param;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    if (engine_win_clock_open(&g.clock) != 0)
        atomic_store_explicit(&g.fail, 1, memory_order_release);
    if (g.play && g.clock.qpc_freq > 0)
        g.play->freq = g.clock.qpc_freq;
    atomic_store_explicit(&g.play_entered, 1, memory_order_release);
    memset(&slice, 0, sizeof slice);
    while (!atomic_load_explicit(&g.quit, memory_order_acquire)) {
        int reason = 0;
        int64_t now;

        if (!g.clock.timer || !g.play) {
            atomic_store_explicit(&g.fail, 1, memory_order_release);
            request_quit();
            break;
        }
        now = engine_win_clock_now(&g.clock);
        playback_tick(now);
        slice.armed = g.armed;
        slice.running = engine_play_alive(g.play) > 0;
        slice.pending = g.play->pending_count > 0;
        slice.next_deadline = next_deadline(now);
        if (engine_win_clock_slice(&g.clock, &slice, g.hook.wake, g.play_wake, &reason) != 0) {
            atomic_store_explicit(&g.fail, 1, memory_order_release);
            request_quit();
            break;
        }
    }
    release_keys();
    return 0;
}

static int wait_entered(atomic_int *flag)
{
    int i;

    for (i = 0; i < 3000; i++) {
        if (atomic_load_explicit(flag, memory_order_acquire))
            return 0;
        if (atomic_load_explicit(&g.quit, memory_order_acquire))
            return 1;
        Sleep(1);
    }
    return 1;
}

static HANDLE start_thread(LPTHREAD_START_ROUTINE proc)
{
    HANDLE thread = CreateThread(NULL, 0, proc, NULL, 0, NULL);

    if (!thread)
        atomic_store_explicit(&g.fail, 1, memory_order_release);
    return thread;
}

static BOOL WINAPI on_console(DWORD kind)
{
    if (kind != CTRL_C_EVENT && kind != CTRL_BREAK_EVENT && kind != CTRL_CLOSE_EVENT &&
        kind != CTRL_LOGOFF_EVENT && kind != CTRL_SHUTDOWN_EVENT)
        return FALSE;
    request_quit();
    if (g.exited)
        WaitForSingleObject(g.exited, ENGINE_JOIN_MS);
    return TRUE;
}

static void emergency_shutdown(void)
{
    release_keys();
    engine_hook_set_stop(&g.hooks, 1);
    engine_win_hook_uninstall(&g.hook);
    close_timer();
    close_pipe();
    close_mutex();
}

static LONG WINAPI on_exception(EXCEPTION_POINTERS *info)
{
    (void)info;
    if (atomic_exchange_explicit(&g.finished, 1, memory_order_acq_rel) == 0)
        emergency_shutdown();
    ExitProcess(ENGINE_EXIT_FAIL);
}

static int finish_resident(void)
{
    int i;

    if (atomic_exchange_explicit(&g.finished, 1, memory_order_acq_rel) != 0)
        return atomic_load_explicit(&g.fail, memory_order_acquire) ? ENGINE_EXIT_FAIL : ENGINE_EXIT_OK;

    request_quit();
    if (g.playback_thread) {
        if (WaitForSingleObject(g.playback_thread, ENGINE_JOIN_MS) != WAIT_OBJECT_0)
            atomic_store_explicit(&g.fail, 1, memory_order_release);
    }
    release_keys();

    engine_hook_set_stop(&g.hooks, 1);
    if (g.hook_thread) {
        for (i = 0; i < 50; i++) {
            if (engine_win_hook_kick(&g.hook) == 0)
                break;
            Sleep(1);
        }
        if (WaitForSingleObject(g.hook_thread, ENGINE_JOIN_MS) != WAIT_OBJECT_0)
            atomic_store_explicit(&g.fail, 1, memory_order_release);
    }
    engine_win_hook_close(&g.hook);

    close_timer();
    if (g.release_pipe)
        SetEvent(g.release_pipe);
    if (g.ipc_thread) {
        if (WaitForSingleObject(g.ipc_thread, ENGINE_JOIN_MS) != WAIT_OBJECT_0)
            atomic_store_explicit(&g.fail, 1, memory_order_release);
        else
            log_close();
    } else {
        log_close();
    }
    close_pipe();
    close_mutex();
    free(g.play);
    g.play = NULL;
    if (g.play_wake) {
        CloseHandle(g.play_wake);
        g.play_wake = NULL;
    }
    if (g.exited)
        SetEvent(g.exited);
    return atomic_load_explicit(&g.fail, memory_order_acquire) ? ENGINE_EXIT_FAIL : ENGINE_EXIT_OK;
}

static int run_resident(const struct engine_args *args)
{
    HANDLE mutex;

    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    memset(&g, 0, sizeof g);
    atomic_init(&g_log.head, 0u);
    atomic_init(&g_log.tail, 0u);
    atomic_init(&g_log.dropped, 0u);
    g_log.file = NULL;
    atomic_init(&g.quit, 0);
    atomic_init(&g.fail, 0);
    atomic_init(&g.keys_done, 0);
    atomic_init(&g.finished, 0);
    atomic_init(&g.ipc_entered, 0);
    atomic_init(&g.hook_entered, 0);
    atomic_init(&g.play_entered, 0);
    atomic_init(&g.front_seq, 0);
    if (args)
        g.args = *args;
    set_engine_log(g.args.profile);
    engine_log("engine start");
    engine_profile_slot_init(&g.profiles);
    engine_state_slot_init(&g.state_slot);
    engine_hook_init(&g.hooks);
    engine_output_init(&g.output);
    engine_cmd_ring_init(&g.cmds);
    engine_trigger_init(&g.triggers);
    engine_claims_init(&g.claims);
    engine_fingers_clear(&g.fingers);
    engine_report_queue_init(&g.reports);
    g.play = (struct engine_play *)calloc(1, sizeof *g.play);
    if (!g.play) {
        atomic_store_explicit(&g.fail, 1, memory_order_release);
        log_close();
        return ENGINE_EXIT_FAIL;
    }
    engine_play_init(g.play);
    read_key_timing();

    mutex = CreateMutexW(NULL, TRUE, L"Local\\MacroUI.Engine");
    if (!mutex) {
        log_close();
        free(g.play);
        g.play = NULL;
        return ENGINE_EXIT_FAIL;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        log_close();
        free(g.play);
        g.play = NULL;
        return ENGINE_EXIT_BUSY;
    }
    g.mutex = mutex;

    g.ipc_wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    g.play_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    g.release_pipe = CreateEventW(NULL, TRUE, FALSE, NULL);
    g.exited = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g.ipc_wake || !g.play_wake || !g.release_pipe || !g.exited ||
        engine_win_hook_open(&g.hook, &g.hooks) != 0) {
        atomic_store_explicit(&g.fail, 1, memory_order_release);
        return finish_resident();
    }
    SetConsoleCtrlHandler(on_console, TRUE);
    SetUnhandledExceptionFilter(on_exception);

    g.ipc_thread = start_thread(ipc_thread);
    if (!g.ipc_thread || wait_entered(&g.ipc_entered) != 0 ||
        atomic_load_explicit(&g.fail, memory_order_acquire))
        return finish_resident();

    g.hook_thread = start_thread(hook_thread);
    if (!g.hook_thread || wait_entered(&g.hook_entered) != 0 ||
        atomic_load_explicit(&g.fail, memory_order_acquire))
        return finish_resident();

    g.playback_thread = start_thread(playback_thread);
    if (!g.playback_thread || wait_entered(&g.play_entered) != 0 ||
        atomic_load_explicit(&g.fail, memory_order_acquire))
        return finish_resident();

    WaitForSingleObject(g.playback_thread, INFINITE);
    return finish_resident();
}

static int set_dpi(void)
{
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return 0;
    return 1;
}

static int write_version(void)
{
    char line[64];
    int len;
    HANDLE out;
    DWORD wrote = 0;

    len = snprintf(line, sizeof line, "%s\n", engine_version());
    if (len <= 0 || len >= (int)sizeof line)
        return ENGINE_EXIT_FAIL;
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE &&
        WriteFile(out, line, (DWORD)len, &wrote, NULL) && wrote == (DWORD)len)
        return ENGINE_EXIT_OK;
    if (printf("%s", line) < 0)
        return ENGINE_EXIT_FAIL;
    fflush(stdout);
    return ENGINE_EXIT_OK;
}

static int parse_command_line(struct engine_args *args)
{
    int argc = 0;
    LPWSTR *wide;
    char *storage = NULL;
    char *argv[ENGINE_ARG_CAP];
    int count = 0;
    int i;
    int mode;

    memset(args, 0, sizeof *args);
    wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide)
        return ENGINE_ARG_BAD;
    if (argc > ENGINE_ARG_CAP)
        argc = ENGINE_ARG_CAP;
    storage = (char *)calloc((size_t)argc, ENGINE_PROFILE_CAP);
    if (!storage && argc > 0) {
        LocalFree(wide);
        return ENGINE_ARG_BAD;
    }
    for (i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, storage + (size_t)i * ENGINE_PROFILE_CAP,
                                    ENGINE_PROFILE_CAP, NULL, NULL);

        argv[count++] = storage + (size_t)i * ENGINE_PROFILE_CAP;
        if (n <= 0)
            argv[count - 1][0] = '\0';
    }
    LocalFree(wide);
    mode = engine_args_parse(count, argv, args);
    free(storage);
    return mode;
}

int engine_win_main(void)
{
    struct engine_args args;
    int mode;

    if (set_dpi() != 0)
        return ENGINE_EXIT_FAIL;
    mode = parse_command_line(&args);
    if (mode == ENGINE_ARG_BAD)
        return ENGINE_EXIT_FAIL;
    if (mode == ENGINE_ARG_SELF_TEST)
        return write_version();
    if (mode == ENGINE_ARG_BENCH)
        return ENGINE_EXIT_OK;
    return run_resident(&args);
}
