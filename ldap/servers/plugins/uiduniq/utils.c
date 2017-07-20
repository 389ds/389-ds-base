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

/***********************************************************************
** NAME
**  utils.c
**
** DESCRIPTION
**
**
** AUTHOR
**   <rweltman@netscape.com>
**
***********************************************************************/


/***********************************************************************
** Includes
***********************************************************************/

#include "plugin-utils.h"
#include "nspr.h"

static char *plugin_name = "utils";

/* ------------------------------------------------------------ */
/*
 * op_error - Record (and report) an operational error.
 */
int
op_error(int internal_error)
{
    slapi_log_err(SLAPI_LOG_PLUGIN, plugin_name,
                  "Internal error: %d\n", internal_error);

    return LDAP_OPERATIONS_ERROR;
}

/* ------------------------------------------------------------ */
/*
 * readPblockAndEntry - search for and read an entry
 * Return:
 *   A pblock containing the entry, or NULL
 */
Slapi_PBlock *
readPblockAndEntry(Slapi_DN *baseDN, const char *filter, char *attrs[])
{
    Slapi_PBlock *spb = NULL;

    BEGIN
    int sres;

    /* Perform the search - the new pblock needs to be freed */
    spb = slapi_search_internal(slapi_sdn_get_dn(baseDN), LDAP_SCOPE_BASE,
                                (char *)filter, NULL, attrs, 0);
    if (!spb) {
        op_error(20);
        break;
    }

    if (slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_RESULT, &sres)) {
        op_error(21);
        break;
    } else if (sres) {
        op_error(22);
        break;
    }
    END

        return spb;
}

/* ------------------------------------------------------------ */
/*
 * hasObjectClass - read an entry and check if it has a
 *   particular object class value
 * Return:
 *   1 - the entry contains the object class value
 *   0 - the entry doesn't contain the object class value
 */
int
entryHasObjectClass(Slapi_PBlock *pb __attribute__((unused)), Slapi_Entry *e, const char *objectClass)
{
    Slapi_Attr *attr;
    Slapi_Value *v;
    const struct berval *bv;
    int vhint;

    if (slapi_entry_attr_find(e, "objectclass", &attr)) {
        return 0; /* no objectclass values! */
    }

    /*
     * Check each of the object class values in turn.
     */
    for (vhint = slapi_attr_first_value(attr, &v);
         vhint != -1;
         vhint = slapi_attr_next_value(attr, vhint, &v)) {
        bv = slapi_value_get_berval(v);
        if (NULL != bv && NULL != bv->bv_val &&
            !strcasecmp(bv->bv_val, objectClass)) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ */
/*
 * dnHasObjectClass - read an entry if it has a particular object class
 * Return:
 *   A pblock containing the entry, or NULL
 */
Slapi_PBlock *
dnHasObjectClass(Slapi_DN *baseDN, const char *objectClass)
{
    char *filter = NULL;
    Slapi_PBlock *spb = NULL;

    BEGIN
    Slapi_Entry **entries;
    char *attrs[2];

    /* Perform the search - the new pblock needs to be freed */
    attrs[0] = "objectclass";
    attrs[1] = NULL;
    filter = PR_smprintf("objectclass=%s", objectClass);
    if (!(spb = readPblockAndEntry(baseDN, filter, attrs))) {
        break;
    }

    if (slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries)) {
        op_error(23);
        break;
    }
    /*
         * Can only be one entry returned on a base search; just check
         * the first one
         */
    if (!*entries) {
        /* Clean up */
        slapi_free_search_results_internal(spb);
        slapi_pblock_destroy(spb);
        spb = NULL;
    }
    END

        if (filter)
    {
        PR_smprintf_free(filter);
    }
    return spb;
}

/* ------------------------------------------------------------ */
/*
 * dnHasAttribute - read an entry if it has a particular attribute
 * Return:
 *   The entry, or NULL
 */
Slapi_PBlock *
dnHasAttribute(const char *baseDN, const char *attrName)
{
    Slapi_PBlock *spb = NULL;
    char *filter = NULL;

    BEGIN
    int sres;
    Slapi_Entry **entries;
    char *attrs[2];

    /* Perform the search - the new pblock needs to be freed */
    attrs[0] = (char *)attrName;
    attrs[1] = NULL;
    filter = PR_smprintf("%s=*", attrName);
    spb = slapi_search_internal((char *)baseDN, LDAP_SCOPE_BASE,
                                filter, NULL, attrs, 0);
    if (!spb) {
        op_error(20);
        break;
    }

    if (slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_RESULT, &sres)) {
        op_error(21);
        break;
    } else if (sres) {
        op_error(22);
        break;
    }

    if (slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries)) {
        op_error(23);
        break;
    }
    /*
         * Can only be one entry returned on a base search; just check
         * the first one
         */
    if (!*entries) {
        /* Clean up */
        slapi_free_search_results_internal(spb);
        slapi_pblock_destroy(spb);
        spb = NULL;
    }
    END

        if (filter)
    {
        PR_smprintf_free(filter);
    }
    return spb;
}
