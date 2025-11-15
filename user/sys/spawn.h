#ifndef __USER_SYS_SPAWN_H
#define __USER_SYS_SPAWN_H

#include "syscalls/spawn.h"

SpawnResult spawn(int mode, char* path, char* arg0, ...);


#endif