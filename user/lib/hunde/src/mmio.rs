use crate::abi::IoError;
use crate::protocol::SERVICE_INVALID_BUFFER;
use core::ffi::c_void;
use core::ptr::NonNull;

unsafe extern "C" {
    fn service_map_mmio(physical_address: u64, size: usize, memory: *mut *mut c_void) -> i32;
    fn service_claim_virtio_device(address: u64) -> i32;
    fn driver_ptr(memory: *mut c_void) -> *mut c_void;
}

pub struct Mapping {
    base: NonNull<u8>,
    length: usize,
}

impl Mapping {
    pub fn map(physical_address: u64, length: usize) -> Result<Self, IoError> {
        if physical_address == 0 || length == 0 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let mut memory = core::ptr::null_mut();
        let status = unsafe { service_map_mmio(physical_address, length, &mut memory) };
        if status != 0 {
            return Err(IoError(status));
        }
        let base = NonNull::new(unsafe { driver_ptr(memory) }.cast::<u8>())
            .ok_or(IoError(SERVICE_INVALID_BUFFER))?;
        Ok(Self { base, length })
    }

    fn register(&self, offset: usize, width: usize) -> Result<*mut u8, IoError> {
        if offset > self.length
            || width > self.length - offset
            || !(self.base.as_ptr() as usize)
                .checked_add(offset)
                .is_some_and(|address| address.is_multiple_of(width))
        {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        Ok(unsafe { self.base.as_ptr().add(offset) })
    }

    pub fn claim_virtio(&self, offset: usize) -> Result<(), IoError> {
        let address = self.register(offset, 0x100)?;
        let status = unsafe { service_claim_virtio_device(address as u64) };
        if status == 0 {
            Ok(())
        } else {
            Err(IoError(status))
        }
    }

    pub fn read_u8(&self, offset: usize) -> Result<u8, IoError> {
        Ok(unsafe { self.register(offset, 1)?.read_volatile() })
    }

    pub fn write_u8(&mut self, offset: usize, value: u8) -> Result<(), IoError> {
        unsafe { self.register(offset, 1)?.write_volatile(value) };
        Ok(())
    }

    pub fn read_u16(&self, offset: usize) -> Result<u16, IoError> {
        Ok(unsafe { self.register(offset, 2)?.cast::<u16>().read_volatile() })
    }

    pub fn write_u16(&mut self, offset: usize, value: u16) -> Result<(), IoError> {
        unsafe {
            self.register(offset, 2)?
                .cast::<u16>()
                .write_volatile(value)
        };
        Ok(())
    }

    pub fn read_u32(&self, offset: usize) -> Result<u32, IoError> {
        Ok(unsafe { self.register(offset, 4)?.cast::<u32>().read_volatile() })
    }

    pub fn write_u32(&mut self, offset: usize, value: u32) -> Result<(), IoError> {
        unsafe {
            self.register(offset, 4)?
                .cast::<u32>()
                .write_volatile(value)
        };
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registers_validate_bounds_and_width() {
        let mut words = [0_u32; 2];
        let mut mapping = Mapping {
            base: NonNull::from(&mut words[0]).cast(),
            length: core::mem::size_of_val(&words),
        };
        mapping.write_u32(4, 0x1234).unwrap();
        assert_eq!(mapping.read_u32(4), Ok(0x1234));
        assert!(mapping.read_u32(5).is_err());
        assert!(mapping.read_u32(8).is_err());
        mapping.write_u8(0, 0x5a).unwrap();
        assert_eq!(mapping.read_u8(0), Ok(0x5a));
        assert!(mapping.read_u8(8).is_err());
        mapping.write_u16(2, 0xabcd).unwrap();
        assert_eq!(mapping.read_u16(2), Ok(0xabcd));
        assert!(mapping.read_u16(3).is_err());
        assert!(mapping.write_u16(8, 0).is_err());
    }
}
