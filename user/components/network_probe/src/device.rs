use heapless::{Deque, Vec};
use smoltcp::phy::{Device, DeviceCapabilities, Medium, RxToken, TxToken};
use smoltcp::time::Instant;

pub(crate) const FRAME_CAPACITY: usize = 1514;
pub(crate) const QUEUE_CAPACITY: usize = 8;
pub(crate) type Frame = Vec<u8, FRAME_CAPACITY>;
type Frames = Deque<Frame, QUEUE_CAPACITY>;

#[derive(Debug, PartialEq, Eq)]
pub(crate) enum PacketError {
    Full,
}

#[derive(Default)]
pub(crate) struct PacketDevice {
    inbound: Frames,
    outbound: Frames,
}

impl PacketDevice {
    pub(crate) fn inject(&mut self, frame: &Frame) -> Result<(), PacketError> {
        self.inbound
            .push_back(frame.clone())
            .map_err(|_| PacketError::Full)
    }

    pub(crate) fn take_transmitted(&mut self) -> Option<Frame> {
        self.outbound.pop_front()
    }

    pub(crate) fn is_idle(&self) -> bool {
        self.inbound.is_empty() && self.outbound.is_empty()
    }

    pub(crate) fn has_input(&self) -> bool {
        !self.inbound.is_empty()
    }
}

pub(crate) struct Receive {
    frame: Frame,
}

impl Receive {
    fn new(frame: Frame) -> Self {
        Self { frame }
    }
}

pub(crate) struct Transmit<'a> {
    queue: &'a mut Frames,
}

impl<'a> Transmit<'a> {
    fn new(queue: &'a mut Frames) -> Self {
        Self { queue }
    }
}

impl RxToken for Receive {
    fn consume<R, F: FnOnce(&[u8]) -> R>(self, consume: F) -> R {
        consume(&self.frame)
    }
}

impl TxToken for Transmit<'_> {
    fn consume<R, F: FnOnce(&mut [u8]) -> R>(self, length: usize, consume: F) -> R {
        let mut frame = Frame::new();
        frame
            .resize(length, 0)
            .expect("Frame exceeds advertised MTU");
        let result = consume(&mut frame);
        self.queue
            .push_back(frame)
            .expect("Reserved TX slot unavailable");
        result
    }
}

impl Device for PacketDevice {
    type RxToken<'a> = Receive;
    type TxToken<'a> = Transmit<'a>;

    fn receive(&mut self, _: Instant) -> Option<(Receive, Transmit<'_>)> {
        if self.outbound.is_full() {
            return None;
        }
        self.inbound
            .pop_front()
            .map(|frame| (Receive::new(frame), Transmit::new(&mut self.outbound)))
    }

    fn transmit(&mut self, _: Instant) -> Option<Transmit<'_>> {
        if self.outbound.is_full() {
            None
        } else {
            Some(Transmit::new(&mut self.outbound))
        }
    }

    fn capabilities(&self) -> DeviceCapabilities {
        let mut capabilities = DeviceCapabilities::default();
        capabilities.medium = Medium::Ethernet;
        capabilities.max_transmission_unit = FRAME_CAPACITY;
        capabilities
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn transmit_reserves_bounded_capacity_and_recovers() {
        let mut device = PacketDevice::default();
        for _ in 0..QUEUE_CAPACITY {
            device
                .transmit(Instant::ZERO)
                .unwrap()
                .consume(FRAME_CAPACITY, |bytes| bytes.fill(17));
        }
        assert!(device.transmit(Instant::ZERO).is_none());
        let frame = device.take_transmitted().unwrap();
        assert_eq!(frame.len(), FRAME_CAPACITY);
        assert!(frame.iter().all(|byte| *byte == 17));
        assert!(device.transmit(Instant::ZERO).is_some());
    }

    #[test]
    fn receive_retains_frame_until_reply_capacity_is_available() {
        let mut device = PacketDevice::default();
        device
            .inject(&Frame::from_slice(b"received frame").unwrap())
            .unwrap();
        for _ in 0..QUEUE_CAPACITY {
            device.transmit(Instant::ZERO).unwrap().consume(64, |_| ());
        }
        assert!(device.receive(Instant::ZERO).is_none());
        assert!(device.has_input());
        device.take_transmitted().unwrap();
        let (received, reply) = device.receive(Instant::ZERO).unwrap();
        received.consume(|bytes| assert_eq!(bytes, b"received frame"));
        reply.consume(64, |_| ());
        assert!(!device.has_input());
        assert!(device.transmit(Instant::ZERO).is_none());
    }

    #[test]
    fn full_receive_queue_preserves_caller_frame_without_overwriting() {
        let mut device = PacketDevice::default();
        for _ in 0..QUEUE_CAPACITY {
            device
                .inject(&Frame::from_slice(b"queued").unwrap())
                .unwrap();
        }
        let rejected = Frame::from_slice(b"rejected").unwrap();
        assert_eq!(device.inject(&rejected), Err(PacketError::Full));
        assert_eq!(&rejected[..], b"rejected");
        for _ in 0..QUEUE_CAPACITY {
            let (received, _) = device.receive(Instant::ZERO).unwrap();
            received.consume(|bytes| assert_eq!(bytes, b"queued"));
        }
        assert!(device.is_idle());
    }
}
