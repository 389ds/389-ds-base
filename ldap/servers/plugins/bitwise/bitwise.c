/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* orfilter.c - implementation of ordering rule filter */

#include <ldap.h>         /* LDAP_UTF8INC */
#include <slap.h>         /* for debug macros */
#include <slapi-plugin.h> /* slapi_berval_cmp, SLAPI_BERVAL_EQ */

#ifdef HPUX11
#include <dl.h>
#endif /* HPUX11 */

/* the match function needs the attribute type and value from the search
   filter - this is unfortunately not passed into the match fn, so we
   have to keep track of this
*/
struct bitwise_match_cb
{
    char *type;         /* the attribute type from the filter ava */
    struct berval *val; /* the value from the filter ava */
};

/*
  The type and val pointers are assumed to have sufficient lifetime -
  we don't have to copy them - they are usually just pointers into
  the SLAPI_PLUGIN_MR_TYPE and SLAPI_PLUGIN_MR_VALUE fields of the
  operation pblock, whose lifetime should encompass the creation
  and destruction of the bitwise_match_cb object.
*/
static struct bitwise_match_cb *
new_bitwise_match_cb(char *type, struct berval *val)
{
    struct bitwise_match_cb *bmc = (struct bitwise_match_cb *)slapi_ch_calloc(1, sizeof(struct bitwise_match_cb));
    bmc->type = slapi_ch_strdup(type);
    bmc->val = val;

    return bmc;
}

static void
delete_bitwise_match_cb(struct bitwise_match_cb *bmc)
{
    slapi_ch_free_string(&bmc->type);
    slapi_ch_free((void **)&bmc);
}

static void
bitwise_filter_destroy(Slapi_PBlock *pb)
{
    void *obj = NULL;
    slapi_pblock_get(pb, SLAPI_PLUGIN_OBJECT, &obj);
    if (obj) {
        struct bitwise_match_cb *bmc = (struct bitwise_match_cb *)obj;
        delete_bitwise_match_cb(bmc);
        obj = NULL;
        /* coverity[var_deref_model] */
        slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, obj);
    }
}

#define BITWISE_OP_AND 0
#define BITWISE_OP_OR 1

static int
internal_bitwise_filter_match(void *obj, Slapi_Entry *entry, Slapi_Attr *attr __attribute__((unused)), int op)
/* returns:  0  filter matched
 *        -1  filter did not match
 *        >0  an LDAP error code
 */
{
    struct bitwise_match_cb *bmc = obj;
    auto int rc = -1; /* no match */
    char **ary = NULL;
    int ii;

    ary = slapi_entry_attr_get_charray(entry, bmc->type);

    /* look through all values until we find a match */
    for (ii = 0; (rc == -1) && ary && ary[ii]; ++ii) {
        unsigned long long a, b;
        char *val_from_entry = ary[ii];
        errno = 0;
        a = strtoull(val_from_entry, NULL, 10);
        if (errno != ERANGE) {
            errno = 0;
            b = strtoull(bmc->val->bv_val, NULL, 10);
            if (errno == ERANGE) {
                rc = LDAP_CONSTRAINT_VIOLATION;
            } else {
                int result = 0;
                /* The Microsoft Windows AD bitwise operators do not work exactly
           as the plain old C bitwise operators work.  For the AND case
           the matching rule is true only if all bits from the given value
           match the value from the entry.  For the OR case, the matching
           rule is true if any bits from the given value match the value
           from the entry.
           For the AND case, this means that even though (a & b) is True,
           if (a & b) != b, the matching rule will return False.
           For the OR case, this means that even though (a | b) is True,
           this may be because there are bits in a.  But we only care
           about bits in a that are also in b.  So we do (a & b) - this
           will return what we want, which is to return True if any of
           the bits in b are also in a.
        */
                if (op == BITWISE_OP_AND) {
                    result = ((a & b) == b); /* all the bits in the given value are found in the value from the entry */
                } else if (op == BITWISE_OP_OR) {
                    result = (a & b); /* any of the bits in b are also in a */
                }
                if (result) {
                    rc = 0;
                }
            }
        }
    }
    slapi_ch_array_free(ary);
    return rc;
}

static int
bitwise_filter_match_and(void *obj, Slapi_Entry *entry, Slapi_Attr *attr)
/* returns:  0  filter matched
 *        -1  filter did not match
 *        >0  an LDAP error code
 */
{
    return internal_bitwise_filter_match(obj, entry, attr, BITWISE_OP_AND);
}

static int
bitwise_filter_match_or(void *obj, Slapi_Entry *entry, Slapi_Attr *attr)
/* returns:  0  filter matched
 *        -1  filter did not match
 *        >0  an LDAP error code
 */
{
    return internal_bitwise_filter_match(obj, entry, attr, BITWISE_OP_OR);
}

static int
bitwise_filter_create(Slapi_PBlock *pb)
{
    auto int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION; /* failed to initialize */
    auto char *mrOID = NULL;
    auto char *mrTYPE = NULL;
    auto struct berval *mrVALUE = NULL;

    if (!slapi_pblock_get(pb, SLAPI_PLUGIN_MR_OID, &mrOID) && mrOID != NULL &&
        !slapi_pblock_get(pb, SLAPI_PLUGIN_MR_TYPE, &mrTYPE) && mrTYPE != NULL &&
        !slapi_pblock_get(pb, SLAPI_PLUGIN_MR_VALUE, &mrVALUE) && mrVALUE != NULL) {

        struct bitwise_match_cb *bmc = NULL;
        if (strcmp(mrOID, "1.2.840.113556.1.4.803") == 0) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_MR_FILTER_MATCH_FN, (void *)bitwise_filter_match_and);
        } else if (strcmp(mrOID, "1.2.840.113556.1.4.804") == 0) {
            slapi_pblock_set(pb, SLAPI_PLUGIN_MR_FILTER_MATCH_FN, (void *)bitwise_filter_match_or);
        } else { /* this oid not handled by this plugin */
            slapi_log_err(SLAPI_LOG_FILTER, "bitwise_filter_create", "OID (%s) not handled\n", mrOID);
            return rc;
        }
        bmc = new_bitwise_match_cb(mrTYPE, mrVALUE);
        slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, bmc);
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESTROY_FN, (void *)bitwise_filter_destroy);
        rc = LDAP_SUCCESS;
    } else {
        slapi_log_err(SLAPI_LOG_FILTER, "bitwise_filter_create", "missing parameter(s)\n");
    }
    slapi_log_err(SLAPI_LOG_FILTER, "bitwise_filter_create", "%i\n", rc);
    return LDAP_SUCCESS;
}

static Slapi_PluginDesc pdesc = {"bitwise", VENDOR, DS_PACKAGE_VERSION,
                                 "bitwise match plugin"};

int /* LDAP error code */
    bitwise_init(Slapi_PBlock *pb)
{
    int rc;

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_MR_FILTER_CREATE_FN, (void *)bitwise_filter_create);
    if (rc == 0) {
        rc = slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc);
    }
    slapi_log_err(SLAPI_LOG_FILTER, "bitwise_init", "%i\n", rc);
    return rc;
}
