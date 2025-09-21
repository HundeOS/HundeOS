#ifndef _USER_SYSCALL_H
#define _USER_SYSCALL_H
#include <stdint.h>

uint32_t syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3);

#endif