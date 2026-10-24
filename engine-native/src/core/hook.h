/* Hooks, trigger bits, and the watchdog decision. This file stays free of Win32.
   The rules are docs/engine/architecture.md. The install is src/win/hook_win.c.
   The state is large. Keep it off a deep stack. */
#ifndef ENGINE_HOOK_H
#define ENGINE_HOOK_H

#include <stdatomic.h>
#include <stdint.h>

#define ENGINE_EVENT_CAP 1024u
#define ENGINE_ECHO_CAP 64u
#define ENGINE_ECHO_WINDOW_MS 150
#define ENGINE_HOOK_SILENCE_MS 8000
#define ENGINE_EXTRA_ECHO 0x56454E44ull

#define ENGINE_KEY_PAUSE 0x13u
#define ENGINE_KEY_NUMPAD_ENTER 256u
#define ENGINE_KEY_LBUTTON 257u
#define ENGINE_KEY_RBUTTON 258u
#define ENGINE_KEY_MBUTTON 259u
#define ENGINE_KEY_XBUTTON1 260u
#define ENGINE_KEY_XBUTTON2 261u
#define ENGINE_KEY_ID_MAX 261u
#define ENGINE_SWALLOW_IDS 262

#define ENGINE_SWALLOW_BIT 0x01u
#define ENGINE_PASS_BIT 0x02u

#define ENGINE_HOOK_KEY 1
#define ENGINE_HOOK_MOUSE 2

#define ENGINE_HOOK_RAISE_KEY 1u
#define ENGINE_HOOK_RAISE_MOUSE 2u

#define ENGINE_MSG_KEYDOWN 0x0100u
#define ENGINE_MSG_KEYUP 0x0101u
#define ENGINE_MSG_SYSKEYDOWN 0x0104u
#define ENGINE_MSG_SYSKEYUP 0x0105u
#define ENGINE_MSG_MOUSEMOVE 0x0200u
#define ENGINE_MSG_LBUTTONDOWN 0x0201u
#define ENGINE_MSG_LBUTTONUP 0x0202u
#define ENGINE_MSG_RBUTTONDOWN 0x0204u
#define ENGINE_MSG_RBUTTONUP 0x0205u
#define ENGINE_MSG_MBUTTONDOWN 0x0207u
#define ENGINE_MSG_MBUTTONUP 0x0208u
#define ENGINE_MSG_MOUSEWHEEL 0x020Au
#define ENGINE_MSG_XBUTTONDOWN 0x020Bu
#define ENGINE_MSG_XBUTTONUP 0x020Cu
#define ENGINE_MSG_MOUSEHWHEEL 0x020Eu

/* LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED, and the mouse pair. */
#define ENGINE_KEY_INJECTED 0x12u
#define ENGINE_MOUSE_INJECTED 0x03u
#define ENGINE_KEY_EXTENDED 0x01u
#define ENGINE_KEY_UP_FLAG 0x80u

struct engine_finger {
    uint16_t id;
    uint8_t down;
    /* The hook returned 1 for this event, so the app in front never saw it. */
    uint8_t swallowed;
    int64_t qpc;
};

struct engine_echo {
    int64_t until;
    uint16_t id;
    uint8_t down;
    uint8_t consumed;
};

struct engine_swallow_copy {
    uint8_t bits[ENGINE_SWALLOW_IDS];
};

/* One writer and one reader on each ring. The hook thread writes events
   and reads echoes. Playback does the opposite. */
struct engine_hook_state {
    atomic_uint event_head;
    atomic_uint event_tail;
    atomic_int event_overflow;
    struct engine_finger events[ENGINE_EVENT_CAP];

    atomic_uint echo_head;
    atomic_uint echo_tail;
    struct engine_echo echoes[ENGINE_ECHO_CAP];

    struct engine_swallow_copy swallow[2];
    atomic_uint swallow_index;

    atomic_int wake_on_input;
    atomic_int_least64_t key_stamp;
    atomic_int_least64_t mouse_stamp;
    atomic_int key_installed;
    atomic_int mouse_installed;
    atomic_int need_mouse;
    atomic_int suspect;
    atomic_uint pending_ops;
    atomic_int stop;

    /* Hook thread only. A key-up follows its own down: swallowed when the
       down was swallowed, passed when it was passed. win_passed is a LWin or
       RWin down that Windows saw. blocked_in_win is a key or button the hook
       swallowed since then. */
    uint8_t down_blocked[ENGINE_SWALLOW_IDS];
    uint8_t win_passed[2];
    uint8_t blocked_in_win;
};

struct engine_hook_sample {
    int kind;
    uint32_t msg;
    uint32_t vk;
    uint32_t scan;
    uint32_t flags;
    uint32_t mouse_data;
    uint64_t extra;
    int64_t now;
};

struct engine_hook_decision {
    int swallow;
    int enqueued;
    int wake;
    /* LWin or RWin. The caller swallows this up and sends the mask key, then
       that up, tagged with ENGINE_EXTRA_ECHO, like AHK's #MenuMaskKey. */
    uint16_t mask_win;
};

/* AHK's default #MenuMaskKey. Unassigned, so no window acts on it. */
#define ENGINE_MENU_MASK_VK 0xE8u

struct engine_hook_watch_in {
    int key_installed;
    int mouse_installed;
    int need_mouse;
    int armed;
    int running;
    int pending;
    int suspect;
    int desktop;
    /* The live config id just changed. Reinstall the mouse hook when this
       config has a middle or side trigger. A keyboard-only config still drops it. */
    int switched;
    int64_t key_stamp;
    int64_t mouse_stamp;
    int64_t now;
    /* 8000 ms in stamp units. 0 turns the silence check off. */
    int64_t silence;
    /* When the last silence raise was asked for. 0 is never. */
    int64_t silence_at;
};

struct engine_hook_watch_out {
    int count;
    uint8_t op[4];
    int clear_suspect;
    /* The keyboard raise in op is the silence rule. */
    int silent;
};

struct engine_swallow_rule {
    int is_trigger;
    int focus_ok;
    int pass;
};

void engine_hook_init(struct engine_hook_state *st);

/* Virtual key after side resolution, or 256 for numpad enter.
   0 is not a key. Right shift is scan 0x36. */
uint16_t engine_resolve_key(uint32_t vk, uint32_t scan, uint32_t flags);

/* 257..261, or 0 when the message is not a button.
   down is 1 on a button-down message.
   An X button is the high word, or the low word when the high word is 0.
   Mouse 4 and mouse 5 use that same rule. */
uint16_t engine_resolve_mouse(uint32_t msg, uint32_t mouse_data, int *down);

/* Protocol names. NULL means the id is omitted from held. */
const char *engine_key_name(uint16_t id);

int engine_is_mouse_button(uint16_t id);

/* Pause, left click, right click, and an empty id are not triggers.
   Middle and both side buttons are. */
int engine_can_trigger(uint16_t id);

/* Pause, on the down edge. The caller calls StopAll. This does not. */
int engine_panic_edge(uint16_t id, int was_down, int down);

/* Empty focus swallows unless the shell or its WebView2 host is in front.
   A set focus swallows only when the stem matches. */
int engine_focus_match(const char *want, const char *have);
int engine_shell_in_front(const char *front, const char *owner);
int engine_focus_swallow(const char *focus_exe, const char *front, const char *owner);

/* Swallow when it is a trigger, focus matches, and the pass count is 0.
   Software and board use the same bits for keys, middle, and both side
   buttons. Left and right always pass. */
void engine_swallow_for_trigger(const struct engine_swallow_rule *rule,
                                int *swallow, int *pass);

/* Clear the inactive copy, then publish it. Fill every id between the two. */
uint8_t *engine_swallow_begin(struct engine_hook_state *st);
void engine_swallow_set(uint8_t *bits, uint16_t id, int swallow, int pass);
void engine_swallow_publish(struct engine_hook_state *st);

void engine_hook_set_wake(struct engine_hook_state *st, int wake);
void engine_hook_set_need_mouse(struct engine_hook_state *st, int need);
void engine_hook_set_suspect(struct engine_hook_state *st);
void engine_hook_set_stop(struct engine_hook_state *st, int stop);

/* 1 when the token was stored. A full ring drops the new token.
   until is an absolute stamp in the same unit as now. Equal still matches. */
int engine_echo_push(struct engine_hook_state *st, uint16_t id, int down, int64_t until);

/* Drop expired tokens. 1 when a live token of this id and direction was consumed. */
int engine_echo_match(struct engine_hook_state *st, uint16_t id, int down, int64_t now);

/* Read the struct fields, write at most one event, and say whether to swallow.
   Move and wheel are not events. Echo and injected input are not a finger.
   Keyboard, middle, and both side buttons swallow from the published map.
   Left and right always pass. */
void engine_hook_classify(struct engine_hook_state *st,
                          const struct engine_hook_sample *sample,
                          struct engine_hook_decision *out);

int engine_event_push(struct engine_hook_state *st, const struct engine_finger *finger);
int engine_event_pop(struct engine_hook_state *st, struct engine_finger *finger);
int engine_event_overflow(struct engine_hook_state *st);
void engine_event_clear_overflow(struct engine_hook_state *st);

/* A missing keyboard hook, a suspect flag, or a return from the lock screen
   re-raises, even when idle. Idle with a live hook does not re-raise.
   While armed or playing, a keyboard hook with no event for silence is
   reinstalled, like AHK NeedsKeyRaise: Windows can remove a hook and leave
   its handle looking valid. A mouse event inside that window, while the
   mouse hook is needed, keeps it. After a silence raise, the next one waits
   another full window. Held fingers are not touched. A config switch reinstalls the
   mouse hook when the new config needs one. Ops run in order. Each op unhooks first. */
void engine_hook_watch(const struct engine_hook_watch_in *in,
                       struct engine_hook_watch_out *out);

/* Latest plan wins. The hook thread takes it between messages. */
void engine_hook_post_ops(struct engine_hook_state *st,
                          const struct engine_hook_watch_out *out);
int engine_hook_take_ops(struct engine_hook_state *st,
                         struct engine_hook_watch_out *out);

#endif
