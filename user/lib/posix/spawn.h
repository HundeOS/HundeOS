#ifndef __USER_LIB_POSIX_SPAWN_H
#define __USER_LIB_POSIX_SPAWN_H

#include "../sys/spawn.h"

SpawnArguments POSIX__spawn_arguments_after_argv0(char * const argv[]); // hundelint: ignore-no-raw-char-buffer
Result(SpawnResult) POSIX__spawnv(SpawnMode mode, const char * path, char * const argv[]); // hundelint: ignore-no-raw-char-buffer

#endif
