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

#include "ldaputili.h"
#include <ldap.h>
#ifdef USE_LDAP_SSL
#include <ldap_ssl.h>
#endif

#if defined( _WINDOWS ) && ! defined( _WIN32 )
/* On 16-bit WINDOWS platforms, it's erroneous to call LDAP API functions
 * via a function pointer, since they are not declared LDAP_CALLBACK.
 * So, we define the following functions, which are LDAP_CALLBACK, and
 * simply delegate to their counterparts in the LDAP API.
 */

#ifdef USE_LDAP_SSL
static LDAP_CALL LDAP_CALLBACK LDAP*
ldapuVd_ssl_init( const char *host, int port, int encrypted )
{
    return ldapssl_init (host, port, encrypted);
}
#else
static LDAP_CALL LDAP_CALLBACK LDAP*
ldapuVd_init    ( const char *host, int port )
{
    return ldap_init (host, port);
}
#endif

static LDAP_CALL LDAP_CALLBACK int
ldapuVd_set_option( LDAP *ld, int opt, void *val )
{
    return ldap_set_option (ld, opt, val);
}

static LDAP_CALL LDAP_CALLBACK int
ldapuVd_simple_bind_s( LDAP* ld, const char *username, const char *passwd )
{
    return ldap_simple_bind_s (ld, username, passwd);
}

static LDAP_CALL LDAP_CALLBACK int
ldapuVd_unbind( LDAP *ld )
{
    return ldap_unbind (ld);
}

static LDAP_CALL LDAP_CALLBACK int
ldapuVd_search_s( LDAP* ld, const char* baseDN, int scope, const char* filter, 
	char** attrs, int attrsonly, LDAPMessage** result )
{
    return ldap_search_s (ld, baseDN, scope, filter, attrs, attrsonly, result);
}

static LDAP_CALL LDAP_CALLBACK int
ldapuVd_count_entries( LDAP* ld, LDAPMessage* msg )
{
    return ldap_count_entries (ld, msg);
}

static LDAP_CALL LDAP_CALLBACK LDAPMessage*
ldapuVd_first_entry( LDAP* ld, LDAPMessage* msg )
{
    return ldap_first_entry (ld, msg);
}

static LDAP_CALL LDAP_CALLBACK LDAPMessage*
ldapuVd_next_entry( LDAP* ld, LDAPMessage* entry )
{
    return ldap_next_entry(ld, entry);
}

static LDAP_CALL LDAP_CALLBACK char*
ldapuVd_get_dn( LDAP* ld, LDAPMessage* entry )
{
    return ldap_get_dn (ld, entry);
}

static LDAP_CALL LDAP_CALLBACK char*
ldapuVd_first_attribute( LDAP* ld, LDAPMessage* entry, BerElement** iter )
{
    return ldap_first_attribute (ld, entry, iter);
}

static LDAP_CALL LDAP_CALLBACK char*
ldapuVd_next_attribute( LDAP* ld, LDAPMessage* entry, BerElement* iter)
{
    return ldap_next_attribute (ld, entry, iter);
}

static LDAP_CALL LDAP_CALLBACK char**
ldapuVd_get_values( LDAP *ld, LDAPMessage *entry, const char *desc )
{
    return ldap_get_values (ld, entry, desc);
}

static LDAP_CALL LDAP_CALLBACK struct berval**
ldapuVd_get_values_len( LDAP *ld, LDAPMessage *entry, const char *desc )
{
    return ldap_get_values_len (ld, entry, desc);
}

#else
/* On other platforms, an LDAP API function can be called via a pointer. */
#ifdef USE_LDAP_SSL
#define ldapuVd_ssl_init        ldapssl_init
#else
#define ldapuVd_init            ldap_init
#endif
#define ldapuVd_set_option      ldap_set_option
#define ldapuVd_simple_bind_s   ldap_simple_bind_s
#define ldapuVd_unbind          ldap_unbind
#define ldapuVd_set_option      ldap_set_option
#define ldapuVd_simple_bind_s   ldap_simple_bind_s
#define ldapuVd_unbind          ldap_unbind
#define ldapuVd_search_s        ldap_search_s
#define ldapuVd_count_entries   ldap_count_entries
#define ldapuVd_first_entry     ldap_first_entry
#define ldapuVd_next_entry      ldap_next_entry
#define ldapuVd_get_dn          ldap_get_dn
#define ldapuVd_first_attribute ldap_first_attribute
#define ldapuVd_next_attribute  ldap_next_attribute
#define ldapuVd_get_values      ldap_get_values
#define ldapuVd_get_values_len  ldap_get_values_len

#endif

/* Several functions in the standard LDAP API have no LDAP* parameter,
   but all the VTable functions do.  Here are some little functions that
   make up the difference, by ignoring their LDAP* parameter:
*/
static int LDAP_CALL LDAP_CALLBACK
ldapuVd_msgfree( LDAP *ld, LDAPMessage *chain )
{
    return ldap_msgfree (chain);
}

static void LDAP_CALL LDAP_CALLBACK
ldapuVd_memfree( LDAP *ld, void *dn )
{
    ldap_memfree (dn);
}

static void LDAP_CALL LDAP_CALLBACK
ldapuVd_ber_free( LDAP *ld, BerElement *ber, int freebuf )
{
    ldap_ber_free (ber, freebuf);
}

static void LDAP_CALL LDAP_CALLBACK
ldapuVd_value_free( LDAP *ld, char **vals )
{
    ldap_value_free (vals);
}

static void LDAP_CALL LDAP_CALLBACK
ldapuVd_value_free_len( LDAP *ld, struct berval **vals )
{
    ldap_value_free_len (vals);
}

static LDAPUVTable_t ldapu_VTable = {
/* By default, the VTable points to the standard LDAP API. */
#ifdef USE_LDAP_SSL
    ldapuVd_ssl_init,
#else
    ldapuVd_init,
#endif
    ldapuVd_set_option,
    ldapuVd_simple_bind_s,
    ldapuVd_unbind,
    ldapuVd_search_s,
    ldapuVd_count_entries,
    ldapuVd_first_entry,
    ldapuVd_next_entry,
    ldapuVd_msgfree,
    ldapuVd_get_dn,
    ldapuVd_memfree,
    ldapuVd_first_attribute,
    ldapuVd_next_attribute,
    ldapuVd_ber_free,
    ldapuVd_get_values,
    ldapuVd_value_free,
    ldapuVd_get_values_len,
    ldapuVd_value_free_len
};

/* Replace ldapu_VTable.  Subsequently, ldaputil will call the
   functions in 'from' (not the LDAP API) to access the directory.
 */
void
ldapu_VTable_set (LDAPUVTable_t* from)
{
    if (from) {
	memcpy (&ldapu_VTable, from, sizeof(LDAPUVTable_t));
    }
}

#ifdef USE_LDAP_SSL
LDAP*
ldapu_ssl_init( const char *defhost, int defport, int defsecure )
{
    if (ldapu_VTable.ldapuV_ssl_init) {
	return ldapu_VTable.ldapuV_ssl_init (defhost, defport, defsecure);
    }
    return NULL;
}
#else
LDAP*
ldapu_init( const char *defhost, int defport )
{
    if (ldapu_VTable.ldapuV_init) {
	return ldapu_VTable.ldapuV_init (defhost, defport);
    }
    return NULL;
}
#endif

int
ldapu_set_option( LDAP *ld, int option, void *optdata )
{
    if (ldapu_VTable.ldapuV_set_option) {
	return ldapu_VTable.ldapuV_set_option (ld, option, optdata);
    }
    return LDAP_LOCAL_ERROR;
}

int
ldapu_simple_bind_s( LDAP *ld, const char *who, const char *passwd )
{
    if (ldapu_VTable.ldapuV_simple_bind_s) {
	return ldapu_VTable.ldapuV_simple_bind_s (ld, who, passwd);
    }
    return LDAP_LOCAL_ERROR;
}

int
ldapu_unbind( LDAP *ld )
{
    if (ldapu_VTable.ldapuV_unbind) {
	return ldapu_VTable.ldapuV_unbind (ld);
    }
    return LDAP_LOCAL_ERROR;
}

int
ldapu_search_s( LDAP *ld, const char *base, int scope,
	        const char *filter, char **attrs, int attrsonly, LDAPMessage **res )
{
    if (ldapu_VTable.ldapuV_search_s) {
	return ldapu_VTable.ldapuV_search_s (ld, base, scope, filter, attrs, attrsonly, res);
    }
    return LDAP_LOCAL_ERROR;
}

int
ldapu_count_entries( LDAP *ld, LDAPMessage *chain )
{
    if (ldapu_VTable.ldapuV_count_entries) {
	return ldapu_VTable.ldapuV_count_entries (ld, chain);
    }
    return 0;
}

LDAPMessage*
ldapu_first_entry( LDAP *ld, LDAPMessage *chain )
{
    if (ldapu_VTable.ldapuV_first_entry) {
	return ldapu_VTable.ldapuV_first_entry (ld, chain);
    }
    return NULL;
}

LDAPMessage*
ldapu_next_entry( LDAP *ld, LDAPMessage *entry )
{
    if (ldapu_VTable.ldapuV_next_entry) {
	return ldapu_VTable.ldapuV_next_entry (ld, entry);
    }
    return NULL;
}

int
ldapu_msgfree( LDAP* ld, LDAPMessage *chain )
{
    if (ldapu_VTable.ldapuV_msgfree) {
	return ldapu_VTable.ldapuV_msgfree (ld, chain);
    }
    return LDAP_SUCCESS;
}

char*
ldapu_get_dn( LDAP *ld, LDAPMessage *entry )
{
    if (ldapu_VTable.ldapuV_get_dn) {
	return ldapu_VTable.ldapuV_get_dn (ld, entry);
    }
    return NULL;
}

void
ldapu_memfree( LDAP* ld, void *p )
{
    if (ldapu_VTable.ldapuV_memfree) {
	ldapu_VTable.ldapuV_memfree (ld, p);
    }
}

char*
ldapu_first_attribute( LDAP *ld, LDAPMessage *entry, BerElement **ber )
{
    if (ldapu_VTable.ldapuV_first_attribute) {
	return ldapu_VTable.ldapuV_first_attribute (ld, entry, ber);
    }
    return NULL;
}

char*
ldapu_next_attribute( LDAP *ld, LDAPMessage *entry, BerElement *ber )
{
    if (ldapu_VTable.ldapuV_next_attribute) {
	return ldapu_VTable.ldapuV_next_attribute (ld, entry, ber);
    }
    return NULL;
}

void
ldapu_ber_free( LDAP* ld, BerElement *ber, int freebuf )
{
    if (ldapu_VTable.ldapuV_ber_free) {
	ldapu_VTable.ldapuV_ber_free (ld, ber, freebuf);
    }
}

char**
ldapu_get_values( LDAP *ld, LDAPMessage *entry, const char *desc )
{
    if (ldapu_VTable.ldapuV_get_values) {
	return ldapu_VTable.ldapuV_get_values (ld, entry, desc);
    } else if (!ldapu_VTable.ldapuV_value_free
	       && ldapu_VTable.ldapuV_get_values_len) {
	auto struct berval** bvals =
		  ldapu_VTable.ldapuV_get_values_len (ld, entry, desc);
	if (bvals) {
	    auto char** vals = (char**)
	      ldapu_malloc ((ldap_count_values_len (bvals) + 1)
			    * sizeof(char*));
	    if (vals) {
		auto char** val;
		auto struct berval** bval;
		for (val = vals, bval = bvals; *bval; ++val, ++bval) {
		    auto const size_t len = (*bval)->bv_len;
		    *val = (char*) ldapu_malloc (len + 1);
		    memcpy (*val, (*bval)->bv_val, len);
		    (*val)[len] = '\0';
		}
		*val = NULL;
		ldapu_value_free_len(ld, bvals);
		return vals;
	    }
	}
	ldapu_value_free_len(ld, bvals);
    }
    return NULL;
}

void
ldapu_value_free( LDAP *ld, char **vals )
{
    if (ldapu_VTable.ldapuV_value_free) {
	ldapu_VTable.ldapuV_value_free (ld, vals);
    } else if (!ldapu_VTable.ldapuV_get_values && vals) {
	auto char** val;
	for (val = vals; *val; ++val) {
	    free (*val);
	}
	free (vals);
    }
}

struct berval**
ldapu_get_values_len( LDAP *ld, LDAPMessage *entry, const char *desc )
{
    if (ldapu_VTable.ldapuV_get_values_len) {
	return ldapu_VTable.ldapuV_get_values_len (ld, entry, desc);
    } else if (!ldapu_VTable.ldapuV_value_free_len
	       && ldapu_VTable.ldapuV_get_values) {
	auto char** vals =
		  ldapu_VTable.ldapuV_get_values (ld, entry, desc);
	if (vals) {
	    auto struct berval** bvals = (struct berval**)
	      ldapu_malloc ((ldap_count_values (vals) + 1)
			    * sizeof(struct berval*));
	    if (bvals) {
		auto char** val;
		auto struct berval** bval;
		for (val = vals, bval = bvals; *val; ++val, ++bval) {
		    auto const size_t len = strlen(*val);
		    *bval = (struct berval*) ldapu_malloc (sizeof(struct berval) + len);
		    (*bval)->bv_len = len;
		    (*bval)->bv_val = ((char*)(*bval)) + sizeof(struct berval);
		    memcpy ((*bval)->bv_val, *val, len);
		}
		*bval = NULL;
		return bvals;
	    }
	}
    }
    return NULL;
}

void
ldapu_value_free_len( LDAP *ld, struct berval **vals )
{
    if (ldapu_VTable.ldapuV_value_free_len) {
	ldapu_VTable.ldapuV_value_free_len (ld, vals);
    } else if (!ldapu_VTable.ldapuV_get_values_len && vals) {
	auto struct berval** val;
	for (val = vals; *val; ++val) {
	    free (*val);
	}
	free (vals);
    }
}
