#include "runner.h"

#include "clock.h"

#include <math.h>
#include <string.h>

#define ENGINE_ADV_CONT 0
#define ENGINE_ADV_WAIT 1
#define ENGINE_ADV_AGAIN 2

static int same_id(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    return strcmp(a, b) == 0;
}

static void copy_id(char *dst, const char *src)
{
    int i;

    if (!src)
        src = "";
    for (i = 0; i < ENGINE_ID_CAP - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int alnum_key(const char *name)
{
    int i;

    if (!name || !name[0])
        return 0;
    for (i = 0; name[i]; i++) {
        char c = name[i];
        int ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!ok)
            return 0;
    }
    return 1;
}

static int punct_key(const char *name)
{
    char c;

    if (!name || !name[0] || name[1])
        return 0;
    c = name[0];
    return c == '-' || c == '=' || c == '[' || c == ']' || c == '\\' ||
           c == ';' || c == '\'' || c == ',' || c == '.' || c == '/' || c == '`';
}

static void lower_copy(const char *src, char *dst, int cap)
{
    int i;

    for (i = 0; i < cap - 1 && src[i]; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        dst[i] = c;
    }
    dst[i] = '\0';
}

static const char *alias_to(const char *key)
{
    static const struct {
        const char *from;
        const char *to;
    } table[] = {
        {"space", "space"},
        {"tab", "tab"},
        {"enter", "enter"},
        {"escape", "escape"},
        {"backspace", "backspace"},
        {"up", "up"},
        {"down", "down"},
        {"left", "left"},
        {"right", "right"},
        {"ins", "Insert"},
        {"del", "Delete"},
        {"shift", "LShift"},
        {"lshift", "LShift"},
        {"rshift", "RShift"},
        {"ctrl", "LCtrl"},
        {"control", "LCtrl"},
        {"lctrl", "LCtrl"},
        {"lcontrol", "LCtrl"},
        {"rctrl", "RCtrl"},
        {"rcontrol", "RCtrl"},
        {"alt", "LAlt"},
        {"lalt", "LAlt"},
        {"ralt", "RAlt"},
        {"lwin", "LWin"},
        {"rwin", "RWin"},
        {"|", "\\"},
    };
    int i;

    for (i = 0; i < (int)(sizeof table / sizeof table[0]); i++) {
        if (strcmp(key, table[i].from) == 0)
            return table[i].to;
    }
    return NULL;
}

static int function_key(const char *name, char *dst, int cap)
{
    int i;

    if (name[0] != 'F' && name[0] != 'f')
        return 0;
    if (name[1] < '0' || name[1] > '9')
        return 0;
    for (i = 1; name[i]; i++) {
        if (name[i] < '0' || name[i] > '9')
            return 0;
        if (i >= cap - 1)
            return 0;
    }
    dst[0] = 'f';
    for (i = 1; name[i]; i++)
        dst[i] = name[i];
    dst[i] = '\0';
    return 1;
}

static int safe_copy(const char *name, char *dst, int cap)
{
    int i;

    if (!name || !name[0])
        return 0;
    if (name[0] == '|' && !name[1])
        name = "\\";
    if (!alnum_key(name) && !punct_key(name))
        return 0;
    for (i = 0; i < cap - 1 && name[i]; i++)
        dst[i] = name[i];
    dst[i] = '\0';
    return dst[0] != '\0';
}

static int map_name(const char *name, uint16_t *id)
{
    int n;
    int i;

    if (!name || !name[0] || !id)
        return 0;
    if (!name[1] && name[0] >= 'a' && name[0] <= 'z') {
        *id = (uint16_t)(0x41 + (name[0] - 'a'));
        return 1;
    }
    if (!name[1] && name[0] >= '0' && name[0] <= '9') {
        *id = (uint16_t)(0x30 + (name[0] - '0'));
        return 1;
    }
    if (name[0] == 'f' && name[1] >= '1' && name[1] <= '9') {
        n = 0;
        for (i = 1; name[i]; i++) {
            if (name[i] < '0' || name[i] > '9')
                return 0;
            n = n * 10 + (name[i] - '0');
            if (n > 24)
                return 0;
        }
        if (n >= 1 && n <= 24) {
            *id = (uint16_t)(0x70 + n - 1);
            return 1;
        }
    }
    if (strcmp(name, "space") == 0) { *id = 0x20; return 1; }
    if (strcmp(name, "tab") == 0) { *id = 0x09; return 1; }
    if (strcmp(name, "enter") == 0) { *id = 0x0D; return 1; }
    if (strcmp(name, "escape") == 0) { *id = 0x1B; return 1; }
    if (strcmp(name, "backspace") == 0) { *id = 0x08; return 1; }
    if (strcmp(name, "up") == 0) { *id = 0x26; return 1; }
    if (strcmp(name, "down") == 0) { *id = 0x28; return 1; }
    if (strcmp(name, "left") == 0) { *id = 0x25; return 1; }
    if (strcmp(name, "right") == 0) { *id = 0x27; return 1; }
    if (strcmp(name, "Insert") == 0) { *id = 0x2D; return 1; }
    if (strcmp(name, "Delete") == 0) { *id = 0x2E; return 1; }
    if (strcmp(name, "LShift") == 0) { *id = 0xA0; return 1; }
    if (strcmp(name, "RShift") == 0) { *id = 0xA1; return 1; }
    if (strcmp(name, "LCtrl") == 0) { *id = 0xA2; return 1; }
    if (strcmp(name, "RCtrl") == 0) { *id = 0xA3; return 1; }
    if (strcmp(name, "LAlt") == 0) { *id = 0xA4; return 1; }
    if (strcmp(name, "RAlt") == 0) { *id = 0xA5; return 1; }
    if (strcmp(name, "LWin") == 0) { *id = 0x5B; return 1; }
    if (strcmp(name, "RWin") == 0) { *id = 0x5C; return 1; }
    if (strcmp(name, "numpadenter") == 0) { *id = ENGINE_KEY_NUMPAD_ENTER; return 1; }
    if (strcmp(name, "LButton") == 0) { *id = ENGINE_KEY_LBUTTON; return 1; }
    if (strcmp(name, "RButton") == 0) { *id = ENGINE_KEY_RBUTTON; return 1; }
    if (strcmp(name, "MButton") == 0) { *id = ENGINE_KEY_MBUTTON; return 1; }
    if (strcmp(name, "XButton1") == 0) { *id = ENGINE_KEY_XBUTTON1; return 1; }
    if (strcmp(name, "XButton2") == 0) { *id = ENGINE_KEY_XBUTTON2; return 1; }
    return 0;
}

int engine_key_id(const char *name, uint16_t *id)
{
    char canon[64];
    char lower[64];
    char fkey[64];
    const char *aliased;
    uint16_t found = 0;

    if (id)
        *id = 0;
    if (!name || !name[0] || strlen(name) >= sizeof canon)
        return 0;

    aliased = alias_to(name);
    if (!aliased) {
        lower_copy(name, lower, (int)sizeof lower);
        aliased = alias_to(lower);
    }
    if (aliased) {
        if (!safe_copy(aliased, canon, (int)sizeof canon))
            return 0;
    } else if (function_key(name, fkey, (int)sizeof fkey)) {
        if (!safe_copy(fkey, canon, (int)sizeof canon))
            return 0;
    } else if (name[0] && !name[1]) {
        char one[4];
        lower_copy(name, one, (int)sizeof one);
        if (!safe_copy(one, canon, (int)sizeof canon))
            return 0;
    } else if (!safe_copy(name, canon, (int)sizeof canon)) {
        return 0;
    }

    if (!map_name(canon, &found))
        return 0;
    if (id)
        *id = found;
    return 1;
}

int engine_step_kind(const char *name)
{
    if (!name)
        return 0;
    if (strcmp(name, "key") == 0) return ENGINE_STEP_KEY;
    if (strcmp(name, "mouse") == 0) return ENGINE_STEP_MOUSE;
    if (strcmp(name, "move") == 0) return ENGINE_STEP_MOVE;
    if (strcmp(name, "scroll") == 0) return ENGINE_STEP_SCROLL;
    if (strcmp(name, "wait") == 0) return ENGINE_STEP_WAIT;
    if (strcmp(name, "randomWait") == 0) return ENGINE_STEP_RANDOM_WAIT;
    if (strcmp(name, "repeat") == 0 || strcmp(name, "steps") == 0)
        return ENGINE_STEP_REPEAT;
    if (strcmp(name, "whileHeld") == 0) return ENGINE_STEP_HOLD_LOOP;
    if (strcmp(name, "swapAfter") == 0) return ENGINE_STEP_SWAP_LOOP;
    if (strcmp(name, "then") == 0) return ENGINE_STEP_BURST;
    if (strcmp(name, "ifShort") == 0) return ENGINE_STEP_IF_SHORT;
    if (strcmp(name, "onceHeld") == 0) return ENGINE_STEP_ONCE_HELD;
    if (strcmp(name, "tapHold") == 0 || strcmp(name, "tapSpam") == 0)
        return ENGINE_STEP_EDGE_HOLD;
    if (strcmp(name, "scanWait") == 0) return ENGINE_STEP_SCAN_WAIT;
    if (strcmp(name, "goto") == 0) return ENGINE_STEP_GOTO;
    if (strcmp(name, "run") == 0) return ENGINE_STEP_RUN;
    return 0;
}

int engine_goto_where(const char *name)
{
    const char *start;
    const char *end;
    char buf[8];
    int n;
    int i;

    if (!name)
        return ENGINE_WHERE_SCREEN;
    start = name;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        start++;
    end = start + strlen(start);
    while (end > start &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;
    n = (int)(end - start);
    if (n <= 0 || n >= (int)sizeof buf)
        return ENGINE_WHERE_SCREEN;
    for (i = 0; i < n; i++) {
        char c = start[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        buf[i] = c;
    }
    buf[n] = '\0';
    if (strcmp(buf, "window") == 0)
        return ENGINE_WHERE_WINDOW;
    if (strcmp(buf, "client") == 0)
        return ENGINE_WHERE_CLIENT;
    return ENGINE_WHERE_SCREEN;
}

int engine_has_hold_loop(const struct engine_step *steps, uint32_t count)
{
    uint32_t i;

    if (!steps)
        return 0;
    for (i = 0; i < count; i++) {
        uint8_t kind = steps[i].type;
        if (kind == ENGINE_STEP_HOLD_LOOP || kind == ENGINE_STEP_SWAP_LOOP ||
            kind == ENGINE_STEP_ONCE_HELD || kind == ENGINE_STEP_EDGE_HOLD)
            return 1;
    }
    return 0;
}

int engine_top_repeats(int mode, int32_t repeat_count, int hold_loop)
{
    int repeats = 1;

    if (mode == ENGINE_MODE_REPEAT)
        repeats = repeat_count < 1 ? 1 : repeat_count;
    else if (mode == ENGINE_MODE_WHILE_HELD || mode == ENGINE_MODE_TOGGLE)
        repeats = 0;
    if (hold_loop)
        return 1;
    return repeats;
}

void engine_macro_init(struct engine_macro *macro)
{
    if (!macro)
        return;
    memset(macro, 0, sizeof *macro);
    macro->speed = 1.0;
    macro->mode = ENGINE_MODE_ONCE;
    macro->repeat_count = 1;
    macro->release_stop = ENGINE_STOP_NEXT_UP;
}

int engine_basic_rewrite(uint16_t trigger, int trigger_is_key, int32_t gap_ms,
                         struct engine_step *out, int cap)
{
    int mouse;

    if (!out || cap < 4)
        return -1;
    if (trigger == 0 || trigger == ENGINE_KEY_LBUTTON || trigger == ENGINE_KEY_RBUTTON)
        return 0;
    mouse = engine_is_mouse_button(trigger);
    if (!mouse && !trigger_is_key)
        return 0;
    if (gap_ms < 0)
        gap_ms = 0;
    memset(out, 0, sizeof(*out) * 4);
    out[0].type = mouse ? (uint8_t)ENGINE_STEP_MOUSE : (uint8_t)ENGINE_STEP_KEY;
    out[0].action = ENGINE_ACT_DOWN;
    out[0].key = trigger;
    out[1].type = ENGINE_STEP_WAIT;
    out[1].a = 18;
    out[2].type = out[0].type;
    out[2].action = ENGINE_ACT_UP;
    out[2].key = trigger;
    out[3].type = ENGINE_STEP_WAIT;
    out[3].a = gap_ms;
    return 4;
}

static void mark_done(struct engine_runner *runner)
{
    if (!runner || runner->done)
        return;
    runner->done = 1;
    runner->paused = 0;
    runner->hold_count = 0;
    runner->pulse_count = 0;
    runner->pressed_count = 0;
    runner->stack_count = 0;
}

static int stop_mode(const struct engine_runner *runner)
{
    int i;

    for (i = runner->stack_count - 1; i >= 0; i--) {
        int mode = runner->stack[i].release_stop;
        if (mode == ENGINE_STOP_FINISH || mode == ENGINE_STOP_NEXT_UP)
            return mode;
    }
    return runner->release_stop;
}

static int hold_has(const struct engine_runner *runner, uint16_t id)
{
    int i;

    for (i = 0; i < runner->hold_count; i++) {
        if (runner->holds[i] == id)
            return 1;
    }
    return 0;
}

static int hold_add(struct engine_runner *runner, uint16_t id)
{
    if (id == 0 || hold_has(runner, id))
        return 1;
    if (runner->hold_count >= ENGINE_HOLD_CAP)
        return 0;
    runner->holds[runner->hold_count++] = id;
    return 1;
}

static void hold_del(struct engine_runner *runner, uint16_t id)
{
    int i;
    int j;

    for (i = 0; i < runner->hold_count; i++) {
        if (runner->holds[i] != id)
            continue;
        for (j = i + 1; j < runner->hold_count; j++)
            runner->holds[j - 1] = runner->holds[j];
        runner->hold_count--;
        return;
    }
}

static int press_has(const struct engine_runner *runner, uint16_t id)
{
    int i;

    for (i = 0; i < runner->pressed_count; i++) {
        if (runner->pressed_now[i] == id)
            return 1;
    }
    return 0;
}

static void press_add(struct engine_runner *runner, uint16_t id)
{
    if (id == 0 || press_has(runner, id))
        return;
    if (runner->pressed_count >= ENGINE_HOLD_CAP)
        return;
    runner->pressed_now[runner->pressed_count++] = id;
}

static void press_del(struct engine_runner *runner, uint16_t id)
{
    int i;
    int j;

    for (i = 0; i < runner->pressed_count; i++) {
        if (runner->pressed_now[i] != id)
            continue;
        for (j = i + 1; j < runner->pressed_count; j++)
            runner->pressed_now[j - 1] = runner->pressed_now[j];
        runner->pressed_count--;
        return;
    }
}

static void pulse_del(struct engine_runner *runner, uint16_t id)
{
    int i;
    int j;

    for (i = 0; i < runner->pulse_count; i++) {
        if (runner->pulses[i].id != id)
            continue;
        for (j = i + 1; j < runner->pulse_count; j++)
            runner->pulses[j - 1] = runner->pulses[j];
        runner->pulse_count--;
        return;
    }
}

static int pulse_set(struct engine_runner *runner, uint16_t id, int64_t until)
{
    int i;

    if (id == 0)
        return 1;
    for (i = 0; i < runner->pulse_count; i++) {
        if (runner->pulses[i].id == id) {
            runner->pulses[i].until = until;
            return 1;
        }
    }
    if (runner->pulse_count >= ENGINE_HOLD_CAP)
        return 0;
    runner->pulses[runner->pulse_count].id = id;
    runner->pulses[runner->pulse_count].until = until;
    runner->pulse_count++;
    return 1;
}

static void pulse_drop_due(struct engine_runner *runner, int64_t now)
{
    int i;

    for (i = 0; i < runner->pulse_count;) {
        if (runner->pulses[i].until > now) {
            i++;
            continue;
        }
        pulse_del(runner, runner->pulses[i].id);
    }
}

static int arm_ms(struct engine_runner *runner, int64_t now, double ms, double speed)
{
    int64_t scaled = engine_scale_wait_ms(ms, speed);
    int64_t delta = engine_ms_to_ticks(scaled, runner->freq);

    runner->wait_until = engine_arm_deadline(now, delta);
    return ENGINE_ADV_WAIT;
}

static int32_t clamp_move(int32_t value)
{
    if (value > ENGINE_REPORT_MOVE_LIMIT)
        return ENGINE_REPORT_MOVE_LIMIT;
    if (value < -ENGINE_REPORT_MOVE_LIMIT)
        return -ENGINE_REPORT_MOVE_LIMIT;
    return value;
}

static int32_t sat_add(int32_t a, int32_t b)
{
    int64_t sum = (int64_t)a + (int64_t)b;

    if (sum > INT32_MAX)
        return INT32_MAX;
    if (sum < INT32_MIN)
        return INT32_MIN;
    return (int32_t)sum;
}

static int32_t sat_delta(int32_t to, int32_t from)
{
    int64_t delta = (int64_t)to - (int64_t)from;

    if (delta > INT32_MAX)
        return INT32_MAX;
    if (delta < INT32_MIN)
        return INT32_MIN;
    return (int32_t)delta;
}

static int local_to_screen(const struct engine_play *play, int where, int32_t x, int32_t y,
                           int32_t *sx, int32_t *sy)
{
    if (where != ENGINE_WHERE_WINDOW && where != ENGINE_WHERE_CLIENT) {
        *sx = x;
        *sy = y;
        return 1;
    }
    if (!play || !play->have_pointer)
        return 0;
    if (where == ENGINE_WHERE_CLIENT) {
        if (!play->pointer.have_client)
            return 0;
        *sx = sat_add(play->pointer.client_x, x);
        *sy = sat_add(play->pointer.client_y, y);
        return 1;
    }
    if (!play->pointer.have_window)
        return 0;
    *sx = sat_add(play->pointer.window_x, x);
    *sy = sat_add(play->pointer.window_y, y);
    return 1;
}

/* Board output uses the remembered point after the first goto of this runner.
   Software, and the first goto, use the snapshot. A missing snapshot is 0, 0. */
static void mouse_pos(const struct engine_play *play, int32_t *x, int32_t *y)
{
    *x = 0;
    *y = 0;
    if (!play)
        return;
    if (play->output.mode != ENGINE_OUTPUT_SOFTWARE && play->output.mouse_ok) {
        *x = play->output.mouse_x;
        *y = play->output.mouse_y;
        return;
    }
    if (play->have_pointer && play->pointer.have_mouse) {
        *x = play->pointer.mouse_x;
        *y = play->pointer.mouse_y;
    }
}

static int push_frame(struct engine_runner *runner, uint32_t first, uint32_t count,
                      int32_t repeats, int release_stop)
{
    struct engine_frame *frame;

    if (runner->stack_count >= ENGINE_FRAME_CAP) {
        mark_done(runner);
        return 0;
    }
    if (first > runner->step_count)
        first = runner->step_count;
    if (count > runner->step_count - first)
        count = runner->step_count - first;
    frame = &runner->stack[runner->stack_count++];
    memset(frame, 0, sizeof *frame);
    frame->first = first;
    frame->count = count;
    frame->repeats_left = repeats;
    frame->release_stop = release_stop;
    return 1;
}

static int finish_frame(struct engine_runner *runner)
{
    struct engine_frame *frame = &runner->stack[runner->stack_count - 1];

    if (frame->count == 0) {
        runner->stack_count--;
        return 1;
    }
    /* repeats_left 0 is forever. Play-mode repeat never stores 0.
       A repeat step may. */
    if (frame->repeats_left == 0) {
        if (runner->stopping) {
            mark_done(runner);
            return 0;
        }
        frame->index = 0;
        return 1;
    }
    frame->repeats_left--;
    if (frame->repeats_left > 0) {
        frame->index = 0;
        return 1;
    }
    runner->stack_count--;
    return 1;
}

static int exec_step(struct engine_runner *runner, struct engine_play *play,
                     const struct engine_step *step, int64_t now, engine_roll_fn roll,
                     void *roll_ctx)
{
    uint16_t id;
    int32_t count;

    if (step->type == ENGINE_STEP_RANDOM_WAIT) {
        struct engine_span span = engine_wait_span((double)step->a, (double)step->b);
        int64_t picked = span.lo;

        runner->scan_wait = 0;
        if (span.lo != span.hi) {
            if (roll)
                picked = roll(span.lo, span.hi, roll_ctx);
            picked = engine_apply_roll(span, picked);
        }
        return arm_ms(runner, now, (double)picked, runner->speed);
    }
    if (step->type == ENGINE_STEP_WAIT) {
        runner->scan_wait = 0;
        return arm_ms(runner, now, (double)step->a, runner->speed);
    }
    if (step->type == ENGINE_STEP_KEY || step->type == ENGINE_STEP_MOUSE) {
        id = step->key;
        if (id == 0 || id > ENGINE_KEY_ID_MAX)
            return ENGINE_ADV_CONT;
        if (step->action == ENGINE_ACT_DOWN) {
            if (!hold_add(runner, id)) {
                mark_done(runner);
                return ENGINE_ADV_WAIT;
            }
            pulse_del(runner, id);
            press_add(runner, id);
            runner->saw_up = 0;
            return ENGINE_ADV_CONT;
        }
        if (step->action == ENGINE_ACT_UP) {
            int just = press_has(runner, id);

            hold_del(runner, id);
            pulse_del(runner, id);
            press_del(runner, id);
            runner->saw_up = 1;
            runner->released_now = 1;
            /* A down and an up in one tick would never be sent. */
            if (just) {
                int64_t one = engine_ms_to_ticks(1, runner->freq);
                if (!pulse_set(runner, id, engine_arm_deadline(now, one))) {
                    mark_done(runner);
                    return ENGINE_ADV_WAIT;
                }
                return arm_ms(runner, now, 1.0, 1.0);
            }
            if (runner->stopping && stop_mode(runner) == ENGINE_STOP_NEXT_UP) {
                mark_done(runner);
                return ENGINE_ADV_WAIT;
            }
            return ENGINE_ADV_CONT;
        }
        {
            int64_t scaled = engine_scale_wait_ms((double)step->a, runner->speed);
            int64_t delta = engine_ms_to_ticks(scaled, runner->freq);
            if (!pulse_set(runner, id, engine_arm_deadline(now, delta))) {
                mark_done(runner);
                return ENGINE_ADV_WAIT;
            }
        }
        return ENGINE_ADV_CONT;
    }
    if (step->type == ENGINE_STEP_MOVE) {
        runner->move_x = sat_add(runner->move_x, clamp_move(step->a));
        runner->move_y = sat_add(runner->move_y, clamp_move(step->b));
        return ENGINE_ADV_CONT;
    }
    if (step->type == ENGINE_STEP_SCROLL) {
        runner->wheel = sat_add(runner->wheel, step->a);
        return ENGINE_ADV_CONT;
    }
    if (step->type == ENGINE_STEP_REPEAT) {
        count = step->a;
        if (count < 0)
            count = 0;
        if (!push_frame(runner, step->first, step->count, count, step->b))
            return ENGINE_ADV_WAIT;
        return ENGINE_ADV_CONT;
    }
    if (step->type == ENGINE_STEP_GOTO) {
        int32_t sx = 0;
        int32_t sy = 0;
        int32_t mx;
        int32_t my;

        /* The first goto of this runner drops the remembered point before
           the delta, even when the window conversion fails. */
        if (!runner->did_goto) {
            if (play)
                engine_output_forget_mouse(&play->output);
            runner->did_goto = 1;
        }
        if (!local_to_screen(play, step->action, step->a, step->b, &sx, &sy))
            return ENGINE_ADV_CONT;
        mouse_pos(play, &mx, &my);
        runner->move_x = sat_add(runner->move_x, sat_delta(sx, mx));
        runner->move_y = sat_add(runner->move_y, sat_delta(sy, my));
        runner->has_goto = 1;
        runner->goto_x = sx;
        runner->goto_y = sy;
        if (play)
            engine_output_remember_mouse(&play->output, sx, sy);
        if (step->count > 0)
            return arm_ms(runner, now, (double)step->count, runner->speed);
        return ENGINE_ADV_CONT;
    }
    if (step->type == ENGINE_STEP_RUN) {
        const struct engine_macro *target;

        if (!play || !play->macros || step->a < 0 || step->a >= play->macro_count)
            return ENGINE_ADV_CONT;
        target = &play->macros[step->a];
        /* once, and do not restart a live id. No cycle guard. */
        engine_play_start(play, target, ENGINE_MODE_ONCE, 0);
        return ENGINE_ADV_CONT;
    }
    return ENGINE_ADV_CONT;
}

static void advance_runner(struct engine_runner *runner, struct engine_play *play, int64_t now,
                           engine_roll_fn roll, void *roll_ctx)
{
    int n;

    if (!runner)
        return;
    runner->move_x = 0;
    runner->move_y = 0;
    runner->wheel = 0;
    runner->has_goto = 0;
    runner->goto_x = 0;
    runner->goto_y = 0;
    runner->released_now = 0;
    runner->pressed_count = 0;
    if (runner->done || runner->paused)
        return;
    pulse_drop_due(runner, now);
    if (runner->wait_until != 0 && now < runner->wait_until)
        return;
    runner->wait_until = 0;
    runner->scan_wait = 0;
    if (runner->stopping && stop_mode(runner) == ENGINE_STOP_NEXT_UP && runner->saw_up) {
        mark_done(runner);
        return;
    }
    if (runner->started_at < 0)
        runner->started_at = now;

    for (n = 0; n < 64; n++) {
        struct engine_frame *frame;
        const struct engine_step *step;
        uint32_t at;
        int result;

        if (runner->done || runner->stack_count == 0) {
            mark_done(runner);
            return;
        }
        frame = &runner->stack[runner->stack_count - 1];
        if (frame->index >= frame->count) {
            if (!finish_frame(runner))
                return;
            continue;
        }
        at = frame->first + frame->index;
        frame->index++;
        if (!runner->steps || at >= runner->step_count)
            continue;
        step = &runner->steps[at];
        result = exec_step(runner, play, step, now, roll, roll_ctx);
        if (result == ENGINE_ADV_AGAIN) {
            if (frame->index > 0)
                frame->index--;
            return;
        }
        if (result == ENGINE_ADV_WAIT)
            return;
    }
    if (!runner->done && runner->wait_until == 0)
        arm_ms(runner, now, 1.0, 1.0);
}

void engine_runner_advance(struct engine_runner *runner, int64_t now,
                           engine_roll_fn roll, void *roll_ctx)
{
    advance_runner(runner, NULL, now, roll, roll_ctx);
}

static int playing(const struct engine_play *play, const char *id)
{
    int i;

    for (i = 0; i < play->runner_count; i++) {
        if (!play->runners[i].done && same_id(play->runners[i].id, id))
            return 1;
    }
    for (i = 0; i < play->pending_count; i++) {
        if (same_id(play->pending[i].id, id))
            return 1;
    }
    return 0;
}

static double raised_speed(double speed)
{
    if (!isfinite(speed))
        speed = 1.0;
    if (speed < 0.05)
        speed = 0.05;
    return speed;
}

static uint32_t macro_step_count(const struct engine_macro *macro)
{
    struct engine_step scratch[4];
    int wrote;

    if (macro->basic) {
        wrote = engine_basic_rewrite(macro->trigger, macro->trigger_is_key,
                                     macro->gap_ms, scratch, 4);
        if (wrote > 0)
            return (uint32_t)wrote;
    }
    if (!macro->steps)
        return 0;
    return macro->step_count;
}

static int arena_fits(const struct engine_play *play, uint32_t count)
{
    if (count > ENGINE_STEP_CAP)
        return 0;
    return play->arena_count <= ENGINE_STEP_CAP - count;
}

static int queue_runner(struct engine_play *play, const struct engine_macro *macro, int mode)
{
    struct engine_step basic[4];
    struct engine_runner *runner;
    const struct engine_step *src;
    uint32_t count;
    uint32_t top;
    uint32_t offset;
    int basic_count;
    int hold;

    if (play->pending_count >= ENGINE_RUNNER_CAP)
        return 0;
    if (play->runner_count + play->pending_count >= ENGINE_RUNNER_CAP)
        return 0;

    src = macro->steps;
    count = src ? macro->step_count : 0;
    top = macro->top_count;
    if (macro->basic) {
        basic_count = engine_basic_rewrite(macro->trigger, macro->trigger_is_key,
                                           macro->gap_ms, basic, 4);
        if (basic_count > 0) {
            src = basic;
            count = (uint32_t)basic_count;
            top = count;
        }
    }
    if (!src)
        count = 0;
    if (top == 0)
        top = count;
    if (top > count)
        top = count;
    if (!arena_fits(play, count))
        return 0;

    offset = play->arena_count;
    if (count > 0)
        memcpy(play->arena + offset, src, count * sizeof *src);
    play->arena_count += count;

    runner = &play->pending[play->pending_count];
    memset(runner, 0, sizeof *runner);
    copy_id(runner->id, macro->id);
    runner->speed = raised_speed(macro->speed);
    runner->freq = play->freq > 0 ? play->freq : 1000;
    runner->started_at = -1;
    runner->steps = play->arena + offset;
    runner->step_count = count;
    runner->mode = mode;
    runner->release_stop = macro->release_stop == ENGINE_STOP_FINISH
                               ? ENGINE_STOP_FINISH
                               : ENGINE_STOP_NEXT_UP;
    runner->trigger = macro->trigger;
    hold = engine_has_hold_loop(runner->steps, top);
    if (!push_frame(runner, 0, top, engine_top_repeats(mode, macro->repeat_count, hold),
                    ENGINE_STOP_NONE))
        return 0;
    play->pending_count++;
    return 1;
}

void engine_play_init(struct engine_play *play)
{
    if (!play)
        return;
    memset(play, 0, sizeof *play);
    engine_output_init(&play->output);
    engine_report_clear(&play->last);
    play->freq = 1000;
    play->dpi = 96;
}

void engine_play_set_roll(struct engine_play *play, engine_roll_fn roll, void *ctx)
{
    if (!play)
        return;
    play->roll = roll;
    play->roll_ctx = ctx;
}

void engine_play_set_macros(struct engine_play *play, const struct engine_macro *macros, int count)
{
    if (!play)
        return;
    if (!macros || count <= 0) {
        play->macros = NULL;
        play->macro_count = 0;
        return;
    }
    play->macros = macros;
    play->macro_count = count;
}

void engine_play_set_pointer(struct engine_play *play, const struct engine_pointer *pointer)
{
    if (!play)
        return;
    if (!pointer) {
        memset(&play->pointer, 0, sizeof play->pointer);
        play->have_pointer = 0;
        return;
    }
    play->pointer = *pointer;
    play->have_pointer = 1;
}

int engine_play_start(struct engine_play *play, const struct engine_macro *macro,
                      int mode_override, int restart)
{
    int mode;
    char id[ENGINE_ID_CAP];

    if (!play || !macro)
        return 0;
    copy_id(id, macro->id);
    if (!id[0])
        return 0;
    if (play->runner_count + play->pending_count >= ENGINE_RUNNER_CAP)
        return 0;
    if (!arena_fits(play, macro_step_count(macro)))
        return 0;
    mode = mode_override >= 0 ? mode_override : macro->mode;
    if (playing(play, id) && !restart)
        return 0;
    if (playing(play, id) &&
        (mode == ENGINE_MODE_WHILE_HELD || mode == ENGINE_MODE_TOGGLE))
        return 0;
    if (macro->busy && playing(play, id))
        return 0;
    if (playing(play, id))
        engine_play_stop(play, id);
    if (macro->exclusive)
        engine_play_stop_all(play);
    return queue_runner(play, macro, mode);
}

void engine_play_flush(struct engine_play *play)
{
    int i;

    if (!play)
        return;
    for (i = 0; i < play->pending_count; i++) {
        if (play->runner_count >= ENGINE_RUNNER_CAP)
            break;
        play->runners[play->runner_count++] = play->pending[i];
    }
    if (i < play->pending_count) {
        int left = play->pending_count - i;
        memmove(&play->pending[0], &play->pending[i], (size_t)left * sizeof play->pending[0]);
        play->pending_count = left;
        return;
    }
    play->pending_count = 0;
}

void engine_play_advance(struct engine_play *play, int64_t now)
{
    int i;

    if (!play)
        return;
    for (i = 0; i < play->runner_count; i++)
        advance_runner(&play->runners[i], play, now, play->roll, play->roll_ctx);
}

static int already(const uint16_t *ids, int count, uint16_t id)
{
    int i;

    for (i = 0; i < count; i++) {
        if (ids[i] == id)
            return 1;
    }
    return 0;
}

int engine_play_desired(const struct engine_play *play, int64_t now,
                        uint16_t *out, int cap)
{
    int count = 0;
    int i;
    int h;

    if (!play || !out || cap <= 0)
        return 0;
    for (i = 0; i < play->runner_count; i++) {
        const struct engine_runner *runner = &play->runners[i];
        if (runner->done || runner->paused)
            continue;
        for (h = 0; h < runner->hold_count && count < cap; h++) {
            uint16_t id = runner->holds[h];
            if (id == 0 || already(out, count, id))
                continue;
            out[count++] = id;
        }
        for (h = 0; h < runner->pulse_count && count < cap; h++) {
            uint16_t id = runner->pulses[h].id;
            if (runner->pulses[h].until <= now || id == 0 || already(out, count, id))
                continue;
            out[count++] = id;
        }
    }
    return count;
}

static int32_t clamp_sum(int64_t value)
{
    if (value > ENGINE_REPORT_MOVE_LIMIT)
        return ENGINE_REPORT_MOVE_LIMIT;
    if (value < -ENGINE_REPORT_MOVE_LIMIT)
        return -ENGINE_REPORT_MOVE_LIMIT;
    return (int32_t)value;
}

static int32_t sat_i32(int64_t value)
{
    if (value > INT32_MAX)
        return INT32_MAX;
    if (value < INT32_MIN)
        return INT32_MIN;
    return (int32_t)value;
}

int engine_play_publish(struct engine_play *play, int64_t now)
{
    uint16_t desired[ENGINE_HOLD_CAP];
    int desired_count;
    int64_t dx = 0;
    int64_t dy = 0;
    int64_t wheel = 0;
    int has_abs = 0;
    int32_t ax = 0;
    int32_t ay = 0;
    int i;

    if (!play)
        return 0;
    for (i = 0; i < play->runner_count; i++) {
        struct engine_runner *runner = &play->runners[i];
        if (runner->paused)
            continue;
        dx += runner->move_x;
        dy += runner->move_y;
        wheel += runner->wheel;
        if (runner->has_goto) {
            has_abs = 1;
            ax = runner->goto_x;
            ay = runner->goto_y;
            runner->has_goto = 0;
        }
        runner->move_x = 0;
        runner->move_y = 0;
        runner->wheel = 0;
    }
    desired_count = engine_play_desired(play, now, desired, ENGINE_HOLD_CAP);
    if (has_abs)
        return engine_output_publish(&play->output, desired, desired_count, NULL, 0,
                                     0, 0, sat_i32(wheel), 1, ax, ay, play->dpi, &play->last);
    return engine_output_publish(&play->output, desired, desired_count, NULL, 0,
                                 clamp_sum(dx), clamp_sum(dy), sat_i32(wheel),
                                 0, 0, 0, play->dpi, &play->last);
}

void engine_play_remove_done(struct engine_play *play)
{
    int write;
    int i;

    if (!play)
        return;
    write = 0;
    for (i = 0; i < play->runner_count; i++) {
        if (play->runners[i].done)
            continue;
        if (write != i)
            play->runners[write] = play->runners[i];
        write++;
    }
    play->runner_count = write;
}

void engine_play_stop(struct engine_play *play, const char *id)
{
    int write;
    int i;

    if (!play || !id)
        return;
    for (i = 0; i < play->runner_count; i++) {
        if (same_id(play->runners[i].id, id))
            mark_done(&play->runners[i]);
    }
    write = 0;
    for (i = 0; i < play->pending_count; i++) {
        if (same_id(play->pending[i].id, id))
            continue;
        if (write != i)
            play->pending[write] = play->pending[i];
        write++;
    }
    play->pending_count = write;
}

void engine_play_stop_all(struct engine_play *play)
{
    int i;

    if (!play)
        return;
    for (i = 0; i < play->runner_count; i++)
        mark_done(&play->runners[i]);
    play->pending_count = 0;
}

void engine_play_ask_stop(struct engine_play *play, const char *id)
{
    int i;

    if (!play || !id)
        return;
    for (i = 0; i < play->runner_count; i++) {
        struct engine_runner *runner = &play->runners[i];
        if (!same_id(runner->id, id) || runner->done)
            continue;
        runner->stopping = 1;
        if (stop_mode(runner) == ENGINE_STOP_NEXT_UP && runner->saw_up)
            mark_done(runner);
    }
}

void engine_play_tick(struct engine_play *play, int64_t now)
{
    if (!play)
        return;
    engine_play_remove_done(play);
    engine_play_flush(play);
    engine_play_advance(play, now);
    engine_play_flush(play);
    engine_play_publish(play, now);
    if (engine_report_flags(&play->last) & ENGINE_REPORT_OVERFLOW)
        engine_play_stop_all(play);
    engine_play_remove_done(play);
}

int engine_play_alive(const struct engine_play *play)
{
    int count = 0;
    int i;

    if (!play)
        return 0;
    for (i = 0; i < play->runner_count; i++) {
        if (!play->runners[i].done)
            count++;
    }
    return count;
}

struct engine_runner *engine_play_runner(struct engine_play *play, int index)
{
    int seen = 0;
    int i;

    if (!play || index < 0)
        return NULL;
    for (i = 0; i < play->runner_count; i++) {
        if (play->runners[i].done)
            continue;
        if (seen == index)
            return &play->runners[i];
        seen++;
    }
    return NULL;
}
