/* Report fill, the applied diff, and the two send plans.
   The image stays logical. swapClicks is applied in the plans, not in the image.
   Software is one SendInput in src/win/output_win.c. The shell writes K and M.
   This file does not call SendInput, open a port, or touch the hook. */
#ifndef ENGINE_OUTPUT_H
#define ENGINE_OUTPUT_H

#include <stdint.h>

#include "hook.h"
#include "report.h"

#define ENGINE_OUTPUT_SOFTWARE 0
#define ENGINE_OUTPUT_RP2040 1
#define ENGINE_OUTPUT_RP2350 2

#define ENGINE_APPLIED_CAP 64

#define ENGINE_SEND_KEY 1
#define ENGINE_SEND_BUTTON 2
#define ENGINE_SEND_WHEEL 3
#define ENGINE_SEND_MOVE 4
#define ENGINE_SEND_ABS 5
#define ENGINE_SEND_CAP 66

#define ENGINE_SEND_EXTENDED 0x0001u
#define ENGINE_SEND_KEYUP 0x0002u

#define ENGINE_MOUSE_LEFTDOWN 0x0002u
#define ENGINE_MOUSE_LEFTUP 0x0004u
#define ENGINE_MOUSE_RIGHTDOWN 0x0008u
#define ENGINE_MOUSE_RIGHTUP 0x0010u
#define ENGINE_MOUSE_MIDDLEDOWN 0x0020u
#define ENGINE_MOUSE_MIDDLEUP 0x0040u
#define ENGINE_MOUSE_XDOWN 0x0080u
#define ENGINE_MOUSE_XUP 0x0100u
#define ENGINE_MOUSE_WHEEL 0x0800u
#define ENGINE_WHEEL_DELTA 120

#define ENGINE_BOARD_KEY 1
#define ENGINE_BOARD_MOVE 2
#define ENGINE_BOARD_STEER 3
#define ENGINE_BOARD_CAP 70

struct engine_output {
    int mode;
    int swap_clicks;
    int mouse_ok;
    int32_t mouse_x;
    int32_t mouse_y;
    uint16_t applied[ENGINE_APPLIED_CAP];
    int applied_count;
};

struct engine_send_op {
    int kind;
    uint16_t id;
    uint16_t vk;
    uint32_t flags;
    uint32_t data;
    int32_t x;
    int32_t y;
};

struct engine_board_op {
    int kind;
    int usage;
    int down;
    int bits;
    int32_t x;
    int32_t y;
    int32_t wheel;
};

struct engine_report_echo {
    uint16_t id;
    uint8_t down;
};

void engine_output_init(struct engine_output *out);
void engine_output_set_swap(struct engine_output *out, int on);

/* Unknown values are stored as software.
   On a change, release is the ups for the previous mode, applied is cleared,
   and the remembered point is forgotten. *sent_mode is that previous mode.
   *rebind is 1. The caller sends release, then clears swallow and publishes it.
   This function does not send and does not call the hook.
   Returns 1 when the mode changed. */
int engine_output_set_mode(struct engine_output *out, int mode,
                           struct engine_report *release, int *rebind,
                           int *sent_mode);

/* Ups for every applied id that fits, then those ids leave applied.
   An up that does not fit stays applied and sets the overflow bit.
   The caller calls StopAll when that bit is set. This function does not. */
int engine_output_release(struct engine_output *out, struct engine_report *release);

/* Downs are desired ids missing from applied, in desired order.
   extra is a repeat down: still desired, not a mouse button, not already a down.
   Ups are applied ids missing from desired, in applied order.
   The image is logical. More than 32 downs or ups sets overflow, writes the
   first 32, and leaves an unsent up applied. Every down is marked applied.
   Returns 1 when the image should be sent. */
int engine_output_publish(struct engine_output *out,
                          const uint16_t *desired, int desired_count,
                          const uint16_t *extra, int extra_count,
                          int32_t dx, int32_t dy, int32_t wheel,
                          int absolute, int32_t ax, int32_t ay, uint32_t dpi,
                          struct engine_report *report);

/* Software absolute stores the point. A relative move nudges a stored point. */
void engine_output_remember_mouse(struct engine_output *out, int32_t x, int32_t y);
void engine_output_forget_mouse(struct engine_output *out);
void engine_output_note_move(struct engine_output *out, const struct engine_report *report);

/* LButton and RButton swap. Every other id is unchanged. */
uint16_t engine_swap_button(uint16_t id, int swap_clicks);

/* 0..4, or -1. */
int engine_mouse_bit(uint16_t id);

/* HID usage for a named id. 0 means the shell skips it.
   Mouse buttons are bits, not usages. An id engine_key_name omits is 0. */
int engine_hid_usage(uint16_t id);

/* One publish, in send order: downs, ups, wheel, then the move.
   Button ids are swapped when swap_clicks is set. The image is not.
   *count is the number written. Returns 0 when there is nothing to send. */
int engine_report_send(const struct engine_report *report, int swap_clicks,
                       struct engine_send_op *ops, int cap, int *count);

/* Board order from docs/engine/ipc-protocol.md. Wheel is already negated.
   Relative dx and dy are scaled from the image dpi. Absolute points are not.
   Returns the mouse bits after the plan. *count is the number written. */
/* A board move in pixels times dpi / 96, rounded half away. 0 and 96 keep it. */
int32_t engine_board_scale(int32_t value, uint32_t dpi);

int engine_report_board(const struct engine_report *report, int swap_clicks, int bits,
                        struct engine_board_op *ops, int cap, int *count);

/* Logical ids, downs then ups. Software does not push these.
   The caller adds 150 ms and calls engine_echo_push. Returns the number written. */
int engine_report_echoes(const struct engine_report *report,
                         struct engine_report_echo *out, int cap);

#endif
