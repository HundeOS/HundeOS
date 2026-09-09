#include "../../user.h"

int main(void)
{
    IO__print(&bstr("segfault: writing to null\n"));

    volatile unsigned long * bad_address = (volatile unsigned long *)0;
    *bad_address = 1;

    IO__print(&bstr("segfault: write unexpectedly succeeded\n"));
    return 1;
}
