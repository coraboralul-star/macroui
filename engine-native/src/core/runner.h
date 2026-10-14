/* Block steps and the runner. This file stays free of Win32.
   Advance does not allocate. The caller passes now and the QPC frequency.
   Tests use frequency 1000, so one tick is one millisecond.
   SendInput stays in src/win/output_win.c. main does not call this. */
#ifndef ENGINE_RUNNER_H
#define ENGINE_RUNNER_H

#include <stdint.h>

#include "output.h"

#define ENGINE_RUNNER_CAP 32
#define ENGINE_FRAME_CAP 16
#define ENGINE_HOLD_CAP 64
#define ENGINE_STEP_CAP 256
#define ENGINE_ID_CAP 64

#define ENGINE_MODE_ONCE 0
#define ENGINE_MODE_REPEAT 1
#define ENGINE_MODE_WHILE_HELD 2
#define ENGINE_MODE_TOGGLE 3
#define ENGINE_MODE_ON_RELEASE 4

#define ENGINE_STOP_NONE 0
#define ENGINE_STOP_NEXT_UP 1
#define ENGINE_STOP_FINISH 2

#define ENGINE_ACT_TAP 0
#define ENGINE_ACT_DOWN 1
#define ENGINE_ACT_UP 2

/* Numbers stay fixed. The play tick runs key, mouse, move, scroll, wait,
   randomWait, repeat, goto, and run.
   The rest are recognized so a later slice can fill them in.
   pressHold is not one of these: it expands to several steps. */
#define ENGINE_STEP_KEY 1
#define ENGINE_STEP_MOUSE 2
#define ENGINE_STEP_MOVE 3
#define ENGINE_STEP_SCROLL 4
#define ENGINE_STEP_WAIT 5
#define ENGINE_STEP_RANDOM_WAIT 6
#define ENGINE_STEP_REPEAT 7
#define ENGINE_STEP_HOLD_LOOP 8
#define ENGINE_STEP_SWAP_LOOP 9
#define ENGINE_STEP_BURST 10
#define ENGINE_STEP_IF_SHORT 11
#define ENGINE_STEP_ONCE_HELD 12
#define ENGINE_STEP_EDGE_HOLD 13
#define ENGINE_STEP_SCAN_WAIT 14
#define ENGINE_STEP_GOTO 15
#define ENGINE_STEP_RUN 16

/* goto where. Anything else, including an empty name, is screen. */
#define ENGINE_WHERE_SCREEN 0
#define ENGINE_WHERE_WINDOW 1
#define ENGINE_WHERE_CLIENT 2

/* key and mouse: action is down, up, or tap. Tap stores holdMs in a.
   wait stores ms in a. randomWait stores min in a and max in b.
   move stores x in a and y in b. scroll stores dy in a.
   repeat stores the count in a, the frame release stop in b, and the child
   span in first and count. A negative repeat count becomes 0, which loops.
   goto stores x in a and y in b, where in action, and the duration in count.
   0 does not wait. A missing duration is 15 when the step is built.
   run stores the catalog index in a. A negative index is a missing target. */
struct engine_step {
    uint8_t type;
    uint8_t action;
    uint16_t key;
    int32_t a;
    int32_t b;
    uint32_t first;
    uint32_t count;
};

struct engine_frame {
    uint32_t first;
    uint32_t count;
    uint32_t index;
    int32_t repeats_left;
    int release_stop;
};

struct engine_pulse {
    uint16_t id;
    int64_t until;
};

struct engine_runner {
    char id[ENGINE_ID_CAP];
    double speed;
    int64_t freq;
    int64_t started_at;
    int64_t wait_until;
    const struct engine_step *steps;
    uint32_t step_count;
    struct engine_frame stack[ENGINE_FRAME_CAP];
    int stack_count;
    uint16_t holds[ENGINE_HOLD_CAP];
    int hold_count;
    struct engine_pulse pulses[ENGINE_HOLD_CAP];
    int pulse_count;
    uint16_t pressed_now[ENGINE_HOLD_CAP];
    int pressed_count;
    int32_t move_x;
    int32_t move_y;
    int32_t wheel;
    int has_goto;
    int did_goto;
    int32_t goto_x;
    int32_t goto_y;
    int mode;
    int release_stop;
    int stopping;
    int saw_up;
    int released_now;
    int scan_wait;
    int done;
    int paused;
    uint16_t trigger;
};

/* top_count 0 means the whole pool is the top frame.
   A nested repeat sets top_count to the parent span. Child indexes are
   absolute indexes into steps. speed is raised to 0.05. A non-finite
   speed becomes 1 first. release_stop other than finish is nextUp. */
struct engine_macro {
    char id[ENGINE_ID_CAP];
    const struct engine_step *steps;
    uint32_t step_count;
    uint32_t top_count;
    double speed;
    int mode;
    int32_t repeat_count;
    int busy;
    int exclusive;
    int release_stop;
    int basic;
    int trigger_is_key;
    int32_t gap_ms;
    uint16_t trigger;
};

typedef int64_t (*engine_roll_fn)(int64_t lo, int64_t hi, void *ctx);

/* Foreground snapshot for goto. The core does not query a window.
   have_mouse 0 is a mouse at 0, 0. A missing window or client rect
   makes that goto a no-op. */
struct engine_pointer {
    int have_mouse;
    int32_t mouse_x;
    int32_t mouse_y;
    int have_window;
    int32_t window_x;
    int32_t window_y;
    int have_client;
    int32_t client_x;
    int32_t client_y;
};

/* The play struct is large. Keep it off a deep stack.
   freq 0 is stored as 1000 when a runner starts. dpi 0 is left 0.
   macros is the caller's catalog. run indexes it. The pointer is the
   tick's mouse and window origin. */
struct engine_play {
    struct engine_runner runners[ENGINE_RUNNER_CAP];
    int runner_count;
    struct engine_runner pending[ENGINE_RUNNER_CAP];
    int pending_count;
    struct engine_step arena[ENGINE_STEP_CAP];
    uint32_t arena_count;
    struct engine_output output;
    struct engine_report last;
    int64_t freq;
    uint32_t dpi;
    engine_roll_fn roll;
    void *roll_ctx;
    const struct engine_macro *macros;
    int macro_count;
    struct engine_pointer pointer;
    int have_pointer;
};

void engine_macro_init(struct engine_macro *macro);

/* 1 when name resolved. An empty or unknown name writes 0 and returns 0.
   Alias and SafeKey run here. Advance does not look up a name. */
int engine_key_id(const char *name, uint16_t *id);

/* 0 for an unknown editor type, including pressHold. */
int engine_step_kind(const char *name);

/* window and client stay. A null, empty, or other name is screen. */
int engine_goto_where(const char *name);

/* 1 when a top-level step loops itself. Nested steps are the caller's count. */
int engine_has_hold_loop(const struct engine_step *steps, uint32_t count);

/* Play-mode repeat count below 1 becomes 1. A hold loop forces 1.
   whileHeld and toggle are 0 unless a hold loop is present. */
int engine_top_repeats(int mode, int32_t repeat_count, int hold_loop);

/* 4 when the trigger is rewritten, 0 when it is left alone, -1 if out is short.
   Left and right click are left alone. gap below 0 becomes 0. The hold is 18. */
int engine_basic_rewrite(uint16_t trigger, int trigger_is_key, int32_t gap_ms,
                         struct engine_step *out, int cap);

void engine_play_init(struct engine_play *play);
void engine_play_set_roll(struct engine_play *play, engine_roll_fn roll, void *ctx);

/* The catalog stays with the caller. A null list or a count below 1 clears it.
   run reads it during Advance. Indexes are resolved before the step is stored. */
void engine_play_set_macros(struct engine_play *play, const struct engine_macro *macros,
                            int count);

/* Copies the snapshot. A null pointer clears it. */
void engine_play_set_pointer(struct engine_play *play, const struct engine_pointer *pointer);

/* mode_override below 0 keeps the macro mode. restart 0 leaves a live id alone.
   Returns 1 when a runner was queued. A full list queues nothing and stops nothing. */
int engine_play_start(struct engine_play *play, const struct engine_macro *macro,
                      int mode_override, int restart);

void engine_play_flush(struct engine_play *play);
void engine_play_advance(struct engine_play *play, int64_t now);

/* Runs one runner. run does nothing here: there is no catalog.
   goto uses a screen point and a mouse at 0, 0.
   engine_play_advance is the path that starts run and reads the pointer. */
void engine_runner_advance(struct engine_runner *runner, int64_t now,
                           engine_roll_fn roll, void *roll_ctx);

/* Holds, then pulses whose deadline is still ahead. Skips a done or paused runner. */
int engine_play_desired(const struct engine_play *play, int64_t now,
                        uint16_t *out, int cap);

/* Sums move and wheel from runners that are still in the list, including a
   runner Advance just finished. A goto replaces that sum with the absolute
   point. The last goto in the list wins. Then writes one report. Returns 1
   when the image should be sent. Does not remove runners and does not call
   StopAll. */
int engine_play_publish(struct engine_play *play, int64_t now);

void engine_play_remove_done(struct engine_play *play);
void engine_play_stop(struct engine_play *play, const char *id);
void engine_play_stop_all(struct engine_play *play);
void engine_play_ask_stop(struct engine_play *play, const char *id);

/* Remove, flush, advance, flush, publish, then remove again.
   A report overflow calls StopAll after that publish. */
void engine_play_tick(struct engine_play *play, int64_t now);

int engine_play_alive(const struct engine_play *play);
struct engine_runner *engine_play_runner(struct engine_play *play, int index);

#endif
