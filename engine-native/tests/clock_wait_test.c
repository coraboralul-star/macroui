/* Functional wait. This is not a jitter report. */

#include "clock_win.h"

#include <stdio.h>

static int fail(const char *name)
{
    fprintf(stderr, "fail %s\n", name);
    return 1;
}

int main(void)
{
    struct engine_win_clock clock;
    struct engine_slice_input slice;
    HANDLE hook;
    HANDLE ipc;
    int reason = 0;
    int64_t before;
    int64_t after;
    int64_t elapsed;
    int64_t one;
    int64_t ceiling;

    if (engine_win_clock_open(&clock) != 0)
        return fail("open");
    if (clock.qpc_freq < 1000)
        return fail("freq");

    one = engine_ms_to_ticks(1, clock.qpc_freq);
    ceiling = engine_ms_to_ticks(1000, clock.qpc_freq);
    if (one <= 0)
        return fail("one ms ticks");

    before = engine_win_clock_now(&clock);
    if (before <= 0)
        return fail("now");
    if (engine_win_clock_wait(&clock, one, 0, NULL, NULL, &reason) != 0)
        return fail("wait 1 ms");
    after = engine_win_clock_now(&clock);
    elapsed = after - before;
    printf("1 ms wait elapsed %.3f ms\n",
           (double)elapsed * 1000.0 / (double)clock.qpc_freq);
    if (reason != ENGINE_WAKE_TIMER)
        return fail("1 ms reason");
    if (elapsed > ceiling)
        return fail("1 ms hung");
    if (elapsed * 10 < one)
        return fail("1 ms returned early");

    hook = CreateEventW(NULL, FALSE, TRUE, NULL);
    if (!hook)
        return fail("hook event");
    before = engine_win_clock_now(&clock);
    reason = 0;
    if (engine_win_clock_wait(&clock, one, 0, hook, NULL, &reason) != 0)
        return fail("ignore hook");
    after = engine_win_clock_now(&clock);
    CloseHandle(hook);
    if (reason != ENGINE_WAKE_TIMER)
        return fail("ignore hook reason");
    if ((after - before) * 10 < one)
        return fail("ignore hook returned early");

    hook = CreateEventW(NULL, FALSE, TRUE, NULL);
    if (!hook)
        return fail("hook wake event");
    reason = 0;
    before = engine_win_clock_now(&clock);
    if (engine_win_clock_wait(&clock, engine_ms_to_ticks(16, clock.qpc_freq),
                              1, hook, NULL, &reason) != 0)
        return fail("hook wake");
    after = engine_win_clock_now(&clock);
    CloseHandle(hook);
    if (reason != ENGINE_WAKE_HOOK)
        return fail("hook wake reason");
    if (after - before > engine_ms_to_ticks(50, clock.qpc_freq))
        return fail("hook wake slow");

    ipc = CreateEventW(NULL, FALSE, TRUE, NULL);
    if (!ipc)
        return fail("ipc event");
    slice.armed = 0;
    slice.running = 0;
    slice.pending = 0;
    slice.next_deadline = engine_win_clock_now(&clock);
    reason = 0;
    before = engine_win_clock_now(&clock);
    if (engine_win_clock_slice(&clock, &slice, NULL, ipc, &reason) != 0)
        return fail("disarmed ipc");
    after = engine_win_clock_now(&clock);
    CloseHandle(ipc);
    if (reason != ENGINE_WAKE_IPC)
        return fail("disarmed ipc reason");
    if (after - before > engine_ms_to_ticks(50, clock.qpc_freq))
        return fail("disarmed ipc slow");

    engine_win_clock_close(&clock);
    printf("ok\n");
    return 0;
}
