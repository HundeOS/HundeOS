use crate::{
    abi::IoError,
    boot::{VirtioDeviceConfiguration, VirtioMmio},
    dma::Area,
    mmio::Mapping,
    protocol::SERVICE_INVALID_BUFFER,
};

pub const PAGE_SIZE: usize = 4096;
pub const QUEUE_BYTES: usize = PAGE_SIZE * 2;
pub const DESCRIPTOR_BYTES: usize = 16;
pub const NEXT: u16 = 1;
pub const WRITE: u16 = 2;
const LIMIT: usize = 128;

fn invalid() -> IoError {
    IoError(SERVICE_INVALID_BUFFER)
}

fn identifies(magic: u32, version: u32, actual: u32, expected: u32) -> bool {
    magic == 0x7472_6976 && version == 1 && actual == expected
}

pub trait Registers {
    fn read(&self, offset: usize) -> Result<u32, IoError>;
    fn write(&mut self, offset: usize, value: u32) -> Result<(), IoError>;
    fn boundary();
}

impl Registers for Mapping {
    fn read(&self, offset: usize) -> Result<u32, IoError> {
        self.read_u32(offset)
    }
    fn write(&mut self, offset: usize, value: u32) -> Result<(), IoError> {
        self.write_u32(offset, value)
    }
    fn boundary() {
        Area::boundary();
    }
}

pub trait Memory {
    fn read(&self, offset: usize, target: &mut [u8]) -> Result<(), IoError>;
    fn write(&mut self, offset: usize, source: &[u8]) -> Result<(), IoError>;
    fn device_address(&self, offset: usize, length: usize) -> Result<u64, IoError>;
    fn clean(&self, offset: usize, length: usize) -> Result<(), IoError>;
    fn invalidate(&self, offset: usize, length: usize) -> Result<(), IoError>;
    fn clean_invalidate(&self, offset: usize, length: usize) -> Result<(), IoError>;
    fn boundary();
}

impl Memory for Area {
    fn read(&self, offset: usize, target: &mut [u8]) -> Result<(), IoError> {
        self.read(offset, target)
    }
    fn write(&mut self, offset: usize, source: &[u8]) -> Result<(), IoError> {
        self.write(offset, source)
    }
    fn device_address(&self, offset: usize, length: usize) -> Result<u64, IoError> {
        self.device_address(offset, length)
    }
    fn clean(&self, offset: usize, length: usize) -> Result<(), IoError> {
        self.clean(offset, length)
    }
    fn invalidate(&self, offset: usize, length: usize) -> Result<(), IoError> {
        self.invalidate(offset, length)
    }
    fn clean_invalidate(&self, offset: usize, length: usize) -> Result<(), IoError> {
        self.clean_invalidate(offset, length)
    }
    fn boundary() {
        Self::boundary();
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Descriptor {
    address: u64,
    length: u32,
    flags: u16,
    next: u16,
}

impl Descriptor {
    pub const fn new(address: u64, length: u32, flags: u16, next: u16) -> Self {
        Self {
            address,
            length,
            flags,
            next,
        }
    }
    pub fn bytes(self) -> [u8; DESCRIPTOR_BYTES] {
        let mut bytes = [0; DESCRIPTOR_BYTES];
        bytes[..8].copy_from_slice(&self.address.to_le_bytes());
        bytes[8..12].copy_from_slice(&self.length.to_le_bytes());
        bytes[12..14].copy_from_slice(&self.flags.to_le_bytes());
        bytes[14..].copy_from_slice(&self.next.to_le_bytes());
        bytes
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Used {
    pub descriptor: u16,
    pub length: u32,
}

impl Used {
    fn new(descriptor: u16, length: u32) -> Self {
        Self { descriptor, length }
    }
}

pub struct SplitQueue<Backing: Memory = Area> {
    memory: Backing,
    size: usize,
    available: u16,
    used: u16,
    busy: [bool; LIMIT],
    outstanding: usize,
}

impl SplitQueue<Area> {
    pub fn allocate(size: usize) -> Result<Self, IoError> {
        Self::new(Area::allocate(QUEUE_BYTES, PAGE_SIZE)?, size)
    }
}

impl<Backing: Memory> SplitQueue<Backing> {
    pub fn new(memory: Backing, size: usize) -> Result<Self, IoError> {
        if size == 0 || size > LIMIT || !size.is_power_of_two() {
            return Err(invalid());
        }
        memory.device_address(0, QUEUE_BYTES)?;
        let mut queue = Self {
            memory,
            size,
            available: 0,
            used: 0,
            busy: [false; LIMIT],
            outstanding: 0,
        };
        queue.reset()?;
        Ok(queue)
    }
    pub fn reset(&mut self) -> Result<(), IoError> {
        self.memory.write(0, &[0; QUEUE_BYTES])?;
        self.memory.clean(0, QUEUE_BYTES)?;
        self.available = 0;
        self.used = 0;
        self.busy.fill(false);
        self.outstanding = 0;
        Ok(())
    }
    pub fn size(&self) -> usize {
        self.size
    }
    pub fn address(&self) -> Result<u64, IoError> {
        self.memory.device_address(0, QUEUE_BYTES)
    }
    pub fn busy(&self, descriptor: u16) -> bool {
        usize::from(descriptor) >= self.size || self.busy[usize::from(descriptor)]
    }
    pub fn descriptor(&mut self, index: u16, descriptor: Descriptor) -> Result<(), IoError> {
        if self.busy(index) {
            return Err(invalid());
        }
        self.memory
            .write(usize::from(index) * DESCRIPTOR_BYTES, &descriptor.bytes())
    }
    pub fn publish(&mut self, descriptor: u16) -> Result<(), IoError> {
        if self.busy(descriptor) || self.outstanding == self.size {
            return Err(invalid());
        }
        let offset = self.size * DESCRIPTOR_BYTES + 4 + usize::from(self.available) % self.size * 2;
        self.memory.write(offset, &descriptor.to_le_bytes())?;
        self.memory.clean(0, PAGE_SIZE)?;
        Backing::boundary();
        let next = self.available.wrapping_add(1);
        self.memory
            .write(self.size * DESCRIPTOR_BYTES + 2, &next.to_le_bytes())?;
        self.memory.clean(0, PAGE_SIZE)?;
        Backing::boundary();
        self.available = next;
        self.busy[usize::from(descriptor)] = true;
        self.outstanding += 1;
        Ok(())
    }
    pub fn pop(&mut self) -> Result<Option<Used>, IoError> {
        self.memory.invalidate(PAGE_SIZE, PAGE_SIZE)?;
        Backing::boundary();
        let mut bytes = [0; 2];
        self.memory.read(PAGE_SIZE + 2, &mut bytes)?;
        let produced = u16::from_le_bytes(bytes);
        let pending = usize::from(produced.wrapping_sub(self.used));
        if pending > self.outstanding {
            return Err(invalid());
        }
        if pending == 0 {
            return Ok(None);
        }
        let offset = PAGE_SIZE + 4 + usize::from(self.used) % self.size * 8;
        let mut element = [0; 8];
        self.memory.read(offset, &mut element)?;
        let descriptor = u32::from_le_bytes(element[..4].try_into().map_err(|_| invalid())?);
        if descriptor >= self.size as u32 || !self.busy[descriptor as usize] {
            return Err(invalid());
        }
        let length = u32::from_le_bytes(element[4..].try_into().map_err(|_| invalid())?);
        self.busy[descriptor as usize] = false;
        self.outstanding -= 1;
        self.used = self.used.wrapping_add(1);
        Ok(Some(Used::new(descriptor as u16, length)))
    }
}

pub struct Transport<Register: Registers = Mapping> {
    registers: Register,
    base: usize,
}

fn region(hardware: VirtioMmio) -> Result<usize, IoError> {
    if hardware.slot_count > 64
        || hardware.slot_size < 0x108
        || !hardware.slot_size.is_multiple_of(4)
    {
        return Err(invalid());
    }
    hardware
        .slot_size
        .checked_mul(hardware.slot_count)
        .and_then(|size| usize::try_from(size).ok())
        .ok_or_else(invalid)
}

pub fn probe_slots(hardware: VirtioMmio, device_id: u32) -> Result<u64, IoError> {
    let registers = Mapping::map(hardware.physical_base, region(hardware)?)?;
    let mut slots = 0;
    for slot in 0..hardware.slot_count {
        let offset = (slot * hardware.slot_size) as usize;
        if identifies(
            registers.read_u32(offset)?,
            registers.read_u32(offset + 4)?,
            registers.read_u32(offset + 8)?,
            device_id,
        ) {
            slots |= 1_u64 << slot;
        }
    }
    Ok(slots)
}

impl Transport<Mapping> {
    pub fn claim(
        configuration: &VirtioDeviceConfiguration,
        device_id: u32,
    ) -> Result<Self, IoError> {
        if configuration.slot >= configuration.hardware.slot_count {
            return Err(invalid());
        }
        let registers = Mapping::map(
            configuration.hardware.physical_base,
            region(configuration.hardware)?,
        )?;
        let base = (configuration.slot * configuration.hardware.slot_size) as usize;
        if !identifies(
            registers.read_u32(base)?,
            registers.read_u32(base + 4)?,
            registers.read_u32(base + 8)?,
            device_id,
        ) {
            return Err(invalid());
        }
        registers.claim_virtio(base)?;
        Self::new(registers, base)
    }
}

impl<Register: Registers> Transport<Register> {
    pub fn new(registers: Register, base: usize) -> Result<Self, IoError> {
        let transport = Self { registers, base };
        if transport.read(0)? != 0x7472_6976 || transport.read(4)? != 1 {
            return Err(invalid());
        }
        Ok(transport)
    }
    fn read(&self, offset: usize) -> Result<u32, IoError> {
        self.registers.read(self.base + offset)
    }
    fn write(&mut self, offset: usize, value: u32) -> Result<(), IoError> {
        self.registers.write(self.base + offset, value)
    }
    pub fn reset(&mut self) -> Result<(), IoError> {
        self.write(0x70, 0)?;
        Register::boundary();
        for _ in 0..1024 {
            if self.read(0x70)? == 0 {
                Register::boundary();
                return Ok(());
            }
        }
        Err(invalid())
    }
    pub fn begin(&mut self, required: u32, optional: u32) -> Result<u32, IoError> {
        self.reset()?;
        self.write(0x70, 1)?;
        self.write(0x70, 3)?;
        let offered = self.read(0x10)?;
        if offered & required != required {
            return Err(invalid());
        }
        let accepted = offered & (required | optional);
        self.write(0x20, accepted)?;
        self.write(0x28, PAGE_SIZE as u32)?;
        Ok(accepted)
    }
    pub fn attach<Backing: Memory>(
        &mut self,
        index: u32,
        queue: &SplitQueue<Backing>,
    ) -> Result<(), IoError> {
        if self.capacity(index)? < queue.size() {
            return Err(invalid());
        }
        let address = queue.address()?;
        if !address.is_multiple_of(PAGE_SIZE as u64) {
            return Err(invalid());
        }
        self.write(0x38, queue.size() as u32)?;
        self.write(0x3c, PAGE_SIZE as u32)?;
        self.write(
            0x40,
            u32::try_from(address / PAGE_SIZE as u64).map_err(|_| invalid())?,
        )
    }
    pub fn capacity(&mut self, index: u32) -> Result<usize, IoError> {
        self.write(0x30, index)?;
        Ok(self.read(0x34)? as usize)
    }
    pub fn start(&mut self) -> Result<(), IoError> {
        self.write(0x70, 7)
    }
    pub fn notify(&mut self, index: u32) -> Result<(), IoError> {
        Register::boundary();
        self.write(0x50, index)
    }
    pub fn acknowledge(&mut self) -> Result<u32, IoError> {
        let status = self.read(0x60)?;
        Register::boundary();
        if status != 0 {
            self.write(0x64, status)?;
            Register::boundary();
        }
        Ok(status)
    }
    pub fn configuration(&self, offset: usize) -> Result<u32, IoError> {
        self.read(0x100 + offset)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::{rc::Rc, vec, vec::Vec};
    use core::cell::RefCell;

    #[derive(Clone)]
    struct Backing {
        bytes: Rc<RefCell<Vec<u8>>>,
        writes: Rc<RefCell<Vec<usize>>>,
    }
    impl Backing {
        fn new() -> Self {
            Self {
                bytes: Rc::new(RefCell::new(vec![0; QUEUE_BYTES])),
                writes: Rc::new(RefCell::new(Vec::new())),
            }
        }
        fn complete(&self, slot: usize, descriptor: u32, length: u32, produced: u16) {
            let mut bytes = self.bytes.borrow_mut();
            bytes[PAGE_SIZE + 4 + slot * 8..PAGE_SIZE + 8 + slot * 8]
                .copy_from_slice(&descriptor.to_le_bytes());
            bytes[PAGE_SIZE + 8 + slot * 8..PAGE_SIZE + 12 + slot * 8]
                .copy_from_slice(&length.to_le_bytes());
            bytes[PAGE_SIZE + 2..PAGE_SIZE + 4].copy_from_slice(&produced.to_le_bytes());
        }
    }
    impl Memory for Backing {
        fn read(&self, offset: usize, target: &mut [u8]) -> Result<(), IoError> {
            target.copy_from_slice(
                self.bytes
                    .borrow()
                    .get(offset..offset + target.len())
                    .ok_or_else(invalid)?,
            );
            Ok(())
        }
        fn write(&mut self, offset: usize, source: &[u8]) -> Result<(), IoError> {
            self.bytes
                .borrow_mut()
                .get_mut(offset..offset + source.len())
                .ok_or_else(invalid)?
                .copy_from_slice(source);
            self.writes.borrow_mut().push(offset);
            Ok(())
        }
        fn device_address(&self, offset: usize, length: usize) -> Result<u64, IoError> {
            self.bytes
                .borrow()
                .get(offset..offset + length)
                .ok_or_else(invalid)?;
            Ok(0x1000 + offset as u64)
        }
        fn clean(&self, offset: usize, length: usize) -> Result<(), IoError> {
            self.device_address(offset, length).map(|_| ())
        }
        fn invalidate(&self, offset: usize, length: usize) -> Result<(), IoError> {
            self.clean(offset, length)
        }
        fn clean_invalidate(&self, offset: usize, length: usize) -> Result<(), IoError> {
            self.clean(offset, length)
        }
        fn boundary() {}
    }

    struct Bank {
        words: [u32; 68],
        writes: Vec<(usize, u32)>,
        refuse_reset: bool,
    }
    impl Bank {
        fn new() -> Self {
            let mut words = [0; 68];
            words[0] = 0x7472_6976;
            words[1] = 1;
            words[0x34 / 4] = 128;
            Self {
                words,
                writes: Vec::new(),
                refuse_reset: false,
            }
        }
    }
    impl Registers for Bank {
        fn read(&self, offset: usize) -> Result<u32, IoError> {
            self.words.get(offset / 4).copied().ok_or_else(invalid)
        }
        fn write(&mut self, offset: usize, value: u32) -> Result<(), IoError> {
            self.writes.push((offset, value));
            if self.refuse_reset && offset == 0x70 && value == 0 {
                return Ok(());
            }
            *self.words.get_mut(offset / 4).ok_or_else(invalid)? = value;
            Ok(())
        }
        fn boundary() {}
    }

    #[test]
    fn identification_preserves_block_and_input_checks_and_adds_network() {
        for id in [1, 2, 18] {
            assert!(identifies(0x7472_6976, 1, id, id));
            assert!(!identifies(0x7472_6976, 2, id, id));
            assert!(!identifies(0, 1, id, id));
            assert!(!identifies(0x7472_6976, 0, id, id));
            assert!(!identifies(0x7472_6976, 1, id, 99));
        }
    }

    #[test]
    fn publication_is_bounded_and_descriptor_stays_owned_until_completion() {
        let backing = Backing::new();
        let mut queue = SplitQueue::new(backing.clone(), 8).unwrap();
        let descriptor = Descriptor::new(0x1122_3344_5566_7788, 1514, NEXT | WRITE, 1);
        queue.descriptor(0, descriptor).unwrap();
        assert_eq!(
            &backing.bytes.borrow()[..8],
            &0x1122_3344_5566_7788_u64.to_le_bytes()
        );
        assert_eq!(&backing.bytes.borrow()[8..12], &1514_u32.to_le_bytes());
        assert_eq!(
            &backing.bytes.borrow()[12..14],
            &(NEXT | WRITE).to_le_bytes()
        );
        queue.publish(0).unwrap();
        assert!(queue.busy(0));
        assert!(queue.publish(0).is_err());
        assert!(queue.descriptor(0, descriptor).is_err());
        assert!(queue.publish(8).is_err());
        assert_eq!(queue.pop().unwrap(), None);
        backing.complete(0, 0, 1514, 1);
        assert_eq!(queue.pop().unwrap(), Some(Used::new(0, 1514)));
        assert!(!queue.busy(0));
        assert_eq!(queue.pop().unwrap(), None);
        assert_eq!(*backing.writes.borrow(), [0, 0, 132, 130]);
    }

    #[test]
    fn wrapping_indices_preserve_publication_slot_and_completion_generation() {
        let backing = Backing::new();
        let mut queue = SplitQueue::new(backing.clone(), 8).unwrap();
        queue.available = u16::MAX;
        queue.used = u16::MAX;
        queue.publish(3).unwrap();
        assert_eq!(queue.available, 0);
        assert_eq!(&backing.bytes.borrow()[146..148], &3_u16.to_le_bytes());
        backing.complete(7, 3, 0, 0);
        assert_eq!(queue.pop().unwrap(), Some(Used::new(3, 0)));
        assert_eq!(queue.used, 0);
    }

    #[test]
    fn malformed_and_duplicate_used_entries_never_release_uncertain_heads() {
        for descriptor in [1, 8, u32::MAX] {
            let backing = Backing::new();
            let mut queue = SplitQueue::new(backing.clone(), 8).unwrap();
            queue.publish(0).unwrap();
            backing.complete(0, descriptor, 64, 1);
            assert!(queue.pop().is_err());
            assert!(queue.busy(0));
        }
        let backing = Backing::new();
        let mut queue = SplitQueue::new(backing.clone(), 8).unwrap();
        queue.publish(0).unwrap();
        queue.publish(1).unwrap();
        backing.complete(0, 0, 0, 2);
        backing.complete(1, 0, 0, 2);
        assert!(queue.pop().unwrap().is_some());
        assert!(queue.pop().is_err());
        assert!(queue.busy(1));
        queue.reset().unwrap();
        queue.publish(0).unwrap();
        backing.complete(0, 0, 0, 2);
        assert!(queue.pop().is_err());
        assert!(queue.busy(0));
        assert!(SplitQueue::new(Backing::new(), 0).is_err());
        assert!(SplitQueue::new(Backing::new(), 3).is_err());
        assert!(SplitQueue::new(Backing::new(), 256).is_err());
    }

    #[test]
    fn legacy_feature_negotiation_queue_setup_and_irq_ack_are_shared() {
        let mut bank = Bank::new();
        bank.words[0x10 / 4] = (1 << 5) | (1 << 16) | 1;
        let mut transport = Transport::new(bank, 0).unwrap();
        assert_eq!(
            transport.begin(1 << 5, 1 << 16).unwrap(),
            (1 << 5) | (1 << 16)
        );
        let queue = SplitQueue::new(Backing::new(), 16).unwrap();
        transport.attach(1, &queue).unwrap();
        transport.start().unwrap();
        transport.notify(1).unwrap();
        assert_eq!(transport.registers.words[0x20 / 4], (1 << 5) | (1 << 16));
        assert_eq!(transport.registers.words[0x30 / 4], 1);
        assert_eq!(transport.registers.words[0x38 / 4], 16);
        assert_eq!(transport.registers.words[0x40 / 4], 1);
        assert_eq!(transport.registers.words[0x70 / 4], 7);
        transport.registers.words[0x60 / 4] = 3;
        assert_eq!(transport.acknowledge().unwrap(), 3);
        assert_eq!(transport.registers.writes.last(), Some(&(0x64, 3)));
        transport.registers.words[0x10 / 4] = 0;
        assert!(transport.begin(1 << 5, 0).is_err());
        transport.registers.words[0x70 / 4] = 7;
        transport.registers.refuse_reset = true;
        assert!(transport.reset().is_err());
    }
}
