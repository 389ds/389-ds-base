/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * AD rewriters
 *
 * This library contains filter rewriters and computed attribute rewriters.
 */

#include "slap.h"

static char *rewriter_name = "filter rewriter adfilter";

#define OBJECTCATEGORY "objectCategory"


/* Rewrite ObjectCategory as described in [1]
 * [1] https://social.technet.microsoft.com/wiki/contents/articles/5392.active-directory-ldap-syntax-filters.aspx#Filter_on_objectCategory_and_objectClass
 * static char *objectcategory_shortcuts[] = {"person", "computer", "user", "contact", "group", "organizationalPerson", NULL};
 */

typedef struct {
    char *attrtype;  /* type = objectCategory */
    char *format;
} objectCategory_arg_t;

static int
substitute_shortcut(Slapi_Filter *f, void *arg)
{
    objectCategory_arg_t *substitute_arg = (objectCategory_arg_t *) arg;
    char *filter_type;
    struct berval *bval;
    char *newval;
    char logbuf[1024] = {0};

    if ((substitute_arg == NULL) ||
        (substitute_arg->attrtype == NULL) ||
        (substitute_arg->format == NULL)) {
        return SLAPI_FILTER_SCAN_STOP;
    }

    /* If (objectCategory=<shortcut>) --> (objectCategory=cn=<shortcut>,cn=Schema,cn=Configuration,<suffix>) */
    if ((slapi_filter_get_ava(f, &filter_type, &bval) == 0) &&
        (slapi_filter_get_choice(f) == LDAP_FILTER_EQUALITY) &&
        (bval->bv_val) &&
        (strcasecmp(filter_type, substitute_arg->attrtype) == 0)) {
        newval = slapi_ch_smprintf(substitute_arg->format, bval->bv_val);
        slapi_log_err(SLAPI_LOG_FILTER, rewriter_name, "objectcategory_check_filter - 1 component %s : %s -> %s\n",
                      slapi_filter_to_string(f, logbuf, sizeof (logbuf)),
                      bval->bv_val,
                      newval);
        slapi_ch_free_string(&bval->bv_val);
        bval->bv_val = newval;
        bval->bv_len = strlen(newval);
    }

    /* Return continue because we should
     * substitute 'from' in all filter components
     */
    return SLAPI_FILTER_SCAN_CONTINUE;
}

/*
 * This is a filter rewriter function for 'ObjectCagerory' attribute
 *
 * Its rewriter config entry looks like
 * dn: cn=adfilter,cn=rewriters,cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: adfilter
 * nsslapd-libpath: librewriters
 * nsslapd-filterrewriter: adfilter_rewrite_objectCategory
 */
int32_t
adfilter_rewrite_objectCategory(Slapi_PBlock *pb)
{
    Slapi_Filter *clientFilter = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Backend *be = NULL;
    const char *be_suffix = NULL;
    int error_code = 0;
    int rc;
    char *format;
    char *strFilter;
    objectCategory_arg_t arg;

    slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &clientFilter);
    slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &strFilter);
    slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &sdn);

    if (strFilter && (strcasestr(strFilter, OBJECTCATEGORY) == NULL)) {
        /* accelerator: returns if filter string does not contain objectcategory */
        return SEARCH_REWRITE_CALLBACK_CONTINUE;
    }
    if ((be = slapi_be_select(sdn)) != NULL) {
        be_suffix = slapi_sdn_get_dn(slapi_be_getsuffix(be, 0));
    }

    /* prepare the argument of filter apply callback: a format and
     * the attribute type that trigger the rewrite
     */
    format = slapi_ch_smprintf("cn=%s,cn=Schema,cn=Configuration,%s", (char *) "%s", (char *) be_suffix);
    arg.attrtype = OBJECTCATEGORY;
    arg.format = format;

    /* Now apply substitute_shortcut on each filter component */
    rc = slapi_filter_apply(clientFilter, substitute_shortcut, &arg, &error_code);
    slapi_ch_free_string(&format);
    if (rc == SLAPI_FILTER_SCAN_NOMORE) {
        return SEARCH_REWRITE_CALLBACK_CONTINUE; /* Let's others rewriter play */
    } else {
        slapi_log_err(SLAPI_LOG_ERR,
                      "adfilter_rewrite_objectCategory", "Could not update the search filter - error %d (%d)\n",
                      rc, error_code);
        return SEARCH_REWRITE_CALLBACK_ERROR; /* operation error */
    }
}