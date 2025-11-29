#include "interpreter.h"

#include <stddef.h>
#include "../user.h"
#include "../sys/spawn.h"

#define STRIP_BUFFER_SIZE 2048
static const char * CMD_EXIT = "exit";

static bool whitespace(const char* input)
{
    return *input == ' ' || *input == '\t' || *input == '\n' || *input == '\r' || *input == '\0';
}

static void strip(const char* input, char* stripped, size_t strip_size)
{
    for (size_t i = 0; i < strip_size; i++)
    {
        stripped[i] = '\0';
    }

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
    print("Executing:\n");
    SpawnResult result = spawn(P_WAIT, cmd, NULL);
    printf("result = %i\n", result);
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
    char stripped[STRIP_BUFFER_SIZE];

    strip(input, stripped, STRIP_BUFFER_SIZE);
    if (matches(stripped, CMD_EXIT))
    {
        exit(0);
        return EXIT;
    }
    return execute(stripped);
}
