/* #97 step 0. Does a mouse move that a WH_MOUSE_LL hook swallows still reach
   Raw Input in another process, the way a game reads the camera?
   And the wheel? Results are in README.md.

   move-probe raw <ms>      Raw Input reader (RIDEV_INPUTSINK). Prints the
                            summed relative X/Y and the event count, split by
                            real device and injected (hDevice 0).
   move-probe hook <ms>     Low-level mouse hook that swallows every move.
   move-probe inject <n>    n relative moves of +4 X, 5 ms apart.
   move-probe wheel <n>     n wheel notches down, 20 ms apart.
   move-probe cursor <ms>   Cursor X/Y at start and end. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long long raw_x[2];
static long long raw_y[2];
static long raw_n[2];
static long swallowed;
static long raw_wheel[2];
static long wheel_swallowed;

static LRESULT CALLBACK raw_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INPUT) {
        RAWINPUT ri;
        UINT size = sizeof ri;

        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            int k = ri.header.hDevice ? 0 : 1;

            if (ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
                raw_wheel[k]++;

            if (ri.data.mouse.lLastX || ri.data.mouse.lLastY) {
                raw_x[k] += ri.data.mouse.lLastX;
                raw_y[k] += ri.data.mouse.lLastY;
                raw_n[k]++;
            }
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK hook_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == WM_MOUSEMOVE) {
        swallowed++;
        return 1;
    }
    if (code == HC_ACTION && wp == WM_MOUSEWHEEL) {
        wheel_swallowed++;
        return 1;
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG m;

    while ((LONG)(end - GetTickCount()) > 0) {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
}

static int run_raw(DWORD ms)
{
    WNDCLASSW wc;
    RAWINPUTDEVICE dev;
    HWND hwnd;

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = raw_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MoveProbeRaw";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    dev.usUsagePage = 0x01;
    dev.usUsage = 0x02;
    dev.dwFlags = RIDEV_INPUTSINK;
    dev.hwndTarget = hwnd;
    if (!hwnd || !RegisterRawInputDevices(&dev, 1, sizeof dev)) {
        printf("raw register failed %lu\n", GetLastError());
        return 1;
    }
    printf("raw ready\n");
    fflush(stdout);
    pump(ms);
    printf("raw real n=%ld x=%lld y=%lld wheel=%ld | injected n=%ld x=%lld y=%lld wheel=%ld\n", raw_n[0],
           raw_x[0], raw_y[0], raw_wheel[0], raw_n[1], raw_x[1], raw_y[1], raw_wheel[1]);
    return 0;
}

static int run_hook(DWORD ms)
{
    HHOOK h = SetWindowsHookExW(WH_MOUSE_LL, hook_proc, GetModuleHandleW(NULL), 0);

    if (!h) {
        printf("hook failed %lu\n", GetLastError());
        return 1;
    }
    printf("hook ready\n");
    fflush(stdout);
    pump(ms);
    UnhookWindowsHookEx(h);
    printf("hook swallowed moves=%ld wheel=%ld\n", swallowed, wheel_swallowed);
    return 0;
}

static int run_inject(int n)
{
    int i;

    for (i = 0; i < n; i++) {
        INPUT in;

        memset(&in, 0, sizeof in);
        in.type = INPUT_MOUSE;
        in.mi.dx = 4;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &in, sizeof in);
        Sleep(5);
    }
    printf("inject sent=%d x=%d\n", n, n * 4);
    return 0;
}

static int run_wheel(int n)
{
    int i;

    for (i = 0; i < n; i++) {
        INPUT in;

        memset(&in, 0, sizeof in);
        in.type = INPUT_MOUSE;
        in.mi.mouseData = (DWORD)-WHEEL_DELTA;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        SendInput(1, &in, sizeof in);
        Sleep(20);
    }
    printf("wheel sent=%d\n", n);
    return 0;
}

static int run_cursor(DWORD ms)
{
    POINT a;
    POINT b;

    GetCursorPos(&a);
    Sleep(ms);
    GetCursorPos(&b);
    printf("cursor moved x=%ld y=%ld\n", b.x - a.x, b.y - a.y);
    return 0;
}

int main(int argc, char **argv)
{
    long v = argc > 2 ? strtol(argv[2], NULL, 10) : 0;

    timeBeginPeriod(1);
    if (argc > 2 && strcmp(argv[1], "raw") == 0)
        return run_raw((DWORD)v);
    if (argc > 2 && strcmp(argv[1], "hook") == 0)
        return run_hook((DWORD)v);
    if (argc > 2 && strcmp(argv[1], "inject") == 0)
        return run_inject((int)v);
    if (argc > 2 && strcmp(argv[1], "wheel") == 0)
        return run_wheel((int)v);
    if (argc > 2 && strcmp(argv[1], "cursor") == 0)
        return run_cursor((DWORD)v);
    printf("usage: move-probe raw|hook|cursor <ms> | inject|wheel <n>\n");
    return 2;
}
