use crate::host::components::{ConfiguredComponent, image_ready};
use crate::host::config::Filesystems;
use crate::host::{Endpoint, Subsystem};
use alloc::collections::VecDeque;
use alloc::rc::Rc;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::cell::RefCell;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{Operation, ProcessId, Reply, Request, SessionId};
use hunde::protocol::{IO_INVALID_MOUNT_POINT, IO_INVALID_PATH, IO_OPERATION_NOT_SUPPORTED};
use hunde::service::{ReadinessMessage, ServiceName};
use hunde::wuff::BundledFilesystem;

#[derive(Clone, Debug, PartialEq, Eq)]
struct DevicePath(String);

impl DevicePath {
    fn parse(path: &str) -> Result<Self, IoError> {
        if !path.starts_with("/dev/") || !canonical(path) {
            return Err(IoError(IO_INVALID_PATH));
        }
        Ok(Self(path.to_string()))
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum MountSource {
    Block(DevicePath, BundledFilesystem),
    Console(crate::console::Definition),
}

impl MountSource {
    fn same_device(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::Block(device, _), Self::Block(candidate, _)) => device == candidate,
            (Self::Console(first), Self::Console(second)) => first == second,
            _ => false,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct MountRequest {
    source: MountSource,
    name: ServiceName,
}

impl MountRequest {
    fn parse(filesystems: &Filesystems, bytes: &[u8]) -> Result<Self, IoError> {
        let text = core::str::from_utf8(bytes).map_err(|_| IoError(IO_INVALID_PATH))?;
        let mut fields = text.split_ascii_whitespace();
        if fields.next() != Some("device") {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        let device = fields.next().ok_or(IoError(IO_INVALID_PATH))?;
        let format = fields.next();
        let path = fields.next().ok_or(IoError(IO_INVALID_MOUNT_POINT))?;
        if fields.next().is_some() || path.len() >= 256 || (!canonical(path) && path != ":console:")
        {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        Ok(Self {
            source: match (device, format, path) {
                (_, Some("console"), _) => {
                    MountSource::Console(crate::console::Definition::parse(device, path)?)
                }
                (_, Some(format), _) if canonical(path) => MountSource::Block(
                    DevicePath::parse(device)?,
                    filesystems
                        .find(format)
                        .ok_or(IoError(IO_INVALID_MOUNT_POINT))?,
                ),
                _ => return Err(IoError(IO_INVALID_MOUNT_POINT)),
            },
            name: ServiceName::from_string(path.to_string()),
        })
    }
}

pub(crate) fn canonical(path: &str) -> bool {
    if path == "/" {
        return true;
    }
    path.starts_with('/')
        && !path.contains('\0')
        && path[1..]
            .split('/')
            .all(|part| !part.is_empty() && part != "." && part != "..")
}

#[derive(Clone)]
pub(crate) struct Job {
    session: SessionId,
    pid: ProcessId,
    request: MountRequest,
}

impl Job {
    fn new(session: SessionId, pid: ProcessId, request: MountRequest) -> Self {
        Self {
            session,
            pid,
            request,
        }
    }
}

struct Pending {
    job: Job,
    result: Option<Result<(), IoError>>,
}

impl Pending {
    fn new(job: Job) -> Self {
        Self { job, result: None }
    }
}

struct State {
    reservations: Vec<MountRequest>,
    requests: Vec<Pending>,
    jobs: VecDeque<Job>,
}

impl State {
    fn new() -> Self {
        Self {
            reservations: Vec::new(),
            requests: Vec::new(),
            jobs: VecDeque::new(),
        }
    }
}

#[derive(Clone)]
pub(crate) struct Mounts(Rc<RefCell<State>>);

impl Mounts {
    pub(crate) fn new() -> Self {
        Self(Rc::new(RefCell::new(State::new())))
    }
    pub(crate) fn take_job(&self) -> Option<Job> {
        self.0.borrow_mut().jobs.pop_front()
    }
    pub(crate) fn has_jobs(&self) -> bool {
        !self.0.borrow().jobs.is_empty()
    }

    fn request(&self, job: Job) -> Result<Option<Reply>, IoError> {
        let mut state = self.0.borrow_mut();
        if let Some(index) = state
            .requests
            .iter()
            .position(|pending| pending.job.session == job.session && pending.job.pid == job.pid)
        {
            if state.requests[index].job.request != job.request {
                return Err(IoError(IO_INVALID_MOUNT_POINT));
            }
            if let Some(result) = state.requests[index].result {
                state.requests.remove(index);
                return result.map(|()| Some(Reply::Value(0)));
            }
            return Ok(None);
        }
        if state.reservations.iter().any(|request| {
            request.name == job.request.name || request.source.same_device(&job.request.source)
        }) {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        if let MountSource::Console(crate::console::Definition::Default(target)) =
            &job.request.source
            && !state.reservations.iter().any(|request| {
                request.name.as_str() == target
                    && matches!(
                        request.source,
                        MountSource::Console(
                            crate::console::Definition::Serial | crate::console::Definition::Video
                        )
                    )
            })
        {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        state.reservations.push(job.request.clone());
        state.requests.push(Pending::new(job.clone()));
        state.jobs.push_back(job);
        Ok(None)
    }

    pub(crate) fn complete(&self, job: &Job, result: Result<(), IoError>) {
        let mut state = self.0.borrow_mut();
        if result.is_err() {
            state.reservations.retain(|request| request != &job.request);
        }
        if let Some(pending) = state
            .requests
            .iter_mut()
            .find(|pending| pending.job.session == job.session && pending.job.pid == job.pid)
        {
            pending.result = Some(result);
        }
    }

    fn disconnect(&self, session: SessionId) {
        self.0
            .borrow_mut()
            .requests
            .retain(|pending| pending.job.session != session);
    }
}

pub(crate) struct Control<Authority> {
    mounts: Mounts,
    authorize: Authority,
    filesystems: Filesystems,
}

impl<Authority> Control<Authority> {
    pub(crate) fn new(mounts: Mounts, authorize: Authority, filesystems: Filesystems) -> Self {
        Self {
            mounts,
            authorize,
            filesystems,
        }
    }
}

impl<Authority: Fn(ProcessId) -> Result<(), IoError> + 'static> IoComponent for Control<Authority> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        (self.authorize)(request.pid)?;
        match request.operation {
            Operation::Mount(config) => self.mounts.request(Job::new(
                request.session,
                request.pid,
                MountRequest::parse(&self.filesystems, config)?,
            )),
            _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
    }
    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.mounts.disconnect(session);
        Ok(())
    }
}

impl<Authority: Fn(ProcessId) -> Result<(), IoError> + 'static> Subsystem for Control<Authority> {
    fn endpoint(&self) -> ServiceName {
        ServiceName::new("hunde-io-host.mounts")
    }
    fn ready_message(&self) -> ReadinessMessage {
        ReadinessMessage::new("Rust mount control ready")
    }
}

pub(crate) fn serve(
    job: Job,
    mounts: Mounts,
    dispatcher: crate::host::dispatch::Dispatcher,
    parker: &mut hunde::fibre::SchedulerParker<'_>,
) -> Result<(), IoError> {
    let result: Result<(), IoError> = (|| {
        let (device, format) = match &job.request.source {
            MountSource::Console(definition) => {
                return crate::console::serve(
                    definition.clone(),
                    job.request.name.clone(),
                    dispatcher,
                    parker,
                    |result| mounts.complete(&job, result),
                );
            }
            MountSource::Block(device, format) => (device, *format),
        };
        let component =
            hunde::wuff::LoadedComponent::mount_filesystem(format, device.0.as_bytes())?;
        let mut filesystem = crate::host::dispatch::Hosted::new(ConfiguredComponent::new(
            job.request.name.clone(),
            image_ready(format.path()),
            component,
        ));
        let driver = filesystem.clone();
        let mut endpoint = Endpoint::listen(&mut filesystem)?;
        let mut worker = crate::host::dispatch::Worker::with_driver(
            dispatcher,
            job.request.name.clone(),
            driver,
        )?;
        if endpoint.publish_ready().is_err() {
            hunde::runtime::fatal(b"Cannot publish registered filesystem endpoint");
        }
        mounts.complete(&job, Ok(()));
        parker.notify()?;
        loop {
            hunde::fibre::block_on(
                core::future::poll_fn(|_| {
                    while endpoint.poll() {}
                    if worker.poll() {
                        return core::task::Poll::Ready(());
                    }
                    core::task::Poll::<()>::Pending
                }),
                parker,
            )?;
            parker.notify()?;
        }
    })();
    if let Err(error) = result {
        mounts.complete(&job, Err(error));
        parker.notify()?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use hunde::io::Handle;
    use hunde::protocol::IO_PERMISSION_DENIED;

    const CONFIG: &[u8] = b"device /dev/virtio7 ext2.wuff /mnt/hosted-ext2";

    #[test]
    fn mount_format_names_come_from_the_configured_registry() {
        let registry = crate::host::config::Configuration::parse(
            b"service /scratch /sbin/components/memfs.wuff\nfilesystem custom /sbin/components/ext2fs.wuff\n",
        ).unwrap().filesystems;
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize, registry);
        assert_eq!(
            call(&mut control, 1, 7, CONFIG),
            Err(IoError(IO_INVALID_MOUNT_POINT))
        );
        assert!(!mounts.has_jobs());
        assert_eq!(
            call(
                &mut control,
                1,
                7,
                b"device /dev/virtio7 custom /mnt/custom"
            ),
            Ok(None)
        );
        let job = mounts.take_job().unwrap();
        assert_eq!(
            job.request.source,
            MountSource::Block(
                DevicePath("/dev/virtio7".to_string()),
                BundledFilesystem::find("/sbin/components/ext2fs.wuff").unwrap()
            )
        );
    }

    fn filesystems() -> Filesystems {
        crate::host::config::Configuration::parse(include_bytes!(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../../config/io-host.conf"
        )))
        .unwrap()
        .filesystems
    }

    #[test]
    fn console_mounts_share_authentication_and_reservations_with_filesystem_mounts() {
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize, filesystems());
        let config = b"device uart_rx,uart_tx console /dev/tty0";
        assert_eq!(
            call(&mut control, 2, 7, b"device /dev/tty0 console :console:"),
            Err(IoError(IO_INVALID_MOUNT_POINT))
        );
        assert_eq!(
            call(&mut control, 1, 8, config),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert!(!mounts.has_jobs());
        assert_eq!(call(&mut control, 1, 7, config), Ok(None));
        let job = mounts.take_job().unwrap();
        assert_eq!(
            job.request.source,
            MountSource::Console(crate::console::Definition::Serial)
        );
        assert_eq!(
            call(
                &mut control,
                2,
                7,
                b"device uart_rx,uart_tx console /dev/tty1"
            ),
            Err(IoError(IO_INVALID_MOUNT_POINT))
        );
        mounts.complete(&job, Ok(()));
        assert_eq!(call(&mut control, 1, 7, config), Ok(Some(Reply::Value(0))));
        assert_eq!(
            call(&mut control, 2, 7, b"device /dev/tty0 console :console:"),
            Ok(None)
        );
        assert!(
            MountRequest::parse(&filesystems(), b"device /dev/virtio1 ext2.wuff :console:")
                .is_err()
        );
        assert!(
            MountRequest::parse(&filesystems(), b"device uart_rx,uart_tx console /tmp/tty0")
                .is_err()
        );
    }

    #[test]
    fn fat32_mount_retains_driver_identity_and_rejects_cross_format_device_reuse() {
        let config = b"device /dev/virtio3 fat32.wuff /mnt/fat";
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize, filesystems());
        assert_eq!(
            call(&mut control, 1, 8, config),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert!(!mounts.has_jobs());
        assert_eq!(call(&mut control, 1, 7, config), Ok(None));
        let job = mounts.take_job().unwrap();
        assert_eq!(
            job.request.source,
            MountSource::Block(
                DevicePath("/dev/virtio3".to_string()),
                filesystems().find("fat32.wuff").unwrap()
            )
        );
        assert_eq!(
            call(
                &mut control,
                2,
                7,
                b"device /dev/virtio3 ext2.wuff /mnt/other"
            ),
            Err(IoError(IO_INVALID_MOUNT_POINT))
        );
        assert_eq!(
            call(
                &mut control,
                2,
                7,
                b"device /dev/virtio4 ext2.wuff /mnt/fat"
            ),
            Err(IoError(IO_INVALID_MOUNT_POINT))
        );
        assert_eq!(
            call(
                &mut control,
                1,
                7,
                b"device /dev/virtio3 ext2.wuff /mnt/fat"
            ),
            Err(IoError(IO_INVALID_MOUNT_POINT))
        );
        mounts.complete(&job, Err(IoError(IO_INVALID_PATH)));
        assert_eq!(
            call(&mut control, 1, 7, config),
            Err(IoError(IO_INVALID_PATH))
        );
        assert_eq!(
            call(
                &mut control,
                2,
                7,
                b"device /dev/virtio3 ext2.wuff /mnt/other"
            ),
            Ok(None)
        );
    }

    #[test]
    fn completed_fat32_mount_stays_reserved_after_control_disconnect() {
        let config = b"device /dev/mmc0p1 fat32.wuff /mnt/sd";
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize, filesystems());
        assert_eq!(call(&mut control, 1, 7, config), Ok(None));
        let job = mounts.take_job().unwrap();
        mounts.complete(&job, Ok(()));
        assert_eq!(call(&mut control, 1, 7, config), Ok(Some(Reply::Value(0))));
        control.disconnect(SessionId(1)).unwrap();
        assert_eq!(
            call(
                &mut control,
                2,
                7,
                b"device /dev/mmc0p1 ext2.wuff /mnt/other"
            ),
            Err(IoError(IO_INVALID_MOUNT_POINT))
        );
    }

    fn authorize(pid: ProcessId) -> Result<(), IoError> {
        if pid == ProcessId(7) {
            Ok(())
        } else {
            Err(IoError(IO_PERMISSION_DENIED))
        }
    }

    fn call(
        control: &mut impl IoComponent,
        session: u64,
        pid: u64,
        config: &[u8],
    ) -> Result<Option<Reply>, IoError> {
        control.execute(Request::new(
            SessionId(session),
            ProcessId(pid),
            Handle(0),
            Operation::Mount(config),
        ))
    }

    #[test]
    fn mount_requires_authority_and_owns_request_text_until_worker_completes() {
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize, filesystems());
        assert_eq!(
            call(&mut control, 1, 8, CONFIG),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert!(!mounts.has_jobs());
        assert_eq!(call(&mut control, 1, 7, CONFIG), Ok(None));
        assert_eq!(call(&mut control, 1, 7, CONFIG), Ok(None));
        let job = mounts.take_job().unwrap();
        assert_eq!(
            job.request.source,
            MountSource::Block(
                DevicePath("/dev/virtio7".to_string()),
                filesystems().find("ext2.wuff").unwrap()
            )
        );
        assert_eq!(job.request.name.as_str(), "/mnt/hosted-ext2");
        assert!(!mounts.has_jobs());
        mounts.complete(&job, Ok(()));
        assert_eq!(call(&mut control, 1, 7, CONFIG), Ok(Some(Reply::Value(0))));
        assert!(call(&mut control, 2, 7, CONFIG).is_err());
    }

    #[test]
    fn duplicate_device_or_endpoint_is_rejected_and_failed_mount_can_retry() {
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize, filesystems());
        assert_eq!(call(&mut control, 1, 7, CONFIG), Ok(None));
        assert!(
            call(
                &mut control,
                2,
                7,
                b"device /dev/virtio7 ext2.wuff /mnt/other"
            )
            .is_err()
        );
        assert!(
            call(
                &mut control,
                2,
                7,
                b"device /dev/virtio8 ext2.wuff /mnt/hosted-ext2"
            )
            .is_err()
        );
        let job = mounts.take_job().unwrap();
        mounts.complete(&job, Err(IoError(IO_INVALID_PATH)));
        assert_eq!(
            call(&mut control, 1, 7, CONFIG),
            Err(IoError(IO_INVALID_PATH))
        );
        assert_eq!(call(&mut control, 2, 7, CONFIG), Ok(None));
    }

    #[test]
    fn client_disconnect_does_not_cancel_or_release_a_configured_mount() {
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize, filesystems());
        assert_eq!(call(&mut control, 1, 7, CONFIG), Ok(None));
        control.disconnect(SessionId(1)).unwrap();
        let job = mounts.take_job().unwrap();
        mounts.complete(&job, Ok(()));
        assert!(call(&mut control, 2, 7, CONFIG).is_err());
        assert!(mounts.0.borrow().requests.is_empty());
    }

    #[test]
    fn noncanonical_paths_and_unsupported_drivers_are_rejected() {
        for config in [
            "device /dev/virtio7 ext2.wuff /mnt/../root",
            "device /dev/virtio7 ext2.wuff /mnt//disk",
            "device /dev/virtio7 ext2 /mnt/disk",
            "device /dev/virtio7 ext2.wuff /mnt/disk extra",
            "device /tmp/file ext2.wuff /mnt/disk",
            "device /dev/virtio7 ext2.wuff /mnt/disk/",
            "device /dev/../root ext2.wuff /mnt/disk",
        ] {
            assert!(
                MountRequest::parse(&filesystems(), config.as_bytes()).is_err(),
                "{config}"
            );
        }
        assert!(
            MountRequest::parse(&filesystems(), b"device /dev/virtio7 ext2.wuff /mnt/\0disk")
                .is_err()
        );
    }

    #[test]
    fn root_requires_authority_and_completion_before_acknowledgement() {
        let config = b"device /dev/virtio1 ext2.wuff /";
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize, filesystems());
        assert_eq!(
            call(&mut control, 1, 8, config),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert!(!mounts.has_jobs());
        assert_eq!(call(&mut control, 1, 7, config), Ok(None));
        let job = mounts.take_job().unwrap();
        assert_eq!(job.request.name.as_str(), "/");
        assert!(call(&mut control, 2, 7, b"device /dev/virtio2 ext2.wuff /").is_err());
        mounts.complete(&job, Err(IoError(IO_INVALID_PATH)));
        assert_eq!(
            call(&mut control, 1, 7, config),
            Err(IoError(IO_INVALID_PATH))
        );
        assert_eq!(call(&mut control, 2, 7, config), Ok(None));
        let job = mounts.take_job().unwrap();
        mounts.complete(&job, Ok(()));
        assert_eq!(call(&mut control, 2, 7, config), Ok(Some(Reply::Value(0))));
        assert!(
            call(
                &mut control,
                3,
                7,
                b"device /dev/virtio1 ext2.wuff /mnt/root"
            )
            .is_err()
        );
    }

    #[test]
    fn boot_configuration_can_mount_before_root_without_being_released_on_disconnect() {
        for device in ["/dev/virtio2", "/dev/mmc0p2"] {
            let config = alloc::format!("device {device} ext2.wuff /boot/config");
            let mounts = Mounts::new();
            let mut control = Control::new(mounts.clone(), authorize, filesystems());
            assert_eq!(call(&mut control, 1, 7, config.as_bytes()), Ok(None));
            let job = mounts.take_job().unwrap();
            assert_eq!(job.request.name.as_str(), "/boot/config");
            assert_eq!(
                job.request.source,
                MountSource::Block(
                    DevicePath(device.to_string()),
                    filesystems().find("ext2.wuff").unwrap()
                )
            );
            assert_eq!(call(&mut control, 1, 7, config.as_bytes()), Ok(None));
            mounts.complete(&job, Ok(()));
            assert_eq!(
                call(&mut control, 1, 7, config.as_bytes()),
                Ok(Some(Reply::Value(0)))
            );
            assert_eq!(control.disconnect(SessionId(1)), Ok(()));
            assert_eq!(
                call(&mut control, 2, 7, b"device /dev/virtio1 ext2.wuff /"),
                Ok(None)
            );
            let root = mounts.take_job().unwrap();
            mounts.complete(&root, Ok(()));
            assert_eq!(
                call(&mut control, 2, 7, b"device /dev/virtio1 ext2.wuff /"),
                Ok(Some(Reply::Value(0)))
            );
            assert!(call(&mut control, 3, 7, config.as_bytes()).is_err());
        }
    }
}
