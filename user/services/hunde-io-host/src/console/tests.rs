use super::*;

#[derive(Clone)]
struct Sink(Rc<RefCell<Vec<u8>>>);

impl Output for Sink {
    fn write(&mut self, bytes: &[u8]) -> Result<usize, IoError> {
        self.0.borrow_mut().extend_from_slice(bytes);
        Ok(bytes.len())
    }
}

fn console() -> Console<Sink> {
    Console::new(
        Rc::new(RefCell::new(Input::new())),
        Sink(Rc::new(RefCell::new(Vec::new()))),
    )
}

#[test]
fn default_alias_dispatches_to_the_same_local_worker_without_a_channel() {
    use crate::host::dispatch::{Dispatcher, Worker};
    use hunde::service::ServiceName;
    let dispatcher = Dispatcher::new();
    assert!(
        dispatcher
            .alias(ServiceName::new(":console:"), "/dev/tty5")
            .is_err()
    );
    let mut worker =
        Worker::new(dispatcher.clone(), ServiceName::new("/dev/tty5"), console()).unwrap();
    dispatcher
        .alias(ServiceName::new(":console:"), "/dev/tty5")
        .unwrap();
    assert!(
        dispatcher
            .alias(ServiceName::new(":console:"), "/dev/tty5")
            .is_err()
    );
    let request = || {
        Request::new(
            SessionId(1),
            ProcessId(2),
            Handle(0),
            Operation::Open(b":console:", IO_OPEN_READ | IO_OPEN_WRITE),
        )
    };
    assert_eq!(dispatcher.execute(Some(":console:"), request()), Ok(None));
    assert!(worker.poll());
    assert!(matches!(
        dispatcher.execute(Some(":console:"), request()),
        Ok(Some(Reply::Value(_)))
    ));
}

fn call(
    console: &mut Console<Sink>,
    session: u64,
    pid: u64,
    handle: Handle,
    operation: Operation<'_>,
) -> Result<Option<Reply>, IoError> {
    console.execute(Request::new(
        SessionId(session),
        ProcessId(pid),
        handle,
        operation,
    ))
}

fn open(console: &mut Console<Sink>, flags: u64) -> Handle {
    let Some(Reply::Value(handle)) =
        call(console, 1, 2, Handle(0), Operation::Open(b"/", flags)).unwrap()
    else {
        panic!("Expected handle")
    };
    Handle(handle)
}

#[test]
fn configuration_requires_explicit_backends_numbered_nodes_and_a_mounted_default() {
    assert_eq!(
        Definition::parse("uart_rx,uart_tx", "/dev/tty0"),
        Ok(Definition::Serial)
    );
    assert_eq!(
        Definition::parse("keyboard,video_term", "/dev/vtty12"),
        Ok(Definition::Video)
    );
    assert_eq!(
        Definition::parse("/dev/vtty0", ":console:"),
        Ok(Definition::Default("/dev/vtty0".into()))
    );
    for (source, path) in [
        ("uart", "/dev/tty0"),
        ("uart_rx,uart_tx", "/dev/tty"),
        ("uart_rx,uart_tx", "/dev/tty0/child"),
        ("uart_rx,uart_tx", ":console:"),
        ("/dev/tty", ":console:"),
        ("/dev/tty0", "/dev/tty1"),
    ] {
        assert!(Definition::parse(source, path).is_err());
    }
}

#[test]
fn one_handle_reads_and_writes_with_direction_checks_and_authenticated_ownership() {
    let mut console = console();
    let handle = open(&mut console, IO_OPEN_READ | IO_OPEN_WRITE);
    console.input.borrow_mut().receive(b'a');
    let mut buffer = [0; 3];
    assert_eq!(
        call(&mut console, 1, 2, handle, Operation::Read(&mut buffer)),
        Ok(Some(Reply::Value(1)))
    );
    assert_eq!(buffer[0], b'a');
    assert_eq!(
        call(&mut console, 1, 2, handle, Operation::Write(b"ok")),
        Ok(Some(Reply::Value(2)))
    );
    assert_eq!(*console.output.0.borrow(), b"ok");
    assert!(call(&mut console, 9, 2, handle, Operation::Write(b"bad")).is_err());
    assert!(call(&mut console, 1, 9, handle, Operation::Write(b"bad")).is_err());
    let reader = open(&mut console, IO_OPEN_READ);
    assert!(call(&mut console, 1, 2, reader, Operation::Write(b"bad")).is_err());
    assert!(call(&mut console, 0, 2, handle, Operation::Close).is_err());
    assert!(call(&mut console, 1, 0, handle, Operation::Close).is_err());
}

#[test]
fn empty_reads_stay_pending_without_blocking_output_and_zero_length_reads_complete() {
    let mut console = console();
    let handle = open(&mut console, IO_OPEN_READ | IO_OPEN_WRITE);
    assert_eq!(
        call(&mut console, 1, 2, handle, Operation::Read(&mut [0])),
        Ok(None)
    );
    assert_eq!(
        call(&mut console, 1, 2, handle, Operation::Write(b"prompt")),
        Ok(Some(Reply::Value(6)))
    );
    assert_eq!(
        call(&mut console, 1, 2, handle, Operation::Read(&mut [])),
        Ok(Some(Reply::Value(0)))
    );
    console.input.borrow_mut().error = Some(IoError(IO_NO_DEVICE_FOUND));
    assert_eq!(
        call(&mut console, 1, 2, handle, Operation::Read(&mut [0])),
        Err(IoError(IO_NO_DEVICE_FOUND))
    );
}

#[test]
fn shell_redirection_does_not_create_or_truncate_terminal_state() {
    let mut console = console();
    console.input.borrow_mut().receive(b'x');
    let handle = open(
        &mut console,
        IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_TRUNCATE,
    );
    assert_eq!(console.descriptions[0].flags, IO_OPEN_WRITE);
    assert_eq!(console.input.borrow().bytes.front(), Some(&b'x'));
    assert_eq!(
        call(&mut console, 1, 2, handle, Operation::Write(b"ok")),
        Ok(Some(Reply::Value(2)))
    );
    assert_eq!(
        call(
            &mut console,
            1,
            2,
            Handle(0),
            Operation::Open(b"/", IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE)
        ),
        Err(IoError(IO_ALREADY_EXISTS))
    );
    assert!(
        call(
            &mut console,
            1,
            2,
            Handle(0),
            Operation::Open(b"/", IO_OPEN_READ | IO_OPEN_TRUNCATE)
        )
        .is_err()
    );
}

#[test]
fn empty_nonblocking_reads_return_would_block_without_consuming_data() {
    let mut console = console();
    let handle = open(&mut console, IO_OPEN_READ);
    let mut buffer = [0];
    let request = Request::new(
        SessionId(1),
        ProcessId(2),
        handle,
        Operation::Read(&mut buffer),
    )
    .with_transfer_flags(IO_TRANSFER_NONBLOCK);
    assert_eq!(console.execute(request), Err(IoError(IO_WOULD_BLOCK)));
}

#[test]
fn terminal_attributes_and_foreground_state_belong_to_the_console() {
    let mut console = console();
    let reader = open(&mut console, IO_OPEN_READ);
    let writer = open(&mut console, IO_OPEN_WRITE);
    let mut attributes = Attributes::new();
    attributes.local_flags = 0;
    call(
        &mut console,
        1,
        2,
        writer,
        Operation::SetTerminalAttributes(attributes),
    )
    .unwrap();
    let mut actual = Attributes::new();
    call(
        &mut console,
        1,
        2,
        reader,
        Operation::GetTerminalAttributes(&mut actual),
    )
    .unwrap();
    assert_eq!(attributes, actual);
    call(
        &mut console,
        1,
        2,
        reader,
        Operation::SetForegroundProcess(ProcessId(7)),
    )
    .unwrap();
    assert_eq!(console.input.borrow_mut().receive(3), Some(ProcessId(7)));
    assert!(console.input.borrow().bytes.is_empty());
    console.disconnect(SessionId(1)).unwrap();
    assert_eq!(console.input.borrow_mut().receive(3), None);
    assert_eq!(console.input.borrow().bytes.front(), Some(&3));
}

#[test]
fn committed_inheritance_survives_disconnect_and_preserves_access() {
    let mut console = console();
    let handle = open(&mut console, IO_OPEN_READ);
    let Some(Reply::Inheritance { token, flags }) =
        call(&mut console, 1, 2, handle, Operation::InheritPrepare).unwrap()
    else {
        panic!("Expected grant")
    };
    call(
        &mut console,
        1,
        2,
        handle,
        Operation::InheritCommit {
            token,
            target: ProcessId(3),
        },
    )
    .unwrap();
    console.disconnect(SessionId(1)).unwrap();
    assert_eq!(console.descriptions.len(), 1);
    assert!(
        call(
            &mut console,
            2,
            4,
            Handle(0),
            Operation::InheritClaim { token, flags }
        )
        .is_err()
    );
    assert!(
        call(
            &mut console,
            2,
            3,
            Handle(0),
            Operation::InheritClaim {
                token,
                flags: IO_OPEN_WRITE
            }
        )
        .is_err()
    );
    assert_eq!(
        call(
            &mut console,
            2,
            3,
            Handle(0),
            Operation::InheritClaim { token, flags }
        ),
        Ok(Some(Reply::Value(handle.0)))
    );
    assert!(call(&mut console, 2, 3, handle, Operation::Write(b"bad")).is_err());
    call(&mut console, 2, 3, handle, Operation::Close).unwrap();
    assert!(console.descriptions.is_empty());
}

#[test]
fn console_limits_and_cancelled_grants_release_descriptions() {
    let mut console = console();
    for _ in 0..64 {
        open(&mut console, IO_OPEN_READ);
    }
    assert!(
        call(
            &mut console,
            1,
            2,
            Handle(0),
            Operation::Open(b"/", IO_OPEN_READ)
        )
        .is_err()
    );
    console.disconnect(SessionId(1)).unwrap();
    assert!(console.descriptions.is_empty());
    assert!(console.grants.is_empty());
    assert!(
        call(
            &mut console,
            1,
            2,
            Handle(0),
            Operation::Open(b"/", IO_OPEN_EXECUTE)
        )
        .is_err()
    );
}
