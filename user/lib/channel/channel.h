#ifndef HUND_USER_CHANNEL_H
#define HUND_USER_CHANNEL_H

#include "ipc/channel.h"
#include "protocol.h"
#include "services/service_directory.h"

Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr target_service);
Result(ChannelCode) Channel__request_once(ChannelPtr self, ConstChannelDataPtr request, size_t request_size);
Result(ChannelCode) Channel__request(ChannelPtr self, ConstChannelDataPtr request, size_t request_size);
Result(OptionalChannelReplySize) Channel__poll_reply_once(ChannelPtr self, ChannelDataPtr reply, size_t reply_capacity);
Result(OptionalChannelReplySize) Channel__poll_reply(ChannelPtr self, ChannelDataPtr reply, size_t reply_capacity);
Result(ChannelReplySize) Channel__submit(ChannelPtr self, ConstChannelDataPtr request, size_t request_size,
                                         ChannelDataPtr reply, size_t reply_capacity);
void Channel__prepare_wait(void);
void Channel__wait_event(void);
ChannelCode Channel__close(ChannelPtr self);

#endif
