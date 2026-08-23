#include "../../user.h"
#include "../../sys/io.h"

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        print("mkdir: missing path\n");
        return 1;
    }

    BString path_arg = BString__new(argv[1]);
    BString path = IO__path_from_arg(&path_arg);

    IOStatus status = IO__make_dir(&path);
    if (status < 0)
    {
        printf("mkdir: error %i\n", (int) status);
        return 1;
    }

    return 0;
}
