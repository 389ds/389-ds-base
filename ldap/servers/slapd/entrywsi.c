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


/* entrywsi.c - routines for dealing with entries... With State Information */

#include "slap.h"
#include "slapi-plugin.h"

static int
entry_present_value_to_deleted_value(Slapi_Attr *a, Slapi_Value *v)
{
    Slapi_Value *r = valueset_remove_value(a, &a->a_present_values, v);
    if (r != NULL) {
        slapi_valueset_add_attr_value_ext(a, &a->a_deleted_values, r, SLAPI_VALUE_FLAG_PASSIN);
    }
    return LDAP_SUCCESS;
}

static int
entry_present_value_to_zapped_value(Slapi_Attr *a, Slapi_Value *v)
{
    if (v != NULL) {
        Slapi_Value *r = valueset_remove_value(a, &a->a_present_values, v);
        if (r != NULL) {
            slapi_value_free(&r);
        }
    }
    return LDAP_SUCCESS;
}

static int
entry_deleted_value_to_present_value(Slapi_Attr *a, Slapi_Value *v)
{
    Slapi_Value *r = valueset_remove_value(a, &a->a_deleted_values, v);
    if (r != NULL) {
        slapi_valueset_add_attr_value_ext(a, &a->a_present_values, r, SLAPI_VALUE_FLAG_PASSIN);
    }
    return LDAP_SUCCESS;
}

static int
entry_deleted_value_to_zapped_value(Slapi_Attr *a, Slapi_Value *v)
{
    if (v != NULL) {
        Slapi_Value *r = valueset_remove_value(a, &a->a_deleted_values, v);
        if (r != NULL) {
            slapi_value_free(&r);
        }
    }
    return LDAP_SUCCESS;
}

static int
entry_present_attribute_to_deleted_attribute(Slapi_Entry *e, Slapi_Attr *a)
{
    attrlist_remove(&e->e_attrs, a->a_type);
    attrlist_add(&e->e_deleted_attrs, a);
    return LDAP_SUCCESS;
}

static int
entry_deleted_attribute_to_present_attribute(Slapi_Entry *e, Slapi_Attr *a)
{
    attrlist_remove(&e->e_deleted_attrs, a->a_type);
    attrlist_add(&e->e_attrs, a);
    return LDAP_SUCCESS;
}

/*
 * Get the first deleted attribute.
 *
 * Return  0: Return the type and the CSN of the deleted attribute.
 * Return -1: There are no deleted attributes.
 */
int
entry_first_deleted_attribute(const Slapi_Entry *e, Slapi_Attr **a)
{
    *a = e->e_deleted_attrs;
    return (*a ? 0 : -1);
}

/*
 * Get the next deleted attribute.
 *
 * Return  0: the type and the CSN of the deleted attribute.
 * Return -1: no deleted attributes.
 */
int
entry_next_deleted_attribute(const Slapi_Entry *e __attribute__((unused)), Slapi_Attr **a)
{
    *a = (*a)->a_next;
    return (*a ? 0 : -1);
}

const CSN *
entry_get_maxcsn(const Slapi_Entry *entry)
{
    return entry->e_maxcsn;
}

void
entry_set_maxcsn(Slapi_Entry *entry, const CSN *csn)
{
    if (NULL == entry->e_maxcsn) {
        entry->e_maxcsn = csn_dup(csn);
    } else if (csn_compare(entry->e_maxcsn, csn) < 0) {
        csn_init_by_csn(entry->e_maxcsn, csn);
    }
}

/*
 * Get the DN CSN of an entry.
 */
const CSN *
entry_get_dncsn(const Slapi_Entry *entry)
{
    return csnset_get_last_csn(entry->e_dncsnset);
}

/*
 * Get the DN CSN set of an entry.
 */
const CSNSet *
entry_get_dncsnset(const Slapi_Entry *entry)
{
    return entry->e_dncsnset;
}

/*
 * Add a DN CSN to an entry.
 */
int
entry_add_dncsn(Slapi_Entry *entry, const CSN *csn)
{
    PR_ASSERT(entry != NULL);
    csnset_update_csn(&entry->e_dncsnset, CSN_TYPE_VALUE_DISTINGUISHED, csn);
    return 0;
}

/*
 * Add a DN CSN to an entry, but uses flags to control the behavior
 * Using the ENTRY_DNCSN_INCREASING flag makes sure the csnset is in
 * order of increasing csn. csnset_insert_csn may not be very fast, so
 * we may have to revisit this if it becomes a performance problem.
 * In most cases, storing the csn unsorted is ok since the server
 * usually makes sure the csn is already in order.  However, when doing
 * a str2entry, the order is not preserved unless we sort it.
 */
int
entry_add_dncsn_ext(Slapi_Entry *entry, const CSN *csn, PRUint32 flags __attribute__((unused)))
{
    PR_ASSERT(entry != NULL);
    csnset_update_csn(&entry->e_dncsnset, CSN_TYPE_VALUE_DISTINGUISHED, csn);
    return 0;
}

/*
 * Set the CSN for all the present values on the entry.
 * This is only intended to be used for new entries
 * being added.
 */
int
entry_set_csn(Slapi_Entry *entry, const CSN *csn)
{
    Slapi_Attr *a;

    PR_ASSERT(entry != NULL);

    slapi_entry_first_attr(entry, &a);
    while (a != NULL) {
        /*
         * JCM - it'd be more efficient if the str2entry code
         * set a flag on the attribute structure.
         */
        if (strcasecmp(a->a_type, SLAPI_ATTR_UNIQUEID) != 0) {
            attr_set_csn(a, csn);
        }
        slapi_entry_next_attr(entry, a, &a);
    }
    return 0;
}

/*
 * Set the Distinguished CSN for the RDN components of the entry.
 */
void
entry_add_rdn_csn(Slapi_Entry *e, const CSN *csn)
{
    char *type;
    char *value;
    int index;
    const Slapi_DN *dn = slapi_entry_get_sdn_const(e);
    Slapi_RDN *rdn = slapi_rdn_new_sdn(dn);
    index = slapi_rdn_get_first(rdn, &type, &value);
    while (index != -1) {
        Slapi_Attr *a = NULL;
        Slapi_Value *v = NULL;
        if ((entry_attr_find_wsi(e, type, &a) == ATTRIBUTE_PRESENT) && (a != NULL)) {
            struct berval bv;
            bv.bv_len = strlen(value);
            bv.bv_val = (void *)value;
            if (attr_value_find_wsi(a, &bv, &v) == VALUE_DELETED) {
                v = NULL;
            }
        }
        if (v != NULL) {
            value_update_csn(v, CSN_TYPE_VALUE_DISTINGUISHED, csn);
        } else {
            /* JCM RDN component isn't a present value - this is illegal. */
        }
        index = slapi_rdn_get_next(rdn, index, &type, &value);
    }
    slapi_rdn_free(&rdn);
}

int32_t
entry_assign_operation_csn(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *parententry, CSN **opcsn)
{
    Slapi_Operation *op;
    const CSN *basecsn = NULL;
    const CSN *parententry_dncsn = NULL;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);

    /*
     * The replication pre-op would have set op->o_csngen_handler for
     * user requests that are against a replica.
     */
    if (op->o_csngen_handler) {
        /*
         * Sync up the CSN generator so that the new csn is greater
         * than the entry's maxcsn and/or the parent's max dncsn.
         */
        if (e) {
            basecsn = entry_get_maxcsn(e);
        }
        if (parententry) {
            parententry_dncsn = entry_get_dncsn(parententry);
            if (csn_compare(parententry_dncsn, basecsn) > 0) {
                basecsn = parententry_dncsn;
            }
        }
        if(op->o_csngen_handler(pb, basecsn, opcsn) != 0) {
            return -1;
        }

        if (*opcsn) {
            operation_set_csn(op, *opcsn);
        }
    }

    return 0;
}

/*
 * Purge state information from the entry older than csnUpTo
 *
 * if csnUpTo is NULL, get rid of all the CSN related info.
 * if csnUpTo is non-NULL, purge all info older than csnUpTo
 */
void
entry_purge_state_information(Slapi_Entry *e, const CSN *csnUpTo)
{
    Slapi_Attr *a = NULL;

    PR_ASSERT(e != NULL);

    for (a = e->e_attrs; NULL != a; a = a->a_next) {
        /*
         * we are passing in the entry so that we may be able to "optimize"
         * the csn related information and roll it up higher to the level
         * of entry
         */
        attr_purge_state_information(e, a, csnUpTo);
    }
    for (a = e->e_deleted_attrs; NULL != a; a = a->a_next) {
        /*
         * we are passing in the entry so that we may be able to "optimize"
         * the csn related information and roll it up higher to the level
         * of entry
         */
        attr_purge_state_information(e, a, csnUpTo);
    }
    csnset_purge(&e->e_dncsnset, csnUpTo);
}

/*
 * Look for the attribute on the present and deleted attribute lists.
 */
int
entry_attr_find_wsi(Slapi_Entry *e, const char *type, Slapi_Attr **a)
{
    int retVal = ATTRIBUTE_NOTFOUND;

    PR_ASSERT(e != NULL);
    PR_ASSERT(type != NULL);
    PR_ASSERT(a != NULL);

    /* Look on the present attribute list */
    *a = attrlist_find(e->e_attrs, type);
    if (*a != NULL) {
        /* The attribute is present */
        retVal = ATTRIBUTE_PRESENT;
    } else {
        /* Maybe the attribue was deleted... */
        *a = attrlist_find(e->e_deleted_attrs, type);
        if (*a != NULL) {
            /* The attribute is deleted */
            retVal = ATTRIBUTE_DELETED;
        } else {
            /* The attribute was not found */
            retVal = ATTRIBUTE_NOTFOUND;
        }
    }
    return retVal;
}

/*
 * Add the attribute to the deleted attribute list.
 *
 * Consumes the attribute.
 */
int
entry_add_deleted_attribute_wsi(Slapi_Entry *e, Slapi_Attr *a)
{
    PR_ASSERT(e != NULL);
    PR_ASSERT(a != NULL);
    attrlist_add(&e->e_deleted_attrs, a);
    return 0;
}

/*
 * Add the attribute to the present attribute list.
 *
 * Consumes the attribute.
 */
int
entry_add_present_attribute_wsi(Slapi_Entry *e, Slapi_Attr *a)
{
    PR_ASSERT(e != NULL);
    PR_ASSERT(a != NULL);
    attrlist_add(&e->e_attrs, a);
    return 0;
}

/*
 * Add a list of values to the attribute, whilst maintaining state information.
 *
 * Preserves LDAP Information Model constraints,
 * returning an LDAP result code.
 */
static Slapi_Value *attr_most_recent_deleted_value(Slapi_Attr *a);
static void resolve_single_valued_two_values(Slapi_Entry *e, Slapi_Attr *a, int attribute_state, Slapi_Value *current_value, Slapi_Value *second_current_value);
static void resolve_single_valued_check_restore_deleted_value(Slapi_Entry *e, Slapi_Attr *a);
static void resolve_single_valued_zap_current(Slapi_Entry *e, Slapi_Attr *a);
static void resolve_single_valued_set_adcsn(Slapi_Attr *a);
static void resolve_single_valued_zap_deleted(Slapi_Attr *a);
static void resolve_attribute_state_single_valued(Slapi_Entry *e, Slapi_Attr *a, int attribute_state);
static void resolve_attribute_state_deleted_to_present(Slapi_Entry *e, Slapi_Attr *a, Slapi_Value **valuestoupdate);
static void resolve_attribute_state_present_to_deleted(Slapi_Entry *e, Slapi_Attr *a, Slapi_Value **valuestoupdate);
static void resolve_attribute_state_to_present_or_deleted(Slapi_Entry *e, Slapi_Attr *a, Slapi_Value **valuestoupdate, int attribute_state);
static int entry_add_present_values_wsi_single_valued(Slapi_Entry *e, const char *type, struct berval **bervals, const CSN *csn, int urp, long flags);
static int entry_add_present_values_wsi_multi_valued(Slapi_Entry *e, const char *type, struct berval **bervals, const CSN *csn, int urp, long flags);

static int
entry_add_present_values_wsi(Slapi_Entry *e, const char *type, struct berval **bervals, const CSN *csn, int urp, long flags __attribute__((unused)))
{
    int retVal = LDAP_SUCCESS;
    Slapi_Attr *a = NULL;
    int attr_state = entry_attr_find_wsi(e, type, &a);
    if (ATTRIBUTE_NOTFOUND == attr_state) {
        /* Create a new attribute */
        a = slapi_attr_new();
        slapi_attr_init(a, type);
        attrlist_add(&e->e_attrs, a);
    }

    if (slapi_attr_flag_is_set(a, SLAPI_ATTR_FLAG_SINGLE)) {
        retVal = entry_add_present_values_wsi_single_valued(e, a->a_type, bervals, csn, urp, 0);
    } else {
        retVal = entry_add_present_values_wsi_multi_valued(e, a->a_type, bervals, csn, urp, 0);
    }
    return retVal;
}

/* Used for debug purpose, it dumps into the error log the
 * entry with the replication stateinfo
 */
#if 0
static void
entry_dump_stateinfo(char *msg, Slapi_Entry* e)
{
	char *s;
	int32_t len = 0;

	s = slapi_entry2str_with_options(e, &len, SLAPI_DUMP_STATEINFO);
	slapi_log_err(SLAPI_LOG_ERR, msg, "%s\n", s);
	slapi_ch_free((void **)&s);
}
#endif

static int
entry_add_present_values_wsi_single_valued(Slapi_Entry *e, const char *type, struct berval **bervals, const CSN *csn, int urp, long flags)
{
    int retVal = LDAP_SUCCESS;
    Slapi_Value **valuestoadd = NULL;

    valuearray_init_bervalarray(bervals, &valuestoadd); /* JCM SLOW FUNCTION */
    if (!valuearray_isempty(valuestoadd)) {
        Slapi_Attr *a = NULL;
        long a_flags_orig;
        int attr_state = entry_attr_find_wsi(e, type, &a);
        const CSN *adcsn = attr_get_deletion_csn(a);
        if (csn_compare(csn, adcsn) < 0) {
            /* the attribute was deleted with an adcsn
             * newer than the current csn.
             * Nothing to do.
             */
            goto done;
        }

        a_flags_orig = a->a_flags;
        a->a_flags |= flags;
        /* Check if the type of the to-be-added values has DN syntax or not. */
        if (slapi_attr_is_dn_syntax_attr(a)) {
            valuearray_dn_normalize_value(valuestoadd);
            a->a_flags |= SLAPI_ATTR_FLAG_NORMALIZED_CES;
        }
        if (urp) {
            valueset_remove_valuearray(&a->a_present_values, a, valuestoadd,
                                       SLAPI_VALUE_FLAG_IGNOREERROR |
                                           SLAPI_VALUE_FLAG_PRESERVECSNSET,
                                       NULL);
            valueset_remove_valuearray(&a->a_deleted_values, a, valuestoadd,
                                       SLAPI_VALUE_FLAG_IGNOREERROR |
                                           SLAPI_VALUE_FLAG_PRESERVECSNSET,
                                       NULL);
            valuearray_update_csn(valuestoadd, CSN_TYPE_VALUE_UPDATED, csn);
            slapi_valueset_add_attr_valuearray_ext(a, &a->a_present_values, valuestoadd, valuearray_count(valuestoadd), SLAPI_VALUE_FLAG_PASSIN, NULL);
            slapi_ch_free((void **)&valuestoadd);
            /*
             * Now delete non-RDN values from a->a_present_values; and
             * restore possible RDN values from a->a_deleted_values
             */
            resolve_attribute_state_single_valued(e, a, attr_state);
            retVal = LDAP_SUCCESS;
        } else {
            Slapi_Value **deletedvalues = NULL;
            switch (attr_state) {
            case ATTRIBUTE_PRESENT:
                /* The attribute is already on the present list */
                break;
            case ATTRIBUTE_DELETED:
                /* Move the deleted attribute onto the present list */
                entry_deleted_attribute_to_present_attribute(e, a);
                break;
            case ATTRIBUTE_NOTFOUND:
                /* No-op - attribute was initialized & added to entry above */
                break;
            }
            /* Check if any of the values to be added are on the deleted list */
            valueset_remove_valuearray(&a->a_deleted_values,
                                       a, valuestoadd,
                                       SLAPI_VALUE_FLAG_IGNOREERROR | SLAPI_VALUE_FLAG_USENEWVALUE,
                                       &deletedvalues); /* JCM Check return code */
            if (deletedvalues != NULL && deletedvalues[0] != NULL) {
                /* Some of the values to be added were on the deleted list */
                Slapi_Value **v = NULL;
                Slapi_ValueSet vs;
                /* Add each deleted value to the present list */
                valuearray_update_csn(deletedvalues, CSN_TYPE_VALUE_UPDATED, csn);
                slapi_valueset_add_attr_valuearray_ext(a, &a->a_present_values, deletedvalues, valuearray_count(deletedvalues), SLAPI_VALUE_FLAG_PASSIN, NULL);
                /* Remove the deleted values from the values to add */
                valueset_set_valuearray_passin(&vs, valuestoadd);
                valueset_remove_valuearray(&vs, a, deletedvalues, SLAPI_VALUE_FLAG_IGNOREERROR, &v);
                valuestoadd = valueset_get_valuearray(&vs);
                valuearray_free(&v);
                slapi_ch_free((void **)&deletedvalues);
            }
            valuearray_update_csn(valuestoadd, CSN_TYPE_VALUE_UPDATED, csn);
            retVal = attr_add_valuearray(a, valuestoadd, slapi_entry_get_dn_const(e));
        }
        a->a_flags = a_flags_orig;
    }
done:
    valuearray_free(&valuestoadd);

    return (retVal);
}

static int
entry_add_present_values_wsi_multi_valued(Slapi_Entry *e, const char *type, struct berval **bervals, const CSN *csn, int urp, long flags)
{
    int retVal = LDAP_SUCCESS;
    Slapi_Value **valuestoadd = NULL;

    valuearray_init_bervalarray(bervals, &valuestoadd); /* JCM SLOW FUNCTION */
    if (!valuearray_isempty(valuestoadd)) {
        Slapi_Attr *a = NULL;
        long a_flags_orig;
        int attr_state = entry_attr_find_wsi(e, type, &a);

        const CSN *adcsn = attr_get_deletion_csn(a);
        if (csn_compare(csn, adcsn) < 0) {
            /* the attribute was deleted with an adcsn
             * newer than the current csn.
             * Nothing to do.
             */
            goto done;
        }
        a_flags_orig = a->a_flags;
        a->a_flags |= flags;
        /* Check if the type of the to-be-added values has DN syntax or not. */
        if (slapi_attr_is_dn_syntax_attr(a)) {
            valuearray_dn_normalize_value(valuestoadd);
            a->a_flags |= SLAPI_ATTR_FLAG_NORMALIZED_CES;
        }
        if (urp) {
            /*
             * Consolidate a->a_present_values and the pending values:
             * Delete the pending values from a->a_present_values
             * and transfer their csnsets to valuestoadd.
             */
            valueset_remove_valuearray(&a->a_present_values, a, valuestoadd,
                                       SLAPI_VALUE_FLAG_IGNOREERROR |
                                           SLAPI_VALUE_FLAG_PRESERVECSNSET,
                                       NULL);
            /*
             * Consolidate a->a_deleted_values and the pending values
             * similarly.
             */
            valueset_remove_valuearray(&a->a_deleted_values, a, valuestoadd,
                                       SLAPI_VALUE_FLAG_IGNOREERROR |
                                           SLAPI_VALUE_FLAG_PRESERVECSNSET,
                                       NULL);

            /* Now add the values in values to add to present or deleted
             * values, depending on their csnset */
            valuearray_update_csn(valuestoadd, CSN_TYPE_VALUE_UPDATED, csn);

            resolve_attribute_state_to_present_or_deleted(e, a, valuestoadd, attr_state);

            slapi_ch_free((void **)&valuestoadd);

            retVal = LDAP_SUCCESS;
        } else {
            Slapi_Value **deletedvalues = NULL;

            switch (attr_state) {
            case ATTRIBUTE_PRESENT:
                /* The attribute is already on the present list */
                break;
            case ATTRIBUTE_DELETED:
                /* Move the deleted attribute onto the present list */
                entry_deleted_attribute_to_present_attribute(e, a);
                break;
            case ATTRIBUTE_NOTFOUND:
                /* No-op - attribute was initialized & added to entry above */
                break;
            }
            /* Check if any of the values to be added are on the deleted list */
            valueset_remove_valuearray(&a->a_deleted_values,
                                       a, valuestoadd,
                                       SLAPI_VALUE_FLAG_IGNOREERROR | SLAPI_VALUE_FLAG_USENEWVALUE,
                                       &deletedvalues); /* JCM Check return code */
            if (deletedvalues != NULL && deletedvalues[0] != NULL) {
                /* Some of the values to be added were on the deleted list */
                Slapi_Value **v = NULL;
                Slapi_ValueSet vs;
                /* Add each deleted value to the present list */
                valuearray_update_csn(deletedvalues, CSN_TYPE_VALUE_UPDATED, csn);
                slapi_valueset_add_attr_valuearray_ext(a, &a->a_present_values, deletedvalues, valuearray_count(deletedvalues), SLAPI_VALUE_FLAG_PASSIN, NULL);
                /* Remove the deleted values from the values to add */
                valueset_set_valuearray_passin(&vs, valuestoadd);
                valueset_remove_valuearray(&vs, a, deletedvalues, SLAPI_VALUE_FLAG_IGNOREERROR, &v);
                valuestoadd = valueset_get_valuearray(&vs);
                valuearray_free(&v);
                slapi_ch_free((void **)&deletedvalues);
            }
            valuearray_update_csn(valuestoadd, CSN_TYPE_VALUE_UPDATED, csn);
            retVal = attr_add_valuearray(a, valuestoadd, slapi_entry_get_dn_const(e));
        }
        a->a_flags = a_flags_orig;
    }
done:
    valuearray_free(&valuestoadd);

    return (retVal);
}

/*
 * Delete a list of values from an attribute, whilst maintaining state information.
 *
 * Preserves LDAP Information Model constraints,
 * returning an LDAP result code.
 */
static int
entry_delete_present_values_wsi_single_valued(Slapi_Entry *e, const char *type, struct berval **vals, const CSN *csn, int urp, int mod_op, struct berval **replacevals);
static int
entry_delete_present_values_wsi_multi_valued(Slapi_Entry *e, const char *type, struct berval **vals, const CSN *csn, int urp, int mod_op, struct berval **replacevals);

static int
entry_delete_present_values_wsi(Slapi_Entry *e, const char *type, struct berval **vals, const CSN *csn, int urp, int mod_op, struct berval **replacevals)
{
    int retVal = LDAP_SUCCESS;
    Slapi_Attr *a = NULL;
    int attr_state = entry_attr_find_wsi(e, type, &a);

    if (attr_state == ATTRIBUTE_PRESENT || (attr_state == ATTRIBUTE_DELETED && urp)) {
        if (slapi_attr_flag_is_set(a, SLAPI_ATTR_FLAG_SINGLE))
            retVal = entry_delete_present_values_wsi_single_valued(e, type, vals, csn, urp, mod_op, replacevals);
        else
            retVal = entry_delete_present_values_wsi_multi_valued(e, type, vals, csn, urp, mod_op, replacevals);
    } else if (attr_state == ATTRIBUTE_DELETED) {
#if defined(USE_OLD_UNHASHED)
        if (is_type_forbidden(type)) {
            retVal = LDAP_SUCCESS;
        } else {
            retVal = LDAP_NO_SUCH_ATTRIBUTE;
        }
#else
        retVal = LDAP_NO_SUCH_ATTRIBUTE;
#endif
    } else if (attr_state == ATTRIBUTE_NOTFOUND) {
#if defined(USE_OLD_UNHASHED)
        if (is_type_protected(type) || is_type_forbidden(type))
#else
        if (is_type_protected(type))
#endif
        {
            retVal = LDAP_SUCCESS;
        } else {
            if (!urp) {
                slapi_log_err(SLAPI_LOG_ARGS, "entry_delete_present_values_wsi",
                              "could not find attribute %s\n", type);
            }
            retVal = LDAP_NO_SUCH_ATTRIBUTE;
        }
        if ((LDAP_MOD_REPLACE == mod_op)) {
            /* Create a new attribute and set the adcsn
                         * We need to do this always to set the deletion csn
                         * The attribute would otherwise be created in the
                         * entry_add_.. function
                         */
            Slapi_Attr *create_a = slapi_attr_new();
            slapi_attr_init(create_a, type);
            attr_set_deletion_csn(create_a, csn);
            entry_add_deleted_attribute_wsi(e, create_a);
        }
    }
    return retVal;
}
static int
entry_delete_present_values_wsi_single_valued(Slapi_Entry *e, const char *type, struct berval **vals, const CSN *csn, int urp, int mod_op __attribute__((unused)), struct berval **replacevals __attribute__((unused)))
{
    int retVal = LDAP_SUCCESS;
    Slapi_Attr *a = NULL;
    int attr_state = entry_attr_find_wsi(e, type, &a);
    /* The attribute is on the present list, or the deleted list and we're doing URP */
    if (vals == NULL || vals[0] == NULL) {
        /* delete the entire attribute */
        slapi_log_err(SLAPI_LOG_ARGS, "entry_delete_present_values_wsi_single_valued",
                      "removing entire attribute %s\n", type);
        const CSN *adcsn = attr_get_deletion_csn(a);
        if (csn_compare(csn, adcsn) < 0) {
            /* the attribute was deleted with an adcsn
             * newer than the current csn.
             * Nothing to do.
             */
            return LDAP_SUCCESS;
        }
        attr_set_deletion_csn(a, csn);
        if (urp) {
            resolve_attribute_state_single_valued(e, a, attr_state);
            /* resolve attr state single valued */
            /* keep_present = check_attr_single_value_is_distingiuished(&a->a_present_values);
            if ( !keep_present) {
                slapi_valueset_done(&a->a_present_values);
                entry_present_attribute_to_deleted_attribute(e, a);
                }
             */
        } else {
            slapi_valueset_done(&a->a_present_values);
            entry_present_attribute_to_deleted_attribute(e, a);
        }
        retVal = LDAP_SUCCESS; /* This Operation always succeeds when the attribute is Present */
    } else {
        /* delete some specific values */
        Slapi_Value **valuestodelete = NULL;
        valuearray_init_bervalarray(vals, &valuestodelete); /* JCM SLOW FUNCTION */
        /* Check if the type of the to-be-deleted values has DN syntax
         * or not. */
        if (slapi_attr_is_dn_syntax_attr(a)) {
            valuearray_dn_normalize_value(valuestodelete);
            a->a_flags |= SLAPI_ATTR_FLAG_NORMALIZED_CES;
        }
        if (urp) {
            Slapi_Value **valuesupdated = NULL;
            valueset_update_csn_for_valuearray(&a->a_present_values, a, valuestodelete, CSN_TYPE_VALUE_DELETED, csn, &valuesupdated);
            if (valuesupdated && *valuesupdated) {
                attr_set_deletion_csn(a, csn);
            }
            /* resolve attr state single valued */
            valuearray_free(&valuesupdated);
            valueset_update_csn_for_valuearray(&a->a_deleted_values, a, valuestodelete, CSN_TYPE_VALUE_DELETED, csn, &valuesupdated);
            valuearray_free(&valuesupdated);
            valuearray_update_csn(valuestodelete, CSN_TYPE_VALUE_DELETED, csn);
            slapi_valueset_add_attr_valuearray_ext(a, &a->a_deleted_values, valuestodelete, valuearray_count(valuestodelete), SLAPI_VALUE_FLAG_PASSIN, NULL);
            slapi_ch_free((void **)&valuestodelete);
            resolve_attribute_state_single_valued(e, a, attr_state);
            retVal = LDAP_SUCCESS;
        } else {
            Slapi_Value **deletedvalues = NULL;
            retVal = valueset_remove_valuearray(&a->a_present_values, a, valuestodelete, 0 /* Do Not Ignore Errors */, &deletedvalues);
            if (retVal == LDAP_SUCCESS && deletedvalues != NULL) {
                valuearray_free(&deletedvalues);
                /* The attribute is single valued and the value was successful deleted */
                /* but there could have been an add in the same operation, so double check */
                if (valueset_isempty(&a->a_present_values)) {
                    /* A doubt here, a direct update deletes the last value
                     * of a single valued attribute. It will only contain deleted values.
                     * Why not setting the adcsn (attr_set_deletion_csn) ?
                     */
                    entry_present_attribute_to_deleted_attribute(e, a);
                }
            } else if (retVal != LDAP_SUCCESS) {
                /* Failed
                 * - Value not found
                 * - Operations error
                 */
                if (retVal == LDAP_OPERATIONS_ERROR) {
                    slapi_log_err(SLAPI_LOG_ERR, "entry_delete_present_values_wsi_single_valued",
                                  "Possible existing duplicate value for attribute type %s found in entry %s\n",
                                  a->a_type, slapi_entry_get_dn_const(e));
                }
            }
            valuearray_free(&valuestodelete);
        }
    }
    return (retVal);
}
static int
entry_delete_present_values_wsi_multi_valued(Slapi_Entry *e, const char *type, struct berval **vals, const CSN *csn, int urp, int mod_op __attribute__((unused)), struct berval **replacevals __attribute__((unused)))
{
    int retVal = LDAP_SUCCESS;
    Slapi_Attr *a = NULL;
    int attr_state = entry_attr_find_wsi(e, type, &a);
    /* The attribute is on the present list, or the deleted list and we're doing URP */
    if (vals == NULL || vals[0] == NULL) {
        /* delete the entire attribute */
        slapi_log_err(SLAPI_LOG_ARGS, "entry_delete_present_values_wsi_multi_valued",
                      "Removing entire attribute %s\n", type);
        const CSN *adcsn = attr_get_deletion_csn(a);
        if (csn_compare(csn, adcsn) < 0) {
            /* the attribute was deleted with an adcsn
                 * newer than the current csn.
                 * Nothing to do.
                 */
            return LDAP_SUCCESS;
        }
        attr_set_deletion_csn(a, csn);
        if (urp) {
            /* there might be values added or specifically deleted later than
                 * the current attr delete operation. These values need to be
                 * preserved, all others can be removed, purging should o the job.
                 */
            valueset_purge(a, &a->a_present_values, csn);
            valueset_purge(a, &a->a_deleted_values, csn);
            if (attr_state == ATTRIBUTE_PRESENT && valueset_isempty(&a->a_present_values))
                entry_present_attribute_to_deleted_attribute(e, a);
        } else {
            slapi_valueset_done(&a->a_present_values);
            slapi_valueset_done(&a->a_deleted_values);
            entry_present_attribute_to_deleted_attribute(e, a);
        }
        retVal = LDAP_SUCCESS; /* This Operation always succeeds when the attribute is Present */
    } else {
        /* delete some specific values */
        Slapi_Value **valuestodelete = NULL;
        valuearray_init_bervalarray(vals, &valuestodelete); /* JCM SLOW FUNCTION */
        /* Check if the type of the to-be-deleted values has DN syntax
             * or not. */
        if (slapi_attr_is_dn_syntax_attr(a)) {
            valuearray_dn_normalize_value(valuestodelete);
            a->a_flags |= SLAPI_ATTR_FLAG_NORMALIZED_CES;
        }
        if (urp) {
            /* check if values to delete are in present values
                 * if v in present values and VU-csn<csn and v not distinguished move to deleted values
                 * if v not in present values, check deleted values and update csn, if distinguisehed at csn
                 *     move back to present values
                 */


            Slapi_Value **valuesupdated = NULL;
            valueset_update_csn_for_valuearray_ext(&a->a_present_values, a, valuestodelete, CSN_TYPE_VALUE_DELETED, csn, &valuesupdated, 1);
            resolve_attribute_state_present_to_deleted(e, a, valuesupdated);
            valuearray_free(&valuesupdated);

            valueset_update_csn_for_valuearray_ext(&a->a_deleted_values, a, valuestodelete, CSN_TYPE_VALUE_DELETED, csn, &valuesupdated, 1);
            resolve_attribute_state_deleted_to_present(e, a, valuesupdated);
            valuearray_free(&valuesupdated);

            valuearray_update_csn(valuestodelete, CSN_TYPE_VALUE_DELETED, csn);
            slapi_valueset_add_attr_valuearray_ext(a, &a->a_deleted_values, valuestodelete, valuearray_count(valuestodelete), SLAPI_VALUE_FLAG_PASSIN, NULL);
            if (attr_state == ATTRIBUTE_PRESENT && valueset_isempty(&a->a_present_values))
                entry_present_attribute_to_deleted_attribute(e, a);
            /* all the elements in valuestodelete are passed;
                 * should free valuestodelete only (don't call valuearray_free)
                 * [622023] */
            slapi_ch_free((void **)&valuestodelete);
            retVal = LDAP_SUCCESS;
        } else {
            /* find the values to delete in present values,
                 * update the csns and add to the deleted values
                 */
            Slapi_Value **deletedvalues = NULL;
            retVal = valueset_remove_valuearray(&a->a_present_values, a, valuestodelete, 0 /* Do Not Ignore Errors */, &deletedvalues);
            if (retVal == LDAP_SUCCESS && deletedvalues != NULL) {
                /* Add each deleted value to the deleted set */
                valuearray_update_csn(deletedvalues, CSN_TYPE_VALUE_DELETED, csn);
                slapi_valueset_add_attr_valuearray_ext(a,
                                                       &a->a_deleted_values, deletedvalues, valuearray_count(deletedvalues), SLAPI_VALUE_FLAG_PASSIN, NULL);
                slapi_ch_free((void **)&deletedvalues);
                if (valueset_isempty(&a->a_present_values)) {
                    /* There are no present values, so move the
                         * attribute to the deleted attribute list. */
                    entry_present_attribute_to_deleted_attribute(e, a);
                }
            } else if (retVal != LDAP_SUCCESS) {
                /* Failed
                     * - Value not found
                     * - Operations error
                     */
                if (retVal == LDAP_OPERATIONS_ERROR) {
                    slapi_log_err(SLAPI_LOG_ERR, "entry_delete_present_values_wsi_multi_valued",
                                  "Possible existing duplicate value for attribute type %s found in entry %s\n",
                                  a->a_type, slapi_entry_get_dn_const(e));
                }
            }
            valuearray_free(&valuestodelete);
        }
    }
    return (retVal);
}

/*
 * Replace all the values of an attribute with a list of attribute values.
 *
 * Preserves LDAP Information Model constraints,
 * returning an LDAP result code.
 */
static int
entry_replace_present_values_wsi(Slapi_Entry *e, const char *type, struct berval **vals, const CSN *csn, int urp)
{
    /*
     * Remove all existing values.
     */
    entry_delete_present_values_wsi(e, type, NULL /* Delete all values */, csn, urp, LDAP_MOD_REPLACE, vals);

    /*
     * Add the new values. If there are no new values,
     * slapi_entry_add_values() returns LDAP_SUCCESS and so the
     * attribute remains deleted (which is the correct outcome).
     */
    return (entry_add_present_values_wsi(e, type, vals, csn, urp, 0));
}

/*
 * Applies the modification to the entry whilst
 * maintaining state information.
 */
int
entry_apply_mod_wsi(Slapi_Entry *e, const LDAPMod *mod, const CSN *csn, int urp)
{
    int retVal = LDAP_SUCCESS;
    int i;
    struct attrs_in_extension *aiep;

    switch (mod->mod_op & ~LDAP_MOD_BVALUES) {
    case LDAP_MOD_ADD:
        slapi_log_err(SLAPI_LOG_ARGS, "entry_apply_mod_wsi", "add: %s\n", mod->mod_type);
        retVal = entry_add_present_values_wsi(e, mod->mod_type, mod->mod_bvalues, csn, urp, 0);
        break;

    case LDAP_MOD_DELETE:
        slapi_log_err(SLAPI_LOG_ARGS, "entry_apply_mod_wsi", "delete: %s\n", mod->mod_type);
        retVal = entry_delete_present_values_wsi(e, mod->mod_type, mod->mod_bvalues, csn, urp, mod->mod_op, NULL);
        break;

    case LDAP_MOD_REPLACE:
        slapi_log_err(SLAPI_LOG_ARGS, "entry_apply_mod_wsi", "replace: %s\n", mod->mod_type);
        retVal = entry_replace_present_values_wsi(e, mod->mod_type, mod->mod_bvalues, csn, urp);
        break;
    }
    if (loglevel_is_set(LDAP_DEBUG_ARGS)) {
        for (i = 0;
             mod->mod_bvalues != NULL && mod->mod_bvalues[i] != NULL;
             i++) {
            if (0 == strcasecmp(PSEUDO_ATTR_UNHASHEDUSERPASSWORD, mod->mod_type))
                continue;
            slapi_log_err(SLAPI_LOG_ARGS, "entry_apply_mod_wsi", "%s: %s\n",
                          mod->mod_type, mod->mod_bvalues[i]->bv_val);
        }
        slapi_log_err(SLAPI_LOG_ARGS, "entry_apply_mod_wsi", "-\n");
    }

    /*
     * Values to be stored in the extension are also processed considering
     * the conflicts above.  The psuedo attributes are removed from the
     * entry and the values (present value only) are put in the extension.
     */
    for (aiep = attrs_in_extension; aiep && aiep->ext_type; aiep++) {
        if (0 == strcasecmp(mod->mod_type, aiep->ext_type)) {
            Slapi_Attr *a;

            /* remove the attribute from the attr list */
            a = attrlist_remove(&e->e_attrs, mod->mod_type);
            if (a && a->a_present_values.va) {
                /* a->a_present_values.va is consumed if successful. */
                int rc = slapi_pw_set_entry_ext(e, a->a_present_values.va,
                                                SLAPI_EXT_SET_REPLACE);
                if (LDAP_SUCCESS == rc) {
                    /* va is set to entry extension; just release the rest */
                    a->a_present_values.va = NULL;
                }
                slapi_attr_free(&a);
            } else {
                Slapi_Value **ext_vals = NULL;
                slapi_pw_get_entry_ext(e, &ext_vals);
                if (ext_vals) {
                    /* slapi_pw_set_entry_ext frees the stored extension */
                    slapi_pw_set_entry_ext(e, NULL, SLAPI_EXT_SET_REPLACE);
                    ext_vals = NULL;
                }
            }
        }
    }

    return retVal;
}

/*
 * Applies the set of modifications to the entry whilst
 * maintaining state information.
 */
int
entry_apply_mods_wsi(Slapi_Entry *e, Slapi_Mods *smods, const CSN *csn, int urp)
{
    int retVal = LDAP_SUCCESS;
    LDAPMod *mod;
    CSN localcsn;

    if (csn) {
        localcsn = *csn; /* make a copy */
    }

    slapi_log_err(SLAPI_LOG_TRACE, "entry_apply_mods_wsi", "==>\n");

    mod = slapi_mods_get_first_mod(smods);
    while (NULL != mod && retVal == LDAP_SUCCESS) {
        if (csn != NULL) {
            retVal = entry_apply_mod_wsi(e, mod, &localcsn, urp);
            /* use subsequence to guarantee absolute ordering of all of the
               mods in a set of mods, if this is a replicated operation,
               and the csn doesn't already have a subsequence
               if the csn already has a subsequence, assume it was generated
               on another replica in the correct order */
            if (csn_get_subseqnum(csn) == 0) {
                csn_increment_subsequence(&localcsn);
            }
        } else {
            retVal = entry_apply_mod(e, mod);
        }
        mod = slapi_mods_get_next_mod(smods);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "entry_apply_mods_wsi", "<== %d\n", retVal);

    return retVal;
}

/*
 * This code implements a computed attribute called 'nscpEntryWSI'.
 * By specifically asking for this attribute the client will receive
 * an LDIF dump of the entry with all its state information.
 *
 * JCM - Security... Only for the Directory Manager.
 */
static const char *nscpEntryWSI = "nscpEntryWSI";
/*
 */
static int
entry_compute_nscpentrywsi(computed_attr_context *c, char *type, Slapi_Entry *e, slapi_compute_output_t outputfn)
{
    int rc = 0;

    if (strcasecmp(type, nscpEntryWSI) == 0) {
        /* If not, we return it as zero */
        char *es;
        char *s;
        char *p;
        int len;
        Slapi_Attr our_attr;
        slapi_attr_init(&our_attr, nscpEntryWSI);
        our_attr.a_flags = SLAPI_ATTR_FLAG_OPATTR;
        es = slapi_entry2str_with_options(e, &len, SLAPI_DUMP_STATEINFO | SLAPI_DUMP_UNIQUEID | SLAPI_DUMP_NOWRAP);
        s = es;
        p = ldif_getline(&s);
        while (p != NULL) {
            Slapi_Value *v;
            char *t, *d;
            /* Strip out the Continuation Markers (JCM - I think that NOWRAP means we don't need to do this any more)*/
            for (t = p, d = p; *t; t++) {
                if (*t != 0x01)
                    *d++ = *t;
            }
            *d = '\0';
            v = slapi_value_new_string(p);
            slapi_attr_add_value(&our_attr, v);
            slapi_value_free(&v);
            p = ldif_getline(&s);
        }
        slapi_ch_free((void **)&es);
        rc = (*outputfn)(c, &our_attr, e);
        attr_done(&our_attr);
        return (rc);
    }

    return -1; /* I see no ships */
}


int
entry_computed_attr_init()
{
    slapi_compute_add_evaluator_ext(entry_compute_nscpentrywsi, 1 /* root only */);
    return 0;
}

static void
purge_attribute_state_multi_valued(const Slapi_Attr *a __attribute__((unused)), Slapi_Value *v)
{
    const CSN *vdcsn = value_get_csn(v, CSN_TYPE_VALUE_DELETED);
    const CSN *vucsn = value_get_csn(v, CSN_TYPE_VALUE_UPDATED);
    if (vdcsn && csn_compare(vdcsn, vucsn) < 0) {
        value_remove_csn(v, CSN_TYPE_VALUE_DELETED);
    }
}

/*
 * utility function for value_distinguished_at_csn...
 */
static const CSN *
vdac_sniff_value(Slapi_ValueSet *vs, const Slapi_Value *v, const CSN *csn, const CSN *most_recent_mdcsn)
{
    const CSN *mdcsn = value_get_csn(v, CSN_TYPE_VALUE_DISTINGUISHED);
    if (mdcsn != NULL) {
        /* This value was/is distinguished... */
        if (csn_compare(csn, most_recent_mdcsn) < 0) {
            /* ...and was distinguished before the point in time we're interested in... */
            int r = csn_compare(mdcsn, most_recent_mdcsn);
            if (r > 0) {
                /* ...and is the most recent MDCSN we've seen thus far. */
                slapi_valueset_done(vs);
                slapi_valueset_add_value(vs, v);
                most_recent_mdcsn = mdcsn;
            } else if (r == 0) {
                /* ...and is as recent as the last most recent MDCSN we've seen thus far. */
                /* Must have been a multi-valued RDN */
                slapi_valueset_add_value(vs, v);
            }
        }
    }
    return most_recent_mdcsn;
}

/*
 * utility function for value_distinguished_at_csn...
 */
static const CSN *
vdac_sniff_attribute(Slapi_ValueSet *vs, Slapi_Attr *a, const CSN *csn, const CSN *most_recent_mdcsn)
{
    Slapi_Value *v;
    int i = slapi_attr_first_value(a, &v);
    while (i != -1) {
        most_recent_mdcsn = vdac_sniff_value(vs, v, csn, most_recent_mdcsn);
        i = slapi_attr_next_value(a, i, &v);
    }
    i = attr_first_deleted_value(a, &v);
    while (i != -1) {
        most_recent_mdcsn = vdac_sniff_value(vs, v, csn, most_recent_mdcsn);
        i = attr_next_deleted_value(a, i, &v);
    }
    return most_recent_mdcsn;
}

/*
 * utility function for value_distinguished_at_csn...
 *
 * Return the set of values that made up the RDN at or before the csn point.
 */
static const CSN *
distinguished_values_at_csn(const Slapi_Entry *e, const CSN *csn, Slapi_ValueSet *vs)
{
    const CSN *most_recent_mdcsn = NULL;
    Slapi_Attr *a;
    int i = slapi_entry_first_attr(e, &a);
    while (i != -1) {
        most_recent_mdcsn = vdac_sniff_attribute(vs, a, csn, most_recent_mdcsn);
        i = slapi_entry_next_attr(e, a, &a);
    }
    i = entry_first_deleted_attribute(e, &a);
    while (i != -1) {
        most_recent_mdcsn = vdac_sniff_attribute(vs, a, csn, most_recent_mdcsn);
        i = entry_next_deleted_attribute(e, &a);
    }
    return most_recent_mdcsn;
}

/*
 * Work out if the value was distinguished at time csn.
 */
static int
value_distinguished_at_csn(const Slapi_Entry *e, const Slapi_Attr *original_attr, Slapi_Value *original_value, const CSN *csn)
{
    int r = 0;
    const CSN *mdcsn = value_get_csn(original_value, CSN_TYPE_VALUE_DISTINGUISHED);
    if (mdcsn != NULL) {
        /*
         * Oh bugger. This means that we have to work out what the RDN components
         * were at this point in time. This is non-trivial since we must walk
         * through all the present and deleted attributes and their present and
         * deleted values. Slow :-(
         */
        Slapi_ValueSet *vs = slapi_valueset_new();
        const CSN *most_recent_mdcsn = distinguished_values_at_csn(e, csn, vs);
        /*
         * We now know what the RDN components were at the point in time we're interested in.
         * And the question we need to answer is :-
         * 'Was the provided value one of those RDN components?'
         */
        if (most_recent_mdcsn != NULL) {
            Slapi_Value *v;
            int i = slapi_valueset_first_value(vs, &v);
            while (i != -1) {
                if (slapi_value_compare(original_attr, original_value, v) == 0) {
                    /* This value was distinguished at the time in question. */
                    r = 1;
                    i = -1;
                } else {
                    i = slapi_valueset_next_value(vs, i, &v);
                }
            }
        }
        slapi_valueset_free(vs);
    } else {
        /* This value has never been distinguished */
        r = 0;
    }
    return r;
}

/* This call ensures that the value does not contain a deletion_csn which is before the presence_csn or distinguished_csn of the value. */
static void
resolve_attribute_state_deleted_to_present(Slapi_Entry *e, Slapi_Attr *a, Slapi_Value **valuestoupdate)
{
    const CSN *vdcsn;
    const CSN *vucsn;
    const CSN *deletedcsn;
    const CSN *adcsn = attr_get_deletion_csn(a);
    int i;
    if (valuestoupdate != NULL && valuestoupdate[0] != NULL) {
        for (i = 0; valuestoupdate[i] != NULL; ++i) {
            purge_attribute_state_multi_valued(a, valuestoupdate[i]);
            vdcsn = value_get_csn(valuestoupdate[i], CSN_TYPE_VALUE_DELETED);
            vucsn = value_get_csn(valuestoupdate[i], CSN_TYPE_VALUE_UPDATED);
            deletedcsn = csn_max(vdcsn, adcsn);
            if ((csn_compare(vucsn, deletedcsn) >= 0) ||
                value_distinguished_at_csn(e, a, valuestoupdate[i], deletedcsn)) {
                entry_deleted_value_to_present_value(a, valuestoupdate[i]);
            }
            valuestoupdate[i]->v_csnset = NULL;
        }
    }
}

static void
resolve_attribute_state_to_present_or_deleted(Slapi_Entry *e, Slapi_Attr *a, Slapi_Value **valuestoupdate, int attribute_state)
{
    const CSN *vdcsn;
    const CSN *vucsn;
    const CSN *deletedcsn;
    const CSN *adcsn = attr_get_deletion_csn(a);
    int i;
    if (valuestoupdate != NULL && valuestoupdate[0] != NULL) {
        for (i = 0; valuestoupdate[i] != NULL; ++i) {
            purge_attribute_state_multi_valued(a, valuestoupdate[i]);
            vdcsn = value_get_csn(valuestoupdate[i], CSN_TYPE_VALUE_DELETED);
            vucsn = value_get_csn(valuestoupdate[i], CSN_TYPE_VALUE_UPDATED);
            deletedcsn = csn_max(vdcsn, adcsn);
            if ((csn_compare(vucsn, deletedcsn) >= 0) ||
                value_distinguished_at_csn(e, a, valuestoupdate[i], deletedcsn)) {
                slapi_valueset_add_attr_value_ext(a, &a->a_present_values, valuestoupdate[i], SLAPI_VALUE_FLAG_PASSIN);
            } else {
                slapi_valueset_add_attr_value_ext(a, &a->a_deleted_values, valuestoupdate[i], SLAPI_VALUE_FLAG_PASSIN);
            }
        }
    }
    if (valueset_isempty(&a->a_present_values)) {
        if (attribute_state == ATTRIBUTE_PRESENT) {
            entry_present_attribute_to_deleted_attribute(e, a);
        }
    } else {
        if (attribute_state == ATTRIBUTE_DELETED) {
            entry_deleted_attribute_to_present_attribute(e, a);
        }
    }
}

static void
resolve_attribute_state_present_to_deleted(Slapi_Entry *e, Slapi_Attr *a, Slapi_Value **valuestoupdate)
{
    const CSN *vdcsn;
    const CSN *vucsn;
    const CSN *deletedcsn;
    const CSN *adcsn = attr_get_deletion_csn(a);
    int i;
    if (valuestoupdate != NULL && valuestoupdate[0] != NULL) {
        for (i = 0; valuestoupdate[i] != NULL; ++i) {
            /* This call ensures that the value does not contain a deletion_csn
             * which is before the presence_csn or distinguished_csn of the value.
             */
            purge_attribute_state_multi_valued(a, valuestoupdate[i]);
            vdcsn = value_get_csn(valuestoupdate[i], CSN_TYPE_VALUE_DELETED);
            vucsn = value_get_csn(valuestoupdate[i], CSN_TYPE_VALUE_UPDATED);
            deletedcsn = csn_max(vdcsn, adcsn);
            if (csn_compare(vucsn, deletedcsn) < 0) {
                if (!value_distinguished_at_csn(e, a, valuestoupdate[i], deletedcsn)) {
                    entry_present_value_to_deleted_value(a, valuestoupdate[i]);
                }
            }
            csnset_free(&valuestoupdate[i]->v_csnset);
        }
    }
}

/* Retrieve from the deleted values the one that
 * was the most recently deleted. Based on its vdcsn
 */
static Slapi_Value *
attr_most_recent_deleted_value(Slapi_Attr *a)
{
    Slapi_Value *v, *most_recent_v;
    int i;
    CSN *vdcsn, *most_recent_vdcsn;

    vdcsn = NULL;
    most_recent_vdcsn = NULL;
    i = attr_first_deleted_value(a, &v);
    most_recent_v = v;

    while (i != -1) {
        vdcsn = (CSN *)value_get_csn(v, CSN_TYPE_VALUE_DELETED);

        if (csn_compare((const CSN *)most_recent_vdcsn, (const CSN *)vdcsn) < 0) {
            most_recent_v = v;
            most_recent_vdcsn = vdcsn;
        }
        i = attr_next_deleted_value(a, i, &v);
    }
    return most_recent_v;
}

/* This routine applies for single valued attribute.
 * The attribute has two current values, it keeps the most recent one
 * and zap the oldest
 */
static void
resolve_single_valued_two_values(Slapi_Entry *e, Slapi_Attr *a, int attribute_state, Slapi_Value *current_value, Slapi_Value *second_current_value)
{

    const CSN *current_value_vucsn;
    const CSN *second_current_value_vucsn;
    Slapi_Value *value_to_zap;
    
    current_value_vucsn = value_get_csn(current_value, CSN_TYPE_VALUE_UPDATED);
    second_current_value_vucsn = value_get_csn(second_current_value, CSN_TYPE_VALUE_UPDATED);
    
    /* First determine which present value will be zapped */
    if (csn_compare(second_current_value_vucsn, current_value_vucsn) < 0) {
        /*
         * The second value is older but was distinguished at the time the current value was added
         * then the second value should become current
         */
        if (value_distinguished_at_csn(e, a, second_current_value, current_value_vucsn)) {
            value_to_zap = current_value;
        } else {
            /* The second value being not distinguished, zap it as it is a single valued attribute */
            value_to_zap = second_current_value;
        }
        
    } else {
        /* Here the current_value is older than the second_current_value */
        if (value_distinguished_at_csn(e, a, current_value, second_current_value_vucsn)) {
            /* current_value was distinguished at the time the second value was added
             * then the current_value should become the current */
            value_to_zap = second_current_value;
        } else {
            value_to_zap = current_value;
        }
    }
    entry_present_value_to_zapped_value(a, value_to_zap);
    


}

/* This routine applies for single valued attribute.
 * It checks if the deleted value is more recent than
 * the present one. If it is, it resurect the deleted value
 *
 * This function leaves untouch the adcsn
 */
static void
resolve_single_valued_check_restore_deleted_value(Slapi_Entry *e, Slapi_Attr *a)
{
    Slapi_Value *deleted_value = NULL;
    Slapi_Value *current_value = NULL;

    /* Retrieve the deleted and current value */
    deleted_value = attr_most_recent_deleted_value(a);
    if (deleted_value == NULL) {
        return;
    }
    slapi_attr_first_value(a, &current_value);

    if (current_value == NULL) {
        /* An attribute needs a present value */
        entry_deleted_value_to_present_value(a, deleted_value);
    } else {
        const CSN *current_value_vucsn;
        const CSN *deleted_value_vucsn;
        const CSN *deleted_value_vdcsn;

        deleted_value_vucsn = value_get_csn(deleted_value, CSN_TYPE_VALUE_UPDATED);
        deleted_value_vdcsn = value_get_csn(deleted_value, CSN_TYPE_VALUE_DELETED);
        current_value_vucsn = value_get_csn(current_value, CSN_TYPE_VALUE_UPDATED);
        if (deleted_value_vucsn &&
                !value_distinguished_at_csn(e, a, current_value, deleted_value_vucsn) &&
                (csn_compare((const CSN *)current_value_vucsn, deleted_value_vucsn) < 0) &&
                (csn_compare((const CSN *)deleted_value_vdcsn, current_value_vucsn) < 0)) {
            /* the condition to resurrect the deleted value is 
             *  - it is more recent than the current value
             *  - its value was deleted before the current value
             *  - the current value is not distinguished
             */
            entry_present_value_to_zapped_value(a, current_value);
            entry_deleted_value_to_present_value(a, deleted_value);
        }
    }
}
/* This function deals with single valued attribute
 * It zap the current value if the adcsn is more recent and the value is not distinguished
 */
static void
resolve_single_valued_zap_current(Slapi_Entry *e, Slapi_Attr *a)
{
    Slapi_Value *current_value = NULL;
    const CSN *current_value_vucsn;
    const CSN *adcsn;

    /* check if the current value should be deleted because 
     * older than adcsn and not distinguished
     */
    slapi_attr_first_value(a, &current_value);
    current_value_vucsn = value_get_csn(current_value, CSN_TYPE_VALUE_UPDATED);
    adcsn = attr_get_deletion_csn(a);
    if (current_value != NULL) {
        if (csn_compare(adcsn, (const CSN *) current_value_vucsn) > 0) {
            /* the attribute was deleted after the value was last updated */
            if (!value_distinguished_at_csn(e, a, current_value, (const CSN *) current_value_vucsn)) {
                entry_present_value_to_zapped_value(a, current_value);
            }
        }
    }
}
/* This function deals with single valued attribute
 * It reset the adcsn if
 * - there is no deleted value and current value is more recent than the adcsn
 * - there is a deleted value and it is more recent than the adcsn
 */
static void
resolve_single_valued_set_adcsn(Slapi_Attr *a)
{
    Slapi_Value *deleted_value = NULL;
    Slapi_Value *current_value = NULL;
    const CSN *current_value_vucsn;
    const CSN *deleted_value_vucsn;
    const CSN *adcsn;
    
    slapi_attr_first_value(a, &current_value);
    current_value_vucsn = value_get_csn(current_value, CSN_TYPE_VALUE_UPDATED);
    deleted_value = attr_most_recent_deleted_value(a);
    deleted_value_vucsn = value_get_csn(deleted_value, CSN_TYPE_VALUE_UPDATED);
    adcsn = attr_get_deletion_csn(a);
    if ((deleted_value != NULL && (csn_compare(adcsn, deleted_value_vucsn) < 0)) ||
        (deleted_value == NULL && (csn_compare(adcsn, current_value_vucsn) < 0))) {
        attr_set_deletion_csn(a, NULL);
    }
}
/* This function deals with single valued attribute
 * It checks if the deleted value worth to be kept
 * 
 * deleted value is zapped if
 * - it is the result of MOD_REPL that is older than current value
 * - It is the result of MOD_DEL_<value> that is belong to the same operation that set the current value
 */
static void
resolve_single_valued_zap_deleted(Slapi_Attr *a)
{
    Slapi_Value *deleted_value = NULL;
    Slapi_Value *current_value = NULL;
    const CSN *current_value_vucsn;
    const CSN *deleted_value_vucsn;
    const CSN *deleted_value_vdcsn;
    const CSN *deleted_value_csn;
    PRBool deleted_on_mod_del = PR_FALSE; /* flag if a value was deleted specifically */

    /* Now determine if the deleted value worth to be kept */
    slapi_attr_first_value(a, &current_value);
    current_value_vucsn = value_get_csn(current_value, CSN_TYPE_VALUE_UPDATED);

    deleted_value = attr_most_recent_deleted_value(a);
    deleted_value_vucsn = value_get_csn(deleted_value, CSN_TYPE_VALUE_UPDATED);
    deleted_value_vdcsn = value_get_csn(deleted_value, CSN_TYPE_VALUE_DELETED);

    /* get the appropriate csn to take into consideration: either from MOD_REPL or from MOD_DEL_specific */
    if (csn_compare(deleted_value_vdcsn, deleted_value_vucsn) <= 0) {
        deleted_value_csn = deleted_value_vucsn;
    } else {
        deleted_value_csn = deleted_value_vdcsn;
        if (0 == csn_compare_ext(current_value_vucsn, deleted_value_vdcsn, CSN_COMPARE_SKIP_SUBSEQ)) {
            /* the deleted value was specifically delete in the same operation that set the current value */
            deleted_on_mod_del = PR_TRUE;
        }
    }
    if ((csn_compare(deleted_value_csn, current_value_vucsn) < 0) || deleted_on_mod_del) {
        entry_deleted_value_to_zapped_value(a, deleted_value);
    }
}

/* This function deals with single valued attribute
 * It does a set of cleanup in the current/deleted values in order
 * to conform the schema, take care of distinguished values and only preserve the
 * values that worth to be kept.
 */
static void
resolve_attribute_state_single_valued(Slapi_Entry *e, Slapi_Attr *a, int attribute_state)
{
    int32_t nbval, i;
    Slapi_Value *current_value = NULL;

    /* retrieve the current value(s) */
    slapi_attr_get_numvalues(a, &nbval);
    i = slapi_attr_first_value(a, &current_value);

    /* If there are several values, first determine which value will be the current (present) one */
    if (nbval > 1) {
        /* There are several values for a single valued attribute, keep the most recent one */
        if (i == -1) {
            slapi_log_err(SLAPI_LOG_ERR, "resolve_attribute_state_single_valued", "Unexpected state of %s that contains more than one value but can not read the second\n", a->a_type);
        } else {
            Slapi_Value *second_current_value = NULL;

            slapi_attr_next_value(a, i, &second_current_value);
            resolve_single_valued_two_values(e, a, attribute_state, current_value, second_current_value);                
        }
    }
    /* There is only one current value (present value) */

    /* Now determine if the deleted value needs to replace the current value */
    resolve_single_valued_check_restore_deleted_value(e, a);

    /* Now determine if the deleted value worth to be kept (vs. current value)  */
    resolve_single_valued_zap_deleted(a);

    /* Now determine if the current value worth to be kept (vs. adcsn) */
    resolve_single_valued_zap_current(e, a);

    /* Now set the adcsn */
    resolve_single_valued_set_adcsn(a);

    /* set the attribute in the correct list in the entry: present or deleted  */
    slapi_attr_first_value(a, &current_value);
    if (current_value == NULL) {
        if (attribute_state == ATTRIBUTE_PRESENT) {
            entry_present_attribute_to_deleted_attribute(e, a);
        }
    } else {
        if (attribute_state == ATTRIBUTE_DELETED) {
            entry_deleted_attribute_to_present_attribute(e, a);
        }
    }
}

const CSN *
entry_get_deletion_csn(Slapi_Entry *e)
{
    const CSN *deletion_csn = NULL;

    PR_ASSERT(NULL != e);
    if (NULL != e) {
        Slapi_Attr *oc_attr = NULL;
        if (entry_attr_find_wsi(e, SLAPI_ATTR_OBJECTCLASS, &oc_attr) == ATTRIBUTE_PRESENT) {
            Slapi_Value *tombstone_value = NULL;
            struct berval v;
            v.bv_val = SLAPI_ATTR_VALUE_TOMBSTONE;
            v.bv_len = strlen(SLAPI_ATTR_VALUE_TOMBSTONE);
            if (attr_value_find_wsi(oc_attr, &v, &tombstone_value) == VALUE_PRESENT) {
                deletion_csn = value_get_csn(tombstone_value, CSN_TYPE_VALUE_UPDATED);
            }
        }
    }
    return deletion_csn;
}
