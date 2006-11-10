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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/***********************************************************************
** $Id: cron_conf.h,v 1.6 2006/11/10 23:45:49 nhosoi Exp $
**
**
** NAME
**  cron_conf.h
**
** DESCRIPTION
**
**
** AUTHOR
**   <robw@netscape.com>
**
***********************************************************************/

#ifndef _CRON_CONF_H_
#define _CRON_CONF_H_

/***********************************************************************
** Includes
***********************************************************************/
/* read and write to cron.conf, cron_conf.c */
/* Alex Feygin, 3/22/96                     */
typedef struct cron_conf_obj
{
  char *name;
  char *command;
  char *dir;
  char *user;
  char *start_time;
  char *days;
} 
cron_conf_obj;
 
typedef struct cron_conf_list
{
  char *name;
  cron_conf_obj *obj;
  struct cron_conf_list *next;
} 
cron_conf_list;
 
/* Reads cron.conf to a null terminated list of cron_conf_objects; returns
   0 if unable to do a read; 1 otherwise */
int cron_conf_read();
 
/* gets a cron object, NULL if it doesnt exist */
cron_conf_obj *cron_conf_get(char *name);
 
/* returns a NULL-terminated cron_conf_list of all the cron conf objects */
cron_conf_list *cron_conf_get_list();
 
/* Creates a cron conf object; all these args get STRDUP'd in the function
   so make sure to free up the space later if need be */
cron_conf_obj *cron_conf_create_obj(char *name, char *command,
						 char *dir,  char *user, 
						 char *start_time, char *days);
 
/* Puts a cron conf object into list or updates it if it already in there.
   Returns either the object passed or the object in there already;
   cco may be FREE'd during this operation so if you need the object
   back, call it like so:
   
   cco = cron_conf_set(cco->name, cco);  
 
   calling cron_conf_set with a NULL cco will cause the 'name' object
   to be deleted.
*/
cron_conf_obj *cron_conf_set(char *name, cron_conf_obj *cco);
 
/* write out current list of cron_conf_objects to cron.conf file */
void cron_conf_write();
 
/* free all cron conf data structures */
void cron_conf_free();

#define ADMCONFDIR "../config/"


#endif /* _CRON_CONF_H_ */
