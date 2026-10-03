#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
    TEMPFILE_SUFFIX_LENGTH = 6,
    TEMPFILE_CANDIDATE_ATTEMPTS = 1024,
};

static const char tempfile_characters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static uint64_t tempfile_sequence;
static bool tempfile_sequence_initialized;

static char * TempFile__suffix(char * path_template)
{
    if (path_template == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    size_t length = strlen(path_template);
    if (length < TEMPFILE_SUFFIX_LENGTH)
    {
        errno = EINVAL;
        return NULL;
    }

    char * suffix = path_template + length - TEMPFILE_SUFFIX_LENGTH;
    for (size_t index = 0; index < TEMPFILE_SUFFIX_LENGTH; index++)
    {
        if (suffix[index] != 'X')
        {
            errno = EINVAL;
            return NULL;
        }
    }
    return suffix;
}

static uint64_t TempFile__next_sequence(void)
{
    if (!tempfile_sequence_initialized)
    {
        tempfile_sequence = ((uint64_t)(uint32_t)getpid() << 32) ^ (uint32_t)rand();
        tempfile_sequence_initialized = true;
    }
    return tempfile_sequence++;
}

static void TempFile__write_suffix(char * suffix)
{
    uint64_t sequence = TempFile__next_sequence();
    size_t character_count = sizeof(tempfile_characters) - 1;
    for (size_t index = TEMPFILE_SUFFIX_LENGTH; index > 0; index--)
    {
        suffix[index - 1] = tempfile_characters[sequence % character_count];
        sequence /= character_count;
    }
}

char * mktemp(char * path_template)
{
    char * suffix = TempFile__suffix(path_template);
    if (suffix == NULL)
    {
        if (path_template != NULL)
        {
            path_template[0] = '\0';
        }
        return path_template;
    }

    for (size_t attempt = 0; attempt < TEMPFILE_CANDIDATE_ATTEMPTS; attempt++)
    {
        struct stat status;
        TempFile__write_suffix(suffix);
        if (lstat(path_template, &status) < 0)
        {
            if (errno == ENOENT)
            {
                return path_template;
            }
            path_template[0] = '\0';
            return path_template;
        }
    }

    errno = EEXIST;
    path_template[0] = '\0';
    return path_template;
}

int mkstemp(char * path_template)
{
    char * suffix = TempFile__suffix(path_template);
    if (suffix == NULL)
    {
        return -1;
    }

    for (size_t attempt = 0; attempt < TEMPFILE_CANDIDATE_ATTEMPTS; attempt++)
    {
        TempFile__write_suffix(suffix);
        int descriptor = open(path_template, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (descriptor >= 0 || errno != EEXIST)
        {
            return descriptor;
        }
    }

    errno = EEXIST;
    return -1;
}

char * mkdtemp(char * path_template)
{
    char * suffix = TempFile__suffix(path_template);
    if (suffix == NULL)
    {
        return NULL;
    }

    for (size_t attempt = 0; attempt < TEMPFILE_CANDIDATE_ATTEMPTS; attempt++)
    {
        TempFile__write_suffix(suffix);
        if (mkdir(path_template, 0700) == 0)
        {
            return path_template;
        }
        if (errno != EEXIST)
        {
            return NULL;
        }
    }

    errno = EEXIST;
    return NULL;
}
