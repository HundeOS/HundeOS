# Channel Tickets and Preferred Scheduling

## Tickets

Each Channel request carries its connection handle, a monotonically increasing
request generation, origin PID, and parent connection/generation (zero for a
root). The connection uses the existing control token field; the other four words
fit the existing 32-byte control payload. I/O payloads and bulk buffers are
unchanged. A backpressured submission does not advance the generation. Exhaustion
requires a new Channel. Direct replies echo the ticket; mismatches and reused
request generations are rejected.

The userspace Channel wrapper inherits the current request context. I/O-service
and block-service execution establish this context around downstream calls.
Fibres inherit context on creation and preserve it across suspension. Custom
asynchronous dispatchers must establish their request's context around downstream
calls with `ChannelRequestContext__swap`.

## Dependency Validation

Each write-owned queue page atomically publishes its latest outgoing message and
cursor before advancing the authoritative produced cursor. The kernel reads
this durable snapshot, not consumed ring slots. There are no new per-request
syscalls or writable cross-endpoint mappings. A busy snapshot or cursor mismatch
is ignored until a later scheduling observation.

For a forwarded request, the kernel checks that both connections are current,
accepted, and open; that the intermediary owns the appropriate side of both;
that the caller still publishes that exact outstanding submission; and that the
forwarded caller agrees with kernel-owned endpoint identity. A parent must be
live, not yet replied, identify this caller as its responder, and share its
origin. An ancestry must reach an authenticated root within the live link count,
rejecting cycles and stale generations.

These are scheduling hints, not authority to access data or proof of application
success. Acknowledgement, closure, and replacement withdraw old links. Fast
exchanges can finish between observations; ordinary unread-message readiness
remains the fallback. Missing a speculative hint does not lose a completion.
Kernel-originated Channels use `QUEUE_KERNEL_PID` for transport authentication and
store the originating process separately on the kernel-owned connection. Their
root tickets carry that real process PID, allowing a service reply chain to lead
back to the blocked caller without making the reserved kernel endpoint schedulable.

## CPU Registration and Admission

CPU schedulers are registered in a shared hashtable keyed by the architecture's
CPU ID. The scheduler does not renumber harts or affinities into a dense logical
range. Process-registry publication uses a separate compact slot because that
shared ABI remains a bounded array.

Every newly created process enters one shared FIFO admission queue without a CPU
assignment. The next CPU making a scheduling decision claims one admitted
process into its local process table and dispatches it ahead of ordinary local
selection. Once claimed, the process stays with that CPU through normal
preemption and waiting; only the priority-driven preferred path below may move
it to another CPU. This avoids treating process-count distribution as load
distribution and lets CPUs pull work when they are actually ready for it.

## Preferred Queue

One shared queue holds preferred processes, deduplicated by process. Every
scheduler refresh rebuilds its priority from authoritative kernel state. Ready
exec continuations precede unread-message receivers, followed by validated
request dependency weight, process class, and longest waiting time. Eligible
receivers and live request responders enter it. A reply enqueues the direct
caller and callers of live ancestor requests. Ancestors may be dispatched
despite unchanged shared observations, entering userspace `WFE` before their
replies arrive. Real kernel waits, sleep, stop, and exit are never overridden.

Enqueueing normally does not force preemption. At a scheduling decision the preferred queue
precedes ordinary class/longest-waiting selection and idle. A ready kernel exec
continuation is the exception: it ends retention of the current preferred process
at that scheduler entry so the blocked spawn can advance immediately. A started
CPU can take preferred work from another started CPU's process table under the
scheduler lock. Running processes are never stolen. An outgoing process cannot
migrate until its previous CPU reaches a subsequent scheduler entry, protecting
the context-switch tail. Exit removes queued references before process disposal.

Residency starts at actual dispatch, independently of enqueue time and CPU usage
sampling. Initial constants in `preferred.h` allow up to 10 ms of warm residency
despite an unchanged userspace wait, within the same 10 ms preferred-processing
budget. Normal shared waiting releases the CPU at the next scheduler decision;
warm waiting gets the bounded exception. Kernel blocking releases it and
termination overrides retention. Repeated timer checks never restart these windows.

If the preferred, admission, and ordinary runnable tiers are all empty, a CPU
selects one of its own unchanged userspace waiters instead of entering the idle
process. This speculative idle fallback never resumes a kernel-blocked, sleeping,
stopped, or exiting process and does not migrate work between CPUs. Candidates
are ordered by the number of validated request-chain stages depending on them,
then by active CPU usage in the completed 100 ms window after userspace wait time
has been removed, then by longest time since dispatch. The choice is recomputed
at the next normal scheduling decision; it does not grant renewable preferred
residency. Ordinary and preferred work always takes precedence.

The interrupt manager uses the same CPUs and wait machinery as other services.
Its realtime process class wins ordinary runnable selection, while a delegated
IRQ explicitly completes its parked queue wait and requests an immediate local
or remote reschedule. When it has drained its work and parks again, other work
uses that CPU normally.

These constants are initial policy choices, not measured optimal values or
latency guarantees. Speculative selection count is exposed through procfs so its
effect can be separated from architecture and emulator wait behavior. A successful
queue push now makes an authenticated data-available syscall after publishing its
shared cursor. When that publication satisfies a parked receiver's wait, the
kernel marks the receiver runnable and sends one reschedule IPI to its owning CPU; repeated notices
coalesce until it parks again. This is opportunistic chain scheduling, not
exclusive gang scheduling: overlapping clusters can interleave. Adaptive
residency, cluster-wide budgets, and kernel publication-to-dispatch tracing remain
open.
Userspace Channel lifecycle tracing now measures client publication, server
acceptance, reply publication, and client-visible completion, including parent
ticket identity for nested chains. See [userspace driver tracing](driver-tracing.md#channel-latency-tracing).
It does not yet split kernel eligibility from scheduler selection and dispatch.
Bounded graph rebuilding and ancestry traversal need profiling before claiming
a throughput improvement.

## Tests

Coverage includes sparse architecture CPU registration, shared process admission,
request generations, stale replies, nested and kernel-originated tickets, fibre
context isolation, parent authentication, cycles, reply and exit
cleanup, refreshed preferred ordering, exec-continuation preemption, migration
eligibility, and dispatch-relative residency. Existing tests retain real-blocking and
shared-wait precedence checks; processing budget tests replace the old
dequeue-ends-priority policy.
