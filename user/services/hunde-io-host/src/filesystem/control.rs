use super::Filesystem;
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
    EmbeddedArchive,
}

impl MountSource {
    fn same_device(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::Block(device, _), Self::Block(candidate, _)) => device == candidate,
            (Self::EmbeddedArchive, Self::EmbeddedArchive) => true,
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
    fn parse(bytes: &[u8]) -> Result<Self, IoError> {
        let text = core::str::from_utf8(bytes).map_err(|_| IoError(IO_INVALID_PATH))?;
        let mut fields = text.split_ascii_whitespace();
        if fields.next() != Some("device") {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        let device = fields.next().ok_or(IoError(IO_INVALID_PATH))?;
        let format = fields.next();
        let path = fields.next().ok_or(IoError(IO_INVALID_MOUNT_POINT))?;
        if fields.next().is_some() || path.len() >= 256 || !canonical(path) {
            return Err(IoError(IO_INVALID_MOUNT_POINT));
        }
        Ok(Self {
            source: match (device, format, path) {
                ("-", Some("embedfs"), "/sbin") => MountSource::EmbeddedArchive,
                (_, Some("ext2.wuff"), _) => {
                    MountSource::Block(DevicePath::parse(device)?, BundledFilesystem::Ext2)
                }
                (_, Some("fat32.wuff"), _) => {
                    MountSource::Block(DevicePath::parse(device)?, BundledFilesystem::Fat32)
                }
                _ => return Err(IoError(IO_INVALID_MOUNT_POINT)),
            },
            name: ServiceName::from_string(path.to_string()),
        })
    }
}

pub(super) fn canonical(path: &str) -> bool {
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
}

impl<Authority> Control<Authority> {
    pub(crate) fn new(mounts: Mounts, authorize: Authority) -> Self {
        Self { mounts, authorize }
    }
}

impl<Authority: Fn(ProcessId) -> Result<(), IoError> + 'static> IoComponent for Control<Authority> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        (self.authorize)(request.pid)?;
        match request.operation {
            Operation::Mount(config) => self.mounts.request(Job::new(
                request.session,
                request.pid,
                MountRequest::parse(config)?,
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
            MountSource::Block(device, format) => (device, *format),
            MountSource::EmbeddedArchive => {
                let component = hunde::embedfs::Filesystem::new()?;
                let mut worker = crate::host::dispatch::Worker::new(
                    dispatcher,
                    job.request.name.clone(),
                    component,
                )?;
                mounts.complete(&job, Ok(()));
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
        };
        let component =
            hunde::wuff::LoadedComponent::mount_filesystem(format, device.0.as_bytes())?;
        let mut filesystem = crate::host::dispatch::Hosted::new(Filesystem::mounted(
            job.request.name.clone(),
            component,
            format,
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
    fn fat32_mount_retains_driver_identity_and_rejects_cross_format_device_reuse() {
        let config = b"device /dev/virtio3 fat32.wuff /mnt/fat";
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize);
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
                BundledFilesystem::Fat32
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
        let mut control = Control::new(mounts.clone(), authorize);
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

    #[test]
    fn bootstrap_archive_requires_authority_and_a_completed_internal_worker() {
        let config = b"device - embedfs /sbin";
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize);
        assert_eq!(
            call(&mut control, 1, 8, config),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert!(!mounts.has_jobs());
        assert_eq!(call(&mut control, 1, 7, config), Ok(None));
        let job = mounts.take_job().unwrap();
        assert_eq!(job.request.source, MountSource::EmbeddedArchive);
        assert_eq!(job.request.name.as_str(), "/sbin");
        assert_eq!(call(&mut control, 1, 7, config), Ok(None));
        mounts.complete(&job, Ok(()));
        assert_eq!(call(&mut control, 1, 7, config), Ok(Some(Reply::Value(0))));
        control.disconnect(SessionId(1)).unwrap();
        assert!(call(&mut control, 2, 7, config).is_err());
        for invalid in [
            b"device - embedfs /other".as_slice(),
            b"device /dev/disk embedfs /sbin",
        ] {
            assert!(MountRequest::parse(invalid).is_err());
        }
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
        let mut control = Control::new(mounts.clone(), authorize);
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
                BundledFilesystem::Ext2
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
        let mut control = Control::new(mounts.clone(), authorize);
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
        let mut control = Control::new(mounts.clone(), authorize);
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
            assert!(MountRequest::parse(config.as_bytes()).is_err(), "{config}");
        }
        assert!(MountRequest::parse(b"device /dev/virtio7 ext2.wuff /mnt/\0disk").is_err());
    }

    #[test]
    fn root_requires_authority_and_completion_before_acknowledgement() {
        let config = b"device /dev/virtio1 ext2.wuff /";
        let mounts = Mounts::new();
        let mut control = Control::new(mounts.clone(), authorize);
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
            let mut control = Control::new(mounts.clone(), authorize);
            assert_eq!(call(&mut control, 1, 7, config.as_bytes()), Ok(None));
            let job = mounts.take_job().unwrap();
            assert_eq!(job.request.name.as_str(), "/boot/config");
            assert_eq!(
                job.request.source,
                MountSource::Block(DevicePath(device.to_string()), BundledFilesystem::Ext2)
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
