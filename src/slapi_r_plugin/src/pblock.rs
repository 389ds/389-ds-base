use libc;
use std::ops::{Deref, DerefMut};
use std::os::raw::c_char;
use std::ptr;

use crate::backend::BackendRef;
use crate::constants::{PblockType, PluginFnType, PluginVersion};
use crate::entry::EntryRef;
pub use crate::log::{log_error, ErrorLevel};

extern "C" {
    fn slapi_pblock_set(pb: *const libc::c_void, arg: i32, value: *const libc::c_void) -> i32;
    fn slapi_pblock_get(pb: *const libc::c_void, arg: i32, value: *const libc::c_void) -> i32;
    fn slapi_pblock_destroy(pb: *const libc::c_void);
    fn slapi_pblock_new() -> *const libc::c_void;
}

pub struct Pblock {
    value: PblockRef,
}

impl Pblock {
    pub fn new() -> Pblock {
        let raw_pb = unsafe { slapi_pblock_new() };
        Pblock {
            value: PblockRef { raw_pb },
        }
    }
}

impl Deref for Pblock {
    type Target = PblockRef;

    fn deref(&self) -> &Self::Target {
        &self.value
    }
}

impl DerefMut for Pblock {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.value
    }
}

impl Drop for Pblock {
    fn drop(&mut self) {
        unsafe { slapi_pblock_destroy(self.value.raw_pb) }
    }
}

pub struct PblockRef {
    raw_pb: *const libc::c_void,
}

impl PblockRef {
    pub fn new(raw_pb: *const libc::c_void) -> Self {
        PblockRef { raw_pb }
    }

    pub unsafe fn as_ptr(&self) -> *const libc::c_void {
        self.raw_pb
    }

    fn set_pb_char_arr_ptr(&mut self, req_type: PblockType, ptr: *const *const c_char) -> i32 {
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe { slapi_pblock_set(self.raw_pb, req_type as i32, value_ptr) }
    }

    fn set_pb_char_ptr(&mut self, req_type: PblockType, ptr: *const c_char) -> i32 {
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe { slapi_pblock_set(self.raw_pb, req_type as i32, value_ptr) }
    }

    fn set_pb_fn_ptr(
        &mut self,
        fn_type: PluginFnType,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe { slapi_pblock_set(self.raw_pb, fn_type as i32, value_ptr) }
    }

    fn get_value_ptr(&mut self, req_type: PblockType) -> Result<*const libc::c_void, ()> {
        let mut value: *mut libc::c_void = ptr::null::<libc::c_void>() as *mut libc::c_void;
        let value_ptr: *const libc::c_void = &mut value as *const _ as *const libc::c_void;
        match unsafe { slapi_pblock_get(self.raw_pb, req_type as i32, value_ptr) } {
            0 => Ok(value),
            e => {
                log_error!(ErrorLevel::Error, "enable to get from pblock -> {:?}", e);
                Err(())
            }
        }
    }

    fn get_value_i32(&mut self, req_type: PblockType) -> Result<i32, ()> {
        let mut value: i32 = 0;
        let value_ptr: *const libc::c_void = &mut value as *const _ as *const libc::c_void;
        match unsafe { slapi_pblock_get(self.raw_pb, req_type as i32, value_ptr) } {
            0 => Ok(value),
            e => {
                log_error!(ErrorLevel::Error, "enable to get from pblock -> {:?}", e);
                Err(())
            }
        }
    }

    pub fn register_start_fn(&mut self, ptr: extern "C" fn(*const libc::c_void) -> i32) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::Start, ptr)
    }

    pub fn register_close_fn(&mut self, ptr: extern "C" fn(*const libc::c_void) -> i32) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::Close, ptr)
    }

    pub fn register_betxn_pre_add_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::BeTxnPreAdd, ptr)
    }

    pub fn register_betxn_pre_modify_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::BeTxnPreModify, ptr)
    }

    pub fn register_syntax_filter_ava_fn(
        &mut self,
        ptr: extern "C" fn(
            *const core::ffi::c_void,
            *const core::ffi::c_void,
            *const core::ffi::c_void,
            i32,
            *mut core::ffi::c_void,
        ) -> i32,
    ) -> i32 {
        // We can't use self.set_pb_fn_ptr here as the fn type sig is different.
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe { slapi_pblock_set(self.raw_pb, PluginFnType::SyntaxFilterAva as i32, value_ptr) }
    }

    pub fn register_syntax_values2keys_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::SyntaxValuesToKeys, ptr)
    }

    pub fn register_syntax_assertion2keys_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::SyntaxAssertion2KeysAva, ptr)
    }

    pub fn register_syntax_flags_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::SyntaxFlags, ptr)
    }

    pub fn register_syntax_oid(&mut self, ptr: *const c_char) -> i32 {
        self.set_pb_char_ptr(PblockType::SyntaxOid, ptr)
    }

    pub fn register_syntax_compare_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::SyntaxCompare, ptr)
    }

    pub fn register_syntax_validate_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::SyntaxValidate, ptr)
    }

    pub fn register_syntax_names(&mut self, arr_ptr: *const *const c_char) -> i32 {
        self.set_pb_char_arr_ptr(PblockType::SyntaxNames, arr_ptr)
    }

    pub fn register_mr_filter_create_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::MRFilterCreate, ptr)
    }

    pub fn register_mr_indexer_create_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::MRIndexerCreate, ptr)
    }

    pub fn register_mr_filter_ava_fn(
        &mut self,
        ptr: extern "C" fn(
            *const core::ffi::c_void,
            *const core::ffi::c_void,
            *const core::ffi::c_void,
            i32,
            *mut core::ffi::c_void,
        ) -> i32,
    ) -> i32 {
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe { slapi_pblock_set(self.raw_pb, PluginFnType::MRFilterAva as i32, value_ptr) }
    }

    pub fn register_mr_filter_sub_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::MRFilterSub, ptr)
    }

    pub fn register_mr_values2keys_fn(
        &mut self,
        ptr: extern "C" fn(
            *const core::ffi::c_void,
            *const core::ffi::c_void,
            *mut core::ffi::c_void,
            i32,
        ) -> i32,
    ) -> i32 {
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe { slapi_pblock_set(self.raw_pb, PluginFnType::MRValuesToKeys as i32, value_ptr) }
    }

    pub fn register_mr_assertion2keys_ava_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::MRAssertionToKeysAva, ptr)
    }

    pub fn register_mr_assertion2keys_sub_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void) -> i32,
    ) -> i32 {
        self.set_pb_fn_ptr(PluginFnType::MRAssertionToKeysSub, ptr)
    }

    pub fn register_mr_compare_fn(
        &mut self,
        ptr: extern "C" fn(*const libc::c_void, *const libc::c_void) -> i32,
    ) -> i32 {
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe { slapi_pblock_set(self.raw_pb, PluginFnType::MRCompare as i32, value_ptr) }
    }

    pub fn register_mr_names(&mut self, arr_ptr: *const *const c_char) -> i32 {
        self.set_pb_char_arr_ptr(PblockType::MRNames, arr_ptr)
    }

    pub fn register_pwd_storage_encrypt_fn(
        &mut self,
        ptr: extern "C" fn(*const c_char) -> *const c_char,
    ) -> i32 {
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe {
            slapi_pblock_set(
                self.raw_pb,
                PluginFnType::PwdStorageEncrypt as i32,
                value_ptr,
            )
        }
    }

    pub fn register_pwd_storage_compare_fn(
        &mut self,
        ptr: extern "C" fn(*const c_char, *const c_char) -> i32,
    ) -> i32 {
        let value_ptr: *const libc::c_void = ptr as *const libc::c_void;
        unsafe {
            slapi_pblock_set(
                self.raw_pb,
                PluginFnType::PwdStorageCompare as i32,
                value_ptr,
            )
        }
    }

    pub fn register_pwd_storage_scheme_name(&mut self, ptr: *const c_char) -> i32 {
        self.set_pb_char_ptr(PblockType::PwdStorageSchemeName, ptr)
    }

    pub fn get_op_add_entryref(&mut self) -> Result<EntryRef, ()> {
        self.get_value_ptr(PblockType::AddEntry)
            .map(|ptr| EntryRef::new(ptr))
    }

    pub fn set_plugin_version(&mut self, vers: PluginVersion) -> i32 {
        self.set_pb_char_ptr(PblockType::Version, vers.to_char_ptr())
    }

    pub fn set_op_backend(&mut self, be: &BackendRef) -> i32 {
        unsafe { slapi_pblock_set(self.raw_pb, PblockType::Backend as i32, be.as_ptr()) }
    }

    pub fn get_plugin_identity(&mut self) -> *const libc::c_void {
        self.get_value_ptr(PblockType::Identity)
            .unwrap_or(std::ptr::null())
    }

    pub fn get_op_result(&mut self) -> i32 {
        self.get_value_i32(PblockType::OpResult).unwrap_or(-1)
    }
}
