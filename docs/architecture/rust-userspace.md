# Rust Userspace

This Cargo workspace contains the Rust I/O host, its socket, pipe, memfs, and procfs components, and the
shared C ABI and async/fibre adapter. Production builds use `no_std`, `core`,
and `alloc` with allocation supplied by the containing host.

The workspace manifest is `user/Cargo.toml`. Rust code lives in the normal
responsibility-based tree alongside C code:

- `user/services/hunde-io-host`: service startup and execution policy.
- `user/drivers/socket`: the dynamically loaded socket component.
- `user/drivers/memfs`: the dynamically loaded temporary filesystem component.
- `user/drivers/procfs`: the dynamically loaded process-accounting filesystem component.
- `user/drivers/sysinfofs`: the dynamically loaded read-only `/boot` component;
  boot information is loaded and formatted once during fallible initialization.
- `user/drivers/ext2fs`: the safe Rust EXT2 engine serving the standard QEMU
  root and hosted volumes. Its explicit per-mount initialization receives a
  versioned host-owned block capability; it does not require `Default`.
- `user/drivers/fat32fs`: the safe Rust FAT32 engine and `IoComponent` adapter,
  including allocation, file/directory mutations, authenticated handles, and
  inheritance, with source-local sparse fixtures and write-failure tests.
  Its `fat32.wuff` uses the same per-mount block capability ABI as EXT2.
  Configured FAT32 mounts now use this component; the C provider is retired.
- `user/services/hund-rohr`: the dynamically loaded pipe component.
- `user/lib/hunde`: shared Rust compatibility crate and C library adapters.
- `user/components/rust_smoke`: lifecycle/ABI smoke component.

There is no separate Rust source tree. Cargo manifests, sources, unit tests,
and crate-level `tests/` directories belong with the component they describe.

## Unsafe Boundary

Every service, driver, and component inherits `unsafe_code = "forbid"` from the
workspace, including its tests and executable targets. Only `hunde` implements
unsafe Rust, and it requires explicit unsafe blocks inside unsafe functions.
This boundary is checked by Cargo/Clippy and compiler-rejection regression tests.
Do not add lint overrides to make unsafe application code compile.

Consumers invoke bootstrap/ABI macros defined in `hunde`; unsafe definitions in
those external macros are library-owned, not a license to write unsafe blocks
or C entry functions in services or drivers.

The host calls `LoadedComponent::load(BundledComponent::Socket)` through a safe
API. The closed registry resolves only approved socket, pipe, memfs, and procfs images shipped under
the read-only `/sbin` bootstrap mount. Arbitrary path loading and ABI invocation
remain private unsafe implementation in `hunde`. The host selects the component;
the registry establishes its trusted identity. This assumes trusted packaged
code, not a sandbox for arbitrary native binaries, and descriptor validation
does not prove arbitrary callbacks memory-safe. Additional bundled components
require a reviewed registry entry and build integration.

## Shared C boundary

`hunde` (`user/lib/hunde`) is the single Rust compatibility crate for the
existing C libraries and host interfaces. Consumers must not copy ABI layouts,
raw C declarations, request/result conversions, callback trampolines, or runtime
bootstrap. Its modules own:

- `abi`: fixed C layouts, tags, layout assertions, and C function declarations.
- `io`: semantic IDs, operations, and bounded borrowed-request translation.
- `devfs`: an owned safe adapter over the existing C device filesystem and
  live device registry, hosted directly in the I/O process. It retains
  authenticated session identities for native handle cleanup; the shared
  native filesystem codec is also used by embedfs.
- `terminal`: the shared C terminal-attribute layout and safe owned stream
  clients for console backends. Explicit service opens reuse Channel
  authentication and managed directional buffers, without routing back into the
  console alias. Terminal-control requests use the existing ABI's bounded
  input/output buffers and foreground offset; services contain no raw FFI.
- `inheritance`: shared authenticated ownership/grant bookkeeping for pipes and memfs.
- `process`: safe coherent process snapshots and bounded kernel-trace reads through host callbacks.
- `block`: typed sector I/O and an owned native block-client adapter, reusing existing Channels and managed buffers.
- `protocol`: generated C I/O operation, status, and flag constants.
- `component`: typed component traits, registration, lifecycle, and C callbacks.
- `runtime`: host-backed component allocation, libc-backed executable allocation,
  Rust executable entry points, and fatal panic handling.
- `fibre`: Future polling and fibre parking, including wake notifications.
- `service` and `wuff`: the existing C I/O-service and module-loader adapters.

The socket crate implements `IoComponent` and uses `hunde::io_component!`.
The I/O host uses `hunde::executable!`; the smoke component implements
`LifecycleComponent` and uses `hunde::lifecycle_component!`. These declarations
generate the required per-image entry points and runtime items from shared
implementations. Component crates need no handwritten unsafe C adapter code.

The host's Rust `main` lives in `user/services/hunde-io-host/src/lib.rs`;
startup and execution policy live in `src/host/mod.rs`. Rust owns
the component, validates its descriptor, registers the endpoint, dispatches
callbacks, publishes readiness, and drives the fibre scheduler and Future.
`user/lib/hunde` owns shared C/native translations and the approved bundled-image
registry and entry invocation. It does not choose which subsystem to start,
select service endpoints, or own the host control loop. Native values live in stable Rust-owned allocations with layouts
reported by C, avoiding duplicated private C struct layouts. Service lifetimes
borrow their Rust request handler exclusively; subsystem handlers own their
loaded components, whose disposal precedes releasing their Wuff mappings.
The service transport is currently process-lifetime. These are not hot-reload APIs.

### Host subsystems

Every host module has its own directory beneath `src`: `host/`, `filesystem/`,
`socket/`, and `streams/`. `lib.rs` only declares modules and the executable entry.
The host owns orchestration and the single shared scheduler. Subsystems select
and own their components and define endpoint names and readiness messages.
The filesystem module currently owns independent `/tmp` and `/proc` instances;
the socket module owns `socket_router`, and streams owns the retained `hund-rohr`
pipe endpoint. The filesystem module also accepts configured `ext2.wuff` and
`fat32.wuff` mounts
through authenticated mount control, with a separate fibre per mounted volume.
This does not move namespace/rootfs ownership.

`host::Subsystem` extends the existing `IoComponent` contract rather than
introducing another request protocol: `execute(Request)` returns
`Result<Option<Reply>, IoError>` (`None` means pending), and `disconnect(SessionId)`
releases session resources. The request carries authenticated session/process
identity and bounded borrowed buffers. Endpoint metadata completes the host
contract. Service discovery names use `hunde::service::ServiceName`; startup
diagnostics use the distinct `ReadinessMessage` type. Names hold either borrowed
static or owned configured UTF-8 text; readiness messages hold static text.
Named constructors distinguish their roles instead of interchangeable byte slices. The shared
service adapter alone converts them to C byte spans, and readiness diagnostics
are logged only after successful readiness publication. Fallible `load`
constructors initialize components; Rust ownership and
drop ordering provide cleanup, not an unused generic shutdown hook.

The shared `Service::listen` adapter accepts any `IoComponent` handler, so native
Channel callbacks dispatch through the actual subsystem before calling its Wuff.
Only `hunde` translates between typed requests/replies and the component's C ABI;
borrowed buffers are forwarded synchronously without copying bulk data or adding
an IPC hop. Existing bundled-image trust checks remain unchanged. All four
endpoints register before any publish readiness, and each polling pass visits
every endpoint even when an earlier endpoint makes progress. Their current
poller remains in one root fibre on the shared scheduler.

### One scheduler, multiple fibres

`Scheduler::run` owns one native scheduler for the process. Its root closure
receives a `SchedulerParker`; `parker.spawn(move |parker| ...)` starts another
fibre on that same scheduler and returns a joinable `TaskHandle`. Spawned
closures own their captures (`'static`), while the root may borrow local state.
The native default limit is 16 live fibres, including the root. Each fibre has
its own stable callback/context record; a failed spawn releases its captures
without changing already scheduled work.

`TaskHandle` is a Future yielding the task's `Result<(), IoError>`. Dropping
the handle detaches rather than cancels work. Task errors are delivered through
the handle and do not abort siblings. `run` drains all fibres before returning
the root's result; joining/detaching a permanently running service does not
provide shutdown or cancellation. Infrastructure failure with live fibres is
fatal, rather than freeing storage referenced by suspended stacks.

Each task can use `block_on` with its own parker. A Channel event wakes every
parked Rust polling fibre to recheck its own state. Task completion also wakes
parked fibres, so a joiner does not wait for an unrelated external event.
Completion releases its state borrow before waking a joiner. The outer loop
only enters the existing generation-checked Channel wait after runnable work
has drained. This is cooperative, single-threaded execution, not a per-component
scheduler, thread pool, timer runtime, or general external-thread Waker bridge.
The host still polls its existing four endpoints in one root fibre; independent
mount/control work can now use additional fibres on the same scheduler.

While native fibres run, the shared library installs a scoped `QueueWaitAdapter`.
C Channel/block readable, writable, and arrival waits suspend the current fibre
and recheck their condition after wakeup, rather than sleeping the process.
The adapter is restored on both successful and failed scheduler runs; fork
reset clears it. Outside a scheduler the original kernel wait remains in use.
The outer scheduler alone sleeps the process once all runnable work is drained.

Yielding makes native calls reentrant on this one OS thread. `QueueWaitGate`
therefore gives one fibre ownership of an entire shared transaction, parking
contenders and waking them on release, including error cleanup. The cached
buffer-manager Channel is protected from lazy connection establishment through
reply/reset. Wuff module-loading I/O is likewise serialized because it uses the
default C I/O client. Per-volume block clients remain independently owned by
their volume fibres. These gates are cooperative ownership, not kernel mutexes
or cross-thread synchronization; they do not make arbitrary native clients
safe for concurrent use.

## Protocol Constants

The C definitions remain authoritative: `IORequestOperation` in
`common/ipc/io_protocol.h`, and `IOStatus`/`IOFlags`/`IOTransferFlags`/`IOSeekWhence` in
`common_includes/syscalls/io.h`. `hunde`'s Cargo build script runs
`tools/generate_rust_io_constants.py`, which asks host Clang to evaluate those
architecture-neutral enums. This handles expressions, aliases, and implicit
increments using C compiler semantics rather than parsing C expressions in a
second language.
Filesystem mode and directory-bound macros are evaluated through a generated
C enum in the same Clang invocation flow, rather than copied numerically.

Bindings are generated into Cargo's `OUT_DIR`, not checked into source control,
and exposed as `hunde::protocol`. Cargo watches both defining headers and the
generator; Make's Rust artifact dependencies include the same inputs. Generation
fails if Clang cannot evaluate a definition or a value cannot fit its wire field
(`u32` operations, `i32` statuses, `u64` flags). Builds, including direct Cargo
commands and core-only builds, require `python3` and `clang` on `PATH`; no Python
packages or Rust build dependencies are required by this generator.

Rust's borrowed `Operation` enum remains a domain type, not a C ABI enum. Request
decoding matches the generated `IO_REQUEST_*` constants, flags re-export the C
`IO_OPEN_*` values, and socket I/O errors use named generated `IO_*` statuses.
Unrelated loader/runtime error domains must not be renamed by numeric
coincidence. Do not cast unvalidated integers into Rust enums.

Generator tests cover C expressions, implicit increments, changed headers,
out-of-range fields, missing/invalid declarations, and independently compile C
static assertions for every generated repository constant. Run them with
`uv run python -m pytest test/tools/test_generate_rust_io_constants.py`.

This is source/library reuse, not a dynamically shared Rust runtime. Each linked
image retains its own runtime state and host-table binding. Only the versioned C
ABI crosses Wuff boundaries; Rust objects and allocator ownership do not.

The default `alloc` feature supports allocating services and I/O components.
The lifecycle smoke disables it and still uses the same crate's core-only ABI,
logging, lifecycle, and panic adapters. It does not need an allocator or a larger
host table. Channel queues remain behind the existing C service transport; this
refactor does not introduce a raw Rust queue protocol or broad new OS APIs.

## Tests And Builds

Use named constructors such as `Request::new(session, pid, handle, operation)`
for public/domain values rather than repeating struct literals in callers.
Construction details belong in the type's implementation; use `Result` for
fallible construction and `Default` for meaningful default initialization.
Explicit ABI records and malformed-wire fixtures remain in the shared adapters
and tests.

The pinned toolchain supplies rustfmt and Clippy:

- `make rust-format` formats all workspace Rust, including the smoke component.
- `make rust-format-check` checks formatting without modifying files.
- `make rust-lint` checks native tests, core-only code, and the selected target's
  production host, socket, and smoke component with `-D warnings`.
- `make ARCH=riscv64 rust-lint-target` checks the RISC-V production configurations;
  the default architecture is AArch64.

Repository-wide `make format`, `make format-check`, and `make hundelint` include
the corresponding Rust commands. Fix findings rather than adding lint allows or
excluding modules. Use source-local unit tests for constructor and ABI behavior.

Run native tests with `cargo test --manifest-path user/Cargo.toml --workspace`.
`make rust-test` is the repository entry point and is included in `make test`.
Unit tests use `#[cfg(test)] mod tests` alongside implementation. Large modules
may put that test module in a separate file. Public-API integration tests belong
in each crate's `tests/` directory; documentation examples use doc tests. Hosted
tests may use `std`; production behavior must not depend on test-only branches.
Use small fake implementations for OS operations rather than mocking globals.

Native tests exercise state transitions and Future scheduling. Cross-target
builds and QEMU tests separately exercise the real C ABI, Wuff loader, protected
Channels and underlying queues, managed I/O buffers, and stackful fibres.

The I/O host publishes the existing `socket_router`, `hund-rohr`, and `/tmp` endpoints.
The existing `hund-io-system` router redirects socket clients and pipe creation
to these endpoints; operations run locally inside dynamically loaded Rust modules.
The one polling fibre polls all three services on every pass, including when one
has a retained blocking request. Pipes use source-local Rust unit tests for
capacity, atomicity, nonblocking behavior, ownership, cleanup, and inheritance;
the QEMU socket-component smoke also exercises Bash pipelines and coexistence.
Full-system hosted-I/O acceptance passes on AArch64 and RISC-V: external
multi-stage pipelines, inherited builtin reads, 70,000-byte transfers, socket
coexistence, and the absence of standalone pipe/socket processes. Libc refreshes
the child's cached PID before inherited Channel claims, so attachment uses the
authenticated child identity. Bulk tests use smaller formatted chunks and a
builtin reader to avoid the separate wide-format truncation limitation in
[TODO](../TODO.md). Hosted memfs now supports exec inheritance; disk filesystem
and stream services retain their current implementations.
The same smoke covers hosted memfs: redirected external commands, inherited
file descriptions, append, directory pagination, file copies, and removal,
with no standalone memfs process.
The hosted procfs checks read every process-accounting leaf, list the live
directory, inherit a snapshot through a redirected external command, and read
`/proc/ktrace` (which may legitimately be empty when memory tracing is disabled).
They also verify that no standalone procfs process runs.
The same procfs assertions are separately runnable as
`test_qemu_rust_procfs_component` on AArch64 and RISC-V. This focused case passes
on both targets; it does not replace or skip the combined I/O smoke, whose
intermittent RISC-V console/pipe stalls remain tracked.
A complete run passes on both architectures; repeated RISC-V runs can still
time out, including with only a partial command echo. This reliability issue
and the unconfirmed cause are recorded in [TODO](../TODO.md); a passing run
does not establish repeatability.

Build the real service/module with `make ARCH=aarch64 rust-io` or
`make ARCH=riscv64 rust-io`. The component's `no_main` link target enables full
LTO of `core` and `alloc` into PIC; linking the prebuilt bare-metal archives
directly would retain non-PIC panic metadata. All exported interfaces use the
versioned C ABI in `common_includes/hunde/io_component.h`. I/O uses version 6
and `hunde_io_component_entry_v6`; the separate lifecycle ABI remains version 1.
Version 6 appends the boot-information snapshot callback to the host table
(72 bytes). C and Rust share the generated version constant; old I/O tables
are rejected. `hunde::boot` alone normalizes native optionals, union members,
and result tags into typed Rust records.

Ordinary components use the default constructor adapter; components requiring
fallible initialization can supply their constructor to `hunde::io_component!`.
The shared `load_with` adapter installs and validates host operations first,
and publishes no callbacks if construction fails. Sysinfofs uses this form so
boot-data loading and snapshot rendering finish before endpoint readiness.
Requests include managed-buffer availability and inheritance metadata.
`Reply::Pipe` returns two named handles and `Reply::Inheritance` returns a
typed token plus access rights; shared adapters translate these to C replies.
`Reply::FileInfo` and `Reply::Directory` carry typed filesystem metadata.
`Operation::WatchFile` and bounded `Operation::WaitFileChange` batches use
typed handles and generations; `Reply::FileChange` returns named events. ABI v5
adds a dedicated completion record, so v4 components are rejected rather than
called with an incompatible return layout. Watch batches are copied from native
byte records without assuming alignment or retaining borrowed Channel storage.
The C adapter writes only the requested protocol union member, preserving inline
read buffers. The host's credential callback returns kernel-derived UID/GID
snapshots; missing processes fail closed, while close/disconnect do not require
fresh credential resolution.

The host operation table and its callbacks must remain valid while the service
or component can allocate, free, or report a fatal error. Callbacks execute
serially in the containing host; a request's buffer pointers are borrowed only
until its execute callback returns. Pending operations keep owned socket state,
not borrowed request memory. Components stay mapped until process exit.

The unsafe generated entry points require live, correctly aligned C host tables
and contexts of the declared ABI. Request spans must describe accessible memory;
read targets must be exclusively writable for the duration of the callback.
These are C-adapter obligations, not requirements imposed on safe component
implementations. Components use typed requests and return Rust Results; only the
shared compatibility crate decodes pointers and constructs C completion records.
