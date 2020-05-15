// It's important that symbol names here are *unique* and do no conflict with symbol
// names in ../../librnsslapd/src/lib.rs
//
// Remember this is just a c-bindgen stub, all logic should come from slapd!

extern crate libc;
extern crate concread;

use slapd;

use libc::c_char;
use std::ffi::{CStr, CString};

mod cache;

#[no_mangle]
pub extern "C" fn do_nothing_rust() -> usize {
    0
}

#[no_mangle]
pub extern "C" fn rust_free_string(s: *mut c_char) {
    if !s.is_null() {
        let _ = unsafe { CString::from_raw(s) };
    }
}

#[no_mangle]
pub extern "C" fn fernet_generate_new_key() -> *mut c_char {
    // It's important to note, we can't return the cstring here, we have to strdup
    // it so that the caller can free it.
    let res_key = slapd::fernet::generate_new_key();
    // While we have a rich error type, we can't do much with it over the ffi, so
    // discard it here (for now). When we impl logging in rust it will be easier to
    // then consume this error type.
    match res_key {
        Ok(key) => {
            let raw = key.into_raw();
            let dup_key = unsafe { libc::strdup(raw) };
            rust_free_string(raw);
            dup_key
        }
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn fernet_validate_key(raw_key: *const c_char) -> bool {
    let c_str_key = unsafe { CStr::from_ptr(raw_key) };
    match slapd::fernet::new(c_str_key) {
        Ok(_) => true,
        Err(_) => false,
    }
}
