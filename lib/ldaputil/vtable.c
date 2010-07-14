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

static LDAPUVTable_t ldapu_VTable = {0};

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
