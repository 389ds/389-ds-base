/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/***********************************************************************
**
** NAME
**  plugin-utils.h
**
** DESCRIPTION
**
**
** AUTHOR
**   <rweltman@netscape.com>
**
***********************************************************************/

#ifndef _PLUGIN_UTILS_H_
#define _PLUGIN_UTILS_H_

/***********************************************************************
** Includes
***********************************************************************/

#include <slapi-plugin.h>
/*
 * slapi-plugin-compat4.h is needed because we use the following deprecated
 * functions:
 *
 * slapi_search_internal()
 * slapi_modify_internal()
 */
#include "slapi-plugin-compat4.h"
#include <dirlite_strings.h>
#include <stdio.h>
#include <string.h>
#ifdef _WINDOWS
#undef strcasecmp
#define strcasecmp strcmpi
#endif
#include "dirver.h"

#ifdef LDAP_DEBUG
#ifndef DEBUG
#define DEBUG
#endif
#endif

#define BEGIN do {
#define END } while(0);

int initCounterLock();
int op_error(int internal_error);
Slapi_PBlock *readPblockAndEntry( const char *baseDN, const char *filter,
								  char *attrs[] );
int entryHasObjectClass(Slapi_PBlock *pb, Slapi_Entry *e,
						const char *objectClass);
Slapi_PBlock *dnHasObjectClass( const char *baseDN, const char *objectClass );
Slapi_PBlock *dnHasAttribute( const char *baseDN, const char *attrName );
int setCounter( Slapi_Entry *e, const char *attrName, int value );
int updateCounter( Slapi_Entry *e, const char *attrName, int increment );
int updateCounterByDN( const char *dn, const char *attrName, int increment );

typedef struct DNLink {
	char *dn;
	void *data;
	struct DNLink *next;
} DNLink;

DNLink *cacheInit( void );
DNLink *cacheAdd( DNLink *root, char *dn, void *data );
char *cacheRemove( DNLink *root, char *dn );
int cacheDelete( DNLink *root, char *dn );
DNLink *cacheFind( DNLink *root, char *dn );

#endif /* _PLUGIN_UTILS_H_ */
