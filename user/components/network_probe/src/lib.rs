#![cfg_attr(feature = "executable", no_std)]

mod device;
mod packet;

use device::PacketDevice;
use smoltcp::iface::{Config, Interface, SocketSet, SocketStorage};
use smoltcp::socket::{tcp, udp};
use smoltcp::time::{Duration, Instant};
use smoltcp::wire::{EthernetAddress, EthernetFrame, EthernetProtocol, IpAddress, IpCidr};

#[cfg(feature = "executable")]
hunde::executable!(run, b"Rust network probe panicked");

#[derive(Debug, PartialEq, Eq)]
enum ProbeError {
    InvalidFrame,
    FullQueue,
    Socket,
    Payload,
    Deadline,
    Incomplete,
}

impl ProbeError {
    fn into_io(self) -> hunde::abi::IoError {
        hunde::runtime::log(match self {
            Self::InvalidFrame => b"network_probe: invalid frame",
            Self::FullQueue => b"network_probe: full queue",
            Self::Socket => b"network_probe: socket operation failed",
            Self::Payload => b"network_probe: payload mismatch",
            Self::Deadline => b"network_probe: deadline failed",
            Self::Incomplete => b"network_probe: incomplete case",
        });
        hunde::abi::IoError(hunde::protocol::IO_PANIC)
    }
}

#[derive(Default, Debug)]
struct Report {
    arp_frames: usize,
    dropped_frames: usize,
    received_bytes: usize,
    short_write: bool,
    udp_received: bool,
    timer_wakeups: usize,
    closed: bool,
    cleaned: bool,
}

impl Report {
    fn validate(&self, loss: bool) -> Result<(), ProbeError> {
        if self.arp_frames < 2
            || self.dropped_frames != usize::from(loss)
            || self.received_bytes != 512
            || !self.short_write
            || !self.udp_received
            || !self.closed
            || !self.cleaned
            || (loss && self.timer_wakeups == 0)
        {
            return Err(ProbeError::Incomplete);
        }
        Ok(())
    }
}

fn deliver(
    source: &mut PacketDevice,
    target: &mut PacketDevice,
    drop_ip: &mut bool,
    report: &mut Report,
) -> Result<(), ProbeError> {
    while let Some(frame) = source.take_transmitted() {
        let ethernet =
            EthernetFrame::new_checked(&frame[..]).map_err(|_| ProbeError::InvalidFrame)?;
        if ethernet.ethertype() == EthernetProtocol::Arp {
            report.arp_frames += 1;
        }
        if *drop_ip && ethernet.ethertype() == EthernetProtocol::Ipv4 {
            *drop_ip = false;
            report.dropped_frames += 1;
        } else {
            target.inject(&frame).map_err(|_| ProbeError::FullQueue)?;
        }
    }
    Ok(())
}

fn interface(device: &mut impl smoltcp::phy::Device, suffix: u8, mac: [u8; 6]) -> Interface {
    let mut config = Config::new(EthernetAddress(mac).into());
    config.random_seed = u64::from(suffix);
    let mut interface = Interface::new(config, device, Instant::ZERO);
    interface.update_ip_addrs(|addresses| {
        addresses
            .push(IpCidr::new(IpAddress::v4(192, 0, 2, suffix), 24))
            .expect("Fixture address capacity");
    });
    interface
}

fn poll(
    interface: &mut Interface,
    device: &mut impl smoltcp::phy::Device,
    sockets: &mut SocketSet<'_>,
    now: Instant,
) {
    interface.poll_maintenance(now);
    interface.poll_ingress_single(now, device, sockets);
    interface.poll_egress(now, device, sockets);
}

fn exercise(loss: bool) -> Result<Report, ProbeError> {
    let mut left = PacketDevice::default();
    let mut right = PacketDevice::default();
    let mut left_interface = interface(&mut left, 1, [2, 0, 0, 0, 0, 1]);
    let mut right_interface = interface(&mut right, 2, [2, 0, 0, 0, 0, 2]);
    let mut left_rx = [0; 128];
    let mut left_tx = [0; 128];
    let mut right_rx = [0; 128];
    let mut right_tx = [0; 128];
    let mut left_udp_rx = [0; 128];
    let mut left_udp_tx = [0; 128];
    let mut right_udp_rx = [0; 128];
    let mut right_udp_tx = [0; 128];
    let mut left_rx_meta = [udp::PacketMetadata::EMPTY; 2];
    let mut left_tx_meta = [udp::PacketMetadata::EMPTY; 2];
    let mut right_rx_meta = [udp::PacketMetadata::EMPTY; 2];
    let mut right_tx_meta = [udp::PacketMetadata::EMPTY; 2];
    let mut left_storage: [SocketStorage<'_>; 2] = Default::default();
    let mut right_storage: [SocketStorage<'_>; 2] = Default::default();
    let mut left_sockets = SocketSet::new(&mut left_storage[..]);
    let mut right_sockets = SocketSet::new(&mut right_storage[..]);
    let client = left_sockets.add(tcp::Socket::new(
        tcp::SocketBuffer::new(&mut left_rx[..]),
        tcp::SocketBuffer::new(&mut left_tx[..]),
    ));
    let server = right_sockets.add(tcp::Socket::new(
        tcp::SocketBuffer::new(&mut right_rx[..]),
        tcp::SocketBuffer::new(&mut right_tx[..]),
    ));
    let sender = left_sockets.add(udp::Socket::new(
        udp::PacketBuffer::new(&mut left_rx_meta[..], &mut left_udp_rx[..]),
        udp::PacketBuffer::new(&mut left_tx_meta[..], &mut left_udp_tx[..]),
    ));
    let receiver = right_sockets.add(udp::Socket::new(
        udp::PacketBuffer::new(&mut right_rx_meta[..], &mut right_udp_rx[..]),
        udp::PacketBuffer::new(&mut right_tx_meta[..], &mut right_udp_tx[..]),
    ));
    right_sockets
        .get_mut::<tcp::Socket>(server)
        .listen(8080)
        .map_err(|_| ProbeError::Socket)?;
    left_sockets
        .get_mut::<tcp::Socket>(client)
        .connect(
            left_interface.context(),
            (IpAddress::v4(192, 0, 2, 2), 8080),
            40000,
        )
        .map_err(|_| ProbeError::Socket)?;
    left_sockets
        .get_mut::<udp::Socket>(sender)
        .bind(40001)
        .map_err(|_| ProbeError::Socket)?;
    right_sockets
        .get_mut::<udp::Socket>(receiver)
        .bind(8081)
        .map_err(|_| ProbeError::Socket)?;
    let payload: [u8; 512] = core::array::from_fn(|index| index as u8);
    let mut sent = 0;
    let mut udp_sent = false;
    let mut drop_ip = loss;
    let mut report = Report::default();
    let mut now = Instant::ZERO;
    for _ in 0..2000 {
        if now > Instant::from_secs(30) {
            return Err(ProbeError::Deadline);
        }
        poll(&mut left_interface, &mut left, &mut left_sockets, now);
        poll(&mut right_interface, &mut right, &mut right_sockets, now);
        deliver(&mut left, &mut right, &mut drop_ip, &mut report)?;
        deliver(&mut right, &mut left, &mut false, &mut report)?;
        let socket = left_sockets.get_mut::<tcp::Socket>(client);
        if sent < payload.len() && socket.can_send() {
            let count = socket
                .send_slice(&payload[sent..])
                .map_err(|_| ProbeError::Socket)?;
            report.short_write |= count < payload.len() - sent;
            sent += count;
        }
        let socket = right_sockets.get_mut::<tcp::Socket>(server);
        if socket.can_recv() {
            let mut received = [0; 128];
            let count = socket
                .recv_slice(&mut received)
                .map_err(|_| ProbeError::Socket)?;
            let end = report.received_bytes + count;
            if payload.get(report.received_bytes..end) != Some(&received[..count]) {
                return Err(ProbeError::Payload);
            }
            report.received_bytes = end;
            if end == payload.len() {
                socket.close();
            }
        }
        if report.received_bytes == payload.len() {
            left_sockets.get_mut::<tcp::Socket>(client).close();
            if !udp_sent {
                left_sockets
                    .get_mut::<udp::Socket>(sender)
                    .send_slice(b"udp-probe", (IpAddress::v4(192, 0, 2, 2), 8081))
                    .map_err(|_| ProbeError::Socket)?;
                udp_sent = true;
            }
        }
        let socket = right_sockets.get_mut::<udp::Socket>(receiver);
        if socket.can_recv() {
            let (bytes, _) = socket.recv().map_err(|_| ProbeError::Socket)?;
            if bytes != b"udp-probe" {
                return Err(ProbeError::Payload);
            }
            report.udp_received = true;
        }
        report.closed = !left_sockets.get::<tcp::Socket>(client).is_open()
            && !right_sockets.get::<tcp::Socket>(server).is_open();
        if report.closed && report.udp_received && left.is_idle() && right.is_idle() {
            left_sockets.remove(client);
            left_sockets.remove(sender);
            right_sockets.remove(server);
            right_sockets.remove(receiver);
            report.cleaned = left_sockets.iter().count() == 0 && right_sockets.iter().count() == 0;
            report.validate(loss)?;
            return Ok(report);
        }
        let deadline = left_interface
            .poll_at(now, &left_sockets)
            .into_iter()
            .chain(right_interface.poll_at(now, &right_sockets))
            .min();
        if left.has_input() || right.has_input() {
            now += Duration::from_millis(1);
        } else {
            let deadline = deadline.ok_or(ProbeError::Deadline)?;
            let next = deadline.max(now + Duration::from_millis(1));
            if next > now + Duration::from_millis(1) {
                report.timer_wakeups += 1;
            }
            now = next;
        }
    }
    Err(ProbeError::Incomplete)
}

pub fn run() -> Result<(), hunde::abi::IoError> {
    for _ in 0..3 {
        exercise(false).map_err(ProbeError::into_io)?;
        exercise(true).map_err(ProbeError::into_io)?;
    }
    hunde::runtime::log(b"network_probe: TCP UDP ARP retransmission cleanup ok");
    packet::exercise()?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn arp_tcp_partial_writes_udp_and_orderly_close() {
        let report = exercise(false).unwrap();
        assert_eq!(report.validate(false), Ok(()));
    }

    #[test]
    fn dropped_syn_recovers_on_deadline_without_an_incoming_event() {
        let report = exercise(true).unwrap();
        assert_eq!(report.dropped_frames, 1);
        assert!(report.timer_wakeups > 0);
        assert!(report.cleaned);
    }

    #[test]
    fn repeated_cases_release_socket_and_packet_capacity() {
        for _ in 0..3 {
            assert!(exercise(false).unwrap().cleaned);
            assert!(exercise(true).unwrap().cleaned);
        }
    }
}
