#include "../user.h"
#include <stdbool.h>

#include "../arch/wait.h"

void main() {
    for (int i = 0; i < 10; i++)
    {
        print("Hello World!\n");
    }

    while (true)
    {
        for (int i = 0; i < 1000; i++)
        {
            wait_for_interrupt();
        }
        print("Wake up!\n");
    }
}
