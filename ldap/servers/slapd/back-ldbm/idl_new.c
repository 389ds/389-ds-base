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


/* New IDL code for new indexing scheme */

/* Note to future editors: 
   This file is now full of redundant code 
   (the DB_ALLIDS_ON_WRITE==true and DB_USE_BULK_FETCH==false code).
   It should be stripped out at the beginning of a 
   major release cycle. 
 */

#include "back-ldbm.h"

static char* filename = "idl_new.c";

/* Bulk fetch feature first in DB 3.3 */
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3300
#define DB_USE_BULK_FETCH 1
#define BULK_FETCH_BUFFER_SIZE (8*1024)
#else
#undef DB_USE_BULK_FETCH
#endif

/* We used to implement allids for inserts, but that's a bad idea. 
   Why ? Because:
   1) Allids results in hard to understand query behavior.
   2) The get() calls needed to check for allids on insert cost performance.
   3) Tests show that there is no significant performance benefit to having allids on writes,
   either for updates or searches.
   Set this to revert to that code */
/* #undef DB_ALLIDS_ON_WRITE */
/* We still enforce allids threshold on reads, to save time and space fetching vast id lists */
#define DB_ALLIDS_ON_READ 1

#if !defined(DB_NEXT_DUP) 
#define DB_NEXT_DUP 0
#endif
#if !defined(DB_GET_BOTH) 
#define DB_GET_BOTH 0
#endif

/* Structure used to hide private idl-specific data in the attrinfo object */
struct idl_private {
	size_t	idl_allidslimit;
	int dummy;
};

static int idl_tune = DEFAULT_IDL_TUNE; /* tuning parameters for IDL code */
/* Currently none for new IDL code */

#if defined(DB_ALLIDS_ON_WRITE)
static int idl_new_store_allids(backend *be, DB *db, DBT *key, DB_TXN *txn);
#endif

void idl_new_set_tune(int val)
{
	idl_tune = val;
}

int idl_new_get_tune() {
  return idl_tune;
}

size_t idl_new_get_allidslimit(struct attrinfo *a, int allidslimit)
{
    idl_private *priv = NULL;

    if (allidslimit) {
        return (size_t)allidslimit;
    }

    PR_ASSERT(NULL != a);
    PR_ASSERT(NULL != a->ai_idl);

    priv = a->ai_idl;
    
    return priv->idl_allidslimit;
}

int idl_new_exceeds_allidslimit(size_t count, struct attrinfo *a, int allidslimit)
{
    size_t limit = idl_new_get_allidslimit(a, allidslimit);
    return (limit != (size_t)-1) && (count > limit);
}


/* routine to initialize the private data used by the IDL code per-attribute */
int idl_new_init_private(backend *be,struct attrinfo *a)
{
	idl_private *priv = NULL;
	struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;

	PR_ASSERT(NULL != a);
	PR_ASSERT(NULL == a->ai_idl);

	priv = (idl_private*) slapi_ch_calloc(sizeof(idl_private),1);
	if (NULL == priv) {
		return -1; /* Memory allocation failure */
	}
	priv->idl_allidslimit = (size_t)li->li_allidsthreshold;
	/* Initialize the structure */
	a->ai_idl = (void*)priv;
	return 0;
}

/* routine to release resources used by IDL private data structure */
int idl_new_release_private(struct attrinfo *a)
{
	PR_ASSERT(NULL != a);
	if (NULL != a->ai_idl)
	{
		slapi_ch_free((void**)&(a->ai_idl) );
	}
	return 0;
}

IDList *
idl_new_fetch(
    backend *be, 
    DB* db, 
    DBT *inkey, 
    DB_TXN *txn, 
    struct attrinfo *a, 
    int *flag_err,
    int allidslimit
)
{
    int ret = 0;
    int idl_rc = 0;
    DBC *cursor = NULL;
    IDList *idl = NULL;
    DBT key;
    DBT data;
    ID id = 0;
    size_t count = 0;
#ifdef DB_USE_BULK_FETCH
    /* beware that a large buffer on the stack might cause a stack overflow on some platforms */
    char buffer[BULK_FETCH_BUFFER_SIZE]; 
    void *ptr;
    DBT dataret;
#endif
    back_txn s_txn;
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;

    if (NEW_IDL_NOOP == *flag_err)
    {
        *flag_err = 0;
        return NULL;
    }

    dblayer_txn_init(li, &s_txn);
    if (txn) {
        dblayer_read_txn_begin(be, txn, &s_txn);
    }

    /* Make a cursor */
    ret = db->cursor(db,txn,&cursor,0);
    if (0 != ret) {
        ldbm_nasty(filename,1,ret);
        cursor = NULL;
        goto error;
    }
    memset(&data, 0, sizeof(data));
#ifdef DB_USE_BULK_FETCH
    data.ulen = sizeof(buffer);
    data.size = sizeof(buffer);
    data.data = buffer;
    data.flags = DB_DBT_USERMEM;
    memset(&dataret, 0, sizeof(dataret));
#else
    data.ulen = sizeof(id);
    data.size = sizeof(id);
    data.data = &id;
    data.flags = DB_DBT_USERMEM;
#endif

    /*
     * We're not expecting the key to change in value
     * so we can just use the input key as a buffer.
     * This avoids memory management of the key.
     */
    memset(&key, 0, sizeof(key));
    key.ulen = inkey->size;
    key.size = inkey->size;
    key.data = inkey->data;
    key.flags = DB_DBT_USERMEM;

    /* Position cursor at the first matching key */
#ifdef DB_USE_BULK_FETCH
    ret = cursor->c_get(cursor,&key,&data,DB_SET|DB_MULTIPLE);
#else
    ret = cursor->c_get(cursor,&key,&data,DB_SET);
#endif
    if (0 != ret) {
        if (DB_NOTFOUND != ret) {
#ifdef DB_USE_BULK_FETCH
            if (ret == DB_BUFFER_SMALL) {
                LDAPDebug(LDAP_DEBUG_ANY, "database index is corrupt; "
                          "data item for key %s is too large for our buffer "
                          "(need=%d actual=%d)\n",
                          key.data, data.size, data.ulen);
            }
#endif
            ldbm_nasty(filename,2,ret);
        }
        goto error; /* Not found is OK, return NULL IDL */
    }

    /* Iterate over the duplicates, amassing them into an IDL */
#ifdef DB_USE_BULK_FETCH
    for (;;) {
        ID lastid = 0;

        DB_MULTIPLE_INIT(ptr, &data);

        for (;;) {
            DB_MULTIPLE_NEXT(ptr, &data, dataret.data, dataret.size);
            if (dataret.data == NULL) break;
            if (ptr == NULL) break;

            if (*(int32_t *)ptr < -1) {
                LDAPDebug1Arg(LDAP_DEBUG_TRACE, "DB_MULTIPLE buffer is corrupt; "
                              "next offset [%d] is less than zero\n",
                              *(int32_t *)ptr);
                /* retry the read */
                break;
            }
            if (dataret.size != sizeof(ID)) {
                LDAPDebug(LDAP_DEBUG_ANY, "database index is corrupt; "
                          "key %s has a data item with the wrong size (%d)\n", 
                          key.data, dataret.size, 0);
                goto error;
            }
            memcpy(&id, dataret.data, sizeof(ID));
            if (id == lastid) { /* dup */
                LDAPDebug1Arg(LDAP_DEBUG_TRACE, "Detected duplicate id "
                              "%d due to DB_MULTIPLE error - skipping\n",
                              id);
                continue; /* get next one */
            }
            /* note the last id read to check for dups */
            lastid = id;
            /* we got another ID, add it to our IDL */
            idl_rc = idl_append_extend(&idl, id);
            if (idl_rc) {
                LDAPDebug(LDAP_DEBUG_ANY, "unable to extend id list (err=%d)\n", idl_rc, 0, 0);
                idl_free(idl); idl = NULL;
                goto error;
            }

            count++;
        }

        LDAPDebug(LDAP_DEBUG_TRACE, "bulk fetch buffer nids=%d\n", count, 0, 0); 
#if defined(DB_ALLIDS_ON_READ)	
        /* enforce the allids read limit */
        if ((NEW_IDL_NO_ALLID != *flag_err) && (NULL != a) &&
            (idl != NULL) && idl_new_exceeds_allidslimit(count, a, allidslimit)) {
        	idl->b_nids = 1;
        	idl->b_ids[0] = ALLID;
        	ret = DB_NOTFOUND; /* fool the code below into thinking that we finished the dups */
        	LDAPDebug(LDAP_DEBUG_BACKLDBM, "search for key for attribute index %s "
        		  "exceeded allidslimit %d - count is %d\n",
        		  a->ai_type, allidslimit, count);
        	break;
        }
#endif
        ret = cursor->c_get(cursor,&key,&data,DB_NEXT_DUP|DB_MULTIPLE);
        if (0 != ret) {
            break;
        }
   }
#else
    for (;;) {
        ret = cursor->c_get(cursor,&key,&data,DB_NEXT_DUP);
        count++;
        if (0 != ret) {
            break;
        }
        /* we got another ID, add it to our IDL */
        idl_rc = idl_append_extend(&idl, id);
        if (idl_rc) {
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                          "unable to extend id list (err=%d)\n", idl_rc);
            idl_free(idl); idl = NULL;
            goto error;
        }
#if defined(DB_ALLIDS_ON_READ)    
        /* enforce the allids read limit */
        if (idl && idl_new_exceeds_allidslimit(count, a, allidslimit)) {
            idl->b_nids = 1;
            idl->b_ids[0] = ALLID;
            ret = DB_NOTFOUND; /* fool the code below into thinking that we finished the dups */
            break;
        }
#endif
   }
#endif

    if (ret != DB_NOTFOUND) {
        idl_free(idl); idl = NULL;
        ldbm_nasty(filename,59,ret);
        goto error;
    }

    ret = 0;

    /* check for allids value */
    if (idl != NULL && idl->b_nids == 1 && idl->b_ids[0] == ALLID) {
        idl_free(idl);
        idl = idl_allids(be);
        LDAPDebug(LDAP_DEBUG_TRACE, "idl_new_fetch %s returns allids\n", 
                  key.data, 0, 0);
    } else {
        LDAPDebug(LDAP_DEBUG_TRACE, "idl_new_fetch %s returns nids=%lu\n", 
                  key.data, (u_long)IDL_NIDS(idl), 0);
    }

error:
    /* Close the cursor */
    if (NULL != cursor) {
        int ret2 = cursor->c_close(cursor);
        if (ret2) {
            ldbm_nasty(filename,3,ret2);
            if (!ret) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                ret = ret2;
            }
        }
    }
    if (ret) {
        dblayer_read_txn_abort(be, &s_txn);
    } else {
        dblayer_read_txn_commit(be, &s_txn);
    }
    *flag_err = ret;
    return idl;
}

/* 
 * Perform the range search in the idl layer instead of the index layer
 * to improve the performance.
 */
IDList *
idl_new_range_fetch(
    backend *be, 
    DB* db, 
    DBT *lowerkey, 
    DBT *upperkey,
    DB_TXN *txn, 
    struct attrinfo *ai,
    int *flag_err,
    int allidslimit,
    int sizelimit,
    time_t stoptime,
    int lookthrough_limit,
    int operator
)
{
    int ret = 0;
    int idl_rc = 0;
    DBC *cursor = NULL;
    IDList *idl = NULL;
    DBT cur_key = {0};
    DBT data = {0};
    ID id = 0;
    size_t count = 0;
#ifdef DB_USE_BULK_FETCH
    /* beware that a large buffer on the stack might cause a stack overflow on some platforms */
    char buffer[BULK_FETCH_BUFFER_SIZE]; 
    void *ptr;
    DBT dataret;
#endif
    back_txn s_txn;
    struct ldbminfo *li = (struct ldbminfo *)be->be_database->plg_private;
    time_t curtime;
    void *saved_key = NULL;

    if (NULL == flag_err) {
        return NULL;
    }

    *flag_err = 0;
    if (NEW_IDL_NOOP == *flag_err) {
        return NULL;
    }
    dblayer_txn_init(li, &s_txn);
    if (txn) {
        dblayer_read_txn_begin(be, txn, &s_txn);
    }

    /* Make a cursor */
    ret = db->cursor(db, txn, &cursor, 0);
    if (0 != ret) {
        ldbm_nasty(filename,1,ret);
        cursor = NULL;
        goto error;
    }
    memset(&data, 0, sizeof(data));
#ifdef DB_USE_BULK_FETCH
    data.ulen = sizeof(buffer);
    data.size = sizeof(buffer);
    data.data = buffer;
    data.flags = DB_DBT_USERMEM;
    memset(&dataret, 0, sizeof(dataret));
#else
    data.ulen = sizeof(id);
    data.size = sizeof(id);
    data.data = &id;
    data.flags = DB_DBT_USERMEM;
#endif

    /*
     * We're not expecting the key to change in value
     * so we can just use the input key as a buffer.
     * This avoids memory management of the key.
     */
    memset(&cur_key, 0, sizeof(cur_key));
    cur_key.ulen = lowerkey->size;
    cur_key.size = lowerkey->size;
    saved_key = cur_key.data = slapi_ch_malloc(lowerkey->size);
    memcpy(cur_key.data, lowerkey->data, lowerkey->size);
    cur_key.flags = DB_DBT_MALLOC;

    /* Position cursor at the first matching key */
#ifdef DB_USE_BULK_FETCH
    ret = cursor->c_get(cursor, &cur_key, &data, DB_SET|DB_MULTIPLE);
#else
    ret = cursor->c_get(cursor, &cur_key, &data, DB_SET);
#endif
    if (0 != ret) {
        if (DB_NOTFOUND != ret) {
#ifdef DB_USE_BULK_FETCH
            if (ret == DB_BUFFER_SMALL) {
                LDAPDebug(LDAP_DEBUG_ANY, "database index is corrupt; "
                          "data item for key %s is too large for our buffer "
                          "(need=%d actual=%d)\n",
                          cur_key.data, data.size, data.ulen);
            }
#endif
            ldbm_nasty(filename,2,ret);
        }
        goto error; /* Not found is OK, return NULL IDL */
    }

    /* Iterate over the duplicates, amassing them into an IDL */
#ifdef DB_USE_BULK_FETCH
    while (cur_key.data &&
           (upperkey && upperkey->data ?
            ((operator == SLAPI_OP_LESS) ?
             DBTcmp(&cur_key, upperkey, ai->ai_key_cmp_fn) < 0 :
             DBTcmp(&cur_key, upperkey, ai->ai_key_cmp_fn) <= 0) :
            PR_TRUE /* e.g., (x > a) */)) {
        ID lastid = 0;

        DB_MULTIPLE_INIT(ptr, &data);

        /* lookthrough limit & sizelimit check */
        if (idl) {
            if ((lookthrough_limit != -1) &&
                (idl->b_nids > (ID)lookthrough_limit)) {
                idl_free(idl);
                idl = idl_allids( be );
                LDAPDebug0Args(LDAP_DEBUG_TRACE,
                          "idl_new_range_fetch - lookthrough_limit exceeded\n");
                *flag_err = LDAP_ADMINLIMIT_EXCEEDED;
                goto error;
            }
            if ((sizelimit > 0) && (idl->b_nids > (ID)sizelimit)) {
                LDAPDebug0Args(LDAP_DEBUG_TRACE,
                               "idl_new_range_fetch - sizelimit exceeded\n");
                *flag_err = LDAP_SIZELIMIT_EXCEEDED;
                goto error;
            }
        }
        /* timelimit check */
        if (stoptime > 0) { /* timelimit is set */
            curtime = current_time();
            if (curtime >= stoptime) {
                LDAPDebug0Args(LDAP_DEBUG_TRACE,
                              "idl_new_range_fetch - timelimit exceeded\n");
                *flag_err = LDAP_TIMELIMIT_EXCEEDED;
                goto error;
            }
        }
        while (PR_TRUE) {
            DB_MULTIPLE_NEXT(ptr, &data, dataret.data, dataret.size);
            if (dataret.data == NULL) break;
            if (ptr == NULL) break;

            if (*(int32_t *)ptr < -1) {
                LDAPDebug1Arg(LDAP_DEBUG_TRACE, "DB_MULTIPLE buffer is corrupt; "
                              "next offset [%d] is less than zero\n",
                              *(int32_t *)ptr);
                /* retry the read */
                break;
            }
            if (dataret.size != sizeof(ID)) {
                LDAPDebug(LDAP_DEBUG_ANY, "database index is corrupt; "
                          "key %s has a data item with the wrong size (%d)\n", 
                          cur_key.data, dataret.size, 0);
                goto error;
            }
            memcpy(&id, dataret.data, sizeof(ID));
            if (id == lastid) { /* dup */
                LDAPDebug1Arg(LDAP_DEBUG_TRACE, "Detedted duplicate id "
                              "%d due to DB_MULTIPLE error - skipping\n",
                              id);
                continue; /* get next one */
            }
            /* note the last id read to check for dups */
            lastid = id;
            /* we got another ID, add it to our IDL */
            idl_rc = idl_append_extend(&idl, id);
            if (idl_rc) {
                LDAPDebug1Arg(LDAP_DEBUG_ANY,
                              "unable to extend id list (err=%d)\n", idl_rc);
                idl_free(idl); idl = NULL;
                goto error;
            }

            count++;
        }

        LDAPDebug(LDAP_DEBUG_TRACE, "bulk fetch buffer nids=%d\n", count, 0, 0); 
#if defined(DB_ALLIDS_ON_READ)
        /* enforce the allids read limit */
        if ((NEW_IDL_NO_ALLID != *flag_err) && ai && (idl != NULL) &&
            idl_new_exceeds_allidslimit(count, ai, allidslimit)) {
            idl->b_nids = 1;
            idl->b_ids[0] = ALLID;
            ret = DB_NOTFOUND; /* fool the code below into thinking that we finished the dups */
            break;
        }
#endif
        ret = cursor->c_get(cursor, &cur_key, &data, DB_NEXT_DUP|DB_MULTIPLE);
        if (saved_key != cur_key.data) {
            /* key was allocated in c_get */
            slapi_ch_free(&saved_key);
            saved_key = cur_key.data;
        }
        if (ret) {
            if (upperkey && upperkey->data && DBT_EQ(&cur_key, upperkey)) {
                /* this is the last key */
                break;
            }
            /* First set the cursor (DB_NEXT_NODUP does not take DB_MULTIPLE) */
            ret = cursor->c_get(cursor, &cur_key, &data, DB_NEXT_NODUP);
            if (saved_key != cur_key.data) {
                /* key was allocated in c_get */
                slapi_ch_free(&saved_key);
                saved_key = cur_key.data;
            }
            if (ret) {
                break;
            }
            /* Read the dup data */
            ret = cursor->c_get(cursor, &cur_key, &data, DB_SET|DB_MULTIPLE);
            if (saved_key != cur_key.data) {
                /* key was allocated in c_get */
                slapi_ch_free(&saved_key);
                saved_key = cur_key.data;
            }
            if (ret) {
                break;
            }
        }
    }
#else
    while (upperkey && upperkey->data ?
           ((operator == SLAPI_OP_LESS) ?
            DBTcmp(&cur_key, upperkey, ai->ai_key_cmp_fn) < 0 :
            DBTcmp(&cur_key, upperkey, ai->ai_key_cmp_fn) <= 0) :
           PR_TRUE /* e.g., (x > a) */) {
        /* lookthrough limit & sizelimit check */
        if (idl) {
            if ((lookthrough_limit != -1) &&
                (idl->b_nids > (ID)lookthrough_limit)) {
                idl_free(idl);
                idl = idl_allids( be );
                LDAPDebug0Args(LDAP_DEBUG_TRACE,
                          "idl_new_range_fetch - lookthrough_limit exceeded\n");
                *flag_err = LDAP_ADMINLIMIT_EXCEEDED;
                goto error;
            }
            if ((sizelimit > 0) && (idl->b_nids > (ID)sizelimit)) {
                LDAPDebug0Args(LDAP_DEBUG_TRACE,
                               "idl_new_range_fetch - sizelimit exceeded\n");
                *flag_err = LDAP_SIZELIMIT_EXCEEDED;
                goto error;
            }
        }
        /* timelimit check */
        if (stoptime > 0) { /* timelimit is set */
            curtime = current_time();
            if (curtime >= stoptime) {
                LDAPDebug0Args(LDAP_DEBUG_TRACE,
                              "idl_new_range_fetch - timelimit exceeded\n");
                *flag_err = LDAP_TIMELIMIT_EXCEEDED;
                goto error;
            }
        }
        ret = cursor->c_get(cursor,&cur_key,&data,DB_NEXT_DUP);
        if (saved_key != cur_key.data) {
            /* key was allocated in c_get */
            slapi_ch_free(&saved_key);
            saved_key = cur_key.data;
        }
        count++;
        if (ret) {
            if (upperkey && upperkey->data && DBT_EQ(&cur_key, upperkey)) {
                /* this is the last key */
                break;
            }
            ret = cursor->c_get(cursor, &cur_key, &data, DB_NEXT_NODUP);
            if (saved_key != cur_key.data) {
                /* key was allocated in c_get */
                slapi_ch_free(&saved_key);
                saved_key = cur_key.data;
            }
            if (ret) {
                break;
            }
        }
        /* we got another ID, add it to our IDL */
        idl_rc = idl_append_extend(&idl, id);
        if (idl_rc) {
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                          "unable to extend id list (err=%d)\n", idl_rc);
            idl_free(idl); idl = NULL;
            goto error;
        }
#if defined(DB_ALLIDS_ON_READ)    
        /* enforce the allids read limit */
        if (idl && idl_new_exceeds_allidslimit(count, ai, allidslimit)) {
            idl->b_nids = 1;
            idl->b_ids[0] = ALLID;
            ret = DB_NOTFOUND; /* fool the code below into thinking that we finished the dups */
            break;
        }
#endif
    }
#endif

    if (ret) {
        if (ret == DB_NOTFOUND) {
            ret = 0; /* normal case */
        } else {
            idl_free(idl); idl = NULL;
            ldbm_nasty(filename,59,ret);
            goto error;
        }
    }

    /* check for allids value */
    if (idl && (idl->b_nids == 1) && (idl->b_ids[0] == ALLID)) {
        idl_free(idl);
        idl = idl_allids(be);
        LDAPDebug1Arg(LDAP_DEBUG_TRACE, "idl_new_fetch %s returns allids\n", 
                      cur_key.data);
    } else {
        LDAPDebug2Args(LDAP_DEBUG_TRACE, "idl_new_fetch %s returns nids=%lu\n", 
                       cur_key.data, (u_long)IDL_NIDS(idl));
    }

error:
    DBT_FREE_PAYLOAD(cur_key);
    /* Close the cursor */
    if (NULL != cursor) {
        int ret2 = cursor->c_close(cursor);
        if (ret2) {
            ldbm_nasty(filename,3,ret2);
            if (!ret) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                ret = ret2;
            }
        }
    }
    if (ret) {
        dblayer_read_txn_abort(be, &s_txn);
    } else {
        dblayer_read_txn_commit(be, &s_txn);
    }
    *flag_err = ret;

    /* sort idl */
    if (idl && !ALLIDS(idl)) {
        qsort((void *)&idl->b_ids[0], idl->b_nids,
              (size_t)sizeof(ID), idl_sort_cmp);
    }
    return idl;
}

int idl_new_insert_key(
    backend *be, 
    DB* db, 
    DBT *key, 
    ID id, 
    DB_TXN *txn, 
    struct attrinfo *a, 
    int *disposition 
)
{
    int ret = 0;
    DBT data;

#if defined(DB_ALLIDS_ON_WRITE)
    DBC *cursor = NULL;
    db_recno_t count;
    ID tmpid = 0;
    /* Make a cursor */
    ret = db->cursor(db,txn,&cursor,0);
    if (0 != ret) {
        ldbm_nasty(filename,58,ret);
        cursor = NULL;
        goto error;
    }
    memset(data, 0, sizeof(data)); /* bdb says data = {0} is not sufficient */
    data.ulen = sizeof(id);
    data.size = sizeof(id);
    data.flags = DB_DBT_USERMEM;
    data.data = &tmpid;
    ret = cursor->c_get(cursor,key,&data,DB_SET);
    if (0 == ret) {
        if (tmpid == ALLID) {
            if (NULL != disposition) {
                *disposition = IDL_INSERT_ALLIDS;
            }
            goto error;  /* allid: don't bother inserting any more */
        }
    } else if (DB_NOTFOUND != ret) {
        ldbm_nasty(filename,12,ret);
        goto error;
    }
    if (NULL != disposition) {
        *disposition = IDL_INSERT_NORMAL;
    }

    data.data = &id;

    /* insert it */
    ret = cursor->c_put(cursor, key, &data, DB_NODUPDATA);
    if (0 != ret) {
        if (DB_KEYEXIST == ret) {
            /* this is okay */
            ret = 0;
        } else {
            ldbm_nasty(filename,50,ret);
        }
    } else {
        /* check for allidslimit exceeded in database */
        if (cursor->c_count(cursor, &count, 0) != 0) {
            LDAPDebug(LDAP_DEBUG_ANY, "could not obtain count for key %s\n",
                      key->data, 0, 0);
            goto error;
        }
        if ((size_t)count > idl_new_get_allidslimit(a, 0)) {
            LDAPDebug(LDAP_DEBUG_TRACE, "allidslimit exceeded for key %s\n",
                      key->data, 0, 0);
            cursor->c_close(cursor);
            cursor = NULL;
            if ((ret = idl_new_store_allids(be, db, key, txn)) == 0) {
                if (NULL != disposition) {
                    *disposition = IDL_INSERT_NOW_ALLIDS;
                }
            }
        }
error:
    /* Close the cursor */
    if (NULL != cursor) {
        int ret2 = cursor->c_close(cursor);
        if (ret2) {
            ldbm_nasty(filename,56,ret2);
        }
    }
#else
    memset(&data, 0, sizeof(data)); /* bdb says data = {0} is not sufficient */
    data.ulen = sizeof(id);
    data.size = sizeof(id);
    data.flags = DB_DBT_USERMEM;
    data.data = &id;

    if (NULL != disposition) {
        *disposition = IDL_INSERT_NORMAL;
    }

    ret = db->put(db, txn, key, &data, DB_NODUPDATA);
    if (0 != ret) {
        if (DB_KEYEXIST == ret) {
            /* this is okay */
            ret = 0;
        } else {
            ldbm_nasty(filename,60,ret);
        }
    }
#endif


    return ret;
}

int idl_new_delete_key(
    backend *be, 
    DB *db, 
    DBT *key, 
    ID id, 
    DB_TXN *txn, 
    struct attrinfo *a 
)
{
    int ret = 0;
    DBC *cursor = NULL;
    DBT data = {0};

    /* Make a cursor */
    ret = db->cursor(db,txn,&cursor,0);
    if (0 != ret) {
        ldbm_nasty(filename,21,ret);
        cursor = NULL;
        goto error;
    }
    data.ulen = sizeof(id);
    data.size = sizeof(id);
    data.flags = DB_DBT_USERMEM;
    data.data = &id;
    /* Position cursor at the key, value pair */
    ret = cursor->c_get(cursor,key,&data,DB_GET_BOTH);
    if (0 == ret) {
        if (id == ALLID) {
            goto error;  /* allid: never delete it */
        }
    } else {
        if (DB_NOTFOUND == ret) {
            ret = 0; /* Not Found is OK, return immediately */
        } else {
            ldbm_nasty(filename,22,ret);
        }
        goto error; 
    }
    /* We found it, so delete it */
    ret = cursor->c_del(cursor,0);
error:
    /* Close the cursor */
    if (NULL != cursor) {
        int ret2 = cursor->c_close(cursor);
        if (ret2) {
            ldbm_nasty(filename,24,ret2);
            if (!ret) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                ret = ret2;
            }
        }
    }
    return ret;
}

#if defined(DB_ALLIDS_ON_WRITE)
static int idl_new_store_allids(backend *be, DB *db, DBT *key, DB_TXN *txn)
{
    int ret = 0;
    DBC *cursor = NULL;
    DBT data = {0};
    ID id = 0;

    /* Make a cursor */
    ret = db->cursor(db,txn,&cursor,0);
    if (0 != ret) {
        ldbm_nasty(filename,31,ret);
        cursor = NULL;
        goto error;
    }
    data.ulen = sizeof(ID);
    data.size = sizeof(ID);
    data.data = &id;
    data.flags = DB_DBT_USERMEM;

    /* Position cursor at the key */
    ret = cursor->c_get(cursor,key,&data,DB_SET);
    if (ret == 0) {
        /* We found it, so delete all duplicates */
        ret = cursor->c_del(cursor,0);
        while (0 == ret) {
            ret = cursor->c_get(cursor,key,&data,DB_NEXT_DUP);
            if (0 != ret) {
                break;
            }
            ret = cursor->c_del(cursor,0);
        }
        if (0 != ret && DB_NOTFOUND != ret) {
            ldbm_nasty(filename,54,ret);
            goto error; 
        } else {
            ret = 0;
        }
    } else {
        if (DB_NOTFOUND == ret) {
            ret = 0; /* Not Found is OK */
        } else {
            ldbm_nasty(filename,32,ret);
            goto error; 
        }
    }

    /* store the ALLID value */
    id = ALLID;
    ret = cursor->c_put(cursor, key, &data, DB_NODUPDATA);
    if (0 != ret) {
        ldbm_nasty(filename,53,ret);
        goto error;
    }

    LDAPDebug(LDAP_DEBUG_TRACE, "key %s has been set to allids\n",
              key->data, 0, 0);

error:
    /* Close the cursor */
    if (NULL != cursor) {
        int ret2 = cursor->c_close(cursor);
        if (ret2) {
            ldbm_nasty(filename,33,ret2);
        }
    }
    return ret;
#ifdef KRAZY_K0DE
    /* If this function is called in "no-allids" mode, then it's a bug */
    ldbm_nasty(filename,63,0);
    return -1;
#endif
}
#endif

int idl_new_store_block(
    backend *be,
    DB *db,
    DBT *key,
    IDList *idl,
    DB_TXN *txn,
    struct attrinfo *a
)
{
    int ret = 0;
    DBC *cursor = NULL;
    DBT data = {0};
    ID id = 0;
    size_t x = 0;
#if defined(DB_ALLIDS_ON_WRITE)
    db_recno_t count;
#endif

    if (NULL == idl)
    {
        return ret;
    }

    /* 
     * Really we need an extra entry point to the DB here, which
     * inserts a list of duplicate keys. In the meantime, we'll
     * just do it by brute force. 
     */
#if defined(DB_ALLIDS_ON_WRITE)
    /* allids check on input idl */
    if (ALLIDS(idl) || (idl->b_nids > (ID)idl_new_get_allidslimit(a, 0))) {
        return idl_new_store_allids(be, db, key, txn);
    }
#endif

    /* Make a cursor */
    ret = db->cursor(db,txn,&cursor,0);
    if (0 != ret) {
        ldbm_nasty(filename,41,ret);
        cursor = NULL;
        goto error;
    }

    /* initialize data DBT */
    data.data = &id;
    data.ulen = sizeof(id);
    data.size = sizeof(id);
    data.flags = DB_DBT_USERMEM;

    /* Position cursor at the key, value pair */
    ret = cursor->c_get(cursor,key,&data,DB_GET_BOTH);
    if (ret == DB_NOTFOUND) {
        ret = 0;
    } else if (ret != 0) {
        ldbm_nasty(filename,47,ret);
        goto error;
    } 

    /* Iterate over the IDs in the idl */
    for (x = 0; x < idl->b_nids; x++) {
        /* insert an id */
        id = idl->b_ids[x];
        ret = cursor->c_put(cursor, key, &data, DB_NODUPDATA);
        if (0 != ret) {
            if (DB_KEYEXIST == ret) {
                ret = 0;   /* exist is okay */
            } else {
                ldbm_nasty(filename,48,ret);
                goto error;
            }
        } 
    }
#if defined(DB_ALLIDS_ON_WRITE)
    /* check for allidslimit exceeded in database */
    if (cursor->c_count(cursor, &count, 0) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "could not obtain count for key %s\n",
                  key->data, 0, 0);
        goto error;
    }
    if ((size_t)count > idl_new_get_allidslimit(a, 0)) {
        LDAPDebug(LDAP_DEBUG_TRACE, "allidslimit exceeded for key %s\n",
                  key->data, 0, 0);
        cursor->c_close(cursor);
        cursor = NULL;
        ret = idl_new_store_allids(be, db, key, txn);
    }
#endif

error:
    /* Close the cursor */
    if (NULL != cursor) {
        int ret2 = cursor->c_close(cursor);
        if (ret2) {
            ldbm_nasty(filename,49,ret2);
            if (!ret) {
                /* if cursor close returns DEADLOCK, we must bubble that up
                   to the higher layers for retries */
                ret = ret2;
            }
        }
    }
    return ret;
}

/* idl_new_compare_dups: comparing ID, pass to libdb for callback */
int idl_new_compare_dups(
#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 3200
    DB *db,
#endif
    const DBT *a, 
    const DBT *b
)
{
	ID a_copy, b_copy;
	memmove(&a_copy, a->data, sizeof(ID));
	memmove(&b_copy, b->data, sizeof(ID));
	return a_copy - b_copy;
}
