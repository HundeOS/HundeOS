use super::{Direction, Error, Frame, Info, Mapping, wire};
use crate::{channel, protocol::*};
use alloc::vec;

pub struct Client {
    rx: channel::Client,
    tx: channel::Client,
    read: Mapping,
    write: Mapping,
    info: Info,
    receiving: bool,
    transmitting: Option<usize>,
    failed: bool,
}

fn exchange(
    channel: &mut channel::Client,
    request: wire::Request,
) -> Result<wire::Response, Error> {
    let mut bytes = [0; 64];
    let length = channel.exchange(wire::bytes(&request), &mut bytes)?;
    let reply: wire::Response = wire::decode(&bytes[..length])?;
    reply.result()?;
    Ok(reply)
}

impl Client {
    pub fn open(endpoint: &[u8]) -> Result<Self, Error> {
        let mut tx = channel::Client::open(endpoint)?;
        let mut rx = channel::Client::open(endpoint)?;
        if tx.peer_pid() != rx.peer_pid() {
            return Err(Error::Denied);
        }
        let write = exchange(&mut tx, wire::Request::new(PACKET_OPEN_TX, 0, 0, 0))?;
        let read = exchange(&mut rx, wire::Request::new(PACKET_OPEN_RX, 0, 0, 0))?;
        let info = write.info()?;
        if info != read.info()? {
            return Err(Error::Invalid);
        }
        Ok(Self {
            write: Mapping::attach(write.buffer, tx.peer_pid(), Direction::Tx)?,
            read: Mapping::attach(read.buffer, rx.peer_pid(), Direction::Rx)?,
            tx,
            rx,
            info,
            receiving: false,
            transmitting: None,
            failed: false,
        })
    }

    pub fn info(&self) -> Info {
        self.info
    }
    pub fn transmitting(&self) -> bool {
        self.transmitting.is_some()
    }

    fn ready(&self) -> Result<(), Error> {
        if self.failed {
            Err(Error::Closed)
        } else if !self.info.link_up() {
            Err(Error::LinkDown)
        } else {
            Ok(())
        }
    }

    pub fn transmit(&mut self, frame: &Frame) -> Result<bool, Error> {
        self.ready()?;
        if self.transmitting.is_some() {
            return Ok(false);
        }
        if frame.len() > self.info.frame_limit() {
            return Err(Error::Invalid);
        }
        self.write.write(0, frame.as_bytes())?;
        let request =
            wire::Request::new(PACKET_TRANSMIT, self.write.buffer.id, 0, frame.len() as u64);
        self.failed = true;
        let sent = self.tx.send(wire::bytes(&request))?;
        self.failed = false;
        if !sent {
            return Ok(false);
        }
        self.transmitting = Some(frame.len());
        Ok(true)
    }

    pub fn poll_transmit(&mut self) -> Result<bool, Error> {
        self.ready()?;
        let Some(expected) = self.transmitting else {
            return Ok(false);
        };
        let mut bytes = [0; 64];
        self.failed = true;
        let Some(length) = self.tx.receive(&mut bytes)? else {
            self.failed = false;
            return Ok(false);
        };
        let reply: wire::Response = wire::decode(&bytes[..length])?;
        reply.result()?;
        if reply.length != expected as u64 || reply.buffer != self.write.buffer {
            return Err(Error::Invalid);
        }
        self.transmitting = None;
        self.failed = false;
        Ok(true)
    }

    pub fn receive(&mut self) -> Result<Option<Frame>, Error> {
        self.ready()?;
        if !self.receiving {
            let request = wire::Request::new(
                PACKET_RECEIVE,
                self.read.buffer.id,
                0,
                self.info.frame_limit() as u64,
            );
            self.failed = true;
            self.receiving = self.rx.send(wire::bytes(&request))?;
            self.failed = false;
            return Ok(None);
        }
        let mut bytes = [0; 64];
        self.failed = true;
        let Some(length) = self.rx.receive(&mut bytes)? else {
            self.failed = false;
            return Ok(None);
        };
        let reply: wire::Response = wire::decode(&bytes[..length])?;
        reply.result()?;
        if reply.buffer != self.read.buffer || reply.length > self.info.frame_limit() as u64 {
            return Err(Error::Invalid);
        }
        let mut frame = vec![0; reply.length as usize];
        self.read.read(0, &mut frame)?;
        let frame = Frame::from_vec(frame)?;
        self.receiving = false;
        self.failed = false;
        Ok(Some(frame))
    }

    pub fn close(mut self) -> Result<(), Error> {
        self.rx.close();
        if self.transmitting.is_some() || self.failed {
            return Err(Error::Busy);
        }
        exchange(&mut self.tx, wire::Request::new(PACKET_CLOSE, 0, 0, 0))?;
        self.tx.close();
        Ok(())
    }
}
