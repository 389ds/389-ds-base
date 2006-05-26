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

/************************************************************

 testpostop.c

 This source file provides examples of post-operation plug-in 
 functions.  The server calls these plug-in functions after 
 executing certain LDAP operations:

 * testpostop_add (called after an LDAP add operation)	
 * testpostop_mod (called after an LDAP modify operation)
 * testpostop_del (called after an LDAP delete operation)
 * testpostop_modrdn (called after an LDAP modify RDN operation)
 * testpostop_abandon (called after an LDAP abandon operation)

 After the server processes an LDAP add, modify, delete, or 
 modify RDN operation, these post-operation plug-in functions 
 log information about the operation to a change log file. 

 The post-abandon plugin simply logs some information to the error
 log to demonstrate that it was called.

 To test this plug-in function, stop the server, edit the dse.ldif file
 (in the <server_root>/slapd-<server_id>/config directory)
 and add the following lines before restarting the server :

 dn: cn=Test PostOp,cn=plugins,cn=config
 objectClass: top
 objectClass: nsSlapdPlugin
 objectClass: extensibleObject
 cn: Test PostOp
 nsslapd-pluginPath: <server_root>/plugins/slapd/slapi/examples/libtest-plugin.so
 nsslapd-pluginInitfunc: testpostop_init
 nsslapd-pluginType: postoperation
 nsslapd-pluginEnabled: on
 nsslapd-plugin-depends-on-type: database
 nsslapd-pluginId: test-postop

 ************************************************************/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "slapi-plugin.h"

#define SLAPD_LOGGING 1
#define	_ADD	0
#define _MOD	1
#define _DEL	2
#define _MODRDN	3

#ifdef _WIN32
static char changelogfile[MAX_PATH+1];
#else
static char *changelogfile = "/tmp/changelog";
#endif

Slapi_PluginDesc postoppdesc = { "test-postop", "Fedora Project", "7.1 SP3",
	"sample post-operation plugin" };

static void write_changelog( int optype, char *dn, void *change, int flag );

/* Current time is a function defined in the server. */
time_t current_time( void );

/* Post-operation plug-in function */
int
testpostop_add( Slapi_PBlock *pb )
{
	Slapi_Entry	*e;
	char		*dn;

	/* Get the entry that has been added and the DN of
	   that entry. */
	if ( slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &e ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_ADD_TARGET, &dn ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		    "testpostop_add", "Could not get parameters\n" );
		return( -1 );
	}

	/* Log the DN of the newly added entry in the server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testpostop_add",
		"Added entry (%s)\n", dn );

	/* Log the DN and the entry to the change log file. */
	write_changelog( _ADD, dn, (void *) e, 0 );

	return( 0 );	/* allow the operation to continue */
}

/* Post-operation plug-in function. */
int
testpostop_mod( Slapi_PBlock *pb )
{
	char	*dn;
	LDAPMod	**mods;

	/* Get the DN of the modified entry and the modifications made. */
	if ( slapi_pblock_get( pb, SLAPI_MODIFY_TARGET, &dn ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		    "testpostop_mod", "Could not get parameters\n" );
		return( -1 );
	}

	/* Log the DN of the modified entry to the server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testpostop_mod", 
		"Modified entry (%s)\n", dn );

	/* Log the DN and the modifications made to the change log file. */
	write_changelog( _MOD, dn, (void *) mods, 0 );

	return( 0 );	/* allow the operation to continue */
}

/* Post-operation plug-in function */
int
testpostop_del( Slapi_PBlock *pb )
{
	char	*dn;

	/* Get the DN of the entry that was removed from the directory. */
	if ( slapi_pblock_get( pb, SLAPI_DELETE_TARGET, &dn ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		    "testpostop_del", "Could not get parameters\n" );
		return( -1 );
	}

	/* Log the DN of the deleted entry to the server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testpostop_del",
		"Deleted entry (%s)\n", dn );

	/* Log the DN of the deleted entry to the change log. */
	write_changelog( _DEL, dn, NULL, 0 );

	return( 0 );	/* allow the operation to continue */
}

/* Post-operation plug-in function */
int
testpostop_modrdn( Slapi_PBlock *pb )
{
	char	*dn;
	char	*newrdn;
	int	dflag;

	/* Get the DN of the renamed entry, the new RDN of the entry,
	   and the flag indicating whether or not the old RDN was
	   removed from the entry. */
	if ( slapi_pblock_get( pb, SLAPI_MODRDN_TARGET, &dn ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &newrdn ) != 0 ||
	    slapi_pblock_get( pb, SLAPI_MODRDN_DELOLDRDN, &dflag ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		    "testpostop_modrdn", "Could not get parameters\n" );
		return( -1 );
	}

	/* Log the DN of the renamed entry to the server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testpostop_modrdn", 
		"modrdn entry (%s)\n", dn );

	/* Log the DN of the renamed entry, its new RDN, and the
	   flag (the one indicating whether or not the old RDN was
	   removed from the entry) to the change log. */
	write_changelog( _MODRDN, dn, (void *) newrdn, dflag );

	return( 0 );	/* allow the operation to continue */
}

/* Post-operation plug-in function */
int
testpostop_abandon( Slapi_PBlock *pb )
{
	int		msgid;

	/* Get the LDAP message ID of the abandoned operation */
	if ( slapi_pblock_get( pb, SLAPI_ABANDON_MSGID, &msgid ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN,
		"testpostop_abandon", "Could not get parameters\n" );
		return( -1 );
	}

	/* Log information about the abandon operation to the
	   server error log. */
	slapi_log_error( SLAPI_LOG_PLUGIN, "testpostop_abandon",  
		"Postoperation abandon function called.\n" 
		"\tTarget MsgID: %d\n",
		msgid );

	return( 0 );	/* allow the operation to continue */
}

/* Initialization function */
#ifdef _WIN32
__declspec(dllexport)
#endif
int
testpostop_init( Slapi_PBlock *pb )
{
	/* Register the four post-operation plug-in functions, 
	   and specify the server plug-in version. */
	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    			SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
				(void *)&postoppdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_ADD_FN,
				(void *) testpostop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODIFY_FN,
				(void *) testpostop_mod ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_DELETE_FN,
                                (void *) testpostop_del ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODRDN_FN,
                                (void *) testpostop_modrdn ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_ABANDON_FN,
                                (void *) testpostop_abandon ) != 0 ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "testpostop_init",
			 "Failed to set version and functions\n" );
		return( -1 );
	}
	return( 0 );
}

/* Function for generating a newly allocated string that contains the
   specified time.  The time is expressed as generalizedTime, except 
   without the time zone. */
static char*
format_localTime( time_t timeval )
{
    char* into;
    struct tm t;
#ifdef _WIN32
    memcpy (&t, localtime (&timeval), sizeof(t));
#else
    localtime_r (&timeval, &t);
#endif
    
    /* Allocate memory for the formatted string.  (slapi_ch_malloc()
       should be used in server plug-ins instead of malloc().)
       This string is freed by the calling function write_changelog(). */
    into = slapi_ch_malloc(15);
    sprintf (into, "%.4li%.2i%.2i%.2i%.2i%.2i",
	     1900L + t.tm_year, 1 + t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return into;
}

/* Logs information on an operation to a change log file. 
   Parameters;
   - optype is type of LDAP operation to record:
     - _ADD for LDAP add operations
     - _MOD for LDAP modify operations
     - _DEL for LDAP delete operations
     - _MODRDN for LDAP modify RDN operations
   - dn is DN of the entry affected by the operation.
   - change is information about the operation performed.
     The type of information depends on the value of optype:
     - For _ADD, it is the newly added entry (Slapi_Entry).
     - For _MOD, it is the list of modifications made (array of
       LDAPMod).
     - For _DEL, it is NULL.
     - For _MODRDN, it is the new RDN of the entry.
   - flag is only used for LDAP modify RDN operations.
     It represents the flag that indicates whether or not
     the old RDN has been removed.
*/
static void
write_changelog(
    int			optype,
    char		*dn,
    void		*change,
    int			flag
)
{
    LDAPMod	**mods;
    Slapi_Entry	*e;
    char	*newrdn, *tmp, *tmpsave;
    FILE	*fp;
    int		len, i, j;
    char*	timestr;
#ifdef _WIN32
	char szTmpPath[MAX_PATH+1];
#endif

    /* Open the change log file */
#ifdef _WIN32        
	GetTempPath( MAX_PATH, szTmpPath ); 
	strcpy( changelogfile, szTmpPath );
	strcat( changelogfile, "\\" );
	strcat( changelogfile, "changelog.txt" );
#endif
	if ( changelogfile == NULL ) {
		return;
	}
	if ( (fp = fopen( changelogfile, "ab" )) == NULL ) {
		slapi_log_error( SLAPI_LOG_PLUGIN, "write_changelog", 
			"Could not open log file %s\n", changelogfile );
		return;
	}

    /* Log the current time of the operation in generalizedTime form */
    timestr = format_localTime( current_time() );
    fprintf( fp, "time: %s\n", timestr );
    slapi_ch_free( ( void ** ) &timestr);
    timestr = NULL;

    /* Print the DN of the entry affected by the operation. */
    fprintf( fp, "dn: %s\n", dn );

    /* Log information about the operation */
    switch ( optype ) {
    case _MOD:
	/* For modify operations, log the attribute type
	   that has been added, replaced, or deleted. */
	fprintf( fp, "changetype: modify\n" );
	mods = (LDAPMod **)change;
	for ( j = 0; mods[j] != NULL; j++ ) {
	    switch ( mods[j]->mod_op & ~LDAP_MOD_BVALUES ) {
	    case LDAP_MOD_ADD:
		fprintf( fp, "add: %s\n", mods[j]->mod_type );
		break;

	    case LDAP_MOD_DELETE:
		fprintf( fp, "delete: %s\n", mods[j]->mod_type );
		break;

	    case LDAP_MOD_REPLACE:
		fprintf( fp, "replace: %s\n", mods[j]->mod_type );
		break;
	    }

	    for ( i = 0; mods[j]->mod_bvalues != NULL &&
		    mods[j]->mod_bvalues[i] != NULL; i++ ) {
		/* XXX should handle binary values XXX */
		fprintf( fp, "%s: %s\n", mods[j]->mod_type,
		    mods[j]->mod_bvalues[i]->bv_val );
	    }
	    fprintf( fp, "-\n" );
	}
	break;

    case _ADD:
	/* For LDAP add operations, log the newly added entry. */
	e = (Slapi_Entry *)change;
	fprintf( fp, "changetype: add\n" );
	/* Get the LDIF string representation of the entry. */
	tmp = slapi_entry2str( e, &len );
	tmpsave = tmp;
	/* Skip the first line, which is the dn: line */
	while (( tmp = strchr( tmp, '\n' )) != NULL ) {
	    tmp++;
	    if ( !isspace( *tmp )) {
		break;
	    }
	}
	fprintf( fp, "%s", tmp );
	slapi_ch_free( (void **)&tmpsave );
	break;

    case _DEL:
	/* For the LDAP delete operation, log the DN of the
	   removed entry.  (Since this is already done earlier,
	   the plug-in just needs to note the type of operation
	   performed. */
	fprintf( fp, "changetype: delete\n" );
	break;

    case _MODRDN:
	/* For the LDAP modify RDN operation, log the new RDN
	   and the flag indicating whether or not the old RDN
	   was removed. */
	newrdn = (char *)change;
	fprintf( fp, "changetype: modrdn\n" );
	fprintf( fp, "newrdn: %s\n", newrdn );
	fprintf( fp, "deleteoldrdn: %d\n", flag ? 1 : 0 );
    }
    fprintf( fp, "\n" );

    fclose( fp );
}

