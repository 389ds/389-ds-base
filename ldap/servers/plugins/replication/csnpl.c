/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "csnpl.h"
#include "llist.h"
#include "repl_shared.h"

struct csnpl 
{
	LList*		csnList;	/* pending list */
	PRRWLock*	csnLock;	/* lock to serialize access to PL */
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
	csnpl->csnLock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "pl_lock");

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

	if ((*csnpl)->csnLock);
		PR_DestroyRWLock ((*csnpl)->csnLock);

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
	
	PR_RWLock_Wlock (csnpl->csnLock);

    /* check to see if this csn is larger than the last csn in the
       pending list. It has to be if we have not seen it since
       the csns are always added in the accending order. */
    csnplnode = llistGetTail (csnpl->csnList);
    if (csnplnode && csn_compare (csnplnode->csn, csn) >= 0)
    {
        PR_RWLock_Unlock (csnpl->csnLock);
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

	PR_RWLock_Unlock (csnpl->csnLock);
	if (rc != 0)
	{
		char s[CSN_STRSIZE];		
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
			"csnplInsert: failed to insert csn (%s) into pending list\n", csn_as_string(csn,PR_FALSE,s));	
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
	PR_RWLock_Wlock (csnpl->csnLock);

	data = (csnpldata *)llistRemove (csnpl->csnList, csn_str);
	if (data == NULL)
	{
		PR_RWLock_Unlock (csnpl->csnLock);
		return -1;		
	}

#ifdef DEBUG
    _csnplDumpContentNoLock(csnpl, "csnplRemove");
#endif

	csn_free(&data->csn);
	slapi_ch_free((void **)&data);

	PR_RWLock_Unlock (csnpl->csnLock);

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

	PR_RWLock_Wlock (csnpl->csnLock);

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
		PR_RWLock_Unlock (csnpl->csnLock);
		return -1;		
	}
	else
	{
		data->committed = PR_TRUE;
	}

	PR_RWLock_Unlock (csnpl->csnLock);

	return 0;
}



CSN* csnplGetMinCSN (CSNPL *csnpl, PRBool *committed)
{
	csnpldata *data;
	CSN *csn = NULL;
	PR_RWLock_Rlock (csnpl->csnLock);
	if ((data = (csnpldata*)llistGetHead (csnpl->csnList)) != NULL)
	{
		csn = csn_dup(data->csn);
		if (NULL != committed)
		{
			*committed = data->committed;
		}
	}
	PR_RWLock_Unlock (csnpl->csnLock);

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

	PR_RWLock_Wlock (csnpl->csnLock);
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

	PR_RWLock_Unlock (csnpl->csnLock);
	return largest_committed_csn;
}

#ifdef DEBUG
/* Dump current content of the list - for debugging */
void 
csnplDumpContent(CSNPL *csnpl, const char *caller)
{
    if (csnpl)
    {
        PR_RWLock_Rlock (csnpl->csnLock);
        _csnplDumpContentNoLock (csnpl, caller);
        PR_RWLock_Unlock (csnpl->csnLock);
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

