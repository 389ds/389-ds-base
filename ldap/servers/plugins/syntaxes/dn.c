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

/* dn.c - dn syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int dn_filter_ava(Slapi_PBlock *pb, struct berval *bvfilter, Slapi_Value **bvals, int ftype, Slapi_Value **retVal);
static int dn_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals);
static int dn_values2keys(Slapi_PBlock *pb, Slapi_Value **vals, Slapi_Value ***ivals, int ftype);
static int dn_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype);
static int dn_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals);
static int dn_validate(struct berval *val);
static void dn_normalize(
    Slapi_PBlock *pb,
    char *s,
    int trim_spaces,
    char **alt);

/* the first name is the official one from RFC 2252 */
static char *names[] = {"DN", DN_SYNTAX_OID, 0};

static Slapi_PluginDesc pdesc = {"dn-syntax", VENDOR,
                                 DS_PACKAGE_VERSION, "distinguished name attribute syntax plugin"};

static const char *distinguishedNameMatch_names[] = {"distinguishedNameMatch", "2.5.13.1", NULL};

static struct mr_plugin_def mr_plugin_table[] = {
    {
        {
            "2.5.13.1",
            NULL,
            "distinguishedNameMatch",
            "The distinguishedNameMatch rule compares an assertion value of the DN "
            "syntax to an attribute value of a syntax (e.g., the DN syntax) whose "
            "corresponding ASN.1 type is DistinguishedName. "
            "The rule evaluates to TRUE if and only if the attribute value and the "
            "assertion value have the same number of relative distinguished names "
            "and corresponding relative distinguished names (by position) are the "
            "same.  A relative distinguished name (RDN) of the assertion value is "
            "the same as an RDN of the attribute value if and only if they have "
            "the same number of attribute value assertions and each attribute "
            "value assertion (AVA) of the first RDN is the same as the AVA of the "
            "second RDN with the same attribute type.  The order of the AVAs is "
            "not significant.  Also note that a particular attribute type may "
            "appear in at most one AVA in an RDN.  Two AVAs with the same "
            "attribute type are the same if their values are equal according to "
            "the equality matching rule of the attribute type.  If one or more of "
            "the AVA comparisons evaluate to Undefined and the remaining AVA "
            "comparisons return TRUE then the distinguishedNameMatch rule "
            "evaluates to Undefined.",
            DN_SYNTAX_OID,
            0,
            NULL /* dn only for now */
        },       /* matching rule desc */
        {
            "distinguishedNameMatch-mr",
            VENDOR,
            DS_PACKAGE_VERSION,
            "distinguishedNameMatch matching rule plugin"}, /* plugin desc */
        distinguishedNameMatch_names,                       /* matching rule name/oid/aliases */
        NULL,
        NULL,
        dn_filter_ava,
        NULL,
        dn_values2keys,
        dn_assertion2keys_ava,
        NULL,
        NULL,
        NULL /* mr_nomalise */
    },
};

static size_t mr_plugin_table_size = sizeof(mr_plugin_table) / sizeof(mr_plugin_table[0]);

static int
matching_rule_plugin_init(Slapi_PBlock *pb)
{
    return syntax_matching_rule_plugin_init(pb, mr_plugin_table, mr_plugin_table_size);
}

static int
register_matching_rule_plugins(void)
{
    return syntax_register_matching_rule_plugins(mr_plugin_table, mr_plugin_table_size, matching_rule_plugin_init);
}

int
dn_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "=> dn_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
                           (void *)dn_filter_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
                           (void *)dn_filter_sub);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
                           (void *)dn_values2keys);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
                           (void *)dn_assertion2keys_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
                           (void *)dn_assertion2keys_sub);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NAMES,
                           (void *)names);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_OID,
                           (void *)DN_SYNTAX_OID);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
                           (void *)dn_validate);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
                           (void *)dn_normalize);

    rc |= register_matching_rule_plugins();
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "<= dn_init %d\n", rc);
    return (rc);
}

static int
dn_filter_ava(Slapi_PBlock *pb, struct berval *bvfilter, Slapi_Value **bvals, int ftype, Slapi_Value **retVal)
{
    int filter_normalized = 0;
    int syntax = SYNTAX_CIS | SYNTAX_DN;
    if (pb) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED,
                         &filter_normalized);
        if (filter_normalized) {
            syntax |= SYNTAX_NORM_FILT;
        }
    }
    return (string_filter_ava(bvfilter, bvals, syntax, ftype, retVal));
}

static int
dn_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals)
{
    return (string_filter_sub(pb, initial, any, final, bvals,
                              SYNTAX_CIS | SYNTAX_DN));
}

static int
dn_values2keys(Slapi_PBlock *pb, Slapi_Value **vals, Slapi_Value ***ivals, int ftype)
{
    return (string_values2keys(pb, vals, ivals, SYNTAX_CIS | SYNTAX_DN,
                               ftype));
}

static int
dn_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype)
{
    return (string_assertion2keys_ava(pb, val, ivals,
                                      SYNTAX_CIS | SYNTAX_DN, ftype));
}

static int
dn_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals)
{
    return (string_assertion2keys_sub(pb, initial, any, final, ivals,
                                      SYNTAX_CIS | SYNTAX_DN));
}

static int
dn_validate(struct berval *val)
{
    int rc = 0; /* Assume value is valid */

    /* A 0 length value is valid for the DN syntax. */
    if (val == NULL) {
        rc = 1;
    } else if (val->bv_len > 0) {
        rc = distinguishedname_validate(val->bv_val, &(val->bv_val[val->bv_len - 1]));
    }

    return rc;
}

static void
dn_normalize(
    Slapi_PBlock *pb __attribute__((unused)),
    char *s,
    int trim_spaces,
    char **alt)
{
    value_normalize_ext(s, SYNTAX_CIS | SYNTAX_DN, trim_spaces, alt);
    return;
}
