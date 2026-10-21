/* The hook thread pumps. Playback does not.
   timeBeginPeriod and AvSetMmThreadCharacteristicsW are not called. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string.h>

#include "hook_win.h"

_Static_assert(WM_KEYDOWN == ENGINE_MSG_KEYDOWN, "keydown");
_Static_assert(WM_KEYUP == ENGINE_MSG_KEYUP, "keyup");
_Static_assert(WM_SYSKEYDOWN == ENGINE_MSG_SYSKEYDOWN, "syskeydown");
_Static_assert(WM_SYSKEYUP == ENGINE_MSG_SYSKEYUP, "syskeyup");
_Static_assert(WM_MOUSEMOVE == ENGINE_MSG_MOUSEMOVE, "move");
_Static_assert(WM_LBUTTONDOWN == ENGINE_MSG_LBUTTONDOWN, "ldown");
_Static_assert(WM_LBUTTONUP == ENGINE_MSG_LBUTTONUP, "lup");
_Static_assert(WM_RBUTTONDOWN == ENGINE_MSG_RBUTTONDOWN, "rdown");
_Static_assert(WM_RBUTTONUP == ENGINE_MSG_RBUTTONUP, "rup");
_Static_assert(WM_MBUTTONDOWN == ENGINE_MSG_MBUTTONDOWN, "mdown");
_Static_assert(WM_MBUTTONUP == ENGINE_MSG_MBUTTONUP, "mup");
_Static_assert(WM_MOUSEWHEEL == ENGINE_MSG_MOUSEWHEEL, "wheel");
_Static_assert(WM_XBUTTONDOWN == ENGINE_MSG_XBUTTONDOWN, "xdown");
_Static_assert(WM_XBUTTONUP == ENGINE_MSG_XBUTTONUP, "xup");
_Static_assert(WM_MOUSEHWHEEL == ENGINE_MSG_MOUSEHWHEEL, "hwheel");
_Static_assert(LLKHF_INJECTED == 0x10, "key injected");
_Static_assert(LLKHF_LOWER_IL_INJECTED == 0x02, "key lower");
_Static_assert(LLKHF_UP == 0x80, "key up");
_Static_assert(LLKHF_EXTENDED == 0x01, "key extended");
_Static_assert(LLMHF_INJECTED == 0x01, "mouse injected");
_Static_assert(LLMHF_LOWER_IL_INJECTED == 0x02, "mouse lower");

static struct engine_hook_state *g_state;
static struct engine_win_hook *g_hook;

static void bind(struct engine_win_hook *hook)
{
    g_hook = hook;
    g_state = hook ? hook->state : NULL;
}

static int64_t qpc_now(void)
{
    LARGE_INTEGER now;

    if (!QueryPerformanceCounter(&now) || now.QuadPart <= 0)
        return 1;
    return now.QuadPart;
}

static void stamp_key(struct engine_win_hook *hook)
{
    if (!hook || !hook->state)
        return;
    atomic_store_explicit(&hook->state->key_stamp, (int_least64_t)qpc_now(),
                          memory_order_release);
}

static void stamp_mouse(struct engine_win_hook *hook)
{
    if (!hook || !hook->state)
        return;
    atomic_store_explicit(&hook->state->mouse_stamp, (int_least64_t)qpc_now(),
                          memory_order_release);
}

static void publish_key(struct engine_win_hook *hook, HHOOK handle)
{
    hook->key = handle;
    if (hook->state)
        atomic_store_explicit(&hook->state->key_installed, handle ? 1 : 0,
                              memory_order_release);
}

static void publish_mouse(struct engine_win_hook *hook, HHOOK handle)
{
    hook->mouse = handle;
    if (hook->state)
        atomic_store_explicit(&hook->state->mouse_installed, handle ? 1 : 0,
                              memory_order_release);
}

static void clear_decision(struct engine_hook_decision *out)
{
    if (!out)
        return;
    out->swallow = 0;
    out->enqueued = 0;
    out->wake = 0;
    out->mask_win = 0;
}

static LRESULT CALLBACK key_proc(int nCode, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK mouse_proc(int nCode, WPARAM wParam, LPARAM lParam);

static void raise_key(struct engine_win_hook *hook)
{
    HHOOK handle;

    /* Unhook first. A handle Windows has already removed still looks valid. */
    if (hook->key) {
        UnhookWindowsHookEx(hook->key);
        hook->key = NULL;
    }
    handle = SetWindowsHookExW(WH_KEYBOARD_LL, key_proc, GetModuleHandleW(NULL), 0);
    publish_key(hook, handle);
    if (handle)
        stamp_key(hook);
}

static void raise_mouse(struct engine_win_hook *hook, int need_mouse)
{
    HHOOK handle = NULL;

    if (hook->mouse) {
        UnhookWindowsHookEx(hook->mouse);
        hook->mouse = NULL;
    }
    if (need_mouse)
        handle = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, GetModuleHandleW(NULL), 0);
    publish_mouse(hook, handle);
    if (handle)
        stamp_mouse(hook);
}

int engine_win_on_key(struct engine_hook_state *st, int nCode, uintptr_t msg,
                      const KBDLLHOOKSTRUCT *info, struct engine_hook_decision *out)
{
    struct engine_hook_sample sample;

    clear_decision(out);
    if (nCode < 0 || !st || !info || !out)
        return 0;
    memset(&sample, 0, sizeof sample);
    sample.kind = ENGINE_HOOK_KEY;
    sample.msg = (uint32_t)msg;
    sample.vk = info->vkCode;
    sample.scan = info->scanCode;
    sample.flags = info->flags;
    sample.extra = (uint64_t)info->dwExtraInfo;
    sample.now = qpc_now();
    engine_hook_classify(st, &sample, out);
    return out->swallow ? 1 : 0;
}

int engine_win_on_mouse(struct engine_hook_state *st, int nCode, uintptr_t msg,
                        const MSLLHOOKSTRUCT *info, struct engine_hook_decision *out)
{
    struct engine_hook_sample sample;

    clear_decision(out);
    if (nCode < 0 || !st || !info || !out)
        return 0;
    memset(&sample, 0, sizeof sample);
    sample.kind = ENGINE_HOOK_MOUSE;
    sample.msg = (uint32_t)msg;
    sample.flags = info->flags;
    sample.mouse_data = info->mouseData;
    sample.extra = (uint64_t)info->dwExtraInfo;
    sample.now = qpc_now();
    engine_hook_classify(st, &sample, out);
    return out->swallow ? 1 : 0;
}

/* AHK #MenuMaskKey. The real Win up is swallowed. vkE8 down and up, then
   the Win up, go out tagged so this hook passes them and they are not a
   finger. Windows then sees a key between the Win down and up and does
   not open Start. */
static void send_menu_mask(uint16_t win)
{
    INPUT in[3];
    int i;

    memset(in, 0, sizeof in);
    for (i = 0; i < 3; i++) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.dwExtraInfo = (ULONG_PTR)ENGINE_EXTRA_ECHO;
    }
    in[0].ki.wVk = (WORD)ENGINE_MENU_MASK_VK;
    in[1].ki.wVk = (WORD)ENGINE_MENU_MASK_VK;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    in[2].ki.wVk = (WORD)win;
    in[2].ki.wScan = (WORD)MapVirtualKeyW(win, MAPVK_VK_TO_VSC);
    in[2].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
    SendInput(3, in, (int)sizeof in[0]);
}

static LRESULT CALLBACK key_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    struct engine_hook_decision decision;

    if (nCode >= 0 && g_state && lParam) {
        engine_win_on_key(g_state, nCode, (uintptr_t)wParam,
                          (const KBDLLHOOKSTRUCT *)lParam, &decision);
        if (decision.wake && g_hook && g_hook->wake)
            SetEvent(g_hook->wake);
        if (decision.mask_win)
            send_menu_mask(decision.mask_win);
        if (decision.swallow)
            return 1;
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static LRESULT CALLBACK mouse_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    struct engine_hook_decision decision;

    if (nCode >= 0 && g_state && lParam) {
        if (engine_win_on_mouse(g_state, nCode, (uintptr_t)wParam,
                                (const MSLLHOOKSTRUCT *)lParam, &decision)) {
            if (decision.wake && g_hook && g_hook->wake)
                SetEvent(g_hook->wake);
            return 1;
        }
        if (decision.wake && g_hook && g_hook->wake)
            SetEvent(g_hook->wake);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int engine_win_hook_open(struct engine_win_hook *hook, struct engine_hook_state *state)
{
    HANDLE wake;

    if (!hook || !state)
        return 1;
    memset(hook, 0, sizeof *hook);
    atomic_init(&hook->thread_id, 0u);
    atomic_init(&hook->generation, 0u);
    hook->state = state;
    wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!wake)
        return 1;
    hook->wake = wake;
    return 0;
}

void engine_win_hook_uninstall(struct engine_win_hook *hook)
{
    if (!hook)
        return;
    if (hook->key) {
        UnhookWindowsHookEx(hook->key);
        hook->key = NULL;
    }
    if (hook->mouse) {
        UnhookWindowsHookEx(hook->mouse);
        hook->mouse = NULL;
    }
    publish_key(hook, NULL);
    publish_mouse(hook, NULL);
}

void engine_win_hook_close(struct engine_win_hook *hook)
{
    if (!hook)
        return;
    engine_win_hook_uninstall(hook);
    if (hook->wake)
        CloseHandle(hook->wake);
    hook->wake = NULL;
    if (g_hook == hook) {
        g_hook = NULL;
        g_state = NULL;
    }
    hook->state = NULL;
}

int engine_win_hook_install(struct engine_win_hook *hook)
{
    if (!hook || !hook->state)
        return 1;
    bind(hook);
    raise_key(hook);
    return hook->key ? 0 : 1;
}

void engine_win_hook_drain(struct engine_win_hook *hook)
{
    struct engine_hook_watch_out plan;
    int need;
    int i;

    if (!hook || !hook->state)
        return;
    if (!engine_hook_take_ops(hook->state, &plan))
        return;
    need = atomic_load_explicit(&hook->state->need_mouse, memory_order_acquire);
    for (i = 0; i < plan.count; i++) {
        if (plan.op[i] == ENGINE_HOOK_RAISE_KEY)
            raise_key(hook);
        else if (plan.op[i] == ENGINE_HOOK_RAISE_MOUSE)
            raise_mouse(hook, need);
    }
    atomic_fetch_add_explicit(&hook->generation, 1u, memory_order_release);
}

int engine_win_hook_kick(struct engine_win_hook *hook)
{
    DWORD id;

    if (!hook)
        return 1;
    id = atomic_load_explicit(&hook->thread_id, memory_order_acquire);
    if (id == 0)
        return 1;
    return PostThreadMessageW(id, WM_APP, 0, 0) ? 0 : 1;
}

void engine_win_hook_run(struct engine_win_hook *hook)
{
    MSG msg;

    if (!hook || !hook->state)
        return;
    PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
    atomic_store_explicit(&hook->thread_id, GetCurrentThreadId(), memory_order_release);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    bind(hook);
    engine_win_hook_install(hook);
    while (!atomic_load_explicit(&hook->state->stop, memory_order_acquire)) {
        BOOL got = GetMessageW(&msg, NULL, 0, 0);

        if (got == 0 || got == (BOOL)-1)
            break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        engine_win_hook_drain(hook);
    }
    engine_win_hook_uninstall(hook);
    atomic_store_explicit(&hook->thread_id, 0u, memory_order_release);
}
