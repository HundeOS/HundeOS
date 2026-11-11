#ifndef HUNDE_CHANNEL_NATIVE_H
#define HUNDE_CHANNEL_NATIVE_H

#include "native.h"
#include "ipc/buffer.h"

RustNativeLayout hunde_channel_client_layout(void);
RustNativeLayout hunde_channel_listener_layout(void);
HundeStatusResult hunde_channel_listen(void * storage, HundeByteSpan name);
RustNativeValue hunde_channel_arrival(void * storage, uint64_t index);
RustNativeLayout hunde_channel_server_layout(void);
HundeStatusResult hunde_channel_accept(void * storage, uint64_t connection);
HundeStatusResult hunde_channel_open(void * storage, HundeByteSpan endpoint);
HundeStatusResult hunde_channel_send(void * storage, HundeByteSpan request);
RustNativeValue hunde_channel_receive(void * storage, uint8_t * target, uint64_t capacity);
uint64_t hunde_channel_server_pid(void * storage);
void hunde_channel_close(void * storage);
RustNativeValue hunde_channel_server_read(void * storage, uint8_t * target, uint64_t capacity);
HundeStatusResult hunde_channel_server_reply(void * storage, HundeByteSpan reply);
uint64_t hunde_channel_transport_pid(void * storage);
uint32_t hunde_channel_server_alive(void * storage);
void hunde_channel_server_close(void * storage);
HundeStatusResult hunde_buffer_validate(ConstBufferDescriptorPtr buffer, uint64_t peer, uint32_t direction);

#endif
