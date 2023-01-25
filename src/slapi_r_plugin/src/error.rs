// use std::convert::TryFrom;

#[derive(Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum RPluginError {
    Unknown = 500,
    Unimplemented = 501,
    FilterType = 502,
}

#[derive(Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum PluginError {
    GenericFailure = -1,
    Unknown = 1000,
    Unimplemented = 1001,
    Pblock = 1002,
    BervalString = 1003,
    InvalidSyntax = 1004,
    InvalidFilter = 1005,
    TxnFailure = 1006,
    MissingValue = 1007,
    InvalidStrToInt = 1008,
    InvalidBase64 = 1009,
    OpenSSL = 1010,
    Format = 1011,
}

#[derive(Debug)]
#[repr(i32)]
pub enum LDAPError {
    Success = 0,
    Operation = 1,
    ObjectClassViolation = 65,
    Other = 80,
    Unknown = 999,
}

impl From<i32> for LDAPError {
    fn from(value: i32) -> Self {
        match value {
            0 => LDAPError::Success,
            1 => LDAPError::Operation,
            65 => LDAPError::ObjectClassViolation,
            80 => LDAPError::Other,
            _ => LDAPError::Unknown,
        }
    }
}

// if we make debug impl, we can use this.
// errmsg = ldap_err2string(result);

#[derive(Debug)]
#[repr(i32)]
pub enum DseCallbackStatus {
    DoNotApply = 0,
    Ok = 1,
    Error = -1,
}

#[derive(Debug)]
pub enum LoggingError {
    Unknown,
    CString(String),
}
