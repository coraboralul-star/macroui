#include "runner.h"

#include <stdio.h>
#include <string.h>

static int fails;
static struct engine_play play;
static int roll_calls;
static int64_t roll_value;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "fail %s\n", name);
        fails++;
    }
}

static struct engine_play *fresh(void)
{
    engine_play_init(&play);
    return &play;
}

static void set_id(struct engine_macro *macro, const char *id)
{
    int i;

    for (i = 0; i < ENGINE_ID_CAP - 1 && id[i]; i++)
        macro->id[i] = id[i];
    macro->id[i] = '\0';
}

static uint16_t kid(const char *name)
{
    uint16_t id = 0;

    engine_key_id(name, &id);
    return id;
}

static int report_has(const struct engine_play *state, uint16_t id)
{
    int n;
    int i;

    if (!state)
        return 0;
    n = engine_report_down_count(&state->last);
    for (i = 0; i < n; i++) {
        if (engine_report_down(&state->last, i) == id)
            return 1;
    }
    return 0;
}

static int has_id(struct engine_play *state, int64_t now, uint16_t id)
{
    uint16_t ids[ENGINE_HOLD_CAP];
    int n = engine_play_desired(state, now, ids, ENGINE_HOLD_CAP);
    int i;

    for (i = 0; i < n; i++) {
        if (ids[i] == id)
            return 1;
    }
    return 0;
}

static int64_t scripted_roll(int64_t lo, int64_t hi, void *ctx)
{
    (void)lo;
    (void)hi;
    (void)ctx;
    roll_calls++;
    return roll_value;
}

static void check_names(void)
{
    uint16_t id = 99;

    expect(engine_key_id("", &id) == 0 && id == 0, "empty name");
    expect(engine_key_id(NULL, &id) == 0, "null name");
    expect(kid("w") == 0x57 && kid("W") == 0x57, "letter w");
    expect(kid("space") == 0x20 && kid("Space") == 0x20, "space");
    expect(kid("1") == 0x31, "digit");
    expect(kid("shift") == 0xA0 && kid("LShift") == 0xA0, "shift means left shift");
    expect(kid("RShift") == 0xA1, "right shift stays right");
    expect(kid("ctrl") == 0xA2 && kid("LControl") == 0xA2, "ctrl");
    expect(kid("rcontrol") == 0xA3, "right ctrl");
    expect(kid("f1") == 0x70 && kid("F12") == 0x7B && kid("f24") == 0x87, "function keys");
    expect(kid("enter") == 0x0D, "enter");
    expect(kid("numpadenter") == ENGINE_KEY_NUMPAD_ENTER, "numpad enter");
    expect(kid("tab") == 0x09 && kid("escape") == 0x1B, "tab and escape");
    expect(kid("up") == 0x26 && kid("ins") == 0x2D && kid("del") == 0x2E, "arrows and edit");
    expect(kid("LButton") == ENGINE_KEY_LBUTTON, "left button");
    expect(kid("lbutton") == 0, "lower left button is not the button");
    expect(kid("RButton") == ENGINE_KEY_RBUTTON, "right button");
    expect(kid("MButton") == ENGINE_KEY_MBUTTON, "middle button");
    expect(kid("XButton1") == ENGINE_KEY_XBUTTON1, "side button");
    expect(kid("XButton2") == ENGINE_KEY_XBUTTON2, "second side button");
    expect(kid("nope") == 0, "unknown name");
    expect(kid("|") == 0xDC && kid("\\") == 0xDC, "pipe and backslash are VK_OEM_5");
    expect(kid("CapsLock") == 0x14, "CapsLock is VK_CAPITAL");
    expect(engine_step_kind("key") == ENGINE_STEP_KEY, "kind key");
    expect(engine_step_kind("randomWait") == ENGINE_STEP_RANDOM_WAIT, "kind random");
    expect(engine_step_kind("steps") == ENGINE_STEP_REPEAT, "steps is a repeat");
    expect(engine_step_kind("whileHeld") == ENGINE_STEP_HOLD_LOOP, "while held kind");
    expect(engine_step_kind("tapSpam") == ENGINE_STEP_EDGE_HOLD, "spam kind");
    expect(engine_step_kind("pressHold") == 0, "pressHold expands and is not a step");
    expect(engine_step_kind("nope") == 0, "unknown kind");
    expect(engine_goto_where("Client") == ENGINE_WHERE_CLIENT, "goto client");
    expect(engine_goto_where("") == ENGINE_WHERE_SCREEN, "goto screen default");
    expect(engine_goto_where(" window ") == ENGINE_WHERE_WINDOW, "goto window trims");
    expect(engine_goto_where("nope") == ENGINE_WHERE_SCREEN && engine_goto_where(NULL) == ENGINE_WHERE_SCREEN,
           "goto other name is screen");
    expect(engine_top_repeats(ENGINE_MODE_REPEAT, 0, 0) == 1, "play repeat floors at 1");
    expect(engine_top_repeats(ENGINE_MODE_REPEAT, 3, 0) == 3, "play repeat keeps 3");
    expect(engine_top_repeats(ENGINE_MODE_WHILE_HELD, 4, 0) == 0, "while held loops");
    expect(engine_top_repeats(ENGINE_MODE_TOGGLE, 4, 0) == 0, "toggle loops");
    expect(engine_top_repeats(ENGINE_MODE_WHILE_HELD, 4, 1) == 1, "hold loop plays once");
    expect(engine_top_repeats(ENGINE_MODE_ONCE, 9, 0) == 1, "play once is one pass");
    expect(engine_top_repeats(ENGINE_MODE_ON_RELEASE, 9, 0) == 1, "on release is one pass");
}

static void check_basic(void)
{
    struct engine_step built[4];
    struct engine_play *state = fresh();
    struct engine_macro macro;
    struct engine_runner *runner;
    uint16_t side = kid("XButton1");
    uint16_t q = kid("q");

    expect(engine_basic_rewrite(ENGINE_KEY_LBUTTON, 0, 5, built, 4) == 0,
           "left click is not a macro trigger");
    expect(engine_basic_rewrite(ENGINE_KEY_RBUTTON, 1, 5, built, 4) == 0,
           "right click is not a macro trigger");
    expect(engine_basic_rewrite(q, 0, 5, built, 4) == 0, "a key needs kind key");
    expect(engine_basic_rewrite(side, 0, 5, built, 4) == 4, "side button basic clicks that button");
    expect(built[0].type == ENGINE_STEP_MOUSE && built[0].action == ENGINE_ACT_DOWN &&
               built[0].key == side,
           "side button step");
    expect(built[1].a == 18 && built[3].a == 5, "side gap");

    engine_macro_init(&macro);
    set_id(&macro, "once-key");
    macro.basic = 1;
    macro.trigger_is_key = 1;
    macro.gap_ms = 5;
    macro.trigger = q;
    expect(engine_play_start(state, &macro, -1, 1) == 1, "basic start");
    engine_play_flush(state);
    runner = &state->runners[0];
    expect(runner->step_count == 4 && runner->steps[0].action == ENGINE_ACT_DOWN &&
               runner->steps[1].a == 18 && runner->steps[2].action == ENGINE_ACT_UP &&
               runner->steps[3].a == 5,
           "repeat ms is the gap after an 18 ms hold");
    engine_runner_advance(runner, 0, NULL, NULL);
    expect(has_id(state, 0, q) == 1, "basic key is down");
    engine_runner_advance(runner, 17, NULL, NULL);
    expect(has_id(state, 17, q) == 1 && runner->done == 0, "basic key stays for the 18 ms hold");
    engine_runner_advance(runner, 18, NULL, NULL);
    expect(has_id(state, 18, q) == 0 && runner->done == 0,
           "basic key releases before the next repeat");
    engine_runner_advance(runner, 23, NULL, NULL);
    expect(runner->done == 1 && has_id(state, 23, q) == 0, "play once finishes after one press");

    fresh();
    engine_macro_init(&macro);
    set_id(&macro, "slow-key");
    macro.basic = 1;
    macro.trigger_is_key = 1;
    macro.gap_ms = 40;
    macro.trigger = kid("b");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    expect(play.runners[0].steps[1].a == 18 && play.runners[0].steps[3].a == 40,
           "repeat ms stays the gap after the hold");

    expect(engine_basic_rewrite(side, 0, -3, built, 4) == 4 && built[3].a == 0,
           "negative gap clamps");
}

static void check_sprint(void)
{
    struct engine_play *state = fresh();
    struct engine_step sprint_steps[4];
    struct engine_step click_steps[5];
    struct engine_macro sprint;
    struct engine_macro click;
    uint16_t w = kid("w");
    uint16_t space = kid("space");
    uint16_t button = kid("LButton");

    memset(sprint_steps, 0, sizeof sprint_steps);
    sprint_steps[0].type = ENGINE_STEP_KEY;
    sprint_steps[0].action = ENGINE_ACT_DOWN;
    sprint_steps[0].key = w;
    sprint_steps[1].type = ENGINE_STEP_REPEAT;
    sprint_steps[1].a = 0;
    sprint_steps[1].first = 2;
    sprint_steps[1].count = 2;
    sprint_steps[2].type = ENGINE_STEP_KEY;
    sprint_steps[2].action = ENGINE_ACT_TAP;
    sprint_steps[2].key = space;
    sprint_steps[2].a = 20;
    sprint_steps[3].type = ENGINE_STEP_WAIT;
    sprint_steps[3].a = 50;

    engine_macro_init(&sprint);
    set_id(&sprint, "sprint-jump");
    sprint.steps = sprint_steps;
    sprint.step_count = 4;
    sprint.top_count = 2;
    sprint.mode = ENGINE_MODE_WHILE_HELD;
    engine_play_start(state, &sprint, -1, 1);
    engine_play_flush(state);
    engine_runner_advance(&state->runners[0], 0, NULL, NULL);
    expect(has_id(state, 0, w) && has_id(state, 0, space), "t0 holds w and space");
    engine_runner_advance(&state->runners[0], 20, NULL, NULL);
    expect(has_id(state, 20, w) && !has_id(state, 20, space), "t20 space released w stays");
    engine_runner_advance(&state->runners[0], 50, NULL, NULL);
    expect(has_id(state, 50, w) && has_id(state, 50, space), "t50 space taps again");

    memset(click_steps, 0, sizeof click_steps);
    click_steps[0].type = ENGINE_STEP_MOUSE;
    click_steps[0].action = ENGINE_ACT_TAP;
    click_steps[0].key = button;
    click_steps[0].a = 15;
    click_steps[1].type = ENGINE_STEP_WAIT;
    click_steps[1].a = 40;
    click_steps[2] = click_steps[0];
    click_steps[3] = click_steps[1];
    click_steps[4] = click_steps[0];
    engine_macro_init(&click);
    set_id(&click, "click-burst");
    click.steps = click_steps;
    click.step_count = 5;
    engine_play_start(state, &click, -1, 1);
    engine_play_flush(state);
    expect(state->runner_count == 2, "both macros running");
    engine_runner_advance(&state->runners[1], 50, NULL, NULL);
    expect(has_id(state, 50, w) && has_id(state, 50, button), "click does not drop w");
    engine_play_stop(state, "click-burst");
    engine_play_remove_done(state);
    expect(has_id(state, 50, w) && !has_id(state, 50, button) && engine_play_alive(state) == 1,
           "stopping click leaves sprint");
    engine_play_publish(state, 50);
    expect(state->output.applied_count == 2 && state->output.applied[0] == w &&
               state->output.applied[1] == space,
           "merger pressed w and space");
    engine_play_stop(state, "sprint-jump");
    engine_play_remove_done(state);
    engine_play_publish(state, 51);
    expect(state->output.applied_count == 0 && engine_play_alive(state) == 0,
           "stopping sprint releases w");
}

static void check_repeat_and_hold(void)
{
    struct engine_play *state = fresh();
    struct engine_step wait_step;
    struct engine_step down_step;
    struct engine_macro macro;
    struct engine_runner *runner;

    memset(&wait_step, 0, sizeof wait_step);
    wait_step.type = ENGINE_STEP_WAIT;
    wait_step.a = 10;
    engine_macro_init(&macro);
    set_id(&macro, "repeat-key");
    macro.steps = &wait_step;
    macro.step_count = 1;
    macro.mode = ENGINE_MODE_REPEAT;
    macro.repeat_count = 3;
    engine_play_start(state, &macro, -1, 1);
    engine_play_flush(state);
    runner = &state->runners[0];
    engine_runner_advance(runner, 0, NULL, NULL);
    expect(runner->done == 0, "repeat still going after 1");
    engine_runner_advance(runner, 10, NULL, NULL);
    expect(runner->done == 0, "repeat still going after 2");
    engine_runner_advance(runner, 20, NULL, NULL);
    expect(runner->done == 0, "repeat still going after 3");
    engine_runner_advance(runner, 30, NULL, NULL);
    expect(runner->done == 1, "repeat stops after the count");

    fresh();
    memset(&down_step, 0, sizeof down_step);
    down_step.type = ENGINE_STEP_KEY;
    down_step.action = ENGINE_ACT_DOWN;
    down_step.key = kid("w");
    engine_macro_init(&macro);
    set_id(&macro, "held-key");
    macro.steps = &down_step;
    macro.step_count = 1;
    macro.mode = ENGINE_MODE_WHILE_HELD;
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    runner = &play.runners[0];
    engine_runner_advance(runner, 0, NULL, NULL);
    engine_runner_advance(runner, 50, NULL, NULL);
    expect(runner->done == 0 && has_id(&play, 50, kid("w")), "while pressed keeps holding");
    engine_play_stop(&play, "held-key");
    engine_play_remove_done(&play);
    expect(has_id(&play, 50, kid("w")) == 0, "while pressed releases on stop");
}

static void check_zero_delay(void)
{
    struct engine_play *state = fresh();
    struct engine_step steps[2];
    struct engine_macro macro;
    struct engine_runner *runner;
    uint16_t a = kid("a");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_KEY;
    steps[0].action = ENGINE_ACT_DOWN;
    steps[0].key = a;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_UP;
    steps[1].key = a;
    engine_macro_init(&macro);
    set_id(&macro, "flash-key");
    macro.steps = steps;
    macro.step_count = 2;
    engine_play_start(state, &macro, -1, 1);
    engine_play_flush(state);
    runner = &state->runners[0];
    engine_runner_advance(runner, 0, NULL, NULL);
    expect(has_id(state, 0, a) && runner->done == 0, "zero delay press is sent");
    engine_runner_advance(runner, 1, NULL, NULL);
    expect(!has_id(state, 1, a) && runner->done == 1, "zero delay press releases");
}

static void check_waits(void)
{
    struct engine_play *state = fresh();
    struct engine_step step;
    struct engine_macro macro;
    struct engine_runner *runner;

    memset(&step, 0, sizeof step);
    step.type = ENGINE_STEP_WAIT;
    step.a = 5;
    engine_macro_init(&macro);
    set_id(&macro, "scaled");
    macro.steps = &step;
    macro.step_count = 1;
    macro.speed = 2.0;
    engine_play_start(state, &macro, -1, 1);
    engine_play_flush(state);
    runner = &state->runners[0];
    engine_runner_advance(runner, 0, NULL, NULL);
    expect(runner->wait_until == 3 && runner->done == 0, "speed scales the wait");
    engine_runner_advance(runner, 2, NULL, NULL);
    expect(runner->done == 0, "scaled wait still held");
    engine_runner_advance(runner, 3, NULL, NULL);
    expect(runner->done == 1, "scaled wait ends on the rounded tick");

    fresh();
    macro.speed = 0.01;
    set_id(&macro, "floor");
    step.a = 1;
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    runner = &play.runners[0];
    expect(runner->speed == 0.05, "speed floors at 0.05");
    engine_runner_advance(runner, 0, NULL, NULL);
    expect(runner->wait_until == 20, "floored speed stretches the wait");

    fresh();
    play.freq = 10000000;
    macro.speed = 1.0;
    step.a = 5;
    set_id(&macro, "ticks");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    runner = &play.runners[0];
    engine_runner_advance(runner, 0, NULL, NULL);
    expect(runner->wait_until == 50000, "frequency converts the wait");
    engine_runner_advance(runner, 49999, NULL, NULL);
    expect(runner->done == 0, "one tick early still waits");
    engine_runner_advance(runner, 50000, NULL, NULL);
    expect(runner->done == 1, "the wait ends on the tick");

    fresh();
    step.a = 0;
    set_id(&macro, "zero");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(play.runners[0].wait_until == 1, "a zero wait still waits 1 ms");

    fresh();
    step.a = -4;
    set_id(&macro, "neg");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(play.runners[0].wait_until == 1, "a negative wait still waits 1 ms");
}

static void check_random(void)
{
    struct engine_play *state = fresh();
    struct engine_step step;
    struct engine_macro macro;

    memset(&step, 0, sizeof step);
    step.type = ENGINE_STEP_RANDOM_WAIT;
    step.a = 70;
    step.b = 40;
    engine_macro_init(&macro);
    set_id(&macro, "roll");
    macro.steps = &step;
    macro.step_count = 1;
    roll_calls = 0;
    roll_value = 55;
    engine_play_set_roll(state, scripted_roll, NULL);
    engine_play_start(state, &macro, -1, 1);
    engine_play_flush(state);
    engine_runner_advance(&state->runners[0], 0, state->roll, state->roll_ctx);
    expect(roll_calls == 1 && state->runners[0].wait_until == 55, "random wait uses the injected roll");

    fresh();
    roll_calls = 0;
    roll_value = 5;
    step.a = 70;
    step.b = 40;
    engine_play_set_roll(&play, scripted_roll, NULL);
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, play.roll, play.roll_ctx);
    expect(play.runners[0].wait_until == 40, "random wait clamps a low roll");

    fresh();
    roll_value = 90;
    engine_play_set_roll(&play, scripted_roll, NULL);
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, play.roll, play.roll_ctx);
    expect(play.runners[0].wait_until == 70, "random wait clamps a high roll");

    fresh();
    roll_calls = 0;
    roll_value = 99;
    step.a = 15;
    step.b = 15;
    engine_play_set_roll(&play, scripted_roll, NULL);
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, play.roll, play.roll_ctx);
    expect(roll_calls == 0 && play.runners[0].wait_until == 15, "random wait uses a fixed range");
}

static void check_move_and_scroll(void)
{
    struct engine_play *state = fresh();
    struct engine_step steps[2];
    struct engine_step big[2];
    struct engine_macro macro;

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_MOVE;
    steps[0].a = 4;
    steps[0].b = -3;
    steps[1].type = ENGINE_STEP_SCROLL;
    steps[1].a = 1;
    engine_macro_init(&macro);
    set_id(&macro, "nudge");
    macro.steps = steps;
    macro.step_count = 2;
    engine_play_start(state, &macro, -1, 1);
    engine_play_tick(state, 0);
    expect(engine_report_dx(&state->last) == 4 && engine_report_dy(&state->last) == -3 &&
               engine_report_wheel(&state->last) == 1 && engine_report_dpi(&state->last) == 96,
           "one shot move and scroll are published");
    expect(engine_play_alive(state) == 0, "finished runner is removed after publish");
    engine_play_tick(state, 1);
    expect(engine_report_active(&state->last) == 0, "the next tick does not repeat the move");

    fresh();
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    engine_play_publish(&play, 0);
    expect(engine_report_active(&play.last) == 0, "a second advance drops an unpublished move");

    fresh();
    memset(big, 0, sizeof big);
    big[0].type = ENGINE_STEP_MOVE;
    big[0].a = 10000;
    big[0].b = 10000;
    big[1].type = ENGINE_STEP_MOVE;
    big[1].a = 10000;
    big[1].b = 10000;
    macro.steps = big;
    macro.step_count = 2;
    set_id(&macro, "clamp");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    engine_play_publish(&play, 0);
    expect(engine_report_dx(&play.last) == 16000 && engine_report_dy(&play.last) == 16000,
           "publish clamps the summed move");
}

static void check_scroll_sum(void)
{
    struct engine_play *state = fresh();
    struct engine_step steps[2];
    struct engine_macro macro;

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_SCROLL;
    steps[0].a = 3;
    steps[1].type = ENGINE_STEP_SCROLL;
    steps[1].a = -1;
    engine_macro_init(&macro);
    set_id(&macro, "wheel");
    macro.steps = steps;
    macro.step_count = 2;
    engine_play_start(state, &macro, -1, 1);
    engine_play_flush(state);
    engine_runner_advance(&state->runners[0], 0, NULL, NULL);
    engine_play_publish(state, 0);
    expect(engine_report_wheel(&state->last) == 2, "scrolls add and stay positive down");
}

static void check_yield_and_skip(void)
{
    struct engine_play *state = fresh();
    struct engine_step steps[70];
    struct engine_step follow[3];
    struct engine_macro macro;
    int i;

    memset(steps, 0, sizeof steps);
    for (i = 0; i < 70; i++) {
        steps[i].type = ENGINE_STEP_MOVE;
        steps[i].a = 1;
    }
    engine_macro_init(&macro);
    set_id(&macro, "yield");
    macro.steps = steps;
    macro.step_count = 70;
    engine_play_start(state, &macro, -1, 1);
    engine_play_flush(state);
    engine_runner_advance(&state->runners[0], 0, NULL, NULL);
    expect(state->runners[0].move_x == 64 && state->runners[0].wait_until == 1 &&
               state->runners[0].done == 0,
           "64 steps yield 1 ms");

    fresh();
    memset(follow, 0, sizeof follow);
    follow[0].type = 99;
    follow[1].type = ENGINE_STEP_KEY;
    follow[1].action = ENGINE_ACT_DOWN;
    follow[1].key = kid("w");
    follow[2].type = ENGINE_STEP_WAIT;
    follow[2].a = 10;
    macro.steps = follow;
    macro.step_count = 3;
    set_id(&macro, "skip");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(has_id(&play, 0, kid("w")) && play.runners[0].done == 0, "an unknown step is skipped");

    fresh();
    follow[0].type = ENGINE_STEP_KEY;
    follow[0].action = ENGINE_ACT_DOWN;
    follow[0].key = 0;
    set_id(&macro, "blank");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(has_id(&play, 0, kid("w")) && !has_id(&play, 0, 0), "an empty key is a no-op");

    fresh();
    {
        struct engine_step later[2];

        memset(later, 0, sizeof later);
        later[0].type = ENGINE_STEP_HOLD_LOOP;
        later[0].first = 1;
        later[0].count = 1;
        later[1].type = ENGINE_STEP_WAIT;
        later[1].a = 10;
        macro.steps = later;
        macro.step_count = 2;
        macro.top_count = 1;
        set_id(&macro, "later");
        engine_play_start(&play, &macro, -1, 1);
        engine_play_flush(&play);
        engine_runner_advance(&play.runners[0], 0, NULL, NULL);
        expect(play.runners[0].done == 1, "a hold loop on the bare runner does nothing");
    }

    fresh();
    {
        struct engine_step later[2];

        memset(later, 0, sizeof later);
        later[0].type = ENGINE_STEP_SWAP_LOOP;
        later[0].first = 1;
        later[0].count = 1;
        later[0].c = 40;
        later[1].type = ENGINE_STEP_WAIT;
        later[1].a = 10;
        macro.steps = later;
        macro.step_count = 2;
        macro.top_count = 1;
        macro.trigger = kid("b");
        set_id(&macro, "later-swap");
        engine_play_start(&play, &macro, -1, 1);
        engine_play_flush(&play);
        engine_runner_advance(&play.runners[0], 0, NULL, NULL);
        expect(play.runners[0].done == 1, "a swap loop on the bare runner does nothing");
    }
    macro.top_count = 0;
    macro.trigger = 0;

    fresh();
    follow[0].type = ENGINE_STEP_KEY;
    follow[0].action = ENGINE_ACT_DOWN;
    follow[0].key = kid("w");
    macro.steps = follow;
    macro.step_count = 1;
    set_id(&macro, "lone");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(play.runners[0].done == 1 && has_id(&play, 0, kid("w")) == 0,
           "a lone down in play once is cleared when the frame ends");
}

static void check_repeat_step(void)
{
    struct engine_play *state = fresh();
    struct engine_step steps[2];
    struct engine_macro macro;
    struct engine_runner *runner;

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_REPEAT;
    steps[0].a = 2;
    steps[0].first = 1;
    steps[0].count = 1;
    steps[1].type = ENGINE_STEP_WAIT;
    steps[1].a = 10;
    engine_macro_init(&macro);
    set_id(&macro, "inner");
    macro.steps = steps;
    macro.step_count = 2;
    macro.top_count = 1;
    engine_play_start(state, &macro, -1, 1);
    engine_play_flush(state);
    runner = &state->runners[0];
    engine_runner_advance(runner, 0, NULL, NULL);
    expect(runner->done == 0, "repeat step started");
    engine_runner_advance(runner, 10, NULL, NULL);
    expect(runner->done == 0, "repeat step has one pass left");
    engine_runner_advance(runner, 20, NULL, NULL);
    expect(runner->done == 1, "repeat step stops after the count");

    fresh();
    steps[0].a = -1;
    set_id(&macro, "forever");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    runner = &play.runners[0];
    engine_runner_advance(runner, 0, NULL, NULL);
    engine_runner_advance(runner, 10, NULL, NULL);
    engine_runner_advance(runner, 20, NULL, NULL);
    expect(runner->done == 0, "a negative repeat count loops");
}

static void check_stop_mode(void)
{
    struct engine_play *state = fresh();
    struct engine_step steps[5];
    struct engine_macro macro;
    uint16_t w = kid("w");
    uint16_t a = kid("a");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_KEY;
    steps[0].action = ENGINE_ACT_DOWN;
    steps[0].key = w;
    steps[1].type = ENGINE_STEP_WAIT;
    steps[1].a = 10;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_UP;
    steps[2].key = w;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = a;
    steps[4].type = ENGINE_STEP_WAIT;
    steps[4].a = 10;
    engine_macro_init(&macro);
    set_id(&macro, "cut");
    macro.steps = steps;
    macro.step_count = 5;
    macro.release_stop = ENGINE_STOP_NEXT_UP;
    engine_play_start(state, &macro, -1, 1);
    engine_play_flush(state);
    engine_play_ask_stop(state, "cut");
    engine_runner_advance(&state->runners[0], 0, NULL, NULL);
    engine_runner_advance(&state->runners[0], 10, NULL, NULL);
    expect(state->runners[0].done == 1 && has_id(state, 10, a) == 0 && has_id(state, 10, w) == 0,
           "nextUp skips the rest");

    fresh();
    macro.release_stop = ENGINE_STOP_FINISH;
    set_id(&macro, "thru");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_play_ask_stop(&play, "thru");
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    engine_runner_advance(&play.runners[0], 10, NULL, NULL);
    expect(play.runners[0].done == 0 && has_id(&play, 10, a) && !has_id(&play, 10, w),
           "finish plays the later down");
}

static void check_start_rules(void)
{
    struct engine_play *state = fresh();
    struct engine_step step;
    struct engine_macro macro;
    struct engine_macro other;

    memset(&step, 0, sizeof step);
    step.type = ENGINE_STEP_WAIT;
    step.a = 10;
    engine_macro_init(&macro);
    macro.steps = &step;
    macro.step_count = 1;
    macro.busy = 1;
    set_id(&macro, "busy-id");
    expect(engine_play_start(state, &macro, -1, 1) == 1, "busy first start");
    expect(engine_play_start(state, &macro, -1, 1) == 0 && state->pending_count == 1,
           "busy ignores a second press");

    fresh();
    macro.busy = 0;
    macro.mode = ENGINE_MODE_WHILE_HELD;
    set_id(&macro, "held");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    expect(engine_play_start(&play, &macro, -1, 1) == 0 && play.pending_count == 0,
           "while held does not restart");

    fresh();
    macro.mode = ENGINE_MODE_ONCE;
    set_id(&macro, "");
    expect(engine_play_start(&play, &macro, -1, 1) == 0, "an empty id does not start");

    set_id(&macro, "once-id");
    engine_play_start(&play, &macro, -1, 1);
    expect(engine_play_start(&play, &macro, -1, 0) == 0 && play.pending_count == 1,
           "restart false leaves the pending runner");

    fresh();
    step.type = ENGINE_STEP_KEY;
    step.action = ENGINE_ACT_DOWN;
    step.key = kid("w");
    macro.steps = &step;
    macro.mode = ENGINE_MODE_WHILE_HELD;
    set_id(&macro, "old");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    engine_macro_init(&other);
    other.steps = &step;
    other.step_count = 1;
    other.exclusive = 1;
    set_id(&other, "new");
    expect(engine_play_start(&play, &other, -1, 1) == 1, "exclusive queues the new runner");
    engine_play_flush(&play);
    engine_play_remove_done(&play);
    expect(engine_play_alive(&play) == 1 && has_id(&play, 0, kid("w")) == 0,
           "exclusive calls StopAll");
}

static void check_overflow(void)
{
    struct engine_play *state = fresh();
    struct engine_step steps[34];
    struct engine_macro macro;
    int i;

    memset(steps, 0, sizeof steps);
    for (i = 0; i < 33; i++) {
        steps[i].type = ENGINE_STEP_KEY;
        steps[i].action = ENGINE_ACT_DOWN;
        steps[i].key = (uint16_t)(i + 1);
    }
    steps[33].type = ENGINE_STEP_WAIT;
    steps[33].a = 10;
    engine_macro_init(&macro);
    set_id(&macro, "wide");
    macro.steps = steps;
    macro.step_count = 34;
    macro.mode = ENGINE_MODE_WHILE_HELD;
    engine_play_start(state, &macro, -1, 1);
    engine_play_tick(state, 0);
    expect((engine_report_flags(&state->last) & ENGINE_REPORT_OVERFLOW) != 0, "report overflow");
    expect(engine_report_down_count(&state->last) == 32, "overflow keeps 32 downs");
    expect(engine_report_down(&state->last, 0) == 1 && engine_report_down(&state->last, 31) == 32,
           "overflow keeps the first downs");
    expect(state->output.applied_count == 33, "the down that did not fit stays applied");
    expect(engine_play_alive(state) == 0, "overflow stops the runner");
}

static int count_id(const struct engine_play *state, const char *id)
{
    int n = 0;
    int i;

    for (i = 0; i < state->runner_count; i++) {
        if (!state->runners[i].done && strcmp(state->runners[i].id, id) == 0)
            n++;
    }
    for (i = 0; i < state->pending_count; i++) {
        if (strcmp(state->pending[i].id, id) == 0)
            n++;
    }
    return n;
}

static void check_goto(void)
{
    struct engine_play *state = fresh();
    struct engine_step steps[3];
    struct engine_macro macro;
    struct engine_pointer pointer;
    struct engine_runner *runner;

    memset(&pointer, 0, sizeof pointer);
    pointer.have_mouse = 1;
    pointer.mouse_x = 10;
    pointer.mouse_y = 20;
    engine_play_set_pointer(state, &pointer);

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_MOVE;
    steps[0].a = 4;
    steps[0].b = -3;
    steps[1].type = ENGINE_STEP_SCROLL;
    steps[1].a = 2;
    steps[2].type = ENGINE_STEP_GOTO;
    steps[2].action = ENGINE_WHERE_SCREEN;
    steps[2].a = 15;
    steps[2].b = 18;
    steps[2].count = 15;
    engine_macro_init(&macro);
    set_id(&macro, "aim");
    macro.steps = steps;
    macro.step_count = 3;
    engine_play_start(state, &macro, -1, 1);
    engine_play_tick(state, 0);
    expect(engine_play_alive(state) == 1, "goto duration keeps the runner");
    expect((engine_report_flags(&state->last) & ENGINE_REPORT_ABSOLUTE) != 0, "goto publishes the point");
    expect(engine_report_ax(&state->last) == 15 && engine_report_ay(&state->last) == 18, "screen goto");
    expect(engine_report_dx(&state->last) == 0 && engine_report_dy(&state->last) == 0,
           "goto drops the relative move");
    expect(engine_report_wheel(&state->last) == 2, "goto keeps the wheel");
    expect(state->output.mouse_ok == 1 && state->output.mouse_x == 15 && state->output.mouse_y == 18,
           "goto remembers the point");
    runner = engine_play_runner(state, 0);
    expect(runner && runner->wait_until == 15, "goto duration is 15 ms");

    engine_play_tick(state, 5);
    expect((engine_report_flags(&state->last) & ENGINE_REPORT_ABSOLUTE) == 0,
           "a waiting goto does not publish again");
    engine_play_tick(state, 15);
    expect(engine_play_alive(state) == 0, "goto finishes after the duration");

    fresh();
    memset(&pointer, 0, sizeof pointer);
    pointer.have_mouse = 1;
    pointer.have_window = 1;
    pointer.window_x = 100;
    pointer.window_y = 200;
    pointer.have_client = 1;
    pointer.client_x = -5;
    pointer.client_y = 8;
    engine_play_set_pointer(&play, &pointer);
    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_GOTO;
    steps[0].action = ENGINE_WHERE_WINDOW;
    steps[0].a = 5;
    steps[0].b = 7;
    engine_macro_init(&macro);
    set_id(&macro, "win");
    macro.steps = steps;
    macro.step_count = 1;
    engine_play_start(&play, &macro, -1, 1);
    engine_play_tick(&play, 0);
    expect(engine_report_ax(&play.last) == 105 && engine_report_ay(&play.last) == 207, "window goto");

    fresh();
    engine_play_set_pointer(&play, &pointer);
    steps[0].action = ENGINE_WHERE_CLIENT;
    steps[0].a = 3;
    steps[0].b = 4;
    set_id(&macro, "client");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_tick(&play, 0);
    expect(engine_report_ax(&play.last) == -2 && engine_report_ay(&play.last) == 12, "client goto");

    fresh();
    pointer.have_window = 0;
    engine_play_set_pointer(&play, &pointer);
    engine_output_set_mode(&play.output, ENGINE_OUTPUT_RP2040, NULL, NULL, NULL);
    engine_output_remember_mouse(&play.output, 9, 9);
    steps[0].action = ENGINE_WHERE_WINDOW;
    steps[0].a = 1;
    steps[0].b = 1;
    set_id(&macro, "miss");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_play_advance(&play, 0);
    runner = &play.runners[0];
    expect(runner->did_goto == 1 && runner->has_goto == 0, "a failed goto still counts as the first");
    expect(play.output.mouse_ok == 0, "the first goto clears the remembered point");
    engine_play_publish(&play, 0);
    expect((engine_report_flags(&play.last) & ENGINE_REPORT_ABSOLUTE) == 0, "a failed goto sends no point");

    fresh();
    memset(&pointer, 0, sizeof pointer);
    pointer.have_mouse = 1;
    pointer.mouse_x = 10;
    pointer.mouse_y = 20;
    engine_play_set_pointer(&play, &pointer);
    engine_output_set_mode(&play.output, ENGINE_OUTPUT_RP2040, NULL, NULL, NULL);
    engine_output_remember_mouse(&play.output, 1, 1);
    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_GOTO;
    steps[0].a = 10;
    steps[0].b = 20;
    steps[1].type = ENGINE_STEP_GOTO;
    steps[1].a = 30;
    steps[1].b = 40;
    set_id(&macro, "board");
    macro.steps = steps;
    macro.step_count = 2;
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_play_advance(&play, 0);
    runner = &play.runners[0];
    expect(runner->move_x == 20 && runner->move_y == 20, "the second board goto uses the remembered point");
    expect(runner->goto_x == 30 && runner->goto_y == 40, "the last goto wins");
    engine_play_publish(&play, 0);
    expect(engine_report_ax(&play.last) == 30 && engine_report_ay(&play.last) == 40 &&
               engine_report_dx(&play.last) == 0,
           "publish sends the absolute point");

    fresh();
    pointer.mouse_x = 0;
    pointer.mouse_y = 0;
    engine_play_set_pointer(&play, &pointer);
    engine_output_remember_mouse(&play.output, 100, 100);
    steps[0].a = 5;
    steps[0].b = 5;
    steps[0].count = 0;
    steps[1].a = 5;
    steps[1].b = 5;
    steps[1].count = 0;
    set_id(&macro, "soft");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_play_advance(&play, 0);
    expect(play.runners[0].move_x == 10 && play.runners[0].move_y == 10,
           "software goto uses the snapshot both times");

    fresh();
    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_GOTO;
    steps[0].a = 8;
    steps[0].b = 9;
    steps[0].count = 15;
    engine_macro_init(&macro);
    set_id(&macro, "slow");
    macro.steps = steps;
    macro.step_count = 1;
    macro.speed = 2.0;
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_play_advance(&play, 0);
    expect(play.runners[0].wait_until == 8, "goto duration divides by speed");
}

static void check_run(void)
{
    struct engine_play *state = fresh();
    struct engine_step caller_steps[3];
    struct engine_step target_steps[2];
    struct engine_step filler_step;
    struct engine_macro catalog[2];
    struct engine_macro fillers[31];
    struct engine_macro caller;
    char name[8];
    int i;
    uint16_t w = kid("w");
    uint16_t a = kid("a");

    memset(caller_steps, 0, sizeof caller_steps);
    memset(target_steps, 0, sizeof target_steps);
    caller_steps[0].type = ENGINE_STEP_RUN;
    caller_steps[0].a = 1;
    caller_steps[1].type = ENGINE_STEP_KEY;
    caller_steps[1].action = ENGINE_ACT_DOWN;
    caller_steps[1].key = a;
    caller_steps[2].type = ENGINE_STEP_WAIT;
    caller_steps[2].a = 50;
    target_steps[0].type = ENGINE_STEP_KEY;
    target_steps[0].action = ENGINE_ACT_DOWN;
    target_steps[0].key = w;
    target_steps[1].type = ENGINE_STEP_WAIT;
    target_steps[1].a = 50;

    engine_macro_init(&catalog[0]);
    engine_macro_init(&catalog[1]);
    set_id(&catalog[0], "a");
    catalog[0].steps = caller_steps;
    catalog[0].step_count = 3;
    set_id(&catalog[1], "b");
    catalog[1].steps = target_steps;
    catalog[1].step_count = 2;
    catalog[1].mode = ENGINE_MODE_WHILE_HELD;
    catalog[1].repeat_count = 5;
    engine_play_set_macros(state, catalog, 2);
    engine_play_start(state, &catalog[0], -1, 1);
    engine_play_tick(state, 0);
    expect(count_id(state, "a") == 1 && count_id(state, "b") == 1, "run queues the target");
    expect(has_id(state, 0, a) == 1 && has_id(state, 0, w) == 0,
           "the target does not advance on the tick it starts");
    expect(engine_play_runner(state, 1) && engine_play_runner(state, 1)->started_at < 0 &&
               engine_play_runner(state, 1)->mode == ENGINE_MODE_ONCE &&
               engine_play_runner(state, 1)->stack[0].repeats_left == 1,
           "run starts the target as once");
    engine_play_tick(state, 1);
    expect(has_id(state, 1, w) == 1, "the target advances on the next tick");
    expect(count_id(state, "b") == 1, "run does not start a second copy");

    fresh();
    caller_steps[0].a = 1;
    caller_steps[1].type = ENGINE_STEP_WAIT;
    caller_steps[1].a = 100;
    caller_steps[1].action = 0;
    caller_steps[1].key = 0;
    target_steps[0].type = ENGINE_STEP_RUN;
    target_steps[0].a = 0;
    target_steps[0].action = 0;
    target_steps[0].key = 0;
    target_steps[1].type = ENGINE_STEP_WAIT;
    target_steps[1].a = 100;
    set_id(&catalog[0], "a");
    catalog[0].steps = caller_steps;
    catalog[0].step_count = 2;
    catalog[0].mode = ENGINE_MODE_ONCE;
    set_id(&catalog[1], "b");
    catalog[1].steps = target_steps;
    catalog[1].step_count = 2;
    catalog[1].mode = ENGINE_MODE_ONCE;
    engine_play_set_macros(&play, catalog, 2);
    engine_play_start(&play, &catalog[0], -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 1);
    expect(count_id(&play, "a") == 1 && count_id(&play, "b") == 1,
           "a running target is not started again");

    fresh();
    caller_steps[0].type = ENGINE_STEP_RUN;
    caller_steps[0].a = 1;
    caller_steps[1].type = 0;
    catalog[0].steps = caller_steps;
    catalog[0].step_count = 1;
    target_steps[0].type = ENGINE_STEP_RUN;
    target_steps[0].a = 0;
    target_steps[1].type = ENGINE_STEP_WAIT;
    target_steps[1].a = 50;
    catalog[1].steps = target_steps;
    catalog[1].step_count = 2;
    engine_play_set_macros(&play, catalog, 2);
    engine_play_start(&play, &catalog[0], -1, 1);
    engine_play_tick(&play, 0);
    expect(count_id(&play, "a") == 0 && count_id(&play, "b") == 1, "the caller finishes before the target advances");
    engine_play_tick(&play, 1);
    expect(count_id(&play, "a") == 1 && count_id(&play, "b") == 1,
           "a finished caller can be started by its target");

    fresh();
    caller_steps[0].type = ENGINE_STEP_RUN;
    caller_steps[0].a = -1;
    caller_steps[1].type = ENGINE_STEP_KEY;
    caller_steps[1].action = ENGINE_ACT_DOWN;
    caller_steps[1].key = a;
    engine_macro_init(&caller);
    set_id(&caller, "solo");
    caller.steps = caller_steps;
    caller.step_count = 3;
    caller_steps[2].type = ENGINE_STEP_WAIT;
    caller_steps[2].a = 50;
    engine_play_set_macros(&play, catalog, 1);
    engine_play_start(&play, &caller, -1, 1);
    engine_play_tick(&play, 0);
    expect(engine_play_alive(&play) == 1 && has_id(&play, 0, a) == 1, "a missing run target does nothing");

    caller_steps[0].a = 4;
    fresh();
    engine_play_set_macros(&play, catalog, 1);
    engine_play_start(&play, &caller, -1, 1);
    engine_play_tick(&play, 0);
    expect(engine_play_alive(&play) == 1 && has_id(&play, 0, a) == 1, "an index past the catalog does nothing");

    fresh();
    caller_steps[0].a = 0;
    caller_steps[1].type = ENGINE_STEP_KEY;
    caller_steps[1].action = ENGINE_ACT_DOWN;
    caller_steps[1].key = a;
    memset(&filler_step, 0, sizeof filler_step);
    filler_step.type = ENGINE_STEP_WAIT;
    filler_step.a = 100;
    engine_macro_init(&catalog[0]);
    set_id(&catalog[0], "c");
    catalog[0].steps = &filler_step;
    catalog[0].step_count = 1;
    catalog[0].mode = ENGINE_MODE_WHILE_HELD;
    engine_play_start(&play, &catalog[0], -1, 1);
    engine_play_flush(&play);
    target_steps[0].type = ENGINE_STEP_WAIT;
    target_steps[0].a = 50;
    target_steps[0].action = 0;
    target_steps[0].key = 0;
    target_steps[1].type = 0;
    engine_macro_init(&catalog[1]);
    set_id(&catalog[1], "b");
    catalog[1].steps = target_steps;
    catalog[1].step_count = 1;
    catalog[1].exclusive = 1;
    catalog[1].mode = ENGINE_MODE_ONCE;
    engine_macro_init(&caller);
    set_id(&caller, "a");
    caller.steps = caller_steps;
    caller.step_count = 3;
    caller_steps[2].type = ENGINE_STEP_WAIT;
    caller_steps[2].a = 50;
    engine_play_set_macros(&play, &catalog[1], 1);
    caller_steps[0].a = 0;
    engine_play_start(&play, &caller, -1, 1);
    engine_play_tick(&play, 0);
    expect(count_id(&play, "c") == 0 && count_id(&play, "a") == 0 && count_id(&play, "b") == 1,
           "an exclusive target stops the others and still starts");
    expect(has_id(&play, 0, a) == 0, "steps after an exclusive run do not play");

    fresh();
    memset(&filler_step, 0, sizeof filler_step);
    filler_step.type = ENGINE_STEP_WAIT;
    filler_step.a = 1000;
    for (i = 0; i < 31; i++) {
        snprintf(name, sizeof name, "f%02d", i);
        engine_macro_init(&fillers[i]);
        set_id(&fillers[i], name);
        fillers[i].steps = &filler_step;
        fillers[i].step_count = 1;
        fillers[i].mode = ENGINE_MODE_WHILE_HELD;
        expect(engine_play_start(&play, &fillers[i], -1, 1) == 1, "filler starts");
    }
    engine_play_flush(&play);
    target_steps[0].type = ENGINE_STEP_WAIT;
    target_steps[0].a = 10;
    engine_macro_init(&catalog[0]);
    set_id(&catalog[0], "extra");
    catalog[0].steps = target_steps;
    catalog[0].step_count = 1;
    caller_steps[0].type = ENGINE_STEP_RUN;
    caller_steps[0].a = 0;
    caller_steps[1].type = 0;
    engine_macro_init(&caller);
    set_id(&caller, "full");
    caller.steps = caller_steps;
    caller.step_count = 1;
    engine_play_set_macros(&play, catalog, 1);
    expect(engine_play_start(&play, &caller, -1, 1) == 1, "the last slot still starts");
    engine_play_tick(&play, 0);
    expect(count_id(&play, "extra") == 0 && count_id(&play, "full") == 0 && engine_play_alive(&play) == 31,
           "a full list refuses the run and leaves the others");
}

static struct engine_runner *live_id(struct engine_play *state, const char *id)
{
    int i;

    for (i = 0; i < state->runner_count; i++) {
        if (!state->runners[i].done && strcmp(state->runners[i].id, id) == 0)
            return &state->runners[i];
    }
    return NULL;
}

static void press(struct engine_fingers *fingers, struct engine_play *state, uint16_t id, int down)
{
    engine_fingers_set(fingers, id, down);
    engine_play_set_fingers(state, fingers);
}

static void check_hold_loop(void)
{
    struct engine_play *state = fresh();
    struct engine_fingers fingers;
    struct engine_step loop_steps[3];
    struct engine_step shot_steps[3];
    struct engine_step wall_steps[2];
    struct engine_step wall2_steps[2];
    struct engine_step plain_steps[1];
    struct engine_step swap_steps[6];
    struct engine_step drain_steps[7];
    struct engine_macro loop;
    struct engine_macro shot;
    struct engine_macro wall;
    struct engine_macro wall2;
    struct engine_macro plain;
    struct engine_macro catalog[4];
    int32_t mute_shot[2];
    uint16_t b = kid("b");
    uint16_t x = kid("x");
    uint16_t n = kid("n");
    uint16_t m = kid("m");
    uint16_t h = kid("h");
    uint16_t w = kid("w");
    uint16_t z = kid("z");
    uint16_t y = kid("y");
    struct engine_runner *runner;

    engine_fingers_clear(&fingers);
    memset(loop_steps, 0, sizeof loop_steps);
    loop_steps[0].type = ENGINE_STEP_HOLD_LOOP;
    loop_steps[0].first = 1;
    loop_steps[0].count = 2;
    loop_steps[1].type = ENGINE_STEP_KEY;
    loop_steps[1].action = ENGINE_ACT_DOWN;
    loop_steps[1].key = x;
    loop_steps[2].type = ENGINE_STEP_WAIT;
    loop_steps[2].a = 10;
    engine_macro_init(&loop);
    set_id(&loop, "loop");
    loop.steps = loop_steps;
    loop.step_count = 3;
    loop.top_count = 1;
    loop.trigger = b;
    press(&fingers, state, b, 1);
    engine_play_start(state, &loop, -1, 1);
    engine_play_tick(state, 0);
    runner = live_id(state, "loop");
    expect(runner != NULL && has_id(state, 0, x) && runner->cycles == 0,
           "hold loop presses while the finger is down");
    engine_play_tick(state, 10);
    runner = live_id(state, "loop");
    expect(runner != NULL && has_id(state, 10, x) && runner->cycles == 1,
           "hold loop restarts the body");
    press(&fingers, state, b, 0);
    engine_play_tick(state, 20);
    expect(live_id(state, "loop") == NULL && has_id(state, 20, x) == 0,
           "hold loop pops when the finger comes up");

    fresh();
    engine_fingers_clear(&fingers);
    loop_steps[0].count = 0;
    engine_play_set_macros(&play, NULL, 0);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &loop, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "loop");
    expect(runner != NULL && runner->wait_until == 1 && runner->cycles == 1,
           "an empty hold loop waits 1 ms");
    engine_play_tick(&play, 1);
    runner = live_id(&play, "loop");
    expect(runner != NULL && runner->wait_until == 2, "an empty hold loop keeps waiting");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 2);
    expect(live_id(&play, "loop") == NULL, "an empty hold loop ends when the finger comes up");

    fresh();
    engine_fingers_clear(&fingers);
    loop_steps[0].count = 2;
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &loop, -1, 1);
    engine_play_tick(&play, 0);
    expect(live_id(&play, "loop") == NULL && has_id(&play, 0, x) == 0,
           "a finger that is already up skips the hold loop");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_fingers(&play, &fingers);
    engine_play_start(&play, &loop, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "loop");
    expect(runner != NULL && has_id(&play, 0, x),
           "a key that was never reported still starts the hold loop");
    engine_play_tick(&play, 10);
    expect(live_id(&play, "loop") == NULL, "that hold loop ends after one pass");

    memset(shot_steps, 0, sizeof shot_steps);
    shot_steps[0].type = ENGINE_STEP_HOLD_LOOP;
    shot_steps[0].first = 1;
    shot_steps[0].count = 2;
    shot_steps[1].type = ENGINE_STEP_KEY;
    shot_steps[1].action = ENGINE_ACT_DOWN;
    shot_steps[1].key = x;
    shot_steps[2].type = ENGINE_STEP_WAIT;
    shot_steps[2].a = 1000;
    engine_macro_init(&shot);
    set_id(&shot, "shot");
    shot.steps = shot_steps;
    shot.step_count = 3;
    shot.top_count = 1;
    shot.trigger = b;
    shot.advanced = 1;

    memset(wall_steps, 0, sizeof wall_steps);
    wall_steps[0].type = ENGINE_STEP_HOLD_LOOP;
    wall_steps[0].first = 1;
    wall_steps[0].count = 1;
    wall_steps[0].mute_at = 0;
    wall_steps[0].mute_n = 2;
    wall_steps[1].type = ENGINE_STEP_WAIT;
    wall_steps[1].a = 10;
    engine_macro_init(&wall);
    set_id(&wall, "wall");
    wall.steps = wall_steps;
    wall.step_count = 2;
    wall.top_count = 1;
    wall.trigger = n;
    mute_shot[0] = 0;
    mute_shot[1] = 1;
    wall.mutes = mute_shot;
    wall.mute_count = 2;
    memcpy(wall2_steps, wall_steps, sizeof wall2_steps);
    wall2_steps[0].mute_n = 1;
    wall2 = wall;
    set_id(&wall2, "wall2");
    wall2.trigger = m;
    wall2.steps = wall2_steps;
    wall2.mute_count = 1;

    fresh();
    engine_fingers_clear(&fingers);
    catalog[0] = shot;
    catalog[1] = wall;
    catalog[2] = wall2;
    engine_play_set_macros(&play, catalog, 3);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, x) && live_id(&play, "shot") != NULL &&
               live_id(&play, "shot")->paused == 0,
           "shot plays while its key is held");
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &wall, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "shot");
    expect(engine_play_muted(&play, "shot") && !engine_play_muted(&play, "wall") &&
               runner != NULL && runner->paused && has_id(&play, 0, x) == 0,
           "wall pauses shot and drops its keys");
    expect(engine_play_mute_count(&play, "shot") == 1, "mute counts once");
    press(&fingers, &play, m, 1);
    engine_play_start(&play, &wall2, -1, 1);
    engine_play_tick(&play, 0);
    expect(engine_play_mute_count(&play, "shot") == 2 && live_id(&play, "shot")->paused,
           "a second mute does not unpause early");
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 10);
    expect(engine_play_mute_count(&play, "shot") == 1 && live_id(&play, "shot")->paused &&
               live_id(&play, "wall") == NULL,
           "releasing one mute leaves the other");
    press(&fingers, &play, m, 0);
    engine_play_tick(&play, 20);
    runner = live_id(&play, "shot");
    expect(engine_play_muted(&play, "shot") == 0 && runner != NULL && runner->paused == 0 &&
               runner->done == 0,
           "releasing the last mute unpauses shot");
    engine_play_tick(&play, 1000);
    expect(has_id(&play, 1000, x) && live_id(&play, "shot") != NULL,
           "shot plays again while its key is still held");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_macros(&play, catalog, 3);
    press(&fingers, &play, b, 1);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_start(&play, &wall, -1, 1);
    engine_play_tick(&play, 0);
    press(&fingers, &play, b, 0);
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 1000);
    expect(live_id(&play, "shot") == NULL && has_id(&play, 1000, x) == 0,
           "shot stays stopped if its key came up during the pause");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_macros(&play, catalog, 3);
    press(&fingers, &play, b, 1);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &wall, -1, 1);
    engine_play_tick(&play, 0);
    expect(engine_play_muted(&play, "shot") && count_id(&play, "shot") == 0,
           "a held key does not start while muted");
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 10);
    expect(count_id(&play, "shot") == 1 && has_id(&play, 10, x) == 0,
           "a key that stayed down starts when the mute lifts");
    engine_play_tick(&play, 11);
    expect(has_id(&play, 11, x), "that restarted hold sends again");

    memset(plain_steps, 0, sizeof plain_steps);
    plain_steps[0].type = ENGINE_STEP_KEY;
    plain_steps[0].action = ENGINE_ACT_DOWN;
    plain_steps[0].key = w;
    engine_macro_init(&plain);
    set_id(&plain, "hold-w");
    plain.steps = plain_steps;
    plain.step_count = 1;
    plain.mode = ENGINE_MODE_WHILE_HELD;
    plain.trigger = h;
    catalog[0] = plain;
    mute_shot[0] = 0;
    wall.mutes = mute_shot;
    wall.mute_count = 1;
    wall_steps[0].mute_n = 1;
    catalog[1] = wall;

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_macros(&play, catalog, 2);
    press(&fingers, &play, h, 1);
    engine_play_start(&play, &plain, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, w), "plain hold macro holds w");
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &wall, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "hold-w");
    expect(runner != NULL && runner->paused && has_id(&play, 0, w) == 0,
           "gate mutes the plain hold");
    press(&fingers, &play, h, 0);
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 10);
    expect(live_id(&play, "hold-w") == NULL && has_id(&play, 10, w) == 0,
           "unmute does not replay a hold whose key came up");

    memset(swap_steps, 0, sizeof swap_steps);
    swap_steps[0].type = ENGINE_STEP_HOLD_LOOP;
    swap_steps[0].first = 3;
    swap_steps[0].count = 1;
    swap_steps[0].a = 40;
    swap_steps[1].type = ENGINE_STEP_KEY;
    swap_steps[1].action = ENGINE_ACT_DOWN;
    swap_steps[1].key = x;
    swap_steps[2].type = ENGINE_STEP_WAIT;
    swap_steps[2].a = 10;
    swap_steps[3].type = ENGINE_STEP_WAIT;
    swap_steps[3].a = 10;
    engine_macro_init(&loop);
    set_id(&loop, "swap");
    loop.steps = swap_steps;
    loop.step_count = 4;
    loop.top_count = 3;
    loop.trigger = b;
    loop.speed = 2.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &loop, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, x) == 0 && live_id(&play, "swap") != NULL,
           "swap time starts inside the hold loop");
    engine_play_tick(&play, 20);
    expect(has_id(&play, 20, x) == 0 && live_id(&play, "swap") != NULL,
           "swap time is not divided by speed");
    engine_play_tick(&play, 40);
    expect(has_id(&play, 40, x), "holding past the time leaves the hold loop");

    memset(drain_steps, 0, sizeof drain_steps);
    drain_steps[0].type = ENGINE_STEP_HOLD_LOOP;
    drain_steps[0].first = 3;
    drain_steps[0].count = 4;
    drain_steps[1].type = ENGINE_STEP_KEY;
    drain_steps[1].action = ENGINE_ACT_DOWN;
    drain_steps[1].key = y;
    drain_steps[2].type = ENGINE_STEP_WAIT;
    drain_steps[2].a = 10;
    drain_steps[3].type = ENGINE_STEP_KEY;
    drain_steps[3].action = ENGINE_ACT_DOWN;
    drain_steps[3].key = x;
    drain_steps[4].type = ENGINE_STEP_WAIT;
    drain_steps[4].a = 50;
    drain_steps[5].type = ENGINE_STEP_KEY;
    drain_steps[5].action = ENGINE_ACT_UP;
    drain_steps[5].key = x;
    drain_steps[6].type = ENGINE_STEP_KEY;
    drain_steps[6].action = ENGINE_ACT_DOWN;
    drain_steps[6].key = z;
    engine_macro_init(&loop);
    set_id(&loop, "drain");
    loop.steps = drain_steps;
    loop.step_count = 7;
    loop.top_count = 3;
    loop.trigger = b;
    loop.speed = 1.0;
    loop.release_stop = ENGINE_STOP_NEXT_UP;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &loop, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, x), "hold starts with the key down");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 50);
    runner = live_id(&play, "drain");
    expect(runner != NULL && has_id(&play, 50, y) && has_id(&play, 50, z) == 0 &&
               has_id(&play, 50, x) == 0,
           "release skips a later down and runs the next block");

    drain_steps[0].b = ENGINE_STOP_FINISH;
    engine_macro_init(&loop);
    set_id(&loop, "full");
    loop.steps = drain_steps;
    loop.step_count = 7;
    loop.top_count = 3;
    loop.trigger = b;
    loop.release_stop = ENGINE_STOP_NEXT_UP;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &loop, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_ask_stop(&play, "full");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 50);
    expect(live_id(&play, "full") != NULL && has_id(&play, 50, z),
           "block play full still presses the later down");
}

static void check_swap_loop(void)
{
    struct engine_play *state = fresh();
    struct engine_fingers fingers;
    struct engine_step gate_steps[5];
    struct engine_step chain_steps[10];
    struct engine_step exit_steps[4];
    struct engine_step shot_steps[3];
    struct engine_step mute_steps[2];
    struct engine_macro gate;
    struct engine_macro shot;
    struct engine_macro catalog[2];
    int32_t mutes[2];
    uint16_t b = kid("b");
    uint16_t n = kid("n");
    uint16_t a = kid("a");
    uint16_t x = kid("x");
    uint16_t y = kid("y");
    struct engine_runner *runner;

    engine_fingers_clear(&fingers);
    memset(gate_steps, 0, sizeof gate_steps);
    gate_steps[0].type = ENGINE_STEP_SWAP_LOOP;
    gate_steps[0].first = 3;
    gate_steps[0].count = 2;
    gate_steps[0].c = 40;
    gate_steps[1].type = ENGINE_STEP_KEY;
    gate_steps[1].action = ENGINE_ACT_DOWN;
    gate_steps[1].key = y;
    gate_steps[2].type = ENGINE_STEP_WAIT;
    gate_steps[2].a = 1000;
    gate_steps[3].type = ENGINE_STEP_KEY;
    gate_steps[3].action = ENGINE_ACT_DOWN;
    gate_steps[3].key = x;
    gate_steps[4].type = ENGINE_STEP_WAIT;
    gate_steps[4].a = 10;
    engine_macro_init(&gate);
    set_id(&gate, "gate");
    gate.steps = gate_steps;
    gate.step_count = 5;
    gate.top_count = 3;
    gate.trigger = b;
    gate.speed = 2.0;
    press(&fingers, state, b, 1);
    engine_play_start(state, &gate, -1, 1);
    engine_play_tick(state, 0);
    runner = live_id(state, "gate");
    expect(runner != NULL && runner->wait_until == 1 && has_id(state, 0, x) == 0 &&
               has_id(state, 0, y) == 0,
           "swap loop waits 1 ms while the finger is short of the time");
    engine_play_tick(state, 20);
    runner = live_id(state, "gate");
    expect(runner != NULL && runner->wait_until == 21 && has_id(state, 20, x) == 0,
           "swap loop time is not divided by speed");
    engine_play_tick(state, 40);
    runner = live_id(state, "gate");
    expect(runner != NULL && has_id(state, 40, x) && runner->wait_until == 45,
           "holding past the time starts the swap loop");

    fresh();
    engine_fingers_clear(&fingers);
    gate.speed = 1.0;
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 10);
    expect(live_id(&play, "gate") != NULL && has_id(&play, 10, y) && has_id(&play, 10, x) == 0,
           "a release before the time skips the swap loop");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y) && has_id(&play, 0, x) == 0,
           "a finger that is already up skips the swap loop");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_fingers(&play, &fingers);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y) && has_id(&play, 0, x) == 0,
           "a key that was never reported skips the swap loop");

    fresh();
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y) && has_id(&play, 0, x) == 0,
           "a missing finger snapshot skips the swap loop");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    gate.trigger = 0;
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y) && has_id(&play, 0, x) == 0,
           "an empty trigger skips the swap loop");
    gate.trigger = b;

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_flush(&play);
    engine_play_ask_stop(&play, "gate");
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y) && has_id(&play, 0, x) == 0,
           "stopping skips the swap loop");

    gate_steps[0].c = 0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, x) && has_id(&play, 0, y) == 0,
           "a zero after time starts the swap loop now");

    gate_steps[0].c = -5;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, x), "a negative after time starts the swap loop now");

    gate_steps[0].c = 0;
    gate_steps[0].count = 0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "gate");
    expect(runner != NULL && runner->wait_until == 1 && runner->cycles == 1,
           "an empty swap loop waits 1 ms");

    memset(chain_steps, 0, sizeof chain_steps);
    chain_steps[0].type = ENGINE_STEP_HOLD_LOOP;
    chain_steps[0].first = 2;
    chain_steps[0].count = 4;
    chain_steps[0].a = 40;
    chain_steps[1].type = ENGINE_STEP_SWAP_LOOP;
    chain_steps[1].first = 6;
    chain_steps[1].count = 4;
    chain_steps[1].c = 40;
    chain_steps[2].type = ENGINE_STEP_KEY;
    chain_steps[2].action = ENGINE_ACT_DOWN;
    chain_steps[2].key = a;
    chain_steps[3].type = ENGINE_STEP_WAIT;
    chain_steps[3].a = 10;
    chain_steps[4].type = ENGINE_STEP_KEY;
    chain_steps[4].action = ENGINE_ACT_UP;
    chain_steps[4].key = a;
    chain_steps[5].type = ENGINE_STEP_WAIT;
    chain_steps[5].a = 10;
    chain_steps[6].type = ENGINE_STEP_KEY;
    chain_steps[6].action = ENGINE_ACT_DOWN;
    chain_steps[6].key = x;
    chain_steps[7].type = ENGINE_STEP_WAIT;
    chain_steps[7].a = 10;
    chain_steps[8].type = ENGINE_STEP_KEY;
    chain_steps[8].action = ENGINE_ACT_UP;
    chain_steps[8].key = x;
    chain_steps[9].type = ENGINE_STEP_WAIT;
    chain_steps[9].a = 10;
    engine_macro_init(&gate);
    set_id(&gate, "swap");
    gate.steps = chain_steps;
    gate.step_count = 10;
    gate.top_count = 2;
    gate.trigger = b;
    gate.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, a) && has_id(&play, 0, x) == 0,
           "swap after starts on the first loop");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 20);
    expect(has_id(&play, 20, x) == 0, "a quick tap never reaches the swap loop");
    engine_play_tick(&play, 30);
    expect(live_id(&play, "swap") == NULL && has_id(&play, 30, x) == 0,
           "a quick tap ends without the second loop");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 20);
    engine_play_tick(&play, 40);
    engine_play_tick(&play, 50);
    expect(has_id(&play, 50, x) && has_id(&play, 50, a) == 0,
           "holding past the time swaps to the second loop");

    memset(exit_steps, 0, sizeof exit_steps);
    exit_steps[0].type = ENGINE_STEP_SWAP_LOOP;
    exit_steps[0].first = 3;
    exit_steps[0].count = 1;
    exit_steps[0].a = 30;
    exit_steps[1].type = ENGINE_STEP_KEY;
    exit_steps[1].action = ENGINE_ACT_DOWN;
    exit_steps[1].key = x;
    exit_steps[2].type = ENGINE_STEP_WAIT;
    exit_steps[2].a = 1000;
    exit_steps[3].type = ENGINE_STEP_WAIT;
    exit_steps[3].a = 10;
    engine_macro_init(&gate);
    set_id(&gate, "exit");
    gate.steps = exit_steps;
    gate.step_count = 4;
    gate.top_count = 3;
    gate.trigger = b;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 20);
    expect(has_id(&play, 20, x) == 0 && live_id(&play, "exit") != NULL,
           "swap loop stays until its own swap time");
    engine_play_tick(&play, 30);
    expect(has_id(&play, 30, x), "swap loop pops when its swap time is reached");

    memset(shot_steps, 0, sizeof shot_steps);
    shot_steps[0].type = ENGINE_STEP_HOLD_LOOP;
    shot_steps[0].first = 1;
    shot_steps[0].count = 2;
    shot_steps[1].type = ENGINE_STEP_KEY;
    shot_steps[1].action = ENGINE_ACT_DOWN;
    shot_steps[1].key = x;
    shot_steps[2].type = ENGINE_STEP_WAIT;
    shot_steps[2].a = 1000;
    engine_macro_init(&shot);
    set_id(&shot, "shot");
    shot.steps = shot_steps;
    shot.step_count = 3;
    shot.top_count = 1;
    shot.trigger = b;
    memset(mute_steps, 0, sizeof mute_steps);
    mute_steps[0].type = ENGINE_STEP_SWAP_LOOP;
    mute_steps[0].first = 1;
    mute_steps[0].count = 1;
    mute_steps[0].mute_at = 0;
    mute_steps[0].mute_n = 2;
    mute_steps[1].type = ENGINE_STEP_WAIT;
    mute_steps[1].a = 10;
    engine_macro_init(&gate);
    set_id(&gate, "wall");
    gate.steps = mute_steps;
    gate.step_count = 2;
    gate.top_count = 1;
    gate.trigger = n;
    mutes[0] = 0;
    mutes[1] = 1;
    gate.mutes = mutes;
    gate.mute_count = 2;
    catalog[0] = shot;
    catalog[1] = gate;
    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_macros(&play, catalog, 2);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &gate, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "shot");
    expect(engine_play_muted(&play, "shot") && engine_play_muted(&play, "wall") == 0 &&
               runner != NULL && runner->paused && has_id(&play, 0, x) == 0,
           "swap loop pauses the other macro and leaves itself out");
}

static void check_burst(void)
{
    struct engine_play *state = fresh();
    struct engine_fingers fingers;
    struct engine_step steps[6];
    struct engine_macro burst;
    uint16_t b = kid("b");
    uint16_t x = kid("x");
    uint16_t y = kid("y");
    uint16_t seven = kid("7");
    struct engine_runner *runner;

    engine_fingers_clear(&fingers);
    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_BURST;
    steps[0].first = 3;
    steps[0].count = 2;
    steps[0].a = 100;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = y;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 10;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = x;
    steps[4].type = ENGINE_STEP_WAIT;
    steps[4].a = 10;
    engine_macro_init(&burst);
    set_id(&burst, "after");
    burst.steps = steps;
    burst.step_count = 5;
    burst.top_count = 3;
    burst.trigger = b;
    burst.speed = 2.0;
    press(&fingers, state, b, 1);
    engine_play_start(state, &burst, -1, 1);
    engine_play_tick(state, 0);
    runner = live_id(state, "after");
    expect(runner != NULL && runner->wait_until == 1 && has_id(state, 0, x) == 0 &&
               has_id(state, 0, y) == 0,
           "burst waits 1 ms while the trigger is down");
    engine_play_tick(state, 50);
    runner = live_id(state, "after");
    expect(runner != NULL && has_id(state, 50, x) == 0, "burst stays held past forMs");
    press(&fingers, state, b, 0);
    engine_play_tick(state, 51);
    runner = live_id(state, "after");
    expect(runner != NULL && has_id(state, 51, x) && has_id(state, 51, y) == 0 &&
               runner->wait_until == 56,
           "a release runs the burst once and scales the child wait");

    steps[0].a = 0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, x) && has_id(&play, 0, y) == 0, "forMs 0 runs the children once");

    steps[0].a = -5;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, x) && live_id(&play, "after") != NULL,
           "a negative forMs runs the children once");

    steps[0].a = 1;
    burst.speed = 2.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "after");
    expect(runner != NULL && runner->wait_until == 5 && has_id(&play, 0, x) &&
               runner->stack_count == 2 && runner->stack[1].burst_until == 0,
           "forMs that truncates to 0 ticks runs once");

    play.freq = 10000000;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    play.freq = 10000000;
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "after");
    expect(runner != NULL && runner->stack_count == 2 &&
               runner->stack[1].burst_until == 5000 && has_id(&play, 0, x),
           "half a millisecond of forMs keeps a burst deadline");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_BURST;
    steps[0].first = 1;
    steps[0].count = 2;
    steps[0].a = 25;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = x;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 10;
    engine_macro_init(&burst);
    set_id(&burst, "loop");
    burst.steps = steps;
    burst.step_count = 3;
    burst.top_count = 1;
    burst.trigger = b;
    burst.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 20);
    runner = live_id(&play, "loop");
    expect(runner != NULL && has_id(&play, 20, x) && runner->wait_until == 30,
           "burst repeats the children until forMs");
    engine_play_tick(&play, 30);
    expect(live_id(&play, "loop") == NULL, "burst ends once forMs has passed");

    steps[0].a = 40;
    burst.speed = 2.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "loop");
    expect(runner != NULL && has_id(&play, 0, x) && runner->wait_until == 5,
           "forMs is divided by speed");
    engine_play_tick(&play, 5);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 15);
    runner = live_id(&play, "loop");
    expect(runner != NULL && has_id(&play, 15, x), "the sped burst is still inside forMs");
    engine_play_tick(&play, 20);
    expect(live_id(&play, "loop") == NULL, "the sped burst ends at forMs over speed");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_BURST;
    steps[0].first = 2;
    steps[0].count = 0;
    steps[0].a = 30;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = y;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 10;
    engine_macro_init(&burst);
    set_id(&burst, "empty");
    burst.steps = steps;
    burst.step_count = 3;
    burst.top_count = 3;
    burst.trigger = b;
    burst.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "empty");
    expect(runner != NULL && runner->wait_until == 1 && has_id(&play, 0, y) == 0,
           "an empty burst yields until forMs");
    engine_play_tick(&play, 29);
    expect(has_id(&play, 29, y) == 0 && live_id(&play, "empty") != NULL,
           "an empty burst is still waiting inside forMs");
    engine_play_tick(&play, 30);
    expect(has_id(&play, 30, y) && live_id(&play, "empty") != NULL,
           "an empty burst runs the next step at forMs");

    burst.trigger = 0;
    steps[0].a = 0;
    steps[0].count = 0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y), "an empty trigger does not hold burst");

    burst.trigger = b;
    fresh();
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y), "a missing snapshot does not hold burst");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_fingers(&play, &fingers);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y), "a key that was never reported does not hold burst");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_KEY;
    steps[0].action = ENGINE_ACT_DOWN;
    steps[0].key = seven;
    steps[1].type = ENGINE_STEP_BURST;
    steps[1].first = 2;
    steps[1].count = 1;
    steps[1].a = 55;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_UP;
    steps[2].key = seven;
    engine_macro_init(&burst);
    set_id(&burst, "seven");
    burst.steps = steps;
    burst.step_count = 3;
    burst.top_count = 2;
    burst.trigger = b;
    burst.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &burst, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 100);
    runner = live_id(&play, "seven");
    expect(runner != NULL && has_id(&play, 100, seven),
           "after release does not send the up while the trigger is held");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 101);
    runner = live_id(&play, "seven");
    expect(runner != NULL && has_id(&play, 101, seven) == 0 && runner->stack_count == 2 &&
               runner->stack[1].burst_until == 156,
           "after release sends the up when the trigger comes up");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_BURST;
    steps[0].first = 2;
    steps[0].count = 2;
    steps[0].a = 100;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = y;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_DOWN;
    steps[2].key = x;
    steps[3].type = ENGINE_STEP_WAIT;
    steps[3].a = 10;
    engine_macro_init(&burst);
    set_id(&burst, "bare");
    burst.steps = steps;
    burst.step_count = 4;
    burst.top_count = 2;
    burst.trigger = b;
    fresh();
    engine_play_start(&play, &burst, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(has_id(&play, 0, x) && has_id(&play, 0, y) == 0 && play.runners[0].done == 0,
           "burst on the bare runner does not wait");
}

static void check_if_short(void)
{
    struct engine_play *state = fresh();
    struct engine_fingers fingers;
    struct engine_step steps[9];
    struct engine_macro tap;
    uint16_t b = kid("b");
    uint16_t x = kid("x");
    uint16_t y = kid("y");
    uint16_t z = kid("z");
    uint16_t o = kid("o");
    uint16_t nine = kid("9");
    struct engine_runner *runner;

    engine_fingers_clear(&fingers);
    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_IF_SHORT;
    steps[0].first = 3;
    steps[0].count = 1;
    steps[0].a = 40;
    steps[0].b = 3;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = y;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 10;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = x;
    engine_macro_init(&tap);
    set_id(&tap, "under");
    tap.steps = steps;
    tap.step_count = 4;
    tap.top_count = 3;
    tap.trigger = b;
    tap.speed = 2.0;
    press(&fingers, state, b, 1);
    engine_play_start(state, &tap, -1, 1);
    engine_play_tick(state, 0);
    runner = live_id(state, "under");
    expect(runner != NULL && runner->wait_until == 1 && has_id(state, 0, x) == 0 &&
               has_id(state, 0, y) == 0 && runner->took_short == 0,
           "ifShort waits 1 ms while the hold is still short");
    engine_play_tick(state, 30);
    runner = live_id(state, "under");
    expect(runner != NULL && runner->wait_until == 31 && has_id(state, 30, x) == 0 &&
               has_id(state, 30, y) == 0,
           "underMs is not divided by speed");
    engine_play_tick(state, 40);
    runner = live_id(state, "under");
    expect(runner != NULL && has_id(state, 40, y) && has_id(state, 40, x) == 0 &&
               runner->took_short == 0 && runner->wait_until == 45,
           "a long hold skips ifShort and still runs the next step");

    steps[0].a = 0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "under");
    expect(runner != NULL && has_id(&play, 0, y) && has_id(&play, 0, x) == 0 &&
               runner->took_short == 0,
           "underMs 0 skips the short children");

    steps[0].a = -4;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y) && has_id(&play, 0, x) == 0,
           "a negative underMs skips the short children");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_HOLD_LOOP;
    steps[0].first = 5;
    steps[0].count = 1;
    steps[1].type = ENGINE_STEP_IF_SHORT;
    steps[1].first = 6;
    steps[1].count = 1;
    steps[1].a = 150;
    steps[1].b = 3;
    steps[2].type = ENGINE_STEP_BURST;
    steps[2].first = 7;
    steps[2].count = 2;
    steps[5].type = ENGINE_STEP_WAIT;
    steps[5].a = 26;
    steps[6].type = ENGINE_STEP_WAIT;
    steps[6].a = 26;
    steps[7].type = ENGINE_STEP_KEY;
    steps[7].action = ENGINE_ACT_DOWN;
    steps[7].key = nine;
    steps[8].type = ENGINE_STEP_WAIT;
    steps[8].a = 10;
    engine_macro_init(&tap);
    set_id(&tap, "short");
    tap.steps = steps;
    tap.step_count = 9;
    tap.top_count = 3;
    tap.trigger = b;
    tap.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 26);
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 52);
    runner = live_id(&play, "short");
    expect(runner != NULL && runner->cycles == 2 && has_id(&play, 52, nine) == 0 &&
               runner->took_short,
           "a short hold counts two cycles and tops up ifShort");
    engine_play_tick(&play, 78);
    runner = live_id(&play, "short");
    expect(runner != NULL && has_id(&play, 78, nine) && runner->cycles == 2,
           "burst runs after the short tap is topped up");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 160);
    runner = live_id(&play, "short");
    expect(runner != NULL && runner->cycles == 1 && has_id(&play, 160, nine) == 0,
           "a long hold is still inside the loop at 160 ms");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 186);
    runner = live_id(&play, "short");
    expect(runner != NULL && runner->cycles == 2 && has_id(&play, 186, nine) &&
               runner->took_short == 0,
           "a long hold skips the extra cycles and runs burst");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_IF_SHORT;
    steps[0].first = 3;
    steps[0].count = 2;
    steps[0].a = 130;
    steps[0].b = 1;
    steps[1].type = ENGINE_STEP_HOLD_LOOP;
    steps[1].first = 5;
    steps[1].count = 2;
    steps[2].type = ENGINE_STEP_BURST;
    steps[2].first = 7;
    steps[2].count = 2;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = o;
    steps[4].type = ENGINE_STEP_WAIT;
    steps[4].a = 10;
    steps[5].type = ENGINE_STEP_KEY;
    steps[5].action = ENGINE_ACT_DOWN;
    steps[5].key = x;
    steps[6].type = ENGINE_STEP_WAIT;
    steps[6].a = 10;
    steps[7].type = ENGINE_STEP_KEY;
    steps[7].action = ENGINE_ACT_DOWN;
    steps[7].key = z;
    steps[8].type = ENGINE_STEP_WAIT;
    steps[8].a = 10;
    engine_macro_init(&tap);
    set_id(&tap, "split");
    tap.steps = steps;
    tap.step_count = 9;
    tap.top_count = 3;
    tap.trigger = b;
    tap.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "split");
    expect(runner != NULL && has_id(&play, 0, o) && has_id(&play, 0, x) == 0 &&
               has_id(&play, 0, z) == 0 && runner->took_short,
           "a quick tap takes ifShort and does not start the hold");
    engine_play_tick(&play, 10);
    expect(has_id(&play, 10, o) && has_id(&play, 10, z) && has_id(&play, 10, x) == 0,
           "tookShort skips the hold and still runs burst");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, o) == 0 && has_id(&play, 0, x) == 0, "a held trigger waits out underMs");
    engine_play_tick(&play, 130);
    runner = live_id(&play, "split");
    expect(runner != NULL && has_id(&play, 130, x) && has_id(&play, 130, o) == 0 &&
               has_id(&play, 130, z) == 0 && runner->took_short == 0,
           "holding past underMs starts the hold and skips ifShort");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 140);
    expect(has_id(&play, 140, x) && has_id(&play, 140, z) && has_id(&play, 140, o) == 0,
           "the hold lets go into burst");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_IF_SHORT;
    steps[0].first = 3;
    steps[0].count = 1;
    steps[0].a = 150;
    steps[0].b = 0;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = y;
    steps[2].type = ENGINE_STEP_BURST;
    steps[2].first = 4;
    steps[2].count = 2;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = x;
    steps[4].type = ENGINE_STEP_KEY;
    steps[4].action = ENGINE_ACT_DOWN;
    steps[4].key = z;
    steps[5].type = ENGINE_STEP_WAIT;
    steps[5].a = 10;
    engine_macro_init(&tap);
    set_id(&tap, "none");
    tap.steps = steps;
    tap.step_count = 6;
    tap.top_count = 3;
    tap.trigger = b;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "none");
    expect(runner != NULL && runner->took_short && has_id(&play, 0, z) &&
               has_id(&play, 0, y) == 0 && has_id(&play, 0, x) == 0,
           "minCycles below 1 still sets tookShort and runs burst");

    tap.trigger = 0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, z) && has_id(&play, 0, x) == 0,
           "an empty trigger takes the short path");

    tap.trigger = b;
    fresh();
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, z) && has_id(&play, 0, x) == 0,
           "a missing snapshot takes the short path");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_fingers(&play, &fingers);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, z), "a key that was never reported takes the short path");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_IF_SHORT;
    steps[0].first = 2;
    steps[0].count = 2;
    steps[0].a = 150;
    steps[0].b = 1;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = y;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_DOWN;
    steps[2].key = x;
    steps[3].type = ENGINE_STEP_WAIT;
    steps[3].a = 10;
    engine_macro_init(&tap);
    set_id(&tap, "bare");
    tap.steps = steps;
    tap.step_count = 4;
    tap.top_count = 2;
    tap.trigger = b;
    fresh();
    engine_play_start(&play, &tap, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(has_id(&play, 0, x) && has_id(&play, 0, y) == 0 && play.runners[0].took_short &&
               play.runners[0].done == 0,
           "ifShort on the bare runner does not yield");
}

static void check_once_held(void)
{
    struct engine_fingers fingers;
    struct engine_step steps[8];
    struct engine_macro shot;
    struct engine_runner *runner;
    uint16_t n = kid("n");
    uint16_t o = kid("o");
    uint16_t nine = kid("9");
    uint16_t x = kid("x");
    uint16_t y = kid("y");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_ONCE_HELD;
    steps[0].first = 2;
    steps[0].count = 3;
    steps[0].a = 70;
    steps[1].type = ENGINE_STEP_BURST;
    steps[1].first = 5;
    steps[1].count = 2;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_DOWN;
    steps[2].key = o;
    steps[3].type = ENGINE_STEP_WAIT;
    steps[3].a = 10;
    steps[4].type = ENGINE_STEP_KEY;
    steps[4].action = ENGINE_ACT_UP;
    steps[4].key = o;
    steps[5].type = ENGINE_STEP_KEY;
    steps[5].action = ENGINE_ACT_DOWN;
    steps[5].key = nine;
    steps[6].type = ENGINE_STEP_WAIT;
    steps[6].a = 10;
    engine_macro_init(&shot);
    set_id(&shot, "edit-shot");
    shot.steps = steps;
    shot.step_count = 7;
    shot.top_count = 2;
    shot.mode = ENGINE_MODE_WHILE_HELD;
    shot.trigger = n;
    shot.speed = 1.0;

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, o) && has_id(&play, 0, nine) == 0,
           "one press starts with the chord");
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->cycles == 1 && has_id(&play, 10, nine) == 0,
           "a fast release keeps the runner and does not shoot yet");
    engine_play_tick(&play, 40);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->cycles == 1 && has_id(&play, 40, nine) == 0,
           "the shot waits out the gap after one chord");
    engine_play_tick(&play, 80);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->cycles == 1 && has_id(&play, 80, nine),
           "a fast press shoots 70 ms after the chord");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    expect(has_id(&play, 10, nine) == 0, "holding through the chord does not shoot");
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 50);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->cycles == 1 && has_id(&play, 50, nine) == 0,
           "letting go during the gap does not shoot early");
    engine_play_tick(&play, 80);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->cycles == 1 && has_id(&play, 80, nine),
           "a press released during the gap still waits the full 70 ms");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 80);
    expect(has_id(&play, 80, o) && has_id(&play, 80, nine) == 0,
           "still holding when the gap ends edits again");
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 90);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->cycles == 2 && has_id(&play, 90, nine) == 0,
           "release after a hold finishes that pass before the shot");
    engine_play_tick(&play, 160);
    expect(has_id(&play, 160, nine), "a hold shoots 70 ms after the last pass");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 80);
    engine_play_tick(&play, 90);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->cycles == 2 && has_id(&play, 90, nine) == 0,
           "a hold finishes the second cycle without shooting");
    engine_play_tick(&play, 91);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->cycles == 2 && has_id(&play, 91, o),
           "a hold starts the next cycle without waiting another 70 ms");

    shot.speed = 2.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->wait_until == 5 && has_id(&play, 0, o),
           "the chord wait is divided by speed");
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 5);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->wait_until == 40 && has_id(&play, 5, nine) == 0,
           "gapMs is divided by speed");
    engine_play_tick(&play, 40);
    expect(has_id(&play, 40, nine), "the sped gap shoots at gapMs over speed");

    shot.speed = 1.0;
    steps[0].a = 0;
    steps[0].count = 3;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->wait_until == 11 && has_id(&play, 10, nine) == 0,
           "a gap of 0 does not wait");
    engine_play_tick(&play, 11);
    expect(has_id(&play, 11, o), "a gap of 0 starts the next chord on the next tick");

    steps[0].a = -4;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->wait_until == 11,
           "a negative gap does not wait");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_ONCE_HELD;
    steps[0].first = 3;
    steps[0].count = 0;
    steps[0].a = 0;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = y;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 1000;
    engine_macro_init(&shot);
    set_id(&shot, "empty");
    shot.steps = steps;
    shot.step_count = 3;
    shot.top_count = 3;
    shot.mode = ENGINE_MODE_WHILE_HELD;
    shot.trigger = n;
    shot.speed = 2.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "empty");
    expect(runner != NULL && runner->wait_until == 1 && runner->cycles == 1 &&
               has_id(&play, 0, y) == 0,
           "an empty onceHeld waits 1 ms");
    engine_play_tick(&play, 1);
    runner = live_id(&play, "empty");
    expect(runner != NULL && runner->cycles == 2 && runner->wait_until == 2 &&
               has_id(&play, 1, y) == 0,
           "an empty onceHeld keeps yielding while the trigger is down");
    press(&fingers, &play, n, 0);
    engine_play_tick(&play, 2);
    expect(has_id(&play, 2, y), "an empty onceHeld pops when the trigger comes up");

    steps[0].a = 70;
    steps[0].count = 3;
    steps[0].first = 2;
    steps[1].type = ENGINE_STEP_BURST;
    steps[1].first = 5;
    steps[1].count = 2;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_DOWN;
    steps[2].key = o;
    steps[3].type = ENGINE_STEP_WAIT;
    steps[3].a = 10;
    steps[4].type = ENGINE_STEP_KEY;
    steps[4].action = ENGINE_ACT_UP;
    steps[4].key = o;
    steps[5].type = ENGINE_STEP_KEY;
    steps[5].action = ENGINE_ACT_DOWN;
    steps[5].key = nine;
    steps[6].type = ENGINE_STEP_WAIT;
    steps[6].a = 10;
    engine_macro_init(&shot);
    set_id(&shot, "edit-shot");
    shot.steps = steps;
    shot.step_count = 7;
    shot.top_count = 2;
    shot.trigger = 0;
    shot.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->wait_until == 80 && has_id(&play, 10, nine) == 0,
           "an empty trigger counts as up");
    engine_play_tick(&play, 80);
    expect(has_id(&play, 80, nine), "an empty trigger shoots after the gap");

    shot.trigger = n;
    fresh();
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->wait_until == 80,
           "a missing snapshot counts as up");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_fingers(&play, &fingers);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "edit-shot");
    expect(runner != NULL && runner->wait_until == 80,
           "a key that was never reported counts as up");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_ONCE_HELD;
    steps[0].first = 3;
    steps[0].count = 1;
    steps[0].a = 70;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = y;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 1000;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = x;
    engine_macro_init(&shot);
    set_id(&shot, "stop");
    shot.steps = steps;
    shot.step_count = 4;
    shot.top_count = 3;
    shot.trigger = n;
    shot.release_stop = ENGINE_STOP_FINISH;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 1);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_flush(&play);
    engine_play_ask_stop(&play, "stop");
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, y) && has_id(&play, 0, x) == 0,
           "stopping skips onceHeld before the frame is pushed");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_IF_SHORT;
    steps[0].first = 3;
    steps[0].count = 1;
    steps[0].a = 150;
    steps[0].b = 1;
    steps[1].type = ENGINE_STEP_ONCE_HELD;
    steps[1].first = 4;
    steps[1].count = 1;
    steps[1].a = 70;
    steps[2].type = ENGINE_STEP_BURST;
    steps[2].first = 5;
    steps[2].count = 2;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = o;
    steps[4].type = ENGINE_STEP_KEY;
    steps[4].action = ENGINE_ACT_DOWN;
    steps[4].key = x;
    steps[5].type = ENGINE_STEP_KEY;
    steps[5].action = ENGINE_ACT_DOWN;
    steps[5].key = nine;
    steps[6].type = ENGINE_STEP_WAIT;
    steps[6].a = 10;
    engine_macro_init(&shot);
    set_id(&shot, "short");
    shot.steps = steps;
    shot.step_count = 7;
    shot.top_count = 3;
    shot.trigger = n;
    shot.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 0);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "short");
    expect(runner != NULL && runner->took_short && has_id(&play, 0, o) &&
               has_id(&play, 0, x) == 0 && has_id(&play, 0, nine),
           "tookShort skips onceHeld and still runs burst");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_ONCE_HELD;
    steps[0].first = 2;
    steps[0].count = 3;
    steps[0].a = 0;
    steps[1].type = ENGINE_STEP_IF_SHORT;
    steps[1].first = 5;
    steps[1].count = 2;
    steps[1].a = 150;
    steps[1].b = 3;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_DOWN;
    steps[2].key = o;
    steps[3].type = ENGINE_STEP_WAIT;
    steps[3].a = 10;
    steps[4].type = ENGINE_STEP_KEY;
    steps[4].action = ENGINE_ACT_UP;
    steps[4].key = o;
    steps[5].type = ENGINE_STEP_KEY;
    steps[5].action = ENGINE_ACT_DOWN;
    steps[5].key = x;
    steps[6].type = ENGINE_STEP_WAIT;
    steps[6].a = 10;
    engine_macro_init(&shot);
    set_id(&shot, "cycles");
    shot.steps = steps;
    shot.step_count = 7;
    shot.top_count = 2;
    shot.trigger = n;
    shot.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 0);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "cycles");
    expect(runner != NULL && runner->cycles == 1 && runner->took_short &&
               has_id(&play, 10, x) && runner->stack_count == 2 &&
               runner->stack[1].repeats_left == 2,
           "onceHeld cycles top up a later ifShort");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_ONCE_HELD;
    steps[0].first = 1;
    steps[0].count = 3;
    steps[0].a = 70;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = x;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 10;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_UP;
    steps[3].key = x;
    engine_macro_init(&shot);
    set_id(&shot, "bare");
    shot.steps = steps;
    shot.step_count = 4;
    shot.top_count = 1;
    shot.trigger = n;
    shot.speed = 1.0;
    fresh();
    engine_play_start(&play, &shot, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(has_id(&play, 0, x) && play.runners[0].done == 0, "onceHeld runs on the bare runner");
    engine_runner_advance(&play.runners[0], 10, NULL, NULL);
    expect(play.runners[0].done == 0 && play.runners[0].cycles == 1 &&
               play.runners[0].wait_until == 80 && has_id(&play, 10, x) == 0,
           "onceHeld on the bare runner counts a missing finger as up");

    play.freq = 10000000;
    fresh();
    play.freq = 10000000;
    engine_fingers_clear(&fingers);
    press(&fingers, &play, n, 0);
    engine_play_start(&play, &shot, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "bare");
    expect(runner != NULL && runner->wait_until == 100000 && has_id(&play, 0, x),
           "onceHeld keeps the chord wait at a fast clock");
    engine_play_tick(&play, 100000);
    runner = live_id(&play, "bare");
    expect(runner != NULL && runner->wait_until == 800000 && has_id(&play, 100000, x) == 0,
           "gapMs uses the arm conversion at a fast clock");
}

static void fill_edge(struct engine_step *step, uint16_t key, int32_t arm, int32_t gap,
                      int32_t hold, int32_t rest, int spam, int ignore, int pause_block,
                      uint32_t first, uint32_t count)
{
    memset(step, 0, sizeof *step);
    step->type = ENGINE_STEP_EDGE_HOLD;
    step->key = key;
    step->a = arm;
    step->b = gap;
    step->d = hold;
    step->e = rest;
    step->action = engine_edge_action(spam, ignore, pause_block);
    step->first = first;
    step->count = count;
}

static void check_edge_hold(void)
{
    struct engine_play *state = fresh();
    struct engine_fingers fingers;
    struct engine_step steps[8];
    struct engine_step watch[2];
    struct engine_macro tap;
    struct engine_macro spam;
    struct engine_macro other;
    struct engine_macro catalog[3];
    struct engine_runner *runner;
    uint16_t side = kid("XButton1");
    uint16_t side2 = kid("XButton2");
    uint16_t z = kid("z");
    uint16_t t = kid("t");
    uint16_t a = kid("a");
    uint16_t click = kid("LButton");
    uint16_t right = kid("RButton");
    uint16_t f = kid("f");

    (void)state;
    expect(engine_step_kind("tapHold") == ENGINE_STEP_EDGE_HOLD, "tap hold kind");
    expect(engine_ignore_mode("macro") == ENGINE_IGNORE_MACRO, "macro ignore");
    expect(engine_ignore_mode("both") == ENGINE_IGNORE_BOTH, "both ignore");
    expect(engine_ignore_mode("once") == ENGINE_IGNORE_OFF && engine_ignore_mode("nope") == ENGINE_IGNORE_OFF,
           "once is not an ignore lock");
    expect(engine_pause_mode("once") == 0, "once is not a lock");
    expect(engine_pause_mode(" block ") == 1 && engine_pause_mode("Block") == 1, "block stays a mute");
    expect(engine_pause_mode("true") == 0 && engine_pause_mode(NULL) == 0, "other pause names stay off");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_OFF, 0, 1, 1);
    steps[1].key = t;
    engine_macro_init(&tap);
    set_id(&tap, "tap-z");
    tap.steps = steps;
    tap.step_count = 2;
    tap.top_count = 1;
    tap.trigger = side;
    tap.mode = ENGINE_MODE_WHILE_HELD;
    tap.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "tap-z");
    expect(runner != NULL && has_id(&play, 0, z) && has_id(&play, 0, side) && report_has(&play, z),
           "tap hold presses z and the trigger");
    engine_play_tick(&play, 5);
    expect(live_id(&play, "tap-z") != NULL && has_id(&play, 5, z) && has_id(&play, 5, side) &&
               report_has(&play, z) == 0,
           "tap hold stays down while the watched key is up");
    engine_play_tick(&play, 400);
    expect(report_has(&play, z) && has_id(&play, 400, z), "tap hold repeats z like a windows hold");
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 410);
    expect(has_id(&play, 410, z) && has_id(&play, 410, side), "tap hold waits before the release");
    engine_play_tick(&play, 415);
    expect(has_id(&play, 415, z) == 0 && has_id(&play, 415, side) == 0,
           "tap hold releases z and the trigger");
    engine_play_tick(&play, 495);
    expect(has_id(&play, 495, z) && has_id(&play, 495, side) == 0,
           "tap hold presses z again while the trigger is still held");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 5);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 15);
    press(&fingers, &play, side, 0);
    engine_play_tick(&play, 95);
    expect(live_id(&play, "tap-z") == NULL && has_id(&play, 95, z) == 0,
           "letting go during the gap does not press z again");
    engine_play_stop_all(&play);
    expect(has_id(&play, 95, z) == 0 && has_id(&play, 95, side) == 0, "physical release clears z");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 5);
    expect(has_id(&play, 5, z) && has_id(&play, 5, side), "sprint stays down before release");
    press(&fingers, &play, side, 0);
    engine_play_tick(&play, 6);
    expect(live_id(&play, "tap-z") == NULL && has_id(&play, 6, z) == 0 && has_id(&play, 6, side) == 0,
           "releasing sprint while holding stops the repress");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], click, 5, 50, 18, 18, 1, ENGINE_IGNORE_OFF, 0, 1, 1);
    steps[1].key = right;
    engine_macro_init(&spam);
    set_id(&spam, "spam-l");
    spam.steps = steps;
    spam.step_count = 2;
    spam.top_count = 1;
    spam.trigger = side2;
    spam.mode = ENGINE_MODE_WHILE_HELD;
    spam.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side2, 1);
    press(&fingers, &play, right, 0);
    engine_play_start(&play, &spam, -1, 1);
    engine_play_tick(&play, 0);
    expect(has_id(&play, 0, click) && has_id(&play, 0, side2),
           "spam starts with the click and the trigger down");
    engine_play_tick(&play, 15);
    expect(has_id(&play, 15, click) && has_id(&play, 15, side2) && report_has(&play, click) == 0,
           "spam stays down through the click");
    engine_play_tick(&play, 20);
    expect(has_id(&play, 20, click) == 0 && has_id(&play, 20, side2),
           "spam lets the click up and keeps the trigger");
    engine_play_tick(&play, 40);
    expect(has_id(&play, 40, click) && has_id(&play, 40, side2),
           "spam clicks again while the trigger is held");
    press(&fingers, &play, right, 1);
    engine_play_tick(&play, 45);
    expect(has_id(&play, 45, click) == 0 && has_id(&play, 45, side2),
           "a tracked press stops the clicks and keeps the trigger");
    engine_play_tick(&play, 70);
    expect(has_id(&play, 70, click) == 0 && has_id(&play, 70, side2), "spam stays quiet for the pause");
    engine_play_tick(&play, 95);
    expect(has_id(&play, 95, click) && has_id(&play, 95, side2),
           "spam clicks again after the pause while still held");
    press(&fingers, &play, side2, 0);
    engine_play_tick(&play, 100);
    expect(live_id(&play, "spam-l") == NULL && has_id(&play, 100, click) == 0 &&
               has_id(&play, 100, side2) == 0,
           "letting go stops the spam");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], click, 0, 50, 18, 18, 1, ENGINE_IGNORE_OFF, 0, 4, 1);
    steps[1].type = ENGINE_STEP_BURST;
    steps[1].first = 2;
    steps[1].count = 2;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_DOWN;
    steps[2].key = a;
    steps[3].type = ENGINE_STEP_WAIT;
    steps[3].a = 40;
    steps[4].key = right;
    engine_macro_init(&spam);
    set_id(&spam, "spam-after");
    spam.steps = steps;
    spam.step_count = 5;
    spam.top_count = 2;
    spam.trigger = side2;
    spam.mode = ENGINE_MODE_WHILE_HELD;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side2, 1);
    press(&fingers, &play, right, 0);
    engine_play_start(&play, &spam, -1, 1);
    engine_play_tick(&play, 0);
    press(&fingers, &play, right, 1);
    engine_play_tick(&play, 10);
    expect(has_id(&play, 10, click) == 0 && has_id(&play, 10, side2),
           "tracked press pauses spam before after release");
    press(&fingers, &play, side2, 0);
    engine_play_tick(&play, 20);
    runner = live_id(&play, "spam-after");
    expect(runner != NULL && has_id(&play, 20, a) && has_id(&play, 20, click) == 0,
           "letting go during the pause runs after release");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_MACRO, 0, 1, 1);
    steps[0].f = 150;
    steps[1].key = t;
    memset(watch, 0, sizeof watch);
    watch[0].type = ENGINE_STEP_KEY;
    watch[0].action = ENGINE_ACT_DOWN;
    watch[0].key = t;
    engine_macro_init(&tap);
    set_id(&tap, "lock-z");
    tap.steps = steps;
    tap.step_count = 2;
    tap.top_count = 1;
    tap.trigger = side;
    tap.mode = ENGINE_MODE_WHILE_HELD;
    engine_macro_init(&spam);
    set_id(&spam, "spam-t");
    spam.steps = watch;
    spam.step_count = 1;
    spam.top_count = 1;
    spam.trigger = f;
    spam.mode = ENGINE_MODE_ONCE;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 15);
    expect(has_id(&play, 15, z) == 0, "lock takes the first tracked press");
    engine_play_tick(&play, 95);
    expect(has_id(&play, 95, z), "lock returns to hold after the first repress");
    press(&fingers, &play, t, 0);
    engine_play_tick(&play, 100);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 110);
    engine_play_tick(&play, 115);
    expect(has_id(&play, 115, z) == 0, "macro lock still takes a physical press inside the window");
    engine_play_tick(&play, 195);
    expect(has_id(&play, 195, z), "macro lock returns to hold after a physical press");
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &spam, -1, 1);
    engine_play_flush(&play);
    engine_play_advance(&play, 200);
    engine_play_publish(&play, 200);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 210);
    engine_play_tick(&play, 215);
    expect(has_id(&play, 215, z), "macro lock skips a sent key inside the window");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_BOTH, 0, 1, 1);
    steps[0].f = 150;
    steps[1].key = t;
    engine_macro_init(&tap);
    set_id(&tap, "lock-both");
    tap.steps = steps;
    tap.step_count = 2;
    tap.top_count = 1;
    tap.trigger = side;
    tap.mode = ENGINE_MODE_WHILE_HELD;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 15);
    engine_play_tick(&play, 95);
    press(&fingers, &play, t, 0);
    engine_play_tick(&play, 100);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 110);
    engine_play_tick(&play, 115);
    expect(has_id(&play, 115, z), "both lock skips a physical press inside the window");
    press(&fingers, &play, t, 0);
    engine_play_tick(&play, 200);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 250);
    engine_play_tick(&play, 260);
    expect(has_id(&play, 260, z) == 0, "a rejected press does not refresh the lock");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 15);
    engine_play_tick(&play, 95);
    press(&fingers, &play, t, 0);
    engine_play_tick(&play, 100);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 270);
    engine_play_tick(&play, 275);
    expect(has_id(&play, 275, z) == 0, "both lock takes a press after the window");

    memset(watch, 0, sizeof watch);
    watch[0].type = ENGINE_STEP_WAIT;
    watch[0].a = 50;
    engine_macro_init(&spam);
    set_id(&spam, "spam-t");
    spam.steps = watch;
    spam.step_count = 1;
    spam.trigger = t;
    spam.mode = ENGINE_MODE_TOGGLE;
    engine_macro_init(&other);
    set_id(&other, "other-f");
    other.steps = watch;
    other.step_count = 1;
    other.trigger = f;
    other.mode = ENGINE_MODE_TOGGLE;
    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_OFF, 0, 1, 2);
    steps[1].key = t;
    steps[2].key = side2;
    engine_macro_init(&tap);
    set_id(&tap, "pause-z");
    tap.steps = steps;
    tap.step_count = 3;
    tap.top_count = 1;
    tap.trigger = side;
    tap.mode = ENGINE_MODE_WHILE_HELD;
    tap.advanced = 1;
    catalog[0] = tap;
    catalog[1] = spam;
    catalog[2] = other;
    fresh();
    engine_play_set_macros(&play, catalog, 3);
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    engine_play_start(&play, &spam, -1, 1);
    engine_play_start(&play, &other, -1, 1);
    engine_play_flush(&play);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "spam-t");
    expect(runner != NULL && runner->paused == 0 && engine_play_muted(&play, "spam-t") == 0 &&
               engine_play_muted(&play, "other-f") == 0,
           "once leaves tracked macros running with no lock");
    expect(engine_once_state(&play, "spam-t") == ENGINE_LOCK_NONE &&
               engine_once_passing(&play, spam.trigger) == 0,
           "once does not pass the tracked key");

    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_OFF, 1, 1, 2);
    steps[1].key = t;
    steps[2].key = side2;
    engine_macro_init(&tap);
    set_id(&tap, "block-z");
    tap.steps = steps;
    tap.step_count = 3;
    tap.top_count = 1;
    tap.trigger = side;
    tap.mode = ENGINE_MODE_WHILE_HELD;
    tap.advanced = 1;
    catalog[0] = tap;
    fresh();
    engine_play_set_macros(&play, catalog, 3);
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &spam, -1, 1);
    engine_play_start(&play, &other, -1, 1);
    engine_play_flush(&play);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "spam-t");
    expect(runner != NULL && runner->paused && engine_play_muted(&play, "spam-t"),
           "block mutes a tracked macro");
    expect(engine_once_passing(&play, spam.trigger) == 0, "block keeps the tracked key swallowed");
    runner = live_id(&play, "other-f");
    expect(runner != NULL && runner->paused == 0 && engine_play_muted(&play, "other-f") == 0,
           "block leaves other macros alone");
    engine_play_stop(&play, "block-z");
    runner = live_id(&play, "spam-t");
    expect(runner != NULL && runner->paused == 0 && engine_play_muted(&play, "spam-t") == 0,
           "release unmutes the tracked macro");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_OFF, 0, 1, 1);
    steps[1].key = t;
    engine_macro_init(&tap);
    set_id(&tap, "fast");
    tap.steps = steps;
    tap.step_count = 2;
    tap.top_count = 1;
    tap.trigger = side;
    tap.mode = ENGINE_MODE_WHILE_HELD;
    tap.speed = 2.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "fast");
    expect(runner != NULL && runner->wait_until == 5, "the 5 ms poll is not divided by speed");
    engine_play_tick(&play, 5);
    press(&fingers, &play, t, 1);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "fast");
    expect(runner != NULL && runner->wait_until == 13 && has_id(&play, 10, z),
           "armMs is divided by speed");
    engine_play_tick(&play, 13);
    runner = live_id(&play, "fast");
    expect(runner != NULL && runner->wait_until == 53 && has_id(&play, 13, z) == 0,
           "gapMs is divided by speed at the wait");
    engine_play_tick(&play, 53);
    expect(has_id(&play, 53, z), "the sped gap presses z again");

    tap.speed = 1.0;
    steps[0].b = 0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 5);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "fast");
    expect(runner != NULL && runner->wait_until == 11 && has_id(&play, 10, z) == 0,
           "a gap of 0 still waits 1 ms");
    engine_play_tick(&play, 11);
    expect(has_id(&play, 11, z), "a gap of 0 presses z after that wait");

    steps[0].b = -5;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 5);
    engine_play_tick(&play, 10);
    runner = live_id(&play, "fast");
    expect(runner != NULL && runner->wait_until == 11, "a negative gap still waits 1 ms");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_OFF, 0, 0, 0);
    engine_macro_init(&tap);
    set_id(&tap, "empty");
    tap.steps = steps;
    tap.step_count = 1;
    tap.top_count = 1;
    tap.trigger = 0;
    tap.mode = ENGINE_MODE_WHILE_HELD;
    fresh();
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 20);
    expect(live_id(&play, "empty") != NULL && has_id(&play, 20, z),
           "an empty trigger does not stop the hold");

    tap.trigger = side;
    set_id(&tap, "missing");
    fresh();
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 1);
    expect(live_id(&play, "missing") == NULL && has_id(&play, 1, z) == 0,
           "a missing snapshot stops the hold");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_fingers(&play, &fingers);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 1);
    expect(live_id(&play, "missing") == NULL, "a key that was never reported stops the hold");

    set_id(&tap, "stop");
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_ask_stop(&play, "stop");
    engine_play_tick(&play, 1);
    expect(live_id(&play, "stop") == NULL && has_id(&play, 1, z) == 0, "stopping ends a tap hold");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], click, 5, 50, 18, 18, 1, ENGINE_IGNORE_OFF, 0, 0, 0);
    engine_macro_init(&spam);
    set_id(&spam, "stop-spam");
    spam.steps = steps;
    spam.step_count = 1;
    spam.top_count = 1;
    spam.trigger = side2;
    spam.mode = ENGINE_MODE_WHILE_HELD;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side2, 1);
    engine_play_start(&play, &spam, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_ask_stop(&play, "stop-spam");
    engine_play_tick(&play, 1);
    expect(live_id(&play, "stop-spam") == NULL && has_id(&play, 1, click) == 0,
           "stopping ends spam");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_IF_SHORT;
    steps[0].a = 150;
    steps[0].b = 0;
    fill_edge(&steps[1], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_OFF, 0, 0, 0);
    steps[2].type = ENGINE_STEP_BURST;
    steps[2].first = 3;
    steps[2].count = 2;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = a;
    steps[4].type = ENGINE_STEP_WAIT;
    steps[4].a = 40;
    engine_macro_init(&tap);
    set_id(&tap, "short");
    tap.steps = steps;
    tap.step_count = 5;
    tap.top_count = 3;
    tap.trigger = side;
    tap.mode = ENGINE_MODE_ONCE;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "short");
    expect(runner != NULL && runner->took_short && has_id(&play, 0, z) == 0 && has_id(&play, 0, a),
           "took_short skips edgeHold and still runs burst");

    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_OFF, 0, 0, 0);
    engine_macro_init(&tap);
    set_id(&tap, "bare");
    tap.steps = steps;
    tap.step_count = 1;
    tap.top_count = 1;
    tap.trigger = side;
    fresh();
    engine_play_start(&play, &tap, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(has_id(&play, 0, z) && play.runners[0].done == 0, "edgeHold holds on the bare runner");
    engine_runner_advance(&play.runners[0], 5, NULL, NULL);
    expect(play.runners[0].done && has_id(&play, 5, z) == 0,
           "edgeHold on the bare runner counts a missing finger as up");

    play.freq = 10000000;
    fresh();
    play.freq = 10000000;
    memset(steps, 0, sizeof steps);
    fill_edge(&steps[0], z, 5, 80, 18, 18, 0, ENGINE_IGNORE_OFF, 0, 1, 1);
    steps[1].key = t;
    engine_macro_init(&tap);
    set_id(&tap, "clock");
    tap.steps = steps;
    tap.step_count = 2;
    tap.top_count = 1;
    tap.trigger = side;
    tap.mode = ENGINE_MODE_WHILE_HELD;
    engine_fingers_clear(&fingers);
    press(&fingers, &play, side, 1);
    press(&fingers, &play, t, 0);
    engine_play_start(&play, &tap, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "clock");
    expect(runner != NULL && runner->wait_until == 50000, "the edge poll uses the fast clock");
    engine_play_tick(&play, 4000000);
    expect(report_has(&play, z), "the key repeat uses the fast clock");
}

static void check_scan_wait(void)
{
    struct engine_fingers fingers;
    struct engine_step steps[6];
    struct engine_macro scan;
    uint16_t b = kid("b");
    uint16_t o = kid("o");
    uint16_t x = kid("x");
    struct engine_runner *runner;

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_KEY;
    steps[0].action = ENGINE_ACT_DOWN;
    steps[0].key = o;
    steps[1].type = ENGINE_STEP_WAIT;
    steps[1].a = 10;
    steps[2].type = ENGINE_STEP_KEY;
    steps[2].action = ENGINE_ACT_UP;
    steps[2].key = o;
    steps[3].type = ENGINE_STEP_SCAN_WAIT;
    steps[3].a = 80;
    steps[4].type = ENGINE_STEP_KEY;
    steps[4].action = ENGINE_ACT_DOWN;
    steps[4].key = x;
    steps[5].type = ENGINE_STEP_WAIT;
    steps[5].a = 10;
    engine_macro_init(&scan);
    set_id(&scan, "scan-hold");
    scan.steps = steps;
    scan.step_count = 6;
    scan.top_count = 6;
    scan.trigger = b;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "scan-hold");
    expect(runner != NULL && runner->scan_wait == 0 && has_id(&play, 0, o) &&
               has_id(&play, 0, x) == 0,
           "scan wait runs the keys first");
    engine_play_tick(&play, 10);
    runner = live_id(&play, "scan-hold");
    expect(runner != NULL && runner->scan_wait && runner->wait_until == 90 &&
               has_id(&play, 10, o) == 0 && has_id(&play, 10, x) == 0,
           "scan wait starts after those keys");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 40);
    expect(live_id(&play, "scan-hold") == NULL && has_id(&play, 40, x) == 0,
           "scan wait exits if the trigger lets go");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 10);
    engine_play_tick(&play, 90);
    runner = live_id(&play, "scan-hold");
    expect(runner != NULL && runner->scan_wait == 0 && has_id(&play, 90, x),
           "scan wait continues after the hold");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_SCAN_WAIT;
    steps[0].a = 80;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = x;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 10;
    engine_macro_init(&scan);
    set_id(&scan, "scan-only");
    scan.steps = steps;
    scan.step_count = 3;
    scan.top_count = 3;
    scan.trigger = b;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "scan-only");
    expect(runner != NULL && runner->scan_wait && has_id(&play, 0, x) == 0,
           "scan wait can run with no keys around it");
    engine_play_tick(&play, 80);
    expect(has_id(&play, 80, x), "scan wait continues after the sleep");

    scan.speed = 2.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "scan-only");
    expect(runner != NULL && runner->wait_until == 40, "scan wait is divided by speed");

    steps[0].a = 0;
    scan.speed = 1.0;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "scan-only");
    expect(runner != NULL && runner->wait_until == 1 && has_id(&play, 0, x) == 0,
           "a scan wait of 0 still waits 1 ms");
    engine_play_tick(&play, 1);
    expect(has_id(&play, 1, x), "a scan wait of 0 presses the next key after that wait");

    steps[0].a = -5;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "scan-only");
    expect(runner != NULL && runner->wait_until == 1, "a negative scan wait still waits 1 ms");

    steps[0].a = 80;
    scan.trigger = 0;
    set_id(&scan, "empty");
    fresh();
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "empty");
    expect(runner != NULL && runner->scan_wait && runner->wait_until == 80,
           "an empty trigger does not stop the scan");
    engine_play_tick(&play, 80);
    expect(has_id(&play, 80, x), "an empty trigger runs the next key after the scan");

    scan.trigger = b;
    set_id(&scan, "missing");
    fresh();
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    expect(live_id(&play, "missing") == NULL && has_id(&play, 0, x) == 0,
           "a missing snapshot stops the scan");

    fresh();
    engine_fingers_clear(&fingers);
    engine_play_set_fingers(&play, &fingers);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    expect(live_id(&play, "missing") == NULL && has_id(&play, 0, x) == 0,
           "a key that was never reported stops the scan");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    expect(live_id(&play, "missing") == NULL && has_id(&play, 0, x) == 0,
           "a trigger that is already up stops the scan");

    set_id(&scan, "stop");
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_ask_stop(&play, "stop");
    engine_play_tick(&play, 1);
    expect(live_id(&play, "stop") == NULL && has_id(&play, 1, x) == 0,
           "stopping ends a scan wait");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_IF_SHORT;
    steps[0].a = 150;
    steps[0].b = 0;
    steps[1].type = ENGINE_STEP_SCAN_WAIT;
    steps[1].a = 80;
    steps[2].type = ENGINE_STEP_BURST;
    steps[2].first = 3;
    steps[2].count = 2;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_DOWN;
    steps[3].key = x;
    steps[4].type = ENGINE_STEP_WAIT;
    steps[4].a = 40;
    engine_macro_init(&scan);
    set_id(&scan, "short");
    scan.steps = steps;
    scan.step_count = 5;
    scan.top_count = 3;
    scan.trigger = b;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 0);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "short");
    expect(runner != NULL && runner->took_short && has_id(&play, 0, x),
           "took_short skips scanWait and still runs burst");

    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_SCAN_WAIT;
    steps[0].a = 80;
    engine_macro_init(&scan);
    set_id(&scan, "bare");
    scan.steps = steps;
    scan.step_count = 1;
    scan.top_count = 1;
    scan.trigger = b;
    fresh();
    engine_play_start(&play, &scan, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(play.runners[0].done, "scanWait on the bare runner counts a missing finger as up");

    scan.trigger = 0;
    set_id(&scan, "bare-empty");
    fresh();
    engine_play_start(&play, &scan, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(play.runners[0].done == 0 && play.runners[0].scan_wait &&
               play.runners[0].wait_until == 80,
           "an empty trigger waits on the bare runner");

    play.freq = 10000000;
    fresh();
    play.freq = 10000000;
    scan.trigger = b;
    set_id(&scan, "clock");
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &scan, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "clock");
    expect(runner != NULL && runner->wait_until == 800000, "scan wait uses the fast clock");
}

static void check_press_hold(void)
{
    struct engine_step built[8];
    struct engine_step child[2];
    struct engine_step dirty[4];
    struct engine_fingers fingers;
    struct engine_macro macro;
    struct engine_runner *runner;
    uint16_t q = kid("q");
    uint16_t b = kid("b");
    uint16_t x = kid("x");

    expect(engine_expand_press_hold("q", 80, 1, NULL, 0, built, 4) == 4,
           "a key with no children expands to four steps");
    expect(built[0].type == ENGINE_STEP_KEY && built[0].action == ENGINE_ACT_DOWN &&
               built[0].key == q,
           "pressHold starts with key down");
    expect(built[1].type == ENGINE_STEP_WAIT && built[1].a == 18, "pressHold waits 18");
    expect(built[2].type == ENGINE_STEP_KEY && built[2].action == ENGINE_ACT_UP &&
               built[2].key == q,
           "pressHold releases that key");
    expect(built[3].type == ENGINE_STEP_SCAN_WAIT && built[3].a == 200,
           "a missing holdMs is 200, not the editor 80");

    expect(engine_expand_press_hold("Q", 0, 0, NULL, 0, built, 4) == 4 &&
               built[0].key == q && built[3].a == 0,
           "holdMs 0 is stored as 0");
    expect(engine_expand_press_hold("q", -4, 0, NULL, 0, built, 4) == 4 && built[3].a == -4,
           "a negative holdMs is stored");
    expect(engine_expand_press_hold("q", 80, 0, NULL, 0, built, 4) == 4 && built[3].a == 80,
           "an explicit holdMs is kept");

    expect(engine_expand_press_hold("", 0, 1, NULL, 0, built, 4) == 1 &&
               built[0].type == ENGINE_STEP_SCAN_WAIT && built[0].a == 200,
           "an empty key only scans");
    expect(engine_expand_press_hold(NULL, 0, 1, NULL, 0, built, 4) == 1 &&
               built[0].type == ENGINE_STEP_SCAN_WAIT,
           "a missing key only scans");
    expect(engine_expand_press_hold(" ", 0, 1, NULL, 0, built, 4) == 1 &&
               built[0].type == ENGINE_STEP_SCAN_WAIT,
           "a blank key only scans");
    expect(engine_expand_press_hold("nope", 0, 1, NULL, 0, built, 4) == 4 &&
               built[0].type == ENGINE_STEP_KEY && built[0].key == 0 && built[1].a == 18 &&
               built[3].type == ENGINE_STEP_SCAN_WAIT,
           "an unknown key still expands");
    expect(engine_expand_press_hold("LButton", 0, 1, NULL, 0, built, 4) == 4 &&
               built[0].type == ENGINE_STEP_KEY && built[0].key == ENGINE_KEY_LBUTTON &&
               built[2].type == ENGINE_STEP_KEY,
           "pressHold writes a key step for a mouse button");

    memset(child, 0, sizeof child);
    child[0].type = ENGINE_STEP_KEY;
    child[0].action = ENGINE_ACT_DOWN;
    child[0].key = x;
    child[0].first = 4;
    child[0].count = 2;
    child[0].mute_at = 3;
    child[1].type = ENGINE_STEP_WAIT;
    child[1].a = 7;
    expect(engine_expand_press_hold("q", 50, 0, child, 2, built, 8) == 3, "children are copied through");
    expect(built[0].type == ENGINE_STEP_KEY && built[0].key == x && built[0].first == 4 &&
               built[0].count == 2 && built[0].mute_at == 3 && built[1].type == ENGINE_STEP_WAIT &&
               built[1].a == 7 && built[2].type == ENGINE_STEP_SCAN_WAIT && built[2].a == 50,
           "a key with children does not add the 18 ms hold");

    memset(dirty, 0xAB, sizeof dirty);
    expect(engine_expand_press_hold("q", 0, 1, NULL, 0, dirty, 3) == -1, "a short buffer is refused");
    expect(((unsigned char *)dirty)[0] == 0xAB, "a short buffer writes nothing");
    expect(engine_expand_press_hold("q", 0, 1, NULL, 0, NULL, 4) == -1, "a missing buffer is refused");
    expect(engine_expand_press_hold("q", 0, 1, NULL, 1, built, 4) == -1, "children need a list");
    expect(engine_expand_press_hold("q", 0, 0, child, 0xFFFFFFFFu, built, 4) == -1,
           "a huge child list does not expand");

    expect(engine_expand_press_hold("q", 80, 1, NULL, 0, built, 4) == 4, "pressHold expands again");
    engine_macro_init(&macro);
    set_id(&macro, "press");
    macro.steps = built;
    macro.step_count = 4;
    macro.top_count = 4;
    macro.trigger = b;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &macro, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "press");
    expect(runner != NULL && runner->scan_wait == 0 && runner->wait_until == 18 &&
               has_id(&play, 0, q) && has_id(&play, 0, x) == 0,
           "pressHold holds the key before the scan");
    engine_play_tick(&play, 18);
    runner = live_id(&play, "press");
    expect(runner != NULL && runner->scan_wait && runner->wait_until == 218 &&
               has_id(&play, 18, q) == 0,
           "pressHold scans after the 18 ms hold");
    press(&fingers, &play, b, 0);
    engine_play_tick(&play, 19);
    expect(live_id(&play, "press") == NULL && has_id(&play, 19, q) == 0,
           "pressHold ends when the trigger comes up");

    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &macro, -1, 1);
    engine_play_tick(&play, 0);
    engine_play_tick(&play, 18);
    engine_play_tick(&play, 217);
    expect(live_id(&play, "press") != NULL, "pressHold is still scanning before holdMs");
    engine_play_tick(&play, 218);
    expect(live_id(&play, "press") == NULL && has_id(&play, 218, q) == 0,
           "pressHold finishes the scan while the trigger stays down");

    expect(engine_expand_press_hold("q", 40, 0, child, 1, built, 4) == 2, "one child expands");
    engine_macro_init(&macro);
    set_id(&macro, "child");
    macro.steps = built;
    macro.step_count = 2;
    macro.top_count = 2;
    macro.trigger = b;
    fresh();
    engine_fingers_clear(&fingers);
    press(&fingers, &play, b, 1);
    engine_play_start(&play, &macro, -1, 1);
    engine_play_tick(&play, 0);
    runner = live_id(&play, "child");
    expect(runner != NULL && runner->scan_wait && runner->wait_until == 40 &&
               has_id(&play, 0, x) && has_id(&play, 0, q) == 0,
           "pressHold runs the children and then scans");
    expect(engine_step_kind("pressHold") == 0, "pressHold stays uncompiled");
}

int main(void)
{
    check_names();
    check_basic();
    check_sprint();
    check_repeat_and_hold();
    check_zero_delay();
    check_waits();
    check_random();
    check_move_and_scroll();
    check_scroll_sum();
    check_yield_and_skip();
    check_repeat_step();
    check_stop_mode();
    check_start_rules();
    check_overflow();
    check_goto();
    check_run();
    check_hold_loop();
    check_swap_loop();
    check_burst();
    check_if_short();
    check_once_held();
    check_edge_hold();
    check_scan_wait();
    check_press_hold();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
