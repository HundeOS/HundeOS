use crate::abi::{IoError, StatusResult};
use crate::protocol::SERVICE_INVALID_BUFFER;
use core::ptr::NonNull;

#[repr(C)]
struct NativeAllocation {
    status: StatusResult,
    address: u64,
    device_address: u64,
}

const _: () = assert!(core::mem::size_of::<NativeAllocation>() == 48);

pub struct Area {
    base: NonNull<u8>,
    device_address: u64,
    length: usize,
}

pub(crate) fn clean_mapped(address: u64, length: u64) {
    unsafe { hunde_dma_clean(address, length) };
}

pub(crate) fn invalidate_mapped(address: u64, length: u64) {
    unsafe { hunde_dma_invalidate(address, length) };
}

pub(crate) fn clean_invalidate_mapped(address: u64, length: u64) {
    unsafe { hunde_dma_clean_invalidate(address, length) };
}

impl Area {
    pub fn allocate(length: usize, alignment: usize) -> Result<Self, IoError> {
        let result = unsafe { hunde_dma_allocate(length as u64, alignment as u64) };
        result.status.into_result()?;
        let base =
            NonNull::new(result.address as *mut u8).ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        Ok(Self {
            base,
            device_address: result.device_address,
            length,
        })
    }

    fn range(&self, offset: usize, length: usize) -> Result<*mut u8, IoError> {
        if length == 0 || offset > self.length || length > self.length - offset {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        Ok(unsafe { self.base.as_ptr().add(offset) })
    }

    pub fn device_address(&self, offset: usize, length: usize) -> Result<u64, IoError> {
        self.range(offset, length)?;
        self.device_address
            .checked_add(offset as u64)
            .ok_or(IoError(SERVICE_INVALID_BUFFER))
    }

    pub fn write(&mut self, offset: usize, source: &[u8]) -> Result<(), IoError> {
        let target = self.range(offset, source.len())?;
        unsafe { core::ptr::copy_nonoverlapping(source.as_ptr(), target, source.len()) };
        Ok(())
    }

    pub fn read(&self, offset: usize, target: &mut [u8]) -> Result<(), IoError> {
        let source = self.range(offset, target.len())?;
        unsafe { core::ptr::copy_nonoverlapping(source, target.as_mut_ptr(), target.len()) };
        Ok(())
    }

    pub fn clean(&self, offset: usize, length: usize) -> Result<(), IoError> {
        let address = self.range(offset, length)?;
        unsafe { hunde_dma_clean(address as u64, length as u64) };
        Ok(())
    }

    pub fn invalidate(&self, offset: usize, length: usize) -> Result<(), IoError> {
        let address = self.range(offset, length)?;
        unsafe { hunde_dma_invalidate(address as u64, length as u64) };
        Ok(())
    }

    pub fn clean_invalidate(&self, offset: usize, length: usize) -> Result<(), IoError> {
        let address = self.range(offset, length)?;
        unsafe { hunde_dma_clean_invalidate(address as u64, length as u64) };
        Ok(())
    }

    pub fn boundary() {
        unsafe { hunde_dma_boundary() };
    }
}

unsafe extern "C" {
    fn hunde_dma_allocate(size: u64, alignment: u64) -> NativeAllocation;
    fn hunde_dma_clean(address: u64, size: u64);
    fn hunde_dma_invalidate(address: u64, size: u64);
    fn hunde_dma_clean_invalidate(address: u64, size: u64);
    fn hunde_dma_boundary();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ranges_and_device_addresses_are_bounded() {
        let mut bytes = [0_u8; 16];
        let area = Area {
            base: NonNull::from(&mut bytes[0]),
            device_address: 0x1000,
            length: bytes.len(),
        };
        assert_eq!(area.device_address(4, 8), Ok(0x1004));
        assert!(area.device_address(16, 1).is_err());
        assert!(area.device_address(0, 0).is_err());
        assert!(area.device_address(12, 5).is_err());
    }

    #[test]
    fn write_and_read_copy_exact_ranges() {
        let mut bytes = [0_u8; 8];
        let mut area = Area {
            base: NonNull::from(&mut bytes[0]),
            device_address: 0x2000,
            length: bytes.len(),
        };
        area.write(2, b"abcd").unwrap();
        let mut output = [0_u8; 4];
        area.read(2, &mut output).unwrap();
        assert_eq!(&output, b"abcd");
        assert_eq!(bytes, [0, 0, b'a', b'b', b'c', b'd', 0, 0]);
    }
}
