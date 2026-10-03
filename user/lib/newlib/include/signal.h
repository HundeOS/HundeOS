#ifndef __USER_LIB_NEWLIB_SIGNAL_H
#define __USER_LIB_NEWLIB_SIGNAL_H

#include <stdint.h>
#include <sys/types.h>

#define NSIG 33

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT SIGABRT
#define SIGEMT 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGBUS 10
#define SIGSEGV 11
#define SIGSYS 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGURG 16
#define SIGSTOP 17
#define SIGTSTP 18
#define SIGCONT 19
#define SIGCHLD 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGIO 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGINFO 29
#define SIGUSR1 30
#define SIGUSR2 31
#define SIGTHR 32

typedef int sig_atomic_t;
#ifndef _SIGSET_T
#define _SIGSET_T
typedef uint32_t sigset_t;
#endif
typedef void (*sig_t)(int);

#define SIG_DFL ((sig_t)0)
#define SIG_IGN ((sig_t)1)
#define SIG_ERR ((sig_t)-1)

struct sigaction
{
    sig_t sa_handler;
    sigset_t sa_mask;
    int sa_flags;
};

#define SA_ONSTACK 0x0001
#define SA_RESTART 0x0002
#define SA_RESETHAND 0x0004
#define SA_NOCLDSTOP 0x0008
#define SA_NODEFER 0x0010
#define SA_NOCLDWAIT 0x0020

#define SIG_BLOCK 1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3

extern const char * const sys_signame[NSIG];

int kill(pid_t pid, int signal);
int raise(int signal);
int sigemptyset(sigset_t * set);
int sigfillset(sigset_t * set);
int sigaddset(sigset_t * set, int signal);
int sigdelset(sigset_t * set, int signal);
int sigismember(const sigset_t * set, int signal);
sig_t signal(int signal, sig_t handler);
int sigaction(int signal, const struct sigaction * action, struct sigaction * previous);
int sigprocmask(int operation, const sigset_t * set, sigset_t * previous);
int sigpending(sigset_t * set);
int sigsuspend(const sigset_t * mask);

#endif
