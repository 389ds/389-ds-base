#ident "ldclt @(#)workarounds.c	1.5 00/12/01"

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
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
        FILE :		workarounds.c
        AUTHOR :        Jean-Luc SCHWING
        VERSION :       1.0
        DATE :		15 December 1998
        DESCRIPTION :	
			This file contains special work-arounds targetted to 
			fix, or work-around, the various bugs that may appear 
			in Solaris 2.7 libldap.
 	LOCAL :		None.
        HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
15/12/98 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
19/09/00 | JL Schwing	| 1.2: Port on Netscape's libldap. This is realized in
			|   such a way that this library become the default
			|   way so a ifdef for Solaris will be used...
---------+--------------+------------------------------------------------------
16/11/00 | JL Schwing	| 1.3 : lint cleanup.
-----------------------------------------------------------------------------
29/11/00 | JL Schwing	| 1.4 : Port on NT 4.
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing	| 1.5 : Port on Linux.
---------+--------------+------------------------------------------------------
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>	/* exit(), ... */			/*JLS 16-11-00*/
#include "lber.h"
#include "ldap.h"
#ifdef SOLARIS_LIBLDAP						/*JLS 19-09-00*/
#include "ldap-private.h"
#else								/*JLS 19-09-00*/
#ifndef _WIN32							/*JLS 01-12-00*/
#include <pthread.h>						/*JLS 01-12-00*/
#endif								/*JLS 01-12-00*/
#include "port.h"	/* Portability definitions */		/*JLS 29-11-00*/
#include "ldclt.h"						/*JLS 19-09-00*/
#endif								/*JLS 19-09-00*/




/* ****************************************************************************
	FUNCTION :	getFdFromLdapSession
	PURPOSE :	This function is a work-around for the bug 4197228
			that is not expected to be fixed soon...
	INPUT :		ld	= ldap session to process.
	OUTPUT :	fd	= the corresponding fd.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int getFdFromLdapSession (
	LDAP	*ld,
	int	*fd)
{
#ifdef SOLARIS_LIBLDAP						/*JLS 19-09-00*/
  *fd = ld->ld_sb.sb_sd;
  return (0);
#else								/*JLS 19-09-00*/
  printf("Error : getFdFromLdapSession() not implemented...\n");/*JLS 19-09-00*/
  exit (EXIT_OTHER);						/*JLS 19-09-00*/
#endif								/*JLS 19-09-00*/
}


/* End of file */
