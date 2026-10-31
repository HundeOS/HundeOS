# HundeOS Documentation

Start with the repository [README](../README.md) for prerequisites, building,
and tests. [Goals](GOALS.md) describes project direction; [TODO](TODO.md) records
agreed unfinished work and open questions.

## Kernel

The [kernel guide index](architecture/kernel/README.md) covers privileged
mechanisms: process lifecycle, address spaces, scheduling, protected IPC,
and shared time/accounting interfaces.

The kernel authenticates identities and enforces mappings and hardware access.
It does not own application file descriptors, filesystem namespaces, or ordinary
device I/O. Boot, panic, and diagnostic output remains independent of userspace.

## Userspace Services And Drivers

The [userspace guide index](architecture/userspace/README.md) separates service
policy, device handling, filesystem components, and shared runtime adapters.
It also identifies where their protocols cross the kernel boundary.

Services coordinate bootstrap, namespace and handle ownership, Channel
authorization, shared buffers, and interrupt delegation. Drivers and filesystem
components implement normal I/O in userspace. Some components share a host
process; a service endpoint does not necessarily identify a separate executable.

## Platforms

- [QEMU](platforms/qemu.md): emulator targets, acceleration, disk images, and
  Python smoke/integration tests.
- [Milk-V Duo S](platforms/milkv-duo-s.md): hardware boot contracts, AArch64 and
  RISC-V images, UART connections, and recorded validation boundaries.
- [Milk-V Duo](platforms/milkv-duo.md): original CV1800B target and emulator loop.

Implementation in the source tree and acceptance on a particular board are
different evidence. Platform guides retain known limitations and unresolved
emulator failures; a successful build does not establish hardware acceptance.

## Shared Code And Development

- [Generic types, collections, and strings](architecture/shared/generic-types.md):
  APIs shared by kernel and userspace, with environment-specific adapters.
- [CLion indexing](development/clion.md): compilation database generation.
- [Project assets](../assets/README.md): logo and panic artwork inventory.

Architecture guides describe ownership, interfaces, and current constraints.
Staged implementation notes are historical where explicitly identified; future
requirements remain in Goals/TODO rather than serving as implementation claims.
