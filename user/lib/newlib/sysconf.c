#include <errno.h>
#include <sys/times.h>
#include <unistd.h>

long sysconf(int name)
{
    if (name == _SC_CLK_TCK)
    {
        return CLK_TCK;
    }

    errno = EINVAL;
    return -1;
}
