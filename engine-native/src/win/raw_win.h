/* Raw Input for middle, mouse 4 and mouse 5. Windows only.
   A low-level hook that swallows a mouse button does not stop Raw Input, so
   this sees the real up even when the hook lost it. It has its own thread:
   a fast mouse sends thousands of WM_INPUT a second, and the hook thread must
   stay free for its callbacks. The board and injected input are not counted. */
#ifndef ENGINE_RAW_WIN_H
#define ENGINE_RAW_WIN_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdatomic.h>

#include "hook.h"

#define ENGINE_RAW_OFF 0
#define ENGINE_RAW_ON 1
#define ENGINE_RAW_FAILED (-1)

struct engine_win_raw {
    struct engine_raw_mouse *tally;
    HWND window;
    atomic_uint thread_id;
    /* OFF, ON or FAILED, for the log. Written by the Raw Input thread. */
    atomic_int status;
};

void engine_win_raw_open(struct engine_win_raw *raw, struct engine_raw_mouse *tally);

/* The thread body. Blocks in GetMessage until stop. */
void engine_win_raw_run(struct engine_win_raw *raw);

/* Register while a config has a middle or side trigger, so no mouse traffic
   reaches this thread otherwise. 0 when the request was posted. */
int engine_win_raw_want(struct engine_win_raw *raw, int on);

/* 0 when the quit was posted. */
int engine_win_raw_stop(struct engine_win_raw *raw);

/* 1 when the device name is the MacroUI board, VID_CAFE&PID_4004. */
int engine_win_raw_is_board(const wchar_t *name);

#endif
