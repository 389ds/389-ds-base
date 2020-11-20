use crate::constants::OpFlags;
use crate::dn::SdnRef;
use crate::error::{LDAPError, PluginError};
use crate::pblock::Pblock;
use crate::plugin::PluginIdRef;
use crate::value::{slapi_value, ValueArray};

use std::ffi::CString;
use std::ops::Deref;
use std::os::raw::c_char;

extern "C" {
    fn slapi_modify_internal_set_pb_ext(
        pb: *const libc::c_void,
        dn: *const libc::c_void,
        mods: *const *const libc::c_void,
        controls: *const *const libc::c_void,
        uniqueid: *const c_char,
        plugin_ident: *const libc::c_void,
        op_flags: i32,
    );
    fn slapi_modify_internal_pb(pb: *const libc::c_void);
    fn slapi_mods_free(smods: *const *const libc::c_void);
    fn slapi_mods_get_ldapmods_byref(smods: *const libc::c_void) -> *const *const libc::c_void;
    fn slapi_mods_new() -> *const libc::c_void;
    fn slapi_mods_add_mod_values(
        smods: *const libc::c_void,
        mtype: i32,
        attrtype: *const c_char,
        value: *const *const slapi_value,
    );
}

#[derive(Debug)]
#[repr(i32)]
pub enum ModType {
    Add = 0,
    Delete = 1,
    Replace = 2,
}

pub struct SlapiMods {
    inner: *const libc::c_void,
    vas: Vec<ValueArray>,
}

impl Drop for SlapiMods {
    fn drop(&mut self) {
        unsafe { slapi_mods_free(&self.inner as *const *const libc::c_void) }
    }
}

impl SlapiMods {
    pub fn new() -> Self {
        SlapiMods {
            inner: unsafe { slapi_mods_new() },
            vas: Vec::new(),
        }
    }

    pub fn append(&mut self, modtype: ModType, attrtype: &str, values: ValueArray) {
        // We can get the value array pointer here to push to the inner
        // because the internal pointers won't change even when we push them
        // to the list to preserve their lifetime.
        let vas = values.as_ptr();
        // We take ownership of this to ensure it lives as least as long as our
        // slapimods structure.
        self.vas.push(values);
        // now we can insert these to the modes.
        let c_attrtype = CString::new(attrtype).expect("failed to allocate attrtype");
        unsafe { slapi_mods_add_mod_values(self.inner, modtype as i32, c_attrtype.as_ptr(), vas) };
    }
}

pub struct Modify {
    pb: Pblock,
    mods: SlapiMods,
}

pub struct ModifyResult {
    _pb: Pblock,
}

impl Modify {
    pub fn new(dn: &SdnRef, mods: SlapiMods, plugin_id: PluginIdRef) -> Result<Self, PluginError> {
        let pb = Pblock::new();
        let lmods = unsafe { slapi_mods_get_ldapmods_byref(mods.inner) };
        // OP_FLAG_ACTION_LOG_ACCESS

        unsafe {
            slapi_modify_internal_set_pb_ext(
                pb.deref().as_ptr(),
                dn.as_ptr(),
                lmods,
                std::ptr::null(),
                std::ptr::null(),
                plugin_id.raw_pid,
                OpFlags::ByassReferrals as i32,
            )
        };

        Ok(Modify { pb, mods })
    }

    pub fn execute(self) -> Result<ModifyResult, LDAPError> {
        let Modify {
            mut pb,
            mods: _mods,
        } = self;
        unsafe { slapi_modify_internal_pb(pb.deref().as_ptr()) };
        let result = pb.get_op_result();

        match result {
            0 => Ok(ModifyResult { _pb: pb }),
            _e => Err(LDAPError::from(result)),
        }
    }
}
