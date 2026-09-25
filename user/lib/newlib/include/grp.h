#ifndef __USER_LIB_NEWLIB_GRP_H
#define __USER_LIB_NEWLIB_GRP_H

#include <sys/types.h>

struct group
{
    char * gr_name;
    gid_t gr_gid;
};

struct group * getgrgid(gid_t group);
const char * group_from_gid(gid_t group, int nogroup);

#endif
