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

/* deliverymethod.c - Delivery Method syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int delivery_filter_ava(Slapi_PBlock *pb, struct berval *bvfilter, Slapi_Value **bvals, int ftype, Slapi_Value **retVal);
static int delivery_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals);
static int delivery_values2keys(Slapi_PBlock *pb, Slapi_Value **val, Slapi_Value ***ivals, int ftype);
static int delivery_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype);
static int delivery_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals);
static int delivery_compare(struct berval *v1, struct berval *v2);
static int delivery_validate(struct berval *val);
static int pdm_validate(const char *start, const char *end);
static void delivery_normalize(
    Slapi_PBlock *pb,
    char *s,
    int trim_spaces,
    char **alt);

/* the first name is the official one from RFC 4517 */
static char *names[] = {"Delivery Method", "delivery", DELIVERYMETHOD_SYNTAX_OID, 0};

static Slapi_PluginDesc pdesc = {"delivery-syntax", VENDOR, DS_PACKAGE_VERSION,
                                 "Delivery Method attribute syntax plugin"};

int
delivery_init(Slapi_PBlock *pb)
{
    int rc, flags;

    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "=> delivery_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
                           (void *)delivery_filter_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
                           (void *)delivery_filter_sub);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
                           (void *)delivery_values2keys);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
                           (void *)delivery_assertion2keys_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
                           (void *)delivery_assertion2keys_sub);
    flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
                           (void *)&flags);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NAMES,
                           (void *)names);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_OID,
                           (void *)DELIVERYMETHOD_SYNTAX_OID);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
                           (void *)delivery_compare);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
                           (void *)delivery_validate);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
                           (void *)delivery_normalize);

    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "<= delivery_init %d\n", rc);
    return (rc);
}

static int
delivery_filter_ava(
    Slapi_PBlock *pb,
    struct berval *bvfilter,
    Slapi_Value **bvals,
    int ftype,
    Slapi_Value **retVal)
{
    int filter_normalized = 0;
    int syntax = SYNTAX_CIS;
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
delivery_filter_sub(
    Slapi_PBlock *pb,
    char *initial,
    char **any,
    char * final,
    Slapi_Value **bvals)
{
    return (string_filter_sub(pb, initial, any, final, bvals, SYNTAX_CIS));
}

static int
delivery_values2keys(
    Slapi_PBlock *pb,
    Slapi_Value **vals,
    Slapi_Value ***ivals,
    int ftype)
{
    return (string_values2keys(pb, vals, ivals, SYNTAX_CIS,
                               ftype));
}

static int
delivery_assertion2keys_ava(
    Slapi_PBlock *pb,
    Slapi_Value *val,
    Slapi_Value ***ivals,
    int ftype)
{
    return (string_assertion2keys_ava(pb, val, ivals,
                                      SYNTAX_CIS, ftype));
}

static int
delivery_assertion2keys_sub(
    Slapi_PBlock *pb,
    char *initial,
    char **any,
    char * final,
    Slapi_Value ***ivals)
{
    return (string_assertion2keys_sub(pb, initial, any, final, ivals,
                                      SYNTAX_CIS));
}

static int
delivery_compare(
    struct berval *v1,
    struct berval *v2)
{
    return value_cmp(v1, v2, SYNTAX_CIS, 3 /* Normalise both values */);
}

static int
delivery_validate(
    struct berval *val)
{
    int rc = 0; /* assume the value is valid */
    const char *start = NULL;
    const char *end = NULL;
    const char *p = NULL;

    /* Per RFC4517:
     *
     * DeliveryMethod = pdm *( WSP DOLLAR WSP pdm )
     * pdm = "any" / "mhs" / "physical" / "telex" / "teletex" /
     *       "g3fax" / "g4fax" / "ia5" / "videotex" / "telephone"
     */

    /* Don't allow a 0 length string */
    if ((val == NULL) || (val->bv_len == 0)) {
        rc = 1;
        goto exit;
    }

    start = &(val->bv_val[0]);
    end = &(val->bv_val[val->bv_len - 1]);

    /* Loop through each delivery method. */
    for (p = start; p <= end; p++) {
        if (p == end) {
            /* Validate start through p */
            rc = pdm_validate(start, p);
            goto exit;
        } else if (IS_SPACE(*p) || IS_DOLLAR(*p)) {
            /* Validate start through p-1.  Advance
             * pointer to next start char. */
            if ((rc = pdm_validate(start, p - 1)) != 0) {
                goto exit;
            } else {
                int got_separator = 0;

                /* Advance until we find the
                 * start of the next pdm. */
                for (p++; p <= end; p++) {
                    /* If we hit the end before encountering
                     * another pdm, fail.  We can do this check
                     * without looking at what the actual char
                     * is first since no single char is a valid
                     * pdm. */
                    if (p == end) {
                        rc = 1;
                        goto exit;
                    } else if (IS_DOLLAR(*p)) {
                        /* Only allow one '$' between pdm's. */
                        if (got_separator) {
                            rc = 1;
                            goto exit;
                        } else {
                            got_separator = 1;
                        }
                    } else if (!IS_SPACE(*p)) {
                        /* Set start to point to what
                         * should be the start of the
                         * next pdm. */
                        start = p;
                        break;
                    }
                }
            }
        }
    }

exit:
    return rc;
}

/*
 * pdm_validate()
 *
 * Returns 0 if the string from start to end is a valid
 * pdm, otherwise returns 1.
 */
static int
pdm_validate(const char *start, const char *end)
{
    int rc = 0; /* Assume string is valid */
    size_t length = 0;

    if ((start == NULL) || (end == NULL)) {
        rc = 1;
        goto exit;
    }

    /* Per RFC4517:
     *
     * DeliveryMethod = pdm *( WSP DOLLAR WSP pdm )
     * pdm = "any" / "mhs" / "physical" / "telex" / "teletex" /
     *       "g3fax" / "g4fax" / "ia5" / "videotex" / "telephone"
     */

    /* Check length first for efficiency. */
    length = end - start + 1;
    switch (length) {
    case 3:
        if ((strncmp(start, "any", length) != 0) &&
            (strncmp(start, "mhs", length) != 0) &&
            (strncmp(start, "ia5", length) != 0)) {
            rc = 1;
        }
        break;
    case 5:
        if ((strncmp(start, "telex", length) != 0) &&
            (strncmp(start, "g3fax", length) != 0) &&
            (strncmp(start, "g4fax", length) != 0)) {
            rc = 1;
        }
        break;
    case 7:
        if (strncmp(start, "teletex", length) != 0) {
            rc = 1;
        }
        break;
    case 8:
        if ((strncmp(start, "physical", length) != 0) &&
            (strncmp(start, "videotex", length) != 0)) {
            rc = 1;
        }
        break;
    case 9:
        if (strncmp(start, "telephone", length) != 0) {
            rc = 1;
        }
        break;
    default:
        rc = 1;
        break;
    }

exit:
    return rc;
}

static void
delivery_normalize(
    Slapi_PBlock *pb __attribute__((unused)),
    char *s,
    int trim_spaces,
    char **alt)
{
    value_normalize_ext(s, SYNTAX_CIS, trim_spaces, alt);
    return;
}
