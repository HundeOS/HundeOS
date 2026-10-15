#include <stdio.h>
#include <string.h>
#include "../../../kernel/arch/basic_io.h"
#include "../../../kernel/trace_buffer.h"

static char basic_io_output[4096];

void BasicIO__reset_output()
{
    basic_io_output[0] = '\0';
}

const char * BasicIO__output()
{
    return basic_io_output;
}

void BasicIO__puts(const char * buffer)
{
    strncat(basic_io_output, buffer, sizeof(basic_io_output) - strlen(basic_io_output) - 1);
}

void DebugTrace__puts(const char * buffer)
{
    BasicIO__puts(buffer);
}

__attribute__((weak)) void KernelTrace__append(const char * text)
{
}

__attribute__((weak)) Result(KernelTraceSize) KernelTrace__snapshot(uint8_t * destination, size_t capacity)
{
    return Success(KernelTraceSize, 0);
}

void BasicIO__put_int(int num)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%i", num);
    strncat(basic_io_output, buffer, sizeof(basic_io_output) - strlen(basic_io_output) - 1);
}
