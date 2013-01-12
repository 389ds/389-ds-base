#ident "ldclt @(#)threadMain.c	1.40 01/05/04"

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
	FILE :		threadMain.c
	AUTHOR :	Jean-Luc SCHWING
	VERSION :       1.0
	DATE :		04 December 1998
	DESCRIPTION :	
			This file implements the main/core part of the 
			threads. The ldap part is in another source file.
 	LOCAL :		None.
	HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
04/12/98 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
10/12/98 | JL Schwing	| 1.2 : Add nb of errors statistics.
---------+--------------+------------------------------------------------------
10/12/98 | JL Schwing	| 1.3 : Implement asynchronous mode.
---------+--------------+------------------------------------------------------
11/12/98 | JL Schwing	| 1.4 : Implement max errors threshold.
			| fflush(stdout) after each printf.
---------+--------------+------------------------------------------------------
14/12/98 | JL Schwing	| 1.5 : Implement "-e close".
			| Add "thread is dead" message.
---------+--------------+------------------------------------------------------
16/12/98 | JL Schwing	| 1.6 : Implement "-e add" and "-e delete".
---------+--------------+------------------------------------------------------
23/12/98 | JL Schwing	| 1.7 : bug fix - crash in msgIdDel().
---------+--------------+------------------------------------------------------
28/12/98 | JL Schwing	| 1.8 : Add tag asyncHit.
---------+--------------+------------------------------------------------------
13/01/99 | JL Schwing	| 1.9 : Implement "-e string".
---------+--------------+------------------------------------------------------
18/01/99 | JL Schwing	| 1.10: Implement "-e randombase".
---------+--------------+------------------------------------------------------
21/01/99 | JL Schwing	| 1.11: Implement "-e ascii".
---------+--------------+------------------------------------------------------
26/02/99 | JL Schwing	| 1.12: Improve strict ascii: reject more characters.
---------+--------------+------------------------------------------------------
26/02/99 | JL Schwing	| 1.13: Quote (aka \) characters rather than reject.
---------+--------------+------------------------------------------------------
04/05/99 | JL Schwing	| 1.14: Modify msgId*() to memorize attribs as well.
---------+--------------+------------------------------------------------------
19/05/99 | JL Schwing	| 1.15: Implement "-e rename".
			| Exit the thread if status==DEAD
---------+--------------+------------------------------------------------------
28/05/99 | JL Schwing	| 1.16: Add new option -W (wait).
---------+--------------+------------------------------------------------------
06/03/00 | JL Schwing	| 1.17: Test malloc() return value.
---------+--------------+------------------------------------------------------
18/08/00 | JL Schwing	| 1.18: Print begin and end dates.
---------+--------------+------------------------------------------------------
25/08/00 | JL Schwing	| 1.19: Implement consistent exit status...
			| Fix some old legacy code.
---------+--------------+------------------------------------------------------
14/11/00 | JL Schwing	| 1.20: Will now use utils.c functions.
---------+--------------+------------------------------------------------------
17/11/00 | JL Schwing	| 1.21: Implement "-e smoothshutdown".
			| Add new functions setThreadStatus() getThreadStatus().
---------+--------------+------------------------------------------------------
21/11/00 | JL Schwing	| 1.22: Implement "-e attreplace=name:mask"
---------+--------------+------------------------------------------------------
29/11/00 | JL Schwing	| 1.23: Port on NT 4.
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing	| 1.24: Port on Linux.
---------+--------------+------------------------------------------------------
15/12/00 | JL Schwing	| 1.25: Add more trace in VERY_VERBOSE mode.
---------+--------------+------------------------------------------------------
18/12/00 | JL Schwing	| 1.26: Add new exit status EXIT_INIT.
---------+--------------+------------------------------------------------------
05/01/01 | JL Schwing	| 1.27: Implement "-e randombinddn" and associated
			|   "-e randombinddnlow/high"
---------+--------------+------------------------------------------------------
08/01/01 | JL Schwing	| 1.28: Implement "-e scalab01".
---------+--------------+------------------------------------------------------
12/01/01 | JL Schwing	| 1.29: Include scalab01.h
---------+--------------+------------------------------------------------------
05/03/01 | JL Schwing	| 1.30: Bug fix - crash SIGSEGV if no binDN provided.
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.31: Implement "-e commoncounter"
			| Add new function incrementCommonCounter().
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.32: Implement "-e dontsleeponserverdown".
---------+--------------+------------------------------------------------------
15/03/01 | JL Schwing	| 1.33: Implement "-e randomattrlist=name:name:name"
			| Add new function selectRandomAttrList().
---------+--------------+------------------------------------------------------
19/03/01 | JL Schwing	| 1.34: Implement "-e genldif=filename"
---------+--------------+------------------------------------------------------
23/03/01 | JL Schwing	| 1.35: Implements "-e rdn=value".
---------+--------------+------------------------------------------------------
28/03/01 | JL Schwing	| 1.36: Support -e commoncounter with -e rdn/object
			| Add new function incrementCommonCounterObject().
---------+--------------+------------------------------------------------------
02/04/01 | JL Schwing	| 1.37: Bug fix : large files support for -e genldif.
---------+--------------+------------------------------------------------------
11/04/01 | JL Schwing	| 1.38: Implement [INCRFROMFILE<NOLOOP>(myfile)]
---------+--------------+------------------------------------------------------
03/05/01 | JL Schwing	| 1.39: Implement -e randombinddnfromfile=filename.
---------+--------------+------------------------------------------------------
04/05/01 | JL Schwing	| 1.40: Implement -e bindonly.
---------+--------------+------------------------------------------------------
*/

#include <stdio.h>	/* printf(), etc... */
#include <string.h>	/* strerror(), etc... */
#include <stdlib.h>	/* exit(), etc... */
#include <ctype.h>	/* isascii(), etc... */
#include <errno.h>	/* errno, etc... */			/*JLS 06-03-00*/
#include <lber.h>	/* ldap C-API BER declarations */
#include <ldap.h>	/* ldap C-API declarations */
#ifndef _WIN32							/*JLS 29-11-00*/
#include <unistd.h>	/* close(), etc... */
#include <pthread.h>	/* pthreads(), etc... */
#include <signal.h>	/* sigfillset(), etc... */
#endif								/*JLS 29-11-00*/

#include "port.h"	/* Portability definitions */		/*JLS 29-11-00*/
#include "ldclt.h"	/* This tool's include file */
#include "utils.h"	/* Utilities functions */		/*JLS 14-11-00*/
#include "scalab01.h"	/* Scalab01 specific */			/*JLS 12-01-01*/














						/* New */	/*JLS 15-03-01*/
/* ****************************************************************************
	FUNCTION :	selectRandomAttrList
	PURPOSE :	Select a random attr list.
	INPUT :		tttctx	= this thread context
	OUTPUT :	None.
	RETURN :	The random list.
	DESCRIPTION :
 *****************************************************************************/
char **
selectRandomAttrList (
	thread_context	*tttctx)
{
  tttctx->attrlist[0] = mctx.attrlist[rndlim(0,mctx.attrlistNb-1)];
  return (tttctx->attrlist);
}





/* ****************************************************************************
	FUNCTION :	randomString
	PURPOSE :	Return a random string, of length nbDigits.
			The string is returned in tttctx->buf2.
	INPUT :		tttctx	 = thread context.
			nbDigits = number of digits required.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
randomString (
	thread_context	*tttctx,
	int		 nbDigits)
{
  rndstr (tttctx->buf2, nbDigits);				/*JLS 14-11-00*/
  return (0);
}







						/* New */	/*JLS 28-03-01*/
/* ****************************************************************************
	FUNCTION :	incrementCommonCounterObject
	PURPOSE :	Purpose of the fct
	INPUT :		tttctx	= thread context
			field	= field to process
	OUTPUT :	None.
	RETURN :	-1 if error or end of loop (no_loop), new value else.
	DESCRIPTION :
 *****************************************************************************/
int
incrementCommonCounterObject (
	thread_context	*tttctx,
	vers_field	*field)
{
  int	 ret;	/* Return value */
  int	 val;	/* New value */

  /*
   * Get mutex
   */
  if ((ret = ldclt_mutex_lock (&(field->cnt_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_lock(field->cnt_mutex), error=%d (%s)\n",
			mctx.pid, tttctx->thrdId, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Compute next value
   */
  switch (field->how)
  {
    case HOW_INCR_FROM_FILE:
    case HOW_INCR_NB:
	if (field->cnt <= field->high)	/* Limit not reached */
	{
	  val = field->cnt;
	  field->cnt++;
	}
	else
	{
	  val        = field->low;
	  field->cnt = field->low + 1;
	}
	break;
    case HOW_INCR_FROM_FILE_NL:
    case HOW_INCR_NB_NOLOOP:
	if (field->cnt <= field->high)	/* Limit not reached */
	{
	  val = field->cnt;
	  field->cnt++;
	}
	else
	  val = -1;	/* Exit thread */
	break;
    default:
	printf ("ldclt[%d]: %s: Illegal how=%d in incrementCommonCounterObject()\n",
			mctx.pid, tttctx->thrdId, field->how);
	val = -1;
	break;
  }

  /*
   * Free mutex
   */
  if ((ret = ldclt_mutex_unlock (&(field->cnt_mutex))) != 0)
  {
    fprintf(stderr,"ldclt[%d]: %s: cannot mutex_unlock(field->cnt_mutex), error=%d (%s)\n",
			mctx.pid, tttctx->thrdId, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Maybe a message to print ?
   */
  if (val < 0)
    printf ("ldclt[%d]: %s: Hit top incrementeal value\n", mctx.pid, tttctx->thrdId);

  return (val);
}






						/* New */	/*JLS 14-03-01*/
/* ****************************************************************************
	FUNCTION :	incrementCommonCounter
	PURPOSE :	Purpose of the fct
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error or end of loop (no_loop), new value else.
	DESCRIPTION :
 *****************************************************************************/
int
incrementCommonCounter (
	thread_context	*tttctx)
{
  int	 ret;	/* Return value */
  int	 val;	/* New value */

  /*
   * Get mutex
   */
  if ((ret = ldclt_mutex_lock (&(mctx.lastVal_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: T%03d: cannot mutex_lock(lastVal_mutex), error=%d (%s)\n",
			mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Compute next value
   */
  if ((mctx.mode & NOLOOP) && (mctx.lastVal >= mctx.randomHigh))
    val = -1;
  else
  {
    mctx.lastVal += mctx.incr;
    if (mctx.lastVal > mctx.randomHigh)
    {
      if (mctx.mode & NOLOOP)
	val = -1;
      else
	mctx.lastVal -= (mctx.randomHigh-mctx.incr) + mctx.randomLow;
    }
    val = mctx.lastVal;
  }

  /*
   * Free mutex
   */
  if ((ret = ldclt_mutex_unlock (&(mctx.lastVal_mutex))) != 0)
  {
    fprintf(stderr,"ldclt[%d]: T%03d: cannot mutex_unlock(lastVal_mutex), error=%d (%s)\n",
			mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Maybe a message to print ?
   */
  if (val < 0)
    printf ("ldclt[%d]: T%03d: Hit top incrementeal value\n", mctx.pid, tttctx->thrdNum);

  return (val);
}





/* ****************************************************************************
	FUNCTION :	incrementNbOpers
	PURPOSE :	Increment the counters tttctx->nbOpers and
			tttctx->totOpers of the given thread.
	INPUT :		tttctx	= thread context.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
incrementNbOpers (
	thread_context	*tttctx)
{
  int	 ret;	/* Return value */

  /*
   * Get mutex
   */
  if ((ret = ldclt_mutex_lock (&(tttctx->nbOpers_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: T%03d: cannot mutex_lock(), error=%d (%s)\n", 
			mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Increment counter
   */
  tttctx->nbOpers++;

  /*
   * Free mutex
   */
  if ((ret = ldclt_mutex_unlock (&(tttctx->nbOpers_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: T%03d: cannot mutex_unlock(), error=%d (%s)\n", 
			mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Increment total and check if max value reached
   */
   tttctx->totOpers++;
   if(tttctx->totalReq > -1) {
     if (tttctx->totOpers >= tttctx->totalReq) {
       if (setThreadStatus(tttctx, MUST_SHUTDOWN) < 0) {
         tttctx->status = DEAD;   /* Force thread to die! */
       }
     }
   }

  return (0);
}







/* ****************************************************************************
	FUNCTION :	ignoreError
	PURPOSE :	Returns true or false depending on the given error
			should be ignored or not (option -I).
			We will sleep() if an error about server down is to be
			ignored.
	INPUT :		err	= error number
	OUTPUT :	None.
	RETURN :	1 if should be ignored, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
ignoreError (
	int	 err)
{
  int	 i;
  for (i=0 ; i<mctx.ignErrNb ; i++)
    if (mctx.ignErr[i] == err)
    {								/*JLS 14-03-01*/
      if ((!(mctx.mode & DONT_SLEEP_DOWN)) &&			/*JLS 14-03-01*/
	  ((err == LDAP_SERVER_DOWN) ||				/*JLS 14-03-01*/
	   (err == LDAP_CONNECT_ERROR)))			/*JLS 14-03-01*/
	ldclt_sleep (1);					/*JLS 14-03-01*/
      return (1);
    }								/*JLS 14-03-01*/
  return (0);
}








/* ****************************************************************************
	FUNCTION :	addErrorStat
	PURPOSE :	Add the given error number to the statistics.
	INPUT :		err	= error number
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
addErrorStat (
	int	 err)
{
  int	 ret;	/* Return value */

  /*
   * Get mutex
   */
  if ((ret = ldclt_mutex_lock (&(mctx.errors_mutex))) != 0)
  {
    fprintf (stderr, 
		"ldclt[%d]: Cannot mutex_lock(errors_mutex), error=%d (%s)\n", 
		mctx.pid, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Update the counters
   */
  if ((err <= 0) || (err >= MAX_ERROR_NB))
  {
    fprintf (stderr, "ldclt[%d]: Illegal error number %d\n", mctx.pid, err);
    fflush (stderr);
    mctx.errorsBad++;
  }
  else
    mctx.errors[err]++;

  /*
   * Release the mutex
   */
  if ((ret = ldclt_mutex_unlock (&(mctx.errors_mutex))) != 0)
  {
    fprintf (stderr,
		"ldclt[%d]: Cannot mutex_unlock(errors_mutex), error=%d (%s)\n",
		mctx.pid, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Maybe we should ignore this error ?
   */
  if (!(ignoreError (err)))
  {
    /*
     * Ok, we should not ignore this error...
     * Maybe the limit is reached ?
     */
    if ((err <= 0) || (err >= MAX_ERROR_NB))
    {
      if (mctx.errorsBad > mctx.maxErrors)
      {
	printf ("ldclt[%d]: Max error limit reached - exiting.\n", mctx.pid);
	(void) printGlobalStatistics();				/*JLS 25-08-00*/
	fflush (stdout);
	ldclt_sleep (5);
	ldcltExit (EXIT_MAX_ERRORS);				/*JLS 25-08-00*/
      }
    }
    else
      if (mctx.errors[err] > mctx.maxErrors)
      {
	printf ("ldclt[%d]: Max error limit reached - exiting.\n", mctx.pid);
	(void) printGlobalStatistics();				/*JLS 25-08-00*/
	fflush (stdout);
	ldclt_sleep (5);
	ldcltExit (EXIT_MAX_ERRORS);				/*JLS 25-08-00*/
      }
  }

  /*
   * Normal end
   */
  return (0);
}






/* ****************************************************************************
	FUNCTION :	msgIdAdd
	PURPOSE :	Add a new message id to the pending ones.
	INPUT :		tttctx	= thread's context.
			msgid	= message id.
			str	= free string.
			dn	= dn of the entry
			attribs	= attributes
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
msgIdAdd (
	thread_context	 *tttctx,
	int		  msgid,
	char		 *str,
	char		 *dn,
	LDAPMod		**attribs)
{
  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: msgIdAdd (%d, %s)\n", mctx.pid, tttctx->thrdNum, msgid, str);

  /*
   * Add the new cell
   */
  if (tttctx->firstMsgId == NULL)
  {
    tttctx->firstMsgId = (msgid_cell *) malloc (sizeof (msgid_cell));
    if (tttctx->firstMsgId == NULL)				/*JLS 06-03-00*/
    {								/*JLS 06-03-00*/
      printf ("ldclt[%d]: T%03d: cannot malloc(tttctx->firstMsgId), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, errno, strerror (errno));
      return (-1);						/*JLS 06-03-00*/
    }								/*JLS 06-03-00*/
    tttctx->lastMsgId  = tttctx->firstMsgId;
  }
  else
  {
    tttctx->lastMsgId->next = (msgid_cell *) malloc (sizeof (msgid_cell));
    if (tttctx->lastMsgId->next == NULL)			/*JLS 06-03-00*/
    {								/*JLS 06-03-00*/
      printf ("ldclt[%d]: T%03d: cannot malloc(tttctx->lastMsgId->next), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, errno, strerror (errno));
      return (-1);						/*JLS 06-03-00*/
    }								/*JLS 06-03-00*/
    tttctx->lastMsgId       = tttctx->lastMsgId->next;
  }

  /*
   * Memorize the information
   */
  tttctx->lastMsgId->next  = NULL;
  tttctx->lastMsgId->msgid = msgid;
  strncpy (tttctx->lastMsgId->str, str, sizeof(tttctx->lastMsgId->str));
  tttctx->lastMsgId->str[sizeof(tttctx->lastMsgId->str)-1] = '\0';
  strncpy (tttctx->lastMsgId->dn, dn, sizeof(tttctx->lastMsgId->dn));
  tttctx->lastMsgId->dn[sizeof(tttctx->lastMsgId->dn)-1] = '\0';
  tttctx->lastMsgId->attribs = attribs;

  return (0);
}







/* ****************************************************************************
	FUNCTION :	msgIdAttribs
	PURPOSE :	Found the requested message id in the pending list.
	INPUT :		tttctx	= thread's context
			msgid	= message id
	OUTPUT :	None
	RETURN :	The associated attributes, or NULL.
	DESCRIPTION :
 *****************************************************************************/
LDAPMod **
msgIdAttribs (
	thread_context	*tttctx,
	int		 msgid)
{
  msgid_cell	*pt;

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: msgIdAttribs (%d)\n", mctx.pid, tttctx->thrdNum, msgid);

  for (pt = tttctx->firstMsgId ; pt != NULL ; pt = pt->next)
    if (pt->msgid == msgid)
      return (pt->attribs); 

  return (NULL);
}








/* ****************************************************************************
	FUNCTION :	msgIdDN
	PURPOSE :	Found the requested message id in the pending list.
	INPUT :		tttctx	= thread's context
			msgid	= message id
	OUTPUT :	None
	RETURN :	The associated DN, or NULL.
	DESCRIPTION :
 *****************************************************************************/
char *
msgIdDN (
	thread_context	*tttctx,
	int		 msgid)
{
  msgid_cell	*pt;

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: msgIdDN (%d)\n", mctx.pid, tttctx->thrdNum, msgid);

  for (pt = tttctx->firstMsgId ; pt != NULL ; pt = pt->next)
    if (pt->msgid == msgid)
      return (pt->dn); 

  return (NULL);
}








/* ****************************************************************************
	FUNCTION :	msgIdStr
	PURPOSE :	Found the requested message id in the pending list.
	INPUT :		tttctx	= thread's context
			msgid	= message id
	OUTPUT :	None
	RETURN :	The associated str, or an error message string.
	DESCRIPTION :
 *****************************************************************************/
char *
msgIdStr (
	thread_context	*tttctx,
	int		 msgid)
{
  msgid_cell	*pt;

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: msgIdStr (%d)\n", mctx.pid, tttctx->thrdNum, msgid);

  for (pt = tttctx->firstMsgId ; pt != NULL ; pt = pt->next)
    if (pt->msgid == msgid)
      return (pt->str); 

  return ("Error: msgid not found");
}







/* ****************************************************************************
	FUNCTION :	msgIdDel
	PURPOSE :	Delete a message id from the pending ones.
	INPUT :		tttctx	= thread's context
			msgid	= message id.
			freeAttr= true or false depending on freing the attribs
	OUTPUT :	None.
	RETURN :	-1 if not found, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int 
msgIdDel (
	thread_context	*tttctx,
	int		 msgid,
	int		 freeAttr)
{
  msgid_cell	*pt;		/* For the loop */
  msgid_cell	*ptToFree;	/* The cell to free */

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: msgIdDel (%d)\n", mctx.pid, tttctx->thrdNum, msgid);

  /*
   * Make sure there is a list !
   */
  if (tttctx->firstMsgId != NULL)
  {
    /*
     * Maybe it is the first one ?
     */
    if (tttctx->firstMsgId->msgid == msgid)
    {
      ptToFree           = tttctx->firstMsgId;
      tttctx->firstMsgId = tttctx->firstMsgId->next;
      if (tttctx->firstMsgId == NULL)
	tttctx->lastMsgId = NULL;
      free (ptToFree);
      return (0);
    }

    /*
     * Let's go through the whole list
     */
    for (pt = tttctx->firstMsgId ; pt->next != NULL ; pt = pt->next)
      if (pt->next->msgid == msgid)
      {
	/*
	 * Be carrefull if it is the last element of the list
	 */
	if (pt->next->next == NULL)
	  tttctx->lastMsgId = pt;
	ptToFree = pt->next;
	pt->next = ptToFree->next;
	if (freeAttr)
	  if (freeAttrib (ptToFree->attribs) < 0)
	    return (-1);

	/*
	 * Free the pointer itself
	 */
	free (ptToFree);
	return (0);
      }
  }

  /*
   * Not found
   */
  printf ("ldclt[%d]: T%03d: message %d not found.\n", mctx.pid, tttctx->thrdNum, msgid);
  fflush (stdout);
  return (-1);
}








					/* New function */	/*JLS 17-11-00*/
/* ****************************************************************************
	FUNCTION :	getThreadStatus
	PURPOSE :	Get the value of a given thread's status.
	INPUT :		tttctx	= thread context
	OUTPUT :	status	= the thread's status
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
getThreadStatus (
	thread_context	*tttctx,
	int		*status)
{
  int	 ret;	/* Return code */

  if ((ret = ldclt_mutex_lock (&(tttctx->status_mutex))) != 0)
  {
    fprintf (stderr, 
		"ldclt[%d]: Cannot mutex_lock(T%03d), error=%d (%s)\n", 
		mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fprintf (stderr, "ldclt[%d]: Problem in getThreadStatus()\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }
  *status = tttctx->status;
  if ((ret = ldclt_mutex_unlock (&(tttctx->status_mutex))) != 0)
  {
    fprintf (stderr,
		"ldclt[%d]: Cannot mutex_unlock(T%03d), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fprintf (stderr, "ldclt[%d]: Problem in getThreadStatus()\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }

  return (0);
}





					/* New function */	/*JLS 17-11-00*/
/* ****************************************************************************
	FUNCTION :	setThreadStatus
	PURPOSE :	Set the value of a given thread's status.
	INPUT :		tttctx	= thread context
			status	= new status
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
setThreadStatus (
	thread_context	*tttctx,
	int		 status)
{
  int	 ret;	/* Return code */

  if ((ret = ldclt_mutex_lock (&(tttctx->status_mutex))) != 0)
  {
    fprintf (stderr, 
		"ldclt[%d]: Cannot mutex_lock(T%03d), error=%d (%s)\n", 
		mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fprintf (stderr, "ldclt[%d]: Problem in setThreadStatus()\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }
  tttctx->status = status;
  if ((ret = ldclt_mutex_unlock (&(tttctx->status_mutex))) != 0)
  {
    fprintf (stderr,
		"ldclt[%d]: Cannot mutex_unlock(T%03d), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fprintf (stderr, "ldclt[%d]: Problem in setThreadStatus()\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }

  return (0);
}













/* ****************************************************************************
	FUNCTION :	threadMain
	PURPOSE :	This function is the main function of the client threads
			part of this tool. 
	INPUT :		arg	= this thread's thread_context
	OUTPUT :	None.
	RETURN :	None.
	DESCRIPTION :
 *****************************************************************************/
void *
threadMain (
	void	*arg)
{
  thread_context	*tttctx;	/* This thread's context */
  int			 go = 1;	/* Thread must continue */
  int			 status;	/* Thread's status */	/*JLS 17-11-00*/

  /*
   * Initialization
   */
  tttctx = (thread_context *) arg;
  if (setThreadStatus (tttctx, CREATED) < 0)			/*JLS 17-11-00*/
  {								/*JLS 17-11-00*/
    tttctx->status = DEAD;					/*JLS 17-11-00*/
    return NULL;							/*JLS 17-11-00*/
  }								/*JLS 17-11-00*/
  tttctx->asyncHit   = 0;
  tttctx->binded     = 0;
  tttctx->fd         = -1;
  tttctx->lastVal    = mctx.randomLow-1;
  tttctx->ldapCtx    = NULL;
  tttctx->matcheddnp = NULL;					/*JLS 15-12-00*/
  tttctx->nbOpers    = 0;
  tttctx->totOpers   = 0;
  tttctx->pendingNb  = 0;
  tttctx->firstMsgId = NULL;
  tttctx->lastMsgId  = NULL;

  /*
   * Don't forget the buffers !!
   * This should save time while redoing random values
   */
  if ((mctx.mode & NEED_FILTER) || (mctx.mod2 & (M2_GENLDIF|M2_NEED_FILTER)))	/*JLS 19-03-01*/
  {
    if (mctx.mod2 & M2_RDN_VALUE)				/*JLS 23-03-01*/
      tttctx->bufFilter = (char *) malloc (MAX_FILTER);		/*JLS 23-03-01*/
    else							/*JLS 23-03-01*/
    {								/*JLS 23-03-01*/
      /*
       * Variable filter ?
       */
      tttctx->bufFilter = (char *) malloc (strlen (mctx.filter) + 1);
      if (tttctx->bufFilter == NULL)				/*JLS 06-03-00*/
      {								/*JLS 06-03-00*/
	printf ("ldclt[%d]: %s: cannot malloc(tttctx->bufFilter), error=%d (%s)\n",
		mctx.pid, tttctx->thrdId, errno, strerror (errno));
	ldcltExit (EXIT_INIT);					/*JLS 18-12-00*/
      }								/*JLS 06-03-00*/
      if (!(mctx.mode & (RANDOM | INCREMENTAL)))
	strcpy (tttctx->bufFilter, mctx.filter);
      else
      {
	tttctx->startRandom = strlen (mctx.randomHead);
	strcpy (tttctx->bufFilter, mctx.randomHead);
	strcpy (&(tttctx->bufFilter[tttctx->startRandom+mctx.randomNbDigit]),
			mctx.randomTail);
	if (mctx.mode & VERY_VERBOSE)
	{
	  printf ("ldclt[%d]: %s: startRandom = %d\n", 
		mctx.pid, tttctx->thrdId, tttctx->startRandom);
	  printf ("ldclt[%d]: %s: randomHead = \"%s\", randomTail = \"%s\"\n",
		mctx.pid, tttctx->thrdId, mctx.randomHead, mctx.randomTail);
	}
      }
    }								/*JLS 23-03-01*/

    /*
     * Variable base DN ?
     */
    tttctx->bufBaseDN = (char *) malloc (strlen (mctx.baseDN) + 1);
    if (tttctx->bufBaseDN == NULL)				/*JLS 06-03-00*/
    {								/*JLS 06-03-00*/
      printf ("ldclt[%d]: T%03d: cannot malloc(tttctx->bufBaseDN), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, errno, strerror (errno));
      ldcltExit (EXIT_INIT);					/*JLS 18-12-00*/
    }								/*JLS 06-03-00*/
    if (!(mctx.mode & RANDOM_BASE))
      strcpy (tttctx->bufBaseDN, mctx.baseDN);
    else
    {
      tttctx->startBaseDN = strlen (mctx.baseDNHead);
      strcpy (tttctx->bufBaseDN, mctx.baseDNHead);
      strcpy (&(tttctx->bufBaseDN[tttctx->startBaseDN+mctx.baseDNNbDigit]),
			mctx.baseDNTail);
    }

    /*
     * Variable bind DN ?
     * Do not forget the random bind password below that is activated
     * at the same time as the random bind DN.
     */
    if (mctx.bindDN != NULL)					/*JLS 05-03-01*/
    {								/*JLS 05-03-01*/
      tttctx->bufBindDN = (char *) malloc (strlen (mctx.bindDN) + 1);
      if (tttctx->bufBindDN == NULL)
      {
	printf ("ldclt[%d]: T%03d: cannot malloc(tttctx->bufBindDN), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, errno, strerror (errno));
	ldcltExit (EXIT_INIT);
      }
      if (!(mctx.mode & RANDOM_BINDDN))
	strcpy (tttctx->bufBindDN, mctx.bindDN);
      else
      {
	tttctx->startBindDN = strlen (mctx.bindDNHead);
	strcpy (tttctx->bufBindDN, mctx.bindDNHead);
	strcpy (&(tttctx->bufBindDN[tttctx->startBindDN+mctx.bindDNNbDigit]),
			mctx.bindDNTail);
      }
    }								/*JLS 05-03-01*/

    /*
     * Variable bind password ?
     * Remember that the random bind password feature is activated
     * by the same option as the random bind DN, but has here its own
     * code section for the ease of coding.
     */
    if (mctx.passwd != NULL)					/*JLS 05-03-01*/
    {								/*JLS 05-03-01*/
      tttctx->bufPasswd = (char *) malloc (strlen (mctx.passwd) + 1);
      if (tttctx->bufPasswd == NULL)
      {
	printf ("ldclt[%d]: T%03d: cannot malloc(tttctx->bufPasswd), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, errno, strerror (errno));
	ldcltExit (EXIT_INIT);
      }
      if (!(mctx.mode & RANDOM_BINDDN))
	strcpy (tttctx->bufPasswd, mctx.passwd);
      else
      {
	tttctx->startPasswd = strlen (mctx.passwdHead);
	strcpy (tttctx->bufPasswd, mctx.passwdHead);
	strcpy (&(tttctx->bufPasswd[tttctx->startPasswd+mctx.passwdNbDigit]),
			mctx.passwdTail);
      }
    }
  }								/*JLS 05-03-01*/

  /*
   * Bind DN from a file ?
   * The trick (mctx.passwd = "foo bar"; ) is needed to
   * simplify the code, because in many places we check
   * if mctx.passwd exist before sending password.
   */
  if (mctx.mod2 & M2_RNDBINDFILE)				/*JLS 03-05-01*/
  {								/*JLS 03-05-01*/
    tttctx->bufBindDN = (char *) malloc (MAX_DN_LENGTH);	/*JLS 03-05-01*/
    tttctx->bufPasswd = (char *) malloc (MAX_DN_LENGTH);	/*JLS 03-05-01*/
    mctx.passwd = "foo bar"; /* trick... */			/*JLS 03-05-01*/
  }								/*JLS 03-05-01*/

    /*
     * Variable Authid ?
     */
    if (mctx.sasl_authid != NULL)
    {
      tttctx->bufSaslAuthid = (char *) malloc (strlen (mctx.sasl_authid) + 1);
      if (tttctx->bufSaslAuthid == NULL)
      {
	printf ("ldclt[%d]: T%03d: cannot malloc(tttctx->bufSaslAuthid), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, errno, strerror (errno));
	ldcltExit (EXIT_INIT);
      }
      if (!(mctx.mod2 & M2_RANDOM_SASLAUTHID))
	strcpy (tttctx->bufSaslAuthid, mctx.sasl_authid);
      else
      {
	tttctx->startSaslAuthid = strlen (mctx.sasl_authid_head);
	strcpy (tttctx->bufSaslAuthid, mctx.sasl_authid_head);
	strcpy (&(tttctx->bufSaslAuthid[tttctx->startSaslAuthid+mctx.sasl_authid_nbdigit]),
			mctx.sasl_authid_tail);
      }
    }

  /*
   * Initiates the attribute replace buffers
   */
  if (mctx.mode & ATTR_REPLACE)			/* New */	/*JLS 21-11-00*/
  {
    tttctx->bufAttrpl = (char *) malloc (strlen (mctx.attrpl) + 1);
    if (tttctx->bufAttrpl == NULL)
    {
      printf ("ldclt[%d]: T%03d: cannot malloc(tttctx->bufAttrpl), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, errno, strerror (errno));
      ldcltExit (EXIT_INIT);					/*JLS 18-12-00*/
    }
    tttctx->startAttrpl = strlen (mctx.attrplHead);
    strcpy (tttctx->bufAttrpl, mctx.attrplHead);
    strcpy (&(tttctx->bufAttrpl[tttctx->startAttrpl+mctx.attrplNbDigit]),
			mctx.attrplTail);
  }


  /*
   * Initiates the attribute replace buffers attrplName
   */
  if ( mctx.mod2 & M2_ATTR_REPLACE_FILE )
  {
    /* bufAttrpl should point to the same memory location that mctx.attrplFileContent points to */
    tttctx->bufAttrpl = mctx.attrplFileContent;
    if (tttctx->bufAttrpl == NULL)
    {
      printf ("ldclt[%d]: T%03d: cannot malloc(tttctx->bufAttrpl), error=%d (%s), can we read file [%s]\n", mctx.pid, tttctx->thrdNum, errno, strerror (errno), mctx.attrplFile);
      ldcltExit (EXIT_INIT);
    }
  }


  /*
   * We are ready to go !
   */
  status = RUNNING;						/*JLS 17-11-00*/
  if (setThreadStatus (tttctx, RUNNING) < 0)			/*JLS 17-11-00*/
    status = DEAD;						/*JLS 17-11-00*/

  /*
   * Let's go !
   */
  while (go && (status != DEAD) && (status != MUST_SHUTDOWN))	/*JLS 17-11-00*/
  {
    if (mctx.waitSec > 0)
    {								/*JLS 17-11-00*/
      ldclt_sleep (mctx.waitSec);

      /*
       * Maybe we should shutdown ?
       */
      if (getThreadStatus (tttctx, &status) < 0)		/*JLS 17-11-00*/
	break;							/*JLS 17-11-00*/
      if (status == MUST_SHUTDOWN)				/*JLS 17-11-00*/
	break;							/*JLS 17-11-00*/
    }								/*JLS 17-11-00*/

    /*
     * Do a LDAP request
     */
    if (tttctx->mode & ADD_ENTRIES)
      if (doAddEntry (tttctx) < 0)
      {
	go = 0;
	continue;
      }
    if (tttctx->mode & ATTR_REPLACE)				/*JLS 21-11-00*/
      if (doAttrReplace (tttctx) < 0)				/*JLS 21-11-00*/
      {								/*JLS 21-11-00*/
	go = 0;							/*JLS 21-11-00*/
	continue;						/*JLS 21-11-00*/
      }								/*JLS 21-11-00*/

    if (mctx.mod2 & M2_ATTR_REPLACE_FILE )
      if (doAttrFileReplace (tttctx) < 0)
      {	
	go = 0;	
	continue;
      }	

    if (tttctx->mode & DELETE_ENTRIES)
      if (doDeleteEntry (tttctx) < 0)
      {
	go = 0;
	continue;
      }
    if (mctx.mod2 & M2_BINDONLY)				/*JLS 04-05-01*/
      if (doBindOnly (tttctx) < 0)				/*JLS 04-05-01*/
      {								/*JLS 04-05-01*/
	go = 0;							/*JLS 04-05-01*/
	continue;						/*JLS 04-05-01*/
      }								/*JLS 04-05-01*/
    if (tttctx->mode & EXACT_SEARCH)
      if (doExactSearch (tttctx) < 0)
      {
	go = 0;
	continue;
      }
    if (tttctx->mode & RENAME_ENTRIES)
      if (doRename (tttctx) < 0)
      {
	go = 0;
	continue;
      }

    /*
     * Maybe a specific scenario ?
     */
    if (tttctx->mode & SCALAB01)				/*JLS 08-01-01*/
      if (doScalab01 (tttctx) < 0)				/*JLS 08-01-01*/
      {								/*JLS 08-01-01*/
	go = 0;							/*JLS 08-01-01*/
	continue;						/*JLS 08-01-01*/
      }								/*JLS 08-01-01*/

    /*
     * Maybe genldif mode ?
     */
    if (mctx.mod2 & M2_GENLDIF)					/*JLS 19-03-01*/
      if (doGenldif (tttctx) < 0)				/*JLS 19-03-01*/
      {								/*JLS 19-03-01*/
	ldclt_flush_genldif();					/*JLS 02-04-01*/
	go = 0;							/*JLS 19-03-01*/
	continue;						/*JLS 19-03-01*/
      }								/*JLS 19-03-01*/

    if (mctx.mod2 & M2_ABANDON) 
    {
      if (doAbandon (tttctx) < 0)
      {
        go = 0;
        continue;
      }
    }

    /*
     * Check the thread's status
     */
    if (getThreadStatus (tttctx, &status) < 0)			/*JLS 17-11-00*/
      break;							/*JLS 17-11-00*/
  }

  /*
   * End of thread
   */
  /* [156984] once setting "DEAD", nothing should be done in the context */
  /* moved the dead message above setThreadStatus(DEAD) */
  printf ("ldclt[%d]: T%03d: thread is dead.\n", mctx.pid, tttctx->thrdNum);
  fflush (stdout);
  if (setThreadStatus (tttctx, DEAD) < 0)			/*JLS 17-11-00*/
  {								/*JLS 17-11-00*/
    ldclt_sleep (1);						/*JLS 17-11-00*/
    tttctx->status = DEAD; /* Force it !!! */			/*JLS 17-11-00*/
  }								/*JLS 17-11-00*/
  return (arg);
}


/* End of file */
