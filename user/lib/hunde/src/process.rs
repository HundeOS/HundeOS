use crate::abi::AbiError;
#[cfg(feature = "alloc")]
use crate::abi::{ERROR, IoError, SUCCESS};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CpuUsage {
    pub total_usec: u64,
    pub user_usec: u64,
    pub system_usec: u64,
    pub short_basis_points: u64,
    pub long_basis_points: u64,
    pub scheduled_usec: u64,
    pub scheduled_short_basis_points: u64,
    pub scheduled_long_basis_points: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct QueueWait {
    pub parks: u64,
    pub activity_wakes: u64,
    pub timeout_wakes: u64,
    pub selections: u64,
    pub speculative_selections: u64,
    pub parked_usec: u64,
    pub max_parked_usec: u64,
    pub runnable_usec: u64,
    pub max_runnable_usec: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MemoryUsage {
    pub private_bytes: u64,
    pub shared_bytes: u64,
    pub device_bytes: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Latency {
    pub p50_usec: u64,
    pub p70_usec: u64,
    pub p90_usec: u64,
    pub p99_usec: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Diagnostics {
    pub uart_bytes: u64,
    pub uart_irqs: u64,
    pub uart_reads: u64,
    pub uart_empty: u64,
    pub backend_reads: u64,
    pub backend_empty: u64,
    pub wait_entries: u64,
    pub unchanged_wakes: u64,
    pub block_cache_hits: u64,
    pub block_cache_misses: u64,
    pub block_cache_evictions: u64,
    pub inode_cache_hits: u64,
    pub inode_cache_misses: u64,
    pub inode_cache_evictions: u64,
    pub block_submissions: u64,
    pub block_blocks: u64,
    pub block_failures: u64,
    pub block_segmented_submissions: u64,
    pub block_device: Latency,
    pub block_device_irq: Latency,
    pub block_manager_wake: Latency,
    pub block_irq_relay: Latency,
    pub block_fiber_wake: Latency,
    pub block_completion: Latency,
}

#[repr(C)]
#[derive(Clone)]
pub(crate) struct Text {
    pub length: u64,
    pub bytes: [u8; 1024],
}
impl Default for Text {
    fn default() -> Self {
        Self {
            length: 0,
            bytes: [0; 1024],
        }
    }
}
#[cfg(feature = "alloc")]
impl Text {
    fn bytes(&self) -> Result<&[u8], IoError> {
        let length =
            usize::try_from(self.length).map_err(|_| IoError(crate::protocol::IO_PANIC))?;
        self.bytes
            .get(..length)
            .ok_or(IoError(crate::protocol::IO_PANIC))
    }
}

#[repr(C)]
#[derive(Clone, Default)]
pub(crate) struct RawSnapshot {
    pub pid: u64,
    pub cpu: u64,
    pub sampled_at_usec: u64,
    pub user_id: u64,
    pub group_id: u64,
    pub user_wait_usec: u64,
    pub has_memory: u64,
    pub has_diagnostics: u64,
    pub cpu_usage: CpuUsage,
    pub queue_wait: QueueWait,
    pub memory: MemoryUsage,
    pub diagnostics: Diagnostics,
    pub name: Text,
    pub state: Text,
    pub wait: Text,
    pub class_name: Text,
}

#[repr(C)]
pub struct ProcessResult {
    pub(crate) kind: u32,
    pub(crate) found: u32,
    pub(crate) snapshot: RawSnapshot,
    pub(crate) error: AbiError,
}

#[cfg(feature = "alloc")]
#[derive(Clone)]
pub struct Snapshot {
    pub pid: crate::io::ProcessId,
    pub cpu: u64,
    pub sampled_at_usec: u64,
    pub owner: crate::io::Credentials,
    pub user_wait_usec: u64,
    pub cpu_usage: CpuUsage,
    pub queue_wait: QueueWait,
    pub memory: Option<MemoryUsage>,
    pub diagnostics: Option<Diagnostics>,
    pub name: alloc::vec::Vec<u8>,
    pub state: alloc::vec::Vec<u8>,
    pub wait: alloc::vec::Vec<u8>,
    pub class_name: alloc::vec::Vec<u8>,
}

#[cfg(feature = "alloc")]
impl ProcessResult {
    pub(crate) fn into_result(self) -> Result<Option<Snapshot>, IoError> {
        match (self.kind, self.found) {
            (SUCCESS, 0) => Ok(None),
            (SUCCESS, 1) => {
                for text in [
                    &self.snapshot.name,
                    &self.snapshot.state,
                    &self.snapshot.wait,
                    &self.snapshot.class_name,
                ] {
                    text.bytes()?;
                }
                if self.snapshot.has_memory > 1 || self.snapshot.has_diagnostics > 1 {
                    return Err(IoError(crate::protocol::IO_PANIC));
                }
                Ok(Some(Snapshot {
                    pid: crate::io::ProcessId(self.snapshot.pid),
                    cpu: self.snapshot.cpu,
                    sampled_at_usec: self.snapshot.sampled_at_usec,
                    owner: crate::io::Credentials::new(
                        crate::io::UserId(self.snapshot.user_id),
                        crate::io::GroupId(self.snapshot.group_id),
                    ),
                    user_wait_usec: self.snapshot.user_wait_usec,
                    cpu_usage: self.snapshot.cpu_usage,
                    queue_wait: self.snapshot.queue_wait,
                    memory: (self.snapshot.has_memory == 1).then_some(self.snapshot.memory),
                    diagnostics: (self.snapshot.has_diagnostics == 1)
                        .then_some(self.snapshot.diagnostics),
                    name: self.snapshot.name.bytes()?.into(),
                    state: self.snapshot.state.bytes()?.into(),
                    wait: self.snapshot.wait.bytes()?.into(),
                    class_name: self.snapshot.class_name.bytes()?.into(),
                }))
            }
            (ERROR, _) => Err(IoError(self.error.code)),
            _ => Err(IoError(crate::protocol::IO_PANIC)),
        }
    }
}

#[cfg(feature = "alloc")]
impl Snapshot {
    pub fn new(pid: crate::io::ProcessId, owner: crate::io::Credentials, name: &[u8]) -> Self {
        Self {
            pid,
            owner,
            name: name.into(),
            cpu: 0,
            sampled_at_usec: 0,
            user_wait_usec: 0,
            cpu_usage: CpuUsage::default(),
            queue_wait: QueueWait::default(),
            memory: None,
            diagnostics: None,
            state: alloc::vec::Vec::new(),
            wait: alloc::vec::Vec::new(),
            class_name: alloc::vec::Vec::new(),
        }
    }
}

#[repr(C)]
pub struct TraceResult {
    pub(crate) kind: u32,
    pub(crate) reserved: u32,
    pub(crate) size: u64,
    pub(crate) error: AbiError,
}
#[cfg(feature = "alloc")]
impl TraceResult {
    fn into_result(self, capacity: usize) -> Result<usize, IoError> {
        match self.kind {
            SUCCESS => usize::try_from(self.size)
                .ok()
                .filter(|size| *size <= capacity)
                .ok_or(IoError(crate::protocol::IO_UNKNOWN_BUFFER)),
            ERROR => Err(IoError(self.error.code)),
            _ => Err(IoError(crate::protocol::IO_PANIC)),
        }
    }
}

#[cfg(feature = "alloc")]
pub fn find(pid: crate::io::ProcessId) -> Result<Option<Snapshot>, IoError> {
    lookup(pid.0, 0)
}
#[cfg(feature = "alloc")]
pub fn at(index: u64) -> Result<Option<Snapshot>, IoError> {
    lookup(index, 1)
}
#[cfg(feature = "alloc")]
fn lookup(selector: u64, by_index: u32) -> Result<Option<Snapshot>, IoError> {
    let operations = unsafe { &*crate::runtime::operations()? };
    let lookup = operations
        .process
        .ok_or(IoError(crate::protocol::IO_NO_DEVICE_FOUND))?;
    unsafe { lookup(selector, by_index) }.into_result()
}
#[cfg(feature = "alloc")]
pub fn kernel_trace(destination: &mut [u8]) -> Result<usize, IoError> {
    let operations = unsafe { &*crate::runtime::operations()? };
    let load = operations
        .trace
        .ok_or(IoError(crate::protocol::IO_NO_DEVICE_FOUND))?;
    unsafe { load(destination.as_mut_ptr(), destination.len() as u64) }
        .into_result(destination.len())
}

const _: () = {
    assert!(core::mem::size_of::<Text>() == 1032);
    assert!(core::mem::size_of::<RawSnapshot>() == 4688);
    assert!(core::mem::offset_of!(RawSnapshot, cpu_usage) == 64);
    assert!(core::mem::offset_of!(RawSnapshot, diagnostics) == 224);
    assert!(core::mem::offset_of!(RawSnapshot, name) == 560);
    assert!(core::mem::offset_of!(RawSnapshot, class_name) == 3656);
    assert!(core::mem::size_of::<ProcessResult>() == 4720);
    assert!(core::mem::size_of::<TraceResult>() == 40);
};

#[cfg(all(test, feature = "alloc"))]
mod tests {
    use super::*;
    use crate::abi::PENDING;
    use crate::protocol::IO_PANIC;

    fn result(snapshot: RawSnapshot) -> ProcessResult {
        ProcessResult {
            kind: SUCCESS,
            found: 1,
            snapshot,
            error: AbiError::new(0, b""),
        }
    }
    #[test]
    fn copies_full_width_credentials_and_non_utf8_process_names() {
        let mut raw = RawSnapshot {
            pid: 42,
            user_id: 1 << 40,
            group_id: 1 << 41,
            ..RawSnapshot::default()
        };
        raw.name.length = 2;
        raw.name.bytes[..2].copy_from_slice(b"x\xff");
        raw.memory.private_bytes = 8192;
        raw.has_memory = 1;
        let record = result(raw).into_result().unwrap().unwrap();
        assert_eq!(record.pid, crate::io::ProcessId(42));
        assert_eq!(record.owner.user.0, 1 << 40);
        assert_eq!(record.owner.group.0, 1 << 41);
        assert_eq!(record.name, b"x\xff");
        assert_eq!(record.memory.unwrap().private_bytes, 8192);
        assert_eq!(record.diagnostics, None);
    }
    #[test]
    fn rejects_invalid_tags_lengths_and_optional_discriminants() {
        let mut raw = RawSnapshot::default();
        raw.name.length = 1025;
        assert!(matches!(result(raw).into_result(), Err(IoError(IO_PANIC))));
        let raw = RawSnapshot {
            has_memory: 2,
            ..RawSnapshot::default()
        };
        assert!(matches!(result(raw).into_result(), Err(IoError(IO_PANIC))));
        let mut invalid = result(RawSnapshot::default());
        invalid.found = 2;
        assert!(matches!(invalid.into_result(), Err(IoError(IO_PANIC))));
        let mut invalid = result(RawSnapshot::default());
        invalid.kind = PENDING;
        assert!(matches!(invalid.into_result(), Err(IoError(IO_PANIC))));
        let mut missing = result(RawSnapshot::default());
        missing.found = 0;
        assert!(missing.into_result().unwrap().is_none());
        let mut error = result(RawSnapshot::default());
        error.kind = ERROR;
        error.error.code = crate::protocol::IO_WAIT_FOR_DATA;
        assert!(matches!(
            error.into_result(),
            Err(IoError(crate::protocol::IO_WAIT_FOR_DATA))
        ));
    }
    #[test]
    fn trace_results_validate_size_and_error_tags() {
        let success = || TraceResult {
            kind: SUCCESS,
            reserved: 0,
            size: 9,
            error: AbiError::new(0, b""),
        };
        assert_eq!(success().into_result(9), Ok(9));
        assert_eq!(
            success().into_result(8),
            Err(IoError(crate::protocol::IO_UNKNOWN_BUFFER))
        );
        let mut error = success();
        error.kind = ERROR;
        error.error.code = crate::protocol::IO_NO_DEVICE_FOUND;
        assert_eq!(
            error.into_result(9),
            Err(IoError(crate::protocol::IO_NO_DEVICE_FOUND))
        );
        let mut error = success();
        error.kind = PENDING;
        assert_eq!(error.into_result(9), Err(IoError(IO_PANIC)));
    }
}
