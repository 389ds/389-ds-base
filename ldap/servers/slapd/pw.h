/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #195302
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef _SLAPD_PW_H_
#define _SLAPD_PW_H_

// Updated to the 13 for PBKDF2_SHA256
#define PWD_MAX_NAME_LEN 13

#define PWD_HASH_PREFIX_START '{'
#define PWD_HASH_PREFIX_END '}'

/*
 * Public functions from pw.c:
 */
struct pw_scheme *pw_name2scheme(char *name);
struct pw_scheme *pw_val2scheme(char *val, char **valpwdp, int first_is_default);
int pw_encodevals(Slapi_Value **vals);
int pw_encodevals_ext(Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals);
int checkPrefix(char *cipher, char *schemaName, char **encrypt, char **algid);
struct passwordpolicyarray *new_passwdPolicy(Slapi_PBlock *pb, const char *dn);
void delete_passwdPolicy(struct passwordpolicyarray **pwpolicy);

/* function for checking the values of fine grained password policy attributes */
int check_pw_duration_value(const char *attr_name, char *value, long minval, long maxval, char *errorbuf, size_t ebuflen);
int check_pw_resetfailurecount_value(const char *attr_name, char *value, long minval, long maxval, char *errorbuf, size_t ebuflen);
int check_pw_storagescheme_value(const char *attr_name, char *value, long minval, long maxval, char *errorbuf, size_t ebuflen);

int pw_is_pwp_admin(Slapi_PBlock *pb, struct passwordpolicyarray *pwp);
/*
 * Public functions from pw_retry.c:
 */
Slapi_Entry *get_entry(Slapi_PBlock *pb, const char *dn);
int set_retry_cnt_mods(Slapi_PBlock *pb, Slapi_Mods *smods, int count);
int set_tpr_usecount_mods(Slapi_PBlock *pb, Slapi_Mods *smods, int count);

#endif /* _SLAPD_PW_H_ */
