#include "clock.h"

#include <stdio.h>

static double quiet_nan(void)
{
    volatile double zero = 0.0;
    return zero / zero;
}

static int fails;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "fail %s\n", name);
        fails++;
    }
}

static void check_round(void)
{
    expect(engine_round_half_away(0.5) == 1, "round 0.5");
    expect(engine_round_half_away(1.5) == 2, "round 1.5");
    expect(engine_round_half_away(2.5) == 3, "round 2.5");
    expect(engine_round_half_away(-0.5) == -1, "round -0.5");
    expect(engine_round_half_away(-1.5) == -2, "round -1.5");
    expect(engine_round_half_away(-2.5) == -3, "round -2.5");
    expect(engine_round_half_away(0.0) == 0, "round 0");
    expect(engine_round_half_away(0.49) == 0, "round 0.49");
    expect(engine_round_half_away(-0.49) == 0, "round -0.49");
    expect(engine_round_half_away(1.49) == 1, "round 1.49");
    expect(engine_round_half_away(-1.49) == -1, "round -1.49");
    expect(engine_round_half_away(quiet_nan()) == 0, "round nan");
}

static void check_scale(void)
{
    expect(engine_scale_wait_ms(5.0, 1.0) == 5, "scale 5/1");
    expect(engine_scale_wait_ms(5.0, 2.0) == 3, "scale 5/2");
    expect(engine_scale_wait_ms(1.0, 3.0) == 1, "scale 1/3 floor");
    expect(engine_scale_wait_ms(0.0, 1.0) == 1, "scale 0");
    expect(engine_scale_wait_ms(0.5, 1.0) == 1, "scale 0.5");
    expect(engine_scale_wait_ms(1.5, 1.0) == 2, "scale 1.5");
    expect(engine_scale_wait_ms(1.0, 0.0) == 1, "scale speed 0");
    expect(engine_scale_wait_ms(1.0, -1.0) == 1, "scale speed negative");
    expect(engine_scale_wait_ms(-4.0, 1.0) == 1, "scale ms negative");
    expect(engine_scale_wait_ms(1.0, 0.5) == 2, "scale speed 0.5");
    expect(engine_scale_wait_ms(1.0, 0.05) == 20, "scale speed floor input");
}

static void check_ticks(void)
{
    const int64_t freq = 10000000;

    expect(engine_ms_to_ticks(5, freq) == 50000, "ticks 5 ms");
    expect(engine_ms_to_ticks(1, freq) == 10000, "ticks 1 ms");
    expect(engine_ms_to_ticks(8, freq) == 80000, "ticks 8 ms");
    expect(engine_ms_to_ticks(16, freq) == 160000, "ticks 16 ms");
    expect(engine_ms_to_ticks(1, 999) == 0, "ticks truncate");
    expect(engine_ms_to_ticks(1, 1000) == 1, "ticks 1 at 1000");
    expect(engine_ms_to_ticks(0, freq) == 0, "ticks scaled 0");
    expect(engine_ms_to_ticks(5, 0) == 0, "ticks freq 0");
    expect(engine_ms_to_ticks(3, 10000) == 30, "ticks 3 at 10000");
}

static void check_deadline(void)
{
    int64_t due = engine_arm_deadline(1000, 50);

    expect(due == 1050, "arm");
    expect(engine_deadline_reached(1050, due) == 1, "reached equal");
    expect(engine_deadline_reached(1049, due) == 0, "reached early");
    expect(engine_arm_deadline(-5, 10) == 10, "arm negative now");
    expect(engine_arm_deadline(20, -4) == 20, "arm negative delta");
    expect(engine_arm_deadline(INT64_MAX - 5, 10) == INT64_MAX, "arm saturate");
}

static void check_span(void)
{
    struct engine_span span = engine_wait_span(10.0, 20.0);
    struct engine_span swapped = engine_wait_span(20.0, 10.0);
    struct engine_span negative = engine_wait_span(-3.0, 4.0);
    struct engine_span halves = engine_wait_span(1.5, 2.5);
    struct engine_span both_neg = engine_wait_span(-1.5, -2.5);
    struct engine_span same = engine_wait_span(7.0, 7.0);

    expect(span.lo == 10 && span.hi == 20, "span order");
    expect(swapped.lo == 10 && swapped.hi == 20, "span swap");
    expect(negative.lo == 0 && negative.hi == 4, "span clamp");
    expect(halves.lo == 2 && halves.hi == 3, "span halves");
    expect(both_neg.lo == 0 && both_neg.hi == 0, "span both negative");
    expect(engine_apply_roll(span, 15) == 15, "roll inside");
    expect(engine_apply_roll(span, 5) == 10, "roll below");
    expect(engine_apply_roll(span, 25) == 20, "roll above");
    expect(engine_apply_roll(same, 7) == 7, "roll equal");
    expect(engine_apply_roll(both_neg, 3) == 0, "roll clamped span");
    expect(engine_scale_wait_ms((double)engine_apply_roll(both_neg, 0), 1.0) == 1,
           "zero roll still waits 1");
}

static void check_pace(void)
{
    const int64_t freq = 10000000;
    const int64_t now = 1000000;
    struct engine_pace pace;

    pace = engine_pace(0, 0, 0, now, now + 1, freq);
    expect(pace.wait_ticks == 160000 && pace.wake_on_input == 0, "disarmed idle");

    pace = engine_pace(1, 0, 0, now, now + 1, freq);
    expect(pace.wait_ticks == 80000 && pace.wake_on_input == 1, "armed idle");

    pace = engine_pace(0, 1, 0, now, now + 500000, freq);
    expect(pace.wait_ticks == 80000 && pace.wake_on_input == 1, "cap 8 ms");

    pace = engine_pace(1, 1, 0, now, now + 30000, freq);
    expect(pace.wait_ticks == 30000 && pace.wake_on_input == 1, "3 ms slice");

    pace = engine_pace(1, 1, 0, now, now, freq);
    expect(pace.wait_ticks == 10000 && pace.wake_on_input == 1, "due waits 1 ms");

    pace = engine_pace(1, 1, 0, now, now - 1, freq);
    expect(pace.wait_ticks == 10000 && pace.wake_on_input == 1, "overdue waits 1 ms");

    pace = engine_pace(0, 0, 1, now, now, freq);
    expect(pace.wait_ticks == 10000 && pace.wake_on_input == 1, "pending is work");

    pace = engine_pace(1, 0, 1, now, now + 80000, freq);
    expect(pace.wait_ticks == 80000 && pace.wake_on_input == 1, "pending not idle");

    pace = engine_pace(1, 1, 0, now, now + 80000, freq);
    expect(pace.wait_ticks == 80000, "exact 8 ms");

    pace = engine_pace(1, 1, 0, now, now + 10000, freq);
    expect(pace.wait_ticks == 10000, "exact 1 ms");

    pace = engine_pace(1, 1, 0, now, now + 10001, freq);
    expect(pace.wait_ticks == 10001, "slice stays in ticks");
}

int main(void)
{
    check_round();
    check_scale();
    check_ticks();
    check_deadline();
    check_span();
    check_pace();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
