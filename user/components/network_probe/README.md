# In-Memory Network Stack Probe

This fixture links smoltcp into a real HundeOS executable using Rust `main` and
the existing C CRT. It is not a network service, a NIC driver, a production
packet transport, or a change to local `socket://` semantics.

`src/device.rs` implements smoltcp's safe packet-token interface with eight-entry
RX/TX queues and 1514-byte frames. It reserves reply capacity before removing an
RX frame. The in-memory link copies through heapless's existing clone operation;
it makes no zero-copy or DMA claim. Only this fixture uses the adapter until a
real packet Channel protocol and hosting model are agreed.

Each run creates two separate Ethernet interfaces, resolves ARP, transfers a
varying 512-byte TCP payload through 128-byte socket buffers, sends a UDP
datagram, closes TCP, and removes all sockets after packet queues drain. A
second case drops the first IPv4 frame (the initial TCP SYN). Both cases run
three times per process. Native tests additionally check TX saturation and RX
retention while reply capacity is exhausted.

The event loop processes one ingress frame per side per turn, plus bounded
maintenance and egress. It advances a deterministic virtual clock to the next
protocol deadline when no incoming frames remain. This tests timer-only
retransmission and cleanup without waiting ten real seconds for TCP TIME_WAIT.
It does not validate real clock/wakeup integration or scheduling latency.

All protocol buffers and frame queues have bounded, stack-owned storage.
smoltcp's `std` and `alloc` features are disabled. The executable still uses the
shared `hunde` runtime bootstrap, allocator, logging, and panic adapter, like
other Rust executables. The fixture inherits the workspace unsafe-code ban.

## Running

```sh
cargo +1.99.1 test --manifest-path user/Cargo.toml -p hunde-network-probe
make ARCH=aarch64 build/aarch64/user/network_probe.bin
make ARCH=riscv64 build/riscv64/user/network_probe.bin
uv run python -m pytest test/smoke/test_qemu.py -k network_stack_in_memory
```

The root filesystem installs the probe at `/usr/local/bin/network_probe`.
It is not included in the bootstrap archive or started during boot. Run it
after the root filesystem is mounted and check its zero exit status. Successful
execution emits
`network_probe: TCP UDP ARP retransmission cleanup ok` to the kernel trace, not
standard output. The smoke test checks that `/sbin/network_probe` is absent,
launches three fresh processes from the root filesystem per architecture, and
checks both their status and trace records, rejecting process faults.

## Upstream and licenses

smoltcp is pinned to 0.14.0, with all resolved registry versions and checksums in
`user/Cargo.lock`. No upstream patches are applied. The feature set includes
Ethernet, IPv4/IPv6/SLAAC, TCP with Reno, UDP, ICMP echo, DHCPv4, and DNS. Only
IPv4 ARP/TCP/UDP paths are exercised here; the rest are compile coverage only.

Registry package license declarations:

| Packages | License |
| --- | --- |
| smoltcp, managed | 0BSD |
| heapless, hash32, cfg-if, stable_deref_trait | MIT OR Apache-2.0 |
| bitflags | MIT/Apache-2.0 |
| byteorder | Unlicense OR MIT |

Keep upstream license texts with any redistributed source packages, and retain
the applicable notices when distributing binaries under the chosen dependency
license alternatives. Upstream references:
[smoltcp 0.14.0](https://github.com/smoltcp-rs/smoltcp/tree/v0.14.0),
[smoltcp license](https://github.com/smoltcp-rs/smoltcp/blob/v0.14.0/LICENSE-0BSD.txt),
[heapless](https://crates.io/crates/heapless/0.9.3),
[managed](https://crates.io/crates/managed/0.8.0).

See [evaluation and remaining integration work](../../../docs/architecture/userspace/network-stack-evaluation.md).
