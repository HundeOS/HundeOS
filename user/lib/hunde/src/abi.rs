use crate::protocol::IO_PANIC;
use core::ffi::c_void;
use core::mem::{offset_of, size_of};

pub const VERSION: u32 = 1;
pub const SUCCESS: u32 = 0;
pub const ERROR: u32 = 1;
pub const PENDING: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Header {
    pub size: u32,
    pub version: u32,
}

impl Header {
    pub const fn io<T>() -> Self {
        Self {
            size: size_of::<T>() as u32,
            version: crate::protocol::HUNDE_IO_COMPONENT_VERSION as u32,
        }
    }

    pub fn accepts_io<T>(&self) -> bool {
        self.version == crate::protocol::HUNDE_IO_COMPONENT_VERSION as u32
            && self.size as usize >= size_of::<T>()
    }

    pub const fn of<T>() -> Self {
        Self {
            size: size_of::<T>() as u32,
            version: VERSION,
        }
    }

    pub fn accepts<T>(&self) -> bool {
        self.version == VERSION && self.size as usize >= size_of::<T>()
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ByteSpan {
    pub bytes: *const u8,
    pub length: u64,
}

impl ByteSpan {
    pub const fn new(bytes: &[u8]) -> Self {
        Self {
            bytes: bytes.as_ptr(),
            length: bytes.len() as u64,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct AbiError {
    pub code: i32,
    pub reserved: u32,
    pub message: ByteSpan,
}

impl AbiError {
    pub const fn new(code: i32, message: &'static [u8]) -> Self {
        Self {
            code,
            reserved: 0,
            message: ByteSpan::new(message),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IoError(pub i32);

#[repr(C)]
pub struct StatusResult {
    pub kind: u32,
    pub reserved: u32,
    pub error: AbiError,
}

impl StatusResult {
    pub fn from_result(result: Result<(), IoError>) -> Self {
        match result {
            Ok(()) => Self::success(),
            Err(error) => Self {
                kind: ERROR,
                reserved: 0,
                error: AbiError::new(error.0, b"Host operation failed"),
            },
        }
    }

    pub fn success() -> Self {
        Self {
            kind: SUCCESS,
            reserved: 0,
            error: AbiError::new(0, b""),
        }
    }

    pub fn into_result(self) -> Result<(), IoError> {
        match self.kind {
            SUCCESS => Ok(()),
            ERROR => Err(IoError(self.error.code)),
            _ => Err(IoError(IO_PANIC)),
        }
    }
}

#[repr(C)]
pub struct HostOperations {
    pub header: Header,
    pub context: *mut c_void,
    pub allocate: Option<unsafe extern "C" fn(u64, u64) -> *mut u8>,
    pub deallocate: Option<unsafe extern "C" fn(*mut u8, u64, u64)>,
    pub fatal: Option<unsafe extern "C" fn(*mut c_void, ByteSpan)>,
    pub credentials: Option<unsafe extern "C" fn(u64) -> CredentialsResult>,
    pub process: Option<unsafe extern "C" fn(u64, u32) -> crate::process::ProcessResult>,
    pub trace: Option<unsafe extern "C" fn(*mut u8, u64) -> crate::process::TraceResult>,
    pub boot_info: Option<unsafe extern "C" fn() -> crate::boot::NativeResult>,
}

#[repr(C)]
pub struct CredentialsResult {
    pub kind: u32,
    pub reserved: u32,
    pub user_id: u64,
    pub group_id: u64,
    pub error: AbiError,
}

impl CredentialsResult {
    pub fn into_result(self) -> Result<crate::io::Credentials, IoError> {
        match self.kind {
            SUCCESS => Ok(crate::io::Credentials::new(
                crate::io::UserId(self.user_id),
                crate::io::GroupId(self.group_id),
            )),
            ERROR => Err(IoError(self.error.code)),
            _ => Err(IoError(IO_PANIC)),
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct RawFileInfo {
    pub mode: u32,
    pub reserved: u32,
    pub user_id: u64,
    pub group_id: u64,
    pub size: u64,
    pub identity: u64,
}

impl RawFileInfo {
    fn from_info(info: crate::io::FileInfo) -> Self {
        Self {
            mode: info.mode,
            user_id: info.owner.user.0,
            group_id: info.owner.group.0,
            size: info.size,
            identity: info.identity,
            reserved: 0,
        }
    }

    #[cfg(feature = "alloc")]
    fn into_info(self) -> crate::io::FileInfo {
        crate::io::FileInfo::new(
            self.mode,
            crate::io::Credentials::new(
                crate::io::UserId(self.user_id),
                crate::io::GroupId(self.group_id),
            ),
            self.size,
            self.identity,
        )
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RawDirectoryEntry {
    pub info: RawFileInfo,
    pub name_length: u64,
    pub name: [u8; 256],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct RawFileChange {
    pub handle: i64,
    pub generation: u64,
    pub events: u64,
}

impl RawFileChange {
    fn from_change(change: crate::io::FileChange) -> Self {
        Self {
            handle: change.watch.handle.0,
            generation: change.watch.generation.0,
            events: change.events.0,
        }
    }

    #[cfg(feature = "alloc")]
    fn into_change(self) -> crate::io::FileChange {
        crate::io::FileChange::new(
            crate::io::FileWatch::new(
                crate::io::Handle(self.handle),
                crate::io::FileGeneration(self.generation),
            ),
            crate::io::FileChangeEvents(self.events),
        )
    }
}

#[repr(C)]
pub struct RawRequest {
    pub operation: u32,
    pub reserved: u32,
    pub session: u64,
    pub pid: u64,
    pub handle: i64,
    pub flags: u64,
    pub count: u64,
    pub read_data: *mut u8,
    pub write_data: *const u8,
    pub capacity: u64,
    pub offset: u64,
    pub inheritance_token: u64,
    pub inheritance_pid: u64,
    pub buffered: u32,
    pub padding: u32,
}

#[cfg(feature = "alloc")]
impl RawRequest {
    pub(crate) fn with_request<Value>(
        request: crate::io::Request<'_>,
        callback: impl FnOnce(&Self) -> Value,
    ) -> Value {
        use crate::io::Operation;
        use crate::protocol::*;
        let mut link_paths = alloc::vec::Vec::new();
        let terminal_attributes;
        let mut raw = Self {
            operation: 0,
            reserved: 0,
            session: request.session.0,
            pid: request.pid.0,
            handle: request.handle.0,
            flags: request.transfer_flags,
            count: 0,
            read_data: core::ptr::null_mut(),
            write_data: core::ptr::null(),
            capacity: 0,
            offset: 0,
            inheritance_token: 0,
            inheritance_pid: 0,
            buffered: 0,
            padding: 0,
        };
        raw.operation = match request.operation {
            Operation::SetForegroundProcess(pid) => {
                raw.offset = pid.0;
                IO_REQUEST_SET_FOREGROUND_PROCESS
            }
            Operation::GetTerminalAttributes(attributes) => {
                raw.read_data = core::ptr::from_mut(attributes).cast();
                raw.count = size_of::<crate::terminal::Attributes>() as u64;
                IO_REQUEST_GET_TERMINAL_ATTRIBUTES
            }
            Operation::SetTerminalAttributes(attributes) => {
                terminal_attributes = attributes;
                raw.write_data = core::ptr::from_ref(&terminal_attributes).cast();
                raw.count = size_of::<crate::terminal::Attributes>() as u64;
                IO_REQUEST_SET_TERMINAL_ATTRIBUTES
            }
            Operation::WatchFile => IO_REQUEST_WATCH_FILE,
            Operation::WaitFileChange(watches) => {
                for watch in watches {
                    link_paths.extend_from_slice(&watch.handle.0.to_ne_bytes());
                    link_paths.extend_from_slice(&watch.generation.0.to_ne_bytes());
                }
                raw.write_data = link_paths.as_ptr();
                raw.count = link_paths.len() as u64;
                IO_REQUEST_WAIT_FILE_CHANGE
            }
            Operation::Open(path, flags) => {
                raw.flags = flags;
                raw.write_data = path.as_ptr();
                raw.count = path.len() as u64;
                IO_REQUEST_OPEN
            }
            Operation::Accept => IO_REQUEST_ACCEPT,
            Operation::Read(buffer) => {
                raw.read_data = buffer.as_mut_ptr();
                raw.count = buffer.len() as u64;
                IO_REQUEST_READ
            }
            Operation::Write(buffer) => {
                raw.write_data = buffer.as_ptr();
                raw.count = buffer.len() as u64;
                IO_REQUEST_WRITE
            }
            Operation::Close => IO_REQUEST_CLOSE,
            Operation::Mount(config) => {
                raw.write_data = config.as_ptr();
                raw.count = config.len() as u64;
                IO_REQUEST_MOUNT
            }
            Operation::Release => IO_REQUEST_RELEASE_CLIENT,
            Operation::ListDirectory(path, offset) => {
                raw.write_data = path.as_ptr();
                raw.count = path.len() as u64;
                raw.offset = offset;
                IO_REQUEST_LIST_DIR
            }
            Operation::FileInfo(path) => {
                raw.write_data = path.as_ptr();
                raw.count = path.len() as u64;
                IO_REQUEST_FILE_INFO
            }
            Operation::Mkdir(path) => {
                raw.write_data = path.as_ptr();
                raw.count = path.len() as u64;
                IO_REQUEST_MKDIR
            }
            Operation::Remove(path, flags) => {
                raw.write_data = path.as_ptr();
                raw.count = path.len() as u64;
                raw.flags = flags;
                IO_REQUEST_REMOVE
            }
            Operation::Link { source, target } => {
                link_paths.extend_from_slice(source);
                link_paths.extend_from_slice(target);
                raw.write_data = link_paths.as_ptr();
                raw.count = link_paths.len() as u64;
                raw.offset = source.len() as u64;
                IO_REQUEST_LINK
            }
            Operation::Seek { offset, whence } => {
                raw.offset = offset as u64;
                raw.flags = whence;
                IO_REQUEST_SEEK
            }
            Operation::PipeCreate { buffered, flags } => {
                raw.buffered = u32::from(buffered);
                raw.flags = flags;
                IO_REQUEST_PIPE_CREATE
            }
            Operation::InheritPrepare => IO_REQUEST_INHERIT_PREPARE,
            Operation::InheritCommit { token, target } => {
                raw.inheritance_token = token.0;
                raw.inheritance_pid = target.0;
                IO_REQUEST_INHERIT_COMMIT
            }
            Operation::InheritClaim { token, flags } => {
                raw.inheritance_token = token.0;
                raw.flags = flags;
                IO_REQUEST_INHERIT_CLAIM
            }
            Operation::InheritAbort(token) => {
                raw.inheritance_token = token.0;
                IO_REQUEST_INHERIT_ABORT
            }
        };
        raw.capacity = raw.count;
        callback(&raw)
    }
}

#[repr(C)]
pub struct Completion {
    pub kind: u32,
    pub reserved: u32,
    pub value: i64,
    pub pipe_read: i64,
    pub pipe_write: i64,
    pub inheritance_flags: u64,
    pub file_info: RawFileInfo,
    pub directory_count: u64,
    pub directory: [RawDirectoryEntry; 3],
    pub file_change: RawFileChange,
    pub error: AbiError,
}

impl Completion {
    #[cfg(feature = "alloc")]
    pub(crate) fn into_result(self, operation: u32) -> Result<Option<crate::io::Reply>, IoError> {
        use crate::io::{DirectoryEntry, Handle, InheritanceToken, Reply};
        use crate::protocol::*;
        match self.kind {
            PENDING => return Ok(None),
            ERROR => return Err(IoError(self.error.code)),
            SUCCESS => {}
            _ => return Err(IoError(IO_PANIC)),
        }
        let reply = match operation {
            IO_REQUEST_PIPE_CREATE => Reply::Pipe {
                reader: Handle(self.pipe_read),
                writer: Handle(self.pipe_write),
            },
            IO_REQUEST_INHERIT_PREPARE => Reply::Inheritance {
                token: InheritanceToken(self.value as u64),
                flags: self.inheritance_flags,
            },
            IO_REQUEST_FILE_INFO => Reply::FileInfo(self.file_info.into_info()),
            IO_REQUEST_WATCH_FILE | IO_REQUEST_WAIT_FILE_CHANGE => {
                Reply::FileChange(self.file_change.into_change())
            }
            IO_REQUEST_LIST_DIR => {
                let count = usize::try_from(self.directory_count).map_err(|_| IoError(IO_PANIC))?;
                let entries = self.directory.get(..count).ok_or(IoError(IO_PANIC))?;
                let entries = entries
                    .iter()
                    .map(|entry| {
                        let length =
                            usize::try_from(entry.name_length).map_err(|_| IoError(IO_PANIC))?;
                        let name = entry.name.get(..length).ok_or(IoError(IO_PANIC))?;
                        DirectoryEntry::new(entry.info.into_info(), name)
                    })
                    .collect::<Result<alloc::vec::Vec<_>, _>>()?;
                Reply::Directory(entries)
            }
            _ => Reply::Value(self.value),
        };
        Ok(Some(reply))
    }

    pub fn from_result(result: Result<Option<crate::io::Reply>, IoError>) -> Self {
        let mut completion = Self {
            kind: SUCCESS,
            reserved: 0,
            value: 0,
            pipe_read: 0,
            pipe_write: 0,
            inheritance_flags: 0,
            file_info: RawFileInfo::default(),
            directory_count: 0,
            file_change: RawFileChange::default(),
            directory: [RawDirectoryEntry {
                info: RawFileInfo::default(),
                name_length: 0,
                name: [0; 256],
            }; 3],
            error: AbiError::new(0, b""),
        };
        match result {
            #[cfg(feature = "alloc")]
            Ok(Some(crate::io::Reply::Redirect(_))) => {
                return Self::from_result(Err(IoError(
                    crate::protocol::IO_OPERATION_NOT_SUPPORTED,
                )));
            }
            Ok(Some(crate::io::Reply::FileChange(change))) => {
                completion.file_change = RawFileChange::from_change(change);
            }
            Ok(Some(crate::io::Reply::Value(value))) => completion.value = value,
            Ok(Some(crate::io::Reply::FileInfo(info))) => {
                completion.file_info = RawFileInfo::from_info(info)
            }
            #[cfg(feature = "alloc")]
            Ok(Some(crate::io::Reply::Directory(entries))) => {
                if entries.len() > completion.directory.len()
                    || entries.iter().any(|entry| entry.name().len() >= 256)
                {
                    return Self::from_result(Err(IoError(
                        crate::protocol::IO_OPERATION_NOT_SUPPORTED,
                    )));
                }
                completion.directory_count = entries.len() as u64;
                for (target, entry) in completion.directory.iter_mut().zip(entries) {
                    target.info = RawFileInfo::from_info(entry.info);
                    target.name_length = entry.name().len() as u64;
                    target.name[..entry.name().len()].copy_from_slice(entry.name());
                }
            }
            Ok(Some(crate::io::Reply::Pipe { reader, writer })) => {
                completion.pipe_read = reader.0;
                completion.pipe_write = writer.0;
            }
            Ok(Some(crate::io::Reply::Inheritance { token, flags })) => {
                completion.value = token.0 as i64;
                completion.inheritance_flags = flags;
            }
            Ok(None) => completion.kind = PENDING,
            Err(IoError(code)) => {
                completion.kind = ERROR;
                completion.error = AbiError::new(code, b"I/O operation failed");
            }
        }
        completion
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Component {
    pub header: Header,
    pub context: *mut c_void,
    pub execute: Option<unsafe extern "C" fn(*mut c_void, *const RawRequest) -> Completion>,
    pub disconnect: Option<unsafe extern "C" fn(*mut c_void, u64) -> StatusResult>,
    pub dispose: Option<unsafe extern "C" fn(*mut c_void) -> StatusResult>,
}

#[repr(C)]
pub struct LoadResult {
    pub kind: u32,
    pub reserved: u32,
    pub component: Component,
    pub error: AbiError,
}

impl LoadResult {
    pub fn success(component: Component) -> Self {
        Self {
            kind: SUCCESS,
            reserved: 0,
            component,
            error: AbiError::new(0, b""),
        }
    }

    pub fn error(code: i32) -> Self {
        Self {
            kind: ERROR,
            reserved: 0,
            component: Component {
                header: Header::io::<Component>(),
                context: core::ptr::null_mut(),
                execute: None,
                disconnect: None,
                dispose: None,
            },
            error: AbiError::new(code, b"Cannot initialize I/O component"),
        }
    }

    pub fn into_result(self) -> Result<Component, IoError> {
        if self.kind != SUCCESS {
            return Err(IoError(self.error.code));
        }
        if !self.component.header.accepts_io::<Component>()
            || self.component.context.is_null()
            || self.component.execute.is_none()
            || self.component.disconnect.is_none()
            || self.component.dispose.is_none()
        {
            return Err(IoError(-1));
        }
        Ok(self.component)
    }
}

#[repr(C)]
pub struct LifecycleHostOperations {
    pub header: Header,
    pub context: *mut c_void,
    pub log: Option<unsafe extern "C" fn(*mut c_void, ByteSpan) -> StatusResult>,
    pub fatal: Option<unsafe extern "C" fn(*mut c_void, ByteSpan)>,
}

#[repr(C)]
pub struct LifecycleDescriptor {
    pub header: Header,
    pub context: *mut c_void,
    pub initialize: Option<unsafe extern "C" fn(*mut c_void) -> StatusResult>,
    pub dispose: Option<unsafe extern "C" fn(*mut c_void) -> StatusResult>,
}

#[repr(C)]
pub struct LifecycleLoadResult {
    pub kind: u32,
    pub reserved: u32,
    pub component: LifecycleDescriptor,
    pub error: AbiError,
}

impl LifecycleLoadResult {
    pub fn success(component: LifecycleDescriptor) -> Self {
        Self {
            kind: SUCCESS,
            reserved: 0,
            component,
            error: AbiError::new(0, b""),
        }
    }

    pub fn error(code: i32) -> Self {
        Self {
            kind: ERROR,
            reserved: 0,
            component: LifecycleDescriptor {
                header: Header::of::<LifecycleDescriptor>(),
                context: core::ptr::null_mut(),
                initialize: None,
                dispose: None,
            },
            error: AbiError::new(code, b"Invalid host component operations"),
        }
    }
}

#[cfg(feature = "alloc")]
pub(crate) mod host {
    use super::*;
    #[repr(C)]
    pub(crate) struct NativeLayout {
        pub size: u64,
        pub alignment: u64,
    }
    #[repr(C)]
    pub(crate) struct AddressResult {
        pub kind: u32,
        pub reserved: u32,
        pub address: *mut c_void,
        pub error: AbiError,
    }
    #[repr(C)]
    pub(crate) struct IoCallbacks {
        pub context: *mut c_void,
        pub execute: unsafe extern "C" fn(
            *mut c_void,
            *const RawRequest,
        ) -> crate::service::ServiceCompletion,
        pub disconnect: unsafe extern "C" fn(*mut c_void, u64) -> StatusResult,
    }

    const _: () = {
        assert!(size_of::<NativeLayout>() == 16);
        assert!(size_of::<AddressResult>() == 40);
        assert!(offset_of!(AddressResult, address) == 8);
        assert!(offset_of!(AddressResult, error) == 16);
        assert!(size_of::<IoCallbacks>() == 24);
    };
    unsafe extern "C" {
        pub(crate) fn hunde_rust_bootstrap_read(
            path: ByteSpan,
            buffer: *mut u8,
            capacity: usize,
        ) -> Completion;
        pub(crate) fn hunde_rust_read_file(
            path: ByteSpan,
            buffer: *mut u8,
            capacity: usize,
        ) -> Completion;
        pub(crate) fn hunde_rust_boot_info() -> crate::boot::NativeResult;
        pub(crate) fn hunde_rust_embedfs_layout() -> NativeLayout;
        pub(crate) fn hunde_rust_embedfs_init(storage: *mut c_void) -> StatusResult;
        pub(crate) fn hunde_rust_embedfs_execute(
            storage: *mut c_void,
            input: *const RawRequest,
        ) -> Completion;
        pub(crate) fn hunde_rust_embedfs_disconnect(
            storage: *mut c_void,
            session: u64,
        ) -> StatusResult;
        pub(crate) fn hunde_rust_block_layout() -> NativeLayout;
        pub(crate) fn hunde_rust_devfs_layout() -> NativeLayout;
        pub(crate) fn hunde_rust_devfs_init(storage: *mut c_void);
        pub(crate) fn hunde_rust_devfs_execute(
            storage: *mut c_void,
            input: *const RawRequest,
        ) -> Completion;
        pub(crate) fn hunde_rust_devfs_disconnect(
            storage: *mut c_void,
            session: u64,
            pid: u64,
        ) -> StatusResult;
        pub(crate) fn hunde_rust_terminal_open(service: ByteSpan, flags: u64) -> Completion;
        pub(crate) fn hunde_rust_terminal_read(
            handle: i64,
            data: *mut u8,
            length: u64,
        ) -> Completion;
        pub(crate) fn hunde_rust_terminal_write(
            handle: i64,
            data: *const u8,
            length: u64,
        ) -> Completion;
        pub(crate) fn hunde_rust_terminal_close(handle: i64) -> StatusResult;
        pub(crate) fn hunde_rust_terminal_interrupt(pid: u64) -> StatusResult;
        pub(crate) fn hunde_rust_block_open(
            storage: *mut c_void,
            path: ByteSpan,
        ) -> crate::block::OpenResult;
        pub(crate) fn hunde_rust_block_read(
            storage: *mut c_void,
            sector: u64,
            destination: *mut u8,
            length: u64,
        ) -> StatusResult;
        pub(crate) fn hunde_rust_block_write(
            storage: *mut c_void,
            sector: u64,
            source: *const u8,
            length: u64,
        ) -> StatusResult;
        pub(crate) fn hunde_rust_block_close(storage: *mut c_void) -> StatusResult;
        pub(crate) fn hunde_rust_block_reclaim(storage: *mut c_void);
        pub(crate) fn hunde_rust_credentials(pid: u64) -> CredentialsResult;
        pub(crate) fn hunde_rust_process(
            selector: u64,
            by_index: u32,
        ) -> crate::process::ProcessResult;
        pub(crate) fn hunde_rust_kernel_trace(
            destination: *mut u8,
            capacity: u64,
        ) -> crate::process::TraceResult;
        pub(crate) fn memalign(alignment: usize, size: usize) -> *mut u8;
        pub(crate) fn free(allocation: *mut c_void);
        pub(crate) fn exit(status: i32) -> !;
        pub(crate) fn hunde_rust_log(message: ByteSpan);
        pub(crate) fn hunde_rust_module_layout() -> NativeLayout;
        pub(crate) fn hunde_rust_module_load(storage: *mut c_void, path: ByteSpan) -> StatusResult;
        pub(crate) fn hunde_rust_module_export(
            storage: *mut c_void,
            name: ByteSpan,
        ) -> AddressResult;
        pub(crate) fn hunde_rust_module_dispose(storage: *mut c_void) -> StatusResult;
        pub(crate) fn hunde_rust_service_layout() -> NativeLayout;
        pub(crate) fn hunde_rust_service_listen(
            storage: *mut c_void,
            name: ByteSpan,
            callbacks: *const IoCallbacks,
        ) -> StatusResult;
        pub(crate) fn hunde_rust_service_poll(storage: *mut c_void) -> bool;
        pub(crate) fn hunde_rust_service_serve_handles(storage: *mut c_void);
        pub(crate) fn hunde_rust_path_router_register(
            name: ByteSpan,
            callbacks: *const IoCallbacks,
        ) -> StatusResult;
        pub(crate) fn hunde_rust_path_router_unregister(callbacks: *const IoCallbacks);
        pub(crate) fn hunde_rust_become_service() -> StatusResult;
        pub(crate) fn hunde_rust_service_event() -> crate::service::ServiceEventResult;
        pub(crate) fn hunde_rust_console_output() -> crate::service::ConsoleResult;
        pub(crate) fn hunde_rust_authorize_mount(pid: u64) -> StatusResult;
        pub(crate) fn hunde_rust_publish_ready(name: ByteSpan) -> StatusResult;
        pub(crate) fn hunde_rust_scheduler_layout() -> NativeLayout;
        pub(crate) fn hunde_rust_scheduler_init(storage: *mut c_void);
        pub(crate) fn hunde_rust_scheduler_spawn(
            storage: *mut c_void,
            task: *const crate::fibre::NativeTask,
        ) -> StatusResult;
        pub(crate) fn hunde_rust_scheduler_run(storage: *mut c_void) -> StatusResult;
        pub(crate) fn hunde_rust_scheduler_park(storage: *mut c_void) -> StatusResult;
        pub(crate) fn hunde_rust_scheduler_wake(storage: *mut c_void) -> StatusResult;
        pub(crate) fn hunde_rust_scheduler_live(storage: *mut c_void) -> u64;
        pub(crate) fn hunde_rust_scheduler_waiting(storage: *mut c_void) -> u64;
        pub(crate) fn hunde_rust_channel_prepare_wait();
        pub(crate) fn hunde_rust_channel_wait();
        pub(crate) fn hunde_rust_channel_wait_ticks(ticks: u64);
    }
}

const _: () = {
    assert!(size_of::<Header>() == 8);
    assert!(size_of::<ByteSpan>() == 16);
    assert!(size_of::<AbiError>() == 24);
    assert!(size_of::<StatusResult>() == 32);
    assert!(size_of::<HostOperations>() == 72);
    assert!(size_of::<CredentialsResult>() == 48);
    assert!(size_of::<RawFileInfo>() == 40);
    assert!(size_of::<RawDirectoryEntry>() == 304);
    assert!(size_of::<RawRequest>() == 104);
    assert!(offset_of!(RawRequest, read_data) == 48);
    assert!(size_of::<Completion>() == 1048);
    assert!(size_of::<Component>() == 40);
    assert!(size_of::<LoadResult>() == 72);
    assert!(size_of::<LifecycleHostOperations>() == 32);
    assert!(size_of::<LifecycleDescriptor>() == 32);
    assert!(size_of::<LifecycleLoadResult>() == 64);
    assert!(offset_of!(ByteSpan, length) == 8);
    assert!(offset_of!(AbiError, message) == 8);
    assert!(offset_of!(StatusResult, error) == 8);
    assert!(offset_of!(LifecycleHostOperations, context) == 8);
    assert!(offset_of!(LifecycleDescriptor, context) == 8);
    assert!(offset_of!(LifecycleDescriptor, initialize) == 16);
    assert!(offset_of!(LifecycleLoadResult, component) == 8);
    assert!(offset_of!(LifecycleLoadResult, error) == 40);
};

#[cfg(test)]
mod tests {
    use super::*;

    #[cfg(feature = "alloc")]
    #[test]
    fn typed_requests_round_trip_through_the_component_boundary() {
        use crate::io::{Handle, InheritanceToken, Operation, ProcessId, Request, SessionId};
        use crate::protocol::*;
        let cases = [
            (Operation::Open(b"/file", 17), IO_REQUEST_OPEN),
            (Operation::WatchFile, IO_REQUEST_WATCH_FILE),
            (Operation::Accept, IO_REQUEST_ACCEPT),
            (Operation::Write(b"bytes"), IO_REQUEST_WRITE),
            (Operation::Close, IO_REQUEST_CLOSE),
            (
                Operation::Mount(b"device /dev/virtio7 ext2.wuff /mnt/disk"),
                IO_REQUEST_MOUNT,
            ),
            (Operation::Release, IO_REQUEST_RELEASE_CLIENT),
            (Operation::ListDirectory(b"/dir", 29), IO_REQUEST_LIST_DIR),
            (Operation::FileInfo(b"/file"), IO_REQUEST_FILE_INFO),
            (Operation::Mkdir(b"/dir"), IO_REQUEST_MKDIR),
            (Operation::Remove(b"/file", 23), IO_REQUEST_REMOVE),
            (
                Operation::Seek {
                    offset: -71,
                    whence: 2,
                },
                IO_REQUEST_SEEK,
            ),
            (
                Operation::PipeCreate {
                    buffered: true,
                    flags: 31,
                },
                IO_REQUEST_PIPE_CREATE,
            ),
            (Operation::InheritPrepare, IO_REQUEST_INHERIT_PREPARE),
            (
                Operation::Link {
                    source: b"/from",
                    target: b"/to",
                },
                IO_REQUEST_LINK,
            ),
            (
                Operation::InheritCommit {
                    token: InheritanceToken(u64::MAX),
                    target: ProcessId(41),
                },
                IO_REQUEST_INHERIT_COMMIT,
            ),
            (
                Operation::InheritClaim {
                    token: InheritanceToken(43),
                    flags: 47,
                },
                IO_REQUEST_INHERIT_CLAIM,
            ),
            (
                Operation::InheritAbort(InheritanceToken(53)),
                IO_REQUEST_INHERIT_ABORT,
            ),
        ];
        for (operation, expected) in cases {
            let request = Request::new(SessionId(7), ProcessId(11), Handle(13), operation);
            RawRequest::with_request(request, |raw| {
                assert_eq!(raw.operation, expected);
                assert_eq!((raw.session, raw.pid, raw.handle), (7, 11, 13));
                let decoded = unsafe { Request::decode(raw) }.unwrap();
                RawRequest::with_request(decoded, |second| {
                    assert_eq!(second.operation, raw.operation);
                    assert_eq!(
                        (second.flags, second.count, second.offset),
                        (raw.flags, raw.count, raw.offset)
                    );
                    assert_eq!(
                        (
                            second.inheritance_token,
                            second.inheritance_pid,
                            second.buffered
                        ),
                        (raw.inheritance_token, raw.inheritance_pid, raw.buffered)
                    );
                    if expected == IO_REQUEST_LINK {
                        assert_eq!(
                            unsafe {
                                core::slice::from_raw_parts(
                                    second.write_data,
                                    second.count as usize,
                                )
                            },
                            b"/from/to"
                        );
                    } else {
                        assert_eq!(second.write_data, raw.write_data);
                    }
                });
            });
        }
        let mut buffer = [0; 3];
        let request = Request::new(
            SessionId(7),
            ProcessId(11),
            Handle(13),
            Operation::Read(&mut buffer),
        )
        .with_transfer_flags(IO_TRANSFER_NONBLOCK);
        RawRequest::with_request(request, |raw| {
            assert_eq!(raw.operation, IO_REQUEST_READ);
            assert_eq!(
                (raw.count, raw.capacity, raw.flags),
                (3, 3, IO_TRANSFER_NONBLOCK)
            );
            match unsafe { Request::decode(raw) }.unwrap().operation {
                Operation::Read(target) => target.fill(42),
                _ => panic!("Expected read"),
            }
        });
        assert_eq!(buffer, [42; 3]);
    }

    #[cfg(feature = "alloc")]
    #[test]
    fn completions_preserve_all_reply_shapes_and_pending_errors() {
        use crate::io::{
            Credentials, DirectoryEntry, FileInfo, GroupId, Handle, InheritanceToken, Reply, UserId,
        };
        use crate::protocol::*;
        let info = FileInfo::new(0o100644, Credentials::new(UserId(3), GroupId(5)), 79, 83);
        let cases = [
            (
                IO_REQUEST_WATCH_FILE,
                Reply::FileChange(crate::io::FileChange::new(
                    crate::io::FileWatch::new(
                        Handle(i64::MAX),
                        crate::io::FileGeneration(u64::MAX),
                    ),
                    crate::io::FileChangeEvents(IO_FILE_CHANGE_WRITE | IO_FILE_CHANGE_TRUNCATE),
                )),
            ),
            (
                IO_REQUEST_WAIT_FILE_CHANGE,
                Reply::FileChange(crate::io::FileChange::new(
                    crate::io::FileWatch::new(Handle(1 << 32), crate::io::FileGeneration(17)),
                    crate::io::FileChangeEvents(IO_FILE_CHANGE_DELETE),
                )),
            ),
            (IO_REQUEST_READ, Reply::Value(97)),
            (
                IO_REQUEST_PIPE_CREATE,
                Reply::Pipe {
                    reader: Handle(101),
                    writer: Handle(103),
                },
            ),
            (
                IO_REQUEST_INHERIT_PREPARE,
                Reply::Inheritance {
                    token: InheritanceToken(u64::MAX),
                    flags: 107,
                },
            ),
            (IO_REQUEST_FILE_INFO, Reply::FileInfo(info)),
            (
                IO_REQUEST_LIST_DIR,
                Reply::Directory(alloc::vec![
                    DirectoryEntry::new(info, b".").unwrap(),
                    DirectoryEntry::new(info, b"file").unwrap()
                ]),
            ),
            (IO_REQUEST_LIST_DIR, Reply::Directory(alloc::vec![])),
        ];
        for (operation, reply) in cases {
            assert_eq!(
                Completion::from_result(Ok(Some(reply.clone()))).into_result(operation),
                Ok(Some(reply))
            );
        }
        assert_eq!(
            Completion::from_result(Ok(None)).into_result(IO_REQUEST_READ),
            Ok(None)
        );
        assert_eq!(
            Completion::from_result(Err(IoError(IO_PERMISSION_DENIED)))
                .into_result(IO_REQUEST_READ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
    }

    #[cfg(feature = "alloc")]
    #[test]
    fn malformed_completion_tags_counts_and_names_are_rejected() {
        use crate::protocol::*;
        let mut completion = Completion::from_result(Ok(None));
        completion.kind = 99;
        assert_eq!(
            completion.into_result(IO_REQUEST_READ),
            Err(IoError(IO_PANIC))
        );
        let mut completion = Completion::from_result(Ok(Some(crate::io::Reply::Value(0))));
        completion.directory_count = 4;
        assert_eq!(
            completion.into_result(IO_REQUEST_LIST_DIR),
            Err(IoError(IO_PANIC))
        );
        let mut completion = Completion::from_result(Ok(Some(crate::io::Reply::Value(0))));
        completion.directory_count = 1;
        completion.directory[0].name_length = u64::MAX;
        assert!(completion.into_result(IO_REQUEST_LIST_DIR).is_err());
    }
    use crate::protocol::IO_PERMISSION_DENIED;

    #[test]
    fn pending_is_distinct_from_an_error() {
        assert_eq!(Completion::from_result(Ok(None)).kind, PENDING);
        let failure = Completion::from_result(Err(IoError(IO_PERMISSION_DENIED)));
        assert_eq!(failure.kind, ERROR);
        assert_eq!(failure.error.code, IO_PERMISSION_DENIED);
    }

    #[test]
    fn invalid_component_descriptor_is_rejected_before_callbacks() {
        let mut result = LoadResult::error(-1);
        result.kind = SUCCESS;
        assert!(matches!(result.into_result(), Err(IoError(-1))));
    }

    #[test]
    fn invalid_status_tag_is_not_success() {
        let mut result = StatusResult::success();
        result.kind = PENDING;
        assert_eq!(result.into_result(), Err(IoError(IO_PANIC)));
    }

    #[test]
    fn credentials_preserve_full_width_ids_and_fail_closed() {
        let credentials = CredentialsResult {
            kind: SUCCESS,
            reserved: 0,
            user_id: 1 << 40,
            group_id: 1 << 41,
            error: AbiError::new(0, b""),
        }
        .into_result()
        .unwrap();
        assert_eq!(credentials.user.0, 1 << 40);
        assert_eq!(credentials.group.0, 1 << 41);
        for (kind, expected) in [
            (ERROR, crate::protocol::IO_UNKNOWN_PID),
            (PENDING, IO_PANIC),
        ] {
            let result = CredentialsResult {
                kind,
                reserved: 0,
                user_id: 0,
                group_id: 0,
                error: AbiError::new(crate::protocol::IO_UNKNOWN_PID, b"Unknown process"),
            };
            assert_eq!(result.into_result(), Err(IoError(expected)));
        }
    }

    #[cfg(feature = "alloc")]
    #[test]
    fn filesystem_completions_preserve_metadata_names_and_enforce_batch_bounds() {
        use crate::io::{Credentials, DirectoryEntry, FileInfo, GroupId, Reply, UserId};
        let info = FileInfo::new(
            crate::protocol::IO_MODE_TYPE_FILE as u32 | 0o644,
            Credentials::new(UserId(1 << 40), GroupId(1 << 41)),
            70000,
            55,
        );
        let completion = Completion::from_result(Ok(Some(Reply::FileInfo(info))));
        assert_eq!(
            (
                completion.file_info.user_id,
                completion.file_info.group_id,
                completion.file_info.identity
            ),
            (1 << 40, 1 << 41, 55)
        );
        let entry = DirectoryEntry::new(info, b"file").unwrap();
        let completion =
            Completion::from_result(Ok(Some(Reply::Directory(alloc::vec![entry.clone()]))));
        assert_eq!(completion.directory_count, 1);
        assert_eq!(&completion.directory[0].name[..4], b"file");
        let invalid = Completion::from_result(Ok(Some(Reply::Directory(alloc::vec![entry; 4]))));
        assert_eq!(
            (invalid.kind, invalid.error.code),
            (ERROR, crate::protocol::IO_OPERATION_NOT_SUPPORTED)
        );
    }

    #[test]
    fn io_version_is_independent_of_lifecycle_and_rejects_v1() {
        assert!(Header::io::<Component>().accepts_io::<Component>());
        assert!(
            !Header {
                size: size_of::<Component>() as u32,
                version: 5
            }
            .accepts_io::<Component>()
        );
        assert!(!Header::of::<Component>().accepts_io::<Component>());
        assert!(
            !Header {
                size: size_of::<Component>() as u32,
                version: 3
            }
            .accepts_io::<Component>()
        );
        assert!(Header::of::<LifecycleDescriptor>().accepts::<LifecycleDescriptor>());
    }

    #[test]
    fn structured_completions_preserve_both_pipe_handles_and_grant_rights() {
        use crate::io::{Handle, InheritanceToken, Reply};
        let completion = Completion::from_result(Ok(Some(Reply::Pipe {
            reader: Handle(123),
            writer: Handle(456),
        })));
        assert_eq!(
            (completion.kind, completion.pipe_read, completion.pipe_write),
            (SUCCESS, 123, 456)
        );
        let completion = Completion::from_result(Ok(Some(Reply::Inheritance {
            token: InheritanceToken(789),
            flags: crate::protocol::IO_OPEN_WRITE,
        })));
        assert_eq!(
            (
                completion.kind,
                completion.value,
                completion.inheritance_flags
            ),
            (SUCCESS, 789, crate::protocol::IO_OPEN_WRITE)
        );
    }
}
