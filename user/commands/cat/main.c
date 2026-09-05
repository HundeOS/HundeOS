#include "../../user.h"
#include "sys/io.h"

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        IO__print(&bstr("cat: missing path\n"));
        return 1;
    }

    BString path_arg = BString__new(argv[1]);
    BString path = IO__path_from_arg(&path_arg);

    IOHandle handle = IO__open(&path, IO_OPEN_READ);
    if (handle < 0)
    {
        IO__printf(&bstr("cat: open error %i\n"), (int) handle);
        return 1;
    }

    for (;;)
    {
        BString buffer = empty_bstr();
        ByteCount bytes = IO__read(handle, &buffer);
        if (bytes == IO_EOF)
        {
            break;
        }

        if (bytes < 0)
        {
            IO__printf(&bstr("cat: read error %i\n"), (int) bytes);
            close(handle);
            return 1;
        }

        if (bytes == 0)
        {
            break;
        }

        IO__print(&buffer);
    }

    close(handle);
    return 0;
}
