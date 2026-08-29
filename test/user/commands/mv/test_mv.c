#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"

typedef struct FakeCall
{
    IOOperation op;
    char path_a[128];
    char path_b[128];
    Flags flags;
    IOHandle handle;
    size_t size;
}
FakeCall;

static FakeCall calls[64];
static size_t call_count;
static char output[512];
static uint64_t output_position;
static int scenario;
static int file_read_count;
static int dir_child_read_count;

#define main mv_main_under_test
#include "../../../../user/commands/mv/main.c"
#include "../../../../user/sys/io.c"

enum
{
    MV_SCENARIO_FILE = 1,
    MV_SCENARIO_DIR = 2,
};

void setUp(void)
{
    call_count = 0;
    output_position = 0;
    output[0] = '\0';
    scenario = 0;
    file_read_count = 0;
    dir_child_read_count = 0;
    memset(calls, 0, sizeof(calls));
}

void tearDown(void)
{
}

static void copy_text(char * target, const char * source, size_t target_size)
{
    size_t position = 0;
    while (position + 1 < target_size && source[position] != '\0')
    {
        target[position] = source[position];
        position++;
    }
    target[position] = '\0';
}

static void record_call(IOOperation op, const char * path_a, const char * path_b, Flags flags, IOHandle handle, size_t size)
{
    calls[call_count].op = op;
    calls[call_count].flags = flags;
    calls[call_count].handle = handle;
    calls[call_count].size = size;
    if (path_a != NULL)
    {
        copy_text(calls[call_count].path_a, path_a, sizeof(calls[call_count].path_a));
    }
    if (path_b != NULL)
    {
        copy_text(calls[call_count].path_b, path_b, sizeof(calls[call_count].path_b));
    }
    call_count++;
}

Anything syscall(SystemCall call, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    TEST_ASSERT_EQUAL(IO_CALL, call);
    IOOperation op = (IOOperation) arg1;

    if (op == IO_OPEN)
    {
        IOPathRequestPtr request = (IOPathRequestPtr) arg2;
        const char * path = BString__cstr(&request->path);
        Flags flags = request->flags;
        record_call(op, path, NULL, flags, 0, 0);

        if (scenario == MV_SCENARIO_FILE)
        {
            if (strcmp(path, "/src.txt") == 0 && flags == IO_OPEN_READ)
            {
                return 11;
            }
            if (strcmp(path, "/dst.txt") == 0 && flags == IO_OPEN_WRITE)
            {
                return 12;
            }
        }

        if (scenario == MV_SCENARIO_DIR)
        {
            if (strcmp(path, "/srcdir") == 0 && flags == IO_OPEN_READ)
            {
                return IO_INVALID_PATH;
            }
            if (strcmp(path, "/srcdir/file.txt") == 0 && flags == IO_OPEN_READ)
            {
                return 21;
            }
            if (strcmp(path, "/destdir/file.txt") == 0 && flags == IO_OPEN_WRITE)
            {
                return 22;
            }
        }

        return IO_INVALID_PATH;
    }

    if (op == IO_LIST_DIR)
    {
        IOListDirRequestPtr request = (IOListDirRequestPtr) arg2;
        const char * path = BString__cstr(&request->path);
        record_call(op, path, NULL, 0, 0, request->max_entries);

        if (scenario == MV_SCENARIO_DIR)
        {
            DirectoryEntryPtr entries = request->entries;
            if (strcmp(path, "/srcdir") == 0)
            {
                BString file_name = bstr("file.txt");
                entries[0] = DirectoryEntry__new(0, 0, 0, 0, &file_name);
                return 1;
            }
            if (strcmp(path, "/srcdir/file.txt") == 0)
            {
                return IO_INVALID_PATH;
            }
            return 0;
        }

        return IO_INVALID_PATH;
    }

    if (op == IO_READ)
    {
        IOHandle handle = (IOHandle) arg2;
        char * buffer = (char *) arg3;
        record_call(op, NULL, NULL, 0, handle, (size_t) arg4);

        if (scenario == MV_SCENARIO_FILE && handle == 11)
        {
            if (file_read_count == 0)
            {
                memcpy(buffer, "hi", 2);
                file_read_count++;
                return 2;
            }
            return IO_EOF;
        }

        if (scenario == MV_SCENARIO_DIR && handle == 21)
        {
            if (dir_child_read_count == 0)
            {
                memcpy(buffer, "woof", 4);
                dir_child_read_count++;
                return 4;
            }
            return IO_EOF;
        }

        return IO_EOF;
    }

    if (op == IO_WRITE)
    {
        IOHandle handle = (IOHandle) arg2;
        const char * buffer = (const char *) arg3;
        size_t size = (size_t) arg4;
        record_call(op, buffer, NULL, 0, handle, size);
        return (Anything) size;
    }

    if (op == IO_CLOSE)
    {
        IOHandle handle = (IOHandle) arg2;
        record_call(op, NULL, NULL, 0, handle, 0);
        return IO_OK;
    }

    if (op == IO_MKDIR)
    {
        IOPathRequestPtr request = (IOPathRequestPtr) arg2;
        const char * path = BString__cstr(&request->path);
        record_call(op, path, NULL, 0, 0, 0);
        return IO_OK;
    }

    if (op == IO_REMOVE)
    {
        IOPathRequestPtr request = (IOPathRequestPtr) arg2;
        const char * path = BString__cstr(&request->path);
        Flags flags = request->flags;
        record_call(op, path, NULL, flags, 0, 0);
        return IO_OK;
    }

    return IO_PANIC;
}

void IO__print(ConstBStringPtr message)
{
    const char * str = BString__cstr(message);
    while (*str != '\0' && output_position + 1 < sizeof(output))
    {
        output[output_position++] = *str++;
    }
    output[output_position] = '\0';
}

int IO__printf(ConstBStringPtr format, ...)
{
    va_list args;
    va_start(args, format);
    BString formatted = BString__vsprintf(format, args);
    va_end(args);
    StringPos written = BString__copy_to(&formatted, output + output_position, sizeof(output) - output_position);
    if (written > 0)
    {
        output_position += (uint64_t) written;
    }
    return (int) written;
}

void test_mv_moves_a_file_via_copy_then_remove(void)
{
    scenario = MV_SCENARIO_FILE;
    char * argv[] = { "mv", "src.txt", "dst.txt" };

    TEST_ASSERT_EQUAL_INT(0, mv_main_under_test(3, argv));
    TEST_ASSERT_EQUAL_INT(8, call_count);
    TEST_ASSERT_EQUAL(IO_OPEN, calls[0].op);
    TEST_ASSERT_EQUAL_STRING("/src.txt", calls[0].path_a);
    TEST_ASSERT_EQUAL(IO_OPEN, calls[1].op);
    TEST_ASSERT_EQUAL_STRING("/dst.txt", calls[1].path_a);
    TEST_ASSERT_EQUAL(IO_WRITE, calls[3].op);
    TEST_ASSERT_EQUAL_MEMORY("hi", calls[3].path_a, 2);
    TEST_ASSERT_EQUAL(IO_REMOVE, calls[7].op);
    TEST_ASSERT_EQUAL_STRING("/src.txt", calls[7].path_a);
}

void test_mv_moves_directory_tree_recursively(void)
{
    scenario = MV_SCENARIO_DIR;
    char * argv[] = { "mv", "/srcdir", "/destdir" };

    TEST_ASSERT_EQUAL_INT(0, mv_main_under_test(3, argv));
    TEST_ASSERT_EQUAL(IO_LIST_DIR, calls[1].op);
    TEST_ASSERT_EQUAL_STRING("/srcdir", calls[1].path_a);
    TEST_ASSERT_EQUAL(IO_MKDIR, calls[2].op);
    TEST_ASSERT_EQUAL_STRING("/destdir", calls[2].path_a);
    TEST_ASSERT_EQUAL(IO_OPEN, calls[3].op);
    TEST_ASSERT_EQUAL_STRING("/srcdir/file.txt", calls[3].path_a);
    TEST_ASSERT_EQUAL(IO_OPEN, calls[4].op);
    TEST_ASSERT_EQUAL_STRING("/destdir/file.txt", calls[4].path_a);
    TEST_ASSERT_EQUAL(IO_REMOVE, calls[10].op);
    TEST_ASSERT_EQUAL_STRING("/srcdir/file.txt", calls[10].path_a);
    TEST_ASSERT_EQUAL(IO_REMOVE, calls[11].op);
    TEST_ASSERT_EQUAL_STRING("/srcdir", calls[11].path_a);
    TEST_ASSERT_EQUAL(IO_REMOVE_RECURSIVE, calls[11].flags);
}
