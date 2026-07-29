#include "interpreter.h"

#include "../../user.h"
#include "../../sys/spawn.h"

#define SPAWN_ARG_LIMIT 3

static const BString CMD_EXIT = BSTRING_NEW("exit");
static const BString COMMAND_SEARCH_PATH = BSTRING_NEW("/ebin");
static const BString QUOTE_DELIMITER = BSTRING_NEW("\"");
static const BString TOKEN_DELIMITERS = BSTRING_NEW(" \t\n\r");

FixedArrayBodyDefine(CommandArguments);

static Command Command__new(void)
{
    Command command = {
        .name = EMPTY_BSTRING,
        .args = { 0 }
    };
    command.args = CommandArguments__new();

    return command;
}

static BStringSplit Interpreter__parse_token(ConstBStringPtr input)
{
    BString remaining = BString__strip(input);

    if (BString__starts_with(&remaining, &QUOTE_DELIMITER))
    {
        BString quoted = BString__slice(&remaining, 1, -1);
        return BString__split_first_any(&quoted, &QUOTE_DELIMITER);
    }

    return BString__split_first_any(&remaining, &TOKEN_DELIMITERS);
}

static bool Interpreter__matches(ConstBStringPtr str, ConstBStringPtr cmd)
{
    return BString__equals(str, cmd);
}

static BString Interpreter__command_path(ConstBStringPtr cmd)
{
    BString root_prefix = BSTRING_NEW("/");

    if (BString__starts_with(cmd, &root_prefix))
    {
        return *cmd;
    }

    BString prefix = BString__append(&COMMAND_SEARCH_PATH, &root_prefix);
    return BString__append(&prefix, cmd);
}

Interpreter Interpreter__new(void)
{
    Interpreter interpreter = {
        .command = { 0 }
    };
    interpreter.command = Command__new();
    return interpreter;
}

Command Interpreter__parse(InterpreterPtr self, ConstBStringPtr input)
{
    self->command = Command__new();

    BString remaining = *input;

    BStringSplit command_split = Interpreter__parse_token(&remaining);
    Match(BString, command_split.match)
        IsSome(parsed_command, self->command.name = parsed_command)
        IsNone(return self->command)
    End

    remaining = command_split.rest;

    while (CommandArguments__len(&self->command.args) < COMMAND_MAX_ARGS)
    {
        BStringSplit arg_split = Interpreter__parse_token(&remaining);
        Match(BString, arg_split.match)
            IsSome(argument,
            {
                ResultMatch(CollectionCode, CommandArguments__push(&self->command.args, argument))
                    IsError(code, message, return self->command)
                End
            })
            IsNone(return self->command)
        End
        remaining = arg_split.rest;
    }

    return self->command;
}

ShellResult Interpreter__execute(InterpreterPtr self, CommandPtr command)
{
    if (BString__is_empty(&command->name))
    {
        return COMMAND_SUCCESS;
    }

    if (Interpreter__matches(&command->name, &CMD_EXIT))
    {
        exit(0);
        return EXIT;
    }

    if (CommandArguments__len(&command->args) > SPAWN_ARG_LIMIT)
    {
        return COMMAND_FAILED;
    }

    BString path = Interpreter__command_path(&command->name);
    const char * arg0 = NULL;
    const char * arg1 = NULL;
    const char * arg2 = NULL;

    FAPtrMatch(CommandArguments, CommandArguments__ptr_at(&command->args, 0))
        IsSome(value, arg0 = BString__cstr(value))
    End
    FAPtrMatch(CommandArguments, CommandArguments__ptr_at(&command->args, 1))
        IsSome(value, arg1 = BString__cstr(value))
    End
    FAPtrMatch(CommandArguments, CommandArguments__ptr_at(&command->args, 2))
        IsSome(value, arg2 = BString__cstr(value))
    End
    SpawnResult result = spawn(P_WAIT, BString__cstr(&path), arg0, arg1, arg2);

    if (result != SPAWN_SUCCESS)
    {
        printf("result = %i\n", (int) result);
    }

    switch (result)
    {
        case SPAWN_SUCCESS:
            return COMMAND_SUCCESS;
        case SPAWN_NO_MEM:
        case SPAWN_NO_FILE:
        case SPAWN_NO_PATH:
        case SPAWN_ARG_TOO_LONG:
        case SPAWN_FAILED:
            return COMMAND_FAILED;
    }

    return COMMAND_FAILED;
}

ShellResult Interpreter__interpret(InterpreterPtr self, ConstBStringPtr input)
{
    Command command = Interpreter__parse(self, input);
    return Interpreter__execute(self, &command);
}
