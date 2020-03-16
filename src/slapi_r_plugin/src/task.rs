use crate::constants::LDAP_SUCCESS;
use crate::entry::EntryRef;
use crate::pblock::PblockRef;
use std::ffi::CString;
use std::os::raw::c_char;
use std::thread;
use std::time::Duration;

extern "C" {
    fn slapi_plugin_new_task(ndn: *const c_char, arg: *const libc::c_void) -> *const libc::c_void;
    fn slapi_task_dec_refcount(task: *const libc::c_void);
    fn slapi_task_inc_refcount(task: *const libc::c_void);
    fn slapi_task_get_refcount(task: *const libc::c_void) -> i32;
    fn slapi_task_begin(task: *const libc::c_void, rc: i32);
    fn slapi_task_finish(task: *const libc::c_void, rc: i32);

    fn slapi_plugin_task_register_handler(
        ident: *const c_char,
        cb: extern "C" fn(
            *const libc::c_void,
            *const libc::c_void,
            *const libc::c_void,
            *mut i32,
            *mut c_char,
            *const libc::c_void,
        ) -> i32,
        pb: *const libc::c_void,
    ) -> i32;
    fn slapi_plugin_task_unregister_handler(
        ident: *const c_char,
        cb: extern "C" fn(
            *const libc::c_void,
            *const libc::c_void,
            *const libc::c_void,
            *mut i32,
            *mut c_char,
            *const libc::c_void,
        ) -> i32,
    ) -> i32;
    fn slapi_task_set_destructor_fn(
        task: *const libc::c_void,
        cb: extern "C" fn(*const libc::c_void),
    );
}

pub struct TaskRef {
    raw_task: *const libc::c_void,
}

pub struct Task {
    value: TaskRef,
}

// Because raw pointers are not send, but we need to send the task to a thread
// as part of the task thread spawn, we need to convince the compiler this
// action is okay. It's probably not because C is terrible, BUT provided the
// server and framework only touch the ref count, we are okay.
unsafe impl Send for Task {}

pub fn task_register_handler_fn(
    ident: &'static str,
    cb: extern "C" fn(
        *const libc::c_void,
        *const libc::c_void,
        *const libc::c_void,
        *mut i32,
        *mut c_char,
        *const libc::c_void,
    ) -> i32,
    pb: &mut PblockRef,
) -> i32 {
    let cstr = CString::new(ident).expect("Invalid ident provided");
    unsafe { slapi_plugin_task_register_handler(cstr.as_ptr(), cb, pb.as_ptr()) }
}

pub fn task_unregister_handler_fn(
    ident: &'static str,
    cb: extern "C" fn(
        *const libc::c_void,
        *const libc::c_void,
        *const libc::c_void,
        *mut i32,
        *mut c_char,
        *const libc::c_void,
    ) -> i32,
) -> i32 {
    let cstr = CString::new(ident).expect("Invalid ident provided");
    unsafe { slapi_plugin_task_unregister_handler(cstr.as_ptr(), cb) }
}

impl Task {
    pub fn new(e: &EntryRef, arg: *const libc::c_void) -> Self {
        let sdn = e.get_sdnref();
        let ndn = unsafe { sdn.as_ndnref() };
        let raw_task = unsafe { slapi_plugin_new_task(ndn.as_ptr(), arg) };
        unsafe { slapi_task_inc_refcount(raw_task) };
        Task {
            value: TaskRef { raw_task },
        }
    }

    pub fn begin(&self) {
        // Indicate we begin
        unsafe { slapi_task_begin(self.value.raw_task, 1) }
    }

    pub fn register_destructor_fn(&mut self, cb: extern "C" fn(*const libc::c_void)) {
        unsafe {
            slapi_task_set_destructor_fn(self.value.raw_task, cb);
        }
    }

    pub fn success(self) {
        unsafe {
            slapi_task_finish(self.value.raw_task, LDAP_SUCCESS);
        }
    }

    pub fn error(self, rc: i32) {
        unsafe { slapi_task_finish(self.value.raw_task, rc) };
    }
}

impl Drop for Task {
    fn drop(&mut self) {
        unsafe {
            slapi_task_dec_refcount(self.value.raw_task);
        }
    }
}

impl TaskRef {
    pub fn new(raw_task: *const libc::c_void) -> Self {
        TaskRef { raw_task }
    }

    pub fn block(&self) {
        // wait for the refcount to go to 0.
        let d = Duration::from_millis(250);
        loop {
            if unsafe { slapi_task_get_refcount(self.raw_task) } > 0 {
                thread::sleep(d);
            } else {
                return;
            }
        }
    }
}
