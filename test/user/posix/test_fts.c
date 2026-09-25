#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "unity.h"

#include "../../../user/lib/newlib/include/fts.h"
#include "../../../user/lib/newlib/include/stdlib.h"

typedef struct FakeDirectory
{
    const char * const * entries;
    size_t count;
    size_t index;
    struct dirent entry;
} FakeDirectory;

static const char * const root_entries[] = {"a", "root-file"};
static const char * const child_entries[] = {"deep"};
static FakeDirectory current_directory;
static size_t opened_directory_count;
static size_t stat_count;
static bool root_file_missing;

void * reallocarray(void * address, size_t count, size_t size)
{
    return realloc(address, count * size);
}

static DIR * FakeFS__opendir(const char * path)
{
    opened_directory_count++;
    if (strcmp(path, "/") == 0)
    {
        current_directory =
            (FakeDirectory){.entries = root_entries, .count = sizeof(root_entries) / sizeof(root_entries[0])};
        return (DIR *)&current_directory;
    }
    if (strcmp(path, "/a") == 0)
    {
        current_directory =
            (FakeDirectory){.entries = child_entries, .count = sizeof(child_entries) / sizeof(child_entries[0])};
        return (DIR *)&current_directory;
    }
    errno = ENOENT;
    return NULL;
}

static struct dirent * FakeFS__readdir(DIR * directory)
{
    FakeDirectory * self = (FakeDirectory *)directory;
    if (self->index >= self->count)
    {
        return NULL;
    }
    memset(&self->entry, 0, sizeof(self->entry));
    const char * name = self->entries[self->index++];
    self->entry.d_type = strcmp(name, "a") == 0 ? DT_DIR : DT_REG;
    memcpy(self->entry.d_name, name, strlen(name) + 1);
    return &self->entry;
}

static int FakeFS__closedir(DIR * directory)
{
    return 0;
}

static int FakeFS__lstat(const char * path, struct stat * status)
{
    stat_count++;
    memset(status, 0, sizeof(*status));
    if (strcmp(path, "/") == 0 || strcmp(path, "/a") == 0)
    {
        status->st_mode = S_IFDIR | 0755;
        return 0;
    }
    if (strcmp(path, "/root-file") == 0 || strcmp(path, "/a/deep") == 0)
    {
        if (root_file_missing && strcmp(path, "/root-file") == 0)
        {
            errno = ENOENT;
            return -1;
        }
        status->st_mode = S_IFREG | 0644;
        return 0;
    }
    errno = ENOENT;
    return -1;
}

#define opendir FakeFS__opendir
#define readdir FakeFS__readdir
#define closedir FakeFS__closedir
#define lstat FakeFS__lstat
#include "../../../user/lib/newlib/fts.c"
#undef opendir
#undef readdir
#undef closedir
#undef lstat

void setUp(void)
{
    current_directory = (FakeDirectory){0};
    opened_directory_count = 0;
    stat_count = 0;
    root_file_missing = false;
    errno = 0;
}

void tearDown(void)
{
}

void test_fts_skip_does_not_open_descendant_directories(void)
{
    char root[] = "/";
    char * paths[] = {root, NULL};
    FTS * stream = fts_open(paths, FTS_PHYSICAL, NULL);

    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL_UINT64(0, opened_directory_count);
    TEST_ASSERT_EQUAL_STRING("/", fts_children(stream, 0)->fts_name);

    FTSENT * entry = fts_read(stream);
    TEST_ASSERT_EQUAL(FTS_D, entry->fts_info);
    TEST_ASSERT_EQUAL_UINT64(1, opened_directory_count);
    TEST_ASSERT_EQUAL_STRING("a", fts_children(stream, FTS_NAMEONLY)->fts_name);

    TEST_ASSERT_EQUAL_INT(0, fts_set(stream, entry, FTS_SKIP));
    TEST_ASSERT_NULL(fts_read(stream));
    TEST_ASSERT_EQUAL_UINT64(1, opened_directory_count);
    TEST_ASSERT_EQUAL_INT(0, fts_close(stream));
}

void test_fts_recursive_walk_emits_directories_in_postorder(void)
{
    char root[] = "/";
    char * paths[] = {root, NULL};
    FTS * stream = fts_open(paths, FTS_PHYSICAL, NULL);

    const char * expected_paths[] = {"/", "/a", "/a/deep", "/a", "/root-file", "/"};
    int expected_info[] = {FTS_D, FTS_D, FTS_F, FTS_DP, FTS_F, FTS_DP};
    for (size_t index = 0; index < sizeof(expected_info) / sizeof(expected_info[0]); index++)
    {
        FTSENT * entry = fts_read(stream);
        TEST_ASSERT_NOT_NULL(entry);
        TEST_ASSERT_EQUAL_STRING(expected_paths[index], entry->fts_path);
        TEST_ASSERT_EQUAL(expected_info[index], entry->fts_info);
    }
    TEST_ASSERT_NULL(fts_read(stream));
    TEST_ASSERT_EQUAL_UINT64(2, opened_directory_count);
    TEST_ASSERT_EQUAL_INT(0, fts_close(stream));
}

void test_fts_nostat_uses_directory_types_without_stating_children(void)
{
    char root[] = "/";
    char * paths[] = {root, NULL};
    FTS * stream = fts_open(paths, FTS_PHYSICAL | FTS_NOSTAT, NULL);

    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL_UINT64(1, stat_count);

    FTSENT * root_entry = fts_read(stream);
    TEST_ASSERT_EQUAL(FTS_D, root_entry->fts_info);
    TEST_ASSERT_EQUAL_UINT64(1, stat_count);

    FTSENT * child = fts_children(stream, FTS_NAMEONLY);
    TEST_ASSERT_NOT_NULL(child);
    TEST_ASSERT_EQUAL_STRING("a", child->fts_name);
    TEST_ASSERT_EQUAL(FTS_D, child->fts_info);
    TEST_ASSERT_NOT_NULL(child->fts_link);
    TEST_ASSERT_EQUAL_STRING("root-file", child->fts_link->fts_name);
    TEST_ASSERT_EQUAL(FTS_NSOK, child->fts_link->fts_info);
    TEST_ASSERT_EQUAL_UINT64(1, stat_count);

    TEST_ASSERT_EQUAL_INT(0, fts_close(stream));
}

void test_fts_child_stat_failure_does_not_mark_parent_unreadable(void)
{
    root_file_missing = true;
    char root[] = "/";
    char * paths[] = {root, NULL};
    FTS * stream = fts_open(paths, FTS_PHYSICAL, NULL);

    TEST_ASSERT_NOT_NULL(stream);
    FTSENT * root_entry = fts_read(stream);
    TEST_ASSERT_NOT_NULL(root_entry);
    TEST_ASSERT_EQUAL(FTS_D, root_entry->fts_info);

    FTSENT * child = fts_children(stream, 0);
    TEST_ASSERT_NOT_NULL(child);
    TEST_ASSERT_EQUAL_STRING("a", child->fts_name);
    TEST_ASSERT_NOT_NULL(child->fts_link);
    TEST_ASSERT_EQUAL_STRING("root-file", child->fts_link->fts_name);
    TEST_ASSERT_EQUAL(FTS_NS, child->fts_link->fts_info);
    TEST_ASSERT_EQUAL(ENOENT, child->fts_link->fts_errno);
    TEST_ASSERT_EQUAL_INT(0, fts_close(stream));
}
