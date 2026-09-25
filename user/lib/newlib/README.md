# Freestanding C And POSIX Compatibility

This directory contains the target userspace C/POSIX compatibility layer. It
adapts conventional file descriptors and C library calls to the native
userspace I/O API; the kernel is not involved in normal file operations.

The allocator source in `nano-mallocr.c` is vendored from newlib
`4.6.0.20260123` with its upstream copyright notice intact. `malloc.c` is the
local build wrapper selecting its entry points.

The compatibility surface grows from concrete port requirements. The pinned
OpenBSD `cat`, `ln`, `ls`, `mkdir`, `mv`, and `rm` sources are its current
end-to-end consumers. Native I/O handles do not fit in POSIX `int` descriptors,
so `posix_io.c` maintains a process-local descriptor map and converts native
status values to `errno` behavior.

Current deliberate limitations include unbuffered stdio, an `fstat` view that
only supplies a transfer block size, and a successful `pledge` compatibility
call without sandbox enforcement. These are compatibility boundaries, not
kernel I/O fallbacks.
