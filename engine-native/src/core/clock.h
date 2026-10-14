/* Pace and the half-away line. This file stays free of Win32.
   The rules are docs/engine/architecture.md. The wait is src/win/clock_win.c. */
#ifndef ENGINE_CLOCK_H
#define ENGINE_CLOCK_H

#include <stdint.h>

/* Half away from zero. Round(0.5) is 1, Round(2.5) is 3, Round(-1.5) is -2.
   Non-finite input is 0. A magnitude that does not fit in int64 saturates. */
int64_t engine_round_half_away(double x);

/* scaled = floor((ms / speed) + 0.5), then raised to 1.
   ms is non-negative. speed was already raised to 0.05.
   A non-positive or non-finite speed returns 1. */
int64_t engine_scale_wait_ms(double ms, double speed);

/* delta = scaled_ms * qpc_freq / 1000, toward zero.
   A non-positive argument returns 0. */
int64_t engine_ms_to_ticks(int64_t scaled_ms, int64_t qpc_freq);

/* now + delta, saturating. A negative side is treated as 0. */
int64_t engine_arm_deadline(int64_t now, int64_t delta_ticks);

/* True when now is at or past the absolute deadline. */
int engine_deadline_reached(int64_t now, int64_t deadline);

/* Inclusive bounds after half-away rounding, a clamp at 0, and a swap. */
struct engine_span {
    int64_t lo;
    int64_t hi;
};

struct engine_span engine_wait_span(double min_ms, double max_ms);

/* The caller injects the roll. The result stays inside the span. */
int64_t engine_apply_roll(struct engine_span span, int64_t roll);

/* One slice after a tick. next_deadline is the soonest absolute QPC tick.
   A due or missing deadline is any value <= now. */
struct engine_pace {
    int64_t wait_ticks;
    int wake_on_input;
};

struct engine_pace engine_pace(int armed, int running, int pending,
                               int64_t now, int64_t next_deadline,
                               int64_t qpc_freq);

#endif
