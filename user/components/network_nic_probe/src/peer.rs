use hunde::{
    packet::{Error, FRAME_CAPACITY, Frame, Info},
    protocol::IO_PANIC,
};
use hunde_network::packet::{Adapter, Transport};
use hunde_network_nic_probe::Conversation;
use smoltcp::wire::{EthernetFrame, EthernetProtocol, IpProtocol, Ipv4Packet, TcpPacket};
use std::{
    io::{self, Read, Write},
    os::unix::net::{UnixListener, UnixStream},
    path::Path,
    thread,
    time::{Duration, Instant},
};

struct Link {
    socket: UnixStream,
    input: Vec<u8>,
    output: Vec<u8>,
    written: usize,
    drop_syn: bool,
}

impl Link {
    fn new(socket: UnixStream) -> io::Result<Self> {
        socket.set_nonblocking(true)?;
        Ok(Self {
            socket,
            input: Vec::new(),
            output: Vec::new(),
            written: 0,
            drop_syn: true,
        })
    }
    fn receive_frame(&mut self) -> io::Result<Option<Frame>> {
        let expected = if self.input.len() < 4 {
            4
        } else {
            let length = u32::from_be_bytes(
                self.input[..4]
                    .try_into()
                    .map_err(|_| io::ErrorKind::InvalidData)?,
            ) as usize;
            if !(14..=FRAME_CAPACITY).contains(&length) {
                return Err(io::ErrorKind::InvalidData.into());
            }
            length + 4
        };
        let mut bytes = [0; FRAME_CAPACITY + 4];
        match self.socket.read(&mut bytes[..expected - self.input.len()]) {
            Ok(0) => return Err(io::ErrorKind::UnexpectedEof.into()),
            Ok(length) => self.input.extend_from_slice(&bytes[..length]),
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => return Ok(None),
            Err(error) => return Err(error),
        }
        if expected == 4 || self.input.len() != expected {
            return Ok(None);
        }
        let data = self.input.split_off(4);
        self.input.clear();
        let frame = Frame::from_vec(data).map_err(|_| io::ErrorKind::InvalidData)?;
        let ethernet =
            EthernetFrame::new_checked(frame.as_bytes()).map_err(|_| io::ErrorKind::InvalidData)?;
        if ethernet.ethertype() == EthernetProtocol::Arp {
            println!("network-peer: ARP");
        }
        if self.drop_syn && ethernet.ethertype() == EthernetProtocol::Ipv4 {
            let ip = Ipv4Packet::new_checked(ethernet.payload())
                .map_err(|_| io::ErrorKind::InvalidData)?;
            if ip.next_header() == IpProtocol::Tcp {
                let tcp =
                    TcpPacket::new_checked(ip.payload()).map_err(|_| io::ErrorKind::InvalidData)?;
                if tcp.syn() && !tcp.ack() {
                    self.drop_syn = false;
                    println!("network-peer: dropped SYN");
                    return Ok(None);
                }
            }
        }
        Ok(Some(frame))
    }
}

impl Transport for Link {
    fn info(&self) -> Info {
        Info::new([2, 0, 0, 0, 1, 2], 1500, true).expect("Peer MAC")
    }
    fn transmitting(&self) -> bool {
        !self.output.is_empty()
    }
    fn transmit(&mut self, frame: &Frame) -> Result<bool, Error> {
        if self.transmitting() {
            return Ok(false);
        }
        self.output
            .extend_from_slice(&(frame.len() as u32).to_be_bytes());
        self.output.extend_from_slice(frame.as_bytes());
        self.written = 0;
        Ok(true)
    }
    fn poll_transmit(&mut self) -> Result<bool, Error> {
        if self.output.is_empty() {
            return Ok(false);
        }
        match self.socket.write(&self.output[self.written..]) {
            Ok(0) => return Err(Error::Closed),
            Ok(count) => self.written += count,
            Err(error) if error.kind() == io::ErrorKind::WouldBlock => return Ok(false),
            Err(_) => return Err(Error::Io),
        }
        if self.written != self.output.len() {
            return Ok(false);
        }
        self.output.clear();
        self.written = 0;
        Ok(true)
    }
    fn receive(&mut self) -> Result<Option<Frame>, Error> {
        self.receive_frame().map_err(|_| Error::Io)
    }
}

fn run(path: &Path) -> io::Result<()> {
    let listener = UnixListener::bind(path)?;
    println!("network-peer: listening");
    let (socket, _) = listener.accept()?;
    let mut device = Adapter::new(Link::new(socket)?);
    let mac = device.info().mac();
    let mut conversation =
        Conversation::new(&mut device, mac, false).map_err(|_| io::ErrorKind::InvalidData)?;
    let start = Instant::now();
    let mut reported = false;
    loop {
        if start.elapsed() > Duration::from_secs(180) {
            return Err(io::ErrorKind::TimedOut.into());
        }
        let mut worked = device.advance().map_err(|_| io::ErrorKind::BrokenPipe)?;
        worked |= conversation
            .advance(
                &mut device,
                smoltcp::time::Instant::from_micros(start.elapsed().as_micros() as i64),
            )
            .map_err(|_| io::ErrorKind::InvalidData)?;
        if conversation.done() && device.idle() && !reported {
            println!("network-peer: external TCP UDP echo complete; lost SYN retransmitted");
            reported = true;
        }
        if !worked {
            thread::sleep(Duration::from_millis(1));
        }
    }
}

fn main() {
    let Some(path) = std::env::args_os().nth(1) else {
        eprintln!("network-peer SOCKET");
        std::process::exit(2);
    };
    if let Err(error) = run(Path::new(&path)) {
        eprintln!("network-peer: {error}");
        std::process::exit(IO_PANIC.unsigned_abs() as i32);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn framed_stream_handles_partial_headers_payloads_and_binary_output() {
        let (socket, mut peer) = UnixStream::pair().unwrap();
        let mut link = Link::new(socket).unwrap();
        let frame = Frame::new(&[0; 64]).unwrap();
        let header = 64_u32.to_be_bytes();
        peer.write_all(&header[..2]).unwrap();
        assert!(link.receive().unwrap().is_none());
        peer.write_all(&header[2..]).unwrap();
        assert!(link.receive().unwrap().is_none());
        peer.write_all(&frame.as_bytes()[..17]).unwrap();
        assert!(link.receive().unwrap().is_none());
        peer.write_all(&frame.as_bytes()[17..]).unwrap();
        assert_eq!(link.receive().unwrap(), Some(frame.clone()));
        assert!(link.transmit(&frame).unwrap());
        assert!(!link.transmit(&frame).unwrap());
        assert!(link.poll_transmit().unwrap());
        let mut received = [0; 68];
        peer.read_exact(&mut received).unwrap();
        assert_eq!(&received[..4], &header);
        assert_eq!(&received[4..], frame.as_bytes());
    }

    #[test]
    fn malformed_lengths_fail_before_allocating_an_unbounded_packet() {
        let (socket, mut peer) = UnixStream::pair().unwrap();
        let mut link = Link::new(socket).unwrap();
        peer.write_all(&u32::MAX.to_be_bytes()).unwrap();
        assert!(link.receive().unwrap().is_none());
        assert!(link.receive().is_err());
        assert_eq!(link.input.len(), 4);
    }
}
