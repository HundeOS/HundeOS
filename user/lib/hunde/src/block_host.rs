use crate::abi::{Header, IoError, StatusResult};
use crate::block::{Device, Error, Sector, validate_transfer};
use crate::protocol::{IO_OPERATION_NOT_SUPPORTED, IO_PANIC};
use alloc::boxed::Box;
use core::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Operations {
    pub header: Header,
    pub context: *mut c_void,
    pub sectors: u64,
    pub read: Option<unsafe extern "C" fn(*mut c_void, u64, *mut u8, u64) -> StatusResult>,
    pub write: Option<unsafe extern "C" fn(*mut c_void, u64, *const u8, u64) -> StatusResult>,
}

pub struct HostedDevice {
    operations: Operations,
}

impl HostedDevice {
    /// Accepts one host-owned device capability for a mounted component.
    ///
    /// # Safety
    /// The pointer must reference a valid aligned header and, if its size is
    /// accepted, a complete operations table. Context and callbacks must remain
    /// valid exclusively for this device until it is dropped. Callbacks must
    /// honor buffer bounds and must not unwind or retain borrowed buffers.
    pub unsafe fn from_raw(operations: *const Operations) -> Result<Self, IoError> {
        let header = unsafe { operations.cast::<Header>().as_ref() }.ok_or(IoError(IO_PANIC))?;
        if !header.accepts::<Operations>() {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        let operations = unsafe { *operations };
        if operations.context.is_null()
            || operations.sectors == 0
            || operations.read.is_none()
            || operations.write.is_none()
        {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        Ok(Self { operations })
    }
}

impl Device for HostedDevice {
    fn sector_count(&self) -> u64 {
        self.operations.sectors
    }

    fn read(&mut self, sector: Sector, destination: &mut [u8]) -> Result<(), Error> {
        validate_transfer(self.sector_count(), sector, destination.len())?;
        unsafe {
            self.operations.read.unwrap()(
                self.operations.context,
                sector.0,
                destination.as_mut_ptr(),
                destination.len() as u64,
            )
        }
        .into_result()
        .map_err(Error::from)
    }

    fn write(&mut self, sector: Sector, source: &[u8]) -> Result<(), Error> {
        validate_transfer(self.sector_count(), sector, source.len())?;
        unsafe {
            self.operations.write.unwrap()(
                self.operations.context,
                sector.0,
                source.as_ptr(),
                source.len() as u64,
            )
        }
        .into_result()
        .map_err(Error::from)
    }
}

pub(crate) struct BlockHost<D: Device> {
    device: Box<D>,
    operations: Operations,
}

fn io_error(error: Error) -> IoError {
    match error {
        Error::Native(error) => error,
        _ => IoError(IO_OPERATION_NOT_SUPPORTED),
    }
}

unsafe extern "C" fn read<D: Device>(
    context: *mut c_void,
    sector: u64,
    destination: *mut u8,
    length: u64,
) -> StatusResult {
    let result = (|| {
        let device = unsafe { context.cast::<D>().as_mut() }.ok_or(IoError(IO_PANIC))?;
        if destination.is_null() || length > isize::MAX as u64 {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        validate_transfer(device.sector_count(), Sector(sector), length as usize)
            .map_err(io_error)?;
        device
            .read(Sector(sector), unsafe {
                core::slice::from_raw_parts_mut(destination, length as usize)
            })
            .map_err(io_error)
    })();
    StatusResult::from_result(result)
}

unsafe extern "C" fn write<D: Device>(
    context: *mut c_void,
    sector: u64,
    source: *const u8,
    length: u64,
) -> StatusResult {
    let result = (|| {
        let device = unsafe { context.cast::<D>().as_mut() }.ok_or(IoError(IO_PANIC))?;
        if source.is_null() || length > isize::MAX as u64 {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        validate_transfer(device.sector_count(), Sector(sector), length as usize)
            .map_err(io_error)?;
        device
            .write(Sector(sector), unsafe {
                core::slice::from_raw_parts(source, length as usize)
            })
            .map_err(io_error)
    })();
    StatusResult::from_result(result)
}

impl<D: Device> BlockHost<D> {
    pub(crate) fn new(device: D) -> Self {
        let mut device = Box::new(device);
        let operations = Operations {
            header: Header::of::<Operations>(),
            context: (&mut *device as *mut D).cast(),
            sectors: device.sector_count(),
            read: Some(read::<D>),
            write: Some(write::<D>),
        };
        Self { device, operations }
    }

    pub(crate) fn operations(&self) -> &Operations {
        &self.operations
    }
}

impl BlockHost<crate::block::Client> {
    pub(crate) fn reclaim_cache(&mut self) {
        self.device.reclaim_cache();
    }
}

const _: () = {
    assert!(core::mem::size_of::<Operations>() == 40);
    assert!(core::mem::offset_of!(Operations, sectors) == 16);
    assert!(core::mem::offset_of!(Operations, read) == 24);
};

#[cfg(test)]
mod tests {
    use super::*;
    use crate::abi::ERROR;

    struct Disk {
        data: [u8; 1024],
        calls: usize,
        fail: bool,
    }

    impl Disk {
        fn new() -> Self {
            Self {
                data: [7; 1024],
                calls: 0,
                fail: false,
            }
        }
    }

    impl Device for Disk {
        fn sector_count(&self) -> u64 {
            2
        }
        fn read(&mut self, sector: Sector, destination: &mut [u8]) -> Result<(), Error> {
            self.calls += 1;
            if self.fail {
                return Err(Error::Native(IoError(-77)));
            }
            let start = sector.0 as usize * 512;
            destination.copy_from_slice(&self.data[start..start + destination.len()]);
            Ok(())
        }
        fn write(&mut self, sector: Sector, source: &[u8]) -> Result<(), Error> {
            self.calls += 1;
            if self.fail {
                return Err(Error::Native(IoError(-77)));
            }
            let start = sector.0 as usize * 512;
            self.data[start..start + source.len()].copy_from_slice(source);
            Ok(())
        }
    }

    #[test]
    fn callbacks_preserve_data_bounds_and_context_when_host_is_moved() {
        let host = BlockHost::new(Disk::new());
        let mut device = unsafe { HostedDevice::from_raw(host.operations()) }.unwrap();
        let host = Box::new(host);
        let mut bytes = [0; 512];
        device.read(Sector(1), &mut bytes).unwrap();
        assert_eq!(bytes, [7; 512]);
        device.write(Sector(0), &[9; 512]).unwrap();
        assert_eq!(&host.device.data[..512], &[9; 512]);
        for sector in [2, u64::MAX] {
            assert_eq!(
                device.read(Sector(sector), &mut bytes),
                Err(Error::InvalidTransfer)
            );
            assert_eq!(
                device.write(Sector(sector), &bytes),
                Err(Error::InvalidTransfer)
            );
        }
        assert_eq!(device.read(Sector(0), &mut []), Err(Error::InvalidTransfer));
        assert_eq!(
            device.write(Sector(0), &[0; 513]),
            Err(Error::InvalidTransfer)
        );
        assert_eq!(host.device.calls, 2);
    }

    #[test]
    fn tables_require_a_valid_header_geometry_context_and_callbacks() {
        let host = BlockHost::new(Disk::new());
        for case in 0..6 {
            let mut operations = *host.operations();
            match case {
                0 => operations.header.version = 2,
                1 => operations.header.size = 8,
                2 => operations.context = core::ptr::null_mut(),
                3 => operations.sectors = 0,
                4 => operations.read = None,
                5 => operations.write = None,
                _ => unreachable!(),
            }
            assert!(unsafe { HostedDevice::from_raw(&operations) }.is_err());
        }
        let header = Header {
            size: 8,
            version: 1,
        };
        assert!(unsafe { HostedDevice::from_raw((&header as *const Header).cast()) }.is_err());
        assert!(unsafe { HostedDevice::from_raw(core::ptr::null()) }.is_err());
    }

    #[test]
    fn raw_callbacks_reject_invalid_lengths_and_pointers_before_io() {
        let host = BlockHost::new(Disk::new());
        let operations = host.operations();
        let mut bytes = [0; 512];
        for length in [0, 513, u64::MAX] {
            assert_eq!(
                unsafe {
                    operations.read.unwrap()(operations.context, 0, bytes.as_mut_ptr(), length)
                }
                .kind,
                ERROR
            );
            assert_eq!(
                unsafe { operations.write.unwrap()(operations.context, 0, bytes.as_ptr(), length) }
                    .kind,
                ERROR
            );
        }
        assert_eq!(
            unsafe { operations.read.unwrap()(operations.context, 0, core::ptr::null_mut(), 512) }
                .kind,
            ERROR
        );
        assert_eq!(
            unsafe { operations.write.unwrap()(operations.context, 0, core::ptr::null(), 512) }
                .kind,
            ERROR
        );
        assert_eq!(
            unsafe { operations.read.unwrap()(core::ptr::null_mut(), 0, bytes.as_mut_ptr(), 512) }
                .kind,
            ERROR
        );
        assert_eq!(host.device.calls, 0);
    }

    #[test]
    fn native_errors_and_malformed_callback_statuses_are_not_successes() {
        let mut disk = Disk::new();
        disk.fail = true;
        let host = BlockHost::new(disk);
        {
            let mut device = unsafe { HostedDevice::from_raw(host.operations()) }.unwrap();
            assert_eq!(
                device.read(Sector(0), &mut [0; 512]),
                Err(Error::Native(IoError(-77)))
            );
            assert_eq!(
                device.write(Sector(0), &[0; 512]),
                Err(Error::Native(IoError(-77)))
            );
        }
        unsafe extern "C" fn malformed(
            _context: *mut c_void,
            _sector: u64,
            _data: *mut u8,
            _length: u64,
        ) -> StatusResult {
            let mut result = StatusResult::success();
            result.kind = 99;
            result
        }
        let mut operations = *host.operations();
        operations.read = Some(malformed);
        let mut device = unsafe { HostedDevice::from_raw(&operations) }.unwrap();
        assert_eq!(
            device.read(Sector(0), &mut [0; 512]),
            Err(Error::Native(IoError(IO_PANIC)))
        );
    }
}
