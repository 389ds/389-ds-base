/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* inchain.c - in_chain syntax routines
  see https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-adts/1e889adc-b503-4423-8985-c28d5c7d4887
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"
#include "slapi-plugin.h"

int inchain_filter_ava(Slapi_PBlock *pb, struct berval *bvfilter, Slapi_Value **bvals, int ftype, Slapi_Value **retVal);
int inchain_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals);
int inchain_values2keys(Slapi_PBlock *pb, Slapi_Value **val, Slapi_Value ***ivals, int ftype);
int inchain_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype);
int inchain_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals);
int inchain_validate(struct berval *val);
void inchain_normalize(
    Slapi_PBlock *pb,
    char *s,
    int trim_spaces,
    char **alt);

/* the first name is the official one from RFC 4517 */
static char *names[] = {"inchain", "inchain", LDAP_MATCHING_RULE_IN_CHAIN_OID, 0};

static Slapi_PluginDesc pdesc = {"inchain-matching-rule", VENDOR, DS_PACKAGE_VERSION,
                                 "inchain matching rule plugin"};

static const char *inchainMatch_names[] = {"inchainMatch", "1.2.840.113556.1.4.1941", NULL};

static struct mr_plugin_def mr_plugin_table[] = {
    {
        {
            "1.2.840.113556.1.4.1941",
            NULL,
            "inchainMatch",
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
            NULL,
            0,
            NULL /* dn only for now */
        },       /* matching rule desc */
        {
            "inchainMatch-mr",
            VENDOR,
            DS_PACKAGE_VERSION,
            "inchain matching rule plugin"}, /* plugin desc */
        inchainMatch_names,                       /* matching rule name/oid/aliases */
        NULL,
        NULL,
        inchain_filter_ava,
        NULL,
        inchain_values2keys,
        inchain_assertion2keys_ava,
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

static int
inchain_feature_allowed(Slapi_PBlock *pb)
{
    int isroot = 0;
    int ldapcode = LDAP_SUCCESS;

    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &isroot);
    if (!isroot) {
        char *dn;
        Slapi_Entry *feature = NULL;

        /* Fetch the feature entry and see if the requestor is allowed access. */
        dn = slapi_ch_smprintf("dn: oid=%s,cn=features,cn=config", LDAP_MATCHING_RULE_IN_CHAIN_OID);
        if ((feature = slapi_str2entry(dn, 0)) != NULL) {
            char *dummy_attr = "1.1";
            Slapi_Backend *be = NULL;

            be = slapi_mapping_tree_find_backend_for_sdn(slapi_entry_get_sdn(feature));
            if (NULL == be) {
                ldapcode = LDAP_INSUFFICIENT_ACCESS;
            } else {
                slapi_pblock_set(pb, SLAPI_BACKEND, be);
                ldapcode = slapi_access_allowed(pb, feature, dummy_attr, NULL, SLAPI_ACL_READ);
            }
        }

        /* If the feature entry does not exist, deny use of the control.  Only
         * the root DN will be allowed to use the control in this case. */
        if ((feature == NULL) || (ldapcode != LDAP_SUCCESS)) {
            ldapcode = LDAP_INSUFFICIENT_ACCESS;
        }
        slapi_ch_free((void **)&dn);
        slapi_entry_free(feature);
    }
    return (ldapcode);
}

int
inchain_init(Slapi_PBlock *pb)
{
    int rc;

    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "=> inchain_init\n");

    rc = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                          (void *)SLAPI_PLUGIN_VERSION_01);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                           (void *)&pdesc);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
                           (void *)inchain_filter_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
                           (void *)inchain_filter_sub);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
                           (void *)inchain_values2keys);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
                           (void *)inchain_assertion2keys_ava);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
                           (void *)inchain_assertion2keys_sub);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NAMES,
                           (void *)names);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_OID,
                           (void *)LDAP_MATCHING_RULE_IN_CHAIN_OID);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
                           (void *)inchain_validate);
    rc |= slapi_pblock_set(pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
                           (void *)inchain_normalize);

    rc |= register_matching_rule_plugins();
    slapi_log_err(SLAPI_LOG_PLUGIN, SYNTAX_PLUGIN_SUBSYSTEM, "<= inchain_init %d\n", rc);
    return (rc);
}

int
inchain_filter_ava(Slapi_PBlock *pb, struct berval *bvfilter, Slapi_Value **bvals, int ftype, Slapi_Value **retVal)
{
    /* always true because candidate entries are valid */
    /* in theory we should check the filter but with inchain MR
     * inchain_values2keys select candidates where membership attribute/value
     * are not systematically present in the candidate entry (recursive call)
     * this is the reason why this usual check does not apply
     */
#if 0
    int syntax = SYNTAX_CIS | SYNTAX_DN;
    return (string_filter_ava(bvfilter, bvals, syntax, ftype, retVal));
#else
    return(0);
#endif
}

int
inchain_filter_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value **bvals)
{
    return(1);
}


static PRIntn memberof_hash_compare_keys(const void *v1, const void *v2)
{
    PRIntn rc;
    if (0 == strcasecmp((const char *) v1, (const char *) v2)) {
        rc = 1;
    } else {
        rc = 0;
    }
    return rc;
}

static PRIntn memberof_hash_compare_values(const void *v1, const void *v2)
{
    PRIntn rc;
    if ((char *) v1 == (char *) v2) {
        rc = 1;
    } else {
        rc = 0;
    }
    return rc;
}

/*
 *  Hashing function using Bernstein's method
 */
static PLHashNumber memberof_hash_fn(const void *key)
{
    PLHashNumber hash = 5381;
    unsigned char *x = (unsigned char *)key;
    int c;

    while ((c = *x++)){
        hash = ((hash << 5) + hash) ^ c;
    }
    return hash;
}

/* allocates the plugin hashtable
 * This hash table is used by operation and is protected from
 * concurrent operations with the memberof_lock (if not usetxn, memberof_lock
 * is not implemented and the hash table will be not used.
 *
 * The hash table contains all the DN of the entries for which the memberof
 * attribute has been computed/updated during the current operation
 *
 * hash table should be empty at the beginning and end of the plugin callback
 */
PLHashTable *hashtable_new(int usetxn)
{
    if (!usetxn) {
        return NULL;
    }

    return PL_NewHashTable(1000,
        memberof_hash_fn,
        memberof_hash_compare_keys,
        memberof_hash_compare_values, NULL, NULL);
}
int
inchain_values2keys(Slapi_PBlock *pb, Slapi_Value **vals, Slapi_Value ***ivals, int ftype)
{
    Slapi_MemberOfResult groupvals = {0};
    Slapi_ValueSet *groupdn_vals;
    Slapi_Value **result;
    int nbvalues;
    Slapi_Value *v;
    Slapi_MemberOfConfig config = {0};
    Slapi_DN *member_sdn;
    Slapi_DN *base_sdn = NULL;
    size_t idx = 0;
    char *mrTYPE;
#if 0
    char *filter_str;
#endif
    char error_msg[1024] = {0};
    int rc;

    slapi_pblock_get(pb, SLAPI_PLUGIN_MR_TYPE, &mrTYPE);
    slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &base_sdn);

    if (! slapi_attr_is_dn_syntax_type(mrTYPE)) {
        slapi_log_err(SLAPI_LOG_ERR, "inchain", "Requires distinguishedName syntax. AttributeDescription %s is not distinguishedName\n");
        result = (Slapi_Value **)slapi_ch_calloc(1, sizeof(Slapi_Value *));
        *ivals = result;
        return(0);
    }

    /* check if the user is allowed to perform inChain matching */
    if (inchain_feature_allowed(pb) != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "inchain", "Requestor is not allowed to use InChain Matching rule\n");
        result = (Slapi_Value **)slapi_ch_calloc(1, sizeof(Slapi_Value *));
        *ivals = result;
        return(0);
    }

    /* it is used only in case of MEMBEROF_REUSE_ONLY of MEMBEROF_REUSE_IF_POSSIBLE
     * to reuse potential results from memberof plugin
     * So its value is only "memberof"
     */
    config.memberof_attr = "memberof";
    config.groupattrs = (char **) slapi_ch_calloc(sizeof(char*), 2);
    config.groupattrs[0] = mrTYPE;
    config.groupattrs[1] = NULL;
    config.subtree_search = PR_FALSE;
    config.allBackends = 0;
    config.entryScopes = (Slapi_DN **)slapi_ch_calloc(sizeof(Slapi_DN *), 2);
    /* only looking in the base search scope */
    config.entryScopes[0] = slapi_sdn_dup((const Slapi_DN *) base_sdn);

    /* no exclusion for inchain */
    config.entryScopeExcludeSubtrees = NULL;

#if 0
    filter_str = slapi_ch_smprintf("(%s=*)", "manager");
    config.group_filter = slapi_str2filter(filter_str);

    config.group_slapiattrs = (Slapi_Attr **)slapi_ch_calloc(sizeof(Slapi_Attr *), 3);
    config.group_slapiattrs[0] = slapi_attr_new();
    config.group_slapiattrs[1] = slapi_attr_new();
    slapi_attr_init(config.group_slapiattrs[0], "manager");
    slapi_attr_init(config.group_slapiattrs[1], "nsuniqueid");
#endif
    config.recurse = PR_TRUE;
    config.maxgroups = 0;
    config.flag = MEMBEROF_REUSE_IF_POSSIBLE;
    config.error_msg = error_msg;
    config.errot_msg_lenght = sizeof(error_msg);

    member_sdn = slapi_sdn_new_dn_byval((const char*) vals[0]->bv.bv_val);
    rc = slapi_memberof(&config, member_sdn, &groupvals);
    if (rc) {
        slapi_log_err(SLAPI_LOG_ERR, "inchain", " slapi_memberof fails %d (msg=%s)\n", rc, error_msg);
    }
#if 0
    slapi_filter_free(config.group_filter, 1);
    slapi_attr_free(&config.group_slapiattrs[0]);
    slapi_attr_free(&config.group_slapiattrs[1]);
#endif
    groupdn_vals = groupvals.nsuniqueid_vals;
    idx = slapi_valueset_first_value(groupdn_vals, &v);
    for (; groupdn_vals && v; idx = slapi_valueset_next_value(groupdn_vals, idx, &v)) {
        char value[1000];
        strncpy(value, v->bv.bv_val, v->bv.bv_len);
        value[v->bv.bv_len] = '\0';
        slapi_log_err(SLAPI_LOG_FILTER, "inchain", " groupvals = %s\n", value);

    }

#if 1

    nbvalues = slapi_valueset_count(groupdn_vals);
    result = (Slapi_Value **)slapi_ch_calloc(nbvalues + 1, sizeof(Slapi_Value *));
    for(idx = 0; idx < slapi_valueset_count(groupdn_vals); idx++) {
        char value[1000];

        result[idx] = slapi_value_dup(groupdn_vals->va[idx]);
        strncpy(value, result[idx]->bv.bv_val, result[idx]->bv.bv_len);
        value[result[idx]->bv.bv_len] = '\0';
        slapi_log_err(SLAPI_LOG_FILTER, "inchain", "copy key %s \n", value);
    }
    if (groupvals.dn_vals) {
        slapi_valueset_free(groupvals.dn_vals);
        groupvals.dn_vals = NULL;
    }
    if (groupvals.nsuniqueid_vals) {
        slapi_valueset_free(groupvals.nsuniqueid_vals);
        groupvals.nsuniqueid_vals = NULL;
    }
    *ivals = result;
    return(0);
#else
    return (string_values2keys(pb, vals, ivals, SYNTAX_CIS | SYNTAX_DN,
                               ftype));
#endif
}

int
inchain_assertion2keys_ava(Slapi_PBlock *pb, Slapi_Value *val, Slapi_Value ***ivals, int ftype)
{
    slapi_log_err(SLAPI_LOG_ERR, "inchain", "inchain_assertion2keys_ava \n");
    return (string_assertion2keys_ava(pb, val, ivals,
                                      SYNTAX_CIS | SYNTAX_DN, ftype));
}

int
inchain_assertion2keys_sub(Slapi_PBlock *pb, char *initial, char **any, char * final, Slapi_Value ***ivals)
{
    slapi_log_err(SLAPI_LOG_ERR, "inchain", "inchain_assertion2keys_sub \n");
    return (string_assertion2keys_sub(pb, initial, any, final, ivals,
                                      SYNTAX_CIS | SYNTAX_DN));
}

int
inchain_validate(struct berval *val)
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

void
inchain_normalize(
    Slapi_PBlock *pb __attribute__((unused)),
    char *s,
    int trim_spaces,
    char **alt)
{
    slapi_log_err(SLAPI_LOG_ERR, "inchain", "inchain_normalize %s \n", s);
    value_normalize_ext(s, SYNTAX_CIS | SYNTAX_DN, trim_spaces, alt);
    return;
}
