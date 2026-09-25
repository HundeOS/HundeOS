#ifndef HUND_MOUNT_CONFIG_H
#define HUND_MOUNT_CONFIG_H
#include "types/result.h"

typedef struct MountConfig
{
    BString device_path;
    BString filesystem_type;
    BString mountpoint;
} MountConfig;

typedef MountConfig * MountConfigPtr;
ResultDef(MountConfig);
MountConfig MountConfig__new(ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint);
Result(MountConfig) MountConfig__parse(ConstBStringPtr line);
BString MountConfig__without_comment(ConstBStringPtr line);
#endif
