#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

/* Frames, the command ring, trigger edges, and the state image.
   Does not arm profiles/default.json and does not call SendInput. */
#include "command.h"
#include "output.h"

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

static void press_key(struct engine_hook_state *st, uint32_t vk, int64_t now,
                      struct engine_hook_decision *out)
{
    struct engine_hook_sample sample;

    memset(&sample, 0, sizeof sample);
    sample.kind = ENGINE_HOOK_KEY;
    sample.msg = ENGINE_MSG_KEYDOWN;
    sample.vk = vk;
    sample.now = now;
    engine_hook_classify(st, &sample, out);
}

static void check_swallow(void)
{
    struct engine_hook_state hooks;
    struct engine_macro macro;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_hook_decision decision;
    struct engine_hook_sample sample;

    engine_hook_init(&hooks);
    engine_macro_init(&macro);
    memset(&extra, 0, sizeof extra);
    memset(&live, 0, sizeof live);
    snprintf(macro.id, sizeof macro.id, "w");
    macro.trigger = 0x57;
    macro.step_count = 1;
    extra.enabled = 1;
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;

    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, NULL);
    press_key(&hooks, 0x57, 10, &decision);
    expect(decision.swallow == 1 && decision.enqueued == 1,
           "an armed trigger is blocked in notepad");
    press_key(&hooks, 0x47, 11, &decision);
    expect(decision.swallow == 0 && decision.enqueued == 1,
           "a key that is not the trigger still reaches the app");

    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 0, NULL);
    press_key(&hooks, 0x57, 12, &decision);
    expect(decision.swallow == 0, "a disarmed trigger is not blocked");

    engine_arm_swallow(&hooks, &live, "MacroShell.exe", NULL, 1, NULL);
    press_key(&hooks, 0x57, 13, &decision);
    expect(decision.swallow == 0, "the editor does not block the trigger");

    snprintf(live.focus_exe, sizeof live.focus_exe, "Game.exe");
    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, NULL);
    press_key(&hooks, 0x57, 14, &decision);
    expect(decision.swallow == 0, "an unmatched focus does not block the trigger");

    live.focus_exe[0] = '\0';
    macro.step_count = 0;
    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, NULL);
    press_key(&hooks, 0x57, 15, &decision);
    expect(decision.swallow == 0, "a trigger with no steps is not blocked");

    macro.step_count = 1;
    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, NULL);
    expect(engine_echo_push(&hooks, 0x57, 1, 1000) == 1, "board echo stored");
    memset(&sample, 0, sizeof sample);
    sample.kind = ENGINE_HOOK_KEY;
    sample.msg = ENGINE_MSG_KEYDOWN;
    sample.vk = 0x57;
    sample.now = 20;
    engine_hook_classify(&hooks, &sample, &decision);
    expect(decision.swallow == 0 && decision.enqueued == 0,
           "a board echo of the trigger key passes and is not a finger");
}

static void check_saved_profile(void)
{
    static const char *first =
        "{\"version\":1,\"name\":\"Default\",\"variables\":{},\"macros\":["
        "{\"id\":\"w\",\"name\":\"W\",\"enabled\":true,\"playMode\":\"whileHeld\","
        "\"trigger\":{\"kind\":\"key\",\"button\":\"w\"},"
        "\"steps\":[{\"type\":\"key\",\"action\":\"down\",\"key\":\"g\"}]}]}";
    static const char *saved =
        "{\"version\":1,\"name\":\"Default\",\"variables\":{},\"macros\":["
        "{\"id\":\"w\",\"name\":\"W\",\"enabled\":true,\"playMode\":\"whileHeld\","
        "\"trigger\":{\"kind\":\"key\",\"button\":\"w\"},"
        "\"steps\":[{\"type\":\"key\",\"action\":\"down\",\"key\":\"g\"}]},"
        "{\"id\":\"r\",\"name\":\"R\",\"enabled\":true,\"playMode\":\"once\","
        "\"trigger\":{\"kind\":\"key\",\"button\":\"r\"},"
        "\"steps\":[{\"type\":\"key\",\"action\":\"down\",\"key\":\"a\"}]}]}";
    struct engine_profile_slot slot;
    struct engine_live_config live;
    struct engine_hook_state hooks;
    struct engine_hook_decision decision;
    const struct engine_profile_image *image;
    int saw_r = 0;
    int i;

    engine_profile_slot_init(&slot);
    expect(engine_profile_slot_apply_document(&slot, first, strlen(first)) == 1,
           "the first profile publishes");
    image = engine_profile_slot_adopt(&slot);
    engine_profile_pick(image, "notepad.exe", "", &live);
    expect(live.macro_count == 1 && live.macros && live.macros[0].trigger == 0x57,
           "playback adopts the trigger on w");

    expect(engine_profile_slot_apply_document(&slot, saved, strlen(saved)) == 1,
           "a saved profile publishes");
    image = engine_profile_slot_adopt(&slot);
    expect(image && image->sequence == 2, "the save is the next image");
    engine_profile_pick(image, "notepad.exe", image->active_id, &live);
    expect(live.macro_count == 2, "playback adopts the saved macro");
    for (i = 0; i < live.macro_count; i++) {
        if (live.macros[i].trigger == 0x52)
            saw_r = 1;
    }
    expect(saw_r == 1, "the saved macro's trigger is live");

    engine_hook_init(&hooks);
    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, NULL);
    press_key(&hooks, 0x52, 30, &decision);
    expect(decision.swallow == 1, "the saved trigger is blocked as soon as it is adopted");
    engine_profile_slot_shutdown(&slot);
}

static void check_once_lock(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_macro macros[2];
    struct engine_profile_extra extras[2];
    struct engine_step step;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    struct engine_hook_state hooks;
    struct engine_hook_decision decision;
    const char *shot = "shot";
    const char *other = "other";
    const char *ids[2];
    const char *missing = "nope";

    if (!play) {
        expect(0, "once play alloc");
        return;
    }
    memset(&step, 0, sizeof step);
    step.type = ENGINE_STEP_WAIT;
    step.a = 50;
    memset(macros, 0, sizeof macros);
    memset(extras, 0, sizeof extras);
    memset(&live, 0, sizeof live);
    engine_play_init(play);
    engine_macro_init(&macros[0]);
    engine_macro_init(&macros[1]);
    snprintf(macros[0].id, sizeof macros[0].id, "shot");
    snprintf(macros[1].id, sizeof macros[1].id, "other");
    macros[0].mode = ENGINE_MODE_REPEAT;
    macros[0].repeat_count = 4;
    macros[0].trigger = 0x41;
    macros[0].steps = &step;
    macros[0].step_count = 1;
    macros[0].top_count = 1;
    macros[1].mode = ENGINE_MODE_ONCE;
    macros[1].trigger = 0x41;
    macros[1].steps = &step;
    macros[1].step_count = 1;
    macros[1].top_count = 1;
    extras[0].enabled = 1;
    extras[1].enabled = 1;
    live.macros = macros;
    live.extras = extras;
    live.macro_count = 1;

    engine_hook_init(&hooks);
    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, play);
    press_key(&hooks, 0x41, 1, &decision);
    expect(decision.swallow == 1, "the trigger is blocked before a lock");

    engine_fingers_clear(&fingers);
    engine_fingers_set(&fingers, 0x41, 0);
    ids[0] = shot;
    engine_once_arm(play, &live, ids, 1, &fingers);
    expect(engine_once_state(play, "shot") == ENGINE_LOCK_OPEN, "a raised key leaves the lock open");
    expect(engine_once_passing(play, 0x41) == 1, "an open lock passes the trigger");
    expect(play->pending_count == 0, "an open lock does not start on a raised key");
    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, play);
    press_key(&hooks, 0x41, 2, &decision);
    expect(decision.swallow == 0 && decision.enqueued == 1, "a locked trigger reaches the app");

    engine_trigger_init(&mem);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    engine_fingers_set(&fingers, 0x41, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 1, "an open lock starts on the down edge");
    engine_play_flush(play);
    expect(play->runners[0].mode == ENGINE_MODE_ONCE, "the lock runs a repeat as once");
    expect(engine_once_state(play, "shot") == ENGINE_LOCK_DONE, "the start spends the lock");
    expect(engine_once_passing(play, 0x41) == 1, "a spent lock still passes the trigger");
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 0 && play->runner_count == 1, "a done lock does not start again");

    ids[0] = shot;
    engine_once_release(play, ids, 1);
    expect(engine_once_state(play, "shot") == ENGINE_LOCK_NONE, "release clears the lock");
    expect(engine_once_passing(play, 0x41) == 0, "release drops the pass");
    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, play);
    press_key(&hooks, 0x41, 3, &decision);
    expect(decision.swallow == 1, "the trigger is blocked again after release");

    engine_play_init(play);
    engine_fingers_clear(&fingers);
    engine_fingers_set(&fingers, 0x41, 1);
    ids[0] = shot;
    engine_once_arm(play, &live, ids, 1, &fingers);
    expect(play->pending_count == 1 && engine_once_state(play, "shot") == ENGINE_LOCK_DONE,
           "a held key starts once and the lock is done");
    engine_trigger_init(&mem);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 1, "a done lock does not queue a second runner");

    engine_play_init(play);
    macros[0].mode = ENGINE_MODE_WHILE_HELD;
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    engine_fingers_set(&fingers, 0x41, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    engine_fingers_set(&fingers, 0x41, 0);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    engine_fingers_set(&fingers, 0x41, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    engine_play_flush(play);
    expect(play->runner_count == 1 && play->runners[0].done == 0, "while held is running before the lock");
    ids[0] = shot;
    engine_once_arm(play, &live, ids, 1, &fingers);
    expect(engine_once_state(play, "shot") == ENGINE_LOCK_DONE && play->runner_count == 1,
           "a live runner is marked done without a second start");
    engine_fingers_set(&fingers, 0x41, 0);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->runners[0].stopping == 1, "a done while-held lock asks the runner to stop");

    engine_play_init(play);
    live.macro_count = 2;
    engine_fingers_clear(&fingers);
    ids[0] = shot;
    ids[1] = other;
    engine_once_arm(play, &live, ids, 2, &fingers);
    expect(engine_once_passing(play, 0x41) == 1, "two locks on one key still pass it once");
    engine_once_release(play, ids, 1);
    expect(engine_once_passing(play, 0x41) == 1, "one remaining lock keeps the pass");
    engine_once_release(play, &other, 1);
    expect(engine_once_passing(play, 0x41) == 0, "the last lock drops the pass");

    engine_play_init(play);
    engine_once_arm(play, &live, &missing, 1, &fingers);
    expect(engine_once_state(play, "nope") == ENGINE_LOCK_OPEN, "a missing macro can still be locked");
    expect(engine_once_passing(play, 0x41) == 0, "a missing macro does not pass a key");
    free(play);
}

static void press_button(struct engine_hook_state *st, uint16_t id, int down, uint32_t flags,
                         uint64_t extra, int64_t now, struct engine_hook_decision *out)
{
    struct engine_hook_sample sample;

    memset(&sample, 0, sizeof sample);
    sample.kind = ENGINE_HOOK_MOUSE;
    sample.now = now;
    sample.flags = flags;
    sample.extra = extra;
    if (id == ENGINE_KEY_LBUTTON)
        sample.msg = down ? ENGINE_MSG_LBUTTONDOWN : ENGINE_MSG_LBUTTONUP;
    else if (id == ENGINE_KEY_RBUTTON)
        sample.msg = down ? ENGINE_MSG_RBUTTONDOWN : ENGINE_MSG_RBUTTONUP;
    else if (id == ENGINE_KEY_MBUTTON)
        sample.msg = down ? ENGINE_MSG_MBUTTONDOWN : ENGINE_MSG_MBUTTONUP;
    else if (id == ENGINE_KEY_XBUTTON1 || id == ENGINE_KEY_XBUTTON2) {
        sample.msg = down ? ENGINE_MSG_XBUTTONDOWN : ENGINE_MSG_XBUTTONUP;
        sample.mouse_data = id == ENGINE_KEY_XBUTTON1 ? (1u << 16) : (2u << 16);
    }
    engine_hook_classify(st, &sample, out);
}

static void check_mouse_triggers(void)
{
    static const uint16_t triggers[] = {
        ENGINE_KEY_MBUTTON, ENGINE_KEY_XBUTTON1, ENGINE_KEY_XBUTTON2
    };
    static const char *trigger_names[] = { "middle", "mouse 4", "mouse 5" };
    static const uint16_t passed[] = { ENGINE_KEY_LBUTTON, ENGINE_KEY_RBUTTON };
    static const char *passed_names[] = { "left", "right" };
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_macro macro;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    struct engine_hook_state hooks;
    struct engine_hook_decision decision;
    struct engine_finger ev;
    char label[96];
    int i;

    if (!play) {
        expect(0, "mouse play alloc");
        return;
    }
    engine_play_init(play);
    engine_macro_init(&macro);
    memset(&extra, 0, sizeof extra);
    memset(&live, 0, sizeof live);
    snprintf(macro.id, sizeof macro.id, "click");
    macro.mode = ENGINE_MODE_WHILE_HELD;
    macro.step_count = 1;
    extra.enabled = 1;
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;

    for (i = 0; i < 3; i++) {
        engine_play_init(play);
        engine_trigger_init(&mem);
        engine_fingers_clear(&fingers);
        macro.trigger = triggers[i];
        engine_fingers_set(&fingers, triggers[i], 1);
        engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
        snprintf(label, sizeof label, "%s held at arm does not start", trigger_names[i]);
        expect(play->pending_count == 0, label);
        engine_fingers_set(&fingers, triggers[i], 0);
        engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
        engine_fingers_set(&fingers, triggers[i], 1);
        engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
        snprintf(label, sizeof label, "%s while held starts on the down edge", trigger_names[i]);
        expect(play->pending_count == 1, label);
        engine_play_flush(play);
        engine_fingers_set(&fingers, triggers[i], 0);
        engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
        snprintf(label, sizeof label, "%s while held stops when the finger comes up", trigger_names[i]);
        expect(play->runners[0].done == 1, label);
    }

    for (i = 0; i < 2; i++) {
        const char *id = "click";

        engine_play_init(play);
        engine_trigger_init(&mem);
        engine_fingers_clear(&fingers);
        macro.trigger = passed[i];
        engine_fingers_set(&fingers, passed[i], 0);
        engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
        engine_fingers_set(&fingers, passed[i], 1);
        engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
        snprintf(label, sizeof label, "%s never starts a macro", passed_names[i]);
        expect(play->pending_count == 0 && play->runner_count == 0, label);
        engine_once_arm(play, &live, &id, 1, &fingers);
        snprintf(label, sizeof label, "a held %s does not arm a play", passed_names[i]);
        expect(play->pending_count == 0, label);
    }

    engine_hook_init(&hooks);
    play->output.mode = ENGINE_OUTPUT_SOFTWARE;
    for (i = 0; i < 3; i++) {
        macro.trigger = triggers[i];
        engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, play);
        press_button(&hooks, triggers[i], 1, 0, 0, 40 + i, &decision);
        snprintf(label, sizeof label, "software swallows %s", trigger_names[i]);
        expect(decision.swallow == 1 && decision.enqueued == 1, label);
        expect(engine_event_pop(&hooks, &ev) == 1 && ev.id == triggers[i],
               "the swallowed click is still a finger");
    }
    for (i = 0; i < 2; i++) {
        macro.trigger = passed[i];
        engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, play);
        press_button(&hooks, passed[i], 1, 0, 0, 46 + i, &decision);
        snprintf(label, sizeof label, "software passes %s", passed_names[i]);
        expect(decision.swallow == 0 && decision.enqueued == 1, label);
        engine_event_pop(&hooks, &ev);
    }

    /* Like a keyboard trigger on a board. The game must not see the finger's
       click. The board's own click of that button matches its echo and passes. */
    play->output.mode = ENGINE_OUTPUT_RP2350;
    for (i = 0; i < 3; i++) {
        macro.trigger = triggers[i];
        engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, play);
        press_button(&hooks, triggers[i], 1, 0, 0, 50 + i, &decision);
        snprintf(label, sizeof label, "a board swallows %s", trigger_names[i]);
        expect(decision.swallow == 1 && decision.enqueued == 1, label);
        expect(engine_event_pop(&hooks, &ev) == 1 && ev.swallowed == 1,
               "the event says it was swallowed");
        press_button(&hooks, triggers[i], 0, 0, 0, 53 + i, &decision);
        snprintf(label, sizeof label, "a board swallows the %s up too", trigger_names[i]);
        expect(decision.swallow == 1, label);
        engine_event_pop(&hooks, &ev);
        expect(engine_echo_push(&hooks, triggers[i], 1, 1000) == 1, "board click echo stored");
        press_button(&hooks, triggers[i], 1, 0, 0, 56 + i, &decision);
        snprintf(label, sizeof label, "the board's own %s passes and is not a finger",
                 trigger_names[i]);
        expect(decision.swallow == 0 && decision.enqueued == 0, label);
    }
    macro.trigger = ENGINE_KEY_XBUTTON1;
    engine_arm_swallow(&hooks, &live, "MacroShell.exe", NULL, 1, play);
    press_button(&hooks, ENGINE_KEY_XBUTTON1, 1, 0, 0, 60, &decision);
    expect(decision.swallow == 0, "a board passes mouse 4 while the editor is in front");
    engine_event_pop(&hooks, &ev);
    press_button(&hooks, ENGINE_KEY_XBUTTON1, 0, 0, 0, 61, &decision);
    engine_event_pop(&hooks, &ev);

    engine_play_init(play);
    play->output.mode = ENGINE_OUTPUT_SOFTWARE;
    macro.trigger = ENGINE_KEY_XBUTTON1;
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    engine_hook_init(&hooks);
    press_button(&hooks, ENGINE_KEY_XBUTTON1, 1, 0, 0, 70, &decision);
    expect(engine_event_pop(&hooks, &ev) == 1 && ev.down == 1, "mouse 4 down is a finger");
    engine_fingers_set(&fingers, ev.id, ev.down);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    press_button(&hooks, ENGINE_KEY_XBUTTON1, 0, 0, 0, 71, &decision);
    engine_event_pop(&hooks, &ev);
    engine_fingers_set(&fingers, ev.id, ev.down);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    press_button(&hooks, ENGINE_KEY_XBUTTON1, 1, 0, 0, 72, &decision);
    engine_event_pop(&hooks, &ev);
    engine_fingers_set(&fingers, ev.id, ev.down);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 1, "mouse 4 while held starts from the hook");
    engine_play_flush(play);
    press_button(&hooks, ENGINE_KEY_XBUTTON1, 0, ENGINE_MOUSE_INJECTED, 0, 73, &decision);
    expect(decision.enqueued == 0, "an injected mouse 4 release is not a finger");
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->runners[0].done == 0, "a sent click does not end the hold");
    press_button(&hooks, ENGINE_KEY_XBUTTON1, 0, 0, ENGINE_EXTRA_ECHO, 74, &decision);
    expect(decision.enqueued == 0, "an echoed mouse 4 release is not a finger");
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->runners[0].done == 0, "the engine's own click does not end the hold");
    press_button(&hooks, ENGINE_KEY_XBUTTON1, 0, 0, 0, 75, &decision);
    expect(engine_event_pop(&hooks, &ev) == 1 && ev.down == 0, "a real mouse 4 release is a finger");
    engine_fingers_set(&fingers, ev.id, ev.down);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->runners[0].done == 1, "the real mouse 4 release ends the hold");

    engine_play_init(play);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    engine_hook_init(&hooks);
    macro.trigger = ENGINE_KEY_XBUTTON2;
    macro.step_count = 1;
    extra.enabled = 1;
    {
        struct engine_hook_sample sample;

        memset(&sample, 0, sizeof sample);
        sample.kind = ENGINE_HOOK_MOUSE;
        sample.msg = ENGINE_MSG_XBUTTONDOWN;
        sample.mouse_data = 2u;
        sample.now = 90;
        engine_hook_classify(&hooks, &sample, &decision);
    }
    expect(engine_event_pop(&hooks, &ev) == 1 && ev.id == ENGINE_KEY_XBUTTON2 && ev.down == 1,
           "low word mouse 5 is a finger");
    engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON2, 0);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON2, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 1, "mouse 5 while held starts like mouse 4");
    engine_play_flush(play);

    engine_play_init(play);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    macro.trigger = 0;
    macro.step_count = 4;
    engine_fingers_set(&fingers, (uint16_t)'e', 1);
    engine_fingers_set(&fingers, (uint16_t)'r', 1);
    engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON1, 1);
    engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON2, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "notepad.exe", "");
    expect(play->pending_count == 0 && play->runner_count == 0, "an empty trigger starts nothing");
    engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, play);
    press_button(&hooks, ENGINE_KEY_XBUTTON2, 1, 0, 0, 91, &decision);
    expect(decision.swallow == 0, "an empty trigger does not swallow mouse 5");
    engine_event_pop(&hooks, &ev);
    free(play);
}

static void check_recover(void)
{
    struct engine_macro macro;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_fingers fingers;
    uint16_t ids[4];
    int n;

    memset(&macro, 0, sizeof macro);
    memset(&extra, 0, sizeof extra);
    memset(&live, 0, sizeof live);
    engine_macro_init(&macro);
    snprintf(macro.id, sizeof macro.id, "ability");
    macro.trigger = 0x45;
    macro.step_count = 1;
    extra.enabled = 1;
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;
    expect(!engine_live_needs_mouse(&live), "a key trigger does not need the mouse hook");
    macro.trigger = ENGINE_KEY_LBUTTON;
    expect(!engine_live_needs_mouse(&live), "left click does not need the mouse hook");
    macro.trigger = ENGINE_KEY_RBUTTON;
    expect(!engine_live_needs_mouse(&live), "right click does not need the mouse hook");
    macro.trigger = ENGINE_KEY_XBUTTON1;
    expect(engine_live_needs_mouse(&live) == 1, "mouse 4 needs the mouse hook");
    macro.trigger = ENGINE_KEY_XBUTTON2;
    expect(engine_live_needs_mouse(&live) == 1, "mouse 5 needs the mouse hook");
    macro.trigger = ENGINE_KEY_MBUTTON;
    expect(engine_live_needs_mouse(&live) == 1, "middle needs the mouse hook");
    extra.enabled = 0;
    expect(!engine_live_needs_mouse(&live), "a disabled side button does not need the mouse hook");
    extra.enabled = 1;
    macro.step_count = 0;
    expect(!engine_live_needs_mouse(&live), "a side button with no steps does not need the mouse hook");
    macro.trigger = 0;
    macro.step_count = 4;
    expect(!engine_can_trigger(0), "an empty trigger is not a key");
    expect(!engine_live_needs_mouse(&live), "an empty trigger does not need the mouse hook");

    {
        struct engine_fingers up;

        engine_fingers_clear(&fingers);
        engine_fingers_clear(&up);
        engine_fingers_set(&fingers, 0x45, 1);
        engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON1, 1);
        engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON2, 1);
        n = engine_fingers_release_os_up(&fingers, up.down, NULL, ids, 4);
        expect(n == 0 && engine_fingers_down(&fingers, 0x45) &&
                   engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON1) &&
                   engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
               "no timer releases a held key or side button");
        engine_fingers_set(&up, 0x45, 1);
        n = engine_fingers_release_os_up(&fingers, up.down, NULL, ids, 4);
        expect(n == 1 && ids[0] == 0x45 && !engine_fingers_down(&fingers, 0x45) &&
                   engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON1) &&
                   engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
               "os up releases only the key that is up");
        engine_fingers_clear(&up);
        engine_fingers_set(&up, ENGINE_KEY_XBUTTON2, 1);
        n = engine_fingers_release_os_up(&fingers, up.down, NULL, ids, 4);
        expect(n == 1 && ids[0] == ENGINE_KEY_XBUTTON2 &&
                   !engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2) &&
                   engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON1),
               "os up releases mouse 5 and leaves mouse 4");
    }
    n = engine_fingers_release_down(&fingers, 0, 1, ids, 4);
    expect(n == 1 && ids[0] == ENGINE_KEY_XBUTTON1 && !engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON1),
           "a hook reinstall releases the mouse button");

    engine_fingers_set(&fingers, 0x45, 1);
    engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON2, 1);
    n = engine_fingers_release_down(&fingers, 1, 0, ids, 4);
    expect(n == 1 && ids[0] == 0x45 && engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
           "a key reinstall leaves the mouse button down");

    {
        struct engine_fingers up;
        struct engine_fingers blocked;

        engine_fingers_clear(&fingers);
        engine_fingers_clear(&up);
        engine_fingers_clear(&blocked);
        engine_fingers_set(&fingers, (uint16_t)'r', 1);
        engine_fingers_set(&up, (uint16_t)'r', 1);
        engine_fingers_set(&blocked, (uint16_t)'r', 1);
        n = engine_fingers_release_os_up(&fingers, up.down, blocked.down, ids, 4);
        expect(n == 0 && engine_fingers_down(&fingers, (uint16_t)'r'),
               "a swallowed trigger stays held when async says up");
        engine_fingers_clear(&blocked);
        n = engine_fingers_release_os_up(&fingers, up.down, blocked.down, ids, 4);
        expect(n == 1 && !engine_fingers_down(&fingers, (uint16_t)'r'),
               "the same key is released when it is not swallowed");

        engine_fingers_clear(&fingers);
        engine_fingers_set(&fingers, ENGINE_KEY_LBUTTON, 1);
        expect(engine_finger_note(&fingers, ENGINE_KEY_LBUTTON, 1, NULL) == 1 &&
                   engine_fingers_down(&fingers, ENGINE_KEY_LBUTTON),
               "a second left click keeps it down");
        expect(engine_finger_note(&fingers, ENGINE_KEY_LBUTTON, 0, NULL) == 0 &&
                   !engine_fingers_down(&fingers, ENGINE_KEY_LBUTTON),
               "the left click up clears a stuck left button");

        expect(engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, NULL) == 0 &&
                   engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
               "the first mouse 5 down is a press");
        expect(engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, NULL) == 1 &&
                   engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
               "a second mouse 5 down is a repeat and stays down");
        expect(engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 0, NULL) == 0 &&
                   !engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
               "the mouse 5 up releases it");
        engine_fingers_set(&fingers, 0x34, 1);
        expect(engine_finger_note(&fingers, 0x34, 1, NULL) == 1 && engine_fingers_down(&fingers, 0x34),
               "a keyboard repeat is a repeat");
        {
            struct engine_raw_mouse raw;
            struct engine_finger_lost lost;
            uint16_t redo[4];

            engine_raw_mouse_init(&raw);
            engine_raw_mouse_set_ok(&raw, 1);
            memset(&lost, 0, sizeof lost);
            lost.raw = &raw;
            engine_fingers_clear(&fingers);
            engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, &lost);
            expect(engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, &lost) ==
                       ENGINE_FINGER_REPEAT,
                   "mouse 5 held in Raw Input: a second down is a repeat");
            engine_raw_mouse_up(&raw, ENGINE_KEY_XBUTTON2);
            expect(engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, &lost) ==
                           ENGINE_FINGER_LOST &&
                       !engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
                   "mouse 5 up in Raw Input: the down lets go for one tick");
            expect(engine_finger_redo(&fingers, &lost, redo, 4) == 1 &&
                       redo[0] == ENGINE_KEY_XBUTTON2 &&
                       engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
                   "after the tick the lost release is pressed again");
            expect(engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, &lost) ==
                       ENGINE_FINGER_REPEAT,
                   "the redo takes a new Raw Input count");
            engine_raw_mouse_up(&raw, ENGINE_KEY_XBUTTON2);
            engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, &lost);
            engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 0, &lost);
            expect(engine_finger_redo(&fingers, &lost, redo, 4) == 0 &&
                       !engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
                   "an up in the same drain drops the redo");
        }
    }
}

/* AHK PollTriggers starts only on an edge. A runner that ends while the
   key is still down stays ended until the key comes up and goes down. */
static void check_held_again(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_macro macro;
    struct engine_step step;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;

    if (!play) {
        expect(0, "held play alloc");
        return;
    }
    engine_play_init(play);
    engine_macro_init(&macro);
    memset(&step, 0, sizeof step);
    memset(&extra, 0, sizeof extra);
    memset(&live, 0, sizeof live);
    step.type = ENGINE_STEP_HOLD_LOOP;
    extra.enabled = 1;
    snprintf(macro.id, sizeof macro.id, "e-hold");
    macro.mode = ENGINE_MODE_ONCE;
    macro.advanced = 1;
    macro.trigger = 0x45;
    macro.steps = &step;
    macro.step_count = 1;
    macro.top_count = 1;
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;

    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
    engine_fingers_set(&fingers, 0x45, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
    expect(play->pending_count == 1, "e starts on the down edge");
    expect(engine_live_claims(&live, 0x45) == 1, "e is claimed");
    expect(engine_live_claims(&live, (uint16_t)'w') == 0, "w is not claimed");
    engine_play_flush(play);
    play->runners[0].done = 1;
    engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
    expect(play->pending_count == 0, "a hold loop waits for a new down edge, like AHK");

    engine_play_init(play);
    macro.advanced = 0;
    macro.steps = NULL;
    macro.step_count = 0;
    macro.top_count = 0;
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
    engine_fingers_set(&fingers, 0x45, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
    expect(play->pending_count == 1, "a plain once still starts on the edge");
    engine_play_flush(play);
    play->runners[0].done = 1;
    engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
    expect(play->pending_count == 0, "a plain once does not start again while the key stays down");

    engine_play_init(play);
    macro.mode = ENGINE_MODE_WHILE_HELD;
    macro.step_count = 1;
    macro.steps = &step;
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    engine_fingers_set(&fingers, 0x35, 1);
    macro.trigger = 0x35;
    engine_poll_triggers(play, &live, &mem, &fingers, "game.exe", "");
    expect(play->pending_count == 0, "5 held at arm does not start");
    engine_fingers_set(&fingers, 0x35, 0);
    engine_poll_triggers(play, &live, &mem, &fingers, "game.exe", "");
    engine_fingers_set(&fingers, 0x35, 1);
    engine_poll_triggers(play, &live, &mem, &fingers, "game.exe", "");
    engine_play_flush(play);
    play->runners[0].done = 1;
    engine_poll_triggers(play, &live, &mem, &fingers, "game.exe", "");
    expect(play->pending_count == 0, "while held waits for a new down edge, like AHK");
    free(play);
}

/* One hook event for a key or a mouse button, from the hook's struct fields. */
static void hook_event(struct engine_hook_state *st, uint16_t id, int down, int64_t now,
                       struct engine_hook_decision *out)
{
    struct engine_hook_sample sample;

    if (engine_is_mouse_button(id)) {
        press_button(st, id, down, 0, 0, now, out);
        return;
    }
    memset(&sample, 0, sizeof sample);
    sample.kind = ENGINE_HOOK_KEY;
    sample.msg = down ? ENGINE_MSG_KEYDOWN : ENGINE_MSG_KEYUP;
    sample.vk = id;
    sample.flags = (down ? 0u : ENGINE_KEY_UP_FLAG) | (id == 0x5B ? ENGINE_KEY_EXTENDED : 0u);
    sample.now = now;
    engine_hook_classify(st, &sample, out);
}

struct held_run {
    int downs;
    int swallowed;
    int starts;
    int playing;
    int lost_count;
    /* raw NULL is no Raw Input. */
    struct engine_finger_lost lost;
};

/* playback_tick in one millisecond: publish the swallow map, take the hook
   events the way drain_fingers does, poll, tick, press a lost release again,
   then read the notes. */
static void held_ms(struct engine_play *play, const struct engine_live_config *live,
                    struct engine_hook_state *hooks, struct engine_trigger_mem *mem,
                    struct engine_fingers *fingers, int64_t now, struct held_run *run)
{
    struct engine_finger ev;
    int i;

    while (engine_event_pop(hooks, &ev)) {
        if (engine_finger_note(fingers, ev.id, ev.down, &run->lost) == ENGINE_FINGER_LOST)
            run->lost_count++;
    }
    engine_play_set_fingers(play, fingers);
    engine_poll_triggers(play, live, mem, fingers, "helldivers2.exe", "");
    engine_play_tick(play, now);
    engine_finger_redo(fingers, &run->lost, NULL, 0);
    for (i = 0; i < play->note_count; i++) {
        if (play->notes[i].kind == ENGINE_NOTE_START)
            run->starts++;
        if (play->notes[i].kind == ENGINE_NOTE_BLOCK && play->notes[i].why == ENGINE_WHY_PLAYING)
            run->playing++;
    }
    play->note_count = 0;
    engine_arm_swallow(hooks, live, "helldivers2.exe", "", 1, play);
}

/* The HellDivers stratagems: a Run Once block, c down, four arrow taps, c up.
   19 steps, about 270 ms, like Orbital 380mm HE Barrage. */
static void build_stratagem(struct engine_step *steps)
{
    static const uint16_t keys[] = { 0x46, 0x44, 0x45, 0x53 };
    int n = 0;
    int i;

    memset(steps, 0, 19 * sizeof *steps);
    steps[n].type = ENGINE_STEP_KEY;
    steps[n].action = ENGINE_ACT_DOWN;
    steps[n++].key = 0x43;
    for (i = 0; i < 4; i++) {
        steps[n].type = ENGINE_STEP_WAIT;
        steps[n++].a = 30;
        steps[n].type = ENGINE_STEP_KEY;
        steps[n].action = ENGINE_ACT_DOWN;
        steps[n++].key = keys[i];
        steps[n].type = ENGINE_STEP_WAIT;
        steps[n++].a = 30;
        steps[n].type = ENGINE_STEP_KEY;
        steps[n].action = ENGINE_ACT_UP;
        steps[n++].key = keys[i];
    }
    steps[n].type = ENGINE_STEP_WAIT;
    steps[n++].a = 30;
    steps[n].type = ENGINE_STEP_KEY;
    steps[n].action = ENGINE_ACT_UP;
    steps[n++].key = 0x43;
}

/* Run Once on a held trigger: Windows repeats a held key after 500 ms, then
   every 33 ms, with no up. That is never a new edge. Only an up and then a
   down starts the macro again, and not while the last pass is playing. */
static void build_tap_block(struct engine_step *steps, uint16_t key);

/* One ms of playback_tick, from the seed to the owed presses. down is the
   finger for this ms. Counts the notes. */
struct owed_run {
    int starts;
    int held_press;
    int blocked;
    int playing;
    int dead_ms;
};

static void owed_ms(struct engine_play *play, const struct engine_live_config *live,
                    struct engine_trigger_mem *mem, struct engine_fingers *fingers, uint16_t key,
                    int down, const char *front, int64_t now, struct owed_run *run)
{
    int i;

    engine_trigger_seed_due(mem, live, fingers);
    engine_fingers_set(fingers, key, down);
    engine_play_set_fingers(play, fingers);
    engine_poll_triggers(play, live, mem, fingers, front, "");
    engine_play_tick(play, now);
    engine_trigger_owed(play, live, mem, fingers, front, "");
    for (i = 0; i < play->note_count; i++) {
        if (play->notes[i].kind == ENGINE_NOTE_START)
            run->starts++;
        if (play->notes[i].kind == ENGINE_NOTE_START && play->notes[i].why == ENGINE_WHY_HELD_PRESS)
            run->held_press++;
        if (play->notes[i].kind == ENGINE_NOTE_BLOCK && play->notes[i].why == ENGINE_WHY_RUNNING)
            run->blocked++;
        if (play->notes[i].kind == ENGINE_NOTE_BLOCK && play->notes[i].why == ENGINE_WHY_PLAYING)
            run->playing++;
    }
    play->note_count = 0;
}

/* Simple Edit: whileHeld with a onceHeld block, 85 ms gap after a release.
   A re-press inside the gap was refused as already-running and the key then
   stayed held with nothing playing. It is kept and starts as held-press once
   the gap ends. A tap that is up again before then is dropped. */
static void check_held_press(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_step steps[4];
    struct engine_macro macro;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    static const int outputs[] = { ENGINE_OUTPUT_SOFTWARE, ENGINE_OUTPUT_RP2350 };
    int o;

    if (!play) {
        expect(0, "held press play alloc");
        return;
    }
    memset(steps, 0, sizeof steps);
    steps[0].type = ENGINE_STEP_ONCE_HELD;
    steps[0].a = 85;
    steps[0].first = 1;
    steps[0].count = 3;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = 0x4F;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 10;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_UP;
    steps[3].key = 0x4F;
    engine_macro_init(&macro);
    snprintf(macro.id, sizeof macro.id, "edit");
    macro.mode = ENGINE_MODE_WHILE_HELD;
    macro.advanced = 1;
    macro.release_stop = ENGINE_STOP_FINISH;
    macro.trigger = 0x42;
    macro.steps = steps;
    macro.step_count = 4;
    macro.top_count = 1;
    memset(&extra, 0, sizeof extra);
    extra.enabled = 1;
    memset(&live, 0, sizeof live);
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;

    for (o = 0; o < 2; o++) {
        struct owed_run run;
        int64_t now;

        /* Down 100-400, up, down again at 425 and held to 1425. */
        memset(&run, 0, sizeof run);
        engine_play_init(play);
        play->output.mode = outputs[o];
        engine_play_set_macros(play, &macro, 1);
        engine_trigger_init(&mem);
        engine_fingers_clear(&fingers);
        for (now = 0; now < 1600; now++) {
            int down = (now >= 100 && now < 400) || (now >= 425 && now < 1425);

            owed_ms(play, &live, &mem, &fingers, 0x42, down, "", now, &run);
            if (now >= 520 && now < 1425 && !engine_play_has_runner(play, "edit"))
                run.dead_ms++;
        }
        expect(run.blocked == 1, "a re-press in the gap is refused while the runner finishes");
        expect(run.held_press == 1 && run.starts == 2, "the refused press starts as held-press");
        expect(run.dead_ms == 0, "the held key plays the whole hold after the gap");
        expect(!engine_play_has_runner(play, "edit"), "held-press runner stops on the up");

        /* A 20 ms tap inside the gap is up before the runner ends: no start. */
        memset(&run, 0, sizeof run);
        engine_play_init(play);
        play->output.mode = outputs[o];
        engine_play_set_macros(play, &macro, 1);
        engine_trigger_init(&mem);
        engine_fingers_clear(&fingers);
        for (now = 0; now < 800; now++) {
            int down = (now >= 100 && now < 400) || (now >= 420 && now < 440);

            owed_ms(play, &live, &mem, &fingers, 0x42, down, "", now, &run);
        }
        expect(run.blocked == 1 && run.held_press == 0 && run.starts == 1,
               "a tap that is up before the gap ends is not owed");

        /* Focus leaves mid-hold and comes back with the key still down: the
           macro stays stopped until a new press. */
        memset(&run, 0, sizeof run);
        engine_play_init(play);
        play->output.mode = outputs[o];
        engine_play_set_macros(play, &macro, 1);
        engine_trigger_init(&mem);
        engine_fingers_clear(&fingers);
        snprintf(extra.focus_exe, sizeof extra.focus_exe, "game.exe");
        for (now = 0; now < 2000; now++) {
            int down = (now >= 100 && now < 1200) || (now >= 1500 && now < 1800);
            const char *front = (now >= 500 && now < 700) ? "explorer.exe" : "game.exe";

            owed_ms(play, &live, &mem, &fingers, 0x42, down, front, now, &run);
            if (now == 1000)
                expect(!engine_play_has_runner(play, "edit"),
                       "focus back with the key held does not restart it");
        }
        expect(run.starts == 2, "after a focus switch the next press plays");
        extra.focus_exe[0] = '\0';
    }
    free(play);
}

/* A profile, reload or start seeds before the tick's events are drained.
   A press drained in that same tick is still a down edge, and a key held
   before it does not start. */
static void check_seed_before_drain(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_step tap[5];
    struct engine_macro macro;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    struct owed_run run;

    if (!play) {
        expect(0, "seed play alloc");
        return;
    }
    engine_play_init(play);
    build_tap_block(tap, (uint16_t)'Z');
    engine_macro_init(&macro);
    snprintf(macro.id, sizeof macro.id, "reload");
    macro.mode = ENGINE_MODE_WHILE_HELD;
    macro.advanced = 1;
    macro.trigger = (uint16_t)'A';
    macro.steps = tap;
    macro.step_count = 5;
    macro.top_count = 5;
    memset(&extra, 0, sizeof extra);
    extra.enabled = 1;
    memset(&live, 0, sizeof live);
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;
    engine_play_set_macros(play, &macro, 1);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    memset(&run, 0, sizeof run);
    owed_ms(play, &live, &mem, &fingers, (uint16_t)'A', 0, "", 1, &run);
    /* The push and the press land in one tick. */
    engine_trigger_note_profile(&mem, 7);
    owed_ms(play, &live, &mem, &fingers, (uint16_t)'A', 1, "", 2, &run);
    expect(run.starts == 1, "a press in the tick of a profile push starts");
    {
        int64_t now;

        /* The release lands with a reload. The onceHeld gap runs out after it. */
        engine_trigger_reseed(&mem);
        for (now = 3; now < 400; now++)
            owed_ms(play, &live, &mem, &fingers, (uint16_t)'A', 0, "", now, &run);
    }
    expect(!engine_play_has_runner(play, "reload"), "a release in the tick of a reload still stops it");

    /* Held before the reload: no start. */
    memset(&run, 0, sizeof run);
    engine_fingers_set(&fingers, (uint16_t)'A', 1);
    engine_trigger_reseed(&mem);
    owed_ms(play, &live, &mem, &fingers, (uint16_t)'A', 1, "", 500, &run);
    expect(run.starts == 0, "a key held before a reload does not start");
    free(play);
}

static void check_run_once_held(void)
{
    static const uint16_t triggers[] = { 0x34, 0x5B, ENGINE_KEY_XBUTTON1, ENGINE_KEY_XBUTTON2 };
    static const char *names[] = { "4", "LWin", "mouse 4", "mouse 5" };
    static const int outputs[] = { ENGINE_OUTPUT_SOFTWARE, ENGINE_OUTPUT_RP2350 };
    static const char *output_names[] = { "software", "board" };
    static struct engine_hook_state hooks;
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_step steps[19];
    struct engine_macro macro;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    struct engine_hook_decision decision;
    /* Raw Input is up and sees the button held the whole time: no ups. */
    struct engine_raw_mouse raw;
    char label[128];
    int t;
    int o;
    int adv;

    if (!play) {
        expect(0, "run once play alloc");
        return;
    }
    engine_raw_mouse_init(&raw);
    engine_raw_mouse_set_ok(&raw, 1);
    build_stratagem(steps);
    memset(&decision, 0, sizeof decision);
    engine_macro_init(&macro);
    memset(&extra, 0, sizeof extra);
    memset(&live, 0, sizeof live);
    snprintf(macro.id, sizeof macro.id, "orbital");
    snprintf(extra.name, sizeof extra.name, "Orbital");
    macro.mode = ENGINE_MODE_ONCE;
    macro.advanced = 1;
    macro.steps = steps;
    macro.step_count = 19;
    macro.top_count = 19;
    extra.enabled = 1;
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;

    for (t = 0; t < 4; t++) {
        for (o = 0; o < 2; o++) {
            struct held_run run;
            int64_t repeat_at = 600;
            int64_t now;

            memset(&run, 0, sizeof run);
            run.lost.raw = &raw;
            engine_hook_init(&hooks);
            engine_play_init(play);
            play->output.mode = outputs[o];
            engine_play_set_macros(play, &macro, 1);
            engine_trigger_init(&mem);
            engine_fingers_clear(&fingers);
            macro.trigger = triggers[t];
            engine_arm_swallow(&hooks, &live, "helldivers2.exe", "", 1, play);
            /* Down at 100 ms, held 3 s, up at 3100 ms. */
            for (now = 0; now < 4000; now++) {
                int fed = 0;

                if (now == 100) {
                    hook_event(&hooks, triggers[t], 1, now, &decision);
                    fed = 1;
                } else if (now == repeat_at && now < 3100) {
                    hook_event(&hooks, triggers[t], 1, now, &decision);
                    repeat_at += 33;
                    fed = 1;
                } else if (now == 3100) {
                    hook_event(&hooks, triggers[t], 0, now, &decision);
                }
                if (fed) {
                    run.downs++;
                    run.swallowed += decision.swallow ? 1 : 0;
                }
                held_ms(play, &live, &hooks, &mem, &fingers, now, &run);
            }
            snprintf(label, sizeof label, "%s %s held 3 s with repeats plays exactly once",
                     output_names[o], names[t]);
            expect(run.downs == 77 && run.starts == 1 && run.lost_count == 0, label);
            snprintf(label, sizeof label, "%s %s run once finishes", output_names[o], names[t]);
            expect(engine_play_alive(play) == 0, label);
            snprintf(label, sizeof label, "%s swallows every %s down, repeats too",
                     output_names[o], names[t]);
            expect(run.swallowed == run.downs, label);
        }
    }

    /* A real up and down while the pass plays is ignored, advanced or basic.
       After it finishes, the next press plays it again. */
    for (adv = 0; adv < 2; adv++) {
        struct held_run run;
        int64_t now;

        memset(&run, 0, sizeof run);
        macro.advanced = adv;
        macro.trigger = ENGINE_KEY_XBUTTON1;
        engine_hook_init(&hooks);
        engine_play_init(play);
        play->output.mode = ENGINE_OUTPUT_RP2350;
        engine_play_set_macros(play, &macro, 1);
        engine_trigger_init(&mem);
        engine_fingers_clear(&fingers);
        engine_arm_swallow(&hooks, &live, "helldivers2.exe", "", 1, play);
        for (now = 0; now < 1500; now++) {
            if (now == 100 || now == 300 || now == 1000)
                hook_event(&hooks, ENGINE_KEY_XBUTTON1, 1, now, &decision);
            else if (now == 200 || now == 400 || now == 1100)
                hook_event(&hooks, ENGINE_KEY_XBUTTON1, 0, now, &decision);
            held_ms(play, &live, &hooks, &mem, &fingers, now, &run);
            if (now == 320) {
                snprintf(label, sizeof label, "%s a second press mid-run does not restart it",
                         adv ? "advanced" : "basic");
                expect(run.starts == 1 && run.playing == 1 &&
                           engine_play_has_runner(play, "orbital"), label);
            }
        }
        snprintf(label, sizeof label, "%s a press after the pass is done plays it again",
                 adv ? "advanced" : "basic");
        expect(run.starts == 2 && run.playing == 1 && engine_play_alive(play) == 0, label);
    }

    /* The hook loses mouse 4's up, and Raw Input still sees it. The next down
       is a new press, not a dead one. After the pass it plays again. While
       the pass plays, it is ignored like any press. Without Raw Input, or
       for a key, it stays a repeat: that press is dead, the one after its up
       plays. */
    for (o = 0; o < 2; o++) {
        static const char *cases[] = { "after the pass", "mid-run", "no raw input", "a key" };
        int c;

        for (c = 0; c < 4; c++) {
            struct engine_raw_mouse tally;
            struct held_run run;
            uint16_t id = c == 3 ? 0x34 : ENGINE_KEY_XBUTTON1;
            int64_t again = c == 1 ? 200 : 1000;
            int64_t now;

            engine_raw_mouse_init(&tally);
            engine_raw_mouse_set_ok(&tally, c != 2);
            memset(&run, 0, sizeof run);
            run.lost.raw = &tally;
            macro.advanced = 1;
            macro.trigger = id;
            engine_hook_init(&hooks);
            engine_play_init(play);
            play->output.mode = outputs[o];
            engine_play_set_macros(play, &macro, 1);
            engine_trigger_init(&mem);
            engine_fingers_clear(&fingers);
            engine_arm_swallow(&hooks, &live, "helldivers2.exe", "", 1, play);
            for (now = 0; now < 3000; now++) {
                if (now == 100 || now == again || now == 2000)
                    hook_event(&hooks, id, 1, now, &decision);
                /* The real up: Raw Input counts it, the hook never reports it. */
                if (now == again - 50)
                    engine_raw_mouse_up(&tally, id);
                if (now == again + 100) {
                    engine_raw_mouse_up(&tally, id);
                    hook_event(&hooks, id, 0, now, &decision);
                }
                if (now == 2100) {
                    engine_raw_mouse_up(&tally, id);
                    hook_event(&hooks, id, 0, now, &decision);
                }
                held_ms(play, &live, &hooks, &mem, &fingers, now, &run);
                if (now == again + 20 && c == 1) {
                    snprintf(label, sizeof label,
                             "%s a lost release mid-run is a press the pass ignores",
                             output_names[o]);
                    expect(run.lost_count == 1 && run.starts == 1 && run.playing == 1, label);
                }
            }
            snprintf(label, sizeof label, "%s lost release %s: starts", output_names[o], cases[c]);
            if (c == 0)
                expect(run.lost_count == 1 && run.starts == 3, label);
            else if (c == 1)
                expect(run.lost_count == 1 && run.starts == 2 && run.playing == 1, label);
            else
                expect(run.lost_count == 0 && run.starts == 2, label);
            snprintf(label, sizeof label, "%s lost release %s: ends up and idle", output_names[o],
                     cases[c]);
            expect(!engine_fingers_down(&fingers, id) && engine_play_alive(play) == 0, label);
        }
    }
    free(play);
}

/* Ability in Overwatch Test: once, one onceHeld block that taps r. Five steps. */
static void build_tap_block(struct engine_step *steps, uint16_t key)
{
    memset(steps, 0, 5 * sizeof *steps);
    steps[0].type = ENGINE_STEP_ONCE_HELD;
    steps[0].first = 1;
    steps[0].count = 4;
    steps[0].a = 150;
    steps[1].type = ENGINE_STEP_KEY;
    steps[1].action = ENGINE_ACT_DOWN;
    steps[1].key = key;
    steps[2].type = ENGINE_STEP_WAIT;
    steps[2].a = 18;
    steps[3].type = ENGINE_STEP_KEY;
    steps[3].action = ENGINE_ACT_UP;
    steps[3].key = key;
    steps[4].type = ENGINE_STEP_WAIT;
    steps[4].a = 18;
}

/* One playback tick per millisecond: poll, then tick, like playback_tick. */
static void play_ms(struct engine_play *play, const struct engine_live_config *live,
                    struct engine_trigger_mem *mem, const struct engine_fingers *fingers,
                    int64_t *now, int ms, int *starts)
{
    int i;

    for (i = 0; i < ms; i++) {
        engine_play_set_fingers(play, fingers);
        engine_poll_triggers(play, live, mem, fingers, "Overwatch.exe", "");
        if (starts)
            *starts += play->pending_count;
        engine_play_tick(play, *now);
        (*now)++;
    }
}

static void check_many_starts(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_step tap[5];
    struct engine_step loop[5];
    struct engine_macro macros[2];
    struct engine_profile_extra extras[2];
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    int64_t now = 1;
    int starts = 0;
    int typed = 0;
    int i;

    if (!play) {
        expect(0, "many play alloc");
        return;
    }
    engine_play_init(play);
    build_tap_block(tap, (uint16_t)'R');
    build_tap_block(loop, (uint16_t)'A');
    loop[0].type = ENGINE_STEP_HOLD_LOOP;
    loop[0].a = 0;
    memset(extras, 0, sizeof extras);
    engine_macro_init(&macros[0]);
    engine_macro_init(&macros[1]);
    snprintf(macros[0].id, sizeof macros[0].id, "ability");
    macros[0].mode = ENGINE_MODE_ONCE;
    macros[0].advanced = 1;
    macros[0].trigger = (uint16_t)'R';
    macros[0].steps = tap;
    macros[0].step_count = 5;
    macros[0].top_count = 1;
    snprintf(macros[1].id, sizeof macros[1].id, "reload");
    macros[1].mode = ENGINE_MODE_WHILE_HELD;
    macros[1].advanced = 1;
    macros[1].trigger = (uint16_t)'A';
    macros[1].steps = loop;
    macros[1].step_count = 5;
    macros[1].top_count = 1;
    extras[0].enabled = 1;
    extras[1].enabled = 1;
    memset(&live, 0, sizeof live);
    live.macros = macros;
    live.extras = extras;
    live.macro_count = 2;
    engine_play_set_macros(play, macros, 2);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    play_ms(play, &live, &mem, &fingers, &now, 5, NULL);

    /* Reload stays held for the whole run, so its runner never leaves. */
    engine_fingers_set(&fingers, (uint16_t)'A', 1);
    play_ms(play, &live, &mem, &fingers, &now, 5, &starts);
    expect(starts == 1, "reload starts once and stays");
    starts = 0;
    for (i = 0; i < 500; i++) {
        engine_fingers_set(&fingers, (uint16_t)'R', 1);
        play_ms(play, &live, &mem, &fingers, &now, 1, &starts);
        if (engine_play_has_runner(play, "ability"))
            typed++;
        play_ms(play, &live, &mem, &fingers, &now, 40, NULL);
        engine_fingers_set(&fingers, (uint16_t)'R', 0);
        play_ms(play, &live, &mem, &fingers, &now, 260, NULL);
    }
    expect(starts == 500, "500 taps of r start 500 runners");
    expect(typed == 500, "every tap of r is running on its press tick");
    expect(engine_play_has_runner(play, "reload"), "the held reload runner is still alive");
    engine_fingers_set(&fingers, (uint16_t)'A', 0);
    play_ms(play, &live, &mem, &fingers, &now, 100, NULL);
    expect(engine_play_alive(play) == 0, "nothing runs once both keys are up");
    expect(play->arena_count == 0, "the arena is empty when idle");
    expect(play->mute_count == 0, "the mute table is empty when idle");
    if (starts != 500)
        fprintf(stderr, "many starts: %d of 500\n", starts);
    free(play);
}

static void check_mute_compaction(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_step hold_x[5];
    struct engine_step hold_y[5];
    struct engine_step tap[5];
    struct engine_macro macros[4];
    struct engine_profile_extra extras[4];
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    static const int32_t mute_b[1] = { 2 };
    static const int32_t mute_c[1] = { 3 };
    const char *ids[4] = { "x", "y", "b", "c" };
    const uint16_t keys[4] = { (uint16_t)'X', (uint16_t)'Y', (uint16_t)'B', (uint16_t)'C' };
    int64_t now = 1;
    int starts = 0;
    int i;

    if (!play) {
        expect(0, "mute play alloc");
        return;
    }
    engine_play_init(play);
    build_tap_block(hold_x, (uint16_t)'Q');
    hold_x[0].type = ENGINE_STEP_HOLD_LOOP;
    hold_x[0].a = 0;
    hold_x[0].mute_n = 1;
    memcpy(hold_y, hold_x, sizeof hold_y);
    build_tap_block(tap, (uint16_t)'Z');
    memset(extras, 0, sizeof extras);
    for (i = 0; i < 4; i++) {
        engine_macro_init(&macros[i]);
        snprintf(macros[i].id, sizeof macros[i].id, "%s", ids[i]);
        macros[i].mode = ENGINE_MODE_WHILE_HELD;
        macros[i].advanced = 1;
        macros[i].trigger = keys[i];
        macros[i].steps = i == 0 ? hold_x : i == 1 ? hold_y : tap;
        macros[i].step_count = 5;
        macros[i].top_count = 1;
        extras[i].enabled = 1;
    }
    macros[0].mutes = mute_b;
    macros[0].mute_count = 1;
    macros[1].mutes = mute_c;
    macros[1].mute_count = 1;
    memset(&live, 0, sizeof live);
    live.macros = macros;
    live.extras = extras;
    live.macro_count = 4;
    engine_play_set_macros(play, macros, 4);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    play_ms(play, &live, &mem, &fingers, &now, 5, NULL);

    /* y holds the first mute slot, x the second. */
    engine_fingers_set(&fingers, (uint16_t)'Y', 1);
    play_ms(play, &live, &mem, &fingers, &now, 5, NULL);
    engine_fingers_set(&fingers, (uint16_t)'X', 1);
    play_ms(play, &live, &mem, &fingers, &now, 5, NULL);
    expect(engine_play_muted(play, "b") && engine_play_muted(play, "c"), "x mutes b and y mutes c");
    engine_fingers_set(&fingers, (uint16_t)'Y', 0);
    play_ms(play, &live, &mem, &fingers, &now, 100, NULL);
    expect(!engine_play_muted(play, "c") && engine_play_muted(play, "b"),
           "y ending unmutes c and leaves b muted");
    /* x's mute span slid down. Its release must still name b. */
    for (i = 0; i < 300; i++) {
        engine_fingers_set(&fingers, (uint16_t)'Y', 1);
        play_ms(play, &live, &mem, &fingers, &now, 1, &starts);
        play_ms(play, &live, &mem, &fingers, &now, 30, NULL);
        engine_fingers_set(&fingers, (uint16_t)'Y', 0);
        play_ms(play, &live, &mem, &fingers, &now, 60, NULL);
    }
    expect(starts == 300, "300 starts of a muting hold loop all start");
    expect(engine_play_muted(play, "b") && !engine_play_muted(play, "c"), "b is still muted by x");
    engine_fingers_set(&fingers, (uint16_t)'X', 0);
    play_ms(play, &live, &mem, &fingers, &now, 100, NULL);
    expect(!engine_play_muted(play, "b") && !engine_play_muted(play, "c"), "x ending unmutes b");
    expect(play->mute_count == 0 && play->arena_count == 0, "both tables are empty when idle");
    engine_fingers_set(&fingers, (uint16_t)'B', 1);
    starts = 0;
    play_ms(play, &live, &mem, &fingers, &now, 1, &starts);
    expect(starts == 1, "b starts after the mute is gone");
    free(play);
}

/* A config switch reaps the muting runner after the catalog has changed. */
static void check_mute_switch(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_step hold[5];
    struct engine_step tap[5];
    struct engine_macro first[2];
    struct engine_macro second[2];
    struct engine_profile_extra extras[2];
    struct engine_live_config live_a;
    struct engine_live_config live_b;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    static const int32_t mute_b[1] = { 1 };
    int64_t now = 1;
    int starts = 0;
    int i;

    if (!play) {
        expect(0, "switch play alloc");
        return;
    }
    engine_play_init(play);
    build_tap_block(hold, (uint16_t)'Q');
    hold[0].type = ENGINE_STEP_HOLD_LOOP;
    hold[0].a = 0;
    hold[0].mute_n = 1;
    build_tap_block(tap, (uint16_t)'Z');
    memset(extras, 0, sizeof extras);
    extras[0].enabled = 1;
    extras[1].enabled = 1;
    for (i = 0; i < 2; i++) {
        engine_macro_init(&first[i]);
        first[i].mode = ENGINE_MODE_WHILE_HELD;
        first[i].advanced = 1;
        first[i].step_count = 5;
        first[i].top_count = 1;
    }
    snprintf(first[0].id, sizeof first[0].id, "a");
    first[0].trigger = (uint16_t)'A';
    first[0].steps = hold;
    first[0].mutes = mute_b;
    first[0].mute_count = 1;
    snprintf(first[1].id, sizeof first[1].id, "b");
    first[1].trigger = (uint16_t)'B';
    first[1].steps = tap;
    /* The other config lists b first. Index 1 is now d. */
    second[0] = first[1];
    second[1] = first[1];
    snprintf(second[1].id, sizeof second[1].id, "d");
    second[1].trigger = (uint16_t)'D';
    memset(&live_a, 0, sizeof live_a);
    live_a.macros = first;
    live_a.extras = extras;
    live_a.macro_count = 2;
    live_b = live_a;
    live_b.macros = second;

    engine_play_set_macros(play, first, 2);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    play_ms(play, &live_a, &mem, &fingers, &now, 5, NULL);
    engine_fingers_set(&fingers, (uint16_t)'A', 1);
    play_ms(play, &live_a, &mem, &fingers, &now, 5, NULL);
    expect(engine_play_muted(play, "b"), "a mutes b");

    /* playback_tick: set the new catalog, then reap. */
    engine_play_set_macros(play, second, 2);
    engine_reap_orphans(play, &live_b);
    engine_trigger_reseed(&mem);
    expect(!engine_play_muted(play, "b"), "reaping a after the switch unmutes b");
    expect(!engine_play_muted(play, "d"), "d was never muted");
    engine_fingers_set(&fingers, (uint16_t)'A', 0);
    play_ms(play, &live_b, &mem, &fingers, &now, 20, NULL);

    engine_play_set_macros(play, first, 2);
    engine_trigger_reseed(&mem);
    play_ms(play, &live_a, &mem, &fingers, &now, 5, NULL);
    engine_fingers_set(&fingers, (uint16_t)'B', 1);
    play_ms(play, &live_a, &mem, &fingers, &now, 1, &starts);
    expect(starts == 1, "b starts after switching back");
    free(play);
}

/* A board tap is two reports, a down then an up. Both must reach the pipe. */
static void check_report_queue(void)
{
    struct engine_report_queue *queue =
        (struct engine_report_queue *)calloc(1, sizeof *queue);
    struct engine_hook_state hooks;
    struct engine_hook_decision decision;
    struct engine_report report;
    struct engine_report got;
    int i;

    if (!queue) {
        expect(0, "queue alloc");
        return;
    }
    engine_report_queue_init(queue);
    for (i = 0; i < 5; i++) {
        engine_report_clear(&report);
        if (i % 2 == 0)
            engine_report_push_down(&report, (uint16_t)(0x41 + i));
        else
            engine_report_push_up(&report, (uint16_t)(0x41 + i - 1));
        expect(engine_report_queue_push(queue, &report) == 1, "report queued");
    }
    for (i = 0; i < 5; i++) {
        struct engine_report_echo echo[4];
        int n;

        expect(engine_report_queue_pop(queue, &got) == 1, "every report comes out");
        n = engine_report_echoes(&got, echo, 4);
        expect(n == 1 && echo[0].down == (i % 2 == 0) &&
                   echo[0].id == (uint16_t)(0x41 + (i % 2 == 0 ? i : i - 1)),
               "reports come out in publish order");
    }
    expect(engine_report_queue_pop(queue, &got) == 0, "the queue is empty after the drain");
    engine_report_clear(&report);
    engine_report_push_down(&report, 0x41);
    for (i = 0; i < (int)ENGINE_REPORT_QUEUE_CAP; i++)
        engine_report_queue_push(queue, &report);
    expect(engine_report_queue_push(queue, &report) == 0, "a full queue refuses");
    expect(engine_report_queue_take_overflow(queue) == 1, "overflow latches");
    expect(engine_report_queue_take_overflow(queue) == 0, "overflow is one shot");
    free(queue);

    /* Published at 0 and written at 300. The echo lands at 440. */
    engine_hook_init(&hooks);
    engine_report_clear(&report);
    engine_report_push_down(&report, 0x57);
    expect(engine_board_echo_written(&hooks, &report, 300, 1000) == 1, "one token per key");
    press_key(&hooks, 0x57, 440, &decision);
    expect(decision.enqueued == 0, "an echo 140 ms after the write is not a finger");
    press_key(&hooks, 0x57, 460, &decision);
    expect(decision.enqueued == 1, "a press after the window is a finger");
}

/* A profile push with the same macro count but a new order. */
static void check_profile_reseed(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_step tap[5];
    struct engine_macro first[2];
    struct engine_macro second[2];
    struct engine_profile_extra extras[2];
    struct engine_live_config live_a;
    struct engine_live_config live_b;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    int64_t now = 1;
    int starts = 0;

    if (!play) {
        expect(0, "reseed play alloc");
        return;
    }
    engine_play_init(play);
    build_tap_block(tap, (uint16_t)'Z');
    memset(extras, 0, sizeof extras);
    extras[0].enabled = 1;
    extras[1].enabled = 1;
    engine_macro_init(&first[0]);
    first[0].mode = ENGINE_MODE_ONCE;
    first[0].advanced = 1;
    first[0].steps = tap;
    first[0].step_count = 5;
    first[0].top_count = 1;
    first[1] = first[0];
    snprintf(first[0].id, sizeof first[0].id, "x");
    first[0].trigger = (uint16_t)'A';
    snprintf(first[1].id, sizeof first[1].id, "y");
    first[1].trigger = (uint16_t)'B';
    second[0] = first[1];
    second[1] = first[0];
    memset(&live_a, 0, sizeof live_a);
    live_a.macros = first;
    live_a.extras = extras;
    live_a.macro_count = 2;
    live_b = live_a;
    live_b.macros = second;

    engine_play_set_macros(play, first, 2);
    engine_trigger_init(&mem);
    engine_trigger_note_profile(&mem, 1);
    engine_fingers_clear(&fingers);
    play_ms(play, &live_a, &mem, &fingers, &now, 5, NULL);
    engine_fingers_set(&fingers, (uint16_t)'A', 1);
    play_ms(play, &live_a, &mem, &fingers, &now, 1, &starts);
    expect(starts == 1, "x starts on a");
    play_ms(play, &live_a, &mem, &fingers, &now, 5, NULL);

    starts = 0;
    engine_play_set_macros(play, second, 2);
    engine_trigger_note_profile(&mem, 2);
    play_ms(play, &live_b, &mem, &fingers, &now, 5, &starts);
    expect(starts == 0, "a held across the push does not start x again");
    engine_trigger_note_profile(&mem, 2);
    engine_fingers_set(&fingers, (uint16_t)'A', 0);
    play_ms(play, &live_b, &mem, &fingers, &now, 300, NULL);
    engine_fingers_set(&fingers, (uint16_t)'B', 1);
    play_ms(play, &live_b, &mem, &fingers, &now, 1, &starts);
    expect(starts == 1 && engine_play_has_runner(play, "y"), "b starts y in the new order");
    free(play);
}

/* AHK ClearHookBindings and SyncHooks drop physDown for trigger keys. A key
   whose up never arrived is cleared by an alt-tab out and back. */
static void check_forget_triggers(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_step loop[5];
    struct engine_macro macro;
    struct engine_profile_extra extra;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_claim_mem claims;
    struct engine_fingers fingers;
    uint16_t gone[8];
    int64_t now = 1;
    int starts = 0;
    int n;

    if (!play) {
        expect(0, "forget play alloc");
        return;
    }
    engine_play_init(play);
    build_tap_block(loop, (uint16_t)'Q');
    loop[0].type = ENGINE_STEP_HOLD_LOOP;
    loop[0].a = 0;
    engine_macro_init(&macro);
    snprintf(macro.id, sizeof macro.id, "reload");
    macro.mode = ENGINE_MODE_WHILE_HELD;
    macro.advanced = 1;
    macro.trigger = (uint16_t)'A';
    macro.steps = loop;
    macro.step_count = 5;
    macro.top_count = 1;
    memset(&extra, 0, sizeof extra);
    extra.enabled = 1;
    memset(&live, 0, sizeof live);
    snprintf(live.focus_exe, sizeof live.focus_exe, "Overwatch.exe");
    live.macros = &macro;
    live.extras = &extra;
    live.macro_count = 1;
    engine_play_set_macros(play, &macro, 1);
    engine_trigger_init(&mem);
    engine_claims_init(&claims);
    engine_fingers_clear(&fingers);

    n = engine_claims_sync(&claims, &live, "Overwatch.exe", "", 1, 0, &fingers, gone, 8);
    expect(n == 0, "the first sync only records");
    play_ms(play, &live, &mem, &fingers, &now, 5, NULL);
    engine_fingers_set(&fingers, (uint16_t)'A', 1);
    play_ms(play, &live, &mem, &fingers, &now, 1, &starts);
    expect(starts == 1, "a starts reload");
    n = engine_claims_sync(&claims, &live, "Overwatch.exe", "", 1, 0, &fingers, gone, 8);
    expect(n == 0 && engine_fingers_down(&fingers, (uint16_t)'A'), "same focus keeps the finger");

    /* The up for a is lost. Alt-tab out and back. */
    n = engine_claims_sync(&claims, &live, "explorer.exe", "", 1, 0, &fingers, gone, 8);
    expect(n == 1 && gone[0] == (uint16_t)'A', "leaving the game forgets the held trigger");
    expect(!engine_fingers_seen(&fingers, (uint16_t)'A'), "the trigger is unknown, like physDown.Delete");
    play_ms(play, &live, &mem, &fingers, &now, 100, NULL);
    expect(!engine_play_has_runner(play, "reload"), "reload ends after its pass, like AHK");
    n = engine_claims_sync(&claims, &live, "Overwatch.exe", "", 1, 0, &fingers, gone, 8);
    expect(n == 0, "coming back has nothing held");
    play_ms(play, &live, &mem, &fingers, &now, 5, NULL);
    starts = 0;
    engine_fingers_set(&fingers, (uint16_t)'A', 1);
    play_ms(play, &live, &mem, &fingers, &now, 1, &starts);
    expect(starts == 1, "the next press of a starts reload again");

    n = engine_claims_sync(&claims, &live, "Overwatch.exe", "", 1, 1, &fingers, gone, 8);
    expect(n == 1, "an output change forgets the held trigger");
    engine_fingers_set(&fingers, (uint16_t)'A', 1);
    engine_fingers_set(&fingers, (uint16_t)'W', 1);
    engine_claims_sync(&claims, &live, "Overwatch.exe", "", 1, 0, &fingers, gone, 8);
    n = engine_claims_sync(&claims, &live, "Overwatch.exe", "", 0, 0, &fingers, gone, 8);
    expect(n == 1 && gone[0] == (uint16_t)'A', "stop drops the trigger and nothing else");
    expect(engine_fingers_down(&fingers, (uint16_t)'W'), "a key that is not a trigger is kept");
    free(play);
}

static int find_note(const struct engine_play *play, int kind, const char *id, int why)
{
    int i;

    for (i = 0; i < play->note_count; i++) {
        if (play->notes[i].kind == kind && strcmp(play->notes[i].id, id) == 0 &&
            (why < 0 || play->notes[i].why == why))
            return i;
    }
    return -1;
}

/* The next engine.log must show every matched edge, start, stop, and refusal. */
static void check_notes(void)
{
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    static struct engine_step big[ENGINE_STEP_CAP + 1];
    struct engine_step loop[5];
    struct engine_macro macros[2];
    struct engine_profile_extra extras[2];
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    int64_t now = 1;
    int edge;
    int start;

    if (!play) {
        expect(0, "notes play alloc");
        return;
    }
    engine_play_init(play);
    build_tap_block(loop, (uint16_t)'Q');
    loop[0].type = ENGINE_STEP_HOLD_LOOP;
    loop[0].a = 0;
    memset(extras, 0, sizeof extras);
    extras[0].enabled = 1;
    extras[1].enabled = 1;
    engine_macro_init(&macros[0]);
    snprintf(macros[0].id, sizeof macros[0].id, "reload");
    macros[0].mode = ENGINE_MODE_WHILE_HELD;
    macros[0].trigger = (uint16_t)'A';
    macros[0].steps = loop;
    macros[0].step_count = 5;
    macros[0].top_count = 1;
    engine_macro_init(&macros[1]);
    snprintf(macros[1].id, sizeof macros[1].id, "huge");
    macros[1].mode = ENGINE_MODE_ONCE;
    macros[1].trigger = (uint16_t)'H';
    macros[1].steps = big;
    macros[1].step_count = ENGINE_STEP_CAP + 1;
    memset(&live, 0, sizeof live);
    live.macros = macros;
    live.extras = extras;
    live.macro_count = 2;
    engine_play_set_macros(play, macros, 2);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    play_ms(play, &live, &mem, &fingers, &now, 5, NULL);
    play->note_count = 0;

    engine_fingers_set(&fingers, (uint16_t)'A', 1);
    play_ms(play, &live, &mem, &fingers, &now, 1, NULL);
    edge = find_note(play, ENGINE_NOTE_EDGE, "reload", ENGINE_WHY_START);
    start = find_note(play, ENGINE_NOTE_START, "reload", ENGINE_WHY_EDGE);
    expect(edge >= 0 && play->notes[edge].down == 1 && play->notes[edge].key == (uint16_t)'A',
           "a down of a is an edge note that started reload");
    expect(start > edge, "the start note follows its edge");
    expect(strcmp(engine_why_name(ENGINE_WHY_ARENA_FULL), "arena-full") == 0, "reason names");
    play->note_count = 0;

    play_ms(play, &live, &mem, &fingers, &now, 50, NULL);
    engine_fingers_set(&fingers, (uint16_t)'A', 0);
    play_ms(play, &live, &mem, &fingers, &now, 2, NULL);
    edge = find_note(play, ENGINE_NOTE_EDGE, "reload", ENGINE_WHY_RELEASE);
    expect(edge >= 0 && play->notes[edge].down == 0, "the up of a is an edge note that stopped reload");
    start = find_note(play, ENGINE_NOTE_STOP, "reload", ENGINE_WHY_RELEASE);
    expect(start > edge && play->notes[start].age >= 50, "the stop note has the reason and the age");
    play->note_count = 0;

    engine_fingers_set(&fingers, (uint16_t)'H', 1);
    play_ms(play, &live, &mem, &fingers, &now, 1, NULL);
    expect(find_note(play, ENGINE_NOTE_BLOCK, "huge", ENGINE_WHY_ARENA_FULL) >= 0,
           "a start that cannot fit is a blocked note");
    expect(find_note(play, ENGINE_NOTE_EDGE, "huge", ENGINE_WHY_ARENA_FULL) >= 0,
           "its edge says why");
    play->note_count = 0;
    engine_play_note(play, ENGINE_NOTE_EDGE, "x", 0, 0, 0, 0, 0);
    play->note_count = ENGINE_NOTE_CAP;
    expect(engine_play_note(play, ENGINE_NOTE_EDGE, "x", 0, 0, 0, 0, 0) == -1 &&
               play->note_dropped == 1,
           "a full note ring counts the drop");
    free(play);
}

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "tests/fixtures"
#endif

struct path_run {
    int starts;
    int held_press;
    int playing;
    int space;
    int e;
};

/* One trigger on create-paths.json in playback_tick's order. edges are
   down/up pairs in ms. Counts starts, presses ignored while playing, and
   the space and e downs the output sees. */
static struct path_run play_path_on(const char *doc, size_t n, uint16_t trigger,
                                    const int64_t *edges, int pairs, int output)
{
    static struct engine_hook_state hooks;
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_profile_slot slot;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    struct engine_finger_lost lost;
    struct engine_hook_decision decision;
    const struct engine_profile_image *image;
    struct path_run run;
    int had_space = 0;
    int had_e = 0;
    int64_t now;
    int i;

    memset(&run, 0, sizeof run);
    if (!play)
        return run;
    engine_profile_slot_init(&slot);
    if (engine_profile_slot_apply_document(&slot, doc, n) != 1) {
        free(play);
        return run;
    }
    image = engine_profile_slot_adopt(&slot);
    engine_profile_pick(image, "game.exe", image->active_id, &live);
    engine_hook_init(&hooks);
    engine_play_init(play);
    play->output.mode = output;
    engine_play_set_macros(play, live.macros, live.macro_count);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    memset(&lost, 0, sizeof lost);
    engine_arm_swallow(&hooks, &live, "game.exe", "", 1, play);
    for (now = 0; now < 1500; now++) {
        struct engine_finger ev;
        uint16_t want[32];
        int got;
        int space = 0;
        int e = 0;

        for (i = 0; i < pairs; i++) {
            if (edges[2 * i] == now)
                hook_event(&hooks, trigger, 1, now, &decision);
            if (edges[2 * i + 1] == now)
                hook_event(&hooks, trigger, 0, now, &decision);
        }
        while (engine_event_pop(&hooks, &ev))
            engine_finger_note(&fingers, ev.id, ev.down, &lost);
        engine_play_set_fingers(play, &fingers);
        engine_poll_triggers(play, &live, &mem, &fingers, "game.exe", "");
        engine_play_tick(play, now);
        engine_trigger_owed(play, &live, &mem, &fingers, "game.exe", "");
        for (i = 0; i < play->note_count; i++) {
            if (play->notes[i].kind == ENGINE_NOTE_START)
                run.starts++;
            if (play->notes[i].kind == ENGINE_NOTE_START && play->notes[i].why == ENGINE_WHY_HELD_PRESS)
                run.held_press++;
            if (play->notes[i].kind == ENGINE_NOTE_BLOCK && play->notes[i].why == ENGINE_WHY_PLAYING)
                run.playing++;
        }
        play->note_count = 0;
        got = engine_play_desired(play, now, want, 32);
        for (i = 0; i < got; i++) {
            space |= want[i] == 0x20;
            e |= want[i] == 0x45;
        }
        run.space += space && !had_space;
        run.e += e && !had_e;
        had_space = space;
        had_e = e;
        engine_arm_swallow(&hooks, &live, "game.exe", "", 1, play);
    }
    engine_profile_slot_shutdown(&slot);
    free(play);
    return run;
}

static struct path_run play_path(const char *doc, size_t n, uint16_t trigger,
                                 const int64_t *edges, int pairs)
{
    return play_path_on(doc, n, trigger, edges, pairs, ENGINE_OUTPUT_SOFTWARE);
}

/* The whole file, NUL-terminated, or NULL. */
static char *read_fixture(const char *path, size_t *n)
{
    char *doc;
    FILE *f = fopen(path, "rb");

    *n = 0;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    doc = (char *)malloc(*n + 1);
    if (!doc || fread(doc, 1, *n, f) != *n) {
        fclose(f);
        free(doc);
        return NULL;
    }
    fclose(f);
    doc[*n] = 0;
    return doc;
}

/* fixtures/create-paths.json is written by the UI (npm run check --
   --write-fixture): one macro per way to make a Run Once, and Play > Repeat
   x3. The AHK --self-test plays the same file with the same expectations. */
static void check_create_paths(void)
{
    static const int64_t hold[] = { 10, 610 };
    static const int64_t tap[] = { 10, 40 };
    static const int64_t twice[] = { 10, 40, 90, 120 };
    static const int64_t held_again[] = { 10, 40, 90, 410 };
    static const struct {
        const char *name;
        uint16_t trigger;
    } paths[] = { { "key path", 0x47 }, { "new path", 0x48 } };
    char label[128];
    size_t n = 0;
    char *doc = read_fixture(FIXTURE_DIR "/create-paths.json", &n);
    struct path_run run;
    int p;

    if (!doc) {
        expect(0, "create-paths.json read");
        return;
    }
    for (p = 0; p < 2; p++) {
        run = play_path(doc, n, paths[p].trigger, hold, 1);
        snprintf(label, sizeof label, "%s: held 600 ms plays one pass", paths[p].name);
        expect(run.starts == 1 && run.space == 1 && run.e == 1, label);
        run = play_path(doc, n, paths[p].trigger, tap, 1);
        snprintf(label, sizeof label, "%s: a 30 ms tap plays the whole pass", paths[p].name);
        expect(run.starts == 1 && run.space == 1 && run.e == 1, label);
        run = play_path(doc, n, paths[p].trigger, twice, 2);
        snprintf(label, sizeof label, "%s: a press while it plays is ignored", paths[p].name);
        expect(run.starts == 1 && run.playing == 1 && run.space == 1 && run.e == 1, label);
        run = play_path(doc, n, paths[p].trigger, held_again, 2);
        snprintf(label, sizeof label, "%s: a held press while it plays is ignored", paths[p].name);
        expect(run.starts == 1 && run.playing == 1 && run.space == 1 && run.e == 1, label);
    }
    run = play_path(doc, n, 0x4A, tap, 1);
    expect(run.starts == 1 && run.space == 3, "repeat x3: a tap plays 3 times, not 9");
    run = play_path(doc, n, 0x4A, hold, 1);
    expect(run.starts == 1 && run.space == 3, "repeat x3: held plays 3 times, not 9");
    free(doc);
}

/* fixtures/hold-repress.json: one pass is space, 150 ms, e, about 190 ms,
   releaseStop nextUp. Up at 40 and down again at 90 lands in the pass that
   finishes after the release. A While Held, Swap After or One Press block
   saved as whileHeld went silent there until the up (#95). The press is
   kept and starts as held-press when that pass ends, so it plays like the
   same macro saved as once. The AHK --self-test plays the same file. */
static void check_hold_repress(void)
{
    static const int64_t hold[] = { 10, 610 };
    static const int64_t held_again[] = { 10, 40, 90, 410 };
    static const int outputs[] = { ENGINE_OUTPUT_SOFTWARE, ENGINE_OUTPUT_RP2350 };
    /* One Press plays its whole pass after a release and the re-press keeps
       the same runner going: one start, a second pass. */
    static const struct {
        const char *name;
        uint16_t trigger;
        int hold_space;
        int starts;
        int held_press;
        int space;
        int e;
    } macros[] = {
        { "While Held, whileHeld", 0x4B, 4, 2, 1, 3, 1 },
        { "While Held, once", 0x4C, 4, 2, 0, 3, 1 },
        { "Swap After, whileHeld", 0x4D, 4, 2, 1, 3, 1 },
        { "One Press, whileHeld", 0x4E, 3, 1, 0, 2, 2 },
    };
    char label[160];
    size_t n = 0;
    char *doc = read_fixture(FIXTURE_DIR "/hold-repress.json", &n);
    struct path_run run;
    int m;
    int o;

    if (!doc) {
        expect(0, "hold-repress.json read");
        return;
    }
    for (o = 0; o < 2; o++) {
        for (m = 0; m < 4; m++) {
            run = play_path_on(doc, n, macros[m].trigger, hold, 1, outputs[o]);
            snprintf(label, sizeof label, "%s: held 600 ms plays every pass", macros[m].name);
            expect(run.starts == 1 && run.space == macros[m].hold_space && run.e == 3, label);
            run = play_path_on(doc, n, macros[m].trigger, held_again, 2, outputs[o]);
            snprintf(label, sizeof label, "%s: a re-press while finishing plays while held",
                     macros[m].name);
            expect(run.starts == macros[m].starts && run.held_press == macros[m].held_press &&
                       run.space == macros[m].space && run.e == macros[m].e,
                   label);
        }
    }
    free(doc);
}

/* One scripted input for the radial harness. KEY is a key or button through
   the hook. RAW is a real mouse move for Raw Input. MOVE is a mouse move
   through the hook. FRONT changes the app in front. DISARM is Stop. */
#define RADIAL_KEY 0
#define RADIAL_RAW 1
#define RADIAL_MOVE 2
#define RADIAL_FRONT 3
#define RADIAL_DISARM 4

struct radial_input {
    int64_t at;
    int kind;
    uint16_t id;
    int down;
    int32_t dx;
    int32_t dy;
};

struct radial_run {
    int starts;
    int one_pass;
    int space;
    int e;
    int x;
    int z;
    /* The hook's answer for each input, in script order. */
    int swallow[24];
    /* "open rk keys", "select rk 2 played", "cancel rk no-slot", joined by "|". */
    char notes[512];
    int open_at_end;
};

static void radial_notes(struct engine_radial_state *st, struct radial_run *run)
{
    int i;

    for (i = 0; i < st->note_count; i++) {
        const struct engine_radial_note *n = &st->notes[i];
        size_t used = strlen(run->notes);
        char line[96];

        if (n->kind == ENGINE_RADIAL_NOTE_OPEN)
            snprintf(line, sizeof line, "open %s %s|", n->radial, engine_radial_mode_name(n->mode));
        else if (n->kind == ENGINE_RADIAL_NOTE_SELECT)
            snprintf(line, sizeof line, "select %s %d %s|", n->radial, n->slot + 1,
                     engine_radial_why_name(n->why, n->mode));
        else
            snprintf(line, sizeof line, "cancel %s %s|", n->radial, engine_radial_why_name(n->why, n->mode));
        if (used + strlen(line) < sizeof run->notes)
            memcpy(run->notes + used, line, strlen(line) + 1);
    }
    st->note_count = 0;
}

/* playback_tick's radial order on fixtures/radial.json, one tick per ms:
   drain, take the raw sum, move, poll the menu, poll triggers, tick, owed,
   then publish the swallow map. Counts the output downs. */
static struct radial_run radial_play(const char *doc, size_t n, const struct radial_input *script,
                                     int count, int output, int64_t until)
{
    static struct engine_hook_state hooks;
    struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
    struct engine_radial_state radial;
    struct engine_raw_mouse raw;
    struct engine_profile_slot slot;
    struct engine_live_config live;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    struct engine_finger_lost lost;
    struct engine_hook_decision decision;
    const struct engine_profile_image *image;
    struct radial_run run;
    const char *front = "game.exe";
    int armed = 1;
    int had[4] = { 0, 0, 0, 0 };
    int64_t now;
    int i;

    memset(&run, 0, sizeof run);
    if (!play)
        return run;
    engine_profile_slot_init(&slot);
    if (engine_profile_slot_apply_document(&slot, doc, n) != 1) {
        free(play);
        return run;
    }
    image = engine_profile_slot_adopt(&slot);
    engine_profile_pick(image, "game.exe", image->active_id, &live);
    engine_hook_init(&hooks);
    engine_raw_mouse_init(&raw);
    engine_radial_init(&radial);
    engine_play_init(play);
    play->output.mode = output;
    engine_play_set_macros(play, live.macros, live.macro_count);
    engine_trigger_init(&mem);
    engine_fingers_clear(&fingers);
    memset(&lost, 0, sizeof lost);
    engine_arm_swallow_radial(&hooks, &live, front, "", armed, play, &radial);
    for (now = 0; now < until; now++) {
        struct engine_finger ev;
        uint16_t want[32];
        static const uint16_t keys[4] = { 0x20, 0x45, 0x58, 0x5A };
        int downs[4] = { 0, 0, 0, 0 };
        int32_t dx = 0;
        int32_t dy = 0;
        int got;

        for (i = 0; i < count; i++) {
            const struct radial_input *in = &script[i];
            struct engine_hook_sample sample;

            if (in->at != now)
                continue;
            memset(&decision, 0, sizeof decision);
            if (in->kind == RADIAL_KEY) {
                hook_event(&hooks, in->id, in->down, now, &decision);
            } else if (in->kind == RADIAL_RAW) {
                engine_raw_move(&raw, in->dx, in->dy);
            } else if (in->kind == RADIAL_MOVE) {
                memset(&sample, 0, sizeof sample);
                sample.kind = ENGINE_HOOK_MOUSE;
                sample.msg = ENGINE_MSG_MOUSEMOVE;
                sample.now = now;
                engine_hook_classify(&hooks, &sample, &decision);
            } else if (in->kind == RADIAL_FRONT) {
                front = in->down ? "game.exe" : "MacroShell.exe";
            } else if (in->kind == RADIAL_DISARM) {
                armed = 0;
            }
            if (i < (int)(sizeof run.swallow / sizeof run.swallow[0]))
                run.swallow[i] = decision.swallow;
        }
        while (engine_event_pop(&hooks, &ev))
            engine_finger_note(&fingers, ev.id, ev.down, &lost);
        engine_play_set_fingers(play, &fingers);
        engine_raw_take(&raw, &dx, &dy);
        engine_radial_move(&radial, dx, dy);
        engine_radial_poll(&radial, play, &live, &fingers, front, "", armed);
        engine_raw_aim(&raw, engine_radial_wants_motion(&radial));
        radial_notes(&radial, &run);
        if (armed)
            engine_poll_triggers_radial(play, &live, &mem, &fingers, front, "", &radial);
        for (i = 0; i < play->pending_count; i++)
            run.one_pass += play->pending[i].one_pass;
        engine_play_tick(play, now);
        if (armed)
            engine_trigger_owed(play, &live, &mem, &fingers, front, "");
        for (i = 0; i < play->note_count; i++) {
            if (play->notes[i].kind == ENGINE_NOTE_START)
                run.starts++;
        }
        play->note_count = 0;
        got = engine_play_desired(play, now, want, 32);
        for (i = 0; i < got; i++) {
            int k;

            for (k = 0; k < 4; k++)
                downs[k] |= want[i] == keys[k];
        }
        run.space += downs[0] && !had[0];
        run.e += downs[1] && !had[1];
        run.x += downs[2] && !had[2];
        run.z += downs[3] && !had[3];
        for (i = 0; i < 4; i++)
            had[i] = downs[i];
        engine_arm_swallow_radial(&hooks, &live, front, "", armed, play, &radial);
    }
    run.open_at_end = radial.open;
    engine_profile_slot_shutdown(&slot);
    free(play);
    return run;
}

#define K(at, id, down) { at, RADIAL_KEY, id, down, 0, 0 }
#define RAW(at, dx, dy) { at, RADIAL_RAW, 0, 0, dx, dy }
#define MOVE(at) { at, RADIAL_MOVE, 0, 0, 0, 0 }

/* fixtures/radial.json is written by the UI (npm run check -- --write-fixture).
   rk is a keys menu on t, rf a flick menu on y with click to pick. Slots:
   1 Strat A (space, e), 2 Strat B (x), 3 Held C (While Held, space, e),
   4 empty. Key one is a macro on 1. The AHK --self-test plays the same file. */
#define T 0x54
#define Y 0x59
#define Q 0x51
#define E 0x45
#define ONE 0x31
static void check_radial(void)
{
    static const int outputs[] = { ENGINE_OUTPUT_SOFTWARE, ENGINE_OUTPUT_RP2350 };
    static const struct radial_input pick[] = { K(10, T, 1), K(30, 0x32, 1), K(50, 0x32, 0), K(80, T, 0) };
    static const struct radial_input cycle[] = { K(10, T, 1), K(20, E, 1), K(25, E, 0), K(30, E, 1),
                                                 K(35, E, 0), K(40, Q, 1), K(45, Q, 0), K(80, T, 0) };
    static const struct radial_input nothing[] = { K(10, T, 1), K(80, T, 0) };
    static const struct radial_input empty[] = { K(10, T, 1), K(30, 0x34, 1), K(40, 0x34, 0), K(80, T, 0) };
    static const struct radial_input owned[] = { K(10, T, 1), K(30, ONE, 1), K(50, ONE, 0), K(80, T, 0),
                                                 K(400, ONE, 1), K(420, ONE, 0) };
    static const struct radial_input held[] = { K(10, T, 1), K(30, 0x33, 1), K(40, 0x33, 0), K(80, T, 0) };
    static const struct radial_input twice[] = { K(10, T, 1), K(20, ONE, 1), K(25, ONE, 0), K(30, T, 0),
                                                 K(60, T, 1), K(70, ONE, 1), K(75, ONE, 0), K(90, T, 0) };
    static const struct radial_input flick[] = { K(10, Y, 1), MOVE(20), RAW(20, 0, -100), K(80, Y, 0), MOVE(100) };
    static const struct radial_input dead[] = { K(10, Y, 1), RAW(20, 10, 10), K(80, Y, 0) };
    static const struct radial_input back[] = { K(10, Y, 1), RAW(20, 0, -100), RAW(30, 0, 400), K(80, Y, 0) };
    static const struct radial_input click[] = { K(10, Y, 1), RAW(20, 100, 0), K(40, ENGINE_KEY_LBUTTON, 1),
                                                 K(50, ENGINE_KEY_LBUTTON, 0), K(80, Y, 0), K(200, ENGINE_KEY_LBUTTON, 1),
                                                 K(210, ENGINE_KEY_LBUTTON, 0) };
    static const struct radial_input focus[] = { K(10, T, 1), K(20, 0x32, 1), K(25, 0x32, 0),
                                                 { 30, RADIAL_FRONT, 0, 0, 0, 0 }, K(80, T, 0) };
    static const struct radial_input stop[] = { K(10, T, 1), K(20, 0x32, 1), K(25, 0x32, 0),
                                                { 30, RADIAL_DISARM, 0, 0, 0, 0 }, K(80, T, 0) };
    char label[160];
    size_t n = 0;
    char *doc = read_fixture(FIXTURE_DIR "/radial.json", &n);
    struct radial_run run;
    int o;

#define RUN(script) radial_play(doc, n, script, (int)(sizeof script / sizeof script[0]), outputs[o], 700)
#define SAY(text) (snprintf(label, sizeof label, "radial %s: %s", outputs[o] ? "board" : "software", text), label)
    if (!doc) {
        expect(0, "radial.json read");
        return;
    }
    expect(engine_radial_slot_at(0, -100, 4, 40) == 0 && engine_radial_slot_at(100, 0, 4, 40) == 1 &&
               engine_radial_slot_at(0, 100, 4, 40) == 2 && engine_radial_slot_at(-100, 0, 4, 40) == 3,
           "radial: slot 1 is up, then clockwise");
    expect(engine_radial_slot_at(20, -20, 4, 40) == -1, "radial: inside the dead zone is no slot");
    expect(engine_radial_slot_at(100, -100, 8, 40) == 1 && engine_radial_slot_at(-100, -100, 8, 40) == 7,
           "radial: 8 slots split the diagonals");
    {
        static const char later[] =
            "{\"version\":1,\"name\":\"R\",\"activeId\":\"c\",\"configs\":[{\"id\":\"c\",\"name\":\"C\",\"macros\":["
            "{\"id\":\"m\",\"enabled\":true,\"advanced\":true,\"trigger\":{\"kind\":\"key\",\"button\":\"g\"},"
            "\"blocks\":[{\"type\":\"steps\",\"steps\":[{\"type\":\"key\",\"action\":\"tap\",\"key\":\"x\"}]}]}],"
            "\"radials\":[{\"id\":\"a\",\"trigger\":{\"kind\":\"key\",\"button\":\"t\"},\"select\":\"auto\",\"slots\":[\"m\",\"gone\"]},"
            "{\"id\":\"b\",\"trigger\":{\"kind\":\"key\",\"button\":\"g\"},\"slots\":[\"m\",\"\"]},"
            "{\"id\":\"c\",\"trigger\":{\"kind\":\"mouse\",\"button\":\"LButton\"},\"slots\":[\"m\",\"\"]}]}],\"macros\":[]}";
        struct engine_profile_slot slot;
        struct engine_live_config live;
        const struct engine_profile_image *image;

        engine_profile_slot_init(&slot);
        expect(engine_profile_slot_apply_document(&slot, later, sizeof later - 1) == 1, "radial: profile loads");
        image = engine_profile_slot_adopt(&slot);
        engine_profile_pick(image, "game.exe", image ? image->active_id : "", &live);
        expect(live.radial_count == 3, "radial: the config keeps its menus");
        expect(live.radial_count == 3 && engine_radial_mode(&live.radials[0], &live) == ENGINE_RADIAL_KEYS,
               "radial: a select this engine does not know plays as keys");
        expect(live.radial_count == 3 && live.radials[0].slot[0] == 0 && live.radials[0].slot[1] == -1,
               "radial: a slot whose macro is gone is empty");
        expect(live.radial_count == 3 && engine_radial_ready(&live.radials[0], &live) &&
                   !engine_radial_ready(&live.radials[1], &live) && !engine_radial_ready(&live.radials[2], &live),
               "radial: a trigger a macro uses, or left click, never opens a menu");
        expect(engine_live_claims(&live, 0x54) && !engine_live_needs_mouse(&live),
               "radial: a keys menu on t claims t and needs no mouse hook");
        engine_profile_slot_shutdown(&slot);
    }
    for (o = 0; o < 2; o++) {
        run = RUN(pick);
        expect(strcmp(run.notes, "open rk keys|select rk 2 played|") == 0, SAY("2 lights slot 2, the release plays it"));
        expect(run.starts == 1 && run.x == 1 && run.space == 0, SAY("slot 2 plays Strat B once"));
        expect(run.swallow[0] && run.swallow[1] && run.swallow[3], SAY("the trigger and 2 are swallowed"));

        run = RUN(cycle);
        expect(strcmp(run.notes, "open rk keys|select rk 1 played|") == 0, SAY("next, next, previous lands on slot 1"));
        expect(run.space == 1 && run.x == 0, SAY("slot 1 plays Strat A"));
        expect(run.swallow[1] && run.swallow[5], SAY("the step keys are swallowed while open"));

        run = RUN(nothing);
        expect(strcmp(run.notes, "open rk keys|cancel rk no-slot|") == 0 && run.starts == 0,
               SAY("a release with nothing picked plays nothing"));

        run = RUN(empty);
        expect(strcmp(run.notes, "open rk keys|cancel rk empty-slot|") == 0 && run.starts == 0,
               SAY("an empty slot plays nothing"));

        run = RUN(owned);
        expect(strcmp(run.notes, "open rk keys|select rk 1 played|") == 0, SAY("1 picks slot 1 while open"));
        expect(run.z == 1 && run.space == 1, SAY("the macro on 1 does not start while the menu is open, then does"));

        run = RUN(held);
        expect(strcmp(run.notes, "open rk keys|select rk 3 played|") == 0, SAY("slot 3 plays"));
        expect(run.one_pass == 1 && run.space == 1 && run.e == 1, SAY("a While Held slot plays one full pass"));

        run = RUN(twice);
        expect(strcmp(run.notes, "open rk keys|select rk 1 played|open rk keys|select rk 1 playing|") == 0,
               SAY("a slot macro that is still playing is not started again"));
        expect(run.starts == 1 && run.space == 1, SAY("Strat A plays once"));

        run = RUN(flick);
        expect(strcmp(run.notes, "open rf flick|select rf 1 played|") == 0, SAY("a flick up picks slot 1"));
        expect(run.swallow[1] && !run.swallow[4], SAY("a move is swallowed only while the flick menu is open"));

        run = RUN(dead);
        expect(strcmp(run.notes, "open rf flick|cancel rf dead-zone|") == 0 && run.starts == 0,
               SAY("a release inside the dead zone cancels"));

        run = RUN(back);
        expect(strcmp(run.notes, "open rf flick|select rf 3 played|") == 0, SAY("a flick back turns at once: the reach is capped"));

        run = RUN(click);
        expect(strcmp(run.notes, "open rf flick|select rf 2 played|") == 0 && run.x == 1, SAY("a click picks the lit slot"));
        expect(run.swallow[2] && run.swallow[3] && !run.swallow[5], SAY("left click is the menu's only while open"));

        run = RUN(focus);
        expect(strcmp(run.notes, "open rk keys|cancel rk focus|") == 0 && run.starts == 0,
               SAY("the menu closes when the game loses focus"));

        run = RUN(stop);
        expect(strcmp(run.notes, "open rk keys|cancel rk stop|") == 0 && run.starts == 0 && !run.open_at_end,
               SAY("Stop closes the menu"));
    }
#undef RUN
#undef SAY
#undef T
#undef Y
#undef Q
#undef E
#undef ONE
    free(doc);
}

/* One named vector from docs/engine/ipc-vectors.txt. Length, or -1. */
static int load_vector(const char *name, unsigned char *out, int cap)
{
    char line[4096];
    int want = 0;
    int n = -1;
    FILE *f = fopen(FIXTURE_DIR "/../../../docs/engine/ipc-vectors.txt", "rb");

    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(name);

        if (want) {
            const char *p = line;

            n = 0;
            while (*p && n < cap) {
                unsigned v;

                while (*p == ' ')
                    p++;
                if (sscanf(p, "%2x", &v) != 1)
                    break;
                out[n++] = (unsigned char)v;
                p += 2;
            }
            break;
        }
        if (strncmp(line, name, len) == 0 && line[len] == ' ')
            want = 1;
    }
    fclose(f);
    return n;
}

/* The overlay is drawn from this view alone, so it must name every slot. */
static void check_radial_view(void)
{
    struct engine_radial_view view;
    unsigned char want[512];
    unsigned char got[1600];
    int want_n;
    int got_n;
    size_t n = 0;
    char *doc;

    memset(&view, 0, sizeof view);
    view.open = 1;
    view.mode = ENGINE_RADIAL_KEYS;
    view.slot_count = 4;
    view.lit = 1;
    view.size = 340;
    view.opacity = 88;
    view.seq = 7;
    view.stamp = 0x100000002ll;
    snprintf(view.id, sizeof view.id, "rk");
    snprintf(view.name, sizeof view.name, "Keys");
    snprintf(view.prev, sizeof view.prev, "q");
    snprintf(view.next, sizeof view.next, "e");
    snprintf(view.labels[0], sizeof view.labels[0], "Strat A");
    snprintf(view.labels[1], sizeof view.labels[1], "Strat B");
    snprintf(view.labels[3], sizeof view.labels[3], "Held C");
    want_n = load_vector("radial-open", want, (int)sizeof want);
    got_n = engine_radial_frame(&view, got, (int)sizeof got);
    expect(want_n > 0 && want_n == got_n && memcmp(want, got, (size_t)got_n) == 0,
           "radial frame matches the radial-open vector");
    memset(&view, 0, sizeof view);
    view.lit = -1;
    view.why = ENGINE_RADIAL_WHY_NO_SLOT;
    view.seq = 8;
    want_n = load_vector("radial-closed", want, (int)sizeof want);
    got_n = engine_radial_frame(&view, got, (int)sizeof got);
    expect(want_n > 0 && want_n == got_n && memcmp(want, got, (size_t)got_n) == 0,
           "radial frame matches the radial-closed vector");
    expect(engine_radial_frame(&view, got, 20) == -1, "radial frame too small for the buffer");
    {
        struct engine_radial_view sent;
        struct engine_radial_view now;

        memset(&sent, 0, sizeof sent);
        memset(&now, 0, sizeof now);
        sent.lit = now.lit = -1;
        expect(!engine_radial_due(&sent, &now, 5000, 0, 1000), "radial beat: closed and still closed sends nothing");
        now.open = 1;
        snprintf(now.id, sizeof now.id, "rk");
        expect(engine_radial_due(&sent, &now, 10, 0, 1000), "radial beat: an open goes out at once");
        sent = now;
        expect(!engine_radial_due(&sent, &now, 199, 0, 1000), "radial beat: nothing new inside 200 ms");
        expect(engine_radial_due(&sent, &now, 200, 0, 1000), "radial beat: open for 200 ms sends again");
        expect(engine_radial_due(&sent, &now, 2000, 0, 1000000) == 0 && engine_radial_due(&sent, &now, 200000, 0, 1000000),
               "radial beat: counts QPC ticks at the given rate");
        now.lit = 2;
        expect(engine_radial_due(&sent, &now, 1, 0, 1000), "radial beat: a new lit slot goes out at once");
        now.open = 0;
        expect(engine_radial_due(&sent, &now, 1, 0, 1000), "radial beat: the close goes out at once");
    }

    doc = read_fixture(FIXTURE_DIR "/radial.json", &n);
    if (!doc) {
        expect(0, "radial.json read for the view");
        return;
    }
    {
        struct engine_play *play = (struct engine_play *)calloc(1, sizeof *play);
        struct engine_radial_state st;
        struct engine_profile_slot slot;
        struct engine_live_config live;
        struct engine_fingers fingers;
        const struct engine_profile_image *image;

        engine_profile_slot_init(&slot);
        if (!play || engine_profile_slot_apply_document(&slot, doc, n) != 1) {
            expect(0, "radial view setup");
            free(play);
            free(doc);
            return;
        }
        image = engine_profile_slot_adopt(&slot);
        engine_profile_pick(image, "game.exe", image->active_id, &live);
        engine_play_init(play);
        engine_play_set_macros(play, live.macros, live.macro_count);
        engine_radial_init(&st);
        engine_fingers_clear(&fingers);
        engine_radial_poll(&st, play, &live, &fingers, "game.exe", "", 1);
        engine_fingers_set(&fingers, 0x54, 1);
        engine_radial_poll(&st, play, &live, &fingers, "game.exe", "", 1);
        engine_radial_fill_view(&st, &live, &view);
        expect(view.open && view.mode == ENGINE_RADIAL_KEYS && view.slot_count == 4 && view.lit == -1,
               "radial view: open, keys, 4 slots, nothing lit");
        expect(strcmp(view.id, "rk") == 0 && strcmp(view.name, "Keys menu") == 0 && strcmp(view.prev, "q") == 0 &&
                   strcmp(view.next, "e") == 0,
               "radial view: the menu and its step keys");
        expect(strcmp(view.labels[0], "Strat A") == 0 && strcmp(view.labels[1], "Strat B") == 0 &&
                   strcmp(view.labels[2], "Held C") == 0 && view.labels[3][0] == 0,
               "radial view: each slot is its macro's name, an empty slot is blank");
        expect(view.size == ENGINE_RADIAL_SIZE && view.opacity == ENGINE_RADIAL_OPACITY,
               "radial view: default size and opacity");
        engine_fingers_set(&fingers, 0x32, 1);
        engine_radial_poll(&st, play, &live, &fingers, "game.exe", "", 1);
        engine_radial_fill_view(&st, &live, &view);
        expect(view.open && view.lit == 1, "radial view: 2 lights slot 2");
        engine_fingers_set(&fingers, 0x54, 0);
        engine_radial_poll(&st, play, &live, &fingers, "game.exe", "", 1);
        engine_radial_fill_view(&st, &live, &view);
        expect(!view.open && view.why == ENGINE_RADIAL_WHY_PLAYED && view.slot_count == 0,
               "radial view: closed after the pick, with the reason");
        engine_profile_slot_shutdown(&slot);
        free(play);
    }
    {
        static const char sized[] =
            "{\"version\":1,\"name\":\"R\",\"activeId\":\"c\",\"configs\":[{\"id\":\"c\",\"name\":\"C\",\"macros\":[],"
            "\"radials\":[{\"id\":\"a\",\"size\":90,\"opacity\":250},{\"id\":\"b\",\"size\":512,\"opacity\":61}]}],\"macros\":[]}";
        struct engine_profile_slot slot;
        struct engine_live_config live;
        const struct engine_profile_image *image;

        engine_profile_slot_init(&slot);
        engine_profile_slot_apply_document(&slot, sized, sizeof sized - 1);
        image = engine_profile_slot_adopt(&slot);
        engine_profile_pick(image, "game.exe", image ? image->active_id : "", &live);
        expect(live.radial_count == 2 && live.radials[0].size == ENGINE_RADIAL_SIZE_MIN &&
                   live.radials[0].opacity == ENGINE_RADIAL_OPACITY_MAX,
               "radial: size and opacity are clamped");
        expect(live.radial_count == 2 && live.radials[1].size == 512 && live.radials[1].opacity == 61,
               "radial: size and opacity in range are kept");
        engine_profile_slot_shutdown(&slot);
    }
    free(doc);
}

int main(void)
{
    check_frames();
    check_ring();
    check_actions();
    check_poll();
    check_state();
    check_swallow();
    check_saved_profile();
    check_once_lock();
    check_mouse_triggers();
    check_recover();
    check_held_again();
    check_run_once_held();
    check_held_press();
    check_seed_before_drain();
    check_many_starts();
    check_mute_compaction();
    check_mute_switch();
    check_report_queue();
    check_profile_reseed();
    check_forget_triggers();
    check_notes();
    check_create_paths();
    check_hold_repress();
    check_radial();
    check_radial_view();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
