# Process Registry And Procfs

The kernel owns process accounting. Userspace `procfs` renders it through the
normal filesystem Channel, mounted at `/proc` by `hund-dateisystem` before boot
readiness is published. This startup path is common to all targets.

`ps` reads the same registry directly through `process_list`; it does not parse
proc files or require the filesystem service to collect CPU samples. The legacy
process-list syscall remains available and uses the same record conversion.
Hardware information stays at `/boot/sysinfo`.

## Published Files

- `/proc` lists published PIDs.
- `/proc/<pid>/status`: PID, UID/GID, name, scheduler state, wait reason,
  process class, assigned CPU, and sample timestamp.
- `/proc/<pid>/cpu`: cumulative scheduled microseconds and CPU percentages
  over the completed 1-second and 100-millisecond accounting windows.
- `/proc/<pid>/memory`: private RAM, shared RAM, and device mappings in bytes.

Directories are read-only `0555`; files are read-only `0444`. Entries carry
the process's numeric credentials. There is no write or execute interface.
Open files retain a snapshot; exit removes subsequent lookups, not the bytes
already held by an open file. Existing filesystem handle/session authentication
applies. Handle enumeration is deferred.

## Scheduler Semantics

`cpu` is the zero-based CPU scheduler assignment, including while waiting. It
does not mean the process is currently executing. `PROC_STATE_RUNNING` means
running at the sample; `READY` and `INTERRUPTED` are runnable scheduler states;
`WAITING` and `SLEEPING` identify parked processes, with the specific park reason
reported separately. Context-switch and stopped states are preserved.

CPU percentages use the existing `CPUUsage` counters: 100% is one fully occupied
core. Totals include kernel execution while the process is scheduled and exclude
parked time. Percentages use the full window denominator, including for new
processes; they are not percentages of all machine cores combined.

Each CPU publishes approximately every 100ms on its timer path. Scheduling and
exit also refresh that CPU's records. `sampled_at_usec` identifies the sample;
these are snapshots, not a promise of instantaneous state when a reader runs.

## Memory Semantics

Core page-table wrappers account mappings independently of the architecture
implementation. RAM backing is identified by its kernel virtual range, which is
also used when exporting subranges. This supports noncontiguous physical pages.

- `private_bytes`: mapped process RAM not also exported through a shared mapping.
- `shared_bytes`: explicitly shared mappings (including registries, queues and
  payloads), plus exported portions of otherwise private allocations.
- `device_bytes`: mapped MMIO, excluded from both RAM categories.

Only overlapping exported pages move from private to shared. Several peers
sharing one page do not multiply the owner's shared count. Partial unmaps and
remaps preserve the remaining ranges; address-space disposal removes its records.
Sizes are page-rounded mapped bytes, not allocator-requested payload sizes.
Page tables and other kernel-only bookkeeping are not included.

Shared bytes are charged to **each mapped address space**. Adding process totals
does not give physical RAM consumption. Memory-accounting metadata allocation
failure is reported as `accounting=unavailable`, never an invented zero count.
Accounting does not change buffer ownership or add reference-counted lifetimes.

## Shared ABI

`common/process/process_registry.h` defines the versioned mapping at
`0x78000000`. Its size is derived from the layout and rounded to pages, rather
than constrained to one page. Capacity matches all 16 scheduler slots, each with
2048 processes plus its idle process. Per-CPU counts avoid scanning empty slots.
Every process maps the same kernel-owned backing read-only and non-executable.

Names use `BString`; state, wait reason, class, credentials and usage are typed
fields. `AtomicRecordArray` provides individually consistent records using atomic
bytes and per-record generations. Kernel writers are serialized by scheduler
locking; userspace readers never write the mapping. Readers retry busy records a
bounded number of times and report retry/error rather than consume torn data.

Listings are live and paged, not a transaction across the whole process table.
Concurrent creation/exit can change directory ordering between requests. A single
record is coherent, but separate files can reflect different sample timestamps.
The registry is observational, not an authorization or process-exit event stream.
