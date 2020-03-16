// BEGIN COPYRIGHT BLOCK
// Copyright (c) 2017, Red Hat, Inc
// All rights reserved.
//
// License: GPL (version 3 or any later version).
// See LICENSE for details.
// END COPYRIGHT BLOCK

#![warn(missing_docs)]

use super::sds_result;
use std::collections::LinkedList;
use std::sync::Mutex;

// Borrow from libc
#[doc(hidden)]
#[allow(non_camel_case_types)]
#[repr(u8)]
pub enum c_void {
    // Two dummy variants so the #[repr] attribute can be used.
    #[doc(hidden)]
    __Variant1,
    #[doc(hidden)]
    __Variant2,
}

/// A thread safe queue. This is made to be compatible with the tqueue api
/// provided by libsds as a proof of concept. As a result it contains some C-isms
/// like holding a free function pointer (rather than drop trait).
pub struct TQueue {
    q: Mutex<LinkedList<*const c_void>>,
    free_fn: Option<extern "C" fn(*const c_void)>,
}

impl TQueue {
    /// Allocate a new thread safe queue. If the free function is provided
    /// on drop of the TQueue, this function will be called on all remaining
    /// elements of the queue.
    pub fn new(free_fn: Option<extern "C" fn(*const c_void)>) -> Self {
        TQueue {
            q: Mutex::new(LinkedList::new()),
            free_fn: free_fn,
        }
    }

    /// Push a pointer into the tail of the queue.
    pub fn enqueue(&self, elem: *const c_void) {
        let mut q_inner = self.q.lock().unwrap();
        q_inner.push_back(elem);
    }

    /// Dequeue the head element of the queue. If not element
    /// exists return None.
    pub fn dequeue(&self) -> Option<*const c_void> {
        let mut q_inner = self.q.lock().unwrap();
        q_inner.pop_front()
    }
}

impl Drop for TQueue {
    fn drop(&mut self) {
        println!("droping tqueue");
        if let Some(f) = self.free_fn {
            let mut q_inner = self.q.lock().unwrap();
            let mut elem = (*q_inner).pop_front();
            while elem.is_some() {
                (f)(elem.unwrap());
                elem = (*q_inner).pop_front();
            }
        }
    }
}

#[no_mangle]
/// C compatible wrapper around the TQueue. Given a valid point, a TQueue pointer
/// is allocated on the heap and referenced in retq. free_fn_ptr may be NULL
/// but if it references a function, this will be called during drop of the TQueue.
pub extern "C" fn sds_tqueue_init(
    retq: *mut *mut TQueue,
    free_fn_ptr: Option<extern "C" fn(*const c_void)>,
) -> sds_result {
    // This piece of type signature magic is because in rust types that extern C,
    // with option has None resolve to null. What this causes is we can wrap
    // our fn ptr with Option in rust, but the C side gives us fn ptr or NULL, and
    // it *works*. It makes the result complete safe on the rust side too!
    if retq.is_null() {
        return sds_result::NullPointer;
    }

    let q = Box::new(TQueue::new(free_fn_ptr));
    unsafe {
        *retq = Box::into_raw(q);
    }
    sds_result::Success
}

#[no_mangle]
/// Push an element to the tail of the queue. The element may be NULL
pub extern "C" fn sds_tqueue_enqueue(q: *const TQueue, elem: *const c_void) -> sds_result {
    // Check for null ....
    unsafe { (*q).enqueue(elem) };
    sds_result::Success
}

#[no_mangle]
/// Dequeue from the head of the queue. The result will be placed into elem.
/// if elem is NULL no dequeue is attempted. If there are no more items
/// ListExhausted is returned.
pub extern "C" fn sds_tqueue_dequeue(q: *const TQueue, elem: *mut *const c_void) -> sds_result {
    if elem.is_null() {
        return sds_result::NullPointer;
    }
    match unsafe { (*q).dequeue() } {
        Some(e) => {
            unsafe {
                *elem = e;
            };
            sds_result::Success
        }
        None => sds_result::ListExhausted,
    }
}

#[no_mangle]
/// Free the queue and all remaining elements. After this point it is
/// not safe to access the queue.
pub extern "C" fn sds_tqueue_destroy(q: *mut TQueue) -> sds_result {
    // This will drop the queue and free it's content
    // mem::drop(q);
    let _q = unsafe { Box::from_raw(q) };
    sds_result::Success
}
