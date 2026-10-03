#include "../../user.h"
#include <stdbool.h>

#include "sys/interrupt.h"

int main(void)
{
    for (int i = 0; i < 10; i++)
    {
        IO__print(&bstr("Hello World!\n"));
    }
    return 0;
}
