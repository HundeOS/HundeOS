#include "unity.h"
#include "../../../user/lib/buffer/client.c"

static bool validate_allowed;
static unsigned releases;
static BufferID released;
static unsigned requests;

Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr target)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr(BUFFER_SERVICE_NAME), *target);
    return Success(Channel, ((Channel){0}));
}

Result(ChannelReplySize)
    Channel__submit(ChannelPtr self, ConstChannelDataPtr data, size_t size, ChannelDataPtr response, size_t capacity)
{
    TEST_ASSERT_EQUAL_UINT64(sizeof(BufferRequest), size);
    TEST_ASSERT_LESS_THAN_UINT64(CHANNEL_PAYLOAD_CAPACITY, size);
    ConstBufferRequestPtr request = (ConstBufferRequestPtr)data;
    BufferReply reply = BufferReply__new(BUFFER_OK);
    requests++;
    if (request->operation == BUFFER_ACQUIRE)
    {
        TEST_ASSERT_EQUAL_BSTRING(bstr("block-test"), BufferRequest__service(request));
        reply.buffer = BufferDescriptor__new(BUFFER_LIMIT, request->direction);
    }
    else
    {
        TEST_ASSERT_EQUAL(BUFFER_RELEASE, request->operation);
        releases++;
        released = request->id;
    }
    __builtin_memcpy(response, &reply, sizeof(reply));
    return Success(ChannelReplySize, sizeof(reply));
}

ChannelCode Channel__close(ChannelPtr self)
{
    return CHANNEL_OK;
}

int64_t BufferMemory__operation(MemOperation operation, uint64_t first, uint64_t second, uint64_t third,
                                uint64_t fourth)
{
    TEST_ASSERT_EQUAL(MEM_BUFFER_VALIDATE, operation);
    TEST_ASSERT_EQUAL_UINT64(42, third);
    return validate_allowed ? BUFFER_BASE : BUFFER_DENIED;
}

void setUp(void)
{
    connected = false;
    validate_allowed = true;
    releases = requests = 0;
    released = 0;
}

void tearDown(void)
{
}

void test_acquire_transports_service_name_and_validates_kernel_mapping(void)
{
    match_result(
        BufferClient__acquire(&bstr("block-test"), 42, BUFFER_READ),
        success(buffer)
        {
            TEST_ASSERT_EQUAL_UINT64(BUFFER_BASE, buffer.address);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(1, requests);
    TEST_ASSERT_EQUAL(0, releases);
}

void test_peer_exit_during_acquire_relinquishes_server_attachment(void)
{
    validate_allowed = false;
    match_result(
        BufferClient__acquire(&bstr("block-test"), 42, BUFFER_READ),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_INVALID, code);
        }
    );
    TEST_ASSERT_EQUAL(2, requests);
    TEST_ASSERT_EQUAL(1, releases);
    TEST_ASSERT_EQUAL_UINT64(BUFFER_LIMIT, released);
}
