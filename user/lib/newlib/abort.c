#include "include/signal.h"
#include "include/stdlib.h"

void _exit(int code) __attribute__((noreturn));

void abort(void)
{
    raise(SIGABRT);
    raise(SIGKILL);
    _exit(128 + SIGABRT);
}
