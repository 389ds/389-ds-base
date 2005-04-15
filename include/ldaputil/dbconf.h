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
#ifndef _LDAPU_DBCONF_H
#define _LDAPU_DBCONF_H

#include <stdio.h>

#ifndef NSAPI_PUBLIC
#ifdef XP_WIN32
#define NSAPI_PUBLIC __declspec(dllexport)
#else
#define NSAPI_PUBLIC 
#endif
#endif

typedef struct dbconf_propval {
    char *prop;			    /* Property name */
    char *val;			    /* Property value */
    struct dbconf_propval *next;    /* Pointer to the next prop-val pair */
} DBPropVal_t;

typedef struct dbconf_dbinfo {
    char *dbname;		/* Database name */
    char *url;			/* Database URL */
    DBPropVal_t *firstprop;	/* pointer to first property-value pair */
    DBPropVal_t *lastprop;	/* pointer to last property-value pair */
    struct dbconf_dbinfo *next;	/* pointer to next db info */
} DBConfDBInfo_t;

typedef struct {
    DBConfDBInfo_t *firstdb;	/* pointer to first db info */
    DBConfDBInfo_t *lastdb;	/* pointer to last db info */
} DBConfInfo_t;

#define DBCONF_DEFAULT_DBNAME "default"

#ifdef __cplusplus
extern "C" {
#endif

NSAPI_PUBLIC extern int dbconf_read_default_dbinfo (const char *file,
						    DBConfDBInfo_t **db_info);
NSAPI_PUBLIC extern int dbconf_read_config_file (const char *file,
						 DBConfInfo_t **conf_info);

NSAPI_PUBLIC extern int ldapu_dbinfo_attrval (DBConfDBInfo_t *db_info,
					      const char *attr, char **val);

NSAPI_PUBLIC extern void dbconf_free_confinfo (DBConfInfo_t *conf_info);
NSAPI_PUBLIC extern void dbconf_free_dbinfo (DBConfDBInfo_t *db_info);

extern void dbconf_free_propval (DBPropVal_t *propval);

extern void dbconf_print_confinfo (DBConfInfo_t *conf_info);
extern void dbconf_print_dbinfo (DBConfDBInfo_t *db_info);
extern void dbconf_print_propval (DBPropVal_t *propval);


NSAPI_PUBLIC int dbconf_output_db_directive (FILE *fp, const char *dbname,
				       const char *url);

NSAPI_PUBLIC int dbconf_output_propval (FILE *fp, const char *dbname,
				  const char *prop, const char *val,
				  const int encoded);

/* Following functions are required by certmap.c file */
extern int dbconf_read_config_file_sub (const char *file,
					const char *directive,
					const int directive_len,
					DBConfInfo_t **conf_info_out);

extern int dbconf_read_default_dbinfo_sub (const char *file,
					   const char *directive,
					   const int directive_len,
					   DBConfDBInfo_t **db_info_out);

NSAPI_PUBLIC int dbconf_get_dbnames (const char *dbmap, char ***dbnames, int *cnt);

NSAPI_PUBLIC int dbconf_free_dbnames (char **dbnames);


extern int ldapu_strcasecmp (const char *s1, const char *s2); 

#ifdef __cplusplus
}
#endif

#endif /* _LDAPU_DBCONF_H */
