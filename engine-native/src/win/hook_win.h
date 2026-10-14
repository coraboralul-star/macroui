/* Low-level hooks. Windows only.
   Call run on the hook thread. The resident process starts that thread.
   Unhook before installing again. The callback does not play a macro. */
#ifndef ENGINE_HOOK_WIN_H
#define ENGINE_HOOK_WIN_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdatomic.h>
#include <stdint.h>

#include "hook.h"

struct engine_win_hook {
    struct engine_hook_state *state;
    HANDLE wake;
    HHOOK key;
    HHOOK mouse;
    atomic_uint thread_id;
    atomic_uint generation;
};

/* 0 on success. Creates the wake event. Does not install a hook. */
int engine_win_hook_open(struct engine_win_hook *hook, struct engine_hook_state *state);
void engine_win_hook_close(struct engine_win_hook *hook);

/* Keyboard hook only. The mouse hook waits for a raise while need_mouse is set. */
int engine_win_hook_install(struct engine_win_hook *hook);
void engine_win_hook_uninstall(struct engine_win_hook *hook);

/* Between messages, on the hook thread. Never from the callback. */
void engine_win_hook_drain(struct engine_win_hook *hook);

/* Blocks in GetMessage until stop is set. Pumps so a removed hook can be put back. */
void engine_win_hook_run(struct engine_win_hook *hook);

/* Wakes GetMessage. 0 on success. */
int engine_win_hook_kick(struct engine_win_hook *hook);

/* Parse one struct. Does not call CallNextHookEx.
   Returns 1 only when the keyboard map says swallow. The mouse call returns 0. */
int engine_win_on_key(struct engine_hook_state *st, int nCode, uintptr_t msg,
                      const KBDLLHOOKSTRUCT *info, struct engine_hook_decision *out);
int engine_win_on_mouse(struct engine_hook_state *st, int nCode, uintptr_t msg,
                        const MSLLHOOKSTRUCT *info, struct engine_hook_decision *out);

#endif
