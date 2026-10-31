#![no_std]

extern crate alloc;

pub mod device;
pub mod queue;

use alloc::rc::Rc;
use core::cell::RefCell;
use device::Device;
use hunde::abi::IoError;
use hunde::block_driver::{Buffer, Server, Transfer, Wait};
use hunde::boot::{BlockDriverConfiguration, VirtioMmio};
use hunde::protocol::SERVICE_INVALID_BUFFER;
use hunde::wuff::{DriverComponent, DriverProgress};
use queue::Direction;

struct Transfers {
    device: Rc<RefCell<Device>>,
}

struct Batch {
    direction: Direction,
    sector: u64,
    offset: u64,
    length: u64,
    device_address: u64,
}

impl Batch {
    fn new(
        direction: Direction,
        sector: u64,
        offset: u64,
        length: u64,
        device_address: u64,
    ) -> Self {
        Self {
            direction,
            sector,
            offset,
            length,
            device_address,
        }
    }
}

impl Transfers {
    fn new(device: Rc<RefCell<Device>>) -> Self {
        Self { device }
    }

    fn execute_batch(
        &mut self,
        batch: Batch,
        buffer: &Buffer,
        wait: &mut Wait,
    ) -> Result<(), IoError> {
        if batch.direction == Direction::Write {
            buffer.clean(batch.offset, batch.length)?;
        } else {
            buffer.clean_invalidate(batch.offset, batch.length)?;
        }
        let submit = self.device.borrow_mut().submit(
            batch.direction,
            batch.sector,
            usize::try_from(batch.length).map_err(|_| IoError(SERVICE_INVALID_BUFFER))?,
            batch.device_address,
        );
        if let Err(error) = submit {
            self.device.borrow_mut().fail();
            return Err(error);
        }
        loop {
            let completion = self.device.borrow_mut().completion();
            match completion {
                Ok(Some(true)) => break,
                Ok(Some(false)) => return Err(IoError(SERVICE_INVALID_BUFFER)),
                Ok(None) => {
                    if let Err(error) = wait.irq() {
                        self.device.borrow_mut().fail();
                        return Err(error);
                    }
                }
                Err(error) => {
                    self.device.borrow_mut().fail();
                    return Err(error);
                }
            }
        }
        if batch.direction == Direction::Read {
            buffer.invalidate(batch.offset, batch.length)?;
        }
        Ok(())
    }

    fn execute(
        &mut self,
        direction: Direction,
        sector: u64,
        count: u64,
        buffer: &Buffer,
        wait: &mut Wait,
    ) -> Result<(), IoError> {
        let mut completed = 0_u64;
        while completed < count {
            let offset = completed
                .checked_mul(512)
                .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
            let mut batch = count - completed;
            let device_address = loop {
                let length = batch
                    .checked_mul(512)
                    .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
                match buffer.device_address(offset, length) {
                    Ok(address) => break address,
                    Err(_) if batch > 1 => batch /= 2,
                    Err(error) => return Err(error),
                }
            };
            let length = batch
                .checked_mul(512)
                .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
            let next_sector = sector
                .checked_add(completed)
                .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
            let started_at = hunde::block_driver::monotonic_usec();
            let result = self.execute_batch(
                Batch::new(direction, next_sector, offset, length, device_address),
                buffer,
                wait,
            );
            hunde::block_driver::record_submission(
                batch,
                hunde::block_driver::monotonic_usec().saturating_sub(started_at),
                result.is_ok(),
            );
            result?;
            completed += batch;
        }
        Ok(())
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
        let direction = if writing {
            Direction::Write
        } else {
            Direction::Read
        };
        self.execute(direction, sector, count, buffer, wait)
    }
}

pub struct BlockDriver {
    device: Rc<RefCell<Device>>,
    server: Server<Transfers>,
    rearm: bool,
    delivery_error: Option<IoError>,
}

impl DriverComponent for BlockDriver {
    fn load(configuration: &[u8]) -> Result<Self, IoError> {
        let configuration = BlockDriverConfiguration::from_wire_bytes(configuration)?;
        let device = Rc::new(RefCell::new(Device::start(
            configuration.hardware,
            configuration.slot,
        )?));
        let irq = device.borrow().irq();
        let sectors = device.borrow().sectors();
        let server = Server::listen(
            &configuration.path,
            sectors,
            irq,
            Transfers::new(device.clone()),
        )?;
        hunde::block_driver::register(&configuration.path, sectors)?;
        Ok(Self {
            device,
            server,
            rearm: false,
            delivery_error: None,
        })
    }

    fn irq(&self) -> Option<u64> {
        Some(self.device.borrow().irq())
    }

    fn step(&mut self) -> Result<DriverProgress, IoError> {
        if let Some(error) = self.delivery_error.take() {
            return Err(error);
        }
        if self.rearm {
            self.rearm = false;
            return Ok(DriverProgress::new(true, true));
        }
        Ok(DriverProgress::new(self.server.step()?, false))
    }

    fn deliver(&mut self) {
        let acknowledgement = self.device.borrow_mut().acknowledge();
        if let Err(error) = acknowledgement {
            self.delivery_error = Some(error);
            self.device.borrow_mut().fail();
            return;
        }
        if let Err(error) = self.server.deliver() {
            self.delivery_error = Some(error);
            self.device.borrow_mut().fail();
            return;
        }
        self.rearm = true;
    }
}

pub fn probe_slots(hardware: VirtioMmio) -> Result<u64, IoError> {
    let length = hardware
        .slot_size
        .checked_mul(hardware.slot_count)
        .and_then(|length| usize::try_from(length).ok())
        .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
    if hardware.slot_count > 64
        || hardware.slot_size < 0x108
        || !hardware.slot_size.is_multiple_of(4)
    {
        return Err(IoError(SERVICE_INVALID_BUFFER));
    }
    let registers = hunde::mmio::Mapping::map(hardware.physical_base, length)?;
    let mut slots = 0;
    for slot in 0..hardware.slot_count {
        let offset = usize::try_from(slot * hardware.slot_size)
            .map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        if Device::is_block(
            registers.read_u32(offset)?,
            registers.read_u32(offset + 4)?,
            registers.read_u32(offset + 8)?,
        ) {
            slots |= 1_u64 << slot;
        }
    }
    Ok(slots)
}
