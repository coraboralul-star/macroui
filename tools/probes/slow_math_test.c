#include "slow_math.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "fail %s\n", name);
        fails++;
    }
}

static struct slow_event mouse(int gate, int focus, uint64_t device, int32_t dx, int32_t dy)
{
    struct slow_event event;

    memset(&event, 0, sizeof event);
    event.gate = gate;
    event.focus = focus;
    event.motion = 1;
    event.device = device;
    event.dx = dx;
    event.dy = dy;
    return event;
}

static struct slow_result step(struct slow_session *session, struct slow_event event)
{
    struct slow_result result;

    memset(&result, 0, sizeof result);
    slow_session_on_raw(session, &event, &result);
    return result;
}

static void check_half(void)
{
    struct slow_session session;
    struct slow_result r;
    int32_t got[4];
    int i;
    int64_t sum = 0;

    slow_session_init(&session);
    r = step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    expect(r.armed && r.skip == SLOW_SKIP_STILL, "down arms");

    for (i = 0; i < 4; i++) {
        r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
        got[i] = r.dx;
        expect(r.dy == 0 && r.skip == SLOW_APPLIED, "half applied");
    }
    expect(got[0] == 0 && got[1] == -1 && got[2] == 0 && got[3] == -1, "half stair");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 100, 0));
    expect(r.dx == -50 && r.dy == 0, "half of 100");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 101, 0));
    expect(r.dx == -50, "101 leaves one");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    expect(r.dx == -1, "leftover completes");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, -1, 0));
    expect(r.dx == 0, "negative holds");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, -1, 0));
    expect(r.dx == 1, "negative completes");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 4, -6));
    expect(r.dx == -2 && r.dy == 3, "axes at once");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    for (i = 0; i < 1000; i++) {
        r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
        sum += r.dx;
    }
    expect(sum == -500, "thousand counts average half");
}

static void check_axes_and_devices(void)
{
    struct slow_session session;
    struct slow_result r;

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 2, 0));
    expect(r.dx == -1 && r.dy == 0, "x only");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 0, 2));
    expect(r.dx == 0 && r.dy == -1, "y only");

    r = step(&session, mouse(SLOW_GATE_NONE, 1, 2, 1, 0));
    expect(r.dx == 0, "second device starts clean");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 2, 1, 0));
    expect(r.dx == -1, "second device half");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 2, 0));
    expect(r.dx == -1, "first device unchanged by the second");
}

static void check_exclusions(void)
{
    struct slow_session session;
    struct slow_result r;
    struct slow_event event;

    slow_session_init(&session);
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 4, 0));
    expect(!r.armed && r.dx == 0 && r.skip == SLOW_SKIP_DISARMED, "idle ignores motion");

    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    expect(r.dx == 0 && r.armed, "remainder pending");

    event = mouse(SLOW_GATE_NONE, 1, 0, -1, 0);
    r = step(&session, event);
    expect(r.skip == SLOW_SKIP_NULL && r.dx == 0, "null device ignored");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    expect(r.dx == -1, "null did not feed back");

    event = mouse(SLOW_GATE_NONE, 1, 7, 10, 0);
    event.board = 1;
    r = step(&session, event);
    expect(r.skip == SLOW_SKIP_BOARD && r.dx == 0, "board ignored");

    event = mouse(SLOW_GATE_NONE, 1, 0, 10, 0);
    event.board = 1;
    r = step(&session, event);
    expect(r.skip == SLOW_SKIP_NULL, "null wins over board flag");

    event = mouse(SLOW_GATE_NONE, 1, 7, 10, 0);
    event.absolute = 1;
    r = step(&session, event);
    expect(r.skip == SLOW_SKIP_ABSOLUTE && r.dx == 0, "absolute ignored");

    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    expect(r.dx == 0, "exclusions left the next odd count pending");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    expect(r.dx == -1, "sequence resumes");
}

static void check_cleanup(void)
{
    struct slow_session session;
    struct slow_result r;
    struct slow_event event;
    int i;

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    r = step(&session, mouse(SLOW_GATE_UP, 1, 1, 0, 0));
    expect(r.edge == SLOW_EDGE_RELEASE && !r.armed && r.discarded == 1, "release drops fraction");
    expect(r.dx == 0, "release does not emit the fraction");

    r = step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 1, 0));
    expect(r.armed && r.dx == 0, "rearm starts clean");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    expect(r.dx == -1, "rearm half is one");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    event = mouse(SLOW_GATE_NONE, 0, 1, 10, 0);
    r = step(&session, event);
    expect(r.edge == SLOW_EDGE_FOCUS && !r.armed && r.dx == 0 && r.discarded == 1, "focus drops");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    expect(!r.armed && r.skip == SLOW_SKIP_DISARMED, "held across focus stays off");
    r = step(&session, mouse(SLOW_GATE_UP, 1, 1, 0, 0));
    expect(!r.armed && r.edge == SLOW_EDGE_NONE, "release after focus is idle");
    r = step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 2, 0));
    expect(r.armed && r.dx == -1, "new press arms again");

    slow_session_init(&session);
    r = step(&session, mouse(SLOW_GATE_DOWN, 0, 1, 4, 0));
    expect(!r.armed && r.dx == 0, "press outside stays off");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 10, 0));
    expect(!r.armed && r.skip == SLOW_SKIP_DISARMED && r.dx == 0, "carry-in does not arm");
    step(&session, mouse(SLOW_GATE_UP, 1, 1, 0, 0));
    r = step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 1, 0));
    expect(r.armed && r.dx == 0, "press inside arms");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    event = mouse(SLOW_GATE_NONE, 1, 1, 8, 0);
    event.panic = 1;
    r = step(&session, event);
    expect(r.quit && r.edge == SLOW_EDGE_PANIC && r.dx == 0 && r.discarded == 1, "panic drops");
    expect(r.skip == SLOW_SKIP_STOPPED, "panic sends nothing");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    step(&session, mouse(SLOW_GATE_NONE, 1, 1, 2, 0));
    event = mouse(SLOW_GATE_NONE, 1, 1, 0, 0);
    event.motion = 0;
    event.leaving = 1;
    r = step(&session, event);
    expect(r.quit && r.edge == SLOW_EDGE_EXIT && r.discarded == 0 && r.dx == 0, "exit after an even count");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 2, 0));
    r = step(&session, mouse(SLOW_GATE_UP, 1, 1, 2, 0));
    expect(r.edge == SLOW_EDGE_RELEASE && r.dx == -1 && r.discarded == 0, "release packet still counts");
    r = step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 1, 0));
    expect(r.dx == 0, "no leftover after an even release");

    slow_session_init(&session);
    for (i = 1; i <= SLOW_DEVICE_CAP; i++) {
        if (i == 1)
            step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 1, 0));
        else
            step(&session, mouse(SLOW_GATE_NONE, 1, (uint64_t)i, 1, 0));
    }
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 99, 5, 0));
    expect(r.skip == SLOW_SKIP_FULL && r.dx == 0, "ninth device skipped");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 1, 0));
    expect(r.dx == -1, "first device kept its remainder");
}

static void check_gate_edges(void)
{
    struct slow_session session;
    struct slow_result r;
    struct slow_event event;

    slow_session_init(&session);
    event = mouse(SLOW_GATE_DOWN, 1, 0, 0, 0);
    event.motion = 0;
    r = step(&session, event);
    expect(r.armed && r.skip == SLOW_SKIP_NO_SAMPLE && r.dx == 0, "key down arms without a mouse");
    r = step(&session, mouse(SLOW_GATE_NONE, 1, 1, 2, 0));
    expect(r.dx == -1, "mouse after key gate");

    event = mouse(SLOW_GATE_DOWN, 1, 1, 4, 0);
    r = step(&session, event);
    expect(r.armed && r.dx == -2, "repeat down is not a second arm");

    slow_session_init(&session);
    step(&session, mouse(SLOW_GATE_DOWN, 1, 1, 0, 0));
    r = step(&session, mouse(SLOW_GATE_UP, 0, 1, 6, 0));
    expect(r.edge == SLOW_EDGE_FOCUS && r.dx == 0 && !r.armed, "focus loss beats the release packet");
}

static void check_board_name(void)
{
    expect(slow_text_has_board("\\\\?\\HID#VID_CAFE&PID_4004#1") == 1, "board path");
    expect(slow_text_has_board("vid_cafe&pid_4004") == 1, "board lower");
    expect(slow_text_has_board("VID_CAFE&PID_4005") == 0, "other pid");
    expect(slow_text_has_board("VID_CAFE") == 0, "vendor only");
    expect(slow_text_has_board("") == 0, "empty name");
    expect(slow_text_has_board(0) == 0, "null name");
}

int main(void)
{
    check_half();
    check_axes_and_devices();
    check_exclusions();
    check_cleanup();
    check_gate_edges();
    check_board_name();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("slow-math-test ok\n");
    return 0;
}
