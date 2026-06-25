#include "../../user.h"
#include "../../sys/io.h"

#define PATH_BUFFER_SIZE 512
#define VALUE_BUFFER_SIZE 2048

static size_t string_len(const char * value)
{
    size_t length = 0;
    while (value[length] != '\0')
    {
        length++;
    }

    return length;
}

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

static const char * split_path_value(const char * arg, char * path, size_t path_size)
{
    char raw_path[PATH_BUFFER_SIZE];
    size_t position = 0;

    for (size_t i = 0; i < PATH_BUFFER_SIZE; i++)
    {
        raw_path[i] = '\0';
    }

    while (arg[position] != '\0' && arg[position] != '=' && position < PATH_BUFFER_SIZE - 1)
    {
        raw_path[position] = arg[position];
        position++;
    }

    if (arg[position] != '=')
    {
        return NULL;
    }

    path_from_arg(raw_path, path, path_size);
    return &arg[position + 1];
}

static void append_value(char * target, size_t target_size, const char * value)
{
    size_t position = string_len(target);
    for (size_t i = 0; value[i] != '\0' && position < target_size - 1; i++)
    {
        target[position++] = value[i];
    }
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        print("write: usage write <path>=<text>\n");
        return 1;
    }

    char path[PATH_BUFFER_SIZE];
    char value_buffer[VALUE_BUFFER_SIZE];
    for (size_t i = 0; i < VALUE_BUFFER_SIZE; i++)
    {
        value_buffer[i] = '\0';
    }

    const char * value = NULL;
    if (argc > 2)
    {
        path_from_arg(argv[1], path, PATH_BUFFER_SIZE);
        for (int i = 2; i < argc; i++)
        {
            if (i > 2)
            {
                append_value(value_buffer, VALUE_BUFFER_SIZE, " ");
            }
            append_value(value_buffer, VALUE_BUFFER_SIZE, argv[i]);
        }
        value = value_buffer;
    }
    else
    {
        value = split_path_value(argv[1], path, PATH_BUFFER_SIZE);
    }

    if (value == NULL)
    {
        print("write: usage write <path>=<text>\n");
        print("write: usage write <path> <text>\n");
        return 1;
    }

    IOHandle handle = open(path, IO_OPEN_WRITE);
    if (handle < 0)
    {
        printf("write: open error %i\n", (int) handle);
        return 1;
    }

    ByteCount bytes = write(handle, (char *) value, string_len(value));
    if (bytes < 0)
    {
        printf("write: write error %i\n", (int) bytes);
        close(handle);
        return 1;
    }

    close(handle);
    return 0;
}
