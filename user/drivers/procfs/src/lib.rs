#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use core::fmt::{self, Write};
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::inheritance::{Grants, Owner};
use hunde::io::{
    Credentials, DirectoryEntry, FileInfo, GroupId, Handle, Operation, ProcessId, Reply, Request,
    SessionId, UserId,
};
use hunde::process::{Latency, Snapshot};
use hunde::protocol::*;

#[cfg(feature = "component")]
hunde::io_component!(Procfs, b"Rust procfs component panicked");

const SNAPSHOT_CAPACITY: usize = 12 * 1024;
const LEAVES: [&[u8]; 5] = [b"status", b"cpu", b"memory", b"queue-wait", b"ipc"];

trait Source {
    fn find(&mut self, pid: ProcessId) -> Result<Option<Snapshot>, IoError>;
    fn at(&mut self, index: u64) -> Result<Option<Snapshot>, IoError>;
    fn trace(&mut self, destination: &mut [u8]) -> Result<usize, IoError>;
}

struct Registry;
impl Source for Registry {
    fn find(&mut self, pid: ProcessId) -> Result<Option<Snapshot>, IoError> {
        hunde::process::find(pid)
    }
    fn at(&mut self, index: u64) -> Result<Option<Snapshot>, IoError> {
        hunde::process::at(index)
    }
    fn trace(&mut self, destination: &mut [u8]) -> Result<usize, IoError> {
        hunde::process::kernel_trace(destination)
    }
}

struct Writer(Vec<u8>);
impl Writer {
    fn new() -> Self {
        Self(Vec::new())
    }
    fn bytes(&mut self, bytes: &[u8]) -> Result<(), IoError> {
        if bytes.len() > SNAPSHOT_CAPACITY - self.0.len() {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        self.0
            .try_reserve(bytes.len())
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        self.0.extend_from_slice(bytes);
        Ok(())
    }
    fn line(&mut self, key: &str, value: u64) -> Result<(), IoError> {
        writeln!(self, "{key}={value}").map_err(|_| IoError(IO_REGISTER_FAILED))
    }
    fn latency(&mut self, name: &str, latency: Latency) -> Result<(), IoError> {
        for (percentile, value) in [
            (50, latency.p50_usec),
            (70, latency.p70_usec),
            (90, latency.p90_usec),
            (99, latency.p99_usec),
        ] {
            writeln!(self, "block_{name}_usec_p{percentile}={value}")
                .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        }
        Ok(())
    }
}
impl Write for Writer {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        self.bytes(text.as_bytes()).map_err(|_| fmt::Error)
    }
}

fn parse(path: &[u8]) -> Result<(ProcessId, &[u8]), IoError> {
    let relative = path.strip_prefix(b"/").ok_or(IoError(IO_INVALID_PATH))?;
    let (number, leaf) = match relative.iter().position(|byte| *byte == b'/') {
        Some(index) => (&relative[..index], &relative[index + 1..]),
        None => (relative, &[][..]),
    };
    if number.is_empty() || (number.len() > 1 && number[0] == b'0') || path.ends_with(b"/") {
        return Err(IoError(IO_INVALID_PATH));
    }
    let pid = number
        .iter()
        .try_fold(0u64, |value, digit| {
            if !digit.is_ascii_digit() {
                return None;
            }
            value.checked_mul(10)?.checked_add((digit - b'0') as u64)
        })
        .ok_or(IoError(IO_INVALID_PATH))?;
    Ok((ProcessId(pid), leaf))
}

fn find(source: &mut impl Source, pid: ProcessId) -> Result<Snapshot, IoError> {
    source.find(pid)?.ok_or(IoError(IO_INVALID_PATH))
}

fn snapshot(source: &mut impl Source, path: &[u8]) -> Result<Vec<u8>, IoError> {
    if path == b"/ktrace" {
        let mut bytes = Vec::new();
        bytes
            .try_reserve_exact(SNAPSHOT_CAPACITY)
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        bytes.resize(SNAPSHOT_CAPACITY, 0);
        let size = source.trace(&mut bytes)?;
        if size > bytes.len() {
            return Err(IoError(IO_UNKNOWN_BUFFER));
        }
        bytes.truncate(size);
        return Ok(bytes);
    }
    let (pid, leaf) = parse(path)?;
    let record = find(source, pid)?;
    let mut writer = Writer::new();
    match leaf {
        b"status" => {
            writer.line("pid", record.pid.0)?;
            writer.line("uid", record.owner.user.0)?;
            writer.line("gid", record.owner.group.0)?;
            for (key, value) in [
                (b"state=".as_slice(), record.state.as_slice()),
                (b"wait=", record.wait.as_slice()),
                (b"class=", record.class_name.as_slice()),
            ] {
                writer.bytes(key)?;
                writer.bytes(value)?;
                writer.bytes(b"\n")?;
            }
            writer.line("cpu", record.cpu)?;
            writer.line("sampled_at_usec", record.sampled_at_usec)?;
            writer.bytes(b"name=")?;
            writer.bytes(&record.name)?;
            writer.bytes(b"\n")?;
        }
        b"cpu" => {
            let usage = record.cpu_usage;
            writer.line("cpu", record.cpu)?;
            writer.line("total_usec", usage.total_usec)?;
            for (key, value) in [
                ("CPU% (1s)", usage.long_basis_points),
                ("CPU% (100ms)", usage.short_basis_points),
            ] {
                writeln!(writer, "{key}={}.{:02}", value / 100, value % 100)
                    .map_err(|_| IoError(IO_REGISTER_FAILED))?;
            }
            writer.line("scheduled_usec", usage.scheduled_usec)?;
            for (key, value) in [
                ("SCHED CPU% (1s)", usage.scheduled_long_basis_points),
                ("SCHED CPU% (100ms)", usage.scheduled_short_basis_points),
            ] {
                writeln!(writer, "{key}={}.{:02}", value / 100, value % 100)
                    .map_err(|_| IoError(IO_REGISTER_FAILED))?;
            }
            writer.line("sampled_at_usec", record.sampled_at_usec)?;
        }
        b"queue-wait" => {
            let usage = record.queue_wait;
            writer.line("parks", usage.parks)?;
            writer.line("activity_wakes", usage.activity_wakes)?;
            writer.line("timeout_wakes", usage.timeout_wakes)?;
            writer.line("selections", usage.selections)?;
            writer.line("speculative_selections", usage.speculative_selections)?;
            writer.line("parked_usec", usage.parked_usec)?;
            writer.line("max_parked_usec", usage.max_parked_usec)?;
            writer.line("runnable_usec", usage.runnable_usec)?;
            writer.line("max_runnable_usec", usage.max_runnable_usec)?;
            writer.line("sampled_at_usec", record.sampled_at_usec)?;
        }
        b"memory" => match record.memory {
            Some(memory) => {
                writer.line("private_bytes", memory.private_bytes)?;
                writer.line("shared_bytes", memory.shared_bytes)?;
                writer.line("device_bytes", memory.device_bytes)?;
                writer.line("sampled_at_usec", record.sampled_at_usec)?;
            }
            None => writer.bytes(b"accounting=unavailable\n")?,
        },
        b"ipc" => match record.diagnostics {
            Some(usage) => {
                writer.line("uart_bytes", usage.uart_bytes)?;
                writer.line("uart_irqs", usage.uart_irqs)?;
                writer.line("uart_reads", usage.uart_reads)?;
                writer.line("uart_empty", usage.uart_empty)?;
                writer.line("backend_reads", usage.backend_reads)?;
                writer.line("backend_empty", usage.backend_empty)?;
                writer.line("block_submissions", usage.block_submissions)?;
                writer.line("block_blocks", usage.block_blocks)?;
                writer.line("block_failures", usage.block_failures)?;
                writer.line("block_cache_hits", usage.block_cache_hits)?;
                writer.line("block_cache_misses", usage.block_cache_misses)?;
                writer.line("block_cache_evictions", usage.block_cache_evictions)?;
                writer.line("inode_cache_hits", usage.inode_cache_hits)?;
                writer.line("inode_cache_misses", usage.inode_cache_misses)?;
                writer.line("inode_cache_evictions", usage.inode_cache_evictions)?;
                writer.latency("device", usage.block_device)?;
                writer.line(
                    "block_segmented_submissions",
                    usage.block_segmented_submissions,
                )?;
                writer.latency("device_irq", usage.block_device_irq)?;
                writer.latency("manager_wake", usage.block_manager_wake)?;
                writer.latency("irq_relay", usage.block_irq_relay)?;
                writer.latency("fiber_wake", usage.block_fiber_wake)?;
                writer.latency("completion", usage.block_completion)?;
                writer.line("wait_entries", usage.wait_entries)?;
                writer.line("unchanged_wakes", usage.unchanged_wakes)?;
                writer.line("user_wait_usec", record.user_wait_usec)?;
                writer.line("sampled_at_usec", record.sampled_at_usec)?;
            }
            None => writer.bytes(b"diagnostics=unavailable\n")?,
        },
        _ => return Err(IoError(IO_INVALID_PATH)),
    }
    Ok(writer.0)
}

fn root_owner() -> Credentials {
    Credentials::new(UserId(0), GroupId(0))
}
fn file_info(owner: Credentials) -> FileInfo {
    FileInfo::new(IO_MODE_TYPE_FILE as u32 | 0o444, owner, 0, 0)
}
fn directory_info(owner: Credentials) -> FileInfo {
    FileInfo::new(IO_MODE_TYPE_DIR as u32 | 0o555, owner, 0, 0)
}

fn list(
    source: &mut impl Source,
    path: &[u8],
    offset: u64,
) -> Result<Vec<DirectoryEntry>, IoError> {
    let mut entries = Vec::new();
    if path.is_empty() || path == b"/" {
        let mut index = offset;
        if index == 0 {
            entries.push(DirectoryEntry::new(file_info(root_owner()), b"ktrace")?);
            index += 1;
        }
        while entries.len() < IO_DIRECTORY_BATCH_CAPACITY as usize {
            let Some(record) = source.at(index - 1)? else {
                break;
            };
            let name = alloc::format!("{}", record.pid.0);
            entries.push(DirectoryEntry::new(
                directory_info(record.owner),
                name.as_bytes(),
            )?);
            index = index.checked_add(1).ok_or(IoError(IO_INVALID_PATH))?;
        }
        return Ok(entries);
    }
    let (pid, leaf) = parse(path)?;
    if !leaf.is_empty() {
        return Err(IoError(IO_INVALID_PATH));
    }
    let record = find(source, pid)?;
    for name in LEAVES
        .iter()
        .skip(usize::try_from(offset).unwrap_or(usize::MAX))
        .take(IO_DIRECTORY_BATCH_CAPACITY as usize)
    {
        entries.push(DirectoryEntry::new(file_info(record.owner), name)?);
    }
    Ok(entries)
}

fn info(source: &mut impl Source, path: &[u8]) -> Result<FileInfo, IoError> {
    if path.is_empty() || path == b"/" {
        return Ok(directory_info(root_owner()));
    }
    if path == b"/ktrace" {
        return Ok(file_info(root_owner()));
    }
    let (pid, leaf) = parse(path)?;
    let record = find(source, pid)?;
    if leaf.is_empty() {
        return Ok(directory_info(record.owner));
    }
    if LEAVES.contains(&leaf) {
        return Ok(file_info(record.owner));
    }
    Err(IoError(IO_INVALID_PATH))
}

struct Description {
    handle: Handle,
    owners: Vec<Owner>,
    bytes: Vec<u8>,
    offset: u64,
}

impl Description {
    fn new(handle: Handle, owner: Owner, bytes: Vec<u8>) -> Result<Self, IoError> {
        let mut owners = Vec::new();
        owners
            .try_reserve_exact(1)
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        owners.push(owner);
        Ok(Self {
            handle,
            owners,
            bytes,
            offset: 0,
        })
    }
}

pub struct Procfs {
    descriptions: Vec<Description>,
    grants: Grants,
    next_handle: i64,
}

impl Default for Procfs {
    fn default() -> Self {
        Self {
            descriptions: Vec::new(),
            grants: Grants::default(),
            next_handle: 1 << 32,
        }
    }
}

impl Procfs {
    pub fn new() -> Self {
        Self::default()
    }
    fn reap(&mut self) {
        self.descriptions.retain(|description| {
            !description.owners.is_empty() || self.grants.retains(description.handle)
        });
    }
    fn execute(
        &mut self,
        request: Request<'_>,
        source: &mut impl Source,
    ) -> Result<Reply, IoError> {
        if request.pid.0 == 0 || request.session.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let owner = Owner::new(request.session, request.pid);
        match request.operation {
            Operation::Open(path, flags) => {
                if flags
                    & !(IO_OPEN_READ
                        | IO_OPEN_WRITE
                        | IO_OPEN_EXECUTE
                        | IO_OPEN_CREATE
                        | IO_OPEN_EXCLUSIVE)
                    != 0
                    || flags & (IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE) == 0
                {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                if flags & (IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE) != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                if flags & (IO_OPEN_WRITE | IO_OPEN_EXECUTE) != 0 {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if path.len() >= IO_DIRENT_NAME_MAX as usize || path.contains(&0) {
                    return Err(IoError(IO_INVALID_PATH));
                }
                if self.descriptions.len() == 32 || self.next_handle == i64::MAX {
                    return Err(IoError(IO_REGISTER_FAILED));
                }
                let bytes = snapshot(source, path)?;
                self.descriptions
                    .try_reserve(1)
                    .map_err(|_| IoError(IO_REGISTER_FAILED))?;
                let handle = Handle(self.next_handle);
                let description = Description::new(handle, owner, bytes)?;
                self.next_handle = handle.0 + 1;
                self.descriptions.push(description);
                return Ok(Reply::Value(handle.0));
            }
            Operation::ListDirectory(path, offset) => {
                return list(source, path, offset).map(Reply::Directory);
            }
            Operation::FileInfo(path) => return info(source, path).map(Reply::FileInfo),
            Operation::Mkdir(_) | Operation::Remove(_, _) => {
                return Err(IoError(IO_PERMISSION_DENIED));
            }
            Operation::Release => {
                self.disconnect(request.session)?;
                return Ok(Reply::Value(0));
            }
            Operation::InheritCommit { token, target } => {
                self.grants.commit(token, owner, target)?;
                return Ok(Reply::Value(0));
            }
            Operation::InheritAbort(token) => {
                self.grants.abort(token, owner)?;
                self.reap();
                return Ok(Reply::Value(0));
            }
            Operation::InheritClaim { token, flags } => {
                let grant = self.grants.claim(token, owner, flags)?;
                let description = self
                    .descriptions
                    .iter_mut()
                    .find(|entry| entry.handle == grant.handle)
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                if !description.owners.contains(&owner) {
                    if description.owners.len() == 64 {
                        return Err(IoError(IO_REGISTER_FAILED));
                    }
                    description
                        .owners
                        .try_reserve(1)
                        .map_err(|_| IoError(IO_REGISTER_FAILED))?;
                    description.owners.push(owner);
                }
                self.grants.complete(token)?;
                return Ok(Reply::Value(grant.handle.0));
            }
            _ => {}
        }
        let index = self
            .descriptions
            .iter()
            .position(|entry| entry.handle == request.handle && entry.owners.contains(&owner))
            .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
        let description = &mut self.descriptions[index];
        match request.operation {
            Operation::Read(target) => {
                if request.transfer_flags & !IO_TRANSFER_NONBLOCK != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                let offset = usize::try_from(description.offset)
                    .unwrap_or(usize::MAX)
                    .min(description.bytes.len());
                let count = target.len().min(description.bytes.len() - offset);
                target[..count].copy_from_slice(&description.bytes[offset..offset + count]);
                description.offset += count as u64;
                Ok(Reply::Value(count as i64))
            }
            Operation::Write(_) => Err(IoError(IO_PERMISSION_DENIED)),
            Operation::Seek { offset, whence } => {
                let base = match whence {
                    IO_SEEK_SET => 0,
                    IO_SEEK_CURRENT => description.offset as i64,
                    IO_SEEK_END => description.bytes.len() as i64,
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                };
                let position = base
                    .checked_add(offset)
                    .filter(|value| *value >= 0)
                    .ok_or(IoError(IO_OPERATION_NOT_SUPPORTED))?;
                description.offset = position as u64;
                Ok(Reply::Value(position))
            }
            Operation::InheritPrepare => {
                self.grants.prepare(description.handle, owner, IO_OPEN_READ)
            }
            Operation::Close => {
                description.owners.retain(|entry| *entry != owner);
                self.grants.close(request.handle, owner);
                self.reap();
                Ok(Reply::Value(0))
            }
            _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
    }
}

impl IoComponent for Procfs {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.execute(request, &mut Registry).map(Some)
    }
    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.grants.release(session);
        for description in &mut self.descriptions {
            description.owners.retain(|owner| owner.session != session);
        }
        self.reap();
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;
    use hunde::io::InheritanceToken;

    struct Fake {
        records: Vec<Snapshot>,
        error: Option<IoError>,
        trace_size: usize,
    }
    impl Fake {
        fn new() -> Self {
            let mut record = Snapshot::new(
                ProcessId(42),
                Credentials::new(UserId(1 << 40), GroupId(2)),
                b"test\xffprocess",
            );
            record.cpu = 3;
            record.sampled_at_usec = 1000000;
            record.state = b"WAITING".into();
            record.wait = b"IO".into();
            record.class_name = b"normal".into();
            record.cpu_usage.total_usec = 104900;
            record.cpu_usage.long_basis_points = 1049;
            record.cpu_usage.short_basis_points = 2500;
            record.cpu_usage.scheduled_usec = 120000;
            record.cpu_usage.scheduled_long_basis_points = 1200;
            record.cpu_usage.scheduled_short_basis_points = 3000;
            record.queue_wait.parks = 5;
            Self {
                records: vec![record],
                error: None,
                trace_size: 6,
            }
        }
    }
    impl Source for Fake {
        fn find(&mut self, pid: ProcessId) -> Result<Option<Snapshot>, IoError> {
            if let Some(error) = self.error {
                return Err(error);
            }
            Ok(self
                .records
                .iter()
                .find(|record| record.pid == pid)
                .cloned())
        }
        fn at(&mut self, index: u64) -> Result<Option<Snapshot>, IoError> {
            if let Some(error) = self.error {
                return Err(error);
            }
            Ok(usize::try_from(index)
                .ok()
                .and_then(|index| self.records.get(index))
                .cloned())
        }
        fn trace(&mut self, destination: &mut [u8]) -> Result<usize, IoError> {
            if let Some(error) = self.error {
                return Err(error);
            }
            destination[..6].copy_from_slice(b"trace\n");
            Ok(self.trace_size)
        }
    }
    fn request(handle: i64, operation: Operation<'_>) -> Request<'_> {
        Request::new(SessionId(10), ProcessId(20), Handle(handle), operation)
    }
    fn open(driver: &mut Procfs, source: &mut Fake, path: &[u8]) -> i64 {
        match driver
            .execute(request(0, Operation::Open(path, IO_OPEN_READ)), source)
            .unwrap()
        {
            Reply::Value(handle) => handle,
            _ => panic!("Expected handle"),
        }
    }
    fn contains(bytes: &[u8], expected: &[u8]) -> bool {
        bytes
            .windows(expected.len())
            .any(|window| window == expected)
    }
    #[test]
    fn formats_all_process_files_without_losing_names_or_accounting() {
        let mut source = Fake::new();
        let status = snapshot(&mut source, b"/42/status").unwrap();
        assert!(contains(&status, b"uid=1099511627776\n"));
        assert!(contains(&status, b"state=WAITING\nwait=IO\nclass=normal\n"));
        assert!(status.ends_with(b"name=test\xffprocess\n"));
        assert_eq!(snapshot(&mut source, b"/42/cpu").unwrap(), b"cpu=3\ntotal_usec=104900\nCPU% (1s)=10.49\nCPU% (100ms)=25.00\nscheduled_usec=120000\nSCHED CPU% (1s)=12.00\nSCHED CPU% (100ms)=30.00\nsampled_at_usec=1000000\n");
        assert!(contains(
            &snapshot(&mut source, b"/42/queue-wait").unwrap(),
            b"parks=5\n"
        ));
        assert_eq!(
            snapshot(&mut source, b"/42/memory").unwrap(),
            b"accounting=unavailable\n"
        );
        assert_eq!(
            snapshot(&mut source, b"/42/ipc").unwrap(),
            b"diagnostics=unavailable\n"
        );
        source.records[0].memory = Some(hunde::process::MemoryUsage {
            private_bytes: 8192,
            shared_bytes: 4096,
            device_bytes: 0,
        });
        source.records[0].diagnostics = Some(hunde::process::Diagnostics::default());
        assert_eq!(
            snapshot(&mut source, b"/42/memory").unwrap(),
            b"private_bytes=8192\nshared_bytes=4096\ndevice_bytes=0\nsampled_at_usec=1000000\n"
        );
        let ipc = snapshot(&mut source, b"/42/ipc").unwrap();
        assert!(contains(&ipc, b"block_device_usec_p99=0\n"));
        assert!(contains(&ipc, b"block_irq_relay_usec_p70=0\n"));
        assert!(contains(&ipc, b"inode_cache_evictions=0\n"));
        assert!(ipc.ends_with(b"user_wait_usec=0\nsampled_at_usec=1000000\n"));
        assert_eq!(snapshot(&mut source, b"/ktrace").unwrap(), b"trace\n");
    }
    #[test]
    fn directories_are_live_paged_and_carry_process_credentials() {
        let mut source = Fake::new();
        for pid in 43..48 {
            source
                .records
                .push(Snapshot::new(ProcessId(pid), root_owner(), b"other"));
        }
        let entries = list(&mut source, b"/", 0).unwrap();
        assert_eq!(entries.len(), 3);
        assert_eq!(entries[0].name(), b"ktrace");
        assert_eq!(entries[1].name(), b"42");
        assert_eq!(entries[1].info, directory_info(source.records[0].owner));
        assert_eq!(list(&mut source, b"/", 3).unwrap()[0].name(), b"44");
        assert!(list(&mut source, b"/", u64::MAX).unwrap().is_empty());
        assert_eq!(
            list(&mut source, b"/42", 0)
                .unwrap()
                .iter()
                .map(|entry| entry.name())
                .collect::<Vec<_>>(),
            LEAVES[..3]
        );
        assert_eq!(
            list(&mut source, b"/42", 3)
                .unwrap()
                .iter()
                .map(|entry| entry.name())
                .collect::<Vec<_>>(),
            LEAVES[3..]
        );
        assert_eq!(
            info(&mut source, b"/42/status").unwrap(),
            file_info(source.records[0].owner)
        );
        source.records.clear();
        assert_eq!(info(&mut source, b"/42"), Err(IoError(IO_INVALID_PATH)));
    }
    #[test]
    fn paths_flags_and_readonly_operations_fail_closed() {
        let mut driver = Procfs::new();
        let mut source = Fake::new();
        for (session, pid) in [(0, 20), (10, 0)] {
            assert_eq!(
                driver.execute(
                    Request::new(
                        SessionId(session),
                        ProcessId(pid),
                        Handle(0),
                        Operation::FileInfo(b"/")
                    ),
                    &mut source
                ),
                Err(IoError(IO_PERMISSION_DENIED))
            );
        }
        for path in [
            b"42/status".as_slice(),
            b"/042/status",
            b"/+42/status",
            b"/42/",
            b"/42/status/",
            b"/18446744073709551616/status",
            b"/42/unknown",
            b"/42/status\0",
        ] {
            assert_eq!(
                driver.execute(request(0, Operation::Open(path, IO_OPEN_READ)), &mut source),
                Err(IoError(IO_INVALID_PATH))
            );
        }
        for flags in [
            IO_OPEN_WRITE,
            IO_OPEN_EXECUTE,
            IO_OPEN_READ | IO_OPEN_WRITE,
            IO_OPEN_READ | IO_OPEN_EXECUTE,
        ] {
            assert_eq!(
                driver.execute(
                    request(0, Operation::Open(b"/42/status", flags)),
                    &mut source
                ),
                Err(IoError(IO_PERMISSION_DENIED))
            );
        }
        for flags in [0, IO_OPEN_CREATE | IO_OPEN_WRITE, IO_OPEN_READ | (1 << 63)] {
            assert_eq!(
                driver.execute(
                    request(0, Operation::Open(b"/42/status", flags)),
                    &mut source
                ),
                Err(IoError(IO_OPERATION_NOT_SUPPORTED))
            );
        }
        assert_eq!(
            driver.execute(request(0, Operation::Mkdir(b"/new")), &mut source),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            driver.execute(request(0, Operation::Remove(b"/42", 0)), &mut source),
            Err(IoError(IO_PERMISSION_DENIED))
        );
    }
    #[test]
    fn open_snapshots_survive_exit_and_authenticate_each_owner() {
        let mut driver = Procfs::new();
        let mut source = Fake::new();
        let handle = open(&mut driver, &mut source, b"/42/status");
        let other = open(&mut driver, &mut source, b"/42/status");
        source.records.clear();
        let mut bytes = [0; 4];
        assert_eq!(
            driver.execute(request(handle, Operation::Read(&mut bytes)), &mut source),
            Ok(Reply::Value(4))
        );
        assert_eq!(&bytes, b"pid=");
        assert_eq!(
            driver.execute(
                Request::new(
                    SessionId(11),
                    ProcessId(20),
                    Handle(handle),
                    Operation::Close
                ),
                &mut source
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            driver.execute(
                Request::new(
                    SessionId(10),
                    ProcessId(21),
                    Handle(handle),
                    Operation::Read(&mut bytes)
                ),
                &mut source
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            driver.execute(request(other, Operation::Read(&mut bytes)), &mut source),
            Ok(Reply::Value(4))
        );
        assert_eq!(&bytes, b"pid=");
        assert_eq!(
            driver.execute(
                request(
                    handle,
                    Operation::Seek {
                        offset: -1,
                        whence: IO_SEEK_SET
                    }
                ),
                &mut source
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(
            driver.execute(
                request(
                    handle,
                    Operation::Seek {
                        offset: i64::MAX,
                        whence: IO_SEEK_SET
                    }
                ),
                &mut source
            ),
            Ok(Reply::Value(i64::MAX))
        );
        assert_eq!(
            driver.execute(
                request(
                    handle,
                    Operation::Seek {
                        offset: 1,
                        whence: IO_SEEK_CURRENT
                    }
                ),
                &mut source
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(
            driver.execute(request(handle, Operation::Read(&mut bytes)), &mut source),
            Ok(Reply::Value(0))
        );
        driver.disconnect(SessionId(10)).unwrap();
        assert!(driver.descriptions.is_empty());
    }
    #[test]
    fn inheritance_is_pid_bound_single_use_and_shares_snapshot_offset() {
        let mut driver = Procfs::new();
        let mut source = Fake::new();
        let handle = open(&mut driver, &mut source, b"/ktrace");
        let Reply::Inheritance { token, flags } = driver
            .execute(request(handle, Operation::InheritPrepare), &mut source)
            .unwrap()
        else {
            panic!("Expected grant");
        };
        driver
            .execute(
                request(
                    handle,
                    Operation::InheritCommit {
                        token,
                        target: ProcessId(21),
                    },
                ),
                &mut source,
            )
            .unwrap();
        assert_eq!(
            driver.execute(
                Request::new(
                    SessionId(11),
                    ProcessId(22),
                    Handle(0),
                    Operation::InheritClaim { token, flags }
                ),
                &mut source
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        let child = || {
            Request::new(
                SessionId(11),
                ProcessId(21),
                Handle(0),
                Operation::InheritClaim { token, flags },
            )
        };
        assert_eq!(
            driver.execute(child(), &mut source),
            Ok(Reply::Value(handle))
        );
        assert_eq!(
            driver.execute(child(), &mut source),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        let mut bytes = [0; 2];
        driver
            .execute(request(handle, Operation::Read(&mut bytes)), &mut source)
            .unwrap();
        assert_eq!(&bytes, b"tr");
        driver.disconnect(SessionId(10)).unwrap();
        driver
            .execute(
                Request::new(
                    SessionId(11),
                    ProcessId(21),
                    Handle(handle),
                    Operation::Read(&mut bytes),
                ),
                &mut source,
            )
            .unwrap();
        assert_eq!(&bytes, b"ac");
        driver.disconnect(SessionId(11)).unwrap();
        assert!(driver.descriptions.is_empty());
        assert_eq!(
            driver.execute(
                request(0, Operation::InheritAbort(InheritanceToken(0))),
                &mut source
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }
    #[test]
    fn committed_grants_retain_snapshots_and_uncommitted_grants_do_not() {
        let mut driver = Procfs::new();
        let mut source = Fake::new();
        let handle = open(&mut driver, &mut source, b"/ktrace");
        let Reply::Inheritance { token, flags } = driver
            .execute(request(handle, Operation::InheritPrepare), &mut source)
            .unwrap()
        else {
            panic!("Expected grant");
        };
        driver
            .execute(
                request(
                    handle,
                    Operation::InheritCommit {
                        token,
                        target: ProcessId(21),
                    },
                ),
                &mut source,
            )
            .unwrap();
        driver.disconnect(SessionId(10)).unwrap();
        assert_eq!(driver.descriptions.len(), 1);
        driver
            .execute(
                Request::new(
                    SessionId(11),
                    ProcessId(21),
                    Handle(0),
                    Operation::InheritClaim { token, flags },
                ),
                &mut source,
            )
            .unwrap();
        driver.disconnect(SessionId(11)).unwrap();
        assert!(driver.descriptions.is_empty());
        let handle = open(&mut driver, &mut source, b"/ktrace");
        driver
            .execute(request(handle, Operation::InheritPrepare), &mut source)
            .unwrap();
        driver.disconnect(SessionId(10)).unwrap();
        assert!(driver.descriptions.is_empty());
        assert!(driver.grants.is_empty());
    }
    #[test]
    fn failures_and_exhaustion_do_not_publish_partial_handles() {
        let mut driver = Procfs::new();
        let mut source = Fake::new();
        source.error = Some(IoError(IO_WAIT_FOR_DATA));
        assert_eq!(
            driver.execute(
                request(0, Operation::Open(b"/42/status", IO_OPEN_READ)),
                &mut source
            ),
            Err(IoError(IO_WAIT_FOR_DATA))
        );
        assert_eq!(list(&mut source, b"/", 0), Err(IoError(IO_WAIT_FOR_DATA)));
        assert!(driver.descriptions.is_empty());
        source.error = None;
        source.trace_size = SNAPSHOT_CAPACITY + 1;
        assert_eq!(
            driver.execute(
                request(0, Operation::Open(b"/ktrace", IO_OPEN_READ)),
                &mut source
            ),
            Err(IoError(IO_UNKNOWN_BUFFER))
        );
        source.trace_size = 6;
        for _ in 0..32 {
            open(&mut driver, &mut source, b"/ktrace");
        }
        assert_eq!(
            driver.execute(
                request(0, Operation::Open(b"/ktrace", IO_OPEN_READ)),
                &mut source
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
        driver.disconnect(SessionId(10)).unwrap();
        driver.next_handle = i64::MAX;
        assert_eq!(
            driver.execute(
                request(0, Operation::Open(b"/ktrace", IO_OPEN_READ)),
                &mut source
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
    }
}
