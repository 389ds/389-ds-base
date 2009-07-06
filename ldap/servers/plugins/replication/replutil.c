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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

 
 /*
 * replutil.c - various utility functions common to all replication methods.
 */

#include <nspr.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#ifndef _WIN32
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#ifdef OS_solaris
#include <dlfcn.h>	/* needed for dlopen and dlsym */
#endif /* solaris: dlopen */
#include <time.h>
#ifdef LINUX
#include <errno.h>	/* weird use of errno */
#endif

#include "slapi-plugin.h"
#include "repl5.h"
#include "repl.h"

typedef int (*open_fn)(const char *path, int flags, ...);

/* this is set during replication plugin initialization from the plugin entry */
static char *replpluginpath = NULL;
static PRBool is_chain_on_update_setup(const Slapi_DN *replroot);

/*
 * All standard changeLogEntry attributes (initialized in get_cleattrs)
 */
static char *cleattrs[ 10 ] = { NULL, NULL, NULL, NULL, NULL, NULL,
			        NULL, NULL, NULL };

/*
 * Function: get_cleattrs
 *
 * Returns: an array of pointers to attribute names.
 *
 * Arguments: None.
 *
 * Description: Initializes, if necessary, and returns an array of char *s
 *              with attribute names used for retrieving changeLogEntry
 *              entries from the directory.
 */
char **
get_cleattrs()
{
    if ( cleattrs[ 0 ] == NULL ) {
        cleattrs[ 0 ] = type_objectclass;
        cleattrs[ 1 ] = attr_changenumber;
        cleattrs[ 2 ] = attr_targetdn;
        cleattrs[ 3 ] = attr_changetype;
	cleattrs[ 4 ] = attr_newrdn;
	cleattrs[ 5 ] = attr_deleteoldrdn;
	cleattrs[ 6 ] = attr_changes;
	cleattrs[ 7 ] = attr_newsuperior;
	cleattrs[ 8 ] = attr_changetime;
	cleattrs[ 9 ] = NULL;
    }
    return cleattrs;
}

/*
 *  Function: add_bval2mods 
 *
 *  Description: same as add_val2mods, but sticks in a bval instead. 
 *               val can be null.
 */
void
add_bval2mods(LDAPMod **mod, char *type, char *val, int mod_op)
{
  *mod = (LDAPMod *) slapi_ch_calloc(1, sizeof (LDAPMod));
  memset (*mod, 0, sizeof(LDAPMod));
  (*mod)->mod_op = mod_op | LDAP_MOD_BVALUES;
  (*mod)->mod_type = slapi_ch_strdup(type);
  
  if (val != NULL){
    (*mod)->mod_bvalues = (struct berval **) slapi_ch_calloc(2, sizeof(struct berval *));
    (*mod)->mod_bvalues[0] = (struct berval *) slapi_ch_malloc (sizeof(struct berval));
    (*mod)->mod_bvalues[1] = NULL;
    (*mod)->mod_bvalues[0]->bv_len = strlen(val);
    (*mod)->mod_bvalues[0]->bv_val = slapi_ch_strdup(val);
  } else {
    (*mod)->mod_bvalues = NULL;
  }
}


char*
copy_berval (struct berval* from)
{
    char* s = slapi_ch_malloc (from->bv_len + 1);
    memcpy (s, from->bv_val, from->bv_len);
    s [from->bv_len] = '\0';
    return s;
}


/*
 * Function: entry_print
 * Arguments: e - entry to print
 * Returns: nothing
 * Description: Prints the contents of an Slapi_Entry struct. Used for debugging.
 */
void
entry_print( Slapi_Entry *e )
{
    int sz;
    char *p;

    printf( "Slapi_Entry dump:\n" );

    if ( e == NULL ) {
	printf( "Slapi_Entry is NULL\n" );
	return;
    }

    if (( p = slapi_entry2str( e, &sz )) == NULL ) {
	printf( "slapi_entry2str returned NULL\n" );
	return;
    }
    puts( p );
    fflush( stdout );
    slapi_ch_free_string( &p );
    return;
}

/* NSPR supports large file, but, according to dboreham, it does not work.
   The backed has its own functions to deal with large files. I thought
   about making them slapi function, but I don't think it makes sense because
   server should only export function which have to do with its operation
   and copying files is not one of them. So, instead, I made a copy of it in the
   replication module. I will switch it to NSPR once that stuff works.
*/

int copyfile(char* source, char * destination, int overwrite, int mode) 
{
#if defined _WIN32
	return (0 == CopyFile(source,destination,overwrite ? FALSE : TRUE));
#else
#ifdef DB_USE_64LFS
#define OPEN_FUNCTION dblayer_open_large
#else
#define OPEN_FUNCTION open	
#endif
	int source_fd = -1;
	int dest_fd = -1;
	char *buffer = NULL;
	int return_value = -1;
	int bytes_to_write = 0;

	/* allocate the buffer */
	buffer =  slapi_ch_malloc(64*1024);
	if (NULL == buffer)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "copy file: memory allocation failed\n");
		goto error;
	}
	/* Open source file */
	source_fd = OPEN_FUNCTION(source,O_RDONLY,0);
	if (-1 == source_fd)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"copyfile: failed to open source file %s\n", source);
		goto error;
	}
	/* Open destination file */
	dest_fd = OPEN_FUNCTION(destination,O_CREAT | O_WRONLY, mode);
	if (-1 == dest_fd)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"copyfile: failed to open destination file %s\n", destination);
		goto error;
	}
	/* Loop round reading data and writing it */
	while (1)
	{
		return_value = read(source_fd,buffer,64*1024);
		if (return_value <= 0)
		{
			/* means error or EOF */
			break;
		}
		bytes_to_write = return_value;
		return_value = write(dest_fd,buffer,bytes_to_write);
		if (return_value != bytes_to_write)
		{
			/* means error */
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"copyfile: failed to write to destination file %s\n", destination);
			return_value = -1;
			break;
		}
	}
error:
	if (source_fd != -1)
	{
		close(source_fd);
	}
	if (dest_fd != -1)
	{
		close(dest_fd);
	}
	slapi_ch_free_string(&buffer);
	return return_value;
#endif
}

/* convert time from string like 1h (1 hour) to corresponding time in seconds */
time_t
age_str2time (const char *age)
{
	char *maxage;
	char unit;
	time_t ageval;

	if (age == NULL || age[0] == '\0' || strcmp (age, "0") == 0)
	{
		return 0; 
	}

	maxage = slapi_ch_strdup ( age );
	unit = maxage[ strlen( maxage ) - 1 ];
	maxage[ strlen( maxage ) - 1 ] = '\0';
	ageval = strntoul( maxage, strlen( maxage ), 10 );
	slapi_ch_free_string(&maxage);
	switch ( unit ) 
	{
		case 's':
			break;
		case 'm':
			ageval *= 60;
			break;
		case 'h':
			ageval *= ( 60 * 60 );
			break;
		case 'd':
			ageval *= ( 24 * 60 * 60 );
			break;
		case 'w':
			ageval *= ( 7 * 24 * 60 * 60 );
			break;
		default:
			slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, 
							"age_str2time: unknown unit \"%c\" "
							"for maxiumum changelog age\n", unit );
			ageval = -1;
	}

	return ageval;
}

const char*
changeType2Str (int type)
{
	switch (type)
	{
		case T_ADDCT:		return T_ADDCTSTR;
		case T_MODIFYCT:	return T_MODIFYCTSTR;
		case T_MODRDNCT:	return T_MODRDNCTSTR;
		case T_DELETECT:	return T_DELETECTSTR;
		default:			return NULL; 
	}
}

int
str2ChangeType (const char *str)
{
	if (strcasecmp (str, T_ADDCTSTR) == 0)
		return T_ADDCT;

	if (strcasecmp (str, T_MODIFYCTSTR) == 0)
		return T_MODIFYCT;

	if (strcasecmp (str, T_MODRDNCTSTR) == 0)
		return T_MODRDNCT;

	if (strcasecmp (str, T_DELETECTSTR) == 0)
		return T_DELETECT;

	return -1;
}

lenstr *
make_changes_string(LDAPMod **ldm, char **includeattrs)
{
    lenstr		*l;
    int			i, j, len;
    int			skip;

    /* loop through the LDAPMod struct and construct the changes attribute */
    l = lenstr_new();

    for ( i = 0; ldm[ i ] != NULL; i++ ) {
	/* If a list of explicit attributes was given, only add those */
	if ( NULL != includeattrs ) {
	    skip = 1;
	    for ( j = 0; includeattrs[ j ] != NULL; j++ ) {
		if ( strcasecmp( includeattrs[ j ], ldm[ i ]->mod_type ) == 0 ) {
		    skip = 0;
		    break;
		}
	    }
	    if ( skip ) {
		continue;
	    }
	}
	switch ( ldm[ i ]->mod_op  & ~LDAP_MOD_BVALUES ) {
	case LDAP_MOD_ADD:
	    addlenstr( l, "add: " );
	    addlenstr( l, ldm[ i ]->mod_type );
	    addlenstr( l, "\n" );
	    break;
	case LDAP_MOD_DELETE:
	    addlenstr( l, "delete: " );
	    addlenstr( l, ldm[ i ]->mod_type );
	    addlenstr( l, "\n" );
	    break;
	case LDAP_MOD_REPLACE:
	    addlenstr( l, "replace: " );
	    addlenstr( l, ldm[ i ]->mod_type );
	    addlenstr( l, "\n" );
	    break;
	}
	for ( j = 0; ldm[ i ]->mod_bvalues != NULL &&
		ldm[ i ]->mod_bvalues[ j ] != NULL; j++ ) {
	    char *buf = NULL;
	    char *bufp = NULL;

	    len = strlen( ldm[ i ]->mod_type );
	    len = LDIF_SIZE_NEEDED( len,
		    ldm[ i ]->mod_bvalues[ j ]->bv_len ) + 1;
	    buf = slapi_ch_malloc( len );
	    bufp = buf;
	    slapi_ldif_put_type_and_value_with_options( &bufp, ldm[ i ]->mod_type,
		    ldm[ i ]->mod_bvalues[ j ]->bv_val,
		    ldm[ i ]->mod_bvalues[ j ]->bv_len, 0 );
	    *bufp = '\0';

	    addlenstr( l, buf );

	    slapi_ch_free_string( &buf );
	}
	addlenstr( l, "-\n" );
    }
    return l;
}

/* note that the string get modified by ldif_parse*** functions */
Slapi_Mods *
parse_changes_string(char *str)
{
	int rc;
	Slapi_Mods *mods;
	Slapi_Mod  mod;
	char *line, *next;
	char *type, *value;
	int vlen;
	struct berval bv;
	
	/* allocate mods array */
	mods = slapi_mods_new ();
	if (mods == NULL)
		return NULL;

	slapi_mods_init (mods, 16); /* JCMREPL - ONREPL : 16 bigger than needed? */
	
	/* parse mods */
	next = str;
	line = ldif_getline (&next);
	while (line)
	{
		slapi_mod_init (&mod, 0);		
		while (line)
		{		
			if (strcasecmp (line, "-") == 0)
			{
				if (slapi_mod_isvalid (&mod))
				{
					slapi_mods_add_smod (mods, &mod);
					/* JCMREPL - ONREPL - slapi_mod_done(&mod) ??? */
				}
				else
				{
					/* ONREPL - need to cleanup */
				}

				line = ldif_getline (&next);
				break;
			}

			rc = ldif_parse_line(line, &type, &value, &vlen);
			if (rc != 0)
			{
				/* ONREPL - log warning */
				slapi_log_error( SLAPI_LOG_REPL, repl_plugin_name, 
						 "Failed to parse the ldif line.\n");
				continue;
			}

			if (strcasecmp (type, "add") == 0)
			{
				slapi_mod_set_operation (&mod, LDAP_MOD_ADD | LDAP_MOD_BVALUES);
			}
			else if (strcasecmp (type, "delete") == 0)
			{
				slapi_mod_set_operation (&mod, LDAP_MOD_DELETE | LDAP_MOD_BVALUES);
			}
			else if (strcasecmp (type, "replace") == 0)
			{
				slapi_mod_set_operation (&mod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
			}
			else /* attr: value pair */
			{
				/* adding first value */
				if (slapi_mod_get_type (&mod) == NULL)
				{
					slapi_mod_set_type (&mod, type);
				}

				bv.bv_val = value;
				bv.bv_len = vlen;

				slapi_mod_add_value (&mod, &bv);
			}
	
			line = ldif_getline (&next);
		}
	}

	return mods;	
}

static void* g_plg_identity [PLUGIN_MAX];

void*
repl_get_plugin_identity (int pluginID)
{
	PR_ASSERT (pluginID < PLUGIN_MAX);
	return g_plg_identity [pluginID];
}

void
repl_set_plugin_identity (int pluginID, void *identity)
{
	PR_ASSERT (pluginID < PLUGIN_MAX);
	g_plg_identity [pluginID] = identity;	
}

/* this function validates operation parameters */
PRBool
IsValidOperation (const slapi_operation_parameters *op)
{
	if (op == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"IsValidOperation: NULL operation\n");
        return PR_FALSE;
    }

    if (op->csn == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"IsValidOperation: NULL operation CSN\n");
        return PR_FALSE;
    }

    if (op->target_address.uniqueid == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"IsValidOperation: NULL entry uniqueid\n");
        return PR_FALSE;
    }

    if (op->target_address.dn == NULL)
    {
        slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"IsValidOperation: NULL entry DN\n");
        return PR_FALSE;
    }

	switch (op->operation_type)
	{
		case SLAPI_OPERATION_ADD:		if (op->p.p_add.target_entry == NULL)
                                        {
                                            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						                    "IsValidOperation: NULL entry for add operation\n");
											return PR_FALSE;
                                        }
										else 
											break;

		case SLAPI_OPERATION_MODIFY:	if (op->p.p_modify.modify_mods == NULL || 
										    op->p.p_modify.modify_mods[0] == NULL)
                                        {
                                            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						                    "IsValidOperation: NULL mods for modify operation\n");
											return PR_FALSE;
                                        }
										else 
											break;

		case SLAPI_OPERATION_MODRDN:	if (op->p.p_modrdn.modrdn_mods == NULL || 
											op->p.p_modrdn.modrdn_mods[0] == NULL)
										{
                                            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						                    "IsValidOperation: NULL mods for modrdn operation\n");
											return PR_FALSE;
                                        }
										if (op->p.p_modrdn.modrdn_newrdn == NULL)
										{
                                            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						                    "IsValidOperation: NULL new rdn for modrdn operation\n");
											return PR_FALSE;
                                        }
										else 
											break;
		
		case SLAPI_OPERATION_DELETE:	break;

		default:						return PR_FALSE;	
	}

	return PR_TRUE;
}



const char *
map_repl_root_to_dbid(Slapi_DN *repl_root)
{
	const char *return_ptr;

	PR_ASSERT(NULL != repl_root);
	if (NULL != repl_root)
	{
		/* XXXggood get per-database ID here, when code available */
	}
	return_ptr = get_server_dataversion(); /* XXXggood temporary hack until we have per-database instance dbids */
	return return_ptr;
}



PRBool 
is_ruv_tombstone_entry (Slapi_Entry *e)
{
    char *dn;
    char *match;
    PR_ASSERT (e);

    dn = slapi_entry_get_dn (e);
    PR_ASSERT (dn);

    /* tombstone has rdn: nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff */
    match = strstr (dn, RUV_STORAGE_ENTRY_UNIQUEID);

    return (match != NULL);    
}

LDAPControl* create_managedsait_control ()
{
    LDAPControl *control;

    control = (LDAPControl*)slapi_ch_malloc (sizeof (LDAPControl));

	control->ldctl_oid = slapi_ch_strdup (LDAP_CONTROL_MANAGEDSAIT);
	control->ldctl_value.bv_val = NULL;
	control->ldctl_value.bv_len = 0;
	control->ldctl_iscritical = '\0';

    return control;
}

LDAPControl* create_backend_control (Slapi_DN *sdn)
{
    LDAPControl *control = NULL;
	const char *be_name = slapi_mtn_get_backend_name(sdn);
	if (NULL != be_name) {
		control = (LDAPControl*)slapi_ch_malloc (sizeof (LDAPControl));
		
		control->ldctl_oid = slapi_ch_strdup ("2.16.840.1.113730.3.4.14");
		control->ldctl_value.bv_val = slapi_ch_strdup(be_name);
		control->ldctl_value.bv_len = strlen (be_name);
		control->ldctl_iscritical = 1;
	}
	
    return control;
}

/*
 * HREF_CHAR_ACCEPTABLE was copied from slapd/referral.c
 * which was copied from libldap/tmplout.c.
 */
/* Note: an identical function is in ../../slapd/referral.c */
#define HREF_CHAR_ACCEPTABLE( c )	(( c >= '-' && c <= '9' ) ||	\
					 ( c >= '@' && c <= 'Z' ) ||	\
					 ( c == '_' ) ||		\
					 ( c >= 'a' && c <= 'z' ))

/*
 *  Function: strcat_escaped
 *
 *  Returns: nothing
 *
 *  Description: Appends string s2 to s1, URL-escaping (%HH) unsafe
 *               characters in s2 as appropriate.  This function was
 *               copied from slapd/referral.c.
 *               which was copied from libldap/tmplout.c.
 *				 added const qualifier
 *               
 * Author: MCS
 */
/*
 * append s2 to s1, URL-escaping (%HH) unsafe characters
 */
/* Note: an identical function is in ../../slapd/referral.c */
static void
strcat_escaped( char *s1, const char *s2 )
{
    char	*p, *q;
    char	*hexdig = "0123456789ABCDEF";

    p = s1 + strlen( s1 );
    for ( q = (char*)s2; *q != '\0'; ++q ) {
	if ( HREF_CHAR_ACCEPTABLE( *q )) {
	    *p++ = *q;
	} else {
	    *p++ = '%';
	    *p++ = hexdig[ 0x0F & ((*(unsigned char*)q) >> 4) ];
	    *p++ = hexdig[ 0x0F & *q ];
	}
    }

    *p = '\0';
}

/*
  This function appends the replication root to the purl referrals found
  in the given ruv and the other given referrals, merges the lists, and sets the
  referrals in the mapping tree node corresponding to the given sdn, which is the
  repl_root
  This function also sets the mapping tree state (e.g. disabled, backend, referral,
  referral on update) - the mapping tree has very specific rules about how states
  can be set in the presence of referrals - specifically:
  1) the nsslapd-referral attribute must be set before changing the state to referral
  or referral on update
  2) the state must be set to backend or disabled before removing referrals
*/
void
repl_set_mtn_state_and_referrals(
	const Slapi_DN *repl_root_sdn,
	const char *mtn_state,
	const RUV *ruv,
	char **ruv_referrals,
	char **other_referrals
)
{
	int rc = 0;
	int ii = 0;
	char **referrals_to_set = NULL;
	PRBool chain_on_update = is_chain_on_update_setup(repl_root_sdn);
	if (NULL == mtn_state) {
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, 
						"repl_set_mtn_referrals: cannot set NULL state.\n");
		return;
	}

	/* Fix for blackflag bug 601440: We want the new behaviour of DS,
	** going forward, to now be that if the nsds5replicareferral attrib
	** has values, it should be the only values in nsslapd-referral (as
	** opposed to older behaviour of concatenating with RUV-based 
	** referrals).   -jay@netscape.com
	*/
	if (other_referrals) {
		/* use the referrals passed in, instead of RUV-based referrals */
		charray_merge(&referrals_to_set, other_referrals, 1);
		/* Do copies. referrals is freed at the end */
	}
	else
	{
		/* use the referrals from the RUV */
		ruv_referrals= (ruv ? ruv_get_referrals(ruv) : ruv_referrals);
		if (ruv_referrals) {
			charray_merge(&referrals_to_set, ruv_referrals, 1);
			if (ruv) /* free referrals from ruv_get_referrals() */
				charray_free(ruv_referrals);
		}
	}

	/* next, add the repl root dn to each referral if not present */
	for (ii = 0; referrals_to_set && referrals_to_set[ii]; ++ii) {
		LDAPURLDesc *lud = NULL;
		int myrc = slapi_ldap_url_parse(referrals_to_set[ii], &lud, 0, NULL);
		/* see if the dn is already in the referral URL */
		if (!lud || !lud->lud_dn) {
			/* add the dn */
			int len = strlen(referrals_to_set[ii]);
			const char *cdn = slapi_sdn_get_dn(repl_root_sdn);
			char *tmpref = NULL;
			int need_slash = 0;
			if (referrals_to_set[ii][len-1] != '/') {
				len++; /* add another one for the slash */
				need_slash = 1;
			}
			len += (strlen(cdn) * 3) + 2;  /* 3 for %HH possible per char */
			tmpref = slapi_ch_malloc(len);
			sprintf(tmpref, "%s%s", referrals_to_set[ii], (need_slash ? "/" : ""));
			strcat_escaped(tmpref, cdn);
			slapi_ch_free((void **)&referrals_to_set[ii]);
			referrals_to_set[ii] = tmpref;
		}
		if (lud)
			ldap_free_urldesc(lud);
	}

        if (!referrals_to_set) { /* deleting referrals */
            /* Set state before */
			if (!chain_on_update) {
				slapi_mtn_set_state(repl_root_sdn, (char *)mtn_state);
			}
            /* We should delete referral only if we want to set the 
               replica database in backend state mode */
			/* if chain on update mode, go ahead and set the referrals anyway */
            if (strcasecmp(mtn_state, STATE_BACKEND) == 0 || chain_on_update) {
                rc = slapi_mtn_set_referral(repl_root_sdn, referrals_to_set);
                if (rc == LDAP_NO_SUCH_ATTRIBUTE) {
                    /* we will get no such attribute (16) if we try to set the referrals to NULL if
                       there are no referrals - not an error */
                    rc = LDAP_SUCCESS;
                }
            }
        } else { /* Replacing */
            rc = slapi_mtn_set_referral(repl_root_sdn, referrals_to_set);
            if (rc == LDAP_SUCCESS && !chain_on_update){
                slapi_mtn_set_state(repl_root_sdn, (char *)mtn_state);
            }
        }

        if (rc != LDAP_SUCCESS) {
		char ebuf[BUFSIZ];
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "repl_set_mtn_referrals: could "
						"not set referrals for replica %s: %d\n",
						escape_string(slapi_sdn_get_dn(repl_root_sdn), ebuf), rc);
	}

	charray_free(referrals_to_set);
	return;
}

/*
 * This function allows to use a local backend in conjunction with
 * a chaining backend
 * The local ldbm backend is the replication consumer database
 * (e.g. on a hub or consumer) - it is read-only except for supplier updates
 * The chaining backend points to the supplier(s)
 * This distribution logic forwards the update request to the chaining 
 * backend, and sends the search request to the local ldbm database
 * 
 * To be able to use it one must define one ldbm backend and one chaining
 * backend in the mapping tree node - the ldbm backend will usually
 * already be there
 * 
 */
int
repl_chain_on_update(Slapi_PBlock *pb, Slapi_DN * target_dn,
					 char **mtn_be_names, int be_count,
					 Slapi_DN * node_dn, int *mtn_be_states)
{
	char * requestor_dn;
	unsigned long op_type;
	Slapi_Operation *op;
	int repl_op = 0;
	int local_backend = -1; /* index of local backend */
	int chaining_backend = -1; /* index of chain backend */
#ifdef DEBUG_CHAIN_ON_UPDATE
	int is_internal = 0;
#endif
	PRBool local_online = PR_FALSE; /* true if the local db is online */
	int ii;
	int opid;
#ifdef DEBUG_CHAIN_ON_UPDATE
	PRUint64 connid = 0;
#endif
	slapi_pblock_get(pb, SLAPI_OPERATION, &op);
#ifdef DEBUG_CHAIN_ON_UPDATE
	if (operation_is_flag_set(op, OP_FLAG_INTERNAL)) {
		is_internal = 1;
	} else {
		slapi_pblock_get(pb, SLAPI_CONN_ID, &connid);
	}
#endif

	slapi_pblock_get(pb, SLAPI_OPERATION_ID, &opid);
	/* first, we have to decide which backend is the local backend
	 * and which is the chaining one
	 * also find out if any are not online (e.g. during import)
	 */
	for (ii = 0; ii < be_count; ++ii)
	{
		Slapi_Backend *be = slapi_be_select_by_instance_name(mtn_be_names[ii]);
		if (slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA))
		{
			chaining_backend = ii;
		}
		else
		{
			local_backend = ii;
			if (mtn_be_states[ii] == SLAPI_BE_STATE_ON)
			{
				local_online = PR_TRUE;
			}
		}
#ifdef DEBUG_CHAIN_ON_UPDATE
		if (is_internal) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=-1 op=%d be "
				"%s is the %s backend and is %s\n", opid,
				mtn_be_names[ii], (chaining_backend == ii) ? "chaining" : "local",
				(mtn_be_states[ii] == SLAPI_BE_STATE_ON) ? "online" : "offline");
		} else {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=%" PRIu64 " op=%d be "
				"%s is the %s backend and is %s\n", connid, opid,
				mtn_be_names[ii], (chaining_backend == ii) ? "chaining" : "local",
				(mtn_be_states[ii] == SLAPI_BE_STATE_ON) ? "online" : "offline");

		}
#endif
	}

	/* if no chaining backends are defined, just use the local one */
	if (chaining_backend == -1) {
		return local_backend;
	}

	/* All internal operations go to the local backend */
	if (operation_is_flag_set(op, OP_FLAG_INTERNAL)) {
		return local_backend;
	}

	/* Check the operation type
	 * read-only operation will go to the local backend if online
	 */
	op_type = slapi_op_get_type(op);
	if (local_online &&
		((op_type == SLAPI_OPERATION_SEARCH) ||
	    (op_type == SLAPI_OPERATION_UNBIND) ||
	    (op_type == SLAPI_OPERATION_COMPARE))) {
#ifdef DEBUG_CHAIN_ON_UPDATE
		if (is_internal) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=-1 op=%d op is "
						"%d: using local backend\n", opid, op_type);
		} else {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=%" PRIu64 " op=%d op is "
						"%d: using local backend\n", connid, opid, op_type);
		}
#endif
		return local_backend;
	}

	/* if the operation is done by directory manager
	 * use local database even for updates because it is an administrative
	 * operation
	 * remarks : one could also use an update DN in the same way
	 * to let update operation go to the local backend when they are done 
	 * by specific administrator user but let all the other user 
	 * go to the read-write replica
	 * also - I don't think it is possible to chain directory manager
	 */
	slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &requestor_dn);
	if (slapi_dn_isroot(requestor_dn)) {
#ifdef DEBUG_CHAIN_ON_UPDATE
		if (is_internal) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=-1 op=%d requestor "
						"is root: using local backend\n", opid);
		} else {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=%" PRIu64 " op=%d requestor "
						"is root: using local backend\n", connid, opid);
		}
#endif
		return local_backend;
	}

	/* if the operation is a replicated operation
	 * use local database even for updates to avoid infinite loops
	 */
	slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
	if (repl_op) {
#ifdef DEBUG_CHAIN_ON_UPDATE
		if (is_internal) {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=-1 op=%d op is "
						"replicated: using local backend\n", opid);
		} else {
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=%" PRIu64 " op=%d op is "
						"replicated: using local backend\n", connid, opid);
		}
#endif
		return local_backend;
	}

    /* if using global password policy, chain the bind request so that the 
       master can update and replicate the password policy op attrs */
	if (op_type == SLAPI_OPERATION_BIND) {
        extern int config_get_pw_is_global_policy();
        if (!config_get_pw_is_global_policy()) {
#ifdef DEBUG_CHAIN_ON_UPDATE
            if (is_internal) {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=-1 op=%d using "
                            "local backend for local password policy\n", opid);
            } else {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=%" PRIu64 " op=%d using "
                            "local backend for local password policy\n", connid, opid);
            }
#endif
            return local_backend;
        }
    }

	/* all other case (update while not directory manager) :
	 * or any normal non replicated client operation while local is disabled (import) :
	 * use the chaining backend 
	 */
#ifdef DEBUG_CHAIN_ON_UPDATE
	if (is_internal) {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=-1 op=%d using "
					"chaining backend\n", opid);
	} else {
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "repl_chain_on_update: conn=%" PRIu64 " op=%d using "
					"chaining backend\n", connid, opid);
	}
#endif
	return chaining_backend;
}

int
repl_enable_chain_on_update(Slapi_DN *suffix)
{
    /* Submit a Modify operation to add the distribution function to the mapping tree
	   node for the given suffix */
    slapi_mods smods;
	int operation_result;
	Slapi_PBlock *pb= slapi_pblock_new();
	char *mtnnodedn;

	slapi_mods_init(&smods,2);

	/* need path and file name of the replication plugin here */
	slapi_mods_add_string(&smods, LDAP_MOD_ADD, "nsslapd-distribution-plugin", replpluginpath);
	slapi_mods_add_string(&smods, LDAP_MOD_ADD, "nsslapd-distribution-funct", "repl_chain_on_update");

	/* need DN of mapping tree node here */
	mtnnodedn = slapi_get_mapping_tree_node_configdn(suffix);
	slapi_modify_internal_set_pb(
		pb,
		mtnnodedn,
		slapi_mods_get_ldapmods_byref(&smods), /* JCM cast */
		NULL, /*Controls*/
		NULL, /*uniqueid*/
		repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
		0);

	slapi_modify_internal_pb(pb); 
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &operation_result);
	slapi_ch_free_string(&mtnnodedn);
	slapi_pblock_destroy(pb);
	switch(operation_result)
	{
	case LDAP_SUCCESS:
		/* OK, everything is fine. */
		break;
	default:
		PR_ASSERT(0); /* JCMREPL FOR DEBUGGING */
	}
	slapi_mods_done(&smods);

	return operation_result;
}

int
repl_disable_chain_on_update(Slapi_DN *suffix)
{
    /* Submit a Modify operation to remove the distribution function from the mapping tree
	   node for the given suffix */
    slapi_mods smods;
	int operation_result;
	Slapi_PBlock *pb= slapi_pblock_new();
	char *mtnnodedn;

	slapi_mods_init(&smods,2);

	slapi_mods_add_modbvps(&smods, LDAP_MOD_DELETE, "nsslapd-distribution-plugin", NULL);
	slapi_mods_add_modbvps(&smods, LDAP_MOD_DELETE, "nsslapd-distribution-funct", NULL);

	/* need DN of mapping tree node here */
	mtnnodedn = slapi_get_mapping_tree_node_configdn(suffix);
	slapi_modify_internal_set_pb(
		pb,
		mtnnodedn,
		slapi_mods_get_ldapmods_byref(&smods), /* JCM cast */
		NULL, /*Controls*/
		NULL, /*uniqueid*/
		repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
		0);

	slapi_modify_internal_pb(pb); 
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &operation_result);
	slapi_ch_free_string(&mtnnodedn);
	slapi_pblock_destroy(pb);
	switch(operation_result)
	{
	case LDAP_SUCCESS:
		/* OK, everything is fine. */
		break;
	default:
		PR_ASSERT(0); /* JCMREPL FOR DEBUGGING */
	}
	slapi_mods_done(&smods);

	return operation_result;
}

static PRBool
is_chain_on_update_setup(const Slapi_DN *replroot)
{
    /* Do an internal search of the mapping tree node to see if chain on update is setup
	   for this replica
	   - has two backends
	   - has a distribution function
	   - has a distribution plugin
	   - one of the backends is a ldbm database
	   - one of the backends is a chaining database
	*/
	static char* attrs[] = { "nsslapd-backend",
							 "nsslapd-distribution-plugin", "nsslapd-distribution-funct",
							 NULL };
	int operation_result;
	Slapi_PBlock *pb= slapi_pblock_new();
	char *mtnnodedn = slapi_get_mapping_tree_node_configdn(replroot);
	PRBool retval = PR_FALSE;
	
	slapi_search_internal_set_pb(
		pb,
		mtnnodedn,
		LDAP_SCOPE_BASE,
		"objectclass=*",
		attrs, /*attrs*/
		0, /*attrsonly*/
		NULL, /*Controls*/
		NULL, /*uniqueid*/
		repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION),
		0);
	slapi_search_internal_pb(pb); 
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &operation_result);
	switch(operation_result)
	{
	case LDAP_SUCCESS:
	{
		Slapi_Entry **entries= NULL;
		slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
		if(entries!=NULL && entries[0]!=NULL)
		{
			Slapi_Entry *e = entries[0];

			char **backends = slapi_entry_attr_get_charray(e, "nsslapd-backend");
			char *plg = slapi_entry_attr_get_charptr(e, "nsslapd-distribution-plugin");
			char *func = slapi_entry_attr_get_charptr(e, "nsslapd-distribution-funct");

			if (backends && backends[0] && backends[1] && plg && func)
			{
				/* all the necessary attrs are present - check to see if we
				   have one chaining backend */
				Slapi_Backend *be0 = slapi_be_select_by_instance_name(backends[0]);
				Slapi_Backend *be1 = slapi_be_select_by_instance_name(backends[1]);
				PRBool foundchain0 = slapi_be_is_flag_set(be0,SLAPI_BE_FLAG_REMOTE_DATA);
				PRBool foundchain1 = slapi_be_is_flag_set(be1,SLAPI_BE_FLAG_REMOTE_DATA);
				retval = (foundchain0 || foundchain1) &&
					!(foundchain0 && foundchain1); /* 1 (but not both) backend is chaining */
			}
			slapi_ch_array_free(backends);
			slapi_ch_free_string(&plg);
			slapi_ch_free_string(&func);
		}
		else /* could not find mapping tree entry - assume not set up */
		{
		}
	}
	break;
	default: /* search error - assume not set up */
		break;
	}
	slapi_ch_free_string(&mtnnodedn);
    slapi_free_search_results_internal(pb);
	slapi_pblock_destroy(pb);

	return retval;
}

void
repl_set_repl_plugin_path(const char *path)
{
	replpluginpath = slapi_ch_strdup(path);
}
