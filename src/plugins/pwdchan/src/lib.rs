#![deny(warnings)]
#[macro_use]
extern crate slapi_r_plugin;
use base64;
use openssl::{hash::MessageDigest, pkcs5::pbkdf2_hmac, rand::rand_bytes};
use slapi_r_plugin::prelude::*;
use std::fmt;

const PBKDF2_ROUNDS: usize = 10_000;
const PBKDF2_SALT_LEN: usize = 24;
const PBKDF2_SHA1_EXTRACT: usize = 20;
const PBKDF2_SHA256_EXTRACT: usize = 32;
const PBKDF2_SHA512_EXTRACT: usize = 64;

pub mod pbkdf2;
pub mod pbkdf2_sha1;
pub mod pbkdf2_sha256;
pub mod pbkdf2_sha512;

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
            PBKDF2_ROUNDS,
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
        fmt::write(&mut output, format_args!("{}$", PBKDF2_ROUNDS)).map_err(|e| {
            log_error!(ErrorLevel::Error, "Format Error -> {:?}", e);
            PluginError::Format
        })?;
        // the base64 salt
        base64::encode_config_buf(&salt, base64::STANDARD, &mut output);
        // Push the delim
        output.push_str("$");
        // Finally the base64 hash
        base64::encode_config_buf(&hash_input, base64::STANDARD, &mut output);
        // Return it
        Ok(output)
    }

    // Remember, encrypted does *not* have the scheme prepended.
    #[inline(always)]
    fn pbkdf2_sha1_compare(cleartext: &str, encrypted: &str) -> Result<bool, PluginError> {
        Self::pbkdf2_compare(cleartext, encrypted, MessageDigest::sha1())
    }

    #[inline(always)]
    fn pbkdf2_sha256_compare(cleartext: &str, encrypted: &str) -> Result<bool, PluginError> {
        Self::pbkdf2_compare(cleartext, encrypted, MessageDigest::sha256())
    }

    #[inline(always)]
    fn pbkdf2_sha512_compare(cleartext: &str, encrypted: &str) -> Result<bool, PluginError> {
        Self::pbkdf2_compare(cleartext, encrypted, MessageDigest::sha512())
    }

    #[inline(always)]
    fn pbkdf2_sha1_encrypt(cleartext: &str) -> Result<String, PluginError> {
        Self::pbkdf2_encrypt(cleartext, MessageDigest::sha1())
    }

    #[inline(always)]
    fn pbkdf2_sha256_encrypt(cleartext: &str) -> Result<String, PluginError> {
        Self::pbkdf2_encrypt(cleartext, MessageDigest::sha256())
    }

    #[inline(always)]
    fn pbkdf2_sha512_encrypt(cleartext: &str) -> Result<String, PluginError> {
        Self::pbkdf2_encrypt(cleartext, MessageDigest::sha512())
    }
}

#[cfg(test)]
mod tests {
    use crate::PwdChanCrypto;
    /*
     * '{PBKDF2}10000$IlfapjA351LuDSwYC0IQ8Q$saHqQTuYnjJN/tmAndT.8mJt.6w'
     * '{PBKDF2-SHA1}10000$ZBEH6B07rgQpJSikyvMU2w$TAA03a5IYkz1QlPsbJKvUsTqNV'
     * '{PBKDF2-SHA256}10000$henZGfPWw79Cs8ORDeVNrQ$1dTJy73v6n3bnTmTZFghxHXHLsAzKaAy8SksDfZBPIw'
     * '{PBKDF2-SHA512}10000$Je1Uw19Bfv5lArzZ6V3EPw$g4T/1sqBUYWl9o93MVnyQ/8zKGSkPbKaXXsT8WmysXQJhWy8MRP2JFudSL.N9RklQYgDPxPjnfum/F2f/TrppA'
     * '{ARGON2}$argon2id$v=19$m=65536,t=2,p=1$IyTQMsvzB2JHDiWx8fq7Ew$VhYOA7AL0kbRXI5g2kOyyp8St1epkNj7WZyUY4pAIQQ'
     */

    #[test]
    fn pwdchan_pbkdf2_sha1_basic() {
        let encrypted = "10000$IlfapjA351LuDSwYC0IQ8Q$saHqQTuYnjJN/tmAndT.8mJt.6w";
        assert!(PwdChanCrypto::pbkdf2_sha1_compare("password", encrypted) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_sha1_compare("password!", encrypted) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_sha1_compare("incorrect", encrypted) == Ok(false));

        // this value gave some invalid b64 errors due to trailing bits and padding.
        let encrypted = "10000$ZBEH6B07rgQpJSikyvMU2w$TAA03a5IYkz1QlPsbJKvUsTqNV";
        assert!(PwdChanCrypto::pbkdf2_sha1_compare("password", encrypted) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_sha1_compare("password!", encrypted) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_sha1_encrypt("password").expect("Failed to hash");
        // Remove the header and check.
        let test_enc = test_enc.replace("{PBKDF2-SHA1}", "");
        assert!(PwdChanCrypto::pbkdf2_sha1_compare("password", &test_enc) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_sha1_compare("password!", &test_enc) == Ok(false));
    }

    #[test]
    fn pwdchan_pbkdf2_sha256_basic() {
        let encrypted = "10000$henZGfPWw79Cs8ORDeVNrQ$1dTJy73v6n3bnTmTZFghxHXHLsAzKaAy8SksDfZBPIw";
        assert!(PwdChanCrypto::pbkdf2_sha256_compare("password", encrypted) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_sha256_compare("password!", encrypted) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_sha256_compare("incorrect", encrypted) == Ok(false));

        // This is a django password with their pbkdf2_sha256$ type.
        // "pbkdf2_sha256$36000$xIEozuZVAoYm$uW1b35DUKyhvQAf1mBqMvoBDcqSD06juzyO/nmyV0+w="
        //            salt -->  xIEozuZVAoYm
        // django doesn't base64 it's salt, so you need to base64 it to:
        //                      eElFb3p1WlZBb1lt
        let encrypted = "36000$eElFb3p1WlZBb1lt$uW1b35DUKyhvQAf1mBqMvoBDcqSD06juzyO/nmyV0+w=";
        assert!(
            PwdChanCrypto::pbkdf2_sha256_compare("eicieY7ahchaoCh0eeTa", encrypted) == Ok(true)
        );
        assert!(PwdChanCrypto::pbkdf2_sha256_compare("password!", encrypted) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_sha256_encrypt("password").expect("Failed to hash");
        // Remove the header and check.
        let test_enc = test_enc.replace("{PBKDF2-SHA256}", "");
        assert!(PwdChanCrypto::pbkdf2_sha256_compare("password", &test_enc) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_sha256_compare("password!", &test_enc) == Ok(false));
    }

    #[test]
    fn pwdchan_pbkdf2_sha512_basic() {
        let encrypted = "10000$Je1Uw19Bfv5lArzZ6V3EPw$g4T/1sqBUYWl9o93MVnyQ/8zKGSkPbKaXXsT8WmysXQJhWy8MRP2JFudSL.N9RklQYgDPxPjnfum/F2f/TrppA";
        assert!(PwdChanCrypto::pbkdf2_sha512_compare("password", encrypted) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_sha512_compare("password!", encrypted) == Ok(false));
        assert!(PwdChanCrypto::pbkdf2_sha512_compare("incorrect", encrypted) == Ok(false));

        let test_enc = PwdChanCrypto::pbkdf2_sha512_encrypt("password").expect("Failed to hash");
        // Remove the header and check.
        let test_enc = test_enc.replace("{PBKDF2-SHA512}", "");
        assert!(PwdChanCrypto::pbkdf2_sha512_compare("password", &test_enc) == Ok(true));
        assert!(PwdChanCrypto::pbkdf2_sha512_compare("password!", &test_enc) == Ok(false));
    }
}
