#include "unity.h"

#include "ipc/kernel_notification_queue.h"
#include "ipc/interrupt_protocol.h"

void test_kernel_notification_queue_coalesces_each_irq_without_capacity_failure(void)
{
    KernelNotificationQueue queue = KernelNotificationQueue__new();
    for (KernelNotificationID id = 0; id < KERNEL_NOTIFICATION_QUEUE_CAPACITY; id++)
    {
        match_result(KernelNotificationQueue__publish(&queue, id, id + 1),
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
    match_result(KernelNotificationQueue__publish(&queue, 48, 1),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(KernelNotificationQueue__publish(&queue, 48, 2),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(
        KernelNotificationQueue__pop(&queue),
        some(event)
        {
            TEST_ASSERT_EQUAL_UINT64(48, event.id);
            TEST_ASSERT_EQUAL_UINT64(2, event.sequence);
        },
        none_test_fail()
    );
}

void test_interrupt_protocol_delivery_and_acknowledgement_preserve_identity(void)
{
    InterruptProtocolResponse delivery = InterruptProtocol__delivery(53, 7, 19);
    InterruptProtocolRequest acknowledgement = InterruptProtocol__acknowledgement(53, 7, 19);

    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_DELIVERY, delivery.kind);
    TEST_ASSERT_EQUAL_UINT64(53, delivery.irq);
    TEST_ASSERT_EQUAL_UINT64(7, delivery.generation);
    TEST_ASSERT_EQUAL_UINT64(19, delivery.sequence);
    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_PROTOCOL_ACKNOWLEDGE, acknowledgement.kind);
    TEST_ASSERT_EQUAL_UINT64(delivery.irq, acknowledgement.irq);
    TEST_ASSERT_EQUAL_UINT64(delivery.generation, acknowledgement.generation);
    TEST_ASSERT_EQUAL_UINT64(delivery.sequence, acknowledgement.sequence);
}
