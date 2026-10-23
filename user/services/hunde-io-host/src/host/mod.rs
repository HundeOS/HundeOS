use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::service::{ReadinessMessage, Service, ServiceName};

use crate::filesystem::{Filesystem, FilesystemKind};
use crate::socket::Socket;
use crate::streams::Streams;

pub(crate) mod dispatch;
mod router;

pub(crate) trait Subsystem: IoComponent {
    fn endpoint(&self) -> ServiceName;
    fn ready_message(&self) -> ReadinessMessage;
}

pub(crate) struct Endpoint<'subsystem> {
    service: Service<'subsystem>,
    ready_message: ReadinessMessage,
}

impl<'subsystem> Endpoint<'subsystem> {
    pub(crate) fn listen(subsystem: &'subsystem mut impl Subsystem) -> Result<Self, IoError> {
        let ready_message = subsystem.ready_message();
        let service = Service::listen(subsystem.endpoint(), subsystem)?;
        Ok(Self {
            service,
            ready_message,
        })
    }

    pub(crate) fn publish_ready(&self) -> Result<(), IoError> {
        self.service.publish_ready(self.ready_message)
    }

    fn serve_handles(&mut self) {
        self.service.serve_handles();
    }

    pub(crate) fn poll(&mut self) -> bool {
        self.service.poll()
    }
}

pub(crate) fn run() -> Result<(), IoError> {
    hunde::service::become_service()?;
    let console = hunde::service::console_output()?;
    let scheduler = hunde::fibre::Scheduler::new()?;
    scheduler.run(|parker| {
        let dispatcher = dispatch::Dispatcher::new();
        let root_filesystem = crate::filesystem::namespace::RootFilesystem::new();
        let local_namespace = crate::filesystem::namespace::Namespace::new(
            root_filesystem.clone(),
            hunde::service::authorize_mount,
        );
        let local_router =
            router::Router::with_dispatcher(local_namespace, console, dispatcher.clone());
        let _local_router =
            hunde::service::LocalPathRouter::register(local_router.endpoint(), local_router)?;
        let filesystem = crate::filesystem::namespace::Namespace::new(
            root_filesystem.clone(),
            hunde::service::authorize_mount,
        );
        let mut router = router::Router::with_dispatcher(filesystem, console, dispatcher.clone());
        let mut namespace = crate::filesystem::namespace::Namespace::new(
            root_filesystem.clone(),
            hunde::service::authorize_mount,
        );
        let mut endpoints = [
            Endpoint::listen(&mut namespace)?,
            Endpoint::listen(&mut router)?,
        ];
        endpoints[1].serve_handles();
        for endpoint in &endpoints {
            endpoint.publish_ready()?;
        }
        let mut components_started = false;
        loop {
            let future = core::future::poll_fn(|_| {
                let events = match poll_service_events(&dispatcher, hunde::service::poll_event) {
                    Ok(events) => events,
                    Err(error) => return core::task::Poll::Ready(Err(error)),
                };
                while poll_all(&mut endpoints, Endpoint::poll) {}
                if events
                    || (!components_started && root_filesystem.bootstrap_ready())
                    || dispatcher.take_notification()
                {
                    core::task::Poll::Ready(Ok(()))
                } else {
                    core::task::Poll::Pending
                }
            });
            hunde::fibre::block_on(future, parker)??;
            if !components_started && root_filesystem.bootstrap_ready() {
                let components = dispatcher.clone();
                drop(parker.spawn(move |parker| {
                    if run_components(components, parker).is_err() {
                        hunde::runtime::fatal(b"Rust I/O component startup failed");
                    }
                    Ok(())
                })?);
                components_started = true;
            }
            parker.notify()?;
        }
    })
}

fn poll_service_events(
    dispatcher: &dispatch::Dispatcher,
    mut poll: impl FnMut() -> Result<Option<hunde::service::ServiceEvent>, IoError>,
) -> Result<bool, IoError> {
    let mut progress = false;
    while let Some(event) = poll()? {
        if let hunde::service::ServiceEvent::MemoryPressure(pressure) = event {
            dispatcher.memory_pressure(pressure);
        }
        progress = true;
    }
    Ok(progress)
}

fn run_components(
    dispatcher: dispatch::Dispatcher,
    parker: &mut hunde::fibre::SchedulerParker<'_>,
) -> Result<(), IoError> {
    let socket = Socket::load()?;
    let mut socket_worker = dispatch::Worker::new(dispatcher.clone(), socket.endpoint(), socket)?;
    let streams = Streams::load()?;
    let mut streams_worker =
        dispatch::Worker::new(dispatcher.clone(), streams.endpoint(), streams)?;
    let temporary = Filesystem::load(FilesystemKind::Temporary)?;
    let mut temporary_worker =
        dispatch::Worker::new(dispatcher.clone(), temporary.endpoint(), temporary)?;
    let processes = Filesystem::load(FilesystemKind::Processes)?;
    let mut processes_worker =
        dispatch::Worker::new(dispatcher.clone(), processes.endpoint(), processes)?;
    let boot = Filesystem::load(FilesystemKind::Boot)?;
    let mut boot_worker = dispatch::Worker::new(dispatcher.clone(), boot.endpoint(), boot)?;
    let mut socket = socket_worker.driver();
    let mut streams = streams_worker.driver();
    let mut temporary = temporary_worker.driver();
    let mut processes = processes_worker.driver();
    let mut boot = boot_worker.driver();
    let mounts = crate::filesystem::control::Mounts::new();
    let mut control =
        crate::filesystem::control::Control::new(mounts.clone(), hunde::service::authorize_mount);
    let mut endpoints = [
        Endpoint::listen(&mut socket)?,
        Endpoint::listen(&mut streams)?,
        Endpoint::listen(&mut temporary)?,
        Endpoint::listen(&mut processes)?,
        Endpoint::listen(&mut boot)?,
        Endpoint::listen(&mut control)?,
    ];
    for endpoint in &endpoints {
        endpoint.publish_ready()?;
    }
    loop {
        while let Some(job) = mounts.take_job() {
            let worker_mounts = mounts.clone();
            let failed_job = job.clone();
            let worker_dispatcher = dispatcher.clone();
            match parker.spawn(move |parker| {
                crate::filesystem::control::serve(job, worker_mounts, worker_dispatcher, parker)
            }) {
                Ok(handle) => drop(handle),
                Err(error) => mounts.complete(&failed_job, Err(error)),
            }
        }
        let future = core::future::poll_fn(|_| {
            while poll_all(&mut endpoints, Endpoint::poll) {}
            if socket_worker.poll()
                | streams_worker.poll()
                | temporary_worker.poll()
                | processes_worker.poll()
                | boot_worker.poll()
            {
                return core::task::Poll::Ready(());
            }
            if mounts.has_jobs() {
                core::task::Poll::Ready(())
            } else {
                core::task::Poll::Pending
            }
        });
        hunde::fibre::block_on(future, parker)?;
        parker.notify()?;
    }
}

fn poll_all<Endpoint>(
    endpoints: &mut [Endpoint],
    mut poll: impl FnMut(&mut Endpoint) -> bool,
) -> bool {
    endpoints
        .iter_mut()
        .fold(false, |progress, endpoint| poll(endpoint) | progress)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn service_events_are_drained_including_normal_pressure_and_unhandled_events() {
        use hunde::service::{MemoryPressure, ServiceEvent};
        let dispatcher = dispatch::Dispatcher::new();
        let mut events = [
            ServiceEvent::MemoryPressure(MemoryPressure::Normal),
            ServiceEvent::Other,
        ]
        .into_iter();
        assert_eq!(
            poll_service_events(&dispatcher, || Ok(events.next())),
            Ok(true)
        );
        assert_eq!(
            poll_service_events(&dispatcher, || Ok(events.next())),
            Ok(false)
        );
        assert!(!dispatcher.take_notification());
        assert_eq!(
            poll_service_events(&dispatcher, || Err(IoError(-17))),
            Err(IoError(-17))
        );
    }

    #[test]
    fn pressure_events_notify_component_workers_without_running_them_inline() {
        use hunde::service::{MemoryPressure, ServiceEvent};
        let dispatcher = dispatch::Dispatcher::new();
        let mut events = [ServiceEvent::MemoryPressure(MemoryPressure::Low)].into_iter();
        assert_eq!(
            poll_service_events(&dispatcher, || Ok(events.next())),
            Ok(true)
        );
        assert!(dispatcher.take_notification());
        assert!(!dispatcher.take_notification());
    }
    use hunde::io::{Handle, Operation, ProcessId, Reply, Request, SessionId};
    use std::cell::RefCell;
    use std::rc::Rc;

    struct State {
        identity: Option<(SessionId, ProcessId, Handle, u64)>,
        disconnected: Option<SessionId>,
        reply: Result<Option<Reply>, IoError>,
        drops: usize,
    }

    impl State {
        fn new() -> Self {
            Self {
                identity: None,
                disconnected: None,
                reply: Ok(Some(Reply::Value(1))),
                drops: 0,
            }
        }
    }

    struct Component(Rc<RefCell<State>>);

    impl Component {
        fn new(state: Rc<RefCell<State>>) -> Self {
            Self(state)
        }
    }

    impl IoComponent for Component {
        fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
            let mut state = self.0.borrow_mut();
            state.identity = Some((
                request.session,
                request.pid,
                request.handle,
                request.transfer_flags,
            ));
            if let Operation::Read(buffer) = request.operation {
                buffer.fill(42);
            }
            state.reply.clone()
        }
        fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
            self.0.borrow_mut().disconnected = Some(session);
            Ok(())
        }
    }

    impl Drop for Component {
        fn drop(&mut self) {
            self.0.borrow_mut().drops += 1;
        }
    }

    fn exercise(subsystem: &mut dyn Subsystem, state: &Rc<RefCell<State>>) {
        let mut buffer = [0; 3];
        let request = Request::new(
            SessionId(7),
            ProcessId(11),
            Handle(13),
            Operation::Read(&mut buffer),
        )
        .with_transfer_flags(hunde::protocol::IO_TRANSFER_NONBLOCK);
        assert_eq!(subsystem.execute(request), Ok(Some(Reply::Value(1))));
        assert_eq!(buffer, [42; 3]);
        assert_eq!(
            state.borrow().identity,
            Some((
                SessionId(7),
                ProcessId(11),
                Handle(13),
                hunde::protocol::IO_TRANSFER_NONBLOCK
            ))
        );
        state.borrow_mut().reply = Ok(None);
        assert_eq!(
            subsystem.execute(Request::new(
                SessionId(7),
                ProcessId(11),
                Handle(13),
                Operation::Accept
            )),
            Ok(None)
        );
        state.borrow_mut().reply = Err(IoError(hunde::protocol::IO_PERMISSION_DENIED));
        assert_eq!(
            subsystem.execute(Request::new(
                SessionId(7),
                ProcessId(11),
                Handle(13),
                Operation::Close
            )),
            Err(IoError(hunde::protocol::IO_PERMISSION_DENIED))
        );
        assert_eq!(subsystem.disconnect(SessionId(7)), Ok(()));
        assert_eq!(state.borrow().disconnected, Some(SessionId(7)));
        assert_eq!(state.borrow().drops, 0);
    }

    #[test]
    fn socket_uses_shared_dispatch_contract_and_owns_its_component() {
        let state = Rc::new(RefCell::new(State::new()));
        let mut socket = Socket::new(Component::new(state.clone()));
        assert_eq!(socket.endpoint(), ServiceName::new("socket_router"));
        assert_eq!(
            socket.ready_message(),
            ReadinessMessage::new("Rust socket.wuff ready")
        );
        exercise(&mut socket, &state);
        drop(socket);
        assert_eq!(state.borrow().drops, 1);
    }

    #[test]
    fn streams_preserve_pipe_endpoint_and_dispatch_contract() {
        let state = Rc::new(RefCell::new(State::new()));
        let mut streams = Streams::new(Component::new(state.clone()));
        assert_eq!(streams.endpoint(), ServiceName::new("hund-rohr"));
        assert_eq!(
            streams.ready_message(),
            ReadinessMessage::new("Rust pipe.wuff ready")
        );
        exercise(&mut streams, &state);
        drop(streams);
        assert_eq!(state.borrow().drops, 1);
    }

    #[test]
    fn filesystem_instances_keep_dispatch_and_cleanup_independent() {
        let temporary_state = Rc::new(RefCell::new(State::new()));
        let processes_state = Rc::new(RefCell::new(State::new()));
        let mut temporary = Filesystem::new(
            FilesystemKind::Temporary,
            Component::new(temporary_state.clone()),
        );
        let mut processes = Filesystem::new(
            FilesystemKind::Processes,
            Component::new(processes_state.clone()),
        );
        assert_eq!(temporary.endpoint(), ServiceName::new("/tmp"));
        assert_eq!(processes.endpoint(), ServiceName::new("/proc"));
        assert_eq!(
            temporary.ready_message(),
            ReadinessMessage::new("Rust memfs.wuff ready")
        );
        assert_eq!(
            processes.ready_message(),
            ReadinessMessage::new("Rust procfs.wuff ready")
        );
        exercise(&mut temporary, &temporary_state);
        assert_eq!(processes_state.borrow().identity, None);
        exercise(&mut processes, &processes_state);
        drop(temporary);
        assert_eq!(temporary_state.borrow().drops, 1);
        assert_eq!(processes_state.borrow().drops, 0);
        drop(processes);
        assert_eq!(processes_state.borrow().drops, 1);
    }

    #[test]
    fn polling_visits_every_endpoint_even_after_progress() {
        let mut polls = [0; 4];
        assert!(poll_all(&mut polls, |count| {
            *count += 1;
            true
        }));
        assert_eq!(polls, [1; 4]);
        assert!(!poll_all(&mut polls, |count| {
            *count += 1;
            false
        }));
        assert_eq!(polls, [2; 4]);
    }
}
