#ifndef __USER_LIB_NEWLIB_SETJMP_H
#define __USER_LIB_NEWLIB_SETJMP_H

#include <machine/setjmp.h>

typedef __hunde_jmp_buf jmp_buf;

int setjmp(jmp_buf environment) __attribute__((returns_twice));
void longjmp(jmp_buf environment, int value) __attribute__((noreturn));

#endif
