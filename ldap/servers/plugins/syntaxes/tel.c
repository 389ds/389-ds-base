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

/* tel.c - telephonenumber syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int tel_filter_ava(Slapi_PBlock *pb, struct berval *bvfilter, Slapi_Value **bvals, int ftype, Slapi_Value **retVal);
static int tel_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals);
static int tel_values2keys(Slapi_PBlock *pb, Slapi_Value **val, Slapi_Value ***ivals, int ftype);
static int tel_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype);
static int tel_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals);
static int tel_compare(struct berval *v1, struct berval *v2);
static int tel_validate(struct berval *val);
static void tel_normalize(
    Slapi_PBlock *pb,
    char *s,
    int trim_spaces,
    char **alt);

/* the first name is the official one from RFC 2252 */
static char *names[] = {"TelephoneNumber", "tel", TELEPHONE_SYNTAX_OID, 0};

static Slapi_PluginDesc pdesc = {"tele-syntax", VENDOR, DS_PACKAGE_VERSION,
                                 "telephoneNumber attribute syntax plugin"};

static const char *telephoneNumberMatch_names[] = {"telephoneNumberMatch", "2.5.13.20", NULL};
static const char *telephoneNumberSubstringsMatch_names[] = {"telephoneNumberSubstringsMatch", "2.5.13.21", NULL};

static char *telephoneNumberSubstringsMatch_syntaxes[] = {TELEPHONE_SYNTAX_OID, NULL};

static struct mr_plugin_def mr_plugin_table[] = {
    {
        {
            "2.5.13.20",
            NULL,
            "telephoneNumberMatch",
            "The telephoneNumberMatch rule compares an assertion value of the "
            "Telephone Number syntax to an attribute value of a syntax (e.g., the "
            "Telephone Number syntax) whose corresponding ASN.1 type is a "
            "PrintableString representing a telephone number. "
            "The rule evaluates to TRUE if and only if the prepared attribute "
            "value character string and the prepared assertion value character "
            "string have the same number of characters and corresponding "
            "characters have the same code point. "
            "In preparing the attribute value and assertion value for comparison, "
            "characters are case folded in the Map preparation step, and only "
            "telephoneNumber Insignificant Character Handling is applied in the "
            "Insignificant Character Handling step.",
            TELEPHONE_SYNTAX_OID,
            0,
            NULL /* tel syntax only */
        },       /* matching rule desc */
        {
            "telephoneNumberMatch-mr",
            VENDOR,
            DS_PACKAGE_VERSION,
            "telephoneNumberMatch matching rule plugin"}, /* plugin desc */
        telephoneNumberMatch_names,                       /* matching rule name/oid/aliases */
        NULL,                                             /* mr_filter_create */
        NULL,                                             /* mr_indexer_create */
        tel_filter_ava,                                   /* mr_filter_ava */
        NULL,                                             /* mr_filter_sub */
        tel_values2keys,                                  /* mr_values2keys */
        tel_assertion2keys_ava,                           /* mr_assertion2keys_ava */
        NULL,                                             /* mr_assertion2keys_sub */
        tel_compare,                                      /* mr_compare */
        NULL                                              /* mr_normalize */
    },
    {
        {"2.5.13.21",
         NULL,
         "telephoneNumberSubstringsMatch",
         "The telephoneNumberSubstringsMatch rule compares an assertion value "
         "of the Substring Assertion syntax to an attribute value of a syntax "
         "(e.g., the Telephone Number syntax) whose corresponding ASN.1 type is "
         "a PrintableString representing a telephone number. "
         "The rule evaluates to TRUE if and only if (1) the prepared substrings "
         "of the assertion value match disjoint portions of the prepared "
         "attribute value character string in the order of the substrings in "
         "the assertion value, (2) an <initial> substring, if present, matches "
         "the beginning of the prepared attribute value character string, and "
         "(3) a <final> substring, if present, matches the end of the prepared "
         "attribute value character string.  A prepared substring matches a "
         "portion of the prepared attribute value character string if "
         "corresponding characters have the same code point. "
         "In preparing the attribute value and assertion value substrings for "
         "comparison, characters are case folded in the Map preparation step, "
         "and only telephoneNumber Insignificant Character Handling is applied "
         "in the Insignificant Character Handling step.",
         "1.3.6.1.4.1.1466.115.121.1.58",
         0,
         telephoneNumberSubstringsMatch_syntaxes}, /* matching rule desc */
        {
            "telephoneNumberSubstringsMatch-mr",
            VENDOR,
            DS_PACKAGE_VERSION,
            "telephoneNumberSubstringsMatch matching rule plugin"}, /* plugin desc */
        telephoneNumberSubstringsMatch_names,                       /* matching rule name/oid/aliases */
        NULL,                                                       /* IFP mr_filter_create; */
        NULL,                                                       /* IFP mr_indexer_create; */
        NULL,                                                       /* mr_filter_ava */
        tel_filter_sub,                                             /* mr_filter_sub */
        tel_values2keys,                                            /* mr_values2keys */
        NULL,                                                       /*mr_assertion2keys_ava */
        tel_assertion2keys_sub,                                     /* mr_assertion2keys_sub */
        tel_compare,                                                /* mr_compare */
        NULL                                                        /* mr_normalize */
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
tel_init(Slapi_PBlock *pb)
{
    int rc, flags;

    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "=> tel_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
                           (void *)tel_filter_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
                           (void *)tel_filter_sub);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
                           (void *)tel_values2keys);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
                           (void *)tel_assertion2keys_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
                           (void *)tel_assertion2keys_sub);
    flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
                           (void *)&flags);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NAMES,
                           (void *)names);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_OID,
                           (void *)TELEPHONE_SYNTAX_OID);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
                           (void *)tel_compare);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
                           (void *)tel_validate);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
                           (void *)tel_normalize);

    rc |= register_matching_rule_plugins();
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "<= tel_init %d\n", rc);
    return (rc);
}

static int
tel_filter_ava(
    Slapi_PBlock *pb,
    struct berval *bvfilter,
    Slapi_Value **bvals,
    int ftype,
    Slapi_Value **retVal)
{
    int filter_normalized = 0;
    int syntax = SYNTAX_TEL | SYNTAX_CIS;
    if (pb) {
        slapi_pblock_get(pb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED,
                         &filter_normalized);
        if (filter_normalized) {
            syntax |= SYNTAX_NORM_FILT;
        }
    }
    return (string_filter_ava(bvfilter, bvals, syntax,
                              ftype, retVal));
}


static int
tel_filter_sub(
    Slapi_PBlock *pb,
    char *initial,
    char **any,
    char * final,
    Slapi_Value **bvals)
{
    return (string_filter_sub(pb, initial, any, final, bvals, SYNTAX_TEL | SYNTAX_CIS));
}

static int
tel_values2keys(
    Slapi_PBlock *pb,
    Slapi_Value **vals,
    Slapi_Value ***ivals,
    int ftype)
{
    return (string_values2keys(pb, vals, ivals, SYNTAX_TEL | SYNTAX_CIS,
                               ftype));
}

static int
tel_assertion2keys_ava(
    Slapi_PBlock *pb,
    Slapi_Value *val,
    Slapi_Value ***ivals,
    int ftype)
{
    return (string_assertion2keys_ava(pb, val, ivals,
                                      SYNTAX_TEL | SYNTAX_CIS, ftype));
}

static int
tel_assertion2keys_sub(
    Slapi_PBlock *pb,
    char *initial,
    char **any,
    char * final,
    Slapi_Value ***ivals)
{
    return (string_assertion2keys_sub(pb, initial, any, final, ivals,
                                      SYNTAX_TEL | SYNTAX_CIS));
}

static int
tel_compare(
    struct berval *v1,
    struct berval *v2)
{
    return value_cmp(v1, v2, SYNTAX_TEL | SYNTAX_CIS, 3 /* Normalise both values */);
}

static int
tel_validate(
    struct berval *val)
{
    int rc = 0; /* assume the value is valid */
    uint i = 0;

    /* Per RFC4517:
     *
     * TelephoneNumber = PrintableString
     * PrintableString = 1*PrintableCharacter
     */

    /* Don't allow a 0 length string */
    if ((val == NULL) || (val->bv_len == 0)) {
        rc = 1;
        goto exit;
    }

    /* Make sure all chars are a PrintableCharacter */
    for (i = 0; i < val->bv_len; i++) {
        if (!IS_PRINTABLE(val->bv_val[i])) {
            rc = 1;
            goto exit;
        }
    }

exit:
    return rc;
}

static void
tel_normalize(
    Slapi_PBlock *pb __attribute__((unused)),
    char *s,
    int trim_spaces,
    char **alt)
{
    value_normalize_ext(s, SYNTAX_TEL | SYNTAX_CIS, trim_spaces, alt);
    return;
}
