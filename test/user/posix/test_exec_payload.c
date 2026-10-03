#include "unity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "../../../user/lib/newlib/exec_payload.c"

void setUp(void)
{
    errno = 0;
}

void tearDown(void)
{
}

void test_exec_payload_packs_path_arguments_and_environment(void)
{
    char * arguments[] = {"bash", "-c", "echo ok", NULL};
    char * environment[] = {"PATH=/bin", "HOME=/home/user", NULL};

    ExecPayloadPtr payload =
        PosixExecPayload__new("/usr/local/bin/bash", "/work/project", arguments, environment, NULL, 0, NULL, 0);

    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_TRUE(ExecPayload__size(payload) <= EXEC_PAYLOAD_MAX);
    TEST_ASSERT_EQUAL_STRING("/usr/local/bin/bash", ExecPayload__path(payload));
    TEST_ASSERT_EQUAL_STRING("/work/project", ExecPayload__current_directory(payload));
    TEST_ASSERT_EQUAL_UINT64(3, ExecPayload__argument_count(payload));
    TEST_ASSERT_EQUAL_STRING("bash", ExecPayload__argument_at(payload, 0));
    TEST_ASSERT_EQUAL_STRING("echo ok", ExecPayload__argument_at(payload, 2));
    TEST_ASSERT_NULL(ExecPayload__argument_at(payload, 3));
    TEST_ASSERT_EQUAL_UINT64(2, ExecPayload__environment_count(payload));
    TEST_ASSERT_EQUAL_STRING("HOME=/home/user", ExecPayload__environment_at(payload, 1));
    PosixExecPayload__dispose(payload);
}

void test_exec_payload_accepts_empty_vectors(void)
{
    ExecPayloadPtr payload = PosixExecPayload__new("/bin/tool", "/", NULL, NULL, NULL, 0, NULL, 0);

    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT64(0, ExecPayload__argument_count(payload));
    TEST_ASSERT_EQUAL_UINT64(0, ExecPayload__environment_count(payload));
    PosixExecPayload__dispose(payload);
}

void test_exec_payload_packs_open_description_inheritance(void)
{
    ExecPayloadOpenDescription open_descriptions[] = {
        {
            .id = 9,
            .status_flags = 1,
            .path_offset = (ExecPayloadOffset) "/tmp/out",
            .local = IO_SESSION_HANDLE_BASE + 41,
            .token = 77,
            .inheritance_flags = 3,
            .service_length = 4,
            .service_data = {'f', 's', '0', 0},
            .single_transfer = 1,
        },
    };
    ExecPayloadDescriptor descriptors[] = {
        {
            .descriptor = 1,
            .descriptor_flags = 0,
            .open_description = 9,
        },
        {
            .descriptor = 4,
            .descriptor_flags = FD_CLOEXEC,
            .open_description = 9,
        },
    };

    ExecPayloadPtr payload = PosixExecPayload__new("/bin/tool", "/", NULL, NULL, open_descriptions, 1, descriptors, 2);

    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL(EXEC_PAYLOAD_OK, ExecPayload__validate(payload, ExecPayload__size(payload)));
    TEST_ASSERT_EQUAL_UINT64(1, ExecPayload__open_description_count(payload));
    TEST_ASSERT_EQUAL_UINT64(2, ExecPayload__descriptor_count(payload));
    ConstExecPayloadOpenDescriptionPtr description = ExecPayload__open_description_at(payload, 0);
    TEST_ASSERT_NOT_NULL(description);
    TEST_ASSERT_EQUAL_UINT64(9, description->id);
    TEST_ASSERT_EQUAL_STRING("/tmp/out", ExecPayload__string_at(payload, description->path_offset));
    ConstExecPayloadDescriptorPtr descriptor = ExecPayload__descriptor_at(payload, 1);
    TEST_ASSERT_NOT_NULL(descriptor);
    TEST_ASSERT_EQUAL_INT(4, descriptor->descriptor);
    TEST_ASSERT_EQUAL_UINT64(9, descriptor->open_description);
    PosixExecPayload__dispose(payload);
}

void test_exec_payload_rejects_an_oversized_argument_block(void)
{
    char * oversized = malloc(EXEC_PAYLOAD_MAX);
    TEST_ASSERT_NOT_NULL(oversized);
    memset(oversized, 'x', EXEC_PAYLOAD_MAX - 1);
    oversized[EXEC_PAYLOAD_MAX - 1] = '\0';
    char * arguments[] = {oversized, NULL};

    TEST_ASSERT_NULL(PosixExecPayload__new("/bin/tool", "/", arguments, NULL, NULL, 0, NULL, 0));
    TEST_ASSERT_EQUAL_INT(E2BIG, errno);
    free(oversized);
}

void test_exec_payload_rejects_a_missing_path(void)
{
    TEST_ASSERT_NULL(PosixExecPayload__new(NULL, "/", NULL, NULL, NULL, 0, NULL, 0));
    TEST_ASSERT_EQUAL_INT(EFAULT, errno);
}
