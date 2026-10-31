# Userspace Services And Drivers

Userspace lives under `user/`: service orchestration in `services/`, device and
filesystem implementations in `drivers/`, commands in `commands/`, and shared
protocol/runtime adapters in `lib/`. Rust uses the shared `user/Cargo.toml`
workspace alongside the C sources.

## Services

| Responsibility | Guide | Implementation |
| --- | --- | --- |
| Initial service and mount startup | [Bootstrap services](bootstrap-services.md) | `user/services/hunde-mama/`, `user/services/hundeschule/` |
| Application I/O, namespaces, handles, and mounts | [Userspace I/O](io-system.md) | `user/services/hunde-io-host/` |
| I/O-host and driver-host ownership | [Host architecture](io-driver-hosts.md) | `user/services/hunde-io-host/`, `user/services/hunde-treiber-host/` |
| Channel setup authorization | [Channels](../kernel/channels-and-event-queues.md) | `user/services/hundekanal-warter/` |
| Directional bulk-buffer leases | [Shared buffers](shared-buffers.md) | `user/services/hunde-puffer/` |
| Device IRQ registration, delivery, and rearm | [Interrupt service](interrupt-service.md) | `user/services/hunde-treiber-host/` |

The Rust driver host publishes the retained `hund-unterbrict` endpoint and hosts
VirtIO block instances with local IRQ dispatch. Other hardware services remain
external. The I/O host loads approved filesystem, socket, and pipe Wuffs and
dispatches their operations locally; those components are not separate processes.

## Drivers And Filesystems

- [I/O protocols](io-system.md): block-backed filesystems, consoles, sockets,
  pipes, device files, and temporary files.
- [Process registry and procfs](process-registry-and-procfs.md): kernel-published
  accounting, userspace rendering, `ps`, and diagnostic snapshots.
- [Bus information](bus-info.md): userspace PCI inventory and query protocol.
- [Driver diagnostics and latency tracing](driver-tracing.md): syscall diagnostics,
  kernel trace snapshots, and the optional userspace Channel trace collector.
- [Bootstrap archive and executable loading](bootstrap-filesystem-handoff.md):
  embedfs ownership, permissions, and the executable-loader boundary.

Device drivers own device registers and device completion. The kernel owns
privileged exception entry and enforces capabilities and mappings. The interrupt
manager owns delegated IRQ registration and rearming. Filesystem and network
policy belongs to the I/O host rather than the kernel or hardware-driver host.

## Shared Libraries And Runtime

- [Rust userspace](rust-userspace.md): safe component interfaces, manifest trust
  registry, shared C adapters, and cooperative fibre execution.
- [Dynamic linking](dynamic-linking.md): ELF/Wuff images, interpreter integration,
  component ABI, and supported relocations.

Initial mounts come from `config/bootstrap.fstab`. Service endpoints and
block-filesystem factories come from `config/io-host.conf`. Both are packaged
in the mapped archive; neither assumes that `/sbin` is already mounted.
