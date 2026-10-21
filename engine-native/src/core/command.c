#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "command.h"
#include "clock.h"

#include <string.h>

_Static_assert((ENGINE_CMD_CAP & (ENGINE_CMD_CAP - 1u)) == 0, "command cap");

static const unsigned char k_magic[4] = { 0x4D, 0x55, 0x49, 0x31 };

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffu);
    p[1] = (unsigned char)((value >> 8) & 0xffu);
    p[2] = (unsigned char)((value >> 16) & 0xffu);
    p[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void write_u16(unsigned char *p, unsigned value)
{
    p[0] = (unsigned char)(value & 0xffu);
    p[1] = (unsigned char)((value >> 8) & 0xffu);
}

int engine_frame_read(const unsigned char *msg, int length, struct engine_pipe_frame *out)
{
    unsigned version;
    unsigned type;
    uint32_t payload;
    unsigned action;

    if (!out)
        return 1;
    memset(out, 0, sizeof *out);
    out->kind = ENGINE_FRAME_BAD;
    if (!msg || length <= 0)
        return 0;
    if (msg[0] == 0x7B) {
        out->kind = ENGINE_FRAME_JSON;
        return 0;
    }
    if (length < 12 || memcmp(msg, k_magic, 4) != 0)
        return 0;
    version = (unsigned)msg[4] | ((unsigned)msg[5] << 8);
    if (version != 1) {
        out->kind = ENGINE_FRAME_BAD_VERSION;
        return 0;
    }
    type = (unsigned)msg[6] | ((unsigned)msg[7] << 8);
    payload = read_u32(msg + 8);
    if ((uint32_t)length != 12u + payload || payload > 1048576u - 12u)
        return 0;

    if (type == 8 && payload == 1) {
        action = msg[12];
        if (action == 1)
            out->kind = ENGINE_FRAME_START;
        else if (action == 2)
            out->kind = ENGINE_FRAME_STOP;
        else if (action == 3)
            out->kind = ENGINE_FRAME_RELOAD;
        else if (action == 4)
            out->kind = ENGINE_FRAME_QUIT;
        else
            out->kind = ENGINE_FRAME_COMMAND;
        return 0;
    }
    if (type == 7 && payload == 1) {
        out->kind = ENGINE_FRAME_OUTPUT;
        out->output_mode = msg[12];
        return 0;
    }
    if (type == 6 && payload >= 4) {
        uint32_t json_bytes = read_u32(msg + 12);

        if (json_bytes != payload - 4u)
            return 0;
        out->kind = ENGINE_FRAME_PROFILE;
        out->profile = msg + 16;
        out->profile_len = json_bytes;
        return 0;
    }
    if (type == 6 || type == 7 || type == 8)
        return 0;
    out->kind = ENGINE_FRAME_IGNORE;
    return 0;
}

void engine_cmd_ring_init(struct engine_cmd_ring *ring)
{
    if (!ring)
        return;
    memset(ring, 0, sizeof *ring);
    atomic_init(&ring->head, 0u);
    atomic_init(&ring->tail, 0u);
    atomic_init(&ring->overflow, 0);
}

int engine_cmd_push(struct engine_cmd_ring *ring, int kind, int mode)
{
    unsigned head;
    unsigned tail;

    if (!ring)
        return 0;
    head = atomic_load_explicit(&ring->head, memory_order_acquire);
    tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    if (tail - head >= ENGINE_CMD_CAP) {
        atomic_store_explicit(&ring->overflow, 1, memory_order_release);
        return 0;
    }
    ring->slots[tail & (ENGINE_CMD_CAP - 1u)].kind = (uint8_t)kind;
    ring->slots[tail & (ENGINE_CMD_CAP - 1u)].mode = (uint8_t)mode;
    atomic_store_explicit(&ring->tail, tail + 1u, memory_order_release);
    return 1;
}

int engine_cmd_pop(struct engine_cmd_ring *ring, struct engine_cmd_item *out)
{
    unsigned head;
    unsigned tail;

    if (!ring || !out)
        return 0;
    head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    if (head == tail)
        return 0;
    *out = ring->slots[head & (ENGINE_CMD_CAP - 1u)];
    atomic_store_explicit(&ring->head, head + 1u, memory_order_release);
    return 1;
}

int engine_cmd_take_overflow(struct engine_cmd_ring *ring)
{
    if (!ring)
        return 0;
    return atomic_exchange_explicit(&ring->overflow, 0, memory_order_acq_rel) != 0;
}

int engine_trigger_action(int mode, int down, int was, int running)
{
    if (mode == ENGINE_MODE_WHILE_HELD) {
        if (running && !down)
            return ENGINE_TRIGGER_STOP;
        if (down && !was)
            return ENGINE_TRIGGER_START;
        return ENGINE_TRIGGER_NONE;
    }
    if (mode == ENGINE_MODE_TOGGLE) {
        if (down && !was)
            return running ? ENGINE_TRIGGER_STOP : ENGINE_TRIGGER_START;
        return ENGINE_TRIGGER_NONE;
    }
    if (mode == ENGINE_MODE_ON_RELEASE) {
        if (!down && was)
            return ENGINE_TRIGGER_START;
        return ENGINE_TRIGGER_NONE;
    }
    if (down && !was)
        return ENGINE_TRIGGER_START;
    return ENGINE_TRIGGER_NONE;
}

void engine_trigger_init(struct engine_trigger_mem *mem)
{
    if (!mem)
        return;
    memset(mem, 0, sizeof *mem);
    mem->need_seed = 1;
}

void engine_trigger_reseed(struct engine_trigger_mem *mem)
{
    if (!mem)
        return;
    mem->need_seed = 1;
}

void engine_trigger_note_profile(struct engine_trigger_mem *mem, uint64_t seq)
{
    if (!mem || mem->profile_seq == seq)
        return;
    mem->profile_seq = seq;
    mem->need_seed = 1;
}

static int same_id(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    return strcmp(a, b) == 0;
}

static int runner_live(const struct engine_play *play, const char *id)
{
    int i;

    if (!play || !id || !id[0])
        return 0;
    for (i = 0; i < play->runner_count; i++) {
        if (!play->runners[i].done && same_id(play->runners[i].id, id))
            return 1;
    }
    for (i = 0; i < play->pending_count; i++) {
        if (same_id(play->pending[i].id, id))
            return 1;
    }
    return 0;
}

static uint32_t top_count(const struct engine_macro *macro)
{
    uint32_t n;

    if (!macro)
        return 0;
    n = macro->top_count ? macro->top_count : macro->step_count;
    if (n > macro->step_count)
        n = macro->step_count;
    return n;
}

static int has_edge(const struct engine_macro *macro, int *spam, int *tap)
{
    uint32_t n;
    uint32_t i;
    int any = 0;

    if (spam)
        *spam = 0;
    if (tap)
        *tap = 0;
    if (!macro || !macro->steps)
        return 0;
    n = top_count(macro);
    for (i = 0; i < n; i++) {
        if (macro->steps[i].type != ENGINE_STEP_EDGE_HOLD)
            continue;
        any = 1;
        if ((macro->steps[i].action & ENGINE_EDGE_SPAM) != 0) {
            if (spam)
                *spam = 1;
        } else if (tap) {
            *tap = 1;
        }
    }
    return any;
}

static int has_hold_block(const struct engine_macro *macro)
{
    uint32_t n;
    uint32_t i;

    if (!macro || !macro->steps)
        return 0;
    n = top_count(macro);
    for (i = 0; i < n; i++) {
        uint8_t kind = macro->steps[i].type;

        if (kind == ENGINE_STEP_HOLD_LOOP || kind == ENGINE_STEP_SWAP_LOOP ||
            kind == ENGINE_STEP_ONCE_HELD)
            return 1;
    }
    return 0;
}

static int holds_on_release(const struct engine_macro *macro, int spam, int tap)
{
    if (has_hold_block(macro) || spam)
        return 0;
    if (tap)
        return 1;
    return macro && (macro->mode == ENGINE_MODE_WHILE_HELD || macro->mode == ENGINE_MODE_TOGGLE);
}

static const struct engine_macro *find_macro(const struct engine_live_config *live, const char *id)
{
    int i;

    if (!live || !live->macros || !id || !id[0])
        return NULL;
    for (i = 0; i < live->macro_count; i++) {
        if (same_id(live->macros[i].id, id))
            return &live->macros[i];
    }
    return NULL;
}

/* StartOncePlay. whileHeld and toggle keep the macro mode. Other modes run once. */
static int once_start(struct engine_play *play, const struct engine_macro *macro)
{
    int spam = 0;
    int tap = 0;
    int edge = has_edge(macro, &spam, &tap);
    int mode = edge ? ENGINE_MODE_WHILE_HELD : (macro ? macro->mode : ENGINE_MODE_ONCE);
    int override = (mode == ENGINE_MODE_WHILE_HELD || mode == ENGINE_MODE_TOGGLE) ? -1
                                                                                   : ENGINE_MODE_ONCE;

    return engine_play_start(play, macro, override, 1);
}

static const char *macro_focus(const struct engine_live_config *live, int index)
{
    if (live && live->extras && live->extras[index].focus_exe[0])
        return live->extras[index].focus_exe;
    if (live && live->focus_exe[0])
        return live->focus_exe;
    return "";
}

static int macro_enabled(const struct engine_live_config *live, int index)
{
    if (!live || !live->extras)
        return 1;
    return live->extras[index].enabled ? 1 : 0;
}

int engine_live_needs_mouse(const struct engine_live_config *live)
{
    int i;

    if (!live || !live->macros || live->macro_count <= 0)
        return 0;
    for (i = 0; i < live->macro_count; i++) {
        uint16_t id = live->macros[i].trigger;

        if (!macro_enabled(live, i) || live->macros[i].step_count == 0)
            continue;
        if (engine_can_trigger(id) && engine_is_mouse_button(id))
            return 1;
    }
    return 0;
}

int engine_live_claims(const struct engine_live_config *live, uint16_t id)
{
    int i;

    if (!live || !live->macros || !engine_can_trigger(id))
        return 0;
    for (i = 0; i < live->macro_count; i++) {
        if (!macro_enabled(live, i) || live->macros[i].step_count == 0)
            continue;
        if (live->macros[i].trigger == id)
            return 1;
    }
    return 0;
}

void engine_claims_init(struct engine_claim_mem *mem)
{
    if (!mem)
        return;
    memset(mem, 0, sizeof *mem);
}

int engine_claims_sync(struct engine_claim_mem *mem, const struct engine_live_config *live,
                       const char *front, const char *owner, int armed, int force,
                       struct engine_fingers *fingers, uint16_t *out, int cap)
{
    uint8_t now[ENGINE_SWALLOW_IDS];
    int swallow;
    int flip;
    int n = 0;
    int i;

    if (!mem)
        return 0;
    memset(now, 0, sizeof now);
    if (armed && live && live->macros) {
        for (i = 0; i < live->macro_count; i++) {
            uint16_t id = live->macros[i].trigger;

            if (!macro_enabled(live, i) || live->macros[i].step_count == 0)
                continue;
            if (engine_can_trigger(id) && id < ENGINE_SWALLOW_IDS)
                now[id] = 1;
        }
    }
    swallow = live ? engine_focus_swallow(live->focus_exe, front, owner) : 0;
    flip = mem->ready && swallow != mem->swallow;
    if (mem->ready && fingers) {
        for (i = 1; i < (int)ENGINE_SWALLOW_IDS; i++) {
            int drop = mem->claimed[i] && !now[i];

            if ((flip || force) && (mem->claimed[i] || now[i]))
                drop = 1;
            if (!drop || !engine_fingers_seen(fingers, (uint16_t)i))
                continue;
            if (engine_fingers_down(fingers, (uint16_t)i)) {
                if (out && n < cap)
                    out[n] = (uint16_t)i;
                n++;
            }
            engine_fingers_forget(fingers, (uint16_t)i);
        }
    }
    memcpy(mem->claimed, now, sizeof now);
    mem->swallow = swallow;
    mem->ready = 1;
    return n;
}

void engine_trigger_seed(struct engine_trigger_mem *mem, const struct engine_live_config *live,
                         const struct engine_fingers *fingers)
{
    int n;
    int i;

    if (!mem)
        return;
    memset(mem->prev, 0, sizeof mem->prev);
    mem->count = 0;
    mem->need_seed = 0;
    if (!live || !live->macros || live->macro_count <= 0)
        return;
    n = live->macro_count;
    if (n > ENGINE_TRIGGER_MEM)
        n = ENGINE_TRIGGER_MEM;
    mem->count = n;
    for (i = 0; i < n; i++) {
        uint16_t id = live->macros[i].trigger;

        mem->prev[i] = (uint8_t)(engine_fingers_down(fingers, id) ? 1 : 0);
    }
}

void engine_poll_triggers(struct engine_play *play, const struct engine_live_config *live,
                          struct engine_trigger_mem *mem, const struct engine_fingers *fingers,
                          const char *front, const char *owner)
{
    int n;
    int i;

    if (!play || !live || !mem || !live->macros || live->macro_count <= 0)
        return;
    if (mem->need_seed || mem->count != live->macro_count)
        engine_trigger_seed(mem, live, fingers);
    n = live->macro_count;
    if (n > ENGINE_TRIGGER_MEM)
        n = ENGINE_TRIGGER_MEM;
    for (i = 0; i < n; i++) {
        const struct engine_macro *macro = &live->macros[i];
        int spam = 0;
        int tap = 0;
        int edge = has_edge(macro, &spam, &tap);
        int mode = edge ? ENGINE_MODE_WHILE_HELD : macro->mode;
        int down;
        int was;
        int running;
        int once;
        int action;

        if (!macro->id[0])
            continue;
        down = engine_fingers_down(fingers, macro->trigger);
        was = mem->prev[i] ? 1 : 0;
        mem->prev[i] = (uint8_t)(down ? 1 : 0);
        if (!engine_can_trigger(macro->trigger))
            continue;
        if (engine_play_muted(play, macro->id))
            continue;
        if (!macro_enabled(live, i) || !engine_focus_swallow(macro_focus(live, i), front, owner)) {
            engine_play_stop(play, macro->id);
            continue;
        }
        running = runner_live(play, macro->id);
        once = engine_once_state(play, macro->id);
        if (once == ENGINE_LOCK_DONE) {
            if (running) {
                if (!down && mode == ENGINE_MODE_WHILE_HELD)
                    engine_play_ask_stop(play, macro->id);
            } else {
                engine_once_ensure(play, macro->id, macro->trigger);
            }
            continue;
        }
        if (macro->advanced && mode != ENGINE_MODE_ON_RELEASE) {
            if (down && !was) {
                if (once == ENGINE_LOCK_OPEN)
                    once_start(play, macro);
                else
                    engine_play_start(play, macro, -1, 1);
                if (once == ENGINE_LOCK_OPEN)
                    engine_once_spend(play, macro->id);
            } else if (!down && running && once != ENGINE_LOCK_OPEN &&
                       holds_on_release(macro, spam, tap)) {
                engine_play_ask_stop(play, macro->id);
            }
            continue;
        }
        action = engine_trigger_action(mode, down, was, running);
        if (action == ENGINE_TRIGGER_START) {
            if (once == ENGINE_LOCK_OPEN)
                once_start(play, macro);
            else
                engine_play_start(play, macro, edge ? ENGINE_MODE_WHILE_HELD : -1, 1);
            if (once == ENGINE_LOCK_OPEN)
                engine_once_spend(play, macro->id);
        } else if (action == ENGINE_TRIGGER_STOP) {
            engine_play_stop(play, macro->id);
        }
    }
}

void engine_once_arm(struct engine_play *play, const struct engine_live_config *live,
                     const char *const *ids, int count, const struct engine_fingers *fingers)
{
    int i;

    if (!play || !ids || count <= 0)
        return;
    for (i = 0; i < count; i++) {
        const char *id = ids[i];
        const struct engine_macro *macro;
        uint16_t key = 0;

        if (!id || !id[0])
            continue;
        macro = find_macro(live, id);
        if (macro)
            key = macro->trigger;
        if (!engine_once_ensure(play, id, key))
            continue;
        if (engine_play_has_runner(play, id)) {
            engine_once_set(play, id, ENGINE_LOCK_DONE);
            continue;
        }
        if (macro && key && engine_can_trigger(key) && engine_fingers_down(fingers, key)) {
            once_start(play, macro);
            engine_once_set(play, id, ENGINE_LOCK_DONE);
            continue;
        }
        engine_once_set(play, id, ENGINE_LOCK_OPEN);
    }
}

void engine_reap_orphans(struct engine_play *play, const struct engine_live_config *live)
{
    int i;

    if (!play)
        return;
    for (i = 0; i < play->runner_count; i++) {
        int found = 0;
        int m;

        if (play->runners[i].done || !play->runners[i].id[0])
            continue;
        if (live && live->macros) {
            for (m = 0; m < live->macro_count; m++) {
                if (same_id(live->macros[m].id, play->runners[i].id)) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found)
            engine_play_stop(play, play->runners[i].id);
    }
}

void engine_arm_swallow(struct engine_hook_state *hooks, const struct engine_live_config *live,
                        const char *front, const char *owner, int armed,
                        const struct engine_play *play)
{
    uint8_t *bits;
    uint8_t swallow[ENGINE_SWALLOW_IDS];
    int focus_ok = 0;
    int i;

    if (!hooks)
        return;
    bits = engine_swallow_begin(hooks);
    if (!bits)
        return;
    memset(swallow, 0, sizeof swallow);
    /* SwallowTriggers is the live profile focus, not each macro's focusExe.
       A macro can run on its own focus while the key still passes. */
    if (armed && live && live->macros && live->macro_count > 0) {
        int n = live->macro_count;

        focus_ok = engine_focus_swallow(live->focus_exe, front, owner);
        for (i = 0; i < n; i++) {
            const struct engine_macro *macro = &live->macros[i];
            uint16_t id = macro->trigger;

            if (!macro_enabled(live, i))
                continue;
            if (macro->step_count == 0)
                continue;
            if (!engine_can_trigger(id))
                continue;
            /* Keyboard triggers are swallowed for software and for a board.
               Middle and both side buttons are swallowed only in software.
               A board lets those clicks through. Left and right never reach here. */
            if (focus_ok &&
                (!engine_is_mouse_button(id) || !play ||
                 play->output.mode == ENGINE_OUTPUT_SOFTWARE))
                swallow[id] = 1;
        }
    }
    for (i = 0; i < ENGINE_SWALLOW_IDS; i++) {
        int pass = play && play->pass_count[i] > 0;

        if (pass)
            engine_swallow_set(bits, (uint16_t)i, 0, 1);
        else if (swallow[i])
            engine_swallow_set(bits, (uint16_t)i, 1, 0);
    }
    engine_swallow_publish(hooks);
}

static int put_str(unsigned char *buf, int cap, int *at, const char *text)
{
    size_t n = text ? strlen(text) : 0;

    if (n > 4096u)
        n = 4096u;
    if (!at || *at < 12 || *at + 2 + (int)n > cap)
        return 1;
    write_u16(buf + *at, (unsigned)n);
    if (n)
        memcpy(buf + *at + 2, text, n);
    *at += 2 + (int)n;
    return 0;
}

int engine_state_frame(const struct engine_state_snapshot *snap,
                       const struct engine_state_window *windows, int window_count,
                       unsigned char *buf, int cap)
{
    int at;
    int running = 0;
    int held = 0;
    int wins = 0;
    int i;
    int drop;

    if (!snap || !buf || cap < 20)
        return -1;
    memset(buf, 0, 12);
    memcpy(buf, k_magic, 4);
    write_u16(buf + 4, 1);
    write_u16(buf + 6, 2);
    at = 20;
    if (put_str(buf, cap, &at, snap->active_id) != 0 || put_str(buf, cap, &at, snap->front) != 0)
        return -1;
    running = snap->running_count;
    if (running < 0)
        running = 0;
    if (running > ENGINE_STATE_RUNNING_CAP)
        running = ENGINE_STATE_RUNNING_CAP;
    if (running > 256)
        running = 256;
    for (i = 0; i < running; i++) {
        if (put_str(buf, cap, &at, snap->running[i].id) != 0 ||
            put_str(buf, cap, &at, snap->running[i].name) != 0)
            return -1;
    }
    held = 0;
    if (snap->held_count > 0) {
        int raw = snap->held_count;

        if (raw > ENGINE_STATE_HELD_CAP)
            raw = ENGINE_STATE_HELD_CAP;
        for (i = 0; i < raw; i++) {
            const char *name = engine_key_name(snap->held[i]);

            if (!name)
                continue;
            if (put_str(buf, cap, &at, name) != 0)
                return -1;
            held++;
        }
    }
    wins = window_count;
    if (wins < 0)
        wins = 0;
    if (!windows)
        wins = 0;
    if (wins > ENGINE_STATE_WINDOW_CAP)
        wins = ENGINE_STATE_WINDOW_CAP;
    drop = wins > 256 ? wins - 256 : 0;
    wins -= drop;
    for (i = drop; i < drop + wins; i++) {
        if (put_str(buf, cap, &at, windows[i].exe) != 0 ||
            put_str(buf, cap, &at, windows[i].title) != 0)
            return -1;
    }
    buf[12] = snap->armed ? 1 : 0;
    buf[13] = 0;
    write_u16(buf + 14, (unsigned)running);
    write_u16(buf + 16, (unsigned)held);
    write_u16(buf + 18, (unsigned)wins);
    write_u32(buf + 8, (uint32_t)(at - 12));
    return at;
}

_Static_assert((ENGINE_REPORT_QUEUE_CAP & (ENGINE_REPORT_QUEUE_CAP - 1u)) == 0, "report cap");

void engine_report_queue_init(struct engine_report_queue *queue)
{
    if (!queue)
        return;
    memset(queue, 0, sizeof *queue);
    atomic_init(&queue->head, 0u);
    atomic_init(&queue->tail, 0u);
    atomic_init(&queue->overflow, 0);
}

int engine_report_queue_push(struct engine_report_queue *queue, const struct engine_report *report)
{
    unsigned head;
    unsigned tail;

    if (!queue || !report)
        return 0;
    head = atomic_load_explicit(&queue->head, memory_order_acquire);
    tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);
    if (tail - head >= ENGINE_REPORT_QUEUE_CAP) {
        atomic_store_explicit(&queue->overflow, 1, memory_order_release);
        return 0;
    }
    queue->slots[tail & (ENGINE_REPORT_QUEUE_CAP - 1u)] = *report;
    atomic_store_explicit(&queue->tail, tail + 1u, memory_order_release);
    return 1;
}

int engine_report_queue_pop(struct engine_report_queue *queue, struct engine_report *out)
{
    unsigned head;
    unsigned tail;

    if (!queue || !out)
        return 0;
    head = atomic_load_explicit(&queue->head, memory_order_relaxed);
    tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
    if (head == tail)
        return 0;
    *out = queue->slots[head & (ENGINE_REPORT_QUEUE_CAP - 1u)];
    atomic_store_explicit(&queue->head, head + 1u, memory_order_release);
    return 1;
}

int engine_report_queue_take_overflow(struct engine_report_queue *queue)
{
    if (!queue)
        return 0;
    return atomic_exchange_explicit(&queue->overflow, 0, memory_order_acq_rel) != 0;
}

int engine_board_echo_written(struct engine_hook_state *hooks, const struct engine_report *report,
                              int64_t now, int64_t freq)
{
    struct engine_report_echo echo[64];
    int64_t until;
    int stored = 0;
    int n;
    int i;

    if (!hooks || !report)
        return 0;
    if (freq <= 0)
        freq = 1000;
    until = now + engine_ms_to_ticks(150, freq);
    n = engine_report_echoes(report, echo, 64);
    for (i = 0; i < n; i++)
        stored += engine_echo_push(hooks, echo[i].id, echo[i].down != 0, until);
    return stored;
}

int engine_report_frame(const struct engine_report *report, unsigned char *buf, int cap)
{
    if (!report || !buf || cap < 12 + ENGINE_REPORT_BYTES)
        return -1;
    memset(buf, 0, 12);
    memcpy(buf, k_magic, 4);
    write_u16(buf + 4, 1);
    write_u16(buf + 6, 5);
    write_u32(buf + 8, ENGINE_REPORT_BYTES);
    memcpy(buf + 12, report->bytes, ENGINE_REPORT_BYTES);
    return 12 + ENGINE_REPORT_BYTES;
}
