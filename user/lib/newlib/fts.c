#include <dirent.h>
#include <errno.h>
#include <fts.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct PosixFTSDirectory
{
    FTSENT * entry;
    FTSENT ** children;
    size_t child_count;
} PosixFTSDirectory;

typedef struct PosixFTSFrame
{
    FTSENT * entry;
    size_t next_child;
} PosixFTSFrame;

typedef struct PosixFTS
{
    FTS public;
    FTSENT ** owned;
    size_t owned_count;
    size_t owned_capacity;
    PosixFTSDirectory * directories;
    size_t directory_count;
    size_t directory_capacity;
    PosixFTSFrame * frames;
    size_t frame_count;
    size_t frame_capacity;
    FTSENT ** root_items;
    size_t root_count;
    size_t root_capacity;
    size_t next_root;
    FTSENT * roots;
    int (*compare)(const FTSENT **, const FTSENT **);
} PosixFTS;

static int PosixFTS__grow(void ** items, size_t * capacity, size_t item_size, size_t required)
{
    if (required <= *capacity)
    {
        return 0;
    }
    size_t next_capacity = *capacity == 0 ? 16 : *capacity * 2;
    while (next_capacity < required)
    {
        next_capacity *= 2;
    }
    void * resized = reallocarray(*items, next_capacity, item_size);
    if (resized == NULL)
    {
        errno = ENOMEM;
        return -1;
    }
    *items = resized;
    *capacity = next_capacity;
    return 0;
}

static char * PosixFTS__copy_string(const char * value)
{
    size_t length = strlen(value) + 1;
    char * copy = malloc(length);
    if (copy != NULL)
    {
        memcpy(copy, value, length);
    }
    return copy;
}

static const char * PosixFTS__name(const char * path)
{
    size_t length = strlen(path);
    if (length == 1 && path[0] == '/')
    {
        return path;
    }
    while (length > 1 && path[length - 1] == '/')
    {
        length--;
    }
    while (length > 0 && path[length - 1] != '/')
    {
        length--;
    }
    return path + length;
}

static FTSENT * PosixFTS__new_entry(PosixFTS * self, const char * path, const char * name, int level, FTSENT * parent,
                                    unsigned char directory_type)
{
    size_t name_length = strlen(name);
    FTSENT * entry = calloc(1, sizeof(*entry) + name_length);
    struct stat * status = calloc(1, sizeof(*status));
    char * path_copy = PosixFTS__copy_string(path);
    if (entry == NULL || status == NULL || path_copy == NULL ||
        PosixFTS__grow((void **)&self->owned, &self->owned_capacity, sizeof(*self->owned), self->owned_count + 1) != 0)
    {
        free(entry);
        free(status);
        free(path_copy);
        return NULL;
    }
    memcpy(entry->fts_name, name, name_length + 1);
    entry->fts_namelen = name_length;
    entry->fts_path = path_copy;
    entry->fts_accpath = path_copy;
    entry->fts_pathlen = strlen(path_copy);
    entry->fts_level = level;
    entry->fts_parent = parent;
    entry->fts_statp = status;
    if ((self->public.fts_options & FTS_NOSTAT) != 0 && directory_type != DT_UNKNOWN)
    {
        if (directory_type == DT_DIR)
        {
            entry->fts_info = FTS_D;
        }
        else if (directory_type == DT_LNK)
        {
            entry->fts_info = FTS_SL;
        }
        else
        {
            entry->fts_info = FTS_NSOK;
        }
    }
    else if (lstat(path, status) != 0)
    {
        entry->fts_errno = errno;
        entry->fts_info = FTS_NS;
    }
    else
    {
        entry->fts_ino = status->st_ino;
        entry->fts_dev = status->st_dev;
        entry->fts_nlink = status->st_nlink;
        entry->fts_info = S_ISDIR(status->st_mode) ? FTS_D : S_ISLNK(status->st_mode) ? FTS_SL : FTS_F;
        if ((self->public.fts_options & FTS_NOSTAT) != 0 && entry->fts_info == FTS_F)
        {
            entry->fts_info = FTS_NSOK;
        }
    }
    self->owned[self->owned_count++] = entry;
    return entry;
}

static int PosixFTS__compare(PosixFTS * self, FTSENT * left, FTSENT * right)
{
    if (self->compare == NULL)
    {
        return 0;
    }
    const FTSENT * left_value = left;
    const FTSENT * right_value = right;
    return self->compare(&left_value, &right_value);
}

static void PosixFTS__sort(PosixFTS * self, FTSENT ** entries, size_t count)
{
    for (size_t index = 1; index < count; index++)
    {
        FTSENT * value = entries[index];
        size_t position = index;
        while (position > 0 && PosixFTS__compare(self, entries[position - 1], value) > 0)
        {
            entries[position] = entries[position - 1];
            position--;
        }
        entries[position] = value;
    }
}

static char * PosixFTS__join(const char * parent, const char * name)
{
    size_t parent_length = strlen(parent);
    size_t name_length = strlen(name);
    int separator = parent_length > 0 && parent[parent_length - 1] != '/';
    if (parent_length + (size_t)separator + name_length + 1 > PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return NULL;
    }
    char * path = malloc(parent_length + (size_t)separator + name_length + 1);
    if (path == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(path, parent, parent_length);
    if (separator)
    {
        path[parent_length++] = '/';
    }
    memcpy(path + parent_length, name, name_length + 1);
    return path;
}

static PosixFTSDirectory * PosixFTS__find_directory(PosixFTS * self, FTSENT * entry)
{
    for (size_t index = 0; index < self->directory_count; index++)
    {
        if (self->directories[index].entry == entry)
        {
            return &self->directories[index];
        }
    }
    return NULL;
}

static PosixFTSDirectory * PosixFTS__load_directory(PosixFTS * self, FTSENT * entry)
{
    PosixFTSDirectory * existing = PosixFTS__find_directory(self, entry);
    if (existing != NULL)
    {
        return existing;
    }

    DIR * directory = opendir(entry->fts_path);
    if (directory == NULL)
    {
        return NULL;
    }

    FTSENT ** children = NULL;
    size_t child_count = 0;
    size_t child_capacity = 0;
    struct dirent * item;
    int read_error;
    while (true)
    {
        errno = 0;
        item = readdir(directory);
        if (item == NULL)
        {
            read_error = errno;
            break;
        }
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0)
        {
            continue;
        }
        char * child_path = PosixFTS__join(entry->fts_path, item->d_name);
        FTSENT * child = child_path == NULL
                             ? NULL
                             : PosixFTS__new_entry(self, child_path, item->d_name, entry->fts_level + 1, entry,
                                                   item->d_type);
        free(child_path);
        if (child == NULL ||
            PosixFTS__grow((void **)&children, &child_capacity, sizeof(*children), child_count + 1) != 0)
        {
            free(children);
            closedir(directory);
            return NULL;
        }
        children[child_count++] = child;
    }
    closedir(directory);
    if (read_error != 0)
    {
        free(children);
        errno = read_error;
        return NULL;
    }

    PosixFTS__sort(self, children, child_count);
    for (size_t index = 0; index < child_count; index++)
    {
        children[index]->fts_link = index + 1 < child_count ? children[index + 1] : NULL;
    }
    if (PosixFTS__grow((void **)&self->directories, &self->directory_capacity, sizeof(*self->directories),
                       self->directory_count + 1) != 0)
    {
        free(children);
        return NULL;
    }
    self->directories[self->directory_count++] =
        (PosixFTSDirectory){.entry = entry, .children = children, .child_count = child_count};
    return &self->directories[self->directory_count - 1];
}

static FTSENT * PosixFTS__emit(PosixFTS * self, FTSENT * entry, unsigned short info)
{
    entry->fts_info = info;
    entry->fts_instr = FTS_NOINSTR;
    self->public.fts_cur = entry;
    errno = 0;
    return entry;
}

static FTSENT * PosixFTS__prepare_entry(PosixFTS * self, FTSENT * entry)
{
    unsigned short info = entry->fts_info;
    if (info != FTS_D)
    {
        return PosixFTS__emit(self, entry, info);
    }

    PosixFTSDirectory * directory = PosixFTS__load_directory(self, entry);
    if (directory == NULL)
    {
        entry->fts_errno = errno;
        return PosixFTS__emit(self, entry, FTS_DNR);
    }
    if (PosixFTS__grow((void **)&self->frames, &self->frame_capacity, sizeof(*self->frames), self->frame_count + 1) != 0)
    {
        return NULL;
    }
    self->frames[self->frame_count++] = (PosixFTSFrame){.entry = entry};
    return PosixFTS__emit(self, entry, FTS_D);
}

static FTSENT * PosixFTS__next_after_current(PosixFTS * self)
{
    if (self->frame_count > 0)
    {
        PosixFTSFrame * frame = &self->frames[self->frame_count - 1];
        PosixFTSDirectory * directory = PosixFTS__find_directory(self, frame->entry);
        if (directory != NULL && frame->next_child < directory->child_count)
        {
            return PosixFTS__prepare_entry(self, directory->children[frame->next_child++]);
        }
        FTSENT * completed = frame->entry;
        self->frame_count--;
        return PosixFTS__emit(self, completed, FTS_DP);
    }
    if (self->next_root < self->root_count)
    {
        return PosixFTS__prepare_entry(self, self->root_items[self->next_root++]);
    }
    self->public.fts_cur = NULL;
    errno = 0;
    return NULL;
}

FTS * fts_open(char * const * paths, int options, int (*compar)(const FTSENT **, const FTSENT **))
{
    if (paths == NULL || paths[0] == NULL || (options & ~FTS_OPTIONMASK) != 0)
    {
        errno = EINVAL;
        return NULL;
    }
    PosixFTS * self = calloc(1, sizeof(*self));
    if (self == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    self->public.fts_options = options;
    self->compare = compar;

    for (size_t index = 0; paths[index] != NULL; index++)
    {
        FTSENT * root =
            PosixFTS__new_entry(self, paths[index], PosixFTS__name(paths[index]), FTS_ROOTLEVEL, NULL, DT_UNKNOWN);
        if (root == NULL ||
            PosixFTS__grow((void **)&self->root_items, &self->root_capacity, sizeof(*self->root_items),
                           self->root_count + 1) != 0)
        {
            fts_close(&self->public);
            return NULL;
        }
        self->root_items[self->root_count++] = root;
    }
    PosixFTS__sort(self, self->root_items, self->root_count);
    for (size_t index = 0; index < self->root_count; index++)
    {
        self->root_items[index]->fts_link = index + 1 < self->root_count ? self->root_items[index + 1] : NULL;
    }
    self->roots = self->root_items[0];
    errno = 0;
    return &self->public;
}

FTSENT * fts_children(FTS * stream, int options)
{
    PosixFTS * self = (PosixFTS *)stream;
    if (self == NULL || (options != 0 && options != FTS_NAMEONLY))
    {
        errno = EINVAL;
        return NULL;
    }
    if (self->public.fts_cur == NULL)
    {
        errno = 0;
        self->public.fts_child = self->roots;
        return self->roots;
    }
    PosixFTSDirectory * directory = PosixFTS__find_directory(self, self->public.fts_cur);
    errno = 0;
    self->public.fts_child = directory == NULL || directory->child_count == 0 ? NULL : directory->children[0];
    return self->public.fts_child;
}

FTSENT * fts_read(FTS * stream)
{
    PosixFTS * self = (PosixFTS *)stream;
    if (self == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    if (self->public.fts_cur != NULL && self->public.fts_cur->fts_info == FTS_D &&
        self->public.fts_cur->fts_instr == FTS_SKIP && self->frame_count > 0)
    {
        self->frame_count--;
    }
    return PosixFTS__next_after_current(self);
}

int fts_set(FTS * stream, FTSENT * entry, int instruction)
{
    PosixFTS * self = (PosixFTS *)stream;
    if (self == NULL || entry == NULL || (instruction != FTS_SKIP && instruction != FTS_AGAIN &&
                                           instruction != FTS_FOLLOW && instruction != FTS_NOINSTR))
    {
        errno = EINVAL;
        return -1;
    }
    entry->fts_instr = (unsigned short)instruction;
    return 0;
}

int fts_close(FTS * stream)
{
    PosixFTS * self = (PosixFTS *)stream;
    if (self == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    for (size_t index = 0; index < self->owned_count; index++)
    {
        free(self->owned[index]->fts_statp);
        free(self->owned[index]->fts_path);
        free(self->owned[index]);
    }
    for (size_t index = 0; index < self->directory_count; index++)
    {
        free(self->directories[index].children);
    }
    free(self->owned);
    free(self->directories);
    free(self->frames);
    free(self->root_items);
    free(self);
    return 0;
}
