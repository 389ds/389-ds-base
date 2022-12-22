use crate::dn::SdnRef;
use crate::error::{LDAPError, PluginError};
use crate::pblock::Pblock;
use crate::plugin::PluginIdRef;
use std::ffi::CString;
use std::ops::Deref;
use std::os::raw::c_char;

extern "C" {
    fn slapi_search_internal_set_pb_ext(
        pb: *const libc::c_void,
        base: *const libc::c_void,
        scope: i32,
        filter: *const c_char,
        attrs: *const *const c_char,
        attrsonly: i32,
        controls: *const *const libc::c_void,
        uniqueid: *const c_char,
        plugin_ident: *const libc::c_void,
        op_flags: i32,
    );
    fn slapi_search_internal_callback_pb(
        pb: *const libc::c_void,
        cb_data: *const libc::c_void,
        cb_result_ptr: *const libc::c_void,
        cb_entry_ptr: *const libc::c_void,
        cb_referral_ptr: *const libc::c_void,
    ) -> i32;
}

#[derive(Debug)]
#[repr(i32)]
pub enum SearchScope {
    Base = 0,
    Onelevel = 1,
    Subtree = 2,
}

enum SearchType {
    InternalMapEntry(
        extern "C" fn(*const core::ffi::c_void, *const core::ffi::c_void) -> i32,
        *const libc::c_void,
    ),
    // InternalMapResult
    // InternalMapReferral
}

pub struct Search {
    pb: Pblock,
    // This is so that the char * to the pb lives long enough as ds won't clone it.
    filter: Option<CString>,
    stype: SearchType,
}

pub struct SearchResult {
    _pb: Pblock,
}

impl Search {
    pub fn new_map_entry<T>(
        basedn: &SdnRef,
        scope: SearchScope,
        filter: &str,
        plugin_id: PluginIdRef,
        cbdata: &T,
        mapfn: extern "C" fn(*const libc::c_void, *const libc::c_void) -> i32,
    ) -> Result<Self, PluginError>
    where
        T: Send,
    {
        // Configure a search based on the requested type.
        let pb = Pblock::new();
        let raw_filter = CString::new(filter).map_err(|_| PluginError::InvalidFilter)?;

        unsafe {
            slapi_search_internal_set_pb_ext(
                pb.deref().as_ptr(),
                basedn.as_ptr(),
                scope as i32,
                raw_filter.as_ptr(),
                std::ptr::null(),
                0,
                std::ptr::null(),
                std::ptr::null(),
                plugin_id.raw_pid,
                0,
            )
        };

        Ok(Search {
            pb,
            filter: Some(raw_filter),
            stype: SearchType::InternalMapEntry(mapfn, cbdata as *const _ as *const libc::c_void),
        })
    }

    // Consume self, do the search
    pub fn execute(self) -> Result<SearchResult, LDAPError> {
        // Deconstruct self
        let Search {
            mut pb,
            filter: _filter,
            stype,
        } = self;

        // run the search based on the type.
        match stype {
            SearchType::InternalMapEntry(mapfn, cbdata) => unsafe {
                slapi_search_internal_callback_pb(
                    pb.deref().as_ptr(),
                    cbdata,
                    std::ptr::null(),
                    mapfn as *const libc::c_void,
                    std::ptr::null(),
                );
            },
        };

        // now check the result, and map to what we need.
        let result = pb.get_op_result();

        match result {
            0 => Ok(SearchResult { _pb: pb }),
            _e => Err(LDAPError::from(result)),
        }
    }
}
