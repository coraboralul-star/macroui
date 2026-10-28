#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "command.h"
#include "profile.h"
#include "report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static int fails;

static void expect(int cond, const char *name)
{
    if (!cond) {
        fprintf(stderr, "fail %s\n", name);
        fails++;
    }
}

static int note_has(const struct engine_profile_image *image, int code)
{
    uint32_t i;

    if (!image)
        return 0;
    for (i = 0; i < image->note_count; i++) {
        if (image->notes[i].code == code)
            return 1;
    }
    return 0;
}

static struct engine_profile_image *compile(const char *text)
{
    struct engine_profile_image *image = NULL;

    expect(engine_profile_compile(text, strlen(text), &image) == 0, "compile accepts the document");
    return image;
}

static void temp_path(char *dst, size_t cap)
{
#ifdef _WIN32
    char dir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof dir, dir);

    if (n == 0 || n >= sizeof dir)
        snprintf(dst, cap, "macroui-profile-47.json");
    else
        snprintf(dst, cap, "%smacroui-profile-47.json", dir);
#else
    snprintf(dst, cap, "/tmp/macroui-profile-47.json");
#endif
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (!file)
        return 0;
    fclose(file);
    return 1;
}

static uint16_t key_z(void)
{
    uint16_t id = 0;

    engine_key_id("z", &id);
    return id;
}

static void check_values(void)
{
    struct json_value root;
    double number = 0;
    const char *text =
        "{\"bad\":\"abc\",\"n\":\"12\",\"whole\":3,\"flag\":true,\"off\":false,\"empty\":null,"
        "\"word\":\" true \",\"one\":\"1\",\"no\":\"false\",\"a\":1,\"a\":2}";

    expect(json_parse(text, strlen(text), &root) == 0, "json object");
    expect(engine_as_num(json_object_get(&root, "bad"), 7, &number) == 1 && number == 7,
           "bad number falls back");
    expect(engine_as_num(json_object_get(&root, "n"), 0, &number) == 0 && number == 12,
           "numeric string parses");
    expect(engine_as_num(json_object_get(&root, "whole"), 0, &number) == 0 && number == 3,
           "number stays a number");
    expect(engine_as_num(json_object_get(&root, "empty"), 4, &number) == 0 && number == 4,
           "null uses the fallback");
    expect(engine_as_num(json_object_get(&root, "missing"), 4, &number) == 0 && number == 4,
           "missing field uses the fallback");
    expect(engine_as_bool(json_object_get(&root, "flag")) == 1, "true is true");
    expect(engine_as_bool(json_object_get(&root, "off")) == 0, "false is false");
    expect(engine_as_bool(json_object_get(&root, "word")) == 1, "true string is trimmed");
    expect(engine_as_bool(json_object_get(&root, "one")) == 1, "string 1 is true");
    expect(engine_as_bool(json_object_get(&root, "no")) == 0, "false string is false");
    expect(engine_as_num(json_object_get(&root, "a"), 0, &number) == 0 && number == 2,
           "duplicate key keeps the last value");
    json_free(&root);

    expect(engine_focus_match("Dawnwalker.exe", "Dawnwalker-Win64-Shipping.exe"), "shipping stem matches");
    expect(engine_focus_match("Dawnwalker-Win64-Shipping.exe", "Dawnwalker.exe"), "editor stem matches");
    expect(!engine_focus_match("notepad.exe", "notepad++.exe"), "notepad does not match notepad++");
    expect(!engine_focus_match("", "Game.exe"), "empty focus does not match");
}

static void check_steps(void)
{
    struct engine_profile_image *image;
    struct engine_live_config live;
    const struct engine_macro *macro;
    const struct engine_step *step;
    struct engine_play *play;

    image = compile("{\"activeId\":\"default\",\"macros\":[{\"id\":\"a\",\"steps\":["
                    "{\"type\":\"key\",\"action\":\"down\",\"key\":\"b\"},"
                    "{\"type\":\"wait\",\"ms\":1.6},"
                    "{\"type\":\"move\",\"x\":4,\"y\":-3},"
                    "{\"type\":\"scroll\",\"dy\":1}"
                    "]}]}");
    if (!image)
        return;
    engine_profile_pick(image, "", "default", &live);
    expect(strcmp(live.live_id, "-") == 0, "no configs keeps the flat list");
    expect(strcmp(live.active_id, "default") == 0, "flat active id is the file id");
    expect(live.macro_count == 1, "one flat macro");
    macro = live.macros;
    expect(macro && macro->step_count == 4 && macro->top_count == 4, "four top steps");
    step = macro->steps;
    expect(step[0].type == ENGINE_STEP_KEY && step[0].action == ENGINE_ACT_DOWN && step[0].key == 0x42,
           "key down uses the virtual key");
    expect(step[1].type == ENGINE_STEP_WAIT && step[1].a == 2, "fractional wait rounds half away");
    expect(step[2].type == ENGINE_STEP_MOVE && step[2].a == 4 && step[2].b == -3, "move keeps the point");
    expect(step[3].type == ENGINE_STEP_SCROLL && step[3].a == 1, "scroll keeps dy");

    play = (struct engine_play *)calloc(1, sizeof *play);
    expect(play != NULL, "play storage");
    if (play) {
        engine_play_init(play);
        engine_play_set_macros(play, live.macros, live.macro_count);
        expect(engine_play_start(play, &live.macros[0], -1, 1) == 1, "compiled macro starts");
        engine_play_tick(play, 0);
        engine_play_tick(play, 2);
        expect(engine_report_dx(&play->last) == 4 && engine_report_dy(&play->last) == -3 &&
                   engine_report_wheel(&play->last) == 1,
               "compiled move and scroll publish");
        free(play);
    }
    engine_profile_image_free(image);

    image = compile("{\"macros\":[{\"id\":\"empty\"}]}");
    if (image) {
        expect(image->flat.count == 1 && image->flat.macros[0].step_count == 0, "missing steps is empty");
        expect(!note_has(image, ENGINE_PROFILE_NOTE_UNKNOWN), "missing steps is not a failure");
        engine_profile_image_free(image);
    }

    image = compile("{\"macros\":[{\"id\":\"null-steps\",\"steps\":null}]}");
    if (image) {
        expect(image->flat.macros[0].step_count == 0, "null steps is empty");
        engine_profile_image_free(image);
    }
}

static void check_blocks(void)
{
    struct engine_profile_image *image;
    const struct engine_macro *macro;
    const struct engine_step *step;

    image = compile("{\"macros\":[{\"id\":\"hold\",\"advanced\":true,\"basic\":true,\"blocks\":["
                    "{\"type\":\"pressHold\",\"key\":\"b\"},"
                    "{\"type\":\"whileHeld\",\"steps\":[{\"type\":\"wait\",\"ms\":26}]},"
                    "{\"type\":\"swapAfter\",\"afterMs\":40,\"steps\":[{\"type\":\"wait\",\"ms\":1}]},"
                    "{\"type\":\"tapHold\",\"watch\":[\"t\"]},"
                    "{\"type\":\"ifShort\",\"steps\":[{\"type\":\"wait\",\"ms\":2}]}"
                    "]}]}");
    if (!image)
        return;
    macro = &image->flat.macros[0];
    expect(macro->advanced == 1 && macro->basic == 0, "blocks win over basic");
    /* pressHold expands to 4, whileHeld, swapAfter, tapHold, ifShort. */
    expect(macro->top_count == 8, "pressHold expands into the block list");
    step = macro->steps;
    expect(step[0].type == ENGINE_STEP_KEY && step[0].action == ENGINE_ACT_DOWN && step[0].key == 0x42,
           "pressHold key down");
    expect(step[1].type == ENGINE_STEP_WAIT && step[1].a == 18, "pressHold waits 18");
    expect(step[2].type == ENGINE_STEP_KEY && step[2].action == ENGINE_ACT_UP, "pressHold key up");
    expect(step[3].type == ENGINE_STEP_SCAN_WAIT && step[3].a == 200, "omitted holdMs is 200");
    expect(step[4].type == ENGINE_STEP_HOLD_LOOP && step[4].a == 40 && step[4].count == 1,
           "whileHeld takes the next swapAfter");
    expect(step[5].type == ENGINE_STEP_SWAP_LOOP && step[5].c == 40, "swapAfter stores afterMs");
    expect(step[6].type == ENGINE_STEP_EDGE_HOLD && step[6].key == key_z() && step[6].a == 5 &&
               step[6].b == 80,
           "tapHold defaults");
    expect(step[7].type == ENGINE_STEP_IF_SHORT && step[7].a == 150 && step[7].b == 3,
           "ifShort defaults");
    engine_profile_image_free(image);
}

static void check_swap_clicks(void)
{
    struct engine_profile_image *image;
    struct engine_report report;
    struct engine_send_op ops[4];
    int count = 0;
    const char *forms[] = {
        "{\"swapClicks\":true,\"macros\":[]}",
        "{\"swapClicks\":1,\"macros\":[]}",
        "{\"swapClicks\":\"true\",\"macros\":[]}",
        "{\"swapClicks\":\"1\",\"macros\":[]}",
        "{\"swapClicks\":\" true \",\"macros\":[]}"
    };
    int i;

    for (i = 0; i < 5; i++) {
        image = compile(forms[i]);
        if (!image)
            continue;
        expect(image->swap_clicks == 1, "swapClicks is on");
        engine_profile_image_free(image);
    }
    image = compile("{\"swapClicks\":false,\"macros\":[]}");
    if (image) {
        expect(image->swap_clicks == 0, "false leaves clicks alone");
        engine_profile_image_free(image);
    }
    image = compile("{\"macros\":[]}");
    if (image) {
        expect(image->swap_clicks == 0, "a missing swapClicks is off");
        engine_profile_image_free(image);
    }
    image = compile("{\"swapClicks\":true,\"macros\":[]}");
    if (!image)
        return;
    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_LBUTTON);
    expect(engine_report_send(&report, image->swap_clicks, ops, 4, &count) == 1, "swapped send");
    expect(count == 1 && ops[0].kind == ENGINE_SEND_BUTTON && ops[0].id == ENGINE_KEY_RBUTTON,
           "profile swap sends the right button");
    expect(engine_report_down(&report, 0) == ENGINE_KEY_LBUTTON, "profile swap leaves the image logical");
    engine_report_clear(&report);
    engine_report_push_down(&report, ENGINE_KEY_MBUTTON);
    engine_report_send(&report, image->swap_clicks, ops, 4, &count);
    expect(ops[0].id == ENGINE_KEY_MBUTTON, "profile swap leaves middle");
    engine_profile_image_free(image);
    image = compile("{\"swapClicks\":true,\"macros\":[{\"id\":\"m4\",\"playMode\":\"whileHeld\","
                    "\"trigger\":{\"kind\":\"side\",\"button\":\"XButton1\"},"
                    "\"steps\":[{\"type\":\"key\",\"action\":\"down\",\"key\":\"g\"}]}]}");
    if (image) {
        expect(image->swap_clicks == 1 && image->flat.count == 1 &&
                   image->flat.macros[0].trigger == ENGINE_KEY_XBUTTON1,
               "swap does not remap a mouse trigger");
        engine_profile_image_free(image);
    }
}

static void check_run_and_ignored(void)
{
    struct engine_profile_image *image;
    struct engine_profile_image *other;
    const struct engine_step *run_step;

    image = compile("{\"variables\":{\"hp\":1},\"macros\":["
                    "{\"id\":\"a\",\"priority\":9,\"steps\":[{\"type\":\"run\",\"macroId\":\"missing\"},"
                    "{\"type\":\"run\",\"macroId\":\"b\"},{\"type\":\"wait\",\"ms\":\"nope\"}]},"
                    "{\"id\":\"b\",\"priority\":1,\"steps\":[{\"type\":\"run\",\"macroId\":\"a\"}]}"
                    "]}");
    if (!image)
        return;
    expect(image->has_variables == 1, "variables is stored");
    expect(image->flat.extras[0].has_priority == 1 && image->flat.extras[0].priority == 9,
           "priority is stored");
    expect(image->flat.extras[0].enabled == 1, "enabled defaults true");
    run_step = image->flat.macros[0].steps;
    expect(run_step[0].type == ENGINE_STEP_RUN && run_step[0].a == -1, "missing run target is a no-op");
    expect(run_step[1].a == 1, "run stores the catalog index");
    expect(image->flat.macros[1].steps[0].a == 0, "a later macro can run an earlier one");
    expect(note_has(image, ENGINE_PROFILE_NOTE_MISSING_RUN), "missing run is reported");
    expect(note_has(image, ENGINE_PROFILE_NOTE_BAD_NUMBER), "bad wait is reported");
    expect(image->flat.count == 2, "notes do not reject the profile");
    other = compile("{\"macros\":["
                    "{\"id\":\"a\",\"priority\":1,\"steps\":[{\"type\":\"run\",\"macroId\":\"missing\"},"
                    "{\"type\":\"run\",\"macroId\":\"b\"},{\"type\":\"wait\",\"ms\":0}]},"
                    "{\"id\":\"b\",\"steps\":[{\"type\":\"run\",\"macroId\":\"a\"}]}"
                    "]}");
    if (other) {
        expect(other->flat.macros[0].step_count == image->flat.macros[0].step_count,
               "priority does not change the steps");
        expect(other->flat.macros[0].steps[1].a == image->flat.macros[0].steps[1].a,
               "priority does not change run");
        engine_profile_image_free(other);
    }
    engine_profile_image_free(image);
}

static void check_live(void)
{
    struct engine_profile_image *image;
    struct engine_live_config live;
    const char *text =
        "{\"activeId\":\"home\",\"macros\":[{\"id\":\"flat\",\"steps\":[{\"type\":\"wait\",\"ms\":1}]}],"
        "\"configs\":["
        "{\"id\":\"home\",\"name\":\"Home\",\"focusExe\":\"\",\"macros\":[{\"id\":\"a\",\"trigger\":{\"kind\":\"key\",\"button\":\"b\"},"
        "\"steps\":[]}]},"
        "{\"id\":\"game\",\"name\":\"Game\",\"focusExe\":\"Game.exe\",\"macros\":[{\"id\":\"g\",\"steps\":[{\"type\":\"wait\",\"ms\":2}]}]},"
        "{\"id\":\"second\",\"focusExe\":\"\",\"macros\":[{\"id\":\"s\",\"steps\":[]}]}"
        "]}";

    image = compile(text);
    if (!image)
        return;
    expect(image->has_configs == 1 && image->flat.count == 0, "configs replace the flat list");
    engine_profile_pick(image, "notepad.exe", "home", &live);
    expect(strcmp(live.live_id, "home") == 0 && live.macro_count == 1, "foreground miss uses empty focus");
    expect(strcmp(live.macros[0].id, "a") == 0, "fallback macros are that config");
    expect(live.trigger_head && live.trigger_head[0x42] == 1, "trigger chain starts at the first macro");

    engine_profile_pick(image, "Game.exe", "home", &live);
    expect(strcmp(live.live_id, "game") == 0 && strcmp(live.active_id, "game") == 0,
           "foreground exe picks that config");
    expect(strcmp(live.name, "Game") == 0, "the chosen config keeps its name");
    expect(strcmp(live.prefer, "home") == 0, "prefer stays the activeId that was passed");

    engine_profile_pick(image, "c:\\\\games\\\\GAME.EXE", "home", &live);
    expect(strcmp(live.live_id, "game") == 0, "focusExe matches the file name, ignoring case and directory");

    engine_profile_pick(image, "Game-Win64-Shipping.exe", "home", &live);
    expect(strcmp(live.live_id, "game") == 0, "shipping exe still matches the stem");

    engine_profile_pick(image, "notepad.exe", "second", &live);
    expect(strcmp(live.live_id, "second") == 0, "activeId breaks the empty-focus tie");

    engine_profile_image_free(image);

    image = compile("{\"activeId\":\"game\",\"configs\":[{\"id\":\"game\",\"focusExe\":\"Game.exe\",\"macros\":[]}]}");
    if (image) {
        engine_profile_pick(image, "notepad.exe", "game", &live);
        expect(live.macro_count == 0 && live.live_id[0] == '\0' && live.active_id[0] == '\0',
               "no fallback means nothing runs");
        expect(strcmp(live.prefer, "game") == 0, "a miss leaves prefer unchanged");
        engine_profile_image_free(image);
    }
}

static void check_swap_and_file(void)
{
    struct engine_profile_slot slot;
    struct engine_state_slot state;
    struct engine_state_snapshot snap;
    const struct engine_profile_image *adopted;
    struct engine_profile_image *first = NULL;
    struct engine_profile_image *second = NULL;
    char path[512];
    char gone[512];
    FILE *file;
    int had_default;

    engine_profile_slot_init(&slot);
    expect(engine_profile_compile("{\"name\":\"One\",\"macros\":[]}", 26, &first) == 0, "first image");
    expect(engine_profile_compile("{\"name\":\"Two\",\"macros\":[]}", 26, &second) == 0, "second image");
    if (!first || !second) {
        engine_profile_image_free(first);
        engine_profile_image_free(second);
        engine_profile_slot_shutdown(&slot);
        return;
    }
    engine_profile_slot_publish(&slot, first);
    adopted = engine_profile_slot_adopt(&slot);
    expect(adopted == first && adopted->sequence == 1, "adopt takes the published image");
    engine_profile_slot_publish(&slot, second);
    expect(first->name[0] == 'O', "the previous image stays until a newer adopt");
    adopted = engine_profile_slot_adopt(&slot);
    expect(adopted == second && strcmp(adopted->name, "Two") == 0, "adopt swaps to the newer image");
    engine_profile_slot_shutdown(&slot);

    engine_profile_slot_init(&slot);
    {
        const char *kept = "{\"name\":\"Kept\",\"macros\":[{\"id\":\"a\",\"steps\":[]}]}";

        expect(engine_profile_slot_load_text(&slot, kept, strlen(kept)) == 0, "text load publishes");
    }
    adopted = engine_profile_slot_adopt(&slot);
    expect(adopted && adopted->flat.count == 1, "loaded macro is kept");
    expect(engine_profile_slot_load_text(&slot, "{", 1) == -1, "corrupt text does not throw");
    expect(strcmp(engine_profile_slot_detail(&slot), ENGINE_PROFILE_READ_DETAIL) == 0,
           "corrupt detail is the profile-read string");
    adopted = engine_profile_slot_adopt(&slot);
    expect(adopted && adopted->flat.count == 1 && strcmp(adopted->name, "Kept") == 0,
           "corrupt text keeps the last good profile");
    engine_profile_slot_shutdown(&slot);

    engine_profile_slot_init(&slot);
    temp_path(path, sizeof path);
    remove(path);
    expect(engine_profile_slot_load_file(&slot, path) == 0, "missing file loads");
    expect(!file_exists(path), "missing file is not created");
    adopted = engine_profile_slot_adopt(&slot);
    expect(adopted && strcmp(adopted->name, "Default") == 0 && adopted->flat.count == 0 &&
               adopted->has_variables == 1 && engine_profile_slot_detail(&slot)[0] == '\0',
           "missing file is the empty document");
    file = fopen(path, "wb");
    expect(file != NULL, "temp profile opens");
    if (file) {
        fputs("{", file);
        fclose(file);
    }
    expect(engine_profile_slot_load_file(&slot, path) == -1, "corrupt file does not throw");
    adopted = engine_profile_slot_adopt(&slot);
    expect(adopted && strcmp(adopted->name, "Default") == 0, "corrupt file keeps the last profile");
    expect(strcmp(engine_profile_slot_detail(&slot), "the saved profile could not be read") == 0,
           "file detail matches");
    remove(path);
    engine_profile_slot_shutdown(&slot);

    snprintf(gone, sizeof gone, "profiles/default.json");
    had_default = file_exists(gone);
    engine_profile_slot_init(&slot);
    engine_profile_slot_load_missing(&slot);
    engine_profile_slot_shutdown(&slot);
    expect(file_exists(gone) == had_default, "default json is not armed");

    engine_state_slot_init(&state);
    engine_state_read(&state, &snap);
    expect(snap.active_id[0] == '\0' && snap.armed == 0, "state starts empty");
    engine_state_publish(&state, 0, "game", "Game.exe");
    engine_state_read(&state, &snap);
    expect(strcmp(snap.active_id, "game") == 0 && strcmp(snap.front, "Game.exe") == 0 && snap.armed == 0,
           "active id is on the snapshot");
}

static void check_empty_trigger(void)
{
    struct engine_profile_image *image;
    struct engine_live_config live;
    struct engine_play *play;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    uint16_t id = 1;
    int slot;

    expect(engine_key_id("", &id) == 0 && id == 0, "an empty name is not a key");
    expect(engine_key_id("XButton2", &id) == 1 && id == ENGINE_KEY_XBUTTON2, "XButton2 name");
    expect(engine_key_id("xbutton2", &id) == 1 && id == ENGINE_KEY_XBUTTON2, "xbutton2 name");
    expect(engine_key_id("xbutton1", &id) == 1 && id == ENGINE_KEY_XBUTTON1, "xbutton1 name");

    image = compile("{\"activeId\":\"ow\",\"configs\":[{\"id\":\"ow\",\"name\":\"Overwatch\","
                    "\"focusExe\":\"Overwatch.exe\",\"macros\":["
                    "{\"id\":\"blank\",\"enabled\":true,\"playMode\":\"whileHeld\","
                    "\"trigger\":{\"kind\":\"key\",\"button\":\"\"},"
                    "\"steps\":[{\"type\":\"key\",\"action\":\"tap\",\"key\":\"q\"}]},"
                    "{\"id\":\"blank-side\",\"enabled\":true,\"playMode\":\"once\","
                    "\"trigger\":{\"kind\":\"side\",\"button\":\"\"},"
                    "\"steps\":[{\"type\":\"key\",\"action\":\"tap\",\"key\":\"q\"}]},"
                    "{\"id\":\"shoot\",\"enabled\":true,\"playMode\":\"whileHeld\","
                    "\"trigger\":{\"kind\":\"side\",\"button\":\"XButton2\"},"
                    "\"steps\":[{\"type\":\"key\",\"action\":\"tap\",\"key\":\"q\"}]}"
                    "]}]}");
    if (!image)
        return;
    engine_profile_pick(image, "Overwatch.exe", "ow", &live);
    expect(live.macro_count == 3, "empty triggers compile beside mouse 5");
    expect(live.macros[0].trigger == 0 && live.macros[1].trigger == 0,
           "an empty button compiles to no trigger");
    expect(live.macros[2].trigger == ENGINE_KEY_XBUTTON2, "shoot compiles to mouse 5");
    expect(live.trigger_head != NULL && live.trigger_head[0] == 0, "trigger 0 is not a chain");
    expect(live.trigger_head[ENGINE_KEY_XBUTTON2] != 0, "mouse 5 is in the chain");
    for (slot = 0; slot <= (int)ENGINE_KEY_ID_MAX; slot++) {
        uint32_t head = live.trigger_head[slot];
        int guard = 0;

        while (head != 0 && guard < live.macro_count) {
            expect(head != 1 && head != 2, "an empty macro is not in any chain");
            if (head > (uint32_t)live.macro_count)
                break;
            head = live.trigger_next[head - 1u];
            guard++;
        }
    }
    expect(engine_live_needs_mouse(&live) == 1, "mouse 5 needs the mouse hook");

    play = (struct engine_play *)calloc(1, sizeof *play);
    expect(play != NULL, "empty trigger play");
    if (play) {
        engine_play_init(play);
        engine_trigger_init(&mem);
        engine_fingers_clear(&fingers);
        engine_poll_triggers(play, &live, &mem, &fingers, "Overwatch.exe", "");
        engine_fingers_set(&fingers, (uint16_t)'e', 1);
        engine_fingers_set(&fingers, (uint16_t)'r', 1);
        engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON1, 1);
        engine_fingers_set(&fingers, ENGINE_KEY_XBUTTON2, 1);
        engine_poll_triggers(play, &live, &mem, &fingers, "Overwatch.exe", "");
        expect(play->pending_count == 1, "mouse 5 starts and an empty trigger does not");
        engine_play_flush(play);
        expect(play->runner_count == 1 && strcmp(play->runners[0].id, "shoot") == 0,
               "the runner is shoot");
        free(play);
    }
    engine_profile_image_free(image);

    image = compile("{\"macros\":[{\"id\":\"blank\",\"enabled\":true,\"playMode\":\"whileHeld\","
                    "\"trigger\":{\"kind\":\"key\",\"button\":\"\"},"
                    "\"steps\":[{\"type\":\"key\",\"action\":\"tap\",\"key\":\"q\"}]}]}");
    if (!image)
        return;
    engine_profile_pick(image, "Overwatch.exe", "", &live);
    expect(live.macros[0].trigger == 0, "a lone empty button is not a trigger");
    expect(!engine_live_needs_mouse(&live), "an empty trigger does not need the mouse hook");
    engine_profile_image_free(image);
}

static void check_named_keys(void)
{
    struct engine_profile_image *image;
    struct engine_live_config live;
    struct engine_play *play;
    struct engine_trigger_mem mem;
    struct engine_fingers fingers;
    uint16_t id = 0;

    expect(engine_key_id("e", &id) == 1 && id == 0x45, "e is VK_E");
    expect(engine_key_id("E", &id) == 1 && id == 0x45, "E is VK_E");
    expect(engine_key_name(0x45) && strcmp(engine_key_name(0x45), "e") == 0, "held name of e");
    expect(engine_key_id("CapsLock", &id) == 1 && id == 0x14, "CapsLock is VK_CAPITAL");
    expect(engine_key_id("capslock", &id) == 1 && id == 0x14, "capslock alias");
    expect(engine_key_id("\\", &id) == 1 && id == 0xDC, "backslash is VK_OEM_5");
    expect(engine_key_id("|", &id) == 1 && id == 0xDC, "pipe is the backslash key");
    expect(engine_key_name(0x14) && strcmp(engine_key_name(0x14), "CapsLock") == 0, "CapsLock name");
    expect(engine_key_name(0xDC) && strcmp(engine_key_name(0xDC), "\\") == 0, "backslash name");

    image = compile("{\"activeId\":\"som\",\"configs\":[{\"id\":\"som\",\"name\":\"SOM Test\","
                    "\"focusExe\":\"ShadowOfMordor.exe\",\"macros\":["
                    "{\"id\":\"space\",\"enabled\":true,\"playMode\":\"once\",\"advanced\":true,"
                    "\"trigger\":{\"kind\":\"key\",\"button\":\"space\"},"
                    "\"blocks\":[{\"type\":\"whileHeld\",\"steps\":["
                    "{\"type\":\"key\",\"action\":\"down\",\"key\":\"space\"},"
                    "{\"type\":\"wait\",\"ms\":350},"
                    "{\"type\":\"key\",\"action\":\"up\",\"key\":\"space\"}]}]},"
                    "{\"id\":\"m5\",\"enabled\":true,\"playMode\":\"once\",\"advanced\":true,"
                    "\"trigger\":{\"kind\":\"side\",\"button\":\"XButton2\"},"
                    "\"blocks\":[{\"type\":\"whileHeld\",\"steps\":["
                    "{\"type\":\"key\",\"action\":\"tap\",\"key\":\"t\"}]}]},"
                    "{\"id\":\"e-mac\",\"enabled\":true,\"playMode\":\"once\",\"advanced\":true,"
                    "\"trigger\":{\"kind\":\"key\",\"button\":\"e\"},"
                    "\"blocks\":[{\"type\":\"whileHeld\",\"steps\":["
                    "{\"type\":\"key\",\"action\":\"down\",\"key\":\"e\"},"
                    "{\"type\":\"wait\",\"ms\":18},"
                    "{\"type\":\"key\",\"action\":\"up\",\"key\":\"e\"}]}]},"
                    "{\"id\":\"caps\",\"enabled\":true,\"playMode\":\"whileHeld\",\"advanced\":true,"
                    "\"trigger\":{\"kind\":\"key\",\"button\":\"CapsLock\"},"
                    "\"blocks\":[{\"type\":\"whileHeld\",\"steps\":["
                    "{\"type\":\"key\",\"action\":\"tap\",\"key\":\"q\"}]}]},"
                    "{\"id\":\"menu\",\"enabled\":true,\"playMode\":\"once\","
                    "\"trigger\":{\"kind\":\"key\",\"button\":\"\\\\\"},"
                    "\"steps\":[{\"type\":\"key\",\"action\":\"tap\",\"key\":\"q\"}]}"
                    "]}]}");
    if (!image)
        return;
    engine_profile_pick(image, "ShadowOfMordor.exe", "som", &live);
    expect(live.macro_count == 5, "SOM shaped catalog");
    expect(live.macros[2].trigger == 0x45, "the e macro triggers on VK_E");
    expect(live.macros[1].trigger == ENGINE_KEY_XBUTTON2, "M5 triggers on XButton2");
    expect(live.macros[3].trigger == 0x14, "CapsLock compiles");
    expect(live.macros[4].trigger == 0xDC, "backslash compiles");
    expect(engine_live_needs_mouse(&live) == 1, "SOM Test needs the mouse hook");
    expect(engine_live_claims(&live, 0x45) == 1, "e matches a macro");
    expect(engine_live_claims(&live, (uint16_t)'w') == 0, "w matches nothing");
    expect(engine_live_claims(&live, 0x30) == 0, "digit 0 matches nothing");

    play = (struct engine_play *)calloc(1, sizeof *play);
    expect(play != NULL, "SOM play");
    if (play) {
        engine_play_init(play);
        engine_play_set_macros(play, live.macros, live.macro_count);
        engine_trigger_init(&mem);
        engine_fingers_clear(&fingers);
        engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
        engine_fingers_set(&fingers, 0x45, 1);
        engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
        engine_play_flush(play);
        expect(play->runner_count == 1 && strcmp(play->runners[0].id, "e-mac") == 0,
               "e starts the once macro");
        play->runners[0].done = 1;
        engine_poll_triggers(play, &live, &mem, &fingers, "ShadowOfMordor.exe", "");
        expect(play->pending_count == 0, "e waits for a new down edge, like AHK");
        free(play);
    }
    engine_profile_image_free(image);
}

static const struct engine_step *turn_step(const struct engine_live_config *live)
{
    if (live->macro_count < 1 || live->macros[0].step_count < 1)
        return NULL;
    return &live->macros[0].steps[0];
}

static void check_turn(void)
{
    struct engine_profile_image *image;
    struct engine_live_config live;
    const struct engine_step *step;
    const char *text =
        "{\"activeId\":\"a\",\"configs\":["
        "{\"id\":\"a\",\"focusExe\":\"A.exe\",\"turn\":{\"countsPer360\":16364},\"macros\":[{\"id\":\"m\","
        "\"steps\":[{\"type\":\"turn\",\"degrees\":180,\"dir\":\"left\",\"ms\":120},"
        "{\"type\":\"turn\",\"degrees\":90.5,\"dir\":\"right\"},{\"type\":\"turn\"}]}]},"
        "{\"id\":\"b\",\"focusExe\":\"B.exe\",\"macros\":[{\"id\":\"n\",\"steps\":[{\"type\":\"turn\",\"degrees\":180}]}]},"
        "{\"id\":\"c\",\"focusExe\":\"C.exe\",\"turn\":{\"countsPer360\":-5},\"macros\":[{\"id\":\"o\",\"steps\":[{\"type\":\"turn\"}]}]}"
        "]}";

    image = compile(text);
    if (!image)
        return;
    engine_profile_pick(image, "A.exe", "a", &live);
    step = turn_step(&live);
    expect(step && step->type == ENGINE_STEP_TURN && step->a == -8182 && step->b == 120 && step->action == 0,
           "turn left 180 is half the config's counts, negative");
    expect(step && step[1].a == 4114 && step[1].b == ENGINE_TURN_MS_DEFAULT,
           "turn rounds the counts and defaults to 30 ms");
    expect(step && step[2].a == 8182 && step[2].action == 0, "turn defaults to 180 right");
    engine_profile_pick(image, "B.exe", "a", &live);
    step = turn_step(&live);
    expect(step && step->a == 0 && (step->action & ENGINE_TURN_UNCALIBRATED), "a config with no turn is uncalibrated");
    engine_profile_pick(image, "C.exe", "a", &live);
    step = turn_step(&live);
    expect(step && (step->action & ENGINE_TURN_UNCALIBRATED), "a negative countsPer360 is uncalibrated");
    engine_profile_image_free(image);

    image = compile("{\"turn\":{\"countsPer360\":3600},\"macros\":[{\"id\":\"f\",\"steps\":[{\"type\":\"turn\",\"degrees\":360,\"ms\":0}]}]}");
    if (image) {
        engine_profile_pick(image, "x.exe", "", &live);
        step = turn_step(&live);
        expect(step && step->a == 3600 && step->b == 0 && step->action == 0, "a profile with no configs uses its own turn");
        engine_profile_image_free(image);
    }
}

int main(void)
{
    check_values();
    check_empty_trigger();
    check_named_keys();
    check_steps();
    check_blocks();
    check_swap_clicks();
    check_run_and_ignored();
    check_live();
    check_swap_and_file();
    check_turn();
    if (fails) {
        fprintf(stderr, "%d profile checks failed\n", fails);
        return 1;
    }
    return 0;
}
