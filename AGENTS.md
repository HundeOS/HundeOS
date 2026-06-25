## Source code organisation
### kernel
Non architecture specific core kernel code. 
Arch specific code in arch/ and linked in.

Interfaces to architecture are defined in kernel/arch

### arch
Architecture specific code. Currently aarch64. Assembly and architecture specific
integration code goes here, along with bootloaders

### user
Userspace code

### test
Unit tests. Running tests is done with make test. A python script is used to
run the tests and generate the runners.

# Instructions for making changes
## Check first
Plan and then tell the user what you plan to do before making changes.

## Unit tests
Add unit tests for any new code in kernel. Some architecture specific code
may not have unit tests as it is difficult to test..

Stubs are often linked in to tests to isolate code under test or provide
implementations that are architecture specific in the codebase.

No dependencies on architecture specific code should be made 
in core kernel tests.

## Troubleshooting
Ensure you have evidence for a theory regarding a problem before
fixing. Ensure the user is informed of the theory and agrees to
the evidence.

# Code standards
## Result types (Some, None, Success, Error, etc)
These should be used in the kernel code in most cases. Raw pointers or NULLs
should be avoided. 

The matching macros to match results types should be used

## Strings in kernel code
KStrings should be used. These are bounded with length stored independently to the buffer.

## Arrays, HashMaps, etc
There are macros to create typesafe arrays, HashMaps, etc. These should be used.

There are Macros for iterating over fixedarrays etc. These should be used.

## Pointers in kernel code
Pointers should be typedefed to a name following the convention
TypeNamePtr
- e.g. typedef Process * ProcessPtr

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
