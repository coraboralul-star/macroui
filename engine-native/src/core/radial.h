/* Radial macro menu (#97). Hold the trigger to open, pick a slot, release to
   play that slot's macro once. No Win32. Playback owns the state.

   One open menu at a time. Every way it ends goes through one close: select,
   cancel, focus, disable, profile change, stop. Whatever the menu holds is
   let go there. The mode is chosen once, at open, by engine_radial_mode.
   Flick reads a vector: engine_radial_move adds raw mouse counts. A cursor
   source can feed the same vector later. */
#ifndef ENGINE_RADIAL_H
#define ENGINE_RADIAL_H

#include <stdint.h>

#include "runner.h"

#define ENGINE_RADIAL_CAP 4
#define ENGINE_RADIAL_SLOTS 8
#define ENGINE_RADIAL_NAME_CAP 64
#define ENGINE_RADIAL_NOTE_CAP 16

/* What the profile asked for. An unknown value is keys. */
#define ENGINE_RADIAL_KEYS 0
#define ENGINE_RADIAL_FLICK 1

#define ENGINE_RADIAL_AT_CENTER 0
#define ENGINE_RADIAL_AT_CURSOR 1

/* Raw counts from where the menu opened. A flick past this lights a slot.
   The vector is capped at this many dead zones, so a flick back turns fast. */
#define ENGINE_RADIAL_DEAD_ZONE 40
#define ENGINE_RADIAL_REACH 3

/* Compiled from config.radials. slot is an index into the config's macros,
   -1 for empty or a macro that is gone. */
struct engine_radial {
    char id[ENGINE_ID_CAP];
    char name[ENGINE_RADIAL_NAME_CAP];
    int enabled;
    uint16_t trigger;
    int select;
    uint16_t prev_key;
    uint16_t next_key;
    int at;
    int32_t dead_zone;
    int click_select;
    int slot_count;
    int32_t slot[ENGINE_RADIAL_SLOTS];
};

#define ENGINE_RADIAL_NOTE_OPEN 1
#define ENGINE_RADIAL_NOTE_SELECT 2
#define ENGINE_RADIAL_NOTE_CANCEL 3

/* Why a menu closed without playing, or what a select did. */
#define ENGINE_RADIAL_WHY_NONE 0
#define ENGINE_RADIAL_WHY_PLAYED 1
#define ENGINE_RADIAL_WHY_PLAYING 2
#define ENGINE_RADIAL_WHY_REFUSED 3
#define ENGINE_RADIAL_WHY_NO_SLOT 4
#define ENGINE_RADIAL_WHY_EMPTY 5
#define ENGINE_RADIAL_WHY_FOCUS 6
#define ENGINE_RADIAL_WHY_DISABLED 7
#define ENGINE_RADIAL_WHY_PROFILE 8
#define ENGINE_RADIAL_WHY_STOP 9
#define ENGINE_RADIAL_WHY_COUNT 10

struct engine_radial_note {
    uint8_t kind;
    uint8_t why;
    uint8_t mode;
    int8_t slot;
    uint8_t slots;
    char radial[ENGINE_ID_CAP];
    char name[ENGINE_RADIAL_NAME_CAP];
    char macro[ENGINE_ID_CAP];
};

/* 1 to 8, then previous and next. */
#define ENGINE_RADIAL_KEY_COUNT 10

struct engine_radial_state {
    int open;
    /* The open menu, copied at open, so a profile push mid-menu cannot move it. */
    struct engine_radial menu;
    int mode;
    int lit;
    int64_t dx;
    int64_t dy;
    uint8_t prev[ENGINE_RADIAL_CAP];
    uint8_t key_prev[ENGINE_RADIAL_KEY_COUNT];
    uint8_t click_prev;
    int need_seed;
    int note_count;
    struct engine_radial_note notes[ENGINE_RADIAL_NOTE_CAP];
};

struct engine_live_config;

void engine_radial_init(struct engine_radial_state *st);

/* The next poll reads the fingers and does not open. Like a trigger reseed. */
void engine_radial_reseed(struct engine_radial_state *st);

/* Enabled, a trigger that can trigger, no enabled macro with steps on the
   same trigger, and at least one slot with a macro. */
int engine_radial_ready(const struct engine_radial *radial, const struct engine_live_config *live);

/* The mode this menu plays in. Called once, when it opens. */
int engine_radial_mode(const struct engine_radial *radial, const struct engine_live_config *live);

/* Slot for a vector, clockwise from straight up. -1 inside the dead zone. */
int engine_radial_slot_at(int64_t dx, int64_t dy, int slot_count, int32_t dead_zone);

/* Opens on a ready menu's trigger down edge, when focus allows. While open:
   1 to 8 and previous / next light a slot (keys), a left click selects
   (flick with click to pick), the trigger up selects the lit slot. Closes on
   focus, disable, a profile without that menu, and disarm. A select plays
   the slot's macro once, start to end, whatever its play mode. A Run Once
   rule: a slot macro that is still playing is not started again. */
void engine_radial_poll(struct engine_radial_state *st, struct engine_play *play,
                        const struct engine_live_config *live, const struct engine_fingers *fingers,
                        const char *front, const char *owner, int armed);

/* Raw mouse counts since the last call. Only a flick menu that is open uses them. */
void engine_radial_move(struct engine_radial_state *st, int32_t dx, int32_t dy);

/* Stop, panic, or disarm. */
void engine_radial_cancel(struct engine_radial_state *st, int why);

/* 1 while an open menu owns this id: a macro on it does not start. */
int engine_radial_owns(const struct engine_radial_state *st, uint16_t id);

/* 1 while a flick menu is open: mouse moves do not reach the cursor. */
int engine_radial_wants_motion(const struct engine_radial_state *st);

/* 1 while a flick menu with click to pick is open: left click is the menu's. */
int engine_radial_wants_click(const struct engine_radial_state *st);

/* Marks the ids to swallow: every ready menu's trigger, and while one is
   open in keys mode, 1 to 8 and its two step keys. focus_ok is the live
   profile focus, like macro triggers. */
void engine_radial_swallow(const struct engine_radial_state *st,
                           const struct engine_live_config *live, int focus_ok, uint8_t *swallow);

/* 1 when a ready menu triggers on middle or a side button, or picks with flick. */
int engine_radial_needs_mouse(const struct engine_live_config *live);

/* 1 when a ready menu uses this id as its trigger. */
int engine_radial_claims(const struct engine_live_config *live, uint16_t id);

/* "keys", "flick". */
const char *engine_radial_mode_name(int mode);

/* "played", "dead-zone", and so on. Never NULL. */
const char *engine_radial_why_name(int why, int mode);

#endif
