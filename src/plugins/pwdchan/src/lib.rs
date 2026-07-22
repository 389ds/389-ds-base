#![deny(warnings)]
#[macro_use]
extern crate slapi_r_plugin;
use base64;
use openssl::{hash::MessageDigest, pkcs5::pbkdf2_hmac, rand::rand_bytes};
use slapi_r_plugin::prelude::*;
use std::fmt::Write;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::convert::TryInto;
use std::os::raw::c_char;

const DEFAULT_PBKDF2_ROUNDS: usize = 100_000;
const MIN_PBKDF2_ROUNDS: usize = 10_000;
const MAX_PBKDF2_ROUNDS: usize = 10_000_000;

const PBKDF2_ROUNDS_ATTR: &str = "nsslapd-pwdPBKDF2NumIterations";
const PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR: &str = "nsslapd-pwdPBKDF2AcceptMaxIterations";

// Scheme identifiers, used for per-scheme config and atomics below.
const SCHEME_PBKDF2: &str = "PBKDF2";
const SCHEME_PBKDF2_SHA1: &str = "PBKDF2-SHA1";
const SCHEME_PBKDF2_SHA256: &str = "PBKDF2-SHA256";
const SCHEME_PBKDF2_SHA512: &str = "PBKDF2-SHA512";

// Each algorithm gets its own atomic counter for thread-safe round and accept max iterations
static PBKDF2_ROUNDS: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA1: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA256: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA512: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);

static PBKDF2_ACCEPT_MAX: AtomicUsize = AtomicUsize::new(0);
static PBKDF2_ACCEPT_MAX_SHA1: AtomicUsize = AtomicUsize::new(0);
static PBKDF2_ACCEPT_MAX_SHA256: AtomicUsize = AtomicUsize::new(0);
static PBKDF2_ACCEPT_MAX_SHA512: AtomicUsize = AtomicUsize::new(0);

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
        Self::validate_pbkdf2_rounds(iterations)?;

        let accept_max = Self::get_pbkdf2_accept_max(scheme)?;
        if iterations > accept_max {
            #[cfg(not(test))]
            log_error!(
                ErrorLevel::Warning,
                "{} iteration count {} exceeds accept max {}",
                scheme,
                iterations,
                accept_max
            );
            return Err(PluginError::InvalidConfiguration);
        }

        Ok(())
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
                base64::decode_config(&s, base64::STANDARD.decode_allow_trailing_bits(true))
                    .map_err(|_e| {
                        #[cfg(not(test))]
                        log_error!(ErrorLevel::Error, "Invalid Base 64 {} -> {:?}", s, _e);
                        PluginError::InvalidBase64
                    })
            })?;

        let hash = part_iter
            .next()
            .ok_or(PluginError::MissingValue)
            .and_then(|ab64| {
                let s = ab64_to_b64!(ab64);
                base64::decode_config(&s, base64::STANDARD.decode_allow_trailing_bits(true))
                    .map_err(|_e| {
                        #[cfg(not(test))]
                        log_error!(ErrorLevel::Error, "Invalid Base 64 {} -> {:?}", s, _e);
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
        let (iter, salt, hash_expected) = Self::pbkdf2_decompose(encrypted, scheme)
            .map_err(|e| {
                match e {
                    // Iteration bounds are logged in validate_pbkdf2_stored_iterations.
                    PluginError::InvalidConfiguration => e,
                    _ => {
                        #[cfg(not(test))]
                        log_error!(ErrorLevel::Error, "invalid hashed pw -> {:?}", e);
                        e
                    }
                }
            })?;
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
        let rounds = Self::get_pbkdf2_rounds(scheme)?;
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
        base64::encode_config_buf(&salt, base64::STANDARD, &mut output);
        // Push the delim
        output.push('$');
        // Finally the base64 hash
        base64::encode_config_buf(&hash_input, base64::STANDARD, &mut output);

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

    fn handle_pbkdf2_config(pb: &mut PblockRef, scheme: &str) -> Result<(), PluginError> {
        let mut rounds = Self::get_pbkdf2_rounds(scheme)?;
        let mut source = "default";

        let entry = pb.get_op_add_entryref()
            .map_err(|_| PluginError::InvalidConfiguration)?;

        if entry.get_attr(PBKDF2_ROUNDS_ATTR).is_some() {
            rounds = Self::parse_config_usize_attr(&entry, PBKDF2_ROUNDS_ATTR)?;
            source = "configuration";
        }

        let mut accept_max = rounds;

        if entry.get_attr(PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR).is_some() {
            accept_max = Self::parse_config_usize_attr(&entry, PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR)?;
        }

        // Preserve accept_max >= rounds: raise accept_max first when needed.
        let current_rounds = Self::get_pbkdf2_rounds(scheme)?;
        if accept_max >= current_rounds {
            Self::set_pbkdf2_accept_max(scheme, accept_max)?;
            Self::set_pbkdf2_rounds(scheme, rounds)?;
        } else {
            Self::set_pbkdf2_rounds(scheme, rounds)?;
            Self::set_pbkdf2_accept_max(scheme, accept_max)?;
        }

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

        // Skip when accept max is unset (0); compare then falls back to rounds.
        #[cfg(test)]
        let configured_accept_max = Self::get_test_accept_max(scheme);
        #[cfg(not(test))]
        let configured_accept_max = {
            let accept_max = Self::get_accept_max_atomic(scheme)?.load(Ordering::Relaxed);
            if accept_max == 0 {
                None
            } else {
                Some(accept_max)
            }
        };
        if let Some(accept_max) = configured_accept_max {
            if rounds > accept_max {
                #[cfg(not(test))]
                log_error_ext!(
                    ErrorLevel::Error,
                    scheme,
                    "Invalid rounds {} for {}, must be <= {} {}",
                    rounds,
                    scheme,
                    PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR,
                    accept_max
                );
                return Err(PluginError::InvalidConfiguration);
            }
        }

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

        let rounds = Self::get_pbkdf2_rounds(scheme)?;
        if accept_max < rounds {
            #[cfg(not(test))]
            log_error_ext!(
                ErrorLevel::Error,
                scheme,
                "Invalid {} value {} for {}, must be >= configured rounds {}",
                PBKDF2_ACCEPT_MAX_ITERATIONS_ATTR,
                accept_max,
                scheme,
                rounds
            );
            return Err(PluginError::InvalidConfiguration);
        }

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

        let accept_max = Self::get_accept_max_atomic(scheme)?.load(Ordering::Relaxed);
        if accept_max == 0 {
            return Self::get_pbkdf2_rounds(scheme);
        }

        Ok(accept_max)
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
            PwdChanCrypto::set_pbkdf2_accept_max(scheme, DEFAULT_PBKDF2_ROUNDS).unwrap();
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

        // Test max rounds - should succeed
        // Accept max must be raised first so max rounds is allowed.
        PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, MAX_PBKDF2_ROUNDS).unwrap();
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
    fn test_pbkdf2_accept_max_must_be_at_least_rounds() {
        reset_pbkdf2_rounds();

        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 50000).unwrap();

        let result = PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 40000);
        assert!(result.is_err());

        let result = PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 50000);
        assert!(result.is_ok());

        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_rounds_must_not_exceed_accept_max() {
        reset_pbkdf2_rounds();

        PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 20000).unwrap();
        PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 20000).unwrap();

        // Raising rounds above accept max would create hashes compare rejects.
        let result = PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 30000);
        assert!(result.is_err());

        let result = PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 20000);
        assert!(result.is_ok());

        PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 30000).unwrap();
        let result = PwdChanCrypto::set_pbkdf2_rounds(SCHEME_PBKDF2_SHA256, 30000);
        assert!(result.is_ok());

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

        // Test high accept max - should succeed
        let result = PwdChanCrypto::set_pbkdf2_accept_max(SCHEME_PBKDF2_SHA256, 600000);
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
        let high_hash = format!("{}$salt123$hash456", DEFAULT_PBKDF2_ROUNDS + 1);
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

        // Stored iterations above accept max - should fail
        let encrypted = "36000$eElFb3p1WlZBb1lt$uW1b35DUKyhvQAf1mBqMvoBDcqSD06juzyO/nmyV0+w=";
        let result = PwdChanCrypto::pbkdf2_compare(
            "eicieY7ahchaoCh0eeTa",
            encrypted,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        );
        assert!(result.is_err());

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

        // Stored iterations above accept max - should fail
        let high_hash = format!("{}${}", DEFAULT_PBKDF2_ROUNDS + 1, encrypted_tail);
        let result = PwdChanCrypto::pbkdf2_compare(
            "password",
            &high_hash,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        );
        assert!(result.is_err());

        // Stored iterations below min - should fail
        let low_hash = format!("{}${}", MIN_PBKDF2_ROUNDS - 1, encrypted_tail);
        let result = PwdChanCrypto::pbkdf2_compare(
            "password",
            &low_hash,
            MessageDigest::sha256(),
            SCHEME_PBKDF2_SHA256,
        );
        assert!(result.is_err());

        // Reset to defaults after test
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
