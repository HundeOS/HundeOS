#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Attributes {
    pub input_flags: u32,
    pub output_flags: u32,
    pub control_flags: u32,
    pub local_flags: u32,
    pub input_speed: u32,
    pub output_speed: u32,
    pub control_characters: [u8; 20],
}

impl Attributes {
    pub const fn new() -> Self {
        use crate::protocol::*;
        Self {
            input_flags: (IO_TERMINAL_INPUT_CR_TO_NL | IO_TERMINAL_INPUT_START_STOP_OUTPUT) as u32,
            output_flags: (IO_TERMINAL_OUTPUT_PROCESSING | IO_TERMINAL_OUTPUT_NL_TO_CR_NL) as u32,
            control_flags: IO_TERMINAL_CONTROL_CHARACTER_SIZE_8 as u32,
            local_flags: (IO_TERMINAL_LOCAL_SIGNALS
                | IO_TERMINAL_LOCAL_CANONICAL
                | IO_TERMINAL_LOCAL_ECHO
                | IO_TERMINAL_LOCAL_ECHO_ERASE
                | IO_TERMINAL_LOCAL_ECHO_KILL
                | IO_TERMINAL_LOCAL_EXTENDED) as u32,
            input_speed: IO_TERMINAL_SPEED_38400 as u32,
            output_speed: IO_TERMINAL_SPEED_38400 as u32,
            control_characters: [
                3, 28, 127, 21, 4, 0, 1, 0, 17, 19, 26, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            ],
        }
    }
}

impl Default for Attributes {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(feature = "alloc")]
mod client {
    use crate::abi::{ByteSpan, IoError, host};
    use crate::io::{Handle, ProcessId, Reply};
    use crate::protocol::{IO_PANIC, IO_REQUEST_OPEN, IO_REQUEST_READ, IO_REQUEST_WRITE};

    pub struct Stream {
        handle: Handle,
    }

    fn value(completion: crate::abi::Completion, operation: u32) -> Result<i64, IoError> {
        match completion.into_result(operation)? {
            Some(Reply::Value(value)) if value >= 0 => Ok(value),
            _ => Err(IoError(IO_PANIC)),
        }
    }

    impl Stream {
        pub fn open(service: &str, flags: u64) -> Result<Self, IoError> {
            if service.is_empty() || service.len() >= 256 || service.as_bytes().contains(&0) {
                return Err(IoError(crate::protocol::IO_INVALID_PATH));
            }
            let handle = value(
                unsafe { host::hunde_rust_terminal_open(ByteSpan::new(service.as_bytes()), flags) },
                IO_REQUEST_OPEN,
            )?;
            Ok(Self {
                handle: Handle(handle),
            })
        }

        pub fn read(&mut self, buffer: &mut [u8]) -> Result<usize, IoError> {
            let count = value(
                unsafe {
                    host::hunde_rust_terminal_read(
                        self.handle.0,
                        buffer.as_mut_ptr(),
                        buffer.len() as u64,
                    )
                },
                IO_REQUEST_READ,
            )? as usize;
            if count > buffer.len() {
                return Err(IoError(IO_PANIC));
            }
            Ok(count)
        }

        pub fn write(&mut self, buffer: &[u8]) -> Result<usize, IoError> {
            let count = value(
                unsafe {
                    host::hunde_rust_terminal_write(
                        self.handle.0,
                        buffer.as_ptr(),
                        buffer.len() as u64,
                    )
                },
                IO_REQUEST_WRITE,
            )? as usize;
            if count > buffer.len() {
                return Err(IoError(IO_PANIC));
            }
            Ok(count)
        }
    }

    impl Drop for Stream {
        fn drop(&mut self) {
            unsafe { host::hunde_rust_terminal_close(self.handle.0) };
        }
    }

    pub fn interrupt(pid: ProcessId) -> Result<(), IoError> {
        unsafe { host::hunde_rust_terminal_interrupt(pid.0) }.into_result()
    }
}

#[cfg(feature = "alloc")]
pub use client::{Stream, interrupt};

#[cfg(all(test, feature = "alloc"))]
mod tests {
    use super::*;
    use crate::abi::RawRequest;
    use crate::io::{Handle, Operation, OwnedRequest, ProcessId, Reply, Request, SessionId};

    #[test]
    fn terminal_controls_round_trip_through_the_owned_request_adapter() {
        assert_eq!(size_of::<Attributes>(), 44);
        let mut attributes = Attributes::new();
        attributes.local_flags = 42;
        RawRequest::with_request(
            Request::new(
                SessionId(1),
                ProcessId(2),
                Handle(3),
                Operation::SetTerminalAttributes(attributes),
            ),
            |raw| {
                let request = unsafe { Request::decode(raw) }.unwrap();
                let Operation::SetTerminalAttributes(actual) = request.operation else {
                    panic!("Expected attributes")
                };
                assert_eq!(actual, attributes);
            },
        );
        let mut output = Attributes::new();
        let mut owned = OwnedRequest::new(Request::new(
            SessionId(1),
            ProcessId(2),
            Handle(3),
            Operation::GetTerminalAttributes(&mut output),
        ));
        owned.with_request(|request| {
            RawRequest::with_request(request, |raw| {
                let request = unsafe { Request::decode(raw) }.unwrap();
                let Operation::GetTerminalAttributes(actual) = request.operation else {
                    panic!("Expected attributes")
                };
                *actual = attributes;
            })
        });
        owned
            .complete(
                Request::new(
                    SessionId(1),
                    ProcessId(2),
                    Handle(3),
                    Operation::GetTerminalAttributes(&mut output),
                ),
                Reply::Value(0),
            )
            .unwrap();
        assert_eq!(output, attributes);
        RawRequest::with_request(
            Request::new(
                SessionId(1),
                ProcessId(2),
                Handle(3),
                Operation::SetForegroundProcess(ProcessId(123)),
            ),
            |raw| {
                let request = unsafe { Request::decode(raw) }.unwrap();
                assert!(matches!(
                    request.operation,
                    Operation::SetForegroundProcess(ProcessId(123))
                ));
            },
        );
    }
}
