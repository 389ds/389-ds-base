#ident "ldclt @(#)ldapfct.c	1.68 01/05/04"

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
	FILE :		ldapfct.c
	AUTHOR :	Jean-Luc SCHWING
	VERSION :       1.0
	DATE :		04 December 1998
	DESCRIPTION :
			This file contains the ldap part of this tool.
	LOCAL :		None.
	HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
04/12/98 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
09/12/98 | JL Schwing	| 1.2 : Forget to free ldap returned data.
---------+--------------+------------------------------------------------------
10/12/98 | JL Schwing	| 1.3 : Add nb of errors statistics.
---------+--------------+------------------------------------------------------
10/12/98 | JL Schwing	| 1.4 : Implement asynchronous mode.
---------+--------------+------------------------------------------------------
11/12/98 | JL Schwing	| 1.5 : fflush(stdout) after each printf.
---------+--------------+------------------------------------------------------
14/12/98 | JL Schwing	| 1.6 : Implement "-e close".
			| New function my_ldap_err2string() to prevent crashes.
---------+--------------+------------------------------------------------------
16/12/98 | JL Schwing	| 1.7 : Implement "-e add" and "-e delete".
			| Create separate functions for connection operations.
---------+--------------+------------------------------------------------------
23/12/98 | JL Schwing	| 1.8 : Factorise the doXxxx() functions.
			| Fix SIGSEGV in ldap_parse_result().
			| Implement result decoding in doAddEntry().
---------+--------------+------------------------------------------------------
24/12/98 | JL Schwing	| 1.9 : Bug fix - should support no message in async.
			| Bug fix - forget to check mctx.asyncMax !!!
			| Modify the getPending() algorythm to empty the queue
			|   (if possible).
---------+--------------+------------------------------------------------------
29/12/98 | JL Schwing	| 1.10: Implement -Q.
			| Bug fix mode -q when asynchronous.
---------+--------------+------------------------------------------------------
11/01/99 | JL Schwing	| 1.11: Implement "-e emailPerson".
			| Bug fix - bad building of rdn in buildNewEntry().
---------+--------------+------------------------------------------------------
13/01/99 | JL Schwing	| 1.12: Implement "-e string".
---------+--------------+------------------------------------------------------
14/01/99 | JL Schwing	| 1.13: Implement "-s <scope>".
---------+--------------+------------------------------------------------------
15/01/99 | JL Schwing	| 1.14: Create automatically the missing nodes.
---------+--------------+------------------------------------------------------
18/01/99 | JL Schwing	| 1.15: Implements "-e randombase".
---------+--------------+------------------------------------------------------
18/01/99 | JL Schwing	| 1.16: Implements "-e v2".
---------+--------------+------------------------------------------------------
20/01/99 | JL Schwing	| 1.17: Bug fix - should'nt fail in createMissingNodes()
			|   if error 68 (Already exists).
			| Increment counter for intermediate node(s) too.
---------+--------------+------------------------------------------------------
23/01/99 | JL Schwing	| 1.18: Improve traces.
			| Bug fix - remove leading spaces from rdn.
---------+--------------+------------------------------------------------------
26/01/99 | JL Schwing	| 1.19: Implement "-e noloop".
---------+--------------+------------------------------------------------------
01/02/99 | JL Schwing	| 1.20: Create *all* the nodes in createMissingNodes()
---------+--------------+------------------------------------------------------
03/02/99 | JL Schwing	| 1.21: Create the leaf entry after createMissingNodes()
---------+--------------+------------------------------------------------------
26/02/99 | JL Schwing	| 1.22: Detect "\," in createMissingNodes().
---------+--------------+------------------------------------------------------
04/05/99 | JL Schwing	| 1.23: Add call to opAdd().
---------+--------------+------------------------------------------------------
04/05/99 | JL Schwing	| 1.24: Forget some calls to opAdd().
---------+--------------+------------------------------------------------------
04/05/99 | JL Schwing	| 1.25: Too many calls to opAdd() !!!
---------+--------------+------------------------------------------------------
19/05/99 | JL Schwing	| 1.27: Implements doRename().
			| Do not print messages about intermediate nodes created
			|   in quiet mode.
			| MOdify getPending()to support rename operations.
---------+--------------+------------------------------------------------------
06/03/00 | JL Schwing	| 1.28: Test malloc() return value.
---------+--------------+------------------------------------------------------
06/03/00 | A. Hornik	| 1.29: Bug fix - SEGV if no passwd provided.
---------+--------------+------------------------------------------------------
03/08/00 | JL Schwing	| 1.30: Improve errors decoding. This improvement
			|   is to retrieve the additional error string that
			|   is returned by the server.  This is implemented for
			|   the asynchronous operations and for the synchronous
			|   search only, because for the other synchronous ops
			|   we should use ldap_get_lderrno() that is not
			|   implement in Solaris's libldap.
			| Add new function printErrorFromLdap().
---------+--------------+------------------------------------------------------
03/08/00 | JL Schwing	| 1.31: Fix SIGSEGV in printErrorFromLdap().
---------+--------------+------------------------------------------------------
11/08/00 | JL Schwing	| 1.32: Improve error decoding.
---------+--------------+------------------------------------------------------
18/08/00 | JL Schwing	| 1.33: Print begin and end dates.
---------+--------------+------------------------------------------------------
25/08/00 | JL Schwing	| 1.34: Implement consistent exit status...
---------+--------------+------------------------------------------------------
11/10/00 | B Kolics     | 1.35: Added SSL connection initialization to
	 |              |       connectToServer, createMissingNodes
---------+--------------+------------------------------------------------------
26/10/00 | B Kolics     | 1.36: Moved SSL client initialization to basicInit
-------------------------------------------------------------------------------
07/11/00 | JL Schwing	| 1.37: Implements dynamic load of ssl-related
			|   functions.
---------+--------------+------------------------------------------------------
07/11/00 | JL Schwing	| 1.38: Implement "-e inetOrgPerson".
			| Add new error message in createMissingNodes().
---------+--------------+------------------------------------------------------
13/11/00 | JL Schwing	| 1.39: Add new options "-e randombaselow and ...high"
---------+--------------+------------------------------------------------------
14/11/00 | JL Schwing	| 1.40: Will now use utils.c functions.
---------+--------------+------------------------------------------------------
14/11/00 | JL Schwing	| 1.41 : Port on AIX.
---------+--------------+------------------------------------------------------
21/11/00 | JL Schwing	| 1.42: Implement "-e attreplace=name:mask"
			| Add new function buildNewModAttrib().
---------+--------------+------------------------------------------------------
24/11/00 | B Kolics	| 1.43: Added SSL client authentication
---------+--------------+------------------------------------------------------
29/11/00 | JL Schwing	| 1.44: Port on NT 4.
---------+--------------+------------------------------------------------------
01/12/00 | JL Schwing	| 1.45: Port on Linux.
---------+--------------+------------------------------------------------------
14/12/00 | JL Schwing	| 1.46: Add more trace in VERY_VERBOSE mode.
			| Fix some trace messages.
			| Add new function dnFromMessage().
---------+--------------+------------------------------------------------------
15/12/00 | JL Schwing	| 1.47: Implement "-e counteach".
			| Implement "-e withnewparent" - cf bug Scopus 526148
---------+--------------+------------------------------------------------------
18/12/00 | JL Schwing	| 1.48: Add new exit status EXIT_RESSOURCE.
---------+--------------+------------------------------------------------------
18/12/00 | JL Schwing	| 1.49: Bug fix when -e rename,counteach
---------+--------------+------------------------------------------------------
18/12/00 | JL Schwing	| 1.50: Bug fix - support errors the user wants to 
			|   ignore in connectToServer().
			| Also a bug fix in tttctx->exitStatus management.
---------+--------------+------------------------------------------------------
03/01/01 | JL Schwing	| 1.51: Implement "-e attrsonly=value".
---------+--------------+------------------------------------------------------
05/01/01 | JL Schwing	| 1.52: Implement "-e randombinddn" and associated
			|   "-e randombinddnlow/high"
			| Add new function buildNewBindDN().
---------+--------------+------------------------------------------------------
05/03/01 | JL Schwing	| 1.53: Fix the "anonymous" mode.
---------+--------------+------------------------------------------------------
08/03/01 | JL Schwing	| 1.54: Change referrals handling.
			| Add new functions refRebindProc() and referralSetup().
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.55: Implement "-e commoncounter"
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.56: Lint cleanup.
			| Port on _WIN32.
---------+--------------+------------------------------------------------------
15/03/01 | JL Schwing	| 1.57: Implement "-e attrlist=name:name:name"
			| Implement "-e randomattrlist=name:name:name"
---------+--------------+------------------------------------------------------
19/03/01 | JL Schwing	| 1.58: Implement "-e object=filename".
			| Implement "-e genldif=filename".
			| Add new functions buildVersatileObject() doGenldif()
---------+--------------+------------------------------------------------------
21/03/01 | JL Schwing	| 1.59: Implements variables in "-e object=filename"
---------+--------------+------------------------------------------------------
23/03/01 | JL Schwing	| 1.60: Implements data file list support in variants.
			| Implements "-e rdn=value".
---------+--------------+------------------------------------------------------
28/03/01 | JL Schwing	| 1.61: Fix traces.
---------+--------------+------------------------------------------------------
28/03/01 | JL Schwing	| 1.62: Support -e commoncounter with -e rdn/object
			| Increment counters in doGenldif().
			| Perfs tunning in doGenldif().
---------+--------------+------------------------------------------------------
02/04/01 | JL Schwing	| 1.63: Bug fix : large files support for -e genldif.
			| Add new function ldclt_write_genldif().
---------+--------------+------------------------------------------------------
11/04/01 | JL Schwing	| 1.64: Implement [INCRFROMFILE<NOLOOP>(myfile)] 
---------+--------------+------------------------------------------------------
11/04/01 | JL Schwing	| 1.65: Bug fix - should not crash if variable not set.
---------+--------------+------------------------------------------------------
03/05/01 | JL Schwing	| 1.66: Implement -e randombinddnfromfile=filename.
			| Add new function getBindAndPasswdFromFile().
---------+--------------+------------------------------------------------------
04/05/01 | JL Schwing	| 1.67: Implement -e bindonly.
			| Add new function doBindOnly().
---------+--------------+------------------------------------------------------
04/05/01 | JL Schwing	| 1.68: Lint cleanup.
---------+--------------+------------------------------------------------------
*/


#include <stdio.h>	/* printf(), etc... */
#include <string.h>	/* strcpy(), etc... */
#include <errno.h>	/* errno, etc... */
#include <stdlib.h>	/* malloc(), etc... */
#include <lber.h>	/* ldap C-API BER declarations */
#include <ldap.h>	/* ldap C-API declarations */
#ifdef LDAP_H_FROM_QA_WKA
#include <proto-ldap.h>	/* ldap C-API prototypes */
#endif
#ifndef _WIN32							/*JLS 29-11-00*/
#include <unistd.h>	/* close(), etc... */
#include <pthread.h>	/* pthreads(), etc... */
#endif								/*JLS 29-11-00*/

#include "port.h"	/* Portability definitions */		/*JLS 29-11-00*/
#include "ldclt.h"	/* This tool's include file */
#include "utils.h"	/* Utilities functions */		/*JLS 14-11-00*/

#include <sasl.h>
#include "ldaptool-sasl.h"
#if !defined(USE_OPENLDAP)
#include <ldap_ssl.h>	/* ldapssl_init(), etc... */
#endif

#include <prprf.h>

#define LDCLT_DEREF_ATTR "secretary"
int ldclt_create_deref_control( LDAP *ld, char *derefAttr, char **attrs, LDAPControl **ctrlp );

#if !defined(USE_OPENLDAP)
int ldclt_build_control( char *oid, BerElement *ber, int freeber, char iscritical, LDAPControl **ctrlp );
#endif
int ldclt_alloc_ber( LDAP *ld, BerElement **berp );


/* ****************************************************************************
	FUNCTION :	my_ldap_err2string
	PURPOSE :	This function is targetted to encapsulate the standard
			function ldap_err2string(), that sometimes returns
			a NULL pointer and thus crashes the appicaliton :-(
	INPUT :		err	= error to decode
	OUTPUT :	None.
	RETURN :	A string that describes the error.
	DESCRIPTION :
 *****************************************************************************/
char *
my_ldap_err2string (
	int	 err)
{
  if (ldap_err2string (err) == NULL)
    return ("ldap_err2string() returns a NULL pointer !!!");
  else
    return (ldap_err2string(err));
}







					/* New function */	/*JLS 14-12-00*/
/* ****************************************************************************
	FUNCTION :	dnFromMessage
	PURPOSE :	Extract the matcheddnp value from an LDAP (error)
			message.
	INPUT :		tttctx	= thread context.
			res	= result to parse
	OUTPUT :	None.
	RETURN :	The matcheddnp or an error string.
	DESCRIPTION :
 *****************************************************************************/
char *
dnFromMessage (
	thread_context	*tttctx,
	LDAPMessage	*res)
{
  static char	*notFound = "*** not found by ldclt ***";
  int		 ret;
  int		 errcodep;

  /*
   * Maybe a previous call to free...
   */
  if (tttctx->matcheddnp)
    ldap_memfree (tttctx->matcheddnp);

  /*
   * Get the requested information
   */
  ret = ldap_parse_result (tttctx->ldapCtx, res, &errcodep, 
		&(tttctx->matcheddnp), NULL, NULL, NULL, 0);
  switch (ret)
  {
    case LDAP_SUCCESS:
    case LDAP_MORE_RESULTS_TO_RETURN:
	  return (tttctx->matcheddnp);
    case LDAP_NO_RESULTS_RETURNED:
    case LDAP_DECODING_ERROR:
    case LDAP_PARAM_ERROR:
    case LDAP_NO_MEMORY:
    default:
	  tttctx->matcheddnp = NULL;
	  printf ("ldclt[%d]: T%03d: Cannot ldap_parse_result(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
	  fflush (stdout);
	  return (notFound);
  }
}

					/* New function */	/*JLS 03-05-01*/
/* ****************************************************************************
	FUNCTION :	getBindAndPasswdFromFile
	PURPOSE :	Get the new bindDN and passwd to use from a dlf.
	INPUT :		tttctx	= this thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
getBindAndPasswdFromFile (
	thread_context	*tttctx)
{
  int	 num;	/* Temp. number */
  int	 i;	/* For the loops */

  /*
   * The bind DN is before the first '\t'
   */
  num = (lrand48() % mctx.rndBindDlf->strNb);
  for (i=0 ; mctx.rndBindDlf->str[num][i]!='\0' &&
	     mctx.rndBindDlf->str[num][i]!='\t' ; i++);
  if (mctx.rndBindDlf->str[num][i] == '\0')
  {
    printf ("ldclt[%d]: %s: No bind DN find line %d of %s\n", 
			mctx.pid, tttctx->thrdId, num+1, mctx.rndBindFname);
    return (-1);
  }
  strncpy (tttctx->bufBindDN, mctx.rndBindDlf->str[num], i);
  tttctx->bufBindDN[i] = '\0';

  /*
   * Skip the '\t' to find the password.
   * The password is from this place up to the end of the line.
   */
  while (mctx.rndBindDlf->str[num][i]!='\0' && 
	 mctx.rndBindDlf->str[num][i]=='\t')
    i++;
  if (mctx.rndBindDlf->str[num][i] == '\0')
  {
    printf ("ldclt[%d]: %s: No password find line %d of %s\n", 
			mctx.pid, tttctx->thrdId, num+1, mctx.rndBindFname);
    return (-1);
  }
  strcpy (tttctx->bufPasswd, &(mctx.rndBindDlf->str[num][i]));

  return (0);
}










					/* New function */	/*JLS 05-01-01*/
/* ****************************************************************************
	FUNCTION :	buildNewBindDN
	PURPOSE :	Purpose of the fct
	INPUT :		tttctx	= this thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
buildNewBindDN (
	thread_context	*tttctx)
{
  /*
   * Maybe we should get the bindDN and passwd from a file ?
   */
  if (mctx.mod2 & M2_RNDBINDFILE)
    return (getBindAndPasswdFromFile (tttctx));

  /*
   * If we shouldn't operate with a variable bind DN, then the buffers
   * are already initiated with the fixed values...
   */
  if (!(mctx.mode & RANDOM_BINDDN))
    return (0);

  /*
   * Generate the random value we will use for both the bind DN 
   * and the password.
   */
  if (mctx.mode & STRING)
    (void) randomString (tttctx, mctx.bindDNNbDigit);
  else
    rnd (tttctx->buf2, mctx.bindDNLow, mctx.bindDNHigh, mctx.bindDNNbDigit);

  /*
   * First, randomize the bind DN.
   */
  strncpy (&(tttctx->bufBindDN[tttctx->startBindDN]), tttctx->buf2,
	mctx.bindDNNbDigit);
  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: bind DN=\"%s\"\n",
             mctx.pid, tttctx->thrdNum, tttctx->bufBindDN);

  /*
   * Second, randomize the bind password.
   */
  strncpy (&(tttctx->bufPasswd[tttctx->startPasswd]), tttctx->buf2,
	mctx.passwdNbDigit);
  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: bind passwd=\"%s\"\n",
             mctx.pid, tttctx->thrdNum, tttctx->bufPasswd);

  /*
   * No problem found.
   */
  return (0);
}







#if defined(USE_OPENLDAP)
int 
refRebindProc(
	LDAP       *ldapCtx,
	const char *url,
	ber_tag_t  request,
	ber_int_t  msgid,
	void       *arg
)
{
  thread_context	*tttctx;
  struct berval		cred;

  tttctx = (thread_context *)arg;

  cred.bv_val = tttctx->bufPasswd;
  cred.bv_len = strlen(tttctx->bufPasswd);
  return ldap_sasl_bind_s(ldapCtx, tttctx->bufBindDN, LDAP_SASL_SIMPLE,
                          &cred, NULL, NULL, NULL);
}
#else /* !USE_OPENLDAP */
					/* New function */	/*JLS 08-03-01*/
/* ****************************************************************************
	FUNCTION :	refRebindProc
	PURPOSE :	This function is intended to perform the rebind when
			a referral requires it.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	Always LDAP_SUCCESS for the moment...
	DESCRIPTION :
 *****************************************************************************/
int
refRebindProc (
	LDAP	 *ldapCtx,
	char	**dnp,
	char	**passwdp,
	int	 *authmethodp,
	int	  freeit,
	void	 *arg)
{
  thread_context	*tttctx;

  tttctx = (thread_context *)arg;

  /*
   * We will assume here that the same DN and passwd will be used to
   * bind to the referred server, so we will just get the values used
   * previously from the thread's context.
   */
  *dnp         = tttctx->bufBindDN;
  *passwdp     = tttctx->bufPasswd;
  *authmethodp = LDAP_AUTH_SIMPLE;

  /*
   * What should we do with the "freeit" argument ? I do not have any
   * memory to free, so let's just ignore it.
   */

  return (LDAP_SUCCESS);
}
#endif /* !USE_OPENLDAP */







					/* New function */	/*JLS 08-03-01*/
/* ****************************************************************************
	FUNCTION :	referralSetup
	PURPOSE :	Initiates referral features. This function is called
			once after the ldap_init().
	INPUT :		tttctx	= this thread's thread_context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
referralSetup (
	thread_context	*tttctx)
{
  int	 ret;	/* Return value */
  void	*ref;	/* Referral settings */

  /*
   * Set the referral options...
   */
  if (mctx.referral == REFERRAL_OFF)
    ref = LDAP_OPT_OFF;
  else
    ref = LDAP_OPT_ON;

  ret = ldap_set_option (tttctx->ldapCtx, LDAP_OPT_REFERRALS, &ref);
  if (ret < 0)
  {
    printf ("ldclt[%d]: %s: Cannot ldap_set_option(LDAP_OPT_REFERRALS)\n",
             mctx.pid, tttctx->thrdId);
    fflush (stdout);
    return (-1);
  }

  /*
   * Maybe the user would like to have an authenticated referral rebind ?
   * Note : at 09-03-01 ldap_set_rebind_proc() is a void return function
   * Note : cannot compile on _WIN32 without the cast... even if I cast to
   *        the same thing !!!!
   */
  if (mctx.referral == REFERRAL_REBIND)
#ifdef _WIN32							/*JLS 14-03-01*/
    ldap_set_rebind_proc (tttctx->ldapCtx, 			/*JLS 14-03-01*/
		(LDAP_REBINDPROC_CALLBACK *)refRebindProc,	/*JLS 14-03-01*/
		(void *)tttctx);				/*JLS 14-03-01*/
#else								/*JLS 14-03-01*/
    ldap_set_rebind_proc (tttctx->ldapCtx, refRebindProc, (void *)tttctx);
#endif								/*JLS 14-03-01*/

  /*
   * Normal end
   */
  return (0);
}








/* ****************************************************************************
	FUNCTION :	connectToServer
	PURPOSE :	Realise the connection to the server.
			If requested by the user, it also realize the
			disconnection prior to connect.
	INPUT :		tttctx	= this thread's thread_context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
connectToServer (
	thread_context	*tttctx)
{
  int	 ret;	/* Return value */
  LBER_SOCKET	 fd;	/* LDAP cnx's fd */
  int	 v2v3;	/* LDAP version used */
  struct berval cred = {0, NULL};

  /*
   * Maybe close the connection ?
   * We will do this *here* to keep the cnx the longest time open.
   */
  if ((mctx.mode & BIND_EACH_OPER) && (tttctx->ldapCtx != NULL))
  {
    /*
     * Maybe the user want the connection to be *closed* rather than
     * being kindly unbinded ?
     */
    if (mctx.mode & CLOSE_FD)
    {
      /*
       * Get the corresponding fd
       */
#ifdef WORKAROUND_4197228
      if (getFdFromLdapSession (tttctx->ldapCtx, &fd) < 0)
      {
	printf ("ldclt[%d]: T%03d: Cannot extract fd from ldap session\n",
		mctx.pid, tttctx->thrdNum);
	fflush (stdout);
	return (-1);
      }
#else
      ret = ldap_get_option (tttctx->ldapCtx, LDAP_OPT_DESC, &fd);
      if (ret < 0)
      {
	printf ("ldclt[%d]: T%03d: Cannot ldap_get_option(LDAP_OPT_DESC)\n",
		mctx.pid, tttctx->thrdNum);
	fflush (stdout);
	return (-1);
      }
#endif
#ifdef TRACE_FD_GET_OPTION_BUG
      printf ("ldclt[%d]: T%03d:  fd=%d\n", mctx.pid, tttctx->thrdNum, (int)fd);
#endif
      if (close ((int)fd) < 0)
      {
	perror ("ldctx");
	printf ("ldclt[%d]: T%03d: cannot close(fd=%d), error=%d (%s)\n",
            mctx.pid, tttctx->thrdNum, (int)fd, errno, strerror (errno));
	return (-1);
      }
    }

    /*
     * Ok, anyway, we must ldap_unbind() to release our contextes
     * at the client side, otherwise this process will rocket through
     * the ceiling.
     * But don't be afraid, the UNBIND operation never reach the
     * server that will only see a suddent socket disconnection.
     */
    ret = ldap_unbind_ext (tttctx->ldapCtx, NULL, NULL);
    if (ret != LDAP_SUCCESS)
    {
      fprintf (stderr, "ldclt[%d]: T%03d: cannot ldap_unbind(), error=%d (%s)\n",
			mctx.pid, tttctx->thrdNum, ret,strerror (ret));
      fflush (stderr);
      if (addErrorStat (ret) < 0)
	return (-1);
      return (-1);
    }
    tttctx->ldapCtx = NULL;
  }

  /*
   * Maybe create the LDAP context ?
   */
  if (tttctx->ldapCtx == NULL)
  {
    const char *mech = LDAP_SASL_SIMPLE;
    const char *binddn = NULL;
    const char *passwd = NULL;
#if defined(USE_OPENLDAP)
    char *ldapurl = NULL;
#endif

#if defined(USE_OPENLDAP)
    ldapurl = PR_smprintf("ldap%s://%s:%d/",
			  (mctx.mode & SSL) ? "s" : "",
			  mctx.hostname, mctx.port);
    if ((ret = ldap_initialize(&tttctx->ldapCtx, ldapurl))) {
	printf ("ldclt[%d]: T%03d: Cannot ldap_initialize (%s), errno=%d ldaperror=%d:%s\n",
		mctx.pid, tttctx->thrdNum, ldapurl, errno, ret, my_ldap_err2string(ret));
	fflush (stdout);
	PR_smprintf_free(ldapurl);
	return (-1);
    }
    PR_smprintf_free(ldapurl);
    ldapurl = NULL;
#else /* !USE_OPENLDAP */
    /*
     * SSL is enabled ?
     */
    if (mctx.mode & SSL)
    {
      /*
       * LDAP session initialization in SSL mode
       * added by: B Kolics (11/10/00)
       */
      tttctx->ldapCtx = ldapssl_init(mctx.hostname, mctx.port, 1);
      if (mctx.mode & VERY_VERBOSE)
	printf ("ldclt[%d]: T%03d: After ldapssl_init (%s, %d), ldapCtx=0x%p\n",
		mctx.pid, tttctx->thrdNum, mctx.hostname, mctx.port,
		tttctx->ldapCtx);
      if (tttctx->ldapCtx == NULL)
      {
	printf ("ldclt[%d]: T%03d: Cannot ldapssl_init (%s, %d), errno=%d\n",
		mctx.pid, tttctx->thrdNum, mctx.hostname, mctx.port, errno);
	fflush (stdout);
	return (-1);
      }
      /*
       * Client authentication is used ?
       */
      if (mctx.mode & CLTAUTH)
	{
	 ret = ldapssl_enable_clientauth(tttctx->ldapCtx, "", mctx.keydbpin, mctx.cltcertname);
	 if (mctx.mode & VERY_VERBOSE)
	   printf 
	    ("ldclt[%d]: T%03d: After ldapssl_enable_clientauth (ldapCtx=0x%p, %s, %s)",
	     mctx.pid, tttctx->thrdNum, tttctx->ldapCtx, mctx.keydbpin,
	     mctx.cltcertname);
	 if (ret < 0)
	 {
	   printf
	    ("ldclt[%d]: T%03d: Cannot ldapssl_enable_clientauth (ldapCtx=0x%p, %s, %s)",
	     mctx.pid, tttctx->thrdNum, tttctx->ldapCtx, mctx.keydbpin,
	     mctx.cltcertname);
	   ldap_perror(tttctx->ldapCtx, "ldapssl_enable_clientauth");
	   fflush (stdout);
	   return (-1);
	 }
      }
    } else {
      /*
       * connection initialization in normal, unencrypted mode
       */
      tttctx->ldapCtx = ldap_init (mctx.hostname, mctx.port);
      if (mctx.mode & VERY_VERBOSE)
	printf ("ldclt[%d]: T%03d: After ldap_init (%s, %d), ldapCtx=0x%p\n",
		mctx.pid, tttctx->thrdNum, mctx.hostname, mctx.port,
		tttctx->ldapCtx);
      if (tttctx->ldapCtx == NULL)
      {
	printf ("ldclt[%d]: T%03d: Cannot ldap_init (%s, %d), errno=%d\n",
		mctx.pid, tttctx->thrdNum, mctx.hostname, mctx.port, errno);
	fflush (stdout);
	return (-1);
      }
    }
#endif /* !USE_OPENLDAP */

    if (mctx.mode & CLTAUTH) {
      mech = "EXTERNAL";
      binddn = "";
      passwd = NULL;
    } else {
      binddn = tttctx->bufBindDN?tttctx->bufBindDN:mctx.bindDN;
      passwd = tttctx->bufPasswd?tttctx->bufPasswd:mctx.passwd;
      if (passwd) {
        cred.bv_val = (char *)passwd;
        cred.bv_len = strlen(passwd);
      }
    }

    if (mctx.mode & LDAP_V2)
      v2v3 = LDAP_VERSION2;
    else
      v2v3 = LDAP_VERSION3;

    ret = ldap_set_option (tttctx->ldapCtx, LDAP_OPT_PROTOCOL_VERSION, &v2v3);
    if (ret < 0)
    {
      printf ("ldclt[%d]: T%03d: Cannot ldap_set_option(LDAP_OPT_PROTOCOL_VERSION)\n",
		mctx.pid, tttctx->thrdNum);
      fflush (stdout);
      return (-1);
    }

    /*
     * Set the referral options...
     */
    if (referralSetup (tttctx) < 0)
      return (-1);
  }

  /*
   * Let's save some time here... If no bindDN is provided, the tool is
   * working in anonymous mode, i.e. we may consider it is always
   * binded.
   * NOTE : maybe some cleanup is needed with the tests mctx.bindDN!=NULL
   *        below in this function ?
   *        03-05-01 : no cleanup I think, cf M2_RNDBINDFILE
   */
  if ((mctx.bindDN == NULL) && ((!(mctx.mod2 & M2_RNDBINDFILE))
       && (!(mctx.mod2 & M2_SASLAUTH))))
  {								/*JLS 05-03-01*/
    tttctx->binded = 1;						/*JLS 05-03-01*/
    return (0);							/*JLS 05-03-01*/
  }								/*JLS 05-03-01*/

  /*
   * Maybe we should bind ?
   */
  /*
   * for SSL client authentication, SASL BIND is used
   */
  if ((mctx.mode & CLTAUTH) && ((!(tttctx->binded)) ||
				(mctx.mode & BIND_EACH_OPER)))
  {
    if (mctx.mode & VERY_VERBOSE)
      printf ("ldclt[%d]: T%03d: Before ldap_sasl_bind_s\n",
               mctx.pid, tttctx->thrdNum);
    ret = ldap_sasl_bind_s (tttctx->ldapCtx, "", "EXTERNAL", NULL, NULL, NULL,
			    NULL);
    if (mctx.mode & VERY_VERBOSE)
      printf ("ldclt[%d]: T%03d: After ldap_sasl_bind_s\n",
               mctx.pid, tttctx->thrdNum);
    if (ret == LDAP_SUCCESS)					/*JLS 18-12-00*/
      tttctx->binded = 1;					/*JLS 18-12-00*/
    else							/*JLS 18-12-00*/
    {								/*JLS 18-12-00*/
      tttctx->binded  = 0;					/*JLS 18-12-00*/
      if (ignoreError (ret))					/*JLS 18-12-00*/
      {								/*JLS 18-12-00*/
	if (!(mctx.mode & QUIET))				/*JLS 18-12-00*/
	{							/*JLS 18-12-00*/
	  printf ("ldclt[%d]: T%03d: Cannot ldap_sasl_bind_s, error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
	  fflush (stdout);					/*JLS 18-12-00*/
	}							/*JLS 18-12-00*/
	if (addErrorStat (ret) < 0)				/*JLS 18-12-00*/
	  return (-1);						/*JLS 18-12-00*/
	return (0);						/*JLS 18-12-00*/
      }								/*JLS 18-12-00*/
      else							/*JLS 18-12-00*/
      {								/*JLS 18-12-00*/
	printf ("ldclt[%d]: T%03d: Cannot ldap_sasl_bind_s, error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
	fflush (stdout);					/*JLS 18-12-00*/
	tttctx->exitStatus = EXIT_NOBIND;			/*JLS 18-12-00*/
	if (addErrorStat (ret) < 0)				/*JLS 18-12-00*/
	  return (-1);						/*JLS 18-12-00*/
	return (-1);						/*JLS 18-12-00*/
      }								/*JLS 18-12-00*/
    }
  } else if ((mctx.mod2 & M2_SASLAUTH) && ((!(tttctx->binded)) ||
                                          (mctx.mode & BIND_EACH_OPER))) {
    void *defaults;
    char *my_saslauthid = NULL;

    if ( mctx.sasl_mech == NULL) {
      fprintf( stderr, "Please specify the SASL mechanism name when "
                           "using SASL options\n");
      return (-1);
    }

    if ( mctx.sasl_secprops != NULL) {
      ret = ldap_set_option( tttctx->ldapCtx, LDAP_OPT_X_SASL_SECPROPS,
                             (void *) mctx.sasl_secprops );

      if ( ret != LDAP_SUCCESS ) {
        fprintf( stderr, "Unable to set LDAP_OPT_X_SASL_SECPROPS: %s\n",
                          mctx.sasl_secprops );
        return (-1);
      }
    }

  /*
   * Generate the random authid if set up so
   */
    if (mctx.mod2 & M2_RANDOM_SASLAUTHID)
    {
      rnd (tttctx->buf2, mctx.sasl_authid_low, mctx.sasl_authid_high,
                         mctx.sasl_authid_nbdigit);
      strncpy (&(tttctx->bufSaslAuthid[tttctx->startSaslAuthid]),
                         tttctx->buf2, mctx.sasl_authid_nbdigit);
      my_saslauthid = tttctx->bufSaslAuthid;
      if (mctx.mode & VERY_VERBOSE)
        printf ("ldclt[%d]: T%03d: Sasl Authid=\"%s\"\n",
             mctx.pid, tttctx->thrdNum, tttctx->bufSaslAuthid);
    }
    else
    {
      my_saslauthid =  mctx.sasl_authid;
    }

    defaults = ldaptool_set_sasl_defaults( tttctx->ldapCtx, mctx.sasl_flags, mctx.sasl_mech,
              my_saslauthid, mctx.sasl_username, mctx.passwd, mctx.sasl_realm );
    if (defaults == NULL) {
      perror ("malloc");
      exit (LDAP_NO_MEMORY);
    }
#if defined(USE_OPENLDAP)
    ret = ldap_sasl_interactive_bind_s( tttctx->ldapCtx, mctx.bindDN, mctx.sasl_mech,
                       NULL, NULL, mctx.sasl_flags,
                       ldaptool_sasl_interact, defaults );
#else
    ret = ldap_sasl_interactive_bind_ext_s( tttctx->ldapCtx, mctx.bindDN, mctx.sasl_mech,
                       NULL, NULL, mctx.sasl_flags,
                       ldaptool_sasl_interact, defaults, NULL );
#endif
    if (ret != LDAP_SUCCESS ) {
      tttctx->binded = 0;
      if (!(mctx.mode & QUIET)) {
	fprintf(stderr, "Error: could not bind: %d:%s\n",
		ret, my_ldap_err2string(ret));
      }
      if (addErrorStat (ret) < 0)
        return (-1);
    } else {
        tttctx->binded = 1;
    }

    ldaptool_free_defaults( defaults );
  } else {
    if (((mctx.bindDN != NULL) || (mctx.mod2 & M2_RNDBINDFILE)) &&  /*03-05-01*/
	((!(tttctx->binded)) || (mctx.mode & BIND_EACH_OPER)))
    {
      struct berval *servercredp = NULL;
      char *binddn = NULL;
      char *passwd = NULL;

      if (buildNewBindDN (tttctx) < 0)				/*JLS 05-01-01*/
        return (-1);						/*JLS 05-01-01*/
      if (tttctx->bufPasswd) {
        binddn = tttctx->bufBindDN;
        passwd = tttctx->bufPasswd;
      } else if (mctx.passwd) {
        binddn = mctx.bindDN;
        passwd = mctx.passwd;
      }
      if (passwd) {
        cred.bv_val = passwd;
        cred.bv_len = strlen(passwd);
      }
      if (mctx.mode & VERY_VERBOSE)
        printf ("ldclt[%d]: T%03d: Before ldap_simple_bind_s (%s, %s)\n",
                mctx.pid, tttctx->thrdNum, binddn,
                passwd?passwd:"NO PASSWORD PROVIDED");
      ret = ldap_sasl_bind_s (tttctx->ldapCtx, binddn,
            LDAP_SASL_SIMPLE, &cred, NULL, NULL, &servercredp);	/*JLS 05-01-01*/
      ber_bvfree(servercredp);
      if (mctx.mode & VERY_VERBOSE)
        printf ("ldclt[%d]: T%03d: After ldap_simple_bind_s (%s, %s)\n",
                mctx.pid, tttctx->thrdNum, binddn,
                passwd?passwd:"NO PASSWORD PROVIDED");
      if (ret == LDAP_SUCCESS)					/*JLS 18-12-00*/
        tttctx->binded = 1;					/*JLS 18-12-00*/
      else							/*JLS 18-12-00*/
      {								/*JLS 18-12-00*/
	tttctx->binded  = 0;					/*JLS 18-12-00*/
	if (ignoreError (ret))					/*JLS 18-12-00*/
	{							/*JLS 18-12-00*/
	  if (!(mctx.mode & QUIET))				/*JLS 18-12-00*/
	  {							/*JLS 18-12-00*/
	    printf("ldclt[%d]: T%03d: Cannot ldap_simple_bind_s (%s, %s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, tttctx->bufBindDN,
		mctx.passwd?tttctx->bufPasswd:"NO PASSWORD PROVIDED",
		ret, my_ldap_err2string (ret));
	    fflush (stdout);					/*JLS 18-12-00*/
	  }							/*JLS 18-12-00*/
	  if (addErrorStat (ret) < 0)				/*JLS 18-12-00*/
	    return (-1);					/*JLS 18-12-00*/
	  return (0);						/*JLS 18-12-00*/
	}							/*JLS 18-12-00*/
	else							/*JLS 18-12-00*/
	{							/*JLS 18-12-00*/
	  printf ("ldclt[%d]: T%03d: Cannot ldap_simple_bind_s (%s, %s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, tttctx->bufBindDN,
		mctx.passwd?tttctx->bufPasswd:"NO PASSWORD PROVIDED",
		ret, my_ldap_err2string (ret));
	  fflush (stdout);					/*JLS 18-12-00*/
	  tttctx->exitStatus = EXIT_NOBIND;			/*JLS 18-12-00*/
	  if (addErrorStat (ret) < 0)				/*JLS 18-12-00*/
	    return (-1);					/*JLS 18-12-00*/
	  return (-1);						/*JLS 18-12-00*/
	}							/*JLS 18-12-00*/
      }
    }
  }

  /*
   * Normal end
   */
  return (0);
}











/* ****************************************************************************
	FUNCTION :	buildVersatileAttribute
	PURPOSE :	Build a new attribute value using the definitions of
			the parameters.
			The pointer returned is always to a safe buffer inside
			the attribute.
	INPUT :		tttctx	= thread context
			object	= object definition
			attrib	= attribute to build
	OUTPUT :	None.
	RETURN :	NULL if error, pointer to the new value else.
	DESCRIPTION :	
 *****************************************************************************/
char *
buildVersatileAttribute (
	thread_context	*tttctx,
	vers_object	*object,
	vers_attribute	*attrib)
{
  vers_field	*field;		/* To parse the fields */
  int		 num;		/* Temp. number */

  /*
   * Maybe this attribute has a constant value ?
   * (i.e. only one field, constant value)
   */
  if (attrib->buf == NULL)
    return (attrib->field->cst);

  /*
   * Well, it looks like we will have to build the new value
   */
  attrib->buf[0] = '\0';	/* No field yet */
  for (field=attrib->field ; field!=NULL ; field=field->next)
  {
    switch (field->how)
    {
      case HOW_CONSTANT:
	strcat (attrib->buf, field->cst);
	break;
      case HOW_INCR_FROM_FILE:
	if (mctx.mode & COMMON_COUNTER)
	{
	  num = incrementCommonCounterObject (tttctx, field->commonField);
	  if (num < 0)
	    return (NULL);
	}
	else
	{
	  if (field->cnt > field->high)
	    field->cnt = field->low;
	  num = field->cnt;
	  field->cnt++;	/* Next value for next loop */
	}
	strcat (attrib->buf, field->dlf->str[num]);
	if (field->var != -1)
	  strcpy (object->var[field->var], field->dlf->str[num]);
	break;
      case HOW_INCR_FROM_FILE_NL:
	if (mctx.mode & COMMON_COUNTER)
	{
	  num = incrementCommonCounterObject (tttctx, field->commonField);
	  if (num < 0)
	    return (NULL);
	}
	else
	{
	  if (field->cnt > field->high)
	  {
	    printf ("ldclt[%d]: %s: Hit top incrementeal value\n",
                     mctx.pid, tttctx->thrdId);
	    return(NULL);
	  }
	  num = field->cnt;
	  field->cnt++;	/* Next value for next loop */
	}
	strcat (attrib->buf, field->dlf->str[num]);
	if (field->var != -1)
	  strcpy (object->var[field->var], tttctx->buf2);
	break;
      case HOW_INCR_NB:
	if (mctx.mode & COMMON_COUNTER)
	{
	  num = incrementCommonCounterObject (tttctx, field->commonField);
	  if (num < 0)
	    return (NULL);
	}
	else
	{
	  if (field->cnt > field->high)
	    field->cnt = field->low;
	  num = field->cnt;
	  field->cnt++;	/* Next value for next loop */
	}
	sprintf (tttctx->buf2, "%0*d", field->nb, num);
	strcat (attrib->buf, tttctx->buf2);
	if (field->var != -1)
	  strcpy (object->var[field->var], tttctx->buf2);
	break;
      case HOW_INCR_NB_NOLOOP:
	if (mctx.mode & COMMON_COUNTER)
	{
	  num = incrementCommonCounterObject (tttctx, field->commonField);
	  if (num < 0)
	    return (NULL);
	}
	else
	{
	  if (field->cnt > field->high)
	  {
	    printf ("ldclt[%d]: %s: Hit top incrementeal value\n",
                     mctx.pid, tttctx->thrdId);
	    return(NULL);
	  }
	  num = field->cnt;
	  field->cnt++;	/* Next value for next loop */
	}
	sprintf (tttctx->buf2, "%0*d", field->nb, num);
	strcat (attrib->buf, tttctx->buf2);
	if (field->var != -1)
	  strcpy (object->var[field->var], tttctx->buf2);
	break;
      case HOW_RND_FROM_FILE:
	num = (lrand48() % field->dlf->strNb);
	strcat (attrib->buf, field->dlf->str[num]);
	if (field->var != -1)
	  strcpy (object->var[field->var], field->dlf->str[num]);
	break;
      case HOW_RND_NUMBER:
	rnd (tttctx->buf2, field->low, field->high, field->nb);
	strcat (attrib->buf, tttctx->buf2);
	if (field->var != -1)
	  strcpy (object->var[field->var], tttctx->buf2);
	break;
      case HOW_RND_STRING:
	(void) randomString (tttctx, field->nb);
	strcat (attrib->buf, tttctx->buf2);
	if (field->var != -1)
	  strcpy (object->var[field->var], tttctx->buf2);
	break;
      case HOW_VARIABLE:
	if (object->var[field->var] == NULL)			/*JLS 11-04-01*/
	{							/*JLS 11-04-01*/
	  printf ("ldclt[%d]: %s: Error : unset variable %c in %s\n",
			mctx.pid, tttctx->thrdId,
			'A'+field->var, attrib->src);		/*JLS 11-04-01*/
	  return(NULL);						/*JLS 11-04-01*/
	}							/*JLS 11-04-01*/
	strcat (attrib->buf, object->var[field->var]);
	break;
      default:
	/*
	 * Should not happen, unless new variant parsed and not
	 * integrated here, or "jardinage"....
	 */
	field      = NULL;
	field->how = 22;	/* Crash !!! */
	break;
    }
  }

  /*
   * Return the new value.
   */
  return (attrib->buf);
}










/* ****************************************************************************
	FUNCTION :	buildRandomRdnOrFilter
	PURPOSE :	This function will build a random string (rdn or filter)
			to be used by ldap_search() or ldap_add() or etc...
			The result is in tttctx->bufFilter
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
buildRandomRdnOrFilter (
	thread_context	*tttctx)
{

  /*
   * Maybe we will operate with a variable base DN ?
   */
  if (mctx.mode & RANDOM_BASE)
  {
    if (mctx.mode & STRING)
      (void) randomString (tttctx, mctx.baseDNNbDigit);
    else
      rnd (tttctx->buf2, mctx.baseDNLow, mctx.baseDNHigh,	/*JLS 14-11-00*/
					mctx.baseDNNbDigit);	/*JLS 14-11-00*/
    strncpy (&(tttctx->bufBaseDN[tttctx->startBaseDN]), 
			tttctx->buf2, mctx.baseDNNbDigit);
    if (mctx.mode & VERY_VERBOSE)
      printf ("ldclt[%d]: T%03d: base DN=\"%s\"\n",
               mctx.pid, tttctx->thrdNum, tttctx->bufBaseDN);
  }

  /*
   * Maybe we must build a random attribute value ?
   * We only support random string generation here.
   */
  if (mctx.mode & ATTR_REPLACE)					/*JLS 21-11-00*/
  {								/*JLS 21-11-00*/
    (void) randomString (tttctx, mctx.attrplNbDigit);		/*JLS 21-11-00*/
    strncpy (&(tttctx->bufAttrpl[tttctx->startAttrpl]), 	/*JLS 21-11-00*/
			tttctx->buf2, mctx.attrplNbDigit);	/*JLS 21-11-00*/
    if (mctx.mode & VERY_VERBOSE)				/*JLS 21-11-00*/
      printf ("ldclt[%d]: T%03d: attrib=\"%s\"\n",
               mctx.pid, tttctx->thrdNum, tttctx->bufAttrpl);
  }								/*JLS 21-11-00*/

  /*
   * Maybe we must use a variant-rdn style ?
   */
  if (mctx.mod2 & M2_RDN_VALUE)					/*JLS 23-03-01*/
  {								/*JLS 23-03-01*/
    char	 *buf;	/* Temp for new value */		/*JLS 23-03-01*/
								/*JLS 23-03-01*/
    buf = buildVersatileAttribute (tttctx, 			/*JLS 23-03-01*/
			tttctx->object, tttctx->object->rdn);	/*JLS 23-03-01*/
    if (buf == NULL)						/*JLS 23-03-01*/
      return (-1);						/*JLS 23-03-01*/
    strcpy (tttctx->bufFilter, tttctx->object->rdnName);	/*JLS 23-03-01*/
    strcat (tttctx->bufFilter, "=");				/*JLS 23-03-01*/
    strcat (tttctx->bufFilter, buf);				/*JLS 23-03-01*/
    if (mctx.mode & VERY_VERBOSE)				/*JLS 28-03-01*/
      printf ("ldclt[%d]: %s: rdn variant mode:filter=\"%s\"\n",
               mctx.pid, tttctx->thrdId, tttctx->bufFilter);
  }								/*JLS 23-03-01*/
  else								/*JLS 23-03-01*/
  {								/*JLS 23-03-01*/
    /*
     * Build the new filter string
     */
    if (mctx.mode & RANDOM)
    {
      if (mctx.mode & STRING)
	(void) randomString (tttctx, mctx.randomNbDigit);
      else
	rnd (tttctx->buf2, mctx.randomLow, mctx.randomHigh,	/*JLS 14-11-00*/
			mctx.randomNbDigit);			/*JLS 14-11-00*/
      strncpy (&(tttctx->bufFilter[tttctx->startRandom]), 
			tttctx->buf2, mctx.randomNbDigit);
      if (mctx.mode & VERY_VERBOSE)
	printf ("ldclt[%d]: %s: random mode:filter=\"%s\"\n",
                 mctx.pid, tttctx->thrdId, tttctx->bufFilter);
    }
    if (mctx.mode & INCREMENTAL)
    {
      if (mctx.mode & COMMON_COUNTER)				/*JLS 14-03-01*/
      {								/*JLS 14-03-01*/
	int	 val;						/*JLS 14-03-01*/
	val = incrementCommonCounter (tttctx);			/*JLS 14-03-01*/
	if (val == -1)						/*JLS 14-03-01*/
	  return (-1);						/*JLS 14-03-01*/
	sprintf (tttctx->buf2, "%0*d", mctx.randomNbDigit, val);/*JLS 14-03-01*/
      }								/*JLS 14-03-01*/
      else							/*JLS 14-03-01*/
      {								/*JLS 14-03-01*/
	tttctx->lastVal++;
	if (tttctx->lastVal > mctx.randomHigh)
	{
	  if (!(mctx.mode & NOLOOP))
	    tttctx->lastVal = mctx.randomLow;
	  else
	  {
	    /*
	     * Well, there is no clean way to exit. Let's use the error
	     * condition and hope all will be ok.
	     */
	    printf ("ldclt[%d]: %s: Hit top incrementeal value\n",
                     mctx.pid, tttctx->thrdId);
	    return (-1);
	  }
	}
	sprintf (tttctx->buf2, "%0*d", mctx.randomNbDigit, tttctx->lastVal);
      }								/*JLS 14-03-01*/

      strncpy (&(tttctx->bufFilter[tttctx->startRandom]), tttctx->buf2,
			mctx.randomNbDigit);
      if (mctx.mode & VERY_VERBOSE)
	printf ("ldclt[%d]: %s: incremental mode:filter=\"%s\"\n",
                 mctx.pid, tttctx->thrdId, tttctx->bufFilter);
    }
  }								/*JLS 23-03-01*/

  return (0);
}









/* ****************************************************************************
	FUNCTION :	addAttrib
	PURPOSE :	Add a new attribute to the given LDAPMod array
	INPUT :		attrs	= existing LDAPMod array
			nb	= number of entries in the array
			newattr	= new attribute to add to the list
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :	Important note: the first time this function is called,
			it must be with nb==0.
 *****************************************************************************/
int
addAttrib (
	LDAPMod	**attrs,
	int	  nb,
	LDAPMod	 *newattr)
{
  attrs[nb] = (LDAPMod *) malloc (sizeof (LDAPMod));
  if (attrs[nb] == NULL)					/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf ("ldclt[%d]: Txxx: cannot malloc(attrs[%d]), error=%d (%s)\n",
		mctx.pid, nb, errno, strerror (errno));
    return (-1);						/*JLS 06-03-00*/
  }								/*JLS 06-03-00*/
  memcpy (attrs[nb], newattr, sizeof (LDAPMod));
  attrs[nb+1] = NULL;
  return (0);
}





/* ****************************************************************************
	FUNCTION :	freeAttrib
	PURPOSE :	Free an array of addAttrib.
	INPUT :		attrs	= LDAPMod array to free
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
freeAttrib (
	LDAPMod	**attrs)
{
  int	i;
  int	j;

  for (i=0 ; attrs[i]!=NULL ; i++) {
    if (attrs[i]->mod_op & LDAP_MOD_BVALUES) {
      for (j=0; attrs[i]->mod_bvalues[j] != NULL; j++) {
        free (attrs[i]->mod_bvalues[j]);
      }
      free (attrs[i]->mod_bvalues);
    } else {
      free (attrs[i]->mod_values);
    }

    free (attrs[i]);
  }

  return (0);
}








/* ****************************************************************************
	FUNCTION :	strList1
	PURPOSE :	Create a list (array) of two strings
	INPUT :		str1	= the first string.
	OUTPUT :	None.
	RETURN :	Pointer to the char *str[2]
	DESCRIPTION :
 *****************************************************************************/
char **
strList1 (
	char	*str1)
{
  char	**p;
  p = (char **) malloc (2 * sizeof (char *));
  if (p == NULL)						/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf ("ldclt[%d]: Txxx: cannot malloc(p), error=%d (%s)\n",
		mctx.pid, errno, strerror (errno));
    ldcltExit (EXIT_RESSOURCE);					/*JLS 18-12-00*/
  }								/*JLS 06-03-00*/
  p[0] = str1;
  p[1] = NULL;
  return (p);
}











					/* New function */	/*JLS 03-08-00*/
/* ****************************************************************************
	FUNCTION :	printErrorFromLdap
	PURPOSE :	Print the error message returned by ldap.
	INPUT :		tttctx	= thread context
			res	= LDAP result
			errcode	= error code
			errmsg	= error message
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
printErrorFromLdap (
	thread_context	*tttctx,
	LDAPMessage	*res,
	int		 errcode,
	char		*errmsg)
{
  int	 ret;		/* Return value */
  char	*addErrMsg;	/* Additional error msg */
  int	 errcodep;	/* Async error code */			/*JLS 11-08-00*/

  /*
   * Print the error message and error code
   */
  printf ("ldclt[%d]: T%03d: %s, error=%d (%s",
		mctx.pid, tttctx->thrdNum, errmsg,
		errcode, my_ldap_err2string (errcode));

  /*
   * See if there is an additional error message...
   */
  ret = ldap_parse_result (tttctx->ldapCtx, res, &errcodep,	/*JLS 11-08-00*/
		NULL, &addErrMsg, NULL, NULL, 0);
  if (ret != LDAP_SUCCESS)
  {
    printf (")\n");
    printf ("ldclt[%d]: T%03d: errcodep = %d\n",
		mctx.pid, tttctx->thrdNum, errcodep);
    printf ("ldclt[%d]: T%03d: Cannot ldap_parse_result(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
    return (-1);
  }

  /*
   * Ok, we were able to retrieve more information...
   * Well, the errcodep is the error of the operation parsed by
   * ldap_parse_result(), so we will print it if different from
   * the one given in argument to this function...
   */
  if (errcodep != errcode)					/*JLS 11-08-00*/
    printf ("ldclt[%d]: errcodep=%d (%s)",
		mctx.pid, errcodep, my_ldap_err2string (errcodep));
  if ((addErrMsg != NULL) && (*addErrMsg != '\0'))
  {
    printf (" - %s", addErrMsg);
    ldap_memfree (addErrMsg);
  }
  printf (")\n");

  /*
   * Don't forget to flush !
   */
  fflush (stdout);
  return (0);
}









/* ****************************************************************************
	FUNCTION :	buildNewModAttribFile
	PURPOSE :	Build a new (random or incremental) target DN and the
			corresponding LDAPMod for attribute modification.
	INPUT :		tttctx	= thread context
	OUTPUT :	newDN	= DN of the new entry
			attrs	= attributes for the ldap_modify
	RETURN :	-1 if error, 0 else.
 *****************************************************************************/
int
buildNewModAttribFile (
	thread_context	 *tttctx,
	char		 *newDn,
	LDAPMod		**attrs)
{
  int		 nbAttribs;	/* Nb of attributes */
  LDAPMod	 attribute;	/* To build the attributes */
  struct berval	*bv = malloc(sizeof(struct berval));
  attribute.mod_bvalues = (struct berval **)malloc(2 * sizeof(struct berval *));
  int		 rc = 0;

  if ((bv == NULL) || (attribute.mod_bvalues == NULL)) {
    rc = -1;
    goto error;
  }

  /*
   * Build the new DN
   * We will assume that the filter (-f argument) is set to use it
   * to build the rdn of the new entry.
   * Note that the random new attribute is also build by this function.
   */
  if (buildRandomRdnOrFilter (tttctx) < 0) {
    rc = -1;
    goto error;
  }

  strcpy (newDn, tttctx->bufFilter);
  strcat (newDn, ",");
  strcat (newDn, tttctx->bufBaseDN);

  /*
   * Build the attributes modification
   */
  bv->bv_len = mctx.attrplFileSize;
  bv->bv_val = mctx.attrplFileContent;
  attrs[0]  = NULL;	/* No attributes yet */
  nbAttribs = 0;	/* No attributes yet */
  attribute.mod_op     = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
  attribute.mod_type   = mctx.attrplName;
  attribute.mod_bvalues[0] = bv;
  attribute.mod_bvalues[1] = NULL;

  if (addAttrib (attrs, nbAttribs++, &attribute) < 0) {
    rc = -1;
    goto error;
  }

  goto done;

error:
  if (bv != NULL) {
    free(bv);
  }
  if (attribute.mod_bvalues != NULL) {
    free(attribute.mod_bvalues);
  }

done:
  /*
   * Normal end
   */
  return (0);
}



/* New function */	/*JLS 21-11-00*/
/* ****************************************************************************
	FUNCTION :	buildNewModAttrib
	PURPOSE :	Build a new (random or incremental) target DN and the
			corresponding LDAPMod for attribute modification.
	INPUT :		tttctx	= thread context
	OUTPUT :	newDN	= DN of the new entry
			attrs	= attributes for the ldap_modify
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
buildNewModAttrib (
	thread_context	 *tttctx,
	char		 *newDn,
	LDAPMod		**attrs)
{
  int		 nbAttribs;	/* Nb of attributes */
  LDAPMod	 attribute;	/* To build the attributes */

  /*
   * Build the new DN
   * We will assume that the filter (-f argument) is set to use it
   * to build the rdn of the new entry.
   * Note that the random new attribute is also build by this function.
   */
  if (buildRandomRdnOrFilter (tttctx) < 0)
    return (-1);
  strcpy (newDn, tttctx->bufFilter);
  strcat (newDn, ",");
  strcat (newDn, tttctx->bufBaseDN);

  /*
   * Build the attributes modification
   */
  attrs[0]  = NULL;	/* No attributes yet */
  nbAttribs = 0;	/* No attributes yet */
  attribute.mod_op     = LDAP_MOD_REPLACE;
  attribute.mod_type   = mctx.attrplName;
  attribute.mod_values = strList1 (tttctx->bufAttrpl);
  if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
    return (-1);

  /*
   * Normal end
   */
  return (0);
}













						/* New */	/*JLS 19-03-01*/
/* ****************************************************************************
	FUNCTION :	buildVersatileObject
	PURPOSE :	Build a new entry using the definitions in the object
			given in parameter.
	INPUT :		tttctx	= thread context
			object	= object definition
	OUTPUT :	attrs	= resulting attributes.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
buildVersatileObject (
	thread_context	 *tttctx,
	vers_object	 *object,
	LDAPMod		**attrs)
{
  int		 nbAttribs;	/* Nb of attributes */
  LDAPMod	 attribute;	/* To build the attributes */
  int		 i;		/* For the loop */
  char		*newValue;	/* New values for the attributes */

  /*
   * Initialization
   */
  attrs[0]  = NULL;	/* No attributes yet */
  nbAttribs = 0;	/* No attributes yet */

  /*
   * What is sure is that ttctx->bufFilter contains the rdn of the new entry !
   * This rdn is build from the filter, and is independant from the
   * object class.
   */
  for (i=0 ; tttctx->bufFilter[i] != '=' ; i++)
    tttctx->buf2[i] = tttctx->bufFilter[i];
  tttctx->buf2[i] = '\0';
  strcpy (tttctx->bufObject1, tttctx->buf2);
  attribute.mod_op     = LDAP_MOD_ADD;
  attribute.mod_type   = tttctx->bufObject1;
  attribute.mod_values = strList1 (&(tttctx->bufFilter[i+1]));
  if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
    return (-1);

  /*
   * We are certain that there is enough space in attrs
   */
  for (i=0 ; i<object->attribsNb ; i++)
  {
    attribute.mod_op   = LDAP_MOD_ADD;
    attribute.mod_type = object->attribs[i].name;

    newValue = buildVersatileAttribute (tttctx, object, &(object->attribs[i]));
    if (newValue == NULL)
      return (-1);

    attribute.mod_values = strList1 (newValue);
    if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
      return (-1);
  }

  return (0);
}












/* ****************************************************************************
	FUNCTION :	buildNewEntry
	PURPOSE :	Build a new (random or incremental) entry, to be used
			for ldap_add() or ldap_modify() operations.
	INPUT :		tttctx	= thread context
	OUTPUT :	newDn	= DN of the new entry
			attrs	= attributes of the new entry
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
buildNewEntry (
	thread_context	 *tttctx,
	char		 *newDn,
	LDAPMod		**attrs)
{
  int		 nbAttribs;	/* Nb of attributes */
  LDAPMod	 attribute;	/* To build the attributes */
  int		 i;		/* To loop */

  /*
   * Build the new DN
   * We will assume that the filter (-f argument) is set to use it
   * to build the rdn of the new entry.
   */
  if (buildRandomRdnOrFilter (tttctx) < 0)
    return (-1);
  strcpy (newDn, tttctx->bufFilter);
  strcat (newDn, ",");
  strcat (newDn, tttctx->bufBaseDN);

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: %s: tttctx->bufFilter=\"%s\"\n",
		mctx.pid, tttctx->thrdId, tttctx->bufFilter);

  /*
   * Maybe we are using the new versatile object definition ?
   */
  if (mctx.mod2 & M2_OBJECT)					/*JLS 19-03-01*/
  {								/*JLS 19-03-01*/
    if (buildVersatileObject(tttctx, tttctx->object, attrs) < 0)/*JLS 19-03-01*/
      return (-1);						/*JLS 19-03-01*/
    if (mctx.mode & VERY_VERBOSE)				/*JLS 19-03-01*/
    {								/*JLS 19-03-01*/
      for (i=0 ; attrs[i] != NULL ; i++)			/*JLS 19-03-01*/
	printf ("ldclt[%d]: %s: attrs[%d]=(\"%s\" , \"%s\")\n",
		mctx.pid, tttctx->thrdId, i,
		attrs[i]->mod_type, attrs[i]->mod_values[0]);	/*JLS 19-03-01*/
    }								/*JLS 19-03-01*/
    return (0);							/*JLS 19-03-01*/
  }								/*JLS 19-03-01*/

  /*
   * Build the attributes
   * First, the class...
   * The classe depends of course on the user's choice.
   * Up to now, we only accept person, or one of its subclasses, emailPerson.
   * The difference is that emailPerson contains no other mandatory attributes,
   * but an optionnal one caled "jpegPhoto". This one will be added at the end
   * of this function.
   * NOTE: When other classes will be managed, this function will be splitted
   *       to do this.
   */
  attrs[0]  = NULL;	/* No attributes yet */
  nbAttribs = 0;	/* No attributes yet */
  attribute.mod_op     = LDAP_MOD_ADD;
  attribute.mod_type   = "objectclass";
  if (mctx.mode & OC_PERSON)
    attribute.mod_values = strList1 ("person");
  if (mctx.mode & OC_EMAILPERSON)
    attribute.mod_values = strList1 ("emailPerson");
  if (mctx.mode & OC_INETORGPRSON)				/*JLS 07-11-00*/
    attribute.mod_values = strList1 ("inetOrgPerson");		/*JLS 07-11-00*/
  if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
    return (-1);

  /*
   * What is sure is that ttctx->bufFilter contains the rdn of the new entry !
   * This rdn is build from the filter, and is independant from the
   * object class.
   */
  for (i=0 ; tttctx->bufFilter[i] != '=' ; i++)
    tttctx->buf2[i] = tttctx->bufFilter[i];
  tttctx->buf2[i] = '\0';
  attribute.mod_op     = LDAP_MOD_ADD;
  attribute.mod_type   = tttctx->buf2;
  attribute.mod_values = strList1 (&(tttctx->bufFilter[i+1]));
  if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
    return (-1);

  /*
   * The other attributes...
   */
  if (mctx.mode & (OC_PERSON|OC_EMAILPERSON|OC_INETORGPRSON))	/*JLS 07-11-00*/
  {
    if (strcmp (tttctx->buf2, "cn"))
    {
      attribute.mod_op     = LDAP_MOD_ADD;
      attribute.mod_type   = "cn";
      attribute.mod_values = strList1 ("toto cn");
      if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
        return (-1);
    }
    if (strcmp (tttctx->buf2, "sn"))
    {
      attribute.mod_op     = LDAP_MOD_ADD;
      attribute.mod_type   = "sn";
      attribute.mod_values = strList1 ("toto sn");
      if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
        return (-1);
    }
    if ((mctx.mode & OC_INETORGPRSON) && (mctx.mod2 & M2_DEREF))
    {
      attribute.mod_op     = LDAP_MOD_ADD;
      attribute.mod_type   = LDCLT_DEREF_ATTR;
      /* refer itself */
      attribute.mod_values = strList1 (newDn);
      if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
        return (-1);
    }
  }

  /*
   * This object class is used because it contains an attribute photo...
   */
  if (mctx.mode & (OC_EMAILPERSON|OC_INETORGPRSON))		/*JLS 07-11-00*/
  {
    attribute.mod_op   = (LDAP_MOD_ADD|LDAP_MOD_BVALUES);
    attribute.mod_type = "jpegPhoto";
    if (getImage (&attribute) < 0)
      return (-1);
    if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
      return (-1);
  }


  /*
   * No more attributes. Dump the attributes...
   */
  if (mctx.mode & VERY_VERBOSE)
  {
    for (i=0 ; attrs[i] != NULL ; i++)
      printf ("ldclt[%d]: T%03d: attrs[%d]=(\"%s\" , \"%s\")\n",
		mctx.pid, tttctx->thrdNum, i,
		attrs[i]->mod_type, attrs[i]->mod_values[0]);
  }
  return (0);
}














/* ****************************************************************************
	FUNCTION :	createMissingNodes
	PURPOSE :	Create the missing intermediate nodes.
	INPUT :		tttctx	= thread context
			newDN	= new DN that was rejected due to error 32
				  LDAP_NO_SUCH_OBJECT
			cnx	= ldap connection. NULL if not connected.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :	This function will call itself recursively while it is
			impossible to create the node, as long as the error is
			LDAP_NO_SUCH_OBJECT (aka 32).
			This function connects to the server to perform its
			task, using the same DN and passwd as in the mctx, i.e.
			the same as the rest of this tool. Hence it is possible
			that it is impossible to initiate a database if the
			user is not cn=admin and the database is empty, because
			only cn=admin may create the root entry.
 *****************************************************************************/
int
createMissingNodes (
	thread_context	*tttctx,
	char		*newDN,
	LDAP		*cnx)
{
  int		 i, j;		/* For the loops */
  int		 ret;		/* Return value */
  char		*nodeDN;	/* Node to create */
  char		 attrName[256];	/* nodeDN's rdn attribute name */
  char		 attrVal[256];	/* nodeDN's rdn attribute value */
  char		*objClass;	/* Object class to create */
  int		 nbAttribs;	/* Nb of attributes */
  LDAPMod	 attribute;	/* To build the attributes */
  LDAPMod	*attrs[4];	/* Attributes of this entry */
  int		 v2v3;		/* LDAP version used */

  /*
   * Skip the rdn of the given newDN, that was rejected.
   * Don't forget to skip the leading spaces...
   */
  nodeDN=newDN;
  while (*nodeDN != '\0')
  {
    if (*nodeDN == ',')
      break;
    if (*nodeDN == '\\')
    {
      nodeDN++;
      if (*nodeDN == '\0')
	break;
    }
    nodeDN++;
  }
  if (*nodeDN == ',')
    nodeDN++;	/* Skip the ',' */
  while ((*nodeDN == ' ') && (*nodeDN != '\0'))
    nodeDN++;
  if (*nodeDN == '\0')
  {
    printf ("ldclt[%d]: T%03d: Reach top of DN for %s\n",
             mctx.pid, tttctx->thrdNum, newDN);
    fflush (stdout);
    return (-1);
  }

  if (mctx.mode & VERY_VERBOSE)					/*JLS 14-12-00*/
    printf ("ldclt[%d]: T%03d: nodeDN: %s\n",
             mctx.pid, tttctx->thrdNum, nodeDN);

  /*
   * Found the naming attribute used for nodeDN's rdn.
   */
  for (i=0 ; (nodeDN[i] != '=') && (nodeDN[i] != '\0') ; i++);
  if (nodeDN[i] == '\0')
  {
    printf ("ldclt[%d]: T%03d: Cannot extract naming attribute from %s\n",
		mctx.pid, tttctx->thrdNum, nodeDN);
    fflush (stdout);
    return (-1);
  }
  strncpy (attrName, nodeDN, i);
  attrName[i] = '\0';

  /*
   * Get the value of this rdn
   */
  for (j=i ; (nodeDN[j] != ',') && (nodeDN[j] != '\0') ; j++);
  if (nodeDN[j] == '\0')
  {
    printf ("ldclt[%d]: T%03d: Cannot extract naming attribute from %s\n",
		mctx.pid, tttctx->thrdNum, nodeDN);
    fflush (stdout);
    return (-1);
  }
  strncpy (attrVal, nodeDN+i+1, j-i-1);
  attrVal[j-i-1] = '\0';

  /*
   * What kind of entry should be create ?
   */
  if (!strcmp (attrName, "o"))
    objClass = "organization";
  else if (!strcmp (attrName, "ou"))
    objClass = "organizationalUnit";
  else if (!strcmp (attrName, "cn"))
    objClass = "organizationalRole";
  else
  {
    printf ("ldclt[%d]: T%03d: Don't know how to create entry when rdn is \"%s=%s\"\n",
		mctx.pid, tttctx->thrdNum, attrName, attrVal);
    fflush (stdout);
    return (-1);
  }

  /*
   * Maybe connect to the server ?
   */
  if (cnx == NULL)
  {
    const char *mech = LDAP_SASL_SIMPLE;
    const char *binddn = NULL;
    const char *passwd = NULL;
    struct berval cred = {0, NULL};
#if defined(USE_OPENLDAP)
    char *ldapurl = NULL;
#endif

    if (mctx.mode & VERY_VERBOSE)				/*JLS 14-12-00*/
      printf ("ldclt[%d]: T%03d: must connect to the server.\n",
               mctx.pid, tttctx->thrdNum);
#if defined(USE_OPENLDAP)
    ldapurl = PR_smprintf("ldap%s://%s:%d/",
			  (mctx.mode & SSL) ? "s" : "",
			  mctx.hostname, mctx.port);
    if ((ret = ldap_initialize(&tttctx->ldapCtx, ldapurl))) {
	printf ("ldclt[%d]: T%03d: Cannot ldap_initialize (%s), errno=%d ldaperror=%d:%s\n",
		mctx.pid, tttctx->thrdNum, ldapurl, errno, ret, my_ldap_err2string(ret));
	fflush (stdout);
	PR_smprintf_free(ldapurl);
	return (-1);
    }
    PR_smprintf_free(ldapurl);
    ldapurl = NULL;
#else /* !USE_OPENLDAP */
    /*
     * SSL is enabled ?
     */
    if (mctx.mode & SSL)
    {
      /*
       * LDAP session initialization in SSL mode
       * added by: B Kolics (11/10/00)
       */
      tttctx->ldapCtx = ldapssl_init(mctx.hostname, mctx.port, 1);
      if (mctx.mode & VERY_VERBOSE)
	printf ("ldclt[%d]: T%03d: After ldapssl_init (%s, %d), ldapCtx=0x%p\n",
		mctx.pid, tttctx->thrdNum, mctx.hostname, mctx.port,
		tttctx->ldapCtx);
      if (tttctx->ldapCtx == NULL)
      {
	printf ("ldclt[%d]: T%03d: Cannot ldapssl_init (%s, %d), errno=%d\n",
		mctx.pid, tttctx->thrdNum, mctx.hostname, mctx.port, errno);
	fflush (stdout);
	return (-1);
      }
      /*
       * Client authentication is used ?
       */
      if (mctx.mode & CLTAUTH)
	{
	 ret = ldapssl_enable_clientauth(tttctx->ldapCtx, "", mctx.keydbpin, mctx.cltcertname);
	 if (mctx.mode & VERY_VERBOSE)
	   printf 
	     ("ldclt[%d]: T%03d: After ldapssl_enable_clientauth (ldapCtx=0x%p, %s, %s)",
	      mctx.pid, tttctx->thrdNum, tttctx->ldapCtx, mctx.keydbpin,
	      mctx.cltcertname);
	 if (ret < 0)
	 {
	   printf 
	    ("ldclt[%d]: T%03d: Cannot ldapssl_enable_clientauth (ldapCtx=0x%p, %s, %s)",
	     mctx.pid, tttctx->thrdNum, tttctx->ldapCtx, mctx.keydbpin,
	     mctx.cltcertname);
	   fflush (stdout);
	   return (-1);
	 }
      }
    } else {
      /*
       * connection initialization in normal, unencrypted mode
       */
      cnx = ldap_init (mctx.hostname, mctx.port);
      if (cnx == NULL)
      {
	printf ("ldclt[%d]: T%03d: Cannot ldap_init (%s, %d), errno=%d\n",
		mctx.pid, tttctx->thrdNum, mctx.hostname, mctx.port, errno);
	fflush (stdout);
	return (-1);
      }
    }
#endif /* !USE_OPENLDAP */

    if (mctx.mode & CLTAUTH) {
      mech = "EXTERNAL";
      binddn = "";
      passwd = NULL;
    } else {
      binddn = tttctx->bufBindDN?tttctx->bufBindDN:mctx.bindDN;
      passwd = tttctx->bufPasswd?tttctx->bufPasswd:mctx.passwd;
      if (passwd) {
        cred.bv_val = (char *)passwd;
        cred.bv_len = strlen(passwd);
      }
    }

    if (mctx.mode & LDAP_V2)
      v2v3 = LDAP_VERSION2;
    else
      v2v3 = LDAP_VERSION3;

    ret = ldap_set_option (cnx, LDAP_OPT_PROTOCOL_VERSION, &v2v3);
    if (ret < 0)
    {
      printf ("ldclt[%d]: T%03d: Cannot ldap_set_option(LDAP_OPT_PROTOCOL_VERSION)\n",
		mctx.pid, tttctx->thrdNum);
      fflush (stdout);
      return (-1);
    }

    /*
     * Bind to the server
     */
    ret = ldap_sasl_bind_s (tttctx->ldapCtx, binddn, mech, &cred, NULL, NULL,
			    NULL);
    if (ret != LDAP_SUCCESS)
    {
      printf ("ldclt[%d]: T%03d: Cannot bind using mech [%s] (%s, %s), error=%d (%s)\n",
				mctx.pid, tttctx->thrdNum,
				mech ? mech : "SIMPLE",
				tttctx->bufBindDN ? tttctx->bufBindDN : "",
				tttctx->bufPasswd ? tttctx->bufPasswd : "",
				ret, my_ldap_err2string (ret));
      fflush (stdout);
      tttctx->exitStatus = EXIT_NOBIND;				/*JLS 25-08-00*/
      if (addErrorStat (ret) < 0)
	return (-1);
      return (-1);
    }
  }

  /*
   * Create the entry
   */
  attrs[0]  = NULL;	/* No attributes yet */
  nbAttribs = 0;	/* No attributes yet */
  attribute.mod_op     = LDAP_MOD_ADD;
  attribute.mod_type   = "objectclass";
  attribute.mod_values = strList1 (objClass);
  if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
    return (-1);
  attribute.mod_op     = LDAP_MOD_ADD;
  attribute.mod_type   = attrName;
  attribute.mod_values = strList1 (attrVal);
  if (addAttrib (attrs, nbAttribs++, &attribute) < 0)
    return (-1);

  /*
   * Add the entry
   * If it doesn't work, we will recurse on the nodeDN
   */
  ret = ldap_add_ext_s (cnx, nodeDN, attrs, NULL, NULL);
  if ((ret != LDAP_SUCCESS) && (ret != LDAP_ALREADY_EXISTS))
  {
    if (ret == LDAP_NO_SUCH_OBJECT)
    {								/*JLS 07-11-00*/
      printf ("ldclt[%d]: T%03d: Parent of %s doesn't exist, looping\n",
			mctx.pid, tttctx->thrdNum, nodeDN);
      if (createMissingNodes (tttctx, nodeDN, cnx) < 0)
	return (-1);
      else
      {
	/*
	 * Well, the upper node is created. Maybe we should now
	 * create the node requested for this instance of the function.
	 * Two solutions, retry a ldap_add_s() or recursive call to
	 * createMissingNodes()... Let's be simple and recurse ;-)
	 * Don't forget that the cnx was released in the previous call.
	 */
	cnx = NULL;
	return (createMissingNodes (tttctx, newDN, cnx));
      }
    }								/*JLS 07-11-00*/

    /*
     * Well, looks like it is more serious !
     */
    printf ("ldclt[%d]: T%03d: Cannot add (%s), error=%d (%s)\n",
	mctx.pid, tttctx->thrdNum, nodeDN, ret, my_ldap_err2string (ret));
    fflush (stdout);
    if (addErrorStat (ret) < 0)
      return (-1);
    return (-1);
  }

  /*
   * Note that error this node may exist, i.e. being just created
   * by another thread !
   * Memorize this operation only if the entry was really created.
   * Maybe we run in check mode, so be carreful...
   */
  if (ret != LDAP_ALREADY_EXISTS)
  {
    if (incrementNbOpers (tttctx) < 0)
      return (-1);
#ifdef SOLARIS							/*JLS 14-11-00*/
    if (mctx.slavesNb > 0)
      if (opAdd (tttctx, LDAP_REQ_ADD, nodeDN, attrs, NULL, NULL) < 0)
	return (-1);
#endif								/*JLS 14-11-00*/
  }
  else								/*JLS 15-12-00*/
  {
    if (mctx.mode & COUNT_EACH)		/*JLS 15-12-00*/
    {								/*JLS 15-12-00*/
      if (incrementNbOpers (tttctx) < 0)			/*JLS 15-12-00*/
	return (-1);						/*JLS 15-12-00*/
    }								/*JLS 15-12-00*/
  }

  /*
   * Ok, we succeed to create the entry ! or it already exist.
   * Don't forget to free the attributes and to release the cnx !!
   */
  if (freeAttrib (attrs) < 0)
    return (-1);

  ret = ldap_unbind_ext (cnx, NULL, NULL);
  if (ret != LDAP_SUCCESS)
  {
    fprintf (stderr, "ldclt[%d]: T%03d: cannot ldap_unbind(), error=%d (%s)\n",
			mctx.pid, tttctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    if (addErrorStat (ret) < 0)
      return (-1);
    return (-1);
  }

  /*
   * Ouf ! End of this function.
   */
  return (0);
}










/* ****************************************************************************
	FUNCTION :	getPending
	PURPOSE :	Get the pending results, and perform some basic controls
			on them.
	INPUT :		tttctx	= thread context
			timeout	= how many times wait for a result.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
getPending (
	thread_context	*tttctx,
	struct timeval	*timeout)
{
  LDAPMessage	*res;		/* LDAP async results */
  int		 ret;		/* Return values */
  int		 expected = 0;	/* Expect this type */
  char		*verb;		/* LDAP verb expected */
  int		 type;		/* Message type */
  int		 errcodep;	/* Async error code */
  int		 msgid;		/* Async message id */
  int		 msgOk;		/* Message read. */
  char		*addErrMsg;	/* Additional error msg */	/*JLS 03-08-00*/

  /*
   * Initialization
   */
  msgOk = 0;	/* No message received */
  if (tttctx->mode & ADD_ENTRIES)
  {
    expected = LDAP_RES_ADD;
    verb     = "ldap_add";
  }
  else
    if (tttctx->mode & DELETE_ENTRIES)
    {
      expected = LDAP_RES_DELETE;
      verb     = "ldap_delete";
    }
    else
      if (tttctx->mode & RENAME_ENTRIES)
      {
	expected = LDAP_RES_MODRDN;
	verb     = "ldap_rename";
      }
      else							/*JLS 21-11-00*/
	if (tttctx->mode & ATTR_REPLACE)			/*JLS 21-11-00*/
	{							/*JLS 21-11-00*/
	  expected = LDAP_RES_MODIFY;				/*JLS 21-11-00*/
	  verb     = "ldap_modify";				/*JLS 21-11-00*/
	}							/*JLS 21-11-00*/

  /*
   * Here, we are in asynchronous mode...
   * Too bad, lot of things to do here.
   * First, let's see if we are above the reading threshold.
   * This function may be called recursivelly to empty the input queue. When
   * it is used this way, the timeout is set to zero.
   */
  if ((timeout == &(mctx.timevalZero)) ||
      (tttctx->pendingNb >= mctx.asyncMin))
  {
    /*
     * Retrieve the next pending request
     * The result of ldap_result() may be -1 (error), 0 (timeout).
     * If timeout, well... let's ignore it and continue.
     */
    ret = ldap_result (tttctx->ldapCtx, LDAP_RES_ANY, 1, timeout, &res);
    if (ret != 0)
    {
      msgOk = 1;
      if (ret < 0)
      {
	if (!ignoreError (ret))
	{
	  msgOk = 0;
	  if (!(mctx.mode & QUIET))
	  {
	    printf ("ldclt[%d]: T%03d: Cannot ldap_result(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
	    fflush (stdout);
	  }
	}
	if (addErrorStat (ret) < 0)
	  return (-1);
      }
      else
      {
	/*
	 * Ensure it is what we expect to see...
	 */
	type  = ldap_msgtype (res);
	msgid = ldap_msgid (res);
	if (type != expected)
	{
	  msgOk = 0;
	  printf ("ldclt[%d]: T%03d: bad result type 0x%02x\n",
                   mctx.pid, tttctx->thrdNum, type);
	  fflush (stdout);
	  if (msgIdDel (tttctx, msgid, 1) < 0)
	    return (-1);
	  return (0);
	}

	/*
	 * Parse the result
	 */
	addErrMsg = NULL;					/*JLS 03-08-00*/
	ret = ldap_parse_result (tttctx->ldapCtx, res, &errcodep,
		NULL, &addErrMsg, NULL, NULL, 0);		/*JLS 03-08-00*/
	if (ret < 0)
	{
	  if (!ignoreError (ret))
	  {
	    msgOk = 0;
	    if (!(mctx.mode & QUIET))
	    {
	      printf ("ldclt[%d]: T%03d: Cannot ldap_parse_result(%s), error=%d (%s",
		mctx.pid, tttctx->thrdNum, msgIdStr (tttctx, msgid),
		ret, my_ldap_err2string (ret));
	      if ((addErrMsg != NULL) && (*addErrMsg != '\0'))	/*JLS 03-08-00*/
	      {							/*JLS 03-08-00*/
		printf (" - %s", addErrMsg);			/*JLS 03-08-00*/
		ldap_memfree (addErrMsg);			/*JLS 03-08-00*/
	      }							/*JLS 03-08-00*/
	      printf (")\n");					/*JLS 03-08-00*/
	      fflush (stdout);
	    }
	  }
	  if (msgIdDel (tttctx, msgid, 1) < 0)
	    return (-1);
	  if (addErrorStat (ret) < 0)
	    return (-1);
	}

	/*
	 * Ensure the operation was well performed
	 * Note that an error to be ignored should be considered as
	 * a good message received (cf boolean msgOk).
	 */
	if (errcodep != LDAP_SUCCESS)
	{
	  if (!ignoreError (ret))
	  {
	    msgOk = 0;
	    if (!(mctx.mode & QUIET))
	    {
	      printf ("ldclt[%d]: T%03d: Cannot %s(%s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, verb, msgIdStr (tttctx, msgid),
		errcodep, my_ldap_err2string (errcodep));
	      fflush (stdout);
	    }
	  }

	  /*
	   * Maybe we must create the intermediate nodes ?
	   */
	  if (((expected == LDAP_RES_ADD) || (expected == LDAP_RES_MODRDN)) &&
	      (errcodep == LDAP_NO_SUCH_OBJECT))
	  {
	    /*
	     * Attention, for the rename operation we will memorize the new
	     *            parent node and not the entry itself.
	     */
	    if (createMissingNodes (tttctx, msgIdStr (tttctx, msgid), NULL) < 0)
	    {
	      printf ("ldclt[%d]: T%03d: Cannot create the intermediate nodes for %s\n",
			mctx.pid, tttctx->thrdNum, msgIdStr (tttctx, msgid));
	      fflush (stdout);
	      return (-1);
	    }
	    if ((mctx.mode & VERBOSE) && (!(mctx.mode & QUIET)))
	    {
	      printf ("ldclt[%d]: T%03d: Intermediate nodes created for %s\n",
			mctx.pid, tttctx->thrdNum, msgIdStr (tttctx, msgid));
	      fflush (stdout);
	    }
	  }

	  /*
	   * Free the message's data
	   */
	  if (msgIdDel (tttctx, msgid, 1) < 0)
	    return (-1);
	  if (addErrorStat (errcodep) < 0)
	    return (-1);
	}
	else
	{
	  /*
	   * Ok, the operation is well performed !
	   * Maybe we are running in check mode ?
	   */
	  if (mctx.slavesNb == 0)
	  {
	    if (msgIdDel (tttctx, msgid, 1) < 0)
	      return (-1);
	  }
#ifdef SOLARIS							/*JLS 14-11-00*/
	  else
	  {
	    switch (expected)
	    {
	      case LDAP_RES_ADD:
		if (opAdd (tttctx, LDAP_REQ_ADD, msgIdDN(tttctx, msgid),
				msgIdAttribs(tttctx, msgid), NULL, NULL) < 0)
		  return (-1);
		break;
	      case LDAP_RES_DELETE:
		if (opAdd (tttctx, LDAP_REQ_DELETE, msgIdDN(tttctx, msgid),
					NULL, NULL, NULL) < 0)
		  return (-1);
		break;
	      case LDAP_RES_MODRDN:
		if (opAdd (tttctx, LDAP_REQ_DELETE, msgIdDN(tttctx, msgid),
					NULL, NULL, NULL) < 0)
/*
   TBC : memorize the newRdn and newParent
 */
		  return (-1);
		break;
	    }
	    if (msgIdDel (tttctx, msgid, 1) < 0)
	      return (-1);
	  }
#endif /* SOLARIS */						/*JLS 14-11-00*/
	}

	/*
	 * Ok, it is a "SUCCESS" message.
	 * Don't forget to free the returned message !
	 */
	tttctx->pendingNb--;
	if ((ret = ldap_msgfree (res)) < 0)
	{
	  if (!((mctx.mode & QUIET) && ignoreError (ret)))
	  {
	    printf ("ldclt[%d]: T%03d: Cannot ldap_msgfree(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
	    fflush (stdout);
	  }
	  if (addErrorStat (ret) < 0)
	    return (-1);
	}
      }
    }
  }

  /*
   * Maybe recurse to read the next message ?
   */
  if (msgOk)
    return (getPending (tttctx, &(mctx.timevalZero)));

  return (0);
}















/* ****************************************************************************
	FUNCTION :	doRename
	PURPOSE :	Perform an ldap_rename() operation.
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doRename (
	thread_context	*tttctx)
{
  char	 oldDn[MAX_DN_LENGTH];	/* DN of the entry to rename */
  int	 ret;			/* Return values */
  int	 retry;			/* Retry after createMissingNodes() */
  int	 retryed;		/* If already retryed */
  int	 msgid;			/* For asynchronous mode */

  /*
   * Connection to the server
   * The function connectToServer() will take care of the various connection/
   * disconnection, bind/unbind/close etc... requested by the user.
   * The cost is one more function call in this application, but the
   * resulting source code will be much more easiest to maintain.
   */
  if (connectToServer (tttctx) < 0)				/*JLS 18-12-00*/
    return (-1);						/*JLS 18-12-00*/
  if (!(tttctx->binded))					/*JLS 18-12-00*/
    return (0);							/*JLS 18-12-00*/

  /*
   * Build a random entry name. Of course, we are not sure it exist
   * but there is no other simple solution...
   * Anyway, the result is: tttctx->bufFilter , tttctx->bufBaseDN
   */
  if (buildRandomRdnOrFilter (tttctx) < 0)
    return (-1);
  strcpy (oldDn, tttctx->bufFilter);
  strcat (oldDn, ",");
  strcat (oldDn, tttctx->bufBaseDN);

  /*
   * Now, build a random new name for this entry
   */
  if (buildRandomRdnOrFilter (tttctx) < 0)
    return (-1);

  /*
   * Do the rename
   * Maybe we are in synchronous mode ?
   * We won't try to recover from errors in this function, because the
   * way the library will tell the application that the target parent
   * (tttctx->bufBaseDN) doesn't exist is LDAP_PROTOCOL_ERROR that may
   * report as well three other problems.
   */
  if (!(mctx.mode & ASYNC))
  {
    /*
     * We will retry with this entry...
     * We need to memorize we have already retry to add an entry, because
     * ldap_rename_s() returns LDAP_PROTOCOL_ERROR if the randomly chosen
     * entry we try to rename does not exist ! If we don't, the program will
     * loop infinitely on this.
     * The only way is to decide that LDAP_PROTOCOL_ERROR is not a *real*
     * error, that is detailed below :
     * The possible meanings of LDAP_PROTOCOL_ERROR are :
     *  - BER problem, unlike to happen
     *  - newrdn is invalid, also unlike to happen
     *  - ldclt is not running ldap v3 - ok, we could leave with this.
     *  - the newparent is invalid. I thing that we could take this for
     *    "doesn't exist"...
     */
    retry   = 1;
    retryed = 0;
    while (retry && !retryed)
    {
      if (mctx.mode & WITH_NEWPARENT)				/*JLS 15-12-00*/
	ret = ldap_rename_s (tttctx->ldapCtx, oldDn,
			tttctx->bufFilter, tttctx->bufBaseDN, 1, NULL, NULL);
      else							/*JLS 15-12-00*/
	ret = ldap_rename_s (tttctx->ldapCtx, oldDn,		/*JLS 15-12-00*/
			tttctx->bufFilter, NULL, 1, NULL, NULL);/*JLS 15-12-00*/
      if (ret == LDAP_SUCCESS)					/*JLS 15-12-00*/
      {
	retry = 0;
	if (incrementNbOpers (tttctx) < 0)	/* Memorize operation */
	  return (-1);
#ifdef SOLARIS							/*JLS 14-11-00*/
	if (mctx.slavesNb > 0)
	  if (opAdd (tttctx, LDAP_REQ_MODRDN, oldDn, NULL,
				tttctx->bufFilter, tttctx->bufBaseDN) < 0)
	    return (-1);
#endif								/*JLS 14-11-00*/
      }
      else	/* Error */
      {
	if (!((mctx.mode & QUIET) && ignoreError (ret)))
	{
	  printf ("ldclt[%d]: T%03d: Cannot rename (%s, %s, %s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, oldDn, tttctx->bufFilter, tttctx->bufBaseDN,
		ret, my_ldap_err2string (ret));
	  fflush (stdout);
	}
	if (addErrorStat (ret) < 0)
	  return (-1);

	/*
	 * Check what to do depending on the error.
	 * From the c-sdk function description, if the parent node of the
	 * new dn (i.e. tttctx->bufBaseDN) doesn't exist, the error returned
	 * is LDAP_PROTOCOL_ERROR that may report as well three other problems.
	 * See discussion above.
	 */

	/*
	 * Maybe we should count each operation ?
	 */
	if ((mctx.mode & COUNT_EACH) &&				/*JLS 18-12-00*/
	    ((ret == LDAP_PROTOCOL_ERROR) ||			/*JLS 18-12-00*/
	     (ret == LDAP_NO_SUCH_OBJECT) ||			/*JLS 18-12-00*/
	     (ret == LDAP_ALREADY_EXISTS)))			/*JLS 18-12-00*/
	{							/*JLS 18-12-00*/
	  if (incrementNbOpers (tttctx) < 0)			/*JLS 18-12-00*/
	    return (-1);					/*JLS 18-12-00*/
	}							/*JLS 18-12-00*/

	/*
	 * Maybe we must create the intermediate nodes ?
	 */
	if (ret != LDAP_PROTOCOL_ERROR)				/*JLS 15-12-00*/
	  retry = 0;
	else
	{
	  if (createMissingNodes (tttctx, tttctx->bufBaseDN, NULL) < 0)
	  {
	    retry = 0;
	    printf ("ldclt[%d]: T%03d: Cannot create the intermediate nodes for %s\n",
		mctx.pid, tttctx->thrdNum, tttctx->bufBaseDN);
	    fflush (stdout);
	    return (-1);
	  }
	  if ((mctx.mode & VERBOSE) && (!(mctx.mode & QUIET)))
	  {
	    printf ("ldclt[%d]: T%03d: Intermediate nodes created for %s\n",
		mctx.pid, tttctx->thrdNum, tttctx->bufBaseDN);
	    fflush (stdout);
	  }
	  retryed = 1;
	}
      }
    }

    /*
     * End of synchronous operations
     */
    return (0);
  }

  /*
   * Here, we are in asynchronous mode...
   * Too bad, lot of things to do here.
   * First, let's see if we are above the reading threshold.
   */
  if (getPending (tttctx, &(mctx.timeval)) < 0)
    return (-1);

  /*
   * Maybe we may send another request ?
   * Well... there is no proper way to retrieve the error number for
   * this, so I guess I may use direct access to the ldap context
   * to read the field ld_errno.
   */
  if (tttctx->pendingNb > mctx.asyncMax)
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 1;
      printf ("ldclt[%d]: T%03d: Max pending request hit.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }
  }
  else
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 0;
      printf ("ldclt[%d]: T%03d: Restart sending.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }

    if (mctx.mode & WITH_NEWPARENT)				/*JLS 15-12-00*/
      ret = ldap_rename (tttctx->ldapCtx, oldDn,
			tttctx->bufFilter, tttctx->bufBaseDN,
			1, NULL, NULL, &msgid);
    else							/*JLS 15-12-00*/
      ret = ldap_rename (tttctx->ldapCtx, oldDn,		/*JLS 15-12-00*/
			tttctx->bufFilter, NULL,		/*JLS 15-12-00*/
			1, NULL, NULL, &msgid);			/*JLS 15-12-00*/
    if (ret < 0)
    {
      if (ldap_get_option (tttctx->ldapCtx, LDAP_OPT_ERROR_NUMBER, &ret) < 0)
      {
	printf ("ldclt[%d]: T%03d: Cannot ldap_get_option(LDAP_OPT_ERROR_NUMBER)\n",
		mctx.pid, tttctx->thrdNum);
	fflush (stdout);
	return (-1);
      }
      else
      {
	if (!((mctx.mode & QUIET) && ignoreError (ret)))
	{
	  printf ("ldclt[%d]: T%03d: Cannot rename (%s, %s, %s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, oldDn, tttctx->bufFilter, tttctx->bufBaseDN,
		ret, my_ldap_err2string (ret));
	  fflush (stdout);
	}
	if (addErrorStat (ret) < 0)
	  return (-1);

	/*
	 * Maybe we should count each operation ?
	 */
	if ((mctx.mode & COUNT_EACH) &&				/*JLS 18-12-00*/
	    ((ret == LDAP_PROTOCOL_ERROR) ||			/*JLS 18-12-00*/
	     (ret == LDAP_NO_SUCH_OBJECT) ||			/*JLS 18-12-00*/
	     (ret == LDAP_ALREADY_EXISTS)))			/*JLS 18-12-00*/
	{							/*JLS 18-12-00*/
	  if (incrementNbOpers (tttctx) < 0)			/*JLS 18-12-00*/
	    return (-1);					/*JLS 18-12-00*/
	}							/*JLS 18-12-00*/

	/*
	 * Maybe we must create the intermediate nodes ?
	 * Question: is it likely probable that such error is returned
	 *           by the server on a *asynchornous* operation ?
	 * See discussion about the error returned in the synchronous section
	 * of this function.
	 */
	if (ret == LDAP_PROTOCOL_ERROR)				/*JLS 15-12-00*/
	{							/*JLS 15-12-00*/
	  if (createMissingNodes (tttctx, tttctx->bufBaseDN, NULL) < 0)
	    return (-1);
	}							/*JLS 15-12-00*/
      }
    }
    else
    {
      /*
       * Memorize the operation
       */
/*
   TBC : I'm not sure what will happen if we call msgIdAdd() with a NULL
	 pointer as attribs !!!!!
 */
      if (msgIdAdd (tttctx, msgid, tttctx->bufBaseDN, oldDn, NULL) < 0)
	return (-1);
      if (incrementNbOpers (tttctx) < 0)
	return (-1);
      tttctx->pendingNb++;
    }
  }

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: pendingNb=%d\n",
             mctx.pid, tttctx->thrdNum, tttctx->pendingNb);

  /*
   * End of asynchronous operation... and also end of function.
   */
  return (0);
}







						/* New */	/*JLS 02-04-01*/
/* ****************************************************************************
	FUNCTION :	ldclt_write_genldif
	PURPOSE :	Implements buffered write to speed up -e genldif
			implementation.
	INPUT :		str	= string to write.
			lgth	= length of this string.
	OUTPUT :	None.
	RETURN :	None.
	DESCRIPTION :	Usual write() function is unbuffered on Solaris and
			thus really slow down the whole process. Using this
			function allow ldclt to perform 8 times faster.
			We cannot use fprintf() because of portability issues
			regarding large files support.
 *****************************************************************************/
char	*ldclt_write_genldif_buf = NULL;
char	*ldclt_write_genldif_pt;
int	 ldclt_write_genldif_nb;

void
ldclt_flush_genldif (void)
{
  write (mctx.genldifFile, ldclt_write_genldif_buf, ldclt_write_genldif_nb);
  ldclt_write_genldif_pt = ldclt_write_genldif_buf;
  ldclt_write_genldif_nb = 0;
}

void
ldclt_write_genldif (
	char	*str,
	int	 lgth)
{
  /*
   * First call ?
   */
  if (ldclt_write_genldif_buf == NULL)
  {
    ldclt_write_genldif_buf = (char *) malloc (65536);
    ldclt_write_genldif_pt  = ldclt_write_genldif_buf;
    ldclt_write_genldif_nb  = 0;
  }

  /*
   * Buffer full ?
   */
  if (ldclt_write_genldif_nb + lgth >= 65536)
    ldclt_flush_genldif();

  /*
   * Add to the buffer
   */
  strncpy (ldclt_write_genldif_pt, str, lgth);
  ldclt_write_genldif_pt += lgth;
  ldclt_write_genldif_nb += lgth;
}









						/* New */	/*JLS 19-03-01*/
/* ****************************************************************************
	FUNCTION :	doGenldif
	PURPOSE :	Create a ldif file from the given parameters.
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doGenldif (
	thread_context	*tttctx)
{
  char		 newDn[MAX_DN_LENGTH];	/* DN of the new entry */
  LDAPMod	*attrs[MAX_ATTRIBS];	/* Attributes of this entry */
  int		 i;			/* For the loop */

  /*
   * Build a new entry
   */
  if (buildNewEntry (tttctx, newDn, attrs) < 0)
    return (-1);

  /*
   * Dump this entry.
   * Using a buffer speeds writes 3 times faster.
   */
  ldclt_write_genldif ("dn: ", 4);				/*JLS 02-04-01*/
  ldclt_write_genldif (newDn, strlen (newDn));			/*JLS 02-04-01*/
  ldclt_write_genldif ("\n", 1);				/*JLS 02-04-01*/
  for (i=0 ; attrs[i]!=NULL ; i++)				/*JLS 02-04-01*/
  {								/*JLS 02-04-01*/
    ldclt_write_genldif (attrs[i]->mod_type, 			/*JLS 02-04-01*/
			strlen (attrs[i]->mod_type));		/*JLS 02-04-01*/
    ldclt_write_genldif (": ", 2);				/*JLS 02-04-01*/
    ldclt_write_genldif (attrs[i]->mod_values[0], 		/*JLS 02-04-01*/
			strlen (attrs[i]->mod_values[0]));	/*JLS 02-04-01*/
    ldclt_write_genldif ("\n", 1);				/*JLS 02-04-01*/
  }								/*JLS 02-04-01*/
  ldclt_write_genldif ("\n", 1);				/*JLS 02-04-01*/

  /*
   * Increment counters
   */
  if (incrementNbOpers (tttctx) < 0)				/*JLS 28-03-01*/
    return (-1);						/*JLS 28-03-01*/

  /*
   * Free the memory and return
   */
  if (freeAttrib (attrs) < 0)
    return (-1);

  return (0);
}












/* ****************************************************************************
	FUNCTION :	doAddEntry
	PURPOSE :	Perform an ldap_add() operation.
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doAddEntry (
	thread_context	*tttctx)
{
  char		 newDn[MAX_DN_LENGTH];	/* DN of the new entry */
  LDAPMod	*attrs[MAX_ATTRIBS];	/* Attributes of this entry */
  int		 ret;			/* Return values */
  int		 retry;			/* Retry after createMissingNodes() */

  /*
   * Connection to the server
   * The function connectToServer() will take care of the various connection/
   * disconnection, bind/unbind/close etc... requested by the user.
   * The cost is one more function call in this application, but the
   * resulting source code will be much more easiest to maintain.
   */
  if (connectToServer (tttctx) < 0)
    return (-1);
  if (!(tttctx->binded))					/*JLS 18-12-00*/
    return (0);							/*JLS 18-12-00*/

  /*
   * Do the add
   * Maybe we are in synchronous mode ?
   */
  if (!(mctx.mode & ASYNC))
  {
    /*
     * Build the new entry
     */
    if (buildNewEntry (tttctx, newDn, attrs) < 0)
      return (-1);

    /*
     * We will retry with this entry...
     */
    retry = 1;
    while (retry)
    {
      ret = ldap_add_ext_s (tttctx->ldapCtx, newDn, attrs, NULL, NULL);
      if (ret != LDAP_SUCCESS)
      {
	if (!((mctx.mode & QUIET) && ignoreError (ret)))
	{
	  printf ("ldclt[%d]: T%03d: Cannot add (%s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, newDn, ret, my_ldap_err2string (ret));
	  fflush (stdout);
	}
	if (addErrorStat (ret) < 0)
	  return (-1);

	/*
	 * Maybe we must create the intermediate nodes ?
	 */
	if (ret != LDAP_NO_SUCH_OBJECT)
	{
	  if ((ret == LDAP_ALREADY_EXISTS) && 			/*JLS 15-12-00*/
	      (mctx.mode & COUNT_EACH))				/*JLS 15-12-00*/
	  {							/*JLS 15-12-00*/
	    if (incrementNbOpers (tttctx) < 0)			/*JLS 15-12-00*/
	      return (-1);					/*JLS 15-12-00*/
	  }							/*JLS 15-12-00*/
	  retry = 0;
	}
	else
	{
	  if (createMissingNodes (tttctx, newDn, NULL) < 0)
	  {
	    retry = 0;
	    printf ("ldclt[%d]: T%03d: Cannot create the intermediate nodes for %s\n",
		mctx.pid, tttctx->thrdNum, newDn);
	    fflush (stdout);
	    return (-1);
	  }
	  if ((mctx.mode & VERBOSE) && (!(mctx.mode & QUIET)))
	  {
	    printf ("ldclt[%d]: T%03d: Intermediate nodes created for %s\n",
		mctx.pid, tttctx->thrdNum, newDn);
	    fflush (stdout);
	  }
	}
      }
      else
      {
	retry = 0;
	if (incrementNbOpers (tttctx) < 0)	/* Memorize operation */
	  return (-1);
#ifdef SOLARIS							/*JLS 14-11-00*/
	if (mctx.slavesNb > 0)
	  if (opAdd (tttctx, LDAP_REQ_ADD, newDn, attrs, NULL, NULL) < 0)
	    return (-1);
#endif								/*JLS 14-11-00*/
      }
    }

    /*
     * Free the attributes
     */
    if (freeAttrib (attrs) < 0)
      return (-1);

    /*
     * End of synchronous operations
     */
    return (0);
  }

  /*
   * Here, we are in asynchronous mode...
   * Too bad, lot of things to do here.
   * First, let's see if we are above the reading threshold.
   */
  if (getPending (tttctx, &(mctx.timeval)) < 0)
    return (-1);

  /*
   * Maybe we may send another request ?
   * Well... there is no proper way to retrieve the error number for
   * this, so I guess I may use direct access to the ldap context
   * to read the field ld_errno.
   */
  if (tttctx->pendingNb > mctx.asyncMax)
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 1;
      printf ("ldclt[%d]: T%03d: Max pending request hit.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }
  }
  else
  {
    int msgid = 0;

    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 0;
      printf ("ldclt[%d]: T%03d: Restart sending.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }

    /*
     * Build the new entry
     */
    if (buildNewEntry (tttctx, newDn, attrs) < 0)
      return (-1);

    ret = ldap_add_ext (tttctx->ldapCtx, newDn, attrs, NULL, NULL, &msgid);
    if (ret < 0)
    {
      if (ldap_get_option (tttctx->ldapCtx, LDAP_OPT_ERROR_NUMBER, &ret) < 0)
      {
	printf ("ldclt[%d]: T%03d: Cannot ldap_get_option(LDAP_OPT_ERROR_NUMBER)\n",
		mctx.pid, tttctx->thrdNum);
	fflush (stdout);
	return (-1);
      }
      else
      {
	if (!((mctx.mode & QUIET) && ignoreError (ret)))
	{
	  printf ("ldclt[%d]: T%03d: Cannot add(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
	  fflush (stdout);
	}
	if (addErrorStat (ret) < 0)
	  return (-1);

	/*
	 * Maybe we must create the intermediate nodes ?
	 * Question: is it likely probable that sush error is returned
	 *           by the server on a *asynchornous* operation ?
	 */
	if (ret == LDAP_NO_SUCH_OBJECT)
	  if (createMissingNodes (tttctx, newDn, NULL) < 0)
	    return (-1);
	if ((ret == LDAP_ALREADY_EXISTS) && 			/*JLS 15-12-00*/
	    (mctx.mode & COUNT_EACH))				/*JLS 15-12-00*/
	{							/*JLS 15-12-00*/
	  if (incrementNbOpers (tttctx) < 0)			/*JLS 15-12-00*/
	    return (-1);					/*JLS 15-12-00*/
	}							/*JLS 15-12-00*/
      }
    }
    else
    {
      /*
       * Memorize the operation
       */
      if (msgIdAdd (tttctx, msgid, newDn, newDn, attrs) < 0)
	return (-1);
      if (incrementNbOpers (tttctx) < 0)
	return (-1);
      tttctx->pendingNb++;
    }
  }

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: pendingNb=%d\n",
             mctx.pid, tttctx->thrdNum, tttctx->pendingNb);

  /*
   * End of asynchronous operation... and also end of function.
   */
  return (0);
}











/* ****************************************************************************
	FUNCTION :	doAttrReplace
	PURPOSE :	Perform an ldap_modify() operation, to replace an
			attribute of the entry.
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doAttrReplace (
	thread_context	*tttctx)
{
  char		 newDn[MAX_DN_LENGTH];	/* DN of the new entry */
  LDAPMod	*attrs[MAX_ATTRIBS];	/* Attributes of this entry */
  int		 ret;			/* Return values */
  int		 msgid;			/* For asynchronous mode */

  /*
   * Connection to the server
   * The function connectToServer() will take care of the various connection/
   * disconnection, bind/unbind/close etc... requested by the user.
   * The cost is one more function call in this application, but the
   * resulting source code will be much more easiest to maintain.
   */
  if (connectToServer (tttctx) < 0)
    return (-1);
  if (!(tttctx->binded))					/*JLS 18-12-00*/
    return (0);							/*JLS 18-12-00*/

  /*
   * Do the modify
   * Maybe we are in synchronous mode ?
   */
  if (!(mctx.mode & ASYNC))
  {
    /*
     * Build the new entry
     */
    if (buildNewModAttrib (tttctx, newDn, attrs) < 0)
      return (-1);

    /*
     * We will modify this entry
     */
    ret = ldap_modify_ext_s (tttctx->ldapCtx, newDn, attrs, NULL, NULL);
    if (ret != LDAP_SUCCESS)
    {
      if (!((mctx.mode & QUIET) && ignoreError (ret)))
      {
	printf ("ldclt[%d]: T%03d: Cannot modify (%s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, newDn, ret, my_ldap_err2string (ret));
	fflush (stdout);
      }
      if (addErrorStat (ret) < 0)
	return (-1);
    }
    else
    {
      if (incrementNbOpers (tttctx) < 0)	/* Memorize operation */
	return (-1);
    }

    /*
     * Free the attributes
     */
    if (freeAttrib (attrs) < 0)
      return (-1);

    /*
     * End of synchronous operations
     */
    return (0);
  }

  /*
   * Here, we are in asynchronous mode...
   * Too bad, lot of things to do here.
   * First, let's see if we are above the reading threshold.
   */
  if (getPending (tttctx, &(mctx.timeval)) < 0)
    return (-1);

  /*
   * Maybe we may send another request ?
   * Well... there is no proper way to retrieve the error number for
   * this, so I guess I may use direct access to the ldap context
   * to read the field ld_errno.
   */
  if (tttctx->pendingNb > mctx.asyncMax)
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 1;
      printf ("ldclt[%d]: T%03d: Max pending request hit.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }
  }
  else
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 0;
      printf ("ldclt[%d]: T%03d: Restart sending.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }

    /*
     * Build the new entry
     */
    if (buildNewModAttrib (tttctx, newDn, attrs) < 0)
      return (-1);

    ret = ldap_modify_ext (tttctx->ldapCtx, newDn, attrs, NULL, NULL, &msgid);
    if (ret != LDAP_SUCCESS)
    {
      if (!((mctx.mode & QUIET) && ignoreError (ret)))
      {
	printf ("ldclt[%d]: T%03d: Cannot modify(%s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, newDn, ret, my_ldap_err2string (ret));
	fflush (stdout);
      }
      if (addErrorStat (ret) < 0)
	return (-1);
    }
    else
    {
      /*
       * Memorize the operation
       */
      if (msgIdAdd (tttctx, msgid, newDn, newDn, attrs) < 0)
	return (-1);
      if (incrementNbOpers (tttctx) < 0)
	return (-1);
      tttctx->pendingNb++;
    }
  }

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: pendingNb=%d\n",
             mctx.pid, tttctx->thrdNum, tttctx->pendingNb);

  /*
   * End of asynchronous operation... and also end of function.
   */
  return (0);
}



/* ****************************************************************************
	FUNCTION :	doAttrFileReplace
	PURPOSE :	Perform an ldap_modify() operation, to replace an
			attribute of the entry with content read from file .
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doAttrFileReplace (
	thread_context	*tttctx)
{
  char		 newDn[MAX_DN_LENGTH];	/* DN of the new entry */
  LDAPMod	*attrs[MAX_ATTRIBS];	/* Attributes of this entry */
  int		 ret;			/* Return values */
  int		 msgid;			/* For asynchronous mode */

  /*
   * Connection to the server
   * The function connectToServer() will take care of the various connection/
   * disconnection, bind/unbind/close etc... requested by the user.
   * The cost is one more function call in this application, but the
   * resulting source code will be much more easiest to maintain.
   */
  if (connectToServer (tttctx) < 0)	/* if connection is being established, */
    return (-1);			/* then tttctx->ldapCtx would exist and holds connection */
  if (!(tttctx->binded))
    return (0);

  /*
   * Do the modify
   * Maybe we are in synchronous mode ?
   */
  if (!(mctx.mode & ASYNC))
  {
    /*
     * Build the new entry
     */
    if (buildNewModAttribFile (tttctx, newDn, attrs) < 0)
      return (-1);

    /*
     * We will modify this entry
     */
    ret = ldap_modify_ext_s (tttctx->ldapCtx, newDn, attrs, NULL, NULL);
    if (ret != LDAP_SUCCESS)
    {
      if (!((mctx.mode & QUIET) && ignoreError (ret)))
      {
	printf ("ldclt[%d]: T%03d: AttriFileReplace Error Cannot modify (%s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, newDn, ret, my_ldap_err2string (ret));
	fflush (stdout);
      }
      if (addErrorStat (ret) < 0)
	return (-1);
    }
    else
    {
      printf ("ldclt[%d]: T%03d: AttriFileReplace modify (%s) success ,\n",
		mctx.pid, tttctx->thrdNum, newDn);
      if (incrementNbOpers (tttctx) < 0)	/* Memorize operation */
	return (-1);
    }

    /*
     * Free the attributes
     */
    if (freeAttrib (attrs) < 0)
      return (-1);

    /*
     * End of synchronous operations
     */
    return (0);
  }

  /*
   * Here, we are in asynchronous mode...
   * Too bad, lot of things to do here.
   * First, let's see if we are above the reading threshold.
   */
  if (getPending (tttctx, &(mctx.timeval)) < 0)
    return (-1);

  /*
   * Maybe we may send another request ?
   * Well... there is no proper way to retrieve the error number for
   * this, so I guess I may use direct access to the ldap context
   * to read the field ld_errno.
   */
  if (tttctx->pendingNb > mctx.asyncMax)
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 1;
      printf ("ldclt[%d]: T%03d: Max pending request hit.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }
  }
  else
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 0;
      printf ("ldclt[%d]: T%03d: Restart sending.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }

    /*
     * Build the new entry
     */
    if (buildNewModAttrib (tttctx, newDn, attrs) < 0)
      return (-1);

    ret = ldap_modify_ext (tttctx->ldapCtx, newDn, attrs, NULL, NULL, &msgid);
    if (ret != LDAP_SUCCESS)
    {
      if (!((mctx.mode & QUIET) && ignoreError (ret)))
      {
	printf ("ldclt[%d]: T%03d: Cannot modify(%s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, newDn, ret, my_ldap_err2string (ret));
	fflush (stdout);
      }
      if (addErrorStat (ret) < 0)
	return (-1);
    }
    else
    {
      /*
       * Memorize the operation
       */
      if (msgIdAdd (tttctx, msgid, newDn, newDn, attrs) < 0)
	return (-1);
      if (incrementNbOpers (tttctx) < 0)
	return (-1);
      tttctx->pendingNb++;
    }
  }

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: pendingNb=%d\n",
             mctx.pid, tttctx->thrdNum, tttctx->pendingNb);

  /*
   * End of asynchronous operation... and also end of function.
   */
  return (0);
}


/* ****************************************************************************
	FUNCTION :	doDeleteEntry
	PURPOSE :	Perform an ldap_delete() operation.
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doDeleteEntry (
	thread_context	*tttctx)
{
  int	  ret;			/* Return values */
  char	  delDn[MAX_DN_LENGTH];	/* The entry to delete */

  /*
   * Connection to the server
   * The function connectToServer() will take care of the various connection/
   * disconnection, bind/unbind/close etc... requested by the user.
   * The cost is one more function call in this application, but the
   * resulting source code will be much more easiest to maintain.
   */
  if (connectToServer (tttctx) < 0)
    return (-1);
  if (!(tttctx->binded))					/*JLS 18-12-00*/
    return (0);							/*JLS 18-12-00*/

  /*
   * Do the delete
   * Maybe we are in synchronous mode ?
   */
  if (!(mctx.mode & ASYNC))
  {
    /*
     * Random (or incremental) creation of the rdn to delete.
     * The resulting rdn is in tttctx->bufFilter.
     */
    if (buildRandomRdnOrFilter (tttctx) < 0)
      return (-1);
    strcpy (delDn, tttctx->bufFilter);
    strcat (delDn, ",");
    strcat (delDn, tttctx->bufBaseDN);

    ret = ldap_delete_ext_s (tttctx->ldapCtx, delDn, NULL, NULL);
    if (ret != LDAP_SUCCESS)
    {
      if (!((mctx.mode & QUIET) && ignoreError (ret)))
      {
	printf ("ldclt[%d]: T%03d: Cannot ldap_delete (%s), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, delDn, ret, my_ldap_err2string (ret));
	fflush (stdout);
      }
      if (addErrorStat (ret) < 0)
	return (-1);
      if ((ret == LDAP_NO_SUCH_OBJECT) && 			/*JLS 15-12-00*/
	  (mctx.mode & COUNT_EACH))				/*JLS 15-12-00*/
      {								/*JLS 15-12-00*/
	if (incrementNbOpers (tttctx) < 0)			/*JLS 15-12-00*/
	  return (-1);						/*JLS 15-12-00*/
      }								/*JLS 15-12-00*/
    }
    else
    {
      if (incrementNbOpers (tttctx) < 0)	/* Memorize operation */
	return (-1);
#ifdef SOLARIS							/*JLS 14-11-00*/
      if (mctx.slavesNb > 0)
	if (opAdd (tttctx, LDAP_REQ_DELETE, delDn, NULL, NULL, NULL) < 0)
	  return (-1);
#endif								/*JLS 14-11-00*/
    }

    /*
     * End of synchronous operations
     */
    return (0);
  }

  /*
   * Here, we are in asynchronous mode...
   * Too bad, lot of things to do here.
   * First, let's see if we are above the reading threshold.
   */
  if (getPending (tttctx, &(mctx.timeval)) < 0)
    return (-1);

  /*
   * Maybe we may send another request ?
   * Well... there is no proper way to retrieve the error number for
   * this, so I guess I may use direct access to the ldap context
   * to read the field ld_errno.
   */
  if (tttctx->pendingNb > mctx.asyncMax)
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 1;
      printf ("ldclt[%d]: T%03d: Max pending request hit.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }
  }
  else
  {
    int msgid = 0;

    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 0;
      printf ("ldclt[%d]: T%03d: Restart sending.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }

    /*
     * Random (or incremental) creation of the rdn to delete.
     * The resulting rdn is in tttctx->bufFilter.
     */
    if (buildRandomRdnOrFilter (tttctx) < 0)
      return (-1);
    strcpy (delDn, tttctx->bufFilter);
    strcat (delDn, ",");
    strcat (delDn, tttctx->bufBaseDN);

    ret = ldap_delete_ext (tttctx->ldapCtx, delDn, NULL, NULL, &msgid);
    if (ret < 0)
    {
      if (ldap_get_option (tttctx->ldapCtx, LDAP_OPT_ERROR_NUMBER, &ret) < 0)
      {
	printf ("ldclt[%d]: T%03d: Cannot ldap_get_option(LDAP_OPT_ERROR_NUMBER)\n",
		mctx.pid, tttctx->thrdNum);
	fflush (stdout);
	return (-1);
      }
      else
      {
	if (!((mctx.mode & QUIET) && ignoreError (ret)))
	{
	  printf ("ldclt[%d]: T%03d: Cannot ldap_delete(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
	  fflush (stdout);
	}
	if (addErrorStat (ret) < 0)
	  return (-1);
	if ((ret == LDAP_NO_SUCH_OBJECT) && 			/*JLS 15-12-00*/
	    (mctx.mode & COUNT_EACH))				/*JLS 15-12-00*/
	{							/*JLS 15-12-00*/
	  if (incrementNbOpers (tttctx) < 0)			/*JLS 15-12-00*/
	    return (-1);					/*JLS 15-12-00*/
	}							/*JLS 15-12-00*/
      }
    }
    else
    {
      /*
       * Memorize the operation
       */
      if (incrementNbOpers (tttctx) < 0)
	return (-1);
      tttctx->pendingNb++;
    }
  }

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: pendingNb=%d\n",
             mctx.pid, tttctx->thrdNum, tttctx->pendingNb);

  /*
   * End of asynchronous operation... and also end of doDeleteEntry().
   */
  return (0);
}








					/* New function */	/*JLS 04-05-01*/
/* ****************************************************************************
	FUNCTION :	doBindOnly
	PURPOSE :	Perform only bind/unbind operations.
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doBindOnly (
	thread_context	*tttctx)
{
  /*
   * Connection to the server
   * The function connectToServer() will take care of the various connection/
   * disconnection, bind/unbind/close etc... requested by the user.
   * The cost is one more function call in this application, but the
   * resulting source code will be much more easiest to maintain.
   */
  if (connectToServer (tttctx) < 0)
    return (-1);

  /* don't count failed binds unless counteach option is used */
  if (!(tttctx->binded) && !(mctx.mode & COUNT_EACH))
    return (0);

  if (incrementNbOpers (tttctx) < 0)
    return (-1);

  return (0);
}






/* ****************************************************************************
	FUNCTION :	doExactSearch
	PURPOSE :	Perform one exact search operation.
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doExactSearch (
	thread_context	*tttctx)
{
  int		  ret;		/* Return value */
  LDAPMessage	 *res;		/* LDAP results */
  LDAPMessage	 *e;		/* LDAP results */
  char		**attrlist;	/* Attribs list */		/*JLS 15-03-01*/
  LDAPControl **ctrlsp = NULL, *ctrls[2], *dctrl = NULL; /* derefence control */

  /* the following variables are used for response parsing */
  int             msgtype, parse_rc , rc ; /* for search result parsing */
  char            *matcheddn, *errmsg, *dn ;
  LDAPControl     **resctrls; 
  LDAPControl     **rcp;

  /*
   * Connection to the server
   * The function connectToServer() will take care of the various connection/
   * disconnection, bind/unbind/close etc... requested by the user.
   * The cost is one more function call in this application, but the
   * resulting source code will be much more easiest to maintain.
   */
  if (connectToServer (tttctx) < 0)				/*JLS 18-12-00*/
    return (-1);						/*JLS 18-12-00*/
  if (!(tttctx->binded))					/*JLS 18-12-00*/
    return (0);							/*JLS 18-12-00*/

  /*
   * Build the filter
   */
  if (buildRandomRdnOrFilter (tttctx) < 0)
    return (-1);

  /*
   * Prepear the attribute list
   */
  if (mctx.attrlistNb == 0)					/*JLS 15-03-01*/
    attrlist = NULL;						/*JLS 15-03-01*/
  else								/*JLS 15-03-01*/
    if (mctx.mode & RANDOM_ATTRLIST)				/*JLS 15-03-01*/
      attrlist = selectRandomAttrList (tttctx);			/*JLS 15-03-01*/
    else							/*JLS 15-03-01*/
      attrlist = mctx.attrlist;					/*JLS 15-03-01*/

  if (mctx.mod2 & M2_DEREF) /* dereference */
  {
    char *attrs[2];
    /* I have stored ref attr at mctx.attRef , deref attr at mctx.attRefDef */

    /* using hard coded value for dereferenced attribute if no mctx.attRef is set */
    if (mctx.attRef == NULL )
    {
      attrs[0] = "cn"; 
    }else{
      attrs[0] = mctx.attRefDef;
    }

    attrs[1] = NULL;

    /* use pre-defined value if passin mctx.attrRefDef is null 
     * the pre-defined value LDCLT_DEREF_ATTR is "secretary"
     */
    if (mctx.attRef == NULL ){
      ret = ldclt_create_deref_control(tttctx->ldapCtx,
                                     LDCLT_DEREF_ATTR, attrs, &dctrl);
    }else{
      ret = ldclt_create_deref_control(tttctx->ldapCtx,
                                     mctx.attRef , attrs, &dctrl);
    }

    /* dctrl contains the returned reference value */
    if (LDAP_SUCCESS == ret)
    {
        ctrls[0] = dctrl;
        ctrls[1] = NULL;
        ctrlsp = ctrls;
    }
    else
    {
      if (!((mctx.mode & QUIET) && ignoreError (ret))) 
        fprintf (stderr, 
          "ldclt[%d]: T%03d: ldclt_create_deref_control() failed, error=%d\n",
          mctx.pid, tttctx->thrdNum, ret);
      if (dctrl) {
        ldap_control_free(dctrl);
      }
      if (addErrorStat(ret) < 0)
        return (-1);
    }
  }
  /*
   * Do the search
   * Maybe we are in synchronous mode ? I hope so, it is really
   * much simple ;-)
   */
  if (!(mctx.mode & ASYNC))
  {
    ret = ldap_search_ext_s (tttctx->ldapCtx, tttctx->bufBaseDN, mctx.scope,
		tttctx->bufFilter, attrlist, 			/*JLS 15-03-01*/
		mctx.attrsonly, ctrlsp, NULL, NULL, -1, &res);	/*JLS 03-01-01*/
    if (ret != LDAP_SUCCESS) 
    { /* if search failed */
      if (!((mctx.mode & QUIET) && ignoreError (ret)))
	(void) printErrorFromLdap (tttctx, res, ret,		/*JLS 03-08-00*/
		"Cannot ldap_search()");			/*JLS 03-08-00*/
      if (addErrorStat (ret) < 0)
      {
        goto bail;
      }
      if ((ret == LDAP_NO_SUCH_OBJECT) && 			/*JLS 15-12-00*/
	  (mctx.mode & COUNT_EACH))				/*JLS 15-12-00*/
      {								/*JLS 15-12-00*/
        if (incrementNbOpers (tttctx) < 0)			/*JLS 15-12-00*/
        {
          goto bail;
        }
      }								/*JLS 15-12-00*/
    }
    else
    { /* if search success & we are in verbose mode */
      if (mctx.mode & VERBOSE)
      {
        for (e = ldap_first_message (tttctx->ldapCtx, res); e != NULL ; e = ldap_next_message (tttctx->ldapCtx, e) )
        {
          msgtype = ldap_msgtype (e);
          switch (msgtype)
          {
            /* if this is an entry that returned */
            case LDAP_RES_SEARCH_ENTRY:
              /* get dereferenced value into resctrls:  deref parsing  */
              parse_rc = ldap_get_entry_controls ( tttctx->ldapCtx, e, &resctrls );
              if ( resctrls != NULL ) 
              {/* parse it only when we have return saved in server control */
                /* get dn */
                if (( dn = ldap_get_dn (tttctx->ldapCtx, e)) != NULL )
                {
                  for ( rcp = resctrls; rcp && *rcp; rcp++ ) 
                  {
                    /* if very_verbose */
                    if ( mctx.mode & VERY_VERBOSE )
                    {
                      printf("dn: [%s] -> deref oid: %s, value: %s\n",
                        dn, 
                        (**rcp).ldctl_oid?(**rcp).ldctl_oid:"none",
                        (**rcp).ldctl_value.bv_val?(**rcp).ldctl_value.bv_val:"none");
                    }
                  }
                  ldap_controls_free( resctrls );
                  ldap_memfree (dn);
                }
              }
            break; /*end of case LDAP_RES_SEARCH_ENTRY */
  
            /* if this is an reference that returned */
            case LDAP_RES_SEARCH_REFERENCE: /* we do nothing here */
            break;
  
            /* if we reach the end of search result */
            case LDAP_RES_SEARCH_RESULT:
              /* just free the returned msg */
              parse_rc = ldap_parse_result (tttctx->ldapCtx, e, &rc, &matcheddn, &errmsg, NULL , NULL , 0); 
              if ( parse_rc != LDAP_SUCCESS )
              {
                printf("ldap_parse_result error: [%s]\n", ldap_err2string( parse_rc ));
              }
              if ( rc != LDAP_SUCCESS )
              {
                printf ("ldap_search_ext_s error: [%s]\n", ldap_err2string( rc ));
              }
            break; /* end of case LDAP_RES_SEARCH_RESULT */
            default:
            break;
          }
        } /*end of message retriving */
      } /* end of verbose mode */

      if (incrementNbOpers (tttctx) < 0)/* Memorize operation */
      {
        goto bail;
      }

      /*
       * Don't forget to free the returned message !
       */
      if ((ret = ldap_msgfree (res)) < 0)
      {
        if (!((mctx.mode & QUIET) && ignoreError (ret)))
        {
          printf ("ldclt[%d]: T%03d: Cannot ldap_msgfree(), error=%d (%s)\n",
                  mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
          fflush (stdout);
        }
        if (addErrorStat (ret) < 0)
        {
          goto bail;
        }
      }
    }

    /*
     * End of synchronous operation
     */
    if (dctrl)
    {
      ldap_control_free(dctrl);
    }
    return (0);
  }

  /*
   * Here, we are in asynchronous mode...
   * Too bad, lot of things to do here.
   * First, let's see if we are above the reading threshold.
   */
  if (tttctx->pendingNb >= mctx.asyncMin)
  {
    /*
     * Retrieve the next pending request
     */
    ret = ldap_result (tttctx->ldapCtx, LDAP_RES_ANY, 1, &(mctx.timeval), &res);
    if (ret < 0)
    {
      if (!((mctx.mode & QUIET) && ignoreError (ret)))
	(void) printErrorFromLdap (tttctx, res, ret,		/*JLS 03-08-00*/
		"Cannot ldap_result()");			/*JLS 03-08-00*/
      if (addErrorStat (ret) < 0)
      {
        goto bail;
      }
    }
    else
    {
      tttctx->pendingNb--;

      /*
       * Don't forget to free the returned message !
       */
      if ((ret = ldap_msgfree (res)) < 0)
      {
        if (!((mctx.mode & QUIET) && ignoreError (ret)))
        {
          printf ("ldclt[%d]: T%03d: Cannot ldap_msgfree(), error=%d (%s)\n",
                mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
          fflush (stdout);
        }
        if (addErrorStat (ret) < 0)
        {
          goto bail;
        }
      }
    }
  }

  /*
   * Maybe we may send another request ?
   * Well... there is no proper way to retrieve the error number for
   * this, so I guess I may use direct access to the ldap context
   * to read the field ld_errno.
   */
  if (tttctx->pendingNb > mctx.asyncMax)
  {
    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 1;
      printf ("ldclt[%d]: T%03d: Max pending request hit.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }
  }
  else
  {
    int msgid = 0;

    if ((mctx.mode & VERBOSE)   &&
	(tttctx->asyncHit == 1) &&
	(!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 0;
      printf ("ldclt[%d]: T%03d: Restart sending.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }

    ret = ldap_search_ext (tttctx->ldapCtx, tttctx->bufBaseDN, mctx.scope,
		tttctx->bufFilter, attrlist, 			/*JLS 15-03-01*/
		mctx.attrsonly, ctrlsp, NULL, NULL, -1, &msgid);	/*JLS 03-01-01*/
    if (ret < 0)
    {
      if (ldap_get_option (tttctx->ldapCtx, LDAP_OPT_ERROR_NUMBER, &ret) < 0)
      {
        printf ("ldclt[%d]: T%03d: Cannot ldap_get_option(LDAP_OPT_ERROR_NUMBER)\n",
        mctx.pid, tttctx->thrdNum);
        fflush (stdout);
        goto bail;
      }
      else
      {
        if (!((mctx.mode & QUIET) && ignoreError (ret)))
        {
          printf ("ldclt[%d]: T%03d: Cannot ldap_search(), error=%d (%s)\n",
        	mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
          fflush (stdout);
        }
        if (addErrorStat (ret) < 0)
          return (-1);
        if ((ret == LDAP_NO_SUCH_OBJECT) && 			/*JLS 15-12-00*/
            (mctx.mode & COUNT_EACH))				/*JLS 15-12-00*/
        {							/*JLS 15-12-00*/
          if (incrementNbOpers (tttctx) < 0) {		/*JLS 15-12-00*/
            goto bail;
          }
        }							/*JLS 15-12-00*/
      }
    }
    else
    {
      /*
       * Memorize the operation
       */
      if (incrementNbOpers (tttctx) < 0) {
        goto bail;
      }
      tttctx->pendingNb++;
    }
  }

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: pendingNb=%d\n",
             mctx.pid, tttctx->thrdNum, tttctx->pendingNb);

  /*
   * End of asynchronous operation... and also end of function.
   */
  if (dctrl)
  {
    ldap_control_free(dctrl);
  }
  return (0);

bail:
  if (dctrl)
  {
    ldap_control_free(dctrl);
  }
  return (-1);
}

/* ****************************************************************************
	FUNCTION :	doAbandon
	PURPOSE :	Perform one abandon operation against an async search.
	INPUT :		tttctx	= thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doAbandon (thread_context *tttctx)
{
  int                 ret;               /* Return value */
  LDAPMessage         *res;              /* LDAP results */
  char                **attrlist;        /* Attribs list */
  struct timeval      mytimeout;
  int                 msgid;

  /*
   * Connection to the server
   * The function connectToServer() will take care of the various connection/
   * disconnection, bind/unbind/close etc... requested by the user.
   * The cost is one more function call in this application, but the
   * resulting source code will be much more easiest to maintain.
   */
  if (connectToServer (tttctx) < 0)
    return (-1);
  if (!(tttctx->binded))
    return (0);

  /*
   * Build the filter
   */
  if (buildRandomRdnOrFilter (tttctx) < 0)
    return (-1);

  attrlist = NULL;

  /*
   * We use asynchronous search to abandon...
   *
   * set (1, 2) to (acyncMin, acyncMax), which combination does not stop write.
   */
  mctx.asyncMin = 1;
  mctx.asyncMax = 2;
  if (tttctx->pendingNb >= mctx.asyncMin)
  {
    mytimeout.tv_sec = 1;
    mytimeout.tv_usec = 0;
    ret = ldap_result (tttctx->ldapCtx,
                       LDAP_RES_ANY, LDAP_MSG_ONE, &mytimeout, &res);
    if (ret < 0)
    {
      if (!((mctx.mode & QUIET) && ignoreError (ret)))
        (void) printErrorFromLdap (tttctx, res, ret, "Cannot ldap_result()");
      if (addErrorStat (ret) < 0)
        return (-1);
    }
    else
    {
      /* ret == 0 --> timeout; op abandoned and no result is returned */
      tttctx->pendingNb--;

      /*
       * Don't forget to free the returned message !
       */
      if ((ret = ldap_msgfree (res)) < 0)
      {
        if (!((mctx.mode & QUIET) && ignoreError (ret)))
        {
          printf ("ldclt[%d]: T%03d: Cannot ldap_msgfree(), error=%d (%s)\n",
                mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
          fflush (stdout);
        }
        if (addErrorStat (ret) < 0)
          return (-1);
      }
    }
  }

  /*
   * Maybe we may send another request ?
   * Well... there is no proper way to retrieve the error number for
   * this, so I guess I may use direct access to the ldap context
   * to read the field ld_errno.
   */
  if (tttctx->pendingNb > mctx.asyncMax)
  {
    if ((mctx.mode & VERBOSE)   &&
        (tttctx->asyncHit == 1) &&
        (!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 1;
      printf ("ldclt[%d]: T%03d: Max pending request hit.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }
  }
  else
  {
    if ((mctx.mode & VERBOSE)   &&
        (tttctx->asyncHit == 1) &&
        (!(mctx.mode & SUPER_QUIET)))
    {
      tttctx->asyncHit = 0;
      printf ("ldclt[%d]: T%03d: Restart sending.\n",
               mctx.pid, tttctx->thrdNum);
      fflush (stdout);
    }

    msgid = -1;
    /* for some reasons, it is an error to pass in a zero'd timeval */
    mytimeout.tv_sec = mytimeout.tv_usec = -1;
    ret = ldap_search_ext (tttctx->ldapCtx, tttctx->bufBaseDN, mctx.scope,
                tttctx->bufFilter, attrlist, mctx.attrsonly,
                NULL, NULL, &mytimeout, -1, &msgid);
    if (mctx.mode & VERY_VERBOSE)
        printf ("ldclt[%d]: T%03d: ldap_search(%s)=>%d\n",
             mctx.pid, tttctx->thrdNum, tttctx->bufFilter, ret);

    if (ret != 0)
    {
      if (ldap_get_option (tttctx->ldapCtx, LDAP_OPT_ERROR_NUMBER, &ret) < 0)
      {
        printf ("ldclt[%d]: T%03d: Cannot ldap_get_option(LDAP_OPT_ERROR_NUMBER)\n",
                mctx.pid, tttctx->thrdNum);
        fflush (stdout);
        return (-1);
      }
      else
      {
        if (!((mctx.mode & QUIET) && ignoreError (ret)))
        {
          printf ("ldclt[%d]: T%03d: Cannot ldap_search(), error=%d (%s)\n",
                mctx.pid, tttctx->thrdNum, ret, my_ldap_err2string (ret));
          fflush (stdout);
        }
        if (addErrorStat (ret) < 0)
          return (-1);
      }
    }
    else
    {
      if (msgid >= 0)
      {
        /* ABANDON the search request immediately */
        (void) ldap_abandon_ext(tttctx->ldapCtx, msgid, NULL, NULL);
      }

      /*
       * Memorize the operation
       */
      if (incrementNbOpers (tttctx) < 0)
        return (-1);
      tttctx->pendingNb++;
      if (mctx.mode & VERY_VERBOSE)
        printf ("ldclt[%d]: T%03d: ldap_abandon(%d)\n",
             mctx.pid, tttctx->thrdNum, msgid);
    }
  }

  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: T%03d: pendingNb=%d\n",
             mctx.pid, tttctx->thrdNum, tttctx->pendingNb);

  /*
   * End of asynchronous operation... and also end of function.
   */
  return (0);
}

#define	LDAP_CONTROL_X_DEREF			"1.3.6.1.4.1.4203.666.5.16"
int 
ldclt_create_deref_control( 
    LDAP *ld, 
    char *derefAttr,
    char **attrs,
    LDAPControl **ctrlp 
)
{
    BerElement *ber;
    int rc;
#if defined(USE_OPENLDAP)
    struct berval *bv = NULL;
#endif
    
    if (ld == 0) {
        return( LDAP_PARAM_ERROR );
    }

    if ( NULL == ctrlp || NULL == derefAttr ||
         NULL == attrs || NULL == *attrs || 0 == strlen(*attrs) ) {
        return ( LDAP_PARAM_ERROR );
    }

    if ( LDAP_SUCCESS != ldclt_alloc_ber( ld, &ber )  ) 
    {
        return( LDAP_NO_MEMORY );
    }

    if ( LBER_ERROR == ber_printf( ber, "{{s{v}}}", derefAttr, attrs ))
    {
        ber_free( ber, 1 );
        return( LDAP_ENCODING_ERROR );
    }

#if defined(USE_OPENLDAP)
    if ( LBER_ERROR == ber_flatten(ber, &bv) ) {
        ber_bvfree( bv );
        ber_free( ber, 1 );
        return( LDAP_ENCODING_ERROR );
    }
    if ( NULL == bv ) {
        ber_free( ber, 1 );
        return( LDAP_NO_MEMORY );
    }
    rc = ldap_control_create( LDAP_CONTROL_X_DEREF, 1, bv, 1, ctrlp );
    ber_bvfree( bv );
    ber_free( ber, 1 );
#else
    rc = ldclt_build_control( LDAP_CONTROL_X_DEREF, ber, 1, 1, ctrlp );
    ber_free( ber, 1 );
#endif

    return( rc );
}

#if !defined(USE_OPENLDAP)
/*
 * Duplicated nsldapi_build_control from 
 * mozilla/directory/c-sdk/ldap/libraries/libldap/control.c
 *
 * build an allocated LDAPv3 control.  Returns an LDAP error code.
 */
int
ldclt_build_control( char *oid, BerElement *ber, int freeber, char iscritical,
    LDAPControl **ctrlp )
{
    int        rc;
    struct berval    *bvp;

    if ( ber == NULL ) {
        bvp = NULL;
    } else {
        /* allocate struct berval with contents of the BER encoding */
        rc = ber_flatten( ber, &bvp );
        if ( freeber ) {
            ber_free( ber, 1 );
        }
        if ( rc == -1 ) {
            return( LDAP_NO_MEMORY );
        }
    }

    /* allocate the new control structure */
    if (( *ctrlp = (LDAPControl *)malloc( sizeof(LDAPControl))) == NULL ) {
        if ( bvp != NULL ) {
            ber_bvfree( bvp );
        }
        return( LDAP_NO_MEMORY );
    }

    /* fill in the fields of this new control */
    (*ctrlp)->ldctl_iscritical = iscritical;  
    if (( (*ctrlp)->ldctl_oid = strdup( oid )) == NULL ) {
        free( *ctrlp ); 
        if ( bvp != NULL ) {
            ber_bvfree( bvp );
        }
        return( LDAP_NO_MEMORY );
    }                

    if ( bvp == NULL ) {
        (*ctrlp)->ldctl_value.bv_len = 0;
        (*ctrlp)->ldctl_value.bv_val = NULL;
    } else {
        (*ctrlp)->ldctl_value = *bvp;    /* struct copy */
        free( bvp );    /* free container, not contents! */
    }

    return( LDAP_SUCCESS );
}
#endif

/*
 * Duplicated nsldapi_build_control from 
 * mozilla/directory/c-sdk/ldap/libraries/libldap/request.c
 *
 * returns an LDAP error code and also sets error inside LDAP 
 */ 
int
ldclt_alloc_ber( LDAP *ld, BerElement **berp )
{
    int    err;
    int beropt;
#if defined(USE_OPENLDAP)
    beropt = LBER_USE_DER;
#else
    beropt = LBER_OPT_USE_DER;
#endif
    /* We use default lberoptions since the value is not public in mozldap. */
     if (( *berp = ber_alloc_t( beropt )) == (BerElement *)NULL ) {
        err = LDAP_NO_MEMORY;
    } else {
        err = LDAP_SUCCESS;
    }

    return( err );
}

/* End of file */
