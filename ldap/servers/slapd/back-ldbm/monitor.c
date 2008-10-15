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

/* monitor.c - ldbm backend monitor function */

/* Required to get portable printf/scanf format macros */
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#else
#error Need to define portable format macros such as PRIu64
#endif /* HAVE_INTTYPES_H */

#include "back-ldbm.h"
#include "dblayer.h"	/* XXXmcs: not sure this is good to do... */
#include <sys/stat.h>


#define MSET(_attr) do { \
    val.bv_val = buf; \
    val.bv_len = strlen(buf); \
    attrlist_replace(&e->e_attrs, (_attr), vals); \
} while (0)

#define MSETF(_attr, _x) do { \
    char tmp_atype[37]; \
    PR_snprintf(tmp_atype, sizeof(tmp_atype), _attr, _x); \
    MSET(tmp_atype); \
} while (0)


/* DSE callback to monitor stats for a particular instance */
int ldbm_back_monitor_instance_search(Slapi_PBlock *pb, Slapi_Entry *e,
    Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    struct ldbminfo *li = NULL;
    struct berval val;
    struct berval *vals[2];
    char buf[BUFSIZ];
    u_long hits, tries;
    long nentries,maxentries;
    size_t size,maxsize;
/* NPCTE fix for bugid 544365, esc 0. <P.R> <04-Jul-2001> */
    struct stat astat;
/* end of NPCTE fix for bugid 544365 */
    DB_MPOOL_FSTAT **mpfstat = NULL;
    int i,j;
    char *absolute_pathname = NULL;

    /* Get the LDBM Info structure for the ldbm backend */
    if (inst->inst_be->be_database == NULL) {
        *returncode= LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    li = (struct ldbminfo *)inst->inst_be->be_database->plg_private;
    if (li == NULL) {
        *returncode= LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

	if (inst->inst_be->be_state != BE_STATE_STARTED)
	{
		*returncode = LDAP_SUCCESS;
		return SLAPI_DSE_CALLBACK_OK;
	}

    vals[0] = &val;
    vals[1] = NULL;

    /* database name */
    PR_snprintf(buf, sizeof(buf), "%s", li->li_plugin->plg_name);
    MSET("database");

    /* read-only status */
    PR_snprintf( buf, sizeof(buf), "%d", inst->inst_be->be_readonly );
    MSET("readOnly");

    /* fetch cache statistics */
    cache_get_stats(&(inst->inst_cache), &hits, &tries, 
		    &nentries, &maxentries, &size, &maxsize);
    sprintf(buf, "%lu", hits);
    MSET("entryCacheHits");
    sprintf(buf, "%lu", tries);
    MSET("entryCacheTries");
    sprintf(buf, "%lu", (unsigned long)(100.0*(double)hits / (double)(tries > 0 ? tries : 1)));
    MSET("entryCacheHitRatio");
    sprintf(buf, "%" PRIuPTR, size);
    MSET("currentEntryCacheSize");
    sprintf(buf, "%" PRIuPTR, maxsize);
    MSET("maxEntryCacheSize");
    sprintf(buf, "%ld", nentries);
    MSET("currentEntryCacheCount");
    sprintf(buf, "%ld", maxentries);
    MSET("maxEntryCacheCount");

#ifdef DEBUG
    {
        /* debugging for hash statistics */
        char *x;
        cache_debug_hash(&(inst->inst_cache), &x);
        val.bv_val = x;
        val.bv_len = strlen(x);
        attrlist_replace(&e->e_attrs, "entrycache-hashtables", vals);
        slapi_ch_free((void **)&x);
    }
#endif

    if (dblayer_memp_stat(li, NULL, &mpfstat) != 0) {
        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    for (i = 0;(mpfstat[i] && (mpfstat[i]->file_name != NULL)); i++) {
#ifdef _WIN32
        int fpos = 0;
#endif

        /* only print out stats on files used by this instance */
        if (strlen(mpfstat[i]->file_name) < strlen(inst->inst_dir_name))
            continue;
        if (strncmp(mpfstat[i]->file_name, inst->inst_dir_name,
                    strlen(inst->inst_dir_name)) != 0)
            continue;

	/* Since the filenames are now relative, we need to construct an absolute version
	 * for the purpose of stat() etc below...
	 */
	slapi_ch_free_string(&absolute_pathname);
	absolute_pathname = slapi_ch_smprintf("%s%c%s" , inst->inst_parent_dir_name, get_sep(inst->inst_parent_dir_name), mpfstat[i]->file_name );

/* NPCTE fix for bugid 544365, esc 0. <P.R> <04-Jul-2001> */
	/* Hide statistic of deleted files (mainly indexes) */
	if (stat(absolute_pathname,&astat))
	    continue;
	/* If the file has been re-created after been deleted
	 * We should show only statistics for the last instance 
	 * Since SleepyCat returns the statistic of the last open file first,
	 * we should only display the first statistic record for a given file
	 */
	for (j=0;j<i;j++) 
		if (!strcmp(mpfstat[i]->file_name,mpfstat[j]->file_name))
			break;
	if (j<i)
		continue;
/* end of NPCTE fix for bugid 544365 */

        /* Get each file's stats */
        PR_snprintf(buf, sizeof(buf), "%s", mpfstat[i]->file_name);
#ifdef _WIN32
        /* 
         * For NT, switch the last
         * backslash to a foward
         * slash. - RJP
         */
        for (fpos = strlen(buf); fpos >= 0; fpos--) {
            if (buf[fpos] == '\\') {
                buf[fpos] = '/';
                break;
            }
        }
#endif
        MSETF("dbFilename-%d", i);
        
        sprintf(buf, "%u", mpfstat[i]->st_cache_hit);
        MSETF("dbFileCacheHit-%d", i);
        sprintf(buf, "%u", mpfstat[i]->st_cache_miss);
        MSETF("dbFileCacheMiss-%d", i);
        sprintf(buf, "%u", mpfstat[i]->st_page_in);
        MSETF("dbFilePageIn-%d", i);
        sprintf(buf, "%u", mpfstat[i]->st_page_out);
        MSETF("dbFilePageOut-%d", i);

	slapi_ch_free_string(&absolute_pathname);
    }

    slapi_ch_free_string(&absolute_pathname);

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR + DB_VERSION_PATCH <= 3204
    /* In DB 3.2.4 and earlier, we need to free each element */
    for (i = 0; mpfstat[i]; i++)
        slapi_ch_free((void **)&mpfstat[i]);
#endif
    slapi_ch_free((void **)&mpfstat);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;

}


/* monitor global ldbm stats */
int ldbm_back_monitor_search(Slapi_PBlock *pb, Slapi_Entry *e,
    Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    struct ldbminfo *li = (struct ldbminfo *)arg;
    struct berval val;
    struct berval *vals[2];
    char buf[BUFSIZ];
    DB_MPOOL_STAT *mpstat = NULL;
    DB_MPOOL_FSTAT **mpfstat = NULL;
    u_int32_t cache_tries;

    vals[0] = &val;
    vals[1] = NULL;

    /* database name */
    PR_snprintf(buf, sizeof(buf), "%s", li->li_plugin->plg_name);
    MSET("database");

    /* we have to ask for file stats in order to get correct global stats */
    if (dblayer_memp_stat(li, &mpstat, &mpfstat) != 0) {
        *returncode = LDAP_OPERATIONS_ERROR;
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    /* cache hits*/
    sprintf(buf, "%u", mpstat->st_cache_hit);
    MSET("dbCacheHits");

    /* cache tries*/
    cache_tries = (mpstat->st_cache_miss + mpstat->st_cache_hit);
    sprintf(buf, "%u", cache_tries);
    MSET("dbCacheTries");

    /* cache hit ratio*/
    sprintf(buf, "%lu", (unsigned long)(100.0 * (double)mpstat->st_cache_hit / (double)(cache_tries > 0 ? cache_tries : 1) ));
    MSET("dbCacheHitRatio");

    sprintf(buf, "%u", mpstat->st_page_in);
    MSET("dbCachePageIn");
    sprintf(buf, "%u", mpstat->st_page_out);
    MSET("dbCachePageOut");
    sprintf(buf, "%u", mpstat->st_ro_evict);
    MSET("dbCacheROEvict");
    sprintf(buf, "%u", mpstat->st_rw_evict);
    MSET("dbCacheRWEvict");

    slapi_ch_free((void **)&mpstat);

    if (mpfstat) {
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR + DB_VERSION_PATCH <= 3204
        /* In DB 3.2.4 and earlier, we need to free each element */
        int i;
        for (i = 0; mpfstat[i]; i++)
            slapi_ch_free((void **)&mpfstat[i]);
#endif
        slapi_ch_free((void **)&mpfstat);
    }

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}


/* monitor global ldbm database stats */
int
ldbm_back_dbmonitor_search(Slapi_PBlock *pb, Slapi_Entry *e,
	Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
	dblayer_private		*dbpriv = NULL;
	struct ldbminfo		*li = NULL;

	PR_ASSERT(NULL != arg);
	li = (struct ldbminfo*)arg;
	dbpriv = (dblayer_private*)li->li_dblayer_private;
	PR_ASSERT(NULL != dbpriv);

	perfctrs_as_entry( e, dbpriv->perf_private, dbpriv->dblayer_env->dblayer_DB_ENV);

    *returncode = LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}
