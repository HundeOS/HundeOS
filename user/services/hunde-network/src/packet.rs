use alloc::{collections::VecDeque, vec};
use hunde::packet::{Client, Error, Frame, Info, QUEUE_CAPACITY};
use smoltcp::{
    phy::{Device, DeviceCapabilities, Medium, RxToken, TxToken},
    time::Instant,
};

pub trait Transport {
    fn info(&self) -> Info;
    fn transmitting(&self) -> bool;
    fn transmit(&mut self, frame: &Frame) -> Result<bool, Error>;
    fn poll_transmit(&mut self) -> Result<bool, Error>;
    fn receive(&mut self) -> Result<Option<Frame>, Error>;
}

impl Transport for Client {
    fn info(&self) -> Info {
        self.info()
    }
    fn transmitting(&self) -> bool {
        self.transmitting()
    }
    fn transmit(&mut self, frame: &Frame) -> Result<bool, Error> {
        self.transmit(frame)
    }
    fn poll_transmit(&mut self) -> Result<bool, Error> {
        self.poll_transmit()
    }
    fn receive(&mut self) -> Result<Option<Frame>, Error> {
        self.receive()
    }
}

pub struct Adapter<Link: Transport = Client> {
    link: Link,
    incoming: VecDeque<Frame>,
    outgoing: VecDeque<Frame>,
    failed: Option<Error>,
}

impl Adapter<Client> {
    pub fn open(endpoint: &[u8]) -> Result<Self, Error> {
        Ok(Self::new(Client::open(endpoint)?))
    }
    pub fn close(self) -> Result<(), Error> {
        self.link.close()
    }
}

impl<Link: Transport> Adapter<Link> {
    pub fn new(link: Link) -> Self {
        Self {
            link,
            incoming: VecDeque::new(),
            outgoing: VecDeque::new(),
            failed: None,
        }
    }
    pub fn info(&self) -> Info {
        self.link.info()
    }
    pub fn idle(&self) -> bool {
        self.outgoing.is_empty() && !self.link.transmitting()
    }
    pub fn advance(&mut self) -> Result<bool, Error> {
        if let Some(error) = self.failed {
            return Err(error);
        }
        match self.advance_link() {
            Ok(worked) => Ok(worked),
            Err(error) => {
                self.failed = Some(error);
                Err(error)
            }
        }
    }
    fn advance_link(&mut self) -> Result<bool, Error> {
        let mut worked = self.link.poll_transmit()?;
        if self.incoming.len() < QUEUE_CAPACITY
            && let Some(frame) = self.link.receive()?
        {
            self.incoming.push_back(frame);
            worked = true;
        }
        if let Some(frame) = self.outgoing.front()
            && self.link.transmit(frame)?
        {
            self.outgoing.pop_front();
            worked = true;
        }
        Ok(worked)
    }
    fn can_transmit(&self) -> bool {
        self.failed.is_none()
            && self.info().link_up()
            && self.outgoing.len() + usize::from(self.link.transmitting()) < QUEUE_CAPACITY
    }
}

pub struct Receive(Frame);
impl RxToken for Receive {
    fn consume<Value, Consume: FnOnce(&[u8]) -> Value>(self, consume: Consume) -> Value {
        consume(self.0.as_bytes())
    }
}

pub struct Transmit<'a> {
    queue: &'a mut VecDeque<Frame>,
    limit: usize,
}
impl<'a> Transmit<'a> {
    fn new(queue: &'a mut VecDeque<Frame>, limit: usize) -> Self {
        Self { queue, limit }
    }
}
impl TxToken for Transmit<'_> {
    fn consume<Value, Consume: FnOnce(&mut [u8]) -> Value>(
        self,
        length: usize,
        consume: Consume,
    ) -> Value {
        assert!(length <= self.limit);
        let mut bytes = vec![0; length];
        let result = consume(&mut bytes);
        self.queue
            .push_back(Frame::from_vec(bytes).expect("smoltcp emitted an invalid Ethernet frame"));
        result
    }
}

impl<Link: Transport> Device for Adapter<Link> {
    type RxToken<'a>
        = Receive
    where
        Self: 'a;
    type TxToken<'a>
        = Transmit<'a>
    where
        Self: 'a;
    fn receive(&mut self, _now: Instant) -> Option<(Self::RxToken<'_>, Self::TxToken<'_>)> {
        if !self.can_transmit() {
            return None;
        }
        let frame = self.incoming.pop_front()?;
        let limit = self.info().frame_limit();
        Some((Receive(frame), Transmit::new(&mut self.outgoing, limit)))
    }
    fn transmit(&mut self, _now: Instant) -> Option<Self::TxToken<'_>> {
        if !self.can_transmit() {
            return None;
        }
        let limit = self.info().frame_limit();
        Some(Transmit::new(&mut self.outgoing, limit))
    }
    fn capabilities(&self) -> DeviceCapabilities {
        let mut capabilities = DeviceCapabilities::default();
        capabilities.medium = Medium::Ethernet;
        capabilities.max_transmission_unit = self.info().frame_limit();
        capabilities
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Fake {
        incoming: VecDeque<Frame>,
        sent: VecDeque<Frame>,
        busy: bool,
        fail: bool,
    }
    impl Fake {
        fn new() -> Self {
            Self {
                incoming: VecDeque::new(),
                sent: VecDeque::new(),
                busy: false,
                fail: false,
            }
        }
    }
    impl Transport for Fake {
        fn info(&self) -> Info {
            Info::new([2, 0, 0, 0, 0, 1], 1500, true).unwrap()
        }
        fn transmitting(&self) -> bool {
            self.busy
        }
        fn transmit(&mut self, frame: &Frame) -> Result<bool, Error> {
            if self.busy {
                return Ok(false);
            }
            self.sent.push_back(frame.clone());
            self.busy = true;
            Ok(true)
        }
        fn poll_transmit(&mut self) -> Result<bool, Error> {
            if self.fail {
                Err(Error::Closed)
            } else {
                Ok(false)
            }
        }
        fn receive(&mut self) -> Result<Option<Frame>, Error> {
            Ok(self.incoming.pop_front())
        }
    }
    #[test]
    fn tokens_reserve_bounded_capacity_including_in_flight_transmission() {
        let mut adapter = Adapter::new(Fake::new());
        for value in 0..QUEUE_CAPACITY {
            adapter
                .transmit(Instant::ZERO)
                .unwrap()
                .consume(64, |bytes| bytes.fill(value as u8));
        }
        assert!(adapter.transmit(Instant::ZERO).is_none());
        assert!(adapter.advance().unwrap());
        assert!(adapter.transmit(Instant::ZERO).is_none());
        assert_eq!(adapter.link.sent.front().unwrap().as_bytes(), &[0; 64]);
        adapter
            .link
            .incoming
            .push_back(Frame::new(&[8; 64]).unwrap());
        adapter.advance().unwrap();
        assert!(adapter.receive(Instant::ZERO).is_none());
        adapter.link.busy = false;
        adapter
            .receive(Instant::ZERO)
            .unwrap()
            .0
            .consume(|bytes| assert_eq!(bytes, &[8; 64]));
        assert_eq!(adapter.capabilities().max_transmission_unit, 1514);
    }
    #[test]
    fn failed_transport_never_publishes_more_tokens() {
        let mut adapter = Adapter::new(Fake::new());
        adapter.link.fail = true;
        assert_eq!(adapter.advance(), Err(Error::Closed));
        assert!(adapter.transmit(Instant::ZERO).is_none());
    }
}
