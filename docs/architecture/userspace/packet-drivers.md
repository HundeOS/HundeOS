# Packet drivers

The reusable Ethernet adapter lives in `hunde-network::packet`. Each instance
owns one interface connection to a driver Wuff in `hunde-treiber-host`.
Localhost remains an IP loopback device inside `network.wuff`; it does not
visit the driver host. External application routing is not enabled by this slice.

## Transport and ownership

`common/ipc/packet_protocol.h` is the authoritative versioned wire contract.
`hunde::packet` owns its safe client/server translations, mapping validation,
and buffer copies. Drivers and the smoltcp adapter contain no unsafe code.

An interface admits one authenticated transport PID, with one RX Channel and
one TX Channel. Separate Channels allow a pending receive without blocking
transmit. OPEN advertises the MAC, IP MTU, link state, and a directional
`hunde-puffer` lease: RX is client-read-only; TX is client-write-only.
The existing manager supplies 2 MiB buffers. Channels carry descriptors and
completions, never inline frame bytes or service-specific shared rings.

TRANSMIT names a lease, offset, and complete Ethernet-frame length. RECEIVE
names the RX lease, offset, and full advertised frame capacity. Frames include
the Ethernet header but exclude FCS; this version supports IP MTUs up to 1500,
frames from 14 through 1514 bytes, and software checksums. VLAN/jumbo/offload
negotiation is not implemented. Every operation checks the session direction,
lease identity, bounds, and current kernel mapping authorization.

The server snapshots TX bytes into an owned bounded frame before a driver sees
them. A successful completion means the driver consumed that snapshot, not
that hardware delivered it. A producer cannot recycle TX storage before the
completion. RX completion precedes the client's private snapshot; only the next
RECEIVE authorizes reuse of RX storage. No Rust references into peer-writable
memory escape the compatibility library. Physical drivers must retain their own
DMA staging until hardware completion; this protocol does not promise zero-copy.

Both adapter staging and dummy-port queues are bounded at eight frames. The
adapter's transmit limit includes its outstanding Channel request. A pending
reply or backpressured transmit retains its owned result/frame without repeating
the operation. Transport failures stop new adapter tokens rather than silently
claiming successful delivery.

Closing either Channel retires both directions of that interface owner. Explicit
CLOSE waits for pending allocations and lease detachments before acknowledging
cleanup; pending I/O is cancelled. Queues are cleared before replacement clients
are admitted. Buffer-manager requests are serialized and polled asynchronously
inside the driver host, so runtime lease allocation/release never blocks IRQ
dispatch. Initial listener/manager Channel setup still uses normal synchronous
service startup.

Service-to-service backing allocations remain retained under the existing
`hunde-puffer` policy, even after attachment release. Repeated opens consume the
manager's finite backing slots; this implementation does not claim pooled
service-buffer reuse, automatic host restart, or recovery of uncertain DMA.
See [shared-buffer lifetime rules](shared-buffers.md).

## Dummy cable

`polled-driver net-dummy /sbin/components/net_dummy.wuff` in `io-host.conf`
selects the Rust Wuff without a concrete driver dependency or startup branch in
the host. Only manifest-approved driver images can load; poll-only entries
must not request an IRQ. The I/O host ignores these driver records.

The Wuff publishes `net-dummy.0` and `net-dummy.1`, with locally administered
MAC addresses ending in 01 and 02. Transmitted frames arrive on the opposite
port. It models a cable, not an echo device, Ethernet controller, or Wi-Fi
association service. Closing a port discards queued cable traffic in both
directions so an old owner's frames cannot reach its replacement.

`network_probe` exercises two real adapter/Channel instances and two smoltcp
Ethernet interfaces through this Wuff: ARP, TCP, short writes, a varying 32 KiB
binary payload, orderly TCP close, and explicit lease cleanup. Its IPC run uses
the real monotonic clock and event/timed waits. It remains a root-filesystem
test executable, not a bootstrap service. Existing in-memory loss/retry tests
and application-facing localhost tests remain separate regressions.

The first real QEMU NIC is now [VirtIO Ethernet](virtio-net.md), using this same
packet interface. Next: interface/address/routing policy and production timer
scheduling for external interfaces. Application UDP,
DHCP/DNS policy, POSIX socket compatibility, and Wi-Fi control remain deferred.
