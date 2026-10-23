use crate::abi::IoError;
use crate::protocol::IO_PANIC;
use alloc::boxed::Box;
use alloc::rc::Rc;
use alloc::sync::Arc;
use alloc::task::Wake;
use core::cell::RefCell;
use core::ffi::c_void;
use core::future::Future;
use core::pin::Pin;
use core::pin::pin;
use core::sync::atomic::{AtomicBool, Ordering};
use core::task::{Context, Poll, Waker};

pub trait FiberParker {
    fn park(&mut self) -> Result<(), IoError>;
}

pub struct Scheduler {
    storage: crate::native::Storage,
}

#[repr(C)]
pub(crate) struct NativeTask {
    entry: unsafe extern "C" fn(*mut c_void) -> crate::abi::StatusResult,
    context: *mut c_void,
}

impl NativeTask {
    fn new(
        entry: unsafe extern "C" fn(*mut c_void) -> crate::abi::StatusResult,
        context: *mut c_void,
    ) -> Self {
        Self { entry, context }
    }
}

const _: () = {
    assert!(core::mem::size_of::<NativeTask>() == 16);
    assert!(core::mem::offset_of!(NativeTask, context) == 8);
};

struct Completion {
    result: Option<Result<(), IoError>>,
    waker: Option<Waker>,
}

impl Completion {
    fn new() -> Self {
        Self {
            result: None,
            waker: None,
        }
    }

    fn finish(&mut self, result: Result<(), IoError>) -> Option<Waker> {
        self.result = Some(result);
        self.waker.take()
    }
}

/// A joinable task. Dropping its handle does not cancel the fibre.
pub struct TaskHandle {
    completion: Rc<RefCell<Completion>>,
}

impl TaskHandle {
    fn new(completion: Rc<RefCell<Completion>>) -> Self {
        Self { completion }
    }
}

impl Future for TaskHandle {
    type Output = Result<(), IoError>;

    fn poll(self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<Self::Output> {
        let mut completion = self.completion.borrow_mut();
        match completion.result {
            Some(result) => Poll::Ready(result),
            None => {
                completion.waker = Some(context.waker().clone());
                Poll::Pending
            }
        }
    }
}

struct OwnedInvocation<'a, Entry> {
    task: NativeTask,
    scheduler: &'a Scheduler,
    entry: Option<Entry>,
    completion: Rc<RefCell<Completion>>,
}

impl<'a, Entry> OwnedInvocation<'a, Entry> {
    fn new(
        task: NativeTask,
        scheduler: &'a Scheduler,
        entry: Entry,
        completion: Rc<RefCell<Completion>>,
    ) -> Self {
        Self {
            task,
            scheduler,
            entry: Some(entry),
            completion,
        }
    }
}

struct Invocation<'a, Entry> {
    scheduler: &'a Scheduler,
    entry: Option<Entry>,
    result: Option<Result<(), IoError>>,
}

impl<'a, Entry> Invocation<'a, Entry> {
    fn new(scheduler: &'a Scheduler, entry: Entry) -> Self {
        Self {
            scheduler,
            entry: Some(entry),
            result: None,
        }
    }
}

impl Scheduler {
    pub fn new() -> Result<Self, IoError> {
        let storage = crate::native::Storage::new(unsafe {
            crate::abi::host::hunde_rust_scheduler_layout()
        })?;
        unsafe { crate::abi::host::hunde_rust_scheduler_init(storage.as_ptr()) };
        Ok(Self { storage })
    }

    pub fn run<Entry: FnOnce(&mut SchedulerParker<'_>) -> Result<(), IoError>>(
        self,
        entry: Entry,
    ) -> Result<(), IoError> {
        unsafe extern "C" fn invoke<
            Entry: FnOnce(&mut SchedulerParker<'_>) -> Result<(), IoError>,
        >(
            context: *mut core::ffi::c_void,
        ) -> crate::abi::StatusResult {
            let invocation = unsafe { &mut *context.cast::<Invocation<'_, Entry>>() };
            let result = invocation
                .entry
                .take()
                .ok_or(IoError(-1))
                .and_then(|entry| entry(&mut SchedulerParker(invocation.scheduler)));
            invocation.result = Some(result);
            crate::abi::StatusResult::success()
        }
        let mut invocation = Invocation::new(&self, entry);
        let task = NativeTask::new(
            invoke::<Entry>,
            (&mut invocation as *mut Invocation<'_, Entry>).cast(),
        );
        unsafe { crate::abi::host::hunde_rust_scheduler_spawn(self.storage.as_ptr(), &task) }
            .into_result()?;
        unsafe { crate::abi::host::hunde_rust_channel_prepare_wait() };
        while unsafe { crate::abi::host::hunde_rust_scheduler_live(self.storage.as_ptr()) } != 0 {
            self.check_progress(unsafe {
                crate::abi::host::hunde_rust_scheduler_run(self.storage.as_ptr())
            })?;
            if unsafe { crate::abi::host::hunde_rust_scheduler_waiting(self.storage.as_ptr()) } != 0
            {
                unsafe { crate::abi::host::hunde_rust_channel_wait() };
                self.check_progress(unsafe {
                    crate::abi::host::hunde_rust_scheduler_wake(self.storage.as_ptr())
                })?;
            }
        }
        invocation.result.ok_or(IoError(IO_PANIC))?
    }

    fn check_progress(&self, result: crate::abi::StatusResult) -> Result<(), IoError> {
        let result = result.into_result();
        if result.is_err()
            && unsafe { crate::abi::host::hunde_rust_scheduler_live(self.storage.as_ptr()) } != 0
        {
            crate::runtime::fatal(b"Rust scheduler failed with live fibres");
        }
        result
    }
}

pub struct SchedulerParker<'a>(&'a Scheduler);

impl SchedulerParker<'_> {
    pub fn notify(&mut self) -> Result<(), IoError> {
        self.0.check_progress(unsafe {
            crate::abi::host::hunde_rust_scheduler_wake(self.0.storage.as_ptr())
        })
    }

    /// Starts owned work on this process's scheduler, not a separate executor.
    pub fn spawn<Entry>(&mut self, entry: Entry) -> Result<TaskHandle, IoError>
    where
        Entry: FnOnce(&mut SchedulerParker<'_>) -> Result<(), IoError> + 'static,
    {
        unsafe extern "C" fn invoke<Entry>(context: *mut c_void) -> crate::abi::StatusResult
        where
            Entry: FnOnce(&mut SchedulerParker<'_>) -> Result<(), IoError>,
        {
            let mut invocation =
                unsafe { Box::from_raw(context.cast::<OwnedInvocation<'_, Entry>>()) };
            let result = invocation
                .entry
                .take()
                .ok_or(IoError(IO_PANIC))
                .and_then(|entry| entry(&mut SchedulerParker(invocation.scheduler)));
            let waker = invocation.completion.borrow_mut().finish(result);
            if let Some(waker) = waker {
                waker.wake();
            }
            if invocation
                .scheduler
                .check_progress(unsafe {
                    crate::abi::host::hunde_rust_scheduler_wake(
                        invocation.scheduler.storage.as_ptr(),
                    )
                })
                .is_err()
            {
                crate::runtime::fatal(b"Rust task completion wake failed");
            }
            crate::abi::StatusResult::success()
        }

        let completion = Rc::new(RefCell::new(Completion::new()));
        let mut invocation = Box::new(OwnedInvocation::new(
            NativeTask::new(invoke::<Entry>, core::ptr::null_mut()),
            self.0,
            entry,
            completion.clone(),
        ));
        invocation.task.context = (&mut *invocation as *mut OwnedInvocation<'_, Entry>).cast();
        let invocation = Box::into_raw(invocation);
        let result = unsafe {
            crate::abi::host::hunde_rust_scheduler_spawn(
                self.0.storage.as_ptr(),
                &(*invocation).task,
            )
        }
        .into_result();
        if let Err(error) = result {
            drop(unsafe { Box::from_raw(invocation) });
            return Err(error);
        }
        Ok(TaskHandle::new(completion))
    }
}

impl FiberParker for SchedulerParker<'_> {
    fn park(&mut self) -> Result<(), IoError> {
        unsafe { crate::abi::host::hunde_rust_scheduler_park(self.0.storage.as_ptr()) }
            .into_result()
    }
}

struct Notification(AtomicBool);

impl Wake for Notification {
    fn wake(self: Arc<Self>) {
        self.wake_by_ref();
    }

    fn wake_by_ref(self: &Arc<Self>) {
        self.0.store(true, Ordering::Release);
    }
}

pub fn block_on<F: Future>(future: F, parker: &mut impl FiberParker) -> Result<F::Output, IoError> {
    let notification = Arc::new(Notification(AtomicBool::new(false)));
    let waker = Waker::from(notification.clone());
    let mut context = Context::from_waker(&waker);
    let mut future = pin!(future);
    loop {
        notification.0.store(false, Ordering::Release);
        match future.as_mut().poll(&mut context) {
            Poll::Ready(value) => return Ok(value),
            Poll::Pending => {
                if !notification.0.swap(false, Ordering::AcqRel) {
                    parker.park()?;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::cell::Cell;
    use core::future::poll_fn;

    #[test]
    fn joining_pending_task_registers_waker_and_completion_preserves_error() {
        let completion = Rc::new(RefCell::new(Completion::new()));
        let mut handle = TaskHandle::new(completion.clone());
        let notification = Arc::new(Notification(AtomicBool::new(false)));
        let waker = Waker::from(notification.clone());
        let mut context = Context::from_waker(&waker);
        assert_eq!(Pin::new(&mut handle).poll(&mut context), Poll::Pending);
        let waker = completion.borrow_mut().finish(Err(IoError(-6)));
        waker.unwrap().wake();
        assert!(notification.0.load(Ordering::Acquire));
        assert_eq!(
            Pin::new(&mut handle).poll(&mut context),
            Poll::Ready(Err(IoError(-6)))
        );
    }

    #[test]
    fn joining_already_completed_task_does_not_park() {
        let completion = Rc::new(RefCell::new(Completion::new()));
        assert!(completion.borrow_mut().finish(Ok(())).is_none());
        let count = Cell::new(0);
        assert_eq!(
            block_on(
                TaskHandle::new(completion),
                &mut Parker {
                    count: &count,
                    fail: true
                }
            ),
            Ok(Ok(()))
        );
        assert_eq!(count.get(), 0);
    }

    #[test]
    fn dropped_join_handle_does_not_cancel_completion_or_affect_sibling() {
        let first = Rc::new(RefCell::new(Completion::new()));
        let second = Rc::new(RefCell::new(Completion::new()));
        drop(TaskHandle::new(first.clone()));
        first.borrow_mut().finish(Err(IoError(-6)));
        assert_eq!(first.borrow().result, Some(Err(IoError(-6))));
        assert_eq!(second.borrow().result, None);
        second.borrow_mut().finish(Ok(()));
        assert_eq!(second.borrow().result, Some(Ok(())));
    }

    struct Parker<'a> {
        count: &'a Cell<usize>,
        fail: bool,
    }

    impl FiberParker for Parker<'_> {
        fn park(&mut self) -> Result<(), IoError> {
            self.count.set(self.count.get() + 1);
            if self.fail { Err(IoError(-6)) } else { Ok(()) }
        }
    }

    #[test]
    fn pending_future_parks_then_repolls_after_fibre_wakeup() {
        let count = Cell::new(0);
        let future = poll_fn(|_| {
            if count.get() == 0 {
                Poll::Pending
            } else {
                Poll::Ready(42)
            }
        });
        assert_eq!(
            block_on(
                future,
                &mut Parker {
                    count: &count,
                    fail: false
                }
            ),
            Ok(42)
        );
        assert_eq!(count.get(), 1);
    }

    #[test]
    fn synchronous_wakeup_is_not_lost_or_parked() {
        let count = Cell::new(0);
        let mut first = true;
        let future = poll_fn(|context| {
            if first {
                first = false;
                context.waker().wake_by_ref();
                Poll::Pending
            } else {
                Poll::Ready(7)
            }
        });
        assert_eq!(
            block_on(
                future,
                &mut Parker {
                    count: &count,
                    fail: false
                }
            ),
            Ok(7)
        );
        assert_eq!(count.get(), 0);
    }

    #[test]
    fn parking_errors_return_to_the_caller() {
        let count = Cell::new(0);
        let future = poll_fn(|_| Poll::<()>::Pending);
        assert_eq!(
            block_on(
                future,
                &mut Parker {
                    count: &count,
                    fail: true
                }
            ),
            Err(IoError(-6))
        );
    }
}
