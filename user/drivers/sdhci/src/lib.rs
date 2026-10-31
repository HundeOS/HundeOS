#![no_std]

extern crate alloc;

use alloc::rc::Rc;
use core::cell::RefCell;
use hunde::abi::IoError;
use hunde::block_driver::{Buffer, Server, Transfer, Wait};
use hunde::boot::SdHost;
use hunde::dma::Area;
use hunde::mmio::Mapping;
use hunde::protocol::{
    SD_HOST_KIND_BCM2835, SD_HOST_KIND_CV181X, SD_HOST_KIND_PCI, SERVICE_BLOCK_REQUEST_FAILED,
    SERVICE_INVALID_BUFFER,
};
use hunde::wuff::{DriverComponent, DriverProgress};

const BLOCK_SIZE: u64 = 512;
const DEVICE_PATH: &[u8] = b"/dev/mmc0";
const POLL_LIMIT: usize = 1_000_000;
const APP_COMMAND_RETRIES: usize = 1_000;
const COMMAND_TIMEOUT_USEC: u64 = 1_000_000;

const BLOCK_SIZE_REGISTER: usize = 0x04;
const BLOCK_COUNT_REGISTER: usize = 0x06;
const ARGUMENT: usize = 0x08;
const TRANSFER_MODE: usize = 0x0c;
const COMMAND: usize = 0x0e;
const RESPONSE: usize = 0x10;
const BUFFER: usize = 0x20;
const PRESENT_STATE: usize = 0x24;
const POWER_CONTROL: usize = 0x29;
const CLOCK_CONTROL: usize = 0x2c;
const TIMEOUT_CONTROL: usize = 0x2e;
const SOFTWARE_RESET: usize = 0x2f;
const INT_STATUS: usize = 0x30;
const INT_ENABLE: usize = 0x34;
const SIGNAL_ENABLE: usize = 0x38;
const CAPABILITIES: usize = 0x40;
const CV181X_VENDOR_MSHC_CTRL: usize = 0x200;
const CV181X_VENDOR_PHY_TX_RX_DLY: usize = 0x240;
const CV181X_VENDOR_PHY_CONFIG: usize = 0x24c;
const CV181X_MSHC_CTRL_DEFAULT: u32 = 0x0000_0302;
const CV181X_PHY_TX_RX_DLY_DEFAULT: u32 = 0x0100_0100;
const CV181X_PHY_CONFIG_ENABLE: u32 = 1;

const CMD_INHIBIT: u32 = 1;
const DAT_INHIBIT: u32 = 2;
const CARD_INSERTED: u32 = 0x10000;
const INTERNAL_CLOCK_ENABLE: u16 = 1;
const INTERNAL_CLOCK_STABLE: u16 = 2;
const SD_CLOCK_ENABLE: u16 = 4;
const RESET_ALL: u8 = 1;
const RESET_COMMAND: u8 = 2;
const RESET_DATA: u8 = 4;
const COMMAND_COMPLETE: u32 = 1;
const TRANSFER_COMPLETE: u32 = 2;
const BUFFER_READ_READY: u32 = 0x20;
const INTERRUPT_ERROR: u32 = 0x8000;
const INTERRUPT_SIGNALS: u32 = 0xffff_8023;
const TRANSFER_READ: u16 = 0x10;
const TRANSFER_MULTI_BLOCK: u16 = 0x20;
const TRANSFER_BLOCK_COUNT: u16 = 2;
const TRANSFER_AUTO_STOP: u16 = 4;
const OCR_READY: u32 = 0x8000_0000;
const OCR_HIGH_CAPACITY: u32 = 0x4000_0000;

#[derive(Clone, Copy, PartialEq, Eq)]
enum ResponseKind {
    None,
    R1,
    R1Busy,
    R2,
    R3,
    R6,
    R7,
}

struct CommandSpec {
    index: u16,
    argument: u32,
    response: ResponseKind,
    transfer_mode: u16,
    data: bool,
}

impl CommandSpec {
    fn new(index: u16, argument: u32, response: ResponseKind) -> Self {
        Self {
            index,
            argument,
            response,
            transfer_mode: 0,
            data: false,
        }
    }

    fn read(index: u16, argument: u32, count: u64) -> Self {
        let mut command = Self::new(index, argument, ResponseKind::R1);
        command.data = true;
        command.transfer_mode = TRANSFER_READ
            | if count > 1 {
                TRANSFER_MULTI_BLOCK | TRANSFER_BLOCK_COUNT | TRANSFER_AUTO_STOP
            } else {
                0
            };
        command
    }

    fn value(&self) -> u16 {
        let response = match self.response {
            ResponseKind::None => 0,
            ResponseKind::R2 => 1 | 8,
            ResponseKind::R3 => 2,
            ResponseKind::R1Busy => 3 | 8 | 16,
            ResponseKind::R1 | ResponseKind::R6 | ResponseKind::R7 => 2 | 8 | 16,
        };
        (self.index << 8) | response | if self.data { 0x20 } else { 0 }
    }

    fn inhibit(&self) -> u32 {
        CMD_INHIBIT
            | if self.data || self.response == ResponseKind::R1Busy {
                DAT_INHIBIT
            } else {
                0
            }
    }
}

fn card_sectors(response: [u32; 4]) -> u64 {
    if (response[0] >> 30) & 3 == 1 {
        let size = (u64::from(response[1] & 0x3f) << 16) | u64::from(response[2] >> 16);
        return (size + 1) * 1024;
    }
    let read_length = (response[1] >> 16) & 15;
    let size = ((response[1] & 0x3ff) << 2) | (response[2] >> 30);
    let multiplier = (response[2] >> 15) & 7;
    ((u64::from(size) + 1) * (1_u64 << (multiplier + 2)) * (1_u64 << read_length)) / BLOCK_SIZE
}

fn clock_divisor(base_hz: u64, target_hz: u64) -> u16 {
    let mut divisor = base_hz.div_ceil(target_hz);
    if divisor <= 1 {
        return 0;
    }
    divisor += divisor & 1;
    divisor = divisor.min(2046);
    let encoded = (divisor / 2) as u16;
    ((encoded & 0xff) << 8) | ((encoded & 0x300) >> 2)
}

#[derive(Default)]
struct PendingStatus(u32);

impl PendingStatus {
    fn record(&mut self, status: u32) {
        self.0 |= status;
    }

    fn take(&mut self, mask: u32) -> Result<bool, IoError> {
        if self.0 & INTERRUPT_ERROR != 0 {
            self.0 = 0;
            return Err(Device::failure());
        }
        if self.0 & mask != mask {
            return Ok(false);
        }
        self.0 &= !mask;
        Ok(true)
    }

    fn clear(&mut self) {
        self.0 = 0;
    }
}

struct Device {
    registers: Mapping,
    kind: u64,
    irq: u64,
    source_clock_hz: u64,
    sectors: u64,
    high_capacity: bool,
    pending_status: PendingStatus,
}

impl Device {
    fn new(host: SdHost) -> Result<Self, IoError> {
        let length = usize::try_from(host.device.mmio_size).map_err(|_| Self::failure())?;
        if host.device.irq == 0
            || !matches!(
                host.kind,
                SD_HOST_KIND_PCI | SD_HOST_KIND_BCM2835 | SD_HOST_KIND_CV181X
            )
            || length
                < if host.kind == SD_HOST_KIND_CV181X {
                    0x250
                } else {
                    0x100
                }
        {
            return Err(Self::failure());
        }
        let mut device = Self {
            registers: Mapping::map(host.device.physical_base, length)?,
            kind: host.kind,
            irq: host.device.irq,
            source_clock_hz: host.source_clock_hz,
            sectors: 0,
            high_capacity: false,
            pending_status: PendingStatus::default(),
        };
        device.setup()?;
        device.init_card()?;
        device
            .registers
            .write_u32(SIGNAL_ENABLE, INTERRUPT_SIGNALS)?;
        Ok(device)
    }

    fn failure() -> IoError {
        IoError(SERVICE_BLOCK_REQUEST_FAILED)
    }

    fn wait8_clear(&self, offset: usize, mask: u8) -> Result<(), IoError> {
        for _ in 0..POLL_LIMIT {
            if self.registers.read_u8(offset)? & mask == 0 {
                return Ok(());
            }
        }
        Err(Self::failure())
    }

    fn wait16_set(&self, offset: usize, mask: u16) -> Result<(), IoError> {
        for _ in 0..POLL_LIMIT {
            if self.registers.read_u16(offset)? & mask == mask {
                return Ok(());
            }
        }
        Err(Self::failure())
    }

    fn wait32_clear(&self, offset: usize, mask: u32) -> Result<(), IoError> {
        for _ in 0..POLL_LIMIT {
            if self.registers.read_u32(offset)? & mask == 0 {
                return Ok(());
            }
        }
        Err(Self::failure())
    }

    fn reset(&mut self, mask: u8) -> Result<(), IoError> {
        self.registers.write_u8(SOFTWARE_RESET, mask)?;
        self.wait8_clear(SOFTWARE_RESET, mask)
    }

    fn set_clock(&mut self, target_hz: u64) -> Result<(), IoError> {
        let base_hz = if self.source_clock_hz != 0 {
            self.source_clock_hz
        } else {
            let mhz = u64::from((self.registers.read_u32(CAPABILITIES)? >> 8) & 0xff);
            (if mhz == 0 { 100 } else { mhz }) * 1_000_000
        };
        let clock = clock_divisor(base_hz, target_hz) | INTERNAL_CLOCK_ENABLE;
        self.registers.write_u16(CLOCK_CONTROL, 0)?;
        self.registers.write_u16(CLOCK_CONTROL, clock)?;
        self.wait16_set(CLOCK_CONTROL, INTERNAL_CLOCK_STABLE)?;
        self.registers
            .write_u16(CLOCK_CONTROL, clock | SD_CLOCK_ENABLE)
    }

    fn setup(&mut self) -> Result<(), IoError> {
        self.reset(RESET_ALL)?;
        if self.kind == SD_HOST_KIND_CV181X {
            let control = self.registers.read_u32(CV181X_VENDOR_MSHC_CTRL)?;
            self.registers
                .write_u32(CV181X_VENDOR_MSHC_CTRL, control | CV181X_MSHC_CTRL_DEFAULT)?;
            self.registers
                .write_u32(CV181X_VENDOR_PHY_TX_RX_DLY, CV181X_PHY_TX_RX_DLY_DEFAULT)?;
            self.registers
                .write_u32(CV181X_VENDOR_PHY_CONFIG, CV181X_PHY_CONFIG_ENABLE)?;
        }
        self.registers.write_u32(INT_STATUS, u32::MAX)?;
        self.registers.write_u32(INT_ENABLE, u32::MAX)?;
        self.registers.write_u32(SIGNAL_ENABLE, 0)?;
        self.registers.write_u8(TIMEOUT_CONTROL, 0x0e)?;
        self.registers.write_u8(POWER_CONTROL, 0x0f)?;
        if self.registers.read_u32(PRESENT_STATE)? & CARD_INSERTED == 0 {
            return Err(Self::failure());
        }
        self.set_clock(400_000)?;
        hunde::block_driver::sleep_usec(5_000)
    }

    fn poll_status(&mut self, mask: u32) -> Result<(), IoError> {
        for _ in 0..POLL_LIMIT {
            let status = self.registers.read_u32(INT_STATUS)?;
            if status & INTERRUPT_ERROR != 0 {
                self.registers.write_u32(INT_STATUS, status)?;
                return Err(Self::failure());
            }
            if status & mask == mask {
                self.registers.write_u32(INT_STATUS, mask)?;
                return Ok(());
            }
        }
        Err(Self::failure())
    }

    fn submit_command(&mut self, command: &CommandSpec) -> Result<(), IoError> {
        self.wait32_clear(PRESENT_STATE, command.inhibit())?;
        self.pending_status.clear();
        self.registers.write_u32(INT_STATUS, u32::MAX)?;
        self.registers
            .write_u16(TRANSFER_MODE, command.transfer_mode)?;
        self.registers.write_u32(ARGUMENT, command.argument)?;
        Area::boundary();
        self.registers.write_u16(COMMAND, command.value())
    }

    fn response(&self, kind: ResponseKind) -> Result<[u32; 4], IoError> {
        let mut response = [0; 4];
        if kind == ResponseKind::R2 {
            for (index, value) in response.iter_mut().enumerate() {
                let offset = RESPONSE + (3 - index) * 4;
                let extra = if index == 3 {
                    0
                } else {
                    u32::from(self.registers.read_u8(offset - 1)?)
                };
                *value = (self.registers.read_u32(offset)? << 8) | extra;
            }
        } else if kind != ResponseKind::None {
            response[0] = self.registers.read_u32(RESPONSE)?;
        }
        Ok(response)
    }

    fn bootstrap_command(&mut self, command: &CommandSpec) -> Result<[u32; 4], IoError> {
        let result = self.submit_command(command).and_then(|()| {
            self.poll_status(COMMAND_COMPLETE)?;
            let response = self.response(command.response)?;
            if command.response == ResponseKind::R1Busy {
                self.poll_status(TRANSFER_COMPLETE)?;
            }
            Ok(response)
        });
        if result.is_err() {
            let _ = self.reset(RESET_COMMAND | RESET_DATA);
            let _ = self.registers.write_u32(INT_STATUS, u32::MAX);
        }
        result
    }

    fn init_card(&mut self) -> Result<(), IoError> {
        let _ = self.bootstrap_command(&CommandSpec::new(0, 0, ResponseKind::None));
        let _ = self.bootstrap_command(&CommandSpec::new(8, 0x1aa, ResponseKind::R7));
        let mut ready = false;
        for _ in 0..APP_COMMAND_RETRIES {
            self.bootstrap_command(&CommandSpec::new(55, 0, ResponseKind::R1))?;
            let response =
                self.bootstrap_command(&CommandSpec::new(41, 0x4030_0000, ResponseKind::R3))?;
            if response[0] & OCR_READY != 0 {
                self.high_capacity = response[0] & OCR_HIGH_CAPACITY != 0;
                ready = true;
                break;
            }
        }
        if !ready {
            return Err(Self::failure());
        }
        self.bootstrap_command(&CommandSpec::new(2, 0, ResponseKind::R2))?;
        let rca =
            self.bootstrap_command(&CommandSpec::new(3, 0, ResponseKind::R6))?[0] & 0xffff_0000;
        let csd = self.bootstrap_command(&CommandSpec::new(9, rca, ResponseKind::R2))?;
        self.sectors = card_sectors(csd);
        if self.sectors == 0 {
            return Err(Self::failure());
        }
        self.bootstrap_command(&CommandSpec::new(7, rca, ResponseKind::R1Busy))?;
        self.set_clock(25_000_000)?;
        self.bootstrap_command(&CommandSpec::new(16, BLOCK_SIZE as u32, ResponseKind::R1))?;
        Ok(())
    }

    fn acknowledge(&mut self) -> Result<(), IoError> {
        let status = self.registers.read_u32(INT_STATUS)?;
        if status != 0 {
            self.registers.write_u32(INT_STATUS, status)?;
            Area::boundary();
            self.pending_status.record(status);
        }
        Ok(())
    }

    fn consume(&mut self, mask: u32) -> Result<bool, IoError> {
        self.acknowledge()?;
        self.pending_status.take(mask)
    }

    fn recover(&mut self) {
        let _ = self.reset(RESET_COMMAND | RESET_DATA);
        let _ = self.registers.write_u32(INT_STATUS, u32::MAX);
        self.pending_status.clear();
    }
}

struct Transfers {
    device: Rc<RefCell<Device>>,
}

impl Transfers {
    fn new(device: Rc<RefCell<Device>>) -> Self {
        Self { device }
    }

    fn await_status(&self, mask: u32, wait: &mut Wait) -> Result<(), IoError> {
        let deadline = hunde::block_driver::monotonic_usec().saturating_add(COMMAND_TIMEOUT_USEC);
        loop {
            if self.device.borrow_mut().consume(mask)? {
                return Ok(());
            }
            if hunde::block_driver::monotonic_usec() >= deadline {
                return Err(Device::failure());
            }
            wait.irq()?;
        }
    }

    fn read(
        &mut self,
        sector: u64,
        count: u64,
        buffer: &Buffer,
        wait: &mut Wait,
    ) -> Result<(), IoError> {
        let length = count
            .checked_mul(BLOCK_SIZE)
            .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        buffer.validate_read_length(length)?;
        {
            let mut device = self.device.borrow_mut();
            if count > u16::MAX as u64
                || sector >= device.sectors
                || count > device.sectors - sector
            {
                return Err(IoError(SERVICE_INVALID_BUFFER));
            }
            let argument = if device.high_capacity {
                sector
            } else {
                sector
                    .checked_mul(BLOCK_SIZE)
                    .ok_or(IoError(SERVICE_INVALID_BUFFER))?
            };
            let argument = u32::try_from(argument).map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
            device
                .registers
                .write_u16(BLOCK_SIZE_REGISTER, BLOCK_SIZE as u16)?;
            device
                .registers
                .write_u16(BLOCK_COUNT_REGISTER, count as u16)?;
            device.submit_command(&CommandSpec::read(
                if count > 1 { 18 } else { 17 },
                argument,
                count,
            ))?;
        }
        let result = (|| {
            self.await_status(COMMAND_COMPLETE, wait)?;
            for block in 0..count {
                self.await_status(BUFFER_READ_READY, wait)?;
                for word in 0..128_u64 {
                    let data = self.device.borrow().registers.read_u32(BUFFER)?;
                    buffer.write(block * BLOCK_SIZE + word * 4, &data.to_le_bytes())?;
                }
            }
            self.await_status(TRANSFER_COMPLETE, wait)
        })();
        if result.is_err() {
            self.device.borrow_mut().recover();
        }
        result
    }
}

impl Transfer for Transfers {
    fn transfer(
        &mut self,
        writing: bool,
        sector: u64,
        count: u64,
        buffer: &Buffer,
        wait: &mut Wait,
    ) -> Result<(), IoError> {
        if writing {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        self.read(sector, count, buffer, wait)
    }
}

pub struct SdhciDriver {
    device: Rc<RefCell<Device>>,
    server: Server<Transfers>,
    rearm: bool,
    delivery_error: Option<IoError>,
}

impl DriverComponent for SdhciDriver {
    fn load(configuration: &[u8]) -> Result<Self, IoError> {
        let host = SdHost::from_wire_bytes(configuration)?;
        let device = Rc::new(RefCell::new(Device::new(host)?));
        let sectors = device.borrow().sectors;
        let server = Server::listen(
            DEVICE_PATH,
            sectors,
            host.device.irq,
            Transfers::new(device.clone()),
        )?;
        hunde::block_driver::register(DEVICE_PATH, sectors)?;
        Ok(Self {
            device,
            server,
            rearm: false,
            delivery_error: None,
        })
    }

    fn irq(&self) -> Option<u64> {
        Some(self.device.borrow().irq)
    }

    fn step(&mut self) -> Result<DriverProgress, IoError> {
        if let Some(error) = self.delivery_error.take() {
            return Err(error);
        }
        if self.rearm {
            self.rearm = false;
            return Ok(DriverProgress::new(true, true));
        }
        self.server.tick()?;
        Ok(DriverProgress::new(self.server.step()?, false))
    }

    fn deliver(&mut self) {
        let result = self
            .device
            .borrow_mut()
            .acknowledge()
            .and_then(|()| self.server.deliver());
        if let Err(error) = result {
            self.delivery_error = Some(error);
            return;
        }
        self.rearm = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn command_flags_and_capacity_match_existing_protocol() {
        assert_eq!(
            CommandSpec::new(9, 0, ResponseKind::R2).value(),
            (9 << 8) | 1 | 8
        );
        assert_eq!(
            CommandSpec::new(41, 0, ResponseKind::R3).value(),
            (41 << 8) | 2
        );
        assert_eq!(CommandSpec::read(18, 0, 2).transfer_mode, 0x36);
        assert_eq!(card_sectors([0x4000_0000, 0, 0x0003_0000, 0]), 4096);
    }

    #[test]
    fn clock_divisors_match_existing_hardware_settings() {
        assert_eq!(clock_divisor(375_000_000, 400_000), 0xd540);
        assert_eq!(clock_divisor(375_000_000, 25_000_000), 0x0800);
    }

    #[test]
    fn interrupt_status_retains_coalesced_read_events_and_rejects_errors() {
        let mut pending = PendingStatus::default();
        pending.record(COMMAND_COMPLETE | BUFFER_READ_READY);
        assert_eq!(pending.take(COMMAND_COMPLETE), Ok(true));
        assert_eq!(pending.take(BUFFER_READ_READY), Ok(true));
        assert_eq!(pending.take(BUFFER_READ_READY), Ok(false));
        pending.record(INTERRUPT_ERROR | TRANSFER_COMPLETE);
        assert_eq!(pending.take(TRANSFER_COMPLETE), Err(Device::failure()));
        assert_eq!(pending.take(TRANSFER_COMPLETE), Ok(false));
    }
}
