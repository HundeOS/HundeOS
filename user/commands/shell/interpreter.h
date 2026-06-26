#ifndef __USER_SHELL_INTERPRETER_H
#define __USER_SHELL_INTERPRETER_H

typedef enum ShellResult
{
    COMMAND_NOT_FOUND,
    COMMAND_EXECUTED,
    COMMAND_FAILED,
    COMMAND_SUCCESS,
    EXIT,
} ShellResult;

ShellResult interpret(const char* input);

#endif