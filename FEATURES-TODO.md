# Features TODO

Discussed features that are not yet implemented. Organize entries as:
System (level 2) -> Feature (level 3) -> Supporting features or architecture
(level 4). Unchecked items are outstanding; record unresolved design choices
explicitly. Entries are not authorization to start implementation.

## Process Management

### Asymmetric Multiprocessing With Dedicated Realtime Cores

Status: deferred. Realtime services should be granted exclusive use of an
available realtime core. General-purpose cores retain normal scheduling and
interrupt handling; an assigned realtime core runs without preemption or
maskable interrupt delivery. The existing realtime-service process class does
not by itself implement this execution model.

#### Core Allocation and Admission

- [ ] Track general-purpose cores, available realtime cores, and realtime-core
  ownership. Decide whether the partition is configured at boot or can change
  at runtime, while preserving capacity for kernel and housekeeping services.
- [ ] Provide an authorized request/grant operation for a realtime service to
  acquire an available core. Return an explicit unavailable result when no core
  can be granted; do not silently substitute ordinary scheduled execution.
- [ ] Pin the admitted service to its granted core and exclude that core from
  general-purpose scheduling, load balancing, and unrelated kernel work.
- [ ] Define transitions into and out of realtime ownership, including setup,
  explicit release, process exit, and restoring a core before reuse.

#### Non-Preemptive Execution and Interrupt Isolation

- [ ] Disable scheduler ticks and involuntary preemption on assigned realtime
  cores. Route device interrupts and ordinary interprocessor interrupts to
  general-purpose cores; exclude realtime cores from routine reschedule requests.
- [ ] Implement interrupt masking and core-role transitions through the kernel
  architecture interfaces, for both AArch64 and the planned RISC-V port.
- [ ] Specify permitted syscalls, voluntary yielding, blocking, and fault
  handling. Disabling interrupts does not remove synchronous faults or explicit
  kernel entry; these must have defined behavior without silently enabling
  preemption. Document any unavoidable platform/firmware exceptions.

#### IPC, Memory, and Shared Kernel State

- [ ] Retain the existing Channels and queues for communication with services
  on general-purpose cores. Define polling or event-based waiting that does not
  depend on delivery of a masked interrupt, including full-queue backpressure.
- [ ] Audit IPC signaling so routine notifications do not introduce interrupt
  handling on realtime cores. An event wakeup must not imply a context switch.
- [ ] Define memory preparation before admission, avoiding demand allocation
  or other unbounded work during realtime execution. Resolve how shared-memory
  unmapping and translation invalidation coordinate with a non-interruptible
  owner before any pages are reclaimed.
- [ ] Audit shared locks and service dependencies for unbounded waiting or
  priority inversion. Core dedication alone does not guarantee bounded latency;
  shared caches, memory bandwidth, and device activity also need consideration.

#### Lifecycle, Recovery, and Observability

- [ ] Define cooperative stop/release and handling of a crashed service.
  Recovery from an unresponsive service is an open design question: ordinary
  preemption or an interrupt-based kill cannot be assumed under this model.
  Decide whether forced recovery requires a core/platform reset or an explicit
  exception to the isolation contract.
- [ ] Expose core roles, ownership, and realtime process state through the
  process registry and `ps`, with CPU accounting that does not require periodic
  interrupts on the realtime core.
- [ ] Test exclusive grants, exhaustion, release/reuse, IRQ routing, continued
  general-purpose progress, IPC backpressure, and exit/fault cleanup. Measure
  realtime jitter under load; define timing guarantees separately from simply
  disabling preemption and interrupts.

### Fork Support

Status: deferred. A child must inherit references to the parent's open resources,
not reopen their paths. General fork and userspace descriptor inheritance are
not implemented. Current console startup passes an endpoint marker and lazily
opens a new stream handle; this is not sufficient for fork semantics.

#### Process Creation

- [ ] Add fork support for creating a child from the calling process's execution
  state and address space. The memory-copying strategy remains undecided.
- [ ] Coordinate child creation with I/O inheritance so the child cannot use
  inherited descriptors before its access is established. Failure handling and
  rollback need to be designed.

#### I/O Subsystem: Shared File Descriptors

- [ ] Give the child its own descriptor table with the parent's fd numbers,
  including stdin (`0`), stdout (`1`), and stderr (`2`).
- [ ] Have `hund-io-system` authorize child-PID-owned references to the same open
  resources. Copying a parent-owned handle number alone must not grant access.
- [ ] Share open-resource state where appropriate: file offsets for the same
  open file, the same socket connection, and the same stream destination.
- [ ] Reference-count shared open resources while keeping descriptor lifetimes
  independent. Closing one process's descriptor must not invalidate another
  process's reference; release the resource when its final reference is gone.
- [ ] Release each process's references on exit, including crashes and missed
  closes, using reliable process-exit notification and service cleanup.

#### IPC Sessions

- [ ] Establish the child's own authenticated Channel session to
  `hund-io-system`; do not duplicate the parent's Channel queues, cursors, or
  in-flight request state.
- [ ] Define how inherited descriptors are associated with that new session
  while retaining their shared underlying resources. The inheritance protocol
  and coordination with the kernel remain to be designed.

Architecture context: [Userspace I/O system](docs/io-system.md) and
[Channels and event queues](docs/channels-and-event-queues.md).

## I/O and Shared Memory

### Bounded Idle Waiting

#### Userspace Polling and Kernel Parking

- [x] Run ready fibres, briefly poll queue state in userspace, then park the process
  with authenticated queue observations. Return to userspace after N ticks even
  without a state change, and always recheck predicates.
- [x] Replace comparison-mode busy polling with userspace `WFE`/`SEV` waits.
  Complete publication before signalling; signal after device IRQ dispatch and
  set a local event on context restoration. Timer preemption remains enabled,
  and normal wait helpers do not invoke kernel parking.
- Retained kernel parking syscall: 10-tick default timeout, with predicate checks
  on each owning CPU tick. The former 20-microsecond userspace polling phase has
  been removed. See [queue waits](docs/queues.md#bounded-waits).
- User comparison on the SDHCI trace target reports substantially better
  responsiveness with kernel parking disabled and tracing unchanged. This
  implicates the parking path, but does not isolate its expensive stage.
- [ ] Measure queue publication to wake detection and actual userspace resumption
  in a controlled diagnostic run, separating activity wakes from timeouts.
  Include syscall entry-to-park and selection-to-return overhead. Current
  counters omit these intervals and publication timestamps; average parked
  duration is not message-delivery latency. Normal builds use event waits;
  reintroducing kernel parking requires measurements and approval.
- [x] Add an isolated timeout probe that parks only itself and compares complete
  call duration with fresh kernel counters. On the SDHCI trace target, 1- and
  10-tick waits left about 18-19 ms per call outside parked/runnable accounting.
  See [measurements and limitations](docs/queues.md#sdhci-trace-measurement).
- [ ] Attribute that remaining entry/completion overhead, including per-park
  allocation/free and tracing costs, before changing the wait implementation.
- [x] Expose aggregated wake reasons, parked durations, and wake-to-selection
  delays through [procfs queue-wait diagnostics](docs/process-registry-and-procfs.md#queue-wait-diagnostics).
- [x] Expose userspace UART bytes/IRQs, empty reads, backend read traffic,
  warden forwards, and unchanged event-wait returns through
  [IPC activity diagnostics](docs/process-registry-and-procfs.md#ipc-activity-diagnostics).
- [ ] Measure idle-console deltas versus typing after the stable-publication
  change, separating real queue/kernel wakes from spurious `WFE` returns.

#### Proposed Shared Scheduling Hint

- [x] Introduce a per-process shared userspace scheduling record: idle queue waits
  publish observations; the kernel consults them during normal preemption and
  process selection instead of requiring a per-wait syscall. Existing fibre
  dispatchers run ready work before entering this path. See the implemented
  queue-observation foundation below; dependency-token propagation is separate.
- Proposed constraints: aggregate readiness across the process, retain timer
  preemption, and avoid per-wait allocation and deferred-syscall completion.
- [x] Keep a publication armed across spurious or unrelated `WFE` returns and
  add a kernel-written wake generation for authenticated notifications. Ordinary
  waits have no periodic retry; validated preferred ancestors use the bounded
  warm-residency policy. Treat userspace fields as untrusted hints; never follow
  arbitrary pointers, override genuine kernel blocking, or prevent pending
  termination from running. See [queue waits](docs/queues.md#bounded-waits).
- [x] Add a four-CPU, single-threaded TCG icount trace target using
  `-icount shift=auto,sleep=on`. Boot hardware information selects `WFE` for
  hardware and that target, while HVF and ordinary MTTCG use active rechecks.
- [x] Stamp stable userspace waits from `CNTVCT_EL0` and expose validated
  `USER_WAIT` elapsed durations in process snapshots without changing scheduler
  state or trusting the timestamp for scheduling decisions.
- [x] Accumulate userspace queue-wait time and expose `USER SLEEP(s)` in `ps`
  plus `user_wait_usec` in procfs, retaining the total when a sampling request
  has already woken the process.
- [x] Report wait-adjusted active CPU time and recent percentages by subtracting
  only user-wait growth that overlaps each scheduled slice, while retaining raw
  scheduled residency in procfs.
- User proposal: a non-timer device interrupt makes all fibre-idle processes
  eligible again, across CPUs. Consider a shared wake generation rather than
  rewriting every process record in interrupt context; exclude reschedule IPIs
  from triggering a recursive broadcast. Mechanism and policy remain open.
- Wakeup cascades must remain live: a downstream service can run and become idle
  before its upstream producer publishes a Channel message. Later userspace
  publication need not generate a kernel interrupt. An IRQ broadcast alone is
  therefore insufficient; retain bounded timer-driven retries or authoritative
  readiness checks. Treat fibre-idle as an advisory scheduling hint, not an
  indefinite kernel-blocked state.

#### Proposed Dependency-Ordered Waiters

- [x] Prioritise every runnable process with unread incoming queue/Channel messages
  above processes without messages, regardless of process class. This includes
  requests, replies, relay messages, and kernel service notifications. Existing
  class/last-run ordering breaks ties; consuming messages removes the priority.
  Implemented using existing kernel mappings, with no per-message syscall. See
  [receiver scheduling priority](docs/queues.md#receiver-scheduling-priority).
- [x] Respect an active shared wait before granting receiver priority or retaining
  the running receiver. Unread messages on another Channel cannot override the
  wait: the process may need a peer to run before it can consume those messages.
  Regression tests cover both initial selection and timer retention, then
  restoring receiver priority when the watched Channel progresses. See
  [receiver scheduling priority](docs/queues.md#receiver-scheduling-priority).
- [x] Add outstanding-request identity and parent propagation independently of
  unread-message state. See [Channel tickets and preferred scheduling](docs/dependency-scheduling.md).
- [ ] Measure publication-to-eligibility, eligibility-to-selection, and service
  handling separately; tune residency using those measurements.
- Keep unread transport work distinct from an outstanding request: a server may
  consume a request and then wait on another service. Account for the warden's
  existing relay stages and cross-CPU
  observation latency. Queue inspection must not require kernel parsing of I/O
  payloads or treating a userspace event as immediate scheduler entry.
- Agreed policy is strict highest priority for message receivers. Fairness between
  receivers and processes without messages is not imposed; sustained unread
  traffic can starve the latter.
- [x] Add dependency-aware preferred scheduling across general-purpose CPUs. User
  proposal: represent P1 waiting on P2 waiting on P3; when P3 publishes a reply,
  enqueue P2 and speculative predecessor P1 for dispatch on available cores.
  P1 should be resident and ready, using its userspace WFE wait until the reply
  arrives, rather than merely reserving capacity for a later dispatch. Retain
  each active stage until it waits on a Channel/queue, with the warm-wait
  exception below. Implemented as opportunistic FIFO scheduling, not exclusive
  gang scheduling; overlapping chains may interleave. See
  [Channel tickets and preferred scheduling](docs/dependency-scheduling.md).
- User requirement: use a preferred scheduler run queue, distinct from IPC
  message queues. Enqueueing a chain member does not by itself force immediate
  preemption of a busy core whose current process has not handed off to wait.
  When a core selects its next process, take preferred queued work ahead of
  ordinary longest-waiting selection or idle. Queued time is not CPU residency;
  start the residency window only on actual dispatch. Implemented as a shared,
  deduplicated FIFO ordered by kernel observation.
- User requirement: track when a process actually receives CPU control and avoid
  switching it away too soon. A speculatively dispatched predecessor needs a
  residency window that permits its unchanged shared wait; ordinary timer
  selection must not immediately undo the warm dispatch. Initial bounds are
  10 ms warm residency and 10 ms preferred processing, without renewal on timer
  checks. Blocking and termination override retention; tuning remains open.
- [x] Carry generation-tagged tickets, origin PID, and parent identity through
  Channel control metadata and the warden. Publish durable atomic records in
  write-owned queue pages; validate endpoint ownership, ancestry, and live
  generations. Kernel Channel transports use `QUEUE_KERNEL_PID` while retaining
  the real originating process for dependency roots. Preserve request context
  across fibre switches. Reject cycles, discard stale/acknowledged links, and
  remove queued processes on exit.
- [x] Allow preferred selection on another started core after the outgoing
  context-switch tail is safe. Do not steal running or kernel-blocked processes.
- [ ] Evaluate event-triggered cross-core notification and adaptive residency.
  Current observation uses existing scheduler entries, without a new IPI.
- Open scope: exclusive cluster budgets/arbitration and context integration for
  custom asynchronous dispatchers outside the common I/O/block wrappers.

- [x] Add shared queue-observation wait controls and scheduler eligibility checks.
  Registration allocates once; normal waits publish without syscalls. The kernel
  validates connection ownership, observes durable cursors and notification
  readiness, and leaves incomplete or invalid publications runnable. Stable
  publications survive unrelated event returns; kernel notification producers
  advance a wake generation to return the process to its dispatcher.
- This foundation gates existing channel hops on actual transport progress.
  It does not yet implement the IRQ token/hop protocol below, stage acknowledgements,
  or operation-failure timeout reporting. See [queue waits](docs/queues.md#bounded-waits).
- [ ] Explore the user's shared per-process waiter record containing a token,
  hop count, and deadline, avoiding per-wait syscalls. On IRQ satisfaction,
  schedule the hardware-facing stage before its dependent stages. A completed
  stage acknowledges through shared state, releasing the next stage. This is
  an authorized implementation task; request-chain protocol integration remains.
- Required distinction: waking a stage is not completing it. Publish the channel
  result before acknowledging its dependency stage. Preserve completion evidence
  across immediate record reuse; clearing a token transiently is insufficient.
- Open design: identify individual request chains sharing an IRQ source, account
  for branches and multiple blocked fibres, and validate token ownership and
  generations. Hop count alone does not identify which producer a caller needs.
- User requirement: an expired deadline makes the process runnable and exposes
  a timeout for that wait. Define separate kernel-owned outcome fields and
  userspace-owned request fields. Timing out does not cancel hardware DMA or
  authorize buffer recycling; operation cancellation remains protocol-specific.
- Open design: detect shared waiter updates across CPUs without per-wait syscalls.
  Timer/scheduler observation introduces latency; event signalling alone does not
  force kernel scheduler entry. Retain failure/exit handling and prevent unrelated
  stalled requests from blocking every waiter on the same device token.

### Shared Bulk Buffers: hunde-puffer

Status: initial implementation in place; policy extensions and performance
measurement remain below. See [Shared buffers](docs/shared-buffers.md).

#### Allocation and Directional Mappings

- [x] Add the `hunde-puffer` service to manage buffer leases through authorized
  kernel allocation, mapping, revocation, and freeing operations.
- [x] Map buffers at common addresses in a reserved range disjoint from other
  user mappings; roll back partial setup failures.
- [x] Provide read buffers writable by the server and read-only to the client,
  and write buffers writable by the client and read-only to the server.
  All mappings must be non-executable.
- [x] Use 2 MiB per directional buffer, with 256 backing slots and at most four
  detached buffers retained in the pool. Map through the architecture page API.
- [ ] Add variable-sized leases and per-client/service quotas when required.

#### Channel Protocol and Migration

- [x] Have protocol open responses advertise ready buffer mappings, capacity,
  direction, and lease identity/generation over the existing Channel.
- [x] Send read/write commands with validated lease identity, length, and
  resource identity. Transfers use offset zero and report actual completion.
- [x] Share one directional pair across a session's protocol resources, with
  the existing single outstanding request serializing buffer access. Treat
  producer bytes as mutable/untrusted, despite read-only consumer mappings.
- [x] Migrate file I/O, executable loading, and filesystem/block-driver paths
  away from 256/512-byte inline bulk transfers. The mediator still copies
  between separate upstream and downstream buffer pairs.
- [x] Bootstrap the manager before buffer-dependent services without creating
  an executable-loader startup dependency cycle.

#### Housekeeping and Resource Safety

- [x] Handle explicit buffer disposal, protocol-resource release, and Channel
  closure as distinct events converging on idempotent cleanup.
- [x] Reconcile affected leases after kernel process exit using a durable
  per-buffer notification slot and authoritative kernel attachment state.
  This is not the general process-exit subscription service.
- [x] Retain surviving attachments until services drain work and release them;
  revoke mappings before reuse. Service-to-service backing is now retained
  for the rest of the boot, including after both services detach or exit.
- [x] Cache safely detached backing in a bounded pool before hard freeing it.
  Sanitize before reassignment and reject stale lease generations.
- [ ] Define unresponsive-peer handling and manager recovery. Restart/remapping
  remains an open design problem, not permission to replay uncertain operations.
- [ ] Publish pooled backing usage separately; participant mappings already
  count as shared memory. Add pressure-driven eviction and live-peer policy.
- [x] Pass multi-sector requests directly into the filesystem's shared
  block-client buffer. VirtIO submits contiguous ranges up to buffer capacity;
  SDHCI uses multi-sector PIO without a sector staging copy.
- [x] Test directional permissions, partial mapping rollback, exit during open,
  stale descriptors, sanitized reuse, and failed pool frees. Smoke-test HVF boot,
  executable loading, FAT32/EXT2 reads, and memfs read/write.
- [ ] Benchmark boot/spawn and multi-block throughput; compare IPC and hardware
  submission counts separately.

#### End-to-End Buffer Reuse

- [x] First step: retain backing allocated between two kernel-registered
  services until reboot. Never pool, free, or reassign it, even after close or
  process exit; still revoke exiting processes' mappings. Keep ordinary client
  cleanup unchanged. See [Shared buffers](docs/shared-buffers.md).
- [ ] Replace permanent retention with orchestrated completion/quiescence and
  reclamation. Explicit unmount and hot-unplug cleanup are deferred; service
  restarts do not automatically inherit old mappings or replay I/O.
  The QEMU Pi UEFI trace EXT2 mount now targets the EXT2 disk rather than the
  FAT EFI disk, removing its repeated failed-startup buffer consumption. See
  [Userspace I/O system](docs/io-system.md). Repeated service failures can still
  exhaust retained slots until coordinated reclamation is implemented.
- [ ] Reuse the client's shared buffer through I/O mediation, FAT32/EXT2, and
  the block driver. Keep control and completions on existing Channels; authorize
  downstream attachments through `hunde-puffer` rather than allocating and
  copying into a separate buffer at each layer.
- [ ] Translate file ranges into block requests referencing the same lease and
  buffer offsets. Coalesce adjacent disk ranges; allow fragmented file ranges
  to fill distinct portions of the destination without a whole-transfer copy.
- [ ] DMA directly to/from those shared ranges with validated device mappings,
  direction, bounds, cache maintenance, and hardware segment limits. Pin backing
  until completion or verified device quiescence, including on process exit;
  do not recycle a buffer merely because its client has died.
  The filesystem-to-VirtIO step now uses direct DMA with boot-lifetime retained
  backing; application-to-filesystem forwarding and reclaimable DMA pins remain.
- [ ] Handle partial-sector reads without exposing neighboring file data, and
  partial-sector writes without overwriting neighboring data. Use bounded
  staging where alignment or hardware constraints require it, not by default
  for the whole transfer.
- [ ] Define downstream attachment authorization and in-flight range ownership
  before implementation. CPU read-only mappings do not freeze producer bytes
  or constrain device DMA. This is an agreed target, not implemented behavior.

## User Management

### User Registration and Persistent Credentials

Status: deferred. Add proper account registration and persistent credential
storage. Storage format, service ownership, and account-administration policy
remain design decisions, not settled implementation requirements.

#### Registration and Account Identity

- [ ] Provide authorized account creation with unique usernames and stable
  user/group identifiers. Define first-user provisioning and distinguish
  interactive accounts from system/service identities.
- [ ] Define account updates, disabling/removal, and password changes or resets,
  including who may perform each operation.

#### Credential Storage

- [ ] Persist account records and password verifiers across reboots. Keep
  credential material separate from publicly readable account metadata.
- [ ] Use an established password-hashing implementation with per-account
  random salts; do not store plaintext passwords or invent cryptography.
  Select the algorithm, parameters, and secure randomness source during design.
- [ ] Restrict credential reads and writes to the authentication authority;
  prevent secrets from appearing in logs, process listings, or filesystem
  metadata. Define crash-safe updates and recovery from damaged storage.

#### Authentication and Integration

- [ ] Connect login to stored accounts and establish authenticated process
  credentials through an authorized kernel boundary. Keep account policy and
  credential storage in userspace; the kernel enforces process identity.
- [ ] Resolve usernames/groups consistently for file ownership and commands,
  without exposing password verifiers. Define failed-login throttling and
  fail-closed behavior when credential storage is unavailable.
- [ ] Test duplicate registration, unauthorized updates, correct/incorrect
  passwords, credential changes, disabled accounts, reboot persistence, and
  interrupted storage updates.

## Device Management

### Bus Monitoring and Driver Lifecycle

Status: initial PCI scan and read-only Channel queries are implemented in
`hundebus`. The service remains running and owns its inventory. See
[Bus information](docs/bus-info.md).

#### Bus Change Detection

- [ ] Add attachment/removal reporting from bus-specific drivers, including
  USB host/hub drivers. USB enumeration and hotplug are not implemented yet.
- [ ] Have `hundebus` maintain the live device topology and coordinate driver
  startup and removal. Define how this integrates with existing supervision.
- [ ] Define coherent paged queries and update notifications once inventories
  can change; the current query interface describes an immutable startup scan.

### System and Bus Information Filesystem

Status: partially implemented. `sysinfofs` serves `/boot/sysinfo` from a typed
hardware snapshot. Bus presentation and the `/sys` layout remain deferred.

#### Data Sources and Presentation

- [x] Add a typed hardware-information syscall for configuration-independent
  bootstrap. `hund-io-system` uses it before `/boot` is mounted.
- [x] Implement `sysinfofs` using the existing filesystem protocol over Channel
  for `/boot/sysinfo`.
- [ ] Query `hundebus` through its Channel interface for bus records.
- [ ] Mount the filesystem at `/sys`; `/sys/info` and `/sys/bus/pci` are proposed
  paths, with final layout to be agreed during implementation.
- [x] Delete the kernel `/boot/sysinfo` provider; userspace `sysinfofs` owns it.

## System Startup

### Userspace Bootstrap

Implemented: [hunde-mama bootstrap](docs/bootstrap-services.md) establishes
`/sbin`, `/boot`, and `/boot/config` before configured startup. The filesystem
namespace accepts mounts over Channel; later mounts preserve the boot overlays.

#### Boot Level Handoff

- [x] Restrict `boot_spawn()` to the initial Mama PID during boot level 0.
- [x] Have Mama complete an irreversible handoff after establishing the basic
  mounts; bootstrap spawning is unavailable afterward.
- [x] Load normal executables through a kernel client of the existing I/O
  Channel protocol, preserving originating PID authentication and removing
  direct embedded and kernel-filesystem fallbacks.
- [x] Park the original normal-spawn syscall while its kernel Channel is pending.
  Scheduler-observed queue progress resumes the kernel continuation through
  OPEN, READ, and CLOSE; background spawn returns only after child scheduling,
  while waiting spawn transfers into the ordinary child wait.
- [x] Keep the immutable archive mapped for embedfs after handoff.
- [ ] Migrate executable transfers to the
  [hunde-puffer shared-buffer design](docs/shared-buffers.md) tracked above.
  The initial shared-buffer implementation replaces the 256-byte bulk replies.
- [ ] Remove the disk providers' 12 KiB open-file snapshot limit before relying
  on them for larger executables. See
  [Executable loading](docs/bootstrap-filesystem-handoff.md).
- [ ] Add credential-based executable-open authorization to the FAT32/EXT2
  adapters. They currently reject executable opens rather than allowing an
  unchecked disk-backed spawn.

#### Remaining Kernel I/O Removal

- [x] Remove the unused in-kernel message broker, scheduler message callbacks,
  and unconsumed process-exit broadcasts. Direct scheduler operations and
  shared IPC remain; see [Userspace I/O system](docs/io-system.md).
- [x] Delete the unused kernel console and stream backends and their UART IRQ
  wiring. Independent kernel logging and panic output remain; see
  [Userspace I/O system](docs/io-system.md).
- [x] Delete the obsolete kernel socket backend and userspace filesystem
  broker, including unused service operations and wrappers.
- [x] Remove kernel bootfs, EXT2, the unused block cache and memory block device,
  and the kernel mount API. Filesystem watchdogs only launch userspace providers.
- [x] Remove legacy stream-server syscalls, buffer mapping, and server maps.
- [x] Remove userspace file-I/O syscall fallbacks. Route link requests through
  the existing I/O and filesystem Channels; see [Userspace I/O system](docs/io-system.md).
- [ ] Implement hard-link creation in supporting filesystem drivers. Channel
  routing exists, but current providers return an explicit unsupported error.
- [x] Retire file I/O syscalls, generic kernel filesystem/device routing,
  `kernel/kio` wrappers, and process FD tables. Startup stdio retains terminal
  endpoint metadata only; general userspace handle inheritance remains deferred.
  Fault reports use independent kernel diagnostics. Retired syscall numbers
  remain reserved and fail dispatch. See [Userspace I/O system](docs/io-system.md).
- [x] Create the separate fibre-based `sdhci_channelio` driver using existing
  Channels and managed buffers, switch SD boot configurations, and retire the
  old SDHCI executable. The hardware path remains read-only polling PIO.
- [x] Remove VirtIO's legacy kernel-broker request/event path, retaining its
  Channel implementation.
- [x] Retire unused `IO__block_read`/`IO__block_write` syscall helpers.
  General fibre infrastructure remains.
- [x] Migrate `channel_blk`/`channel_fs` to BlockServer/BlockClient and delete
  the kernel block broker and private transport. Device discovery metadata and
  capability checks remain in the kernel. See [Userspace I/O system](docs/io-system.md).
- [ ] Replace the boot disk's size-based identification with an explicit stable
  boot-device identity supplied by the boot environment.

## Architecture

### RISC-V Support

Status: deferred. Proposed first target: RV64 on QEMU `virt`, with OpenSBI
firmware and a supervisor-mode kernel. Confirm the ISA extensions, ABI, and
virtual-memory mode before implementation; physical-board support is not yet
selected. QEMU provides a generated device tree and a suitable virtual platform
for initial bring-up. See the [QEMU RISC-V virt documentation](https://www.qemu.org/docs/master/system/riscv/virt.html).

#### Build and Architecture Boundaries

- [ ] Add RISC-V toolchain, assembler, linker scripts, kernel/userspace build
  targets, embedded binaries, and compilation-database entries.
- [ ] Implement the existing `kernel/arch` interfaces in `arch/riscv64`, with
  shared architecture primitives in `common/arch` and userspace assembly under
  the userspace architecture directories. Do not scatter ISA conditionals
  through core kernel, services, or protocols.
- [ ] Audit AArch64 assumptions in address layouts, ELF machine validation,
  page sizes, register contexts, compiler flags, and assembly selection.

#### Boot and Platform Discovery

- [ ] Implement entry code, boot stacks, linker layout, early memory setup, and
  the handoff into the architecture-neutral kernel.
- [ ] Read the device tree for memory, reserved regions, hardware threads
  (harts), timer frequency, interrupt controllers, UART, and VirtIO devices;
  populate the existing system-information and device-registry interfaces.
- [ ] Add firmware integration through the Supervisor Binary Interface (SBI),
  selecting and checking required extensions rather than assuming availability.
  See the [SBI specification](https://github.com/riscv-non-isa/riscv-sbi-doc).
- [ ] Preserve independent early-boot and panic output. Add the initial
  platform's UART implementation rather than assuming the AArch64 PL011 device.

#### Memory Management and Protection

- [ ] Implement page-table creation, mapping, unmapping, permissions, address
  space switching, and fault decoding for the selected RV64 paging mode
  (Sv39 is a proposed starting point).
- [ ] Implement local and remote translation invalidation, executable-code
  synchronization, and required memory barriers.
- [ ] Preserve kernel/user isolation and shared-memory permissions used by
  Channels, notification queues, and read-only registries. Audit DMA and MMIO
  ordering for the chosen platform.

#### Traps, Scheduling, and Userspace ABI

- [ ] Implement trap entry/return, register save/restore, syscall dispatch,
  user-mode entry, and process context switching.
- [ ] Port userspace startup, syscall wrappers, atomics, and fibre context
  switching. Define floating-point/vector enablement and state preservation
  consistently with compiler settings and the selected ABI.
- [ ] Implement timers and scheduler tick accounting, then secondary-hart
  startup, per-hart state, and interprocessor rescheduling.

#### Interrupts and IPC Wakeups

- [ ] Implement interrupt routing, masking, acknowledgement, and completion
  for the chosen controller (PLIC is the proposed initial target). Preserve
  device-clear-before-rearm semantics through `hund-unterbrict`.
- [ ] Implement the shared IPC platform signal/wait interfaces with correct
  ordering and lost-wakeup protection. Do not assume AArch64 `SEV`/`WFE` can be
  translated directly; choose a RISC-V wakeup mechanism and define any required
  kernel assistance for userspace waits and cross-hart notification.

#### Drivers and Verification

- [ ] Reuse the existing Channel-based I/O, block, and filesystem services;
  port hardware-specific UART and VirtIO integration without duplicating their
  protocols or moving userspace ownership back into the kernel.
- [ ] Bring up one hart first: boot/panic output, memory protection, timer,
  first userspace process, syscalls, and Channel request/reply.
- [ ] Verify login, console input/output, block I/O, and mounted filesystems,
  then multi-hart scheduling, shared-memory races, IRQ delivery, and wakeups.
- [ ] Add architecture-specific emulator tests and keep architecture-neutral
  unit tests and existing AArch64 targets passing.
