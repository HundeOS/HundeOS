use crate::abi::{AbiError, ByteSpan, ERROR, IoError, SUCCESS, host};
use crate::native::Storage;

pub const SECTOR_SIZE: usize = 512;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Sector(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidPath,
    InvalidTransfer,
    Native(IoError),
}

impl From<IoError> for Error {
    fn from(error: IoError) -> Self {
        Self::Native(error)
    }
}

pub trait Device {
    fn sector_count(&self) -> u64;
    fn read(&mut self, sector: Sector, destination: &mut [u8]) -> Result<(), Error>;
    fn write(&mut self, sector: Sector, source: &[u8]) -> Result<(), Error>;
}

#[repr(C)]
pub(crate) struct OpenResult {
    kind: u32,
    reserved: u32,
    sector_count: u64,
    error: AbiError,
}

impl OpenResult {
    fn into_result(self) -> Result<u64, IoError> {
        match self.kind {
            SUCCESS if self.sector_count != 0 => Ok(self.sector_count),
            ERROR => Err(IoError(self.error.code)),
            _ => Err(IoError(crate::protocol::IO_PANIC)),
        }
    }
}

pub struct Client {
    storage: Option<Storage>,
    sectors: u64,
}

impl Client {
    pub fn open(path: &[u8]) -> Result<Self, Error> {
        if path.is_empty() || path.len() >= 1024 || path.contains(&0) {
            return Err(Error::InvalidPath);
        }
        let storage = Storage::new(unsafe { host::hunde_rust_block_layout() })?;
        let result = unsafe { host::hunde_rust_block_open(storage.as_ptr(), ByteSpan::new(path)) };
        let sectors = result.into_result()?;
        Ok(Self {
            storage: Some(storage),
            sectors,
        })
    }

    pub fn close(mut self) -> Result<(), Error> {
        let storage = self.storage.take().expect("Open block client");
        unsafe { host::hunde_rust_block_close(storage.as_ptr()) }
            .into_result()
            .map_err(Error::from)
    }

    pub fn reclaim_cache(&mut self) {
        let storage = self.storage.as_ref().expect("Open block client");
        unsafe { host::hunde_rust_block_reclaim(storage.as_ptr()) };
    }
}

pub(crate) fn validate_transfer(sectors: u64, sector: Sector, length: usize) -> Result<(), Error> {
    if length == 0
        || !length.is_multiple_of(SECTOR_SIZE)
        || sector.0 > sectors
        || length as u64 / SECTOR_SIZE as u64 > sectors - sector.0
    {
        return Err(Error::InvalidTransfer);
    }
    Ok(())
}

impl Device for Client {
    fn sector_count(&self) -> u64 {
        self.sectors
    }

    fn read(&mut self, sector: Sector, destination: &mut [u8]) -> Result<(), Error> {
        validate_transfer(self.sectors, sector, destination.len())?;
        let storage = self.storage.as_ref().expect("Open block client");
        unsafe {
            host::hunde_rust_block_read(
                storage.as_ptr(),
                sector.0,
                destination.as_mut_ptr(),
                destination.len() as u64,
            )
        }
        .into_result()
        .map_err(Error::from)
    }

    fn write(&mut self, sector: Sector, source: &[u8]) -> Result<(), Error> {
        validate_transfer(self.sectors, sector, source.len())?;
        let storage = self.storage.as_ref().expect("Open block client");
        unsafe {
            host::hunde_rust_block_write(
                storage.as_ptr(),
                sector.0,
                source.as_ptr(),
                source.len() as u64,
            )
        }
        .into_result()
        .map_err(Error::from)
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        if let Some(storage) = &self.storage {
            let _ = unsafe { host::hunde_rust_block_close(storage.as_ptr()) };
        }
    }
}

const _: () = {
    assert!(core::mem::size_of::<OpenResult>() == 40);
    assert!(core::mem::offset_of!(OpenResult, sector_count) == 8);
    assert!(core::mem::offset_of!(OpenResult, error) == 16);
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn open_results_reject_empty_devices_and_invalid_tags() {
        for kind in [SUCCESS, 9] {
            let result = OpenResult {
                kind,
                reserved: 0,
                sector_count: 0,
                error: AbiError::new(0, b""),
            };
            assert_eq!(
                result.into_result(),
                Err(IoError(crate::protocol::IO_PANIC))
            );
        }
        let result = OpenResult {
            kind: ERROR,
            reserved: 0,
            sector_count: 0,
            error: AbiError::new(-88, b""),
        };
        assert_eq!(result.into_result(), Err(IoError(-88)));
    }

    #[test]
    fn transfer_bounds_reject_unaligned_empty_overflowing_and_out_of_range_buffers() {
        assert!(validate_transfer(10, Sector(9), 512).is_ok());
        for (sector, length) in [(0, 0), (0, 513), (10, 512), (9, 1024), (u64::MAX, 512)] {
            assert!(validate_transfer(10, Sector(sector), length).is_err());
        }
    }
}
