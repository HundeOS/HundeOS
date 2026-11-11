#![cfg_attr(feature = "component", no_std)]

extern crate alloc;

use alloc::{collections::VecDeque, format, rc::Rc};
use core::cell::RefCell;
use hunde::abi::IoError;
use hunde::packet::{Device, Error, Frame, Info, QUEUE_CAPACITY, Server};
use hunde::wuff::{DriverComponent, DriverProgress};

#[derive(Default)]
struct Cable {
    queues: [VecDeque<Frame>; 2],
}

struct Port {
    cable: Rc<RefCell<Cable>>,
    index: usize,
    info: Info,
}

impl Port {
    fn new(cable: Rc<RefCell<Cable>>, index: usize) -> Result<Self, Error> {
        Ok(Self {
            cable,
            index,
            info: Info::new([2, 0, 0, 0, 0, index as u8 + 1], 1500, true)?,
        })
    }
}

impl Device for Port {
    fn info(&self) -> Info {
        self.info
    }
    fn transmit(&mut self, frame: Frame) -> Result<(), Frame> {
        let mut cable = self.cable.borrow_mut();
        let destination = &mut cable.queues[1 - self.index];
        if destination.len() == QUEUE_CAPACITY {
            return Err(frame);
        }
        destination.push_back(frame);
        Ok(())
    }
    fn receive(&mut self) -> Result<Option<Frame>, Error> {
        Ok(self.cable.borrow_mut().queues[self.index].pop_front())
    }
    fn clear(&mut self) {
        for queue in &mut self.cable.borrow_mut().queues {
            queue.clear();
        }
    }
}

pub struct CableDriver {
    ports: [Server<Port>; 2],
}

impl DriverComponent for CableDriver {
    fn load(configuration: &[u8]) -> Result<Self, IoError> {
        let base = core::str::from_utf8(configuration).map_err(|_| Error::Invalid.into_io())?;
        if base.is_empty()
            || base.len() > 253
            || !base
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
        {
            return Err(Error::Invalid.into_io());
        }
        let cable = Rc::new(RefCell::new(Cable::default()));
        let first = Port::new(cable.clone(), 0).map_err(Error::into_io)?;
        let second = Port::new(cable, 1).map_err(Error::into_io)?;
        Ok(Self {
            ports: [
                Server::new(format!("{base}.0").as_bytes(), first).map_err(Error::into_io)?,
                Server::new(format!("{base}.1").as_bytes(), second).map_err(Error::into_io)?,
            ],
        })
    }
    fn irq(&self) -> Option<u64> {
        None
    }
    fn step(&mut self) -> Result<DriverProgress, IoError> {
        let first = self.ports[0].step().map_err(Error::into_io)?;
        let second = self.ports[1].step().map_err(Error::into_io)?;
        Ok(DriverProgress::new(first | second, false))
    }
    fn deliver(&mut self) {}
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cable_preserves_frames_and_backpressure_and_discards_old_session_traffic() {
        let cable = Rc::new(RefCell::new(Cable::default()));
        let mut left = Port::new(cable.clone(), 0).unwrap();
        let mut right = Port::new(cable, 1).unwrap();
        assert_ne!(left.info().mac(), right.info().mac());
        for value in 0..QUEUE_CAPACITY {
            left.transmit(Frame::new(&[value as u8; 64]).unwrap())
                .unwrap();
        }
        let rejected = Frame::new(&[255; 64]).unwrap();
        assert_eq!(left.transmit(rejected.clone()), Err(rejected));
        assert_eq!(left.receive(), Ok(None));
        for value in 0..QUEUE_CAPACITY {
            assert_eq!(
                right.receive().unwrap().unwrap().as_bytes(),
                &[value as u8; 64]
            );
        }
        right.transmit(Frame::new(&[17; 1514]).unwrap()).unwrap();
        assert_eq!(left.receive().unwrap().unwrap().len(), 1514);
        left.transmit(Frame::new(&[0; 14]).unwrap()).unwrap();
        left.clear();
        assert_eq!(right.receive(), Ok(None));
    }
}
