/* Shell frames the resident process applies. No Win32.
   start arms. stop disarms and StopAll. reload and profile publish an image
   on the IPC thread. output changes the send mode. Playback advances.
   See docs/engine/plan.md and docs/engine/ipc-protocol.md. */
#ifndef ENGINE_COMMAND_H
#define ENGINE_COMMAND_H

#include <stdatomic.h>
#include <stdint.h>

#include "hook.h"
#include "process.h"
#include "profile.h"

#define ENGINE_CMD_START 1
#define ENGINE_CMD_STOP 2
#define ENGINE_CMD_RELOAD 3
#define ENGINE_CMD_OUTPUT 4
#define ENGINE_CMD_CAP 64u

#define ENGINE_FRAME_IGNORE 0
#define ENGINE_FRAME_QUIT 1
#define ENGINE_FRAME_JSON 2
#define ENGINE_FRAME_BAD_VERSION 3
#define ENGINE_FRAME_BAD 4
#define ENGINE_FRAME_START 5
#define ENGINE_FRAME_STOP 6
#define ENGINE_FRAME_RELOAD 7
#define ENGINE_FRAME_COMMAND 8
#define ENGINE_FRAME_OUTPUT 9
#define ENGINE_FRAME_PROFILE 10

#define ENGINE_TRIGGER_NONE 0
#define ENGINE_TRIGGER_START 1
#define ENGINE_TRIGGER_STOP 2
#define ENGINE_TRIGGER_MEM 1024

#define ENGINE_STATE_WINDOW_CAP 256
#define ENGINE_STATE_TITLE_CAP 256

struct engine_pipe_frame {
    int kind;
    int output_mode;
    const unsigned char *profile;
    uint32_t profile_len;
};

struct engine_cmd_item {
    uint8_t kind;
    uint8_t mode;
};

/* One writer (IPC) and one reader (playback). Overflow drops the new item. */
struct engine_cmd_ring {
    struct engine_cmd_item slots[ENGINE_CMD_CAP];
    atomic_uint head;
    atomic_uint tail;
    atomic_int overflow;
};

struct engine_trigger_mem {
    int need_seed;
    int count;
    /* The profile image the seed belongs to. prev is by macro index, so a
       new image reseeds, like AHK sets liveId to Chr(1) on a profile. */
    uint64_t profile_seq;
    uint8_t prev[ENGINE_TRIGGER_MEM];
    /* A whileHeld press refused because the last runner was still finishing.
       Kept until the key comes up, by macro index like prev. */
    uint8_t owed[ENGINE_TRIGGER_MEM];
};

/* The triggers AHK would have hooked last tick, and its swallow focus. */
struct engine_claim_mem {
    int ready;
    int swallow;
    uint8_t claimed[ENGINE_SWALLOW_IDS];
};

struct engine_state_window {
    char exe[ENGINE_PROFILE_FOCUS_CAP];
    char title[ENGINE_STATE_TITLE_CAP];
};

/* Board reports, playback to the IPC thread. One writer, one reader, in
   publish order. A report is a diff of downs and ups, so dropping one can
   leave a key held on the board. A full queue refuses and latches. */
#define ENGINE_REPORT_QUEUE_CAP 256u

struct engine_report_queue {
    struct engine_report slots[ENGINE_REPORT_QUEUE_CAP];
    atomic_uint head;
    atomic_uint tail;
    atomic_int overflow;
};

/* Version 1 frames. A leading '{' is JSON. Short, bad magic, and a length
   that does not match the message are bad frames. Profile bytes point into
   msg and are valid only until the caller reads again. */
int engine_frame_read(const unsigned char *msg, int length, struct engine_pipe_frame *out);

void engine_cmd_ring_init(struct engine_cmd_ring *ring);

/* 1 when stored. 0 sets the overflow latch and does not store. */
int engine_cmd_push(struct engine_cmd_ring *ring, int kind, int mode);
int engine_cmd_pop(struct engine_cmd_ring *ring, struct engine_cmd_item *out);

/* 1 once, then the latch is clear. */
int engine_cmd_take_overflow(struct engine_cmd_ring *ring);

/* whileHeld stops when the finger is up. toggle flips on the down edge.
   onRelease starts on the up edge. once and repeat start on the down edge. */
int engine_trigger_action(int mode, int down, int was, int running);

void engine_trigger_init(struct engine_trigger_mem *mem);
void engine_trigger_reseed(struct engine_trigger_mem *mem);

/* The next poll copies the current finger into prev and does not start. */
void engine_trigger_seed(struct engine_trigger_mem *mem, const struct engine_live_config *live,
                         const struct engine_fingers *fingers);

/* A new profile image reseeds on the next poll. */
void engine_trigger_note_profile(struct engine_trigger_mem *mem, uint64_t seq);

/* Seed now when a reseed is due. Playback calls this before it drains the
   tick's finger events, so a press or release that arrives with a profile,
   a reload or a start is still an edge. A key held before that does not
   start. 1 when it seeded. */
int engine_trigger_seed_due(struct engine_trigger_mem *mem, const struct engine_live_config *live,
                            const struct engine_fingers *fingers);

/* First ENGINE_TRIGGER_MEM macros. A tapHold or tapSpam block uses whileHeld
   for the edge. Advanced blocks ask-stop when the release should finish the
   pass. A done once-lock does not start the macro again. An open lock starts
   it once, then the lock is spent. A start needs a down edge, like AHK.
   A runner that ends while the key is still down is not started again,
   except a whileHeld press that came while it was finishing (owed below).
   A Run Once (play once, no hold loop) that is still playing ignores a new
   down edge: one press is one whole pass. The edge is noted as "playing". */
void engine_poll_triggers(struct engine_play *play, const struct engine_live_config *live,
                          struct engine_trigger_mem *mem, const struct engine_fingers *fingers,
                          const char *front, const char *owner);

/* The same, while a radial menu may be open. A macro on a key the open menu
   owns (1 to 8, its step keys, left click to pick) sees no edge. radial may be NULL. */
void engine_poll_triggers_radial(struct engine_play *play, const struct engine_live_config *live,
                                 struct engine_trigger_mem *mem, const struct engine_fingers *fingers,
                                 const char *front, const char *owner,
                                 const struct engine_radial_state *radial);

/* After the tick. A whileHeld press refused as already-running, because the
   last runner was finishing its pass after the release, starts once that
   runner is gone and the key is still down. Reason held-press. An up, a
   focus switch, a disabled macro or a mute drops it. */
void engine_trigger_owed(struct engine_play *play, const struct engine_live_config *live,
                         struct engine_trigger_mem *mem, const struct engine_fingers *fingers,
                         const char *front, const char *owner);

/* ArmOnceLock. Each id passes its trigger until release. A live runner is
   marked done. A finger that is already down starts one play, then done.
   Anything else stays open. pauseWatch "once" does not call this. */
void engine_once_arm(struct engine_play *play, const struct engine_live_config *live,
                     const char *const *ids, int count, const struct engine_fingers *fingers);

/* Mark a runner done when its id is not in the live catalog. */
void engine_reap_orphans(struct engine_play *play, const struct engine_live_config *live);

/* Empty when disarmed. An armed trigger, a key, middle, or a side button, is
   swallowed when the live profile focus says so, for software and for a
   board. That is the AHK $* hotkey, and the board low-level hook that eats
   the real key or click. A board's own clicks match an echo token first and
   pass. Unmatched focus and the editor pass the button. Pause, left click,
   and right click are not triggers, so those clicks always pass and never
   start a macro. Middle and both side buttons are triggers. A once-lock pass
   count lets that button through. A null play has no pass list. */
void engine_arm_swallow(struct engine_hook_state *hooks, const struct engine_live_config *live,
                        const char *front, const char *owner, int armed,
                        const struct engine_play *play);

/* The same, plus each ready radial menu's trigger, the open menu's keys, and
   the move and left-click flags for an open flick menu. radial may be NULL. */
void engine_arm_swallow_radial(struct engine_hook_state *hooks, const struct engine_live_config *live,
                               const char *front, const char *owner, int armed,
                               const struct engine_play *play,
                               const struct engine_radial_state *radial);

/* 1 when a live enabled macro with steps triggers on middle or a side button,
   or a ready radial menu does, or picks with flick.
   Left, right, and keyboard triggers do not install the mouse hook. */
int engine_live_needs_mouse(const struct engine_live_config *live);

/* 1 when an enabled macro with steps, or a ready radial menu, uses this id as its trigger. */
int engine_live_claims(const struct engine_live_config *live, uint16_t id);

void engine_claims_init(struct engine_claim_mem *mem);

/* AHK forgets a trigger's finger when its hotkey goes away: SyncSwallow
   flipping (focus in or out of the live profile), an output change, a key
   that is no longer a trigger, and stop. Those keys go back to never
   reported, so a lost up cannot keep a trigger held. Claims are empty while
   disarmed. force is the output change. The first call only records.
   Writes the forgotten ids that were down. Returns how many. */
int engine_claims_sync(struct engine_claim_mem *mem, const struct engine_live_config *live,
                       const char *front, const char *owner, int armed, int force,
                       struct engine_fingers *fingers, uint16_t *out, int cap);

/* 0 when the frame fits. -1 when cap is too small. Held ids without a name
   are omitted. Window extras past the cap are dropped from the front. */
int engine_state_frame(const struct engine_state_snapshot *snap,
                       const struct engine_state_window *windows, int window_count,
                       unsigned char *buf, int cap);

void engine_report_queue_init(struct engine_report_queue *queue);

/* 1 when stored. 0 sets the overflow latch and stores nothing. */
int engine_report_queue_push(struct engine_report_queue *queue, const struct engine_report *report);
int engine_report_queue_pop(struct engine_report_queue *queue, struct engine_report *out);

/* 1 once, then the latch is clear. */
int engine_report_queue_take_overflow(struct engine_report_queue *queue);

/* Call after the report frame is written. Each down and up in the report
   gets an echo token that lasts 150 ms from now, so the Pico tap is not a
   finger. AHK queues the token right after its pipe write. now and freq are
   QPC. Returns the number of tokens stored. */
int engine_board_echo_written(struct engine_hook_state *hooks, const struct engine_report *report,
                              int64_t now, int64_t freq);

/* Header plus the 156-byte image. Returns the byte count, or -1. */
int engine_report_frame(const struct engine_report *report, unsigned char *buf, int cap);

#endif
