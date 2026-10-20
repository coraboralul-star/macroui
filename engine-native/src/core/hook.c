#include "hook.h"

#include <string.h>

_Static_assert((ENGINE_EVENT_CAP & (ENGINE_EVENT_CAP - 1u)) == 0, "event cap");
_Static_assert((ENGINE_ECHO_CAP & (ENGINE_ECHO_CAP - 1u)) == 0, "echo cap");
_Static_assert(ENGINE_SWALLOW_IDS == ENGINE_KEY_ID_MAX + 1u, "swallow ids");

static void note_stamp(atomic_int_least64_t *stamp, int64_t now)
{
    int_least64_t stored = now > 0 ? (int_least64_t)now : 1;
    atomic_store_explicit(stamp, stored, memory_order_release);
}

static int key_is_up(uint32_t msg, uint32_t flags)
{
    if (msg == ENGINE_MSG_KEYUP || msg == ENGINE_MSG_SYSKEYUP)
        return 1;
    return (flags & ENGINE_KEY_UP_FLAG) != 0;
}

static int mouse_motion(uint32_t msg)
{
    return msg == ENGINE_MSG_MOUSEMOVE
        || msg == ENGINE_MSG_MOUSEWHEEL
        || msg == ENGINE_MSG_MOUSEHWHEEL;
}

static void push_op(struct engine_hook_watch_out *out, uint8_t op)
{
    if (out->count >= 4)
        return;
    out->op[out->count++] = op;
}

static int ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return (unsigned char)c;
}

/* Trimmed, lowercased, and without a trailing .exe.
   -1 does not fit. 0 is empty. */
static int stem_copy(const char *s, char *out, int cap)
{
    const char *begin;
    const char *end;
    int n;
    int i;

    if (!s)
        return 0;
    begin = s;
    while (*begin == ' ' || *begin == '\t')
        begin++;
    end = begin + strlen(begin);
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t'))
        end--;
    /* ahk_exe and WinGetProcessName compare the file name, not the directory. */
    {
        const char *p;

        for (p = begin; p < end; p++) {
            if (*p == '\\' || *p == '/')
                begin = p + 1;
        }
    }
    n = (int)(end - begin);
    if (n >= cap)
        return -1;
    for (i = 0; i < n; i++)
        out[i] = (char)ascii_lower((unsigned char)begin[i]);
    out[n] = 0;
    if (n >= 4 && out[n - 4] == '.' && out[n - 3] == 'e'
        && out[n - 2] == 'x' && out[n - 1] == 'e') {
        n -= 4;
        out[n] = 0;
    }
    return n;
}

static int stem_prefix(const char *host, const char *stem)
{
    size_t n = strlen(stem);

    if (n == 0)
        return 0;
    if (strncmp(host, stem, n) != 0)
        return 0;
    return host[n] == '-';
}

static uint8_t swallow_bits(struct engine_hook_state *st, uint16_t id)
{
    unsigned index;

    if (!st || id > ENGINE_KEY_ID_MAX)
        return 0;
    index = atomic_load_explicit(&st->swallow_index, memory_order_acquire) & 1u;
    return st->swallow[index].bits[id];
}

static void echo_reap(struct engine_hook_state *st, uint16_t id, int down,
                      int64_t now, int *found)
{
    uint32_t head = atomic_load_explicit(&st->echo_head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&st->echo_tail, memory_order_acquire);
    uint32_t i;

    *found = 0;
    for (i = head; i != tail; i++) {
        struct engine_echo *token = &st->echoes[i & (ENGINE_ECHO_CAP - 1u)];
        int expired = now > token->until;

        if (token->consumed || expired) {
            token->consumed = 1;
            continue;
        }
        if (!*found && token->id == id && (token->down != 0) == (down != 0)) {
            token->consumed = 1;
            *found = 1;
        }
    }
    while (head != tail) {
        struct engine_echo *token = &st->echoes[head & (ENGINE_ECHO_CAP - 1u)];
        if (!token->consumed)
            break;
        head++;
    }
    atomic_store_explicit(&st->echo_head, head, memory_order_release);
}

void engine_hook_init(struct engine_hook_state *st)
{
    if (!st)
        return;
    memset(st, 0, sizeof *st);
    atomic_init(&st->event_head, 0u);
    atomic_init(&st->event_tail, 0u);
    atomic_init(&st->event_overflow, 0);
    atomic_init(&st->echo_head, 0u);
    atomic_init(&st->echo_tail, 0u);
    atomic_init(&st->swallow_index, 0u);
    atomic_init(&st->wake_on_input, 0);
    atomic_init(&st->key_stamp, 0);
    atomic_init(&st->mouse_stamp, 0);
    atomic_init(&st->key_installed, 0);
    atomic_init(&st->mouse_installed, 0);
    atomic_init(&st->need_mouse, 0);
    atomic_init(&st->suspect, 0);
    atomic_init(&st->pending_ops, 0u);
    atomic_init(&st->stop, 0);
}

uint16_t engine_resolve_key(uint32_t vk, uint32_t scan, uint32_t flags)
{
    int extended = (flags & ENGINE_KEY_EXTENDED) != 0;

    if (vk == 0 || vk > 255)
        return 0;
    if (vk == 0xA0 || (vk == 0x10 && scan != 0x36))
        return 0xA0;
    if (vk == 0xA1 || (vk == 0x10 && scan == 0x36))
        return 0xA1;
    if (vk == 0xA2 || (vk == 0x11 && !extended))
        return 0xA2;
    if (vk == 0xA3 || (vk == 0x11 && extended))
        return 0xA3;
    if (vk == 0xA4 || (vk == 0x12 && !extended))
        return 0xA4;
    if (vk == 0xA5 || (vk == 0x12 && extended))
        return 0xA5;
    if (vk == 0x5B)
        return 0x5B;
    if (vk == 0x5C)
        return 0x5C;
    if (vk == 0x0D && extended)
        return ENGINE_KEY_NUMPAD_ENTER;
    return (uint16_t)vk;
}

uint16_t engine_resolve_mouse(uint32_t msg, uint32_t mouse_data, int *down)
{
    int pressed = 0;
    uint16_t id = 0;
    uint32_t button;

    if (msg == ENGINE_MSG_LBUTTONDOWN || msg == ENGINE_MSG_LBUTTONUP) {
        id = ENGINE_KEY_LBUTTON;
        pressed = msg == ENGINE_MSG_LBUTTONDOWN;
    } else if (msg == ENGINE_MSG_RBUTTONDOWN || msg == ENGINE_MSG_RBUTTONUP) {
        id = ENGINE_KEY_RBUTTON;
        pressed = msg == ENGINE_MSG_RBUTTONDOWN;
    } else if (msg == ENGINE_MSG_MBUTTONDOWN || msg == ENGINE_MSG_MBUTTONUP) {
        id = ENGINE_KEY_MBUTTON;
        pressed = msg == ENGINE_MSG_MBUTTONDOWN;
    } else if (msg == ENGINE_MSG_XBUTTONDOWN || msg == ENGINE_MSG_XBUTTONUP) {
        button = (mouse_data >> 16) & 0xffffu;
        if (button == 0)
            button = mouse_data & 0xffffu;
        pressed = msg == ENGINE_MSG_XBUTTONDOWN;
        if (button == 1)
            id = ENGINE_KEY_XBUTTON1;
        else if (button == 2)
            id = ENGINE_KEY_XBUTTON2;
    }
    if (down)
        *down = pressed;
    return id;
}

const char *engine_key_name(uint16_t id)
{
    static const char digits[10][2] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
    };
    static const char letters[26][2] = {
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
        "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"
    };
    static const char *fn[12] = {
        "f1", "f2", "f3", "f4", "f5", "f6",
        "f7", "f8", "f9", "f10", "f11", "f12"
    };

    if (id >= 0x30 && id <= 0x39)
        return digits[id - 0x30];
    if (id >= 0x41 && id <= 0x5A)
        return letters[id - 0x41];
    if (id >= 0x70 && id <= 0x7B)
        return fn[id - 0x70];
    switch (id) {
    case 0x0D: return "enter";
    case 0x10: return "LShift";
    case 0x11: return "LCtrl";
    case 0x12: return "LAlt";
    case 0x20: return "space";
    case 0x5B: return "LWin";
    case 0x5C: return "RWin";
    case 0xA0: return "LShift";
    case 0xA1: return "RShift";
    case 0xA2: return "LCtrl";
    case 0xA3: return "RCtrl";
    case 0xA4: return "LAlt";
    case 0xA5: return "RAlt";
    case ENGINE_KEY_NUMPAD_ENTER: return "numpadenter";
    case ENGINE_KEY_LBUTTON: return "LButton";
    case ENGINE_KEY_RBUTTON: return "RButton";
    case ENGINE_KEY_MBUTTON: return "MButton";
    case ENGINE_KEY_XBUTTON1: return "XButton1";
    case ENGINE_KEY_XBUTTON2: return "XButton2";
    default: return NULL;
    }
}

int engine_is_mouse_button(uint16_t id)
{
    return id >= ENGINE_KEY_LBUTTON && id <= ENGINE_KEY_XBUTTON2;
}

int engine_repeat_is_missed_up(uint16_t id, int down, int already)
{
    if (!down || !already)
        return 0;
    return id == ENGINE_KEY_XBUTTON1 || id == ENGINE_KEY_XBUTTON2;
}

int engine_can_trigger(uint16_t id)
{
    if (id == 0 || id == ENGINE_KEY_PAUSE)
        return 0;
    if (id == ENGINE_KEY_LBUTTON || id == ENGINE_KEY_RBUTTON)
        return 0;
    return 1;
}

int engine_panic_edge(uint16_t id, int was_down, int down)
{
    return id == ENGINE_KEY_PAUSE && down && !was_down;
}

int engine_focus_match(const char *want, const char *have)
{
    char left[260];
    char right[260];
    int ln = stem_copy(want, left, (int)sizeof left);
    int rn = stem_copy(have, right, (int)sizeof right);

    if (ln <= 0 || rn <= 0)
        return 0;
    if (strcmp(left, right) == 0)
        return 1;
    if (stem_prefix(right, left) || stem_prefix(left, right))
        return 1;
    return 0;
}

int engine_shell_in_front(const char *front, const char *owner)
{
    if (engine_focus_match("MacroShell.exe", front))
        return 1;
    if (!engine_focus_match("msedgewebview2.exe", front))
        return 0;
    return engine_focus_match("MacroShell.exe", owner);
}

int engine_focus_swallow(const char *focus_exe, const char *front, const char *owner)
{
    char stem[260];
    int n = stem_copy(focus_exe, stem, (int)sizeof stem);

    if (n < 0)
        return 0;
    if (n == 0)
        return !engine_shell_in_front(front, owner);
    if (!front || front[0] == 0)
        return 0;
    return engine_focus_match(focus_exe, front);
}

void engine_swallow_for_trigger(const struct engine_swallow_rule *rule,
                                int *swallow, int *pass)
{
    int do_pass;
    int do_swallow;

    if (!rule) {
        if (swallow)
            *swallow = 0;
        if (pass)
            *pass = 0;
        return;
    }
    do_pass = rule->pass != 0;
    do_swallow = rule->is_trigger && rule->focus_ok && !do_pass;
    if (swallow)
        *swallow = do_swallow;
    if (pass)
        *pass = do_pass;
}

uint8_t *engine_swallow_begin(struct engine_hook_state *st)
{
    unsigned index;
    uint8_t *bits;

    if (!st)
        return NULL;
    index = atomic_load_explicit(&st->swallow_index, memory_order_relaxed) & 1u;
    bits = st->swallow[index ^ 1u].bits;
    memset(bits, 0, sizeof st->swallow[0].bits);
    return bits;
}

void engine_swallow_set(uint8_t *bits, uint16_t id, int swallow, int pass)
{
    uint8_t value = 0;

    if (!bits || id > ENGINE_KEY_ID_MAX)
        return;
    if (swallow)
        value |= ENGINE_SWALLOW_BIT;
    if (pass)
        value |= ENGINE_PASS_BIT;
    bits[id] = value;
}

void engine_swallow_publish(struct engine_hook_state *st)
{
    unsigned index;

    if (!st)
        return;
    index = atomic_load_explicit(&st->swallow_index, memory_order_relaxed) & 1u;
    atomic_store_explicit(&st->swallow_index, index ^ 1u, memory_order_release);
}

void engine_hook_set_wake(struct engine_hook_state *st, int wake)
{
    if (!st)
        return;
    atomic_store_explicit(&st->wake_on_input, wake ? 1 : 0, memory_order_release);
}

void engine_hook_set_need_mouse(struct engine_hook_state *st, int need)
{
    if (!st)
        return;
    atomic_store_explicit(&st->need_mouse, need ? 1 : 0, memory_order_release);
}

void engine_hook_set_suspect(struct engine_hook_state *st)
{
    if (!st)
        return;
    atomic_store_explicit(&st->suspect, 1, memory_order_release);
}

void engine_hook_set_stop(struct engine_hook_state *st, int stop)
{
    if (!st)
        return;
    atomic_store_explicit(&st->stop, stop ? 1 : 0, memory_order_release);
}

int engine_echo_push(struct engine_hook_state *st, uint16_t id, int down, int64_t until)
{
    uint32_t head;
    uint32_t tail;
    struct engine_echo *token;

    if (!st || id == 0 || id > ENGINE_KEY_ID_MAX)
        return 0;
    head = atomic_load_explicit(&st->echo_head, memory_order_acquire);
    tail = atomic_load_explicit(&st->echo_tail, memory_order_relaxed);
    if (tail - head >= ENGINE_ECHO_CAP)
        return 0;
    token = &st->echoes[tail & (ENGINE_ECHO_CAP - 1u)];
    token->until = until;
    token->id = id;
    token->down = down ? 1 : 0;
    token->consumed = 0;
    atomic_store_explicit(&st->echo_tail, tail + 1u, memory_order_release);
    return 1;
}

int engine_echo_match(struct engine_hook_state *st, uint16_t id, int down, int64_t now)
{
    int found = 0;

    if (!st)
        return 0;
    echo_reap(st, id, down, now, &found);
    return found;
}

int engine_event_push(struct engine_hook_state *st, const struct engine_finger *finger)
{
    uint32_t head;
    uint32_t tail;

    if (!st || !finger)
        return 0;
    head = atomic_load_explicit(&st->event_head, memory_order_acquire);
    tail = atomic_load_explicit(&st->event_tail, memory_order_relaxed);
    if (tail - head >= ENGINE_EVENT_CAP) {
        atomic_store_explicit(&st->event_overflow, 1, memory_order_release);
        return 0;
    }
    st->events[tail & (ENGINE_EVENT_CAP - 1u)] = *finger;
    atomic_store_explicit(&st->event_tail, tail + 1u, memory_order_release);
    return 1;
}

int engine_event_pop(struct engine_hook_state *st, struct engine_finger *finger)
{
    uint32_t head;
    uint32_t tail;

    if (!st || !finger)
        return 0;
    head = atomic_load_explicit(&st->event_head, memory_order_relaxed);
    tail = atomic_load_explicit(&st->event_tail, memory_order_acquire);
    if (head == tail)
        return 0;
    *finger = st->events[head & (ENGINE_EVENT_CAP - 1u)];
    atomic_store_explicit(&st->event_head, head + 1u, memory_order_release);
    return 1;
}

int engine_event_overflow(struct engine_hook_state *st)
{
    if (!st)
        return 0;
    return atomic_load_explicit(&st->event_overflow, memory_order_acquire) != 0;
}

void engine_event_clear_overflow(struct engine_hook_state *st)
{
    if (!st)
        return;
    atomic_store_explicit(&st->event_overflow, 0, memory_order_release);
}

void engine_hook_classify(struct engine_hook_state *st,
                          const struct engine_hook_sample *sample,
                          struct engine_hook_decision *out)
{
    uint16_t id = 0;
    int down = 0;
    struct engine_finger finger;
    uint8_t bits;

    if (out) {
        out->swallow = 0;
        out->enqueued = 0;
        out->wake = 0;
    }
    if (!st || !sample || !out)
        return;
    if (sample->kind == ENGINE_HOOK_MOUSE && mouse_motion(sample->msg))
        return;

    if (sample->kind == ENGINE_HOOK_KEY)
        note_stamp(&st->key_stamp, sample->now);
    else if (sample->kind == ENGINE_HOOK_MOUSE)
        note_stamp(&st->mouse_stamp, sample->now);
    else
        return;

    if (sample->extra == ENGINE_EXTRA_ECHO)
        return;
    if (sample->kind == ENGINE_HOOK_KEY) {
        if (sample->flags & ENGINE_KEY_INJECTED)
            return;
        id = engine_resolve_key(sample->vk, sample->scan, sample->flags);
        down = !key_is_up(sample->msg, sample->flags);
    } else {
        if (sample->flags & ENGINE_MOUSE_INJECTED)
            return;
        id = engine_resolve_mouse(sample->msg, sample->mouse_data, &down);
    }
    if (id == 0)
        return;
    if (engine_echo_match(st, id, down, sample->now))
        return;

    finger.id = id;
    finger.down = down ? 1 : 0;
    finger.unused = 0;
    finger.qpc = sample->now;
    if (engine_event_push(st, &finger)) {
        out->enqueued = 1;
        if (atomic_load_explicit(&st->wake_on_input, memory_order_acquire))
            out->wake = 1;
    }
    if (id == ENGINE_KEY_LBUTTON || id == ENGINE_KEY_RBUTTON)
        return;
    bits = swallow_bits(st, id);
    if ((bits & ENGINE_SWALLOW_BIT) && !(bits & ENGINE_PASS_BIT))
        out->swallow = 1;
}

void engine_hook_watch(const struct engine_hook_watch_in *in,
                       struct engine_hook_watch_out *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);
    if (!in)
        return;

    if (!in->key_installed) {
        push_op(out, ENGINE_HOOK_RAISE_KEY);
        push_op(out, ENGINE_HOOK_RAISE_MOUSE);
        return;
    }
    if (in->suspect) {
        out->clear_suspect = 1;
        push_op(out, ENGINE_HOOK_RAISE_KEY);
        push_op(out, ENGINE_HOOK_RAISE_MOUSE);
        return;
    }
    /* The lock screen can remove a hook whose handle still looks valid.
       Coming back is the evidence. A quiet stamp is not. */
    if (in->desktop) {
        push_op(out, ENGINE_HOOK_RAISE_KEY);
        if (in->need_mouse || in->mouse_installed)
            push_op(out, ENGINE_HOOK_RAISE_MOUSE);
        return;
    }
    if (!in->armed && !in->running && !in->pending)
        return;

    if (in->need_mouse && !in->mouse_installed)
        push_op(out, ENGINE_HOOK_RAISE_MOUSE);
    else if (!in->need_mouse && in->mouse_installed)
        push_op(out, ENGINE_HOOK_RAISE_MOUSE);
}

void engine_hook_post_ops(struct engine_hook_state *st,
                          const struct engine_hook_watch_out *out)
{
    uint32_t packed = 0;
    int n;
    int i;

    if (!st || !out)
        return;
    n = out->count;
    if (n > 4)
        n = 4;
    for (i = 0; i < n; i++) {
        uint32_t op = out->op[i];
        if (op == 0 || op > 0xffu)
            break;
        packed |= op << (8 * i);
    }
    atomic_store_explicit(&st->pending_ops, packed, memory_order_release);
}

int engine_hook_take_ops(struct engine_hook_state *st,
                         struct engine_hook_watch_out *out)
{
    uint32_t packed;
    int i;

    if (!out)
        return 0;
    memset(out, 0, sizeof *out);
    if (!st)
        return 0;
    packed = atomic_exchange_explicit(&st->pending_ops, 0u, memory_order_acq_rel);
    for (i = 0; i < 4; i++) {
        uint8_t op = (uint8_t)((packed >> (8 * i)) & 0xffu);
        if (!op)
            break;
        out->op[out->count++] = op;
    }
    return out->count;
}
