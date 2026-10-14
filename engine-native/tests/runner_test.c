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
    expect(kid("nope") == 0 && kid("|") == 0, "unknown name");
    expect(engine_step_kind("key") == ENGINE_STEP_KEY, "kind key");
    expect(engine_step_kind("randomWait") == ENGINE_STEP_RANDOM_WAIT, "kind random");
    expect(engine_step_kind("steps") == ENGINE_STEP_REPEAT, "steps is a repeat");
    expect(engine_step_kind("whileHeld") == ENGINE_STEP_HOLD_LOOP, "while held kind");
    expect(engine_step_kind("tapSpam") == ENGINE_STEP_EDGE_HOLD, "spam kind");
    expect(engine_step_kind("pressHold") == 0 && engine_step_kind("nope") == 0, "unknown kind");
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
    struct engine_step hold;
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
    memset(&hold, 0, sizeof hold);
    hold.type = ENGINE_STEP_HOLD_LOOP;
    hold.key = kid("w");
    macro.steps = &hold;
    macro.step_count = 1;
    set_id(&macro, "later");
    engine_play_start(&play, &macro, -1, 1);
    engine_play_flush(&play);
    engine_runner_advance(&play.runners[0], 0, NULL, NULL);
    expect(play.runners[0].done == 1 && has_id(&play, 0, kid("w")) == 0,
           "hold loop is not executed yet");

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
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
