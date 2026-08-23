#include "interpreter.h"

#include "../../user.h"
#include "../../sys/spawn.h"

#define SPAWN_ARG_LIMIT 3

static const BString CMD_EXIT = BSTRING_NEW("exit");
static const BString COMMAND_SEARCH_PATH = BSTRING_NEW("/sbin");
static const BString QUOTE_DELIMITER = BSTRING_NEW("\"");
static const BString TOKEN_DELIMITERS = BSTRING_NEW(" \t\n\r");
static const BString BACKGROUND_OPERATOR = BSTRING_NEW("&");

FixedArrayBodyDefine(CommandArguments);

static Command Command__new(void)
{
    Command command = {
        .name = EMPTY_BSTRING,
        .args = { 0 },
        .background = false,
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
    match_optional(BString, command_split.match,
        some(parsed_command) { self->command.name = parsed_command; },
        none() { return self->command; }
    )

    remaining = command_split.rest;

    while (CommandArguments__len(&self->command.args) < COMMAND_MAX_ARGS)
    {
        BStringSplit arg_split = Interpreter__parse_token(&remaining);
        match_optional(BString, arg_split.match,
            some(argument)
            {
                match_result(CollectionCode, CommandArguments__push(&self->command.args, argument),
                    success(_) {},
                    error(code, message) { return self->command; }
                )
            },
            none() { break; }
        )
        remaining = arg_split.rest;
    }

    match_optional(BString, CommandArguments__pop(&self->command.args),
        some(last_argument)
        {
            if (BString__equals(&last_argument, &BACKGROUND_OPERATOR))
            {
                self->command.background = true;
            }
            else
            {
                CommandArguments__push(&self->command.args, last_argument);
            }
        },
        none() {}
    )

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
    const char * arg0 = NULL; // hundelint: ignore-no-null
    const char * arg1 = NULL; // hundelint: ignore-no-null
    const char * arg2 = NULL; // hundelint: ignore-no-null

    match_optional(CommandArguments__type_ptr, CommandArguments__ptr_at(&command->args, 0),
        some(value) { arg0 = BString__cstr(value); },
        none() {}
    )
    match_optional(CommandArguments__type_ptr, CommandArguments__ptr_at(&command->args, 1),
        some(value) { arg1 = BString__cstr(value); },
        none() {}
    )
    match_optional(CommandArguments__type_ptr, CommandArguments__ptr_at(&command->args, 2),
        some(value) { arg2 = BString__cstr(value); },
        none() {}
    )
    SpawnMode mode = command->background ? P_NOWAIT : P_WAIT;
    SpawnResult result = spawn(mode, BString__cstr(&path), arg0, arg1, arg2);

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
