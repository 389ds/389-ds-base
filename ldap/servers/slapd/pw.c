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
 * Copyright (C) 2009 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #195302
 *
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * slapd hashed password routines
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sechash.h>
#if defined( _WIN32 )
#undef DEBUG
#endif /*  _WIN32 */
#if defined( _WIN32 )
#undef LDAPDebug
#endif	/*  _WIN32 */
#if defined( USE_MOZLDAP )
#define LDAP_MOD_OP (0x0007)
#endif /* USE_MOZLDAP */

#include "slap.h"


#define DENY_PW_CHANGE_ACI "(targetattr = \"userPassword\") ( version 3.0; acl \"disallow_pw_change_aci\"; deny (write ) userdn = \"ldap:///self\";)"
#define GENERALIZED_TIME_LENGTH 15

static int pw_in_history(Slapi_Value **history_vals, const Slapi_Value *pw_val);
static int update_pw_history( Slapi_PBlock *pb, const Slapi_DN *sdn, char *old_pw );
static int check_trivial_words (Slapi_PBlock *, Slapi_Entry *, Slapi_Value **,
		char *attrtype, int toklen, Slapi_Mods *smods );
static int pw_boolean_str2value (const char *str);
/* static LDAPMod* pw_malloc_mod (char* name, char* value, int mod_op); */


/*  
 * We want to be able to return errors to internal operations (which
 * can come from the password change extended operation). So we have
 * a special result function that does the right thing for an internal op.
 */

static void
pw_send_ldap_result(
    Slapi_PBlock	*pb,
    int			err,
    char		*matched,
    char		*text,
    int			nentries,
    struct berval	**urls
)
{
	int internal_op = 0;
	Slapi_Operation *operation = NULL;
	
	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
	internal_op= operation_is_flag_set(operation, OP_FLAG_INTERNAL);

	if (internal_op) {
		slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &err);
	} else {
		send_ldap_result(pb, err, matched, text, nentries, urls);
	}
}

/*
 * Like slapi_value_find, except for passwords.
 * returns 0 if password "v" is found in "vals"; non-zero otherwise
 */
SLAPI_DEPRECATED int
slapi_pw_find(
    struct berval	**vals,
    struct berval	*v
)
{
	int rc;
	Slapi_Value **svin_vals= NULL;
	Slapi_Value svin_v;
	slapi_value_init_berval(&svin_v,v);
	valuearray_init_bervalarray(vals,&svin_vals); /* JCM SLOW FUNCTION */
	rc= slapi_pw_find_sv(svin_vals,&svin_v);
	valuearray_free(&svin_vals);
	value_done(&svin_v);
	return rc;
}

/*
 * Like slapi_value_find, except for passwords.
 * returns 0 if password "v" is found in "vals"; non-zero otherwise
 */

int
slapi_pw_find_sv(
    Slapi_Value **vals,
    const Slapi_Value *v
)
{
	struct pw_scheme	*pwsp;
	char			*valpwd;
    int     		i;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> slapi_pw_find value: \"%s\"\n", slapi_value_get_string(v), 0, 0 ); /* JCM Innards */

    for ( i = 0; vals[i] != NULL; i++ )
    {
		pwsp = pw_val2scheme( (char*)slapi_value_get_string(vals[i]), &valpwd, 1 ); /* JCM Innards*/
		if ( pwsp != NULL && 
			(*(pwsp->pws_cmp))( (char*)slapi_value_get_string(v), valpwd ) == 0 ) /* JCM Innards*/
		{
			LDAPDebug( LDAP_DEBUG_TRACE,
			    "<= slapi_pw_find matched \"%s\" using scheme \"%s\"\n",
			    valpwd, pwsp->pws_name, 0 );
			free_pw_scheme( pwsp );
            return( 0 );	/* found it */
        }
		free_pw_scheme( pwsp );
	}
 
	LDAPDebug( LDAP_DEBUG_TRACE, "<= slapi_pw_find no matching password\n", 0, 0, 0 );

    return( 1 );	/* no match */
}

/* Checks if the specified value is encoded.
   Returns 1 if it is and 0 otherwise 
 */
int slapi_is_encoded (char *value)
{
	struct pw_scheme *is_hashed = NULL;
	int is_encoded = 0;

	is_hashed  = pw_val2scheme ( value, NULL, 0 );
	if ( is_hashed != NULL )
	{
		free_pw_scheme( is_hashed );
		is_encoded = 1;
	}
	return (is_encoded);
}


char* slapi_encode (char *value, char *alg)
{
	return( slapi_encode_ext( NULL, NULL, value, alg ) );
}

char* slapi_encode_ext (Slapi_PBlock *pb, const Slapi_DN *sdn, char *value, char *alg)
{
	struct pw_scheme *enc_scheme = NULL;
	char *(*pws_enc) ( char *pwd ) = NULL;
	char *hashedval = NULL;
	passwdPolicy *pwpolicy=NULL;

	if (alg == NULL) /* use local scheme, or global if we can't fetch local */
	{
		pwpolicy = new_passwdPolicy(pb, (char*)slapi_sdn_get_ndn(sdn) );
		pws_enc = pwpolicy->pw_storagescheme->pws_enc;

		if (pws_enc == NULL)
		{
			slapi_log_error( SLAPI_LOG_FATAL, NULL, 
							"slapi_encode: no server scheme\n" );
			return NULL;		
		}
	}
	else
	{
		enc_scheme = pw_name2scheme(alg);
		if ( enc_scheme == NULL) 
		{
			char * scheme_list = plugin_get_pwd_storage_scheme_list(PLUGIN_LIST_PWD_STORAGE_SCHEME);
			if ( scheme_list != NULL ) {
				slapi_log_error( SLAPI_LOG_FATAL, NULL, 
								"slapi_encode: invalid scheme - %s\n"
								"Valid values are: %s\n", alg, scheme_list );
				slapi_ch_free((void **)&scheme_list);
			} else {
				slapi_log_error( SLAPI_LOG_FATAL, NULL,
								"slapi_encode: invalid scheme - %s\n"
								"no pwdstorage scheme plugin loaded", alg);
			}
			return NULL;
		}
		pws_enc = enc_scheme->pws_enc;
		free_pw_scheme(enc_scheme);
	}
	
	hashedval = (*pws_enc)(value);

	return hashedval;
}

/*
 * Return a pointer to the pw_scheme struct for scheme "name"
 * NULL is returned is no matching scheme is found.
 */

struct pw_scheme *
pw_name2scheme( char *name )
{
	struct pw_scheme	*pwsp;
	struct slapdplugin *p;

	if ( (p = plugin_get_pwd_storage_scheme(name, strlen(name), PLUGIN_LIST_PWD_STORAGE_SCHEME)) != NULL ) {
		pwsp =  (struct pw_scheme *) slapi_ch_malloc (sizeof(struct pw_scheme));
		if ( pwsp != NULL ) {
			typedef int (*CMPFP)(char *, char *);
			typedef char * (*ENCFP)(char *);
			pwsp->pws_name = slapi_ch_strdup( p->plg_pwdstorageschemename );
			pwsp->pws_cmp = (CMPFP)p->plg_pwdstorageschemecmp;
			pwsp->pws_enc = (ENCFP)p->plg_pwdstorageschemeenc;
			pwsp->pws_len = strlen(pwsp->pws_name) ;
			return(pwsp);
		}
	}

	return( NULL );
}

void free_pw_scheme(struct pw_scheme *pwsp)
{
	if ( pwsp != NULL )
	{
		slapi_ch_free( (void**)&pwsp->pws_name );
		slapi_ch_free( (void**)&pwsp );
	}
}

/*
 * Return the password scheme for value "val".  This is determined by
 * checking "val" against our scheme prefixes.
 *
 * If "valpwdp" is not NULL, it is set to point to the value with any
 * prefix removed.
 *
 * If no matching scheme is found and first_is_default is non-zero, the
 * first scheme is returned.  If no matching scheme is found and
 * first_is_default is zero, NULL is returned.
 */

struct pw_scheme *
pw_val2scheme( char *val, char **valpwdp, int first_is_default )
{
	struct pw_scheme	*pwsp;
    int     		namelen, prefixlen;
	char			*end, buf[ PWD_MAX_NAME_LEN + 1 ];

	if ( *val != PWD_HASH_PREFIX_START ||
	    ( end = strchr( val, PWD_HASH_PREFIX_END )) == NULL ||
	    ( namelen = end - val - 1 ) > PWD_MAX_NAME_LEN ) {
		if ( !first_is_default ) {
			return( NULL );
		}
		pwsp = pw_name2scheme("CLEAR");	/* default to first scheme */
		prefixlen = 0;
	} else {
		memcpy( buf, val + 1, namelen );
		buf[ namelen ] = '\0';
		pwsp = pw_name2scheme(buf);
		if ( pwsp == NULL ) {
            if ( !first_is_default ) {
                return( NULL );
            }
			pwsp = pw_name2scheme("CLEAR");
			prefixlen = 0;
		} else {
			prefixlen = pwsp->pws_len + 2;
		}
	}

	if ( valpwdp != NULL ) {
		*valpwdp = val + prefixlen;
	}

	return( pwsp );
}


/*
 * re-encode the password values in "vals" using a hashing algorithm
 * vals[n] is assumed to be an alloc'd Slapi_Value that can be free'd and
 * replaced.  If a value is already encoded, we do not re-encode it.
 * Return 0 if all goes well and < 0 if an error occurs.
 */

int
pw_encodevals( Slapi_Value **vals )
{
	return( pw_encodevals_ext( NULL, NULL, vals ) );
}

/*
 * Same as pw_encodevals, except if a pb and sdn are passed in, we will try
 * to check the password scheme specified by local password policy.
 */
int
pw_encodevals_ext( Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals )
{
	int	i;
	passwdPolicy *pwpolicy=NULL;
	char *(*pws_enc) ( char *pwd ) = NULL;

	if ( (NULL == pb) || (NULL == vals) ) {
		return( 0 );
	}
 
	/* new_passwdPolicy gives us a local policy if sdn and pb are set and
	   can be used to find a local policy, else we get the global policy */
	pwpolicy = new_passwdPolicy(pb, sdn ? (char*)slapi_sdn_get_ndn(sdn) : NULL);
	if (pwpolicy) {
		if (pwpolicy->pw_storagescheme) {
			pws_enc = pwpolicy->pw_storagescheme->pws_enc;
		}
	}

	/* Password scheme encryption function was not found */
	if ( pws_enc == NULL ) {
		return( 0 );
	}

	for ( i = 0; vals[ i ] != NULL; ++i ) {
		struct pw_scheme *pwsp = NULL;
		char *enc = NULL;
		if ( (pwsp=pw_val2scheme( (char*)slapi_value_get_string(vals[ i ]), NULL, 0)) != NULL ) { /* JCM Innards */
			/* If the value already specifies clear storage, call the
			 * clear storage plug-in */
			if (strcasecmp( pwsp->pws_name, "clear" ) == 0) {
				enc = (*pwsp->pws_enc)( (char*)slapi_value_get_string(vals[ i ]) );
			} else {
				free_pw_scheme( pwsp );
				continue;	/* don't touch pre-encoded values */
			}
		}
		free_pw_scheme( pwsp );

		if ((!enc) && (( enc = (*pws_enc)( (char*)slapi_value_get_string(vals[ i ]) )) == NULL )) {
			return( -1 );
		}

                slapi_value_free(&vals[ i ]);
                vals[ i ] = slapi_value_new_string_passin(enc);
	}

	return( 0 );
}

/*
 * Check if the prefix of the cipher is the one that is supposed to be 
 * Extract from the whole cipher the encrypted password (remove the prefix)
 */
int checkPrefix(char *cipher, char *schemaName, char **encrypt)
{
	int namelen;
	/* buf contains the extracted schema name */
	char *end, buf[ 3*PWD_MAX_NAME_LEN + 1 ];

	if ( (*cipher == PWD_HASH_PREFIX_START) &&
		 ((end = strchr(cipher, PWD_HASH_PREFIX_END)) != NULL) &&
		 ((namelen = end - cipher - 1 ) <= (3*PWD_MAX_NAME_LEN)) )
	{
		memcpy( buf, cipher + 1, namelen );
		buf[ namelen ] = '\0';
		if ( strcasecmp( buf, schemaName) != 0 )
		{
			/* schema names are different, error */
			return 1;
		}
		else
		{
			/* extract the encrypted password */
			*encrypt = cipher + strlen(schemaName) + 2;
			return 0;
		}
	}
	/* cipher is not prefixed, already in clear ? */
	return -1;
}

/*
* Decode the attribute "attr_name" with one of the reversible encryption mechanism 
* Returns -1 on error
* Returns 0 on success with strdup'ed plain
* Returns 1 on success with *plain=cipher
*/
int
pw_rever_decode(char *cipher, char **plain, const char * attr_name)
{
	struct pw_scheme	*pwsp = NULL;
	struct slapdplugin	*p = NULL;

	int ret_code = 1;

	for ( p = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME); p != NULL; p = p->plg_next )
	{
		char *L_attr = NULL;
		int i = 0;
		char *encrypt = NULL;
		int prefixOK = -1;

		/* Get the appropriate decoding function */
		for ( L_attr = p->plg_argv[i]; i<p->plg_argc; L_attr = p->plg_argv[++i] )
		{
			if (slapi_attr_types_equivalent(L_attr, attr_name))
			{
				typedef int (*CMPFP)(char *, char *);
				typedef char * (*ENCFP)(char *);

				pwsp =  (struct pw_scheme *) slapi_ch_calloc (1, sizeof(struct pw_scheme));

				pwsp->pws_dec = (ENCFP)p->plg_pwdstorageschemedec;
				pwsp->pws_name = slapi_ch_strdup( p->plg_pwdstorageschemename );
				pwsp->pws_len = strlen(pwsp->pws_name) ;
				if ( pwsp->pws_dec != NULL )	
				{
					/* check that the prefix of the cipher is the same name
						as the scheme name */
					prefixOK = checkPrefix(cipher, pwsp->pws_name, &encrypt);
					if ( prefixOK == -1 )
					{
						/* no prefix, already in clear ? */
						*plain = cipher;
						ret_code = 1;
						goto free_and_return;
					}
					else if ( prefixOK == 1 )
					{
						/* scheme names are different */
						ret_code = -1;
						goto free_and_return;
					}
					else
					{
						if ( ( *plain = (pwsp->pws_dec)( encrypt )) == NULL ) 
						{
							/* pb during decoding */
							ret_code = -1;
							goto free_and_return;
						}
						/* decoding is OK */
						ret_code = 0;
						goto free_and_return;
					}
				}
				free_pw_scheme( pwsp );
				pwsp = NULL;
			}
		}
	}
free_and_return:
	if ( pwsp != NULL )
	{
		free_pw_scheme( pwsp );
	}
	return(ret_code);
}

/*
 * Encode the attribute "attr_name" with one of the reversible encryption mechanism 
 */
int
pw_rever_encode(Slapi_Value **vals, char * attr_name)
{
	char	*enc;
	struct pw_scheme	*pwsp = NULL;
	struct slapdplugin	*p;

	if (vals == NULL){
		return (0);
	}
	
	for ( p = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME); p != NULL; p = p->plg_next )
	{
		char *L_attr = NULL;
		int i = 0;

		/* Get the appropriate encoding function */
		for ( L_attr = p->plg_argv[i]; i<p->plg_argc; L_attr = p->plg_argv[++i] )
		{
			if (slapi_attr_types_equivalent(L_attr, attr_name))
			{
				typedef int (*CMPFP)(char *, char *);
				typedef char * (*ENCFP)(char *);

				pwsp =  (struct pw_scheme *) slapi_ch_calloc (1, sizeof(struct pw_scheme));

				pwsp->pws_enc = (ENCFP)p->plg_pwdstorageschemeenc;
				pwsp->pws_name = slapi_ch_strdup( p->plg_pwdstorageschemename );
				if ( pwsp->pws_enc != NULL )	
				{
					for ( i = 0; vals[i] != NULL; ++i ) 
					{
						char *encrypt = NULL;
						int prefixOK;

						prefixOK = checkPrefix((char*)slapi_value_get_string(vals[i]), 
												pwsp->pws_name,
												&encrypt);
						if ( prefixOK == 0 )
						{
							/* Don't touch already encoded value */
							continue;   /* don't touch pre-encoded values */
						}
						else if (prefixOK == 1 )
						{
							/* credential is already encoded, but not with this schema. Error */
							free_pw_scheme( pwsp );
							return( -1 );
						}
							

						if ( ( enc = (pwsp->pws_enc)( (char*)slapi_value_get_string(vals[ i ]) )) == NULL ) 
						{
							free_pw_scheme( pwsp );
							return( -1 );
						}
						slapi_value_free(&vals[ i ]);
						vals[ i ] = slapi_value_new_string_passin(enc);
						free_pw_scheme( pwsp );
						return (0);
					}
				}
				free_pw_scheme( pwsp );
			}
		}
	}
	 
	return(-1);
}

/* ONREPL - below are the functions moved from pw_mgmt.c.
			this is done to allow the functions to be used
			by functions linked into libslapd.
 */

/* update_pw_info is called after password is modified successfully */
/* it should update passwordHistory, and passwordExpirationTime */
/* SLAPI_ENTRY_POST_OP must be set */

int
update_pw_info ( Slapi_PBlock *pb , char *old_pw)
{
	Slapi_Operation *operation = NULL;
	Slapi_DN *sdn = NULL;
	Slapi_Mods	smods;
	passwdPolicy *pwpolicy = NULL;
	time_t pw_exp_date;
	time_t cur_time;
	const char 	*target_dn, *bind_dn;
	char *timestr;
	int internal_op = 0;

	slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
	slapi_pblock_get( pb, SLAPI_TARGET_SDN, &sdn );
	slapi_pblock_get( pb, SLAPI_REQUESTOR_NDN, &bind_dn);
	internal_op = slapi_operation_is_flag_set(operation, SLAPI_OP_FLAG_INTERNAL);
	target_dn = slapi_sdn_get_ndn(sdn);
	pwpolicy = new_passwdPolicy(pb, target_dn);
	cur_time = current_time();
	slapi_mods_init(&smods, 0);
	
	/* update passwordHistory */
	if ( old_pw != NULL && pwpolicy->pw_history == 1 ) {
		update_pw_history(pb, sdn, old_pw);
		slapi_ch_free ( (void**)&old_pw );
	}

	/* Update the "pwdUpdateTime" attribute */
	if ( pwpolicy->pw_track_update_time ){
		timestr = format_genTime(cur_time);
		slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdUpdateTime",timestr);
		slapi_ch_free((void **)&timestr);
	}

	/* update password allow change time */
	if ( pwpolicy->pw_minage != 0) {
		timestr = format_genTime( time_plus_sec( cur_time, pwpolicy->pw_minage ));
		slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordAllowChangeTime", timestr);
		slapi_ch_free((void **)&timestr);
	}

	/*
	 * Fix for Bug 560707
	 * Removed the restriction that the lock variables (retry count) will
	 * be set only when root resets the password.
	 * Now admins will also have these privileges.
	 */
	if (pwpolicy->pw_lockout) {
		set_retry_cnt_mods (pb, &smods, 0 );
	}

	/* Clear the passwordgraceusertime from the user entry */
	slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordgraceusertime", "0");

	/*
	 * If the password is reset by a different user, mark it the first time logon.  If this is an internal
	 * operation, we have a special case for the password modify extended operation where
	 * we stuff the actual user who initiated the password change in pb_conn.  We check
	 * for this special case to ensure we reset the expiration date properly.
	 */
	if ((internal_op && pwpolicy->pw_must_change && (!pb->pb_conn || strcasecmp(target_dn, pb->pb_conn->c_dn))) ||
	    (!internal_op && pwpolicy->pw_must_change && (target_dn && bind_dn && strcasecmp(target_dn, bind_dn))))
	{
		pw_exp_date = NO_TIME;
	} else if ( pwpolicy->pw_exp == 1 ) {
		Slapi_Entry *pse = NULL;

		/* update password expiration date */
		pw_exp_date = time_plus_sec ( cur_time, pwpolicy->pw_maxage );
		slapi_pblock_get(pb,SLAPI_ENTRY_POST_OP,&pse);
		if (pse) {
		    char *prev_exp_date_str; 
		    /*
		     * if the password expiry time is SLAPD_END_TIME,
		     * don't roll it back 
		     */
		    prev_exp_date_str = slapi_entry_attr_get_charptr(pse,"passwordExpirationTime");
		    if (prev_exp_date_str) {
		        time_t prev_exp_date;

				prev_exp_date = parse_genTime(prev_exp_date_str);
				if (prev_exp_date == NO_TIME || prev_exp_date == NOT_FIRST_TIME) {
					/* ignore as will replace */
				} else if (prev_exp_date == SLAPD_END_TIME) {
					/* Special entries' passwords never expire */
					slapi_ch_free((void**)&prev_exp_date_str);
					pw_apply_mods(sdn, &smods);
					slapi_mods_done(&smods);
					return 0;
				}
				slapi_ch_free((void**)&prev_exp_date_str);
		    }
		} /* post op entry */
	} else if (pwpolicy->pw_must_change) {
		/*
		 * pw is not changed by root, and must change pw first time 
		 * log on 
		 */
		pw_exp_date = NOT_FIRST_TIME;
	} else {
		pw_apply_mods(sdn, &smods);
		slapi_mods_done(&smods);
		return 0;
	}

	timestr = format_genTime ( pw_exp_date );
	slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpirationTime", timestr);
	slapi_ch_free((void **)&timestr);
	
	slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpWarned", "0");

	pw_apply_mods(sdn, &smods);
	slapi_mods_done(&smods);
    if (pb->pb_conn) { /* no conn for internal op */
        /* reset c_needpw to 0 */
        pb->pb_conn->c_needpw = 0;
    }
    return 0;
}

int
check_pw_minage ( Slapi_PBlock *pb, const Slapi_DN *sdn, struct berval **vals)
{
	char *dn= (char*)slapi_sdn_get_ndn(sdn); /* jcm - Had to cast away const */
	passwdPolicy *pwpolicy=NULL;
	int pwresponse_req = 0;

	pwpolicy = new_passwdPolicy(pb, dn);
	slapi_pblock_get ( pb, SLAPI_PWPOLICY, &pwresponse_req );

	if ( !pb->pb_op->o_isroot && 
		pwpolicy->pw_minage != 0 ) {

		Slapi_Entry     *e;
		char *passwordAllowChangeTime;

		/* retrieve the entry */
		e = get_entry ( pb, dn );
		if ( e == NULL ) {
			return ( -1 );
		}
		/* get passwordAllowChangeTime attribute */
		passwordAllowChangeTime= slapi_entry_attr_get_charptr(e, "passwordAllowChangeTime");
		
		if (passwordAllowChangeTime!=NULL)
		{
			time_t pw_allowchange_date;
			char *cur_time_str = NULL;
			
			pw_allowchange_date = parse_genTime(passwordAllowChangeTime);
        	slapi_ch_free((void **) &passwordAllowChangeTime );

			/* check if allow to change the password */
			cur_time_str = format_genTime ( current_time() );
			if ( difftime ( pw_allowchange_date,
							parse_genTime ( cur_time_str )) > 0 )
			{
				if ( pwresponse_req == 1 ) {
					slapi_pwpolicy_make_response_control ( pb, -1, -1,
							LDAP_PWPOLICY_PWDTOOYOUNG );
				}
				pw_send_ldap_result ( pb,
                        LDAP_CONSTRAINT_VIOLATION, NULL,
                        "within password minimum age", 0, NULL );
				slapi_entry_free( e );
				slapi_ch_free((void **) &cur_time_str );
				return ( 1 );
			}
        	slapi_ch_free((void **) &cur_time_str );
		}
        slapi_entry_free( e );
	}
	return ( 0 );
}

/* check_pw_syntax is called before add or modify operation on userpassword attribute*/

int
check_pw_syntax ( Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals, 
			char **old_pw, Slapi_Entry *e, int mod_op)
{
	return ( check_pw_syntax_ext(pb, sdn, vals, old_pw, e, mod_op, NULL) );
}

int
check_pw_syntax_ext ( Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals,
			char **old_pw, Slapi_Entry *e, int mod_op, Slapi_Mods *smods)
{
   	Slapi_Attr		*attr;
	int 			i, pwresponse_req = 0;
	int				is_replication = 0;
	int				internal_op = 0;
	char			*dn= (char*)slapi_sdn_get_ndn(sdn); /* jcm - Had to cast away const */
	char			*pwd = NULL;
	char			*p = NULL;
	char			errormsg[ BUFSIZ ];
	passwdPolicy	*pwpolicy = NULL;
	Slapi_Operation *operation = NULL;

	/*
	 * check_pw_syntax_ext could be called with mod_op == LDAP_MOD_DELETE.
	 * In that case, no need to check the password syntax, but just returns 
	 * PASS == 0.
	 */
	if (LDAP_MOD_DELETE == (mod_op & LDAP_MOD_OP)) {
		return 0;
	}
	if (NULL == vals) {
		slapi_log_error( SLAPI_LOG_FATAL, NULL, 
		                 "check_pw_syntax_ext: no passwords to check\n" );
		return -1;
	}

	pwpolicy = new_passwdPolicy(pb, dn);
	slapi_pblock_get ( pb, SLAPI_PWPOLICY, &pwresponse_req );

	slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replication);
	slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
	internal_op = slapi_operation_is_flag_set(operation, SLAPI_OP_FLAG_INTERNAL);

	if ( pwpolicy->pw_syntax == 1 ) {
		for ( i = 0; vals[ i ] != NULL; ++i ) {
			int syntax_violation = 0;
			int num_digits = 0;
			int num_alphas = 0;
			int num_uppers = 0;
			int num_lowers = 0;
			int num_specials = 0;
			int num_8bit = 0;
			int num_repeated = 0;
			int max_repeated = 0;
			int num_categories = 0;

			/* Check if password is already hashed and reject if so.  We
			 * We need to allow the root DN and replicated ops to send
			 * pre-hashed passwords. We also check for a connection object
			 * when processing an internal operation to handle a special
			 * case for the password modify extended operation. */
			if (slapi_is_encoded((char *)slapi_value_get_string(vals[i]))) {
				if ((!is_replication && ((internal_op && pb->pb_conn && !slapi_dn_isroot(pb->pb_conn->c_dn)) ||
					(!internal_op && !pb->pb_requestor_isroot)))) {
					PR_snprintf( errormsg, BUFSIZ,
						"invalid password syntax - passwords with storage scheme are not allowed");
					if ( pwresponse_req == 1 ) {
						slapi_pwpolicy_make_response_control ( pb, -1, -1,
								LDAP_PWPOLICY_INVALIDPWDSYNTAX );
					}
					pw_send_ldap_result ( pb, LDAP_CONSTRAINT_VIOLATION, NULL, errormsg, 0, NULL );
					return( 1 );
				} else {
					/* We want to skip syntax checking since this is a pre-hashed
					 * password from replication or the root DN. */
					return( 0 );
				}
			}

			/* check for the minimum password length */
			if ( pwpolicy->pw_minlength >
				ldap_utf8characters((char *)slapi_value_get_string( vals[i] )) )
			{
				PR_snprintf( errormsg, BUFSIZ,
				    "invalid password syntax - password must be at least %d characters long",
				    pwpolicy->pw_minlength );
				if ( pwresponse_req == 1 ) {
					slapi_pwpolicy_make_response_control ( pb, -1, -1,
							LDAP_PWPOLICY_PWDTOOSHORT );
				}
				pw_send_ldap_result ( pb, LDAP_CONSTRAINT_VIOLATION, NULL, errormsg, 0, NULL );
				return ( 1 );
			}

			/* check character types */
			pwd = (char *)slapi_value_get_string( vals[i] );
			p = pwd;
			while ( p && *p )
			{
				if ( ldap_utf8isdigit( p ) ) {
					num_digits++;
				} else if ( ldap_utf8isalpha( p ) ) {
					num_alphas++;
					if ( slapi_utf8isLower( (unsigned char *)p ) ) {
						num_lowers++;
					} else {
						num_uppers++;
					}
				} else {
					/* check if this is an 8-bit char */
					if ( *p & 128 ) {
						num_8bit++;
					} else {
						num_specials++;
					}
				}

				/* check for repeating characters. If this is the
				   first char of the password, no need to check */
				if ( pwd != p ) {
					int len = ldap_utf8len( p );
					char *prev_p = ldap_utf8prev( p );

					if ( len == ldap_utf8len( prev_p ) )
					{
						if ( memcmp( p, prev_p, len ) == 0 )
                                        	{
							num_repeated++;
							if ( max_repeated < num_repeated ) {
								max_repeated = num_repeated;
							}
						} else {
							num_repeated = 0;
						}
					} else {
						num_repeated = 0;
					}
				}

				p = ldap_utf8next( p );
			}

			/* tally up the number of character categories */
			if ( num_digits > 0 )
				++num_categories;
			if ( num_uppers > 0 )
				++num_categories;
			if ( num_lowers > 0 )
				++num_categories;
			if ( num_specials > 0 )
				++num_categories;
			if ( num_8bit > 0 )
				++num_categories;

			/* check for character based syntax limits */
			if ( pwpolicy->pw_mindigits > num_digits ) {
				syntax_violation = 1;
				PR_snprintf ( errormsg, BUFSIZ,
                                    "invalid password syntax - password must contain at least %d digit characters",
                                    pwpolicy->pw_mindigits );
			} else if ( pwpolicy->pw_minalphas > num_alphas ) {
				syntax_violation = 1;
				PR_snprintf ( errormsg, BUFSIZ,
				    "invalid password syntax - password must contain at least %d alphabetic characters",
				    pwpolicy->pw_minalphas );
			} else if ( pwpolicy->pw_minuppers > num_uppers ) {
				syntax_violation = 1;
				PR_snprintf ( errormsg, BUFSIZ,
				    "invalid password syntax - password must contain at least %d uppercase characters",
				    pwpolicy->pw_minuppers );
			} else if ( pwpolicy->pw_minlowers > num_lowers ) {
				syntax_violation = 1;
				PR_snprintf ( errormsg, BUFSIZ,
				    "invalid password syntax - password must contain at least %d lowercase characters",
					pwpolicy->pw_minlowers );
			} else if ( pwpolicy->pw_minspecials > num_specials ) {
				syntax_violation = 1;
				PR_snprintf ( errormsg, BUFSIZ,
				    "invalid password syntax - password must contain at least %d special characters",
				    pwpolicy->pw_minspecials );
			} else if ( pwpolicy->pw_min8bit > num_8bit ) {
				syntax_violation = 1;
				PR_snprintf ( errormsg, BUFSIZ,
				    "invalid password syntax - password must contain at least %d 8-bit characters",
				    pwpolicy->pw_min8bit );
			} else if ( (pwpolicy->pw_maxrepeats != 0) && (pwpolicy->pw_maxrepeats < (max_repeated + 1)) ) {
				syntax_violation = 1;
				PR_snprintf ( errormsg, BUFSIZ,
				    "invalid password syntax - a character cannot be repeated more than %d times",
				    (pwpolicy->pw_maxrepeats + 1) );
			} else if ( pwpolicy->pw_mincategories > num_categories ) {
				syntax_violation = 1;
				PR_snprintf ( errormsg, BUFSIZ,
				    "invalid password syntax - password must contain at least %d character "
				    "categories (valid categories are digit, uppercase, lowercase, special, and 8-bit characters)",
				    pwpolicy->pw_mincategories );
			}

			/* If the password failed syntax checking, send the result and return */
			if (syntax_violation) {
				if ( pwresponse_req == 1 ) {
					slapi_pwpolicy_make_response_control ( pb, -1, -1,
					    LDAP_PWPOLICY_INVALIDPWDSYNTAX );
				}
				pw_send_ldap_result ( pb, LDAP_CONSTRAINT_VIOLATION, NULL, errormsg, 0, NULL );
				return ( 1 );
			}
		}
	}

	/* get the entry and check for the password history if this is called by a modify operation */
	if ( mod_op ) {
		/* retrieve the entry */
		e = get_entry ( pb, dn );
		if ( e == NULL ) {
			return ( -1 );
		}

		/* check for password history */
		if ( pwpolicy->pw_history == 1 ) {
			attr = attrlist_find(e->e_attrs, "passwordHistory");
			if (attr && 
				!valueset_isempty(&attr->a_present_values))
			{
				Slapi_Value **va= attr_get_present_values(attr);
				if ( pw_in_history( va, vals[0] ) == 0 ) {
					if ( pwresponse_req == 1 ) {
						slapi_pwpolicy_make_response_control ( pb, -1, -1,
							LDAP_PWPOLICY_PWDINHISTORY );
					}
					pw_send_ldap_result ( pb, 
						LDAP_CONSTRAINT_VIOLATION, NULL,
						"password in history", 0, NULL );
					slapi_entry_free( e ); 
					return ( 1 );
				}
			}

			/* get current password. check it and remember it  */
			attr = attrlist_find(e->e_attrs, "userpassword");
			if (attr && !valueset_isempty(&attr->a_present_values))
			{
				Slapi_Value **va= valueset_get_valuearray(&attr->a_present_values);
				if (slapi_is_encoded((char*)slapi_value_get_string(vals[0]))) 
				{
					if (slapi_attr_value_find(attr, (struct berval *)slapi_value_get_berval(vals[0])) == 0 )
					{
						pw_send_ldap_result ( pb, 
										   LDAP_CONSTRAINT_VIOLATION ,NULL,
										   "password in history", 0, NULL);
						slapi_entry_free( e ); 
						return ( 1 );
					}
				} else 
				{
					if ( slapi_pw_find_sv ( va, vals[0] ) == 0 )
					{
						pw_send_ldap_result ( pb, 
										   LDAP_CONSTRAINT_VIOLATION ,NULL,
										   "password in history", 0, NULL);
						slapi_entry_free( e ); 
						return ( 1 );
					}
				}
				/* We copy the 1st value of the userpassword attribute.
				 * This is because password policy assumes that there's only one 
				 *  password in the userpassword attribute.
				 */
				*old_pw = slapi_ch_strdup(slapi_value_get_string(va[0]));
			} else {
				*old_pw = NULL;
			}
		}
	}

	/* check for trivial words if syntax checking is enabled */
	if ( pwpolicy->pw_syntax == 1 ) {
		/* e is null if this is an add operation*/
		if ( check_trivial_words ( pb, e, vals, "uid", pwpolicy->pw_mintokenlength, smods ) == 1 ||
			check_trivial_words ( pb, e, vals, "cn", pwpolicy->pw_mintokenlength, smods ) == 1 ||
			check_trivial_words ( pb, e, vals, "sn", pwpolicy->pw_mintokenlength, smods ) == 1 ||
			check_trivial_words ( pb, e, vals, "givenname", pwpolicy->pw_mintokenlength, smods ) == 1 ||
			check_trivial_words ( pb, e, vals, "ou", pwpolicy->pw_mintokenlength, smods ) == 1 ||
			check_trivial_words ( pb, e, vals, "mail", pwpolicy->pw_mintokenlength, smods ) == 1)
		{
			if ( mod_op ) {
				slapi_entry_free( e );
			}

			return 1;
		}
	}

	if ( mod_op ) {
		/* free e only when called by modify operation */
		slapi_entry_free( e ); 
	}
	return 0; 	/* success */

}

static int
update_pw_history( Slapi_PBlock *pb, const Slapi_DN *sdn, char *old_pw )
{
	time_t 		t, old_t, cur_time;
	int 		i = 0, oldest = 0;
	int res;
	Slapi_Entry		*e;
	Slapi_Attr 	*attr;
	LDAPMod 	attribute;
	char 		*values_replace[25]; /* 2-24 passwords in history */
	LDAPMod 	*list_of_mods[2];
	Slapi_PBlock 	mod_pb;
	char		*history_str;
	char		*str;
	passwdPolicy *pwpolicy = NULL;
	const char *dn = slapi_sdn_get_dn(sdn);

	pwpolicy = new_passwdPolicy(pb, dn);

	/* retrieve the entry */
	e = get_entry ( pb, dn );
	if ( e == NULL ) {
		return ( 1 );
	}

	history_str = (char *)slapi_ch_malloc(GENERALIZED_TIME_LENGTH + strlen(old_pw) + 1);
	/* get password history, and find the oldest password in history */
	cur_time = current_time ();
	old_t = cur_time;
	str = format_genTime ( cur_time );
	attr = attrlist_find(e->e_attrs, "passwordHistory");
	if (attr && !valueset_isempty(&attr->a_present_values))
	{
		Slapi_Value **va= valueset_get_valuearray(&attr->a_present_values);
		for ( i = oldest = 0 ; 
			  (va[i] != NULL) && (slapi_value_get_length(va[i]) > 0) ;
			  i++ ) {

			values_replace[i] = (char*)slapi_value_get_string(va[i]);
			strncpy( history_str, values_replace[i], GENERALIZED_TIME_LENGTH);
			history_str[GENERALIZED_TIME_LENGTH] = '\0';
			if (history_str[GENERALIZED_TIME_LENGTH - 1] != 'Z'){
				/* The time is not a generalized Time. Probably a password history from 4.x */
				history_str[GENERALIZED_TIME_LENGTH - 1] = '\0';
			}
			t = parse_genTime ( history_str ); 
			if ( difftime ( t, old_t ) < 0 ) {
				oldest = i;
				old_t = t;
			}
		}
	}
	strcpy ( history_str, str );
	strcat ( history_str, old_pw );
	if ( i >= pwpolicy->pw_inhistory ) {
		/* replace the oldest password in history */
		values_replace[oldest] = history_str;
		values_replace[pwpolicy->pw_inhistory] = NULL;
	} else {
		/* add old_pw at the end of password history */
		values_replace[i] =  history_str;
		values_replace[++i]=NULL;
	}

	/* modify the attribute */
	attribute.mod_type = "passwordHistory";
	attribute.mod_op = LDAP_MOD_REPLACE;
	attribute.mod_values = values_replace;
	
	list_of_mods[0] = &attribute;
	list_of_mods[1] = NULL;

	pblock_init(&mod_pb);
	slapi_modify_internal_set_pb_ext(&mod_pb, sdn, list_of_mods, NULL, NULL, 
								 pw_get_componentID(), 0);
	slapi_modify_internal_pb(&mod_pb);
	slapi_pblock_get(&mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
	if (res != LDAP_SUCCESS){
		LDAPDebug2Args(LDAP_DEBUG_ANY,
		    "WARNING: passwordPolicy modify error %d on entry '%s'\n", res, dn);
	}

	pblock_done(&mod_pb);

	slapi_ch_free((void **) &str );
	slapi_ch_free((void **) &history_str );
	slapi_entry_free( e );
	return 0;
}

static
int pw_in_history( Slapi_Value **history_vals, const Slapi_Value *pw_val)
{
	Slapi_Value **trimmed_history = NULL;
	int num_history_vals = 0;
	int i;
	int	ret = -1;
	const char *pw_str = slapi_value_get_string(pw_val);
	
	if (slapi_is_encoded((char*)pw_str)){
		/* If the password is encoded, we just do a string match with all previous passwords */
		for ( i = 0; history_vals[i] != NULL; i++){
			const char * h_val = slapi_value_get_string(history_vals[i]);
			
			if ( h_val != NULL && 
				 slapi_value_get_length(history_vals[i]) >= 14 ) 
			{
				int pos = 14;
				if (h_val[pos] == 'Z')
					pos++;
				if (strcmp(&(h_val[pos]), pw_str) == 0){
					/* Password found */
					/* Let's just return */
					return (0);
				}
			}
		}
	} else { /* Password is in clear */
		/* Count the number of history vals. */
		for (i = 0; history_vals[i] != NULL; i++ )
		{
			num_history_vals++;
		}

		/* Allocate the array */
		trimmed_history = (Slapi_Value **)slapi_ch_calloc(num_history_vals + 1, sizeof(Slapi_Value *));

		/* strip the timestamps  */
		for ( i = 0; history_vals[i] != NULL; i++ )
		{
			char *h_val = (char *)slapi_value_get_string(history_vals[i]);
			size_t h_len = slapi_value_get_length(history_vals[i]);

			/* Allocate a value and put it in the array. */
			trimmed_history[i] = (Slapi_Value *)slapi_ch_calloc(1, sizeof(Slapi_Value));
			
			if ( h_val != NULL && 
				 h_len >= 14 )
			{
				/* LP: With the new genTime, the password history format has changed */
				int pos = 14;
				if (h_val[pos] == 'Z')
					pos++;
				trimmed_history[i]->bv.bv_val = &(h_val[pos]);
				trimmed_history[i]->bv.bv_len = h_len - pos;
			} else {
				trimmed_history[i]->bv.bv_val = NULL;
				trimmed_history[i]->bv.bv_len = 0;
			}
		}

		/* NULL terminate the array. */
		trimmed_history[i] = NULL;

		/* Check if the new password is in the trimmed history list. */
		ret = slapi_pw_find_sv(trimmed_history, pw_val);
	
		/* Free the trimmed values. */
		for ( i = 0; trimmed_history[i] != NULL; i++ )
		{
			slapi_ch_free((void **)&trimmed_history[i]);
		}

		/* Free the array. */
		slapi_ch_free((void **)&trimmed_history);
	}

	return ( ret );
}

int
slapi_add_pwd_control ( Slapi_PBlock *pb, char *arg, long time) {
	LDAPControl	new_ctrl;
	char		buf[12];
	
	LDAPDebug( LDAP_DEBUG_TRACE, "=> slapi_add_pwd_control\n", 0, 0, 0 );
	
	sprintf( buf, "%ld", time );
	new_ctrl.ldctl_oid = arg;
	new_ctrl.ldctl_value.bv_val = buf;
	new_ctrl.ldctl_value.bv_len = strlen( buf );
	new_ctrl.ldctl_iscritical = 0;         /* 0 = false. */
	
	if ( slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL, &new_ctrl ) != 0 ) {
		return( -1 );
	}

	return( 0 );
}

void
pw_mod_allowchange_aci(int pw_prohibit_change)
{
	const Slapi_DN *base;
	char		*values_mod[2];
	LDAPMod		mod;
	LDAPMod		*mods[2];
	Slapi_Backend *be;
	char *cookie = NULL;

	mods[0] = &mod;
	mods[1] = NULL;
	mod.mod_type = "aci";
	mod.mod_values = values_mod;

	if (pw_prohibit_change) {
		mod.mod_op = LDAP_MOD_ADD;
	}
	else
	{
		/* Allow change password by default  */
		/* remove the aci if it is there.  it is ok to fail */
		mod.mod_op = LDAP_MOD_DELETE;
	}

	be = slapi_get_first_backend (&cookie);
	/* Foreach backend... */
    while (be)
    {
		/* Don't add aci on a chaining backend holding remote entries */
        if((!be->be_private) && (!slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA)))
        {
			/* There's only One suffix per DB now. No need to loop */
			base = slapi_be_getsuffix(be, 0);
			if (base != NULL)
			{
				Slapi_PBlock pb;
				int rc;
				
				pblock_init (&pb);
				values_mod[0] = DENY_PW_CHANGE_ACI;
				values_mod[1] = NULL;
				slapi_modify_internal_set_pb_ext(&pb, base, mods, NULL, NULL,
				                                 pw_get_componentID(), 0);
				slapi_modify_internal_pb(&pb);
				slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
				if (rc == LDAP_SUCCESS){
					/* 
					** Since we modified the acl 
					** successfully, let's update the 
					** in-memory acl list
					*/
					slapi_pblock_set(&pb, SLAPI_TARGET_SDN, (void *)base);
					plugin_call_acl_mods_update (&pb, LDAP_REQ_MODIFY );
				}
				pblock_done(&pb);
			}
        }
		be = slapi_get_next_backend (cookie);
    }
	slapi_ch_free((void **) &cookie);
}

void
add_password_attrs( Slapi_PBlock *pb, Operation *op, Slapi_Entry *e )
{
	struct berval   bv;
	struct berval   *bvals[2];
	Slapi_Attr     **a, **next;
	passwdPolicy *pwpolicy = NULL;
	const char *dn = slapi_entry_get_ndn(e);
	int has_allowchangetime = 0, has_expirationtime = 0;
	time_t existing_exptime = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "add_password_attrs\n", 0, 0, 0 );

	bvals[0] = &bv;
	bvals[1] = NULL;
		
	/* If passwordexpirationtime is specified by the user, don't 
	   try to assign the initial value */
	for ( a = &e->e_attrs; *a != NULL; a = next ) {
		if ( strcasecmp( (*a)->a_type, 
			"passwordexpirationtime" ) == 0) {
			Slapi_Value *sval;
			if (slapi_attr_first_value(*a, &sval) == 0) {
				const struct berval *bv = slapi_value_get_berval(sval);
				existing_exptime = parse_genTime(bv->bv_val);
			}
			has_expirationtime = 1;
			
		} else if ( strcasecmp( (*a)->a_type,
			"passwordallowchangetime" ) == 0) {
			has_allowchangetime = 1;
		}
		next = &(*a)->a_next;
	}

	if ( has_allowchangetime && has_expirationtime ) {
		return;
	}

	pwpolicy = new_passwdPolicy(pb, dn);

	if ( !has_expirationtime && 
		( pwpolicy->pw_exp || pwpolicy->pw_must_change ) ) {
		if ( pwpolicy->pw_must_change) {
			/* must change password when first time logon */
			bv.bv_val = format_genTime ( NO_TIME );
		} else if ( pwpolicy->pw_exp ) {
			bv.bv_val = format_genTime ( time_plus_sec ( current_time (),
       	                 pwpolicy->pw_maxage ) );
		}
		bv.bv_len = strlen( bv.bv_val );
		slapi_entry_attr_merge( e, "passwordexpirationtime", bvals );
		slapi_ch_free_string( &bv.bv_val );
	}

	/* 
	 * If the password minimum age is not 0, calculate when the password 
	 * is allowed to be changed again and store the result 
	 * in passwordallowchangetime in the user's entry.
	 * If the password has expired, don't add passwordallowchangetime,
	 * otherwise if the user has grace logins, they can't be used to change
	 * the password if we set a passwordallowchangetime in the future.
	 */
	if ( !has_allowchangetime && pwpolicy->pw_minage != 0 && 
		(has_expirationtime && existing_exptime > current_time()) ) {
		bv.bv_val = format_genTime ( time_plus_sec ( current_time (),
                        pwpolicy->pw_minage ) );
		bv.bv_len = strlen( bv.bv_val );
	
		slapi_entry_attr_merge( e, "passwordallowchangetime", bvals );
		slapi_ch_free((void **) &bv.bv_val );
	}
}

static int
check_trivial_words (Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Value **vals, char *attrtype,
	int toklen, Slapi_Mods *smods )
{
	Slapi_Attr     *attr = NULL;
	Slapi_Mod      *smodp = NULL, *smod = NULL;
	Slapi_ValueSet *vs = NULL;
	Slapi_Value    *valp = NULL;
	struct berval  *bvp = NULL;
	int            i, pwresponse_req = 0;

	slapi_pblock_get ( pb, SLAPI_PWPOLICY, &pwresponse_req );

        /* Get a list of present values for attrtype in the existing entry, if there is one */
	if (e != NULL )
	{
		if ( (attr = attrlist_find(e->e_attrs, attrtype)) &&
			(!valueset_isempty(&attr->a_present_values)) )
		{
			/* allocate and add present values to valueset */
			slapi_attr_get_valueset( attr, &vs );
		}
	}

	/* allocate new one if not allocated above by
	   slapi_attr_get_valueset */
	if (!vs) {
		vs = slapi_valueset_new();
	}

	/* Get a list of new values for attrtype from the operation */
	if ( smods && (smod = slapi_mod_new()) )
	{
		for (smodp = slapi_mods_get_first_smod(smods, smod);
			smodp != NULL; smodp = slapi_mods_get_next_smod(smods, smod) )
		{
			/* Operation has new values for attrtype */
			if ( PL_strcasecmp(attrtype, slapi_mod_get_type(smodp)) == 0 )
			{
				/* iterate through smodp values and add them if they don't exist */
				for ( bvp = slapi_mod_get_first_value( smodp );  bvp != NULL;
					bvp = slapi_mod_get_next_value( smodp ) )
				{
					/* Add new value to valueset */
					valp = slapi_value_new_berval( bvp );
                                        slapi_valueset_add_value_ext( vs, valp, SLAPI_VALUE_FLAG_PASSIN );
					valp = NULL;
				}
			}
		}
		/* Free smod */
        	slapi_mod_free(&smod);
		smod = NULL;
		smodp = NULL;
	}

	/* If valueset isn't empty, we need to check if the password contains the values */
	if ( slapi_valueset_count(vs) != 0 )
	{
		for ( i = slapi_valueset_first_value( vs, &valp);
			(i != -1) && (valp != NULL);
			i = slapi_valueset_next_value( vs, i, &valp) )
		{
			/* If the value is smaller than the max token length,
			 * we don't need to check the password */
			if ( ldap_utf8characters(slapi_value_get_string( valp )) < toklen )
				continue;

			/* See if the password contains the value */
			if ( PL_strcasestr( slapi_value_get_string( vals[0] ),
				slapi_value_get_string( valp ) ) )
			{
				if ( pwresponse_req == 1 )
				{
					slapi_pwpolicy_make_response_control ( pb, -1, -1,
						LDAP_PWPOLICY_INVALIDPWDSYNTAX );
				}
				pw_send_ldap_result ( pb, 
					LDAP_CONSTRAINT_VIOLATION, NULL,
					"invalid password syntax - password based off of user entry", 0, NULL );

				/* Free valueset */
				slapi_valueset_free( vs );
				return ( 1 );
			}
		}
	}

	/* Free valueset */
	slapi_valueset_free( vs );
	return ( 0 );
}


void
pw_add_allowchange_aci(Slapi_Entry *e, int pw_prohibit_change) {
	char		*aci_pw = NULL;
	const char *aciattr = "aci";

	aci_pw = slapi_ch_strdup(DENY_PW_CHANGE_ACI);

	if (pw_prohibit_change) {
		/* Add ACI */
		slapi_entry_add_string(e, aciattr, aci_pw);
	} else {
		/* Remove ACI */
		slapi_entry_delete_string(e, aciattr, aci_pw);
	}
	slapi_ch_free((void **) &aci_pw);
}

/* This function creates a passwdPolicy structure, loads it from either
 * slapdFrontendconfig or the entry pointed by pwdpolicysubentry and
 * returns the structure.
 */
passwdPolicy *
new_passwdPolicy(Slapi_PBlock *pb, const char *dn)
{
	Slapi_ValueSet *values = NULL;
	Slapi_Entry *e = NULL, *pw_entry = NULL;
	int type_name_disposition = 0;
	char *actual_type_name = NULL;
	int attr_free_flags = 0;
	int rc=0;
	passwdPolicy *pwdpolicy = NULL;
	struct pw_scheme *pwdscheme = NULL;
	Slapi_Attr *attr;
	char *attr_name;
	Slapi_Value **sval;
	slapdFrontendConfig_t *slapdFrontendConfig;
	int optype = -1;

	/* If we already allocated a pw policy, return it */
	if(pb && pb->pwdpolicy){
		return pb->pwdpolicy;
	}

	slapdFrontendConfig = getFrontendConfig();
	pwdpolicy = (passwdPolicy *)slapi_ch_calloc(1, sizeof(passwdPolicy));

	if (pb) {
		slapi_pblock_get( pb, SLAPI_OPERATION_TYPE, &optype );
	}

	if (dn && (slapdFrontendConfig->pwpolicy_local == 1)) {
		/*  If we're doing an add, COS does not apply yet so we check
			parents for the pwdpolicysubentry.  We look only for virtual
			attributes, because real ones are for single-target policy. */
		/* RFE - is there a way to make this work for non-existent entries 
		 * when we don't pass in pb?  We'll need to do this if we add support 
		 * for password policy plug-ins. */
		if (optype == SLAPI_OPERATION_ADD) {
			char *parentdn = slapi_ch_strdup(dn);
			char *nextdn = NULL;
			while ((nextdn = slapi_dn_parent( parentdn )) != NULL) {
				if (((e = get_entry( pb, nextdn )) != NULL)) {
					if ((slapi_vattr_values_get(e, "pwdpolicysubentry",
							&values, &type_name_disposition, &actual_type_name, 
							SLAPI_VIRTUALATTRS_REQUEST_POINTERS |
							SLAPI_VIRTUALATTRS_ONLY,
							&attr_free_flags)) == 0) {
						/* pwdpolicysubentry found! */
						break;
					} else {
						/* Parent didn't have it, check grandparent... */
						slapi_ch_free_string( &parentdn );
						parentdn = nextdn;
						slapi_entry_free( e );
						e = NULL;
					}
				} else {
					/* Reached the top without finding a pwdpolicysubentry. */
					break;
				}
			}

			slapi_ch_free_string( &parentdn );
			slapi_ch_free_string( &nextdn );

		/*  If we're not doing an add, we look for the pwdpolicysubentry
			attribute in the target entry itself. */
		} else {
			if ( (e = get_entry( pb, dn )) != NULL ) {
				rc = slapi_vattr_values_get(e, "pwdpolicysubentry", &values,
					&type_name_disposition, &actual_type_name, 
					SLAPI_VIRTUALATTRS_REQUEST_POINTERS, &attr_free_flags);
				if (rc) {
					values = NULL;
				}
			}
		}

		if (values != NULL) {
			Slapi_Value *v = NULL;	
			const struct berval *bvp = NULL;

			if ( ((rc = slapi_valueset_first_value( values, &v )) != -1) &&
				( bvp = slapi_value_get_berval( v )) != NULL ) {
				if ( bvp != NULL ) {
					/* we got the pwdpolicysubentry value */
					pw_entry = get_entry ( pb, bvp->bv_val);
				}
			} 

			slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);

			slapi_entry_free( e );

			if ( pw_entry == NULL ) {
				LDAPDebug1Arg(LDAP_DEBUG_ANY,
				              "loading global password policy for %s"
				              "--local policy entry not found\n", dn);
				goto done;
			}

			/* set the default passwordLegacyPolicy setting */
			pwdpolicy->pw_is_legacy = 1;

			/* set passwordTrackUpdateTime */
			pwdpolicy->pw_track_update_time = slapdFrontendConfig->pw_policy.pw_track_update_time;

			for (slapi_entry_first_attr(pw_entry, &attr); attr;
					slapi_entry_next_attr(pw_entry, attr, &attr))
			{
				slapi_attr_get_type(attr, &attr_name);
				if (!strcasecmp(attr_name, "passwordminage")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_minage = slapi_value_get_timelong(*sval);
						if (-1 == pwdpolicy->pw_minage) {
							LDAPDebug2Args(LDAP_DEBUG_ANY, 
								"Password Policy Entry%s: Invalid passwordMinAge: %s\n",
								slapi_entry_get_dn_const(pw_entry),
								slapi_value_get_string(*sval));
						}
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordmaxage")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_maxage = slapi_value_get_timelong(*sval);
						if (-1 == pwdpolicy->pw_maxage) {
							LDAPDebug2Args(LDAP_DEBUG_ANY, 
								"Password Policy Entry%s: Invalid passwordMaxAge: %s\n",
								slapi_entry_get_dn_const(pw_entry),
								slapi_value_get_string(*sval));
						}
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordwarning")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_warning = slapi_value_get_timelong(*sval);
						if (-1 == pwdpolicy->pw_warning) {
							LDAPDebug2Args(LDAP_DEBUG_ANY, 
								"Password Policy Entry%s: Invalid passwordWarning: %s\n",
								slapi_entry_get_dn_const(pw_entry),
								slapi_value_get_string(*sval));
						}
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordhistory")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_history = 
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordinhistory")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_inhistory = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordlockout")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_lockout = 
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordmaxfailure")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_maxfailure = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordunlock")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_unlock = 
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordlockoutduration")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_lockduration = slapi_value_get_timelong(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordresetfailurecount")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_resetfailurecount = slapi_value_get_long(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordchange")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_change = 
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}       
				}
				else
				if (!strcasecmp(attr_name, "passwordmustchange")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_must_change = 
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordchecksyntax")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_syntax = 
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordminlength")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_minlength = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordmindigits")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_mindigits = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordminalphas")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_minalphas = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordminuppers")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_minuppers = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordminlowers")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_minlowers = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordminspecials")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_minspecials = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordmin8bit")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_min8bit = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordmaxrepeats")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_maxrepeats = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordmincategories")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_mincategories = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordmintokenlength")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_mintokenlength = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordexp")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_exp = 
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordgracelimit")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_gracelimit = slapi_value_get_int(*sval);
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordstoragescheme")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_storagescheme =
						pw_name2scheme((char*)slapi_value_get_string(*sval));
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordLegacyPolicy")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_is_legacy =
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}
				}
				else
				if (!strcasecmp(attr_name, "passwordTrackUpdateTime")) {
					if ((sval = attr_get_present_values(attr))) {
						pwdpolicy->pw_track_update_time =
						pw_boolean_str2value(slapi_value_get_string(*sval));
					}
				}
			} /* end of for() loop */
			if (pw_entry) {
				slapi_entry_free(pw_entry);
			}
			if(pb){
				pb->pwdpolicy = pwdpolicy;
			}
			return pwdpolicy;
		} else if ( e ) { 
			slapi_entry_free( e );
		}
	}

done:
	/*
	 * If we are here, that means we need to load the passwdPolicy
	 * structure from slapdFrontendconfig
	 */
	*pwdpolicy = slapdFrontendConfig->pw_policy;
	pwdscheme = (struct pw_scheme *)slapi_ch_calloc(1, sizeof(struct pw_scheme));
	*pwdscheme = *slapdFrontendConfig->pw_storagescheme;
	pwdscheme->pws_name = strdup( slapdFrontendConfig->pw_storagescheme->pws_name );
	pwdpolicy->pw_storagescheme = pwdscheme;
	if(pb){
		pb->pwdpolicy = pwdpolicy;
	}

	return pwdpolicy;

} /* End of new_passwdPolicy() */

void
delete_passwdPolicy( passwdPolicy **pwpolicy)
{
	if (pwpolicy && *pwpolicy) {
		free_pw_scheme( (*(*pwpolicy)).pw_storagescheme );
		slapi_ch_free((void **)pwpolicy);
	}
}

/*
 * Encode the PWPOLICY RESPONSE control.
 *
 * Create a password policy response control,
 * and add it to the PBlock to be returned to the client.
 *
 * Returns:
 *   success ( 0 )
 *   operationsError (1),
 */
int
slapi_pwpolicy_make_response_control (Slapi_PBlock *pb, int seconds, int logins, ber_int_t error)
{
	BerElement *ber= NULL;    
	struct berval *bvp = NULL;
	int rc = -1;

	/*
	PasswordPolicyResponseValue ::= SEQUENCE {
		warning   [0] CHOICE OPTIONAL {
			timeBeforeExpiration  [0] INTEGER (0 .. maxInt),
			graceLoginsRemaining  [1] INTEGER (0 .. maxInt) }
		error     [1] ENUMERATED OPTIONAL {
			passwordExpired       (0),
			accountLocked         (1),
			changeAfterReset      (2),
			passwordModNotAllowed (3),
			mustSupplyOldPassword (4),
			invalidPasswordSyntax (5),
			passwordTooShort      (6),
			passwordTooYoung      (7),
			passwordInHistory     (8) } }
	*/
	
	LDAPDebug( LDAP_DEBUG_TRACE, "=> slapi_pwpolicy_make_response_control", 0, 0, 0 );
	if ( ( ber = ber_alloc()) == NULL )
	{
		return rc;
	}

	rc = ber_printf( ber, "{" );
	if ( seconds >= 0 || logins >= 0 ) {
		if ( seconds >= 0 ) {
			rc = ber_printf( ber, "t{ti}", LDAP_TAG_PWP_WARNING,
							LDAP_TAG_PWP_SECSLEFT,
							seconds );
		} 
		else {
			rc = ber_printf( ber, "t{ti}", LDAP_TAG_PWP_WARNING,
							LDAP_TAG_PWP_GRCLOGINS,
							logins );
		}
	}
	if ( error >= 0 ) {
		rc = ber_printf( ber, "te", LDAP_TAG_PWP_ERROR, error );
	}
	rc = ber_printf( ber, "}" );

	if ( rc != -1 )
	{
		rc = ber_flatten( ber, &bvp );
	}
    
	ber_free( ber, 1 );

	if ( rc != -1 )
	{        
		LDAPControl	new_ctrl = {0};
		new_ctrl.ldctl_oid = LDAP_X_CONTROL_PWPOLICY_RESPONSE;
		new_ctrl.ldctl_value = *bvp;
		new_ctrl.ldctl_iscritical = 0;         
		rc= slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL, &new_ctrl );
		ber_bvfree(bvp);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= slapi_pwpolicy_make_response_control", 0, 0, 0 );

	return (rc==-1?LDAP_OPERATIONS_ERROR:LDAP_SUCCESS);
}

static int
pw_boolean_str2value (const char *str)
{
	if ( !strcasecmp(str, "true") ||
		 !strcasecmp(str, "on") ||
		 !strcasecmp(str, "1") ) {
		return ( LDAP_ON );
	}

	if ( !strcasecmp(str, "false") ||
		 !strcasecmp(str, "off") ||
		 !strcasecmp(str, "0") ) {
		return ( LDAP_OFF );
	}

	return (-1);
}

int
check_pw_duration_value( const char *attr_name, char *value, 
                       long minval, long maxval, char *errorbuf )
{
	int retVal = LDAP_SUCCESS;
	long age;

	age = parse_duration(value);
	if (-1 == age) {
		PR_snprintf ( errorbuf, BUFSIZ, 
		              "password minimum age \"%s\" is invalid. ", value );
		retVal = LDAP_CONSTRAINT_VIOLATION;
	} else if (0 == strcasecmp(CONFIG_PW_LOCKDURATION_ATTRIBUTE, attr_name)) {
		if ( (age <= 0) ||
			 (age > (MAX_ALLOWED_TIME_IN_SECS - current_time())) ||
			 ((-1 != minval) && (age < minval)) ||
			 ((-1 != maxval) && (age > maxval))) {
			PR_snprintf ( errorbuf, BUFSIZ, "%s: \"%s\" seconds is invalid. ",
			              attr_name, value );
			retVal = LDAP_CONSTRAINT_VIOLATION;
		}
	} else {
		if ( (age < 0) ||
			 (age > (MAX_ALLOWED_TIME_IN_SECS - current_time())) ||
			 ((-1 != minval) && (age < minval)) ||
			 ((-1 != maxval) && (age > maxval))) {
			PR_snprintf ( errorbuf, BUFSIZ, "%s: \"%s\" seconds is invalid. ",
			              attr_name, value );
			retVal = LDAP_CONSTRAINT_VIOLATION;
		}
	}

	return retVal;
}

int
check_pw_resetfailurecount_value( const char *attr_name, char *value, long minval, long maxval, char *errorbuf )
{
	int retVal = LDAP_SUCCESS;
	long duration = 0; /* in minutes */

	/* in seconds */  
	duration = strtol (value, NULL, 0);
	if ( duration < 0 || duration > (MAX_ALLOWED_TIME_IN_SECS - current_time()) ) {
		PR_snprintf ( errorbuf, BUFSIZ, 
			"password reset count duration \"%s\" seconds is invalid. ",
			value );
		retVal = LDAP_CONSTRAINT_VIOLATION;
	}

	return retVal;
}

int
check_pw_storagescheme_value( const char *attr_name, char *value, long minval, long maxval, char *errorbuf )
{
	int retVal = LDAP_SUCCESS;
	struct pw_scheme *new_scheme = NULL;
	char * scheme_list = NULL;

	scheme_list = plugin_get_pwd_storage_scheme_list(PLUGIN_LIST_PWD_STORAGE_SCHEME);
	new_scheme = pw_name2scheme(value);
	if ( new_scheme == NULL) {
		if ( scheme_list != NULL ) {
			PR_snprintf ( errorbuf, BUFSIZ,
					"%s: invalid scheme - %s. Valid schemes are: %s",
					CONFIG_PW_STORAGESCHEME_ATTRIBUTE, value, scheme_list );
		} else {
			PR_snprintf ( errorbuf, BUFSIZ,
					"%s: invalid scheme - %s (no pwdstorage scheme"
					" plugin loaded)",
					CONFIG_PW_STORAGESCHEME_ATTRIBUTE, value);
		}
	retVal = LDAP_CONSTRAINT_VIOLATION;
	}
	else if ( new_scheme->pws_enc == NULL )
	{
		/* For example: the NS-MTA-MD5 password scheme is for comparision only
		and for backward compatibility with an Old Messaging Server that was
		setting passwords in the directory already encrypted. The scheme cannot
		and won't encrypt passwords if they are in clear. We don't take it 
		*/ 

		if ( scheme_list != NULL ) {
			PR_snprintf ( errorbuf, BUFSIZ, 
				"%s: invalid encoding scheme - %s\nValid values are: %s\n",
				CONFIG_PW_STORAGESCHEME_ATTRIBUTE, value, scheme_list );
		}

		retVal = LDAP_CONSTRAINT_VIOLATION;
	}
    
	free_pw_scheme(new_scheme);
	slapi_ch_free_string(&scheme_list);

	return retVal;
}

/* check_account_lock is called before bind opeation; this could be a pre-op. */
int
slapi_check_account_lock ( Slapi_PBlock *pb, Slapi_Entry * bind_target_entry, int pwresponse_req, int check_password_policy, int send_result) {

	time_t		unlock_time;
	time_t		cur_time;
	char		*cur_time_str = NULL;
	char *accountUnlockTime;
	passwdPolicy *pwpolicy = NULL;
	char *dn = NULL;

	/* kexcoff: account inactivation */
	int rc = 0;
	Slapi_ValueSet *values = NULL;
	int type_name_disposition = 0;
	char *actual_type_name = NULL;
	int attr_free_flags = 0;
	/* kexcoff - end */

	if ( bind_target_entry == NULL ) 
		return -1;

	if(check_password_policy)
	{
		dn = slapi_entry_get_ndn(bind_target_entry);
		pwpolicy = new_passwdPolicy(pb, dn);
	}

	/* kexcoff: account inactivation */
	/* check if the entry is locked by nsAccountLock attribute - account inactivation feature */

	rc = slapi_vattr_values_get(bind_target_entry, "nsAccountLock", 
								&values, 
								&type_name_disposition, &actual_type_name,
								SLAPI_VIRTUALATTRS_REQUEST_POINTERS,
								&attr_free_flags);
	if ( rc == 0 && NULL != values )
	{
		Slapi_Value *v = NULL;	
		const struct berval *bvp = NULL;

		if ( (slapi_valueset_first_value( values, &v ) != -1) &&
				( bvp = slapi_value_get_berval( v )) != NULL )
		{
			if ( (bvp != NULL) && (strcasecmp(bvp->bv_val, "true") == 0) )
			{
				/* account inactivated */
				if (check_password_policy && pwresponse_req) {
					slapi_pwpolicy_make_response_control ( pb, -1, -1,
							LDAP_PWPOLICY_ACCTLOCKED );
				}
				if (send_result)
					send_ldap_result ( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
							"Account inactivated. Contact system administrator.",
							0, NULL );
				slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);
				goto locked;
			}
		} /* else, account "activated", keep on the process */
	}

	if ( values != NULL )
		slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);

	/* kexcoff - end */

	/*
	 * Check if the password policy has to be checked or not
	 */
	if ( !check_password_policy || pwpolicy->pw_lockout == 0 ) {
		goto notlocked;
	}

	/*
	 * Check the attribute of the password policy
	 */

	/* check if account is locked out.  If so, send result and return 1 */
	{
		unsigned int maxfailure= pwpolicy->pw_maxfailure;
		/* It's locked if passwordRetryCount >= maxfailure */
		if ( slapi_entry_attr_get_uint(bind_target_entry,"passwordRetryCount") < maxfailure )
		{
			/* Not locked */
			goto notlocked;	
		}
	}

	/* locked but maybe it's time to unlock it */
	accountUnlockTime= slapi_entry_attr_get_charptr(bind_target_entry, "accountUnlockTime");
	if (accountUnlockTime != NULL)
	{
		unlock_time = parse_genTime(accountUnlockTime);
		slapi_ch_free((void **) &accountUnlockTime );

		if ( pwpolicy->pw_unlock == 0 && 
			unlock_time == NO_TIME ) {

	        /* account is locked forever. contact admin to reset */
			if (pwresponse_req) {
				slapi_pwpolicy_make_response_control ( pb, -1, -1,
						LDAP_PWPOLICY_ACCTLOCKED );
			}
			if (send_result)
				send_ldap_result ( pb, LDAP_CONSTRAINT_VIOLATION, NULL,
						"Exceed password retry limit. Contact system administrator to reset.",
						0, NULL );
			goto locked;
		}
		cur_time = current_time();
		cur_time_str = format_genTime( cur_time);
		if ( difftime ( parse_genTime( cur_time_str ), unlock_time )  < 0 ) {

			/* account is locked, cannot do anything */	
			if (pwresponse_req) {
				slapi_pwpolicy_make_response_control ( pb, -1, -1,
						LDAP_PWPOLICY_ACCTLOCKED );
			}
			if (send_result)
				send_ldap_result ( pb, LDAP_CONSTRAINT_VIOLATION, NULL,
						"Exceed password retry limit. Please try later.",
						0, NULL );
			slapi_ch_free((void **) &cur_time_str );
			goto locked;
		} 
		slapi_ch_free((void **) &cur_time_str );
	}

notlocked:
	/* account is not locked. */
	return (0);
locked:
	return (1);
}

/* The idea here is that these functions could allow us to have password
 * policy plugins in the future.  The plugins would register callbacks for these
 * slapi functions that would be used here if any pwpolicy plugin is configured to
 * be used.  Right now, we just use the normal server password policy code since
 * we don't have a pwpolicy plugin type. */
Slapi_PWPolicy *
slapi_get_pwpolicy(Slapi_DN *dn)
{
    return ((Slapi_PWPolicy *)new_passwdPolicy(NULL, slapi_sdn_get_ndn(dn)));
}

void
slapi_pwpolicy_free(Slapi_PWPolicy *pwpolicy)
{
    delete_passwdPolicy((passwdPolicy **)&pwpolicy);
}

int
slapi_pwpolicy_is_expired(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e, time_t *expire_time, int *remaining_grace)
{
    int is_expired = 0;

    if (pwpolicy && e) {
        /* If password expiration is enabled in the policy,
         * check if the password has expired. */
        if (pwpolicy->pw_exp == 1) {
            char *expiration_val = NULL;
            time_t _expire_time;
            double diff_t = 0;
            char *cur_time_str = NULL;
            time_t cur_time;

            expiration_val = slapi_entry_attr_get_charptr(e, "passwordExpirationTime");
            if (expiration_val) {
                _expire_time = parse_genTime(expiration_val);

                cur_time = current_time();
                cur_time_str = format_genTime(cur_time);

                if ((_expire_time != NO_TIME) && (_expire_time != NOT_FIRST_TIME) &&
                    ((diff_t = difftime (_expire_time, parse_genTime(cur_time_str))) <= 0)) {
                    is_expired = 1;
                }

                if (is_expired) {
                    if (remaining_grace) {
                        /* Fill in the number of remaining grace logins */
                        int grace_attempts = 0;

                        grace_attempts = slapi_entry_attr_get_int(e, "passwordGraceUserTime");
                        if (pwpolicy->pw_gracelimit > grace_attempts) {
                            *remaining_grace = pwpolicy->pw_gracelimit - grace_attempts;
                        } else {
                            *remaining_grace = 0;
                        }
                    }
                } else if (expire_time) {
                    /* Fill in the expiration time */
                    if ((_expire_time != NO_TIME) && (_expire_time != NOT_FIRST_TIME)) {
                        *expire_time = _expire_time;
                    } else {
                        *expire_time = (time_t)0;
                    }
                }

                slapi_ch_free_string(&cur_time_str);
            }
        } else if (expire_time) {
            /* Passwords never expire */
            *expire_time = (time_t)0;
        }
    }

    return is_expired;
}

int
slapi_pwpolicy_is_locked(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e, time_t *unlock_time)
{
    int is_locked = 0;

    if (pwpolicy && e) {
        /* Check if account is locked */
        if ( pwpolicy->pw_lockout == 1) {
            if (slapi_entry_attr_get_uint(e, "passwordRetryCount") >= pwpolicy->pw_maxfailure) {
                is_locked = 1;
            }
        }

        if (is_locked) {
            /* See if it's time for the account to be unlocked */
            char *unlock_time_str = NULL;
            char *cur_time_str = NULL;
            time_t _unlock_time = (time_t)0;
            time_t cur_time;

            unlock_time_str = slapi_entry_attr_get_charptr(e, "accountUnlockTime");
            if (unlock_time_str) {
                _unlock_time = parse_genTime(unlock_time_str);
            }

            if ((pwpolicy->pw_unlock == 0) && (_unlock_time == NO_TIME)) {
                /* Account is locked forever */
                if (unlock_time) {
                    *unlock_time = (time_t)0;
                }
            } else {
                cur_time = current_time();
                cur_time_str = format_genTime(cur_time);

                if (difftime(parse_genTime(cur_time_str), _unlock_time)  < 0) {
                    /* Account is not due to be unlocked yet.
                     * Fill in the unlock time. */
                    if (unlock_time) {
                        *unlock_time = _unlock_time;
                    }
                } else {
                    /* Account is due to be unlocked */
                    is_locked = 0;
                }

                slapi_ch_free_string(&cur_time_str);
            }
        }
    }

    return is_locked;
}

int
slapi_pwpolicy_is_reset(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e)
{
    int is_reset = 0;

    if (pwpolicy && e) {
        /* Check if password was reset and needs to be changed */
        if (pwpolicy->pw_must_change) {
            char *expiration_val = 0;
            time_t expire_time = (time_t)0;

            expiration_val = slapi_entry_attr_get_charptr(e, "passwordExpirationTime");
            if (expiration_val) {
                expire_time = parse_genTime(expiration_val);
                if (expire_time == NO_TIME) {
                    is_reset = 1;
                }
            }
        }
    }

    return is_reset;
}

/*
 * Entry extension for unhashed password
 */
static int pw_entry_objtype = -1;
static int pw_entry_handle = -1;

struct slapi_pw_entry_ext {
	Slapi_RWLock *pw_entry_lock;   /* necessary? */
	Slapi_Value **pw_entry_values; /* stashed values */
};

/*
 * constructor for the entry object extension.
 */
static void *
pw_entry_constructor(void *object, void *parent)
{
	struct slapi_pw_entry_ext *pw_extp = NULL;
	Slapi_RWLock *rwlock;
	if ((rwlock = slapi_new_rwlock()) == NULL) {
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
		                "pw_entry_constructor: slapi_new_rwlock() failed\n");
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
		                "WARNING: the server cannot handle unhashed password.\n");
		return NULL;
	}
	pw_extp = (struct slapi_pw_entry_ext *)slapi_ch_calloc(1,
                                             sizeof(struct slapi_pw_entry_ext));
	pw_extp->pw_entry_lock = rwlock;
	return pw_extp;
}

/*
 * destructor for the entry object extension.
 */
static void
pw_entry_destructor(void *extension, void *object, void *parent)
{
	struct slapi_pw_entry_ext *pw_extp = (struct slapi_pw_entry_ext *)extension;

	if (NULL == pw_extp) {
		return;
	}
	
	valuearray_free(&pw_extp->pw_entry_values);

	if (pw_extp->pw_entry_lock) {
		slapi_destroy_rwlock(pw_extp->pw_entry_lock);
	}
	slapi_ch_free((void **)&pw_extp);
}

/* Called once from main */
void
pw_exp_init ( void )
{
	if (slapi_register_object_extension(SLAPI_EXTMOD_PWPOLICY,
	                                    SLAPI_EXT_ENTRY,
	                                    pw_entry_constructor,
	                                    pw_entry_destructor,
	                                    &pw_entry_objtype,
	                                    &pw_entry_handle) != 0) {
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
		                "pw_init: slapi_register_object_extension failed; "
		                "unhashed password is not able to access\n");
	}
}

/* 
 * The output value vals is not a copy.  
 * Caller must duplicate it to use it for other than referring.
 */
int
slapi_pw_get_entry_ext(Slapi_Entry *entry, Slapi_Value ***vals)
{
	struct slapi_pw_entry_ext *extp = NULL;

	if (NULL == vals) {
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
		                "slapi_pw_get_entry_ext: output param vals is NULL.\n");
		return LDAP_PARAM_ERROR;
	}
	*vals = NULL;

	if ((-1 == pw_entry_objtype) || (-1 == pw_entry_handle)) {
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "slapi_pw_get_entry_ext: pw_entry_extension is not "
		                "registered\n");
		return LDAP_OPERATIONS_ERROR;
	}

	extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
	                                                           pw_entry_objtype,
	                                                           entry,
	                                                           pw_entry_handle);
	if ((NULL == extp) || (NULL == extp->pw_entry_values)) {
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "slapi_pw_get_entry_ext: "
		                "pw_entry_extension is not set\n");
		return LDAP_NO_SUCH_ATTRIBUTE;
	}

	slapi_rwlock_rdlock(extp->pw_entry_lock);
	*vals = extp->pw_entry_values;
	slapi_rwlock_unlock(extp->pw_entry_lock);
	return LDAP_SUCCESS;
}

/* If vals is NULL, the stored extension is freed.  */
/* If slapi_pw_set_entry_ext is successful, vals are consumed. */
int
slapi_pw_set_entry_ext(Slapi_Entry *entry, Slapi_Value **vals, int flags)
{
	struct slapi_pw_entry_ext *extp = NULL;

	if ((-1 == pw_entry_objtype) || (-1 == pw_entry_handle)) {
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "slapi_pw_set_entry_ext: "
		                "pw_entry_extension is not registered\n");
		return LDAP_OPERATIONS_ERROR;
	}

	extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
	                                                           pw_entry_objtype,
	                                                           entry,
	                                                           pw_entry_handle);
	if (NULL == extp) {
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "slapi_pw_set_entry_ext: "
		                "pw_entry_extension is not set\n");
		return LDAP_NO_SUCH_ATTRIBUTE;
	}

	slapi_rwlock_wrlock(extp->pw_entry_lock);
	if (NULL == vals) { /* Set NULL; used for delete. */
		valuearray_free(&extp->pw_entry_values); /* Null is taken care */
	} else {
		if (SLAPI_EXT_SET_REPLACE == flags) {
			valuearray_free(&extp->pw_entry_values); /* Null is taken care */
		}
		/* Each (Slapi_Value *) in vals is passed in. */
		valuearray_add_valuearray(&extp->pw_entry_values, vals,
		                          SLAPI_VALUE_FLAG_PASSIN);
		/* To keep the word "consumed", free vals part, as well. */
		slapi_ch_free((void **)&vals);
	}
	slapi_rwlock_unlock(extp->pw_entry_lock);
	return LDAP_SUCCESS;
}

int
pw_copy_entry_ext(Slapi_Entry *src_e, Slapi_Entry *dest_e)
{
	struct slapi_pw_entry_ext *src_extp = NULL;
	struct slapi_pw_entry_ext *dest_extp = NULL;

	if ((-1 == pw_entry_objtype) || (-1 == pw_entry_handle)) {
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "pw_copy_entry_ext: "
		                "pw_entry_extension is not registered\n");
		return LDAP_OPERATIONS_ERROR;
	}

	src_extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
	                                                           pw_entry_objtype,
	                                                           src_e,
	                                                           pw_entry_handle);
	if (NULL == src_extp) {
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "pw_copy_entry_ext: source pw_entry_extension is "
		                "not set\n");
		return LDAP_NO_SUCH_ATTRIBUTE;
	}

	slapi_rwlock_rdlock(src_extp->pw_entry_lock);
	dest_extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
	                                                           pw_entry_objtype,
	                                                           dest_e,
	                                                           pw_entry_handle);
	if (NULL == dest_extp) {
		slapi_rwlock_unlock(src_extp->pw_entry_lock);
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "pw_copy_entry_ext: "
		                "dest pw_entry_extension is not set\n");
		return LDAP_NO_SUCH_ATTRIBUTE;
	}

	slapi_rwlock_wrlock(dest_extp->pw_entry_lock);
	valuearray_add_valuearray(&dest_extp->pw_entry_values,
	                          src_extp->pw_entry_values, 0);
	slapi_rwlock_unlock(dest_extp->pw_entry_lock);
	slapi_rwlock_unlock(src_extp->pw_entry_lock);
	return LDAP_SUCCESS;
}

/* 
 * The returned string is a copy.  
 * Caller must free it.
 */
char *
slapi_get_first_clear_text_pw(Slapi_Entry *entry)
{
	struct slapi_pw_entry_ext *extp = NULL;
	Slapi_Value **pwvals = NULL;
	const char *password_str = NULL;

	if ((-1 == pw_entry_objtype) || (-1 == pw_entry_handle)) {
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "slapi_get_first_clear_text_pw: "
		                "pw_entry_extension is not registered\n");
		return NULL;
	}

	extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
	                                                           pw_entry_objtype,
	                                                           entry,
	                                                           pw_entry_handle);
	if ((NULL == extp) || (NULL == extp->pw_entry_values)) {
		slapi_log_error(SLAPI_LOG_TRACE, NULL,
		                "slapi_get_first_clear_text_pw: "
		                "pw_entry_extension is not set\n");
		return NULL;
	}

	slapi_rwlock_rdlock(extp->pw_entry_lock);
	pwvals = extp->pw_entry_values;
	if (pwvals) {
		Slapi_ValueSet vset;
		Slapi_Value *value = NULL;
		/* pwvals is passed in to vset; thus no need to free vset. */
		valueset_set_valuearray_passin(&vset, pwvals);
		slapi_valueset_first_value(&vset, &value);
		password_str = slapi_value_get_string(value);
	}
	slapi_rwlock_unlock(extp->pw_entry_lock);
	return slapi_ch_strdup(password_str); /* slapi_ch_strdup(NULL) is okay */
}
