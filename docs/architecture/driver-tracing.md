# Userspace driver tracing

`DRIVER_DEBUG` formats a bounded record in the calling process and submits it
through `SYSCALL_SYSLOG`. The kernel stamps the authenticated process ID and
name, then writes the record to the architecture kernel-trace sink. Driver
diagnostics do not open a Channel, wait for a userspace acknowledgement, or
depend on a mounted filesystem. `/var/log/driver.log` and its former
`hund-verfolgen` record transport have been retired.

The syscall returns after the architecture trace sink accepts the output. It
cannot wait on another process, although a synchronous architecture sink such
as a UART can still delay the caller while transmitting the record. Kernel
boot, panic, and userspace driver diagnostics therefore share the configured
kernel-trace destination.

## Kernel trace snapshots

Targets built with `KERNEL_MEMORY_TRACE` retain the newest 12 KiB written
through the architecture-neutral kernel trace path. The ring overwrites its
oldest bytes and uses a nonblocking guard: trace output may be lost under
contention, but a diagnostic write never waits for a reader or for free space.
Architecture trace sinks remain independent, so a target may tee the same
post-handoff trace to both memory and a UART.

The `SYSTEM_INFO_READ_KERNEL_TRACE` operation copies a non-destructive snapshot
into a validated writable mapping owned by a `hund` process. The userspace
`procfs` driver uses that operation to expose `/proc/ktrace` as a read-only
snapshot file. Opening the file captures the newest retained bytes; it neither
drains nor acknowledges the kernel ring. Bootstrap output emitted before the
architecture-neutral kernel trace path is active remains available only through
the platform bootstrap sink.

Fatal userspace process records remain in the kernel trace and are also copied
to the console sink before the process is terminated. This keeps the exception
cause, program counter, and fault address visible when the separate kernel-trace
UART is not connected. Ordinary kernel and driver trace records are not copied
to the console.

## Channel latency tracing

`hund-verfolgen` collects generic Channel lifecycle timestamps in the
memfs-backed `/tmp/io-trace.log`. Keeping the high-volume diagnostic output off
the measured disk filesystem prevents trace persistence from competing with the
filesystem requests under observation. Each participating process registers a fixed-size
shared trace ring with the service. The process has write access and
`hund-verfolgen` has read access, preserving the directional shared-buffer
permission model. Participating processes explicitly prepare tracing before
entering the work being measured. Recording an event only writes an already
registered ring; if no ring is available, the event is dropped without making a
Channel call. It therefore does not add another synchronous hop or tracing-service
dependency to the path being measured.

Latency tracing is controlled by the kernel `SystemSettings` returned from
`SYSCALL_SYSTEM_INFO`. Only processes running as the built-in `hund` user may
read these settings. The setting is currently disabled, so `hundeschule` does
not start `hund-verfolgen` and `IOTrace__prepare` succeeds without registering a
ring. The collector and ring implementation remain available for a later
enabled configuration.

The Channel library records four points for each request ticket:

- client request publication;
- server request acceptance;
- server reply publication; and
- client-visible completion.

A completed `IO-LATENCY` line identifies the client, server, request generation,
origin, and parent ticket. `queue_usec` measures publication to server acceptance,
`service_usec` measures server acceptance to reply publication, `return_usec`
measures reply publication to client-visible completion, and `total_usec` covers
the complete request. The record also retains the absolute `published_at_usec`,
`received_at_usec`, `replied_at_usec`, and `completed_at_usec` timestamps needed
to correlate work performed between nested requests. Each request and return
interval is split further into:

- `*_observe_usec`, before the scheduler validates the ticket and makes its
  receiver eligible;
- `*_runnable_usec`, while an eligible receiver waits to be selected;
- `*_dispatch_usec`, between selection and the actual context switch; and
- `*_userspace_usec`, between dispatch and the Channel operation observing the
  message or completion.

Parent tickets allow nested service calls to be assembled into a dependency
chain without interpreting protocol payloads.

Each server acceptance also produces an `IO-HOP` record. The Channel publisher
stores its monotonic timestamp in the shared payload immediately before the
successful queue push, and the receiver captures another timestamp when it
consumes that payload. `queue_usec` is the receiver timestamp minus that exact
publication timestamp. The record names the immediate `client` and `server`, as
well as the origin and parent-ticket lineage, so a request chain can be grouped
by transport hop without attributing downstream service work to the queue.

The `fs-latency` command brackets a filesystem workload with trace-file offsets
and monotonic timestamps, synchronously drains the trace sources, and reports
both transport and nested-service distributions. An `io_hop` line measures the
publish-to-consume distribution for an immediate client/server pair. An
`io_chain` line correlates child tickets with their parent and separates:

- `pickup_to_emit`: parent acceptance, or completion of the preceding sibling
  request, to publication of the next child request;
- `downstream`: publication to completion of each child request; and
- `resume_to_reply`: completion of the final child request to publication of
  the parent reply.

The sibling ordering prevents serial child requests from being counted as
overlapping parent work. Records with missing or inconsistent chronology are
excluded rather than reported as zero latency. Percentile values are upper
bounds of the diagnostic histogram buckets, rather than interpolated values
from retained raw samples.

The clocks used by supported targets are comparable between processes. Rings
are deliberately lossy so diagnostics cannot block I/O. Ring overwrite and
output-buffer loss produce `IO-LATENCY-DROPPED` records; incomplete correlation
evictions are silently discarded instead of writing another I/O record. The latter
avoids a diagnostic feedback loop when only the server half of the trace exists.
`hund-verfolgen`, `hunde-puffer`, and `hundekanal-warter` disable this
instrumentation because registering their own trace buffers would create a
bootstrap dependency cycle among the tracing transport services.

Before a traced process exits, it requests a synchronous drain. The trace
service snapshots and drains every registered source before acknowledging that
request because a completed Channel span contains client events and server
events from separate rings. This preserves the final correlations of a
short-lived command without waiting for unrelated events published after the
snapshot.

A registered source is retained for a short grace interval after it disappears
from one process-registry snapshot. This prevents a transient incomplete
registry observation from discarding a live process's trace ring.

Scheduler timing records use the same ticket and add the kernel's first validated
observation, selection, and actual dispatch for both the request receiver and
returning client. The shared dispatch slot is deliberately lossy like the trace
rings; a later dispatch can replace a record that userspace has not consumed.
`request_scheduler_observed` and `return_scheduler_observed` distinguish a real
zero-duration phase from unavailable scheduler timing. The four Channel points
remain reportable when either scheduler record is lost.
