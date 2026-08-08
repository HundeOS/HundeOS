#include "../../user.h"
#include "../../sys/system.h"

int main(int argc, char ** argv)
{
    if (argc != 1)
    {
        print("shutdown: usage shutdown\n");
        return 1;
    }

    SystemCallResult result = shutdown_system();
    if (result != SYSCALL_OK)
    {
        printf("shutdown: error %i\n", (int) result);
        return 1;
    }

    return 0;
}
