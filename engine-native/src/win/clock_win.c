/* The playback wait. The hook thread already pumps, so this thread does not.
   timeBeginPeriod and AvSetMmThreadCharacteristicsW are not called. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "clock_win.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* TIMER_ALL_ACCESS. The timer is unnamed, so the name pointer stays null. */
#define ENGINE_TIMER_ACCESS 0x001F0003u

static int64_t ticks_to_100ns(int64_t wait_ticks, int64_t qpc_freq)
{
    int64_t due;

    if (wait_ticks <= 0 || qpc_freq <= 0)
        return 1;
    if (wait_ticks > INT64_MAX / 10000000)
        wait_ticks = INT64_MAX / 10000000;
    due = (wait_ticks * 10000000) / qpc_freq;
    if (due < 1)
        due = 1;
    if (due > INT64_MAX / 2)
        due = INT64_MAX / 2;
    return due;
}

int engine_win_clock_open(struct engine_win_clock *clock)
{
    LARGE_INTEGER freq;
    HANDLE timer;

    if (!clock)
        return 1;
    clock->timer = NULL;
    clock->qpc_freq = 0;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart < 1000)
        return 1;
    timer = CreateWaitableTimerExW(NULL, NULL,
                                   CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                   ENGINE_TIMER_ACCESS);
    if (!timer)
        return 1;
    clock->timer = timer;
    clock->qpc_freq = freq.QuadPart;
    return 0;
}

void engine_win_clock_close(struct engine_win_clock *clock)
{
    if (!clock)
        return;
    if (clock->timer)
        CloseHandle(clock->timer);
    clock->timer = NULL;
    clock->qpc_freq = 0;
}

int64_t engine_win_clock_now(const struct engine_win_clock *clock)
{
    LARGE_INTEGER now;

    (void)clock;
    if (!QueryPerformanceCounter(&now))
        return 0;
    return now.QuadPart;
}

int engine_win_clock_wait(struct engine_win_clock *clock, int64_t wait_ticks,
                          int wake_on_input, HANDLE hook_wake, HANDLE ipc_wake,
                          int *reason)
{
    HANDLE handles[3];
    DWORD count = 0;
    LARGE_INTEGER due;
    DWORD result;
    int hook_index = -1;
    int ipc_index = -1;

    if (!clock || !clock->timer || !reason || wait_ticks <= 0)
        return 1;

    handles[count++] = clock->timer;
    if (wake_on_input && hook_wake) {
        hook_index = (int)count;
        handles[count++] = hook_wake;
    }
    if (ipc_wake) {
        if (hook_index >= 0 && hook_wake == ipc_wake)
            ipc_index = hook_index;
        else {
            ipc_index = (int)count;
            handles[count++] = ipc_wake;
        }
    }

    due.QuadPart = -ticks_to_100ns(wait_ticks, clock->qpc_freq);
    if (!SetWaitableTimer(clock->timer, &due, 0, NULL, NULL, FALSE))
        return 1;

    result = WaitForMultipleObjects(count, handles, FALSE, INFINITE);
    CancelWaitableTimer(clock->timer);
    if (result == WAIT_OBJECT_0) {
        *reason = ENGINE_WAKE_TIMER;
        return 0;
    }
    if (hook_index >= 0 && result == WAIT_OBJECT_0 + (DWORD)hook_index) {
        *reason = ENGINE_WAKE_HOOK;
        return 0;
    }
    if (ipc_index >= 0 && result == WAIT_OBJECT_0 + (DWORD)ipc_index) {
        *reason = ENGINE_WAKE_IPC;
        return 0;
    }
    return 1;
}

int engine_win_clock_slice(struct engine_win_clock *clock,
                           const struct engine_slice_input *in,
                           HANDLE hook_wake, HANDLE ipc_wake, int *reason)
{
    int64_t now;
    struct engine_pace pace;

    if (!clock || !clock->timer || !in || !reason)
        return 1;
    now = engine_win_clock_now(clock);
    pace = engine_pace(in->armed, in->running, in->pending, now,
                       in->next_deadline, clock->qpc_freq);
    if (pace.wait_ticks <= 0)
        return 1;
    return engine_win_clock_wait(clock, pace.wait_ticks, pace.wake_on_input,
                                 hook_wake, ipc_wake, reason);
}
