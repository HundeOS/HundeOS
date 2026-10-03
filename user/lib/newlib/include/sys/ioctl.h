#ifndef __USER_LIB_NEWLIB_SYS_IOCTL_H
#define __USER_LIB_NEWLIB_SYS_IOCTL_H

struct winsize
{
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TIOCGWINSZ 0x5413UL
#define TIOCSWINSZ 0x5414UL

int ioctl(int descriptor, unsigned long request, ...);

#endif
