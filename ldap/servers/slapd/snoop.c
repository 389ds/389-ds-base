/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* Operation Snooping Function.
   Used by server internal code (and plugins if they fancy)
   to detect state changes in the server.
   Works by snooping the operation stream (as a postop plugin)
   and calling back all the affected registered parties.
*/

/* Insert code here ... */


int statechange_register(callback *func, char *dns)
{
	int ret = -1;

	/* create register cache */

	return ret;
}

int statechange_unregister(callback *func, char *dns)
{
	int ret = -1;

	return ret;
}


int postop()
{
	/* state change, evaluate who it effects and notify */

}

