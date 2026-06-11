# Newlib Allocator Slice

This directory vendors the nano malloc implementation from newlib
`4.6.0.20260123`.

The allocator source is kept in `nano-mallocr.c` with its upstream copyright
notice intact. `malloc.c` is the local build wrapper that selects the allocator
entry points used by this freestanding userspace runtime.
