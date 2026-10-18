/* Startup decisions and the three fixed pipe replies.
   No Win32. The mutex, the threads, and the pipe call are src/win/win_entry.c.
   See docs/engine/architecture.md. */
#ifndef ENGINE_PROCESS_H
#define ENGINE_PROCESS_H

#define ENGINE_PROFILE_CAP 4096

#define ENGINE_EXIT_OK 0
#define ENGINE_EXIT_FAIL 1
/* The mutex already exists. The holder is left alone. */
#define ENGINE_EXIT_BUSY 2

enum engine_arg_mode {
    ENGINE_ARG_RESIDENT = 0,
    ENGINE_ARG_SELF_TEST = 1,
    ENGINE_ARG_BENCH = 2,
    ENGINE_ARG_BAD = 3
};

struct engine_args {
    int mode;
    char profile[ENGINE_PROFILE_CAP];
};

enum engine_boot_step {
    ENGINE_BOOT_DPI = 1,
    ENGINE_BOOT_MUTEX = 2,
    ENGINE_BOOT_IPC = 3,
    ENGINE_BOOT_HOOK = 4,
    ENGINE_BOOT_PLAYBACK = 5
};

enum engine_exit_step {
    ENGINE_EXIT_KEYS = 1,
    ENGINE_EXIT_UNHOOK = 2,
    ENGINE_EXIT_TIMER = 3,
    ENGINE_EXIT_PIPE = 4,
    ENGINE_EXIT_MUTEX = 5
};

enum engine_pipe_kind {
    ENGINE_PIPE_IGNORE = 0,
    ENGINE_PIPE_QUIT = 1,
    ENGINE_PIPE_JSON = 2,
    ENGINE_PIPE_BAD_VERSION = 3
};

/* argv[0] is the program name. --self-test wins over --bench.
   --profile stores the next argument and does not open it.
   A missing profile value, a value past the cap, or any other
   argument is ENGINE_ARG_BAD. The default is the resident process. */
int engine_args_parse(int argc, char **argv, struct engine_args *out);

/* Resident runs all five steps. Every other mode stops after DPI,
   which is before the mutex. Returns the number written. */
int engine_boot_steps(int mode, int *steps, int cap);

/* Release keys, unhook, timer, pipe, mutex. Returns 5. */
int engine_exit_steps(int *steps, int cap);

int engine_steps_equal(const int *got, int got_count, const int *want, int want_count);

/* Quit is the 13-byte command frame. A leading '{' is JSON.
   Magic MUI1 with a version other than 1 is a bad version.
   Anything else is ignored. Quit is checked first. */
int engine_pipe_kind(const unsigned char *msg, int length);

const unsigned char *engine_ack_command(int *length);
const unsigned char *engine_ack_profile(int *length);
const unsigned char *engine_ack_bad_frame(int *length);
const unsigned char *engine_ack_bad_version(int *length);
const unsigned char *engine_error_profile_read(int *length);

#endif
