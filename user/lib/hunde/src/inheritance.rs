use crate::abi::IoError;
use crate::io::{Handle, InheritanceToken, ProcessId, Reply, SessionId};
use crate::protocol::{IO_PERMISSION_DENIED, IO_REGISTER_FAILED, IO_UNKNOWN_HANDLE};
use alloc::vec::Vec;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Owner {
    pub session: SessionId,
    pub pid: ProcessId,
}

impl Owner {
    pub const fn new(session: SessionId, pid: ProcessId) -> Self {
        Self { session, pid }
    }
}

#[derive(Clone, Copy)]
pub struct Grant {
    pub handle: Handle,
    pub flags: u64,
    token: InheritanceToken,
    source: Owner,
    target: Option<ProcessId>,
}

pub struct Grants {
    entries: Vec<Grant>,
    next_token: u64,
}

impl Default for Grants {
    fn default() -> Self {
        Self {
            entries: Vec::new(),
            next_token: 1,
        }
    }
}

impl Grants {
    fn index(&self, token: InheritanceToken) -> Result<usize, IoError> {
        self.entries
            .iter()
            .position(|grant| grant.token == token)
            .ok_or(IoError(IO_UNKNOWN_HANDLE))
    }
    pub fn prepare(&mut self, handle: Handle, source: Owner, flags: u64) -> Result<Reply, IoError> {
        if self.entries.len() == 64 || self.next_token > i64::MAX as u64 {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let token = InheritanceToken(self.next_token);
        self.next_token += 1;
        self.entries.push(Grant {
            handle,
            flags,
            token,
            source,
            target: None,
        });
        Ok(Reply::Inheritance { token, flags })
    }
    pub fn commit(
        &mut self,
        token: InheritanceToken,
        source: Owner,
        target: ProcessId,
    ) -> Result<(), IoError> {
        let index = self.index(token)?;
        let grant = &mut self.entries[index];
        if grant.source != source || grant.target.is_some() || target.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        grant.target = Some(target);
        Ok(())
    }
    pub fn claim(
        &self,
        token: InheritanceToken,
        owner: Owner,
        flags: u64,
    ) -> Result<Grant, IoError> {
        let grant = self.entries[self.index(token)?];
        if grant.target != Some(owner.pid) || flags != grant.flags {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        Ok(grant)
    }
    pub fn complete(&mut self, token: InheritanceToken) -> Result<(), IoError> {
        let index = self.index(token)?;
        self.entries.remove(index);
        Ok(())
    }
    pub fn abort(&mut self, token: InheritanceToken, source: Owner) -> Result<(), IoError> {
        self.abort_candidate(token, source)?;
        self.complete(token)
    }
    pub fn abort_candidate(
        &self,
        token: InheritanceToken,
        source: Owner,
    ) -> Result<Grant, IoError> {
        let index = self.index(token)?;
        if self.entries[index].source != source {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        Ok(self.entries[index])
    }
    pub fn retains(&self, handle: Handle) -> bool {
        self.entries
            .iter()
            .any(|grant| grant.handle == handle && grant.target.is_some())
    }
    pub fn retains_except(&self, handle: Handle, token: InheritanceToken) -> bool {
        self.entries
            .iter()
            .any(|grant| grant.handle == handle && grant.target.is_some() && grant.token != token)
    }
    pub fn close(&mut self, handle: Handle, source: Owner) {
        self.entries.retain(|grant| {
            grant.handle != handle || grant.source != source || grant.target.is_some()
        });
    }
    pub fn release(&mut self, session: SessionId) {
        self.entries
            .retain(|grant| grant.source.session != session || grant.target.is_some());
    }
    pub fn invalidate(&mut self, handle: Handle) {
        self.entries.retain(|grant| grant.handle != handle);
    }
    pub fn len(&self) -> usize {
        self.entries.len()
    }
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn prepare(grants: &mut Grants, handle: Handle, source: Owner) -> InheritanceToken {
        match grants.prepare(handle, source, 1).unwrap() {
            Reply::Inheritance { token, .. } => token,
            _ => panic!("Expected inheritance grant"),
        }
    }

    #[test]
    fn abort_preview_is_authorized_and_does_not_consume_the_grant() {
        let mut grants = Grants::default();
        let source = Owner::new(SessionId(1), ProcessId(2));
        let token = prepare(&mut grants, Handle(7), source);
        let impostor = Owner::new(SessionId(3), ProcessId(2));
        assert!(grants.abort_candidate(token, impostor).is_err());
        assert!(grants.abort(token, impostor).is_err());
        assert_eq!(grants.len(), 1);
        let preview = grants.abort_candidate(token, source).unwrap();
        assert_eq!(preview.handle, Handle(7));
        assert_eq!(preview.flags, 1);
        assert_eq!(grants.len(), 1);
        grants.abort(token, source).unwrap();
        assert!(grants.abort_candidate(token, source).is_err());
    }

    #[test]
    fn retention_preview_counts_only_other_committed_grants_for_the_handle() {
        let mut grants = Grants::default();
        let source = Owner::new(SessionId(1), ProcessId(2));
        let first = prepare(&mut grants, Handle(7), source);
        let second = prepare(&mut grants, Handle(7), source);
        let unrelated = prepare(&mut grants, Handle(8), source);
        grants.commit(first, source, ProcessId(3)).unwrap();
        grants.commit(unrelated, source, ProcessId(3)).unwrap();
        assert!(grants.retains(Handle(7)));
        assert!(!grants.retains_except(Handle(7), first));
        assert!(grants.retains_except(Handle(7), second));
        grants.commit(second, source, ProcessId(3)).unwrap();
        assert!(grants.retains_except(Handle(7), first));
        grants.abort(second, source).unwrap();
        assert!(!grants.retains_except(Handle(7), first));
    }

    #[test]
    fn exhaustion_does_not_wrap_or_publish_a_grant() {
        let mut grants = Grants {
            entries: Vec::new(),
            next_token: i64::MAX as u64 + 1,
        };
        assert_eq!(
            grants.prepare(Handle(1), Owner::new(SessionId(1), ProcessId(1)), 1),
            Err(IoError(IO_REGISTER_FAILED))
        );
        assert!(grants.is_empty());
    }
}
