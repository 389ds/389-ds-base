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
// Each algorithm gets its own atomic counter for thread-safe round configuration
static PBKDF2_ROUNDS: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA1: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA256: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);
static PBKDF2_ROUNDS_SHA512: AtomicUsize = AtomicUsize::new(DEFAULT_PBKDF2_ROUNDS);

// Thread-local storage for test environment
#[cfg(test)]
thread_local! {
    static TEST_PBKDF2_ROUNDS: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ROUNDS_SHA1: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ROUNDS_SHA256: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
    static TEST_PBKDF2_ROUNDS_SHA512: std::cell::Cell<Option<usize>> = std::cell::Cell::new(None);
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
        fn scheme_name() -> &'static str { "PBKDF2" }
    }
}

mod pbkdf2_sha1 {
    use super::*;

    pub struct PwdChanPbkdf2Sha1;
    slapi_r_plugin_hooks!(pwdchan_pbkdf2_sha1, PwdChanPbkdf2Sha1);

    impl super::Pbkdf2Plugin for PwdChanPbkdf2Sha1 {
        fn digest_type() -> MessageDigest { MessageDigest::sha1() }
        fn scheme_name() -> &'static str { "PBKDF2-SHA1" }
    }
}

mod pbkdf2_sha256 {
    use super::*;

    pub struct PwdChanPbkdf2Sha256;
    slapi_r_plugin_hooks!(pwdchan_pbkdf2_sha256, PwdChanPbkdf2Sha256);

    impl super::Pbkdf2Plugin for PwdChanPbkdf2Sha256 {
        fn digest_type() -> MessageDigest { MessageDigest::sha256() }
        fn scheme_name() -> &'static str { "PBKDF2-SHA256" }
    }
}

mod pbkdf2_sha512 {
    use super::*;

    pub struct PwdChanPbkdf2Sha512;
    slapi_r_plugin_hooks!(pwdchan_pbkdf2_sha512, PwdChanPbkdf2Sha512);

    impl super::Pbkdf2Plugin for PwdChanPbkdf2Sha512 {
        fn digest_type() -> MessageDigest { MessageDigest::sha512() }
        fn scheme_name() -> &'static str { "PBKDF2-SHA512" }
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
                PwdChanCrypto::handle_pbkdf2_rounds_config(pb, Self::digest_type())?;
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
                PwdChanCrypto::pbkdf2_encrypt(cleartext, Self::digest_type())
            }

            fn pwd_storage_compare(cleartext: &str, encrypted: &str) -> Result<bool, PluginError> {
                PwdChanCrypto::pbkdf2_compare(cleartext, encrypted, Self::digest_type())
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
    #[inline(always)]
    fn pbkdf2_decompose(encrypted: &str) -> Result<(usize, Vec<u8>, Vec<u8>), PluginError> {
        let mut part_iter = encrypted.split('$');

        let iter = part_iter
            .next()
            .ok_or(PluginError::MissingValue)
            .and_then(|iter_str| {
                usize::from_str_radix(iter_str, 10).map_err(|e| {
                    log_error!(ErrorLevel::Error, "Invalid Integer {} -> {:?}", iter_str, e);
                    PluginError::InvalidStrToInt
                })
            })?;

        let salt = part_iter
            .next()
            .ok_or(PluginError::MissingValue)
            .and_then(|ab64| {
                let s = ab64_to_b64!(ab64);
                base64::decode_config(&s, base64::STANDARD.decode_allow_trailing_bits(true))
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
                base64::decode_config(&s, base64::STANDARD.decode_allow_trailing_bits(true))
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
    ) -> Result<bool, PluginError> {
        let (iter, salt, hash_expected) = Self::pbkdf2_decompose(encrypted).map_err(|e| {
            // This means our DB content is flawed.
            log_error!(ErrorLevel::Error, "invalid hashed pw -> {:?}", e);
            e
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

    fn pbkdf2_encrypt(cleartext: &str, digest: MessageDigest) -> Result<String, PluginError> {
        let rounds = Self::get_pbkdf2_rounds(digest)?;

        let (hash_length, str_length, header) = if digest == MessageDigest::sha1() {
            (PBKDF2_SHA1_EXTRACT, 80, "{PBKDF2-SHA1}")
        } else if digest == MessageDigest::sha256() {
            (PBKDF2_SHA256_EXTRACT, 100, "{PBKDF2-SHA256}")
        } else if digest == MessageDigest::sha512() {
            (PBKDF2_SHA512_EXTRACT, 140, "{PBKDF2-SHA512}")
        } else {
            return Err(PluginError::Unknown);
        };

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

    // Private helper method to get the appropriate AtomicUsize reference
    fn get_rounds_atomic(digest: MessageDigest) -> &'static AtomicUsize {
        match digest {
            d if d == MessageDigest::sha1() => &PBKDF2_ROUNDS_SHA1,
            d if d == MessageDigest::sha256() => &PBKDF2_ROUNDS_SHA256,
            d if d == MessageDigest::sha512() => &PBKDF2_ROUNDS_SHA512,
            _ => &PBKDF2_ROUNDS,
        }
    }

    fn handle_pbkdf2_rounds_config(pb: &mut PblockRef, digest: MessageDigest) -> Result<(), PluginError> {
        let mut rounds = Self::get_rounds_atomic(digest).load(Ordering::Relaxed);
        let mut source = "default";

        // Try to get the entry from the parameter block
        let entry = pb.get_op_add_entryref()
            .map_err(|_| PluginError::InvalidConfiguration)?;

        // Check if the rounds attribute exists and get its value
        if let Some(value_array) = entry.get_attr(PBKDF2_ROUNDS_ATTR) {
            if let Some(value) = value_array.first() {
                let rounds_str: String = value
                    .as_ref()
                    .try_into()
                    .map_err(|_| {
                        log_error!(
                            ErrorLevel::Error,
                            "Failed to parse {} value",
                            PBKDF2_ROUNDS_ATTR
                        );
                        PluginError::InvalidConfiguration
                    })?;

                rounds = rounds_str.parse::<usize>().map_err(|e| {
                    log_error!(
                        ErrorLevel::Error,
                        "Invalid PBKDF2 number of iterations value '{}': {}",
                        rounds_str,
                        e
                    );
                    PluginError::InvalidConfiguration
                })?;
                source = "configuration";
            }
        }

        Self::set_pbkdf2_rounds(digest, rounds)?;

        let digest_name = if digest == MessageDigest::sha1() {
            "PBKDF2-SHA1"
        } else if digest == MessageDigest::sha256() {
            "PBKDF2-SHA256"
        } else if digest == MessageDigest::sha512() {
            "PBKDF2-SHA512"
        } else {
            "Unknown"
        };

        log_error_ext!(
            ErrorLevel::Info,
            digest_name,
            "Number of iterations set to {} from {}",
            rounds,
            source
        );
        Ok(())
    }

    fn set_pbkdf2_rounds(digest: MessageDigest, rounds: usize) -> Result<(), PluginError> {
        if rounds < MIN_PBKDF2_ROUNDS || rounds > MAX_PBKDF2_ROUNDS {
            log_error!(
                ErrorLevel::Error,
                "Invalid PBKDF2 number of iterations {}, must be between {} and {}",
                rounds,
                MIN_PBKDF2_ROUNDS,
                MAX_PBKDF2_ROUNDS
            );
            return Err(PluginError::InvalidConfiguration);
        }

        #[cfg(test)]
        {
            // In test mode, store in thread-local storage
            match digest {
                d if d == MessageDigest::sha1() => {
                    TEST_PBKDF2_ROUNDS_SHA1.with(|cell| cell.set(Some(rounds)));
                },
                d if d == MessageDigest::sha256() => {
                    TEST_PBKDF2_ROUNDS_SHA256.with(|cell| cell.set(Some(rounds)));
                },
                d if d == MessageDigest::sha512() => {
                    TEST_PBKDF2_ROUNDS_SHA512.with(|cell| cell.set(Some(rounds)));
                },
                _ => {
                    TEST_PBKDF2_ROUNDS.with(|cell| cell.set(Some(rounds)));
                },
            }
        }

        #[cfg(not(test))]
        {
            Self::get_rounds_atomic(digest).store(rounds, Ordering::Relaxed);
        }

        Ok(())
    }

    fn get_pbkdf2_rounds(digest: MessageDigest) -> Result<usize, PluginError> {
        #[cfg(test)]
        {
            // In test mode, try to get from thread-local storage first
            let thread_local_value = match digest {
                d if d == MessageDigest::sha1() => {
                    TEST_PBKDF2_ROUNDS_SHA1.with(|cell| cell.get())
                },
                d if d == MessageDigest::sha256() => {
                    TEST_PBKDF2_ROUNDS_SHA256.with(|cell| cell.get())
                },
                d if d == MessageDigest::sha512() => {
                    TEST_PBKDF2_ROUNDS_SHA512.with(|cell| cell.get())
                },
                _ => {
                    TEST_PBKDF2_ROUNDS.with(|cell| cell.get())
                },
            };
            
            // If thread-local value exists, use it; otherwise fall back to global
            if let Some(value) = thread_local_value {
                return Ok(value);
            }
        }
        
        // If not in test mode or no thread-local value, use global
        Ok(Self::get_rounds_atomic(digest).load(Ordering::Relaxed))
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
        
        // Set default values for this thread
        PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha1(), DEFAULT_PBKDF2_ROUNDS).unwrap();
        PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha256(), DEFAULT_PBKDF2_ROUNDS).unwrap();
        PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha512(), DEFAULT_PBKDF2_ROUNDS).unwrap();
    }

    #[test]
    fn test_pbkdf2_encrypt_with_different_rounds() {
        // Reset to defaults first
        reset_pbkdf2_rounds();
        
        // Set different rounds for each algorithm
        assert!(PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha1(), 15000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha256(), 20000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha512(), 25000).is_ok());

        // Verify rounds are correctly set
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(MessageDigest::sha1()).unwrap(), 15000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(MessageDigest::sha256()).unwrap(), 20000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(MessageDigest::sha512()).unwrap(), 25000);

        let test_password = "test_password";

        // Test SHA1
        let sha1_result = PwdChanCrypto::pbkdf2_encrypt(test_password, MessageDigest::sha1()).unwrap();
        // Check if it has the header and extract the rounds
        assert!(sha1_result.starts_with("{PBKDF2-SHA1}"));
        let sha1_no_header = sha1_result.replace("{PBKDF2-SHA1}", "");
        let sha1_parts: Vec<&str> = sha1_no_header.split('$').collect();
        let rounds: usize = sha1_parts[0].parse().unwrap();
        assert_eq!(rounds, 15000, "SHA1 rounds should be 15000, got {}", rounds);
        
        // Test SHA256
        let sha256_result = PwdChanCrypto::pbkdf2_encrypt(test_password, MessageDigest::sha256()).unwrap();
        // Check if it has the header and extract the rounds
        assert!(sha256_result.starts_with("{PBKDF2-SHA256}"));
        let sha256_no_header = sha256_result.replace("{PBKDF2-SHA256}", "");
        let sha256_parts: Vec<&str> = sha256_no_header.split('$').collect();
        let rounds: usize = sha256_parts[0].parse().unwrap();
        assert_eq!(rounds, 20000, "SHA256 rounds should be 20000, got {}", rounds);
        
        // Test SHA512
        let sha512_result = PwdChanCrypto::pbkdf2_encrypt(test_password, MessageDigest::sha512()).unwrap();
        // Check if it has the header and extract the rounds
        assert!(sha512_result.starts_with("{PBKDF2-SHA512}"));
        let sha512_no_header = sha512_result.replace("{PBKDF2-SHA512}", "");
        let sha512_parts: Vec<&str> = sha512_no_header.split('$').collect();
        let rounds: usize = sha512_parts[0].parse().unwrap();
        assert_eq!(rounds, 25000, "SHA512 rounds should be 25000, got {}", rounds);
        
        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_rounds_configuration() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        // Test different rounds for each algorithm
        assert!(PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha1(), 15000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha256(), 20000).is_ok());
        assert!(PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha512(), 25000).is_ok());

        // Verify each algorithm has its own rounds setting
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(MessageDigest::sha1()).unwrap(), 15000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(MessageDigest::sha256()).unwrap(), 20000);
        assert_eq!(PwdChanCrypto::get_pbkdf2_rounds(MessageDigest::sha512()).unwrap(), 25000);

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_rounds_limits() {
        // Reset to defaults first
        reset_pbkdf2_rounds();

        // Test max limit - should fail
        let result = PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha1(), MAX_PBKDF2_ROUNDS + 1);
        assert!(result.is_err());

        // Test min rounds - should succeed
        let result = PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha1(), MIN_PBKDF2_ROUNDS);
        assert!(result.is_ok());

        // Test invalid rounds for SHA256 - too low - should fail
        let result = PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha256(), MIN_PBKDF2_ROUNDS - 1);
        assert!(result.is_err());

        // Test max rounds - should succeed
        let result = PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha256(), MAX_PBKDF2_ROUNDS);
        assert!(result.is_ok());

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn test_pbkdf2_decompose() {
        let valid_hash = "10000$salt123$hash456";
        let result = PwdChanCrypto::pbkdf2_decompose(valid_hash);
        assert!(result.is_ok());
        let (iter, _salt, _hash) = result.unwrap();
        assert_eq!(iter, 10000);

        // Test invalid format
        let invalid_hash = "invalid";
        assert!(PwdChanCrypto::pbkdf2_decompose(invalid_hash).is_err());
    }

    #[test]
    fn pwdchan_pbkdf2_sha1_basic() {
        // Reset to defaults first
        reset_pbkdf2_rounds();
        
        PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha1(), 10000).unwrap();

        let encrypted = "10000$IlfapjA351LuDSwYC0IQ8Q$saHqQTuYnjJN/tmAndT.8mJt.6w";
        assert!(PwdChanCrypto::pbkdf2_compare("password", encrypted, MessageDigest::sha1()) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare("password!", encrypted, MessageDigest::sha1()) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_compare("incorrect", encrypted, MessageDigest::sha1()) == Ok(false));

        let encrypted = "10000$ZBEH6B07rgQpJSikyvMU2w$TAA03a5IYkz1QlPsbJKvUsTqNV";
        assert!(PwdChanCrypto::pbkdf2_compare("password", encrypted, MessageDigest::sha1()) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare("password!", encrypted, MessageDigest::sha1()) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_encrypt("password", MessageDigest::sha1()).expect("Failed to hash");
        let test_enc = test_enc.replace("{PBKDF2-SHA1}", "");
        assert!(PwdChanCrypto::pbkdf2_compare("password", &test_enc, MessageDigest::sha1()) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare("password!", &test_enc, MessageDigest::sha1()) == Ok(false));

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn pwdchan_pbkdf2_sha256_basic() {
        // Reset to defaults first
        reset_pbkdf2_rounds();
        
        PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha256(), 10000).unwrap();

        let encrypted = "10000$henZGfPWw79Cs8ORDeVNrQ$1dTJy73v6n3bnTmTZFghxHXHLsAzKaAy8SksDfZBPIw";
        assert!(PwdChanCrypto::pbkdf2_compare("password", encrypted, MessageDigest::sha256()) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare("password!", encrypted, MessageDigest::sha256()) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_compare("incorrect", encrypted, MessageDigest::sha256()) == Ok(false));

        // This is a django password with their pbkdf2_sha256$ type.
        // "pbkdf2_sha256$36000$xIEozuZVAoYm$uW1b35DUKyhvQAf1mBqMvoBDcqSD06juzyO/nmyV0+w="
        //            salt -->  xIEozuZVAoYm
        // django doesn't base64 it's salt, so you need to base64 it to:
        //                      eElFb3p1WlZBb1lt
        let encrypted = "36000$eElFb3p1WlZBb1lt$uW1b35DUKyhvQAf1mBqMvoBDcqSD06juzyO/nmyV0+w=";
        assert!(
            PwdChanCrypto::pbkdf2_compare("eicieY7ahchaoCh0eeTa", encrypted, MessageDigest::sha256()) == Ok(true)
        );
        assert!(PwdChanCrypto::pbkdf2_compare("password!", encrypted, MessageDigest::sha256()) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_encrypt("password", MessageDigest::sha256()).expect("Failed to hash");
        // Remove the header and check.
        let test_enc = test_enc.replace("{PBKDF2-SHA256}", "");
        assert!(PwdChanCrypto::pbkdf2_compare("password", &test_enc, MessageDigest::sha256()) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare("password!", &test_enc, MessageDigest::sha256()) == Ok(false));

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }

    #[test]
    fn pwdchan_pbkdf2_sha512_basic() {
        // Reset to defaults first
        reset_pbkdf2_rounds();
        
        PwdChanCrypto::set_pbkdf2_rounds(MessageDigest::sha512(), 10000).unwrap();

        let encrypted = "10000$Je1Uw19Bfv5lArzZ6V3EPw$g4T/1sqBUYWl9o93MVnyQ/8zKGSkPbKaXXsT8WmysXQJhWy8MRP2JFudSL.N9RklQYgDPxPjnfum/F2f/TrppA";
        assert!(PwdChanCrypto::pbkdf2_compare("password", encrypted, MessageDigest::sha512()) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare("password!", encrypted, MessageDigest::sha512()) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_compare("incorrect", encrypted, MessageDigest::sha512()) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_encrypt("password", MessageDigest::sha512()).expect("Failed to hash");
        // Remove the header and check.
        let test_enc = test_enc.replace("{PBKDF2-SHA512}", "");
        assert!(PwdChanCrypto::pbkdf2_compare("password", &test_enc, MessageDigest::sha512()) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_compare("password!", &test_enc, MessageDigest::sha512()) == Ok(false));

        // Reset to defaults after test
        reset_pbkdf2_rounds();
    }
}
