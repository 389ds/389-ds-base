/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/***********************************************************************
** $Id: cron_conf.h,v 1.1 2005/01/21 00:40:52 cvsadm Exp $
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

#define MAGNUS_CONF "magnus.conf"
#define ADMCONFDIR "../config/"


#endif /* _CRON_CONF_H_ */
