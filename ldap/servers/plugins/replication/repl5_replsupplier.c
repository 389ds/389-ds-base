/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* repl5_replsupplier.c */
/*

A replsupplier is an object that knows how to manage outbound replication
for one consumer.

Methods:
init()
configure()
start()
stop()
destroy()
status()
notify()

*/

#include "slapi-plugin.h"
#include "repl5.h"

typedef struct repl_supplier {
	PRUint32 client_change_count; /* # of client-supplied changes */
	PRUint32 repl_change_count; /* # of replication updates */

	PRLock *lock;
	
} repl_supplier;


static void repl_supplier_free(Repl_Supplier **rsp);

/*
 * Create and initialize this replsupplier object.
 */
Repl_Supplier *
replsupplier_init(Slapi_Entry *e)
{
	Repl_Supplier *rs;

	if ((rs = (Repl_Supplier *)slapi_ch_malloc(sizeof(Repl_Supplier))) == NULL)
	{
		goto loser;
	}
	if ((rs->lock = PR_NewLock()) == NULL)
	{
		goto loser;
	}
	return rs;

loser:
	repl_supplier_free(&rs);
	return NULL;
}



static void
repl_supplier_free(Repl_Supplier **rsp)
{
	if (NULL != rsp)
	{
		Repl_Supplier *rs = *rsp;
		if (NULL != rs)
		{
			if (NULL != rs->lock)
			{
				PR_DestroyLock(rs->lock);
				rs->lock = NULL;
			}
			slapi_ch_free((void **)rsp);
		}
	}
}



/*
 * Configure a repl_supplier object.
 */
void
replsupplier_configure(Repl_Supplier *rs, Slapi_PBlock *pb)
{
	PR_ASSERT(NULL != rs);
	
}



/*
 * Start a repl_supplier object. This means that it's ok for
 * the repl_supplier to begin normal replication duties. It does
 * not necessarily mean that a replication session will occur
 * immediately.
 */
void
replsupplier_start(Repl_Supplier *rs)
{
	PR_ASSERT(NULL != rs);
}
	



/*
 * Stop a repl_supplier object. This causes any active replication
 * sessions to be stopped ASAP, and puts the repl_supplier into a
 * stopped state. No additional replication activity will occur
 * until the replsupplier_start() function is called.
 */
void
replsupplier_stop(Repl_Supplier *rs)
{
	PR_ASSERT(NULL != rs);
}




/*
 * Destroy a repl_supplier object. The object will be stopped, if it
 * is not already stopped.
 */
void
replsupplier_destroy(Repl_Supplier **rsp)
{
	Repl_Supplier *rs;

	PR_ASSERT(NULL != rsp && NULL != *rsp);

	rs = *rsp;

	slapi_ch_free((void **)rsp);
}



/*
 * This method should be called by the repl_bos whenever it determines
 * that a change to the replicated area serviced by this repl_supplier
 * has occurred. This gives the repl_supplier a chance to implement a
 * scheduling policy.
 */
void
replsupplier_notify(Repl_Supplier *rs, PRUint32 eventmask)
{
	PR_ASSERT(NULL != rs);
}



/*
 * This method is used to obtain the status of this particular
 * repl_supplier object. Eventually it will return some object containing
 * status information. For now, it's just a placeholder function.
 */
PRUint32
replsupplier_get_status(Repl_Supplier *rs)
{
	PR_ASSERT(NULL != rs);
	return 0;
}
