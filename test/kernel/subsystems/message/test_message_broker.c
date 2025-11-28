#include "unity.h"
#include "../../../../kernel/subsystems/message/message_broker.h"
#include "../../fixtures/kernel.h"
#include "../../../../kernel/kernel_core.h"

static KernelPtr kernel = NULL;
static MessagePtr received_message_1 = NULL;
static MessagePtr received_message_2 = NULL;
static const ProcessID DUMMY_PID = 1234;


void setUp()
{
    kernel = init_test_kernel_without_filesystem();
    received_message_1 = NULL;
    received_message_2 = NULL;
}

void tearDown()
{

}

void fake_callback_1(KernelListenerPtr self, MessagePtr message)
{
    received_message_1 = message;
}

void fake_callback_2(KernelListenerPtr self, MessagePtr message)
{
    received_message_2 = message;
}

void test_message_broker_listener_registers_and_delivers_message()
{
    Listener listener = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback_1);

    ResultMatch(ListenerRegisterResult, MessageBroker__register_listener(kernel->message_broker, listener));
        IsError(code, message, TEST_FAIL());
    End

    MessagePtr message = Message__heap_new_anything(SOURCE_PROCESS, 1234,DUMMY_PID);

    ResultMatch(AckCode, MessageBroker__deliver(kernel->message_broker, message))
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_EQUAL(message, received_message_1);
}

void test_message_broker_listener_registers_and_delivers_message_for_multiple_listeners()
{
    Listener listener_1 = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback_1);
    Listener listener_2 = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback_2);

    ResultMatch(ListenerRegisterResult, MessageBroker__register_listener(kernel->message_broker, listener_1));
        IsError(code, message, TEST_FAIL());
    End
    ResultMatch(ListenerRegisterResult, MessageBroker__register_listener(kernel->message_broker, listener_2));
        IsError(code, message, TEST_FAIL());
    End

    MessagePtr message = Message__heap_new_anything(SOURCE_PROCESS, 1234, DUMMY_PID);

    ResultMatch(AckCode, MessageBroker__deliver(kernel->message_broker, message))
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_EQUAL(message, received_message_1);
    TEST_ASSERT_EQUAL(message, received_message_2);
}

void test_message_broker_doesnt_deliver_when_not_registered_for_source()
{
    Listener listener = KernelListener__new(kernel, SOURCE_PROCESS, fake_callback_1);
    ResultMatch(ListenerRegisterResult, MessageBroker__register_listener(kernel->message_broker, listener));
        IsError(code, message, TEST_FAIL());
    End
    MessagePtr message = Message__heap_new_anything(SOURCE_FAULT, 1234, DUMMY_PID);

    ResultMatch(AckCode, MessageBroker__deliver(kernel->message_broker, message))
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_NULL(received_message_1);
    TEST_ASSERT_NULL(received_message_2);
}