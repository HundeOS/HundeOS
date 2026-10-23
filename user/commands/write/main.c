#include "../../user.h"
#include "sys/io.h"

static Optional(BString) Write__split_path_value(ConstBStringPtr arg, BStringPtr path)
{
    if (!BString__contains_char(arg, '='))
    {
        return None(BString);
    }

    BStringSplit split = BString__split_first_any(arg, &bstr("="));
    match_optional(split.match,
        some(raw_path)
        {
            *path = IO__path_from_arg(&raw_path);
        },
        none_do_nothing()
    );
    return Some(BString, split.rest);
}

static BString Write__value_from_args(int argc, char ** argv)
{
    BString value = empty_bstr();
    for (int i = 2; i < argc; i++)
    {
        if (i > 2)
        {
            value = BString__append(&value, &bstr(" "));
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
        IO__print(&bstr("write: usage write <path>=<text>\n"));
        return 1;
    }

    BString path = empty_bstr();
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

    match_optional(
        value,
        some(text)
        {
            match_result(
                IO__open(&path, IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_TRUNCATE),
                success(handle)
                {
                    match_result(
                        IO__write(handle, &text),
                        success(bytes)
                        {
                            IO__close(handle);
                            return 0;
                        },
                        error(code, message)
                        {
                            IO__printf(&bstr("write: write error %i\n"), code);
                            IO__close(handle);
                            return 1;
                        }
                    );
                },
                error(code, message)
                {
                    IO__printf(&bstr("write: open error %i\n"), code);
                    return 1;
                }
            );
            return 1;
        },
        none_do_nothing()
    );

    IO__print(&bstr("write: usage write <path>=<text>\n"));
    IO__print(&bstr("write: usage write <path> <text>\n"));
    return 1;
}
