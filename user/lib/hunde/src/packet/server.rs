use super::{Device, Direction, Error, Frame, Mapping, wire};
use crate::{channel, protocol::*};
use alloc::{collections::VecDeque, vec, vec::Vec};

struct Session {
    channel: channel::Server,
    peer: u64,
    direction: Option<Direction>,
    mapping: Option<Mapping>,
    request: Option<wire::Request>,
    frame: Option<Frame>,
    reply: Option<wire::Response>,
    closing: bool,
    close_wait: bool,
    allocating: bool,
    releasing: bool,
}

impl Session {
    fn new(channel: channel::Server) -> Self {
        Self {
            channel,
            peer: 0,
            direction: None,
            mapping: None,
            request: None,
            frame: None,
            reply: None,
            closing: false,
            close_wait: false,
            allocating: false,
            releasing: false,
        }
    }
}

struct Job {
    connection: u64,
    request: wire::BufferRequest,
    acquire: bool,
}

pub struct Server<Driver: Device> {
    driver: Driver,
    endpoint: Vec<u8>,
    listener: channel::Listener,
    manager: channel::Client,
    sessions: Vec<Session>,
    jobs: VecDeque<Job>,
    active: Option<Job>,
    owner: Option<u64>,
    closing: bool,
}

impl<Driver: Device> Server<Driver> {
    pub fn new(endpoint: &[u8], driver: Driver) -> Result<Self, Error> {
        if endpoint.is_empty() || endpoint.len() > 255 || endpoint.contains(&0) {
            return Err(Error::Invalid);
        }
        let manager = channel::Client::open(b"hunde-puffer")?;
        let listener = channel::Listener::listen(endpoint)?;
        listener.publish_ready(endpoint)?;
        Ok(Self {
            driver,
            endpoint: endpoint.to_vec(),
            listener,
            manager,
            sessions: Vec::new(),
            jobs: VecDeque::new(),
            active: None,
            owner: None,
            closing: false,
        })
    }

    fn begin_close(&mut self) {
        if self.closing {
            return;
        }
        self.closing = true;
        self.driver.clear();
        let info = self.driver.info();
        for session in &mut self.sessions {
            if Some(session.peer) != self.owner {
                continue;
            }
            session.closing = true;
            session.frame = None;
            if session.request.take().is_some() && !session.close_wait {
                session.reply = Some(wire::Response::new(PACKET_CLOSED, info));
            }
        }
    }

    fn manager_step(&mut self) -> Result<bool, Error> {
        let mut worked = false;
        if self.active.is_none()
            && let Some(job) = self.jobs.front()
            && self.manager.send(wire::bytes(&job.request))?
        {
            self.active = self.jobs.pop_front();
            worked = true;
        }
        if self.active.is_none() {
            return Ok(worked);
        }
        let mut bytes = [0; 40];
        let Some(length) = self.manager.receive(&mut bytes)? else {
            return Ok(worked);
        };
        let reply: wire::BufferReply = wire::decode(&bytes[..length])?;
        let job = self.active.take().ok_or(Error::Invalid)?;
        let session = self
            .sessions
            .iter_mut()
            .find(|session| session.channel.connection() == job.connection)
            .ok_or(Error::Invalid)?;
        if job.acquire {
            session.allocating = false;
            if reply.status == BUFFER_OK {
                let direction = session.direction.ok_or(Error::Invalid)?;
                match Mapping::attach(reply.buffer, session.peer, direction) {
                    Ok(mapping) => session.mapping = Some(mapping),
                    Err(_) => {
                        session.releasing = true;
                        session.channel.close();
                        self.jobs.push_back(Job {
                            connection: job.connection,
                            request: wire::BufferRequest::release(reply.buffer),
                            acquire: false,
                        });
                        self.begin_close();
                        return Ok(true);
                    }
                }
                if !session.closing {
                    let mut response = wire::Response::new(PACKET_OK, self.driver.info());
                    response.buffer = reply.buffer;
                    session.reply = Some(response);
                }
            } else {
                session.direction = None;
                session.reply = Some(wire::Response::new(PACKET_IO_ERROR, self.driver.info()));
            }
        } else {
            if reply.status != BUFFER_OK {
                return Err(Error::Io);
            }
            session.mapping = None;
            session.direction = None;
            session.releasing = false;
        }
        Ok(true)
    }

    fn cleanup(&mut self) {
        for session in &mut self.sessions {
            if session.closing
                && !session.releasing
                && let Some(mapping) = &session.mapping
            {
                self.jobs.push_back(Job {
                    connection: session.channel.connection(),
                    request: wire::BufferRequest::release(mapping.buffer),
                    acquire: false,
                });
                session.releasing = true;
            }
        }
        if self.closing
            && !self
                .sessions
                .iter()
                .any(|session| session.allocating || session.releasing || session.mapping.is_some())
        {
            for session in &mut self.sessions {
                if session.close_wait {
                    session.reply = Some(wire::Response::new(PACKET_OK, self.driver.info()));
                    session.close_wait = false;
                }
            }
            self.owner = None;
            self.closing = false;
        }
        self.sessions.retain(|session| {
            session.channel.alive()
                || session.mapping.is_some()
                || session.allocating
                || session.releasing
        });
    }

    pub fn step(&mut self) -> Result<bool, Error> {
        let mut worked = false;
        for connection in self.listener.connections() {
            if self
                .sessions
                .iter()
                .any(|session| session.channel.connection() == connection)
            {
                continue;
            }
            let Ok(channel) = channel::Server::accept(connection) else {
                worked = true;
                continue;
            };
            if self.sessions.len() < PACKET_SESSION_LIMIT as usize {
                self.sessions.push(Session::new(channel));
            }
            worked = true;
        }
        if self
            .sessions
            .iter()
            .any(|session| !session.channel.alive() && Some(session.peer) == self.owner)
        {
            worked |= !self.closing;
            self.begin_close();
        }
        worked |= self.manager_step()?;
        let info = self.driver.info();
        for index in 0..self.sessions.len() {
            if !self.sessions[index].channel.alive() {
                continue;
            }
            let session = &mut self.sessions[index];
            if let Some(response) = &session.reply {
                match session.channel.reply(wire::bytes(response)) {
                    Ok(true) => {
                        session.reply = None;
                        worked = true;
                    }
                    Ok(false) => {}
                    Err(_) => {
                        session.channel.close();
                        worked = true;
                    }
                }
                continue;
            }
            if session.allocating || session.close_wait {
                continue;
            }
            if session.request.is_none() {
                let mut bytes = [0; 512];
                let length = match session.channel.receive(&mut bytes) {
                    Ok(Some(length)) => length,
                    Ok(None) => continue,
                    Err(_) => {
                        session.channel.close();
                        worked = true;
                        continue;
                    }
                };
                worked = true;
                session.peer = session.channel.peer_pid();
                if session.peer == 0 {
                    session.channel.close();
                    continue;
                }
                match wire::decode::<wire::Request>(&bytes[..length]) {
                    Ok(request) if request.valid() => session.request = Some(request),
                    _ => {
                        session.reply = Some(wire::Response::new(PACKET_INVALID, info));
                        continue;
                    }
                }
            }
            let request = session.request.ok_or(Error::Invalid)?;
            let status = match request.operation {
                PACKET_OPEN_RX | PACKET_OPEN_TX => {
                    let direction = if request.operation == PACKET_OPEN_RX {
                        Direction::Rx
                    } else {
                        Direction::Tx
                    };
                    if self.closing
                        || session.closing
                        || session.direction.is_some()
                        || self.owner.is_some_and(|owner| owner != session.peer)
                        || self
                            .sessions
                            .iter()
                            .any(|other| other.direction == Some(direction))
                    {
                        Some(PACKET_BUSY)
                    } else {
                        let session = &mut self.sessions[index];
                        self.owner = Some(session.peer);
                        session.direction = Some(direction);
                        session.allocating = true;
                        self.jobs.push_back(Job {
                            connection: session.channel.connection(),
                            request: wire::BufferRequest::acquire(
                                &self.endpoint,
                                session.peer,
                                direction.buffer(),
                            ),
                            acquire: true,
                        });
                        None
                    }
                }
                PACKET_CLOSE => {
                    if self.owner.is_some_and(|owner| owner != session.peer) {
                        Some(PACKET_DENIED)
                    } else if self.owner.is_none() {
                        Some(PACKET_OK)
                    } else {
                        session.close_wait = true;
                        self.begin_close();
                        None
                    }
                }
                _ => self.data(index, request)?,
            };
            self.sessions[index].request = if status.is_none()
                && !self.sessions[index].allocating
                && !self.sessions[index].close_wait
            {
                self.sessions[index].request
            } else {
                None
            };
            if let Some(status) = status {
                self.sessions[index].reply = Some(wire::Response::new(status, info));
            }
            worked |= self.sessions[index].reply.is_some();
        }
        self.cleanup();
        Ok(worked)
    }

    fn data(&mut self, index: usize, request: wire::Request) -> Result<Option<i32>, Error> {
        let session = &mut self.sessions[index];
        let info = self.driver.info();
        if session.closing {
            return Ok(Some(PACKET_CLOSED));
        }
        let Some(mapping) = &session.mapping else {
            return Ok(Some(PACKET_DENIED));
        };
        let expected = if request.operation == PACKET_TRANSMIT {
            Direction::Tx
        } else {
            Direction::Rx
        };
        if session.direction != Some(expected)
            || !request.transfer_is_valid(mapping.buffer, expected, info)
            || mapping.validate(request.offset, request.length).is_err()
        {
            return Ok(Some(PACKET_INVALID));
        }
        if !info.link_up() {
            return Ok(Some(PACKET_LINK_DOWN));
        }
        let length = if expected == Direction::Tx {
            let frame = match session.frame.take() {
                Some(frame) => frame,
                None => {
                    let mut bytes = vec![0; request.length as usize];
                    mapping.read(request.offset, &mut bytes)?;
                    Frame::from_vec(bytes)?
                }
            };
            if let Err(frame) = self.driver.transmit(frame) {
                session.frame = Some(frame);
                return Ok(None);
            }
            request.length
        } else {
            let Some(frame) = self.driver.receive()? else {
                return Ok(None);
            };
            if frame.len() > request.length as usize {
                return Ok(Some(PACKET_INVALID));
            }
            mapping.write(request.offset, frame.as_bytes())?;
            frame.len() as u64
        };
        let mut reply = wire::Response::new(PACKET_OK, info);
        reply.buffer = mapping.buffer;
        reply.length = length;
        session.request = None;
        session.reply = Some(reply);
        Ok(None)
    }
}
