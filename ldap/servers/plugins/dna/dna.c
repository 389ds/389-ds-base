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
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/**
 * Distributed Numeric Assignment plug-in
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include "portable.h"
#include "nspr.h"
#include "slapi-private.h"
#include "prclist.h"

/* Required to get portable printf/scanf format macros */
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>

#else
#error Need to define portable format macros such as PRIu64
#endif /* HAVE_INTTYPES_H */

/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif

#define DNA_PLUGIN_SUBSYSTEM "dna-plugin"
#define DNA_PLUGIN_VERSION 0x00020000

#define DNA_DN "cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config" /* temporary */

#define DNA_SUCCESS 0
#define DNA_FAILURE -1

/* Default range request timeout */
#define DNA_DEFAULT_TIMEOUT 10

/**
 * DNA config types
 */
#define DNA_TYPE            "dnaType"
#define DNA_PREFIX          "dnaPrefix"
#define DNA_NEXTVAL         "dnaNextValue"
#define DNA_INTERVAL        "dnaInterval"
#define DNA_GENERATE        "dnaMagicRegen"
#define DNA_FILTER          "dnaFilter"
#define DNA_SCOPE           "dnaScope"

/* since v2 */
#define DNA_MAXVAL          "dnaMaxValue"
#define DNA_SHARED_CFG_DN   "dnaSharedCfgDN"

/* Shared Config */
#define DNA_REMAINING       "dnaRemainingValues"
#define DNA_THRESHOLD       "dnaThreshold"
#define DNA_HOSTNAME        "dnaHostname"
#define DNA_PORTNUM         "dnaPortNum"
#define DNA_SECURE_PORTNUM  "dnaSecurePortNum"

/* For transferred ranges */
#define DNA_NEXT_RANGE            "dnaNextRange"
#define DNA_RANGE_REQUEST_TIMEOUT "dnaRangeRequestTimeout"

/* Replication types */
#define DNA_REPL_BIND_DN     "nsds5ReplicaBindDN"
#define DNA_REPL_CREDS       "nsds5ReplicaCredentials"
#define DNA_REPL_BIND_METHOD "nsds5ReplicaBindMethod"
#define DNA_REPL_TRANSPORT   "nsds5ReplicaTransportInfo"
#define DNA_REPL_PORT        "nsds5ReplicaPort"

#define DNA_FEATURE_DESC      "Distributed Numeric Assignment"
#define DNA_EXOP_FEATURE_DESC "DNA Range Extension Request"
#define DNA_PLUGIN_DESC       "Distributed Numeric Assignment plugin"
#define DNA_INT_PREOP_DESC    "Distributed Numeric Assignment internal preop plugin"
#define DNA_POSTOP_DESC       "Distributed Numeric Assignment postop plugin"
#define DNA_EXOP_DESC         "Distributed Numeric Assignment range extension extop plugin"

#define INTEGER_SYNTAX_OID                  "1.3.6.1.4.1.1466.115.121.1.27"

#define DNA_EXTEND_EXOP_REQUEST_OID  "2.16.840.1.113730.3.5.10"
#define DNA_EXTEND_EXOP_RESPONSE_OID "2.16.840.1.113730.3.5.11"

static Slapi_PluginDesc pdesc = { DNA_FEATURE_DESC,
                                  VENDOR,
                                  DS_PACKAGE_VERSION,
                                  DNA_PLUGIN_DESC };

static Slapi_PluginDesc exop_pdesc = { DNA_EXOP_FEATURE_DESC,
                                       VENDOR,
                                       DS_PACKAGE_VERSION,
                                       DNA_EXOP_DESC };


/**
 * linked list of config entries
 */

struct configEntry {
    PRCList list;
    char *dn;
    char *type;
    char *prefix;
    char *filter;
    Slapi_Filter *slapi_filter;
    char *generate;
    char *scope;
    PRUint64 interval;
    PRUint64 threshold;
    char *shared_cfg_base;
    char *shared_cfg_dn;
    PRUint64 timeout;
    /* This lock protects the 5 members below.  All
     * of the above members are safe to read as long
     * as you call dna_read_lock() first. */
    Slapi_Mutex *lock;
    PRUint64 nextval;
    PRUint64 maxval;
    PRUint64 remaining;
    PRUint64 next_range_lower;
    PRUint64 next_range_upper;
    /* This lock protects the extend_in_progress
     * member.  This is used to prevent us from
     * processing a range extention request and
     * trying to extend out own range at the same
     * time. */
    Slapi_Mutex *extend_lock;
    int extend_in_progress;
};

static PRCList *dna_global_config = NULL;
static PRRWLock *g_dna_cache_lock;

static void *_PluginID = NULL;
static char *_PluginDN = NULL;

static int g_plugin_started = 0;

static char *hostname = NULL;
static char *portnum = NULL;
static char *secureportnum = NULL;


/**
 * server struct for shared ranges
 */
struct dnaServer {
    PRCList list;
    char *host;
    unsigned int port;
    unsigned int secureport;
    PRUint64 remaining;
};

static char *dna_extend_exop_oid_list[] = {
    DNA_EXTEND_EXOP_REQUEST_OID,
    NULL
};


/**
 *
 * DNA plug-in management functions
 *
 */
int dna_init(Slapi_PBlock * pb);
static int dna_start(Slapi_PBlock * pb);
static int dna_close(Slapi_PBlock * pb);
static int dna_internal_preop_init(Slapi_PBlock *pb);
static int dna_postop_init(Slapi_PBlock * pb);
static int dna_exop_init(Slapi_PBlock * pb);

/**
 *
 * Local operation functions
 *
 */
static int dna_load_plugin_config();
static int dna_parse_config_entry(Slapi_Entry * e, int apply);
static void dna_delete_config();
static void dna_free_config_entry(struct configEntry ** entry);
static int dna_load_host_port();

/**
 *
 * helpers
 *
 */
static char *dna_get_dn(Slapi_PBlock * pb);
static int dna_dn_is_config(char *dn);
static int dna_get_next_value(struct configEntry * config_entry,
                                 char **next_value_ret);
static int dna_first_free_value(struct configEntry *config_entry,
                                PRUint64 *newval);
static int dna_fix_maxval(struct configEntry *config_entry);
static void dna_notice_allocation(struct configEntry *config_entry,
                                  PRUint64 new, PRUint64 last, int fix);
static int dna_update_shared_config(struct configEntry * config_entry);
static void dna_update_config_event(time_t event_time, void *arg);
static int dna_get_shared_servers(struct configEntry *config_entry, PRCList **servers);
static void dna_free_shared_server(struct dnaServer **server);
static void dna_delete_shared_servers(PRCList **servers);
static int dna_release_range(char *range_dn, PRUint64 *lower, PRUint64 *upper);
static int dna_request_range(struct configEntry *config_entry,
                             struct dnaServer *server,
                             PRUint64 *lower, PRUint64 *upper);
static struct berval *dna_create_range_request(char *range_dn);
static int dna_update_next_range(struct configEntry *config_entry,
                                 PRUint64 lower, PRUint64 upper);
static int dna_activate_next_range(struct configEntry *config_entry);
static int dna_is_replica_bind_dn(char *range_dn, char *bind_dn);
static int dna_get_replica_bind_creds(char *range_dn, struct dnaServer *server,
                                      char **bind_dn, char **bind_passwd,
                                      char **bind_method, int *is_ssl, int *port);

/**
 *
 * the ops (where the real work is done)
 *
 */
static int dna_config_check_post_op(Slapi_PBlock * pb);
static int dna_pre_op(Slapi_PBlock * pb, int modtype);
static int dna_mod_pre_op(Slapi_PBlock * pb);
static int dna_add_pre_op(Slapi_PBlock * pb);
static int dna_extend_exop(Slapi_PBlock *pb);

/**
 * debug functions - global, for the debugger
 */
void dna_dump_config();
void dna_dump_config_entry(struct configEntry *);

/**
 * set the debug level
 */
#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
    module_ldap_debug = level_ptr;
}
#endif

/**
 *
 * Deal with cache locking
 *
 */
void dna_read_lock()
{
    PR_RWLock_Rlock(g_dna_cache_lock);
}

void dna_write_lock()
{
    PR_RWLock_Wlock(g_dna_cache_lock);
}

void dna_unlock()
{
    PR_RWLock_Unlock(g_dna_cache_lock);
}

/**
 *
 * Get the dna plug-in version
 *
 */
int dna_version()
{
    return DNA_PLUGIN_VERSION;
}

/**
 * Plugin identity mgmt
 */
void setPluginID(void *pluginID)
{
    _PluginID = pluginID;
}

void *getPluginID()
{
    return _PluginID;
}

void setPluginDN(char *pluginDN)
{
    _PluginDN = pluginDN;
}

char *getPluginDN()
{
    return _PluginDN;
}

/*
	dna_init
	-------------
	adds our callbacks to the list
*/
int
dna_init(Slapi_PBlock *pb)
{
    int status = DNA_SUCCESS;
    char *plugin_identity = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_init\n");

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
                         (void *) dna_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *) dna_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_MODIFY_FN,
                         (void *) dna_mod_pre_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ADD_FN,
                         (void *) dna_add_pre_op) != 0 ||
        /* internal preoperation */
        slapi_register_plugin("internalpreoperation",  /* op type */
                              1,        /* Enabled */
                              "dna_init",   /* this function desc */
                              dna_internal_preop_init,  /* init func */
                              DNA_INT_PREOP_DESC,      /* plugin desc */
                              NULL,     /* ? */
                              plugin_identity   /* access control */
        ) ||
        /* the config change checking post op */
        slapi_register_plugin("postoperation",  /* op type */
                              1,        /* Enabled */
                              "dna_init",   /* this function desc */
                              dna_postop_init,  /* init func for post op */
                              DNA_POSTOP_DESC,      /* plugin desc */
                              NULL,     /* ? */
                              plugin_identity   /* access control */
        ) ||
        /* the range extension extended operation */
        slapi_register_plugin("extendedop", /* op type */
                              1,        /* Enabled */
                              "dna_init",   /* this function desc */
                              dna_exop_init,  /* init func for exop */
                              DNA_EXOP_DESC,      /* plugin desc */
                              NULL,     /* ? */
                              plugin_identity   /* access control */
        )
        ) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_init: failed to register plugin\n");
        status = DNA_FAILURE;
    }

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_init\n");
    return status;
}

static int
dna_internal_preop_init(Slapi_PBlock *pb)
{
    int status = DNA_SUCCESS;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN,
                         (void *) dna_mod_pre_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN,
                         (void *) dna_add_pre_op) != 0) {
        status = DNA_FAILURE;
    }
 
    return status;
}

static int
dna_postop_init(Slapi_PBlock *pb)
{
    int status = DNA_SUCCESS;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN,
                         (void *) dna_config_check_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODRDN_FN,
                         (void *) dna_config_check_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_DELETE_FN,
                         (void *) dna_config_check_post_op) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN,
                         (void *) dna_config_check_post_op) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_postop_init: failed to register plugin\n");
        status = DNA_FAILURE;
    }

    return status;
}


static int
dna_exop_init(Slapi_PBlock * pb)
{
    int status = DNA_SUCCESS;

    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *) &exop_pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_EXT_OP_OIDLIST,
                         (void *) dna_extend_exop_oid_list) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_EXT_OP_FN,
                         (void *) dna_extend_exop) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_exop_init: failed to register plugin\n");
        status = DNA_FAILURE;
    }

    return status;
}


/*
	dna_start
	--------------
	Kicks off the config cache.
	It is called after dna_init.
*/
static int
dna_start(Slapi_PBlock * pb)
{
    char *plugindn = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_start\n");

    /* Check if we're already started */
    if (g_plugin_started) {
        goto done;
    }

    g_dna_cache_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "dna");

    if (!g_dna_cache_lock) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_start: lock creation failed\n");

        return DNA_FAILURE;
    }

    /**
	 *	Get the plug-in target dn from the system
	 *	and store it for future use. This should avoid
	 *	hardcoding of DN's in the code.
	 */
    slapi_pblock_get(pb, SLAPI_TARGET_DN, &plugindn);
    if (NULL == plugindn || 0 == strlen(plugindn)) {
        slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                        "dna_start: had to use hard coded config dn\n");
        plugindn = DNA_DN;
    } else {
        slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                        "dna_start: config at %s\n", plugindn);

    }

    setPluginDN(plugindn);

    /* We need the host and port number of this server
     * in case shared config is enabled for any of the
     * ranges we are managing. */
    if (dna_load_host_port() != DNA_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_start: unable to load host and port information\n");
    }

    /*
     * Load the config for our plug-in
     */
    dna_global_config = (PRCList *)
        slapi_ch_calloc(1, sizeof(struct configEntry));
    PR_INIT_CLIST(dna_global_config);

    if (dna_load_plugin_config() != DNA_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_start: unable to load plug-in configuration\n");
        return DNA_FAILURE;
    }

    g_plugin_started = 1;
    slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                    "dna: ready for service\n");
    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_start\n");

done:
    return DNA_SUCCESS;
}

/*
	dna_close
	--------------
	closes down the cache
*/
static int
dna_close(Slapi_PBlock * pb)
{
    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_close\n");

    dna_delete_config();

    slapi_ch_free((void **)&dna_global_config);

    slapi_ch_free_string(&hostname);
    slapi_ch_free_string(&portnum);
    slapi_ch_free_string(&secureportnum);

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_close\n");

    return DNA_SUCCESS;
}

/*
 * config looks like this
 * - cn=myplugin
 * --- cn=posix
 * ------ cn=accounts
 * ------ cn=groups
 * --- cn=samba
 * --- cn=etc
 * ------ cn=etc etc
 */
static int
dna_load_plugin_config()
{
    int status = DNA_SUCCESS;
    int result;
    int i;
    time_t now;
    Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_load_plugin_config\n");

    dna_write_lock();
    dna_delete_config();

    search_pb = slapi_pblock_new();

    slapi_search_internal_set_pb(search_pb, getPluginDN(),
                                 LDAP_SCOPE_SUBTREE, "objectclass=*",
                                 NULL, 0, NULL, NULL, getPluginID(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

    if (LDAP_SUCCESS != result) {
        status = DNA_FAILURE;
        goto cleanup;
    }

    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                     &entries);
    if (NULL == entries || NULL == entries[0]) {
        status = DNA_SUCCESS;
        goto cleanup;
    }

    for (i = 0; (entries[i] != NULL); i++) {
        /* We don't care about the status here because we may have
         * some invalid config entries, but we just want to continue
         * looking for valid ones. */
        dna_parse_config_entry(entries[i], 1);
    }

    /* Setup an event to update the shared config 30
     * seconds from now.  We need to do this since
     * performing the operation at this point when
     * starting up  would cause the change to not
     * get changelogged. */
    time(&now);
    slapi_eq_once(dna_update_config_event, NULL, now + 30);

  cleanup:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
    dna_unlock();
    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_load_plugin_config\n");

    return status;
}

/*
 * dna_parse_config_entry()
 *
 * Parses a single config entry.  If apply is non-zero, then
 * we will load and start using the new config.  You can simply
 * validate config without making any changes by setting apply
 * to 0.
 *
 * Returns DNA_SUCCESS if the entry is valid and DNA_FAILURE
 * if it is invalid.
 */
static int
dna_parse_config_entry(Slapi_Entry * e, int apply)
{
    char *value;
    struct configEntry *entry = NULL;
    struct configEntry *config_entry;
    PRCList *list;
    int entry_added = 0;
    int ret = DNA_SUCCESS;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_parse_config_entry\n");

    /* If this is the main DNA plug-in
     * config entry, just bail. */
    if (strcasecmp(getPluginDN(), slapi_entry_get_ndn(e)) == 0) {
        ret = DNA_FAILURE;
        goto bail;
    }

    entry = (struct configEntry *)
	slapi_ch_calloc(1, sizeof(struct configEntry));
    if (NULL == entry) {
        ret = DNA_FAILURE;
        goto bail;
    }

    value = slapi_entry_get_ndn(e);
    if (value) {
        entry->dn = slapi_ch_strdup(value);
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> dn [%s]\n", entry->dn);

    value = slapi_entry_attr_get_charptr(e, DNA_TYPE);
    if (value) {
        entry->type = value;
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_parse_config_entry: The %s config "
                        "setting is required for range %s.\n",
                        DNA_TYPE, entry->dn);
        ret = DNA_FAILURE;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%s]\n", DNA_TYPE, entry->type);

    value = slapi_entry_attr_get_charptr(e, DNA_NEXTVAL);
    if (value) {
        entry->nextval = strtoull(value, 0, 0);
        slapi_ch_free_string(&value);
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_parse_config_entry: The %s config "
                        "setting is required for range %s.\n",
                        DNA_NEXTVAL, entry->dn);
        ret = DNA_FAILURE;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%" NSPRIu64 "]\n", DNA_NEXTVAL, entry->nextval);

    value = slapi_entry_attr_get_charptr(e, DNA_PREFIX);
    if (value && value[0]) {
        entry->prefix = value;
    } else {
        /* TODO - If a prefix is not  defined, then we need to ensure
         * that the proper matching rule is in place for this
         * attribute type.  We require this since we internally
         * perform a sorted range search on what we assume to
         * be an INTEGER syntax. */
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%s]\n", DNA_PREFIX, entry->prefix);

    /* Set the default interval to 1 */
    entry->interval = 1;

#ifdef DNA_ENABLE_INTERVAL
    value = slapi_entry_attr_get_charptr(e, DNA_INTERVAL);
    if (value) {
        entry->interval = strtoull(value, 0, 0);
        slapi_ch_free_string(&value);
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%" NSPRIu64 "]\n", DNA_INTERVAL, entry->interval);
#endif

    value = slapi_entry_attr_get_charptr(e, DNA_GENERATE);
    if (value) {
        entry->generate = value;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%s]\n", DNA_GENERATE, entry->generate);

    value = slapi_entry_attr_get_charptr(e, DNA_FILTER);
    if (value) {
        entry->filter = value;
        if (NULL == (entry->slapi_filter = slapi_str2filter(value))) {
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM ,
                "Error: Invalid search filter in entry [%s]: [%s]\n",
                entry->dn, value);
            ret = DNA_FAILURE;
            goto bail;
        }
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_parse_config_entry: The %s config "
                        "setting is required for range %s.\n",
                        DNA_FILTER, entry->dn);
        ret = DNA_FAILURE;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%s]\n", DNA_FILTER, value);

    value = slapi_entry_attr_get_charptr(e, DNA_SCOPE);
    if (value) {
        /* TODO - Allow multiple scope settings for a single range.  This may
         * make ordering the scopes tough when we put them in the clist. */
        entry->scope = slapi_dn_normalize(value);
    } else {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_parse_config_entry: The %s config "
                        "config setting is required for range %s.\n",
                        DNA_SCOPE, entry->dn);
        ret = DNA_FAILURE;
        goto bail;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%s]\n", DNA_SCOPE, entry->scope);

    /* optional, if not specified set -1  which is converted to the max unisgnee
     * value */
    value = slapi_entry_attr_get_charptr(e, DNA_MAXVAL);
    if (value) {
            entry->maxval = strtoull(value, 0, 0);
            slapi_ch_free_string(&value);
    } else {
        entry->maxval = -1;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%" NSPRIu64 "]\n", DNA_MAXVAL, entry->maxval);

    value = slapi_entry_attr_get_charptr(e, DNA_SHARED_CFG_DN);
    if (value) {
        Slapi_Entry *shared_e = NULL;
        Slapi_DN *sdn = NULL;

        sdn = slapi_sdn_new_dn_byref(value);

        if (sdn) {
            slapi_search_internal_get_entry(sdn, NULL, &shared_e, getPluginID());
            slapi_sdn_free(&sdn);
        }

        /* Make sure that the shared config entry exists. */
        if (!shared_e) {
            /* We didn't locate the shared config container entry. Log
             * a message and skip this config entry. */
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_parse_config_entry: Unable to locate "
                            "shared configuration entry (%s)\n", value);
            ret = DNA_FAILURE;
            goto bail;
        } else {
            slapi_entry_free(shared_e);
            shared_e = NULL;
        }

        entry->shared_cfg_base = slapi_dn_normalize(value);

        /* We prepend the host & port of this instance as a
         * multi-part RDN for the shared config entry. */
        entry->shared_cfg_dn = slapi_ch_smprintf("%s=%s+%s=%s,%s", DNA_HOSTNAME,
                                          hostname, DNA_PORTNUM, portnum, value);
        slapi_dn_normalize(entry->shared_cfg_dn);

        slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                        "----------> %s [%s]\n", DNA_SHARED_CFG_DN,
                        entry->shared_cfg_base);
    }

    value = slapi_entry_attr_get_charptr(e, DNA_THRESHOLD);
    if (value) {
        entry->threshold = strtoull(value, 0, 0);

        slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                        "----------> %s [%" NSPRIu64 "]\n", DNA_THRESHOLD, value);

        slapi_ch_free_string(&value);
    } else {
        entry->threshold = 1;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%" NSPRIu64 "]\n", DNA_THRESHOLD, entry->threshold);

    value = slapi_entry_attr_get_charptr(e, DNA_RANGE_REQUEST_TIMEOUT);
    if (value) {
        entry->timeout = strtoull(value, 0, 0);
        slapi_ch_free_string(&value);
    } else {
        entry->timeout = DNA_DEFAULT_TIMEOUT;
    }

    slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                    "----------> %s [%" NSPRIu64 "]\n", DNA_RANGE_REQUEST_TIMEOUT,
                    entry->timeout);

    value = slapi_entry_attr_get_charptr(e, DNA_NEXT_RANGE);
    if (value) {
        char *p = NULL;

        /* the next range value is in the form "<lower>-<upper>" */
        if ((p = strstr(value, "-")) != NULL) {
            *p = '\0';
            ++p;
            entry->next_range_lower = strtoull(value, 0, 0);
            entry->next_range_upper = strtoull(p, 0, 0);

            /* validate that upper is greater than lower */
            if (entry->next_range_upper <= entry->next_range_lower) {
                slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                                "dna_parse_config_entry: Illegal %s "
                                "setting specified for range %s.  Legal "
                                "format is <lower>-<upper>.\n",
                                DNA_NEXT_RANGE, entry->dn);
                ret = DNA_FAILURE;
                entry->next_range_lower = 0;
                entry->next_range_upper = 0;
            }           

            /* make sure next range doesn't overlap with
             * the active range */
            if (((entry->next_range_upper <= entry->maxval) &&
                 (entry->next_range_upper >= entry->nextval)) ||
                ((entry->next_range_lower <= entry->maxval) &&
                 (entry->next_range_lower >= entry->nextval))) {
                slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_parse_config_entry: Illegal %s "
                            "setting specified for range %s.  %s "
                            "overlaps with the active range.\n",
                            DNA_NEXT_RANGE, entry->dn, DNA_NEXT_RANGE);
                ret = DNA_FAILURE;
                entry->next_range_lower = 0;
                entry->next_range_upper = 0;
            }
        } else {
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_parse_config_entry: Illegal %s "
                            "setting specified for range %s.  Legal "
                            "format is <lower>-<upper>.\n",
                            DNA_NEXT_RANGE, entry->dn);
            ret = DNA_FAILURE;
        }
 
        slapi_ch_free_string(&value);
    }

    /* If we were only called to validate config, we can
     * just bail out before applying the config changes */
    if (apply == 0) {
        goto bail;
    }

    /* Calculate number of remaining values. */
    if (entry->next_range_lower != 0) {
        entry->remaining = ((entry->next_range_upper - entry->next_range_lower + 1) /
                             entry->interval) + ((entry->maxval - entry->nextval + 1) /
                             entry->interval);
    } else if (entry->nextval >= entry->maxval) {
        entry->remaining = 0;
    } else {
        entry->remaining = ((entry->maxval - entry->nextval + 1) /
                             entry->interval);
    }

    /* create the new value lock for this range */
    entry->lock = slapi_new_mutex();
    if (!entry->lock) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_parse_config_entry: Unable to create lock "
                        "for range %s.\n", entry->dn);
        ret = DNA_FAILURE;
        goto bail;
    }

    /**
     * Finally add the entry to the list
     * we group by type then by filter
     * and finally sort by dn length with longer dn's
     * first - this allows the scope checking
     * code to be simple and quick and
     * cunningly linear
     */
    if (!PR_CLIST_IS_EMPTY(dna_global_config)) {
        list = PR_LIST_HEAD(dna_global_config);
        while (list != dna_global_config) {
            config_entry = (struct configEntry *) list;

            if (slapi_attr_type_cmp(config_entry->type, entry->type, 1))
                goto next;

            if (slapi_filter_compare(config_entry->slapi_filter,
                                     entry->slapi_filter))
                goto next;

            if (slapi_dn_issuffix(entry->scope, config_entry->scope)) {
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG,
                                DNA_PLUGIN_SUBSYSTEM,
                                "store [%s] before [%s] \n", entry->scope,
                                config_entry->scope);
                entry_added = 1;
                break;
            }

          next:
            list = PR_NEXT_LINK(list);

            if (dna_global_config == list) {
                /* add to tail */
                PR_INSERT_BEFORE(&(entry->list), list);
                slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                                "store [%s] at tail\n", entry->scope);
                entry_added = 1;
                break;
            }
        }
    } else {
        /* first entry */
        PR_INSERT_LINK(&(entry->list), dna_global_config);
        slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                        "store [%s] at head \n", entry->scope);
        entry_added = 1;
    }

  bail:
    if (0 == entry_added) {
        /* Don't log error if we weren't asked to apply config */
        if ((apply != 0) && (entry != NULL)) {
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_parse_config_entry: Invalid config entry "
                            "[%s] skipped\n", entry->dn);
        }
        dna_free_config_entry(&entry);
    } else {
        ret = DNA_SUCCESS;
    }

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_parse_config_entry\n");

    return ret;
}

static void
dna_free_config_entry(struct configEntry ** entry)
{
    struct configEntry *e = *entry;

    if (e == NULL)
        return;

    if (e->dn) {
        slapi_log_error(SLAPI_LOG_CONFIG, DNA_PLUGIN_SUBSYSTEM,
                        "freeing config entry [%s]\n", e->dn);
        slapi_ch_free_string(&e->dn);
    }

    if (e->type)
        slapi_ch_free_string(&e->type);

    if (e->prefix)
        slapi_ch_free_string(&e->prefix);

    if (e->filter)
        slapi_ch_free_string(&e->filter);

    if (e->slapi_filter)
        slapi_filter_free(e->slapi_filter, 1);

    if (e->generate)
        slapi_ch_free_string(&e->generate);

    if (e->scope)
        slapi_ch_free_string(&e->scope);

    if (e->shared_cfg_base)
        slapi_ch_free_string(&e->shared_cfg_base);

    if (e->shared_cfg_dn)
        slapi_ch_free_string(&e->shared_cfg_dn);

    if (e->lock)
        slapi_destroy_mutex(e->lock);

    slapi_ch_free((void **) entry);
}

static void
dna_delete_configEntry(PRCList *entry)
{
    PR_REMOVE_LINK(entry);
    dna_free_config_entry((struct configEntry **) &entry);
}

static void
dna_delete_config()
{
    PRCList *list;

    while (!PR_CLIST_IS_EMPTY(dna_global_config)) {
        list = PR_LIST_HEAD(dna_global_config);
        dna_delete_configEntry(list);
    }

    return;
}

static void
dna_free_shared_server(struct dnaServer **server)
{
    struct dnaServer *s = *server;

    slapi_ch_free_string(&s->host);

    slapi_ch_free((void **)server);
}

static void
dna_delete_shared_servers(PRCList **servers)
{
    PRCList *server;

    while (!PR_CLIST_IS_EMPTY(*servers)) {
        server = PR_LIST_HEAD(*servers);
        PR_REMOVE_LINK(server);
        dna_free_shared_server((struct dnaServer **)&server);
    }

    slapi_ch_free((void **)servers);
    *servers = NULL;

    return;
}

static int
dna_load_host_port()
{
    int status = DNA_SUCCESS;
    Slapi_Entry *e = NULL;
    Slapi_DN *config_dn = NULL;
    char *attrs[4];

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_load_host_port\n");

    attrs[0] = "nsslapd-localhost";
    attrs[1] = "nsslapd-port";
    attrs[2] = "nsslapd-secureport";
    attrs[3] = NULL;

    config_dn = slapi_sdn_new_dn_byref("cn=config");
    if (config_dn) {
        slapi_search_internal_get_entry(config_dn, attrs, &e, getPluginID());
        slapi_sdn_free(&config_dn);
    }

    if (e) {
        hostname = slapi_entry_attr_get_charptr(e, "nsslapd-localhost");
        portnum = slapi_entry_attr_get_charptr(e, "nsslapd-port");
        secureportnum = slapi_entry_attr_get_charptr(e, "nsslapd-secureport");
        slapi_entry_free(e);
    }

    if (!hostname || !portnum) {
        status = DNA_FAILURE;
    }

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_load_host_port\n");

    return status;
}

/*
 * dna_update_config_event()
 *
 * Event queue callback that we use to do the initial
 * update of the shared config entries shortly after
 * startup.
 */
static void
dna_update_config_event(time_t event_time, void *arg)
{
    Slapi_PBlock *pb = NULL;
    struct configEntry *config_entry = NULL;
    PRCList *list = NULL;

    /* Get read lock to prevent config changes */
    dna_read_lock();

    /* Loop through all config entries and update the shared
     * config entries. */
    if (!PR_CLIST_IS_EMPTY(dna_global_config)) {
        list = PR_LIST_HEAD(dna_global_config);

        /* Create the pblock.  We'll reuse this for all
         * shared config updates. */
        if ((pb = slapi_pblock_new()) == NULL)
            goto bail;

        while (list != dna_global_config) {
            config_entry = (struct configEntry *) list;

            /* If a shared config dn is set, update the shared config. */
            if (config_entry->shared_cfg_dn != NULL) {
                slapi_lock_mutex(config_entry->lock);

                /* First delete the existing shared config entry.  This
                 * will allow the entry to be updated for things like
                 * port number changes, etc. */
                slapi_delete_internal_set_pb(pb, config_entry->shared_cfg_dn,
                                             NULL, NULL, getPluginID(), 0);

                /* We don't care about the results */
                slapi_delete_internal_pb(pb);

                /* Now force the entry to be recreated */
                dna_update_shared_config(config_entry);

                slapi_unlock_mutex(config_entry->lock);
                slapi_pblock_init(pb);
            }

            list = PR_NEXT_LINK(list);
        }
    }

bail:
    dna_unlock();
    slapi_pblock_destroy(pb);
}

/****************************************************
    Distributed ranges Helpers
****************************************************/

/*
 * dna_fix_maxval()
 *
 * Attempts to extend the range represented by
 * config_entry.
 *
 * The lock for configEntry should be obtained
 * before calling this function.
 */
static int dna_fix_maxval(struct configEntry *config_entry)
{
    PRCList *servers = NULL;
    PRCList *server = NULL;
    PRUint64 lower = 0;
    PRUint64 upper = 0;
    int ret = LDAP_OPERATIONS_ERROR;

    if (config_entry == NULL) {
        goto bail;
    } 

    /* If we already have a next range we only need
     * to activate it. */
    if (config_entry->next_range_lower != 0) {
        ret = dna_activate_next_range(config_entry);
        if (ret != 0) {
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_fix_maxval: Unable to activate the "
                            "next range for range %s.\n", config_entry->dn);
        }
    } else if (config_entry->shared_cfg_base) {
        /* Find out if there are any other servers to request
         * range from. */
        dna_get_shared_servers(config_entry, &servers);

        if (servers) {
            /* We have other servers we can try to extend
             * our range from.  Loop through them and
             * request range until someone gives us some
             * values, or we hit the end of the list. */
            server = PR_LIST_HEAD(servers);
            while (server != servers) {
                if (dna_request_range(config_entry, (struct dnaServer *)server,
                                      &lower, &upper) != 0) {
                    server = PR_NEXT_LINK(server);
                } else {
                    /* Someone provided us with a new range. Attempt 
                     * to update the config. */
                    if ((ret = dna_update_next_range(config_entry, lower, upper)) == 0) {
                        break;
                    }
                }
            }

            /* free the list of servers */
            dna_delete_shared_servers(&servers);
        }
    }

bail:
    return ret;
}

/* dna_notice_allocation()
 *
 * Called after a new value has been allocated from the range.
 * This function will update the config entries and cached info
 * appropriately.  This includes activating the next range if
 * we've exhausted the current range. 
 *
 * The last parameter is the value that has just been allocated.
 * The new parameter should be the next available value. If you
 * set both of these parameters to 0, then this function will
 * just check if the next range needs to be activated and update
 * the config accordingly. 
 *
 * The lock for configEntry should be obtained before calling
 * this function. */
static void
dna_notice_allocation(struct configEntry *config_entry, PRUint64 new,
                                  PRUint64 last, int fix)
{
    /* update our cached config entry */
    if ((new != 0) && (new <= (config_entry->maxval + config_entry->interval))) {
        config_entry->nextval = new;
    }

    /* check if we've exhausted our active range */
    if ((last == config_entry->maxval) || 
        (config_entry->nextval > config_entry->maxval)) {
        /* If we already have a next range set, make it the
         * new active range. */
        if (config_entry->next_range_lower != 0) {
            /* Make the next range active */
            if (dna_activate_next_range(config_entry) != 0) {
                slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                                "dna_notice_allocation: Unable to activate "
                                "the next range for range %s.\n", config_entry->dn);
            }
        } else {
            config_entry->remaining = 0;
            /* update the shared configuration */
            dna_update_shared_config(config_entry);
        }
    } else {
        if (config_entry->next_range_lower != 0) {
            config_entry->remaining = ((config_entry->maxval - config_entry->nextval + 1) /
                                    config_entry->interval) + ((config_entry->next_range_upper -
                                    config_entry->next_range_lower +1) / config_entry->interval);
        } else {
            config_entry->remaining = ((config_entry->maxval - config_entry->nextval + 1) /
                                    config_entry->interval);
        }

        /* update the shared configuration */
        dna_update_shared_config(config_entry);
    }

    /* Check if we passed the threshold and try to fix maxval if so.  We
     * don't need to do this if we already have a next range on deck. */
    if ((config_entry->next_range_lower == 0) && (config_entry->remaining <= config_entry->threshold)) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_notice_allocation: Passed threshold of %" NSPRIu64 " remaining values "
                        "for range %s. (%" NSPRIu64 " values remain)\n",
                        config_entry->threshold, config_entry->dn, config_entry->remaining);
        /* Only attempt to fix maxval if the fix flag is set. */
        if (fix != 0) {
            dna_fix_maxval(config_entry);
        }
    }

    return;
}

static int
dna_get_shared_servers(struct configEntry *config_entry, PRCList **servers)
{
    int ret = LDAP_SUCCESS;
    Slapi_PBlock *pb = NULL;
    Slapi_Entry **entries = NULL;
    char *attrs[5];

    /* First do a search in the shared config area for this
     * range to find other servers who are managing this range. */
    attrs[0] = DNA_HOSTNAME;
    attrs[1] = DNA_PORTNUM;
    attrs[2] = DNA_SECURE_PORTNUM;
    attrs[3] = DNA_REMAINING;
    attrs[4] = NULL;

    pb = slapi_pblock_new();
    if (NULL == pb) {
        ret = LDAP_OPERATIONS_ERROR;
        goto cleanup;
    }

    slapi_search_internal_set_pb(pb, config_entry->shared_cfg_base,
                                 LDAP_SCOPE_ONELEVEL, "objectclass=*",
                                 attrs, 0, NULL,
                                 NULL, getPluginID(), 0);
    slapi_search_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (LDAP_SUCCESS != ret) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_get_shared_servers: search failed for shared "
                        "config: %s [error %d]\n", config_entry->shared_cfg_base,
                        ret);
        goto cleanup;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                     &entries);

    if (entries && entries[0]) {
        Slapi_DN *cfg_sdn = NULL;
        int i;

        cfg_sdn = slapi_sdn_new_dn_byref(config_entry->shared_cfg_dn);

        /* We found some entries.  Go through them and
         * order them based off of remaining values. */
        for (i = 0; entries[i]; i++) {
            /* skip our own shared config entry */
            if (slapi_sdn_compare(cfg_sdn, slapi_entry_get_sdn(entries[i]))) {
                struct dnaServer *server = NULL;

                /* set up the server list entry */
                server = (struct dnaServer *) slapi_ch_calloc(1,
                         sizeof(struct dnaServer));
                server->host = slapi_entry_attr_get_charptr(entries[i],
                                                            DNA_HOSTNAME);
                server->port = slapi_entry_attr_get_uint(entries[i], DNA_PORTNUM);
                server->secureport = slapi_entry_attr_get_uint(entries[i], DNA_SECURE_PORTNUM);
                server->remaining = slapi_entry_attr_get_ulonglong(entries[i],
                                                                   DNA_REMAINING);

                /* validate the entry */
                if (!server->host || server->port == 0 || server->remaining == 0) {
                    /* free and skip this one */
                    slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                                    "dna_get_shared_servers: skipping invalid "
                                    "shared config entry (%s)\n", slapi_entry_get_dn(entries[i]));
                    dna_free_shared_server(&server);
                    continue;
                }

                /* add a server entry to the list */
                if (*servers == NULL) {
                    /* first entry */
                    *servers = (PRCList *) slapi_ch_calloc(1,
                               sizeof(struct dnaServer));
                    PR_INIT_CLIST(*servers);
                    PR_INSERT_LINK(&(server->list), *servers);
                } else {
                    /* Find the right slot for this entry. We
                     * want to order the entries based off of
                     * the remaining number of values, higest
                     * to lowest. */
                    struct dnaServer *sitem;
                    PRCList* item = PR_LIST_HEAD(*servers);

                    while (item != *servers) {
                        sitem = (struct dnaServer *)item;
                        if (server->remaining > sitem->remaining) {
                            PR_INSERT_BEFORE(&(server->list), item);
                            break;
                        }

                        item = PR_NEXT_LINK(item);

                        if (*servers == item) {
                            /* add to tail */
                            PR_INSERT_BEFORE(&(server->list), item);
                            break;
                        }
                    }
                }
            }
        }

        slapi_sdn_free(&cfg_sdn);
    }


  cleanup:
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
    return ret;
}

/*
 * dna_request_range()
 *
 * Requests range extension from another server.
 * Returns 0 on success and will fill in upper
 * and lower.  Returns non-0 on failure and will
 * zero out upper and lower.
 */
static int dna_request_range(struct configEntry *config_entry,
                             struct dnaServer *server,
                             PRUint64 *lower, PRUint64 *upper)
{
    char *bind_dn = NULL;
    char *bind_passwd = NULL;
    char *bind_method = NULL;
    int is_ssl = 0;
    struct berval *request = NULL;
    char *retoid = NULL;
    struct berval *responsedata = NULL;
    BerElement *respber = NULL;
    LDAP *ld = NULL;
    char *lower_str = NULL;
    char *upper_str = NULL;
    int set_extend_flag = 0;
    int ret = LDAP_OPERATIONS_ERROR;
    int port = 0;
    int timelimit;
#if defined(USE_OPENLDAP)
    struct timeval timeout;
#endif
    /* See if we're allowed to send a range request now */
    slapi_lock_mutex(config_entry->extend_lock);
    if (config_entry->extend_in_progress) {
        /* We're already processing a range extention, so bail. */
        slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                        "dna_request_range: Already processing a "
                        "range extension request.  Skipping request.\n");
        slapi_unlock_mutex(config_entry->extend_lock);
        goto bail;
    } else {
        /* Set a flag indicating that we're attempting to extend this range */
        config_entry->extend_in_progress = 1;
        set_extend_flag = 1;
        slapi_unlock_mutex(config_entry->extend_lock);
    }

    /* Fetch the replication bind dn info */
    if (dna_get_replica_bind_creds(config_entry->shared_cfg_base, server,
                                   &bind_dn, &bind_passwd, &bind_method,
                                   &is_ssl, &port) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_request_range: Unable to retrieve "
                        "replica bind credentials.\n");
        goto bail;
    }

    if ((request = dna_create_range_request(config_entry->shared_cfg_base)) == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_request_range: Failed to create "
                        "range extension extended operation request.\n");
        goto bail;
    }

    if ((ld = slapi_ldap_init(server->host, port, is_ssl, 0)) == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_request_range: Unable to "
                        "initialize LDAP session to server %s:%u.\n",
                         server->host, server->port);
        goto bail;
    }

    /* Disable referrals and set timelimit and a connect timeout */
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);
    timelimit = config_entry->timeout / 1000; /* timeout is in msec */
    ldap_set_option(ld, LDAP_OPT_TIMELIMIT, &timelimit);
#if defined(USE_OPENLDAP)
    timeout.tv_sec = config_entry->timeout / 1000;
    timeout.tv_usec = (config_entry->timeout % 1000) * 1000;
    ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &timeout);
#else
    ldap_set_option(ld, LDAP_X_OPT_CONNECT_TIMEOUT, &config_entry->timeout);
#endif
    /* Bind to the replica server */
    ret = slapi_ldap_bind(ld, bind_dn, bind_passwd, bind_method,
                          NULL, NULL, NULL, NULL);

    if (ret != LDAP_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_request_range: Error binding "
                        " to replica server %s:%u. [error %d]\n",
                        server->host, server->port, ret);
        goto bail;
    }

    /* Send range extension request */
    ret = ldap_extended_operation_s(ld, DNA_EXTEND_EXOP_REQUEST_OID,
                        request, NULL, NULL, &retoid, &responsedata);

    if (ret != LDAP_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_request_range: Error sending "
                        "range extension extended operation request "
                        "to server %s:%u [error %d]\n", server->host,
                        server->port, ret);
        goto bail;
    }

    /* Verify that the OID is correct. */
    if (strcmp(retoid, DNA_EXTEND_EXOP_RESPONSE_OID) != 0) {
        ret = LDAP_OPERATIONS_ERROR;
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_request_range: Received incorrect response OID.\n");
        goto bail;
    }

    /* Parse response */
    if (responsedata) {
        respber = ber_init(responsedata);
        ber_scanf(respber, "{aa}", &lower_str, &upper_str);
    }

    /* Fill in upper and lower */
    if (upper_str && lower_str) {
        *upper = strtoull(upper_str, 0, 0);
        *lower = strtoull(lower_str, 0, 0);
        ret = 0;
    } else {
        ret = LDAP_OPERATIONS_ERROR;
    }

bail:
    if (set_extend_flag) {
        slapi_lock_mutex(config_entry->extend_lock);
        config_entry->extend_in_progress = 0;
        slapi_unlock_mutex(config_entry->extend_lock);
    }
    slapi_ldap_unbind(ld);
    slapi_ch_free_string(&bind_dn);
    slapi_ch_free_string(&bind_passwd);
    slapi_ch_free_string(&bind_method);
    slapi_ch_free_string(&retoid);
    slapi_ch_free_string(&lower_str);
    slapi_ch_free_string(&upper_str);
    ber_free(respber, 1);
    ber_bvfree(request);
    ber_bvfree(responsedata);

    if (ret != 0) {
        *upper = 0;
        *lower = 0;
    }

    return ret;
}

static struct berval *dna_create_range_request(char *range_dn)
{
    struct berval *requestdata = NULL;
    struct berval shared_dn = { 0, NULL };
    BerElement    *ber = NULL;

    shared_dn.bv_val = range_dn;
    shared_dn.bv_len = strlen(shared_dn.bv_val);

    if((ber = ber_alloc()) == NULL) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_create_range_request: Error "
                        "allocating request data.\n");
        goto bail;
    }

    if (LBER_ERROR == (ber_printf(ber, "{o}", shared_dn.bv_val, shared_dn.bv_len))) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_create_range_request: Error "
                        "encoding request data.\n");
        goto bail;
    }

    if (ber_flatten(ber, &requestdata) == -1) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_create_range_request: Error "
                        "encoding request data.\n");
        goto bail;
    }

bail:
    ber_free(ber, 1);

    return requestdata;
}

/****************************************************
	Helpers
****************************************************/

static char *dna_get_dn(Slapi_PBlock * pb)
{
    char *dn = 0;
    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_get_dn\n");

    if (slapi_pblock_get(pb, SLAPI_TARGET_DN, &dn)) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_get_dn: failed to get dn of changed entry");
        goto bail;
    }

/*        slapi_dn_normalize( dn );
*/
  bail:
    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_get_dn\n");

    return dn;
}

/* config check
        matching config dn or a descendent reloads config
*/
static int dna_dn_is_config(char *dn)
{
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_is_config\n");

    if (slapi_dn_issuffix(dn, getPluginDN())) {
        ret = 1;
    }

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_is_config\n");

    return ret;
}

#define DNA_LDAP_TAG_SK_REVERSE 0x81L

static LDAPControl *dna_build_sort_control(const char *attr)
{
    LDAPControl *ctrl;
    BerElement *ber;
    int rc;

    ber = ber_alloc();
    if (NULL == ber)
        return NULL;

    rc = ber_printf(ber, "{{stb}}", attr, DNA_LDAP_TAG_SK_REVERSE, 1);
    if (-1 == rc) {
        ber_free(ber, 1);
        return NULL;
    }

    rc = slapi_build_control(LDAP_CONTROL_SORTREQUEST, ber, 1, &ctrl);

    ber_free(ber, 1);

    if (LDAP_SUCCESS != rc)
         return NULL;

    return ctrl;
}

/****************************************************
        Functions that actually do things other
        than config and startup
****************************************************/

/* 
 * dna_first_free_value()
 *
 * We do search all values between nextval and maxval asking the
 * server to sort them, then we check the first free spot and
 * use it as newval.  If we go past the end of the range, we
 * return LDAP_OPERATIONS_ERROR and set newval to be > the
 * maximum configured value for this range. */
static int
dna_first_free_value(struct configEntry *config_entry,
                                PRUint64 *newval)
{
    Slapi_Entry **entries = NULL;
    Slapi_PBlock *pb = NULL;
    LDAPControl **ctrls = NULL;
    char *attrs[2];
    char *filter;
    char *prefix;
    char *type;
    int result, status, filterlen;
    PRUint64 tmpval, sval, i;
    char *strval = NULL;

    /* check if the config is already out of range */
    if (config_entry->nextval > config_entry->maxval) {
        *newval = config_entry->nextval;
        return LDAP_OPERATIONS_ERROR;
    }

    prefix = config_entry->prefix;
    type = config_entry->type;
    tmpval = config_entry->nextval;

    attrs[0] = type;
    attrs[1] = NULL;

    /* We don't sort if we're using a prefix (non integer type).  Instead,
     * we just search to see if the next value is free, and keep incrementing
     * until we find the next free value. */
    if (prefix) {
        /* The 7 below is for all of the filter characters "(&(=))"
         * plus the trailing \0.  The 20 is for the maximum string
         * representation of a " NSPRIu64 ". */
        filterlen = strlen(config_entry->filter) +
                                 strlen(prefix) + strlen(type)
                                 + 7 + 20;
        filter = slapi_ch_malloc(filterlen);
        snprintf(filter, filterlen, "(&%s(%s=%s%" PRIu64 "))",
                          config_entry->filter, type, prefix, tmpval);
    } else {
        ctrls = (LDAPControl **)slapi_ch_calloc(2, sizeof(LDAPControl));
        if (NULL == ctrls)
            return LDAP_OPERATIONS_ERROR;

        ctrls[0] = dna_build_sort_control(config_entry->type);
        if (NULL == ctrls[0]) {
            slapi_ch_free((void **)&ctrls);
            return LDAP_OPERATIONS_ERROR;
        }

        filter = slapi_ch_smprintf("(&%s(&(%s>=%" NSPRIu64 ")(%s<=%" NSPRIu64 ")))",
                                   config_entry->filter,
                                   type, tmpval,
                                   type, config_entry->maxval);
    }

    if (NULL == filter) {
        ldap_controls_free(ctrls);
        ctrls = NULL;
        return LDAP_OPERATIONS_ERROR;
    }

    pb = slapi_pblock_new();
    if (NULL == pb) {
        ldap_controls_free(ctrls);
        ctrls = NULL;
        slapi_ch_free_string(&filter);
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_search_internal_set_pb(pb, config_entry->scope,
                                 LDAP_SCOPE_SUBTREE, filter,
                                 attrs, 0, ctrls,
                                 NULL, getPluginID(), 0);
    slapi_search_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
    if (LDAP_SUCCESS != result) {
        status = LDAP_OPERATIONS_ERROR;
        goto cleanup;
    }

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                     &entries);

    if (NULL == entries || NULL == entries[0]) {
        /* no values means we already have a good value */
        *newval = tmpval;
        status = LDAP_SUCCESS;
        goto cleanup;
    }

    if (prefix) {
        /* The next value identified in the config entry has already
         * been taken.  We just iterate through the values until we
         * (hopefully) find a free one. */
        for (tmpval += config_entry->interval; tmpval <= config_entry->maxval;
             tmpval += config_entry->interval) {
            /* filter is guaranteed to be big enough since we allocated
             * enough space to fit a string representation of any unsigned
             * 64-bit integer */
            snprintf(filter, filterlen, "(&%s(%s=%s%" PRIu64 "))",
                              config_entry->filter, type, prefix, tmpval);

            /* clear out the pblock so we can re-use it */
            slapi_free_search_results_internal(pb);
            slapi_pblock_init(pb);

            slapi_search_internal_set_pb(pb, config_entry->scope,
                                 LDAP_SCOPE_SUBTREE, filter,
                                 attrs, 0, 0,
                                 NULL, getPluginID(), 0);

            slapi_search_internal_pb(pb);

            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &result);
            if (LDAP_SUCCESS != result) {
                status = LDAP_OPERATIONS_ERROR;
                goto cleanup;
            }

            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                             &entries);

            if (NULL == entries || NULL == entries[0]) {
                /* no values means we already have a good value */
                *newval = tmpval;
                status = LDAP_SUCCESS;
                goto cleanup;
            }
        }
    } else {
        /* entries are sorted and filtered for value >= tval therefore if the
         * first one does not match tval it means that the value is free,
         * otherwise we need to cycle through values until we find a mismatch,
         * the first mismatch is the first free pit */
        sval = 0;
        for (i = 0; NULL != entries[i]; i++) {
            strval = slapi_entry_attr_get_charptr(entries[i], type);
            errno = 0;
            sval = strtoull(strval, 0, 0);
            if (errno) {
                /* something very wrong here ... */
                status = LDAP_OPERATIONS_ERROR;
                goto cleanup;
            }
            slapi_ch_free_string(&strval);

            if (tmpval != sval)
                break;

            if (config_entry->maxval < sval)
                break;

            tmpval += config_entry->interval;
        }
    }

    /* check if we went past the end of the range */
    if (tmpval <= config_entry->maxval) {
        *newval = tmpval;
        status = LDAP_SUCCESS;
    } else {
        /* we set newval past the end of the range
         * so the caller can easily detect that we
         * overflowed the configured range. */
        *newval = tmpval;
        status = LDAP_OPERATIONS_ERROR;
    }

cleanup:
    slapi_ch_free_string(&filter);
    slapi_ch_free_string(&strval);
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return status;
}

/*
 * Perform ldap operationally atomic increment
 * Return the next value to be assigned
 */
static int dna_get_next_value(struct configEntry *config_entry,
                                 char **next_value_ret)
{
    Slapi_PBlock *pb = NULL;
    LDAPMod mod_replace;
    LDAPMod *mods[2];
    char *replace_val[2];
    /* 16 for max 64-bit unsigned plus the trailing '\0' */
    char next_value[17];
    PRUint64 setval = 0;
    PRUint64 nextval = 0;
    int ret;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_get_next_value\n");

    /* get the lock to prevent contention with other threads over
     * the next new value for this range. */
    slapi_lock_mutex(config_entry->lock);

    /* get the first value */
    ret = dna_first_free_value(config_entry, &setval);
    if (LDAP_SUCCESS != ret) {
        /* check if we overflowed the configured range */
        if (setval > config_entry->maxval) {
            /* try for a new range or fail */
            ret = dna_fix_maxval(config_entry);
            if (LDAP_SUCCESS != ret) {
                slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                                "dna_get_next_value: no more values available!!\n");
                goto done;
            }

            /* get the first value from our newly extended range */
            ret = dna_first_free_value(config_entry, &setval);
            if (LDAP_SUCCESS != ret)
                goto done;
        } else {
            /* dna_first_free_value() failed for some unknown reason */
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_get_next_value: failed to allocate a new ID!!\n");
            goto done;
        }
    }

    nextval = setval + config_entry->interval;
    /* update nextval if we have not reached the end
     * of our current range */
    if ((config_entry->maxval == -1) ||
        (nextval <= (config_entry->maxval + config_entry->interval))) {
        /* try to set the new next value in the config entry */
        PR_snprintf(next_value, sizeof(next_value),"%" NSPRIu64, nextval);

        /* set up our replace modify operation */
        replace_val[0] = next_value;
        replace_val[1] = 0;
        mod_replace.mod_op = LDAP_MOD_REPLACE;
        mod_replace.mod_type = DNA_NEXTVAL;
        mod_replace.mod_values = replace_val;
        mods[0] = &mod_replace;
        mods[1] = 0;

        pb = slapi_pblock_new();
        if (NULL == pb) {
            ret = LDAP_OPERATIONS_ERROR;
            goto done;
        }

        slapi_modify_internal_set_pb(pb, config_entry->dn,
                                     mods, 0, 0, getPluginID(), 0);

        slapi_modify_internal_pb(pb);

        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);

        slapi_pblock_destroy(pb);
        pb = NULL;
    }

    if (LDAP_SUCCESS == ret) {
        slapi_ch_free_string(next_value_ret);
        *next_value_ret = slapi_ch_smprintf("%" NSPRIu64, setval);
        if (NULL == *next_value_ret) {
            ret = LDAP_OPERATIONS_ERROR;
            goto done;
        }

        /* update our cached config */
        dna_notice_allocation(config_entry, nextval, setval, 1);
    }

  done:
    slapi_unlock_mutex(config_entry->lock);

    if (pb)
        slapi_pblock_destroy(pb);

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_get_next_value\n");

    return ret;
}

/*
 * dna_update_shared_config()
 *
 * Updates the shared config entry if one is
 * configured.  Returns the LDAP result code.
 *
 * The lock for configEntry should be obtained
 * before calling this function.
 * */
static int
dna_update_shared_config(struct configEntry * config_entry)
{
    int ret = LDAP_SUCCESS;

    if (config_entry && config_entry->shared_cfg_dn) {
        /* Update the shared config entry if one is configured */
        Slapi_PBlock *pb = NULL;
        LDAPMod mod_replace;
        LDAPMod *mods[2];
        char *replace_val[2];
        /* 16 for max 64-bit unsigned plus the trailing '\0' */
        char remaining_vals[17];

        /* We store the number of remaining assigned values
         * in the shared config entry. */
        PR_snprintf(remaining_vals, sizeof(remaining_vals),"%" NSPRIu64, config_entry->remaining);

        /* set up our replace modify operation */
        replace_val[0] = remaining_vals;
        replace_val[1] = 0;
        mod_replace.mod_op = LDAP_MOD_REPLACE;
        mod_replace.mod_type = DNA_REMAINING;
        mod_replace.mod_values = replace_val;
        mods[0] = &mod_replace;
        mods[1] = 0;

        pb = slapi_pblock_new();
        if (NULL == pb) {
            ret = LDAP_OPERATIONS_ERROR;
        } else {
            slapi_modify_internal_set_pb(pb, config_entry->shared_cfg_dn,
                                         mods, NULL, NULL, getPluginID(), 0);

            slapi_modify_internal_pb(pb);

            slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);

            /* If the shared config for this instance doesn't
             * already exist, we add it. */
            if (ret == LDAP_NO_SUCH_OBJECT) {
                Slapi_Entry *e = NULL;

                /* Set up the new shared config entry */
                e = slapi_entry_alloc();
                /* the entry now owns the dup'd dn */
                slapi_entry_init(e, slapi_ch_strdup(config_entry->shared_cfg_dn), NULL);

                slapi_entry_add_string(e, SLAPI_ATTR_OBJECTCLASS, "extensibleObject");
                slapi_entry_add_string(e, DNA_HOSTNAME, hostname);
                slapi_entry_add_string(e, DNA_PORTNUM, portnum);
                if (secureportnum) {
                    slapi_entry_add_string(e, DNA_SECURE_PORTNUM, secureportnum);
                }
                slapi_entry_add_string(e, DNA_REMAINING, remaining_vals);

                /* clear pb for re-use */
                slapi_pblock_init(pb);

                /* e will be consumed by slapi_add_internal() */
                slapi_add_entry_internal_set_pb(pb, e, NULL, getPluginID(), 0);
                slapi_add_internal_pb(pb);
                slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
            }

            if (ret != LDAP_SUCCESS) {
                slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_update_shared_config: Unable to update shared config entry: %s [error %d]\n",
                        config_entry->shared_cfg_dn, ret);
            }

            slapi_pblock_destroy(pb);
            pb = NULL;
        }
    }

    return ret;
}

/* 
 * dna_update_next_range()
 *
 * Sets the proper value for the next range in
 * all configuration entries and in-memory cache.
 *
 * The range you are updating should be locked
 * before calling this function.
 */
static int
dna_update_next_range(struct configEntry *config_entry,
                                 PRUint64 lower, PRUint64 upper)
{
    Slapi_PBlock *pb = NULL;
    LDAPMod mod_replace;
    LDAPMod *mods[2];
    char *replace_val[2];
    /* 32 for the two numbers, 1 for the '-', and one for the '\0' */
    char nextrange_value[34];
    int ret = 0;

    /* Try to set the new next range in the config entry. */
    PR_snprintf(nextrange_value, sizeof(nextrange_value), "%" NSPRIu64 "-%" NSPRIu64,
             lower, upper);

    /* set up our replace modify operation */
    replace_val[0] = nextrange_value;
    replace_val[1] = 0;
    mod_replace.mod_op = LDAP_MOD_REPLACE;
    mod_replace.mod_type = DNA_NEXT_RANGE;
    mod_replace.mod_values = replace_val;
    mods[0] = &mod_replace;
    mods[1] = 0;

    pb = slapi_pblock_new();
    if (NULL == pb) {
        ret = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    slapi_modify_internal_set_pb(pb, config_entry->dn,
                                 mods, 0, 0, getPluginID(), 0);

    slapi_modify_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);

    slapi_pblock_destroy(pb);
    pb = NULL;

    if (ret != LDAP_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_update_next_range: Error updating "
                        "configuration entry [err=%d]\n", ret);
    } else {
        /* update the cached config and the shared config */
        config_entry->next_range_lower = lower;
        config_entry->next_range_upper = upper;
        dna_notice_allocation(config_entry, 0, 0, 0);
    }

bail:
    return ret;
}

/* dna_activate_next_range()
 *
 * Makes the next range the active range.  This
 * will update the config entry, the in-memory
 * config info, and the shared config entry.
 *
 * The lock for configEntry should
 * be obtained before calling this function.
 */
static int
dna_activate_next_range(struct configEntry *config_entry)
{
    Slapi_PBlock *pb = NULL;
    LDAPMod mod_maxval;
    LDAPMod mod_nextval;
    LDAPMod mod_nextrange;
    LDAPMod *mods[4];
    char *maxval_vals[2];
    char *nextval_vals[2];
    char *nextrange_vals[1];
    /* 16 for max 64-bit unsigned plus the trailing '\0' */
    char maxval_val[17];
    char nextval_val[17];
    int ret = 0;

    /* Setup the modify operation for the config entry */
    PR_snprintf(maxval_val, sizeof(maxval_val),"%" NSPRIu64, config_entry->next_range_upper);
    PR_snprintf(nextval_val, sizeof(nextval_val),"%" NSPRIu64, config_entry->next_range_lower);

    maxval_vals[0] = maxval_val;
    maxval_vals[1] = 0;
    nextval_vals[0] = nextval_val;
    nextval_vals[1] = 0;
    nextrange_vals[0] = 0;

    mod_maxval.mod_op = LDAP_MOD_REPLACE;
    mod_maxval.mod_type = DNA_MAXVAL;
    mod_maxval.mod_values = maxval_vals;
    mod_nextval.mod_op = LDAP_MOD_REPLACE;
    mod_nextval.mod_type = DNA_NEXTVAL;
    mod_nextval.mod_values = nextval_vals;
    mod_nextrange.mod_op = LDAP_MOD_DELETE;
    mod_nextrange.mod_type = DNA_NEXT_RANGE;
    mod_nextrange.mod_values = nextrange_vals;

    mods[0] = &mod_maxval;
    mods[1] = &mod_nextval;
    mods[2] = &mod_nextrange;
    mods[3] = 0;

    /* Update the config entry first */
    pb = slapi_pblock_new();
    if (NULL == pb) {
        ret = LDAP_OPERATIONS_ERROR;
        goto bail;
    }

    slapi_modify_internal_set_pb(pb, config_entry->dn,
                                 mods, 0, 0, getPluginID(), 0);

    slapi_modify_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);

    slapi_pblock_destroy(pb);
    pb = NULL;

    if (ret != LDAP_SUCCESS) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_activate_next_range: Error updating "
                        "configuration entry [err=%d]\n", ret);
    } else {
        /* Update the in-memory config info */
        config_entry->maxval = config_entry->next_range_upper;
        config_entry->nextval = config_entry->next_range_lower;
        config_entry->next_range_upper = 0;
        config_entry->next_range_lower = 0;
        config_entry->remaining = ((config_entry->maxval - config_entry->nextval + 1) /
                                    config_entry->interval);
        /* update the shared configuration */
        dna_update_shared_config(config_entry);
    }

bail:
    return ret;
}

/*
 * dna_is_replica_bind_dn()
 *
 * Checks if the passed in DN is the replica bind DN.  This
 * is used to check if a user is allowed to request range
 * from us.
 *
 * Returns 1 if bind_dn matches the replica bind dn, 0 otherwise. */
static int dna_is_replica_bind_dn(char *range_dn, char *bind_dn)
{
    char *replica_dn = NULL;
    Slapi_DN *replica_sdn = NULL;
    char *replica_bind_dn = NULL;
    Slapi_DN *replica_bind_sdn = NULL;
    Slapi_DN *range_sdn = NULL;
    Slapi_DN *bind_sdn = NULL;
    Slapi_Entry *e = NULL;
    char *attrs[2];
    Slapi_Backend *be = NULL;
    const char *be_suffix = NULL;
    int ret = 0;

    /* Find the backend suffix where the shared config is stored. */
    range_sdn = slapi_sdn_new_dn_byref(range_dn);
    if ((be = slapi_be_select(range_sdn)) != NULL) {
        be_suffix = slapi_sdn_get_dn(slapi_be_getsuffix(be, 0));
    }

    /* Fetch the "cn=replica" entry for the backend that stores
     * the shared config.  We need to see what the configured
     * replica bind DN is. */
    if (be_suffix) {
        replica_dn = slapi_ch_smprintf("cn=replica,cn=\"%s\",cn=mapping tree,cn=config", be_suffix);
        replica_sdn = slapi_sdn_new_dn_passin(replica_dn);

        attrs[0] = DNA_REPL_BIND_DN;
        attrs[1] = 0;

        /* Find cn=replica entry via search */
        slapi_search_internal_get_entry(replica_sdn, attrs, &e, getPluginID());

        if (e) {
            replica_bind_dn = slapi_entry_attr_get_charptr(e, DNA_REPL_BIND_DN);
        } else {
            slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                            "dna_is_replica_bind_dn: Failed to fetch replica entry "
                            "for range %s\n", range_dn);
        }
    }

    if (replica_bind_dn) {
        /* Compare the passed in bind dn to the replica bind dn */
        bind_sdn = slapi_sdn_new_dn_byref(bind_dn);
        replica_bind_sdn = slapi_sdn_new_dn_passin(replica_bind_dn);
        if (slapi_sdn_compare(bind_sdn, replica_bind_sdn) == 0) {
            ret = 1;
        }
    }

    slapi_entry_free(e);
    slapi_sdn_free(&range_sdn);
    slapi_sdn_free(&replica_sdn);
    slapi_sdn_free(&replica_bind_sdn);
    slapi_sdn_free(&bind_sdn);

    return ret;
}

static int dna_get_replica_bind_creds(char *range_dn, struct dnaServer *server,
                                      char **bind_dn, char **bind_passwd,
                                      char **bind_method, int *is_ssl, int *port)
{
    Slapi_PBlock *pb = NULL;
    Slapi_DN *range_sdn = NULL;
    char *replica_dn = NULL;
    Slapi_Backend *be = NULL;
    const char *be_suffix = NULL;
    char *attrs[6];
    char *filter = NULL;
    char *bind_cred = NULL;
    char *transport = NULL;
    Slapi_Entry **entries = NULL;
    int ret = LDAP_OPERATIONS_ERROR;

    /* Find the backend suffix where the shared config is stored. */
    range_sdn = slapi_sdn_new_dn_byref(range_dn);
    if ((be = slapi_be_select(range_sdn)) != NULL) {
        be_suffix = slapi_sdn_get_dn(slapi_be_getsuffix(be, 0));
    }

    /* Fetch the replication agreement entry */
    if (be_suffix) {
        replica_dn = slapi_ch_smprintf("cn=replica,cn=\"%s\",cn=mapping tree,cn=config",
                                       be_suffix);

        filter = slapi_ch_smprintf("(&(nsds5ReplicaHost=%s)(|(" DNA_REPL_PORT "=%u)"
                                   "(" DNA_REPL_PORT "=%u)))",
                                   server->host, server->port, server->secureport);

        attrs[0] = DNA_REPL_BIND_DN;
        attrs[1] = DNA_REPL_CREDS;
        attrs[2] = DNA_REPL_BIND_METHOD;
        attrs[3] = DNA_REPL_TRANSPORT;
        attrs[4] = DNA_REPL_PORT;
        attrs[5] = 0;

        pb = slapi_pblock_new();
        if (NULL == pb) {
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_get_replica_bind_creds: Failed to "
                            "allocate pblock\n");
            goto bail;
        }

        slapi_search_internal_set_pb(pb, replica_dn,
                                     LDAP_SCOPE_ONELEVEL, filter,
                                     attrs, 0, NULL, NULL, getPluginID(), 0);
        slapi_search_internal_pb(pb);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);

        if (LDAP_SUCCESS != ret) {
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_get_replica_bind_creds: Failed to fetch replica "
                            "bind credentials for range %s, server %s, port %u [error %d]\n",
                            range_dn, server->host, server->port, ret);
            goto bail;
        }

        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
                         &entries);

        if (NULL == entries || NULL == entries[0]) {
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_get_replica_bind_creds: Failed to fetch replication "
                            "agreement for range %s, server %s, port %u\n", range_dn,
                            server->host, server->port);
            ret = LDAP_OPERATIONS_ERROR;
            goto bail;
        }

        /* Get the replication bind dn and password from the agreement.  It
         * is up to the caller to free these when they are finished. */
        slapi_ch_free_string(bind_dn);
        slapi_ch_free_string(bind_method);
        *bind_dn = slapi_entry_attr_get_charptr(entries[0], DNA_REPL_BIND_DN);
        *bind_method = slapi_entry_attr_get_charptr(entries[0], DNA_REPL_BIND_METHOD);
        bind_cred = slapi_entry_attr_get_charptr(entries[0], DNA_REPL_CREDS);
        transport = slapi_entry_attr_get_charptr(entries[0], DNA_REPL_TRANSPORT);
        *port = slapi_entry_attr_get_int(entries[0], DNA_REPL_PORT);

        /* Check if we should use SSL */
        if (transport && (strcasecmp(transport, "SSL") == 0)) {
            *is_ssl = 1;
        } else if (transport && (strcasecmp(transport, "TLS") == 0)) {
            *is_ssl = 2;
        } else {
            *is_ssl = 0;
        }

        /* fix up the bind method */
        if ((NULL == *bind_method) || (strcasecmp(*bind_method, "SIMPLE") == 0)) {
            slapi_ch_free_string(bind_method);
            *bind_method = slapi_ch_strdup(LDAP_SASL_SIMPLE);
        } else if (strcasecmp(*bind_method, "SSLCLIENTAUTH") == 0) {
            slapi_ch_free_string(bind_method);
            *bind_method = slapi_ch_strdup(LDAP_SASL_EXTERNAL);
        } else if (strcasecmp(*bind_method, "SASL/GSSAPI") == 0) {
            slapi_ch_free_string(bind_method);
            *bind_method = slapi_ch_strdup("GSSAPI");
        } else if (strcasecmp(*bind_method, "SASL/DIGEST-MD5") == 0) {
            slapi_ch_free_string(bind_method);
            *bind_method = slapi_ch_strdup("DIGEST-MD5");
        } else { /* some other weird value */
            ; /* just use it directly */
        }

        /* Decode the password */
        if (bind_cred) {
            int pw_ret = 0;
            slapi_ch_free_string(bind_passwd);
            pw_ret = pw_rever_decode(bind_cred, bind_passwd, DNA_REPL_CREDS);

            if (pw_ret == -1) {
                slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                            "dna_get_replica_bind_creds: Failed to decode "
                            "replica bind credentials for range %s, server %s, "
                            "port %u\n", range_dn, server->host, server->port);
                goto bail;
            } else if (pw_ret != 0) {
                /* The password was already in clear text, so pw_rever_decode
                 * simply set bind_passwd to bind_cred.  Set bind_cred to NULL
                 * to prevent a double free.  The memory is now owned by
                 * bind_passwd, which is the callers responsibility to free. */
                bind_cred = NULL;
            }
        }
    }

    /* If we didn't get both a bind DN and a decoded password,
     * then just free everything and return an error. */
    if (*bind_dn && *bind_passwd) {
        ret = 0;
    } else {
        slapi_ch_free_string(bind_dn);
        slapi_ch_free_string(bind_passwd);
        slapi_ch_free_string(bind_method);
    }

bail:
    slapi_ch_free_string(&filter);
    slapi_sdn_free(&range_sdn);
    slapi_ch_free_string(&replica_dn);
    slapi_ch_free_string(&bind_cred);
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);

    return ret;
}

/* for mods and adds:
	where dn's are supplied, the closest in scope
	is used as long as the type and filter
	are identical - otherwise all matches count
*/

static int dna_pre_op(Slapi_PBlock * pb, int modtype)
{
    char *dn = 0;
    PRCList *list = 0;
    struct configEntry *config_entry = 0;
    struct slapi_entry *e = 0;
    Slapi_Entry *resulting_e = 0;
    char *last_type = 0;
    char *value = 0;
    int generate = 0;
    Slapi_Mods *smods = 0;
    Slapi_Mod *smod = 0;
    LDAPMod **mods;
    int free_entry = 0;
    char *errstr = NULL;
    int ret = 0;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_pre_op\n");

    /* Just bail if we aren't ready to service requests yet. */
    if (!g_plugin_started)
        goto bail;

    if (0 == (dn = dna_get_dn(pb)))
        goto bail;

    if (LDAP_CHANGETYPE_ADD == modtype) {
        slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
    } else {
        /* xxxPAR: Ideally SLAPI_MODIFY_EXISTING_ENTRY should be
         * available but it turns out that is only true if you are
         * a dbm backend pre-op plugin - lucky dbm backend pre-op
         * plugins.
         * I think that is wrong since the entry is useful for filter
         * tests and schema checks and this plugin shouldn't be limited
         * to a single backend type, but I don't want that fight right
         * now so we go get the entry here
         *
         slapi_pblock_get( pb, SLAPI_MODIFY_EXISTING_ENTRY, &e);
         */
        Slapi_DN *tmp_dn = slapi_sdn_new_dn_byref(dn);
        if (tmp_dn) {
            slapi_search_internal_get_entry(tmp_dn, 0, &e, getPluginID());
            slapi_sdn_free(&tmp_dn);
            free_entry = 1;
        }

        /* grab the mods - we'll put them back later with
         * our modifications appended
         */
        slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
        smods = slapi_mods_new();
        slapi_mods_init_passin(smods, mods);

        /* We need the resulting entry after the mods are applied to
         * see if the entry is within the scope. */
        if (e) {
            resulting_e = slapi_entry_dup(e);
            if (mods && (slapi_entry_apply_mods(resulting_e, mods) != LDAP_SUCCESS)) {
                /* The mods don't apply cleanly, so we just let this op go
                 * to let the main server handle it. */
                goto bailmod;
            }
        }
    }

    if (0 == e)
        goto bailmod;

    if (dna_dn_is_config(dn)) {
        /* Validate config changes, but don't apply them.
         * This allows us to reject invalid config changes
         * here at the pre-op stage.  Applying the config
         * needs to be done at the post-op stage. */
        Slapi_Entry *test_e = NULL;

        /* For a MOD, we need to check the resulting entry */
        if (LDAP_CHANGETYPE_ADD == modtype) {
            test_e = e;
        } else {
            test_e = resulting_e;
        }

        if (dna_parse_config_entry(test_e, 0) != DNA_SUCCESS) {
            /* Refuse the operation if config parsing failed. */
            ret = LDAP_UNWILLING_TO_PERFORM;
            if (LDAP_CHANGETYPE_ADD == modtype) {
                errstr = slapi_ch_smprintf("Not a valid DNA configuration entry.");
            } else {
                errstr = slapi_ch_smprintf("Changes result in an invalid "
                                           "DNA configuration.");
            }
        }

        /* We're done, so just bail. */
        goto bailmod;
    }

    dna_read_lock();

    if (!PR_CLIST_IS_EMPTY(dna_global_config)) {
        list = PR_LIST_HEAD(dna_global_config);

        while (list != dna_global_config && LDAP_SUCCESS == ret) {
            config_entry = (struct configEntry *) list;

            /* did we already service this type? */
            if (last_type) {
                if (!slapi_attr_type_cmp(config_entry->type, last_type, 1))
                    goto next;
            }

            /* is the entry in scope? */
            if (config_entry->scope) {
                if (!slapi_dn_issuffix(dn, config_entry->scope))
                    goto next;
            }

            /* does the entry match the filter? */
            if (config_entry->slapi_filter) {
                Slapi_Entry *test_e = NULL;

                /* For a MOD operation, we need to check the filter
                 * against the resulting entry. */
                if (LDAP_CHANGETYPE_ADD == modtype) {
                    test_e = e;
                } else {
                    test_e = resulting_e;
                }

                if (LDAP_SUCCESS != slapi_vattr_filter_test(pb,
                                                            test_e,
                                                            config_entry->
                                                            slapi_filter, 0))
                    goto next;
            }


            if (LDAP_CHANGETYPE_ADD == modtype) {
                /* does attribute contain the magic value
                   or is the type not there?
                 */
                value =
                    slapi_entry_attr_get_charptr(e, config_entry->type);
                if ((value
                     && !slapi_UTF8CASECMP(config_entry->generate, value))
                    || 0 == value) {
                    generate = 1;
                }

                slapi_ch_free_string(&value);
            } else {
                /* check mods for magic value */
                Slapi_Mod *next_mod = slapi_mod_new();
                smod = slapi_mods_get_first_smod(smods, next_mod);
                while (smod) {
                    char *type = (char *)
                        slapi_mod_get_type(smod);

                    if (slapi_attr_types_equivalent(type,
                                                    config_entry->type)) {
                        /* If all values are being deleted, we need to
                         * generate a new value. */
                        if (SLAPI_IS_MOD_DELETE(slapi_mod_get_operation(smod))) {
                            int numvals = slapi_mod_get_num_values(smod);

                            if (numvals == 0) {
                                generate = 1;
                            } else {
                                Slapi_Attr *attr = NULL;
                                int e_numvals = 0;

                                slapi_entry_attr_find(e, type, &attr);
                                if (attr) {
                                    slapi_attr_get_numvalues(attr, &e_numvals);
                                    if (numvals >= e_numvals) {
                                        generate = 1;
                                    }
                                }
                            }
                        } else {
                            /* This is either adding or replacing a value */
                            struct berval *bv = slapi_mod_get_first_value(smod);

                            /* If generate is already set, a previous mod in
                             * this same modify operation either removed all
                             * values or set the magic value.  It's possible
                             * that this mod is adding a valid value, which
                             * means we would not want to generate a new value.
                             * It is safe to unset generate since it will be
                             * reset here if necessary. */
                            generate = 0;

                            /* If we have a value, see if it's the magic value. */
                            if (bv) {
                                int len = strlen(config_entry->generate);
                                if (len == bv->bv_len) {
                                    if (!slapi_UTF8NCASECMP(bv->bv_val,
                                                            config_entry->generate,
                                                            len)) {
                                        generate = 1;
                                    }
                                }
                            } else {
                                /* This is a replace with no new values, so we need
                                 * to generate a new value. */
                                generate = 1;
                            }
                        }
                    }

                    slapi_mod_done(next_mod);
                    smod = slapi_mods_get_next_smod(smods, next_mod);
                }

                slapi_mod_free(&next_mod);
            }

            /* We need to perform one last check for modify operations.  If an
             * entry within the scope has not triggered generation yet, we need
             * to see if a value exists for the managed type in the resulting
             * entry.  This will catch a modify operation that brings an entry
             * into scope for a managed range, but doesn't supply a value for
             * the managed type.
             */
            if ((LDAP_CHANGETYPE_MODIFY == modtype) && !generate) {
                Slapi_Attr *attr = NULL;
                if (slapi_entry_attr_find(resulting_e, config_entry->type, &attr) != 0) {
                    generate = 1;
                }
            }

            if (generate) {
                char *new_value;
                int len;

                /* create the value to add */
                ret = dna_get_next_value(config_entry, &value);
                if (DNA_SUCCESS != ret) {
                    errstr = slapi_ch_smprintf("Allocation of a new value for"
                                               " %s failed! Unable to proceed.",
                                               config_entry->type);
                    break;
                }

                len = strlen(value) + 1;
                if (config_entry->prefix) {
                    len += strlen(config_entry->prefix);
                }

                new_value = slapi_ch_malloc(len);

                if (config_entry->prefix) {
                    strcpy(new_value, config_entry->prefix);
                    strcat(new_value, value);
                } else
                    strcpy(new_value, value);

                /* do the mod */
                if (LDAP_CHANGETYPE_ADD == modtype) {
                    /* add - add to entry */
                    slapi_entry_attr_set_charptr(e,
                                                 config_entry->type,
                                                 new_value);
                } else {
                    /* mod - add to mods */
                    slapi_mods_add_string(smods,
                                          LDAP_MOD_REPLACE,
                                          config_entry->type, new_value);
                }

                /* free up */
                slapi_ch_free_string(&value);
                slapi_ch_free_string(&new_value);

                /* make sure we don't generate for this
                 * type again
                 */
                if (LDAP_SUCCESS == ret) {
                    last_type = config_entry->type;
                }

                generate = 0;
            }
          next:
            list = PR_NEXT_LINK(list);
        }
    }

    dna_unlock();

  bailmod:
    if (LDAP_CHANGETYPE_MODIFY == modtype) {
        /* Put the updated mods back into place. */
        mods = slapi_mods_get_ldapmods_passout(smods);
        slapi_pblock_set(pb, SLAPI_MODIFY_MODS, mods);
        slapi_mods_free(&smods);
    }

  bail:

    if (free_entry && e)
        slapi_entry_free(e);

    if (resulting_e)
        slapi_entry_free(resulting_e);

    if (ret) {
        slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                        "dna_pre_op: operation failure [%d]\n", ret);
        slapi_send_ldap_result(pb, ret, NULL, errstr, 0, NULL);
        slapi_ch_free((void **)&errstr);
        ret = DNA_FAILURE;
    }

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_pre_op\n");

    return ret;
}

static int dna_add_pre_op(Slapi_PBlock * pb)
{
    return dna_pre_op(pb, LDAP_CHANGETYPE_ADD);
}

static int dna_mod_pre_op(Slapi_PBlock * pb)
{
    return dna_pre_op(pb, LDAP_CHANGETYPE_MODIFY);
}

static int dna_config_check_post_op(Slapi_PBlock * pb)
{
    char *dn;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_config_check_post_op\n");

    if ((dn = dna_get_dn(pb))) {
        if (dna_dn_is_config(dn))
            dna_load_plugin_config();
    }

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_config_check_post_op\n");

    return 0;
}


/****************************************************
 * Range Extension Extended Operation
 ***************************************************/
static int dna_extend_exop(Slapi_PBlock *pb)
{
    int ret = -1;
    struct berval *reqdata = NULL;
    BerElement *tmp_bere = NULL;
    char *shared_dn = NULL;
    char *bind_dn = NULL;
    char *oid = NULL;
    PRUint64 lower = 0;
    PRUint64 upper = 0;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_extend_exop\n");

    /* Fetch the request OID */
    slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_OID, &oid);
    if (!oid) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_extend_exop: Unable to retrieve request OID.\n");
        goto free_and_return;
    }

    /* Make sure the request OID is correct. */
    if (strcmp(oid, DNA_EXTEND_EXOP_REQUEST_OID) != 0) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_extend_exop: Received incorrect request OID.\n");
        goto free_and_return;
    }

    /* Fetch the request data */
    slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_VALUE, &reqdata);
    if (!reqdata) {
        slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                        "dna_extend_exop: No request data received.\n");
        goto free_and_return;
    }

    /* decode the exop */
    if ((tmp_bere = ber_init(reqdata)) == NULL) {
        ret = -1;
        goto free_and_return;
    }

    if (ber_scanf(tmp_bere, "{a}", &shared_dn) == LBER_ERROR) {
        ret = -1;
        goto free_and_return;
    }

    slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                    "dna_extend_exop: received range extension "
                    "request for range [%s]\n", shared_dn);

    /* Only allow range requests from the replication bind DN user */
    slapi_pblock_get(pb, SLAPI_CONN_DN, &bind_dn);
    if (!dna_is_replica_bind_dn(shared_dn, bind_dn)) {
        ret = LDAP_INSUFFICIENT_ACCESS;
        goto free_and_return;
    }

    /* See if we have the req. range configured.
     * If so, we need to see if we have range to provide. */
    ret = dna_release_range(shared_dn, &lower, &upper);

    if (ret == LDAP_SUCCESS) {
        /* We have range to give away, so construct
         * and send the response. */
        BerElement    *respber = NULL;
        struct berval *respdata = NULL;
        struct berval range_low = {0, NULL};
        struct berval range_high = {0, NULL};
        char lowstr[16];
        char highstr[16];

        /* Create the exop response */
        PR_snprintf(lowstr, sizeof(lowstr), "%" NSPRIu64, lower);
        PR_snprintf(highstr, sizeof(highstr), "%" NSPRIu64, upper);
        range_low.bv_val = lowstr;
        range_low.bv_len = strlen(range_low.bv_val);
        range_high.bv_val = highstr;
        range_high.bv_len = strlen(range_high.bv_val);

        if ((respber = ber_alloc()) == NULL) {
            ret = LDAP_NO_MEMORY;
            goto free_and_return;
        }

        if (LBER_ERROR == (ber_printf(respber, "{oo}",
                           range_low.bv_val, range_low.bv_len,
                           range_high.bv_val, range_high.bv_len))) {
            slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                    "dna_extend_exop: Unable to encode exop response.\n");
            ber_free(respber, 1);
            ret = LDAP_ENCODING_ERROR;
            goto free_and_return;
        }

        ber_flatten(respber, &respdata);
        ber_free(respber, 1);

        slapi_pblock_set(pb, SLAPI_EXT_OP_RET_OID, DNA_EXTEND_EXOP_RESPONSE_OID);
        slapi_pblock_set(pb, SLAPI_EXT_OP_RET_VALUE, respdata);

        /* send the response ourselves */
        slapi_send_ldap_result( pb, ret, NULL, NULL, 0, NULL );
        ret = SLAPI_PLUGIN_EXTENDED_SENT_RESULT;
        ber_bvfree(respdata);

        slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                        "dna_extend_exop: Released range %" NSPRIu64 "-%" NSPRIu64 ".\n",
                        lower, upper);
    }

  free_and_return:
    slapi_ch_free_string(&shared_dn);
    slapi_ch_free_string(&bind_dn);
    if (NULL != tmp_bere) {
        ber_free(tmp_bere, 1);
        tmp_bere = NULL;
    }

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_extend_exop\n");

    return ret;
}

/*
 * dna_release_range()
 *
 * Checks if we have any values that we can release
 * for the range specified by range_dn.
 */
static int
dna_release_range(char *range_dn, PRUint64 *lower, PRUint64 *upper)
{
    int ret = 0;
    int match = 0;
    PRCList *list = NULL;
    Slapi_DN *cfg_base_sdn = NULL;
    Slapi_DN *range_sdn = NULL;
    struct configEntry *config_entry = NULL;
    int set_extend_flag = 0;

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "--> dna_release_range\n");

    if (range_dn) {
        range_sdn = slapi_sdn_new_dn_byref(range_dn);

        dna_read_lock();

        /* Go through the config entries to see if we
         * have a shared range configured that matches
         * the range from the exop request. */
        if (!PR_CLIST_IS_EMPTY(dna_global_config)) {
            list = PR_LIST_HEAD(dna_global_config);
            while ((list != dna_global_config) && match != 1) {
                config_entry = (struct configEntry *)list;
                cfg_base_sdn = slapi_sdn_new_dn_byref(config_entry->shared_cfg_base);
                
                if (slapi_sdn_compare(cfg_base_sdn, range_sdn) == 0) {
                    /* We found a match.  Set match flag to
                     * break out of the loop. */
                    match = 1;
                } else {
                    config_entry = NULL;
                    list = PR_NEXT_LINK(list);
                }

                slapi_sdn_free(&cfg_base_sdn);
            }
          
        }

        /* config_entry will point to our match if we found one */
        if (config_entry) {
            int release = 0;
            Slapi_PBlock *pb = NULL;
            LDAPMod mod_replace;
            LDAPMod *mods[2];
            char *replace_val[2];
            /* 16 for max 64-bit unsigned plus the trailing '\0' */
            char max_value[17];

            /* Need to bail if we're performing a range request
             * for this range.  This is to prevent the case where two
             * servers are asking each other for more range for the
             * same managed range.  This would result in a network
             * deadlock until the idletimeout kills one of the
             * connections. */
            slapi_lock_mutex(config_entry->extend_lock);
            if (config_entry->extend_in_progress) {
                /* We're already processing a range extention, so bail. */
                slapi_log_error(SLAPI_LOG_PLUGIN, DNA_PLUGIN_SUBSYSTEM,
                                "dna_release_range: Already processing a "
                                "range extension request.  Skipping request.\n");
                slapi_unlock_mutex(config_entry->extend_lock);
                ret = LDAP_UNWILLING_TO_PERFORM;
                goto bail;
            } else {
                /* Set a flag indicating that we're attempting to extend this range */
                config_entry->extend_in_progress = 1;
                set_extend_flag = 1;
                slapi_unlock_mutex(config_entry->extend_lock);
            }

            /* Obtain the lock for this range */
            slapi_lock_mutex(config_entry->lock);

            /* Refuse if we're at or below our threshold */
            if (config_entry->remaining <= config_entry->threshold) {
                ret = LDAP_UNWILLING_TO_PERFORM;
                goto bail;
            }

            /* If we have a next range, we need to give up values from
             * it instead of from the active range */
            if (config_entry->next_range_lower != 0) {
                /* Release up to half of our values from the next range. */
                release = (((config_entry->next_range_upper - config_entry->next_range_lower + 1) /
                           2) / config_entry->threshold) * config_entry->threshold; 

                if (release == 0) {
                    ret = LDAP_UNWILLING_TO_PERFORM;
                    goto bail;
                }

                *upper = config_entry->next_range_upper;
                *lower = *upper - release + 1;

                /* Try to set the new next range in the config */
                ret = dna_update_next_range(config_entry, config_entry->next_range_lower,
                                          *lower - 1);
            } else {
                /* We release up to half of our remaining values,
                 * but we'll only release a range that is a multiple
                 * of our threshold. */
                release = ((config_entry->remaining / 2) /
                            config_entry->threshold) * config_entry->threshold;

                if (release == 0) {
                    ret = LDAP_UNWILLING_TO_PERFORM;
                    goto bail;
                }

                /* We give away values from the upper end of our range. */
                *upper = config_entry->maxval;
                *lower = *upper - release + 1;

                /* try to set the new maxval in the config entry */
                PR_snprintf(max_value, sizeof(max_value),"%" NSPRIu64, (*lower - 1));

                /* set up our replace modify operation */
                replace_val[0] = max_value;
                replace_val[1] = 0;
                mod_replace.mod_op = LDAP_MOD_REPLACE;
                mod_replace.mod_type = DNA_MAXVAL;
                mod_replace.mod_values = replace_val;
                mods[0] = &mod_replace;
                mods[1] = 0;

                pb = slapi_pblock_new();
                if (NULL == pb) {
                    ret = LDAP_OPERATIONS_ERROR;
                    goto bail;
                }

                slapi_modify_internal_set_pb(pb, config_entry->dn,
                                             mods, 0, 0, getPluginID(), 0);

                slapi_modify_internal_pb(pb);

                slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);

                slapi_pblock_destroy(pb);
                pb = NULL;

                if (ret == LDAP_SUCCESS) {
                    /* Adjust maxval in our cached config and shared config */
                    config_entry->maxval = *lower - 1;
                    dna_notice_allocation(config_entry, config_entry->nextval, 0, 0);
                }
            }

            if (ret != LDAP_SUCCESS) {
                    /* Updating the config failed, so reset. We don't
                     * want to give the caller any range */
                    *lower = 0;
                    *upper = 0;
                    slapi_log_error(SLAPI_LOG_FATAL, DNA_PLUGIN_SUBSYSTEM,
                                    "dna_release_range: Error updating "
                                    "configuration entry [err=%d]\n", ret);
            }
        }

bail:
        if (set_extend_flag) {
            slapi_lock_mutex(config_entry->extend_lock);
            config_entry->extend_in_progress = 0;
            slapi_unlock_mutex(config_entry->extend_lock);
        }

        if (config_entry) {
            slapi_unlock_mutex(config_entry->lock);
        }
        slapi_sdn_free(&range_sdn);

        dna_unlock();
    }

    slapi_log_error(SLAPI_LOG_TRACE, DNA_PLUGIN_SUBSYSTEM,
                    "<-- dna_release_range\n");

    return ret;
}

/****************************************************
	End of
	Functions that actually do things other
	than config and startup
****************************************************/

/**
 * debug functions to print config
 */
void dna_dump_config()
{
    PRCList *list;

    dna_read_lock();

    if (!PR_CLIST_IS_EMPTY(dna_global_config)) {
        list = PR_LIST_HEAD(dna_global_config);
        while (list != dna_global_config) {
            dna_dump_config_entry((struct configEntry *) list);
            list = PR_NEXT_LINK(list);
        }
    }

    dna_unlock();
}


void dna_dump_config_entry(struct configEntry * entry)
{
    printf("<---- type -----------> %s\n", entry->type);
    printf("<---- filter ---------> %s\n", entry->filter);
    printf("<---- prefix ---------> %s\n", entry->prefix);
    printf("<---- scope ----------> %s\n", entry->scope);
    printf("<---- next value -----> %" PRIu64 "\n", entry->nextval);
    printf("<---- max value ------> %" PRIu64 "\n", entry->maxval);
    printf("<---- interval -------> %" PRIu64 "\n", entry->interval);
    printf("<---- generate flag --> %s\n", entry->generate);
    printf("<---- shared cfg base > %s\n", entry->shared_cfg_base);
    printf("<---- shared cfg DN --> %s\n", entry->shared_cfg_dn);
    printf("<---- threshold ------> %" PRIu64 "", entry->threshold);
}
