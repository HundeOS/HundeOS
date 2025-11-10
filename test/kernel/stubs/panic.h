#ifndef KERNEL_STUBS_PANIC_H
#define KERNEL_STUBS_PANIC_H
#include <stdbool.h>

void panic(const char * message);
bool panic_occurred();
void clear_panic_state();

#endif