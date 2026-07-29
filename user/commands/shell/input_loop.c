#include "../../user.h"
#include "input_loop.h"

typedef struct ConsoleInputStream
{
    InputStream input;
}
ConsoleInputStream;

static BString ConsoleInputStream__read_line(InputStreamPtr self, bool echo);

static ConsoleInputStream ConsoleInputStream__new(void)
{
    ConsoleInputStream input = {
        .input = {
            .read_line = ConsoleInputStream__read_line
        }
    };
    return input;
}

static BString ConsoleInputStream__read_line(InputStreamPtr self, bool echo)
{
    return Console__read_line(echo);
}

static void Shell__handle_result(ConstShellPtr self, ShellResult result)
{
    switch (result)
    {
        case EXIT:
            print("Shell exiting\n");
            break;
        case COMMAND_NOT_FOUND:
            print("Command not found\n");
            break;
        case COMMAND_FAILED:
            print("Command failed\n");
            break;
        case COMMAND_EXECUTED:
        case COMMAND_SUCCESS:
            break;
    }
}

BString InputStream__read_line(InputStreamPtr self, bool echo)
{
    return self->read_line(self, echo);
}

Shell Shell__new(InputStreamPtr input)
{
    Shell shell = {
        .interpreter = Interpreter__new(),
        .input = input
    };
    return shell;
}

ShellResult Shell__run_once(ShellPtr self)
{
    print("\n> ");
    BString line = InputStream__read_line(self->input, true);
    print("\n");

    ShellResult result = Interpreter__interpret(&self->interpreter, &line);
    Shell__handle_result(self, result);
    return result;
}

int Shell__run(ShellPtr self)
{
    while (true)
    {
        if (Shell__run_once(self) == EXIT)
        {
            return 0;
        }
    }
}

int input_loop()
{
    ConsoleInputStream input = ConsoleInputStream__new();
    Shell shell = Shell__new(&input.input);
    return Shell__run(&shell);
}
