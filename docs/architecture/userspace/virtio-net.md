# VirtIO Ethernet

`virtio_net.wuff` is a Rust, IRQ-driven Ethernet driver inside
`hunde-treiber-host`. Its client-facing interface is the existing
[packet Channel protocol](packet-drivers.md), not a new socket or buffer
transport. `network.wuff` still serves application localhost; external
application routing is not enabled by this driver slice.

## Discovery and shared implementation

The manifest selects the transport device ID, endpoint prefix, and approved Wuff:

```
virtio-driver 1 ethernet /sbin/components/virtio_net.wuff
```

The driver host probes matching MMIO slots, constructs a
`VirtioEndpointConfiguration`, routes each IRQ to itself, and loads one Wuff per
device. Endpoint names are `ethernet.0`, `ethernet.1`, and so on. The generic
transport row introduces no concrete network-driver dependency or device-specific
polling branch in the host. No endpoint appears when no NIC is present.

`hunde::virtio` owns legacy-MMIO discovery, feature negotiation, queue attachment,
notification, interrupt acknowledgement, and split-ring descriptors/publication/
completion. Block, keyboard, and network drivers share these operations; their
domain protocols remain in their respective driver crates. `hunde::dma` owns
allocation, physical-address validation, cache maintenance, and bounded copies.
Only `hunde` contains unsafe Rust. The existing kernel claim/reset mechanism now
recognizes the network device ID as well as block and input devices.

This first driver supports legacy VirtIO MMIO (transport version 1), one RX/TX
queue pair, a device-supplied MAC, optional link status, and MTU 1500. It does not
support PCI or modern MMIO, merged RX buffers, multiqueue, indirect descriptors,
VLAN/jumbo frames, or checksum/segmentation offloads. The driver follows the
[VirtIO 1.2 legacy network contract](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html):
without merged RX buffers the header is ten bytes. Header and Ethernet payload
occupy separate descriptors, so the driver does not require ANY_LAYOUT.

## Ownership and IRQ handling

Each direction has eight private, 2048-byte DMA staging slots and a 16-descriptor
split ring. RX additionally has a bounded eight-frame owned software queue.
Channels and `hunde-puffer` leases never double as NIC DMA storage. TX completion
to the packet client means its snapshot was copied into driver-owned staging;
only a validated used-ring completion permits that DMA slot to be overwritten.
Legacy TX used lengths are ignored, but descriptor identity and ownership are
validated. Unexpected indices, duplicate completions, out-of-bounds RX extents,
and unnegotiated RX offload headers fail closed. Runt Ethernet frames are dropped
and their slots reposted without terminating the shared host. RX draining is
bounded per host iteration so malformed traffic cannot monopolize dispatch.

An IRQ is acknowledged through the shared MMIO implementation. The Wuff drains
TX completions and RX packets before asking the host to rearm. When software RX
staging fills, it defers rearming until packet-client progress makes room. IRQ
routing, controller access, and kernel acknowledgement remain host/kernel
responsibilities; the NIC does not access controller registers itself.

Closing an interface resets the device and confirms reset before resetting the
rings or reusing staging. A failed reset prevents further I/O without recycling
uncertain DMA. Driver-owned DMA allocations retain their existing process-lifetime
backing policy; this slice does not add DMA reclamation or automatic host restart.
Process teardown continues to reset claimed devices before releasing their address
space. Kernel hardware code only enforces claims/reset; packet handling remains
entirely in userspace.

## Acceptance status

Native tests, formatting/lint checks, both target builds, and external-NIC
smoke on AArch64 and RISC-V pass. Dummy-cable, localhost, root-filesystem, and
shutdown regressions pass on both architectures.

The original RISC-V external-NIC fixture exceeded QEMU's eight MMIO transports
with seven disks, the keyboard, and the NIC, failing before boot. NIC fixtures
now omit the five auxiliary test disks and their mounts on both architectures,
retaining root, the required boot-configuration disk, keyboard, and serial
console. Storage regressions retain their original disk layout. Both reduced
NIC fixtures pass ARP, ICMP, TCP/UDP, lost-SYN recovery, cleanup, IRQ delivery,
keyboard startup, and shutdown checks.

## Isolated acceptance fixture

`/usr/local/bin/network_nic_probe` uses the real packet adapter and a static
192.0.2.1/24 address. The host-native `network-peer` fixture uses the same pinned
smoltcp and adapter APIs at 192.0.2.2/24, joined to QEMU by a framed Unix stream.
It is not a second TCP/IP implementation. The test deliberately drops the first
TCP SYN, then checks ARP, ICMP echo, binary UDP echo, a varying 32 KiB bidirectional
TCP exchange with partial writes, graceful TCP close, interface reset/lease
cleanup, IRQ delivery, and system shutdown.

The peer binds only a private local Unix socket: there is no host bridge, tap,
Internet access, privileged network setup, DHCP, or DNS. QEMU trace targets use
`QEMU_NET_ARGS=-nic none` by default; the smoke fixture explicitly attaches
`virtio-net-device` with the Unix-stream backend and sets `QEMU_TEST_BLOCK_ARGS=`
to omit auxiliary disks, with a private root-only mount configuration.
Both probe executables are test
fixtures, not bootstrap services; only the Wuff is included in the boot archive.

Run:

```
uv run pytest -q test/smoke/test_qemu.py -k virtio_network
```

Native tests cover ring wraparound, duplicate/invalid completion rejection,
directional descriptors, TX staging retention, RX backpressure, reset failure,
framing, and the peer conversation's ARP/ICMP/TCP/UDP/loss behavior. Existing dummy
cable and localhost smoke tests remain independent regressions.
