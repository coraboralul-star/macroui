#include "process.h"

#include <string.h>

/* command-quit in docs/engine/ipc-vectors.txt. */
static const unsigned char k_quit[] = {
    0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04
};

/* ack-command. ok, which command, empty error. No id. */
static const unsigned char k_ack_command[] = {
    0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00
};

/* ack-bad-frame. */
static const unsigned char k_ack_bad_frame[] = {
    0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x03, 0x00, 0x0D, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x00, 0x62, 0x61, 0x64, 0x20, 0x66, 0x72, 0x61, 0x6D, 0x65
};

/* ack-bad-version. */
static const unsigned char k_ack_bad_version[] = {
    0x4D, 0x55, 0x49, 0x31, 0x01, 0x00, 0x03, 0x00, 0x0F, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0B, 0x00, 0x62, 0x61, 0x64, 0x20, 0x76, 0x65, 0x72, 0x73,
    0x69, 0x6F, 0x6E
};

static const unsigned char k_magic[] = { 0x4D, 0x55, 0x49, 0x31 };

static const int k_boot_resident[] = {
    ENGINE_BOOT_DPI, ENGINE_BOOT_MUTEX, ENGINE_BOOT_IPC, ENGINE_BOOT_HOOK,
    ENGINE_BOOT_PLAYBACK
};

static const int k_boot_early[] = { ENGINE_BOOT_DPI };

static const int k_exit_order[] = {
    ENGINE_EXIT_KEYS, ENGINE_EXIT_UNHOOK, ENGINE_EXIT_TIMER, ENGINE_EXIT_PIPE,
    ENGINE_EXIT_MUTEX
};

static int copy_steps(const int *src, int n, int *steps, int cap)
{
    int i;

    if (!steps || cap <= 0)
        return 0;
    if (n > cap)
        n = cap;
    for (i = 0; i < n; i++)
        steps[i] = src[i];
    return n;
}

static const unsigned char *bytes_of(const unsigned char *src, int n, int *length)
{
    if (length)
        *length = n;
    return src;
}

int engine_args_parse(int argc, char **argv, struct engine_args *out)
{
    int saw_self = 0;
    int saw_bench = 0;
    int bad = 0;
    int i;

    if (!out)
        return ENGINE_ARG_BAD;
    memset(out, 0, sizeof *out);
    if (argc < 0)
        argc = 0;
    for (i = 1; i < argc; i++) {
        const char *arg = argv ? argv[i] : NULL;

        if (!arg) {
            bad = 1;
            continue;
        }
        if (strcmp(arg, "--self-test") == 0) {
            saw_self = 1;
            continue;
        }
        if (strcmp(arg, "--bench") == 0) {
            saw_bench = 1;
            continue;
        }
        if (strcmp(arg, "--profile") == 0) {
            const char *path;
            size_t len;

            i++;
            path = (i < argc && argv) ? argv[i] : NULL;
            if (!path || path[0] == '\0' || path[0] == '-') {
                bad = 1;
                continue;
            }
            len = strlen(path);
            if (len >= ENGINE_PROFILE_CAP) {
                bad = 1;
                continue;
            }
            memcpy(out->profile, path, len + 1);
            continue;
        }
        bad = 1;
    }
    if (bad)
        out->mode = ENGINE_ARG_BAD;
    else if (saw_self)
        out->mode = ENGINE_ARG_SELF_TEST;
    else if (saw_bench)
        out->mode = ENGINE_ARG_BENCH;
    else
        out->mode = ENGINE_ARG_RESIDENT;
    return out->mode;
}

int engine_boot_steps(int mode, int *steps, int cap)
{
    if (mode == ENGINE_ARG_RESIDENT)
        return copy_steps(k_boot_resident, 5, steps, cap);
    return copy_steps(k_boot_early, 1, steps, cap);
}

int engine_exit_steps(int *steps, int cap)
{
    return copy_steps(k_exit_order, 5, steps, cap);
}

int engine_steps_equal(const int *got, int got_count, const int *want, int want_count)
{
    int i;

    if (got_count != want_count)
        return 0;
    if (got_count < 0)
        return 0;
    if (got_count == 0)
        return 1;
    if (!got || !want)
        return 0;
    for (i = 0; i < got_count; i++) {
        if (got[i] != want[i])
            return 0;
    }
    return 1;
}

int engine_pipe_kind(const unsigned char *msg, int length)
{
    unsigned int version;

    if (!msg || length <= 0)
        return ENGINE_PIPE_IGNORE;
    if (length == (int)sizeof k_quit && memcmp(msg, k_quit, sizeof k_quit) == 0)
        return ENGINE_PIPE_QUIT;
    if (msg[0] == 0x7B)
        return ENGINE_PIPE_JSON;
    if (length < 8 || memcmp(msg, k_magic, 4) != 0)
        return ENGINE_PIPE_IGNORE;
    version = (unsigned int)msg[4] | ((unsigned int)msg[5] << 8);
    if (version != 1)
        return ENGINE_PIPE_BAD_VERSION;
    return ENGINE_PIPE_IGNORE;
}

const unsigned char *engine_ack_command(int *length)
{
    return bytes_of(k_ack_command, (int)sizeof k_ack_command, length);
}

const unsigned char *engine_ack_bad_frame(int *length)
{
    return bytes_of(k_ack_bad_frame, (int)sizeof k_ack_bad_frame, length);
}

const unsigned char *engine_ack_bad_version(int *length)
{
    return bytes_of(k_ack_bad_version, (int)sizeof k_ack_bad_version, length);
}
