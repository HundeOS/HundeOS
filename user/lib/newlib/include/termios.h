#ifndef __USER_LIB_NEWLIB_TERMIOS_H
#define __USER_LIB_NEWLIB_TERMIOS_H

#include <stdint.h>

typedef uint8_t cc_t;
typedef uint32_t speed_t;
typedef uint32_t tcflag_t;

#define NCCS 20

struct termios
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

struct winsize;

#define IGNBRK 0000001U
#define BRKINT 0000002U
#define IGNPAR 0000004U
#define PARMRK 0000010U
#define INPCK 0000020U
#define ISTRIP 0000040U
#define INLCR 0000100U
#define IGNCR 0000200U
#define ICRNL 0000400U
#define IXON 0002000U
#define IXANY 0004000U
#define IXOFF 0010000U

#define OPOST 0000001U
#define ONLCR 0000004U
#define OCRNL 0000010U
#define ONOCR 0000020U
#define ONLRET 0000040U

#define CSIZE 0000060U
#define CS5 0000000U
#define CS6 0000020U
#define CS7 0000040U
#define CS8 0000060U
#define CSTOPB 0000100U
#define CREAD 0000200U
#define PARENB 0000400U
#define PARODD 0001000U
#define HUPCL 0002000U
#define CLOCAL 0004000U

#define ISIG 0000001U
#define ICANON 0000002U
#define ECHO 0000010U
#define ECHOE 0000020U
#define ECHOK 0000040U
#define ECHONL 0000100U
#define NOFLSH 0000200U
#define TOSTOP 0000400U
#define IEXTEN 0100000U

#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT 15
#define VEOL2 16

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#define TCOOFF 0
#define TCOON 1
#define TCIOFF 2
#define TCION 3

#define B0 0000000U
#define B50 0000001U
#define B75 0000002U
#define B110 0000003U
#define B134 0000004U
#define B150 0000005U
#define B200 0000006U
#define B300 0000007U
#define B600 0000010U
#define B1200 0000011U
#define B1800 0000012U
#define B2400 0000013U
#define B4800 0000014U
#define B9600 0000015U
#define B19200 0000016U
#define B38400 0000017U

int tcgetattr(int descriptor, struct termios * attributes);
int tcgetwinsize(int descriptor, struct winsize * size);
int tcsetattr(int descriptor, int action, const struct termios * attributes);
int tcsetwinsize(int descriptor, const struct winsize * size);
int tcdrain(int descriptor);
int tcflow(int descriptor, int action);

#endif
