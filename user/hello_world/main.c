#include "../user.h"
#include <stdbool.h>

#include "../sys/wait.h"

void main() {
    for (int i = 0; i < 10; i++)
    {
        print("Hello World!\n");
    }
}
