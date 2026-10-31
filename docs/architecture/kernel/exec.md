# Process Image Overlay

## Scope

`execve` replaces the calling process image. It is not implemented by spawning a
new process and exiting the caller. A successful overlay preserves the process
identity, parent relationship, credentials, accounting, and eligible open
resources while replacing the address space, execution context, stack, heap,
program name, arguments, and environment.

Executable bytes continue to arrive through the userspace I/O system. The kernel
does not gain a filesystem or file-descriptor table for exec.

## Argument And Environment Payload

The userspace compatibility layer packs the path, argument vector, and
environment vector into one offset-based payload. The complete payload is
limited to 64 KiB and is copied and validated at the syscall boundary before an
asynchronous executable load can park the caller. Offsets make the copied value
self-contained and avoid retaining caller pointers.

Existing native spawn requests retain their current bounded argument interface
until they are migrated to the same payload. New process startup now receives
both a null-terminated argument vector and a null-terminated environment vector.
Existing spawn callers begin with an empty environment.

## Transaction

The kernel builds a candidate process image containing a new page table, ELF
mappings, stack, startup vectors, heap state, allocation records, and initial
architecture context. The live `Process` remains unchanged until executable
validation, image construction, and resource-inheritance preparation succeed.

Commit installs the candidate image into the existing `Process`, switches away
from the old active address space, and then releases the old image. The process
resumes at the new ELF entry point and the syscall does not return. A failure
before commit destroys the candidate and resumes the old image with a mapped
errno value.

## Open Resources

Descriptor tables and I/O sessions remain userspace state. Before commit, libc
groups descriptors by open description and asks each final resource-owning
service to prepare a single-use claim. Claims are bound to the authenticated
process and exec transaction. The new runtime opens fresh sessions and rebuilds
the descriptor table from committed claims.

Descriptors marked `FD_CLOEXEC` are excluded and released only when the image
commit succeeds. Failed exec leaves every descriptor unchanged. Duplicate
descriptors continue to share one open description, including file offsets and
pipe endpoints. The same prepare, claim, and abort protocol is shared with fork
inheritance. The Rust I/O host owns application handles and dispatches to the
component that owns each open description; external providers retain their own
resource ownership.

## Error Contract

The POSIX layer maps missing paths to `ENOENT`, execute permission failures to
`EACCES`, invalid executable formats to `ENOEXEC`, oversized payloads to `E2BIG`,
and allocation failure to `ENOMEM`. Successful `execve` never returns.

## Current Status

The 64 KiB payload format, userspace builder, and public `execve` entry point are
implemented. The kernel copies and validates the payload before an asynchronous
load, builds a candidate image, and atomically installs it into the calling
`Process` while retaining its PID, parent, credentials, accounting, and startup
standard-I/O endpoints. The startup ABI supplies the copied argument and
environment vectors; libc provides mutable `environ`, `getenv`, `setenv`,
`unsetenv`, and `putenv`. Existing native spawn callers receive an empty
environment.

The exec payload now carries non-`FD_CLOEXEC` open-description and descriptor
tables. Libc prepares authenticated inheritance claims before the overlay,
commits those claims before entering the kernel, and the replacement image opens
fresh I/O sessions before rebuilding its descriptor table. Duplicate descriptors
continue to share one open description.

Remaining work is limited to resource kinds and cleanup semantics that are not
yet implemented: pipes and sockets do not inherit through exec, committed grants
still need orphan cleanup if the replacement image exits before claiming, and
descriptors excluded by `FD_CLOEXEC` still need reliable close/release on the
successful overlay path.
