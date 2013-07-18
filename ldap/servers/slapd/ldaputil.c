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
 *
 * mozldap_ldap_explode, mozldap_ldap_explode_dn, mozldap_ldap_explode_rdn
 * are from the file ldap/libraries/libldap/getdn.c in the Mozilla LDAP C SDK
 *
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 * 
 * The contents of this file are subject to the Mozilla Public License Version 
 * 1.1 (the "License"); you may not use this file except in compliance with 
 * the License. You may obtain a copy of the License at 
 * http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 * 
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 * 
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998-1999
 * the Initial Developer. All Rights Reserved.
 * 
 * Contributor(s):
 * 
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 * 
 *  Copyright (c) 1994 Regents of the University of Michigan.
 *  All rights reserved.
 *
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* ldaputil.c   -- LDAP utility functions and wrappers */
#ifdef _WIN32
#include <direct.h> /* for getcwd */
#else
#include <sys/socket.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#endif
#include <libgen.h>
#include <pk11func.h>
#include "slap.h"
#include "prtime.h"
#include "prinrval.h"
#include "snmp_collator.h"
#if !defined(USE_OPENLDAP)
#include <ldap_ssl.h>
#include <ldappr.h>
#else
/* need mutex around ldap_initialize - see https://fedorahosted.org/389/ticket/348 */
static PRCallOnceType ol_init_callOnce = {0,0};
static PRLock *ol_init_lock = NULL;

static PRStatus
internal_ol_init_init(void)
{
    PR_ASSERT(NULL == ol_init_lock);
    if ((ol_init_lock = PR_NewLock()) == NULL) {
        PRErrorCode errorCode = PR_GetError();
        slapi_log_error(SLAPI_LOG_FATAL, "internal_ol_init_init", "PR_NewLock failed %d:%s\n",
                        errorCode, slapd_pr_strerror(errorCode));
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}
#endif

/* the server depends on the old, deprecated ldap_explode behavior which openldap
   does not support - the use of the mozldap code should be discouraged as
   there are issues that mozldap does not handle correctly. */
static char **mozldap_ldap_explode( const char *dn, const int notypes, const int nametype );
static char **mozldap_ldap_explode_dn( const char *dn, const int notypes );
static char **mozldap_ldap_explode_rdn( const char *rdn, const int notypes );

#ifdef HAVE_KRB5
static void clear_krb5_ccache();
#endif

#ifdef MEMPOOL_EXPERIMENTAL
void _free_wrapper(void *ptr)
{
    slapi_ch_free(&ptr);
}
#endif

/*
 * Function: slapi_ldap_unbind()
 * Purpose: release an LDAP session obtained from a call to slapi_ldap_init().
 */
void
slapi_ldap_unbind( LDAP *ld )
{
    if ( ld != NULL ) {
	ldap_unbind_ext( ld, NULL, NULL );
    }
}

#if defined(USE_OPENLDAP)
/* mozldap ldap_init and ldap_url_parse accept a hostname in the form
   host1[:port1]SPACEhost2[:port2]SPACEhostN[:portN]
   where SPACE is a single space (0x20) character
   for openldap, we have to convert this to a string like this:
   PROTO://host1[:port1]/SPACEPROTO://host2[:port2]/SPACEPROTO://hostN[:portN]/
   where PROTO is ldap or ldaps or ldapi
   if proto is NULL, assume hostname_or_uri is really a valid ldap uri
*/
static char *
convert_to_openldap_uri(const char *hostname_or_uri, int port, const char *proto)
{
    char *retstr = NULL;
    char *my_copy = NULL;
    char *start = NULL;
    char *iter = NULL;
    char *ptr = NULL;
    char *s = NULL;
    const char *brkstr = " ";
    int done = 0;

    if (!hostname_or_uri) {
	    return NULL;
    }

    if(slapi_is_ipv6_addr(hostname_or_uri)){
        /* We need to encapsulate the ipv6 addr with brackets  */
        my_copy = slapi_ch_smprintf("[%s]",hostname_or_uri);
    } else {
        my_copy = slapi_ch_strdup(hostname_or_uri);
    }

    /* see if hostname_or_uri is an ldap uri */
    if (!proto && !PL_strncasecmp(my_copy, "ldap", 4)) {
        start = my_copy + 4;
        if ((*start == 's') || (*start == 'i')) {
            start++;
        }
        if (!PL_strncmp(start, "://", 3)) {
            *start = '\0';
            proto = my_copy;
            start += 3;
        } else {
            slapi_log_error(SLAPI_LOG_FATAL, "convert_to_openldap_uri",
                "The given LDAP URI [%s] is not valid\n", hostname_or_uri);
            goto end;
        }
    } else if (!proto) {
	    slapi_log_error(SLAPI_LOG_FATAL, "convert_to_openldap_uri",
            "The given LDAP URI [%s] is not valid\n", hostname_or_uri);
        goto end;
    } else {
        start = my_copy; /* just assume it's not a uri */
    }
	    
    for (s = ldap_utf8strtok_r(my_copy, brkstr, &iter); s != NULL; s = ldap_utf8strtok_r(NULL, brkstr, &iter)) {
        /* strtok will grab the '/' at the end of the uri, if any,  so terminate parsing there */
        if ((ptr = strchr(s, '/'))) {
            *ptr = '\0';
            done = 1;
        }
        if (retstr) {
            retstr = PR_sprintf_append(retstr, "/ %s://%s", proto, s);
        } else {
            retstr = PR_smprintf("%s://%s", proto, s);
        }
        if (done) {
            break;
        }
    }

    /* add the port on the last one */
    retstr = PR_sprintf_append(retstr, ":%d/", port);
end:
    slapi_ch_free_string(&my_copy);
    return retstr;    
}
#endif /* USE_OPENLDAP */

const char *
slapi_urlparse_err2string( int err )
{
    const char *s="internal error";

    switch( err ) {
    case 0:
	s = "no error";
	break;
    case LDAP_URL_ERR_BADSCOPE:
	s = "invalid search scope";
	break;
    case LDAP_URL_ERR_MEM:
	s = "unable to allocate memory";
	break;
    case LDAP_URL_ERR_PARAM:
	s = "bad parameter to an LDAP URL function";
	break;
#if defined(USE_OPENLDAP)
    case LDAP_URL_ERR_BADSCHEME:
	s = "does not begin with ldap://, ldaps://, or ldapi://";
	break;
    case LDAP_URL_ERR_BADENCLOSURE:
	s = "missing trailing '>' in enclosure";
	break;
    case LDAP_URL_ERR_BADURL:
	s = "not a valid LDAP URL";
	break;
    case LDAP_URL_ERR_BADHOST:
	s = "hostname part of url is not valid or not given";
	break;
    case LDAP_URL_ERR_BADATTRS:
	s = "attribute list not formatted correctly or missing";
	break;
    case LDAP_URL_ERR_BADFILTER:
	s = "search filter not correct";
	break;
    case LDAP_URL_ERR_BADEXTS:
	s = "extensions not specified correctly";
	break;
#else /* !USE_OPENLDAP */
    case LDAP_URL_ERR_NOTLDAP:
	s = "missing ldap:// or ldaps:// or ldapi://";
	break;
    case LDAP_URL_ERR_NODN:
	s = "missing suffix";
	break;
#endif
    }

    return( s );
}

/* there are various differences among url parsers - directory server
   needs the ability to parse partial URLs - those with no dn - and
   needs to be able to tell if it is a secure url (ldaps) or not */
int
slapi_ldap_url_parse(const char *url, LDAPURLDesc **ludpp, int require_dn, int *secure)
{
    PR_ASSERT(url);
    PR_ASSERT(ludpp);
    int rc;
    const char *url_to_use = url;
#if defined(USE_OPENLDAP)
    char *urlescaped = NULL;
#endif

    if (secure) {
        *secure = 0;
    }
#if defined(USE_OPENLDAP)
    /* openldap does not support the non-standard multi host:port URLs supported
       by mozldap - so we have to fake out openldap - replace all spaces with %20 -
       replace all but the last colon with %3A
       Go to the 3rd '/' or to the end of the string (convert only the host:port part) */
    if (url) {
        char *p = strstr(url, "://");
        if (p) {
            int foundspace = 0;
            int coloncount = 0;
            char *lastcolon = NULL;

            p += 3;
            for (; *p && (*p != '/'); p++) {
                if (*p == ' ') {
                    foundspace = 1;
                }
                if (*p == ':') {
                    coloncount++;
                    lastcolon = p;
                }
            }
            if (foundspace) {
                char *src = NULL, *dest = NULL;

                /* have to convert url, len * 3 is way too much, but acceptable */
                urlescaped = slapi_ch_calloc(strlen(url) * 3, sizeof(char));
                dest = urlescaped;
                /* copy the scheme */
                src = strstr(url, "://");
                src += 3;
                memcpy(dest, url, src-url);
                dest += (src-url);
                /*
                 * we have to convert all spaces to %20 - we have to convert
                 * all colons except the last one to %3A
                 */
                for (; *src; ++src) {
                    if (src < p) {
                        if (*src == ' ') {
                            memcpy(dest, "%20", 3);
                            dest += 3;
                        } else if ((coloncount > 1) && (*src == ':') && (src != lastcolon)) {
                            memcpy(dest, "%3A", 3);
                            dest += 3;
                        } else {
                            *dest++ = *src;
                        }
                    } else {
                        *dest++ = *src;
                    }
                }
                *dest = '\0';
                url_to_use = urlescaped;
            }
        }
    }
#endif

#if defined(HAVE_LDAP_URL_PARSE_NO_DEFAULTS)
    rc = ldap_url_parse_no_defaults(url_to_use, ludpp, require_dn);
    if (!rc && *ludpp && secure) {
        *secure = (*ludpp)->lud_options & LDAP_URL_OPT_SECURE;
    }
#else /* openldap */
#if defined(HAVE_LDAP_URL_PARSE_EXT) && defined(LDAP_PVT_URL_PARSE_NONE) && defined(LDAP_PVT_URL_PARSE_NOEMPTY_DN)
    rc = ldap_url_parse_ext(url_to_use, ludpp, require_dn ? LDAP_PVT_URL_PARSE_NONE : LDAP_PVT_URL_PARSE_NOEMPTY_DN);
#else
    rc = ldap_url_parse(url_to_use, ludpp);
    if ((rc || !*ludpp) && !require_dn) { /* failed - see if failure was due to missing dn */
        size_t len = strlen(url_to_use);
        /* assume the url is just scheme://host:port[/] - add the empty string
           as the DN (adding a trailing / first if needed) and try to parse
           again
        */
        char *urlcopy = slapi_ch_smprintf("%s%s%s", url_to_use, (url_to_use[len-1] == '/' ? "" : "/"), "");
        if (*ludpp) {
            ldap_free_urldesc(*ludpp); /* free the old one, if any */
        }
        rc = ldap_url_parse(urlcopy, ludpp);
        slapi_ch_free_string(&urlcopy);
        if (0 == rc) { /* only problem was the DN - free it */
            slapi_ch_free_string(&((*ludpp)->lud_dn));
        }
    }
#endif
    if (!rc && *ludpp && secure) {
        *secure = (*ludpp)->lud_scheme && !strcmp((*ludpp)->lud_scheme, "ldaps");
    }
#endif /* openldap */

#if defined(USE_OPENLDAP)
    if (urlescaped && (*ludpp) && (*ludpp)->lud_host) {
        /* have to unescape lud_host - can unescape in place */
        char *p = strstr((*ludpp)->lud_host, "://");
        if (p) {
            char *dest = NULL;
            p += 3;
            dest = p;
            /* up to the first '/', unescape the host */
            for (; *p && (*p != '/'); p++) {
                if (!strncmp(p, "%20", 3)) {
                    *dest++ = ' ';
                    p += 2;
                } else if (!strncmp(p, "%3A", 3)) {
                    *dest++ = ':';
                    p += 2;
                } else {
                    *dest++ = *p;
                }
            }
            /* just copy the remainder of the host, if any */
            while (*p) {
                *dest++ = *p++;
            }
            *dest = '\0';
        }
    }
    slapi_ch_free_string(&urlescaped);
#endif
    return rc;
}

#include <sasl.h>

int
slapi_ldap_get_lderrno(LDAP *ld, char **m, char **s)
{
    int rc = LDAP_SUCCESS;

#if defined(USE_OPENLDAP)
    ldap_get_option(ld, LDAP_OPT_RESULT_CODE, &rc);
    if (m) {
        ldap_get_option(ld, LDAP_OPT_MATCHED_DN, m);
    }
    if (s) {
#ifdef LDAP_OPT_DIAGNOSTIC_MESSAGE
        ldap_get_option(ld, LDAP_OPT_DIAGNOSTIC_MESSAGE, s);
#else
        ldap_get_option(ld, LDAP_OPT_ERROR_STRING, s);
#endif
    }
#else /* !USE_OPENLDAP */
    rc = ldap_get_lderrno( ld, m, s );
#endif
    return rc;
}

void
slapi_ldif_put_type_and_value_with_options( char **out, const char *t, const char *val, int vlen, unsigned long options )
{
#if defined(USE_OPENLDAP)
	/* openldap always wraps and always does conservative base64 encoding
	   we unwrap here, but clients will have to do their own base64 decode */
    int type = LDIF_PUT_VALUE;
    char *save = *out;

    if (options & LDIF_OPT_VALUE_IS_URL) {
        type = LDIF_PUT_URL;
    }
    ldif_sput( out, type, t, val, vlen );
    if (options & LDIF_OPT_NOWRAP) {
        /* modify out in place, stripping out continuation lines */
        char *src = save;
        char *dest = save;
        for (; src < *out; ++src) {
            if ((src < (*out - 2)) && !strncmp(src, "\n ", 2)) {
                src += 2; /* skip continuation */
            }
            *dest++ = *src;
        }
        *out = dest; /* move 'out' back if we removed some continuation lines */
    }
#else
    ldif_put_type_and_value_with_options( out, (char *)t, (char *)val, vlen, options );
#endif
}

void
slapi_ldap_value_free( char **vals )
{
#if defined(USE_OPENLDAP)
    slapi_ch_array_free(vals);
#else
    ldap_value_free(vals);
#endif
}

int
slapi_ldap_count_values( char **vals )
{
#if defined(USE_OPENLDAP)
    return ldap_count_values_len((struct berval **)vals);
#else
    return ldap_count_values(vals);
#endif
}

int
slapi_ldap_create_proxyauth_control (
    LDAP *ld, /* only used to get current ber options */
    const char *dn, /* proxy dn */
    const char ctl_iscritical,
    int usev2, /* use the v2 (.18) control instead */
    LDAPControl **ctrlp /* value to return */
)
{
    int rc = 0;
#if defined(USE_OPENLDAP)
    BerElement *ber = NULL;
    int beropts = 0;
    char *berfmtstr = NULL;
    char *ctrloid = NULL;
    struct berval *bv = NULL;

    /* note - there is currently no way to get the beroptions from the ld*,
       so we just hard code it here */
    beropts = LBER_USE_DER; /* openldap seems to use DER by default */
    if (ctrlp == NULL) {
	return LDAP_PARAM_ERROR;
    }
    if (NULL == dn) {
	dn = "";
    }

    if (NULL == (ber = ber_alloc_t(beropts))) {
	return LDAP_NO_MEMORY;
    }

    if (usev2) {
	berfmtstr = "s";
	ctrloid = LDAP_CONTROL_PROXIEDAUTH;
    } else {
	berfmtstr = "{s}";
	ctrloid = LDAP_CONTROL_PROXYAUTH;
    }

    if (LBER_ERROR == ber_printf(ber, berfmtstr, dn)) {
	ber_free(ber, 1);
	return LDAP_ENCODING_ERROR;
    }

    if (LBER_ERROR == ber_flatten(ber, &bv)) {
        ber_bvfree(bv);
        ber_free(ber, 1);
	return LDAP_ENCODING_ERROR;
    }
	
    if (NULL == bv) {
        ber_free(ber, 1);
        return LDAP_NO_MEMORY;
    }

    rc = ldap_control_create(ctrloid, ctl_iscritical, bv, 1, ctrlp);
    ber_bvfree(bv);
    ber_free(ber, 1);
#else
    if (usev2) {
        rc = ldap_create_proxiedauth_control(ld, dn, ctrlp);
    } else {
        rc = ldap_create_proxyauth_control(ld, dn, ctl_iscritical, ctrlp);
    }
#endif
    return rc;
}

int
slapi_ldif_parse_line(
    char *line,
    struct berval *type,
    struct berval *value,
    int *freeval
)
{
    int rc;
#if defined(USE_OPENLDAP)
    rc = ldif_parse_line2(line, type, value, freeval);
    /* check that type and value are null terminated */
#else
    int vlen;
    rc = ldif_parse_line(line, &type->bv_val, &value->bv_val, &vlen);
    type->bv_len = type->bv_val ? strlen(type->bv_val) : 0;
    value->bv_len = vlen;
    *freeval = 0; /* always returns in place */
#endif
    return rc;
}

#if defined(USE_OPENLDAP)
static int
setup_ol_tls_conn(LDAP *ld, int clientauth)
{
    char *certdir = config_get_certdir();
    int optval = 0;
    int ssl_strength = 0;
    int rc = 0;

    if (config_get_ssl_check_hostname()) {
	ssl_strength = LDAP_OPT_X_TLS_HARD;
    } else {
	/* verify certificate only */
	ssl_strength = LDAP_OPT_X_TLS_NEVER;
    }

    if ((rc = ldap_set_option(ld, LDAP_OPT_X_TLS_REQUIRE_CERT, &ssl_strength))) {
	slapi_log_error(SLAPI_LOG_FATAL, "setup_ol_tls_conn",
			"failed: unable to set REQUIRE_CERT option to %d\n", ssl_strength);
    }
    /* tell it where our cert db is */
    if ((rc = ldap_set_option(ld, LDAP_OPT_X_TLS_CACERTDIR, certdir))) {
	slapi_log_error(SLAPI_LOG_FATAL, "setup_ol_tls_conn",
			"failed: unable to set CACERTDIR option to %s\n", certdir);
    }
    slapi_ch_free_string(&certdir);
#if defined(LDAP_OPT_X_TLS_PROTOCOL_MIN)
    optval = LDAP_OPT_X_TLS_PROTOCOL_SSL3;
    if ((rc = ldap_set_option(ld, LDAP_OPT_X_TLS_PROTOCOL_MIN, &optval))) {
	slapi_log_error(SLAPI_LOG_FATAL, "setup_ol_tls_conn",
			"failed: unable to set minimum TLS protocol level to SSL3\n");
    }
#endif /* LDAP_OPT_X_TLS_PROTOCOL_MIN */
    if (clientauth) {
	rc = slapd_SSL_client_auth(ld);
	if (rc) {
	    slapi_log_error(SLAPI_LOG_FATAL, "setup_ol_tls_conn",
			    "failed: unable to setup connection for TLS/SSL EXTERNAL client cert authentication - %d\n", rc);
	}
    }

    /* have to do this last - this creates the new TLS handle and sets/copies
       all of the parameters set above into that TLS handle context - note
       that optval is ignored - what matters is that it is not NULL */
    if ((rc = ldap_set_option(ld, LDAP_OPT_X_TLS_NEWCTX, &optval))) {
	slapi_log_error(SLAPI_LOG_FATAL, "setup_ol_tls_conn",
			"failed: unable to create new TLS context\n");
    }

    return rc;
}
#endif /* defined(USE_OPENLDAP) */

/*
  Perform LDAP init and return an LDAP* handle.  If ldapurl is given,
  that is used as the basis for the protocol, host, port, and whether
  to use starttls (given on the end as ldap://..../?????starttlsOID
  If hostname is given, LDAP or LDAPS is assumed, and this will override
  the hostname from the ldapurl, if any.  If port is > 0, this is the
  port number to use.  It will override the port in the ldapurl, if any.
  If no port is given in port or ldapurl, the default will be used based
  on the secure setting (389 for ldap, 636 for ldaps, 389 for starttls)
  secure takes 1 of 3 values - 0 means regular ldap, 1 means ldaps, 2
  means regular ldap with starttls.
  filename is the ldapi file name - if this is given, and no other options
  are given, ldapi is assumed.
 */
/* util_sasl_path: the string argument for putenv.
   It must be a global or a static */
char util_sasl_path[MAXPATHLEN];

LDAP *
slapi_ldap_init_ext(
    const char *ldapurl, /* full ldap url */
    const char *hostname, /* can also use this to override
			     host in url */
    int port, /* can also use this to override port in url */
    int secure, /* 0 for ldap, 1 for ldaps, 2 for starttls -
		   override proto in url */
    int shared, /* if true, LDAP* will be shared among multiple threads */
    const char *filename /* for ldapi */
)
{
    LDAPURLDesc	*ludp = NULL;
    LDAP *ld = NULL;
    int rc = 0;
    int secureurl = 0;
    int ldap_version3 = LDAP_VERSION3;

    /* We need to provide a sasl path used for client connections, especially
       if the server is not set up to be a sasl server - since mozldap provides
       no way to override the default path programatically, we set the sasl
       path to the environment variable SASL_PATH. */
    char *configpluginpath = config_get_saslpath();
    char *pluginpath = configpluginpath;
    char *pp = NULL;

    if (NULL == pluginpath || (*pluginpath == '\0')) {
	    slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_init_ext",
			"configpluginpath == NULL\n");
        if (!(pluginpath = getenv("SASL_PATH"))) {
#if defined(LINUX) && defined(__LP64__)
            pluginpath = "/usr/lib64/sasl2";
#else
            pluginpath = "/usr/lib/sasl2";
#endif
        }
    }
    if ('\0' == util_sasl_path[0] || /* first time */
        NULL == (pp = strchr(util_sasl_path, '=')) || /* invalid arg for putenv */
        (0 != strcmp(++pp, pluginpath)) /* sasl_path has been updated */ )
    {
        PR_snprintf(util_sasl_path, sizeof(util_sasl_path), "SASL_PATH=%s", pluginpath);
	    slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_init_ext", "putenv(%s)\n", util_sasl_path);
        putenv(util_sasl_path);
    }
    slapi_ch_free_string(&configpluginpath);

    /* if ldapurl is given, parse it */
    if (ldapurl && ((rc = slapi_ldap_url_parse(ldapurl, &ludp, 0, &secureurl)) || !ludp)) {
        slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
            "Could not parse given LDAP URL [%s] : error [%s]\n",
            ldapurl, /* ldapurl cannot be NULL here */
            slapi_urlparse_err2string(rc));
        goto done;
    }

    /* use url host if no host given */
    if (!hostname && ludp && ludp->lud_host) {
        hostname = ludp->lud_host;
    }

    /* use url port if no port given */
    if (!port && ludp && ludp->lud_port) {
        port = ludp->lud_port;
    }

    /* use secure setting from url if none given */
    if (!secure && ludp) {
        if (secureurl) {
            secure = 1;
        } else if (0/* starttls option - not supported yet in LDAP URLs */) {
            secure = 2;
        }
    }

    /* ldap_url_parse doesn't yet handle ldapi */
    /*
      if (!filename && ludp && ludp->lud_file) {
      filename = ludp->lud_file;
      }
    */

#ifdef MEMPOOL_EXPERIMENTAL
    {
        /*
         * slapi_ch_malloc functions need to be set to LDAP C SDK
         */
        struct ldap_memalloc_fns memalloc_fns;
        memalloc_fns.ldapmem_malloc = (LDAP_MALLOC_CALLBACK *)slapi_ch_malloc;
        memalloc_fns.ldapmem_calloc = (LDAP_CALLOC_CALLBACK *)slapi_ch_calloc;
        memalloc_fns.ldapmem_realloc = (LDAP_REALLOC_CALLBACK *)slapi_ch_realloc;
        memalloc_fns.ldapmem_free = (LDAP_FREE_CALLBACK *)_free_wrapper;
    }
    /* 
     * MEMPOOL_EXPERIMENTAL: 
     * These LDAP C SDK init function needs to be revisited.
     * In ldap_init called via ldapssl_init and prldap_init initializes
     * options and set default values including memalloc_fns, then it
     * initializes as sasl client by calling sasl_client_init.  In
     * sasl_client_init, it creates mechlist using the malloc function
     * available at the moment which could mismatch the malloc/free functions
     * set later.
     */
#endif

#if defined(USE_OPENLDAP)
    if (ldapurl) {
        if (PR_SUCCESS != PR_CallOnce(&ol_init_callOnce, internal_ol_init_init)) {
            slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
               "Could not perform internal ol_init init\n");
            rc = -1;
            goto done;
        }

        PR_Lock(ol_init_lock);
        rc = ldap_initialize(&ld, ldapurl);
        PR_Unlock(ol_init_lock);
        if (rc) {
            slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
               "Could not initialize LDAP connection to [%s]: %d:%s\n",
               ldapurl, rc, ldap_err2string(rc));
            goto done;
        }
    } else {
        char *makeurl = NULL;

        if (filename) {
            makeurl = slapi_ch_smprintf("ldapi://%s/", filename);
        } else { /* host port */
            makeurl = convert_to_openldap_uri(hostname, port, (secure == 1 ? "ldaps" : "ldap"));
        }
        if (PR_SUCCESS != PR_CallOnce(&ol_init_callOnce, internal_ol_init_init)) {
            slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
                "Could not perform internal ol_init init\n");
            rc = -1;
            goto done;
        }

        PR_Lock(ol_init_lock);
        rc = ldap_initialize(&ld, makeurl);
        PR_Unlock(ol_init_lock);
        if (rc) {
            slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
                "Could not initialize LDAP connection to [%s]: %d:%s\n",
                makeurl, rc, ldap_err2string(rc));
            slapi_ch_free_string(&makeurl);
            goto done;
        }
        slapi_ch_free_string(&makeurl);
    }

    if(config_get_connection_nocanon()){
        /*
         * The NONCANON flag tells openldap to use the hostname specified in
         * the ldap_initialize command, rather than looking up the
         * hostname using gethostname or similar - this allows running
         * sasl/gssapi tests on machines that don't have a canonical
         * hostname (such as localhost.localdomain).
         */
        if((rc = ldap_set_option(ld, LDAP_OPT_X_SASL_NOCANON, LDAP_OPT_ON))){
        	slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
                "Could not set ldap option LDAP_OPT_X_SASL_NOCANON for (%s), error %d (%s)\n",
                ldapurl, rc, ldap_err2string(rc) );
        }
    }
#else /* !USE_OPENLDAP */
    if (filename) {
        /* ldapi in mozldap client is not yet supported */
    } else if (secure == 1) {
        ld = ldapssl_init(hostname, port, secure);
    } else { /* regular ldap and/or starttls */
        /*
         * Leverage the libprldap layer to take care of all the NSPR
         * integration.
         * Note that ldapssl_init() uses libprldap implicitly.
         */
        ld = prldap_init(hostname, port, shared);
    }
#endif /* !USE_OPENLDAP */

    /* must explicitly set version to 3 */
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &ldap_version3);

    /* Update snmp interaction table */
    if (hostname) {
        if (ld == NULL) {
            set_snmp_interaction_row((char *)hostname, port, -1);
        } else {
            set_snmp_interaction_row((char *)hostname, port, 0);
        }
    }

    if ((ld != NULL) && !filename) {
        /*
         * Set the outbound LDAP I/O timeout based on the server config.
         */
        int io_timeout_ms = config_get_outbound_ldap_io_timeout();

        if (io_timeout_ms > 0) {
#if defined(USE_OPENLDAP)
            struct timeval tv;

            tv.tv_sec = io_timeout_ms / 1000;
            tv.tv_usec = (io_timeout_ms % 1000) * 1000;
            if (LDAP_OPT_SUCCESS != ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &tv)) {
                slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
                    "failed: unable to set outbound I/O timeout to %dms\n", io_timeout_ms);
                slapi_ldap_unbind(ld);
                ld = NULL;
                goto done;
            }
#else /* !USE_OPENLDAP */
            if (prldap_set_session_option(ld, NULL, PRLDAP_OPT_IO_MAX_TIMEOUT, io_timeout_ms) != LDAP_SUCCESS) {
                slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
                    "failed: unable to set outbound I/O timeout to %dms\n", io_timeout_ms);
                slapi_ldap_unbind(ld);
                ld = NULL;
                goto done;
            }
#endif /* !USE_OPENLDAP */
        }

        /*
         * Set SSL strength (server certificate validity checking).
         */
        if (secure > 0) {
#if defined(USE_OPENLDAP)
            if (setup_ol_tls_conn(ld, 0)) {
                slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
                    "failed: unable to set SSL/TLS options\n");
            }
#else
            int ssl_strength = 0;
            LDAP *myld = NULL;

            /*
             * We can only use the set functions below with a real
             * LDAP* if it has already gone through ldapssl_init -
             * so, use NULL if using starttls
             */
            if (secure == 1) {
                myld = ld;
            }

            if (config_get_ssl_check_hostname()) {
                /* check hostname against name in certificate */
                ssl_strength = LDAPSSL_AUTH_CNCHECK;
            } else {
                /* verify certificate only */
                ssl_strength = LDAPSSL_AUTH_CERT;
            }

            if ((rc = ldapssl_set_strength(myld, ssl_strength)) ||
                (rc = ldapssl_set_option(myld, SSL_ENABLE_SSL2, PR_FALSE)) ||
                (rc = ldapssl_set_option(myld, SSL_ENABLE_SSL3, PR_TRUE)) ||
                (rc = ldapssl_set_option(myld, SSL_ENABLE_TLS, PR_TRUE)))
            {
                int prerr = PR_GetError();

                slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_init_ext",
                    "failed: unable to set SSL options ("
                    SLAPI_COMPONENT_NAME_NSPR " error %d - %s)\n",
                    prerr, slapd_pr_strerror(prerr));
            }
            if (secure == 1) {
                /* tell bind code we are using SSL */
                ldap_set_option(ld, LDAP_OPT_SSL, LDAP_OPT_ON);
            }
#endif /* !USE_OPENLDAP */
        }
    }

    if (ld && (secure == 2)) {
        /*
         * We don't have a way to stash context data with the LDAP*, so we
         * stash the information in the client controls (currently unused).
         * We don't want to open the connection in ldap_init, since that's
         * not the semantic - the connection is not usually opened until
         * the first operation is sent, which is usually the bind - or
         * in this case, the start_tls - so we stash the start_tls so
         * we can do it in slapi_ldap_bind - note that this will get
         * cleaned up when the LDAP* is disposed of
         */
        LDAPControl start_tls_dummy_ctrl;
        LDAPControl **clientctrls = NULL;

        /* returns copy of controls */
        ldap_get_option(ld, LDAP_OPT_CLIENT_CONTROLS, &clientctrls);

        start_tls_dummy_ctrl.ldctl_oid = START_TLS_OID;
        start_tls_dummy_ctrl.ldctl_value.bv_val = NULL;
        start_tls_dummy_ctrl.ldctl_value.bv_len = 0;
        start_tls_dummy_ctrl.ldctl_iscritical = 0;
        slapi_add_control_ext(&clientctrls, &start_tls_dummy_ctrl, 1);
        /* set option frees old list and copies the new list */
        ldap_set_option(ld, LDAP_OPT_CLIENT_CONTROLS, clientctrls);
        ldap_controls_free(clientctrls); /* free the copy */
    }

    slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_init_ext",
            "Success: set up conn to [%s:%d]%s\n",
            hostname, port,
            (secure == 2) ? " using startTLS" :
            ((secure == 1) ? " using SSL" : ""));
done:
    ldap_free_urldesc(ludp);

    return( ld );
}

/*
 * Function: slapi_ldap_init()
 * Description: just like ldap_ssl_init() but also arranges for the LDAP
 *	session handle returned to be safely shareable by multiple threads
 *	if "shared" is non-zero.
 * Returns:
 *	an LDAP session handle (NULL if some local error occurs).
 */
LDAP *
slapi_ldap_init( char *ldaphost, int ldapport, int secure, int shared )
{
    return slapi_ldap_init_ext(NULL, ldaphost, ldapport, secure, shared, NULL);
}

/*
 * Does the correct bind operation simple/sasl/cert depending
 * on the arguments passed in.  If the user specified to use
 * starttls in init, this will do the starttls first.  If using
 * ssl or client cert auth, this will initialize the client side
 * of that.
 */
int
slapi_ldap_bind(
    LDAP *ld, /* ldap connection */
    const char *bindid, /* usually a bind DN for simple bind */
    const char *creds, /* usually a password for simple bind */
    const char *mech, /* name of mechanism */
    LDAPControl **serverctrls, /* additional controls to send */
    LDAPControl ***returnedctrls, /* returned controls */
    struct timeval *timeout, /* timeout */
    int *msgidp /* pass in non-NULL for async handling */
)
{
    int rc = LDAP_SUCCESS;
    int err;
    LDAPControl **clientctrls = NULL;
    int secure = 0;
    struct berval bvcreds = {0, NULL};
    LDAPMessage *result = NULL;
    struct berval *servercredp = NULL;

    /* do starttls if requested
       NOTE - starttls is an extop, not a control, but we don't have
       a place we can stash this information in the LDAP*, other
       than the currently unused clientctrls */
    ldap_get_option(ld, LDAP_OPT_CLIENT_CONTROLS, &clientctrls);
    if (clientctrls && clientctrls[0] &&
	slapi_control_present(clientctrls, START_TLS_OID, NULL, NULL)) {
	secure = 2;
    } else {
#if defined(USE_OPENLDAP)
	/* openldap doesn't have a SSL/TLS yes/no flag - so grab the
	   ldapurl, parse it, and see if it is a secure one */
	char *ldapurl = NULL;

	ldap_get_option(ld, LDAP_OPT_URI, &ldapurl);
	if (ldapurl && !PL_strncasecmp(ldapurl, "ldaps", 5)) {
	    secure = 1;
	}
	slapi_ch_free_string(&ldapurl);
#else /* !USE_OPENLDAP */
	ldap_get_option(ld, LDAP_OPT_SSL, &secure);
#endif
    }
    ldap_controls_free(clientctrls);
    ldap_set_option(ld, LDAP_OPT_CLIENT_CONTROLS, NULL);

    if ((secure > 0) && mech && !strcmp(mech, LDAP_SASL_EXTERNAL)) {
#if defined(USE_OPENLDAP)
	/* we already set up a tls context in slapi_ldap_init_ext() - this will
	   free those old settings and context and create a new one */
	rc = setup_ol_tls_conn(ld, 1);
#else
	/* SSL connections will use the server's security context
	   and cert for client auth */
	rc = slapd_SSL_client_auth(ld);
#endif
	if (rc != 0) {
	    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
			    "Error: could not configure the server for cert "
			    "auth - error %d - make sure the server is "
			    "correctly configured for SSL/TLS\n", rc);
	    goto done;
	} else {
	    slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_bind",
			    "Set up conn to use client auth\n");
	}
	bvcreds.bv_val = NULL; /* ignore username and passed in creds */
	bvcreds.bv_len = 0; /* for external auth */
	bindid = NULL;
    } else { /* other type of auth */
	bvcreds.bv_val = (char *)creds;
	bvcreds.bv_len = creds ? strlen(creds) : 0;
    }

    if (secure == 2) { /* send start tls */
	rc = ldap_start_tls_s(ld, NULL /* serverctrls?? */, NULL);
	if (LDAP_SUCCESS != rc) {
	    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
			    "Error: could not send startTLS request: "
			    "error %d (%s) errno %d (%s)\n",
			    rc, ldap_err2string(rc), errno, slapd_system_strerror(errno));
	    goto done;
	}
	slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_bind",
			"startTLS started on connection\n");
    }

    /* The connection has been set up - now do the actual bind, depending on
       the mechanism and arguments */
    if (!mech || (mech == LDAP_SASL_SIMPLE) ||
	!strcmp(mech, LDAP_SASL_EXTERNAL)) {
	int mymsgid = 0;

	slapi_log_error(SLAPI_LOG_SHELL, "slapi_ldap_bind",
			"attempting %s bind with id [%s] creds [%s]\n",
			mech ? mech : "SIMPLE",
			bindid, creds);
	if ((rc = ldap_sasl_bind(ld, bindid, mech, &bvcreds, serverctrls,
	                         NULL /* clientctrls */, &mymsgid))) {
	    char *myhostname = NULL;
	    char *copy = NULL;
	    char *ptr = NULL;
	    int myerrno = errno;
	    int gaierr = 0;

	    ldap_get_option(ld, LDAP_OPT_HOST_NAME, &myhostname);
	    if (myhostname) {
	        ptr = strchr(myhostname, ':');
	        if (ptr) {
	            copy = slapi_ch_strdup(myhostname);
	            *(copy + (ptr - myhostname)) = '\0';
	            myhostname = copy;
	        }
	    }

	    if (0 == myerrno) {
	        struct addrinfo *result = NULL;
	        gaierr = getaddrinfo(myhostname, NULL, NULL, &result);
	        myerrno = errno;
	        if (result) {
	            freeaddrinfo(result);
	        }
	    }
	    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
			    "Error: could not send bind request for id "
			    "[%s] authentication mechanism [%s]: error %d (%s), system error %d (%s), "
			    "network error %d (%s, host \"%s\")\n",
			    bindid ? bindid : "(anon)",
			    mech ? mech : "SIMPLE",
			    rc, ldap_err2string(rc),
			    PR_GetError(), slapd_pr_strerror(PR_GetError()),
			    myerrno ? myerrno : gaierr,
			    myerrno ? slapd_system_strerror(myerrno) : gai_strerror(gaierr),
			    myhostname ? myhostname : "unknown host");
	    slapi_ch_free_string(&copy);
	    goto done;
	}

	if (msgidp) { /* let caller process result */
	    *msgidp = mymsgid;
	} else { /* process results */
            struct timeval default_timeout, *bind_timeout;
            
            if ((timeout == NULL) || ((timeout->tv_sec == 0) && (timeout->tv_usec == 0))) {
                    /* Let's wait 1 min max to bind */
                    default_timeout.tv_sec  = 60;
                    default_timeout.tv_usec = 0;
                    
                    bind_timeout = &default_timeout;
            } else {
                    /* take the one provided by the caller. It should be the one defined in the protocol */
                    bind_timeout = timeout;
            }
	    rc = ldap_result(ld, mymsgid, LDAP_MSG_ALL, bind_timeout, &result);
	    if (-1 == rc) { /* error */
		rc = slapi_ldap_get_lderrno(ld, NULL, NULL);
		slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
				"Error reading bind response for id "
				"[%s] authentication mechanism [%s]: error %d (%s) errno %d (%s)\n",
				bindid ? bindid : "(anon)",
				mech ? mech : "SIMPLE",
				rc, ldap_err2string(rc), errno, slapd_system_strerror(errno));
		goto done;
	    } else if (rc == 0) { /* timeout */
		rc = LDAP_TIMEOUT;
		slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
				"Error: timeout after [%ld.%ld] seconds reading "
				"bind response for [%s] authentication mechanism [%s]\n",
				bind_timeout->tv_sec, bind_timeout->tv_usec,
				bindid ? bindid : "(anon)",
				mech ? mech : "SIMPLE");
		goto done;
	    }
        /* if we got here, we were able to read success result */
        /* Get the controls sent by the server if requested */
        if ((rc = ldap_parse_result(ld, result, &err, NULL, NULL,
                      NULL, returnedctrls, 0)) != LDAP_SUCCESS) {
            slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
                "Error: could not parse bind result: error %d (%s) errno %d (%s)\n",
                rc, ldap_err2string(rc), errno, slapd_system_strerror(errno));
            goto done;
        }

        /* check the result code from the bind operation */
        if(err){
            rc = err;
            slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
                            "Error: could not bind id "
                            "[%s] authentication mechanism [%s]: error %d (%s) errno %d (%s)\n",
                            bindid ? bindid : "(anon)",
                            mech ? mech : "SIMPLE",
                            rc, ldap_err2string(rc), errno, slapd_system_strerror(errno));
            goto done;
        }

	    /* parse the bind result and get the ldap error code */
	    if ((rc = ldap_parse_sasl_bind_result(ld, result, &servercredp,
						  0))) {
		rc = slapi_ldap_get_lderrno(ld, NULL, NULL);
		slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
				"Error: could not read bind results for id "
				"[%s] authentication mechanism [%s]: error %d (%s) errno %d (%s)\n",
				bindid ? bindid : "(anon)",
				mech ? mech : "SIMPLE",
				rc, ldap_err2string(rc), errno, slapd_system_strerror(errno));
		goto done;
	    }
	}
    } else {
	/* a SASL mech - set the sasl ssf to 0 if using TLS/SSL */
	/* openldap supports tls + sasl security */
#if !defined(USE_OPENLDAP)
	if (secure) {
	    sasl_ssf_t max_ssf = 0;
	    ldap_set_option(ld, LDAP_OPT_X_SASL_SSF_MAX, &max_ssf);
	}
#endif
	rc = slapd_ldap_sasl_interactive_bind(ld, bindid, creds, mech,
					      serverctrls, returnedctrls,
					      msgidp);
	if (LDAP_SUCCESS != rc) {
	    slapi_log_error(SLAPI_LOG_FATAL, "slapi_ldap_bind",
			    "Error: could not perform interactive bind for id "
			    "[%s] authentication mechanism [%s]: error %d (%s)\n",
			    bindid ? bindid : "(anon)",
			    mech, /* mech cannot be SIMPLE here */
			    rc, ldap_err2string(rc));
#ifdef HAVE_KRB5
        if(mech && !strcmp(mech, "GSSAPI") && rc == 49){
            /* only on err 49 should we clear out the credential cache */
            clear_krb5_ccache();
        }
#endif
	}
    }

done:
    slapi_ch_bvfree(&servercredp);
    ldap_msgfree(result);

    return rc;
}

char **
slapi_ldap_explode_rdn(const char *rdn, int notypes)
{
    return mozldap_ldap_explode_rdn(rdn, notypes);
}

char **
slapi_ldap_explode_dn(const char *dn, int notypes)
{
    return mozldap_ldap_explode_dn(dn, notypes);
}

void
slapi_add_auth_response_control( Slapi_PBlock *pb, const char *binddn )
{
    LDAPControl		arctrl;
	char			dnbuf_fixedsize[ 512 ], *dnbuf, *dnbuf_dynamic = NULL;
	size_t			dnlen;

	if ( NULL == binddn ) {
		binddn = "";
	}
	dnlen = strlen( binddn );

	/*
	 * According to draft-weltman-ldapv3-auth-response-03.txt section
	 * 4 (Authentication Response Control):
	 *
	 *   The controlType is "2.16.840.1.113730.3.4.15". If the bind request   
	 *   succeeded and resulted in an identity (not anonymous), the 
	 *   controlValue contains the authorization identity [AUTH] granted to 
	 *   the requestor. If the bind request resulted in anonymous 
	 *   authentication, the controlValue field is a string of zero length. 
	 *
	 * [AUTH] is a reference to RFC 2829, which in section 9 defines
	 * authorization identity as:
	 *
	 *
	 *   The authorization identity is a string in the UTF-8 character set,
	 *   corresponding to the following ABNF [7]:
	 *
	 *   ; Specific predefined authorization (authz) id schemes are
	 *   ; defined below -- new schemes may be defined in the future.
	 *
	 *   authzId    = dnAuthzId / uAuthzId
	 *
	 *   ; distinguished-name-based authz id.
	 *   dnAuthzId  = "dn:" dn
	 *   dn         = utf8string    ; with syntax defined in RFC 2253
	 *
	 *   ; unspecified userid, UTF-8 encoded.
	 *   uAuthzId   = "u:" userid
	 *   userid     = utf8string    ; syntax unspecified
	 *
	 *   A utf8string is defined to be the UTF-8 encoding of one or more ISO
	 *   10646 characters.
	 *
	 * We always map identities to DNs, so we always use the dnAuthzId form.
	 */
	arctrl.ldctl_oid = LDAP_CONTROL_AUTH_RESPONSE;
	arctrl.ldctl_iscritical = 0;

	if ( dnlen == 0 ) {		/* anonymous -- return zero length value */
		arctrl.ldctl_value.bv_val = "";
		arctrl.ldctl_value.bv_len = 0;
	} else {				/* mapped to a DN -- return "dn:<DN>" */
		if ( 3 + dnlen < sizeof( dnbuf_fixedsize )) {
			dnbuf = dnbuf_fixedsize;
		} else {
			dnbuf = dnbuf_dynamic = slapi_ch_malloc( 4 + dnlen );
		}
		strcpy( dnbuf, "dn:" );
		strcpy( dnbuf + 3, binddn );
		arctrl.ldctl_value.bv_val = dnbuf;
		arctrl.ldctl_value.bv_len = 3 + dnlen;
	}
	
	if ( slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL, &arctrl ) != 0 ) {
		slapi_log_error( SLAPI_LOG_FATAL, "bind",
				"unable to add authentication response control" );
	}

	if ( NULL != dnbuf_dynamic ) {
		slapi_ch_free_string( &dnbuf_dynamic );
	}
}

/* the following implements the client side of sasl bind, for LDAP server
   -> LDAP server SASL */

typedef struct {
    char *mech;
    char *authid;
    char *username;
    char *passwd;
    char *realm;
} ldapSaslInteractVals;

#ifdef HAVE_KRB5
static void set_krb5_creds(
    const char *authid,
    const char *username,
    const char *passwd,
    const char *realm,
    ldapSaslInteractVals *vals
);
#endif

static void *
ldap_sasl_set_interact_vals(LDAP *ld, const char *mech, const char *authid,
			    const char *username, const char *passwd,
			    const char *realm)
{
    ldapSaslInteractVals *vals = NULL;
    char *idprefix = "";

    vals = (ldapSaslInteractVals *)
        slapi_ch_calloc(1, sizeof(ldapSaslInteractVals));

    if (!vals) {
        return NULL;
    }

    if (mech) {
        vals->mech = slapi_ch_strdup(mech);
    } else {
        ldap_get_option(ld, LDAP_OPT_X_SASL_MECH, &vals->mech);
    }

    if (vals->mech && !strcasecmp(vals->mech, "DIGEST-MD5")) {
        idprefix = "dn:"; /* prefix name and id with this string */
    }

    if (authid) { /* use explicit passed in value */
        vals->authid = slapi_ch_smprintf("%s%s", idprefix, authid);
    } else { /* use option value if any */
        ldap_get_option(ld, LDAP_OPT_X_SASL_AUTHCID, &vals->authid);
        if (!vals->authid) {
/* get server user id? */
            vals->authid = slapi_ch_strdup("");
        }
    }

    if (username) { /* use explicit passed in value */
        vals->username = slapi_ch_smprintf("%s%s", idprefix, username);
    } else { /* use option value if any */
        ldap_get_option(ld, LDAP_OPT_X_SASL_AUTHZID, &vals->username);
        if (!vals->username) { /* use default sasl value */
            vals->username = slapi_ch_strdup("");
        }
    }

    if (passwd) {
        vals->passwd = slapi_ch_strdup(passwd);
    } else {
        vals->passwd = slapi_ch_strdup("");
    }

    if (realm) {
        vals->realm = slapi_ch_strdup(realm);
    } else {
        ldap_get_option(ld, LDAP_OPT_X_SASL_REALM, &vals->realm);
        if (!vals->realm) { /* use default sasl value */
            vals->realm = slapi_ch_strdup("");
        }
    }

#ifdef HAVE_KRB5
    if (mech && !strcmp(mech, "GSSAPI")) {
        set_krb5_creds(authid, username, passwd, realm, vals);
    }
#endif /* HAVE_KRB5 */

    return vals;
}

static void
ldap_sasl_free_interact_vals(void *defaults)
{
    ldapSaslInteractVals *vals = defaults;

    if (vals) {
        slapi_ch_free_string(&vals->mech);
        slapi_ch_free_string(&vals->authid);
        slapi_ch_free_string(&vals->username);
        slapi_ch_free_string(&vals->passwd);
        slapi_ch_free_string(&vals->realm);
        slapi_ch_free(&defaults);
    }
}

static int 
ldap_sasl_get_val(ldapSaslInteractVals *vals, sasl_interact_t *interact, unsigned flags)
{
    const char	*defvalue = interact->defresult;
    int authtracelevel = SLAPI_LOG_SHELL; /* special auth tracing */

    if (vals != NULL) {
        switch(interact->id) {
        case SASL_CB_AUTHNAME:
            defvalue = vals->authid;
            slapi_log_error(authtracelevel, "ldap_sasl_get_val",
                            "Using value [%s] for SASL_CB_AUTHNAME\n",
                            defvalue ? defvalue : "(null)");
            break;
        case SASL_CB_USER:
            defvalue = vals->username;
            slapi_log_error(authtracelevel, "ldap_sasl_get_val",
                            "Using value [%s] for SASL_CB_USER\n",
                            defvalue ? defvalue : "(null)");
            break;
        case SASL_CB_PASS:
            defvalue = vals->passwd;
            slapi_log_error(authtracelevel, "ldap_sasl_get_val",
                            "Using value [%s] for SASL_CB_PASS\n",
                            defvalue ? defvalue : "(null)");
            break;
        case SASL_CB_GETREALM:
            defvalue = vals->realm;
            slapi_log_error(authtracelevel, "ldap_sasl_get_val",
                            "Using value [%s] for SASL_CB_GETREALM\n",
                            defvalue ? defvalue : "(null)");
            break;
        }
    }

    if (defvalue != NULL) {
        interact->result = defvalue;
        if ((char *)interact->result == NULL)
            return (LDAP_NO_MEMORY);
        interact->len = strlen((char *)(interact->result));
    }
    return (LDAP_SUCCESS);
}

static int
ldap_sasl_interact_cb(LDAP *ld, unsigned flags, void *defaults, void *prompts)
{
    sasl_interact_t *interact = NULL;
    ldapSaslInteractVals *sasldefaults = defaults;
    int rc;

    if (prompts == NULL) {
        return (LDAP_PARAM_ERROR);
    }

    for (interact = prompts; interact->id != SASL_CB_LIST_END; interact++) {
        /* Obtain the default value */
        if ((rc = ldap_sasl_get_val(sasldefaults, interact, flags)) != LDAP_SUCCESS) {
            return (rc);
        }
    }

    return (LDAP_SUCCESS);
}

/* figure out from the context and this error if we should
   attempt to retry the bind */
static int
can_retry_bind(LDAP *ld, const char *mech, const char *bindid,
               const char *creds, int rc, const char *errmsg)
{
    int localrc = 0;
    if (errmsg && strstr(errmsg, "Ticket expired")) {
        localrc = 1;
    }

    return localrc;
}

int
slapd_ldap_sasl_interactive_bind(
    LDAP *ld, /* ldap connection */
    const char *bindid, /* usually a bind DN for simple bind */
    const char *creds, /* usually a password for simple bind */
    const char *mech, /* name of mechanism */
    LDAPControl **serverctrls, /* additional controls to send */
    LDAPControl ***returnedctrls, /* returned controls */
    int *msgidp /* pass in non-NULL for async handling */
)
{
    int rc = LDAP_SUCCESS;
    int tries = 0;

    while (tries < 2) {
        void *defaults = ldap_sasl_set_interact_vals(ld, mech, bindid, bindid,
                                                     creds, NULL);
        /* have to first set the defaults used by the callback function */
        /* call the bind function */
	/* openldap does not have the ext version - not sure how to get the
	   returned controls */
#if defined(USE_OPENLDAP)
        rc = ldap_sasl_interactive_bind_s(ld, bindid, mech, serverctrls,
                                              NULL, LDAP_SASL_QUIET,
                                              ldap_sasl_interact_cb, defaults);
#else
        rc = ldap_sasl_interactive_bind_ext_s(ld, bindid, mech, serverctrls,
                                              NULL, LDAP_SASL_QUIET,
                                              ldap_sasl_interact_cb, defaults,
                                              returnedctrls);
#endif
        ldap_sasl_free_interact_vals(defaults);
        if (LDAP_SUCCESS != rc) {
            char *errmsg = NULL;
            rc = slapi_ldap_get_lderrno(ld, NULL, &errmsg);
            slapi_log_error(SLAPI_LOG_FATAL, "slapd_ldap_sasl_interactive_bind",
                            "Error: could not perform interactive bind for id "
                            "[%s] mech [%s]: LDAP error %d (%s) (%s) "
                            "errno %d (%s)\n",
                            bindid ? bindid : "(anon)",
                            mech ? mech : "SIMPLE",
                            rc, ldap_err2string(rc), errmsg,
                            errno, slapd_system_strerror(errno));
            if (can_retry_bind(ld, mech, bindid, creds, rc, errmsg)) {
                ; /* pass through to retry one time */
            } else {
                break; /* done - fail - cannot retry */
            }
        } else {
            break; /* done - success */
        }
        tries++;
    }

    return rc;
}

#ifdef HAVE_KRB5
#include <krb5.h>

/* for some reason this is not in the public API?
   but it is documented e.g. man kinit */
#ifndef KRB5_ENV_CCNAME
#define KRB5_ENV_CCNAME "KRB5CCNAME"
#endif

static void
show_one_credential(int authtracelevel,
                    krb5_context ctx, krb5_creds *cred)
{
    char *logname = "show_one_credential";
    krb5_error_code rc;
    char *name = NULL, *sname = NULL;
    char startts[BUFSIZ], endts[BUFSIZ], renewts[BUFSIZ];

    if ((rc = krb5_unparse_name(ctx, cred->client, &name))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get client name from credential: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }
    if ((rc = krb5_unparse_name(ctx, cred->server, &sname))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get server name from credential: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }
    if (!cred->times.starttime) {
        cred->times.starttime = cred->times.authtime;
    }
    krb5_timestamp_to_sfstring((krb5_timestamp)cred->times.starttime,
                               startts, sizeof(startts), NULL);
    krb5_timestamp_to_sfstring((krb5_timestamp)cred->times.endtime,
                               endts, sizeof(endts), NULL);
    krb5_timestamp_to_sfstring((krb5_timestamp)cred->times.renew_till,
                               renewts, sizeof(renewts), NULL);

    slapi_log_error(authtracelevel, logname,
                    "\tKerberos credential: client [%s] server [%s] "
                    "start time [%s] end time [%s] renew time [%s] "
                    "flags [0x%x]\n", name, sname, startts, endts,
                    renewts, (uint32_t)cred->ticket_flags);

cleanup:
    krb5_free_unparsed_name(ctx, name);
    krb5_free_unparsed_name(ctx, sname);

    return;
}

/*
 * Call this after storing the credentials in the cache
 */
static void
show_cached_credentials(int authtracelevel,
                        krb5_context ctx, krb5_ccache cc,
                        krb5_principal princ)
{
    char *logname = "show_cached_credentials";
    krb5_error_code rc = 0;
    krb5_creds creds;
    krb5_cc_cursor cur;
    char *princ_name = NULL;

    if ((rc = krb5_unparse_name(ctx, princ, &princ_name))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get principal name from principal: %d (%s)\n",
                        rc, error_message(rc));
	    goto cleanup;
    }

	slapi_log_error(authtracelevel, logname,
                    "Ticket cache: %s:%s\nDefault principal: %s\n\n",
                    krb5_cc_get_type(ctx, cc),
                    krb5_cc_get_name(ctx, cc), princ_name);

    if ((rc = krb5_cc_start_seq_get(ctx, cc, &cur))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get cursor to iterate cached credentials: "
                        "%d (%s)\n", rc, error_message(rc));
        goto cleanup;
    }

    while (!(rc = krb5_cc_next_cred(ctx, cc, &cur, &creds))) {
        show_one_credential(authtracelevel, ctx, &creds);
        krb5_free_cred_contents(ctx, &creds);
    }
    if (rc == KRB5_CC_END) {
        if ((rc = krb5_cc_end_seq_get(ctx, cc, &cur))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Could not close cached credentials cursor: "
                            "%d (%s)\n", rc, error_message(rc));
            goto cleanup;
        }
	}

cleanup:
	krb5_free_unparsed_name(ctx, princ_name);

    return;
}

static int
looks_like_a_princ_name(const char *name)
{
    /* a valid principal name will be a non-empty string
       that doesn't have a = in it (which will likely be
       a bind DN) */
    return (name && *name && !strchr(name, '='));
}

static int
credentials_are_valid(
    krb5_context ctx,
    krb5_ccache cc,
    krb5_principal princ,
    const char *princ_name, 
    int *rc
)
{
    char *logname = "credentials_are_valid";
    int myrc = 0;
    krb5_creds mcreds; /* match these values */
    krb5_creds creds; /* returned creds */
    char *tgs_princ_name = NULL;
    krb5_timestamp currenttime;
    int authtracelevel = SLAPI_LOG_SHELL; /* special auth tracing */
    int realm_len;
    char *realm_str;
    int time_buffer = 30; /* seconds - go ahead and renew if creds are
                             about to expire  */

    memset(&mcreds, 0, sizeof(mcreds));
    memset(&creds, 0, sizeof(creds));
    *rc = 0;
    if (!cc) {
        /* ok - no error */
        goto cleanup;
    }

    /* have to construct the tgs server principal in
       order to set mcreds.server required in order
       to use krb5_cc_retrieve_creds() */
    /* get default realm first */
    realm_len = krb5_princ_realm(ctx, princ)->length;
    realm_str = krb5_princ_realm(ctx, princ)->data;
    tgs_princ_name = slapi_ch_smprintf("%s/%*s@%*s", KRB5_TGS_NAME,
                                       realm_len, realm_str,
                                       realm_len, realm_str);

    if ((*rc = krb5_parse_name(ctx, tgs_princ_name, &mcreds.server))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could parse principal [%s]: %d (%s)\n",
                        tgs_princ_name, *rc, error_message(*rc));
        goto cleanup;
    }

    mcreds.client = princ;
    if ((*rc = krb5_cc_retrieve_cred(ctx, cc, 0, &mcreds, &creds))) {
        if (*rc == KRB5_CC_NOTFOUND) {
            /* ok - no creds for this princ in the cache */
            *rc = 0;
        }
        goto cleanup;
    }

    /* have the creds - now look at the timestamp */
    if ((*rc = krb5_timeofday(ctx, &currenttime))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get current time: %d (%s)\n",
                        *rc, error_message(*rc));
        goto cleanup;
    }

    if (currenttime > (creds.times.endtime + time_buffer)) {
        slapi_log_error(authtracelevel, logname,
                        "Credentials for [%s] have expired or will soon "
                        "expire - now [%d] endtime [%d]\n", princ_name,
                        currenttime, creds.times.endtime);
        goto cleanup;
    }

    myrc = 1; /* credentials are valid */
cleanup:
   	krb5_free_cred_contents(ctx, &creds);
    slapi_ch_free_string(&tgs_princ_name);
    if (mcreds.server) {
        krb5_free_principal(ctx, mcreds.server);
    }

    return myrc;
}

static PRCallOnceType krb5_callOnce = {0,0};
static PRLock *krb5_lock = NULL;

static PRStatus
internal_krb5_init(void)
{
    PR_ASSERT(NULL == krb5_lock);
    if ((krb5_lock = PR_NewLock()) == NULL) {
        PRErrorCode errorCode = PR_GetError();
        slapi_log_error(SLAPI_LOG_FATAL, NULL, "internal_krb5_init PR_NewLock failed %d:%s\n",
                        errorCode, slapd_pr_strerror(errorCode));
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

/*
 * This implementation assumes that we want to use the 
 * keytab from the default keytab env. var KRB5_KTNAME
 * as.  This code is very similar to kinit -k -t.  We
 * get a krb context, get the default keytab, get
 * the credentials from the keytab, authenticate with
 * those credentials, create a ccache, store the
 * credentials in the ccache, and set the ccache
 * env var to point to those credentials.
 */
static void
set_krb5_creds(
    const char *authid,
    const char *username,
    const char *passwd,
    const char *realm,
    ldapSaslInteractVals *vals
)
{
    char *logname = "set_krb5_creds";
    const char *cc_type = "MEMORY"; /* keep cred cache in memory */
    krb5_context ctx = NULL;
    krb5_ccache cc = NULL;
    krb5_principal princ = NULL;
    char *princ_name = NULL;
    krb5_error_code rc = 0;
    krb5_creds creds;
    krb5_keytab kt = NULL;
    char *cc_name = NULL;
    char ktname[MAX_KEYTAB_NAME_LEN];
    static char cc_env_name[1024+32]; /* size from ccdefname.c */
    int new_ccache = 0;
    int authtracelevel = SLAPI_LOG_SHELL; /* special auth tracing 
                                             not sure what shell was
                                             used for, does not
                                             appear to be used 
                                             currently */

    /* wipe this out so we can safely free it later if we
       short circuit */
    memset(&creds, 0, sizeof(creds));

    /*
     * we are using static variables and sharing an in-memory credentials cache
     * so we put a lock around all kerberos interactions
     */
    if (PR_SUCCESS != PR_CallOnce(&krb5_callOnce, internal_krb5_init)) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not perform internal krb5 init\n");
        rc = -1;
        goto cleanup;
    }

    PR_Lock(krb5_lock);
    
    /* initialize the kerberos context */
    if ((rc = krb5_init_context(&ctx))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not init Kerberos context: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }

    /* see if there is already a ccache, and see if there are
       creds in the ccache */
    /* grab the default ccache - note: this does not open the cache */
    if ((rc = krb5_cc_default(ctx, &cc))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get default Kerberos ccache: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }

    /* use this cache - construct the full cache name */
    cc_name = slapi_ch_smprintf("%s:%s", krb5_cc_get_type(ctx, cc),
                                krb5_cc_get_name(ctx, cc));

    /* grab the principal from the ccache - will fail if there
       is no ccache */
    if ((rc = krb5_cc_get_principal(ctx, cc, &princ))) {
        if (KRB5_FCC_NOFILE == rc) { /* no cache - ok */
            slapi_log_error(authtracelevel, logname,
                            "The default credentials cache [%s] not found: "
                            "will create a new one.\n", cc_name);
            /* close the cache - we will create a new one below */
            krb5_cc_close(ctx, cc);
            cc = NULL;
            slapi_ch_free_string(&cc_name);
            /* fall through to the keytab auth code below */
        } else { /* fatal */
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Could not open default Kerberos ccache [%s]: "
                            "%d (%s)\n", cc_name, rc, error_message(rc));
            goto cleanup;
        }
    } else { /* have a valid ccache && found principal */
        if ((rc = krb5_unparse_name(ctx, princ, &princ_name))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Unable to get name of principal from ccache [%s]: "
                            "%d (%s)\n", cc_name, rc, error_message(rc));
            goto cleanup;
        }
        slapi_log_error(authtracelevel, logname,
                        "Using principal [%s] from ccache [%s]\n",
                        princ_name, cc_name);
    }

    /* if this is not our type of ccache, there is nothing more we can
       do - just punt and let sasl/gssapi take it's course - this
       usually means there has been an external kinit e.g. in the
       start up script, and it is the responsibility of the script to
       renew those credentials or face lots of sasl/gssapi failures
       This means, however, that the caller MUST MAKE SURE THERE IS NO
       DEFAULT CCACHE FILE or the server will attempt to use it (and
       likely fail) - THERE MUST BE NO DEFAULT CCACHE FILE IF YOU WANT
       THE SERVER TO AUTHENTICATE WITH THE KEYTAB
       NOTE: cc types are case sensitive and always upper case */
    if (cc && strcmp(cc_type, krb5_cc_get_type(ctx, cc))) {
        static int errmsgcounter = 0;
        int loglevel = SLAPI_LOG_FATAL;
        if (errmsgcounter) {
            loglevel = authtracelevel;
        }
        /* make sure we log this message once, in case the user has
           done something unintended, we want to make sure they know
           about it.  However, if the user knows what he/she is doing,
           by using an external ccache file, they probably don't want
           to be notified with an error every time. */
        slapi_log_error(loglevel, logname,
                        "The server will use the external SASL/GSSAPI "
                        "credentials cache [%s:%s].  If you want the "
                        "server to automatically authenticate with its "
                        "keytab, you must remove this cache.  If you "
                        "did not intend to use this cache, you will likely "
                        "see many SASL/GSSAPI authentication failures.\n",
                        krb5_cc_get_type(ctx, cc), krb5_cc_get_name(ctx, cc));
        errmsgcounter++;
        goto cleanup;
    }

    /* need to figure out which principal to use
       1) use the one from the ccache
       2) use username
       3) construct one in the form ldap/fqdn@REALM
    */
    if (!princ && looks_like_a_princ_name(username) &&
        (rc = krb5_parse_name(ctx, username, &princ))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Error: could not convert [%s] into a kerberos "
                        "principal: %d (%s)\n", username,
                        rc, error_message(rc));
        goto cleanup;
    }

    if (getenv("HACK_PRINCIPAL_NAME") &&
        (rc = krb5_parse_name(ctx, getenv("HACK_PRINCIPAL_NAME"), &princ))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Error: could not convert [%s] into a kerberos "
                        "principal: %d (%s)\n", getenv("HACK_PRINCIPAL_NAME"),
                        rc, error_message(rc));
        goto cleanup;
    }

    /* if still no principal, construct one */
    if (!princ) {
        char *hostname = config_get_localhost();
        if ((rc = krb5_sname_to_principal(ctx, hostname, "ldap",
                                          KRB5_NT_SRV_HST, &princ))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Error: could not construct ldap service "
                            "principal from hostname [%s]: %d (%s)\n",
                            hostname ? hostname : "NULL", rc, error_message(rc));
        }
        slapi_ch_free_string(&hostname);
        if (rc) {
            goto cleanup;
        }
    }

    slapi_ch_free_string(&princ_name);
    if ((rc = krb5_unparse_name(ctx, princ, &princ_name))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Unable to get name of principal: "
                        "%d (%s)\n", rc, error_message(rc));
        goto cleanup;
    }

    slapi_log_error(authtracelevel, logname,
                    "Using principal named [%s]\n", princ_name);

    /* grab the credentials from the ccache, if any -
       if the credentials are still valid, we do not have
       to authenticate again */
    if (credentials_are_valid(ctx, cc, princ, princ_name, &rc)) {
        slapi_log_error(authtracelevel, logname,
                        "Credentials for principal [%s] are still "
                        "valid - no auth is necessary.\n",
                        princ_name);
        goto cleanup;
    } else if (rc) { /* some error other than "there are no credentials" */
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Unable to verify cached credentials for "
                        "principal [%s]: %d (%s)\n", princ_name,
                        rc, error_message(rc));
        goto cleanup;
    }      

    /* find our default keytab */
    if ((rc = krb5_kt_default(ctx, &kt))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Unable to get default keytab: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }

    /* get name of keytab for debugging purposes */
    if ((rc = krb5_kt_get_name(ctx, kt, ktname, sizeof(ktname)))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Unable to get name of default keytab: %d (%s)\n",
                        rc, error_message(rc));
        goto cleanup;
    }

    slapi_log_error(authtracelevel, logname,
                    "Using keytab named [%s]\n", ktname);

    /* now do the actual kerberos authentication using
       the keytab, and get the creds */
    rc = krb5_get_init_creds_keytab(ctx, &creds, princ, kt,
                                    0, NULL, NULL);
    if (rc) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not get initial credentials for principal [%s] "
                        "in keytab [%s]: %d (%s)\n",
                        princ_name, ktname, rc, error_message(rc));
        goto cleanup;
    }

    /* completely done with the keytab now, close it */
    krb5_kt_close(ctx, kt);
    kt = NULL; /* no double free */

    /* we now have the creds and the principal to which the
       creds belong - use or allocate a new memory based
       cache to hold the creds */
    if (!cc_name) {
#if HAVE_KRB5_CC_NEW_UNIQUE
        /* krb5_cc_new_unique is a new convenience function which
           generates a new unique name and returns a memory
           cache with that name */
        if ((rc = krb5_cc_new_unique(ctx, cc_type, NULL, &cc))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Could not create new unique memory ccache: "
                            "%d (%s)\n",
                            rc, error_message(rc));
            goto cleanup;
        }
        cc_name = slapi_ch_smprintf("%s:%s", cc_type,
                                    krb5_cc_get_name(ctx, cc));
#else
        /* store the cache in memory - krb5_init_context uses malloc
           to create the ctx, so the address should be unique enough
           for our purposes */
        if (!(cc_name = slapi_ch_smprintf("%s:%p", cc_type, ctx))) {
            slapi_log_error(SLAPI_LOG_FATAL, logname,
                            "Could create Kerberos memory ccache: "
                            "out of memory\n");
            rc = 1;
            goto cleanup;
        }
#endif
        slapi_log_error(authtracelevel, logname,
                        "Generated new memory ccache [%s]\n", cc_name);
        new_ccache = 1; /* need to set this in env. */
    } else {
        slapi_log_error(authtracelevel, logname,
                        "Using existing ccache [%s]\n", cc_name);
    }

    /* krb5_cc_resolve is basically like an init -
       this creates the cache structure, and creates a slot
       for the cache in the static linked list in memory, if
       there is not already a slot -
       see cc_memory.c for details 
       cc could already have been created by new_unique above
    */
    if (!cc && (rc = krb5_cc_resolve(ctx, cc_name, &cc))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not create ccache [%s]: %d (%s)\n",
                        cc_name, rc, error_message(rc));
        goto cleanup;
    }

    /* wipe out previous contents of cache for this principal, if any */
    if ((rc = krb5_cc_initialize(ctx, cc, princ))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not initialize ccache [%s] for the new "
                        "credentials for principal [%s]: %d (%s)\n",
                        cc_name, princ_name, rc, error_message(rc));
        goto cleanup;
    }

    /* store the credentials in the cache */
    if ((rc = krb5_cc_store_cred(ctx, cc, &creds))) {
        slapi_log_error(SLAPI_LOG_FATAL, logname,
                        "Could not store the credentials in the "
                        "ccache [%s] for principal [%s]: %d (%s)\n",
                        cc_name, princ_name, rc, error_message(rc));
        goto cleanup;
    }

    /* now, do a "klist" to show the credential information, and log it */
    show_cached_credentials(authtracelevel, ctx, cc, princ);

    /* set the CC env var to the value of the cc cache name */
    /* since we can't pass krb5 context up and out of here
       and down through the ldap sasl layer, we set this
       env var so that calls to krb5_cc_default_name will
       use this */
    if (new_ccache) {
        PR_snprintf(cc_env_name, sizeof(cc_env_name),
                    "%s=%s", KRB5_ENV_CCNAME, cc_name);
        PR_SetEnv(cc_env_name);
        slapi_log_error(authtracelevel, logname,
                        "Set new env for ccache: [%s]\n",
                        cc_env_name);
    }

cleanup:
    /* use NULL as username and authid */
    slapi_ch_free_string(&vals->username);
    slapi_ch_free_string(&vals->authid);

    krb5_free_unparsed_name(ctx, princ_name);
    if (kt) { /* NULL not allowed */
        krb5_kt_close(ctx, kt);
    }
    if (creds.client == princ) {
        creds.client = NULL;
    }
    krb5_free_cred_contents(ctx, &creds);
    slapi_ch_free_string(&cc_name);
    krb5_free_principal(ctx, princ);
    if (cc) {
        krb5_cc_close(ctx, cc);
    }
    if (ctx) { /* cannot pass NULL to free context */
        krb5_free_context(ctx);
    }
    PR_Unlock(krb5_lock);

    return;
}

static void
clear_krb5_ccache()
{
    krb5_context ctx = NULL;
    krb5_ccache cc = NULL;
    int rc = 0;

    PR_Lock(krb5_lock);

    /* initialize the kerberos context */
    if ((rc = krb5_init_context(&ctx))) {
        slapi_log_error(SLAPI_LOG_FATAL, "clear_krb5_ccache", "Could not initialize kerberos context: %d (%s)\n",
                        rc, error_message(rc));
        goto done;
    }
    /* get the default ccache */
    if ((rc = krb5_cc_default(ctx, &cc))) {
        slapi_log_error(SLAPI_LOG_FATAL, "clear_krb5_ccache", "Could not get default kerberos ccache: %d (%s)\n",
                        rc, error_message(rc));
        goto done;
    }
    /* destroy the ccache */
    if((rc = krb5_cc_destroy(ctx, cc))){
        slapi_log_error(SLAPI_LOG_FATAL, "clear_krb5_ccache", "Could not destroy kerberos ccache: %d (%s)\n",
                        rc, error_message(rc));
    } else {
        slapi_log_error(SLAPI_LOG_TRACE,"clear_krb5_ccache", "Successfully cleared kerberos ccache\n");
    }

done:
    if(ctx){
        krb5_free_context(ctx);
    }

    PR_Unlock(krb5_lock);
}

#endif /* HAVE_KRB5 */

#define LDAP_DN		1
#define LDAP_RDN	2

#define INQUOTE		1
#define OUTQUOTE	2

/* We use the following two functions when built against OpenLDAP
 * or the MozLDAP libs since the MozLDAP ldap_explode_dn() function
 * does not handle trailing whitespace characters properly. */
static char **
mozldap_ldap_explode( const char *dn, const int notypes, const int nametype )
{
	char	*p, *q, *rdnstart, **rdns = NULL;
	size_t	plen = 0;
	int		state = 0;
	int		count = 0;
	int		startquote = 0;
	int		endquote = 0;
	int		len = 0;
	int		goteq = 0;

	if ( dn == NULL ) {
		dn = "";
	}

	while ( ldap_utf8isspace( (char *)dn )) { /* ignore leading spaces */
		++dn;
	}

	p = rdnstart = (char *) dn;
	state = OUTQUOTE;

	do {
		p += plen;
		plen = 1;
		switch ( *p ) {
		case '\\':
			if ( *++p == '\0' )
				p--;
			else
				plen = LDAP_UTF8LEN(p);
			break;
		case '"':
			if ( state == INQUOTE )
				state = OUTQUOTE;
			else
				state = INQUOTE;
			break;
		case '+': if ( nametype != LDAP_RDN ) break;
		case ';':
		case ',':
		case '\0':
			if ( state == OUTQUOTE ) {
				/*
				 * semicolon and comma are not valid RDN
				 * separators.
				 */
				if ( nametype == LDAP_RDN && 
					( *p == ';' || *p == ',' || !goteq)) {
					charray_free( rdns );
					return NULL;
				}
				if ( (*p == ',' || *p == ';') && !goteq ) {
                                   /* If we get here, we have a case similar
				    * to <attr>=<value>,<string>,<attr>=<value>
				    * This is not a valid dn */
				    charray_free( rdns );
				    return NULL;
				}
				goteq = 0;
				++count;
				if ( rdns == NULL ) {
					if (( rdns = (char **)slapi_ch_malloc( 8
						 * sizeof( char *))) == NULL )
						return( NULL );
				} else if ( count >= 8 ) {
					if (( rdns = (char **)slapi_ch_realloc(
					    (char *)rdns, (count+1) *
					    sizeof( char *))) == NULL )
						return( NULL );
				}
				rdns[ count ] = NULL;
				endquote = 0;
				if ( notypes ) {
					for ( q = rdnstart;
					    q < p && *q != '='; ++q ) {
						;
					}
					if ( q < p ) { /* *q == '=' */
						rdnstart = ++q;
					}
					if ( *rdnstart == '"' ) {
						startquote = 1;
						++rdnstart;
					}
					
					if ( (*(p-1) == '"') && startquote ) {
						endquote = 1;
						--p;
					}
				}

				len = p - rdnstart;
				if (( rdns[ count-1 ] = (char *)slapi_ch_calloc(
				    1, len + 1 )) != NULL ) {
				    	memcpy( rdns[ count-1 ], rdnstart,
					    len );
					if ( !endquote ) {
						/* trim trailing spaces */
						while ( len > 0 &&
							(rdns[count-1][len-1] == ' ')) {
							--len;
						}
					}
					rdns[ count-1 ][ len ] = '\0';
				}

				/*
				 *  Don't forget to increment 'p' back to where
				 *  it should be.  If we don't, then we will
				 *  never get past an "end quote."
				 */
				if ( endquote == 1 )
					p++;

				rdnstart = *p ? p + 1 : p;
				while ( ldap_utf8isspace( rdnstart ))
					++rdnstart;
			}
			break;
		case '=':
			if ( state == OUTQUOTE ) {
				goteq = 1;
			}
			/* FALL */
		default:
			plen = LDAP_UTF8LEN(p);
			break;
		}
	} while ( *p );

	return( rdns );
}

static char **
mozldap_ldap_explode_dn( const char *dn, const int notypes )
{
	return( mozldap_ldap_explode( dn, notypes, LDAP_DN ) );
}

static char **
mozldap_ldap_explode_rdn( const char *rdn, const int notypes )
{
	return( mozldap_ldap_explode( rdn, notypes, LDAP_RDN ) );
}

int
slapi_is_ipv6_addr( const char *hostname ){
    PRNetAddr addr;

    if(PR_StringToNetAddr(hostname, &addr) == PR_SUCCESS &&
       !PR_IsNetAddrType(&addr, PR_IpAddrV4Mapped) &&
       addr.raw.family == PR_AF_INET6)
    {
        return 1;
    }
    return 0;
}

/*
 * Get the length of the ber-encoded ldap message.  Note, only the length of
 * the LDAP operation is returned, not the length of the entire berval.
 * Add 2 to the length for the entire PDU size.  If "strict" is set then
 * the entire LDAP PDU must be in the berval.
 */
ber_len_t
slapi_berval_get_msg_len(struct berval *bv, int strict)
{
    ber_len_t len, rest;
    unsigned char *ptr;
    int i;

    /* Get the ldap operation length */
    rest = bv->bv_len - 1;
    ptr =  (unsigned char *)bv->bv_val ;
    ptr++;  /* skip the tag and get right to the length */
    len = *ptr++;
    rest--;

    if ( len & 0x80U ) {
        len &= 0x7fU;
        if ( len - 1U > sizeof(ber_len_t) - 1U || rest < len ) {
            /* Indefinite-length/too long length/not enough data */
            return -1;
        }
        rest -= len;
        i = len;
        for( len = *ptr++ & 0xffU; --i; len |= *ptr++ & 0xffU ) {
            len <<= 8;
        }
    }
    if( strict && len > rest ) {
        return -1;
    }

    return len;
}
