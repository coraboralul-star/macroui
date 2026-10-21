/* Profile load. The IPC thread compiles an immutable image.
   Playback adopts a newer image and does not allocate.
   Field, AsNum, and AsBool match engine/MacroEngine.ahk.
   priority and variables are kept and not read.
   swapClicks is read. 1, true, "true", and "1" set it.
   See docs/engine/plan.md. */
#ifndef ENGINE_PROFILE_H
#define ENGINE_PROFILE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "json.h"
#include "runner.h"

#define ENGINE_PROFILE_NAME_CAP 128
#define ENGINE_PROFILE_FOCUS_CAP 260
#define ENGINE_PROFILE_WHERE_CAP 80
#define ENGINE_PROFILE_NOTE_CAP 128
#define ENGINE_PROFILE_TEXT_MAX (8u * 1024u * 1024u)
#define ENGINE_TRIGGER_SLOTS (ENGINE_KEY_ID_MAX + 1u)

#define ENGINE_PROFILE_READ_DETAIL "the saved profile could not be read"

#define ENGINE_PROFILE_NOTE_UNKNOWN 1
#define ENGINE_PROFILE_NOTE_BAD_NUMBER 2
#define ENGINE_PROFILE_NOTE_MISSING_RUN 3
#define ENGINE_PROFILE_NOTE_SKIPPED 4

/* 1 when a bad value fell back to fallback. A missing value uses fallback and returns 0.
   null and "" are missing for this purpose: the key was present as an empty string,
   and AsNum returns the fallback without counting a failed parse. */
int engine_as_num(const struct json_value *value, double fallback, double *out);

/* True for boolean true, numeric 1, or "true" / "1" after trim and lower case. */
int engine_as_bool(const struct json_value *value);

struct engine_profile_note {
    int code;
    char where[ENGINE_PROFILE_WHERE_CAP];
};

struct engine_profile_extra {
    char name[ENGINE_PROFILE_NAME_CAP];
    char focus_exe[ENGINE_PROFILE_FOCUS_CAP];
    int enabled;
    int has_priority;
    int32_t priority;
};

/* trigger_head is 1-based into this catalog, or 0. trigger_next is 1-based too.
   The chain is profile order for one key id. Mouse ids are included. */
struct engine_catalog {
    struct engine_macro *macros;
    struct engine_profile_extra *extras;
    int count;
    char id[ENGINE_ID_CAP];
    char name[ENGINE_PROFILE_NAME_CAP];
    char focus_exe[ENGINE_PROFILE_FOCUS_CAP];
    uint16_t trigger_head[ENGINE_TRIGGER_SLOTS];
    uint32_t *trigger_next;
};

struct engine_profile_image {
    uint64_t sequence;
    struct engine_profile_image *retired_next;
    char name[ENGINE_PROFILE_NAME_CAP];
    char active_id[ENGINE_ID_CAP];
    char focus_exe[ENGINE_PROFILE_FOCUS_CAP];
    int swap_clicks;
    int has_variables;
    int has_configs;
    struct engine_catalog flat;
    struct engine_catalog *configs;
    uint32_t config_count;
    struct engine_step *steps;
    uint32_t step_count;
    struct engine_profile_note *notes;
    uint32_t note_count;
};

/* The live catalog. live_id is "-" when the file has no configs array.
   active_id is the snapshot field: the file id when live_id is "-", otherwise live_id.
   prefer is the activeId the caller passed. A hit does not replace it. */
struct engine_live_config {
    const struct engine_macro *macros;
    const struct engine_profile_extra *extras;
    const uint16_t *trigger_head;
    const uint32_t *trigger_next;
    int macro_count;
    char live_id[ENGINE_ID_CAP];
    char active_id[ENGINE_ID_CAP];
    char prefer[ENGINE_ID_CAP];
    char name[ENGINE_PROFILE_NAME_CAP];
    char focus_exe[ENGINE_PROFILE_FOCUS_CAP];
};

#define ENGINE_STATE_RUNNING_CAP 32
#define ENGINE_STATE_HELD_CAP 64

struct engine_state_row {
    char id[ENGINE_ID_CAP];
    char name[ENGINE_PROFILE_NAME_CAP];
};

struct engine_state_snapshot {
    int armed;
    char active_id[ENGINE_ID_CAP];
    char front[ENGINE_PROFILE_FOCUS_CAP];
    int running_count;
    struct engine_state_row running[ENGINE_STATE_RUNNING_CAP];
    int held_count;
    uint16_t held[ENGINE_STATE_HELD_CAP];
};

struct engine_state_slot {
    struct engine_state_snapshot copy[2];
    atomic_int published;
};

struct engine_profile_slot {
    atomic_uintptr_t published;
    atomic_uint_fast64_t adopted;
    struct engine_profile_image *retired;
    uint64_t next_seq;
    int failed;
    char detail[64];
};

/* 0 and *out set. -1 on a bad document. *out is NULL then.
   A missing steps array compiles as empty. A missing run target stores -1.
   Notes are recorded. They do not reject the document. */
int engine_profile_compile(const char *text, size_t length, struct engine_profile_image **out);

void engine_profile_image_free(struct engine_profile_image *image);

/* front and prefer may be null. They mean empty. Does not allocate. */
void engine_profile_pick(const struct engine_profile_image *image, const char *front,
                         const char *prefer, struct engine_live_config *out);

void engine_profile_slot_init(struct engine_profile_slot *slot);

/* Takes ownership. Frees an older image only after playback has adopted a newer one. */
void engine_profile_slot_publish(struct engine_profile_slot *slot, struct engine_profile_image *image);

/* Acquire the published image. Does not allocate or free. */
const struct engine_profile_image *engine_profile_slot_adopt(struct engine_profile_slot *slot);

void engine_profile_slot_shutdown(struct engine_profile_slot *slot);

/* 0 when a new image is published. -1 keeps the last image, or an empty one,
   and sets the profile-read detail. */
int engine_profile_slot_load_text(struct engine_profile_slot *slot, const char *text, size_t length);

/* The empty document LoadDisk would write. Does not create a file. */
int engine_profile_slot_load_missing(struct engine_profile_slot *slot);

int engine_profile_slot_fail(struct engine_profile_slot *slot);

/* ASCII and UTF-8 paths that fopen can open. A missing file is the empty document. */
int engine_profile_slot_load_file(struct engine_profile_slot *slot, const char *path);

/* Profile frame. 1 when a JSON object was published. 0 applies nothing:
   a parse failure or a value that is not an object. Does not fail the slot
   and does not send the profile-read error. That error is the file reload. */
int engine_profile_slot_apply_document(struct engine_profile_slot *slot, const char *text,
                                       size_t length);

const char *engine_profile_slot_detail(const struct engine_profile_slot *slot);

void engine_state_slot_init(struct engine_state_slot *slot);

/* Fills the inactive copy, then publishes it. Does not allocate.
   Running rows and held ids are cleared. */
void engine_state_publish(struct engine_state_slot *slot, int armed, const char *active_id,
                          const char *front);

/* Publishes a full snapshot. Caps are the caller's. Does not allocate. */
void engine_state_publish_snapshot(struct engine_state_slot *slot,
                                   const struct engine_state_snapshot *snap);

void engine_state_read(const struct engine_state_slot *slot, struct engine_state_snapshot *out);

#endif
