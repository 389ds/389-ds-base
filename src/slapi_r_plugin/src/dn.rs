use std::convert::TryFrom;
use std::ffi::{CStr, CString};
use std::ops::Deref;
use std::os::raw::c_char;

unsafe extern "C" {
    unsafe fn slapi_sdn_get_dn(sdn: *const libc::c_void) -> *const c_char;
    unsafe fn slapi_sdn_new_dn_byval(dn: *const c_char) -> *const libc::c_void;
    unsafe fn slapi_sdn_issuffix(sdn: *const libc::c_void, suffix_sdn: *const libc::c_void) -> i32;
    unsafe fn slapi_sdn_free(sdn: *const *const libc::c_void);
    unsafe fn slapi_sdn_dup(sdn: *const libc::c_void) -> *const libc::c_void;
}

#[derive(Debug)]
pub struct SdnRef {
    raw_sdn: *const libc::c_void,
}

#[derive(Debug)]
pub struct NdnRef {
    raw_ndn: *const c_char,
}

#[derive(Debug)]
pub struct Sdn {
    value: SdnRef,
}

unsafe impl Send for Sdn {}

impl From<&CStr> for Sdn {
    fn from(value: &CStr) -> Self {
        Self {
            value: SdnRef {
                raw_sdn: unsafe { slapi_sdn_new_dn_byval(value.as_ptr()) },
            },
        }
    }
}

impl TryFrom<&str> for Sdn {
    type Error = ();

    fn try_from(value: &str) -> Result<Self, Self::Error> {
        let cstr: CString = CString::new(value).map_err(|_| ())?;
        Ok(Self::from(cstr.as_c_str()))
    }
}

impl Clone for Sdn {
    fn clone(&self) -> Self {
        let raw_sdn: *const libc::c_void = unsafe { slapi_sdn_dup(self.value.raw_sdn) };
        Self {
            value: SdnRef { raw_sdn },
        }
    }
}

impl Drop for Sdn {
    fn drop(&mut self) {
        unsafe { slapi_sdn_free(&self.value.raw_sdn) }
    }
}

impl Deref for Sdn {
    type Target = SdnRef;

    fn deref(&self) -> &Self::Target {
        &self.value
    }
}

impl SdnRef {
    pub const fn new(raw_sdn: *const libc::c_void) -> Self {
        Self { raw_sdn }
    }

    /// This is unsafe, as you need to ensure that the `SdnRef` associated lives at
    /// least as long as the `NdnRef`, else this may cause a use-after-free.
    pub unsafe fn as_ndnref(&self) -> NdnRef {
        let raw_ndn: *const c_char = slapi_sdn_get_dn(self.raw_sdn);
        NdnRef { raw_ndn }
    }

    pub fn to_dn_string(&self) -> String {
        let dn_raw: *const c_char = unsafe { slapi_sdn_get_dn(self.raw_sdn) };
        let dn_cstr: &CStr = unsafe { CStr::from_ptr(dn_raw) };
        dn_cstr.to_string_lossy().to_string()
    }

    pub(crate) const fn as_ptr(&self) -> *const libc::c_void {
        self.raw_sdn
    }

    pub fn is_below_suffix(&self, other: &Self) -> bool {
        (unsafe { slapi_sdn_issuffix(self.raw_sdn, other.raw_sdn) } != 0)
    }
}

impl NdnRef {
    pub(crate) const fn as_ptr(&self) -> *const c_char {
        self.raw_ndn
    }
}
