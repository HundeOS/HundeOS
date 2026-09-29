#include "unity.h"

#include "ipc/kernel_notification_queue.h"
#include "ipc/interrupt_protocol.h"

void test_pending_check_does_not_consume_notification(void)
{
    KernelNotificationQueue queue = KernelNotificationQueue__new();
    TEST_ASSERT_FALSE(KernelNotificationQueue__has_pending(&queue));
    match_result(KernelNotificationQueue__publish(&queue, 4, 19, 123),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(KernelNotificationQueue__has_pending(&queue));
    TEST_ASSERT_TRUE(KernelNotificationQueue__has_pending(&queue));
    match_optional(
        KernelNotificationQueue__pop(&queue),
        some(value)
        {
            TEST_ASSERT_EQUAL_UINT64(19, value.sequence);
            TEST_ASSERT_EQUAL_UINT64(123, value.occurred_at_usec);
        },
        none_test_fail()
    );
    TEST_ASSERT_FALSE(KernelNotificationQueue__has_pending(&queue));
}

void test_kernel_notification_queue_coalesces_each_irq_without_capacity_failure(void)
{
    KernelNotificationQueue queue = KernelNotificationQueue__new();
    for (KernelNotificationID id = 0; id < KERNEL_NOTIFICATION_QUEUE_CAPACITY; id++)
    {
        match_result(KernelNotificationQueue__publish(&queue, id, id + 1, id + 1000),
            success_do_nothing(),
                     error_test_fail()
        );
    }

    for (KernelNotificationID id = 0; id < KERNEL_NOTIFICATION_QUEUE_CAPACITY; id++)
    {
        match_optional(
            KernelNotificationQueue__pop(&queue),
            some(event)
            {
                TEST_ASSERT_EQUAL_UINT64(id, event.id);
                TEST_ASSERT_EQUAL_UINT64(id + 1, event.sequence);
                TEST_ASSERT_EQUAL_UINT64(id + 1000, event.occurred_at_usec);
            },
            none_test_fail()
        );
    }
    match_optional(KernelNotificationQueue__pop(&queue),
        some_test_fail(),
        none_do_nothing()
    );
}

void test_kernel_notification_queue_replaces_a_coalesced_delivery_sequence(void)
{
    KernelNotificationQueue queue = KernelNotificationQueue__new();
    match_result(KernelNotificationQueue__publish(&queue, 48, 1, 100),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(KernelNotificationQueue__publish(&queue, 48, 2, 200),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(
        KernelNotificationQueue__pop(&queue),
        some(event)
        {
            TEST_ASSERT_EQUAL_UINT64(48, event.id);
            TEST_ASSERT_EQUAL_UINT64(2, event.sequence);
            TEST_ASSERT_EQUAL_UINT64(200, event.occurred_at_usec);
        },
        none_test_fail()
    );
}

void test_interrupt_protocol_delivery_and_acknowledgement_preserve_identity(void)
{
    InterruptProtocolResponse delivery = InterruptProtocol__delivery(53, 7, 19, 100, 120);
    InterruptProtocolRequest acknowledgement = InterruptProtocol__acknowledgement(53, 7, 19);

    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_DELIVERY, delivery.kind);
    TEST_ASSERT_EQUAL_UINT64(53, delivery.irq);
    TEST_ASSERT_EQUAL_UINT64(7, delivery.generation);
    TEST_ASSERT_EQUAL_UINT64(19, delivery.sequence);
    TEST_ASSERT_EQUAL_UINT64(100, delivery.kernel_irq_at_usec);
    TEST_ASSERT_EQUAL_UINT64(120, delivery.manager_received_at_usec);
    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_ACKNOWLEDGE, acknowledgement.kind);
    TEST_ASSERT_EQUAL_UINT64(delivery.irq, acknowledgement.irq);
    TEST_ASSERT_EQUAL_UINT64(delivery.generation, acknowledgement.generation);
    TEST_ASSERT_EQUAL_UINT64(delivery.sequence, acknowledgement.sequence);
}
