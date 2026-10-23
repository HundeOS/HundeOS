use crate::abi::{ByteSpan, HostOperations, IoError, LifecycleHostOperations};
use core::alloc::{GlobalAlloc, Layout};
use core::sync::atomic::{AtomicPtr, Ordering};

static HOST: AtomicPtr<HostOperations> = AtomicPtr::new(core::ptr::null_mut());
static LIFECYCLE_HOST: AtomicPtr<LifecycleHostOperations> = AtomicPtr::new(core::ptr::null_mut());

/// Binds the lifecycle host used by logging and fatal callbacks.
///
/// # Safety
/// A non-null pointer must refer to an aligned, initialized host table whose
/// storage, context, and callbacks remain valid until image exit. The table
/// must remain immutable, and callbacks must not unwind across the C ABI.
pub unsafe fn install_lifecycle_host(host: *const LifecycleHostOperations) -> Result<(), IoError> {
    let operations = unsafe { host.as_ref() }.ok_or(IoError(-1))?;
    if !operations.header.accepts::<LifecycleHostOperations>()
        || operations.log.is_none()
        || operations.fatal.is_none()
    {
        return Err(IoError(-1));
    }
    LIFECYCLE_HOST.store(host.cast_mut(), Ordering::Release);
    Ok(())
}

/// Binds the allocating host used by this image.
///
/// # Safety
/// A non-null pointer must refer to an aligned, initialized table that remains
/// immutable and valid with its context and callbacks until image exit.
/// Allocation callbacks must honor Rust allocation layouts and must not unwind.
/// The allocator binding must not change while its allocations remain live.
pub unsafe fn install_host(host: *const HostOperations) -> Result<(), IoError> {
    let operations = unsafe { host.as_ref() }.ok_or(IoError(-1))?;
    if !operations.header.accepts_io::<HostOperations>()
        || operations.allocate.is_none()
        || operations.deallocate.is_none()
        || operations.fatal.is_none()
        || operations.credentials.is_none()
        || operations.process.is_none()
        || operations.trace.is_none()
        || operations.boot_info.is_none()
    {
        return Err(IoError(-1));
    }
    HOST.store(host.cast_mut(), Ordering::Release);
    Ok(())
}

pub struct HostAllocator;

#[cfg(feature = "alloc")]
pub struct LibcAllocator;

#[cfg(feature = "alloc")]
unsafe impl GlobalAlloc for LibcAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe {
            crate::abi::host::memalign(
                layout.align().max(core::mem::size_of::<usize>()),
                layout.size(),
            )
        }
    }

    unsafe fn dealloc(&self, allocation: *mut u8, _layout: Layout) {
        unsafe { crate::abi::host::free(allocation.cast()) };
    }
}

#[cfg(feature = "alloc")]
unsafe extern "C" fn allocate(size: u64, alignment: u64) -> *mut u8 {
    match Layout::from_size_align(size as usize, alignment as usize) {
        Ok(layout) => unsafe { LibcAllocator.alloc(layout) },
        Err(_) => core::ptr::null_mut(),
    }
}

#[cfg(feature = "alloc")]
unsafe extern "C" fn deallocate(allocation: *mut u8, _size: u64, _alignment: u64) {
    unsafe { crate::abi::host::free(allocation.cast()) };
}

#[cfg(feature = "alloc")]
unsafe extern "C" fn terminate(_context: *mut core::ffi::c_void, message: ByteSpan) {
    unsafe {
        crate::abi::host::hunde_rust_log(message);
        crate::abi::host::exit(1);
    }
}

#[cfg(feature = "alloc")]
pub fn initialize_executable() {
    const OPERATIONS: HostOperations = HostOperations {
        header: crate::abi::Header::io::<HostOperations>(),
        context: core::ptr::null_mut(),
        allocate: Some(allocate),
        deallocate: Some(deallocate),
        fatal: Some(terminate),
        credentials: Some(crate::abi::host::hunde_rust_credentials),
        process: Some(crate::abi::host::hunde_rust_process),
        trace: Some(crate::abi::host::hunde_rust_kernel_trace),
        boot_info: Some(crate::abi::host::hunde_rust_boot_info),
    };
    unsafe { install_host(&OPERATIONS) }.expect("Invalid executable operations");
}

#[cfg(feature = "alloc")]
pub(crate) fn operations() -> Result<*const HostOperations, IoError> {
    let host = HOST.load(Ordering::Acquire);
    if host.is_null() {
        return Err(IoError(-1));
    }
    Ok(host)
}

#[cfg(feature = "alloc")]
pub fn credentials(pid: crate::io::ProcessId) -> Result<crate::io::Credentials, IoError> {
    let operations = unsafe { &*operations()? };
    let lookup = operations
        .credentials
        .ok_or(IoError(crate::protocol::IO_UNKNOWN_PID))?;
    let result = unsafe { lookup(pid.0) };
    result.into_result()
}

#[cfg(feature = "alloc")]
pub fn log(message: &[u8]) {
    unsafe { crate::abi::host::hunde_rust_log(ByteSpan::new(message)) };
}

#[macro_export]
macro_rules! executable {
    ($entry:path, $message:expr) => {
        #[global_allocator]
        static HUNDE_ALLOCATOR: $crate::runtime::LibcAllocator = $crate::runtime::LibcAllocator;
        $crate::panic_handler!($message);

        #[unsafe(no_mangle)]
        pub extern "C" fn main() -> i32 {
            $crate::runtime::initialize_executable();
            match $entry() {
                Ok(()) => 0,
                Err(_) => $crate::runtime::fatal(b"Rust service failed"),
            }
        }
    };
}

unsafe impl GlobalAlloc for HostAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        match unsafe { HOST.load(Ordering::Acquire).as_ref() } {
            Some(host) => match host.allocate {
                Some(allocate) => unsafe { allocate(layout.size() as u64, layout.align() as u64) },
                None => core::ptr::null_mut(),
            },
            None => core::ptr::null_mut(),
        }
    }

    unsafe fn dealloc(&self, allocation: *mut u8, layout: Layout) {
        if let Some(host) = unsafe { HOST.load(Ordering::Acquire).as_ref() }
            && let Some(deallocate) = host.deallocate
        {
            unsafe { deallocate(allocation, layout.size() as u64, layout.align() as u64) };
        }
    }
}

pub fn fatal(message: &'static [u8]) -> ! {
    if let Some(host) = unsafe { HOST.load(Ordering::Acquire).as_ref() }
        && let Some(fatal) = host.fatal
    {
        unsafe { fatal(host.context, ByteSpan::new(message)) };
    }
    if let Some(host) = unsafe { LIFECYCLE_HOST.load(Ordering::Acquire).as_ref() }
        && let Some(fatal) = host.fatal
    {
        unsafe { fatal(host.context, ByteSpan::new(message)) };
    }
    loop {
        core::hint::spin_loop();
    }
}

#[macro_export]
macro_rules! panic_handler {
    ($message:expr) => {
        #[panic_handler]
        fn hunde_panic(_info: &core::panic::PanicInfo<'_>) -> ! {
            $crate::runtime::fatal($message)
        }
    };
}

#[macro_export]
macro_rules! host_runtime {
    ($message:expr) => {
        #[global_allocator]
        static HUNDE_ALLOCATOR: $crate::runtime::HostAllocator = $crate::runtime::HostAllocator;
        $crate::panic_handler!($message);
    };
}
