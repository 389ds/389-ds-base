/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/* repl5_replica_config.c - replica configuration over ldap */
#include <ctype.h> /* for isdigit() */
#include "repl5.h"
#include "cl5_api.h"
#include "cl5.h"
#include "slap.h"

/* CONFIG_BASE: no need to optimize */
#define CONFIG_BASE "cn=mapping tree,cn=config"
#define CONFIG_FILTER "(objectclass=nsDS5Replica)"
#define TASK_ATTR "nsds5Task"
#define CL2LDIF_TASK "CL2LDIF"
#define LDIF2CL_TASK "LDIF2CL"
#define CLEANRUV "CLEANRUV"
#define CLEANRUVLEN 8
#define CLEANALLRUV "CLEANALLRUV"
#define CLEANALLRUVLEN 11
#define REPLICA_RDN "cn=replica"

#define CLEANALLRUV_MAX_WAIT 7200 /* 2 hours */
#define CLEANALLRUV_SLEEP 5

int slapi_log_urp = SLAPI_LOG_REPL;
static ReplicaId cleaned_rids[CLEANRID_BUFSIZ] = {0};
static ReplicaId pre_cleaned_rids[CLEANRID_BUFSIZ] = {0};
static ReplicaId aborted_rids[CLEANRID_BUFSIZ] = {0};
static PRLock *rid_lock = NULL;
static PRLock *abort_rid_lock = NULL;
static pthread_mutex_t notify_lock;
static pthread_cond_t notify_cvar;
static PRLock *task_count_lock = NULL;
static int32_t clean_task_count = 0;
static int32_t abort_task_count = 0;

/* Forward Declartions */
static int replica_config_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_modify(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_post_modify(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_delete(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_cleanall_ruv_task(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static int replica_csngen_test_task(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static int replica_config_change_type_and_id(Replica *r, const char *new_type, const char *new_id, char *returntext, int apply_mods);
static int replica_config_change_updatedn(Replica *r, const LDAPMod *mod, char *returntext, int apply_mods);
static int replica_config_change_updatedngroup(Replica *r, const LDAPMod *mod, char *returntext, int apply_mods);
static int replica_config_change_flags(Replica *r, const char *new_flags, char *returntext, int apply_mods);
static int replica_execute_task(Replica *r, const char *task_name, char *returntext, int apply_mods);
static int replica_execute_cl2ldif_task(Replica *r, char *returntext);
static int replica_execute_ldif2cl_task(Replica *r, char *returntext);
static int replica_execute_cleanruv_task(Replica *r, ReplicaId rid, char *returntext);
static int replica_execute_cleanall_ruv_task(Replica *r, ReplicaId rid, Slapi_Task *task, const char *force_cleaning, PRBool original_task, char *returntext);
static void replica_cleanallruv_thread(void *arg);
static void replica_send_cleanruv_task(Repl_Agmt *agmt, cleanruv_data *clean_data);
static int check_agmts_are_alive(Replica *replica, ReplicaId rid, Slapi_Task *task);
static int check_agmts_are_caught_up(cleanruv_data *data, char *maxcsn);
static int replica_cleanallruv_send_extop(Repl_Agmt *ra, cleanruv_data *data, int check_result);
static int replica_cleanallruv_send_abort_extop(Repl_Agmt *ra, Slapi_Task *task, struct berval *payload);
static int replica_cleanallruv_check_maxcsn(Repl_Agmt *agmt, char *basedn, char *rid_text, char *maxcsn, Slapi_Task *task);
static int replica_cleanallruv_replica_alive(Repl_Agmt *agmt);
static int replica_cleanallruv_check_ruv(char *repl_root, Repl_Agmt *ra, char *rid_text, Slapi_Task *task, char *force);
static int replica_cleanup_task(Object *r, const char *task_name, char *returntext, int apply_mods);
static int replica_task_done(Replica *replica);
static void delete_cleaned_rid_config(cleanruv_data *data);
static int replica_cleanallruv_is_finished(Repl_Agmt *agmt, char *filter, Slapi_Task *task);
static void check_replicas_are_done_cleaning(cleanruv_data *data);
static void check_replicas_are_done_aborting(cleanruv_data *data);
static CSN *replica_cleanallruv_find_maxcsn(Replica *replica, ReplicaId rid, char *basedn);
static int replica_cleanallruv_get_replica_maxcsn(Repl_Agmt *agmt, char *rid_text, char *basedn, CSN **csn);
static void preset_cleaned_rid(ReplicaId rid);
static multimaster_mtnode_extension *_replica_config_get_mtnode_ext(const Slapi_Entry *e);
static void replica_cleanall_ruv_destructor(Slapi_Task *task);
static void replica_cleanall_ruv_abort_destructor(Slapi_Task *task);
static void remove_keep_alive_entry(Slapi_Task *task, ReplicaId rid, const char *repl_root);
static void clean_agmts(cleanruv_data *data);

/*
 * Note: internal add/modify/delete operations should not be run while
 * s_configLock is held.  E.g., slapi_modify_internal_pb via replica_task_done
 * in replica_config_post_modify.
 */
static PRLock *s_configLock;

static int
dont_allow_that(Slapi_PBlock *pb __attribute__((unused)),
                Slapi_Entry *entryBefore __attribute__((unused)),
                Slapi_Entry *e __attribute__((unused)),
                int *returncode,
                char *returntext __attribute__((unused)),
                void *arg __attribute__((unused)))
{
    *returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}

int
replica_config_init()
{
    int rc = 0;
    pthread_condattr_t condAttr;

    s_configLock = PR_NewLock();

    if (s_configLock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_init - "
                                                       "Failed to create configuration lock; NSPR error - %d\n",
                      PR_GetError());
        return -1;
    }
    rid_lock = PR_NewLock();
    if (rid_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_init - "
                                                       "Failed to create rid_lock; NSPR error - %d\n",
                      PR_GetError());
        return -1;
    }
    abort_rid_lock = PR_NewLock();
    if (abort_rid_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_init - "
                                                       "Failed to create abort_rid_lock; NSPR error - %d\n",
                      PR_GetError());
        return -1;
    }
    task_count_lock = PR_NewLock();
    if (task_count_lock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_init - "
                                                       "Failed to create task_count_lock; NSPR error - %d\n",
                      PR_GetError());
        return -1;
    }
    if ((rc = pthread_mutex_init(&notify_lock, NULL)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "replica_config_init",
                      "Failed to create notify lock: error %d (%s)\n",
                      rc, strerror(rc));
        return -1;
    }
    if ((rc = pthread_condattr_init(&condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "replica_config_init",
                      "Failed to create notify new condition attribute variable. error %d (%s)\n",
                      rc, strerror(rc));
        return -1;
    }
    if ((rc = pthread_condattr_setclock(&condAttr, CLOCK_MONOTONIC)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "replica_config_init",
                      "Cannot set condition attr clock. error %d (%s)\n",
                      rc, strerror(rc));
        return -1;
    }
    if ((rc = pthread_cond_init(&notify_cvar, &condAttr)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "replica_config_init",
                      "Failed to create new condition variable. error %d (%s)\n",
                      rc, strerror(rc));
        return -1;
    }
    pthread_condattr_destroy(&condAttr);

    /* config DSE must be initialized before we get here */
    slapi_config_register_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                   CONFIG_FILTER, replica_config_add, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                   CONFIG_FILTER, replica_config_modify, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                   CONFIG_FILTER, dont_allow_that, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                   CONFIG_FILTER, replica_config_delete, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                   CONFIG_FILTER, replica_config_search, NULL);
    slapi_config_register_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_POSTOP,
                                   CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                   CONFIG_FILTER, replica_config_post_modify,
                                   NULL);

    /* register the CLEANALLRUV & ABORT task */
    slapi_task_register_handler("cleanallruv", replica_cleanall_ruv_task);
    slapi_task_register_handler("abort cleanallruv", replica_cleanall_ruv_abort);

    /* register the csngen_test task
     *
     * To start the test, create a task
     * dn: cn=run the test,cn=csngen_test,cn=tasks,cn=config
     * objectclass: top
     * objectclass: extensibleobject
     * cn: run the test
     * ttl: 300
     */
    slapi_task_register_handler("csngen_test", replica_csngen_test_task);

    return 0;
}

void
replica_config_destroy()
{
    if (s_configLock) {
        PR_DestroyLock(s_configLock);
        s_configLock = NULL;
    }

    /* config DSE must be initialized before we get here */
    slapi_config_remove_callback(SLAPI_OPERATION_ADD, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                 CONFIG_FILTER, replica_config_add);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                 CONFIG_FILTER, replica_config_modify);
    slapi_config_remove_callback(SLAPI_OPERATION_MODRDN, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                 CONFIG_FILTER, dont_allow_that);
    slapi_config_remove_callback(SLAPI_OPERATION_DELETE, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                 CONFIG_FILTER, replica_config_delete);
    slapi_config_remove_callback(SLAPI_OPERATION_SEARCH, DSE_FLAG_PREOP, CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                 CONFIG_FILTER, replica_config_search);
    slapi_config_remove_callback(SLAPI_OPERATION_MODIFY, DSE_FLAG_PREOP,
                                 CONFIG_BASE, LDAP_SCOPE_SUBTREE,
                                 CONFIG_FILTER, replica_config_post_modify);
}

#define MSG_NOREPLICARDN "no replica rdn\n"
#define MSG_NOREPLICANORMRDN  "no replica normalized rdn\n"
#define MSG_CNREPLICA "replica rdn %s should be %s\n"
#define MSG_ALREADYCONFIGURED "replica already configured for %s\n"

static int
replica_config_add(Slapi_PBlock *pb __attribute__((unused)),
                   Slapi_Entry *e,
                   Slapi_Entry *entryAfter __attribute__((unused)),
                   int *returncode,
                   char *errorbuf,
                   void *arg __attribute__((unused)))
{
    Replica *r = NULL;
    multimaster_mtnode_extension *mtnode_ext;
    char *replica_root = (char *)slapi_entry_attr_get_ref(e, attr_replicaRoot);
    char *errortext = NULL;
    Slapi_RDN *replicardn;

    if (errorbuf != NULL) {
        errortext = errorbuf;
    }

    *returncode = LDAP_SUCCESS;

    /* check rdn is "cn=replica" */
    replicardn = slapi_rdn_new_sdn(slapi_entry_get_sdn(e));
    if (replicardn) {
          const char *nrdn = slapi_rdn_get_nrdn(replicardn);
          if (nrdn == NULL) {
              if (errortext != NULL) {
                 strcpy(errortext, MSG_NOREPLICANORMRDN);
              }
              slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_add - "MSG_NOREPLICANORMRDN);
              slapi_rdn_free(&replicardn);
              *returncode = LDAP_UNWILLING_TO_PERFORM;
              return SLAPI_DSE_CALLBACK_ERROR;
          } else {
             if (strcmp(nrdn,REPLICA_RDN)!=0) {
                 if (errortext != NULL) {
                     PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE,MSG_CNREPLICA, nrdn, REPLICA_RDN);
                 }
                 slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,"replica_config_add - "MSG_CNREPLICA, nrdn, REPLICA_RDN);
                 slapi_rdn_free(&replicardn);
                 *returncode = LDAP_UNWILLING_TO_PERFORM;
                 return SLAPI_DSE_CALLBACK_ERROR;
             }
             slapi_rdn_free(&replicardn);
          }
    } else {
        if (errortext != NULL) {
            strcpy(errortext, MSG_NOREPLICARDN);
        }
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_add - "MSG_NOREPLICARDN);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    PR_Lock(s_configLock);

    /* add the dn to the dn hash so we can tell this replica is being configured */
    replica_add_by_dn(replica_root);

    mtnode_ext = _replica_config_get_mtnode_ext(e);
    PR_ASSERT(mtnode_ext);

    if (mtnode_ext->replica) {
        if ( errortext != NULL ) {
            PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, MSG_ALREADYCONFIGURED, replica_root);
        }
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_add - "MSG_ALREADYCONFIGURED, replica_root);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        goto done;
    }

    /* create replica object */
    *returncode = replica_new_from_entry(e, errortext, PR_TRUE /* is a newly added entry */, &r);
    if (r == NULL) {
        goto done;
    }

    /* Set the mapping tree node state, and the referrals from the RUV */
    consumer5_set_mapping_tree_state_for_replica(r, NULL);

    /* ONREPL if replica is added as writable we need to execute protocol that
       introduces new writable replica to the topology */

    mtnode_ext->replica = object_new(r, replica_destroy); /* Refcnt is 1 */

    /* add replica object to the hash */
    *returncode = replica_add_by_name(replica_get_name(r), r); /* Increments object refcnt */
    /* delete the dn from the dn hash - done with configuration */
    replica_delete_by_dn(replica_root);

done:

    PR_Unlock(s_configLock);

    if (*returncode != LDAP_SUCCESS) {
        if (mtnode_ext->replica)
            object_release(mtnode_ext->replica);
        return SLAPI_DSE_CALLBACK_ERROR;
    } else
        return SLAPI_DSE_CALLBACK_OK;
}

static int
replica_config_modify(Slapi_PBlock *pb,
                      Slapi_Entry *entryBefore __attribute__((unused)),
                      Slapi_Entry *e,
                      int *returncode,
                      char *returntext,
                      void *arg __attribute__((unused)))
{
    int rc = 0;
    LDAPMod **mods;
    int i, apply_mods;
    multimaster_mtnode_extension *mtnode_ext;
    Replica *r = NULL;
    char *replica_root = NULL;
    char buf[SLAPI_DSE_RETURNTEXT_SIZE];
    char *errortext = returntext ? returntext : buf;
    char *config_attr, *config_attr_value;
    Slapi_Operation *op;
    void *identity;

    if (returntext) {
        returntext[0] = '\0';
    }
    *returncode = LDAP_SUCCESS;

    /* just let internal operations originated from replication plugin to go through */
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &identity);

    if (operation_is_flag_set(op, OP_FLAG_INTERNAL) &&
        (identity == repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION))) {
        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
    }

    replica_root = (char *)slapi_entry_attr_get_ref(e, attr_replicaRoot);

    PR_Lock(s_configLock);

    mtnode_ext = _replica_config_get_mtnode_ext(e);
    PR_ASSERT(mtnode_ext);

    if (mtnode_ext->replica == NULL) {
        PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Replica does not exist for %s", replica_root);
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_modify - %s\n",
                      errortext);
        *returncode = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    r = object_get_data(mtnode_ext->replica);
    PR_ASSERT(r);

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    for (apply_mods = 0; apply_mods <= 1; apply_mods++) {
        /* we only allow the replica ID and type to be modified together e.g.
           if converting a read only replica to a master or vice versa -
           we will need to change both the replica ID and the type at the same
           time - we must disallow changing the replica ID if the type is not
           being changed and vice versa
        */
        char *new_repl_id = NULL;
        char *new_repl_type = NULL;
        /* we also need to handle the change of repl_flags and enable or disable 
         * the changelog
         */
        char *new_repl_flag = NULL;

        if (*returncode != LDAP_SUCCESS)
            break;

        for (i = 0; mods && (mods[i] && (LDAP_SUCCESS == rc)); i++) {
            if (*returncode != LDAP_SUCCESS)
                break;

            config_attr = (char *)mods[i]->mod_type;
            PR_ASSERT(config_attr);

            /* disallow modifications or removal of replica root,
               replica name and replica state attributes */
            if (strcasecmp(config_attr, attr_replicaRoot) == 0 ||
                strcasecmp(config_attr, attr_replicaName) == 0 ||
                strcasecmp(config_attr, attr_state) == 0) {
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Modification of %s attribute is not allowed",
                            config_attr);
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_modify - %s\n",
                              errortext);
            }
            /* this is a request to delete an attribute */
            else if ((mods[i]->mod_op & LDAP_MOD_DELETE) || mods[i]->mod_bvalues == NULL || mods[i]->mod_bvalues[0]->bv_val == NULL) {
                /*
                 *  Where possible/allowed return replica config settings to their
                 *  default values.
                 */
                if (strcasecmp(config_attr, attr_replicaBindDn) == 0) {
                    *returncode = replica_config_change_updatedn(r, mods[i], errortext, apply_mods);
                } else if (strcasecmp(config_attr, attr_replicaBindDnGroup) == 0) {
                    *returncode = replica_config_change_updatedngroup(r, mods[i], errortext, apply_mods);
                } else if (strcasecmp(config_attr, attr_replicaBindDnGroupCheckInterval) == 0) {
                    replica_set_groupdn_checkinterval(r, -1);
                } else if (strcasecmp(config_attr, attr_replicaReferral) == 0) {
                    if (apply_mods) {
                        replica_set_referrals(r, NULL);
                        consumer5_set_mapping_tree_state_for_replica(r, NULL);
                    }
                } else if (strcasecmp(config_attr, type_replicaCleanRUV) == 0 ||
                           strcasecmp(config_attr, type_replicaAbortCleanRUV) == 0) {
                    /*
                     * Nothing to do in this case, allow it, and continue.
                     */
                    continue;
                } else if (strcasecmp(config_attr, type_replicaProtocolTimeout) == 0) {
                    if (apply_mods)
                        replica_set_protocol_timeout(r, DEFAULT_PROTOCOL_TIMEOUT);
                } else if (strcasecmp(config_attr, type_replicaBackoffMin) == 0) {
                    if (apply_mods)
                        replica_set_backoff_min(r, PROTOCOL_BACKOFF_MINIMUM);
                } else if (strcasecmp(config_attr, type_replicaBackoffMax) == 0) {
                    if (apply_mods)
                        replica_set_backoff_max(r, PROTOCOL_BACKOFF_MAXIMUM);
                } else if (strcasecmp(config_attr, type_replicaPrecisePurge) == 0) {
                    if (apply_mods)
                        replica_set_precise_purging(r, 0);
                } else if (strcasecmp(config_attr, type_replicaReleaseTimeout) == 0) {
                    if (apply_mods)
                        replica_set_release_timeout(r, 0);
                } else {
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Deletion of %s attribute is not allowed", config_attr);
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_modify - %s\n", errortext);
                }
            } else /* modify an attribute */
            {
                config_attr_value = (char *)mods[i]->mod_bvalues[0]->bv_val;
                if (NULL == config_attr_value) {
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE, "Attribute %s value is NULL.\n",
                                config_attr);
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_modify - %s\n",
                                  errortext);
                    break;
                }

                if (strcasecmp(config_attr, attr_replicaBindDn) == 0) {
                    *returncode = replica_config_change_updatedn(r, mods[i], errortext, apply_mods);
                } else if (strcasecmp(config_attr, attr_replicaBindDnGroup) == 0) {
                    *returncode = replica_config_change_updatedngroup(r, mods[i], errortext, apply_mods);
                } else if (strcasecmp(config_attr, attr_replicaBindDnGroupCheckInterval) == 0) {
                    int64_t interval = 0;
                    if (repl_config_valid_num(config_attr, config_attr_value, -1, INT_MAX, returncode, errortext, &interval) == 0) {
                        replica_set_groupdn_checkinterval(r, interval);
                    } else {
                        break;
                    }
                } else if (strcasecmp(config_attr, attr_replicaType) == 0) {
                    int64_t rtype;
                    slapi_ch_free_string(&new_repl_type);
                    if (repl_config_valid_num(config_attr, config_attr_value, 1, 3, returncode, errortext, &rtype) == 0) {
                        new_repl_type = slapi_ch_strdup(config_attr_value);
                    } else {
                        break;
                    }
                } else if (strcasecmp(config_attr, attr_replicaId) == 0) {
                    int64_t rid = 0;
                    if (repl_config_valid_num(config_attr, config_attr_value, 1, MAX_REPLICA_ID, returncode, errortext, &rid) == 0) {
                        slapi_ch_free_string(&new_repl_id);
                        new_repl_id = slapi_ch_strdup(config_attr_value);
                    } else {
                        break;
                    }
                } else if (strcasecmp(config_attr, attr_flags) == 0) {
                    int64_t rflags = 0;
                    if (repl_config_valid_num(config_attr, config_attr_value, 0, 1, returncode, errortext, &rflags) == 0) {
                        *returncode = replica_config_change_flags(r, config_attr_value, errortext, apply_mods);
                    } else {
                        break;
                    }
                } else if (strcasecmp(config_attr, TASK_ATTR) == 0) {
                    *returncode = replica_execute_task(r, config_attr_value, errortext, apply_mods);
                } else if (strcasecmp(config_attr, attr_replicaReferral) == 0) {
                    if (apply_mods) {
                        Slapi_Mod smod;
                        Slapi_ValueSet *vs = slapi_valueset_new();
                        slapi_mod_init_byref(&smod, mods[i]);
                        slapi_valueset_set_from_smod(vs, &smod);
                        replica_set_referrals(r, vs);
                        slapi_mod_done(&smod);
                        slapi_valueset_free(vs);
                        consumer5_set_mapping_tree_state_for_replica(r, NULL);
                    }
                } else if (strcasecmp(config_attr, type_replicaPurgeDelay) == 0) {
                    if (apply_mods && config_attr_value[0]) {
                        int64_t delay = 0;
                        if (repl_config_valid_num(config_attr, config_attr_value, -1, INT_MAX, returncode, errortext, &delay) == 0) {
                            replica_set_purge_delay(r, delay);
                        } else {
                            break;
                        }
                    }
                } else if (strcasecmp(config_attr, type_replicaTombstonePurgeInterval) == 0) {
                    if (apply_mods && config_attr_value[0]) {
                        int64_t interval;
                        if (repl_config_valid_num(config_attr, config_attr_value, -1, INT_MAX, returncode, errortext, &interval) == 0) {
                            replica_set_tombstone_reap_interval(r, interval);
                        } else {
                            break;
                        }
                    }
                }
                /* ignore modifiers attributes added by the server */
                else if (slapi_attr_is_last_mod(config_attr)) {
                    *returncode = LDAP_SUCCESS;
                } else if (strcasecmp(config_attr, type_replicaProtocolTimeout) == 0) {
                    if (apply_mods) {
                        int64_t ptimeout = 0;
                        if (repl_config_valid_num(config_attr, config_attr_value, 1, INT_MAX, returncode, errortext, &ptimeout) == 0) {
                            replica_set_protocol_timeout(r, ptimeout);
                        } else {
                            break;
                        }
                    }
                } else if (strcasecmp(config_attr, type_replicaBackoffMin) == 0) {
                    if (apply_mods) {
                        int64_t val = 0;
                        int64_t max;
                        if (repl_config_valid_num(config_attr, config_attr_value, 1, INT_MAX, returncode, errortext, &val) == 0) {
                            max = replica_get_backoff_max(r);
                            if (val > max){
                                *returncode = LDAP_UNWILLING_TO_PERFORM;
                                PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE,
                                            "Attribute %s value (%s) is invalid, must be a number less than the max backoff time (%d).\n",
                                            config_attr, config_attr_value, (int)max);
                                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_modify - %s\n", errortext);
                                break;
                            }
                            replica_set_backoff_min(r, val);
                        } else {
                            break;
                        }
                    }
                } else if (strcasecmp(config_attr, type_replicaBackoffMax) == 0) {
                    if (apply_mods) {
                        int64_t val = 0;
                        int64_t min;
                        if (repl_config_valid_num(config_attr, config_attr_value, 1, INT_MAX, returncode, errortext, &val) == 0) {
                            min = replica_get_backoff_min(r);
                            if (val < min) {
                                *returncode = LDAP_UNWILLING_TO_PERFORM;
                                PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE,
                                            "Attribute %s value (%s) is invalid, must be a number more than the min backoff time (%d).\n",
                                            config_attr, config_attr_value, (int)min);
                                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_modify - %s\n", errortext);
                                break;
                            }
                            replica_set_backoff_max(r, val);
                        } else {
                            break;
                        }
                    }
                } else if (strcasecmp(config_attr, type_replicaPrecisePurge) == 0) {
                    if (apply_mods) {
                        if (config_attr_value[0]) {
                            uint64_t on_off = 0;

                            if (strcasecmp(config_attr_value, "on") == 0) {
                                on_off = 1;
                            } else if (strcasecmp(config_attr_value, "off") == 0) {
                                on_off = 0;
                            } else {
                                /* Invalid value */
                                *returncode = LDAP_UNWILLING_TO_PERFORM;
                                PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE,
                                            "Invalid value for %s: %s  Value should be \"on\" or \"off\"\n",
                                            type_replicaPrecisePurge, config_attr_value);
                                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                                              "replica_config_modify - %s:\n", errortext);
                                break;
                            }
                            replica_set_precise_purging(r, on_off);
                        } else {
                            replica_set_precise_purging(r, 0);
                        }
                    }
                } else if (strcasecmp(config_attr, type_replicaReleaseTimeout) == 0) {
                    if (apply_mods) {
                        int64_t val;
                        if (repl_config_valid_num(config_attr, config_attr_value, 0, INT_MAX, returncode, errortext, &val) == 0) {
                            replica_set_release_timeout(r, val);
                        } else {
                            break;
                        }
                    }
                } else {
                    *returncode = LDAP_UNWILLING_TO_PERFORM;
                    PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE,
                                "Modification of attribute %s is not allowed in replica entry", config_attr);
                    slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_modify - %s\n", errortext);
                }
            }
        }

        if (new_repl_id || new_repl_type) {
            *returncode = replica_config_change_type_and_id(r, new_repl_type, new_repl_id, errortext, apply_mods);
            PR_Unlock(s_configLock);
            replica_update_state(0, (void *)replica_get_name(r));
            PR_Lock(s_configLock);
            slapi_ch_free_string(&new_repl_id);
            slapi_ch_free_string(&new_repl_type);
            agmtlist_notify_all(pb);
        }
        if (new_repl_flag) {
            *returncode = replica_config_change_flags(r, new_repl_flag, errortext, apply_mods);
            slapi_ch_free_string(&new_repl_flag);
        }
    }

done:

    PR_Unlock(s_configLock);

    if (*returncode != LDAP_SUCCESS) {
        return SLAPI_DSE_CALLBACK_ERROR;
    } else {
        return SLAPI_DSE_CALLBACK_OK;
    }
}

static int
replica_config_post_modify(Slapi_PBlock *pb,
                           Slapi_Entry *entryBefore __attribute__((unused)),
                           Slapi_Entry *e,
                           int *returncode,
                           char *returntext,
                           void *arg __attribute__((unused)))
{
    int rc = 0;
    LDAPMod **mods;
    int i, apply_mods;
    multimaster_mtnode_extension *mtnode_ext;
    char *replica_root = NULL;
    char buf[SLAPI_DSE_RETURNTEXT_SIZE];
    char *errortext = returntext ? returntext : buf;
    char *config_attr, *config_attr_value;
    Slapi_Operation *op;
    void *identity;
    int flag_need_cleanup = 0;

    if (returntext) {
        returntext[0] = '\0';
    }
    *returncode = LDAP_SUCCESS;

    /* just let internal operations originated from replication plugin to go through */
    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &identity);

    if (operation_is_flag_set(op, OP_FLAG_INTERNAL) &&
        (identity == repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION))) {
        *returncode = LDAP_SUCCESS;
        return SLAPI_DSE_CALLBACK_OK;
    }

    replica_root = (char *)slapi_entry_attr_get_ref(e, attr_replicaRoot);

    PR_Lock(s_configLock);

    mtnode_ext = _replica_config_get_mtnode_ext(e);
    PR_ASSERT(mtnode_ext);

    if (mtnode_ext->replica == NULL) {
        PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Replica does not exist for %s", replica_root);
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_config_post_modify - %s\n",
                      errortext);
        *returncode = LDAP_OPERATIONS_ERROR;
        goto done;
    }

    PR_ASSERT(object_get_data(mtnode_ext->replica) != NULL);

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    for (apply_mods = 0; apply_mods <= 1; apply_mods++) {
        /* we only allow the replica ID and type to be modified together e.g.
           if converting a read only replica to a master or vice versa -
           we will need to change both the replica ID and the type at the same
           time - we must disallow changing the replica ID if the type is not
           being changed and vice versa
        */
        if (*returncode != LDAP_SUCCESS)
            break;

        for (i = 0; mods && (mods[i] && (LDAP_SUCCESS == rc)); i++) {
            if (*returncode != LDAP_SUCCESS)
                break;

            config_attr = (char *)mods[i]->mod_type;
            PR_ASSERT(config_attr);

            /* disallow modifications or removal of replica root,
               replica name and replica state attributes */
            if (strcasecmp(config_attr, attr_replicaRoot) == 0 ||
                strcasecmp(config_attr, attr_replicaName) == 0 ||
                strcasecmp(config_attr, attr_state) == 0) {
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                PR_snprintf(errortext, SLAPI_DSE_RETURNTEXT_SIZE,
                            "Modification of %s attribute is not allowed",
                            config_attr);
                slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                              "replica_config_post_modify - %s\n",
                              errortext);
            }
            /* this is a request to delete an attribute */
            else if ((mods[i]->mod_op & LDAP_MOD_DELETE) ||
                     mods[i]->mod_bvalues == NULL ||
                     mods[i]->mod_bvalues[0]->bv_val == NULL) {
                ;
            } else /* modify an attribute */
            {
                config_attr_value = (char *)mods[i]->mod_bvalues[0]->bv_val;

                if (strcasecmp(config_attr, TASK_ATTR) == 0) {
                    flag_need_cleanup = 1;
                }
            }
        }
    }

done:
    PR_Unlock(s_configLock);

    /* Call replica_cleanup_task after s_configLock is reliesed */
    if (flag_need_cleanup) {
        *returncode = replica_cleanup_task(mtnode_ext->replica,
                                           config_attr_value,
                                           errortext, apply_mods);
    }

    if (*returncode != LDAP_SUCCESS) {
        return SLAPI_DSE_CALLBACK_ERROR;
    } else {
        return SLAPI_DSE_CALLBACK_OK;
    }
}

static int
replica_config_delete(Slapi_PBlock *pb __attribute__((unused)),
                      Slapi_Entry *e,
                      Slapi_Entry *entryAfter __attribute__((unused)),
                      int *returncode,
                      char *returntext __attribute__((unused)),
                      void *arg __attribute__((unused)))
{
    multimaster_mtnode_extension *mtnode_ext;
    Replica *r;
    int rc;
    Slapi_Backend *be;

    PR_Lock(s_configLock);

    mtnode_ext = _replica_config_get_mtnode_ext(e);
    PR_ASSERT(mtnode_ext);

    if (mtnode_ext->replica) {
        /* remove object from the hash */
        Object *r_obj = mtnode_ext->replica;
        back_info_config_entry config_entry = {0};

        r = (Replica *)object_get_data(mtnode_ext->replica);

        /* retrieves changelog DN and keep it in config_entry.ce */
        be = slapi_be_select(replica_get_root(r));
        config_entry.dn = "cn=changelog";
        rc = slapi_back_ctrl_info(be, BACK_INFO_CLDB_GET_CONFIG, (void *)&config_entry);
        if (rc !=0 || config_entry.ce == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name_cl,
                          "replica_config_delete - failed to read config for changelog\n");
            PR_Unlock(s_configLock);
            *returncode = LDAP_OPERATIONS_ERROR;
            return SLAPI_DSE_CALLBACK_ERROR;
        }
        mtnode_ext->replica = NULL;  /* moving it before deleting the CL because
                                      * deletion can take some time giving the opportunity
                                      * to an operation to start while CL is deleted
                                      */
        PR_ASSERT(r);
        /* The changelog for this replica is no longer valid, so we should remove it. */
        slapi_log_err(SLAPI_LOG_WARNING, repl_plugin_name, "replica_config_delete - "
                                                           "The changelog for replica %s is no longer valid since "
                                                           "the replica config is being deleted.  Removing the changelog.\n",
                      slapi_sdn_get_dn(replica_get_root(r)));

        /* As we are removing a replica, all references to it need to be cleared
         * There are many of them:
         * - all replicas are stored in a hash table (s_hash): replicat_delete_by_name
         * - callbacks have been registered with the replica as argument: changelog5_register_config_callbacks
         * - replica is stored in mapping tree extension mtnode_ext: object_release
         */
        cldb_RemoveReplicaDB(r);
        replica_delete_by_name(replica_get_name(r));
        changelog5_remove_config_callbacks(slapi_entry_get_dn_const(config_entry.ce));
        slapi_entry_free(config_entry.ce);
        object_release(r_obj);
    }

    PR_Unlock(s_configLock);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}

static void
replica_config_search_last_modified(Slapi_PBlock *pb __attribute__((unused)), Slapi_Entry *e, Replica *replica)
{
    Object *ruv_obj = NULL;
    RUV *ruv = NULL;
    Slapi_Value **values;

    if (replica == NULL)
        return;
    ruv_obj = replica_get_ruv(replica);
    ruv = object_get_data(ruv_obj);

    if ((values = ruv_last_modified_to_valuearray(ruv)) != NULL) {
        slapi_entry_add_values_sv(e, type_ruvElementUpdatetime, values);
        valuearray_free(&values);
    }

    object_release(ruv_obj);
}

static void
replica_config_search_ruv(Slapi_PBlock *pb __attribute__((unused)), Slapi_Entry *e, Replica *replica)
{
    Object *ruv_obj = NULL;
    RUV *ruv = NULL;
    Slapi_Value **values;

    if (replica == NULL)
        return;
    ruv_obj = replica_get_ruv(replica);
    ruv = object_get_data(ruv_obj);

    if ((values = ruv_to_valuearray(ruv)) != NULL) {
        slapi_entry_add_values_sv(e, type_ruvElement, values);
        valuearray_free(&values);
    }
    object_release(ruv_obj);

    /* now add all the repl agmts maxcsnsruv */
    add_agmt_maxcsns(e, replica);
}

/* Returns PR_TRUE if 'attr' is present in the search requested attributes
 * else it returns PR_FALSE
 */
static PRBool
search_requested_attr(Slapi_PBlock *pb, const char *attr)
{
    char **attrs = NULL;
    int i;

    slapi_pblock_get(pb, SLAPI_SEARCH_ATTRS, &attrs);
    if ((attr == NULL) || (attrs == NULL))
        return PR_FALSE;

    for (i = 0; attrs[i] != NULL; i++) {
        if (strcasecmp(attrs[i], attr) == 0) {
            return PR_TRUE;
        }
    }

    return PR_FALSE;
}

static int
replica_config_search(Slapi_PBlock *pb,
                      Slapi_Entry *e,
                      Slapi_Entry *entryAfter __attribute__((unused)),
                      int *returncode __attribute__((unused)),
                      char *returntext __attribute__((unused)),
                      void *arg __attribute__((unused)))
{
    multimaster_mtnode_extension *mtnode_ext;
    int changeCount = 0;
    PRBool reapActive = PR_FALSE;
    char val[64];

    /* add attribute that contains number of entries in the changelog for this replica */

    PR_Lock(s_configLock);

    mtnode_ext = _replica_config_get_mtnode_ext(e);
    PR_ASSERT(mtnode_ext);

    if (mtnode_ext->replica) {
        Replica *replica = (Replica *)object_get_data(mtnode_ext->replica);
        if (cldb_is_open(replica)) {
            changeCount = cl5GetOperationCount(replica);
        }
        if (replica) {
            reapActive = replica_get_tombstone_reap_active(replica);
        }
        /* Check if the in memory ruv is requested */
        if (search_requested_attr(pb, type_ruvElement)) {
            replica_config_search_ruv(pb, e, replica);
        }
        /* Check if the last update time is requested */
        if (search_requested_attr(pb, type_ruvElementUpdatetime)) {
            replica_config_search_last_modified(pb, e, replica);
        }
    }
    sprintf(val, "%d", changeCount);
    slapi_entry_add_string(e, type_replicaChangeCount, val);
    slapi_entry_attr_set_int(e, "nsds5replicaReapActive", (int)reapActive);

    PR_Unlock(s_configLock);

    return SLAPI_DSE_CALLBACK_OK;
}

static int
replica_config_change_type_and_id(Replica *r, const char *new_type, const char *new_id, char *returntext, int apply_mods)
{
    int type = REPLICA_TYPE_READONLY; /* by default - replica is read-only */
    ReplicaType oldtype;
    ReplicaId rid;
    ReplicaId oldrid;

    PR_ASSERT(r);

    oldtype = replica_get_type(r);
    oldrid = replica_get_rid(r);
    if (new_type == NULL) {
        if (oldtype) {
            type = oldtype;
        }
    } else {
        type = atoi(new_type);
        if (type <= REPLICA_TYPE_UNKNOWN || type >= REPLICA_TYPE_END) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "invalid replica type %d", type);
            return LDAP_OPERATIONS_ERROR;
        }
        /* disallow changing type to itself just to permit a replica ID change */
        if (oldtype == type) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "replica type is already %d - not changing", type);
            return LDAP_OPERATIONS_ERROR;
        }
    }

    if (type == REPLICA_TYPE_READONLY) {
        rid = READ_ONLY_REPLICA_ID; /* default rid for read only */
    } else if (!new_id) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "a replica ID is required when changing replica type to read-write");
        return LDAP_UNWILLING_TO_PERFORM;
    } else {
        int temprid = atoi(new_id);
        if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                        "attribute %s must have a value greater than 0 "
                        "and less than %d",
                        attr_replicaId, READ_ONLY_REPLICA_ID);
            return LDAP_UNWILLING_TO_PERFORM;
        } else {
            rid = (ReplicaId)temprid;
        }
    }

    /* error if old rid == new rid */
    if (oldrid == rid) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "replica ID is already %d - not changing", rid);
        return LDAP_OPERATIONS_ERROR;
    }

    if (apply_mods) {
        Object *ruv_obj, *gen_obj;
        RUV *ruv;
        CSNGen *gen;

        ruv_obj = replica_get_ruv(r);
        if (ruv_obj) {
            /* we need to rewrite the repl_csngen with the new rid */
            ruv = object_get_data(ruv_obj);
            gen_obj = replica_get_csngen(r);
            if (gen_obj) {
                const char *purl = multimaster_get_local_purl();

                gen = (CSNGen *)object_get_data(gen_obj);
                csngen_rewrite_rid(gen, rid);
                if (purl && type == REPLICA_TYPE_UPDATABLE) {
                    ruv_add_replica(ruv, rid, purl);
                    ruv_move_local_supplier_to_first(ruv, rid);
                    replica_reset_csn_pl(r);
                }
                ruv_delete_replica(ruv, oldrid);
                cl5CleanRUV(oldrid, r);
                replica_set_csn_assigned(r);
            }
            object_release(ruv_obj);
        }
        replica_set_type(r, type);
        replica_set_rid(r, rid);

        /* Set the mapping tree node, and the list of referrals */
        consumer5_set_mapping_tree_state_for_replica(r, NULL);
    }

    return LDAP_SUCCESS;
}

static int
replica_config_change_updatedn(Replica *r, const LDAPMod *mod, char *returntext __attribute__((unused)), int apply_mods)
{
    PR_ASSERT(r);

    if (apply_mods) {
        Slapi_Mod smod;
        Slapi_ValueSet *vs = slapi_valueset_new();
        slapi_mod_init_byref(&smod, (LDAPMod *)mod); /* cast away const */
        slapi_valueset_set_from_smod(vs, &smod);
        replica_set_updatedn(r, vs, mod->mod_op);
        slapi_mod_done(&smod);
        slapi_valueset_free(vs);
    }

    return LDAP_SUCCESS;
}

static int
replica_config_change_updatedngroup(Replica *r, const LDAPMod *mod, char *returntext __attribute__((unused)), int apply_mods)
{
    PR_ASSERT(r);

    if (apply_mods) {
        Slapi_Mod smod;
        Slapi_ValueSet *vs = slapi_valueset_new();
        slapi_mod_init_byref(&smod, (LDAPMod *)mod); /* cast away const */
        slapi_valueset_set_from_smod(vs, &smod);
        replica_set_groupdn(r, vs, mod->mod_op);
        slapi_mod_done(&smod);
        slapi_valueset_free(vs);
    }

    return LDAP_SUCCESS;
}

static int
replica_config_change_flags(Replica *r, const char *new_flags, char *returntext __attribute__((unused)), int apply_mods)
{
    PR_ASSERT(r);

    if (apply_mods) {
        uint32_t flags;

        flags = atol(new_flags);

        if (replica_is_flag_set(r, REPLICA_LOG_CHANGES) && !(flags & REPLICA_LOG_CHANGES)) {
            /* the replica no longer maintains a changelog, reset */
            int32_t write_ruv = 1;
            cldb_UnSetReplicaDB(r, (void *)&write_ruv);
        } else if (!replica_is_flag_set(r, REPLICA_LOG_CHANGES) && (flags & REPLICA_LOG_CHANGES)) {
            /* the replica starts to maintains a changelog, set */
            cldb_SetReplicaDB(r, NULL);
        }
        replica_replace_flags(r, flags);
    }

    return LDAP_SUCCESS;
}

static int
replica_execute_task(Replica *r, const char *task_name, char *returntext, int apply_mods)
{

    if (strcasecmp(task_name, CL2LDIF_TASK) == 0) {
        if (apply_mods) {
            return replica_execute_cl2ldif_task(r, returntext);
        } else
            return LDAP_SUCCESS;
    } else if (strcasecmp(task_name, LDIF2CL_TASK) == 0) {
        if (apply_mods) {
            return replica_execute_ldif2cl_task(r, returntext);
        } else
            return LDAP_SUCCESS;
    } else if (strncasecmp(task_name, CLEANRUV, CLEANRUVLEN) == 0) {
        int temprid = atoi(&(task_name[CLEANRUVLEN]));
        if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid replica id (%d) for task - %s", temprid, task_name);
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_execute_task - %s\n", returntext);
            return LDAP_OPERATIONS_ERROR;
        }
        if (apply_mods) {
            return replica_execute_cleanruv_task(r, (ReplicaId)temprid, returntext);
        } else
            return LDAP_SUCCESS;
    } else if (strncasecmp(task_name, CLEANALLRUV, CLEANALLRUVLEN) == 0) {
        int temprid = atoi(&(task_name[CLEANALLRUVLEN]));
        if (temprid <= 0 || temprid >= READ_ONLY_REPLICA_ID) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid replica id (%d) for task - (%s)", temprid, task_name);
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_execute_task - %s\n", returntext);
            return LDAP_OPERATIONS_ERROR;
        }
        if (apply_mods) {
            Slapi_Task *empty_task = NULL;
            return replica_execute_cleanall_ruv_task(r, (ReplicaId)temprid, empty_task, "no", PR_TRUE, returntext);
        } else
            return LDAP_SUCCESS;
    } else {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Unsupported replica task - %s", task_name);
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_execute_task - %s\n", returntext);
        return LDAP_OPERATIONS_ERROR;
    }
}

static int
replica_cleanup_task(Object *r, const char *task_name __attribute__((unused)), char *returntext __attribute__((unused)), int apply_mods)
{
    int rc = LDAP_SUCCESS;
    if (apply_mods) {
        Replica *replica = (Replica *)object_get_data(r);
        if (NULL == replica) {
            rc = LDAP_OPERATIONS_ERROR;
        } else {
            rc = replica_task_done(replica);
        }
    }
    return rc;
}

static int
replica_task_done(Replica *replica)
{
    int rc = LDAP_OPERATIONS_ERROR;
    char *replica_dn = NULL;
    Slapi_DN *replica_sdn = NULL;
    Slapi_PBlock *pb = NULL;
    LDAPMod *mods[2];
    LDAPMod mod;
    if (NULL == replica) {
        return rc;
    }
    /* dn: cn=replica,cn=dc\3Dexample\2Cdc\3Dcom,cn=mapping tree,cn=config */
    replica_dn = slapi_ch_smprintf("%s,cn=\"%s\",%s",
                                   REPLICA_RDN,
                                   slapi_sdn_get_dn(replica_get_root(replica)),
                                   CONFIG_BASE);
    if (NULL == replica_dn) {
        return rc;
    }
    replica_sdn = slapi_sdn_new_dn_passin(replica_dn);
    pb = slapi_pblock_new();
    mods[0] = &mod;
    mods[1] = NULL;
    mod.mod_op = LDAP_MOD_DELETE | LDAP_MOD_BVALUES;
    mod.mod_type = (char *)TASK_ATTR;
    mod.mod_bvalues = NULL;

    slapi_modify_internal_set_pb_ext(pb, replica_sdn, mods, NULL /* controls */,
                                     NULL /* uniqueid */,
                                     repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
                                     0 /* flags */);
    slapi_modify_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if ((rc != LDAP_SUCCESS) && (rc != LDAP_NO_SUCH_ATTRIBUTE)) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_task_done - "
                      "Failed to remove (%s) attribute from (%s) entry; "
                      "LDAP error - %d\n",
                      TASK_ATTR, replica_dn, rc);
    }

    slapi_pblock_destroy(pb);
    slapi_sdn_free(&replica_sdn);

    return rc;
}

static int
replica_execute_cl2ldif_task(Replica *replica, char *returntext)
{
    int rc;
    char fName[MAXPATHLEN];
    char *clDir = NULL;

    if (!cldb_is_open(replica)) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Changelog is not open");
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_execute_cl2ldif_task - %s\n", returntext);
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    /* file is stored in the changelog directory and is named
       <replica name>.ldif */
    Slapi_Backend *be = slapi_be_select(replica_get_root(replica));
    clDir = cl5GetLdifDir(be);
    if (NULL == clDir) {
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    if (NULL == replica) {
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    PR_snprintf(fName, MAXPATHLEN, "%s/%s_cl.ldif", clDir, replica_get_name(replica));
    slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name,
                  "replica_execute_cl2ldif_task - Beginning changelog export of replica \"%s\"\n",
                  replica_get_name(replica));
    rc = cl5ExportLDIF(fName, replica);
    if (rc == CL5_SUCCESS) {
        slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name,
                      "replica_execute_cl2ldif_task - Finished changelog export of replica \"%s\"\n",
                      replica_get_name(replica));
        rc = LDAP_SUCCESS;
    } else {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Failed changelog export replica %s; "
                    "changelog error - %d",
                    replica_get_name(replica), rc);
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_execute_cl2ldif_task - %s\n", returntext);
        rc = LDAP_OPERATIONS_ERROR;
    }
bail:
    slapi_ch_free_string(&clDir);

    return rc;
}

static int
replica_execute_ldif2cl_task(Replica *replica, char *returntext)
{
    int rc = CL5_SUCCESS;
    char fName[MAXPATHLEN];
    char *clDir = NULL;

    if (!cldb_is_open(replica)) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "changelog is not open");
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_execute_ldif2cl_task - %s\n", returntext);
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    /* file is stored in the changelog directory and is named
       <replica name>.ldif */
    Slapi_Backend *be = slapi_be_select(replica_get_root(replica));
    clDir = cl5GetLdifDir(be);
    if (NULL == clDir) {
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    if (NULL == replica) {
        rc = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    PR_snprintf(fName, MAXPATHLEN, "%s/%s_cl.ldif", clDir, replica_get_name(replica));

    slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name,
                  "replica_execute_ldif2cl_task -  Beginning changelog import of replica \"%s\".  "
                  "All replication updates will be rejected until the import is complete.\n",
                  replica_get_name(replica));

    rc = cl5ImportLDIF(clDir, fName, replica);
    if (CL5_SUCCESS == rc) {
        slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name,
                      "replica_execute_ldif2cl_task - Finished changelog import of replica \"%s\"\n",
                      replica_get_name(replica));
    } else {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Failed changelog import replica %s; dir: %s file: %s - changelog error: %d",
                    replica_get_name(replica), clDir, fName, rc);
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name,
                      "replica_execute_ldif2cl_task - %s\n", returntext);
        rc = LDAP_OPERATIONS_ERROR;
    }

bail:
    slapi_ch_free_string(&clDir);

    /* if cl5ImportLDIF returned an error, report it first. */
    return rc;
}

static multimaster_mtnode_extension *
_replica_config_get_mtnode_ext(const Slapi_Entry *e)
{
    const char *replica_root;
    Slapi_DN *sdn = NULL;
    mapping_tree_node *mtnode;
    multimaster_mtnode_extension *ext = NULL;

    /* retirve root of the tree for which replica is configured */
    replica_root = slapi_entry_attr_get_charptr(e, attr_replicaRoot);
    if (replica_root == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_replica_config_get_mtnode_ext - "
                                                       "Configuration entry %s missing %s attribute\n",
                      slapi_entry_get_dn((Slapi_Entry *)e),
                      attr_replicaRoot);
        return NULL;
    }

    sdn = slapi_sdn_new_dn_passin(replica_root);

    /* locate mapping tree node for the specified subtree */
    mtnode = slapi_get_mapping_tree_node_by_dn(sdn);
    if (mtnode == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "_replica_config_get_mtnode_ext - "
                                                       "Failed to locate mapping tree node for dn %s\n",
                      slapi_sdn_get_dn(sdn));
    } else {
        /* check if replica object already exists for the specified subtree */
        ext = (multimaster_mtnode_extension *)repl_con_get_ext(REPL_CON_EXT_MTNODE, mtnode);
    }

    slapi_sdn_free(&sdn);

    return ext;
}

int
replica_execute_cleanruv_task_ext(Replica *r, ReplicaId rid)
{
    return replica_execute_cleanruv_task(r, rid, NULL);
}

static int
replica_execute_cleanruv_task(Replica *replica, ReplicaId rid, char *returntext __attribute__((unused)))
{
    Object *RUVObj;
    RUV *local_ruv = NULL;
    cleanruv_purge_data *purge_data;
    int rc = 0;
    PR_ASSERT(replica);

    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "cleanAllRUV_task - Cleaning rid (%d)...\n", (int)rid);
    RUVObj = replica_get_ruv(replica);
    PR_ASSERT(RUVObj);
    local_ruv = (RUV *)object_get_data(RUVObj);
    /* Need to check that :
     *  - rid is not the local one
     *  - rid is not the last one
     */
    if ((replica_get_rid(replica) == rid) ||
        (ruv_replica_count(local_ruv) <= 1)) {
        return LDAP_UNWILLING_TO_PERFORM;
    }
    rc = ruv_delete_replica(local_ruv, rid);
    if (replica_write_ruv(replica)) {
        slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "cleanAllRUV_task - Could not write RUV\n");
    }
    object_release(RUVObj);

    /* Update Mapping Tree to reflect RUV changes */
    consumer5_set_mapping_tree_state_for_replica(replica, NULL);

    /*
     *  Clean the changelog RUV's
     */
    cl5CleanRUV(rid, replica);

    /*
     * Now purge the changelog.  The purging thread will free the purge_data
     */
    purge_data = (cleanruv_purge_data *)slapi_ch_calloc(1, sizeof(cleanruv_purge_data));
    purge_data->cleaned_rid = rid;
    purge_data->suffix_sdn = replica_get_root(replica);
    purge_data->replica = replica;
    trigger_cl_purging(purge_data);

    if (rc != RUV_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "cleanAllRUV_task - Task failed(%d)\n", rc);
        return LDAP_OPERATIONS_ERROR;
    }
    slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "cleanAllRUV_task - Finished successfully\n");
    return LDAP_SUCCESS;
}
/* This thread runs the tests of csn generator.
 * It will log a set of csn generated while simulating local and remote time skews
 * All csn should increase
 */
void
replica_csngen_test_thread(void *arg)
{
    csngen_test_data *data = (csngen_test_data *)arg;
    int rc = 0;
    if (data->task) {
        slapi_task_inc_refcount(data->task);
        slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name, "replica_csngen_test_thread --> refcount incremented.\n");
    }

    /* defined in csngen.c */
    csngen_test();

    if (data->task) {
        slapi_task_finish(data->task, rc);
        slapi_task_dec_refcount(data->task);
        slapi_log_err(SLAPI_LOG_INFO, repl_plugin_name, "replica_csngen_test_thread <-- refcount incremented.\n");
    }
}

/* It spawn a thread running the test of a csn generator */
static int
replica_csngen_test_task(Slapi_PBlock *pb __attribute__((unused)),
                          Slapi_Entry *e,
                          Slapi_Entry *eAfter __attribute__((unused)),
                          int *returncode,
                          char *returntext,
                          void *arg __attribute__((unused)))
{
    Slapi_Task *task = NULL;
    csngen_test_data *data;
    PRThread *thread = NULL;
    int rc = SLAPI_DSE_CALLBACK_OK;

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    data = (csngen_test_data *)slapi_ch_calloc(1, sizeof(csngen_test_data));
    data->task = task;

    thread = PR_CreateThread(PR_USER_THREAD, replica_csngen_test_thread,
                             (void *)data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
    }
    if (rc != SLAPI_DSE_CALLBACK_OK) {
        slapi_task_finish(task, rc);
    }
    return rc;
}

static int
replica_cleanall_ruv_task(Slapi_PBlock *pb __attribute__((unused)),
                          Slapi_Entry *e,
                          Slapi_Entry *eAfter __attribute__((unused)),
                          int *returncode,
                          char *returntext,
                          void *arg __attribute__((unused)))
{
    Slapi_Task *task = NULL;
    const Slapi_DN *task_dn;
    Slapi_DN *dn = NULL;
    Replica *replica;
    ReplicaId rid = -1;
    const char *force_cleaning;
    const char *base_dn;
    const char *rid_str;
    const char *orig_val = NULL;
    PRBool original_task = PR_TRUE;
    int rc = SLAPI_DSE_CALLBACK_OK;

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));
    task_dn = slapi_entry_get_sdn(e);
    if (task == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "cleanAllRUV_task - Failed to create new task\n");
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* register our destructor for waiting the task is done */
    slapi_task_set_destructor_fn(task, replica_cleanall_ruv_destructor);

    /*
     *  Get our task settings
     */
    if ((rid_str = slapi_entry_attr_get_ref(e, "replica-id")) == NULL) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Missing replica-id attribute");
        cleanruv_log(task, -1, CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    rid = atoi(rid_str);
    if ((base_dn = slapi_entry_attr_get_ref(e, "replica-base-dn")) == NULL) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Missing replica-base-dn attribute");
        cleanruv_log(task, (int)rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if ((force_cleaning = slapi_entry_attr_get_ref(e, "replica-force-cleaning")) != NULL) {
        if (strcasecmp(force_cleaning, "yes") != 0 && strcasecmp(force_cleaning, "no") != 0) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid value for replica-force-cleaning "
                                                               "(%s).  Value must be \"yes\" or \"no\" for task - (%s)",
                        force_cleaning, slapi_sdn_get_dn(task_dn));
            cleanruv_log(task, (int)rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
            *returncode = LDAP_OPERATIONS_ERROR;
            rc = SLAPI_DSE_CALLBACK_ERROR;
            goto out;
        }
    } else {
        force_cleaning = "no";
    }
    if ((orig_val = slapi_entry_attr_get_ref(e, "replica-original-task")) != NULL) {
        if (!strcasecmp(orig_val, "0")) {
            original_task = PR_FALSE;
        }
    }
    /*
     *  Check the rid
     */
    if (rid <= 0 || rid >= READ_ONLY_REPLICA_ID) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid replica id (%d) for task - (%s)",
                    rid, slapi_sdn_get_dn(task_dn));
        cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if (is_cleaned_rid(rid)) {
        /* we are already cleaning this rid */
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Replica id (%d) is already being cleaned", rid);
        cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /*
     *  Get the replica object
     */
    dn = slapi_sdn_new_dn_byval(base_dn);
    if ((replica = replica_get_replica_from_dn(dn)) == NULL) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Could not find replica from dn(%s)", slapi_sdn_get_dn(dn));
        cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    /* clean the RUV's */
    rc = replica_execute_cleanall_ruv_task(replica, rid, task, force_cleaning, original_task, returntext);

out:
    if (rc) {
        cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Task failed...(%d)", rc);
        slapi_task_finish(task, *returncode);
    } else {
        rc = SLAPI_DSE_CALLBACK_OK;
    }
    slapi_sdn_free(&dn);

    return rc;
}

/*
 *  CLEANALLRUV task
 *
 *  [1]  Get the maxcsn from the RUV of the rid we want to clean
 *  [2]  Create the payload for the "cleanallruv" extended ops
 *  [3]  Create "monitor" thread to do the real work.
 *
 */
static int
replica_execute_cleanall_ruv_task(Replica *replica, ReplicaId rid, Slapi_Task *task, const char *force_cleaning, PRBool original_task, char *returntext __attribute__((unused)))
{
    struct berval *payload = NULL;
    Slapi_Task *pre_task = NULL; /* this is supposed to be null for logging */
    cleanruv_data *data = NULL;
    PRThread *thread = NULL;
    CSN *maxcsn = NULL;
    char csnstr[CSN_STRSIZE];
    char *ridstr = NULL;
    char *basedn = NULL;
    int rc = 0;

    cleanruv_log(pre_task, rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Initiating CleanAllRUV Task...");

    /*
     *  Grab the replica
     */
    if (replica == NULL) {
        cleanruv_log(pre_task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Replica object is NULL, aborting task");
        return -1;
    }
    /*
     *  Check if this is a consumer
     */
    if (replica_get_type(replica) == REPLICA_TYPE_READONLY) {
        /* this is a consumer, send error */
        cleanruv_log(pre_task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Failed to clean rid (%d), task can not be run on a consumer", rid);
        if (task) {
            rc = -1;
            slapi_task_finish(task, rc);
        }
        return -1;
    }
    /*
     *  Grab the max csn of the deleted replica
     */
    cleanruv_log(pre_task, rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Retrieving maxcsn...");
    basedn = (char *)slapi_sdn_get_dn(replica_get_root(replica));
    maxcsn = replica_cleanallruv_find_maxcsn(replica, rid, basedn);
    if (maxcsn == NULL || csn_get_replicaid(maxcsn) == 0) {
        /*
         *  This is for consistency with extop csn creation, where
         *  we want the csn string to be "0000000000000000000" not ""
         */
        csn_free(&maxcsn);
        maxcsn = csn_new();
        csn_init_by_string(maxcsn, "");
    }
    csn_as_string(maxcsn, PR_FALSE, csnstr);
    cleanruv_log(pre_task, rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Found maxcsn (%s)", csnstr);
    /*
     *  Create payload
     */
    ridstr = slapi_ch_smprintf("%d:%s:%s:%s", rid, basedn, csnstr, force_cleaning);
    payload = create_cleanruv_payload(ridstr);
    slapi_ch_free_string(&ridstr);

    if (payload == NULL) {
        cleanruv_log(pre_task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Failed to create extended op payload, aborting task");
        rc = -1;
        goto fail;
    }

    if (check_and_set_cleanruv_task_count(rid) != LDAP_SUCCESS) {
        cleanruv_log(NULL, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR,
                     "Exceeded maximum number of active CLEANALLRUV tasks(%d)", CLEANRIDSIZ);
        rc = LDAP_UNWILLING_TO_PERFORM;
        goto fail;
    }

    /*
     *  Launch the cleanallruv thread.  Once all the replicas are cleaned it will release the rid
     */
    data = (cleanruv_data *)slapi_ch_calloc(1, sizeof(cleanruv_data));
    if (data == NULL) {
        cleanruv_log(pre_task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Failed to allocate cleanruv_data.  Aborting task.");
        rc = -1;
        PR_Lock(task_count_lock);
        clean_task_count--;
        PR_Unlock(task_count_lock);
        goto fail;
    }
    data->replica = replica;
    data->rid = rid;
    data->task = task;
    data->payload = payload;
    data->sdn = NULL;
    data->maxcsn = maxcsn;
    data->repl_root = slapi_ch_strdup(basedn);
    data->force = slapi_ch_strdup(force_cleaning);

    /* It is either a consequence of a direct ADD cleanAllRuv task
     * or modify of the replica to add nsds5task: cleanAllRuv
     */
    data->original_task = original_task;

    thread = PR_CreateThread(PR_USER_THREAD, replica_cleanallruv_thread,
                             (void *)data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        rc = -1;
        slapi_ch_free_string(&data->force);
        slapi_ch_free_string(&data->repl_root);
        goto fail;
    } else {
        goto done;
    }

fail:
    cleanruv_log(pre_task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Failed to clean rid (%d)", rid);
    if (task) {
        slapi_task_finish(task, rc);
    }
    if (payload) {
        ber_bvfree(payload);
    }
    csn_free(&maxcsn);

done:

    return rc;
}

void
replica_cleanallruv_thread_ext(void *arg)
{
    replica_cleanallruv_thread(arg);
}

/*
 *  CLEANALLRUV Thread
 *
 *  [1]  Wait for the maxcsn to be covered
 *  [2]  Make sure all the replicas are alive
 *  [3]  Set the cleaned rid
 *  [4]  Send the cleanAllRUV extop to all the replicas
 *  [5]  Manually send the CLEANRUV task to replicas that do not support CLEANALLRUV
 *  [6]  Wait for all the replicas to be cleaned.
 *  [7]  Trigger cl trimming, release the rid, and remove all the "cleanallruv" attributes
 *       from the config.
 */
static void
replica_cleanallruv_thread(void *arg)
{
    cleanruv_data *data = arg;
    Object *agmt_obj = NULL;
    Object *ruv_obj = NULL;
    Repl_Agmt *agmt = NULL;
    RUV *ruv = NULL;
    char csnstr[CSN_STRSIZE];
    char *returntext = NULL;
    char *rid_text = NULL;
    int agmt_not_notified = 1;
    int found_dirty_rid = 1;
    int interval = 10;
    int aborted = 0;
    int rc = 0;

    /* Increase active thread count to prevent a race condition at server shutdown */
    g_incr_active_threadcnt();

    if (!data || slapi_is_shutting_down()) {
        goto done;
    }

    if (data->task) {
        slapi_task_inc_refcount(data->task);
        slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                      "replica_cleanallruv_thread --> refcount incremented (%d).\n",
                      data->task->task_refcount);
    }
    /*
     *  Initialize our settings
     */
    if (data->replica == NULL) {
        /*
         * This thread was initiated at startup because the process did not finish.  Due
         * to startup timing issues, we need to wait before grabbing the replica obj, as
         * the backends might not be online yet.
         */
        struct timespec current_time = {0};
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        current_time.tv_sec += 10;

        pthread_mutex_lock(&notify_lock);
        pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
        pthread_mutex_unlock(&notify_lock);
        data->replica = replica_get_replica_from_dn(data->sdn);
        if (data->replica == NULL) {
            cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Unable to retrieve repl object from dn(%s).", data->sdn);
            aborted = 1;
            goto done;
        }
    }
    /* verify we have set our repl objects */
    if (data->replica == NULL) {
        cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Unable to set the replica objects.");
        aborted = 1;
        goto done;
    }
    if (data->repl_root == NULL) {
        /* we must have resumed from start up, fill in the repl root */
        data->repl_root = slapi_ch_strdup(slapi_sdn_get_dn(replica_get_root(data->replica)));
    }
    if (data->task) {
        slapi_task_begin(data->task, 1);
    }
    /*
     *  We have already preset this rid, but if we are forcing a clean independent of state
     *  of other servers for this RID we can set_cleaned_rid()
     */
    if (data->force) {
        set_cleaned_rid(data->rid);
    }

    rid_text = slapi_ch_smprintf("%d", data->rid);
    csn_as_string(data->maxcsn, PR_FALSE, csnstr);
    /*
     *  Add the cleanallruv task to the repl config - so we can handle restarts
     */
    add_cleaned_rid(data); /* marks config that we started cleaning a rid */
    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Cleaning rid (%d)...", data->rid);
    /*
     *  First, wait for the maxcsn to be covered
     */
    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO,
                 "Waiting to process all the updates from the deleted replica...");
    ruv_obj = replica_get_ruv(data->replica);
    ruv = object_get_data(ruv_obj);
    while (data->maxcsn && !is_task_aborted(data->rid) && !is_cleaned_rid(data->rid) && !slapi_is_shutting_down()) {
        struct timespec current_time = {0};
        if (csn_get_replicaid(data->maxcsn) == 0 ||
            ruv_covers_csn_cleanallruv(ruv, data->maxcsn) ||
            strcasecmp(data->force, "yes") == 0) {
            /* We are caught up, now we can clean the ruv's */
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        current_time.tv_sec += CLEANALLRUV_SLEEP;
        pthread_mutex_lock(&notify_lock);
        pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
        pthread_mutex_unlock(&notify_lock);
    }
    object_release(ruv_obj);
    /*
     *  Next, make sure all the replicas are up and running before sending off the clean ruv tasks
     *
     *  Even if we are forcing the cleaning, the replicas still need to be up
     */
    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Waiting for all the replicas to be online...");
    if (strcasecmp(data->force, "no") == 0 && check_agmts_are_alive(data->replica, data->rid, data->task)) {
        /* error, aborted or shutdown */
        aborted = 1;
        goto done;
    }
    /*
     *  Make sure all the replicas have seen the max csn
     */
    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Waiting for all the replicas to receive all the deleted replica updates...");
    if (strcasecmp(data->force, "no") == 0 && check_agmts_are_caught_up(data, csnstr)) {
        /* error, aborted or shutdown */
        aborted = 1;
        goto done;
    }
    /*
     *  Set the rid as notified - this blocks the changelog from sending out updates
     *  during this process, as well as prevents the db ruv from getting polluted.
     */
    set_cleaned_rid(data->rid);
    /*
     *  Now send the cleanruv extended op to all the agreements
     */
    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Sending cleanAllRUV task to all the replicas...");
    while (agmt_not_notified && !is_task_aborted(data->rid) && !slapi_is_shutting_down()) {
        agmt_obj = agmtlist_get_first_agreement_for_replica(data->replica);
        if (agmt_obj == NULL) {
            /* no agmts, just clean this replica */
            break;
        }
        while (agmt_obj && !slapi_is_shutting_down()) {
            agmt = (Repl_Agmt *)object_get_data(agmt_obj);
            if (!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
                agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
                agmt_not_notified = 0;
                continue;
            }
            if (replica_cleanallruv_send_extop(agmt, data, 1) == 0) {
                agmt_not_notified = 0;
            } else {
                agmt_not_notified = 1;
                cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, LOG_WARNING, "Failed to send task to replica (%s)", agmt_get_long_name(agmt));
                if (strcasecmp(data->force, "no") == 0) {
                    object_release(agmt_obj);
                    break;
                }
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
        }

        if (is_task_aborted(data->rid)) {
            aborted = 1;
            goto done;
        }
        if (agmt_not_notified == 0 || strcasecmp(data->force, "yes") == 0) {
            break;
        }
        /*
         *  need to sleep between passes
         */
        cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                     "Not all replicas have received the cleanallruv extended op, retrying in %d seconds",
                     interval);
        if (!slapi_is_shutting_down()) {
            struct timespec current_time = {0};
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += interval;
            pthread_mutex_lock(&notify_lock);
            pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
            pthread_mutex_unlock(&notify_lock);
        }
        interval *= 2;
        if (interval >= CLEANALLRUV_MAX_WAIT) {
            interval = CLEANALLRUV_MAX_WAIT;
        }
    }
    /*
     *  Run the CLEANRUV task
     */
    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Cleaning local ruv's...");
    replica_execute_cleanruv_task(data->replica, data->rid, returntext);
    /*
     *  Wait for all the replicas to be cleaned
     */
    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO,
                 "Waiting for all the replicas to be cleaned...");

    interval = 10;
    while (found_dirty_rid && !is_task_aborted(data->rid) && !slapi_is_shutting_down()) {
        agmt_obj = agmtlist_get_first_agreement_for_replica(data->replica);
        if (agmt_obj == NULL) {
            break;
        }
        while (agmt_obj && !slapi_is_shutting_down()) {
            agmt = (Repl_Agmt *)object_get_data(agmt_obj);
            if (!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
                agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
                found_dirty_rid = 0;
                continue;
            }
            if (replica_cleanallruv_check_ruv(data->repl_root, agmt, rid_text, data->task, data->force) == 0) {
                found_dirty_rid = 0;
            } else {
                found_dirty_rid = 1;
                cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE, "Replica is not cleaned yet (%s)",
                             agmt_get_long_name(agmt));
                object_release(agmt_obj);
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
        }
        /* If the task is abort or everyone is cleaned, break out */
        if (is_task_aborted(data->rid)) {
            aborted = 1;
            goto done;
        }
        if (found_dirty_rid == 0 || strcasecmp(data->force, "yes") == 0) {
            break;
        }
        /*
         * Need to sleep between passes unless we are shutting down
         */
        if (!slapi_is_shutting_down()) {
            struct timespec current_time = {0};
            cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                         "Replicas have not been cleaned yet, retrying in %d seconds",
                         interval);
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += interval;
            pthread_mutex_lock(&notify_lock);
            pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
            pthread_mutex_unlock(&notify_lock);
        }
        interval *= 2;
        if (interval >= CLEANALLRUV_MAX_WAIT) {
            interval = CLEANALLRUV_MAX_WAIT;
        }
    } /* while */

done:
    /*
     *  If the replicas are cleaned, release the rid
     */
    if (slapi_is_shutting_down()) {
        stop_ruv_cleaning();
    }
    if (!aborted && !slapi_is_shutting_down()) {
        /*
         * Success - the rid has been cleaned!
         *
         * Delete the cleaned rid config.
         * Make sure all the replicas have been "pre_cleaned"
         * Remove the keep alive entry if present
         * Clean the agreements' RUV
         * Remove the rid from the internal clean list
         */
        delete_cleaned_rid_config(data);
        check_replicas_are_done_cleaning(data);
        if (data->original_task) {
            cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Original task deletes Keep alive entry (%d).", data->rid);
            remove_keep_alive_entry(data->task, data->rid, data->repl_root);
        } else {
            cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Propagated task does not delete Keep alive entry (%d).", data->rid);
        }
        clean_agmts(data);
        remove_cleaned_rid(data->rid);
        cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Successfully cleaned rid(%d)", data->rid);
    } else {
        /*
         *  Shutdown or abort
         */
        if (!is_task_aborted(data->rid) || slapi_is_shutting_down()) {
            cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                         "Server shutting down.  Process will resume at server startup");
        } else {
            cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE, "Task aborted for rid(%d).", data->rid);
            delete_cleaned_rid_config(data);
            remove_cleaned_rid(data->rid);
        }
    }
    if (data->task) {
        slapi_task_finish(data->task, rc);
        slapi_task_dec_refcount(data->task);
        slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                      "replica_cleanallruv_thread <-- refcount decremented.\n");
    }
    if (data->payload) {
        ber_bvfree(data->payload);
    }

    csn_free(&data->maxcsn);
    slapi_sdn_free(&data->sdn);
    slapi_ch_free_string(&data->repl_root);
    slapi_ch_free_string(&data->force);
    slapi_ch_free_string(&rid_text);
    slapi_ch_free((void **)&data);
    /* decrement task count */
    PR_Lock(task_count_lock);
    clean_task_count--;
    PR_Unlock(task_count_lock);
    g_decr_active_threadcnt();
}

/*
 * Clean the RUV attributes from all the agreements
 */
static void
clean_agmts(cleanruv_data *data)
{
    Object *agmt_obj = NULL;
    Repl_Agmt *agmt = NULL;

    agmt_obj = agmtlist_get_first_agreement_for_replica(data->replica);
    if (agmt_obj == NULL) {
        return;
    }
    while (agmt_obj && !slapi_is_shutting_down()) {
        agmt = (Repl_Agmt *)object_get_data(agmt_obj);
        if (!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
            agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
            continue;
        }
        cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Cleaning agmt...");
        agmt_stop(agmt);
        agmt_update_consumer_ruv(agmt);
        agmt_start(agmt);
        agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
    }
    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Cleaned replication agreements.");
}

/*
 * Remove the "Keep-Alive" replication entry.
 */
static void
remove_keep_alive_entry(Slapi_Task *task, ReplicaId rid, const char *repl_root)
{
    Slapi_PBlock *delete_pb = NULL;
    char *keep_alive_dn = NULL;
    int rc = 0;

    /* Construct the repl keep alive dn from the rid and replication suffix */
    keep_alive_dn = PR_smprintf("cn=repl keep alive %d,%s", (int)rid, repl_root);

    delete_pb = slapi_pblock_new();
    slapi_delete_internal_set_pb(delete_pb, keep_alive_dn, NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_delete_internal_pb(delete_pb);
    slapi_pblock_get(delete_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc == LDAP_NO_SUCH_OBJECT) {
        /* No problem, it's not always there */
        cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "No Keep-Alive entry to remove (%s)",
                     keep_alive_dn);
    } else if (rc != LDAP_SUCCESS) {
        /* Failed to delete the entry */
        cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_ERR, "Failed to delete Keep-Alive entry (%s) "
                                                               "Error (%d) This entry will need to be manually removed",
                     keep_alive_dn, rc);
    } else {
        /* Success */
        cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_INFO, "Removed Keep-Alive entry (%s)", keep_alive_dn);
    }
    slapi_pblock_destroy(delete_pb);
    slapi_ch_free_string(&keep_alive_dn);
}

static void
replica_cleanall_ruv_destructor(Slapi_Task *task)
{
    slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                  "replica_cleanall_ruv_destructor -->\n");
    stop_ruv_cleaning();
    if (task) {
        while (slapi_task_get_refcount(task) > 0) {
            /* Yield to wait for the fixup task finishes. */
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                  "replica_cleanall_ruv_destructor <--\n");
}

static void
replica_cleanall_ruv_abort_destructor(Slapi_Task *task)
{
    slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                  "replica_cleanall_ruv_abort_destructor -->\n");
    stop_ruv_cleaning();
    if (task) {
        while (slapi_task_get_refcount(task) > 0) {
            /* Yield to wait for the fixup task finishes. */
            DS_Sleep(PR_MillisecondsToInterval(100));
        }
    }
    slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name,
                  "replica_cleanall_ruv_abort_destructor <--\n");
}

/*
 *  Loop over the agmts, and check if they are in the last phase of cleaning, meaning they have
 *  released cleanallruv data from the config
 */
static void
check_replicas_are_done_cleaning(cleanruv_data *data)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;
    char csnstr[CSN_STRSIZE];
    char *filter = NULL;
    int not_all_cleaned = 1;
    int interval = 10;

    cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_INFO,
                 "Waiting for all the replicas to finish cleaning...");

    csn_as_string(data->maxcsn, PR_FALSE, csnstr);
    filter = PR_smprintf("(%s=%d:%s:%s:%d:%s)", type_replicaCleanRUV, (int)data->rid, csnstr, data->force, data->original_task ? 1 : 0, data->repl_root);
    while (not_all_cleaned && !is_task_aborted(data->rid) && !slapi_is_shutting_down()) {
        agmt_obj = agmtlist_get_first_agreement_for_replica(data->replica);
        if (agmt_obj == NULL) {
            break;
        }
        while (agmt_obj && !slapi_is_shutting_down()) {
            agmt = (Repl_Agmt *)object_get_data(agmt_obj);
            if (!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
                agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
                not_all_cleaned = 0;
                continue;
            }
            if (replica_cleanallruv_is_finished(agmt, filter, data->task) == 0) {
                not_all_cleaned = 0;
            } else {
                not_all_cleaned = 1;
                object_release(agmt_obj);
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
        }
        if (not_all_cleaned == 0 ||
            is_task_aborted(data->rid) ||
            strcasecmp(data->force, "yes") == 0) {
            break;
        }

        cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                     "Not all replicas finished cleaning, retrying in %d seconds",
                     interval);
        if (!slapi_is_shutting_down()) {
            struct timespec current_time = {0};
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += interval;
            pthread_mutex_lock(&notify_lock);
            pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
            pthread_mutex_unlock(&notify_lock);
        }

        interval *= 2;
        if (interval >= CLEANALLRUV_MAX_WAIT) {
            interval = CLEANALLRUV_MAX_WAIT;
        }
    }
    slapi_ch_free_string(&filter);
}

/*
 *  Search this replica for the nsds5ReplicaCleanruv attribute, we don't return
 *  an entry, we know it has finished cleaning.
 */
static int
replica_cleanallruv_is_finished(Repl_Agmt *agmt, char *filter, Slapi_Task *task __attribute__((unused)))
{
    Repl_Connection *conn = NULL;
    ConnResult crc = 0;
    struct berval *payload = NULL;
    int msgid = 0;
    int rc = -1;

    if ((conn = conn_new(agmt)) == NULL) {
        return rc;
    }
    if (conn_connect(conn) == CONN_OPERATION_SUCCESS) {
        payload = create_cleanruv_payload(filter);
        crc = conn_send_extended_operation(conn, REPL_CLEANRUV_CHECK_STATUS_OID, payload, NULL, &msgid);
        if (crc == CONN_OPERATION_SUCCESS) {
            struct berval *retsdata = NULL;
            char *retoid = NULL;

            crc = conn_read_result_ex(conn, &retoid, &retsdata, NULL, msgid, NULL, 1);
            if (CONN_OPERATION_SUCCESS == crc) {
                char *response = NULL;

                decode_cleanruv_payload(retsdata, &response);
                if (response == NULL) {
                    /* this replica does not support cleanallruv */
                    rc = 0;
                } else if (strcmp(response, CLEANRUV_FINISHED) == 0) {
                    /* finished cleaning */
                    rc = 0;
                }
                if (NULL != retsdata)
                    ber_bvfree(retsdata);
                slapi_ch_free_string(&response);
                slapi_ch_free_string(&retoid);
            }
        }
    }
    conn_delete_internal_ext(conn);
    if (payload) {
        ber_bvfree(payload);
    }

    return rc;
}

/*
 *  Loop over the agmts, and check if they are in the last phase of aborting, meaning they have
 *  released the abort cleanallruv data from the config
 */
static void
check_replicas_are_done_aborting(cleanruv_data *data)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;
    char *filter = NULL;
    int not_all_aborted = 1;
    int interval = 10;

    cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_INFO,
                 "Waiting for all the replicas to finish aborting...");

    filter = PR_smprintf("(%s=%d:%s)", type_replicaAbortCleanRUV, data->rid, data->repl_root);

    while (not_all_aborted && !slapi_is_shutting_down()) {
        agmt_obj = agmtlist_get_first_agreement_for_replica(data->replica);
        if (agmt_obj == NULL) {
            break;
        }
        while (agmt_obj && !slapi_is_shutting_down()) {
            agmt = (Repl_Agmt *)object_get_data(agmt_obj);
            if (get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
                agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
                not_all_aborted = 0;
                continue;
            }
            if (replica_cleanallruv_is_finished(agmt, filter, data->task) == 0) {
                not_all_aborted = 0;
            } else {
                not_all_aborted = 1;
                object_release(agmt_obj);
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
        }
        if (not_all_aborted == 0) {
            break;
        }
        cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                     "Not all replicas finished aborting, retrying in %d seconds", interval);
        if (!slapi_is_shutting_down()) {
            struct timespec current_time = {0};
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += interval;
            pthread_mutex_lock(&notify_lock);
            pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
            pthread_mutex_unlock(&notify_lock);
        }
        interval *= 2;
        if (interval >= CLEANALLRUV_MAX_WAIT) {
            interval = CLEANALLRUV_MAX_WAIT;
        }
    }
    slapi_ch_free_string(&filter);
}

/*
 *  Waits for all the repl agmts to be have have the maxcsn.  Returns error only on abort or shutdown
 */
static int
check_agmts_are_caught_up(cleanruv_data *data, char *maxcsn)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;
    char *rid_text;
    int not_all_caughtup = 1;
    int interval = 10;

    rid_text = slapi_ch_smprintf("%d", data->rid);

    while (not_all_caughtup && !is_task_aborted(data->rid) && !slapi_is_shutting_down()) {
        agmt_obj = agmtlist_get_first_agreement_for_replica(data->replica);
        if (agmt_obj == NULL) {
            not_all_caughtup = 0;
            break;
        }
        while (agmt_obj && !slapi_is_shutting_down()) {
            agmt = (Repl_Agmt *)object_get_data(agmt_obj);
            if (!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
                agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
                not_all_caughtup = 0;
                continue;
            }
            if (replica_cleanallruv_check_maxcsn(agmt, data->repl_root, rid_text, maxcsn, data->task) == 0) {
                not_all_caughtup = 0;
            } else {
                not_all_caughtup = 1;
                cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                             "Replica not caught up (%s)", agmt_get_long_name(agmt));
                object_release(agmt_obj);
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
        }

        if (not_all_caughtup == 0 || is_task_aborted(data->rid)) {
            break;
        }
        cleanruv_log(data->task, data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                     "Not all replicas caught up, retrying in %d seconds", interval);
        if (!slapi_is_shutting_down()) {
            struct timespec current_time = {0};
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += interval;
            pthread_mutex_lock(&notify_lock);
            pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
            pthread_mutex_unlock(&notify_lock);
        }
        interval *= 2;
        if (interval >= CLEANALLRUV_MAX_WAIT) {
            interval = CLEANALLRUV_MAX_WAIT;
        }
    }
    slapi_ch_free_string(&rid_text);

    if (is_task_aborted(data->rid)) {
        return -1;
    }

    return not_all_caughtup;
}

/*
 *  Waits for all the repl agmts to be online.  Returns error only on abort or shutdown
 */
static int
check_agmts_are_alive(Replica *replica, ReplicaId rid, Slapi_Task *task)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;
    int not_all_alive = 1;
    int interval = 10;

    while (not_all_alive && is_task_aborted(rid) == 0 && !slapi_is_shutting_down()) {
        agmt_obj = agmtlist_get_first_agreement_for_replica(replica);
        if (agmt_obj == NULL) {
            not_all_alive = 0;
            break;
        }
        while (agmt_obj && !slapi_is_shutting_down()) {
            agmt = (Repl_Agmt *)object_get_data(agmt_obj);
            if (!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
                agmt_obj = agmtlist_get_next_agreement_for_replica(replica, agmt_obj);
                not_all_alive = 0;
                continue;
            }
            if (replica_cleanallruv_replica_alive(agmt) == 0) {
                not_all_alive = 0;
            } else {
                not_all_alive = 1;
                cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE, "Replica not online (%s)",
                             agmt_get_long_name(agmt));
                object_release(agmt_obj);
                break;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica(replica, agmt_obj);
        }

        if (not_all_alive == 0 || is_task_aborted(rid)) {
            break;
        }
        cleanruv_log(task, rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE, "Not all replicas online, retrying in %d seconds...",
                     interval);

        if (!slapi_is_shutting_down()) {
            struct timespec current_time = {0};
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += interval;
            pthread_mutex_lock(&notify_lock);
            pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
            pthread_mutex_unlock(&notify_lock);
        }
        interval *= 2;
        if (interval >= CLEANALLRUV_MAX_WAIT) {
            interval = CLEANALLRUV_MAX_WAIT;
        }
    }
    if (is_task_aborted(rid)) {
        return -1;
    }

    return not_all_alive;
}

/*
 *  Create the CLEANALLRUV extended op payload
 */
struct berval *
create_cleanruv_payload(char *value)
{
    struct berval *req_data = NULL;
    BerElement *tmp_bere = NULL;

    if ((tmp_bere = der_alloc()) == NULL) {
        goto error;
    }
    if (ber_printf(tmp_bere, "{s}", value) == -1) {
        goto error;
    }
    if (ber_flatten(tmp_bere, &req_data) == -1) {
        goto error;
    }
    goto done;

error:
    if (NULL != req_data) {
        ber_bvfree(req_data);
        req_data = NULL;
    }

done:
    if (NULL != tmp_bere) {
        ber_free(tmp_bere, 1);
        tmp_bere = NULL;
    }

    return req_data;
}

/*
 *  Manually add the CLEANRUV task to replicas that do not support
 *  the CLEANALLRUV task.
 */
static void
replica_send_cleanruv_task(Repl_Agmt *agmt, cleanruv_data *clean_data)
{
    Repl_Connection *conn;
    ConnResult crc = 0;
    LDAP *ld;
    Slapi_DN *sdn;
    struct berval *vals[2];
    struct berval val;
    LDAPMod *mods[2];
    LDAPMod mod;
    char *repl_dn = NULL;
    char data[15];
    int rc;

    if ((conn = conn_new(agmt)) == NULL) {
        return;
    }
    crc = conn_connect(conn);
    if (CONN_OPERATION_SUCCESS != crc) {
        conn_delete_internal_ext(conn);
        return;
    }
    ld = conn_get_ldap(conn);
    if (ld == NULL) {
        conn_delete_internal_ext(conn);
        return;
    }
    sdn = agmt_get_replarea(agmt);
    if (!sdn) {
        conn_delete_internal_ext(conn);
        return;
    }
    mod.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    mod.mod_type = "nsds5task";
    mod.mod_bvalues = vals;
    vals[0] = &val;
    vals[1] = NULL;
    val.bv_len = PR_snprintf(data, sizeof(data), "CLEANRUV%d", clean_data->rid);
    val.bv_val = data;
    mods[0] = &mod;
    mods[1] = NULL;
    repl_dn = slapi_create_dn_string("cn=replica,cn=\"%s\",cn=mapping tree,cn=config", slapi_sdn_get_dn(sdn));
    /*
     *  Add task to remote replica
     */
    rc = ldap_modify_ext_s(ld, repl_dn, mods, NULL, NULL);

    if (rc != LDAP_SUCCESS) {
        char *hostname = agmt_get_hostname(agmt);

        cleanruv_log(clean_data->task, clean_data->rid, CLEANALLRUV_ID, SLAPI_LOG_ERR,
                     "Failed to add CLEANRUV task (%s) to replica (%s).  You will need "
                     "to manually run the CLEANRUV task on this replica (%s) error (%d)",
                     repl_dn, agmt_get_long_name(agmt), hostname, rc);
        slapi_ch_free_string(&hostname);
    }
    slapi_ch_free_string(&repl_dn);
    slapi_sdn_free(&sdn);
    conn_delete_internal_ext(conn);
}

/*
 *  Check if the rid is in our list of "cleaned" rids
 */
int
is_cleaned_rid(ReplicaId rid)
{
    PR_Lock(rid_lock);
    for (size_t i = 0; i < CLEANRID_BUFSIZ; i++) {
        if (rid == cleaned_rids[i]) {
            PR_Unlock(rid_lock);
            return 1;
        }
    }
    PR_Unlock(rid_lock);

    return 0;
}

int
is_pre_cleaned_rid(ReplicaId rid)
{
    PR_Lock(rid_lock);
    for (size_t i = 0; i < CLEANRID_BUFSIZ; i++) {
        if (rid == pre_cleaned_rids[i]) {
            PR_Unlock(rid_lock);
            return 1;
        }
    }
    PR_Unlock(rid_lock);

    return 0;
}

int
is_task_aborted(ReplicaId rid)
{
    int i;

    if (rid == 0) {
        return 0;
    }
    PR_Lock(abort_rid_lock);
    for (i = 0; i < CLEANRID_BUFSIZ && aborted_rids[i] != 0; i++) {
        if (rid == aborted_rids[i]) {
            PR_Unlock(abort_rid_lock);
            return 1;
        }
    }
    PR_Unlock(abort_rid_lock);
    return 0;
}

static void
preset_cleaned_rid(ReplicaId rid)
{
    int i;

    PR_Lock(rid_lock);
    for (i = 0; i < CLEANRID_BUFSIZ && pre_cleaned_rids[i] != rid; i++) {
        if (pre_cleaned_rids[i] == 0) {
            pre_cleaned_rids[i] = rid;
            break;
        }
    }
    PR_Unlock(rid_lock);
}

/*
 *  Just add the rid to the in memory, as we don't want it to survive after a restart,
 *  This prevent the changelog from sending updates from this rid, and the local ruv
 *  will not be updated either.
 */
void
set_cleaned_rid(ReplicaId rid)
{
    int i;

    PR_Lock(rid_lock);
    for (i = 0; i < CLEANRID_BUFSIZ && cleaned_rids[i] != rid; i++) {
        if (cleaned_rids[i] == 0) {
            cleaned_rids[i] = rid;
        }
    }
    PR_Unlock(rid_lock);
}

/*
 *  Add the rid and maxcsn to the repl config (so we can resume after a server restart)
 */
void
add_cleaned_rid(cleanruv_data *cleanruv_data)
{
    Slapi_PBlock *pb;
    struct berval *vals[2];
    struct berval val;
    LDAPMod *mods[2];
    LDAPMod mod;
    char *data = NULL;
    char *dn = NULL;
    int rc;
    ReplicaId rid;
    Replica *r;
    char *forcing;

    rid = cleanruv_data->rid;
    r = cleanruv_data->replica;
    forcing = cleanruv_data->force;

    if (r == NULL) {
        return;
    }
    /*
     *  Write the rid & maxcsn to the config entry
     */
    data = slapi_ch_smprintf("%d:%s:%d:%s",
            rid, forcing, cleanruv_data->original_task ? 1 : 0,
            cleanruv_data->repl_root);
    dn = replica_get_dn(r);
    pb = slapi_pblock_new();
    mod.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    mod.mod_type = (char *)type_replicaCleanRUV;
    mod.mod_bvalues = vals;
    vals[0] = &val;
    vals[1] = NULL;
    val.bv_len = strlen(data);
    val.bv_val = data;
    mods[0] = &mod;
    mods[1] = NULL;

    slapi_modify_internal_set_pb(pb, dn, mods, NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS && rc != LDAP_TYPE_OR_VALUE_EXISTS && rc != LDAP_NO_SUCH_OBJECT) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "CleanAllRUV Task - add_cleaned_rid: "
                                                       "Failed to update replica config (%d), rid (%d)\n",
                      rc, rid);
    }
    slapi_ch_free_string(&data);
    slapi_ch_free_string(&dn);
    slapi_pblock_destroy(pb);
}

/*
 *  Add aborted rid and repl root to config in case of a server restart
 */
void
add_aborted_rid(ReplicaId rid, Replica *r, char *repl_root, char *certify_all, PRBool original_task)
{
    Slapi_PBlock *pb;
    struct berval *vals[2];
    struct berval val;
    LDAPMod *mods[2];
    LDAPMod mod;
    char *data;
    char *dn;
    int rc;
    int i;

    PR_Lock(abort_rid_lock);
    for (i = 0; i < CLEANRID_BUFSIZ; i++) {
        if (aborted_rids[i] == 0) {
            aborted_rids[i] = rid;
            break;
        }
    }
    PR_Unlock(abort_rid_lock);
    /*
     *  Write the rid to the config entry
     */
    dn = replica_get_dn(r);
    pb = slapi_pblock_new();
    data = PR_smprintf("%d:%s:%s:%d", rid, repl_root, certify_all, original_task ? 1 : 0);
    mod.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    mod.mod_type = (char *)type_replicaAbortCleanRUV;
    mod.mod_bvalues = vals;
    vals[0] = &val;
    vals[1] = NULL;
    val.bv_val = data;
    val.bv_len = strlen(data);
    mods[0] = &mod;
    mods[1] = NULL;

    slapi_modify_internal_set_pb(pb, dn, mods, NULL, NULL,
                                 repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_modify_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != LDAP_SUCCESS && rc != LDAP_TYPE_OR_VALUE_EXISTS && rc != LDAP_NO_SUCH_OBJECT) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "Abort CleanAllRUV Task - add_aborted_rid: "
                                                       "Failed to update replica config (%d), rid (%d)\n",
                      rc, rid);
    }

    slapi_ch_free_string(&dn);
    slapi_ch_free_string(&data);
    slapi_pblock_destroy(pb);
}

void
delete_aborted_rid(Replica *r, ReplicaId rid, char *repl_root, char *certify_all, PRBool original_task, int skip)
{
    Slapi_PBlock *pb;
    LDAPMod *mods[2];
    LDAPMod mod;
    struct berval *vals[2];
    struct berval val;
    char *data;
    char *dn;
    int rc;

    if (r == NULL)
        return;

    if (skip) {
        /* skip the deleting of the config, and just remove the in memory rid */
        ReplicaId new_abort_rids[CLEANRID_BUFSIZ] = {0};
        int32_t idx = 0;

        PR_Lock(abort_rid_lock);
        for (size_t i = 0; i < CLEANRID_BUFSIZ; i++) {
            if (aborted_rids[i] != rid) {
                new_abort_rids[idx] = aborted_rids[i];
                idx++;
            }
        }
        memcpy(aborted_rids, new_abort_rids, sizeof(new_abort_rids));
        PR_Unlock(abort_rid_lock);
    } else {
        /* only remove the config, leave the in-memory rid */
        dn = replica_get_dn(r);
        data = PR_smprintf("%d:%s:%s:%d", (int)rid, repl_root, certify_all, original_task ? 1 : 0);

        mod.mod_op = LDAP_MOD_DELETE | LDAP_MOD_BVALUES;
        mod.mod_type = (char *)type_replicaAbortCleanRUV;
        mod.mod_bvalues = vals;
        vals[0] = &val;
        vals[1] = NULL;
        val.bv_val = data;
        val.bv_len = strlen(data);
        mods[0] = &mod;
        mods[1] = NULL;

        pb = slapi_pblock_new();
        slapi_modify_internal_set_pb(pb, dn, mods, NULL, NULL, repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
        slapi_modify_internal_pb(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT) {
            slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "Abort CleanAllRUV Task - delete_aborted_rid: "
                                                           "Failed to remove replica config (%d), rid (%d)\n",
                          rc, rid);
        }
        slapi_pblock_destroy(pb);
        slapi_ch_free_string(&dn);
        slapi_ch_free_string(&data);
    }
}

/*
 *  Just remove the dse.ldif config, but we need to keep the cleaned rids in memory until we know we are done
 */
static void
delete_cleaned_rid_config(cleanruv_data *clean_data)
{
    Slapi_PBlock *pb, *modpb;
    Slapi_Entry **entries = NULL;
    LDAPMod *mods[2];
    LDAPMod mod;
    char *iter = NULL;
    char *dn = NULL;
    int i, ii;
    int rc = -1, ret, rid;

    if (clean_data == NULL) {
        cleanruv_log(NULL, -1, CLEANALLRUV_ID, SLAPI_LOG_ERR, "delete_cleaned_rid_config - cleanruv data is NULL, "
                                                              "failed to clean the config.");
        return;
    }
    /*
     *  Search the config for the exact attribute value to delete
     */
    pb = slapi_pblock_new();
    if (clean_data->replica) {
        dn = replica_get_dn(clean_data->replica);
    } else {
        goto bail;
    }

    slapi_search_internal_set_pb(pb, dn, LDAP_SCOPE_SUBTREE, "nsds5ReplicaCleanRUV=*", NULL, 0, NULL, NULL,
                                 (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret != LDAP_SUCCESS) {
        cleanruv_log(clean_data->task, clean_data->rid, CLEANALLRUV_ID, SLAPI_LOG_ERR,
                     "delete_cleaned_rid_config - Internal search failed(%d).", ret);
        goto bail;
    } else {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (entries == NULL || entries[0] == NULL) {
            /*
             *  No matching entries!
             */
            cleanruv_log(clean_data->task, clean_data->rid, CLEANALLRUV_ID, SLAPI_LOG_ERR,
                         "delete_cleaned_rid_config - Failed to find any "
                         "entries with nsds5ReplicaCleanRUV under (%s)",
                         dn);
            goto bail;
        } else {
            /*
             *  Clean all the matching entries
             */
            for (i = 0; entries[i] != NULL; i++) {
                char **attr_val = slapi_entry_attr_get_charray(entries[i], type_replicaCleanRUV);
                char *edn = slapi_entry_get_dn(entries[i]);

                for (ii = 0; attr_val && attr_val[ii] && i < 5; ii++) {
                    /* make a copy to retain the full value after toking */
                    char *aval = slapi_ch_strdup(attr_val[ii]);

                    rid = atoi(ldap_utf8strtok_r(attr_val[ii], ":", &iter));
                    if (rid == clean_data->rid) {
                        struct berval *vals[2];
                        struct berval val[1];
                        val[0].bv_len = strlen(aval);
                        val[0].bv_val = aval;
                        vals[0] = &val[0];
                        vals[1] = NULL;

                        mod.mod_op = LDAP_MOD_DELETE | LDAP_MOD_BVALUES;
                        mod.mod_type = (char *)type_replicaCleanRUV;
                        mod.mod_bvalues = vals;
                        mods[0] = &mod;
                        mods[1] = NULL;

                        modpb = slapi_pblock_new();
                        slapi_modify_internal_set_pb(modpb, edn, mods, NULL, NULL,
                                                     repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
                        slapi_modify_internal_pb(modpb);
                        slapi_pblock_get(modpb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
                        slapi_pblock_destroy(modpb);
                        if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT) {
                            cleanruv_log(clean_data->task, clean_data->rid, CLEANALLRUV_ID, SLAPI_LOG_ERR,
                                    "delete_cleaned_rid_config - Failed to remove task data from (%s) error (%d), rid (%d)",
                                    edn, rc, clean_data->rid);
                            slapi_ch_array_free(attr_val);
                            goto bail;
                        }
                    }
                    slapi_ch_free_string(&aval);
                }
                slapi_ch_array_free(attr_val);
            }
        }
    }

bail:
    if (rc != LDAP_SUCCESS && rc != LDAP_NO_SUCH_OBJECT) {
        cleanruv_log(clean_data->task, clean_data->rid, CLEANALLRUV_ID, SLAPI_LOG_ERR,
                     "delete_cleaned_rid_config - Failed to remove replica config "
                     "(%d), rid (%d)",
                     rc, clean_data->rid);
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
    slapi_ch_free_string(&dn);
}

/*
 *  Remove the rid from our list, and the config
 */
void
remove_cleaned_rid(ReplicaId rid)
{
    ReplicaId new_cleaned_rids[CLEANRID_BUFSIZ] = {0};
    ReplicaId new_pre_cleaned_rids[CLEANRID_BUFSIZ] = {0};
    size_t idx = 0;

    PR_Lock(rid_lock);

    for (size_t i = 0; i < CLEANRID_BUFSIZ; i++) {
        if (cleaned_rids[i] != rid) {
            new_cleaned_rids[idx] = cleaned_rids[i];
            idx++;
        }
    }
    memcpy(cleaned_rids, new_cleaned_rids, sizeof(new_cleaned_rids));

    /* now do the preset cleaned rids */
    idx = 0;
    for (size_t i = 0; i < CLEANRID_BUFSIZ; i++) {
        if (pre_cleaned_rids[i] != rid) {
            new_pre_cleaned_rids[idx] = pre_cleaned_rids[i];
            idx++;
        }
    }
    memcpy(pre_cleaned_rids, new_pre_cleaned_rids, sizeof(new_pre_cleaned_rids));

    PR_Unlock(rid_lock);
}

/*
 *  Abort the CLEANALLRUV task
 */
int
replica_cleanall_ruv_abort(Slapi_PBlock *pb __attribute__((unused)),
                           Slapi_Entry *e,
                           Slapi_Entry *eAfter __attribute__((unused)),
                           int *returncode,
                           char *returntext,
                           void *arg __attribute__((unused)))
{
    struct berval *payload = NULL;
    cleanruv_data *data = NULL;
    PRThread *thread = NULL;
    Slapi_Task *task = NULL;
    Slapi_DN *sdn = NULL;
    Replica *replica;
    ReplicaId rid = -1;
    PRBool original_task = PR_TRUE;
    const char *certify_all;
    const char *orig_val;
    const char *base_dn;
    const char *rid_str;
    char *ridstr = NULL;
    int rc = SLAPI_DSE_CALLBACK_OK;

    /* allocate new task now */
    task = slapi_new_task(slapi_entry_get_ndn(e));

    /* register our destructor for waiting the task is done */
    slapi_task_set_destructor_fn(task, replica_cleanall_ruv_abort_destructor);

    /*
     *  Get our task settings
     */
    if ((rid_str = slapi_entry_attr_get_ref(e, "replica-id")) == NULL) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Missing required attr \"replica-id\"");
        cleanruv_log(task, -1, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    certify_all = slapi_entry_attr_get_ref(e, "replica-certify-all");
    /*
     *  Check the rid
     */
    rid = atoi(rid_str);
    if (rid <= 0 || rid >= READ_ONLY_REPLICA_ID) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid replica id (%d) for task - (%s)",
                    rid, slapi_sdn_get_dn(slapi_entry_get_sdn(e)));
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if ((base_dn = slapi_entry_attr_get_ref(e, "replica-base-dn")) == NULL) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Missing required attr \"replica-base-dn\"");
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_OBJECT_CLASS_VIOLATION;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if (!is_cleaned_rid(rid) && !is_pre_cleaned_rid(rid)) {
        /* we are not cleaning this rid */
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Replica id (%d) is not being cleaned, nothing to abort.", rid);
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if (is_task_aborted(rid)) {
        /* we are already aborting this rid */
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Replica id (%d) is already being aborted", rid);
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /*
     *  Get the replica object
     */
    sdn = slapi_sdn_new_dn_byval(base_dn);
    if ((replica = replica_get_replica_from_dn(sdn)) == NULL) {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Failed to find replica from dn(%s)", base_dn);
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /*
     *  Check certify value
     */
    if (certify_all) {
        if (strcasecmp(certify_all, "yes") && strcasecmp(certify_all, "no")) {
            PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Invalid value for \"replica-certify-all\", the value "
                                                               "must be \"yes\" or \"no\".");
            cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
            *returncode = LDAP_OPERATIONS_ERROR;
            rc = SLAPI_DSE_CALLBACK_ERROR;
            goto out;
        }
    } else {
        /*
         * The default should be not to certify all the replicas, because
         * we might be trying to abort a clean task that is "hanging" due
         * to unreachable replicas.  If the default is "yes" then the abort
         * task will run into the same issue.
         */
        certify_all = "no";
    }

    if (check_and_set_abort_cleanruv_task_count() != LDAP_SUCCESS) {
        /* we are already running the maximum number of tasks */
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                    "Exceeded maximum number of active ABORT CLEANALLRUV tasks(%d)",
                    CLEANRIDSIZ);
        cleanruv_log(task, -1, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "%s", returntext);
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        goto out;
    }
    /*
     *  Create payload
     */
    ridstr = slapi_ch_smprintf("%d:%s:%s", rid, base_dn, certify_all);
    payload = create_cleanruv_payload(ridstr);

    if (payload == NULL) {
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "Failed to create extended op payload, aborting task");
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    /*
     *  Stop the cleaning, and delete the rid
     */
    add_aborted_rid(rid, replica, (char *)base_dn, (char *)certify_all, original_task);
    stop_ruv_cleaning();

    /*
     *  Prepare the abort struct and fire off the thread
     */
    data = (cleanruv_data *)slapi_ch_calloc(1, sizeof(cleanruv_data));
    if (data == NULL) {
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "Failed to allocate abort_cleanruv_data.  Aborting task.");
        *returncode = LDAP_OPERATIONS_ERROR;
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }
    if ((orig_val = slapi_entry_attr_get_ref(e, "replica-original-task")) != NULL) {
        if (!strcasecmp(orig_val, "0")) {
            original_task = PR_FALSE;
        }
    }
    data->replica = replica;
    data->task = task;
    data->payload = payload;
    data->rid = rid;
    data->repl_root = slapi_ch_strdup(base_dn);
    data->sdn = NULL;
    data->certify = slapi_ch_strdup(certify_all);
    data->original_task = original_task;

    thread = PR_CreateThread(PR_USER_THREAD, replica_abort_task_thread,
                             (void *)data, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                             PR_UNJOINABLE_THREAD, SLAPD_DEFAULT_THREAD_STACKSIZE);
    if (thread == NULL) {
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "Unable to create abort thread.  Aborting task.");
        *returncode = LDAP_OPERATIONS_ERROR;
        slapi_ch_free_string(&data->certify);
        rc = SLAPI_DSE_CALLBACK_ERROR;
    }

out:
    slapi_ch_free_string(&ridstr);
    slapi_sdn_free(&sdn);

    if (rc != SLAPI_DSE_CALLBACK_OK) {
        cleanruv_log(task, rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "Abort Task failed (%d)", rc);
        slapi_task_finish(task, rc);
    }

    return rc;
}

/*
 *  Abort CLEANALLRUV task thread
 */
void
replica_abort_task_thread(void *arg)
{
    cleanruv_data *data = (cleanruv_data *)arg;
    Repl_Agmt *agmt;
    Object *agmt_obj;
    int agmt_not_notified = 1;
    int interval = 10;
    int count = 0, rc = 0;

    if (!data || slapi_is_shutting_down()) {
        return; /* no data */
    }

    /* Increase active thread count to prevent a race condition at server shutdown */
    g_incr_active_threadcnt();

    if (data->task) {
        slapi_task_inc_refcount(data->task);
        slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name, "replica_abort_task_thread --> refcount incremented.\n");
    }
    cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_INFO, "Aborting task for rid(%d)...", data->rid);

    /*
     *   Need to build the replica from the dn
     */
    if (data->replica == NULL) {
        /*
         * This thread was initiated at startup because the process did not finish.  Due
         * to timing issues, we need to wait to grab the replica obj until we get here.
         */
        if ((data->replica = replica_get_replica_from_dn(data->sdn)) == NULL) {
            cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR,
                         "Failed to get replica object from dn (%s).", slapi_sdn_get_dn(data->sdn));
            goto done;
        }
    }

    /*
     *  Now send the abort cleanruv extended op to all the agreements
     */
    while (agmt_not_notified && !slapi_is_shutting_down()) {
        agmt_obj = agmtlist_get_first_agreement_for_replica(data->replica);
        if (agmt_obj == NULL) {
            agmt_not_notified = 0;
            break;
        }
        while (agmt_obj && !slapi_is_shutting_down()) {
            agmt = (Repl_Agmt *)object_get_data(agmt_obj);
            if (!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
                agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
                agmt_not_notified = 0;
                continue;
            }
            if (replica_cleanallruv_send_abort_extop(agmt, data->task, data->payload)) {
                if (strcasecmp(data->certify, "yes") == 0) {
                    /* we are verifying all the replicas receive the abort task */
                    agmt_not_notified = 1;
                    object_release(agmt_obj);
                    break;
                } else {
                    /* we do not care if we could not reach a replica, just continue as if we did */
                    agmt_not_notified = 0;
                }
            } else {
                /* success */
                agmt_not_notified = 0;
            }
            agmt_obj = agmtlist_get_next_agreement_for_replica(data->replica, agmt_obj);
        } /* while loop for agmts */

        if (agmt_not_notified == 0) {
            /* everybody has been contacted */
            break;
        }
        /*
         *  Need to sleep between passes. unless we are shutting down
         */
        if (!slapi_is_shutting_down()) {
            struct timespec current_time = {0};
            cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_NOTICE, "Retrying in %d seconds", interval);
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            current_time.tv_sec += interval;
            pthread_mutex_lock(&notify_lock);
            pthread_cond_timedwait(&notify_cvar, &notify_lock, &current_time);
            pthread_mutex_unlock(&notify_lock);
        }

        interval *= 2;
        if (interval >= CLEANALLRUV_MAX_WAIT) {
            interval = CLEANALLRUV_MAX_WAIT;
        }
    } /* while */

done:
    if (agmt_not_notified || slapi_is_shutting_down()) {
        /* failure */
        cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "Abort task failed, will resume the task at the next server startup.");
    } else {
        /*
         *  Wait for this server to stop its cleanallruv task(which removes the rid from the cleaned list)
         */
        cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_INFO, "Waiting for CleanAllRUV task to abort...");
        while (is_cleaned_rid(data->rid) && !slapi_is_shutting_down()) {
            DS_Sleep(PR_SecondsToInterval(1));
            count++;
            if (count == 60) { /* it should not take this long */
                cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "CleanAllRUV task failed to abort.  You might need to "
                                                                                         "rerun the task.");
                rc = -1;
                break;
            }
        }
        /*
         *  Clean up the config
         */
        delete_aborted_rid(data->replica, data->rid, data->repl_root, data->certify, data->original_task, 1); /* delete just the config, leave rid in memory */
        if (strcasecmp(data->certify, "yes") == 0) {
            check_replicas_are_done_aborting(data);
        }
        delete_aborted_rid(data->replica, data->rid, data->repl_root, data->certify, data->original_task, 0); /* remove the in-memory aborted rid */
        if (rc == 0) {
            cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_INFO, "Successfully aborted task for rid(%d)", data->rid);
        } else {
            cleanruv_log(data->task, data->rid, ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR, "Failed to abort task for rid(%d)", data->rid);
        }
    }

    if (data->task) {
        slapi_task_finish(data->task, agmt_not_notified);
        slapi_task_dec_refcount(data->task);
        slapi_log_err(SLAPI_LOG_PLUGIN, repl_plugin_name, "replica_abort_task_thread <-- refcount incremented.\n");
    }
    if (data->payload) {
        ber_bvfree(data->payload);
    }
    slapi_ch_free_string(&data->repl_root);
    slapi_ch_free_string(&data->certify);
    slapi_sdn_free(&data->sdn);
    slapi_ch_free((void **)&data);
    PR_Lock(task_count_lock);
    abort_task_count--;
    PR_Unlock(task_count_lock);
    g_decr_active_threadcnt();
}

static int
replica_cleanallruv_send_abort_extop(Repl_Agmt *ra, Slapi_Task *task, struct berval *payload)
{
    Repl_Connection *conn = NULL;
    ConnResult crc = 0;
    int msgid = 0;
    int rc = -1;

    if ((conn = conn_new(ra)) == NULL) {
        return rc;
    }
    if (conn_connect(conn) == CONN_OPERATION_SUCCESS) {
        crc = conn_send_extended_operation(conn, REPL_ABORT_CLEANRUV_OID, payload, NULL, &msgid);
        /*
         * success or failure, just return the error code
         */
        rc = crc;
        if (rc) {
            cleanruv_log(task, agmt_get_consumer_rid(ra, conn), ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR,
                         "Failed to send extop to replica(%s).", agmt_get_long_name(ra));
        }
    } else {
        cleanruv_log(task, agmt_get_consumer_rid(ra, conn), ABORT_CLEANALLRUV_ID, SLAPI_LOG_ERR,
                     "Failed to connect to replica(%s).", agmt_get_long_name(ra));
        rc = -1;
    }
    conn_delete_internal_ext(conn);

    return rc;
}


static int
replica_cleanallruv_send_extop(Repl_Agmt *ra, cleanruv_data *clean_data, int check_result)
{
    Repl_Connection *conn = NULL;
    ConnResult crc = 0;
    int msgid = 0;
    int rc = -1;

    if ((conn = conn_new(ra)) == NULL) {
        return rc;
    }
    if (conn_connect(conn) == CONN_OPERATION_SUCCESS) {
        crc = conn_send_extended_operation(conn, REPL_CLEANRUV_OID, clean_data->payload, NULL, &msgid);
        if (crc == CONN_OPERATION_SUCCESS && check_result) {
            struct berval *retsdata = NULL;
            char *retoid = NULL;

            crc = conn_read_result_ex(conn, &retoid, &retsdata, NULL, msgid, NULL, 1);
            if (CONN_OPERATION_SUCCESS == crc) {
                char *response = NULL;

                decode_cleanruv_payload(retsdata, &response);
                if (response && strcmp(response, CLEANRUV_ACCEPTED) == 0) {
                    /* extop was accepted */
                    rc = 0;
                } else {
                    cleanruv_log(clean_data->task, clean_data->rid, CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                                 "Replica %s does not support the CLEANALLRUV task.  "
                                 "Sending replica CLEANRUV task...",
                                 slapi_sdn_get_dn(agmt_get_dn_byref(ra)));
                    /*
                     *  Ok, this replica doesn't know about CLEANALLRUV, so just manually
                     *  add the CLEANRUV task to the replica.
                     */
                    replica_send_cleanruv_task(ra, clean_data);
                    rc = 0;
                }
                if (NULL != retsdata)
                    ber_bvfree(retsdata);
                slapi_ch_free_string(&retoid);
                slapi_ch_free_string(&response);
            }
        } else {
            /*
             * success or failure, just return the error code
             */
            rc = crc;
        }
    }
    conn_delete_internal_ext(conn);

    return rc;
}

static CSN *
replica_cleanallruv_find_maxcsn(Replica *replica, ReplicaId rid, char *basedn)
{
    Object *agmt_obj;
    Repl_Agmt *agmt;
    CSN *maxcsn = NULL, *topcsn = NULL;
    char *rid_text = slapi_ch_smprintf("%d", rid);
    char *csnstr = NULL;

    /* start with the local maxcsn */
    csnstr = replica_cleanallruv_get_local_maxcsn(rid, basedn);
    if (csnstr) {
        topcsn = csn_new();
        csn_init_by_string(topcsn, csnstr);
        slapi_ch_free_string(&csnstr);
    }

    agmt_obj = agmtlist_get_first_agreement_for_replica(replica);
    if (agmt_obj == NULL) { /* no agreements */
        goto done;
    }
    while (agmt_obj && !slapi_is_shutting_down()) {
        agmt = (Repl_Agmt *)object_get_data(agmt_obj);
        if (!agmt_is_enabled(agmt) || get_agmt_agreement_type(agmt) == REPLICA_TYPE_WINDOWS) {
            agmt_obj = agmtlist_get_next_agreement_for_replica(replica, agmt_obj);
            continue;
        }
        if (replica_cleanallruv_get_replica_maxcsn(agmt, rid_text, basedn, &maxcsn) == 0) {
            if (maxcsn == NULL) {
                agmt_obj = agmtlist_get_next_agreement_for_replica(replica, agmt_obj);
                continue;
            }
            if (topcsn == NULL) {
                topcsn = maxcsn;
            } else {
                if (csn_compare(topcsn, maxcsn) < 0) {
                    csn_free(&topcsn);
                    topcsn = maxcsn;
                } else {
                    csn_free(&maxcsn);
                }
            }
        }
        agmt_obj = agmtlist_get_next_agreement_for_replica(replica, agmt_obj);
    }

done:
    slapi_ch_free_string(&rid_text);

    return topcsn;
}

static int
replica_cleanallruv_get_replica_maxcsn(Repl_Agmt *agmt, char *rid_text, char *basedn, CSN **csn)
{
    Repl_Connection *conn = NULL;
    ConnResult crc = -1;
    struct berval *payload = NULL;
    CSN *maxcsn = NULL;
    char *data = NULL;
    int msgid = 0;

    if ((conn = conn_new(agmt)) == NULL) {
        return -1;
    }

    data = slapi_ch_smprintf("%s:%s", rid_text, basedn);
    payload = create_cleanruv_payload(data);

    if (conn_connect(conn) == CONN_OPERATION_SUCCESS) {
        crc = conn_send_extended_operation(conn, REPL_CLEANRUV_GET_MAXCSN_OID, payload, NULL, &msgid);
        if (crc == CONN_OPERATION_SUCCESS) {
            struct berval *retsdata = NULL;
            char *retoid = NULL;

            crc = conn_read_result_ex(conn, &retoid, &retsdata, NULL, msgid, NULL, 1);
            if (CONN_OPERATION_SUCCESS == crc) {
                char *remote_maxcsn = NULL;

                decode_cleanruv_payload(retsdata, &remote_maxcsn);
                if (remote_maxcsn && strcmp(remote_maxcsn, CLEANRUV_NO_MAXCSN)) {
                    maxcsn = csn_new();
                    csn_init_by_string(maxcsn, remote_maxcsn);
                    *csn = maxcsn;
                } else {
                    /* no csn */
                    *csn = NULL;
                }
                slapi_ch_free_string(&retoid);
                slapi_ch_free_string(&remote_maxcsn);
                if (NULL != retsdata)
                    ber_bvfree(retsdata);
            }
        }
    }
    conn_delete_internal_ext(conn);
    slapi_ch_free_string(&data);
    if (payload)
        ber_bvfree(payload);

    return (int)crc;
}

static int
replica_cleanallruv_check_maxcsn(Repl_Agmt *agmt, char *basedn, char *rid_text, char *maxcsn, Slapi_Task *task)
{
    Repl_Connection *conn = NULL;
    ConnResult crc = 0;
    struct berval *payload = NULL;
    char *data = NULL;
    int msgid = 0;
    int rc = -1;

    if ((conn = conn_new(agmt)) == NULL) {
        return -1;
    }

    data = slapi_ch_smprintf("%s:%s", rid_text, basedn);
    payload = create_cleanruv_payload(data);

    if (conn_connect(conn) == CONN_OPERATION_SUCCESS) {
        crc = conn_send_extended_operation(conn, REPL_CLEANRUV_GET_MAXCSN_OID, payload, NULL, &msgid);
        if (crc == CONN_OPERATION_SUCCESS) {
            struct berval *retsdata = NULL;
            char *retoid = NULL;

            crc = conn_read_result_ex(conn, &retoid, &retsdata, NULL, msgid, NULL, 1);
            if (CONN_OPERATION_SUCCESS == crc) {
                char *remote_maxcsn = NULL;

                decode_cleanruv_payload(retsdata, &remote_maxcsn);
                if (remote_maxcsn && strcmp(remote_maxcsn, CLEANRUV_NO_MAXCSN)) {
                    CSN *max, *repl_max;

                    max = csn_new();
                    repl_max = csn_new();
                    csn_init_by_string(max, maxcsn);
                    csn_init_by_string(repl_max, remote_maxcsn);
                    if (csn_compare(repl_max, max) < 0) {
                        /* we are not caught up yet, free, and return */
                        cleanruv_log(task, atoi(rid_text), CLEANALLRUV_ID, SLAPI_LOG_NOTICE,
                                     "Replica maxcsn (%s) is not caught up with deleted replica's maxcsn(%s)",
                                     remote_maxcsn, maxcsn);
                        rc = -1;
                    } else {
                        /* ok this replica is caught up */
                        rc = 0;
                    }
                    csn_free(&max);
                    csn_free(&repl_max);
                } else {
                    /* no remote_maxcsn - return success */
                    rc = 0;
                }
                slapi_ch_free_string(&retoid);
                slapi_ch_free_string(&remote_maxcsn);
                if (NULL != retsdata)
                    ber_bvfree(retsdata);
            }
        }
    }
    conn_delete_internal_ext(conn);
    slapi_ch_free_string(&data);
    if (payload)
        ber_bvfree(payload);

    return rc;
}


static int
replica_cleanallruv_replica_alive(Repl_Agmt *agmt)
{
    Repl_Connection *conn = NULL;
    LDAP *ld = NULL;
    LDAPMessage *result = NULL;
    int rc = -1;

    if ((conn = conn_new(agmt)) == NULL) {
        return rc;
    }
    if (conn_connect(conn) == CONN_OPERATION_SUCCESS) {
        ld = conn_get_ldap(conn);
        if (ld == NULL) {
            slapi_log_err(SLAPI_LOG_REPL, repl_plugin_name, "CleanAllRUV_task - failed to get LDAP "
                                                            "handle from the replication agmt (%s).  Moving on to the next agmt.\n",
                          agmt_get_long_name(agmt));
            conn_delete_internal_ext(conn);
            return -1;
        }
        if (ldap_search_ext_s(ld, "", LDAP_SCOPE_BASE, "objectclass=top",
                              NULL, 0, NULL, NULL, NULL, 0, &result) == LDAP_SUCCESS) {
            rc = 0;
        } else {
            rc = -1;
        }
        if (result)
            ldap_msgfree(result);
    }
    conn_delete_internal_ext(conn);

    return rc;
}

static int
replica_cleanallruv_check_ruv(char *repl_root, Repl_Agmt *agmt, char *rid_text, Slapi_Task *task __attribute__((unused)), char *force)
{
    Repl_Connection *conn = NULL;
    ConnResult crc = 0;
    struct berval *payload = NULL;
    char *data = NULL;
    int msgid = 0;
    int rc = -1;

    if ((conn = conn_new(agmt)) == NULL) {
        if (strcasecmp(force, "yes") == 0) {
            return 0;
        }
        return rc;
    }

    data = slapi_ch_smprintf("%s:%s", rid_text, repl_root);
    payload = create_cleanruv_payload(data);

    if (conn_connect(conn) == CONN_OPERATION_SUCCESS) {
        crc = conn_send_extended_operation(conn, REPL_CLEANRUV_GET_MAXCSN_OID, payload, NULL, &msgid);
        if (crc == CONN_OPERATION_SUCCESS) {
            struct berval *retsdata = NULL;
            char *retoid = NULL;

            crc = conn_read_result_ex(conn, &retoid, &retsdata, NULL, msgid, NULL, 1);
            if (CONN_OPERATION_SUCCESS == crc) {
                char *remote_maxcsn = NULL;

                decode_cleanruv_payload(retsdata, &remote_maxcsn);
                if (remote_maxcsn && strcmp(remote_maxcsn, CLEANRUV_NO_MAXCSN)) {
                    /* remote replica still has dirty RUV element */
                    rc = -1;
                } else {
                    /* no maxcsn = we're clean */
                    rc = 0;
                }
                slapi_ch_free_string(&retoid);
                slapi_ch_free_string(&remote_maxcsn);
                if (NULL != retsdata)
                    ber_bvfree(retsdata);
            }
        }
    } else {
        if (strcasecmp(force, "yes") == 0) {
            /* We are forcing, we don't care that the replica is not online */
            rc = 0;
        }
    }
    conn_delete_internal_ext(conn);
    slapi_ch_free_string(&data);
    if (payload)
        ber_bvfree(payload);

    return rc;
}

/*
 * Before starting a cleanAllRUV task make sure there are not
 * too many task threads already running.  If everything is okay
 * also pre-set the RID now so rebounding extended ops do not
 * try to clean it over and over.
 */
int32_t
check_and_set_cleanruv_task_count(ReplicaId rid)
{
    int32_t rc = 0;

    PR_Lock(task_count_lock);
    if (clean_task_count >= CLEANRIDSIZ) {
        rc = -1;
    } else {
        clean_task_count++;
        preset_cleaned_rid(rid);
    }
    PR_Unlock(task_count_lock);

    return rc;
}

int32_t
check_and_set_abort_cleanruv_task_count(void)
{
    int32_t rc = 0;

    PR_Lock(task_count_lock);
    if (abort_task_count > CLEANRIDSIZ) {
        rc = -1;
    } else {
        abort_task_count++;
    }
    PR_Unlock(task_count_lock);

    return rc;
}

/*
 *  Notify sleeping CLEANALLRUV threads to stop
 */
void
stop_ruv_cleaning()
{
    pthread_mutex_lock(&notify_lock);
    pthread_cond_signal(&notify_cvar);
    pthread_mutex_unlock(&notify_lock);
}

/*
 *  Write our logging to the task and error log
 */
void
cleanruv_log(Slapi_Task *task, int rid, char *task_type, int sev_level, char *fmt, ...)
{
    va_list ap1;
    va_list ap2;
    va_list ap3;
    va_list ap4;
    char *errlog_fmt;

    va_start(ap1, fmt);
    va_start(ap2, fmt);
    va_start(ap3, fmt);
    va_start(ap4, fmt);

    if (task) {
        slapi_task_log_notice_ext(task, fmt, ap1);
        slapi_task_log_status_ext(task, fmt, ap2);
        slapi_task_inc_progress(task);
    }
    errlog_fmt = PR_smprintf("%s (rid %d): %s \n", task_type, rid, fmt);
    slapi_log_error_ext(sev_level, repl_plugin_name, errlog_fmt, ap3, ap4);
    slapi_ch_free_string(&errlog_fmt);

    va_end(ap1);
    va_end(ap2);
    va_end(ap3);
    va_end(ap4);
}

char *
replica_cleanallruv_get_local_maxcsn(ReplicaId rid, char *base_dn)
{
    Slapi_PBlock *search_pb = NULL;
    Slapi_Entry **entries = NULL;
    char **ruv_elements = NULL;
    char *maxcsn = NULL;
    char *filter = NULL;
    char *ridstr = NULL;
    char *iter = NULL;
    char *attrs[2];
    char *ruv_part = NULL;
    int part_count = 0;
    int res, i;

    /*
     *  Get the maxruv from the database tombstone entry
     */
    filter = "(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectclass=nstombstone))";
    attrs[0] = "nsds50ruv";
    attrs[1] = NULL;
    ridstr = slapi_ch_smprintf("{replica %d ldap", rid);

    search_pb = slapi_pblock_new();
    slapi_search_internal_set_pb(search_pb, base_dn, LDAP_SCOPE_SUBTREE, filter, attrs, 0, NULL, NULL, repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);

    if (LDAP_SUCCESS == res) {
        slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (NULL == entries || entries[0] == NULL) {
            /* Hmmm, no tombstone!  Error out */
        } else {
            /* find the right ruv element, and find the maxcsn */
            ruv_elements = slapi_entry_attr_get_charray(entries[0], attrs[0]);
            for (i = 0; ruv_elements && ruv_elements[i]; i++) {
                if (strstr(ruv_elements[i], ridstr)) {
                    /* get the max csn */
                    ruv_part = ldap_utf8strtok_r(ruv_elements[i], " ", &iter);
                    for (part_count = 1; ruv_part && part_count < 5; part_count++) {
                        ruv_part = ldap_utf8strtok_r(iter, " ", &iter);
                    }
                    if (part_count == 5 && ruv_part) { /* we have the maxcsn */
                        maxcsn = slapi_ch_strdup(ruv_part);
                        break;
                    }
                }
            }
            slapi_ch_array_free(ruv_elements);
        }
    } else {
        /* internal search failed */
        cleanruv_log(NULL, (int)rid, CLEANALLRUV_ID, SLAPI_LOG_ERR,
                     "replica_cleanallruv_get_local_maxcsn - Internal search failed (%d)\n", res);
    }

    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    slapi_ch_free_string(&ridstr);

    return maxcsn;
}
