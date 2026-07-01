#include "../../sys/spawn.h"
#include "../../user.h"

#ifndef INIT_ENTRYPOINT
#define INIT_ENTRYPOINT main
#endif

static int Init__spawn_background(const char * path)
{
    SpawnResult result = spawn(P_NOWAIT, path, NULL, NULL, NULL);
    if (result < 0)
    {
        printf("init: failed to start %s: %i\n", path, (int) result);
        return 1;
    }
    return 0;
}

int INIT_ENTRYPOINT(void)
{
    int failures = 0;

    failures += Init__spawn_background("/ebin/virtio_blk");
    failures += Init__spawn_background("/ebin/uart_rx");
    failures += Init__spawn_background("/ebin/uart_tx");

    if (failures != 0)
    {
        return 1;
    }

    SpawnResult shell_result = spawn(P_WAIT, "/ebin/shell", NULL, NULL, NULL);
    if (shell_result < 0)
    {
        printf("init: failed to start /ebin/shell: %i\n", (int) shell_result);
        return 1;
    }

    return 0;
}
