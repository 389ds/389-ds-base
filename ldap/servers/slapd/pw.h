/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _SLAPD_PW_H_
#define _SLAPD_PW_H_

#define PWD_MAX_NAME_LEN    10

#define PWD_HASH_PREFIX_START	'{'
#define PWD_HASH_PREFIX_END	'}'

/*
 * 
 * structure for holding password scheme info.
 */
struct pw_scheme {
	/* case-insensitive name used in prefix of passwords that use scheme */
	char	*pws_name;

	/* length of pws_name */
	int	pws_len;

	/* thread-safe comparison function; returns 0 for positive matches */
	/* userpwd is value sent over LDAP bind; dbpwd is from the database */
	int	(*pws_cmp)( char *userpwd, char *dbpwd );

	/* thread-safe encoding function (returns pointer to malloc'd string) */
	char	*(*pws_enc)( char *pwd );

	/* thread-safe decoding function (returns pointer to malloc'd string) */
	char	*(*pws_dec)( char *pwd );
};

/*
 * Public functions from pw.c:
 */
struct pw_scheme *pw_name2scheme( char *name );
struct pw_scheme *pw_val2scheme( char *val, char **valpwdp, int first_is_default );
int pw_encodevals( Slapi_Value **vals );
int checkPrefix(char *cipher, char *schemaName, char **encrypt);
struct passwordpolicyarray *new_passwdPolicy ( Slapi_PBlock *pb, char *dn );
void delete_passwdPolicy( struct passwordpolicyarray **pwpolicy);

/* function for checking the values of fine grained password policy attributes */
int check_pw_minage_value( const char *attr_name, char *value, long minval, long maxval, char *errorbuf );
int check_pw_lockduration_value( const char *attr_name, char *value, long minval, long maxval, char *errorbuf );
int check_pw_resetfailurecount_value( const char *attr_name, char *value, long minval, long maxval, char *errorbuf );
int check_pw_storagescheme_value( const char *attr_name, char *value, long minval, long maxval, char *errorbuf );

/*
 * Public functions from pw_retry.c:
 */
Slapi_Entry *get_entry ( Slapi_PBlock *pb, const char *dn );
void set_retry_cnt_mods ( Slapi_PBlock *pb, Slapi_Mods *smods, int count);

#endif /* _SLAPD_PW_H_ */
