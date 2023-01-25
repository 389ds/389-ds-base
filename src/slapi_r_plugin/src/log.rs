use std::ffi::CString;
use std::os::raw::c_char;

use crate::constants;
use crate::error::LoggingError;

extern "C" {
    fn slapi_log_error(level: i32, system: *const c_char, message: *const c_char) -> i32;
}

pub fn log_error(
    level: ErrorLevel,
    subsystem: String,
    message: String,
) -> Result<(), LoggingError> {
    let c_subsystem = CString::new(subsystem)
        .map_err(|e| LoggingError::CString(format!("failed to convert subsystem -> {:?}", e)))?;
    let c_message = CString::new(message)
        .map_err(|e| LoggingError::CString(format!("failed to convert message -> {:?}", e)))?;

    match unsafe { slapi_log_error(level as i32, c_subsystem.as_ptr(), c_message.as_ptr()) } {
        constants::LDAP_SUCCESS => Ok(()),
        _ => Err(LoggingError::Unknown),
    }
}

#[repr(i32)]
#[derive(Debug)]
/// This is a safe rust representation of the values from slapi-plugin.h
/// such as SLAPI_LOG_FATAL, SLAPI_LOG_TRACE, SLAPI_LOG_ ... These vaulues
/// must matche their counter parts in slapi-plugin.h
pub enum ErrorLevel {
    /// Always log messages at this level. Soon to go away, see EMERG, ALERT, CRIT, ERR, WARNING, NOTICE, INFO, DEBUG
    Fatal = 0,
    /// Log detailed messages.
    Trace = 1,
    /// Log packet tracing.
    Packets = 2,
    /// Log argument tracing.
    Args = 3,
    /// Log connection tracking.
    Conns = 4,
    /// Log BER parsing.
    Ber = 5,
    /// Log filter processing.
    Filter = 6,
    /// Log configuration processing.
    Config = 7,
    /// Log access controls
    Acl = 8,
    /// Log .... ???
    Shell = 9,
    /// Log .... ???
    Parse = 10,
    /// Log .... ???
    House = 11,
    /// Log detailed replication information.
    Repl = 12,
    /// Log cache management.
    Cache = 13,
    /// Log detailed plugin operations.
    Plugin = 14,
    /// Log .... ???
    Timing = 15,
    /// Log backend infomation.
    BackLDBM = 16,
    /// Log ACL processing.
    AclSummary = 17,
    /// Log nuncstans processing.
    NuncStansDONOTUSE = 18,
    /// Emergency messages. Server is bursting into flame.
    Emerg = 19,
    /// Important alerts, server may explode soon.
    Alert = 20,
    /// Critical messages, but the server isn't going to explode. Admin should intervene.
    Crit = 21,
    /// Error has occured, but we can keep going. Could indicate misconfiguration.
    Error = 22,
    /// Warning about an issue that isn't very important. Good to resolve though.
    Warning = 23,
    /// Inform the admin of something that they should know about, IE server is running now.
    Notice = 24,
    /// Informational messages that are nice to know.
    Info = 25,
    /// Debugging information from the server.
    Debug = 26,
}
