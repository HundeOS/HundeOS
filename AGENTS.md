## Source code organisation
### kernel
Non architecture specific kernel code. 
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
