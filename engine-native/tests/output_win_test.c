#include "output_win.h"

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

static void check_fill(void)
{
    struct engine_report report;
    struct engine_send_op ops[ENGINE_SEND_CAP];
    INPUT inputs[ENGINE_SEND_CAP];
    int count = 0;
    int filled;
    DWORD abs_flags;

    engine_report_clear(&report);
    engine_report_push_down(&report, 0x20);
    engine_report_set_relative(&report, 4, -3);
    engine_report_set_wheel(&report, 1);
    engine_report_send(&report, 0, ops, ENGINE_SEND_CAP, &count);
    filled = engine_win_fill(ops, count, inputs, ENGINE_SEND_CAP);
    expect(filled == 3, "three inputs");
    expect(inputs[0].type == INPUT_KEYBOARD && inputs[0].ki.wVk == 0x20, "space");
    expect(inputs[0].ki.dwExtraInfo == (ULONG_PTR)ENGINE_EXTRA_ECHO, "key echo");
    expect(inputs[1].type == INPUT_MOUSE && inputs[1].mi.dwFlags == MOUSEEVENTF_WHEEL, "wheel");
    expect(inputs[1].mi.mouseData == (DWORD)(int)-120, "wheel data");
    expect(inputs[1].mi.dwExtraInfo == (ULONG_PTR)ENGINE_EXTRA_ECHO, "wheel echo");
    expect(inputs[2].mi.dwFlags == MOUSEEVENTF_MOVE && inputs[2].mi.dx == 4 && inputs[2].mi.dy == -3,
           "relative move");
    expect(inputs[2].mi.dwExtraInfo == (ULONG_PTR)ENGINE_EXTRA_ECHO, "move echo");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    engine_report_send(&report, 1, ops, ENGINE_SEND_CAP, &count);
    engine_win_fill(ops, count, inputs, ENGINE_SEND_CAP);
    expect(inputs[0].type == INPUT_MOUSE && inputs[0].mi.dwFlags == MOUSEEVENTF_RIGHTDOWN,
           "swap fills the right button");
    expect(inputs[0].mi.dwExtraInfo == (ULONG_PTR)ENGINE_EXTRA_ECHO, "button echo");

    engine_report_clear(&report);
    engine_report_set_absolute(&report, 10, 20);
    engine_report_send(&report, 0, ops, ENGINE_SEND_CAP, &count);
    engine_win_fill(ops, count, inputs, ENGINE_SEND_CAP);
    abs_flags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    expect(inputs[0].mi.dwFlags == abs_flags, "absolute flags");
    expect(inputs[0].mi.dx >= 0 && inputs[0].mi.dx <= 65535, "absolute x is normalized");
    expect(inputs[0].mi.dy >= 0 && inputs[0].mi.dy <= 65535, "absolute y is normalized");
    expect(inputs[0].mi.dwExtraInfo == (ULONG_PTR)ENGINE_EXTRA_ECHO, "absolute echo");
}

static void check_send(void)
{
    struct engine_report report;
    INPUT move;

    engine_report_clear(&report);
    expect(engine_win_send_report(&report, 0) == 0, "an empty image is not sent");
    engine_report_set_dpi(&report, 96);
    expect(engine_win_send_report(&report, 0) == 0, "dpi alone is not sent");

    memset(&move, 0, sizeof move);
    move.type = INPUT_MOUSE;
    move.mi.dwFlags = MOUSEEVENTF_MOVE;
    move.mi.dwExtraInfo = (ULONG_PTR)ENGINE_EXTRA_ECHO;
    expect(SendInput(1, &move, (int)sizeof move) == 1, "a zero move is accepted");
}

int main(void)
{
    check_fill();
    check_send();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
