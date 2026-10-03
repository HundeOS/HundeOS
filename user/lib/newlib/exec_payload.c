#include "exec_payload.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t PosixExecPayload__vector_count(char * const vector[])
{
    size_t count = 0;
    if (vector == NULL)
    {
        return 0;
    }
    while (vector[count] != NULL)
    {
        count++;
    }
    return count;
}

static size_t PosixExecPayload__add_size(size_t total, size_t amount)
{
    if (amount > EXEC_PAYLOAD_MAX - total)
    {
        errno = E2BIG;
        return SIZE_MAX;
    }
    return total + amount;
}

static size_t PosixExecPayload__align(size_t value, size_t alignment)
{
    size_t remainder = value % alignment;
    return remainder == 0 ? value : value + alignment - remainder;
}

static size_t PosixExecPayload__add_vector_size(size_t total, char * const vector[], size_t count)
{
    if (count > (EXEC_PAYLOAD_MAX - total) / sizeof(ExecPayloadOffset))
    {
        errno = E2BIG;
        return SIZE_MAX;
    }
    total += count * sizeof(ExecPayloadOffset);
    for (size_t index = 0; index < count; index++)
    {
        size_t length = strlen(vector[index]);
        total = PosixExecPayload__add_size(total, length + 1);
        if (total == SIZE_MAX)
        {
            return SIZE_MAX;
        }
    }
    return total;
}

static size_t PosixExecPayload__add_open_description_size(size_t total,
                                                         ConstExecPayloadOpenDescriptionPtr open_descriptions,
                                                         size_t count)
{
    total = PosixExecPayload__align(total, _Alignof(ExecPayloadOpenDescription));
    if (count > (EXEC_PAYLOAD_MAX - total) / sizeof(ExecPayloadOpenDescription))
    {
        errno = E2BIG;
        return SIZE_MAX;
    }
    total += count * sizeof(ExecPayloadOpenDescription);
    for (size_t index = 0; index < count; index++)
    {
        const char * path = open_descriptions[index].path_offset == 0 ? "" : (const char *)open_descriptions[index].path_offset;
        total = PosixExecPayload__add_size(total, strlen(path) + 1);
        if (total == SIZE_MAX)
        {
            return SIZE_MAX;
        }
    }
    return total;
}

static size_t PosixExecPayload__copy_vector(ExecPayloadPtr self, ExecPayloadOffsetPtr offsets, size_t cursor,
                                            char * const vector[], size_t count)
{
    for (size_t index = 0; index < count; index++)
    {
        size_t length = strlen(vector[index]) + 1;
        offsets[index] = cursor;
        memcpy((uint8_t *)self + cursor, vector[index], length);
        cursor += length;
    }
    return cursor;
}

ExecPayloadPtr PosixExecPayload__new(const char * path, const char * current_directory, char * const arguments[],
                                     char * const environment[],
                                     ConstExecPayloadOpenDescriptionPtr open_descriptions,
                                     size_t open_description_count, ConstExecPayloadDescriptorPtr descriptors,
                                     size_t descriptor_count)
{
    if (path == NULL || current_directory == NULL)
    {
        errno = EFAULT;
        return NULL;
    }
    size_t path_length = strlen(path);
    size_t current_directory_length = strlen(current_directory);
    if (path_length >= PATH_MAX || current_directory_length >= PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }

    size_t argument_count = PosixExecPayload__vector_count(arguments);
    size_t environment_count = PosixExecPayload__vector_count(environment);
    size_t size = sizeof(ExecPayload);
    size = PosixExecPayload__add_size(size, path_length + 1);
    if (size != SIZE_MAX)
    {
        size = PosixExecPayload__add_size(size, current_directory_length + 1);
    }
    if (size != SIZE_MAX)
    {
        size = PosixExecPayload__add_vector_size(size, arguments, argument_count);
    }
    if (size != SIZE_MAX)
    {
        size = PosixExecPayload__add_vector_size(size, environment, environment_count);
    }
    if (size != SIZE_MAX)
    {
        size = PosixExecPayload__add_open_description_size(size, open_descriptions, open_description_count);
    }
    if (size != SIZE_MAX)
    {
        size = PosixExecPayload__align(size, _Alignof(ExecPayloadDescriptor));
        if (descriptor_count > (EXEC_PAYLOAD_MAX - size) / sizeof(ExecPayloadDescriptor))
        {
            errno = E2BIG;
            size = SIZE_MAX;
        }
        else
        {
            size += descriptor_count * sizeof(ExecPayloadDescriptor);
        }
    }
    if (size == SIZE_MAX)
    {
        return NULL;
    }

    ExecPayloadPtr self = malloc(size);
    if (self == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    self->size = size;
    self->argument_count = argument_count;
    self->environment_count = environment_count;
    self->open_description_count = open_description_count;
    self->descriptor_count = descriptor_count;

    size_t cursor = sizeof(ExecPayload);
    self->arguments_offset = cursor;
    ExecPayloadOffsetPtr argument_offsets = (ExecPayloadOffsetPtr)((uint8_t *)self + cursor);
    cursor += argument_count * sizeof(ExecPayloadOffset);
    self->environment_offset = cursor;
    ExecPayloadOffsetPtr environment_offsets = (ExecPayloadOffsetPtr)((uint8_t *)self + cursor);
    cursor += environment_count * sizeof(ExecPayloadOffset);
    self->path_offset = cursor;
    memcpy((uint8_t *)self + cursor, path, path_length + 1);
    cursor += path_length + 1;
    self->current_directory_offset = cursor;
    memcpy((uint8_t *)self + cursor, current_directory, current_directory_length + 1);
    cursor += current_directory_length + 1;
    cursor = PosixExecPayload__copy_vector(self, argument_offsets, cursor, arguments, argument_count);
    cursor = PosixExecPayload__copy_vector(self, environment_offsets, cursor, environment, environment_count);

    cursor = PosixExecPayload__align(cursor, _Alignof(ExecPayloadOpenDescription));
    self->open_descriptions_offset = cursor;
    ExecPayloadOpenDescriptionPtr packed_open_descriptions =
        (ExecPayloadOpenDescriptionPtr)((uint8_t *)self + cursor);
    cursor += open_description_count * sizeof(ExecPayloadOpenDescription);
    for (size_t index = 0; index < open_description_count; index++)
    {
        packed_open_descriptions[index] = open_descriptions[index];
        const char * description_path =
            open_descriptions[index].path_offset == 0 ? "" : (const char *)open_descriptions[index].path_offset;
        size_t description_path_length = strlen(description_path) + 1;
        packed_open_descriptions[index].path_offset = cursor;
        memcpy((uint8_t *)self + cursor, description_path, description_path_length);
        cursor += description_path_length;
    }

    cursor = PosixExecPayload__align(cursor, _Alignof(ExecPayloadDescriptor));
    self->descriptors_offset = cursor;
    ExecPayloadDescriptorPtr packed_descriptors = (ExecPayloadDescriptorPtr)((uint8_t *)self + cursor);
    for (size_t index = 0; index < descriptor_count; index++)
    {
        packed_descriptors[index] = descriptors[index];
    }
    return self;
}

void PosixExecPayload__dispose(ExecPayloadPtr self)
{
    free(self);
}
