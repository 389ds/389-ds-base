/** BEGIN COPYRIGHT BLOCK
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
