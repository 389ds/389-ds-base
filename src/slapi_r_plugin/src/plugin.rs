use crate::constants::{PluginType, PLUGIN_DEFAULT_PRECEDENCE};
use crate::dn::Sdn;
use crate::entry::EntryRef;
use crate::error::LDAPError;
use crate::error::PluginError;
use crate::pblock::PblockRef;
use crate::task::Task;
use libc;
use std::ffi::CString;
use std::os::raw::c_char;
use std::ptr;

extern "C" {
    fn slapi_register_plugin_ext(
        plugintype: *const c_char,
        enabled: i32,
        initsymbol: *const c_char,
        initfunc: *const libc::c_void,
        name: *const c_char,
        argv: *const *const c_char,
        group_identity: *const libc::c_void,
        precedence: i32,
    ) -> i32;
}

pub struct PluginIdRef {
    pub raw_pid: *const libc::c_void,
}

pub fn register_plugin_ext(
    ptype: PluginType,
    plugname: &str,
    initfnname: &str,
    initfn: extern "C" fn(*const libc::c_void) -> i32,
) -> i32 {
    let c_plugname = match CString::new(plugname) {
        Ok(c) => c,
        Err(_) => return 1,
    };
    let c_initfnname = match CString::new(initfnname) {
        Ok(c) => c,
        Err(_) => return 1,
    };
    let argv = [c_plugname.as_ptr(), ptr::null()];
    let value_ptr: *const libc::c_void = initfn as *const libc::c_void;

    unsafe {
        slapi_register_plugin_ext(
            ptype.to_char_ptr(),
            1,
            c_initfnname.as_ptr(),
            value_ptr,
            c_plugname.as_ptr(),
            &argv as *const *const c_char,
            ptr::null(),
            PLUGIN_DEFAULT_PRECEDENCE,
        )
    }
}

pub trait SlapiPlugin3 {
    // We require a newer rust for default associated types.
    // type TaskData = ();
    type TaskData;

    fn has_pre_modify() -> bool {
        false
    }

    fn has_post_modify() -> bool {
        false
    }

    fn has_pre_add() -> bool {
        false
    }

    fn has_post_add() -> bool {
        false
    }

    fn has_betxn_pre_modify() -> bool {
        false
    }

    fn has_betxn_pre_add() -> bool {
        false
    }

    fn has_task_handler() -> Option<&'static str> {
        None
    }

    fn has_pwd_storage() -> bool {
        false
    }

    fn start(_pb: &mut PblockRef) -> Result<(), PluginError>;

    fn close(_pb: &mut PblockRef) -> Result<(), PluginError>;

    fn betxn_pre_modify(_pb: &mut PblockRef) -> Result<(), PluginError> {
        Err(PluginError::Unimplemented)
    }

    fn betxn_pre_add(_pb: &mut PblockRef) -> Result<(), PluginError> {
        Err(PluginError::Unimplemented)
    }

    fn task_validate(_e: &EntryRef) -> Result<Self::TaskData, LDAPError> {
        Err(LDAPError::Other)
    }

    fn task_be_dn_hint(_data: &Self::TaskData) -> Option<Sdn> {
        None
    }

    fn task_handler(_task: &Task, _data: Self::TaskData) -> Result<Self::TaskData, PluginError> {
        Err(PluginError::Unimplemented)
    }

    fn pwd_scheme_name() -> &'static str {
        panic!("Unimplemented pwd_scheme_name for password storage plugin")
    }

    fn pwd_storage_encrypt(_cleartext: &str) -> Result<String, PluginError> {
        Err(PluginError::Unimplemented)
    }

    fn pwd_storage_compare(_cleartext: &str, _encrypted: &str) -> Result<bool, PluginError> {
        Err(PluginError::Unimplemented)
    }
}
