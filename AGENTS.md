## Source code organisation
### kernel
Non architecture specific core kernel code. 
Arch specific code in arch/ and linked in.

Interfaces to architecture are defined in kernel/arch

### arch
Architecture specific code. AArch64 and RISC-V are current full-system targets;
AMD64 is a future goal. Assembly and architecture specific integration code
goes here, along with bootloaders.

### Architecture selection
Architecture-neutral code must not use compiler architecture macros or conditional
compilation to select an implementation. Put target-specific code, ABI types, and
headers beneath an `arch/<target>` directory, and have the build select the matching
source files and include directories. Hosted test implementations must be selected
the same way through an `arch/hosted` directory.

### user
Userspace code. It includes commands and drivers

### test
Unit tests. Running tests is done with make test. A python script is used to
run the tests and generate the runners.

### assets
Project-owned visual and media assets, including official HundeOS logos, panic
artwork, and source material intended for documentation, boot visuals, release
artifacts, and future product integration. These files are not generated build
outputs. Do not delete, replace, optimise, rename, or otherwise modify anything
under `assets/` unless the user explicitly requests that exact asset change.
Build cleanup must never remove this directory.

# Instructions for making changes
## Check first
Plan and then tell the user what you plan to do before making changes.

## Deferred features
Treat [Goals](docs/GOALS.md) as project direction, not an implementation backlog.
Maintain [TODO](docs/TODO.md) for granular features discussed with the user
but not yet implemented. Read relevant entries before working on those features.
When a discussed feature is deferred, record or update it using the hierarchy
System -> Feature -> Supporting features or architecture. Distinguish agreed
requirements from open design questions; do not add speculative features or
treat a backlog entry as authorization to implement it. When work is completed,
update the entry to reflect what remains and link to the implementation docs.

## Compilation database
Run `make compile-commands` whenever source files are added or removed so IDEs
and other tooling receive the current build targets and compiler flags.

## Unit tests
### Rust
Organize code by responsibility, not language. Rust services belong under
`user/services`, drivers under `user/drivers`, shared libraries under `user/lib`,
and component fixtures under `user/components`. `user/Cargo.toml` is the shared
workspace; do not create a separate language-specific source tree.

Use the standard Rust test organization: unit tests live alongside implementation
in `#[cfg(test)] mod tests` modules, normally in the same source file. Large test
modules may use a separate module file. Crate-level integration tests belong in
the crate's `tests/` directory and exercise its public API; executable examples
belong in documentation tests. Run host-native tests with `make rust-test`, or
`cargo test --manifest-path user/Cargo.toml --workspace`.
Keep production crates `no_std` where required; their host-native tests may use
`std`. Test modules must not change production behavior through test-only branches.
Use small fakes for host operations. Test actual Wuff loading, ABI callbacks,
Channels, and fibre integration separately under QEMU on both architectures.

Use the shared `user/lib/hunde` crate for C ABI definitions, raw C imports,
request/result translation, component callback adapters, and runtime bootstrap.
Service and driver crates contain domain logic and thin registration declarations,
not copied FFI structs, unsafe request decoders, allocator/panic setup, or callback
trampolines. Extend the shared crate only for operations required by current
callers, and test translations alongside their shared implementation.

Use `hunde::protocol` for C I/O operation, status, and flag constants. These are
generated from the authoritative C enums during Cargo builds; do not duplicate
their numeric values in Rust. Keep Rust's payload-carrying operation types as
safe domain interfaces, not wire enums. Tests for malformed or unknown wire
values may use deliberately invalid numbers.

Service, driver, and component crates must inherit the workspace's
`unsafe_code = "forbid"` lint, covering all targets and tests. Only the shared
`hunde` compatibility crate may implement unsafe Rust. Do not weaken the lint or
move unsafe blocks into consumers, including for module loading. ABI/bootstrap
macros are defined in `hunde`; consumers only invoke their safe declarations.
Safe component loading accepts approved bundled component identities, not
caller-supplied paths. The library owns that trust registry and private raw
loading implementation; the host still chooses which component to load. Do not
make arbitrary native loading safe merely by hiding its unsafe block.

Rust service startup, component ownership, registration, dispatch, and execution
policy belong in Rust. Use Rust `main` entry points with the existing C CRT;
do not implement service control flow in a C launcher. Shared adapters under
`user/lib/hunde` may translate existing C library layouts and protocols, but must
not select components, hard-code service endpoints, or own host policy.

Use named constructors such as `Request::new(...)` for public/domain structs
instead of repeating initialization details in callers. Keep struct literals in
the owning type's implementation; named alternatives such as `try_new`, `of`,
or `from_result` are appropriate when they explain construction better. Use
`Result` for fallible construction, and implement `Default` when a meaningful
zero-argument default exists. Explicit C ABI records and malformed-wire test
fixtures may use literals within the shared compatibility layer and its tests.

Use rustfmt and Clippy from the pinned toolchain. Run `make rust-format` to format
Rust, `make rust-format-check` to check formatting, and `make rust-lint` for
Clippy with warnings treated as errors. The repository `format`, `format-check`,
and `hundelint` targets include these Rust checks. Clippy must cover hosted tests,
the core-only crate, and the selected target's production service/components;
run `make ARCH=aarch64 rust-lint-target` and `make ARCH=riscv64 rust-lint-target`
when changing shared or target-facing Rust code. Fix findings rather than adding
lint suppressions or excluding code from the checks.

### Python
Use pytest for testing Python code. Write module-level test functions, not test
classes or unittest.TestCase subclasses. Use pytest fixtures for shared setup
and dependencies, including tmp_path where appropriate. Inject dependencies
explicitly rather than monkeypatching globals or replacing attributes at runtime.
When test doubles are needed, use small fake implementations, not mocks or
mocking libraries. Do not use monkeypatch or unittest.mock.

### Kernel
Add unit tests for any new code in kernel. Some architecture specific code
may not have unit tests as it is difficult to test..

Stubs are often linked in to tests to isolate code under test or provide
implementations that are architecture specific in the codebase.

No dependencies on architecture specific code should be made 
in core kernel tests.

### Drivers and userspace programs
Properly used commands and drivers running in userspace should
be unit tested, stubbing out kernel interfaces. Prefer integration
style testing although tests of key functions can also be written

## Troubleshooting
Ensure you have evidence for a theory regarding a problem before
fixing. Ensure the user is informed of the theory and agrees to
the evidence.

# Code standards
## Reuse built-in types and operations
Read [Generic types, collections, and strings](docs/architecture/generic-types.md) before
implementing or changing collection, string, Optional, or Result operations.

Before writing a local helper or loop for searching, membership, filtering,
sorting, copying, parsing, or collection mutation, inspect the relevant shared
type's public API, macro-generated functions, and existing callers. Use the
built-in operation when it fits. Using an iteration macro does not justify
reimplementing an operation the collection already provides.

If an operation is missing, consider adding a small reusable function to the
owning generic type instead of duplicating it in a caller. Explain the gap and
why the function belongs there in the proposed change. Add tests alongside the
type's existing tests and update the guide when its public API changes. Keep
domain-specific logic in the caller, and do not add speculative APIs without a
current need.

## Result types (Some, None, Success, Error, etc)
These should be used in the kernel code in most cases. Raw pointers or NULLs
should not be used in the kernel or driver. Optional types should be used.
Syscall handlers are the only place where NULL checks are allowed, and only
for validating raw syscall boundary arguments before converting them to typed
kernel values.
Dont be stupid thinking you satisfy this requirement by using a 0 instead of a NULL

The matching macros to match results types should be used

Do not access Optional or Result internals directly, including fields like
`.present`, `.some.val`, success payload fields, or error payload fields. Use
`match_optional`, `match_result`, `unwrap_optional`, or `unwrap_result` instead.
The match and unwrap macros derive their types from their expressions; never
pass an explicit type argument. The only exception is inside the implementation
of the Optional/Result macros themselves.

## Strings in code
BStrings should be used. These are bounded with length stored independently to the buffer.

Do not access fields in the BString struct direcly, 
use the provided functions or add functions if they make sense and are needed.

## Arrays, HashMaps, etc
There are macros to create typesafe arrays, HashMaps, etc. These should be used.

There are Macros for iterating over fixedarrays etc. These should be used.

Do not access fields data structure structs directly,
use the provided functions or add functions if they make sense and are needed.

Generated collection typedefs such as names ending in `__type`, `__type_ptr`,
`__optional`, or `__value_type` are private implementation details. Do not use
them in callers. Match and unwrap macros derive their value types from their
expressions; never pass an explicit type argument.

## Pointers in kernel code
Pointers should be typedefed to a name following the convention
TypeNamePtr
- e.g. typedef Process * ProcessPtr

## Unused names
Do not add `(void) name;` statements to suppress unused parameter or unused variable warnings. Disable the relevant unused warning in the build flags instead.

## GOTO no go
Dont use goto statements, ever.

## Lint
Always run `make hundelint` after changing code. Treat any hundelint finding as
work that must be fixed, even though the command may exit 0. Do not describe
existing findings as acceptable baseline findings. Do not add ignores,
suppressions, or exclusions to hide lint findings unless the user explicitly
approves that exact suppression.

## critical_section
When using mutexes in the kernel, use the critical_section macros along
with their exit macros (i.e. cs_return)

Never hold a mutex or critical_section across an actual context switch. Code
that mutates shared scheduling, service, or event state should return an
explicit result describing whether a reschedule is needed, then release the
lock before any context switch or preemption path is triggered. Separate
"mark runnable / request reschedule" from "perform context switch now".

## Object orientation in c
An object orientated like namespacing convention is used. This means 
function/methods for a certain class are prefixed with the class name followed by TWO underscores `__`.
An example namespacing for a function would be `EmbeddedFileIO__remove`

Additionally the object being acted on should be the first argument to the function and be named `self` so
as to clearly indicate it is the object being acted on while not clashing with the c++ `this`.

## Return values
Use `Anything` for a scalar protocol field or callback argument whose meaning
intentionally varies by operation, such as a reply slot that may contain a
handle or a byte count. Convert it to the operation's concrete type at the
consumer boundary. Do not use `Anything` when the field has one fixed meaning;
use the domain type instead.

Do not pass multiple pointers to a function just to return several values. It
is a smell and should not be done. Return a struct with named fields instead.

Do not use `bool` status returns with pointer out-parameters for fallible
operations. If a function can fail and needs to return data, return
`Result(ValueType)` or `Optional(ValueType)` as appropriate. If several values
must be returned, define a small named result struct and wrap that in
`Result(...)`. Pointer parameters are acceptable for buffers that the caller
owns and the function fills, but not for ordinary scalar/result values such as
ids, offsets, entries, handles, or parsed metadata.

## Key kernel structures
When possible kernel structures should contain the other structures they use,
not pointers to structures. This is to make memory allocation/cleanup simpler

## Critical sections
In kernel code mutexes should be used along with critical_section macros

Critical sections must not call into code paths that can context switch before
the mutex is released.

## Control flow
Do not use `goto` in new or changed code. When modifying code near an existing
`goto`, refactor that control flow to use structured returns or helper
functions instead.

Do not use conditional compilation directives such as `#if`, `#ifdef`, or
`#ifndef` inside function bodies. Function control flow must remain visible in
every build. It is acceptable to define a complete function or macro
conditionally at file scope and call it unconditionally from the function.

## Test code
Use `TEST_ASSERT_EQUAL_BSTRING(expected, actual)` from `test/hundetest.h` for
BString equality assertions. Both arguments are BString values, for example
`TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/sysinfo"), *path)` when `path` is a pointer.
The macro evaluates each argument once. Do not convert BStrings
to C strings for equality assertions; lengths and embedded NUL bytes matter.

Do not add special test-only branches to production code. Tests should use
stubs, fixtures, dependency injection, or alternate build-linked
implementations instead of `#ifdef TEST` style conditionals in the code being
tested.

# Fixing issues
## Broken functionality takes priority
Broken existing functionality is the number one priority, ahead of new features,
migrations, refactoring, or the next planned slice. When a failure is discovered,
stop feature work, preserve the evidence, and prioritize investigation and a
validated root-cause fix, following the evidence and approval requirements below.
This applies even when the failure may predate the current changes or its cause
is not yet established.

A passing retry does not resolve an intermittent failure. Do not dismiss it as
unrelated without evidence, mark the affected functionality as fully validated,
or park it in TODOs and continue feature work. Recording a failure is not a
substitute for addressing it. Defer broken functionality or resume feature work
only with the user's explicit agreement, clearly stating what remains broken
and what validation is incomplete.

## propose a theory and evidence
- Tell the user what you plan to do before making changes. If there is no
evidence, the user will be asked to provide evidence.
- Dont just leap in to fixing things without consulting the user first

# Changing code
## propose a change
Tell the user what you plan to do before making changes.

## No speculative code
Do not add features, code branches, fields, parameters, flags, hooks, or
abstractions that are not needed for the current requirement. Avoid preserving
unused future flexibility. If a branch or field has no current caller or state
where it is required, leave it out until that requirement exists.

## Vendored source
Do not modify vendored source or add downstream patches that change vendored
source. Supply the platform interfaces that upstream software expects through
HundeOS-owned libc, headers, compatibility code, or build configuration. If an
upstream source change appears unavoidable, stop and discuss it with the user
instead of carrying a local patch.

## Write tests
Add unit tests for any new code in kernel. Some architecture specific code
may not have unit tests as it is difficult to test..

# Kernel architecture
Read [Shared buffers: hunde-puffer](docs/architecture/shared-buffers.md) before changing bulk
I/O transfer, buffer allocation, mapping permissions, recycling, or cleanup.
The agreed target uses separately shared directional buffers managed by
`hunde-puffer`; existing Channels carry descriptors, commands, and completions.
Do not treat inline Channel payloads as the bulk-transfer design or introduce
service-specific buffer transports. The document distinguishes requirements
from unimplemented behavior and open sizing/recovery decisions.

Read [Userspace I/O system](docs/architecture/io-system.md) before changing I/O routing,
file-handle ownership, filesystem service transport, or socket/stream service
interfaces. `hund-io-system` is the common application-facing I/O service;
filesystem, network, and stream protocols use the existing bidirectional Channel.

Read [Channels and event queues](docs/architecture/channels-and-event-queues.md) before changing
service discovery, shared queues, channel lifecycle, or managed shared-page memory.

Read [Queues](docs/architecture/queues.md) before changing
queue access permissions, connection establishment, or transport authentication.
It records the initial atomic message queue API and the remaining migration away
from globally writable service queues.

Read [Userspace interrupt service](docs/architecture/interrupt-service.md) before changing
device IRQ delegation, interrupt registration/authentication, or acknowledgement
and rearm handling. This document describes the current `hund-unterbrict` design.

Read [Userspace driver tracing](docs/architecture/driver-tracing.md) before changing
`DRIVER_DEBUG`, driver log transport, batching, or `/var/log/driver.log` ownership.
Ordinary driver diagnostics go through `hund-verfolgen`; independent kernel and
bootstrap diagnostics must not depend on that service or the root filesystem.

## Current microkernel architecture

HundeOS is a microkernel system. Keep the privileged kernel focused on:

* Scheduling, process lifecycle, and process identity.
* Address spaces, memory protection, and managed shared-memory mappings.
* IPC foundations, authentication, and hardware capability enforcement.
* Interrupt entry and routing, with device handling delegated to userspace.
* Minimal early boot, panic, and diagnostic output that works independently of
  userspace services.

Normal application and device I/O belongs in userspace. This includes filesystem
namespaces, file handles, filesystems, block and input drivers, console services,
sockets, and the `/boot`, `/dev`, and `/proc` information filesystems. These
services use Channels and managed shared buffers; do not add kernel filesystem,
file-descriptor, stream, socket, or block-I/O fallbacks.

# Other
- reload this file (AGENTS.md) every time there is user interaction

## Dont be stupid or lazy
- Dont twist user instructions in to something that is just a hack on the
  current design or implementation
- For example "lets bring in the BSD cat and make it work", doesnt mean
  to hack the current cat implementation to have a BSD license.
  This would require vendoring source code, and providing the
  interfaces it needs to compile and link.

## Dont be overconfident
- Dont leap in to solution mode changing code when the user gives you an error
panic or whatever. Gather evidence for your theory and
avoid confirmation bias therefore you should look for evidence that may
disprove your theory. 
- Once you have a theory present it to the user with a plan to be
approved. DO NOT MAKE CHANGES UNTIL THE USER AGREES
