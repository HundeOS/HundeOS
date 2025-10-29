#include "../user.h"
#include <stdbool.h>
#include <stdint.h>

#include "../arch/aarch64/io.h"

int main(void)
{
    while (true)
    {
        char buf[100];
        print("> ");
        int64_t read = console_input(buf, 100);
        if (read <= 0)
        {
            print("Error reading console input\n");
            return -1;
        }
        if (read > 0)
        {
            print(buf);
        }

    }
}