use crate::abi::{ByteSpan, IoError, host};
use crate::io::Reply;
use crate::protocol::{IO_REQUEST_READ, IO_UNKNOWN_BUFFER};
use alloc::vec::Vec;

pub fn read_file(path: &[u8], limit: usize) -> Result<Vec<u8>, IoError> {
    let mut bytes = alloc::vec![0; limit];
    let completion = unsafe {
        host::hunde_rust_bootstrap_read(ByteSpan::new(path), bytes.as_mut_ptr(), bytes.len())
    };
    match completion.into_result(IO_REQUEST_READ)? {
        Some(Reply::Value(count)) if count >= 0 && count as usize <= bytes.len() => {
            bytes.truncate(count as usize);
            Ok(bytes)
        }
        _ => Err(IoError(IO_UNKNOWN_BUFFER)),
    }
}
