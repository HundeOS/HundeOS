# Intermittent Issues

Central record of intermittent runtime failures, their evidence, investigation
status, and validation gaps. Passing retries do not close an issue or establish
that two similar symptoms have the same cause. Record a demonstrated root cause
and regression evidence before marking an issue resolved.

The user explicitly deferred INT-001 and requested consolidation of similar
reports on 7 November 2026. INT-002 has its own earlier deferral below. Moving
historical reports here does not establish a new diagnosis or imply that an
unresolved failure is fixed. Reproducible platform limitations remain in the
[platform guides](README.md#platforms).

## Userspace

### Login And Shell Startup

#### INT-001

**RISC-V login timeout during static TLS acceptance**

- **Status:** unresolved; investigation explicitly deferred on 7 November 2026.
- **Environment:** four-core RISC-V QEMU, `qemu-riscv64-trace`; observed on
  6 November 2026 while validating static TLS and per-thread `errno`.
- **Evidence:** `test_qemu_grep_reads_pipeline[riscv64]` reached `login:`,
  accepted the username, printed `password:`, and then timed out after 60 seconds
  waiting for `bash-5.3# `. The console showed a newline after the password;
  no Bash prompt or reported process fault appeared in the captured trace tail.
  The pipeline command had not run, so this does not demonstrate a grep failure.
- **Reproduction command:**
  `uv run python -m pytest -q test/smoke/test_qemu.py -k 'thread_create_join or grep_reads_pipeline'`.
- **Subsequent evidence:** TLS probes pass on AArch64 and RISC-V. Forty-five
  subsequent RISC-V boots passed login, a grep pipeline, and the TLS probe;
  another 50 login/logout transitions within those boots passed. These results
  do not explain or close the original timeout.
- **Evidence retained locally:** `/tmp/tls-qemu-failure.log`,
  `/tmp/tls-riscv-repro.log`, `/tmp/tls-riscv-repro-second.log`,
  `/tmp/tls-riscv-stress-repro.log`, and `/tmp/tls-riscv-relogin-repro.log`.
  These are temporary diagnostic artifacts, not durable repository evidence;
  the observed failure and validation counts are preserved here.
- **Unknowns:** the blocked process, wait state, and failing call path were not
  captured. A relationship to TLS, earlier I/O stalls, or any known fixed defect
  has not been established.
- **Next investigation:** capture guest CPU stacks, process/thread wait states,
  and console/Channel progress during a recurrence. Retain the original input
  and timeout; do not increase the timeout to make acceptance pass.

See [userspace threads](architecture/kernel/threads.md) for the TLS scope.
The AArch64 TLS fault in the same test run was separately traced to stale
assembly objects missing thread-pointer save/restore. Assembly include
dependencies were repaired and the AArch64 TLS smoke passed; that finding does
not explain the RISC-V login timeout.

### Hosted I/O

#### INT-004

**Historical RISC-V console and pipe stalls with incomplete attribution**

- **Status:** historical observations, not a newly reproduced open failure.
  The captured exit-lifetime and PLIC defects below were fixed, but no evidence
  establishes that every earlier stall had one of those causes.
- **Evidence:** repeated hosted-I/O runs stopped after only `pri` or `exe` had
  echoed, in a builtin pipeline, in the first external multi-stage pipeline
  after procfs checks, or during the 70,000-byte builtin transfer. The focused
  procfs case passed on both targets while combined runs could still time out.
  A run after trace-buffer mapping repairs still stalled in the 70,000-byte
  transfer; its retry stopped after echoing only `exe`, before execution.
- **Later validation:** the complete hosted-I/O smoke passed six consecutive
  RISC-V runs after the exit-lifetime fix and passed on AArch64, with unchanged
  input and timeouts. Later direct-hosted-dispatch acceptance also passed three
  consecutive RISC-V runs with background EXT2 reads.
- **Remaining boundary:** preserve the distinct observations rather than
  attributing a future timeout to a fixed defect. A recurrence needs its own
  runtime capture; INT-001 is not assumed to be the same issue.

See the [hosted-I/O acceptance record](TODO.md#hunde-io-host) and
[Rust userspace smoke coverage](architecture/userspace/rust-userspace.md).

## Kernel

### Interrupt Delegation

#### INT-002

**RISC-V interrupt-manager instruction abort during hosted sysinfo acceptance**

- **Status:** unresolved. The user explicitly agreed to resume driver migration
  on 27 October 2026 while this failure remained open.
- **Evidence:** `hund-unterbrict` reported PC zero during a root filesystem copy,
  followed by failed UART IRQ subscriptions, repeated UART restarts, and kernel
  heap exhaustion. The relationship to the sysinfo migration is not established.
- **Diagnostics improved:** the fault logger now preserves full-width PC,
  fault address, syndrome, and exception code; a regression covers nonzero upper
  bits. The original temporary trace is no longer available, so the complete
  failing PC and call path still need capture.
- **Subsequent evidence:** twenty debugger-backed RISC-V boots and the original
  C-host comparison did not reproduce the fault. Passing retries do not close it.
- **Next investigation:** capture the failing call path before changing IRQ
  handling, driver restart policy, or treating the reported PC as a proven
  null-function-pointer call.

### Process Teardown And Queues

#### INT-003

**Historical `User exception without a current process` panic**

- **Status:** historical report with unconfirmed root-cause attribution.
- **Evidence:** observed during queue lifecycle investigation; it was not
  deterministically reproduced. Investigation identified an unsafe cross-CPU
  teardown path, which was removed from signal delivery.
- **Subsequent evidence:** four-core QEMU passed five queue-server kill/restart
  cycles, each with 128 successful round trips before termination, without a
  kernel panic. This is smoke coverage, not proof that the original panic's
  cause was established or that SMP teardown is exhaustively validated.
- **Next investigation if it recurs:** capture exception context and the
  running-thread/process ownership during termination and address-space handoff.

See [process termination and queue teardown](architecture/kernel/queues.md#process-termination-and-queue-teardown).

## Proven Fixes Related To Earlier Reports

These are resolved defects, not additional open intermittent issues. Their
evidence does not automatically close reports with similar symptoms.

### Active-Page-Table Lifetime On Exit

A debugger capture during a RISC-V login stall found a CPU in exit
cleanup/preemption with `satp` still referencing a root lacking the kernel
mapping. It repeatedly faulted fetching the trap vector while holding the
scheduler lock; the other three CPUs spun on that lock. Exit freed process page
tables before switching away from them.

Exit now detaches the active root before freeing user memory or page tables,
with hosted regressions for active and non-current exits. Scheduler, process,
and CPU suites passed 237 tests. The complete hosted-I/O smoke passed six
consecutive RISC-V runs, including the 70,000-byte transfer, and passed on
AArch64 without changing input or timeouts. See
[address-space detachment](architecture/kernel/fork.md#address-space-detachment-on-exit).

### Lost PLIC Enable-Word Updates

Concurrent kernel masking and userspace rearming lost PLIC enable-word updates,
causing intermittent RISC-V disk-operation stalls. Context enables are now
kernel-owned; masking and rearming use per-IRQ priorities. Every physical hart
receives the unshifted priority-page base, without exposing enable words to the
manager. Register/registration regressions and 100-cycle live mkdir/list/remove
acceptance passed on both architectures. See
[interrupt acknowledgement flow](architecture/userspace/interrupt-service.md#interrupt-and-acknowledgement-flow).
