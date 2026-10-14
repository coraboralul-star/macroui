/* Software output. One array, one SendInput.
   dwExtraInfo is 0x56454E44 on every INPUT, keyboard and mouse.
   timeBeginPeriod and AvSetMmThreadCharacteristicsW are not called. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string.h>

#include "output_win.h"

_Static_assert(sizeof(INPUT) == 40, "x64 INPUT");
_Static_assert(ENGINE_EXTRA_ECHO == 0x56454E44ull, "echo");
_Static_assert(KEYEVENTF_EXTENDEDKEY == ENGINE_SEND_EXTENDED, "extended");
_Static_assert(KEYEVENTF_KEYUP == ENGINE_SEND_KEYUP, "keyup");
_Static_assert(MOUSEEVENTF_LEFTDOWN == ENGINE_MOUSE_LEFTDOWN, "left down");
_Static_assert(MOUSEEVENTF_LEFTUP == ENGINE_MOUSE_LEFTUP, "left up");
_Static_assert(MOUSEEVENTF_RIGHTDOWN == ENGINE_MOUSE_RIGHTDOWN, "right down");
_Static_assert(MOUSEEVENTF_RIGHTUP == ENGINE_MOUSE_RIGHTUP, "right up");
_Static_assert(MOUSEEVENTF_MIDDLEDOWN == ENGINE_MOUSE_MIDDLEDOWN, "middle down");
_Static_assert(MOUSEEVENTF_MIDDLEUP == ENGINE_MOUSE_MIDDLEUP, "middle up");
_Static_assert(MOUSEEVENTF_XDOWN == ENGINE_MOUSE_XDOWN, "x down");
_Static_assert(MOUSEEVENTF_XUP == ENGINE_MOUSE_XUP, "x up");
_Static_assert(MOUSEEVENTF_WHEEL == ENGINE_MOUSE_WHEEL, "wheel");
_Static_assert(WHEEL_DELTA == ENGINE_WHEEL_DELTA, "wheel delta");

static void absolute_point(int32_t ax, int32_t ay, LONG *dx, LONG *dy, DWORD *flags)
{
    int origin_x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int origin_y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int64_t x = (int64_t)ax - origin_x;
    int64_t y = (int64_t)ay - origin_y;

    *flags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    if (width < 2)
        width = 2;
    if (height < 2)
        height = 2;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x > (int64_t)width - 1)
        x = (int64_t)width - 1;
    if (y > (int64_t)height - 1)
        y = (int64_t)height - 1;
    *dx = (LONG)(x * 65535 / (width - 1));
    *dy = (LONG)(y * 65535 / (height - 1));
}

static void fill_one(const struct engine_send_op *op, INPUT *input)
{
    memset(input, 0, sizeof *input);
    if (op->kind == ENGINE_SEND_KEY) {
        input->type = INPUT_KEYBOARD;
        input->ki.wVk = op->vk;
        input->ki.dwFlags = op->flags;
        input->ki.dwExtraInfo = (ULONG_PTR)ENGINE_EXTRA_ECHO;
        return;
    }
    input->type = INPUT_MOUSE;
    input->mi.dwExtraInfo = (ULONG_PTR)ENGINE_EXTRA_ECHO;
    if (op->kind == ENGINE_SEND_BUTTON) {
        input->mi.dwFlags = op->flags;
        input->mi.mouseData = op->data;
        return;
    }
    if (op->kind == ENGINE_SEND_WHEEL) {
        input->mi.dwFlags = MOUSEEVENTF_WHEEL;
        input->mi.mouseData = op->data;
        return;
    }
    if (op->kind == ENGINE_SEND_MOVE) {
        input->mi.dwFlags = MOUSEEVENTF_MOVE;
        input->mi.dx = op->x;
        input->mi.dy = op->y;
        return;
    }
    if (op->kind == ENGINE_SEND_ABS) {
        absolute_point(op->x, op->y, &input->mi.dx, &input->mi.dy, &input->mi.dwFlags);
    }
}

int engine_win_fill(const struct engine_send_op *ops, int count, INPUT *out, int cap)
{
    int n = 0;
    int i;

    if (!ops || !out || count <= 0 || cap <= 0)
        return 0;
    if (count > cap)
        count = cap;
    for (i = 0; i < count; i++) {
        fill_one(&ops[i], &out[n]);
        n++;
    }
    return n;
}

int engine_win_send_report(const struct engine_report *report, int swap_clicks)
{
    struct engine_send_op ops[ENGINE_SEND_CAP];
    INPUT inputs[ENGINE_SEND_CAP];
    int count = 0;
    int filled;

    if (!report)
        return 0;
    if (!engine_report_send(report, swap_clicks, ops, ENGINE_SEND_CAP, &count) || count <= 0)
        return 0;
    filled = engine_win_fill(ops, count, inputs, ENGINE_SEND_CAP);
    if (filled <= 0)
        return 0;
    return (int)SendInput((UINT)filled, inputs, (int)sizeof(INPUT));
}
