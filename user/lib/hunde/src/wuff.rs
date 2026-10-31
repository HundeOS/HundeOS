use crate::abi::{self, ByteSpan, Component, HostOperations, IoError, LoadResult, RawRequest};
use crate::native::Storage;
use crate::protocol::IO_PANIC;
use alloc::boxed::Box;
use core::ffi::c_void;
use core::mem::ManuallyDrop;

struct Module {
    storage: Storage,
}

impl Module {
    fn load(path: &[u8]) -> Result<Self, IoError> {
        let storage = Storage::new(unsafe { abi::host::hunde_rust_module_layout() })?;
        unsafe { abi::host::hunde_rust_module_load(storage.as_ptr(), ByteSpan::new(path)) }
            .into_result()?;
        Ok(Self { storage })
    }

    fn export(&self, name: &[u8]) -> Result<*mut c_void, IoError> {
        let result = unsafe {
            abi::host::hunde_rust_module_export(self.storage.as_ptr(), ByteSpan::new(name))
        };
        match result.kind {
            abi::SUCCESS if !result.address.is_null() => Ok(result.address),
            abi::ERROR => Err(IoError(result.error.code)),
            _ => Err(IoError(IO_PANIC)),
        }
    }
}

impl Drop for Module {
    fn drop(&mut self) {
        if unsafe { abi::host::hunde_rust_module_dispose(self.storage.as_ptr()) }
            .into_result()
            .is_err()
        {
            crate::runtime::fatal(b"Cannot release Wuff mappings");
        }
    }
}

pub(crate) struct Dispatch {
    descriptor: Component,
}

impl crate::component::IoComponent for Dispatch {
    fn execute(
        &mut self,
        request: crate::io::Request<'_>,
    ) -> Result<Option<crate::io::Reply>, IoError> {
        self.dispatch(request)
    }

    fn disconnect(&mut self, session: crate::io::SessionId) -> Result<(), IoError> {
        unsafe { self.descriptor.disconnect.unwrap()(self.descriptor.context, session.0) }
            .into_result()
    }
}

impl Dispatch {
    fn dispatch(
        &mut self,
        request: crate::io::Request<'_>,
    ) -> Result<Option<crate::io::Reply>, IoError> {
        RawRequest::with_request(request, |raw| {
            unsafe { self.descriptor.execute.unwrap()(self.descriptor.context, raw) }
                .into_result(raw.operation)
        })
    }

    fn new(result: LoadResult) -> Result<Self, IoError> {
        Ok(Self {
            descriptor: result.into_result()?,
        })
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ImageKind {
    Service,
    Filesystem,
    Driver,
}

include!(concat!(env!("OUT_DIR"), "/io_components.rs"));

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BundledComponent(&'static str);

impl BundledComponent {
    pub fn find(path: &str) -> Result<Self, IoError> {
        approved_image(path, ImageKind::Service).map(Self)
    }

    pub fn path(self) -> &'static str {
        self.0
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BundledFilesystem(&'static str);

impl BundledFilesystem {
    pub fn find(path: &str) -> Result<Self, IoError> {
        approved_image(path, ImageKind::Filesystem).map(Self)
    }

    pub fn path(self) -> &'static str {
        self.0
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BundledDriver(&'static str);

impl BundledDriver {
    pub fn find(path: &str) -> Result<Self, IoError> {
        approved_image(path, ImageKind::Driver).map(Self)
    }
}

#[repr(C)]
pub struct DriverStepResult {
    status: crate::abi::StatusResult,
    flags: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DriverDescriptor {
    header: crate::abi::Header,
    context: *mut c_void,
    irq: u64,
    step: Option<unsafe extern "C" fn(*mut c_void) -> DriverStepResult>,
    deliver: Option<unsafe extern "C" fn(*mut c_void)>,
}

#[repr(C)]
pub struct DriverLoadResult {
    kind: u32,
    reserved: u32,
    descriptor: DriverDescriptor,
    error: crate::abi::AbiError,
}

impl DriverLoadResult {
    fn success(driver: DriverDescriptor) -> Self {
        Self {
            kind: abi::SUCCESS,
            reserved: 0,
            descriptor: driver,
            error: abi::AbiError::new(0, b""),
        }
    }

    fn error(error: IoError) -> Self {
        Self {
            kind: abi::ERROR,
            reserved: 0,
            descriptor: DriverDescriptor {
                header: abi::Header::of::<DriverDescriptor>(),
                context: core::ptr::null_mut(),
                irq: 0,
                step: None,
                deliver: None,
            },
            error: abi::AbiError::new(error.0, b"Driver initialization failed"),
        }
    }

    fn into_descriptor(self) -> Result<DriverDescriptor, IoError> {
        if self.kind != abi::SUCCESS {
            return Err(IoError(if self.kind == abi::ERROR {
                self.error.code
            } else {
                IO_PANIC
            }));
        }
        if !self.descriptor.header.accepts::<DriverDescriptor>()
            || self.descriptor.context.is_null()
            || self.descriptor.step.is_none()
            || self.descriptor.deliver.is_none()
        {
            return Err(IoError(IO_PANIC));
        }
        Ok(self.descriptor)
    }
}

pub struct DriverProgress {
    pub worked: bool,
    pub rearm: bool,
}

impl DriverProgress {
    pub const fn new(worked: bool, rearm: bool) -> Self {
        Self { worked, rearm }
    }
}

pub trait DriverComponent: Sized + 'static {
    fn load(configuration: &[u8]) -> Result<Self, IoError>;
    fn irq(&self) -> Option<u64>;
    fn step(&mut self) -> Result<DriverProgress, IoError>;
    fn deliver(&mut self);
}

unsafe extern "C" fn driver_step<Driver: DriverComponent>(
    context: *mut c_void,
) -> DriverStepResult {
    let result = unsafe { context.cast::<Driver>().as_mut() }
        .ok_or(IoError(crate::protocol::IO_PERMISSION_DENIED))
        .and_then(Driver::step);
    match result {
        Ok(progress) => DriverStepResult {
            status: crate::abi::StatusResult::success(),
            flags: u64::from(progress.worked) | (u64::from(progress.rearm) << 1),
        },
        Err(error) => DriverStepResult {
            status: crate::abi::StatusResult::from_result(Err(error)),
            flags: 0,
        },
    }
}

unsafe extern "C" fn driver_deliver<Driver: DriverComponent>(context: *mut c_void) {
    if let Some(driver) = unsafe { context.cast::<Driver>().as_mut() } {
        driver.deliver();
    }
}

/// Initializes a driver image with host-owned allocator callbacks.
///
/// # Safety
/// The host and configuration must remain valid for this call. The host must
/// serialize callbacks and retain the image while the returned driver is live.
pub unsafe fn load_driver<Driver: DriverComponent>(
    host: *const HostOperations,
    configuration: ByteSpan,
) -> DriverLoadResult {
    if let Err(error) = unsafe { crate::runtime::install_host(host) } {
        return DriverLoadResult::error(error);
    }
    if configuration.bytes.is_null() || configuration.length > usize::MAX as u64 {
        return DriverLoadResult::error(IoError(crate::protocol::SERVICE_INVALID_BUFFER));
    }
    let bytes =
        unsafe { core::slice::from_raw_parts(configuration.bytes, configuration.length as usize) };
    match Driver::load(bytes) {
        Ok(driver) if driver.irq() != Some(0) => {
            let irq = driver.irq().unwrap_or_default();
            DriverLoadResult::success(DriverDescriptor {
                header: abi::Header::of::<DriverDescriptor>(),
                context: Box::into_raw(Box::new(driver)).cast(),
                irq,
                step: Some(driver_step::<Driver>),
                deliver: Some(driver_deliver::<Driver>),
            })
        }
        Ok(_) => DriverLoadResult::error(IoError(crate::protocol::SERVICE_INVALID_BUFFER)),
        Err(error) => DriverLoadResult::error(error),
    }
}

#[macro_export]
macro_rules! driver_component {
    ($driver:ty, $message:expr) => {
        $crate::host_runtime!($message);

        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn hunde_driver_component_entry_v1(
            host: *const $crate::abi::HostOperations,
            configuration: $crate::abi::ByteSpan,
        ) -> $crate::wuff::DriverLoadResult {
            unsafe { $crate::wuff::load_driver::<$driver>(host, configuration) }
        }
    };
}

pub struct LoadedDriver {
    descriptor: DriverDescriptor,
    _module: ManuallyDrop<Module>,
}

impl LoadedDriver {
    pub fn load(image: BundledDriver, configuration: &[u8]) -> Result<Self, IoError> {
        let module = ManuallyDrop::new(Module::load(image.0.as_bytes())?);
        let symbol = module.export(b"hunde_driver_component_entry_v1")?;
        let entry: unsafe extern "C" fn(*const HostOperations, ByteSpan) -> DriverLoadResult =
            unsafe { core::mem::transmute(symbol) };
        let host = crate::runtime::operations()?;
        let result = unsafe { entry(host, ByteSpan::new(configuration)) };
        Ok(Self {
            descriptor: result.into_descriptor()?,
            _module: module,
        })
    }

    pub fn irq(&self) -> Option<u64> {
        (self.descriptor.irq != 0).then_some(self.descriptor.irq)
    }

    pub fn deliver(&mut self) {
        unsafe { self.descriptor.deliver.unwrap()(self.descriptor.context) };
    }

    pub fn step(&mut self) -> Result<DriverProgress, IoError> {
        let result = unsafe { self.descriptor.step.unwrap()(self.descriptor.context) };
        result.status.into_result()?;
        if result.flags & !3 != 0 {
            return Err(IoError(IO_PANIC));
        }
        if self.irq().is_none() && result.flags & 2 != 0 {
            return Err(IoError(IO_PANIC));
        }
        Ok(DriverProgress {
            worked: result.flags & 1 != 0,
            rearm: result.flags & 2 != 0,
        })
    }
}

fn approved_image(path: &str, kind: ImageKind) -> Result<&'static str, IoError> {
    APPROVED_IMAGES
        .iter()
        .find(|(approved, approved_kind)| *approved == path && *approved_kind == kind)
        .map(|(path, _)| *path)
        .ok_or(IoError(crate::protocol::IO_PERMISSION_DENIED))
}

pub struct LoadedComponent {
    pub(crate) dispatch: Dispatch,
    _module: Module,
    block: Option<crate::block_host::BlockHost<crate::block::Client>>,
}

impl crate::component::IoComponent for LoadedComponent {
    fn execute(
        &mut self,
        request: crate::io::Request<'_>,
    ) -> Result<Option<crate::io::Reply>, IoError> {
        self.dispatch.dispatch(request)
    }

    fn disconnect(&mut self, session: crate::io::SessionId) -> Result<(), IoError> {
        crate::component::IoComponent::disconnect(&mut self.dispatch, session)
    }

    fn reclaim_cache(&mut self) -> Result<(), IoError> {
        if let Some(block) = &mut self.block {
            block.reclaim_cache();
        }
        Ok(())
    }
}

impl LoadedComponent {
    /// Mounts an approved filesystem using one host-owned native block client.
    pub fn mount_filesystem(
        filesystem: BundledFilesystem,
        device_path: &[u8],
    ) -> Result<Self, IoError> {
        let client = crate::block::Client::open(device_path).map_err(|error| match error {
            crate::block::Error::Native(error) => error,
            _ => IoError(crate::protocol::IO_INVALID_PATH),
        })?;
        let block = crate::block_host::BlockHost::new(client);
        let operations = crate::runtime::operations()?;
        let module = Module::load(filesystem.path().as_bytes())?;
        let symbol = module.export(b"hunde_filesystem_component_entry_v1")?;
        let entry: unsafe extern "C" fn(
            *const HostOperations,
            *const crate::block_host::Operations,
        ) -> LoadResult = unsafe { core::mem::transmute(symbol) };
        let dispatch = Dispatch::new(unsafe { entry(operations, block.operations()) })?;
        Ok(Self {
            dispatch,
            _module: module,
            block: Some(block),
        })
    }

    /// Loads an approved component shipped in the read-only bootstrap image.
    pub fn load(component: BundledComponent) -> Result<Self, IoError> {
        unsafe { Self::load_path(component.path().as_bytes()) }
    }

    /// Loads a trusted resident I/O component through the native Wuff loader.
    ///
    /// # Safety
    /// The image must implement the declared versioned entry and callback ABI.
    /// Its callbacks must obey buffer lifetimes, serialize host access, and not
    /// unwind or retain borrowed request pointers. Descriptor validation cannot
    /// establish these properties for arbitrary executable code.
    unsafe fn load_path(path: &[u8]) -> Result<Self, IoError> {
        let operations = crate::runtime::operations()?;
        let module = Module::load(path)?;
        let symbol = module.export(b"hunde_io_component_entry_v6")?;
        let entry: unsafe extern "C" fn(*const HostOperations) -> LoadResult =
            unsafe { core::mem::transmute(symbol) };
        let dispatch = Dispatch::new(unsafe { entry(operations) })?;
        Ok(Self {
            dispatch,
            _module: module,
            block: None,
        })
    }
}

impl Drop for Dispatch {
    fn drop(&mut self) {
        let descriptor = &self.descriptor;
        if unsafe { descriptor.dispose.unwrap()(descriptor.context) }
            .into_result()
            .is_err()
        {
            crate::runtime::fatal(b"Cannot dispose I/O component");
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::abi::{Completion, StatusResult};
    use crate::protocol::{IO_PERMISSION_DENIED, IO_WOULD_BLOCK};

    unsafe extern "C" fn fake_driver_step(_context: *mut c_void) -> DriverStepResult {
        DriverStepResult {
            status: StatusResult::success(),
            flags: 0,
        }
    }

    unsafe extern "C" fn fake_driver_deliver(_context: *mut c_void) {}

    #[test]
    fn driver_descriptor_requires_valid_version_context_and_callbacks() {
        let mut context = 0_u8;
        let mut descriptor = DriverDescriptor {
            header: crate::abi::Header::of::<DriverDescriptor>(),
            context: (&mut context as *mut u8).cast(),
            irq: 13,
            step: Some(fake_driver_step),
            deliver: Some(fake_driver_deliver),
        };
        let load = |descriptor| DriverLoadResult {
            kind: abi::SUCCESS,
            reserved: 0,
            descriptor,
            error: crate::abi::AbiError::new(0, b""),
        };
        assert!(load(descriptor).into_descriptor().is_ok());
        descriptor.header.version += 1;
        assert_eq!(
            load(descriptor).into_descriptor().err(),
            Some(IoError(IO_PANIC))
        );
        descriptor.header.version -= 1;
        descriptor.context = core::ptr::null_mut();
        assert_eq!(
            load(descriptor).into_descriptor().err(),
            Some(IoError(IO_PANIC))
        );
        descriptor.context = (&mut context as *mut u8).cast();
        descriptor.irq = 0;
        assert!(load(descriptor).into_descriptor().is_ok());
        descriptor.irq = 13;
        descriptor.step = None;
        assert_eq!(
            load(descriptor).into_descriptor().err(),
            Some(IoError(IO_PANIC))
        );
        descriptor.step = Some(fake_driver_step);
        descriptor.deliver = None;
        assert_eq!(
            load(descriptor).into_descriptor().err(),
            Some(IoError(IO_PANIC))
        );
    }

    #[test]
    fn manifest_images_are_approved_only_for_their_declared_abi() {
        assert_eq!(
            BundledComponent::find("/sbin/components/socket.wuff")
                .unwrap()
                .path(),
            "/sbin/components/socket.wuff"
        );
        assert_eq!(
            BundledFilesystem::find("/sbin/components/ext2fs.wuff")
                .unwrap()
                .path(),
            "/sbin/components/ext2fs.wuff"
        );
        assert!(BundledComponent::find("/sbin/components/ext2fs.wuff").is_err());
        assert!(BundledFilesystem::find("/sbin/components/socket.wuff").is_err());
        assert!(BundledDriver::find("/sbin/components/uart_rx.wuff").is_ok());
        assert!(BundledDriver::find("/sbin/components/uart_tx.wuff").is_ok());
        assert!(BundledComponent::find("/sbin/components/uart_rx.wuff").is_err());
        assert!(BundledComponent::find("/sbin/components/uart_tx.wuff").is_err());
        assert!(BundledDriver::find("/sbin/components/socket.wuff").is_err());
        for path in [
            "/tmp/socket.wuff",
            "/sbin/components/unknown.wuff",
            "/sbin/components/../components/socket.wuff",
        ] {
            assert!(BundledComponent::find(path).is_err());
            assert!(BundledFilesystem::find(path).is_err());
            assert!(BundledDriver::find(path).is_err());
        }
    }

    struct FakeComponent {
        completion: core::cell::Cell<u32>,
        session: u64,
        pid: u64,
        disconnected: u64,
        disposed: bool,
    }

    impl FakeComponent {
        fn new() -> Self {
            Self {
                completion: core::cell::Cell::new(abi::SUCCESS),
                session: 0,
                pid: 0,
                disconnected: 0,
                disposed: false,
            }
        }

        fn descriptor(&mut self) -> Component {
            Component {
                header: abi::Header::io::<Component>(),
                context: (self as *mut Self).cast(),
                execute: Some(execute),
                disconnect: Some(disconnect),
                dispose: Some(dispose),
            }
        }
    }

    unsafe extern "C" fn execute(context: *mut c_void, request: *const RawRequest) -> Completion {
        let state = unsafe { &mut *context.cast::<FakeComponent>() };
        let request = unsafe { &*request };
        state.session = request.session;
        state.pid = request.pid;
        unsafe { *request.read_data = 255 };
        match state.completion.get() {
            abi::SUCCESS => Completion::from_result(Ok(Some(crate::io::Reply::Value(1)))),
            abi::PENDING => Completion::from_result(Ok(None)),
            _ => Completion::from_result(Err(IoError(IO_PERMISSION_DENIED))),
        }
    }

    unsafe extern "C" fn disconnect(context: *mut c_void, session: u64) -> StatusResult {
        let state = unsafe { &mut *context.cast::<FakeComponent>() };
        state.disconnected = session;
        StatusResult::success()
    }

    unsafe extern "C" fn dispose(context: *mut c_void) -> StatusResult {
        unsafe { (*context.cast::<FakeComponent>()).disposed = true };
        StatusResult::success()
    }

    fn request(target: &mut u8) -> RawRequest {
        RawRequest {
            operation: crate::protocol::IO_REQUEST_READ,
            reserved: 0,
            inheritance_token: 0,
            inheritance_pid: 0,
            buffered: 0,
            padding: 0,
            offset: 0,
            session: 42,
            pid: 71,
            handle: 99,
            flags: 0,
            count: 1,
            read_data: target,
            write_data: core::ptr::null(),
            capacity: 1,
        }
    }

    #[test]
    fn host_dispatch_preserves_identity_buffers_and_completion_tags() {
        let mut state = FakeComponent::new();
        let mut dispatch = Dispatch::new(LoadResult::success(state.descriptor())).unwrap();
        let context = (&mut dispatch as *mut Dispatch).cast();
        let mut target = 0;
        let input = request(&mut target);
        let completion = unsafe { crate::component::execute::<Dispatch>(context, &input) };
        assert_eq!(completion.kind, abi::SUCCESS);
        assert_eq!(completion.value, 1);
        assert_eq!((state.session, state.pid, target), (42, 71, 255));
        state.completion.set(abi::PENDING);
        assert_eq!(
            unsafe { crate::component::execute::<Dispatch>(context, &input) }.kind,
            abi::PENDING
        );
        state.completion.set(abi::ERROR);
        let completion = unsafe { crate::component::execute::<Dispatch>(context, &input) };
        assert_eq!(completion.kind, abi::ERROR);
        assert_eq!(completion.error.code, IO_PERMISSION_DENIED);
    }

    #[test]
    fn host_disconnect_and_drop_reach_the_original_component() {
        let mut state = FakeComponent::new();
        let mut dispatch = Dispatch::new(LoadResult::success(state.descriptor())).unwrap();
        let context = (&mut dispatch as *mut Dispatch).cast();
        assert_eq!(
            unsafe { crate::component::disconnect::<Dispatch>(context, 77) }.into_result(),
            Ok(())
        );
        assert_eq!(state.disconnected, 77);
        assert!(!state.disposed);
        drop(dispatch);
        assert!(state.disposed);
    }

    #[test]
    fn invalid_descriptor_and_null_callback_arguments_are_rejected() {
        assert!(matches!(
            Dispatch::new(LoadResult::error(IO_WOULD_BLOCK)),
            Err(IoError(IO_WOULD_BLOCK))
        ));
        let mut state = FakeComponent::new();
        let mut descriptor = state.descriptor();
        descriptor.execute = None;
        assert!(matches!(
            Dispatch::new(LoadResult::success(descriptor)),
            Err(IoError(-1))
        ));
        assert_eq!(
            unsafe {
                crate::component::execute::<Dispatch>(core::ptr::null_mut(), core::ptr::null())
            }
            .kind,
            abi::ERROR
        );
        assert!(
            unsafe { crate::component::disconnect::<Dispatch>(core::ptr::null_mut(), 1) }
                .into_result()
                .is_err()
        );
        let mut dispatch = Dispatch::new(LoadResult::success(state.descriptor())).unwrap();
        assert_eq!(
            unsafe {
                crate::component::execute::<Dispatch>(
                    (&mut dispatch as *mut Dispatch).cast(),
                    core::ptr::null(),
                )
            }
            .kind,
            abi::ERROR
        );
    }
}
