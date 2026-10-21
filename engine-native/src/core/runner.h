/* Block steps and the runner. This file stays free of Win32.
   Advance does not allocate. The caller passes now and the QPC frequency.
   Tests use frequency 1000, so one tick is one millisecond.
   SendInput stays in src/win/output_win.c. main does not call this. */
#ifndef ENGINE_RUNNER_H
#define ENGINE_RUNNER_H

#include <stdint.h>

#include "output.h"

#define ENGINE_RUNNER_CAP 32
#define ENGINE_FRAME_CAP 16
#define ENGINE_HOLD_CAP 64
#define ENGINE_STEP_CAP 1024
#define ENGINE_ID_CAP 64
#define ENGINE_MUTE_ID_CAP 1024
#define ENGINE_MUTE_SLOT_CAP 64
#define ENGINE_ONCE_CAP 64
#define ENGINE_LOCK_NONE 0
#define ENGINE_LOCK_OPEN 1
#define ENGINE_LOCK_DONE 2
#define ENGINE_FINGER_BYTES ((ENGINE_KEY_ID_MAX / 8u) + 1u)

#define ENGINE_MODE_ONCE 0
#define ENGINE_MODE_REPEAT 1
#define ENGINE_MODE_WHILE_HELD 2
#define ENGINE_MODE_TOGGLE 3
#define ENGINE_MODE_ON_RELEASE 4

/* Why a runner started, stopped, or was refused, and what a trigger edge
   did. The playback thread turns these into engine.log lines. */
#define ENGINE_WHY_NONE 0
#define ENGINE_WHY_EDGE 1
#define ENGINE_WHY_UNMUTE 2
#define ENGINE_WHY_RUN 3
#define ENGINE_WHY_ONCE_LOCK 4
#define ENGINE_WHY_RUNNING 5
#define ENGINE_WHY_NO_RESTART 6
#define ENGINE_WHY_BUSY 7
#define ENGINE_WHY_RUNNER_CAP 8
#define ENGINE_WHY_ARENA_FULL 9
#define ENGINE_WHY_NO_ID 10
#define ENGINE_WHY_MUTED 11
#define ENGINE_WHY_ONCE_DONE 12
#define ENGINE_WHY_FOCUS 13
#define ENGINE_WHY_DISABLED 14
#define ENGINE_WHY_FINISHED 15
#define ENGINE_WHY_RELEASE 16
#define ENGINE_WHY_NEXT_UP 17
#define ENGINE_WHY_SCAN_RELEASE 18
#define ENGINE_WHY_EDGE_RELEASE 19
#define ENGINE_WHY_TOGGLE 20
#define ENGINE_WHY_STOP 21
#define ENGINE_WHY_STOP_ALL 22
#define ENGINE_WHY_EXCLUSIVE 23
#define ENGINE_WHY_RESTART 24
#define ENGINE_WHY_ORPHAN 25
#define ENGINE_WHY_FRAME_CAP 26
#define ENGINE_WHY_HOLD_CAP 27
#define ENGINE_WHY_ASK_STOP 28
#define ENGINE_WHY_PANIC 29
#define ENGINE_WHY_OVERFLOW 30
#define ENGINE_WHY_COMMAND 31
#define ENGINE_WHY_START 32
#define ENGINE_WHY_COUNT 33

#define ENGINE_NOTE_EDGE 1
#define ENGINE_NOTE_START 2
#define ENGINE_NOTE_STOP 3
#define ENGINE_NOTE_BLOCK 4
#define ENGINE_NOTE_CAP 128

#define ENGINE_STOP_NONE 0
#define ENGINE_STOP_NEXT_UP 1
#define ENGINE_STOP_FINISH 2

#define ENGINE_ACT_TAP 0
#define ENGINE_ACT_DOWN 1
#define ENGINE_ACT_UP 2

/* Numbers stay fixed. The play tick runs key, mouse, move, scroll, wait,
   randomWait, repeat, goto, run, holdLoop, swapLoop, burst, ifShort,
   onceHeld, edgeHold, and scanWait.
   pressHold is not one of these. engine_expand_press_hold writes the steps. */
#define ENGINE_STEP_KEY 1
#define ENGINE_STEP_MOUSE 2
#define ENGINE_STEP_MOVE 3
#define ENGINE_STEP_SCROLL 4
#define ENGINE_STEP_WAIT 5
#define ENGINE_STEP_RANDOM_WAIT 6
#define ENGINE_STEP_REPEAT 7
#define ENGINE_STEP_HOLD_LOOP 8
#define ENGINE_STEP_SWAP_LOOP 9
#define ENGINE_STEP_BURST 10
#define ENGINE_STEP_IF_SHORT 11
#define ENGINE_STEP_ONCE_HELD 12
#define ENGINE_STEP_EDGE_HOLD 13
#define ENGINE_STEP_SCAN_WAIT 14
#define ENGINE_STEP_GOTO 15
#define ENGINE_STEP_RUN 16

/* goto where. Anything else, including an empty name, is screen. */
#define ENGINE_WHERE_SCREEN 0
#define ENGINE_WHERE_WINDOW 1
#define ENGINE_WHERE_CLIENT 2

/* key and mouse: action is down, up, or tap. Tap stores holdMs in a.
   wait stores ms in a. randomWait stores min in a and max in b.
   move stores x in a and y in b. scroll stores dy in a.
   repeat stores the count in a, the frame release stop in b, and the child
   span in first and count. A negative repeat count becomes 0, which loops.
   goto stores x in a and y in b, where in action, and the duration in count.
   0 does not wait. A missing duration is 15 when the step is built.
   run stores the catalog index in a. A negative index is a missing target.
   holdLoop stores the child span in first and count, swapAfterMs in a, and
   the frame release stop in b. 0 swapAfterMs does not swap. b keeps finish
   and nextUp. Anything else is none. mute_at and mute_n index the macro
   mute table. Those entries are catalog indexes.
   swapLoop stores the same fields, and afterMs in c. 0 afterMs does not
   wait. The step builder stores 150 when the file omitted afterMs.
   afterMs is not divided by speed.
   burst stores the child span in first and count, and forMs in a.
   0 and a negative a run the children once. A positive a is divided by
   speed, then converted to ticks with truncation. It is not forced up to
   1 ms. The step builder stores 0 when the file omitted forMs.
   ifShort stores the child span in first and count, underMs in a, and
   minCycles in b. underMs is not divided by speed. 0 and a negative a
   skip the children. The step builder stores 150 and 3 when the file
   omitted them. took_short then skips later top-frame steps except burst
   and ifShort. c stays swapLoop afterMs. burst and ifShort do not use it.
   onceHeld stores the child span in first and count, and gapMs in a.
   The step builder stores 70 when the file omitted gapMs. gapMs is
   divided by speed when the frame is built. 0 and a negative a do not
   wait. A positive gap waits through the normal arm path. c stays
   swapLoop afterMs. onceHeld does not use it.
   edgeHold stores the edge key in key. armMs is a. gapMs is b. holdMs is
   d. restMs is e. ignoreMs is f. c stays swapLoop afterMs. action bit 0
   is spam, set only for tapSpam. Bits 1 and 2 are the ignore mode. Bit 3
   is pause block. first and count are the watch-key span. Those steps are
   not executed. The step builder stores key z, armMs 5, gapMs 80, holdMs
   18, and restMs 18 when the file omitted them. armMs and gapMs are
   divided by speed at the wait. holdMs and restMs are divided by speed
   when the frame is built. The 5 ms poll, the key delay, and the key
   repeat are not. "once" is not an ignore mode and not a pause lock.
   A burst frame stores its deadline in burst_until. 0 means there is none.
   A onceHeld frame stores the phase, the first-pass flag, the scaled gap,
   and the gap deadline. The gap deadline 0 means it has not been armed.
   An edgeHold frame stores the phase, the watch list, and the spam clock.
   scanWait stores ms in a, the same field as wait. ms is divided by speed
   when the wait is armed. 0 and a negative a still wait 1 ms. A non-empty
   trigger that the finger snapshot says is up marks the runner done.
   An empty trigger does not. While scan_wait is set, stopping or a trigger
   finger that is up ends the runner during the wait. scan_wait is set only
   when the step runs, and it is cleared after the wait finishes.
   pressHold does not get a step number. Child steps are copied through.
   A non-empty key with no children writes key down, wait 18, key up.
   scanWait is always appended. holdMs defaults to 200 when it was omitted.
   That default is not the editor's 80. 0 and a negative holdMs are stored.
   The wait of 18 and holdMs are divided later, on the wait arm path. */

#define ENGINE_EDGE_WATCH_CAP 8
#define ENGINE_EDGE_MUTE_CAP 8
/* Mute slots one hold frame can keep. Later ids in the list are not muted. */
#define ENGINE_FRAME_MUTE_CAP 16
#define ENGINE_EDGE_PHASE_DOWN 0
#define ENGINE_EDGE_PHASE_ARM 1
#define ENGINE_EDGE_PHASE_GAP 2
#define ENGINE_EDGE_PHASE_QUIET 3
#define ENGINE_IGNORE_OFF 0
#define ENGINE_IGNORE_MACRO 1
#define ENGINE_IGNORE_BOTH 2
#define ENGINE_EDGE_SPAM 0x01u
#define ENGINE_EDGE_PAUSE_BLOCK 0x08u
/* Dry self-test numbers, the default when the caller sets nothing.
   Windows sets the OS keyboard delay and repeat through
   engine_play_set_key_timing, like AHK KeyDelayMs and KeyRepeatMs. */
#define ENGINE_KEY_DELAY_MS 400
#define ENGINE_KEY_REPEAT_MS 33
struct engine_step {
    uint8_t type;
    uint8_t action;
    uint16_t key;
    int32_t a;
    int32_t b;
    uint32_t first;
    uint32_t count;
    uint16_t mute_at;
    uint16_t mute_n;
    int32_t c;
    int32_t d;
    int32_t e;
    int32_t f;
};

struct engine_frame {
    uint32_t first;
    uint32_t count;
    uint32_t index;
    int32_t repeats_left;
    int release_stop;
    int hold_loop;
    int drain;
    int mute_on;
    int32_t swap_after_ms;
    int64_t burst_until;
    int once_held;
    int once_phase;
    int once_first;
    int64_t once_gap;
    int64_t once_ready;
    int edge_hold;
    int edge_spam;
    uint16_t edge_key;
    int edge_phase;
    int32_t edge_arm_ms;
    int32_t edge_gap_ms;
    int edge_ignore;
    int32_t edge_ignore_ms;
    uint16_t edge_watch[ENGINE_EDGE_WATCH_CAP];
    uint8_t edge_prev[ENGINE_EDGE_WATCH_CAP];
    int64_t edge_lock[ENGINE_EDGE_WATCH_CAP];
    uint8_t edge_lock_set[ENGINE_EDGE_WATCH_CAP];
    int edge_watch_n;
    int64_t edge_repeat_at;
    int64_t edge_hold_for;
    int64_t edge_rest_for;
    int edge_spam_on;
    int64_t edge_spam_at;
    int edge_mute_on;
    /* Slots in play->mute_slot_*, not catalog indexes. A slot keeps its id
       while this frame holds a count on it, so a config switch or a profile
       push cannot make the release unmute a different macro. */
    uint8_t edge_muted[ENGINE_EDGE_MUTE_CAP];
    int edge_muted_n;
    uint16_t mute_at;
    uint16_t mute_n;
    uint8_t mute_slot[ENGINE_FRAME_MUTE_CAP];
    int mute_slot_n;
};

struct engine_pulse {
    uint16_t id;
    int64_t until;
};

struct engine_runner {
    char id[ENGINE_ID_CAP];
    double speed;
    int64_t freq;
    int32_t key_delay_ms;
    int32_t key_repeat_ms;
    int64_t started_at;
    int64_t wait_until;
    const struct engine_step *steps;
    uint32_t step_count;
    struct engine_frame stack[ENGINE_FRAME_CAP];
    int stack_count;
    uint16_t holds[ENGINE_HOLD_CAP];
    int hold_count;
    struct engine_pulse pulses[ENGINE_HOLD_CAP];
    int pulse_count;
    uint16_t echo[ENGINE_HOLD_CAP];
    int echo_count;
    uint16_t pressed_now[ENGINE_HOLD_CAP];
    int pressed_count;
    int32_t move_x;
    int32_t move_y;
    int32_t wheel;
    int has_goto;
    int did_goto;
    int32_t goto_x;
    int32_t goto_y;
    int mode;
    int release_stop;
    int stopping;
    int saw_up;
    int released_now;
    int scan_wait;
    int done;
    int paused;
    int32_t cycles;
    int took_short;
    uint16_t trigger;
    /* This runner's span of play->mute_ids. The arena and the mute table
       are compacted when runners finish, so both spans move. */
    uint32_t mute_first;
    uint32_t mute_len;
    /* ENGINE_WHY_*, set by the first stop. */
    uint8_t end_reason;
};

/* One log event. age is in the runner's ticks. key and down are for EDGE. */
struct engine_note {
    uint8_t kind;
    uint8_t why;
    uint8_t mode;
    uint8_t down;
    uint16_t key;
    int64_t age;
    char id[ENGINE_ID_CAP];
};

/* top_count 0 means the whole pool is the top frame.
   A nested repeat sets top_count to the parent span. Child indexes are
   absolute indexes into steps. speed is raised to 0.05. A non-finite
   speed becomes 1 first. release_stop other than finish is nextUp. */
struct engine_macro {
    char id[ENGINE_ID_CAP];
    const struct engine_step *steps;
    uint32_t step_count;
    uint32_t top_count;
    double speed;
    int mode;
    int32_t repeat_count;
    int busy;
    int exclusive;
    int release_stop;
    int basic;
    int advanced;
    int trigger_is_key;
    int32_t gap_ms;
    uint16_t trigger;
    const int32_t *mutes;
    uint32_t mute_count;
};

typedef int64_t (*engine_roll_fn)(int64_t lo, int64_t hi, void *ctx);

/* Foreground snapshot for goto. The core does not query a window.
   have_mouse 0 is a mouse at 0, 0. A missing window or client rect
   makes that goto a no-op. */
/* Physical finger. seen is physDown.Has. down is the value.
   A key that was never reported is not down. */
struct engine_fingers {
    uint8_t down[ENGINE_FINGER_BYTES];
    uint8_t seen[ENGINE_FINGER_BYTES];
};

struct engine_pointer {
    int have_mouse;
    int32_t mouse_x;
    int32_t mouse_y;
    int have_window;
    int32_t window_x;
    int32_t window_y;
    int have_client;
    int32_t client_x;
    int32_t client_y;
};

/* The play struct is large. Keep it off a deep stack.
   freq 0 is stored as 1000 when a runner starts. dpi 0 is left 0.
   macros is the caller's catalog. run indexes it. holdLoop and swapLoop
   index mutes the same way. The pointer is the tick's mouse and window origin.
   The finger snapshot is the physical key state for that tick. */
struct engine_play {
    struct engine_runner runners[ENGINE_RUNNER_CAP];
    int runner_count;
    struct engine_runner pending[ENGINE_RUNNER_CAP];
    int pending_count;
    struct engine_step arena[ENGINE_STEP_CAP];
    uint32_t arena_count;
    struct engine_output output;
    struct engine_report last;
    int64_t freq;
    int32_t key_delay_ms;
    int32_t key_repeat_ms;
    uint32_t dpi;
    engine_roll_fn roll;
    void *roll_ctx;
    const struct engine_macro *macros;
    int macro_count;
    struct engine_pointer pointer;
    int have_pointer;
    struct engine_fingers fingers;
    int have_fingers;
    int64_t last_sent[ENGINE_KEY_ID_MAX + 1u];
    uint8_t last_sent_ok[ENGINE_KEY_ID_MAX + 1u];
    /* Mute targets as ids, resolved from the catalog when the runner is
       queued, like AHK keeps the id strings. An empty name mutes nothing. */
    char mute_ids[ENGINE_MUTE_ID_CAP][ENGINE_ID_CAP];
    uint32_t mute_count;
    char mute_slot_id[ENGINE_MUTE_SLOT_CAP][ENGINE_ID_CAP];
    int mute_slot_count[ENGINE_MUTE_SLOT_CAP];
    int mute_slot_resume[ENGINE_MUTE_SLOT_CAP];
    int mute_slot_used[ENGINE_MUTE_SLOT_CAP];
    /* Once-lock. pass_count is how many locks are letting that key through.
       pauseWatch "once" does not arm a lock. An empty slot has an empty id. */
    uint8_t pass_count[ENGINE_SWALLOW_IDS];
    char once_id[ENGINE_ONCE_CAP][ENGINE_ID_CAP];
    uint16_t once_key[ENGINE_ONCE_CAP];
    uint8_t once_state[ENGINE_ONCE_CAP];
    uint8_t once_pass[ENGINE_ONCE_CAP];
    /* Playback reads and clears these after each tick. */
    struct engine_note notes[ENGINE_NOTE_CAP];
    int note_count;
    uint32_t note_dropped;
    /* The last tick's now, for stop ages. */
    int64_t now;
    /* Why the last engine_play_start returned 0. */
    int refused;
};

void engine_macro_init(struct engine_macro *macro);

/* 1 when name resolved. An empty or unknown name writes 0 and returns 0.
   Alias and SafeKey run here. Advance does not look up a name. */
int engine_key_id(const char *name, uint16_t *id);

/* 0 for an unknown editor type. pressHold is 0 because it expands. */
int engine_step_kind(const char *name);

/* window and client stay. A null, empty, or other name is screen. */
int engine_goto_where(const char *name);

/* "macro" and "both" stay. "once" and every other name are off. */
int engine_ignore_mode(const char *name);

/* "block" stays, after trim and lower case. "once" is off. */
int engine_pause_mode(const char *name);

/* Packs spam, the ignore mode, and pause block into an edgeHold action. */
uint8_t engine_edge_action(int spam, int ignore, int pause_block);

/* 1 when a top-level step loops itself. Nested steps are the caller's count. */
int engine_has_hold_loop(const struct engine_step *steps, uint32_t count);

/* Play-mode repeat count below 1 becomes 1. A hold loop forces 1.
   whileHeld and toggle are 0 unless a hold loop is present. */
int engine_top_repeats(int mode, int32_t repeat_count, int hold_loop);

/* 4 when the trigger is rewritten, 0 when it is left alone, -1 if out is short.
   Left and right click are left alone. gap below 0 becomes 0. The hold is 18. */
int engine_basic_rewrite(uint16_t trigger, int trigger_is_key, int32_t gap_ms,
                         struct engine_step *out, int cap);

/* Expands a pressHold block. It does not allocate.
   A missing steps array is an empty list: child_count 0.
   Children are copied in order. out must not overlap them.
   When NormKey keeps the key and there are no children, it writes key down, wait 18, key up.
   The key id is engine_key_id. An unknown name still expands, with id 0.
   An empty name does not. scanWait is always last. omitted uses 200.
   Otherwise hold_ms is stored, including 0 and a negative value.
   Returns the number written, or -1 when out cannot hold it. */
int engine_expand_press_hold(const char *key, int32_t hold_ms, int omitted,
                             const struct engine_step *children, uint32_t child_count,
                             struct engine_step *out, int cap);

void engine_play_init(struct engine_play *play);
void engine_play_set_roll(struct engine_play *play, engine_roll_fn roll, void *ctx);
/* The first edge-key repeat and the repeat after it. 0 or less keeps the
   dry numbers. A runner copies them when it starts. */
void engine_play_set_key_timing(struct engine_play *play, int32_t delay_ms, int32_t repeat_ms);
/* AHK KeyDelayMs and KeyRepeatMs from the SPI_GETKEYBOARDDELAY and
   SPI_GETKEYBOARDSPEED values. Out of range is clamped the same way. */
int32_t engine_key_delay_from_setting(int delay);
int32_t engine_key_repeat_from_setting(int speed);

/* The catalog stays with the caller. A null list or a count below 1 clears it.
   run reads it during Advance. Indexes are resolved before the step is stored. */
void engine_play_set_macros(struct engine_play *play, const struct engine_macro *macros,
                            int count);

/* Copies the snapshot. A null pointer clears it. */
void engine_play_set_pointer(struct engine_play *play, const struct engine_pointer *pointer);

/* Clears both maps. */
void engine_fingers_clear(struct engine_fingers *fingers);

/* Marks the id as seen. down 0 leaves it up. Id 0 and an id past the table are ignored. */
void engine_fingers_set(struct engine_fingers *fingers, uint16_t id, int down);

/* 1 when the id was reported and is down. */
int engine_fingers_down(const struct engine_fingers *fingers, uint16_t id);

/* 1 when the id was reported, including a report that it is up. */
int engine_fingers_seen(const struct engine_fingers *fingers, uint16_t id);

/* Back to never reported, like AHK physDown.Delete. */
void engine_fingers_forget(struct engine_fingers *fingers, uint16_t id);

/* at[id] is the last event for that id, in the same unit as now. count is the
   length of at. Id 0 and an id past count are ignored. */
void engine_finger_at_touch(int64_t *at, int count, uint16_t id, int64_t now);

/* up and blocked use the same bit layout as fingers->down. A set up bit means
   that id looks up. A set blocked bit is a key the engine swallows: it stays
   down, because a swallowed key never reaches GetAsyncKeyState. A null
   blocked mask blocks nothing. Writes the released ids. Returns how many. */
int engine_fingers_release_os_up(struct engine_fingers *fingers, const uint8_t *up,
                                 const uint8_t *blocked, uint16_t *out, int cap);

/* Apply one hook event. A second down of mouse 4 or 5 clears the finger and
   appends id to defer. A later up of that id removes the defer and leaves the
   finger up, so a click in one batch does not stay held. Every other event,
   including a second left click, applies immediately. Returns 1 when deferred. */
int engine_finger_note(struct engine_fingers *fingers, uint16_t id, int down,
                       uint16_t *defer, int *defer_count, int cap);

/* Releases every down id. keys 1 includes non-mouse ids. mouse 1 includes
   mouse buttons. Writes the released ids. Returns how many were released. */
int engine_fingers_release_down(struct engine_fingers *fingers, int keys, int mouse,
                                uint16_t *out, int cap);

/* Copies the snapshot. A null pointer clears it.
   A missing snapshot is not a finger: holdLoop does not treat it as a release. */
void engine_play_set_fingers(struct engine_play *play, const struct engine_fingers *fingers);

/* 1 when the mute count for id is above 0. */
int engine_play_muted(const struct engine_play *play, const char *id);

/* The mute count. 0 when id is not in the table. */
int engine_play_mute_count(const struct engine_play *play, const char *id);

/* 1 when a runner or a pending runner has this id and is not done. */
int engine_play_has_runner(const struct engine_play *play, const char *id);

/* ENGINE_LOCK_NONE, OPEN, or DONE. */
int engine_once_state(const struct engine_play *play, const char *id);

/* 1 when a once-lock is letting this key through. */
int engine_once_passing(const struct engine_play *play, uint16_t key);

/* Remember id and add one pass for key. A second call for the same id does
   not add another pass. 0 when the table is full and id is new. */
int engine_once_ensure(struct engine_play *play, const char *id, uint16_t key);

/* State is OPEN or DONE. A missing id is left missing. */
void engine_once_set(struct engine_play *play, const char *id, int state);

/* Done. The pass stays until release. */
void engine_once_spend(struct engine_play *play, const char *id);

/* Drop the pass and the lock for each id. */
void engine_once_release(struct engine_play *play, const char *const *ids, int count);

/* A done lock with no runner keeps its pass. */
void engine_once_refresh(struct engine_play *play);

/* mode_override below 0 keeps the macro mode. restart 0 leaves a live id alone.
   Returns 1 when a runner was queued. A full list queues nothing and stops nothing.
   why is the START reason. A refusal stores its reason in play->refused and
   notes BLOCK, except a live run target that is left alone. */
int engine_play_start_why(struct engine_play *play, const struct engine_macro *macro,
                          int mode_override, int restart, int why);
int engine_play_start(struct engine_play *play, const struct engine_macro *macro,
                      int mode_override, int restart);

/* "edge", "arena-full", and so on. Never NULL. */
const char *engine_why_name(int why);

/* Appends one note. Returns its index, or -1 when the ring is full. */
int engine_play_note(struct engine_play *play, int kind, const char *id, int why, int mode,
                     uint16_t key, int down, int64_t age);

void engine_play_flush(struct engine_play *play);
void engine_play_advance(struct engine_play *play, int64_t now);

/* Runs one runner. run does nothing here: there is no catalog.
   goto uses a screen point and a mouse at 0, 0.
   holdLoop and swapLoop do nothing here: there is no mute list and no finger
   snapshot. engine_play_advance is the path that starts run, reads the pointer,
   and runs those blocks. burst, ifShort, onceHeld, edgeHold, and scanWait run
   here. There is no finger, so burst does not wait, ifShort does not yield,
   onceHeld counts the trigger as up, and a non-empty edgeHold or scanWait
   trigger is up. */
void engine_runner_advance(struct engine_runner *runner, int64_t now,
                           engine_roll_fn roll, void *roll_ctx);

/* Holds, then pulses whose deadline is still ahead. Skips a done or paused runner. */
int engine_play_desired(const struct engine_play *play, int64_t now,
                        uint16_t *out, int cap);

/* Sums move and wheel from runners that are still in the list, including a
   runner Advance just finished. A goto replaces that sum with the absolute
   point. The last goto in the list wins. Then writes one report. Returns 1
   when the image should be sent. Does not remove runners and does not call
   StopAll. */
int engine_play_publish(struct engine_play *play, int64_t now);

void engine_play_remove_done(struct engine_play *play);
void engine_play_stop(struct engine_play *play, const char *id);
void engine_play_stop_all(struct engine_play *play);
void engine_play_stop_why(struct engine_play *play, const char *id, int why);
void engine_play_stop_all_why(struct engine_play *play, int why);
void engine_play_ask_stop(struct engine_play *play, const char *id);

/* Remove, flush, advance, flush, publish, then remove again.
   A report overflow calls StopAll after that publish. */
void engine_play_tick(struct engine_play *play, int64_t now);

int engine_play_alive(const struct engine_play *play);
struct engine_runner *engine_play_runner(struct engine_play *play, int index);

#endif
