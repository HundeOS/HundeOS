#ifndef __USER_LIB_NEWLIB_SYS_MMAN_H
#define __USER_LIB_NEWLIB_SYS_MMAN_H

#include <stddef.h>
#include <sys/types.h>

#define PROT_READ 1
#define MAP_FILE 0
#define MAP_SHARED 1
#define MAP_FAILED ((void *)-1)
#define MADV_SEQUENTIAL 2

void * mmap(void * address, size_t length, int protection, int flags, int descriptor, off_t offset);
int munmap(void * address, size_t length);
int madvise(void * address, size_t length, int advice);

#endif
