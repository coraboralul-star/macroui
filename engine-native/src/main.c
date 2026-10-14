#include <stdio.h>

#include "version.h"

#if defined(_WIN32)
#include "win_entry.h"
#endif

int main(int argc, char **argv)
{
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    return engine_win_main();
#else
    (void)argc;
    (void)argv;
    if (printf("%s\n", engine_version()) < 0)
        return 1;
    return 0;
#endif
}
