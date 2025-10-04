#ifndef __KERNEL_CPU_H
#define __KERNEL_CPU_H

// architecture specific
typedef struct cpu cpu;

extern uint64_t CPU__id();
extern uint64_t CPU__count();

#endif
