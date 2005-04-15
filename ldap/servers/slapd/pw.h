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
