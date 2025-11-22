#include "interpreter.h"

#include <stddef.h>
#include "../user.h"
#include "../sys/spawn.h"


static const char * CMD_EXIT = "exit";

static bool whitespace(const char* input)
{
    return *input == ' ' || *input == '\t' || *input == '\n' || *input == '\r' || *input == '\0';
}

static void strip(const char* input, char* stripped)
{
    for (const char * i = input; *i != '\0'; i++)
    {
        if (!whitespace(i))
        {
            *stripped++ = *i;
        }
    }
}

static size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}

static bool matches(const char* str, const char* cmd)
{
    if (strlen(str) != strlen(cmd))
    {
        return false;
    }
    for (size_t i = 0; i < strlen(cmd); i++)
    {
        if (str[i] != cmd[i])
        {
            return false;
        }
    }

    return true;
}

ShellResult execute(const char* cmd)
{
    SpawnResult result = spawn(P_WAIT, cmd, NULL);
    switch (result)
    {
        case SPAWN_SUCCESS:
            return COMMAND_SUCCESS;
        case SPAWN_NO_MEM:
        case SPAWN_NO_FILE:
        case SPAWN_NO_PATH:
        case SPAWN_FAILED:
            return COMMAND_FAILED;
    }
    return COMMAND_FAILED;
}

ShellResult interpret(const char* input)
{
    char stripped[1024];

    strip(input, stripped);
    if (matches(stripped, CMD_EXIT))
    {
        return EXIT;
    }
    return COMMAND_NOT_FOUND;
}
