# TODO

Granular work and open design questions supporting the high-level
[project goals](GOALS.md). Organize entries as:
System (level 2) -> Feature (level 3) -> Supporting features or architecture
(level 4). Unchecked items are outstanding; record unresolved design choices
explicitly. Entries are not authorization to start implementation.

## POSIX Compatibility

### Bash Milestone

Status: active project focus. The target is enough compatible behavior to
cross-compile and run Bash, including an interactive shell with external
commands, pipelines, redirection, signals, and job control. POSIX certification
is not the objective, and existing native commands and APIs remain supported.

Work through the following sections in order. Each stage must leave a tested
interface for both AArch64 and RISC-V rather than adding declarations or stubs
only to advance the Bash build.

#### 1. Reproducible Cross Build And Target Runtime

- [x] Pin the Bash release and patches, record source checksums and configuration
  switches, and add reproducible out-of-tree AArch64 and RISC-V build targets.
- [x] Define a HundeOS target triplet and teach the selected Bash release's
  `config.sub` about it, without pretending that HundeOS has another system's ABI.
- [x] Build an architecture-qualified target sysroot containing only HundeOS
  headers, startup objects, static userspace libraries, and required compiler
  runtime support. Do not link every userspace library object into every program.
- [x] Provide a reusable userspace CRT and link specification for `_start`, static
  libraries, garbage collection, architecture flags, and any required `libgcc`
  helpers. The packaged artifacts and smoke target are documented in
  `user/ports/bash/README.md`.
- [ ] Add a reviewed cross-configure cache for probes that cannot run target
  executables, including wait-status layout, signal behavior, process groups,
  named pipes, `dup2`, collation, byte order, and type sizes.
  - [x] Seed the cache with the verified header implementation of `isblank` and
    the deliberate absence of signal-mask-preserving `sigsetjmp`.
  - [x] Record the tested POSIX wait-status exit-code offset of eight bits.
  - [ ] Review and record the remaining runtime-only probes as their underlying
    interfaces are implemented and tested.
- [ ] Keep Bash host generators such as `mksignames` and `mkbuiltins` on the host
  compiler and headers while all target objects use only the HundeOS sysroot.
- [x] Install the resulting executable into the architecture-qualified disk
  rootfs and include both Bash builds in `make compile-commands`. Bash is now a
  PIE using `/sbin/wuff-ld` with `libc.wuff.1` as its only runtime dependency;
  its bundled support libraries and PIC `libm` remain statically linked.
- [x] Remove the replaced custom shell implementation and its obsolete tests.
  Login uses `/usr/local/bin/bash`; no retired-binary directory exclusion remains
  in the build. The standalone devfs and C socket-router sources are also gone;
  the host's live C devfs backend remains in `user/lib/fs/devfs.c`. See
  [Bash integration](../user/ports/bash/README.md) and
  [device filesystem](architecture/io-system.md#device-filesystem).

#### 2. Non-Local Control Flow

- [x] Add `<setjmp.h>` with architecture-specific `jmp_buf`, `setjmp`, and
  `longjmp`. The AArch64 and RISC-V QEMU smoke tests cover nested jumps, restored
  stacks, zero-to-one return conversion, and every ABI callee-saved register.
- [ ] Add `sigjmp_buf`, `sigsetjmp`, and `siglongjmp` after process signal masks
  exist. Define which mask state they preserve and test saved and unsaved mask
  modes on both architectures.

#### 3. Signal Foundation

- [x] Add `<signal.h>` with `sig_atomic_t`, `sigset_t`, `struct sigaction`, the
  required `SIG_*` values, disposition constants, mask operations, and flags.
- [x] Implement `sigemptyset`, `sigfillset`, `sigaddset`, `sigdelset`, and
  `sigismember` in the userspace compatibility layer.
- [ ] Complete POSIX kernel signal delivery semantics.
  - [x] Add kernel-backed default, ignored, and userspace-handler dispositions;
    per-process masks and pending signals; nested handler entry and `sigreturn`;
    and AArch64/RISC-V context restoration. All signals numbered 1 through 32
    can now be queued, and `SIGKILL` and `SIGSTOP` cannot be caught or blocked.
  - [x] Implement the default stop/continue actions for `SIGSTOP`, `SIGTSTP`,
    `SIGTTIN`, `SIGTTOU`, and `SIGCONT`, including scheduler stopped state and
    stopped/continued child events.
  - [ ] Complete signal generation at the remaining subsystem boundaries,
    including terminal-generated signals and `SIGPIPE` from pipe writes.
- [x] Implement `raise` and positive-PID `kill`, including signal-zero existence
  probes and errno translation.
- [ ] Complete the userspace signal interfaces and interruption behavior.
  - [x] Back `signal`, `sigaction`, `sigprocmask`, and `sigpending` with kernel
    process state and delivery.
  - [x] Replace the immediate-`EINTR` `sigsuspend` compatibility behavior with
    an atomic temporary-mask-and-wait operation.
  - [x] Implement `alarm` and process-group `kill`.
  - [x] Define and implement interrupted syscall behavior for child waits,
    sleeps, and queue-backed I/O. Caught handlers return `EINTR` or restart the
    saved syscall according to `SA_RESTART`; `SA_NOCLDSTOP` and `SA_NOCLDWAIT`
    control child signal and event generation.
- [x] Preserve signal dispositions and masks across fork without inheriting
  pending signals. Across exec, preserve ignored dispositions, the mask, and
  pending signals while resetting caught handlers and active handler frames.
  Early-boot and panic output remain independent of userspace signal delivery.

##### OpenBSD Command Acceptance Targets

- [x] Vendor the pinned OpenBSD `bin/kill/kill.c` unchanged, build it for AArch64
  and RISC-V, and provide its `NSIG`/signal-name table, `strcasecmp`,
  `strncasecmp`, signed PID type, positive-PID targeting, signal-zero checks,
  errno mappings, and command tests.
- [x] Complete OpenBSD `kill` acceptance with process-group targeting and
  sender/target permission rules, including PID `0`, `-1`, negative process
  groups, signal-zero probes, and same-session `SIGCONT` behavior.
- [ ] Provide `fg` and `bg` behavior through Bash job-control support rather
  than standalone executables. They require a shell-owned job table,
  `SIGCHLD` masking and delivery, stopped/continued child events, `waitpid` with
  job-control status, process groups, `SIGCONT`, `sigsuspend`, `tcsetpgrp`, and
  saved/restored terminal state. Keep the vendored OpenBSD source unchanged and
  put HundeOS integration outside the vendor tree.

#### 4. Compile And Static Link Closure

- [ ] Add the process, wait, terminal, time, resource, system-identification, and
  selection headers and types required by Bash: `pid_t`, `clock_t`, `sig_atomic_t`,
  `sigset_t`, `fd_set`, `timeval`, `rlimit`, `tms`, `utsname`, and `termios`.
  - [x] Add terminal-backed `ttyname` and `isatty`. Startup UART and keyboard/video
    endpoints report their logical terminal identity, duplicates preserve it,
    and invalid or non-terminal descriptors report `EBADF` or `ENOTTY`.
  - [x] Provide `<termios.h>`, `tcgetattr`, `tcsetattr`, `tcdrain`, and `tcflow`
    through the userspace I/O protocol. Terminal stream owners persist the
    attributes, Bash selects its POSIX terminal driver, and `nojobs.c` now
    compiles through the former `<sgtty.h>` boundary. Bash now compiles through
    `fork`, `puts`, its POSIX signal selection, `bashline.c`, and the remaining
    main shell objects and builtins. Libc now provides deterministic `rand` and
    `srand` plus collision-retrying `mktemp`, `mkstemp`, and `mkdtemp`.
    `mkstemp` uses exclusive creation and `mkdtemp` uses directory creation for
    atomic collision protection. Bash remains unpatched and `lib/sh/tmpfile.c`
    compiles on both targets.
  - [x] Provide `struct timeval`, `fgets`, and iteration over the built-in group
    database. Multibyte support avoids Bash 5.3's malformed fallback-only
    `rl_point++:` statement without modifying vendored source.
- [ ] Fill the constants and macros required by the build, including wait-status
  macros and options, descriptor and open flags, signal and terminal constants,
  integer limits, `ARG_MAX`, `OPEN_MAX`, and the expanded errno set.
  - [x] Add `<sys/param.h>` as a compatibility entry point for the limits already
    defined by HundeOS. Both target sysroots include it successfully, and Bash's
    configure detects it on AArch64 and RISC-V.
  - [x] Add `O_APPEND` with shared open-description state and atomic
    append-at-write behavior in the filesystem services.
- [ ] Complete the string and algorithm surface required by Bash, including
  `qsort`, `strncpy`, `strcat`, `strdup`, `strpbrk`, `strstr`, `strcasecmp`, and
  `strncasecmp`. Core memory, string, character-classification, and sorting
  routines come from an explicitly verified Newlib libc member allowlist;
  HundeOS retains its local `strdup`, `strcasecmp`, and `strncasecmp` adapters.
  - [x] Add and test the standard `strncpy` declaration and implementation,
    including NUL padding, truncation without forced termination, zero-length
    copies, and the returned destination pointer.
  - [x] Add and test the standard `strcat` declaration and implementation,
    including empty-source behavior and the returned destination pointer.
  - [x] Add and test allocating `strdup`, including empty strings and ownership
    independent of the source buffer. Bash's `lib/sh/itos.c` compiles on both
    targets.
  - [x] Select and target-test Newlib's `qsort` for arbitrary-sized elements.
  - [x] Add deterministic `rand` and `srand` for non-cryptographic compatibility.
    The implementation is reproducible when reseeded and does not claim entropy.
  - [x] Add `mktemp`, `mkstemp`, and `mkdtemp` with validated templates,
    deterministic candidate names, collision retries, and atomic creation for
    files and directories. `mktemp` remains a compatibility-only, non-atomic
    name query.
  - [ ] Design and implement a secure system entropy source before exposing
    `getrandom`, `/dev/urandom`, or using random values for salts, tokens, or
    other security-sensitive purposes.
- [ ] Complete numeric conversion and math declarations, including `strtol`,
  `strtoul`, `strtod`, range handling, `<inttypes.h>`, and `<math.h>`.
  - [x] Pin Newlib `4.6.0.20260123` and build its `math.h` dependency closure
    and `libm.a` for AArch64 and RISC-V. The sysroot deliberately does not
    expose unsupported Newlib libc headers. The math smoke binary links on both
    targets, and Bash compiles into its fallback `strtod` implementation.
  - [x] Provide Newlib's `strtol`, `strtoul`, `strtoll`, and `strtoull` through
    the verified libc member allowlist. The target probe covers bases, end
    pointers, signed and unsigned limits, `ERANGE`, and invalid-base `EINVAL`
    behavior on AArch64 and RISC-V.
- [x] Add the `abort` declaration and terminating implementation. `SIGABRT` uses
  the kernel's fatal-signal path and reports signal termination to the parent.
- [ ] Complete the required stdio surface, including `sprintf`, `vsnprintf`,
  `vprintf`, `puts`, character and line I/O, `perror`, `setlinebuf`, and `dprintf`.
  - [x] Provide `sprintf`, `vsprintf`, `snprintf`, `vsnprintf`, and `putc` over
    the existing bounded formatter and descriptor-backed streams.
  - [ ] Remove the observed wide-format output truncation: on AArch64,
    Bash `printf '%070000d' 1` reports success but emits only 1,023 bytes,
    including when sent directly to a file without a pipe. Investigate the
    formatter/stdio boundary separately; pipe capacity tests must generate
    data in smaller chunks until this is resolved.
  - [x] Provide and test `puts`, including its trailing newline and output-failure
    behavior. Bash compiles through `mailcheck.c` on AArch64 and RISC-V.
- [ ] Add the time, identity, system, and resource interfaces needed to close the
  link: `gettimeofday`, `clock_gettime`, `nanosleep`, meaningful calendar time,
  process IDs and credentials, `uname`, `times`, `sysconf`, `pathconf`, and the
  resource-limit family.
  - [x] Provide `clock_gettime(CLOCK_MONOTONIC)` from the architecture counter
    and a sequence-protected, kernel-owned page mapped read-only into every
    process. See
    [Shared clock page](architecture/clock-page.md).
  - [x] Provide `gettimeofday` from an optional boot-time wall-clock and
    monotonic correlation sample. QEMU uses PL031 or Goldfish RTC hardware and
    Raspberry Pi UEFI uses firmware `GetTime`; absence reports `ENOSYS`. This
    gives Bash seed variation but is not secure entropy. See
    [Shared clock page](architecture/clock-page.md).
    Bash now compiles through `lib/sh/random.c`; Newlib supplies the completed
    math and integer-conversion surfaces. HundeOS defines terminal window-size
    ioctls for source compatibility, but the ioctls and libc's `tcgetwinsize`
    and `tcsetwinsize` entry points validate the terminal and report
    `EOPNOTSUPP`; Readline uses its environment or fixed-size fallback. Both
    target builds now compile all Bash and Readline objects and link Bash ELFs
    with no unresolved symbols. The build uses bundled termcap as the
    owner of its globals, libc supports allocating `getcwd`, and weak POSIX
    environment entry points allow Bash's definitions to override libc without
    losing scoped HundeOS implementations.
  - [ ] Add post-boot wall-clock correction and synchronization semantics.
  - [x] Provide `times()` with microsecond ticks, separate user/system accounting,
    interrupt and scheduler exclusion, and cumulative CPU time for waited-for
    children. See [Process CPU time](architecture/process-cpu-time.md).
  - [x] Provide `sleep` over the native microsecond sleep syscall. Signal delivery
    can interrupt the kernel sleep; the compatibility function deliberately
    returns zero rather than calculating unslept seconds.
  - [x] Provide the fixed compatibility hostname `hundeos`. Bash detects
    `gethostname` and compiles `lib/sh/oslib.c` on AArch64 and RISC-V.
  - [x] Provide kernel-backed `alarm`, including replacement, cancellation,
    previous remaining-time reporting, and one-shot `SIGALRM` delivery.
- [ ] Disable NLS, dynamic loadable builtins, and network pseudo-paths for the
  first static port. Record each as an optional capability rather than providing
  false-success stubs; bundled Readline and termcap remain enabled.
  - [x] Patch Bash's network-disabled `netopen` fallback to include its existing
    diagnostic declarations. The fallback compiles on AArch64 and RISC-V without
    adding socket interfaces to libc.
  - [x] Add strict UTF-8 multibyte conversion, wide-character classification and
    display widths, and `C`/`C.UTF-8` locale selection, then enable Bash's
    multibyte paths.
- [x] Iterate configure, compile, and link failures into this inventory until an
  unmodified Bash source tree produces a HundeOS ELF on both architectures. The
  initial static closure is now superseded by the dynamic Bash build above.

#### 5. Fork, Exec, And Wait

- [ ] Complete the address-space and execution-state fork work described under
  [Fork Support](#fork-support), with defined copying semantics and rollback.
- [ ] Implement `fork` and the supported `vfork` behavior required by Bash.
- [x] Fix nested Bash fork failures caused by copying kernel-owned doorbell and
  queue-wait-control allocations while resetting their references. Exclude the
  backing and mappings from eager fork, then create fresh child IPC state.
  Regressions cover three process generations and background external-command
  loops alongside pipelines. See [process-local IPC allocations](architecture/fork.md#process-local-ipc-allocations).
- [ ] Implement `execve` and the required `exec*` family over the native overlay
  operation, including argv, environment, credentials, errors, inherited
  descriptors, and close-on-exec processing.
  - [x] Define and test the bounded 64 KiB offset-based path, argv, and envp
    payload used at the future exec syscall boundary. See
    [Process image overlay](architecture/exec.md).
  - [x] Validate and copy the payload in the kernel, build a candidate process
    image, and commit it atomically into the existing PID. The public `execve`
    entry point uses the existing exec syscall with `P_OVERLAY`; candidate
    construction failure leaves the calling image unchanged. See
    [Process image overlay](architecture/exec.md).
  - [x] Prepare and claim non-`FD_CLOEXEC` open resources through fresh
    authenticated sessions, with rollback before image commit.
- [x] Implement `wait` and `waitpid` over child events for any or selected
  positive-PID children, blocking waits, `WNOHANG`, normal exits, signal
  termination, `ECHILD`, and POSIX status encoding.
- [x] Complete process-group selection, stopped and continued child events, and
  interruption by delivered signals for `waitpid`.
- [ ] Implement the required `wait3` behavior.
- [ ] Complete effective group queries and parent/child lifecycle state needed
  by Bash, including reparenting children when their recorded parent exits.
  - [x] Add `getpid` and kernel-backed `getppid`. Processes retain the parent PID
    assigned at creation so the query does not dereference a departed parent.
  - [x] Back `getuid`, `getgid`, `geteuid`, and `getegid` with the kernel's
    current process credentials. Real and effective IDs are identical in the
    current single-identity model; `setuid` and `setgid` accept only unchanged
    identities and reject mutations with `EPERM` until a kernel operation and
    authorization rules exist.

#### 6. Pipes And Shared Descriptors

- [x] Implement the `hund-rohr` userspace anonymous-pipe service with authenticated
  paired endpoints, blocking reads, EOF after the final writer closes, 64 KiB
  bounded storage, atomic writes through 4096 bytes, backpressure, and `EPIPE`
  after the final reader closes. It uses the common I/O Channel and directional
  `hunde-puffer` buffers. Its Rust `pipe.wuff` now runs inside `hunde-io-host`;
  `hundeschule` waits for the retained endpoint instead of starting a pipe process.
- [x] Implement `pipe`, `dup`, `dup2`, and `F_DUPFD` over process-local
  descriptors and reference-counted open descriptions. Duplicates share the
  native endpoint, close it only after the final local descriptor closes, and
  clear `FD_CLOEXEC` on the new descriptor.
- [x] Implement userspace `F_GETFD` and `F_SETFD` storage for `FD_CLOEXEC`,
  including standard descriptors.
- [x] Add shared open-file status flags and `F_GETFL`/`F_SETFL`, preserving the
  access mode while allowing `O_NONBLOCK` to change. Nonblocking pipe reads and
  writes return `EAGAIN`; blocking descriptors retain the existing wait path.
- [ ] Deliver `SIGPIPE` as well as returning `EPIPE` once userspace signal
  dispositions and masks are implemented.
- [ ] Release `FD_CLOEXEC` descriptors reliably after a successful exec overlay.
- [ ] Give each child its own descriptor table referring to shared open resources;
  share file offsets and pipe endpoints while keeping descriptor flags and closes
  process-local.
- [ ] Authorize inherited references through each child's own authenticated
  `hund-io-system` session, with reference counting, rollback, exit cleanup, and
  no globally usable handle numbers.

#### 7. Environment, Current Directory, And Filesystem Semantics

- [ ] Propagate a copied environment through fork and exec.
  - [x] Add startup `envp`, mutable `environ`, `getenv`, `setenv`, `unsetenv`, and
    `putenv`. Existing native spawn callers receive an empty environment until
    they migrate to the exec payload.
  - [x] Copy `envp` into the replacement image during `execve`.
  - [ ] Copy the environment during fork once fork is implemented.
- [x] Add a per-process current directory with `chdir`, inherited cwd, relative
  path lookup, and truthful `getcwd` rather than resolving every path from root.
  The libc-owned state, fork/exec inheritance, and kernel startup transport are
  documented in [Userspace I/O system](architecture/io-system.md#common-application-interface).
- [ ] Implement the remaining open semantics Bash relies on.
  - [x] Implement atomic exclusive creation and create empty files at open time.
  - [ ] Apply creation modes and `umask`.
  - [ ] Implement truncation without remove/recreate.
  - [ ] Enforce close-on-exec. Append is implemented by the service owning each
    file offset.
- [ ] Implement working rename, symbolic links and `readlink`, hard links,
  temporary-file creation, unlink-while-open, and cleanup of here-documents.
- [ ] Complete `access`, effective-identity permission checks, timestamps, link
  counts, ownership, `umask`, filesystem metadata, and precise errno mappings.
- [ ] Add a filesystem resize operation and use it for shrinking or extending
  `ftruncate`; libc currently succeeds only when the requested size is already
  the file's size, which supports dense-copy finalization without hiding a real
  resize request.
- [x] Provide `/dev/null` through devfs with EOF-on-read and discard-on-write
  semantics. See [Userspace I/O system](architecture/io-system.md#device-filesystem).
- [ ] Provide usable `/dev/tty` and `/dev/fd` behavior, or configure Bash
  explicitly for an agreed equivalent where it does not change semantics.
  - [ ] Complete acceptance of fstab-configured Rust console instances in the I/O host, mounting
    numbered `/dev/ttyN` and `/dev/vttyN` character devices and an explicit
    `:console:` default alias. Pair the existing UART or keyboard/framebuffer
    services behind host-owned bidirectional handles, terminal attributes,
    foreground state and bounded input-pump fibres on the host scheduler.
    See [configured consoles](architecture/io-system.md#configured-consoles).
    `/dev/tty` controlling-terminal selection, virtual-terminal switching and
    hardware-service consolidation remain unimplemented.
    - [x] Correct devfs root metadata and host devfs in the Rust I/O host through
      a safe adapter over its retained C backend. Remove the standalone devfs
      executable. See [device filesystem](architecture/io-system.md#device-filesystem).
    - [x] Correct kernel-generated block aliases to canonical `/dev/hdN` paths.
      AArch64 and RISC-V boot acceptance verify complete `/dev` listings,
      canonical and alias metadata reads, inherited handles, `/dev/null` I/O,
      and configured serial console input/output. See
      [device filesystem](architecture/io-system.md#device-filesystem).
    - [x] Fix and complete repeated-boot acceptance for the listener-registration
      race that prevented configured consoles from opening UART Channels and reaching
      login. `cs_return(value)` now captures its expression before releasing the
      mutex; all callers use the function-like form. Hosted regressions fail
      with the previous unlock-first behavior and pass with the corrected macro.
      The corrected kernel reaches login in 1,000 consecutive four-CPU AArch64
      boots using fresh clones of the original failing disk snapshots. Root/devfs/
      console, nested-fork/filesystem stress, and socket/70,000-byte pipe acceptance
      pass on both AArch64 and RISC-V. Native suites pass 421 tests; hundelint,
      repository formatting, and 122 Python tooling tests pass.
      See [kernel lock ownership](architecture/queues.md#kernel-lock-ownership).
    - [ ] Validate the configured keyboard/framebuffer console on the UEFI
      framebuffer profile; serial console acceptance passes on both architectures.
- [ ] Replace the sixteen-argument spawn limit with defined argv and environment
  byte limits, report `E2BIG`, and test large command invocations.
  - [x] Define `ARG_MAX` as 64 KiB and enforce it in the exec payload builder.
    Native spawn remains limited to sixteen arguments and still needs migration.

#### 8. Terminal Sessions And Job Control

- [ ] Implement `<termios.h>`, `tcgetattr`, `tcsetattr`, terminal control
  characters and modes, and working window-size `ioctl`.
  - [x] Implement `isatty` and `ttyname` from userspace terminal endpoint
    identity, including descriptors duplicated from standard streams.
  - [x] Transport and persist POSIX terminal attributes in terminal stream
    services. `TCSANOW` and `TCSADRAIN` are accepted; unsupported software flow
    control reports `EOPNOTSUPP`.
  - [ ] Apply canonical input, echo, special-character, and flow-control modes
    in the terminal input/output services, and implement window-size control.
- [x] Implement process groups and sessions with `setpgid`, `getpgrp`, `setsid`,
  `killpg`, and group-aware parent/child lifecycle rules.
- [ ] Add controlling-terminal ownership and `tcgetpgrp`/`tcsetpgrp`, including
  foreground and background access checks.
- [ ] Deliver terminal-generated `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`,
  `SIGTTOU`, and `SIGHUP` to the correct foreground process group, with stopped
  and continued child reporting.
- [ ] Exercise bundled Readline against real terminal modes, resize reporting,
  signal interruption, input editing, and history files.

#### 9. Bash System Validation

- [ ] Add userspace unit and integration tests for each compatibility interface,
  using native-service fakes or alternate linked implementations as appropriate.
- [ ] Add AArch64 and RISC-V full-system tests for startup, builtins, variables,
  environment propagation, scripts, external commands, exit status, and cleanup.
- [ ] Test redirection, pipelines, command substitution, here-documents, Ctrl-C,
  stopped and resumed jobs, foreground/background jobs, and clean shell exit.
  - [ ] Fix the foreground `cat` Ctrl-C timeout reproduced by the full-system
    command smoke test on both AArch64 and RISC-V with the C root provider.
    Hosted-root validation must not attribute this control failure to Rust EXT2.
  - [x] Extend memfs open-description inheritance for redirected external
    commands. Hosted Rust memfs uses authenticated grants sharing open
    descriptions and offsets across fork/exec. See
    [temporary filesystem](architecture/io-system.md#temporary-filesystem).
- [ ] Run representative upstream Bash tests that match the supported feature
  set and record deliberate unsupported optional facilities.

### Existing Command Compatibility

- [x] Stage an architecture-qualified disk rootfs and build the unmodified,
  pinned OpenBSD `cat` source under `/usr/local/bin`. Keep it out of the embedded
  bootstrap archive and exercise raw and formatted reads on both architectures.
- [x] Introduce process-local `int` descriptors over native 64-bit I/O handles,
  POSIX read/write/close behavior and errno mapping, compatibility stdio, getopt,
  ctype, err/warn, program identity, and file-status headers.
- [x] Port the pinned OpenBSD `ls`, `ln`, `mv`, `mkdir`, `rm`, `cp`, `grep`, and
  `tail` sources into `/usr/local/bin`, with their existing full-system coverage.
- [x] Stream disk reads through filesystem providers, including EXT2 direct,
  single-indirect, and double-indirect blocks and FAT32 cluster chains.
- [ ] Define enforceable process restriction semantics before treating the
  successful OpenBSD `pledge` compatibility call as a security boundary.
- [ ] Define whether secure overwrite can provide a meaningful guarantee over
  the userspace filesystem and block-service stack before supporting `rm -P`.
- [ ] Allocate EXT2 inodes and data blocks across block groups; writable images
  currently reserve enough group-zero inodes for normal operation.
- [ ] Move post-root services and drivers to the disk rootfs, then mount the
  embedded archive at a dedicated bootstrap path containing only bootstrap code.

## Userspace Service Hosts

### Consolidated I/O And Driver Hosts

Status: architecture agreed and documented in
[I/O and driver hosts](architecture/io-driver-hosts.md). `hund-unterbrict`
already hosts VirtIO block instances and dispatches their IRQs locally. The
Rust socket, pipe, memfs, and procfs components now load inside `hunde-io-host`, preserving
the `socket_router`, `hund-rohr`, `/tmp`, and `/proc` endpoints. Other I/O components still use the current
multi-process service graph. See the
[first Rust socket slice](architecture/io-driver-hosts.md#first-rust-socket-slice).

#### Baseline And Host Interfaces

- [x] Retire the standalone C socket router, its obsolete tests, and all boot
  configuration entries. The Rust `socket.wuff` component inside `hunde-io-host`
  retains the existing `socket_router` endpoint; the C server is no longer built
  or embedded. See [socket protocol](architecture/io-system.md#socket-protocol).

- [x] Define the `hunde-io-host` and `hunde-treiber-host` responsibility,
  trust, cache, IPC, shared-buffer, and failure-domain boundaries.
- [ ] Measure representative open, cache-hit, cache-miss, block, console, and
  network paths before consolidation. Record latency, Channel publications, and
  scheduling transitions rather than assuming every path loses a process hop.
- [ ] Define static, versioned C component interfaces with authenticated host
  request contexts, explicit ownership, asynchronous completion, and cleanup.
- [ ] Preserve external service protocols during migration, but remove internal
  Channel paths after both components reside in the same host.

#### `hunde-treiber-host`

- [x] Extract VirtIO block into an instance runtime with per-device DMA state,
  permit one process to claim multiple devices, and dispatch hosted block IRQs
  without an interrupt-client Channel relay.
- [ ] Evolve `hund-unterbrict` into `hunde-treiber-host`, retaining delegated IRQ
  receipt, device-clear-before-rearm ordering, MMIO and DMA ownership, and
  fail-closed handling of unserviced interrupt sources.
- [ ] Move block, network-interface, UART, display, input, and other
  hardware-facing drivers into the host as required by current hardware targets.
- [ ] Remove standalone driver compatibility executables after full-system
  parity and measurements pass on AArch64 and RISC-V.
- [ ] Define host crash cleanup for device claims, IRQ routes, DMA, and requests
  before relying on automatic driver-host restart.
- [ ] Investigate the RISC-V interrupt-manager instruction abort observed during
  hosted sysinfo acceptance: `hund-unterbrict` reported PC zero during a root
  filesystem copy, followed by failed UART IRQ subscriptions, repeated UART
  restarts, and kernel heap exhaustion. Preserve the original trace and establish
  the failing call path before changing IRQ handling or restart policy; the cause
  and relationship to the sysinfo migration are not established. The fault
  logger truncates addresses to 32 bits, so the full PC still needs capture.
  The user explicitly approved continuing migration while monitoring the crash;
  unchanged-image cold boots remain under debugger capture. This is unresolved,
  not validated by passing retries.

#### `hunde-io-host`

- [x] Replace the C FAT32 provider with native safe Rust `fat32.wuff` in the
  I/O host, rather than adding a Rust wrapper over the C engine.
  - [x] Add a read-only `no_std` Rust engine over `hunde::block::Device`, with
    checked FAT32 geometry, cycle/bounds validation, short-name path lookup,
    paginated directories, fragmented offset reads, and sparse image fixtures.
  - [x] Add engine writes, allocation, creation, zero-length truncation, and
    file/empty-directory removal, with native parity tests, failure injection,
    and disposable-image filesystem checks. Existing mounts remain unchanged.
  - [x] Add the safe `IoComponent` adapter and native tests for authenticated
    handles, shared inherited offsets, permissions, and lifecycle cleanup.
  - [x] Add `fat32.wuff` packaging, approved filesystem loading, explicit
    driver selection in mount control, and boot mount/namespace parsing.
    Device reservations reject duplicate devices across filesystem formats.
  - [x] Resolve integration acceptance blockers: the Wuff loader now allocates
    the complete aligned image span before choosing a load bias, preventing
    later segments from colliding with existing process allocations. Segment
    permissions and RELRO remain separate; gaps are inaccessible and backing
    memory is freed once. Regression tests cover fragmented holes, alignment,
    permissions, and cleanup. `humpen` skips hosted FAT32 mounts, and trace
    validation uses guaranteed I/O readiness rather than verbose-only logging.
    FAT32 acceptance and root-filesystem smoke tests pass on both architectures;
    disposable FAT32 images pass filesystem consistency checks. See
    [Loader boundary](architecture/dynamic-linking.md#loader-boundary).
  - [x] Preserve long-name lookup, executable opens, and recursive removal;
    migrate configured FAT32 mounts and retire the C backend, launcher, and
    obsolete C tests. Boot configuration accepts the old `fat32` spelling as a
    hosted alias. Acceptance verifies existing SD/channel mounts, long-name
    reads, execution from FAT32, recursive removal, and absence of the C driver.
  Long-name creation and POSIX unlink-while-open remain unimplemented; neither
  was supported by the C provider. Whether to add them is a separate follow-up
  decision. Live or grant-retained files return busy rather than losing storage
  underneath open handles.
  See [FAT32 hosting preparation](architecture/io-system.md#fat32-hosting-preparation).

- [x] Organize host, filesystem, socket, and streams into module directories.
  Share typed subsystem dispatch through `IoComponent`, preserve existing
  endpoint/readiness behavior, and keep FFI translations in `hunde`. Filesystem
  instances own `/tmp` and `/proc`; namespace/rootfs migration and EXT2 mounting
  remain separate work. See [host subsystems](architecture/rust-userspace.md#host-subsystems).
- [x] Replace raw subsystem endpoint/message byte slices with distinct
  `ServiceName` and `ReadinessMessage` types throughout the shared service
  adapter and all host modules. Convert text to bytes at the library boundary;
  reject accidental interchange at compile time. See
  [host subsystems](architecture/rust-userspace.md#host-subsystems).
- [x] Support multiple Rust fibres on one process-owned native scheduler, with
  per-fibre callbacks, owned captures, joinable task results, spawn-failure
  cleanup, and wake-all polling on Channel events/task completion. Keep the
  existing endpoint poller intact; future mount/control work must share this
  scheduler rather than introduce component schedulers. See
  [scheduler ownership](architecture/rust-userspace.md#one-scheduler-multiple-fibres).
- [x] Start the Rust I/O host, load a resident Rust socket Wuff, preserve existing
  Channel/queue authentication and managed buffers, and bridge its polling
  Future to the existing fibre scheduler. Keep standard source-local Rust unit
  tests and per-crate `tests/` integration test conventions documented.
- [x] Consolidate C ABI and runtime translations in `user/lib/hunde`, including
  typed I/O requests, reusable component callbacks, service/Wuff/fibre adapters,
  and core-only lifecycle smoke support. Host and component crates use thin
  registration declarations; see the
  [shared C boundary](architecture/rust-userspace.md#shared-c-boundary).
- [x] Move host startup, Rust `main`, component ownership and dispatch, endpoint
  registration/readiness, and scheduler execution policy into Rust. Remove the
  host-specific C launcher/dispatcher; keep only reusable native-library and
  protocol translations in `user/lib/hunde`. See the
  [Rust host boundary](architecture/io-driver-hosts.md#first-rust-socket-slice).
- [x] Colocate Cargo crates with their service, driver, library, or component in
  the normal userspace tree, using `user/Cargo.toml` as the workspace. Forbid
  unsafe Rust in consumers and tests; keep the approved bundled-component loader
  and raw ABI operations in `hunde`. See
  [Rust userspace](architecture/rust-userspace.md#unsafe-boundary).
- [x] Generate Rust I/O operation, status, and flag constants from the existing
  C enums during Cargo builds, replace duplicated protocol literals, and test
  regeneration and C value parity. See
  [protocol constants](architecture/rust-userspace.md#protocol-constants).
- [x] Reserve restored local handle identities in the client allocator, including
  overflow-safe exhaustion checks for open, accept, and paired pipe allocation.
  Native regressions cover restored stdio collisions and allocation bounds.
- [x] Move pipes into resident Rust `pipe.wuff` in `hunde-io-host`, retain
  the `hund-rohr` endpoint, and retire its standalone executable. Add typed
  paired-handle replies and authenticated fork/exec grants; poll sockets and
  pipes together through the existing fibre and Channel adapters. See
  [hosted Rust pipes](architecture/io-driver-hosts.md#hosted-rust-pipes).
- [x] Initialize libc's cached child PID before inherited Channel attachment.
  AArch64 debugger evidence showed children 38/39 attaching queues with cached
  parent PID 33. The fork wrapper now refreshes identity before claiming I/O;
  a native ordering regression covers this requirement. See
  [fork inheritance](architecture/fork.md).
- [x] Complete hosted pipe full-system acceptance on AArch64 and RISC-V.
  Both boot both Wuffs without a standalone pipe process and pass external
  multi-stage pipelines, inherited builtin reads, a 70,000-byte transfer, and
  repeated socket exchanges while pipes are active. The smoke generates data
  in 1,000-byte chunks and reads it with a builtin to isolate pipe behavior
  from the separate formatter limitation above.
- [x] Port memfs into a safe Rust `memfs.wuff` hosted with sockets and pipes;
  preserve creator permissions, sticky-root removal, bounded descriptions,
  directory pagination, seek/append, and dynamic contents. Add filesystem
  request/reply translation and kernel credential snapshots to I/O ABI v3,
  and share inheritance bookkeeping with pipes in `hunde`. Retire the C
  executable and wait for hosted readiness during `/tmp` mount setup. See
  [hosted Rust memfs](architecture/io-driver-hosts.md#hosted-rust-memfs).
- [x] Add and run hosted memfs full-system acceptance on AArch64 and RISC-V,
  including redirected external commands, fork/exec inheritance, directory
  pagination, append, file copies, and socket/pipe coexistence. The full smoke
  verifies the absence of standalone memfs and the resident Wuff readiness.
- [x] Port procfs formatting, read-only metadata, snapshot descriptions, seek,
  authenticated ownership, and inheritance into safe Rust `procfs.wuff`.
  Add shared `hunde::process` accounting/trace callbacks in I/O ABI v4, retain
  the `/proc` Channel and namespace mount, and leave `ps` on `process_list`.
  See [hosted Rust procfs](architecture/io-driver-hosts.md#hosted-rust-procfs).
- [x] Complete focused hosted procfs acceptance on AArch64 and RISC-V, including
  process listing, directory enumeration, all accounting leaves, inherited
  snapshots, kernel-trace reads, and absence of a standalone procfs process.
  Remove the retired C implementation and replace its tests with source-local
  Rust tests and native boundary regressions.
  The native trace callback uses an ordinary page-backed temporary buffer:
  Rust heap slices may cross mappings, while the syscall requires one mapping
  containing its output. It checks bounds, copies the snapshot, and releases
  the buffer on success and failure. Tests cover allocation/release failures,
  invalid capacities, oversized results, and empty/full snapshots.
  `test_qemu_rust_procfs_component` passes on both architectures. The combined
  I/O smoke passes on both architectures after the exit-lifetime fix below.
  See [process registry and procfs](architecture/process-registry-and-procfs.md).
- [x] Fix the active-page-table lifetime bug exposed by intermittent RISC-V
  full-system timeouts and repeat hosted-I/O acceptance. Before the fix, a repeated
  run stops after echoing only `pri` from the next redirection command, before
  the command is fully entered. An earlier run stalled in a builtin pipeline.
  During procfs migration, another run completed every process-accounting leaf
  and inherited snapshot read, then stalled in the first external multi-stage
  pipeline. This is an observation, not proof of a shared root cause.
  After fixing trace-buffer mappings, one run completed procfs and the small
  pipelines but stalled in the 70,000-byte builtin transfer. A fresh retry
  stopped after echoing only `exe` from the inheritance command, before it
  executed. The focused procfs acceptance case passes on both architectures;
  the combined smoke remains enabled with unchanged input and timeouts.
  A debugger capture during a subsequent login stall found a CPU in exit
  cleanup/preemption with `satp` still referencing a root lacking the kernel
  mapping. That CPU repeatedly faulted fetching the trap vector while holding
  the scheduler lock; the other three CPUs spun on that lock. Exit previously
  freed the process page tables before switching away from them. Exit now
  calls an architecture-specific active-root detach before freeing user memory
  or tables, with hosted regression coverage for active and non-current exits.
  See [address-space detachment](architecture/fork.md#address-space-detachment-on-exit).
  The complete hosted-I/O smoke passes six consecutive RISC-V runs, including
  the 70,000-byte pipe transfer, and passes on AArch64. Input and timeouts are
  unchanged. Scheduler, process, and CPU suites pass 237 tests. This fixes the
  captured exit-lifetime defect; any future stalls still need their own runtime
  evidence rather than assuming the same cause or increasing timeouts.
- [x] Start the safe Rust EXT2 migration with a checked volume reader and a
  shared `hunde::block` native adapter. Cover disk geometry, paths, directory
  records, direct/single/double-indirect and sparse reads, device failures,
  native transfer bounds, and real generated images. Active mounts remain on C.
  See [Rust EXT2 migration](architecture/io-driver-hosts.md#rust-ext2-migration-volume-reader-slice).
- [x] Add Rust EXT2 existing-file overwrites and growth, block-bitmap allocation
  across groups, direct/single/double-indirect pointer updates, sparse-gap
  zeroing, and primary free-block/inode-sector accounting. Stage each write
  before commit, restore attempted blocks after an I/O failure, and refuse
  further volume I/O if rollback fails. This is not crash recovery or journaling.
  Cover real-image write/remount, space exhaustion, metadata protection, every
  staging-read/commit-write failure, partial writes, and failed rollback.
  See [existing-file mutation](architecture/io-driver-hosts.md#existing-file-mutation).
- [x] Add Rust EXT2 inode allocation, file creation, and `mkdir`, including
  directory growth, `.`/`..`, link counts, and free-inode/used-directory
  accounting. Reuse staged mutation and rollback; cover failure injection,
  multi-group allocation, legacy directory records, and real-image remounts.
  See [file and directory creation](architecture/io-driver-hosts.md#file-and-directory-creation).
- [x] Add Rust EXT2 file unlinking, empty-directory removal, and reclamation
  of inode and direct/single/double-indirect block allocations on final unlink.
  Maintain existing hard-link counts and directory accounting; reuse staged
  rollback and cover failures, corruption, multiple groups, and real-image
  remounts. Parent directory blocks are retained for reuse.
  See [removal and reclamation](architecture/io-driver-hosts.md#removal-and-reclamation).
- [x] Add Rust EXT2 regular-file hard-link creation, reusing directory insertion
  and staged rollback. Reject directory links, duplicate/invalid names, reserved
  or unallocated targets, unsupported layouts, and link-count overflow. Cover
  directory growth, failures, remounts, and final-link reclamation.
  See [hard-link creation](architecture/io-driver-hosts.md#hard-link-creation).
- [x] Add Rust EXT2 owner-checked regular-file handles with independent offsets,
  access modes, explicit close/session cleanup, and deferred final-unlink
  reclamation until the last open description closes. Keep failed cleanup
  retryable; test stale/wrong-owner handles, failures, and real-image persistence.
  See [open files and deferred reclamation](architecture/io-driver-hosts.md#open-files-and-deferred-reclamation).
- [x] Add Rust EXT2 shared-description inheritance through the existing grant
  registry: PID-bound commit, access-preserving single-use claim, shared offsets,
  owner-isolated close/release, and committed-grant retention across source
  cleanup. Keep final-grant abort cleanup retryable after I/O errors and test
  limits, failed cleanup, and real-image handoff/final reclamation.
  See [inherited open descriptions](architecture/io-driver-hosts.md#inherited-open-descriptions).
- [x] Add the Rust EXT2 request adapter for an explicitly mounted volume,
  reusing `IoComponent`, request/reply types, credential callbacks, and existing
  handles/grants. Check ancestor search, owner/group/other permissions, sticky
  removal, setgid directory creation, seeks, directory batches, and fallible
  disconnect cleanup. This does not yet load an EXT2 Wuff or switch mounts.
  See [EXT2 request adapter](architecture/io-driver-hosts.md#ext2-request-adapter).
- [x] Add versioned per-device block-host callbacks and fallible EXT2 Wuff
  initialization, preserving ABI-v4 components. Build/install `ext2fs.wuff` for
  both architectures and expose the approved filesystem loader. Keep block
  capabilities alive through component disposal, and refuse disposal while
  owners/grants remain. This does not yet configure a live EXT2 endpoint.
  See [EXT2 Wuff mount boundary](architecture/io-driver-hosts.md#ext2-wuff-mount-boundary).
- [x] Expose Rust EXT2 create/exclusive opens, write-only truncation, and append
  through the request adapter. Preserve inode identity and shared offsets during
  truncation, check permissions/ownership before mutation, and keep failed writes
  retryable. Cover handle exhaustion, sparse-block reclamation, and rollback.
  See [EXT2 request adapter](architecture/io-driver-hosts.md#ext2-request-adapter).
- [x] Support Rust EXT2 read/execute opens for the kernel executable loader.
  Enforce directory search and selected owner/group/other read/execute permissions;
  even root requires an executable mode bit. Preserve execute/read/write access
  through inheritance and reject execute opens that would create a missing file.
  Exercise an actual dynamically linked command loaded from the hosted volume.
  See [EXT2 request adapter](architecture/io-driver-hosts.md#ext2-request-adapter).
- [x] Dispatch the existing hard-link request through the shared C/Rust/Wuff
  adapters and Rust EXT2, preserving both bounded paths and namespace rejection
  of cross-mount links. Add recursive removal with per-entry permissions, sticky
  checks, bounded traversal/cycle detection, and normal open-file retention.
  See [EXT2 request adapter](architecture/io-driver-hosts.md#ext2-request-adapter).
- [x] Reuse the existing write-through block cache in the native Rust block
  client, including scan-resistant reads, resident updates/rollback, diagnostics,
  and reclaimable growth cleanup. Initialize fresh cache storage in place rather
  than returning a large aggregate on a host fibre stack. See
  [hosted EXT2 caching](architecture/io-driver-hosts.md#hosted-ext2-caching).
- [x] Admit the root mount and order bootstrap publication only after root
  device readiness and hosted endpoint initialization. Preserve `/sbin`, `/boot`,
  and `/boot/config`, and prevent simultaneous C/Rust ownership of the root disk.
  Unit tests cover root-first ordering, failed initialization/publication, and
  duplicate C/Rust configuration rejection. See
  [configured hosted EXT2](architecture/io-driver-hosts.md#configured-hosted-ext2-volumes).
- [x] Switch the standard QEMU root provider after filesystem parity and boot
  regression coverage pass on AArch64 and RISC-V. Defaults now select `ext2.wuff`
  for `/`; fresh-image boots reach Bash, load dynamically linked commands, and
  pass filesystem/shutdown acceptance on both architectures. Bootstrap `/sbin`,
  `/boot`, and `/boot/config` remain unchanged. See
  [hosted root acceptance](architecture/io-driver-hosts.md#hosted-ext2-file-watches).
  - [x] Expose existing `IO_REQUEST_WATCH_FILE` and `IO_REQUEST_WAIT_FILE_CHANGE`
    through the shared Rust adapter and hosted EXT2. Preserve generation-based
    changes, retained waits, disconnect cleanup, and cooperative host progress.
    Shared ABI v5 preserves typed generations/events and bounded native batches.
    Waits allow the owning process on its dedicated Channel, matching C, while
    registration and data operations remain session-bound. See
    [hosted EXT2 file watches](architecture/io-driver-hosts.md#hosted-ext2-file-watches).
  - [x] Separate write access from explicit truncation throughout libc and the
    native/Rust I/O interfaces. A plain write-only open must preserve contents;
    `O_TRUNC` must truncate the existing inode rather than remove/recreate its
    path. Preserve hard links, open descriptions, and file-watch continuity.
    Both architectures pass the `rm -P` non-destructive check, in-place hard-link
    and open-descriptor checks, and tail-follow through POSIX redirection. See
    [EXT2 request adapter](architecture/io-driver-hosts.md#ext2-request-adapter).
- [ ] Investigate rejected directory entries in the previously reused QEMU
  root images. The Rust decoder rejects full root enumeration while fresh
  images boot successfully. Corruption versus an overly strict Rust decoder
  has not been established; compare the rejected records with independent EXT2
  tooling before choosing a fix. Do not silently replace user images.
- [x] Mount a separately configured EXT2 Wuff volume through authenticated
  mount control after secondary block-driver readiness. Use one shared host
  scheduler, a fibre per volume, Channels and `hunde-puffer`, with duplicate
  hosted device/endpoint checks. This secondary-volume slice preceded the
  standard QEMU root switch above.
  See [configured hosted EXT2](architecture/io-driver-hosts.md#configured-hosted-ext2-volumes).
- [x] Complete live hosted EXT2 file/directory acceptance on AArch64 and RISC-V:
  read the seeded file, list directories, and create/remove a directory through
  the actual Wuff and block callbacks. Decode transfer flags only for read/write
  requests; keep open modes, seek origins, remove flags, and inheritance flags
  in their operation-specific fields. Boundary regressions cover the original
  file-open failure and preserve unknown transfer bits for backend validation.
- [x] Make native block/Channel waits cooperative with the host scheduler.
  Serialize the cached buffer-manager connection/transaction and default-client
  Wuff-loading I/O, wake local ownership contenders on release, and retain
  process waits outside the scheduler. Delayed-queue/shared-client regressions
  and concurrent EXT2/socket/pipe smoke coverage check progress, not bounded
  latency. See [native wait ownership](architecture/rust-userspace.md#one-scheduler-multiple-fibres).
- [x] Fix intermittent RISC-V disk-operation stalls from lost PLIC enable-word
  updates during concurrent kernel masking and userspace rearming. Keep context
  enables kernel-owned; mask/rearm through per-IRQ priorities. Supply the
  unshifted priority-page base on every physical hart, without mapping enable
  words into the manager. Register/registration regressions and 100-cycle live
  mkdir/list/remove acceptance cover both architectures. See
  [interrupt acknowledgement flow](architecture/interrupt-service.md#interrupt-and-acknowledgement-flow).
- [x] Move bootstrap EXT2 `/boot/config` into the host only after early host
  startup can load bootstrap components without depending on that mount.
  Mama now initializes hosted `ext2.wuff` before namespace publication and boot
  handoff, loading from `/sbin` without an application root or `/boot/config`.
  Native tests cover ordering and failed device readiness, initialization, and
  publication. Root/shutdown, hosted filesystem/procfs, and combined I/O smoke
  tests pass on AArch64 and RISC-V with no standalone C EXT2 process. `/boot`
  sysinfofs is also hosted by the Rust snapshot component; physical-platform acceptance
  of the common hosted SD-partition bootstrap path is still needed.
  See [bootstrap services](architecture/bootstrap-services.md).
- [x] Complete acceptance of the standard QEMU `/mnt/channelio-ext2` migration
  to the existing hosted `ext2.wuff` implementation on AArch64 and RISC-V.
  All three standard QEMU configurations select the hosted driver. Root/shutdown,
  hosted filesystem/procfs, and concurrent socket/pipe/memfs smoke tests pass on
  AArch64 and RISC-V. Checks cover seeded reads, directory creation/removal,
  truncate/write/read. That slice retained one C EXT2 process for
  `/boot/config`; the subsequent bootstrap migration removes it.
  See [direct hosted dispatch](architecture/io-driver-hosts.md#direct-hosted-dispatch).
- [ ] Migrate the remaining Pi UEFI and Milk-V EXT2 fstab selections after
  target-specific bootstrap and block-device-access validation. Keep those
  root/data selections unchanged until then; the common Mama bootstrap mount
  request now uses hosted EXT2, with physical-platform acceptance still needed.
- [x] Move filesystem namespace ownership into the Rust I/O host with an explicit
  root slot and mounted overlays, preserving bootstrap mounts, authenticated
  registration, longest-component-prefix routing, directory pagination, and
  cross-mount link rejection. Mama starts the namespace before `/sbin`; another
  fibre loads components after the archive is mounted. Retire standalone C
  namespace startup/build while retaining its endpoint for existing clients.
  The obsolete C service source, launcher, tests, and startup-path reference
  are removed; namespace tests live alongside the Rust implementation.
  Process-local namespace routing is implemented for the native Wuff loader,
  with scoped callback ownership, reentrancy checks, fork reset, and unchanged
  external Channel routing. Root filesystem/shutdown and hosted-component
  acceptance pass on AArch64 and RISC-V, including one I/O host and no standalone
  namespace process. Kernel self-Channels remain forbidden.
  See [root namespace ownership](architecture/io-driver-hosts.md#root-namespace-ownership).
- [ ] Move the common application endpoint, namespace, mounts, handle ownership,
  filesystems, sockets, streams, pipes, terminals, and information filesystems
  into one host with local dispatch between its components.
  Application routing, namespace, and mount selection now reside in the host;
  hosted handle dispatch is implemented; terminal/device providers and remaining
  external filesystems are still separate.
  - [x] Complete application-router migration acceptance on AArch64 and RISC-V.
    The Rust host now serves `hund-io-system`, selects streams/sockets/pipes,
    and calls its filesystem namespace locally. Mama no longer starts the C
    router; console output selection uses the shared boot hardware adapter.
    The obsolete C router source, tests, and startup/build references are removed;
    routing tests live alongside the Rust host implementation. The endpoint
    name remains unchanged for clients.
    Root/shutdown, hosted filesystem/procfs, and combined socket/pipe/memfs I/O
    smoke tests pass on both architectures without a standalone C router.
    See [application router ownership](architecture/io-driver-hosts.md#application-router-ownership).
  - [x] Complete direct hosted-dispatch acceptance on both architectures.
    Filesystems, sockets, and pipes use host-owned handles, local component jobs,
    PID/session checks, opaque inheritance tokens, batched cross-component file
    waits, and worker-side disconnect cleanup. Preserve existing directional
    shared buffers with bounded owned staging across fibre waits. Retain
    external-provider redirects only until authenticated adapters or provider
    migration remove those boundaries; do not forward using host privileges.
    Root/shutdown, hosted EXT2/procfs, and combined socket/pipe/memfs smoke tests
    pass on AArch64 and RISC-V. The combined RISC-V smoke passes three consecutive
    runs with background EXT2 reads and unchanged input/timeouts.
    Host alias lifetime handling keeps
    backend ownership until the owner's last alias closes, with real-pipe
    regressions for independent owners, EOF, and grant cleanup.
    See [direct hosted dispatch](architecture/io-driver-hosts.md#direct-hosted-dispatch).
  - [x] Consume memory-pressure service events in the Rust host and reclaim
    native block-cache growth on each volume's owning worker. LLDB captured an
    unconsumed normal-pressure event in the spinning host while a Bash child
    remained ready on the same CPU; queue wait correctly refused to park with
    that event pending. Typed event polling drains normal notifications and
    coalesces low/critical pressure without reclaiming across suspended I/O.
    Regressions cover event decoding/draining, worker ownership, busy/failed
    retries, and reclaiming cache growth without closing the device.
    See [direct hosted dispatch](architecture/io-driver-hosts.md#direct-hosted-dispatch).
- [ ] Keep filesystem metadata, filesystem data, network policy, and any shared
  raw-block cache in the I/O host. Keep only hardware queue state and
  device-specific metadata in the driver host.
- [ ] Preserve authenticated sessions, stale-handle rejection, blocked-request
  progress, process-exit cleanup, and the existing native and POSIX I/O APIs.
- [ ] Extend existing `hunde-puffer` leases and permissions across the required
  application, I/O-host, and driver-host participants without adding another
  bulk-data transport or assuming universal zero-copy operation.
- [ ] Compare the consolidated implementation against the baseline measurements
  on AArch64 and RISC-V before removing the old service processes.

#### Open Design Questions

- [ ] Define how boot-critical modules are supplied without depending on the
  filesystem or device stack that they initialize.
- [ ] Decide which current service endpoint names remain temporarily available
  during migration and the evidence required to remove each alias.
- [ ] Define recovery of sessions, buffers, caches, devices, and outstanding
  operations after either host exits; current service restart recovery remains
  deferred.

## Shared Libraries

### Dynamic ELF Loading

#### Loader Boundary And Format

- [x] Use standard ELF `ET_DYN` objects with the `.wuff` suffix, an ABI-major
  runtime `SONAME` such as `libc.wuff.1`, and linker-only `.so` compatibility
  aliases where conventional toolchains require them.
- [x] Define the userspace dynamic-loader and host-module boundary without
  making filesystem, dependency, or module policy a kernel concern. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [x] Produce PIC `libc.wuff.1` sysroot artifacts for AArch64 and RISC-V while
  retaining static linking as the default. RISC-V `.wuff` links use LLVM LLD
  because its bare-metal GNU linker does not support shared output.
- [x] Add aligned and exact-address userspace mappings, page-aligned permission
  finalization without RWX, rollback unmapping, and architecture instruction
  synchronization required by the userspace loader. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [x] Introduce separate virtual-mapping metadata for page-aligned permission
  changes within dynamic allocations while retaining one ownership record per
  backing allocation. Fork preserves split permissions and cleanup frees the
  backing allocation once. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [x] Produce shared objects and an AArch64/RISC-V dynamically linked PIE smoke
  executable with the project toolchains. The executable records
  `/sbin/wuff-ld`, depends on `libc.wuff.1`, and build-time checks constrain its
  relocations and unsupported TLS or initialization metadata. The initial
  dynamic tags, symbol visibility, and `/usr/lib` search rules are documented in
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [ ] Complete the reusable userspace `ET_DYN` loader for dynamically linked
  programs and host components, including interpreter startup, constructors,
  and any additional reviewed AArch64 and RISC-V relocations they emit. Extend
  the contract when AMD64 support begins.
- [x] Implement the dependency-free loader slice: validated ELF64 load regions,
  injected and syscall-backed mappings, SysV export lookup, the AArch64 and
  RISC-V relocations emitted by libc, RELA PLT processing, final RX/RO/RW and
  GNU RELRO permissions, and complete mapping rollback. Unsupported
  dependencies, imported symbols, TLS, and single-object interpreters are
  rejected.
  See [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [x] Add bounded breadth-first `DT_NEEDED` graph loading with injected image
  lookup, cycle deduplication, root-first global symbol scope, strong and weak
  import binding, visibility handling, and graph-wide rollback. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [x] Add the initial filesystem library search policy: accept plain dependency
  names, search only `/usr/lib`, bound file images, release provider-owned image
  buffers after preparation, and expose a validated root executable entry. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [x] Implement the static `/sbin/wuff-ld` program with private startup that
  reopens the preserved root path, loads its dependency graph, and transfers the
  original startup ABI without claiming inherited I/O. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [x] Redirect accepted dynamic exec requests through `/sbin/wuff-ld` while
  preserving the original executable path, argv, envp, working directory,
  inherited I/O payload, and process identity. The kernel validates the fixed
  interpreter contract, asynchronously reloads the static interpreter through
  `hund-io-system`, and leaves dependency loading and relocation in userspace.
  Add initialization metadata or further relocations only when reviewed program
  inputs emit them. `LD_LIBRARY_PATH`, RPATH, and RUNPATH remain deferred.
- [x] Dynamically link the first ordinary command, `/usr/local/bin/cat`, against
  `/usr/lib/libc.wuff.1` on AArch64 and RISC-V. Keep `/sbin/wuff-ld` and early
  bootstrap services statically linked so dynamic loading does not introduce a
  dependency cycle during service and filesystem startup.
- [x] Migrate every disk-root command to the shared PIC/PIE build and runtime
  dependency on `/usr/lib/libc.wuff.1`: `cat`, `cp`, `env`, `fs-latency`,
  `grep`, `kill`, `ln`, `ls`, `mkdir`, `mv`, `ps`, `rm`, `shutdown`, `tail`, and
  `write`. Keep embedded programs, drivers, bootstrap services, the static
  interpreter, and Bash outside this slice. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [x] Dynamically link Bash against `/usr/lib/libc.wuff.1` on both architectures,
  with build-time validation of the interpreter, sole dependency, unsupported
  metadata, and reviewed relocation set. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).

#### Mapping And Runtime Lifecycle

- [ ] Map load segments with final write/execute permissions, share immutable
  pages where appropriate, and reject malformed or unsupported dynamic objects.
- [ ] Support initialization and finalization arrays, thread-local storage needed
  by the selected libc/runtime, dependency reference lifetimes, and process-exit
  cleanup.
- [ ] Add unit coverage for parsing, symbol lookup, relocation, and failure
  rollback, plus full-system tests running two programs against one shared
  library.
- [x] Add hosted unit coverage for the dependency-free parser, export lookup,
  relative relocation, permission finalization, unsupported metadata, and
  rollback, plus cross-linked AArch64 and RISC-V `.wuff` smoke objects.

### Loadable Host Components

#### Component ABI And Lifecycle

- [x] Add the host-module loader entry points: load a dependency graph while
  rejecting `PT_INTERP`, load a module from the filesystem while releasing its
  temporary image, and resolve required exports from the root object only as a
  `Result(WuffSymbol)`. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [ ] Define the single-entry, versioned C ABI and typed host-operation tables
  used by both I/O and hardware components. The common lifecycle entry, tagged
  Result records, logging/fatal host operations, descriptor, and C/Rust layout
  assertions are implemented; the I/O and hardware-specific operation tables
  remain. Do not expose language-specific objects, unwinding, or private
  collection layouts across the boundary.
- [ ] Load trusted components during host startup, validate ABI size and version,
  establish final segment permissions, initialize only after the complete object
  graph succeeds, and roll back partial loads.
- [ ] Keep initially loaded components resident until host exit. Decide whether
  runtime unloading is required only after defining callback, fibre, request,
  IRQ, shared-buffer, and DMA quiescence.

#### Minimal Rust Components

- [x] Pin Rust 1.99.1 and build the first dependency-free `#![no_std]` component
  with the built-in AArch64 and RV64IMAC bare-metal targets, the C component ABI,
  PIC, and aborting panic behavior. A HundeOS `std` port and custom Rust target
  are not prerequisites. See
  [Dynamic linking and host modules](architecture/dynamic-linking.md).
- [ ] Provide the minimal runtime boundary: host-backed allocation when `alloc`
  is used, fatal panic handling, required memory/integer/atomic symbols, and ABI
  adapters around unsafe MMIO, DMA, and shared-buffer access.
  Allocation, fatal panic handling, PIC `core`/`alloc`, the socket Channel-service
  adapter, and multi-fibre scheduling with joinable tasks are implemented. Hardware
  MMIO/DMA wrappers and broader Rust Channel-client APIs remain.
- [ ] Keep unwinding, TLS, operating-system threads, and Rust-language ABI values
  outside the initial contract.
- [ ] Add a separate cross-language smoke test for C-to-Rust calls, Rust-to-host
  callbacks, ABI layout, optional allocation, supported atomics, ELF relocation,
  final permissions, and loaded execution on AArch64 and RISC-V.

## Process Management

### Event-Driven Realtime Services

Status: `hund-unterbrict` is a realtime-priority service without CPU
reservation. It parks when it has no work. Delegated IRQ publication makes a
parked manager runnable and requests an immediate reschedule on its owning CPU,
using an IPI for remote wakeup. Scheduler ticks remain enabled on every CPU.

- [x] Promote the registered interrupt manager to the highest runnable process
  class without changing its CPU ownership.
- [x] Wake a manager parked in a queue wait when a delegated IRQ is published,
  with direct local preemption or a remote reschedule IPI.
- [x] Allow ordinary admission, preferred selection, ordinary selection, and
  speculative waiting on every CPU while the manager is parked.
- [ ] Measure IRQ-publication-to-manager-dispatch latency under CPU and I/O load.
- [ ] Audit shared locks and service dependencies for priority inversion and
  unbounded work in the interrupt-manager path.
- [ ] Define whether an active realtime handler needs a bounded run-to-block
  budget if measurements show harmful timer preemption during IRQ processing.
- [ ] Define failure cleanup for a crashed interrupt manager while keeping its
  delegated IRQ sources masked.

### Fork Support

Status: required by stage 5 of the active Bash milestone. Implementation follows
the cross-build, non-local control-flow, signal, and static-link stages above.
The staged implementation plan is documented in
[Fork and copy-on-write](architecture/fork.md). The first functional increment
uses eager private-memory copies; COW follows without changing the syscall or
I/O inheritance contract. The first implementation keeps the child unpublished
until libc commits service-side descriptor grants, then the child opens fresh
authenticated sessions and claims those grants before `fork()` returns.
Ordinary filesystem and stream descriptors inherit by shared underlying
resource rather than by reopening paths; hosted pipes now use the same grant
protocol. Sockets, close-on-exec, and
committed-grant cleanup for children that die before claiming remain open work.
Architecture details are documented in
[Fork and copy-on-write](architecture/fork.md).

#### Process Creation

- [x] Add a typed fork syscall and architecture-specific context result helpers;
  return the child PID to the parent and zero to the cloned child.
- [x] Implement transactional eager copying of private allocations into a fresh
  child page table, recreating kernel mappings and excluding IPC, device, and
  in-flight process state.
- [x] Keep the child unpublished and non-runnable until memory and inherited I/O
  references commit; roll back every allocation, mapping, grant, and scheduler
  insertion on failure.
- [x] Reject service processes, MMIO mappings, shared allocations, and live IPC
  state atomically in the initial eager-copy implementation. Queue wait and
  doorbell mappings are reset rather than inherited as live wait state.

#### Copy-On-Write

- [ ] Choose extent- or page-based backing ownership consistent with allocator
  release semantics, then add reference counting and memory-accounting support.
- [ ] Add architecture-neutral mapping inspection, read-only sharing, page
  replacement, and translation-invalidation operations with AArch64 and RISC-V
  implementations.
- [ ] Normalize architecture write-fault information and recover only tracked COW
  faults; preserve process aborts for all other user faults.
- [ ] Replace eager copying with COW for writable private pages and cover writes,
  multiple fork generations, exit, exec, `sbrk`, memory pressure, and rollback.

#### I/O Subsystem: Shared File Descriptors

- [x] Give the child its own descriptor table with the parent's fd numbers for
  ordinary libc descriptors, including stdin (`0`), stdout (`1`), and stderr
  (`2`).
- [x] Have final resource-owning services authorize child-PID-owned references to
  the same open resources. Copying a parent-owned handle number alone must not
  grant access.
- [x] Share open-resource state for implemented resource kinds: filesystem open
  file offsets and stream destinations.
- [x] Reference-count shared open resources while keeping descriptor lifetimes
  independent for filesystem and stream handles. Closing one process's
  descriptor must not invalidate another process's reference; release the
  resource when its final reference is gone.
- [ ] Release each process's references on exit, including crashes and missed
  closes, using reliable process-exit notification and service cleanup. Parent
  session loss releases uncommitted grants; committed grants are retained until
  child claim and still need orphan cleanup if the child exits first.
- [x] Add prepare, child-PID-bound commit, single-use claim, and abort operations
  to final resource-owning services; keep `hund-io-system` as a router.
- [x] Extend descriptor inheritance to hosted Rust pipes using authenticated,
  child-PID-bound, single-use grants. See [pipes](architecture/io-system.md).
- [ ] Extend descriptor inheritance to sockets.
- [ ] Release `FD_CLOEXEC` descriptors reliably after a successful exec overlay.

#### IPC Sessions

- [x] Establish the child's own authenticated Channel session to
  `hund-io-system`; do not duplicate the parent's Channel queues, cursors, or
  in-flight request state.
- [x] Define how inherited descriptors are associated with that new session
  while retaining their shared underlying resources. The child fork-return path
  must discard copied session state, open fresh Channels, and claim committed
  service grants before application code can run.
- [x] Choose the coordinator that keeps the child non-runnable during grant
  commit and guarantees rollback if the parent exits mid-transaction. Libc
  prepares and commits I/O grants around the staged kernel fork calls, then
  resumes the unpublished child only after commit succeeds.

Architecture context: [Userspace I/O system](architecture/io-system.md) and
[Channels and event queues](architecture/channels-and-event-queues.md).

## I/O and Shared Memory

### Bounded Idle Waiting

#### Userspace Polling and Kernel Parking

- [x] Fix premature peer-doorbell revocation observed in RISC-V
  `hundekanal-warter` and `embedfs`: a push/pop could pass its open check before
  remote close unmapped the alias. Keep the survivor's mappings until its own
  detach and retain backing across process exit/exec using kernel-only reference
  accounting. Cover multiple aliases, kernel clients, private metadata, and
  failed final-free retry, without additional signalling syscalls. See
  [queue mapping lifetime](architecture/queues.md#initial-queue-api).
- [x] Run ready fibres, poll queue state in userspace, then park the process with
  its observed process-doorbell generation. Under the scheduler lock, recheck the
  live doorbell and kernel-owned readiness state before committing the park.
- [x] Replace comparison-mode busy polling with userspace `WFE`/`SEV` waits.
  Complete publication before signalling; signal after device IRQ dispatch and
  set a local event on context restoration. Timer preemption remains enabled.
- Kernel queue waits now use the process doorbell generation and authoritative
  kernel readiness checks. Normal waits use an indefinite park; the diagnostic
  probe retains bounded 1-to-100-tick timeouts. See
  [queue waits](architecture/queues.md#bounded-waits).
- User comparison on the SDHCI trace target reports substantially better
  responsiveness with kernel parking disabled and tracing unchanged. This
  implicates the parking path, but does not isolate its expensive stage.
- [ ] Measure queue publication to wake detection and actual userspace resumption
  in a controlled diagnostic run, separating activity wakes from timeouts.
  Include syscall entry-to-park and selection-to-return overhead. Current
  counters omit these intervals and publication timestamps; average parked
  duration is not message-delivery latency. A queue push now uses an authenticated
  data-available syscall and reschedule IPI for a changed parked receiver;
  quantify that path separately from message service time.
- [x] Add an isolated timeout probe that parks only itself and compares complete
  call duration with fresh kernel counters. On the SDHCI trace target, 1- and
  10-tick waits left about 18-19 ms per call outside parked/runnable accounting.
  See [measurements and limitations](architecture/queues.md#sdhci-trace-measurement).
- [ ] Attribute that remaining entry/completion overhead, including per-park
  allocation/free and tracing costs, before changing the wait implementation.
- [x] Expose aggregated wake reasons, parked durations, and wake-to-selection
  delays through [procfs queue-wait diagnostics](architecture/process-registry-and-procfs.md#queue-wait-diagnostics).
- [x] Expose userspace UART bytes/IRQs, empty reads, backend read traffic,
  Channel publications, and unchanged event-wait returns through
  [IPC activity diagnostics](architecture/process-registry-and-procfs.md#ipc-activity-diagnostics).
- [x] Retain a bounded, lossy kernel trace in memory and expose a validated,
  non-destructive snapshot through `/proc/ktrace` for hardware with no second
  UART adapter. See [kernel trace snapshots](architecture/driver-tracing.md#kernel-trace-snapshots).
- [ ] Measure idle-console deltas versus typing after the stable-publication
  change, separating real queue/kernel wakes from spurious `WFE` returns.

#### Proposed Shared Scheduling Hint

- [x] Introduce a per-process shared userspace scheduling record: idle queue waits
  publish the observed process-doorbell generation for scheduling diagnostics and
  dependency hints. Kernel parking remains a syscall and uses kernel-owned live
  readiness state. Existing fibre dispatchers run ready work before entering this
  path; dependency-token propagation is separate.
- Proposed constraints: aggregate readiness across the process, retain timer
  preemption, and avoid per-wait allocation and deferred-syscall completion.
- [x] Replace per-queue userspace snapshots and the separate kernel-written wake
  generation with one process doorbell generation. Kernel queue waits record the
  submitted generation only after checking all owned queues and kernel
  notifications under the scheduler lock. Timer and voluntary-yield scheduling
  paths compare that generation and current kernel state. Treat the shared
  userspace publication as an untrusted hint only. See
  [queue waits](architecture/queues.md#bounded-waits).
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
- [x] Keep wakeup cascades live when downstream work arrives after a service has
  parked. Successful pushes authenticate the receiver and trigger an immediate
  kernel readiness check; consumption-only progress is detected through the
  process doorbell and authoritative state checks on timer or voluntary-yield
  scheduler entry. Fibre-idle publications remain advisory.

#### Proposed Dependency-Ordered Waiters

- [x] Prioritise every runnable process with unread incoming queue/Channel messages
  above processes without messages, regardless of process class. This includes
  direct requests, replies, and kernel service notifications. Existing
  class/last-run ordering breaks ties; consuming messages removes the priority.
  Implemented using existing kernel mappings, with no per-message syscall. See
  [receiver scheduling priority](architecture/queues.md#receiver-scheduling-priority).
- [x] Make live unread transport on any connection owned by a process override
  an unchanged userspace wait publication. The scheduler checks authoritative
  queue state rather than trusting a user-selected wait set. Regression tests
  cover both initial selection and timer retention when the unread connection
  was absent from the preceding userspace scan. See
  [receiver scheduling priority](architecture/queues.md#receiver-scheduling-priority).
- [x] Add outstanding-request identity and parent propagation independently of
  unread-message state. See [Channel tickets and preferred scheduling](architecture/dependency-scheduling.md).
- [x] Trace client publication, server acceptance, reply publication, and
  client-visible completion for Channel requests, retaining parent tickets so
  nested I/O chains can be reconstructed. Records are batched through lossy
  per-process shared rings into the memfs-backed `/tmp/io-trace.log`. See
  [Channel latency tracing](architecture/driver-tracing.md#channel-latency-tracing).
- [x] Add ticket-correlated kernel timestamps for publication-to-eligibility and
  eligibility-to-selection/dispatch to the Channel latency trace.
- [ ] Re-enable Channel latency tracing through `SystemSettings` when latency
  measurement is needed again. The setting is disabled by default, so
  `hundeschule` does not start `hund-verfolgen` and participants do not register
  trace rings. Ordinary driver diagnostics now use the kernel syslog syscall and
  remain independent of this setting.
- [ ] Tune scheduler residency using the measured Channel latency breakdown.
- [x] Retain the final trace records of short-lived processes by synchronously
  draining a bounded snapshot of every registered source before acknowledging
  the exiting process's drain request.
- [x] Record the exact monotonic publication timestamp in each directional Queue data area
  and emit an `IO-HOP` record at consumption, allowing `fs-latency` to report
  publish-to-consume percentiles for each immediate client/server pair.
- [x] Preserve absolute Channel lifecycle timestamps in completed trace records
  and correlate parent and child tickets in `fs-latency`. Report parent
  pickup-to-child-publication, child roundtrip, and final-child-completion-to-parent-
  reply separately, including serial sibling requests.
- [ ] Make high-volume Channel tracing preserve enough records for balanced
  per-hop sample counts without allowing diagnostics to block I/O. Current
  rings are deliberately lossy, so fan-out-heavy filesystem workloads can
  retain different sample counts at successive hops.
- Keep unread transport work distinct from an outstanding request: a server may
  consume a request and then wait on another service. Account for direct
  client/service stages and cross-CPU observation latency. Queue inspection must not require kernel parsing of I/O
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
  gang scheduling; overlapping chains may interleave. The preferred queue is
  resorted on every scheduler refresh so actionable receivers and deeper
  validated dependency chains displace older, less important entries. See
  [Channel tickets and preferred scheduling](architecture/dependency-scheduling.md).
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
  direct Channel control metadata. Publish durable atomic records in
  write-owned queue pages; validate endpoint ownership, ancestry, and live
  generations. Kernel Channel transports use `QUEUE_KERNEL_PID` while retaining
  the real originating process for dependency roots. Preserve request context
  across fibre switches. Reject cycles, discard stale/acknowledged links, and
  remove queued processes on exit.
- [x] Remove `hundekanal-warter` from established Channel traffic. The warden
  authenticates an `OPEN`, asks the kernel to delegate the client directly to
  the service, returns that connection, and discards the temporary control
  state. Requests and replies use directional Queue data and peer doorbells;
  the kernel owns connection close and process-exit cleanup. See
  [Channels and event queues](architecture/channels-and-event-queues.md).
- [x] Allow preferred selection on another started core after the outgoing
  context-switch tail is safe. Do not steal running or kernel-blocked processes.
- [x] Treat the idle process as the final scheduling choice. When a CPU has no
  preferred, newly admitted, or ordinarily runnable process, select one of its
  unchanged userspace waiters. Rank local candidates by validated dependent-chain
  impact, recent wait-adjusted activity, and longest time since dispatch. Do not
  migrate this fallback or override real kernel blocking. Expose speculative
  selections through procfs for RISC-V versus AArch64 measurement.
- [x] Admit every newly created process through one shared scheduler queue. The
  next CPU making a scheduling decision claims an admitted process into its
  local run table; after that, the process retains CPU affinity unless the
  dependency-aware preferred path has a priority reason to migrate it. CPU
  scheduler lookup is keyed by the architecture CPU ID rather than indexing a
  dense array, and process-registry publication uses a separate compact slot.
- [x] Add event-triggered queue notification. A successful push
  publishes shared progress first, authenticates the live connection in the
  kernel, and wakes a changed parked receiver. A remote receiver gets one
  coalesced reschedule IPI; on a single-CPU system, the notifying process yields
  through normal scheduler selection. Pop only publishes consumption and does
  not announce new schedulable work. A park-time recheck closes the
  notify-before-park race. See [queue waits](architecture/queues.md#bounded-waits).
- [ ] Evaluate adaptive residency using the resulting latency measurements.
- Open scope: exclusive cluster budgets/arbitration and context integration for
  custom asynchronous dispatchers outside the common I/O/block wrappers.

- [x] Retire shared per-queue observation arrays. The shared wait control now
  publishes only the process doorbell generation and timing diagnostics; it is
  advisory. Kernel queue waits use a syscall that records the generation and
  checks kernel-owned queues and notifications before parking and again on timer
  or voluntary-yield scheduler entry.
- This foundation gates existing channel hops on actual transport progress.
  It does not yet implement the IRQ token/hop protocol below, stage acknowledgements,
  or operation-failure timeout reporting. See [queue waits](architecture/queues.md#bounded-waits).
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
measurement remain below. See [Shared buffers](architecture/shared-buffers.md).

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
  away from 256/512-byte inline bulk transfers. Remove I/O mediator copies by
  redirecting clients to final services, which allocate the client-facing pair.
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
  cleanup unchanged. See [Shared buffers](architecture/shared-buffers.md).
- [ ] Replace permanent retention with orchestrated completion/quiescence and
  reclamation. Explicit unmount and hot-unplug cleanup are deferred; service
  restarts do not automatically inherit old mappings or replay I/O.
  The QEMU Pi UEFI trace EXT2 mount now targets the EXT2 disk rather than the
  FAT EFI disk, removing its repeated failed-startup buffer consumption. See
  [Userspace I/O system](architecture/io-system.md). Repeated service failures can still
  exhaust retained slots until coordinated reclamation is implemented.
- [ ] Reuse the client's final-service shared buffer through FAT32/EXT2 and
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

### Filesystem Read Latency

Status: the userspace block cache has a fixed 1024-sector base and grows with
reclaimable page-backed chunks while at least half of kernel-managed memory is
free. EXT2 returns those chunks when low-memory service events arrive. The inode
cache remains bounded, and service consolidation remains staged work so its
effect can be measured independently.

- [x] Add a reusable 1024-sector (512 KiB) scan-resistant LRU cache above
  `BlockClient`. Preserve contiguous miss batching, protect blocks after a
  repeat access, update resident sectors after successful writes without
  admitting streaming writes, and expose hit, miss, and eviction counts through
  `/proc/<pid>/ipc`.
- [x] Route EXT2 block reads and writes through the cache. Repeated pathname
  traversal can reuse cached inode-table and directory blocks without another
  block-service request.
- [x] Track total/free heap pages, reserve half of managed memory from
  reclaimable growth, and publish low/critical pressure transitions at 20% and
  10% free memory. Reclaimable allocations register their process for
  asynchronous service events; event delivery occurs after allocator locks are
  released. EXT2 drops dynamic block-cache chunks on pressure while retaining
  the base cache.
- [x] Add a bounded 256-entry EXT2 inode cache using the shared scan-resistant
  cache type. Update it after successful inode writes, invalidate freed inodes,
  and export separate hit, miss, and eviction counters through procfs.
- [x] Consolidate filesystem adapters in the Rust I/O host, removing the separate
  namespace/filesystem-service Channel hop while retaining the application-facing
  I/O protocol and failure handling. The obsolete C namespace service is removed;
  see [direct hosted dispatch](architecture/io-driver-hosts.md#direct-hosted-dispatch).
- [ ] Revisit filesystem realtime scheduling only after cache and consolidation
  measurements identify residual scheduler latency; do not infer realtime
  suitability from service placement alone.

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
[Bus information](architecture/bus-info.md).

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
- [x] Move `/boot/sysinfo` to hosted Rust `sysinfofs.wuff`, replacing the
  standalone bootstrap process. Load typed boot information once through the
  shared `hunde` compatibility layer, cache the formatted read-only contents
  at component initialization, and retain `/boot/config` as an EXT2 overlay.
  Native/Rust tests and all three smoke tests pass on AArch64 and RISC-V
  (RISC-V passed on unchanged retry after the interrupt-manager fault recorded
  under `hunde-treiber-host`). All fields, including `cpu.online_count`, are
  initialization-time snapshots. See
  [cached boot information](architecture/bootstrap-services.md#cached-boot-information).
- [ ] Query `hundebus` through its Channel interface for bus records.
- [ ] Mount the filesystem at `/sys`; `/sys/info` and `/sys/bus/pci` are proposed
  paths, with final layout to be agreed during implementation.
- [x] Delete the kernel `/boot/sysinfo` provider; userspace `sysinfofs` owns it.

## System Startup

### Userspace Bootstrap

Implemented: [hunde-mama bootstrap](architecture/bootstrap-services.md) establishes
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
- [x] Reuse the C archive backend through a safe `hunde` Rust adapter and hand
  `/sbin` to the I/O host after bootstrap mounts are ready. Drain bootstrap
  handles and queued replies before Mama retires the old process; retain the
  kernel-owned archive backing independently of either consumer. See
  [bootstrap services](architecture/bootstrap-services.md).
  The wrapper, internal worker, read-only archive mapping, authenticated
  drain/retirement protocol, fibre-aware local completion, and local file
  transfers are implemented with native/Rust tests. Root/shutdown, hosted
  filesystem/procfs, and concurrent socket/pipe/memfs smoke tests pass on both
  AArch64 and RISC-V, including absence of the retired embedfs process. Libc's
  build-selected export policy keeps the synthetic `_stack` marker private on
  AArch64; both architectures check its absence from runtime exports. Loader
  range validation remains unchanged. See
  [dynamic linking](architecture/dynamic-linking.md#interpreter-integration).
- [ ] Migrate executable transfers to the
  [hunde-puffer shared-buffer design](architecture/shared-buffers.md) tracked above.
  The initial shared-buffer implementation replaces the 256-byte bulk replies.
- [ ] Remove the disk providers' 12 KiB open-file snapshot limit before relying
  on them for larger executables. See
  [Executable loading](architecture/bootstrap-filesystem-handoff.md).
- [ ] Add credential-based executable-open authorization to the FAT32/EXT2
  adapters. They currently reject executable opens rather than allowing an
  unchecked disk-backed spawn.

#### Remaining Kernel I/O Removal

- [x] Remove the unused in-kernel message broker, scheduler message callbacks,
  and unconsumed process-exit broadcasts. Direct scheduler operations and
  shared IPC remain; see [Userspace I/O system](architecture/io-system.md).
- [x] Delete the unused kernel console and stream backends and their UART IRQ
  wiring. Independent kernel logging and panic output remain; see
  [Userspace I/O system](architecture/io-system.md).
- [x] Delete the obsolete kernel socket backend and userspace filesystem
  broker, including unused service operations and wrappers.
- [x] Remove kernel bootfs, EXT2, the unused block cache and memory block device,
  and the kernel mount API. Filesystem watchdogs only launch userspace providers.
- [x] Remove legacy stream-server syscalls, buffer mapping, and server maps.
- [x] Remove userspace file-I/O syscall fallbacks. Route link requests through
  the existing I/O and filesystem Channels; see [Userspace I/O system](architecture/io-system.md).
- [x] Implement hard-link creation in EXT2 through the userspace filesystem
  Channel. Providers without inode-sharing semantics continue to return an
  explicit unsupported error.
- [x] Retire file I/O syscalls, generic kernel filesystem/device routing,
  `kernel/kio` wrappers, and process FD tables. Startup stdio retains terminal
  endpoint metadata only; general userspace handle inheritance remains deferred.
  Fault reports use independent kernel diagnostics. Retired syscall numbers
  remain reserved and fail dispatch. See [Userspace I/O system](architecture/io-system.md).
- [x] Create the separate fibre-based `sdhci_channelio` driver using existing
  Channels and managed buffers, switch SD boot configurations, and retire the
  old SDHCI executable. The hardware path remains read-only polling PIO.
- [x] Remove VirtIO's legacy kernel-broker request/event path, retaining its
  Channel implementation.
- [x] Retire unused `IO__block_read`/`IO__block_write` syscall helpers.
  General fibre infrastructure remains.
- [x] Migrate `channel_blk`/`channel_fs` to BlockServer/BlockClient and delete
  the kernel block broker and private transport. Device discovery metadata and
  capability checks remain in the kernel. See [Userspace I/O system](architecture/io-system.md).
- [ ] Replace the boot disk's size-based identification with an explicit stable
  boot-device identity supplied by the boot environment.

## Architecture

### RISC-V Support

Status: in progress. The agreed first target is RV64IMAC on QEMU `virt`, using
the LP64 soft-float ABI, OpenSBI firmware, supervisor mode, four harts, and Sv39.
The first physical-board target is the original 64 MiB Milk-V Duo using the
CV1800B RISC-V application core and the vendor FSBL/OpenSBI/U-Boot SD boot
chain. QEMU provides a generated device tree and a suitable virtual platform
for architecture bring-up. See the
[QEMU RISC-V virt documentation](https://www.qemu.org/docs/master/system/riscv/virt.html).

#### Milk-V Duo CV1800B

- [x] Produce a reproducible `boot.sd` payload for the vendor SD boot chain.
  The initial bring-up FIT enters supervisor mode at `0x80200000`, writes to the
  DW APB UART at `0x04140000`, and validates the firmware-provided DTB handoff.
  See [Milk-V Duo bring-up](architecture/milkv-duo.md).
- [x] Add the full `milkv_duo` boot platform with a one-hart, 64 MiB Sv39 layout,
  CV1800B PLIC metadata, UART register stride, reserved firmware memory, and
  board DTB parsing without requiring QEMU VirtIO or PCI nodes. `make milkv-duo`
  now links the full kernel and userspace and packages a stock-firmware
  compatible FIT; see [Milk-V Duo](architecture/milkv-duo.md).
- [x] Automate a complete flashable SD-card image with the checksum-pinned
  official `fip.bin` boot chain, the HundeOS `boot.sd`, and an
  architecture-qualified ext2 root filesystem. `make milkv-duo-sd-image`
  produces the two-partition image described in
  [Milk-V Duo](architecture/milkv-duo.md).
- [ ] Reduce the board's embedded filesystem to the services required before
  the SD root is mounted. The full image now fits with two 1 MiB boot stacks,
  but it still embeds all userspace binaries rather than handing most of them
  off to the SD root filesystem.
- [ ] Bring up the CVITEK SDHCI controller at `0x04310000` through the existing
  userspace block protocol, identify partitions explicitly, and mount boot
  configuration and root filesystems from the same SD card.
- [ ] Verify on hardware through the 115200 baud UART: early output, kernel
  handoff, timer and interrupts, userspace bootstrap, SD I/O, login, and Bash.
  Hardware serial capture remains required evidence for completion.

#### Milk-V Duo S SG2000

- [x] Keep synchronous `COMMAND_FLOW` UART records out of the normal AArch64
  image. Live hardware measurements showed a roughly 13 ms queue-to-dispatch
  delay per approximately 150-byte record at 115200 baud. The isolated
  `milkv-duo-s-sd-image-aarch64-command-trace` target remains available for
  scheduler diagnosis and must not be used for performance comparisons. See
  [Milk-V Duo S](architecture/milkv-duo-s.md).
- [ ] Bring up the SG2000 Cortex-A53 path with the official ARM64 firmware.
  `make milkv-duo-s-aarch64` now builds the board-specific AArch64 kernel,
  staged FIT, and vendor-compatible DTB, while
  `make milkv-duo-s-sd-image-aarch64` produces the complete image from the
  checksum-pinned v2.0.1 firmware. Hardware entry and MMU validation remain,
  followed by GICv2 CPU-interface support in the common AArch64 interrupt path,
  timer/device interrupts, userspace bootstrap, and login. See
  [Milk-V Duo S](architecture/milkv-duo-s.md).

- [x] Add a CV1800B-QEMU approximation with the Duo S kernel, DTB, 510 MiB RAM,
  and complete SD image for rapid integration testing. The isolated build uses
  QEMU-compatible PTE attributes and UART0 as its console because this model
  does not emulate SG2000 UART1. It remains explicitly outside CV181x hardware
  validation. See [Milk-V Duo S](architecture/milkv-duo-s.md).
- [ ] Diagnose the CV1800B QEMU fork's repeated invalid-CSR diagnostics and the
  root `ext2fs` startup stall. The approximation currently validates generic
  SDHCI initialization, partition discovery, and boot-config mounting but does
  not reach login.
- [x] Add a separate `milkv_duo_s` boot platform using the stock
  `config-cv1813h_milkv_duos_sd` FIT contract, one RISC-V hart, the 510 MiB
  firmware-exposed memory range, the initial firmware reservation, and the
  Duo S UART, PLIC, RTC, and CV181x SD host description. See
  [Milk-V Duo S](architecture/milkv-duo-s.md).
- [x] Automate a complete Duo S SD image from the checksum-pinned official
  v1.1.4 image. `make milkv-duo-s-sd-image-riscv` preserves the vendor boot chain
  and raw logo region, replaces `boot.sd`, installs the 8 MiB boot configuration
  filesystem in partition two, and relocates the HundeOS ext2 root to partition
  three.
- [x] Work around the stock U-Boot 8 MiB kernel decompression limit without
  replacing `fip.bin`. The FIT now asks U-Boot to decompress the full kernel as
  a staged loadable and enters it through a small board-specific handoff. See
  [Milk-V Duo S](architecture/milkv-duo-s.md).
- [x] Bootstrap the userspace CV181x SDHCI service before boot configuration,
  expose MBR partitions through the common block protocol, mount
  `/dev/mmc0p2` as `/boot/config`, and configure `/dev/mmc0p3` as root. The
  controller, partition discovery, and `/boot/config` mount are verified on
  hardware; root-mount completion remains to be validated. See
  [Milk-V Duo S](architecture/milkv-duo-s.md).
- [x] Split the physical Duo S UART configuration after console bring-up. UART0
  carries early bootstrap output and the userspace console; after kernel handoff,
  kernel and driver syscall traces use UART1. The QEMU approximation continues
  to share UART0 because it does not emulate SG2000 UART1. See
  [Milk-V Duo S](architecture/milkv-duo-s.md).
- [ ] Implement and validate the CVITEK RTC register interface without treating
  it as the incompatible QEMU Goldfish RTC.
- [ ] Validate login and Bash through the Duo S UART0 userspace console while
  kernel and driver syscall traces use UART1 independently. Synchronous shell
  startup now returns structured child-abort details for login to render on
  UART0; hardware validation of the shell fault and successful startup paths
  remains. See [Milk-V Duo S](architecture/milkv-duo-s.md).
- [ ] Decide whether HundeOS should replace or reconfigure the stock firmware
  path to reclaim the C906L coprocessor. The current stock boot runs its RTOS
  before OpenSBI and exposes only the C906 application core as a hart; a future
  change needs an explicit memory, interrupt, and device-ownership contract.

#### Build and Architecture Boundaries

- [x] Qualify target artifacts by architecture and platform. AArch64 now builds
  beneath `build/aarch64`, and RISC-V beneath `build/riscv64`; host tests remain
  beneath `build/test`.
- [ ] Fix `test/run_tests.py` cleanup to remove the repository `build/test`
  directory and shared fixture/stub objects. Its current relative cleanup removes
  `test/build`, while per-suite cleans miss sibling objects; run top-level
  `make clean` before tests after changing shared structure layouts meanwhile.
- [x] Add the RV64 cross-toolchain flags, early linker script, merged AArch64
  and RISC-V compilation-database generation, and a QEMU verification target.
- [x] Make compilation-database capture side-effect-free and checked. Preserve
  static/PIC and architecture variants, retain recursive Bash working
  directories, keep normal Newlib build prerequisites, and publish atomically
  only after successful capture. Do not evaluate recipe shell substitutions or
  regenerate Newlib/Bash build preparation during capture. Regression tests
  cover failure reporting, previous-database preservation, and clean-directory
  capture; normal static/PIC sysroots build on both architectures. See
  [CLion code indexing](architecture/clion.md).
- [ ] Add RISC-V toolchain, assembler, linker scripts, kernel/userspace build
  targets, and embedded binaries. The early supervisor payload is complete;
  the architecture-matched userspace programs and services now compile and
  link. The full kernel links and boots through the userspace service graph on
  four-hart QEMU.
- [ ] Implement the existing `kernel/arch` interfaces in `arch/riscv64`, with
  shared architecture primitives in `common/arch` and userspace assembly under
  the userspace architecture directories. Do not scatter ISA conditionals
  through core kernel, services, or protocols.
- [ ] Audit AArch64 assumptions in address layouts, ELF machine validation,
  page sizes, register contexts, compiler flags, and assembly selection.

#### Boot and Platform Discovery

- [x] Enter the initial payload at `0x80200000` through OpenSBI, preserve the
  hart ID and device-tree address, establish and clear early memory, print over
  the QEMU 16550 UART, and shut down through SBI.
- [ ] Implement entry code, boot stacks, linker layout, early memory setup, and
  the handoff into the architecture-neutral kernel. The payload crosses the
  architecture-neutral handoff contract carrying the hart ID and populated
  `SystemInfo`, then enters `CPU__enter_kernel`. The OpenSBI-selected boot hart
  retains its architectural hart ID; SBI HSM starts the remaining QEMU harts
  with separate boot-stack slots while the kernel and scheduler continue to use
  their architectural IDs. The QEMU bootstrap currently maps a
  fixed `1G` RAM window with 2 MiB leaves; generalizing or removing the temporary
  bootstrap mappings remains.
- [ ] Read the device tree for memory, reserved regions, hardware threads
  (harts), timer frequency, interrupt controllers, UART, and VirtIO devices;
  populate the existing system-information and device-registry interfaces. The
  bounded early parser now discovers QEMU RAM, enabled harts, timebase, NS16550
  UART, PLIC, VirtIO-MMIO slots, and PCI ECAM. The boot path now publishes CPU,
  RAM, UART, PCI, tagged PLIC metadata, FDT reservation-map entries, and
  `/reserved-memory` children through `SystemInfo`; AArch64 retains tagged GIC
  metadata. RISC-V free spans are page-aligned and exclude the loaded image,
  boot stack, and discovered reservations. The general heap consumes these
  discontiguous spans without permitting allocations to bridge reserved holes.
  Device-registry publication remains.
- [ ] Add firmware integration through the Supervisor Binary Interface (SBI),
  selecting and checking required extensions rather than assuming availability.
  See the [SBI specification](https://github.com/riscv-non-isa/riscv-sbi-doc).
- [x] Preserve independent early-boot and panic output and support QEMU's
  NS16550 UART without assuming the AArch64 PL011 device. Boot metadata selects
  the UART implementation used by the shared userspace RX and TX drivers.

#### Memory Management and Protection

- [ ] Implement page-table creation, mapping, unmapping, permissions, address
  space switching, and fault decoding for Sv39. The bootstrap now enables Sv39
  with 2 MiB leaves for both temporary identity and canonical higher-half QEMU
  RAM/UART mappings. The low entry stub clears memory, activates translation,
  and transfers control to the higher-half linked image; the boot stack, DTB,
  MMIO metadata, kernel image, and heap are then addressed through canonical
  aliases. The fixed QEMU UART and PLIC apertures are available in both
  bootstrap address ranges. General process page-table creation, 4 KiB user/MMIO mapping,
  unmapping, lower-half address-space switching, and local invalidation are
  implemented. Process roots preserve the active upper-half supervisor
  mappings. Removing the temporary bootstrap identity mappings and fault
  decoding remain.
- [ ] Complete translation maintenance. Local per-page and whole-address-space
  invalidation plus page-table publication barriers are implemented; remote
  invalidation and executable-code synchronization remain.
- [ ] Preserve kernel/user isolation and shared-memory permissions used by
  Channels, notification queues, and read-only registries. Audit DMA and MMIO
  ordering for the chosen platform.

#### Traps, Scheduling, and Userspace ABI

- [ ] Implement trap entry/return, register save/restore, syscall dispatch,
  user-mode entry, and process context switching. Supervisor trap entry now
  saves/restores the integer context, dispatches syscalls, enters userspace, and
  switches process contexts on all four QEMU harts. Floating-point/vector
  context remains.
- [ ] Port userspace startup, syscall wrappers, atomics, and fibre context
  switching. Define floating-point/vector enablement and state preservation
  consistently with compiler settings and the selected ABI. RV64 userspace now
  has LP64 startup and syscall entry, shared-memory fences, spin-based Channel
  waits, PLIC rearming for the initial supervisor context, and integer-only
  fibre context switching. The DTB timebase is propagated through boot hardware
  information for userspace `time` conversion. The four-hart runtime reaches the
  login prompt through the complete userspace service graph.
- [ ] Implement timers and scheduler tick accounting, then secondary-hart
  startup, per-hart state, and interprocessor rescheduling. The architectural
  time counter, timebase conversion, wait/signal primitives, hart identity,
  per-hart trap installation, PLIC contexts, cache synchronization, SBI HSM
  startup, and SBI shutdown are implemented. Timer-driven scheduling runs on
  all four harts; explicit interprocessor rescheduling remains.

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
  first userspace process, syscalls, and Channel request/reply. This checkpoint
  is complete; the default full-system target now uses four harts.
- [ ] Verify login, console input/output, block I/O, and mounted filesystems,
  then multi-hart scheduling, shared-memory races, IRQ delivery, and wakeups.
  Four-hart QEMU reaches `login:`, accepts interactive input through the NS16550
  console, mounts the root, SD, Channel I/O FAT32, and Channel I/O ext2
  filesystems, and schedules work on every hart. Stress coverage for
  shared-memory races, remote invalidation, IRQ delivery, and wakeups remains.
- [x] Add architecture-specific emulator tests and keep architecture-neutral
  unit tests and existing AArch64 targets passing. `make qemu-riscv64-trace`
  runs a full-system four-hart boot using the dedicated RV64 disk configuration,
  keeps userspace I/O on the interactive NS16550 console, and writes kernel
  diagnostics to `build/riscv64/kernel-trace.log` through semihosting. Host tests
  cover valid, truncated, and incomplete RISC-V device trees plus
  platform-to-system conversion and Sv39 page-table operations. `make smoke-test`
  boots the traced AArch64 and RISC-V QEMU targets, logs in, exercises system
  information, memfs, and process listing, then shuts each guest down cleanly.
