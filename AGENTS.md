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

## Unit tests
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
`Match`, `ResultMatch`, `Unwrap`, or `ResultUnwrap` instead. The only exception
is inside the implementation of the Optional/Result macros themselves.

## Strings in code
BStrings should be used. These are bounded with length stored independently to the buffer.

Do not access fields in the BString struct direcly, 
use the provided functions or add functions if they make sense and are needed.

## Arrays, HashMaps, etc
There are macros to create typesafe arrays, HashMaps, etc. These should be used.

There are Macros for iterating over fixedarrays etc. These should be used.

Do not access fields data structure structs directly,
use the provided functions or add functions if they make sense and are needed.

## Pointers in kernel code
Pointers should be typedefed to a name following the convention
TypeNamePtr
- e.g. typedef Process * ProcessPtr

## Unused names
Do not add `(void) name;` statements to suppress unused parameter or unused variable warnings. Disable the relevant unused warning in the build flags instead.

## GOTO no go
Dont use goto statements, ever.

## CritcalSection
When using mutexes in the kernel, use the CriticalSection macros along
with their exit macros (i.e. cs_return)

Never hold a mutex or CriticalSection across an actual context switch. Code
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

## Key kernel structures
When possible kernel structures should contain the other structures they use,
not pointers to structures. This is to make memory allocation/cleanup simpler

## Critical sections
In kernel code mutexes should be used along with CriticalSection macros

Critical sections must not call into code paths that can context switch before
the mutex is released.

## Control flow
Do not use `goto` in new or changed code. When modifying code near an existing
`goto`, refactor that control flow to use structured returns or helper
functions instead.

# Fixing issues
## propose a theory and evidence
- Tell the user what you plan to do before making changes. If there is no
evidence, the user will be asked to provide evidence.
- Dont just leap in to fixing things without consulting the user first

# Changing code
## propose a change
Tell the user what you plan to do before making changes.

## Write tests
Add unit tests for any new code in kernel. Some architecture specific code
may not have unit tests as it is difficult to test..

# Kernel architecture
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
