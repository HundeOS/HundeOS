use crate::abi::{IoError, RawRequest, host};
use crate::component::IoComponent;
use crate::inheritance::Owner;
use crate::io::{Reply, Request, SessionId};
use crate::native::Storage;
use crate::protocol::{IO_PERMISSION_DENIED, IO_REGISTER_FAILED};
use alloc::vec::Vec;

pub struct Filesystem {
    storage: Storage,
    owners: Vec<Owner>,
}

fn remember(owners: &mut Vec<Owner>, owner: Owner) -> Result<(), IoError> {
    if owner.session.0 == 0 || owner.pid.0 == 0 {
        return Err(IoError(IO_PERMISSION_DENIED));
    }
    if let Some(existing) = owners
        .iter()
        .find(|existing| existing.session == owner.session)
    {
        return if *existing == owner {
            Ok(())
        } else {
            Err(IoError(IO_PERMISSION_DENIED))
        };
    }
    owners
        .try_reserve(1)
        .map_err(|_| IoError(IO_REGISTER_FAILED))?;
    owners.push(owner);
    Ok(())
}

impl Filesystem {
    pub fn new() -> Result<Self, IoError> {
        let storage = Storage::new(unsafe { host::hunde_rust_devfs_layout() })?;
        unsafe { host::hunde_rust_devfs_init(storage.as_ptr()) };
        Ok(Self {
            storage,
            owners: Vec::new(),
        })
    }
}

impl IoComponent for Filesystem {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        remember(&mut self.owners, Owner::new(request.session, request.pid))?;
        RawRequest::with_request(request, |raw| {
            unsafe { host::hunde_rust_devfs_execute(self.storage.as_ptr(), raw) }
                .into_result(raw.operation)
        })
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        if let Some(index) = self
            .owners
            .iter()
            .position(|owner| owner.session == session)
        {
            unsafe {
                host::hunde_rust_devfs_disconnect(
                    self.storage.as_ptr(),
                    session.0,
                    self.owners[index].pid.0,
                )
            }
            .into_result()?;
            self.owners.remove(index);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::io::ProcessId;

    #[test]
    fn cleanup_identity_is_authenticated_and_stable_for_each_session() {
        let mut owners = Vec::new();
        let owner = Owner::new(SessionId(7), ProcessId(11));
        remember(&mut owners, owner).unwrap();
        remember(&mut owners, owner).unwrap();
        assert_eq!(owners, [owner]);
        assert_eq!(
            remember(&mut owners, Owner::new(SessionId(7), ProcessId(12))),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            remember(&mut owners, Owner::new(SessionId(0), ProcessId(11))),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            remember(&mut owners, Owner::new(SessionId(7), ProcessId(0))),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(owners, [owner]);
    }
}
