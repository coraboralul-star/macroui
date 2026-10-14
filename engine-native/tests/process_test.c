#include "process.h"

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

static void check_args(void)
{
    struct engine_args args;
    char *none[] = { "macroui-engine" };
    char *self[] = { "macroui-engine", "--self-test" };
    char *bench[] = { "macroui-engine", "--bench" };
    char *both[] = { "macroui-engine", "--bench", "--self-test" };
    char *profile[] = { "macroui-engine", "--profile", "C:\\profiles\\default.json" };
    char *missing[] = { "macroui-engine", "--profile" };
    char *flag[] = { "macroui-engine", "--profile", "--self-test" };
    char *empty[] = { "macroui-engine", "--profile", "" };
    char *unknown[] = { "macroui-engine", "--nope" };
    char *last[] = { "macroui-engine", "--profile", "one", "--profile", "two" };
    char long_path[ENGINE_PROFILE_CAP + 8];
    char *too_long[3];

    expect(engine_args_parse(1, none, &args) == ENGINE_ARG_RESIDENT, "no args is resident");
    expect(args.profile[0] == '\0', "no relative default");

    expect(engine_args_parse(2, self, &args) == ENGINE_ARG_SELF_TEST, "self-test");
    expect(engine_args_parse(2, bench, &args) == ENGINE_ARG_BENCH, "bench");
    expect(engine_args_parse(3, both, &args) == ENGINE_ARG_SELF_TEST, "self-test wins");

    expect(engine_args_parse(3, profile, &args) == ENGINE_ARG_RESIDENT, "profile stays resident");
    expect(strcmp(args.profile, "C:\\profiles\\default.json") == 0, "profile path stored");

    expect(engine_args_parse(2, missing, &args) == ENGINE_ARG_BAD, "profile needs a value");
    expect(engine_args_parse(3, flag, &args) == ENGINE_ARG_BAD, "profile value is not a flag");
    expect(engine_args_parse(3, empty, &args) == ENGINE_ARG_BAD, "empty profile is bad");
    expect(engine_args_parse(2, unknown, &args) == ENGINE_ARG_BAD, "unknown is bad");

    expect(engine_args_parse(5, last, &args) == ENGINE_ARG_RESIDENT, "last profile wins");
    expect(strcmp(args.profile, "two") == 0, "last profile path");

    memset(long_path, 'a', sizeof long_path);
    long_path[sizeof long_path - 1] = '\0';
    too_long[0] = "macroui-engine";
    too_long[1] = "--profile";
    too_long[2] = long_path;
    expect(engine_args_parse(3, too_long, &args) == ENGINE_ARG_BAD, "profile past the cap");
    expect(args.profile[0] == '\0', "a long path is not truncated");

    expect(engine_args_parse(0, NULL, &args) == ENGINE_ARG_RESIDENT, "empty argv is resident");
    expect(engine_args_parse(1, none, NULL) == ENGINE_ARG_BAD, "null out");
}

static void check_order(void)
{
    int steps[8];
    int want_boot[] = {
        ENGINE_BOOT_DPI, ENGINE_BOOT_MUTEX, ENGINE_BOOT_IPC, ENGINE_BOOT_HOOK,
        ENGINE_BOOT_PLAYBACK
    };
    int want_early[] = { ENGINE_BOOT_DPI };
    int want_exit[] = {
        ENGINE_EXIT_KEYS, ENGINE_EXIT_UNHOOK, ENGINE_EXIT_TIMER, ENGINE_EXIT_PIPE,
        ENGINE_EXIT_MUTEX
    };
    int n;

    n = engine_boot_steps(ENGINE_ARG_RESIDENT, steps, 8);
    expect(engine_steps_equal(steps, n, want_boot, 5), "resident boot order");
    expect(steps[1] == ENGINE_BOOT_MUTEX, "mutex is step 2");

    n = engine_boot_steps(ENGINE_ARG_SELF_TEST, steps, 8);
    expect(engine_steps_equal(steps, n, want_early, 1), "self-test stops before the mutex");
    n = engine_boot_steps(ENGINE_ARG_BENCH, steps, 8);
    expect(engine_steps_equal(steps, n, want_early, 1), "bench stops before the mutex");
    n = engine_boot_steps(ENGINE_ARG_BAD, steps, 8);
    expect(engine_steps_equal(steps, n, want_early, 1), "bad args stop before the mutex");

    n = engine_exit_steps(steps, 8);
    expect(engine_steps_equal(steps, n, want_exit, 5), "exit order");
    expect(engine_steps_equal(steps, 4, want_exit, 5) == 0, "a short exit list does not match");
}

static void check_pipe(void)
{
    static const unsigned char quit[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04
    };
    static const unsigned char ack_command[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00,
        0x01, 0x01, 0x00, 0x00
    };
    static const unsigned char ack_frame[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x03, 0x00, 0x0D, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x09, 0x00, 0x62, 0x61, 0x64, 0x20, 0x66, 0x72, 0x61, 0x6D, 0x65
    };
    static const unsigned char ack_version[] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x03, 0x00, 0x0F, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x0B, 0x00, 0x62, 0x61, 0x64, 0x20, 0x76, 0x65, 0x72, 0x73,
        0x69, 0x6F, 0x6E
    };
    static const unsigned char json[] = {
        '{', '"', 'v', '"', ':', '1', ',', '"', 't', 'y', 'p', 'e', '"', ':',
        '"', 'c', 'o', 'm', 'm', 'a', 'n', 'd', '"', '}'
    };
    unsigned char bad_version[8] = {
        0x4D, 0x55, 0x49, 0x31, 0x02, 0x00, 0x08, 0x00
    };
    unsigned char start[13] = {
        0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01
    };
    const unsigned char *ack;
    int n = 0;

    expect(engine_pipe_kind(quit, (int)sizeof quit) == ENGINE_PIPE_QUIT, "quit frame");
    expect(engine_pipe_kind(json, (int)sizeof json) == ENGINE_PIPE_JSON, "json is not quit");
    expect(engine_pipe_kind(bad_version, 8) == ENGINE_PIPE_BAD_VERSION, "bad version");
    expect(engine_pipe_kind(start, 13) == ENGINE_PIPE_IGNORE, "start is not applied here");
    expect(engine_pipe_kind(quit, 12) == ENGINE_PIPE_IGNORE, "a short quit is not quit");
    expect(engine_pipe_kind(NULL, 13) == ENGINE_PIPE_IGNORE, "null message");

    ack = engine_ack_command(&n);
    expect(n == (int)sizeof ack_command && memcmp(ack, ack_command, sizeof ack_command) == 0,
           "ack command bytes");
    ack = engine_ack_bad_frame(&n);
    expect(n == (int)sizeof ack_frame && memcmp(ack, ack_frame, sizeof ack_frame) == 0,
           "ack bad frame bytes");
    ack = engine_ack_bad_version(&n);
    expect(n == (int)sizeof ack_version && memcmp(ack, ack_version, sizeof ack_version) == 0,
           "ack bad version bytes");
}

int main(void)
{
    check_args();
    check_order();
    check_pipe();
    if (fails) {
        fprintf(stderr, "%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
