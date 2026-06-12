#include "interpreter.h"

#include <stddef.h>
#include "../../user.h"
#include "../../sys/spawn.h"

#define STRIP_BUFFER_SIZE 2048
#define COMMAND_PATH_BUFFER_SIZE 2048
#define COMMAND_ARG_BUFFER_SIZE 2048
static const char * CMD_EXIT = "exit";
static const char * COMMAND_SEARCH_PATH = "/ebin";

static bool whitespace(const char* input)
{
    return *input == ' ' || *input == '\t' || *input == '\n' || *input == '\r' || *input == '\0';
}

static void clear_buffer(char* buffer, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        buffer[i] = '\0';
    }
}

static void parse_command(const char* input, char* command, size_t command_size, char* arg0, size_t arg0_size)
{
    clear_buffer(command, command_size);
    clear_buffer(arg0, arg0_size);

    while (whitespace(input) && *input != '\0')
    {
        input++;
    }

    size_t command_pos = 0;
    while (!whitespace(input) && command_pos < command_size - 1)
    {
        command[command_pos++] = *input++;
    }

    while (whitespace(input) && *input != '\0')
    {
        input++;
    }

    size_t arg_pos = 0;
    while (!whitespace(input) && arg_pos < arg0_size - 1)
    {
        arg0[arg_pos++] = *input++;
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

static void command_path(const char* cmd, char* path, size_t path_size)
{
    for (size_t i = 0; i < path_size; i++)
    {
        path[i] = '\0';
    }

    if (cmd[0] == '/')
    {
        for (size_t i = 0; cmd[i] != '\0' && i < path_size - 1; i++)
        {
            path[i] = cmd[i];
        }
        return;
    }

    size_t position = 0;
    for (size_t i = 0; COMMAND_SEARCH_PATH[i] != '\0' && position < path_size - 1; i++)
    {
        path[position++] = COMMAND_SEARCH_PATH[i];
    }

    if (position < path_size - 1)
    {
        path[position++] = '/';
    }

    for (size_t i = 0; cmd[i] != '\0' && position < path_size - 1; i++)
    {
        path[position++] = cmd[i];
    }
}

ShellResult execute(const char* cmd)
{
    char path[COMMAND_PATH_BUFFER_SIZE];
    command_path(cmd, path, COMMAND_PATH_BUFFER_SIZE);

    // print("Executing:\n");
    SpawnResult result = spawn(P_WAIT, path, NULL);
    if (result != SPAWN_SUCCESS)
    {
        printf("result = %i\n", result);
    }

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

ShellResult execute_with_arg(const char* cmd, const char* arg0)
{
    char path[COMMAND_PATH_BUFFER_SIZE];
    command_path(cmd, path, COMMAND_PATH_BUFFER_SIZE);

    // print("Executing:\n");
    SpawnResult result = spawn(P_WAIT, path, arg0);
    if (result != 0)
    {
        printf("result = %i\n", result);
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

ShellResult interpret(const char* input)
{
    char command[STRIP_BUFFER_SIZE];
    char arg0[COMMAND_ARG_BUFFER_SIZE];

    parse_command(input, command, STRIP_BUFFER_SIZE, arg0, COMMAND_ARG_BUFFER_SIZE);
    if (matches(command, CMD_EXIT))
    {
        exit(0);
        return EXIT;
    }
    if (arg0[0] != '\0')
    {
        return execute_with_arg(command, arg0);
    }
    return execute(command);
}
