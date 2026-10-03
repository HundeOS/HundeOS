#ifndef __USER_LIB_NEWLIB_CURRENT_DIRECTORY_H
#define __USER_LIB_NEWLIB_CURRENT_DIRECTORY_H

#include "../../../common/types/bstring.h"

int PosixPath__absolute(const char * path, BStringPtr result);
void LibC__set_current_directory(const char * path);

#endif
