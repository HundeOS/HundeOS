use crate::Input;
use hunde::abi::IoError;
use hunde::boot::{VirtioDeviceConfiguration, VirtioMmio};
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
const PAGE_SIZE: usize = 4096;
const QUEUE_BYTES: usize = PAGE_SIZE * 2;
const QUEUE_SIZE: usize = 32;
const DMA_BYTES: usize = QUEUE_BYTES + PAGE_SIZE;
const EVENT_BYTES: usize = 8;
const AVAILABLE_INDEX: usize = QUEUE_SIZE * 16 + 2;
const AVAILABLE_RING: usize = QUEUE_SIZE * 16 + 4;
const USED_INDEX: usize = PAGE_SIZE + 2;
const USED_RING: usize = PAGE_SIZE + 4;

pub struct Device {
    registers: Mapping,
    register_base: usize,
    dma: Area,
    queue_size: usize,
    last_used: u16,
    available: u16,
    irq: u64,
}

impl Device {
    pub fn is_keyboard(magic: u32, version: u32, device_id: u32) -> bool {
        magic == 0x7472_6976 && version >= 1 && device_id == 18
    }

    pub fn start(configuration: VirtioDeviceConfiguration) -> Result<Self, IoError> {
        let hardware = configuration.hardware;
        let region_size = hardware
            .slot_size
            .checked_mul(hardware.slot_count)
            .and_then(|size| usize::try_from(size).ok())
            .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        let register_base = configuration
            .slot
            .checked_mul(hardware.slot_size)
            .and_then(|offset| usize::try_from(offset).ok())
            .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        if hardware.slot_size < 0x100 || !hardware.slot_size.is_multiple_of(4) {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let registers = Mapping::map(hardware.physical_base, region_size)?;
        if !Self::is_keyboard(
            registers.read_u32(register_base + MAGIC)?,
            registers.read_u32(register_base + VERSION)?,
            registers.read_u32(register_base + DEVICE_ID)?,
        ) {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        registers.claim_virtio(register_base)?;
        let dma = Area::allocate(DMA_BYTES, PAGE_SIZE)?;
        let irq = hardware
            .first_irq
            .checked_add(configuration.slot)
            .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        let mut device = Self {
            registers,
            register_base,
            dma,
            queue_size: 0,
            last_used: 0,
            available: 0,
            irq,
        };
        device.initialize()?;
        Ok(device)
    }

    pub fn irq(&self) -> u64 {
        self.irq
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
        self.dma.write(0, &[0; DMA_BYTES])?;
        self.dma.clean(0, DMA_BYTES)?;
        self.write(GUEST_PAGE_SIZE, PAGE_SIZE as u32)?;
        self.write(QUEUE_SELECT, 0)?;
        self.queue_size = usize::min(self.read(QUEUE_SIZE_MAX)? as usize, QUEUE_SIZE);
        if self.queue_size == 0 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let address = self.dma.device_address(0, QUEUE_BYTES)?;
        if !address.is_multiple_of(PAGE_SIZE as u64) {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        self.write(QUEUE_SIZE_REGISTER, self.queue_size as u32)?;
        self.write(QUEUE_ALIGN, PAGE_SIZE as u32)?;
        self.write(
            QUEUE_PFN,
            u32::try_from(address / PAGE_SIZE as u64)
                .map_err(|_| IoError(SERVICE_INVALID_BUFFER))?,
        )?;
        self.dma.invalidate(PAGE_SIZE, PAGE_SIZE)?;
        let mut used = [0_u8; 2];
        self.dma.read(USED_INDEX, &mut used)?;
        self.last_used = u16::from_le_bytes(used);
        for index in 0..self.queue_size {
            let mut descriptor = [0_u8; 16];
            descriptor[..8].copy_from_slice(
                &self
                    .dma
                    .device_address(QUEUE_BYTES + index * EVENT_BYTES, EVENT_BYTES)?
                    .to_le_bytes(),
            );
            descriptor[8..12].copy_from_slice(&(EVENT_BYTES as u32).to_le_bytes());
            descriptor[12..14].copy_from_slice(&2_u16.to_le_bytes());
            self.dma.write(index * 16, &descriptor)?;
            self.dma
                .write(AVAILABLE_RING + index * 2, &(index as u16).to_le_bytes())?;
            self.dma
                .clean_invalidate(QUEUE_BYTES + index * EVENT_BYTES, EVENT_BYTES)?;
        }
        self.available = self.queue_size as u16;
        self.dma.clean(0, PAGE_SIZE)?;
        Area::boundary();
        self.dma
            .write(AVAILABLE_INDEX, &self.available.to_le_bytes())?;
        self.dma.clean(0, PAGE_SIZE)?;
        Area::boundary();
        self.write(QUEUE_NOTIFY, 0)?;
        self.write(STATUS, 7)
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

    fn requeue(&mut self, descriptor: u16) -> Result<(), IoError> {
        let slot = usize::from(self.available) % self.queue_size;
        self.dma
            .write(AVAILABLE_RING + slot * 2, &descriptor.to_le_bytes())?;
        self.dma.clean(0, PAGE_SIZE)?;
        Area::boundary();
        self.available = self.available.wrapping_add(1);
        self.dma
            .write(AVAILABLE_INDEX, &self.available.to_le_bytes())?;
        self.dma.clean(0, PAGE_SIZE)?;
        Area::boundary();
        self.write(QUEUE_NOTIFY, 0)
    }

    pub fn drain(&mut self, input: &mut Input) -> Result<(bool, bool), IoError> {
        let mut progress = false;
        loop {
            self.dma.invalidate(PAGE_SIZE, PAGE_SIZE)?;
            let mut index_bytes = [0_u8; 2];
            self.dma.read(USED_INDEX, &mut index_bytes)?;
            if self.last_used == u16::from_le_bytes(index_bytes) {
                return Ok((progress, true));
            }
            if !input.has_room_for_event() {
                return Ok((progress, false));
            }
            Area::boundary();
            let slot = usize::from(self.last_used) % self.queue_size;
            let mut element = [0_u8; 8];
            self.dma.read(USED_RING + slot * 8, &mut element)?;
            let descriptor = u32::from_le_bytes(element[..4].try_into().unwrap_or_default());
            let length = u32::from_le_bytes(element[4..].try_into().unwrap_or_default());
            if descriptor >= self.queue_size as u32 || length < EVENT_BYTES as u32 {
                return Err(IoError(SERVICE_INVALID_BUFFER));
            }
            let offset = QUEUE_BYTES + descriptor as usize * EVENT_BYTES;
            self.dma.invalidate(offset, EVENT_BYTES)?;
            let mut event = [0_u8; EVENT_BYTES];
            self.dma.read(offset, &mut event)?;
            if u16::from_le_bytes(event[..2].try_into().unwrap_or_default()) == 1 {
                input.key(
                    u16::from_le_bytes(event[2..4].try_into().unwrap_or_default()),
                    i32::from_le_bytes(event[4..].try_into().unwrap_or_default()),
                );
            }
            self.dma.clean_invalidate(offset, EVENT_BYTES)?;
            self.requeue(descriptor as u16)?;
            self.last_used = self.last_used.wrapping_add(1);
            progress = true;
        }
    }
}

pub fn probe(hardware: VirtioMmio) -> Result<Option<u64>, IoError> {
    let length = hardware
        .slot_size
        .checked_mul(hardware.slot_count)
        .and_then(|length| usize::try_from(length).ok())
        .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
    if hardware.slot_size < 0x100 || !hardware.slot_size.is_multiple_of(4) {
        return Err(IoError(SERVICE_INVALID_BUFFER));
    }
    let registers = Mapping::map(hardware.physical_base, length)?;
    for slot in 0..hardware.slot_count {
        let offset = usize::try_from(slot * hardware.slot_size)
            .map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        if Device::is_keyboard(
            registers.read_u32(offset + MAGIC)?,
            registers.read_u32(offset + VERSION)?,
            registers.read_u32(offset + DEVICE_ID)?,
        ) {
            return Ok(Some(slot));
        }
    }
    Ok(None)
}
