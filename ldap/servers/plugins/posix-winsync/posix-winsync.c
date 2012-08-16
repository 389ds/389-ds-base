/** Author: Carsten Grzemba grzemba@contac-dt.de>
 *
 * Copyright (C) 2011 contac Datentechnik GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 only
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 $Id: posix-winsync.c 40 2011-06-10 08:28:56Z grzemba $
 **/

/* 
 * - AD needs for Posix attributes a NIS Domainname, this software expect a attribute nisDomain with the name in a upper container on DS side
 * - currently the winsync API has no callbacks for new created entries on DS side

 compile:
 gcc -g -shared -m64 -fPIC -c -D WINSYNC_TEST_POSIX \
               -I ../fedora-ds/ds/ldap/servers/slapd  \
               -I ../fedora-ds/ds/ldap/servers/plugins/replication \
               -I /usr/include/mps posix-winsync.c
 link:
 ld -G posix-winsync.o -o libposix-winsync.so

 configure DS with

 dn: cn=Posix Winsync API,cn=plugins,cn=config
 objectclass: top
 objectclass: nsSlapdPlugin
 objectclass: extensibleObject
 cn: Posix Winsync API
 nsslapd-pluginpath: libposix-winsync
 nsslapd-plugininitfunc: posix_winsync_plugin_init
 nsslapd-plugintype: preoperation
 nsslapd-pluginenabled: on
 nsslapd-plugin-depends-on-type: database
 nsslapd-pluginDescription: Sync Posix Attributes for users and groups between AD and DS if available and user lock/unlock
 nsslapd-pluginVendor: contac Datentechnik GmbH
 nsslapd-pluginId: posix-winsync-plugin
 nsslapd-pluginVersion: POSIX/1.0

 AFTER that make new replication aggrements

 for details see: Red_Hat_Directory_Server-8.2-Plug-in_Guide-en-US.pdf
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef WINSYNC_TEST_POSIX
#include <slapi-plugin.h>
#include "winsync-plugin.h"
#else
#include <dirsrv/slapi-plugin.h>
#include <dirsrv/winsync-plugin.h>
#endif
#include <plstr.h>
#include <strings.h>
#include <stdlib.h>
#include "posix-wsp-ident.h"
#include "posix-group-func.h"

#define MEMBEROFTASK "memberof task"
Slapi_Value **
valueset_get_valuearray(const Slapi_ValueSet *vs); /* stolen from proto-slap.h */
void *
posix_winsync_get_plugin_identity();
void *
posix_winsync_agmt_init(const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree);

/**
 * Plugin identifiers
 */
static Slapi_PluginDesc posix_winsync_pdesc =
    { "posix-winsync-plugin", VENDOR, DS_PACKAGE_VERSION,
      "Sync Posix Attributs for users and groups between AD and DS if available" };
typedef struct _windows_attr_map
{
    char *windows_attribute_name;
    char *ldap_attribute_name;
} windows_attribute_map;

static windows_attribute_map user_attribute_map[] = { { "unixHomeDirectory", "homeDirectory" },
                                                      { "loginShell", "loginShell" },
                                                      { "uidNumber", "uidNumber" },
                                                      { "gidNumber", "gidNumber" },
                                                      { "gecos", "gecos" },
                                                      { NULL, NULL } };

static windows_attribute_map user_mssfu_attribute_map[] =
    { { "msSFU30homedirectory", "homeDirectory" },
      { "msSFU30loginshell", "loginShell" },
      { "msSFU30uidnumber", "uidNumber" },
      { "msSFU30gidnumber", "gidNumber" },
      { "msSFU30gecos", "gecos" },
      { NULL, NULL } };

static windows_attribute_map group_attribute_map[] = { { "memberUid", "memberUid" },
                                                       { "gidNumber", "gidNumber" },
                                                       { NULL, NULL } };

static windows_attribute_map group_mssfu_attribute_map[] = { { "msSFU30memberUid", "memberUid" },
                                                             { "msSFU30gidNumber", "gidNumber" },
                                                             { NULL, NULL } };

static char *posix_winsync_plugin_name = POSIX_WINSYNC_PLUGIN_NAME;

enum
{
    ACCT_DISABLE_INVALID, /* the invalid value */
    ACCT_DISABLE_NONE, /* do not sync acct disable status */
    ACCT_DISABLE_TO_AD, /* sync only from ds to ad */
    ACCT_DISABLE_TO_DS, /* sync only from ad to ds */
    ACCT_DISABLE_BOTH
/* bi-directional sync */
};

/*
 * Check if the given entry has account lock on (i.e. entry is disabled)
 * Mostly copied from check_account_lock in the server code.
 * Returns: 0 - account is disabled (lock == "true")
 *          1 - account is enabled (lock == "false" or empty)
 *         -1 - some sort of error
 */
static int
check_account_lock(Slapi_Entry *ds_entry, int *isvirt)
{
    int rc = 1;
    Slapi_ValueSet *values = NULL;
    int type_name_disposition = 0;
    char *actual_type_name = NULL;
    int attr_free_flags = 0;
    char *strval;

    /* first, see if the attribute is a "real" attribute */
    strval = slapi_entry_attr_get_charptr(ds_entry, "nsAccountLock");
    if (strval) { /* value is real */
        *isvirt = 0; /* value is real */
        rc = 1; /* default to enabled */
        if (PL_strncasecmp(strval, "true", 4) == 0) {
            rc = 0; /* account is disabled */
        }
        slapi_ch_free_string(&strval);
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "<-- check_account_lock - entry [%s] has real "
                            "attribute nsAccountLock and entry %s locked\n",
                        slapi_entry_get_dn_const(ds_entry), rc ? "is not" : "is");
        return rc;
    }

    rc = slapi_vattr_values_get(ds_entry, "nsAccountLock", &values, &type_name_disposition,
                                &actual_type_name, SLAPI_VIRTUALATTRS_REQUEST_POINTERS,
                                &attr_free_flags);
    if (rc == 0) {
        Slapi_Value *v = NULL;
        const struct berval *bvp = NULL;

        rc = 1; /* default is enabled */
        *isvirt = 1; /* value is virtual */
        if ((slapi_valueset_first_value(values, &v) != -1) &&
            ((bvp = slapi_value_get_berval(v)) != NULL)) {
            if ((bvp != NULL) && (PL_strncasecmp(bvp->bv_val, "true", 4) == 0)) {
                slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);
                rc = 0; /* account is disabled */
            }
        }

        if (values != NULL) {
            slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);
        }
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "<-- check_account_lock - entry [%s] has virtual "
                            "attribute nsAccountLock and entry %s locked\n",
                        slapi_entry_get_dn_const(ds_entry), rc ? "is not" : "is");
    } else {
        rc = 1; /* no attr == entry is enabled */
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "<-- check_account_lock - entry [%s] does not "
                            "have attribute nsAccountLock - entry %s locked\n",
                        slapi_entry_get_dn_const(ds_entry), rc ? "is not" : "is");
    }

    return rc;
}

/*
 * This can be used either in the to ad direction or the to ds direction, since in both
 * cases we have to read both entries and compare the values.
 * ad_entry - entry from AD
 * ds_entry - entry from DS
 * direction - either ACCT_DISABLE_TO_AD or ACCT_DISABLE_TO_DS
 *
 * If smods is given, this is the list of mods to send in the given direction.  The
 * appropriate modify operation will be added to this list or changed to the correct
 * value if it already exists.
 * Otherwise, if a destination entry is given, the value will be written into
 * that entry.
 */
static void
sync_acct_disable(void *cbdata, /* the usual domain config data */
    const Slapi_Entry *ad_entry, /* the AD entry */
    Slapi_Entry *ds_entry, /* the DS entry */
    int direction, /* the direction - TO_AD or TO_DS */
    Slapi_Entry *update_entry, /* the entry to update for ADDs */
    Slapi_Mods *smods, /* the mod list for MODIFYs */
    int *do_modify /* if not NULL, set this to true if mods were added */
)
{
    int ds_is_enabled = 1; /* default to true */
    int ad_is_enabled = 1; /* default to true */
    unsigned long adval = 0; /* raw account val from ad entry */
    int isvirt = 0;

    /* get the account lock state of the ds entry */
    if (0 == check_account_lock(ds_entry, &isvirt)) {
        ds_is_enabled = 0;
    }
    if (isvirt)
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "<-- sync_acct_disable - %s DS nsaccountlock is virtual!!!!\n",
                        slapi_entry_get_dn_const(ds_entry));
    /* get the account lock state of the ad entry */
    adval = slapi_entry_attr_get_ulong(ad_entry, "UserAccountControl");
    if (adval & 0x2) {
        /* account is disabled */
        ad_is_enabled = 0;
    }

    if (ad_is_enabled == ds_is_enabled) { /* both have same value - nothing to do */
        return;
    }

    /* have to enable or disable */
    if (direction == ACCT_DISABLE_TO_AD) {
        unsigned long mask;
        /* set the mod or entry */
        if (ds_is_enabled) {
            mask = ~0x2;
            adval &= mask; /* unset the 0x2 disable bit */
        } else {
            mask = 0x2;
            adval |= mask; /* set the 0x2 disable bit */
        }
        if (update_entry) {
            slapi_entry_attr_set_ulong(update_entry, "userAccountControl", adval);
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "<-- sync_acct_disable - %s AD account [%s] - "
                                "new value is [%ld]\n", (ds_is_enabled) ? "enabled" : "disabled",
                            slapi_entry_get_dn_const(update_entry), adval);
        } else {
            /* iterate through the mods - if there is already a mod
             for userAccountControl, change it - otherwise, add it */
            char acctvalstr[32];
            LDAPMod *mod = NULL;
            struct berval *mod_bval = NULL;
            for (mod = slapi_mods_get_first_mod(smods); mod; mod = slapi_mods_get_next_mod(smods)) {
                if (!PL_strcasecmp(mod->mod_type, "userAccountControl") && mod->mod_bvalues
                    && mod->mod_bvalues[0]) {
                    mod_bval = mod->mod_bvalues[0];
                    /* mod_bval points directly to value inside mod list */
                    break;
                }
            }
            if (!mod_bval) { /* not found - add it */
                struct berval tmpbval = { 0, NULL };
                Slapi_Mod *smod = slapi_mod_new();
                slapi_mod_init(smod, 1); /* one element */
                slapi_mod_set_type(smod, "userAccountControl");
                slapi_mod_set_operation(smod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
                slapi_mod_add_value(smod, &tmpbval);
                /* add_value makes a copy of the bval - so let's get a pointer
                 to that new value - we will change the bval in place */
                mod_bval = slapi_mod_get_first_value(smod);
                /* mod_bval points directly to value inside mod list */
                /* now add the new mod to smods */
                slapi_mods_add_ldapmod(smods, slapi_mod_get_ldapmod_passout(smod));
                /* smods now owns the ldapmod */
                slapi_mod_free(&smod);
                if (do_modify) {
                    *do_modify = 1; /* added mods */
                }
            }
            if (mod_bval) {
                /* this is where we set or update the actual value
                 mod_bval points directly into the mod list we are
                 sending */
                if (mod_bval->bv_val && (mod_bval->bv_len > 0)) {
                    /* get the old val */
                    adval = strtol(mod_bval->bv_val, NULL, 10);
                }
                if (ds_is_enabled) {
                    mask = ~0x2;
                    adval &= mask; /* unset the 0x2 disable bit */
                } else {
                    mask = 0x2;
                    adval |= mask; /* set the 0x2 disable bit */
                }
                PR_snprintf(acctvalstr, sizeof(acctvalstr), "%lu", adval);
                slapi_ch_free_string(&mod_bval->bv_val);
                mod_bval->bv_val = slapi_ch_strdup(acctvalstr);
                mod_bval->bv_len = strlen(acctvalstr);
            }
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "<-- sync_acct_disable - %s AD account [%s] - "
                                "new value is [%ld]\n", (ds_is_enabled) ? "enabled" : "disabled",
                            slapi_entry_get_dn_const(ad_entry), adval);
        }
    }

    if (direction == ACCT_DISABLE_TO_DS) {
        char *attrtype = NULL;
        char *attrval;
        char *val = NULL;

        attrtype = (isvirt) ? "nsRoleDN" : "nsAccountLock";
        if (ad_is_enabled) {
            attrval = NULL; /* will delete the value */
        } else {
            if (isvirt) {
                val = slapi_create_dn_string("cn=nsManagedDisabledRole,%s",
                                             slapi_sdn_get_dn(posix_winsync_config_get_suffix()));
                attrval = val;
            } else {
                attrval = "true";
            }
        }

        if (update_entry) {
            slapi_entry_attr_set_charptr(update_entry, attrtype, attrval);
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "<-- sync_acct_disable - %s DS account [%s]\n", (ad_is_enabled)
                                ? "enable" : "disable", slapi_entry_get_dn_const(ds_entry));
        } else { /* do mod */
            Slapi_Mod *smod = slapi_mod_new();

            slapi_mod_init(smod, 1); /* one element */
            slapi_mod_set_type(smod, attrtype);
            if (attrval == NULL) {
                slapi_mod_set_operation(smod, LDAP_MOD_DELETE | LDAP_MOD_BVALUES);
            } else {
                Slapi_Value *v = NULL;
                v = slapi_value_new_string(attrval);
                slapi_mod_set_operation(smod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
                slapi_mod_add_value(smod, slapi_value_get_berval(v));
                slapi_value_free(&v);
            }
            slapi_mods_add_ldapmod(smods, slapi_mod_get_ldapmod_passout(smod));
            slapi_mod_free(&smod);

            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "<-- sync_acct_disable - %s DS account [%s]\n", (ad_is_enabled)
                                ? "enable" : "disable", slapi_entry_get_dn_const(ds_entry));
            if (do_modify) {
                *do_modify = 1; /* added mods */
            }
        }
        slapi_ch_free_string(&val);
    }
    return;
}

/* Returns non-zero if the attribute value sets are identical.  */
static int
attr_compare_equal(Slapi_Attr *a, Slapi_Attr *b)
{
    int i = 0;
    Slapi_Value *va = NULL;

    /* Iterate through values in attr a and search for each in attr b */
    for (i = slapi_attr_first_value(a, &va); va && (i != -1); i = slapi_attr_next_value(a, i, &va)) {

        /* Compare the entire attribute value */
        if (slapi_attr_value_find(b, slapi_value_get_berval(va)) != 0) {
            return 0;
        }
    }
    return 1;
}

static int
addNisDomainName(Slapi_Mod *smod, const Slapi_Entry *ds_entry)
{
    Slapi_Entry *entry = NULL;
    char *type_NisDomain = "nisDomain";
    Slapi_PBlock * pb;
    int rc = -1;

    char* nisdomainname = NULL;
    Slapi_DN* entry_sdn = slapi_entry_get_sdn((Slapi_Entry *) ds_entry);
    Slapi_DN* subtree_sdn = slapi_sdn_new();
    Slapi_DN *childparent = slapi_sdn_new();
    struct berval **vals;

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "addNisDomainName start DN:%s\n",
                    slapi_sdn_get_dn(entry_sdn));
    slapi_sdn_get_parent(entry_sdn, subtree_sdn);
    pb = slapi_pblock_new();
    do {
        Slapi_Attr *attr = NULL;
        char *nisDomainAttr[] = { type_NisDomain, NULL };

        slapi_sdn_get_parent(subtree_sdn, childparent);
        if (slapi_sdn_isempty(childparent)) {
            rc = -1;
            break;
        }
        rc = slapi_search_internal_get_entry(childparent, nisDomainAttr, &entry,
                                             posix_winsync_get_plugin_identity());
        if (rc == 0) {
            if (rc == 0 && entry) {
                rc = slapi_entry_attr_find(entry, type_NisDomain, &attr);
                if (attr) {
                    rc = slapi_attr_get_bervals_copy(attr, &vals);
                    break;
                } else {
                    rc = LDAP_NO_SUCH_ATTRIBUTE;
                }
            }
        }
        slapi_sdn_copy(childparent, subtree_sdn);
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "addNisDomainName iterate DN:%s\n", slapi_sdn_get_dn(subtree_sdn));
        slapi_entry_free(entry);
        entry = NULL;
    } while (PR_TRUE);
    slapi_pblock_destroy(pb);
    if (rc != 0) {
        slapi_log_error(SLAPI_LOG_REPL, posix_winsync_plugin_name,
                        "addNisDomainName: no nisdomainname found in %s, LDAP Err%d\n",
                        slapi_sdn_get_dn(subtree_sdn), rc);
    } else {
        slapi_mod_init(smod, 1);
        slapi_mod_set_type(smod, "msSFU30NisDomain");
        slapi_mod_set_operation(smod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
        slapi_mod_add_value(smod, vals[0]);
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "addNisDomainName NisDomain %s found in DN:%s\n", vals[0]->bv_val,
                        slapi_sdn_get_dn(childparent));
        if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN))
            slapi_mod_dump((LDAPMod*) slapi_mod_get_ldapmod_byref(smod), 0);
        ber_bvecfree(vals);

    }
    slapi_sdn_free(&childparent);
    slapi_entry_free(entry);
    entry = NULL;
    slapi_sdn_free(&subtree_sdn);

    slapi_ch_free_string(&nisdomainname);
    return rc;
}

static void
posix_winsync_dirsync_search_params_cb(void *cbdata, const char *agmt_dn, char **base, int *scope,
    char **filter, char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_dirsync_search_params_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_dirsync_search_params_cb -- end\n");

    return;
}

/* called before searching for a single entry from AD - agmt_dn will be NULL */
static void
posix_winsync_pre_ad_search_cb(void *cbdata, const char *agmt_dn, char **base, int *scope,
    char **filter, char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_pre_ad_search_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_pre_ad_search_cb -- end\n");

    return;
}

/* called before an internal search to get a single DS entry - agmt_dn will be NULL */
static void
posix_winsync_pre_ds_search_entry_cb(void *cbdata, const char *agmt_dn, char **base, int *scope,
    char **filter, char ***attrs, LDAPControl ***serverctrls)
{
    /*
     char *tmpbase=slapi_ch_strdup(*base);
     char *d = *base;
     char *s = tmpbase;
     int i=0;
     */
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "--> _pre_ds_search_cb -- begin\n");
    /* skip the first subtree container ou=xyz, */
    /*    if (strlen(*base) > 3) {
     s++;
     while(*s !='\0'){
     if (((*(s) == ',') || (*(s) == ';' )) && (*((s)-1) != '\\')){
     s++;
     while(*s !='\0'){
     *d++ = *s++;
     }
     *d='\0';
     break;
     }
     s++;
     }
     }
     */
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "-- _pre_ds_search_cb - base [%s] "
                        "scope [%d] filter [%s]\n", *base, *scope, *filter);
    /*    slapi_ch_free_string(&tmpbase); */
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "<-- _pre_ds_search_cb -- end\n");
    return;
}

/* called before the total update to get all entries from the DS to sync to AD */
static void
posix_winsync_pre_ds_search_all_cb(void *cbdata, const char *agmt_dn, char **base, int *scope,
    char **filter, char ***attrs, LDAPControl ***serverctrls)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_pre_ds_search_all_cb -- orig filter [%s] -- begin\n",
                    ((filter && *filter) ? *filter : "NULL"));

    /*    slapi_ch_free_string(filter);
     *filter = slapi_ch_strdup("(|(objectclass=posixaccount)(objectclass=posixgroup))");
     */
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_pre_ds_search_all_cb -- end\n");

    return;
}

static void
posix_winsync_pre_ad_mod_user_cb(void *cbdata, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry,
    Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify)
{
    LDAPMod *mod = NULL;
    int rc = 0;
    Slapi_Attr *attr = NULL;
    windows_attribute_map *attr_map = user_attribute_map;

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_pre_ad_mod_user_cb -- begin DS account [%s]\n",
                    slapi_entry_get_dn_const(ds_entry));
    if (posix_winsync_config_get_msSFUSchema()) {
        attr_map = user_mssfu_attribute_map;
    }

    /* called if init Replica: add nisDomain, uidnumber, ... if avail */
    for (rc = slapi_entry_first_attr(ds_entry, &attr); rc == 0;
         rc = slapi_entry_next_attr(ds_entry, attr, &attr)) {
        char *type = NULL;

        size_t i = 0;

        slapi_attr_get_type(attr, &type);
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "_pre_ad_mod_user_cb -- check modify type %s\n", type);
        while (attr_map[i].windows_attribute_name != NULL) {
            if (0 == slapi_attr_type_cmp(type, attr_map[i].ldap_attribute_name,
                                         SLAPI_TYPE_CMP_SUBTYPE)) {
                Slapi_Attr *ad_attr = NULL;
                Slapi_ValueSet *vs = NULL;
                char *ad_type = NULL;
                int is_present_local;

                slapi_attr_get_valueset(attr, &vs);
                ad_type = slapi_ch_strdup(attr_map[i].windows_attribute_name);
                slapi_entry_attr_find(ad_entry, ad_type, &ad_attr);
                is_present_local = (NULL == ad_attr) ? 0 : 1;
                if (is_present_local) {
                    int values_equal = 0;
                    values_equal = attr_compare_equal(attr, ad_attr);
                    if (!values_equal) {
                        slapi_log_error(
                                        SLAPI_LOG_PLUGIN,
                                        posix_winsync_plugin_name,
                                        "_pre_ad_mod_user_cb -- update mods: %s, %s : values are different -> modify\n",
                                        slapi_sdn_get_dn(slapi_entry_get_sdn_const(ds_entry)),
                                        ad_type);

                        slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE, ad_type,
                                                  valueset_get_valuearray(vs));
                        *do_modify = 1;
                    }
                } else {
                    slapi_mods_add_mod_values(smods, LDAP_MOD_ADD, ad_type,
                                              valueset_get_valuearray(vs));
                    if (0 == slapi_attr_type_cmp(type, "uidNumber", SLAPI_TYPE_CMP_SUBTYPE)) {
                        Slapi_Mod *mysmod = slapi_mod_new();
                        addNisDomainName(mysmod, ds_entry);
                        slapi_mods_add_ldapmod(smods, slapi_mod_get_ldapmod_passout(mysmod));
                        slapi_mod_free(&mysmod);
                    }
                    *do_modify = 1;
                }
                slapi_ch_free((void**) &ad_type);
                slapi_valueset_free(vs);

                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                "_pre_ad_mod_user_cb -- add modify %s DS account [%s]\n",
                                attr_map[i].windows_attribute_name,
                                slapi_entry_get_dn_const(ds_entry));

            }
            i++;
        }
        if (0 == slapi_attr_type_cmp(type, "nsAccountLock", SLAPI_TYPE_CMP_SUBTYPE))
            sync_acct_disable(cbdata, rawentry, ds_entry, ACCT_DISABLE_TO_AD, NULL, smods,
                              do_modify);
    }
    if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN)) {
        for (mod = slapi_mods_get_first_mod(smods); mod; mod = slapi_mods_get_next_mod(smods)) {
            slapi_mod_dump(mod, 0);
        }
    }
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_pre_ad_mod_user_cb -- end\n");

    return;
}

static void
posix_winsync_pre_ad_mod_group_cb(void *cbdata, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry,
    Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify)
{
    LDAPMod *mod = NULL;
    int rc = 0;
    Slapi_Attr *attr = NULL;
    windows_attribute_map *attr_map = group_attribute_map;

    if (posix_winsync_config_get_msSFUSchema())
        attr_map = group_mssfu_attribute_map;

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> _pre_ad_mod_group_cb -- begin DS account [%s]\n",
                    slapi_entry_get_dn_const(ds_entry));

    /* called if init Replica: add nisDomain, gidnumber, memberuid, if avail */
    for (rc = slapi_entry_first_attr(ds_entry, &attr); rc == 0;
         rc = slapi_entry_next_attr(ds_entry, attr, &attr)) {
        char *type = NULL;
        size_t i = 0;

        slapi_attr_get_type(attr, &type);
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "_pre_ad_mod_group_cb -- check modify type %s\n", type);
        while (attr_map[i].windows_attribute_name != NULL) {
            if (0 == slapi_attr_type_cmp(type, attr_map[i].ldap_attribute_name,
                                         SLAPI_TYPE_CMP_SUBTYPE)) {
                Slapi_Attr *ad_attr = NULL;
                Slapi_ValueSet *vs = NULL;
                char *ad_type = NULL;
                int is_present_local;

                slapi_attr_get_valueset(attr, &vs);
                ad_type = slapi_ch_strdup(attr_map[i].windows_attribute_name);
                slapi_entry_attr_find(ad_entry, ad_type, &ad_attr);
                is_present_local = (NULL == ad_attr) ? 0 : 1;
                if (is_present_local) {
                    int values_equal = 0;
                    values_equal = attr_compare_equal(attr, ad_attr);
                    if (!values_equal) {
                        slapi_log_error(
                                        SLAPI_LOG_PLUGIN,
                                        posix_winsync_plugin_name,
                                        "_pre_ad_mod_group_cb -- update mods: %s, %s : values are different -> modify\n",
                                        slapi_sdn_get_dn(slapi_entry_get_sdn_const(ds_entry)),
                                        ad_type);

                        slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE, ad_type,
                                                  valueset_get_valuearray(vs));
                        *do_modify = 1;
                    }
                } else {

                    slapi_mods_add_mod_values(smods, LDAP_MOD_ADD, ad_type,
                                              valueset_get_valuearray(vs));
                    if (0 == slapi_attr_type_cmp(type, "gidNumber", SLAPI_TYPE_CMP_SUBTYPE)) {
                        Slapi_Mod *mysmod = slapi_mod_new();
                        addNisDomainName(mysmod, ds_entry);
                        slapi_mods_add_ldapmod(smods, slapi_mod_get_ldapmod_passout(mysmod));
                        slapi_mod_free(&mysmod);
                    }
                    *do_modify = 1;
                }
                slapi_ch_free((void**) &ad_type);
                slapi_valueset_free(vs);

                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                "_pre_ad_mod_group_cb -- add modify %s DS account [%s]\n",
                                attr_map[i].windows_attribute_name,
                                slapi_entry_get_dn_const(ds_entry));
            }
            i++;
        }
    }
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "_pre_ad_mod_group_cb -- step\n");
    if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN)) {
        for (mod = slapi_mods_get_first_mod(smods); mod; mod = slapi_mods_get_next_mod(smods)) {
            slapi_mod_dump(mod, 0);
        }
    }
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- _pre_ad_mod_group_cb -- end\n");

    return;
}

static void
posix_winsync_pre_ds_mod_user_cb(void *cbdata, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry,
    Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify)
{
    LDAPMod* mod = NULL;
    Slapi_Attr *attr = NULL;
    int is_present_local = 0;
    int do_modify_local = 0;
    int rc;
    windows_attribute_map *attr_map = user_attribute_map;

    if (posix_winsync_config_get_msSFUSchema())
        attr_map = user_mssfu_attribute_map;

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> _pre_ds_mod_user_cb -- begin\n");

    /* add objectclass: posixAccount, uidnumber ,gidnumber ,homeDirectory, loginshell */
    /* in the ad to ds case we have no changelog, so we have to compare the entries */
    for (rc = slapi_entry_first_attr(ad_entry, &attr); rc == 0;
         rc = slapi_entry_next_attr(ad_entry, attr, &attr)) {
        char *type = NULL;
        size_t i = 0;

        slapi_attr_get_type(attr, &type);
        while (attr_map[i].windows_attribute_name != NULL) {
            if (0 == slapi_attr_type_cmp(type, attr_map[i].windows_attribute_name,
                                         SLAPI_TYPE_CMP_SUBTYPE)) {
                Slapi_Attr *local_attr = NULL;
                char *local_type = NULL;
                Slapi_ValueSet *vs = NULL;

                slapi_attr_get_valueset(attr, &vs);
                local_type = slapi_ch_strdup(attr_map[i].ldap_attribute_name);
                slapi_entry_attr_find(ds_entry, local_type, &local_attr);
                is_present_local = (NULL == local_attr) ? 0 : 1;
                if (is_present_local) {
                    int values_equal = 0;
                    values_equal = attr_compare_equal(attr, local_attr);
                    if (!values_equal) {
                        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                        "_pre_ds_mod_user_cb -- update mods: %s, %s : values are different -> modify\n",
                                        slapi_sdn_get_dn(slapi_entry_get_sdn_const(ds_entry)),
                                        local_type);

                        slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE, local_type,
                                                  valueset_get_valuearray(vs));
                        *do_modify = 1;
                    }
                } else {

                    slapi_mods_add_mod_values(smods, LDAP_MOD_ADD, local_type,
                                              valueset_get_valuearray(vs));
                    *do_modify = do_modify_local = 1;
                }
                slapi_valueset_free(vs);
                slapi_ch_free((void**) &local_type);
                /* what about if delete all values on windows ????? */
            }
            i++;
        }
    }
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- _pre_ds_mod_user_cb present %d modify %d\n", is_present_local,
                    do_modify_local);

    if (!is_present_local && do_modify_local) {
        Slapi_Attr *oc_attr = NULL;
        Slapi_Value *voc = slapi_value_new();

        slapi_value_init_string(voc, "posixAccount");
        rc = slapi_entry_attr_find(ds_entry, "objectClass", &oc_attr);
        if (rc == 0) {
            const struct berval *bv = slapi_value_get_berval(voc);
            if (bv && slapi_attr_value_find(oc_attr, bv) != 0) {
                Slapi_ValueSet *oc_vs = slapi_valueset_new();
                Slapi_Value *oc_nv = slapi_value_new();

                slapi_attr_get_valueset(oc_attr, &oc_vs);
                slapi_value_init_string(oc_nv, "posixAccount");
                slapi_valueset_add_value(oc_vs, oc_nv);
                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                "<-- _pre_ds_mod_user_cb add oc:posixAccount\n");

                slapi_value_init_string(voc, "shadowAccount");
                if (slapi_attr_value_find(oc_attr, slapi_value_get_berval(voc)) != 0) {
                    Slapi_Value *oc_nv = slapi_value_new();

                    slapi_value_init_string(oc_nv, "shadowAccount");
                    slapi_valueset_add_value(oc_vs, oc_nv);
                    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                    "<-- _pre_ds_mod_user_cb add oc:shadowAccount\n");
                }
                slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE, "objectClass",
                                          valueset_get_valuearray(oc_vs));
                slapi_value_free(&oc_nv);
                slapi_valueset_free(oc_vs);
            }
        }
        slapi_value_free(&voc);
    }
    sync_acct_disable(cbdata, rawentry, ds_entry, ACCT_DISABLE_TO_DS, NULL, smods, do_modify);
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "<-- _pre_ds_mod_user_cb %s %s\n",
                    slapi_sdn_get_dn(slapi_entry_get_sdn_const(ds_entry)), (do_modify) ? "modified"
                        : "not modified");

    if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN)) {
        for (mod = slapi_mods_get_first_mod(smods); mod; mod = slapi_mods_get_next_mod(smods)) {
            slapi_mod_dump(mod, 0);
        }
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "<-- _pre_ds_mod_user_cb -- end\n");

    return;
}

static void
posix_winsync_pre_ds_mod_group_cb(void *cbdata, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry,
    Slapi_Entry *ds_entry, Slapi_Mods *smods, int *do_modify)
{
    LDAPMod* mod = NULL;
    Slapi_Attr *attr = NULL;
    int is_present_local = 0;
    int do_modify_local = 0;
    int rc;
    windows_attribute_map *attr_map = group_attribute_map;

    if (posix_winsync_config_get_msSFUSchema())
        attr_map = group_mssfu_attribute_map;

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> _pre_ds_mod_group_cb -- begin\n");
    /* in the ad to ds case we have no changelog, so we have to compare the entries */
    for (rc = slapi_entry_first_attr(ad_entry, &attr); rc == 0; rc
        = slapi_entry_next_attr(ad_entry, attr, &attr)) {
        char *type = NULL;
        Slapi_ValueSet *vs = NULL;
        size_t i = 0;

        slapi_attr_get_type(attr, &type);
        while (attr_map[i].windows_attribute_name != NULL) {
            if (0 == slapi_attr_type_cmp(type, attr_map[i].windows_attribute_name,
                                         SLAPI_TYPE_CMP_SUBTYPE)) {
                Slapi_Attr *local_attr = NULL;
                char *local_type = NULL;

                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "1.\n");
                slapi_attr_get_valueset(attr, &vs);
                local_type = slapi_ch_strdup(attr_map[i].ldap_attribute_name);
                slapi_entry_attr_find(ds_entry, local_type, &local_attr);
                is_present_local = (NULL == local_attr) ? 0 : 1;
                if (is_present_local) {
                    int values_equal = 0;
                    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "2.\n");
                    values_equal = attr_compare_equal(attr, local_attr);
                    if (!values_equal) {
                        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                        "_pre_ds_mod_group_cb -- update mods: %s, %s : values are different -> modify\n",
                                        slapi_sdn_get_dn(slapi_entry_get_sdn_const(ds_entry)),
                                        local_type);

                        slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE, local_type,
                                                  valueset_get_valuearray(vs));
                        *do_modify = 1;
                    }
                } else {
                    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "3.\n");

                    slapi_mods_add_mod_values(smods, LDAP_MOD_ADD, local_type,
                                              valueset_get_valuearray(vs));
                    *do_modify = do_modify_local = 1;
                }
                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "4.\n");

                slapi_ch_free((void**) &local_type);
                slapi_valueset_free(vs);
                /* what about if delete all values on windows ???? */
            }
            i++;
        }
    }
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "_pre_ds_mod_group_cb present %d modify %d before\n", is_present_local,
                    do_modify_local);
    if (posix_winsync_config_get_mapMemberUid()) {
        memberUidLock();
        modGroupMembership(ds_entry, smods, do_modify);
        memberUidUnlock();
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "_pre_ds_mod_group_cb present %d modify %d\n", is_present_local,
                    do_modify_local);

    if (!is_present_local && do_modify_local) {
        Slapi_Attr *oc_attr = NULL;
        Slapi_Value *voc = slapi_value_new();

        slapi_value_init_string(voc, "posixGroup");
        slapi_entry_attr_find(ds_entry, "objectClass", &oc_attr);
        if (slapi_attr_value_find(oc_attr, slapi_value_get_berval(voc)) != 0) {
            Slapi_ValueSet *oc_vs = slapi_valueset_new();
            Slapi_Value *oc_nv = slapi_value_new();

            slapi_attr_get_valueset(oc_attr, &oc_vs);
            slapi_value_init_string(oc_nv, "posixGroup");
            slapi_valueset_add_value(oc_vs, oc_nv);
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "_pre_ds_mod_group_cb add oc:posixGroup\n");
            slapi_mods_add_mod_values(smods, LDAP_MOD_REPLACE, "objectClass",
                                      valueset_get_valuearray(oc_vs));
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "_pre_ds_mod_group_cb step\n");
            slapi_value_free(&oc_nv);
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "_pre_ds_mod_group_cb step\n");
            /* slapi_valuset_free(oc_vs); */
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "_pre_ds_mod_group_cb step\n");
        }
        slapi_value_free(&voc);
    }
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "_pre_ds_mod_group_cb step\n");

    if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN)) {
        for (mod = slapi_mods_get_first_mod(smods); mod; mod = slapi_mods_get_next_mod(smods)) {
            slapi_mod_dump(mod, 0);
        }
    }
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- _pre_ds_mod_group_cb -- end\n");

    return;
}

static void
posix_winsync_pre_ds_add_user_cb(void *cbdata, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry,
    Slapi_Entry *ds_entry)
{
    Slapi_Attr *attr = NULL;
    char *type = NULL;
    PRBool posixval = PR_FALSE;
    windows_attribute_map *attr_map = user_attribute_map;

    if (posix_winsync_config_get_msSFUSchema())
        attr_map = user_mssfu_attribute_map;

    /* add objectclass: posixAccount, uidnumber, gidnumber, homeDirectory, loginShell */
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> _pre_ds_add_user_cb -- begin\n");

    for (slapi_entry_first_attr(ad_entry, &attr); attr; slapi_entry_next_attr(ad_entry, attr, &attr)) {
        size_t i = 0;

        slapi_attr_get_type(attr, &type);
        if (!type) {
            continue;
        }

        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "--> _pre_ds_add_user_cb -- "
            "look for [%s] to new entry [%s]\n", type, slapi_entry_get_dn_const(ds_entry));
        while (attr_map[i].windows_attribute_name != NULL) {
            if (slapi_attr_type_cmp(attr_map[i].windows_attribute_name, type,
                                    SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                Slapi_ValueSet *svs = NULL;
                slapi_attr_get_valueset(attr, &svs);
                slapi_entry_add_valueset(ds_entry, attr_map[i].ldap_attribute_name, svs);
                slapi_valueset_free(svs);

                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                "--> _pre_ds_add_user_cb -- "
                                    "adding val for [%s] to new entry [%s]\n", type,
                                slapi_entry_get_dn_const(ds_entry));
                posixval = PR_TRUE;
            }
            i++;
        }
    }
    if (posixval) {
        int rc;
        rc = slapi_entry_add_string(ds_entry, "objectClass", "posixAccount");
        rc |= slapi_entry_add_string(ds_entry, "objectClass", "shadowAccount");
        rc |= slapi_entry_add_string(ds_entry, "objectClass", "inetUser");
        if (rc != 0)
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "<-- _pre_ds_add_user_cb -- adding objectclass for new entry failed %d\n",
                            rc);
    }
    sync_acct_disable(cbdata, rawentry, ds_entry, ACCT_DISABLE_TO_DS, ds_entry, NULL, NULL);
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "<-- _pre_ds_add_user_cb -- end\n");

    return;
}

static void
posix_winsync_pre_ds_add_group_cb(void *cbdata, const Slapi_Entry *rawentry, Slapi_Entry *ad_entry,
    Slapi_Entry *ds_entry)
{
    Slapi_Attr *attr = NULL;
    char *type = NULL;
    PRBool posixval = PR_FALSE;
    windows_attribute_map *attr_map = group_attribute_map;

    if (posix_winsync_config_get_msSFUSchema())
        attr_map = group_mssfu_attribute_map;

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_pre_ds_add_group_cb -- begin\n");

    for (slapi_entry_first_attr(ad_entry, &attr); attr; slapi_entry_next_attr(ad_entry, attr, &attr)) {
        size_t i = 0;

        slapi_attr_get_type(attr, &type);
        if (!type) {
            continue;
        }

        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name, "--> _pre_ds_add_group_cb -- "
            "look for [%s] to new entry [%s]\n", type, slapi_entry_get_dn_const(ds_entry));
        while (attr_map[i].windows_attribute_name != NULL) {
            if (slapi_attr_type_cmp(attr_map[i].windows_attribute_name, type,
                                    SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                Slapi_ValueSet *svs = NULL;
                slapi_attr_get_valueset(attr, &svs);
                slapi_entry_add_valueset(ds_entry, attr_map[i].ldap_attribute_name, svs);
                slapi_valueset_free(svs);

                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                "--> _pre_ds_add_group_cb -- "
                                    "adding val for [%s] to new entry [%s]\n", type,
                                slapi_entry_get_dn_const(ds_entry));
                posixval = PR_TRUE;
            }
            i++;
        }
    }
    if (posixval) {
        int rc;
        rc = slapi_entry_add_string(ds_entry, "objectClass", "posixGroup");
        if (rc != 0) {
            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "<-- _pre_ds_add_group_cb -- adding objectclass for new entry failed %d\n",
                            rc);
        } else {
            if (posix_winsync_config_get_mapMemberUid()) {
                memberUidLock();
                addGroupMembership(ds_entry, ad_entry);
                memberUidUnlock();
            }
        }
    }
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_pre_ds_add_group_cb -- end\n");

    return;
}

static void
posix_winsync_get_new_ds_user_dn_cb(void *cbdata, const Slapi_Entry *rawentry,
    Slapi_Entry *ad_entry, char **new_dn_string, const Slapi_DN *ds_suffix,
    const Slapi_DN *ad_suffix)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_get_new_ds_user_dn_cb -- old dn [%s] -- begin\n",
                    *new_dn_string);

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_get_new_ds_user_dn_cb -- new dn [%s] -- end\n",
                    *new_dn_string);

    return;
}

static void
posix_winsync_get_new_ds_group_dn_cb(void *cbdata, const Slapi_Entry *rawentry,
    Slapi_Entry *ad_entry, char **new_dn_string, const Slapi_DN *ds_suffix,
    const Slapi_DN *ad_suffix)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_get_new_ds_group_dn_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_get_new_ds_group_dn_cb -- end\n");

    return;
}

static void
posix_winsync_pre_ad_mod_user_mods_cb(void *cbdata, const Slapi_Entry *rawentry,
    const Slapi_DN *local_dn, const Slapi_Entry *ds_entry, LDAPMod * const *origmods,
    Slapi_DN *remote_dn, LDAPMod ***modstosend)
{
    Slapi_Mods *smods = slapi_mods_new();
    Slapi_Mods *new_smods = slapi_mods_new();
    LDAPMod *mod = NULL;
    windows_attribute_map *attr_map = user_attribute_map;

    if (posix_winsync_config_get_msSFUSchema())
        attr_map = user_mssfu_attribute_map;

    /* mod if changed objectclass: posixAccount, uidnumber, gidnumber, homeDirectory, loginShell */
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> _pre_ad_mod_user_mods_cb -- begin DS account [%s] \n",
                    slapi_entry_get_dn_const(ds_entry));

    /* wrap the modstosend in a Slapi_Mods for convenience */
    slapi_mods_init_passin(new_smods, *modstosend);
    slapi_mods_init_byref(smods, (LDAPMod**) origmods);

    for (mod = slapi_mods_get_first_mod(smods); mod; mod = slapi_mods_get_next_mod(smods)) {
        size_t i = 0;
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "_pre_ad_mod_user_mods_cb -- check modify type %s\n", mod->mod_type);
        while (attr_map[i].windows_attribute_name != NULL) {
            if (0 == slapi_attr_type_cmp(mod->mod_type, attr_map[i].ldap_attribute_name,
                                         SLAPI_TYPE_CMP_SUBTYPE)) {
                Slapi_Mod *mysmod = slapi_mod_new();
                slapi_mod_init_byval(mysmod, mod);
                slapi_mod_set_type(mysmod, attr_map[i].windows_attribute_name);
                slapi_mods_add_ldapmod(new_smods, slapi_mod_get_ldapmod_passout(mysmod));
                slapi_mod_free(&mysmod);
                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                "_pre_ad_mod_user_mods_cb -- add modify %s DS account [%s]\n",
                                attr_map[i].windows_attribute_name,
                                slapi_entry_get_dn_const(ds_entry));
                if (0 == slapi_attr_type_cmp(mod->mod_type, "uidNumber", SLAPI_TYPE_CMP_SUBTYPE)) {
                    Slapi_Mod *ocsmod = slapi_mod_new();
                    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                    "_pre_ad_mod_user_mods_cb -- add NisDomain\n");
                    addNisDomainName(ocsmod, ds_entry);
                    slapi_mods_add_ldapmod(new_smods, slapi_mod_get_ldapmod_passout(ocsmod));
                    slapi_mod_free(&ocsmod);
                }
            }
            i++;
        }
        if (0 == slapi_attr_type_cmp(mod->mod_type, "nsRoleDN", SLAPI_TYPE_CMP_SUBTYPE)) {
            int dummy = 0;
            sync_acct_disable(cbdata, rawentry, (Slapi_Entry *) ds_entry, ACCT_DISABLE_TO_AD, NULL,
                              new_smods, &dummy);
        }
    }
    if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN)) {
        for (mod = slapi_mods_get_first_mod(new_smods); mod; mod
            = slapi_mods_get_next_mod(new_smods)) {
            slapi_mod_dump(mod, 0);
        }
    }
    *modstosend = slapi_mods_get_ldapmods_passout(new_smods);

    slapi_mods_free(&smods);
    slapi_mods_free(&new_smods);
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- _pre_ad_mod_user_mods_cb -- end\n");

    return;
}

static void
posix_winsync_pre_ad_mod_group_mods_cb(void *cbdata, const Slapi_Entry *rawentry,
    const Slapi_DN *local_dn, const Slapi_Entry *ds_entry, LDAPMod * const *origmods,
    Slapi_DN *remote_dn, LDAPMod ***modstosend)
{
    Slapi_Mods *smods = slapi_mods_new();
    Slapi_Mods *new_smods = slapi_mods_new();
    LDAPMod *mod = NULL;
    windows_attribute_map *attr_map = group_attribute_map;

    if (posix_winsync_config_get_msSFUSchema())
        attr_map = group_mssfu_attribute_map;

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> _pre_ad_mod_group_mods_cb -- begin\n");
    /* wrap the modstosend in a Slapi_Mods for convenience */
    slapi_mods_init_passin(new_smods, *modstosend);
    slapi_mods_init_byref(smods, (LDAPMod**) origmods);

    for (mod = slapi_mods_get_first_mod(smods); mod; mod = slapi_mods_get_next_mod(smods)) {
        size_t i = 0;
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "_pre_ad_mod_group_mods_cb -- check modify type %s\n", mod->mod_type);
        while (attr_map[i].windows_attribute_name != NULL) {
            if (0 == slapi_attr_type_cmp(mod->mod_type, attr_map[i].ldap_attribute_name,
                                         SLAPI_TYPE_CMP_SUBTYPE)) {
                Slapi_Mod *mysmod = slapi_mod_new();
                if (mod->mod_op & LDAP_MOD_DELETE) {
                    slapi_mod_init(mysmod, 0);
                    slapi_mod_set_operation(mysmod, LDAP_MOD_DELETE | LDAP_MOD_BVALUES);
                    slapi_mod_set_type(mysmod, attr_map[i].windows_attribute_name);
                } else {
                    slapi_mod_init_byval(mysmod, mod);
                    slapi_mod_set_type(mysmod, attr_map[i].windows_attribute_name);
                    if (0
                        == slapi_attr_type_cmp(mod->mod_type, "gidNumber", SLAPI_TYPE_CMP_SUBTYPE)) {
                        Slapi_Mod *ocsmod = slapi_mod_new();
                        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                        "_pre_ad_mod_group_mods_cb -- add NisDomain\n");
                        addNisDomainName(ocsmod, ds_entry);
                        slapi_mods_add_ldapmod(new_smods, slapi_mod_get_ldapmod_passout(ocsmod));
                        slapi_mod_free(&ocsmod);
                    }
                }
                slapi_mods_add_ldapmod(new_smods, slapi_mod_get_ldapmod_passout(mysmod));
                slapi_mod_free(&mysmod);
                slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                                "_pre_ad_mod_group_mods_cb -- add modify %s DS account [%s]\n",
                                attr_map[i].windows_attribute_name,
                                slapi_entry_get_dn_const(ds_entry));
            }
            i++;
        }
    }
    *modstosend = slapi_mods_get_ldapmods_passout(new_smods);
    if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN)) {
        for (mod = slapi_mods_get_first_mod(new_smods); mod;
             mod = slapi_mods_get_next_mod(new_smods)) {
            slapi_mod_dump(mod, 0);
        }
    }
    slapi_mods_free(&smods);
    slapi_mods_free(&new_smods);

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- _pre_ad_mod_group_mods_cb -- end\n");

    return;
}

static int
posix_winsync_can_add_entry_to_ad_cb(void *cbdata, const Slapi_Entry *local_entry,
    const Slapi_DN *remote_dn)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_can_add_entry_to_ad_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_can_add_entry_to_ad_cb -- end\n");

    return 1; /* false - do not allow entries to be added to ad */
}

static void
posix_winsync_begin_update_cb(void *cbdata, const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree,
    int is_total)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_begin_update_cb -- begin\n");

    posix_winsync_config_reset_MOFTaskCreated();

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_begin_update_cb -- end\n");

    return;
}

static void
posix_winsync_end_update_cb(void *cbdata, const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree,
    int is_total)
{

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_end_update_cb -- begin %d %d\n",
                    posix_winsync_config_get_MOFTaskCreated(),
                    posix_winsync_config_get_createMOFTask());
    if (posix_winsync_config_get_MOFTaskCreated() && posix_winsync_config_get_createMOFTask()) {
        /* add a task to schedule memberof Plugin for fix memebrof attributs */
        Slapi_PBlock *pb = slapi_pblock_new();
        Slapi_Entry *e_task = slapi_entry_alloc();
        int rc = 0;
        char *dn = slapi_create_dn_string("cn=%s,cn=%s,cn=tasks,cn=config",
                                          posix_winsync_plugin_name, MEMBEROFTASK);
        slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                        "--> posix_winsync_end_update_cb, create task %s\n", dn);
        if (NULL == dn) {
            slapi_log_error(SLAPI_LOG_FATAL, posix_winsync_plugin_name,
                            "posix_winsync_end_update_cb: "
                                "failed to create task dn: cn=%s,%s,cn=tasks,cn=config\n",
                            posix_winsync_plugin_name, MEMBEROFTASK);
            return;
        }
        slapi_entry_init(e_task, slapi_ch_strdup(dn), NULL);
        slapi_entry_add_string(e_task, "cn", slapi_ch_strdup(posix_winsync_plugin_name));
        slapi_entry_add_string(e_task, "objectClass", "extensibleObject");
        slapi_entry_add_string(e_task, "basedn", slapi_sdn_get_dn(ds_subtree));

        slapi_add_entry_internal_set_pb(pb, e_task, NULL, posix_winsync_get_plugin_identity(), 0);
        slapi_add_internal_pb(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (rc != 0) {
            slapi_log_error(SLAPI_LOG_FATAL, posix_winsync_plugin_name,
                            "posix_winsync_end_update_cb: "
                                "failed to add task entry\n");
        } else {

            slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                            "posix_winsync_end_update_cb: "
                                "add task entry\n");
        }
        /* slapi_entry_free(e_task); */
        slapi_pblock_destroy(pb);
        pb = NULL;
        posix_winsync_config_reset_MOFTaskCreated();
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_end_update_cb -- end\n");

    return;
}

static void
posix_winsync_destroy_agmt_cb(void *cbdata, const Slapi_DN *ds_subtree, const Slapi_DN *ad_subtree)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_destroy_agmt_cb -- begin\n");

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_destroy_agmt_cb -- end\n");

    return;
}

static void *posix_winsync_api[] = { NULL, /* reserved for api broker use, must be zero */
                                     posix_winsync_agmt_init,
                                     posix_winsync_dirsync_search_params_cb,
                                     posix_winsync_pre_ad_search_cb,
                                     posix_winsync_pre_ds_search_entry_cb,
                                     posix_winsync_pre_ds_search_all_cb,
                                     posix_winsync_pre_ad_mod_user_cb,
                                     posix_winsync_pre_ad_mod_group_cb,
                                     posix_winsync_pre_ds_mod_user_cb,
                                     posix_winsync_pre_ds_mod_group_cb,
                                     posix_winsync_pre_ds_add_user_cb,
                                     posix_winsync_pre_ds_add_group_cb,
                                     posix_winsync_get_new_ds_user_dn_cb,
                                     posix_winsync_get_new_ds_group_dn_cb,
                                     posix_winsync_pre_ad_mod_user_mods_cb,
                                     posix_winsync_pre_ad_mod_group_mods_cb,
                                     posix_winsync_can_add_entry_to_ad_cb,
                                     posix_winsync_begin_update_cb,
                                     posix_winsync_end_update_cb,
                                     posix_winsync_destroy_agmt_cb };

static Slapi_ComponentId *posix_winsync_plugin_id = NULL;

/*
 ** Plugin identity mgmt
 */

void
posix_winsync_set_plugin_identity(void * identity)
{
    posix_winsync_plugin_id = identity;
}

void *
posix_winsync_get_plugin_identity()
{
    return posix_winsync_plugin_id;
}

static int
posix_winsync_plugin_start(Slapi_PBlock *pb)
{
    int rc;
    Slapi_Entry *config_e = NULL; /* entry containing plugin config */

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_plugin_start -- begin\n");

    if (slapi_apib_register(WINSYNC_v1_0_GUID, posix_winsync_api)) {
        slapi_log_error(SLAPI_LOG_FATAL, posix_winsync_plugin_name,
                        "<-- posix_winsync_plugin_start -- failed to register winsync api -- end\n");
        return -1;
    }

    if (slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &config_e) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, posix_winsync_plugin_name, "missing config entry\n");
        return (-1);
    }
    if ((rc = posix_winsync_config(config_e)) != LDAP_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, posix_winsync_plugin_name, "configuration failed (%s)\n",
                        ldap_err2string(rc));
        return (-1);
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_plugin_start -- registered; end\n");
    return 0;
}

static int
posix_winsync_plugin_close(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_plugin_close -- begin\n");

    slapi_apib_unregister(WINSYNC_v1_0_GUID);

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_plugin_close -- end\n");
    return 0;
}

/* this is the slapi plugin init function,
 not the one used by the winsync api
 */
int
posix_winsync_plugin_init(Slapi_PBlock *pb)
{
    void *plugin_id = NULL;

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "--> posix_winsync_plugin_init -- begin\n");

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) != 0
        || slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN, (void *) posix_winsync_plugin_start) != 0
        || slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN, (void *) posix_winsync_plugin_close) != 0
        || slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *) &posix_winsync_pdesc) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, posix_winsync_plugin_name,
                        "<-- posix_winsync_plugin_init -- failed to register plugin -- end\n");
        return -1;
    }

    /* Retrieve and save the plugin identity to later pass to
     internal operations */
    if (slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_id) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, posix_winsync_plugin_name,
                        "<-- posix_winsync_plugin_init -- failed to retrieve plugin identity -- end\n");
        return -1;
    }

    posix_winsync_set_plugin_identity(plugin_id);

    slapi_log_error(SLAPI_LOG_PLUGIN, posix_winsync_plugin_name,
                    "<-- posix_winsync_plugin_init -- end\n");
    return 0;
}

