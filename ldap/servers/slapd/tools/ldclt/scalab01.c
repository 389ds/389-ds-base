#ident "ldclt @(#)scalab01.c	1.8 01/05/03"

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
	FILE :		scalab01.c
	AUTHOR :	Jean-Luc SCHWING
	VERSION :       1.0
	DATE :		08 January 2001
	DESCRIPTION :	
			This file contains the implmentation of the specific
			scenario scalab01 of ldclt.
			I implement this set of functions in a separate file to
			reduce the interconnection(s) between the main ldclt
			and the add-ons, keeping in mind the possibility to use
			a dynamic load of plugins for a future release.
	LOCAL :		None.
	HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
08/01/01 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
12/01/01 | JL Schwing	| 1.2 : Second set of options for -e scalab01
---------+--------------+------------------------------------------------------
29/01/01 | B Kolics	| 1.3 : readAttrValue() uses filter of requested attr
---------+--------------+------------------------------------------------------
01/02/01 | JL Schwing	| 1.4 : Protect against multiple choice of same user.
---------+--------------+------------------------------------------------------
26/02/01 | JL Schwing	| 1.5 : Port on non-solaris platforms...
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.6 : Lint cleanup.
			| Bug fix : forget to set ldap protocol version.
---------+--------------+------------------------------------------------------
26/04/01 | B Kolics     | 1.7 : in case of lock failure, thread is not aborted
---------+--------------+------------------------------------------------------
03/05/01 | B Kolics     | 1.8 : bug fix - forget to release more line.
---------+--------------+------------------------------------------------------
*/



#include <stdio.h>	/* printf(), etc... */
#include <stdlib.h>	/* malloc(), etc... */
#include <string.h>	/* strcpy(), etc... */
#include <errno.h>	/* perror(), etc... */
#ifndef _WIN32
#include <pthread.h>	/* pthreads(), etc... */
#endif

#include <lber.h>	/* ldap C-API BER declarations */
#include <ldap.h>	/* ldap C-API declarations */
#if !defined(USE_OPENLDAP)
#include <ldap_ssl.h>	/* ldapssl_init(), etc... */
#endif
#include <prprf.h>
#include "port.h"	/* Portability definitions */
#include "ldclt.h"	/* This tool's include file */
#include "utils.h"	/* Utilities functions */
#include "scalab01.h"	/* Scalab01 specific definitions */





/*
 * Private data structures.
 */
scalab01_context	 s1ctx;





/* ****************************************************************************
	FUNCTION :	scalab01_init
	PURPOSE :	Initiates the scalab01 scenario.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
scalab01_init (void)
{
  int	 ret;		/* Return value */

  s1ctx.nbcnx      = 0;		/* No connection yet */
  s1ctx.list       = NULL;	/* No record yet */
  s1ctx.lockingMax = 0;		/* No locking yet */

  /*
   * Initiates mutexes
   */
  if ((ret = ldclt_mutex_init (&(s1ctx.list_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s\n", mctx.pid, strerror (ret));
    fprintf (stderr, "ldclt[%d]: Error: cannot initiate s1ctx.list_mutex\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }
  if ((ret = ldclt_mutex_init (&(s1ctx.locking_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s\n", mctx.pid, strerror (ret));
    fprintf (stderr, "ldclt[%d]: Error: cannot initiate s1ctx.locking_mutex\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }
  if ((ret = ldclt_mutex_init (&(s1ctx.nbcnx_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s\n", mctx.pid, strerror (ret));
    fprintf (stderr, "ldclt[%d]: Error: cannot initiate s1ctx.nbcnx_mutex\n", mctx.pid);
    fflush (stderr);
    return (-1);
  }

  /*
   * No error
   */
  return (0);
}







/* ****************************************************************************
	FUNCTION :	scalab01Lock
	PURPOSE :	Lock for single user trying to connect.
	INPUT :		tttctx	= thread context.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 cannot lock, 1 if could lock.
	DESCRIPTION :
 *****************************************************************************/
int
scalab01Lock (
	thread_context	*tttctx)
{
  int	 i;	/* For the loop */
  int	 ret;	/* Return code */
  int	 res;	/* Result of this function */

  /*
   * Get secure access to the common data structure.
   */
  if ((ret = ldclt_mutex_lock (&(s1ctx.locking_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_lock(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdId, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Is it locked ?
   */
  res = 1;
  for (i=0 ; i<s1ctx.lockingMax ; i++)
    if ((s1ctx.locking[i] != NULL) && 
	(!strcmp (s1ctx.locking[i], tttctx->bufBindDN)))
    {
      res = 0;
      break;
    }
  if (res == 1)
  {
    for (i=0 ; (i<s1ctx.lockingMax) && (s1ctx.locking[i] != NULL) ; i++);
    if (i == s1ctx.lockingMax)
    {
      if (s1ctx.lockingMax == SCALAB01_MAX_LOCKING)
	res = 0;
      else
	s1ctx.lockingMax++;
    }
    if (res != 0)
      s1ctx.locking[i] = tttctx->bufBindDN;
  }

  /*
   * Free mutex
   */
  if ((ret = ldclt_mutex_unlock (&(s1ctx.locking_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_unlock(), error=%d (%s)\n",
			mctx.pid, tttctx->thrdId, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  return (res);
}








/* ****************************************************************************
	FUNCTION :	scalab01Unlock
	PURPOSE :	Unlock for single user trying to connect.
	INPUT :		tttctx	= thread context.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
scalab01Unlock (
	thread_context	*tttctx)
{
  int	 i;	/* For the loop */
  int	 ret;	/* Return code */

  /*
   * Get secure access to the common data structure.
   */
  if ((ret = ldclt_mutex_lock (&(s1ctx.locking_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_lock(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdId, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Find the entry and unlock it.
   */
  for (i=0 ; i<s1ctx.lockingMax ; i++)
    if ((s1ctx.locking[i] != NULL) && 
	(!strcmp (s1ctx.locking[i], tttctx->bufBindDN)))
    {
      s1ctx.locking[i] = NULL;
      break;
    }

  /*
   * Free mutex
   */
  if ((ret = ldclt_mutex_unlock (&(s1ctx.locking_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_unlock(), error=%d (%s)\n",
			mctx.pid, tttctx->thrdId, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  return (0);
}









/* ****************************************************************************
	FUNCTION :	scalab01_modemIncr
	PURPOSE :	Increments the modem nb of cnx
	INPUT :		ident	= thread identifier
	OUTPUT :	None.
	RETURN :	-1 if error
			 0 if no modem available
			 1 if modem available
	DESCRIPTION :
 *****************************************************************************/
int
scalab01_modemIncr (
	char	*ident)
{
  int	 ret;	/* Return value */
  int	 res;	/* Result of this function */

  /*
   * Get secure access to the common data structure.
   */
  if ((ret = ldclt_mutex_lock (&(s1ctx.nbcnx_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_lock(), error=%d (%s)\n",
		mctx.pid, ident, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  if (s1ctx.nbcnx >= s1ctx.maxcnxnb)
    res = 0;
  else
  {
    res = 1;
    s1ctx.nbcnx++;
  }

  /*
   * Free mutex
   */
  if ((ret = ldclt_mutex_unlock (&(s1ctx.nbcnx_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_unlock(), error=%d (%s)\n",
			mctx.pid, ident, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  return (res);
}


/* ****************************************************************************
	FUNCTION :	scalab01_modemDecr
	PURPOSE :	Decrements the modem nb of cnx
	INPUT :		ident	= thread identifier
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
scalab01_modemDecr (
	char	*ident)
{
  int	 ret;	/* Return value */

  /*
   * Get secure access to the common data structure.
   */
  if ((ret = ldclt_mutex_lock (&(s1ctx.nbcnx_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_lock(), error=%d (%s)\n",
		mctx.pid, ident, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  s1ctx.nbcnx--;

  /*
   * Free mutex
   */
  if ((ret = ldclt_mutex_unlock (&(s1ctx.nbcnx_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_unlock(), error=%d (%s)\n",
			mctx.pid, ident, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  return (0);
}







/* ****************************************************************************
	FUNCTION :	scalab01_addLogin
	PURPOSE :	Add a new user login to the s1ctx structure.
	INPUT :		tttctx	 = thread context.
			dn	 = user's dn.
			duration = duration of the connection.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
scalab01_addLogin (
	thread_context	*tttctx,
	char		*dn,
	int		 duration)
{
  int		 ret;	/* Return value */
  isp_user	*new;	/* New entry */
  isp_user	*cur;	/* Current entry */
  int		 rc = 0;

  /*
   * Create the new record.
   */
  new = (isp_user *) malloc (sizeof (isp_user));
  if (NULL == new) {
    fprintf (stderr, "ldclt[%d]: %s: cannot malloc(isp_user), error=%d (%s)\n",
      mctx.pid, tttctx->thrdId, errno, strerror (errno));
    fflush (stderr);
    return -1;
  }

  strncpy (new->dn, dn, sizeof(new->dn));
  new->dn[sizeof(new->dn)-1] = '\0';
  new->cost = new->counter = duration;
  new->next = NULL;

  /*
   * Get secure access to the common data structure.
   * Note : it should be possible to reduce the "size" of this critical
   *        section but I am not 100% certain this won't mess up all things.
   */
  if ((ret = ldclt_mutex_lock (&(s1ctx.list_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_lock(), error=%d (%s)\n",
		mctx.pid, tttctx->thrdId, ret, strerror (ret));
    fflush (stderr);
    rc = -1;
    goto error;
  }

  /*
   * Maybe this is the first entry of the list ?
   */
  if (s1ctx.list == NULL)
    s1ctx.list = new;
  else
  {
    /*
     * Check with the list's head
     */
    if (s1ctx.list->counter >= duration)
    {
      new->next  = s1ctx.list;
      s1ctx.list = new;
    }
    else
    {
      cur = s1ctx.list;

      /* If cur is NULL, we should just bail and free new. */
      if (cur == NULL)
      {
        goto error;
      }

      while (cur != NULL)
      {
	if (cur->next == NULL)
	{
	  cur->next = new;
	  cur       = NULL; /* Exit loop */
	}
	else if (cur->next->counter >= duration)
	{
	    new->next = cur->next;
	    cur->next = new;
	    cur       = NULL; /* Exit loop */
	}
	else
	{
	    cur = cur->next;
	}
      }
    }
  }

  goto done;

error:
  if (new) free(new);

done:
  /*
   * Free mutex
   */
  if ((ret = ldclt_mutex_unlock (&(s1ctx.list_mutex))) != 0)
  {
    fprintf (stderr, "ldclt[%d]: %s: cannot mutex_unlock(), error=%d (%s)\n",
			mctx.pid, tttctx->thrdId, ret, strerror (ret));
    fflush (stderr);
    rc = -1;
  }

  return rc;
}









/* ****************************************************************************
	FUNCTION :	scalab01_connectSuperuser
	PURPOSE :	Purpose of the fct
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
scalab01_connectSuperuser (void)
{
  char	 bindDN [MAX_DN_LENGTH] = {0};	/* To bind */
  unsigned int mode = mctx.mode;
  unsigned int mod2 = mctx.mod2;

  if (!(mode & CLTAUTH)) {
    snprintf (bindDN, sizeof(bindDN), "%s,%s", SCALAB01_SUPER_USER_RDN, mctx.baseDN);
    bindDN[sizeof(bindDN)-1] = '\0';
  }
  /* clear bits not applicable to this mode */
  mod2 &= ~M2_RNDBINDFILE;
  mod2 &= ~M2_SASLAUTH;
  mod2 &= ~M2_RANDOM_SASLAUTHID;
  /* force bind to happen */
  mode |= BIND_EACH_OPER;
  s1ctx.ldapCtx = connectToLDAP(NULL, bindDN, SCALAB01_SUPER_USER_PASSWORD, mode, mod2);
  if (!s1ctx.ldapCtx) {
    return (-1);
  }

  /*
   * Normal end...
   */
  return (0);
}








/* ****************************************************************************
	FUNCTION :	readAttrValue
	PURPOSE :	This function will ldap_search the given entry for the
			value of the given attribute.
	INPUT :		ident	= thread identifier
			ldapCtx	= LDAP context
			dn	= dn of the entry to process
			attname	= attribute name
	OUTPUT :	value	= attribute value. This buffer must be
				  initiated with enough memory.
				  value[0] == '\0' if not find.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
readAttrValue (
	LDAP	*ldapCtx,
	char	*ident,
	char	*dn,
	char	*attname,
	char	*value)
{
  int		  ret;		/* Return value */
  char		 *attrs[2];	/* Attribute to retrieve */
  LDAPMessage	 *res;		/* LDAP responses */
  LDAPMessage	 *cur;		/* Current message */
  BerElement	 *ber;		/* To decode the response */
  char		 *aname;	/* Current attribute name */
  char		 *filter;	/* Filter used for searching */

  /*
   * First, ldap_search() the entry.
   */
  attrs[0] = attname;
  attrs[1] = NULL;

  filter = (char *)malloc((4+strlen(attname))*sizeof(char));
  if (NULL == filter)
  {
    printf ("ldclt[%d]: %s: Out of memory\n", mctx.pid, ident);
    fflush (stdout);
    return (-1);
  }

  sprintf(filter, "(%s=*)", attname);
  ret = ldap_search_ext_s (ldapCtx, dn, LDAP_SCOPE_BASE,
			   filter, attrs, 0, NULL, NULL, NULL, -1, &res);
  free(filter);
  if (ret != LDAP_SUCCESS)
  {
    printf ("ldclt[%d]: %s: Cannot ldap_search (%s in %s), error=%d (%s)\n",
		mctx.pid, ident, attname, dn, ret, my_ldap_err2string (ret));
    fflush (stdout);
    return (-1);
  }

  /*
   * Decode the response
   */
  value[0] = '\0';	/* Not find yet */
  cur  = ldap_first_entry (ldapCtx, res);
  while ((!value[0]) && (cur != NULL))
  {
    aname = ldap_first_attribute (ldapCtx, cur, &ber);
    while ((!value[0]) && (aname != NULL))
    {
      /*
       * We expect this attribute to be single-valued.
       */
      if (!strcmp (aname, attname))
      {
	struct berval **vals;
	vals = ldap_get_values_len (ldapCtx, cur, aname);
	if (vals == NULL)
	{
	  printf ("ldclt[%d]: %s: no value for %s in %s\n",
                   mctx.pid, ident, dn, attname);
	  fflush (stdout);
	  return (-1);
	}
	strncpy (value, vals[0]->bv_val, vals[0]->bv_len);
	value[vals[0]->bv_len] = '\0';
	ldap_value_free_len (vals);
      }

      /*
       * Next attribute
       */
      ldap_memfree (aname);
      if (!value[0])
	aname = ldap_next_attribute (ldapCtx, cur, ber);
    }

    /*
     * Next entry - shouldn't happen in theory
     */
    if (ber != NULL)
      ber_free (ber, 0);
    cur = ldap_next_entry (ldapCtx, cur);
  }
  ldap_msgfree (res);	/* Free the response */

  return (0);
}







/* ****************************************************************************
	FUNCTION :	writeAttrValue
	PURPOSE :	This function will ldap_modify the given entry to
			replace the value of the given attribute.
	INPUT :		ident	= thread identifier
			ldapCtx	= LDAP context
			dn	= dn of the entry to process
			attname	= attribute name
			value	= attribute value
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
writeAttrValue (
	LDAP	*ldapCtx,
	char	*ident,
	char	*dn,
	char	*attname,
	char	*value)
{
  int		 ret;		/* Return value */
  LDAPMod	 attribute;	/* To build the attributes */
  LDAPMod	*attrsmod[2];	/* Modify attributes */
  char		*pvalues[2];	/* To build the values list */

  /*
   * Prepear the data to be written
   */
  pvalues[0]           = value;
  pvalues[1]           = NULL;
  attribute.mod_op     = LDAP_MOD_REPLACE;
  attribute.mod_type   = attname;
  attribute.mod_values = pvalues;
  attrsmod[0]          = &attribute;
  attrsmod[1]          = NULL;

  /*
   * Store the data in the directory.
   */
  ret = ldap_modify_ext_s (ldapCtx, dn, attrsmod, NULL, NULL);
  if (ret != LDAP_SUCCESS)
  {
    printf ("ldclt[%d]: %s: Cannot ldap_modify_ext_s (%s in %s), error=%d (%s)\n",
			mctx.pid, ident, attname, dn, ret, my_ldap_err2string (ret));
    fflush (stdout);
    return (-1);
  }

  return (0);
}







/* ****************************************************************************
	FUNCTION :	scalab01_unlock
	PURPOSE :	Unlock the user given in argument.
	INPUT :		entry	= entry to unlock.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
scalab01_unlock (
	isp_user	*user)
{
  int	 account;	/* Accounting value */
  char	 buf[20];	/* To read/write attribute */

  /*
   * Increment accounting counters
   * First, read the current value.
   */
  if (readAttrValue (s1ctx.ldapCtx,"ctrl",user->dn,SCALAB01_ACC_ATTRIB,buf) < 0)
  {
    printf ("ldclt[%d]: ctrl: Cannot read accounting attribute of %s\n",
             mctx.pid, user->dn);
    fflush (stdout);
    return (-1);
  }

  /*
   * If this attribute has no value (doesn't exist) we assume it is 0.
   */
  if (buf[0] != '\0')
    account = atoi (buf);
  else
  {
    printf ("ldclt[%d]: ctrl: No accounting attribute for %s - assume it is 0\n",
             mctx.pid, user->dn);
    fflush (stdout);
    account = 0;
  }

  /*
   * Compute the new value and store it in the directory.
   */
  sprintf (buf, "%d", account + user->cost);
  if (writeAttrValue (s1ctx.ldapCtx,"ctrl",user->dn,SCALAB01_ACC_ATTRIB,buf) <0)
  {
    printf ("ldclt[%d]: ctrl: Cannot write accounting attribute of %s\n",
             mctx.pid, user->dn);
    fflush (stdout);
    return (-1);
  }

  /*
   * Unlock the user
   */
  if (writeAttrValue (s1ctx.ldapCtx, "ctrl", user->dn, 
			SCALAB01_LOCK_ATTRIB, SCALAB01_VAL_UNLOCKED) < 0)
  {
    printf ("ldclt[%d]: ctrl: Cannot write lock (unlock) attribute of %s\n",
             mctx.pid, user->dn);
    fflush (stdout);
    return (-1);
  }
  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: ctrl: entry %s unlocked\n",
             mctx.pid, user->dn);

  /*
   * Decrement modem pool usage...
   */
  if (scalab01_modemDecr ("ctrl") < 0)
    return (-1);

  /*
   * Normal end
   */
  return (0);
}








/* ****************************************************************************
	FUNCTION :	scalab01_control
	PURPOSE :	This function implements the control loop/thread of
			the scalab01 scenario. Its main target is to manage
			the counters of each "connection" and to unlock the
			entry when time is reached.
	INPUT :		None.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
void *
scalab01_control (
	void	*arg)
{
  isp_user	*cur;	/* Current entry */
  isp_user	*head;	/* Head of entries to process */
  int		 ret;	/* Return value */
  int		 nbTot;	/* Total nb entries locked */
  int		 nbU;	/* Number unlocked */

  /*
   * Initialization
   * Failure to connect is a critical error...
   */
  if (scalab01_connectSuperuser () < 0)
    ldcltExit (EXIT_NOBIND);

  /*
   * Main loop
   */
  while (1 /*CONSTCOND*/)					/*JLS 14-03-01*/
  {
    ldclt_sleep (1);	/* Poll the connections every second */
    nbTot = nbU = 0;	/* No entries processed yet */

    /*
     * Get protected access to the entries
     */
    if ((ret = ldclt_mutex_lock (&(s1ctx.list_mutex))) != 0)
    {
      fprintf (stderr, "ldclt[%d]: ctrl: cannot mutex_lock(), error=%d (%s)\n",
		mctx.pid, ret, strerror (ret));
      fflush (stderr);
      ldcltExit (EXIT_OTHER);
    }

    /*
     * Decrement all counters
     */
    for (cur=s1ctx.list ; cur!=NULL ; cur=cur->next)
    {
      cur->counter--;
      nbTot++;
    }

    /*
     * Find the entries to process.
     */
    if ((s1ctx.list == NULL) || (s1ctx.list->counter > 0))
      head = NULL;
    else
    {
      head = cur = s1ctx.list;
      while ((cur != NULL) && (cur->counter == 0))
	cur = cur->next;
      s1ctx.list = cur;
    }

    /*
     * Release mutex
     */
    if ((ret = ldclt_mutex_unlock (&(s1ctx.list_mutex))) != 0)
    {
      fprintf (stderr, "ldclt[%d]: ctrl: cannot mutex_unlock(), error=%d (%s)\n",
		mctx.pid, ret, strerror (ret));
      fflush (stderr);
      ldcltExit (EXIT_OTHER);
    }

    /*
     * Now, we have "head" that points either to NULL or to a list of
     * entries to process.
     * Attention, this list of entries is not terminated by NULL, but
     * we must rather check the field head->next->counter" for the last
     * entry...
     *
     * NOTE : implements this section as a separate thread to keep the
     *        general timer working...
     */
    while (head != NULL)
    {
      if (scalab01_unlock (head) < 0)
      {
	printf ("ldclt[%d]: ctrl: cannot unlock %s\n", mctx.pid, head->dn);
	ldcltExit (EXIT_OTHER);
      }
      nbU++;	/* One more entry unlocked */

      /*
       * Next entry...
       */
      cur = head;
      if (head->next == NULL)
	head = NULL;
      else
	if (head->next->counter != 0)
	  head = NULL;
	else
	  head = head->next;

      free (cur);
    } /* while (head =! NULL) */

    /*
     * Print some stats...
     */
    if (mctx.mode & VERBOSE)
      printf ("ldclt[%d]: ctrl: nb entries unlocked / total : %3d / %5d\n",
               mctx.pid, nbU, nbTot);
  } /* Main loop */

  /*
   * End of thread
   */
}







/* ****************************************************************************
	FUNCTION :	doScalab01
	PURPOSE :	Implements the client part of the scalab01 scenario.
	INPUT :		tttctx	= this thread context
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
doScalab01 (
	thread_context	*tttctx)
{
  char	 buf[32];	/* To read attributes value */
  int	 duration;	/* Use a variable for trace purpose */
  int	 res;		/* Result of cnx to modem pool */
  int	 doloop;	/* To know if we should loop */

  /*
   * Simulate connection to the modem pool.
   */
  while ((res = scalab01_modemIncr(tttctx->thrdId)) != 1)
    switch (res)
    {
      case 0:
	ldclt_sleep (s1ctx.wait==0?SCALAB01_DEF_WAIT_TIME:rndlim(0,s1ctx.wait));
	break;
      case -1:
	return (-1);
	break;
    }

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
   * Check that no other thread is using the same identity...
   */
  doloop = 1;
  while (doloop)
  {
    switch (scalab01Lock (tttctx))
    {
      case 0:
	ldclt_sleep (1);
	break;
      case 1:
	doloop = 0;
	break;
      case -1:
	return (-1);
	break;
    }
  }

  /*
   * Ok, we are now binded. Great ;-)
   * The DN we used to bind is available in tttctx->bufBindDN
   * Read lock attribute
   */
  if (readAttrValue (tttctx->ldapCtx, tttctx->thrdId, tttctx->bufBindDN,
		SCALAB01_LOCK_ATTRIB, buf) < 0)
  {
    printf ("ldclt[%d]: %s: Cannot read lock attribute of %s\n", 
				mctx.pid, tttctx->thrdId, tttctx->bufBindDN);
    fflush (stdout);
    (void) scalab01_modemDecr (tttctx->thrdId);
    return (-1);
  }
  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: %s: entry %s lock read\n",
             mctx.pid, tttctx->thrdId, tttctx->bufBindDN);

  /*
   * If locked, then we cannot login now...
   */
  if (!strcmp (buf, SCALAB01_VAL_LOCKED))
  {
    if (scalab01_modemDecr (tttctx->thrdId) < 0)
      return (-1);
    return (0);
  }

  /*
   * If not locked :
   *  - lock the user
   *  - decide how many times will be connected
   *  - add information to the list of connected
   */
  if (writeAttrValue (tttctx->ldapCtx, tttctx->thrdId, tttctx->bufBindDN,
		SCALAB01_LOCK_ATTRIB, SCALAB01_VAL_LOCKED) < 0)
  {
    printf ("ldclt[%d]: %s: Cannot write lock attribute of %s\n", 
				mctx.pid, tttctx->thrdId, tttctx->bufBindDN);
    fflush (stdout);
    /*
     * It can still happen that two threads write this attribute at the same
     * time, so there can be failure in one of the threads
     * in this case just return
     */
    if (scalab01_modemDecr (tttctx->thrdId) < 0)		/*JLS 03-05-01*/
      return (-1);						/*JLS 03-05-01*/
    return (0);							/*BK  26-04-01*/
  }
  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: %s: entry %s lock written\n",
             mctx.pid, tttctx->thrdId, tttctx->bufBindDN);

  if (scalab01Unlock (tttctx) < 0)
    return (-1);

  duration = rndlim (1, s1ctx.cnxduration);
  if (scalab01_addLogin (tttctx, tttctx->bufBindDN, duration) < 0)
  {
    printf ("ldclt[%d]: %s: Cannot memorize new login of %s\n", 
				mctx.pid, tttctx->thrdId, tttctx->bufBindDN);
    fflush (stdout);
    return (-1);
  }
  if (mctx.mode & VERY_VERBOSE)
    printf ("ldclt[%d]: %s: entry %s login added duration %6d\n",
		mctx.pid, tttctx->thrdId, tttctx->bufBindDN, duration);

  /*
   * Memorize the operation
   */
  if (incrementNbOpers (tttctx) < 0)
    return (-1);

  /*
   * Wait before next operation...
   */
  if (s1ctx.wait > 0)
    ldclt_sleep (rndlim (0,s1ctx.wait));

  /*
   * Unbind
   */
/*
TBC - this is done in the next loop... - cf connectToServer()
*/

  return (0);
}






/* End of file */
