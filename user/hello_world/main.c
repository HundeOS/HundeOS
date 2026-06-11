#include "../user.h"
#include <stdbool.h>

#include "../sys/wait.h"

int main(void) {
    for (int i = 0; i < 10; i++)
    {
        print("Hello World!\n");
    }
    return 0;
}
