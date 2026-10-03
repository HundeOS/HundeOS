#ifndef __USER_LIB_NEWLIB_GRP_H
#define __USER_LIB_NEWLIB_GRP_H

#include <sys/types.h>

struct group
{
    char * gr_name;
    char * gr_passwd;
    gid_t gr_gid;
    char ** gr_mem;
};

struct group * getgrgid(gid_t group);
void setgrent(void);
struct group * getgrent(void);
void endgrent(void);
char * group_from_gid(gid_t group, int nogroup);

#endif
