#ifndef __USER_SHELL_INTERPRETER_H
#define __USER_SHELL_INTERPRETER_H

#include <stddef.h>
#include "../../../common/types/bstring.h"
#include "types/fixed_array.h"

#define COMMAND_MAX_ARGS 32

OptionalDef(ConstBStringPtr);
FixedArrayDefine(BString, COMMAND_MAX_ARGS, CommandArguments, bstring_equals);

typedef struct Command
{
    BString name;
    CommandArguments args;
    bool background;
}
Command;

typedef Command * CommandPtr;
typedef const Command * ConstCommandPtr;

typedef struct Interpreter
{
    Command command;
}
Interpreter;

typedef Interpreter * InterpreterPtr;
typedef const Interpreter * ConstInterpreterPtr;

typedef enum ShellResult
{
    COMMAND_NOT_FOUND,
    COMMAND_EXECUTED,
    COMMAND_FAILED,
    COMMAND_SUCCESS,
    EXIT,
} ShellResult;

Interpreter Interpreter__new(void);
Command Interpreter__parse(InterpreterPtr self, ConstBStringPtr input);
ShellResult Interpreter__execute(InterpreterPtr self, CommandPtr command);
ShellResult Interpreter__interpret(InterpreterPtr self, ConstBStringPtr input);

#endif
