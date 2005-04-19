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
