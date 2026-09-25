#ifndef __USER_LIB_NEWLIB_POSIX_IO_H
#define __USER_LIB_NEWLIB_POSIX_IO_H

#include "syscalls/io.h"

IOHandle PosixIO__native_handle(int descriptor);
Optional(BString) PosixIO__path(int descriptor);

#endif
