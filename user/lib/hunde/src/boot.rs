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
            memory_start: info.memory_start,
            memory_end: info.memory_end,
            interrupt_controller: controller,
        })
    }
}

const _: () = {
    assert!(core::mem::size_of::<RawHardwareInfo>() == 400);
    assert!(core::mem::size_of::<NativeResult>() == 432);
    assert!(core::mem::offset_of!(NativeResult, info) == 32);
};

#[cfg(test)]
mod tests {
    use super::*;

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
        let absent = result(RawHardwareInfo::default()).into_result().unwrap();
        assert!(absent.rtc.is_none());
        assert!(absent.framebuffer.is_none());
        assert!(absent.pci.is_none());
        assert!(absent.sd_host.is_none());
        assert!(absent.ethernet.is_none());
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
