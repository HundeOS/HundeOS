use crate::Input;
use hunde::{
    abi::IoError,
    boot::{VirtioDeviceConfiguration, VirtioMmio},
    dma::Area,
    protocol::SERVICE_INVALID_BUFFER,
    virtio::{Descriptor, PAGE_SIZE, SplitQueue, Transport, WRITE},
};

const QUEUE_SIZE: usize = 32;
const EVENT_BYTES: usize = 8;

pub struct Device {
    transport: Transport,
    dma: Area,
    queue: SplitQueue,
    irq: u64,
}

impl Device {
    pub fn start(configuration: VirtioDeviceConfiguration) -> Result<Self, IoError> {
        let mut transport = Transport::claim(&configuration, 18)?;
        transport.begin(0, 0)?;
        let dma = Area::allocate(PAGE_SIZE, PAGE_SIZE)?;
        let queue = SplitQueue::allocate(transport.capacity(0)?.min(QUEUE_SIZE))?;
        transport.attach(0, &queue)?;
        let irq = configuration
            .hardware
            .first_irq
            .checked_add(configuration.slot)
            .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        let mut device = Self {
            transport,
            dma,
            queue,
            irq,
        };
        for index in 0..device.queue.size() {
            device.queue.descriptor(
                index as u16,
                Descriptor::new(
                    device
                        .dma
                        .device_address(index * EVENT_BYTES, EVENT_BYTES)?,
                    EVENT_BYTES as u32,
                    WRITE,
                    0,
                ),
            )?;
            device
                .dma
                .clean_invalidate(index * EVENT_BYTES, EVENT_BYTES)?;
            device.queue.publish(index as u16)?;
        }
        device.transport.start()?;
        device.transport.notify(0)?;
        Ok(device)
    }
    pub fn irq(&self) -> u64 {
        self.irq
    }
    pub fn acknowledge(&mut self) -> Result<(), IoError> {
        self.transport.acknowledge().map(|_| ())
    }
    pub fn drain(&mut self, input: &mut Input) -> Result<(bool, bool), IoError> {
        let mut progress = false;
        while input.has_room_for_event() {
            let Some(used) = self.queue.pop()? else {
                return Ok((progress, true));
            };
            if used.length != EVENT_BYTES as u32 {
                return Err(IoError(SERVICE_INVALID_BUFFER));
            }
            let offset = usize::from(used.descriptor) * EVENT_BYTES;
            self.dma.invalidate(offset, EVENT_BYTES)?;
            let mut event = [0; EVENT_BYTES];
            self.dma.read(offset, &mut event)?;
            if u16::from_le_bytes(event[..2].try_into().unwrap_or_default()) == 1 {
                input.key(
                    u16::from_le_bytes(event[2..4].try_into().unwrap_or_default()),
                    i32::from_le_bytes(event[4..].try_into().unwrap_or_default()),
                );
            }
            self.dma.clean_invalidate(offset, EVENT_BYTES)?;
            self.queue.publish(used.descriptor)?;
            self.transport.notify(0)?;
            progress = true;
        }
        Ok((progress, false))
    }
}

pub fn probe(hardware: VirtioMmio) -> Result<Option<u64>, IoError> {
    let slots = hunde::virtio::probe_slots(hardware, 18)?;
    Ok((slots != 0).then(|| u64::from(slots.trailing_zeros())))
}
