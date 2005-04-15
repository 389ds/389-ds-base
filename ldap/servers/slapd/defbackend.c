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
/*
 * defbackend.c - implement a "backend of last resort" which is used only
 * when a request's basedn does not match one of the suffixes of any of the
 * configured backends.
 *
 */

#include "slap.h"

/*
 * ---------------- Macros ---------------------------------------------------
 */
#define DEFBACKEND_TYPE			"default"

#define DEFBACKEND_OP_NOT_HANDLED	0
#define DEFBACKEND_OP_HANDLED		1


/*
 * ---------------- Static Variables -----------------------------------------
 */
static struct slapdplugin	defbackend_plugin;
static Slapi_Backend			*defbackend_backend = NULL;


/*
 * ---------------- Prototypes for Private Functions -------------------------
 */
static int defbackend_default( Slapi_PBlock *pb );
static int defbackend_noop( Slapi_PBlock *pb );
static int defbackend_abandon( Slapi_PBlock *pb );
static int defbackend_bind( Slapi_PBlock *pb );
static int defbackend_next_search_entry( Slapi_PBlock *pb );


/*
 * ---------------- Public Functions -----------------------------------------
 */

/*
 * defbackend_init:  instantiate the default backend
 */
void
defbackend_init( void )
{
    int			rc;
    char		*errmsg;
    Slapi_PBlock	pb;

    LDAPDebug( LDAP_DEBUG_TRACE, "defbackend_init\n", 0, 0, 0 );

    /*
     * create a new backend
     */
    pblock_init( &pb );
    defbackend_backend = slapi_be_new( DEFBACKEND_TYPE , DEFBACKEND_TYPE, 1 /* Private */, 0 /* Do Not Log Changes */ );
    if (( rc = slapi_pblock_set( &pb, SLAPI_BACKEND, defbackend_backend ))
	    != 0 ) {
	errmsg = "slapi_pblock_set SLAPI_BACKEND failed";
	goto cleanup_and_return;
    }

    /*
     * create a plugin structure for this backend since the
     * slapi_pblock_set()/slapi_pblock_get() functions assume there is one.
     */
    memset( &defbackend_plugin, '\0', sizeof( struct slapdplugin ));
    defbackend_plugin.plg_type = SLAPI_PLUGIN_DATABASE;
    defbackend_backend->be_database = &defbackend_plugin;
    if (( rc = slapi_pblock_set( &pb, SLAPI_PLUGIN, &defbackend_plugin ))
	    != 0 ) {
	errmsg = "slapi_pblock_set SLAPI_PLUGIN failed";
	goto cleanup_and_return;
    }

    /* default backend is managed as if it would */
    /* contain remote data.			 */
    slapi_be_set_flag(defbackend_backend,SLAPI_BE_FLAG_REMOTE_DATA);

    /*
     * install handler functions, etc.
     */
    errmsg = "slapi_pblock_set handlers failed";
    rc = slapi_pblock_set( &pb, SLAPI_PLUGIN_VERSION,
	    (void *)SLAPI_PLUGIN_CURRENT_VERSION );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_BIND_FN,
	    (void *)defbackend_bind );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_UNBIND_FN,
	    (void *)defbackend_noop );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_SEARCH_FN,
	    (void *)defbackend_default );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN,
	    (void *)defbackend_next_search_entry );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_COMPARE_FN,
	    (void *)defbackend_default );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_MODIFY_FN,
	    (void *)defbackend_default );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_MODRDN_FN,
	    (void *)defbackend_default );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_ADD_FN,
	    (void *)defbackend_default );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_DELETE_FN,
	    (void *)defbackend_default );
    rc |= slapi_pblock_set( &pb, SLAPI_PLUGIN_DB_ABANDON_FN,
	    (void *)defbackend_abandon );

cleanup_and_return:
    if ( rc != 0 ) {
	LDAPDebug( LDAP_DEBUG_ANY, "defbackend_init: failed (%s)\n",
		errmsg, 0, 0 );
	exit( 1 );
    }
}


/*
 * defbackend_get_backend: return a pointer to the default backend.
 * we never return NULL.
 */
Slapi_Backend *
defbackend_get_backend( void )
{
    return( defbackend_backend );
}


/*
 * ---------------- Private Functions ----------------------------------------
 */

static int
defbackend_default( Slapi_PBlock *pb )
{
    LDAPDebug( LDAP_DEBUG_TRACE, "defbackend_default\n", 0, 0, 0 );

    send_nobackend_ldap_result( pb );

    return( DEFBACKEND_OP_HANDLED );
}


static int
defbackend_noop( Slapi_PBlock *pb )
{
    LDAPDebug( LDAP_DEBUG_TRACE, "defbackend_noop\n", 0, 0, 0 );

    return( DEFBACKEND_OP_HANDLED );
}


static int
defbackend_abandon( Slapi_PBlock *pb )
{
    LDAPDebug( LDAP_DEBUG_TRACE, "defbackend_abandon\n", 0, 0, 0 );

    /* nothing to do */
    return( DEFBACKEND_OP_HANDLED );
}


static int
defbackend_bind( Slapi_PBlock *pb )
{
    int			rc, method;
    struct berval	*cred;

    LDAPDebug( LDAP_DEBUG_TRACE, "defbackend_bind\n", 0, 0, 0 );

    /*
     * Accept simple binds that do not contain passwords (but do not
     * update the bind DN field in the connection structure since we don't
     * grant access based on these "NULL binds")
     */
    slapi_pblock_get( pb, SLAPI_BIND_METHOD, &method );
    slapi_pblock_get( pb, SLAPI_BIND_CREDENTIALS, &cred );
    if ( method == LDAP_AUTH_SIMPLE && cred->bv_len == 0 ) {
	PR_AtomicIncrement(g_get_global_snmp_vars()->ops_tbl.dsAnonymousBinds);
	rc = SLAPI_BIND_ANONYMOUS;
    } else {
	send_nobackend_ldap_result( pb );
	rc = SLAPI_BIND_FAIL;
    }

    return( rc );
}



static int
defbackend_next_search_entry( Slapi_PBlock *pb )
{
    LDAPDebug( LDAP_DEBUG_TRACE, "defbackend_next_search_entry\n", 0, 0, 0 );

    return( 0 );	/* no entries and no error */
}
