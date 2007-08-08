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

/* Database performance counters stuff  */
#include "back-ldbm.h"

#include "perfctrs.h"

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4000
#define TXN_STAT(env, statp, flags, malloc) \
	(env)->txn_stat((env), (statp), (flags))
#define MEMP_STAT(env, gsp, fsp, flags, malloc) \
	(env)->memp_stat((env), (gsp), (fsp), (flags))
#define LOG_STAT(env, spp, flags, malloc) (env)->log_stat((env), (spp), (flags))
#define LOCK_STAT(env, statp, flags, malloc) \
	(env)->lock_stat((env), (statp), (flags))
#if DB_VERSION_MINOR >= 4 /* i.e. 4.4 or later */
#define GET_N_LOCK_WAITS(lockstat)   lockstat->st_lock_wait
#else
#define GET_N_LOCK_WAITS(lockstat)   lockstat->st_nconflicts
#endif

#else	/* older than db 4.0 */
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3300
#define TXN_STAT(env, statp, flags, malloc) txn_stat((env), (statp))
#define MEMP_STAT(env, gsp, fsp, flags, malloc) memp_stat((env), (gsp), (fsp))
#define LOG_STAT(env, spp, flags, malloc) log_stat((env), (spp))
#define LOCK_STAT(env, statp, flags, malloc) lock_stat((env), (statp))
#define GET_N_LOCK_WAITS(lockstat)   lockstat->st_nconflicts
#else	/* older than db 3.3 */
#define TXN_STAT(env, statp, flags, malloc) txn_stat((env), (statp), (malloc))
#define MEMP_STAT(env, gsp, fsp, flags, malloc) 
	memp_stat((env), (gsp), (fsp), (malloc))
#define LOG_STAT(env, spp, flags, malloc) log_stat((env), (spp), (malloc))
#define LOCK_STAT(env, statp, flags, malloc) lock_stat((env), (statp), (malloc))
#define GET_N_LOCK_WAITS(lockstat)   lockstat->st_nconflicts
#endif
#endif

static void perfctrs_update(perfctrs_private *priv, DB_ENV *db_env);
static void perfctr_add_to_entry( Slapi_Entry *e, char *type,
	PRUint32 countervalue );

/*
 * Win32 specific code (to support the Windows NT/2000 Performance Monitor).
 */
#if defined(_WIN32)
static 
char * string_concatenate(char *a, char* b)
{
	size_t string_length = 0;
	char *string = NULL;

	string_length = strlen(a) + strlen(b) + 1;
	string = malloc(string_length);
	if (NULL == string) {
		return string;
	}
	sprintf(string,"%s%s",a,b);
	return string;
}

static void init_shared_memory(perfctrs_private *priv)
{
	performance_counters *perf = (performance_counters*)priv->memory;
	if (NULL != perf) {
		memset(perf,0,sizeof(performance_counters));
	}
}

static int open_event(char *name, perfctrs_private *priv)
{
	HANDLE hEvent = INVALID_HANDLE_VALUE;

	hEvent = OpenEvent(EVENT_ALL_ACCESS,FALSE,name);
	if (NULL == hEvent) {
		hEvent = CreateEvent(NULL,FALSE,FALSE,name);
		if (NULL == hEvent) {
			LDAPDebug(LDAP_DEBUG_ANY,"BAD EV 1, err=%d\n",GetLastError(),0,0);
			return -1;
		}
	}
	priv->hEvent = hEvent;
	return 0;
}

static int open_shared_memory(char *name, perfctrs_private *priv)
{
	HANDLE hMapping = INVALID_HANDLE_VALUE;
	void *pMemory = NULL;
	/* We fear a bug in NT where it fails to attach to an existing region on calling CreateFileMapping, so let's call OpenFileMapping first */
	hMapping = OpenFileMapping(FILE_MAP_ALL_ACCESS,FALSE,name);
	if (NULL == hMapping) {
		hMapping = CreateFileMapping((HANDLE)0xFFFFFFFF,NULL,PAGE_READWRITE,0,sizeof(performance_counters),name);
		if (NULL == hMapping) {
			LDAPDebug(LDAP_DEBUG_ANY,"BAD MAP 1, err=%d\n",GetLastError(),0,0);
			return -1;
		}
	}
	/* If we got to here, we have the mapping object open */
	pMemory = MapViewOfFile(hMapping,FILE_MAP_ALL_ACCESS,0,0,0);
	if (NULL == pMemory) {
		LDAPDebug(LDAP_DEBUG_ANY,"BAD MAP 2, err=%d\n",GetLastError(),0,0);
		return -1;
	}
	priv->memory = pMemory;
	priv->hMemory = hMapping;
	return 0;
}
#endif

/* Init perf ctrs */
void perfctrs_init(struct ldbminfo *li, perfctrs_private **ret_priv)
{
	perfctrs_private *priv = NULL;

#if defined(_WIN32)
	/* XXX What's my instance name ? */

	/* 
	 * We have a single DB environment for all backend databases.
	 * Therefore the instance name can be the server instance name.
	 * To match the db perf ctr DLL the instance name should be the
	 * name of a key defined in the registry under:
	 *   HKEY_LOCAL_MACHINE\SOFTWARE\Netscape\Directory\5
	 * i.e. slapd-servername
	 */

	char *string = NULL;
	char *instance_name = li->li_plugin->plg_name; /* XXX does not identify server instance */
#endif

	*ret_priv = NULL;

#if defined(_WIN32)
	/*
	 * On Windows, the performance counters reside in shared memory.
	 */
	if (NULL == instance_name) {
		return;
	}
	/* Invent the name for the shared memory region */
	string = string_concatenate(instance_name,PERFCTRS_REGION_SUFFIX);
	if (NULL == string) {
		return;
	}
#endif

	/*
	 * We need the perfctrs_private area on all platforms.
	 */
	priv = calloc(1,sizeof(perfctrs_private));
	if (NULL == priv) {
		return;
	}

#if defined(_WIN32)
	/* Try to open the shared memory region */
	open_shared_memory(string,priv);
	free(string);
	/* Invent the name for the update mutex */
	string = string_concatenate(instance_name,PERFCTRS_MUTEX_SUFFIX);
	if (NULL == string) {
		return;
	}
	open_event(string,priv);
	free(string);
	init_shared_memory(priv);

#else
	/*
	 * On other platforms, the performance counters reside in regular memory.
	 */
	if ( NULL == ( priv->memory = calloc( 1, sizeof( performance_counters )))) {
		return;
	}
#endif

	*ret_priv = priv;
}

/* Terminate perf ctrs */
void perfctrs_terminate(perfctrs_private **priv)
{
#if defined(_WIN32)
	if (NULL != (*priv)->memory) {
		UnmapViewOfFile((*priv)->memory);
	}
	if (NULL != (*priv)->hMemory) {
		CloseHandle((*priv)->hMemory);
	}
	if (NULL != (*priv)->hEvent) {
		CloseHandle((*priv)->hEvent);
	}
#else
	if (NULL != (*priv)->memory) {
		free((*priv)->memory);
	}
#endif

	free( (*priv) );
        (*priv) = NULL;
}

/* Wait while checking for perfctr update requests */
void perfctrs_wait(size_t milliseconds,perfctrs_private *priv,DB_ENV *db_env)
{
#if defined(_WIN32)
	if (NULL != priv) {
		DWORD ret = 0;
		if (NULL != priv->hEvent) {
			/* Sleep waiting on the perfctrs update event */
			ret = WaitForSingleObject(priv->hEvent,milliseconds);
			/* If we didn't time out, update the perfctrs */
			if (ret == WAIT_OBJECT_0) {
				perfctrs_update(priv,db_env);
			}
		} else {
			Sleep(milliseconds);
		}
	}
#else
	/* Just sleep */
	PRIntervalTime    interval;   /*NSPR timeout stuffy*/
	interval = PR_MillisecondsToInterval(milliseconds);
	DS_Sleep(interval);
#endif
}

/* Update perfctrs */
static
void perfctrs_update(perfctrs_private *priv, DB_ENV *db_env)
{
	int ret = 0;
	performance_counters *perf;
	if (NULL == priv) {
		return;
	}
	if (NULL == db_env) {
		return;
	}
        perf = (performance_counters*)priv->memory;
	if (NULL == perf) {
		return;
	}
	/* Call libdb to get the various stats */
	if (NULL != db_env->lg_handle)
	{
		DB_LOG_STAT *logstat = NULL;
		ret = LOG_STAT(db_env,&logstat,0,malloc);
		if (0 == ret) {
			perf->log_region_wait_rate = logstat->st_region_wait;
			perf->log_write_rate = 1024*1024*logstat->st_w_mbytes + logstat->st_w_bytes;
			perf->log_bytes_since_checkpoint = 1024*1024*logstat->st_wc_mbytes + logstat->st_wc_bytes;
		}
		free(logstat);
	}
	if (NULL != db_env->tx_handle)
	{
		DB_TXN_STAT *txnstat = NULL;
		ret = TXN_STAT(db_env, &txnstat, 0, malloc);
		if (0 == ret) {
			perf->active_txns = txnstat->st_nactive;
			perf->commit_rate = txnstat->st_ncommits;
			perf->abort_rate = txnstat->st_naborts;
			perf->txn_region_wait_rate = txnstat->st_region_wait;
		}
		if (txnstat)
			free(txnstat);
	}
	if (NULL != db_env->lk_handle)
	{
		DB_LOCK_STAT *lockstat = NULL;
		ret = LOCK_STAT(db_env,&lockstat,0,malloc);
		if (0 == ret) {
			perf->lock_region_wait_rate = lockstat->st_region_wait;	
			perf->deadlock_rate = lockstat->st_ndeadlocks;
			perf->configured_locks = lockstat->st_maxlocks;
			perf->current_locks = lockstat->st_nlocks;
			perf->max_locks = lockstat->st_maxnlocks;
			perf->lockers = lockstat->st_nlockers;
			perf->lock_conflicts = GET_N_LOCK_WAITS(lockstat);
			perf->lock_request_rate = lockstat->st_nrequests;			
			perf->current_lock_objects = lockstat->st_nobjects;
			perf->max_lock_objects = lockstat->st_maxnobjects;
		}
		free(lockstat);
	}
	if (NULL != db_env->mp_handle)
	{
		DB_MPOOL_STAT	*mpstat = NULL;
		ret = MEMP_STAT(db_env,&mpstat,NULL,0,malloc);
		if (0 == ret) {
#define ONEG  1073741824
			perf->cache_size_bytes = mpstat->st_gbytes * ONEG + mpstat->st_bytes;
			perf->page_access_rate = mpstat->st_cache_hit + mpstat->st_cache_miss;			
			perf->cache_hit = mpstat->st_cache_hit;			
			perf->cache_try = mpstat->st_cache_hit + mpstat->st_cache_miss;			
			perf->page_create_rate = mpstat->st_page_create;			
			perf->page_read_rate = mpstat->st_page_in;			
			perf->page_write_rate = mpstat->st_page_out;			
			perf->page_ro_evict_rate = mpstat->st_ro_evict;			
			perf->page_rw_evict_rate = mpstat->st_rw_evict;			
			perf->hash_buckets = mpstat->st_hash_buckets;			
			perf->hash_search_rate = mpstat->st_hash_searches;			
			perf->longest_chain_length = mpstat->st_hash_longest;			
			perf->hash_elements_examine_rate = mpstat->st_hash_examined;			
			perf->pages_in_use = mpstat->st_page_dirty + mpstat->st_page_clean;			
			perf->dirty_pages = mpstat->st_page_dirty;			
			perf->clean_pages = mpstat->st_page_clean;			
			perf->page_trickle_rate = mpstat->st_page_trickle;			
			perf->cache_region_wait_rate = mpstat->st_region_wait;			
			free(mpstat);
		}
	}
	/* Place the stats in the shared memory region */
	/* Bump the sequence number */
	perf->sequence_number++;
}



/*
 * Define a map (array of structures) which is used to retrieve performance
 * counters from the performance_counters structure and map them to an
 * LDAP attribute type.
 */

#define SLAPI_LDBM_PERFCTR_AT_PREFIX	"nsslapd-db-"
typedef struct slapi_ldbm_perfctr_at_map {
	char	*pam_type;		/* name of LDAP attribute type */
	size_t	pam_offset;		/* offset into performance_counters struct */
} SlapiLDBMPerfctrATMap;

static SlapiLDBMPerfctrATMap perfctr_at_map[] = {
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "abort-rate",
			offsetof( performance_counters, abort_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "active-txns",
			offsetof( performance_counters, active_txns ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "cache-hit",
			offsetof( performance_counters, cache_hit ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "cache-try",
			offsetof( performance_counters, cache_try ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "cache-region-wait-rate",
			offsetof( performance_counters, cache_region_wait_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "cache-size-bytes",
			offsetof( performance_counters, cache_size_bytes ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "clean-pages",
			offsetof( performance_counters, clean_pages ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "commit-rate",
			offsetof( performance_counters, commit_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "deadlock-rate",
			offsetof( performance_counters, deadlock_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "dirty-pages",
			offsetof( performance_counters, dirty_pages ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "hash-buckets",
			offsetof( performance_counters, hash_buckets ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "hash-elements-examine-rate",
			offsetof( performance_counters, hash_elements_examine_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "hash-search-rate",
			offsetof( performance_counters, hash_search_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "lock-conflicts",
			offsetof( performance_counters, lock_conflicts ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "lock-region-wait-rate",
			offsetof( performance_counters, lock_region_wait_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "lock-request-rate",
			offsetof( performance_counters, lock_request_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "lockers",
			offsetof( performance_counters, lockers ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "configured-locks",
			offsetof( performance_counters, configured_locks ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "current-locks",
			offsetof( performance_counters, current_locks ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "max-locks",
			offsetof( performance_counters, max_locks ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "current-lock-objects",
			offsetof( performance_counters, current_lock_objects ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "max-lock-objects",
			offsetof( performance_counters, max_lock_objects ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "log-bytes-since-checkpoint",
			offsetof( performance_counters, log_bytes_since_checkpoint ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "log-region-wait-rate",
			offsetof( performance_counters, log_region_wait_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "log-write-rate",
			offsetof( performance_counters, log_write_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "longest-chain-length",
			offsetof( performance_counters, longest_chain_length ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "objects-locked",
			offsetof( performance_counters, page_access_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "page-create-rate",
			offsetof( performance_counters, page_create_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "page-read-rate",
			offsetof( performance_counters, page_read_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "page-ro-evict-rate",
			offsetof( performance_counters, page_ro_evict_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "page-rw-evict-rate",
			offsetof( performance_counters, page_rw_evict_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "page-trickle-rate",
			offsetof( performance_counters, page_trickle_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "page-write-rate",
			offsetof( performance_counters, page_write_rate ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "pages-in-use",
			offsetof( performance_counters, pages_in_use ) },
	{ SLAPI_LDBM_PERFCTR_AT_PREFIX "txn-region-wait-rate",
			offsetof( performance_counters, txn_region_wait_rate ) },
};
#define SLAPI_LDBM_PERFCTR_AT_MAP_COUNT \
		(sizeof(perfctr_at_map) / sizeof(SlapiLDBMPerfctrATMap))


/*
 * Set attributes and values in entry `e' based on performance counter
 * information (from `priv').
 */
void
perfctrs_as_entry( Slapi_Entry *e, perfctrs_private *priv, DB_ENV *db_env )
{
	performance_counters *perf;
	int	i;

        if (priv == NULL) return;

        perf = (performance_counters*)priv->memory;

	/*
	 * First, update the values so they are current.
	 */
	perfctrs_update( priv, db_env );

	/*
	 * Then convert all the counters to attribute values.
	 */
	for ( i = 0; i < SLAPI_LDBM_PERFCTR_AT_MAP_COUNT; ++i ) {
		perfctr_add_to_entry( e, perfctr_at_map[i].pam_type,
			*((PRUint32 *)((char *)perf + perfctr_at_map[i].pam_offset)));
	}
}


static void
perfctr_add_to_entry( Slapi_Entry *e, char *type, PRUint32 countervalue )
{
	/*
	 * XXXmcs: the following line assumes that long's are 32 bits or larger,
	 * which we assume in other places too I am sure.
	 */
	slapi_entry_attr_set_ulong( e, type, (unsigned long)countervalue );
}
