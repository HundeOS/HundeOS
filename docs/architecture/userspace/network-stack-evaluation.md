# Rust Network Stack Evaluation

## Decision scope

Evaluated on 2026-11-09. Recommend smoltcp for the first HundeOS TCP/IP
integration, subject to the target-runtime and interoperability tests below.
The first in-memory integration slice is approved and implemented as the fixture
described below. Application loopback TCP is now implemented as a Wuff inside
`hunde-io-host`, described in [localhost networking](localhost.md). This does not
select a NIC or add a POSIX socket ABI. NetBSD rump remains an alternative
if later requirements exceed smoltcp's protocol coverage.

The existing `socket.wuff` implements local named byte streams, not IP networking.
Preserve those semantics. Network application I/O must still enter through
`hund-io-system`; neither the stack nor sockets belong in the privileged kernel.
See [I/O system](io-system.md#socket-protocol).

## Evaluated upstream

- Crates.io `smoltcp = "=0.14.0"`, upstream commit
  `cbfe05f835cfc75bf781addc1c0eb2e1a0df1ea3` from the published VCS metadata.
- Published archive SHA-256:
  `b6f8b28ad56c6e35524a37dd492af5d1a47e31e1a4d175cd12f89c075f01980f`.
- 0BSD license; preserve upstream provenance and review dependency licenses when
  importing. No vendored source was modified or added to the production tree.
- Edition 2024, minimum Rust 1.91; tested using the repository's Rust 1.99.1.
- Defaults disabled. Evaluated features: `medium-ethernet`, `proto-ipv4`,
  `proto-ipv6`, `proto-ipv6-slaac`, `socket-tcp`, `socket-tcp-reno`, `socket-udp`,
  `socket-icmp`, `socket-dhcpv4`, `socket-dns`, `auto-icmp-echo-reply`.

The resolved graph contains smoltcp, managed 0.8.0, byteorder 1.5.0, bitflags
1.3.2, cfg-if 1.0.5, heapless 0.9.3, hash32 0.3.1, and stable_deref_trait 1.2.1.
Neither `std`, `alloc`, nor the hosted libc/TAP backends was enabled. The probe
also uses heapless directly for bounded frame queues.

Authoritative references:
[manifest](https://github.com/smoltcp-rs/smoltcp/blob/v0.14.0/Cargo.toml),
[protocol coverage](https://github.com/smoltcp-rs/smoltcp/blob/v0.14.0/README.md),
[device contract](https://docs.rs/smoltcp/0.14.0/smoltcp/phy/trait.Device.html), and
[interface polling](https://docs.rs/smoltcp/0.14.0/smoltcp/iface/struct.Interface.html).
The source manifest is authoritative where README examples or defaults differ.

## Initial scratch evaluation

An isolated scratch crate at `/tmp/hunde-smoltcp-eval` contains the downloaded
source archive, Cargo lockfile, probe, and logs. These are temporary artifacts,
not a durable project test suite. The probe is a `no_std` library with
`unsafe_code = "forbid"`, using stack-owned socket buffers and two independent
interfaces connected through safe, bounded in-memory Ethernet devices.

The host runner executes the same library code without enabling smoltcp's
`std` or `alloc` features. All three tests passed:

1. ARP exchange, TCP connection establishment, 512-byte transfer through
   128-byte socket buffers with short writes, payload validation, orderly TCP
   close, and UDP delivery between distinct IPv4 addresses.
2. Drop the first outgoing IPv4 frame during TCP establishment, then verify
   successful transfer and closure as the synthetic clock advances. This
   exercises retransmission without another incoming packet providing progress.
3. Fill the eight-frame transmit queue, verify `Device::transmit` returns
   `None`, then verify capacity becomes available after a frame is removed.

Commands, all successful:

```sh
cargo +1.99.1 test --manifest-path /tmp/hunde-smoltcp-eval/Cargo.toml
cargo +1.99.1 clippy --manifest-path /tmp/hunde-smoltcp-eval/Cargo.toml --all-targets -- -D warnings
cargo +1.99.1 build --manifest-path /tmp/hunde-smoltcp-eval/Cargo.toml --release --target aarch64-unknown-none-softfloat
cargo +1.99.1 build --manifest-path /tmp/hunde-smoltcp-eval/Cargo.toml --release --target riscv64imac-unknown-none-elf
```

These initial target builds produced libraries, not linked HundeOS executables.
No QEMU, real NIC, Linux peer, throughput, or hardware DMA validation was done
at that stage. The repository fixture below extends this coverage.
IPv6/SLAAC, ICMP, DHCPv4, and DNS were compiled but not exercised. This is not a
security audit or evidence of full protocol conformance.

## Repository fixture

`user/components/network_probe` is now a workspace crate using the pinned
dependency without upstream modifications. Its safe bounded packet adapter and
six native tests cover frame-queue saturation, RX retention until reply capacity
is available, ARP, TCP partial writes and varying payloads, UDP, timer-driven
recovery after a lost SYN, orderly closure, and explicit socket removal.

The Rust executable uses the shared `hunde` bootstrap and existing C CRT, and is
installed at `/usr/local/bin/network_probe` on the root filesystem, not in the
bootstrap archive. It is never started by boot configuration, and bootstrap does
not depend on networking. Both target executables link successfully.
The stack buffers remain bounded and stack-owned; the surrounding executable
uses the standard shared runtime, including its allocator and diagnostics.
Both owning code and tests inherit `unsafe_code = "forbid"`.

`test_qemu_network_stack_in_memory` passed on AArch64 and RISC-V on 2026-11-09.
It checks that the probe is absent from `/sbin` and runs three fresh rootfs probe
processes per
architecture, checking exit status, success trace records, and absence of process
faults. Each process repeats the baseline and packet-loss cases three times.
The fixture advances a virtual clock to protocol deadlines and bounds each
polling turn. It does not yet exercise real clock/wakeup integration, physical
packet transport, a NIC, application network sockets, or peer interoperability.

See [fixture instructions and dependency licenses](../../../user/components/network_probe/README.md).

Validation also passed the full `make test` suite, `make hundelint`, production
Clippy for both architectures, and `make format-check`. The compilation database
was regenerated after adding the fixture sources. No upstream source changes,
kernel changes, NIC implementation, or application socket changes were required.

## Integration fit and gaps

### Runtime and execution

smoltcp takes explicit timestamps and frame-device tokens; the tested path needs
no OS threads, pthread layer, compiler TLS, allocator, or BSD host shim. It can
have one owning task, fitting the existing cooperative runtime without first
making native I/O clients safe for multiple OS threads. Rust `std::thread` and
parallel fibre schedulers are not prerequisites for this route.

Reuse `IpcPlatform__monotonic_usec` through a checked safe `hunde` adapter rather
than adding another architecture-specific clock. Schedule the earlier of a
packet/request event and `Interface::poll_at`'s deadline; retransmission and
other timers must advance even when no frames arrive. A sleep-only device loop
or polling only in response to application requests is insufficient.

`Interface::poll` drains ingress until the device runs out of frames. Do not
put that unbounded work on a shared host callback under sustained input. Use
`poll_ingress_single`, bounded egress, and explicit per-turn budgets, then yield
to other I/O work. The original `IoComponent` contract had execute/disconnect
callbacks but no autonomous network timer callback. Hosting needs that lifecycle
rather than network-specific polling branches in the I/O host.
The localhost integration adds a generic `advance(now_usec)` callback with an
absolute next deadline to I/O component ABI v7. Existing passive components use
the default idle schedule. The host forwards the earliest deadline to its fibre
scheduler, which waits for a Channel event or a rounded-up millisecond timeout.

The stack is a Rust Wuff owned by the I/O host, not a separate executable or an
interrupt-host component. Discovery and routing stay with the common I/O system,
while NIC register and IRQ handling stays in a
Rust Wuff under `hunde-treiber-host`. Keep ABI, clock, MMIO, DMA, and shared-buffer
unsafe operations in `hunde`, not in the stack or NIC crate.

### Packet ownership

Define a packet-oriented NIC Channel protocol, not a stream of undelimited
bytes. Reuse the directional `hunde-puffer` buffers and existing Channel
authentication, descriptor, generation, and completion mechanisms. Define
frame lengths, bounded queue capacity, TX completion, RX recycling, link state,
and failed-device cleanup before mapping this onto hardware. Do not repurpose
the block protocol or place bulk packets inline in Channels.

smoltcp receives bytes through an RX token and constructs outgoing bytes through
a TX token. `receive` also needs a TX token for immediate responses. Reserve
actual transmit capacity before handing out a token: its consume callback does
not return a transport error. Handle asynchronous device failure in the adapter
and service lifecycle. Advertise checksum offloads only when the NIC really
provides them; the probe used software checksums.

The adapter must retain buffers until callbacks and any DMA use are complete,
and never recycle uncertain in-flight memory. Zero-copy is not established by
this evaluation; bounded staging may be needed to reconcile token borrowing
with asynchronous shared-buffer ownership. Use the shared copying operations
rather than adding driver-local unsafe copies. See
[shared-buffer lifetime rules](shared-buffers.md).

### Application sockets and policy

The current local stream interface cannot simply become TCP/IP by linking a
crate. Define IP addresses and ports, TCP connection/accept lifetimes,
nonblocking readiness, partial transfers, close/half-close, errors, cancellation,
and authenticated ownership. smoltcp's listening TCP socket becomes the accepted
connection; the service must maintain replacement listeners and a bounded
backlog rather than assuming a BSD accept queue is supplied automatically.

UDP needs datagram boundaries and peer-address results, which the current
byte-stream read/write interface does not express. Keep local `socket://name`
behavior intact. POSIX socket compatibility is separate from the packet stack.
Address configuration, route/DNS policy, buffer quotas, and per-boot random
seeding also belong to our service integration; the probe uses deterministic
seeds only for repeatability, not as production configuration.

### Protocol trade-offs

Upstream supports the basic Ethernet/ARP, IPv4/IPv6, TCP/UDP/ICMP, DHCPv4 client,
and DNS resolver building blocks. It is not a complete BSD networking subsystem,
TLS implementation, or Wi-Fi association/security stack.

Documented omissions include 802.1Q VLAN frames, jumbo frames, TCP selective
acknowledgements, TCP timestamps, and packetization-layer path MTU discovery.
The evaluated configuration does not enable IP fragmentation/reassembly, so
large fragmented datagrams require an explicit feature and memory-policy
decision. Reno was selected rather than floating-point CUBIC for an initial
soft-float-friendly configuration. Socket and table capacities must be sized
explicitly; the probe's tiny buffers are test fixtures, not production defaults.

## Implementation sequencing

The in-memory evaluation and application TCP localhost integration are complete:

1. Completed: pinned dependency in the shared workspace without upstream
   changes, with a safe in-memory packet adapter and persistent native tests.
2. Completed: in-memory two-interface TCP/UDP probe as a real HundeOS executable
   under both QEMU architectures, including packet loss, virtual deadlines,
   bounded buffers, and cleanup. This proves runtime/linking, not just
   cross-compilation. This fixture uses virtual time; the packet-Channel probe
   below separately exercises real clock and wakeup integration.
3. Completed: IPv4/IPv6 TCP loopback inside `network.wuff` in the I/O host,
   using generic component deadlines, authenticated stream handles, and the
   existing directional application buffers. Validated separate client/server
   processes under both QEMU architectures, including a 32 KiB binary transfer,
   listener/connect/accept/close, refusal, and legacy stream coexistence.
4. Implemented: standard packet Channels and directional `hunde-puffer` leases,
   a reusable per-interface smoltcp adapter, and a dummy Rust Wuff with two
   cable-connected Ethernet ports in the driver host. See
   [packet drivers](packet-drivers.md).
5. Implemented: IRQ-driven Rust `virtio_net.wuff`, with common split-ring/MMIO
   operations shared with block and keyboard drivers. An isolated smoltcp peer
   fixture exercises ARP, ping, TCP/UDP, lost-SYN recovery, cleanup, and shutdown.
   Both AArch64 and RISC-V QEMU acceptance pass with auxiliary test disks/mounts
   omitted; root, boot configuration, keyboard, and serial console remain.
   See [VirtIO Ethernet](virtio-net.md).
6. Integrate external interfaces with application-facing addressing/routing and
   timer policy before adding DHCP/DNS configuration or claiming application
   networking is ready. Physical Ethernet/Wi-Fi drivers remain separate work.

The evaluation favors this smaller Rust-native path over implementing rump's
host compatibility layer first. It does not justify promising BSD feature
parity, production throughput, or immediate Wi-Fi support.
