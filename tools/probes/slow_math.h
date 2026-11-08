/* Opt-in experiment: while a gate is held in the target window, owe back half of
   each physical relative mouse count and inject that debt as a later packet.

   s is exactly 1/2. This is not device DPI, not the game's sensitivity, and
   not Windows pointer speed. A fractional count stays in a per-device, per-axis
   remainder until another physical count completes it, or until release, focus
   loss, panic, or exit discards it.

   The caller injects. This file does not call SendInput and does not see
   Windows. Device 0 is injected input (no device handle). A board flag is the
   Vendetta board, which must not be counted or the counter feeds itself. */
#ifndef SLOW_MATH_H
#define SLOW_MATH_H

#include <stdint.h>

#define SLOW_KEEP 1
#define SLOW_DEN 2
#define SLOW_DEVICE_CAP 8

#define SLOW_GATE_NONE 0
#define SLOW_GATE_DOWN 1
#define SLOW_GATE_UP 2

#define SLOW_APPLIED 0
#define SLOW_SKIP_DISARMED 1
#define SLOW_SKIP_NULL 2
#define SLOW_SKIP_BOARD 3
#define SLOW_SKIP_ABSOLUTE 4
#define SLOW_SKIP_STILL 5
#define SLOW_SKIP_FULL 6
#define SLOW_SKIP_STOPPED 7
#define SLOW_SKIP_NO_SAMPLE 8

#define SLOW_EDGE_NONE 0
#define SLOW_EDGE_RELEASE 1
#define SLOW_EDGE_FOCUS 2
#define SLOW_EDGE_PANIC 3
#define SLOW_EDGE_EXIT 4

struct slow_slot {
    uint64_t id;
    int32_t acc_x;
    int32_t acc_y;
};

struct slow_session {
    int armed;
    int gate_down;
    int suppress;
    struct slow_slot slot[SLOW_DEVICE_CAP];
};

/* motion is 0 for a key event. A mouse packet sets motion, device, absolute,
   board, and the raw relative counts. device 0 is a null handle. */
struct slow_event {
    int gate;
    int focus;
    int panic;
    int leaving;
    int motion;
    uint64_t device;
    int absolute;
    int board;
    int32_t dx;
    int32_t dy;
};

struct slow_result {
    int32_t dx;
    int32_t dy;
    int skip;
    int edge;
    int quit;
    int armed;
    int discarded;
};

void slow_session_init(struct slow_session *session);

/* 1 when name contains VID_CAFE&PID_4004, ASCII case-insensitive. */
int slow_text_has_board(const char *name);

void slow_session_on_raw(struct slow_session *session, const struct slow_event *event,
                         struct slow_result *result);

#endif
