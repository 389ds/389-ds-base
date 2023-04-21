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


/**
 * Distributed Numeric Assignment plug-in
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "portable.h"
#include "slap.h"
#include "nspr.h"
#include "slapi-private.h"
#include "slapi-plugin.h"
#include "prclist.h"

#include <sys/stat.h>



#define TEST_SLAPI_MEMBEROF_FEATURE_DESC "test slapi_memberof"
#define TEST_SLAPI_MEMBEROF_EXOP_FEATURE_DESC "test slapi_memberof Extension Request"
#define TEST_SLAPI_MEMBEROF_PLUGIN_DESC "test slapi_memberof plugin"
#define TEST_SLAPI_MEMBEROF_EXOP_DESC "test slapi_memberof extop plugin"


#define TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM "test-slapi_memberof-plugin"

#define TEST_SLAPI_MEMBEROF_MEMBER_DN        "slapimemberOfMemberDN"
#define TEST_SLAPI_MEMBEROF_GROUP_ATTR       "slapimemberOfGroupAttr"
#define TEST_SLAPI_MEMBEROF_ATTR             "slapimemberOfAttr"
#define TEST_SLAPI_MEMBEROF_BACKEND_ATTR     "slapimemberOfAllBackends"
#define TEST_SLAPI_MEMBEROF_ENTRY_SCOPE_ATTR "slapimemberOfEntryScope"
#define TEST_SLAPI_MEMBEROF_ENTRY_SCOPE_EXCLUDE_SUBTREE "slapimemberOfEntryScopeExcludeSubtree"
#define TEST_SLAPI_MEMBEROF_SKIP_NESTED_ATTR "slapimemberOfSkipNested"
#define TEST_SLAPI_MEMBEROF_MAXGROUP         "slapimemberOfMaxGroup"


static Slapi_PluginDesc pdesc = {TEST_SLAPI_MEMBEROF_FEATURE_DESC,
                                 "389 Project - test plugin",
                                 "RHDS 9.3",
                                 TEST_SLAPI_MEMBEROF_PLUGIN_DESC};

static Slapi_PluginDesc exop_pdesc = {TEST_SLAPI_MEMBEROF_EXOP_FEATURE_DESC,
                                      "389 Project - test plugin",
                                      "RHDS 9.3",
                                      TEST_SLAPI_MEMBEROF_EXOP_DESC};

typedef struct test_slapi_memberof_config
{
    char *member_dn;
    Slapi_DN *sdn_member_dn;
    char **groupattrs;
    char *memberof_attr;
    int32_t maxgroup;
    PRBool allBackends;
    PRBool skip_nested;
    char **entryScopes;
    Slapi_DN **sdn_entryScopes;
    char **entryScopeExcludeSubtrees;
    Slapi_DN **sdn_entryScopeExcludeSubtrees;
} Test_Slapi_MemberOf_Config;
static Test_Slapi_MemberOf_Config theConfig = {0};
static void *_PluginID = NULL;

#define TEST_SLAPI_MEMBEROF_EXOP_REQUEST_OID "2.3.4.5.113730.6.7.1"
#define TEST_SLAPI_MEMBEROF_EXOP_RESPONSE_OID "2.3.4.5.113730.6.7.2"
static char *test_slapi_memberof_exop_oid_list[] = {
    TEST_SLAPI_MEMBEROF_EXOP_REQUEST_OID,
    NULL};


int test_slapi_memberof_init(Slapi_PBlock *pb);
static int test_slapi_memberof_exop_init(Slapi_PBlock *pb);
static int test_slapi_memberof_extend_exop(Slapi_PBlock *pb);
static int test_slapi_memberof_start(Slapi_PBlock *pb);
static int test_slapi_memberof_close(Slapi_PBlock *pb __attribute__((unused)));


/**
 * Plugin identity mgmt
 */
void
setPluginID(void *pluginID)
{
    _PluginID = pluginID;
}

void *
getPluginID(void)
{
    return _PluginID;
}


/*
    test_slapi_memberof_init plugin init function
*/
int
test_slapi_memberof_init(Slapi_PBlock *pb)
{
    int status = SLAPI_PLUGIN_SUCCESS;
    char *plugin_identity = NULL;

    slapi_log_err(SLAPI_LOG_NOTICE, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> test_slapi_memberof_init\n");

    /**
     * Store the plugin identity for later use.
     * Used for internal operations
     */

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    setPluginID(plugin_identity);

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *)test_slapi_memberof_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *)test_slapi_memberof_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pdesc) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_init - Failed to register plugin\n");
        status = SLAPI_PLUGIN_FAILURE;
    }

    if ((status == SLAPI_PLUGIN_SUCCESS) &&
        /* the range extension extended operation */
        slapi_register_plugin("extendedop", /* op type */
                              1,                 /* Enabled */
                              "test_slapi_memberof_init",        /* this function desc */
                              test_slapi_memberof_exop_init,     /* init func for exop */
                              TEST_SLAPI_MEMBEROF_EXOP_DESC,     /* plugin desc */
                              NULL,              /* ? */
                              plugin_identity    /* access control */
                              )) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_init - Failed to register plugin\n");
        status = SLAPI_PLUGIN_FAILURE;
    }


    slapi_log_err(SLAPI_LOG_NOTICE, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- test_slapi_memberof_init\n");
    return status;
}




static int
test_slapi_memberof_exop_init(Slapi_PBlock *pb)
{
    int status = SLAPI_PLUGIN_SUCCESS;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&exop_pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_EXT_OP_OIDLIST,
                         (void *)test_slapi_memberof_exop_oid_list) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_EXT_OP_FN,
                         (void *)test_slapi_memberof_extend_exop) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_exop_init - Failed to register plugin\n");
        status = SLAPI_PLUGIN_FAILURE;
    }

    return status;
}

static int
test_slapi_memberof_start(Slapi_PBlock *pb)
{
    Slapi_Entry *config_e = NULL; /* entry containing plugin config */
    char **groupattrs = NULL;
    char *memberof_attr = NULL;
    char *member_dn = NULL;
    int maxgroup = 0;
    const char *allBackends = NULL;
    const char *skip_nested = NULL;
    char **entryScopes = NULL;
    char **entryScopeExcludeSubtrees = NULL;
    size_t i;

    if (slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &config_e) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_start - Missing config entry\n");
        return SLAPI_PLUGIN_FAILURE;
    }
    groupattrs = slapi_entry_attr_get_charray(config_e, TEST_SLAPI_MEMBEROF_GROUP_ATTR);
    memberof_attr = slapi_entry_attr_get_charptr(config_e, TEST_SLAPI_MEMBEROF_ATTR);
    member_dn = slapi_entry_attr_get_charptr(config_e, TEST_SLAPI_MEMBEROF_MEMBER_DN);
    allBackends = slapi_entry_attr_get_ref(config_e, TEST_SLAPI_MEMBEROF_BACKEND_ATTR);
    skip_nested = slapi_entry_attr_get_ref(config_e, TEST_SLAPI_MEMBEROF_SKIP_NESTED_ATTR);
    entryScopes = slapi_entry_attr_get_charray(config_e, TEST_SLAPI_MEMBEROF_ENTRY_SCOPE_ATTR);
    entryScopeExcludeSubtrees = slapi_entry_attr_get_charray(config_e, TEST_SLAPI_MEMBEROF_ENTRY_SCOPE_EXCLUDE_SUBTREE);
    maxgroup = slapi_entry_attr_get_int(config_e, TEST_SLAPI_MEMBEROF_MAXGROUP);

    theConfig.groupattrs = groupattrs;
    theConfig.member_dn = member_dn;
    theConfig.sdn_member_dn = slapi_sdn_new_dn_byval(member_dn);
    theConfig.memberof_attr = memberof_attr;
    if (skip_nested) {
        if (strcasecmp(skip_nested, "on") == 0) {
            theConfig.skip_nested = PR_TRUE;
        } else {
            theConfig.skip_nested = PR_FALSE;
        }
    } else {
        theConfig.skip_nested = PR_FALSE;
    }

    if (allBackends) {
        if (strcasecmp(allBackends, "on") == 0) {
            theConfig.allBackends = PR_TRUE;
        } else {
            theConfig.allBackends = PR_FALSE;
        }
    } else {
        theConfig.allBackends = PR_FALSE;
    }
    theConfig.entryScopes = entryScopes;
    for (i = 0; entryScopes && entryScopes[i]; i++);
    theConfig.sdn_entryScopes = (Slapi_DN **) slapi_ch_calloc(sizeof(Slapi_DN *), i + 1);
    for (i = 0; entryScopes && entryScopes[i]; i++) {
        theConfig.sdn_entryScopes[i] = slapi_sdn_new_dn_byval((const char *)entryScopes[i]);
    }

    theConfig.entryScopeExcludeSubtrees = entryScopeExcludeSubtrees;
    for (i = 0; entryScopeExcludeSubtrees && entryScopeExcludeSubtrees[i]; i++);
    theConfig.sdn_entryScopeExcludeSubtrees = (Slapi_DN **) slapi_ch_calloc(sizeof(Slapi_DN *), i + 1);
    for (i = 0; entryScopeExcludeSubtrees && entryScopeExcludeSubtrees[i]; i++) {
        theConfig.sdn_entryScopeExcludeSubtrees[i] = slapi_sdn_new_dn_byval((const char *)entryScopeExcludeSubtrees[i]);
    }
    theConfig.maxgroup = maxgroup;

    return SLAPI_PLUGIN_SUCCESS;
}

static int
test_slapi_memberof_close(Slapi_PBlock *pb __attribute__((unused)))
{
    return SLAPI_PLUGIN_SUCCESS;
}

/****************************************************
 * Test Slapi_memberof Extended Operation
 ***************************************************/
static int
test_slapi_memberof_extend_exop(Slapi_PBlock *pb)
{
    char *oid = NULL;
    int ret = SLAPI_PLUGIN_EXTENDED_NOT_HANDLED;
    int i;
    int idx = 0;
    int count;
    char error_buffer[1024] = {0};
    int32_t error_buffer_size;
    Slapi_MemberOfConfig config = {0};
    Slapi_MemberOfResult groupvals = {0};
    Slapi_DN *target_sdn;
    BerElement *req_bere = NULL;
    struct berval *reqdata = NULL;
    char *req_dn = NULL;
    BerElement *respber = NULL;
    struct berval *respdata = NULL;
    Slapi_Value *v;
    struct berval **returned_bervals = NULL;
    char **returned_array = NULL;

    error_buffer_size = sizeof(error_buffer);

    if (!slapi_plugin_running(pb)) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - plugin not started\n");
        return ret;
    }

    slapi_log_err(SLAPI_LOG_NOTICE, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                  "--> test_slapi_memberof_extend_exop\n");
        /* Fetch the request OID */
    slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_OID, &oid);
    if (!oid) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - Unable to retrieve request OID.\n");
        return ret;
    }

    /* decode the exop */
    slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_VALUE, &reqdata);
    if (BV_HAS_DATA(reqdata)) {
        req_bere = ber_init(reqdata);
        if (req_bere == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                          "test_slapi_memberof_extend_exop - Failed to decode/init the DN from the request.\n");
            return ret;
        }
        ber_scanf(req_bere, "a", &req_dn);
        ber_free(req_bere, 1);
        req_bere = NULL;
    }

    if (req_dn) {
        slapi_log_err(SLAPI_LOG_NOTICE, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "--> Target entry is %s\n", req_dn);
    } else {
        slapi_log_err(SLAPI_LOG_NOTICE, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "--> Target entry is (fallback) %s\n", theConfig.member_dn);
    }

    /* Make sure the request OID is correct. */
    if (strcmp(oid, TEST_SLAPI_MEMBEROF_EXOP_REQUEST_OID) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - Received incorrect request OID.\n");
        return ret;
    }

    /* Checking the incoming attributes */
    if (theConfig.member_dn == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - missing %s attribute (e.g. 'uid=user0,ou=people,dc=example,dc=com').\n",
                      TEST_SLAPI_MEMBEROF_MEMBER_DN);
        strncpy(error_buffer, "missing slapimemberOfMemberDN", error_buffer_size - 1);
        goto skip_it;
    }
    if (theConfig.memberof_attr == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - missing %s attribute (e.g. 'memberof').\n",
                      TEST_SLAPI_MEMBEROF_ATTR);
        strncpy(error_buffer, "missing slapimemberOfAttr", error_buffer_size - 1);
        goto skip_it;
    }
    if ((theConfig.groupattrs == NULL) || (theConfig.groupattrs[0] == NULL)) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - missing %s attribute (e.g. 'member' or 'manager').\n",
                      TEST_SLAPI_MEMBEROF_GROUP_ATTR);
        strncpy(error_buffer, "missing slapimemberOfGroupAttr", error_buffer_size - 1);
        goto skip_it;
    }
    if ((theConfig.entryScopes == NULL) || (theConfig.entryScopes[0] == NULL)) {
        slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - missing %s attribute (e.g. 'dc=example,dc=com').\n",
                      TEST_SLAPI_MEMBEROF_ENTRY_SCOPE_ATTR);
        strncpy(error_buffer, "missing slapimemberOfEntryScope", error_buffer_size - 1);
        goto skip_it;
    }

    config.memberof_attr = (const char *) theConfig.memberof_attr;
    config.groupattrs = theConfig.groupattrs;
    config.allBackends = theConfig.allBackends;
    config.recurse = (! theConfig.skip_nested);

    config.entryScopes = theConfig.sdn_entryScopes;
    config.entryScopeExcludeSubtrees = theConfig.sdn_entryScopeExcludeSubtrees;
    config.maxgroups = theConfig.maxgroup;
    config.flag = MEMBEROF_RECOMPUTE;
    config.error_msg = error_buffer;
    config.errot_msg_lenght = error_buffer_size;
    config.subtree_search = PR_FALSE;
    if (req_dn) {
        target_sdn = slapi_sdn_new_dn_byval(req_dn);
    } else {
        target_sdn = theConfig.sdn_member_dn;
    }
    ret = slapi_memberof(&config, target_sdn, &groupvals);
    if (req_dn) {
        slapi_sdn_free(&target_sdn);
    }
    slapi_log_err(SLAPI_LOG_NOTICE, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - slapi_memberof -> %d).\n",
                      ret);
    /*sval = valueset_get_valuearray(groupvals.dn_vals); */
    for (i = slapi_valueset_first_value(groupvals.dn_vals, &v);
         i != -1;
         i = slapi_valueset_next_value(groupvals.dn_vals, i, &v)) {
        slapi_log_err(SLAPI_LOG_NOTICE, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                      "test_slapi_memberof_extend_exop - slapi_memberof found %s.\n",
                      slapi_value_get_string(v));
    }


skip_it:
    if ((respber = ber_alloc()) == NULL) {
        ret = LDAP_NO_MEMORY;
        goto free_and_return;
    }
    count = slapi_valueset_count(groupvals.dn_vals);
    if (count) {
        returned_bervals = (struct berval **)slapi_ch_malloc(sizeof(struct berval *) * (count + 1));
        returned_bervals[count] = NULL;
        for (i = slapi_valueset_first_value(groupvals.dn_vals, &v), idx = 0;
             i != -1;
             i = slapi_valueset_next_value(groupvals.dn_vals, i, &v), idx++) {
            returned_bervals[idx] = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
            returned_bervals[idx]->bv_val = slapi_ch_strdup(slapi_value_get_string(v));
            returned_bervals[idx]->bv_len = strlen(slapi_value_get_string(v));
        }
        returned_array = (char **) slapi_ch_malloc(sizeof(char *) * (count + 1));
        returned_array[count] = NULL;
        for (i = slapi_valueset_first_value(groupvals.dn_vals, &v), idx = 0;
             i != -1;
             i = slapi_valueset_next_value(groupvals.dn_vals, i, &v), idx++) {
            returned_array[idx] = slapi_ch_strdup(slapi_value_get_string(v));
        }
        if (LBER_ERROR == (ber_printf(respber, "[V]", returned_bervals))) {
            slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                    "test_slapi_memberof_extend_exop - Unable to encode exop response.\n");
            ber_free(respber, 1);
            ret = LDAP_ENCODING_ERROR;
            goto free_and_return;
        }
    } else {
        if (LBER_ERROR == (ber_printf(respber, "{s}", error_buffer))) {
            slapi_log_err(SLAPI_LOG_ERR, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                    "test_slapi_memberof_extend_exop - Unable to encode exop response.\n");
            ber_free(respber, 1);
            ret = LDAP_ENCODING_ERROR;
            goto free_and_return;
        }
    }
    ber_flatten(respber, &respdata);
    ber_free(respber, 1);

    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_OID, TEST_SLAPI_MEMBEROF_EXOP_RESPONSE_OID);
    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_VALUE, respdata);

    /* send the response ourselves */
    slapi_send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
    ret = SLAPI_PLUGIN_EXTENDED_SENT_RESULT;
    ber_bvfree(respdata);

free_and_return:

    if (returned_bervals)
        ber_bvecfree(returned_bervals);
    slapi_log_err(SLAPI_LOG_NOTICE, TEST_SLAPI_MEMBEROF_PLUGIN_SUBSYSTEM,
                  "<-- test_slapi_memberof_extend_exop\n");

    return ret;
}

