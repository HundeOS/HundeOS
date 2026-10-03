#ifndef __USER_LIB_NEWLIB_SYS_WAIT_H
#define __USER_LIB_NEWLIB_SYS_WAIT_H

#include <sys/types.h>

#define WNOHANG 0x01
#define WUNTRACED 0x02
#define WCONTINUED 0x08

#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WTERMSIG(status) ((status) & 0x7f)
#define WSTOPSIG(status) WEXITSTATUS(status)
#define WIFEXITED(status) (WTERMSIG(status) == 0)
#define WIFSIGNALED(status) (WTERMSIG(status) != 0 && WTERMSIG(status) != 0x7f)
#define WIFSTOPPED(status) (WTERMSIG(status) == 0x7f)
#define WIFCONTINUED(status) ((status) == 0xffff)
#define WCOREDUMP(status) ((status) & 0x80)

pid_t wait(int * status);
pid_t waitpid(pid_t pid, int * status, int options);

#endif
