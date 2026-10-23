use crate::abi::{self, ByteSpan, IoError};
use crate::component::IoComponent;
use crate::native::Storage;
use alloc::borrow::Cow;
use alloc::boxed::Box;
use alloc::rc::Rc;
use alloc::string::String;
use core::cell::RefCell;
use core::marker::PhantomData;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ConsoleOutput {
    Serial,
    Framebuffer,
}

#[repr(C)]
pub(crate) struct ConsoleResult {
    kind: u32,
    framebuffer: u32,
    error: abi::AbiError,
}

impl ConsoleResult {
    fn into_result(self) -> Result<ConsoleOutput, IoError> {
        match (self.kind, self.framebuffer) {
            (abi::SUCCESS, 0) => Ok(ConsoleOutput::Serial),
            (abi::SUCCESS, 1) => Ok(ConsoleOutput::Framebuffer),
            (abi::ERROR, _) => Err(IoError(self.error.code)),
            _ => Err(IoError(crate::protocol::IO_PANIC)),
        }
    }
}

pub fn console_output() -> Result<ConsoleOutput, IoError> {
    unsafe { abi::host::hunde_rust_console_output() }.into_result()
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MemoryPressure {
    Normal,
    Low,
    Critical,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ServiceEvent {
    MemoryPressure(MemoryPressure),
    Other,
}

#[repr(C)]
pub(crate) struct ServiceEventResult {
    kind: u32,
    event_type: u32,
    source: u64,
    error: abi::AbiError,
}

impl ServiceEventResult {
    fn into_result(self) -> Result<Option<ServiceEvent>, IoError> {
        use crate::protocol::{
            IO_PANIC, MEM_PRESSURE_CRITICAL, MEM_PRESSURE_LOW, MEM_PRESSURE_NORMAL,
            SERVICE_EVENT_MEMORY_PRESSURE,
        };
        match self.kind {
            abi::PENDING => Ok(None),
            abi::ERROR => Err(IoError(self.error.code)),
            abi::SUCCESS if self.event_type == SERVICE_EVENT_MEMORY_PRESSURE => {
                let pressure = match self.source {
                    MEM_PRESSURE_NORMAL => MemoryPressure::Normal,
                    MEM_PRESSURE_LOW => MemoryPressure::Low,
                    MEM_PRESSURE_CRITICAL => MemoryPressure::Critical,
                    _ => return Err(IoError(IO_PANIC)),
                };
                Ok(Some(ServiceEvent::MemoryPressure(pressure)))
            }
            abi::SUCCESS => Ok(Some(ServiceEvent::Other)),
            _ => Err(IoError(IO_PANIC)),
        }
    }
}

const _: () = {
    assert!(core::mem::size_of::<ServiceEventResult>() == 40);
    assert!(core::mem::offset_of!(ServiceEventResult, source) == 8);
};

pub fn poll_event() -> Result<Option<ServiceEvent>, IoError> {
    unsafe { abi::host::hunde_rust_service_event() }.into_result()
}

#[repr(C)]
pub(crate) struct ServiceCompletion {
    completion: abi::Completion,
    redirect: u64,
    offset: u64,
    service_length: u64,
    path_length: u64,
    target_length: u64,
    service: [u8; 256],
    path: [u8; 256],
    target: [u8; 256],
}

impl ServiceCompletion {
    fn from_result(result: Result<Option<crate::io::Reply>, IoError>) -> Self {
        let mut output = Self {
            completion: abi::Completion::from_result(Ok(None)),
            redirect: 0,
            offset: 0,
            service_length: 0,
            path_length: 0,
            target_length: 0,
            service: [0; 256],
            path: [0; 256],
            target: [0; 256],
        };
        if let Ok(Some(crate::io::Reply::Redirect(route))) = &result {
            let checked =
                crate::io::Redirect::new(&route.service, &route.path, &route.target, route.offset);
            if let Err(error) = checked {
                output.completion = abi::Completion::from_result(Err(error));
                return output;
            }
            output.redirect = 1;
            output.offset = route.offset;
            output.service_length = route.service.len() as u64;
            output.path_length = route.path.len() as u64;
            output.target_length = route.target.len() as u64;
            output.service[..route.service.len()].copy_from_slice(route.service.as_bytes());
            output.path[..route.path.len()].copy_from_slice(&route.path);
            output.target[..route.target.len()].copy_from_slice(&route.target);
            output.completion = abi::Completion::from_result(Ok(Some(crate::io::Reply::Value(0))));
        } else {
            output.completion = abi::Completion::from_result(result);
        }
        output
    }
}

unsafe extern "C" fn execute<Handler: IoComponent>(
    context: *mut core::ffi::c_void,
    raw: *const abi::RawRequest,
) -> ServiceCompletion {
    let result = (|| {
        let handler = unsafe { context.cast::<Handler>().as_mut() }
            .ok_or(IoError(crate::protocol::IO_PERMISSION_DENIED))?;
        let raw = unsafe { raw.as_ref() }.ok_or(IoError(crate::protocol::IO_PERMISSION_DENIED))?;
        handler.execute(unsafe { crate::io::Request::decode(raw) }?)
    })();
    ServiceCompletion::from_result(result)
}

unsafe fn retain_local_handler<Handler>(
    context: *mut core::ffi::c_void,
) -> Result<Rc<RefCell<Handler>>, IoError> {
    let address = core::ptr::NonNull::new(context.cast::<RefCell<Handler>>())
        .ok_or(IoError(crate::protocol::IO_PERMISSION_DENIED))?;
    unsafe {
        Rc::increment_strong_count(address.as_ptr());
        Ok(Rc::from_raw(address.as_ptr()))
    }
}

unsafe extern "C" fn execute_local<Handler: IoComponent>(
    context: *mut core::ffi::c_void,
    raw: *const abi::RawRequest,
) -> ServiceCompletion {
    let result = (|| {
        let handler = unsafe { retain_local_handler::<Handler>(context) }?;
        let raw = unsafe { raw.as_ref() }.ok_or(IoError(crate::protocol::IO_PERMISSION_DENIED))?;
        let mut handler = handler
            .try_borrow_mut()
            .map_err(|_| IoError(crate::protocol::IO_OPERATION_NOT_SUPPORTED))?;
        handler.execute(unsafe { crate::io::Request::decode(raw) }?)
    })();
    ServiceCompletion::from_result(result)
}

pub struct LocalPathRouter<Handler: IoComponent> {
    callbacks: Box<abi::host::IoCallbacks>,
    _handler: Rc<RefCell<Handler>>,
}

unsafe extern "C" fn disconnect_local<Handler: IoComponent>(
    context: *mut core::ffi::c_void,
    session: u64,
) -> abi::StatusResult {
    let result = (|| {
        let handler = unsafe { retain_local_handler::<Handler>(context) }?;
        let mut handler = handler
            .try_borrow_mut()
            .map_err(|_| IoError(crate::protocol::IO_OPERATION_NOT_SUPPORTED))?;
        handler.disconnect(crate::io::SessionId(session))
    })();
    abi::StatusResult::from_result(result)
}

impl<Handler: IoComponent> LocalPathRouter<Handler> {
    pub fn register(name: ServiceName, handler: Handler) -> Result<Self, IoError> {
        let handler = Rc::new(RefCell::new(handler));
        let callbacks = Box::new(abi::host::IoCallbacks {
            context: Rc::as_ptr(&handler).cast_mut().cast(),
            execute: execute_local::<Handler>,
            disconnect: disconnect_local::<Handler>,
        });
        unsafe {
            abi::host::hunde_rust_path_router_register(
                ByteSpan::new(name.as_str().as_bytes()),
                &*callbacks,
            )
        }
        .into_result()?;
        Ok(Self {
            callbacks,
            _handler: handler,
        })
    }
}

impl<Handler: IoComponent> Drop for LocalPathRouter<Handler> {
    fn drop(&mut self) {
        unsafe { abi::host::hunde_rust_path_router_unregister(&*self.callbacks) };
    }
}

/// The registered Channel endpoint name used for service discovery and readiness.
///
/// ```compile_fail
/// use hunde::service::{ReadinessMessage, ServiceName};
/// let name: ServiceName = ReadinessMessage::new("Socket service ready");
/// ```
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ServiceName(Cow<'static, str>);

impl ServiceName {
    pub const fn new(name: &'static str) -> Self {
        Self(Cow::Borrowed(name))
    }

    pub fn from_string(name: String) -> Self {
        Self(Cow::Owned(name))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// Human-readable diagnostics logged after an endpoint successfully becomes ready.
///
/// ```compile_fail
/// use hunde::service::{ReadinessMessage, ServiceName};
/// let message: ReadinessMessage = ServiceName::new("socket_router");
/// ```
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReadinessMessage(&'static str);

impl ReadinessMessage {
    pub const fn new(message: &'static str) -> Self {
        Self(message)
    }

    pub const fn as_str(self) -> &'static str {
        self.0
    }
}

pub fn become_service() -> Result<(), IoError> {
    unsafe { abi::host::hunde_rust_become_service() }.into_result()
}

pub fn authorize_mount(client: crate::io::ProcessId) -> Result<(), IoError> {
    unsafe { abi::host::hunde_rust_authorize_mount(client.0) }.into_result()
}

pub struct Service<'component> {
    storage: Storage,
    name: ServiceName,
    _component: PhantomData<&'component mut dyn IoComponent>,
}

impl<'component> Service<'component> {
    pub fn listen<Handler: IoComponent>(
        name: ServiceName,
        component: &'component mut Handler,
    ) -> Result<Self, IoError> {
        let storage = Storage::new(unsafe { abi::host::hunde_rust_service_layout() })?;
        let callbacks = abi::host::IoCallbacks {
            context: (component as *mut Handler).cast(),
            execute: execute::<Handler>,
            disconnect: crate::component::disconnect::<Handler>,
        };
        unsafe {
            abi::host::hunde_rust_service_listen(
                storage.as_ptr(),
                ByteSpan::new(name.as_str().as_bytes()),
                &callbacks,
            )
        }
        .into_result()?;
        Ok(Self {
            storage,
            name,
            _component: PhantomData,
        })
    }

    pub fn publish_ready(&self, message: ReadinessMessage) -> Result<(), IoError> {
        unsafe {
            abi::host::hunde_rust_publish_ready(ByteSpan::new(self.name.as_str().as_bytes()))
        }
        .into_result()?;
        crate::runtime::log(message.as_str().as_bytes());
        Ok(())
    }

    pub fn poll(&mut self) -> bool {
        unsafe { abi::host::hunde_rust_service_poll(self.storage.as_ptr()) }
    }

    pub fn serve_handles(&mut self) {
        unsafe { abi::host::hunde_rust_service_serve_handles(self.storage.as_ptr()) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn service_event_results_distinguish_empty_errors_and_pressure_levels() {
        use crate::protocol::{
            MEM_PRESSURE_CRITICAL, MEM_PRESSURE_LOW, MEM_PRESSURE_NORMAL,
            SERVICE_EVENT_MEMORY_PRESSURE,
        };
        let result = |kind, event_type, source| ServiceEventResult {
            kind,
            event_type,
            source,
            error: abi::AbiError::new(-17, b""),
        };
        assert_eq!(result(abi::PENDING, 0, 0).into_result(), Ok(None));
        assert_eq!(result(abi::ERROR, 0, 0).into_result(), Err(IoError(-17)));
        for (source, pressure) in [
            (MEM_PRESSURE_NORMAL, MemoryPressure::Normal),
            (MEM_PRESSURE_LOW, MemoryPressure::Low),
            (MEM_PRESSURE_CRITICAL, MemoryPressure::Critical),
        ] {
            assert_eq!(
                result(abi::SUCCESS, SERVICE_EVENT_MEMORY_PRESSURE, source).into_result(),
                Ok(Some(ServiceEvent::MemoryPressure(pressure)))
            );
        }
        assert_eq!(
            result(abi::SUCCESS, u32::MAX, 0).into_result(),
            Ok(Some(ServiceEvent::Other))
        );
        assert!(
            result(abi::SUCCESS, SERVICE_EVENT_MEMORY_PRESSURE, u64::MAX)
                .into_result()
                .is_err()
        );
        assert!(result(u32::MAX, 0, 0).into_result().is_err());
    }

    #[test]
    fn console_result_checks_native_tags_and_framebuffer_presence() {
        let result = |kind, framebuffer, code| ConsoleResult {
            kind,
            framebuffer,
            error: abi::AbiError::new(code, b""),
        };
        assert_eq!(
            result(abi::SUCCESS, 0, 0).into_result(),
            Ok(ConsoleOutput::Serial)
        );
        assert_eq!(
            result(abi::SUCCESS, 1, 0).into_result(),
            Ok(ConsoleOutput::Framebuffer)
        );
        assert_eq!(result(abi::ERROR, 0, 19).into_result(), Err(IoError(19)));
        assert!(result(abi::SUCCESS, 2, 0).into_result().is_err());
        assert!(result(77, 0, 0).into_result().is_err());
        assert_eq!(core::mem::size_of::<ConsoleResult>(), 32);
    }

    struct LocalHandler {
        calls: usize,
    }

    impl LocalHandler {
        fn new() -> Self {
            Self { calls: 0 }
        }
    }

    impl IoComponent for LocalHandler {
        fn execute(
            &mut self,
            request: crate::io::Request<'_>,
        ) -> Result<Option<crate::io::Reply>, IoError> {
            self.calls += 1;
            assert_eq!(request.session, crate::io::SessionId(0));
            assert!(matches!(
                request.operation,
                crate::io::Operation::FileInfo(b"/sbin/component")
            ));
            Ok(Some(crate::io::Reply::Value(request.pid.0 as i64)))
        }

        fn disconnect(&mut self, _session: crate::io::SessionId) -> Result<(), IoError> {
            self.calls = 0;
            Ok(())
        }
    }

    #[test]
    fn local_router_preserves_identity_and_rejects_reentrant_mutable_dispatch() {
        use crate::io::{Handle, Operation, ProcessId, Request, SessionId};
        let owner = Rc::new(RefCell::new(LocalHandler::new()));
        let context = Rc::as_ptr(&owner).cast_mut().cast();
        abi::RawRequest::with_request(
            Request::new(
                SessionId(0),
                ProcessId(9),
                Handle(0),
                Operation::FileInfo(b"/sbin/component"),
            ),
            |raw| {
                let output = unsafe { execute_local::<LocalHandler>(context, raw) };
                assert_eq!(output.completion.kind, abi::SUCCESS);
                assert_eq!(output.completion.value, 9);
                assert_eq!(owner.borrow().calls, 1);
                let active = owner.borrow_mut();
                let output = unsafe { execute_local::<LocalHandler>(context, raw) };
                assert_eq!(output.completion.kind, abi::ERROR);
                assert_eq!(
                    output.completion.error.code,
                    crate::protocol::IO_OPERATION_NOT_SUPPORTED
                );
                drop(active);
                let output = unsafe { disconnect_local::<LocalHandler>(context, 0) };
                assert_eq!(output.kind, abi::SUCCESS);
                assert_eq!(owner.borrow().calls, 0);
            },
        );
    }

    #[test]
    fn redirect_envelope_preserves_paths_offsets_and_component_abi() {
        use crate::io::{Redirect, Reply};
        let route = Redirect::new("/tmp", b"/source", b"/target", 17).unwrap();
        let output = ServiceCompletion::from_result(Ok(Some(Reply::Redirect(route.clone()))));
        assert_eq!(output.redirect, 1);
        assert_eq!(output.offset, 17);
        assert_eq!(&output.service[..output.service_length as usize], b"/tmp");
        assert_eq!(&output.path[..output.path_length as usize], b"/source");
        assert_eq!(&output.target[..output.target_length as usize], b"/target");
        assert_eq!(
            abi::Completion::from_result(Ok(Some(Reply::Redirect(route)))).kind,
            abi::ERROR
        );
        assert_eq!(
            ServiceCompletion::from_result(Ok(None)).completion.kind,
            abi::PENDING
        );
        assert_eq!(
            ServiceCompletion::from_result(Err(IoError(19)))
                .completion
                .error
                .code,
            19
        );
        assert!(Redirect::new("bad\0service", b"/", b"", 0).is_err());
        assert!(Redirect::new("/", &[b'x'; 256], b"", 0).is_err());
        assert_eq!(core::mem::size_of::<ServiceCompletion>(), 1856);
    }

    #[test]
    fn service_names_preserve_registry_identifiers_and_mount_paths() {
        const SOCKET: ServiceName = ServiceName::new("socket_router");
        const TEMPORARY: ServiceName = ServiceName::new("/tmp");
        assert_eq!(SOCKET.as_str(), "socket_router");
        assert_eq!(TEMPORARY.as_str(), "/tmp");
        assert_ne!(SOCKET, TEMPORARY);
    }

    #[test]
    fn readiness_messages_preserve_diagnostic_text() {
        const MESSAGE: ReadinessMessage = ReadinessMessage::new("Rust socket.wuff ready");
        assert_eq!(MESSAGE.as_str(), "Rust socket.wuff ready");
        assert_eq!(ReadinessMessage::new("I/O prêt").as_str(), "I/O prêt");
    }

    #[test]
    fn dynamic_service_names_own_their_text_when_cloned_and_moved() {
        let name = ServiceName::from_string(String::from("/mnt/hosted-ext2"));
        let cloned = name.clone();
        drop(name);
        assert_eq!(cloned.as_str(), "/mnt/hosted-ext2");
    }
}
