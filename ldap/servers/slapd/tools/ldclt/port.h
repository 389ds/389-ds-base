#ident "ldclt @(#)port.h	1.4 01/04/03"

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
        FILE :		port.h
        AUTHOR :        Jean-Luc SCHWING
        VERSION :       1.0
        DATE :		28 November 2000
        DESCRIPTION :	
			This file contains the include (interface) definitions 
			of port.c
 LOCAL :		None.
        HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
28/11/00 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing	| 1.2 : Port on Linux.
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing	| 1.3 : Port on HP-UX.
---------+--------------+------------------------------------------------------
03/04/01 | JL Schwing	| 1.4 : Linux large file issue...
---------+--------------+------------------------------------------------------
*/

/*
 * Tuning of the code
 */
#ifdef AIX							/*JLS 01-12-00*/
#define LDCLT_CAST_SIGACTION	1				/*JLS 01-12-00*/
#endif								/*JLS 01-12-00*/

#ifdef HPUX							/*JLS 01-12-00*/
#define LDCLT_CAST_SIGACTION	1				/*JLS 01-12-00*/
#define LDCLT_NO_DLOPEN		1				/*JLS 01-12-00*/
#endif								/*JLS 01-12-00*/

#ifdef LINUX							/*JLS 01-12-00*/
#define LDCLT_CAST_SIGACTION	1				/*JLS 01-12-00*/
#ifndef O_LARGEFILE						/*JLS 03-04-01*/
# define O_LARGEFILE	0100000					/*JLS 03-04-01*/
#endif								/*JLS 03-04-01*/
#endif								/*JLS 01-12-00*/

#ifdef _WIN32							/*JLS 01-12-00*/
#define LDCLT_NO_DLOPEN		1				/*JLS 01-12-00*/
#endif								/*JLS 01-12-00*/


/************************************************************************/
/************************************************************************/
/****************         NT section              ***********************/
/************************************************************************/
/************************************************************************/

#ifdef _WIN32

typedef CRITICAL_SECTION	 ldclt_mutex_t;
typedef DWORD			 ldclt_tid;

extern int	 getopt (int argc, char **argv, char *optstring);
extern int	 getsubopt(char **optionp, char **tokens, char **valuep);
extern long	 lrand48 (void);
extern char	*optarg;
extern int	 optind;

#else /* _WIN32 */

/************************************************************************/
/************************************************************************/
/****************         Unix section            ***********************/
/************************************************************************/
/************************************************************************/

typedef pthread_mutex_t		ldclt_mutex_t;
typedef pthread_t		ldclt_tid;

#endif /* _WIN32 */


/*
 * Portability functions common to all platforms
 */
extern int	ldclt_mutex_init   (ldclt_mutex_t *mutex);
extern int	ldclt_mutex_lock   (ldclt_mutex_t *mutex);
extern int	ldclt_mutex_unlock (ldclt_mutex_t *mutex);
extern void	ldclt_sleep (int nseconds);
extern int	ldclt_thread_create (ldclt_tid *tid, void *(*fct)(void *), void *param);

/* End of file */
