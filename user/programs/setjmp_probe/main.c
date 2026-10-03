#include <setjmp.h>
#include <stdint.h>

#include "sys/print.h"

int SetjmpProbe__registers(void);

static void SetjmpProbe__jump_from_nested_frame(jmp_buf environment, int value)
{
    volatile uint64_t stack_data[16] = {1};
    if (stack_data[0] != 1)
    {
        value = -1;
    }
    longjmp(environment, value);
}

int main(void)
{
    if (SetjmpProbe__registers() != 0)
    {
        return 1;
    }

    jmp_buf outer;
    jmp_buf inner;

    int outer_result = setjmp(outer);
    if (outer_result == 0)
    {
        int inner_result = setjmp(inner);
        if (inner_result == 0)
        {
            SetjmpProbe__jump_from_nested_frame(inner, 0);
        }
        if (inner_result != 1)
        {
            return 1;
        }
        SetjmpProbe__jump_from_nested_frame(outer, 7);
    }
    if (outer_result != 7)
    {
        return 1;
    }

    IO__print(&bstr("setjmp_probe: ok\n"));
    return 0;
}
