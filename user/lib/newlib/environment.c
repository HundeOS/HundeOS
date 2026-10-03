#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "runtime.h"

typedef struct EnvironmentStorage
{
    char ** values;
    bool * owned;
    size_t count;
    size_t capacity;
    bool managed;
} EnvironmentStorage;

typedef EnvironmentStorage * EnvironmentStoragePtr;

static char * empty_environment[] = {NULL};
static EnvironmentStorage storage;
char ** environ = empty_environment;

static void EnvironmentStorage__release(EnvironmentStoragePtr self)
{
    if (!self->managed)
    {
        return;
    }
    for (size_t index = 0; index < self->count; index++)
    {
        if (self->owned[index])
        {
            free(self->values[index]);
        }
    }
    free(self->owned);
    free(self->values);
    *self = (EnvironmentStorage){0};
}

void LibC__set_environment(char ** environment)
{
    EnvironmentStorage__release(&storage);
    environ = environment == NULL ? empty_environment : environment;
}

static size_t EnvironmentStorage__count_values(char ** values)
{
    size_t count = 0;
    while (values[count] != NULL)
    {
        count++;
    }
    return count;
}

static bool EnvironmentStorage__manage(EnvironmentStoragePtr self)
{
    if (self->managed)
    {
        return true;
    }

    size_t count = EnvironmentStorage__count_values(environ);
    size_t capacity = count + 1;
    char ** values = malloc(sizeof(*values) * (capacity + 1));
    bool * owned = calloc(capacity, sizeof(*owned));
    if (values == NULL || owned == NULL)
    {
        free(values);
        free(owned);
        errno = ENOMEM;
        return false;
    }
    for (size_t index = 0; index < count; index++)
    {
        values[index] = environ[index];
    }
    values[count] = NULL;
    self->values = values;
    self->owned = owned;
    self->count = count;
    self->capacity = capacity;
    self->managed = true;
    environ = values;
    return true;
}

static bool EnvironmentStorage__reserve(EnvironmentStoragePtr self, size_t needed)
{
    if (needed <= self->capacity)
    {
        return true;
    }

    size_t capacity = self->capacity * 2;
    while (capacity < needed)
    {
        capacity *= 2;
    }
    char ** values = malloc(sizeof(*values) * (capacity + 1));
    bool * owned = calloc(capacity, sizeof(*owned));
    if (values == NULL || owned == NULL)
    {
        free(values);
        free(owned);
        errno = ENOMEM;
        return false;
    }
    for (size_t index = 0; index < self->count; index++)
    {
        values[index] = self->values[index];
        owned[index] = self->owned[index];
    }
    values[self->count] = NULL;
    free(self->values);
    free(self->owned);
    self->values = values;
    self->owned = owned;
    self->capacity = capacity;
    environ = values;
    return true;
}

static bool EnvironmentName__valid(const char * name)
{
    return name != NULL && name[0] != '\0' && strchr(name, '=') == NULL;
}

static size_t EnvironmentStorage__find(EnvironmentStoragePtr self, const char * name, size_t name_length)
{
    for (size_t index = 0; index < self->count; index++)
    {
        if (strncmp(self->values[index], name, name_length) == 0 && self->values[index][name_length] == '=')
        {
            return index;
        }
    }
    return self->count;
}

static void EnvironmentStorage__remove_at(EnvironmentStoragePtr self, size_t index)
{
    if (self->owned[index])
    {
        free(self->values[index]);
    }
    for (size_t cursor = index + 1; cursor < self->count; cursor++)
    {
        self->values[cursor - 1] = self->values[cursor];
        self->owned[cursor - 1] = self->owned[cursor];
    }
    self->count--;
    self->values[self->count] = NULL;
    self->owned[self->count] = false;
}

char * LibC__getenv(const char * name)
{
    if (!EnvironmentName__valid(name))
    {
        return NULL;
    }
    size_t name_length = strlen(name);
    for (size_t index = 0; environ[index] != NULL; index++)
    {
        if (strncmp(environ[index], name, name_length) == 0 && environ[index][name_length] == '=')
        {
            return environ[index] + name_length + 1;
        }
    }
    return NULL;
}

int LibC__setenv(const char * name, const char * value, int overwrite)
{
    if (!EnvironmentName__valid(name) || value == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (!EnvironmentStorage__manage(&storage))
    {
        return -1;
    }

    size_t name_length = strlen(name);
    size_t index = EnvironmentStorage__find(&storage, name, name_length);
    if (index < storage.count && overwrite == 0)
    {
        return 0;
    }
    size_t value_length = strlen(value);
    if (name_length > SIZE_MAX - value_length - 2)
    {
        errno = ENOMEM;
        return -1;
    }
    char * entry = malloc(name_length + value_length + 2);
    if (entry == NULL)
    {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, name_length);
    entry[name_length] = '=';
    memcpy(entry + name_length + 1, value, value_length + 1);

    if (index == storage.count)
    {
        if (!EnvironmentStorage__reserve(&storage, storage.count + 1))
        {
            free(entry);
            return -1;
        }
        storage.count++;
        storage.values[storage.count] = NULL;
    }
    else if (storage.owned[index])
    {
        free(storage.values[index]);
    }
    storage.values[index] = entry;
    storage.owned[index] = true;
    return 0;
}

int LibC__unsetenv(const char * name)
{
    if (!EnvironmentName__valid(name))
    {
        errno = EINVAL;
        return -1;
    }
    if (!EnvironmentStorage__manage(&storage))
    {
        return -1;
    }

    size_t name_length = strlen(name);
    size_t index = EnvironmentStorage__find(&storage, name, name_length);
    while (index < storage.count)
    {
        EnvironmentStorage__remove_at(&storage, index);
        index = EnvironmentStorage__find(&storage, name, name_length);
    }
    return 0;
}

int LibC__putenv(char * string)
{
    if (string == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    char * separator = strchr(string, '=');
    if (separator == NULL || separator == string)
    {
        errno = EINVAL;
        return -1;
    }
    if (!EnvironmentStorage__manage(&storage))
    {
        return -1;
    }

    size_t name_length = (size_t)(separator - string);
    size_t index = EnvironmentStorage__find(&storage, string, name_length);
    if (index < storage.count && storage.values[index] == string)
    {
        return 0;
    }
    if (index == storage.count)
    {
        if (!EnvironmentStorage__reserve(&storage, storage.count + 1))
        {
            return -1;
        }
        storage.count++;
        storage.values[storage.count] = NULL;
    }
    else if (storage.owned[index])
    {
        free(storage.values[index]);
    }
    storage.values[index] = string;
    storage.owned[index] = false;

    size_t duplicate = index + 1;
    while (duplicate < storage.count)
    {
        if (strncmp(storage.values[duplicate], string, name_length) == 0 &&
            storage.values[duplicate][name_length] == '=')
        {
            EnvironmentStorage__remove_at(&storage, duplicate);
        }
        else
        {
            duplicate++;
        }
    }
    return 0;
}

char * __attribute__((weak)) getenv(const char * name)
{
    return LibC__getenv(name);
}

int __attribute__((weak)) setenv(const char * name, const char * value, int overwrite)
{
    return LibC__setenv(name, value, overwrite);
}

int __attribute__((weak)) unsetenv(const char * name)
{
    return LibC__unsetenv(name);
}

int __attribute__((weak)) putenv(char * string)
{
    return LibC__putenv(string);
}
