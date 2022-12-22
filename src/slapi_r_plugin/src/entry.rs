use crate::dn::SdnRef;
use crate::value::{slapi_value, ValueArrayRef, ValueRef};
use std::ffi::CString;
use std::os::raw::c_char;

extern "C" {
    fn slapi_entry_get_sdn(e: *const libc::c_void) -> *const libc::c_void;
    fn slapi_entry_add_value(
        e: *const libc::c_void,
        a: *const c_char,
        v: *const slapi_value,
    ) -> i32;
    fn slapi_entry_attr_get_valuearray(
        e: *const libc::c_void,
        a: *const c_char,
    ) -> *const *const slapi_value;
}

pub struct EntryRef {
    raw_e: *const libc::c_void,
}

/*
pub struct Entry {
    value: EntryRef,
}

impl Drop for Entry {
    fn drop(&mut self) {
        ()
    }
}

impl Deref for Entry {
    type Target = EntryRef;

    fn deref(&self) -> &Self::Target {
        &self.value
    }
}

impl Entry {
    // Forget about this value, and get a pointer back suitable for providing to directory
    // server to take ownership.
    pub unsafe fn forget(self) -> *mut libc::c_void {
        unimplemented!();
    }
}
*/

impl EntryRef {
    pub fn new(raw_e: *const libc::c_void) -> Self {
        EntryRef { raw_e }
    }

    // get the sdn
    pub fn get_sdnref(&self) -> SdnRef {
        let sdn_ptr = unsafe { slapi_entry_get_sdn(self.raw_e) };
        SdnRef::new(sdn_ptr)
    }

    pub fn get_attr(&self, name: &str) -> Option<ValueArrayRef> {
        let cname = CString::new(name).expect("invalid attr name");
        let va = unsafe { slapi_entry_attr_get_valuearray(self.raw_e, cname.as_ptr()) };

        if va.is_null() {
            None
        } else {
            Some(ValueArrayRef::new(va as *const libc::c_void))
        }
    }

    pub fn contains_attr(&self, name: &str) -> bool {
        let cname = CString::new(name).expect("invalid attr name");
        let va = unsafe { slapi_entry_attr_get_valuearray(self.raw_e, cname.as_ptr()) };

        // If it's null, it's not present, so flip the logic.
        !va.is_null()
    }

    pub fn add_value(&mut self, a: &str, v: &ValueRef) {
        // turn the attr to a c string.
        // TODO FIX
        let attr_name = CString::new(a).expect("Invalid attribute name");
        // Get the raw ptr.
        let raw_value_ref = unsafe { v.as_ptr() };
        // We ignore the return because it always returns 0.
        let _ = unsafe {
            // By default, this clones.
            slapi_entry_add_value(self.raw_e, attr_name.as_ptr(), raw_value_ref)
        };
    }

    /*
    pub fn replace_value(&mut self, a: &str, v: &ValueRef) {
        // slapi_entry_attr_replace(e, SLAPI_ATTR_ENTRYUSN, new_bvals);
        unimplemented!();
    }
    */
}
