#ifndef __KERNEL_HALT_H
#define __KERNEL_HALT_H

typedef struct exception_context_t exception_context_t;

extern void halt();
void panic(const char * error, exception_context_t * ctx);
extern void dump_state(exception_context_t * ctx);

#endif