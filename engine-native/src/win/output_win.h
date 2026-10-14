/* One SendInput for a report image. Windows only.
   The resident process calls this to release keys on exit.
   The echo value is on every INPUT. */
#ifndef ENGINE_OUTPUT_WIN_H
#define ENGINE_OUTPUT_WIN_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "output.h"

/* Fills INPUT from a send plan. Absolute pixels are normalized here.
   Returns the number written. */
int engine_win_fill(const struct engine_send_op *ops, int count,
                    INPUT *out, int cap);

/* Plans the image, then one SendInput. Returns the number of events accepted.
   An empty image returns 0 and does not call SendInput. */
int engine_win_send_report(const struct engine_report *report, int swap_clicks);

#endif
