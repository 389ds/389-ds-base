/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* SSL-related stuff for slapd */

#include <stdio.h>
#include <sys/param.h>
#include <ssl.h>
#include <nss.h>
#include <keyhi.h>
#include <sslproto.h>
#include "secmod.h"
#include <string.h>
#include <errno.h>

#define NEED_TOK_PBE /* defines tokPBE and ptokPBE - see slap.h */
#include "slap.h"
#include <unistd.h>

#include "svrcore.h"
#include "fe.h"
#include "certdb.h"

/* For IRIX... */
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif


/******************************************************************************
 * Default SSL Version Rule
 * Old SSL version attributes:
 *   nsSSL3: off -- nsSSL3 == SSL_LIBRARY_VERSION_3_0
 *   nsTLS1: on  -- nsTLS1 == SSL_LIBRARY_VERSION_TLS_1_2 and greater
 *   Note: TLS1.0 is defined in RFC2246, which is close to SSL 3.0.
 * New SSL version attributes:
 *   sslVersionMin: TLS1.2
 *   sslVersionMax: max ssl version supported by NSS
 ******************************************************************************/

#define DEFVERSION "TLS1.2"

extern char *slapd_SSL3ciphers;
extern symbol_t supported_ciphers[];
static SSLVersionRange defaultNSSVersions;
static SSLVersionRange supportedNSSVersions;
static SSLVersionRange slapdNSSVersions;


/* dongle_file_name is set in slapd_nss_init when we set the path for the
   key, cert, and secmod files - the dongle file must be in the same directory
   and use the same naming scheme
*/
static char *dongle_file_name = NULL;

static int _security_library_initialized = 0;
static int _ssl_listener_initialized = 0;
static int _nss_initialized = 0;

/* Our name for the internal token, must match PKCS-11 config data below */
static char *internalTokenName = "Internal (Software) Token";

static int stimeout;
static char *ciphers = NULL;
static char *configDN = "cn=encryption,cn=config";


/* Copied from libadmin/libadmin.h public/nsapi.h */
#define SERVER_KEY_NAME "Server-Key"
#define MAGNUS_ERROR_LEN 1024
#define LOG_WARN 0
#define LOG_FAILURE 3
#define LOG_MSG 4
#define FILE_PATHSEP '/'

/* ----------------------- Multiple cipher support ------------------------ */
/* cipher set flags */
#define CIPHER_SET_NONE 0x0
#define CIPHER_SET_ALL 0x1
#define CIPHER_SET_DEFAULT 0x2
#define CIPHER_SET_DEFAULTWEAKCIPHER 0x10  /* allowWeakCipher is not set in cn=encryption */
#define CIPHER_SET_ALLOWWEAKCIPHER 0x20    /* allowWeakCipher is on */
#define CIPHER_SET_DISALLOWWEAKCIPHER 0x40 /* allowWeakCipher is off */
#define CIPHER_SET_DEFAULTWEAKDHPARAM 0x100  /* allowWeakDhParam is not set in cn=encryption */
#define CIPHER_SET_ALLOWWEAKDHPARAM 0x200    /* allowWeakDhParam is on */
#define CIPHER_SET_DISALLOWWEAKDHPARAM 0x400 /* allowWeakDhParam is off */

#define CIPHER_SET_ISDEFAULT(flag) \
    (((flag)&CIPHER_SET_DEFAULT) ? PR_TRUE : PR_FALSE)
#define CIPHER_SET_ISALL(flag) \
    (((flag)&CIPHER_SET_ALL) ? PR_TRUE : PR_FALSE)

#define ALLOWWEAK_ISDEFAULT(flag) \
    (((flag)&CIPHER_SET_DEFAULTWEAKCIPHER) ? PR_TRUE : PR_FALSE)
#define ALLOWWEAK_ISON(flag) \
    (((flag)&CIPHER_SET_ALLOWWEAKCIPHER) ? PR_TRUE : PR_FALSE)
#define ALLOWWEAK_ISOFF(flag) \
    (((flag)&CIPHER_SET_DISALLOWWEAKCIPHER) ? PR_TRUE : PR_FALSE)

/*
 * If ISALL or ISDEFAULT, allowWeakCipher is true only if CIPHER_SET_ALLOWWEAKCIPHER.
 * Otherwise (user specified cipher list), allowWeakCipher is true
 * if CIPHER_SET_ALLOWWEAKCIPHER or CIPHER_SET_DEFAULTWEAKCIPHER.
 */
#define CIPHER_SET_ALLOWSWEAKCIPHER(flag) \
    ((CIPHER_SET_ISDEFAULT(flag) | CIPHER_SET_ISALL(flag)) ? (ALLOWWEAK_ISON(flag) ? PR_TRUE : PR_FALSE) : (!ALLOWWEAK_ISOFF(flag) ? PR_TRUE : PR_FALSE))

#define CIPHER_SET_DISABLE_ALLOWSWEAKCIPHER(flag) \
    ((flag) & ~CIPHER_SET_ALLOWWEAKCIPHER)

/* flags */
#define CIPHER_IS_DEFAULT 0x1
#define CIPHER_MUST_BE_DISABLED 0x2
#define CIPHER_IS_WEAK 0x4
#define CIPHER_IS_DEPRECATED 0x8

static int allowweakdhparam = CIPHER_SET_DEFAULTWEAKDHPARAM;

static char **cipher_names = NULL;
static char **enabled_cipher_names = NULL;
typedef struct
{
    char *name;
    int num;
    int flags;
} cipherstruct;

static cipherstruct *_conf_ciphers = NULL;
static void _conf_init_ciphers(void);

/* E.g., "SSL3", "TLS1.2", "Unknown SSL version: 0x0" */
#define VERSION_STR_LENGTH 64

/* Supported SSL versions  */
/* nsSSL2: on -- we don't allow this any more. */
PRBool enableSSL2 = PR_FALSE;
/*
 * nsSSL3: on -- disable SSLv3 by default.
 * Corresonding to SSL_LIBRARY_VERSION_3_0
 */
PRBool enableSSL3 = PR_FALSE;
/*
 * nsTLS1: on -- enable TLS1 by default.
 * Corresonding to SSL_LIBRARY_VERSION_TLS_1_2 and greater.
 */
PRBool enableTLS1 = PR_TRUE;

/*
 * OpenLDAP client library with OpenSSL (ticket 47536)
 */
#define PEMEXT ".pem"
/* CA cert pem file */
static char *CACertPemFile = NULL;

/* helper functions for openldap update. */
static int slapd_extract_cert(Slapi_Entry *entry, int isCA);
static int slapd_extract_key(Slapi_Entry *entry, char *token, PK11SlotInfo *slot);
static void entrySetValue(Slapi_DN *sdn, char *type, char *value);
static char *gen_pem_path(char *filename);

static void
slapd_SSL_report(int degree, char *fmt, va_list args)
{
    char buf[2048];
    char *msg = NULL;
    int sev;

    if (degree == LOG_FAILURE) {
        sev = SLAPI_LOG_ERR;
        msg = "failure";
    } else if (degree == LOG_WARN) {
        sev = SLAPI_LOG_WARNING;
        msg = "alert";
    } else {
        sev = SLAPI_LOG_INFO;
        msg = "info";
    }
    PR_vsnprintf(buf, sizeof(buf), fmt, args);
    slapi_log_err(sev, "Security Initialization", "SSL %s: %s\n", msg, buf);
}

void
slapd_SSL_error(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    slapd_SSL_report(LOG_FAILURE, fmt, args);
    va_end(args);
}

void
slapd_SSL_warn(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    slapd_SSL_report(LOG_WARN, fmt, args);
    va_end(args);
}

void
slapd_SSL_info(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    slapd_SSL_report(LOG_MSG, fmt, args);
    va_end(args);
}

char **
getSupportedCiphers(void)
{
    SSLCipherSuiteInfo info;
    char *sep = "::";
    int number_of_ciphers = SSL_NumImplementedCiphers;
    int idx = 0;
    PRBool isFIPS = slapd_pk11_isFIPS();

    _conf_init_ciphers();

    if ((cipher_names == NULL) && (_conf_ciphers)) {
        cipher_names = (char **)slapi_ch_calloc((number_of_ciphers + 1), sizeof(char *));
        for (size_t i = 0; _conf_ciphers[i].name != NULL; i++) {
            SSL_GetCipherSuiteInfo((PRUint16)_conf_ciphers[i].num, &info, sizeof(info));
            /* only support FIPS approved ciphers in FIPS mode */
            if (!isFIPS || info.isFIPS) {
                cipher_names[idx++] = slapi_ch_smprintf("%s%s%s%s%s%s%d",
                                                        _conf_ciphers[i].name, sep,
                                                        info.symCipherName, sep,
                                                        info.macAlgorithmName, sep,
                                                        info.symKeyBits);
            }
        }
        cipher_names[idx] = NULL;
    }
    return cipher_names;
}

int
get_allow_weak_dh_param(Slapi_Entry *e)
{
    /* Check if the user wants weak params */
    int allow = CIPHER_SET_DEFAULTWEAKDHPARAM;
    char *val;
    val = slapi_entry_attr_get_charptr(e, "allowWeakDHParam");
    if (val) {
        if (!PL_strcasecmp(val, "off") || !PL_strcasecmp(val, "false") ||
            !PL_strcmp(val, "0") || !PL_strcasecmp(val, "no")) {
            allow = CIPHER_SET_DISALLOWWEAKDHPARAM;
        } else if (!PL_strcasecmp(val, "on") || !PL_strcasecmp(val, "true") ||
                   !PL_strcmp(val, "1") || !PL_strcasecmp(val, "yes")) {
            allow = CIPHER_SET_ALLOWWEAKDHPARAM;
            slapd_SSL_warn("The value of allowWeakDHParam is set to %s. THIS EXPOSES YOU TO CVE-2015-4000.", val);
        } else {
            slapd_SSL_warn("The value of allowWeakDHParam \"%s\" is invalid.",
                           "Ignoring it and set it to default.", val);
        }
    }
    slapi_ch_free((void **)&val);
    return allow;
}


char **
getEnabledCiphers(void)
{
    SSLCipherSuiteInfo info;
    char *sep = "::";
    int number_of_ciphers = 0;
    int idx = 0;
    PRBool enabled;

    /* We have to wait until the SSL initialization is done. */
    if (!slapd_ssl_listener_is_initialized()) {
        return NULL;
    }
    if ((enabled_cipher_names == NULL) && _conf_ciphers) {
        for (size_t x = 0; _conf_ciphers[x].name; x++) {
            SSL_CipherPrefGetDefault(_conf_ciphers[x].num, &enabled);
            if (enabled) {
                number_of_ciphers++;
            }
        }
        enabled_cipher_names = (char **)slapi_ch_calloc((number_of_ciphers + 1), sizeof(char *));
        for (size_t x = 0; _conf_ciphers[x].name; x++) {
            SSL_CipherPrefGetDefault(_conf_ciphers[x].num, &enabled);
            if (enabled) {
                SSL_GetCipherSuiteInfo((PRUint16)_conf_ciphers[x].num, &info, sizeof(info));
                enabled_cipher_names[idx++] = slapi_ch_smprintf("%s%s%s%s%s%s%d",
                                                                _conf_ciphers[x].name, sep,
                                                                info.symCipherName, sep,
                                                                info.macAlgorithmName, sep,
                                                                info.symKeyBits);
            }
        }
    }

    return enabled_cipher_names;
}

static PRBool
cipher_check_fips(int idx, char ***suplist, char ***unsuplist)
{
    PRBool rc = PR_TRUE;

    if (_conf_ciphers && slapd_pk11_isFIPS()) {
        SSLCipherSuiteInfo info;
        if (SECFailure == SSL_GetCipherSuiteInfo((PRUint16)_conf_ciphers[idx].num,
                                                 &info, sizeof info)) {
            PRErrorCode errorCode = PR_GetError();
            if (slapi_is_loglevel_set(SLAPI_LOG_CONFIG)) {
                slapd_SSL_warn("No information for cipher suite [%s] "
                               "error %d - %s",
                               _conf_ciphers[idx].name,
                               errorCode, slapd_pr_strerror(errorCode));
            }
            rc = PR_FALSE;
        }
        if (rc && !info.isFIPS) {
            if (slapi_is_loglevel_set(SLAPI_LOG_CONFIG)) {
                slapd_SSL_warn("FIPS mode is enabled but "
                               "cipher suite [%s] is not approved for FIPS - "
                               "the cipher suite will be disabled - if "
                               "you want to use this cipher suite, you must use modutil to "
                               "disable FIPS in the internal token.",
                               _conf_ciphers[idx].name);
            }
            rc = PR_FALSE;
        }
        if (!rc && unsuplist && !charray_inlist(*unsuplist, _conf_ciphers[idx].name)) {
            charray_add(unsuplist, _conf_ciphers[idx].name);
        }
        if (rc && suplist && !charray_inlist(*suplist, _conf_ciphers[idx].name)) {
            charray_add(suplist, _conf_ciphers[idx].name);
        }
    }
    return rc;
}

int
getSSLVersionInfo(int *ssl2, int *ssl3, int *tls1)
{
    if (!slapd_ssl_listener_is_initialized()) {
        return -1;
    }
    *ssl2 = enableSSL2;
    *ssl3 = enableSSL3;
    *tls1 = enableTLS1;
    return 0;
}

int
getSSLVersionRange(char **min, char **max)
{
    if (!min && !max) {
        return -1;
    }
    if (!slapd_ssl_listener_is_initialized()) {
        /*
         * We have not initialized NSS yet, so we will set the default for
         * now. Then it will get adjusted to NSS's default min and max once
         * we complete the security initialization in slapd_ssl_init2()
         */
        if (min) {
            *min = slapi_getSSLVersion_str(LDAP_OPT_X_TLS_PROTOCOL_TLS1_2, NULL, 0);
        }
        if (max) {
            *max = slapi_getSSLVersion_str(LDAP_OPT_X_TLS_PROTOCOL_TLS1_2, NULL, 0);
        }
        return -1;
    }
    if (min) {
        *min = slapi_getSSLVersion_str(slapdNSSVersions.min, NULL, 0);
    }
    if (max) {
        *max = slapi_getSSLVersion_str(slapdNSSVersions.max, NULL, 0);
    }
    return 0;
}

void
getSSLVersionRangeOL(int *min, int *max)
{
    /* default range values */
    if (min) {
        *min = LDAP_OPT_X_TLS_PROTOCOL_TLS1_2;
    }
    if (max) {
        *max = LDAP_OPT_X_TLS_PROTOCOL_TLS1_2;
    }
    if (!slapd_ssl_listener_is_initialized()) {
        return;
    }

    if (min) {
        switch (slapdNSSVersions.min) {
        case SSL_LIBRARY_VERSION_3_0:
            *min = LDAP_OPT_X_TLS_PROTOCOL_SSL3;
            break;
        case SSL_LIBRARY_VERSION_TLS_1_0:
            *min = LDAP_OPT_X_TLS_PROTOCOL_TLS1_0;
            break;
        case SSL_LIBRARY_VERSION_TLS_1_1:
            *min = LDAP_OPT_X_TLS_PROTOCOL_TLS1_1;
            break;
        case SSL_LIBRARY_VERSION_TLS_1_2:
            *min = LDAP_OPT_X_TLS_PROTOCOL_TLS1_2;
            break;
        default:
            if (slapdNSSVersions.min > SSL_LIBRARY_VERSION_TLS_1_2) {
                *min = LDAP_OPT_X_TLS_PROTOCOL_TLS1_2 + 1;
            } else {
                *min = LDAP_OPT_X_TLS_PROTOCOL_SSL3;
            }
            break;
        }
    }
    if (max) {
        switch (slapdNSSVersions.max) {
        case SSL_LIBRARY_VERSION_3_0:
            *max = LDAP_OPT_X_TLS_PROTOCOL_SSL3;
            break;
        case SSL_LIBRARY_VERSION_TLS_1_0:
            *max = LDAP_OPT_X_TLS_PROTOCOL_TLS1_0;
            break;
        case SSL_LIBRARY_VERSION_TLS_1_1:
            *max = LDAP_OPT_X_TLS_PROTOCOL_TLS1_1;
            break;
        case SSL_LIBRARY_VERSION_TLS_1_2:
            *max = LDAP_OPT_X_TLS_PROTOCOL_TLS1_2;
            break;
        default:
            if (slapdNSSVersions.max > SSL_LIBRARY_VERSION_TLS_1_2) {
                *max = LDAP_OPT_X_TLS_PROTOCOL_TLS1_2 + 1;
            } else {
                *max = LDAP_OPT_X_TLS_PROTOCOL_SSL3;
            }
            break;
        }
    }
    return;
}

static void
_conf_init_ciphers(void)
{
    SECStatus rc;
    SSLCipherSuiteInfo info;
    const PRUint16 *implementedCiphers = SSL_GetImplementedCiphers();

    /* Initialize _conf_ciphers */
    if (_conf_ciphers) {
        return;
    }
    _conf_ciphers = (cipherstruct *)slapi_ch_calloc(SSL_NumImplementedCiphers + 1, sizeof(cipherstruct));

    for (size_t x = 0; implementedCiphers && (x < SSL_NumImplementedCiphers); x++) {
        rc = SSL_GetCipherSuiteInfo(implementedCiphers[x], &info, sizeof info);
        if (SECFailure == rc) {
            slapi_log_err(SLAPI_LOG_ERR, "Security Initialization",
                          "_conf_init_ciphers - Failed to get the cipher suite info of cipher ID %d\n",
                          implementedCiphers[x]);
            continue;
        }
        if (!_conf_ciphers[x].num) { /* initialize each cipher */
            _conf_ciphers[x].name = slapi_ch_strdup(info.cipherSuiteName);
            _conf_ciphers[x].num = implementedCiphers[x];
            if (info.symCipher == ssl_calg_null) {
                _conf_ciphers[x].flags |= CIPHER_MUST_BE_DISABLED;
            } else {
                _conf_ciphers[x].flags |= info.isExportable ? CIPHER_IS_WEAK : (info.symCipher < ssl_calg_3des) ? CIPHER_IS_WEAK : (info.effectiveKeyBits < 128) ? CIPHER_IS_WEAK : 0;
            }
        }
    }
    return;
}

/*
 * flag: CIPHER_SET_ALL     -- enable all
 *       CIPHER_SET_NONE    -- disable all
 *       CIPHER_SET_DEFAULT -- set default ciphers
 *       CIPHER_SET_ALLOW_WEAKCIPHER -- allow weak ciphers (can be or'ed with the ather CIPHER_SET flags)
 */
static void
_conf_setallciphers(int flag, char ***suplist, char ***unsuplist)
{
    SECStatus rc;
    PRBool setdefault = CIPHER_SET_ISDEFAULT(flag);
    PRBool enabled = CIPHER_SET_ISALL(flag);
    PRBool allowweakcipher = CIPHER_SET_ALLOWSWEAKCIPHER(flag);
    PRBool setme = PR_FALSE;
    const PRUint16 *implementedCiphers = SSL_GetImplementedCiphers();

    _conf_init_ciphers();

    for (size_t x = 0; implementedCiphers && (x < SSL_NumImplementedCiphers); x++) {
        if (_conf_ciphers[x].flags & CIPHER_IS_DEFAULT) {
            /* certainly, not the first time. */
            setme = PR_TRUE;
        } else if (setdefault) {
            /*
             * SSL_CipherPrefGetDefault
             * If the application has not previously set the default preference,
             * SSL_CipherPrefGetDefault returns the factory setting.
             */
            rc = SSL_CipherPrefGetDefault(_conf_ciphers[x].num, &setme);
            if (SECFailure == rc) {
                slapi_log_err(SLAPI_LOG_ERR, "Security Initialization",
                              "_conf_setallciphers - Failed to get the default state of cipher %s\n",
                              _conf_ciphers[x].name);
                continue;
            }
            if (!allowweakcipher && (_conf_ciphers[x].flags & CIPHER_IS_WEAK)) {
                setme = PR_FALSE;
            }
            _conf_ciphers[x].flags |= setme ? CIPHER_IS_DEFAULT : 0;
        } else if (enabled && !(_conf_ciphers[x].flags & CIPHER_MUST_BE_DISABLED)) {
            if (!allowweakcipher && (_conf_ciphers[x].flags & CIPHER_IS_WEAK)) {
                setme = PR_FALSE;
            } else {
                setme = PR_TRUE;
            }
        } else {
            setme = PR_FALSE;
        }
        if (setme) {
            setme = cipher_check_fips(x, suplist, unsuplist);
        }
        SSL_CipherPrefSetDefault(_conf_ciphers[x].num, setme);
    }
}

static char *
charray2str(char **ary, const char *delim)
{
    char *str = NULL;
    while (ary && *ary) {
        if (str) {
            str = PR_sprintf_append(str, "%s%s", delim, *ary++);
        } else {
            str = slapi_ch_smprintf("%s", *ary++);
        }
    }

    return str;
}

void
_conf_dumpciphers(void)
{
    PRBool enabled;
    /* {"SSL3","rc4", SSL_EN_RC4_128_WITH_MD5}, */
    slapd_SSL_info("Configured NSS Ciphers");
    for (size_t x = 0; _conf_ciphers[x].name; x++) {
        SSL_CipherPrefGetDefault(_conf_ciphers[x].num, &enabled);
        if (enabled) {
            slapd_SSL_info("\t%s: enabled%s%s%s", _conf_ciphers[x].name,
                           (_conf_ciphers[x].flags & CIPHER_IS_WEAK) ? ", (WEAK CIPHER)" : "",
                           (_conf_ciphers[x].flags & CIPHER_IS_DEPRECATED) ? ", (DEPRECATED)" : "",
                           (_conf_ciphers[x].flags & CIPHER_MUST_BE_DISABLED) ? ", (MUST BE DISABLED)" : "");
        } else if (slapi_is_loglevel_set(SLAPI_LOG_CONFIG)) {
            slapd_SSL_info("\t%s: disabled%s%s%s", _conf_ciphers[x].name,
                           (_conf_ciphers[x].flags & CIPHER_IS_WEAK) ? ", (WEAK CIPHER)" : "",
                           (_conf_ciphers[x].flags & CIPHER_IS_DEPRECATED) ? ", (DEPRECATED)" : "",
                           (_conf_ciphers[x].flags & CIPHER_MUST_BE_DISABLED) ? ", (MUST BE DISABLED)" : "");
        }
    }
}

char *
_conf_setciphers(char *setciphers, int flags)
{
    char *t, err[MAGNUS_ERROR_LEN];
    int active;
    size_t x = 0;
    char *raw = setciphers;
    char **suplist = NULL;
    char **unsuplist = NULL;
    PRBool enabledOne = PR_FALSE;

    /* #47838: harden the list of ciphers available by default */
    /* Default is to activate all of them ==> none of them*/
    if (!setciphers || (setciphers[0] == '\0') || !PL_strcasecmp(setciphers, "default")) {
        _conf_setallciphers((CIPHER_SET_DEFAULT | flags), NULL, NULL);
        slapd_SSL_info("Enabling default cipher set.");
        _conf_dumpciphers();
        return NULL;
    }

    if (PL_strcasestr(setciphers, "+all")) {
        /*
         * Enable all the ciphers if "+all" and the following while loop would
         * disable the user disabled ones.  This is needed because we added a new
         * set of ciphers in the table. Right now there is no support for this
         * from the console
         */
        _conf_setallciphers((CIPHER_SET_ALL | flags), &suplist, NULL);
        enabledOne = PR_TRUE;
    } else {
        /* If "+all" is not in nsSSL3Ciphers value, disable all first,
         * then enable specified ciphers. */
        _conf_setallciphers(CIPHER_SET_NONE /* disabled */, NULL, NULL);
    }

    t = setciphers;
    while (t) {
        while ((*setciphers) && (isspace(*setciphers)))
            ++setciphers;

        switch (*setciphers++) {
        case '+':
            active = 1;
            break;
        case '-':
            active = 0;
            break;
        default:
            if (strlen(raw) > MAGNUS_ERROR_LEN) {
                PR_snprintf(err, sizeof(err) - 3, "%s...", raw);
                return slapi_ch_smprintf("invalid ciphers <%s>: format is +cipher1,-cipher2...", err);
            } else {
                return slapi_ch_smprintf("invalid ciphers <%s>: format is +cipher1,-cipher2...", raw);
            }
        }
        if ((t = strchr(setciphers, ',')))
            *t++ = '\0';

        if (strcasecmp(setciphers, "all")) { /* if not all */
            PRBool enabled = active ? PR_TRUE : PR_FALSE;
            for (x = 0; _conf_ciphers[x].name; x++) {
                if (!PL_strcasecmp(setciphers, _conf_ciphers[x].name)) {
                    if (_conf_ciphers[x].flags & CIPHER_IS_WEAK) {
                        if (active && CIPHER_SET_ALLOWSWEAKCIPHER(flags)) {
                            slapd_SSL_warn("Cipher %s is weak.  It is enabled since allowWeakCipher is \"on\" "
                                           "(default setting for the backward compatibility). "
                                           "We strongly recommend to set it to \"off\".  "
                                           "Please replace the value of allowWeakCipher with \"off\" in "
                                           "the encryption config entry cn=encryption,cn=config and "
                                           "restart the server.",
                                           setciphers);
                        } else {
                            /* if the cipher is weak and we don't allow weak cipher,
                               disable it. */
                            enabled = PR_FALSE;
                        }
                    }
                    if (enabled) {
                        /* if the cipher is not weak or we allow weak cipher,
                           check fips. */
                        enabled = cipher_check_fips(x, NULL, &unsuplist);
                    }
                    if (enabled) {
                        enabledOne = PR_TRUE; /* At least one active cipher is set. */
                    }
                    SSL_CipherPrefSetDefault(_conf_ciphers[x].num, enabled);
                    break;
                }
            }
            if (!_conf_ciphers[x].name) {
                slapd_SSL_warn("Cipher suite %s is not available in NSS %d.%d.  Ignoring %s",
                               setciphers, NSS_VMAJOR, NSS_VMINOR, setciphers);
            }
        }
        if (t) {
            setciphers = t;
        }
    }
    if (unsuplist && *unsuplist) {
        char *strsup = charray2str(suplist, ",");
        char *strunsup = charray2str(unsuplist, ",");
        slapd_SSL_warn("FIPS mode is enabled - only the following "
                       "cipher suites are approved for FIPS: [%s] - "
                       "the specified cipher suites [%s] are disabled - if "
                       "you want to use these unsupported cipher suites, you must use modutil to "
                       "disable FIPS in the internal token.",
                       strsup ? strsup : "(none)", strunsup ? strunsup : "(none)");
        slapi_ch_free_string(&strsup);
        slapi_ch_free_string(&strunsup);
    }

    slapi_ch_free((void **)&suplist);   /* strings inside are static */
    slapi_ch_free((void **)&unsuplist); /* strings inside are static */

    if (!enabledOne) {
        char *nocipher = slapi_ch_smprintf("No active cipher suite is available.");
        return nocipher;
    }
    _conf_dumpciphers();

    return NULL;
}

/* SSL Policy stuff */

/*
 * SSLPLCY_Install
 *
 * Call the SSL_CipherPolicySet function for each ciphersuite.
 */
PRStatus
SSLPLCY_Install(void)
{

    SECStatus s = 0;

    s = NSS_SetDomesticPolicy();

    return s ? PR_FAILURE : PR_SUCCESS;
}

/**
 * Get a particular entry
 */
static Slapi_Entry *
getConfigEntry(const char *dn, Slapi_Entry **e2)
{
    Slapi_DN sdn;

    slapi_sdn_init_dn_byref(&sdn, dn);
    slapi_search_internal_get_entry(&sdn, NULL, e2,
                                    plugin_get_default_component_id());
    slapi_sdn_done(&sdn);
    return *e2;
}

/**
 * Free an entry
 */
static void
freeConfigEntry(Slapi_Entry **e)
{
    if ((e != NULL) && (*e != NULL)) {
        slapi_entry_free(*e);
        *e = NULL;
    }
}

/**
 * Get a list of child DNs
 */
static char **
getChildren(char *dn)
{
    Slapi_PBlock *new_pb = NULL;
    Slapi_Entry **e;
    int search_result = 1;
    int nEntries = 0;
    char **list = NULL;

    new_pb = slapi_search_internal(dn, LDAP_SCOPE_ONELEVEL,
                                   "(objectclass=nsEncryptionModule)",
                                   NULL, NULL, 0);

    slapi_pblock_get(new_pb, SLAPI_NENTRIES, &nEntries);
    if (nEntries > 0) {
        slapi_pblock_get(new_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);
        slapi_pblock_get(new_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &e);
        if (e != NULL) {
            list = (char **)slapi_ch_malloc(sizeof(*list) * (nEntries + 1));
            for (size_t i = 0; e[i] != NULL; i++) {
                list[i] = slapi_ch_strdup(slapi_entry_get_dn(e[i]));
            }
            list[nEntries] = NULL;
        }
    }
    slapi_free_search_results_internal(new_pb);
    slapi_pblock_destroy(new_pb);
    return list;
}

/**
 * Free a list of child DNs
 */
static void
freeChildren(char **list)
{
    if (list != NULL) {
        for (size_t i = 0; list[i] != NULL; i++) {
            slapi_ch_free((void **)(&list[i]));
        }
        slapi_ch_free((void **)(&list));
    }
}

static void
entrySetValue(Slapi_DN *sdn, char *type, char *value)
{
    Slapi_PBlock *mypb = slapi_pblock_new();
    LDAPMod attr;
    LDAPMod *mods[2];
    char *values[2];

    values[0] = value;
    values[1] = NULL;

    /* modify the attribute */
    attr.mod_type = type;
    attr.mod_op = LDAP_MOD_REPLACE;
    attr.mod_values = values;

    mods[0] = &attr;
    mods[1] = NULL;

    slapi_modify_internal_set_pb_ext(mypb, sdn, mods, NULL, NULL, (void *)plugin_get_default_component_id(), 0);
    slapi_modify_internal_pb(mypb);
    slapi_pblock_destroy(mypb);
}

/* Logs a warning and returns 1 if cert file doesn't exist. You
 * can skip the warning log message by setting no_log to 1.*/
static int
warn_if_no_cert_file(const char *dir, int no_log)
{
    int ret = 0;
    char *filename = slapi_ch_smprintf("%s/cert8.db", dir);
    PRStatus status = PR_Access(filename, PR_ACCESS_READ_OK);
    if (PR_SUCCESS != status) {
        slapi_ch_free_string(&filename);
        filename = slapi_ch_smprintf("%s/cert9.db", dir);
        status = PR_Access(filename, PR_ACCESS_READ_OK);
        if (PR_SUCCESS != status) {
            ret = 1;
            if (!no_log) {
                slapi_log_err(SLAPI_LOG_CRIT, "Security Initialization",
                              "warn_if_no_cert_file - Certificate DB file cert8.db nor cert9.db exists in [%s] - SSL initialization will likely fail\n", dir);
            }
        }
    }

    slapi_ch_free_string(&filename);
    return ret;
}

/* Logs a warning and returns 1 if key file doesn't exist. You
 * can skip the warning log message by setting no_log to 1.*/
static int
warn_if_no_key_file(const char *dir, int no_log)
{
    int ret = 0;
    char *filename = slapi_ch_smprintf("%s/key3.db", dir);
    PRStatus status = PR_Access(filename, PR_ACCESS_READ_OK);
    if (PR_SUCCESS != status) {
        slapi_ch_free_string(&filename);
        filename = slapi_ch_smprintf("%s/key4.db", dir);
        status = PR_Access(filename, PR_ACCESS_READ_OK);
        if (PR_SUCCESS != status) {
            ret = 1;
            if (!no_log) {
                slapi_log_err(SLAPI_LOG_CRIT, "Security Initialization",
                              "warn_if_no_key_file - Key DB file key3.db nor key4.db exists in [%s] - SSL initialization will likely fail\n", dir);
            }
        }
    }

    slapi_ch_free_string(&filename);
    return ret;
}

/*
 * If non NULL buf and positive bufsize is given,
 * the memory is used to store the version string.
 * Otherwise, the memory for the string is allocated.
 * The latter case, caller is responsible to free it.
 */
char *
slapi_getSSLVersion_str(PRUint16 vnum, char *buf, size_t bufsize)
{
    char *vstr = buf;
    if (vnum >= SSL_LIBRARY_VERSION_3_0) {
        if (vnum == SSL_LIBRARY_VERSION_3_0) { /* SSL3 */
            if (buf && bufsize) {
                PR_snprintf(buf, bufsize, "SSL3");
            } else {
                vstr = slapi_ch_smprintf("SSL3");
            }
        } else { /* TLS v X.Y */
            const char *TLSFMT = "TLS%d.%d";
            int minor_offset = 0; /* e.g. 0x0401 -> TLS v 2.1, not 2.0 */

            if ((vnum & SSL_LIBRARY_VERSION_3_0) == SSL_LIBRARY_VERSION_3_0) {
                minor_offset = 1; /* e.g. 0x0301 -> TLS v 1.0, not 1.1 */
            }
            if (buf && bufsize) {
                PR_snprintf(buf, bufsize, TLSFMT, (vnum >> 8) - 2, (vnum & 0xff) - minor_offset);
            } else {
                vstr = slapi_ch_smprintf(TLSFMT, (vnum >> 8) - 2, (vnum & 0xff) - minor_offset);
            }
        }
    } else if (vnum == SSL_LIBRARY_VERSION_2) { /* SSL2 */
        if (buf && bufsize) {
            PR_snprintf(buf, bufsize, "SSL2");
        } else {
            vstr = slapi_ch_smprintf("SSL2");
        }
    } else {
        if (buf && bufsize) {
            PR_snprintf(buf, bufsize, "Unknown SSL version: 0x%x", vnum);
        } else {
            vstr = slapi_ch_smprintf("Unknown SSL version: 0x%x", vnum);
        }
    }
    return vstr;
}

#define SSLVGreater(x, y) (((x) > (y)) ? (x) : (y))

/*
 * slapd_nss_init() is always called from main(), even if we do not
 * plan to listen on a secure port.  If config_available is 0, the
 * config. entries from dse.ldif are NOT available (used only when
 * running in referral mode).
 * As of DS6.1, the init_ssl flag passed is ignored.
 *
 * richm 20070126 - By default now we put the key/cert db files
 * in an instance specific directory (the certdir directory) so
 * we do not need a prefix any more.
 */
int
slapd_nss_init(int init_ssl __attribute__((unused)), int config_available __attribute__((unused)))
{
    SECStatus secStatus;
    PRErrorCode errorCode;
    int rv = 0;
    int len = 0;
    int create_certdb = 0;
    PRUint32 nssFlags = 0;
    char *certdir;
    char dmin[VERSION_STR_LENGTH], dmax[VERSION_STR_LENGTH];
    char smin[VERSION_STR_LENGTH], smax[VERSION_STR_LENGTH];

    /* Get the range of the supported SSL version */
    SSL_VersionRangeGetSupported(ssl_variant_stream, &supportedNSSVersions);
    (void)slapi_getSSLVersion_str(supportedNSSVersions.min, smin, sizeof(smin));
    (void)slapi_getSSLVersion_str(supportedNSSVersions.max, smax, sizeof(smax));

    /* Get the enabled default range */
    SSL_VersionRangeGetDefault(ssl_variant_stream, &defaultNSSVersions);
    (void)slapi_getSSLVersion_str(defaultNSSVersions.min, dmin, sizeof(dmin));
    (void)slapi_getSSLVersion_str(defaultNSSVersions.max, dmax, sizeof(dmax));
    slapi_log_err(SLAPI_LOG_CONFIG, "Security Initialization",
                  "slapd_nss_init - Supported range by NSS: min: %s, max: %s\n",
                  smin, smax);
    slapi_log_err(SLAPI_LOG_CONFIG, "Security Initialization",
                  "slapd_nss_init - Enabled default range by NSS: min: %s, max: %s\n",
                  dmin, dmax);

    /* set in slapd_bootstrap_config,
       thus certdir is available even if config_available is false */
    certdir = config_get_certdir();

    /* make sure path does not end in the path separator character */
    len = strlen(certdir);
    if (certdir[len - 1] == '/' || certdir[len - 1] == '\\') {
        certdir[len - 1] = '\0';
    }

    /* If the server is configured to use SSL, we must have a key and cert db */
    if (config_get_security()) {
        warn_if_no_cert_file(certdir, 0);
        warn_if_no_key_file(certdir, 0);
    } else { /* otherwise, NSS will create empty databases */
        /* we open the key/cert db in rw mode, so make sure the directory
           is writable */
        if (PR_SUCCESS != PR_Access(certdir, PR_ACCESS_WRITE_OK)) {
            char *serveruser = "unknown";

            serveruser = config_get_localuser();
            slapi_log_err(SLAPI_LOG_CRIT, "Security Initialization",
                          "slapd_nss_init - The key/cert database directory [%s] is not writable by "
                          "the server uid [%s]: initialization likely to fail.\n",
                          certdir, serveruser);
            slapi_ch_free_string(&serveruser);
        }
    }

    /* Check if we have a certdb already.  If not, set a flag that we are
     * going to create one so we can set the appropriate permissions on it. */
    if (warn_if_no_cert_file(certdir, 1) || warn_if_no_key_file(certdir, 1)) {
        create_certdb = 1;
    }

    /******** Initialise NSS *********/

    nssFlags &= (~NSS_INIT_READONLY);
    slapd_pk11_configurePKCS11(NULL, NULL, tokPBE, ptokPBE, NULL, NULL, NULL, NULL, 0, 0);
    secStatus = NSS_Initialize(certdir, NULL, NULL, "secmod.db", nssFlags);

    dongle_file_name = slapi_ch_smprintf("%s/pin.txt", certdir);

    if (secStatus != SECSuccess) {
        errorCode = PR_GetError();
        slapd_SSL_error("NSS initialization failed (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s): "
                        "certdir: %s",
                        errorCode, slapd_pr_strerror(errorCode), certdir);
        rv = -1;
    }

    if (SSLPLCY_Install() != PR_SUCCESS) {
        errorCode = PR_GetError();
        slapd_SSL_error("Unable to set SSL export policy (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                        errorCode, slapd_pr_strerror(errorCode));
        return -1;
    }

    /* NSS creates the certificate db files with a mode of 600.  There
     * is no way to pass in a mode to use for creation to NSS, so we
     * need to modify it after creation.  We need to allow read and
     * write permission to the group so the certs can be managed via
     * the console/adminserver. */
    if (create_certdb) {
        char *cert8db_file_name = NULL;
        char *cert9db_file_name = NULL;
        char *key3db_file_name = NULL;
        char *key4db_file_name = NULL;
        char *secmoddb_file_name = NULL;
        char *pkcs11txt_file_name = NULL;


        cert8db_file_name = slapi_ch_smprintf("%s/cert8.db", certdir);
        cert9db_file_name = slapi_ch_smprintf("%s/cert9.db", certdir);
        key3db_file_name = slapi_ch_smprintf("%s/key3.db", certdir);
        key4db_file_name = slapi_ch_smprintf("%s/key4.db", certdir);
        secmoddb_file_name = slapi_ch_smprintf("%s/secmod.db", certdir);
        pkcs11txt_file_name = slapi_ch_smprintf("%s/pkcs11.txt", certdir);

        if (access(cert8db_file_name, F_OK) == 0 &&
            chmod(cert8db_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) {
            slapi_log_err(SLAPI_LOG_WARNING, "Security Initialization", "slapd_nss_init - chmod failed for file %s error (%d) %s.\n",
                          cert8db_file_name, errno, slapd_system_strerror(errno));
        }
        if (access(cert9db_file_name, F_OK) == 0 &&
            chmod(cert9db_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) {
            slapi_log_err(SLAPI_LOG_WARNING, "Security Initialization", "slapd_nss_init - chmod failed for file %s error (%d) %s.\n",
                          cert9db_file_name, errno, slapd_system_strerror(errno));
        }
        if (access(key3db_file_name, F_OK) == 0 &&
            chmod(key3db_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) {
            slapi_log_err(SLAPI_LOG_WARNING, "Security Initialization", "slapd_nss_init - chmod failed for file %s error (%d) %s.\n",
                          key3db_file_name, errno, slapd_system_strerror(errno));
        }
        if (access(key4db_file_name, F_OK) == 0 &&
            chmod(key4db_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) {
            slapi_log_err(SLAPI_LOG_WARNING, "Security Initialization", "slapd_nss_init - chmod failed for file %s error (%d) %s.\n",
                          key4db_file_name, errno, slapd_system_strerror(errno));
        }
        if (access(secmoddb_file_name, F_OK) == 0 &&
            chmod(secmoddb_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) {
            slapi_log_err(SLAPI_LOG_WARNING, "Security Initialization", "slapd_nss_init - chmod failed for file %s error (%d) %s.\n",
                          secmoddb_file_name, errno, slapd_system_strerror(errno));
        }
        if (access(pkcs11txt_file_name, F_OK) == 0 &&
            chmod(pkcs11txt_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)) {
            slapi_log_err(SLAPI_LOG_WARNING, "Security Initialization", "slapd_nss_init - chmod failed for file %s error (%d) %s.\n",
                          pkcs11txt_file_name, errno, slapd_system_strerror(errno));
        }

        slapi_ch_free_string(&cert8db_file_name);
        slapi_ch_free_string(&cert9db_file_name);
        slapi_ch_free_string(&key3db_file_name);
        slapi_ch_free_string(&key4db_file_name);
        slapi_ch_free_string(&secmoddb_file_name);
        slapi_ch_free_string(&pkcs11txt_file_name);
    }

    /****** end of NSS Initialization ******/
    _nss_initialized = 1;
    slapi_ch_free_string(&certdir);
    return rv;
}

static int
svrcore_setup(void)
{
    PRErrorCode errorCode;
    int rv = 0;
#ifdef WITH_SYSTEMD
    SVRCOREStdSystemdPinObj *StdPinObj;
    StdPinObj = (SVRCOREStdSystemdPinObj *)SVRCORE_GetRegisteredPinObj();
#else
    SVRCOREStdPinObj *StdPinObj;
    StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
#endif

    if (StdPinObj) {
        return 0; /* already registered */
    }
#ifdef WITH_SYSTEMD
    if (SVRCORE_CreateStdSystemdPinObj(&StdPinObj, dongle_file_name, PR_TRUE, PR_TRUE, 90) != SVRCORE_Success) {
#else
    if (SVRCORE_CreateStdPinObj(&StdPinObj, dongle_file_name, PR_TRUE) != SVRCORE_Success) {
#endif
        errorCode = PR_GetError();
        slapd_SSL_warn("Unable to create PinObj (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                       errorCode, slapd_pr_strerror(errorCode));
        return -1;
    }
    SVRCORE_RegisterPinObj((SVRCOREPinObj *)StdPinObj);

    return rv;
}

/*
 * slapd_ssl_init() is called from main() if we plan to listen
 * on a secure port.
 */
int
slapd_ssl_init()
{
    PRErrorCode errorCode;
    char **family_list;
    char *val = NULL;
    PK11SlotInfo *slot;
    Slapi_Entry *entry = NULL;
    SECStatus rv = SECFailure;

    /* Get general information */

    getConfigEntry(configDN, &entry);

    val = slapi_entry_attr_get_charptr(entry, "nssslSessionTimeout");
    ciphers = slapi_entry_attr_get_charptr(entry, "nsssl3ciphers");

    allowweakdhparam = get_allow_weak_dh_param(entry);
    if (allowweakdhparam & CIPHER_SET_ALLOWWEAKDHPARAM) {
        slapd_SSL_warn("notice, generating new WEAK DH param");
        rv = SSL_EnableWeakDHEPrimeGroup(NULL, PR_TRUE);
        if (rv != SECSuccess) {
            slapd_SSL_error("Warning, unable to generate weak dh parameters");
        }
    }

    /* We are currently using the value of sslSessionTimeout
       for ssl3SessionTimeout, see SSL_ConfigServerSessionIDCache() */
    /* Note from Tom Weinstein on the meaning of the timeout:

       Timeouts are in seconds.  '0' means use the default, which is
       24hrs for SSL3 and 100 seconds for SSL2.
    */

    if (!val) {
        errorCode = PR_GetError();
        slapd_SSL_error("Failed to retrieve SSL "
                        "configuration information (" SLAPI_COMPONENT_NAME_NSPR " error %d - not found): "
                        "nssslSessionTimeout: %s ",
                        errorCode, slapd_pr_strerror(errorCode));
        slapi_ch_free((void **)&ciphers);
        freeConfigEntry(&entry);
        return -1;
    }

    stimeout = atoi(val);
    slapi_ch_free((void **)&val);

    if (svrcore_setup()) {
        freeConfigEntry(&entry);
        return -1;
    }
    if (config_get_extract_pem()) {
        /* extract cert file and convert it to a pem file. */
        slapd_extract_cert(entry, PR_TRUE);
    }

    if ((family_list = getChildren(configDN))) {
        char **family;
        char *token;
        char *activation;
        int isinternal = 0;

        for (family = family_list; *family; family++) {

            token = NULL;
            activation = NULL;

            freeConfigEntry(&entry);

            getConfigEntry(*family, &entry);
            if (entry == NULL) {
                continue;
            }

            activation = slapi_entry_attr_get_charptr(entry, "nssslactivation");
            if ((!activation) || (!PL_strcasecmp(activation, "off"))) {
                /* this family was turned off, goto next */
                slapi_ch_free((void **)&activation);
                continue;
            }

            slapi_ch_free((void **)&activation);

            token = slapi_entry_attr_get_charptr(entry, "nsssltoken");
            if (token) {
                if (!PL_strcasecmp(token, "internal") ||
                    !PL_strcasecmp(token, "internal (software)")) {
                    slot = slapd_pk11_getInternalKeySlot();
                    isinternal = 1;
                } else {
                    slot = slapd_pk11_findSlotByName(token);
                }
            } else {
                errorCode = PR_GetError();
                slapd_SSL_error("Unable to get token (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                                errorCode, slapd_pr_strerror(errorCode));
                freeChildren(family_list);
                freeConfigEntry(&entry);
                return -1;
            }

            if (!slot) {
                errorCode = PR_GetError();
                slapd_SSL_error("Unable to find slot (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                                errorCode, slapd_pr_strerror(errorCode));
                freeChildren(family_list);
                freeConfigEntry(&entry);
                slapi_ch_free((void **)&token);
                return -1;
            }
/* authenticate */
#ifdef WITH_SYSTEMD
            slapd_SSL_warn("Sending pin request to SVRCore. You may need to run"
                           " systemd-tty-ask-password-agent to provide the password.");
#endif
            if (slapd_pk11_authenticate(slot, PR_TRUE, NULL) != SECSuccess) {
                errorCode = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "Security Initialization",
                              "slapd_ssl_init - Unable to authenticate (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
                              errorCode, slapd_pr_strerror(errorCode));
                freeChildren(family_list);
                freeConfigEntry(&entry);
                slapi_ch_free((void **)&token);
                return -1;
            }
            if (config_get_extract_pem()) {
                /* Get Server{Key,Cert}ExtractFile from cn=Cipher,cn=encryption entry if any. */
                slapd_extract_cert(entry, PR_FALSE);
                slapd_extract_key(entry, isinternal ? internalTokenName : token, slot);
            }
            slapi_ch_free((void **)&token);
        }
        freeChildren(family_list);
        freeConfigEntry(&entry);
    }

    freeConfigEntry(&entry);

    /* Introduce a way of knowing whether slapd_ssl_init has
     * already been executed. */
    _security_library_initialized = 1;

    return 0;
}

/*
 * val:   sslVersionMin/Max value set in cn=encryption,cn=config (INPUT)
 * rval:  Corresponding value to set SSLVersionRange (OUTPUT)
 * ismin: True if val is sslVersionMin value
 */
#define SSLSTR "ssl"
#define SSLLEN (sizeof(SSLSTR) - 1)
#define TLSSTR "tls"
#define TLSLEN (sizeof(TLSSTR) - 1)
static int
set_NSS_version(char *val, PRUint16 *rval, int ismin)
{
    char *vp;
    char dmin[VERSION_STR_LENGTH], dmax[VERSION_STR_LENGTH];

    if (NULL == rval) {
        return 1;
    }
    (void)slapi_getSSLVersion_str(defaultNSSVersions.min, dmin, sizeof(dmin));
    (void)slapi_getSSLVersion_str(defaultNSSVersions.max, dmax, sizeof(dmax));

    if (!strncasecmp(val, SSLSTR, SSLLEN)) { /* ssl# NOT SUPPORTED */
        if (ismin) {
            slapd_SSL_warn("SSL3 is no longer supported.  Using NSS default min value: %s", dmin);
            (*rval) = defaultNSSVersions.min;
        } else {
            slapd_SSL_warn("SSL3 is no longer supported.  Using NSS default max value: %s", dmax);
            (*rval) = defaultNSSVersions.max;
        }
    } else if (!strncasecmp(val, TLSSTR, TLSLEN)) { /* tls# */
        float tlsv;
        vp = val + TLSLEN;
        sscanf(vp, "%4f", &tlsv);
        if (tlsv < 1.1f) { /* TLS1.0 */
            if (ismin) {
                if (supportedNSSVersions.min > SSL_LIBRARY_VERSION_TLS_1_0) {
                    slapd_SSL_warn("The value of sslVersionMin "
                                   "\"%s\" is lower than the supported version; "
                                   "the default value \"%s\" is used.",
                                   val, dmin);
                    (*rval) = defaultNSSVersions.min;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_0;
                }
            } else {
                if (supportedNSSVersions.max < SSL_LIBRARY_VERSION_TLS_1_0) {
                    /* never happens */
                    slapd_SSL_warn("The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.",
                                   val, dmax);
                    (*rval) = defaultNSSVersions.max;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_0;
                }
            }
        } else if (tlsv < 1.2f) { /* TLS1.1 */
            if (ismin) {
                if (supportedNSSVersions.min > SSL_LIBRARY_VERSION_TLS_1_1) {
                    slapd_SSL_warn("The value of sslVersionMin "
                                   "\"%s\" is lower than the supported version; "
                                   "the default value \"%s\" is used.",
                                   val, dmin);
                    (*rval) = defaultNSSVersions.min;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_1;
                }
            } else {
                if (supportedNSSVersions.max < SSL_LIBRARY_VERSION_TLS_1_1) {
                    /* never happens */
                    slapd_SSL_warn("The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.",
                                   val, dmax);
                    (*rval) = defaultNSSVersions.max;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_1;
                }
            }
        } else if (tlsv < 1.3f) { /* TLS1.2 */
            if (ismin) {
                if (supportedNSSVersions.min > SSL_LIBRARY_VERSION_TLS_1_2) {
                    slapd_SSL_warn("The value of sslVersionMin "
                                   "\"%s\" is lower than the supported version; "
                                   "the default value \"%s\" is used.",
                                   val, dmin);
                    (*rval) = defaultNSSVersions.min;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_2;
                }
            } else {
                if (supportedNSSVersions.max < SSL_LIBRARY_VERSION_TLS_1_2) {
                    /* never happens */
                    slapd_SSL_warn("The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.",
                                   val, dmax);
                    (*rval) = defaultNSSVersions.max;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_2;
                }
            }
        } else if (tlsv < 1.4f) { /* TLS1.3 */
            if (ismin) {
                if (supportedNSSVersions.min > SSL_LIBRARY_VERSION_TLS_1_3) {
                    slapd_SSL_warn("The value of sslVersionMin "
                                   "\"%s\" is lower than the supported version; "
                                   "the default value \"%s\" is used.",
                                   val, dmin);
                    (*rval) = defaultNSSVersions.min;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_3;
                }
            } else {
                if (supportedNSSVersions.max < SSL_LIBRARY_VERSION_TLS_1_3) {
                    /* never happens */
                    slapd_SSL_warn("The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.",
                                   val, dmax);
                    (*rval) = defaultNSSVersions.max;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_3;
                }
            }
        } else { /* Specified TLS is newer than supported */
            if (ismin) {
                slapd_SSL_warn("The value of sslVersionMin "
                               "\"%s\" is out of the range of the supported version; "
                               "the default value \"%s\" is used.",
                               val, dmin);
                (*rval) = defaultNSSVersions.min;
            } else {
                slapd_SSL_warn("The value of sslVersionMax "
                               "\"%s\" is out of the range of the supported version; "
                               "the default value \"%s\" is used.",
                               val, dmax);
                (*rval) = defaultNSSVersions.max;
            }
        }
    } else {
        if (ismin) {
            slapd_SSL_warn("The value of sslVersionMin "
                           "\"%s\" is invalid; the default value \"%s\" is used.",
                           val, dmin);
            (*rval) = defaultNSSVersions.min;
        } else {
            slapd_SSL_warn("The value of sslVersionMax "
                           "\"%s\" is invalid; the default value \"%s\" is used.",
                           val, dmax);
            (*rval) = defaultNSSVersions.max;
        }
    }
    return 0;
}
#undef SSLSTR
#undef SSLLEN
#undef TLSSTR
#undef TLSLEN

int
slapd_ssl_init2(PRFileDesc **fd, int startTLS)
{
    PRFileDesc *pr_sock, *sock = (*fd);
    PRErrorCode errorCode;
    SECStatus rv = SECFailure;
    char **family_list;
    CERTCertificate *cert = NULL;
    SECKEYPrivateKey *key = NULL;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
    const char *val = NULL;
    char *cipher_val = NULL;
    char *clientauth_val = NULL;
    char *default_val = NULL;
    int nFamilies = 0;
    SECStatus sslStatus;
    int slapd_SSLclientAuth;
    char *tmpDir;
    Slapi_Entry *e = NULL;
    PRBool fipsMode = PR_FALSE;
    PRUint16 NSSVersionMin = defaultNSSVersions.min;
    PRUint16 NSSVersionMax = defaultNSSVersions.max;
    char mymin[VERSION_STR_LENGTH], mymax[VERSION_STR_LENGTH];
    int allowweakcipher = CIPHER_SET_DEFAULTWEAKCIPHER;
    int_fast16_t renegotiation = (int_fast16_t)SSL_RENEGOTIATE_REQUIRES_XTN;

/* turn off the PKCS11 pin interactive mode */
/* wibrown 2016 */
/* We don't need to do the detection for the StdSystemPin, it does it */
/* automatically for us. */
#ifndef WITH_SYSTEMD
    SVRCOREStdPinObj *StdPinObj;

    if (svrcore_setup()) {
        return 1;
    }

    StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
    SVRCORE_SetStdPinInteractive(StdPinObj, PR_FALSE);
#endif

    /*
     * Cipher preferences must be set before any sslSocket is created
     * for such sockets to take preferences into account.
     */
    getConfigEntry(configDN, &e);
    if (e == NULL) {
        slapd_SSL_error("Failed get config entry %s", configDN);
        return 1;
    }
    val = slapi_entry_attr_get_ref(e, "allowWeakCipher");
    if (val) {
        if (!PL_strcasecmp(val, "off") || !PL_strcasecmp(val, "false") ||
            !PL_strcmp(val, "0") || !PL_strcasecmp(val, "no")) {
            allowweakcipher = CIPHER_SET_DISALLOWWEAKCIPHER;
        } else if (!PL_strcasecmp(val, "on") || !PL_strcasecmp(val, "true") ||
                   !PL_strcmp(val, "1") || !PL_strcasecmp(val, "yes")) {
            allowweakcipher = CIPHER_SET_ALLOWWEAKCIPHER;
        } else {
            slapd_SSL_warn("The value of allowWeakCipher \"%s\" in %s is invalid.",
                           "Ignoring it and set it to default.", val, configDN);
        }
    }

    /* Set SSL cipher preferences */
    if (NULL != (cipher_val = _conf_setciphers(ciphers, allowweakcipher))) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Failed to set SSL cipher "
                       "preference information: %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                       cipher_val, errorCode, slapd_pr_strerror(errorCode));
        slapi_ch_free_string(&cipher_val);
    }
    slapi_ch_free_string(&ciphers);
    freeConfigEntry(&e);

    /* Import pr fd into SSL */
    pr_sock = SSL_ImportFD(NULL, sock);
    if (pr_sock == (PRFileDesc *)NULL) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Failed to import NSPR "
                       "fd into SSL (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                       errorCode, slapd_pr_strerror(errorCode));
        return 1;
    }

    (*fd) = pr_sock;

    /* Step / Three.6 /
     *  - If in FIPS mode, authenticate to the token before
     *    doing anything else
     */
    {
        PK11SlotInfo *slot = slapd_pk11_getInternalSlot();
        if (!slot) {
            errorCode = PR_GetError();
            slapd_SSL_error("Unable to get internal slot (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                            errorCode, slapd_pr_strerror(errorCode));
            return -1;
        }

        if (slapd_pk11_isFIPS()) {
            if (slapd_pk11_authenticate(slot, PR_TRUE, NULL) != SECSuccess) {
                errorCode = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "Security Initialization",
                              "slapd_ssl_init2 - Unable to authenticate (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
                              errorCode, slapd_pr_strerror(errorCode));
                return -1;
            }
            fipsMode = PR_TRUE;
        }

        slapd_pk11_setSlotPWValues(slot, 0, 0);
    }

    /*
     * Now, get the complete list of cipher families. Each family
     * has a token name and personality name which we'll use to find
     * appropriate keys and certs, and call SSL_ConfigSecureServer
     * with.
     */

    if ((family_list = getChildren(configDN))) {
        char **family;
        char cert_name[1024];
        char *token;
        char *personality;
        char *activation;

        for (family = family_list; *family; family++) {
            token = NULL;
            personality = NULL;
            activation = NULL;

            getConfigEntry(*family, &e);
            if (e == NULL) {
                continue;
            }

            activation = slapi_entry_attr_get_charptr(e, "nssslactivation");
            if ((!activation) || (!PL_strcasecmp(activation, "off"))) {
                /* this family was turned off, goto next */
                slapi_ch_free_string(&activation);
                freeConfigEntry(&e);
                continue;
            }

            slapi_ch_free_string(&activation);

            token = slapi_entry_attr_get_charptr(e, "nsssltoken");
            personality = slapi_entry_attr_get_charptr(e, "nssslpersonalityssl");
            if (token && personality) {
                if (!PL_strcasecmp(token, "internal") ||
                    !PL_strcasecmp(token, "internal (software)"))
                    PL_strncpyz(cert_name, personality, sizeof(cert_name));
                else
                    /* external PKCS #11 token - attach token name */
                    PR_snprintf(cert_name, sizeof(cert_name), "%s:%s", token, personality);
            } else {
                errorCode = PR_GetError();
                slapd_SSL_warn("Failed to get cipher "
                               "family information. Missing nsssltoken or"
                               "nssslpersonalityssl in %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                               *family, errorCode, slapd_pr_strerror(errorCode));
                slapi_ch_free_string(&token);
                slapi_ch_free_string(&personality);
                freeConfigEntry(&e);
                continue;
            }

            slapi_ch_free((void **)&token);

            /* Step Four -- Locate the server certificate */
            cert = slapd_pk11_findCertFromNickname(cert_name, NULL);

            if (cert == NULL) {
                errorCode = PR_GetError();
                slapd_SSL_warn("Can't find "
                               "certificate (%s) for family %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                               cert_name, *family,
                               errorCode, slapd_pr_strerror(errorCode));
            }
            /* Step Five -- Get the private key from cert  */
            if (cert != NULL)
                key = slapd_pk11_findKeyByAnyCert(cert, NULL);

            if (key == NULL) {
                errorCode = PR_GetError();
                slapd_SSL_warn("Unable to retrieve "
                               "private key for cert %s of family %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                               cert_name, *family,
                               errorCode, slapd_pr_strerror(errorCode));
                slapi_ch_free_string(&personality);
                CERT_DestroyCertificate(cert);
                cert = NULL;
                freeConfigEntry(&e);
                continue;
            }

            /* Step Six  -- Configure Secure Server Mode  */
            if (pr_sock) {
                SECCertificateUsage returnedUsages;

                if (config_get_validate_cert_switch() == SLAPD_VALIDATE_CERT_OFF) {
                    /* If we're set to ignore certificate verification issues,
                     * just skip performing verification. */
                    rv = SECSuccess;
                } else {
                    /* Check if the certificate is valid. */
                    rv = CERT_VerifyCertificateNow(
                        CERT_GetDefaultCertDB(), cert, PR_TRUE,
                        certificateUsageSSLServer,
                        SSL_RevealPinArg(pr_sock),
                        &returnedUsages);

                    if (rv != SECSuccess) {
                        /* Log warning */
                        errorCode = PR_GetError();
                        slapd_SSL_warn("CERT_VerifyCertificateNow: "
                                       "verify certificate failed "
                                       "for cert %s of family %s (" SLAPI_COMPONENT_NAME_NSPR
                                       " error %d - %s)",
                                       cert_name, *family, errorCode,
                                       slapd_pr_strerror(errorCode));

                        /* If we're set to only warn, go ahead and
                         * override rv to allow us to start up. */
                        if (config_get_validate_cert_switch() == SLAPD_VALIDATE_CERT_WARN) {
                            rv = SECSuccess;
                        }
                    }
                }

                if (SECSuccess == rv) {
                    SSLKEAType certKEA;

                    /* If we want weak dh params, flag it on the socket now! */
                    rv = SSL_OptionSet(*fd, SSL_ENABLE_SERVER_DHE, PR_TRUE);
                    if (rv != SECSuccess) {
                        slapd_SSL_warn("Warning, unable to start DHE");
                    }
                    if (allowweakdhparam & CIPHER_SET_ALLOWWEAKDHPARAM) {
                        slapd_SSL_warn("notice, allowing weak parameters on socket.");
                        rv = SSL_EnableWeakDHEPrimeGroup(*fd, PR_TRUE);
                        if (rv != SECSuccess) {
                            slapd_SSL_warn("Warning, unable to allow weak DH params on socket.");
                        }
                    }

                    certKEA = NSS_FindCertKEAType(cert);
                    rv = SSL_ConfigSecureServer(*fd, cert, key, certKEA);
                    if (SECSuccess != rv) {
                        errorCode = PR_GetError();
                        slapd_SSL_warn("ConfigSecureServer: "
                                       "Server key/certificate is "
                                       "bad for cert %s of family %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                                       cert_name, *family, errorCode,
                                       slapd_pr_strerror(errorCode));
                    }
                }
            }
            if (cert) {
                CERT_DestroyCertificate(cert);
                cert = NULL;
            }
            if (key) {
                slapd_pk11_DestroyPrivateKey(key);
                key = NULL;
            }
            slapi_ch_free((void **)&personality);
            if (SECSuccess != rv) {
                freeConfigEntry(&e);
                continue;
            }
            nFamilies++;
            freeConfigEntry(&e);
        }
        freeChildren(family_list);
    }


    if (!nFamilies) {
        slapd_SSL_error("None of the cipher are valid");
        return -1;
    }

    /* Step Seven -- Configure Server Session ID Cache  */

    tmpDir = slapd_get_tmp_dir();

    slapi_log_err(SLAPI_LOG_TRACE, "Security Initialization",
                  "slapd_ssl_init2 - tmp dir = %s\n", tmpDir);

    rv = SSL_ConfigServerSessionIDCache(0, stimeout, stimeout, tmpDir);
    slapi_ch_free_string(&tmpDir);
    if (rv) {
        errorCode = PR_GetError();
        if (errorCode == ENOSPC) {
            slapd_SSL_error("Config of server nonce cache failed, "
                            "out of disk space! Make more room in /tmp "
                            "and try again. (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                            errorCode, slapd_pr_strerror(errorCode));
        } else {
            slapd_SSL_error("Config of server nonce cache failed (error %d - %s)",
                            errorCode, slapd_pr_strerror(errorCode));
        }
        return rv;
    }

    sslStatus = SSL_OptionSet(pr_sock, SSL_SECURITY, PR_TRUE);
    if (sslStatus != SECSuccess) {
        errorCode = PR_GetError();
        slapd_SSL_error("Failed to enable security "
                        "on the imported socket (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                        errorCode, slapd_pr_strerror(errorCode));
        return -1;
    }

    /* Retrieve the SSL Client Authentication status from cn=config */
    /* Set a default value if no value found */
    getConfigEntry(configDN, &e);
    if (e != NULL) {
        clientauth_val = (char *)slapi_entry_attr_get_ref(e, "nssslclientauth");
    }

    if (!clientauth_val) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Cannot get SSL Client "
                       "Authentication status. No nsslclientauth in %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                       configDN, errorCode, slapd_pr_strerror(errorCode));
        switch (SLAPD_DEFAULT_SSLCLIENTAUTH) {
        case SLAPD_SSLCLIENTAUTH_OFF:
            default_val = "off";
            break;
        case SLAPD_SSLCLIENTAUTH_ALLOWED:
            default_val = "allowed";
            break;
        case SLAPD_SSLCLIENTAUTH_REQUIRED:
            default_val = "required";
            break;
        default:
            default_val = "allowed";
            break;
        }
        clientauth_val = default_val;
    }
    if (config_set_SSLclientAuth("nssslclientauth", clientauth_val, errorbuf,
                                 CONFIG_APPLY) != LDAP_SUCCESS) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Cannot set SSL Client "
                       "Authentication status to \"%s\", error (%s). "
                       "Supported values are \"off\", \"allowed\" "
                       "and \"required\". (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                       val, errorbuf, errorCode, slapd_pr_strerror(errorCode));
    }

    if (e != NULL) {
        val = slapi_entry_attr_get_ref(e, "nsSSL3");
        if (val) {
            if (!PL_strcasecmp(val, "on")) {
                slapd_SSL_warn("NSS no longer support SSL3, the nsSSL3 setting will be ignored");
            }
        }
        val = slapi_entry_attr_get_ref(e, "nsTLS1");
        if (val) {
            if (!PL_strcasecmp(val, "off")) {
                slapd_SSL_warn("NSS only supports TLS, the nsTLS1 setting of \"off\" will be ignored");
            }
        }
        val = slapi_entry_attr_get_ref(e, "sslVersionMin");
        if (val) {
            /* Use the user defined minimum */
            (void)set_NSS_version((char *)val, &NSSVersionMin, 1);
        } else {
            /* Force our default minimum */
            (void)set_NSS_version(DEFVERSION, &NSSVersionMin, 1);
        }
        val = slapi_entry_attr_get_ref(e, "sslVersionMax");
        if (val) {
            (void)set_NSS_version((char *)val, &NSSVersionMax, 0);
        }
        if (NSSVersionMin > NSSVersionMax) {
            (void)slapi_getSSLVersion_str(NSSVersionMin, mymin, sizeof(mymin));
            (void)slapi_getSSLVersion_str(NSSVersionMax, mymax, sizeof(mymax));
            slapd_SSL_warn("The min value of NSS version range \"%s\" is greater than the max value \"%s\".  Adjusting the max to match the miniumum.",
                           mymin, mymax);
            NSSVersionMax = NSSVersionMin;
        }
    }

    /* Handle the SSL version range */
    slapdNSSVersions.min = NSSVersionMin;
    slapdNSSVersions.max = NSSVersionMax;
    (void)slapi_getSSLVersion_str(slapdNSSVersions.min, mymin, sizeof(mymin));
    (void)slapi_getSSLVersion_str(slapdNSSVersions.max, mymax, sizeof(mymax));
    slapi_log_err(SLAPI_LOG_INFO, "Security Initialization",
                  "slapd_ssl_init2 - Configured SSL version range: min: %s, max: %s\n",
                  mymin, mymax);
    sslStatus = SSL_VersionRangeSet(pr_sock, &slapdNSSVersions);
    if (sslStatus != SECSuccess) {
        errorCode = PR_GetError();
        slapd_SSL_error("Security Initialization - "
                "slapd_ssl_init2 - Failed to set SSL range: min: %s, max: %s - error %d (%s)",
                mymin, mymax, errorCode, slapd_pr_strerror(errorCode));
    }
    /*
     * Get the version range as NSS might have adjusted our requested range.  FIPS mode is
     * pretty picky about this stuff.
     */
    sslStatus = SSL_VersionRangeGet(pr_sock, &slapdNSSVersions);
    if (sslStatus == SECSuccess) {
        if (slapdNSSVersions.max > LDAP_OPT_X_TLS_PROTOCOL_TLS1_2 && fipsMode) {
            /*
             * FIPS & NSS currently only support a max version of TLS1.2
             * (although NSS advertises 1.3 as a max range in FIPS mode),
             * hopefully this code block can be removed soon...
             */
            slapdNSSVersions.max = LDAP_OPT_X_TLS_PROTOCOL_TLS1_2;
        }
        /* Reset request range */
        sslStatus = SSL_VersionRangeSet(pr_sock, &slapdNSSVersions);
        if (sslStatus == SECSuccess) {
            (void)slapi_getSSLVersion_str(slapdNSSVersions.min, mymin, sizeof(mymin));
            (void)slapi_getSSLVersion_str(slapdNSSVersions.max, mymax, sizeof(mymax));
            slapi_log_err(SLAPI_LOG_INFO, "Security Initialization",
                          "slapd_ssl_init2 - NSS adjusted SSL version range: min: %s, max: %s\n",
                          mymin, mymax);
        } else {
            errorCode = PR_GetError();
            (void)slapi_getSSLVersion_str(slapdNSSVersions.min, mymin, sizeof(mymin));
            (void)slapi_getSSLVersion_str(slapdNSSVersions.max, mymax, sizeof(mymax));
            slapd_SSL_error("Security Initialization - "
                    "slapd_ssl_init2 - Failed to set SSL range: min: %s, max: %s - error %d (%s)",
                    mymin, mymax, errorCode, slapd_pr_strerror(errorCode));
        }
    } else {
        errorCode = PR_GetError();
        slapd_SSL_error("Security Initialization - ",
                "slapd_ssl_init2 - Failed to get SSL range from socket - error %d (%s)",
                errorCode, slapd_pr_strerror(errorCode));
    }

    val = NULL;
    if (e != NULL) {
        val = slapi_entry_attr_get_ref(e, "nsTLSAllowClientRenegotiation");
    }
    if (val) {
        /* We default to allowing reneg.  If the option is "no",
         * disable reneg.  Else if the option isn't "yes", complain
         * and do the default (allow reneg). */
        if (PL_strcasecmp(val, "off") == 0) {
            renegotiation = SSL_RENEGOTIATE_NEVER;
        } else if (PL_strcasecmp(val, "on") == 0) {
            renegotiation = SSL_RENEGOTIATE_REQUIRES_XTN;
        } else {
            slapd_SSL_warn("The value of nsTLSAllowClientRenegotiation is invalid (should be 'on' or 'off'). Using default 'on'.");
            renegotiation = SSL_RENEGOTIATE_REQUIRES_XTN;
        }
    }

    sslStatus = SSL_OptionSet(pr_sock, SSL_ENABLE_RENEGOTIATION, (PRBool)renegotiation);
    if (sslStatus != SECSuccess) {
        errorCode = PR_GetError();
        slapd_SSL_error("Failed to set SSL renegotiation on the imported "
                        "socket (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                        errorCode, slapd_pr_strerror(errorCode));
        return -1;
    }

    freeConfigEntry(&e);

    if ((slapd_SSLclientAuth = config_get_SSLclientAuth()) != SLAPD_SSLCLIENTAUTH_OFF) {
        int err;
        switch (slapd_SSLclientAuth) {
        case SLAPD_SSLCLIENTAUTH_ALLOWED:
            /*
             * REQUEST is true
             * REQUIRED is false
             */
            if ((err = SSL_OptionSet(pr_sock, SSL_REQUEST_CERTIFICATE, PR_TRUE)) < 0) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "Security Initialization",
                              "SSL_OptionSet(SSL_REQUEST_CERTIFICATE,PR_TRUE) %d " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              err, prerr, slapd_pr_strerror(prerr));
            }
            if ((err = SSL_OptionSet(pr_sock, SSL_REQUIRE_CERTIFICATE, PR_FALSE)) < 0) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "Security Initialization",
                              "SSL_OptionSet(SSL_REQUIRE_CERTIFICATE,PR_FALSE) %d " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              err, prerr, slapd_pr_strerror(prerr));
            }
            break;
        case SLAPD_SSLCLIENTAUTH_REQUIRED:
            /* Give the client a clear opportunity to send her certificate: */
            /*
             * REQUEST is true
             * REQUIRED is true
             */
            if ((err = SSL_OptionSet(pr_sock, SSL_REQUEST_CERTIFICATE, PR_TRUE)) < 0) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "Security Initialization",
                              "SSL_OptionSet(SSL_REQUEST_CERTIFICATE,PR_TRUE) %d " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              err, prerr, slapd_pr_strerror(prerr));
            }
            if ((err = SSL_OptionSet(pr_sock, SSL_REQUIRE_CERTIFICATE, PR_TRUE)) < 0) {
                PRErrorCode prerr = PR_GetError();
                slapi_log_err(SLAPI_LOG_ERR, "Security Initialization",
                              "SSL_OptionSet(SSL_REQUIRE_CERTIFICATE,PR_TRUE) %d " SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                              err, prerr, slapd_pr_strerror(prerr));
            }
            break;
        default:
            break;
        }
    }

    /* Introduce a way of knowing whether slapd_ssl_init2 has
     * already been executed.
     * The cases in which slapd_ssl_init2 is executed during an
     * Start TLS operation are not taken into account, for it is
     * the fact of being executed by the server's SSL listener socket
     * that matters. */

    if (!startTLS)
        _ssl_listener_initialized = 1;

    return 0;
}

/* richm 20020227
   To do LDAP client SSL init, we need to do

    static void
    ldapssl_basic_init( void )
    {
        PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);

        PR_SetConcurrency( 4 );
    }
    NSS_Init(certdbpath);
    SSL_OptionSetDefault(SSL_ENABLE_SSL2, PR_FALSE);
    SSL_OptionSetDefault(SSL_ENABLE_SSL3, PR_TRUE);
    s = NSS_SetDomesticPolicy();
We already do pr_init, we don't need pr_setconcurrency, we already do nss_init and the rest

*/

int
slapd_SSL_client_auth(LDAP *ld)
{
    int rc = 0;
    PRErrorCode errorCode;
    char *pw = NULL;
    char **family_list;
    Slapi_Entry *entry = NULL;
    char cert_name[1024];
    char *token = NULL;
    SVRCOREStdPinObj *StdPinObj;
    SVRCOREError err = SVRCORE_Success;
    char *finalpersonality = NULL;
    char *CertExtractFile = NULL;
    char *KeyExtractFile = NULL;

    if ((family_list = getChildren(configDN))) {
        char **family;
        char *activation = NULL;
        char *cipher = NULL;
        char *personality = NULL;

        for (family = family_list; *family; family++) {
            getConfigEntry(*family, &entry);
            if (entry == NULL) {
                continue;
            }

            activation = slapi_entry_attr_get_charptr(entry, "nssslactivation");
            if ((!activation) || (!PL_strcasecmp(activation, "off"))) {
                /* this family was turned off, goto next */
                slapi_ch_free((void **)&activation);
                freeConfigEntry(&entry);
                continue;
            }
            slapi_ch_free((void **)&activation);

            personality = slapi_entry_attr_get_charptr(entry, "nssslpersonalityssl");
            cipher = slapi_entry_attr_get_charptr(entry, "cn");
            if (cipher && !PL_strcasecmp(cipher, "RSA")) {
                char *ssltoken;

                /* If there already is a token name, use it */
                if (token) {
                    slapi_ch_free_string(&personality);
                    slapi_ch_free_string(&cipher);
                    freeConfigEntry(&entry);
                    continue;
                }

                ssltoken = slapi_entry_attr_get_charptr(entry, "nsssltoken");
                if (ssltoken && personality) {
                    if (!PL_strcasecmp(ssltoken, "internal") ||
                        !PL_strcasecmp(ssltoken, "internal (software)"))
                    {
                        if ( slapd_pk11_isFIPS() ) {
                            /*
                             * FIPS mode changes the internal token name, so we need to
                             * grab the new token name from the internal slot.
                             */
                            PK11SlotInfo *slot = slapd_pk11_getInternalSlot();
                            token = slapi_ch_strdup(slapd_PK11_GetTokenName(slot));
                            PK11_FreeSlot(slot);
                        } else {
                            /*
                             * Translate config internal name to more readable form.
                             * Certificate name is just the personality for internal tokens.
                             */
                            token = slapi_ch_strdup(internalTokenName);
                        }
                        /* openldap needs tokenname:certnick */
                        PR_snprintf(cert_name, sizeof(cert_name), "%s:%s", token, personality);
                        slapi_ch_free_string(&ssltoken);
                    } else {
                        /* external PKCS #11 token - attach token name */
                        token = ssltoken; /*ssltoken was already dupped */
                        PR_snprintf(cert_name, sizeof(cert_name), "%s:%s", token, personality);
                    }
                } else {
                    errorCode = PR_GetError();
                    slapd_SSL_warn("Failed to get cipher "
                                   "family information.  Missing nsssltoken or"
                                   "nssslpersonalityssl in %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                                   *family, errorCode, slapd_pr_strerror(errorCode));
                    slapi_ch_free_string(&ssltoken);
                    slapi_ch_free_string(&personality);
                    slapi_ch_free_string(&cipher);
                    freeConfigEntry(&entry);
                    continue;
                }
            } else { /* external PKCS #11 cipher */
                char *ssltoken;

                ssltoken = slapi_entry_attr_get_charptr(entry, "nsssltoken");
                if (ssltoken && personality) {

                    /* free the old token and remember the new one */
                    if (token)
                        slapi_ch_free_string(&token);
                    token = ssltoken; /*ssltoken was already dupped */

                    /* external PKCS #11 token - attach token name */
                    PR_snprintf(cert_name, sizeof(cert_name), "%s:%s", token, personality);
                } else {
                    errorCode = PR_GetError();
                    slapd_SSL_warn("Failed to get cipher "
                                   "family information.  Missing nsssltoken or"
                                   "nssslpersonalityssl in %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                                   *family, errorCode, slapd_pr_strerror(errorCode));
                    slapi_ch_free_string(&ssltoken);
                    slapi_ch_free_string(&personality);
                    slapi_ch_free_string(&cipher);
                    freeConfigEntry(&entry);
                    continue;
                }
            }
            slapi_ch_free_string(&finalpersonality);
            finalpersonality = personality;
            slapi_ch_free_string(&cipher);
            /* Get ServerCert/KeyExtractFile from given entry if any. */
            slapi_ch_free_string(&CertExtractFile);
            CertExtractFile = slapi_entry_attr_get_charptr(entry, "ServerCertExtractFile");
            slapi_ch_free_string(&KeyExtractFile);
            KeyExtractFile = slapi_entry_attr_get_charptr(entry, "ServerKeyExtractFile");
            freeConfigEntry(&entry);
        } /* end of for */

        freeChildren(family_list);
    }

    /* Free config data */

    if (token && !svrcore_setup()) {
#ifdef WITH_SYSTEMD
        slapd_SSL_warn("Sending pin request to SVRCore. You may need to run "
                       "systemd-tty-ask-password-agent to provide the password.");
#endif
        StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
        err = SVRCORE_StdPinGetPin(&pw, StdPinObj, token);
        if (err != SVRCORE_Success || pw == NULL) {
            errorCode = PR_GetError();
            slapd_SSL_warn("SSL client authentication cannot be used "
                           "(no password). (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                           errorCode, slapd_pr_strerror(errorCode));
        } else {
            if (slapi_client_uses_non_nss(ld)  && config_get_extract_pem()) {
                char *certdir = config_get_certdir();
                char *keyfile = NULL;
                char *certfile = NULL;
                if (KeyExtractFile) {
                    if ('/' == *KeyExtractFile) {
                        keyfile = KeyExtractFile;
                    } else {
                        keyfile = slapi_ch_smprintf("%s/%s", certdir, KeyExtractFile);
                        slapi_ch_free_string(&KeyExtractFile);
                    }
                } else {
                    keyfile = slapi_ch_smprintf("%s/%s-Key%s", certdir, finalpersonality, PEMEXT);
                }
                if (CertExtractFile) {
                    if ('/' == *CertExtractFile) {
                        certfile = CertExtractFile;
                    } else {
                        certfile = slapi_ch_smprintf("%s/%s", certdir, CertExtractFile);
                        slapi_ch_free_string(&CertExtractFile);
                    }
                } else {
                    certfile = slapi_ch_smprintf("%s/%s%s", certdir, finalpersonality, PEMEXT);
                }
                slapi_ch_free_string(&certdir);
                if (PR_SUCCESS != PR_Access(keyfile, PR_ACCESS_EXISTS)) {
                    slapi_ch_free_string(&keyfile);
                    slapd_SSL_warn("SSL key file (%s) for client authentication does not exist. "
                                   "Using %s",
                                   keyfile, SERVER_KEY_NAME);
                    keyfile = slapi_ch_strdup(SERVER_KEY_NAME);
                }
                rc = ldap_set_option(ld, LDAP_OPT_X_TLS_KEYFILE, keyfile);
                if (rc) {
                    slapd_SSL_warn("SSL client authentication cannot be used "
                                   "unable to set the key to use to %s",
                                   keyfile);
                }
                slapi_ch_free_string(&keyfile);
                rc = PR_Access(certfile, PR_ACCESS_EXISTS);
                if (rc) {
                    slapi_ch_free_string(&certfile);
                    slapd_SSL_warn("SSL cert file (%s) for client authentication does not exist. "
                                   "Using %s",
                                   certfile, cert_name);
                    certfile = cert_name;
                }
                rc = ldap_set_option(ld, LDAP_OPT_X_TLS_CERTFILE, certfile);
                if (rc) {
                    slapd_SSL_warn("SSL client authentication cannot be used "
                                   "unable to set the cert to use to %s",
                                   certfile);
                }
                if (certfile != cert_name) {
                    slapi_ch_free_string(&certfile);
                }
            } else {
                rc = ldap_set_option(ld, LDAP_OPT_X_TLS_KEYFILE, SERVER_KEY_NAME);
                if (rc) {
                    slapd_SSL_warn("SSL client authentication cannot be used "
                                   "unable to set the key to use to %s",
                                   SERVER_KEY_NAME);
                }
                rc = ldap_set_option(ld, LDAP_OPT_X_TLS_CERTFILE, cert_name);
                if (rc) {
                    slapd_SSL_warn("SSL client authentication cannot be used "
                                   "unable to set the cert to use to %s",
                                   cert_name);
                }
            }
        }
    } else {
        if (token == NULL) {
            slapd_SSL_warn("slapd_SSL_client_auth - certificate token was not found");
        }
        rc = -1;
    }

    slapi_ch_free_string(&token);
    slapi_ch_free_string(&pw);
    slapi_ch_free_string(&finalpersonality);

    slapi_log_err(SLAPI_LOG_TRACE, "slapd_SSL_client_auth", "%i\n", rc);
    return rc;
}

/* Function for keeping track of the SSL initialization status:
 *      - returns 1: when slapd_ssl_init has been executed
 */
int
slapd_security_library_is_initialized()
{
    return _security_library_initialized;
}


/* Function for keeping track of the SSL listener socket initialization status:
 *      - returns 1: when slapd_ssl_init2 has been executed
 */
int
slapd_ssl_listener_is_initialized()
{
    return _ssl_listener_initialized;
}

int
slapd_nss_is_initialized()
{
    return _nss_initialized;
}

/* memory to store tmpdir is allocated and returned; caller should free it. */
char *
slapd_get_tmp_dir()
{
    static char tmp[MAXPATHLEN];
    char *tmpdir = NULL;
    ;

    tmp[0] = '\0';

    if ((tmpdir = config_get_tmpdir()) == NULL) {
        slapi_log_err(
            SLAPI_LOG_NOTICE,
            "slapd_get_tmp_dir",
            "config_get_tmpdir returns NULL Setting tmp dir to default\n");

        strcpy(tmp, "/tmp");
        return slapi_ch_strdup(tmp);
    }

    if (mkdir(tmpdir, 00770) == -1) {
        if (errno == EEXIST) {
            slapi_log_err(
                SLAPI_LOG_TRACE,
                "slapd_get_tmp_dir",
                "mkdir(%s, 00770) - already exists\n",
                tmpdir);
        } else {
            slapi_log_err(
                SLAPI_LOG_DEBUG,
                "slapd_get_tmp_dir",
                "mkdir(%s, 00770) Error: %s\n",
                tmpdir, strerror(errno));
        }
    }

    return (tmpdir);
}

SECKEYPrivateKey *
slapd_get_unlocked_key_for_cert(CERTCertificate *cert, void *pin_arg)
{
    SECKEYPrivateKey *key = NULL;
    PK11SlotListElement *sle;
    PK11SlotList *slotlist = PK11_GetAllSlotsForCert(cert, NULL);
    const char *certsubject = cert->subjectName ? cert->subjectName : "unknown cert";

    if (!slotlist) {
        PRErrorCode errcode = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "slapd_get_unlocked_key_for_cert",
                      "Cannot get slot list for certificate [%s] (%d: %s)\n",
                      certsubject, errcode, slapd_pr_strerror(errcode));
        return key;
    }

    for (sle = slotlist->head; sle; sle = sle->next) {
        PK11SlotInfo *slot = sle->slot;
        const char *slotname = (slot && PK11_GetSlotName(slot)) ? PK11_GetSlotName(slot) : "unknown slot";
        const char *tokenname = (slot && PK11_GetTokenName(slot)) ? PK11_GetTokenName(slot) : "unknown token";
        if (!slot) {
            slapi_log_err(SLAPI_LOG_TRACE, "slapd_get_unlocked_key_for_cert",
                          "Missing slot for slot list element for certificate [%s]\n",
                          certsubject);
        } else if (!PK11_NeedLogin(slot) || PK11_IsLoggedIn(slot, pin_arg)) {
            key = PK11_FindKeyByDERCert(slot, cert, pin_arg);
            slapi_log_err(SLAPI_LOG_TRACE, "slapd_get_unlocked_key_for_cert",
                          "Found unlocked slot [%s] token [%s] for certificate [%s]\n",
                          slotname, tokenname, certsubject);
            break;
        } else {
            PRErrorCode errcode = PR_GetError();
            slapi_log_err(SLAPI_LOG_NOTICE, "slapd_get_unlocked_key_for_cert",
                          "Skipping locked slot [%s] token [%s] for certificate [%s] (%d - %s)\n",
                          slotname, tokenname, certsubject, errcode, slapd_pr_strerror(errcode));
        }
    }

    if (!key) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_get_unlocked_key_for_cert",
                      "Could not find any unlocked slots for certificate [%s].  "
                      "Please review your TLS/SSL configuration.  The following slots were found:\n",
                      certsubject);
        for (sle = slotlist->head; sle; sle = sle->next) {
            PK11SlotInfo *slot = sle->slot;
            const char *slotname = (slot && PK11_GetSlotName(slot)) ? PK11_GetSlotName(slot) : "unknown slot";
            const char *tokenname = (slot && PK11_GetTokenName(slot)) ? PK11_GetTokenName(slot) : "unknown token";
            slapi_log_err(SLAPI_LOG_ERR, "slapd_get_unlocked_key_for_cert",
                          "Slot [%s] token [%s] was locked.\n",
                          slotname, tokenname);
        }
    }

    PK11_FreeSlotList(slotlist);
    return key;
}

/*
 * Functions to extract key and cert from the NSS cert db.
 */
#include <libgen.h>
#include <seccomon.h>
#include <secmodt.h>
#include <certt.h>
#include <base64.h>
#define DONOTEDIT "This file is auto-generated by 389-ds-base.\nDo not edit directly.\n"
#define NS_CERT_HEADER "-----BEGIN CERTIFICATE-----"
#define NS_CERT_TRAILER "-----END CERTIFICATE-----"
#define KEY_HEADER "-----BEGIN PRIVATE KEY-----"
#define KEY_TRAILER "-----END PRIVATE KEY-----"
#define ENCRYPTED_KEY_HEADER "-----BEGIN ENCRYPTED PRIVATE KEY-----"
#define ENCRYPTED_KEY_TRAILER "-----END ENCRYPTED PRIVATE KEY-----"

typedef struct
{
    enum
    {
        PW_NONE = 0,
        PW_FROMFILE = 1,
        PW_PLAINTEXT = 2,
        PW_EXTERNAL = 3
    } source;
    char *data;
} secuPWData;

static SECStatus
listCerts(CERTCertDBHandle *handle, CERTCertificate *cert, PK11SlotInfo *slot __attribute__((unused)), PRFileDesc *outfile, void *pwarg __attribute__((unused)))
{
    SECItem data;
    SECStatus rv = SECFailure;
    CERTCertList *certs;
    CERTCertListNode *node;
    CERTCertificate *the_cert = NULL;
    char *name = NULL;

    if (!cert) {
        slapi_log_err(SLAPI_LOG_ERR, "listCerts", "No cert given\n");
        return rv;
    }
    name = cert->nickname;

    if (!name) {
        slapi_log_err(SLAPI_LOG_ERR, "listCerts", "No cert nickname\n");
        return rv;
    }
    the_cert = CERT_FindCertByNicknameOrEmailAddr(handle, name);
    if (!the_cert) {
        slapi_log_err(SLAPI_LOG_ERR, "listCerts", "Could not find cert: %s\n", name);
        return SECFailure;
    }

    PR_fprintf(outfile, "%s\n", DONOTEDIT);
    /* Here, we have one cert with the desired nickname or email
     * address.  Now, we will attempt to get a list of ALL certs
     * with the same subject name as the cert we have.  That list
     * should contain, at a minimum, the one cert we have already found.
     * If the list of certs is empty (NULL), the libraries have failed.
     */
    certs = CERT_CreateSubjectCertList(NULL, handle, &the_cert->derSubject,
                                       PR_Now(), PR_FALSE);
    CERT_DestroyCertificate(the_cert);
    if (!certs) {
        slapi_log_err(SLAPI_LOG_ERR, "listCerts", "Problem printing certificates\n");
        return SECFailure;
    }
    for (node = CERT_LIST_HEAD(certs); !CERT_LIST_END(node, certs); node = CERT_LIST_NEXT(node)) {
        the_cert = node->cert;
        PR_fprintf(outfile, "Issuer: %s\n", the_cert->issuerName);
        PR_fprintf(outfile, "Subject: %s\n", the_cert->subjectName);
        /* now get the subjectList that matches this cert */
        data.data = the_cert->derCert.data;
        data.len = the_cert->derCert.len;
        char *data2ascii = BTOA_DataToAscii(data.data, data.len);
        PR_fprintf(outfile, "\n%s\n%s\n%s\n", NS_CERT_HEADER, data2ascii, NS_CERT_TRAILER);
        PORT_Free(data2ascii);
        rv = SECSuccess;
    }
    if (certs) {
        CERT_DestroyCertList(certs);
    }
    if (rv) {
        slapi_log_err(SLAPI_LOG_ERR, "listCerts", "Problem printing certificate nicknames\n");
        return SECFailure;
    }

    return rv;
}

static char *
gen_pem_path(char *filename)
{
    char *pem = NULL;
    char *pempath = NULL;
    char *dname = NULL;
    char *bname = NULL;
    char *certdir = NULL;

    if (!filename) {
        goto bail;
    }
    certdir = config_get_certdir();
    pem = PL_strstr(filename, PEMEXT);
    if (pem) {
        *pem = '\0';
    }
    bname = basename(filename);
    dname = dirname(filename);
    if (!PL_strcmp(dname, ".")) {
        /* just a file name */
        pempath = slapi_ch_smprintf("%s/%s%s", certdir, bname, PEMEXT);
    } else if (*dname == '/') {
        /* full path */
        pempath = slapi_ch_smprintf("%s/%s%s", dname, bname, PEMEXT);
    } else {
        /* relative path */
        pempath = slapi_ch_smprintf("%s/%s/%s%s", certdir, dname, bname, PEMEXT);
    }
bail:
    slapi_ch_free_string(&certdir);
    return pempath;
}

static int
slapd_extract_cert(Slapi_Entry *entry, int isCA)
{
    CERTCertDBHandle *certHandle;
    char *certdir = NULL;
    CERTCertListNode *node;
    CERTCertList *list = PK11_ListCerts(PK11CertListAll, NULL);
    PRFileDesc *outFile = NULL;
    SECStatus rv = SECFailure;
    char *CertExtractFile = NULL;
    char *certfile = NULL;
    char *personality = NULL;

    if (!entry) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_cert",
                      "No entry is given for %s Cert.\n", isCA ? "CA" : "Server");
        goto bail;
    }

    /* Get CertExtractFile from given entry if any. */
    if (isCA) {
        CertExtractFile = slapi_entry_attr_get_charptr(entry, "CACertExtractFile");
    } else {
        CertExtractFile = slapi_entry_attr_get_charptr(entry, "ServerCertExtractFile");
        personality = slapi_entry_attr_get_charptr(entry, "nsSSLPersonalitySSL");
    }
    certfile = gen_pem_path(CertExtractFile);
    if (isCA) {
        slapi_ch_free_string(&CACertPemFile);
        CACertPemFile = certfile;
    }

    certdir = config_get_certdir();
    certHandle = CERT_GetDefaultCertDB();
    for (node = CERT_LIST_HEAD(list); !CERT_LIST_END(node, list);
         node = CERT_LIST_NEXT(node)) {
        CERTCertificate *cert = node->cert;
        CERTCertTrust trust;
        switch (isCA) {
        case PR_TRUE:
            if ((CERT_GetCertTrust(cert, &trust) == SECSuccess) &&
                (trust.sslFlags & (CERTDB_VALID_CA | CERTDB_TRUSTED_CA | CERTDB_TRUSTED_CLIENT_CA))) {
                /* default token "internal" */
                PK11SlotInfo *slot = slapd_pk11_getInternalKeySlot();
                slapi_log_err(SLAPI_LOG_INFO, "slapd_extract_cert", "CA CERT NAME: %s\n", cert->nickname);
                if (!certfile) {
                    char buf[BUFSIZ];
                    certfile = slapi_ch_smprintf("%s/%s%s", certdir,
                                                 escape_string_for_filename(cert->nickname, buf), PEMEXT);
                    entrySetValue(slapi_entry_get_sdn(entry), "CACertExtractFile", certfile);
                    slapi_set_cacertfile(certfile);
                }
                if (!outFile) {
                    outFile = PR_Open(certfile, PR_CREATE_FILE | PR_RDWR | PR_TRUNCATE, 00660);
                }
                if (!outFile) {
                    slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_cert",
                                  "Unable to open \"%s\" for writing (%d, %d).\n",
                                  certfile, PR_GetError(), PR_GetOSError());
                    goto bail;
                }
                rv = listCerts(certHandle, cert, slot, outFile, NULL);
                if (rv) {
                    slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_cert", "listCerts failed\n");
                    break;
                }
            }
            break;
        default:
            if (!PL_strcmp(cert->nickname, personality)) {
                PK11SlotInfo *slot = slapd_pk11_getInternalKeySlot();
                slapi_log_err(SLAPI_LOG_INFO, "slapd_extract_cert", "SERVER CERT NAME: %s\n", cert->nickname);
                if (!certfile) {
                    char buf[BUFSIZ];
                    certfile = slapi_ch_smprintf("%s/%s%s", certdir,
                                                 escape_string_for_filename(cert->nickname, buf), PEMEXT);
                }
                if (!outFile) {
                    outFile = PR_Open(certfile, PR_CREATE_FILE | PR_RDWR | PR_TRUNCATE, 00660);
                }
                if (!outFile) {
                    slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_cert",
                                  "Unable to open \"%s\" for writing (%d, %d).\n",
                                  certfile, PR_GetError(), PR_GetOSError());
                    goto bail;
                }
                rv = listCerts(certHandle, cert, slot, outFile, NULL);
                if (rv) {
                    slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_cert", "listCerts failed\n");
                }
                PR_Close(outFile);
                outFile = NULL;
                break; /* One cert per one pem file. */
            }
            break;
        }
    }
    rv = SECSuccess;
bail:
    CERT_DestroyCertList(list);
    slapi_ch_free_string(&CertExtractFile);
    if (CACertPemFile != certfile) {
        slapi_ch_free_string(&certfile);
    }
    slapi_ch_free_string(&personality);
    slapi_ch_free_string(&certdir);
    if (outFile) {
        PR_Close(outFile);
    }
    return rv;
}

/*
 * Borrowed from keyutil.c (crypto-util)
 *
 * Extract the public and private keys and the subject
 * distinguished from the cert with the given nickname
 * in the given slot.
 *
 * @param nickname the certificate nickname
 * @param slot the slot where keys it was loaded
 * @param pwdat module authentication password
 * @param privkey private key out
 * @param pubkey public key out
 * @param subject subject out
 */
static SECStatus
extractRSAKeysAndSubject(
    const char *nickname,
    PK11SlotInfo *slot,
    secuPWData *pwdata,
    SECKEYPrivateKey **privkey,
    SECKEYPublicKey **pubkey,
    CERTName **subject)
{
    PRErrorCode rv = SECFailure;
    CERTCertificate *cert = PK11_FindCertFromNickname((char *)nickname, NULL);
    if (!cert) {
        rv = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "extractRSAKeysAndSubject",
                      "Failed extract cert with %s, (%d-%s, %d).\n",
                      nickname, rv, slapd_pr_strerror(rv), PR_GetOSError());
        goto bail;
    }

    *pubkey = CERT_ExtractPublicKey(cert);
    if (!*pubkey) {
        rv = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "extractRSAKeysAndSubject",
                      "Could not get public key from cert for %s, (%d-%s, %d)\n",
                      nickname, rv, slapd_pr_strerror(rv), PR_GetOSError());
        goto bail;
    }

    *privkey = PK11_FindKeyByDERCert(slot, cert, pwdata);
    if (!*privkey) {
        rv = PR_GetError();
        slapi_log_err(SLAPI_LOG_ERR, "extractRSAKeysAndSubject",
                      "Unable to find the key with PK11_FindKeyByDERCert for %s, (%d-%s, %d)\n",
                      nickname, rv, slapd_pr_strerror(rv), PR_GetOSError());
        *privkey = PK11_FindKeyByAnyCert(cert, &pwdata);
        if (!*privkey) {
            rv = PR_GetError();
            slapi_log_err(SLAPI_LOG_ERR, "extractRSAKeysAndSubject",
                          "Unable to find the key with PK11_FindKeyByAnyCert for %s, (%d-%s, %d)\n",
                          nickname, rv, slapd_pr_strerror(rv), PR_GetOSError());
            goto bail;
        }
    }

    PR_ASSERT(((*privkey)->keyType) == rsaKey);
    *subject = CERT_AsciiToName(cert->subjectName);

    if (!*subject) {
        slapi_log_err(SLAPI_LOG_ERR, "extractRSAKeysAndSubject",
                      "Improperly formatted name: \"%s\"\n",
                      cert->subjectName);
        goto bail;
    }
    rv = SECSuccess;
bail:
    if (cert)
        CERT_DestroyCertificate(cert);
    return rv;
}

/*
 * Decrypt the private key
 */
SECStatus
DecryptKey(
    SECKEYEncryptedPrivateKeyInfo *epki,
    SECOidTag algTag __attribute__((unused)),
    SECItem *pwitem,
    secuPWData *pwdata,
    SECItem *derPKI)
{
    SECItem *cryptoParam = NULL;
    PK11SymKey *symKey = NULL;
    PK11Context *ctx = NULL;
    SECStatus rv = SECFailure;

    if (!pwitem) {
        return rv;
    }

    do {
        SECAlgorithmID algid = epki->algorithm;
        CK_MECHANISM_TYPE cryptoMechType;
        CK_ATTRIBUTE_TYPE operation = CKA_DECRYPT;
        PK11SlotInfo *slot = NULL;

        cryptoMechType = PK11_GetPBECryptoMechanism(&algid, &cryptoParam, pwitem);
        if (cryptoMechType == CKM_INVALID_MECHANISM) {
            break;
        }

        slot = PK11_GetBestSlot(cryptoMechType, NULL);
        if (!slot) {
            break;
        }

        symKey = PK11_PBEKeyGen(slot, &algid, pwitem, PR_FALSE, pwdata);
        if (symKey == NULL) {
            break;
        }

        ctx = PK11_CreateContextBySymKey(cryptoMechType, operation, symKey, cryptoParam);
        if (ctx == NULL) {
            break;
        }

        rv = PK11_CipherOp(ctx,
                           derPKI->data,                  /* out     */
                           (int *)(&derPKI->len),         /* out len */
                           (int)epki->encryptedData.len,  /* max out */
                           epki->encryptedData.data,      /* in      */
                           (int)epki->encryptedData.len); /* in len  */

        PR_ASSERT(derPKI->len == epki->encryptedData.len);
        PR_ASSERT(rv == SECSuccess);
        rv = PK11_Finalize(ctx);
        PR_ASSERT(rv == SECSuccess);

    } while (0);

    /* cleanup */
    if (symKey) {
        PK11_FreeSymKey(symKey);
    }
    if (cryptoParam) {
        SECITEM_ZfreeItem(cryptoParam, PR_TRUE);
        cryptoParam = NULL;
    }
    if (ctx) {
        PK11_DestroyContext(ctx, PR_TRUE);
    }

    return rv;
}

/* #define ENCRYPTEDKEY 1 */
#define RAND_PASS_LEN 32
static int
slapd_extract_key(Slapi_Entry *entry, char *token __attribute__((unused)), PK11SlotInfo *slot)
{
    char *KeyExtractFile = NULL;
    char *personality = NULL;
    char *keyfile = NULL;
    unsigned char randomPassword[RAND_PASS_LEN] = {0};
    SECStatus rv = SECFailure;
    SECItem pwitem = {0, NULL, 0};
    SECItem clearKeyDER = {0, NULL, 0};
    PRFileDesc *outFile = NULL;
    SECKEYEncryptedPrivateKeyInfo *epki = NULL;
    SECKEYPrivateKey *privkey = NULL;
    SECKEYPublicKey *pubkey = NULL;
    secuPWData pwdata = {PW_NONE, 0};
    CERTName *subject = NULL;
    PLArenaPool *arenaForPKI = NULL;
    char *b64 = NULL;
    PRUint32 total = 0;
    PRUint32 numBytes = 0;
    char *certdir = NULL;
#if defined(ENCRYPTEDKEY)
    char *keyEncPwd = NULL;
    SVRCOREError err = SVRCORE_Success;
    PRArenaPool *arenaForEPKI = NULL;
    SVRCOREStdPinObj *StdPinObj;
    SECItem *encryptedKeyDER = NULL;
#endif

    if (!entry) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "No entry is given for Server Key.\n");
        goto bail;
    }
#if defined(ENCRYPTEDKEY)
    if (!token) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "No token is given.\n");
        goto bail;
    }
    StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
    if (!StdPinObj) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "No entry is given for Server Key.\n");
        goto bail;
    }
    err = SVRCORE_StdPinGetPin(&keyEncPwd, StdPinObj, token);
    if (err || !keyEncPwd) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "Failed to extract pw with token %s.\n", token);
        goto bail;
    }
    pwitem.data = (unsigned char *)keyEncPwd;
    pwitem.len = (unsigned int)strlen(keyEncPwd);
    pwitem.type = siBuffer;
#else
    /* Caller wants clear keys. Make up a dummy
     * password to get NSS to export an encrypted
     * key which we will decrypt.
     */
    rv = PK11_GenerateRandom(randomPassword, sizeof(randomPassword) - 1);
    if (rv != SECSuccess) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key", "Failed to generate random.\n");
        goto bail;
    }
    pwitem.data = randomPassword;
    pwitem.len = strlen((const char *)randomPassword);
    pwitem.type = siBuffer;
#endif

    /* Get ServerKeyExtractFile from given entry if any. */
    KeyExtractFile = slapi_entry_attr_get_charptr(entry, "ServerKeyExtractFile");
    personality = slapi_entry_attr_get_charptr(entry, "nsSSLPersonalitySSL");
    if (!personality) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "nsSSLPersonalitySSL value not found.\n");
        goto bail;
    }
    certdir = config_get_certdir();
    keyfile = gen_pem_path(KeyExtractFile);
    if (!keyfile) {
        char buf[BUFSIZ];
        keyfile = slapi_ch_smprintf("%s/%s-Key%s", certdir,
                                    escape_string_for_filename(personality, buf), PEMEXT);
    }
    outFile = PR_Open(keyfile, PR_CREATE_FILE | PR_RDWR | PR_TRUNCATE, 00660);
    if (!outFile) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "Unable to open \"%s\" for writing (%d, %d).\n",
                      keyfile, PR_GetError(), PR_GetOSError());
        goto bail;
    }
    rv = extractRSAKeysAndSubject(personality, slot, &pwdata, &privkey, &pubkey, &subject);
    if (rv != SECSuccess) {
#if defined(ENCRYPTEDKEY)
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "Failed to extract keys for \"%s\".\n", token);
#else
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key", "Failed to extract keys for %s.\n", personality);
#endif
        goto bail;
    }

    /*
     * Borrowed the code from KeyOut in keyutil.c (crypto-util).
     * Is it ok to hardcode the algorithm SEC_OID_DES_EDE3_CBC???
     */
    epki = PK11_ExportEncryptedPrivKeyInfo(NULL, SEC_OID_DES_EDE3_CBC, &pwitem, privkey, 1000, &pwdata);
    if (!epki) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "Unable to export encrypted private key (%d, %d).\n",
                      PR_GetError(), PR_GetOSError());
        goto bail;
    }
#if defined(ENCRYPTEDKEY)
    arenaForEPKI = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
    /* NULL dest to let it allocate memory for us */
    encryptedKeyDER = SEC_ASN1EncodeItem(arenaForEPKI, NULL, epki, SECKEY_EncryptedPrivateKeyInfoTemplate);
    if (!encryptedKeyDER) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "SEC_ASN1EncodeItem failed. (%d, %d).\n", PR_GetError(), PR_GetOSError());
        goto bail;
    }
#else
    /* Make a decrypted key the one to write out. */
    arenaForPKI = PORT_NewArena(2048);
    if (!arenaForPKI) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "PORT_NewArena failed. (%d, %d).\n", PR_GetError(), PR_GetOSError());
        goto bail;
    }
    clearKeyDER.data = PORT_ArenaAlloc(arenaForPKI, epki->encryptedData.len);
    clearKeyDER.len = epki->encryptedData.len;
    clearKeyDER.type = siBuffer;

    rv = DecryptKey(epki, SEC_OID_DES_EDE3_CBC, &pwitem, &pwdata, &clearKeyDER);
    if (rv != SECSuccess) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "DekryptKey failed. (%d, %d).\n", PR_GetError(), PR_GetOSError());
        goto bail;
    }
#endif

/* we could be exporting a clear or encrypted key */
#if defined(ENCRYPTEDKEY)
    b64 = BTOA_ConvertItemToAscii(encryptedKeyDER);
#else
    b64 = BTOA_ConvertItemToAscii(&clearKeyDER);
#endif
    if (!b64) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "Failed to conver to the ASCII (%d, %d).\n",
                      PR_GetError(), PR_GetOSError());
        goto bail;
    }

    total = PL_strlen(b64);
    PR_fprintf(outFile, "%s\n", DONOTEDIT);
#if defined(ENCRYPTEDKEY)
    PR_fprintf(outFile, "%s\n", ENCRYPTED_KEY_HEADER);
#else
    PR_fprintf(outFile, "%s\n", KEY_HEADER);
#endif
    numBytes = PR_Write(outFile, b64, total);
    if (numBytes != total) {
        slapi_log_err(SLAPI_LOG_ERR, "slapd_extract_key",
                      "Failed to write to the file (%d, %d).\n",
                      PR_GetError(), PR_GetOSError());
        goto bail;
    }
#if defined(ENCRYPTEDKEY)
    PR_fprintf(outFile, "\n%s\n", ENCRYPTED_KEY_TRAILER);
#else
    PR_fprintf(outFile, "\n%s\n", KEY_TRAILER);
#endif
    rv = SECSuccess;
bail:
    slapi_ch_free_string(&certdir);
    slapi_ch_free_string(&KeyExtractFile);
    slapi_ch_free_string(&keyfile);
    slapi_ch_free_string(&personality);
    if (outFile) {
        PR_Close(outFile);
    }
#if defined(ENCRYPTEDKEY)
    if (arenaForEPKI) {
        PORT_FreeArena(arenaForEPKI, PR_FALSE);
    }
    if (pwitem.data) {
        memset(pwitem.data, 0, pwitem.len);
        PORT_Free(pwitem.data);
    }
    memset(&pwitem, 0, sizeof(SECItem));
#else
    if (arenaForPKI) {
        PORT_FreeArena(arenaForPKI, PR_FALSE);
    }
    if (privkey) {
        slapd_pk11_DestroyPrivateKey(privkey);
    }
    if (pubkey) {
        slapd_pk11_DestroyPublicKey(pubkey);
    }
    if (subject) {
        CERT_DestroyName(subject);
    }
    if (epki) {
        SECKEY_DestroyEncryptedPrivateKeyInfo(epki, PR_TRUE);
    }
    if (b64) {
        PORT_Free(b64);
    }
    memset(randomPassword, 0, strlen((const char *)randomPassword));
#endif
    return rv;
}

const char *
slapi_get_cacertfile()
{
    return CACertPemFile;
}

void
slapi_set_cacertfile(char *certfile)
{
    slapi_ch_free_string(&CACertPemFile);
    CACertPemFile = certfile;
}
