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

The Channel payload remains the control and record transport. Driver messages
are bounded below the existing one-page Channel payload capacity; this service
does not introduce a second shared-memory ring or logging transport.
