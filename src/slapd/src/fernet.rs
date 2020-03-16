// Routines for managing fernet encryption

use crate::error::SlapdError;
use fernet::Fernet;
use std::ffi::{CStr, CString};

pub fn generate_new_key() -> Result<CString, SlapdError> {
    let k = Fernet::generate_key();
    CString::new(k).map_err(|_| SlapdError::CStringInvalidError)
}

pub fn new(c_str_key: &CStr) -> Result<Fernet, SlapdError> {
    let str_key = c_str_key
        .to_str()
        .map_err(|_| SlapdError::CStringInvalidError)?;
    Fernet::new(str_key).ok_or(SlapdError::FernetInvalidKey)
}

pub fn encrypt(fernet: &Fernet, dn: &CStr) -> Result<CString, SlapdError> {
    let tok = fernet.encrypt(dn.to_bytes());
    CString::new(tok).map_err(|_| SlapdError::CStringInvalidError)
}

pub fn decrypt(fernet: &Fernet, tok: &CStr, ttl: u64) -> Result<CString, SlapdError> {
    let s = tok.to_str().map_err(|_| SlapdError::CStringInvalidError)?;
    let r: Vec<u8> = fernet
        .decrypt_with_ttl(s, ttl)
        .map_err(|_| SlapdError::FernetInvalidKey)?;
    CString::new(r).map_err(|_| SlapdError::CStringInvalidError)
}
