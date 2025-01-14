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

/* matchrule.c */


#include "back-ldbm.h"

/* NPCTE fix for bug # 394184, SD, 20 Jul 00 */
/* replace the hard coded return value by the appropriate LDAP error code */
/*
 * Returns:  0 -- OK                now is: LDAP_SUCCESS (fix for bug #394184)
 *            -1 -- protocol error    now is: LDAP_PROTOCOL_ERROR
 *            -3 -- operation error   now is: LDAP_OPERATIONS_ERROR
 */
int
create_matchrule_indexer(Slapi_PBlock **pb, char *matchrule, char *type)
{
    IFP mrINDEX = NULL;
    int return_value = LDAP_SUCCESS;
    unsigned int sort_indicator = SLAPI_PLUGIN_MR_USAGE_SORT;

    if (pb == NULL) {
        return LDAP_OPERATIONS_ERROR;
    }

    if (*pb == NULL) {
        *pb = slapi_pblock_new();
    }
    if (*pb == NULL) {
        /* Memory allocation faliure */
        /* Operations error to the calling routine */
        return LDAP_OPERATIONS_ERROR;
    }

    /* If these fail, it's an operations error */
    return_value |= slapi_pblock_set(*pb, SLAPI_PLUGIN_MR_OID, matchrule);
    return_value |= slapi_pblock_set(*pb, SLAPI_PLUGIN_MR_TYPE, type);
    return_value |= slapi_pblock_set(*pb, SLAPI_PLUGIN_MR_USAGE, (void *)&sort_indicator);
    if (0 != return_value) {
        return LDAP_OPERATIONS_ERROR;
    }

    /* If this fails, could be operations error, or that OID is not supported */
    return_value = slapi_mr_indexer_create(*pb);
    if (0 != return_value) {
        return LDAP_PROTOCOL_ERROR;
    }

    /* If these fail, ops error */
    return_value = slapi_pblock_get(*pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX);

    if ((0 != return_value) || (mrINDEX == NULL)) {
        /* doesn't have an old MR_INDEX_FN - look for MR_INDEX_SV_FN */
        return_value = slapi_pblock_get(*pb, SLAPI_PLUGIN_MR_INDEX_SV_FN, &mrINDEX);

        if ((0 != return_value) || (mrINDEX == NULL)) {
            return LDAP_OPERATIONS_ERROR;
        } else {
            return LDAP_SUCCESS;
        }
    } else {
        return LDAP_SUCCESS;
    }
}
/* End NPCTE fix for bug # 394184 */

int
destroy_matchrule_indexer(Slapi_PBlock *pb)
{
    Slapi_Value **keys = NULL;
    IFP mrDESTROY = NULL;
    if (!slapi_pblock_get(pb, SLAPI_PLUGIN_DESTROY_FN, &mrDESTROY)) {
        if (mrDESTROY != NULL) {
            mrDESTROY(pb);
        }
    }
    /* matching rule indexers which handle Slapi_Value**
       directly will own the keys, free them, and set
       SLAPI_PLUGIN_MR_KEYS to NULL in the destroy
       function - the old style matching rule indexers
       which only deal with struct berval ** will not
       free the Slapi_Value** wrappers so we have to free
       them here */
    slapi_pblock_get(pb, SLAPI_PLUGIN_MR_KEYS, &keys);
    if (keys) {
        valuearray_free(&keys);
        slapi_pblock_set(pb, SLAPI_PLUGIN_MR_KEYS, NULL);
    }
    return 0;
}


/*
 * This routine returns pointer to memory which is owned by the plugin, so don't
 * free it. Gets freed by the next call to this routine, or when the indexer
 * is destroyed
 */
int
matchrule_values_to_keys(Slapi_PBlock *pb, Slapi_Value **input_values, struct berval ***output_values)
{
    IFP mrINDEX = NULL;

    slapi_pblock_get(pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX);
    slapi_pblock_set(pb, SLAPI_PLUGIN_MR_VALUES, input_values);
    if (mrINDEX) {
        mrINDEX(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_MR_KEYS, output_values);
        return LDAP_SUCCESS;
    } else {
        return LDAP_OPERATIONS_ERROR;
    }
}

/*
 * This routine returns pointer to memory which is owned by the plugin, so don't
 * free it. Gets freed by the next call to this routine, or when the indexer
 * is destroyed
 */
int
matchrule_values_to_keys_sv(Slapi_PBlock *pb, Slapi_Value **input_values, Slapi_Value ***output_values)
{
    IFP mrINDEX = NULL;

    slapi_pblock_get(pb, SLAPI_PLUGIN_MR_INDEX_SV_FN, &mrINDEX);
    if (NULL == mrINDEX) { /* old school - does not have SV function */
        int rc;
        struct berval **bvo = NULL;
        rc = matchrule_values_to_keys(pb, input_values, &bvo);
        /* note - the indexer owns bvo and will free it when destroyed */
        valuearray_init_bervalarray(bvo, output_values);
        /* store output values in SV form - caller expects SLAPI_PLUGIN_MR_KEYS is Slapi_Value** */
        slapi_pblock_set(pb, SLAPI_PLUGIN_MR_KEYS, *output_values);
        return rc;
    }

    slapi_pblock_set(pb, SLAPI_PLUGIN_MR_VALUES, input_values);
    mrINDEX(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_MR_KEYS, output_values);
    return 0;
}
