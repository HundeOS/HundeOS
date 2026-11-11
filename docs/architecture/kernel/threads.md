# Userspace Threads

## Status and scope

This is the design for adding multiple schedulable execution contexts to one
HundeOS process. The initial thread's context, stack, scheduling state, wait
records, and signal-handler state now live in an embedded `Thread` value;
address-space, image, IPC, service, device, signal, accounting, scheduling
policy, and lifecycle fields have distinct process-owned groups. CPU run queues,
normal ready selection, running-context lookup, context-switch records, yield
operations, and exception context saving now identify the embedded thread,
while process tables remain for PID lookup and process snapshots. Syscall
dispatch passes the calling thread alongside its owning process to handlers;
blocking queue, sleep, signal-suspend, service, child, and exec continuations
retain that thread. Admission, preferred and speculative scheduling policy
remain process-based. Each process now tracks its threads in a small
process-owned collection; `fork` and overlay `exec` reject a process with a
sibling before changing its image. Process
exit marks the process as terminating, prevents further dispatch, requests
remote rescheduling, and waits for each CPU to leave the process address space
before reclaiming its image. The raw thread-create syscall now publishes a
sibling on the calling process's CPU, with a kernel-allocated stack, a distinct
thread ID, and an architecture-specific starting context. The runtime
trampoline calls the start routine and then the non-returning current-thread
exit syscall. A join syscall parks the caller or reaps an already exited
thread, returning the exit value through a validated userspace mapping. The
initial thread can exit while siblings continue; the final thread ends the
process. Joining a created thread releases its stack, saved signal state,
parked operations, and context. The embedded initial thread's execution
resources remain until process teardown while process-level helpers still
refer to its context. Created threads remain on one CPU until process accounting
and other shared state support concurrent execution on separate CPUs. The remote-CPU
handoff still needs full-system validation with siblings on both architectures.
Process-private atomic compare-and-park/wake, a userspace mutex, static executable
TLS, and per-thread Newlib state are implemented. Cross-CPU execution and shared
runtime thread safety remain outstanding. These APIs do not make existing
service or driver code safe to run concurrently.

The immediate motivation is a host runtime for NetBSD rump and, later, separate
fibre schedulers in the I/O and driver hosts. Thread support alone does not
establish a working rump port: that also requires its clock, memory, locking,
randomness, and packet-I/O host interfaces. Existing host fibres remain
cooperative and single-threaded until their native and Rust dependencies have
been audited for cross-thread use.

## Current coupling

`Process` currently embeds its initial thread, tracks process-owned thread
membership, and owns the address space, process identity, service and device
registrations, and aggregate CPU accounting.
`CPUScheduler` stores `ThreadPtr` runnable entries alongside process records
and identifies the running thread by its state.
Deferred syscall `KernelContext` retains the parked thread. Keyed unpark,
queue notifications and timeouts, exec-load polling, service-event and
child-event completion select the thread holding the matching parked operation.
The userspace queue-wait control and doorbell remain process-scoped; each
kernel queue waiter tracks its own observed generation. Preferred and
speculative scheduling and process-wide stop still assume the embedded thread.
Exit detaches all process-owned threads and waits for remote address-space
handoff before freeing the process image.

Userspace has one native fibre scheduler per process. Its queue-wait adapter and
transaction gates rely on one OS thread. Newlib uses dynamic per-thread
reentrancy state, and its allocator is serialized with `UserMutex`. Native I/O
bookkeeping is not yet thread-safe. Loaded Wuffs currently reject compiler TLS
metadata. The runtime cannot be assumed fully thread-safe after a kernel syscall
is added.

## Ownership model

- A process is the identity and resource domain. It owns PID, parent/children,
  credentials, capabilities, signal dispositions and process-directed pending
  signals, address space, memory mappings, executable image, service and device
  claims, Channel and buffer attachments, process-wide doorbell, and aggregate
  accounting. Existing Channel authentication and resource grants continue to
  use PID, not thread ID.
- A thread is the schedulable execution context within exactly one process. It
  owns a distinct thread ID, architecture context, userspace stack lifetime,
  run/park state, CPU residency and accounting contribution, active syscall and
  continuation, queue-wait registration, signal mask, handler frames, and
  thread-directed pending signals, thread-pointer state, and a bounded
  diagnostic name. The initial program thread uses this same representation;
  it is not a special second execution path.
- A CPU schedules a thread and activates its owning process's page table. Two
  threads of one process may run on different CPUs. A process image or mapping
  may be reclaimed only after every thread has stopped executing in it and all
  CPUs using it have detached or invalidated that address space.
- Process state transitions that affect all threads, including stop, terminate,
  exec, and device teardown, are coordinated once per process. A thread exiting
  alone must not close process-owned Channels, release buffer attachments, or
  notify the parent of a process exit. The final thread performs process-exit
  cleanup and produces one child event.

Thread IDs must be distinguishable from PIDs in typed APIs and generation-safe
when referenced by a joiner or kernel wait record. Creation reserves a thread
slot, stack mapping, and initial context before publishing a runnable thread;
failure rolls all of them back. A joinable exited thread retains only the state
needed for one join until reaped. The process has a bounded thread count.

Every thread has its own readable name that can be changed without changing
the process name, PID, scheduling policy, or Channel identity. The initial name
may derive from the process name; created threads may be renamed by the owning
process. Name changes copy and validate a complete bounded string at the
syscall boundary. Thread IDs and names are diagnostic metadata, not authority.

## Thread start and normal termination

The main thread enters the existing userspace `_start` path. Returning from
`main` calls process-wide `exit`, as it does now. New threads enter a userspace
runtime trampoline with their start function and argument. If the start
function returns, the trampoline passes its result to `pthread_exit`; it does
not return into a kernel address or call process-wide `exit`.

`pthread_exit` performs userspace per-thread cleanup and then makes a
non-returning thread-exit syscall. The kernel records the join result, removes
the caller from scheduling, wakes a joiner, and reclaims its execution resources
only after switching off its stack. Returning from a created thread and calling
`pthread_exit` explicitly have the same result. Calling process-wide `exit`
from any thread still terminates the whole process. If the main thread calls
`pthread_exit`, siblings continue; the final thread exit ends the process.

## Scheduling and blocking

Scheduler run queues, current-context selection, preemption, and deferred
syscall completion must refer to a thread. Any operation that currently finds a
`ProcessPtr` by PID for a parked continuation must instead retain the exact
thread, while authorization continues to derive its owning process. A thread
may park without parking its siblings. Completion wakes the thread that issued
the operation, not an arbitrary thread with the same PID. Process CPU time is
the aggregate of its threads, preserving the current `times()` contract.

The process doorbell remains a notification generation for shared Channel
state. Each thread has its own observed generation and wait registration; a
publication must not be lost when several threads are asleep. The first
implementation can wake all eligible process waiters and let each recheck its
own condition. It must not assume the existing single-producer/single-consumer
Queue implementation is safe for concurrent callers: a userspace Channel or
transaction has one serialized owner unless its library is explicitly made
thread-safe. Kernel wake selection and any later reduction of unnecessary wakes
must preserve the generation-checked poll-to-park race closure.

### Process-Private Atomic Waits

`SYSCALL_THREAD_WAIT` (`0x9E`) takes a naturally aligned 32-bit word address,
an expected 32-bit value, and a relative timeout in microseconds. The word must
be in writable private user RAM: executable data, a stack, heap, or dynamic
allocation. MMIO, managed shared buffers, and doorbells are not accepted.
`SYSCALL_THREAD_WAKE` (`0x9F`) takes the same address and a maximum waiter count;
its nonnegative result is the number of matching registrations made ready.
Zero wakes none; `UINT64_MAX` permits waking every matching waiter.

The key is the owning process plus user virtual address, not a physical address
or a global handle. The scheduler lock serializes the acquire load and waiter
registration against wake selection. A store followed by a wake cannot slip
between the comparison and registration. Registration only changes scheduling
state; actual context switching happens after the lock is released. Waking
marks waiters ready for normal scheduler dispatch rather than switching while
holding a lock. Created threads still share one CPU; this does not enable
parallel access to process mappings or the rest of the runtime.

`thread_wait` and `thread_wake` in `user/lib/sys/thread_wait.h` expose this ABI.
Callers use C atomic operations on `ThreadWaitWord`, publish changes before
waking, and recheck their condition after returning. A zero timeout polls;
`THREAD_WAIT_INDEFINITE` waits without a deadline. Finite waits round up to
scheduler ticks with an additional boundary tick, matching the sleep path, and
do not promise an exact dispatch time.

Wait results are `THREAD_WAIT_WOKEN` (0), `THREAD_WAIT_CHANGED` (1),
`THREAD_WAIT_TIMED_OUT` (2), or `THREAD_WAIT_INTERRUPTED` (-4096). Invalid words
or arguments return `THREAD_WAIT_INVALID` (-1); registration failures return
`THREAD_WAIT_FAILED` (-3). A caught signal interrupts an atomic wait even when
its handler uses `SA_RESTART`. The pre-park signal check is conservative: an
unmasked pending signal may return interruption even when its disposition is
ignored. Normal signal delivery remains unchanged. The caller must still
recheck its condition.

Registrations use the thread's existing parked-operation ownership, with no
separate waiter registry. Wake and timeout select a registration only once.
Stopped threads may record a wake or timeout without becoming runnable until
continued. Signal interruption removes the registration; exit makes a thread
ineligible for wakes, and join/process teardown releases retained operations.
The kernel retains the address as a key, not a pointer into the allocation.
The application must keep the word mapped and its object alive until all waits
have finished; freeing or reusing its storage while waiters remain is invalid
application synchronization. Interprocess waits and automatic unmap cancellation
are not implemented.

### Userspace Mutex

`UserMutex` in `user/lib/sys/user_mutex.h` supplies construction, try-lock,
blocking lock, and unlock. The implementation owns one atomic word with
unlocked, locked, and contended states. Uncontended acquisition/release stays
entirely in userspace; contention uses the atomic-wait syscall and unlock wakes
one successor. Acquire/release ordering protects the caller's critical section.
Blocking lock retries changed values and signal interruption.

This is a process-private, nonrecursive mutex, not a robust or priority-inheriting
lock. Callers must release only locks they own and keep the mutex alive while
it is used; ownership and FIFO fairness are not enforced. It is not suitable for
signal-handler reentry. Condition variables, libc locking, Rust wrappers, and
parallel host schedulers remain separate follow-on work.

The kernel tests cover address validation, process isolation, wake-before-wait,
bounded wake counts, timeout/wake ordering, interruption, stopped waiters, and
exit/join cleanup. Native userspace tests cover ABI argument translation and
mutex fast/contended paths. `/sbin/thread_probe sync`, included in the existing
AArch64/RISC-V thread smoke test, exercises four contending threads that sleep
inside the critical section, finite waits, `SA_RESTART` interruption, and exit
from a signal handler while waiting.

## Libc allocation

The HundeOS-owned `user/lib/newlib/malloc.c` adapter holds one process-private
`UserMutex` across each complete `malloc`, `free`, `calloc`, `realloc`,
`memalign`, and `malloc_usable_size` operation. `allocator_core.c` compiles the
unchanged upstream allocator under internal names. Its nested allocations,
metadata reads, copies, and frees do not reenter the public wrappers or acquire
the mutex recursively. Heap growth from the allocator occurs under that same
lock. Direct `_sbrk` calls are not a concurrent allocator interface.

`process_fork` acquires this mutex before the kernel snapshots the address
space and releases it after the syscall in the parent, the child, and on
failure. The child therefore inherits consistent allocator metadata and a lock
owned by its surviving caller, which it immediately releases. This does not
enable multithreaded fork: the existing kernel rejection remains in place.

Allocation is not async-signal-safe; signal handlers must not allocate, free,
or fork while interrupting allocator code. Lock failures trap rather than
allowing an operation to continue without protection. As with `UserMutex`,
terminating a thread while it owns the allocator lock is not robust recovery.

Hosted adapter tests verify that all six entry points call the core under one
lock; syscall-wrapper tests verify the fork lock boundary and release paths.
`/sbin/thread_probe allocation` exercises contended allocation, initialized and
preserved payloads, aligned blocks, reuse, and allocation after a single-threaded
fork. The QEMU thread smoke runs this probe on AArch64 and RISC-V. This is not a
claim of cross-CPU userspace-thread execution, which remains disabled.

## Signals, fork, exec, and exit

Signal dispositions, process-directed pending signals, and child events remain
process-owned. Masks, active handler frames, and directed pending signals are
thread-owned. The kernel selects an eligible unmasked thread for a
process-directed signal and routes a thread-directed signal only to its target.
Default stop and terminate actions affect the whole process. Signal delivery
must interrupt only the selected thread's parked operation and restore that
thread's context on `sigreturn`.

The target multithreaded `fork` behavior is a new child process containing only
a replica of the calling thread and an independent copy of the parent's entire
address space. The other threads' userspace memory, including stack and lock
bytes, is copied, but their execution contexts and pending kernel waits are
not. The target `execve` behavior replaces the process image after
stopping sibling threads, preserving PID and leaving one thread at the new
entry point. A failed exec leaves the old image and its threads usable. Until
those transactions are implemented, calls from a process with more than one
thread must fail before any visible mutation; silently cloning sibling state or
freeing their active mappings is unsafe. Existing single-threaded fork and exec
behavior must remain unchanged.

An explicit thread-exit operation ends only its caller. The existing process
exit path ends the entire process, quiesces all threads, and cleans process
resources once. Fatal process-level faults and uncatchable signals retain their
current whole-process behavior. Remote CPU quiescence is a prerequisite to
freeing a shared page table on exit or exec, not a later cleanup optimization.

## Userspace runtime boundary

Static executables support initialized and zero-initialized C `_Thread_local`
storage, with template alignment up to 64 KiB and total image size up to 16 MiB.
The ELF loader validates `PT_TLS` and retains its user-mapped initialization
template in `ProcessImage`. The thread-local-image syscall copies its bounded
descriptor to the caller; the kernel does not allocate libc state.

The C CRT initializes the initial thread before entering application code.
`thread_create` allocates an independent TLS image and zeroed Newlib reentrancy
record, then supplies the thread pointer to the creation syscall. The kernel
installs it before making the thread runnable. AArch64 saves and restores
`TPIDR_EL0`; RISC-V uses the existing saved `tp` register. Each new thread copies
the original ELF template, not its creator's modified variables. Thread exit
clears the pointer and releases that thread's runtime allocation before the
non-returning exit syscall. Fork preserves the cloned pointer and runtime
mapping; exec installs a fresh image and initializes a new runtime.

The layout follows the static TLS conventions in the
[AArch64 System V ABI](https://github.com/ARM-software/abi-aa/blob/main/sysvabi64/sysvabi64.rst)
and [RISC-V ELF psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/).
The initial control block reserves the dynamic-vector slot, but dynamic TLS
lookup and Wuff TLS relocations are not implemented. Wuffs continue to reject
compiler TLS metadata; this static slice does not enable TLS in loaded modules.
The shared libc uses `__getreent` for independent `errno` and the selected
Newlib conversion routines, including programs using the Wuff interpreter.

The first kernel thread slice does not enable parallel host work. A usable
parallel runtime still needs thread-safe descriptor/Channel bookkeeping, plus
ELF TLS handling for dynamically loaded images. TLS support is a
requirement before claiming rump or parallel hosts work, not an optional
optimization. The existing native fibre scheduler,
`QueueWaitAdapter`, and cooperative `QueueWaitGate` cannot be shared across OS
threads as they stand. Each host thread should own its own fibre scheduler;
cross-thread work transfer must use an explicit synchronized boundary, and live
fibres or non-`Send` components must not migrate implicitly.

The rump port can then implement its upstream host-thread, lock, clock, and I/O
interfaces against these facilities without changing vendored NetBSD source.
It must prove that blocking host calls release rump's scheduling context before
they park.

## Validation and sequencing

1. Refactor kernel ownership so one-thread processes retain existing behavior.
   Test process identity, scheduling, queue waits, signals, exit, fork, and exec
   before publishing any second thread.
2. Add transactional creation, thread-local park/continuation ownership,
   exit/join, and bounded thread lifetime. Test two threads on one and separate
   CPUs, including one blocked while the other progresses.
3. Add the compare-and-park wake primitive and test wake-before-park, timeout,
   signal interruption, repeated joins, and concurrent exit. Test teardown
   while a sibling runs on another CPU.
4. Add thread names and per-thread runtime state, then implement the TLS model
   required by the selected libc and loaded images. Test name isolation,
   per-thread `errno`, thread-pointer preservation across preemption, TLS
   initialization, and thread-exit cleanup on both architectures.
5. Make the remaining userspace runtime thread-safe, then run host-native and
   AArch64/RISC-V QEMU tests. Parallel host fibre schedulers and the rump port
   are separate follow-on slices.

Process-local IDs, kernel-owned stacks, and process-private address-keyed waits
are now implemented contracts. Remaining decisions include bounded thread names,
process-versus-thread scheduling fairness, cross-CPU mapping/runtime safety,
multithreaded signal/fork/exec policy, and loaded-image TLS. Resolve these against
actual libc/rump callers rather than adding unused generality.
