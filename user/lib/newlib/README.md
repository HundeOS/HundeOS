# Freestanding C And POSIX Compatibility

This directory contains the target userspace C/POSIX compatibility layer. It
adapts conventional file descriptors and C library calls to the native
userspace I/O API; the kernel is not involved in normal file operations.

The allocator source in `nano-mallocr.c` is vendored from newlib
`4.6.0.20260123` with its upstream copyright notice intact. `malloc.c` is the
local build wrapper selecting its entry points.

The complete pinned Newlib release is retained under `vendor/newlib/src` and
built for each target architecture. The target sysroot takes Newlib's `math.h`
dependency closure and `libm.a`, plus an explicit allowlist of OS-independent
libc archive members for memory, strings, sorting, character classification,
and integer conversion. The build verifies that each selected member exists
exactly once and that their combined undefined symbols are limited to the
declared allowlist before adding them to the HundeOS `libc.a`. It never installs
Newlib's complete libc or advertises headers for unsupported operating-system
interfaces.

This directory remains the HundeOS libc and POSIX adaptation layer. `errno.c`
supplies the shared `__errno` entry point and the minimal `_impure_ptr` storage
needed by the selected Newlib conversion routines. Newlib's `impure.o` and its
stdio dependencies are deliberately not selected.

The compatibility surface grows from concrete port requirements. The pinned
OpenBSD `cat`, `ln`, `ls`, `mkdir`, `mv`, and `rm` sources are its current
end-to-end consumers. Native I/O handles do not fit in POSIX `int` descriptors,
so `posix_io.c` maintains a process-local descriptor map and converts native
status values to `errno` behavior.

The stdio subset provides seek-aware buffered file reads and line-buffered
standard output. Current deliberate limitations include an `fstat` view that
only supplies a transfer block size and a successful `pledge` compatibility
call without sandbox enforcement. These are compatibility boundaries, not
kernel I/O fallbacks.
