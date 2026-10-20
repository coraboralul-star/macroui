#include "hook.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "fail %s\n", name);
        fails++;
    }
}

static struct engine_hook_state state;

static void reset(void)
{
    engine_hook_init(&state);
}

static struct engine_hook_sample key_sample(uint32_t vk, uint32_t scan, uint32_t flags,
                                            uint32_t msg, int64_t now)
{
    struct engine_hook_sample sample;

    memset(&sample, 0, sizeof sample);
    sample.kind = ENGINE_HOOK_KEY;
    sample.vk = vk;
    sample.scan = scan;
    sample.flags = flags;
    sample.msg = msg;
    sample.now = now;
    return sample;
}

static void check_names(void)
{
    expect(engine_key_name(0) == NULL, "empty id has no name");
    expect(engine_key_name(0x01) == NULL, "unnamed id is omitted");
    expect(strcmp(engine_key_name(0x42), "b") == 0, "vk b");
    expect(strcmp(engine_key_name(0x20), "space") == 0, "vk space");
    expect(strcmp(engine_key_name(0x70), "f1") == 0, "vk f1");
    expect(strcmp(engine_key_name(0x7B), "f12") == 0, "vk f12");
    expect(strcmp(engine_key_name(0xA0), "LShift") == 0, "vk left shift");
    expect(strcmp(engine_key_name(0xA1), "RShift") == 0, "vk right shift");
    expect(strcmp(engine_key_name(0xA2), "LCtrl") == 0, "ctrl name is LCtrl");
    expect(strcmp(engine_key_name(0x0D), "enter") == 0, "vk enter");
    expect(strcmp(engine_key_name(ENGINE_KEY_NUMPAD_ENTER), "numpadenter") == 0, "numpad enter");
    expect(strcmp(engine_key_name(ENGINE_KEY_LBUTTON), "LButton") == 0, "left button");
    expect(strcmp(engine_key_name(ENGINE_KEY_XBUTTON2), "XButton2") == 0, "x2");
    expect(strcmp(engine_key_name(0x30), "0") == 0, "digit");
}

static void check_resolve(void)
{
    expect(engine_resolve_key(0x10, 0x36, 0) == 0xA1, "right scan is RShift");
    expect(engine_resolve_key(0x10, 0x2A, 0) == 0xA0, "left scan is LShift");
    expect(engine_resolve_key(0x11, 0x1D, 1) == 0xA3, "extended ctrl is RCtrl");
    expect(engine_resolve_key(0x11, 0x1D, 0) == 0xA2, "ctrl is LCtrl");
    expect(engine_resolve_key(0x12, 0, 1) == 0xA5, "extended alt is RAlt");
    expect(engine_resolve_key(0x12, 0, 0) == 0xA4, "alt is LAlt");
    expect(engine_resolve_key(0x5B, 0, 0) == 0x5B, "left win");
    expect(engine_resolve_key(0x5C, 0, 0) == 0x5C, "right win");
    expect(engine_resolve_key(0x0D, 0x1C, 1) == ENGINE_KEY_NUMPAD_ENTER, "numpad enter id");
    expect(engine_resolve_key(0x0D, 0x1C, 0) == 0x0D, "enter stays enter");
    expect(engine_resolve_key(0x42, 0x30, 0) == 0x42, "letter stays the virtual key");
    expect(engine_resolve_key(0, 0, 0) == 0, "vk 0 is empty");
    expect(engine_is_mouse_button(ENGINE_KEY_MBUTTON) == 1, "middle is a mouse button");
    expect(engine_is_mouse_button(0x42) == 0, "a letter is not a mouse button");
    expect(engine_resolve_mouse(ENGINE_MSG_XBUTTONDOWN, 1u << 16, NULL) == ENGINE_KEY_XBUTTON1,
           "high word 1 is mouse 4");
    expect(engine_resolve_mouse(ENGINE_MSG_XBUTTONDOWN, 2u << 16, NULL) == ENGINE_KEY_XBUTTON2,
           "high word 2 is mouse 5");
    expect(engine_resolve_mouse(ENGINE_MSG_XBUTTONDOWN, 1u, NULL) == ENGINE_KEY_XBUTTON1,
           "low word 1 is mouse 4");
    expect(engine_resolve_mouse(ENGINE_MSG_XBUTTONDOWN, 2u, NULL) == ENGINE_KEY_XBUTTON2,
           "low word 2 is mouse 5");
    expect(engine_resolve_mouse(ENGINE_MSG_XBUTTONUP, 2u, NULL) == ENGINE_KEY_XBUTTON2,
           "mouse 5 up uses the same word");
    expect(engine_repeat_is_missed_up(ENGINE_KEY_XBUTTON1, 1, 1) == 1,
           "a second mouse 4 down is a missed up");
    expect(engine_repeat_is_missed_up(ENGINE_KEY_XBUTTON2, 1, 1) == 1,
           "a second mouse 5 down is a missed up");
    expect(engine_repeat_is_missed_up(0x45, 1, 1) == 0, "a keyboard repeat is not a missed up");
    expect(engine_repeat_is_missed_up(ENGINE_KEY_XBUTTON2, 1, 0) == 0,
           "the first mouse 5 down is not a missed up");
}

static void check_trigger_rules(void)
{
    struct engine_swallow_rule rule;
    int swallow = 0;
    int pass = 0;

    expect(engine_can_trigger(0) == 0, "an empty trigger is not a key");
    expect(engine_can_trigger(0x42) == 1, "a letter can trigger");
    expect(engine_can_trigger(ENGINE_KEY_MBUTTON) == 1, "middle can trigger");
    expect(engine_can_trigger(ENGINE_KEY_XBUTTON1) == 1, "side button can trigger");
    expect(engine_can_trigger(ENGINE_KEY_LBUTTON) == 0, "left click is not a trigger");
    expect(engine_can_trigger(ENGINE_KEY_RBUTTON) == 0, "right click is not a trigger");
    expect(engine_can_trigger(ENGINE_KEY_XBUTTON2) == 1, "second side button can trigger");
    expect(engine_can_trigger(ENGINE_KEY_PAUSE) == 0, "pause is not a trigger");
    expect(engine_panic_edge(ENGINE_KEY_PAUSE, 0, 1) == 1, "pause down is panic");
    expect(engine_panic_edge(ENGINE_KEY_PAUSE, 1, 1) == 0, "pause repeat is not a new panic");
    expect(engine_panic_edge(ENGINE_KEY_PAUSE, 1, 0) == 0, "pause up is not panic");
    expect(engine_panic_edge(0x42, 0, 1) == 0, "a letter is not panic");

    rule.is_trigger = 1;
    rule.focus_ok = 1;
    rule.pass = 0;
    engine_swallow_for_trigger(&rule, &swallow, &pass);
    expect(swallow == 1 && pass == 0, "software and board swallow a focused trigger");
    rule.focus_ok = 0;
    engine_swallow_for_trigger(&rule, &swallow, &pass);
    expect(swallow == 0, "unmatched focus does not swallow");
    rule.focus_ok = 1;
    rule.pass = 1;
    engine_swallow_for_trigger(&rule, &swallow, &pass);
    expect(swallow == 0 && pass == 1, "a pass count lets the key through");
    rule.is_trigger = 0;
    rule.pass = 0;
    engine_swallow_for_trigger(&rule, &swallow, &pass);
    expect(swallow == 0, "a key that is not a trigger is not swallowed");
}

static void check_focus(void)
{
    expect(engine_focus_match("Dawnwalker.exe", "Dawnwalker-Win64-Shipping.exe") == 1,
           "ue shipping matches editor exe");
    expect(engine_focus_match("Dawnwalker-Win64-Shipping.exe", "Dawnwalker.exe") == 1,
           "ue editor matches shipping exe");
    expect(engine_focus_match("notepad.exe", "notepad++.exe") == 0,
           "notepad does not match notepad++");
    expect(engine_focus_match("Notepad.exe", "NOTEPAD.EXE") == 1,
           "focus match ignores case");
    expect(engine_focus_match("Notepad.exe", "C:\\Windows\\System32\\notepad.exe") == 1,
           "focus match uses the file name");
    expect(engine_focus_match("C:/Windows/notepad.exe", "notepad.exe") == 1,
           "a path in focusExe still matches the file name");
    expect(engine_focus_match("chrome.exe", "Dawnwalker.exe") == 0,
           "unrelated exe does not match");
    expect(engine_focus_swallow("", "notepad.exe", NULL) == 1,
           "no focus restriction swallows");
    expect(engine_focus_swallow(NULL, "MacroShell.exe", NULL) == 0,
           "the editor window does not swallow keys");
    expect(engine_focus_swallow("", "msedgewebview2.exe", "MacroShell.exe") == 0,
           "webview host does not swallow");
    expect(engine_focus_swallow("", "msedgewebview2.exe", "notepad.exe") == 1,
           "a webview owned by another process swallows");
    expect(engine_focus_swallow("___never___.exe", "notepad.exe", NULL) == 0,
           "unmatched focus does not swallow");
    expect(engine_focus_swallow("Game.exe", "Game-Win64-Shipping.exe", NULL) == 1,
           "shipping exe still matches the stem");
    expect(engine_focus_swallow("Game.exe", "", NULL) == 0,
           "a set focus does not swallow an empty foreground");
}

static void check_watch_plan(void)
{
    struct engine_hook_watch_in in;
    struct engine_hook_watch_out out;

    memset(&in, 0, sizeof in);
    in.silence = 8000;
    in.now = 9000;
    engine_hook_watch(&in, &out);
    expect(out.count == 2
           && out.op[0] == ENGINE_HOOK_RAISE_KEY
           && out.op[1] == ENGINE_HOOK_RAISE_MOUSE,
           "a missing keyboard hook re-raises both");

    in.key_installed = 1;
    in.suspect = 1;
    engine_hook_watch(&in, &out);
    expect(out.clear_suspect == 1 && out.count == 2,
           "a profile parse puts both hooks back");

    in.suspect = 0;
    engine_hook_watch(&in, &out);
    expect(out.count == 0, "idle engine leaves a live hook");

    in.running = 1;
    in.key_stamp = 1;
    in.need_mouse = 0;
    in.silence = 8000;
    in.now = 9000;
    engine_hook_watch(&in, &out);
    expect(out.count == 0, "a quiet keyboard hook stays");

    in.need_mouse = 1;
    in.mouse_installed = 1;
    in.mouse_stamp = 1;
    in.key_stamp = 1;
    engine_hook_watch(&in, &out);
    expect(out.count == 0, "a quiet mouse hook stays");

    in.mouse_installed = 0;
    in.key_stamp = 1;
    in.mouse_stamp = 0;
    in.now = 9000;
    engine_hook_watch(&in, &out);
    expect(out.count == 1 && out.op[0] == ENGINE_HOOK_RAISE_MOUSE,
           "a missing mouse hook is installed");

    in.need_mouse = 1;
    in.mouse_installed = 0;
    in.running = 1;
    in.key_stamp = 1;
    in.mouse_stamp = 1;
    in.now = 20000;
    in.silence = 8000;
    engine_hook_watch(&in, &out);
    expect(out.count == 1 && out.op[0] == ENGINE_HOOK_RAISE_MOUSE,
           "silence does not reinstall a live keyboard hook");

    in.need_mouse = 0;
    in.mouse_installed = 1;
    in.key_stamp = 9000;
    in.mouse_stamp = 9000;
    engine_hook_watch(&in, &out);
    expect(out.count == 1 && out.op[0] == ENGINE_HOOK_RAISE_MOUSE,
           "a keyboard-only profile removes the mouse hook");

    memset(&in, 0, sizeof in);
    in.key_installed = 1;
    in.mouse_installed = 1;
    in.need_mouse = 1;
    in.desktop = 1;
    in.key_stamp = 1;
    in.mouse_stamp = 1;
    in.now = 9000;
    in.silence = 8000;
    engine_hook_watch(&in, &out);
    expect(out.count == 2 && out.op[0] == ENGINE_HOOK_RAISE_KEY &&
               out.op[1] == ENGINE_HOOK_RAISE_MOUSE,
           "returning from the lock screen reinstalls both hooks");
}

static int pop_id(struct engine_finger *finger)
{
    return engine_event_pop(&state, finger);
}

static void check_classify(void)
{
    struct engine_hook_sample sample;
    struct engine_hook_decision decision;
    struct engine_finger finger;
    uint8_t *bits;
    int i;

    reset();
    sample = key_sample(0x42, 0x30, 0, ENGINE_MSG_KEYDOWN, 50);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 1 && decision.swallow == 0 && decision.wake == 0,
           "a physical press is queued without a wake");
    expect(pop_id(&finger) == 1 && finger.id == 0x42 && finger.down == 1 && finger.qpc == 50,
           "physical press tracked");
    sample.msg = ENGINE_MSG_KEYUP;
    sample.now = 60;
    engine_hook_classify(&state, &sample, &decision);
    expect(pop_id(&finger) == 1 && finger.down == 0, "physical release tracked");

    sample = key_sample(0x42, 0x30, ENGINE_KEY_INJECTED, ENGINE_MSG_KEYDOWN, 70);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0, "a sent key cannot press the trigger");
    expect(atomic_load_explicit(&state.key_stamp, memory_order_acquire) == 70,
           "injected input still marks the keyboard hook alive");

    sample = key_sample(0x42, 0x30, 0x02, ENGINE_MSG_KEYDOWN, 71);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0, "lower integrity injected input is not a finger");

    sample = key_sample(0x42, 0x30, 0, ENGINE_MSG_KEYDOWN, 80);
    sample.extra = ENGINE_EXTRA_ECHO;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0 && decision.swallow == 0,
           "echo extra info is not a finger");

    expect(engine_echo_push(&state, 0x42, 1, 150) == 1, "echo token stored");
    sample = key_sample(0x42, 0x30, 0, ENGINE_MSG_KEYDOWN, 150);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0, "hid echo matches a board tap");
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 1, "hid echo is consumed once");
    expect(pop_id(&finger) == 1 && finger.id == 0x42, "a second tap inside the window is a finger");

    reset();
    expect(engine_echo_push(&state, 0x42, 1, 150) == 1, "echo token stored again");
    sample = key_sample(0x42, 0x30, 0, ENGINE_MSG_KEYDOWN, 151);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 1, "a tap after the echo window is a finger");
    pop_id(&finger);

    reset();
    sample = key_sample(0x10, 0x36, 0, ENGINE_MSG_KEYDOWN, 10);
    engine_hook_classify(&state, &sample, &decision);
    expect(pop_id(&finger) == 1 && finger.id == 0xA1, "queued right shift");

    sample = key_sample(0x0D, 0x1C, ENGINE_KEY_EXTENDED, ENGINE_MSG_KEYDOWN, 11);
    engine_hook_classify(&state, &sample, &decision);
    expect(pop_id(&finger) == 1 && finger.id == ENGINE_KEY_NUMPAD_ENTER, "queued numpad enter");

    bits = engine_swallow_begin(&state);
    engine_swallow_set(bits, 0x42, 1, 0);
    engine_swallow_publish(&state);
    sample = key_sample(0x42, 0x30, 0, ENGINE_MSG_KEYDOWN, 12);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.swallow == 1 && decision.enqueued == 1, "keyboard hook swallows from the map");
    pop_id(&finger);

    bits = engine_swallow_begin(&state);
    engine_swallow_set(bits, 0x42, 1, 1);
    engine_swallow_publish(&state);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.swallow == 0 && decision.enqueued == 1, "pass bit wins over swallow");
    pop_id(&finger);

    memset(&sample, 0, sizeof sample);
    sample.kind = ENGINE_HOOK_MOUSE;
    sample.msg = ENGINE_MSG_MOUSEMOVE;
    sample.now = 90;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0, "mouse move is not an event");
    expect(atomic_load_explicit(&state.mouse_stamp, memory_order_acquire) == 0,
           "mouse move does not mark the mouse hook");

    sample.msg = ENGINE_MSG_MOUSEWHEEL;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0, "vertical wheel is not an event");
    sample.msg = ENGINE_MSG_MOUSEHWHEEL;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0, "horizontal wheel is not an event");

    bits = engine_swallow_begin(&state);
    engine_swallow_set(bits, ENGINE_KEY_LBUTTON, 1, 0);
    engine_swallow_set(bits, ENGINE_KEY_RBUTTON, 1, 0);
    engine_swallow_set(bits, ENGINE_KEY_MBUTTON, 1, 0);
    engine_swallow_publish(&state);
    sample.msg = ENGINE_MSG_LBUTTONDOWN;
    sample.now = 100;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.swallow == 0 && decision.enqueued == 1, "left click always passes");
    expect(pop_id(&finger) == 1 && finger.id == ENGINE_KEY_LBUTTON && finger.down == 1,
           "left button queued");
    sample.msg = ENGINE_MSG_RBUTTONDOWN;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.swallow == 0 && decision.enqueued == 1, "right click always passes");
    pop_id(&finger);
    sample.msg = ENGINE_MSG_MBUTTONDOWN;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.swallow == 1 && decision.enqueued == 1, "a mapped middle click is swallowed");
    pop_id(&finger);
    expect(atomic_load_explicit(&state.mouse_stamp, memory_order_acquire) == 100,
           "a button marks the mouse hook");

    sample.msg = ENGINE_MSG_XBUTTONDOWN;
    sample.mouse_data = 2u << 16;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.swallow == 0 && decision.enqueued == 1, "an unmapped side button is passed");
    expect(pop_id(&finger) == 1 && finger.id == ENGINE_KEY_XBUTTON2, "x2 queued");
    sample.mouse_data = 2u;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 1, "low word mouse 5 is queued");
    expect(pop_id(&finger) == 1 && finger.id == ENGINE_KEY_XBUTTON2, "low word mouse 5 is mouse 5");
    sample.mouse_data = 1u;
    engine_hook_classify(&state, &sample, &decision);
    expect(pop_id(&finger) == 1 && finger.id == ENGINE_KEY_XBUTTON1, "low word mouse 4 is mouse 4");

    sample.msg = ENGINE_MSG_LBUTTONDOWN;
    sample.flags = ENGINE_MOUSE_INJECTED;
    sample.mouse_data = 0;
    sample.extra = 0;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0, "an injected click is not a finger");

    sample.flags = 0;
    sample.extra = ENGINE_EXTRA_ECHO;
    sample.msg = ENGINE_MSG_XBUTTONDOWN;
    sample.mouse_data = 1u << 16;
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0 && decision.swallow == 0, "a sent side button is not a finger");

    {
        static const uint32_t msgs[] = {
            ENGINE_MSG_LBUTTONDOWN, ENGINE_MSG_RBUTTONDOWN, ENGINE_MSG_MBUTTONDOWN,
            ENGINE_MSG_XBUTTONDOWN, ENGINE_MSG_XBUTTONDOWN
        };
        static const uint32_t data[] = { 0, 0, 0, 1u << 16, 2u << 16 };
        static const uint16_t ids[] = {
            ENGINE_KEY_LBUTTON, ENGINE_KEY_RBUTTON, ENGINE_KEY_MBUTTON,
            ENGINE_KEY_XBUTTON1, ENGINE_KEY_XBUTTON2
        };
        int b;

        for (b = 0; b < 5; b++) {
            memset(&sample, 0, sizeof sample);
            sample.kind = ENGINE_HOOK_MOUSE;
            sample.msg = msgs[b];
            sample.mouse_data = data[b];
            sample.now = 110;
            engine_hook_classify(&state, &sample, &decision);
            expect(decision.enqueued == 1 && pop_id(&finger) == 1 && finger.id == ids[b] &&
                       finger.down == 1,
                   "every mouse button can be a finger");
        }
    }

    reset();
    engine_hook_set_wake(&state, 1);
    sample = key_sample(0x42, 0x30, 0, ENGINE_MSG_KEYDOWN, 5);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.wake == 1, "playback is woken when it asked");
    pop_id(&finger);
    engine_hook_set_wake(&state, 0);
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.wake == 0, "a disarmed slice does not wake on the key");
    pop_id(&finger);

    reset();
    sample = key_sample(0x42, 0x30, 0, ENGINE_MSG_KEYDOWN, 1);
    for (i = 0; i < (int)ENGINE_EVENT_CAP; i++)
        engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 1 && engine_event_overflow(&state) == 0, "the ring holds 1024");
    engine_hook_classify(&state, &sample, &decision);
    expect(decision.enqueued == 0 && engine_event_overflow(&state) == 1,
           "a full ring latches overflow and returns");
    for (i = 0; i < (int)ENGINE_EVENT_CAP; i++)
        expect(pop_id(&finger) == 1, "queued events stay in order");
    expect(pop_id(&finger) == 0, "the overflowed event was not stored");
    engine_event_clear_overflow(&state);
    expect(engine_event_overflow(&state) == 0, "overflow can be cleared");
}

static void check_ops(void)
{
    struct engine_hook_watch_out plan;
    struct engine_hook_watch_out got;

    reset();
    memset(&plan, 0, sizeof plan);
    plan.count = 2;
    plan.op[0] = ENGINE_HOOK_RAISE_KEY;
    plan.op[1] = ENGINE_HOOK_RAISE_MOUSE;
    engine_hook_post_ops(&state, &plan);
    memset(&plan, 0, sizeof plan);
    plan.count = 1;
    plan.op[0] = ENGINE_HOOK_RAISE_MOUSE;
    engine_hook_post_ops(&state, &plan);
    expect(engine_hook_take_ops(&state, &got) == 1 && got.op[0] == ENGINE_HOOK_RAISE_MOUSE,
           "the latest raise plan wins");
    expect(engine_hook_take_ops(&state, &got) == 0, "the plan is taken once");
}

int main(void)
{
    check_names();
    check_resolve();
    check_trigger_rules();
    check_focus();
    check_watch_plan();
    check_classify();
    check_ops();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
