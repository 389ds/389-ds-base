/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (aclerror.c)
 *
 *	This module provides error-handling facilities for ACL-related
 *	errors.
 */

#include "base/systems.h"
#ifdef NSPR20
#include "prprf.h"
#else
#include "nspr/prprf.h"
#endif
#include "prlog.h"
#include "libaccess/nserror.h"
#include "libaccess/nsautherr.h"
#include "libaccess/aclerror.h"
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclerror.h>

#define aclerrnomem	XP_GetAdminStr(DBT_AclerrfmtAclerrnomem)
#define aclerropen	XP_GetAdminStr(DBT_AclerrfmtAclerropen)
#define aclerrdupsym1	XP_GetAdminStr(DBT_AclerrfmtAclerrdupsym1)
#define aclerrdupsym3	XP_GetAdminStr(DBT_AclerrfmtAclerrdupsym3)
#define aclerrsyntax	XP_GetAdminStr(DBT_AclerrfmtAclerrsyntax)
#define aclerrundef	XP_GetAdminStr(DBT_AclerrfmtAclerrundef)
#define aclaclundef	XP_GetAdminStr(DBT_AclerrfmtAclaclundef)
#define aclerradb	XP_GetAdminStr(DBT_AclerrfmtAclerradb)
#define aclerrparse1	XP_GetAdminStr(DBT_AclerrfmtAclerrparse1)
#define aclerrparse2	XP_GetAdminStr(DBT_AclerrfmtAclerrparse2)
#define aclerrparse3	XP_GetAdminStr(DBT_AclerrfmtAclerrparse3)
#define aclerrnorlm	XP_GetAdminStr(DBT_AclerrfmtAclerrnorlm)
#define unknownerr	XP_GetAdminStr(DBT_AclerrfmtUnknownerr)
#define	aclerrinternal	XP_GetAdminStr(DBT_AclerrfmtAclerrinternal)
#define	aclerrinval	XP_GetAdminStr(DBT_AclerrfmtAclerrinval)
#define aclerrfail	XP_GetAdminStr(DBT_AclerrfmtAclerrfail)
#define aclerrio	XP_GetAdminStr(DBT_AclerrfmtAclerrio)

/*
 * Description (aclErrorFmt)
 *
 *	This function formats an ACL error message into a buffer provided
 *	by the caller.  The ACL error information is passed in an error
 *	list structure.  The caller can indicate how many error frames
 *	should be processed.  A newline is inserted between messages for
 *	different error frames.  The error frames on the error list are
 *	all freed, regardless of the maximum depth for traceback.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer
 *	msgbuf			- pointer to error message buffer
 *	maxlen			- maximum length of generated message
 *	maxdepth		- maximum depth for traceback
 */

void aclErrorFmt(NSErr_t * errp, char * msgbuf, int maxlen, int maxdepth)
{
    NSEFrame_t * efp;		/* error frame pointer */
    int len;			/* length of error message text */
    int depth = 0;		/* current depth */

    msgbuf[0] = 0;

    while ((efp = errp->err_first) != 0) {

	/* Stop if the message buffer is full */
	if (maxlen <= 0) break;

	if (depth > 0) {
	    /* Put a newline & tab between error frame messages */
	    *msgbuf++ = '\n';
	    if (--maxlen <= 0) break;
	    *msgbuf++ = '\t';
	    if (--maxlen <= 0) break;
	}

	if (!strcmp(efp->ef_program, ACL_Program)) {

	    /* Identify the facility generating the error and the id number */
	    len = PR_snprintf(msgbuf, maxlen,
			      "[%s%d] ", efp->ef_program, efp->ef_errorid);
	    msgbuf += len;
	    maxlen -= len;

	    if (maxlen <= 0) break;

	    len = 0;

	    switch (efp->ef_retcode) {

	      case ACLERRFAIL:
	      case ACLERRNOMEM:
	      case ACLERRINTERNAL:
	      case ACLERRINVAL:
		switch (efp->ef_errc) {
		case 3:
		    PR_snprintf(msgbuf, maxlen, efp->ef_errv[0], efp->ef_errv[1], efp->ef_errv[2]);
		    break;
		case 2:
		    PR_snprintf(msgbuf, maxlen, efp->ef_errv[0], efp->ef_errv[1]);
		    break;
		case 1:
		    strncpy(msgbuf, efp->ef_errv[0], maxlen);
		    break;
		default:
		    PR_ASSERT(0); /* don't break -- continue into case 0 */
		case 0:
	            switch (efp->ef_retcode) {
	            case ACLERRFAIL:
		        strncpy(msgbuf, XP_GetAdminStr(DBT_AclerrfmtAclerrfail), maxlen);
			break;
                    case ACLERRNOMEM:
                        strncpy(msgbuf, aclerrnomem, maxlen);
	                break;
	            case ACLERRINTERNAL:
		        strncpy(msgbuf, aclerrinternal, maxlen);
			break;
	            case ACLERRINVAL:
		        strncpy(msgbuf, aclerrinval, maxlen);
			break;
		    }
		    break;
		}
		msgbuf[maxlen-1] = '\0';
                len = strlen(msgbuf);
		break;

	      case ACLERROPEN:
		/* File open error: filename, system_errmsg */
		if (efp->ef_errc == 2) {
		    len = PR_snprintf(msgbuf, maxlen, aclerropen,
				      efp->ef_errv[0], efp->ef_errv[1]);
		}
		break;

	      case ACLERRDUPSYM:
		/* Duplicate symbol */
		if (efp->ef_errc == 1) {
		    /* Duplicate symbol: filename, line#, symbol */
		    len = PR_snprintf(msgbuf, maxlen, aclerrdupsym1,
				      efp->ef_errv[0]);
		}
		else if (efp->ef_errc == 3) {
		    /* Duplicate symbol: symbol */
		    len = PR_snprintf(msgbuf, maxlen, aclerrdupsym3,
				      efp->ef_errv[0], efp->ef_errv[1],
				      efp->ef_errv[2]);
		}
		break;

	      case ACLERRSYNTAX:
		if (efp->ef_errc == 2) {
		    /* Syntax error: filename, line# */
		    len = PR_snprintf(msgbuf, maxlen, aclerrsyntax,
				      efp->ef_errv[0], efp->ef_errv[1]);
		}
		break;

	      case ACLERRUNDEF:
		  if (efp->ef_errorid == ACLERR3800) {
		      /* Undefined symbol: acl, method/database name */
		      len = PR_snprintf(msgbuf, maxlen, aclaclundef,
					efp->ef_errv[0], efp->ef_errv[1],
					efp->ef_errv[2]);
		  }
		  else if (efp->ef_errc == 3) {
		      /* Undefined symbol: filename, line#, symbol */
		      len = PR_snprintf(msgbuf, maxlen, aclerrundef,
					efp->ef_errv[0], efp->ef_errv[1],
					efp->ef_errv[2]);
		  }
		  break;

	      case ACLERRADB:
		if (efp->ef_errc == 2) {
		    /* Authentication database error: DB name, symbol */
		    len = PR_snprintf(msgbuf, maxlen, aclerradb,
				      efp->ef_errv[0], efp->ef_errv[1]);
		}
		break;

	      case ACLERRPARSE:
		if (efp->ef_errc == 2) {
		    /* Parse error: filename, line# */
		    len = PR_snprintf(msgbuf, maxlen, aclerrparse2,
				      efp->ef_errv[0], efp->ef_errv[1]);
		}
		else if (efp->ef_errc == 3) {
		    /* Parse error: filename, line#, token */
		    len = PR_snprintf(msgbuf, maxlen, aclerrparse3,
				      efp->ef_errv[0], efp->ef_errv[1],
				      efp->ef_errv[2]);
		}
                else if (efp->ef_errc == 1) {
		    /* Parse error: line or pointer */
		    len = PR_snprintf(msgbuf, maxlen, aclerrparse1, 
                                      efp->ef_errv[0]);
		}
		break;

	      case ACLERRNORLM:
		if (efp->ef_errc == 1) {
		    /* No realm: name */
		    len = PR_snprintf(msgbuf, maxlen, aclerrnorlm,
				      efp->ef_errv[0]);
		}
		break;

	    case ACLERRIO:
		if (efp->ef_errc == 2) {
		    len = PR_snprintf(msgbuf, maxlen, aclerrio,
				      efp->ef_errv[0], efp->ef_errv[1]);
		}
		break;

	      default:
		len = PR_snprintf(msgbuf, maxlen, unknownerr, efp->ef_retcode);
		break;
	    }
	}
	else if (!strcmp(efp->ef_program, NSAuth_Program)) {
	    nsadbErrorFmt(errp, msgbuf, maxlen, maxdepth - depth);
	}
	else {
	    len = PR_snprintf(msgbuf, maxlen, unknownerr, efp->ef_retcode);
	}

	msgbuf += len;
	maxlen -= len;

	/* Free this frame */
	nserrFFree(errp, efp);

	if (++depth >= maxdepth) break;
    }

    /* Free any remaining error frames */
    nserrDispose(errp);
}
