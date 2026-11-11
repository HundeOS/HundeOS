# Freestanding C And POSIX Compatibility

This directory contains the target userspace C/POSIX compatibility layer. It
adapts conventional file descriptors and C library calls to the native
userspace I/O API; the kernel is not involved in normal file operations.

The allocator source in `nano-mallocr.c` is vendored from newlib
`4.6.0.20260123` with its upstream copyright notice intact. `malloc.c` is the
local adapter serializing its public entry points with a process-private
`UserMutex`. `allocator_core.c` selects and privately renames the upstream
entry points, so nested calls from `calloc`, `realloc`, and `memalign` stay
inside one lock without requiring recursion or changes to the vendored source.
The wrapper also enables upstream `memalign`; the Rust host allocation adapter
uses it to honor Rust `Layout` alignment while retaining ordinary `free`.

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
supplies `__getreent` and `__errno` over the current thread's runtime allocation.
Newlib is built with multithread support and dynamic reentrancy so the selected
conversion routines use that same per-thread record. `_impure_ptr` remains
bootstrap storage only. Newlib's `impure.o` and its stdio dependencies are
deliberately not selected. Allocation is serialized; descriptor and native I/O
bookkeeping are not yet thread-safe. See [Userspace threads](../../../docs/architecture/kernel/threads.md)
for static TLS support and the remaining loaded-image limitation.

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
