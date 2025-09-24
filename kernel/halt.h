#ifndef __KERNEL_HALT_H
#define __KERNEL_HALT_H

extern void halt();
void panic(const char * error);
extern void dump_state();

#endif