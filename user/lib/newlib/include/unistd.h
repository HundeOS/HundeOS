#ifndef __USER_LIB_NEWLIB_UNISTD_H
#define __USER_LIB_NEWLIB_UNISTD_H

#include <stddef.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#define _POSIX_VERSION 200809L

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

extern char * optarg;
extern int opterr;
extern int optind;
extern int optopt;

int close(int descriptor);
int dup(int descriptor);
int dup2(int descriptor, int target);
int pipe(int descriptors[2]);
int fsync(int descriptor);
ssize_t read(int descriptor, void * buffer, size_t size);
ssize_t write(int descriptor, const void * buffer, size_t size);
unsigned int sleep(unsigned int seconds);
unsigned int alarm(unsigned int seconds);
int getopt(int argc, char * const argv[], const char * options);
int pledge(const char * promises, const char * paths);
int access(const char * path, int mode);
int unlink(const char * path);
int rmdir(const char * path);
int link(const char * source, const char * target);
int linkat(int source_descriptor, const char * source, int target_descriptor, const char * target, int flags);
int symlink(const char * source, const char * target);
int rename(const char * source, const char * target);
int isatty(int descriptor);
char * ttyname(int descriptor);
int readlink(const char * path, char * buffer, size_t size);
pid_t getpid(void);
pid_t getppid(void);
pid_t getpgrp(void);
pid_t getpgid(pid_t pid);
int setpgid(pid_t pid, pid_t process_group_id);
pid_t setsid(void);
uid_t getuid(void);
gid_t getgid(void);
uid_t geteuid(void);
gid_t getegid(void);
int setuid(uid_t user);
int setgid(gid_t group);
int kill(pid_t pid, int signal);
int killpg(pid_t process_group_id, int signal);
pid_t fork(void);
int execve(const char * path, char * const arguments[], char * const environment[]);
int execvp(const char * file, char * const arguments[]);
int chdir(const char * path);
char * getcwd(char * buffer, size_t size);
int gethostname(char * name, size_t size);
char * realpath(const char * path, char * resolved);
int chown(const char * path, uid_t owner, gid_t group);
int lchown(const char * path, uid_t owner, gid_t group);
int chflags(const char * path, unsigned long flags);
off_t lseek(int descriptor, off_t offset, int whence);
int ftruncate(int descriptor, off_t length);
int fchmodat(int descriptor, const char * path, mode_t mode, int flags);
int chflagsat(int descriptor, const char * path, unsigned long flags, int options);
int utimensat(int descriptor, const char * path, const struct timespec times[2], int flags);
int mkfifo(const char * path, mode_t mode);
int mknod(const char * path, mode_t mode, dev_t device);
void _exit(int code) __dead;

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#endif
