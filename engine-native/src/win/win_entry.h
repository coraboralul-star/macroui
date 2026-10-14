/* Windows resident process. Linux main does not include this.
   The playback tick does not advance a runner. Profile load is #47. */
#ifndef ENGINE_WIN_ENTRY_H
#define ENGINE_WIN_ENTRY_H

#include "output.h"

int engine_win_main(void);

/* Ups for applied software keys, through one SendInput per report.
   A board mode clears applied and does not call SendInput.
   Returns the number of events SendInput accepted. */
int engine_win_release_output(struct engine_output *out);

#endif
