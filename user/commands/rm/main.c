#include <stdio.h>
#include <string.h>

#include "platform.h"

static bool RmPlatform__secure_overwrite_requested(int argc, char ** argv)
{
    for (int index = 1; index < argc; index++)
    {
        if (strcmp(argv[index], "--") == 0 || argv[index][0] != '-' || strlen(argv[index]) == 1)
        {
            break;
        }
        if (strcspn(argv[index] + 1, "P") < strlen(argv[index] + 1))
        {
            return true;
        }
    }
    return false;
}

int main(int argc, char ** argv)
{
    if (RmPlatform__secure_overwrite_requested(argc, argv))
    {
        fprintf(stderr, "rm: -P is not supported\n");
        return 1;
    }
    return OpenBSDRm__main(argc, argv);
}

void RmPlatform__unsupported_random(void * buffer, size_t size)
{
    fprintf(stderr, "rm: secure overwrite is not supported\n");
    _exit(1);
}
