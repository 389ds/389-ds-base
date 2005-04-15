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
 * Description (nseframe.c)
 *
 *	This module is part of the NSACL_RES_ERROR facility.  It contains functions
 *	for allocating, freeing, and managing error frame structures.  It
 *	does not contain routines for generating error messages through
 *	the use of a message file.
 */

#include "base/systems.h"
#include "netsite.h"
#include "libaccess/nserror.h"

/*
 * Description (nserrDispose)
 *
 *	This function is used to dispose of an entire list of error
 *	frames when the error information is no longer needed.  It
 *	does not free the list head, since it is usually not dynamically
 *	allocated.
 *
 * Arguments:
 *
 *	errp			- error frame list head pointer
 */

void nserrDispose(NSErr_t * errp)
{
    /* Ignore null list head */
    if (errp == 0) return;

    while (errp->err_first) {

	nserrFFree(errp, errp->err_first);
    }
}

/*
 * Description (nserrFAlloc)
 *
 *	This is the default allocator for error frame structures.  It
 *	calls an allocator function indicated by err_falloc, if any,
 *	or else uses MALLOC().
 *
 * Arguments:
 *
 *	errp			- error frame list head pointer
 *				  (may be null)
 *
 * Returns:
 *
 *	The return value is a pointer to a cleared error frame.  The
 *	frame will not have been added to the list referenced by errp.
 *	An allocation error is indicated by a null return value.
 */

NSEFrame_t * nserrFAlloc(NSErr_t * errp)
{
    NSEFrame_t * efp;			/* return error frame pointer */

    /* Allocate the error frame */
    efp = (errp && errp->err_falloc)
			? (*errp->err_falloc)(errp)
			: (NSEFrame_t *)MALLOC(sizeof(NSEFrame_t));

    if (efp) {
	/* Clear the error frame */
	memset((void *)efp, 0, sizeof(NSEFrame_t));
    }

    return efp;
}

/*
 * Description (nserrFFree)
 *
 *	This function frees an error frame structure.  If an error list
 *	head is specified, it first checks whether the indicated error
 *	frame is on the list, and removes it if so.  If the ef_dispose
 *	field is non-null, the indicated function is called.  The error
 *	frame is deallocated using either a function indicated by
 *	err_free in the list head, or FREE() otherwise.
 *
 * Arguments:
 *
 *	errp			- error frame list head pointer
 *				  (may be null)
 *	efp			- error frame pointer
 */

void nserrFFree(NSErr_t * errp, NSEFrame_t * efp)
{
    NSEFrame_t **lefp;		/* pointer to error frame pointer */
    NSEFrame_t * pefp;		/* previous error frame on list */
    int i;

    /* Ignore null error frame pointer */
    if (efp == 0) return;

    /* Got a list head? */
    if (errp) {

	/* Yes, see if this frame is on the list */
	pefp = 0;
	for (lefp = &errp->err_first; *lefp != 0; lefp = &pefp->ef_next) {
	    if (*lefp == efp) {

		/* Yes, remove it */
		*lefp = efp->ef_next;
		if (errp->err_last == efp) errp->err_last = pefp;
		break;
	    }
	    pefp = *lefp;
	}
    }

    /* Free strings referenced by the frame */
    for (i = 0; i < efp->ef_errc; ++i) {
	if (efp->ef_errv[i]) {
	    FREE(efp->ef_errv[i]);
	}
    }

    /* Free the frame */
    if (errp && errp->err_ffree) {
	(*errp->err_ffree)(errp, efp);
    }
    else {
	FREE(efp);
    }
}

/*
 * Description (nserrGenerate)
 *
 *	This function is called to generate an error frame and add it
 *	to a specified error list.
 *
 * Arguments:
 *
 *	errp			- error frame list head pointer
 *				  (may be null)
 *	retcode			- return code (ef_retcode)
 *	errorid			- error id (ef_errorid)
 *	program			- program string pointer (ef_program)
 *	errc			- count of error arguments (ef_errc)
 *	...			- values for ef_errv[]
 *
 * Returns:
 *
 *	The return value is a pointer to the generated error frame, filled
 *	in with the provided information.  An allocation error is indicated
 *	by a null return value.
 */

NSEFrame_t * nserrGenerate(NSErr_t * errp, long retcode, long errorid,
			   char * program, int errc, ...)
{
    NSEFrame_t * efp;			/* error frame pointer */
    char * esp;				/* error string pointer */
    int i;
    va_list ap;

    /* Null frame list head pointer means do nothing */
    if (errp == 0) {
	return 0;
    }

    /* Limit the number of values in ef_errv[] */
    if (errc > NSERRMAXARG) errc = NSERRMAXARG;

    /* Allocate the error frame */
    efp = nserrFAlloc(errp);

    /* Did we get it? */
    if (efp) {

	/* Yes, copy information into it */
	efp->ef_retcode = retcode;
	efp->ef_errorid = errorid;
	efp->ef_program = program;
	efp->ef_errc = errc;

	/* Get the string arguments and copy them */
	va_start(ap, errc);
	for (i = 0; i < errc; ++i) {
	    esp = va_arg(ap, char *);
	    efp->ef_errv[i] = STRDUP(esp);
	}

	/* Add the frame to the list (if any) */
	if (errp) {
	    efp->ef_next = errp->err_first;
	    errp->err_first = efp;
	    if (efp->ef_next == 0) errp->err_last = efp;
	}
    }

    /* Return the error frame pointer */
    return efp;
}
