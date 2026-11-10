#![deny(warnings)]
#[macro_use]
extern crate slapi_r_plugin;
use base64::Engine as _;
use base64::engine::{GeneralPurpose, general_purpose::{self, GeneralPurposeConfig}};
use openssl::{hash::MessageDigest, pkcs5::pbkdf2_hmac, rand::rand_bytes};
use slapi_r_plugin::prelude::*;
use std::fmt::Write;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::convert::TryInto;
use std::os::raw::c_char;
use std::time::{SystemTime, UNIX_EPOCH};

/// A base64 engine that tolerates non-canonical trailing bits.
/// OpenLDAP/passlib's ab64 encoding can produce trailing bits that
/// the strict STANDARD engine rejects. This matches the old
/// `base64::STANDARD.decode_allow_trailing_bits(true)` behavior.
const B64_PERMISSIVE: GeneralPurpose = GeneralPurpose::new(
    &base64::alphabet::STANDARD,
    GeneralPurposeConfig::new()
        .with_decode_allow_trailing_bits(true),
);

const DEFAULT_PBKDF2_ROUNDS: usize = 100_000;
const MIN_PBKDF2_ROUNDS: usize = 10_000;
const MAX_PBKDF2_ROUNDS: usize = 10_000_000;
// Missing/invalid accept-max falls back to the highest generation limit the
// plugin already allowed, so upgraded installs keep verifying existing hashes.
const DEFAULT_PBKDF2_ACCEPT_MAX: usize = MAX_PBKDF2_ROUNDS;

const PBKDF2_ROUNDS_ATTR: &str = "nsslapd-pwdPBKDF2NumIterations";
const PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR: &str = "nsslapd-pwdPBKDF2AcceptMaxIterations";

// Scheme identifiers, used for per-scheme config and atomics below.
const SCHEME_PBKDF2: &str = "PBKDF2";
const SCHEME_PBKDF2_SHA1: &str = "PBKDF2-SHA1";
const SCHEME_PBKDF2_SHA256: &str = "PBKDF2-SHA256";
const SCHEME_PBKDF2_SHA512: &str = "PBKDF2-SHA512";

// Rejections outside range iteration counts are logged at most once per interval.
const PBKDF2_REJECT_LOG_INTERVAL: u64 = 300;

// Each algorithm gets its own atomic counter for thread-safe round and accept max iterations.
static PBKDF2_ROUNDS: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA1: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA256: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA512: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);

static PBKDF2_ACCEPT_MAX: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ACCEPT_MAX);
static PBKDF2_ACCEPT_MAX_SHA1: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ACCEPT_MAX);
static PBKDF2_ACCEPT_MAX_SHA256: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ACCEPT_MAX);
static PBKDF2_ACCEPT_MAX_SHA512: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ACCEPT_MAX);

// Rejection log throttling and suppression counters.
static PBKDF2_REJECT_LAST_LOG: AtomicU64 = AtomicU64::new(0);
static PBKDF2_REJECT_LAST_LOG_SHA1: AtomicU64 = AtomicU64::new(0);
static PBKDF2_REJECT_LAST_LOG_SHA256: AtomicU64 = AtomicU64::new(0);
static PBKDF2_REJECT_LAST_LOG_SHA512: AtomicU64 = AtomicU64::new(0);

static PBKDF2_REJECT_SUPPRESSED: AtomicU64 = AtomicU64::new(0);
static PBKDF2_REJECT_SUPPRESSED_SHA1: AtomicU64 = AtomicU64::new(0);
static PBKDF2_REJECT_SUPPRESSED_SHA256: AtomicU64 = AtomicU64::new(0);
static PBKDF2_REJECT_SUPPRESSED_SHA512: AtomicU64 = AtomicU64::new(0);

// Thread-local storage for test environment
#[cfg(test)]
thread_local! {
    static TEST_PBKDF2_ROUNDS: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ROUNDS_SHA1: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ROUNDS_SHA256: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ROUNDS_SHA512: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ACCEPT_MAX: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ACCEPT_MAX_SHA1: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ACCEPT_MAX_SHA256: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ACCEPT_MAX_SHA512: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
}

const PBKDF2_SALT_LEN: usize = 24;
const PBKDF2_SHA1_EXTRACT: usize = 20;
const PBKDF2_SHA256_EXTRACT: usize = 32;
const PBKDF2_SHA512_EXTRACT: usize = 64;

struct PwdChanCrypto;

// OpenLDAP based their PBKDF2 implementation on passlib from python, that uses a
// non-standard base64 altchar set and padding that is not supported by
// anything else in the world. To manage this, we only ever encode to base64 with
// no pad but we have to remap ab64 to b64. This function allows b64 standard with
// padding to pass, and remaps ab64 to b64 standard with padding.
macro_rules! ab64_to_b64 {
    ($ab64:expr) => {{
        let mut s = $ab64.replace(".", "+");
        match s.len() & 3 {
            0 => {
                // Do nothing
            }
            1 => {
                // One is invalid, do nothing, we'll error in base64
            }
            2 => s.push_str("=="),
            3 => s.push_str("="),
            _ => unreachable!(),
        }
        s
    }};
}

// Create a module for each plugin type to avoid name conflicts
mod pbkdf2 {
    use super::*;

    pub struct PwdChanPbkdf2;
    slapi_r_plugin_hooks!(pwdchan_pbkdf2, PwdChanPbkdf2);

    impl super::Pbkdf2Plugin for PwdChanPbkdf2 {
        fn digest_type() -> MessageDigest { MessageDigest::sha1() }
        fn scheme_name() -> &'static str { SCHEME_PBKDF2 }
    }
}

mod pbkdf2_sha1 {
    use super::*;

    pub struct PwdChanPbkdf2Sha1;
    slapi_r_plugin_hooks!(pwdchan_pbkdf2_sha1, PwdChanPbkdf2Sha1);

    impl super::Pbkdf2Plugin for PwdChanPbkdf2Sha1 {
        fn digest_type() -> MessageDigest { MessageDigest::sha1() }
        fn scheme_name() -> &'static str { SCHEME_PBKDF2_SHA1 }
    }
}

mod pbkdf2_sha256 {
    use super::*;

    pub struct PwdChanPbkdf2Sha256;
    slapi_r_plugin_hooks!(pwdchan_pbkdf2_sha256, PwdChanPbkdf2Sha256);

    impl super::Pbkdf2Plugin for PwdChanPbkdf2Sha256 {
        fn digest_type() -> MessageDigest { MessageDigest::sha256() }
        fn scheme_name() -> &'static str { SCHEME_PBKDF2_SHA256 }
    }
}

mod pbkdf2_sha512 {
    use super::*;

    pub struct PwdChanPbkdf2Sha512;
    slapi_r_plugin_hooks!(pwdchan_pbkdf2_sha512, PwdChanPbkdf2Sha512);

    impl super::Pbkdf2Plugin for PwdChanPbkdf2Sha512 {
        fn digest_type() -> MessageDigest { MessageDigest::sha512() }
        fn scheme_name() -> &'static str { SCHEME_PBKDF2_SHA512 }
    }
}

// Common trait for PBKDF2 functionality
trait Pbkdf2Plugin {
    fn digest_type() -> MessageDigest;
    fn scheme_name() -> &'static str;
}

// Implement common plugin functionality
macro_rules! impl_slapi_pbkdf2_plugin {
    ($plugin_type:ty) => {
        impl SlapiPlugin3 for $plugin_type {
            type TaskData = ();

            fn start(pb: &mut PblockRef) -> Result<(), PluginError> {
                log_error!(ErrorLevel::Trace, "{} plugin start", Self::scheme_name());
                PwdChanCrypto::handle_pbkdf2_config(pb, Self::scheme_name())?;
                Ok(())
            }

            fn close(_pb: &mut PblockRef) -> Result<(), PluginError> {
                log_error!(ErrorLevel::Trace, "{} plugin close", Self::scheme_name());
                Ok(())
            }

            fn has_pwd_storage() -> bool {
                true
            }

            fn pwd_scheme_name() -> &'static str {
                Self::scheme_name()
            }

            fn pwd_storage_encrypt(cleartext: &str) -> Result<String, PluginError> {
                PwdChanCrypto::pbkdf2_encrypt(cleartext, Self::digest_type(), Self::scheme_name())
            }

            fn pwd_storage_compare(cleartext: &str, encrypted: &str) -> Result<bool, PluginError> {
                PwdChanCrypto::pbkdf2_compare(cleartext, encrypted, Self::digest_type(), Self::scheme_name())
            }
        }
    };
}

// Apply the implementation to all plugin types
impl_slapi_pbkdf2_plugin!(pbkdf2::PwdChanPbkdf2);
impl_slapi_pbkdf2_plugin!(pbkdf2_sha1::PwdChanPbkdf2Sha1);
impl_slapi_pbkdf2_plugin!(pbkdf2_sha256::PwdChanPbkdf2Sha256);
impl_slapi_pbkdf2_plugin!(pbkdf2_sha512::PwdChanPbkdf2Sha512);

impl PwdChanCrypto {
    fn validate_pbkdf2_rounds(value: usize) -> Result<(), PluginError> {
        if value < MIN_PBKDF2_ROUNDS || value > MAX_PBKDF2_ROUNDS {
            #[cfg(not(test))]
            log_error!(
                ErrorLevel::Error,
                "Invalid PBKDF2 iteration count {}, must be between {} and {}",
                value,
                MIN_PBKDF2_ROUNDS,
                MAX_PBKDF2_ROUNDS
            );
            return Err(PluginError::InvalidConfiguration);
        }

        Ok(())
    }

    fn validate_pbkdf2_stored_iterations(iterations: usize, scheme: &str) -> Result<(), PluginError> {
        let accept_max = Self::get_pbkdf2_accept_max(scheme)?;
        if iterations < MIN_PBKDF2_ROUNDS || iterations > accept_max {
            Self::log_rejected_iterations(scheme, iterations, accept_max);
            return Err(PluginError::InvalidConfiguration);
        }

        Ok(())
    }

    fn get_reject_log_atomics(
        scheme: &str,
    ) -> Result<(&'static AtomicU64, &'static AtomicU64), PluginError> {
        match scheme {
            SCHEME_PBKDF2 => Ok((&PBKDF2_REJECT_LAST_LOG, &PBKDF2_REJECT_SUPPRESSED)),
            SCHEME_PBKDF2_SHA1 => Ok((
                &PBKDF2_REJECT_LAST_LOG_SHA1,
                &PBKDF2_REJECT_SUPPRESSED_SHA1,
            )),
            SCHEME_PBKDF2_SHA256 => Ok((
                &PBKDF2_REJECT_LAST_LOG_SHA256,
                &PBKDF2_REJECT_SUPPRESSED_SHA256,
            )),
            SCHEME_PBKDF2_SHA512 => Ok((
                &PBKDF2_REJECT_LAST_LOG_SHA512,
                &PBKDF2_REJECT_SUPPRESSED_SHA512,
            )),
            _ => Err(PluginError::Unknown),
        }
    }

    fn log_rejected_iterations(scheme: &str, iterations: usize, accept_max: usize) {
        let Ok((last_log, suppressed_ctr)) = Self::get_reject_log_atomics(scheme) else {
            return;
        };

        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        let last = last_log.load(Ordering::Relaxed);

        if last != 0 && now.saturating_sub(last) < PBKDF2_REJECT_LOG_INTERVAL {
            suppressed_ctr.fetch_add(1, Ordering::Relaxed);
            return;
        }

        last_log.store(now, Ordering::Relaxed);
        let suppressed = suppressed_ctr.swap(0, Ordering::Relaxed);

        #[cfg(not(test))]
        {
            if iterations > accept_max {
                log_error_ext!(
                    ErrorLevel::Error,
                    scheme,
                    "Rejected a password hash with iteration count {} above the accepted maximum {}",
                    iterations,
                    accept_max,
                );
            } else {
                log_error_ext!(
                    ErrorLevel::Error,
                    scheme,
                    "Rejected a password hash with iteration count {} below the supported minimum {}",
                    iterations,
                    MIN_PBKDF2_ROUNDS,
                );
            }

            if suppressed > 0 {
                log_error_ext!(
                    ErrorLevel::Error,
                    scheme,
                    "{} additional outside range iteration rejections were suppressed in the last {} seconds",
                    suppressed,
                    now.saturating_sub(last),
                );
            }
        }

        #[cfg(test)]
        {
            let _ = (iterations, accept_max, suppressed, last, now);
        }
    }

    #[inline(always)]
    fn pbkdf2_decompose(encrypted: &str, scheme: &str) -> Result<(usize, Vec<u8>, Vec<u8>), PluginError>
    {
        let mut part_iter = encrypted.split('$');

        let iter = part_iter
            .next()
            .ok_or(PluginError::MissingValue)
            .and_then(|iter_str| {
                usize::from_str_radix(iter_str, 10).map_err(|_e| {
                    #[cfg(not(test))]
                    log_error!(ErrorLevel::Error, "Invalid Integer {} -> {:?}", iter_str, _e);
                    PluginError::InvalidStrToInt
                })
            })?;

        Self::validate_pbkdf2_stored_iterations(iter, scheme)?;

        let salt = part_iter
            .next()
            .ok_or(PluginError::MissingValue)
            .and_then(|ab64| {
                let s = ab64_to_b64!(ab64);
                B64_PERMISSIVE.decode(&s)
                    .map_err(|e| {
                        log_error!(ErrorLevel::Error, "Invalid Base 64 {} -> {:?}", s, e);
                        PluginError::InvalidBase64
                    })
            })?;

        let hash = part_iter
            .next()
            .ok_or(PluginError::MissingValue)
            .and_then(|ab64| {
                let s = ab64_to_b64!(ab64);
                B64_PERMISSIVE.decode(&s)
                    .map_err(|e| {
                        log_error!(ErrorLevel::Error, "Invalid Base 64 {} -> {:?}", s, e);
                        PluginError::InvalidBase64
                    })
            })?;

        Ok((iter, salt, hash))
    }

    fn pbkdf2_compare(
        cleartext: &str,
        encrypted: &str,
        digest: MessageDigest,
        scheme: &str,
    ) -> Result<bool, PluginError> {
        let (iter, salt, hash_expected) = match Self::pbkdf2_decompose(encrypted, scheme) {
            Ok(parts) => parts,
            Err(PluginError::InvalidConfiguration) => return Ok(false),
            Err(e) => {
                #[cfg(not(test))]
                log_error!(ErrorLevel::Error, "invalid hashed pw -> {:?}", e);
                return Err(e);
            }
        };
        // Need to pre-alloc the space as as_mut_slice can't resize.
        let mut hash_input: Vec<u8> = (0..hash_expected.len()).map(|_| 0).collect();

        pbkdf2_hmac(
            cleartext.as_bytes(),
            &salt,
            iter,
            digest,
            hash_input.as_mut_slice(),
        )
        .map_err(|e| {
            log_error!(ErrorLevel::Error, "OpenSSL Error -> {:?}", e);
            PluginError::OpenSSL
        })
        .map(|()| hash_input == hash_expected)
    }

    fn scheme_format(scheme: &str) -> Result<(usize, usize, &'static str), PluginError> {
        match scheme {
            SCHEME_PBKDF2 => Ok((PBKDF2_SHA1_EXTRACT, 72, "{PBKDF2}")),
            SCHEME_PBKDF2_SHA1 => Ok((PBKDF2_SHA1_EXTRACT, 80, "{PBKDF2-SHA1}")),
            SCHEME_PBKDF2_SHA256 => Ok((PBKDF2_SHA256_EXTRACT, 100, "{PBKDF2-SHA256}")),
            SCHEME_PBKDF2_SHA512 => Ok((PBKDF2_SHA512_EXTRACT, 140, "{PBKDF2-SHA512}")),
            _ => Err(PluginError::Unknown),
        }
    }

    fn pbkdf2_encrypt(cleartext: &str, digest: MessageDigest, scheme: &str) -> Result<String, PluginError> {
        let mut rounds = Self::get_pbkdf2_rounds(scheme)?;
        let accept_max = Self::get_pbkdf2_accept_max(scheme)?;
        if rounds > accept_max {
            rounds = accept_max;
        }
        let (hash_length, str_length, header) = Self::scheme_format(scheme)?;

        // generate salt
        let mut salt: Vec<u8> = (0..PBKDF2_SALT_LEN).map(|_| 0).collect();
        rand_bytes(salt.as_mut_slice()).map_err(|e| {
            log_error!(ErrorLevel::Error, "OpenSSL Error -> {:?}", e);
            PluginError::OpenSSL
        })?;

        let mut hash_input: Vec<u8> = (0..hash_length).map(|_| 0).collect();

        pbkdf2_hmac(
            cleartext.as_bytes(),
            &salt,
            rounds,
            digest,
            hash_input.as_mut_slice(),
        )
        .map_err(|e| {
            log_error!(ErrorLevel::Error, "OpenSSL Error -> {:?}", e);
            PluginError::OpenSSL
        })?;

        let mut output = String::with_capacity(str_length);
        // Write the header
        output.push_str(header);

        // The iter + delim
        write!(&mut output, "{}$", rounds).map_err(|e| {
            log_error!(ErrorLevel::Error, "Format Error -> {:?}", e);
            PluginError::Format
        })?;
        // the base64 salt
        general_purpose::STANDARD.encode_string(&salt, &mut output);
        // Push the delim
        output.push('$');
        // Finally the base64 hash
        general_purpose::STANDARD.encode_string(&hash_input, &mut output);

        Ok(output)
    }

    fn get_rounds_atomic(scheme: &str) -> Result<&'static AtomicUsize, PluginError> {
        match scheme {
            SCHEME_PBKDF2 => Ok(&PBKDF2_ROUNDS),
            SCHEME_PBKDF2_SHA1 => Ok(&PBKDF2_ROUNDS_SHA1),
            SCHEME_PBKDF2_SHA256 => Ok(&PBKDF2_ROUNDS_SHA256),
            SCHEME_PBKDF2_SHA512 => Ok(&PBKDF2_ROUNDS_SHA512),
            _ => Err(PluginError::Unknown),
        }
    }

    fn get_accept_max_atomic(scheme: &str) -> Result<&'static AtomicUsize, PluginError> {
        match scheme {
            SCHEME_PBKDF2 => Ok(&PBKDF2_ACCEPT_MAX),
            SCHEME_PBKDF2_SHA1 => Ok(&PBKDF2_ACCEPT_MAX_SHA1),
            SCHEME_PBKDF2_SHA256 => Ok(&PBKDF2_ACCEPT_MAX_SHA256),
            SCHEME_PBKDF2_SHA512 => Ok(&PBKDF2_ACCEPT_MAX_SHA512),
            _ => Err(PluginError::Unknown),
        }
    }

    #[cfg(test)]
    fn set_test_rounds(scheme: &str, rounds: Option<usize>) {
        match scheme {
            SCHEME_PBKDF2 => TEST_PBKDF2_ROUNDS.with(|cell| cell.set(rounds)),
            SCHEME_PBKDF2_SHA1 => TEST_PBKDF2_ROUNDS_SHA1.with(|cell| cell.set(rounds)),
            SCHEME_PBKDF2_SHA256 => TEST_PBKDF2_ROUNDS_SHA256.with(|cell| cell.set(rounds)),
            SCHEME_PBKDF2_SHA512 => TEST_PBKDF2_ROUNDS_SHA512.with(|cell| cell.set(rounds)),
            _ => {}
        }
    }

    #[cfg(test)]
    fn get_test_rounds(scheme: &str) -> Option<usize> {
        match scheme {
            SCHEME_PBKDF2 => TEST_PBKDF2_ROUNDS.with(|cell| cell.get()),
            SCHEME_PBKDF2_SHA1 => TEST_PBKDF2_ROUNDS_SHA1.with(|cell| cell.get()),
            SCHEME_PBKDF2_SHA256 => TEST_PBKDF2_ROUNDS_SHA256.with(|cell| cell.get()),
            SCHEME_PBKDF2_SHA512 => TEST_PBKDF2_ROUNDS_SHA512.with(|cell| cell.get()),
            _ => None,
        }
    }

    #[cfg(test)]
    fn set_test_accept_max(scheme: &str, accept_max: Option<usize>) {
        match scheme {
            SCHEME_PBKDF2 => TEST_PBKDF2_ACCEPT_MAX.with(|cell| cell.set(accept_max)),
            SCHEME_PBKDF2_SHA1 => TEST_PBKDF2_ACCEPT_MAX_SHA1.with(|cell| cell.set(accept_max)),
            SCHEME_PBKDF2_SHA256 => TEST_PBKDF2_ACCEPT_MAX_SHA256.with(|cell| cell.set(accept_max)),
            SCHEME_PBKDF2_SHA512 => TEST_PBKDF2_ACCEPT_MAX_SHA512.with(|cell| cell.set(accept_max)),
            _ => {}
        }
    }

    #[cfg(test)]
    fn get_test_accept_max(scheme: &str) -> Option<usize> {
        match scheme {
            SCHEME_PBKDF2 => TEST_PBKDF2_ACCEPT_MAX.with(|cell| cell.get()),
            SCHEME_PBKDF2_SHA1 => TEST_PBKDF2_ACCEPT_MAX_SHA1.with(|cell| cell.get()),
            SCHEME_PBKDF2_SHA256 => TEST_PBKDF2_ACCEPT_MAX_SHA256.with(|cell| cell.get()),
            SCHEME_PBKDF2_SHA512 => TEST_PBKDF2_ACCEPT_MAX_SHA512.with(|cell| cell.get()),
            _ => None,
        }
    }

    fn parse_config_usize_attr(
        entry: &EntryRef,
        attr: &str,
    ) -> Result<usize, PluginError> {
        let value_array = entry
            .get_attr(attr)
            .ok_or(PluginError::InvalidConfiguration)?;
        let value = value_array.first().ok_or(PluginError::InvalidConfiguration)?;
        let value_str: String = value.as_ref().try_into().map_err(|_| {
            log_error!(ErrorLevel::Error, "Failed to parse {} value", attr);
            PluginError::InvalidConfiguration
        })?;

        value_str.parse::<usize>().map_err(|e| {
            log_error!(
                ErrorLevel::Error,
                "Invalid {} value '{}': {}",
                attr,
                value_str,
                e
            );
            PluginError::InvalidConfiguration
        })
    }

    fn read_config_usize_attr(entry: &EntryRef, attr: &str) -> Option<usize> {
        match Self::parse_config_usize_attr(entry, attr) {
            Ok(value) => Some(value),
            Err(_) => None,
        }
    }

    fn resolve_pbkdf2_startup_config(
        configured_rounds: Option<usize>,
        configured_accept_max: Option<usize>,
    ) -> (usize, usize, &'static str, Option<usize>) {
        let (mut rounds, source) = match configured_rounds {
            Some(value) if Self::validate_pbkdf2_rounds(value).is_ok() => {
                (value, "configuration")
            }
            _ => (DEFAULT_PBKDF2_ROUNDS, "default"),
        };

        let accept_max = match configured_accept_max {
            Some(value) if Self::validate_pbkdf2_rounds(value).is_ok() => value,
            _ => DEFAULT_PBKDF2_ACCEPT_MAX,
        };

        let capped_from = if rounds > accept_max {
            let original = rounds;
            rounds = accept_max;
            Some(original)
        } else {
            None
        };

        (rounds, accept_max, source, capped_from)
    }

    fn handle_pbkdf2_config(pb: &mut PblockRef, scheme: &str) -> Result<(), PluginError> {
        // Keep verification ceiling independent of generation rounds.
        let mut configured_rounds = None;
        let mut configured_accept_max = None;

        if let Ok(entry) = pb.get_op_add_entryref() {
            if entry.get_attr(PBKDF2_ROUNDS_ATTR).is_some() {
                match Self::read_config_usize_attr(&entry, PBKDF2_ROUNDS_ATTR) {
                    Some(value) if Self::validate_pbkdf2_rounds(value).is_ok() => {
                        configured_rounds = Some(value);
                    }
                    Some(value) => {
                        log_error_ext!(
                            ErrorLevel::Error,
                            scheme,
                            "Invalid {} value {}, must be between {} and {}",
                            PBKDF2_ROUNDS_ATTR,
                            value,
                            MIN_PBKDF2_ROUNDS,
                            MAX_PBKDF2_ROUNDS,
                        );
                        return Err(PluginError::InvalidConfiguration);
                    }
                    None => {
                        log_error_ext!(
                            ErrorLevel::Error,
                            scheme,
                            "Invalid {} value",
                            PBKDF2_ROUNDS_ATTR,
                        );
                        return Err(PluginError::InvalidConfiguration);
                    }
                }
            }

            if entry.get_attr(PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR).is_some() {
                match Self::read_config_usize_attr(&entry, PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR) {
                    Some(value) if Self::validate_pbkdf2_rounds(value).is_ok() => {
                        configured_accept_max = Some(value);
                    }
                    Some(value) => {
                        log_error_ext!(
                            ErrorLevel::Error,
                            scheme,
                            "Invalid {} value {}, must be between {} and {}; using the default {}",
                            PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR,
                            value,
                            MIN_PBKDF2_ROUNDS,
                            MAX_PBKDF2_ROUNDS,
                            DEFAULT_PBKDF2_ACCEPT_MAX,
                        );
                    }
                    None => {
                        log_error_ext!(
                            ErrorLevel::Error,
                            scheme,
                            "Invalid {} value; using the default {}",
                            PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR,
                            DEFAULT_PBKDF2_ACCEPT_MAX,
                        );
                    }
                }
            }
        }

        let (rounds, accept_max, source, capped_from) =
            Self::resolve_pbkdf2_startup_config(configured_rounds, configured_accept_max);

        if let Some(original_rounds) = capped_from {
            log_error_ext!(
                ErrorLevel::Info,
                scheme,
                "Configured {} rounds; capping at the configured accept max {}",
                original_rounds,
                accept_max,
            );
        }

        Self::set_pbkdf2_accept_max(scheme, accept_max)?;
        Self::set_pbkdf2_rounds(scheme, rounds)?;

        log_error_ext!(
            ErrorLevel::Info,
            scheme,
            "Number of iterations set to {} from {}",
            rounds,
            source,
        );

        log_error_ext!(
            ErrorLevel::Info,
            scheme,
            "PBKDF2 accept max iterations set to {}",
            accept_max,
        );

        Ok(())
    }

    fn set_pbkdf2_rounds(scheme: &str, rounds: usize) -> Result<(), PluginError> {
        Self::validate_pbkdf2_rounds(rounds)?;

        #[cfg(test)]
        {
            Self::set_test_rounds(scheme, Some(rounds));
        }

        #[cfg(not(test))]
        {
            Self::get_rounds_atomic(scheme)?.store(rounds, Ordering::Relaxed);
        }

        Ok(())
    }

    fn get_pbkdf2_rounds(scheme: &str) -> Result<usize, PluginError> {
        #[cfg(test)]
        {
            if let Some(value) = Self::get_test_rounds(scheme) {
                return Ok(value);
            }
        }

        Ok(Self::get_rounds_atomic(scheme)?.load(Ordering::Relaxed))
    }

    fn set_pbkdf2_accept_max(scheme: &str, accept_max: usize) -> Result<(), PluginError> {
        Self::validate_pbkdf2_rounds(accept_max)?;

        #[cfg(test)]
        {
            Self::set_test_accept_max(scheme, Some(accept_max));
        }

        #[cfg(not(test))]
        {
            Self::get_accept_max_atomic(scheme)?.store(accept_max, Ordering::Relaxed);
        }

        Ok(())
    }

    fn get_pbkdf2_accept_max(scheme: &str) -> Result<usize, PluginError> {
        #[cfg(test)]
        {
            if let Some(value) = Self::get_test_accept_max(scheme) {
                return Ok(value);
            }
        }

        Ok(Self::get_accept_max_atomic(scheme)?.load(Ordering::Relaxed))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::PwdChanCrypto;

    /*
     * '{PBKDF2}10000$IlfapjA351LuDSwYC0IQ8Q$saHqQTuYnjJN/tmAndT.8mJt.6w'
     * '{PBKDF2-SHA1}10000$ZBEH6B07rgQpJSikyvMU2w$TAA03a5IYkz1QlPsbJKvUsTqNV'
     * '{PBKDF2-SHA256}10000$henZGfPWw79Cs8ORDeVNrQ$1dTJy73v6n3bnTmTZFghxHXHLsAzKaAy8SksDfZBPIw'
     * '{PBKDF2-SHA512}10000$Je1Uw19Bfv5lArzZ6V3EPw$g4T/1sqBUYWl9o93MVnyQ/8zKGSkPbKaXXsT8WmysXQJhWy8MRP2JFudSL.N9RklQYgDPxPjnfum/F2f/TrppA'
     * '{ARGON2}$argon2id$v=19$m=65536,t=2,p=1$IyTQMsvzB2JHDiWx8fq7Ew$VhYOA7AL0kbRXI5g2kOyyp8St1epkNj7WZyUY4pAIQQ'
     */

    // A helper function for tests to reset rounds to defaults
    fn reset_pbkdf2_rounds() {
        // Reset the rounds to defaults in thread-local storage
        TEST_PBKDF2_ROUNDS.with(|cell| cell.set(None));
        TEST_PBKDF2_ROUNDS_SHA1.with(|cell| cell.set(None));
        TEST_PBKDF2_ROUNDS_SHA256.with(|cell| cell.set(None));
        TEST_PBKDF2_ROUNDS_SHA512.with(|cell| cell.set(None));
        TEST_PBKDF2_ACCEPT_MAX.with(|cell| cell.set(None));
        TEST_PBKDF2_ACCEPT_MAX_SHA1.with(|cell| cell.set(None));
        TEST_PBKDF2_ACCEPT_MAX_SHA256.with(|cell| cell.set(None));
        TEST_PBKDF2_ACCEPT_MAX_SHA512.with(|cell| cell.set(None));

        // Set default values for each scheme independently
        for scheme in [
            SCHEME_PBKDF2,
            SCHEME_PBKDF2_SHA1,
            SCHEME_PBKDF2_SHA256,
            SCHEME_PBKDF2_SHA512,
        ] {
            PwdChanCrypto::set_pbkdf2_rounds(scheme, DEFAULT_PBKDF2_ROUNDS).unwrap();
            PwdChanCrypto::set_pbkdf2_accept_max(scheme, DEFAULT_PBKDF2_ACCEPT_MAX).unwrap();
        }
    }

    #[test]
    fn test_pbkdf2_encrypt_with_different_rounds() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        // Set different rounds for each scheme
        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA1, 15000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 20000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA512, 25000).is_ok());

        // Verify rounds are correctly set per scheme
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2_SHA1).unwrap(), 15000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2_SHA256).unwrap(), 20000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2_SHA512).unwrap(), 25000);

        let test_password = "test_password";

        // Test SHA1 (PBKDF2-SHA1 scheme)
        let sha1_result = PwdChanCrypto::pbkdf2_encrypt(
            test_password,
            MessageDigest::sha1(),
            SCHEME_PBKDF2_SHA1,
        )
        .unwrap();
        assert!(sha1_result.starts_with("{PBKDF2-SHA1}"));
        let sha1_no_header = sha1_result.replace("{PBKDF2-SHA1}", "");
        let sha1_parts: Vec<&str> = sha1_no_header.split('$').collect();
        let rounds: usize = sha1_parts[0].parse().unwrap();
        assert_eq!(rounds, 15000, "SHA1 rounds should be 15000, got {}", rounds);

        // Test SHA256
        let sha256_result = PwdChanCrypto::pbkdf2_encrypt(
            test_password,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        )
        .unwrap();
        assert!(sha256_result.starts_with("{PBKDF2-SHA256}"));
        let sha256_no_header = sha256_result.replace("{PBKDF2-SHA256}", "");
        let sha256_parts: Vec<&str> = sha256_no_header.split('$').collect();
        let rounds: usize = sha256_parts[0].parse().unwrap();
        assert_eq!(rounds, 20000, "SHA256 rounds should be 20000, got {}", rounds);

        // Test SHA512
        let sha512_result = PwdChanCrypto::pbkdf2_encrypt(
            test_password,
            MessageDigest::sha512(),
            SCHEME_PBKDF2_SHA512,
        )
        .unwrap();
        assert!(sha512_result.starts_with("{PBKDF2-SHA512}"));
        let sha512_no_header = sha512_result.replace("{PBKDF2-SHA512}", "");
        let sha512_parts: Vec<&str> = sha512_no_header.split('$').collect();
        let rounds: usize = sha512_parts[0].parse().unwrap();
        assert_eq!(rounds, 25000, "SHA512 rounds should be 25000, got {}", rounds);

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_legacy_and_sha1_independent_config() {
        reset_pbkdf2_rounds();

        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2, 11000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA1, 12000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2, 50000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA1, 60000).is_ok());

        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2).unwrap(), 11000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2_SHA1).unwrap(), 12000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2).unwrap(), 50000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA1).unwrap(), 60000);

        let legacy = PwdChanCrypto::pbkdf2_encrypt(
            "password",
            MessageDigest::sha1(),
            SCHEME_PBKDF2,
        )
        .unwrap();
        assert!(legacy.starts_with("{PBKDF2}"));
        let legacy_rounds: usize = legacy
            .trim_start_matches("{PBKDF2}")
            .split('$')
            .next()
            .unwrap()
            .parse()
            .unwrap();
        assert_eq!(legacy_rounds, 11000);

        let sha1 = PwdChanCrypto::pbkdf2_encrypt(
            "password",
            MessageDigest::sha1(),
            SCHEME_PBKDF2_SHA1,
        )
        .unwrap();
        assert!(sha1.starts_with("{PBKDF2-SHA1}"));
        let sha1_rounds: usize = sha1
            .trim_start_matches("{PBKDF2-SHA1}")
            .split('$')
            .next()
            .unwrap()
            .parse()
            .unwrap();
        assert_eq!(sha1_rounds, 12000);

        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_rounds_configuration() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        // Test different rounds for each scheme
        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA1, 15000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 20000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA512, 25000).is_ok());

        // Verify each scheme has its own rounds setting
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2_SHA1).unwrap(), 15000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2_SHA256).unwrap(), 20000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2_SHA512).unwrap(), 25000);

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_rounds_limits() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        // Test max limit - should fail
        let result = PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA1, MAX_PBKDF2_ROUNDS + 1);
        assert!(result.is_err());

        // Test min rounds - should succeed
        let result = PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA1, MIN_PBKDF2_ROUNDS);
        assert!(result.is_ok());

        // Test invalid rounds for SHA256 - too low - should fail
        let result = PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, MIN_PBKDF2_ROUNDS - 1);
        assert!(result.is_err());

        // Test max rounds - should succeed even without raising accept max.
        let result = PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, MAX_PBKDF2_ROUNDS);
        assert!(result.is_ok());

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_accept_max_configuration() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA1, 15000).unwrap();
        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 20000).unwrap();
        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA512, 25000).unwrap();

        // Test different accept max for each scheme
        assert!(PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA1, 15000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 20000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA512, 25000).is_ok());

        // Verify each scheme has its own accept max setting
        assert_eq!(PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA1).unwrap(), 15000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256).unwrap(), 20000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA512).unwrap(), 25000);

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_accept_max_independent_of_rounds() {
        reset_pbkdf2_rounds();

        // Lowering generation rounds must not force the verification ceiling down.
        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 50000).unwrap();
        PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, DEFAULT_PBKDF2_ACCEPT_MAX)
            .unwrap();
        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 20000).unwrap();

        assert_eq!(
            PwdChanCrypto::get_pbkdf2_rounds(SCHEME_PBKDF2_SHA256).unwrap(),
            20000
        );
        assert_eq!(
            PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256).unwrap(),
            DEFAULT_PBKDF2_ACCEPT_MAX
        );

        // Accept max below generation rounds is allowed; encrypt caps instead.
        assert!(PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 15000).is_ok());
        assert_eq!(
            PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256).unwrap(),
            15000
        );

        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_encrypt_caps_rounds_to_accept_max() {
        reset_pbkdf2_rounds();

        PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 15000).unwrap();
        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 30000).unwrap();

        let encrypted = PwdChanCrypto::pbkdf2_encrypt(
            "password",
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        )
        .unwrap();
        let rounds: usize = encrypted
            .trim_start_matches("{PBKDF2-SHA256}")
            .split('$')
            .next()
            .unwrap()
            .parse()
            .unwrap();
        assert_eq!(rounds, 15000);

        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_startup_config_soft_fallback_and_cap() {
        // Missing config -> independent defaults.
        let (rounds, accept_max, source, capped_from) =
            PwdChanCrypto::resolve_pbkdf2_startup_config(None, None);
        assert_eq!(rounds, DEFAULT_PBKDF2_ROUNDS);
        assert_eq!(accept_max, DEFAULT_PBKDF2_ACCEPT_MAX);
        assert_eq!(source, "default");
        assert!(capped_from.is_none());

        let (rounds, accept_max, source, capped_from) =
            PwdChanCrypto::resolve_pbkdf2_startup_config(None, Some(MAX_PBKDF2_ROUNDS + 1));
        assert_eq!(rounds, DEFAULT_PBKDF2_ROUNDS);
        assert_eq!(accept_max, DEFAULT_PBKDF2_ACCEPT_MAX);
        assert_eq!(source, "default");
        assert!(capped_from.is_none());

        // Lowered rounds keep the independent accept-max default.
        let (rounds, accept_max, source, capped_from) =
            PwdChanCrypto::resolve_pbkdf2_startup_config(Some(20000), None);
        assert_eq!(rounds, 20000);
        assert_eq!(accept_max, DEFAULT_PBKDF2_ACCEPT_MAX);
        assert_eq!(source, "configuration");
        assert!(capped_from.is_none());

        // Valid 600k rounds stay under the default (10m) accept-max ceiling.
        let (rounds, accept_max, source, capped_from) =
            PwdChanCrypto::resolve_pbkdf2_startup_config(Some(600_000), None);
        assert_eq!(rounds, 600_000);
        assert_eq!(accept_max, DEFAULT_PBKDF2_ACCEPT_MAX);
        assert_eq!(source, "configuration");
        assert!(capped_from.is_none());

        // Max generation rounds are accepted when accept-max is unset.
        let (rounds, accept_max, source, capped_from) =
            PwdChanCrypto::resolve_pbkdf2_startup_config(Some(MAX_PBKDF2_ROUNDS), None);
        assert_eq!(rounds, MAX_PBKDF2_ROUNDS);
        assert_eq!(accept_max, DEFAULT_PBKDF2_ACCEPT_MAX);
        assert_eq!(source, "configuration");
        assert!(capped_from.is_none());

        // Rounds above accept-max are capped, not rejected.
        let (rounds, accept_max, source, capped_from) =
            PwdChanCrypto::resolve_pbkdf2_startup_config(Some(30000), Some(15000));
        assert_eq!(rounds, 15000);
        assert_eq!(accept_max, 15000);
        assert_eq!(source, "configuration");
        assert_eq!(capped_from, Some(30000));
    }

    fn reset_reject_log_state(scheme: &str) {
        let (last_log, suppressed) = PwdChanCrypto::get_reject_log_atomics(scheme).unwrap();
        last_log.store(0, Ordering::Relaxed);
        suppressed.store(0, Ordering::Relaxed);
    }

    #[test]
    fn test_pbkdf2_reject_log_throttle() {
        reset_pbkdf2_rounds();
        reset_reject_log_state(SCHEME_PBKDF2_SHA256);
        PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 10000).unwrap();

        let (last_log, suppressed) =
            PwdChanCrypto::get_reject_log_atomics(SCHEME_PBKDF2_SHA256).unwrap();

        // First rejection records last-log time and does not suppress.
        assert!(PwdChanCrypto::validate_pbkdf2_stored_iterations(
            20000,
            SCHEME_PBKDF2_SHA256
        )
        .is_err());
        let first_log = last_log.load(Ordering::Relaxed);
        assert_ne!(first_log, 0);
        assert_eq!(suppressed.load(Ordering::Relaxed), 0);

        // Further rejections inside the interval only bump the suppressed count.
        assert!(PwdChanCrypto::validate_pbkdf2_stored_iterations(
            20000,
            SCHEME_PBKDF2_SHA256
        )
        .is_err());
        assert!(PwdChanCrypto::validate_pbkdf2_stored_iterations(
            MIN_PBKDF2_ROUNDS - 1,
            SCHEME_PBKDF2_SHA256
        )
        .is_err());
        assert_eq!(last_log.load(Ordering::Relaxed), first_log);
        assert_eq!(suppressed.load(Ordering::Relaxed), 2);

        reset_reject_log_state(SCHEME_PBKDF2_SHA256);
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_accept_max_limits() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA1, MIN_PBKDF2_ROUNDS).unwrap();
        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, MIN_PBKDF2_ROUNDS).unwrap();

        // Test min limit - should fail
        let result = PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA1, MIN_PBKDF2_ROUNDS - 1);
        assert!(result.is_err());

        // Test min accept max - should succeed
        let result = PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA1, MIN_PBKDF2_ROUNDS);
        assert!(result.is_ok());

        // Fresh-install-style 600k ceiling - should succeed
        let result = PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 600_000);
        assert!(result.is_ok());

        // Absolute policy maximum - should succeed
        let result = PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, MAX_PBKDF2_ROUNDS);
        assert!(result.is_ok());

        // Test accept max above policy/OpenSSL limit - should fail
        let result =
            PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, MAX_PBKDF2_ROUNDS + 1);
        assert!(result.is_err());

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_decompose() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        // Valid hash - should succeed
        let valid_hash = "10000$salt123$hash456";
        let result = PwdChanCrypto::pbkdf2_decompose(valid_hash, SCHEME_PBKDF2_SHA256);
        assert!(result.is_ok());
        let (iter, _salt, _hash) = result.unwrap();
        assert_eq!(iter, 10000);

        // Iteration count above accept max - should fail
        let high_hash = format!("{}$salt123$hash456", DEFAULT_PBKDF2_ACCEPT_MAX + 1);
        let result = PwdChanCrypto::pbkdf2_decompose(&high_hash, SCHEME_PBKDF2_SHA256);
        assert!(result.is_err());

        // Iteration count below min - should fail
        let low_hash = format!("{}$salt123$hash456", MIN_PBKDF2_ROUNDS - 1);
        let result = PwdChanCrypto::pbkdf2_decompose(&low_hash, SCHEME_PBKDF2_SHA256);
        assert!(result.is_err());

        // Invalid format - should fail
        let result = PwdChanCrypto::pbkdf2_decompose("invalid", SCHEME_PBKDF2_SHA256);
        assert!(result.is_err());

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_compare_accept_max_iterations() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        assert!(PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 10000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 10000).is_ok());
        assert_eq!(PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256).unwrap(), 10000);

        // Stored iterations above accept max - normal compare failure after throttled log.
        let encrypted = "36000$eElFb3p1WlZBb1lt$uW1b35DUKyhvQAf1mBqMvoBDcqSD06juzyO/nmyV0+w=";
        let result = PwdChanCrypto::pbkdf2_compare(
            "eicieY7ahchaoCh0eeTa",
            encrypted,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        );
        assert_eq!(result, Ok(false));

        assert!(PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 60000).is_ok());
        assert_eq!(PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256).unwrap(), 60000);

        // Stored iterations within accept max - should succeed
        let result = PwdChanCrypto::pbkdf2_compare(
            "eicieY7ahchaoCh0eeTa",
            encrypted,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        );
        assert!(result == Ok(true));

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_compare_reject_invalid_iterations() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        let encrypted_tail = "henZGfPWw79Cs8ORDeVNrQ$1dTJy73v6n3bnTmTZFghxHXHLsAzKaAy8SksDfZBPIw";

        // Stored iterations above accept max - normal compare failure.
        let high_hash = format!("{}${}", DEFAULT_PBKDF2_ACCEPT_MAX + 1, encrypted_tail);
        let result = PwdChanCrypto::pbkdf2_compare(
            "password",
            &high_hash,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        );
        assert_eq!(result, Ok(false));

        // Stored iterations below min - normal compare failure.
        let low_hash = format!("{}${}", MIN_PBKDF2_ROUNDS - 1, encrypted_tail);
        let result = PwdChanCrypto::pbkdf2_compare(
            "password",
            &low_hash,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        );
        assert_eq!(result, Ok(false));

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_default_accept_max_allows_600k_and_10m_boundary() {
        reset_pbkdf2_rounds();

        // Unset accept-max falls back to MAX_PBKDF2_ROUNDS so upgraded
        // installs keep verifying hashes the plugin could previously create.
        assert_eq!(DEFAULT_PBKDF2_ACCEPT_MAX, MAX_PBKDF2_ROUNDS);
        assert_eq!(
            PwdChanCrypto::get_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256).unwrap(),
            DEFAULT_PBKDF2_ACCEPT_MAX
        );
        assert!(PwdChanCrypto::validate_pbkdf2_stored_iterations(
            600_000,
            SCHEME_PBKDF2_SHA256
        )
        .is_ok());
        assert!(PwdChanCrypto::validate_pbkdf2_stored_iterations(
            MAX_PBKDF2_ROUNDS,
            SCHEME_PBKDF2_SHA256
        )
        .is_ok());
        assert!(PwdChanCrypto::validate_pbkdf2_stored_iterations(
            MAX_PBKDF2_ROUNDS + 1,
            SCHEME_PBKDF2_SHA256
        )
        .is_err());

        // Compare maps out-of-range iterations to Ok(false), not Err.
        let encrypted_tail = "henZGfPWw79Cs8ORDeVNrQ$1dTJy73v6n3bnTmTZFghxHXHLsAzKaAy8SksDfZBPIw";
        let over_max_hash = format!("{}${}", MAX_PBKDF2_ROUNDS + 1, encrypted_tail);
        assert_eq!(
            PwdChanCrypto::pbkdf2_compare(
                "password",
                &over_max_hash,
                MessageDigest::sha256(),
                SCHEME_PBKDF2_SHA256,
            ),
            Ok(false)
        );

        reset_pbkdf2_rounds();
    }

    #[test]
    fn pwdchan_pbkdf2_sha1_basic() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2, 10000).unwrap();
        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA1, 10000).unwrap();

        // Legacy {PBKDF2} scheme
        let encrypted = "10000$IlfapjA351LuDSwYC0IQ8Q$saHqQTuYnjJN/tmAndT.8mJt.6w";
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password",
            encrypted,
            MessageDigest::sha1(),
            SCHEME_PBKDF2,
        ) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password!",
            encrypted,
            MessageDigest::sha1(),
            SCHEME_PBKDF2,
        ) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "incorrect",
            encrypted,
            MessageDigest::sha1(),
            SCHEME_PBKDF2,
        ) == Ok(false));

        // {PBKDF2-SHA1} scheme
        let encrypted = "10000$ZBEH6B07rgQpJSikyvMU2w$TAA03a5IYkz1QlPsbJKvUsTqNV";
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password",
            encrypted,
            MessageDigest::sha1(),
            SCHEME_PBKDF2_SHA1,
        ) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password!",
            encrypted,
            MessageDigest::sha1(),
            SCHEME_PBKDF2_SHA1,
        ) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_encrypt(
            "password",
            MessageDigest::sha1(),
            SCHEME_PBKDF2_SHA1,
        )
        .expect("Failed to hash");
        let test_enc = test_enc.replace("{PBKDF2-SHA1}", "");
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password",
            &test_enc,
            MessageDigest::sha1(),
            SCHEME_PBKDF2_SHA1,
        ) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password!",
            &test_enc,
            MessageDigest::sha1(),
            SCHEME_PBKDF2_SHA1,
        ) == Ok(false));

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn pwdchan_pbkdf2_sha256_basic() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 10000).unwrap();

        let encrypted = "10000$henZGfPWw79Cs8ORDeVNrQ$1dTJy73v6n3bnTmTZFghxHXHLsAzKaAy8SksDfZBPIw";
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password",
            encrypted,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        ) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password!",
            encrypted,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        ) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "incorrect",
            encrypted,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        ) == Ok(false));

        // This is a django password with their pbkdf2_sha256$ type.
        // "pbkdf2_sha256$36000$xIEozuZVAoYm$uW1b35DUKyhvQAf1mBqMvoBDcqSD06juzyO/nmyV0+w="
        //            salt -->  xIEozuZVAoYm
        // django doesn't base64 it's salt, so you need to base64 it to:
        //                      eElFb3p1WlZBb1lt
        let encrypted = "36000$eElFb3p1WlZBb1lt$uW1b35DUKyhvQAf1mBqMvoBDcqSD06juzyO/nmyV0+w=";
        assert!(
            PwdChanCrypto::pbkdf2_compare(
                "eicieY7ahchaoCh0eeTa",
                encrypted,
                MessageDigest::sha256(),
                SCHEME_PBKDF2_SHA256,
            ) == Ok(true)
        );
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password!",
            encrypted,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        ) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_encrypt(
            "password",
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        )
        .expect("Failed to hash");
        // Remove the header and check.
        let test_enc = test_enc.replace("{PBKDF2-SHA256}", "");
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password",
            &test_enc,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        ) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password!",
            &test_enc,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        ) == Ok(false));

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn pwdchan_pbkdf2_sha512_basic() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA512, 10000).unwrap();

        let encrypted = "10000$Je1Uw19Bfv5lArzZ6V3EPw$g4T/1sqBUYWl9o93MVnyQ/8zKGSkPbKaXXsT8WmysXQJhWy8MRP2JFudSL.N9RklQYgDPxPjnfum/F2f/TrppA";
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password",
            encrypted,
            MessageDigest::sha512(),
            SCHEME_PBKDF2_SHA512,
        ) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password!",
            encrypted,
            MessageDigest::sha512(),
            SCHEME_PBKDF2_SHA512,
        ) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "incorrect",
            encrypted,
            MessageDigest::sha512(),
            SCHEME_PBKDF2_SHA512,
        ) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_encrypt(
            "password",
            MessageDigest::sha512(),
            SCHEME_PBKDF2_SHA512,
        )
        .expect("Failed to hash");
        // Remove the header and check.
        let test_enc = test_enc.replace("{PBKDF2-SHA512}", "");
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password",
            &test_enc,
            MessageDigest::sha512(),
            SCHEME_PBKDF2_SHA512,
        ) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare(
            "password!",
            &test_enc,
            MessageDigest::sha512(),
            SCHEME_PBKDF2_SHA512,
        ) == Ok(false));

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }
}
