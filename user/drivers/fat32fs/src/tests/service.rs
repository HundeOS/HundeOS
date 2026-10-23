use super::*;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{
    Credentials, GroupId, Handle, InheritanceToken, Operation, ProcessId, Reply, Request,
    SessionId, UserId,
};
use hunde::protocol::*;

fn request(session: u64, handle: Handle, operation: Operation<'_>) -> Request<'_> {
    Request::new(
        SessionId(session),
        ProcessId(100 + session),
        handle,
        operation,
    )
}

fn execute(
    service: &mut Fat32<Disk>,
    session: u64,
    handle: Handle,
    operation: Operation<'_>,
) -> Result<Option<Reply>, IoError> {
    service.execute(request(session, handle, operation), |_| {
        Ok(Credentials::new(UserId(0), GroupId(0)))
    })
}

fn open(service: &mut Fat32<Disk>, session: u64, path: &[u8], flags: u64) -> Handle {
    let Some(Reply::Value(handle)) =
        execute(service, session, Handle(0), Operation::Open(path, flags)).unwrap()
    else {
        panic!("Expected handle");
    };
    Handle(handle)
}

fn prepare(service: &mut Fat32<Disk>, handle: Handle) -> (InheritanceToken, u64) {
    let Some(Reply::Inheritance { token, flags }) =
        execute(service, 1, handle, Operation::InheritPrepare).unwrap()
    else {
        panic!("Expected grant");
    };
    (token, flags)
}

#[test]
fn handles_authenticate_both_process_and_session_and_close_once() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    let handle = open(&mut service, 1, b"/hello.txt", IO_OPEN_READ);
    for (session, pid) in [(2, 101), (1, 102), (0, 101), (1, 0)] {
        let error = service.execute(
            Request::new(
                SessionId(session),
                ProcessId(pid),
                handle,
                Operation::Seek {
                    offset: 0,
                    whence: IO_SEEK_CURRENT,
                },
            ),
            |_| panic!("Handle operation"),
        );
        assert_eq!(
            error,
            Err(IoError(if session == 0 || pid == 0 {
                IO_PERMISSION_DENIED
            } else {
                IO_UNKNOWN_HANDLE
            }))
        );
    }
    assert_eq!(
        execute(&mut service, 1, handle, Operation::Close),
        Ok(Some(Reply::Value(0)))
    );
    assert_eq!(
        execute(&mut service, 1, handle, Operation::Close),
        Err(IoError(IO_UNKNOWN_HANDLE))
    );
    assert_eq!(IoComponent::dispose(&mut service), Ok(()));
}

#[test]
fn offsets_are_independent_until_inherited_and_append_uses_current_eof() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    let first = open(&mut service, 1, b"/empty.txt", IO_OPEN_READ | IO_OPEN_WRITE);
    let second = open(&mut service, 2, b"/EMPTY.TXT", IO_OPEN_READ | IO_OPEN_WRITE);
    for (session, handle, data) in [
        (1, first, b"abc".as_slice()),
        (2, second, b"def".as_slice()),
    ] {
        assert_eq!(
            service.execute(
                request(session, handle, Operation::Write(data))
                    .with_transfer_flags(IO_TRANSFER_APPEND | IO_TRANSFER_NONBLOCK),
                |_| panic!("Handle operation")
            ),
            Ok(Some(Reply::Value(3)))
        );
    }
    assert_eq!(
        execute(
            &mut service,
            1,
            first,
            Operation::Seek {
                offset: 0,
                whence: IO_SEEK_CURRENT
            }
        ),
        Ok(Some(Reply::Value(3)))
    );
    assert_eq!(
        execute(
            &mut service,
            2,
            second,
            Operation::Seek {
                offset: -6,
                whence: IO_SEEK_END
            }
        ),
        Ok(Some(Reply::Value(0)))
    );
    let mut bytes = [0; 8];
    assert_eq!(
        execute(&mut service, 2, second, Operation::Read(&mut bytes)),
        Ok(Some(Reply::Value(6)))
    );
    assert_eq!(&bytes[..6], b"abcdef");
    assert_eq!(
        execute(&mut service, 2, second, Operation::Read(&mut bytes)),
        Ok(Some(Reply::Value(0)))
    );
    assert_eq!(
        execute(
            &mut service,
            2,
            second,
            Operation::Seek {
                offset: -1,
                whence: IO_SEEK_SET
            }
        ),
        Err(IoError(IO_OPERATION_NOT_SUPPORTED))
    );
}

#[test]
fn committed_grants_survive_disconnect_share_offsets_and_preserve_access() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    let handle = open(&mut service, 1, b"/hello.txt", IO_OPEN_READ);
    let (token, flags) = prepare(&mut service, handle);
    assert_eq!(
        execute(
            &mut service,
            2,
            handle,
            Operation::InheritCommit {
                token,
                target: ProcessId(102)
            }
        ),
        Err(IoError(IO_PERMISSION_DENIED))
    );
    execute(
        &mut service,
        1,
        handle,
        Operation::InheritCommit {
            token,
            target: ProcessId(102),
        },
    )
    .unwrap();
    execute(
        &mut service,
        1,
        handle,
        Operation::Seek {
            offset: 10,
            whence: IO_SEEK_SET,
        },
    )
    .unwrap();
    IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
    assert_eq!(
        IoComponent::dispose(&mut service),
        Err(IoError(IO_WOULD_BLOCK))
    );
    for (session, claimed_flags) in [(3, flags), (2, flags | IO_OPEN_WRITE)] {
        assert_eq!(
            execute(
                &mut service,
                session,
                Handle(0),
                Operation::InheritClaim {
                    token,
                    flags: claimed_flags
                }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
    }
    assert_eq!(
        execute(
            &mut service,
            2,
            Handle(0),
            Operation::InheritClaim { token, flags }
        ),
        Ok(Some(Reply::Value(handle.0)))
    );
    assert_eq!(
        execute(
            &mut service,
            2,
            handle,
            Operation::Seek {
                offset: 0,
                whence: IO_SEEK_CURRENT
            }
        ),
        Ok(Some(Reply::Value(10)))
    );
    assert_eq!(
        execute(&mut service, 2, handle, Operation::Write(b"denied")),
        Err(IoError(IO_PERMISSION_DENIED))
    );
    assert_eq!(
        execute(
            &mut service,
            2,
            Handle(0),
            Operation::InheritClaim { token, flags }
        ),
        Err(IoError(IO_UNKNOWN_HANDLE))
    );
    IoComponent::disconnect(&mut service, SessionId(2)).unwrap();
    assert_eq!(IoComponent::dispose(&mut service), Ok(()));
}

#[test]
fn live_inherited_owners_share_position_and_close_independently() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    let handle = open(&mut service, 1, b"/HELLO.TXT", IO_OPEN_READ);
    let (token, flags) = prepare(&mut service, handle);
    execute(
        &mut service,
        1,
        handle,
        Operation::InheritCommit {
            token,
            target: ProcessId(102),
        },
    )
    .unwrap();
    execute(
        &mut service,
        2,
        Handle(0),
        Operation::InheritClaim { token, flags },
    )
    .unwrap();
    execute(
        &mut service,
        2,
        handle,
        Operation::Seek {
            offset: 27,
            whence: IO_SEEK_SET,
        },
    )
    .unwrap();
    assert_eq!(
        execute(
            &mut service,
            1,
            handle,
            Operation::Seek {
                offset: 0,
                whence: IO_SEEK_CURRENT
            }
        ),
        Ok(Some(Reply::Value(27)))
    );
    execute(&mut service, 1, handle, Operation::Close).unwrap();
    assert_eq!(
        execute(
            &mut service,
            2,
            handle,
            Operation::Seek {
                offset: 0,
                whence: IO_SEEK_CURRENT
            }
        ),
        Ok(Some(Reply::Value(27)))
    );
    execute(&mut service, 2, handle, Operation::Release).unwrap();
    assert_eq!(IoComponent::dispose(&mut service), Ok(()));
}

#[test]
fn uncommitted_grants_die_on_close_or_disconnect_and_abort_releases_retention() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    for disconnect in [false, true] {
        let handle = open(&mut service, 1, b"/EMPTY.TXT", IO_OPEN_READ);
        let (token, _) = prepare(&mut service, handle);
        if disconnect {
            IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
        } else {
            execute(&mut service, 1, handle, Operation::Close).unwrap();
        }
        assert_eq!(
            execute(
                &mut service,
                1,
                handle,
                Operation::InheritCommit {
                    token,
                    target: ProcessId(102)
                }
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(IoComponent::dispose(&mut service), Ok(()));
    }
    let handle = open(&mut service, 1, b"/EMPTY.TXT", IO_OPEN_READ);
    let (token, _) = prepare(&mut service, handle);
    execute(
        &mut service,
        1,
        handle,
        Operation::InheritCommit {
            token,
            target: ProcessId(102),
        },
    )
    .unwrap();
    IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
    assert_eq!(
        execute(&mut service, 2, handle, Operation::InheritAbort(token)),
        Err(IoError(IO_PERMISSION_DENIED))
    );
    execute(&mut service, 1, handle, Operation::InheritAbort(token)).unwrap();
    assert_eq!(IoComponent::dispose(&mut service), Ok(()));
}

#[test]
fn create_exclusive_truncate_and_removal_obey_open_description_lifetime() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    execute(&mut service, 1, Handle(0), Operation::Mkdir(b"/NEW")).unwrap();
    let flags = IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE;
    let handle = open(&mut service, 1, b"/new/file.bin", flags);
    execute(&mut service, 1, handle, Operation::Write(b"payload")).unwrap();
    assert_eq!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::Open(b"/NEW/FILE.BIN", flags | IO_OPEN_TRUNCATE)
        ),
        Err(IoError(IO_ALREADY_EXISTS))
    );
    for path in [b"/new/file.bin".as_slice(), b"/NEW"] {
        assert_eq!(
            execute(&mut service, 1, Handle(0), Operation::Remove(path, 0)),
            Err(IoError(IO_WOULD_BLOCK))
        );
    }
    let writer = open(
        &mut service,
        2,
        b"/NEW/FILE.BIN",
        IO_OPEN_WRITE | IO_OPEN_TRUNCATE,
    );
    let mut data = [0; 8];
    assert_eq!(
        execute(&mut service, 1, handle, Operation::Read(&mut data)),
        Ok(Some(Reply::Value(0)))
    );
    assert_eq!(
        execute(
            &mut service,
            1,
            handle,
            Operation::Seek {
                offset: 0,
                whence: IO_SEEK_CURRENT
            }
        ),
        Ok(Some(Reply::Value(7)))
    );
    IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
    execute(&mut service, 2, writer, Operation::Close).unwrap();
    execute(
        &mut service,
        1,
        Handle(0),
        Operation::Remove(b"/new/file.bin", 0),
    )
    .unwrap();
    execute(&mut service, 1, Handle(0), Operation::Remove(b"/new", 0)).unwrap();
    assert_eq!(
        execute(&mut service, 1, Handle(0), Operation::FileInfo(b"/new")),
        Err(IoError(IO_INVALID_PATH))
    );
}

#[test]
fn metadata_and_directory_pagination_use_consistent_root_owned_modes() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    let Some(Reply::Directory(entries)) = execute(
        &mut service,
        1,
        Handle(0),
        Operation::ListDirectory(b"/docs", 0),
    )
    .unwrap() else {
        panic!("Expected directory");
    };
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].name(), b"NOTE.TXT");
    let Some(Reply::FileInfo(info)) = execute(
        &mut service,
        1,
        Handle(0),
        Operation::FileInfo(b"/docs/note.txt"),
    )
    .unwrap() else {
        panic!("Expected metadata");
    };
    assert_eq!(info, entries[0].info);
    assert_eq!(info.size, 6);
    assert_eq!(info.mode, IO_MODE_TYPE_FILE as u32 | 0o755);
    assert_eq!(info.owner, Credentials::new(UserId(0), GroupId(0)));
    assert_eq!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::ListDirectory(b"/DOCS", 1)
        ),
        Ok(Some(Reply::Directory(Vec::new())))
    );
}

#[test]
fn permissions_and_invalid_flags_fail_without_side_effects() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    for operation in [
        Operation::Open(b"/EMPTY.TXT", IO_OPEN_WRITE),
        Operation::Open(b"/NEW.TXT", IO_OPEN_WRITE | IO_OPEN_CREATE),
        Operation::Mkdir(b"/NEW"),
        Operation::Remove(b"/EMPTY.TXT", 0),
    ] {
        assert_eq!(
            service.execute(request(1, Handle(0), operation), |_| Ok(Credentials::new(
                UserId(7),
                GroupId(8)
            ))),
            Err(IoError(IO_PERMISSION_DENIED))
        );
    }
    for flags in [
        0,
        IO_OPEN_READ | IO_OPEN_TRUNCATE,
        IO_OPEN_READ | IO_OPEN_CREATE,
        IO_OPEN_WRITE | IO_OPEN_EXCLUSIVE,
        IO_OPEN_READ | IO_OPEN_BIND,
    ] {
        assert_eq!(
            execute(
                &mut service,
                1,
                Handle(0),
                Operation::Open(b"/EMPTY.TXT", flags)
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
    }
    assert_eq!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::Open(b"/HELLO.TXT", IO_OPEN_EXECUTE)
        ),
        Err(IoError(IO_PERMISSION_DENIED))
    );
    let handle = open(&mut service, 1, b"/EMPTY.TXT", IO_OPEN_WRITE);
    assert_eq!(
        execute(&mut service, 1, handle, Operation::Read(&mut [0; 1])),
        Err(IoError(IO_PERMISSION_DENIED))
    );
    assert_eq!(
        service.execute(
            request(1, handle, Operation::Write(b"x")).with_transfer_flags(1 << 63),
            |_| panic!("No credentials")
        ),
        Err(IoError(IO_OPERATION_NOT_SUPPORTED))
    );
    assert_eq!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::Open(b"/MISSING", IO_OPEN_READ)
        ),
        Err(IoError(IO_INVALID_PATH))
    );
    for path in [
        b"/docs/../EMPTY.TXT".as_slice(),
        b"/EMPTY.TXT\0",
        b"/docs//note.txt",
        b"relative",
    ] {
        assert_eq!(
            execute(&mut service, 1, Handle(0), Operation::FileInfo(path)),
            Err(IoError(IO_INVALID_PATH))
        );
    }
}

#[test]
fn readonly_attributes_apply_to_metadata_and_writes_even_for_root() {
    let mut disk = Disk::new(1);
    disk.sectors.get_mut(&DATA_START).unwrap()[11] |= 1;
    let mut service = Fat32::mount(disk).unwrap();
    assert_eq!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::Open(b"/HELLO.TXT", IO_OPEN_WRITE | IO_OPEN_TRUNCATE)
        ),
        Err(IoError(IO_PERMISSION_DENIED))
    );
    let Some(Reply::FileInfo(info)) = execute(
        &mut service,
        1,
        Handle(0),
        Operation::FileInfo(b"/HELLO.TXT"),
    )
    .unwrap() else {
        panic!("Expected metadata");
    };
    assert_eq!(info.mode & 0o777, 0o555);
    open(&mut service, 1, b"/HELLO.TXT", IO_OPEN_READ);
}

#[test]
fn failed_writes_preserve_offsets_and_handle_exhaustion_does_not_create() {
    let mut disk = Disk::new(1);
    disk.write_failures.push(1);
    let mut service = Fat32::mount(disk).unwrap();
    let handle = open(&mut service, 1, b"/EMPTY.TXT", IO_OPEN_WRITE);
    assert!(execute(&mut service, 1, handle, Operation::Write(b"data")).is_err());
    assert_eq!(
        execute(
            &mut service,
            1,
            handle,
            Operation::Seek {
                offset: 0,
                whence: IO_SEEK_CURRENT
            }
        ),
        Ok(Some(Reply::Value(0)))
    );
    assert_eq!(
        execute(&mut service, 1, handle, Operation::Write(b"data")),
        Ok(Some(Reply::Value(4)))
    );
    for _ in 1..64 {
        open(&mut service, 1, b"/EMPTY.TXT", IO_OPEN_READ);
    }
    assert_eq!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::Open(b"/NEW.TXT", IO_OPEN_WRITE | IO_OPEN_CREATE)
        ),
        Err(IoError(IO_REGISTER_FAILED))
    );
    IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
    assert_eq!(
        execute(&mut service, 1, Handle(0), Operation::FileInfo(b"/NEW.TXT")),
        Err(IoError(IO_INVALID_PATH))
    );
    assert_eq!(IoComponent::dispose(&mut service), Ok(()));
}

#[test]
fn grant_retention_blocks_removal_until_abort_without_live_owners() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    let handle = open(&mut service, 1, b"/docs/note.txt", IO_OPEN_READ);
    let (token, _) = prepare(&mut service, handle);
    execute(
        &mut service,
        1,
        handle,
        Operation::InheritCommit {
            token,
            target: ProcessId(102),
        },
    )
    .unwrap();
    IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
    for path in [b"/DOCS/NOTE.TXT".as_slice(), b"/docs"] {
        assert_eq!(
            execute(&mut service, 2, Handle(0), Operation::Remove(path, 0)),
            Err(IoError(IO_WOULD_BLOCK))
        );
    }
    execute(&mut service, 1, handle, Operation::InheritAbort(token)).unwrap();
    execute(
        &mut service,
        2,
        Handle(0),
        Operation::Remove(b"/DOCS/NOTE.TXT", 0),
    )
    .unwrap();
    execute(&mut service, 2, Handle(0), Operation::Remove(b"/DOCS", 0)).unwrap();
}

#[test]
fn failed_truncating_open_does_not_publish_a_description_or_destroy_data() {
    let mut disk = Disk::new(1);
    disk.write_failures.push(1);
    let mut service = Fat32::mount(disk).unwrap();
    assert!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::Open(b"/HELLO.TXT", IO_OPEN_WRITE | IO_OPEN_TRUNCATE)
        )
        .is_err()
    );
    assert_eq!(IoComponent::dispose(&mut service), Ok(()));
    let Some(Reply::FileInfo(info)) = execute(
        &mut service,
        1,
        Handle(0),
        Operation::FileInfo(b"/HELLO.TXT"),
    )
    .unwrap() else {
        panic!("Expected metadata");
    };
    assert_eq!(info.size, 700);
    open(
        &mut service,
        1,
        b"/HELLO.TXT",
        IO_OPEN_WRITE | IO_OPEN_TRUNCATE,
    );
    let Some(Reply::FileInfo(info)) = execute(
        &mut service,
        1,
        Handle(0),
        Operation::FileInfo(b"/HELLO.TXT"),
    )
    .unwrap() else {
        panic!("Expected metadata");
    };
    assert_eq!(info.size, 0);
}

#[test]
fn credential_failures_are_propagated_and_nonroot_reads_are_allowed() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    assert_eq!(
        service.execute(
            request(
                1,
                Handle(0),
                Operation::Open(b"/EMPTY.TXT", IO_OPEN_WRITE | IO_OPEN_TRUNCATE)
            ),
            |_| Err(IoError(IO_PERMISSION_DENIED))
        ),
        Err(IoError(IO_PERMISSION_DENIED))
    );
    assert_eq!(IoComponent::dispose(&mut service), Ok(()));
    let Some(Reply::Value(handle)) = service
        .execute(
            request(1, Handle(0), Operation::Open(b"/HELLO.TXT", IO_OPEN_READ)),
            |_| Ok(Credentials::new(UserId(7), GroupId(8))),
        )
        .unwrap()
    else {
        panic!("Expected handle");
    };
    assert_eq!(
        service.execute(
            request(1, Handle(handle), Operation::Read(&mut [0; 10])),
            |_| panic!("Handle reads use established access")
        ),
        Ok(Some(Reply::Value(10)))
    );
}

#[test]
fn executable_access_is_explicit_and_survives_inheritance() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    let handle = open(
        &mut service,
        1,
        b"/HELLO.TXT",
        IO_OPEN_READ | IO_OPEN_EXECUTE,
    );
    let (token, flags) = prepare(&mut service, handle);
    assert_eq!(flags, IO_OPEN_READ | IO_OPEN_EXECUTE);
    execute(
        &mut service,
        1,
        handle,
        Operation::InheritCommit {
            token,
            target: ProcessId(102),
        },
    )
    .unwrap();
    execute(
        &mut service,
        2,
        Handle(0),
        Operation::InheritClaim { token, flags },
    )
    .unwrap();
    assert_eq!(
        execute(&mut service, 2, handle, Operation::Write(b"denied")),
        Err(IoError(IO_PERMISSION_DENIED))
    );
}

#[test]
fn recursive_removal_handles_multiple_batches_and_does_not_remove_root() {
    let mut service = Fat32::mount(Disk::new(1)).unwrap();
    execute(&mut service, 1, Handle(0), Operation::Mkdir(b"/TREE")).unwrap();
    execute(
        &mut service,
        1,
        Handle(0),
        Operation::Mkdir(b"/TREE/NESTED"),
    )
    .unwrap();
    for index in 0..35 {
        let path = alloc::format!("/TREE/NESTED/F{index}.TXT");
        let handle = open(
            &mut service,
            1,
            path.as_bytes(),
            IO_OPEN_WRITE | IO_OPEN_CREATE,
        );
        execute(&mut service, 1, handle, Operation::Close).unwrap();
    }
    execute(
        &mut service,
        1,
        Handle(0),
        Operation::Remove(b"/TREE", IO_REMOVE_RECURSIVE),
    )
    .unwrap();
    assert_eq!(
        execute(&mut service, 1, Handle(0), Operation::FileInfo(b"/TREE")),
        Err(IoError(IO_INVALID_PATH))
    );
    assert_eq!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::Remove(b"/", IO_REMOVE_RECURSIVE)
        ),
        Err(IoError(IO_PERMISSION_DENIED))
    );
    assert!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::FileInfo(b"/HELLO.TXT")
        )
        .is_ok()
    );
}

#[test]
fn long_name_and_alias_share_busy_identity() {
    let mut service =
        Fat32::mount(Disk::new(1).with_long_name("Long filename spanning cluster.txt")).unwrap();
    let handle = open(&mut service, 1, b"/LONGFI~1.TXT", IO_OPEN_READ);
    assert_eq!(
        execute(
            &mut service,
            1,
            Handle(0),
            Operation::Remove(b"/Long filename spanning cluster.txt", 0)
        ),
        Err(IoError(IO_WOULD_BLOCK))
    );
    execute(&mut service, 1, handle, Operation::Close).unwrap();
    execute(
        &mut service,
        1,
        Handle(0),
        Operation::Remove(b"/Long filename spanning cluster.txt", 0),
    )
    .unwrap();
}
