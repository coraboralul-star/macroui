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

    play->output.mode = ENGINE_OUTPUT_RP2350;
    for (i = 0; i < 3; i++) {
        macro.trigger = triggers[i];
        engine_arm_swallow(&hooks, &live, "notepad.exe", NULL, 1, play);
        press_button(&hooks, triggers[i], 1, 0, 0, 50 + i, &decision);
        snprintf(label, sizeof label, "a board passes %s", trigger_names[i]);
        expect(decision.swallow == 0 && decision.enqueued == 1, label);
        engine_event_pop(&hooks, &ev);
    }

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
        uint16_t defer[4];
        int defer_count = 0;
        int i;

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
        defer_count = 0;
        expect(engine_finger_note(&fingers, ENGINE_KEY_LBUTTON, 1, defer, &defer_count, 4) == 0,
               "a second left click is applied");
        expect(engine_finger_note(&fingers, ENGINE_KEY_LBUTTON, 0, defer, &defer_count, 4) == 0 &&
                   defer_count == 0 && !engine_fingers_down(&fingers, ENGINE_KEY_LBUTTON),
               "the left click up clears a stuck left button");

        engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON2, 1);
        defer_count = 0;
        expect(engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, defer, &defer_count, 4) == 1,
               "a second mouse 5 down is deferred");
        expect(engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 0, defer, &defer_count, 4) == 0 &&
                   defer_count == 0 && !engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
               "mouse 5 up in the same batch cancels the defer");

        engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON2, 1);
        defer_count = 0;
        engine_finger_note(&fingers, ENGINE_KEY_XBUTTON2, 1, defer, &defer_count, 4);
        for (i = 0; i < defer_count; i++)
            engine_fingers_set(&fingers, defer[i], 1);
        expect(engine_fingers_down(&fingers, ENGINE_KEY_XBUTTON2),
               "mouse 5 stays down when the new press has no up yet");
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
    check_many_starts();
    check_mute_compaction();
    check_mute_switch();
    check_report_queue();
    check_profile_reseed();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
