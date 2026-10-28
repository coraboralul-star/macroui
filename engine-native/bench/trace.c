#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

/* Trace corpus for the rest of #49.
   One profile and one input script, played on the fake clock.
   Output is key, move, wheel, and goto lines. This file does not call SendInput.
   engine/TracePlay.ahk plays the same script. bench/expected is this player's output.
   A Windows run diffs the AHK output against that file. */

#include "runner.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define JS_NULL 0
#define JS_BOOL 1
#define JS_NUM 2
#define JS_STR 3
#define JS_ARR 4
#define JS_OBJ 5

#define JS_NODE_CAP 2048
#define JS_ARENA (256 * 1024)
#define FILE_CAP (256 * 1024)
#define MACRO_CAP 16
#define EVENT_CAP 256
#define OUT_CAP (128 * 1024)
#define UNTIL_CAP 10000

struct js {
    int kind;
    int boolean;
    double num;
    char *str;
    char *key;
    struct js *child;
    struct js *next;
};

struct built {
    struct engine_macro macro;
    struct engine_step steps[ENGINE_STEP_CAP];
    int32_t mutes[64];
    int step_n;
    int mute_n;
};

struct cat {
    char id[MACRO_CAP][ENGINE_ID_CAP];
    int n;
};

struct event {
    int64_t time;
    int kind;
    char a[64];
    char b[64];
    int32_t x;
    int32_t y;
};

#define EV_FINGER 1
#define EV_START 2
#define EV_STOP 3
#define EV_ASK 4
#define EV_MOUSE 5
#define EV_WINDOW 6
#define EV_CLIENT 7

struct script {
    char profile[256];
    int until;
    int has_until;
    int64_t roll;
    int has_roll;
    struct event ev[EVENT_CAP];
    int ev_n;
};

struct buf {
    char data[OUT_CAP];
    int n;
    int overflow;
};

static struct js g_nodes[JS_NODE_CAP];
static int g_node_n;
static char g_arena[JS_ARENA];
static int g_arena_n;
static char g_file[FILE_CAP];
static char g_err[512];
static struct engine_play g_play;
static struct built g_built[MACRO_CAP];
static struct engine_macro g_macros[MACRO_CAP];
static int g_macro_n;
/* The profile's turn.countsPer360. 0 is not calibrated. */
static double g_turn_per360;
static int64_t g_roll;

static void set_err(const char *msg)
{
    snprintf(g_err, sizeof g_err, "%s", msg);
}

static void reset_json(void)
{
    g_node_n = 0;
    g_arena_n = 0;
}

static char *intern(const char *s, int n)
{
    char *p;

    if (n < 0 || g_arena_n > JS_ARENA - n - 1)
        return NULL;
    p = g_arena + g_arena_n;
    memcpy(p, s, (size_t)n);
    p[n] = '\0';
    g_arena_n += n + 1;
    return p;
}

static struct js *new_node(int kind)
{
    struct js *n;

    if (g_node_n >= JS_NODE_CAP)
        return NULL;
    n = &g_nodes[g_node_n++];
    memset(n, 0, sizeof *n);
    n->kind = kind;
    return n;
}

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r')
        (*p)++;
}

static struct js *parse_value(const char **p);

static struct js *parse_string(const char **p)
{
    char tmp[4096];
    int n = 0;
    struct js *node;

    if (**p != '"')
        return NULL;
    (*p)++;
    while (**p && **p != '"') {
        char c = **p;
        if ((unsigned char)c < 0x20)
            return NULL;
        if (c == '\\') {
            (*p)++;
            c = **p;
            if (c == '"' || c == '\\' || c == '/')
                ;
            else if (c == 'n')
                c = '\n';
            else if (c == 't')
                c = '\t';
            else if (c == 'r')
                c = '\r';
            else if (c == 'b')
                c = '\b';
            else if (c == 'f')
                c = '\f';
            else if (c == 'u') {
                int h;
                int v = 0;
                if (!(*p)[1] || !(*p)[2] || !(*p)[3] || !(*p)[4])
                    return NULL;
                for (h = 1; h <= 4; h++) {
                    char d = (*p)[h];
                    v *= 16;
                    if (d >= '0' && d <= '9')
                        v += d - '0';
                    else if (d >= 'a' && d <= 'f')
                        v += d - 'a' + 10;
                    else if (d >= 'A' && d <= 'F')
                        v += d - 'A' + 10;
                    else
                        return NULL;
                }
                if (v > 0x7F)
                    return NULL;
                c = (char)v;
                *p += 4;
            } else {
                return NULL;
            }
        }
        if (n >= (int)sizeof tmp - 1)
            return NULL;
        tmp[n++] = c;
        (*p)++;
    }
    if (**p != '"')
        return NULL;
    (*p)++;
    node = new_node(JS_STR);
    if (!node)
        return NULL;
    node->str = intern(tmp, n);
    if (!node->str)
        return NULL;
    return node;
}

static struct js *parse_number(const char **p)
{
    const char *start = *p;
    char tmp[64];
    int n;
    struct js *node;
    char *end;

    if (**p == '-')
        (*p)++;
    if (**p < '0' || **p > '9')
        return NULL;
    while (**p >= '0' && **p <= '9')
        (*p)++;
    if (**p == '.') {
        (*p)++;
        if (**p < '0' || **p > '9')
            return NULL;
        while (**p >= '0' && **p <= '9')
            (*p)++;
    }
    if (**p == 'e' || **p == 'E') {
        (*p)++;
        if (**p == '+' || **p == '-')
            (*p)++;
        if (**p < '0' || **p > '9')
            return NULL;
        while (**p >= '0' && **p <= '9')
            (*p)++;
    }
    n = (int)(*p - start);
    if (n <= 0 || n >= (int)sizeof tmp)
        return NULL;
    memcpy(tmp, start, (size_t)n);
    tmp[n] = '\0';
    node = new_node(JS_NUM);
    if (!node)
        return NULL;
    node->num = strtod(tmp, &end);
    if (end == tmp)
        return NULL;
    return node;
}

static struct js *parse_array(const char **p)
{
    struct js *node;
    struct js *last = NULL;

    node = new_node(JS_ARR);
    if (!node)
        return NULL;
    (*p)++;
    skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return node;
    }
    for (;;) {
        struct js *v = parse_value(p);
        if (!v)
            return NULL;
        if (!node->child)
            node->child = v;
        else
            last->next = v;
        last = v;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            skip_ws(p);
            continue;
        }
        if (**p == ']') {
            (*p)++;
            return node;
        }
        return NULL;
    }
}

static struct js *parse_object(const char **p)
{
    struct js *node;
    struct js *last = NULL;

    node = new_node(JS_OBJ);
    if (!node)
        return NULL;
    (*p)++;
    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return node;
    }
    for (;;) {
        struct js *key;
        struct js *v;
        skip_ws(p);
        key = parse_string(p);
        if (!key)
            return NULL;
        skip_ws(p);
        if (**p != ':')
            return NULL;
        (*p)++;
        v = parse_value(p);
        if (!v)
            return NULL;
        v->key = key->str;
        if (!node->child)
            node->child = v;
        else
            last->next = v;
        last = v;
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == '}') {
            (*p)++;
            return node;
        }
        return NULL;
    }
}

static struct js *parse_word(const char **p, const char *word, int kind, int boolean)
{
    int n = (int)strlen(word);
    struct js *node;

    if (strncmp(*p, word, (size_t)n) != 0)
        return NULL;
    *p += n;
    node = new_node(kind);
    if (!node)
        return NULL;
    node->boolean = boolean;
    return node;
}

static struct js *parse_value(const char **p)
{
    skip_ws(p);
    if (**p == '"')
        return parse_string(p);
    if (**p == '{')
        return parse_object(p);
    if (**p == '[')
        return parse_array(p);
    if (**p == '-' || (**p >= '0' && **p <= '9'))
        return parse_number(p);
    if (**p == 't')
        return parse_word(p, "true", JS_BOOL, 1);
    if (**p == 'f')
        return parse_word(p, "false", JS_BOOL, 0);
    if (**p == 'n')
        return parse_word(p, "null", JS_NULL, 0);
    return NULL;
}

static int read_file(const char *path, char *dst, int cap)
{
    FILE *f;
    long n;
    int i;
    int w;

    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    n = ftell(f);
    if (n < 0 || n >= cap) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    if (n > 0 && fread(dst, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    dst[n] = '\0';
    i = 0;
    if (n >= 3 && (unsigned char)dst[0] == 0xEF && (unsigned char)dst[1] == 0xBB &&
        (unsigned char)dst[2] == 0xBF)
        i = 3;
    w = 0;
    while (dst[i]) {
        if (dst[i] != '\r')
            dst[w++] = dst[i];
        i++;
    }
    dst[w] = '\0';
    return w;
}

static struct js *parse_json(const char *text)
{
    const char *p = text;
    struct js *root;

    reset_json();
    root = parse_value(&p);
    if (!root)
        return NULL;
    skip_ws(&p);
    if (*p)
        return NULL;
    return root;
}

static const struct js *jget(const struct js *obj, const char *key)
{
    const struct js *m;

    if (!obj || obj->kind != JS_OBJ)
        return NULL;
    for (m = obj->child; m; m = m->next) {
        if (m->key && strcmp(m->key, key) == 0)
            return m;
    }
    return NULL;
}

static int jstr(const struct js *n, const char **out)
{
    if (!n || n->kind != JS_STR || !n->str)
        return 0;
    *out = n->str;
    return 1;
}

static int ji32(const struct js *n, int32_t *out)
{
    if (!n || n->kind != JS_NUM)
        return 0;
    if (n->num > 2147483647.0 || n->num < -2147483648.0)
        return 0;
    *out = (int32_t)n->num;
    return 1;
}

static int jbool(const struct js *n, int *out)
{
    if (!n)
        return 0;
    if (n->kind == JS_BOOL) {
        *out = n->boolean;
        return 1;
    }
    if (n->kind == JS_NUM) {
        *out = n->num != 0.0;
        return 1;
    }
    return 0;
}

static int is_type(const struct js *obj, const char *type)
{
    const char *s = NULL;

    if (!jstr(jget(obj, "type"), &s))
        return 0;
    return strcmp(s, type) == 0;
}

static int key_survives(const char *name)
{
    int i;
    uint16_t id = 0;

    if (!name || !name[0] || strlen(name) >= 64)
        return 0;
    if (engine_key_id(name, &id))
        return 1;
    for (i = 0; name[i]; i++) {
        char c = name[i];
        int ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!ok)
            return 0;
    }
    return 1;
}

static int known_block(const struct js *item)
{
    return is_type(item, "whileHeld") || is_type(item, "ifShort") || is_type(item, "swapAfter") ||
           is_type(item, "then") || is_type(item, "repeat") || is_type(item, "wait") ||
           is_type(item, "steps") || is_type(item, "onceHeld") || is_type(item, "tapHold") ||
           is_type(item, "tapSpam") || is_type(item, "pressHold");
}

static int press_slots(const struct js *block)
{
    const struct js *steps = jget(block, "steps");
    const struct js *item;
    const char *key = "";
    int n = 0;
    int prefix;

    if (steps && steps->kind == JS_ARR) {
        for (item = steps->child; item; item = item->next) {
            if (item->kind == JS_OBJ)
                n++;
        }
    }
    jstr(jget(block, "key"), &key);
    prefix = key_survives(key) && n == 0 ? 3 : 0;
    return prefix + n + 1;
}

static int block_slots(const struct js *item)
{
    if (!item || item->kind != JS_OBJ || !known_block(item))
        return 0;
    if (is_type(item, "pressHold"))
        return press_slots(item);
    return 1;
}

static int step_slots(const struct js *item)
{
    if (!item || item->kind != JS_OBJ)
        return 0;
    return 1;
}

static int cat_find(const struct cat *cat, const char *id)
{
    int i;

    if (!cat || !id)
        return -1;
    for (i = 0; i < cat->n; i++) {
        if (strcmp(cat->id[i], id) == 0)
            return i;
    }
    return -1;
}

static int stop_code(const struct js *obj)
{
    const char *s = NULL;

    if (!jstr(jget(obj, "releaseStop"), &s))
        return ENGINE_STOP_NONE;
    if (strcmp(s, "finish") == 0)
        return ENGINE_STOP_FINISH;
    if (strcmp(s, "nextUp") == 0)
        return ENGINE_STOP_NEXT_UP;
    return ENGINE_STOP_NONE;
}

static int32_t field_i(const struct js *obj, const char *key, int32_t missing)
{
    int32_t v = missing;

    if (!ji32(jget(obj, key), &v))
        return missing;
    return v;
}

static int32_t lookahead_swap(const struct js *next)
{
    if (!next || next->kind != JS_OBJ || !is_type(next, "swapAfter"))
        return 0;
    if (!jget(next, "afterMs"))
        return 150;
    return field_i(next, "afterMs", 150);
}

static int compile_sequence(struct built *b, const struct js *list, int as_blocks, const struct cat *cat,
                            const char *owner, uint32_t *first, uint32_t *count);

static int append_watch(struct built *b, const struct js *list, uint32_t *first, uint32_t *count)
{
    const struct js *item;
    int base = b->step_n;
    int n = 0;

    *first = 0;
    *count = 0;
    if (!list || list->kind != JS_ARR)
        return 1;
    for (item = list->child; item; item = item->next) {
        uint16_t id = 0;
        if (item->kind != JS_STR)
            continue;
        if (b->step_n >= ENGINE_STEP_CAP)
            return 0;
        memset(&b->steps[b->step_n], 0, sizeof b->steps[0]);
        engine_key_id(item->str, &id);
        b->steps[b->step_n].key = id;
        b->step_n++;
        n++;
    }
    *first = (uint32_t)base;
    *count = (uint32_t)n;
    return 1;
}

static int append_mutes(struct built *b, const struct js *list, const struct cat *cat, uint16_t *at,
                        uint16_t *n)
{
    const struct js *item;
    int base = b->mute_n;
    int count = 0;

    *at = 0;
    *n = 0;
    if (!list || list->kind != JS_ARR)
        return 1;
    for (item = list->child; item; item = item->next) {
        int idx;
        if (item->kind != JS_STR)
            continue;
        idx = cat_find(cat, item->str);
        if (idx < 0)
            continue;
        if (b->mute_n >= (int)(sizeof b->mutes / sizeof b->mutes[0]))
            return 0;
        b->mutes[b->mute_n++] = idx;
        count++;
    }
    if (count > 0) {
        *at = (uint16_t)base;
        *n = (uint16_t)count;
    }
    return 1;
}

static int attach_steps(struct built *b, const struct js *item, const struct cat *cat, const char *owner,
                        struct engine_step *out)
{
    uint32_t first = 0;
    uint32_t count = 0;

    if (!compile_sequence(b, jget(item, "steps"), 0, cat, owner, &first, &count))
        return 0;
    out->first = first;
    out->count = count;
    return 1;
}

static int fill_one(struct built *b, const struct js *item, int as_blocks, const struct js *next,
                    const struct cat *cat, const char *owner, struct engine_step *out)
{
    const char *type = "";
    uint16_t id = 0;

    memset(out, 0, sizeof *out);
    jstr(jget(item, "type"), &type);
    if (as_blocks && strcmp(type, "whileHeld") == 0) {
        out->type = ENGINE_STEP_HOLD_LOOP;
        out->a = lookahead_swap(next);
        out->b = stop_code(item);
        if (!append_mutes(b, jget(item, "mute"), cat, &out->mute_at, &out->mute_n))
            return 0;
        return attach_steps(b, item, cat, owner, out);
    }
    if (as_blocks && strcmp(type, "swapAfter") == 0) {
        out->type = ENGINE_STEP_SWAP_LOOP;
        out->a = lookahead_swap(next);
        out->b = stop_code(item);
        out->c = jget(item, "afterMs") ? field_i(item, "afterMs", 150) : 150;
        if (!append_mutes(b, jget(item, "mute"), cat, &out->mute_at, &out->mute_n))
            return 0;
        return attach_steps(b, item, cat, owner, out);
    }
    if (as_blocks && strcmp(type, "then") == 0) {
        out->type = ENGINE_STEP_BURST;
        out->a = field_i(item, "forMs", 0);
        return attach_steps(b, item, cat, owner, out);
    }
    if (as_blocks && strcmp(type, "ifShort") == 0) {
        out->type = ENGINE_STEP_IF_SHORT;
        out->a = field_i(item, "underMs", 150);
        out->b = field_i(item, "minCycles", 3);
        return attach_steps(b, item, cat, owner, out);
    }
    if (as_blocks && strcmp(type, "onceHeld") == 0) {
        out->type = ENGINE_STEP_ONCE_HELD;
        out->a = field_i(item, "gapMs", 70);
        return attach_steps(b, item, cat, owner, out);
    }
    if (as_blocks && (strcmp(type, "tapHold") == 0 || strcmp(type, "tapSpam") == 0)) {
        const char *key = "z";
        const char *ignore = "off";
        const char *pause = "off";
        uint32_t first = 0;
        uint32_t count = 0;
        jstr(jget(item, "key"), &key);
        jstr(jget(item, "ignore"), &ignore);
        jstr(jget(item, "pauseWatch"), &pause);
        out->type = ENGINE_STEP_EDGE_HOLD;
        engine_key_id(key, &id);
        out->key = id;
        out->a = field_i(item, "armMs", 5);
        out->b = field_i(item, "gapMs", 80);
        out->d = field_i(item, "holdMs", 18);
        out->e = field_i(item, "restMs", 18);
        out->f = field_i(item, "ignoreMs", 0);
        out->action = engine_edge_action(strcmp(type, "tapSpam") == 0, engine_ignore_mode(ignore),
                                         engine_pause_mode(pause));
        if (!append_watch(b, jget(item, "watch"), &first, &count))
            return 0;
        out->first = first;
        out->count = count;
        return 1;
    }
    if (as_blocks && strcmp(type, "repeat") == 0) {
        out->type = ENGINE_STEP_REPEAT;
        out->a = field_i(item, "count", 1);
        out->b = stop_code(item);
        return attach_steps(b, item, cat, owner, out);
    }
    if (as_blocks && strcmp(type, "steps") == 0) {
        out->type = ENGINE_STEP_REPEAT;
        out->a = 1;
        out->b = stop_code(item);
        return attach_steps(b, item, cat, owner, out);
    }
    if (as_blocks && strcmp(type, "wait") == 0) {
        out->type = ENGINE_STEP_WAIT;
        out->a = field_i(item, "ms", 0);
        return 1;
    }
    if (strcmp(type, "key") == 0 || strcmp(type, "mouse") == 0) {
        const char *name = "";
        const char *action = "tap";
        jstr(jget(item, strcmp(type, "key") == 0 ? "key" : "button"), &name);
        jstr(jget(item, "action"), &action);
        engine_key_id(name, &id);
        out->type = strcmp(type, "key") == 0 ? ENGINE_STEP_KEY : ENGINE_STEP_MOUSE;
        out->key = id;
        if (strcmp(action, "down") == 0)
            out->action = ENGINE_ACT_DOWN;
        else if (strcmp(action, "up") == 0)
            out->action = ENGINE_ACT_UP;
        else {
            out->action = ENGINE_ACT_TAP;
            out->a = field_i(item, "holdMs", 1);
        }
        return 1;
    }
    if (strcmp(type, "move") == 0) {
        out->type = ENGINE_STEP_MOVE;
        out->a = field_i(item, "x", 0);
        out->b = field_i(item, "y", 0);
        return 1;
    }
    if (strcmp(type, "turn") == 0) {
        const struct js *deg = jget(item, "degrees");
        const char *dir = "right";
        double degrees = deg && deg->kind == JS_NUM ? deg->num : 180;

        jstr(jget(item, "dir"), &dir);
        out->type = ENGINE_STEP_TURN;
        out->b = field_i(item, "ms", ENGINE_TURN_MS_DEFAULT);
        if (out->b < 0)
            out->b = 0;
        if (!(degrees > 0))
            degrees = 0;
        if (degrees > ENGINE_TURN_DEGREES_MAX)
            degrees = ENGINE_TURN_DEGREES_MAX;
        if (!(g_turn_per360 > 0)) {
            out->action = ENGINE_TURN_UNCALIBRATED;
        } else {
            out->a = (int32_t)floor(degrees * g_turn_per360 / 360.0 + 0.5);
            if (strcmp(dir, "left") == 0)
                out->a = -out->a;
        }
        return 1;
    }
    if (strcmp(type, "scroll") == 0) {
        out->type = ENGINE_STEP_SCROLL;
        out->a = field_i(item, "dy", 0);
        return 1;
    }
    if (strcmp(type, "wait") == 0) {
        out->type = ENGINE_STEP_WAIT;
        out->a = field_i(item, "ms", 0);
        return 1;
    }
    if (strcmp(type, "scanWait") == 0) {
        out->type = ENGINE_STEP_SCAN_WAIT;
        out->a = field_i(item, "ms", 0);
        return 1;
    }
    if (strcmp(type, "randomWait") == 0) {
        out->type = ENGINE_STEP_RANDOM_WAIT;
        out->a = field_i(item, "minMs", 0);
        out->b = field_i(item, "maxMs", 0);
        return 1;
    }
    if (strcmp(type, "repeat") == 0) {
        out->type = ENGINE_STEP_REPEAT;
        out->a = field_i(item, "count", 1);
        out->b = stop_code(item);
        return attach_steps(b, item, cat, owner, out);
    }
    if (strcmp(type, "goto") == 0) {
        const char *where = NULL;
        int32_t ms = 15;
        jstr(jget(item, "where"), &where);
        if (jget(item, "ms"))
            ms = field_i(item, "ms", 15);
        out->type = ENGINE_STEP_GOTO;
        out->a = field_i(item, "x", 0);
        out->b = field_i(item, "y", 0);
        out->action = (uint8_t)engine_goto_where(where);
        out->count = ms > 0 ? (uint32_t)ms : 0;
        return 1;
    }
    if (strcmp(type, "run") == 0) {
        const char *id_name = "";
        jstr(jget(item, "macroId"), &id_name);
        out->type = ENGINE_STEP_RUN;
        out->a = cat_find(cat, id_name);
        return 1;
    }
    return 1;
}

static int fill_press(struct built *b, const struct js *block, const struct cat *cat, const char *owner,
                      struct engine_step *out, int cap)
{
    struct engine_step kids[64];
    const struct js *steps = jget(block, "steps");
    const struct js *item;
    const char *key = "";
    int kn = 0;
    int omitted;
    int32_t hold = 0;

    if (steps && steps->kind == JS_ARR) {
        for (item = steps->child; item; item = item->next) {
            if (item->kind != JS_OBJ)
                continue;
            if (kn >= (int)(sizeof kids / sizeof kids[0]))
                return -1;
            if (!fill_one(b, item, 0, NULL, cat, owner, &kids[kn]))
                return -1;
            kn++;
        }
    }
    jstr(jget(block, "key"), &key);
    omitted = jget(block, "holdMs") == NULL;
    if (!omitted)
        hold = field_i(block, "holdMs", 0);
    return engine_expand_press_hold(key, hold, omitted, kids, (uint32_t)kn, out, cap);
}

static int compile_sequence(struct built *b, const struct js *list, int as_blocks, const struct cat *cat,
                            const char *owner, uint32_t *first, uint32_t *count)
{
    const struct js *item;
    int slots = 0;
    int base;
    int slot = 0;

    *first = (uint32_t)b->step_n;
    *count = 0;
    if (!list || list->kind != JS_ARR)
        return 1;
    for (item = list->child; item; item = item->next)
        slots += as_blocks ? block_slots(item) : step_slots(item);
    if (slots < 0 || b->step_n > ENGINE_STEP_CAP - slots)
        return 0;
    base = b->step_n;
    if (slots > 0)
        memset(&b->steps[base], 0, (size_t)slots * sizeof b->steps[0]);
    b->step_n = base + slots;
    for (item = list->child; item; item = item->next) {
        int n = as_blocks ? block_slots(item) : step_slots(item);
        int wrote;
        if (n == 0)
            continue;
        if (as_blocks && is_type(item, "pressHold")) {
            wrote = fill_press(b, item, cat, owner, &b->steps[base + slot], n);
            if (wrote != n)
                return 0;
            slot += wrote;
            continue;
        }
        if (!fill_one(b, item, as_blocks, item->next, cat, owner, &b->steps[base + slot]))
            return 0;
        slot++;
    }
    if (slot != slots)
        return 0;
    *first = (uint32_t)base;
    *count = (uint32_t)slots;
    return 1;
}

static int mode_of(const char *s)
{
    if (!s)
        return ENGINE_MODE_ONCE;
    if (strcmp(s, "repeat") == 0)
        return ENGINE_MODE_REPEAT;
    if (strcmp(s, "whileHeld") == 0)
        return ENGINE_MODE_WHILE_HELD;
    if (strcmp(s, "toggle") == 0)
        return ENGINE_MODE_TOGGLE;
    if (strcmp(s, "onRelease") == 0)
        return ENGINE_MODE_ON_RELEASE;
    return ENGINE_MODE_ONCE;
}

static void copy_id(char *dst, const char *src)
{
    int i;

    if (!src)
        src = "";
    for (i = 0; i < ENGINE_ID_CAP - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int compile_profile(const struct js *root)
{
    const struct js *macros;
    const struct js *item;
    struct cat cat;
    int i;

    memset(&cat, 0, sizeof cat);
    g_macro_n = 0;
    {
        const struct js *per = jget(jget(root, "turn"), "countsPer360");

        g_turn_per360 = per && per->kind == JS_NUM && per->num > 0 ? per->num : 0;
    }
    macros = jget(root, "macros");
    if (!macros || macros->kind != JS_ARR) {
        set_err("profile has no macros array");
        return 0;
    }
    for (item = macros->child; item; item = item->next) {
        const char *id = "";
        if (item->kind != JS_OBJ)
            continue;
        if (cat.n >= MACRO_CAP) {
            set_err("too many macros");
            return 0;
        }
        jstr(jget(item, "id"), &id);
        copy_id(cat.id[cat.n], id);
        cat.n++;
    }
    i = 0;
    for (item = macros->child; item; item = item->next) {
        struct built *b;
        const struct js *trig;
        const char *id = "";
        const char *mode = "once";
        const char *button = "";
        const char *kind = "";
        const char *stop = NULL;
        int advanced = 0;
        int basic = 0;
        int flag = 0;
        uint32_t first = 0;
        uint32_t count = 0;
        const struct js *blocks;
        if (item->kind != JS_OBJ)
            continue;
        b = &g_built[i];
        memset(b, 0, sizeof *b);
        engine_macro_init(&b->macro);
        jstr(jget(item, "id"), &id);
        copy_id(b->macro.id, id);
        if (jget(item, "speed") && jget(item, "speed")->kind == JS_NUM)
            b->macro.speed = jget(item, "speed")->num;
        jstr(jget(item, "playMode"), &mode);
        b->macro.mode = mode_of(mode);
        b->macro.repeat_count = field_i(item, "repeatCount", 1);
        if (jbool(jget(item, "busy"), &flag))
            b->macro.busy = flag;
        if (jbool(jget(item, "exclusive"), &flag))
            b->macro.exclusive = flag;
        if (jstr(jget(item, "releaseStop"), &stop) && strcmp(stop, "finish") == 0)
            b->macro.release_stop = ENGINE_STOP_FINISH;
        jbool(jget(item, "advanced"), &advanced);
        jbool(jget(item, "basic"), &basic);
        trig = jget(item, "trigger");
        if (trig && trig->kind == JS_OBJ) {
            uint16_t kid = 0;
            jstr(jget(trig, "button"), &button);
            jstr(jget(trig, "kind"), &kind);
            engine_key_id(button, &kid);
            b->macro.trigger = kid;
            b->macro.trigger_is_key = strcmp(kind, "key") == 0;
        }
        b->macro.gap_ms = field_i(item, "gapMs", 18);
        blocks = jget(item, "blocks");
        if (advanced && blocks && blocks->kind == JS_ARR && blocks->child) {
            if (!compile_sequence(b, blocks, 1, &cat, b->macro.id, &first, &count)) {
                set_err("block compile failed");
                return 0;
            }
            b->macro.steps = b->steps;
            b->macro.step_count = (uint32_t)b->step_n;
            b->macro.top_count = count;
            b->macro.mutes = b->mutes;
            b->macro.mute_count = (uint32_t)b->mute_n;
            b->macro.advanced = 1;
        } else if (!(basic && engine_basic_rewrite(b->macro.trigger, b->macro.trigger_is_key,
                                                   b->macro.gap_ms, b->steps, 4) > 0)) {
            if (!compile_sequence(b, jget(item, "steps"), 0, &cat, b->macro.id, &first, &count)) {
                set_err("step compile failed");
                return 0;
            }
            b->macro.steps = b->step_n ? b->steps : NULL;
            b->macro.step_count = (uint32_t)b->step_n;
            b->macro.top_count = count;
            b->macro.basic = basic;
        } else {
            b->macro.basic = 1;
        }
        g_macros[i] = b->macro;
        i++;
    }
    g_macro_n = i;
    return 1;
}

static int path_ok(const char *rel)
{
    if (!rel || !rel[0] || rel[0] == '/' || rel[0] == '\\')
        return 0;
    if (strstr(rel, ".."))
        return 0;
    return 1;
}

static int join_path(char *dst, int cap, const char *a, const char *b)
{
    int n = snprintf(dst, (size_t)cap, "%s/%s", a, b);
    return n > 0 && n < cap;
}

static int load_script(const char *path, struct script *script)
{
    char text[65536];
    char *line;
    int n;

    memset(script, 0, sizeof *script);
    n = read_file(path, text, (int)sizeof text);
    if (n < 0) {
        set_err("cannot read case");
        return 0;
    }
    line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        char verb[32];
        char a[64];
        char b[64];
        char which[32];
        long long t = 0;
        int x = 0;
        int y = 0;
        int got;
        if (nl)
            *nl = '\0';
        if (line[0] != '\0' && line[0] != '#') {
            if (strncmp(line, "profile ", 8) == 0) {
                snprintf(script->profile, sizeof script->profile, "%s", line + 8);
            } else if (strncmp(line, "until ", 6) == 0) {
                script->until = atoi(line + 6);
                script->has_until = 1;
            } else if (strncmp(line, "roll ", 5) == 0) {
                script->roll = (int64_t)atoll(line + 5);
                script->has_roll = 1;
            } else if (sscanf(line, "%lld pointer %31s %d %d", &t, which, &x, &y) == 4) {
                struct event *ev;
                if (script->ev_n >= EVENT_CAP) {
                    set_err("too many events");
                    return 0;
                }
                ev = &script->ev[script->ev_n++];
                ev->time = (int64_t)t;
                ev->x = x;
                ev->y = y;
                if (strcmp(which, "mouse") == 0)
                    ev->kind = EV_MOUSE;
                else if (strcmp(which, "window") == 0)
                    ev->kind = EV_WINDOW;
                else if (strcmp(which, "client") == 0)
                    ev->kind = EV_CLIENT;
                else {
                    set_err("bad pointer");
                    return 0;
                }
            } else {
                got = sscanf(line, "%lld %31s %63s %63s", &t, verb, a, b);
                if (got < 2) {
                    set_err("bad event");
                    return 0;
                }
                if (script->ev_n >= EVENT_CAP) {
                    set_err("too many events");
                    return 0;
                }
                if (strcmp(verb, "finger") == 0 && got == 4) {
                    struct event *ev = &script->ev[script->ev_n++];
                    ev->time = (int64_t)t;
                    ev->kind = EV_FINGER;
                    snprintf(ev->a, sizeof ev->a, "%s", a);
                    ev->x = strcmp(b, "down") == 0;
                } else if (strcmp(verb, "start") == 0 && got >= 3) {
                    struct event *ev = &script->ev[script->ev_n++];
                    ev->time = (int64_t)t;
                    ev->kind = EV_START;
                    snprintf(ev->a, sizeof ev->a, "%s", a);
                } else if (strcmp(verb, "stop") == 0 && got >= 3) {
                    struct event *ev = &script->ev[script->ev_n++];
                    ev->time = (int64_t)t;
                    ev->kind = EV_STOP;
                    snprintf(ev->a, sizeof ev->a, "%s", a);
                } else if (strcmp(verb, "ask") == 0 && got >= 3) {
                    struct event *ev = &script->ev[script->ev_n++];
                    ev->time = (int64_t)t;
                    ev->kind = EV_ASK;
                    snprintf(ev->a, sizeof ev->a, "%s", a);
                } else {
                    set_err("bad event");
                    return 0;
                }
            }
        }
        if (!nl)
            break;
        line = nl + 1;
    }
    if (!script->has_until || !path_ok(script->profile)) {
        set_err("case needs profile and until");
        return 0;
    }
    if (script->until < 0 || script->until > UNTIL_CAP) {
        set_err("until is out of range");
        return 0;
    }
    return 1;
}

static int64_t roll_fn(int64_t lo, int64_t hi, void *ctx)
{
    (void)lo;
    (void)hi;
    (void)ctx;
    return g_roll;
}

static struct engine_macro *find_macro(const char *id)
{
    int i;

    for (i = 0; i < g_macro_n; i++) {
        if (strcmp(g_macros[i].id, id) == 0)
            return &g_macros[i];
    }
    return NULL;
}

static int format_name(uint16_t id, char *dst, int cap)
{
    static const struct {
        uint16_t id;
        const char *name;
    } table[] = {
        {0x20, "space"},   {0x09, "tab"},     {0x0D, "enter"},   {0x1B, "escape"},
        {0x08, "backspace"}, {0x26, "up"},    {0x28, "down"},    {0x25, "left"},
        {0x27, "right"},   {0x2D, "Insert"},  {0x2E, "Delete"},  {0xA0, "LShift"},
        {0xA1, "RShift"},  {0xA2, "LCtrl"},   {0xA3, "RCtrl"},   {0xA4, "LAlt"},
        {0xA5, "RAlt"},    {0x5B, "LWin"},    {0x5C, "RWin"},
        {ENGINE_KEY_NUMPAD_ENTER, "numpadenter"},
        {ENGINE_KEY_LBUTTON, "LButton"},
        {ENGINE_KEY_RBUTTON, "RButton"},
        {ENGINE_KEY_MBUTTON, "MButton"},
        {ENGINE_KEY_XBUTTON1, "XButton1"},
        {ENGINE_KEY_XBUTTON2, "XButton2"},
    };
    int i;

    if (cap < 2)
        return 0;
    if (id >= 'A' && id <= 'Z') {
        dst[0] = (char)('a' + (id - 'A'));
        dst[1] = '\0';
        return 1;
    }
    if (id >= '0' && id <= '9') {
        dst[0] = (char)id;
        dst[1] = '\0';
        return 1;
    }
    if (id >= 0x70 && id <= 0x87) {
        snprintf(dst, (size_t)cap, "f%d", (int)(id - 0x70 + 1));
        return 1;
    }
    for (i = 0; i < (int)(sizeof table / sizeof table[0]); i++) {
        if (table[i].id == id) {
            snprintf(dst, (size_t)cap, "%s", table[i].name);
            return 1;
        }
    }
    snprintf(dst, (size_t)cap, "id%u", (unsigned)id);
    return 1;
}

static void emit_line(struct buf *out, const char *line)
{
    int n = (int)strlen(line);

    if (out->n > OUT_CAP - n - 1) {
        out->overflow = 1;
        return;
    }
    memcpy(out->data + out->n, line, (size_t)n);
    out->n += n;
    out->data[out->n] = '\0';
}

static int cmp_cstr(const void *a, const void *b)
{
    return strcmp(a, b);
}

/* AHK walks its hold map in sorted key order. The native hold list is
   insertion order. Sort names so one tick's downs or ups compare as a set. */
static void emit_named(struct buf *out, int64_t now, const char *verb, int n, int ups)
{
    char names[ENGINE_REPORT_CAP][32];
    char line[128];
    int i;

    if (n > ENGINE_REPORT_CAP)
        n = ENGINE_REPORT_CAP;
    for (i = 0; i < n; i++) {
        uint16_t id = ups ? engine_report_up(&g_play.last, i) : engine_report_down(&g_play.last, i);
        format_name(id, names[i], 32);
    }
    if (n > 1)
        qsort(names, (size_t)n, sizeof names[0], cmp_cstr);
    for (i = 0; i < n; i++) {
        snprintf(line, sizeof line, "%lld %s %s\n", (long long)now, verb, names[i]);
        emit_line(out, line);
    }
}

static void emit_report(struct buf *out, int64_t now)
{
    char line[128];
    int abs = (engine_report_flags(&g_play.last) & ENGINE_REPORT_ABSOLUTE) != 0;
    int32_t dx = engine_report_dx(&g_play.last);
    int32_t dy = engine_report_dy(&g_play.last);
    int32_t wheel = engine_report_wheel(&g_play.last);

    emit_named(out, now, "down", engine_report_down_count(&g_play.last), 0);
    emit_named(out, now, "up", engine_report_up_count(&g_play.last), 1);
    if (abs) {
        snprintf(line, sizeof line, "%lld goto %d %d\n", (long long)now, engine_report_ax(&g_play.last),
                 engine_report_ay(&g_play.last));
        emit_line(out, line);
    } else if (dx != 0 || dy != 0) {
        snprintf(line, sizeof line, "%lld move %d %d\n", (long long)now, dx, dy);
        emit_line(out, line);
    }
    if (wheel != 0) {
        snprintf(line, sizeof line, "%lld wheel %d\n", (long long)now, wheel);
        emit_line(out, line);
    }
}

static int play_script(const struct script *script, struct buf *out)
{
    struct engine_fingers fingers;
    struct engine_pointer pointer;
    int have_pointer = 0;
    int t;
    int i;

    memset(out, 0, sizeof *out);
    memset(&pointer, 0, sizeof pointer);
    engine_play_init(&g_play);
    engine_fingers_clear(&fingers);
    engine_play_set_fingers(&g_play, &fingers);
    engine_play_set_macros(&g_play, g_macros, g_macro_n);
    if (script->has_roll) {
        g_roll = script->roll;
        engine_play_set_roll(&g_play, roll_fn, NULL);
    }
    for (t = 0; t <= script->until; t++) {
        for (i = 0; i < script->ev_n; i++) {
            const struct event *ev = &script->ev[i];
            if (ev->time != t)
                continue;
            if (ev->kind == EV_FINGER) {
                uint16_t id = 0;
                engine_key_id(ev->a, &id);
                engine_fingers_set(&fingers, id, ev->x);
                engine_play_set_fingers(&g_play, &fingers);
            } else if (ev->kind == EV_START) {
                struct engine_macro *macro = find_macro(ev->a);
                if (macro)
                    engine_play_start(&g_play, macro, -1, 1);
            } else if (ev->kind == EV_STOP) {
                engine_play_stop(&g_play, ev->a);
            } else if (ev->kind == EV_ASK) {
                engine_play_ask_stop(&g_play, ev->a);
            } else if (ev->kind == EV_MOUSE || ev->kind == EV_WINDOW || ev->kind == EV_CLIENT) {
                if (ev->kind == EV_MOUSE) {
                    pointer.have_mouse = 1;
                    pointer.mouse_x = ev->x;
                    pointer.mouse_y = ev->y;
                } else if (ev->kind == EV_WINDOW) {
                    pointer.have_window = 1;
                    pointer.window_x = ev->x;
                    pointer.window_y = ev->y;
                } else {
                    pointer.have_client = 1;
                    pointer.client_x = ev->x;
                    pointer.client_y = ev->y;
                }
                have_pointer = 1;
                engine_play_set_pointer(&g_play, &pointer);
            }
        }
        (void)have_pointer;
        engine_play_tick(&g_play, t);
        emit_report(out, t);
        if (out->overflow) {
            set_err("trace overflow");
            return 0;
        }
    }
    return 1;
}

static void ensure_dir(const char *path)
{
#ifdef _WIN32
    (void)_mkdir(path);
#else
    (void)mkdir(path, 0755);
#endif
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    size_t n;

    if (!f)
        return 0;
    n = strlen(text);
    if (n > 0 && fwrite(text, 1, n, f) != n) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static void show_diff(const char *name, const char *got, const char *want)
{
    fprintf(stderr, "diff %s\n--- native or expected\n%s--- other\n%s", name, want, got);
}

static int same_text(const char *a, const char *b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    return strcmp(a, b) == 0;
}

static int approved(const char *bench, const char *name)
{
    char path[512];
    char text[8192];
    char *line;

    if (!join_path(path, (int)sizeof path, bench, "approved.txt"))
        return 0;
    if (read_file(path, text, (int)sizeof text) < 0)
        return 0;
    line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (line[0] != '\0' && line[0] != '#' && strcmp(line, name) == 0)
            return 1;
        if (!nl)
            break;
        line = nl + 1;
    }
    return 0;
}

static int each_name(const char *bench, int (*fn)(const char *bench, const char *name, void *ctx), void *ctx)
{
    char path[512];
    char text[8192];
    char *line;
    int n = 0;

    if (!join_path(path, (int)sizeof path, bench, "order.txt"))
        return -1;
    if (read_file(path, text, (int)sizeof text) < 0) {
        fprintf(stderr, "cannot read order.txt\n");
        return -1;
    }
    line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (line[0] != '\0' && line[0] != '#') {
            if (!fn(bench, line, ctx))
                return -1;
            n++;
        }
        if (!nl)
            break;
        line = nl + 1;
    }
    if (n == 0) {
        fprintf(stderr, "order.txt is empty\n");
        return -1;
    }
    return n;
}

struct run_ctx {
    int write;
    int failed;
};

static int run_one(const char *bench, const char *name, void *ctx)
{
    struct run_ctx *run = ctx;
    struct script script;
    struct buf out;
    char case_path[512];
    char rel[256];
    char profile_path[512];
    char expect_path[512];
    struct js *root;

    snprintf(rel, sizeof rel, "cases/%s.case", name);
    if (!join_path(case_path, (int)sizeof case_path, bench, rel)) {
        fprintf(stderr, "%s path\n", name);
        run->failed++;
        return 1;
    }
    if (!load_script(case_path, &script)) {
        fprintf(stderr, "%s %s\n", name, g_err);
        run->failed++;
        return 1;
    }
    if (!join_path(profile_path, (int)sizeof profile_path, bench, script.profile)) {
        fprintf(stderr, "%s profile path\n", name);
        run->failed++;
        return 1;
    }
    if (read_file(profile_path, g_file, FILE_CAP) < 0) {
        fprintf(stderr, "%s cannot read profile\n", name);
        run->failed++;
        return 1;
    }
    root = parse_json(g_file);
    if (!root || !compile_profile(root)) {
        fprintf(stderr, "%s %s\n", name, g_err[0] ? g_err : "bad profile");
        run->failed++;
        return 1;
    }
    if (!play_script(&script, &out)) {
        fprintf(stderr, "%s %s\n", name, g_err);
        run->failed++;
        return 1;
    }
    snprintf(rel, sizeof rel, "expected/%s.out", name);
    if (!join_path(expect_path, (int)sizeof expect_path, bench, rel)) {
        run->failed++;
        return 1;
    }
    if (run->write) {
        if (!write_text(expect_path, out.data)) {
            fprintf(stderr, "%s cannot write expected\n", name);
            run->failed++;
        }
        return 1;
    }
    if (read_file(expect_path, g_file, FILE_CAP) < 0) {
        fprintf(stderr, "%s missing expected\n", name);
        run->failed++;
        return 1;
    }
    if (!same_text(out.data, g_file)) {
        show_diff(name, out.data, g_file);
        run->failed++;
    }
    return 1;
}

static int check_one(const char *bench, const char *name, void *ctx)
{
    char *dir = ctx;
    char rel[256];
    char expect_path[512];
    char got_path[512];
    char got[FILE_CAP];

    if (approved(bench, name))
        return 1;
    snprintf(rel, sizeof rel, "expected/%s.out", name);
    if (!join_path(expect_path, (int)sizeof expect_path, bench, rel))
        return 0;
    snprintf(rel, sizeof rel, "%s.out", name);
    if (!join_path(got_path, (int)sizeof got_path, dir, rel))
        return 0;
    if (read_file(expect_path, g_file, FILE_CAP) < 0) {
        fprintf(stderr, "%s missing expected\n", name);
        return 0;
    }
    if (read_file(got_path, got, FILE_CAP) < 0) {
        fprintf(stderr, "%s missing ahk trace\n", name);
        return 0;
    }
    if (!same_text(got, g_file)) {
        show_diff(name, got, g_file);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *bench;
    const char *check = NULL;
    struct run_ctx run;
    char expect_dir[512];
    int i;

    if (argc < 2) {
        fprintf(stderr, "engine-trace <bench-dir> [--write] [--check <ahk-dir>]\n");
        return 1;
    }
    bench = argv[1];
    memset(&run, 0, sizeof run);
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--write") == 0) {
            run.write = 1;
        } else if (strcmp(argv[i], "--check") == 0 && i + 1 < argc) {
            check = argv[++i];
        } else {
            fprintf(stderr, "unknown argument\n");
            return 1;
        }
    }
    if (check) {
        if (each_name(bench, check_one, (void *)check) < 0)
            return 1;
        printf("ok\n");
        return 0;
    }
    if (run.write && join_path(expect_dir, (int)sizeof expect_dir, bench, "expected"))
        ensure_dir(expect_dir);
    if (each_name(bench, run_one, &run) < 0)
        return 1;
    if (run.failed) {
        fprintf(stderr, "%d failed\n", run.failed);
        return 1;
    }
    printf("ok\n");
    return 0;
}
