/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* cl4_api.h - minimal interface to 4.0 changelog necessary to link 4.0 changelog
			   to 5.0 replication
 */

#ifndef CL4_API_H
#define CL4_API_H

#include "repl.h"

/*** Error Codes ***/
enum
{
	CL4_SUCCESS,
	CL4_BAD_DATA,
	CL4_BAD_FORMAT,
	CL4_NOT_FOUND,
	CL4_MEMORY_ERROR,
	CL4_CSNPL_ERROR,
	CL4_LDAP_ERROR,
	CL4_INTERNAL_ERROR
};

/*** APIs ***/
/*	Name:			cl4Init
	Description:	initializes 4.0 changelog subsystem
	Parameters:		none
	Return:			????
 */
int cl4Init ();

/*	Name:			cl4WriteOperation
	Description:	logs operation to 4.0 changelog; operation must go through CD&R engine first
	Parameters:		op - operation to be logged
					
	Return:			????
 */
int cl4WriteOperation (const slapi_operation_parameters *op);

/*	Name:			cl4ChangeTargetDN
	Description:	modifies change entry target dn; should be called for conflicts due to naming collisions;
					raw dn should be passed for add operations; normolized dn otherwise.
	Parameters:		csn - csn of the change entry to be modified
					newDN - new target dn of the entry
	Return:			????
 */
int cl4ChangeTargetDN (const CSN* csn, const char *newDN);

/*	Name:			cl4AssignChangeNumbers
	Description:	this function should be called periodically to assign change numbers to changelog
					entries. Intended for use with event queue
	Parameters:		parameters are not currently used
	Return:			none
 */
void cl4AssignChangeNumbers (time_t when, void *arg);

/*	Name:			cl4Cleanup
	Description:	frees memory held by 4.0 changelog subsystem
	Parameters:		none
	Return:			none
 */
void cl4Clean ();
#endif
