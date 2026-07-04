#include "interpreter.h"

#include <stddef.h>
#include "../../user.h"
#include "../../sys/spawn.h"
#include "../../sys/syslog.h"

#define STRIP_BUFFER_SIZE 2048
#define COMMAND_PATH_BUFFER_SIZE 2048
#define COMMAND_ARG_BUFFER_SIZE 2048
#define COMMAND_MAX_ARGS 3
#define COMMAND_ARG_STORAGE_SLOTS (COMMAND_MAX_ARGS + 1)
#define SHELL_DEBUG_PREVIEW_SIZE 96
static const char * CMD_EXIT = "exit";
static const char * COMMAND_SEARCH_PATH = "/ebin";
static char command_buffer[STRIP_BUFFER_SIZE];
static char argument_buffers[COMMAND_ARG_STORAGE_SLOTS][COMMAND_ARG_BUFFER_SIZE];

static void debug_preview(const char * input, char * target, size_t target_size)
{
    size_t out = 0;
    for (size_t in = 0; input[in] != '\0' && out < target_size - 1 && in < SHELL_DEBUG_PREVIEW_SIZE; in++)
    {
        char ch = input[in];
        if (ch == '\n')
        {
            if (out + 2 >= target_size)
            {
                break;
            }
            target[out++] = '\\';
            target[out++] = 'n';
            continue;
        }
        if (ch == '\r')
        {
            if (out + 2 >= target_size)
            {
                break;
            }
            target[out++] = '\\';
            target[out++] = 'r';
            continue;
        }
        if (ch == '\t')
        {
            if (out + 2 >= target_size)
            {
                break;
            }
            target[out++] = '\\';
            target[out++] = 't';
            continue;
        }
        target[out++] = ch;
    }
    target[out] = '\0';
}

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

static size_t parse_command(
    const char* input,
    char* command,
    size_t command_size,
    char args[COMMAND_ARG_STORAGE_SLOTS][COMMAND_ARG_BUFFER_SIZE]
)
{
    clear_buffer(command, command_size);
    for (size_t i = 0; i < COMMAND_ARG_STORAGE_SLOTS; i++)
    {
        clear_buffer(args[i], COMMAND_ARG_BUFFER_SIZE);
    }

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

    size_t arg_count = 0;
    while (*input != '\0' && arg_count < COMMAND_MAX_ARGS)
    {
        while (whitespace(input) && *input != '\0')
        {
            input++;
        }

        if (*input == '\0')
        {
            break;
        }

        size_t arg_pos = 0;
        while (!whitespace(input) && arg_pos < COMMAND_ARG_BUFFER_SIZE - 1)
        {
            args[arg_count][arg_pos++] = *input++;
        }

        arg_count++;

        while (!whitespace(input) && *input != '\0')
        {
            input++;
        }
    }

    return arg_count;
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
    kernel_syslog_tagf("SHELL", "execute cmd=%s path=%s", cmd, path);
    SpawnResult result = spawn(P_WAIT, path, NULL, NULL, NULL);
    kernel_syslog_tagf("SHELL", "execute spawn result=%i", result);
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

ShellResult execute_with_args(const char* cmd, char args[COMMAND_ARG_STORAGE_SLOTS][COMMAND_ARG_BUFFER_SIZE], size_t arg_count)
{
    char path[COMMAND_PATH_BUFFER_SIZE];
    command_path(cmd, path, COMMAND_PATH_BUFFER_SIZE);

    const char * arg0 = arg_count > 0 ? args[0] : NULL;
    const char * arg1 = arg_count > 1 ? args[1] : NULL;
    const char * arg2 = arg_count > 2 ? args[2] : NULL;

    // print("Executing:\n");
    kernel_syslog_tagf(
        "SHELL",
        "execute_with_args cmd=%s path=%s argc=%i arg0=%s arg1=%s arg2=%s",
        cmd,
        path,
        (int) arg_count,
        arg0 ? arg0 : "",
        arg1 ? arg1 : "",
        arg2 ? arg2 : ""
    );
    SpawnResult result = spawn(P_WAIT, path, arg0, arg1, arg2);
    kernel_syslog_tagf("SHELL", "execute_with_args spawn result=%i", result);
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
    char * command = command_buffer;
    char preview[SHELL_DEBUG_PREVIEW_SIZE * 2];
    debug_preview(input, preview, sizeof(preview));
    kernel_syslog_tagf("SHELL", "interpret input=%s", preview);

    size_t arg_count = parse_command(input, command, STRIP_BUFFER_SIZE, argument_buffers);
    kernel_syslog_tagf(
        "SHELL",
        "interpret parsed command=%s argc=%i arg0=%s arg1=%s arg2=%s",
        command,
        (int) arg_count,
        arg_count > 0 ? argument_buffers[0] : "",
        arg_count > 1 ? argument_buffers[1] : "",
        arg_count > 2 ? argument_buffers[2] : ""
    );
    if (matches(command, CMD_EXIT))
    {
        kernel_syslog_tagf("SHELL", "interpret exit command");
        exit(0);
        return EXIT;
    }
    if (arg_count > 0)
    {
        return execute_with_args(command, argument_buffers, arg_count);
    }
    return execute(command);
}
