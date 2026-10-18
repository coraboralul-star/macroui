#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

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
        "{\"id\":\"home\",\"focusExe\":\"\",\"macros\":[{\"id\":\"a\",\"trigger\":{\"kind\":\"key\",\"button\":\"b\"},"
        "\"steps\":[]}]},"
        "{\"id\":\"game\",\"focusExe\":\"Game.exe\",\"macros\":[{\"id\":\"g\",\"steps\":[{\"type\":\"wait\",\"ms\":2}]}]},"
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
    expect(strcmp(live.prefer, "game") == 0, "a picked config becomes the next prefer");

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

int main(void)
{
    check_values();
    check_steps();
    check_blocks();
    check_swap_clicks();
    check_run_and_ignored();
    check_live();
    check_swap_and_file();
    if (fails) {
        fprintf(stderr, "%d profile checks failed\n", fails);
        return 1;
    }
    return 0;
}
