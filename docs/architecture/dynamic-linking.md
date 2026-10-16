# Dynamic linking and host modules

## Scope and current state

HundeOS will use one userspace ELF loading foundation for shared libraries,
dynamically linked programs, and components loaded by `hunde-io-host` and
`hunde-treiber-host`. Host modules use standard ELF `ET_DYN` objects; they are
not a project-specific binary format.

The implementation does not exist yet. The current kernel executable loader
accepts static `ET_EXEC` images and rejects `PT_INTERP` and `PT_DYNAMIC`.
Ordinary userspace page allocation produces read/write, non-executable memory
and does not provide the fixed mapping and permission-finalization operations a
secure userspace loader requires.

Filesystem and library search policy remains in userspace. The kernel provides
validated address-space operations and enforces mapping permissions; it does not
resolve libraries, symbols, dependencies, or driver names.

## Loader boundary

The reusable userspace loader must:

- validate ELF headers, program headers, dynamic entries, string and symbol
  tables, relocation tables, and every overflow-prone range before use;
- load `PT_LOAD` segments at a valid chosen base with their required alignment;
- resolve declared dependencies and symbols according to documented search and
  visibility rules;
- implement only the reviewed AArch64 and RISC-V relocation sets, rejecting
  unsupported relocations rather than approximating them;
- relocate through temporary writable mappings and then establish final RX, RO,
  or RW permissions without leaving writable executable pages;
- synchronize instruction memory through the architecture interface after code
  relocation and before execution;
- run initialization arrays only after the complete object graph is valid and
  final permissions are installed; and
- roll back mappings, references, and partially initialized objects on failure.

The kernel memory interface therefore needs page-aligned fixed or suitably
aligned allocation, permission reduction or replacement, unmapping, and the
architecture-required instruction synchronization boundary. Authorization must
prevent ordinary writable memory from becoming executable contrary to process
policy. These mechanisms are address-space primitives, not filesystem or module
selection policy.

The loader is a userspace library shared by the program interpreter and both
hosts. Hosts add component policy and ABI validation around it rather than
implementing separate ELF loaders.

## Host module ABI

Each module exports a single well-known, versioned C entry point. The entry point
receives a versioned table of host operations and returns a named result value
containing either a component descriptor or an error. Descriptors contain size
and version fields so a host can reject incompatible modules before invoking
component operations.

The ABI uses fixed-width integers, `extern "C"` calling conventions, and
explicitly laid-out structures. It does not expose C macro-generated collection
layouts, Rust enums, Rust trait objects, C++ objects, allocator-private objects,
or language unwinding. Buffer descriptors and opaque handles cross the boundary;
language-owned references do not.

Host operations provide only the facilities required by the component type,
including allocation, logging, time, asynchronous work, and the applicable I/O
or device operations. A hardware component receives authorized MMIO, DMA, and
IRQ operations from `hunde-treiber-host`; an I/O component does not. Loading a
module grants the authority of its host and is therefore restricted to trusted
host configuration.

The initial lifecycle is load during host startup, initialize, and retain until
host exit. General `dlclose` and live driver replacement are not implied. They
require a later quiescence design covering callbacks, fibres, pending requests,
IRQ deliveries, shared-buffer leases, and DMA.

## Minimal Rust contract

Initial Rust components use `#![no_std]` and the built-in bare-metal AArch64 and
RV64IMAC targets. The project pins the Rust toolchain used to build them. A
HundeOS-specific Rust target and a full `std` port are not prerequisites.

The supported initial contract is:

- `core`, with `alloc` available only after the component supplies a global
  allocator backed by explicit host allocation operations;
- aborting panic behavior, with no unwinding across the module boundary;
- no thread-local storage, operating-system threads, or language runtime service;
- `extern "C"` exports and imports using `#[repr(C)]` ABI structures;
- compiler-required memory, integer, and supported-width atomic operations; and
- the same architecture feature and calling-convention assumptions as C
  userspace on each target.

A Rust panic terminates the containing host through its fatal-error path. Rust
does not make an in-process component an isolation boundary. Unsafe code remains
necessary at the ABI, MMIO, DMA, and shared-buffer edges and must be kept in
small reviewed adapters.

The compatibility smoke test is separate from the C sysroot smoke. For both
architectures it must cover C-to-Rust calls, Rust-to-host callbacks, ABI layout,
allocation when `alloc` is enabled, supported atomics, ELF relocation, final
segment permissions, and execution of a loaded module.

## Deferred runtime facilities

Thread-local storage, unwinding, a full Rust standard library, general-purpose
`dlopen`/`dlclose`, shared-library page deduplication, and live component upgrade
are not part of the minimum host-module implementation. Add them only for a
current caller with defined lifetime and failure semantics.

## Verification

Unit tests cover malformed objects, dependency cycles, symbol lookup,
architecture relocations, permission transitions, constructor ordering, and
complete rollback after every failure stage. Full-system tests load and execute
one C component and one Rust component on AArch64 and RISC-V, reject an
incompatible ABI version, and verify that executable pages are never writable
after initialization.

See [I/O and driver hosts](io-driver-hosts.md) and the concrete work in
[TODO](../TODO.md).
