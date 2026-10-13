/* Timer, hooks, SendInput, the pipe, and the mutex land in this file.
   This issue only links it. See docs/engine/architecture.md. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int engine_win_layer_present(void)
{
    return sizeof(INPUT) >= 40u ? 1 : 0;
}
