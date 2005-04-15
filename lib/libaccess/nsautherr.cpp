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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (nsautherr.c)
 *
 *	This module provides facilities for handling authentication
 *	errors.
 */

#include <string.h>
#include "base/systems.h"
#include "prprf.h"
#include "libaccess/nserror.h"
#include "libaccess/nsautherr.h"

/* Error message formats XXX internationalize XXX */
static char * nsaerrnomem = "insufficient dynamic memory";
static char * nsaerrinval = "invalid argument";
static char * nsaerropen = "error opening %s";
static char * nsaerrmkdir = "error creating %s";
static char * nsaerrname = "%s not found in database %s";
static char * unknownerr = "error code %d";

/*
 * Description (nsadbErrorFmt)
 *
 *	This function formats an authentication error message into a
 *	buffer provided by the caller.  The authentication error
 *	information is passed in an error list structure.  The caller
 *	can indicate how many error frames should be processed.  A
 *	newline is inserted between messages for different error frames.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer
 *	msgbuf			- pointer to error message buffer
 *	maxlen			- maximum length of generated message
 *	maxdepth		- maximum depth for traceback
 */

NSAPI_PUBLIC void nsadbErrorFmt(NSErr_t * errp, char * msgbuf, int maxlen, int maxdepth)
{
    NSEFrame_t * efp;		/* error frame pointer */
    int len;			/* length of error message text */
    int depth = 0;		/* current depth */

    msgbuf[0] = 0;

    for (efp = errp->err_first; efp != 0; efp = efp->ef_next) {

	/* Stop if the message buffer is full */
	if (maxlen <= 0) break;

	if (depth > 0) {
	    /* Put a newline between error frame messages */
	    *msgbuf++ = '\n';
	    if (--maxlen <= 0) break;
	}

	/* Identify the facility generating the error and the id number */
	len = PR_snprintf(msgbuf, maxlen,
			  "[%s%d] ", efp->ef_program, efp->ef_errorid);
	msgbuf += len;
	maxlen -= len;

	if (maxlen <= 0) break;

	len = 0;

	if (!strcmp(efp->ef_program, NSAuth_Program)) {

	    switch (efp->ef_retcode) {
	      case NSAERRNOMEM:
		strncpy(msgbuf, nsaerrnomem, maxlen);
		len = strlen(nsaerrnomem);
		break;

	      case NSAERRINVAL:
		/* Invalid function argument error: */
		strncpy(msgbuf, nsaerrinval, maxlen);
		len = strlen(nsaerrinval);
		break;

	      case NSAERROPEN:
		/* File open error: filename */
		if (efp->ef_errc == 1) {
		    len = PR_snprintf(msgbuf, maxlen, nsaerropen,
				      efp->ef_errv[0], efp->ef_errv[1]);
		}
		break;

	      case NSAERRMKDIR:
		/* error creating database directory: database name */
		if (efp->ef_errc == 1) {
		    len = PR_snprintf(msgbuf, maxlen, nsaerrmkdir,
				      efp->ef_errv[0]);
		}
		break;

	      case NSAERRNAME:
		/* user or group name not found: database, name */
		if (efp->ef_errc == 2) {
		    len = PR_snprintf(msgbuf, maxlen, nsaerrname,
				      efp->ef_errv[0], efp->ef_errv[1]);
		}
		break;

	      default:
		len = PR_snprintf(msgbuf, maxlen, unknownerr, efp->ef_retcode);
		break;
	    }
	}
	else {
	    len = PR_snprintf(msgbuf, maxlen, unknownerr, efp->ef_retcode);
	}

	msgbuf += len;
	maxlen -= len;

	if (++depth >= maxdepth) break;
    }
}
