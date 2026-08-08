#include "io/uart.h"
#include "../kernel/arch/basic_io.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void BasicIO__puts(const char* str) {
    puts(str);
}

void BasicIO__put_int(int num) {
    put_int(num);
}

#ifdef KERNEL_SEMIHOSTING_TRACE
#define SEMIHOST_SYS_OPEN 0x01
#define SEMIHOST_SYS_WRITE 0x05
#define SEMIHOST_SYS_WRITE0 0x04
#define SEMIHOST_OPEN_WRITE 4

static const char trace_path[] = "build/kernel-trace.log";
static uintptr_t trace_handle = (uintptr_t) -1;
static bool trace_open_attempted = false;
static volatile uint32_t trace_write_lock = 0;

static uintptr_t Semihosting__call(uintptr_t operation, uintptr_t argument)
{
    register uintptr_t x0 __asm__("x0") = operation;
    register uintptr_t x1 __asm__("x1") = argument;
    __asm__ volatile("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return x0;
}

static size_t DebugTrace__strlen(const char * str)
{
    size_t len = 0;
    while (str != NULL && str[len] != '\0')
    {
        len++;
    }
    return len;
}

static uintptr_t DebugTrace__open_handle()
{
    if (!trace_open_attempted)
    {
        trace_open_attempted = true;
        uintptr_t args[3] = {
            (uintptr_t) trace_path,
            SEMIHOST_OPEN_WRITE,
            sizeof(trace_path) - 1,
        };
        trace_handle = Semihosting__call(SEMIHOST_SYS_OPEN, (uintptr_t) args);
    }

    return trace_handle;
}

static bool DebugTrace__try_lock()
{
    return __atomic_exchange_n(&trace_write_lock, 1u, __ATOMIC_ACQUIRE) == 0u;
}

static void DebugTrace__unlock()
{
    __atomic_store_n(&trace_write_lock, 0u, __ATOMIC_RELEASE);
}

void DebugTrace__puts(const char* str)
{
    if (!DebugTrace__try_lock())
    {
        return;
    }

    uintptr_t handle = DebugTrace__open_handle();
    if (handle == (uintptr_t) -1)
    {
        DebugTrace__unlock();
        return;
    }

    uintptr_t args[3] = {
        handle,
        (uintptr_t) str,
        DebugTrace__strlen(str),
    };
    Semihosting__call(SEMIHOST_SYS_WRITE, (uintptr_t) args);
    DebugTrace__unlock();
}
#else
void DebugTrace__puts(const char* str)
{
    BasicIO__puts(str);
}
#endif
