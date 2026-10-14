#include "clock.h"

#include <math.h>

int64_t engine_round_half_away(double x)
{
    double shifted;

    if (!isfinite(x))
        return 0;
    /* 2^63 is the first double that does not fit in a positive int64. */
    if (x >= 9223372036854775808.0)
        return INT64_MAX;
    if (x <= -9223372036854775808.0)
        return INT64_MIN;
    if (x >= 0.0)
        shifted = floor(x + 0.5);
    else
        shifted = ceil(x - 0.5);
    if (shifted >= 9223372036854775808.0)
        return INT64_MAX;
    if (shifted <= -9223372036854775808.0)
        return INT64_MIN;
    return (int64_t)shifted;
}

int64_t engine_scale_wait_ms(double ms, double speed)
{
    double scaled;

    if (!isfinite(ms) || ms < 0.0)
        ms = 0.0;
    if (!isfinite(speed) || speed <= 0.0)
        return 1;
    scaled = floor((ms / speed) + 0.5);
    if (!isfinite(scaled) || scaled < 1.0)
        return 1;
    if (scaled >= 9223372036854775808.0)
        return INT64_MAX;
    return (int64_t)scaled;
}

int64_t engine_ms_to_ticks(int64_t scaled_ms, int64_t qpc_freq)
{
    if (scaled_ms <= 0 || qpc_freq <= 0)
        return 0;
    if (qpc_freq > INT64_MAX / scaled_ms)
        return INT64_MAX;
    return (scaled_ms * qpc_freq) / 1000;
}

int64_t engine_arm_deadline(int64_t now, int64_t delta_ticks)
{
    if (now < 0)
        now = 0;
    if (delta_ticks < 0)
        delta_ticks = 0;
    if (now > INT64_MAX - delta_ticks)
        return INT64_MAX;
    return now + delta_ticks;
}

int engine_deadline_reached(int64_t now, int64_t deadline)
{
    return now >= deadline;
}

struct engine_span engine_wait_span(double min_ms, double max_ms)
{
    struct engine_span span;

    span.lo = engine_round_half_away(min_ms);
    span.hi = engine_round_half_away(max_ms);
    if (span.lo < 0)
        span.lo = 0;
    if (span.hi < 0)
        span.hi = 0;
    if (span.lo > span.hi) {
        int64_t hold = span.lo;
        span.lo = span.hi;
        span.hi = hold;
    }
    return span;
}

int64_t engine_apply_roll(struct engine_span span, int64_t roll)
{
    if (span.lo > span.hi) {
        int64_t hold = span.lo;
        span.lo = span.hi;
        span.hi = hold;
    }
    if (roll < span.lo)
        return span.lo;
    if (roll > span.hi)
        return span.hi;
    return roll;
}

struct engine_pace engine_pace(int armed, int running, int pending,
                               int64_t now, int64_t next_deadline,
                               int64_t qpc_freq)
{
    struct engine_pace pace;
    int64_t one = engine_ms_to_ticks(1, qpc_freq);
    int64_t eight = engine_ms_to_ticks(8, qpc_freq);
    int64_t remain;

    if (!running && !pending) {
        if (!armed) {
            pace.wait_ticks = engine_ms_to_ticks(16, qpc_freq);
            pace.wake_on_input = 0;
            return pace;
        }
        pace.wait_ticks = eight;
        pace.wake_on_input = 1;
        return pace;
    }

    if (next_deadline <= now)
        remain = 0;
    else
        remain = next_deadline - now;
    if (remain < one)
        remain = one;
    if (remain > eight)
        remain = eight;
    pace.wait_ticks = remain;
    pace.wake_on_input = 1;
    return pace;
}
