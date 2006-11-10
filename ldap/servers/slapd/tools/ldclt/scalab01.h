#ident "ldclt @(#)scalab01.h	1.3 01/03/14"

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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/*
        FILE :		scalab01.h
        AUTHOR :        Jean-Luc SCHWING
        VERSION :       1.0
        DATE :		12 January 2001
        DESCRIPTION :	
			This file contains the definitions related to the 
			scenario scalab01 of ldclt.
 LOCAL :		None.
        HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
12/01/01 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
01/02/01 | JL Schwing	| 1.2 : Protect against multiple choice of same user.
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.3 : Lint cleanup.
---------+--------------+------------------------------------------------------
*/


#ifndef SCALAB01_H
#define SCALAB01_H


/*
 * Default values for scalab01
 */
#define SCALAB01_ACC_ATTRIB		"ntUserUnitsPerWeek"
#define SCALAB01_DEF_MAX_CNX		5000
#define SCALAB01_DEF_CNX_DURATION	3600
#define SCALAB01_DEF_WAIT_TIME		10
#define SCALAB01_LOCK_ATTRIB		"ntUserFlags"
#define SCALAB01_SUPER_USER_RDN		"cn=super user"
#define SCALAB01_SUPER_USER_PASSWORD	"super user password"
#define SCALAB01_VAL_LOCKED		"1"
#define SCALAB01_VAL_UNLOCKED		"0"
#define SCALAB01_MAX_LOCKING		4096

/*
 * This structure is intended to memorize the information about
 * the "ISP" users connected.
 * Uses mainly static size data to save malloc()/free() calls.
 */
typedef struct isp_user {
	char		 dn[MAX_DN_LENGTH];	/* User's DN */
	int		 cost;			/* Cnx cost */
	int		 counter;		/* To free it */
	struct isp_user	*next;			/* Next entry */
} isp_user;

/*
 * This is the scalab01 context structure.
 */
typedef struct scalab01_context {
	int		 cnxduration;	/* Max cnx duration */
	LDAP		*ldapCtx;	/* LDAP context */
	isp_user	*list;		/* ISP users list */
	ldclt_mutex_t	 list_mutex;	/* Protect list */
	char		*locking[SCALAB01_MAX_LOCKING];
	ldclt_mutex_t	 locking_mutex;
	int		 lockingMax;
	int		 maxcnxnb;	/* Modem pool size */
	int		 nbcnx;		/* Nb cnx to the modem */
	ldclt_mutex_t	 nbcnx_mutex;	/* Protect nbcnx */
	int		 wait;		/* Retry every this time */
} scalab01_context;

/*
 * Exported functions and structures
 */
extern int		 doScalab01 (thread_context *tttctx);
extern scalab01_context	 s1ctx;
extern void		*scalab01_control (void *);
extern int		 scalab01_init (void);			/*JLS 14-03-01*/

#endif /* SCALAB01_H */

/* End of file */
