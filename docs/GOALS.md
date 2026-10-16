# Goals

These are the high-level outcomes HundeOS is working toward. They describe the
direction of the project, not an implementation order or authorization to begin
work. Concrete tasks and open design questions belong in [TODO](TODO.md).

## Microkernel System

Keep the privileged kernel focused on process scheduling and lifecycle, address
spaces and memory protection, IPC foundations, hardware authority, interrupt
entry and routing, and the minimum boot and diagnostic facilities needed to
operate the system.

Normal application and device I/O belongs in userspace. Filesystem namespaces,
file handles, filesystems, block and input drivers, console services, sockets,
and system-information filesystems communicate through authenticated Channels
and managed shared memory. Kernel boot, panic, and diagnostic output remains
independent so failures can still be reported before userspace is available.

## Consolidated Userspace I/O Hosts

Consolidate normal I/O execution into two userspace hosts. `hunde-io-host` owns
application-facing I/O policy and high-level components such as namespaces,
mounts, filesystems, sockets, streams, and shared caches. `hunde-treiber-host`
owns hardware-facing components such as device discovery, MMIO, DMA, interrupt
routing and rearming, and block, network, input, and console hardware drivers.

Components within one host call each other locally. The two hosts communicate
through authenticated Channels and managed shared buffers, preserving a process
boundary between protocol policy and hardware authority. This consolidation does
not absorb bootstrap, Channel authorization, shared-buffer management, tracing,
or independent kernel diagnostics into either host.

## POSIX Compatibility And Bash

Provide a useful POSIX compatibility layer rather than pursuing certification
as an end in itself. The concrete compatibility milestone is to cross-compile
and run Bash on HundeOS, including an interactive shell with working process,
descriptor, signal, pipe, filesystem, and terminal behavior.

Compatibility work should be driven by real program requirements and tested at
the userspace boundary. Native HundeOS APIs and services remain free to expose
capabilities that do not fit POSIX.

## Shared Libraries

Support dynamically linked userspace programs and reusable shared libraries.
This includes an explicit dynamic-loader boundary, architecture-appropriate ELF
relocations, dependency and symbol resolution, protected segment mappings, and
well-defined library initialization, teardown, and lifetime behavior.

Use the same standard ELF loading foundation for dynamically loaded I/O and
hardware components. Host modules expose a small versioned C ABI rather than a
language-specific ABI. Support memory-safe `no_std` Rust components on AArch64
and RISC-V without making a full Rust standard-library port a prerequisite.

## Architecture Portability

Keep architecture-neutral kernel and userspace code independent of compiler
architecture conditionals. AArch64 and RISC-V are current full-system targets;
maintain behavioral parity and smoke coverage between them. Add AMD64 as a
supported architecture, with architecture-specific integration isolated under
`arch/amd64` and matching userspace architecture directories.
