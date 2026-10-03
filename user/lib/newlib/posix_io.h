#ifndef __USER_LIB_NEWLIB_POSIX_IO_H
#define __USER_LIB_NEWLIB_POSIX_IO_H

#include "exec_payload.h"
#include "syscalls/io.h"

IOHandle PosixIO__native_handle(int descriptor);
Optional(BString) PosixIO__path(int descriptor);
Optional(BString) PosixIO__terminal_path(int descriptor);
ExecPayloadPtr PosixIO__prepare_exec_payload(const char * path, const char * current_directory,
                                             char * const arguments[], char * const environment[]);
IOStatus PosixIO__commit_exec(void);
void PosixIO__abort_exec(void);
IOStatus PosixIO__claim_exec(ConstExecPayloadPtr payload);
IOStatus PosixIO__prepare_fork(void);
IOStatus PosixIO__commit_fork(uint64_t child_pid);
void PosixIO__abort_fork(void);
IOStatus PosixIO__claim_fork(void);
void PosixIO__finish_fork(void);

#endif
