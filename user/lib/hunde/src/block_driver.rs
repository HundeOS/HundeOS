use crate::abi::{ByteSpan, IoError, StatusResult, host::NativeLayout};
use crate::native::Storage;
use crate::protocol::{BUFFER_READ, SERVICE_INVALID_BUFFER};
use alloc::boxed::Box;
use core::ffi::c_void;
use core::mem::ManuallyDrop;

#[repr(C)]
#[derive(Clone, Copy)]
struct NativeBuffer {
    id: u64,
    address: u64,
    capacity: u64,
    direction: u32,
    reserved: u32,
}

const _: () = assert!(core::mem::size_of::<NativeBuffer>() == 32);

pub struct Buffer {
    native: NativeBuffer,
}

impl Buffer {
    fn new(native: NativeBuffer) -> Self {
        Self { native }
    }

    fn address(&self, offset: u64, length: u64) -> Result<u64, IoError> {
        if length == 0 || offset > self.native.capacity || length > self.native.capacity - offset {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        self.native
            .address
            .checked_add(offset)
            .ok_or(IoError(SERVICE_INVALID_BUFFER))
    }

    pub fn device_address(&self, offset: u64, length: u64) -> Result<u64, IoError> {
        self.address(offset, length)?;
        let address = unsafe { hunde_block_buffer_device_address(&self.native, offset, length) };
        u64::try_from(address).map_err(|_| IoError(SERVICE_INVALID_BUFFER))
    }

    pub fn clean(&self, offset: u64, length: u64) -> Result<(), IoError> {
        crate::dma::clean_mapped(self.address(offset, length)?, length);
        Ok(())
    }

    pub fn invalidate(&self, offset: u64, length: u64) -> Result<(), IoError> {
        crate::dma::invalidate_mapped(self.address(offset, length)?, length);
        Ok(())
    }

    pub fn clean_invalidate(&self, offset: u64, length: u64) -> Result<(), IoError> {
        crate::dma::clean_invalidate_mapped(self.address(offset, length)?, length);
        Ok(())
    }

    pub fn write(&self, offset: u64, bytes: &[u8]) -> Result<(), IoError> {
        if self.native.direction != BUFFER_READ {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let address = self.address(
            offset,
            u64::try_from(bytes.len()).map_err(|_| IoError(SERVICE_INVALID_BUFFER))?,
        )?;
        let target = usize::try_from(address).map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        unsafe { core::ptr::copy(bytes.as_ptr(), target as *mut u8, bytes.len()) };
        Ok(())
    }

    pub fn validate_read_length(&self, length: u64) -> Result<(), IoError> {
        if self.native.direction != BUFFER_READ {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        self.address(0, length).map(|_| ())
    }
}

pub struct Wait {
    runtime: *mut c_void,
}

impl Wait {
    pub fn irq(&mut self) -> Result<(), IoError> {
        unsafe { hunde_block_driver_await(self.runtime) }.into_result()
    }
}

pub trait Transfer {
    fn transfer(
        &mut self,
        writing: bool,
        sector: u64,
        count: u64,
        buffer: &Buffer,
        wait: &mut Wait,
    ) -> Result<(), IoError>;
}

pub struct Server<Driver: Transfer> {
    storage: ManuallyDrop<Storage>,
    _driver: ManuallyDrop<Box<Driver>>,
}

impl<Driver: Transfer> Server<Driver> {
    pub fn listen(name: &[u8], sectors: u64, irq: u64, driver: Driver) -> Result<Self, IoError> {
        unsafe extern "C" fn transfer<Driver: Transfer>(
            context: *mut c_void,
            writing: u32,
            sector: u64,
            count: u64,
            buffer: *const NativeBuffer,
            runtime: *mut c_void,
        ) -> StatusResult {
            let result = unsafe { context.cast::<Driver>().as_mut() }
                .ok_or(IoError(SERVICE_INVALID_BUFFER))
                .and_then(|driver| {
                    let buffer =
                        unsafe { buffer.as_ref() }.ok_or(IoError(SERVICE_INVALID_BUFFER))?;
                    driver.transfer(
                        writing != 0,
                        sector,
                        count,
                        &Buffer::new(*buffer),
                        &mut Wait { runtime },
                    )
                });
            StatusResult::from_result(result)
        }

        let storage = ManuallyDrop::new(Storage::new(unsafe { hunde_block_driver_layout() })?);
        let mut driver = ManuallyDrop::new(Box::new(driver));
        let result = unsafe {
            hunde_block_driver_init(
                storage.as_ptr(),
                ByteSpan::new(name),
                sectors,
                irq,
                (&mut **driver as *mut Driver).cast(),
                transfer::<Driver>,
            )
        }
        .into_result();
        result?;
        Ok(Self {
            storage,
            _driver: driver,
        })
    }

    pub fn step(&mut self) -> Result<bool, IoError> {
        unsafe { hunde_block_driver_step(self.storage.as_ptr()) }.into_result()?;
        Ok(unsafe { hunde_block_driver_worked(self.storage.as_ptr()) != 0 })
    }

    pub fn deliver(&mut self) -> Result<(), IoError> {
        unsafe { hunde_block_driver_deliver(self.storage.as_ptr()) }.into_result()
    }

    pub fn tick(&mut self) -> Result<(), IoError> {
        self.deliver()
    }
}

pub fn register(name: &[u8], sectors: u64) -> Result<(), IoError> {
    unsafe { hunde_block_driver_register(ByteSpan::new(name), sectors) }.into_result()
}

pub fn monotonic_usec() -> u64 {
    unsafe { hunde_block_driver_monotonic_usec() }
}

pub fn sleep_usec(usec: u64) -> Result<(), IoError> {
    unsafe { hunde_block_driver_sleep_usec(usec) }.into_result()
}

pub fn record_submission(blocks: u64, device_usec: u64, succeeded: bool) {
    unsafe { hunde_block_driver_record(blocks, device_usec, u32::from(succeeded)) };
}

unsafe extern "C" {
    fn hunde_block_driver_layout() -> NativeLayout;
    fn hunde_block_driver_init(
        storage: *mut c_void,
        name: ByteSpan,
        sectors: u64,
        irq: u64,
        context: *mut c_void,
        transfer: unsafe extern "C" fn(
            *mut c_void,
            u32,
            u64,
            u64,
            *const NativeBuffer,
            *mut c_void,
        ) -> StatusResult,
    ) -> StatusResult;
    fn hunde_block_driver_step(storage: *mut c_void) -> StatusResult;
    fn hunde_block_driver_worked(storage: *mut c_void) -> u32;
    fn hunde_block_driver_deliver(storage: *mut c_void) -> StatusResult;
    fn hunde_block_driver_sleep_usec(usec: u64) -> StatusResult;
    fn hunde_block_driver_await(storage: *mut c_void) -> StatusResult;
    fn hunde_block_driver_register(name: ByteSpan, sectors: u64) -> StatusResult;
    fn hunde_block_buffer_device_address(
        buffer: *const NativeBuffer,
        offset: u64,
        length: u64,
    ) -> i64;
    fn hunde_block_driver_monotonic_usec() -> u64;
    fn hunde_block_driver_record(blocks: u64, device_usec: u64, succeeded: u32);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mapped_ranges_reject_empty_overflow_and_crossing_accesses() {
        let buffer = Buffer::new(NativeBuffer {
            id: 1,
            address: 0x1000,
            capacity: 4096,
            direction: 1,
            reserved: 0,
        });
        assert_eq!(buffer.address(100, 512), Ok(0x1064));
        assert!(buffer.address(4096, 1).is_err());
        assert!(buffer.address(0, 0).is_err());
        assert!(buffer.address(u64::MAX, 1).is_err());
    }

    #[test]
    fn pio_write_checks_direction_and_bounds() {
        let mut words = [0_u8; 8];
        let mut buffer = Buffer::new(NativeBuffer {
            id: 1,
            address: words.as_mut_ptr() as u64,
            capacity: words.len() as u64,
            direction: 1,
            reserved: 0,
        });
        buffer.write(2, &[1, 2, 3]).unwrap();
        assert_eq!(words, [0, 0, 1, 2, 3, 0, 0, 0]);
        assert!(buffer.write(7, &[1, 2]).is_err());
        buffer.native.direction = 2;
        assert!(buffer.write(0, &[9]).is_err());
    }
}
