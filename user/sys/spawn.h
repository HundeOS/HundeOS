#ifndef __USER_SYS_SPAWN_H
#define __USER_SYS_SPAWN_H

#include "syscalls/spawn.h"

SpawnResult spawn(SpawnMode mode, const char* path, const char* arg0, const char* arg1, const char* arg2);


#endif
