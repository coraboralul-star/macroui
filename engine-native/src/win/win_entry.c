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
    char front[ENGINE_PROFILE_FOCUS_CAP];
    char front_pub[ENGINE_PROFILE_FOCUS_CAP];
    atomic_uint front_seq;
    char prefer[ENGINE_ID_CAP];
    uint64_t prefer_seq;
};

static struct engine_resident g;

static void request_quit(void)
{
    atomic_store_explicit(&g.quit, 1, memory_order_release);
    if (g.ipc_wake)
        SetEvent(g.ipc_wake);
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
    if (atomic_exchange_explicit(&g.keys_done, 1, memory_order_acq_rel) != 0)
        return;
    engine_win_release_output(&g.output);
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

static int put_str(unsigned char *buf, int cap, int *at, const char *text)
{
    size_t n = text ? strlen(text) : 0;

    if (n > 4096)
        n = 4096;
    if (!at || *at < 0 || *at + 2 + (int)n > cap)
        return 1;
    buf[*at] = (unsigned char)(n & 0xffu);
    buf[*at + 1] = (unsigned char)((n >> 8) & 0xffu);
    if (n)
        memcpy(buf + *at + 2, text, n);
    *at += 2 + (int)n;
    return 0;
}

/* Hello, then one state. A failed state write is not a dropped client.
   Running, held, and windows stay empty until playback publishes them. */
static int greet_client(void)
{
    struct engine_state_snapshot snap;
    unsigned char hello[13] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
    };
    unsigned char state[512];
    unsigned int payload;
    int at;

    engine_state_read(&g.state_slot, &snap);
    hello[12] = snap.armed ? 1 : 0;
    if (write_frame(g.pipe, hello, (int)sizeof hello) != 0)
        return 1;

    memset(state, 0, sizeof state);
    memcpy(state, hello, 12);
    state[6] = 2;
    state[7] = 0;
    state[12] = snap.armed ? 1 : 0;
    at = 20;
    if (put_str(state, (int)sizeof state, &at, snap.active_id) != 0 ||
        put_str(state, (int)sizeof state, &at, snap.front) != 0)
        return 0;
    payload = (unsigned int)(at - 12);
    state[8] = (unsigned char)(payload & 0xffu);
    state[9] = (unsigned char)((payload >> 8) & 0xffu);
    state[10] = (unsigned char)((payload >> 16) & 0xffu);
    state[11] = (unsigned char)((payload >> 24) & 0xffu);
    write_frame_tries(g.pipe, state, at, 1);
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
    int kind = engine_pipe_kind(msg, length);
    int ack_len = 0;
    const unsigned char *ack = NULL;

    if (kind == ENGINE_PIPE_QUIT)
        ack = engine_ack_command(&ack_len);
    else if (kind == ENGINE_PIPE_JSON)
        ack = engine_ack_bad_frame(&ack_len);
    else if (kind == ENGINE_PIPE_BAD_VERSION)
        ack = engine_ack_bad_version(&ack_len);
    else
        return;
    if (write_frame(g.pipe, ack, ack_len) != 0 && kind == ENGINE_PIPE_QUIT) {
        atomic_store_explicit(&g.fail, 1, memory_order_release);
        request_quit();
        return;
    }
    if (kind == ENGINE_PIPE_BAD_VERSION)
        drop_client();
    if (kind == ENGINE_PIPE_QUIT) {
        /* The ack is already written. The flag is next, then playback wakes. */
        request_quit();
    }
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

static void publish_front_name(const char *name)
{
    unsigned seq = atomic_load_explicit(&g.front_seq, memory_order_relaxed);

    atomic_store_explicit(&g.front_seq, seq + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    snprintf(g.front_pub, sizeof g.front_pub, "%s", name ? name : "");
    atomic_store_explicit(&g.front_seq, seq + 2u, memory_order_release);
}

static void read_front_name(char *dst, size_t cap)
{
    int tries;

    if (!dst || cap == 0)
        return;
    for (tries = 0; tries < 8; tries++) {
        unsigned seq = atomic_load_explicit(&g.front_seq, memory_order_acquire);
        char tmp[ENGINE_PROFILE_FOCUS_CAP];

        if (seq & 1u)
            continue;
        memcpy(tmp, g.front_pub, sizeof tmp);
        tmp[sizeof tmp - 1u] = '\0';
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&g.front_seq, memory_order_relaxed) != seq)
            continue;
        snprintf(dst, cap, "%s", tmp);
        return;
    }
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

/* Foreground image name for PickLive. This stays on the IPC thread. */
static void refresh_front(void)
{
    HWND hwnd;
    DWORD pid = 0;
    HANDLE proc;
    wchar_t wide[1024];
    DWORD n = (DWORD)(sizeof wide / sizeof wide[0]);
    char name[ENGINE_PROFILE_FOCUS_CAP];

    name[0] = '\0';
    hwnd = GetForegroundWindow();
    if (hwnd && GetWindowThreadProcessId(hwnd, &pid) && pid) {
        proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (proc) {
            if (QueryFullProcessImageNameW(proc, 0, wide, &n))
                copy_image_base(wide, name, sizeof name);
            CloseHandle(proc);
        }
    }
    publish_front_name(name);
}

static void load_resident_profile(void)
{
    wchar_t wide[ENGINE_PROFILE_CAP];
    HANDLE file;
    LARGE_INTEGER size;
    char *buf;
    DWORD got = 0;
    int chars;

    /* No --profile is the empty document. There is no relative default. */
    if (!g.args.profile[0]) {
        engine_profile_slot_load_missing(&g.profiles);
        return;
    }
    chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, g.args.profile, -1, wide,
                                ENGINE_PROFILE_CAP);
    if (chars <= 0) {
        engine_profile_slot_fail(&g.profiles);
        return;
    }
    file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();

        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            engine_profile_slot_load_missing(&g.profiles);
        else
            engine_profile_slot_fail(&g.profiles);
        return;
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (unsigned long long)size.QuadPart > ENGINE_PROFILE_TEXT_MAX) {
        CloseHandle(file);
        engine_profile_slot_fail(&g.profiles);
        return;
    }
    buf = (char *)malloc((size_t)size.QuadPart + 1u);
    if (!buf) {
        CloseHandle(file);
        engine_profile_slot_fail(&g.profiles);
        return;
    }
    if (size.QuadPart > 0 &&
        (!ReadFile(file, buf, (DWORD)size.QuadPart, &got, NULL) || got != (DWORD)size.QuadPart)) {
        CloseHandle(file);
        free(buf);
        engine_profile_slot_fail(&g.profiles);
        return;
    }
    CloseHandle(file);
    buf[got] = '\0';
    engine_profile_slot_load_text(&g.profiles, buf, got);
    free(buf);
}

static void adopt_profile(void)
{
    const struct engine_profile_image *image;
    struct engine_live_config live;
    uint64_t seq;

    image = engine_profile_slot_adopt(&g.profiles);
    seq = image ? image->sequence : 0;
    if (seq != g.prefer_seq) {
        g.prefer_seq = seq;
        snprintf(g.prefer, sizeof g.prefer, "%s", image ? image->active_id : "");
    }
    read_front_name(g.front, sizeof g.front);
    engine_profile_pick(image, g.front, g.prefer, &live);
    snprintf(g.prefer, sizeof g.prefer, "%s", live.prefer);
    /* Connect writes hello and one state. Later publishes do not write a frame. */
    engine_state_publish(&g.state_slot, 0, live.active_id, g.front);
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
        pump_window();
        refresh_front();
        if (g.pipe)
            poll_pipe();
        if (g.ipc_wake)
            WaitForSingleObject(g.ipc_wake, 8);
    }
    if (g.session) {
        DestroyWindow(g.session);
        g.session = NULL;
    }
    if (g.release_pipe)
        WaitForSingleObject(g.release_pipe, ENGINE_JOIN_MS);
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

static DWORD WINAPI playback_thread(LPVOID param)
{
    struct engine_slice_input slice;

    (void)param;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    if (engine_win_clock_open(&g.clock) != 0)
        atomic_store_explicit(&g.fail, 1, memory_order_release);
    atomic_store_explicit(&g.play_entered, 1, memory_order_release);
    memset(&slice, 0, sizeof slice);
    while (!atomic_load_explicit(&g.quit, memory_order_acquire)) {
        int reason = 0;

        if (!g.clock.timer) {
            atomic_store_explicit(&g.fail, 1, memory_order_release);
            request_quit();
            break;
        }
        /* Adopt first, then read the foreground name the IPC thread published.
           This tick does not allocate, does not query a window, and does not
           advance a runner. The 16 ms slice also waits on the IPC wake. */
        adopt_profile();
        if (engine_win_clock_slice(&g.clock, &slice, g.hook.wake, g.ipc_wake, &reason) != 0) {
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
    }
    close_pipe();
    close_mutex();
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
    engine_profile_slot_init(&g.profiles);
    engine_state_slot_init(&g.state_slot);
    engine_hook_init(&g.hooks);
    engine_output_init(&g.output);

    mutex = CreateMutexW(NULL, TRUE, L"Local\\MacroUI.Engine");
    if (!mutex)
        return ENGINE_EXIT_FAIL;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return ENGINE_EXIT_BUSY;
    }
    g.mutex = mutex;

    g.ipc_wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    g.release_pipe = CreateEventW(NULL, TRUE, FALSE, NULL);
    g.exited = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g.ipc_wake || !g.release_pipe || !g.exited ||
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
