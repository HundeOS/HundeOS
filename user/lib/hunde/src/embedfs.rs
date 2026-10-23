use crate::abi::{IoError, RawRequest, host};
use crate::component::IoComponent;
use crate::io::{Reply, Request, SessionId};
use crate::native::Storage;

/// Read-only access to the process's kernel-mapped bootstrap archive.
pub struct Filesystem {
    storage: Storage,
}

impl Filesystem {
    pub fn new() -> Result<Self, IoError> {
        let storage = Storage::new(unsafe { host::hunde_rust_embedfs_layout() })?;
        unsafe { host::hunde_rust_embedfs_init(storage.as_ptr()) }.into_result()?;
        Ok(Self { storage })
    }
}

impl IoComponent for Filesystem {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        RawRequest::with_request(request, |raw| {
            unsafe { host::hunde_rust_embedfs_execute(self.storage.as_ptr(), raw) }
                .into_result(raw.operation)
        })
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        unsafe { host::hunde_rust_embedfs_disconnect(self.storage.as_ptr(), session.0) }
            .into_result()
    }
}
