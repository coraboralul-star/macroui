#include <stdio.h>

#include "version.h"

#if defined(_WIN32)
int engine_win_layer_present(void);
#endif

int main(void)
{
    if (printf("%s\n", engine_version()) < 0)
        return 1;
#if defined(_WIN32)
    if (engine_win_layer_present() != 1)
        return 1;
#endif
    return 0;
}
