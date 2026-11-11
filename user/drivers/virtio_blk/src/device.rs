use crate::queue::{Direction, QUEUE_SIZE, Submission};
use hunde::{
    abi::IoError,
    boot::{VirtioDeviceConfiguration, VirtioMmio},
    dma::Area,
    protocol::SERVICE_INVALID_BUFFER,
    virtio::{PAGE_SIZE, SplitQueue, Transport},
};

const STATUS_OFFSET: usize = 64;

pub struct Device {
    transport: Transport,
    dma: Area,
    queue: SplitQueue,
    irq: u64,
    sectors: u64,
    failed: bool,
    inflight: bool,
}

impl Device {
    pub fn start(hardware: VirtioMmio, slot: u64) -> Result<Self, IoError> {
        let configuration = VirtioDeviceConfiguration::new(hardware, slot)?;
        let mut transport = Transport::claim(&configuration, 2)?;
        transport.begin(0, 0)?;
        let sectors =
            u64::from(transport.configuration(0)?) | (u64::from(transport.configuration(4)?) << 32);
        if sectors == 0 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let queue = SplitQueue::allocate(QUEUE_SIZE)?;
        let dma = Area::allocate(PAGE_SIZE, PAGE_SIZE)?;
        transport.attach(0, &queue)?;
        transport.start()?;
        Ok(Self {
            transport,
            dma,
            queue,
            irq: hardware
                .first_irq
                .checked_add(slot)
                .ok_or(IoError(SERVICE_INVALID_BUFFER))?,
            sectors,
            failed: false,
            inflight: false,
        })
    }
    pub fn irq(&self) -> u64 {
        self.irq
    }
    pub fn sectors(&self) -> u64 {
        self.sectors
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
            || (length / 512) as u64 > self.sectors - sector
        {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let submission = Submission::new(
            direction,
            sector,
            length,
            self.dma.device_address(0, 16)?,
            data_device_address,
            self.dma.device_address(STATUS_OFFSET, 1)?,
        )?;
        self.dma.write(0, &submission.header)?;
        self.dma.write(STATUS_OFFSET, &[1])?;
        self.dma.clean(0, 16)?;
        self.dma.clean_invalidate(STATUS_OFFSET, 1)?;
        for (index, descriptor) in submission.descriptors.into_iter().enumerate() {
            self.queue.descriptor(index as u16, descriptor)?;
        }
        self.queue.publish(0)?;
        self.inflight = true;
        if let Err(error) = self.transport.notify(0) {
            self.failed = true;
            return Err(error);
        }
        Ok(())
    }
    pub fn acknowledge(&mut self) -> Result<(), IoError> {
        self.transport.acknowledge().map(|_| ())
    }
    pub fn completion(&mut self) -> Result<Option<bool>, IoError> {
        if !self.inflight || self.queue.pop()?.is_none() {
            return Ok(None);
        }
        self.inflight = false;
        self.dma.invalidate(STATUS_OFFSET, 1)?;
        let mut status = [0];
        self.dma.read(STATUS_OFFSET, &mut status)?;
        Ok(Some(status[0] == 0))
    }
    pub fn fail(&mut self) {
        self.failed = true;
    }
}
