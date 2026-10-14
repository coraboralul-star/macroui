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

static int write_frame(HANDLE pipe, const unsigned char *bytes, int length)
{
    int tries;

    if (!pipe || !bytes || length <= 0)
        return 1;
    for (tries = 0; tries < 4; tries++) {
        DWORD wrote = 0;

        if (WriteFile(pipe, bytes, (DWORD)length, &wrote, NULL))
            return 0;
        if (GetLastError() != ERROR_NO_DATA)
            return 1;
        Sleep(1);
    }
    return 1;
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
        if (connected || err == ERROR_PIPE_CONNECTED)
            g.client = 1;
        else
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

static DWORD WINAPI ipc_thread(LPVOID param)
{
    (void)param;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
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
        /* Disarmed, nothing running. The 16 ms slice also waits on the IPC wake.
           This thread does not advance a runner. */
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
    if (args)
        g.args = *args;
    /* #47 reads the path. This process does not open it and has no relative default. */
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
