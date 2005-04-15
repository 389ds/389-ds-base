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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* cl4.h - global declarations used by the 4.0 style changelog module
 */

#ifndef CL4_H
#define CL4_H

#include "slapi-private.h"
#include "portable.h" /* GGOODREPL - is this cheating? */

#define CONFIG_CHANGELOG_SUFFIX_ATTRIBUTE     "nsslapd-changelogsuffix"

/* A place to store changelog config info */
typedef struct _chglog4Info  chglog4Info;

/* in cl4.c */	
chglog4Info* changelog4_new (Slapi_Entry *e, char *errorbuf);
void changelog4_free (chglog4Info** cl4);
void changelog4_lock (Object *obj, PRBool write);
void changelog4_unlock (Object *obj);
const char * changelog4_get_dir (const chglog4Info* cl4);
const char * changelog4_get_suffix (const chglog4Info* cl4);
time_t changelog4_get_maxage (const chglog4Info* cl4);
unsigned long changelog4_get_maxentries (const chglog4Info* cl4);
void changelog4_set_dir (chglog4Info* cl4, const char *dir);
void changelog4_set_suffix (chglog4Info* cl4, const char *suffix);
void changelog4_set_maxage (chglog4Info* cl4, const char *maxage);
void changelog4_set_maxentries (chglog4Info* cl4, const char* maxentries);

/* In cl4_suffix.c */
char *get_changelog_dataversion(const chglog4Info* cl4);
void set_changelog_dataversion(chglog4Info* cl4, const char *dataversion);

/* In cl4_config.c */
int changelog4_config_init();
void changelog4_config_destroy();

/*
 * backend configuration information
 * Previously, these two typedefs were in ../../slapd/slapi-plugin.h but
 * the CL4 code is the only remaining code that references these definitions.
 */
typedef struct config_directive
{
	char	*file_name;	/* file from which to read directive */
	int		lineno;		/* line to read */
	int		argc;		/* number of argvs */
	char	**argv;		/* directive in agrv format */
} slapi_config_directive;

typedef struct be_config
{
	char					*type;		/* type of the backend   */
	char					*suffix;	/* suffix of the backend */
	int						is_private;	/* 1 - private, 0 -not   */
	int						log_change;	/* 1 - write change to the changelog; 0 - don't */
	slapi_config_directive	*directives;/* configuration directives */
	int						dir_count;	/* number of directives */
} slapi_be_config;
  
#endif
