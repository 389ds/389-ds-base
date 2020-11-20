use crate::PwdChanCrypto;
use slapi_r_plugin::prelude::*;
use std::os::raw::c_char;

/*
 *                    /---- plugin ident
 *                    |          /---- Struct name.
 *                    V          V
 */
slapi_r_plugin_hooks!(pwdchan_pbkdf2_sha512, PwdChanPbkdf2Sha512);

struct PwdChanPbkdf2Sha512;

impl SlapiPlugin3 for PwdChanPbkdf2Sha512 {
    // We require a newer rust for default associated types.
    type TaskData = ();

    fn start(_pb: &mut PblockRef) -> Result<(), PluginError> {
        log_error!(ErrorLevel::Trace, "plugin start");
        Ok(())
    }

    fn close(_pb: &mut PblockRef) -> Result<(), PluginError> {
        log_error!(ErrorLevel::Trace, "plugin close");
        Ok(())
    }

    fn has_pwd_storage() -> bool {
        true
    }

    fn pwd_scheme_name() -> &'static str {
        "PBKDF2-SHA512"
    }

    fn pwd_storage_encrypt(cleartext: &str) -> Result<String, PluginError> {
        PwdChanCrypto::pbkdf2_sha512_encrypt(cleartext)
    }

    fn pwd_storage_compare(cleartext: &str, encrypted: &str) -> Result<bool, PluginError> {
        PwdChanCrypto::pbkdf2_sha512_compare(cleartext, encrypted)
    }
}
