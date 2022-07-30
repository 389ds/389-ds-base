/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2022 Red Hat, Inc.
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
#define REPLICA_RDN "cn=replica"
int slapi_log_urp = SLAPI_LOG_REPL;

/* Forward Declartions */
static int replica_config_add(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_modify(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_post_modify(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_delete(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_config_search(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg);
static int replica_csngen_test_task(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *eAfter, int *returncode, char *returntext, void *arg);
static int replica_config_change_type_and_id(Replica *r, const char *new_type, const char *new_id, char *returntext, int apply_mods);
static int replica_config_change_updatedn(Replica *r, const LDAPMod *mod, char *returntext, int apply_mods);
static int replica_config_change_updatedngroup(Replica *r, const LDAPMod *mod, char *returntext, int apply_mods);
static int replica_config_change_flags(Replica *r, const char *new_flags, char *returntext, int apply_mods);
static int replica_execute_task(Replica *r, const char *task_name, char *returntext, int apply_mods);
static int replica_execute_cl2ldif_task(Replica *r, char *returntext);
static int replica_execute_ldif2cl_task(Replica *r, char *returntext);
static int replica_cleanup_task(Object *r, const char *task_name, char *returntext, int apply_mods);
static int replica_task_done(Replica *replica);
static multisupplier_mtnode_extension *_replica_config_get_mtnode_ext(const Slapi_Entry *e);

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

    s_configLock = PR_NewLock();

    if (s_configLock == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, repl_plugin_name, "replica_config_init - "
                                                       "Failed to create configuration lock; NSPR error - %d\n",
                      PR_GetError());
        return -1;
    }

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

    /* Init all the cleanallruv stuff */
    rc = cleanallruv_init();

    return rc;
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
    multisupplier_mtnode_extension *mtnode_ext;
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
    multisupplier_mtnode_extension *mtnode_ext;
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
        (identity == repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION))) {
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
           if converting a read only replica to a supplier or vice versa -
           we will need to change both the replica ID and the type at the same
           time - we must disallow changing the replica ID if the type is not
           being changed and vice versa
        */
        char *new_repl_id = NULL;
        char *new_repl_type = NULL;

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
    multisupplier_mtnode_extension *mtnode_ext;
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
        (identity == repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION))) {
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
           if converting a read only replica to a supplier or vice versa -
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
    multisupplier_mtnode_extension *mtnode_ext;
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
    multisupplier_mtnode_extension *mtnode_ext;
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
                const char *purl = multisupplier_get_local_purl();

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
                                     repl_get_plugin_identity(PLUGIN_MULTISUPPLIER_REPLICATION),
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

static multisupplier_mtnode_extension *
_replica_config_get_mtnode_ext(const Slapi_Entry *e)
{
    const char *replica_root;
    Slapi_DN *sdn = NULL;
    mapping_tree_node *mtnode;
    multisupplier_mtnode_extension *ext = NULL;

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
        ext = (multisupplier_mtnode_extension *)repl_con_get_ext(REPL_CON_EXT_MTNODE, mtnode);
    }

    slapi_sdn_free(&sdn);

    return ext;
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
