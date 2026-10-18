#![no_std]

use core::ffi::c_void;
use core::mem::{offset_of, size_of};
use core::panic::PanicInfo;
use core::sync::atomic::{AtomicPtr, Ordering};

const ABI_VERSION: u32 = 1;
const ABI_SUCCESS: u32 = 0;
const ABI_ERROR: u32 = 1;
const ABI_INVALID_HOST: i32 = -1;

static HOST: AtomicPtr<HostOperationsV1> = AtomicPtr::new(core::ptr::null_mut());
static INITIALIZED_MESSAGE: &[u8] = b"Rust host component initialized";
static INVALID_HOST_MESSAGE: &[u8] = b"Invalid host component operations";
static PANIC_MESSAGE: &[u8] = b"Rust host component panicked";

#[repr(C)]
#[derive(Clone, Copy)]
struct AbiHeader {
    size: u32,
    version: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct ByteSpan {
    bytes: *const u8,
    length: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct AbiError {
    code: i32,
    reserved: u32,
    message: ByteSpan,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct StatusResult {
    kind: u32,
    reserved: u32,
    error: AbiError,
}

type HostLog = unsafe extern "C" fn(*mut c_void, ByteSpan) -> StatusResult;
type HostFatal = unsafe extern "C" fn(*mut c_void, ByteSpan);

#[repr(C)]
pub struct HostOperationsV1 {
    header: AbiHeader,
    context: *mut c_void,
    log: HostLog,
    fatal: HostFatal,
}

type ComponentInitialize = unsafe extern "C" fn(*mut c_void) -> StatusResult;
type ComponentDispose = unsafe extern "C" fn(*mut c_void) -> StatusResult;

#[repr(C)]
#[derive(Clone, Copy)]
struct ComponentDescriptorV1 {
    header: AbiHeader,
    context: *mut c_void,
    initialize: ComponentInitialize,
    dispose: ComponentDispose,
}

#[repr(C)]
pub struct ComponentLoadResult {
    kind: u32,
    reserved: u32,
    component: ComponentDescriptorV1,
    error: AbiError,
}

const _: () = {
    assert!(size_of::<AbiHeader>() == 8);
    assert!(size_of::<ByteSpan>() == 16);
    assert!(offset_of!(ByteSpan, length) == 8);
    assert!(size_of::<AbiError>() == 24);
    assert!(offset_of!(AbiError, message) == 8);
    assert!(size_of::<StatusResult>() == 32);
    assert!(offset_of!(StatusResult, error) == 8);
    assert!(size_of::<HostOperationsV1>() == 32);
    assert!(offset_of!(HostOperationsV1, context) == 8);
    assert!(size_of::<ComponentDescriptorV1>() == 32);
    assert!(offset_of!(ComponentDescriptorV1, context) == 8);
    assert!(size_of::<ComponentLoadResult>() == 64);
    assert!(offset_of!(ComponentLoadResult, component) == 8);
    assert!(offset_of!(ComponentLoadResult, error) == 40);
};

const fn span(bytes: &'static [u8]) -> ByteSpan {
    ByteSpan {
        bytes: bytes.as_ptr(),
        length: bytes.len() as u64,
    }
}

const fn empty_error() -> AbiError {
    AbiError {
        code: 0,
        reserved: 0,
        message: ByteSpan {
            bytes: core::ptr::null(),
            length: 0,
        },
    }
}

const fn success() -> StatusResult {
    StatusResult {
        kind: ABI_SUCCESS,
        reserved: 0,
        error: empty_error(),
    }
}

const fn load_error(code: i32, message: ByteSpan) -> ComponentLoadResult {
    ComponentLoadResult {
        kind: ABI_ERROR,
        reserved: 0,
        component: ComponentDescriptorV1 {
            header: AbiHeader {
                size: 0,
                version: 0,
            },
            context: core::ptr::null_mut(),
            initialize,
            dispose,
        },
        error: AbiError {
            code,
            reserved: 0,
            message,
        },
    }
}

unsafe extern "C" fn initialize(context: *mut c_void) -> StatusResult {
    let host = unsafe { &*context.cast::<HostOperationsV1>() };
    unsafe { (host.log)(host.context, span(INITIALIZED_MESSAGE)) }
}

unsafe extern "C" fn dispose(_context: *mut c_void) -> StatusResult {
    success()
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    let host = HOST.load(Ordering::Acquire);
    if !host.is_null() {
        let host = unsafe { &*host };
        unsafe { (host.fatal)(host.context, span(PANIC_MESSAGE)) };
    }
    loop {
        core::hint::spin_loop();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn hunde_component_entry_v1(host: *const HostOperationsV1) -> ComponentLoadResult {
    if host.is_null() {
        return load_error(ABI_INVALID_HOST, span(INVALID_HOST_MESSAGE));
    }
    let host_operations = unsafe { &*host };
    if host_operations.header.size < size_of::<HostOperationsV1>() as u32
        || host_operations.header.version != ABI_VERSION
    {
        return load_error(ABI_INVALID_HOST, span(INVALID_HOST_MESSAGE));
    }
    HOST.store(host.cast_mut(), Ordering::Release);
    ComponentLoadResult {
        kind: ABI_SUCCESS,
        reserved: 0,
        component: ComponentDescriptorV1 {
            header: AbiHeader {
                size: size_of::<ComponentDescriptorV1>() as u32,
                version: ABI_VERSION,
            },
            context: host.cast_mut().cast(),
            initialize,
            dispose,
        },
        error: empty_error(),
    }
}
