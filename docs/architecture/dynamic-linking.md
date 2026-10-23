# Dynamic linking and host modules

## Scope and current state

HundeOS will use one userspace ELF loading foundation for shared libraries,
dynamically linked programs, and components loaded by `hunde-io-host` and
`hunde-treiber-host`. Host modules use standard ELF `ET_DYN` objects; they are
not a project-specific binary format.

HundeOS shared libraries use the `.wuff` suffix. ABI-major versions follow the
suffix: libc's first runtime `SONAME` is `libc.wuff.1`, with `libc.wuff` as its
native link-time name. The development sysroot may expose `libc.so` as a
linker-only alias so conventional `-lc` and upstream configure tests continue to
work; runtime dependencies record `libc.wuff.1`. The suffix does not change ELF
format or dynamic-section semantics.

The build can produce PIC `libc.wuff.1` objects for AArch64 and RISC-V with
`make sysroot-wuff-all`; the normal sysroot remains statically linked. RISC-V
uses LLVM LLD for `.wuff` output because the bare-metal GNU linker does not
support shared objects. `make wuff-smoke-all` produces dependency-free test
objects for both architectures. `make wuff-dynamic-smoke-all` produces a PIE
executable for each architecture with `/sbin/wuff-ld` as its interpreter and
`libc.wuff.1` as its runtime dependency.

The disk-root commands `cat`, `cp`, `env`, `fs-latency`, `grep`, `kill`, `ln`,
`ls`, `mkdir`, `mv`, `ps`, `rm`, `shutdown`, `tail`, and `write`, plus Bash, are
dynamically linked on AArch64 and RISC-V. The disk root stages `libc.wuff.1` in
`/usr/lib`, and each command records only that runtime dependency. A common build rule
compiles their HundeOS and imported OpenBSD objects as PIC, links them as PIEs,
and validates the interpreter, dependency, metadata, and architecture-specific
relocation contract. Bash uses the same contract while retaining its bundled
static support libraries and PIC `libm`; only libc is a runtime dependency.
RISC-V Bash also statically links a dedicated PIC compiler-runtime archive for
the soft-float binary64 arithmetic, comparison, and integer-conversion helpers
required by Bash and `libm`. This avoids text relocations from the bare-metal
toolchain's non-PIC `libgcc.a`; host differential tests cover the implemented
IEEE-754 operations against hardware binary64 results.
`/sbin/wuff-ld`, embedded programs, early bootstrap services, and drivers remain
statically linked: the interpreter, filesystem
path to `/usr/lib`, and the I/O services needed to read it must exist before
dynamic loading can begin. Existing full-system coverage exercises Bash and the
commands on both architectures.

The reusable userspace loader validates and maps ELF64 `ET_DYN` objects, reads
SysV hash-backed symbol and string tables, resolves dependency graphs and
imports, and applies the AArch64 and RISC-V relocations emitted by the current
libc build. It supports `RELATIVE`, absolute 64-bit, AArch64 `GLOB_DAT`, and both
architectures' `JUMP_SLOT` relocations, including RELA PLT tables. It finalizes
each load region as RX, RO, or RW, then makes `PT_GNU_RELRO` pages read-only and
rolls back the complete graph on failure. It has injected memory and dependency
operations for hosted tests and syscall-backed memory operations for HundeOS.
The initial filesystem dependency provider accepts only plain `DT_NEEDED` names,
loads them from `/usr/lib`, limits each image to 16 MiB, and releases its file
buffer as soon as the ELF loader has copied the loadable content. The loader
also validates a nonzero ELF entry against an executable mapping and exposes the
root entry to an interpreter. A graph root may contain exactly one bounded
`PT_INTERP` naming `/sbin/wuff-ld`; dependencies and the single-object loader
reject interpreters. Constructors and TLS remain unsupported.

The current PIE smoke executables and disk-root commands use SysV hash, string
and symbol tables, `DT_NEEDED`, PLT metadata, immediate binding flags, and
AArch64 or RISC-V `JUMP_SLOT` relocations. The commands may additionally emit
`RELATIVE` and AArch64 `GLOB_DAT` or RISC-V absolute 64-bit relocations. Their
load contract includes `PT_DYNAMIC` and GNU RELRO. PLT-only objects do not need
`DT_RELAENT`; that entry-size tag is required only when a regular `DT_RELA`
table is present. Build checks reject text relocations, TLS
segments, initialization and finalization tags, a different interpreter or
dependency, and any relocation outside each executable's reviewed architecture
set. The reusable loader ignores non-operational linker metadata but validates
and uses the tables that affect loading. New emitted metadata is reviewed before
the contract expands.

The kernel executable loader maps static `ET_EXEC` images directly. It also
recognizes the constrained dynamic executable contract described below and
redirects accepted images through the static interpreter. Ordinary userspace
page allocation produces read/write, non-executable memory. The memory syscall
interface also provides aligned allocation, exact-address allocation, and
page-aligned permission replacement within dynamic-loader mappings.

Filesystem and library search policy remains in userspace. The kernel provides
validated address-space operations and enforces mapping permissions; it does not
resolve libraries, symbols, dependencies, or driver names.

`WuffLoader__load_graph` receives a named root image and an injected dependency
provider. The provider owns path construction, filesystem access, and library
search policy; the reusable ELF layer requests exact `DT_NEEDED` names and
requires the returned image name to match. A successful provider load transfers
ownership of the image to the graph loader. The loader invokes the provider's
release operation after preparation on both success and failure; mapped objects
never retain pointers into provider buffers. Objects are appended in
breadth-first dependency order and repeated names are loaded once, so cycles
terminate without special-case graph edges. The root object is first in the
global lookup scope, followed by its breadth-first dependencies. The first
visible global or weak definition wins. Hidden and internal symbols are not
exported, protected and hidden definitions bind locally, and an unresolved weak
import binds to zero. Any missing strong import, provider failure, malformed
object, relocation error, or permission failure disposes every mapping
accumulated by the graph.

## Initial library search

The interpreter and hosts use the filesystem provider for the initial runtime
policy. A dependency name must be nonempty and contain no `/`; the provider
constructs `/usr/lib/<name>` and reads exactly the size reported by
`hund-io-system`. Direct paths, relative paths, the current directory, RPATH,
RUNPATH, and `LD_LIBRARY_PATH` are not searched. `LD_LIBRARY_PATH` may be added
later in the userspace interpreter, but it is not part of bootstrap linking and
will require rules for privileged or capability-bearing programs.

## Interpreter integration

On AArch64, libc and programs share the export policy in `user/ports/wuff_program.map`:
normal symbols remain public, while the linker's synthetic `_stack` marker is
private. The libc link has a post-link check rejecting that marker in its
dynamic symbol table. On AArch64 its fixed value can fall into an unmapped
segment gap as code layout changes; it is not a runtime stack allocation or a
libc API. Hiding this toolchain artifact keeps valid images loadable without
relaxing the runtime loader's symbol-range validation.
RISC-V's linker does not define this marker, so its existing build-selected
flags omit the version script; the post-link absence check still applies.

The initial interpreter is a statically linked `/sbin/wuff-ld`. The kernel only
parses enough `PT_INTERP` metadata to select it: a dynamically linked executable
must contain exactly one bounded NUL-terminated interpreter path equal to
`/sbin/wuff-ld` and a `PT_DYNAMIC` segment. The interpreter itself must not have
`PT_INTERP`. The kernel does not load dependencies or apply dynamic relocations.

For an accepted dynamic executable, exec replaces the requested image with the
static interpreter while preserving the original executable path, argv, envp,
working directory, inherited I/O description payload, and process identity. The
interpreter reopens the original executable through `hund-io-system`, loads its
graph, and transfers control to the validated root entry using HundeOS's
existing register startup ABI. Constructors remain deferred. Its private startup
path must not claim inherited POSIX I/O descriptions; the target program's
ordinary `_start` does that exactly once after control is transferred.

The interpreter binary is present in the embedded `/sbin` filesystem. Its
private startup initializes libc process state without claiming inherited I/O,
resolves the preserved executable path against the preserved working directory,
reopens the root image through `hund-io-system`, loads the dependency graph, and
transfers the original startup ABI values to the validated root entry. Kernel
exec classifies the requested image before mapping it. An accepted dynamic image
is discarded, `/sbin/wuff-ld` is fetched through a fresh `hund-io-system`
channel, and that static image is launched under the requested program's name.
Overlay exec retains its existing payload for the interpreter and target;
ordinary spawn retains the requested path in `argv[0]` and has the existing
empty environment and root working directory.

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

The loader computes the complete page-aligned `PT_LOAD` span, including gaps,
and obtains one backing allocation with `MEM_ALLOC_ALIGNED_PAGES`. The span's
start is rounded down to the maximum load-segment alignment so the chosen load
bias satisfies every segment's alignment, including images with a nonzero first
load page. Segment records are views within that allocation, not separately
owned mappings. Selecting a base from only the first segment's size is unsafe:
later fixed-address segments can otherwise collide with unrelated allocations.

The allocation begins RW/NX for copying and relocation. Finalization first
sets the whole span to `MEM_PAGE_NONE`, then applies each segment's RX, RO, or RW
permissions and finally RELRO. Gaps and leading alignment padding remain
inaccessible; no RWX permission exists. `MEM_PROTECT_PAGES` splits mapping metadata
without splitting ownership. Executable transitions synchronize instruction
memory through the architecture implementation before publishing the executable
mapping. Rollback and unload release the backing allocation once, not once per
segment; `WuffObject__dispose` retains ownership if a failed free must be retried.

Separate per-process mapping ranges track page permissions while one allocation
record retains ownership of each backing allocation. Partial protection splits
only the mapping ranges; free and process-exit cleanup still release the backing
allocation once, and fork reproduces the split permissions in the child. These
mechanisms are address-space primitives, not filesystem or module-selection
policy.

The loader is a userspace library shared by the program interpreter and both
hosts. Hosts add component policy and ABI validation around it rather than
implementing separate ELF loaders.

Host module callers use `WuffLoader__load_module_graph` or the filesystem-backed
`WuffFilesystem__load_module`. Module roots reject `PT_INTERP`, while their
dependencies retain the ordinary shared-library rules. The filesystem helper
releases the temporary root image after the loader has copied its loadable
content, on both success and failure. `WuffGraph__find_root_export` returns a
`Result(WuffSymbol)` for a required export and searches only the root module, so
a dependency cannot accidentally satisfy the component entry-point contract.
The returned graph owns its mappings and supports rollback through
`WuffGraph__dispose`.

## Host module ABI

Each module exports a single well-known, versioned C entry point. The entry point
receives a versioned table of host operations and returns a named result value
containing either a component descriptor or an error. Descriptors contain size
and version fields so a host can reject incompatible modules before invoking
component operations.

The common lifecycle ABI is declared in `common_includes/hunde/host_component.h`.
Version 1 exports `hunde_component_entry_v1`. The entry receives
`HundeHostOperationsV1` and returns `HundeComponentLoadResult`; initialization,
disposal, logging, and fatal-error calls use explicitly tagged Result records.
Byte spans are borrowed only for the duration of a call. The common descriptor
does not expose Channels, Queues, fibres, Wuff loader state, or language-owned
collections. I/O and hardware hosts add separately versioned operation tables
for their component types rather than extending the common lifecycle structure.

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

The first compatibility object uses Rust 1.99.1, the
`aarch64-unknown-none-softfloat` and `riscv64imac-unknown-none-elf` targets, PIC,
and aborting panic behavior. Its C and Rust definitions both assert every common
ABI size and offset. The linked module has one exported symbol, no interpreter,
runtime dependency, TLS, constructor, finalizer, or text relocation, and uses
only relocations already accepted by the Wuff loader.

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

The first allocating Rust service/component pair is `hunde-io-host` and
`socket.wuff`, built from the Cargo workspace in `user`. Its versioned
I/O table adds explicit host allocation/free operations and borrowed request
buffers to the common tagged-result contract. Full LTO at the component link
step regenerates bundled `core`/`alloc` as PIC instead of retaining non-PIC
bare-metal archive metadata. See the
[socket slice](io-driver-hosts.md#first-rust-socket-slice) for Channel, queue,
fibre, lifecycle, and test boundaries. The shared `hunde` compatibility crate
owns these C translations for the host, socket, and lifecycle smoke; the latter
disables its `alloc` feature and retains the original non-allocating host ABI.

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
