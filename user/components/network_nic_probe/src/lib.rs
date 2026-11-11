#![cfg_attr(feature = "executable", no_std)]

extern crate alloc;

use alloc::{vec, vec::Vec};
use hunde::{abi::IoError, packet::Error, protocol::IO_PANIC};
use hunde_network::packet::Adapter;
use smoltcp::{
    iface::{Config, Interface, SocketHandle, SocketSet},
    phy::ChecksumCapabilities,
    socket::{icmp, tcp, udp},
    time::Instant,
    wire::{EthernetAddress, Icmpv4Packet, Icmpv4Repr, IpAddress, IpCidr},
};

#[cfg(feature = "executable")]
hunde::executable!(run, b"Rust NIC probe panicked");

const LENGTH: usize = 32 * 1024;

pub fn payload(index: usize) -> u8 {
    (index.wrapping_mul(37) ^ (index >> 8)) as u8
}

pub struct Conversation {
    interface: Interface,
    sockets: SocketSet<'static>,
    tcp: SocketHandle,
    udp: SocketHandle,
    icmp: SocketHandle,
    client: bool,
    sent: usize,
    received: usize,
    short: bool,
    udp_sent: bool,
    udp_done: bool,
    ping_sent: bool,
    ping_done: bool,
}

impl Conversation {
    pub fn new(
        device: &mut impl smoltcp::phy::Device,
        mac: [u8; 6],
        client: bool,
    ) -> Result<Self, IoError> {
        let suffix = if client { 1 } else { 2 };
        let mut config = Config::new(EthernetAddress(mac).into());
        config.random_seed = suffix as u64;
        let mut interface = Interface::new(config, device, Instant::ZERO);
        interface.update_ip_addrs(|addresses| {
            addresses
                .push(IpCidr::new(IpAddress::v4(192, 0, 2, suffix), 24))
                .expect("Fixture IP capacity");
        });
        let mut sockets = SocketSet::new(Vec::new());
        let tcp = sockets.add(tcp::Socket::new(
            tcp::SocketBuffer::new(vec![0; 512]),
            tcp::SocketBuffer::new(vec![0; 512]),
        ));
        let udp = sockets.add(udp::Socket::new(
            udp::PacketBuffer::new(vec![udp::PacketMetadata::EMPTY; 4], vec![0; 1024]),
            udp::PacketBuffer::new(vec![udp::PacketMetadata::EMPTY; 4], vec![0; 1024]),
        ));
        let icmp = sockets.add(icmp::Socket::new(
            icmp::PacketBuffer::new(vec![icmp::PacketMetadata::EMPTY; 2], vec![0; 128]),
            icmp::PacketBuffer::new(vec![icmp::PacketMetadata::EMPTY; 2], vec![0; 128]),
        ));
        if client {
            sockets
                .get_mut::<tcp::Socket>(tcp)
                .connect(
                    interface.context(),
                    (IpAddress::v4(192, 0, 2, 2), 8080),
                    40000,
                )
                .map_err(|_| IoError(IO_PANIC))?;
            sockets
                .get_mut::<icmp::Socket>(icmp)
                .bind(icmp::Endpoint::Ident(123))
                .map_err(|_| IoError(IO_PANIC))?;
        } else {
            sockets
                .get_mut::<tcp::Socket>(tcp)
                .listen(8080)
                .map_err(|_| IoError(IO_PANIC))?;
        }
        sockets
            .get_mut::<udp::Socket>(udp)
            .bind(if client { 40001 } else { 8081 })
            .map_err(|_| IoError(IO_PANIC))?;
        Ok(Self {
            interface,
            sockets,
            tcp,
            udp,
            icmp,
            client,
            sent: 0,
            received: 0,
            short: false,
            udp_sent: false,
            udp_done: false,
            ping_sent: false,
            ping_done: false,
        })
    }

    pub fn advance(
        &mut self,
        device: &mut impl smoltcp::phy::Device,
        now: Instant,
    ) -> Result<bool, IoError> {
        self.interface.poll_maintenance(now);
        self.interface
            .poll_ingress_single(now, device, &mut self.sockets);
        self.interface.poll_egress(now, device, &mut self.sockets);
        let mut worked = false;
        let socket = self.sockets.get_mut::<tcp::Socket>(self.tcp);
        if self.client && self.sent < LENGTH && socket.can_send() {
            let mut bytes = [0; 1024];
            let length = bytes.len().min(LENGTH - self.sent);
            for (offset, byte) in bytes[..length].iter_mut().enumerate() {
                *byte = payload(self.sent + offset);
            }
            let count = socket
                .send_slice(&bytes[..length])
                .map_err(|_| IoError(IO_PANIC))?;
            self.short |= count < length;
            self.sent += count;
            worked = true;
        }
        if socket.can_recv() && (self.client || socket.can_send()) {
            let mut bytes = [0; 512];
            let capacity = if self.client {
                bytes.len()
            } else {
                (socket.send_capacity() - socket.send_queue()).min(bytes.len())
            };
            let length = socket
                .recv_slice(&mut bytes[..capacity])
                .map_err(|_| IoError(IO_PANIC))?;
            if bytes[..length]
                .iter()
                .enumerate()
                .any(|(offset, byte)| *byte != payload(self.received + offset))
            {
                return Err(IoError(IO_PANIC));
            }
            self.received += length;
            if !self.client {
                let count = socket
                    .send_slice(&bytes[..length])
                    .map_err(|_| IoError(IO_PANIC))?;
                if count != length {
                    return Err(IoError(IO_PANIC));
                }
                self.sent += count;
            }
            worked = true;
        }
        if self.received == LENGTH {
            socket.close();
        }
        let socket = self.sockets.get_mut::<udp::Socket>(self.udp);
        if self.client && !self.udp_sent && socket.can_send() {
            socket
                .send_slice(b"binary\0UDP\xff", (IpAddress::v4(192, 0, 2, 2), 8081))
                .map_err(|_| IoError(IO_PANIC))?;
            self.udp_sent = true;
            worked = true;
        }
        if socket.can_recv() && socket.can_send() {
            let mut bytes = [0; 32];
            let (length, peer) = socket
                .recv_slice(&mut bytes)
                .map_err(|_| IoError(IO_PANIC))?;
            if &bytes[..length] != b"binary\0UDP\xff" {
                return Err(IoError(IO_PANIC));
            }
            if !self.client {
                socket
                    .send_slice(&bytes[..length], peer)
                    .map_err(|_| IoError(IO_PANIC))?;
            }
            self.udp_done = true;
            worked = true;
        }
        if self.client {
            let socket = self.sockets.get_mut::<icmp::Socket>(self.icmp);
            if !self.ping_sent && socket.can_send() {
                let request = Icmpv4Repr::EchoRequest {
                    ident: 123,
                    seq_no: 1,
                    data: b"Hunde ping",
                };
                let bytes = socket
                    .send(request.buffer_len(), IpAddress::v4(192, 0, 2, 2))
                    .map_err(|_| IoError(IO_PANIC))?;
                request.emit(
                    &mut Icmpv4Packet::new_unchecked(bytes),
                    &ChecksumCapabilities::default(),
                );
                self.ping_sent = true;
                worked = true;
            }
            if socket.can_recv() {
                let (bytes, _) = socket.recv().map_err(|_| IoError(IO_PANIC))?;
                let packet = Icmpv4Packet::new_checked(bytes).map_err(|_| IoError(IO_PANIC))?;
                let reply = Icmpv4Repr::parse(&packet, &ChecksumCapabilities::default())
                    .map_err(|_| IoError(IO_PANIC))?;
                self.ping_done = matches!(
                    reply,
                    Icmpv4Repr::EchoReply {
                        ident: 123,
                        seq_no: 1,
                        data: b"Hunde ping"
                    }
                );
                worked = true;
            }
        }
        Ok(worked)
    }

    pub fn done(&self) -> bool {
        self.sent == LENGTH
            && self.received == LENGTH
            && self.udp_done
            && (!self.client || (self.ping_done && self.short))
            && matches!(
                self.sockets.get::<tcp::Socket>(self.tcp).state(),
                tcp::State::Closed | tcp::State::TimeWait
            )
    }
}

pub fn run() -> Result<(), IoError> {
    let mut device = Adapter::open(b"ethernet.0").map_err(Error::into_io)?;
    let mac = device.info().mac();
    let mut conversation = Conversation::new(&mut device, mac, true)?;
    let start = hunde::runtime::monotonic_usec();
    loop {
        hunde::driver::prepare_wait();
        let elapsed = hunde::runtime::monotonic_usec().saturating_sub(start);
        if elapsed > 30_000_000 {
            hunde::runtime::log(b"network_nic_probe: deadline failed");
            return Err(IoError(IO_PANIC));
        }
        let mut worked = device.advance().map_err(Error::into_io)?;
        worked |= conversation.advance(&mut device, Instant::from_micros(elapsed as i64))?;
        if conversation.done() && device.idle() {
            break;
        }
        if !worked {
            hunde::driver::wait_ticks(1);
        }
    }
    device.close().map_err(Error::into_io)?;
    hunde::runtime::log(b"network_nic_probe: external ARP ICMP TCP UDP cleanup ok");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::{collections::VecDeque, rc::Rc};
    use core::cell::RefCell;
    use hunde::packet::{Frame, Info, QUEUE_CAPACITY};
    use hunde_network::packet::Transport;
    use smoltcp::wire::{EthernetFrame, EthernetProtocol, IpProtocol, Ipv4Packet, TcpPacket};

    #[derive(Default)]
    struct Wire {
        queues: [VecDeque<Frame>; 2],
        dropped: bool,
        arp: usize,
    }
    struct Link {
        wire: Rc<RefCell<Wire>>,
        side: usize,
    }
    impl Link {
        fn new(wire: Rc<RefCell<Wire>>, side: usize) -> Self {
            Self { wire, side }
        }
    }
    impl Transport for Link {
        fn info(&self) -> Info {
            Info::new([2, 0, 0, 0, 1, self.side as u8 + 1], 1500, true).unwrap()
        }
        fn transmitting(&self) -> bool {
            false
        }
        fn poll_transmit(&mut self) -> Result<bool, Error> {
            Ok(false)
        }
        fn receive(&mut self) -> Result<Option<Frame>, Error> {
            Ok(self.wire.borrow_mut().queues[self.side].pop_front())
        }
        fn transmit(&mut self, frame: &Frame) -> Result<bool, Error> {
            let mut wire = self.wire.borrow_mut();
            if wire.queues[1 - self.side].len() == QUEUE_CAPACITY {
                return Ok(false);
            }
            let ethernet = EthernetFrame::new_checked(frame.as_bytes()).unwrap();
            if ethernet.ethertype() == EthernetProtocol::Arp {
                wire.arp += 1;
            }
            if !wire.dropped && ethernet.ethertype() == EthernetProtocol::Ipv4 {
                let ip = Ipv4Packet::new_checked(ethernet.payload()).unwrap();
                if ip.next_header() == IpProtocol::Tcp {
                    let tcp = TcpPacket::new_checked(ip.payload()).unwrap();
                    if tcp.syn() && !tcp.ack() {
                        wire.dropped = true;
                        return Ok(true);
                    }
                }
            }
            wire.queues[1 - self.side].push_back(frame.clone());
            Ok(true)
        }
    }

    #[test]
    fn fixture_requires_real_arp_ping_udp_binary_tcp_echo_and_loss_recovery() {
        let wire = Rc::new(RefCell::new(Wire::default()));
        let mut left = Adapter::new(Link::new(wire.clone(), 0));
        let mut right = Adapter::new(Link::new(wire.clone(), 1));
        let left_mac = left.info().mac();
        let right_mac = right.info().mac();
        let mut client = Conversation::new(&mut left, left_mac, true).unwrap();
        let mut server = Conversation::new(&mut right, right_mac, false).unwrap();
        for tick in 0..30_000 {
            left.advance().unwrap();
            right.advance().unwrap();
            let now = Instant::from_millis(tick);
            client.advance(&mut left, now).unwrap();
            server.advance(&mut right, now).unwrap();
            if client.done() && server.done() && left.idle() && right.idle() {
                break;
            }
        }
        assert!(client.done());
        assert!(server.done());
        assert!(wire.borrow().dropped);
        assert!(wire.borrow().arp >= 2);
    }
}
