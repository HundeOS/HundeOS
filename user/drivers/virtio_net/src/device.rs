use alloc::{collections::VecDeque, vec};
use hunde::{
    abi::IoError,
    boot::VirtioDeviceConfiguration,
    dma::Area,
    mmio::Mapping,
    packet::{Error, FRAME_CAPACITY, Frame, Info, QUEUE_CAPACITY},
    protocol::SERVICE_INVALID_BUFFER,
    virtio::{Descriptor, Memory, NEXT, PAGE_SIZE, Registers, SplitQueue, Transport, WRITE},
};

const MAC: u32 = 1 << 5;
const STATUS: u32 = 1 << 16;
const HEADER: usize = 10;
const PAYLOAD: usize = 64;
const STRIDE: usize = 2048;
const POOL_BYTES: usize = QUEUE_CAPACITY * STRIDE;

pub(super) struct Device<Register: Registers = Mapping, Backing: Memory = Area> {
    transport: Transport<Register>,
    rx: SplitQueue<Backing>,
    tx: SplitQueue<Backing>,
    read: Backing,
    write: Backing,
    incoming: VecDeque<Frame>,
    info: Info,
    features: u32,
    fault: Option<IoError>,
}

impl Device {
    pub(super) fn start(configuration: VirtioDeviceConfiguration) -> Result<Self, IoError> {
        Self::new(
            Transport::claim(&configuration, 1)?,
            SplitQueue::allocate(QUEUE_CAPACITY * 2)?,
            SplitQueue::allocate(QUEUE_CAPACITY * 2)?,
            Area::allocate(POOL_BYTES, PAGE_SIZE)?,
            Area::allocate(POOL_BYTES, PAGE_SIZE)?,
        )
    }
}

impl<Register: Registers, Backing: Memory> Device<Register, Backing> {
    fn new(
        mut transport: Transport<Register>,
        rx: SplitQueue<Backing>,
        tx: SplitQueue<Backing>,
        read: Backing,
        write: Backing,
    ) -> Result<Self, IoError> {
        let features = transport.begin(MAC, STATUS)?;
        let info = Self::read_info(&transport, features)?;
        let mut device = Self {
            transport,
            rx,
            tx,
            read,
            write,
            incoming: VecDeque::new(),
            info,
            features,
            fault: None,
        };
        device.configure()?;
        Ok(device)
    }
    fn initialize(&mut self) -> Result<(), IoError> {
        self.features = self.transport.begin(MAC, STATUS)?;
        self.update_info()?;
        self.configure()
    }
    fn configure(&mut self) -> Result<(), IoError> {
        self.rx.reset()?;
        self.tx.reset()?;
        self.incoming.clear();
        self.transport.attach(0, &self.rx)?;
        self.transport.attach(1, &self.tx)?;
        for index in 0..QUEUE_CAPACITY {
            self.post_receive(index)?;
        }
        self.transport.start()?;
        self.transport.notify(0)
    }
    fn update_info(&mut self) -> Result<(), IoError> {
        self.info = Self::read_info(&self.transport, self.features)?;
        Ok(())
    }
    fn read_info(transport: &Transport<Register>, features: u32) -> Result<Info, IoError> {
        let first = transport.configuration(0)?.to_le_bytes();
        let second = transport.configuration(4)?.to_le_bytes();
        Info::new(
            [first[0], first[1], first[2], first[3], second[0], second[1]],
            1500,
            features & STATUS == 0 || second[2] & 1 != 0,
        )
        .map_err(Error::into_io)
    }
    fn post_receive(&mut self, index: usize) -> Result<(), IoError> {
        let offset = index * STRIDE;
        self.read.clean_invalidate(offset, STRIDE)?;
        self.rx.descriptor(
            (index * 2) as u16,
            Descriptor::new(
                self.read.device_address(offset, HEADER)?,
                HEADER as u32,
                NEXT | WRITE,
                (index * 2 + 1) as u16,
            ),
        )?;
        self.rx.descriptor(
            (index * 2 + 1) as u16,
            Descriptor::new(
                self.read.device_address(offset + PAYLOAD, FRAME_CAPACITY)?,
                FRAME_CAPACITY as u32,
                WRITE,
                0,
            ),
        )?;
        self.rx.publish((index * 2) as u16)
    }
    pub(super) fn info(&self) -> Info {
        self.info
    }
    pub(super) fn acknowledge(&mut self) -> Result<(), IoError> {
        self.transport.acknowledge().map(|_| ())
    }
    pub(super) fn fault(&self) -> Result<(), IoError> {
        self.fault.map_or(Ok(()), Err)
    }
    pub(super) fn transmit(&mut self, frame: Frame) -> Result<(), Frame> {
        if self.fault.is_some() || !self.info.link_up() {
            return Err(frame);
        }
        let Some(index) = (0..QUEUE_CAPACITY).find(|index| !self.tx.busy((index * 2) as u16))
        else {
            return Err(frame);
        };
        if let Err(error) = self.submit(index, &frame) {
            self.fault = Some(error);
            return Err(frame);
        }
        Ok(())
    }
    fn submit(&mut self, index: usize, frame: &Frame) -> Result<(), IoError> {
        let offset = index * STRIDE;
        self.write.write(offset, &[0; HEADER])?;
        self.write.write(offset + PAYLOAD, frame.as_bytes())?;
        self.write.clean(offset, STRIDE)?;
        self.tx.descriptor(
            (index * 2) as u16,
            Descriptor::new(
                self.write.device_address(offset, HEADER)?,
                HEADER as u32,
                NEXT,
                (index * 2 + 1) as u16,
            ),
        )?;
        self.tx.descriptor(
            (index * 2 + 1) as u16,
            Descriptor::new(
                self.write.device_address(offset + PAYLOAD, frame.len())?,
                frame.len() as u32,
                0,
                0,
            ),
        )?;
        self.tx.publish((index * 2) as u16)?;
        self.transport.notify(1)
    }
    pub(super) fn receive(&mut self) -> Result<Option<Frame>, Error> {
        self.fault()?;
        Ok(self.incoming.pop_front())
    }
    pub(super) fn clear(&mut self) {
        if let Err(error) = self.initialize() {
            self.fault = Some(error);
        }
    }
    pub(super) fn poll(&mut self) -> Result<(bool, bool), IoError> {
        self.fault()?;
        self.update_info()?;
        let mut worked = false;
        while let Some(used) = self.tx.pop()? {
            if used.descriptor % 2 != 0 {
                return Err(IoError(SERVICE_INVALID_BUFFER));
            }
            worked = true;
        }
        for _ in 0..QUEUE_CAPACITY {
            if self.incoming.len() == QUEUE_CAPACITY {
                return Ok((worked, false));
            }
            let Some(used) = self.rx.pop()? else {
                return Ok((worked, true));
            };
            if used.descriptor % 2 != 0
                || !(HEADER..=HEADER + FRAME_CAPACITY).contains(&(used.length as usize))
            {
                return Err(IoError(SERVICE_INVALID_BUFFER));
            }
            let index = usize::from(used.descriptor) / 2;
            if (used.length as usize) < HEADER + 14 {
                self.post_receive(index)?;
                self.transport.notify(0)?;
                worked = true;
                continue;
            }
            let offset = index * STRIDE;
            self.read.invalidate(offset, STRIDE)?;
            let mut header = [0; HEADER];
            self.read.read(offset, &mut header)?;
            if header[0] != 0 || header[1] != 0 {
                return Err(IoError(SERVICE_INVALID_BUFFER));
            }
            let mut bytes = vec![0; used.length as usize - HEADER];
            self.read.read(offset + PAYLOAD, &mut bytes)?;
            self.incoming
                .push_back(Frame::from_vec(bytes).map_err(Error::into_io)?);
            self.post_receive(index)?;
            self.transport.notify(0)?;
            worked = true;
        }
        Ok((worked, false))
    }
}

impl<Register: Registers, Backing: Memory> Drop for Device<Register, Backing> {
    fn drop(&mut self) {
        let _ = self.transport.reset();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::{rc::Rc, vec::Vec};
    use core::cell::RefCell;

    #[derive(Clone)]
    struct Ram {
        bytes: Rc<RefCell<Vec<u8>>>,
        address: u64,
    }
    impl Ram {
        fn new(length: usize, address: u64) -> Self {
            Self {
                bytes: Rc::new(RefCell::new(vec![0; length])),
                address,
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
    impl Memory for Ram {
        fn read(&self, offset: usize, target: &mut [u8]) -> Result<(), IoError> {
            target.copy_from_slice(&self.bytes.borrow()[offset..offset + target.len()]);
            Ok(())
        }
        fn write(&mut self, offset: usize, source: &[u8]) -> Result<(), IoError> {
            self.bytes.borrow_mut()[offset..offset + source.len()].copy_from_slice(source);
            Ok(())
        }
        fn device_address(&self, offset: usize, length: usize) -> Result<u64, IoError> {
            self.bytes
                .borrow()
                .get(offset..offset + length)
                .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
            Ok(self.address + offset as u64)
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
    #[derive(Clone)]
    struct Bank {
        words: Rc<RefCell<[u32; 68]>>,
        refuse_reset: Rc<RefCell<bool>>,
    }
    impl Bank {
        fn new() -> Self {
            let mut words = [0; 68];
            words[0] = 0x7472_6976;
            words[1] = 1;
            words[2] = 1;
            words[0x10 / 4] = MAC | STATUS | 1;
            words[0x34 / 4] = 16;
            words[0x100 / 4] = 2;
            words[0x104 / 4] = (1 << 16) | (1 << 8);
            Self {
                words: Rc::new(RefCell::new(words)),
                refuse_reset: Rc::new(RefCell::new(false)),
            }
        }
    }
    impl Registers for Bank {
        fn read(&self, offset: usize) -> Result<u32, IoError> {
            Ok(self.words.borrow()[offset / 4])
        }
        fn write(&mut self, offset: usize, value: u32) -> Result<(), IoError> {
            if !(*self.refuse_reset.borrow() && offset == 0x70 && value == 0) {
                self.words.borrow_mut()[offset / 4] = value;
            }
            Ok(())
        }
        fn boundary() {}
    }
    struct Fixture {
        bank: Bank,
        rx: Ram,
        tx: Ram,
        read: Ram,
        write: Ram,
    }
    impl Fixture {
        fn new() -> Self {
            Self {
                bank: Bank::new(),
                rx: Ram::new(hunde::virtio::QUEUE_BYTES, 0x1000),
                tx: Ram::new(hunde::virtio::QUEUE_BYTES, 0x4000),
                read: Ram::new(POOL_BYTES, 0x8000),
                write: Ram::new(POOL_BYTES, 0xc000),
            }
        }
        fn device(&self) -> Device<Bank, Ram> {
            Device::new(
                Transport::new(self.bank.clone(), 0).unwrap(),
                SplitQueue::new(self.rx.clone(), 16).unwrap(),
                SplitQueue::new(self.tx.clone(), 16).unwrap(),
                self.read.clone(),
                self.write.clone(),
            )
            .unwrap()
        }
    }

    #[test]
    fn negotiates_only_mac_and_link_and_keeps_dma_staging_until_tx_completion() {
        let fixture = Fixture::new();
        let mut device = fixture.device();
        assert_eq!(device.info().mac(), [2, 0, 0, 0, 0, 1]);
        assert!(device.info().link_up());
        assert_eq!(fixture.bank.words.borrow()[0x20 / 4], MAC | STATUS);
        for index in 0..QUEUE_CAPACITY {
            device
                .transmit(Frame::new(&[index as u8; 64]).unwrap())
                .unwrap();
        }
        let rejected = Frame::new(&[255; 64]).unwrap();
        assert_eq!(device.transmit(rejected.clone()), Err(rejected.clone()));
        assert_eq!(
            &fixture.write.bytes.borrow()[PAYLOAD..PAYLOAD + 64],
            &[0; 64]
        );
        fixture.tx.complete(0, 0, 9999, 1);
        assert_eq!(device.poll().unwrap(), (true, true));
        device.transmit(rejected).unwrap();
        assert_eq!(
            &fixture.write.bytes.borrow()[PAYLOAD..PAYLOAD + 64],
            &[255; 64]
        );
        assert_eq!(&fixture.write.bytes.borrow()[..HEADER], &[0; HEADER]);
        assert_eq!(
            &fixture.tx.bytes.borrow()[8..12],
            &(HEADER as u32).to_le_bytes()
        );
        assert_eq!(&fixture.tx.bytes.borrow()[12..14], &NEXT.to_le_bytes());
    }

    #[test]
    fn receive_snapshots_complete_frames_before_reposting_and_bounds_staging() {
        let fixture = Fixture::new();
        let mut device = fixture.device();
        for index in 0..QUEUE_CAPACITY {
            fixture.read.bytes.borrow_mut()
                [index * STRIDE + PAYLOAD..index * STRIDE + PAYLOAD + 64]
                .fill(index as u8);
            fixture.rx.complete(
                index,
                (index * 2) as u32,
                (HEADER + 64) as u32,
                QUEUE_CAPACITY as u16,
            );
        }
        assert_eq!(device.poll().unwrap(), (true, false));
        fixture.read.bytes.borrow_mut()[PAYLOAD..PAYLOAD + 64].fill(255);
        fixture.rx.complete(8, 0, (HEADER + 64) as u32, 9);
        assert_eq!(device.poll().unwrap(), (false, false));
        assert_eq!(device.receive().unwrap().unwrap().as_bytes(), &[0; 64]);
        assert_eq!(device.poll().unwrap(), (true, false));
        for index in 1..QUEUE_CAPACITY {
            assert_eq!(
                device.receive().unwrap().unwrap().as_bytes(),
                &[index as u8; 64]
            );
        }
        assert_eq!(device.receive().unwrap().unwrap().as_bytes(), &[255; 64]);
        assert_eq!(device.poll().unwrap(), (false, true));
    }

    #[test]
    fn malformed_receive_extents_and_unnegotiated_offloads_are_rejected() {
        for length in [0, (HEADER - 1) as u32, (HEADER + FRAME_CAPACITY + 1) as u32] {
            let fixture = Fixture::new();
            let mut device = fixture.device();
            fixture.rx.complete(0, 0, length, 1);
            assert!(device.poll().is_err());
        }
        let fixture = Fixture::new();
        let mut device = fixture.device();
        fixture.read.bytes.borrow_mut()[0] = 1;
        fixture.rx.complete(0, 0, (HEADER + 64) as u32, 1);
        assert!(device.poll().is_err());
    }

    #[test]
    fn runt_ethernet_frames_are_dropped_and_reposted_without_failing_the_host() {
        for length in 0..14 {
            let fixture = Fixture::new();
            let mut device = fixture.device();
            fixture.rx.complete(0, 0, (HEADER + length) as u32, 1);
            assert_eq!(device.poll().unwrap(), (true, true));
            assert_eq!(device.receive().unwrap(), None);
            assert!(device.rx.busy(0));
        }
    }

    #[test]
    fn close_resets_dma_before_reuse_and_failed_reset_prevents_new_io() {
        let fixture = Fixture::new();
        let mut device = fixture.device();
        device.transmit(Frame::new(&[42; 64]).unwrap()).unwrap();
        device.clear();
        assert!(device.fault().is_ok());
        assert_eq!(device.poll().unwrap(), (false, true));
        for _ in 0..QUEUE_CAPACITY {
            device.transmit(Frame::new(&[7; 64]).unwrap()).unwrap();
        }
        *fixture.bank.refuse_reset.borrow_mut() = true;
        device.clear();
        assert!(device.fault().is_err());
        assert!(device.transmit(Frame::new(&[9; 64]).unwrap()).is_err());
        assert!(device.receive().is_err());
    }
}
