# Bash Port

The build copies the pinned upstream tree from `vendor/bash/src` into
`build/<arch>/bash/source`, applies the patches in `patches/`, and configures an
out-of-tree target build in `build/<arch>/bash/build`.

The architecture-qualified target sysroot is staged at
`build/<arch>/sysroot`. It contains the public compatibility headers, `crt0.o`,
`libc.a`, `libhunde.a`, and the pinned Newlib `libm.a`. The sysroot installs
Newlib's `math.h` dependency closure and an explicitly verified allowlist of
OS-independent libc archive members without exposing unsupported Newlib libc
headers. `make sysroot-smoke-all` compiles and statically links a small program
using only that packaged target environment.

The initial configuration keeps bundled Readline and job control enabled. NLS,
network redirections and Bash's allocator are disabled until
their HundeOS dependencies are deliberately implemented. `bash-configure`
completes for both targets. Bash is configured to use plain `setjmp` until
HundeOS provides signal-mask-preserving `sigsetjmp` semantics. Reviewed answers
for runtime-only configure probes live in
`config.cache`, which is copied into each target build before configuration.

`bash-build` now compiles through `execute_cmd.c` and enters the generated
`y.tab.c` parser on both AArch64 and RISC-V. The sysroot provides
`isblank`, `strstr`, `sleep`, and descriptor `fcntl` support for `F_GETFD`,
`F_SETFD`, `F_GETFL`, and `F_SETFL`. Open-file status is shared across duplicate
descriptors, and pipe transfers honor `O_NONBLOCK` with `EAGAIN` while ordinary
descriptors keep blocking. `FD_CLOEXEC` remains descriptor-local; close-on-exec
enforcement is deferred until general descriptor inheritance is implemented.
Kernel-backed real and effective
credential queries, identity-preserving setters, and complete built-in password
records are also available. Kernel-backed `alarm` provides one-shot `SIGALRM`
delivery, replacement, and cancellation. POSIX `wait` and `waitpid` support
process and process-group selection, `WNOHANG`, stopped and continued children,
signal interruption, and encoded exit or signal status. Bounded
`sprintf`, `vsprintf`, `snprintf`, and `vsnprintf`, stream `putc`, and the
`ENOEXEC` and `E2BIG` errno values are available. Startup supplies an environment
vector, and libc provides mutable `environ`, `getenv`, `setenv`, `unsetenv`, and
`putenv`; Bash's configure detects those interfaces on both targets. Libc builds
the bounded 64 KiB path, argument, and environment payload and provides `execve`
over the native atomic process-image overlay described in
`docs/architecture/kernel/exec.md`. The sysroot now provides `<sys/param.h>` as the
compatibility entry point for HundeOS path limits, and Bash's configure detects
it on both architectures. Bash now compiles past its `F_GETFL`, `F_SETFL`, and
`ttyname` uses; configure detects `ttyname` on both targets, and `general.c`
compiles. Libc and the filesystem services provide real `O_APPEND` behavior as
well as tested `abort`; selected Newlib members provide `strncpy`, `qsort`, and
the core memory and string operations.
Kernel-backed process identity provides `getpid` and `getppid`, and
`variables.c` now compiles on both targets. Libc provides `puts` over its
descriptor-backed, line-buffered standard output, and `mailcheck.c` now compiles
on both targets. The sysroot provides POSIX
`<termios.h>` and userspace-service-backed terminal attribute operations, so
Bash selects its POSIX terminal driver and `nojobs.c` compiles past the former
legacy `<sgtty.h>` and `fork` boundaries. Libc now provides process-local
kernel-backed behavior for `signal`, `sigaction`, `sigprocmask`, `sigpending`,
and atomic `sigsuspend`; configure selects `HAVE_POSIX_SIGNALS` and `sig.c`
compiles on both targets. Caught handlers interrupt or restart blocking child
waits and queue-backed I/O according to `SA_RESTART`. Libc provides the fixed
compatibility hostname `hundeos`; Bash detects `gethostname` and compiles
`lib/sh/oslib.c` on both targets. Libc also provides allocating `strdup`, so
`lib/sh/itos.c` compiles on both targets. A port patch makes Bash's diagnostic
declarations available to its network-disabled fallback, so `lib/sh/netopen.c`
also compiles on both targets. Libc provides deterministic, non-cryptographic
`rand` and `srand` plus `mktemp`, `mkstemp`, and `mkdtemp`. Candidate names mix
the process ID with deterministic state and retry collisions; `mkstemp` uses
exclusive creation and `mkdtemp` uses directory creation as their atomic safety
mechanisms. Bash remains unmodified and `lib/sh/tmpfile.c` compiles on both
targets. Libc provides `gettimeofday` from the wall-clock and monotonic sample
published on the shared clock page. This gives Bash's pseudorandom generator
seed variation; HundeOS does not claim a secure entropy interface.
`lib/sh/random.c` now compiles on both targets. Selected Newlib libc members
provide `strtol`, `strtoul`, `strtoll`, and `strtoull` with shared HundeOS
`errno` behavior; Bash's configure detects them on both targets and the build
passes `lib/sh/strtod.c`. HundeOS defines the standard terminal window-size
ioctl ABI for source compatibility, but its consoles do not currently have a
resize source. The ioctls and libc's `tcgetwinsize` and `tcsetwinsize` entry
points validate the terminal and return `EOPNOTSUPP` without reading or changing
window state.
Readline therefore uses `LINES` and `COLUMNS` when supplied and otherwise falls
back to 80 columns by 24 rows. `NEED_EXTERN_PC` gives bundled termcap ownership
of `PC`, `UP`, and `BC`. Libc supports allocating `getcwd(NULL, 0)`, so Bash does
not compile its fallback, and its environment functions override libc's weak
POSIX entry points while libc retains strong scoped implementations for HundeOS
callers and hosted tests. `bash-build` builds only the target shell rather than
the host-only documentation helpers. Both AArch64 and RISC-V now compile and
statically link a Bash ELF with no unresolved symbols. Runtime integration and
shell behavior on HundeOS remain to be verified. The architecture-qualified
disk rootfs installs the executable as `/usr/local/bin/bash`, and login starts
that Bash executable directly. The old native shell is retired from the normal
userspace build.

The runtime now also provides anonymous `pipe`, `dup`, `dup2`, and `F_DUPFD`
over the `hund-rohr` userspace service. These are usable within one process and
support redirection ownership correctly. Pipeline execution still depends on
the authenticated cross-process descriptor inheritance work in
`docs/TODO.md`; the compatibility layer does not claim that copied handle
numbers grant a child access.

HundeOS libc exposes `sysconf(_SC_CLK_TCK)` with the same microsecond tick rate
used by `times()`. Bash detects that interface during configuration, so its
`time` keyword reports elapsed and CPU seconds without a platform-specific
source change or a 60 Hz fallback.

Configure or build the selected `ARCH` with:

```sh
make bash-configure
make bash-build
```

Use `make bash-configure-all` or `make bash-build-all` to run the corresponding
target for both AArch64 and RISC-V.
