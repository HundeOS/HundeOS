#include "../../user.h"
#include "../../sys/io.h"

static Optional(BString) Write__split_path_value(ConstBStringPtr arg, BStringPtr path)
{
    if (!BString__contains_char(arg, '='))
    {
        return None(BString);
    }

    BString delimiter = BSTRING_NEW("=");
    BStringSplit split = BString__split_first_any(arg, &delimiter);
    match_optional(BString, split.match,
        some(raw_path) { *path = IO__path_from_arg(&raw_path); },
        none_do_nothing()
    );
    return Some(BString, split.rest);
}

static BString Write__value_from_args(int argc, char ** argv)
{
    BString value = EMPTY_BSTRING;
    BString space = BSTRING_NEW(" ");
    for (int i = 2; i < argc; i++)
    {
        if (i > 2)
        {
            value = BString__append(&value, &space);
        }

        BString arg = BString__new(argv[i]);
        value = BString__append(&value, &arg);
    }

    return value;
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        print("write: usage write <path>=<text>\n");
        return 1;
    }

    BString path = EMPTY_BSTRING;
    Optional(BString) value = None(BString);
    if (argc > 2)
    {
        BString path_arg = BString__new(argv[1]);
        path = IO__path_from_arg(&path_arg);
        value = Some(BString, Write__value_from_args(argc, argv));
    }
    else
    {
        BString arg = BString__new(argv[1]);
        value = Write__split_path_value(&arg, &path);
    }

    match_optional(BString, value,
        some(text)
        {
            IOHandle handle = IO__open(&path, IO_OPEN_WRITE);
            if (handle < 0)
            {
                printf("write: open error %i\n", (int) handle);
                return 1;
            }

            ByteCount bytes = IO__write(handle, &text);
            if (bytes < 0)
            {
                printf("write: write error %i\n", (int) bytes);
                close(handle);
                return 1;
            }

            close(handle);
            return 0;
        },
        none_do_nothing()
    );

    print("write: usage write <path>=<text>\n");
    print("write: usage write <path> <text>\n");
    return 1;
}
