// It's important that symbol names here are *unique* and do no conflict with symbol
// names in ../../librslapd/src/lib.rs
//
// Remember this is just a c-bindgen stub, all logic should come from slapd!

extern crate libc;
use libc::c_char;
use slapd;
use std::ffi::{CStr, CString};

#[no_mangle]
pub extern "C" fn do_nothing_again_rust() -> usize {
    0
}

#[no_mangle]
pub extern "C" fn fernet_generate_token(dn: *const c_char, raw_key: *const c_char) -> *mut c_char {
    if dn.is_null() || raw_key.is_null() {
        return std::ptr::null_mut();
    }
    // Given a DN, generate a fernet token, or return NULL on error.
    let c_str_key = unsafe { CStr::from_ptr(raw_key) };
    let c_str_dn = unsafe { CStr::from_ptr(dn) };
    match slapd::fernet::new(c_str_key) {
        Ok(inst) => {
            // We have an instance, let's make the token.
            match slapd::fernet::encrypt(&inst, c_str_dn) {
                Ok(tok) => {
                    // We have to move string memory ownership by copying so the system
                    // allocator has it.
                    let raw = tok.into_raw();
                    let dup_tok = unsafe { libc::strdup(raw) };
                    unsafe {
                        CString::from_raw(raw);
                    };
                    dup_tok
                }
                Err(_) => std::ptr::null_mut(),
            }
        }
        Err(_) => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn fernet_verify_token(
    dn: *const c_char,
    token: *const c_char,
    raw_key: *const c_char,
    ttl: u64,
) -> bool {
    if dn.is_null() || raw_key.is_null() || token.is_null() {
        return false;
    }

    let c_str_key = unsafe { CStr::from_ptr(raw_key) };
    let c_str_dn = unsafe { CStr::from_ptr(dn) };
    let c_str_token = unsafe { CStr::from_ptr(token) };

    match slapd::fernet::new(c_str_key) {
        Ok(inst) => {
            match slapd::fernet::decrypt(&inst, c_str_token, ttl) {
                Ok(val) => {
                    // Finally check if the extracted dn is what we expect
                    val.as_c_str() == c_str_dn
                }
                Err(_) => false,
            }
        }
        Err(_) => false,
    }
}
