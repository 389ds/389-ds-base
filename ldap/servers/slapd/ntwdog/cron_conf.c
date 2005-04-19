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

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <stdlib.h>
#include <stdarg.h>
#include "cron_conf.h"

#define NSAPI_PUBLIC

#ifndef BUF_SIZE
#define BUF_SIZE 4096
#endif

#ifndef S_BUF_SIZE
#define S_BUF_SIZE 1024
#endif

#ifdef XP_WIN32
#pragma warning (disable: 4005)  // macro redifinition //
#define MALLOC(size)     malloc(size)
#define REALLOC(x, size) realloc(x, size)
#define FREE(x)          free((void*) x)
#define STRDUP(x)        strdup(x)
#define strcasecmp(x, y) stricmp(x, y)
#pragma warning (default: 4005)  // macro redifinition //
#endif

static char *admroot;
static char *nsroot;

#define DAILY "Sun Mon Tue Wed Thu Fri Sat"

static cron_conf_list *cclist   = NULL;
static cron_conf_list *cctail   = NULL;
static char           *conffile = NULL;

#ifndef CRON_CONF_STAND_ALONE
static void set_roots()
{
  char *ar = ADMCONFDIR;
  if(ar)
      admroot = STRDUP(ar);
}
#endif

/* General note: strtok() is not MT safe on Unix , but it is okay to call 
   here because this file is NT only and strtok() is MT safe on NT */

static char *nocr(char *buf)
{
  if (buf)
    {
      if(buf[strlen(buf) - 1] == '\n')
	buf[strlen(buf) - 1] = '\0';
    }
    
  return buf;
}

static int debug(char *fmt, ...)
{
  va_list args;
  char buf[BUF_SIZE];
 
  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);

  fprintf(stdout, "<<DEBUG>> %s <<DEBUG>>\n", buf);
  fflush(stdout);

  return 1;
}

static char *get_conf_file()
{
  static char conffile [S_BUF_SIZE];
  char        nsconfile[S_BUF_SIZE];
  char        buf      [BUF_SIZE];
  char       *r, *p;
  FILE       *fp;
  int         flag = 0;

  if (admroot)
    sprintf(nsconfile, "%s/ns-cron.conf", admroot);
  else
    sprintf(nsconfile, "%s/admin-serv/config/ns-cron.conf", nsroot);
  
  if (!(fp = fopen(nsconfile, "r")))
    return NULL;

  while(fgets(buf, sizeof(buf), fp))
    {
      r = strtok(buf, " \t\n");
      if (!r) /* bad line, ignore */
	continue;
 
      p = strtok(NULL, " \t\n");
      if (!p) /* bad line, ignore */
	continue;
 
      if (!strcasecmp(r, "ConfFile"))
	{
	 /* if filename without path is specified, default to admin svr dir */
	 if((strchr(p, '\\') == NULL) && 
	    (strchr(p, '/') == NULL))
	    sprintf(conffile, "%s/%s", admroot, p);
	 else
	    sprintf(conffile, "%s", p);
	 flag++;
	 break;
	}
    }

  fclose(fp);

  if (!flag)
    return NULL;

  return conffile;
}


#ifndef CRON_CONF_STAND_ALONE
NSAPI_PUBLIC
#endif
cron_conf_obj *cron_conf_create_obj(char *name, char *command, char *dir,
				    char *user, char *start_time, char *days)
{
  cron_conf_obj *object;
  char *d = NULL;

  object = (cron_conf_obj*)MALLOC(sizeof(cron_conf_obj));
 
  object->name       = (name)       ? STRDUP(name)       : NULL;
  object->command    = (command)    ? STRDUP(command)    : NULL;
  object->dir        = (dir)        ? STRDUP(dir)        : NULL;
  object->user       = (user)       ? STRDUP(user)       : NULL;
  object->start_time = (start_time) ? STRDUP(start_time) : NULL;

#if 1
  if (days)
    {
      if (!(strcasecmp(days, "Daily")))
	d = STRDUP(DAILY);
      else
	d = STRDUP(days);
    }
#else
  d = STRDUP("Wed Thu");
#endif

  object->days = d;

  return object;
}


static void cron_conf_free_listobj(cron_conf_list *lobj)
{
  cron_conf_obj *obj = lobj->obj;


  if (obj)
    {
      if(obj->name)       FREE(obj->name);
      if(obj->command)    FREE(obj->command);
      if(obj->dir)        FREE(obj->dir);
      if(obj->user)       FREE(obj->user);
      if(obj->start_time) FREE(obj->start_time);
      if(obj->days)       FREE(obj->days);
      
      FREE(obj);
    }
 
  FREE(lobj);
}


static cron_conf_obj *get_object(FILE *fp)
{
  cron_conf_obj *object;
  char name      [S_BUF_SIZE];
  char command   [S_BUF_SIZE];
  char dir       [S_BUF_SIZE];
  char user      [S_BUF_SIZE]; 
  char start_time[S_BUF_SIZE];
  char days      [S_BUF_SIZE];
  char buf       [BUF_SIZE];
  char *p, *q;
  int flag = 0;
  int hascom, hasdir, hasuser, hastime, hasdays;

  p = fgets(buf, sizeof(buf), fp);

  if (!p)
    return NULL;
  /* else debug("Read line '%s'", nocr(buf)); */

  if (strncmp(buf, "<Object", 7))
    return NULL;

  hascom = hasdir = hasuser = hastime = hasdays = 0;

  p = strtok(buf,  "<=>\n\t ");
  if (!p)
    return NULL;

  p = strtok(NULL, "<=>\n\t ");
  if (!p)
    return NULL;

  p = strtok(NULL, "<=>\n\t ");
  if (!p)
    return NULL;

  sprintf(name, "%s", p);
  /* debug("Setting name to '%s'", name); */

  while(fgets(buf, sizeof(buf), fp))
    {
      /* debug("Read line '%s'", nocr(buf)); */

      p = strtok(buf, " \t\n");

      if (!p)
	continue;

      if (!strcasecmp(p, "</Object>"))
	{
	  flag++;
	  break;
	}

      if(!strcasecmp(p, "Command"))
	{
	  q = strtok(NULL, "\n");

	  if (q)
	    q = strchr(q, '\"');

	  if (q)
	    q++;
      
	  if (q)
	    {
	      if (!hascom)
		{
		  /* get rid of quotes */
		  p = strrchr(q, '\"');

		  if (p)
		    *p = '\0';

		  if (q)
		    {
		      PR_snprintf(command, S_BUF_SIZE, "%s", q);
		      /* debug("Setting command to '%s'", command); */
		      hascom++;
		    }
		}
	      else /* already has a command */
		;  /* ignore */
	    }
	  continue;
	}

      if(!strcasecmp(p, "Dir"))
	{
	  q = strtok(NULL, "\n");

	  if (q)
	    q = strchr(q, '\"');

	  if (q)
	    q++;
      
	  if (q)
	    {
	      if (!hasdir)
		{
		  /* get rid of quotes */
		  p = strrchr(q, '\"');

		  if (p)
		    *p = '\0';

		  if (q)
		    {
		      PR_snprintf(dir, S_BUF_SIZE, "%s", q);
		      /* debug("Setting dir to '%s'", dir); */
		      hasdir++;
		    }
		}
	      else /* already has a dir */
		;  /* ignore */
	    }
	  continue;
	}

      else if(!strcasecmp(p, "User"))
	{
	  q = strtok(NULL, " \t\n");
      
	  if (q)
	    {
	      if (!hasuser)
		{
		  PR_snprintf(user, S_BUF_SIZE, "%s", q);
		  /* debug("Setting user to '%s'", user); */
		  hasuser++;
		}
	      else /* already has a user */
		;  /* ignore */
	    }
	  continue;
	}

      else if(!strcasecmp(p, "Time"))
	{
	  q = strtok(NULL, "\n");
	  
	  if (q)
	    {
	      if (!hastime)
		{
		  PR_snprintf(start_time, S_BUF_SIZE, "%s", q);
		  /* debug("Setting time to '%s'", start_time); */
		  hastime++;
		}
	      else /* already has a time */
		;  /* ignore */
	    }
	  continue;
	}

      else if(!strcasecmp(p, "Days"))
	{
	  q = strtok(NULL, "\n");

	  if (q)
	    {
	      if (!hasdays)
		{
		  PR_snprintf(days, S_BUF_SIZE, "%s", q);
		  /* debug("Setting days to '%s'", days); */
		  hasdays++;
		}
	      else /* already has days */
		;  /* ignore */
	    }
	  continue;
	}

      else
	{
	  /* gibberish...  ignore... will be fixed when
	     file is rewritten */
	  continue;	  
	}
    }

  object = cron_conf_create_obj(name,
				(hascom)  ? command    : NULL, 
				(hasdir)  ? dir        : NULL,
				(hasuser) ? user       : NULL, 
				(hastime) ? start_time : NULL, 
				(hasdays) ? days       : NULL);

  return object;
}


static void cron_conf_write_stream(FILE *fp)
{
  cron_conf_obj *obj;
  cron_conf_list *lobj;

  for(lobj = cclist; lobj; lobj = lobj->next)
    {
      obj = lobj->obj;

      fprintf(fp, "<Object name=%s>\n", (obj->name) ? obj->name : "?");
      fprintf(fp, "    Command \"%s\"\n", (obj->command) ? obj->command : "?");
      if (obj->dir) 
	fprintf(fp, "    Dir \"%s\"\n", obj->dir);
      if (obj->user) 
	fprintf(fp, "    User %s\n", obj->user);
      fprintf(fp, "    Time %s\n", (obj->start_time) ? obj->start_time : "?");
      fprintf(fp, "    Days %s\n", (obj->days) ? obj->days : "?");
      fprintf(fp, "</Object>\n");
    }
}


static void cron_conf_delete(char *name, cron_conf_obj *cco)
{
  cron_conf_list *lobj = NULL;
  cron_conf_list *pobj = NULL;

  if (!cclist)
    return;

  if (!strcmp(cclist->name, name))
    {
      lobj = cclist;
      cclist = cclist->next;
      if (cctail == lobj)
	cctail = cclist;

      cron_conf_free_listobj(lobj);
    }
  else
    {
      pobj = cclist;

      for(lobj = cclist->next; lobj; lobj = lobj->next)
	{
	  if(!strcmp(lobj->name, name))
	    {
	      if (lobj == cctail)
		cctail = pobj;

	      pobj->next = lobj->next;
	      cron_conf_free_listobj(lobj);

	      break;
	    }

	  pobj = lobj;
	}
    }

  return;
}

#ifndef CRON_CONF_STAND_ALONE
NSAPI_PUBLIC
#endif
int cron_conf_read()
{
  FILE *fp;
  cron_conf_obj *obj;
  cron_conf_list *lobj;

#ifndef CRON_CONF_STAND_ALONE
  set_roots();
#endif

  if (!(conffile = get_conf_file()))
    {
      /* debug("Conffile is null"); */
      return 0;
    }
  /* else debug("Conffile: '%s'", conffile); */

  if (!(fp = fopen(conffile, "r")))
    {
      /* debug("Couldn't open conffile"); */
      return 0;
    }

  while((obj = get_object(fp)))
    {
      lobj       = (cron_conf_list*)MALLOC(sizeof(struct cron_conf_list));
      lobj->name = obj->name;
      lobj->obj  = obj;
      lobj->next = NULL;

      /* debug("Created a list object named '%s'", lobj->name); */

      if(cclist == NULL) /* first object */
	{
	  cclist = cctail = lobj;
	}
      else
	{
	  cctail->next = lobj;
	  cctail       = lobj;
	}

      /* debug("List now, head: '%s', tail: '%s'", 
	 cclist->name, cctail->name); */
    }

  fclose(fp);

  return 1;
}

#ifndef CRON_CONF_STAND_ALONE
NSAPI_PUBLIC
#endif
cron_conf_obj *cron_conf_get(char *name)
{
  cron_conf_obj  *obj  = NULL;
  cron_conf_list *lobj = NULL;

 /* find object */
  for(lobj = cclist; lobj; lobj = lobj->next)
    {
      if(!strcmp(lobj->name, name))
	{
	  obj = lobj->obj;
	  break;
	}
    }

#if 0
  if (obj)
    {
      debug("Found object %s", obj->name);
      debug("obj->command = %s", (obj->command) ? obj->command : "NULL");
      debug("obj->dir = %s", (obj->dir) ? obj->dir : "NULL");
      debug("obj->user = %s", (obj->user) ? obj->user : "NULL");
      debug("obj->start_time = %s", (obj->start_time) ? obj->start_time : "NULL");
      debug("obj->days = %s", (obj->days) ? obj->days : "NULL");
    }
#endif

  return obj;
}


#ifndef CRON_CONF_STAND_ALONE
NSAPI_PUBLIC
#endif
cron_conf_list *cron_conf_get_list()
{
  return cclist;
}

#ifndef CRON_CONF_STAND_ALONE
NSAPI_PUBLIC
#endif
cron_conf_obj *cron_conf_set(char *name, cron_conf_obj *cco)
{
  cron_conf_obj  *obj  = NULL;
  cron_conf_list *lobj = NULL;

  if (!name)
    return NULL;

  if (!cco)
    {
      cron_conf_delete(name, cco);
      return NULL;
    }
  else /* cco exists */
    {
      /* find object */
      obj = cron_conf_get(name);


      if (obj)   /* found it */
	{
	  if (cco->command)
	    {
	      FREE(obj->command);
	      obj->command = cco->command;
	    }

	  if (cco->dir)
	    {
	      FREE(obj->dir);
	      obj->dir = cco->dir;
	    }

	  if (cco->user)
	    {
	      FREE(obj->user);
	      obj->user = cco->user;
	    }

	  if (cco->start_time)
	    {
	      FREE(obj->start_time);
	      obj->start_time = cco->start_time;
	    }

	  if (cco->days)
	    {
	      FREE(obj->days);
	      obj->days = cco->days;
	    }

	  FREE(cco);
	}
      else /* couldn't find it */
	{
	  obj = cco;
	  
	  lobj       = (cron_conf_list*)MALLOC(sizeof(cron_conf_list));
	  lobj->name = obj->name;
	  lobj->obj  = obj;
	  lobj->next = NULL;
	  
	  if(cclist == NULL) /* first object */
	    {
	      cclist = cctail = lobj;
	    }
	  else
	    {
	      cctail->next = lobj;
	      cctail = lobj;
	    }
	}
    }

  return obj;
}

void cron_conf_write()
{
  FILE *fp;

  if (!conffile)
    conffile = get_conf_file();

  if(!(fp = fopen(conffile, "w")))
    return;

  cron_conf_write_stream(fp);

  fclose(fp);
}


#ifndef CRON_CONF_STAND_ALONE
NSAPI_PUBLIC
#endif
void cron_conf_free()
{
  cron_conf_list  *lobj  = NULL;
 
  /* find object */
  while(cclist)
    {
      lobj   = cclist;
      cclist = cclist->next;

      cron_conf_free_listobj(lobj);
    }

  cclist = cctail = NULL;
}

