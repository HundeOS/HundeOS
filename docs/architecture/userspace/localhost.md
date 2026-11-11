# Localhost networking

`network.wuff` links unmodified smoltcp 0.14.0 into a safe Rust I/O component,
implemented under `user/services/hunde-network`. The I/O host loads it from the
approved `config/io-host.conf` image registry as endpoint `network`. There is no
network executable, privileged socket implementation, or NIC dependency.
The manifest selects this service independently of bootstrap filesystem mounts;
removing its `service network` entry disables it without a host code change.
Like other configured resident components, a selected component must load
successfully before the host publishes component readiness.

Applications open `tcp://127.0.0.1:PORT`, `tcp://localhost:PORT`, or
`tcp://[::1]:PORT` through `hund-io-system`. `localhost` is a fixed IPv4 alias,
not a DNS lookup. Only these loopback addresses and nonzero explicit ports are
accepted. `socket://name` continues to select the independent local stream
component, unchanged.

## Runtime and transport

The component owns one smoltcp interface, its socket set, and upstream's IP
`Loopback` device. A safe wrapper limits that device to 32 queued packets of at
most 1500 bytes, preserving capacity for an immediate response when receiving.
It does not reimplement packet allocation or copying. No hardware registers,
DMA, IRQs, or unsafe Rust are required in the component.

I/O component ABI v7 adds `advance(now_usec)`, returning whether work occurred
and an optional absolute next deadline. Each host turn runs maintenance, at
most one ingress packet, and bounded egress. Passive components return an idle
schedule. There is no network-specific branch in host component polling.

The shared Rust fibre scheduler collects the earliest registered deadline and
waits for a Channel event or that timeout, rounding upward to milliseconds.
All fibre waiters are repolled after a wake. The timestamp comes from the common
`IpcPlatform__monotonic_usec` adapter in `hunde`; no new architecture clock is
introduced. Protocol timers therefore advance without another application
request or incoming packet. Pending local packets request an immediate turn.

External application requests retain the existing authenticated Channel and
directional `hunde-puffer` buffers. Calls into the Wuff borrow request buffers
only during execution. Pending reads/writes/connects return without retaining
raw buffer pointers, and the common host retries owned requests. One application
session still permits only one outstanding operation. The stack has one owner;
this does not make the native I/O library safe for concurrent OS threads.

## Stream semantics and bounds

- `BIND` or `LISTEN` opens a listener. A duplicate endpoint fails. Each listener
  owns four listening smoltcp sockets; accept transfers a connected socket to a
  new handle and installs a replacement before publishing that handle.
- A connect open completes only after the TCP handshake. Retry of a pending
  open reuses its existing socket. Refusal or the 30-second connection timeout
  fails and releases it. Ephemeral ports are selected from 49152 through 65535,
  excluding live local endpoints.
- Handles are owned by authenticated session and PID. Accept/read/write/close
  reject another owner. Session disconnect cancels pending opens, removes
  listeners and unaccepted peers, and aborts owned connections. Accepted peers
  remain independent of listener closure.
- Each TCP socket has separate 4096-byte receive and transmit buffers. Reads
  and writes may return short counts. Empty reads and full writes wait without
  blocking other clients. Zero-length transfers succeed immediately. Buffered
  data precedes orderly EOF; a reset fails with `IO_NOT_OPEN`.
- Close invalidates the application handle immediately and lets TCP drain/close
  in the background. Closed sockets are removed; a 30-second cleanup deadline
  bounds retained closing sockets even if a peer never closes. Established
  application-owned sockets have no idle timeout.
- There are at most 64 application resources and 128 TCP sockets, including
  backlog and closing sockets. Failed allocation rolls back unpublished sockets.
- Nonblocking accept/read/write report `IO_WOULD_BLOCK` when pending. Nonblocking
  connect is explicitly unsupported rather than allocating an inaccessible
  pending socket. Other transfer flags are rejected.

For example, run two separate shell jobs:

```sh
socket_test listen tcp://localhost:8080 &
socket_test connect tcp://localhost:8080 hello
```

## Limits and validation

This is TCP loopback, not external networking, BSD socket compatibility, or UDP
datagrams. Half-close, readiness/select, socket inheritance, DNS lookup, restart
recovery, and NIC packet transport remain unimplemented. The internal-only stack
uses a deterministic seed; a suitable entropy source and TCP sequence/port
policy are required before connecting it to untrusted external packets.

Native tests cover IPv4/IPv6 and the localhost alias, partial/binary transfers,
backpressure, ownership, listener replacement, refusals, capacity exhaustion,
cleanup deadlines, nonblocking behavior, and bounded upstream loopback tokens.
The QEMU smoke `test_qemu_tcp_localhost` exercises separate client/server
processes through the I/O host on both architectures, including a 32 KiB varying
binary payload, refusal, legacy local sockets, and pipe coexistence. The binary
transfer checks every byte and accumulates short transfers on both ends.

On 2026-11-09, the full `make test` suite passed, including nine native network
tests. After the fixture adopted bounded BString chunks, its seven targeted C
tests and the expanded AArch64/RISC-V QEMU smoke passed again. `make hundelint`
reports clean, production Clippy passed for both targets, and `make format-check`
passed. No NIC or external-peer interoperability is claimed by these runs.

smoltcp and managed remain 0BSD; enabling their upstream `alloc` support does
not enable `std` or an OS networking backend. No upstream sources are patched.
