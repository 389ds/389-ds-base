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

/* parents.c - where the adults live */

#include "back-ldbm.h"

char *numsubordinates = LDBM_NUMSUBORDINATES_STR;
char *hassubordinates = "hassubordinates";
char *tombstone_numsubordinates = LDBM_TOMBSTONE_NUMSUBORDINATES_STR;

/* Routine where any in-memory modification of a parent entry happens on some
 * state-change in one of its children. vaid op values are:
 *     PARENTUPDATE_ADD == child entry newly added,
 *     PARENTUPDATE_DEL == child entry about to be deleted;
 *     3 == child modified, in which case childmods points to the modifications.
 * The child entry passed is in the state which reflects the mods having been
 * appied. op ==3 HAS NOT BEEN IMPLEMENTED YET
 * The routine is allowed to modify the parent entry, and to return a set of
 * LDAPMods reflecting the changes it made. The LDAPMods array must be freed
 * by the called by calling ldap_free_mods(p,1)
 *
 *     PARENTUPDATE_RESURECT == turning a tombstone into an entry
 *                              tombstone_numsubordinates--
 *                              numsubordinates++
 */
/*
 * PARENTUPDATE_CREATE_TOMBSTONE: turning an entry into a tombstone
 *                                numsubordinates--
 *                                tombstone_numsubordinates++
 * PARENTUPDATE_DELETE_TOMBSTONE: don't touch numsubordinates, and
 *                                tombstone_numsubordinates--
 */

int
parent_update_on_childchange(modify_context *mc, int op, size_t *new_sub_count)
{
    int ret = 0;
    int mod_op = 0;
    Slapi_Attr *read_attr = NULL;
    size_t current_sub_count = 0;
    int already_present = 0;
    int repl_op = 0;
    Slapi_Mods *smods = NULL;
    char value_buffer[22] = {0}; /* enough digits for 2^64 children */

    if (new_sub_count)
        *new_sub_count = 0;

    repl_op = PARENTUPDATE_TOMBSTONE_MASK & op;
    op &= PARENTUPDATE_MASK;

    /* Check nobody is trying to use op == 3, it's not implemented yet */
    PR_ASSERT((op == PARENTUPDATE_ADD) || (op == PARENTUPDATE_DEL) || (op == PARENTUPDATE_RESURECT));

    /* We want to invent a mods set to be passed to modify_apply_mods() */

    /* For now, we're only interested in subordinatecount.
       We first examine the present value for the attribute.
       If it isn't present and we're adding, we assign value 1 to the attribute and add it.
       If it is present, we increment or decrement depending upon whether we're adding or deleting.
       If the value after decrementing is zero, we remove it.
    */

    smods = slapi_mods_new();

    /* Get the present value of the subcount attr, or 0 if not present */
    ret = slapi_entry_attr_find(mc->old_entry->ep_entry, numsubordinates, &read_attr);
    if (0 == ret) {
        /* decode the value */
        Slapi_Value *sval;
        slapi_attr_first_value(read_attr, &sval);
        if (sval != NULL) {
            const struct berval *bval = slapi_value_get_berval(sval);
            if (NULL != bval) {
                already_present = 1;
                current_sub_count = atol(bval->bv_val);
            }
        }
    }

    if ((PARENTUPDATE_ADD == op) && (PARENTUPDATE_CREATE_TOMBSTONE == repl_op)) {
        /* we are directly adding a tombstone entry, only need to
         * update the tombstone subordinates
         */
    } else if (PARENTUPDATE_DELETE_TOMBSTONE != repl_op) {
        /* are we adding ? */
        if (((PARENTUPDATE_ADD == op) || (PARENTUPDATE_RESURECT == op)) && !already_present) {
            /* If so, and the parent entry does not already have a subcount
             * attribute, we need to add it */
            mod_op = LDAP_MOD_ADD;
        } else if (PARENTUPDATE_DEL == op) {
            if (!already_present) {
                /* This means that there was a conflict.  Before coming to this point,
                 * the entry to be deleted was deleted... */
                slapi_log_err(SLAPI_LOG_ERR, "parent_update_on_childchange",
                              "Parent %s has no children. (op 0x%x, repl_op 0x%x)\n",
                              slapi_entry_get_dn(mc->old_entry->ep_entry), op, repl_op);
                slapi_mods_free(&smods);
                return -1;
            } else {
                if (current_sub_count == 1) {
                    mod_op = LDAP_MOD_DELETE;
                } else {
                    mod_op = LDAP_MOD_REPLACE;
                }
            }
        } else {
            /* (PARENTUPDATE_ADD == op) && already_present */
            mod_op = LDAP_MOD_REPLACE;
        }

        /* Now compute the new value */
        if ((PARENTUPDATE_ADD == op) || (PARENTUPDATE_RESURECT == op)) {
            current_sub_count++;
        } else {
            current_sub_count--;
        }

        if (mod_op == LDAP_MOD_DELETE) {
            slapi_mods_add(smods, mod_op | LDAP_MOD_BVALUES,
                           numsubordinates, 0, NULL);
        } else {
            sprintf(value_buffer, "%lu", (long unsigned int)current_sub_count);
            slapi_mods_add(smods, mod_op | LDAP_MOD_BVALUES,
                           numsubordinates, strlen(value_buffer), value_buffer);
        }
        if (new_sub_count) {
            *new_sub_count = current_sub_count;
        }
    }

    /* tombstoneNumSubordinates has to be updated if a tombstone child has been
     * deleted or a tombstone has been directly added (cenotaph)
     * or a tombstone is resurrected
     */
    current_sub_count = LDAP_MAXINT;
    if (repl_op || (PARENTUPDATE_RESURECT == op)) {
        ret = slapi_entry_attr_find(mc->old_entry->ep_entry,
                                    tombstone_numsubordinates, &read_attr);
        if (0 == ret) {
            /* decode the value */
            Slapi_Value *sval;
            slapi_attr_first_value(read_attr, &sval);
            if (sval != NULL) {
                const struct berval *bval = slapi_value_get_berval(sval);
                if (NULL != bval) {
                    current_sub_count = atol(bval->bv_val);
                }
            }
        }

        if ((PARENTUPDATE_DELETE_TOMBSTONE == repl_op) || (PARENTUPDATE_RESURECT == op)) {
            /* deleting a tombstone entry:
             * reaping or manually deleting it */
            if ((current_sub_count != LDAP_MAXINT) &&
                (current_sub_count > 0)) {
                current_sub_count--;
                mod_op = LDAP_MOD_REPLACE;
                sprintf(value_buffer, "%lu", (long unsigned int)current_sub_count);
                slapi_mods_add(smods, mod_op | LDAP_MOD_BVALUES,
                               tombstone_numsubordinates,
                               strlen(value_buffer), value_buffer);
            }
        } else if (PARENTUPDATE_CREATE_TOMBSTONE == repl_op) {
            /* creating a tombstone entry */
            if (current_sub_count != LDAP_MAXINT) {
                current_sub_count++;
            } else { /* tombstonenumsubordinates does not exist */
                current_sub_count = 1;
            }
            mod_op = LDAP_MOD_REPLACE;
            sprintf(value_buffer, "%lu", (long unsigned int)current_sub_count);
            slapi_mods_add(smods, mod_op | LDAP_MOD_BVALUES,
                           tombstone_numsubordinates,
                           strlen(value_buffer), value_buffer);
        }
    }

    ret = modify_apply_mods(mc, smods); /* smods passed in */
    return ret;
}
