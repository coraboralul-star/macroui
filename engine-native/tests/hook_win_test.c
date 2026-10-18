/* Parses hook structs and installs a hook that passes keys through.
   This is not a latency report. */

#include "hook_win.h"

#include <stdio.h>
#include <string.h>

static struct engine_hook_state state;
static struct engine_win_hook hook;

static int fail(const char *name)
{
    fprintf(stderr, "fail %s\n", name);
    return 1;
}

static int pop_ok(struct engine_finger *finger, uint16_t id, int down)
{
    if (!engine_event_pop(&state, finger))
        return 0;
    return finger->id == id && finger->down == down;
}

static int check_structs(void)
{
    KBDLLHOOKSTRUCT key;
    MSLLHOOKSTRUCT mouse;
    struct engine_hook_decision decision;
    struct engine_finger finger;
    uint8_t *bits;

    engine_hook_init(&state);
    memset(&key, 0, sizeof key);
    key.vkCode = 0x42;
    key.scanCode = 0x30;
    if (engine_win_on_key(&state, 0, WM_KEYDOWN, &key, &decision) != 0)
        return fail("letter does not swallow yet");
    if (!decision.enqueued || !pop_ok(&finger, 0x42, 1))
        return fail("letter struct queued");

    engine_hook_init(&state);
    key.flags = LLKHF_INJECTED;
    engine_win_on_key(&state, 0, WM_KEYDOWN, &key, &decision);
    if (decision.enqueued)
        return fail("injected struct is not a finger");
    if (atomic_load_explicit(&state.key_stamp, memory_order_acquire) == 0)
        return fail("injected struct did not stamp");

    engine_hook_init(&state);
    memset(&key, 0, sizeof key);
    key.vkCode = 0x42;
    key.dwExtraInfo = (ULONG_PTR)ENGINE_EXTRA_ECHO;
    engine_win_on_key(&state, 0, WM_KEYDOWN, &key, &decision);
    if (decision.enqueued || decision.swallow)
        return fail("echo struct is not a finger");

    engine_hook_init(&state);
    memset(&key, 0, sizeof key);
    key.vkCode = VK_SHIFT;
    key.scanCode = 0x36;
    engine_win_on_key(&state, 0, WM_KEYDOWN, &key, &decision);
    if (!pop_ok(&finger, 0xA1, 1))
        return fail("right shift struct");

    engine_hook_init(&state);
    memset(&key, 0, sizeof key);
    key.vkCode = VK_RETURN;
    key.flags = LLKHF_EXTENDED;
    engine_win_on_key(&state, 0, WM_KEYDOWN, &key, &decision);
    if (!pop_ok(&finger, ENGINE_KEY_NUMPAD_ENTER, 1))
        return fail("numpad enter struct");

    engine_hook_init(&state);
    memset(&key, 0, sizeof key);
    key.vkCode = 0x42;
    bits = engine_swallow_begin(&state);
    engine_swallow_set(bits, 0x42, 1, 0);
    engine_swallow_publish(&state);
    if (engine_win_on_key(&state, 0, WM_KEYDOWN, &key, &decision) != 1)
        return fail("swallow from the struct");
    if (engine_win_on_key(&state, -1, WM_KEYDOWN, &key, &decision) != 0)
        return fail("negative code passes");
    if (atomic_load_explicit(&state.event_tail, memory_order_acquire)
        == atomic_load_explicit(&state.event_head, memory_order_acquire) + 1u)
        ;
    else
        return fail("negative code still queued");

    engine_hook_init(&state);
    memset(&mouse, 0, sizeof mouse);
    engine_win_on_mouse(&state, 0, WM_MOUSEMOVE, &mouse, &decision);
    if (decision.enqueued)
        return fail("move struct is not an event");
    if (atomic_load_explicit(&state.mouse_stamp, memory_order_acquire) != 0)
        return fail("move struct stamped");

    bits = engine_swallow_begin(&state);
    engine_swallow_set(bits, ENGINE_KEY_LBUTTON, 1, 0);
    engine_swallow_publish(&state);
    if (engine_win_on_mouse(&state, 0, WM_LBUTTONDOWN, &mouse, &decision) != 1)
        return fail("a mapped mouse button was passed");
    if (!pop_ok(&finger, ENGINE_KEY_LBUTTON, 1))
        return fail("left button struct");

    memset(&mouse, 0, sizeof mouse);
    mouse.mouseData = 1u << 16;
    if (engine_win_on_mouse(&state, 0, WM_XBUTTONUP, &mouse, &decision) != 0)
        return fail("an unmapped side button was swallowed");
    if (!pop_ok(&finger, ENGINE_KEY_XBUTTON1, 0))
        return fail("x1 up struct");
    return 0;
}

static DWORD WINAPI hook_thread(LPVOID param)
{
    engine_win_hook_run((struct engine_win_hook *)param);
    return 0;
}

static int wait_flag(atomic_int *flag, int want, int ms)
{
    int i;

    for (i = 0; i < ms; i++) {
        if (atomic_load_explicit(flag, memory_order_acquire) == want)
            return 1;
        Sleep(1);
    }
    return 0;
}

static int wait_gen(unsigned min_gen, int ms)
{
    int i;

    for (i = 0; i < ms; i++) {
        if (atomic_load_explicit(&hook.generation, memory_order_acquire) >= min_gen)
            return 1;
        Sleep(1);
    }
    return 0;
}

static int stop_and_join(HANDLE thread)
{
    DWORD wait;

    engine_hook_set_stop(&state, 1);
    engine_win_hook_kick(&hook);
    wait = WaitForSingleObject(thread, 3000);
    CloseHandle(thread);
    if (wait != WAIT_OBJECT_0)
        return fail("hook thread");
    engine_win_hook_close(&hook);
    if (atomic_load_explicit(&state.key_installed, memory_order_acquire) != 0)
        return fail("keyboard hook left installed");
    if (atomic_load_explicit(&state.mouse_installed, memory_order_acquire) != 0)
        return fail("mouse hook left installed");
    return 0;
}

static int check_live(void)
{
    struct engine_hook_watch_out plan;
    HANDLE thread;

    engine_hook_init(&state);
    if (engine_win_hook_open(&hook, &state) != 0)
        return fail("open");
    thread = CreateThread(NULL, 0, hook_thread, &hook, 0, NULL);
    if (!thread) {
        engine_win_hook_close(&hook);
        return fail("thread");
    }
    if (!wait_flag(&state.key_installed, 1, 3000)) {
        stop_and_join(thread);
        return fail("install");
    }
    if (atomic_load_explicit(&state.mouse_installed, memory_order_acquire) != 0) {
        stop_and_join(thread);
        return fail("mouse hook installed before it was needed");
    }

    engine_hook_set_need_mouse(&state, 1);
    memset(&plan, 0, sizeof plan);
    plan.count = 1;
    plan.op[0] = ENGINE_HOOK_RAISE_MOUSE;
    engine_hook_post_ops(&state, &plan);
    if (engine_win_hook_kick(&hook) != 0 || !wait_gen(1, 3000)) {
        stop_and_join(thread);
        return fail("mouse raise");
    }
    if (atomic_load_explicit(&state.mouse_installed, memory_order_acquire) != 1) {
        stop_and_join(thread);
        return fail("mouse hook was not put back");
    }

    memset(&plan, 0, sizeof plan);
    plan.count = 1;
    plan.op[0] = ENGINE_HOOK_RAISE_KEY;
    engine_hook_post_ops(&state, &plan);
    if (engine_win_hook_kick(&hook) != 0 || !wait_gen(2, 3000)) {
        stop_and_join(thread);
        return fail("keyboard reraise");
    }
    if (atomic_load_explicit(&state.key_installed, memory_order_acquire) != 1) {
        stop_and_join(thread);
        return fail("keyboard hook was not put back");
    }
    return stop_and_join(thread);
}

int main(void)
{
    if (check_structs() != 0)
        return 1;
    if (check_live() != 0)
        return 1;
    printf("ok\n");
    return 0;
}
