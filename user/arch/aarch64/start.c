#include "../../user.h"

extern int main(void);

void _start(void)
{
    int code = main();
    exit(code);

    for (;;)
    {
    }
}
