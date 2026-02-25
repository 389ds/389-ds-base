use crate::ber::{ol_berval, BerValRef};
use crate::dn::Sdn;
use std::convert::{From, TryFrom, TryInto};
use std::ffi::{CString, CStr};
use std::iter::once;
use std::iter::FromIterator;
use std::mem;
use std::ops::Deref;
use std::os::raw::c_char;
use std::ptr;
use uuid::Uuid;

unsafe extern "C" {
    unsafe fn slapi_value_new() -> *mut slapi_value;
    unsafe fn slapi_value_free(v: *mut *const libc::c_void);
}

#[repr(C)]
/// From ./ldap/servers/slapd/slap.h
pub struct slapi_value {
    bv: ol_berval,
    v_csnset: *const libc::c_void,
    v_flags: u32,
}

pub struct ValueArrayRefIter<'a> {
    idx: isize,
    va_ref: &'a ValueArrayRef,
}

impl<'a> Iterator for ValueArrayRefIter<'a> {
    type Item = ValueRef;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        // So long as va_ref.raw_slapi_val + offset != NULL, continue.
        // this is so wildly unsafe, but you know, that's just daily life of C anyway ...
        unsafe {
            let n_ptr: *const slapi_value = *(self.va_ref.raw_slapi_val.offset(self.idx));
            if n_ptr.is_null() {
                None
            } else {
                // Advance the iter.
                self.idx += 1;
                let raw_berval: *const ol_berval = &(*n_ptr).bv;
                Some(Self::Item {
                    raw_slapi_val: n_ptr,
                    bvr: BerValRef { raw_berval },
                })
            }
        }
    }
}

pub struct ValueArrayRef {
    raw_slapi_val: *const *const slapi_value,
}

impl ValueArrayRef {
    pub const fn new(raw_slapi_val: *const libc::c_void) -> Self {
        let raw_slapi_val: *const *const slapi_value = raw_slapi_val.cast();
        Self { raw_slapi_val }
    }

    pub const fn iter(&self) -> ValueArrayRefIter<'_> {
        ValueArrayRefIter {
            idx: 0,
            va_ref: self,
        }
    }

    pub fn first(&self) -> Option<ValueRef> {
        self.iter().next()
    }
}

pub struct ValueArray {
    data: Vec<*mut slapi_value>,
    vrf: ValueArrayRef,
}

impl Deref for ValueArray {
    type Target = ValueArrayRef;

    fn deref(&self) -> &Self::Target {
        &self.vrf
    }
}

impl ValueArray {
    /// Take ownership of this value array, returning the pointer to the inner memory
    /// and forgetting about it for ourself. This prevents the drop handler from freeing
    /// the `slapi_value`, ie we are giving this to the 389-ds framework to manage from now.
    pub unsafe fn take_ownership(mut self) -> *const *const slapi_value {
        let mut vs: Vec<*mut slapi_value> = vec![];
        mem::swap(&mut self.data, &mut vs);
        let bs: Box<[*mut slapi_value]> = vs.into_boxed_slice();
        Box::leak(bs) as *const _ as *const *const slapi_value
    }

    pub fn as_ptr(&self) -> *const *const slapi_value {
        self.data.as_ptr().cast()
    }
}

impl FromIterator<Value> for ValueArray {
    fn from_iter<I: IntoIterator<Item = Value>>(iter: I) -> Self {
        let data: Vec<*mut slapi_value> = iter
            .into_iter()
            .map(|v| unsafe { v.take_ownership() })
            .chain(once(ptr::null_mut::<slapi_value>()))
            .collect();
        let vrf: ValueArrayRef = ValueArrayRef {
            raw_slapi_val: data.as_ptr().cast(),
        };
        Self { data, vrf }
    }
}

impl Drop for ValueArray {
    fn drop(&mut self) {
        self.data.drain(0..).for_each(|mut v| unsafe {
            slapi_value_free((&raw mut v).cast());
        });
    }
}

#[derive(Debug)]
pub struct ValueRef {
    raw_slapi_val: *const slapi_value,
    bvr: BerValRef,
}

impl ValueRef {
    pub(crate) const unsafe fn as_ptr(&self) -> *const slapi_value {
        // This is unsafe as the *const may outlive the value ref.
        self.raw_slapi_val
    }
}

pub struct Value {
    value: ValueRef,
}

impl Value {
    pub unsafe fn take_ownership(mut self) -> *mut slapi_value {
        let mut n_ptr: *const slapi_value = ptr::null();
        mem::swap(&mut self.value.raw_slapi_val, &mut n_ptr);
        n_ptr.cast_mut()
        // Now drop will run and not care.
    }
}

impl Drop for Value {
    fn drop(&mut self) {
        if !self.value.raw_slapi_val.is_null() {
            // free it
            unsafe {
                slapi_value_free(
                    (&raw mut self.value.raw_slapi_val).cast(),
                );
            }
        }
    }
}

impl Deref for Value {
    type Target = ValueRef;

    fn deref(&self) -> &Self::Target {
        &self.value
    }
}

impl From<&Uuid> for Value {
    fn from(u: &Uuid) -> Self {
        // turn the uuid to a str
        let u_str: String = u.to_hyphenated().to_string();
        let len: usize = u_str.len();
        let cstr: Box<CStr> = CString::new(u_str)
            .expect("Invalid uuid, should never occur!")
            .into_boxed_c_str();
        let s_ptr: *const c_char = cstr.as_ptr();
        Box::leak(cstr);

        let v: *mut slapi_value = unsafe { slapi_value_new() };
        unsafe {
            (*v).bv.len = len;
            (*v).bv.data = s_ptr.cast::<u8>();
        }

        Self {
            value: ValueRef::new(v.cast()),
        }
    }
}

impl ValueRef {
    pub const fn new(raw_slapi_val: *const libc::c_void) -> Self {
        let raw_slapi_val: *const slapi_value = raw_slapi_val.cast();
        let raw_berval: *const ol_berval = unsafe { &(*raw_slapi_val).bv };
        Self {
            raw_slapi_val,
            bvr: BerValRef { raw_berval },
        }
    }
}

impl TryFrom<&ValueRef> for String {
    type Error = ();

    fn try_from(value: &ValueRef) -> Result<Self, Self::Error> {
        value.bvr.into_string().ok_or(())
    }
}

impl TryFrom<&ValueRef> for Uuid {
    type Error = ();

    fn try_from(value: &ValueRef) -> Result<Self, Self::Error> {
        (&value.bvr).try_into().map_err(|_| ())
    }
}

impl TryFrom<&ValueRef> for Sdn {
    type Error = ();

    fn try_from(value: &ValueRef) -> Result<Self, Self::Error> {
        // We need to do a middle step of moving through a cstring as
        // bervals may not always have a trailing NULL, and sdn expects one.
        let cdn: CString = value.bvr.into_cstring().ok_or(())?;
        Ok(cdn.as_c_str().into())
    }
}

impl AsRef<Self> for ValueRef {
    fn as_ref(&self) -> &Self {
        self
    }
}

impl Deref for ValueRef {
    type Target = BerValRef;

    fn deref(&self) -> &Self::Target {
        &self.bvr
    }
}
