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
	FILE :		opCheck.c
	AUTHOR :        Jean-Luc SCHWING
	VERSION :       1.0
	DATE :		04 May 1999
	DESCRIPTION :
			This file contains the functions used to manage and
			check the operations performed by the tool.
			These functions manages the operation list
			"mctx.opListTail", match an entry retrieved from the
			server to the attributes memorized for one operation,
			etc...
	LOCAL :		None.
	HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
04/05/99 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
05/05/99 | F. Pistolesi	| 1.8 : Add communication with remote host.
			| Implement operations check.
---------+--------------+------------------------------------------------------
06/05/99 | JL Schwing	| 1.10: Implements opDecOper().
			| Add more traces in VERY_VERBOSE
---------+--------------+------------------------------------------------------
20/05/99 | JL Schwing	| 1.18: Add params (newRdn and newParent) to opAdd()
			| Decode operations in Cnnn messages.
			| No more exit on EINTR in accept()
			| Fix memory leak in thOperFree()
---------+--------------+------------------------------------------------------
21/05/99 | JL Schwing	| 1.19: Minor fixes in messages.
			| Purify cleanup - Free memory read in opCheckLoop()
			| Fix thOperFree() - pb when head of list to delete
			| Fix memory leak in opNext().
---------+--------------+------------------------------------------------------
26/05/99 | JL Schwing	| 1.21: Bug fix - return(-1) bad place in opNext().
			| Minor fixes in messages.
---------+--------------+------------------------------------------------------
27/05/99 | JL Schwing	| 1.22 : Add statistics to check threads.
---------+--------------+------------------------------------------------------
27/05/99 | F. Pistolesi	| 1.23 : Fix statistics and other algorythms.
---------+--------------+------------------------------------------------------
31/05/99 | JL Schwing	| 1.25 : Bug fix - should test opRead() returned pointer
---------+--------------+------------------------------------------------------
02/06/99 | JL Schwing	| 1.26 : Add flag in main ctx to know if slave was 
			|   connected or not.
			| Add counter of operations received in check threads.
---------+--------------+------------------------------------------------------
06/03/00 | JL Schwing	| 1.27: Test malloc() return value.
---------+--------------+------------------------------------------------------
18/08/00 | JL Schwing	| 1.28: Print begin and end dates.
---------+--------------+------------------------------------------------------
17/11/00 | JL Schwing	| 1.29: Implement "-e smoothshutdown".
---------+--------------+------------------------------------------------------
29/11/00 | JL Schwing	| 1.30: Port on NT 4.
---------+--------------+------------------------------------------------------
*/

#include <pthread.h>		/* Posix threads */
#include <errno.h>		/* errno, etc... */
#include <stdlib.h>		/* exit(), etc... */
#include <unistd.h> 		/* sleep(), etc... */
#include <stdio.h>		/* printf(), etc... */
#include <signal.h>		/* sigset(), etc... */
#include <string.h>		/* strerror(), etc... */
#include <sys/resource.h>	/* setrlimit(), etc... */
#include <lber.h>		/* ldap C-API BER decl. */
#include <ldap.h>		/* ldap C-API decl. */
#include <sys/poll.h>		/* djani : while porting */
#include <sys/socket.h>		/* djani : while porting */
#include <sys/types.h> 		/* djani : while porting */
#include <netdb.h>		/* djani : while porting */
#include <netinet/in.h> 	/* djani : while porting */
#ifdef LDAP_H_FROM_QA_WKA
#include <proto-ldap.h>		/* ldap C-API prototypes */
#endif
#include "port.h"		/* Portability definitions */	/*JLS 29-11-00*/
#include "ldclt.h"		/* This tool's include file */
#include "remote.h"		/* Definitions common with the slave */

enum {SINGLE=0,FIRST,MIDDLE,LAST};






/* ****************************************************************************
	FUNCTION :	opDecOper
	PURPOSE :	This function decodes an LDAP operation and return a
			printable string.
	INPUT :		op	= operation to decode
	OUTPUT :	None.
	RETURN :	The decoded string.
	DESCRIPTION :
 *****************************************************************************/
char *
opDecOper (
	int	 op)
{
  switch (op)
  {
    case LDAP_REQ_MODIFY:	return ("modify");	break;
    case LDAP_REQ_ADD:		return ("add");		break;
    case LDAP_REQ_DELETE:	return ("delete");	break;
    case LDAP_REQ_MODRDN:	return ("modrdn");	break;
    default:			return ("??unknown??");	break;
  }
}







/* ****************************************************************************
	FUNCTION :	LDAPMod2attributes
	PURPOSE :	Convert a LDAPMod-like array of attributes to the
			internal attributes array.
	INPUT :		mods	= LDAPMod array. If NULL, attribs[] is
				  initiated as an empty array.
	OUTPUT :	attribs	= struct attribute array. This array is of
				  MAX_ATTRIBS length.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
LDAPMod2attributes (
	LDAPMod		**mods,
	attribute	 *attribs)
{
  int	 i;	/* For the loop */

  /*
   * Maybe there is no mods ?? This occurs for rename operation, for example.
   */
  if (mods == NULL)
  {
    attribs[0].type = NULL;
    return (0);
  }

  /*
   * Process each entry
   */
  for (i=0 ; i< MAX_ATTRIBS && mods[i] != NULL ; i++)
  {
    attribs[i].type = strdup (mods[i]->mod_type);
    if (attribs[i].type == NULL)				/*JLS 06-03-00*/
    {								/*JLS 06-03-00*/
      printf ("Error: cannot strdup(attribs[%d].type), error=%d (%s)\n",
		i, errno, strerror (errno));			/*JLS 06-03-00*/
      return (-1);						/*JLS 06-03-00*/
    }								/*JLS 06-03-00*/

    /*
     * Well, if it is a binary value, it is most likely an image
     * that is read by mmap and always available. Thus there is no reason
     * to copy it, just modify the pointers.
     */
    if (mods[i]->mod_op & LDAP_MOD_BVALUES)
    {
      attribs[i].dontFree = 1;
      attribs[i].length   = mods[i]->mod_bvalues[0]->bv_len;
      attribs[i].value    = mods[i]->mod_bvalues[0]->bv_val;
    }
    else
    {
      attribs[i].dontFree = 0;
      attribs[i].length   = strlen (mods[i]->mod_values[0]);
      attribs[i].value    = strdup (mods[i]->mod_values[0]);
      if (attribs[i].value == NULL)				/*JLS 06-03-00*/
      {								/*JLS 06-03-00*/
        printf ("Error: cannot strdup(attribs[%d].value), error=%d (%s)\n",
		i, errno, strerror (errno));			/*JLS 06-03-00*/
        return (-1);						/*JLS 06-03-00*/
      }								/*JLS 06-03-00*/
    }
  }

  /*
   * Don't forget to mark the end !
   */
  if (i<MAX_ATTRIBS)
    attribs[i].type = NULL;
  return (0);
}









/* ****************************************************************************
	FUNCTION :	freeAttributesArray
	PURPOSE :	This function is targetted to free an array of
			struct attribute. It does not free the array itself,
			but only the types and values memorized in it.
	INPUT :		attribs	= array to free.
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
freeAttributesArray (
	attribute	*attribs)
{
  int	 i;	/* For the loop */

  for (i=0;i<MAX_ATTRIBS&&attribs[i].type != NULL;i++)
  {
    free (attribs[i].type);
    if (!(attribs[i].dontFree))
      free (attribs[i].value);
  }
  return (0);
}









/* ****************************************************************************
	FUNCTION :	opAdd
	PURPOSE :	Add a new operation to the list.
	INPUT :		tttctx	  = thread context
			type	  = operation type
			dn	  = target's DN
			attribs	  = operation attributes
			newRdn	  = new rdn    (valid for rename only)
			newParent = new parent (valid for rename only)
	OUTPUT :	None.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :	Note that the attributes given in argument are
			directly memorized (i.e. no copy), hence they should
			*not* be freed by the calling function.
 *****************************************************************************/
int
opAdd (
	thread_context	 *tttctx,
	int		  type,
	char		 *dn,
	LDAPMod		**attribs,
	char		 *newRdn,
	char		 *newParent)
{
  int	 ret;		/* Return value */
  oper	*newOper;	/* New operation to memorize */

  if (mctx.mode & VERY_VERBOSE)
    printf ("T%03d: opAdd (%s, %s)\n", tttctx->thrdNum, opDecOper(type), dn);


  /*
   * Go to protected section. This will enforce the correct sequencing
   * of the operations performed because the whole function is lock
   * for the threads.
   * Note: Maybe reduce the size of this section ? To be checked.
   */
  if ((ret = pthread_mutex_lock (&(mctx.opListTail_mutex))) != 0)
  {
    fprintf (stderr,
	"T%03d: cannot pthread_mutex_lock(opListTail), error=%d (%s)\n",
	tttctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Create the new cell
   */
  newOper            = (oper *) malloc (sizeof (oper));
  if (newOper == NULL)						/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf ("T%03d: cannot malloc(newOper), error=%d (%s)\n",	/*JLS 06-03-00*/
		tttctx->thrdNum, errno, strerror (errno));	/*JLS 06-03-00*/
    return (-1);						/*JLS 06-03-00*/
  }								/*JLS 06-03-00*/
  newOper->next      = NULL;
  newOper->type      = type;
  newOper->skipped   = mctx.slavesNb;
  newOper->dn        = strdup (dn);
  if (newOper->dn == NULL)					/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf("T%03d: cannot strdup(newOper->dn), error=%d (%s)\n",/*JLS 06-03-00*/
		tttctx->thrdNum, errno, strerror (errno));	/*JLS 06-03-00*/
    return (-1);						/*JLS 06-03-00*/
  }								/*JLS 06-03-00*/
  newOper->newRdn    = (newRdn    == NULL ? NULL : strdup (newRdn));
  if (newOper->newRdn == NULL)					/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf ("T%03d: cannot strdup(newOper->newRdn), error=%d (%s)\n",
		tttctx->thrdNum, errno, strerror (errno));	/*JLS 06-03-00*/
    return (-1);						/*JLS 06-03-00*/
  }								/*JLS 06-03-00*/
  newOper->newParent = (newParent == NULL ? NULL : strdup (newParent));
  if (newOper->newParent == NULL)				/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf ("T%03d: cannot strdup(newOper->newParent), error=%d (%s)\n",
		tttctx->thrdNum, errno, strerror (errno));	/*JLS 06-03-00*/
    return (-1);						/*JLS 06-03-00*/
  }								/*JLS 06-03-00*/
  if (LDAPMod2attributes (attribs, newOper->attribs) < 0)
    return (-1);

  /*
   * Don't forget to initiate this cell's mutex !
   */
  if ((ret = pthread_mutex_init(&(newOper->skipped_mutex), NULL)) != 0)
  {
    fprintf (stderr,
	"T%03d: cannot initiate skipped_mutex error=%d (%s)\n",
	tttctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  /*
   * Link the cell
   */
  mctx.opListTail->next = newOper;
  mctx.opListTail       = newOper;

  /*
   * Release the mutex
   */
  if ((ret = pthread_mutex_unlock (&(mctx.opListTail_mutex))) != 0)
  {
    fprintf (stderr,
	"T%03d: cannot pthread_mutex_unlock(opListTail), error=%d (%s)\n",
	tttctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }

  return (0);
}







/* ****************************************************************************
	FUNCTION :	opNext
	PURPOSE :	Return the next available operation. May return NULL
			if no operation available.
	INPUT :		ctctx	= thread context
	OUTPUT :	op	= next operation. May be NULL.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
opNext (
	check_context	 *ctctx,
	oper		**op)
{
  int	 ret;		/* Return value */
  oper	*newHead;	/* The new head operation */

  /*
   * Maybe there is no new operation ?
   */
  if (ctctx->headListOp->next == NULL)
  {
    *op = NULL;
    if (mctx.mode & VERY_VERBOSE)
      printf ("C%03d: opNext --> NULL\n", ctctx->thrdNum);
    return (0);
  }

  /*
   * Ok, there is one new operation. Let's skip the head and
   * go to the new operation...
   */
  if ((ret = pthread_mutex_lock (&(ctctx->headListOp->skipped_mutex))) != 0)
  {
    fprintf (stderr,
	"C%03d: cannot pthread_mutex_lock(skipped_mutex), error=%d (%s)\n",
	ctctx->thrdNum, ret, strerror (ret));
    fflush (stderr);
    return (-1);
  }
  newHead = ctctx->headListOp->next;
  ctctx->headListOp->skipped--;

  /*
   * If there is another thread that has not skipped, let's move to the
   * next operation and unlock the counter.
   */
  if (ctctx->headListOp->skipped != 0)
  {
    if ((ret = pthread_mutex_unlock (&(ctctx->headListOp->skipped_mutex))) != 0)
    {
      fprintf (stderr,
	"C%03d: cannot pthread_mutex_unlock(skipped_mutex), error=%d (%s)\n",
	ctctx->thrdNum, ret, strerror (ret));
      fflush (stderr);
      return (-1);
    }
  }
  else
  {
    /*
     * Well, looks like we are the last thread to skip.... Let's free this
     * operation. BTW, there is no reason to unlock/release the mutex because
     * it will be destroyed !
     * Note: may be NULL when LDAP_REQ_DELETE for example.
     */
    if (ctctx->headListOp->attribs != NULL)
      if (freeAttributesArray (ctctx->headListOp->attribs) < 0)
	return (-1);
    if (ctctx->headListOp->dn != NULL)
      free (ctctx->headListOp->dn);
    if (ctctx->headListOp->newRdn != NULL)
      free (ctctx->headListOp->newRdn);
    if (ctctx->headListOp->newParent != NULL)
      free (ctctx->headListOp->newParent);
    free (ctctx->headListOp);
  }

  /*
   * End of function
   */
  *op = ctctx->headListOp = newHead;
  if (mctx.mode & VERY_VERBOSE)
    printf ("C%03d: opNext --> (%s, %s)\n",
		ctctx->thrdNum, opDecOper ((*op)->type), (*op)->dn);
  return (0);
}









/* ****************************************************************************
	FUNCTION :	opRead
	PURPOSE :	Read the n'th operation from the head.
	INPUT :		ctctx	= thread context
			num	= number of the operation to retrieve
	OUTPUT :	op	= returned operation. May be NULL.
	RETURN :	-1 if error, 0 else.
	DESCRIPTION :
 *****************************************************************************/
int
opRead (
	check_context	 *ctctx,
	int		  num,
	oper		**op)
{
  *op = ctctx->headListOp;
  while (num != 0)
  {
    /*
     * Maybe not enough entries in the list ?
     */
    if (*op == NULL)
      return (0);
    *op = (*op)->next;
    num--;
  }

  /*
   * If there, we got it :-)
   */
  return (0);
}






/* ****************************************************************************
	FUNCTION :	thOperAdd
	PURPOSE :	This function copies an operation to the late
			operation list
	INPUT :		head list and operation to copy
	OUTPUT :	None.
	RETURN :	New head
	DESCRIPTION :
 *****************************************************************************/
thoper *
thOperAdd ( thoper *head, oper *elem, int f)
{
  thoper *new,*t=head;
  int i;

  new=malloc(sizeof(thoper));
  if (new == NULL)						/*JLS 06-03-00*/
  {								/*JLS 06-03-00*/
    printf ("Txxx: cannot malloc(new), error=%d (%s)\n",	/*JLS 06-03-00*/
		errno, strerror (errno));			/*JLS 06-03-00*/
    ldcltExit (1);						/*JLS 18-08-00*/
  }								/*JLS 06-03-00*/
  new->next=NULL;
  new->first=f;
  new->type=elem->type;
  new->dn=strdup(elem->dn);
  if (elem->newRdn != NULL)
    new->newRdn=strdup(elem->newRdn);
  if (elem->newParent != NULL)
    new->newParent=strdup(elem->newParent);
  for(i=0;i<MAX_ATTRIBS&&elem->attribs[i].type;i++)
  {
    new->attribs[i].type=strdup(elem->attribs[i].type);
    if (new->attribs[i].type == NULL)				/*JLS 06-03-00*/
    {								/*JLS 06-03-00*/
      printf ("Txxx: cannot strdup(new->attribs[%d].type), error=%d (%s)\n",
		errno, i, strerror (errno));			/*JLS 06-03-00*/
      ldcltExit (1);						/*JLS 18-08-00*/
    }								/*JLS 06-03-00*/
    new->attribs[i].length   = elem->attribs[i].length;
    if((new->attribs[i].dontFree=elem->attribs[i].dontFree))
      new->attribs[i].value = elem->attribs[i].value;
    else
    {
      new->attribs[i].value = strdup(elem->attribs[i].value);
      if (new->attribs[i].value == NULL)			/*JLS 06-03-00*/
      {								/*JLS 06-03-00*/
        printf ("Txxx: cannot strdup(new->attribs[%d].value), error=%d (%s)\n",
		errno, i, strerror (errno));			/*JLS 06-03-00*/
        ldcltExit (1);						/*JLS 18-08-00*/
      }								/*JLS 06-03-00*/
    }
  }
  if(i<MAX_ATTRIBS)
    new->attribs[i].type=NULL;
  if(head==NULL)
    return new;
  for(t=head;t->next;)
    t=t->next;
  t->next=new;
  return head;
}






/* ****************************************************************************
	FUNCTION :	thOperFree
	PURPOSE :	This function frees memory for a late operation
	INPUT :		Head of list and operation to delete
	OUTPUT :	None.
	RETURN :	new head
	DESCRIPTION :
 *****************************************************************************/
thoper *
thOperFree (thoper *head, thoper *elem)
{
  thoper *t;

  freeAttributesArray(elem->attribs);
  free (elem->dn);
  if (elem->newRdn != NULL)
    free (elem->newRdn);
  if (elem->newParent != NULL)
    free (elem->newParent);
  if(head!=elem)
  {
	for(t=head;t->next!=elem;)
		t=t->next;
	t->next=t->next->next;
  }
  else
    head = head->next;
  free(elem);
  return head;
}






/* ****************************************************************************
	FUNCTION :	opCheckLoop
	PURPOSE :	This function is the per slave check function
	INPUT :		arg	= this check thread's check_context
	OUTPUT :	None.
	RETURN :	None.
	DESCRIPTION :
 *****************************************************************************/
void *
opCheckLoop ( void* arg)
{
  struct check_context *cctx=(struct check_context *)arg;
  struct pollfd pfd;
  repconfirm *recOper;
  oper *myop;
  thoper *t;
  unsigned char recbuf[1500];
  int ret,i,timeout;
  int		 cnt;		/* To count loops for timeout purpose */
  int		 fndlt;		/* Found late operation */
  int		 nbRead;	/* Nb char read() */
  int		 status;	/* Thread status */		/*JLS 17-11-00*/

  recOper=(repconfirm*)recbuf;
  pfd.fd=cctx->sockfd;
  pfd.events=(POLLIN|POLLPRI);
  pfd.revents=0;
  cctx->status=INITIATED;
  if((timeout=mctx.timeout)<30)
	timeout=30;
  /*
   * First time in here?
   */
  if(cctx->calls==1)
	cctx->dcOper=NULL;

  while((ret=poll(&pfd,1,500))>=0)
  {
	if(ret)
	{
		/*
		 * Exit if read error on the net
		 */
		if ((nbRead = read (pfd.fd,recOper,sizeof(repconfirm)))<0)
			break;
		if (nbRead != sizeof(repconfirm))
			printf ("C%03d(%s): Partial header read %d - expected %d\n",cctx->thrdNum, cctx->slaveName, nbRead, sizeof(repconfirm));
		recOper->type=ntohl(recOper->type);
		recOper->res=ntohl(recOper->res);
		recOper->dnSize=ntohl(recOper->dnSize);
		/*
		 * Beware of structure alignment
		 */
		if((nbRead=read(pfd.fd,recOper->dn+sizeof(recOper->dn),recOper->dnSize))<0)
			break;
		if (nbRead != recOper->dnSize)
			printf ("C%03d(%s): Partial dn read %d - expected %d\n",cctx->thrdNum, cctx->slaveName, nbRead, recOper->dnSize);
		if (nbRead > (1500 - sizeof(repconfirm)))
			printf ("C%03d(%s): Read too much %d - expected %d\n",cctx->thrdNum, cctx->slaveName, nbRead, 1500 - sizeof(repconfirm));
		cnt=0;
		cctx->nbOpRecv++;
		if(mctx.mode&VERY_VERBOSE)
		{
			printf("C%03d(%s): Rec %s\n",cctx->thrdNum,cctx->slaveName,recOper->dn);
			for(myop=cctx->headListOp->next;myop;myop=myop->next)
				printf("C%03d(%s): IN : %s\n",cctx->thrdNum,cctx->slaveName,myop->dn);
			for(t=cctx->dcOper;t;t=t->next)
				printf("C%03d(%s): LATE : %s\n",cctx->thrdNum,cctx->slaveName,t->dn);
		}
		/*
		 * Do not tell me there was an error during replica...
		 */
		if(recOper->res)
		{
			printf("C%03d(%s): Replica failed, op:%d(%s), dn=\"%s\" res=%d\n",
                 cctx->thrdNum, cctx->slaveName,
                 recOper->type, opDecOper(recOper->type),
                 recOper->dn, recOper->res );
			switch (recOper->res)
			{
				case 32:	cctx->nbRepFail32++	; break;
				case 68:	cctx->nbRepFail68++	; break;
				default:	cctx->nbRepFailX++	; break;
			}
		}
		/*
		 * Is this a late operation?
		 */
		fndlt=0;
		if(cctx->dcOper)
		{
			for (i=1,t = cctx->dcOper;t;i++)
				if ((recOper->type!=t->type) || strcmp(recOper->dn,t->dn))
					t = t->next;
				else
				{
					/*
					 * if this is a single operation: 132456
					 */
					if(t->first==SINGLE)
					{
						/*
						 * error.
						 */
						printf("C%03d(%s): Late replica:   op:%d(%s), dn=\"%s\"\n",
                               cctx->thrdNum, cctx->slaveName,
                               t->type, opDecOper(t->type), t->dn );
						cctx->nbLate++;
					} else if (t->first==MIDDLE)
					{
						/*
						 * Middle of a series : 23546
						 */
						printf("C%03d(%s): Early (%3d)     op:%d(%s), dn=\"%s\"\n",
                               cctx->thrdNum, cctx->slaveName,i,
                               t->type, opDecOper(t->type), t->dn );
						cctx->nbEarly++;
						
					} else if(t->next)
					{
						/*
						 * else maybe we are in a re-corrected situation.
						 * we should receive the next one, now.
						 */
							if(t->next->first!=LAST)
								t->next->first=FIRST;
					}
					cctx->dcOper = thOperFree (cctx->dcOper, t);
					fndlt=1;
					break;
				}
		}
		if(!fndlt)
		{
			/*
			 * See if the operation we received is the same as the head
			 */
			opRead(cctx,1,&myop);
			if (myop != NULL &&
				recOper->type==myop->type &&
				strcmp(recOper->dn,myop->dn) == 0)
			    opNext(cctx,&myop);
			else
			{
				/*
				 * Nope, look for it
				 */
				for(i=2;opRead(cctx,i,&myop)==0;i++)
					if(myop)
					{
						if(recOper->type==myop->type &&
							strcmp(recOper->dn,myop->dn) == 0)
						{
							/*
							 * Skip all between current head and this one
							 */
							printf("C%03d(%s): Early (%3d)     op:%d(%s), dn=\"%s\"\n", cctx->thrdNum,cctx->slaveName, i-1, recOper->type, opDecOper(recOper->type), recOper->dn );
							cctx->nbEarly++;
							opNext(cctx,&myop);
							/*
							 * mark the first of the series as leader
							 */
							cctx->dcOper=thOperAdd(cctx->dcOper,myop,
								i==2?SINGLE:FIRST);
							for(;i>2;i--)
							{
								opNext(cctx,&myop);
								/*
								 * copy up until one before last
								 */
								if(myop)
									thOperAdd(cctx->dcOper,myop,
										i==3?LAST:MIDDLE);
							}
							opNext(cctx,&myop);
							break;
						}
					} else break;
				if(!myop)
				{
					printf("C%03d(%s): Not on list     op:%d(%s), dn=\"%s\"\n", cctx->thrdNum,cctx->slaveName, recOper->type, opDecOper(recOper->type), recOper->dn );
					cctx->nbNotOnList++;
				}
			}
		}
	}
	pfd.events=(POLLIN|POLLPRI);
	pfd.revents=0;
	/*
	 * operations threads still running?
	 */
	for(i=0;i<mctx.nbThreads;i++)
	{							/*JLS 17-11-00*/
	  if (getThreadStatus (&(tctx[i]), &status) < 0)	/*JLS 17-11-00*/
	    break;						/*JLS 17-11-00*/
	  if(status != DEAD)					/*JLS 17-11-00*/
	  {
	    cnt=0;
	    break;
	  }
	}							/*JLS 17-11-00*/
	/*
	 * twice half a second...
	 */
	if(++cnt>timeout*2)
			break;
  }
  if(mctx.mode&VERY_VERBOSE)
	printf("C%03d(%s): Exiting\n",cctx->thrdNum,cctx->slaveName);
  /*
   * Any operation left?
   */
  for(opNext(cctx,&myop);myop;opNext(cctx,&myop))
  {
	printf("Operation %d(%s) still on Queue for %s (%s)\n",myop->type,opDecOper(myop->type),cctx->slaveName,myop->dn);
	cctx->nbStillOnQ++;
  }
  for(t=cctx->dcOper;t;t=t->next)
  {
	printf("Lost op %d(%s) on %s (%s)\n",t->type,opDecOper(t->type),cctx->slaveName,t->dn);
	cctx->nbLostOp++;
  }
  close(cctx->sockfd);
  cctx->status=DEAD;
  pthread_exit(NULL);
}






/* ****************************************************************************
	FUNCTION :	opCheckMain
	PURPOSE :	This function is the main function of the check
			operation threads,
	INPUT :		arg	= NULL
	OUTPUT :	None.
	RETURN :	None.
	DESCRIPTION :
 *****************************************************************************/
void *
opCheckMain (
	void	*arg)
{
  struct sockaddr_in srvsaddr,claddr;
  struct hostent cltaddr; 
#ifdef LINUX
  struct hostent *stupidlinux=NULL;
#endif
#ifdef AIX
  struct hostent_data stupidaix;
#endif
  struct linger lopt;
  uint32_t ipaddr;
  int newfd,sockfd,ncctx,i,err;
  char buffer[128];
  int	 retry;			/* To retry on EINTR */

  /*
   * Initialization
   */

  srvsaddr.sin_addr.s_addr=htonl(INADDR_ANY);
  srvsaddr.sin_family=AF_INET;
  srvsaddr.sin_port=htons(masterPort);

  /*
   * Let's go !!!
   */

  if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
  {
	perror("Socket");
	ldcltExit(1);						/*JLS 18-08-00*/
  }
  i=1;
  if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,(void *)&i,sizeof(int))!=0)
	perror("Sockopt");
  if(bind(sockfd,(struct sockaddr*)&srvsaddr,sizeof(struct sockaddr))!=0)
  {
	perror("Bind");
	ldcltExit(1);						/*JLS 18-08-00*/
  }
  if(listen(sockfd,1)!=0)
	perror("listen");
  for(ncctx=0;;)
  {
	i=sizeof(claddr);
	retry = 1;
	while (retry)
	{
#ifdef AIX
		if ((newfd=accept(sockfd,(struct sockaddr *)&claddr,(unsigned long *)&i))>=0)
#else
		if ((newfd=accept(sockfd,(struct sockaddr *)&claddr,&i))>=0)
#endif
			retry = 0;
		else
			if (errno != EINTR)
			{
				perror("Accept");
				ldcltExit(1);			/*JLS 18-08-00*/
			}
	}
	/*
	 * get client's name
	 */
	ipaddr=ntohl(claddr.sin_addr.s_addr);

#ifdef AIX
	gethostbyaddr_r((char*)&ipaddr,sizeof(ipaddr),AF_INET,&cltaddr,
		&stupidaix);
#else
#ifdef LINUX
	 gethostbyaddr_r((char*)&ipaddr,sizeof(ipaddr),AF_INET,&cltaddr,
		buffer,128, &stupidlinux, &err); 
#else
#if defined(HPUX) && defined(__LP64__)
	 gethostbyaddr((char*)&ipaddr,sizeof(ipaddr),AF_INET);
#else
	 gethostbyaddr_r((char*)&ipaddr,sizeof(ipaddr),AF_INET,&cltaddr,
		buffer,128,&err); 
#endif
#endif
#endif

	i=1;
	if(setsockopt(newfd,IPPROTO_TCP, TCP_NODELAY,(void *)&i,sizeof(int))!=0)
		perror("Nagle");
	/*
	 * Linger: when connection ends, send an RST instead of a FIN
	 * This way the client will have a fail on the first write instead of
	 * the second
	 */
	lopt.l_onoff=1;
	lopt.l_linger=0;
	if(setsockopt(newfd,SOL_SOCKET,SO_LINGER,(void*)&lopt,sizeof(struct linger))<0)
		perror("Linger");
	/*
	 * Search for an empty client slot. If a client reconnects, use the
	 * same slot
	 */
	for(i=0;i<mctx.slavesNb;i++)
	{
		if(cctx[i].calls==0)
		{
			i=ncctx++;
			break;
		}
		if(cctx[i].slaveName&&cctx[i].status==DEAD)
			if(strcmp(cctx[i].slaveName,cltaddr.h_name)==0)
				break;
	}
	if(i>=mctx.slavesNb)
	{
		fprintf (stderr, "ldclt: Too many slaves %s\n",cltaddr.h_name);
		close(newfd);
		continue;
	}

	cctx[i].sockfd=newfd;
	cctx[i].calls++;
	cctx[i].slaveName=strdup(cltaddr.h_name);
	if ((err = pthread_create (&(cctx[i].tid), NULL,
		opCheckLoop, (void *)&(cctx[i]))) != 0)
	{
		fprintf (stderr, "ldclt: %s\n", strerror (err));
		fprintf (stderr, "Error: cannot create thread opCheck for %s\n",
				cltaddr.h_name);
		fflush (stderr);
	}
    else
      mctx.slaveConn = 1;
  } 
  close(sockfd);
}






/* End of file */
