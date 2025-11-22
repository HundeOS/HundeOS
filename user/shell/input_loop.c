#include "../user.h"
#include "interpreter.h"

int input_loop()
{
    while (true)
    {
        char buf[1024];
        print("\n> ");
        ByteCount read = console_read_line(buf, true, 1024);
        print("\n");
        print(buf);
        print("\n");

        ShellResult result = interpret(buf);
        switch (result)
        {
            case EXIT:
                print("Shell exiting\n");
                return 0;
            case COMMAND_NOT_FOUND:
                print("Command not found\n");
                break;
            case COMMAND_FAILED:
                print("Command failed\n");
                break;
            case COMMAND_SUCCESS:
                print("Command completed successfully\n");
                break;
        }
    }

}