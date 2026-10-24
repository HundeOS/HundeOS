use alloc::collections::VecDeque;
use alloc::rc::Rc;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::cell::RefCell;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::inheritance::{Grants, Owner};
use hunde::io::{Handle, Operation, ProcessId, Reply, Request, SessionId};
use hunde::protocol::*;
use hunde::terminal::{Attributes, Stream};

#[cfg(test)]
mod tests;

const INPUT_CAPACITY: usize = 1024;

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum Definition {
    Serial,
    Video,
    Default(String),
}

impl Definition {
    pub(crate) fn parse(source: &str, path: &str) -> Result<Self, IoError> {
        if path == ":console:" && terminal_path(source) {
            return Ok(Self::Default(source.to_string()));
        }
        if !terminal_path(path) {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        match source {
            "uart_rx,uart_tx" => Ok(Self::Serial),
            "keyboard,video_term" => Ok(Self::Video),
            _ => Err(IoError(IO_INVALID_MOUNT_POINT)),
        }
    }

    fn input(&self) -> Result<&'static str, IoError> {
        match self {
            Self::Serial => Ok("uart_rx"),
            Self::Video => Ok("keyboard"),
            Self::Default(_) => Err(IoError(IO_INVALID_MOUNT_POINT)),
        }
    }

    fn output(&self) -> Result<&'static str, IoError> {
        match self {
            Self::Serial => Ok("uart_tx"),
            Self::Video => Ok("video_term"),
            Self::Default(_) => Err(IoError(IO_INVALID_MOUNT_POINT)),
        }
    }
}

pub(crate) fn terminal_path(path: &str) -> bool {
    path.strip_prefix("/dev/vtty")
        .or_else(|| path.strip_prefix("/dev/tty"))
        .is_some_and(|number| {
            !number.is_empty() && number.bytes().all(|byte| byte.is_ascii_digit())
        })
}

struct Input {
    bytes: VecDeque<u8>,
    foreground: Option<(Owner, ProcessId)>,
    interrupted: Option<ProcessId>,
    error: Option<IoError>,
    attributes: Attributes,
}

impl Input {
    fn new() -> Self {
        Self {
            bytes: VecDeque::new(),
            foreground: None,
            interrupted: None,
            error: None,
            attributes: Attributes::new(),
        }
    }

    fn receive(&mut self, byte: u8) -> Option<ProcessId> {
        if byte == 3
            && let Some((_, pid)) = self.foreground
        {
            return Some(pid);
        }
        self.bytes.push_back(byte);
        None
    }
}

struct Description {
    handle: Handle,
    owners: Vec<Owner>,
    flags: u64,
}

pub(crate) trait Output: 'static {
    fn write(&mut self, data: &[u8]) -> Result<usize, IoError>;
}

struct DeviceOutput {
    stream: Stream,
}

impl Output for DeviceOutput {
    fn write(&mut self, data: &[u8]) -> Result<usize, IoError> {
        self.stream.write(data)
    }
}

struct Console<Backend> {
    input: Rc<RefCell<Input>>,
    output: Backend,
    descriptions: Vec<Description>,
    grants: Grants,
    next_handle: i64,
}

impl<Backend> Console<Backend> {
    fn new(input: Rc<RefCell<Input>>, output: Backend) -> Self {
        Self {
            input,
            output,
            descriptions: Vec::new(),
            grants: Grants::default(),
            next_handle: IO_SESSION_HANDLE_BASE as i64,
        }
    }

    fn reap(&mut self) {
        self.descriptions.retain(|description| {
            !description.owners.is_empty() || self.grants.retains(description.handle)
        });
    }
}

impl<Backend: Output> IoComponent for Console<Backend> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        if request.session.0 == 0 || request.pid.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let owner = Owner::new(request.session, request.pid);
        let reply = match request.operation {
            Operation::FileInfo(_) => Reply::FileInfo(hunde::io::FileInfo::new(
                IO_MODE_TYPE_CHAR as u32 | 0o666,
                hunde::io::Credentials::new(hunde::io::UserId(0), hunde::io::GroupId(0)),
                0,
                0,
            )),
            Operation::Open(_, flags) => {
                let access = flags & (IO_OPEN_READ | IO_OPEN_WRITE);
                if access == 0
                    || flags
                        & !(IO_OPEN_READ
                            | IO_OPEN_WRITE
                            | IO_OPEN_CREATE
                            | IO_OPEN_EXCLUSIVE
                            | IO_OPEN_TRUNCATE)
                        != 0
                    || (flags & IO_OPEN_TRUNCATE != 0 && access & IO_OPEN_WRITE == 0)
                    || (flags & IO_OPEN_EXCLUSIVE != 0 && flags & IO_OPEN_CREATE == 0)
                {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if flags & IO_OPEN_EXCLUSIVE != 0 {
                    return Err(IoError(IO_ALREADY_EXISTS));
                }
                if self.descriptions.len() == 64 {
                    return Err(IoError(IO_REGISTER_FAILED));
                }
                let handle = Handle(self.next_handle);
                self.next_handle = self
                    .next_handle
                    .checked_add(1)
                    .ok_or(IoError(IO_REGISTER_FAILED))?;
                self.descriptions.push(Description {
                    handle,
                    owners: alloc::vec![owner],
                    flags: access,
                });
                Reply::Value(handle.0)
            }
            Operation::InheritCommit { token, target } => {
                self.grants.commit(token, owner, target)?;
                Reply::Value(0)
            }
            Operation::InheritAbort(token) => {
                self.grants.abort(token, owner)?;
                self.reap();
                Reply::Value(0)
            }
            Operation::InheritClaim { token, flags } => {
                let grant = self.grants.claim(token, owner, flags)?;
                let description = self
                    .descriptions
                    .iter_mut()
                    .find(|description| description.handle == grant.handle)
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                if !description.owners.contains(&owner) {
                    if description.owners.len() == 64 {
                        return Err(IoError(IO_REGISTER_FAILED));
                    }
                    description.owners.push(owner);
                }
                self.grants.complete(token)?;
                Reply::Value(grant.handle.0)
            }
            Operation::Release => {
                self.disconnect(request.session)?;
                Reply::Value(0)
            }
            _ => {
                let index = self
                    .descriptions
                    .iter()
                    .position(|description| {
                        description.handle == request.handle && description.owners.contains(&owner)
                    })
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                let flags = self.descriptions[index].flags;
                match request.operation {
                    Operation::Read(buffer) if flags & IO_OPEN_READ != 0 => {
                        let mut input = self.input.borrow_mut();
                        if input.interrupted == Some(request.pid) {
                            input.interrupted = None;
                            return Err(IoError(IO_INTERRUPTED));
                        }
                        if input.bytes.is_empty() && !buffer.is_empty() {
                            if let Some(error) = input.error {
                                return Err(error);
                            }
                            if request.transfer_flags & IO_TRANSFER_NONBLOCK != 0 {
                                return Err(IoError(IO_WOULD_BLOCK));
                            }
                            return Ok(None);
                        }
                        let count = buffer.len().min(input.bytes.len());
                        for target in &mut buffer[..count] {
                            *target = input.bytes.pop_front().ok_or(IoError(IO_PANIC))?;
                        }
                        Reply::Value(count as i64)
                    }
                    Operation::Write(buffer) if flags & IO_OPEN_WRITE != 0 => {
                        Reply::Value(self.output.write(buffer)? as i64)
                    }
                    Operation::GetTerminalAttributes(attributes) => {
                        *attributes = self.input.borrow().attributes;
                        Reply::Value(0)
                    }
                    Operation::SetTerminalAttributes(attributes) => {
                        self.input.borrow_mut().attributes = attributes;
                        Reply::Value(0)
                    }
                    Operation::SetForegroundProcess(pid) if flags & IO_OPEN_READ != 0 => {
                        let mut input = self.input.borrow_mut();
                        input.foreground = if pid.0 == 0 { None } else { Some((owner, pid)) };
                        input.interrupted = None;
                        Reply::Value(0)
                    }
                    Operation::InheritPrepare => {
                        return self.grants.prepare(request.handle, owner, flags).map(Some);
                    }
                    Operation::Close => {
                        self.descriptions[index]
                            .owners
                            .retain(|candidate| *candidate != owner);
                        self.grants.close(request.handle, owner);
                        self.reap();
                        Reply::Value(0)
                    }
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                }
            }
        };
        Ok(Some(reply))
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        for description in &mut self.descriptions {
            description.owners.retain(|owner| owner.session != session);
        }
        self.grants.release(session);
        self.reap();
        let mut input = self.input.borrow_mut();
        if input
            .foreground
            .is_some_and(|(owner, _)| owner.session == session)
        {
            input.foreground = None;
        }
        Ok(())
    }
}

pub(crate) fn serve(
    definition: Definition,
    name: hunde::service::ServiceName,
    dispatcher: crate::host::dispatch::Dispatcher,
    parker: &mut hunde::fibre::SchedulerParker<'_>,
    ready: impl FnOnce(Result<(), IoError>),
) -> Result<(), IoError> {
    if let Definition::Default(target) = definition {
        ready(dispatcher.alias(name, &target));
        parker.notify()?;
        return Ok(());
    }
    let mut source = match Stream::open(definition.input()?, IO_OPEN_READ) {
        Ok(source) => source,
        Err(error) => {
            ready(Err(error));
            parker.notify()?;
            return Ok(());
        }
    };
    let output = match Stream::open(definition.output()?, IO_OPEN_WRITE) {
        Ok(output) => output,
        Err(error) => {
            ready(Err(error));
            parker.notify()?;
            return Ok(());
        }
    };
    let input = Rc::new(RefCell::new(Input::new()));
    let legacy = match definition {
        Definition::Serial => ":tty:",
        Definition::Video => ":keyboard:",
        Definition::Default(_) => return Err(IoError(IO_INVALID_MOUNT_POINT)),
    };
    let mut worker = crate::host::dispatch::Worker::new(
        dispatcher.clone(),
        name.clone(),
        Console::new(input.clone(), DeviceOutput { stream: output }),
    )?;
    dispatcher.alias(hunde::service::ServiceName::new(legacy), name.as_str())?;
    let pump = input.clone();
    drop(parker.spawn(move |parker| {
        loop {
            let space = hunde::fibre::block_on(
                core::future::poll_fn(|_| {
                    let space = INPUT_CAPACITY - pump.borrow().bytes.len();
                    if space == 0 {
                        core::task::Poll::Pending
                    } else {
                        core::task::Poll::Ready(space)
                    }
                }),
                parker,
            )?;
            let mut buffer = [0; 64];
            let capacity = buffer.len().min(space);
            match source.read(&mut buffer[..capacity]) {
                Ok(0) => {
                    pump.borrow_mut().error = Some(IoError(IO_NO_DEVICE_FOUND));
                    parker.notify()?;
                    return Ok(());
                }
                Ok(count) => {
                    for byte in &buffer[..count] {
                        let interrupt = pump.borrow_mut().receive(*byte);
                        if let Some(pid) = interrupt
                            && hunde::terminal::interrupt(pid).is_ok()
                        {
                            pump.borrow_mut().interrupted = Some(pid);
                        }
                    }
                }
                Err(error) => {
                    pump.borrow_mut().error = Some(error);
                    parker.notify()?;
                    return Ok(());
                }
            }
            parker.notify()?;
        }
    })?);
    ready(Ok(()));
    parker.notify()?;
    loop {
        hunde::fibre::block_on(
            core::future::poll_fn(|_| {
                if worker.poll() {
                    core::task::Poll::Ready(())
                } else {
                    core::task::Poll::Pending
                }
            }),
            parker,
        )?;
        parker.notify()?;
    }
}
