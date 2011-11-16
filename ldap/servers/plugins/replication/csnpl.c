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


#include "csnpl.h"
#include "llist.h"
#include "repl_shared.h"

struct csnpl 
{
	LList*		csnList;	/* pending list */
	Slapi_RWLock*	csnLock;	/* lock to serialize access to PL */
};	

typedef struct _csnpldata
{
	PRBool		committed;  /* True if CSN committed */
	CSN			*csn;       /* The actual CSN */
} csnpldata;

/* forward declarations */
#ifdef DEBUG
static void _csnplDumpContentNoLock(CSNPL *csnpl, const char *caller);
#endif

CSNPL* csnplNew ()
{
	CSNPL *csnpl;

	csnpl = (CSNPL *)slapi_ch_malloc (sizeof (CSNPL));
	if (csnpl == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"csnplNew: failed to allocate pending list\n");
		return NULL;
	}

	csnpl->csnList = llistNew ();
	if (csnpl->csnList == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"csnplNew: failed to allocate pending list\n");
		slapi_ch_free ((void**)&csnpl);
		return NULL;	
	}

	/* ONREPL: do locks need different names */
	csnpl->csnLock = slapi_new_rwlock();

	if (csnpl->csnLock == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"csnplNew: failed to create lock; NSPR error - %d\n",
						PR_GetError ());
		slapi_ch_free ((void**)&(csnpl->csnList));
		slapi_ch_free ((void**)&csnpl);
		return NULL;
	}

	return csnpl;
}


void
csnpldata_free(void **data)
{
	csnpldata **data_to_free = (csnpldata **)data;
	if (NULL != data_to_free)
	{
		if (NULL != (*data_to_free)->csn)
		{
			csn_free(&(*data_to_free)->csn);
		}
		slapi_ch_free((void **)data_to_free);
	}
}

void csnplFree (CSNPL **csnpl)
{
	if ((csnpl == NULL) || (*csnpl == NULL))
		return;

	/* free all remaining nodes */
	llistDestroy (&((*csnpl)->csnList), (FNFree)csnpldata_free);

	if ((*csnpl)->csnLock)
		slapi_destroy_rwlock ((*csnpl)->csnLock);

	slapi_ch_free ((void**)csnpl);	
}

/* This function isnerts a CSN into the pending list 
 * Returns: 0 if the csn was successfully inserted
 *          1 if the csn has already been seen
 *         -1 for any other kind of errors
 */
int csnplInsert (CSNPL *csnpl, const CSN *csn)
{
	int rc;
	csnpldata *csnplnode;
	char csn_str[CSN_STRSIZE];

	if (csnpl == NULL || csn == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			"csnplInsert: invalid argument\n");
		return -1;
	}
	
	slapi_rwlock_wrlock (csnpl->csnLock);

    /* check to see if this csn is larger than the last csn in the
       pending list. It has to be if we have not seen it since
       the csns are always added in the accending order. */
    csnplnode = llistGetTail (csnpl->csnList);
    if (csnplnode && csn_compare (csnplnode->csn, csn) >= 0)
    {
        slapi_rwlock_unlock (csnpl->csnLock);
        return 1;
    }

	csnplnode = (csnpldata *)slapi_ch_malloc(sizeof(csnpldata));
	csnplnode->committed = PR_FALSE;
	csnplnode->csn = csn_dup(csn);
	csn_as_string(csn, PR_FALSE, csn_str);
	rc = llistInsertTail (csnpl->csnList, csn_str, csnplnode);

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplInsert");
#endif

	slapi_rwlock_unlock (csnpl->csnLock);
	if (rc != 0)
	{
		char s[CSN_STRSIZE];		
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                            "csnplInsert: failed to insert csn (%s) into pending list\n", csn_as_string(csn,PR_FALSE,s));
        }
		return -1;
	}

	return 0;
}

int csnplRemove (CSNPL *csnpl, const CSN *csn)
{
	csnpldata *data;
	char csn_str[CSN_STRSIZE];

    if (csnpl == NULL || csn == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			"csnplRemove: invalid argument\n");
		return -1;
	}

	csn_as_string(csn, PR_FALSE, csn_str);
	slapi_rwlock_wrlock (csnpl->csnLock);

	data = (csnpldata *)llistRemove (csnpl->csnList, csn_str);
	if (data == NULL)
	{
		slapi_rwlock_unlock (csnpl->csnLock);
		return -1;		
	}

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplRemove");
#endif

	csn_free(&data->csn);
	slapi_ch_free((void **)&data);

	slapi_rwlock_unlock (csnpl->csnLock);

	return 0;
}

int csnplCommit (CSNPL *csnpl, const CSN *csn)
{
	csnpldata *data;
	char csn_str[CSN_STRSIZE];

    if (csnpl == NULL || csn == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			"csnplCommit: invalid argument\n");
		return -1;
	}
	csn_as_string(csn, PR_FALSE, csn_str);

	slapi_rwlock_wrlock (csnpl->csnLock);

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplCommit");
#endif

	data = (csnpldata*)llistGet (csnpl->csnList, csn_str);
	if (data == NULL)
	{
		/*
		 * In the scenario "4.x master -> 6.x legacy-consumer -> 6.x consumer"
		 * csn will have rid=65535. Hence 6.x consumer will get here trying
		 * to commit r->min_csn_pl because its rid matches that in the csn.
		 * However, r->min_csn_pl is always empty for a dedicated consumer.
		 * Exclude READ-ONLY replica ID here from error logging.
		 */
		ReplicaId rid = csn_get_replicaid (csn);
		if (rid < MAX_REPLICA_ID)
		{
	        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
			            "csnplCommit: can't find csn %s\n", csn_str);
		}
		slapi_rwlock_unlock (csnpl->csnLock);
		return -1;		
	}
	else
	{
		data->committed = PR_TRUE;
	}

	slapi_rwlock_unlock (csnpl->csnLock);

	return 0;
}



CSN* csnplGetMinCSN (CSNPL *csnpl, PRBool *committed)
{
	csnpldata *data;
	CSN *csn = NULL;
	slapi_rwlock_rdlock (csnpl->csnLock);
	if ((data = (csnpldata*)llistGetHead (csnpl->csnList)) != NULL)
	{
		csn = csn_dup(data->csn);
		if (NULL != committed)
		{
			*committed = data->committed;
		}
	}
	slapi_rwlock_unlock (csnpl->csnLock);

	return csn;
}


/*
 * Roll up the list of pending CSNs, removing all of the CSNs at the
 * head of the the list that are committed and contiguous. Returns
 * the largest committed CSN, or NULL if no contiguous block of
 * committed CSNs appears at the beginning of the list. The caller
 * is responsible for freeing the CSN returned.
 */
CSN *
csnplRollUp(CSNPL *csnpl, CSN **first_commited)
{
	CSN *largest_committed_csn = NULL;
	csnpldata *data;
	PRBool freeit = PR_TRUE;

	slapi_rwlock_wrlock (csnpl->csnLock);
	if (first_commited) {
	   /* Avoid non-initialization issues due to careless callers */
	  *first_commited = NULL;
	}
	data = (csnpldata *)llistGetHead(csnpl->csnList);
	while (NULL != data && data->committed)
	{
		if (NULL != largest_committed_csn && freeit)
		{
			csn_free(&largest_committed_csn);
		}
		freeit = PR_TRUE;
		largest_committed_csn = data->csn; /* Save it */
		if (first_commited && (*first_commited == NULL)) {
			*first_commited = data->csn;
			freeit = PR_FALSE;
		}
		data = (csnpldata*)llistRemoveHead (csnpl->csnList);
		slapi_ch_free((void **)&data);
		data = (csnpldata *)llistGetHead(csnpl->csnList);
	} 

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplRollUp");
#endif

	slapi_rwlock_unlock (csnpl->csnLock);
	return largest_committed_csn;
}

#ifdef DEBUG
/* Dump current content of the list - for debugging */
void 
csnplDumpContent(CSNPL *csnpl, const char *caller)
{
    if (csnpl)
    {
        slapi_rwlock_rdlock (csnpl->csnLock);
        _csnplDumpContentNoLock (csnpl, caller);
        slapi_rwlock_unlock (csnpl->csnLock);
    }   
}

/* helper function */
static void _csnplDumpContentNoLock(CSNPL *csnpl, const char *caller)
{
    csnpldata *data;
    void *iterator;
    char csn_str[CSN_STRSIZE];
    
    data = (csnpldata *)llistGetFirst(csnpl->csnList, &iterator);
	if (data) {
	    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "%s: CSN Pending list content:\n",
                    caller ? caller : "");
	}
    while (data)
    {
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "%s, %s\n",                        
                        csn_as_string(data->csn, PR_FALSE, csn_str),
                        data->committed ? "committed" : "not committed");
        data = (csnpldata *)llistGetNext (csnpl->csnList, &iterator);
    }
}
#endif

