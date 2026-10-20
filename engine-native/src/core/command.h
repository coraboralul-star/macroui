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
    uint8_t prev[ENGINE_TRIGGER_MEM];
    /* Set on a real down edge. Cleared on the up and on a reseed.
       A hold that is still down uses it to start again after the runner
       stops. The seed poll does not set it, so a key already held at arm
       stays quiet. */
    uint8_t latched[ENGINE_TRIGGER_MEM];
};

struct engine_state_window {
    char exe[ENGINE_PROFILE_FOCUS_CAP];
    char title[ENGINE_STATE_TITLE_CAP];
};

struct engine_report_slot {
    struct engine_report copy[2];
    atomic_uint seq;
    atomic_int index;
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

/* First ENGINE_TRIGGER_MEM macros. A tapHold or tapSpam block uses whileHeld
   for the edge. Advanced blocks ask-stop when the release should finish the
   pass. A done once-lock does not start the macro again. An open lock starts
   it once, then the lock is spent. A while-held macro, or a once macro whose
   block is a hold loop, starts again when that down edge already happened
   and the finger is still down but the runner is gone. */
void engine_poll_triggers(struct engine_play *play, const struct engine_live_config *live,
                          struct engine_trigger_mem *mem, const struct engine_fingers *fingers,
                          const char *front, const char *owner);

/* ArmOnceLock. Each id passes its trigger until release. A live runner is
   marked done. A finger that is already down starts one play, then done.
   Anything else stays open. pauseWatch "once" does not call this. */
void engine_once_arm(struct engine_play *play, const struct engine_live_config *live,
                     const char *const *ids, int count, const struct engine_fingers *fingers);

/* Mark a runner done when its id is not in the live catalog. */
void engine_reap_orphans(struct engine_play *play, const struct engine_live_config *live);

/* Empty when disarmed. An armed keyboard trigger is swallowed when the live
   profile focus says so, for software and for a board. That is the AHK $*
   hotkey, and the board low-level hook that still eats the real key.
   A mouse trigger is swallowed only in software. Board output passes the
   click. Unmatched focus and the editor pass the button. Pause, left click,
   and right click are not triggers, so those clicks always pass and never
   start a macro. Middle and both side buttons are triggers. A once-lock pass
   count lets that button through. A null play has no pass list and treats
   output as software. */
void engine_arm_swallow(struct engine_hook_state *hooks, const struct engine_live_config *live,
                        const char *front, const char *owner, int armed,
                        const struct engine_play *play);

/* 1 when a live enabled macro with steps triggers on middle or a side button.
   Left, right, and keyboard triggers do not install the mouse hook. */
int engine_live_needs_mouse(const struct engine_live_config *live);

/* 1 when an enabled macro with steps uses this id as its trigger. */
int engine_live_claims(const struct engine_live_config *live, uint16_t id);

/* 0 when the frame fits. -1 when cap is too small. Held ids without a name
   are omitted. Window extras past the cap are dropped from the front. */
int engine_state_frame(const struct engine_state_snapshot *snap,
                       const struct engine_state_window *windows, int window_count,
                       unsigned char *buf, int cap);

void engine_report_slot_init(struct engine_report_slot *slot);
void engine_report_slot_publish(struct engine_report_slot *slot, const struct engine_report *report);

/* 1 when seq changed. *seen is updated. */
int engine_report_slot_take(struct engine_report_slot *slot, uint32_t *seen,
                            struct engine_report *out);

/* Header plus the 156-byte image. Returns the byte count, or -1. */
int engine_report_frame(const struct engine_report *report, unsigned char *buf, int cap);

#endif
