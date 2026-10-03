#include "unity.h"

#include <errno.h>
#include <string.h>

#include "../../../user/lib/newlib/environment.c"

static char startup_path[] = "PATH=/bin";
static char startup_home[] = "HOME=/home/user";
static char duplicate_home[] = "HOME=/tmp";
static char * startup_environment[] = {startup_path, startup_home, duplicate_home, NULL};
static char putenv_entry[32];

void setUp(void)
{
    errno = 0;
    strcpy(putenv_entry, "MODE=initial");
    LibC__set_environment(startup_environment);
}

void tearDown(void)
{
}

void test_getenv_reads_the_startup_environment(void)
{
    TEST_ASSERT_EQUAL_STRING("/bin", LibC__getenv("PATH"));
    TEST_ASSERT_EQUAL_STRING("/home/user", LibC__getenv("HOME"));
    TEST_ASSERT_NULL(LibC__getenv("MISSING"));
}

void test_setenv_replaces_and_appends_owned_entries(void)
{
    TEST_ASSERT_EQUAL_INT(0, LibC__setenv("HOME", "/root", 1));
    TEST_ASSERT_EQUAL_STRING("/root", LibC__getenv("HOME"));
    TEST_ASSERT_EQUAL_INT(0, LibC__setenv("TERM", "hunde", 1));
    TEST_ASSERT_EQUAL_STRING("hunde", LibC__getenv("TERM"));

    TEST_ASSERT_EQUAL_INT(0, LibC__setenv("HOME", "/ignored", 0));
    TEST_ASSERT_EQUAL_STRING("/root", LibC__getenv("HOME"));
}

void test_unsetenv_removes_every_entry_with_the_name(void)
{
    TEST_ASSERT_EQUAL_INT(0, LibC__unsetenv("HOME"));
    TEST_ASSERT_NULL(LibC__getenv("HOME"));
    TEST_ASSERT_EQUAL_STRING("/bin", LibC__getenv("PATH"));
}

void test_putenv_retains_the_callers_string(void)
{
    TEST_ASSERT_EQUAL_INT(0, LibC__putenv(putenv_entry));
    TEST_ASSERT_EQUAL_STRING("initial", LibC__getenv("MODE"));

    strcpy(putenv_entry, "MODE=changed");
    TEST_ASSERT_EQUAL_STRING("changed", LibC__getenv("MODE"));
}

void test_environment_mutators_reject_invalid_names(void)
{
    TEST_ASSERT_EQUAL_INT(-1, LibC__setenv("BAD=NAME", "value", 1));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, LibC__unsetenv(""));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);

    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, LibC__putenv("missing-separator"));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}
