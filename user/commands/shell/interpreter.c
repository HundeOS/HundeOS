#include "interpreter.h"

#include "../../user.h"
#include "sys/io.h"
#include "sys/process.h"
#include "sys/spawn.h"

#define SPAWN_ARG_LIMIT SPAWN_MAX_ARGS

static const BString CMD_EXIT = bstr("exit");
static const BString QUOTE_DELIMITER = bstr("\"");
static const BString TOKEN_DELIMITERS = bstr(" \t\n\r");
static const BString BACKGROUND_OPERATOR = bstr("&");

FixedArrayBodyDefine(CommandArguments);

static Command Command__new(void)
{
    Command command = {
        .name = empty_bstr(),
        .args = {0},
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

static BString Interpreter__command_path(ConstBStringPtr directory, ConstBStringPtr cmd)
{
    BString prefix = BString__append(directory, &bstr("/"));
    return BString__append(&prefix, cmd);
}

static SpawnResult Interpreter__spawn_in(ConstBStringPtr directory, ConstBStringPtr command, SpawnMode mode,
                                         ConstSpawnArgumentsPtr arguments)
{
    BString path = Interpreter__command_path(directory, command);
    return spawn_argv(mode, &path, arguments);
}

static bool Interpreter__set_foreground(uint64_t pid)
{
    match_result(
        IO__set_terminal_foreground(pid),
        success(status)
        {
            return status == IO_OK;
        },
        error(code, message)
        {
            return false;
        }
    );
    return false;
}

static ShellResult Interpreter__wait_for_foreground(SpawnResult child_pid)
{
    bool foreground_set = Interpreter__set_foreground((uint64_t)child_pid);
    if (!foreground_set)
    {
        kill_process((uint64_t)child_pid, PROCESS_SIGNAL_KILL);
    }

    ProcessChildEvent event = ProcessChildEvent__new();
    ProcessChildEventResult wait_result;
    do
    {
        wait_result = wait_for_child_event(&event);
    } while (wait_result == PROCESS_CHILD_EVENT_OK && event.pid != (uint64_t)child_pid);

    bool foreground_reset = Interpreter__set_foreground(0);
    return foreground_set && wait_result == PROCESS_CHILD_EVENT_OK && foreground_reset ? COMMAND_SUCCESS
                                                                                       : COMMAND_FAILED;
}

Interpreter Interpreter__new(void)
{
    Interpreter interpreter = {.command = {0}};
    interpreter.command = Command__new();
    return interpreter;
}

Command Interpreter__parse(InterpreterPtr self, ConstBStringPtr input)
{
    self->command = Command__new();

    BString remaining = *input;

    BStringSplit command_split = Interpreter__parse_token(&remaining);
    match_optional(
        command_split.match,
        some(parsed_command)
        {
            self->command.name = parsed_command;
        },
        none()
        {
            return self->command;
        }
    );

    remaining = command_split.rest;

    while (CommandArguments__len(&self->command.args) < COMMAND_MAX_ARGS)
    {
        BStringSplit arg_split = Interpreter__parse_token(&remaining);
        match_optional(
            arg_split.match,
            some(argument)
            {
                match_result(
                    CommandArguments__push(&self->command.args, argument),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return self->command;
                    }
                );
            },
            none()
            {
                break;
            }
        );
        remaining = arg_split.rest;
    }

    match_optional(
        CommandArguments__pop(&self->command.args),
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
        none_do_nothing()
    );

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

    SpawnArguments arguments = SpawnArguments__new();
    CommandArguments command_arguments = command->args;
    for_each_fa(
        CommandArguments,
        command_arguments,
        argument_index,
        argument,
        {
            match_result(
                SpawnArguments__push(&arguments, argument),
                success_do_nothing(),
                error(code, message)
                {
                    return COMMAND_FAILED;
                }
            );
        }
    );
    SpawnMode mode = P_NOWAIT;
    SpawnResult result;
    if (BString__starts_with(&command->name, &bstr("/")))
    {
        result = spawn_argv(mode, &command->name, &arguments);
    }
    else
    {
        result = Interpreter__spawn_in(&bstr("/usr/local/bin"), &command->name, mode, &arguments);
        if (result == SPAWN_NO_FILE || result == SPAWN_NO_PATH)
        {
            result = Interpreter__spawn_in(&bstr("/usr/bin"), &command->name, mode, &arguments);
        }
        if (result == SPAWN_NO_FILE || result == SPAWN_NO_PATH)
        {
            result = Interpreter__spawn_in(&bstr("/bin"), &command->name, mode, &arguments);
        }
        if (result == SPAWN_NO_FILE || result == SPAWN_NO_PATH)
        {
            result = Interpreter__spawn_in(&bstr("/sbin"), &command->name, mode, &arguments);
        }
    }

    if (result > SPAWN_SUCCESS && !command->background)
    {
        return Interpreter__wait_for_foreground(result);
    }

    if (result >= SPAWN_SUCCESS)
    {
        return COMMAND_SUCCESS;
    }

    if (result < SPAWN_SUCCESS)
    {
        IO__printf(&bstr("result = %i\n"), (int)result);
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
