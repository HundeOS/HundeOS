use smoltcp::phy::{Device, DeviceCapabilities, Loopback, Medium, TxToken};
use smoltcp::time::Instant;

const QUEUE_LIMIT: usize = 32;

pub(crate) struct LocalDevice {
    inner: Loopback,
    queued: usize,
}

impl Default for LocalDevice {
    fn default() -> Self {
        Self {
            inner: Loopback::new(Medium::Ip),
            queued: 0,
        }
    }
}

impl LocalDevice {
    pub(crate) fn has_input(&self) -> bool {
        self.queued != 0
    }
}

pub(crate) struct Transmit<'a, Token> {
    inner: Token,
    queued: &'a mut usize,
}

impl<'a, Token> Transmit<'a, Token> {
    fn new(inner: Token, queued: &'a mut usize) -> Self {
        Self { inner, queued }
    }
}

impl<Token: TxToken> TxToken for Transmit<'_, Token> {
    fn consume<Value, Consume: FnOnce(&mut [u8]) -> Value>(
        self,
        length: usize,
        consume: Consume,
    ) -> Value {
        assert!(length <= 1500);
        let result = self.inner.consume(length, consume);
        *self.queued += 1;
        result
    }
}

impl Device for LocalDevice {
    type RxToken<'a> = <Loopback as Device>::RxToken<'a>;
    type TxToken<'a> = Transmit<'a, <Loopback as Device>::TxToken<'a>>;

    fn receive(&mut self, now: Instant) -> Option<(Self::RxToken<'_>, Self::TxToken<'_>)> {
        self.inner.receive(now).map(|(receive, transmit)| {
            self.queued -= 1;
            (receive, Transmit::new(transmit, &mut self.queued))
        })
    }

    fn transmit(&mut self, now: Instant) -> Option<Self::TxToken<'_>> {
        if self.queued == QUEUE_LIMIT {
            return None;
        }
        self.inner
            .transmit(now)
            .map(|token| Transmit::new(token, &mut self.queued))
    }

    fn capabilities(&self) -> DeviceCapabilities {
        let mut capabilities = self.inner.capabilities();
        capabilities.max_transmission_unit = 1500;
        capabilities
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use smoltcp::phy::RxToken;

    #[test]
    fn upstream_loopback_is_bounded_and_reserves_reply_capacity() {
        let mut device = LocalDevice::default();
        for value in 0..QUEUE_LIMIT {
            device
                .transmit(Instant::ZERO)
                .unwrap()
                .consume(1, |bytes| bytes.fill(value as u8));
        }
        assert!(device.transmit(Instant::ZERO).is_none());
        let (receive, reply) = device.receive(Instant::ZERO).unwrap();
        receive.consume(|bytes| assert_eq!(bytes, &[0]));
        reply.consume(1, |bytes| bytes.fill(255));
        assert!(device.transmit(Instant::ZERO).is_none());
        for value in 1..QUEUE_LIMIT {
            let (receive, _) = device.receive(Instant::ZERO).unwrap();
            receive.consume(|bytes| assert_eq!(bytes, &[value as u8]));
        }
        let (receive, _) = device.receive(Instant::ZERO).unwrap();
        receive.consume(|bytes| assert_eq!(bytes, &[255]));
        assert!(!device.has_input());
        assert!(device.transmit(Instant::ZERO).is_some());
    }
}
