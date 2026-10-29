#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "profile.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct step_buf {
    struct engine_step *items;
    uint32_t n;
    uint32_t cap;
};

struct mute_buf {
    int32_t *items;
    uint32_t n;
    uint32_t cap;
};

struct built_macro {
    struct engine_macro macro;
    struct engine_profile_extra extra;
    uint32_t step_at;
};

struct built_catalog {
    struct built_macro *macros;
    int count;
    char id[ENGINE_ID_CAP];
    char name[ENGINE_PROFILE_NAME_CAP];
    char focus_exe[ENGINE_PROFILE_FOCUS_CAP];
    struct engine_radial radials[ENGINE_RADIAL_CAP];
    int radial_count;
    /* Slot macro ids, turned into indexes once the macros are final. */
    char slot_ids[ENGINE_RADIAL_CAP][ENGINE_RADIAL_SLOTS][ENGINE_ID_CAP];
};

struct build {
    struct engine_profile_note *notes;
    uint32_t note_count;
    struct engine_step *steps;
    uint32_t step_n;
    uint32_t step_cap;
    struct mute_buf mutes;
    char *ids;
    int id_count;
    const char *self_id;
    int oom;
    /* countsPer360 of the catalog being built. 0 is not calibrated. */
    double turn_per360;
    struct built_catalog flat;
    struct built_catalog *configs;
    uint32_t config_count;
    uint32_t config_cap;
};

static void copy_cap(char *dst, size_t cap, const char *src)
{
    size_t i;

    if (!dst || cap == 0)
        return;
    if (!src)
        src = "";
    for (i = 0; i + 1u < cap && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int same_text(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;

        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void trim_lower(const char *src, char *dst, size_t cap)
{
    const char *end;
    size_t n;
    size_t i;

    if (!dst || cap == 0)
        return;
    if (!src)
        src = "";
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')
        src++;
    end = src + strlen(src);
    while (end > src && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;
    n = (size_t)(end - src);
    if (n >= cap)
        n = cap - 1u;
    for (i = 0; i < n; i++) {
        char c = src[i];

        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        dst[i] = c;
    }
    dst[n] = '\0';
}

static int parse_user_number(const char *text, double *out)
{
    char *end = NULL;
    const char *s = text ? text : "";

    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    if (!*s)
        return -1;
    *out = strtod(s, &end);
    if (end == s)
        return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        end++;
    if (*end || !isfinite(*out))
        return -1;
    return 0;
}

int engine_as_num(const struct json_value *value, double fallback, double *out)
{
    double parsed = 0;

    if (!out)
        return 0;
    if (!value || value->kind == JSON_NONE || value->kind == JSON_NULL) {
        *out = fallback;
        return 0;
    }
    if (value->kind == JSON_BOOL) {
        *out = value->boolean ? 1.0 : 0.0;
        return 0;
    }
    if (value->kind == JSON_NUMBER) {
        if (!isfinite(value->number)) {
            *out = fallback;
            return 1;
        }
        *out = value->number;
        return 0;
    }
    if (value->kind == JSON_STRING) {
        if (!value->text || !value->text[0]) {
            *out = fallback;
            return 0;
        }
        if (parse_user_number(value->text, &parsed) == 0) {
            *out = parsed;
            return 0;
        }
    }
    *out = fallback;
    return 1;
}

int engine_as_bool(const struct json_value *value)
{
    char text[32];

    if (!value)
        return 0;
    if (value->kind == JSON_BOOL)
        return value->boolean ? 1 : 0;
    if (value->kind == JSON_NUMBER)
        return value->number == 1.0;
    if (value->kind != JSON_STRING && value->kind != JSON_NULL)
        return 0;
    trim_lower(value->text ? value->text : "", text, sizeof text);
    return strcmp(text, "true") == 0 || strcmp(text, "1") == 0;
}

static int value_text(const struct json_value *value, char *dst, size_t cap)
{
    if (!dst || cap == 0)
        return 0;
    dst[0] = '\0';
    if (!value)
        return 0;
    if ((value->kind == JSON_STRING || value->kind == JSON_NULL)) {
        copy_cap(dst, cap, value->text ? value->text : "");
        return 1;
    }
    if (value->kind == JSON_NUMBER && value->number_is_int) {
        snprintf(dst, cap, "%lld", value->integer);
        return 1;
    }
    return 0;
}

static int32_t half_away(double value)
{
    if (!isfinite(value))
        return 0;
    if (value >= 0)
        value = floor(value + 0.5);
    else
        value = ceil(value - 0.5);
    if (value >= 2147483647.0)
        return INT32_MAX;
    if (value <= -2147483648.0)
        return INT32_MIN;
    return (int32_t)value;
}

static int32_t floor_to_i(double value)
{
    if (!isfinite(value))
        return 0;
    value = floor(value);
    if (value >= 2147483647.0)
        return INT32_MAX;
    if (value <= -2147483648.0)
        return INT32_MIN;
    return (int32_t)value;
}

static void add_note(struct build *b, int code, const char *where)
{
    struct engine_profile_note *next;

    if (!b || b->note_count >= ENGINE_PROFILE_NOTE_CAP)
        return;
    next = (struct engine_profile_note *)realloc(b->notes,
                                                 (size_t)(b->note_count + 1u) * sizeof *next);
    if (!next)
        return;
    b->notes = next;
    memset(&b->notes[b->note_count], 0, sizeof b->notes[b->note_count]);
    b->notes[b->note_count].code = code;
    copy_cap(b->notes[b->note_count].where, ENGINE_PROFILE_WHERE_CAP, where);
    b->note_count++;
}

static void nest_where(char *dst, size_t cap, const char *parent, const char *field, uint32_t index)
{
    snprintf(dst, cap, "%s.%s[%u]", parent && parent[0] ? parent : "profile", field, index);
}

static int step_push(struct step_buf *buf, const struct engine_step *step, struct build *b)
{
    if (!buf || b->oom)
        return -1;
    if (buf->n >= buf->cap) {
        uint32_t cap = buf->cap ? buf->cap * 2u : 8u;
        struct engine_step *next;

        if (cap < buf->n + 1u)
            cap = buf->n + 1u;
        next = (struct engine_step *)realloc(buf->items, (size_t)cap * sizeof *next);
        if (!next) {
            b->oom = 1;
            return -1;
        }
        buf->items = next;
        buf->cap = cap;
    }
    buf->items[buf->n++] = *step;
    return 0;
}

static int buf_append(struct step_buf *dst, const struct step_buf *src, struct build *b)
{
    uint32_t i;

    if (!src)
        return 0;
    for (i = 0; i < src->n; i++) {
        if (step_push(dst, &src->items[i], b) != 0)
            return -1;
    }
    return 0;
}

static int arena_push(struct build *b, const struct engine_step *step)
{
    struct step_buf view;

    view.items = b->steps;
    view.n = b->step_n;
    view.cap = b->step_cap;
    if (step_push(&view, step, b) != 0)
        return -1;
    b->steps = view.items;
    b->step_n = view.n;
    b->step_cap = view.cap;
    return 0;
}

static int mute_push(struct build *b, int32_t index)
{
    if (b->oom)
        return -1;
    if (b->mutes.n >= 65535u)
        return 0;
    if (b->mutes.n >= b->mutes.cap) {
        uint32_t cap = b->mutes.cap ? b->mutes.cap * 2u : 4u;
        int32_t *next = (int32_t *)realloc(b->mutes.items, (size_t)cap * sizeof *next);

        if (!next) {
            b->oom = 1;
            return -1;
        }
        b->mutes.items = next;
        b->mutes.cap = cap;
    }
    b->mutes.items[b->mutes.n++] = index;
    return 0;
}

static const struct json_value *array_field(const struct json_value *object, const char *key)
{
    const struct json_value *value = json_object_get(object, key);

    if (!value || value->kind != JSON_ARRAY)
        return NULL;
    return value;
}

static int32_t field_i(struct build *b, const struct json_value *object, const char *key,
                       double fallback, int use_floor, const char *where)
{
    const struct json_value *value = json_object_get(object, key);
    double number = fallback;

    if (engine_as_num(value, fallback, &number))
        add_note(b, ENGINE_PROFILE_NOTE_BAD_NUMBER, where);
    return use_floor ? floor_to_i(number) : half_away(number);
}

static int field_bool(const struct json_value *object, const char *key, int fallback)
{
    const struct json_value *value = json_object_get(object, key);

    if (!value)
        return fallback;
    return engine_as_bool(value);
}

static uint16_t key_of(const char *name)
{
    uint16_t id = 0;

    if (!name || !name[0])
        return 0;
    engine_key_id(name, &id);
    return id;
}

static int id_index(const struct build *b, const char *id)
{
    int i;

    if (!b || !id || !id[0])
        return -1;
    for (i = 0; i < b->id_count; i++) {
        const char *row = b->ids + (size_t)i * ENGINE_ID_CAP;

        if (same_text(row, id))
            return i;
    }
    return -1;
}

static uint8_t action_of(const struct json_value *step)
{
    char text[16];

    if (!value_text(json_object_get(step, "action"), text, sizeof text))
        return ENGINE_ACT_TAP;
    if (same_text(text, "down"))
        return ENGINE_ACT_DOWN;
    if (same_text(text, "up"))
        return ENGINE_ACT_UP;
    return ENGINE_ACT_TAP;
}

static int stop_of(const struct json_value *step, int macro_default)
{
    char text[16];

    if (!value_text(json_object_get(step, "releaseStop"), text, sizeof text))
        return macro_default ? ENGINE_STOP_NEXT_UP : ENGINE_STOP_NONE;
    if (same_text(text, "finish"))
        return ENGINE_STOP_FINISH;
    if (same_text(text, "nextUp"))
        return ENGINE_STOP_NEXT_UP;
    return macro_default ? ENGINE_STOP_NEXT_UP : ENGINE_STOP_NONE;
}

static int known_mode(const char *text)
{
    return same_text(text, "once") || same_text(text, "repeat") || same_text(text, "whileHeld") ||
           same_text(text, "toggle") || same_text(text, "onRelease");
}

static int mode_of(const char *text)
{
    if (same_text(text, "repeat"))
        return ENGINE_MODE_REPEAT;
    if (same_text(text, "whileHeld"))
        return ENGINE_MODE_WHILE_HELD;
    if (same_text(text, "toggle"))
        return ENGINE_MODE_TOGGLE;
    if (same_text(text, "onRelease"))
        return ENGINE_MODE_ON_RELEASE;
    return ENGINE_MODE_ONCE;
}

static int pause_of(const struct json_value *value)
{
    char text[32];

    if (!value)
        return 0;
    if (value->kind == JSON_BOOL && value->boolean)
        return 0;
    if (value->kind == JSON_NUMBER && value->number == 1.0)
        return 0;
    if (!value_text(value, text, sizeof text))
        return 0;
    return engine_pause_mode(text);
}

static int ignore_of(const struct json_value *value)
{
    char text[32];
    char lower[32];

    if (!value_text(value, text, sizeof text))
        return ENGINE_IGNORE_OFF;
    trim_lower(text, lower, sizeof lower);
    return engine_ignore_mode(lower);
}

static int commit_children(struct build *b, struct step_buf *side, struct step_buf *kids,
                           struct engine_step *step)
{
    uint32_t span = side->n;

    if (buf_append(side, kids, b) != 0)
        return -1;
    step->first = kids->n ? span : 0;
    step->count = kids->n;
    return 0;
}

static void add_mutes(struct build *b, const struct json_value *block, struct engine_step *step)
{
    const struct json_value *mute = array_field(block, "mute");
    uint32_t start = b->mutes.n;
    uint32_t i;

    step->mute_at = (uint16_t)start;
    step->mute_n = 0;
    if (!mute)
        return;
    for (i = 0; i < mute->length; i++) {
        char id[ENGINE_ID_CAP];
        int index;

        if (!value_text(&mute->items[i], id, sizeof id) || !id[0])
            continue;
        if (b->self_id && same_text(b->self_id, id))
            continue;
        index = id_index(b, id);
        if (index < 0)
            continue;
        if (mute_push(b, index) != 0)
            return;
    }
    step->mute_at = (uint16_t)start;
    step->mute_n = (uint16_t)(b->mutes.n - start);
}

static int32_t next_swap_ms(struct build *b, const struct json_value *blocks, uint32_t index,
                            const char *where)
{
    const struct json_value *next;
    char type[32];
    char spot[ENGINE_PROFILE_WHERE_CAP];

    if (!blocks || index + 1u >= blocks->length)
        return 0;
    next = &blocks->items[index + 1u];
    if (next->kind != JSON_OBJECT)
        return 0;
    if (!value_text(json_object_get(next, "type"), type, sizeof type) || !same_text(type, "swapAfter"))
        return 0;
    snprintf(spot, sizeof spot, "%s.after", where);
    return field_i(b, next, "afterMs", 150, 0, spot);
}

static int compile_steps(struct build *b, const struct json_value *array, struct step_buf *dest,
                         struct step_buf *side, const char *where);

static int block_only(const char *type)
{
    return same_text(type, "whileHeld") || same_text(type, "swapAfter") || same_text(type, "then") ||
           same_text(type, "steps") || same_text(type, "tapHold") || same_text(type, "tapSpam");
}

static int compile_leaf(struct build *b, const struct json_value *step, const char *type,
                        struct step_buf *dest, struct step_buf *side, const char *where, int32_t swap_ms,
                        int from_block)
{
    struct engine_step out;
    struct step_buf kids;

    memset(&out, 0, sizeof out);
    memset(&kids, 0, sizeof kids);
    if (!from_block && block_only(type))
        return 1;
    if (same_text(type, "key") || same_text(type, "mouse")) {
        char name[64];
        const char *field = same_text(type, "mouse") ? "button" : "key";

        out.type = same_text(type, "mouse") ? (uint8_t)ENGINE_STEP_MOUSE : (uint8_t)ENGINE_STEP_KEY;
        out.action = action_of(step);
        value_text(json_object_get(step, field), name, sizeof name);
        out.key = key_of(name);
        if (out.action == ENGINE_ACT_TAP)
            out.a = field_i(b, step, "holdMs", 1, 0, where);
    } else if (same_text(type, "move")) {
        out.type = ENGINE_STEP_MOVE;
        out.a = field_i(b, step, "x", 0, 0, where);
        out.b = field_i(b, step, "y", 0, 0, where);
        if (out.a > ENGINE_REPORT_MOVE_LIMIT)
            out.a = ENGINE_REPORT_MOVE_LIMIT;
        if (out.a < -ENGINE_REPORT_MOVE_LIMIT)
            out.a = -ENGINE_REPORT_MOVE_LIMIT;
        if (out.b > ENGINE_REPORT_MOVE_LIMIT)
            out.b = ENGINE_REPORT_MOVE_LIMIT;
        if (out.b < -ENGINE_REPORT_MOVE_LIMIT)
            out.b = -ENGINE_REPORT_MOVE_LIMIT;
    } else if (same_text(type, "turn")) {
        char dir[16];
        double degrees = 180;
        double counts;

        out.type = ENGINE_STEP_TURN;
        if (engine_as_num(json_object_get(step, "degrees"), 180, &degrees))
            add_note(b, ENGINE_PROFILE_NOTE_BAD_NUMBER, where);
        if (!(degrees > 0))
            degrees = 0;
        if (degrees > ENGINE_TURN_DEGREES_MAX)
            degrees = ENGINE_TURN_DEGREES_MAX;
        out.b = field_i(b, step, "ms", ENGINE_TURN_MS_DEFAULT, 0, where);
        if (out.b < 0)
            out.b = 0;
        if (!(b->turn_per360 > 0)) {
            out.action = ENGINE_TURN_UNCALIBRATED;
        } else {
            counts = floor(degrees * b->turn_per360 / 360.0 + 0.5);
            if (counts > (double)ENGINE_TURN_COUNTS_MAX)
                counts = (double)ENGINE_TURN_COUNTS_MAX;
            out.a = (int32_t)counts;
            if (value_text(json_object_get(step, "dir"), dir, sizeof dir) && same_text(dir, "left"))
                out.a = -out.a;
        }
    } else if (same_text(type, "scroll")) {
        out.type = ENGINE_STEP_SCROLL;
        out.a = field_i(b, step, "dy", 0, 0, where);
    } else if (same_text(type, "wait") || same_text(type, "scanWait")) {
        out.type = same_text(type, "scanWait") ? (uint8_t)ENGINE_STEP_SCAN_WAIT : (uint8_t)ENGINE_STEP_WAIT;
        out.a = field_i(b, step, "ms", 0, 0, where);
    } else if (same_text(type, "randomWait")) {
        out.type = ENGINE_STEP_RANDOM_WAIT;
        out.a = field_i(b, step, "minMs", 0, 0, where);
        out.b = field_i(b, step, "maxMs", 0, 0, where);
    } else if (same_text(type, "goto")) {
        char place[16];
        int32_t ms;

        out.type = ENGINE_STEP_GOTO;
        out.a = field_i(b, step, "x", 0, 0, where);
        out.b = field_i(b, step, "y", 0, 0, where);
        if (!value_text(json_object_get(step, "where"), place, sizeof place))
            copy_cap(place, sizeof place, "screen");
        out.action = (uint8_t)engine_goto_where(place);
        ms = field_i(b, step, "ms", 15, 0, where);
        out.count = ms > 0 ? (uint32_t)ms : 0;
    } else if (same_text(type, "run")) {
        char id[ENGINE_ID_CAP];
        int index;

        out.type = ENGINE_STEP_RUN;
        value_text(json_object_get(step, "macroId"), id, sizeof id);
        index = id_index(b, id);
        if (index < 0) {
            if (id[0])
                add_note(b, ENGINE_PROFILE_NOTE_MISSING_RUN, where);
            out.a = -1;
        } else {
            out.a = index;
        }
    } else if (same_text(type, "repeat") || (from_block && same_text(type, "steps"))) {
        const struct json_value *children = array_field(step, "steps");

        out.type = ENGINE_STEP_REPEAT;
        out.a = same_text(type, "steps") ? 1 : field_i(b, step, "count", 1, 1, where);
        out.b = stop_of(step, 0);
        if (children && compile_steps(b, children, &kids, side, where) != 0) {
            free(kids.items);
            return -1;
        }
        if (commit_children(b, side, &kids, &out) != 0) {
            free(kids.items);
            return -1;
        }
    } else if (same_text(type, "holdLoop") || same_text(type, "swapLoop") ||
               (from_block && (same_text(type, "whileHeld") || same_text(type, "swapAfter")))) {
        const struct json_value *children = array_field(step, "steps");
        int swap = same_text(type, "swapLoop") || same_text(type, "swapAfter");

        out.type = swap ? (uint8_t)ENGINE_STEP_SWAP_LOOP : (uint8_t)ENGINE_STEP_HOLD_LOOP;
        out.a = swap_ms;
        out.b = stop_of(step, 0);
        if (swap)
            out.c = field_i(b, step, "afterMs", 150, 0, where);
        add_mutes(b, step, &out);
        if (children && compile_steps(b, children, &kids, side, where) != 0) {
            free(kids.items);
            return -1;
        }
        if (commit_children(b, side, &kids, &out) != 0) {
            free(kids.items);
            return -1;
        }
    } else if (same_text(type, "burst") || (from_block && same_text(type, "then"))) {
        const struct json_value *children = array_field(step, "steps");

        out.type = ENGINE_STEP_BURST;
        out.a = field_i(b, step, "forMs", 0, 0, where);
        if (children && compile_steps(b, children, &kids, side, where) != 0) {
            free(kids.items);
            return -1;
        }
        if (commit_children(b, side, &kids, &out) != 0) {
            free(kids.items);
            return -1;
        }
    } else if (same_text(type, "ifShort")) {
        const struct json_value *children = array_field(step, "steps");

        out.type = ENGINE_STEP_IF_SHORT;
        out.a = field_i(b, step, "underMs", 150, 0, where);
        out.b = field_i(b, step, "minCycles", 3, 1, where);
        if (children && compile_steps(b, children, &kids, side, where) != 0) {
            free(kids.items);
            return -1;
        }
        if (commit_children(b, side, &kids, &out) != 0) {
            free(kids.items);
            return -1;
        }
    } else if (same_text(type, "onceHeld")) {
        const struct json_value *children = array_field(step, "steps");

        out.type = ENGINE_STEP_ONCE_HELD;
        out.a = field_i(b, step, "gapMs", 70, 0, where);
        if (children && compile_steps(b, children, &kids, side, where) != 0) {
            free(kids.items);
            return -1;
        }
        if (commit_children(b, side, &kids, &out) != 0) {
            free(kids.items);
            return -1;
        }
    } else if (same_text(type, "edgeHold") ||
               (from_block && (same_text(type, "tapHold") || same_text(type, "tapSpam")))) {
        const struct json_value *watch = array_field(step, "watch");
        struct step_buf keys;
        char name[64];
        uint32_t i;
        int spam = same_text(type, "tapSpam");

        memset(&keys, 0, sizeof keys);
        if (!spam && same_text(type, "edgeHold"))
            spam = field_bool(step, "spam", 0);
        if (!json_object_get(step, "key"))
            copy_cap(name, sizeof name, "z");
        else
            value_text(json_object_get(step, "key"), name, sizeof name);
        out.type = ENGINE_STEP_EDGE_HOLD;
        out.key = key_of(name);
        out.a = field_i(b, step, "armMs", 5, 0, where);
        out.b = field_i(b, step, "gapMs", 80, 0, where);
        out.d = field_i(b, step, "holdMs", 18, 0, where);
        out.e = field_i(b, step, "restMs", 18, 0, where);
        out.f = field_i(b, step, "ignoreMs", 0, 0, where);
        out.action = engine_edge_action(spam, ignore_of(json_object_get(step, "ignore")),
                                        pause_of(json_object_get(step, "pauseWatch")));
        if (watch) {
            for (i = 0; i < watch->length; i++) {
                struct engine_step one;
                char watch_name[64];

                if (!value_text(&watch->items[i], watch_name, sizeof watch_name))
                    continue;
                memset(&one, 0, sizeof one);
                one.key = key_of(watch_name);
                if (step_push(&keys, &one, b) != 0) {
                    free(keys.items);
                    return -1;
                }
            }
        }
        if (commit_children(b, side, &keys, &out) != 0) {
            free(keys.items);
            return -1;
        }
        free(keys.items);
        keys.items = NULL;
    } else {
        free(kids.items);
        return 1;
    }
    free(kids.items);
    if (b->oom || step_push(dest, &out, b) != 0)
        return -1;
    return 0;
}

static int compile_press(struct build *b, const struct json_value *block, struct step_buf *dest,
                         struct step_buf *side, const char *where)
{
    struct step_buf kids;
    struct engine_step *expanded = NULL;
    const struct json_value *steps = array_field(block, "steps");
    const struct json_value *hold_field = json_object_get(block, "holdMs");
    char key[64];
    int omitted = hold_field == NULL;
    int32_t hold;
    int cap;
    int wrote;
    int i;

    memset(&kids, 0, sizeof kids);
    if (!json_object_get(block, "key"))
        key[0] = '\0';
    else if (!value_text(json_object_get(block, "key"), key, sizeof key))
        key[0] = '\0';
    hold = field_i(b, block, "holdMs", 200, 0, where);
    if (steps && compile_steps(b, steps, &kids, side, where) != 0) {
        free(kids.items);
        return -1;
    }
    if (kids.n > 1000000u) {
        free(kids.items);
        b->oom = 1;
        return -1;
    }
    cap = (int)kids.n + 4;
    expanded = (struct engine_step *)calloc((size_t)cap, sizeof *expanded);
    if (!expanded) {
        free(kids.items);
        b->oom = 1;
        return -1;
    }
    wrote = engine_expand_press_hold(key, hold, omitted, kids.items, kids.n, expanded, cap);
    free(kids.items);
    if (wrote < 0) {
        free(expanded);
        b->oom = 1;
        return -1;
    }
    for (i = 0; i < wrote; i++) {
        if (step_push(dest, &expanded[i], b) != 0) {
            free(expanded);
            return -1;
        }
    }
    free(expanded);
    return 0;
}

static int compile_steps(struct build *b, const struct json_value *array, struct step_buf *dest,
                         struct step_buf *side, const char *where)
{
    uint32_t i;

    if (!array)
        return 0;
    for (i = 0; i < array->length; i++) {
        char type[32];
        char spot[ENGINE_PROFILE_WHERE_CAP];
        int rc;

        nest_where(spot, sizeof spot, where, "steps", i);
        if (array->items[i].kind != JSON_OBJECT) {
            add_note(b, ENGINE_PROFILE_NOTE_SKIPPED, spot);
            continue;
        }
        if (!value_text(json_object_get(&array->items[i], "type"), type, sizeof type))
            type[0] = '\0';
        rc = compile_leaf(b, &array->items[i], type, dest, side, spot, 0, 0);
        if (rc < 0)
            return -1;
        if (rc > 0) {
            char unknown[ENGINE_PROFILE_WHERE_CAP];

            snprintf(unknown, sizeof unknown, "%s %s", spot, type[0] ? type : "empty");
            add_note(b, ENGINE_PROFILE_NOTE_UNKNOWN, unknown);
        }
    }
    return b->oom ? -1 : 0;
}

static int compile_blocks(struct build *b, const struct json_value *array, struct step_buf *dest,
                          struct step_buf *side, const char *where)
{
    uint32_t i;

    if (!array)
        return 0;
    for (i = 0; i < array->length; i++) {
        char type[32];
        char spot[ENGINE_PROFILE_WHERE_CAP];
        int32_t swap_ms;
        int rc;

        nest_where(spot, sizeof spot, where, "blocks", i);
        if (array->items[i].kind != JSON_OBJECT) {
            add_note(b, ENGINE_PROFILE_NOTE_SKIPPED, spot);
            continue;
        }
        if (!value_text(json_object_get(&array->items[i], "type"), type, sizeof type))
            type[0] = '\0';
        if (same_text(type, "pressHold")) {
            if (compile_press(b, &array->items[i], dest, side, spot) != 0)
                return -1;
            continue;
        }
        swap_ms = 0;
        if (same_text(type, "whileHeld") || same_text(type, "swapAfter"))
            swap_ms = next_swap_ms(b, array, i, spot);
        rc = compile_leaf(b, &array->items[i], type, dest, side, spot, swap_ms, 1);
        if (rc < 0)
            return -1;
        if (rc > 0) {
            char unknown[ENGINE_PROFILE_WHERE_CAP];

            snprintf(unknown, sizeof unknown, "%s %s", spot, type[0] ? type : "empty");
            add_note(b, ENGINE_PROFILE_NOTE_UNKNOWN, unknown);
        }
    }
    return b->oom ? -1 : 0;
}

static int commit_macro_steps(struct build *b, struct step_buf *top, struct step_buf *side,
                              struct built_macro *out)
{
    uint32_t at = b->step_n;
    uint32_t i;

    for (i = 0; i < top->n; i++) {
        if (arena_push(b, &top->items[i]) != 0)
            return -1;
    }
    for (i = 0; i < side->n; i++) {
        if (arena_push(b, &side->items[i]) != 0)
            return -1;
    }
    for (i = 0; i < top->n + side->n; i++) {
        struct engine_step *step = &b->steps[at + i];

        if (step->count > 0)
            step->first += top->n;
    }
    out->step_at = at;
    out->macro.step_count = top->n + side->n;
    out->macro.top_count = top->n;
    return 0;
}

static int compile_one_macro(struct build *b, const struct json_value *macro, struct built_macro *out,
                             const char *where)
{
    struct step_buf top;
    struct step_buf side;
    const struct json_value *blocks;
    const struct json_value *trigger;
    const struct json_value *mode_value;
    char mode_text[32];
    char button[64];
    char kind[16];
    double speed = 1;
    int advanced;
    int basic;

    memset(out, 0, sizeof *out);
    memset(&top, 0, sizeof top);
    memset(&side, 0, sizeof side);
    engine_macro_init(&out->macro);
    value_text(json_object_get(macro, "id"), out->macro.id, sizeof out->macro.id);
    value_text(json_object_get(macro, "name"), out->extra.name, sizeof out->extra.name);
    value_text(json_object_get(macro, "focusExe"), out->extra.focus_exe, sizeof out->extra.focus_exe);
    out->extra.enabled = field_bool(macro, "enabled", 1);
    if (json_object_get(macro, "priority")) {
        out->extra.has_priority = 1;
        out->extra.priority = field_i(b, macro, "priority", 0, 0, where);
    }
    if (engine_as_num(json_object_get(macro, "speed"), 1, &speed))
        add_note(b, ENGINE_PROFILE_NOTE_BAD_NUMBER, where);
    if (!isfinite(speed))
        speed = 1;
    out->macro.speed = speed;
    mode_value = json_object_get(macro, "playMode");
    if (mode_value && value_text(mode_value, mode_text, sizeof mode_text)) {
        if (!known_mode(mode_text))
            add_note(b, ENGINE_PROFILE_NOTE_UNKNOWN, where);
        out->macro.mode = mode_of(mode_text);
    } else if (mode_value) {
        add_note(b, ENGINE_PROFILE_NOTE_UNKNOWN, where);
        out->macro.mode = ENGINE_MODE_ONCE;
    }
    out->macro.repeat_count = field_i(b, macro, "repeatCount", 1, 1, where);
    out->macro.busy = field_bool(macro, "busy", 0);
    out->macro.exclusive = field_bool(macro, "exclusive", 0);
    out->macro.release_stop = stop_of(macro, 1);
    out->macro.gap_ms = field_i(b, macro, "gapMs", 18, 0, where);
    trigger = json_object_get(macro, "trigger");
    if (trigger && trigger->kind == JSON_OBJECT) {
        value_text(json_object_get(trigger, "button"), button, sizeof button);
        value_text(json_object_get(trigger, "kind"), kind, sizeof kind);
        out->macro.trigger = key_of(button);
        out->macro.trigger_is_key = same_text(kind, "key");
    }
    b->self_id = out->macro.id;
    advanced = field_bool(macro, "advanced", 0);
    basic = field_bool(macro, "basic", 0);
    blocks = array_field(macro, "blocks");
    if (advanced && blocks && blocks->length > 0) {
        if (compile_blocks(b, blocks, &top, &side, where) != 0) {
            free(top.items);
            free(side.items);
            return -1;
        }
        out->macro.advanced = 1;
        out->macro.basic = 0;
    } else {
        const struct json_value *steps = array_field(macro, "steps");

        if (steps && compile_steps(b, steps, &top, &side, where) != 0) {
            free(top.items);
            free(side.items);
            return -1;
        }
        out->macro.advanced = advanced;
        out->macro.basic = basic;
    }
    if (commit_macro_steps(b, &top, &side, out) != 0) {
        free(top.items);
        free(side.items);
        return -1;
    }
    free(top.items);
    free(side.items);
    out->macro.mutes = b->mutes.items;
    out->macro.mute_count = b->mutes.n;
    b->mutes.items = NULL;
    b->mutes.n = 0;
    b->mutes.cap = 0;
    b->self_id = NULL;
    return b->oom ? -1 : 0;
}

static int compile_catalog(struct build *b, const struct json_value *array, struct built_catalog *cat,
                           const char *where)
{
    uint32_t i;
    int count = 0;
    int used = 0;
    char *ids;

    if (!array)
        return 0;
    for (i = 0; i < array->length; i++) {
        char spot[ENGINE_PROFILE_WHERE_CAP];

        if (array->items[i].kind == JSON_OBJECT) {
            count++;
            continue;
        }
        nest_where(spot, sizeof spot, where, "macros", i);
        add_note(b, ENGINE_PROFILE_NOTE_SKIPPED, spot);
    }
    if (count <= 0)
        return 0;
    ids = (char *)calloc((size_t)count, ENGINE_ID_CAP);
    cat->macros = (struct built_macro *)calloc((size_t)count, sizeof *cat->macros);
    if (!ids || !cat->macros) {
        free(ids);
        free(cat->macros);
        cat->macros = NULL;
        b->oom = 1;
        return -1;
    }
    cat->count = count;
    for (i = 0; i < array->length; i++) {
        if (array->items[i].kind != JSON_OBJECT)
            continue;
        value_text(json_object_get(&array->items[i], "id"), ids + (size_t)used * ENGINE_ID_CAP,
                   ENGINE_ID_CAP);
        used++;
    }
    b->ids = ids;
    b->id_count = count;
    used = 0;
    for (i = 0; i < array->length; i++) {
        char spot[ENGINE_PROFILE_WHERE_CAP];

        if (array->items[i].kind != JSON_OBJECT)
            continue;
        nest_where(spot, sizeof spot, where, "macros", i);
        if (compile_one_macro(b, &array->items[i], &cat->macros[used], spot) != 0) {
            free(ids);
            b->ids = NULL;
            b->id_count = 0;
            return -1;
        }
        used++;
    }
    free(ids);
    b->ids = NULL;
    b->id_count = 0;
    return 0;
}

static void free_built_catalog(struct built_catalog *cat)
{
    int i;

    if (!cat)
        return;
    if (cat->macros) {
        for (i = 0; i < cat->count; i++)
            free((void *)cat->macros[i].macro.mutes);
        free(cat->macros);
    }
    memset(cat, 0, sizeof *cat);
}

static void build_clear(struct build *b)
{
    uint32_t i;

    if (!b)
        return;
    free(b->notes);
    free(b->steps);
    free(b->mutes.items);
    free(b->ids);
    free_built_catalog(&b->flat);
    if (b->configs) {
        for (i = 0; i < b->config_count; i++)
            free_built_catalog(&b->configs[i]);
        free(b->configs);
    }
    memset(b, 0, sizeof *b);
}

static void free_catalog(struct engine_catalog *cat)
{
    int i;

    if (!cat)
        return;
    if (cat->macros) {
        for (i = 0; i < cat->count; i++)
            free((void *)cat->macros[i].mutes);
        free(cat->macros);
    }
    free(cat->extras);
    free(cat->trigger_next);
    memset(cat, 0, sizeof *cat);
}

void engine_profile_image_free(struct engine_profile_image *image)
{
    uint32_t i;

    if (!image)
        return;
    free(image->steps);
    free(image->notes);
    free_catalog(&image->flat);
    if (image->configs) {
        for (i = 0; i < image->config_count; i++)
            free_catalog(&image->configs[i]);
        free(image->configs);
    }
    free(image);
}

static void build_triggers(struct engine_catalog *cat)
{
    int i;

    memset(cat->trigger_head, 0, sizeof cat->trigger_head);
    if (!cat->trigger_next || cat->count <= 0)
        return;
    memset(cat->trigger_next, 0, (size_t)cat->count * sizeof *cat->trigger_next);
    for (i = 0; i < cat->count; i++) {
        uint16_t id = cat->macros[i].trigger;
        uint32_t head;

        if (id == 0 || id >= ENGINE_TRIGGER_SLOTS || (uint32_t)i + 1u > 65535u)
            continue;
        head = cat->trigger_head[id];
        if (head == 0) {
            cat->trigger_head[id] = (uint16_t)(i + 1);
            continue;
        }
        while (head > 0 && cat->trigger_next[head - 1u] != 0)
            head = cat->trigger_next[head - 1u];
        if (head > 0)
            cat->trigger_next[head - 1u] = (uint32_t)i + 1u;
    }
}

/* Slot ids to macro indexes. An id that is not in this config is an empty slot. */
static void finalize_radials(struct engine_catalog *dst, const struct built_catalog *src)
{
    int r;

    dst->radial_count = src->radial_count;
    for (r = 0; r < src->radial_count; r++) {
        int s;

        dst->radials[r] = src->radials[r];
        for (s = 0; s < dst->radials[r].slot_count; s++) {
            int m;

            dst->radials[r].slot[s] = -1;
            if (!src->slot_ids[r][s][0])
                continue;
            for (m = 0; m < dst->count; m++) {
                if (same_text(dst->macros[m].id, src->slot_ids[r][s])) {
                    dst->radials[r].slot[s] = m;
                    break;
                }
            }
        }
    }
}

static int finalize_catalog(struct engine_catalog *dst, struct built_catalog *src,
                            struct engine_step *steps)
{
    int i;

    memset(dst, 0, sizeof *dst);
    copy_cap(dst->id, sizeof dst->id, src->id);
    copy_cap(dst->name, sizeof dst->name, src->name);
    copy_cap(dst->focus_exe, sizeof dst->focus_exe, src->focus_exe);
    if (src->count <= 0) {
        finalize_radials(dst, src);
        return 0;
    }
    dst->macros = (struct engine_macro *)calloc((size_t)src->count, sizeof *dst->macros);
    dst->extras = (struct engine_profile_extra *)calloc((size_t)src->count, sizeof *dst->extras);
    dst->trigger_next = (uint32_t *)calloc((size_t)src->count, sizeof *dst->trigger_next);
    if (!dst->macros || !dst->extras || !dst->trigger_next) {
        free_catalog(dst);
        return -1;
    }
    dst->count = src->count;
    for (i = 0; i < src->count; i++) {
        dst->macros[i] = src->macros[i].macro;
        src->macros[i].macro.mutes = NULL;
        dst->macros[i].steps = NULL;
        if (dst->macros[i].step_count > 0 && steps)
            dst->macros[i].steps = steps + src->macros[i].step_at;
        dst->extras[i] = src->macros[i].extra;
    }
    build_triggers(dst);
    finalize_radials(dst, src);
    return 0;
}

static struct engine_profile_image *make_empty(void)
{
    struct engine_profile_image *image = (struct engine_profile_image *)calloc(1, sizeof *image);

    if (!image)
        return NULL;
    copy_cap(image->name, sizeof image->name, "Default");
    image->has_variables = 1;
    return image;
}

static uint16_t radial_key(const struct json_value *object, const char *field, const char *fallback)
{
    char name[64];

    if (!value_text(json_object_get(object, field), name, sizeof name) || !name[0])
        copy_cap(name, sizeof name, fallback);
    return key_of(name);
}

/* config.radials, like normalizeRadial in ui/src/profile.ts. A select this
   engine does not know plays as keys. Left and right click are never a trigger. */
static void compile_radials(struct build *b, const struct json_value *list, struct built_catalog *cat,
                            const char *where)
{
    uint32_t i;

    if (!list || list->kind != JSON_ARRAY)
        return;
    for (i = 0; i < list->length && cat->radial_count < ENGINE_RADIAL_CAP; i++) {
        const struct json_value *item = &list->items[i];
        const struct json_value *trigger;
        const struct json_value *slots;
        struct engine_radial *radial;
        char spot[ENGINE_PROFILE_WHERE_CAP];
        char text[64];
        int r = cat->radial_count;
        uint32_t s;

        if (item->kind != JSON_OBJECT)
            continue;
        nest_where(spot, sizeof spot, where, "radials", i);
        radial = &cat->radials[r];
        memset(radial, 0, sizeof *radial);
        memset(cat->slot_ids[r], 0, sizeof cat->slot_ids[r]);
        value_text(json_object_get(item, "id"), radial->id, sizeof radial->id);
        value_text(json_object_get(item, "name"), radial->name, sizeof radial->name);
        radial->enabled = field_bool(item, "enabled", 1);
        trigger = json_object_get(item, "trigger");
        if (trigger && trigger->kind == JSON_OBJECT &&
            value_text(json_object_get(trigger, "button"), text, sizeof text))
            radial->trigger = key_of(text);
        if (radial->trigger == ENGINE_KEY_LBUTTON || radial->trigger == ENGINE_KEY_RBUTTON)
            radial->trigger = 0;
        radial->select = value_text(json_object_get(item, "select"), text, sizeof text) &&
                                 same_text(text, "flick")
                             ? ENGINE_RADIAL_FLICK
                             : ENGINE_RADIAL_KEYS;
        radial->prev_key = radial_key(item, "prevKey", "q");
        radial->next_key = radial_key(item, "nextKey", "e");
        radial->at = value_text(json_object_get(item, "at"), text, sizeof text) && same_text(text, "cursor")
                         ? ENGINE_RADIAL_AT_CURSOR
                         : ENGINE_RADIAL_AT_CENTER;
        radial->dead_zone = field_i(b, item, "deadZone", ENGINE_RADIAL_DEAD_ZONE, 0, spot);
        if (radial->dead_zone < 1)
            radial->dead_zone = ENGINE_RADIAL_DEAD_ZONE;
        radial->click_select = field_bool(item, "clickSelect", 0);
        radial->size = field_i(b, item, "size", ENGINE_RADIAL_SIZE, 0, spot);
        if (radial->size < ENGINE_RADIAL_SIZE_MIN)
            radial->size = ENGINE_RADIAL_SIZE_MIN;
        if (radial->size > ENGINE_RADIAL_SIZE_MAX)
            radial->size = ENGINE_RADIAL_SIZE_MAX;
        radial->opacity = field_i(b, item, "opacity", ENGINE_RADIAL_OPACITY, 0, spot);
        if (radial->opacity < ENGINE_RADIAL_OPACITY_MIN)
            radial->opacity = ENGINE_RADIAL_OPACITY_MIN;
        if (radial->opacity > ENGINE_RADIAL_OPACITY_MAX)
            radial->opacity = ENGINE_RADIAL_OPACITY_MAX;
        slots = array_field(item, "slots");
        for (s = 0; slots && s < slots->length && s < ENGINE_RADIAL_SLOTS; s++)
            value_text(&slots->items[s], cat->slot_ids[r][s], ENGINE_ID_CAP);
        radial->slot_count = slots ? (int)(slots->length < ENGINE_RADIAL_SLOTS ? slots->length : ENGINE_RADIAL_SLOTS) : 0;
        if (radial->slot_count < 2)
            radial->slot_count = 2;
        cat->radial_count++;
    }
}

/* turn.countsPer360 of a config, or of the profile when it has no configs.
   Missing, 0, negative, and not a number are 0: not calibrated. */
static double turn_per360(const struct json_value *owner)
{
    const struct json_value *turn = json_object_get(owner, "turn");
    double per360 = 0;

    if (!turn || turn->kind != JSON_OBJECT)
        return 0;
    if (engine_as_num(json_object_get(turn, "countsPer360"), 0, &per360) || !(per360 > 0))
        return 0;
    if (per360 > (double)ENGINE_TURN_COUNTS_MAX)
        return (double)ENGINE_TURN_COUNTS_MAX;
    return per360;
}

int engine_profile_compile(const char *text, size_t length, struct engine_profile_image **out)
{
    struct json_value root;
    struct build b;
    struct engine_profile_image *image;
    const struct json_value *configs;
    uint32_t i;

    if (!out)
        return -1;
    *out = NULL;
    if (!text)
        return -1;
    if (json_parse(text, length, &root) != 0)
        return -1;
    if (root.kind != JSON_OBJECT) {
        json_free(&root);
        return -1;
    }
    memset(&b, 0, sizeof b);
    configs = json_object_get(&root, "configs");
    if (configs && configs->kind == JSON_ARRAY && configs->length > 0) {
        for (i = 0; i < configs->length; i++) {
            char spot[ENGINE_PROFILE_WHERE_CAP];
            struct built_catalog *next;

            nest_where(spot, sizeof spot, "profile", "configs", i);
            if (configs->items[i].kind != JSON_OBJECT) {
                add_note(&b, ENGINE_PROFILE_NOTE_SKIPPED, spot);
                continue;
            }
            if (b.config_count >= b.config_cap) {
                uint32_t cap = b.config_cap ? b.config_cap * 2u : 4u;
                next = (struct built_catalog *)realloc(b.configs, (size_t)cap * sizeof *next);

                if (!next) {
                    b.oom = 1;
                    break;
                }
                memset(next + b.config_cap, 0, (size_t)(cap - b.config_cap) * sizeof *next);
                b.configs = next;
                b.config_cap = cap;
            }
            value_text(json_object_get(&configs->items[i], "id"), b.configs[b.config_count].id,
                       sizeof b.configs[b.config_count].id);
            value_text(json_object_get(&configs->items[i], "name"), b.configs[b.config_count].name,
                       sizeof b.configs[b.config_count].name);
            value_text(json_object_get(&configs->items[i], "focusExe"),
                       b.configs[b.config_count].focus_exe,
                       sizeof b.configs[b.config_count].focus_exe);
            b.turn_per360 = turn_per360(&configs->items[i]);
            compile_radials(&b, json_object_get(&configs->items[i], "radials"), &b.configs[b.config_count],
                            spot);
            if (compile_catalog(&b, array_field(&configs->items[i], "macros"), &b.configs[b.config_count],
                                spot) != 0) {
                free_built_catalog(&b.configs[b.config_count]);
                b.oom = 1;
                break;
            }
            b.config_count++;
        }
    } else {
        b.turn_per360 = turn_per360(&root);
        if (compile_catalog(&b, array_field(&root, "macros"), &b.flat, "profile") != 0)
            b.oom = 1;
    }
    if (b.oom) {
        json_free(&root);
        build_clear(&b);
        return -1;
    }
    image = (struct engine_profile_image *)calloc(1, sizeof *image);
    if (!image) {
        json_free(&root);
        build_clear(&b);
        return -1;
    }
    value_text(json_object_get(&root, "name"), image->name, sizeof image->name);
    value_text(json_object_get(&root, "activeId"), image->active_id, sizeof image->active_id);
    value_text(json_object_get(&root, "focusExe"), image->focus_exe, sizeof image->focus_exe);
    image->swap_clicks = engine_as_bool(json_object_get(&root, "swapClicks"));
    image->has_variables = json_object_get(&root, "variables") != NULL;
    image->has_configs = configs && configs->kind == JSON_ARRAY && configs->length > 0;
    image->steps = b.steps;
    image->step_count = b.step_n;
    image->notes = b.notes;
    image->note_count = b.note_count;
    b.steps = NULL;
    b.notes = NULL;
    b.step_n = 0;
    b.note_count = 0;
    if (!image->has_configs) {
        if (finalize_catalog(&image->flat, &b.flat, image->steps) != 0) {
            json_free(&root);
            engine_profile_image_free(image);
            build_clear(&b);
            return -1;
        }
    } else {
        image->configs = (struct engine_catalog *)calloc(b.config_count ? b.config_count : 1u,
                                                         sizeof *image->configs);
        if (!image->configs) {
            json_free(&root);
            engine_profile_image_free(image);
            build_clear(&b);
            return -1;
        }
        image->config_count = b.config_count;
        for (i = 0; i < b.config_count; i++) {
            if (finalize_catalog(&image->configs[i], &b.configs[i], image->steps) != 0) {
                json_free(&root);
                engine_profile_image_free(image);
                build_clear(&b);
                return -1;
            }
        }
    }
    json_free(&root);
    build_clear(&b);
    *out = image;
    return 0;
}

static void live_clear(struct engine_live_config *out)
{
    if (out)
        memset(out, 0, sizeof *out);
}

static void use_catalog(struct engine_live_config *out, const struct engine_catalog *cat)
{
    if (!cat)
        return;
    out->macros = cat->macros;
    out->extras = cat->extras;
    out->trigger_head = cat->trigger_head;
    out->trigger_next = cat->trigger_next;
    out->macro_count = cat->count;
    out->radials = cat->radials;
    out->radial_count = cat->radial_count;
    copy_cap(out->focus_exe, sizeof out->focus_exe, cat->focus_exe);
    copy_cap(out->name, sizeof out->name, cat->name);
}

void engine_profile_pick(const struct engine_profile_image *image, const char *front,
                         const char *prefer, struct engine_live_config *out)
{
    uint32_t i;
    int chosen = -1;
    int fallback = -1;

    live_clear(out);
    if (!out)
        return;
    copy_cap(out->prefer, sizeof out->prefer, prefer);
    if (!image) {
        return;
    }
    if (!image->has_configs) {
        use_catalog(out, &image->flat);
        copy_cap(out->live_id, sizeof out->live_id, "-");
        copy_cap(out->active_id, sizeof out->active_id, image->active_id);
        copy_cap(out->prefer, sizeof out->prefer, image->active_id);
        copy_cap(out->name, sizeof out->name, image->name);
        copy_cap(out->focus_exe, sizeof out->focus_exe, image->focus_exe);
        return;
    }
    for (i = 0; i < image->config_count; i++) {
        const char *exe = image->configs[i].focus_exe;

        if (!exe[0]) {
            if (fallback < 0)
                fallback = (int)i;
            else if (same_text(image->configs[i].id, prefer ? prefer : ""))
                fallback = (int)i;
            continue;
        }
        if (front && front[0] && engine_focus_match(exe, front)) {
            chosen = (int)i;
            break;
        }
    }
    if (chosen < 0)
        chosen = fallback;
    if (chosen < 0)
        return;
    use_catalog(out, &image->configs[chosen]);
    copy_cap(out->live_id, sizeof out->live_id, image->configs[chosen].id);
    copy_cap(out->active_id, sizeof out->active_id, image->configs[chosen].id);
    copy_cap(out->focus_exe, sizeof out->focus_exe, image->configs[chosen].focus_exe);
}

static void reap_retired(struct engine_profile_slot *slot)
{
    uint64_t adopted;
    uintptr_t live;
    struct engine_profile_image **link;

    if (!slot)
        return;
    adopted = atomic_load_explicit(&slot->adopted, memory_order_acquire);
    live = atomic_load_explicit(&slot->published, memory_order_acquire);
    link = &slot->retired;
    while (*link) {
        struct engine_profile_image *image = *link;

        if ((uintptr_t)image != live && image->sequence != 0 && image->sequence < adopted) {
            *link = image->retired_next;
            image->retired_next = NULL;
            engine_profile_image_free(image);
            continue;
        }
        link = &image->retired_next;
    }
}

void engine_profile_slot_init(struct engine_profile_slot *slot)
{
    if (!slot)
        return;
    memset(slot, 0, sizeof *slot);
    atomic_init(&slot->published, (uintptr_t)0);
    atomic_init(&slot->adopted, (uint64_t)0);
}

void engine_profile_slot_publish(struct engine_profile_slot *slot, struct engine_profile_image *image)
{
    uintptr_t prev_bits;
    struct engine_profile_image *prev;

    if (!slot || !image)
        return;
    if (slot->next_seq == UINT64_MAX)
        slot->next_seq = 0;
    image->sequence = ++slot->next_seq;
    image->retired_next = NULL;
    prev_bits = atomic_exchange_explicit(&slot->published, (uintptr_t)image, memory_order_acq_rel);
    prev = (struct engine_profile_image *)prev_bits;
    if (prev) {
        prev->retired_next = slot->retired;
        slot->retired = prev;
    }
    reap_retired(slot);
}

const struct engine_profile_image *engine_profile_slot_adopt(struct engine_profile_slot *slot)
{
    uintptr_t bits;
    struct engine_profile_image *image;

    if (!slot)
        return NULL;
    bits = atomic_load_explicit(&slot->published, memory_order_acquire);
    image = (struct engine_profile_image *)bits;
    if (image && image->sequence != 0)
        atomic_store_explicit(&slot->adopted, image->sequence, memory_order_release);
    return image;
}

void engine_profile_slot_shutdown(struct engine_profile_slot *slot)
{
    uintptr_t bits;
    struct engine_profile_image *image;
    struct engine_profile_image *next;

    if (!slot)
        return;
    bits = atomic_exchange_explicit(&slot->published, (uintptr_t)0, memory_order_acq_rel);
    engine_profile_image_free((struct engine_profile_image *)bits);
    for (image = slot->retired; image; image = next) {
        next = image->retired_next;
        engine_profile_image_free(image);
    }
    slot->retired = NULL;
    slot->failed = 0;
    slot->detail[0] = '\0';
}

static void set_read_error(struct engine_profile_slot *slot)
{
    slot->failed = 1;
    copy_cap(slot->detail, sizeof slot->detail, ENGINE_PROFILE_READ_DETAIL);
}

int engine_profile_slot_fail(struct engine_profile_slot *slot)
{
    if (!slot)
        return -1;
    set_read_error(slot);
    if (atomic_load_explicit(&slot->published, memory_order_acquire) == 0) {
        struct engine_profile_image *empty = make_empty();

        if (empty)
            engine_profile_slot_publish(slot, empty);
    }
    return -1;
}

int engine_profile_slot_load_text(struct engine_profile_slot *slot, const char *text, size_t length)
{
    struct engine_profile_image *image = NULL;

    if (!slot)
        return -1;
    if (engine_profile_compile(text, length, &image) != 0 || !image)
        return engine_profile_slot_fail(slot);
    slot->failed = 0;
    slot->detail[0] = '\0';
    engine_profile_slot_publish(slot, image);
    return 0;
}

int engine_profile_slot_load_missing(struct engine_profile_slot *slot)
{
    struct engine_profile_image *image;

    if (!slot)
        return -1;
    image = make_empty();
    if (!image)
        return engine_profile_slot_fail(slot);
    slot->failed = 0;
    slot->detail[0] = '\0';
    engine_profile_slot_publish(slot, image);
    return 0;
}

int engine_profile_slot_load_file(struct engine_profile_slot *slot, const char *path)
{
    FILE *file;
    long size;
    char *buf;
    size_t got;
    int rc;

    if (!slot)
        return -1;
    if (!path || !path[0])
        return engine_profile_slot_fail(slot);
    file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT)
            return engine_profile_slot_load_missing(slot);
        return engine_profile_slot_fail(slot);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return engine_profile_slot_fail(slot);
    }
    size = ftell(file);
    if (size < 0 || (unsigned long)size > ENGINE_PROFILE_TEXT_MAX) {
        fclose(file);
        return engine_profile_slot_fail(slot);
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return engine_profile_slot_fail(slot);
    }
    buf = (char *)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(file);
        return engine_profile_slot_fail(slot);
    }
    got = fread(buf, 1, (size_t)size, file);
    fclose(file);
    if (got != (size_t)size) {
        free(buf);
        return engine_profile_slot_fail(slot);
    }
    buf[got] = '\0';
    rc = engine_profile_slot_load_text(slot, buf, got);
    free(buf);
    return rc;
}

const char *engine_profile_slot_detail(const struct engine_profile_slot *slot)
{
    if (!slot || !slot->failed)
        return "";
    return slot->detail;
}

int engine_profile_slot_apply_document(struct engine_profile_slot *slot, const char *text,
                                       size_t length)
{
    struct json_value root;
    struct engine_profile_image *image = NULL;

    if (!slot)
        return 0;
    if (!text) {
        text = "";
        length = 0;
    }
    if (json_parse(text, length, &root) != 0 || root.kind != JSON_OBJECT) {
        if (root.kind != JSON_NONE)
            json_free(&root);
        return 0;
    }
    json_free(&root);
    if (engine_profile_compile(text, length, &image) != 0 || !image)
        return 0;
    slot->failed = 0;
    slot->detail[0] = '\0';
    engine_profile_slot_publish(slot, image);
    return 1;
}

void engine_state_slot_init(struct engine_state_slot *slot)
{
    if (!slot)
        return;
    memset(slot, 0, sizeof *slot);
    atomic_init(&slot->published, -1);
}

void engine_state_publish(struct engine_state_slot *slot, int armed, const char *active_id,
                          const char *front)
{
    int cur;
    int next;
    struct engine_state_snapshot *dst;

    if (!slot)
        return;
    cur = atomic_load_explicit(&slot->published, memory_order_acquire);
    next = cur == 0 ? 1 : 0;
    dst = &slot->copy[next];
    memset(dst, 0, sizeof *dst);
    dst->armed = armed ? 1 : 0;
    copy_cap(dst->active_id, sizeof dst->active_id, active_id);
    copy_cap(dst->front, sizeof dst->front, front);
    atomic_store_explicit(&slot->published, next, memory_order_release);
}

void engine_state_publish_snapshot(struct engine_state_slot *slot,
                                   const struct engine_state_snapshot *snap)
{
    int cur;
    int next;
    struct engine_state_snapshot *dst;

    if (!slot || !snap)
        return;
    cur = atomic_load_explicit(&slot->published, memory_order_acquire);
    next = cur == 0 ? 1 : 0;
    dst = &slot->copy[next];
    *dst = *snap;
    if (dst->running_count < 0)
        dst->running_count = 0;
    if (dst->running_count > ENGINE_STATE_RUNNING_CAP)
        dst->running_count = ENGINE_STATE_RUNNING_CAP;
    if (dst->held_count < 0)
        dst->held_count = 0;
    if (dst->held_count > ENGINE_STATE_HELD_CAP)
        dst->held_count = ENGINE_STATE_HELD_CAP;
    dst->armed = dst->armed ? 1 : 0;
    atomic_store_explicit(&slot->published, next, memory_order_release);
}

void engine_state_read(const struct engine_state_slot *slot, struct engine_state_snapshot *out)
{
    int cur;

    if (!out)
        return;
    memset(out, 0, sizeof *out);
    if (!slot)
        return;
    cur = atomic_load_explicit(&slot->published, memory_order_acquire);
    if (cur != 0 && cur != 1)
        return;
    *out = slot->copy[cur];
}
