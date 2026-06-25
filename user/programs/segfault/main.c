#include "../../user.h"

int main(void)
{
    print("segfault: writing to null\n");

    volatile unsigned long * bad_address = (volatile unsigned long *) 0;
    *bad_address = 1;

    print("segfault: write unexpectedly succeeded\n");
    return 1;
}
