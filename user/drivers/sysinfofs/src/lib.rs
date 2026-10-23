#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use core::fmt::{self, Write};
use hunde::abi::IoError;
use hunde::boot::{HardwareInfo, InterruptController};
use hunde::component::IoComponent;
use hunde::inheritance::{Grants, Owner};
use hunde::io::{
    Credentials, DirectoryEntry, FileInfo, GroupId, Handle, Operation, Reply, Request, SessionId,
    UserId,
};
use hunde::protocol::*;

#[cfg(feature = "component")]
hunde::io_component!(
    Sysinfofs,
    b"Rust sysinfofs component panicked",
    Sysinfofs::new
);

struct Contents(Vec<u8>);
impl Contents {
    fn new() -> Self {
        Self(Vec::new())
    }
    fn line(&mut self, key: &str, value: u64) -> Result<(), IoError> {
        writeln!(self, "{key}={value}").map_err(|_| IoError(IO_UNKNOWN_BUFFER))
    }
}
impl Write for Contents {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        if text.len() > 12 * 1024 - self.0.len() {
            return Err(fmt::Error);
        }
        self.0.try_reserve(text.len()).map_err(|_| fmt::Error)?;
        self.0.extend_from_slice(text.as_bytes());
        Ok(())
    }
}

fn render(info: &HardwareInfo) -> Result<Vec<u8>, IoError> {
    let mut output = Contents::new();
    output.line("cpu.count", info.cpu_count)?;
    output.line("cpu.online_count", info.online_cpu_count)?;
    output.line("uart.physical_base", info.uart.physical_base)?;
    output.line("uart.mmio_size", info.uart.mmio_size)?;
    output.line("uart.irq", info.uart.irq)?;
    output.line("uart.kind", info.uart.kind)?;
    output.line("uart.register_shift", info.uart.register_shift)?;
    output.line("uart.register_width", info.uart.register_width)?;
    output.line("rtc.present", u64::from(info.rtc.is_some()))?;
    if let Some(value) = info.rtc {
        output.line("rtc.physical_base", value.device.physical_base)?;
        output.line("rtc.mmio_size", value.device.mmio_size)?;
        output.line("rtc.irq", value.device.irq)?;
        output.line("rtc.kind", value.kind)?;
    }
    output.line("framebuffer.present", u64::from(info.framebuffer.is_some()))?;
    if let Some(value) = info.framebuffer {
        output.line("framebuffer.physical_base", value.physical_base)?;
        output.line("framebuffer.physical_size", value.size)?;
        output.line("framebuffer.width", value.width)?;
        output.line("framebuffer.height", value.height)?;
        output.line("framebuffer.pixels_per_scanline", value.pixels_per_scanline)?;
        output.line("framebuffer.pixel_format", value.pixel_format)?;
        output.line("framebuffer.red_mask", value.red_mask)?;
        output.line("framebuffer.green_mask", value.green_mask)?;
        output.line("framebuffer.blue_mask", value.blue_mask)?;
        output.line("framebuffer.reserved_mask", value.reserved_mask)?;
    }
    output.line("pci.present", u64::from(info.pci.is_some()))?;
    if let Some(value) = info.pci {
        output.line("pci.ecam.physical_base", value.ecam_physical_base)?;
        output.line("pci.ecam.size", value.ecam_size)?;
        output.line("pci.bus.start", value.bus_start)?;
        output.line("pci.bus.end", value.bus_end)?;
    }
    output.line("sd_host.present", u64::from(info.sd_host.is_some()))?;
    if let Some(value) = info.sd_host {
        output.line("sd_host.physical_base", value.device.physical_base)?;
        output.line("sd_host.mmio_size", value.device.mmio_size)?;
        output.line("sd_host.irq", value.device.irq)?;
        output.line("sd_host.kind", value.kind)?;
        output.line("sd_host.source_clock_hz", value.source_clock_hz)?;
    }
    output.line("ethernet.present", u64::from(info.ethernet.is_some()))?;
    if let Some(value) = info.ethernet {
        output.line("ethernet.mac_kind", value.kind)?;
        output.line("ethernet.physical_base", value.device.physical_base)?;
        output.line("ethernet.mmio_size", value.device.mmio_size)?;
        output.line("ethernet.irq", value.device.irq)?;
    }
    output.line("memory.physical_start", info.memory_start)?;
    output.line("memory.physical_end", info.memory_end)?;
    output.line(
        "memory.physical_size",
        info.memory_end
            .checked_sub(info.memory_start)
            .ok_or(IoError(IO_INVALID_PATH))?,
    )?;
    match info.interrupt_controller {
        InterruptController::Other(kind) => output.line("interrupt_controller.kind", kind)?,
        InterruptController::Gic { kind, info } => {
            output.line("interrupt_controller.kind", kind)?;
            output.line(
                "interrupt_controller.distributor_physical_base",
                info.distributor_physical_base,
            )?;
            output.line(
                "interrupt_controller.redistributor_physical_base",
                info.redistributor_physical_base,
            )?;
            output.line(
                "interrupt_controller.cpu_interface_physical_base",
                info.cpu_interface_physical_base,
            )?;
        }
        InterruptController::Plic { kind, info } => {
            output.line("interrupt_controller.kind", kind)?;
            output.line(
                "interrupt_controller.plic.physical_base",
                info.physical_base,
            )?;
            output.line("interrupt_controller.plic.source_count", info.source_count)?;
        }
    }
    Ok(output.0)
}

struct Description {
    handle: Handle,
    owners: Vec<Owner>,
    offset: u64,
}
impl Description {
    fn new(handle: Handle, owner: Owner) -> Result<Self, IoError> {
        let mut owners = Vec::new();
        owners
            .try_reserve(1)
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        owners.push(owner);
        Ok(Self {
            handle,
            owners,
            offset: 0,
        })
    }
}

pub struct Sysinfofs {
    bytes: Vec<u8>,
    descriptions: Vec<Description>,
    grants: Grants,
    next_handle: i64,
}

impl Sysinfofs {
    pub fn new() -> Result<Self, IoError> {
        Self::with_source(HardwareInfo::load)
    }
    fn with_source(load: impl FnOnce() -> Result<HardwareInfo, IoError>) -> Result<Self, IoError> {
        let bytes = render(&load()?)?;
        Ok(Self {
            bytes,
            descriptions: Vec::new(),
            grants: Grants::default(),
            next_handle: IO_SESSION_HANDLE_BASE as i64,
        })
    }
    fn info(&self, path: &[u8]) -> Result<FileInfo, IoError> {
        let owner = Credentials::new(UserId(0), GroupId(0));
        match path {
            b"/" => Ok(FileInfo::new(IO_MODE_TYPE_DIR as u32 | 0o555, owner, 0, 1)),
            b"/sysinfo" => Ok(FileInfo::new(
                IO_MODE_TYPE_FILE as u32 | 0o444,
                owner,
                self.bytes.len() as u64,
                2,
            )),
            _ => Err(IoError(IO_INVALID_PATH)),
        }
    }
    fn reap(&mut self) {
        self.descriptions.retain(|description| {
            !description.owners.is_empty() || self.grants.retains(description.handle)
        });
    }
    fn request(&mut self, request: Request<'_>) -> Result<Reply, IoError> {
        if request.pid.0 == 0 || request.session.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let owner = Owner::new(request.session, request.pid);
        match request.operation {
            Operation::Open(path, flags) => {
                if flags != IO_OPEN_READ {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if path != b"/sysinfo" {
                    return Err(IoError(IO_INVALID_PATH));
                }
                if self.descriptions.len() == 32 || self.next_handle == i64::MAX {
                    return Err(IoError(IO_REGISTER_FAILED));
                }
                self.descriptions
                    .try_reserve(1)
                    .map_err(|_| IoError(IO_REGISTER_FAILED))?;
                let handle = Handle(self.next_handle);
                self.descriptions.push(Description::new(handle, owner)?);
                self.next_handle += 1;
                return Ok(Reply::Value(handle.0));
            }
            Operation::FileInfo(path) => return self.info(path).map(Reply::FileInfo),
            Operation::ListDirectory(path, offset) => {
                if path != b"/" {
                    return Err(IoError(IO_INVALID_PATH));
                }
                let mut entries = Vec::new();
                if offset == 0 {
                    entries
                        .try_reserve(1)
                        .map_err(|_| IoError(IO_REGISTER_FAILED))?;
                    entries.push(DirectoryEntry::new(self.info(b"/sysinfo")?, b"sysinfo")?);
                }
                return Ok(Reply::Directory(entries));
            }
            Operation::Mkdir(_) | Operation::Remove(_, _) | Operation::Link { .. } => {
                return Err(IoError(IO_PERMISSION_DENIED));
            }
            Operation::Release => {
                self.disconnect(request.session)?;
                return Ok(Reply::Value(0));
            }
            Operation::InheritCommit { token, target } => {
                self.grants.commit(token, owner, target)?;
                return Ok(Reply::Value(0));
            }
            Operation::InheritAbort(token) => {
                self.grants.abort(token, owner)?;
                self.reap();
                return Ok(Reply::Value(0));
            }
            Operation::InheritClaim { token, flags } => {
                let grant = self.grants.claim(token, owner, flags)?;
                let description = self
                    .descriptions
                    .iter_mut()
                    .find(|entry| entry.handle == grant.handle)
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                if !description.owners.contains(&owner) {
                    if description.owners.len() == 64 {
                        return Err(IoError(IO_REGISTER_FAILED));
                    }
                    description
                        .owners
                        .try_reserve(1)
                        .map_err(|_| IoError(IO_REGISTER_FAILED))?;
                    description.owners.push(owner);
                }
                self.grants.complete(token)?;
                return Ok(Reply::Value(grant.handle.0));
            }
            _ => {}
        }
        let description = self
            .descriptions
            .iter_mut()
            .find(|entry| entry.handle == request.handle && entry.owners.contains(&owner))
            .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
        match request.operation {
            Operation::Read(target) => {
                if request.transfer_flags & !IO_TRANSFER_NONBLOCK != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                let offset = usize::try_from(description.offset)
                    .unwrap_or(usize::MAX)
                    .min(self.bytes.len());
                let count = target.len().min(self.bytes.len() - offset);
                target[..count].copy_from_slice(&self.bytes[offset..offset + count]);
                description.offset += count as u64;
                Ok(Reply::Value(count as i64))
            }
            Operation::Seek { offset, whence } => {
                let base = match whence {
                    IO_SEEK_SET => 0,
                    IO_SEEK_CURRENT => description.offset as i64,
                    IO_SEEK_END => self.bytes.len() as i64,
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                };
                let position = base
                    .checked_add(offset)
                    .filter(|value| *value >= 0)
                    .ok_or(IoError(IO_OPERATION_NOT_SUPPORTED))?;
                description.offset = position as u64;
                Ok(Reply::Value(position))
            }
            Operation::InheritPrepare => {
                self.grants.prepare(description.handle, owner, IO_OPEN_READ)
            }
            Operation::Close => {
                description.owners.retain(|entry| *entry != owner);
                self.grants.close(request.handle, owner);
                self.reap();
                Ok(Reply::Value(0))
            }
            Operation::Write(_) => Err(IoError(IO_PERMISSION_DENIED)),
            _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
    }
}

impl IoComponent for Sysinfofs {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.request(request).map(Some)
    }
    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.grants.release(session);
        for description in &mut self.descriptions {
            description.owners.retain(|owner| owner.session != session);
        }
        self.reap();
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::cell::Cell;
    use hunde::boot::{Device, Ethernet, Framebuffer, Gic, Pci, Plic, Rtc, SdHost};
    use hunde::io::ProcessId;

    fn hardware() -> HardwareInfo {
        let mut info = HardwareInfo::new();
        info.cpu_count = 4;
        info.online_cpu_count = 3;
        info.uart.physical_base = 0x9000000;
        info.uart.mmio_size = 4096;
        info.uart.irq = 33;
        info.uart.kind = 1;
        info.uart.register_width = 4;
        info.memory_start = 0x40000000;
        info.memory_end = 0x80000000;
        info
    }

    fn request(handle: Handle, operation: Operation<'_>) -> Request<'_> {
        Request::new(SessionId(1), ProcessId(10), handle, operation)
    }

    fn open(filesystem: &mut Sysinfofs) -> Handle {
        let reply = filesystem
            .request(request(
                Handle(0),
                Operation::Open(b"/sysinfo", IO_OPEN_READ),
            ))
            .unwrap();
        let Reply::Value(handle) = reply else {
            panic!("expected handle");
        };
        Handle(handle)
    }

    #[test]
    fn initialization_loads_once_and_all_opens_share_the_cached_snapshot() {
        let loads = Cell::new(0);
        let mut filesystem = Sysinfofs::with_source(|| {
            loads.set(loads.get() + 1);
            Ok(hardware())
        })
        .unwrap();
        let expected = filesystem.bytes.clone();
        let first = open(&mut filesystem);
        let second = open(&mut filesystem);
        for handle in [first, second] {
            let mut bytes = [0; 17];
            assert_eq!(
                filesystem.request(request(handle, Operation::Read(&mut bytes))),
                Ok(Reply::Value(17))
            );
            assert_eq!(&bytes, &expected[..17]);
        }
        assert_eq!(loads.get(), 1);
        assert_eq!(filesystem.bytes, expected);
        let text = core::str::from_utf8(&expected).unwrap();
        assert!(text.contains("cpu.online_count=3\n"));
        for peripheral in ["rtc", "framebuffer", "pci", "sd_host", "ethernet"] {
            let key = alloc::format!("{peripheral}.present=0\n");
            assert!(text.contains(&key));
        }
    }

    #[test]
    fn optional_hardware_and_gic_render_the_existing_key_value_contract() {
        let mut info = hardware();
        let mut device = Device::new();
        device.physical_base = 0x4310000;
        device.mmio_size = 4096;
        device.irq = 36;
        let mut rtc = Rtc::new();
        rtc.device = device;
        rtc.kind = 2;
        info.rtc = Some(rtc);
        let mut sd = SdHost::new();
        sd.device = device;
        sd.kind = 2;
        sd.source_clock_hz = 375000000;
        info.sd_host = Some(sd);
        let mut ethernet = Ethernet::new();
        ethernet.device = device;
        ethernet.kind = 7;
        info.ethernet = Some(ethernet);
        let mut pci = Pci::new();
        pci.ecam_physical_base = 0x30000000;
        pci.ecam_size = 0x10000000;
        pci.bus_end = 255;
        info.pci = Some(pci);
        let mut framebuffer = Framebuffer::new();
        framebuffer.physical_base = 0x80000000;
        framebuffer.size = 8192;
        framebuffer.width = 64;
        framebuffer.height = 32;
        framebuffer.pixels_per_scanline = 64;
        framebuffer.pixel_format = 1;
        framebuffer.red_mask = 2;
        framebuffer.green_mask = 3;
        framebuffer.blue_mask = 4;
        framebuffer.reserved_mask = 5;
        info.framebuffer = Some(framebuffer);
        let mut gic = Gic::new();
        gic.distributor_physical_base = 0x8000000;
        gic.redistributor_physical_base = 0x80a0000;
        info.interrupt_controller = InterruptController::Gic { kind: 2, info: gic };
        let bytes = render(&info).unwrap();
        let text = core::str::from_utf8(&bytes).unwrap();
        for expected in [
            "uart.physical_base=150994944\n",
            "uart.irq=33\n",
            "uart.register_width=4\n",
            "rtc.present=1\n",
            "rtc.kind=2\n",
            "rtc.irq=36\n",
            "sd_host.present=1\n",
            "sd_host.source_clock_hz=375000000\n",
            "ethernet.present=1\n",
            "ethernet.mac_kind=7\n",
            "pci.present=1\n",
            "pci.ecam.physical_base=805306368\n",
            "pci.bus.end=255\n",
            "framebuffer.present=1\n",
            "framebuffer.physical_size=8192\n",
            "framebuffer.width=64\n",
            "framebuffer.reserved_mask=5\n",
            "memory.physical_size=1073741824\n",
            "interrupt_controller.kind=2\n",
            "interrupt_controller.redistributor_physical_base=134873088\n",
        ] {
            assert!(text.contains(expected), "missing {expected}");
        }
        assert!(!text.contains("interrupt_controller.plic."));
    }

    #[test]
    fn plic_rendering_does_not_expose_gic_union_members() {
        let mut info = hardware();
        let mut plic = Plic::new();
        plic.physical_base = 0xc000000;
        plic.source_count = 95;
        info.interrupt_controller = InterruptController::Plic {
            kind: 3,
            info: plic,
        };
        let bytes = render(&info).unwrap();
        let text = core::str::from_utf8(&bytes).unwrap();
        assert!(text.contains("interrupt_controller.plic.physical_base=201326592\n"));
        assert!(text.contains("interrupt_controller.plic.source_count=95\n"));
        assert!(!text.contains("interrupt_controller.distributor_physical_base"));
    }

    #[test]
    fn metadata_listing_permissions_and_failed_initialization_are_explicit() {
        assert!(matches!(
            Sysinfofs::with_source(|| Err(IoError(-77))),
            Err(IoError(-77))
        ));
        let mut filesystem = Sysinfofs::with_source(|| Ok(hardware())).unwrap();
        let info = filesystem.info(b"/sysinfo").unwrap();
        assert_eq!(info.size, filesystem.bytes.len() as u64);
        assert_eq!(info.mode & 0o777, 0o444);
        let Reply::Directory(entries) = filesystem
            .request(request(Handle(0), Operation::ListDirectory(b"/", 0)))
            .unwrap()
        else {
            panic!("expected directory");
        };
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].name(), b"sysinfo");
        assert_eq!(
            filesystem.request(request(Handle(0), Operation::ListDirectory(b"/", 1))),
            Ok(Reply::Directory(Vec::new()))
        );
        for operation in [
            Operation::Open(b"/sysinfo", IO_OPEN_WRITE),
            Operation::Mkdir(b"/dir"),
            Operation::Remove(b"/sysinfo", 0),
        ] {
            assert_eq!(
                filesystem.request(request(Handle(0), operation)),
                Err(IoError(IO_PERMISSION_DENIED))
            );
        }
        for path in [b"/missing".as_slice(), b"/../sysinfo", b"/sysinfo\0"] {
            assert!(filesystem.info(path).is_err());
        }
    }

    #[test]
    fn handles_are_authenticated_seekable_and_released_on_disconnect() {
        let mut filesystem = Sysinfofs::with_source(|| Ok(hardware())).unwrap();
        let handle = open(&mut filesystem);
        let mut bytes = [0; 8];
        assert_eq!(
            filesystem.request(Request::new(
                SessionId(2),
                ProcessId(10),
                handle,
                Operation::Read(&mut bytes)
            )),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            filesystem.request(Request::new(
                SessionId(1),
                ProcessId(20),
                handle,
                Operation::Read(&mut bytes)
            )),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            filesystem.request(request(
                handle,
                Operation::Seek {
                    offset: 0,
                    whence: IO_SEEK_END
                }
            )),
            Ok(Reply::Value(filesystem.bytes.len() as i64))
        );
        assert_eq!(
            filesystem.request(request(handle, Operation::Read(&mut bytes))),
            Ok(Reply::Value(0))
        );
        assert_eq!(
            filesystem.request(request(
                handle,
                Operation::Seek {
                    offset: -1,
                    whence: IO_SEEK_SET
                }
            )),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        filesystem.disconnect(SessionId(1)).unwrap();
        assert_eq!(
            filesystem.request(request(handle, Operation::Close)),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert!(filesystem.descriptions.is_empty());
    }

    #[test]
    fn inherited_handles_keep_the_shared_offset_and_survive_parent_close() {
        let mut filesystem = Sysinfofs::with_source(|| Ok(hardware())).unwrap();
        let handle = open(&mut filesystem);
        let mut prefix = [0; 2];
        filesystem
            .request(request(handle, Operation::Read(&mut prefix)))
            .unwrap();
        assert_eq!(&prefix, b"cp");
        let Reply::Inheritance { token, flags } = filesystem
            .request(request(handle, Operation::InheritPrepare))
            .unwrap()
        else {
            panic!("expected grant");
        };
        filesystem
            .request(request(
                handle,
                Operation::InheritCommit {
                    token,
                    target: ProcessId(20),
                },
            ))
            .unwrap();
        filesystem
            .request(request(handle, Operation::Close))
            .unwrap();
        assert_eq!(
            filesystem.request(Request::new(
                SessionId(2),
                ProcessId(20),
                Handle(0),
                Operation::InheritClaim { token, flags }
            )),
            Ok(Reply::Value(handle.0))
        );
        let mut bytes = [0; 4];
        assert_eq!(
            filesystem.request(Request::new(
                SessionId(2),
                ProcessId(20),
                handle,
                Operation::Read(&mut bytes)
            )),
            Ok(Reply::Value(4))
        );
        assert_eq!(&bytes, b"u.co");
        filesystem.disconnect(SessionId(2)).unwrap();
        assert!(filesystem.descriptions.is_empty());
    }

    #[test]
    fn handle_limit_close_and_disconnect_do_not_invalidate_cached_contents() {
        let mut filesystem = Sysinfofs::with_source(|| Ok(hardware())).unwrap();
        let expected = filesystem.bytes.clone();
        let first = open(&mut filesystem);
        for _ in 1..32 {
            open(&mut filesystem);
        }
        assert_eq!(
            filesystem.request(request(
                Handle(0),
                Operation::Open(b"/sysinfo", IO_OPEN_READ)
            )),
            Err(IoError(IO_REGISTER_FAILED))
        );
        filesystem
            .request(request(first, Operation::Close))
            .unwrap();
        open(&mut filesystem);
        filesystem.disconnect(SessionId(1)).unwrap();
        assert!(filesystem.descriptions.is_empty());
        assert_eq!(filesystem.bytes, expected);
        open(&mut filesystem);
    }
}
