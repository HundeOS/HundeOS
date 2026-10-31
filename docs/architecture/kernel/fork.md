# Fork And Copy-On-Write

## Scope

Fork is implemented with eager private-memory copies for Bash and other
userspace programs. Authenticated I/O inheritance is implemented separately in
libc and the owning services. Copy-on-write (COW) remains a deferred memory
optimization. The stages below record the implementation contract and the
remaining COW design, rather than an instruction to implement fork from scratch.

The kernel owns process identity, execution contexts, address spaces, scheduling,
and process-exit notification. Userspace services continue to own files, pipes,
sockets, streams, and their open-resource state. Fork must not move those
resources or descriptor tables into the kernel.

## Required Semantics

At a successful `fork` syscall boundary, the parent receives the child PID and
the child receives zero. The child resumes from the same saved userspace context
with an independent address space and process-local state. Failure returns an
error to the parent and leaves no visible child or inherited service references.

The child inherits credentials, current directory, environment, signal
dispositions, descriptor numbers, and references to the same open resources.
Pending signals, child-event queues, scheduler accounting, parked operations,
in-flight IPC requests, service registration, IRQ ownership, and device claims
are not copied. Signal-mask and disposition details remain part of the signal
milestone.

Descriptor flags such as `FD_CLOEXEC` are copied per descriptor. Open-resource
state such as file offsets and pipe endpoints is shared and reference counted by
the owning userspace service. Closing a descriptor in either process affects
only that process's reference until the final reference closes.

## Current Constraints

`Process` already contains an architecture-specific saved context, a process
page table, allocation metadata, parent identity, credentials, and child-event
storage. Both architectures provide `ProcessContext__copy` and
`Process__set_syscall_result`, which are sufficient to express the two fork
return values once a child context exists.

Current private user allocations own contiguous kernel-heap allocations and are
freed by allocation base. There is no shared ownership record for those
backings. Page-table code can map and unmap pages, but it has no
architecture-neutral permission-change, mapping-inspection, or COW-fault API.
User write faults currently terminate the process on both architectures.

The POSIX descriptor table and `IOClient` sessions live in copied private
userspace memory. Channel queue mappings, buffer mappings, cursors, and backend
handles are authenticated to the parent session. The child must not use those
copied session objects. It needs fresh Channels and handles granted to its own
authenticated PID.

### Process-local IPC allocations

The doorbell and queue-wait-control pages are kernel runtime state. Doorbells
are tracked as `USER_ALLOCATION_DOORBELL` with kernel-private backing references;
queue-wait-control pages remain private dynamic allocations. Eager fork excludes
typed doorbell allocations, the allocation identified by the parent's
`queue_wait_control`, and shared mappings. The child starts with neither
the backing allocations nor their mappings or Optional references; its first
IPC use allocates fresh state. Ordinary private allocations and their current
permissions still inherit normally.

Disposing an old image releases the doorbell owner's backing reference. Surviving
connections retain their own references until both endpoints detach, so exiting
or replacing the image cannot free a page still used by an in-flight peer ring.
See [queue lifetimes](queues.md#initial-queue-api).

Resetting only the Optional references is insufficient: a copied doorbell at
the fixed virtual address collides with the child's newly created doorbell.
A subsequent fork then sees overlapping allocation/mapping records and fails
permission copying, previously reported as `ENOMEM`. Kernel regressions cover
fresh IPC state across parent, child, and grandchild, with no accumulation of
orphaned wait-control pages. QEMU acceptance exercises external commands from
a background Bash subshell alongside a pipeline on both architectures.

### Address-space detachment on exit

Before freeing an exiting process's user allocations or page tables, the
scheduler calls `MMU__dereference_user_mappings(kernel, table)`. This is a
local-CPU operation: it compares the supplied root against the hardware's
active root and leaves an unrelated active address space unchanged.

On AArch64, detachment installs the empty guard table in `TTBR0_EL1`, leaving
the kernel mappings in `TTBR1_EL1`. On RISC-V, it installs the permanent
kernel-only root in `satp`; that root retains the shared supervisor mappings.
Both paths complete the required translation invalidation and barriers before
returning. Only then may exit cleanup release the old memory and tables.

This does not stop a remote CPU using an address space. Any future teardown
of a remotely running process must first establish that the other CPU has
stopped using it; the local detach operation is not a remote reclamation
protocol.

## Stage 1: Eager-Copy Fork

1. Define a typed `SYSCALL_PROCESS_FORK` result and a libc `fork()` wrapper.
   Add architecture-neutral helpers for cloning a saved context and setting its
   fork return value; keep register selection in `arch/<target>`.
2. Add a fork constructor that creates an unpublished child. Copy identity and
   inheritable process metadata, allocate a fresh page table, reproduce private
   mappings at the same virtual addresses, and copy their bytes. Recreate
   kernel-provided read-only mappings rather than copying their backing. Because
   the child page table is not visible to hardware during construction, install
   its mappings without per-mapping publication barriers, then publish the
   completed table once before making the child visible.
3. Do not inherit Channel queues, directional buffers, queue-wait pages, device
   mappings, IRQ state, parked syscalls, or in-flight service work. Fork must
   reject a currently unsupported allocation or process state before publishing
   the child.
4. Clone the saved userspace context after the memory image exists. Set the
   child's result to zero and the parent's result to the child PID. Reset child
   scheduler accounting, pending signals, child events, abort state, and service
   state.
5. Make construction transactional. Every allocation, mapping, process-table
   insertion, scheduler insertion, and service grant needs a reverse operation.
   The child becomes runnable only after memory and inherited I/O references are
   committed.
6. Test context results, byte-for-byte private-memory independence, inherited
   identity, excluded kernel/IPC state, allocation failure at each construction
   phase, rollback, parent exit, child exit, and both architecture builds.

The exact treatment of service processes and inherited MMIO is an open design
decision. The first Bash-focused implementation may reject those callers, but
must do so explicitly and atomically rather than silently dropping mappings.

## Stage 2: Authenticated I/O Inheritance

Fork uses a userspace inheritance transaction; the kernel does not copy or
interpret file handles.

1. Before invoking the kernel fork operation, libc groups descriptors by unique
   open description. `IOClient` asks each owning service to prepare a reference
   grant for the same underlying resource. Preparation does not make a resource
   usable by another process.
2. The kernel creates the child in a non-runnable state and returns its PID to
   the parent-side fork transaction. The parent commits each prepared grant to
   that PID. Any failure revokes all prepared or committed grants and destroys
   the unpublished child.
3. The child starts through a fork-return trampoline before `fork()` returns to
   application code. Libc obtains the child's kernel identity and refreshes its
   cached PID before attempting any inherited I/O claims. Queue attachment
   authenticates that PID against the kernel-created queue identity; using the
   copied parent PID would reject the child's fresh Channels. It then discards
   copied Channel/session state, creates fresh
   authenticated sessions, claims the committed grants, and replaces native
   handles in each copied open description. Duplicate descriptors continue to
   refer to one open description.
4. Once all child claims succeed, the trampoline permits the child-side `fork`
   call to return to application code. The parent-side wrapper returns only
   after commit succeeds. Services increment shared-resource references only at
   commit and release them on close, session loss, or process-exit cleanup.

The transaction protocol needs bounded grant identifiers, parent-session and
child-PID binding, single-use claims, explicit abort, and idempotent cleanup.
The final resource-owning service performs the grant; `hund-io-system` routes
requests but does not become the owner of backend file, pipe, or socket state.

The implemented coordinator is libc's `fork()` wrapper. It prepares descriptor
grants, asks the kernel for an unpublished child, commits grants to that child
PID, and resumes the child only after commit succeeds. The child-side return path
resets copied I/O sessions and claims the committed grants before application
code receives `fork() == 0`. Parent-side failures abort service grants and the
unpublished child. Parent session loss releases uncommitted grants; committed
grants are retained for the child and still need orphan cleanup if the child
dies before claiming them.

## Stage 3: Copy-On-Write

COW replaces only the eager copying of writable private pages. The fork syscall,
context behavior, I/O inheritance transaction, and rollback contract remain the
same.

1. Introduce explicit kernel ownership for fork-shareable private backing. The
   ownership unit must match what the allocator can safely release. Current
   multi-page allocations cannot be treated as independently freeable physical
   frames without first changing their allocation metadata.
2. Add architecture-neutral page-table operations to inspect a user mapping,
   install the same backing read-only in another address space, replace one page,
   and invalidate the affected translation. A software COW marker belongs in
   kernel mapping metadata; architecture PTE software bits may cache it but must
   not be the only ownership record.
3. During fork, map writable private pages read-only into parent and child and
   retain their backing. Preserve genuinely read-only and executable mappings.
   Managed shared memory remains shared under its existing ownership rules and
   is never converted to private COW memory.
4. Decode user write-permission faults in `arch/aarch64` and `arch/riscv64`, then
   pass a normalized fault description to the kernel. Only a tracked COW write
   fault is recoverable. All other access, translation, and execute faults keep
   the existing process-abort behavior.
5. Resolve a COW fault by allocating one page, copying the old contents, replacing
   the faulting process's mapping with a writable mapping, updating ownership,
   and invalidating the translation. Allocation failure terminates or reports
   the process consistently without corrupting the other sharers.
6. Update unmap, `sbrk`, exec overlay, and process exit to release shared backing
   exactly once. Test parent-first and child-first writes, multiple generations,
   read-only faults, partial-allocation rollback, exit without writes, and memory
   accounting.

Before implementing COW, decide whether backing ownership is extent-based or
page-based and document the allocator change that makes partial release valid.
Also verify the required local or cross-CPU TLB invalidation rules for a process
whose address space may migrate between CPUs.

## Delivery Order

1. Eager-copy kernel fork with exhaustive rollback tests.
2. Service grant protocol and child-owned Channel/session reconstruction.
3. POSIX `fork`, filesystem/stream descriptor inheritance, `waitpid`, and exec
   overlay integration sufficient for a foreground Bash command.
4. COW backing ownership and page-table primitives.
5. Recoverable COW write faults on AArch64 and RISC-V.
6. Pipe and socket descriptor inheritance, then stress tests for repeated
   pipelines, fork/exit churn, memory pressure, and
   parent/child write isolation.
