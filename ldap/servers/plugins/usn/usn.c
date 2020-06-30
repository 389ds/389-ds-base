/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "usn.h"

static Slapi_PluginDesc pdesc = {
    "USN", VENDOR, DS_PACKAGE_VERSION,
    "USN (Update Sequence Number) plugin"};

static void *_usn_identity = NULL;

static int usn_preop_init(Slapi_PBlock *pb);
static int usn_bepreop_init(Slapi_PBlock *pb);
static int usn_betxnpreop_init(Slapi_PBlock *pb);
static int usn_bepostop_init(Slapi_PBlock *pb);
static int usn_rootdse_init(Slapi_PBlock *pb);

static int usn_preop_delete(Slapi_PBlock *pb);
static int usn_bepreop_modify(Slapi_PBlock *pb);
static int usn_betxnpreop_add(Slapi_PBlock *pb);
static int usn_betxnpreop_delete(Slapi_PBlock *pb);
static int usn_bepostop(Slapi_PBlock *pb);
static int usn_bepostop_delete(Slapi_PBlock *pb);
static int usn_bepostop_modify(Slapi_PBlock *pb);

static int usn_start(Slapi_PBlock *pb);
static int usn_close(Slapi_PBlock *pb);
static int usn_get_attr(Slapi_PBlock *pb, const char *type, void *value);

static int usn_rootdse_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);

/*
 * Register USN plugin
 * Note: USN counter initialization is done in the backend (ldbm_usn_init).
 */
int
usn_init(Slapi_PBlock *pb)
{
    int rc = 0;
    void *identity = NULL;
    Slapi_Entry *plugin_entry = NULL;
    int is_betxn = 0;
    const char *plugintype;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_init\n");

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &identity);

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry) {
        is_betxn = slapi_entry_attr_get_bool(plugin_entry,
                                             "nsslapd-pluginbetxn");
    }

    /* slapi_register_plugin always returns SUCCESS (0) */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pdesc) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_init - Failed to register version & description\n");
        rc = -1;
        goto bail;
    }
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *)usn_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *)usn_close) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_init - Failed to register close callback & task\n");
        rc = -1;
        goto bail;
    }

    /* usn_preop_init: plugintype is preoperation (not be/betxn) */
    plugintype = "preoperation";
    rc = slapi_register_plugin(plugintype, 1 /* Enabled */,
                               "usn_preop_init", usn_preop_init,
                               "USN preoperation plugin", NULL, identity);

    /* usn_bepreop_init: plugintype is bepreoperation (not betxn) */
    plugintype = "bepreoperation";
    rc |= slapi_register_plugin(plugintype, 1 /* Enabled */,
                                "usn_bepreop_init", usn_bepreop_init,
                                "USN bepreoperation plugin", NULL, identity);

    /* usn_bepreop_init: plugintype is betxnpreoperation */
    plugintype = "betxnpreoperation";
    rc |= slapi_register_plugin(plugintype, 1 /* Enabled */,
                                "usn_betxnpreop_init", usn_betxnpreop_init,
                                "USN betxnpreoperation plugin", NULL, identity);
    plugintype = "bepostoperation";
    if (is_betxn) {
        plugintype = "betxnpostoperation";
    }
    rc |= slapi_register_plugin(plugintype, 1 /* Enabled */,
                                "usn_bepostop_init", usn_bepostop_init,
                                "USN bepostoperation plugin", NULL, identity);
    usn_set_identity(identity);
bail:
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_init\n");
    return rc;
}

/* This ops must be preop not be/betxn */
static int
usn_preop_init(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    int predel = SLAPI_PLUGIN_PRE_DELETE_FN;

    if (slapi_pblock_set(pb, predel, (void *)usn_preop_delete) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_preop_init - Failed to register preop plugin\n");
        rc = SLAPI_PLUGIN_FAILURE;
    }

    return rc;
}

static int
usn_bepreop_init(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    int premod = SLAPI_PLUGIN_BE_PRE_MODIFY_FN;
    int premdn = SLAPI_PLUGIN_BE_PRE_MODRDN_FN;

    /* usn_bepreop functions are called at BE_PRE_OP timing,
     * not at BE_TXN_PREOP */
    /* modify/modrdn updates mods which is evaluated before the
     * transaction start */
    if ((slapi_pblock_set(pb, premod, (void *)usn_bepreop_modify) != 0) ||
        (slapi_pblock_set(pb, premdn, (void *)usn_bepreop_modify) != 0)) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_bepreop_init - Failed to register bepreop plugin\n");
        rc = SLAPI_PLUGIN_FAILURE;
    }

    return rc;
}

static int
usn_betxnpreop_init(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    int preadd = SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN;
    int predel = SLAPI_PLUGIN_BE_TXN_PRE_DELETE_TOMBSTONE_FN;

    if ((slapi_pblock_set(pb, preadd, (void *)usn_betxnpreop_add) != 0) ||
        (slapi_pblock_set(pb, predel, (void *)usn_betxnpreop_delete) != 0)) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_betxnpreop_init - Failed to register betxnpreop plugin\n");
        rc = SLAPI_PLUGIN_FAILURE;
    }

    return rc;
}

static int
usn_bepostop_init(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    Slapi_Entry *plugin_entry = NULL;
    const char *plugin_type = NULL;
    int postadd = SLAPI_PLUGIN_BE_POST_ADD_FN;
    int postmod = SLAPI_PLUGIN_BE_POST_MODIFY_FN;
    int postmdn = SLAPI_PLUGIN_BE_POST_MODRDN_FN;
    int postdel = SLAPI_PLUGIN_BE_POST_DELETE_FN;

    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
        plugin_entry &&
        (plugin_type = slapi_entry_attr_get_ref(plugin_entry, "nsslapd-plugintype")) &&
        plugin_type && strstr(plugin_type, "betxn")) {
        postadd = SLAPI_PLUGIN_BE_TXN_POST_ADD_FN;
        postmod = SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN;
        postmdn = SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN;
        postdel = SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN;
    }

    if ((slapi_pblock_set(pb, postadd, (void *)usn_bepostop) != 0) ||
        (slapi_pblock_set(pb, postdel, (void *)usn_bepostop_delete) != 0) ||
        (slapi_pblock_set(pb, postmod, (void *)usn_bepostop_modify) != 0) ||
        (slapi_pblock_set(pb, postmdn, (void *)usn_bepostop) != 0)) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_bepostop_init - Failed to register bepostop plugin\n");
        rc = SLAPI_PLUGIN_FAILURE;
    }

    return rc;
}

static int
usn_rootdse_init(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_FAILURE;

    if (slapi_config_register_callback_plugin(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP | DSE_FLAG_PLUGIN,
                                              "", LDAP_SCOPE_BASE, "(objectclass=*)",
                                              usn_rootdse_search, NULL, pb)) {
        rc = SLAPI_PLUGIN_SUCCESS;
    }

    return rc;
}

/*
 * usn_start: usn_rootdse_init -- set rootdse callback to aggregate in rootDSE
 *            usn_cleanup_start -- initialize USN tombstone cleanup task
 */
static int
usn_start(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_SUCCESS;
    Slapi_Value *value;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM, "--> usn_start\n");

    rc = usn_rootdse_init(pb);
    rc |= usn_cleanup_start(pb);
    if (rc) {
        rc = SLAPI_PLUGIN_FAILURE;
        goto bail;
    }
    if (0) { /* Not executed; test code for slapi_get_plugin_default_config */
        Slapi_ValueSet *vs = NULL;
        Slapi_Value *v = NULL;
        int i;

        slapi_get_plugin_default_config("nsds5ReplicatedAttributeList", &vs);
        if (vs) {
            for (i = slapi_valueset_first_value(vs, &v);
                 i != -1;
                 i = slapi_valueset_next_value(vs, i, &v)) {
                slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                              "nsds5ReplicatedAttributeList: %s\n",
                              slapi_value_get_string(v));
            }
        }
        slapi_valueset_free(vs);
    }
    /* add nsds5ReplicatedAttributeList: (objectclass=*) $ EXCLUDE entryusn
     * to cn=plugin default config,cn=config */
    value = slapi_value_new_string("(objectclass=*) $ EXCLUDE entryusn");
    if (slapi_set_plugin_default_config("nsds5ReplicatedAttributeList", value)) {
        rc = SLAPI_PLUGIN_FAILURE;
    }
    slapi_value_free(&value);

bail:
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_start (rc: %d)\n", rc);
    return rc;
}

static int
usn_close(Slapi_PBlock *pb __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM, "--> usn_close\n");

    usn_cleanup_close();
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
                                 "", LDAP_SCOPE_BASE, "(objectclass=*)", usn_rootdse_search);

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM, "<-- usn_close\n");

    return SLAPI_PLUGIN_SUCCESS;
}

/*
 * usn_preop_delete -- set params to turn the entry to tombstone
 */
static int
usn_preop_delete(Slapi_PBlock *pb)
{
    Slapi_Operation *op = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_preop_delete\n");

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    if (NULL == op) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_preop_delete - Failed; no operation.\n");
        return SLAPI_PLUGIN_FAILURE;
    }
    slapi_operation_set_replica_attr_handler(op, (void *)usn_get_attr);

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_preop_delete\n");

    return SLAPI_PLUGIN_SUCCESS;
}

static void
_usn_add_next_usn(Slapi_Entry *e, Slapi_Backend *be)
{
    struct berval usn_berval = {0};
    Slapi_Attr *attr = NULL;

    if (NULL == be->be_usn_counter) {
        /* USN plugin is not enabled */
        return;
    }

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> _usn_add_next_usn\n");

    /* add next USN to the entry; "be" contains the usn counter */
    usn_berval.bv_val = slapi_ch_smprintf("%" PRIu64,
                                          slapi_counter_get_value(be->be_usn_counter));
    usn_berval.bv_len = strlen(usn_berval.bv_val);
    slapi_entry_attr_find(e, SLAPI_ATTR_ENTRYUSN, &attr);
    if (NULL == attr) { /* ENTRYUSN does not exist; add it */
        Slapi_Value *usn_value = slapi_value_new_berval(&usn_berval);
        slapi_entry_add_value(e, SLAPI_ATTR_ENTRYUSN, usn_value);
        slapi_value_free(&usn_value);
    } else { /* ENTRYUSN exists; replace it */
        struct berval *new_bvals[2];
        new_bvals[0] = &usn_berval;
        new_bvals[1] = NULL;
        slapi_entry_attr_replace(e, SLAPI_ATTR_ENTRYUSN, new_bvals);
    }
    slapi_ch_free_string(&usn_berval.bv_val);

    /*
     * increment the counter now and decrement in the bepostop
     * if the operation will fail
     */
    slapi_counter_increment(be->be_usn_counter);

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- _usn_add_next_usn\n");

    return;
}

static int
_usn_mod_next_usn(LDAPMod ***mods, Slapi_Backend *be)
{
    Slapi_Mods smods = {0};
    struct berval *bvals[2];
    struct berval usn_berval = {0};
    char counter_buf[USN_COUNTER_BUF_LEN];

    if (NULL == be->be_usn_counter) {
        /* USN plugin is not enabled */
        return LDAP_UNWILLING_TO_PERFORM;
    }

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> _usn_mod_next_usn\n");

    /* add next USN to the mods; "be" contains the usn counter */
    usn_berval.bv_val = counter_buf;
    snprintf(usn_berval.bv_val, USN_COUNTER_BUF_LEN, "%" PRIu64,
             slapi_counter_get_value(be->be_usn_counter));
    usn_berval.bv_len = strlen(usn_berval.bv_val);
    bvals[0] = &usn_berval;
    bvals[1] = NULL;

    slapi_mods_init_passin(&smods, *mods);
    /* bvals is duplicated by ber_bvdup in slapi_mods_add_modbvps */
    slapi_mods_add_modbvps(&smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES,
                           SLAPI_ATTR_ENTRYUSN, bvals);

    *mods = slapi_mods_get_ldapmods_passout(&smods);

    /*
     * increment the counter now and decrement in the bepostop
     * if the operation will fail
     */
    slapi_counter_increment(be->be_usn_counter);

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- _usn_mod_next_usn\n");
    return LDAP_SUCCESS;
}

/*
 * usn_betxnpreop_add - add next USN to the entry to be added
 */
static int
usn_betxnpreop_add(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    Slapi_Backend *be = NULL;
    int rc = SLAPI_PLUGIN_SUCCESS;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_betxnpreop_add\n");

    /* add next USN to the entry; "be" contains the usn counter */
    slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
    if (NULL == e) {
        rc = LDAP_NO_SUCH_OBJECT;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
        rc = SLAPI_PLUGIN_FAILURE;
        goto bail;
    }
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (NULL == be) {
        rc = LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
        rc = SLAPI_PLUGIN_FAILURE;
        goto bail;
    }
    _usn_add_next_usn(e, be);
bail:
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_betxnpreop_add\n");

    return rc;
}

/*
 * usn_betxnpreop_delete -- add/replace next USN to the entry
 *                       bepreop_delete is not called if the entry is tombstone
 */
static int
usn_betxnpreop_delete(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    Slapi_Backend *be = NULL;
    int32_t tombstone_incremented = 0;
    int rc = SLAPI_PLUGIN_SUCCESS;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_betxnpreop_delete\n");

    /* add next USN to the entry; "be" contains the usn counter */
    slapi_pblock_get(pb, SLAPI_DELETE_BEPREOP_ENTRY, &e);
    if (NULL == e) {
        rc = LDAP_NO_SUCH_OBJECT;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
        rc = SLAPI_PLUGIN_FAILURE;
        goto bail;
    }
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (NULL == be) {
        rc = LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
        rc = SLAPI_PLUGIN_FAILURE;
        goto bail;
    }
    _usn_add_next_usn(e, be);
    tombstone_incremented = 1;
bail:
    slapi_pblock_set(pb, SLAPI_USN_INCREMENT_FOR_TOMBSTONE, &tombstone_incremented);
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_betxnpreop_delete\n");

    return rc;
}

/*
 * usn_bepreop_modify - add/replace next USN to the mods;
 *                      shared by modify and modrdn
 * Note: bepreop should not return other than LDAP_SUCCESS.
 */
static int
usn_bepreop_modify(Slapi_PBlock *pb)
{
    LDAPMod **mods = NULL;
    Slapi_Backend *be = NULL;
    int rc = SLAPI_PLUGIN_SUCCESS;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_bepreop_modify\n");

    /* add/replace next USN to the mods; "be" contains the usn counter */
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (NULL == be) {
        slapi_log_err(SLAPI_LOG_ERR, USN_PLUGIN_SUBSYSTEM,
                      "usn_bepreop_modify - No backend.\n");
        rc = LDAP_PARAM_ERROR;
        slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
        rc = SLAPI_PLUGIN_FAILURE;
        goto bail;
    }
    if (LDAP_SUCCESS == _usn_mod_next_usn(&mods, be)) {
        slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
    }
bail:
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_bepreop_modify\n");
    return rc;
}

/* count down the counter */
static int
usn_bepostop(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_FAILURE;
    Slapi_Backend *be = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_bepostop\n");

    /* if op is not successful, decrement the counter, else - do nothing */
    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
    if (LDAP_SUCCESS != rc) {
        slapi_pblock_get(pb, SLAPI_BACKEND, &be);
        if (NULL == be) {
            rc = LDAP_PARAM_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
            rc = SLAPI_PLUGIN_FAILURE;
            goto bail;
        }

        if (be->be_usn_counter) {
            slapi_counter_decrement(be->be_usn_counter);
        }
    }

    /* no plugin failure */
    rc = SLAPI_PLUGIN_SUCCESS;
bail:
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_bepostop\n");

    return rc;
}

/* count down the counter on a failure and mod ignore */
static int
usn_bepostop_modify(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_FAILURE;
    Slapi_Backend *be = NULL;
    LDAPMod **mods = NULL;
    int32_t do_decrement = 0;
    int i;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_bepostop_modify\n");

    /* if op is not successful, don't increment the counter */
    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
    if (LDAP_SUCCESS != rc) {
        do_decrement = 1;
    }

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    for (i = 0; mods && mods[i]; i++) {
        if (0 == strcasecmp(mods[i]->mod_type, SLAPI_ATTR_ENTRYUSN)) {
            if (mods[i]->mod_op & LDAP_MOD_IGNORE) {
                slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                              "usn_bepostop_modify - MOD_IGNORE detected\n");
                do_decrement = 1; /* conflict occurred.
                                     decrement he counter. */
            } else {
                break;
            }
        }
    }

    if (do_decrement) {
        slapi_pblock_get(pb, SLAPI_BACKEND, &be);
        if (NULL == be) {
            rc = LDAP_PARAM_ERROR;
            slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
            rc = SLAPI_PLUGIN_FAILURE;
            goto bail;
        }
        if (be->be_usn_counter) {
            slapi_counter_decrement(be->be_usn_counter);
        }
    }

    /* no plugin failure */
    rc = SLAPI_PLUGIN_SUCCESS;
bail:
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_bepostop_modify\n");

    return rc;
}

/* count up the counter */
/* if the op is delete and the op was not successful, remove preventryusn */
/* the function is executed on TXN level */
static int
usn_bepostop_delete(Slapi_PBlock *pb)
{
    int rc = SLAPI_PLUGIN_FAILURE;
    Slapi_Backend *be = NULL;
    int32_t tombstone_incremented = 0;

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_bepostop_delete\n");

    /* if op is not successful and it is a tombstone entry, decrement the counter */
    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
    if (LDAP_SUCCESS != rc) {
        slapi_pblock_get(pb, SLAPI_USN_INCREMENT_FOR_TOMBSTONE, &tombstone_incremented);
        if (tombstone_incremented) {
            slapi_pblock_get(pb, SLAPI_BACKEND, &be);
            if (NULL == be) {
                rc = LDAP_PARAM_ERROR;
                slapi_pblock_set(pb, SLAPI_RESULT_CODE, &rc);
                rc = SLAPI_PLUGIN_FAILURE;
                goto bail;
            }

            if (be->be_usn_counter) {
                slapi_counter_decrement(be->be_usn_counter);
            }
        }
    }

    /* no plugin failure */
    rc = SLAPI_PLUGIN_SUCCESS;
bail:
    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_bepostop_delete\n");

    return rc;
}

/* mimic replication to turn on create_tombstone_entry */
static int
usn_get_attr(Slapi_PBlock *pb __attribute__((unused)), const char *type, void *value)
{
    if (0 == strcasecmp(type, "nsds5ReplicaTombstonePurgeInterval")) {
        *(int *)value = 1;
    } else {
        *(int *)value = 0;
    }

    return 0;
}

void
usn_set_identity(void *identity)
{
    _usn_identity = identity;
}

void *
usn_get_identity(void)
{
    return _usn_identity;
}

/*
 * usn_rootdse_search -- callback for the search on root DSN
 *                       add lastusn value per backend
 *
 * example:
 * ldapsearch -b "" -s base "(objectclass=*)" lastusn
 * dn:
 * lastusn;userroot: 72
 * lastusn;testbackend: 15
 */
static int
usn_rootdse_search(Slapi_PBlock *pb __attribute__((unused)),
                   Slapi_Entry *e,
                   Slapi_Entry *entryAfter __attribute__((unused)),
                   int *returncode __attribute__((unused)),
                   char *returntext __attribute__((unused)),
                   void *arg __attribute__((unused)))
{
    char *cookie = NULL;
    Slapi_Backend *be;
    struct berval *vals[2];
    struct berval usn_berval;
    vals[0] = &usn_berval;
    vals[1] = NULL;
    char counter_buf[USN_COUNTER_BUF_LEN];
    int attr_len = 64; /* length of lastusn;<backend_name> */
    char *attr = (char *)slapi_ch_malloc(attr_len);
    char *attr_subp = NULL;
    int isglobal = config_get_entryusn_global();

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "--> usn_rootdse_search\n");

    usn_berval.bv_val = counter_buf;
    if (isglobal) {
        /* nsslapd-entryusn-global: on*/
        /* root dse shows ...
         * lastusn: <num> */
        snprintf(attr, USN_LAST_USN_ATTR_CORE_LEN + 1, "%s", USN_LAST_USN);
        for (be = slapi_get_first_backend(&cookie); be;
             be = slapi_get_next_backend(cookie)) {
            if (be->be_usn_counter) {
                break;
            }
        }
        if (be && be->be_usn_counter) {
            /* get a next USN counter from be_usn_counter;
             * then minus 1 from it (except if be_usn_counter has value 0) */
            if (slapi_counter_get_value(be->be_usn_counter)) {
                snprintf(usn_berval.bv_val, USN_COUNTER_BUF_LEN, "%" PRIu64,
                         slapi_counter_get_value(be->be_usn_counter) - 1);
            } else {
                snprintf(usn_berval.bv_val, USN_COUNTER_BUF_LEN, "-1");
            }
            usn_berval.bv_len = strlen(usn_berval.bv_val);
            slapi_entry_attr_replace(e, attr, vals);
        }
    } else {
        /* nsslapd-entryusn-global: off (default) */
        /* root dse shows ...
         * lastusn;<backend>: <num> */
        snprintf(attr, USN_LAST_USN_ATTR_CORE_LEN + 2, "%s;", USN_LAST_USN);
        attr_subp = attr + USN_LAST_USN_ATTR_CORE_LEN + 1;
        for (be = slapi_get_first_backend(&cookie); be;
             be = slapi_get_next_backend(cookie)) {
            if (NULL == be->be_usn_counter) {
                /* no counter == not a db backend */
                continue;
            }
            /* get a next USN counter from be_usn_counter;
             * then minus 1 from it (except if be_usn_counter has value 0) */
            if (slapi_counter_get_value(be->be_usn_counter)) {
                snprintf(usn_berval.bv_val, USN_COUNTER_BUF_LEN, "%" PRIu64,
                         slapi_counter_get_value(be->be_usn_counter) - 1);
            } else {
                snprintf(usn_berval.bv_val, USN_COUNTER_BUF_LEN, "-1");
            }
            usn_berval.bv_len = strlen(usn_berval.bv_val);

            if (USN_LAST_USN_ATTR_CORE_LEN + strlen(be->be_name) + 2 > attr_len) {
                attr_len *= 2;
                attr = (char *)slapi_ch_realloc(attr, attr_len);
                attr_subp = attr + USN_LAST_USN_ATTR_CORE_LEN;
            }
            snprintf(attr_subp, attr_len - USN_LAST_USN_ATTR_CORE_LEN,
                     "%s", be->be_name);
            slapi_entry_attr_replace(e, attr, vals);
        }
    }

    slapi_ch_free_string(&cookie);
    slapi_ch_free_string(&attr);

    slapi_log_err(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                  "<-- usn_rootdse_search\n");
    return SLAPI_DSE_CALLBACK_OK;
}
