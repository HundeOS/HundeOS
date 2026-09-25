#ifndef __USER_COMMANDS_RM_PLATFORM_H
#define __USER_COMMANDS_RM_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

int OpenBSDRm__main(int argc, char ** argv);
void RmPlatform__unsupported_random(void * buffer, size_t size);

#endif
