#include "../../user.h"
#include "sys/io.h"

int main(int argc, char ** argv)
{
    if (argc != 3)
    {
        IO__print(&bstr("ln: usage ln <source> <target>\n"));
        return 1;
    }

    BString source_arg = BString__new(argv[1]);
    BString target_arg = BString__new(argv[2]);
    BString source_path = IO__path_from_arg(&source_arg);
    BString target_path = IO__path_from_arg(&target_arg);

    IOStatus status = IO__link_file(&source_path, &target_path);
    if (status != IO_OK)
    {
        IO__printf(&bstr("ln: error %s (%i)\n"), io_status_string(status), (int) status);
        return 1;
    }

    return 0;
}
