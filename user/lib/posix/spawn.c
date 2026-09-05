#include "spawn.h"

static BString posix_spawn_no_path = bstr("posix spawn missing path");
static const size_t POSIX_ARGV0_INDEX = 0;
static const size_t POSIX_FIRST_HUNDOS_ARGUMENT_INDEX = POSIX_ARGV0_INDEX + 1;

SpawnArguments POSIX__spawn_arguments_after_argv0(char * const argv[]) // hundelint: ignore-no-raw-char-buffer
{
    SpawnArguments arguments = SpawnArguments__new();
    if (argv == NULL || argv[POSIX_ARGV0_INDEX] == NULL) // hundelint: ignore-no-null
    {
        return arguments;
    }

    // Hudos spawn arguments exclude argv0; Process__setup_with_args supplies it from the executable path.
    for (size_t index = POSIX_FIRST_HUNDOS_ARGUMENT_INDEX; argv[index] != NULL; index++) // hundelint: ignore-no-null
    {
        match_result(CollectionCode, SpawnArguments__push(&arguments, bstring(argv[index])),
            success_do_nothing(),
            error(code, message) { return arguments; }
        );
    }

    return arguments;
}

Result(SpawnResult) POSIX__spawnv(SpawnMode mode, const char * path, char * const argv[]) // hundelint: ignore-no-raw-char-buffer
{
    if (path == NULL) // hundelint: ignore-no-null
    {
        return Error(SpawnResult, posix_spawn_no_path, SPAWN_NO_PATH);
    }

    BString spawn_path = bstring(path);
    SpawnArguments arguments = POSIX__spawn_arguments_after_argv0(argv);
    return OS__spawn_argv(mode, &spawn_path, &arguments);
}
