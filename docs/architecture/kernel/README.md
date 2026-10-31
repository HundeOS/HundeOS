# Kernel Architecture

Core implementation lives in `kernel/`; target-specific entry, context, MMU,
interrupt-controller, timer, and bootstrap code lives in `arch/<target>/`.
`kernel/arch/` defines the architecture-facing interfaces.

| Area | Guide | Source entry points |
| --- | --- | --- |
| Process creation and memory copying | [Fork and COW](fork.md) | `kernel/process.c`, `arch/<target>/process.c` |
| Process image replacement | [Exec](exec.md) | `kernel/process.c`, `kernel/syscall/` |
| Protected IPC and event interfaces | [Channels and events](channels-and-event-queues.md) | `common/ipc/`, `kernel/ipc/` |
| IPC mappings, lifetime, and waits | [Queues](queues.md) | `kernel/ipc/queues.c`, `kernel/syscall/service_handler.c` |
| Request-chain scheduling | [Channel tickets and scheduling](dependency-scheduling.md) | `kernel/scheduler/` |
| Shared elapsed/realtime clock | [Clock page](clock-page.md) | `common/time/`, `kernel/` |
| POSIX process CPU counters | [Process CPU time](process-cpu-time.md) | `kernel/process.c`, `kernel/scheduler/` |

Channel guides include both sides of the IPC boundary because kernel
authentication and userspace establishment must agree. Protocol and bulk-buffer
policy lives in the [userspace services](../userspace/README.md).
The kernel publishes process-accounting records; userspace
[procfs](../userspace/process-registry-and-procfs.md) formats them as files.

Fork currently copies private memory eagerly. The COW stage is a deferred
design, not current write-fault handling. Executable bytes arrive through
userspace I/O; ELF validation and process-image construction remain privileged.
