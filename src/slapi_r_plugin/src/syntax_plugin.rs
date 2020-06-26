use crate::ber::BerValRef;
// use crate::constants::FilterType;
use crate::charray::Charray;
use crate::error::PluginError;
use crate::pblock::PblockRef;
use crate::value::{ValueArray, ValueArrayRef};
use std::cmp::Ordering;
use std::ffi::CString;
use std::os::raw::c_char;
use std::ptr;

// need a call to slapi_register_plugin_ext

extern "C" {
    fn slapi_matchingrule_register(mr: *const slapi_matchingRuleEntry) -> i32;
}

#[repr(C)]
struct slapi_matchingRuleEntry {
    mr_oid: *const c_char,
    _mr_oidalias: *const c_char,
    mr_name: *const c_char,
    mr_desc: *const c_char,
    mr_syntax: *const c_char,
    _mr_obsolete: i32, // unused
    mr_compat_syntax: *const *const c_char,
}

// oid - the oid of the matching rule
// name - the name of the mr
// desc - description
// syntax - the syntax of the attribute we apply to
// compat_syntax - extended syntaxes f other attributes we may apply to.
pub unsafe fn matchingrule_register(
    oid: &str,
    name: &str,
    desc: &str,
    syntax: &str,
    compat_syntax: &[&str],
) -> i32 {
    // Make everything CStrings that live long enough.

    let oid_cs = CString::new(oid).expect("invalid oid");
    let name_cs = CString::new(name).expect("invalid name");
    let desc_cs = CString::new(desc).expect("invalid desc");
    let syntax_cs = CString::new(syntax).expect("invalid syntax");

    // We have to do this so the cstrings live long enough.
    let compat_syntax_ca = Charray::new(compat_syntax).expect("invalid compat_syntax");

    let new_mr = slapi_matchingRuleEntry {
        mr_oid: oid_cs.as_ptr(),
        _mr_oidalias: ptr::null(),
        mr_name: name_cs.as_ptr(),
        mr_desc: desc_cs.as_ptr(),
        mr_syntax: syntax_cs.as_ptr(),
        _mr_obsolete: 0,
        mr_compat_syntax: compat_syntax_ca.as_ptr(),
    };

    let new_mr_ptr = &new_mr as *const _;
    slapi_matchingrule_register(new_mr_ptr)
}

pub trait SlapiSyntaxPlugin1 {
    fn attr_oid() -> &'static str;

    fn attr_compat_oids() -> Vec<&'static str>;

    fn attr_supported_names() -> Vec<&'static str>;

    fn syntax_validate(bval: &BerValRef) -> Result<(), PluginError>;

    fn eq_mr_oid() -> &'static str;

    fn eq_mr_name() -> &'static str;

    fn eq_mr_desc() -> &'static str;

    fn eq_mr_supported_names() -> Vec<&'static str>;

    fn filter_ava_eq(
        _pb: &mut PblockRef,
        _bval_filter: &BerValRef,
        _vals: &ValueArrayRef,
    ) -> Result<bool, PluginError> {
        Ok(false)
    }

    fn eq_mr_filter_values2keys(
        _pb: &mut PblockRef,
        _vals: &ValueArrayRef,
    ) -> Result<ValueArray, PluginError>;
}

pub trait SlapiOrdMr: SlapiSyntaxPlugin1 {
    fn ord_mr_oid() -> Option<&'static str> {
        None
    }

    fn ord_mr_name() -> &'static str {
        panic!("Unimplemented ord_mr_name for SlapiOrdMr")
    }

    fn ord_mr_desc() -> &'static str {
        panic!("Unimplemented ord_mr_desc for SlapiOrdMr")
    }

    fn ord_mr_supported_names() -> Vec<&'static str> {
        panic!("Unimplemented ord_mr_supported_names for SlapiOrdMr")
    }

    fn filter_ava_ord(
        _pb: &mut PblockRef,
        _bval_filter: &BerValRef,
        _vals: &ValueArrayRef,
    ) -> Result<Option<Ordering>, PluginError> {
        Ok(None)
    }

    fn filter_compare(_a: &BerValRef, _b: &BerValRef) -> Ordering {
        panic!("Unimplemented filter_compare")
    }
}

pub trait SlapiSubMr: SlapiSyntaxPlugin1 {
    fn sub_mr_oid() -> Option<&'static str> {
        None
    }

    fn sub_mr_name() -> &'static str {
        panic!("Unimplemented sub_mr_name for SlapiSubMr")
    }

    fn sub_mr_desc() -> &'static str {
        panic!("Unimplemented sub_mr_desc for SlapiSubMr")
    }

    fn sub_mr_supported_names() -> Vec<&'static str> {
        panic!("Unimplemented sub_mr_supported_names for SlapiSubMr")
    }
}
