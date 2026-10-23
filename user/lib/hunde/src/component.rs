use crate::abi::{Header, IoError, StatusResult};
use core::ffi::c_void;

#[cfg(feature = "alloc")]
mod io {
    use crate::abi::{
        Completion, Component, Header, HostOperations, IoError, LoadResult, RawRequest,
        StatusResult,
    };
    use crate::io::{Request, SessionId};
    use crate::protocol::IO_PERMISSION_DENIED;
    use alloc::boxed::Box;
    use core::ffi::c_void;

    pub trait IoComponent: 'static {
        fn execute(&mut self, request: Request<'_>) -> Result<Option<crate::io::Reply>, IoError>;
        fn disconnect(&mut self, session: SessionId) -> Result<(), IoError>;
        fn reclaim_cache(&mut self) -> Result<(), IoError> {
            Ok(())
        }
        fn dispose(&mut self) -> Result<(), IoError> {
            Ok(())
        }
    }

    pub trait FilesystemComponent: IoComponent {
        fn mount(device: crate::block_host::HostedDevice) -> Result<Self, IoError>
        where
            Self: Sized;
    }

    pub(crate) unsafe extern "C" fn execute<Driver: IoComponent>(
        context: *mut c_void,
        raw: *const RawRequest,
    ) -> Completion {
        let result = (|| {
            let driver = unsafe { context.cast::<Driver>().as_mut() }
                .ok_or(IoError(IO_PERMISSION_DENIED))?;
            let raw = unsafe { raw.as_ref() }.ok_or(IoError(IO_PERMISSION_DENIED))?;
            let request = unsafe { Request::decode(raw) }?;
            driver.execute(request)
        })();
        Completion::from_result(result)
    }

    pub(crate) unsafe extern "C" fn disconnect<Driver: IoComponent>(
        context: *mut c_void,
        session: u64,
    ) -> StatusResult {
        let result = (|| {
            let driver = unsafe { context.cast::<Driver>().as_mut() }
                .ok_or(IoError(IO_PERMISSION_DENIED))?;
            driver.disconnect(SessionId(session))
        })();
        StatusResult::from_result(result)
    }

    unsafe extern "C" fn dispose<Driver: IoComponent>(context: *mut c_void) -> StatusResult {
        if context.is_null() {
            return StatusResult::from_result(Err(IoError(IO_PERMISSION_DENIED)));
        }
        let driver = unsafe { &mut *context.cast::<Driver>() };
        if let Err(error) = driver.dispose() {
            return StatusResult::from_result(Err(error));
        }
        drop(unsafe { Box::from_raw(context.cast::<Driver>()) });
        StatusResult::success()
    }

    /// Initializes an owned driver and its C callback descriptor.
    ///
    /// # Safety
    /// The host must satisfy `runtime::install_host`'s lifetime and callback
    /// requirements. Returned callbacks must be serialized, receive valid
    /// bounded request buffers, and never run after disposal or image unmapping.
    pub unsafe fn load<Driver: IoComponent + Default>(host: *const HostOperations) -> LoadResult {
        unsafe { load_with(host, || Ok(Driver::default())) }
    }

    /// Initializes a fallible component before publishing any callbacks.
    ///
    /// # Safety
    /// The host and returned callbacks must satisfy `load`'s contracts.
    pub unsafe fn load_with<Driver: IoComponent>(
        host: *const HostOperations,
        initialize: fn() -> Result<Driver, IoError>,
    ) -> LoadResult {
        if let Err(error) = unsafe { crate::runtime::install_host(host) } {
            return LoadResult::error(error.0);
        }
        initialized(initialize())
    }

    fn initialized<Driver: IoComponent>(result: Result<Driver, IoError>) -> LoadResult {
        match result {
            Ok(driver) => descriptor(driver),
            Err(error) => LoadResult::error(error.0),
        }
    }

    /// Mounts an explicit host device before publishing component callbacks.
    ///
    /// # Safety
    /// The host must satisfy `load` and `HostedDevice::from_raw` contracts.
    /// Both capabilities must remain valid until successful component disposal.
    pub unsafe fn mount<Driver: FilesystemComponent>(
        host: *const HostOperations,
        block: *const crate::block_host::Operations,
    ) -> LoadResult {
        if let Err(error) = unsafe { crate::runtime::install_host(host) } {
            return LoadResult::error(error.0);
        }
        let device = match unsafe { crate::block_host::HostedDevice::from_raw(block) } {
            Ok(device) => device,
            Err(error) => return LoadResult::error(error.0),
        };
        mount_device::<Driver>(device)
    }

    fn mount_device<Driver: FilesystemComponent>(
        device: crate::block_host::HostedDevice,
    ) -> LoadResult {
        let result = Driver::mount(device);
        match result {
            Ok(driver) => descriptor(driver),
            Err(error) => LoadResult::error(error.0),
        }
    }

    fn descriptor<Driver: IoComponent>(driver: Driver) -> LoadResult {
        LoadResult::success(Component {
            header: Header::io::<Component>(),
            context: Box::into_raw(Box::new(driver)).cast(),
            execute: Some(execute::<Driver>),
            disconnect: Some(disconnect::<Driver>),
            dispose: Some(dispose::<Driver>),
        })
    }

    #[macro_export]
    macro_rules! io_component {
        ($driver:ty, $message:expr, $initialize:path) => {
            $crate::host_runtime!($message);

            /// Initializes a fallible I/O component through the shared adapter.
            ///
            /// # Safety
            /// The host and callbacks must satisfy `component::load_with`'s contracts.
            #[unsafe(no_mangle)]
            pub unsafe extern "C" fn hunde_io_component_entry_v6(
                host: *const $crate::abi::HostOperations,
            ) -> $crate::abi::LoadResult {
                unsafe { $crate::component::load_with::<$driver>(host, $initialize) }
            }
        };
        ($driver:ty, $message:expr) => {
            $crate::host_runtime!($message);

            /// Initializes this image's I/O component through the shared adapter.
            ///
            /// # Safety
            /// The host and returned callbacks must satisfy `component::load`'s
            /// host-table, request-buffer, serialization, and disposal contracts.
            #[unsafe(no_mangle)]
            pub unsafe extern "C" fn hunde_io_component_entry_v6(
                host: *const $crate::abi::HostOperations,
            ) -> $crate::abi::LoadResult {
                unsafe { $crate::component::load::<$driver>(host) }
            }
        };
    }

    #[macro_export]
    macro_rules! filesystem_component {
        ($driver:ty, $message:expr) => {
            $crate::host_runtime!($message);

            /// Mounts an explicit host capability through the shared ABI adapter.
            ///
            /// # Safety
            /// Both host tables must satisfy `component::mount`'s contracts.
            #[unsafe(no_mangle)]
            pub unsafe extern "C" fn hunde_filesystem_component_entry_v1(
                host: *const $crate::abi::HostOperations,
                block: *const $crate::block_host::Operations,
            ) -> $crate::abi::LoadResult {
                unsafe { $crate::component::mount::<$driver>(host, block) }
            }
        };
    }

    #[cfg(test)]
    mod tests {
        use super::super::{LifecycleComponent, LifecycleHost, initialize, lifecycle_dispose};
        use super::*;
        use crate::abi::{ByteSpan, ERROR, LifecycleHostOperations, PENDING, SUCCESS};
        use crate::io::{Handle, Operation, ProcessId};
        use crate::protocol::IO_UNKNOWN_HANDLE;
        use alloc::rc::Rc;
        use alloc::vec::Vec;
        use core::cell::RefCell;

        #[test]
        fn fallible_initialization_does_not_publish_callbacks_on_failure() {
            let failed = initialized::<Driver>(Err(IoError(-77)));
            assert_eq!(failed.kind, ERROR);
            assert_eq!(failed.error.code, -77);
            assert!(failed.component.execute.is_none());
            assert!(failed.component.disconnect.is_none());
            assert!(failed.component.dispose.is_none());
        }

        #[derive(Default)]
        struct State {
            identity: Option<(SessionId, ProcessId, Handle)>,
            disconnected: Option<SessionId>,
            bytes: Vec<u8>,
            drops: usize,
            dispose_error: bool,
        }

        #[derive(Default)]
        struct Driver(Rc<RefCell<State>>);

        impl IoComponent for Driver {
            fn execute(
                &mut self,
                request: Request<'_>,
            ) -> Result<Option<crate::io::Reply>, IoError> {
                let mut state = self.0.borrow_mut();
                state.identity = Some((request.session, request.pid, request.handle));
                match request.operation {
                    Operation::Write(source) => {
                        state.bytes.extend_from_slice(source);
                        Ok(Some(crate::io::Reply::Value(source.len() as i64)))
                    }
                    Operation::Read(target) => {
                        let count = target.len().min(state.bytes.len());
                        target[..count].copy_from_slice(&state.bytes[..count]);
                        Ok(Some(crate::io::Reply::Value(count as i64)))
                    }
                    Operation::Accept => Ok(None),
                    _ => Err(IoError(IO_UNKNOWN_HANDLE)),
                }
            }

            fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
                self.0.borrow_mut().disconnected = Some(session);
                Ok(())
            }

            fn dispose(&mut self) -> Result<(), IoError> {
                if self.0.borrow().dispose_error {
                    return Err(IoError(-98));
                }
                Ok(())
            }
        }

        impl Drop for Driver {
            fn drop(&mut self) {
                self.0.borrow_mut().drops += 1;
            }
        }

        #[test]
        fn failed_disposal_preserves_context_until_successful_retry() {
            let state = Rc::new(RefCell::new(State::default()));
            state.borrow_mut().dispose_error = true;
            let component = descriptor(Driver(state.clone())).into_result().unwrap();
            assert!(
                unsafe { component.dispose.unwrap()(component.context) }
                    .into_result()
                    .is_err()
            );
            assert_eq!(state.borrow().drops, 0);
            state.borrow_mut().dispose_error = false;
            assert!(
                unsafe { component.dispose.unwrap()(component.context) }
                    .into_result()
                    .is_ok()
            );
            assert_eq!(state.borrow().drops, 1);
        }

        struct MountDriver;

        impl IoComponent for MountDriver {
            fn execute(
                &mut self,
                _request: Request<'_>,
            ) -> Result<Option<crate::io::Reply>, IoError> {
                Ok(None)
            }
            fn disconnect(&mut self, _session: SessionId) -> Result<(), IoError> {
                Ok(())
            }
        }

        impl FilesystemComponent for MountDriver {
            fn mount(mut device: crate::block_host::HostedDevice) -> Result<Self, IoError> {
                use crate::block::Device;
                let mut bytes = [0; 512];
                device
                    .read(crate::block::Sector(0), &mut bytes)
                    .map_err(|_| IoError(-91))?;
                if bytes[0] != 42 {
                    return Err(IoError(-92));
                }
                Ok(Self)
            }
        }

        #[test]
        fn fallible_mount_only_publishes_callbacks_after_initialization() {
            unsafe extern "C" fn read(
                context: *mut c_void,
                _sector: u64,
                data: *mut u8,
                length: u64,
            ) -> StatusResult {
                unsafe { core::slice::from_raw_parts_mut(data, length as usize) }
                    .fill(unsafe { *context.cast::<u8>() });
                StatusResult::success()
            }
            unsafe extern "C" fn write(
                _context: *mut c_void,
                _sector: u64,
                _data: *const u8,
                _length: u64,
            ) -> StatusResult {
                StatusResult::success()
            }
            let mut marker = 0u8;
            let operations = crate::block_host::Operations {
                header: Header::of::<crate::block_host::Operations>(),
                context: (&mut marker as *mut u8).cast(),
                sectors: 1,
                read: Some(read),
                write: Some(write),
            };
            let failed = mount_device::<MountDriver>(
                unsafe { crate::block_host::HostedDevice::from_raw(&operations) }.unwrap(),
            );
            assert_eq!(failed.kind, ERROR);
            assert_eq!(failed.error.code, -92);
            assert!(failed.component.execute.is_none());
            marker = 42;
            assert_eq!(marker, 42);
            let mounted = mount_device::<MountDriver>(
                unsafe { crate::block_host::HostedDevice::from_raw(&operations) }.unwrap(),
            )
            .into_result()
            .unwrap();
            assert!(
                unsafe { mounted.dispose.unwrap()(mounted.context) }
                    .into_result()
                    .is_ok()
            );
        }

        fn request(
            operation: u32,
            count: u64,
            capacity: u64,
            source: &[u8],
            target: &mut [u8],
        ) -> RawRequest {
            RawRequest {
                operation,
                reserved: 0,
                inheritance_token: 0,
                inheritance_pid: 0,
                buffered: 0,
                padding: 0,
                offset: 0,
                session: 123,
                pid: 456,
                handle: 789,
                flags: 0,
                count,
                capacity,
                write_data: source.as_ptr(),
                read_data: target.as_mut_ptr(),
            }
        }

        #[test]
        fn shared_callbacks_preserve_identity_and_borrow_binary_buffers() {
            let state = Rc::new(RefCell::new(State::default()));
            let component = descriptor(Driver(state.clone())).into_result().unwrap();
            let execute = component.execute.unwrap();
            let input = request(
                crate::protocol::IO_REQUEST_WRITE,
                3,
                3,
                &[0, 255, 42],
                &mut [],
            );
            let completion = unsafe { execute(component.context, &input) };
            assert_eq!(completion.kind, SUCCESS);
            assert_eq!(completion.value, 3);
            assert_eq!(
                state.borrow().identity,
                Some((SessionId(123), ProcessId(456), Handle(789)))
            );
            let mut target = [0; 4];
            let input = request(crate::protocol::IO_REQUEST_READ, 4, 4, &[], &mut target);
            let completion = unsafe { execute(component.context, &input) };
            assert_eq!(completion.value, 3);
            assert_eq!(&target[..3], &[0, 255, 42]);
            assert_eq!(
                unsafe { component.disconnect.unwrap()(component.context, 123) }.kind,
                SUCCESS
            );
            assert_eq!(state.borrow().disconnected, Some(SessionId(123)));
            assert_eq!(
                unsafe { component.dispose.unwrap()(component.context) }.kind,
                SUCCESS
            );
            assert_eq!(state.borrow().drops, 1);
        }

        #[test]
        fn shared_callbacks_distinguish_pending_errors_and_invalid_input() {
            let component = descriptor(Driver::default()).into_result().unwrap();
            let execute = component.execute.unwrap();
            let input = request(crate::protocol::IO_REQUEST_ACCEPT, 0, 0, &[], &mut []);
            assert_eq!(unsafe { execute(component.context, &input) }.kind, PENDING);
            let input = request(crate::protocol::IO_REQUEST_CLOSE, 0, 0, &[], &mut []);
            let completion = unsafe { execute(component.context, &input) };
            assert_eq!(completion.kind, ERROR);
            assert_eq!(completion.error.code, IO_UNKNOWN_HANDLE);
            let input = request(crate::protocol::IO_REQUEST_WRITE, 5, 4, &[], &mut []);
            assert_eq!(
                unsafe { execute(component.context, &input) }.error.code,
                crate::protocol::IO_OPERATION_NOT_SUPPORTED
            );
            assert_eq!(
                unsafe { execute(component.context, core::ptr::null()) }
                    .error
                    .code,
                IO_PERMISSION_DENIED
            );
            assert_eq!(
                unsafe { component.dispose.unwrap()(component.context) }.kind,
                SUCCESS
            );
        }

        struct LoggedComponent;

        impl LifecycleComponent for LoggedComponent {
            fn initialize(host: &LifecycleHost<'_>) -> Result<(), IoError> {
                host.log(b"initialized")
            }
        }

        unsafe extern "C" fn log(context: *mut c_void, message: ByteSpan) -> StatusResult {
            let state = unsafe { &mut *context.cast::<Vec<u8>>() };
            state.extend_from_slice(unsafe {
                core::slice::from_raw_parts(message.bytes, message.length as usize)
            });
            StatusResult::success()
        }

        #[test]
        fn lifecycle_callback_uses_the_same_status_and_span_translation() {
            let mut logged = Vec::new();
            let mut host = LifecycleHostOperations {
                header: Header::of::<LifecycleHostOperations>(),
                context: (&mut logged as *mut Vec<u8>).cast(),
                log: Some(log),
                fatal: None,
            };
            let context = (&mut host as *mut LifecycleHostOperations).cast();
            assert_eq!(
                unsafe { initialize::<LoggedComponent>(context) }.kind,
                SUCCESS
            );
            assert_eq!(logged, b"initialized");
            assert_eq!(unsafe { lifecycle_dispose(context) }.kind, SUCCESS);
        }
    }
}

#[cfg(feature = "alloc")]
pub use io::{FilesystemComponent, IoComponent, load, load_with, mount};

#[cfg(feature = "alloc")]
pub(crate) use io::disconnect;
#[cfg(all(test, feature = "alloc"))]
pub(crate) use io::execute;

pub struct LifecycleHost<'a>(&'a crate::abi::LifecycleHostOperations);

impl LifecycleHost<'_> {
    pub fn log(&self, message: &[u8]) -> Result<(), IoError> {
        let log = self.0.log.ok_or(IoError(-1))?;
        unsafe { log(self.0.context, crate::abi::ByteSpan::new(message)) }.into_result()
    }
}

pub trait LifecycleComponent {
    fn initialize(host: &LifecycleHost<'_>) -> Result<(), IoError>;
}

unsafe extern "C" fn initialize<Driver: LifecycleComponent>(context: *mut c_void) -> StatusResult {
    let result = (|| {
        let host = unsafe {
            context
                .cast::<crate::abi::LifecycleHostOperations>()
                .as_ref()
        }
        .ok_or(IoError(-1))?;
        Driver::initialize(&LifecycleHost(host))
    })();
    StatusResult::from_result(result)
}

unsafe extern "C" fn lifecycle_dispose(_context: *mut c_void) -> StatusResult {
    StatusResult::success()
}

/// Creates lifecycle callbacks bound to the supplied host.
///
/// # Safety
/// The host must satisfy `runtime::install_lifecycle_host`'s lifetime and
/// callback requirements. The descriptor may only be used while this image and
/// its host table remain mapped, and callbacks must receive its original context.
pub unsafe fn load_lifecycle<Driver: LifecycleComponent>(
    host: *const crate::abi::LifecycleHostOperations,
) -> crate::abi::LifecycleLoadResult {
    if let Err(error) = unsafe { crate::runtime::install_lifecycle_host(host) } {
        return crate::abi::LifecycleLoadResult::error(error.0);
    }
    crate::abi::LifecycleLoadResult::success(crate::abi::LifecycleDescriptor {
        header: Header::of::<crate::abi::LifecycleDescriptor>(),
        context: host.cast_mut().cast(),
        initialize: Some(initialize::<Driver>),
        dispose: Some(lifecycle_dispose),
    })
}

#[macro_export]
macro_rules! lifecycle_component {
    ($driver:ty, $message:expr) => {
        $crate::panic_handler!($message);

        /// Initializes this image's lifecycle component through the shared adapter.
        ///
        /// # Safety
        /// The host and descriptor callbacks must satisfy
        /// `component::load_lifecycle`'s lifetime and context requirements.
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn hunde_component_entry_v1(
            host: *const $crate::abi::LifecycleHostOperations,
        ) -> $crate::abi::LifecycleLoadResult {
            unsafe { $crate::component::load_lifecycle::<$driver>(host) }
        }
    };
}
