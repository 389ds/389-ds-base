use std::ffi::CString;
use std::iter::once;
use std::os::raw::c_char;
use std::ptr;

pub struct Charray {
    _pin: Vec<CString>,
    charray: Vec<*const c_char>,
}

impl Charray {
    pub fn new(input: &[&str]) -> Result<Self, ()> {
        let pin: Result<Vec<_>, ()> = input
            .iter()
            .map(|s| CString::new(*s).map_err(|_e| ()))
            .collect();

        let pin = pin?;

        let charray: Vec<_> = pin
            .iter()
            .map(|s| s.as_ptr())
            .chain(once(ptr::null()))
            .collect();

        Ok(Charray { _pin: pin, charray })
    }

    pub fn as_ptr(&self) -> *const *const c_char {
        self.charray.as_ptr()
    }
}
