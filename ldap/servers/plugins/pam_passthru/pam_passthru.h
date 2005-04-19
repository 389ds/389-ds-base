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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * pam_passthru.h - Pass Through Authentication shared definitions
 *
 */

#ifndef _PAM_PASSTHRU_H_
#define _PAM_PASSTHRU_H_

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include "portable.h"
#include "slapi-plugin.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */
#include "dirver.h"
#include <nspr.h>

/* Private API: to get slapd_pr_strerror() and SLAPI_COMPONENT_NAME_NSPR */
#include "slapi-private.h"

/*
 * macros
 */
#define PAM_PASSTHRU_PLUGIN_SUBSYSTEM	"pam_passthru-plugin"   /* for logging */

#define PAM_PASSTHRU_ASSERT( expr )		PR_ASSERT( expr )

#define PAM_PASSTHRU_OP_NOT_HANDLED		0
#define PAM_PASSTHRU_OP_HANDLED		1

/* #define	PAM_PASSTHRU_VERBOSE_LOGGING	*/

/*
 * structs
 */
typedef struct pam_passthrusuffix {
    Slapi_DN *pamptsuffix_dn;
    struct pam_passthrusuffix *pamptsuffix_next;
} Pam_PassthruSuffix;

#define PAMPT_MISSING_SUFFIX_ERROR  0 /* error out if an included or excluded suffix is missing */
#define PAMPT_MISSING_SUFFIX_ALLOW  1 /* allow but log missing suffixes */
#define PAMPT_MISSING_SUFFIX_IGNORE 2 /* allow and don't log missing suffixes */

#define PAMPT_MISSING_SUFFIX_ERROR_STRING "ERROR"
#define PAMPT_MISSING_SUFFIX_ALLOW_STRING "ALLOW"
#define PAMPT_MISSING_SUFFIX_IGNORE_STRING "IGNORE"

typedef struct pam_passthruconfig {
	Slapi_Mutex *lock; /* for config access */
    Pam_PassthruSuffix *pamptconfig_includes; /* list of suffixes to include in this op */
    Pam_PassthruSuffix *pamptconfig_excludes; /* list of suffixes to exclude in this op */
	PRBool pamptconfig_fallback; /* if false, failure here fails entire bind */
	                             /* if true, failure here falls through to regular bind */
    PRBool pamptconfig_secure; /* if true, plugin only operates on secure connections */
	char *pamptconfig_pam_ident_attr; /* name of attribute in user entry for ENTRY map method */
	int pamptconfig_map_method1; /* how to map the BIND DN to the PAM identity */
	int pamptconfig_map_method2; /* how to map the BIND DN to the PAM identity */
	int pamptconfig_map_method3; /* how to map the BIND DN to the PAM identity */
#define PAMPT_MAP_METHOD_NONE -1 /* do not map */
#define PAMPT_MAP_METHOD_DN 0 /* use the full DN as the PAM identity */
#define PAMPT_MAP_METHOD_RDN 1 /* use the leftmost RDN value as the PAM identity */
#define PAMPT_MAP_METHOD_ENTRY 2 /* use the PAM identity attribute in the entry */
	char *pamptconfig_service; /* the PAM service name for pam_start() */
} Pam_PassthruConfig;

#define PAMPT_MAP_METHOD_DN_STRING "DN"
#define PAMPT_MAP_METHOD_RDN_STRING "RDN"
#define PAMPT_MAP_METHOD_ENTRY_STRING "ENTRY"

#define PAMPT_MISSING_SUFFIX_ATTR "pamMissingSuffix" /* single valued */
#define PAMPT_EXCLUDES_ATTR "pamExcludeSuffix" /* multi valued */
#define PAMPT_INCLUDES_ATTR "pamIncludeSuffix" /* multi valued */
#define PAMPT_PAM_IDENT_ATTR "pamIDAttr" /* single valued (for now) */
#define PAMPT_MAP_METHOD_ATTR "pamIDMapMethod" /* single valued */
#define PAMPT_FALLBACK_ATTR "pamFallback" /* single */
#define PAMPT_SECURE_ATTR "pamSecure" /* single */
#define PAMPT_SERVICE_ATTR "pamService" /* single */

/*
 * public functions
 */

void pam_passthruauth_set_plugin_identity(void * identity);
void * pam_passthruauth_get_plugin_identity();

/*
 * pam_ptconfig.c:
 */
int pam_passthru_config( Slapi_Entry *config_e );
Pam_PassthruConfig *pam_passthru_get_config( void );
int pam_passthru_check_suffix(Pam_PassthruConfig *cfg, char *binddn);

/*
 * pam_ptimpl.c
 */
int pam_passthru_do_pam_auth(Slapi_PBlock *pb, Pam_PassthruConfig *cfg);

#endif	/* _PAM_PASSTHRU_H_ */
