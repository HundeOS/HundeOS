# Process CPU time

HundeOS exposes POSIX process CPU accounting through `times()`. The native
`SYSCALL_PROCESS_TIMES` snapshot uses microseconds for both its elapsed value and
all four CPU counters; the userspace ABI therefore defines `CLK_TCK` as
1,000,000.

## Accounting modes

Each scheduled process has one of three accounting modes:

- `CPU_USAGE_USER` charges execution in userspace to `tms_utime`.
- `CPU_USAGE_SYSTEM` charges synchronous syscall handling and deferred syscall
  continuations to `tms_stime`.
- `CPU_USAGE_PAUSED` covers interrupt dispatch and scheduler work. This time
  remains visible in the scheduler's existing residency counters but is not
  charged as POSIX process CPU time.

Context switching starts a process paused, changes it to system mode while any
deferred syscall continuation runs, and changes it to user mode immediately
before returning to userspace. Architecture exception entry changes user
execution to system mode for syscalls and to paused mode for interrupts.

## Child accounting

An exiting child's final user and system totals, including descendants it
already waited for, are stored in its child-exit event. The parent adds those
totals to `tms_cutime` and `tms_cstime` only when `wait` or `waitpid` consumes
that event. This preserves the POSIX definition of terminated, waited-for
children rather than counting every exited child immediately.
