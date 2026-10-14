/* High-resolution wait. Windows only.
   No timeBeginPeriod, no MMCSS, no completion routine, no message loop. */
#ifndef ENGINE_CLOCK_WIN_H
#define ENGINE_CLOCK_WIN_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdint.h>

#include "clock.h"

struct engine_win_clock {
    HANDLE timer;
    int64_t qpc_freq;
};

enum engine_wake_reason {
    ENGINE_WAKE_TIMER = 1,
    ENGINE_WAKE_HOOK = 2,
    ENGINE_WAKE_IPC = 3
};

/* 0 on success. The timer is unnamed and high-resolution. */
int engine_win_clock_open(struct engine_win_clock *clock);
void engine_win_clock_close(struct engine_win_clock *clock);
int64_t engine_win_clock_now(const struct engine_win_clock *clock);

/* hook_wake is included only when wake_on_input is set.
   ipc_wake is included whenever it is non-NULL, including the 16 ms slice. */
int engine_win_clock_wait(struct engine_win_clock *clock, int64_t wait_ticks,
                          int wake_on_input, HANDLE hook_wake, HANDLE ipc_wake,
                          int *reason);

struct engine_slice_input {
    int armed;
    int running;
    int pending;
    int64_t next_deadline;
};

/* One playback slice: read QPC, pace, wait. The caller loops. */
int engine_win_clock_slice(struct engine_win_clock *clock,
                           const struct engine_slice_input *in,
                           HANDLE hook_wake, HANDLE ipc_wake, int *reason);

#endif
