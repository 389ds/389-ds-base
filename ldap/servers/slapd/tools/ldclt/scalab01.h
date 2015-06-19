#ident "ldclt @(#)scalab01.h	1.3 01/03/14"

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
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
