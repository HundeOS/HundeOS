#ifndef __KERNEL_HALT_H
#define __KERNEL_HALT_H

void halt();
void panic(const char * error);
void dump_state();

#endif