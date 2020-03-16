use crate::log::{log_error, ErrorLevel};
use libc;
use std::ffi::CString;
// use std::ptr;
use std::slice;

use std::convert::TryFrom;
use uuid::Uuid;

use crate::error::PluginError;

#[repr(C)]
pub(crate) struct ol_berval {
    pub len: usize,
    pub data: *const u8,
}

#[derive(Debug)]
pub struct BerValRef {
    pub(crate) raw_berval: *const ol_berval,
}

impl BerValRef {
    pub fn new(raw_berval: *const libc::c_void) -> Self {
        // so we retype this
        let raw_berval = raw_berval as *const ol_berval;
        BerValRef { raw_berval }
    }

    pub(crate) fn into_cstring(&self) -> Option<CString> {
        // Cstring does not need a trailing null, so if we have one, ignore it.
        let l: usize = unsafe { (*self.raw_berval).len };
        let d_slice = unsafe { slice::from_raw_parts((*self.raw_berval).data, l) };
        CString::new(d_slice)
            .or_else(|e| {
                // Try it again, but with one byte less to trim a potential trailing null that
                // could have been allocated, and ensure it has at least 1 byte of good data
                // remaining.
                if l > 1 {
                    let d_slice = unsafe { slice::from_raw_parts((*self.raw_berval).data, l - 1) };
                    CString::new(d_slice)
                } else {
                    Err(e)
                }
            })
            .map_err(|_| {
                log_error!(
                    ErrorLevel::Trace,
                    "invalid ber parse attempt, may contain a null byte? -> {:?}",
                    self
                );
                ()
            })
            .ok()
    }

    pub fn into_string(&self) -> Option<String> {
        // Convert a Some to a rust string.
        self.into_cstring().and_then(|v| {
            v.into_string()
                .map_err(|_| {
                    log_error!(
                        ErrorLevel::Trace,
                        "failed to convert cstring to string -> {:?}",
                        self
                    );
                    ()
                })
                .ok()
        })
    }
}

impl TryFrom<&BerValRef> for Uuid {
    type Error = PluginError;

    fn try_from(value: &BerValRef) -> Result<Self, Self::Error> {
        let val_string = value.into_string().ok_or(PluginError::BervalString)?;

        Uuid::parse_str(val_string.as_str())
            .map(|r| {
                log_error!(ErrorLevel::Trace, "valid uuid -> {:?}", r);
                r
            })
            .map_err(|_e| {
                log_error!(ErrorLevel::Plugin, "Invalid uuid");
                PluginError::InvalidSyntax
            })
    }
}
