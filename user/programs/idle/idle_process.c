#include <stdbool.h>
#include "../../sys/wait.h"
#include "../../user.h"

int main(void)
{
    while(true)
    {
        // print("reenter idle\n");
        wait_for_interrupt();
    }
}
