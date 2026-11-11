use crate::abi::{IoError, host::NativeLayout};
use alloc::alloc::{alloc_zeroed, dealloc};
use core::alloc::Layout;
use core::ffi::c_void;
use core::ptr::NonNull;

pub(crate) unsafe fn copy_to_mapped(target: *mut u8, source: &[u8]) {
    unsafe { core::ptr::copy(source.as_ptr(), target, source.len()) };
}

pub(crate) unsafe fn copy_from_mapped(source: *const u8, target: &mut [u8]) {
    unsafe { core::ptr::copy(source, target.as_mut_ptr(), target.len()) };
}

pub(crate) struct Storage {
    address: NonNull<u8>,
    layout: Layout,
}

impl Storage {
    pub(crate) fn new(native: NativeLayout) -> Result<Self, IoError> {
        let layout = Layout::from_size_align(native.size as usize, native.alignment as usize)
            .map_err(|_| IoError(-1))?;
        if layout.size() == 0 {
            return Err(IoError(-1));
        }
        let address = NonNull::new(unsafe { alloc_zeroed(layout) }).ok_or(IoError(-3))?;
        Ok(Self { address, layout })
    }

    pub(crate) fn as_ptr(&self) -> *mut c_void {
        self.address.as_ptr().cast()
    }
}

impl Drop for Storage {
    fn drop(&mut self) {
        unsafe { dealloc(self.address.as_ptr(), self.layout) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mapped_copies_preserve_binary_data_and_neighboring_bytes() {
        let mut mapping = [99; 32];
        let bytes = [0, 255, 0, 128, 10, 13];
        unsafe { copy_to_mapped(mapping.as_mut_ptr().add(7), &bytes) };
        let mut snapshot = [0; 6];
        unsafe { copy_from_mapped(mapping.as_ptr().add(7), &mut snapshot) };
        assert_eq!(snapshot, bytes);
        assert_eq!(&mapping[..7], &[99; 7]);
        assert_eq!(&mapping[13..], &[99; 19]);
    }

    #[test]
    fn native_storage_is_aligned_zeroed_and_stable_when_moved() {
        let storage = Storage::new(NativeLayout {
            size: 96,
            alignment: 64,
        })
        .unwrap();
        let address = storage.as_ptr();
        assert_eq!(address as usize % 64, 0);
        assert!(
            unsafe { core::slice::from_raw_parts(address.cast::<u8>(), 96) }
                .iter()
                .all(|value| *value == 0)
        );
        let moved = alloc::boxed::Box::new(storage);
        assert_eq!(address, moved.as_ptr());
    }

    #[test]
    fn invalid_native_layouts_are_errors() {
        assert!(matches!(
            Storage::new(NativeLayout {
                size: 0,
                alignment: 8
            }),
            Err(IoError(-1))
        ));
        assert!(matches!(
            Storage::new(NativeLayout {
                size: 8,
                alignment: 3
            }),
            Err(IoError(-1))
        ));
    }
}
