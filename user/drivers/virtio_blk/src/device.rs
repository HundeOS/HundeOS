use crate::queue::{DESCRIPTOR_BYTES, Direction, QUEUE_BYTES, QUEUE_SIZE, Queue, USED_OFFSET};
use hunde::abi::IoError;
use hunde::boot::VirtioMmio;
use hunde::dma::Area;
use hunde::mmio::Mapping;
use hunde::protocol::SERVICE_INVALID_BUFFER;

const MAGIC: usize = 0x000;
const VERSION: usize = 0x004;
const DEVICE_ID: usize = 0x008;
const GUEST_PAGE_SIZE: usize = 0x028;
const QUEUE_SELECT: usize = 0x030;
const QUEUE_SIZE_MAX: usize = 0x034;
const QUEUE_SIZE_REGISTER: usize = 0x038;
const QUEUE_ALIGN: usize = 0x03c;
const QUEUE_PFN: usize = 0x040;
const QUEUE_NOTIFY: usize = 0x050;
const INTERRUPT_STATUS: usize = 0x060;
const INTERRUPT_ACK: usize = 0x064;
const STATUS: usize = 0x070;
const CONFIG: usize = 0x100;
const PAGE_SIZE: usize = 4096;
const DMA_BYTES: usize = QUEUE_BYTES + PAGE_SIZE;
const HEADER_OFFSET: usize = QUEUE_BYTES;
const HEADER_BYTES: usize = 16;
const REQUEST_STATUS_OFFSET: usize = HEADER_OFFSET + HEADER_BYTES;
const AVAILABLE_RING_OFFSET: usize = QUEUE_SIZE * DESCRIPTOR_BYTES + 4;
const AVAILABLE_INDEX_OFFSET: usize = QUEUE_SIZE * DESCRIPTOR_BYTES + 2;
const DEVICE_STATUS_OFFSET: usize = USED_OFFSET + 2;

pub struct Device {
    registers: Mapping,
    register_base: usize,
    dma: Area,
    queue: Queue,
    irq: u64,
    sectors: u64,
    failed: bool,
    inflight: bool,
}

impl Device {
    pub fn start(hardware: VirtioMmio, slot: u64) -> Result<Self, IoError> {
        let region_size = usize::try_from(
            hardware
                .slot_size
                .checked_mul(hardware.slot_count)
                .ok_or(IoError(SERVICE_INVALID_BUFFER))?,
        )
        .map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        let register_base = usize::try_from(
            slot.checked_mul(hardware.slot_size)
                .ok_or(IoError(SERVICE_INVALID_BUFFER))?,
        )
        .map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        if slot >= hardware.slot_count || slot >= 64 || hardware.slot_size < 0x108 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let registers = Mapping::map(hardware.physical_base, region_size)?;
        if !Self::is_block(
            registers.read_u32(register_base + MAGIC)?,
            registers.read_u32(register_base + VERSION)?,
            registers.read_u32(register_base + DEVICE_ID)?,
        ) {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        registers.claim_virtio(register_base)?;
        let low = u64::from(registers.read_u32(register_base + CONFIG)?);
        let high = u64::from(registers.read_u32(register_base + CONFIG + 4)?);
        let sectors = low | (high << 32);
        if sectors == 0 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let dma = Area::allocate(DMA_BYTES, PAGE_SIZE)?;
        let irq = hardware
            .first_irq
            .checked_add(slot)
            .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        let mut device = Self {
            registers,
            register_base,
            dma,
            queue: Queue::new(0),
            irq,
            sectors,
            failed: false,
            inflight: false,
        };
        device.initialize()?;
        Ok(device)
    }

    pub fn is_block(magic: u32, version: u32, device_id: u32) -> bool {
        magic == 0x7472_6976 && version == 1 && device_id == 2
    }

    pub fn irq(&self) -> u64 {
        self.irq
    }

    pub fn sectors(&self) -> u64 {
        self.sectors
    }

    fn read(&self, offset: usize) -> Result<u32, IoError> {
        self.registers.read_u32(self.register_base + offset)
    }

    fn write(&mut self, offset: usize, value: u32) -> Result<(), IoError> {
        self.registers.write_u32(self.register_base + offset, value)
    }

    fn initialize(&mut self) -> Result<(), IoError> {
        self.write(STATUS, 0)?;
        self.write(STATUS, 1)?;
        self.write(STATUS, 3)?;
        self.dma.clean(0, DMA_BYTES)?;
        self.write(GUEST_PAGE_SIZE, PAGE_SIZE as u32)?;
        self.write(QUEUE_SELECT, 0)?;
        if self.read(QUEUE_SIZE_MAX)? < QUEUE_SIZE as u32 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let queue_address = self.dma.device_address(0, QUEUE_BYTES)?;
        if queue_address % PAGE_SIZE as u64 != 0 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        self.write(QUEUE_SIZE_REGISTER, QUEUE_SIZE as u32)?;
        self.write(QUEUE_ALIGN, PAGE_SIZE as u32)?;
        let page_frame = u32::try_from(queue_address / PAGE_SIZE as u64)
            .map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        self.write(QUEUE_PFN, page_frame)?;
        self.dma.invalidate(USED_OFFSET, PAGE_SIZE)?;
        let mut used = [0_u8; 2];
        self.dma.read(DEVICE_STATUS_OFFSET, &mut used)?;
        self.queue = Queue::new(u16::from_le_bytes(used));
        self.write(STATUS, 7)
    }

    pub fn submit(
        &mut self,
        direction: Direction,
        sector: u64,
        length: usize,
        data_device_address: u64,
    ) -> Result<(), IoError> {
        if self.failed
            || self.inflight
            || sector >= self.sectors
            || u64::try_from(length / 512).map_err(|_| IoError(SERVICE_INVALID_BUFFER))?
                > self.sectors - sector
        {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let header_address = self.dma.device_address(HEADER_OFFSET, HEADER_BYTES)?;
        let status_address = self.dma.device_address(REQUEST_STATUS_OFFSET, 1)?;
        let submission = self.queue.prepare(
            direction,
            sector,
            length,
            header_address,
            data_device_address,
            status_address,
        )?;
        self.dma.write(HEADER_OFFSET, &submission.header)?;
        self.dma.write(REQUEST_STATUS_OFFSET, &[1])?;
        for (index, descriptor) in submission.descriptors.iter().enumerate() {
            self.dma
                .write(index * DESCRIPTOR_BYTES, &descriptor.bytes())?;
        }
        self.dma.write(
            AVAILABLE_RING_OFFSET + submission.available_slot * 2,
            &0_u16.to_le_bytes(),
        )?;
        self.dma.clean(HEADER_OFFSET, HEADER_BYTES)?;
        self.dma.clean_invalidate(REQUEST_STATUS_OFFSET, 1)?;
        self.dma.clean(0, PAGE_SIZE)?;
        Area::boundary();
        self.dma.write(
            AVAILABLE_INDEX_OFFSET,
            &submission.available_index.to_le_bytes(),
        )?;
        self.dma.clean(0, PAGE_SIZE)?;
        Area::boundary();
        if let Err(error) = self.write(QUEUE_NOTIFY, 0) {
            self.failed = true;
            return Err(error);
        }
        self.queue.publish();
        self.inflight = true;
        Ok(())
    }

    pub fn acknowledge(&mut self) -> Result<(), IoError> {
        let status = self.read(INTERRUPT_STATUS)?;
        Area::boundary();
        if status != 0 {
            self.write(INTERRUPT_ACK, status)?;
            Area::boundary();
        }
        Ok(())
    }

    pub fn completion(&mut self) -> Result<Option<bool>, IoError> {
        self.dma.invalidate(USED_OFFSET, PAGE_SIZE)?;
        let mut used = [0_u8; 2];
        self.dma.read(DEVICE_STATUS_OFFSET, &mut used)?;
        if !self.inflight || !self.queue.complete(u16::from_le_bytes(used)) {
            return Ok(None);
        }
        self.inflight = false;
        self.dma.invalidate(REQUEST_STATUS_OFFSET, 1)?;
        let mut status = [0_u8; 1];
        self.dma.read(REQUEST_STATUS_OFFSET, &mut status)?;
        Ok(Some(status[0] == 0))
    }

    pub fn fail(&mut self) {
        self.failed = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn probe_requires_legacy_block_device() {
        assert!(Device::is_block(0x7472_6976, 1, 2));
        assert!(!Device::is_block(0x7472_6976, 2, 2));
        assert!(!Device::is_block(0, 1, 2));
        assert!(!Device::is_block(0x7472_6976, 0, 2));
        assert!(!Device::is_block(0x7472_6976, 1, 1));
    }
}
