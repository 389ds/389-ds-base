/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 *
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception.
 *
 *
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "usn.h"

static Slapi_PluginDesc pdesc = {
        "USN", VENDOR, DS_PACKAGE_VERSION,
        "USN (Update Sequence Number) plugin" };

static CSNGen *_usn_csngen = NULL;

static void *_usn_identity = NULL;

static int usn_preop_init(Slapi_PBlock *pb);
static int usn_bepreop_init(Slapi_PBlock *pb);
static int usn_bepostop_init(Slapi_PBlock *pb);
static int usn_rootdse_init();

static int usn_preop_delete(Slapi_PBlock *pb);
static int usn_bepreop_add(Slapi_PBlock *pb);
static int usn_bepreop_delete(Slapi_PBlock *pb);
static int usn_bepreop_modify(Slapi_PBlock *pb);
static int usn_bepostop(Slapi_PBlock *pb);
static int usn_start(Slapi_PBlock *pb);
static int usn_close(Slapi_PBlock *pb);
static int usn_get_attr(Slapi_PBlock *pb, const char* type, void *value);

static int usn_rootdse_search(Slapi_PBlock *pb, Slapi_Entry* e,
        Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg);

/*
 * Register USN plugin
 * Note: USN counter initialization is done in the backend (ldbm_usn_init).
 */
int
usn_init(Slapi_PBlock *pb)
{
    int rc = 0;
    void *identity = NULL;
    int enabled = 0;

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_init\n");

    slapi_pblock_get(pb, SLAPI_PLUGIN_ENABLED, &enabled);

    if (!enabled) {
        /* not enabled */
        goto bail;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &identity);

    /* slapi_register_plugin always returns SUCCESS (0) */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pdesc) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                        "usn_init: failed to register version & description\n");
        rc = -1;
        goto bail;
    }
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *)usn_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *)usn_close) != 0 ) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                        "usn_init: failed to register close callback & task\n");
        rc = -1;
        goto bail;
    }

    rc = slapi_register_plugin("preoperation", 1 /* Enabled */,
                               "usn_preop_init", usn_preop_init,
                               "USN preoperation plugin", NULL, identity);
    rc |= slapi_register_plugin("bepreoperation", 1 /* Enabled */,
                               "usn_bepreop_init", usn_bepreop_init,
                               "USN bepreoperation plugin", NULL, identity);
    rc |= slapi_register_plugin("bepostoperation", 1 /* Enabled */,
                               "usn_bepostop_init", usn_bepostop_init,
                               "USN bepostoperation plugin", NULL, identity);
    usn_set_identity(identity);
bail:
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_init\n");
    return rc;
}

static int
usn_preop_init(Slapi_PBlock *pb)
{
    int rc = 0;

    /* set up csn generator for tombstone */
    _usn_csngen = csngen_new(USN_CSNGEN_ID, NULL);
    if (NULL == _usn_csngen) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                        "usn_preop_init: csngen_new failed\n");
        rc = -1;
    }

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_DELETE_FN,
                                 (void *)usn_preop_delete) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                        "usn_preop_init: failed to register preop plugin\n");
        rc = -1;
    }

    return rc;
}

static int
usn_bepreop_init(Slapi_PBlock *pb)
{
    int rc = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_BE_PRE_ADD_FN,
                                 (void *)usn_bepreop_add) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_BE_PRE_DELETE_FN,
                                 (void *)usn_bepreop_delete) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_BE_PRE_MODIFY_FN,
                                 (void *)usn_bepreop_modify) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_BE_PRE_MODRDN_FN,
                                 (void *)usn_bepreop_modify) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                    "usn_bepreop_init: failed to register bepreop plugin\n");
        rc = -1;
    }

    return rc;
}

static int
usn_bepostop_init(Slapi_PBlock *pb)
{
    int rc = 0;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_BE_POST_ADD_FN,
                                 (void *)usn_bepostop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_BE_POST_DELETE_FN,
                                 (void *)usn_bepostop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_BE_POST_MODIFY_FN,
                                 (void *)usn_bepostop) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_BE_POST_MODRDN_FN,
                                 (void *)usn_bepostop) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                    "usn_bepostop_init: failed to register bepostop plugin\n");
        rc = -1;
    }

    return rc;
}

static int
usn_rootdse_init()
{
    int rc = -1;

    if (slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP,
                                        "", LDAP_SCOPE_BASE, "(objectclass=*)", 
                                        usn_rootdse_search, NULL)) {
        rc = 0;
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
    int rc = 0;
    Slapi_Value *value;

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM, "--> usn_start\n");

    rc = usn_rootdse_init();
    rc |= usn_cleanup_start(pb);
    if (rc) {
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
                slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                        "nsds5ReplicatedAttributeList: %s\n", 
                        slapi_value_get_string(v));
            }
        }
        slapi_valueset_free(vs);
    }
    /* add nsds5ReplicatedAttributeList: (objectclass=*) $ EXCLUDE entryusn 
     * to cn=plugin default config,cn=config */
    value = slapi_value_new_string("(objectclass=*) $ EXCLUDE entryusn");
    rc = slapi_set_plugin_default_config("nsds5ReplicatedAttributeList", value);
    slapi_value_free(&value);
bail:
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_start (rc: %d)\n", rc);
    return rc;
}

/*
 * usn_close: release the csn generator used to convert an entry to tombstone
 */
static int
usn_close(Slapi_PBlock *pb)
{
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM, "--> usn_close\n");

    csngen_free(&_usn_csngen);

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM, "<-- usn_close\n");

    return 0;
}

/* 
 * usn_preop_delete -- set params to turn the entry to tombstone
 */
static int
usn_preop_delete(Slapi_PBlock *pb)
{
    int rc = 0;
    CSN *csn = NULL;
    CSN *orig_csn = NULL;
    Slapi_Operation *op = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_preop_delete\n");

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    orig_csn = operation_get_csn(op);

    if (NULL == orig_csn) {
        /* 
         * No other plugins hasn't set csn yet, so let's set USN's csn.
         * If other plugin overrides csn and replica_attr_handler, that's fine.
         */
        rc = csngen_new_csn(_usn_csngen, &csn, PR_FALSE /* notify */);
        if (CSN_SUCCESS != rc) {
            slapi_log_error(SLAPI_LOG_FATAL, USN_PLUGIN_SUBSYSTEM,
                            "usn_preop_delete: csngen_new failed (%d)\n", rc);
            csngen_free(&csn);
            goto bail;
        }
        operation_set_csn(op, csn);
        slapi_operation_set_replica_attr_handler(op, (void *)usn_get_attr);
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_preop_delete\n");

    return rc;
}

#define KEEP_PREV_USN 1

static void
_usn_add_next_usn(Slapi_Entry *e, Slapi_Backend *be, int flags)
{
    struct berval usn_berval = {0};
    Slapi_Attr* attr = NULL;

    if (NULL == be->be_usn_counter) {
        /* USN plugin is not enabled */
        return;
    }

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> _usn_add_next_usn\n");

    /* add next USN to the entry; "be" contains the usn counter */
    usn_berval.bv_val = slapi_ch_smprintf("%" NSPRIu64, 
                                   slapi_counter_get_value(be->be_usn_counter));
    usn_berval.bv_len = strlen(usn_berval.bv_val);
    slapi_entry_attr_find(e, SLAPI_ATTR_ENTRYUSN, &attr);
    if (NULL == attr) { /* ENTRYUSN does not exist; add it */
        Slapi_Value *usn_value = slapi_value_new_berval(&usn_berval);
        slapi_entry_add_value(e, SLAPI_ATTR_ENTRYUSN, usn_value);
        slapi_value_free(&usn_value);
    } else { /* ENTRYUSN exists; replace it */
        struct berval *new_bvals[2];
        struct berval **prev_values = NULL;
        if (KEEP_PREV_USN == flags) {
            if (0 == slapi_attr_get_bervals_copy(attr, &prev_values)) {
                slapi_entry_add_values(e,
                                SLAPI_ATTR_ENTRYUSN_PREV, prev_values);
                ber_bvecfree(prev_values);
            }
        }
        new_bvals[0] = &usn_berval;
        new_bvals[1] = NULL;
        slapi_entry_attr_replace(e, SLAPI_ATTR_ENTRYUSN, new_bvals);
    }
    slapi_ch_free_string(&usn_berval.bv_val);

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
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

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> _usn_mod_next_usn\n");

    /* add next USN to the mods; "be" contains the usn counter */
    usn_berval.bv_val = counter_buf;
    PR_snprintf(usn_berval.bv_val, USN_COUNTER_BUF_LEN, "%" NSPRIu64, 
                                   slapi_counter_get_value(be->be_usn_counter));
    usn_berval.bv_len = strlen(usn_berval.bv_val);
    bvals[0] = &usn_berval;
    bvals[1] = NULL;

    slapi_mods_init_byref(&smods, *mods);
    /* bvals is duplicated by ber_bvdup in slapi_mods_add_modbvps */
    slapi_mods_add_modbvps(&smods, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES,
                           SLAPI_ATTR_ENTRYUSN, bvals);

    *mods = slapi_mods_get_ldapmods_byref(&smods);

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- _usn_mod_next_usn\n");
    return LDAP_SUCCESS;
}

/*
 * usn_bepreop_add - add next USN to the entry to be added
 */
static int
usn_bepreop_add(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    Slapi_Backend *be = NULL;
    int rc = LDAP_SUCCESS;

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_bepreop_add\n");

    /* add next USN to the entry; "be" contains the usn counter */
    slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
    if (NULL == e) {
        rc = LDAP_NO_SUCH_OBJECT;    
        goto bail;
    }
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (NULL == be) {
        rc = LDAP_PARAM_ERROR;    
        goto bail;
    }
    _usn_add_next_usn(e, be, 0);
bail:
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_bepreop_add\n");
    return rc;
}

/* 
 * usn_bepreop_delete -- add/replace next USN to the entry
 *                       bepreop_delete is not called if the entry is tombstone
 */
static int
usn_bepreop_delete(Slapi_PBlock *pb)
{
    Slapi_Entry *e = NULL;
    Slapi_Backend *be = NULL;
    int rc = LDAP_SUCCESS;

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_bepreop_delete\n");

    /* add next USN to the entry; "be" contains the usn counter */
    slapi_pblock_get(pb, SLAPI_DELETE_BEPREOP_ENTRY, &e);
    if (NULL == e) {
        rc = LDAP_NO_SUCH_OBJECT;    
        goto bail;
    }
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (NULL == be) {
        rc = LDAP_PARAM_ERROR;    
        goto bail;
    }
    if (e->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
        Slapi_Operation *op = NULL;
        slapi_pblock_get(pb, SLAPI_OPERATION, &op);
        slapi_operation_set_flag(op, OP_FLAG_TOMBSTONE_ENTRY);
    } else {
        _usn_add_next_usn(e, be, KEEP_PREV_USN);
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_bepreop_delete\n");
    return rc;
}

/*
 * usn_bepreop_modify - add/replace next USN to the mods; 
 *                      shared by modify and modrdn
 */
static int
usn_bepreop_modify (Slapi_PBlock *pb)
{
    LDAPMod **mods = NULL;
    Slapi_Backend *be = NULL;
    int rc = LDAP_SUCCESS;

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_bepreop_modify\n");

    /* add/replace next USN to the mods; "be" contains the usn counter */
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (NULL == be) {
        rc = LDAP_PARAM_ERROR;    
        goto bail;
    }
    if (LDAP_SUCCESS == _usn_mod_next_usn(&mods, be)) {
        slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_bepreop_modify\n");
    return rc;
}

/* count up the counter */
static int
usn_bepostop (Slapi_PBlock *pb)
{
    int rc = -1;
    Slapi_Backend *be = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_bepostop\n");

    /* if op is not successful, don't increment the counter */
    slapi_pblock_get(pb, SLAPI_RESULT_CODE, &rc);
    if (LDAP_SUCCESS != rc) {
        goto bail;
    }

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);
    if (NULL == be) {
        rc = LDAP_PARAM_ERROR;    
        goto bail;
    }

    if (be->be_usn_counter) {
        slapi_counter_increment(be->be_usn_counter);
    }
bail:
    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_bepostop\n");
    return rc;
}

/* mimic replication to turn on create_tombstone_entry */
static int
usn_get_attr(Slapi_PBlock *pb, const char* type, void *value)
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
usn_get_identity()
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
usn_rootdse_search(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter,
                   int *returncode, char *returntext, void *arg)
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

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "--> usn_rootdse_search\n");

    usn_berval.bv_val = counter_buf;
    PR_snprintf(attr, USN_LAST_USN_ATTR_CORE_LEN+1, "%s;", USN_LAST_USN);
    attr_subp = attr + USN_LAST_USN_ATTR_CORE_LEN;
    for (be = slapi_get_first_backend(&cookie); be;
         be = slapi_get_next_backend(cookie)) {
        if (NULL == be->be_usn_counter) { /* no counter == not a db backend */
            continue;
        }
        /* get a next USN counter from be_usn_counter; then minus 1 from it */
        PR_snprintf(usn_berval.bv_val, USN_COUNTER_BUF_LEN, "%" NSPRI64 "d", 
                                 slapi_counter_get_value(be->be_usn_counter)-1);
        usn_berval.bv_len = strlen(usn_berval.bv_val);

        if (USN_LAST_USN_ATTR_CORE_LEN + strlen(be->be_name) + 1 > attr_len) {
            attr_len *= 2;
            attr = (char *)slapi_ch_realloc(attr, attr_len);
            attr_subp = attr + USN_LAST_USN_ATTR_CORE_LEN;
        }
        PR_snprintf(attr_subp, attr_len - USN_LAST_USN_ATTR_CORE_LEN,
                                "%s", be->be_name);
        slapi_entry_attr_replace(e, attr, vals);
    }

    slapi_ch_free_string(&cookie);
    slapi_ch_free_string(&attr);

    slapi_log_error(SLAPI_LOG_TRACE, USN_PLUGIN_SUBSYSTEM,
                    "<-- usn_rootdse_search\n");
    return SLAPI_DSE_CALLBACK_OK;
}
