use alloc::collections::VecDeque;
use alloc::rc::Rc;
use alloc::vec::Vec;
use core::cell::RefCell;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::inheritance::Owner;
use hunde::io::{
    FileWatch, Handle, InheritanceToken, Operation, OwnedRequest, ProcessId, Reply, Request,
    SessionId,
};
use hunde::protocol::{
    IO_PANIC, IO_PERMISSION_DENIED, IO_REGISTER_FAILED, IO_SESSION_HANDLE_BASE, IO_UNKNOWN_HANDLE,
    IO_WAIT_FOR_DATA,
};
use hunde::service::ServiceName;

const MAX_REQUESTS: usize = 64;
const MAX_HANDLES: usize = 512;

#[derive(Clone, Copy, PartialEq, Eq)]
struct ComponentId(usize);
#[derive(Clone, Copy, PartialEq, Eq)]
struct JobId(u64);

#[derive(Clone, Copy)]
struct Binding {
    host: Handle,
    backend: Handle,
    component: ComponentId,
    owner: Owner,
}
impl Binding {
    fn new(host: Handle, backend: Handle, component: ComponentId, owner: Owner) -> Self {
        Self {
            host,
            backend,
            component,
            owner,
        }
    }

    fn shares_backend_owner(&self, other: Self) -> bool {
        self.component == other.component
            && self.backend == other.backend
            && self.owner == other.owner
    }
}

struct Grant {
    host: InheritanceToken,
    backend: InheritanceToken,
    source: Binding,
    target: Option<ProcessId>,
    flags: u64,
}
impl Grant {
    fn new(host: InheritanceToken, backend: InheritanceToken, source: Binding, flags: u64) -> Self {
        Self {
            host,
            backend,
            source,
            target: None,
            flags,
        }
    }
}

enum Action {
    Pass,
    Open(Handle),
    Close {
        binding: Binding,
        close_backend: bool,
    },
    Pipe(Handle, Handle),
    Prepare(InheritanceToken, Binding),
    Commit(InheritanceToken, ProcessId),
    Claim(InheritanceToken, Handle),
    Abort(InheritanceToken),
    Watch(Handle),
    Wait(Vec<(Handle, Handle)>),
    Release,
}

enum Work {
    Request(OwnedRequest),
    Disconnect,
}
struct Job {
    id: JobId,
    owner: Owner,
    component: ComponentId,
    work: Work,
    action: Action,
}
impl Job {
    fn new(id: JobId, owner: Owner, component: ComponentId, work: Work, action: Action) -> Self {
        Self {
            id,
            owner,
            component,
            work,
            action,
        }
    }
}
struct Completed {
    request: Option<OwnedRequest>,
    reply: Result<Reply, IoError>,
}

struct WatchGroup {
    component: ComponentId,
    watches: Vec<FileWatch>,
    handles: Vec<(Handle, Handle)>,
}

impl WatchGroup {
    fn new(component: ComponentId) -> Self {
        Self {
            component,
            watches: Vec::new(),
            handles: Vec::new(),
        }
    }
}
impl Completed {
    fn new(request: Option<OwnedRequest>, reply: Result<Reply, IoError>) -> Self {
        Self { request, reply }
    }
}
struct Pending {
    owner: Owner,
    jobs: Vec<JobId>,
    completed: Option<Completed>,
    all: bool,
    disconnect: bool,
}
impl Pending {
    fn new(owner: Owner, all: bool, disconnect: bool) -> Self {
        Self {
            owner,
            jobs: Vec::new(),
            completed: None,
            all,
            disconnect,
        }
    }
}
struct Component {
    name: ServiceName,
    queue: VecDeque<Job>,
    reclaim_pending: bool,
}
impl Component {
    fn new(name: ServiceName) -> Self {
        Self {
            name,
            queue: VecDeque::new(),
            reclaim_pending: false,
        }
    }
}

struct State {
    components: Vec<Component>,
    bindings: Vec<Binding>,
    grants: Vec<Grant>,
    pending: Vec<Pending>,
    next_handle: Option<i64>,
    next_token: u64,
    next_job: u64,
    notify: bool,
}
impl State {
    fn new() -> Self {
        Self {
            components: Vec::new(),
            bindings: Vec::new(),
            grants: Vec::new(),
            pending: Vec::new(),
            next_handle: Some(IO_SESSION_HANDLE_BASE as i64),
            next_token: 1,
            next_job: 1,
            notify: false,
        }
    }
    fn component(&self, name: &str) -> Option<ComponentId> {
        self.components
            .iter()
            .position(|component| component.name.as_str() == name)
            .map(ComponentId)
    }
    fn binding(&self, handle: Handle, owner: Owner, watch: bool) -> Result<Binding, IoError> {
        let binding = *self
            .bindings
            .iter()
            .find(|binding| binding.host == handle)
            .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
        if binding.owner.pid != owner.pid || (!watch && binding.owner.session != owner.session) {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        Ok(binding)
    }
    fn grant(&self, token: InheritanceToken) -> Result<&Grant, IoError> {
        self.grants
            .iter()
            .find(|grant| grant.host == token)
            .ok_or(IoError(IO_UNKNOWN_HANDLE))
    }
    fn reserve(&mut self) -> Result<Handle, IoError> {
        if self.bindings.len() + self.pending.len() * 2 >= MAX_HANDLES {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let value = self.next_handle.ok_or(IoError(IO_REGISTER_FAILED))?;
        self.next_handle = value.checked_add(1);
        Ok(Handle(value))
    }
    fn token(&mut self) -> Result<InheritanceToken, IoError> {
        if self.grants.len() + self.pending.len() >= 128 || self.next_token > i64::MAX as u64 {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let token = InheritanceToken(self.next_token);
        self.next_token += 1;
        Ok(token)
    }
    fn enqueue(
        &mut self,
        pending: &mut Pending,
        component: ComponentId,
        work: Work,
        action: Action,
    ) -> Result<(), IoError> {
        let id = JobId(self.next_job);
        self.next_job = self
            .next_job
            .checked_add(1)
            .ok_or(IoError(IO_REGISTER_FAILED))?;
        pending.jobs.push(id);
        self.components[component.0].queue.push_back(Job::new(
            id,
            pending.owner,
            component,
            work,
            action,
        ));
        self.notify = true;
        Ok(())
    }
    fn active(&self, job: &Job) -> bool {
        self.pending
            .iter()
            .any(|pending| pending.owner == job.owner && pending.jobs.contains(&job.id))
    }

    fn apply(&mut self, job: &Job, reply: Reply) -> Result<Reply, IoError> {
        let owner = job.owner;
        let component = job.component;
        match &job.action {
            Action::Pass => Ok(reply),
            Action::Open(host) => {
                let Reply::Value(backend) = reply else {
                    return Err(IoError(IO_PANIC));
                };
                self.bindings
                    .push(Binding::new(*host, Handle(backend), component, owner));
                Ok(Reply::Value(host.0))
            }
            Action::Pipe(reader, writer) => {
                let Reply::Pipe {
                    reader: backend_reader,
                    writer: backend_writer,
                } = reply
                else {
                    return Err(IoError(IO_PANIC));
                };
                self.bindings
                    .push(Binding::new(*reader, backend_reader, component, owner));
                self.bindings
                    .push(Binding::new(*writer, backend_writer, component, owner));
                Ok(Reply::Pipe {
                    reader: *reader,
                    writer: *writer,
                })
            }
            Action::Close {
                binding,
                close_backend,
            } => {
                self.bindings.retain(|entry| entry.host != binding.host);
                if *close_backend {
                    self.grants.retain(|grant| {
                        !grant.source.shares_backend_owner(*binding) || grant.target.is_some()
                    });
                }
                Ok(reply)
            }
            Action::Prepare(token, source) => {
                let Reply::Inheritance {
                    token: backend,
                    flags,
                } = reply
                else {
                    return Err(IoError(IO_PANIC));
                };
                self.grants
                    .push(Grant::new(*token, backend, *source, flags));
                Ok(Reply::Inheritance {
                    token: *token,
                    flags,
                })
            }
            Action::Commit(token, target) => {
                self.grants
                    .iter_mut()
                    .find(|grant| grant.host == *token)
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?
                    .target = Some(*target);
                Ok(reply)
            }
            Action::Claim(token, host) => {
                let Reply::Value(backend) = reply else {
                    return Err(IoError(IO_PANIC));
                };
                self.bindings
                    .push(Binding::new(*host, Handle(backend), component, owner));
                self.grants.retain(|grant| grant.host != *token);
                Ok(Reply::Value(host.0))
            }
            Action::Abort(token) => {
                self.grants.retain(|grant| grant.host != *token);
                Ok(reply)
            }
            Action::Watch(host) => {
                let Reply::FileChange(mut change) = reply else {
                    return Err(IoError(IO_PANIC));
                };
                change.watch.handle = *host;
                Ok(Reply::FileChange(change))
            }
            Action::Wait(handles) => {
                let Reply::FileChange(mut change) = reply else {
                    return Err(IoError(IO_PANIC));
                };
                change.watch.handle = handles
                    .iter()
                    .find(|(backend, _)| *backend == change.watch.handle)
                    .ok_or(IoError(IO_PANIC))?
                    .1;
                Ok(Reply::FileChange(change))
            }
            Action::Release => {
                self.bindings
                    .retain(|binding| binding.component != component || binding.owner != owner);
                self.grants.retain(|grant| {
                    grant.source.component != component
                        || grant.source.owner != owner
                        || grant.target.is_some()
                });
                Ok(reply)
            }
        }
    }

    fn finish(&mut self, job: Job, result: Result<Reply, IoError>) {
        if !self.active(&job) {
            if result.is_ok() {
                match &job.action {
                    Action::Commit(token, target) => {
                        if let Some(grant) =
                            self.grants.iter_mut().find(|grant| grant.host == *token)
                        {
                            grant.target = Some(*target);
                        }
                    }
                    Action::Claim(token, _) => self.grants.retain(|grant| grant.host != *token),
                    Action::Release => {
                        self.bindings.retain(|binding| {
                            binding.component != job.component || binding.owner != job.owner
                        });
                        self.grants.retain(|grant| {
                            grant.source.component != job.component
                                || grant.source.owner != job.owner
                                || grant.target.is_some()
                        });
                    }
                    _ => {}
                }
            }
            return;
        }
        let result = result.and_then(|reply| self.apply(&job, reply));
        let Some(pending) = self
            .pending
            .iter_mut()
            .find(|pending| pending.owner == job.owner && pending.jobs.contains(&job.id))
        else {
            return;
        };
        pending.jobs.retain(|id| *id != job.id);
        if !pending.all || pending.jobs.is_empty() || result.is_err() {
            pending.jobs.clear();
            let request = match job.work {
                Work::Request(request) => Some(request),
                Work::Disconnect => None,
            };
            pending.completed = Some(Completed::new(request, result));
        }
    }
}

#[derive(Clone)]
pub(crate) struct Dispatcher(Rc<RefCell<State>>);
impl Dispatcher {
    pub(crate) fn new() -> Self {
        Self(Rc::new(RefCell::new(State::new())))
    }
    pub(crate) fn contains(&self, service: &str) -> bool {
        self.0.borrow().component(service).is_some()
    }
    pub(crate) fn take_notification(&self) -> bool {
        core::mem::take(&mut self.0.borrow_mut().notify)
    }

    pub(crate) fn memory_pressure(&self, pressure: hunde::service::MemoryPressure) {
        if pressure == hunde::service::MemoryPressure::Normal {
            return;
        }
        let mut state = self.0.borrow_mut();
        for component in &mut state.components {
            component.reclaim_pending = true;
        }
        state.notify = true;
    }

    pub(crate) fn execute(
        &self,
        service: Option<&str>,
        mut request: Request<'_>,
    ) -> Result<Option<Reply>, IoError> {
        let owner = Owner::new(request.session, request.pid);
        let mut state = self.0.borrow_mut();
        if let Some(index) = state
            .pending
            .iter()
            .position(|pending| pending.owner == owner)
        {
            if state.pending[index].disconnect {
                return Err(IoError(IO_PERMISSION_DENIED));
            }
            if state.pending[index].completed.is_none() {
                return Ok(None);
            }
            let completed = state
                .pending
                .remove(index)
                .completed
                .ok_or(IoError(IO_PANIC))?;
            let reply = completed.reply?;
            return completed
                .request
                .ok_or(IoError(IO_PANIC))?
                .complete(request, reply)
                .map(Some);
        }
        if state.pending.len() >= MAX_REQUESTS {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let mut pending = Pending::new(owner, false, false);
        if let Operation::WaitFileChange(watches) = &request.operation {
            if watches.is_empty() {
                return Err(IoError(IO_UNKNOWN_HANDLE));
            }
            let mut groups: Vec<WatchGroup> = Vec::new();
            for watch in watches {
                let binding = state.binding(watch.handle, owner, true)?;
                let index = if let Some(index) = groups
                    .iter()
                    .position(|group| group.component == binding.component)
                {
                    index
                } else {
                    groups.push(WatchGroup::new(binding.component));
                    groups.len() - 1
                };
                groups[index]
                    .watches
                    .push(FileWatch::new(binding.backend, watch.generation));
                groups[index].handles.push((binding.backend, binding.host));
            }
            for group in groups {
                let owned = OwnedRequest::new(Request::new(
                    owner.session,
                    owner.pid,
                    Handle(0),
                    Operation::WaitFileChange(group.watches),
                ));
                state.enqueue(
                    &mut pending,
                    group.component,
                    Work::Request(owned),
                    Action::Wait(group.handles),
                )?;
            }
        } else if matches!(request.operation, Operation::Release) {
            pending.all = true;
            for index in 0..state.components.len() {
                let owned = OwnedRequest::new(Request::new(
                    owner.session,
                    owner.pid,
                    Handle(0),
                    Operation::Release,
                ));
                state.enqueue(
                    &mut pending,
                    ComponentId(index),
                    Work::Request(owned),
                    Action::Release,
                )?;
            }
            if pending.jobs.is_empty() {
                return Ok(Some(Reply::Value(0)));
            }
        } else {
            let (component, action) = match &mut request.operation {
                Operation::InheritCommit { token, target } => {
                    let grant = state.grant(*token)?;
                    if grant.source.owner != owner {
                        return Err(IoError(IO_PERMISSION_DENIED));
                    }
                    let (component, backend, host) =
                        (grant.source.component, grant.backend, grant.host);
                    *token = backend;
                    (component, Action::Commit(host, *target))
                }
                Operation::InheritAbort(token) => {
                    let grant = state.grant(*token)?;
                    if grant.source.owner != owner {
                        return Err(IoError(IO_PERMISSION_DENIED));
                    }
                    let (component, backend, host) =
                        (grant.source.component, grant.backend, grant.host);
                    *token = backend;
                    (component, Action::Abort(host))
                }
                Operation::InheritClaim { token, flags } => {
                    let grant = state.grant(*token)?;
                    if grant.target != Some(owner.pid) || grant.flags != *flags {
                        return Err(IoError(IO_PERMISSION_DENIED));
                    }
                    let (component, backend, host) =
                        (grant.source.component, grant.backend, grant.host);
                    *token = backend;
                    (component, Action::Claim(host, state.reserve()?))
                }
                Operation::Open(..)
                | Operation::PipeCreate { .. }
                | Operation::FileInfo(..)
                | Operation::ListDirectory(..)
                | Operation::Mkdir(..)
                | Operation::Remove(..)
                | Operation::Link { .. } => {
                    let component = state
                        .component(service.ok_or(IoError(IO_UNKNOWN_HANDLE))?)
                        .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                    let action = match request.operation {
                        Operation::Open(..) => Action::Open(state.reserve()?),
                        Operation::PipeCreate { .. } => {
                            if state.bindings.len() + state.pending.len() * 2 > MAX_HANDLES - 2 {
                                return Err(IoError(IO_REGISTER_FAILED));
                            }
                            Action::Pipe(state.reserve()?, state.reserve()?)
                        }
                        _ => Action::Pass,
                    };
                    (component, action)
                }
                _ => {
                    let binding = state.binding(request.handle, owner, false)?;
                    request.handle = binding.backend;
                    let action = match request.operation {
                        Operation::Accept => Action::Open(state.reserve()?),
                        Operation::Close => Action::Close {
                            binding,
                            close_backend: !state.bindings.iter().any(|entry| {
                                entry.host != binding.host && entry.shares_backend_owner(binding)
                            }),
                        },
                        Operation::InheritPrepare => Action::Prepare(state.token()?, binding),
                        Operation::WatchFile => Action::Watch(binding.host),
                        _ => Action::Pass,
                    };
                    (binding.component, action)
                }
            };
            state.enqueue(
                &mut pending,
                component,
                Work::Request(OwnedRequest::new(request)),
                action,
            )?;
        }
        state.pending.push(pending);
        Ok(None)
    }

    pub(crate) fn disconnect(&self, session: SessionId) -> Result<(), IoError> {
        let mut state = self.0.borrow_mut();
        if let Some(index) = state
            .pending
            .iter()
            .position(|pending| pending.owner.session == session && pending.disconnect)
        {
            if state.pending[index].completed.is_none() {
                return Err(IoError(IO_WAIT_FOR_DATA));
            }
            return state
                .pending
                .remove(index)
                .completed
                .ok_or(IoError(IO_PANIC))?
                .reply
                .map(|_| ());
        }
        let owner = state
            .pending
            .iter()
            .find(|pending| pending.owner.session == session)
            .map(|pending| pending.owner)
            .or_else(|| {
                state
                    .bindings
                    .iter()
                    .find(|binding| binding.owner.session == session)
                    .map(|binding| binding.owner)
            })
            .or_else(|| {
                state
                    .grants
                    .iter()
                    .find(|grant| grant.source.owner.session == session)
                    .map(|grant| grant.source.owner)
            });
        let Some(owner) = owner else {
            return Ok(());
        };
        state
            .pending
            .retain(|pending| pending.owner.session != session);
        let mut pending = Pending::new(owner, true, true);
        for index in 0..state.components.len() {
            state.enqueue(
                &mut pending,
                ComponentId(index),
                Work::Disconnect,
                Action::Release,
            )?;
        }
        if pending.jobs.is_empty() {
            return Ok(());
        }
        state.pending.push(pending);
        Err(IoError(IO_WAIT_FOR_DATA))
    }
}

pub(crate) struct Hosted<Driver>(Rc<RefCell<Driver>>);
impl<Driver> Hosted<Driver> {
    pub(crate) fn new(driver: Driver) -> Self {
        Self(Rc::new(RefCell::new(driver)))
    }
}
impl<Driver> Clone for Hosted<Driver> {
    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}
impl<Driver: IoComponent> IoComponent for Hosted<Driver> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.0
            .try_borrow_mut()
            .map_err(|_| IoError(IO_WAIT_FOR_DATA))?
            .execute(request)
    }
    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.0
            .try_borrow_mut()
            .map_err(|_| IoError(IO_WAIT_FOR_DATA))?
            .disconnect(session)
    }

    fn reclaim_cache(&mut self) -> Result<(), IoError> {
        self.0
            .try_borrow_mut()
            .map_err(|_| IoError(IO_WAIT_FOR_DATA))?
            .reclaim_cache()
    }
}

pub(crate) struct Worker<Driver> {
    dispatcher: Dispatcher,
    component: ComponentId,
    driver: Hosted<Driver>,
    waiting: Vec<Job>,
}
impl<Driver: super::Subsystem> super::Subsystem for Hosted<Driver> {
    fn endpoint(&self) -> ServiceName {
        self.0.borrow().endpoint()
    }
    fn ready_message(&self) -> hunde::service::ReadinessMessage {
        self.0.borrow().ready_message()
    }
}
impl<Driver: IoComponent> Worker<Driver> {
    pub(crate) fn new(
        dispatcher: Dispatcher,
        name: ServiceName,
        driver: Driver,
    ) -> Result<Self, IoError> {
        Self::with_driver(dispatcher, name, Hosted::new(driver))
    }
    pub(crate) fn with_driver(
        dispatcher: Dispatcher,
        name: ServiceName,
        driver: Hosted<Driver>,
    ) -> Result<Self, IoError> {
        let mut state = dispatcher.0.borrow_mut();
        if state.component(name.as_str()).is_some() {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let component = ComponentId(state.components.len());
        state.components.push(Component::new(name));
        drop(state);
        Ok(Self {
            dispatcher,
            component,
            driver,
            waiting: Vec::new(),
        })
    }
    pub(crate) fn driver(&self) -> Hosted<Driver> {
        self.driver.clone()
    }
    pub(crate) fn poll(&mut self) -> bool {
        let mut progress = false;
        let reclaim_pending =
            self.dispatcher.0.borrow().components[self.component.0].reclaim_pending;
        if reclaim_pending && self.driver.reclaim_cache().is_ok() {
            self.dispatcher.0.borrow_mut().components[self.component.0].reclaim_pending = false;
            progress = true;
        }
        while let Some(job) = self.dispatcher.0.borrow_mut().components[self.component.0]
            .queue
            .pop_front()
        {
            self.waiting.push(job);
            progress = true;
        }
        let mut remaining = Vec::new();
        for mut job in core::mem::take(&mut self.waiting) {
            if !self.dispatcher.0.borrow().active(&job) {
                continue;
            }
            let result = if matches!(
                job.action,
                Action::Close {
                    close_backend: false,
                    ..
                }
            ) {
                Ok(Some(Reply::Value(0)))
            } else {
                match &mut job.work {
                    Work::Request(request) => {
                        request.with_request(|request| self.driver.execute(request))
                    }
                    Work::Disconnect => match self.driver.disconnect(job.owner.session) {
                        Ok(()) => Ok(Some(Reply::Value(0))),
                        Err(_) => {
                            remaining.push(job);
                            continue;
                        }
                    },
                }
            };
            match result {
                Ok(None) => remaining.push(job),
                result => {
                    self.dispatcher
                        .0
                        .borrow_mut()
                        .finish(job, result.and_then(|reply| reply.ok_or(IoError(IO_PANIC))));
                    progress = true;
                }
            }
        }
        self.waiting = remaining;
        progress
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hunde::io::{FileChange, FileChangeEvents, FileGeneration};
    use hunde::protocol::{IO_FILE_CHANGE_WRITE, IO_OPEN_READ};

    struct Recorder {
        data: Vec<u8>,
        blocked: bool,
        changed: bool,
        cleanup_fails: bool,
        calls: usize,
        cleanups: usize,
        reclaims: usize,
        reclaim_fails: bool,
        last_owner: Owner,
        last_path: Vec<u8>,
    }
    impl Recorder {
        fn new(data: &[u8]) -> Self {
            Self {
                data: data.into(),
                blocked: false,
                changed: false,
                cleanup_fails: false,
                calls: 0,
                cleanups: 0,
                reclaims: 0,
                reclaim_fails: false,
                last_owner: owner(0),
                last_path: Vec::new(),
            }
        }
    }
    struct Driver(Rc<RefCell<Recorder>>);
    impl Driver {
        fn new(data: &[u8]) -> Self {
            Self(Rc::new(RefCell::new(Recorder::new(data))))
        }
    }
    impl IoComponent for Driver {
        fn reclaim_cache(&mut self) -> Result<(), IoError> {
            let mut state = self.0.borrow_mut();
            state.reclaims += 1;
            if state.reclaim_fails {
                return Err(IoError(IO_WAIT_FOR_DATA));
            }
            Ok(())
        }
        fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
            let mut state = self.0.borrow_mut();
            state.calls += 1;
            state.last_owner = Owner::new(request.session, request.pid);
            if state.blocked
                && matches!(request.operation, Operation::Read(_) | Operation::Open(..))
            {
                return Ok(None);
            }
            let reply = match request.operation {
                Operation::Open(path, _) => {
                    state.last_path = path.into();
                    Reply::Value(1)
                }
                Operation::Accept | Operation::InheritClaim { .. } => Reply::Value(1),
                Operation::PipeCreate { .. } => Reply::Pipe {
                    reader: Handle(1),
                    writer: Handle(2),
                },
                Operation::Read(buffer) => {
                    let count = buffer.len().min(state.data.len());
                    buffer[..count].copy_from_slice(&state.data[..count]);
                    Reply::Value(count as i64)
                }
                Operation::Write(buffer) => {
                    state.data = buffer.into();
                    state.changed = true;
                    Reply::Value(buffer.len() as i64)
                }
                Operation::InheritPrepare => Reply::Inheritance {
                    token: InheritanceToken(1),
                    flags: IO_OPEN_READ,
                },
                Operation::WatchFile => Reply::FileChange(FileChange::new(
                    FileWatch::new(request.handle, FileGeneration(0)),
                    FileChangeEvents(0),
                )),
                Operation::WaitFileChange(watches) => {
                    if !state.changed {
                        return Ok(None);
                    }
                    Reply::FileChange(FileChange::new(
                        FileWatch::new(watches[0].handle, FileGeneration(1)),
                        FileChangeEvents(IO_FILE_CHANGE_WRITE),
                    ))
                }
                Operation::Close
                | Operation::Release
                | Operation::InheritCommit { .. }
                | Operation::InheritAbort(_) => Reply::Value(0),
                _ => return Err(IoError(IO_UNKNOWN_HANDLE)),
            };
            Ok(Some(reply))
        }
        fn disconnect(&mut self, _session: SessionId) -> Result<(), IoError> {
            let mut state = self.0.borrow_mut();
            state.cleanups += 1;
            if state.cleanup_fails {
                Err(IoError(IO_PANIC))
            } else {
                Ok(())
            }
        }
    }
    fn owner(session: u64) -> Owner {
        Owner::new(SessionId(session), ProcessId(session + 10))
    }

    #[test]
    fn pressure_reclamation_is_coalesced_per_worker_and_normal_does_not_cancel_it() {
        use hunde::service::MemoryPressure;
        let dispatcher = Dispatcher::new();
        let (mut first, first_state) = make_worker(&dispatcher, "/", b"root");
        let (mut second, second_state) = make_worker(&dispatcher, "/mnt", b"mount");
        dispatcher.memory_pressure(MemoryPressure::Normal);
        assert!(!first.poll());
        dispatcher.memory_pressure(MemoryPressure::Low);
        dispatcher.memory_pressure(MemoryPressure::Critical);
        dispatcher.memory_pressure(MemoryPressure::Normal);
        assert_eq!(first_state.borrow().reclaims, 0);
        assert_eq!(second_state.borrow().reclaims, 0);
        assert!(first.poll());
        assert_eq!(first_state.borrow().reclaims, 1);
        assert_eq!(second_state.borrow().reclaims, 0);
        assert!(!first.poll());
        assert!(second.poll());
        assert_eq!(second_state.borrow().reclaims, 1);
    }

    #[test]
    fn busy_or_failed_reclamation_stays_pending_until_the_owner_can_reclaim() {
        use hunde::service::MemoryPressure;
        let dispatcher = Dispatcher::new();
        let (mut worker, state) = make_worker(&dispatcher, "/", b"root");
        dispatcher.memory_pressure(MemoryPressure::Critical);
        let driver = worker.driver();
        let borrowed = driver.0.borrow_mut();
        assert!(!worker.poll());
        assert_eq!(state.borrow().reclaims, 0);
        drop(borrowed);
        state.borrow_mut().reclaim_fails = true;
        assert!(!worker.poll());
        assert_eq!(state.borrow().reclaims, 1);
        state.borrow_mut().reclaim_fails = false;
        assert!(worker.poll());
        assert_eq!(state.borrow().reclaims, 2);
        assert!(!worker.poll());
    }
    fn request(owner: Owner, handle: Handle, operation: Operation<'_>) -> Request<'_> {
        Request::new(owner.session, owner.pid, handle, operation)
    }
    fn make_worker(
        dispatcher: &Dispatcher,
        name: &'static str,
        data: &[u8],
    ) -> (Worker<Driver>, Rc<RefCell<Recorder>>) {
        let driver = Driver::new(data);
        let state = driver.0.clone();
        (
            Worker::new(dispatcher.clone(), ServiceName::new(name), driver).unwrap(),
            state,
        )
    }
    fn open(
        dispatcher: &Dispatcher,
        worker: &mut Worker<Driver>,
        name: &str,
        owner: Owner,
    ) -> Handle {
        assert_eq!(
            dispatcher.execute(
                Some(name),
                request(owner, Handle(0), Operation::Open(b"/file", IO_OPEN_READ))
            ),
            Ok(None)
        );
        assert!(worker.poll());
        let Some(Reply::Value(handle)) = dispatcher
            .execute(
                Some(name),
                request(owner, Handle(0), Operation::Open(b"/file", IO_OPEN_READ)),
            )
            .unwrap()
        else {
            panic!("expected handle")
        };
        Handle(handle)
    }
    fn prepare(
        dispatcher: &Dispatcher,
        worker: &mut Worker<Driver>,
        owner: Owner,
        handle: Handle,
    ) -> InheritanceToken {
        dispatcher
            .execute(None, request(owner, handle, Operation::InheritPrepare))
            .unwrap();
        worker.poll();
        let Some(Reply::Inheritance { token, .. }) = dispatcher
            .execute(None, request(owner, handle, Operation::InheritPrepare))
            .unwrap()
        else {
            panic!("expected grant")
        };
        token
    }

    #[test]
    fn host_handles_isolate_colliding_component_handles_and_authenticate_owners() {
        let dispatcher = Dispatcher::new();
        let (mut first, _) = make_worker(&dispatcher, "/", b"root");
        let (mut second, _) = make_worker(&dispatcher, "/tmp", b"temp");
        let first_handle = open(&dispatcher, &mut first, "/", owner(1));
        let second_handle = open(&dispatcher, &mut second, "/tmp", owner(1));
        assert_ne!(first_handle, second_handle);
        assert!(first_handle.0 >= IO_SESSION_HANDLE_BASE as i64);
        let mut buffer = [99; 8];
        dispatcher
            .execute(
                None,
                request(owner(1), second_handle, Operation::Read(&mut buffer)),
            )
            .unwrap();
        second.poll();
        assert_eq!(buffer, [99; 8]);
        assert_eq!(
            dispatcher.execute(
                None,
                request(owner(1), second_handle, Operation::Read(&mut buffer))
            ),
            Ok(Some(Reply::Value(4)))
        );
        assert_eq!(&buffer[..4], b"temp");
        assert_eq!(
            dispatcher.execute(None, request(owner(2), second_handle, Operation::Close)),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        dispatcher
            .execute(None, request(owner(1), second_handle, Operation::Close))
            .unwrap();
        second.poll();
        assert_eq!(
            dispatcher.execute(None, request(owner(1), second_handle, Operation::Close)),
            Ok(Some(Reply::Value(0)))
        );
        assert_eq!(
            dispatcher.execute(None, request(owner(1), second_handle, Operation::Close)),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_ne!(
            open(&dispatcher, &mut second, "/tmp", owner(1)),
            second_handle
        );
    }

    #[test]
    fn pending_reads_do_not_block_other_workers_or_borrow_client_buffers() {
        let dispatcher = Dispatcher::new();
        let (mut first, blocked) = make_worker(&dispatcher, "/", b"root");
        let (mut second, _) = make_worker(&dispatcher, "/tmp", b"temp");
        let first_handle = open(&dispatcher, &mut first, "/", owner(1));
        let second_handle = open(&dispatcher, &mut second, "/tmp", owner(2));
        blocked.borrow_mut().blocked = true;
        let mut first_buffer = [0; 4];
        let mut second_buffer = [0; 4];
        dispatcher
            .execute(
                None,
                request(owner(1), first_handle, Operation::Read(&mut first_buffer)),
            )
            .unwrap();
        first.poll();
        dispatcher
            .execute(
                None,
                request(owner(2), second_handle, Operation::Read(&mut second_buffer)),
            )
            .unwrap();
        second.poll();
        assert_eq!(
            dispatcher.execute(
                None,
                request(owner(2), second_handle, Operation::Read(&mut second_buffer))
            ),
            Ok(Some(Reply::Value(4)))
        );
        assert_eq!(&second_buffer, b"temp");
        assert_eq!(first_buffer, [0; 4]);
        assert_eq!(
            dispatcher.execute(
                None,
                request(owner(1), first_handle, Operation::Read(&mut first_buffer))
            ),
            Ok(None)
        );
        blocked.borrow_mut().blocked = false;
        first.poll();
        assert_eq!(
            dispatcher.execute(
                None,
                request(owner(1), first_handle, Operation::Read(&mut first_buffer))
            ),
            Ok(Some(Reply::Value(4)))
        );
        assert_eq!(&first_buffer, b"root");
    }

    #[test]
    fn grants_are_host_owned_pid_bound_and_survive_source_disconnect() {
        let dispatcher = Dispatcher::new();
        let (mut first, _) = make_worker(&dispatcher, "/", b"root");
        let (mut second, _) = make_worker(&dispatcher, "/tmp", b"temp");
        let first_handle = open(&dispatcher, &mut first, "/", owner(1));
        let second_handle = open(&dispatcher, &mut second, "/tmp", owner(1));
        let first_token = prepare(&dispatcher, &mut first, owner(1), first_handle);
        let second_token = prepare(&dispatcher, &mut second, owner(1), second_handle);
        assert_ne!(first_token, second_token);
        let operation = || Operation::InheritCommit {
            token: first_token,
            target: owner(2).pid,
        };
        dispatcher
            .execute(None, request(owner(1), Handle(0), operation()))
            .unwrap();
        first.poll();
        dispatcher
            .execute(None, request(owner(1), Handle(0), operation()))
            .unwrap();
        assert_eq!(
            dispatcher.execute(
                None,
                request(
                    owner(3),
                    Handle(0),
                    Operation::InheritClaim {
                        token: first_token,
                        flags: IO_OPEN_READ
                    }
                )
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert!(dispatcher.disconnect(owner(1).session).is_err());
        first.poll();
        second.poll();
        assert_eq!(dispatcher.disconnect(owner(1).session), Ok(()));
        let claim = || Operation::InheritClaim {
            token: first_token,
            flags: IO_OPEN_READ,
        };
        dispatcher
            .execute(None, request(owner(2), Handle(0), claim()))
            .unwrap();
        first.poll();
        let Some(Reply::Value(inherited)) = dispatcher
            .execute(None, request(owner(2), Handle(0), claim()))
            .unwrap()
        else {
            panic!("expected inherited handle")
        };
        assert_ne!(Handle(inherited), first_handle);
        assert_eq!(
            dispatcher.execute(None, request(owner(2), Handle(0), claim())),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            dispatcher.execute(
                None,
                request(
                    owner(2),
                    Handle(0),
                    Operation::InheritClaim {
                        token: second_token,
                        flags: IO_OPEN_READ
                    }
                )
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }

    #[test]
    fn file_waits_span_components_and_return_the_host_handle_on_a_dedicated_channel() {
        let dispatcher = Dispatcher::new();
        let (mut first, _) = make_worker(&dispatcher, "/", b"root");
        let (mut second, changed) = make_worker(&dispatcher, "/tmp", b"temp");
        let first_handle = open(&dispatcher, &mut first, "/", owner(1));
        let second_handle = open(&dispatcher, &mut second, "/tmp", owner(1));
        let watcher = Owner::new(SessionId(99), owner(1).pid);
        let watches = || {
            Operation::WaitFileChange(alloc::vec![
                FileWatch::new(first_handle, FileGeneration(0)),
                FileWatch::new(second_handle, FileGeneration(0))
            ])
        };
        dispatcher
            .execute(None, request(watcher, Handle(0), watches()))
            .unwrap();
        first.poll();
        second.poll();
        assert_eq!(
            dispatcher.execute(None, request(watcher, Handle(0), watches())),
            Ok(None)
        );
        changed.borrow_mut().changed = true;
        second.poll();
        let Some(Reply::FileChange(change)) = dispatcher
            .execute(None, request(watcher, Handle(0), watches()))
            .unwrap()
        else {
            panic!("expected readiness")
        };
        assert_eq!(change.watch.handle, second_handle);
        first.poll();
        assert!(first.waiting.is_empty());
    }

    #[test]
    fn disconnect_cancels_pending_work_and_retries_failed_backend_cleanup() {
        let dispatcher = Dispatcher::new();
        let (mut worker, state) = make_worker(&dispatcher, "/", b"root");
        state.borrow_mut().blocked = true;
        dispatcher
            .execute(
                Some("/"),
                request(owner(1), Handle(0), Operation::Open(b"/file", IO_OPEN_READ)),
            )
            .unwrap();
        worker.poll();
        state.borrow_mut().cleanup_fails = true;
        assert!(dispatcher.disconnect(owner(1).session).is_err());
        worker.poll();
        assert!(dispatcher.disconnect(owner(1).session).is_err());
        state.borrow_mut().cleanup_fails = false;
        worker.poll();
        assert_eq!(dispatcher.disconnect(owner(1).session), Ok(()));
        assert!(dispatcher.0.borrow().bindings.is_empty());
        assert!(worker.waiting.is_empty());
    }

    #[test]
    fn admission_limits_and_handle_exhaustion_fail_before_backend_mutation() {
        let dispatcher = Dispatcher::new();
        let (mut worker, state) = make_worker(&dispatcher, "/", b"root");
        dispatcher.0.borrow_mut().next_handle = Some(i64::MAX);
        assert_eq!(
            open(&dispatcher, &mut worker, "/", owner(1)),
            Handle(i64::MAX)
        );
        let calls = state.borrow().calls;
        assert_eq!(
            dispatcher.execute(
                Some("/"),
                request(owner(2), Handle(0), Operation::Open(b"/file", IO_OPEN_READ))
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
        assert_eq!(state.borrow().calls, calls);
        let dispatcher = Dispatcher::new();
        let (_worker, _) = make_worker(&dispatcher, "/", b"root");
        for index in 0..MAX_REQUESTS {
            assert_eq!(
                dispatcher.execute(
                    Some("/"),
                    request(
                        owner(index as u64 + 1),
                        Handle(0),
                        Operation::FileInfo(b"/file")
                    )
                ),
                Ok(None)
            );
        }
        assert_eq!(
            dispatcher.execute(
                Some("/"),
                request(owner(100), Handle(0), Operation::FileInfo(b"/file"))
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
    }

    #[test]
    fn pipes_reserve_both_host_handle_slots_before_dispatch() {
        let dispatcher = Dispatcher::new();
        let (_worker, recorder) = make_worker(&dispatcher, "hund-rohr", b"");
        {
            let mut state = dispatcher.0.borrow_mut();
            state.bindings = (0..MAX_HANDLES - 1)
                .map(|index| {
                    Binding::new(
                        Handle(IO_SESSION_HANDLE_BASE as i64 + index as i64),
                        Handle(index as i64 + 1),
                        ComponentId(0),
                        owner(1),
                    )
                })
                .collect();
            state.next_handle = Some(IO_SESSION_HANDLE_BASE as i64 + MAX_HANDLES as i64 - 1);
        }
        assert_eq!(
            dispatcher.execute(
                Some("hund-rohr"),
                request(
                    owner(2),
                    Handle(0),
                    Operation::PipeCreate {
                        buffered: true,
                        flags: IO_OPEN_READ
                    }
                )
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
        assert_eq!(recorder.borrow().calls, 0);
        assert_eq!(dispatcher.0.borrow().bindings.len(), MAX_HANDLES - 1);
    }

    fn pipe_call<'request>(
        dispatcher: &Dispatcher,
        worker: &mut Worker<hunde_pipe::PipeService>,
        owner: Owner,
        handle: Handle,
        operation: impl Fn() -> Operation<'request>,
    ) -> Result<Reply, IoError> {
        assert_eq!(
            dispatcher.execute(Some("hund-rohr"), request(owner, handle, operation())),
            Ok(None)
        );
        assert!(worker.poll());
        dispatcher
            .execute(Some("hund-rohr"), request(owner, handle, operation()))?
            .ok_or(IoError(IO_WAIT_FOR_DATA))
    }

    fn pipe_fixture() -> (Dispatcher, Worker<hunde_pipe::PipeService>, Handle, Handle) {
        let dispatcher = Dispatcher::new();
        let mut worker = Worker::new(
            dispatcher.clone(),
            ServiceName::new("hund-rohr"),
            hunde_pipe::PipeService::default(),
        )
        .unwrap();
        let pipe = || Operation::PipeCreate {
            buffered: true,
            flags: hunde::protocol::IO_OPEN_READ | hunde::protocol::IO_OPEN_WRITE,
        };
        let Reply::Pipe { reader, writer } =
            pipe_call(&dispatcher, &mut worker, owner(1), Handle(0), pipe).unwrap()
        else {
            panic!("expected pipe")
        };
        (dispatcher, worker, reader, writer)
    }

    fn pipe_prepare(
        dispatcher: &Dispatcher,
        worker: &mut Worker<hunde_pipe::PipeService>,
        source: Owner,
        handle: Handle,
    ) -> (InheritanceToken, u64) {
        let Reply::Inheritance { token, flags } =
            pipe_call(dispatcher, worker, source, handle, || {
                Operation::InheritPrepare
            })
            .unwrap()
        else {
            panic!("expected grant")
        };
        (token, flags)
    }

    fn pipe_claim(
        dispatcher: &Dispatcher,
        worker: &mut Worker<hunde_pipe::PipeService>,
        source: Owner,
        target: Owner,
        token: InheritanceToken,
        flags: u64,
    ) -> Handle {
        pipe_call(dispatcher, worker, source, Handle(0), || {
            Operation::InheritCommit {
                token,
                target: target.pid,
            }
        })
        .unwrap();
        let Reply::Value(handle) = pipe_call(dispatcher, worker, target, Handle(0), || {
            Operation::InheritClaim { token, flags }
        })
        .unwrap() else {
            panic!("expected alias")
        };
        Handle(handle)
    }

    #[test]
    fn closing_one_claimed_pipe_alias_keeps_another_alias_usable() {
        let (dispatcher, mut worker, reader, writer) = pipe_fixture();
        let mut aliases = Vec::new();
        for _ in 0..2 {
            let (token, flags) = pipe_prepare(&dispatcher, &mut worker, owner(1), writer);
            aliases.push(pipe_claim(
                &dispatcher,
                &mut worker,
                owner(1),
                owner(2),
                token,
                flags,
            ));
        }
        assert_ne!(aliases[0], aliases[1]);
        pipe_call(&dispatcher, &mut worker, owner(2), aliases[0], || {
            Operation::Close
        })
        .unwrap();
        assert_eq!(
            dispatcher.execute(None, request(owner(2), aliases[0], Operation::Write(b"x"))),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            pipe_call(&dispatcher, &mut worker, owner(2), aliases[1], || {
                Operation::Write(b"x")
            }),
            Ok(Reply::Value(1))
        );
        pipe_call(&dispatcher, &mut worker, owner(2), aliases[1], || {
            Operation::Close
        })
        .unwrap();
        assert_eq!(
            pipe_call(&dispatcher, &mut worker, owner(1), writer, || {
                Operation::Write(b"y")
            }),
            Ok(Reply::Value(1))
        );
        pipe_call(&dispatcher, &mut worker, owner(1), writer, || {
            Operation::Close
        })
        .unwrap();
        let mut bytes = [0; 2];
        dispatcher
            .execute(None, request(owner(1), reader, Operation::Read(&mut bytes)))
            .unwrap();
        worker.poll();
        assert_eq!(
            dispatcher.execute(None, request(owner(1), reader, Operation::Read(&mut bytes))),
            Ok(Some(Reply::Value(2)))
        );
        assert_eq!(&bytes, b"xy");
        dispatcher
            .execute(None, request(owner(1), reader, Operation::Read(&mut bytes)))
            .unwrap();
        worker.poll();
        assert_eq!(
            dispatcher.execute(None, request(owner(1), reader, Operation::Read(&mut bytes))),
            Err(IoError(hunde::protocol::IO_EOF))
        );
    }

    #[test]
    fn alias_grants_survive_source_alias_close_but_not_last_owner_close() {
        let (dispatcher, mut worker, _, writer) = pipe_fixture();
        let (token, flags) = pipe_prepare(&dispatcher, &mut worker, owner(1), writer);
        let alias = pipe_claim(&dispatcher, &mut worker, owner(1), owner(1), token, flags);
        let (retained, flags) = pipe_prepare(&dispatcher, &mut worker, owner(1), writer);
        let (aborted, _) = pipe_prepare(&dispatcher, &mut worker, owner(1), writer);
        let (expired, _) = pipe_prepare(&dispatcher, &mut worker, owner(1), writer);
        pipe_call(&dispatcher, &mut worker, owner(1), writer, || {
            Operation::Close
        })
        .unwrap();
        let inherited = pipe_claim(
            &dispatcher,
            &mut worker,
            owner(1),
            owner(2),
            retained,
            flags,
        );
        pipe_call(&dispatcher, &mut worker, owner(1), Handle(0), || {
            Operation::InheritAbort(aborted)
        })
        .unwrap();
        assert!(dispatcher.0.borrow().grant(expired).is_ok());
        pipe_call(&dispatcher, &mut worker, owner(1), alias, || {
            Operation::Close
        })
        .unwrap();
        assert!(dispatcher.0.borrow().grants.is_empty());
        assert_eq!(
            dispatcher.execute(
                None,
                request(
                    owner(1),
                    Handle(0),
                    Operation::InheritCommit {
                        token: expired,
                        target: owner(3).pid
                    }
                )
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            pipe_call(&dispatcher, &mut worker, owner(2), inherited, || {
                Operation::Write(b"x")
            }),
            Ok(Reply::Value(1))
        );
    }

    #[test]
    fn committed_alias_grants_survive_last_owner_close() {
        let (dispatcher, mut worker, _, writer) = pipe_fixture();
        let (token, flags) = pipe_prepare(&dispatcher, &mut worker, owner(1), writer);
        let alias = pipe_claim(&dispatcher, &mut worker, owner(1), owner(1), token, flags);
        let (token, flags) = pipe_prepare(&dispatcher, &mut worker, owner(1), writer);
        pipe_call(&dispatcher, &mut worker, owner(1), Handle(0), || {
            Operation::InheritCommit {
                token,
                target: owner(2).pid,
            }
        })
        .unwrap();
        pipe_call(&dispatcher, &mut worker, owner(1), writer, || {
            Operation::Close
        })
        .unwrap();
        pipe_call(&dispatcher, &mut worker, owner(1), alias, || {
            Operation::Close
        })
        .unwrap();
        let Reply::Value(inherited) =
            pipe_call(&dispatcher, &mut worker, owner(2), Handle(0), || {
                Operation::InheritClaim { token, flags }
            })
            .unwrap()
        else {
            panic!("expected inherited handle")
        };
        assert!(dispatcher.0.borrow().grants.is_empty());
        assert_eq!(
            pipe_call(
                &dispatcher,
                &mut worker,
                owner(2),
                Handle(inherited),
                || Operation::Write(b"x")
            ),
            Ok(Reply::Value(1))
        );
    }
}
