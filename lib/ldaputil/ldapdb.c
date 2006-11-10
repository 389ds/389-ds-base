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


#ifndef DONT_USE_LDAP_SSL
#define USE_LDAP_SSL
#endif


#include <string.h>
#include <malloc.h>

#include <nspr.h>
#include <prthread.h>
#include <prmon.h>

#include "ldaputil/errors.h"
#include "ldaputil/certmap.h"
#include "ldaputil/ldapdb.h"

#ifdef USE_LDAP_SSL
/* removed for new ns security integration
#include <sec.h>
#include <key.h>
#include "cert.h"
*/
#include <ssl.h>
#include "ldap_ssl.h"
#endif

#include "ldaputili.h"

#define LDAPDB_PREFIX_WITH_SLASHES	    "ldapdb://"
#define LDAPDB_PREFIX_WITH_SLASHES_LEN  9

uintn           tsdindex;

static void ldb_crit_init (LDAPDatabase_t *ldb)
{
    ldb->crit = PR_NewMonitor();
}

static void ldb_crit_enter (LDAPDatabase_t *ldb)
{
	PR_EnterMonitor(ldb->crit);
}

static void ldb_crit_exit (LDAPDatabase_t *ldb)
{
	PR_ExitMonitor(ldb->crit);
}

struct ldap_error {
    int     le_errno;
    char    *le_matched;
    char    *le_errmsg;
};


static void set_ld_error( int err, char *matched, char *errmsg, void *dummy )
{
    struct ldap_error *le;

    if (!(le = (struct ldap_error *) PR_GetThreadPrivate(tsdindex))) {
	le = (struct ldap_error *) malloc(sizeof(struct ldap_error));
	memset((void *)le, 0, sizeof(struct ldap_error));
	PR_SetThreadPrivate(tsdindex, (void *)le);
    }
    le->le_errno = err;
    if ( le->le_matched != NULL ) {
	ldap_memfree( le->le_matched );
    }
    le->le_matched = matched;
    if ( le->le_errmsg != NULL ) {
	ldap_memfree( le->le_errmsg );
    }
    le->le_errmsg = errmsg;
}

static int get_ld_error( char **matched, char **errmsg, void *dummy )
{
    struct ldap_error *le;

    le = (struct ldap_error *) PR_GetThreadPrivate( tsdindex);
    if ( matched != NULL ) {
	*matched = le->le_matched;
    }
    if ( errmsg != NULL ) {
	*errmsg = le->le_errmsg;
    }
    return( le->le_errno );
}

static void set_errno( int err )
{
    PR_SetError( err, 0);
}

static int get_errno( void )
{
    return( PR_GetError() );
}

#ifdef LDAP_OPT_DNS_FN_PTRS		/* not supported in older LDAP SDKs */
static LDAPHostEnt *
ldapu_copyPRHostEnt2LDAPHostEnt( LDAPHostEnt *ldhp, PRHostEnt *prhp )
{
	ldhp->ldaphe_name = prhp->h_name;
	ldhp->ldaphe_aliases = prhp->h_aliases;
	ldhp->ldaphe_addrtype = prhp->h_addrtype;
	ldhp->ldaphe_length =  prhp->h_length;
	ldhp->ldaphe_addr_list =  prhp->h_addr_list;
	return( ldhp );
}

static LDAPHostEnt *
ldapu_gethostbyname( const char *name, LDAPHostEnt *result,
	char *buffer, int buflen, int *statusp, void *extradata )
{
	PRHostEnt	prhent;

	if( !statusp || ( *statusp = (int)PR_GetHostByName( name, buffer, 
			buflen, &prhent )) == PR_FAILURE ) {
		return( NULL );
	}

	return( ldapu_copyPRHostEnt2LDAPHostEnt( result, &prhent ));
}

static LDAPHostEnt *
ldapu_gethostbyaddr( const char *addr, int length, int type,
	LDAPHostEnt *result, char *buffer, int buflen, int *statusp,
	void *extradata )
{
    return( (LDAPHostEnt *)PR_GetError() );
}
#endif /* LDAP_OPT_DNS_FN_PTRS */


static void unescape_ldap_basedn (char *str)
{
    if (strchr(str, '%')) {
	register int x = 0, y = 0;
	int l = strlen(str);
	char digit;

	while(x < l)  {
	    if((str[x] == '%') && (x < (l - 2)))  {
		++x;
		digit = (str[x] >= 'A' ? 
			 ((str[x] & 0xdf) - 'A')+10 : (str[x] - '0'));
		digit *= 16;

		++x;
		digit += (str[x] >= 'A' ? 
			  ((str[x] & 0xdf) - 'A')+10 : (str[x] - '0'));

		str[y] = digit;
	    }
	    else {
		str[y] = str[x];
	    }
	    x++;
	    y++;
	}
	str[y] = '\0';
    }
}

/*
 *  extract_path_and_basedn:
 *	Description:
 *	    Parses the ldapdb url and returns pathname to the lcache.conf file
 *	    and basedn.  The caller must free the memory allocated for the
 *	    returned path and the basedn.
 *	Arguments:
 *	    url   		URL (must begin with ldapdb://)
 *	    path    		Pathname to the lcache.conf file
 *	    basedn		basedn for the ldapdb.
 *	Return Values: (same as ldap_find)
 *	    LDAPU_SUCCESS	if the URL is parsed successfully
 *	    <rv>		if error, one of the LDAPU_ errors.
 */
static int extract_path_and_basedn(const char *url_in, char **path_out,
				   char **basedn_out)
{
    char *url = strdup(url_in);
    char *basedn;
    char *path;

    *path_out = 0;
    *basedn_out = 0;

    if (!url) return LDAPU_ERR_OUT_OF_MEMORY;

    if (strncmp(url, LDAPDB_URL_PREFIX, LDAPDB_URL_PREFIX_LEN)) {
	free(url);
	return LDAPU_ERR_URL_INVALID_PREFIX;
    }

    path = url + LDAPDB_URL_PREFIX_LEN;

    if (strncmp(path, "//", 2)) {
	free(url);
	return LDAPU_ERR_URL_INVALID_PREFIX;
    }

    path += 2;

    /* Find base DN -- empty string is OK */
    if ((basedn = strrchr(path, '/')) == NULL) {
	free(url);
	return LDAPU_ERR_URL_NO_BASEDN;
    }

    *basedn++ = '\0';		/* terminate the path */
    unescape_ldap_basedn(basedn);
    *basedn_out = strdup(basedn);
    *path_out = strdup(path);
    free(url);
    return (*basedn_out && *path_out) ? LDAPU_SUCCESS : LDAPU_ERR_OUT_OF_MEMORY;
}

NSAPI_PUBLIC int ldapu_ldapdb_url_parse (const char *url,
					 LDAPDatabase_t **ldb_out)
{
    char *path = 0;
    char *basedn = 0;
    LDAPDatabase_t *ldb = 0;
    int rv;

    rv = extract_path_and_basedn(url, &path, &basedn);

    if (rv != LDAPU_SUCCESS) {
	if (path) free(path);
	if (basedn) free(basedn);
	return rv;
    }

    ldb = (LDAPDatabase_t *)malloc(sizeof(LDAPDatabase_t));

    if (!ldb) {
	if (path) free(path);
	if (basedn) free(basedn);
	return LDAPU_ERR_OUT_OF_MEMORY;
    }

    memset((void *)ldb, 0, sizeof(LDAPDatabase_t));
    ldb->basedn = basedn;	/* extract_path_and_basedn has allocated */
    ldb->host = path;		/* memory for us -- don't make a copy */
    ldb_crit_init(ldb);
    *ldb_out = ldb;

    return LDAPU_SUCCESS;
}


/*
 *  ldapu_url_parse:
 *	Description:
 *	    Parses the ldapdb or ldap url and returns a LDAPDatabase_t struct
 *	Arguments:
 *	    url   		URL (must begin with ldapdb://)
 *	    binddn  		DN to use to bind to ldap server.
 *	    bindpw		Password to use to bind to ldap server.
 *	    ldb			a LDAPDatabase_t struct filled from parsing
 *				the url.
 *	Return Values: (same as ldap_find)
 *	    LDAPU_SUCCESS	if the URL is parsed successfully
 *	    <rv>		if error, one of the LDAPU_ errors.
 */
NSAPI_PUBLIC int ldapu_url_parse (const char *url, const char *binddn,
				  const char *bindpw,
				  LDAPDatabase_t **ldb_out)
{
    LDAPDatabase_t *ldb;
    LDAPURLDesc *ludp = 0;
    int rv;

    *ldb_out = 0;

    if (!strncmp(url, LDAPDB_PREFIX_WITH_SLASHES,
		 LDAPDB_PREFIX_WITH_SLASHES_LEN))
    {
	return ldapu_ldapdb_url_parse(url, ldb_out);
    }

    /* call ldapsdk's parse function */
    rv = ldap_url_parse((char *)url, &ludp);

    if (rv != LDAP_SUCCESS) {
	if (ludp) ldap_free_urldesc(ludp);
	return LDAPU_ERR_URL_PARSE_FAILED;
    }

    ldb = (LDAPDatabase_t *)malloc(sizeof(LDAPDatabase_t));

    if (!ldb) {
	ldap_free_urldesc(ludp);
	return LDAPU_ERR_OUT_OF_MEMORY;
    }

    memset((void *)ldb, 0, sizeof(LDAPDatabase_t));
    ldb->host = ludp->lud_host ? strdup(ludp->lud_host) : 0;
    ldb->use_ssl = ludp->lud_options & LDAP_URL_OPT_SECURE;
    ldb->port = ludp->lud_port ? ludp->lud_port : ldb->use_ssl ? 636 : 389;
    ldb->basedn = ludp->lud_dn ? strdup(ludp->lud_dn) : 0;
    ldb_crit_init(ldb);
    ldap_free_urldesc(ludp);

    if (binddn) ldb->binddn = strdup(binddn);

    if (bindpw) ldb->bindpw = strdup(bindpw);

    /* success */
    *ldb_out = ldb;
    
    return LDAPU_SUCCESS;
}

NSAPI_PUBLIC void ldapu_free_LDAPDatabase_t (LDAPDatabase_t *ldb)
{
    if (ldb->host) free(ldb->host);
    if (ldb->basedn) free(ldb->basedn);
    if (ldb->filter) free(ldb->filter);
    if (ldb->binddn) free(ldb->binddn);
    if (ldb->bindpw) free(ldb->bindpw);
    if (ldb->ld) ldapu_unbind(ldb->ld);
    memset((void *)ldb, 0, sizeof(LDAPDatabase_t));
    free(ldb);
}

NSAPI_PUBLIC LDAPDatabase_t *ldapu_copy_LDAPDatabase_t (const LDAPDatabase_t *ldb)
{
    LDAPDatabase_t *nldb = (LDAPDatabase_t *)malloc(sizeof(LDAPDatabase_t));

    if (!nldb) return 0;

    memset((void *)nldb, 0, sizeof(LDAPDatabase_t));
    nldb->use_ssl = ldb->use_ssl;
    if (ldb->host) nldb->host = strdup(ldb->host);
    nldb->port = ldb->port;
    if (ldb->basedn) nldb->basedn = strdup(ldb->basedn);
    nldb->scope = ldb->scope;
    if (ldb->filter) nldb->filter = strdup(ldb->filter);
    nldb->ld = 0;
    if (ldb->binddn) nldb->binddn = strdup(ldb->binddn);
    if (ldb->bindpw) nldb->bindpw = strdup(ldb->bindpw);
    nldb->bound = 0;
    ldb_crit_init(nldb);

    return nldb;
}

NSAPI_PUBLIC int ldapu_is_local_db (const LDAPDatabase_t *ldb)
{
    return ldb->port ? 0 : 1;
}

static int LDAP_CALL LDAP_CALLBACK
ldapu_rebind_proc (LDAP *ld, char **whop, char **passwdp,
                   int *authmethodp, int freeit, void *arg)
{
  if (freeit == 0) {
    LDAPDatabase_t *ldb = (LDAPDatabase_t *)arg;
    *whop = ldb->binddn;
    *passwdp = ldb->bindpw;
    *authmethodp = LDAP_AUTH_SIMPLE;
  }

  return LDAP_SUCCESS;
}

NSAPI_PUBLIC int ldapu_ldap_init(LDAPDatabase_t *ldb)
{
    LDAP *ld = 0;

    ldb_crit_enter(ldb);

#ifdef USE_LDAP_SSL
    /* Note. This assume the security related initialization is done  */
    /* The step needed is :
       PR_Init
       RNG_SystemInfoForRNG
       RNG_RNGInit
       CERT_OpenCertDBFilename
       CERT_SetDefaultCertDB
       SECMOD_init

       And because ldapssl_init depends on security initialization, it is
       no good for non-ssl init
       */
    if (ldb->use_ssl)
      ld = ldapssl_init(ldb->host, ldb->port, ldb->use_ssl);
    else ldap_init(ldb->host, ldb->port);
#else
    ld = ldapu_init(ldb->host, ldb->port);
#endif

    if (ld == NULL) {
	DBG_PRINT1("ldapu_ldap_init: Failed to initialize connection");
	ldb_crit_exit(ldb);
	return LDAPU_ERR_LDAP_INIT_FAILED;
    }

    {
	struct ldap_thread_fns  tfns;

        PR_NewThreadPrivateIndex(&tsdindex, NULL);

        /* set mutex pointers */
        memset( &tfns, '\0', sizeof(struct ldap_thread_fns) );
	tfns.ltf_mutex_alloc = (void *(*)(void))PR_NewMonitor;
	tfns.ltf_mutex_free = (void (*)(void *))PR_DestroyMonitor;
        tfns.ltf_mutex_lock = (int (*)(void *)) PR_EnterMonitor;
        tfns.ltf_mutex_unlock = (int (*)(void *)) PR_ExitMonitor;
        tfns.ltf_get_errno = get_errno;
        tfns.ltf_set_errno = set_errno;
        tfns.ltf_get_lderrno = get_ld_error;
        tfns.ltf_set_lderrno = set_ld_error;
        /* set ld_errno pointers */
        if ( ldapu_set_option( ld, LDAP_OPT_THREAD_FN_PTRS, (void *) &tfns )
	     != 0 ) {
	    ldb_crit_exit(ldb);
	    return LDAPU_ERR_LDAP_SET_OPTION_FAILED;
        }
    }
#ifdef LDAP_OPT_DNS_FN_PTRS		/* not supported in older LDAP SDKs */
    {
	/* install DNS functions */
	struct ldap_dns_fns	dnsfns;
        memset( &dnsfns, '\0', sizeof(struct ldap_dns_fns) );
	dnsfns.lddnsfn_bufsize = PR_NETDB_BUF_SIZE;
	dnsfns.lddnsfn_gethostbyname = ldapu_gethostbyname;
	dnsfns.lddnsfn_gethostbyaddr = ldapu_gethostbyaddr;
        if ( ldapu_set_option( ld, LDAP_OPT_DNS_FN_PTRS, (void *)&dnsfns )
	     != 0 ) {
	    ldb_crit_exit(ldb);
	    return LDAPU_ERR_LDAP_SET_OPTION_FAILED;
        }
    }
#endif /* LDAP_OPT_DNS_FN_PTRS */

    if (ldapu_is_local_db(ldb)) {
      /* No more Local db support, force error!  */
      return LDAPU_ERR_LCACHE_INIT_FAILED;
#if 0
	int optval = 1;

	if (lcache_init(ld, ldb->host) != 0) {
	    ldb_crit_exit(ldb);
	    return LDAPU_ERR_LCACHE_INIT_FAILED;
	}

	if (ldap_set_option(ld, LDAP_OPT_CACHE_ENABLE, &optval) != 0) {
	    ldb_crit_exit(ldb);
	    return LDAPU_ERR_LDAP_SET_OPTION_FAILED;
	}

	optval = LDAP_CACHE_LOCALDB;

	if (ldap_set_option(ld, LDAP_OPT_CACHE_STRATEGY, &optval) != 0) {
	    ldb_crit_exit(ldb);
	    return LDAPU_ERR_LDAP_SET_OPTION_FAILED;
	}
#endif
    }
    else if (ldb->binddn && *ldb->binddn) {
      /* Set the rebind proc */
      /* Rebind proc is used when chasing a referral */
      ldap_set_rebind_proc(ld, ldapu_rebind_proc, (void *)ldb);
    }

    ldb->ld = ld;
    ldb_crit_exit(ldb);

    return LDAPU_SUCCESS;
}

NSAPI_PUBLIC int ldapu_ldap_rebind (LDAPDatabase_t *ldb)
{
    int retry;
    int rv = LDAPU_FAILED;

    ldb_crit_enter(ldb);

    if (ldb->ld) {
	retry = (ldb->bound != -1 ? 1 : 0); /* avoid recursion */

#ifdef USE_LDAP_SSL
	if (ldb->use_ssl && !CERT_GetDefaultCertDB()) {
	    /* default cert database has not been initialized */
	    rv = LDAPU_ERR_NO_DEFAULT_CERTDB;
	}
	else
#endif
	{
	    rv = ldap_simple_bind_s(ldb->ld, ldb->binddn, ldb->bindpw);
	}

	/* retry once if the LDAP server is down */
	if (rv == LDAP_SERVER_DOWN && retry) {
	    ldb->bound = -1; /* to avoid recursion */
	    rv = ldapu_ldap_reinit_and_rebind(ldb);
	}

	if (rv == LDAPU_SUCCESS) ldb->bound = 1;
    }

    ldb_crit_exit(ldb);

    return rv;
}

NSAPI_PUBLIC int ldapu_ldap_init_and_bind (LDAPDatabase_t *ldb)
{
    int rv = LDAPU_SUCCESS;

    ldb_crit_enter(ldb);

    if (!ldb->ld) {
	rv = ldapu_ldap_init(ldb);
	/* ldb->bound may be set to -1 to avoid recursion */
	if (ldb->bound == 1) ldb->bound = 0;
    }

    /* bind as binddn & bindpw if not bound already */
    if (rv == LDAPU_SUCCESS && ldb->bound != 1) {
	rv = ldapu_ldap_rebind (ldb);
    }

    ldb_crit_exit(ldb);

    return rv;
}

NSAPI_PUBLIC int ldapu_ldap_reinit_and_rebind (LDAPDatabase_t *ldb)
{
    int rv;

    ldb_crit_enter(ldb);

    if (ldb->ld) {
	ldapu_unbind(ldb->ld);
	ldb->ld = 0;
    }

    rv = ldapu_ldap_init_and_bind(ldb);
    ldb_crit_exit(ldb);
    return rv;
}

