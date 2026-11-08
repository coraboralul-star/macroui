/* Opt-in Windows experiment. Hold a gate in a named window and this process
   injects the opposite of half the physical relative counts.

   It is not wired into either macro engine. AutoHotkey reads raw input on the
   same thread as its low-level hook, so a SendInput there can stall that hook.
   This file installs no hook. The board is not a counter path: a board packet
   is excluded so it cannot feed the next counter.

   Default is idle. Motion is countered only while the gate is held and the
   foreground exe was named on the command line. Release, focus loss, End, and
   process exit drop any fractional remainder and send nothing further. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slow_math.h"

#define SLOW_ECHO 0x56454E44u
#define SLOW_FOCUS_CAP 8

#ifndef MOUSEEVENTF_MOVE_NOCOALESCE
#define MOUSEEVENTF_MOVE_NOCOALESCE 0x2000
#endif

static struct slow_session g_session;
static DWORD g_tid;
static volatile LONG g_quit;
static HWND g_focus_hwnd;
static int g_focus_known;
static int g_focus_match;
static char g_focus_name[64] = "?";
static wchar_t g_focus[SLOW_FOCUS_CAP][64];
static int g_focus_n;
static int g_gate_key;
static int g_gate_vk;
static int g_panic_vk = VK_END;
static USHORT g_gate_down = RI_MOUSE_BUTTON_5_DOWN;
static USHORT g_gate_up = RI_MOUSE_BUTTON_5_UP;
static const char *g_gate_label = "xbutton2";
static const char *g_panic_label = "End";

static long long g_phys_x, g_phys_y, g_counter_x, g_counter_y, g_drop;
static unsigned long long g_nulls, g_boards, g_abs, g_still, g_off, g_full, g_fail, g_sent;

struct board_slot {
    HANDLE device;
    int board;
    int known;
};

static struct board_slot g_boards[16];

static int same_text(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;

        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int same_wide(const wchar_t *a, const wchar_t *b)
{
    while (*a && *b) {
        wchar_t ca = *a;
        wchar_t cb = *b;

        if (ca >= L'A' && ca <= L'Z')
            ca = (wchar_t)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z')
            cb = (wchar_t)(cb - L'A' + L'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void usage(void)
{
    printf("slow-probe [--gate GATE] [--focus EXE]... [--panic KEY]\n");
    printf("  GATE   xbutton2 (default), xbutton1, mbutton, or a key:\n");
    printf("         a-z, 0-9, lshift, rshift, lctrl, rctrl, lalt, ralt,\n");
    printf("         space, capslock, tab, f1-f12\n");
    printf("  EXE    foreground file name. Default Overwatch.exe. Repeatable.\n");
    printf("  KEY    end (default), pause, delete, esc, or the same key names.\n");
    printf("Hold GATE in a named window. The button or key is not swallowed.\n");
    printf("End, or the panic key, and Ctrl+C leave. --self-test prints and exits.\n");
}

static int parse_key(const char *text, int *vk, const char **label)
{
    if (!text || !text[0])
        return 0;
    if (same_text(text, "end")) {
        *vk = VK_END;
        *label = "End";
        return 1;
    }
    if (same_text(text, "pause")) {
        *vk = VK_PAUSE;
        *label = "Pause";
        return 1;
    }
    if (same_text(text, "delete")) {
        *vk = VK_DELETE;
        *label = "Delete";
        return 1;
    }
    if (same_text(text, "esc") || same_text(text, "escape")) {
        *vk = VK_ESCAPE;
        *label = "Esc";
        return 1;
    }
    if (same_text(text, "space")) {
        *vk = VK_SPACE;
        *label = "Space";
        return 1;
    }
    if (same_text(text, "capslock")) {
        *vk = VK_CAPITAL;
        *label = "CapsLock";
        return 1;
    }
    if (same_text(text, "tab")) {
        *vk = VK_TAB;
        *label = "Tab";
        return 1;
    }
    if (same_text(text, "lshift")) {
        *vk = VK_LSHIFT;
        *label = "LShift";
        return 1;
    }
    if (same_text(text, "rshift")) {
        *vk = VK_RSHIFT;
        *label = "RShift";
        return 1;
    }
    if (same_text(text, "lctrl")) {
        *vk = VK_LCONTROL;
        *label = "LCtrl";
        return 1;
    }
    if (same_text(text, "rctrl")) {
        *vk = VK_RCONTROL;
        *label = "RCtrl";
        return 1;
    }
    if (same_text(text, "lalt")) {
        *vk = VK_LMENU;
        *label = "LAlt";
        return 1;
    }
    if (same_text(text, "ralt")) {
        *vk = VK_RMENU;
        *label = "RAlt";
        return 1;
    }
    if (text[0] == 'f' || text[0] == 'F') {
        int n = 0;
        const char *p = text + 1;

        if (*p < '0' || *p > '9')
            return 0;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            p++;
        }
        if (*p || n < 1 || n > 12)
            return 0;
        *vk = VK_F1 + (n - 1);
        *label = text;
        return 1;
    }
    if (text[1] != 0)
        return 0;
    if ((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z')) {
        *vk = text[0] >= 'a' ? text[0] - 'a' + 'A' : text[0];
        *label = text;
        return 1;
    }
    if (text[0] >= '0' && text[0] <= '9') {
        *vk = text[0];
        *label = text;
        return 1;
    }
    return 0;
}

static int parse_gate(const char *text)
{
    if (same_text(text, "xbutton2") || same_text(text, "mouse5")) {
        g_gate_key = 0;
        g_gate_down = RI_MOUSE_BUTTON_5_DOWN;
        g_gate_up = RI_MOUSE_BUTTON_5_UP;
        g_gate_label = "xbutton2";
        return 1;
    }
    if (same_text(text, "xbutton1") || same_text(text, "mouse4")) {
        g_gate_key = 0;
        g_gate_down = RI_MOUSE_BUTTON_4_DOWN;
        g_gate_up = RI_MOUSE_BUTTON_4_UP;
        g_gate_label = "xbutton1";
        return 1;
    }
    if (same_text(text, "mbutton") || same_text(text, "middle") || same_text(text, "mouse3")) {
        g_gate_key = 0;
        g_gate_down = RI_MOUSE_MIDDLE_BUTTON_DOWN;
        g_gate_up = RI_MOUSE_MIDDLE_BUTTON_UP;
        g_gate_label = "mbutton";
        return 1;
    }
    g_gate_key = 1;
    if (!parse_key(text, &g_gate_vk, &g_gate_label))
        return 0;
    g_gate_label = text;
    return 1;
}

static int add_focus(const char *text)
{
    int i;

    if (!text || !text[0] || g_focus_n >= SLOW_FOCUS_CAP)
        return 0;
    for (i = 0; text[i]; i++) {
        unsigned char c = (unsigned char)text[i];

        if (i >= 63 || c < 32 || c > 126)
            return 0;
        g_focus[g_focus_n][i] = (wchar_t)c;
    }
    g_focus[g_focus_n][i] = 0;
    g_focus_n++;
    return 1;
}

static int parse_args(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (same_text(argv[i], "--gate")) {
            if (i + 1 >= argc || !parse_gate(argv[++i]))
                return 0;
        } else if (same_text(argv[i], "--focus")) {
            if (i + 1 >= argc || !add_focus(argv[++i]))
                return 0;
        } else if (same_text(argv[i], "--panic")) {
            if (i + 1 >= argc || !parse_key(argv[++i], &g_panic_vk, &g_panic_label))
                return 0;
        } else {
            return 0;
        }
    }
    if (g_focus_n == 0 && !add_focus("Overwatch.exe"))
        return 0;
    if (g_gate_key && g_gate_vk == g_panic_vk)
        return 0;
    return 1;
}

static void print_line(const char *tag)
{
    printf("%s armed=%d focus=%s gate=%s phys=%lld,%lld counter=%lld,%lld sent=%llu null=%llu board=%llu abs=%llu still=%llu off=%llu full=%llu drop=%lld fail=%llu\n",
           tag, g_session.armed, g_focus_name, g_gate_label, g_phys_x, g_phys_y, g_counter_x, g_counter_y, g_sent,
           g_nulls, g_boards, g_abs, g_still, g_off, g_full, g_drop, g_fail);
    fflush(stdout);
}

static const wchar_t *base_name(const wchar_t *path)
{
    const wchar_t *base = path;
    const wchar_t *p;

    for (p = path; *p; p++) {
        if (*p == L'\\' || *p == L'/')
            base = p + 1;
    }
    return base;
}

static void copy_base(const wchar_t *wide)
{
    int i;

    for (i = 0; i < 63 && wide && wide[i]; i++) {
        wchar_t c = wide[i];

        g_focus_name[i] = (c > 0 && c < 128) ? (char)c : '?';
    }
    g_focus_name[i] = 0;
    if (!g_focus_name[0])
        memcpy(g_focus_name, "?", 2);
}

static int focus_matches(void)
{
    HWND hwnd = GetForegroundWindow();
    DWORD pid = 0;
    wchar_t path[512];
    DWORD n = 512;
    HANDLE process;
    const wchar_t *base;
    int i;
    int match = 0;

    if (g_focus_known && hwnd == g_focus_hwnd)
        return g_focus_match;
    g_focus_known = 1;
    g_focus_hwnd = hwnd;
    if (!hwnd) {
        memcpy(g_focus_name, "?", 2);
        g_focus_match = 0;
        printf("focus ? match=0\n");
        fflush(stdout);
        return 0;
    }
    GetWindowThreadProcessId(hwnd, &pid);
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process || !QueryFullProcessImageNameW(process, 0, path, &n)) {
        if (process)
            CloseHandle(process);
        memcpy(g_focus_name, "?", 2);
        g_focus_match = 0;
        printf("focus ? match=0\n");
        fflush(stdout);
        return 0;
    }
    CloseHandle(process);
    base = base_name(path);
    copy_base(base);
    for (i = 0; i < g_focus_n; i++) {
        if (same_wide(base, g_focus[i]))
            match = 1;
    }
    g_focus_match = match;
    printf("focus %s match=%d\n", g_focus_name, match);
    fflush(stdout);
    return match;
}

static int wide_is_board(const wchar_t *name)
{
    char buf[512];
    int i;

    if (!name)
        return 0;
    for (i = 0; i < 511 && name[i]; i++) {
        wchar_t c = name[i];

        buf[i] = (c > 0 && c < 128) ? (char)c : '?';
    }
    buf[i] = 0;
    return slow_text_has_board(buf);
}

static int device_is_board(HANDLE device)
{
    int i;
    int free_slot = -1;
    wchar_t name[512];
    UINT size = 512;
    int board;

    if (!device)
        return 0;
    for (i = 0; i < 16; i++) {
        if (g_boards[i].known && g_boards[i].device == device)
            return g_boards[i].board;
        if (!g_boards[i].known && free_slot < 0)
            free_slot = i;
    }
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name, &size) == (UINT)-1)
        board = 0;
    else
        board = wide_is_board(name);
    if (free_slot >= 0) {
        g_boards[free_slot].known = 1;
        g_boards[free_slot].device = device;
        g_boards[free_slot].board = board;
    }
    return board;
}

static void inject(int32_t dx, int32_t dy)
{
    INPUT input;

    memset(&input, 0, sizeof input);
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE;
    input.mi.dwExtraInfo = (ULONG_PTR)SLOW_ECHO;
    if (SendInput(1, &input, (int)sizeof input) == 1)
        g_sent++;
    else
        g_fail++;
}

static void note_skip(const struct slow_result *result)
{
    if (result->skip == SLOW_SKIP_NULL)
        g_nulls++;
    else if (result->skip == SLOW_SKIP_BOARD)
        g_boards++;
    else if (result->skip == SLOW_SKIP_ABSOLUTE)
        g_abs++;
    else if (result->skip == SLOW_SKIP_STILL)
        g_still++;
    else if (result->skip == SLOW_SKIP_DISARMED)
        g_off++;
    else if (result->skip == SLOW_SKIP_FULL)
        g_full++;
}

static void apply_event(struct slow_event *event)
{
    struct slow_result result;
    int was = g_session.armed;

    slow_session_on_raw(&g_session, event, &result);
    g_drop += result.discarded;
    if (event->motion && result.skip == SLOW_APPLIED) {
        g_phys_x += event->dx;
        g_phys_y += event->dy;
        g_counter_x += result.dx;
        g_counter_y += result.dy;
        if (result.dx != 0 || result.dy != 0)
            inject(result.dx, result.dy);
    } else if (event->motion) {
        note_skip(&result);
    }
    if (!was && result.armed)
        print_line("arm");
    if (result.edge == SLOW_EDGE_RELEASE)
        print_line("disarm-release");
    else if (result.edge == SLOW_EDGE_FOCUS)
        print_line("disarm-focus");
    else if (result.edge == SLOW_EDGE_PANIC)
        print_line("disarm-panic");
    else if (result.edge == SLOW_EDGE_EXIT)
        print_line("disarm-exit");
    if (result.quit)
        PostQuitMessage(0);
}

static int gate_edge(USHORT flags)
{
    int up = (flags & g_gate_up) != 0;
    int down = (flags & g_gate_down) != 0;

    if (g_gate_key)
        return SLOW_GATE_NONE;
    if (up)
        return SLOW_GATE_UP;
    if (down)
        return SLOW_GATE_DOWN;
    return SLOW_GATE_NONE;
}

static void on_input(HRAWINPUT handle)
{
    RAWINPUT input;
    UINT size = sizeof input;
    struct slow_event event;

    if (GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;
    memset(&event, 0, sizeof event);
    event.focus = focus_matches();
    if (input.header.dwType == RIM_TYPEKEYBOARD) {
        USHORT vk = input.data.keyboard.VKey;
        int up = (input.data.keyboard.Flags & RI_KEY_BREAK) != 0;

        if (!input.header.hDevice)
            return;
        if (!up && vk == (USHORT)g_panic_vk)
            event.panic = 1;
        if (g_gate_key && vk == (USHORT)g_gate_vk)
            event.gate = up ? SLOW_GATE_UP : SLOW_GATE_DOWN;
        apply_event(&event);
        return;
    }
    if (input.header.dwType != RIM_TYPEMOUSE)
        return;
    event.motion = 1;
    event.device = (uint64_t)(uintptr_t)input.header.hDevice;
    event.absolute = (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0;
    event.board = device_is_board(input.header.hDevice);
    event.dx = (int32_t)input.data.mouse.lLastX;
    event.dy = (int32_t)input.data.mouse.lLastY;
    event.gate = gate_edge(input.data.mouse.usButtonFlags);
    apply_event(&event);
}

static LRESULT CALLBACK probe_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INPUT)
        on_input((HRAWINPUT)lp);
    else if (msg == WM_TIMER)
        print_line(g_session.armed ? "tick" : "idle");
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL WINAPI on_ctrl(DWORD kind)
{
    (void)kind;
    InterlockedExchange(&g_quit, 1);
    if (g_tid)
        PostThreadMessageW(g_tid, WM_QUIT, 0, 0);
    return TRUE;
}

static int register_usage(HWND hwnd, USHORT usage, int on)
{
    RAWINPUTDEVICE dev;

    memset(&dev, 0, sizeof dev);
    dev.usUsagePage = 1;
    dev.usUsage = usage;
    dev.dwFlags = on ? RIDEV_INPUTSINK : RIDEV_REMOVE;
    dev.hwndTarget = on ? hwnd : NULL;
    return RegisterRawInputDevices(&dev, 1, sizeof dev) ? 0 : 1;
}

static void banner(void)
{
    int i;

    printf("slow-probe experiment: software counter-motion at %d/%d.\n", SLOW_KEEP, SLOW_DEN);
    printf("Not device DPI, not in-game sensitivity, not Windows pointer speed.\n");
    printf("Gate %s is observed and is not swallowed, so the game sees it too. Panic %s, or Ctrl+C.\n",
           g_gate_label, g_panic_label);
    printf("Focus:");
    for (i = 0; i < g_focus_n; i++)
        printf(" %ls", g_focus[i]);
    printf("\n");
    printf("Other windows are left alone. Board output and the AHK engine are not this path.\n");
    printf("Release, focus loss, panic, and exit drop a fractional remainder.\n");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG msg;
    int i;

    for (i = 1; i < argc; i++) {
        if (same_text(argv[i], "--self-test")) {
            printf("slow-probe 0\n");
            return 0;
        }
        if (same_text(argv[i], "--help") || same_text(argv[i], "-h")) {
            usage();
            return 0;
        }
    }
    if (!parse_args(argc, argv)) {
        usage();
        return 2;
    }
    slow_session_init(&g_session);
    banner();

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = probe_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SlowProbeRaw";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        printf("window class failed %lu\n", GetLastError());
        return 1;
    }
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        printf("window failed %lu\n", GetLastError());
        return 1;
    }
    g_tid = GetCurrentThreadId();
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    if (register_usage(hwnd, 0x02, 1) || register_usage(hwnd, 0x06, 1)) {
        printf("raw input unavailable %lu\n", GetLastError());
        register_usage(hwnd, 0x02, 0);
        register_usage(hwnd, 0x06, 0);
        return 1;
    }
    if (!SetTimer(hwnd, 1, 1000, NULL)) {
        printf("timer failed %lu\n", GetLastError());
        register_usage(hwnd, 0x02, 0);
        register_usage(hwnd, 0x06, 0);
        return 1;
    }
    print_line("ready");
    while (InterlockedCompareExchange(&g_quit, 0, 0) == 0 && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    {
        struct slow_event event;
        struct slow_result result;

        memset(&event, 0, sizeof event);
        event.leaving = 1;
        event.focus = 1;
        slow_session_on_raw(&g_session, &event, &result);
        g_drop += result.discarded;
        print_line(InterlockedCompareExchange(&g_quit, 0, 0) ? "ctrl" : "exit");
    }
    register_usage(hwnd, 0x02, 0);
    register_usage(hwnd, 0x06, 0);
    return 0;
}
