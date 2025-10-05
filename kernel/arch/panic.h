#ifndef __KERNEL_ARCH_HALT_H
#define __KERNEL_ARCH_HALT_H

typedef struct exception_context_t exception_context_t;

extern void Kernel__halt();
extern void Kernel__dump_state(exception_context_t * ctx);

#endif