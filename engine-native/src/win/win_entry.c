/* Hooks, SendInput, the pipe, and the mutex land later.
   The high-res wait is clock_win.c. See docs/engine/architecture.md. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int engine_win_layer_present(void)
{
    return sizeof(INPUT) >= 40u ? 1 : 0;
}
