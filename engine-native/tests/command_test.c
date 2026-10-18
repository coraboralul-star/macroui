/* Frames, the command ring, trigger edges, and the state image.
   Does not arm profiles/default.json and does not call SendInput. */
#include "command.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "fail %s\n", name);
        fails++;
    }
}

static void check_frames(void)
{
    static const unsigned char start[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01
    };
    static const unsigned char stop[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02
    };
    static const unsigned char reload[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03
    };
    static const unsigned char quit[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04
    };
    static const unsigned char unknown[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
    };
    static const unsigned char software[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
    };
    static const unsigned char board[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01
    };
    static const unsigned char empty_doc[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x06, 0x00, 0x3D, 0x00, 0x00, 0x00,
        0x39, 0x00, 0x00, 0x00, 0x7B, 0x22, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F,
        0x6E, 0x22, 0x3A, 0x31, 0x2C, 0x22, 0x6E, 0x61, 0x6D, 0x65, 0x22, 0x3A,
        0x22, 0x44, 0x65, 0x66, 0x61, 0x75, 0x6C, 0x74, 0x22, 0x2C, 0x22, 0x76,
        0x61, 0x72, 0x69, 0x61, 0x62, 0x6C, 0x65, 0x73, 0x22, 0x3A, 0x7B, 0x7D,
        0x2C, 0x22, 0x6D, 0x61, 0x63, 0x72, 0x6F, 0x73, 0x22, 0x3A, 0x5B, 0x5D, 0x7D
    };
    static const unsigned char not_object[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x06, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x5B, 0x5D
    };
    static const unsigned char json[] = { '{', '"', 'v', '"' };
    unsigned char bad_version[12] = {
        0x4D, 0x55, 0x49, 0x31, 0x02, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    struct engine_pipe_frame frame;
    struct engine_profile_slot slot;
    const struct engine_profile_image *image;

    expect(engine_frame_read(start, (int)sizeof start, &frame) == 0 && frame.kind == ENGINE_FRAME_START,
           "start arms");
    expect(engine_frame_read(stop, (int)sizeof stop, &frame) == 0 && frame.kind == ENGINE_FRAME_STOP,
           "stop disarms");
    expect(engine_frame_read(reload, (int)sizeof reload, &frame) == 0 &&
               frame.kind == ENGINE_FRAME_RELOAD,
           "reload");
    expect(engine_frame_read(quit, (int)sizeof quit, &frame) == 0 && frame.kind == ENGINE_FRAME_QUIT,
           "quit");
    expect(engine_frame_read(unknown, (int)sizeof unknown, &frame) == 0 &&
               frame.kind == ENGINE_FRAME_COMMAND,
           "unknown command still acks");
    expect(engine_frame_read(software, (int)sizeof software, &frame) == 0 &&
               frame.kind == ENGINE_FRAME_OUTPUT && frame.output_mode == 0,
           "output software");
    expect(engine_frame_read(board, (int)sizeof board, &frame) == 0 && frame.output_mode == 1,
           "output rp2040");
    expect(engine_frame_read(json, (int)sizeof json, &frame) == 0 && frame.kind == ENGINE_FRAME_JSON,
           "json is a bad frame for the native engine");
    expect(engine_frame_read(bad_version, 12, &frame) == 0 && frame.kind == ENGINE_FRAME_BAD_VERSION,
           "bad version");
    expect(engine_frame_read(start, 12, &frame) == 0 && frame.kind == ENGINE_FRAME_BAD, "short command");
    expect(engine_frame_read(empty_doc, (int)sizeof empty_doc, &frame) == 0 &&
               frame.kind == ENGINE_FRAME_PROFILE && frame.profile_len == 0x39,
           "profile document");
    expect(engine_frame_read(not_object, (int)sizeof not_object, &frame) == 0 &&
               frame.kind == ENGINE_FRAME_PROFILE,
           "a non-object is still a profile frame");

    engine_profile_slot_init(&slot);
    expect(engine_profile_slot_apply_document(&slot, (const char *)not_object + 16, 2) == 0,
           "a JSON array applies nothing");
    expect(engine_profile_slot_adopt(&slot) == NULL, "the array does not publish");
    expect(engine_profile_slot_apply_document(&slot, (const char *)frame.profile, frame.profile_len) == 0,
           "the array frame applies nothing");
    expect(engine_frame_read(empty_doc, (int)sizeof empty_doc, &frame) == 0, "empty profile again");
    expect(engine_profile_slot_apply_document(&slot, (const char *)frame.profile, frame.profile_len) == 1,
           "an object profile publishes");
    image = engine_profile_slot_adopt(&slot);
    expect(image && image->sequence == 1, "the object is the first image");
    engine_profile_slot_shutdown(&slot);
}

static void check_ring(void)
{
    struct engine_cmd_ring ring;
    struct engine_cmd_item item;
    int i;

    engine_cmd_ring_init(&ring);
    for (i = 0; i < (int)ENGINE_CMD_CAP; i++)
        expect(engine_cmd_push(&ring, ENGINE_CMD_START, 0) == 1, "ring accepts 64");
    expect(engine_cmd_push(&ring, ENGINE_CMD_STOP, 0) == 0, "the 65th command overflows");
    expect(engine_cmd_take_overflow(&ring) == 1, "overflow latches");
    expect(engine_cmd_take_overflow(&ring) == 0, "overflow is one shot");
    expect(engine_cmd_pop(&ring, &item) == 1 && item.kind == ENGINE_CMD_START, "order kept");
    for (i = 1; i < (int)ENGINE_CMD_CAP; i++)
        engine_cmd_pop(&ring, &item);
    expect(engine_cmd_pop(&ring, &item) == 0, "the missed stop was not stored");
    expect(engine_cmd_push(&ring, ENGINE_CMD_OUTPUT, 2) == 1, "output fits again");
    expect(engine_cmd_pop(&ring, &item) == 1 && item.kind == ENGINE_CMD_OUTPUT && item.mode == 2,
           "output mode stays with the command");
}

static void check_actions(void)
{
    expect(engine_trigger_action(ENGINE_MODE_ONCE, 1, 0, 0) == ENGINE_TRIGGER_START, "once starts");
    expect(engine_trigger_action(ENGINE_MODE_ONCE, 0, 1, 1) == ENGINE_TRIGGER_NONE, "once ignores the up");
    expect(engine_trigger_action(ENGINE_MODE_WHILE_HELD, 1, 0, 0) == ENGINE_TRIGGER_START,
           "while held starts");
    expect(engine_trigger_action(ENGINE_MODE_WHILE_HELD, 0, 1, 1) == ENGINE_TRIGGER_STOP,
           "while held stops");
    expect(engine_trigger_action(ENGINE_MODE_TOGGLE, 1, 0, 0) == ENGINE_TRIGGER_START, "toggle starts");
    expect(engine_trigger_action(ENGINE_MODE_TOGGLE, 1, 0, 1) == ENGINE_TRIGGER_STOP, "toggle stops");
    expect(engine_trigger_action(ENGINE_MODE_ON_RELEASE, 0, 1, 0) == ENGINE_TRIGGER_START,
           "on release starts");
    expect(engine_trigger_action(ENGINE_MODE_REPEAT, 1, 1, 0) == ENGINE_TRIGGER_NONE, "a held key is not an edge");
}

static void check_poll(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_macro macro;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;

    if (!play) {
        expect(0, "play alloc");
        return;
    }
    engine_play_init(play);
    engine_macro_init(&macro);
    memset(&extra, 0, sizeof extra);
    memset(&live, 0, sizeof live);
    memset(&fingers, 0, sizeof fingers);
    snprintf(macro.id, sizeof macro.id, "shot");
    macro.mode = ENGINE_MODE_ONCE;
    macro.trigger = 0x41;
    extra.enabled = 1;
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;
    engine_trigger_init(&mem);
    engine_fingers_set(&fingers, 0x41, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 0, "a key held at arm does not start");
    engine_fingers_set(&fingers, 0x41, 0);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    engine_fingers_set(&fingers, 0x41, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 1, "once starts on the down edge");
    engine_poll_triggers(play, &live, &mem, &fingers, "MacroShell.exe", "");
    expect(play->pending_count == 0, "the editor focus stops it");

    engine_play_init(play);
    macro.mode = ENGINE_MODE_WHILE_HELD;
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    engine_fingers_set(&fingers, 0x41, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 0, "seed eats the first held edge");
    engine_fingers_set(&fingers, 0x41, 0);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    engine_fingers_set(&fingers, 0x41, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 1, "while held starts on the next press");
    engine_play_flush(play);
    engine_fingers_set(&fingers, 0x41, 0);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->runners[0].done == 1, "while held stops when the finger comes up");
    free(play);
}

static void check_state(void)
{
    static const unsigned char sample[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x02, 0x00, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x01, 0x00, 0x07, 0x00, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6C, 0x74, 0x08,
        0x00, 0x47, 0x61, 0x6D, 0x65, 0x2E, 0x65, 0x78, 0x65, 0x0B, 0x00, 0x73, 0x70, 0x72, 0x69,
        0x6E, 0x74, 0x2D, 0x6A, 0x75, 0x6D, 0x70, 0x0B, 0x00, 0x53, 0x70, 0x72, 0x69, 0x6E, 0x74,
        0x20, 0x6A, 0x75, 0x6D, 0x70, 0x05, 0x00, 0x73, 0x70, 0x61, 0x63, 0x65, 0x08, 0x00, 0x47,
        0x61, 0x6D, 0x65, 0x2E, 0x65, 0x78, 0x65, 0x04, 0x00, 0x47, 0x61, 0x6D, 0x65
    };
    struct engine_state_snapshot snap;
    struct engine_state_window window;
    unsigned char buf[256];
    int n;

    memset(&snap, 0, sizeof snap);
    memset(&window, 0, sizeof window);
    snap.armed = 1;
    snprintf(snap.active_id, sizeof snap.active_id, "default");
    snprintf(snap.front, sizeof snap.front, "Game.exe");
    snap.running_count = 1;
    snprintf(snap.running[0].id, sizeof snap.running[0].id, "sprint-jump");
    snprintf(snap.running[0].name, sizeof snap.running[0].name, "Sprint jump");
    snap.held_count = 1;
    snap.held[0] = 0x20;
    snprintf(window.exe, sizeof window.exe, "Game.exe");
    snprintf(window.title, sizeof window.title, "Game");
    n = engine_state_frame(&snap, &window, 1, buf, (int)sizeof buf);
    expect(n == (int)sizeof sample && memcmp(buf, sample, sizeof sample) == 0, "state sample");
}

int main(void)
{
    check_frames();
    check_ring();
    check_actions();
    check_poll();
    check_state();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
