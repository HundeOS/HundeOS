#ifndef HUND_MOUNT_CLIENT_H
#define HUND_MOUNT_CLIENT_H
#include "mount_config.h"
#include "protocol.h"
#define IO_HOST_MOUNT_SERVICE "hunde-io-host.mounts"
Result(IOStatus) MountClient__add(ChannelPtr self, MountConfig config);
Result(IOStatus) MountClient__start_hosted(MountConfig config);
#endif
