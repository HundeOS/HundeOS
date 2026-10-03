#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#include "posix_io.h"

static char terminal_name[BSTRING_MAX_SIZE];

int isatty(int descriptor)
{
    match_optional(
        PosixIO__terminal_path(descriptor),
        some(path)
        {
            return 1;
        },
        none()
        {
            return 0;
        }
    );
}

char * ttyname(int descriptor)
{
    match_optional(
        PosixIO__terminal_path(descriptor),
        some(path)
        {
            BString__copy_to(&path, terminal_name, sizeof(terminal_name));
            return terminal_name;
        },
        none()
        {
            return NULL;
        }
    );
}
