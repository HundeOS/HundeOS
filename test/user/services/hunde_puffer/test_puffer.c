#include "unity.h"
#include "../../../../user/services/hunde-puffer/puffer.h"

static Puffer manager;
static ServiceDirectory directory;
static uint64_t generation;
static BufferID ids[BUFFER_LIMIT];
static uint64_t states[BUFFER_LIMIT];
static unsigned allocated;
static unsigned freed;
static bool fail_free;

int64_t BufferMemory__operation(MemOperation operation, uint64_t first, uint64_t second, uint64_t third,
                                uint64_t fourth)
{
    if (operation == MEM_BUFFER_CREATE)
    {
        unsigned slot = fourth == 0 ? allocated++ : fourth % BUFFER_LIMIT;
        ids[slot] = ++generation * BUFFER_LIMIT + slot;
        states[slot] = BUFFER_CLIENT_ATTACHED | BUFFER_SERVER_ATTACHED;
        return ids[slot];
    }
    unsigned slot = first % BUFFER_LIMIT;
    if (ids[slot] != first)
    {
        return BUFFER_INVALID;
    }
    if (operation == MEM_BUFFER_STATE)
    {
        return states[slot];
    }
    if (operation == MEM_BUFFER_DROP)
    {
        states[slot] &= ~(second == 8 ? BUFFER_SERVER_ATTACHED : BUFFER_CLIENT_ATTACHED);
        return BUFFER_OK;
    }
    if (operation == MEM_BUFFER_FREE)
    {
        if (fail_free || states[slot] != 0)
        {
            return BUFFER_BUSY;
        }
        ids[slot] = 0;
        freed++;
        return BUFFER_OK;
    }
    return BUFFER_INVALID;
}
void setUp(void)
{
    directory = (ServiceDirectory){0};
    directory.entries[0].name = bstr("block-test");
    directory.entries[0].service_pid = 8;
    directory.entries[0].present = 1;
    directory.entries[0].generation = 1;
    manager = Puffer__new(&directory);
    generation = allocated = freed = 0;
    fail_free = false;
    __builtin_memset(ids, 0, sizeof(ids));
    __builtin_memset(states, 0, sizeof(states));
}
void tearDown(void)
{
}

static BufferReply acquire(void)
{
    BufferRequest request = BufferRequest__new(BUFFER_ACQUIRE, 42, BUFFER_READ, 0, 0);
    TEST_ASSERT_TRUE(BufferRequest__set_service(&request, &bstr("block-test")));
    return Puffer__execute(&manager, 8, &request);
}
static BufferStatus release(BufferID id, BufferParticipant caller, bool pair)
{
    BufferRequest request = BufferRequest__new(pair ? BUFFER_RELEASE_PAIR : BUFFER_RELEASE, 42, 0, id, 0);
    return Puffer__execute(&manager, caller, &request).status;
}

void test_puffer_retains_live_peer_then_reuses_detached_buffer_with_fresh_id(void)
{
    BufferReply first = acquire();
    TEST_ASSERT_EQUAL(BUFFER_OK, first.status);
    TEST_ASSERT_EQUAL(BUFFER_DENIED, release(first.buffer.id, 99, false));
    TEST_ASSERT_EQUAL(BUFFER_OK, release(first.buffer.id, 42, false));
    BufferReply second = acquire();
    TEST_ASSERT_NOT_EQUAL(first.buffer.address, second.buffer.address);
    TEST_ASSERT_EQUAL(BUFFER_OK, release(first.buffer.id, 8, true));
    BufferReply reused = acquire();
    TEST_ASSERT_EQUAL_HEX64(first.buffer.address, reused.buffer.address);
    TEST_ASSERT_NOT_EQUAL(first.buffer.id, reused.buffer.id);
    TEST_ASSERT_EQUAL(BUFFER_OK, release(first.buffer.id, 8, true));
    TEST_ASSERT_EQUAL(BUFFER_CLIENT_ATTACHED | BUFFER_SERVER_ATTACHED, states[reused.buffer.id % BUFFER_LIMIT]);
}
void test_puffer_reconciles_exit_and_evicts_above_pool_limit_without_freeing_attached_leases(void)
{
    BufferReply leases[BUFFER_POOL_LIMIT + 2];
    for (unsigned index = 0; index < BUFFER_POOL_LIMIT + 2; index++)
    {
        leases[index] = acquire();
    }
    for (unsigned index = 0; index < BUFFER_POOL_LIMIT + 1; index++)
    {
        states[leases[index].buffer.id % BUFFER_LIMIT] = 0;
        Puffer__reconcile(&manager, leases[index].buffer.id);
    }
    fail_free = true;
    Puffer__housekeeping(&manager);
    TEST_ASSERT_EQUAL(0, freed);
    fail_free = false;
    Puffer__housekeeping(&manager);
    TEST_ASSERT_EQUAL(1, freed);
    TEST_ASSERT_EQUAL(BUFFER_POOL_LIMIT + 1, BufferLeases__len(&manager.leases));
}
void test_puffer_requires_registered_service_identity(void)
{
    BufferRequest request = BufferRequest__new(BUFFER_ACQUIRE, 42, BUFFER_READ, 0, 0);
    TEST_ASSERT_TRUE(BufferRequest__set_service(&request, &bstr("block-test")));
    TEST_ASSERT_EQUAL(BUFFER_DENIED, Puffer__execute(&manager, 99, &request).status);
    TEST_ASSERT_TRUE(BufferRequest__set_service(&request, &bstr("not-registered")));
    TEST_ASSERT_EQUAL(BUFFER_DENIED, Puffer__execute(&manager, 8, &request).status);
    TEST_ASSERT_EQUAL(0, allocated);
}

void test_buffer_request_rejects_overlong_names_without_truncating_identity(void)
{
    uint8_t bytes[256];
    __builtin_memset(bytes, 'a', sizeof(bytes));
    BString name = BString__from_chars((const char *)bytes, sizeof(bytes));
    BufferRequest request = BufferRequest__new(BUFFER_ACQUIRE, 42, BUFFER_READ, 0, 0);
    TEST_ASSERT_FALSE(BufferRequest__set_service(&request, &name));
    request.service_length = UINT64_MAX;
    BString decoded = BufferRequest__service(&request);
    TEST_ASSERT_TRUE(BString__is_empty(&decoded));
}

void test_puffer_never_pools_or_frees_retained_service_buffers(void)
{
    BufferReply first = acquire();
    states[first.buffer.id % BUFFER_LIMIT] |= BUFFER_RETAINED;
    TEST_ASSERT_EQUAL(BUFFER_OK, release(first.buffer.id, 8, true));
    Puffer__reconcile(&manager, first.buffer.id);
    Puffer__housekeeping(&manager);
    BufferReply second = acquire();
    TEST_ASSERT_NOT_EQUAL(first.buffer.address, second.buffer.address);
    TEST_ASSERT_EQUAL_UINT(0, freed);
}
