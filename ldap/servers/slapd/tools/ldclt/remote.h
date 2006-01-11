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
        FILE :		remote.h
        AUTHOR :        Jean-Luc SCHWING
        VERSION :       1.0
        DATE :		04 May 1999
        DESCRIPTION :	
			This file contains the definitions used by the remote 
			control module of ldclt.
 LOCAL :		None.
        HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
04/05/99 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
05/05/99 | F. Pistolesi	| 1.3 : Implements communication with remote part.
---------+--------------+------------------------------------------------------
06/05/99 | JL Schwing	| 1.4 : Port on Solaris 2.5.1
---------+--------------+------------------------------------------------------
*/

/*
 * Network includes
 */

#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef OSF1
#ifndef _UINT32_T
#define _UINT32_T
typedef unsigned long	uint32_t;
#endif /* _UINT32_T */
#else /* OS_RELEASE */
#include <inttypes.h>
#endif /* OS_RELEASE */
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

typedef struct {
	uint32_t	 type,res,dnSize;
	char		 dn[sizeof(uint32_t)];
} repconfirm;

extern int masterPort;

/* End of file */
