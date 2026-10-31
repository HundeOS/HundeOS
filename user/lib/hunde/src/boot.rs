#[cfg(any(feature = "alloc", test))]
use crate::abi::IoError;
use crate::abi::StatusResult;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Device {
    pub physical_base: u64,
    pub mmio_size: u64,
    pub irq: u64,
}

impl Device {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn from_parts(physical_base: u64, mmio_size: u64, irq: u64) -> Self {
        Self {
            physical_base,
            mmio_size,
            irq,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Uart {
    pub physical_base: u64,
    pub mmio_size: u64,
    pub irq: u64,
    pub kind: u64,
    pub register_shift: u64,
    pub register_width: u64,
}

impl Uart {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn from_wire_bytes(bytes: &[u8]) -> Result<Self, crate::abi::IoError> {
        if bytes.len() != core::mem::size_of::<Self>() {
            return Err(crate::abi::IoError(crate::protocol::SERVICE_INVALID_BUFFER));
        }
        let mut fields = bytes
            .as_chunks::<8>()
            .0
            .iter()
            .map(|chunk| u64::from_ne_bytes(*chunk));
        Ok(Self {
            physical_base: fields.next().unwrap_or_default(),
            mmio_size: fields.next().unwrap_or_default(),
            irq: fields.next().unwrap_or_default(),
            kind: fields.next().unwrap_or_default(),
            register_shift: fields.next().unwrap_or_default(),
            register_width: fields.next().unwrap_or_default(),
        })
    }

    pub fn wire_bytes(&self) -> &[u8] {
        unsafe {
            core::slice::from_raw_parts(
                (self as *const Self).cast::<u8>(),
                core::mem::size_of::<Self>(),
            )
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Rtc {
    pub device: Device,
    pub kind: u64,
}

impl Rtc {
    pub fn new() -> Self {
        Self::default()
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Framebuffer {
    pub physical_base: u64,
    pub size: u64,
    pub width: u64,
    pub height: u64,
    pub pixels_per_scanline: u64,
    pub pixel_format: u64,
    pub red_mask: u64,
    pub green_mask: u64,
    pub blue_mask: u64,
    pub reserved_mask: u64,
}

impl Framebuffer {
    pub fn new() -> Self {
        Self::default()
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Pci {
    pub ecam_physical_base: u64,
    pub ecam_size: u64,
    pub bus_start: u64,
    pub bus_end: u64,
}

impl Pci {
    pub fn new() -> Self {
        Self::default()
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct SdHost {
    pub device: Device,
    pub kind: u64,
    pub source_clock_hz: u64,
}

impl SdHost {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn from_wire_bytes(bytes: &[u8]) -> Result<Self, crate::abi::IoError> {
        if bytes.len() != core::mem::size_of::<Self>() {
            return Err(crate::abi::IoError(crate::protocol::SERVICE_INVALID_BUFFER));
        }
        let mut fields = bytes
            .as_chunks::<8>()
            .0
            .iter()
            .map(|chunk| u64::from_ne_bytes(*chunk));
        Ok(Self {
            device: Device::from_parts(
                fields.next().unwrap_or_default(),
                fields.next().unwrap_or_default(),
                fields.next().unwrap_or_default(),
            ),
            kind: fields.next().unwrap_or_default(),
            source_clock_hz: fields.next().unwrap_or_default(),
        })
    }

    pub fn wire_bytes(&self) -> [u8; core::mem::size_of::<Self>()] {
        let mut bytes = [0; core::mem::size_of::<Self>()];
        for (index, field) in [
            self.device.physical_base,
            self.device.mmio_size,
            self.device.irq,
            self.kind,
            self.source_clock_hz,
        ]
        .into_iter()
        .enumerate()
        {
            bytes[index * 8..(index + 1) * 8].copy_from_slice(&field.to_ne_bytes());
        }
        bytes
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Ethernet {
    pub device: Device,
    pub kind: u64,
}

impl Ethernet {
    pub fn new() -> Self {
        Self::default()
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Gic {
    pub distributor_physical_base: u64,
    pub redistributor_physical_base: u64,
    pub cpu_interface_physical_base: u64,
}

impl Gic {
    pub fn new() -> Self {
        Self::default()
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Plic {
    pub physical_base: u64,
    pub source_count: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VirtioMmio {
    pub physical_base: u64,
    pub slot_size: u64,
    pub slot_count: u64,
    pub first_irq: u64,
}

#[cfg(feature = "alloc")]
pub struct VirtioDeviceConfiguration {
    pub hardware: VirtioMmio,
    pub slot: u64,
}

#[cfg(feature = "alloc")]
impl VirtioDeviceConfiguration {
    pub fn new(hardware: VirtioMmio, slot: u64) -> Result<Self, IoError> {
        if slot >= hardware.slot_count {
            return Err(IoError(crate::protocol::SERVICE_INVALID_BUFFER));
        }
        Ok(Self { hardware, slot })
    }

    pub fn from_wire_bytes(bytes: &[u8]) -> Result<Self, IoError> {
        if bytes.len() != 40 {
            return Err(IoError(crate::protocol::SERVICE_INVALID_BUFFER));
        }
        let mut fields = bytes
            .as_chunks::<8>()
            .0
            .iter()
            .copied()
            .map(u64::from_ne_bytes);
        let hardware = VirtioMmio {
            physical_base: fields.next().unwrap_or_default(),
            slot_size: fields.next().unwrap_or_default(),
            slot_count: fields.next().unwrap_or_default(),
            first_irq: fields.next().unwrap_or_default(),
        };
        Self::new(hardware, fields.next().unwrap_or_default())
    }

    pub fn wire_bytes(&self) -> [u8; 40] {
        let mut bytes = [0; 40];
        for (index, value) in [
            self.hardware.physical_base,
            self.hardware.slot_size,
            self.hardware.slot_count,
            self.hardware.first_irq,
            self.slot,
        ]
        .into_iter()
        .enumerate()
        {
            bytes[index * 8..(index + 1) * 8].copy_from_slice(&value.to_ne_bytes());
        }
        bytes
    }
}

#[cfg(feature = "alloc")]
pub struct BlockDriverConfiguration {
    pub hardware: VirtioMmio,
    pub slot: u64,
    pub path: alloc::vec::Vec<u8>,
}

#[cfg(feature = "alloc")]
impl BlockDriverConfiguration {
    pub fn new(hardware: VirtioMmio, slot: u64, path: &[u8]) -> Result<Self, IoError> {
        if slot >= hardware.slot_count || path.is_empty() || path.len() > 255 {
            return Err(IoError(crate::protocol::SERVICE_INVALID_BUFFER));
        }
        Ok(Self {
            hardware,
            slot,
            path: path.into(),
        })
    }

    pub fn wire_bytes(&self) -> alloc::vec::Vec<u8> {
        let mut bytes = alloc::vec::Vec::with_capacity(48 + self.path.len());
        for value in [
            self.hardware.physical_base,
            self.hardware.slot_size,
            self.hardware.slot_count,
            self.hardware.first_irq,
            self.slot,
            self.path.len() as u64,
        ] {
            bytes.extend_from_slice(&value.to_le_bytes());
        }
        bytes.extend_from_slice(&self.path);
        bytes
    }

    pub fn from_wire_bytes(bytes: &[u8]) -> Result<Self, IoError> {
        if bytes.len() < 48 {
            return Err(IoError(crate::protocol::SERVICE_INVALID_BUFFER));
        }
        let mut values = bytes[..48]
            .as_chunks::<8>()
            .0
            .iter()
            .map(|chunk| u64::from_le_bytes(*chunk));
        let hardware = VirtioMmio {
            physical_base: values.next().unwrap_or_default(),
            slot_size: values.next().unwrap_or_default(),
            slot_count: values.next().unwrap_or_default(),
            first_irq: values.next().unwrap_or_default(),
        };
        let slot = values.next().unwrap_or_default();
        let length = usize::try_from(values.next().unwrap_or_default())
            .map_err(|_| IoError(crate::protocol::SERVICE_INVALID_BUFFER))?;
        if length != bytes.len() - 48 {
            return Err(IoError(crate::protocol::SERVICE_INVALID_BUFFER));
        }
        Self::new(hardware, slot, &bytes[48..])
    }
}

impl Plic {
    pub fn new() -> Self {
        Self::default()
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InterruptController {
    Other(u64),
    Gic { kind: u64, info: Gic },
    Plic { kind: u64, info: Plic },
}

#[derive(Clone, Debug)]
pub struct HardwareInfo {
    pub cpu_count: u64,
    pub online_cpu_count: u64,
    pub uart: Uart,
    pub rtc: Option<Rtc>,
    pub framebuffer: Option<Framebuffer>,
    pub pci: Option<Pci>,
    pub sd_host: Option<SdHost>,
    pub ethernet: Option<Ethernet>,
    pub virtio_mmio: Option<VirtioMmio>,
    pub memory_start: u64,
    pub memory_end: u64,
    pub interrupt_controller: InterruptController,
}

impl Default for HardwareInfo {
    fn default() -> Self {
        Self {
            cpu_count: 0,
            online_cpu_count: 0,
            uart: Uart::new(),
            rtc: None,
            framebuffer: None,
            pci: None,
            sd_host: None,
            ethernet: None,
            virtio_mmio: None,
            memory_start: 0,
            memory_end: 0,
            interrupt_controller: InterruptController::Other(0),
        }
    }
}

impl HardwareInfo {
    pub fn new() -> Self {
        Self::default()
    }
    #[cfg(feature = "alloc")]
    pub fn load() -> Result<Self, IoError> {
        let operations = unsafe { &*crate::runtime::operations()? };
        let load = operations
            .boot_info
            .ok_or(IoError(crate::protocol::IO_NO_DEVICE_FOUND))?;
        unsafe { load() }.into_result()
    }
}

#[repr(C)]
#[derive(Default)]
struct RawHardwareInfo {
    cpu_count: u64,
    online_cpu_count: u64,
    uart: Uart,
    rtc_present: u64,
    rtc: Rtc,
    framebuffer_present: u64,
    framebuffer: Framebuffer,
    pci_present: u64,
    pci: Pci,
    sd_host_present: u64,
    sd_host: SdHost,
    ethernet_present: u64,
    ethernet: Ethernet,
    memory_start: u64,
    memory_end: u64,
    controller_kind: u64,
    gic_present: u64,
    gic: Gic,
    plic_present: u64,
    plic: Plic,
    virtio_mmio_present: u64,
    virtio_mmio: VirtioMmio,
}

#[repr(C)]
pub struct NativeResult {
    status: StatusResult,
    info: RawHardwareInfo,
}

#[cfg(any(feature = "alloc", test))]
fn optional<Value>(present: u64, value: Value) -> Result<Option<Value>, IoError> {
    match present {
        0 => Ok(None),
        1 => Ok(Some(value)),
        _ => Err(IoError(crate::protocol::IO_PANIC)),
    }
}

impl NativeResult {
    #[cfg(any(feature = "alloc", test))]
    fn into_result(self) -> Result<HardwareInfo, IoError> {
        self.status.into_result()?;
        let info = self.info;
        if info.memory_end < info.memory_start {
            return Err(IoError(crate::protocol::IO_PANIC));
        }
        let controller = match (
            optional(info.gic_present, info.gic)?,
            optional(info.plic_present, info.plic)?,
        ) {
            (Some(gic), None) => InterruptController::Gic {
                kind: info.controller_kind,
                info: gic,
            },
            (None, Some(plic)) => InterruptController::Plic {
                kind: info.controller_kind,
                info: plic,
            },
            (None, None) => InterruptController::Other(info.controller_kind),
            _ => return Err(IoError(crate::protocol::IO_PANIC)),
        };
        Ok(HardwareInfo {
            cpu_count: info.cpu_count,
            online_cpu_count: info.online_cpu_count,
            uart: info.uart,
            rtc: optional(info.rtc_present, info.rtc)?,
            framebuffer: optional(info.framebuffer_present, info.framebuffer)?,
            pci: optional(info.pci_present, info.pci)?,
            sd_host: optional(info.sd_host_present, info.sd_host)?,
            ethernet: optional(info.ethernet_present, info.ethernet)?,
            virtio_mmio: optional(info.virtio_mmio_present, info.virtio_mmio)?,
            memory_start: info.memory_start,
            memory_end: info.memory_end,
            interrupt_controller: controller,
        })
    }
}

const _: () = {
    assert!(core::mem::size_of::<RawHardwareInfo>() == 440);
    assert!(core::mem::size_of::<NativeResult>() == 472);
    assert!(core::mem::offset_of!(NativeResult, info) == 32);
};

#[cfg(test)]
mod tests {
    use super::*;

    #[cfg(feature = "alloc")]
    #[test]
    fn virtio_device_configuration_round_trips_and_checks_slot() {
        let hardware = VirtioMmio {
            physical_base: 0x1000_1000,
            slot_size: 0x200,
            slot_count: 64,
            first_irq: 48,
        };
        let configuration = VirtioDeviceConfiguration::new(hardware, 5).unwrap();
        let bytes = configuration.wire_bytes();
        let parsed = VirtioDeviceConfiguration::from_wire_bytes(&bytes).unwrap();
        assert_eq!(parsed.hardware, hardware);
        assert_eq!(parsed.slot, 5);
        assert!(VirtioDeviceConfiguration::new(hardware, 64).is_err());
        assert!(VirtioDeviceConfiguration::from_wire_bytes(&bytes[..39]).is_err());
    }

    #[cfg(feature = "alloc")]
    #[test]
    fn block_driver_configuration_round_trips_and_rejects_malformed_lengths() {
        let hardware = VirtioMmio {
            physical_base: 0x1000_1000,
            slot_size: 0x200,
            slot_count: 64,
            first_irq: 32,
        };
        let configuration = BlockDriverConfiguration::new(hardware, 5, b"/dev/virtio1").unwrap();
        let wire = configuration.wire_bytes();
        let parsed = BlockDriverConfiguration::from_wire_bytes(&wire).unwrap();
        assert_eq!(parsed.hardware, hardware);
        assert_eq!(parsed.slot, 5);
        assert_eq!(parsed.path, b"/dev/virtio1");
        assert!(BlockDriverConfiguration::from_wire_bytes(&wire[..wire.len() - 1]).is_err());
        assert!(BlockDriverConfiguration::from_wire_bytes(&wire[..47]).is_err());
        assert!(BlockDriverConfiguration::new(hardware, 64, b"/dev/virtio1").is_err());
    }

    #[test]
    fn uart_wire_bytes_round_trip_and_reject_wrong_size() {
        let uart = Uart {
            physical_base: 0x1020_3000,
            mmio_size: 0x1000,
            irq: 33,
            kind: 2,
            register_shift: 2,
            register_width: 4,
        };
        assert_eq!(Uart::from_wire_bytes(uart.wire_bytes()), Ok(uart));
        assert_eq!(
            Uart::from_wire_bytes(&uart.wire_bytes()[..47]),
            Err(IoError(crate::protocol::SERVICE_INVALID_BUFFER))
        );
    }

    #[test]
    fn sd_host_wire_bytes_round_trip_and_reject_wrong_size() {
        let host = SdHost {
            device: Device::from_parts(0x1004_0000, 0x1000, 37),
            kind: crate::protocol::SD_HOST_KIND_PCI,
            source_clock_hz: 0,
        };
        assert_eq!(SdHost::from_wire_bytes(&host.wire_bytes()), Ok(host));
        assert_eq!(
            SdHost::from_wire_bytes(&host.wire_bytes()[..39]),
            Err(IoError(crate::protocol::SERVICE_INVALID_BUFFER))
        );
    }

    fn result(info: RawHardwareInfo) -> NativeResult {
        NativeResult {
            status: StatusResult::success(),
            info,
        }
    }

    #[test]
    fn boot_snapshot_decodes_optional_peripherals_without_c_optional_layouts() {
        let raw = RawHardwareInfo {
            cpu_count: 4,
            online_cpu_count: 3,
            memory_start: 0x40000000,
            memory_end: 0x80000000,
            rtc_present: 1,
            framebuffer_present: 1,
            pci_present: 1,
            sd_host_present: 1,
            ethernet_present: 1,
            virtio_mmio_present: 1,
            virtio_mmio: VirtioMmio {
                physical_base: 0x1000_1000,
                slot_size: 0x1000,
                slot_count: 64,
                first_irq: 32,
            },
            ..RawHardwareInfo::default()
        };
        let info = result(raw).into_result().unwrap();
        assert_eq!(info.cpu_count, 4);
        assert_eq!(info.online_cpu_count, 3);
        assert!(info.rtc.is_some());
        assert!(info.framebuffer.is_some());
        assert!(info.pci.is_some());
        assert!(info.sd_host.is_some());
        assert!(info.ethernet.is_some());
        assert!(info.virtio_mmio.is_some());
        assert_eq!(info.virtio_mmio.unwrap().slot_count, 64);
        let absent = result(RawHardwareInfo::default()).into_result().unwrap();
        assert!(absent.rtc.is_none());
        assert!(absent.framebuffer.is_none());
        assert!(absent.pci.is_none());
        assert!(absent.sd_host.is_none());
        assert!(absent.ethernet.is_none());
        assert!(absent.virtio_mmio.is_none());
    }

    #[test]
    fn controller_details_are_selected_by_normalized_tags_not_c_union_layouts() {
        let raw = RawHardwareInfo {
            controller_kind: 1,
            gic_present: 1,
            ..RawHardwareInfo::default()
        };
        assert!(matches!(
            result(raw).into_result().unwrap().interrupt_controller,
            InterruptController::Gic { kind: 1, .. }
        ));
        let raw = RawHardwareInfo {
            controller_kind: 3,
            plic_present: 1,
            ..RawHardwareInfo::default()
        };
        assert!(matches!(
            result(raw).into_result().unwrap().interrupt_controller,
            InterruptController::Plic { kind: 3, .. }
        ));
        let raw = RawHardwareInfo {
            gic_present: 1,
            plic_present: 1,
            ..RawHardwareInfo::default()
        };
        assert!(result(raw).into_result().is_err());
    }

    #[test]
    fn malformed_tags_memory_ranges_and_native_errors_fail_closed() {
        for raw in [
            RawHardwareInfo {
                rtc_present: 2,
                ..RawHardwareInfo::default()
            },
            RawHardwareInfo {
                gic_present: 2,
                ..RawHardwareInfo::default()
            },
            RawHardwareInfo {
                memory_start: 1,
                memory_end: 0,
                ..RawHardwareInfo::default()
            },
        ] {
            assert!(result(raw).into_result().is_err());
        }
        let failure = NativeResult {
            status: StatusResult::from_result(Err(IoError(-77))),
            info: RawHardwareInfo::default(),
        };
        assert!(matches!(failure.into_result(), Err(IoError(-77))));
    }
}
