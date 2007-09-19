#ident "ldclt @(#)ldclt.c	1.89 01/06/19"

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
	FILE :		ldclt.c
	AUTHOR :	Jean-Luc SCHWING
	VERSION :       1.0
	DATE :		03 December 1998
	DESCRIPTION :	
			This file is the main file of the ldclt tool. This tool 
			is targetted to be a multi-threaded ldap client, 
			specially designed to ensure good reliability of both 
			the basic ldap server purpose, and the replication 
			processes. It is *not* targetted against the 
			functionnality aspect of the product, but rather on the 
			stress and long-term operation.
 	LOCAL :		None.
	HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
03/12/98 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
10/12/98 | JL Schwing	| 1.2 : Add statistics report when exiting.
---------+--------------+------------------------------------------------------
10/12/98 | JL Schwing	| 1.3 : Trap SIGQUIL to issue statistics without exit.
			| Bug fix - call ldap_err2string() to decode ldap errs.
---------+--------------+------------------------------------------------------
11/12/98 | JL Schwing	| 1.4 : Implement max errors threshold.
			| fflush(stdout) after each printf.
			| Will exit(0) on SIGINT
---------+--------------+------------------------------------------------------
14/12/98 | JL Schwing	| 1.5 : Implement "-e close".
			| Ensure thread not dead prior to issue "no activity" 
			| Add statts for the number of dead threads.
---------+--------------+------------------------------------------------------
16/12/98 | JL Schwing	| 1.6 : Implement "-e add" and "-e delete".
			| Improve printout of options.
---------+--------------+------------------------------------------------------
24/12/98 | JL Schwing	| 1.7 : Fix memory problem.
---------+--------------+------------------------------------------------------
28/12/98 | JL Schwing	| 1.8 : Add more statistics.
---------+--------------+------------------------------------------------------
29/12/98 | JL Schwing	| 1.9 : Implement -Q.
---------+--------------+------------------------------------------------------
29/12/98 | JL Schwing	| 1.10: Don't print pending stats if not asynchronous.
---------+--------------+------------------------------------------------------
29/12/98 | JL Schwing	| 1.11: Fix typo.
---------+--------------+------------------------------------------------------
30/12/98 | JL Schwing	| 1.12: Protect messages "no activity for %d seconds"
			|   by SUPER_QUIET mode.
---------+--------------+------------------------------------------------------
11/01/99 | JL Schwing	| 1.13: Implement "-e emailPerson".
---------+--------------+------------------------------------------------------
13/01/99 | JL Schwing	| 1.14: Implement "-e string".
---------+--------------+------------------------------------------------------
14/01/99 | JL Schwing	| 1.15: Implement "-s <scope>".
---------+--------------+------------------------------------------------------
18/01/99 | JL Schwing	| 1.16: Implement "-e randombase".
---------+--------------+------------------------------------------------------
18/01/99 | JL Schwing	| 1.17: Implement "-e v2".
---------+--------------+------------------------------------------------------
21/01/99 | JL Schwing	| 1.18: Implement "-e ascii".
---------+--------------+------------------------------------------------------
26/01/99 | JL Schwing	| 1.19: Implement "-e noloop".
---------+--------------+------------------------------------------------------
28/01/99 | JL Schwing	| 1.20: Implement "-T <total>".
---------+--------------+------------------------------------------------------
04/05/99 | JL Schwing	| 1.21: Implement operations list.
---------+--------------+------------------------------------------------------
06/05/99 | JL Schwing	| 1.25: Add proper shutdwon (wait for check threads).
			| Implement "-P <master port>".
---------+--------------+------------------------------------------------------
06/05/99 | JL Schwing	| 1.26: Implement "-e modrdn".
---------+--------------+------------------------------------------------------
06/05/99 | F. Pistolesi	| 1.27: Some fixes.
---------+--------------+------------------------------------------------------
07/05/99 | JL Schwing	| 1.28: Some fixes.
---------+--------------+------------------------------------------------------
19/05/99 | JL Schwing	| 1.30: Implement "-e rename".
			| Set the threads status to DEAD when nb of opers done.
			| Lint-cleanup.
---------+--------------+------------------------------------------------------
21/05/99 | JL Schwing	| 1.31: Fix Unitialized Memory Read for head's mutex.
---------+--------------+------------------------------------------------------
27/05/99 | JL Schwing	| 1.32 : Add statistics to check threads.
---------+--------------+------------------------------------------------------
28/05/99 | JL Schwing	| 1.33 : Add new option -W (wait).
---------+--------------+------------------------------------------------------
02/06/99 | JL Schwing	| 1.34 : Add flag in main ctx to know if slave was 
			|   connected or not.
			| Add counter of operations received in check threads.
---------+--------------+------------------------------------------------------
06/03/00 | JL Schwing	| 1.35: Test malloc() and strdup() return value.
---------+--------------+------------------------------------------------------
04/08/00 | JL Schwing	| 1.36: Add stats on nb inactivity per thread.
---------+--------------+------------------------------------------------------
08/08/00 | JL Schwing	| 1.37: Print global statistics every 1000 loops.
---------+--------------+------------------------------------------------------
18/08/00 | JL Schwing	| 1.38: Print global statistics every 90 loops.
			| Bug fix in this new feature.
			| Print ldclt version.
			| Print date of begin and of end.
			| Add new function ldcltExit().
---------+--------------+------------------------------------------------------
25/08/00 | JL Schwing	| 1.39: Implement consistent exit status...
---------+--------------+------------------------------------------------------
25/08/00 | JL Schwing	| 1.40: Will only load images if -e emailPerson
---------+--------------+------------------------------------------------------
11/10/00 | B Kolics     | 1.41: Implement "-Z certfile".
---------+--------------+------------------------------------------------------
26/10/00 | B Kolics     | 1.42: Move SSL client initialization to basicInit()
-------------------------------------------------------------------------------
07/11/00 | JL Schwing	| 1.43: Add handlers for dynamic load of ssl-related
			|   functions.
			| Add new function sslDynLoadInit().
-----------------------------------------------------------------------------
07/11/00 | JL Schwing	| 1.44: Implement "-e inetOrgPerson".
---------+--------------+------------------------------------------------------
08/11/00 | JL Schwing	| 1.45: Improve error message when initiating ssl.
---------+--------------+------------------------------------------------------
13/11/00 | JL Schwing	| 1.46: Add new options "-e randombaselow and ...high"
			| Made use of exit (EXIT_PARAMS) in main().
---------+--------------+------------------------------------------------------
14/11/00 | JL Schwing	| 1.47: Port on AIX.
---------+--------------+------------------------------------------------------
16/11/00 | JL Schwing	| 1.48: Implement "-e imagesdir=path".
---------+--------------+------------------------------------------------------
17/11/00 | JL Schwing	| 1.49: Implement "-e smoothshutdown".
			| Forget to add mode decoding.
			| Add new function shutdownThreads().
---------+--------------+------------------------------------------------------
21/11/00 | JL Schwing	| 1.50: Implement "-e attreplace=name:mask"
			| Add new function parseFilter().
---------+--------------+------------------------------------------------------
22/11/00 | JL Schwing	| 1.51: Will now use LD_LIBRARY_PATH to load libssl.
---------+--------------+------------------------------------------------------
24/11/00 | B Kolics     | 1.52: Added SSL client authentication
---------+--------------+------------------------------------------------------
29/11/00 | JL Schwing	| 1.53: Port on NT 4.
---------+--------------+------------------------------------------------------
30/11/00 | JL Schwing	| 1.54: Bug fix - bad error message if -eimagesdir=path
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing	| 1.55: Port on Linux.
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing	| 1.56: Port on HP-UX.
---------+--------------+------------------------------------------------------
07/12/00 | JL Schwing	| 1.57: Bug fix - crash SIGBUS in main:1840 if no
			|   filter is provided to the tool.
			| Build the argv list before parsing.
---------+--------------+------------------------------------------------------
15/12/00 | JL Schwing	| 1.58: Implement "-e counteach".
			| Implement "-e withnewparent".
---------+--------------+------------------------------------------------------
18/12/00 | JL Schwing	| 1.59: Fix an exit status problem.
---------+--------------+------------------------------------------------------
18/12/00 | JL Schwing	| 1.60: Minor fix/improvement in -I management.
---------+--------------+------------------------------------------------------
19/12/00 | JL Schwing	| 1.61: Implement "-e noglobalstats".
---------+--------------+------------------------------------------------------
19/12/00 | JL Schwing	| 1.62: Add comments.
---------+--------------+------------------------------------------------------
03/01/01 | JL Schwing	| 1.63: Implement "-e attrsonly=value".
---------+--------------+------------------------------------------------------
05/01/01 | JL Schwing	| 1.64: Implement "-e randombinddn" and associated
			|   "-e randombinddnlow/high"
---------+--------------+------------------------------------------------------
08/01/01 | JL Schwing	| 1.65: Implement "-e scalab01".
			| Replace all exit() by ldcltExit()
---------+--------------+------------------------------------------------------
12/01/01 | JL Schwing	| 1.66: Second set of options for -e scalab01
			| Sanity check in decodeExecParams().
---------+--------------+------------------------------------------------------
26/02/01 | JL Schwing	| 1.67: Use ldclt_sleep() not sleep() (NT issue).
---------+--------------+------------------------------------------------------
08/03/01 | JL Schwing	| 1.68: Change referrals handling.
			| Use a static char[] to store ldclt version.
			| Add new function decodeReferralParams().
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.69: Implement "-e commoncounter"
			| Add a control for referral mode.
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.70: Implement "-e dontsleeponserverdown".
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.71: Misc fixes for HP-UX compilation.
---------+--------------+------------------------------------------------------
15/03/01 | JL Schwing	| 1.72: Implement "-e attrlist=name:name:name"
			| Implement "-e randomattrlist=name:name:name"
---------+--------------+------------------------------------------------------
19/03/01 | JL Schwing	| 1.73: Implement "-e object=filename"
			| Bug fix - understand EXIT_INIT in ldcltExit().
			| Implement "-e genldif=filename"
			| Add new function tttctxInit().
---------+--------------+------------------------------------------------------
21/03/01 | JL Schwing	| 1.74: Implements variables in "-e object=filename"
			| Add new function copyVersObject().
---------+--------------+------------------------------------------------------
23/03/01 | JL Schwing	| 1.75: Implements data file list support in variants.
			| Implements "-e rdn=value".
			| Add new functions copyVersAttribute() decodeRdnParam()
---------+--------------+------------------------------------------------------
28/03/01 | JL Schwing	| 1.76: Update options checking for "-e rdn=value".
---------+--------------+------------------------------------------------------
28/03/01 | JL Schwing	| 1.77: Support -e commoncounter with -e rdn/object
			| Remove MAX_ATTRLIST - use MAX_ATTRIBS.
			| Bug fix - forget to initiate some fields in -e rdn=
---------+--------------+------------------------------------------------------
02/04/01 | JL Schwing	| 1.78: Bug fix : large files support for -e genldif.
---------+--------------+------------------------------------------------------
03/04/01 | JL Schwing	| 1.79: Port on _WIN32 and OSF1.
---------+--------------+------------------------------------------------------
05/04/01 | JL Schwing	| 1.80: Bug fix - forget to print genldif file name.
---------+--------------+------------------------------------------------------
05/04/01 | JL Schwing	| 1.81: Implement -e append.
---------+--------------+------------------------------------------------------
11/04/01 | JL Schwing	| 1.82: Bug fix in -e append.
---------+--------------+------------------------------------------------------
23/04/01 | JL Schwing	| 1.83: Improved arguments print at startup.
			| Add new function buildArgListString().
---------+--------------+------------------------------------------------------
23/04/01 | JL Schwing	| 1.84: Exit on error 2 if extra arguments provided.
---------+--------------+------------------------------------------------------
03/05/01 | JL Schwing	| 1.85: Implement -e randombinddnfromfile=filename.
---------+--------------+------------------------------------------------------
04/05/01 | JL Schwing	| 1.86: Implement -e bindonly.
---------+--------------+------------------------------------------------------
04/05/01 | JL Schwing	| 1.87: Fix options check.
---------+--------------+------------------------------------------------------
16/06/01 | JL Schwing	| 1.89: Allow SSL for HP-UX.
---------+--------------+------------------------------------------------------
*/

#include <stdlib.h>		/* exit(), etc... */
#include <stdio.h>		/* printf(), etc... */
#include <signal.h>		/* sigset(), etc... */
#include <string.h>		/* strerror(), etc... */
#include <errno.h>		/* errno, etc... */		/*JLS 06-03-00*/
#include <fcntl.h>		/* O_RDONLY, etc... */		/*JLS 02-04-01*/
#include <time.h>		/* ctime(), etc... */		/*JLS 18-08-00*/
#include <lber.h>		/* ldap C-API BER decl. */
#include <ldap.h>		/* ldap C-API decl. */
#include <ldap_ssl.h>           /* ldapssl_init(), etc... */
#ifdef LDAP_H_FROM_QA_WKA
#include <proto-ldap.h>		/* ldap C-API prototypes */
#endif
#ifndef _WIN32							/*JLS 29-11-00*/
#include <pthread.h>		/* pthreads(), etc... */
#include <unistd.h>		/* close(), etc... */
#include <dlfcn.h>		/* dlopen(), etc... */		/*JLS 07-11-00*/
#include <sys/resource.h>	/* setrlimit(), etc... */
#include <sys/time.h>		/* struct rlimit, etc... */
#endif

#include "port.h"		/* Portability definitions */	/*JLS 29-11-00*/
#include "ldclt.h"		/* This tool's include file */
#include "utils.h"		/* Utilities functions */	/*JLS 16-11-00*/
#include "scalab01.h"		/* Scalab01 specific */		/*JLS 12-01-01*/



/*
 * Global variables
 */
main_context	 mctx;			/* Main context */
thread_context	 tctx [MAX_THREADS];	/* Threads contextes */
check_context	 cctx [MAX_SLAVES];	/* Check threads contextes */
int 		 masterPort=16000;

extern char	*ldcltVersion;		/* ldclt version */	/*JLS 18-08-00*/










					/* New function */	/*JLS 18-08-00*/
/* ****************************************************************************
	FUNCTION :	ldcltExit
	PURPOSE :	Print the last data then exit the process.
	INPUT :		status	= exit status
	OUTPUT :	None.
	RETURN :	None.
	DESCRIPTION :
 *****************************************************************************/
void
ldcltExit (
	int	 status)
{
  time_t	 tim;

  tim = time (NULL);
  printf ("ldclt[%d]: Ending at %s", mctx.pid, ctime (&tim));
  printf ("ldclt[%d]: Exit status %d - ", mctx.pid, status);
  switch (status)						/*JLS 25-08-00*/
  {								/*JLS 25-08-00*/
    case EXIT_OK:						/*JLS 25-08-00*/
	printf ("No problem during execution.\n");		/*JLS 25-08-00*/
	break;							/*JLS 25-08-00*/
    case EXIT_PARAMS:						/*JLS 25-08-00*/
	printf ("Error in parameters.\n");			/*JLS 25-08-00*/
	break;							/*JLS 25-08-00*/
    case EXIT_MAX_ERRORS:					/*JLS 25-08-00*/
	printf ("Max errors reached.\n");			/*JLS 25-08-00*/
	break;							/*JLS 25-08-00*/
    case EXIT_NOBIND:						/*JLS 25-08-00*/
	printf ("Cannot bind.\n");				/*JLS 25-08-00*/
	break;							/*JLS 25-08-00*/
    case EXIT_LOADSSL:						/*JLS 07-11-00*/
	printf ("Cannot load libssl.\n");			/*JLS 07-11-00*/
	break;							/*JLS 07-11-00*/
    case EXIT_INIT:						/*JLS 19-03-01*/
	printf ("Cannot initialize ldclt.\n");			/*JLS 19-03-01*/
	break;							/*JLS 19-03-01*/
    case EXIT_OTHER:						/*JLS 25-08-00*/
	printf ("Other kind of error.\n");			/*JLS 25-08-00*/
	break;							/*JLS 25-08-00*/
    default:							/*JLS 25-08-00*/
	printf ("Undocumented error - update source code.\n");	/*JLS 25-08-00*/
	break;							/*JLS 25-08-00*/
  }								/*JLS 25-08-00*/
  exit (status);
}


						/* New */	/*JLS 23-03-01*/
/* ****************************************************************************
	FUNCTION :	copyVersAttribute
	PURPOSE :	Copy a versatile object's attribute
	INPUT :		srcattr	= source attribute
	OUTPUT :	dstattr	= destination attribute
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
copyVersAttribute (
	vers_attribute	*srcattr,
	vers_attribute	*dstattr)
{
  vers_field	*src;		/* Source field */
  vers_field	*dst;		/* Destination field */

  dstattr->name  = srcattr->name;
  dstattr->src   = srcattr->src;
  dstattr->field = (vers_field *) malloc (sizeof (vers_field));

  /*
   * Copy each field of the attribute
   */
  src = srcattr->field;
  dst = dstattr->field;
  while (src != NULL)
  {
    memcpy (dst, src, sizeof (vers_field));
    dst->commonField = src;					/*JLS 28-03-01*/
    if ((src = src->next) != NULL)
    {
      dst->next = (vers_field *) malloc (sizeof (vers_field));
      dst = dst->next;
    }
  }

  /*
   * Do we need a buffer ?
   */
  if (srcattr->buf == NULL)					/*JLS 28-03-01*/
    dstattr->buf = NULL;					/*JLS 28-03-01*/
  else								/*JLS 28-03-01*/
    dstattr->buf = (char *) malloc (MAX_FILTER);

  /*
   * End of function
   */
  return (0);
}








						/* New */	/*JLS 21-03-01*/
/* ****************************************************************************
	FUNCTION :	copyVersObject
	PURPOSE :	Copy a versatile object.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	NULL if error, copy of the object else.
	DESCRIPTION :
 *****************************************************************************/
vers_object *
copyVersObject (
	vers_object	*srcobj)
{
  vers_object	*newobj;	/* New object */
  int		 i;		/* For the loops */

  /*
   * Copy the object and initiates the buffers...
   */
  newobj            = (vers_object *) malloc (sizeof (vers_object));
  newobj->attribsNb = srcobj->attribsNb;
  newobj->fname     = srcobj->fname;

  /*
   * Initiates the variables
   */
  for (i=0 ; i+VAR_MIN < VAR_MAX ; i++)
    if (srcobj->var[i] == NULL)
      newobj->var[i] = NULL;
    else
      newobj->var[i] = (char *) malloc (MAX_FILTER);

  /*
   * Maybe copy the rdn ?
   */
  if (srcobj->rdn != NULL)
  {
    newobj->rdnName = strdup (srcobj->rdnName);
    newobj->rdn     = (vers_attribute *) malloc (sizeof (vers_attribute));
    if (copyVersAttribute (srcobj->rdn, newobj->rdn) < 0)
      return (NULL);
  }

  /*
   * Copy each attribute
   */
  for (i=0 ; i < srcobj->attribsNb ; i++)
    if (copyVersAttribute (&(srcobj->attribs[i]), &(newobj->attribs[i])) < 0)
      return (NULL);

  /*
   * Return the new object
   */
  return (newobj);
}










						/* New */	/*JLS 19-03-01*/
/* ****************************************************************************
	FUNCTION :	tttctxInit
	PURPOSE :	Initiates the thread context
	INPUT :		num	= thread number
	OUTPUT :	tttctx	= initiates context
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
tttctxInit (
	int		 num,
	thread_context	*tttctx)
{
  int	 ret;	/* ldclt_mutex_init() return value */

  /*
   * Initialize data for this thread
   */
  tttctx->active      = mctx.inactivMax + 1;
  tttctx->attrlist[0] = NULL;
  tttctx->exitStatus  = EXIT_OK;
  tttctx->nbInactRow  = 0;
  tttctx->nbInactTot  = 0;
  tttctx->mode        = mctx.mode;
  tttctx->status      = FREE;
  tttctx->thrdNum     = num;
  tttctx->totalReq    = mctx.totalReq;
  sprintf (tttctx->thrdId, "T%03d", tttctx->thrdNum);

  if (mctx.mod2 & M2_OBJECT)
  {
    tttctx->bufObject1 = (char *) malloc (MAX_FILTER);
    if ((tttctx->object = copyVersObject (&(mctx.object))) == NULL)
      return(-1);
  }

  /*
   * Initiate the mutexes that protect the private data structures.
   */
  if ((ret = ldclt_mutex_init(&(tttctx->nbOpers_mutex))) != 0)
  {
    fprintf (stderr, "ldclt: %s\n", strerror (ret));
    fprintf (stderr,"Error: cannot initiate nbOpers_mutex %s\n",tttctx->thrdId);
    fflush (stderr);
    return (-1);
  }
  if ((ret = ldclt_mutex_init (&(tttctx->status_mutex))) != 0)
  {
    fprintf (stderr, "ldclt: %s\n", strerror (ret));
    fprintf (stderr, "Error: cannot initiate status_mutex %s\n",tttctx->thrdId);
    fflush (stderr);
    return (-1);
  }

  return (0);
}






/* ****************************************************************************
	FUNCTION :	runThem
	PURPOSE :	This function implements the launching of the threads.
			The monitoring will be realized in monitorIt().
			This function also create the check threads if needed.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
runThem (void)
{
  int	 i;	/* For the loops */				/*JLS 23-03-01*/
  int	 ret;	/* pthread_xxx() return value */


#ifdef SOLARIS							/*JLS 17-11-00*/
  /*
   * Maybe create the check operation threads.
   */
  if (mctx.slavesNb > 0)
  {
    for (i=0 ; i<mctx.slavesNb ; i++)
    {
      if (mctx.mode & VERY_VERBOSE)
	printf ("ldclt[%d]: Creating thread C%03d\n", mctx.pid, i);

      /*
       * Initiate context for this thread
       */
      cctx[i].headListOp  = mctx.opListTail;
      cctx[i].status      = DEAD;
      cctx[i].thrdNum     = i;
      cctx[i].calls       = 0;
      cctx[i].slaveName   = NULL;
      cctx[i].nbEarly     = 0;
      cctx[i].nbLate      = 0;
      cctx[i].nbLostOp    = 0;
      cctx[i].nbNotOnList = 0;
      cctx[i].nbOpRecv    = 0;
      cctx[i].nbRepFail32 = 0;
      cctx[i].nbRepFail68 = 0;
      cctx[i].nbRepFailX  = 0;
      cctx[i].nbStillOnQ  = 0;
    }

    /*
     * Create the main (aka monitoring) check operation thread.
     */
    if ((ret = ldclt_thread_create (NULL, opCheckMain, NULL)) != 0)
    {
      fprintf (stderr, "ldclt[%d]: %s\n", mctx.pid, strerror (ret));
      fprintf (stderr, "ldclt[%d]: Error: cannot create thread C%03d\n", mctx.pid, i);
      fflush (stderr);
      return (-1);
    }
  }
#endif /* SOLARIS */						/*JLS 14-11-00*/

  /*
   * Maybe we need to start a special control thread ?
   */
  if (mctx.mode & SCALAB01)					/*JLS 08-01-01*/
  {								/*JLS 08-01-01*/
    ldclt_tid	 dummy;	/* Don't need tid */			/*JLS 14-03-01*/
								/*JLS 08-01-01*/
    if ((ret = ldclt_thread_create (&dummy,			/*JLS 08-01-01*/
			scalab01_control, (void *)NULL)) != 0)	/*JLS 08-01-01*/
    {								/*JLS 08-01-01*/
      fprintf (stderr, "ldclt[%d]: %s\n", mctx.pid, strerror (ret));		/*JLS 08-01-01*/
      fprintf (stderr, "ldclt[%d]: Error: cannot create thread scalab01_control\n", mctx.pid);
      fflush (stderr);						/*JLS 08-01-01*/
      return (-1);						/*JLS 08-01-01*/
    }								/*JLS 08-01-01*/
    ldclt_sleep (2);	/* Time to initialize */		/*JLS 26-02-01*/
  }								/*JLS 08-01-01*/

  /*
   * Ok, the check operation threads are now created (if needed).
   * Let's create the (working) ldap client threads.
   */
  for (i=0 ; i<mctx.nbThreads ; i++)
  {
    if (mctx.mode & VERY_VERBOSE)
      printf ("ldclt[%d]: Creating thread T%03d\n", mctx.pid, i);
    if (tttctxInit (i, &(tctx[i])) < 0)				/*JLS 19-03-01*/
      return (-1);						/*JLS 19-03-01*/

    /*
     * Create the thread
     */
    if ((ret = ldclt_thread_create (&(tctx[i].tid),
			threadMain, (void *)&(tctx[i]))) != 0)
    {
      fprintf (stderr, "ldclt[%d]: %s\n", mctx.pid, strerror (ret));
      fprintf (stderr, "ldclt[%d]: Error: cannot create thread T%03d\n", mctx.pid, i);
      fflush (stderr);
      return (-1);
    }
  }

  return (0);
}







					/* New function */	/*JLS 17-11-00*/
/* ****************************************************************************
	FUNCTION :	shutdownThreads
	PURPOSE :	This function is targetted to shutdown the threads.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
shutdownThreads (void)
{
  int	 i;		/* To process the threads */
  int	 status;	/* Thread's status */
  int	 allDead;	/* All threads are dead */
  int	 maxLoops;	/* Max loops waiting for DEAD */
  int	 ret;		/* Return code */

  /*
   * Command all the threads to shutdown.
   * Must set to MUST_SHUTDOWN only if not dead, hence need to
   * expose the mutex here...
   */
  for (i=0 ; i<mctx.nbThreads ; i++)
  {
    if ((ret = ldclt_mutex_lock (&(tctx[i].status_mutex))) != 0)
    {
      fprintf (stderr, 
		"Cannot mutex_lock(T%03d), error=%d (%s)\n", 
		tctx[i].thrdNum, ret, strerror (ret));
      printf ("Cannot command shutdwon to thread %d\n", tctx[i].thrdNum);
      return (-1);
    }
    if (tctx[i].status != DEAD)
      tctx[i].status = MUST_SHUTDOWN;
    if ((ret = ldclt_mutex_unlock (&(tctx[i].status_mutex))) != 0)
    {
      fprintf (stderr, 
		"Cannot mutex_unlock(T%03d), error=%d (%s)\n", 
		tctx[i].thrdNum, ret, strerror (ret));
      printf ("Cannot command shutdwon to thread %d\n", tctx[i].thrdNum);
      return (-1);
    }
  }

  /*
   * Wait (maybe ?) for the thread to actually die...
   */
  if (mctx.mode & SMOOTHSHUTDOWN)
  {
    int alivecnt;
    allDead  = 0;
    maxLoops = 20;
    while (maxLoops && !allDead)
    {
      allDead = 1;
      alivecnt = 0;
      for (i=0 ; i<mctx.nbThreads ; i++)
      {
	if (getThreadStatus (&(tctx[i]), &status) < 0)
	{
	    printf ("Cannot command shutdown to thread %d\n", tctx[i].thrdNum);
	    return (-1);
	}
	if (status != DEAD)
        {
	   allDead = 0;
	   alivecnt++;
        }
      }
      maxLoops--;
      if (!allDead)
	ldclt_sleep (1);
    }

    if (!maxLoops) 
    {
      printf ("%d thread(s) don't die...\n", alivecnt);
      return (-1);
    }
  }

  return (0);
}







/* ****************************************************************************
	FUNCTION :	monitorThem
	PURPOSE :	This function will monitor all the client threads.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
monitorThem (void)
{
  int	 i;		/* To parse the threads */
  int	 ret;		/* Return value */
  int	 nbOpers;	/* This thread nb of operations */
  int	 nbOpersTot;	/* Total nb of operations */
  int	 allDead = 0;	/* All threads are dead */
  int	 status;	/* Thread's status */			/*JLS 17-11-00*/

  while (!allDead)
  {
    ldclt_sleep (mctx.sampling);
    nbOpersTot = 0;
    allDead    = 1;	/* Assume all threads are dead */

    /*
     * Parse all the threads
     */
    for (i=0 ; i<mctx.nbThreads ; i++)
    {
      status = RUNNING;
      if ((ret = ldclt_mutex_lock (&(tctx[i].nbOpers_mutex))) != 0)
      {
	fprintf (stderr, 
		"ldclt[%d]: Cannot mutex_lock(T%03d), error=%d (%s)\n", 
		mctx.pid, tctx[i].thrdNum, ret, strerror (ret));
	fflush (stderr);
	return (-1);
      }
      nbOpers         = tctx[i].nbOpers;
      tctx[i].nbOpers = 0;
      if ((ret = ldclt_mutex_unlock (&(tctx[i].nbOpers_mutex))) != 0)
      {
	fprintf (stderr,
		"ldclt[%d]: Cannot mutex_unlock(T%03d), error=%d (%s)\n",
		mctx.pid, tctx[i].thrdNum, ret, strerror (ret));
	fflush (stderr);
	return (-1);
      }

      /*
       * Report results, and check activity
       */
      if (mctx.mode & VERY_VERBOSE)
	printf ("ldclt[%d]: T%03d: nbOpers = %d\n", mctx.pid, tctx[i].thrdNum, nbOpers);
      if (nbOpers != 0)
      {
	tctx[i].active      = mctx.inactivMax + 1;
	tctx[i].nbInactRow  = 0;				/*JLS 04-08-00*/
	nbOpersTot         += nbOpers;
      }
      else
      {
	tctx[i].active--;
	if (getThreadStatus (&(tctx[i]), &status) < 0)		/*JLS 17-11-00*/
	{							/*JLS 17-11-00*/
	  printf ("ldclt[%d]: T%03d: Cannot get status\n", mctx.pid, tctx[i].thrdNum);
	  status = RUNNING;	/* Be conservative */		/*JLS 17-11-00*/
	}							/*JLS 17-11-00*/
	if ((!(tctx[i].active)) && (status != DEAD))		/*JLS 17-11-00*/
	{
	  tctx[i].nbInactRow++;					/*JLS 04-08-00*/
	  tctx[i].nbInactTot++;					/*JLS 04-08-00*/
	  if (!(mctx.mode & SUPER_QUIET))
	  {
	    printf ("ldclt[%d]: T%03d: No activity for %d seconds"
			" -- %5d in row, total %5d\n",		/*JLS 04-08-00*/
			mctx.pid, tctx[i].thrdNum, 
			(mctx.inactivMax + 1) * mctx.sampling,
			tctx[i].nbInactRow, tctx[i].nbInactTot);/*JLS 08-08-00*/
	    fflush (stdout);
	  }
	  tctx[i].active = mctx.inactivMax + 1;
	  mctx.nbNoActivity++;
	}
      }

      if (status != DEAD)					/*JLS 17-11-00*/
	allDead = 0;
    } /* For each thread */

    /*
     * Summary of operations
     */
    printf ("ldclt[%d]: Average rate: %7.2f/thr  (%7.2f/sec), total: %6d\n",
		mctx.pid, (float)nbOpersTot/(float)mctx.nbThreads,
		(float)nbOpersTot/(float)mctx.sampling, nbOpersTot);
    fflush (stdout);

    /*
     * Gather global statistics
     */
    mctx.totNbOpers += nbOpersTot;
    mctx.totNbSamples++;

    /*
     * Maybe the number of samples is achieved ?
     */
    mctx.nbSamples--;
    if (mctx.nbSamples == 0)
    {
      if (shutdownThreads() < 0)				/*JLS 17-11-00*/
	printf ("ldclt[%d]: Problem while shutting down threads,\n", mctx.pid);
      allDead = 1;
      printf ("ldclt[%d]: Number of samples achieved. Bye-bye...\n", mctx.pid);
    }

    /*
     * Maybe global statistics to print ?
     * Keep this at the end of the loop !!!
     */
    if ((!allDead) && (!(--mctx.globStatsCnt)))			/*JLS 18-08-00*/
    {								/*JLS 08-08-00*/
      if (printGlobalStatistics() < 0)				/*JLS 08-08-00*/
      {								/*JLS 08-08-00*/
	printf ("ldclt[%d]: Cannot print global statistics...\n", mctx.pid);
	printf ("ldclt[%d]: Bye-bye...", mctx.pid);
	ldcltExit (EXIT_OTHER);					/*JLS 25-08-00*/
      }								/*JLS 08-08-00*/
      mctx.globStatsCnt = DEF_GLOBAL_NB;			/*JLS 08-08-00*/
    }								/*JLS 08-08-00*/
  } /* while (!allDead) */

#ifdef SOLARIS							/*JLS 17-11-00*/
  /*
   * Well, all the productors are dead.
   * Let's wait for the consumers (aka ckeck threads)
   */
  allDead = 0;
  if (mctx.slavesNb > 0)
    while (!allDead)
    {
      allDead=1;
      for (i=0 ; i<mctx.slavesNb ; i++)
	if (cctx[i].status != DEAD)
	  allDead = 0;
      if (!allDead)
	ldclt_sleep (1);
    }
#endif								/*JLS 17-11-00*/

  /*
   * Check the exit status of the threads
   */
  for (i=0 ; i<mctx.nbThreads ; i++)				/*JLS 08-08-00*/
    if (tctx[i].exitStatus > mctx.exitStatus)			/*JLS 08-08-00*/
      mctx.exitStatus = tctx[i].exitStatus;			/*JLS 08-08-00*/

  /*
   * If there, all threads are dead
   */
  printf ("ldclt[%d]: All threads are dead - exit.\n", mctx.pid);
  fflush (stdout);
  return (0);
}










/* ****************************************************************************
	FUNCTION :	printGlobalStatistics
	PURPOSE :	This function will print the global statistics numbers.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
printGlobalStatistics (void)
{
  int	 i;		/* For the loops */
  char	 buf[256];	/* To build the error strings */
  int	 found;		/* Something was found */
  int	 total;		/* Total statistics */

  /*
   * Pending operations statistics
   */
  if (mctx.mode & ASYNC)
  {
    total = 0;
    for (i=0 ; i<mctx.nbThreads ; i++)
    {
      printf ("ldclt[%d]: T%03d: pendingNb=%d\n", mctx.pid, tctx[i].thrdNum, tctx[i].pendingNb);
      total += tctx[i].pendingNb;
    }
    printf ("ldclt[%d]: Global total pending operations: %d\n", mctx.pid, total);
  }

  /*
   * Operations statistics
   */
  printf ("ldclt[%d]: Global average rate: %7.2f/thr  (%6.2f/sec), total: %6d\n",
		mctx.pid, (float)mctx.totNbOpers/(float)mctx.nbThreads,
		(float)mctx.totNbOpers/(float)(mctx.sampling*mctx.totNbSamples),
		mctx.totNbOpers);

  /*
   * No activity reports.
   */
  if (mctx.nbNoActivity == 0)
    printf ("ldclt[%d]: Global number times \"no activity\" reports: never\n", mctx.pid);
  else
    printf ("ldclt[%d]: Global number times \"no activity\" reports: %d\n", 
		mctx.pid, mctx.nbNoActivity);

  /*
   * Dead threads statistics
   */
  total = 0;
  for (i=0 ; i<mctx.nbThreads ; i++)
    if (tctx[i].status == DEAD)
      total++;
  if (total != 0)
    printf ("ldclt[%d]: Global number of dead threads: %d\n", mctx.pid, total);

  /*
   * Errors statistics
   * No mutex because this function is called at exit
   * Note:	Maybe implement a way to stop the running threads ?
   */
  found = 0;
  for (i=0 ; i<MAX_ERROR_NB ; i++)
    if (mctx.errors[i] > 0)
    {
      found = 1;
      sprintf (buf, "(%s)", my_ldap_err2string (i));
      printf ("ldclt[%d]: Global error %2d %s occurs %5d times\n",
          mctx.pid, i, buf, mctx.errors[i]);
    }
  if (mctx.errorsBad > 0)
  {
    found = 1;
    printf ("ldclt[%d]: Global illegal errors (codes not in [0, %d]) occurs %5d times\n",
	mctx.pid, MAX_ERROR_NB-1, mctx.errorsBad);
  }
  if (!found)
    printf ("ldclt[%d]: Global no error occurs during this session.\n", mctx.pid);

  /*
   * Check threads statistics
   */
  if (mctx.slavesNb > 0)
  {
    if (!(mctx.slaveConn))
      printf ("ldclt[%d]: Problem: slave never connected !!!!\n", mctx.pid);
    else
    {
      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbOpRecv;
      printf ("ldclt[%d]: Global number of replication operations received: %5d\n",
          mctx.pid, total);

      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbEarly;
      printf ("ldclt[%d]: Global number of early replication:               %5d\n",
          mctx.pid, total);

      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbLate;
      printf ("ldclt[%d]: Global number of late replication:                %5d\n",
          mctx.pid, total);

      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbLostOp;
      printf ("ldclt[%d]: Global number of lost operation:                  %5d\n",
          mctx.pid, total);

      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbNotOnList;
      printf ("ldclt[%d]: Global number of not on list replication op.:     %5d\n",
          mctx.pid, total);

      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbRepFail32;
      printf ("ldclt[%d]: Global number of repl failed LDAP_NO_SUCH_OBJECT: %5d\n",
          mctx.pid, total);

      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbRepFail68;
      printf ("ldclt[%d]: Global number of repl failed LDAP_ALREADY_EXISTS: %5d\n",
          mctx.pid, total);

      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbRepFailX;
      printf ("ldclt[%d]: Global number of repl failed other error:         %5d\n",
          mctx.pid, total);

      total = 0;
      for (i=0 ; i<mctx.slavesNb ; i++)
	total += cctx[i].nbStillOnQ;
      printf ("ldclt[%d]: Global number of repl still on Queue:             %5d\n",
          mctx.pid, total);
    }
  }

  /*
   * Normal end
   */
  fflush (stdout);
  return (0);
}







#ifndef _WIN32							/*JLS 29-11-00*/
/* ****************************************************************************
	FUNCTION :	trapVector
	PURPOSE :	Interruption vector for SIGINT and SIGQUIT
	INPUT :		sig	= the signal
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :	Issue statistics report. If SIGINT, exit the tool.
			The other signal (SIGQUIT) will only issue statistics.
 *****************************************************************************/
void 
trapVector (
	int		 sig,
	siginfo_t	*siginfo,
	void		*truc)
{
  printf ("\n"); /* Jump over the ^C or ^\ */
  (void) printGlobalStatistics();
  if (sig == SIGINT)
  {
    printf ("Catch SIGINT - exit...\n");
    fflush (stdout);
    ldcltExit (mctx.exitStatus);				/*JLS 25-08-00*/
  }
  return;
}
#endif	/* _WIN32 */						/*JLS 29-11-00*/





/* ****************************************************************************
	FUNCTION :	initMainThread
	PURPOSE :	Initiates the main thread
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
initMainThread (void)
{
#ifndef _WIN32							/*JLS 29-11-00*/
  struct sigaction	 act;

  /*
   * Trap SIGINT.
   */
#ifdef LDCLT_CAST_SIGACTION					/*JLS 01-12-00*/
  act.sa_handler   = (void(*)(int)) trapVector;			/*JLS 14-11-00*/
#else								/*JLS 14-11-00*/
  act.sa_handler   = trapVector;
#endif								/*JLS 14-11-00*/
  act.sa_sigaction = trapVector;
  act.sa_flags     = SA_NODEFER;
  sigemptyset (&(act.sa_mask));
  sigaddset (&(act.sa_mask), SIGINT);
  sigfillset (&(act.sa_mask));
  if (sigaction (SIGINT, &act, NULL) < 0)
  {
    perror ("ldclt");
    fprintf (stderr, "ldclt[%d]: Error: cannot sigaction(SIGINT)\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }

  /*
   * Trap SIGQUIT.
   */
#ifdef LDCLT_CAST_SIGACTION					/*JLS 01-12-00*/
  act.sa_handler   = (void(*)(int)) trapVector;			/*JLS 14-11-00*/
#else								/*JLS 14-11-00*/
  act.sa_handler   = trapVector;
#endif								/*JLS 14-11-00*/
  act.sa_sigaction = trapVector;
  act.sa_flags     = SA_NODEFER;
  sigemptyset (&(act.sa_mask));
  sigaddset (&(act.sa_mask), SIGQUIT);
  sigfillset (&(act.sa_mask));
  if (sigaction (SIGQUIT, &act, NULL) < 0)
  {
    perror ("ldclt");
    fprintf (stderr, "ldclt[%d]: Error: cannot sigaction(SIGQUIT)\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }
#endif	/* _WIN32 */						/*JLS 29-11-00*/

  return (0);
}








					/* New function */	/*JLS 21-11-00*/
/* ****************************************************************************
	FUNCTION :	parseFilter
	PURPOSE :	This function parse a string in the form abcXXXdef
			and returns the head, tail and number of digits for
			the XXX part.
	INPUT :		src	= source string
	OUTPUT :	head	= head ==> abc
			tail	= tail ==> def
			ndigits	= number of digits
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
parseFilter (
	char	 *src,
	char	**head,
	char	**tail,
	int	 *ndigits)
{
  int	 i, j;

  for (i=0 ; (i<strlen(src)) && (src[i]!='X') ; i++);
  *head = (char *)malloc(i+1);
  if (*head == NULL)
  {
    printf ("Error: cannot malloc(*head), error=%d (%s)\n",
		errno, strerror (errno));
    return (-1);
  }
  strncpy (*head, src, i);
  (*head)[i] = '\0';

  for (j=i ; (i<strlen(src)) && (src[j]=='X') ; j++);
  *tail = (char *)malloc(strlen(src)-j+1);
  if (*tail == NULL)
  {
    printf ("Error: cannot malloc(*tail), error=%d (%s)\n",
		errno, strerror (errno));
    return (-1);
  }
  strcpy (*tail, &(src[j]));

  *ndigits = j-i;

  return (0);
}













/* ****************************************************************************
	FUNCTION :	basicInit
	PURPOSE :	This function performs the basic initializations of
			this tool, as soon as all the options are decoded.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
basicInit (void)
{
#ifndef _WIN32							/*JLS 29-11-00*/
  struct rlimit	 rlp;	/* For setrlimit() */
#endif	/* _WIN32 */						/*JLS 29-11-00*/
  int		 i;	/* For the loops */			/*JLS 21-11-00*/
  int		 ret;	/* Return value */
  int		 oflags;/* open() flags */			/*JLS 05-04-01*/

  /*
   * Misc inits
   */
  mctx.timeval.tv_sec      = mctx.timeout;
  mctx.timeval.tv_usec     = 0;
  mctx.timevalZero.tv_sec  = 0;
  mctx.timevalZero.tv_usec = 0;

  /*
   * Initiate the utilities
   */
  if (utilsInit() < 0)						/*JLS 14-11-00*/
  {								/*JLS 14-11-00*/
    fprintf (stderr, "Cannot initialize utilities.\n");		/*JLS 14-11-00*/
    return (-1);						/*JLS 14-11-00*/
  }								/*JLS 14-11-00*/

  /*
   * Maybe random data to be read from file ?
   */
  if (mctx.mod2 & M2_RNDBINDFILE)				/*JLS 03-05-01*/
  {								/*JLS 03-05-01*/
    mctx.rndBindDlf = dataListFile (mctx.rndBindFname);		/*JLS 03-05-01*/
    if (mctx.rndBindDlf == NULL)				/*JLS 03-05-01*/
    {								/*JLS 03-05-01*/
      fprintf (stderr, "Error : cannot read %s\n", 		/*JLS 03-05-01*/
					mctx.rndBindFname);	/*JLS 03-05-01*/
      fflush (stderr);						/*JLS 03-05-01*/
      return (-1);						/*JLS 03-05-01*/
    }								/*JLS 03-05-01*/
  }								/*JLS 03-05-01*/

  /*
   * Genldif output file ?
   * This file should not already exist.
   */
  if (mctx.mod2 & M2_GENLDIF)					/*JLS 19-03-01*/
  {								/*JLS 19-03-01*/
    if (!(mctx.mod2 & M2_APPEND))				/*JLS 11-04-01*/
    {								/*JLS 11-04-01*/
      mctx.genldifFile = open (mctx.genldifName, O_RDONLY);	/*JLS 02-04-01*/
      if (mctx.genldifFile != -1)				/*JLS 02-04-01*/
      {								/*JLS 19-03-01*/
	fprintf (stderr,"Error: File exits %s\n", 		/*JLS 19-03-01*/
					mctx.genldifName);	/*JLS 19-03-01*/
	fflush (stderr);					/*JLS 19-03-01*/
	return (-1);						/*JLS 19-03-01*/
      }								/*JLS 19-03-01*/
    }								/*JLS 11-04-01*/
    if (mctx.mod2 & M2_APPEND)					/*JLS 05-04-01*/
      oflags = O_APPEND|O_WRONLY|O_CREAT;			/*JLS 05-04-01*/
    else							/*JLS 05-04-01*/
      oflags = O_EXCL|O_WRONLY|O_CREAT;				/*JLS 05-04-01*/
#if !defined(_WIN32) && !defined(OSF1) && !defined(__LP64__) && !defined(_LP64)				/*JLS 05-04-01*/
    oflags |= O_LARGEFILE;					/*JLS 05-04-01*/
#endif								/*JLS 03-04-01*/
    mctx.genldifFile = open (mctx.genldifName, oflags, 0666);	/*JLS 05-04-01*/
    if (mctx.genldifFile == -1)					/*JLS 02-04-01*/
    {								/*JLS 19-03-01*/
      fprintf (stderr, "ldclt: %s\n", strerror (errno));	/*JLS 19-03-01*/
      fprintf (stderr,"Error: cannot create %s\n", 		/*JLS 19-03-01*/
					mctx.genldifName);	/*JLS 19-03-01*/
      fflush (stderr);						/*JLS 19-03-01*/
      return (-1);						/*JLS 19-03-01*/
    }								/*JLS 19-03-01*/
    mctx.nbThreads = 1;						/*JLS 19-03-01*/
  }								/*JLS 19-03-01*/

  /*
   * Maybe common counter ?
   */
  if ((mctx.mode & COMMON_COUNTER) &&(!(mctx.mod2 & M2_OBJECT)))/*JLS 28-03-01*/
  {								/*JLS 14-03-01*/
    if ((ret = ldclt_mutex_init(&(mctx.lastVal_mutex))) != 0)	/*JLS 14-03-01*/
    {								/*JLS 14-03-01*/
      fprintf (stderr, "ldclt: %s\n", strerror (ret));		/*JLS 14-03-01*/
      fprintf (stderr,"Error: cannot initiate lastVal_mutex\n");/*JLS 14-03-01*/
      fflush (stderr);						/*JLS 14-03-01*/
      return (-1);						/*JLS 14-03-01*/
    }								/*JLS 14-03-01*/
    mctx.lastVal = mctx.randomLow-1;				/*JLS 14-03-01*/
  }								/*JLS 14-03-01*/

  /*
   * Set appropriate number of files...
   */
#ifndef _WIN32							/*JLS 29-11-00*/
  if (mctx.nbThreads > 54)
  {
    if (getrlimit (RLIMIT_NOFILE, &rlp) < 0)
    {
      perror ("ldclt");
      fprintf (stderr, "Error: cannot getrlimit()\n");
      fflush (stderr);
      return (-1);
    }
    rlp.rlim_cur = rlp.rlim_max;
    if (setrlimit (RLIMIT_NOFILE, &rlp) < 0)
    {
      perror ("ldclt");
      fprintf (stderr, "Error: cannot setrlimit()\n");
      fflush (stderr);
      return (-1);
    }
    if (mctx.mode & VERBOSE)
      printf ("Set file number to %u\n", (unsigned int)rlp.rlim_max);
  }
#endif	/* _WIN32 */						/*JLS 29-11-00*/

  /*
   * Maybe an object to read ?
   */
  if (mctx.mod2 & M2_OBJECT)					/*JLS 19-03-01*/
    if (readObject (&(mctx.object)) < 0)			/*JLS 19-03-01*/
    {								/*JLS 19-03-01*/
      printf ("Error: cannot parse %s\n", mctx.object.fname);	/*JLS 19-03-01*/
      return (-1);						/*JLS 19-03-01*/
    }								/*JLS 19-03-01*/

  /*
   * Maybe random filter to prepare ?
   */
  if ((mctx.mode & (RANDOM | INCREMENTAL)) && 
      (!(mctx.mod2 & M2_RDN_VALUE)))				/*JLS 23-03-01*/
  {
    if (parseFilter (mctx.filter, &(mctx.randomHead),		/*JLS 21-11-00*/
		&(mctx.randomTail), &(mctx.randomNbDigit)) < 0)	/*JLS 21-11-00*/
    {								/*JLS 21-11-00*/
      printf ("Error: cannot parse filter...\n");		/*JLS 21-11-00*/
      return (-1);						/*JLS 21-11-00*/
    }								/*JLS 21-11-00*/
  }

  /*
   * Maybe random base DN to prepare ?
   */
  if (mctx.mode & RANDOM_BASE)
  {
    if (parseFilter (mctx.baseDN, &(mctx.baseDNHead),		/*JLS 21-11-00*/
		&(mctx.baseDNTail), &(mctx.baseDNNbDigit)) < 0)	/*JLS 21-11-00*/
    {								/*JLS 21-11-00*/
      printf ("Error: cannot parse base DN...\n");		/*JLS 21-11-00*/
      return (-1);						/*JLS 21-11-00*/
    }								/*JLS 21-11-00*/
  }

  /*
   * Maybe random bind DN to prepare ?
   */
  if (mctx.mode & RANDOM_BINDDN)				/*JLS 05-01-01*/
  {								/*JLS 05-01-01*/
    if (parseFilter (mctx.bindDN, &(mctx.bindDNHead),		/*JLS 05-01-01*/
		&(mctx.bindDNTail), &(mctx.bindDNNbDigit)) < 0)	/*JLS 05-01-01*/
    {								/*JLS 05-01-01*/
      printf ("Error: cannot parse bind DN...\n");		/*JLS 05-01-01*/
      return (-1);						/*JLS 05-01-01*/
    }								/*JLS 05-01-01*/
    if (parseFilter (mctx.passwd, &(mctx.passwdHead),		/*JLS 05-01-01*/
		&(mctx.passwdTail), &(mctx.passwdNbDigit)) < 0)	/*JLS 05-01-01*/
    {								/*JLS 05-01-01*/
      printf ("Error: cannot parse password...\n");		/*JLS 05-01-01*/
      return (-1);						/*JLS 05-01-01*/
    }								/*JLS 05-01-01*/
  }								/*JLS 05-01-01*/

  /*
   * Maybe random authid to prepare ?
   */
  if (mctx.mod2 & M2_RANDOM_SASLAUTHID)
  {
    if (parseFilter (mctx.sasl_authid, &(mctx.sasl_authid_head),
        &(mctx.sasl_authid_tail), &(mctx.sasl_authid_nbdigit)) < 0)
    {
      printf ("Error: cannot parse bind DN...\n");
      return (-1);
    }
  }

  /*
   * Maybe an attribute replacement to prepare ?
   */
  if (mctx.mode & ATTR_REPLACE)					/*JLS 21-11-00*/
  {								/*JLS 21-11-00*/
    /*
     * Find the attribute name
     */
    for (i=0 ; (i<strlen(mctx.attrpl)) && 			/*JLS 21-11-00*/
			(mctx.attrpl[i]!=':') ; i++);		/*JLS 21-11-00*/
    mctx.attrplName = (char *)malloc(i+1);			/*JLS 21-11-00*/
    strncpy (mctx.attrplName, mctx.attrpl, i);			/*JLS 21-11-00*/
    mctx.attrplName[i] = '\0';					/*JLS 21-11-00*/

    /*
     * Parse the attribute value
     */
    if (parseFilter (mctx.attrpl+i+1, &(mctx.attrplHead),	/*JLS 21-11-00*/
		&(mctx.attrplTail), &(mctx.attrplNbDigit)) < 0)	/*JLS 21-11-00*/
    {								/*JLS 21-11-00*/
      printf ("Error: cannot parse attreplace...\n");		/*JLS 21-11-00*/
      return (-1);						/*JLS 21-11-00*/
    }								/*JLS 21-11-00*/
  }								/*JLS 21-11-00*/


  /*
   * Initiates statistics fields
   */
  mctx.totNbOpers   = 0;
  mctx.totNbSamples = 0;
  mctx.errorsBad    = 0;
  for (i=0 ; i<MAX_ERROR_NB ; i++)
    mctx.errors[i] = 0;

  /*
   * Initiate the mutex that protect the errors statistics
   */
  if ((ret = ldclt_mutex_init(&(mctx.errors_mutex))) != 0)
  {
    fprintf (stderr, "ldclt: %s\n", strerror (ret));
    fprintf (stderr, "Error: cannot initiate errors_mutex\n");
    fflush (stderr);
    return (-1);
  }

  /*
   * Load the images
   */
  if (mctx.mode & (OC_EMAILPERSON|OC_INETORGPRSON))		/*JLS 07-11-00*/
    if (loadImages (mctx.imagesDir) < 0)			/*JLS 16-11-00*/
      return (-1);

  /*
   * Maybe we should initiate the operation list mutex and other check-related
   * thing...
   */
  if (mctx.slavesNb > 0)
  {
    /*
     * Initiates the mutex
     */
    if ((ret = ldclt_mutex_init(&(mctx.opListTail_mutex))) != 0)
    {
      fprintf (stderr, "ldclt: %s\n", strerror (ret));
      fprintf (stderr, "Error: cannot initiate opListTail_mutex\n");
      fflush (stderr);
      return (-1);
    }

    /*
     * Initiates the first operation with empty dummy values
     * We need to initiate this entry to dummy values because some opXyz()
     * functions will access this entry careless.
     */
    mctx.opListTail = (oper *) malloc (sizeof (oper));
    if (mctx.opListTail == NULL)				/*JLS 06-03-00*/
    {								/*JLS 06-03-00*/
      printf ("Error: cannot malloc(mctx.opListTail), error=%d (%s)\n",
		errno, strerror (errno));			/*JLS 06-03-00*/
      return (-1);						/*JLS 06-03-00*/
    }								/*JLS 06-03-00*/
    mctx.opListTail->dn              = strdup("Dummy initial dn");
    if (mctx.opListTail->dn == NULL)				/*JLS 06-03-00*/
    {								/*JLS 06-03-00*/
      printf ("Error: cannot strdup(mctx.opListTail->dn), error=%d (%s)\n",
		errno, strerror (errno));			/*JLS 06-03-00*/
      return (-1);						/*JLS 06-03-00*/
    }								/*JLS 06-03-00*/
    mctx.opListTail->attribs[0].type = NULL;
    mctx.opListTail->newRdn          = NULL;
    mctx.opListTail->newParent       = NULL;
    mctx.opListTail->skipped         = 0;
    mctx.opListTail->next            = NULL;
    if ((ret=ldclt_mutex_init(&(mctx.opListTail->skipped_mutex))) != 0)
    {
      fprintf (stderr, "ldclt: %s\n", strerror (ret));
      fprintf (stderr, "Error: cannot initiate opListTail->skipped_mutex\n");
      fflush (stderr);
      return (-1);
    }
  }

  /*
   * SSL is enabled ?
   */
  if (mctx.mode & SSL)
  {
    /*
     * The initialization of certificate based and basic authentication differs
     * B Kolics 23-11-00
     */
    if (mctx.mode & CLTAUTH)
    {
      if (ldapssl_clientauth_init(mctx.certfile, NULL, 1, mctx.keydbfile, NULL) < 0)
      {
	fprintf (stderr, "ldclt: %s\n", strerror (errno));
	fprintf (stderr, "Cannot ldapssl_clientauth_init (%s,%s)\n",
		 mctx.certfile, mctx.keydbfile);
	fflush (stderr);
	return (-1);
      }
    } else {
      if (ldapssl_client_init(mctx.certfile, NULL) < 0)
      {
	fprintf (stderr, "ldclt: %s\n", strerror (errno));
	fprintf (stderr, "Cannot ldapssl_client_init (%s)\n",	/*JLS 08-11-00*/
		 mctx.certfile);				/*JLS 08-11-00*/
	fflush (stderr);
	return (-1);
      }
    }
  }

  /*
   * Specific scenarios initialization...
   */
  if (mctx.mode & SCALAB01)					/*JLS 08-01-01*/
    if (scalab01_init() < 0)					/*JLS 08-01-01*/
    {								/*JLS 08-01-01*/
      fprintf (stderr, "ldclt: cannot init scalab01\n");	/*JLS 08-01-01*/
      fflush (stderr);						/*JLS 08-01-01*/
      return (-1);						/*JLS 08-01-01*/
    }								/*JLS 08-01-01*/

  /*
   * Normal end
   */
  return (0);
}








/* ****************************************************************************
	FUNCTION :	printModeValues
	PURPOSE :	This function is targetted to print the bits mask of
			the mode field.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	None.
	DESCRIPTION :
 *****************************************************************************/
void 
dumpModeValues (void)
{
  if (mctx.mode & QUIET)
    printf (" quiet");
  if (mctx.mode & SUPER_QUIET)
    printf (" super_quiet");
  if (mctx.mode & VERBOSE)
    printf (" verbose");
  if (mctx.mode & VERY_VERBOSE)
    printf (" very_verbose");
  if (mctx.mode & COUNT_EACH)					/*JLS 15-12-00*/
    printf (" counteach");					/*JLS 15-12-00*/
  if (mctx.mode & LDAP_V2)
    printf (" v2");
  if (mctx.mode & ASYNC)
    printf (" asynchronous");
  if (mctx.mode & INCREMENTAL)
    printf (" incremental");
  if (mctx.mode & COMMON_COUNTER)				/*JLS 14-03-01*/
    printf (" commoncounter");					/*JLS 14-03-01*/
  if (mctx.mode & NOLOOP)
    printf (" noloop");
  if (mctx.mode & RANDOM)
    printf (" random");
  if (mctx.mode & RANDOM_BASE)
    printf (" randombase");
  if (mctx.mode & RANDOM_BINDDN)				/*JLS 05-01-01*/
    printf (" randombinddn");					/*JLS 05-01-01*/
  if (mctx.mode & STRING)
    printf (" string");
  if (mctx.mode & ASCII_7BITS)
    printf (" ascii");
  if (mctx.mode & CLOSE_FD)
    printf (" close_fd");
  if (mctx.mode & BIND_EACH_OPER)
    printf (" bind_each_operation");
  if (mctx.mode & SSL)
    printf (" ssl");
  if (mctx.mode & CLTAUTH)
    printf (" ssl_with_client_authentication");                 /* BK 23-11-00*/
  if (mctx.mod2 & M2_SASLAUTH)
    printf (" saslauth");
  if (mctx.mod2 & M2_RANDOM_SASLAUTHID)
    printf (" randomauthid");
  if (mctx.mode & SMOOTHSHUTDOWN)				/*JLS 17-11-00*/
    printf (" smoothshutdown");					/*JLS 17-11-00*/
  if (mctx.mode & DONT_SLEEP_DOWN)				/*JLS 14-03-01*/
    printf (" dontsleeponserverdown");				/*JLS 14-03-01*/

  if (mctx.mode & ADD_ENTRIES)
    printf (" add");
  if (mctx.mode & ATTR_REPLACE)					/*JLS 21-11-00*/
    printf (" attrib_replace");					/*JLS 21-11-00*/
  if (mctx.mod2 & M2_BINDONLY)					/*JLS 04-05-01*/
    printf (" bindonly");					/*JLS 04-05-01*/
  if (mctx.mode & DELETE_ENTRIES)
    printf (" delete");
  if (mctx.mode & EXACT_SEARCH)
    printf (" exact_search");
  if (mctx.mode & RANDOM_ATTRLIST)				/*JLS 15-03-01*/
    printf (" randomattrlist");					/*JLS 15-03-01*/
  if (mctx.mode & RENAME_ENTRIES)
    printf (" rename");
  if (mctx.mode & WITH_NEWPARENT)				/*JLS 15-12-00*/
    printf (" withnewparent");					/*JLS 15-12-00*/

  if (mctx.mod2 & M2_GENLDIF)					/*JLS 08-01-01*/
    printf (" genldif=%s", mctx.genldifName);			/*JLS 05-04-01*/
  if (mctx.mode & SCALAB01)					/*JLS 08-01-01*/
    printf (" scalab01");					/*JLS 08-01-01*/

  if (mctx.mode & OC_EMAILPERSON)
    printf (" class=emailPerson");
  if (mctx.mode & OC_PERSON)
    printf (" class=person");
  if (mctx.mode & OC_INETORGPRSON)				/*JLS 07-11-00*/
    printf (" class=inetOrgPerson");				/*JLS 07-11-00*/
  if (mctx.mod2 & M2_OBJECT)					/*JLS 19-03-01*/
    printf (" object=%s", mctx.object.fname);			/*JLS 19-03-01*/
  if (mctx.mod2 & M2_RNDBINDFILE)				/*JLS 04-05-01*/
    printf (" randombinddnfromfile=%s", mctx.rndBindFname);	/*JLS 04-05-01*/
  return;
}








/*
 * Scope parameters (-s sub-options)
 */
char *scopeParams[] = {
#define SP_ONE		0
	"one",
#define SP_SUBTREE	1
	"subtree",
#define SP_BASE		2
	"base",
	NULL
};

/* ****************************************************************************
	FUNCTION :	decodeScopeParams
	PURPOSE :	Decode the scope parameters (ak asub-options of the 
			option -s).
	INPUT :		optarg	= argument to decode
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
decodeScopeParams (
	char	*optarg)
{
  char	*suboptions;	/* To parse optarg */
  char	*subvalue;	/* Current sub-option */

  suboptions = optarg;
  while (*suboptions != '\0')
  {
    switch (getsubopt (&suboptions, scopeParams, &subvalue))
    {
      case SP_BASE:
	mctx.scope = LDAP_SCOPE_BASE;
	break;
      case SP_ONE:
	mctx.scope = LDAP_SCOPE_ONELEVEL;
	break;
      case SP_SUBTREE:
	mctx.scope = LDAP_SCOPE_SUBTREE;
	break;
      default:
	fprintf (stderr, "Error: illegal option -s %s\n", subvalue);
	return (-1);
	break;
    }
  }
  return (0);
}






/* ****************************************************************************
	FUNCTION :	saslSetParam
	PURPOSE :	Sets SASL parameters
	INPUT :		saslarg	= value to decode
	OUTPUT :	None.
	RETURN :	-1 if error, 0 otherwise.
	DESCRIPTION :	Copied from Mozilla LDAP C SDK (common.c)
 *****************************************************************************/
int
saslSetParam (
	char	*saslarg)
{
  char *attr = NULL;
  int argnamelen;

  if (saslarg == NULL) {
    fprintf (stderr, "Error: missing SASL argument\n");
    return (-1);
  }

  attr = strchr(saslarg, '=');
  if (attr == NULL) {
     fprintf( stderr, "Didn't find \"=\" character in %s\n", saslarg);
     return (-1);
  }

  argnamelen = attr - saslarg;
  attr++;

  if (!strncasecmp(saslarg, "secProp", argnamelen)) {
    if ( mctx.sasl_secprops != NULL ) {
      fprintf( stderr, "secProp previously specified\n");
      return (-1);
    }
    if (( mctx.sasl_secprops = strdup(attr)) == NULL ) {
      perror ("malloc");
      exit (LDAP_NO_MEMORY);
    }
  } else if (!strncasecmp(saslarg, "realm", argnamelen)) {
    if ( mctx.sasl_realm != NULL ) {
      fprintf( stderr, "Realm previously specified\n");
      return (-1);
    }
    if (( mctx.sasl_realm = strdup(attr)) == NULL ) {
      perror ("malloc");
      exit (LDAP_NO_MEMORY);
    }
  } else if (!strncasecmp(saslarg, "authzid", argnamelen)) {
    if (mctx.sasl_username != NULL) {
      fprintf( stderr, "Authorization name previously specified\n");
      return (-1);
    }
    if (( mctx.sasl_username = strdup(attr)) == NULL ) {
      perror ("malloc");
      exit (LDAP_NO_MEMORY);
    }
  } else if (!strncasecmp(saslarg, "authid", argnamelen)) {
    if ( mctx.sasl_authid != NULL ) {
      fprintf( stderr, "Authentication name previously specified\n");
      return (-1);
    }
    if (( mctx.sasl_authid = strdup(attr)) == NULL) {
      perror ("malloc");
      exit (LDAP_NO_MEMORY);
    }
  } else if (!strncasecmp(saslarg, "mech", argnamelen)) {
    if ( mctx.sasl_mech != NULL ) {
      fprintf( stderr, "Mech previously specified\n");
      return (-1);
    }
    if (( mctx.sasl_mech = strdup(attr)) == NULL) {
      perror ("malloc");
      exit (LDAP_NO_MEMORY);
    }
  } else if (!strncasecmp(saslarg, "flags", argnamelen)) {
    int len = strlen(attr);
    if (len && !strncasecmp(attr, "automatic", len)) {
      mctx.sasl_flags = LDAP_SASL_AUTOMATIC;
    } else if (len && !strncasecmp(attr, "interactive", len)) {
      mctx.sasl_flags = LDAP_SASL_INTERACTIVE;
    } else if (len && !strncasecmp(attr, "quiet", len)) {
      mctx.sasl_flags = LDAP_SASL_QUIET;
    } else {
      fprintf(stderr, "Invalid SASL flags value [%s]: must be one of "
              "automatic, interactive, or quiet\n", attr);
      return (-1);
    }
  } else {
    fprintf (stderr, "Invalid SASL attribute name %s\n", saslarg);
    return (-1);
  }
  return 0;
}





					/* New function */	/*JLS 08-03-01*/
/* ****************************************************************************
	FUNCTION :	decodeReferralParams
	PURPOSE :	Decode -e referral params.
	INPUT :		val	= value to decode
	OUTPUT :	None.
	RETURN :	-1 if error, decoded value otherwise.
	DESCRIPTION :
 *****************************************************************************/
int
decodeReferralParams (
	char	*val)
{
  if (val == NULL)
  {
    fprintf (stderr, "Error: missing arg referral\n");
    return (-1);
  }
  if (!strcmp (val, "on"))
    return (REFERRAL_ON);
  if (!strcmp (val, "off"))
    return (REFERRAL_OFF);
  if (!strcmp (val, "rebind"))
    return (REFERRAL_REBIND);

  fprintf (stderr, "Error: illegal arg %s for referral\n", val);
  return (-1);
}











/* ****************************************************************************
	FUNCTION :	addAttrToList
	PURPOSE :	Add attributes in the attribute list.
	INPUT :		list	= list to process.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
addAttrToList (
	char	*list)
{
  int	 start;	/* Start of the attr name */
  int	 end;	/* End of the attr name */

  /*
   * Sanity check
   */
  if ((list == NULL) || (!(strlen(list))))
  {
    fprintf (stderr, "Error: missing attrlist\n");
    return (-1);
  }

  /*
   * The main loop.
   */
  start = 0;
  while (start < strlen (list))
  {
    /*
     * Maybe no more room in the structures ?
     */
    if ((mctx.attrlistNb+1) == MAX_ATTRIBS)
    {
      fprintf (stderr, "Error : too many attributes in attrlist\n");
      return (-1);
    }

    for (end=start ; (list[end]!='\0') && (list[end]!=':') ; end++);
    mctx.attrlist[mctx.attrlistNb] = (char *) malloc (1+end-start);
    strncpy (mctx.attrlist[mctx.attrlistNb], &(list[start]), end-start);
    mctx.attrlist[mctx.attrlistNb][end-start] = '\0';
    mctx.attrlistNb++;
    start = end+1;
  }

  return (0);
}







						/* New */	/*JLS 23-03-01*/
/* ****************************************************************************
	FUNCTION :	decodeRdnParam
	PURPOSE :	Decodes a -e rdn=value parameter.
	INPUT :		value	= value to decode.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
decodeRdnParam (
	char	*value)
{
  int	 i;	/* For the loops */

  /*
   * Note : ldclt does allow parameters overload. Ideally, we should
   *        free memory if a rdn has already been provided, but let's
   *        simply lost this data...
   *        Anyway, there is not a lot of memory used here...
   */
  mctx.object.rdn      = (vers_attribute *) malloc (sizeof (vers_attribute));
  mctx.object.rdn->buf = NULL;					/*JLS 28-03-01*/

  /*
   * Find this attribute's name
   */
  for (i=0 ; (value[i] != '\0') && (value[i] != ':') ; i++);
  if (value[i] == '\0')
  {
    fprintf (stderr, "Error: missing rdn attribute name\n");
    return (-1);
  }
  mctx.object.rdnName = (char *) malloc (i+1);
  strncpy (mctx.object.rdnName, value, i);
  mctx.object.rdnName[i] = '\0';

  /*
   * Decode the value
   */
  if (parseAttribValue ("-e rdn=", &(mctx.object), 
				value+i+1, mctx.object.rdn) < 0)
    return (-1);

  return (0);
}














/*
 * Exec params (-e sub-options)
 */
char *execParams[] = {
#define EP_EXACT_SEARCH		 	0
	"esearch",
#define EP_BIND_EACH_OPER	 	1
	"bindeach",
#define EP_RANDOM		 	2
	"random",
#define EP_CLOSE_FD			 3
	"close",
#define EP_INCREMENTAL		 	4
	"incr",
#define EP_ADD_ENTRIES		 	5
	"add",
#define EP_OC_PERSON		 	6
	"person",
#define EP_DELETE_ENTRIES	 	7
	"delete",
#define EP_OC_EMAILPERSON	 	8
	"emailPerson",
#define EP_STRING		 	9
	"string",
#define EP_RANDOM_BASE			10
	"randombase",
#define EP_LDAP_V2			11
	"v2",
#define EP_ASCII_7BITS			12
	"ascii",
#define EP_NOLOOP			13
	"noloop",
#define EP_RENAME			14
	"rename",
#define EP_OC_INETORGPRSON		15			/*JLS 07-11-00*/
	"inetOrgPerson",					/*JLS 07-11-00*/
#define EP_RANDOM_BASE_LOW		16			/*JLS 13-11-00*/
	"randombaselow",					/*JLS 13-11-00*/
#define EP_RANDOM_BASE_HIGH		17			/*JLS 13-11-00*/
	"randombasehigh",					/*JLS 13-11-00*/
#define EP_IMAGES_DIR			18			/*JLS 16-11-00*/
	"imagesdir",						/*JLS 16-11-00*/
#define EP_SMOOTH_SHUTDOWN		19			/*JLS 17-11-00*/
	"smoothshutdown",					/*JLS 17-11-00*/
#define EP_ATT_REPLACE			20			/*JLS 21-11-00*/
	"attreplace",						/*JLS 21-11-00*/
#define EP_CLTCERT_NAME			21			/* BK 23-11-00*/
	"cltcertname",						/* BK 23-11-00*/
#define EP_KEYDB_FILE			22			/* BK 23-11-00*/
	"keydbfile",						/* BK 23-11-00*/
#define EP_KEYDB_PIN			23			/* BK 23-11-00*/
	"keydbpin",						/* BK 23-11-00*/
#define EP_COUNT_EACH			24			/*JLS 15-12-00*/
	"counteach",						/*JLS 15-12-00*/
#define EP_WITH_NEWPARENT		25			/*JLS 15-12-00*/
	"withnewparent",					/*JLS 15-12-00*/
#define EP_NO_GLOBAL_STATS		26			/*JLS 19-12-00*/
	"noglobalstats",					/*JLS 19-12-00*/
#define EP_ATTRSONLY			27			/*JLS 03-01-01*/
	"attrsonly",						/*JLS 03-01-01*/
#define EP_RANDOMBINDDN			28			/*JLS 05-01-01*/
	"randombinddn",						/*JLS 05-01-01*/
#define EP_RANDOMBINDDNHIGH		29			/*JLS 05-01-01*/
	"randombinddnhigh",					/*JLS 05-01-01*/
#define EP_RANDOMBINDDNLOW		30			/*JLS 05-01-01*/
	"randombinddnlow",					/*JLS 05-01-01*/
#define EP_SCALAB01			31			/*JLS 08-01-01*/
	"scalab01",						/*JLS 08-01-01*/
#define EP_SCALAB01_CNXDURATION		32			/*JLS 12-01-01*/
	"scalab01_cnxduration",					/*JLS 12-01-01*/
#define EP_SCALAB01_WAIT		33			/*JLS 12-01-01*/
	"scalab01_wait",					/*JLS 12-01-01*/
#define EP_SCALAB01_MAXCNXNB		34			/*JLS 12-01-01*/
	"scalab01_maxcnxnb",					/*JLS 12-01-01*/
#define EP_REFERRAL			35			/*JLS 08-03-01*/
	"referral",						/*JLS 08-03-01*/
#define EP_COMMONCOUNTER		36			/*JLS 14-03-01*/
	"commoncounter",					/*JLS 14-03-01*/
#define EP_DONTSLEEPONSERVERDOWN	37			/*JLS 14-03-01*/
	"dontsleeponserverdown",				/*JLS 14-03-01*/
#define EP_ATTRLIST			38			/*JLS 15-03-01*/
	"attrlist",						/*JLS 15-03-01*/
#define EP_RANDOMATTRLIST		39			/*JLS 15-03-01*/
	"randomattrlist",					/*JLS 15-03-01*/
#define EP_OBJECT			40			/*JLS 19-03-01*/
	"object",						/*JLS 19-03-01*/
#define EP_GENLDIF			41			/*JLS 19-03-01*/
	"genldif",						/*JLS 19-03-01*/
#define EP_RDN				42			/*JLS 23-03-01*/
	"rdn",							/*JLS 23-03-01*/
#define EP_APPEND			43			/*JLS 05-04-01*/
	"append",						/*JLS 05-04-01*/
#define EP_RANDOMBINDDNFROMFILE		44			/*JLS 03-05-01*/
	"randombinddnfromfile",					/*JLS 03-05-01*/
#define EP_BINDONLY			45			/*JLS 04-05-01*/
	"bindonly",						/*JLS 04-05-01*/
#define EP_RANDOMSASLAUTHID		46
	"randomauthid",
#define EP_RANDOMSASLAUTHIDHIGH		47
	"randomauthidhigh",
#define EP_RANDOMSASLAUTHIDLOW		48
	"randomauthidlow",
	NULL
};

/* ****************************************************************************
	FUNCTION :	decodeExecParams
	PURPOSE :	Decode the execution parameters (aka sub-options of the
			option -e).
	INPUT :		optarg	= argument to decode.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
decodeExecParams (
	char	*optarg)
{
  char	*suboptions;	/* To parse optarg */
  char	*subvalue;	/* Current sub-option */

  suboptions = optarg;
  while (*suboptions != '\0')
  {
    switch (getsubopt (&suboptions, execParams, &subvalue))
    {
      case EP_ADD_ENTRIES:
	mctx.mode |= ADD_ENTRIES;
	break;
      case EP_APPEND:						/*JLS 05-04-01*/
	mctx.mod2 |= M2_APPEND;					/*JLS 05-04-01*/
	break;							/*JLS 05-04-01*/
      case EP_ASCII_7BITS:
	mctx.mode |= ASCII_7BITS;
	break;
      case EP_ATT_REPLACE:					/*JLS 21-11-00*/
	mctx.mode |= ATTR_REPLACE;				/*JLS 21-11-00*/
	if (subvalue == NULL)					/*JLS 21-11-00*/
	{							/*JLS 21-11-00*/
	  fprintf (stderr, "Error: missing arg attreplace\n");	/*JLS 21-11-00*/
	  return (-1);						/*JLS 21-11-00*/
	}							/*JLS 21-11-00*/
	mctx.attrpl = strdup (subvalue);			/*JLS 21-11-00*/
	break;							/*JLS 21-11-00*/
      case EP_ATTRLIST:						/*JLS 15-03-01*/
	return (addAttrToList (subvalue));			/*JLS 15-03-01*/
	break;							/*JLS 15-03-01*/
      case EP_ATTRSONLY:					/*JLS 03-01-01*/
	if (subvalue == NULL)					/*JLS 12-01-01*/
	{							/*JLS 12-01-01*/
	  fprintf (stderr, "Error: missing arg attrsonly\n");	/*JLS 12-01-01*/
	  return (-1);						/*JLS 12-01-01*/
	}							/*JLS 12-01-01*/
	mctx.attrsonly = atoi (subvalue);			/*JLS 03-01-01*/
	break;							/*JLS 03-01-01*/
      case EP_BIND_EACH_OPER:
	mctx.mode |= BIND_EACH_OPER;
	break;
      case EP_BINDONLY:						/*JLS 04-05-01*/
	mctx.mod2 |= M2_BINDONLY;				/*JLS 04-05-01*/
	break;							/*JLS 04-05-01*/
      case EP_CLOSE_FD:
	mctx.mode |= CLOSE_FD;
	break;
      case EP_CLTCERT_NAME:					/* BK 23-11-00*/
	mctx.mode |= CLTAUTH;				        /* BK 23-11-00*/
	if (subvalue == NULL)					/* BK 23-11-00*/
	{							/* BK 23-11-00*/
	  fprintf (stderr,"Error: missing arg for cltcertname\n");
	                                                        /* BK 23-11-00*/
	  return (-1);						/* BK 23-11-00*/
	}							/* BK 23-11-00*/
	mctx.cltcertname = strdup (subvalue);			/* BK 23-11-00*/
	break;							/* BK 23-11-00*/
      case EP_COMMONCOUNTER:					/*JLS 14-03-01*/
	mctx.mode |= COMMON_COUNTER;				/*JLS 14-03-01*/
	break;							/*JLS 14-03-01*/
      case EP_COUNT_EACH:					/*JLS 15-12-00*/
	mctx.mode |= COUNT_EACH;				/*JLS 15-12-00*/
	break;							/*JLS 15-12-00*/
      case EP_DELETE_ENTRIES:
	mctx.mode |= DELETE_ENTRIES;
	break;
      case EP_DONTSLEEPONSERVERDOWN:				/*JLS 14-03-01*/
	mctx.mode |= DONT_SLEEP_DOWN;				/*JLS 14-03-01*/
	break;							/*JLS 14-03-01*/
      case EP_EXACT_SEARCH:
	mctx.mode |= EXACT_SEARCH;
	break;
      case EP_IMAGES_DIR:					/*JLS 16-11-00*/
	if (subvalue == NULL)					/*JLS 16-11-00*/
	{							/*JLS 16-11-00*/
	  fprintf(stderr,"Error: missing arg for imagesdir\n");	/*JLS 16-11-00*/
	  return (-1);						/*JLS 16-11-00*/
	}							/*JLS 16-11-00*/
	mctx.imagesDir = strdup (subvalue);			/*JLS 16-11-00*/
	break;							/*JLS 16-11-00*/
      case EP_INCREMENTAL:
	mctx.mode |= INCREMENTAL;
	break;
      case EP_KEYDB_FILE:					/* BK 23-11-00*/
	mctx.mode |= CLTAUTH;				        /* BK 23-11-00*/
	if (subvalue == NULL)					/* BK 23-11-00*/
	{							/* BK 23-11-00*/
	  fprintf (stderr,"Error: missing arg for keydbfile\n");/* BK 23-11-00*/
	  return (-1);						/* BK 23-11-00*/
	}							/* BK 23-11-00*/
	mctx.keydbfile = strdup (subvalue);			/* BK 23-11-00*/
	break;							/* BK 23-11-00*/
      case EP_KEYDB_PIN:					/* BK 23-11-00*/
	mctx.mode |= CLTAUTH;				        /* BK 23-11-00*/
	if (subvalue == NULL)					/* BK 23-11-00*/
	{							/* BK 23-11-00*/
	  fprintf (stderr,"Error: missing arg for keydbpin\n"); /* BK 23-11-00*/
	  return (-1);						/* BK 23-11-00*/
	}							/* BK 23-11-00*/
	mctx.keydbpin = strdup (subvalue);			/* BK 23-11-00*/
	break;							/* BK 23-11-00*/
      case EP_LDAP_V2:
	mctx.mode |= LDAP_V2;
	break;
      case EP_NO_GLOBAL_STATS:					/*JLS 19-12-00*/
	mctx.globStatsCnt = -1;					/*JLS 19-12-00*/
	break;							/*JLS 19-12-00*/
      case EP_NOLOOP:
	mctx.mode |= NOLOOP;
	break;
      case EP_OBJECT:						/*JLS 19-03-01*/
	mctx.mod2 |= M2_OBJECT;				        /*JLS 19-03-01*/
	if (subvalue == NULL)					/*JLS 19-03-01*/
	{							/*JLS 19-03-01*/
	  fprintf (stderr, "Error: missing object filename\n");	/*JLS 19-03-01*/
	  return (-1);						/*JLS 19-03-01*/
	}							/*JLS 19-03-01*/
	mctx.object.fname = strdup (subvalue);			/*JLS 19-03-01*/
	break;							/*JLS 19-03-01*/
      case EP_OC_EMAILPERSON:
	mctx.mode |= OC_EMAILPERSON;
	break;
      case EP_GENLDIF:						/*JLS 19-03-01*/
	mctx.mod2 |= M2_GENLDIF;			        /*JLS 19-03-01*/
	if (subvalue == NULL)					/*JLS 19-03-01*/
	{							/*JLS 19-03-01*/
	  fprintf (stderr, "Error: missing genldif filename\n");/*JLS 19-03-01*/
	  return (-1);						/*JLS 19-03-01*/
	}							/*JLS 19-03-01*/
	mctx.genldifName = strdup (subvalue);			/*JLS 19-03-01*/
	break;							/*JLS 19-03-01*/
      case EP_OC_INETORGPRSON:					/*JLS 07-11-00*/
	mctx.mode |= OC_INETORGPRSON;				/*JLS 07-11-00*/
	break;							/*JLS 07-11-00*/
      case EP_OC_PERSON:
	mctx.mode |= OC_PERSON;
	break;
      case EP_RANDOM:
	mctx.mode |= RANDOM;
	break;
      case EP_RANDOM_BASE:
	mctx.mode |= RANDOM_BASE;
	break;
      case EP_RANDOM_BASE_HIGH:					/*JLS 13-11-00*/
	mctx.mode |= RANDOM_BASE;				/*JLS 13-11-00*/
	if (subvalue == NULL)					/*JLS 12-01-01*/
	{							/*JLS 12-01-01*/
	  fprintf(stderr,"Error: missing arg randombasehigh\n");/*JLS 12-01-01*/
	  return (-1);						/*JLS 12-01-01*/
	}							/*JLS 12-01-01*/
	mctx.baseDNHigh = atoi (subvalue);			/*JLS 13-11-00*/
	break;							/*JLS 13-11-00*/
      case EP_RANDOM_BASE_LOW:					/*JLS 13-11-00*/
	mctx.mode |= RANDOM_BASE;				/*JLS 13-11-00*/
	if (subvalue == NULL)					/*JLS 12-01-01*/
	{							/*JLS 12-01-01*/
	  fprintf(stderr, "Error: missing arg randombaselow\n");/*JLS 12-01-01*/
	  return (-1);						/*JLS 12-01-01*/
	}							/*JLS 12-01-01*/
	mctx.baseDNLow = atoi (subvalue);			/*JLS 13-11-00*/
	break;							/*JLS 13-11-00*/
      case EP_RANDOMATTRLIST:					/*JLS 15-03-01*/
	mctx.mode |= RANDOM_ATTRLIST;				/*JLS 15-03-01*/
	return (addAttrToList (subvalue));			/*JLS 15-03-01*/
	break;							/*JLS 15-03-01*/
      case EP_RANDOMBINDDN:					/*JLS 05-01-01*/
	mctx.mode |= RANDOM_BINDDN;				/*JLS 05-01-01*/
	break;							/*JLS 05-01-01*/
      case EP_RANDOMBINDDNFROMFILE:				/*JLS 03-05-01*/
	mctx.mod2 |= M2_RNDBINDFILE;				/*JLS 03-05-01*/
	if (subvalue == NULL)					/*JLS 03-05-01*/
	{							/*JLS 03-05-01*/
	  fprintf(stderr,"Error: missing file name for randombinddnfromfile\n");
	  return (-1);						/*JLS 03-05-01*/
	}							/*JLS 03-05-01*/
	mctx.rndBindFname = strdup (subvalue);			/*JLS 03-05-01*/
	break;							/*JLS 03-05-01*/
      case EP_RANDOMBINDDNHIGH:					/*JLS 05-01-01*/
	mctx.mode |= RANDOM_BINDDN;				/*JLS 05-01-01*/
	if (subvalue == NULL)					/*JLS 12-01-01*/
	{							/*JLS 12-01-01*/
	  fprintf(stderr,"Error: missing arg randombindhigh\n");/*JLS 12-01-01*/
	  return (-1);						/*JLS 12-01-01*/
	}							/*JLS 12-01-01*/
	mctx.bindDNHigh = atoi (subvalue);			/*JLS 05-01-01*/
	break;							/*JLS 05-01-01*/
      case EP_RANDOMBINDDNLOW:					/*JLS 05-01-01*/
	mctx.mode |= RANDOM_BINDDN;				/*JLS 05-01-01*/
	if (subvalue == NULL)					/*JLS 12-01-01*/
	{							/*JLS 12-01-01*/
	  fprintf(stderr, "Error: missing arg randombindlow\n");/*JLS 12-01-01*/
	  return (-1);						/*JLS 12-01-01*/
	}							/*JLS 12-01-01*/
	mctx.bindDNLow = atoi (subvalue);			/*JLS 05-01-01*/
	break;							/*JLS 05-01-01*/
      case EP_RANDOMSASLAUTHID:
	mctx.mod2 |= M2_RANDOM_SASLAUTHID;
	break;
      case EP_RANDOMSASLAUTHIDHIGH:
	mctx.mod2 |= M2_RANDOM_SASLAUTHID;
	if (subvalue == NULL)
	{
	  fprintf(stderr,"Error: missing arg randomauthidhigh\n");
	  return (-1);
	}
	mctx.sasl_authid_high = atoi (subvalue);
	break;
      case EP_RANDOMSASLAUTHIDLOW:
	mctx.mod2 |= M2_RANDOM_SASLAUTHID;
	if (subvalue == NULL)
	{
	  fprintf(stderr, "Error: missing arg randomauthidlow\n");
	  return (-1);
	}
	mctx.sasl_authid_low = atoi (subvalue);
	break;
      case EP_RDN:						/*JLS 23-03-01*/
	if (decodeRdnParam (subvalue) < 0)			/*JLS 23-03-01*/
	  return (-1);						/*JLS 23-03-01*/
	mctx.mod2 |= M2_RDN_VALUE;				/*JLS 23-03-01*/
	break;							/*JLS 23-03-01*/
      case EP_REFERRAL:						/*JLS 08-03-01*/
	if ((mctx.referral=decodeReferralParams(subvalue))<0)	/*JLS 08-03-01*/
	  return (-1);						/*JLS 08-03-01*/
	break;							/*JLS 08-03-01*/
      case EP_RENAME:
	mctx.mode |= RENAME_ENTRIES;
	break;
      case EP_SCALAB01:						/*JLS 08-01-01*/
	mctx.mode |= SCALAB01;					/*JLS 08-01-01*/
	break;							/*JLS 08-01-01*/
      case EP_SCALAB01_CNXDURATION:				/*JLS 12-01-01*/
	mctx.mode |= SCALAB01;					/*JLS 12-01-01*/
	if (subvalue == NULL)					/*JLS 12-01-01*/
	{							/*JLS 12-01-01*/
	  fprintf (stderr, "Error: missing arg scalab01_cnxduration\n");
	  return (-1);						/*JLS 12-01-01*/
	}							/*JLS 12-01-01*/
	s1ctx.cnxduration = atoi (subvalue);			/*JLS 12-01-01*/
	break;							/*JLS 12-01-01*/
      case EP_SCALAB01_MAXCNXNB:				/*JLS 12-01-01*/
	mctx.mode |= SCALAB01;					/*JLS 12-01-01*/
	if (subvalue == NULL)					/*JLS 12-01-01*/
	{							/*JLS 12-01-01*/
	  fprintf (stderr, "Error: missing arg scalab01_maxcnxnb\n");
	  return (-1);						/*JLS 12-01-01*/
	}							/*JLS 12-01-01*/
	s1ctx.maxcnxnb = atoi (subvalue);			/*JLS 12-01-01*/
	break;							/*JLS 12-01-01*/
      case EP_SCALAB01_WAIT:					/*JLS 12-01-01*/
	mctx.mode |= SCALAB01;					/*JLS 12-01-01*/
	if (subvalue == NULL)					/*JLS 12-01-01*/
	{							/*JLS 12-01-01*/
	  fprintf(stderr, "Error: missing arg scalab01_wait\n");/*JLS 12-01-01*/
	  return (-1);						/*JLS 12-01-01*/
	}							/*JLS 12-01-01*/
	s1ctx.wait = atoi (subvalue);				/*JLS 12-01-01*/
	break;							/*JLS 12-01-01*/
      case EP_SMOOTH_SHUTDOWN:					/*JLS 17-11-00*/
	mctx.mode |= SMOOTHSHUTDOWN;				/*JLS 17-11-00*/
	break;							/*JLS 17-11-00*/
      case EP_STRING:
	mctx.mode |= STRING;
	break;
      case EP_WITH_NEWPARENT:					/*JLS 15-12-00*/
	mctx.mode |= WITH_NEWPARENT;				/*JLS 15-12-00*/
	break;							/*JLS 15-12-00*/
      default:
	fprintf (stderr, "Error: illegal option -e %s\n", subvalue);
	return (-1);
	break;
    }
  }
  /*
   * SSL client authentication is only supported in LDAP_V3, because we have
   * to perform a SASL BIND operation in this case, and it is V3 specific
   */
  if ((mctx.mode & LDAP_V2) && (mctx.mode & CLTAUTH))
  {
    fprintf 
      (stderr,"Error: SSL client authentication is supported in LDAPv3 only");
    return (-1);
  }
  return (0);
}






					/* New function */	/*JLS 23-04-01*/
/* ****************************************************************************
	FUNCTION :	buildArgListString
	PURPOSE :	Saved the arguments of ldclt into a string.
	INPUT :		argc, argv
	OUTPUT :	None.
	RETURN :	The resulting string.
	DESCRIPTION :
 *****************************************************************************/
char *
buildArgListString (
	int	  argc,
	char	**argv)
{
  char	*argvList;	/* Arg list */
  int	 lgth;		/* Length of argv list */
  int	 i;		/* For the loops */

  /*
   * Compute the length
   */
  lgth = 0;
  for (i=0 ; i<argc ; i++)
  {
    lgth += strlen (argv[i]) + 1;
    if ((strchr (argv[i], ' ') != NULL) || (strchr (argv[i], '\t') != NULL))
      lgth += 2;
  }
  argvList = (char *) malloc (lgth);
  argvList[0] = '\0';
  strcat (argvList, argv[0]);
  for (i=1 ; i<argc ; i++)
  {
    strcat (argvList, " ");
    if ((strchr (argv[i], ' ') == NULL) && (strchr (argv[i], '\t') == NULL))
      strcat (argvList, argv[i]);
    else
    {
      strcat (argvList, "\"");
      strcat (argvList, argv[i]);
      strcat (argvList, "\"");
    }
  }
  

  return (argvList);
}








/* ****************************************************************************
	FUNCTION :	main
	PURPOSE :	Main function of ldclt
	INPUT :		argc, argv	= see man page
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
main (
	int	 argc,
	char **argv)
{
  int	  opt_ret;	/* For getopt() */
  int	  i;		/* For the loops */
  time_t  tim;		/* For time() */			/*JLS 18-08-00*/
  char	 *argvList;	/* To keep track in core files */	/*JLS 07-12-00*/
  int	  found;	/* General purpose variable */		/*JLS 18-12-00*/
  char	  verStr[40];	/* Version string */			/*JLS 13-03-01*/

  /*
   * Build the argv list to keep track of it...
   * Print version.
   * We are using a string variable to keep the version in the core files.
   */
  argvList = buildArgListString (argc, argv);			/*JLS 23-04-01*/
  sprintf (verStr, "ldclt version %s", ldcltVersion);		/*JLS 13-03-01*/
  printf ("%s\n", verStr);					/*JLS 13-03-01*/

  /*
   * Initialization
   */
  mctx.attrlistNb    = 0;					/*JLS 15-03-01*/
  mctx.attrsonly     = DEF_ATTRSONLY;				/*JLS 03-01-01*/
  mctx.baseDN        = "dc=example,dc=com";
  mctx.baseDNLow     = -1;					/*JLS 13-11-00*/
  mctx.baseDNHigh    = -1;					/*JLS 13-11-00*/
  mctx.bindDN	     = NULL;
  mctx.bindDNLow     = -1;					/*JLS 05-01-01*/
  mctx.bindDNHigh    = -1;					/*JLS 05-01-01*/
  mctx.dlf	     = NULL;					/*JLS 23-03-01*/
  mctx.exitStatus    = EXIT_OK;					/*JLS 25-08-00*/
  mctx.filter	     = NULL;
  mctx.globStatsCnt  = DEF_GLOBAL_NB;				/*JLS 08-08-00*/
  mctx.hostname	     = "localhost";
  mctx.ignErrNb      = 0;
  mctx.images	     = NULL;					/*JLS 17-11-00*/
  mctx.imagesDir     = DEF_IMAGES_PATH;				/*JLS 16-11-00*/
  mctx.inactivMax    = DEF_INACTIV_MAX;
  mctx.maxErrors     = DEF_MAX_ERRORS;
  mctx.mode	     = NOTHING;
  mctx.mod2	     = NOTHING;
  mctx.nbNoActivity  = 0;
  mctx.nbSamples     = -1;
  mctx.nbThreads     = DEF_NB_THREADS;
  mctx.opListTail    = NULL;
  mctx.passwd	     = NULL;
  mctx.pid	     = getpid();
  mctx.port	     = DEF_PORT;
  mctx.randomLow     = -1;
  mctx.randomHigh    = -1;
  mctx.referral	     = DEF_REFERRAL;				/*JLS 08-03-01*/
  mctx.sampling      = DEF_SAMPLING;
  mctx.sasl_authid   = NULL;
  mctx.sasl_flags    = LDAP_SASL_QUIET;
  mctx.sasl_mech     = NULL;
  mctx.sasl_realm    = NULL;
  mctx.sasl_secprops = NULL;
  mctx.sasl_username = NULL;
  mctx.scope	     = DEF_SCOPE;
  mctx.slaveConn     = 0;
  mctx.slavesNb      = 0;
  mctx.timeout	     = DEF_TIMEOUT;
  mctx.totalReq	     = -1;
  mctx.waitSec       = 0;
  s1ctx.cnxduration  = SCALAB01_DEF_CNX_DURATION;		/*JLS 12-01-01*/
  s1ctx.maxcnxnb     = SCALAB01_DEF_MAX_CNX;			/*JLS 12-01-01*/
  s1ctx.wait         = SCALAB01_DEF_WAIT_TIME;			/*JLS 12-01-01*/

  /*
   * Initiates the object *NOW*
   * It is mandatory to do it *NOW* because of -e rdn option.
   */
  mctx.object.attribsNb = 0;					/*JLS 23-03-01*/
  mctx.object.rdn       = NULL;					/*JLS 23-03-01*/
  for (i=0 ; i+VAR_MIN < VAR_MAX ; i++)				/*JLS 23-03-01*/
    mctx.object.var[i] = NULL;					/*JLS 23-03-01*/

  /*
   * Get options
   */
  while ((opt_ret = getopt (argc, argv, 
		"a:b:D:e:E:f:h:i:I:n:N:o:p:qQr:R:s:S:t:T:vVw:W:Z:H")) != EOF)
    switch (opt_ret)
    {
      case 'a':
	mctx.mode     |= ASYNC;
	mctx.asyncMax  = atoi (optarg);
	mctx.asyncMin  = mctx.asyncMax / 2;
	break;
      case 'b':
	mctx.baseDN = optarg;
	break;
      case 'D':
	mctx.bindDN = optarg;
	break;
      case 'e':
	if (decodeExecParams (optarg) < 0)
	  ldcltExit (EXIT_PARAMS);				/*JLS 13-11-00*/
	break;
      case 'E':
	mctx.maxErrors = atoi (optarg);
	break;
      case 'f':
	mctx.filter = optarg;
	break;
      case 'h':
	mctx.hostname = optarg;
	break;
      case 'i':
	mctx.inactivMax = atoi (optarg);
	break;
      case 'I':
	found = 0;						/*JLS 18-12-00*/
	for (i=0 ; i<mctx.ignErrNb ; i++)			/*JLS 18-12-00*/
	  if (mctx.ignErr[i] == atoi (optarg))			/*JLS 18-12-00*/
	    found = 1;						/*JLS 18-12-00*/
	if (found)						/*JLS 18-12-00*/
	  break;						/*JLS 18-12-00*/
	if (mctx.ignErrNb == MAX_IGN_ERRORS)
	{
	  fprintf (stderr, "Error: too many errors to ignore.\n");
	  ldcltExit (EXIT_PARAMS);				/*JLS 13-11-00*/
	}
	mctx.ignErr[mctx.ignErrNb++] = atoi (optarg);
	break;
      case 'n':
	mctx.nbThreads = atoi (optarg);
	break;
      case 'N':
	mctx.nbSamples = atoi (optarg);
	break;
      case 'o':
        if (saslSetParam (optarg) < 0)
          ldcltExit (EXIT_PARAMS);
        mctx.mod2 |= M2_SASLAUTH;
        break;
      case 'p':
	mctx.port = atoi (optarg);
	break;
      case 'P':
	masterPort = atoi (optarg);
	break;
      case 'q':
	mctx.mode |= QUIET;
	break;
      case 'Q':
	mctx.mode |= QUIET;
	mctx.mode |= SUPER_QUIET;
	break;
      case 'r':
	mctx.randomLow = atoi (optarg);
	break;
      case 'R':
	mctx.randomHigh = atoi (optarg);
	break;
      case 's':
	if (decodeScopeParams (optarg) < 0)
	  ldcltExit (EXIT_PARAMS);				/*JLS 13-11-00*/
	break;
      case 't':
	mctx.timeout = atoi (optarg);
	break;
      case 'S':
	mctx.slaves[mctx.slavesNb] = optarg;
	mctx.slavesNb++;
	break;
      case 'T':
	mctx.totalReq = atoi (optarg);
	break;
      case 'v':
	mctx.mode |= VERBOSE;
	break;
      case 'V':
	mctx.mode |= VERBOSE;
	mctx.mode |= VERY_VERBOSE;
	break;
      case 'w':
	mctx.passwd = optarg;
	break;
      case 'W':
	mctx.waitSec = atoi (optarg);
	break;
      case 'Z':
	mctx.mode |= SSL;
	mctx.certfile = optarg;
	break;
      case 'H':
	usage ();
	ldcltExit (EXIT_OK);					/*JLS 18-12-00*/
	break;
      case '?':
	usage ();
	ldcltExit (EXIT_PARAMS);				/*JLS 13-11-00*/
	break;
    }

  /*
   * It should not be any extra argument.
   */
  if (optind != argc)						/*JLS 23-04-01*/
  {								/*JLS 23-04-01*/
    fprintf (stderr, "Error: unexpected extra arguments.\n");	/*JLS 23-04-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 23-04-01*/
  }								/*JLS 23-04-01*/

  /*
   * If scalab01 is set, then some other features are automatically enabled.
   */
  if (mctx.mode & SCALAB01)					/*JLS 08-01-01*/
  {								/*JLS 08-01-01*/
    mctx.mode |= BIND_EACH_OPER;				/*JLS 08-01-01*/
    mctx.mode |= RANDOM_BINDDN;					/*JLS 08-01-01*/
  }								/*JLS 08-01-01*/

  /*
   * Check coherency
   */
  if (mctx.nbThreads <= 0)
  {
    fprintf (stderr, "Error: it must be at least 1 thread, not \"%d\"\n",
		mctx.nbThreads);
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if (mctx.nbThreads > MAX_THREADS)
  {
    fprintf (stderr, "Error: too many threads %d, maximum is %d\n",
		mctx.nbThreads, MAX_THREADS);
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if ((!(mctx.mode & VALID_OPERS)) &&
      (!(mctx.mod2 & M2_VALID_OPERS)))				/*JLS 04-05-01*/
  {
    fprintf (stderr, "Error: don't know what to do...\n");
    fprintf (stderr, "Error: please use option -e for this purpose.\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if ((mctx.mod2 & M2_APPEND) && (!(mctx.mod2 & M2_GENLDIF)))	/*JLS 05-04-01*/
  {								/*JLS 05-04-01*/
    fprintf (stderr, "Error: -e append requires -e genldif.\n");/*JLS 05-04-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 05-04-01*/
  }								/*JLS 05-04-01*/
  if ((mctx.filter != NULL) && (mctx.mod2 & M2_RDN_VALUE))	/*JLS 23-03-01*/
  {								/*JLS 23-03-01*/
    fprintf (stderr, "Error: -f and -e rdn= are exclusive.\n");	/*JLS 23-03-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 23-03-01*/
  }								/*JLS 23-03-01*/
  if ((mctx.mod2 & M2_RDN_VALUE) && (mctx.mode & RANDOM))	/*JLS 28-03-01*/
  {								/*JLS 28-03-01*/
    fprintf (stderr, "Error: exclusive -e random and -e rdn\n");/*JLS 28-03-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 28-03-01*/
  }								/*JLS 28-03-01*/
  if ((mctx.mod2 & M2_RDN_VALUE) && (mctx.mode & INCREMENTAL))	/*JLS 28-03-01*/
  {								/*JLS 28-03-01*/
    fprintf (stderr, "Error: exclusive -e incr and -e rdn\n");	/*JLS 28-03-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 28-03-01*/
  }								/*JLS 28-03-01*/
  if ((mctx.mode & NEED_FILTER) && 
      ((mctx.filter == NULL) && (!(mctx.mod2 & M2_RDN_VALUE))))	/*JLS 23-03-01*/
  {
    fprintf (stderr, "Error: missing filter...\n");
    fprintf (stderr, "Error: use -f or -e rdn=value for this purpose.\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if ((!((mctx.mode & NEED_FILTER) || (mctx.mod2 & M2_GENLDIF)))/*JLS 04-05-01*/
	&& (mctx.filter != NULL))				/*JLS 04-05-01*/
  {								/*JLS 04-05-01*/
    fprintf (stderr, "Error: do not need filter -f\n");		/*JLS 04-05-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 04-05-01*/
  }								/*JLS 04-05-01*/
  if ((!((mctx.mode & NEED_FILTER) || (mctx.mod2 & M2_GENLDIF)))/*JLS 04-05-01*/
	&& (mctx.mod2 & M2_RDN_VALUE))				/*JLS 04-05-01*/
  {								/*JLS 04-05-01*/
    fprintf (stderr, "Error: do not need -e rdn=value\n");	/*JLS 04-05-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 04-05-01*/
  }								/*JLS 04-05-01*/
  if ((mctx.mod2 & M2_RDN_VALUE) &&				/*JLS 28-03-01*/
      ((mctx.randomLow >= 0) || (mctx.randomHigh > 0)))		/*JLS 28-03-01*/
  {								/*JLS 28-03-01*/
    fprintf (stderr, "Error: -e rdn exclusive with -r or -R\n");/*JLS 28-03-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 28-03-01*/
  }								/*JLS 28-03-01*/
  if (mctx.mode & NEED_RANGE)					/*JLS 28-03-01*/
  {								/*JLS 28-03-01*/
    if (((mctx.randomLow >= 0) && (mctx.randomHigh < 0)) ||
	((mctx.randomLow <  0) && (mctx.randomHigh > 0)) ||
	(mctx.randomLow > mctx.randomHigh))
    {
      fprintf (stderr, "Error: invalid random levels %d and %d\n",
		  mctx.randomLow, mctx.randomHigh);
      fprintf (stderr, "Error: use both options -r and -R for this purpose.\n");
      ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
    }
    if ((mctx.randomLow < 0) && (mctx.randomHigh < 0))		/*JLS 28-03-01*/
    {
      fprintf (stderr, "Error: missing values range.\n");
      fprintf (stderr, "Error: use both options -r and -R for this purpose.\n");
      ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
    }
  }								/*JLS 28-03-01*/
  if (mctx.inactivMax < 0)
  {
    fprintf (stderr, "Error: max times inactivity should not be negative.\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if (mctx.maxErrors < 0)
  {
    fprintf (stderr, "Error: max allowed errors should not be negative.\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if ((mctx.mode & INCREMENTAL) && (mctx.mode & RANDOM))
  {
    fprintf (stderr, "Error: modes -e incr and -e random are exclusive.\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if ((mctx.mode & NOLOOP) && (!(mctx.mode & INCREMENTAL)))
  {
    fprintf (stderr, "Error: mode -e noloop requires mode -e incr.\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if ((mctx.mode & NEED_RND_INCR) && 
      (!((mctx.mode & (RANDOM|INCREMENTAL)) || (mctx.mod2 & M2_RDN_VALUE))))
  {
    fprintf (stderr, "Error: -e add requires either -e incr/random/rdn\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if (mctx.filter != NULL)					/*JLS 07-12-00*/
  {								/*JLS 07-12-00*/
    for (i=0 ; (mctx.filter[i] != '\0') && (mctx.filter[i] != '=') ; i++);
    if (mctx.filter[i] != '=')
    {
      fprintf (stderr, "Error: filter must be \"attrib=value\", not \"%s\"\n",
	  mctx.filter);
      ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
    }
  }								/*JLS 07-12-00*/
  if ((mctx.mode & NEED_CLASSES) && 
	(!((mctx.mode & THE_CLASSES) || (mctx.mod2 & M2_OBJECT))))
  {
    fprintf (stderr, "Error: missing classes (option -e)\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if ((mctx.mode & STRING) && (!(mctx.mode & RANDOM)))
  {
    fprintf (stderr, "Error: -e string is only valid with -e random.\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if (mctx.waitSec < 0)
  {
    fprintf (stderr, "Error: -W should have a positive value.\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }
  if ((mctx.mode & RANDOM_BASE) && 				/*JLS 13-11-00*/
      ((mctx.baseDNLow < 0) || (mctx.baseDNHigh < 0)))		/*JLS 13-11-00*/
  {								/*JLS 13-11-00*/
    fprintf (stderr, "Error: missing ranges for randombase.\n");/*JLS 13-11-00*/
    fprintf (stderr, "Error: use option -e randombaselow=\n");	/*JLS 13-11-00*/
    fprintf (stderr, "Error: use option -e randombasehigh=\n");	/*JLS 13-11-00*/
    ldcltExit (EXIT_PARAMS);					/*JLS 13-11-00*/
  }								/*JLS 13-11-00*/
  if ((mctx.mod2 & M2_RNDBINDFILE) && 				/*JLS 03-05-01*/
      (mctx.mode & RANDOM_BINDDN))				/*JLS 03-05-01*/
  {								/*JLS 03-05-01*/
    fprintf (stderr, 						/*JLS 03-05-01*/
	"Error : exclusive -e randombinddn and -e randombinddnfromfile\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 03-05-01*/
  }								/*JLS 03-05-01*/
  if ((mctx.mode & RANDOM_BINDDN) && 				/*JLS 05-01-01*/
      ((mctx.bindDNLow < 0) || (mctx.bindDNHigh < 0)))		/*JLS 05-01-01*/
  {								/*JLS 05-01-01*/
    fprintf(stderr,"Error: missing ranges for randombinddn.\n");/*JLS 05-01-01*/
    fprintf(stderr,"Error: use option -e randombinddnlow=\n");	/*JLS 05-01-01*/
    fprintf(stderr,"Error: use option -e randombinddnhigh=\n");	/*JLS 05-01-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 05-01-01*/
  }								/*JLS 05-01-01*/
  if ((mctx.mod2 & M2_RANDOM_SASLAUTHID) &&
      ((mctx.sasl_authid_low < 0) || (mctx.sasl_authid_high < 0)))
  {
    fprintf(stderr,"Error: missing ranges for randomauthid.\n");
    fprintf(stderr,"Error: use option -e randomauthidlow=\n");
    fprintf(stderr,"Error: use option -e randomauthidhigh=\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 05-01-01*/
  }								/*JLS 05-01-01*/
  if (mctx.mode & CLTAUTH)                                      /* BK 23-11-00*/
  {                                                             /* BK 23-11-00*/
    if (!(mctx.mode & SSL))                                     /* BK 23-11-00*/
    {                                                           /* BK 23-11-00*/
      fprintf (stderr, "Error: no certificate DB specified.\n");/* BK 23-11-00*/
      fprintf (stderr, "Error: use -Z certfile.\n");            /* BK 23-11-00*/
      ldcltExit (EXIT_PARAMS);                                  /* BK 23-11-00*/
    }                                                           /* BK 23-11-00*/
    if (mctx.cltcertname == NULL)                               /* BK 23-11-00*/
    {								/* BK 23-11-00*/
      fprintf (stderr, "Error: no client certificate name specified.\n");
							        /* BK 23-11-00*/
      fprintf (stderr, "Error: use option -e cltcertname=\n");  /* BK 23-11-00*/
      ldcltExit (EXIT_PARAMS);					/* BK 23-11-00*/
    }								/* BK 23-11-00*/
    if (mctx.keydbfile == NULL)                                 /* BK 23-11-00*/
    {								/* BK 23-11-00*/
      fprintf (stderr, "Error: no key database file specified.\n");
							        /* BK 23-11-00*/
      fprintf (stderr, "Error: use option -e keydbfile=\n");    /* BK 23-11-00*/
      ldcltExit (EXIT_PARAMS);					/* BK 23-11-00*/
    }								/* BK 23-11-00*/
    if (mctx.keydbpin == NULL)                                  /* BK 23-11-00*/
    {								/* BK 23-11-00*/
      fprintf (stderr, "Error: no key database password specified.\n");
							        /* BK 23-11-00*/
      fprintf (stderr, "Error: use option -e keydbpin=\n");     /* BK 23-11-00*/
      ldcltExit (EXIT_PARAMS);					/* BK 23-11-00*/
    }								/* BK 23-11-00*/
  }
  if ((mctx.mode & WITH_NEWPARENT) && 				/*JLS 15-12-00*/
      (!(mctx.mode & RENAME_ENTRIES)))				/*JLS 15-12-00*/
  {								/*JLS 15-12-00*/
    fprintf (stderr, "Error : option -e withnewparent needs -e rename\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 15-12-00*/
  }								/*JLS 15-12-00*/
  if ((mctx.attrsonly != 0) && (mctx.attrsonly != 1))		/*JLS 03-01-01*/
  {								/*JLS 03-01-01*/
    fprintf (stderr, "Error : option -e attrsonly=%d not 0|1\n",/*JLS 03-01-01*/
			mctx.attrsonly);			/*JLS 03-01-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 03-01-01*/
  }								/*JLS 03-01-01*/
  if (mctx.mode & SCALAB01)					/*JLS 12-01-01*/
  {								/*JLS 12-01-01*/
    if (s1ctx.cnxduration <= 0)					/*JLS 12-01-01*/
    {								/*JLS 12-01-01*/
      fprintf (stderr, "Error : -e scalab01_cnxduration=%d <= 0\n",
			s1ctx.cnxduration);			/*JLS 12-01-01*/
      ldcltExit (EXIT_PARAMS);					/*JLS 12-01-01*/
    }								/*JLS 12-01-01*/
    if (s1ctx.maxcnxnb <= 0)					/*JLS 12-01-01*/
    {								/*JLS 12-01-01*/
      fprintf (stderr, "Error : -e scalab01_maxcnxnb=%d <= 0\n",/*JLS 12-01-01*/
			s1ctx.maxcnxnb);			/*JLS 12-01-01*/
      ldcltExit (EXIT_PARAMS);					/*JLS 12-01-01*/
    }								/*JLS 12-01-01*/
    if (s1ctx.wait <= 0)					/*JLS 12-01-01*/
    {								/*JLS 12-01-01*/
      fprintf (stderr, "Error : -e scalab01_wait=%d <= 0\n",	/*JLS 12-01-01*/
			s1ctx.wait);				/*JLS 12-01-01*/
      ldcltExit (EXIT_PARAMS);					/*JLS 12-01-01*/
    }								/*JLS 12-01-01*/
  }								/*JLS 12-01-01*/
  if ((mctx.referral == REFERRAL_REBIND) &&			/*JLS 14-03-01*/
      ((mctx.bindDN == NULL) || (mctx.passwd == NULL)))		/*JLS 14-03-01*/
  {								/*JLS 14-03-01*/
    fprintf (stderr, "Error: -e referral=rebind needs -D and -w\n");/*14-03-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 14-03-01*/
  }								/*JLS 14-03-01*/
  if ((mctx.mode & COMMON_COUNTER) &&				/*JLS 14-03-01*/
      (!((mctx.mode & INCREMENTAL) || (mctx.mod2 & M2_OBJECT))))/*JLS 28-03-01*/
  {								/*JLS 14-03-01*/
    fprintf (stderr, "Error: -e commoncounter needs -e incr or -e object\n");
    ldcltExit (EXIT_PARAMS);					/*JLS 14-03-01*/
  }								/*JLS 14-03-01*/
  if ((mctx.attrlistNb != 0) && (!(mctx.mode & EXACT_SEARCH)))	/*JLS 15-03-01*/
  {								/*JLS 15-03-01*/
    fprintf(stderr,"Error : -e attrlist requires -e esearch\n");/*JLS 15-03-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 15-03-01*/
  }								/*JLS 15-03-01*/
  if ((mctx.mod2 & M2_GENLDIF) && (mctx.mode & VALID_OPERS))	/*JLS 19-03-01*/
  {								/*JLS 19-03-01*/
    fprintf(stderr,"Error : -e genldif is exclusive.\n"); 	/*JLS 19-03-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 19-03-01*/
  }								/*JLS 19-03-01*/
  if ((mctx.mod2 & M2_RDN_VALUE) && (!(mctx.mod2 & M2_OBJECT)))	/*JLS 23-03-01*/
  {								/*JLS 23-03-01*/
    fprintf(stderr,"Error : -e rdn needs -e object.\n"); 	/*JLS 23-03-01*/
    ldcltExit (EXIT_PARAMS);					/*JLS 23-03-01*/
  }								/*JLS 23-03-01*/

  /*
   * Basic initialization from the user's parameters/options
   */
  if (basicInit() < 0)
    ldcltExit (EXIT_INIT);					/*JLS 18-12-00*/

  /*
   * What are we doing now...
   */
  if (mctx.mode & VERBOSE)
  {
    printf ("%s\n", argvList);					/*JLS 07-12-00*/
    printf ("Process ID         = %d\n", mctx.pid);
    printf ("Host to connect    = %s\n", mctx.hostname);
    printf ("Port number        = %d\n", mctx.port);
    if (mctx.bindDN == NULL)
      printf ("Bind DN            = NULL\n");
    else
      printf ("Bind DN            = %s\n", mctx.bindDN);
    if (mctx.passwd == NULL)
      printf ("Passwd             = NULL\n");
    else
      printf ("Passwd             = %s\n", mctx.passwd);
    switch (mctx.referral)					/*JLS 08-03-01*/
    {								/*JLS 08-03-01*/
      case REFERRAL_OFF:					/*JLS 08-03-01*/
	printf ("Referral           = off\n");			/*JLS 08-03-01*/
	break;							/*JLS 08-03-01*/
      case REFERRAL_ON:						/*JLS 08-03-01*/
	printf ("Referral           = on\n");			/*JLS 08-03-01*/
	break;							/*JLS 08-03-01*/
      case REFERRAL_REBIND:					/*JLS 08-03-01*/
	printf ("Referral           = rebind\n");		/*JLS 08-03-01*/
	break;							/*JLS 08-03-01*/
    }								/*JLS 08-03-01*/
    printf ("Base DN            = %s\n", mctx.baseDN);
    if (mctx.filter == NULL)
      printf ("Filter             = NULL\n");
    else
      printf ("Filter             = \"%s\"\n", mctx.filter);
    if (mctx.attrlistNb > 0)					/*JLS 15-03-01*/
    {								/*JLS 15-03-01*/
      printf ("Attributes list    =");				/*JLS 15-03-01*/
      for (i=0 ; i<mctx.attrlistNb ; i++)			/*JLS 15-03-01*/
	printf (" %s", mctx.attrlist[i]);			/*JLS 15-03-01*/
      printf ("\n");						/*JLS 15-03-01*/
    }								/*JLS 15-03-01*/
    printf ("Max times inactive = %d\n", mctx.inactivMax);
    printf ("Max allowed errors = %d\n", mctx.maxErrors);
    printf ("Number of samples  = %d\n", mctx.nbSamples);
    printf ("Number of threads  = %d\n", mctx.nbThreads);
    printf ("Total op. req.     = %d\n", mctx.totalReq);
    printf ("Running mode       = 0x%08x\n", mctx.mode);
    printf ("Running mode       =");
    dumpModeValues ();						/*JLS 19-03-01*/
    printf ("\n");
    if (mctx.mode & SCALAB01)					/*JLS 12-01-01*/
    {								/*JLS 12-01-01*/
      printf("Scalab01 cnx dur.  = %d sec\n",s1ctx.cnxduration);/*JLS 12-01-01*/
      printf ("Scalab01 max nb cnx= %d\n", s1ctx.maxcnxnb);	/*JLS 12-01-01*/
      printf ("Scalab01 wait time = %d sec\n", s1ctx.wait);	/*JLS 12-01-01*/
    }								/*JLS 12-01-01*/
    printf ("LDAP oper. timeout = %d sec\n", mctx.timeout);
    printf ("Sampling interval  = %d sec\n", mctx.sampling);
    if (mctx.mode & EXACT_SEARCH)
    {								/*JLS 03-01-01*/
      switch (mctx.scope)
      {
	case LDAP_SCOPE_BASE:
		printf ("Scope              = base\n");
		break;
	case LDAP_SCOPE_ONELEVEL:
		printf ("Scope              = one level\n");
		break;
	case LDAP_SCOPE_SUBTREE:
		printf ("Scope              = subtree\n");
		break;
      }
      printf ("Attrsonly          = %d\n", mctx.attrsonly);	/*JLS 03-01-01*/
    }								/*JLS 03-01-01*/
    if (mctx.images != NULL)					/*JLS 17-11-00*/
      printf ("Images directory   = %s\n", mctx.imagesDir);	/*JLS 17-11-00*/
    if ((mctx.mode & NEED_RANGE) && (mctx.randomLow >= 0))	/*JLS 28-03-01*/
      printf ("Values range       = [%d , %d]\n", 
				mctx.randomLow, mctx.randomHigh);
    if ((mctx.mode & (RANDOM | INCREMENTAL)) &&
	(!(mctx.mod2 & M2_RDN_VALUE)))				/*JLS 23-03-01*/
    {
      printf ("Filter's head      = \"%s\"\n", mctx.randomHead);
      printf ("Filter's tail      = \"%s\"\n", mctx.randomTail);
    }
    if (mctx.mode & RANDOM_BASE)
    {
      printf ("Base DN's head     = \"%s\"\n", mctx.baseDNHead);
      printf ("Base DN's tail     = \"%s\"\n", mctx.baseDNTail);
      printf ("Base DN's range    = [%d , %d]\n", 		/*JLS 13-11-00*/
			mctx.baseDNLow, mctx.baseDNHigh);	/*JLS 13-11-00*/
    }
    if (mctx.mode & RANDOM_BINDDN)				/*JLS 05-01-01*/
    {								/*JLS 05-01-01*/
      printf ("Bind DN's head     = \"%s\"\n", mctx.bindDNHead);/*JLS 05-01-01*/
      printf ("Bind DN's tail     = \"%s\"\n", mctx.bindDNTail);/*JLS 05-01-01*/
      printf ("Bind DN's range    = [%d , %d]\n", 		/*JLS 05-01-01*/
			mctx.bindDNLow, mctx.bindDNHigh);	/*JLS 05-01-01*/
      printf ("Bind passwd's head = \"%s\"\n", mctx.passwdHead);/*JLS 05-01-01*/
      printf ("Bind passwd's tail = \"%s\"\n", mctx.passwdTail);/*JLS 05-01-01*/
    }								/*JLS 05-01-01*/
    if (mctx.mod2 & M2_RANDOM_SASLAUTHID)
    {								/*JLS 05-01-01*/
      printf ("Bind Authid's head     = \"%s\"\n", mctx.sasl_authid_head);
      printf ("Bind Authid's tail     = \"%s\"\n", mctx.sasl_authid_tail);
      printf ("Bind Authid's range    = [%d , %d]\n",
			mctx.sasl_authid_low, mctx.sasl_authid_high);
    }								/*JLS 05-01-01*/
    if (mctx.mode & ATTR_REPLACE)				/*JLS 21-11-00*/
    {								/*JLS 21-11-00*/
      printf ("Attribute's head   = \"%s\"\n", mctx.attrplHead);/*JLS 21-11-00*/
      printf ("Attribute's tail   = \"%s\"\n", mctx.attrplTail);/*JLS 21-11-00*/
    }								/*JLS 21-11-00*/
    if (mctx.mode & ASYNC)
    {
      printf ("Async max pending  = %d\n", mctx.asyncMax);
      printf ("Async min pending  = %d\n", mctx.asyncMin);
    }
    for (i=0 ; i<mctx.ignErrNb ; i++)
      printf ("Ignore error       = %d (%s)\n", 
		mctx.ignErr[i], my_ldap_err2string (mctx.ignErr[i]));
    fflush (stdout);
    if (mctx.slavesNb > 0)
    {
      printf ("Slave(s) to check  =");
      for (i=0 ; i<mctx.slavesNb ; i++)
	printf (" %s", mctx.slaves[i]);
      printf ("\n");
    }
  }

  /*
   * Let's go!
   */
  tim = time(NULL);
  printf ("ldclt[%d]: Starting at %s\n", mctx.pid, ctime (&tim));			/*JLS 18-08-00*/
  if (runThem() < 0)
    ldcltExit (EXIT_OTHER);					/*JLS 25-08-00*/
  if (initMainThread() < 0)
    ldcltExit (EXIT_OTHER);					/*JLS 25-08-00*/
  if (monitorThem() < 0)
    ldcltExit (EXIT_OTHER);					/*JLS 25-08-00*/
  if (printGlobalStatistics() < 0)
    ldcltExit (EXIT_OTHER);					/*JLS 25-08-00*/

  ldcltExit (mctx.exitStatus);					/*JLS 25-08-00*/

  return mctx.exitStatus;
}


/* End of file */
