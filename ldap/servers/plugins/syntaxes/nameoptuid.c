/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* nameoptuid.c - Name And Optional UID syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int nameoptuid_filter_ava(Slapi_PBlock *pb, struct berval *bvfilter, Slapi_Value **bvals, int ftype, Slapi_Value **retVal);
static int nameoptuid_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals);
static int nameoptuid_values2keys(Slapi_PBlock *pb, Slapi_Value **val, Slapi_Value ***ivals, int ftype);
static int nameoptuid_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype);
static int nameoptuid_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals);
static int nameoptuid_compare(struct berval *v1, struct berval *v2);
static int nameoptuid_validate(struct berval *val);
static void nameoptuid_normalize(
    Slapi_PBlock *pb,
    char *s,
    int trim_spaces,
    char **alt);

/* the first name is the official one from RFC 4517 */
static char *names[] = {"Name And Optional UID", "nameoptuid", NAMEANDOPTIONALUID_SYNTAX_OID, 0};

static Slapi_PluginDesc pdesc = {"nameoptuid-syntax", VENDOR, DS_PACKAGE_VERSION,
                                 "Name And Optional UID attribute syntax plugin"};

static const char *uniqueMemberMatch_names[] = {"uniqueMemberMatch", "2.5.13.23", NULL};
static struct mr_plugin_def mr_plugin_table[] = {
    {
        {
            "2.5.13.23",
            NULL,
            "uniqueMemberMatch",
            "The uniqueMemberMatch rule compares an assertion value of the Name "
            "And Optional UID syntax to an attribute value of a syntax (e.g., the "
            "Name And Optional UID syntax) whose corresponding ASN.1 type is "
            "NameAndOptionalUID.  "
            "The rule evaluates to TRUE if and only if the <distinguishedName> "
            "components of the assertion value and attribute value match according "
            "to the distinguishedNameMatch rule and either, (1) the <BitString> "
            "component is absent from both the attribute value and assertion "
            "value, or (2) the <BitString> component is present in both the "
            "attribute value and the assertion value and the <BitString> component "
            "of the assertion value matches the <BitString> component of the "
            "attribute value according to the bitStringMatch rule.  "
            "Note that this matching rule has been altered from its description in "
            "X.520 [X.520] in order to make the matching rule commutative.  Server "
            "implementors should consider using the original X.520 semantics "
            "(where the matching was less exact) for approximate matching of "
            "attributes with uniqueMemberMatch as the equality matching rule.",
            NAMEANDOPTIONALUID_SYNTAX_OID,
            0,
            NULL /* no other syntaxes supported */
        },       /* matching rule desc */
        {
            "uniqueMemberMatch-mr",
            VENDOR,
            DS_PACKAGE_VERSION,
            "uniqueMemberMatch matching rule plugin"}, /* plugin desc */
        uniqueMemberMatch_names,                       /* matching rule name/oid/aliases */
        NULL,
        NULL,
        nameoptuid_filter_ava,
        NULL,
        nameoptuid_values2keys,
        nameoptuid_assertion2keys_ava,
        NULL,
        nameoptuid_compare,
        NULL /* mr_normalise */
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
nameoptuid_init(Slapi_PBlock *pb)
{
    int rc, flags;

    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "=> nameoptuid_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
                           (void *)nameoptuid_filter_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
                           (void *)nameoptuid_filter_sub);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
                           (void *)nameoptuid_values2keys);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
                           (void *)nameoptuid_assertion2keys_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
                           (void *)nameoptuid_assertion2keys_sub);
    flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
                           (void *)&flags);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NAMES,
                           (void *)names);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_OID,
                           (void *)NAMEANDOPTIONALUID_SYNTAX_OID);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
                           (void *)nameoptuid_compare);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
                           (void *)nameoptuid_validate);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
                           (void *)nameoptuid_normalize);

    rc |= register_matching_rule_plugins();
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "<= nameoptuid_init %d\n", rc);
    return (rc);
}

static int
nameoptuid_filter_ava(
    Slapi_PBlock *pb,
    struct berval *bvfilter,
    Slapi_Value **bvals,
    int ftype,
    Slapi_Value **retVal)
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
nameoptuid_filter_sub(
    Slapi_PBlock *pb,
    char *initial,
    char **any,
    char * final,
    Slapi_Value **bvals)
{
    return (string_filter_sub(pb, initial, any, final, bvals,
                              SYNTAX_CIS | SYNTAX_DN));
}

static int
nameoptuid_values2keys(
    Slapi_PBlock *pb,
    Slapi_Value **vals,
    Slapi_Value ***ivals,
    int ftype)
{
    return (string_values2keys(pb, vals, ivals, SYNTAX_CIS | SYNTAX_DN,
                               ftype));
}

static int
nameoptuid_assertion2keys_ava(
    Slapi_PBlock *pb,
    Slapi_Value *val,
    Slapi_Value ***ivals,
    int ftype)
{
    return (string_assertion2keys_ava(pb, val, ivals,
                                      SYNTAX_CIS | SYNTAX_DN, ftype));
}

static int
nameoptuid_assertion2keys_sub(
    Slapi_PBlock *pb,
    char *initial,
    char **any,
    char * final,
    Slapi_Value ***ivals)
{
    return (string_assertion2keys_sub(pb, initial, any, final, ivals,
                                      SYNTAX_CIS | SYNTAX_DN));
}

static int
nameoptuid_compare(
    struct berval *v1,
    struct berval *v2)
{
    return value_cmp(v1, v2, SYNTAX_CIS | SYNTAX_DN, 3 /* Normalise both values */);
}

static int
nameoptuid_validate(
    struct berval *val)
{
    int rc = 0; /* assume the value is valid */
    int got_sharp = 0;
    const char *p = NULL;
    const char *start = NULL;
    const char *end = NULL;

    /* Per RFC4517:
     *
     * NameAndOptionalUID = distinguishedName [ SHARP BitString ]
     */

    /* Don't allow a 0 length string */
    if ((val == NULL) || (val->bv_len == 0)) {
        rc = 1;
        goto exit;
    }

    start = &(val->bv_val[0]);
    end = &(val->bv_val[val->bv_len - 1]);

    /* Find the last SHARP in the value that may be separating
     * the distinguishedName from the optional BitString. */
    for (p = end; p >= start + 1; p--) {
        if (IS_SHARP(*p)) {
            got_sharp = 1;
            break;
        }
    }

    if (got_sharp) {
        /* Try to validate everything after the sharp as
         * a BitString.  If this fails, we may still have
         * a valid value since a sharp is allowed in a
         * distinguishedName.  If we don't find a valid
         * BitString, just validate the entire value as
         * a distinguishedName. */
        if ((rc = bitstring_validate_internal(p + 1, end)) != 0) {
            rc = distinguishedname_validate(start, end);
        } else {
            rc = distinguishedname_validate(start, p - 1);
        }
    } else {
        /* No optional BitString is present, so validate
         * the entire value as a distinguishedName. */
        rc = distinguishedname_validate(start, end);
    }

exit:
    return rc;
}

static void
nameoptuid_normalize(
    Slapi_PBlock *pb __attribute__((unused)),
    char *s,
    int trim_spaces,
    char **alt)
{
    value_normalize_ext(s, SYNTAX_CIS | SYNTAX_DN, trim_spaces, alt);
    return;
}
