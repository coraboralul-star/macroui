#include "output.h"

#include "clock.h"

#include <string.h>

static int contains_id(const uint16_t *ids, int count, uint16_t id)
{
    int i;

    for (i = 0; i < count; i++) {
        if (ids[i] == id)
            return 1;
    }
    return 0;
}

static void remove_applied(struct engine_output *out, uint16_t id)
{
    int i;
    int j;

    for (i = 0; i < out->applied_count; i++) {
        if (out->applied[i] != id)
            continue;
        for (j = i + 1; j < out->applied_count; j++)
            out->applied[j - 1] = out->applied[j];
        out->applied_count--;
        return;
    }
}

static int32_t sat_i32(int64_t value)
{
    if (value > INT32_MAX)
        return INT32_MAX;
    if (value < INT32_MIN)
        return INT32_MIN;
    return (int32_t)value;
}

static int32_t scale_axis(int32_t value, uint32_t dpi)
{
    if (dpi == 0 || dpi == 96)
        return value;
    return sat_i32(engine_round_half_away((double)value * (double)dpi / 96.0));
}

static int32_t negate_wheel(int32_t wheel)
{
    if (wheel == INT32_MIN)
        return INT32_MAX;
    return -wheel;
}

static int key_extended(uint16_t id, uint16_t vk)
{
    if (id == ENGINE_KEY_NUMPAD_ENTER)
        return 1;
    switch (vk) {
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x2C:
    case 0x2D:
    case 0x2E:
    case 0x5C:
    case 0x5D:
    case 0x6F:
    case 0x90:
    case 0xA3:
    case 0xA5:
        return 1;
    default:
        return 0;
    }
}

static void button_event(uint16_t id, int up, uint32_t *flags, uint32_t *data)
{
    *data = 0;
    *flags = 0;
    switch (id) {
    case ENGINE_KEY_LBUTTON:
        *flags = up ? ENGINE_MOUSE_LEFTUP : ENGINE_MOUSE_LEFTDOWN;
        break;
    case ENGINE_KEY_RBUTTON:
        *flags = up ? ENGINE_MOUSE_RIGHTUP : ENGINE_MOUSE_RIGHTDOWN;
        break;
    case ENGINE_KEY_MBUTTON:
        *flags = up ? ENGINE_MOUSE_MIDDLEUP : ENGINE_MOUSE_MIDDLEDOWN;
        break;
    case ENGINE_KEY_XBUTTON1:
        *flags = up ? ENGINE_MOUSE_XUP : ENGINE_MOUSE_XDOWN;
        *data = 1;
        break;
    case ENGINE_KEY_XBUTTON2:
        *flags = up ? ENGINE_MOUSE_XUP : ENGINE_MOUSE_XDOWN;
        *data = 2;
        break;
    default:
        break;
    }
}

static void push_send(struct engine_send_op *ops, int cap, int *count,
                      const struct engine_send_op *item)
{
    if (*count < cap && ops)
        ops[*count] = *item;
    (*count)++;
}

static void push_board(struct engine_board_op *ops, int cap, int *count,
                       const struct engine_board_op *item)
{
    if (*count < cap && ops)
        ops[*count] = *item;
    (*count)++;
}

static void queue_key(const struct engine_report *report, int up, int index,
                      int swap_clicks, struct engine_send_op *ops, int cap, int *count)
{
    struct engine_send_op item;
    uint16_t id = up ? engine_report_up(report, index) : engine_report_down(report, index);
    int bit;

    memset(&item, 0, sizeof item);
    if (id == 0 || id > ENGINE_KEY_ID_MAX)
        return;
    id = engine_swap_button(id, swap_clicks);
    bit = engine_mouse_bit(id);
    if (bit >= 0) {
        item.kind = ENGINE_SEND_BUTTON;
        item.id = id;
        button_event(id, up, &item.flags, &item.data);
        push_send(ops, cap, count, &item);
        return;
    }
    if (id == ENGINE_KEY_NUMPAD_ENTER) {
        item.kind = ENGINE_SEND_KEY;
        item.id = id;
        item.vk = 0x0D;
        item.flags = ENGINE_SEND_EXTENDED;
    } else if (id <= 255) {
        item.kind = ENGINE_SEND_KEY;
        item.id = id;
        item.vk = id;
        if (key_extended(id, id))
            item.flags = ENGINE_SEND_EXTENDED;
    } else {
        return;
    }
    if (up)
        item.flags |= ENGINE_SEND_KEYUP;
    push_send(ops, cap, count, &item);
}

void engine_output_init(struct engine_output *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);
}

void engine_output_set_swap(struct engine_output *out, int on)
{
    if (!out)
        return;
    out->swap_clicks = on ? 1 : 0;
}

int engine_output_release(struct engine_output *out, struct engine_report *release)
{
    uint16_t ups[ENGINE_APPLIED_CAP];
    int count;
    int keep;
    int i;

    if (!out || !release)
        return 0;
    engine_report_clear(release);
    count = out->applied_count;
    if (count > ENGINE_APPLIED_CAP)
        count = ENGINE_APPLIED_CAP;
    if (count < 0)
        count = 0;
    for (i = 0; i < count; i++)
        ups[i] = out->applied[i];
    keep = count > ENGINE_REPORT_CAP ? ENGINE_REPORT_CAP : count;
    for (i = 0; i < keep; i++)
        engine_report_push_up(release, ups[i]);
    if (count > ENGINE_REPORT_CAP)
        engine_report_set_overflow(release);
    if (keep >= out->applied_count) {
        out->applied_count = 0;
    } else {
        int tail = out->applied_count - keep;
        memmove(out->applied, out->applied + keep, (size_t)tail * sizeof out->applied[0]);
        out->applied_count = tail;
    }
    return engine_report_active(release);
}

int engine_output_set_mode(struct engine_output *out, int mode,
                           struct engine_report *release, int *rebind,
                           int *sent_mode)
{
    int next = mode;

    if (!out)
        return 0;
    if (next != ENGINE_OUTPUT_RP2040 && next != ENGINE_OUTPUT_RP2350)
        next = ENGINE_OUTPUT_SOFTWARE;
    if (sent_mode)
        *sent_mode = out->mode;
    if (next == out->mode) {
        if (rebind)
            *rebind = 0;
        if (release)
            engine_report_clear(release);
        return 0;
    }
    if (release)
        engine_output_release(out, release);
    else
        out->applied_count = 0;
    out->mouse_ok = 0;
    out->mode = next;
    if (rebind)
        *rebind = 1;
    return 1;
}

int engine_output_publish(struct engine_output *out,
                          const uint16_t *desired, int desired_count,
                          const uint16_t *extra, int extra_count,
                          int32_t dx, int32_t dy, int32_t wheel,
                          int absolute, int32_t ax, int32_t ay, uint32_t dpi,
                          struct engine_report *report)
{
    uint16_t want[ENGINE_APPLIED_CAP];
    uint8_t want_seen[ENGINE_SWALLOW_IDS];
    uint16_t downs[ENGINE_APPLIED_CAP];
    uint16_t ups[ENGINE_APPLIED_CAP];
    int want_count = 0;
    int down_count = 0;
    int up_count = 0;
    int i;

    if (!out || !report)
        return 0;
    if (!desired || desired_count < 0)
        desired_count = 0;
    if (!extra || extra_count < 0)
        extra_count = 0;
    engine_report_clear(report);
    memset(want_seen, 0, sizeof want_seen);

    for (i = 0; i < desired_count && want_count < ENGINE_APPLIED_CAP; i++) {
        uint16_t id = desired[i];
        if (id == 0 || id > ENGINE_KEY_ID_MAX || want_seen[id])
            continue;
        want_seen[id] = 1;
        want[want_count++] = id;
    }
    for (i = 0; i < want_count; i++) {
        if (contains_id(out->applied, out->applied_count, want[i]))
            continue;
        downs[down_count++] = want[i];
    }
    for (i = 0; i < extra_count && down_count < ENGINE_APPLIED_CAP; i++) {
        uint16_t id = extra[i];
        if (id == 0 || id > ENGINE_KEY_ID_MAX || engine_is_mouse_button(id))
            continue;
        if (!want_seen[id] || contains_id(downs, down_count, id))
            continue;
        downs[down_count++] = id;
    }
    for (i = 0; i < out->applied_count && up_count < ENGINE_APPLIED_CAP; i++) {
        uint16_t id = out->applied[i];
        if (want_seen[id])
            continue;
        ups[up_count++] = id;
    }

    for (i = 0; i < down_count && i < ENGINE_REPORT_CAP; i++)
        engine_report_push_down(report, downs[i]);
    if (down_count > ENGINE_REPORT_CAP)
        engine_report_set_overflow(report);
    for (i = 0; i < up_count && i < ENGINE_REPORT_CAP; i++)
        engine_report_push_up(report, ups[i]);
    if (up_count > ENGINE_REPORT_CAP)
        engine_report_set_overflow(report);

    if (absolute)
        engine_report_set_absolute(report, ax, ay);
    else
        engine_report_set_relative(report, dx, dy);
    engine_report_set_wheel(report, wheel);
    engine_report_set_dpi(report, dpi);

    for (i = 0; i < down_count; i++) {
        if (contains_id(out->applied, out->applied_count, downs[i]))
            continue;
        if (out->applied_count >= ENGINE_APPLIED_CAP) {
            engine_report_set_overflow(report);
            break;
        }
        out->applied[out->applied_count++] = downs[i];
    }
    {
        int keep = up_count > ENGINE_REPORT_CAP ? ENGINE_REPORT_CAP : up_count;
        for (i = 0; i < keep; i++)
            remove_applied(out, ups[i]);
    }
    return engine_report_active(report);
}

void engine_output_remember_mouse(struct engine_output *out, int32_t x, int32_t y)
{
    if (!out)
        return;
    out->mouse_x = x;
    out->mouse_y = y;
    out->mouse_ok = 1;
}

void engine_output_forget_mouse(struct engine_output *out)
{
    if (!out)
        return;
    out->mouse_ok = 0;
}

void engine_output_note_move(struct engine_output *out, const struct engine_report *report)
{
    if (!out || !report)
        return;
    if (engine_report_flags(report) & ENGINE_REPORT_ABSOLUTE) {
        engine_output_remember_mouse(out, engine_report_ax(report), engine_report_ay(report));
        return;
    }
    if (!out->mouse_ok)
        return;
    if (engine_report_dx(report) == 0 && engine_report_dy(report) == 0)
        return;
    out->mouse_x += engine_report_dx(report);
    out->mouse_y += engine_report_dy(report);
}

uint16_t engine_swap_button(uint16_t id, int swap_clicks)
{
    if (!swap_clicks)
        return id;
    if (id == ENGINE_KEY_LBUTTON)
        return ENGINE_KEY_RBUTTON;
    if (id == ENGINE_KEY_RBUTTON)
        return ENGINE_KEY_LBUTTON;
    return id;
}

int engine_mouse_bit(uint16_t id)
{
    if (id < ENGINE_KEY_LBUTTON || id > ENGINE_KEY_XBUTTON2)
        return -1;
    return (int)(id - ENGINE_KEY_LBUTTON);
}

int engine_hid_usage(uint16_t id)
{
    if (!engine_key_name(id) || engine_is_mouse_button(id))
        return 0;
    if (id == ENGINE_KEY_NUMPAD_ENTER)
        return 0x58;
    if (id >= 0x41 && id <= 0x5A)
        return 0x04 + (id - 0x41);
    if (id >= 0x31 && id <= 0x39)
        return 0x1E + (id - 0x31);
    if (id == 0x30)
        return 0x27;
    if (id >= 0x70 && id <= 0x7B)
        return 0x3A + (id - 0x70);
    switch (id) {
    case 0x0D: return 0x28;
    case 0x10:
    case 0xA0: return 0xE1;
    case 0x11:
    case 0xA2: return 0xE0;
    case 0x12:
    case 0xA4: return 0xE2;
    case 0x20: return 0x2C;
    case 0x5B: return 0xE3;
    case 0x5C: return 0xE7;
    case 0xA1: return 0xE5;
    case 0xA3: return 0xE4;
    case 0xA5: return 0xE6;
    default: return 0;
    }
}

int engine_report_send(const struct engine_report *report, int swap_clicks,
                       struct engine_send_op *ops, int cap, int *count)
{
    struct engine_send_op item;
    int written = 0;
    int i;
    int32_t wheel;
    int32_t notches;

    if (count)
        *count = 0;
    if (!report || !engine_report_active(report) || cap < 0)
        return 0;
    for (i = 0; i < engine_report_down_count(report); i++)
        queue_key(report, 0, i, swap_clicks, ops, cap, &written);
    for (i = 0; i < engine_report_up_count(report); i++)
        queue_key(report, 1, i, swap_clicks, ops, cap, &written);

    wheel = engine_report_wheel(report);
    if (wheel != 0) {
        int64_t data = (int64_t)wheel * -(int64_t)ENGINE_WHEEL_DELTA;
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_SEND_WHEEL;
        item.flags = ENGINE_MOUSE_WHEEL;
        item.data = (uint32_t)sat_i32(data);
        push_send(ops, cap, &written, &item);
    }

    if (engine_report_flags(report) & ENGINE_REPORT_ABSOLUTE) {
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_SEND_ABS;
        item.x = engine_report_ax(report);
        item.y = engine_report_ay(report);
        push_send(ops, cap, &written, &item);
    } else if (engine_report_dx(report) || engine_report_dy(report)) {
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_SEND_MOVE;
        item.x = engine_report_dx(report);
        item.y = engine_report_dy(report);
        push_send(ops, cap, &written, &item);
    }

    notches = written;
    if (count)
        *count = written > cap ? cap : written;
    return notches > 0;
}

int engine_report_board(const struct engine_report *report, int swap_clicks, int bits,
                        struct engine_board_op *ops, int cap, int *count)
{
    struct engine_board_op item;
    int written = 0;
    int mouse_touch = 0;
    int absolute;
    int i;
    int32_t dx;
    int32_t dy;
    int32_t wheel;
    uint32_t dpi;

    if (count)
        *count = 0;
    if (!report || !engine_report_active(report) || cap < 0)
        return bits;

    dx = engine_report_dx(report);
    dy = engine_report_dy(report);
    wheel = engine_report_wheel(report);
    absolute = (engine_report_flags(report) & ENGINE_REPORT_ABSOLUTE) != 0;
    dpi = engine_report_dpi(report);
    if (!absolute) {
        if (dpi == 0)
            dpi = 96;
        dx = scale_axis(dx, dpi);
        dy = scale_axis(dy, dpi);
    }
    wheel = negate_wheel(wheel);

    for (i = 0; i < engine_report_down_count(report); i++) {
        uint16_t id = engine_swap_button(engine_report_down(report, i), swap_clicks);
        int bit = engine_mouse_bit(id);
        int usage;

        if (id == 0)
            continue;
        if (bit >= 0) {
            bits |= 1 << bit;
            mouse_touch = 1;
            continue;
        }
        usage = engine_hid_usage(id);
        if (!usage)
            continue;
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_BOARD_KEY;
        item.usage = usage;
        item.down = 1;
        push_board(ops, cap, &written, &item);
    }

    if (absolute) {
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_BOARD_STEER;
        item.bits = bits;
        item.x = engine_report_ax(report);
        item.y = engine_report_ay(report);
        push_board(ops, cap, &written, &item);
        mouse_touch = 0;
        dx = 0;
        dy = 0;
    } else if (mouse_touch || dx != 0 || dy != 0 || wheel != 0) {
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_BOARD_MOVE;
        item.bits = bits;
        item.x = dx;
        item.y = dy;
        item.wheel = wheel;
        push_board(ops, cap, &written, &item);
        dx = 0;
        dy = 0;
        wheel = 0;
        mouse_touch = 0;
    }

    for (i = 0; i < engine_report_up_count(report); i++) {
        uint16_t id = engine_swap_button(engine_report_up(report, i), swap_clicks);
        int bit = engine_mouse_bit(id);
        int usage;

        if (id == 0)
            continue;
        if (bit >= 0) {
            bits &= ~(1 << bit);
            mouse_touch = 1;
            continue;
        }
        usage = engine_hid_usage(id);
        if (!usage)
            continue;
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_BOARD_KEY;
        item.usage = usage;
        item.down = 0;
        push_board(ops, cap, &written, &item);
    }

    if (wheel != 0) {
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_BOARD_MOVE;
        item.bits = bits;
        item.wheel = wheel;
        push_board(ops, cap, &written, &item);
    }
    if (mouse_touch) {
        memset(&item, 0, sizeof item);
        item.kind = ENGINE_BOARD_MOVE;
        item.bits = bits;
        push_board(ops, cap, &written, &item);
    }

    if (count)
        *count = written > cap ? cap : written;
    return bits;
}

int engine_report_echoes(const struct engine_report *report,
                         struct engine_report_echo *out, int cap)
{
    int written = 0;
    int i;

    if (!report || !out || cap < 0)
        return 0;
    for (i = 0; i < engine_report_down_count(report); i++) {
        uint16_t id = engine_report_down(report, i);
        if (id == 0)
            continue;
        if (written < cap && out) {
            out[written].id = id;
            out[written].down = 1;
        }
        written++;
    }
    for (i = 0; i < engine_report_up_count(report); i++) {
        uint16_t id = engine_report_up(report, i);
        if (id == 0)
            continue;
        if (written < cap && out) {
            out[written].id = id;
            out[written].down = 0;
        }
        written++;
    }
    return written > cap ? cap : written;
}
