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

 $Id: posix-group-func.c 28 2011-05-13 14:35:29Z grzemba $
 */
#include "slapi-plugin.h"
#include "slapi-private.h"

#include <string.h>
#include <nspr.h>
#include "posix-wsp-ident.h"

#define MAX_RECURSION_DEPTH (5)

Slapi_Value **
valueset_get_valuearray(const Slapi_ValueSet *vs); /* stolen from proto-slap.h */
static int hasObjectClass(Slapi_Entry *entry, const char *objectClass);

static PRMonitor *memberuid_operation_lock = 0;

void
memberUidLock()
{
    PR_EnterMonitor(memberuid_operation_lock);
}

void
memberUidUnlock()
{
    PR_ExitMonitor(memberuid_operation_lock);
}

int
memberUidLockInit()
{
    return (memberuid_operation_lock = PR_NewMonitor()) != NULL;
}

void
addDynamicGroupIfNecessary(Slapi_Entry *entry, Slapi_Mods *smods) {
    Slapi_Attr *oc_attr = NULL;
    Slapi_Value *voc = slapi_value_new();

    slapi_value_init_string(voc, "dynamicGroup");
    slapi_entry_attr_find(entry, "objectClass", &oc_attr);

    if (slapi_attr_value_find(oc_attr, slapi_value_get_berval(voc)) != 0) {
        if (smods) {
            slapi_mods_add_string(smods, LDAP_MOD_ADD, "objectClass", "dynamicGroup");
        }
        else {
            smods = slapi_mods_new();
            slapi_mods_add_string(smods, LDAP_MOD_ADD, "objectClass", "dynamicGroup");

            Slapi_PBlock *mod_pb = slapi_pblock_new();
            slapi_modify_internal_set_pb_ext(mod_pb, slapi_entry_get_sdn(entry), slapi_mods_get_ldapmods_passout(smods), 0, 0,
                                             posix_winsync_get_plugin_identity(), 0);
            slapi_modify_internal_pb(mod_pb);
            slapi_pblock_destroy(mod_pb);

            slapi_mods_free(&smods);
        }
    }

    slapi_value_free(&voc);
}

Slapi_Entry *
getEntry(const char *udn, char **attrs)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "getEntry: search %s\n", udn);

    Slapi_DN *udn_sdn = slapi_sdn_new_dn_byval(udn);
    Slapi_Entry *result = NULL;
    int rc = slapi_search_internal_get_entry(udn_sdn, attrs, &result, posix_winsync_get_plugin_identity());
    slapi_sdn_free(&udn_sdn);

    if (rc == 0) {
        if (result != NULL) {
            return result; /* Must be freed */
        }
        else {
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "getEntry: %s not found\n", udn);
        }
    }
    else {
        slapi_log_error(SLAPI_LOG_FATAL, POSIX_WINSYNC_PLUGIN_NAME,
                        "getEntry: error searching for uid: %d", rc);
    }

    return NULL;
}

/* search the user with DN udn and returns uid*/
char *
searchUid(const char *udn)
{
    char *attrs[] = { "uid", "objectclass", NULL };
    Slapi_Entry *entry = getEntry(udn,
                                  /* "(|(objectclass=posixAccount)(objectclass=ldapsubentry))", */
                                  attrs);
    char *uid = NULL;

    if (entry) {
        Slapi_Attr *attr = NULL;
        Slapi_Value *v = NULL;

        if (slapi_entry_attr_find(entry, "uid", &attr) == 0 && hasObjectClass(entry, "posixAccount")) {
            slapi_attr_first_value(attr, &v);
            uid = slapi_ch_strdup(slapi_value_get_string(v));
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "searchUid: return uid %s\n", uid);
        } else {
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "searchUid: uid in %s not found\n", udn);
        }

        if (uid && posix_winsync_config_get_lowercase()) {
            uid = slapi_dn_ignore_case(uid);
        }

        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "searchUid: About to free entry (%s)\n", udn);
        
        slapi_entry_free(entry);
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "searchUid(%s): <==\n", udn);
        
    return uid;
}

int
dn_in_set(const char* uid, char **uids)
{
    int i;
    Slapi_DN *sdn_uid = NULL;
    Slapi_DN *sdn_ul = NULL;

    if (uids == NULL || uid == NULL)
        return false;

    sdn_uid = slapi_sdn_new_dn_byval(uid);
    sdn_ul = slapi_sdn_new();

    for (i = 0; uids[i]; i++) {
        slapi_sdn_set_dn_byref(sdn_ul, uids[i]);
        if (slapi_sdn_compare(sdn_uid, sdn_ul) == 0) {
            slapi_sdn_free(&sdn_ul);
            slapi_sdn_free(&sdn_uid);
            return true;
        }
        slapi_sdn_done(sdn_ul);
    }
    slapi_sdn_free(&sdn_ul);
    slapi_sdn_free(&sdn_uid);
    return false;
}

int
uid_in_set(const char* uid, char **uids)
{
    int i;

    if (uid == NULL)
        return false;
    for (i = 0; uids != NULL && uids[i] != NULL; i++) {
        Slapi_RDN *i_rdn = NULL;
        char *i_uid = NULL;
        char *t = NULL;

        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "uid_in_set: comp %s %s \n",
                        uid, uids[i]);
        i_rdn = slapi_rdn_new_dn(uids[i]);
        if (slapi_rdn_get_first(i_rdn, &t, &i_uid) == 1) {
            if (strncasecmp(uid, i_uid, 256) == 0) {
                slapi_rdn_free(&i_rdn);
                return true;
            }
        }
        slapi_rdn_free(&i_rdn);
    }
    return false;
}

int
uid_in_valueset(const char* uid, Slapi_ValueSet *uids)
{
    int i;
    Slapi_Value *v = NULL;

    if (uid == NULL)
        return false;
    for (i = slapi_valueset_first_value(uids, &v); i != -1;
         i = slapi_valueset_next_value(uids, i, &v)) {
        Slapi_RDN *i_rdn = NULL;
        char *i_uid = NULL;
        char *t = NULL;

        const char *uid_i = slapi_value_get_string(v);

        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "uid_in_valueset: comp %s %s \n",
                        uid, uid_i);
        i_rdn = slapi_rdn_new_dn(uid_i);
        if (slapi_rdn_get_first(i_rdn, &t, &i_uid) == 1) {
            if (strncasecmp(uid, i_uid, 256) == 0) {
                slapi_rdn_free(&i_rdn);
                return true;
            }
        }
        slapi_rdn_free(&i_rdn);
    }
    return false;
}

/* return 1 if smods already has the given mod - 0 otherwise */
static int
smods_has_mod(Slapi_Mods *smods, int modtype, const char *type, const char *val)
{
    int rc = 0;
    Slapi_Mod *smod = slapi_mod_new(), *smodp = NULL;

    for (smodp = slapi_mods_get_first_smod(smods, smod);
         (rc == 0) && smods && (smodp != NULL);
         smodp = slapi_mods_get_next_smod(smods, smod)) {
        if (slapi_attr_types_equivalent(slapi_mod_get_type(smod), type)
            && ((slapi_mod_get_operation(smod) | LDAP_MOD_BVALUES) == (modtype | LDAP_MOD_BVALUES))) {
            /* type and op are equal - see if val is in the mod's list of values */
            Slapi_Value *sval = slapi_value_new_string((char *) val);
            Slapi_Attr *attr = slapi_attr_new();
            struct berval *bvp = NULL;

            slapi_attr_init(attr, type);
            for (bvp = slapi_mod_get_first_value(smodp); (rc == 0) && (bvp != NULL);
                 bvp = slapi_mod_get_next_value(smodp)) {
                Slapi_Value *modval = slapi_value_new_berval(bvp);

                rc = (slapi_value_compare(attr, sval, modval) == 0);
                slapi_value_free(&modval);
            }
            slapi_value_free(&sval);
            slapi_attr_free(&attr);
        }
    }
    slapi_mod_free(&smod);
    return rc;
}

static int
hasObjectClass(Slapi_Entry *entry, const char *objectClass)
{
    int rc = 0;
    int i;
    Slapi_Attr  *obj_attr = NULL;
    Slapi_Value *value    = NULL;

    rc = slapi_entry_attr_find(entry, "objectclass", &obj_attr);

    if (rc != 0) {
        return 0; /* Doesn't have any objectclasses */
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "Scanning objectclasses\n");

    for (
        i = slapi_attr_first_value(obj_attr, &value);
        i != -1;
        i = slapi_attr_next_value(obj_attr, i, &value)
    ) {
        const char *oc = NULL;
        oc = slapi_value_get_string(value);
        if (strcasecmp(oc, objectClass) == 0) {
            return 1; /* Entry has the desired objectclass */
        }
    }
    
    return 0; /* Doesn't have desired objectclass */
}

void
posix_winsync_foreach_parent(Slapi_Entry *entry, char **attrs, plugin_search_entry_callback callback, void *callback_data)
{
    char *cookie = NULL;
    Slapi_Backend *be = NULL;

    char *value = slapi_entry_get_ndn(entry);
    size_t vallen = value ? strlen(value) : 0;
    char *filter_escaped_value = slapi_escape_filter_value(value, vallen);
    char *filter = slapi_ch_smprintf("(uniqueMember=%s)", filter_escaped_value);
    slapi_ch_free_string(&filter_escaped_value);

    Slapi_PBlock *search_pb = slapi_pblock_new();

    for (be = slapi_get_first_backend(&cookie); be;
         be = slapi_get_next_backend(cookie)) {
        const Slapi_DN *base_sdn = slapi_be_getsuffix(be, 0);
        if (base_sdn == NULL) {
            continue;
        }
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "posix_winsync_foreach_parent: Searching subtree %s for %s\n",
                        slapi_sdn_get_dn(base_sdn),
                        filter);
        
        slapi_search_internal_set_pb(search_pb,
                                     slapi_sdn_get_dn(base_sdn),
                                     LDAP_SCOPE_SUBTREE,
                                     filter,
                                     attrs, 0, NULL, NULL,
                                     posix_winsync_get_plugin_identity(), 0);
        slapi_search_internal_callback_pb(search_pb, callback_data, 0, callback, 0);        
        
        slapi_pblock_init(search_pb);
    }

    slapi_pblock_destroy(search_pb);
    slapi_ch_free((void**)&cookie);
    slapi_ch_free_string(&filter);
}

/* Retrieve nested membership from chains of groups.
 * Muid_vs  in => any preexisting membership list
 *         out => the union of the input list and the total membership
 * Muid_nested_vs out => the members of muid_vs "out" that weren't in muid_vs "in"
 * deletions in => Any elements to NOT consider if members of base_sdn
 */
void
getMembershipFromDownward(Slapi_Entry *entry, Slapi_ValueSet *muid_vs, Slapi_ValueSet *muid_nested_vs, Slapi_ValueSet *deletions, const Slapi_DN *base_sdn, int depth)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "getMembershipFromDownward: ==>\n");
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "getMembershipFromDownward: entry name: %s\n",
                    slapi_entry_get_dn_const(entry));

    int rc = 0;
    Slapi_Attr *um_attr = NULL; /* Entry attributes uniqueMember */
    Slapi_Value *uid_value = NULL; /* uniqueMember attribute values */

    if (depth >= MAX_RECURSION_DEPTH) {
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "getMembershipFromDownward: recursion limit reached: %d\n", depth);
        return;
    }

    rc = slapi_entry_attr_find(entry, "uniquemember", &um_attr);
    if (rc != 0 || um_attr == NULL) {
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "getMembershipFromDownward end: attribute uniquemember not found\n");
        return;
    }

    int i;
    for (i = slapi_attr_first_value(um_attr, &uid_value); i != -1;
         i = slapi_attr_next_value(um_attr, i, &uid_value)) {

        char *attrs[] = { "uniqueMember", "memberUid", "uid", "objectClass", NULL };
        const char *uid_dn = slapi_value_get_string(uid_value);
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "getMembershipFromDownward: iterating uniqueMember: %s\n",
                        uid_dn);
        
        if (deletions && !slapi_sdn_compare(slapi_entry_get_sdn_const(entry), base_sdn)) {
            if (slapi_valueset_find(um_attr, deletions, uid_value)) {
                slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                "getMembershipFromDownward: Skipping iteration because of deletion\n");

                continue;
            }
        }

        Slapi_Entry *child = getEntry(uid_dn, attrs);

        if (!child) {
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "getMembershipFromDownward end: child not found: %s\n", uid_dn);
        }
        else {
            /* PosixGroups except for the top one are already fully mapped out */
            if ((!hasObjectClass(entry, "posixGroup") || depth == 0) &&
                (hasObjectClass(child, "ntGroup") || hasObjectClass(child, "posixGroup"))) {

                /* Recurse downward */
                getMembershipFromDownward(child, muid_vs, muid_nested_vs, deletions, base_sdn, depth + 1);
            }

            if (hasObjectClass(child, "posixAccount")) {
                Slapi_Attr *uid_attr = NULL;
                Slapi_Value *v = NULL;
                if (slapi_entry_attr_find(child, "uid", &uid_attr) == 0) {
                    slapi_attr_first_value(uid_attr, &v);

                    if (v && !slapi_valueset_find(uid_attr, muid_vs, v)) {                        
                        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                        "getMembershipFromDownward: adding member: %s\n",
                                        slapi_value_get_string(v));
                        slapi_valueset_add_value(muid_vs, v);
                        slapi_valueset_add_value(muid_nested_vs, v);
                    }
                }
            }
            slapi_entry_free(child);
        }
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "getMembershipFromDownward: <==\n");
}

struct propogateMembershipUpwardArgs {
    Slapi_ValueSet *muid_vs;
    int depth;
};

/* Forward declaration for next function */
void propogateMembershipUpward(Slapi_Entry *, Slapi_ValueSet *, int);

int
propogateMembershipUpwardCallback(Slapi_Entry *child, void *callback_data)
{
    struct propogateMembershipUpwardArgs *args = (struct propogateMembershipUpwardArgs *)(callback_data);
    propogateMembershipUpward(child, args->muid_vs, args->depth);
    return 0;
}

void
propogateMembershipUpward(Slapi_Entry *entry, Slapi_ValueSet *muid_vs, int depth)
{
    if (depth >= MAX_RECURSION_DEPTH) {
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "propogateMembershipUpward: recursion limit reached: %d\n", depth);
        return;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "propogateMembershipUpward: ==>\n");
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "propogateMembershipUpward: entry name: %s\n",
                    slapi_entry_get_dn_const(entry));

    Slapi_ValueSet *muid_here_vs   = NULL;
    Slapi_ValueSet *muid_upward_vs = NULL;

    /* Get the memberUids at this location, and figure out local changes to memberUid (if any)
     *  and changes to send upward.
     */
    if (depth > 0 && hasObjectClass(entry, "posixGroup")) {
        int addDynamicGroup = 0;
        Slapi_Attr *muid_old_attr = NULL;
        Slapi_ValueSet *muid_old_vs = NULL;
        int rc = slapi_entry_attr_find(entry, "memberUid", &muid_old_attr);
        if (rc != 0 || muid_old_attr == NULL) { /* Found no memberUid list, so create  */
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "propogateMembershipUpward: no attribute memberUid\n");
            
            /* There's no values from this entry to add */
            muid_upward_vs = muid_vs;
            muid_here_vs = muid_vs;
        }
        else {
            int i = 0;
            Slapi_Value *v = NULL;
            /* Eliminate duplicates */
            muid_upward_vs = slapi_valueset_new();
            muid_here_vs = slapi_valueset_new();

            slapi_attr_get_valueset(muid_old_attr, &muid_old_vs);
            slapi_valueset_set_valueset(muid_upward_vs, muid_old_vs);

            for (i = slapi_valueset_first_value(muid_vs, &v); i != -1;
                 i = slapi_valueset_next_value(muid_vs, i, &v)) {
                
                if (!slapi_valueset_find(muid_old_attr, muid_old_vs, v)) {
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "propogateMembershipUpward: adding %s to set\n",
                                    slapi_value_get_string(v));

                    addDynamicGroup = 1;
                    slapi_valueset_add_value(muid_here_vs, v);
                    slapi_valueset_add_value(muid_upward_vs, v);
                }
            }
            slapi_valueset_free(muid_old_vs);
        }

        /* Update this group's membership */
        slapi_entry_add_valueset(entry, "memberUid", muid_here_vs);
        if (addDynamicGroup) {
            addDynamicGroupIfNecessary(entry, NULL);
            slapi_entry_add_valueset(entry, "dsOnlyMemberUid", muid_here_vs);
        }
    }
    else {
        muid_upward_vs = muid_vs;
    }

    /* Find groups containing this one, recurse
     */
    char *attrs[] = {"memberUid", "objectClass", NULL};
    struct propogateMembershipUpwardArgs data = {muid_upward_vs, depth + 1};

    posix_winsync_foreach_parent(entry, attrs, propogateMembershipUpwardCallback, &data);

/* Cleanup */
    if (muid_here_vs && muid_here_vs != muid_vs) {
        slapi_valueset_free(muid_here_vs); muid_here_vs = NULL;
    }
    if (muid_upward_vs && muid_upward_vs != muid_vs) {
        slapi_valueset_free(muid_upward_vs); muid_upward_vs = NULL;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "propogateMembershipUpward: <==\n");
}

struct propogateDeletionsUpwardArgs {
    const Slapi_DN *base_sdn;
    Slapi_ValueSet *smod_deluids;
    Slapi_ValueSet *del_nested_vs;
    int depth;
};

/* Forward declaration for next function */
void propogateDeletionsUpward(Slapi_Entry *, const Slapi_DN *, Slapi_ValueSet*, Slapi_ValueSet *, int);

int
propogateDeletionsUpwardCallback(Slapi_Entry *entry, void *callback_data)
{
    struct propogateDeletionsUpwardArgs *args = (struct propogateDeletionsUpwardArgs *)(callback_data);
    propogateDeletionsUpward(entry, args->base_sdn, args->smod_deluids, args->del_nested_vs, args->depth);
    return 0;
}

void
propogateDeletionsUpward(Slapi_Entry *entry, const Slapi_DN *base_sdn, Slapi_ValueSet *smod_deluids, Slapi_ValueSet *del_nested_vs, int depth)
{
    if (smod_deluids == NULL) return;

    if (depth >= MAX_RECURSION_DEPTH) {
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "propogateDeletionsUpward: recursion limit reached: %d\n", depth);
        return;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "propogateDeletionsUpward: ==>\n");
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "propogateDeletionsUpward: entry name: %s\n",
                    slapi_entry_get_dn_const(entry));

    char *attrs[] = { "uniqueMember", "memberUid", "objectClass", NULL };
    struct propogateDeletionsUpwardArgs data = {base_sdn, smod_deluids, del_nested_vs, depth + 1};
    posix_winsync_foreach_parent(entry, attrs, propogateDeletionsUpwardCallback, &data);

    Slapi_Attr *muid_attr = NULL;
    int rc = slapi_entry_attr_find(entry, "dsOnlyMemberUid", &muid_attr);
    
    if (rc == 0 && muid_attr != NULL) {

        Slapi_ValueSet *muid_vs = slapi_valueset_new();
        Slapi_ValueSet *muid_nested_vs = slapi_valueset_new();
        Slapi_ValueSet *muid_deletions_vs = slapi_valueset_new();

        getMembershipFromDownward(entry, muid_vs, muid_nested_vs, smod_deluids, base_sdn, 0);

        int i;
        Slapi_Value *v;
        for (i = slapi_attr_first_value(muid_attr, &v); i != -1;
             i = slapi_attr_next_value(muid_attr, i, &v)) {
            if (!slapi_valueset_find(muid_attr, muid_vs, v)) {
                const char *uid = slapi_value_get_string(v);
                if (depth == 0 && !uid_in_valueset(uid, smod_deluids)) {
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "propogateDeletionsUpward: Adding deletion to modlist: %s\n",
                                    slapi_value_get_string(v));
                    slapi_valueset_add_value(del_nested_vs, v);                    
                }
                else if (depth > 0) {
                    slapi_valueset_add_value(muid_deletions_vs, v);                
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "propogateDeletionsUpward: Adding deletion to deletion list: %s\n",
                                    slapi_value_get_string(v));
                }
            }
        }

        if (depth > 0) {
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "propogateDeletionsUpward: executing deletion list\n");

            Slapi_Mods *smods = slapi_mods_new();
            slapi_mods_add_mod_values(smods, LDAP_MOD_DELETE, "memberuid", valueset_get_valuearray(muid_deletions_vs));
            slapi_mods_add_mod_values(smods, LDAP_MOD_DELETE, "dsonlymemberuid", valueset_get_valuearray(muid_deletions_vs));

            Slapi_PBlock *mod_pb = slapi_pblock_new();
            slapi_modify_internal_set_pb_ext(mod_pb, slapi_entry_get_sdn(entry), slapi_mods_get_ldapmods_passout(smods), 0, 0,
                                             posix_winsync_get_plugin_identity(), 0);
            slapi_modify_internal_pb(mod_pb);
            slapi_pblock_destroy(mod_pb);

            slapi_mods_free(&smods);
        }

        slapi_valueset_free(muid_vs); muid_vs = NULL;
        slapi_valueset_free(muid_nested_vs); muid_nested_vs = NULL;
        slapi_valueset_free(muid_deletions_vs); muid_deletions_vs = NULL;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "propogateDeletionsUpward: <==\n");
}

int
modGroupMembership(Slapi_Entry *entry, Slapi_Mods *smods, int *do_modify, int newposixgroup)
{
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "modGroupMembership: ==>\n");
    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "modGroupMembership: Modding %s\n",
                    slapi_entry_get_dn_const(entry));

    int posixGroup = hasObjectClass(entry, "posixGroup");

    if (!(posixGroup || hasObjectClass(entry, "ntGroup")) && !newposixgroup) {
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "modGroupMembership end: Not a posixGroup or ntGroup\n");
        return 0;
    }

    Slapi_Mod *smod = NULL;
    Slapi_Mod *nextMod = slapi_mod_new();
    int del_mod = 0; /* Bool: was there a delete mod? */
    char **smod_adduids = NULL;
    Slapi_ValueSet *smod_deluids = NULL;

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "modGroupMembership: posixGroup -> look for uniquemember\n");
    if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN))
        slapi_mods_dump(smods, "memberUid - mods dump - initial");
    for (smod = slapi_mods_get_first_smod(smods, nextMod); smod; smod
             = slapi_mods_get_next_smod(smods, nextMod)) {
        if (slapi_attr_types_equivalent(slapi_mod_get_type(smod), "uniqueMember")) {
            struct berval *bv;

            int current_del_mod = SLAPI_IS_MOD_DELETE(slapi_mod_get_operation(smod));
            if (current_del_mod) {
                del_mod = 1;
            }
            
            for (bv = slapi_mod_get_first_value(smod); bv;
                 bv = slapi_mod_get_next_value(smod)) {
                Slapi_Value *sv = slapi_value_new();

                slapi_value_init_berval(sv, bv); /* copies bv_val */
                if (current_del_mod) {
                    if (!smod_deluids) smod_deluids = slapi_valueset_new();

                    slapi_valueset_add_value(smod_deluids, sv);
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "modGroupMembership: add to deluids %s\n",
                                    bv->bv_val);
                } else {
                    slapi_ch_array_add(&smod_adduids,
                                       slapi_ch_strdup(slapi_value_get_string(sv)));
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "modGroupMembership: add to adduids %s\n",
                                    bv->bv_val);
                }
                slapi_value_free(&sv);
            }
        }
    }
    slapi_mod_free(&nextMod);

    int muid_rc = 0;
    Slapi_Attr * muid_attr  = NULL; /* Entry attributes        */
    Slapi_ValueSet *muid_vs = NULL;
    Slapi_Value * uid_value = NULL; /* Attribute values        */

    Slapi_ValueSet *adduids = slapi_valueset_new();
    Slapi_ValueSet *add_nested_vs = slapi_valueset_new();
    Slapi_ValueSet *deluids = slapi_valueset_new();
    Slapi_ValueSet *del_nested_vs = slapi_valueset_new();

    const Slapi_DN *base_sdn = slapi_entry_get_sdn_const(entry);

    int j = 0;

    if (del_mod || smod_deluids != NULL) {
        do { /* Create a context to "break" from */
            muid_rc = slapi_entry_attr_find(entry, "memberUid", &muid_attr);

            if (smod_deluids == NULL) { /* deletion of the last value, deletes the Attribut from entry complete, this operation has no value, so we must look by self */
                Slapi_Attr * um_attr = NULL; /* Entry attributes        */
                int rc = slapi_entry_attr_find(entry, "uniquemember", &um_attr);
                
                if (rc != 0 || um_attr == NULL) {
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "modGroupMembership end: attribute uniquemember not found\n");
                    break;
                }

                slapi_attr_get_valueset(um_attr, &smod_deluids);
            }
            if (muid_rc != 0 || muid_attr == NULL) {
                slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                "modGroupMembership end: attribute memberUid not found\n");
            }
            else if (posix_winsync_config_get_mapMemberUid()) {
                /* ...loop for value...    */
                for (j = slapi_attr_first_value(muid_attr, &uid_value); j != -1;
                     j = slapi_attr_next_value(muid_attr, j, &uid_value)) {
                    /* remove from uniquemember: remove from memberUid also */
                    const char *uid = NULL;
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "modGroupMembership: test dellist \n");
                    uid = slapi_value_get_string(uid_value);
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "modGroupMembership: test dellist %s\n", uid);
                    if (uid_in_valueset(uid, smod_deluids)) {
                        slapi_valueset_add_value(deluids, uid_value);
                        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                        "modGroupMembership: add to dellist %s\n", uid);
                    }
                }
            }
            
            if (posix_winsync_config_get_mapNestedGrouping()) {
                propogateDeletionsUpward(entry, base_sdn, smod_deluids, del_nested_vs, 0);
                int i;
                Slapi_Value *v;
                for (i = slapi_valueset_first_value(del_nested_vs, &v); i != -1;
                     i = slapi_valueset_next_value(del_nested_vs, i, &v)) {
                    slapi_valueset_add_value(deluids, v);
                }
            }
        } while (false);
    }
    if (smod_adduids != NULL) { /* not MOD_DELETE */
        const char *uid_dn = NULL;

        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "modGroupMembership: posixGroup -> look for uniquemember\n");

        if (muid_rc == 0 && muid_attr == NULL) {
            muid_rc = slapi_entry_attr_find(entry, "memberUid", &muid_attr);
        }
        if (muid_rc == 0 && muid_attr != NULL) {
            slapi_attr_get_valueset(muid_attr, &muid_vs);
        }
        else {
            muid_vs = slapi_valueset_new();
        }

        if (posix_winsync_config_get_mapMemberUid()) {
            for (j = 0; smod_adduids[j]; j++) {
                static char *uid = NULL;

                uid_dn = smod_adduids[j];
                slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                "modGroupMembership: perform user %s\n", uid_dn);

                uid = searchUid(uid_dn);

                if (uid == NULL) {
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "modGroupMembership: uid not found for %s, cannot do anything\n",
                                    uid_dn); /* member on longer on server, do nothing */
                } else {
                    Slapi_Value *v = slapi_value_new();
                    slapi_value_init_string_passin(v, uid);

                    if (muid_rc == 0 && muid_attr != NULL &&
                        slapi_valueset_find(muid_attr, muid_vs, v) != NULL) {

                        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                        "modGroupMembership: uid found in memberuid list %s nothing to do\n",
                                        uid);
                    }
                    else {
                        slapi_valueset_add_value(adduids, v);
                        slapi_valueset_add_value(muid_vs, v);
                        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                        "modGroupMembership: add to modlist %s\n", uid);
                    }

                    slapi_value_free(&v); /* also frees uid since it was a passin */
                }
            }
        }

        if (posix_winsync_config_get_mapNestedGrouping()) {

            for (j = 0; smod_adduids[j]; ++j) {
                char *attrs[] = { "uniqueMember", "memberUid", "uid", "objectClass", NULL };
                Slapi_Entry *child = getEntry(smod_adduids[j], attrs);

                if (child) {
                    if (hasObjectClass(child, "ntGroup") || hasObjectClass(child, "posixGroup")) {
                        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                        "modGroupMembership: Found mod to add group, adding membership: %s\n",
                                        smod_adduids[j]);
                        Slapi_ValueSet *muid_tempnested = slapi_valueset_new();
                        getMembershipFromDownward(child, muid_vs, add_nested_vs, smod_deluids, base_sdn, 0);

                        slapi_valueset_free(muid_tempnested); muid_tempnested = NULL;
                    }
                }
                else {
                    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                    "modGroupMembership: entry not found for dn: %s\n",
                                    smod_adduids[j]);
                }
            }

            getMembershipFromDownward(entry, muid_vs, add_nested_vs, smod_deluids, base_sdn, 0);
            int i = 0;
            Slapi_Value *v = NULL;
            for (i = slapi_valueset_first_value(add_nested_vs, &v); i != -1;
                 i = slapi_valueset_next_value(add_nested_vs, i, &v)) {
                slapi_valueset_add_value(adduids, v);
            }

            propogateMembershipUpward(entry, adduids, 0);
        }
    }
    if (posixGroup) {
        int addDynamicGroup = 0;
        int i;
        Slapi_Value *v;
        for (i = slapi_valueset_first_value(adduids, &v); i != -1;
             i = slapi_valueset_next_value(adduids, i, &v)){
            const char *muid = slapi_value_get_string(v);
            if (!smods_has_mod(smods, LDAP_MOD_ADD, "memberUid", muid)) {
                *do_modify = 1;
                slapi_mods_add_string(smods, LDAP_MOD_ADD, "memberUid", muid);
            }
        }
        for (i = slapi_valueset_first_value(add_nested_vs, &v); i != -1;
             i = slapi_valueset_next_value(add_nested_vs, i, &v)) {
            const char *muid = slapi_value_get_string(v);
            if (!smods_has_mod(smods, LDAP_MOD_ADD, "dsOnlyMemberUid", muid)) {
                addDynamicGroup = 1;
                *do_modify = 1;
                slapi_mods_add_string(smods, LDAP_MOD_ADD, "dsOnlyMemberUid", muid);
            }
        }
        for (i = slapi_valueset_first_value(deluids, &v); i != -1;
             i = slapi_valueset_next_value(deluids, i, &v)){
            const char *muid = slapi_value_get_string(v);
            if (!smods_has_mod(smods, LDAP_MOD_DELETE, "memberUid", muid)) {
                *do_modify = 1;
                slapi_mods_add_string(smods, LDAP_MOD_DELETE, "memberUid", muid);
            }
        }
        for (i = slapi_valueset_first_value(del_nested_vs, &v); i != -1;
             i = slapi_valueset_next_value(del_nested_vs, i, &v)){
            const char *muid = slapi_value_get_string(v);
            if (!smods_has_mod(smods, LDAP_MOD_DELETE, "dsOnlyMemberUid", muid)) {
                *do_modify = 1;
                slapi_mods_add_string(smods, LDAP_MOD_DELETE, "dsOnlyMemberUid", muid);
            }
        }
        if (addDynamicGroup) {
            addDynamicGroupIfNecessary(entry, smods);
        }

        if (slapi_is_loglevel_set(SLAPI_LOG_PLUGIN))
            slapi_mods_dump(smods, "memberUid - mods dump");
        posix_winsync_config_set_MOFTaskCreated();
    }
    slapi_ch_array_free(smod_adduids);
    smod_adduids = NULL;
    if (smod_deluids) slapi_valueset_free(smod_deluids);
    smod_deluids = NULL;

    slapi_valueset_free(adduids);
    adduids = NULL;
    slapi_valueset_free(deluids);
    deluids = NULL;

    slapi_valueset_free(add_nested_vs); add_nested_vs = NULL;
    slapi_valueset_free(del_nested_vs); del_nested_vs = NULL;

    if (muid_vs) {
        slapi_valueset_free(muid_vs); muid_vs = NULL;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "modGroupMembership: <==\n");
    return 0;
}

int
addUserToGroupMembership(Slapi_Entry *entry)
{
    Slapi_Attr *uid_attr = NULL;
    Slapi_Value *v = NULL;
    Slapi_ValueSet *muid_vs = slapi_valueset_new();

    if (slapi_entry_attr_find(entry, "uid", &uid_attr) == 0) {
        slapi_attr_first_value(uid_attr, &v);

        if (v) {
            slapi_valueset_add_value(muid_vs, v);
        }
    }

    propogateMembershipUpward(entry, muid_vs, 0);

    slapi_valueset_free(muid_vs); muid_vs = NULL;
    return 0;
}

int
addGroupMembership(Slapi_Entry *entry, Slapi_Entry *ad_entry)
{
    int rc = 0;
    int i;

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "addGroupMembership: ==>\n");

    int posixGroup = hasObjectClass(entry, "posixGroup");

    if(!(posixGroup || hasObjectClass(entry, "ntGroup"))) {
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "addGroupMembership: didn't find posixGroup or ntGroup objectclass\n");
        return 0;
    }

    Slapi_Attr * um_attr = NULL; /* Entry attributes uniquemember        */
    Slapi_Attr * muid_attr = NULL; /* Entry attributes memebrof       */
    Slapi_Value * uid_value = NULL; /* uniquemember Attribute values        */

    Slapi_ValueSet *newvs = NULL;

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                    "addGroupMembership: posixGroup -> look for uniquemember\n");
    rc = slapi_entry_attr_find(entry, "uniquemember", &um_attr);
    if (rc != 0 || um_attr == NULL) {
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "addGroupMembership end: attribute uniquemember not found\n");
        return 0;
    }
    /* found attribute uniquemember */
    rc = slapi_entry_attr_find(entry, "memberUid", &muid_attr);
    if (rc != 0 || muid_attr == NULL) { /* Found no memberUid list, so create  */
        slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                        "addGroupMembership: no attribute memberUid\n");
        muid_attr = NULL;
    }
    newvs = slapi_valueset_new();
    /* ...loop for value...    */
    if (posix_winsync_config_get_mapMemberUid()) {
        for (i = slapi_attr_first_value(um_attr, &uid_value); i != -1;
             i = slapi_attr_next_value(um_attr, i, &uid_value)) {
            const char *uid_dn = NULL;
            static char *uid = NULL;
            Slapi_Value *v = NULL;

            uid_dn = slapi_value_get_string(uid_value);
            slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                            "addGroupMembership: perform member %s\n", uid_dn);
            uid = searchUid(uid_dn);
            if (uid == NULL) {
                slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME,
                                "addGroupMembership: uid not found for %s, cannot do anything\n",
                                uid_dn); /* member on longer on server, do nothing */
            } else {
                v = slapi_value_new_string(uid);
                slapi_ch_free_string(&uid);
                if (slapi_attr_value_find(muid_attr, slapi_value_get_berval(v)) != 0) {
                    slapi_valueset_add_value(newvs, v);
                }
                slapi_value_free(&v);
            }
        }
    }

    if (posix_winsync_config_get_mapNestedGrouping()) {
        Slapi_ValueSet *muid_nested_vs = slapi_valueset_new();

        getMembershipFromDownward(entry, newvs, muid_nested_vs, NULL, NULL, 0);
        propogateMembershipUpward(entry, newvs, 0);

        if (posixGroup) {
            addDynamicGroupIfNecessary(entry, NULL);
            slapi_entry_add_valueset(entry, "dsOnlyMemberUid", muid_nested_vs);
        }

        slapi_valueset_free(muid_nested_vs); muid_nested_vs = NULL;   
    }

    if (posixGroup) {
        slapi_entry_add_valueset(entry, "memberUid", newvs);
    }

    slapi_valueset_free(newvs); newvs = NULL;
    posix_winsync_config_get_MOFTaskCreated();

    slapi_log_error(SLAPI_LOG_PLUGIN, POSIX_WINSYNC_PLUGIN_NAME, "addGroupMembership: <==\n");
    return 0;
}

