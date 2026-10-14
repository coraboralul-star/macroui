#include "output.h"

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

static int hex_nibble(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int parse_hex(const char *text, uint8_t *out, int cap)
{
    int count = 0;

    while (*text) {
        int hi;
        int lo;

        while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
            text++;
        if (*text == 0)
            break;
        hi = hex_nibble((unsigned char)*text++);
        if (*text == 0)
            return -1;
        lo = hex_nibble((unsigned char)*text++);
        if (hi < 0 || lo < 0 || count >= cap)
            return -1;
        out[count++] = (uint8_t)((hi << 4) | lo);
    }
    return count;
}

static int load_frame(const char *name, uint8_t *out, int cap)
{
    static const char *paths[] = {
        "docs/engine/ipc-vectors.txt",
        "../docs/engine/ipc-vectors.txt",
        "../../docs/engine/ipc-vectors.txt"
    };
    FILE *file = NULL;
    char line[2048];
    size_t name_len = strlen(name);
    int i;

    for (i = 0; i < 3; i++) {
#ifdef _MSC_VER
        if (fopen_s(&file, paths[i], "rb") != 0)
            file = NULL;
#else
        file = fopen(paths[i], "rb");
#endif
        if (file)
            break;
    }
    if (!file)
        return -1;
    while (fgets(line, (int)sizeof line, file)) {
        if (strncmp(line, name, name_len) == 0 && line[name_len] == ' ')
            break;
    }
    if (feof(file) || !fgets(line, (int)sizeof line, file)) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return parse_hex(line, out, cap);
}

static void expect_payload(const struct engine_report *report, const char *name)
{
    uint8_t raw[256];
    int count = load_frame(name, raw, (int)sizeof raw);
    int i;

    expect(count == 12 + ENGINE_REPORT_BYTES, name);
    if (count != 12 + ENGINE_REPORT_BYTES)
        return;
    for (i = 0; i < ENGINE_REPORT_BYTES; i++) {
        if (report->bytes[i] == raw[12 + i])
            continue;
        fprintf(stderr, "fail %s at %d got %02X want %02X\n",
                name, i, report->bytes[i], raw[12 + i]);
        fails++;
        return;
    }
}

static void check_vectors(void)
{
    struct engine_report report;

    engine_report_clear(&report);
    engine_report_push_down(&report, 0x20);
    engine_report_set_relative(&report, 4, -3);
    engine_report_set_wheel(&report, 1);
    engine_report_set_dpi(&report, 96);
    expect_payload(&report, "report-relative");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    engine_report_set_absolute(&report, 10, 20);
    engine_report_set_dpi(&report, 120);
    expect(engine_report_dx(&report) == 0 && engine_report_dy(&report) == 0, "absolute clears relative");
    expect_payload(&report, "report-absolute");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    engine_report_push_down(&report, 0x57);
    engine_report_push_down(&report, 0x20);
    engine_report_set_relative(&report, 4, -3);
    engine_report_set_dpi(&report, 96);
    expect_payload(&report, "report-three-downs");

    engine_report_clear(&report);
    engine_report_push_up(&report, 0x20);
    engine_report_set_dpi(&report, 96);
    expect_payload(&report, "report-up");

    engine_report_clear(&report);
    engine_report_push_down(&report, 0x20);
    engine_report_set_overflow(&report);
    engine_report_set_dpi(&report, 96);
    expect_payload(&report, "report-overflow");
}

static void check_image_rules(void)
{
    struct engine_report report;
    int i;

    engine_report_clear(&report);
    expect(engine_report_active(&report) == 0, "zeros are not sent");
    engine_report_set_dpi(&report, 96);
    expect(engine_report_active(&report) == 0, "dpi alone is not sent");
    engine_report_set_overflow(&report);
    expect(engine_report_active(&report) == 0, "overflow alone is not sent");
    expect(engine_report_push_down(&report, 0) == 0, "id 0 is skipped");
    expect(engine_report_down_count(&report) == 0, "id 0 does not count");
    engine_report_set_relative(&report, 16001, -16001);
    expect(engine_report_dx(&report) == 16000 && engine_report_dy(&report) == -16000, "move clamps");
    expect(engine_report_load(&report, report.bytes, 10) == 0, "short image is refused");
    engine_report_clear(&report);
    for (i = 0; i < ENGINE_REPORT_CAP; i++)
        expect(engine_report_push_down(&report, (uint16_t)(i + 1)) == 1, "down fits");
    expect(engine_report_push_down(&report, 0x20) == 0, "down 33 is dropped");
    expect(engine_report_flags(&report) & ENGINE_REPORT_OVERFLOW, "a full down list overflows");
    expect(engine_report_down_count(&report) == ENGINE_REPORT_CAP, "the count stays 32");
}

static void check_send_plan(void)
{
    struct engine_report report;
    struct engine_report copy;
    struct engine_send_op ops[ENGINE_SEND_CAP];
    int count = 0;

    engine_report_clear(&report);
    engine_report_push_down(&report, 0x20);
    engine_report_set_relative(&report, 4, -3);
    engine_report_set_wheel(&report, 1);
    engine_report_set_dpi(&report, 96);
    copy = report;
    expect(engine_report_send(&report, 0, ops, ENGINE_SEND_CAP, &count) == 1, "relative sends");
    expect(count == 3, "key, wheel, move");
    expect(ops[0].kind == ENGINE_SEND_KEY && ops[0].vk == 0x20 && ops[0].flags == 0, "space down");
    expect(ops[1].kind == ENGINE_SEND_WHEEL && ops[1].data == (uint32_t)(int32_t)-120, "positive wheel is down");
    expect(ops[2].kind == ENGINE_SEND_MOVE && ops[2].x == 4 && ops[2].y == -3, "relative move");
    expect(memcmp(report.bytes, copy.bytes, sizeof report.bytes) == 0, "the send plan leaves the image");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    engine_report_send(&report, 1, ops, ENGINE_SEND_CAP, &count);
    expect(ops[0].kind == ENGINE_SEND_BUTTON && ops[0].id == ENGINE_KEY_RBUTTON, "swap left sends right");
    expect(ops[0].flags == ENGINE_MOUSE_RIGHTDOWN, "right down flag");
    expect(engine_report_down(&report, 0) == ENGINE_KEY_LBUTTON, "the image stays left");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_MBUTTON);
    engine_report_send(&report, 1, ops, ENGINE_SEND_CAP, &count);
    expect(ops[0].id == ENGINE_KEY_MBUTTON && ops[0].flags == ENGINE_MOUSE_MIDDLEDOWN, "swap leaves middle");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_NUMPAD_ENTER);
    engine_report_send(&report, 0, ops, ENGINE_SEND_CAP, &count);
    expect(ops[0].vk == 0x0D && (ops[0].flags & ENGINE_SEND_EXTENDED), "numpad enter is extended return");

    engine_report_clear(&report);
    engine_report_push_up(&report, 0x0D);
    engine_report_send(&report, 0, ops, ENGINE_SEND_CAP, &count);
    expect(ops[0].vk == 0x0D && ops[0].flags == ENGINE_SEND_KEYUP, "enter up is not extended");

    engine_report_clear(&report);
    engine_report_push_down(&report, 0xA3);
    engine_report_send(&report, 0, ops, ENGINE_SEND_CAP, &count);
    expect(ops[0].vk == 0xA3 && (ops[0].flags & ENGINE_SEND_EXTENDED), "right ctrl is extended");

    engine_report_clear(&report);
    engine_report_set_absolute(&report, 10, 20);
    engine_report_set_wheel(&report, -2);
    engine_report_send(&report, 0, ops, ENGINE_SEND_CAP, &count);
    expect(count == 2 && ops[0].kind == ENGINE_SEND_WHEEL && ops[0].data == 240, "negative wheel is up");
    expect(ops[1].kind == ENGINE_SEND_ABS && ops[1].x == 10 && ops[1].y == 20, "absolute follows the wheel");
}

static void check_board_plan(void)
{
    struct engine_report report;
    struct engine_board_op ops[ENGINE_BOARD_CAP];
    int count = 0;
    int bits;

    expect(engine_hid_usage(0x20) == 0x2C, "space usage");
    expect(engine_hid_usage(0x57) == 0x1A, "w usage");
    expect(engine_hid_usage(ENGINE_KEY_NUMPAD_ENTER) == 0x58, "numpad enter usage");
    expect(engine_hid_usage(0xA2) == 0xE0, "left ctrl usage");
    expect(engine_hid_usage(0x01) == 0, "unnamed id is skipped");
    expect(engine_hid_usage(ENGINE_KEY_LBUTTON) == 0, "a button is not a usage");

    engine_report_clear(&report);
    engine_report_push_down(&report, 0x20);
    engine_report_set_relative(&report, 4, -3);
    engine_report_set_wheel(&report, 1);
    engine_report_set_dpi(&report, 96);
    bits = engine_report_board(&report, 0, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(count == 2 && bits == 0, "space and a move");
    expect(ops[0].kind == ENGINE_BOARD_KEY && ops[0].usage == 0x2C && ops[0].down == 1, "K space down");
    expect(ops[1].kind == ENGINE_BOARD_MOVE && ops[1].x == 4 && ops[1].y == -3 && ops[1].wheel == -1,
           "board wheel is negated");

    engine_report_clear(&report);
    engine_report_push_down(&report, 0x20);
    engine_report_set_relative(&report, 1, -1);
    engine_report_set_wheel(&report, 1);
    engine_report_set_dpi(&report, 144);
    engine_report_board(&report, 0, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(ops[1].x == 2 && ops[1].y == -2 && ops[1].wheel == -1, "dpi 144 scales half away");

    engine_report_clear(&report);
    engine_report_set_relative(&report, 4, -3);
    engine_report_set_dpi(&report, 0);
    engine_report_board(&report, 0, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(count == 1 && ops[0].x == 4 && ops[0].y == -3, "dpi 0 is 96");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    engine_report_push_down(&report, 0x57);
    engine_report_push_down(&report, 0x20);
    engine_report_set_relative(&report, 4, -3);
    engine_report_set_dpi(&report, 96);
    bits = engine_report_board(&report, 0, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(bits == 1 && count == 3, "three downs");
    expect(ops[0].usage == 0x1A && ops[1].usage == 0x2C, "w then space");
    expect(ops[2].kind == ENGINE_BOARD_MOVE && ops[2].bits == 1 && ops[2].wheel == 0, "one M after the button");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    engine_report_set_absolute(&report, 10, 20);
    engine_report_set_dpi(&report, 120);
    bits = engine_report_board(&report, 0, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(count == 1 && ops[0].kind == ENGINE_BOARD_STEER && ops[0].x == 10 && ops[0].y == 20,
           "absolute is steer");
    expect(bits == 1, "left bit");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    engine_report_set_absolute(&report, 10, 20);
    engine_report_set_wheel(&report, 1);
    engine_report_board(&report, 0, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(count == 2 && ops[1].kind == ENGINE_BOARD_MOVE && ops[1].wheel == -1 && ops[1].x == 0,
           "absolute leaves the wheel");

    engine_report_clear(&report);
    engine_report_push_up(&report, ENGINE_KEY_LBUTTON);
    bits = engine_report_board(&report, 0, 1, ops, ENGINE_BOARD_CAP, &count);
    expect(bits == 0 && count == 1 && ops[0].kind == ENGINE_BOARD_MOVE && ops[0].bits == 0,
           "button up sends the cleared bits");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    engine_report_push_up(&report, ENGINE_KEY_LBUTTON);
    engine_report_board(&report, 0, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(count == 2 && ops[0].bits == 1 && ops[1].bits == 0, "click down then up");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    bits = engine_report_board(&report, 1, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(bits == 2 && ops[0].bits == 2, "swap left sends the right bit");

    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_MBUTTON);
    bits = engine_report_board(&report, 1, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(bits == 4, "swap leaves the middle bit");

    engine_report_clear(&report);
    engine_report_push_down(&report, 0x01);
    engine_report_board(&report, 0, 7, ops, ENGINE_BOARD_CAP, &count);
    expect(count == 0, "an unnamed id writes no line");

    engine_report_clear(&report);
    engine_report_set_wheel(&report, 2);
    engine_report_board(&report, 0, 0, ops, ENGINE_BOARD_CAP, &count);
    expect(count == 1 && ops[0].wheel == -2 && ops[0].x == 0 && ops[0].y == 0, "wheel only");
}

static void check_diff(void)
{
    struct engine_output out;
    struct engine_report report;
    struct engine_report_echo echoes[4];
    uint16_t held[3];
    uint16_t extra[1];
    uint16_t many[40];
    int i;

    engine_output_init(&out);
    held[0] = 0x20;
    expect(engine_output_publish(&out, held, 1, NULL, 0, 0, 0, 0, 0, 0, 0, 96, &report) == 1,
           "a new key is sent");
    expect(engine_report_down_count(&report) == 1 && engine_report_down(&report, 0) == 0x20, "space down");
    expect(out.applied_count == 1 && out.applied[0] == 0x20, "space is applied");
    expect(engine_output_publish(&out, held, 1, NULL, 0, 0, 0, 0, 0, 0, 0, 96, &report) == 0,
           "a held key is not sent again");

    held[0] = 0x57;
    held[1] = 0x20;
    engine_output_publish(&out, held, 2, NULL, 0, 0, 0, 0, 0, 0, 0, 96, &report);
    expect(engine_report_down_count(&report) == 1 && engine_report_down(&report, 0) == 0x57, "w is the new down");
    expect(out.applied_count == 2 && out.applied[0] == 0x20 && out.applied[1] == 0x57, "applied keeps order");

    held[0] = 0x57;
    engine_output_publish(&out, held, 1, NULL, 0, 0, 0, 0, 0, 0, 0, 96, &report);
    expect(engine_report_up_count(&report) == 1 && engine_report_up(&report, 0) == 0x20, "space up");
    expect(out.applied_count == 1 && out.applied[0] == 0x57, "w remains");

    extra[0] = 0x57;
    held[0] = 0x57;
    engine_output_publish(&out, held, 1, extra, 1, 0, 0, 0, 0, 0, 0, 96, &report);
    expect(engine_report_down_count(&report) == 1 && engine_report_down(&report, 0) == 0x57, "a repeat down");
    expect(out.applied_count == 1, "the repeat does not double applied");
    expect(engine_report_echoes(&report, echoes, 4) == 1 && echoes[0].id == 0x57 && echoes[0].down == 1,
           "echo uses the logical id");

    extra[0] = ENGINE_KEY_LBUTTON;
    held[0] = ENGINE_KEY_LBUTTON;
    held[1] = 0x57;
    engine_output_publish(&out, held, 2, extra, 1, 0, 0, 0, 0, 0, 0, 96, &report);
    expect(engine_report_down_count(&report) == 1 && engine_report_down(&report, 0) == ENGINE_KEY_LBUTTON,
           "left is the new down");
    held[0] = ENGINE_KEY_LBUTTON;
    engine_output_publish(&out, held, 1, extra, 1, 0, 0, 0, 0, 0, 0, 96, &report);
    expect(engine_report_down_count(&report) == 0, "a mouse repeat is not sent");
    expect(engine_report_up_count(&report) == 1 && engine_report_up(&report, 0) == 0x57, "w goes up");

    for (i = 0; i < 33; i++)
        many[i] = (uint16_t)(i + 1);
    engine_output_init(&out);
    engine_output_publish(&out, many, 33, NULL, 0, 0, 0, 0, 0, 0, 0, 96, &report);
    expect(engine_report_down_count(&report) == 32, "the image keeps 32 downs");
    expect(engine_report_flags(&report) & ENGINE_REPORT_OVERFLOW, "33 downs overflow");
    expect(out.applied_count == 33 && out.applied[32] == 33, "the extra down stays applied");
    engine_output_publish(&out, NULL, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 96, &report);
    expect(engine_report_up_count(&report) == 32, "the image keeps 32 ups");
    expect(engine_report_flags(&report) & ENGINE_REPORT_OVERFLOW, "33 ups overflow");
    expect(out.applied_count == 1 && out.applied[0] == 33, "the unsent up stays applied");
}

static void check_mode(void)
{
    struct engine_output out;
    struct engine_report release;
    uint16_t space = 0x20;
    int rebind = 0;
    int sent = -1;

    engine_output_init(&out);
    engine_output_publish(&out, &space, 1, NULL, 0, 0, 0, 0, 0, 0, 0, 96, &release);
    engine_output_remember_mouse(&out, 8, 9);
    expect(engine_output_set_mode(&out, ENGINE_OUTPUT_RP2040, &release, &rebind, &sent) == 1,
           "mode changes");
    expect(sent == ENGINE_OUTPUT_SOFTWARE, "the release uses the old mode");
    expect(out.mode == ENGINE_OUTPUT_RP2040, "the stored mode is the new one");
    expect(rebind == 1, "a mode change rebinds");
    expect(out.mouse_ok == 0, "the point is forgotten");
    expect(out.applied_count == 0, "applied is cleared");
    expect(engine_report_up_count(&release) == 1 && engine_report_up(&release, 0) == 0x20, "space is released");

    expect(engine_output_set_mode(&out, ENGINE_OUTPUT_RP2040, &release, &rebind, &sent) == 0,
           "the same mode does nothing");
    expect(rebind == 0 && engine_report_active(&release) == 0, "no second release");

    expect(engine_output_set_mode(&out, 9, &release, &rebind, &sent) == 1, "an unknown mode is software");
    expect(out.mode == ENGINE_OUTPUT_SOFTWARE && sent == ENGINE_OUTPUT_RP2040, "software replaced the board");
    expect(engine_output_set_mode(&out, 9, &release, &rebind, &sent) == 0, "unknown stays software");

    engine_output_publish(&out, &space, 1, NULL, 0, 4, -3, 0, 1, 10, 20, 96, &release);
    engine_output_note_move(&out, &release);
    expect(out.mouse_ok == 1 && out.mouse_x == 10 && out.mouse_y == 20, "absolute is remembered");
    engine_report_clear(&release);
    engine_report_set_relative(&release, 4, -3);
    engine_output_note_move(&out, &release);
    expect(out.mouse_x == 14 && out.mouse_y == 17, "a stored point is nudged");
}

int main(void)
{
    check_vectors();
    check_image_rules();
    check_send_plan();
    check_board_plan();
    check_diff();
    check_mode();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}