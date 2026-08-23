#include "../../user.h"
#include "../../sys/io.h"

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        print("cat: missing path\n");
        return 1;
    }

    BString path_arg = BString__new(argv[1]);
    BString path = IO__path_from_arg(&path_arg);

    IOHandle handle = IO__open(&path, IO_OPEN_READ);
    if (handle < 0)
    {
        printf("cat: open error %i\n", (int) handle);
        return 1;
    }

    for (;;)
    {
        BString buffer = EMPTY_BSTRING;
        ByteCount bytes = IO__read(handle, &buffer);
        if (bytes == IO_EOF)
        {
            break;
        }

        if (bytes < 0)
        {
            printf("cat: read error %i\n", (int) bytes);
            close(handle);
            return 1;
        }

        if (bytes == 0)
        {
            break;
        }

        print(BString__cstr(&buffer));
    }

    close(handle);
    return 0;
}
