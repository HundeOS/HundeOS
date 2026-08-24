#include "unity.h"
#include "../../../../kernel/subsystems/message/message_broker.h"
#include "../../../../kernel/subsystems/message/subsystem_codes.h"
#include "../../fixtures/kernel.h"
#include "../../../../kernel/kernel_core.h"

static KernelPtr kernel = NULL;
static Optional(Message) received_message_1 = None(Message);
static Optional(Message) received_message_2 = None(Message);
static const ProcessID DUMMY_PID = 1234;


void setUp()
{
    kernel = init_test_kernel_without_filesystem();
    received_message_1 = None(Message);
    received_message_2 = None(Message);
}

void tearDown()
{

}

void fake_callback_1(KernelListenerPtr self, MessagePtr message)
{
    received_message_1 = Some(Message, *message);
}

void fake_callback_2(KernelListenerPtr self, MessagePtr message)
{
    received_message_2 = Some(Message, *message);
}

void test_message_broker_listener_registers_and_delivers_message()
{
    Listener listener = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback_1);

    match_result(ListenerRegisterResult, MessageBroker__register_listener(kernel->message_broker, listener), success_do_nothing(), error_test_fail());

    MessagePtr message = Message__heap_new_anything(SOURCE_PROCESS, 1234,DUMMY_PID);

    match_result(AckCode, MessageBroker__deliver(kernel->message_broker, message), success_do_nothing(), error_test_fail());

    TEST_ASSERT_TRUE(received_message_1.present);
    TEST_ASSERT_EQUAL(1234, received_message_1.some.val.source_code);
}

void test_message_broker_listener_registers_and_delivers_message_for_multiple_listeners()
{
    Listener listener_1 = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback_1);
    Listener listener_2 = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback_2);

    match_result(ListenerRegisterResult, MessageBroker__register_listener(kernel->message_broker, listener_1), success_do_nothing(), error_test_fail());
    match_result(ListenerRegisterResult, MessageBroker__register_listener(kernel->message_broker, listener_2), success_do_nothing(), error_test_fail());

    MessagePtr message = Message__heap_new_anything(SOURCE_PROCESS, 1234, DUMMY_PID);

    match_result(AckCode, MessageBroker__deliver(kernel->message_broker, message), success_do_nothing(), error_test_fail());

    TEST_ASSERT_TRUE(received_message_1.present);
    TEST_ASSERT_TRUE(received_message_2.present);
    TEST_ASSERT_EQUAL(1234, received_message_1.some.val.source_code);
    TEST_ASSERT_EQUAL(1234, received_message_2.some.val.source_code);
}

void test_message_broker_doesnt_deliver_when_not_registered_for_source()
{
    Listener listener = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback_1);
    match_result(ListenerRegisterResult, MessageBroker__register_listener(kernel->message_broker, listener), success_do_nothing(), error_test_fail());

    MessagePtr message = Message__heap_new_anything(SOURCE_FAULT, 1234, DUMMY_PID);

    match_result(AckCode, MessageBroker__deliver(kernel->message_broker, message), success_do_nothing(), error_test_fail());

    TEST_ASSERT_FALSE(received_message_1.present);
    TEST_ASSERT_FALSE(received_message_2.present);
}
