#include "unsupported_random.h"

#include <stdio.h>
#include <unistd.h>

void arc4random_buf(void * buffer, size_t size)
{
    fprintf(stderr, "rm: -P is not supported\n");
    _exit(1);
}
