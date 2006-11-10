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
