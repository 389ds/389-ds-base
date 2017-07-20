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

/* bitstring.c - Bit String syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int bitstring_filter_ava(Slapi_PBlock *pb, struct berval *bvfilter, Slapi_Value **bvals, int ftype, Slapi_Value **retVal);
static int bitstring_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals);
static int bitstring_values2keys(Slapi_PBlock *pb, Slapi_Value **val, Slapi_Value ***ivals, int ftype);
static int bitstring_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype);
static int bitstring_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals);
static int bitstring_compare(struct berval *v1, struct berval *v2);
static int bitstring_validate(struct berval *val);
static void bitstring_normalize(
    Slapi_PBlock *pb,
    char *s,
    int trim_spaces,
    char **alt);

/* the first name is the official one from RFC 4517 */
static char *names[] = {"Bit String", "bitstring", BITSTRING_SYNTAX_OID, 0};

static Slapi_PluginDesc pdesc = {"bitstring-syntax", VENDOR, DS_PACKAGE_VERSION,
                                 "Bit String attribute syntax plugin"};

static const char *bitStringMatch_names[] = {"bitStringMatch", "2.5.13.16", NULL};

static struct mr_plugin_def mr_plugin_table[] = {
    {
        {
            "2.5.13.16",
            NULL,
            "bitStringMatch",
            "The bitStringMatch rule compares an assertion value of the Bit String "
            "syntax to an attribute value of a syntax (e.g., the Bit String "
            "syntax) whose corresponding ASN.1 type is BIT STRING.  "
            "If the corresponding ASN.1 type of the attribute syntax does not have "
            "a named bit list [ASN.1] (which is the case for the Bit String "
            "syntax), then the rule evaluates to TRUE if and only if the attribute "
            "value has the same number of bits as the assertion value and the bits "
            "match on a bitwise basis.  "
            "If the corresponding ASN.1 type does have a named bit list, then "
            "bitStringMatch operates as above, except that trailing zero bits in "
            "the attribute and assertion values are treated as absent.",
            BITSTRING_SYNTAX_OID,
            0,
            NULL /* only the specified syntax is supported */
        },       /* matching rule desc */
        {
            "bitStringMatch-mr",
            VENDOR,
            DS_PACKAGE_VERSION,
            "bitStringMatch matching rule plugin"}, /* plugin desc */
        bitStringMatch_names,                       /* matching rule name/oid/aliases */
        NULL,
        NULL,
        bitstring_filter_ava,
        NULL,
        bitstring_values2keys,
        bitstring_assertion2keys_ava,
        NULL,
        bitstring_compare,
        NULL /* mr_normalize; */
    }};

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
bitstring_init(Slapi_PBlock *pb)
{
    int rc, flags;

    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "=> bitstring_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
                           (void *)bitstring_filter_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
                           (void *)bitstring_filter_sub);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
                           (void *)bitstring_values2keys);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
                           (void *)bitstring_assertion2keys_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
                           (void *)bitstring_assertion2keys_sub);
    flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
                           (void *)&flags);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NAMES,
                           (void *)names);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_OID,
                           (void *)BITSTRING_SYNTAX_OID);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
                           (void *)bitstring_compare);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
                           (void *)bitstring_validate);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
                           (void *)bitstring_normalize);

    rc |= register_matching_rule_plugins();
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "<= bitstring_init %d\n", rc);
    return (rc);
}

static int
bitstring_filter_ava(
    Slapi_PBlock *pb,
    struct berval *bvfilter,
    Slapi_Value **bvals,
    int ftype,
    Slapi_Value **retVal)
{
    int filter_normalized = 0;
    int syntax = SYNTAX_CES;
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
bitstring_filter_sub(
    Slapi_PBlock *pb,
    char *initial,
    char **any,
    char * final,
    Slapi_Value **bvals)
{
    return (string_filter_sub(pb, initial, any, final, bvals, SYNTAX_CES));
}

static int
bitstring_values2keys(
    Slapi_PBlock *pb,
    Slapi_Value **vals,
    Slapi_Value ***ivals,
    int ftype)
{
    return (string_values2keys(pb, vals, ivals, SYNTAX_CES,
                               ftype));
}

static int
bitstring_assertion2keys_ava(
    Slapi_PBlock *pb,
    Slapi_Value *val,
    Slapi_Value ***ivals,
    int ftype)
{
    return (string_assertion2keys_ava(pb, val, ivals,
                                      SYNTAX_CES, ftype));
}

static int
bitstring_assertion2keys_sub(
    Slapi_PBlock *pb,
    char *initial,
    char **any,
    char * final,
    Slapi_Value ***ivals)
{
    return (string_assertion2keys_sub(pb, initial, any, final, ivals,
                                      SYNTAX_CES));
}

static int
bitstring_compare(
    struct berval *v1,
    struct berval *v2)
{
    return value_cmp(v1, v2, SYNTAX_CES, 3 /* Normalise both values */);
}

static int
bitstring_validate(
    struct berval *val)
{
    int rc = 0; /* assume the value is valid */

    /* Don't allow a 0 length string */
    if ((val == NULL) || (val->bv_len == 0)) {
        rc = 1;
        goto exit;
    }

    rc = bitstring_validate_internal(val->bv_val, &(val->bv_val[val->bv_len - 1]));

exit:
    return rc;
}

static void
bitstring_normalize(
    Slapi_PBlock *pb __attribute__((unused)),
    char *s,
    int trim_spaces,
    char **alt)
{
    value_normalize_ext(s, SYNTAX_CES, trim_spaces, alt);
    return;
}
