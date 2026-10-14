/* Spawns the resident process. A second instance must exit 2.
   The job object kills that process if this test stops early.
   This file does not arm profiles/default.json. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "process.h"
#include "win_entry.h"

static int fails;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "fail %s\n", name);
        fails++;
    }
}

static int mutex_held(void)
{
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\MacroUI.Engine");

    if (!mutex)
        return 0;
    CloseHandle(mutex);
    return 1;
}

static int engine_path(wchar_t *path, DWORD cap)
{
    DWORD n = GetModuleFileNameW(NULL, path, cap);
    wchar_t *slash;

    if (n == 0 || n >= cap)
        return 1;
    slash = wcsrchr(path, L'\\');
    if (!slash)
        return 1;
    if ((size_t)(cap - (DWORD)(slash + 1 - path)) < 20)
        return 1;
    if (wcscpy_s(slash + 1, cap - (DWORD)(slash + 1 - path), L"macroui-engine.exe") != 0)
        return 1;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return 1;
    return 0;
}

struct child {
    PROCESS_INFORMATION pi;
    HANDLE job;
    HANDLE out_read;
};

static void child_close(struct child *child, int kill)
{
    if (!child)
        return;
    if (kill && child->pi.hProcess) {
        TerminateProcess(child->pi.hProcess, 1);
        WaitForSingleObject(child->pi.hProcess, 3000);
    }
    if (child->out_read)
        CloseHandle(child->out_read);
    if (child->pi.hThread)
        CloseHandle(child->pi.hThread);
    if (child->pi.hProcess)
        CloseHandle(child->pi.hProcess);
    if (child->job)
        CloseHandle(child->job);
    memset(child, 0, sizeof *child);
}

static int child_start(struct child *child, const wchar_t *exe, const wchar_t *args, int capture)
{
    wchar_t cmd[4096];
    size_t used = 0;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW si;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    HANDLE in_nul = NULL;
    HANDLE out_write = NULL;

    memset(child, 0, sizeof *child);
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    cmd[0] = 0;
    if (wcschr(exe, L'"'))
        return 1;
    if (used + 3 >= sizeof cmd / sizeof cmd[0])
        return 1;
    cmd[used++] = L'"';
    if (wcscpy_s(cmd + used, (sizeof cmd / sizeof cmd[0]) - used, exe) != 0)
        return 1;
    used += wcslen(cmd + used);
    if (used + 2 >= sizeof cmd / sizeof cmd[0])
        return 1;
    cmd[used++] = L'"';
    cmd[used] = 0;
    if (args && args[0]) {
        if (used + 1 + wcslen(args) + 1 >= sizeof cmd / sizeof cmd[0])
            return 1;
        cmd[used++] = L' ';
        if (wcscpy_s(cmd + used, (sizeof cmd / sizeof cmd[0]) - used, args) != 0)
            return 1;
    }

    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (capture) {
        if (!CreatePipe(&child->out_read, &out_write, &sa, 0))
            return 1;
        SetHandleInformation(child->out_read, HANDLE_FLAG_INHERIT, 0);
        in_nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, NULL);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = in_nul;
        si.hStdOutput = out_write;
        si.hStdError = out_write;
    }
    if (!CreateProcessW(exe, cmd, NULL, NULL, capture ? TRUE : FALSE, CREATE_NO_WINDOW, NULL, NULL,
                        &si, &child->pi)) {
        if (out_write)
            CloseHandle(out_write);
        if (in_nul)
            CloseHandle(in_nul);
        child_close(child, 1);
        return 1;
    }
    if (out_write)
        CloseHandle(out_write);
    if (in_nul)
        CloseHandle(in_nul);

    child->job = CreateJobObjectW(NULL, NULL);
    if (child->job) {
        memset(&info, 0, sizeof info);
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(child->job, JobObjectExtendedLimitInformation, &info, sizeof info) ||
            !AssignProcessToJobObject(child->job, child->pi.hProcess)) {
            CloseHandle(child->job);
            child->job = NULL;
        }
    }
    return 0;
}

static int child_code(struct child *child, DWORD timeout, int *code, char *out, int out_cap)
{
    DWORD wait;
    DWORD status = 1;
    int got = 0;

    if (out && out_cap > 0)
        out[0] = 0;
    wait = WaitForSingleObject(child->pi.hProcess, timeout);
    if (child->out_read && out && out_cap > 1) {
        for (;;) {
            DWORD n = 0;
            DWORD room = (DWORD)(out_cap - 1 - got);

            if (room == 0)
                break;
            if (!ReadFile(child->out_read, out + got, room, &n, NULL) || n == 0)
                break;
            got += (int)n;
            out[got] = 0;
        }
    }
    if (wait != WAIT_OBJECT_0)
        return 1;
    if (!GetExitCodeProcess(child->pi.hProcess, &status))
        return 1;
    *code = (int)status;
    return 0;
}

static int run_args(const wchar_t *exe, const wchar_t *args, DWORD timeout, int *code, char *out,
                    int out_cap)
{
    struct child child;
    int rc;

    if (child_start(&child, exe, args, out != NULL) != 0)
        return 1;
    rc = child_code(&child, timeout, code, out, out_cap);
    child_close(&child, rc != 0);
    return rc;
}

static void trim_line(char *text)
{
    size_t n;
    char *start;

    if (!text)
        return;
    start = text;
    while (*start == ' ' || *start == '\r' || *start == '\n' || *start == '\t')
        start++;
    if (start != text)
        memmove(text, start, strlen(start) + 1);
    n = strlen(text);
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\r' || text[n - 1] == '\n' ||
                     text[n - 1] == '\t')) {
        text[--n] = 0;
    }
}

static int read_ack(HANDLE pipe, unsigned char *buf, int cap, int *got)
{
    DWORD start = GetTickCount();

    *got = 0;
    for (;;) {
        DWORD avail = 0;
        DWORD n = 0;

        if (GetTickCount() - start > 2000)
            return 1;
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL))
            return 1;
        if (avail == 0) {
            Sleep(0);
            continue;
        }
        if (!ReadFile(pipe, buf, (DWORD)cap, &n, NULL))
            return 1;
        *got = (int)n;
        return 0;
    }
}

static int send_quit(void)
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    DWORD mode = PIPE_READMODE_MESSAGE;
    const unsigned char *ack;
    unsigned char got[64];
    int ack_len = 0;
    int n = 0;
    int i;
    static const unsigned char quit[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04
    };

    for (i = 0; i < 50; i++) {
        pipe = CreateFileW(L"\\\\.\\pipe\\MacroUI", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE)
            break;
        Sleep(50);
    }
    if (pipe == INVALID_HANDLE_VALUE)
        return 1;
    if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
        CloseHandle(pipe);
        return 1;
    }
    {
        DWORD wrote = 0;

        if (!WriteFile(pipe, quit, (DWORD)sizeof quit, &wrote, NULL) || wrote != sizeof quit) {
            CloseHandle(pipe);
            return 1;
        }
    }
    ack = engine_ack_command(&ack_len);
    if (read_ack(pipe, got, (int)sizeof got, &n) != 0 || n != ack_len || memcmp(got, ack, (size_t)n) != 0) {
        CloseHandle(pipe);
        return 1;
    }
    CloseHandle(pipe);
    return 0;
}

static void check_release(void)
{
    struct engine_output out;

    engine_output_init(&out);
    expect(engine_win_release_output(&out) == 0, "nothing applied is not sent");

    out.applied[0] = 0x87;
    out.applied_count = 1;
    expect(engine_win_release_output(&out) > 0, "F24 up is sent");
    expect(out.applied_count == 0, "software applied is cleared");

    engine_output_init(&out);
    out.mode = ENGINE_OUTPUT_RP2040;
    out.applied[0] = 0x87;
    out.applied_count = 1;
    expect(engine_win_release_output(&out) == 0, "a board release does not call SendInput");
    expect(out.applied_count == 0, "board applied is cleared");
}

static void check_resident(const wchar_t *exe)
{
    struct child first;
    struct child second;
    wchar_t profile[MAX_PATH];
    wchar_t args[MAX_PATH + 32];
    DWORD len;
    int code = -1;
    int i;
    int saw = 0;

    len = GetTempPathW(MAX_PATH, profile);
    if (len == 0 || len + 28 >= MAX_PATH) {
        expect(0, "temp path");
        return;
    }
    if (wcscat_s(profile, MAX_PATH, L"macroui-48-absent.json") != 0) {
        expect(0, "profile path");
        return;
    }
    DeleteFileW(profile);
    if (swprintf_s(args, MAX_PATH + 32, L"--profile \"%s\"", profile) < 0) {
        expect(0, "profile args");
        return;
    }
    if (wcschr(profile, L'"')) {
        expect(0, "profile quote");
        return;
    }

    if (child_start(&first, exe, args, 0) != 0) {
        expect(0, "start resident");
        return;
    }
    for (i = 0; i < 50; i++) {
        if (mutex_held()) {
            saw = 1;
            break;
        }
        if (WaitForSingleObject(first.pi.hProcess, 0) == WAIT_OBJECT_0)
            break;
        Sleep(50);
    }
    expect(saw, "resident takes the mutex");
    if (!saw) {
        child_close(&first, 1);
        expect(GetFileAttributesW(profile) == INVALID_FILE_ATTRIBUTES, "absent profile was not created");
        return;
    }

    if (child_start(&second, exe, NULL, 0) != 0) {
        expect(0, "start second");
    } else if (child_code(&second, 3000, &code, NULL, 0) != 0) {
        expect(0, "second exits");
        child_close(&second, 1);
    } else {
        expect(code == ENGINE_EXIT_BUSY, "second exits busy");
        child_close(&second, 0);
    }
    expect(WaitForSingleObject(first.pi.hProcess, 0) == WAIT_TIMEOUT, "the holder stays up");

    expect(send_quit() == 0, "quit ack");
    if (child_code(&first, 5000, &code, NULL, 0) != 0) {
        expect(0, "resident exits");
        child_close(&first, 1);
    } else {
        expect(code == ENGINE_EXIT_OK, "resident exit 0");
        child_close(&first, 0);
    }
    expect(!mutex_held(), "mutex is free after exit");
    expect(GetFileAttributesW(profile) == INVALID_FILE_ATTRIBUTES, "profile path is not opened");
}

int main(void)
{
    wchar_t exe[1024];
    char text[128];
    int code = -1;
    int user_held;
    HANDLE mine;

    if (engine_path(exe, 1024) != 0) {
        fprintf(stderr, "fail find engine\n");
        return 1;
    }
    user_held = mutex_held();

    text[0] = 0;
    if (run_args(exe, L"--self-test", 5000, &code, text, (int)sizeof text) != 0) {
        expect(0, "self-test ran");
    } else {
        trim_line(text);
        expect(code == ENGINE_EXIT_OK, "self-test exit");
        expect(strcmp(text, "macroui-engine 0") == 0, "self-test version");
    }
    if (run_args(exe, L"--bench", 5000, &code, NULL, 0) != 0)
        expect(0, "bench ran");
    else
        expect(code == ENGINE_EXIT_OK, "bench exit");
    if (run_args(exe, L"--profile", 5000, &code, NULL, 0) != 0)
        expect(0, "bad profile ran");
    else
        expect(code == ENGINE_EXIT_FAIL, "bad profile exit");
    if (run_args(exe, L"--nope", 5000, &code, NULL, 0) != 0)
        expect(0, "unknown ran");
    else
        expect(code == ENGINE_EXIT_FAIL, "unknown exit");
    if (!user_held)
        expect(!mutex_held(), "early exits do not take the mutex");

    mine = CreateMutexW(NULL, TRUE, L"Local\\MacroUI.Engine");
    if (!mine) {
        expect(0, "hold mutex");
    } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mine);
        if (run_args(exe, NULL, 5000, &code, NULL, 0) != 0)
            expect(0, "busy spawn");
        else
            expect(code == ENGINE_EXIT_BUSY, "already held exits busy");
    } else {
        if (run_args(exe, NULL, 5000, &code, NULL, 0) != 0)
            expect(0, "busy spawn");
        else
            expect(code == ENGINE_EXIT_BUSY, "held by the test exits busy");
        ReleaseMutex(mine);
        CloseHandle(mine);
        expect(!mutex_held(), "test holder released the mutex");
        check_resident(exe);
    }

    check_release();
    if (!user_held)
        expect(!mutex_held(), "mutex was not left held");
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
