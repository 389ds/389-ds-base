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
#include <ldap_ssl.h> /* ldapssl_client_init */
#include "certdb.h"

/* For IRIX... */
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

extern char* slapd_SSL3ciphers;
extern symbol_t supported_ciphers[];

/* dongle_file_name is set in slapd_nss_init when we set the path for the
   key, cert, and secmod files - the dongle file must be in the same directory
   and use the same naming scheme
*/
static char*	dongle_file_name = NULL;

static int _security_library_initialized = 0;
static int _ssl_listener_initialized = 0;

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


static char **cipher_names = NULL;
typedef struct {
	char *version;
    char *name;
    int num;
} cipherstruct;


static cipherstruct _conf_ciphers[] = {
    {"SSL3","rc4", SSL_EN_RC4_128_WITH_MD5},
    {"SSL3","rc4export", SSL_EN_RC4_128_EXPORT40_WITH_MD5},
    {"SSL3","rc2", SSL_EN_RC2_128_CBC_WITH_MD5},
    {"SSL3","rc2export", SSL_EN_RC2_128_CBC_EXPORT40_WITH_MD5},
    /*{"idea", SSL_EN_IDEA_128_CBC_WITH_MD5}, */
    {"SSL3","des", SSL_EN_DES_64_CBC_WITH_MD5},
    {"SSL3","desede3", SSL_EN_DES_192_EDE3_CBC_WITH_MD5},
    {"SSL3","rsa_rc4_128_md5", SSL_RSA_WITH_RC4_128_MD5},
    {"SSL3","rsa_3des_sha", SSL_RSA_WITH_3DES_EDE_CBC_SHA},
    {"SSL3","rsa_des_sha", SSL_RSA_WITH_DES_CBC_SHA},
    {"SSL3","rsa_fips_3des_sha", SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA},
    {"SSL3","rsa_fips_des_sha", SSL_RSA_FIPS_WITH_DES_CBC_SHA},
    {"SSL3","rsa_rc4_40_md5", SSL_RSA_EXPORT_WITH_RC4_40_MD5},
    {"SSL3","rsa_rc2_40_md5", SSL_RSA_EXPORT_WITH_RC2_CBC_40_MD5},
    {"SSL3","rsa_null_md5", SSL_RSA_WITH_NULL_MD5},
    {"TLS","tls_rsa_export1024_with_rc4_56_sha", TLS_RSA_EXPORT1024_WITH_RC4_56_SHA},
    {"TLS","tls_rsa_export1024_with_des_cbc_sha", TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA},
    {"SSL3","fortezza", SSL_FORTEZZA_DMS_WITH_FORTEZZA_CBC_SHA},
    {"SSL3","fortezza_rc4_128_sha", SSL_FORTEZZA_DMS_WITH_RC4_128_SHA},
    {"SSL3","fortezza_null", SSL_FORTEZZA_DMS_WITH_NULL_SHA}, 

	
    /*{"SSL3","dhe_dss_40_sha", SSL_DHE_DSS_EXPORT_WITH_DES40_CBC_SHA}, */
    {"SSL3","dhe_dss_des_sha", SSL_DHE_DSS_WITH_DES_CBC_SHA},
    {"SSL3","dhe_dss_3des_sha", SSL_DHE_DSS_WITH_3DES_EDE_CBC_SHA},
    /*{"SSL3","dhe_rsa_40_sha", SSL_DHE_RSA_EXPORT_WITH_DES40_CBC_SHA}, */
    {"SSL3","dhe_rsa_des_sha", SSL_DHE_RSA_WITH_DES_CBC_SHA},
    {"SSL3","dhe_rsa_3des_sha", SSL_DHE_RSA_WITH_3DES_EDE_CBC_SHA},

    {"TLS","tls_rsa_aes_128_sha", TLS_RSA_WITH_AES_128_CBC_SHA},
    {"TLS","tls_dhe_dss_aes_128_sha", TLS_DHE_DSS_WITH_AES_128_CBC_SHA},
    {"TLS","tls_dhe_rsa_aes_128_sha", TLS_DHE_RSA_WITH_AES_128_CBC_SHA},

    {"TLS","tls_rsa_aes_256_sha", TLS_RSA_WITH_AES_256_CBC_SHA},
    {"TLS","tls_dhe_dss_aes_256_sha", TLS_DHE_DSS_WITH_AES_256_CBC_SHA},
    {"TLS","tls_dhe_rsa_aes_256_sha", TLS_DHE_RSA_WITH_AES_256_CBC_SHA},
    /*{"TLS","tls_dhe_dss_1024_des_sha", TLS_DHE_DSS_EXPORT1024_WITH_DES_CBC_SHA}, */
    {"TLS","tls_dhe_dss_1024_rc4_sha", TLS_RSA_EXPORT1024_WITH_RC4_56_SHA},
    {"TLS","tls_dhe_dss_rc4_128_sha", TLS_DHE_DSS_WITH_RC4_128_SHA},
    {NULL, NULL, 0}
};

char ** getSupportedCiphers()
{
	SSLCipherSuiteInfo info;
	char *sep = "::";
	int number_of_ciphers = sizeof (_conf_ciphers) /sizeof(cipherstruct);
	int i;
	if (cipher_names == NULL ) {
		cipher_names = (char **) slapi_ch_calloc ((number_of_ciphers +1 ) , sizeof(char *));
		for (i = 0 ; _conf_ciphers[i].name != NULL; i++ ) {
			SSL_GetCipherSuiteInfo((PRUint16)_conf_ciphers[i].num,&info,sizeof(info));
			cipher_names[i] = PR_smprintf("%s%s%s%s%s%s%s%s%d\0",_conf_ciphers[i].version,sep,_conf_ciphers[i].name,sep,info.symCipherName,sep,info.macAlgorithmName,sep,info.symKeyBits);
		}
		cipher_names[i] = NULL;
	}
	return cipher_names;
}
void
_conf_setallciphers(int active)
{
    int x;

    /* MLM - change: Because null_md5 is NOT encrypted at all, force
     *       them to activate it by name. */
    for(x = 0; _conf_ciphers[x].name; x++)  {
        if(active && !strcmp(_conf_ciphers[x].name, "rsa_null_md5"))  {
            continue;
        }
        SSL_CipherPrefSetDefault(_conf_ciphers[x].num, active ? PR_TRUE : PR_FALSE);
    }
}

char *
_conf_setciphers(char *ciphers)
{
    char *t, err[MAGNUS_ERROR_LEN];
    int x, active;
	char *raw = ciphers;

    /* Default is to activate all of them */
    if(!ciphers || ciphers[0] == '\0') {
        _conf_setallciphers(1);
        return NULL;
    }
/* Enable all the ciphers by default and the following while loop would disable the user disabled ones This is needed becuase we added a new set of ciphers in the table . Right now there is no support for this from the console */	
    _conf_setallciphers(1);

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

        if(!strcasecmp(ciphers, "all"))
            _conf_setallciphers(active);
        else {
            for(x = 0; _conf_ciphers[x].name; x++) {
                if(!strcasecmp(ciphers, _conf_ciphers[x].name)) {
		  SSL_CipherPrefSetDefault(_conf_ciphers[x].num, active ? PR_TRUE : PR_FALSE);
                    break;
                }
            }
            if(!_conf_ciphers[x].name) {
                PR_snprintf(err, sizeof(err), "unknown cipher %s", ciphers);
                return slapi_ch_strdup(err);
            }
        }
        if(t)
            ciphers = t;
    }
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
    exit(1);
}

void
slapd_SSL_warn(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    slapd_SSL_report(LOG_WARN, fmt, args);
    va_end(args);
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

static void
warn_if_no_cert_file(const char *dir)
{
    char *filename = slapi_ch_smprintf("%s/cert8.db", dir);
	PRStatus status = PR_Access(filename, PR_ACCESS_READ_OK);
	if (PR_SUCCESS != status) {
        slapi_ch_free_string(&filename);
        filename = slapi_ch_smprintf("%s/cert7.db", dir);
        status = PR_Access(filename, PR_ACCESS_READ_OK);
        if (PR_SUCCESS != status) {
            slapi_log_error(SLAPI_LOG_FATAL, "SSL Initialization",
                            "Warning: certificate DB file cert8.db nor cert7.db exists in [%s] - SSL initialization will likely fail\n",
                            dir);
        }
    }
    slapi_ch_free_string(&filename);
}

static void
warn_if_no_key_file(const char *dir)
{
	char *filename = slapi_ch_smprintf("%s/key3.db", dir);
	PRStatus status = PR_Access(filename, PR_ACCESS_READ_OK);
	if (PR_SUCCESS != status) {
        slapi_log_error(SLAPI_LOG_FATAL, "SSL Initialization",
                        "Warning: key DB file %s does not exist - SSL initialization will likely fail\n",
                        filename);
	}
	slapi_ch_free_string(&filename);
}

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
    PRUint32 nssFlags = 0;
	char *certdir;

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
        warn_if_no_cert_file(certdir);
        warn_if_no_key_file(certdir);
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

    /****** end of NSS Initialization ******/

    slapi_ch_free_string(&certdir);
    return rv;
}

/*
 * slapd_ssl_init() is called from main() if we plan to listen
 * on a secure port.
 */
int
slapd_ssl_init() {
    PRErrorCode errorCode;
    char ** family_list;
    char *val = NULL;
    char cipher_string[1024];
    int rv = 0;
    PK11SlotInfo *slot;
#ifndef _WIN32
    SVRCOREStdPinObj *StdPinObj;
#else
    SVRCOREFilePinObj *FilePinObj;
    SVRCOREAltPinObj *AltPinObj;
    SVRCORENTUserPinObj *NTUserPinObj;
#endif
    Slapi_Entry *entry = NULL;

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
      return -1;
    }

    stimeout = atoi(val);
    slapi_ch_free((void **) &val);
    
#ifndef _WIN32
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

    if((family_list = getChildren(configDN))) {
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
		if((!activation) || (!strcasecmp(activation, "off"))) {
			/* this family was turned off, goto next */
			slapi_ch_free((void **) &activation);
			continue;
		}

		slapi_ch_free((void **) &activation);

		token = slapi_entry_attr_get_charptr( entry, "nsssltoken" );
                if( token ) {
                        if( !strcasecmp(token, "internal") ||
                            !strcasecmp(token, "internal (software)"))
    				slot = slapd_pk11_getInternalKeySlot();
     			else
    				slot = slapd_pk11_findSlotByName(token);
    		} else {
		        errorCode = PR_GetError();
      			slapd_SSL_warn("Security Initialization: Unable to get token ("
				       SLAPI_COMPONENT_NAME_NSPR " error %d - %s)", 
				       errorCode, slapd_pr_strerror(errorCode));
      			return -1;
		}

		slapi_ch_free((void **) &token);

		if (!slot) {
		        errorCode = PR_GetError();
      			slapd_SSL_warn("Security Initialization: Unable to find slot ("
				       SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
				       errorCode, slapd_pr_strerror(errorCode));
      			return -1;
    		}
    		/* authenticate */
    		if(slapd_pk11_authenticate(slot, PR_TRUE, NULL) != SECSuccess)
    		{
		        errorCode = PR_GetError();
      			slapd_SSL_warn("Security Initialization: Unable to authenticate ("
				       SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
				       errorCode, slapd_pr_strerror(errorCode));
      			return -1;
    		}
    	}
	freeChildren( family_list );
    }
	freeConfigEntry( &entry );

    if(SSLPLCY_Install() != PR_SUCCESS) {
        errorCode = PR_GetError();
	slapd_SSL_warn("Security Initialization: Unable to set SSL export policy ("
		       SLAPI_COMPONENT_NAME_NSPR " error %d - %s)", 
		       errorCode, slapd_pr_strerror(errorCode));
	return -1;
    }


    /* ugaston- Cipher preferences must be set before any sslSocket is created
     * for such sockets to take preferences into account.
     */

    /* Step Three.5: Set SSL cipher preferences */
    *cipher_string = 0;
    if(ciphers && (*ciphers) && strcmp(ciphers, "blank"))
         PL_strncpyz(cipher_string, ciphers, sizeof(cipher_string));
    slapi_ch_free((void **) &ciphers);

    if( NULL != (val = _conf_setciphers(cipher_string)) ) {
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


    if ( rv != 0 )
	  return rv;


    return 0;

}


int slapd_ssl_init2(PRFileDesc **fd, int startTLS)
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

    /* turn off the PKCS11 pin interactive mode */
#ifndef _WIN32
    SVRCOREStdPinObj *StdPinObj;

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
            if((!activation) || (!strcasecmp(activation, "off"))) {
                /* this family was turned off, goto next */
                slapi_ch_free((void **) &activation);
                freeConfigEntry( &e );
                continue;
            }

            slapi_ch_free((void **) &activation);

            token = slapi_entry_attr_get_charptr( e, "nsssltoken" );
            personality = slapi_entry_attr_get_charptr( e, "nssslpersonalityssl" );
            if( token && personality ) {
                if( !strcasecmp(token, "internal") ||
                    !strcasecmp(token, "internal (software)") )
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
                rv = CERT_VerifyCertificateNow(
                                    CERT_GetDefaultCertDB(), cert, PR_TRUE,
                                    certificateUsageSSLServer, 
                                    SSL_RevealPinArg(pr_sock),
                                    &returnedUsages);
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
                } else {
                    /* verify certificate failed */
                    /* If the common name in the subject DN for the certificate
                     * is not identical to the domain name passed in the 
                     * hostname parameter, SECFailure. */
                    errorCode = PR_GetError();
                    slapd_SSL_warn("CERT_VerifyCertificateNow: "
                                   "verify certificate failed "
                                   "for cert %s of family %s ("
                                   SLAPI_COMPONENT_NAME_NSPR
                                   " error %d - %s)",
                                   cert_name, *family, errorCode, 
                                   slapd_pr_strerror(errorCode));
                }
            }
            if (cert) {
                CERT_DestroyCertificate(cert);
                cert = NULL;
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

    sslStatus = SSL_OptionSet(pr_sock, SSL_ENABLE_SSL3, PR_TRUE);
    if (sslStatus != SECSuccess) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Failed to enable SSLv3 "
               "on the imported socket (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
               errorCode, slapd_pr_strerror(errorCode));
    }

    sslStatus = SSL_OptionSet(pr_sock, SSL_ENABLE_TLS, PR_TRUE);
    if (sslStatus != SECSuccess) {
        errorCode = PR_GetError();
        slapd_SSL_warn("Security Initialization: Failed to enable TLS "
               "on the imported socket (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
               errorCode, slapd_pr_strerror(errorCode));
    }
/* Explicitly disabling SSL2 - NGK */
    sslStatus = SSL_OptionSet(pr_sock, SSL_ENABLE_SSL2, PR_FALSE);

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
	SSL_OptionSetDefault(SSL_ENABLE_SSLdirs
3, PR_TRUE);
	s = NSS_SetDomesticPolicy(); 
We already do pr_init, we don't need pr_setconcurrency, we already do nss_init and the rest

*/   

int
slapd_SSL_client_init()
{
    return 0;
}

static int
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
            if((!activation) || (!strcasecmp(activation, "off"))) {
                    /* this family was turned off, goto next */
					slapi_ch_free((void **) &activation);
					freeConfigEntry( &entry );
                    continue;
            }

	    slapi_ch_free((void **) &activation);

            personality = slapi_entry_attr_get_charptr( entry, "nssslpersonalityssl" );
            cipher = slapi_entry_attr_get_charptr( entry, "cn" );
	    if ( cipher && !strcasecmp(cipher, "RSA" )) {
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
			  if( !strcasecmp(ssltoken, "internal") ||
			      !strcasecmp(ssltoken, "internal (software)") ) {

						/* Translate config internal name to more
			 			 * readable form.  Certificate name is just
			 			 * the personality for internal tokens.
			 			 */
						token = slapi_ch_strdup(internalTokenName);
						PL_strncpyz(cert_name, personality, sizeof(cert_name));
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

	/* We cannot allow NSS to cache outgoing client auth connections -
	   each client auth connection must have it's own non-shared SSL
	   connection to the peer so that it will go through the
	   entire handshake protocol every time including the use of its
	   own unique client cert - see bug 605457
	*/

	ldapssl_set_option(ld, SSL_NO_CACHE, PR_TRUE);

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
	rc = ldapssl_enable_clientauth (ld, SERVER_KEY_NAME, pw, cert_name);
	if (rc != 0) {
	    errorCode = PR_GetError();
	    slapd_SSL_warn("ldapssl_enable_clientauth(%s, %s) %i ("
				SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
			    SERVER_KEY_NAME, cert_name, rc, 
			    errorCode, slapd_pr_strerror(errorCode));
	}
    }

    if (token) slapi_ch_free((void**)&token);
    slapi_ch_free((void**)&pw);

    LDAPDebug (LDAP_DEBUG_TRACE, "slapd_SSL_client_auth() %i\n", rc, 0, 0);
    return rc;
}

int
slapd_simple_client_bind_s(LDAP* ld, char* DN, char* pw, int LDAPv)
{
    int rc;
    PRErrorCode errorCode;

    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, (void *) &LDAPv);
    rc = ldap_simple_bind_s (ld, DN, pw);
    if (rc != 0) {
      errorCode = PR_GetError();
      slapd_SSL_warn("ldap_simple_bind_s(%s, %s) %i (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
		     DN, pw, rc, errorCode, slapd_pr_strerror(errorCode));
    }
    LDAPDebug (LDAP_DEBUG_TRACE, "slapd_simple_client_bind_s(%s, %i) %i\n", DN, LDAPv, rc);
    return rc;
}

int
slapd_SSL_client_bind_s (LDAP* ld, char* DN, char* pw, int use_SSL, int LDAPv)
{
    int rc;
    struct berval noCred = {0, 0};

    if (!use_SSL || LDAPv == LDAP_VERSION2) {
		rc = slapd_simple_client_bind_s(ld, DN, pw, LDAPv);
    } else {
      
                LDAPDebug (
			   LDAP_DEBUG_TRACE,
			   "slapd_SSL_client_bind_s: Trying SSL Client Authentication\n",
			   0, 0, 0);
		
		rc = slapd_SSL_client_auth(ld);
		
		if(rc != 0)
		{
		        LDAPDebug (
				   LDAP_DEBUG_TRACE,
				   "slapd_SSL_client_bind_s: SSL Client Auth Failed during replication Bind\n",
				   0, 0, 0);
			return rc;
		}
									      
		rc = ldap_sasl_bind_s (ld, "", LDAP_SASL_EXTERNAL, &noCred,
				       NULL /* LDAPControl **serverctrls */,
				       NULL /* LDAPControl **clientctrls */,
				       NULL /* struct berval **servercredp */);		

    }
    LDAPDebug (
	       LDAP_DEBUG_TRACE,
	       "slapd_SSL_client_bind_s(%i,%i) %i\n", use_SSL, LDAPv, rc);
    return rc;
}

int
slapd_sasl_ext_client_bind (LDAP* ld, int **msgid)
{
    int rc;
    PRErrorCode errorCode;
    struct berval noCred = {0, 0};

	LDAPDebug (
		LDAP_DEBUG_TRACE,
		"slapd_sasl_ext_client_bind: Trying SSL Client Authentication\n",
		0, 0, 0);
		
	rc = slapd_SSL_client_auth(ld);
	if(rc != 0)
	{
		LDAPDebug (
			LDAP_DEBUG_TRACE,
			"slapd_sasl_ext_client_bind: SSL Client Auth Failed during replication Bind\n",
			0, 0, 0);
		return rc;
	}
										  
	rc = ldap_sasl_bind (ld, "", LDAP_SASL_EXTERNAL, &noCred,
		NULL, 
		NULL,
		*msgid);		
	if (rc != 0) {
	        errorCode = PR_GetError();
		slapd_SSL_warn("ldap_sasl_bind(\"\",LDAP_SASL_EXTERNAL) %i (" SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
			       rc, errorCode, slapd_pr_strerror(errorCode));
	}
	
	LDAPDebug (
		LDAP_DEBUG_TRACE,
		"slapd_sasl_ext_client_bind %i\n", rc, 0, 0);

	return rc;
}


int slapd_Client_auth(LDAP* ld)
{
	int rc=0;

	rc = slapd_SSL_client_auth (ld);

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

/* memory to store tmpdir is allocated and returned; caller should free it. */
char* slapd_get_tmp_dir()
{
	static char tmp[MAXPATHLEN];
	char* tmpdir = NULL;;
#if defined( XP_WIN32 )
	unsigned ilen;
	char pch;
#endif
	struct stat ffinfo;

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

	if(stat(tmpdir, &ffinfo) == -1)
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
			slapi_log_error(
			 SLAPI_LOG_FATAL,
			 "slapd_get_tmp_dir",
			 "mkdir(%s, 00770) Error: %s\n",
			 tmpdir, strerror(errno));	
		}
#endif
	return ( tmpdir );
}
