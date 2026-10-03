#include <errno.h>
#include <unistd.h>

#include "../../common/types/bstring.h"

static const BString HOSTNAME = bstr("hundeos");

int gethostname(char * name, size_t size)
{
    if (name == NULL)
    {
        errno = EFAULT;
        return -1;
    }
    if (size <= (size_t)BString__length(&HOSTNAME))
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    BString__copy_to(&HOSTNAME, name, (StringPos)size);
    return 0;
}
