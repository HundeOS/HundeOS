#ifndef HUND_MOUNT_CLIENT_H
#define HUND_MOUNT_CLIENT_H
#include "mount_config.h"
#include "protocol.h"
ResultDef(IOStatus);
Result(IOStatus) MountClient__add(ChannelPtr self, MountConfig config);
#endif
