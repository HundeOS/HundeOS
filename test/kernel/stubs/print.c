#include <stdio.h>
#include "../../../kernel/arch/basic_io.h"


void BasicIO__puts(const char * buffer)
{
    printf(buffer);
}

void BasicIO__put_int(int num)
{
    printf("%i", num);
}
