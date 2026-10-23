use crate::abi::{self, ByteSpan, Component, HostOperations, IoError, LoadResult, RawRequest};
use crate::native::Storage;
use crate::protocol::IO_PANIC;
use core::ffi::c_void;

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
pub enum BundledComponent {
    Socket,
    Pipe,
    Memfs,
    Procfs,
    Sysinfofs,
}

impl BundledComponent {
    fn path(self) -> &'static [u8] {
        match self {
            Self::Socket => b"/sbin/components/socket.wuff",
            Self::Pipe => b"/sbin/components/pipe.wuff",
            Self::Memfs => b"/sbin/components/memfs.wuff",
            Self::Procfs => b"/sbin/components/procfs.wuff",
            Self::Sysinfofs => b"/sbin/components/sysinfofs.wuff",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BundledFilesystem {
    Ext2,
    Fat32,
}

impl BundledFilesystem {
    fn path(self) -> &'static [u8] {
        match self {
            Self::Ext2 => b"/sbin/components/ext2fs.wuff",
            Self::Fat32 => b"/sbin/components/fat32.wuff",
        }
    }
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
        let module = Module::load(filesystem.path())?;
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
        unsafe { Self::load_path(component.path()) }
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

    #[test]
    fn bundled_filesystems_use_approved_bootstrap_image_paths() {
        assert_eq!(
            BundledFilesystem::Ext2.path(),
            b"/sbin/components/ext2fs.wuff"
        );
        assert_eq!(
            BundledFilesystem::Fat32.path(),
            b"/sbin/components/fat32.wuff"
        );
    }

    #[test]
    fn bundled_socket_uses_the_approved_bootstrap_image_path() {
        assert_eq!(
            BundledComponent::Socket.path(),
            b"/sbin/components/socket.wuff"
        );
        assert_eq!(BundledComponent::Pipe.path(), b"/sbin/components/pipe.wuff");
        assert_eq!(
            BundledComponent::Memfs.path(),
            b"/sbin/components/memfs.wuff"
        );
        assert_eq!(
            BundledComponent::Procfs.path(),
            b"/sbin/components/procfs.wuff"
        );
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
