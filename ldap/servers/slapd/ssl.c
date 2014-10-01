/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* SSL-related stuff for slapd */

#if defined( _WINDOWS )
#include <windows.h>
#include <winsock.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "proto-ntutil.h"
#include <string.h>
#include <stdlib.h>
#include <direct.h>
#include <io.h>
#endif

#include <stdio.h>
#include <sys/param.h>
#include <ssl.h>
#include <nss.h>
#include <key.h>
#include <sslproto.h>
#include "secmod.h"
#include <string.h>
#include <errno.h>

#define NEED_TOK_DES /* defines tokDes and ptokDes - see slap.h */
#include "slap.h"

#include "svrcore.h"
#include "fe.h"
#include "certdb.h"

#if !defined(USE_OPENLDAP)
#include "ldap_ssl.h"
#endif

/* For IRIX... */
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

#if NSS_VMAJOR * 100 + NSS_VMINOR >= 315
#define NSS_TLS12 1
#elif NSS_VMAJOR * 100 + NSS_VMINOR >= 314
#define NSS_TLS11 1
#else
#define NSS_TLS10 1
#endif

extern char* slapd_SSL3ciphers;
extern symbol_t supported_ciphers[];
#if !defined(NSS_TLS10) /* NSS_TLS11 or newer */
static SSLVersionRange    enabledNSSVersions;
#endif

/* dongle_file_name is set in slapd_nss_init when we set the path for the
   key, cert, and secmod files - the dongle file must be in the same directory
   and use the same naming scheme
*/
static char*	dongle_file_name = NULL;

static int _security_library_initialized = 0;
static int _ssl_listener_initialized = 0;
static int _nss_initialized = 0;

/* Our name for the internal token, must match PKCS-11 config data below */
static char *internalTokenName = "Internal (Software) Token";

static int stimeout;
static char *ciphers = NULL;
static char * configDN = "cn=encryption,cn=config";

/* Copied from libadmin/libadmin.h public/nsapi.h */
#define SERVER_KEY_NAME "Server-Key"
#define MAGNUS_ERROR_LEN 1024
#define LOG_WARN 0
#define LOG_FAILURE 3
#define FILE_PATHSEP '/'

/* ----------------------- Multiple cipher support ------------------------ */
/* cipher set flags */
#define CIPHER_SET_NONE               0x0
#define CIPHER_SET_ALL                0x1
#define CIPHER_SET_DEFAULT            0x2
#define CIPHER_SET_DEFAULTWEAKCIPHER  0x10 /* allowWeakCipher is not set in cn=encryption */
#define CIPHER_SET_ALLOWWEAKCIPHER    0x20 /* allowWeakCipher is on */
#define CIPHER_SET_DISALLOWWEAKCIPHER 0x40 /* allowWeakCipher is off */

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
  ((CIPHER_SET_ISDEFAULT(flag)|CIPHER_SET_ISALL(flag)) ? \
   (ALLOWWEAK_ISON(flag) ? PR_TRUE : PR_FALSE) : \
   (!ALLOWWEAK_ISOFF(flag) ? PR_TRUE : PR_FALSE))

#define CIPHER_SET_DISABLE_ALLOWSWEAKCIPHER(flag) \
  ((flag)&~CIPHER_SET_ALLOWWEAKCIPHER)

/* flags */
#define CIPHER_IS_DEFAULT       0x1
#define CIPHER_MUST_BE_DISABLED 0x2
#define CIPHER_IS_WEAK          0x4
#define CIPHER_IS_DEPRECATED    0x8
static char **cipher_names = NULL;
static char **enabled_cipher_names = NULL;
typedef struct {
    char *name;
    int num;
    int flags; 
} cipherstruct;

static cipherstruct *_conf_ciphers = NULL;
static void _conf_init_ciphers();
/* 
 * This lookup table is for supporting the old cipher name.
 * Once swtiching to the NSS cipherSuiteName is done,
 * this lookup_cipher table can be removed.
 */
typedef struct {
    char *alias;
    char *name;
} lookup_cipher;
static lookup_cipher _lookup_cipher[] = {
    {"rc4",                                 "SSL_CK_RC4_128_WITH_MD5"},
    {"rc4export",                           "SSL_CK_RC4_128_EXPORT40_WITH_MD5"},
    {"rc2",                                 "SSL_CK_RC2_128_CBC_WITH_MD5"},
    {"rc2export",                           "SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5"},
    /*{"idea",                              "SSL_EN_IDEA_128_CBC_WITH_MD5"}, */
    {"des",                                 "SSL_CK_DES_64_CBC_WITH_MD5"},
    {"desede3",                             "SSL_CK_DES_192_EDE3_CBC_WITH_MD5"},
    {"rsa_rc4_128_md5",                     "TLS_RSA_WITH_RC4_128_MD5"},
    {"rsa_rc4_128_sha",                     "TLS_RSA_WITH_RC4_128_SHA"},
    {"rsa_3des_sha",                        "TLS_RSA_WITH_3DES_EDE_CBC_SHA"},
    {"tls_rsa_3des_sha",                    "TLS_RSA_WITH_3DES_EDE_CBC_SHA"},
    {"rsa_fips_3des_sha",                   "SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA"},
    {"fips_3des_sha",                       "SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA"},
    {"rsa_des_sha",                         "TLS_RSA_WITH_DES_CBC_SHA"},
    {"rsa_fips_des_sha",                    "SSL_RSA_FIPS_WITH_DES_CBC_SHA"},
    {"fips_des_sha",                        "SSL_RSA_FIPS_WITH_DES_CBC_SHA"}, /* ditto */
    {"rsa_rc4_40_md5",                      "TLS_RSA_EXPORT_WITH_RC4_40_MD5"},
    {"tls_rsa_rc4_40_md5",                  "TLS_RSA_EXPORT_WITH_RC4_40_MD5"},
    {"rsa_rc2_40_md5",                      "TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5"},
    {"tls_rsa_rc2_40_md5",                  "TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5"},
    {"rsa_null_md5",                        "TLS_RSA_WITH_NULL_MD5"}, /* disabled by default */
    {"rsa_null_sha",                        "TLS_RSA_WITH_NULL_SHA"}, /* disabled by default */
    {"tls_rsa_export1024_with_rc4_56_sha",  "TLS_RSA_EXPORT1024_WITH_RC4_56_SHA"},
    {"rsa_rc4_56_sha",                      "TLS_RSA_EXPORT1024_WITH_RC4_56_SHA"}, /* ditto */
    {"tls_rsa_export1024_with_des_cbc_sha", "TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA"},
    {"rsa_des_56_sha",                      "TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA"}, /* ditto */
    {"fortezza",                            ""}, /* deprecated */
    {"fortezza_rc4_128_sha",                ""}, /* deprecated */
    {"fortezza_null",                       ""}, /* deprecated */

    /*{"dhe_dss_40_sha", SSL_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA, 0}, */
    {"dhe_dss_des_sha",                     "TLS_DHE_DSS_WITH_DES_CBC_SHA"},
    {"dhe_dss_3des_sha",                    "TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA"},
    {"dhe_rsa_40_sha",                      "TLS_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA"},
    {"dhe_rsa_des_sha",                     "TLS_DHE_RSA_WITH_DES_CBC_SHA"},
    {"dhe_rsa_3des_sha",                    "TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA"},

    {"tls_rsa_aes_128_sha",                 "TLS_RSA_WITH_AES_128_CBC_SHA"},
    {"rsa_aes_128_sha",                     "TLS_RSA_WITH_AES_128_CBC_SHA"}, /* ditto */
    {"tls_dh_dss_aes_128_sha",              ""}, /* deprecated */
    {"tls_dh_rsa_aes_128_sha",              ""}, /* deprecated */
    {"tls_dhe_dss_aes_128_sha",             "TLS_DHE_DSS_WITH_AES_128_CBC_SHA"},
    {"tls_dhe_rsa_aes_128_sha",             "TLS_DHE_RSA_WITH_AES_128_CBC_SHA"},

    {"tls_rsa_aes_256_sha",                 "TLS_RSA_WITH_AES_256_CBC_SHA"},
    {"rsa_aes_256_sha",                     "TLS_RSA_WITH_AES_256_CBC_SHA"}, /* ditto */
    {"tls_dss_aes_256_sha",                 ""}, /* deprecated */
    {"tls_rsa_aes_256_sha",                 ""}, /* deprecated */
    {"tls_dhe_dss_aes_256_sha",             "TLS_DHE_DSS_WITH_AES_256_CBC_SHA"},
    {"tls_dhe_rsa_aes_256_sha",             "TLS_DHE_RSA_WITH_AES_256_CBC_SHA"},
    /*{"tls_dhe_dss_1024_des_sha",          ""}, */
    {"tls_dhe_dss_1024_rc4_sha",            "TLS_RSA_EXPORT1024_WITH_RC4_56_SHA"},
    {"tls_dhe_dss_rc4_128_sha",             "TLS_DHE_DSS_WITH_RC4_128_SHA"},
#if defined(NSS_TLS12)
    /* New in NSS 3.15 */
    {"tls_rsa_aes_128_gcm_sha",             "TLS_RSA_WITH_AES_128_GCM_SHA256"},
    {"tls_dhe_rsa_aes_128_gcm_sha",         "TLS_DHE_RSA_WITH_AES_128_GCM_SHA256"},
    {"tls_dhe_dss_aes_128_gcm_sha",         NULL}, /* not available */
#endif
    {NULL, NULL}
};

static void
slapd_SSL_report(int degree, char *fmt, va_list args)
{
    char buf[2048];
    PR_vsnprintf( buf, sizeof(buf), fmt, args );
    LDAPDebug( LDAP_DEBUG_ANY, "SSL %s: %s\n",
	       (degree == LOG_FAILURE) ? "failure" : "alert",
	       buf, 0 );
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

char **
getSupportedCiphers()
{
	SSLCipherSuiteInfo info;
	char *sep = "::";
	int number_of_ciphers = SSL_NumImplementedCiphers;
	int i;
	int idx = 0;
	PRBool isFIPS = slapd_pk11_isFIPS();

	_conf_init_ciphers();

	if ((cipher_names == NULL) && (_conf_ciphers)) {
		cipher_names = (char **)slapi_ch_calloc((number_of_ciphers + 1), sizeof(char *));
		for (i = 0 ; _conf_ciphers[i].name != NULL; i++ ) {
			SSL_GetCipherSuiteInfo((PRUint16)_conf_ciphers[i].num,&info,sizeof(info));
			/* only support FIPS approved ciphers in FIPS mode */
			if (!isFIPS || info.isFIPS) {
				cipher_names[idx++] = PR_smprintf("%s%s%s%s%s%s%d",
						_conf_ciphers[i].name,sep,
						info.symCipherName,sep,
						info.macAlgorithmName,sep,
						info.symKeyBits);
			}
		}
		cipher_names[idx] = NULL;
	}
	return cipher_names;
}

char **
getEnabledCiphers()
{
    SSLCipherSuiteInfo info;
    char *sep = "::";
    int number_of_ciphers = 0;
    int x;
    int idx = 0;
    PRBool enabled;

    /* We have to wait until the SSL initialization is done. */
    if (!slapd_ssl_listener_is_initialized()) {
        return NULL;
    }
    if ((enabled_cipher_names == NULL) && _conf_ciphers) {
        for (x = 0; _conf_ciphers[x].name; x++) {
            SSL_CipherPrefGetDefault(_conf_ciphers[x].num, &enabled);
            if (enabled) {
                number_of_ciphers++;
            }
        }
        enabled_cipher_names = (char **)slapi_ch_calloc((number_of_ciphers + 1), sizeof(char *));
        for (x = 0; _conf_ciphers[x].name; x++) {
            SSL_CipherPrefGetDefault(_conf_ciphers[x].num, &enabled);
            if (enabled) {
                SSL_GetCipherSuiteInfo((PRUint16)_conf_ciphers[x].num,&info,sizeof(info));
                enabled_cipher_names[idx++] = PR_smprintf("%s%s%s%s%s%s%d",
                        _conf_ciphers[x].name,sep,
                        info.symCipherName,sep,
                        info.macAlgorithmName,sep,
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
                slapd_SSL_warn("Security Initialization: no information for cipher suite [%s] "
                               "error %d - %s", _conf_ciphers[idx].name,
                               errorCode, slapd_pr_strerror(errorCode));
            }
            rc = PR_FALSE;
        }
        if (rc && !info.isFIPS) {
            if (slapi_is_loglevel_set(SLAPI_LOG_CONFIG)) {
                slapd_SSL_warn("Security Initialization: FIPS mode is enabled but "
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

static void
_conf_init_ciphers()
{
    int x;
    SECStatus rc;
    SSLCipherSuiteInfo info;
    const PRUint16 *implementedCiphers = SSL_GetImplementedCiphers();

    /* Initialize _conf_ciphers */
    if (_conf_ciphers) {
        return;
    }
    _conf_ciphers = (cipherstruct *)slapi_ch_calloc(SSL_NumImplementedCiphers + 1, sizeof(cipherstruct));

    for (x = 0; implementedCiphers && (x < SSL_NumImplementedCiphers); x++) {
        rc = SSL_GetCipherSuiteInfo(implementedCiphers[x], &info, sizeof info);
        if (SECFailure == rc) {
            slapi_log_error(SLAPI_LOG_FATAL, "SSL Initialization",
                            "Warning: failed to get the cipher suite info of cipher ID %d\n",
                            implementedCiphers[x]);
            continue;
        }
        if (!_conf_ciphers[x].num) { /* initialize each cipher */
            _conf_ciphers[x].name = slapi_ch_strdup(info.cipherSuiteName);
            _conf_ciphers[x].num = implementedCiphers[x];
            if (info.symCipher == ssl_calg_null) {
                _conf_ciphers[x].flags |= CIPHER_MUST_BE_DISABLED;
            } else {
                _conf_ciphers[x].flags |= info.isExportable?CIPHER_IS_WEAK:
                                          (info.symCipher < ssl_calg_3des)?CIPHER_IS_WEAK:
                                          (info.effectiveKeyBits < 128)?CIPHER_IS_WEAK:0;
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
    int x;
    SECStatus rc;
    PRBool setdefault = CIPHER_SET_ISDEFAULT(flag);
    PRBool enabled = CIPHER_SET_ISALL(flag);
    PRBool allowweakcipher = CIPHER_SET_ALLOWSWEAKCIPHER(flag);
    PRBool setme = PR_FALSE;
    const PRUint16 *implementedCiphers = SSL_GetImplementedCiphers();

    _conf_init_ciphers();

    for (x = 0; implementedCiphers && (x < SSL_NumImplementedCiphers); x++) {
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
                slapi_log_error(SLAPI_LOG_FATAL, "SSL Initialization",
                    "Warning: failed to get the default state of cipher %s\n",
                    _conf_ciphers[x].name);
                continue;
            }
            if (!allowweakcipher && (_conf_ciphers[x].flags & CIPHER_IS_WEAK)) {
                setme = PR_FALSE;
            }
            _conf_ciphers[x].flags |= setme?CIPHER_IS_DEFAULT:0;
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
            str = PR_smprintf("%s", *ary++);
        }
    }

    return str;
}

void
_conf_dumpciphers()
{
    int x;
    PRBool enabled;
    /* {"SSL3","rc4", SSL_EN_RC4_128_WITH_MD5}, */
    slapd_SSL_warn("Configured NSS Ciphers");
    for (x = 0; _conf_ciphers[x].name; x++) {
        SSL_CipherPrefGetDefault(_conf_ciphers[x].num, &enabled);
        if (enabled) {
            slapd_SSL_warn("\t%s: enabled%s%s%s", _conf_ciphers[x].name,
                           (_conf_ciphers[x].flags&CIPHER_IS_WEAK)?", (WEAK CIPHER)":"",
                           (_conf_ciphers[x].flags&CIPHER_IS_DEPRECATED)?", (DEPRECATED)":"",
                           (_conf_ciphers[x].flags&CIPHER_MUST_BE_DISABLED)?", (MUST BE DISABLED)":"");
        } else if (slapi_is_loglevel_set(SLAPI_LOG_CONFIG)) {
            slapd_SSL_warn("\t%s: disabled%s%s%s", _conf_ciphers[x].name,
                           (_conf_ciphers[x].flags&CIPHER_IS_WEAK)?", (WEAK CIPHER)":"",
                           (_conf_ciphers[x].flags&CIPHER_IS_DEPRECATED)?", (DEPRECATED)":"",
                           (_conf_ciphers[x].flags&CIPHER_MUST_BE_DISABLED)?", (MUST BE DISABLED)":"");
        }
    }
}

char *
_conf_setciphers(char *ciphers, int flags)
{
    char *t, err[MAGNUS_ERROR_LEN];
    int x, i, active;
    char *raw = ciphers;
    char **suplist = NULL;
    char **unsuplist = NULL;
    PRBool enabledOne = PR_FALSE;

    /* #47838: harden the list of ciphers available by default */
    /* Default is to activate all of them ==> none of them*/
    if (!ciphers || (ciphers[0] == '\0') || !PL_strcasecmp(ciphers, "default")) {
        _conf_setallciphers((CIPHER_SET_DEFAULT|flags), NULL, NULL);
        slapd_SSL_warn("Security Initialization: Enabling default cipher set.");
        _conf_dumpciphers();
        return NULL;
    }

    if (PL_strcasestr(ciphers, "+all")) {
        /*
         * Enable all the ciphers if "+all" and the following while loop would
         * disable the user disabled ones.  This is needed because we added a new
         * set of ciphers in the table. Right now there is no support for this
         * from the console
         */
        _conf_setallciphers((CIPHER_SET_ALL|flags), &suplist, NULL);
        enabledOne = PR_TRUE;
    } else {
        /* If "+all" is not in nsSSL3Ciphers value, disable all first,
         * then enable specified ciphers. */
        _conf_setallciphers(CIPHER_SET_NONE /* disabled */, NULL, NULL);
    }

    t = ciphers;
    while(t) {
        while((*ciphers) && (isspace(*ciphers))) ++ciphers;

        switch(*ciphers++) {
          case '+':
            active = 1; break;
          case '-':
            active = 0; break;
          default:
            PR_snprintf(err, sizeof(err), "invalid ciphers <%s>: format is "
                    "+cipher1,-cipher2...", raw);
            return slapi_ch_strdup(err);
        }
        if( (t = strchr(ciphers, ',')) )
            *t++ = '\0';

        if (strcasecmp(ciphers, "all")) { /* if not all */
            PRBool enabled = active ? PR_TRUE : PR_FALSE;
            int lookup = 1;
            for (x = 0; _conf_ciphers[x].name; x++) {
                if (!PL_strcasecmp(ciphers, _conf_ciphers[x].name)) {
                    if (_conf_ciphers[x].flags & CIPHER_IS_WEAK) {
                        if (active && CIPHER_SET_ALLOWSWEAKCIPHER(flags)) { 
                            slapd_SSL_warn("Cipher %s is weak.  It is enabled since allowWeakCipher is \"on\" "
                                           "(default setting for the backward compatibility). "
                                           "We strongly recommend to set it to \"off\".  "
                                           "Please replace the value of allowWeakCipher with \"off\" in "
                                           "the encryption config entry cn=encryption,cn=config and "
                                           "restart the server.", ciphers);
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
                    lookup = 0;
                    break;
                }
            }
            if (lookup) { /* lookup with old cipher name and get NSS cipherSuiteName */
                for (i = 0; _lookup_cipher[i].alias; i++) {
                    if (!PL_strcasecmp(ciphers, _lookup_cipher[i].alias)) {
                        if (!_lookup_cipher[i].name[0]) {
                            slapd_SSL_warn("Cipher suite %s is not available in NSS %d.%d.  Ignoring %s",
                                           ciphers, NSS_VMAJOR, NSS_VMINOR, ciphers);
                            continue;
                        }
                        for (x = 0; _conf_ciphers[x].name; x++) {
                            if (!PL_strcasecmp(_lookup_cipher[i].name, _conf_ciphers[x].name)) {
                                if (enabled) {
                                    if (_conf_ciphers[x].flags & CIPHER_IS_WEAK) {
                                        if (active && CIPHER_SET_ALLOWSWEAKCIPHER(flags)) {
                                            slapd_SSL_warn("Cipher %s is weak. "
                                                           "It is enabled since allowWeakCipher is \"on\" "
                                                           "(default setting for the backward compatibility). "
                                                           "We strongly recommend to set it to \"off\".  "
                                                           "Please replace the value of allowWeakCipher with \"off\" in "
                                                           "the encryption config entry cn=encryption,cn=config and "
                                                           "restart the server.", ciphers);
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
                                }
                                if (enabled) {
                                    enabledOne = PR_TRUE; /* At least one active cipher is set. */
                                }
                                SSL_CipherPrefSetDefault(_conf_ciphers[x].num, enabled);
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            if (!lookup && !_conf_ciphers[x].name) { /* If lookup, it's already reported. */
                slapd_SSL_warn("Cipher suite %s is not available in NSS %d.%d.  Ignoring %s",
                               ciphers, NSS_VMAJOR, NSS_VMINOR, ciphers);
            }
        }
        if(t) {
            ciphers = t;
        }
    }
    if (unsuplist && *unsuplist) {
        char *strsup = charray2str(suplist, ",");
        char *strunsup = charray2str(unsuplist, ",");
        slapd_SSL_warn("Security Initialization: FIPS mode is enabled - only the following "
                       "cipher suites are approved for FIPS: [%s] - "
                       "the specified cipher suites [%s] are disabled - if "
                       "you want to use these unsupported cipher suites, you must use modutil to "
                       "disable FIPS in the internal token.",
                       strsup ? strsup : "(none)", strunsup ? strunsup : "(none)");
        slapi_ch_free_string(&strsup);
        slapi_ch_free_string(&strunsup);
    }

    slapi_ch_free((void **)&suplist); /* strings inside are static */
    slapi_ch_free((void **)&unsuplist); /* strings inside are static */

    if (!enabledOne) {
        char *nocipher = PR_smprintf("No active cipher suite is available.");
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

  return s?PR_FAILURE:PR_SUCCESS;

}

/**
 * Get a particular entry
 */
static Slapi_Entry *
getConfigEntry( const char *dn, Slapi_Entry **e2 ) {
	Slapi_DN	sdn;

	slapi_sdn_init_dn_byref( &sdn, dn );
	slapi_search_internal_get_entry( &sdn, NULL, e2,
			plugin_get_default_component_id());
	slapi_sdn_done( &sdn );
	return *e2;
}

/**
 * Free an entry
 */
static void
freeConfigEntry( Slapi_Entry ** e ) {
	if ( (e != NULL) && (*e != NULL) ) {
		slapi_entry_free( *e );
		*e = NULL;
	}
}

/**
 * Get a list of child DNs
 */
static char **
getChildren( char *dn ) {
	Slapi_PBlock    *new_pb = NULL;
	Slapi_Entry     **e;
	int             search_result = 1;
	int             nEntries = 0;
	char            **list = NULL;

	new_pb = slapi_search_internal ( dn, LDAP_SCOPE_ONELEVEL,
									 "(objectclass=nsEncryptionModule)",
									 NULL, NULL, 0);

	slapi_pblock_get( new_pb, SLAPI_NENTRIES, &nEntries);
	if ( nEntries > 0 ) {
		slapi_pblock_get( new_pb, SLAPI_PLUGIN_INTOP_RESULT, &search_result);
		slapi_pblock_get( new_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &e);
		if ( e != NULL ) {
			int i;
			list = (char **)slapi_ch_malloc( sizeof(*list) * (nEntries + 1));
			for ( i = 0; e[i] != NULL; i++ ) {
				list[i] = slapi_ch_strdup(slapi_entry_get_dn(e[i]));
			}
			list[nEntries] = NULL;
		}
	}
	slapi_free_search_results_internal(new_pb);
	slapi_pblock_destroy(new_pb );
	return list;
}

/**
 * Free a list of child DNs
 */
static void
freeChildren( char **list ) {
	if ( list != NULL ) {
		int i;
		for ( i = 0; list[i] != NULL; i++ ) {
			slapi_ch_free( (void **)(&list[i]) );
		}
		slapi_ch_free( (void **)(&list) );
	}
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
        filename = slapi_ch_smprintf("%s/cert7.db", dir);
        status = PR_Access(filename, PR_ACCESS_READ_OK);
        if (PR_SUCCESS != status) {
            ret = 1;
            if (!no_log) {
                slapi_log_error(SLAPI_LOG_FATAL, "SSL Initialization",
                    "Warning: certificate DB file cert8.db nor cert7.db exists in [%s] - "
                    "SSL initialization will likely fail\n", dir);
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
        ret = 1;
        if (!no_log) {
            slapi_log_error(SLAPI_LOG_FATAL, "SSL Initialization",
                "Warning: key DB file %s does not exist - SSL initialization will "
                "likely fail\n", filename);
        }
	}

	slapi_ch_free_string(&filename);
    return ret;
}

#if !defined(NSS_TLS10) /* NSS_TLS11 or newer */
typedef struct _nss_version_list {
    PRUint16 vnum;
    char* vname;
} NSSVersion_list;
NSSVersion_list _NSSVersion_list[] =
{
    {SSL_LIBRARY_VERSION_2,       "SSL2"},
    {SSL_LIBRARY_VERSION_3_0,     "SSL3"},
    {SSL_LIBRARY_VERSION_TLS_1_0, "TLS1.0"},
    {SSL_LIBRARY_VERSION_TLS_1_1, "TLS1.1"},
#if defined(NSS_TLS12)
    {SSL_LIBRARY_VERSION_TLS_1_2, "TLS1.2"},
#endif
    {0, "unknown"}
};

static char *
getNSSVersion_str(PRUint16 vnum)
{
    NSSVersion_list *nvlp = NULL;
    char *vstr = "none";
    if (vnum) {
        for (nvlp = _NSSVersion_list; nvlp && nvlp->vnum; nvlp++) {
            if (nvlp->vnum == vnum) {
                vstr = nvlp->vname;
                break;
            }
        }
    }
    return vstr;
}

/* restrict SSLVersionRange with the existing SSL config params (nsSSL3, nsTLS1) */
static void
restrict_SSLVersionRange(SSLVersionRange *sslversion, PRBool enableSSL3, PRBool enableTLS1)
{
    int rc = 0;
    if (enableSSL3) {
        if (enableTLS1) {
            /* no restriction */
            ;
        } else {
            if (enabledNSSVersions.min > SSL_LIBRARY_VERSION_3_0) {
                slapd_SSL_warn("Security Initialization: "
                               "Supported range: min: %s, max: %s; "
                               "but the SSL configuration of the server disables nsTLS1. "
                               "Ignoring nsTLS1: off\n",
                               getNSSVersion_str(enabledNSSVersions.min),
                               getNSSVersion_str(enabledNSSVersions.max));
                rc = 1;
            } else if (sslversion->min > SSL_LIBRARY_VERSION_3_0) {
                slapd_SSL_warn("Security Initialization: "
                               "Configured range: min: %s, max: %s; "
                               "but the SSL configuration of the server disables nsTLS1. "
                               "Ignoring nsTLS1: off\n",
                               getNSSVersion_str(sslversion->min),
                               getNSSVersion_str(sslversion->max));
                rc = 1;
            } else if (sslversion->max < SSL_LIBRARY_VERSION_3_0) {
                slapd_SSL_warn("Security Initialization: "
                               "Configured range: min: %s, max: %s; "
                               "but the SSL configuration of the server enabled nsSSL3. "
                               "Ignoring max: %s\n",
                               getNSSVersion_str(sslversion->min),
                               getNSSVersion_str(sslversion->max),
                               getNSSVersion_str(sslversion->max));
                sslversion->min = SSL_LIBRARY_VERSION_3_0; /* don't enable SSL2 */
                sslversion->max = SSL_LIBRARY_VERSION_3_0;
                rc = 1;
            } else {
                sslversion->min = SSL_LIBRARY_VERSION_3_0; /* don't enable SSL2 */
                sslversion->max = SSL_LIBRARY_VERSION_3_0;
            }
        }
    } else {
        if (enableTLS1) {
            if (enabledNSSVersions.max < SSL_LIBRARY_VERSION_TLS_1_0) {
                slapd_SSL_warn("Security Initialization: "
                               "Supported range: min: %s, max: %s; "
                               "but the SSL configuration of the server disables nsSSL3. ",
                               "Ignoring nsSSL3: off\n",
                               getNSSVersion_str(enabledNSSVersions.min),
                               getNSSVersion_str(enabledNSSVersions.max));
                sslversion->min = SSL_LIBRARY_VERSION_3_0; /* don't enable SSL2 */
                sslversion->max = SSL_LIBRARY_VERSION_3_0;
                rc = 1;
            } else if (sslversion->max < SSL_LIBRARY_VERSION_TLS_1_0) {
                slapd_SSL_warn("Security Initialization: "
                               "Configured range: min: %s, max: %s; "
                               "but the SSL configuration of the server disables nsSSL3. "
                               "Ignoring nsSSL3: off\n",
                               getNSSVersion_str(sslversion->min),
                               getNSSVersion_str(sslversion->max));
                sslversion->min = SSL_LIBRARY_VERSION_3_0; /* don't enable SSL2 */
                sslversion->max = SSL_LIBRARY_VERSION_3_0;
                rc = 1;
            } else if (sslversion->min < SSL_LIBRARY_VERSION_TLS_1_0) {
                sslversion->min = SSL_LIBRARY_VERSION_TLS_1_0;
            }
        } else {
            slapd_SSL_warn("Security Initialization: "
                            "Supported range: min: %s, max: %s; "
                            "but the SSL configuration of the server disables nsSSL3 and nsTLS1. "
                            "Ignoring nsSSL3: off and nsTLS1: off\n",
                            getNSSVersion_str(enabledNSSVersions.min),
                            getNSSVersion_str(enabledNSSVersions.max));
            rc = 1;
        }
    }
    if (0 == rc) {
        slapi_log_error(SLAPI_LOG_FATAL, "SSL Initialization",
                        "SSL version range: min: %s, max: %s\n",
                        getNSSVersion_str(sslversion->min),
                        getNSSVersion_str(sslversion->max));
    }
}
#endif

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
slapd_nss_init(int init_ssl, int config_available)
{
	SECStatus secStatus;
	PRErrorCode errorCode;
	int rv = 0;
	int len = 0;
	int create_certdb = 0;
	PRUint32 nssFlags = 0;
	char *certdir;
	char *certdb_file_name = NULL;
	char *keydb_file_name = NULL;
	char *secmoddb_file_name = NULL;
#if !defined(NSS_TLS10) /* NSS_TLS11 or newer */
	/* Get the range of the supported SSL version */
	SSL_VersionRangeGetSupported(ssl_variant_stream, &enabledNSSVersions);
	
	slapi_log_error(SLAPI_LOG_CONFIG, "SSL Initialization",
	                "supported range: min: %s, max: %s\n",
	                getNSSVersion_str(enabledNSSVersions.min),
	                getNSSVersion_str(enabledNSSVersions.max));
#endif

	/* set in slapd_bootstrap_config,
	   thus certdir is available even if config_available is false */
	certdir = config_get_certdir();

	/* make sure path does not end in the path separator character */
	len = strlen(certdir);
	if (certdir[len-1] == '/' || certdir[len-1] == '\\') {
		certdir[len-1] = '\0';
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
#ifndef _WIN32
			serveruser = config_get_localuser();
#endif
			slapi_log_error(SLAPI_LOG_FATAL, "SSL Initialization",
				"Warning: The key/cert database directory [%s] is not writable by "
				"the server uid [%s]: initialization likely to fail.\n",
				certdir, serveruser);
#ifndef _WIN32
			slapi_ch_free_string(&serveruser);
#endif
		}
	}

	/* Check if we have a certdb already.  If not, set a flag that we are
	 * going to create one so we can set the appropriate permissions on it. */
	if (warn_if_no_cert_file(certdir, 1) || warn_if_no_key_file(certdir, 1)) {
		create_certdb = 1;
	}

	/******** Initialise NSS *********/
    
	nssFlags &= (~NSS_INIT_READONLY);
	slapd_pk11_configurePKCS11(NULL, NULL, tokDes, ptokDes, NULL, NULL, NULL, NULL, 0, 0 );
	secStatus = NSS_Initialize(certdir, NULL, NULL, "secmod.db", nssFlags);

	dongle_file_name = PR_smprintf("%s/pin.txt", certdir);

	if (secStatus != SECSuccess) {
		errorCode = PR_GetError();
		slapd_SSL_warn("Security Initialization: NSS initialization failed ("
					   SLAPI_COMPONENT_NAME_NSPR " error %d - %s): "
					   "certdir: %s",
					   errorCode, slapd_pr_strerror(errorCode), certdir);
		rv = -1;
	}

	if(SSLPLCY_Install() != PR_SUCCESS) {
		errorCode = PR_GetError();
		slapd_SSL_warn("Security Initialization: Unable to set SSL export policy ("
					   SLAPI_COMPONENT_NAME_NSPR " error %d - %s)", 
					   errorCode, slapd_pr_strerror(errorCode));
		return -1;
	}

	/* NSS creates the certificate db files with a mode of 600.  There
	 * is no way to pass in a mode to use for creation to NSS, so we
	 * need to modify it after creation.  We need to allow read and
	 * write permission to the group so the certs can be managed via
	 * the console/adminserver. */
	if (create_certdb) {
		certdb_file_name = slapi_ch_smprintf("%s/cert8.db", certdir);
		keydb_file_name = slapi_ch_smprintf("%s/key3.db", certdir);
		secmoddb_file_name = slapi_ch_smprintf("%s/secmod.db", certdir);
		if(chmod(certdb_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP )){
			LDAPDebug(LDAP_DEBUG_ANY, "slapd_nss_init: chmod failed for file %s error (%d) %s.\n",
					certdb_file_name, errno, slapd_system_strerror(errno));
		}
		if(chmod(keydb_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP )){
			LDAPDebug(LDAP_DEBUG_ANY, "slapd_nss_init: chmod failed for file %s error (%d) %s.\n",
					keydb_file_name, errno, slapd_system_strerror(errno));
		}
		if(chmod(secmoddb_file_name, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP )){
			LDAPDebug(LDAP_DEBUG_ANY, "slapd_nss_init: chmod failed for file %s error (%d) %s.\n",
					secmoddb_file_name, errno, slapd_system_strerror(errno));
		}
	}

    /****** end of NSS Initialization ******/
    _nss_initialized = 1;
    slapi_ch_free_string(&certdb_file_name);
    slapi_ch_free_string(&keydb_file_name);
    slapi_ch_free_string(&secmoddb_file_name);
    slapi_ch_free_string(&certdir);
    return rv;
}

static int
svrcore_setup()
{
    PRErrorCode errorCode;
    int rv = 0;
#ifndef _WIN32
    SVRCOREStdPinObj *StdPinObj;
#else
    SVRCOREFilePinObj *FilePinObj;
    SVRCOREAltPinObj *AltPinObj;
    SVRCORENTUserPinObj *NTUserPinObj;
#endif
#ifndef _WIN32
    StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
    if (StdPinObj) {
	return 0; /* already registered */
    }
    if ( SVRCORE_CreateStdPinObj(&StdPinObj, dongle_file_name, PR_TRUE) !=
	SVRCORE_Success) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Unable to create PinObj ("
				SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
				errorCode, slapd_pr_strerror(errorCode));
	return -1;
    }
    SVRCORE_RegisterPinObj((SVRCOREPinObj *)StdPinObj);
#else
    AltPinObj = (SVRCOREAltPinObj *)SVRCORE_GetRegisteredPinObj();
    if (AltPinObj) {
	return 0; /* already registered */
    }
    if (SVRCORE_CreateFilePinObj(&FilePinObj, dongle_file_name) !=
	SVRCORE_Success) {
        errorCode = PR_GetError();
	slapd_SSL_warn("Security Initialization: Unable to create FilePinObj ("
				SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
				errorCode, slapd_pr_strerror(errorCode));
	return -1;
    }
    if (SVRCORE_CreateNTUserPinObj(&NTUserPinObj) != SVRCORE_Success){
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Unable to create NTUserPinObj ("
				SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
				errorCode, slapd_pr_strerror(errorCode));
        return -1;
    }
    if (SVRCORE_CreateAltPinObj(&AltPinObj, (SVRCOREPinObj *)FilePinObj,
	(SVRCOREPinObj *)NTUserPinObj) != SVRCORE_Success) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Unable to create AltPinObj ("
				SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
				errorCode, slapd_pr_strerror(errorCode));
        return -1;
    }
    SVRCORE_RegisterPinObj((SVRCOREPinObj *)AltPinObj);

#endif /* _WIN32 */

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
    char ** family_list;
    char *val = NULL;
    char cipher_string[1024];
    int rv = 0;
    PK11SlotInfo *slot;
    Slapi_Entry *entry = NULL;
    int allowweakcipher = CIPHER_SET_DEFAULTWEAKCIPHER;

    /* Get general information */

    getConfigEntry( configDN, &entry );

    val = slapi_entry_attr_get_charptr( entry, "nssslSessionTimeout" );
    ciphers = slapi_entry_attr_get_charptr( entry, "nsssl3ciphers" );

    /* We are currently using the value of sslSessionTimeout
       for ssl3SessionTimeout, see SSL_ConfigServerSessionIDCache() */
    /* Note from Tom Weinstein on the meaning of the timeout:

       Timeouts are in seconds.  '0' means use the default, which is
       24hrs for SSL3 and 100 seconds for SSL2.
    */

    if(!val) {
      errorCode = PR_GetError();
      slapd_SSL_warn("Security Initialization: Failed to retrieve SSL "
                     "configuration information ("
                     SLAPI_COMPONENT_NAME_NSPR " error %d - %s): "
                     "nssslSessionTimeout: %s ",
                     errorCode, slapd_pr_strerror(errorCode),
             (val ? "found" : "not found"));
      slapi_ch_free((void **) &val);
      slapi_ch_free((void **) &ciphers);
      freeConfigEntry( &entry );
      return -1;
    }

    stimeout = atoi(val);
    slapi_ch_free((void **) &val);

    if (svrcore_setup()) {
        freeConfigEntry( &entry );
        return -1;
    }

    val = slapi_entry_attr_get_charptr(entry, "allowWeakCipher");
    if (val) {
        if (!PL_strcasecmp(val, "off") || !PL_strcasecmp(val, "false") || 
                !PL_strcmp(val, "0") || !PL_strcasecmp(val, "no")) {
            allowweakcipher = CIPHER_SET_DISALLOWWEAKCIPHER;
        } else if (!PL_strcasecmp(val, "on") || !PL_strcasecmp(val, "true") || 
                !PL_strcmp(val, "1") || !PL_strcasecmp(val, "yes")) {
            allowweakcipher = CIPHER_SET_ALLOWWEAKCIPHER;
        } else {
            slapd_SSL_warn("The value of allowWeakCipher \"%s\" in "
                           "cn=encryption,cn=config is invalid. "
                           "Ignoring it and set it to default.", val);
        }
    }
    slapi_ch_free((void **) &val);
 
    if ((family_list = getChildren(configDN))) {
        char **family;
        char *token;
        char *activation;

        for (family = family_list; *family; family++) {

            token = NULL;
            activation = NULL;

            freeConfigEntry( &entry );

            getConfigEntry( *family, &entry );
            if ( entry == NULL ) {
                continue;
            }

            activation = slapi_entry_attr_get_charptr( entry, "nssslactivation" );
            if((!activation) || (!PL_strcasecmp(activation, "off"))) {
                /* this family was turned off, goto next */
                slapi_ch_free((void **) &activation);
                continue;
            }

            slapi_ch_free((void **) &activation);

            token = slapi_entry_attr_get_charptr( entry, "nsssltoken" );
            if ( token ) {
                if (!PL_strcasecmp(token, "internal") ||
                    !PL_strcasecmp(token, "internal (software)")) {
                    slot = slapd_pk11_getInternalKeySlot();
                } else {
                    slot = slapd_pk11_findSlotByName(token);
                }
            } else {
                errorCode = PR_GetError();
                slapd_SSL_warn("Security Initialization: Unable to get token ("
                       SLAPI_COMPONENT_NAME_NSPR " error %d - %s)", 
                       errorCode, slapd_pr_strerror(errorCode));
                freeChildren(family_list);
                freeConfigEntry( &entry );
                return -1;
            }

            slapi_ch_free((void **) &token);

            if (!slot) {
                errorCode = PR_GetError();
                slapd_SSL_warn("Security Initialization: Unable to find slot ("
                       SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                       errorCode, slapd_pr_strerror(errorCode));
                freeChildren(family_list);
                freeConfigEntry( &entry );
                return -1;
            }
            /* authenticate */
            if (slapd_pk11_authenticate(slot, PR_TRUE, NULL) != SECSuccess) {
                errorCode = PR_GetError();
                slapd_SSL_warn("Security Initialization: Unable to authenticate ("
                       SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                       errorCode, slapd_pr_strerror(errorCode));
                freeChildren(family_list);
                freeConfigEntry( &entry );
                return -1;
            }
        }
        freeChildren( family_list );
        freeConfigEntry( &entry );
    }

    /* ugaston- Cipher preferences must be set before any sslSocket is created
     * for such sockets to take preferences into account.
     */

    /* Step Three.5: Set SSL cipher preferences */
    *cipher_string = 0;
    if(ciphers && (*ciphers) && PL_strcmp(ciphers, "blank"))
         PL_strncpyz(cipher_string, ciphers, sizeof(cipher_string));
    slapi_ch_free((void **) &ciphers);

    if ( NULL != (val = _conf_setciphers(cipher_string, allowweakcipher)) ) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Failed to set SSL cipher "
            "preference information: %s (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)", 
            val, errorCode, slapd_pr_strerror(errorCode));
        rv = 3;
        slapi_ch_free((void **) &val);
    }

    freeConfigEntry( &entry );
 
    /* Introduce a way of knowing whether slapd_ssl_init has
     * already been executed. */
    _security_library_initialized = 1; 

    if ( rv != 0 ) {
        return rv;
    }

    return 0;
}

#if !defined(NSS_TLS10) /* NSS_TLS11 or newer */
/* 
 * val:   sslVersionMin/Max value set in cn=encription,cn=config (INPUT)
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
    char *vp, *endp;
    int vnum;

    if (NULL == rval) {
        return 1;
    }
    if (!strncasecmp(val, SSLSTR, SSLLEN)) { /* ssl# */
        vp = val + SSLLEN;
        vnum = strtol(vp, &endp, 10);
        if (2 == vnum) {
            if (ismin) {
                if (enabledNSSVersions.min > SSL_LIBRARY_VERSION_2) {
                   slapd_SSL_warn("Security Initialization: The value of sslVersionMin "
                                  "\"%s\" is lower than the supported version; "
                                  "the default value \"%s\" is used.\n",
                                  val, getNSSVersion_str(enabledNSSVersions.min));
                   (*rval) = enabledNSSVersions.min;
                } else {
                   (*rval) = SSL_LIBRARY_VERSION_2;
                }
            } else {
                if (enabledNSSVersions.max < SSL_LIBRARY_VERSION_2) {
                    /* never happens */
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.max));
                   (*rval) = enabledNSSVersions.max;
                } else {
                   (*rval) = SSL_LIBRARY_VERSION_2;
                }
            }
        } else if (3 == vnum) {
            if (ismin) {
                if (enabledNSSVersions.min > SSL_LIBRARY_VERSION_3_0) {
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMin "
                                   "\"%s\" is lower than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.min));
                   (*rval) = enabledNSSVersions.min;
                } else {
                   (*rval) = SSL_LIBRARY_VERSION_3_0;
                }
            } else {
                if (enabledNSSVersions.max < SSL_LIBRARY_VERSION_3_0) {
                    /* never happens */
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.max));
                    (*rval) = enabledNSSVersions.max;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_3_0;
                }
            }
        } else {
            if (ismin) {
                slapd_SSL_warn("Security Initialization: The value of sslVersionMin "
                               "\"%s\" is invalid; the default value \"%s\" is used.\n",
                               val, getNSSVersion_str(enabledNSSVersions.min));
                (*rval) = enabledNSSVersions.min;
            } else {
                slapd_SSL_warn("Security Initialization: The value of sslVersionMax "
                               "\"%s\" is invalid; the default value \"%s\" is used.\n",
                               val, getNSSVersion_str(enabledNSSVersions.max));
                (*rval) = enabledNSSVersions.max;
            }
        }
    } else if (!strncasecmp(val, TLSSTR, TLSLEN)) { /* tls# */
        float tlsv;
        vp = val + TLSLEN;
        sscanf(vp, "%4f", &tlsv);
        if (tlsv < 1.1) { /* TLS1.0 */
            if (ismin) {
                if (enabledNSSVersions.min > SSL_LIBRARY_VERSION_TLS_1_0) {
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMin "
                                   "\"%s\" is lower than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.min));
                   (*rval) = enabledNSSVersions.min;
                } else {
                   (*rval) = SSL_LIBRARY_VERSION_TLS_1_0;
                }
            } else {
                if (enabledNSSVersions.max < SSL_LIBRARY_VERSION_TLS_1_0) {
                    /* never happens */
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.max));
                    (*rval) = enabledNSSVersions.max;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_0;
                }
            }
        } else if (tlsv < 1.2) { /* TLS1.1 */
            if (ismin) {
                if (enabledNSSVersions.min > SSL_LIBRARY_VERSION_TLS_1_1) {
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMin "
                                   "\"%s\" is lower than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.min));
                   (*rval) = enabledNSSVersions.min;
                } else {
                   (*rval) = SSL_LIBRARY_VERSION_TLS_1_1;
                }
            } else {
                if (enabledNSSVersions.max < SSL_LIBRARY_VERSION_TLS_1_1) {
                    /* never happens */
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.max));
                    (*rval) = enabledNSSVersions.max;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_1;
                }
            }
        } else if (tlsv < 1.3) { /* TLS1.2 */
#if defined(NSS_TLS12)
            if (ismin) {
                if (enabledNSSVersions.min > SSL_LIBRARY_VERSION_TLS_1_2) {
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMin "
                                   "\"%s\" is lower than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.min));
                   (*rval) = enabledNSSVersions.min;
                } else {
                   (*rval) = SSL_LIBRARY_VERSION_TLS_1_2;
                }
            } else {
                if (enabledNSSVersions.max < SSL_LIBRARY_VERSION_TLS_1_2) {
                    /* never happens */
                    slapd_SSL_warn("Security Initialization: The value of sslVersionMax "
                                   "\"%s\" is higher than the supported version; "
                                   "the default value \"%s\" is used.\n",
                                   val, getNSSVersion_str(enabledNSSVersions.max));
                    (*rval) = enabledNSSVersions.max;
                } else {
                    (*rval) = SSL_LIBRARY_VERSION_TLS_1_2;
                }
            }
#endif
        } else { /* Specified TLS is newer than supported */
            if (ismin) {
                slapd_SSL_warn("Security Initialization: The value of sslVersionMin "
                               "\"%s\" is out of the range of the supported version; "
                               "the default value \"%s\" is used.\n",
                               val, getNSSVersion_str(enabledNSSVersions.min));
                (*rval) = enabledNSSVersions.min;
            } else {
                slapd_SSL_warn("Security Initialization: The value of sslVersionMax "
                               "\"%s\" is out of the range of the supported version; "
                               "the default value \"%s\" is used.\n",
                               val, getNSSVersion_str(enabledNSSVersions.min));
                (*rval) = enabledNSSVersions.max;
            }
        }
    } else {
        if (ismin) {
            slapd_SSL_warn("Security Initialization: The value of sslVersionMin "
                           "\"%s\" is invalid; the default value \"%s\" is used.\n",
                           val, getNSSVersion_str(enabledNSSVersions.min));
            (*rval) = enabledNSSVersions.min;
        } else {
            slapd_SSL_warn("Security Initialization: The value of sslVersionMax "
                           "\"%s\" is invalid; the default value \"%s\" is used.\n",
                           val, getNSSVersion_str(enabledNSSVersions.min));
            (*rval) = enabledNSSVersions.max;
        }
    }
    return 0;
}
#undef SSLSTR
#undef SSLLEN
#undef TLSSTR
#undef TLSLEN
#endif

int 
slapd_ssl_init2(PRFileDesc **fd, int startTLS)
{
    PRFileDesc        *pr_sock, *sock = (*fd);
    PRErrorCode errorCode;
    SECStatus  rv = SECFailure;
    char ** family_list;
    CERTCertificate   *cert = NULL;
    SECKEYPrivateKey  *key = NULL;
    char errorbuf[BUFSIZ];
    char *val = NULL;
    char *default_val = NULL;
    int nFamilies = 0;
    SECStatus sslStatus;
    int slapd_SSLclientAuth;
    char* tmpDir;
    Slapi_Entry *e = NULL;
    PRBool enableSSL2 = PR_FALSE;
    PRBool enableSSL3 = PR_TRUE;
    PRBool enableTLS1 = PR_TRUE;
    PRBool fipsMode = PR_FALSE;
#if !defined(NSS_TLS10) /* NSS_TLS11 or newer */
    PRUint16 NSSVersionMin = enabledNSSVersions.min;
    PRUint16 NSSVersionMax = enabledNSSVersions.max;
#endif

    /* turn off the PKCS11 pin interactive mode */
#ifndef _WIN32
    SVRCOREStdPinObj *StdPinObj;

    if (svrcore_setup()) {
        return 1;
    }

    StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
    SVRCORE_SetStdPinInteractive(StdPinObj, PR_FALSE);
#endif

    errorbuf[0] = '\0';

    /* Import pr fd into SSL */
    pr_sock = SSL_ImportFD( NULL, sock );
    if( pr_sock == (PRFileDesc *)NULL ) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Failed to import NSPR "
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
            slapd_SSL_warn("Security Initialization: Unable to get internal slot ("
                SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                errorCode, slapd_pr_strerror(errorCode));
            return -1;
        }

        if(slapd_pk11_isFIPS()) {
            if(slapd_pk11_authenticate(slot, PR_TRUE, NULL) != SECSuccess) {
               errorCode = PR_GetError();
               slapd_SSL_warn("Security Initialization: Unable to authenticate ("
                  SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                  errorCode, slapd_pr_strerror(errorCode));
               return -1;
            }
            fipsMode = PR_TRUE;
            /* FIPS does not like to use SSLv3 */
            enableSSL3 = PR_FALSE;
        }
    
        slapd_pk11_setSlotPWValues(slot, 0, 0);
    }



    /*
     * Now, get the complete list of cipher families. Each family
     * has a token name and personality name which we'll use to find
     * appropriate keys and certs, and call SSL_ConfigSecureServer
     * with.
     */

    if((family_list = getChildren(configDN))) {
        char **family;
        char cert_name[1024];
        char *token;
        char *personality;
        char *activation;

        for (family = family_list; *family; family++) {
            token = NULL;
            personality = NULL;
            activation = NULL;

            getConfigEntry( *family, &e );
            if ( e == NULL ) {
                continue;
            }

            activation = slapi_entry_attr_get_charptr( e, "nssslactivation" );
            if((!activation) || (!PL_strcasecmp(activation, "off"))) {
                /* this family was turned off, goto next */
                slapi_ch_free((void **) &activation);
                freeConfigEntry( &e );
                continue;
            }

            slapi_ch_free((void **) &activation);

            token = slapi_entry_attr_get_charptr( e, "nsssltoken" );
            personality = slapi_entry_attr_get_charptr( e, "nssslpersonalityssl" );
            if( token && personality ) {
                if( !PL_strcasecmp(token, "internal") ||
                    !PL_strcasecmp(token, "internal (software)") )
                    PL_strncpyz(cert_name, personality, sizeof(cert_name));
                else
                    /* external PKCS #11 token - attach token name */
                    PR_snprintf(cert_name, sizeof(cert_name), "%s:%s", token, personality);
            }
            else {
                errorCode = PR_GetError();
                slapd_SSL_warn("Security Initialization: Failed to get cipher "
                           "family information. Missing nsssltoken or"
                           "nssslpersonalityssl in %s ("
                            SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                           *family, errorCode, slapd_pr_strerror(errorCode));
                slapi_ch_free((void **) &token);
                slapi_ch_free((void **) &personality);
                freeConfigEntry( &e );
                continue;
            }

            slapi_ch_free((void **) &token);

            /* Step Four -- Locate the server certificate */
            cert = slapd_pk11_findCertFromNickname(cert_name, NULL);

            if (cert == NULL) {
                errorCode = PR_GetError();
                slapd_SSL_warn("Security Initialization: Can't find "
                            "certificate (%s) for family %s ("
                            SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                            cert_name, *family, 
                            errorCode, slapd_pr_strerror(errorCode));
            }
            /* Step Five -- Get the private key from cert  */
            if( cert != NULL )
                key = slapd_pk11_findKeyByAnyCert(cert, NULL);

            if (key == NULL) {
                errorCode = PR_GetError();
                slapd_SSL_warn("Security Initialization: Unable to retrieve "
                           "private key for cert %s of family %s ("
                           SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                            cert_name, *family,
                            errorCode, slapd_pr_strerror(errorCode));
                slapi_ch_free((void **) &personality);
                CERT_DestroyCertificate(cert);
                cert = NULL;
                freeConfigEntry( &e );
                continue;
            }

            /* Step Six  -- Configure Secure Server Mode  */
            if(pr_sock) {
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
                                       "for cert %s of family %s ("
                                       SLAPI_COMPONENT_NAME_NSPR
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
                    if( slapd_pk11_fortezzaHasKEA(cert) == PR_TRUE ) {
                        rv = SSL_ConfigSecureServer(*fd, cert, key, kt_fortezza);
                    }
                    else {
                        rv = SSL_ConfigSecureServer(*fd, cert, key, kt_rsa);
                    }
                    if (SECSuccess != rv) {
                        errorCode = PR_GetError();
                        slapd_SSL_warn("ConfigSecureServer: "
                                "Server key/certificate is "
                                "bad for cert %s of family %s ("
                                SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
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
            slapi_ch_free((void **) &personality);
            if (SECSuccess != rv) {
                freeConfigEntry( &e );
                continue;
            }
            nFamilies++;
            freeConfigEntry( &e );
        }
        freeChildren( family_list );
    }


    if ( !nFamilies ) {
        slapd_SSL_error("None of the cipher are valid");
        return -1;
    }

    /* Step Seven -- Configure Server Session ID Cache  */

    tmpDir = slapd_get_tmp_dir();

    slapi_log_error(SLAPI_LOG_TRACE,
                    "slapd_ssl_init2", "tmp dir = %s\n", tmpDir);

    rv = SSL_ConfigServerSessionIDCache(0, stimeout, stimeout, tmpDir);
    slapi_ch_free_string(&tmpDir);
    if (rv) {
      errorCode = PR_GetError();
      if (errorCode == ENOSPC) {
        slapd_SSL_error("Config of server nonce cache failed, "
            "out of disk space! Make more room in /tmp "
            "and try again. (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
            errorCode, slapd_pr_strerror(errorCode));
      }
      else {
    slapd_SSL_error("Config of server nonce cache failed (error %d - %s)",
            errorCode, slapd_pr_strerror(errorCode));
      }
      return rv;
    }

    sslStatus = SSL_OptionSet(pr_sock, SSL_SECURITY, PR_TRUE);
    if (sslStatus != SECSuccess) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Failed to enable security "
               "on the imported socket (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
               errorCode, slapd_pr_strerror(errorCode));
        return -1;
    }

/* Explicitly disabling SSL2 - NGK */
    sslStatus = SSL_OptionSet(pr_sock, SSL_ENABLE_SSL2, enableSSL2);
    if (sslStatus != SECSuccess) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Failed to %s SSLv2 "
               "on the imported socket (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
               enableSSL2 ? "enable" : "disable",
               errorCode, slapd_pr_strerror(errorCode));
        return -1;
    }

    /* Retrieve the SSL Client Authentication status from cn=config */
    /* Set a default value if no value found */
    getConfigEntry( configDN, &e );
    val = NULL;
    if ( e != NULL ) {
        val = slapi_entry_attr_get_charptr( e, "nssslclientauth" );
    }

    if( !val ) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Cannot get SSL Client "
               "Authentication status. No nsslclientauth in %s ("
                SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
               configDN, errorCode, slapd_pr_strerror(errorCode));
        switch( SLAPD_SSLCLIENTAUTH_DEFAULT ) {
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
        val = default_val;
    }
    if( config_set_SSLclientAuth( "nssslclientauth", val, errorbuf,
                CONFIG_APPLY ) != LDAP_SUCCESS ) {
            errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Cannot set SSL Client "
                   "Authentication status to \"%s\", error (%s). "
                   "Supported values are \"off\", \"allowed\" "
                   "and \"required\". (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                   val, errorbuf, errorCode, slapd_pr_strerror(errorCode));
    }
    if (val != default_val) {
        slapi_ch_free_string(&val);
    }

    if ( e != NULL ) {
        val = slapi_entry_attr_get_charptr( e, "nsSSL3" );
        if ( val ) {
            if ( !PL_strcasecmp( val, "off" ) ) {
                enableSSL3 = PR_FALSE;
            } else if ( !PL_strcasecmp( val, "on" ) ) {
                enableSSL3 = PR_TRUE;
            } else {
                enableSSL3 = slapi_entry_attr_get_bool( e, "nsSSL3" );
            }
            if ( fipsMode && enableSSL3 ) {
                slapd_SSL_warn("Security Initialization: FIPS mode is enabled and "
                               "nsSSL3 explicitly set to on - SSLv3 is not approved "
                               "for use in FIPS mode - SSLv3 will be disabled - if "
                               "you want to use SSLv3, you must use modutil to "
                               "disable FIPS in the internal token.");
                enableSSL3 = PR_FALSE;
            }
        }
        slapi_ch_free_string( &val );
        val = slapi_entry_attr_get_charptr( e, "nsTLS1" );
        if ( val ) {
            if ( !PL_strcasecmp( val, "off" ) ) {
                enableTLS1 = PR_FALSE;
            } else if ( !PL_strcasecmp( val, "on" ) ) {
                enableTLS1 = PR_TRUE;
            } else {
                enableTLS1 = slapi_entry_attr_get_bool( e, "nsTLS1" );
            }
        }
        slapi_ch_free_string( &val );
#if !defined(NSS_TLS10) /* NSS_TLS11 or newer */
        val = slapi_entry_attr_get_charptr( e, "sslVersionMin" );
        if ( val ) {
            (void)set_NSS_version(val, &NSSVersionMin, 1);
        }
        slapi_ch_free_string( &val );
        val = slapi_entry_attr_get_charptr( e, "sslVersionMax" );
        if ( val ) {
            (void)set_NSS_version(val, &NSSVersionMax, 0);
        }
        slapi_ch_free_string( &val );
        if (NSSVersionMin > NSSVersionMax) {
            slapd_SSL_warn("Security Initialization: The min value of NSS version range "
                        "\"%s\" is greater than the max value \"%s\"; "
                           "the default range \"%s\" - \"%s\" is used.\n",
                           getNSSVersion_str(NSSVersionMin), 
                           getNSSVersion_str(NSSVersionMax),
                           getNSSVersion_str(enabledNSSVersions.min),
                           getNSSVersion_str(enabledNSSVersions.max));
            NSSVersionMin = enabledNSSVersions.min;
            NSSVersionMax = enabledNSSVersions.max;
        }
#endif
    }
#if !defined(NSS_TLS10) /* NSS_TLS11 or newer */
    if (NSSVersionMin > 0) {
        /* Use new NSS API SSL_VersionRangeSet (NSS3.14 or newer) */
        SSLVersionRange myNSSVersions;
        myNSSVersions.min = NSSVersionMin;
        myNSSVersions.max = NSSVersionMax;
        restrict_SSLVersionRange(&myNSSVersions, enableSSL3, enableTLS1);
        sslStatus = SSL_VersionRangeSet(pr_sock, &myNSSVersions);
        if (sslStatus == SECSuccess) {
            /* Set the restricted value to the cn=encryption entry */
        } else {
            slapd_SSL_error("SSL Initialization 2: "
                            "Failed to set SSL range: min: %s, max: %s\n",
                            getNSSVersion_str(myNSSVersions.min),
                            getNSSVersion_str(myNSSVersions.max));
        }
    } else {
#endif
        /* deprecated code */
        sslStatus = SSL_OptionSet(pr_sock, SSL_ENABLE_SSL3, enableSSL3);
        if (sslStatus != SECSuccess) {
            errorCode = PR_GetError();
            slapd_SSL_warn("Security Initialization: Failed to %s SSLv3 "
                   "on the imported socket (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                   enableSSL3 ? "enable" : "disable",
                   errorCode, slapd_pr_strerror(errorCode));
        }

        sslStatus = SSL_OptionSet(pr_sock, SSL_ENABLE_TLS, enableTLS1);
        if (sslStatus != SECSuccess) {
            errorCode = PR_GetError();
            slapd_SSL_warn("Security Initialization: Failed to %s TLSv1 "
                   "on the imported socket (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
                   enableTLS1 ? "enable" : "disable",
                   errorCode, slapd_pr_strerror(errorCode));
        }
#if !defined(NSS_TLS10) /* NSS_TLS11 or newer */
    }
#endif
    freeConfigEntry( &e );

    if(( slapd_SSLclientAuth = config_get_SSLclientAuth()) != SLAPD_SSLCLIENTAUTH_OFF ) {
        int err;
        switch (slapd_SSLclientAuth) {
          case SLAPD_SSLCLIENTAUTH_ALLOWED:
#ifdef SSL_REQUIRE_CERTIFICATE    /* new feature */
            if ((err = SSL_OptionSet (pr_sock, SSL_REQUIRE_CERTIFICATE, PR_FALSE)) < 0) {
                PRErrorCode prerr = PR_GetError();
                LDAPDebug (LDAP_DEBUG_ANY,
                 "SSL_OptionSet(SSL_REQUIRE_CERTIFICATE,PR_FALSE) %d "
                 SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                 err, prerr, slapd_pr_strerror(prerr));
            }
#endif
            /* Give the client a clear opportunity to send her certificate: */
          case SLAPD_SSLCLIENTAUTH_REQUIRED:
            if ((err = SSL_OptionSet (pr_sock, SSL_REQUEST_CERTIFICATE, PR_TRUE)) < 0) {
                PRErrorCode prerr = PR_GetError();
                LDAPDebug (LDAP_DEBUG_ANY,
                 "SSL_OptionSet(SSL_REQUEST_CERTIFICATE,PR_TRUE) %d "
                 SLAPI_COMPONENT_NAME_NSPR " error %d (%s)\n",
                 err, prerr, slapd_pr_strerror(prerr));
            }
          default: break;
        }
    }

    /* Introduce a way of knowing whether slapd_ssl_init2 has
     * already been executed.
     * The cases in which slapd_ssl_init2 is executed during an
     * Start TLS operation are not taken into account, for it is
     * the fact of being executed by the server's SSL listener socket
     * that matters. */

    if (!startTLS)
      _ssl_listener_initialized = 1; /* --ugaston */

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
slapd_SSL_client_auth (LDAP* ld)
{
    int rc = 0;
    PRErrorCode errorCode;
    char* pw = NULL;
    char ** family_list;
    Slapi_Entry *entry = NULL;
    char cert_name[1024];
    char *token = NULL;
#ifndef _WIN32
    SVRCOREStdPinObj *StdPinObj;
#else
    SVRCOREAltPinObj *AltPinObj;
#endif
    SVRCOREError err = SVRCORE_Success;

    if((family_list = getChildren(configDN))) {
        char **family;
        char *personality = NULL;
        char *activation = NULL;
		char *cipher = NULL;

        for (family = family_list; *family; family++) {
            getConfigEntry( *family, &entry );
            if ( entry == NULL ) {
                    continue;
            }

            activation = slapi_entry_attr_get_charptr( entry, "nssslactivation" );
            if((!activation) || (!PL_strcasecmp(activation, "off"))) {
                    /* this family was turned off, goto next */
					slapi_ch_free((void **) &activation);
					freeConfigEntry( &entry );
                    continue;
            }

	    slapi_ch_free((void **) &activation);

            personality = slapi_entry_attr_get_charptr( entry, "nssslpersonalityssl" );
            cipher = slapi_entry_attr_get_charptr( entry, "cn" );
	    if ( cipher && !PL_strcasecmp(cipher, "RSA" )) {
			char *ssltoken;

			/* If there already is a token name, use it */
			if (token) {
				slapi_ch_free((void **) &personality);
				slapi_ch_free((void **) &cipher);
				freeConfigEntry( &entry );
				continue;
			}

			ssltoken = slapi_entry_attr_get_charptr( entry, "nsssltoken" );
 			if( ssltoken && personality ) {
			  if( !PL_strcasecmp(ssltoken, "internal") ||
			      !PL_strcasecmp(ssltoken, "internal (software)") ) {

						/* Translate config internal name to more
			 			 * readable form.  Certificate name is just
			 			 * the personality for internal tokens.
			 			 */
						token = slapi_ch_strdup(internalTokenName);
#if defined(USE_OPENLDAP)
						/* openldap needs tokenname:certnick */
						PR_snprintf(cert_name, sizeof(cert_name), "%s:%s", token, personality);
#else
						PL_strncpyz(cert_name, personality, sizeof(cert_name));
#endif
						slapi_ch_free((void **) &ssltoken);
			  } else {
						/* external PKCS #11 token - attach token name */
						/*ssltoken was already dupped and we don't need it anymore*/
						token = ssltoken;
						PR_snprintf(cert_name, sizeof(cert_name), "%s:%s", token, personality);
			  }
 			} else {
			  errorCode = PR_GetError(); 
			  slapd_SSL_warn("Security Initialization: Failed to get cipher "
					 "family information.  Missing nsssltoken or"
					 "nssslpersonalityssl in %s ("
					 SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
					 *family, errorCode, slapd_pr_strerror(errorCode));
	  		  slapi_ch_free((void **) &ssltoken);
			  slapi_ch_free((void **) &personality);
			  slapi_ch_free((void **) &cipher);
			  freeConfigEntry( &entry );
			  continue;
			}
	    } else { /* external PKCS #11 cipher */
			char *ssltoken;

			ssltoken = slapi_entry_attr_get_charptr( entry, "nsssltoken" );
			if( token && personality ) {

				/* free the old token and remember the new one */
				if (token) slapi_ch_free((void **)&token);
				token = ssltoken; /*ssltoken was already dupped and we don't need it anymore*/

				/* external PKCS #11 token - attach token name */
				PR_snprintf(cert_name, sizeof(cert_name), "%s:%s", token, personality);
			} else {
			  errorCode = PR_GetError();
			  slapd_SSL_warn("Security Initialization: Failed to get cipher "
					 "family information.  Missing nsssltoken or"
					 "nssslpersonalityssl in %s ("
					 SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
					 *family, errorCode, slapd_pr_strerror(errorCode));
	        	  slapi_ch_free((void **) &ssltoken);
	    		  slapi_ch_free((void **) &personality);
			  slapi_ch_free((void **) &cipher);
			  freeConfigEntry( &entry );
			  continue;
			}

	    }
	    slapi_ch_free((void **) &personality);
	    slapi_ch_free((void **) &cipher);
		freeConfigEntry( &entry );
        } /* end of for */

		freeChildren( family_list );
    }

    /* Free config data */

    if (!svrcore_setup()) {
#ifndef _WIN32
	StdPinObj = (SVRCOREStdPinObj *)SVRCORE_GetRegisteredPinObj();
	err =  SVRCORE_StdPinGetPin( &pw, StdPinObj, token );
#else
	AltPinObj = (SVRCOREAltPinObj *)SVRCORE_GetRegisteredPinObj();
	pw = SVRCORE_GetPin( (SVRCOREPinObj *)AltPinObj, token, PR_FALSE);
#endif
	if ( err != SVRCORE_Success || pw == NULL) {
	    errorCode = PR_GetError();
	    slapd_SSL_warn("SSL client authentication cannot be used "
			   "(no password). (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)", 
			   errorCode, slapd_pr_strerror(errorCode));
	} else {
#if defined(USE_OPENLDAP)
	    rc = ldap_set_option(ld, LDAP_OPT_X_TLS_KEYFILE, SERVER_KEY_NAME);
	    if (rc) {
		slapd_SSL_warn("SSL client authentication cannot be used "
			       "unable to set the key to use to %s", SERVER_KEY_NAME);
	    }
	    rc = ldap_set_option(ld, LDAP_OPT_X_TLS_CERTFILE, cert_name);
	    if (rc) {
		slapd_SSL_warn("SSL client authentication cannot be used "
			       "unable to set the cert to use to %s", cert_name);
	    }
	    /* not sure what else needs to be done for client auth - don't 
	       currently have a way to pass in the password to use to unlock
	       the keydb - nor a way to disable caching */
#else /* !USE_OPENLDAP */
	    rc = ldapssl_enable_clientauth (ld, SERVER_KEY_NAME, pw, cert_name);
	    if (rc != 0) {
		errorCode = PR_GetError();
		slapd_SSL_warn("ldapssl_enable_clientauth(%s, %s) %i ("
			       SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
			       SERVER_KEY_NAME, cert_name, rc, 
			       errorCode, slapd_pr_strerror(errorCode));
	    } else {
		/* We cannot allow NSS to cache outgoing client auth connections -
		   each client auth connection must have it's own non-shared SSL
		   connection to the peer so that it will go through the
		   entire handshake protocol every time including the use of its
		   own unique client cert - see bug 605457
		*/

		ldapssl_set_option(ld, SSL_NO_CACHE, PR_TRUE);
	    }
#endif
	}
    }

    if (token) slapi_ch_free((void**)&token);
    slapi_ch_free((void**)&pw);

    LDAPDebug (LDAP_DEBUG_TRACE, "slapd_SSL_client_auth() %i\n", rc, 0, 0);
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
char* slapd_get_tmp_dir()
{
	static char tmp[MAXPATHLEN];
	char* tmpdir = NULL;;
#if defined( XP_WIN32 )
	unsigned ilen;
	char pch;
#endif

	tmp[0] = '\0';

	if((tmpdir = config_get_tmpdir()) == NULL)
	{
		slapi_log_error(
			 SLAPI_LOG_FATAL,
			 "slapd_get_tmp_dir",
			 "config_get_tmpdir returns NULL Setting tmp dir to default\n");

#if defined( XP_WIN32 )
		ilen = sizeof(tmp);
		GetTempPath( ilen, tmp );
		tmp[ilen-1] = (char)0;
		ilen = strlen(tmp);
		/* Remove trailing slash. */
		pch = tmp[ilen-1];
		if( pch == '\\' || pch == '/' )
			tmp[ilen-1] = '\0';
#else
		strcpy(tmp, "/tmp");
#endif
		return slapi_ch_strdup(tmp);
	}

#if defined( XP_WIN32 )
	{
		char *ptr = NULL;
		char *endptr = tmpdir + strlen(tmpdir);
		for(ptr = tmpdir; ptr < endptr; ptr++)
		{
			if('/' == *ptr)
				*ptr = '\\';
		}
	}
#endif

#if defined( XP_WIN32 )
	if(CreateDirectory(tmpdir, NULL) == 0)
	{
		slapi_log_error(
			 SLAPI_LOG_FATAL,
			 "slapd_get_tmp_dir",
			 "CreateDirectory(%s, NULL) Error: %s\n",
			 tmpdir, strerror(errno));	
	}
#else
	if(mkdir(tmpdir, 00770) == -1)
	{
		if (errno == EEXIST) {
			slapi_log_error(
			 SLAPI_LOG_TRACE,
			 "slapd_get_tmp_dir",
			 "mkdir(%s, 00770) - already exists\n",
			 tmpdir);
		} else {
			slapi_log_error(
			 SLAPI_LOG_FATAL,
			 "slapd_get_tmp_dir",
			 "mkdir(%s, 00770) Error: %s\n",
			 tmpdir, strerror(errno));
		}
	}
#endif
	return ( tmpdir );
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
		slapi_log_error(SLAPI_LOG_FATAL, "slapd_get_unlocked_key_for_cert",
				"Error: cannot get slot list for certificate [%s] (%d: %s)\n",
				certsubject, errcode, slapd_pr_strerror(errcode));
		return key;
	}

	for (sle = slotlist->head; sle; sle = sle->next) {
		PK11SlotInfo *slot = sle->slot;
		const char *slotname = (slot && PK11_GetSlotName(slot)) ? PK11_GetSlotName(slot) : "unknown slot";
		const char *tokenname = (slot && PK11_GetTokenName(slot)) ? PK11_GetTokenName(slot) : "unknown token";
		if (!slot) {
			slapi_log_error(SLAPI_LOG_TRACE, "slapd_get_unlocked_key_for_cert",
					"Missing slot for slot list element for certificate [%s]\n",
					certsubject);
		} else if (!PK11_NeedLogin(slot) || PK11_IsLoggedIn(slot, pin_arg)) {
			key = PK11_FindKeyByDERCert(slot, cert, pin_arg);
			slapi_log_error(SLAPI_LOG_TRACE, "slapd_get_unlocked_key_for_cert",
					"Found unlocked slot [%s] token [%s] for certificate [%s]\n",
					slotname, tokenname, certsubject);
			break;
		} else {
			slapi_log_error(SLAPI_LOG_TRACE, "slapd_get_unlocked_key_for_cert",
					"Skipping locked slot [%s] token [%s] for certificate [%s]\n",
					slotname, tokenname, certsubject);
		}
	}

	if (!key) {
		slapi_log_error(SLAPI_LOG_FATAL, "slapd_get_unlocked_key_for_cert",
				"Error: could not find any unlocked slots for certificate [%s].  "
		                "Please review your TLS/SSL configuration.  The following slots were found:\n",
		                certsubject);
		for (sle = slotlist->head; sle; sle = sle->next) {
			PK11SlotInfo *slot = sle->slot;
			const char *slotname = (slot && PK11_GetSlotName(slot)) ? PK11_GetSlotName(slot) : "unknown slot";
			const char *tokenname = (slot && PK11_GetTokenName(slot)) ? PK11_GetTokenName(slot) : "unknown token";
			slapi_log_error(SLAPI_LOG_FATAL, "slapd_get_unlocked_key_for_cert",
					"Slot [%s] token [%s] was locked.\n",
					slotname, tokenname);
		}

	}

	PK11_FreeSlotList(slotlist);
	return key;
}

