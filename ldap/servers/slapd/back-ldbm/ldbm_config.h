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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef _LDBM_CONFIG_H_
#define _LDBM_CONFIG_H_

struct config_info;
typedef struct config_info config_info;

typedef int config_set_fn_t(void *arg, void *value, char *errorbuf, int phase, int apply);
typedef void *config_get_fn_t(void *arg);
								/* The value for these is passed around as a
								 * void *, the actual value should be gotten 
								 * by casting the void * as shown below. */ 
#define CONFIG_TYPE_ONOFF 1		/* val = (int) value */
#define CONFIG_TYPE_STRING 2	/* val = (char *) value - The get functions
								 * for this type must return alloced memory
								 * that should be freed by the caller. */
#define CONFIG_TYPE_INT 3		/* val = (int) value */
#define CONFIG_TYPE_LONG 4		/* val = (long) value */
#define CONFIG_TYPE_INT_OCTAL 5	/* Same as CONFIG_TYPE_INT, but shown in
								 * octal */
#define CONFIG_TYPE_SIZE_T 6    /* val = (size_t) value */

/* How changes to some config attributes are handled depends on what
 * "phase" the server is in.  Initialization, reading the config
 * information at startup, or actually running. */
#define CONFIG_PHASE_INITIALIZATION 1
#define CONFIG_PHASE_STARTUP 2
#define CONFIG_PHASE_RUNNING 3
#define CONFIG_PHASE_INTERNAL 4

#define CONFIG_FLAG_PREVIOUSLY_SET 1
#define CONFIG_FLAG_ALWAYS_SHOW 2
#define CONFIG_FLAG_ALLOW_RUNNING_CHANGE 4
#define CONFIG_FLAG_SKIP_DEFAULT_SETTING 8

struct config_info {
	char *config_name;
	int config_type;
	char *config_default_value;
	config_get_fn_t *config_get_fn;
	config_set_fn_t *config_set_fn;
	int config_flags;
};

#define CONFIG_INSTANCE "nsslapd-instance"
#define CONFIG_LOOKTHROUGHLIMIT "nsslapd-lookthroughlimit"
#define CONFIG_RANGELOOKTHROUGHLIMIT "nsslapd-rangelookthroughlimit"
#define CONFIG_PAGEDLOOKTHROUGHLIMIT "nsslapd-pagedlookthroughlimit"
#define CONFIG_IDLISTSCANLIMIT "nsslapd-idlistscanlimit"
#define CONFIG_PAGEDIDLISTSCANLIMIT "nsslapd-pagedidlistscanlimit"
#define CONFIG_DIRECTORY "nsslapd-directory"
#define CONFIG_MODE "nsslapd-mode"
#define CONFIG_DBCACHESIZE "nsslapd-dbcachesize"
#define CONFIG_DBNCACHE "nsslapd-dbncache"
#define CONFIG_MAXPASSBEFOREMERGE "nsslapd-maxpassbeforemerge"
#define CONFIG_IMPORT_CACHE_AUTOSIZE	"nsslapd-import-cache-autosize"
#define CONFIG_CACHE_AUTOSIZE		"nsslapd-cache-autosize"
#define CONFIG_CACHE_AUTOSIZE_SPLIT	"nsslapd-cache-autosize-split"
#define CONFIG_IMPORT_CACHESIZE         "nsslapd-import-cachesize"
#define CONFIG_INDEX_BUFFER_SIZE         "nsslapd-index-buffer-size"
#define CONFIG_EXCLUDE_FROM_EXPORT		"nsslapd-exclude-from-export"
#define CONFIG_EXCLUDE_FROM_EXPORT_DEFAULT_VALUE \
	"entrydn entryid dncomp parentid numSubordinates tombstonenumsubordinates entryusn"

/* dblayer config options - These are hidden from the user
 * and can't be updated on the fly. */
#define CONFIG_DB_LOGDIRECTORY "nsslapd-db-logdirectory"
#define CONFIG_DB_DURABLE_TRANSACTIONS "nsslapd-db-durable-transaction"
#define CONFIG_DB_CIRCULAR_LOGGING "nsslapd-db-circular-logging"
#define CONFIG_DB_TRANSACTION_LOGGING "nsslapd-db-transaction-logging"
#define CONFIG_DB_CHECKPOINT_INTERVAL "nsslapd-db-checkpoint-interval"
#define CONFIG_DB_TRANSACTION_BATCH  "nsslapd-db-transaction-batch-val"
#define CONFIG_DB_TRANSACTION_BATCH_MIN_SLEEP  "nsslapd-db-transaction-batch-min-wait"
#define CONFIG_DB_TRANSACTION_BATCH_MAX_SLEEP  "nsslapd-db-transaction-batch-max-wait"
#define CONFIG_DB_LOGBUF_SIZE "nsslapd-db-logbuf-size"
#define CONFIG_DB_PAGE_SIZE "nsslapd-db-page-size"
#define CONFIG_DB_INDEX_PAGE_SIZE "nsslapd-db-index-page-size" /* With the new 
   idl design, the large 8Kbyte pages we use are not
   optimal. The page pool churns very quickly as we add new IDs under a
   sustained add load. Smaller pages stop this happening so much and
   consequently make us spend less time flushing dirty pages on checkpoints.
   But 8K is still a good page size for id2entry. So we now allow different
   page sizes for the primary and secondary indices. */
#define CONFIG_DB_IDL_DIVISOR "nsslapd-db-idl-divisor"
#define CONFIG_DB_LOGFILE_SIZE "nsslapd-db-logfile-size"
#define CONFIG_DB_TRICKLE_PERCENTAGE "nsslapd-db-trickle-percentage"
#define CONFIG_DB_SPIN_COUNT "nsslapd-db-spin-count"
#define CONFIG_DB_VERBOSE "nsslapd-db-verbose"
#define CONFIG_DB_DEBUG "nsslapd-db-debug"
#define CONFIG_DB_LOCK "nsslapd-db-locks"
#define CONFIG_DB_NAMED_REGIONS "nsslapd-db-named-regions"
#define CONFIG_DB_PRIVATE_MEM "nsslapd-db-private-mem"
#define CONFIG_DB_PRIVATE_IMPORT_MEM "nsslapd-db-private-import-mem"
#define CONFIG_DB_SHM_KEY "nsslapd-db-shm-key"
#define CONFIG_DB_CACHE "nsslapd-db-cache"
#define CONFIG_DB_DEBUG_CHECKPOINTING "nsslapd-db-debug-checkpointing"
#define CONFIG_DB_HOME_DIRECTORY "nsslapd-db-home-directory"
#define CONFIG_DB_LOCKDOWN "nsslapd-db-lockdown"
#define CONFIG_DB_TX_MAX "nsslapd-db-tx-max"

#define CONFIG_IDL_SWITCH               "nsslapd-idl-switch"
#define CONFIG_BYPASS_FILTER_TEST       "nsslapd-search-bypass-filter-test"
#define CONFIG_USE_VLV_INDEX            "nsslapd-search-use-vlv-index"
#define CONFIG_SERIAL_LOCK              "nsslapd-serial-lock"
#define CONFIG_BACKEND_OPT_LEVEL	"nsslapd-backend-opt-level"

#define CONFIG_ENTRYRDN_SWITCH          "nsslapd-subtree-rename-switch"
/* nsslapd-noancestorid is ignored unless nsslapd-subtree-rename-switch is on */
#define CONFIG_ENTRYRDN_NOANCESTORID    "nsslapd-noancestorid"

/* instance config options */
#define CONFIG_INSTANCE_CACHESIZE       "nsslapd-cachesize"
#define CONFIG_INSTANCE_CACHEMEMSIZE    "nsslapd-cachememsize"
#define CONFIG_INSTANCE_DNCACHEMEMSIZE  "nsslapd-dncachememsize"
#define CONFIG_INSTANCE_SUFFIX          "nsslapd-suffix"
#define CONFIG_INSTANCE_READONLY        "nsslapd-readonly"
#define CONFIG_INSTANCE_DIR      		"nsslapd-directory"

#define CONFIG_INSTANCE_REQUIRE_INDEX   "nsslapd-require-index"

#define CONFIG_USE_LEGACY_ERRORCODE     "nsslapd-do-not-use-vlv-error"

#define CONFIG_LDBM_DN "cn=config,cn=ldbm database,cn=plugins,cn=config"

#define LDBM_INSTANCE_CONFIG_DONT_WRITE 1

/* Some fuctions in ldbm_config.c used by ldbm_instance_config.c */
int ldbm_config_add_dse_entries(struct ldbminfo *li, char **entries, char *string1, char *string2, char *string3, int flags);
int ldbm_config_add_dse_entry(struct ldbminfo *li, char *entry, int flags);
void ldbm_config_get(void *arg, config_info *config, char *buf);
int ldbm_config_set(void *arg, char *attr_name, config_info *config_array, struct berval *bval, char *err_buf, int phase, int apply_mod, int mod_op);
int ldbm_config_ignored_attr(char *attr_name);

/* Functions in ldbm_instance_config.c used in ldbm_config.c */
int ldbm_instance_config_load_dse_info(ldbm_instance *inst);
int ldbm_instance_config_add_index_entry(ldbm_instance *inst, Slapi_Entry *e, int flags);
int
ldbm_instance_index_config_enable_index(ldbm_instance *inst, Slapi_Entry* e);
int ldbm_instance_create_default_user_indexes(ldbm_instance *inst);


#endif /* _LDBM_CONFIG_H_ */
