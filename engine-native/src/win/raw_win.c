/* Raw Input thread. See raw_win.h. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string.h>
#include <wchar.h>

#include "raw_win.h"

#define ENGINE_RAW_WANT (WM_APP + 1)

static struct engine_win_raw *g_raw;

void engine_win_raw_open(struct engine_win_raw *raw, struct engine_raw_mouse *tally)
{
    if (!raw)
        return;
    memset(raw, 0, sizeof *raw);
    atomic_init(&raw->thread_id, 0u);
    atomic_init(&raw->status, ENGINE_RAW_OFF);
    raw->tally = tally;
}

int engine_win_raw_is_board(const wchar_t *name)
{
    static const wchar_t want[] = L"VID_CAFE&PID_4004";
    size_t n = wcslen(want);
    const wchar_t *p;

    if (!name)
        return 0;
    for (p = name; *p; p++) {
        if (_wcsnicmp(p, want, n) == 0)
            return 1;
    }
    return 0;
}

static int from_board(HANDLE device)
{
    wchar_t name[512];
    UINT size = (UINT)(sizeof name / sizeof name[0]);

    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name, &size) == (UINT)-1)
        return 0;
    name[(sizeof name / sizeof name[0]) - 1] = 0;
    return engine_win_raw_is_board(name);
}

static void on_input(struct engine_win_raw *raw, HRAWINPUT handle)
{
    RAWINPUT input;
    UINT size = sizeof input;
    USHORT flags;

    if (GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;
    if (input.header.dwType != RIM_TYPEMOUSE)
        return;
    flags = input.data.mouse.usButtonFlags;
    if ((flags & (RI_MOUSE_MIDDLE_BUTTON_UP | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP)) == 0)
        return;
    /* SendInput has no device. The board is the engine's own output. */
    if (!input.header.hDevice || from_board(input.header.hDevice))
        return;
    if (flags & RI_MOUSE_MIDDLE_BUTTON_UP)
        engine_raw_mouse_up(raw->tally, ENGINE_KEY_MBUTTON);
    if (flags & RI_MOUSE_BUTTON_4_UP)
        engine_raw_mouse_up(raw->tally, ENGINE_KEY_XBUTTON1);
    if (flags & RI_MOUSE_BUTTON_5_UP)
        engine_raw_mouse_up(raw->tally, ENGINE_KEY_XBUTTON2);
}

static LRESULT CALLBACK raw_proc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INPUT && g_raw)
        on_input(g_raw, (HRAWINPUT)lParam);
    return DefWindowProcW(window, msg, wParam, lParam);
}

static void set_want(struct engine_win_raw *raw, int on)
{
    RAWINPUTDEVICE dev;

    memset(&dev, 0, sizeof dev);
    dev.usUsagePage = 1;
    dev.usUsage = 2;
    if (on) {
        dev.dwFlags = RIDEV_INPUTSINK;
        dev.hwndTarget = raw->window;
    } else {
        dev.dwFlags = RIDEV_REMOVE;
    }
    if (!on) {
        engine_raw_mouse_set_ok(raw->tally, 0);
        if (atomic_load_explicit(&raw->status, memory_order_acquire) == ENGINE_RAW_ON)
            RegisterRawInputDevices(&dev, 1, sizeof dev);
        atomic_store_explicit(&raw->status, ENGINE_RAW_OFF, memory_order_release);
        return;
    }
    if (raw->window && RegisterRawInputDevices(&dev, 1, sizeof dev)) {
        engine_raw_mouse_set_ok(raw->tally, 1);
        atomic_store_explicit(&raw->status, ENGINE_RAW_ON, memory_order_release);
    } else {
        engine_raw_mouse_set_ok(raw->tally, 0);
        atomic_store_explicit(&raw->status, ENGINE_RAW_FAILED, memory_order_release);
    }
}

void engine_win_raw_run(struct engine_win_raw *raw)
{
    WNDCLASSW wc;
    MSG msg;

    if (!raw || !raw->tally)
        return;
    g_raw = raw;
    PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = raw_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"MacroUI.EngineRaw";
    RegisterClassW(&wc);
    raw->window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                  wc.hInstance, NULL);
    atomic_store_explicit(&raw->thread_id, GetCurrentThreadId(), memory_order_release);
    for (;;) {
        BOOL got = GetMessageW(&msg, NULL, 0, 0);

        if (got == 0 || got == (BOOL)-1)
            break;
        if (msg.hwnd == NULL && msg.message == ENGINE_RAW_WANT) {
            set_want(raw, msg.wParam != 0);
            continue;
        }
        DispatchMessageW(&msg);
    }
    set_want(raw, 0);
    if (raw->window)
        DestroyWindow(raw->window);
    raw->window = NULL;
    atomic_store_explicit(&raw->thread_id, 0u, memory_order_release);
    g_raw = NULL;
}

int engine_win_raw_want(struct engine_win_raw *raw, int on)
{
    DWORD id;

    if (!raw)
        return 1;
    id = atomic_load_explicit(&raw->thread_id, memory_order_acquire);
    if (id == 0)
        return 1;
    return PostThreadMessageW(id, ENGINE_RAW_WANT, on ? 1 : 0, 0) ? 0 : 1;
}

int engine_win_raw_stop(struct engine_win_raw *raw)
{
    DWORD id;

    if (!raw)
        return 1;
    id = atomic_load_explicit(&raw->thread_id, memory_order_acquire);
    if (id == 0)
        return 1;
    return PostThreadMessageW(id, WM_QUIT, 0, 0) ? 0 : 1;
}
