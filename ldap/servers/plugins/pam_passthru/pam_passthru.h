/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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
#include <nspr.h>

/* Private API: to get slapd_pr_strerror() and SLAPI_COMPONENT_NAME_NSPR */
#include "slapi-private.h"

/*
 * macros
 */
#define PAM_PASSTHRU_PLUGIN_SUBSYSTEM "pam_passthru-plugin" /* for logging */
#define PAM_PASSTHRU_INT_POSTOP_DESC "PAM Passthru internal postop plugin"
#define PAM_PASSTHRU_PREOP_DESC "PAM Passthru preop plugin"
#define PAM_PASSTHRU_POSTOP_DESC "PAM Passthru postop plugin"

#define PAM_PASSTHRU_ASSERT(expr) PR_ASSERT(expr)

#define PAM_PASSTHRU_OP_NOT_HANDLED 0
#define PAM_PASSTHRU_OP_HANDLED 1
#define PAM_PASSTHRU_SUCCESS 0
#define PAM_PASSTHRU_FAILURE -1

/* #define    PAM_PASSTHRU_VERBOSE_LOGGING    */

/*
 * Plug-in globals
 */
extern PRCList *pam_passthru_global_config;

/*
 * structs
 */
typedef struct pam_passthrusuffix
{
    Slapi_DN *pamptsuffix_dn;
    struct pam_passthrusuffix *pamptsuffix_next;
} Pam_PassthruSuffix;

#define PAMPT_MISSING_SUFFIX_ERROR 0  /* error out if an included or excluded suffix is missing */
#define PAMPT_MISSING_SUFFIX_ALLOW 1  /* allow but log missing suffixes */
#define PAMPT_MISSING_SUFFIX_IGNORE 2 /* allow and don't log missing suffixes */

#define PAMPT_MISSING_SUFFIX_ERROR_STRING "ERROR"
#define PAMPT_MISSING_SUFFIX_ALLOW_STRING "ALLOW"
#define PAMPT_MISSING_SUFFIX_IGNORE_STRING "IGNORE"

typedef struct pam_passthruconfig
{
    PRCList list;
    char *dn;
    Pam_PassthruSuffix *pamptconfig_includes; /* list of suffixes to include in this op */
    Pam_PassthruSuffix *pamptconfig_excludes; /* list of suffixes to exclude in this op */
    char *filter_str;                         /* search filter used to identify bind entries to include in this op */
    Slapi_Filter *slapi_filter;               /* a Slapi_Filter version of the above filter */
    PRBool pamptconfig_fallback;              /* if false, failure here fails entire bind */
                                              /* if true, failure here falls through to regular bind */
    PRBool pamptconfig_secure;                /* if true, plugin only operates on secure connections */
    PRBool pamptconfig_thread_safe;           /* if true, the underlying pam module is thread safe */
                                              /* if false, the module is not thread safe => serialize calls */
    char *pamptconfig_pam_ident_attr;         /* name of attribute in user entry for ENTRY map method */
    int pamptconfig_map_method1;              /* how to map the BIND DN to the PAM identity */
    int pamptconfig_map_method2;              /* how to map the BIND DN to the PAM identity */
    int pamptconfig_map_method3;              /* how to map the BIND DN to the PAM identity */
#define PAMPT_MAP_METHOD_NONE -1              /* do not map */
#define PAMPT_MAP_METHOD_DN 0                 /* use the full DN as the PAM identity */
#define PAMPT_MAP_METHOD_RDN 1                /* use the leftmost RDN value as the PAM identity */
#define PAMPT_MAP_METHOD_ENTRY 2              /* use the PAM identity attribute in the entry */
    char *pamptconfig_service;                /* the PAM service name for pam_start() */
} Pam_PassthruConfig;

#define PAMPT_MAP_METHOD_DN_STRING "DN"
#define PAMPT_MAP_METHOD_RDN_STRING "RDN"
#define PAMPT_MAP_METHOD_ENTRY_STRING "ENTRY"

#define PAMPT_MISSING_SUFFIX_ATTR "pamMissingSuffix" /* single valued */
#define PAMPT_EXCLUDES_ATTR "pamExcludeSuffix"       /* multi valued */
#define PAMPT_INCLUDES_ATTR "pamIncludeSuffix"       /* multi valued */
#define PAMPT_PAM_IDENT_ATTR "pamIDAttr"             /* single valued (for now) */
#define PAMPT_MAP_METHOD_ATTR "pamIDMapMethod"       /* single valued */
#define PAMPT_FALLBACK_ATTR "pamFallback"            /* single */
#define PAMPT_SECURE_ATTR "pamSecure"                /* single */
#define PAMPT_THREAD_SAFE_ATTR "pamModuleIsThreadSafe" /* single */
#define PAMPT_SERVICE_ATTR "pamService"              /* single */
#define PAMPT_FILTER_ATTR "pamFilter"                /* single */

/*
 * public functions
 */

void pam_passthruauth_set_plugin_identity(void *identity);
void *pam_passthruauth_get_plugin_identity(void);
void pam_passthruauth_set_plugin_sdn(const Slapi_DN *plugin_sdn);
const Slapi_DN *pam_passthruauth_get_plugin_sdn(void);
const char *pam_passthruauth_get_plugin_dn(void);
void pam_passthru_read_lock(void);
void pam_passthru_write_lock(void);
void pam_passthru_unlock(void);

/*
 * pam_ptconfig.c:
 */
int pam_passthru_load_config(int skip_validate);
void pam_passthru_delete_config(void);
Pam_PassthruConfig *pam_passthru_get_config(Slapi_DN *bind_sdn);
int pam_passthru_validate_config(Slapi_Entry *e, char *returntext);
int pam_passthru_dn_is_config(Slapi_DN *sdn);
void pam_passthru_set_config_area(Slapi_DN *sdn);
Slapi_DN *pam_passthru_get_config_area(void);
void pam_passthru_free_config_area(void);

/*
 * pam_ptimpl.c
 */
int pam_passthru_pam_init(void);
int pam_passthru_pam_free(void);
int pam_passthru_do_pam_auth(Slapi_PBlock *pb, Pam_PassthruConfig *cfg);

#endif /* _PAM_PASSTHRU_H_ */
