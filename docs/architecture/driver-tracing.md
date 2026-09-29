# Userspace driver tracing

Driver diagnostics use the `hund-verfolgen` userspace service. `DRIVER_DEBUG`
formats a bounded record in the calling process and sends it over the existing
Channel transport. The service uses the authenticated Channel origin as the
record PID; callers cannot select another process identity.

`hundeschule` starts `hund-verfolgen` before device drivers. The service accepts
and acknowledges records into a bounded 64 KiB memory buffer while the root
filesystem is being established. Once `/` is mounted, `hundeschule` sends the
storage-ready operation. The service opens `/var/log/driver.log`, drains all
currently available client records, and writes each drained burst as one I/O
operation. A full buffer drops complete records rather than blocking the boot
dependency chain, and the next successful flush records the drop count.

This path is separate from kernel tracing. Kernel boot, panic, and bootstrap
mount diagnostics must remain observable when userspace IPC or the root
filesystem is unavailable, so the kernel syslog syscall remains available to
the small set of early bootstrap services that require it. Ordinary drivers
must use `DRIVER_DEBUG` and must not write diagnostic traffic through that
syscall.

The Channel payload remains the control and record transport for formatted
driver messages. Driver messages are bounded below the existing one-page Channel
payload capacity.

## Channel latency tracing

`hund-verfolgen` also collects generic Channel lifecycle timestamps in the
memfs-backed `/tmp/io-trace.log`. Keeping the high-volume diagnostic output off
the measured disk filesystem prevents trace persistence from competing with the
filesystem requests under observation. Each participating process registers a fixed-size
shared trace ring with the service. The process has write access and
`hund-verfolgen` has read access, preserving the directional shared-buffer
permission model. Recording an event does not make a Channel call and therefore
does not add another synchronous hop to the path being measured.

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
