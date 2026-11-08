#include "slow_math.h"

#include <stdint.h>
#include <string.h>

#define SLOW_OWED (SLOW_DEN - SLOW_KEEP)

static int disarm(struct slow_session *session)
{
    int n = 0;
    int i;

    session->armed = 0;
    for (i = 0; i < SLOW_DEVICE_CAP; i++) {
        if (session->slot[i].acc_x != 0)
            n++;
        if (session->slot[i].acc_y != 0)
            n++;
        session->slot[i].id = 0;
        session->slot[i].acc_x = 0;
        session->slot[i].acc_y = 0;
    }
    return n;
}

static int32_t negate_emit(int64_t emit)
{
    int64_t counter = -emit;

    if (counter > INT32_MAX)
        return INT32_MAX;
    if (counter < INT32_MIN)
        return INT32_MIN;
    return (int32_t)counter;
}

/* acc holds the unsent part of (1-s)*counts, in units of 1/SLOW_DEN.
   Division truncates toward zero, so a lone +1 at one half emits nothing
   and the next +1 emits -1. */
static void step_axis(int32_t *acc, int32_t delta, int32_t *counter)
{
    int64_t sum = (int64_t)(*acc) + (int64_t)delta * (int64_t)SLOW_OWED;
    int64_t emit = sum / SLOW_DEN;
    int64_t left = sum - emit * SLOW_DEN;

    *acc = (int32_t)left;
    *counter = negate_emit(emit);
}

static struct slow_slot *slot_for(struct slow_session *session, uint64_t id, int *full)
{
    int i;
    int free_slot = -1;

    *full = 0;
    for (i = 0; i < SLOW_DEVICE_CAP; i++) {
        if (session->slot[i].id == id)
            return &session->slot[i];
        if (session->slot[i].id == 0 && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0) {
        *full = 1;
        return 0;
    }
    session->slot[free_slot].id = id;
    session->slot[free_slot].acc_x = 0;
    session->slot[free_slot].acc_y = 0;
    return &session->slot[free_slot];
}

static void apply_packet(struct slow_session *session, const struct slow_event *event,
                         struct slow_result *result)
{
    struct slow_slot *slot;
    int full = 0;

    if (event->device == 0) {
        result->skip = SLOW_SKIP_NULL;
        return;
    }
    if (event->board) {
        result->skip = SLOW_SKIP_BOARD;
        return;
    }
    if (event->absolute) {
        result->skip = SLOW_SKIP_ABSOLUTE;
        return;
    }
    if (event->dx == 0 && event->dy == 0) {
        result->skip = SLOW_SKIP_STILL;
        return;
    }
    slot = slot_for(session, event->device, &full);
    if (!slot) {
        result->skip = SLOW_SKIP_FULL;
        return;
    }
    step_axis(&slot->acc_x, event->dx, &result->dx);
    step_axis(&slot->acc_y, event->dy, &result->dy);
    result->skip = SLOW_APPLIED;
}

static int lower_ascii(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 'a';
    return c;
}

int slow_text_has_board(const char *name)
{
    static const char want[] = "vid_cafe&pid_4004";
    const char *p;

    if (!name)
        return 0;
    for (p = name; *p; p++) {
        int i;

        for (i = 0; want[i]; i++) {
            unsigned char c = (unsigned char)p[i];

            if (c == 0 || lower_ascii(c) != (unsigned char)want[i])
                break;
        }
        if (want[i] == 0)
            return 1;
    }
    return 0;
}

void slow_session_init(struct slow_session *session)
{
    if (!session)
        return;
    memset(session, 0, sizeof *session);
}

void slow_session_on_raw(struct slow_session *session, const struct slow_event *event,
                         struct slow_result *result)
{
    struct slow_result out;
    int apply = 0;

    memset(&out, 0, sizeof out);
    out.skip = SLOW_SKIP_DISARMED;
    if (!session || !event || !result)
        return;

    if (event->panic || event->leaving) {
        out.discarded = disarm(session);
        out.edge = event->panic ? SLOW_EDGE_PANIC : SLOW_EDGE_EXIT;
        out.quit = 1;
        out.skip = SLOW_SKIP_STOPPED;
        goto done;
    }

    if (!event->focus) {
        if (session->armed) {
            out.discarded = disarm(session);
            out.edge = SLOW_EDGE_FOCUS;
        }
        if (event->gate == SLOW_GATE_UP) {
            session->gate_down = 0;
            session->suppress = 0;
        } else if (event->gate == SLOW_GATE_DOWN || session->gate_down) {
            session->gate_down = 1;
            session->suppress = 1;
        }
        out.skip = SLOW_SKIP_DISARMED;
        goto done;
    }

    if (event->gate == SLOW_GATE_UP) {
        int was = session->armed;

        session->gate_down = 0;
        session->suppress = 0;
        if (was) {
            apply = 1;
            out.edge = SLOW_EDGE_RELEASE;
        }
    } else if (event->gate == SLOW_GATE_DOWN) {
        if (!session->gate_down) {
            session->gate_down = 1;
            if (!session->suppress)
                session->armed = 1;
        }
        apply = session->armed;
    } else {
        apply = session->armed;
    }

    if (apply && event->motion)
        apply_packet(session, event, &out);
    else if (!event->motion)
        out.skip = SLOW_SKIP_NO_SAMPLE;
    else
        out.skip = SLOW_SKIP_DISARMED;

    if (out.edge == SLOW_EDGE_RELEASE)
        out.discarded = disarm(session);

done:
    out.armed = session->armed;
    *result = out;
}
