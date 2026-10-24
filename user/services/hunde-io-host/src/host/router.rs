use super::Subsystem;
use super::dispatch::Dispatcher;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{Operation, Redirect, Reply, Request, SessionId};
use hunde::protocol::{
    IO_NO_DEVICE_FOUND, IO_OPEN_READ, IO_OPEN_WRITE, IO_PERMISSION_DENIED, IO_UNKNOWN_HANDLE,
};
use hunde::service::{ConsoleOutput, ReadinessMessage, ServiceName};

pub(crate) struct Router<Filesystem> {
    filesystem: Filesystem,
    console: ConsoleOutput,
    dispatcher: Dispatcher,
}

impl<Filesystem> Router<Filesystem> {
    pub(crate) fn with_dispatcher(
        filesystem: Filesystem,
        console: ConsoleOutput,
        dispatcher: Dispatcher,
    ) -> Self {
        Self {
            filesystem,
            console,
            dispatcher,
        }
    }

    fn stream(&self, path: &[u8], flags: u64) -> Result<Reply, IoError> {
        let service = match (path, flags) {
            (b":keyboard:", IO_OPEN_READ) => "keyboard",
            (b":tty:" | b":console:", IO_OPEN_READ) => "uart_rx",
            (b":tty:", IO_OPEN_WRITE) => "uart_tx",
            (b":console:", IO_OPEN_WRITE) => match self.console {
                ConsoleOutput::Serial => "uart_tx",
                ConsoleOutput::Framebuffer => "video_term",
            },
            (b":keyboard:" | b":tty:" | b":console:", _) => {
                return Err(IoError(IO_PERMISSION_DENIED));
            }
            _ => return Err(IoError(IO_NO_DEVICE_FOUND)),
        };
        Ok(Reply::Redirect(Redirect::new(service, path, b"", 0)?))
    }
}

fn is_stream(path: &[u8]) -> bool {
    path.len() >= 3
        && path.starts_with(b":")
        && path.ends_with(b":")
        && !path[1..path.len() - 1]
            .iter()
            .any(|byte| matches!(byte, b':' | b'/' | 0 | b' ' | b'\t' | b'\r' | b'\n'))
}

impl<Filesystem: IoComponent> IoComponent for Router<Filesystem> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        if let Operation::FileInfo(path) = &request.operation
            && is_stream(path)
            && let Ok(service) = core::str::from_utf8(path)
            && self.dispatcher.contains(service)
        {
            return self.dispatcher.execute(Some(service), request);
        }
        if let Operation::PipeCreate { .. } = request.operation {
            if self.dispatcher.contains("hund-rohr") {
                return self.dispatcher.execute(Some("hund-rohr"), request);
            }
            return Ok(Some(Reply::Redirect(Redirect::new(
                "hund-rohr",
                b"",
                b"",
                0,
            )?)));
        }
        if let Operation::Open(path, flags) = &request.operation
            && is_stream(path)
        {
            let hosted = match *path {
                b":console:" => Some(":console:"),
                b":tty:" => Some(":tty:"),
                b":keyboard:" if *flags == IO_OPEN_READ => Some(":keyboard:"),
                b":keyboard:" => return Err(IoError(IO_PERMISSION_DENIED)),
                _ => None,
            };
            if let Some(service) = hosted
                && self.dispatcher.contains(service)
            {
                return self.dispatcher.execute(Some(service), request);
            }
            return self.stream(path, *flags).map(Some);
        }
        let (path, target, offset) = match &request.operation {
            Operation::Open(path, _)
            | Operation::FileInfo(path)
            | Operation::Mkdir(path)
            | Operation::Remove(path, _) => (*path, b"".as_slice(), 0),
            Operation::ListDirectory(path, offset) => (*path, b"".as_slice(), *offset),
            Operation::Link { source, target } => (*source, *target, 0),
            _ => return self.dispatcher.execute(None, request),
        };
        if path.starts_with(b"socket://") {
            if self.dispatcher.contains("socket_router") {
                return self.dispatcher.execute(Some("socket_router"), request);
            }
            return Ok(Some(Reply::Redirect(Redirect::new(
                "socket_router",
                path,
                target,
                offset,
            )?)));
        }
        let operation = match &request.operation {
            Operation::Open(path, flags) => Operation::Open(path, *flags),
            Operation::FileInfo(path) => Operation::FileInfo(path),
            Operation::Mkdir(path) => Operation::Mkdir(path),
            Operation::Remove(path, flags) => Operation::Remove(path, *flags),
            Operation::ListDirectory(path, offset) => Operation::ListDirectory(path, *offset),
            Operation::Link { source, target } => Operation::Link { source, target },
            _ => return Err(IoError(IO_UNKNOWN_HANDLE)),
        };
        let routed = self.filesystem.execute(Request::new(
            request.session,
            request.pid,
            request.handle,
            operation,
        ))?;
        let Some(Reply::Redirect(route)) = routed else {
            return Ok(routed);
        };
        if !self.dispatcher.contains(&route.service) {
            return Ok(Some(Reply::Redirect(route)));
        }
        let operation = match request.operation {
            Operation::Open(_, flags) => Operation::Open(&route.path, flags),
            Operation::FileInfo(_) => Operation::FileInfo(&route.path),
            Operation::Mkdir(_) => Operation::Mkdir(&route.path),
            Operation::Remove(_, flags) => Operation::Remove(&route.path, flags),
            Operation::ListDirectory(..) => Operation::ListDirectory(&route.path, route.offset),
            Operation::Link { .. } => Operation::Link {
                source: &route.path,
                target: &route.target,
            },
            _ => return Err(IoError(IO_UNKNOWN_HANDLE)),
        };
        self.dispatcher.execute(
            Some(&route.service),
            Request::new(request.session, request.pid, request.handle, operation)
                .with_transfer_flags(request.transfer_flags),
        )
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.dispatcher.disconnect(session)?;
        self.filesystem.disconnect(session)
    }
}

impl<Filesystem: IoComponent> Subsystem for Router<Filesystem> {
    fn endpoint(&self) -> ServiceName {
        ServiceName::new("hund-io-system")
    }
    fn ready_message(&self) -> ReadinessMessage {
        ReadinessMessage::new("Rust I/O router ready")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::filesystem::namespace::{Namespace, RootFilesystem};
    use hunde::io::{Handle, ProcessId};
    use hunde::protocol::{IO_INVALID_PATH, IO_OPEN_CREATE};

    type Filesystem = Namespace<fn(ProcessId) -> Result<(), IoError>>;

    struct LocalBackend {
        path: &'static [u8],
    }
    impl LocalBackend {
        fn new(path: &'static [u8]) -> Self {
            Self { path }
        }
    }
    impl IoComponent for LocalBackend {
        fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
            assert_eq!((request.session, request.pid), (SessionId(7), ProcessId(9)));
            let reply = match request.operation {
                Operation::Open(path, _) => {
                    assert_eq!(path, self.path);
                    Reply::Value(1)
                }
                Operation::PipeCreate { .. } => Reply::Pipe {
                    reader: Handle(1),
                    writer: Handle(2),
                },
                Operation::Read(buffer) => {
                    buffer.fill(7);
                    Reply::Value(buffer.len() as i64)
                }
                Operation::Close => Reply::Value(0),
                _ => return Err(IoError(IO_UNKNOWN_HANDLE)),
            };
            Ok(Some(reply))
        }
        fn disconnect(&mut self, _session: SessionId) -> Result<(), IoError> {
            Ok(())
        }
    }

    #[test]
    fn hosted_files_sockets_and_pipes_are_served_on_the_front_door_without_redirects() {
        use crate::host::dispatch::Worker;
        let mut router = router(ConsoleOutput::Serial);
        let mut files = Worker::new(
            router.dispatcher.clone(),
            ServiceName::new("/tmp"),
            LocalBackend::new(b"/file"),
        )
        .unwrap();
        let mut sockets = Worker::new(
            router.dispatcher.clone(),
            ServiceName::new("socket_router"),
            LocalBackend::new(b"socket://test"),
        )
        .unwrap();
        let mut pipes = Worker::new(
            router.dispatcher.clone(),
            ServiceName::new("hund-rohr"),
            LocalBackend::new(b""),
        )
        .unwrap();
        assert_eq!(
            router.execute(request(Operation::Open(b"/tmp/file", IO_OPEN_READ))),
            Ok(None)
        );
        files.poll();
        let Some(Reply::Value(file)) = router
            .execute(request(Operation::Open(b"/tmp/file", IO_OPEN_READ)))
            .unwrap()
        else {
            panic!("expected host handle, not a redirect")
        };
        let mut buffer = [0; 4];
        assert_eq!(
            router.execute(Request::new(
                SessionId(7),
                ProcessId(9),
                Handle(file),
                Operation::Read(&mut buffer)
            )),
            Ok(None)
        );
        files.poll();
        assert_eq!(
            router.execute(Request::new(
                SessionId(7),
                ProcessId(9),
                Handle(file),
                Operation::Read(&mut buffer)
            )),
            Ok(Some(Reply::Value(4)))
        );
        assert_eq!(buffer, [7; 4]);
        assert_eq!(
            router.execute(request(Operation::Open(b"socket://test", IO_OPEN_READ))),
            Ok(None)
        );
        sockets.poll();
        let Some(Reply::Value(socket)) = router
            .execute(request(Operation::Open(b"socket://test", IO_OPEN_READ)))
            .unwrap()
        else {
            panic!("expected socket handle")
        };
        assert_ne!(file, socket);
        let pipe = || Operation::PipeCreate {
            buffered: true,
            flags: IO_OPEN_READ | IO_OPEN_WRITE,
        };
        assert_eq!(router.execute(request(pipe())), Ok(None));
        pipes.poll();
        let Some(Reply::Pipe { reader, writer }) = router.execute(request(pipe())).unwrap() else {
            panic!("expected pipe handles")
        };
        assert_ne!(reader, writer);
        assert_ne!(reader.0, file);
        assert_ne!(writer.0, socket);
    }

    fn authorize(_pid: ProcessId) -> Result<(), IoError> {
        Ok(())
    }

    fn request(operation: Operation<'_>) -> Request<'_> {
        Request::new(SessionId(7), ProcessId(9), Handle(0), operation)
    }

    fn router(console: ConsoleOutput) -> Router<Filesystem> {
        let mut filesystem = Namespace::new(
            RootFilesystem::new(),
            authorize as fn(ProcessId) -> Result<(), IoError>,
        );
        for config in [
            b"device - ext2.wuff /".as_slice(),
            b"device - embedfs /sbin",
            b"device - memfs /tmp",
        ] {
            filesystem
                .execute(request(Operation::Mount(config)))
                .unwrap();
        }
        Router::with_dispatcher(filesystem, console, Dispatcher::new())
    }

    fn route(router: &mut Router<Filesystem>, operation: Operation<'_>) -> Redirect {
        match router.execute(request(operation)).unwrap() {
            Some(Reply::Redirect(route)) => route,
            _ => panic!("expected final redirect"),
        }
    }

    #[test]
    fn filesystem_paths_select_mounts_locally_and_rewrite_both_link_paths() {
        let mut router = router(ConsoleOutput::Serial);
        assert_eq!(
            route(&mut router, Operation::Open(b"/file", IO_OPEN_READ)).service,
            "/"
        );
        assert_eq!(
            route(
                &mut router,
                Operation::Open(b"/sbin/component", IO_OPEN_READ)
            ),
            Redirect::new("/sbin", b"/component", b"", 0).unwrap()
        );
        assert_eq!(
            route(
                &mut router,
                Operation::Link {
                    source: b"/tmp/source",
                    target: b"/tmp/target"
                }
            ),
            Redirect::new("/tmp", b"/source", b"/target", 0).unwrap()
        );
        assert_eq!(
            route(&mut router, Operation::ListDirectory(b"/", 5)).offset,
            3
        );
        assert!(
            router
                .execute(request(Operation::Link {
                    source: b"/file",
                    target: b"/tmp/target"
                }))
                .is_err()
        );
    }

    #[test]
    fn serial_and_framebuffer_streams_preserve_existing_access_policy() {
        for output in [ConsoleOutput::Serial, ConsoleOutput::Framebuffer] {
            let mut router = router(output);
            assert_eq!(
                route(&mut router, Operation::Open(b":keyboard:", IO_OPEN_READ)).service,
                "keyboard"
            );
            assert_eq!(
                route(&mut router, Operation::Open(b":console:", IO_OPEN_READ)).service,
                "uart_rx"
            );
            assert_eq!(
                route(&mut router, Operation::Open(b":tty:", IO_OPEN_WRITE)).service,
                "uart_tx"
            );
            let console = route(&mut router, Operation::Open(b":console:", IO_OPEN_WRITE));
            assert_eq!(console.path, b":console:");
            assert_eq!(
                console.service,
                if output == ConsoleOutput::Serial {
                    "uart_tx"
                } else {
                    "video_term"
                }
            );
            for (path, flags) in [
                (b":keyboard:".as_slice(), IO_OPEN_WRITE),
                (b":tty:", IO_OPEN_READ | IO_OPEN_WRITE),
                (b":console:", IO_OPEN_WRITE | IO_OPEN_CREATE),
            ] {
                assert_eq!(
                    router.execute(request(Operation::Open(path, flags))),
                    Err(IoError(IO_PERMISSION_DENIED))
                );
            }
        }
    }

    #[test]
    fn unknown_and_malformed_stream_paths_keep_distinct_errors() {
        let mut router = router(ConsoleOutput::Serial);
        assert_eq!(
            router.execute(request(Operation::Open(b":unknown:", IO_OPEN_READ))),
            Err(IoError(IO_NO_DEVICE_FOUND))
        );
        for path in [
            b"::".as_slice(),
            b":bad/name:",
            b":bad name:",
            b":bad\0name:",
            b":bad:name:",
        ] {
            assert_eq!(
                router.execute(request(Operation::Open(path, IO_OPEN_READ))),
                Err(IoError(IO_INVALID_PATH))
            );
        }
        assert_eq!(
            router.execute(request(Operation::Read(&mut []))),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            router.execute(request(Operation::Mount(b"device - ext2 /other"))),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }

    #[test]
    fn sockets_and_pipes_redirect_to_hosted_endpoints_without_path_changes() {
        let mut router = router(ConsoleOutput::Serial);
        assert_eq!(
            route(
                &mut router,
                Operation::Open(b"socket://test", IO_OPEN_WRITE)
            ),
            Redirect::new("socket_router", b"socket://test", b"", 0).unwrap()
        );
        assert_eq!(
            route(
                &mut router,
                Operation::PipeCreate {
                    buffered: true,
                    flags: IO_OPEN_READ | IO_OPEN_WRITE
                }
            ),
            Redirect::new("hund-rohr", b"", b"", 0).unwrap()
        );
        assert_eq!(router.endpoint(), ServiceName::new("hund-io-system"));
    }
}
