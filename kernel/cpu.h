#ifndef __KERNEL_CPU_H
#define __KERNEL_CPU_H

// architecture specific
typedef struct cpu cpu;

extern uint64_t cpu_id();
extern uint64_t cpu_count();

#endif
