#include "../../user.h"
#include "sys/io.h"

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        IO__print(&bstr("rm: usage rm [-r] <path>\n"));
        return 1;
    }

    Flags flags = 0;
    BString path_arg = BString__new(argv[1]);
    if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'r' && argv[1][2] == '\0')
    {
        flags = IO_REMOVE_RECURSIVE;
        path_arg = BString__new(argv[2]);
    }

    BString path = IO__path_from_arg(&path_arg);

    IOStatus status = IO__remove_file_with_flags(&path, flags);
    if (status < 0)
    {
        IO__printf(&bstr("rm: error %i\n"), (int)status);
        return 1;
    }

    return 0;
}
