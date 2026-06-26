#include "../../user.h"
#include "../../sys/io.h"

#define PATH_BUFFER_SIZE 512
#define READ_BUFFER_SIZE 512

static void path_from_arg(const char * arg, char * path, size_t path_size)
{
    for (size_t i = 0; i < path_size; i++)
    {
        path[i] = '\0';
    }

    if (arg == NULL || arg[0] == '\0')
    {
        path[0] = '/';
        return;
    }

    size_t position = 0;
    if (arg[0] != '/' && position < path_size - 1)
    {
        path[position++] = '/';
    }

    for (size_t i = 0; arg[i] != '\0' && position < path_size - 1; i++)
    {
        path[position++] = arg[i];
    }
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        print("cat: missing path\n");
        return 1;
    }

    char path[PATH_BUFFER_SIZE];
    path_from_arg(argv[1], path, PATH_BUFFER_SIZE);

    IOHandle handle = open(path, IO_OPEN_READ);
    if (handle < 0)
    {
        printf("cat: open error %i\n", (int) handle);
        return 1;
    }

    for (;;)
    {
        char buffer[READ_BUFFER_SIZE + 1];
        ByteCount bytes = read(handle, buffer, READ_BUFFER_SIZE);
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

        buffer[bytes] = '\0';
        print(buffer);
    }

    close(handle);
    return 0;
}
