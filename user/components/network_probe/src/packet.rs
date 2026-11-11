use super::{ProbeError, interface, poll};
use hunde::abi::IoError;
use hunde_network::packet::Adapter;
use smoltcp::{
    iface::{SocketSet, SocketStorage},
    socket::tcp,
    time::Instant,
    wire::IpAddress,
};

const PAYLOAD_LENGTH: usize = 32 * 1024;

fn value(index: usize) -> u8 {
    (index.wrapping_mul(37) ^ (index >> 8)) as u8
}

pub(super) fn exercise() -> Result<(), IoError> {
    let mut left = Adapter::open(b"net-dummy.0").map_err(hunde::packet::Error::into_io)?;
    let mut right = Adapter::open(b"net-dummy.1").map_err(hunde::packet::Error::into_io)?;
    let left_mac = left.info().mac();
    let right_mac = right.info().mac();
    if left_mac == right_mac {
        return Err(ProbeError::Incomplete.into_io());
    }
    let mut left_interface = interface(&mut left, 1, left_mac);
    let mut right_interface = interface(&mut right, 2, right_mac);
    let mut left_rx = [0; 512];
    let mut left_tx = [0; 512];
    let mut right_rx = [0; 512];
    let mut right_tx = [0; 512];
    let mut left_storage: [SocketStorage<'_>; 1] = Default::default();
    let mut right_storage: [SocketStorage<'_>; 1] = Default::default();
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
    right_sockets
        .get_mut::<tcp::Socket>(server)
        .listen(8080)
        .map_err(|_| ProbeError::Socket.into_io())?;
    left_sockets
        .get_mut::<tcp::Socket>(client)
        .connect(
            left_interface.context(),
            (IpAddress::v4(192, 0, 2, 2), 8080),
            40000,
        )
        .map_err(|_| ProbeError::Socket.into_io())?;
    let start = hunde::runtime::monotonic_usec();
    let mut sent = 0;
    let mut received = 0;
    let mut short = false;
    loop {
        hunde::driver::prepare_wait();
        let elapsed = hunde::runtime::monotonic_usec().saturating_sub(start);
        if elapsed > 30_000_000 {
            return Err(ProbeError::Deadline.into_io());
        }
        let now = Instant::from_micros(elapsed as i64);
        let mut worked = left.advance().map_err(hunde::packet::Error::into_io)?;
        worked |= right.advance().map_err(hunde::packet::Error::into_io)?;
        poll(&mut left_interface, &mut left, &mut left_sockets, now);
        poll(&mut right_interface, &mut right, &mut right_sockets, now);
        let socket = left_sockets.get_mut::<tcp::Socket>(client);
        if sent < PAYLOAD_LENGTH && socket.can_send() {
            let mut bytes = [0; 1024];
            let length = bytes.len().min(PAYLOAD_LENGTH - sent);
            for (offset, byte) in bytes[..length].iter_mut().enumerate() {
                *byte = value(sent + offset);
            }
            let count = socket
                .send_slice(&bytes[..length])
                .map_err(|_| ProbeError::Socket.into_io())?;
            short |= count < length;
            sent += count;
            worked = true;
        }
        let socket = right_sockets.get_mut::<tcp::Socket>(server);
        if socket.can_recv() {
            let mut bytes = [0; 512];
            let length = socket
                .recv_slice(&mut bytes)
                .map_err(|_| ProbeError::Socket.into_io())?;
            if bytes[..length]
                .iter()
                .enumerate()
                .any(|(offset, byte)| *byte != value(received + offset))
            {
                return Err(ProbeError::Payload.into_io());
            }
            received += length;
            worked = true;
        }
        if received == PAYLOAD_LENGTH {
            left_sockets.get_mut::<tcp::Socket>(client).close();
            right_sockets.get_mut::<tcp::Socket>(server).close();
            let closed = |socket: &tcp::Socket<'_>| {
                matches!(socket.state(), tcp::State::Closed | tcp::State::TimeWait)
            };
            if closed(left_sockets.get::<tcp::Socket>(client))
                && closed(right_sockets.get::<tcp::Socket>(server))
                && left.idle()
                && right.idle()
            {
                if !short {
                    return Err(ProbeError::Incomplete.into_io());
                }
                break;
            }
        }
        if !worked {
            hunde::driver::wait_ticks(1);
        }
    }
    left.close().map_err(hunde::packet::Error::into_io)?;
    right.close().map_err(hunde::packet::Error::into_io)?;
    hunde::runtime::log(b"network_probe: packet Channel cable TCP cleanup ok");
    Ok(())
}
