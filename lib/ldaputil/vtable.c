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

#include "ldaputili.h"
#include <ldap.h>

static LDAPUVTable_t ldapu_VTable = {0};

/* Replace ldapu_VTable.  Subsequently, ldaputil will call the
   functions in 'from' (not the LDAP API) to access the directory.
 */
void
ldapu_VTable_set(LDAPUVTable_t *from)
{
    if (from) {
        memcpy(&ldapu_VTable, from, sizeof(LDAPUVTable_t));
    }
}

int
ldapu_set_option(LDAP *ld, int option, void *optdata)
{
    if (ldapu_VTable.ldapuV_set_option) {
        return ldapu_VTable.ldapuV_set_option(ld, option, optdata);
    }
    return LDAP_LOCAL_ERROR;
}

int
ldapu_simple_bind_s(LDAP *ld, const char *who, const char *passwd)
{
    if (ldapu_VTable.ldapuV_simple_bind_s) {
        return ldapu_VTable.ldapuV_simple_bind_s(ld, who, passwd);
    }
    return LDAP_LOCAL_ERROR;
}

int
ldapu_unbind(LDAP *ld)
{
    if (ldapu_VTable.ldapuV_unbind) {
        return ldapu_VTable.ldapuV_unbind(ld);
    }
    return LDAP_LOCAL_ERROR;
}

int
ldapu_search_s(LDAP *ld, const char *base, int scope, const char *filter, char **attrs, int attrsonly, LDAPMessage **res)
{
    if (ldapu_VTable.ldapuV_search_s) {
        return ldapu_VTable.ldapuV_search_s(ld, base, scope, filter, attrs, attrsonly, res);
    }
    return LDAP_LOCAL_ERROR;
}

int
ldapu_count_entries(LDAP *ld, LDAPMessage *chain)
{
    if (ldapu_VTable.ldapuV_count_entries) {
        return ldapu_VTable.ldapuV_count_entries(ld, chain);
    }
    return 0;
}

LDAPMessage *
ldapu_first_entry(LDAP *ld, LDAPMessage *chain)
{
    if (ldapu_VTable.ldapuV_first_entry) {
        return ldapu_VTable.ldapuV_first_entry(ld, chain);
    }
    return NULL;
}

LDAPMessage *
ldapu_next_entry(LDAP *ld, LDAPMessage *entry)
{
    if (ldapu_VTable.ldapuV_next_entry) {
        return ldapu_VTable.ldapuV_next_entry(ld, entry);
    }
    return NULL;
}

int
ldapu_msgfree(LDAP *ld, LDAPMessage *chain)
{
    if (ldapu_VTable.ldapuV_msgfree) {
        return ldapu_VTable.ldapuV_msgfree(ld, chain);
    }
    return LDAP_SUCCESS;
}

char *
ldapu_get_dn(LDAP *ld, LDAPMessage *entry)
{
    if (ldapu_VTable.ldapuV_get_dn) {
        return ldapu_VTable.ldapuV_get_dn(ld, entry);
    }
    return NULL;
}

void
ldapu_memfree(LDAP *ld, void *p)
{
    if (ldapu_VTable.ldapuV_memfree) {
        ldapu_VTable.ldapuV_memfree(ld, p);
    }
}

char *
ldapu_first_attribute(LDAP *ld, LDAPMessage *entry, BerElement **ber)
{
    if (ldapu_VTable.ldapuV_first_attribute) {
        return ldapu_VTable.ldapuV_first_attribute(ld, entry, ber);
    }
    return NULL;
}

char *
ldapu_next_attribute(LDAP *ld, LDAPMessage *entry, BerElement *ber)
{
    if (ldapu_VTable.ldapuV_next_attribute) {
        return ldapu_VTable.ldapuV_next_attribute(ld, entry, ber);
    }
    return NULL;
}

void
ldapu_ber_free(LDAP *ld, BerElement *ber, int freebuf)
{
    if (ldapu_VTable.ldapuV_ber_free) {
        ldapu_VTable.ldapuV_ber_free(ld, ber, freebuf);
    }
}

char **
ldapu_get_values(LDAP *ld, LDAPMessage *entry, const char *desc)
{
    if (ldapu_VTable.ldapuV_get_values) {
        return ldapu_VTable.ldapuV_get_values(ld, entry, desc);
    } else if (!ldapu_VTable.ldapuV_value_free && ldapu_VTable.ldapuV_get_values_len) {
        auto struct berval **bvals =
            ldapu_VTable.ldapuV_get_values_len(ld, entry, desc);
        if (bvals) {
            auto char **vals = (char **)
                ldapu_malloc((ldap_count_values_len(bvals) + 1) * sizeof(char *));
            if (vals) {
                auto char **val;
                auto struct berval **bval;
                for (val = vals, bval = bvals; *bval; ++val, ++bval) {
                    auto const size_t len = (*bval)->bv_len;
                    *val = (char *)ldapu_malloc(len + 1);
                    memcpy(*val, (*bval)->bv_val, len);
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
ldapu_value_free(LDAP *ld, char **vals)
{
    if (ldapu_VTable.ldapuV_value_free) {
        ldapu_VTable.ldapuV_value_free(ld, vals);
    } else if (!ldapu_VTable.ldapuV_get_values && vals) {
        auto char **val;
        for (val = vals; *val; ++val) {
            free(*val);
        }
        free(vals);
    }
}

struct berval **
ldapu_get_values_len(LDAP *ld, LDAPMessage *entry, const char *desc)
{
    if (ldapu_VTable.ldapuV_get_values_len) {
        return ldapu_VTable.ldapuV_get_values_len(ld, entry, desc);
    }
    return NULL;
}

void
ldapu_value_free_len(LDAP *ld, struct berval **vals)
{
    if (ldapu_VTable.ldapuV_value_free_len) {
        ldapu_VTable.ldapuV_value_free_len(ld, vals);
    } else if (!ldapu_VTable.ldapuV_get_values_len && vals) {
        auto struct berval **val;
        for (val = vals; *val; ++val) {
            free(*val);
        }
        free(vals);
    }
}
