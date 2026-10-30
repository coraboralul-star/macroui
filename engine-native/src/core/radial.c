#include "radial.h"

#include <math.h>
#include <string.h>

#include "hook.h"
#include "profile.h"

static const uint16_t k_slot_keys[ENGINE_RADIAL_SLOTS] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38 };

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t n;

    if (!dst || cap == 0)
        return;
    n = src ? strlen(src) : 0;
    if (n >= cap)
        n = cap - 1;
    if (n)
        memcpy(dst, src, n);
    dst[n] = '\0';
}

static uint16_t key_at(const struct engine_radial *menu, int k)
{
    if (k < ENGINE_RADIAL_SLOTS)
        return k_slot_keys[k];
    return k == ENGINE_RADIAL_SLOTS ? menu->prev_key : menu->next_key;
}

static int down_of(const struct engine_fingers *fingers, uint16_t id)
{
    return id != 0 && fingers && engine_fingers_down(fingers, id) ? 1 : 0;
}

static int macro_on(const struct engine_live_config *live, int index)
{
    if (!live || !live->macros || index < 0 || index >= live->macro_count)
        return 0;
    if (live->extras && !live->extras[index].enabled)
        return 0;
    return live->macros[index].step_count > 0;
}

void engine_radial_init(struct engine_radial_state *st)
{
    if (!st)
        return;
    memset(st, 0, sizeof *st);
    st->lit = -1;
    st->need_seed = 1;
}

void engine_radial_reseed(struct engine_radial_state *st)
{
    if (st)
        st->need_seed = 1;
}

int engine_radial_ready(const struct engine_radial *radial, const struct engine_live_config *live)
{
    int i;
    int any = 0;

    if (!radial || !radial->enabled || !engine_can_trigger(radial->trigger))
        return 0;
    if (live && live->macros) {
        for (i = 0; i < live->macro_count; i++) {
            if (macro_on(live, i) && live->macros[i].trigger == radial->trigger)
                return 0;
        }
    }
    for (i = 0; i < radial->slot_count && i < ENGINE_RADIAL_SLOTS; i++) {
        if (macro_on(live, radial->slot[i]))
            any = 1;
    }
    return any;
}

int engine_radial_mode(const struct engine_radial *radial, const struct engine_live_config *live)
{
    (void)live;
    return radial && radial->select == ENGINE_RADIAL_FLICK ? ENGINE_RADIAL_FLICK : ENGINE_RADIAL_KEYS;
}

int engine_radial_slot_at(int64_t dx, int64_t dy, int slot_count, int32_t dead_zone)
{
    double angle;
    double width;
    int slot;

    if (slot_count <= 0)
        return -1;
    if (dead_zone < 1)
        dead_zone = 1;
    if ((double)dx * (double)dx + (double)dy * (double)dy <= (double)dead_zone * (double)dead_zone)
        return -1;
    /* Screen y grows down, so up is -dy. 0 degrees is straight up, then clockwise. */
    angle = atan2((double)dx, (double)-dy) * 180.0 / 3.14159265358979323846;
    if (angle < 0)
        angle += 360.0;
    width = 360.0 / (double)slot_count;
    slot = (int)floor((angle + width / 2.0) / width);
    return slot % slot_count;
}

static void note(struct engine_radial_state *st, int kind, int why, int slot, const char *macro)
{
    struct engine_radial_note *n;

    if (st->note_count >= ENGINE_RADIAL_NOTE_CAP)
        return;
    n = &st->notes[st->note_count++];
    memset(n, 0, sizeof *n);
    n->kind = (uint8_t)kind;
    n->why = (uint8_t)why;
    n->mode = (uint8_t)st->mode;
    n->slot = (int8_t)slot;
    n->slots = (uint8_t)st->menu.slot_count;
    copy_text(n->radial, sizeof n->radial, st->menu.id);
    copy_text(n->name, sizeof n->name, st->menu.name);
    copy_text(n->macro, sizeof n->macro, macro);
}

static void seed(struct engine_radial_state *st, const struct engine_live_config *live,
                 const struct engine_fingers *fingers)
{
    int i;

    memset(st->prev, 0, sizeof st->prev);
    for (i = 0; live && live->radials && i < live->radial_count && i < ENGINE_RADIAL_CAP; i++)
        st->prev[i] = (uint8_t)down_of(fingers, live->radials[i].trigger);
    for (i = 0; i < ENGINE_RADIAL_KEY_COUNT; i++)
        st->key_prev[i] = (uint8_t)(st->open ? down_of(fingers, key_at(&st->menu, i)) : 0);
    st->click_prev = (uint8_t)down_of(fingers, ENGINE_KEY_LBUTTON);
    st->need_seed = 0;
}

/* The one way out. #100's camera lock lets go here too. */
static void close_menu(struct engine_radial_state *st, int kind, int why, int slot, const char *macro)
{
    if (!st->open)
        return;
    note(st, kind, why, slot, macro);
    st->last_why = why;
    st->open = 0;
    st->lit = -1;
    st->dx = 0;
    st->dy = 0;
}

static void select_lit(struct engine_radial_state *st, struct engine_play *play,
                       const struct engine_live_config *live)
{
    int slot = st->lit;
    int index = slot >= 0 && slot < st->menu.slot_count ? st->menu.slot[slot] : -1;
    const struct engine_macro *macro;

    if (slot < 0) {
        close_menu(st, ENGINE_RADIAL_NOTE_CANCEL, ENGINE_RADIAL_WHY_NO_SLOT, -1, "");
        return;
    }
    if (!macro_on(live, index)) {
        close_menu(st, ENGINE_RADIAL_NOTE_CANCEL, ENGINE_RADIAL_WHY_EMPTY, slot, "");
        return;
    }
    macro = &live->macros[index];
    if (engine_play_has_runner(play, macro->id)) {
        close_menu(st, ENGINE_RADIAL_NOTE_SELECT, ENGINE_RADIAL_WHY_PLAYING, slot, macro->id);
        return;
    }
    close_menu(st, ENGINE_RADIAL_NOTE_SELECT,
               engine_play_start_pass(play, macro, ENGINE_WHY_RADIAL) ? ENGINE_RADIAL_WHY_PLAYED
                                                                      : ENGINE_RADIAL_WHY_REFUSED,
               slot, macro->id);
}

static const struct engine_radial *find_menu(const struct engine_live_config *live, const char *id)
{
    int i;

    for (i = 0; live && live->radials && i < live->radial_count; i++) {
        if (strcmp(live->radials[i].id, id) == 0)
            return &live->radials[i];
    }
    return NULL;
}

static void open_menu(struct engine_radial_state *st, const struct engine_radial *radial,
                      const struct engine_live_config *live, const struct engine_fingers *fingers)
{
    int i;

    st->open = 1;
    st->menu = *radial;
    st->mode = engine_radial_mode(radial, live);
    st->lit = -1;
    st->dx = 0;
    st->dy = 0;
    /* A key already down when the menu opens is not a pick. */
    for (i = 0; i < ENGINE_RADIAL_KEY_COUNT; i++)
        st->key_prev[i] = (uint8_t)down_of(fingers, key_at(radial, i));
    st->click_prev = (uint8_t)down_of(fingers, ENGINE_KEY_LBUTTON);
    note(st, ENGINE_RADIAL_NOTE_OPEN, ENGINE_RADIAL_WHY_NONE, -1, "");
}

static void read_keys(struct engine_radial_state *st, const struct engine_fingers *fingers)
{
    int count = st->menu.slot_count;
    int k;

    for (k = 0; k < ENGINE_RADIAL_KEY_COUNT; k++) {
        int down = down_of(fingers, key_at(&st->menu, k));
        int edge = down && !st->key_prev[k];

        st->key_prev[k] = (uint8_t)down;
        if (!edge || count <= 0 || st->mode != ENGINE_RADIAL_KEYS)
            continue;
        if (k < ENGINE_RADIAL_SLOTS) {
            if (k < count)
                st->lit = k;
        } else if (k == ENGINE_RADIAL_SLOTS) {
            st->lit = st->lit <= 0 ? count - 1 : st->lit - 1;
        } else {
            st->lit = st->lit < 0 || st->lit >= count - 1 ? 0 : st->lit + 1;
        }
    }
}

void engine_radial_poll(struct engine_radial_state *st, struct engine_play *play,
                        const struct engine_live_config *live, const struct engine_fingers *fingers,
                        const char *front, const char *owner, int armed)
{
    int focus_ok;
    int i;

    if (!st)
        return;
    if (!armed) {
        close_menu(st, ENGINE_RADIAL_NOTE_CANCEL, ENGINE_RADIAL_WHY_STOP, -1, "");
        seed(st, live, fingers);
        return;
    }
    focus_ok = live ? engine_focus_swallow(live->focus_exe, front, owner) : 0;
    if (st->open) {
        const struct engine_radial *now = find_menu(live, st->menu.id);

        if (!now || !engine_radial_ready(now, live)) {
            close_menu(st, ENGINE_RADIAL_NOTE_CANCEL,
                       now && !now->enabled ? ENGINE_RADIAL_WHY_DISABLED : ENGINE_RADIAL_WHY_PROFILE, -1, "");
        } else if (!focus_ok) {
            close_menu(st, ENGINE_RADIAL_NOTE_CANCEL, ENGINE_RADIAL_WHY_FOCUS, -1, "");
        } else {
            /* Slots follow the newest profile. The mode stays what it opened with. */
            st->menu = *now;
        }
    }
    if (st->need_seed) {
        seed(st, live, fingers);
        if (st->open && !down_of(fingers, st->menu.trigger))
            select_lit(st, play, live);
        return;
    }
    if (st->open) {
        int click = down_of(fingers, ENGINE_KEY_LBUTTON);
        int clicked = click && !st->click_prev;

        st->click_prev = (uint8_t)click;
        read_keys(st, fingers);
        if (clicked && engine_radial_wants_click(st))
            select_lit(st, play, live);
        else if (!down_of(fingers, st->menu.trigger))
            select_lit(st, play, live);
    }
    for (i = 0; live && live->radials && i < live->radial_count && i < ENGINE_RADIAL_CAP; i++) {
        const struct engine_radial *radial = &live->radials[i];
        int down = down_of(fingers, radial->trigger);
        int was = st->prev[i];

        st->prev[i] = (uint8_t)down;
        if (st->open || !down || was || !focus_ok || !engine_radial_ready(radial, live))
            continue;
        open_menu(st, radial, live, fingers);
    }
}

void engine_radial_move(struct engine_radial_state *st, int32_t dx, int32_t dy)
{
    double reach;
    double len;

    if (!st || !st->open || st->mode != ENGINE_RADIAL_FLICK)
        return;
    st->dx += dx;
    st->dy += dy;
    reach = (double)(st->menu.dead_zone > 0 ? st->menu.dead_zone : ENGINE_RADIAL_DEAD_ZONE) * ENGINE_RADIAL_REACH;
    len = sqrt((double)st->dx * (double)st->dx + (double)st->dy * (double)st->dy);
    if (len > reach) {
        st->dx = (int64_t)llround((double)st->dx * reach / len);
        st->dy = (int64_t)llround((double)st->dy * reach / len);
    }
    st->lit = engine_radial_slot_at(st->dx, st->dy, st->menu.slot_count, st->menu.dead_zone);
}

void engine_radial_cancel(struct engine_radial_state *st, int why)
{
    if (st)
        close_menu(st, ENGINE_RADIAL_NOTE_CANCEL, why, -1, "");
}

int engine_radial_owns(const struct engine_radial_state *st, uint16_t id)
{
    int k;

    if (!st || !st->open || id == 0)
        return 0;
    if (id == st->menu.trigger)
        return 1;
    if (engine_radial_wants_click(st) && id == ENGINE_KEY_LBUTTON)
        return 1;
    if (st->mode != ENGINE_RADIAL_KEYS)
        return 0;
    for (k = 0; k < ENGINE_RADIAL_KEY_COUNT; k++) {
        if (key_at(&st->menu, k) == id)
            return 1;
    }
    return 0;
}

int engine_radial_wants_motion(const struct engine_radial_state *st)
{
    return st && st->open && st->mode == ENGINE_RADIAL_FLICK;
}

int engine_radial_wants_click(const struct engine_radial_state *st)
{
    return engine_radial_wants_motion(st) && st->menu.click_select;
}

void engine_radial_swallow(const struct engine_radial_state *st,
                           const struct engine_live_config *live, int focus_ok, uint8_t *swallow)
{
    int i;

    if (!swallow || !focus_ok || !live || !live->radials)
        return;
    for (i = 0; i < live->radial_count; i++) {
        uint16_t id = live->radials[i].trigger;

        if (engine_radial_ready(&live->radials[i], live) && id < ENGINE_SWALLOW_IDS)
            swallow[id] = 1;
    }
    if (!st || !st->open || st->mode != ENGINE_RADIAL_KEYS)
        return;
    for (i = 0; i < ENGINE_RADIAL_KEY_COUNT; i++) {
        uint16_t id = key_at(&st->menu, i);

        if (id != 0 && id < ENGINE_SWALLOW_IDS && engine_can_trigger(id))
            swallow[id] = 1;
    }
}

int engine_radial_needs_mouse(const struct engine_live_config *live)
{
    int i;

    for (i = 0; live && live->radials && i < live->radial_count; i++) {
        const struct engine_radial *radial = &live->radials[i];

        if (!engine_radial_ready(radial, live))
            continue;
        if (engine_is_mouse_button(radial->trigger) || radial->select == ENGINE_RADIAL_FLICK)
            return 1;
    }
    return 0;
}

int engine_radial_claims(const struct engine_live_config *live, uint16_t id)
{
    int i;

    for (i = 0; live && live->radials && i < live->radial_count; i++) {
        if (live->radials[i].trigger == id && engine_radial_ready(&live->radials[i], live))
            return 1;
    }
    return 0;
}

void engine_radial_fill_view(const struct engine_radial_state *st, const struct engine_live_config *live,
                             struct engine_radial_view *out)
{
    const char *key;
    int i;

    if (!out)
        return;
    memset(out, 0, sizeof *out);
    out->lit = -1;
    if (!st)
        return;
    out->why = st->last_why;
    if (!st->open)
        return;
    out->open = 1;
    out->mode = st->mode;
    out->slot_count = st->menu.slot_count;
    out->lit = st->lit;
    out->at = st->menu.at;
    out->size = st->menu.size;
    out->opacity = st->menu.opacity;
    copy_text(out->id, sizeof out->id, st->menu.id);
    copy_text(out->name, sizeof out->name, st->menu.name);
    key = engine_key_name(st->menu.prev_key);
    copy_text(out->prev, sizeof out->prev, key ? key : "");
    key = engine_key_name(st->menu.next_key);
    copy_text(out->next, sizeof out->next, key ? key : "");
    for (i = 0; i < st->menu.slot_count && i < ENGINE_RADIAL_SLOTS; i++) {
        int index = st->menu.slot[i];

        if (!macro_on(live, index))
            continue;
        if (live->extras && live->extras[index].name[0])
            copy_text(out->labels[i], sizeof out->labels[i], live->extras[index].name);
        else
            copy_text(out->labels[i], sizeof out->labels[i], live->macros[index].id);
    }
}

int engine_radial_due(const struct engine_radial_view *sent, const struct engine_radial_view *view,
                      int64_t now, int64_t sent_at, int64_t freq)
{
    if (!sent || !view)
        return 0;
    if (view->open != sent->open)
        return 1;
    if (!view->open)
        return 0;
    if (view->lit != sent->lit || strcmp(view->id, sent->id) != 0)
        return 1;
    if (freq <= 0)
        freq = 1000;
    return now - sent_at >= (int64_t)ENGINE_RADIAL_BEAT_MS * freq / 1000;
}

const char *engine_radial_mode_name(int mode)
{
    return mode == ENGINE_RADIAL_FLICK ? "flick" : "keys";
}

const char *engine_radial_why_name(int why, int mode)
{
    switch (why) {
    case ENGINE_RADIAL_WHY_PLAYED: return "played";
    case ENGINE_RADIAL_WHY_PLAYING: return "playing";
    case ENGINE_RADIAL_WHY_REFUSED: return "refused";
    case ENGINE_RADIAL_WHY_NO_SLOT: return mode == ENGINE_RADIAL_FLICK ? "dead-zone" : "no-slot";
    case ENGINE_RADIAL_WHY_EMPTY: return "empty-slot";
    case ENGINE_RADIAL_WHY_FOCUS: return "focus";
    case ENGINE_RADIAL_WHY_DISABLED: return "disabled";
    case ENGINE_RADIAL_WHY_PROFILE: return "profile";
    case ENGINE_RADIAL_WHY_STOP: return "stop";
    default: return "none";
    }
}
