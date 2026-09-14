## Source code organisation
### kernel
Non architecture specific core kernel code. 
Arch specific code in arch/ and linked in.

Interfaces to architecture are defined in kernel/arch

### arch
Architecture specific code. Currently aarch64. Assembly and architecture specific
integration code goes here, along with bootloaders

### user
Userspace code. It includes commands and drivers

### test
Unit tests. Running tests is done with make test. A python script is used to
run the tests and generate the runners.

# Instructions for making changes
## Check first
Plan and then tell the user what you plan to do before making changes.

## Compilation database
Run `make compile-commands` whenever source files are added or removed so IDEs
and other tooling receive the current build targets and compiler flags.

## Unit tests
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
Read [Generic types, collections, and strings](docs/generic-types.md) before
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

## Write tests
Add unit tests for any new code in kernel. Some architecture specific code
may not have unit tests as it is difficult to test..

# Kernel architecture
Read [Userspace I/O system](docs/io-system.md) before changing I/O routing,
file-handle ownership, filesystem service transport, or socket/stream service
interfaces. `hund-io-system` is the common application-facing I/O service;
filesystem, network, and stream protocols use the existing bidirectional Channel.

Read [Channels and event queues](docs/channels-and-event-queues.md) before changing
service discovery, shared queues, channel lifecycle, or managed shared-page memory.

Read [Queues](docs/queues.md) before changing
queue access permissions, connection establishment, or transport authentication.
It records the initial atomic message queue API and the remaining migration away
from globally writable service queues.

Read [Userspace interrupt service](docs/interrupt-service.md) before changing
device IRQ delegation, interrupt registration/authentication, or acknowledgement
and rearm handling. This document describes the proposed `hund-unterbrict` design.

## We will be working towards a "millikernel" architecture
This means that the kernel is neither monolithic or split up in to micro concerns.

* IO will start in the kernel
  * Certain subsystems may run in userspace
* Memory management will start in the kernel
  * Certain processes may be able to affect page tables in the future
* Process management will live in the kernel
* IPC primitives will start in the kernel (message passing)
* Later on certain interrupts will be passed to specialised processes

# Other
- reload this file (AGENTS.md) every time there is user interaction

## Dont be overconfident
- Dont leap in to solution mode changing code when the user gives you an error
panic or whatever. Gather evidence for your theory and
avoid confirmation bias therefore you should look for evidence that may
disprove your theory. 
- Once you have a theory present it to the user with a plan to be
approved. DO NOT MAKE CHANGES UNTIL THE USER AGREES
