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

/* init.c - initialize ldbm backend */

#include "back-ldbm.h"
#include "../slapi-plugin.h"
#include "idlapi.h"

static void *IDL_api[3];

static Slapi_PluginDesc pdesc = { "ldbm-backend", PLUGIN_MAGIC_VENDOR_STR,
        PRODUCTTEXT, "high-performance LDAP backend database plugin" };

static int add_ldbm_internal_attr_syntax( const char *name, const char *oid,
		const char *syntax, const char *mr_equality, unsigned long extraflags );

#ifdef _WIN32
int *module_ldap_debug = 0;

void 
plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

/* pb: not used */
int
ldbm_back_add_schema( Slapi_PBlock *pb )
{
	int rc = add_ldbm_internal_attr_syntax( "entrydn",
			LDBM_ENTRYDN_OID, DN_SYNTAX_OID, DNMATCH_NAME,
			SLAPI_ATTR_FLAG_SINGLE );

	rc |= add_ldbm_internal_attr_syntax( "dncomp",
			LDBM_DNCOMP_OID, DN_SYNTAX_OID, DNMATCH_NAME,
			0 );

	rc |= add_ldbm_internal_attr_syntax( "parentid",
			LDBM_PARENTID_OID, DIRSTRING_SYNTAX_OID, CASEIGNOREMATCH_NAME,
			SLAPI_ATTR_FLAG_SINGLE );

	rc |= add_ldbm_internal_attr_syntax( "entryid",
			LDBM_ENTRYID_OID, DIRSTRING_SYNTAX_OID, CASEIGNOREMATCH_NAME,
			SLAPI_ATTR_FLAG_SINGLE );

	return rc;
}

int 
ldbm_back_init( Slapi_PBlock *pb )
{
	struct ldbminfo	*li;
	int		rc;
	struct slapdplugin *p;
	static int interface_published = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> ldbm_back_init\n", 0, 0, 0 );

   	slapi_pblock_get(pb, SLAPI_PLUGIN, &p);
	
	/* allocate backend-specific stuff */
	li = (struct ldbminfo *) slapi_ch_calloc( 1, sizeof(struct ldbminfo) );
	
	/* Record the identity of the ldbm plugin.  The plugin 
	 * identity is used during internal ops. */
	slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &(li->li_identity));

	/* keep a pointer back to the plugin */
	li->li_plugin = p;
	
	/* set shutdown flag to zero.*/
	li->li_shutdown = 0;

	/* Initialize the set of instances. */
	li->li_instance_set = objset_new(&ldbm_back_instance_set_destructor);
	
	/* initialize dblayer  */
	if (dblayer_init(li)) {
		LDAPDebug( LDAP_DEBUG_ANY, "ldbm_back_init: dblayer_init failed\n",0, 0, 0 );
		return (-1);
	}

	/* Fill in the fields of the ldbminfo and the dblayer_private
	 * structures with some default values */
	ldbm_config_setup_default(li);

        /* ask the factory to give us space in the Connection object
         * (only bulk import uses this)
         */
        if (slapi_register_object_extension(p->plg_name, SLAPI_EXT_CONNECTION,
            factory_constructor, factory_destructor,
            &li->li_bulk_import_object, &li->li_bulk_import_handle) != 0) {
            LDAPDebug(LDAP_DEBUG_ANY, "ldbm_back_init: "
                      "slapi_register_object_extension failed.\n", 0, 0, 0);
            return (-1);
        }

	/* add some private attributes */
	rc = ldbm_back_add_schema( pb );

	/* set plugin private pointer and initialize locks, etc. */
	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_PRIVATE, (void *) li );
	
	if ((li->li_dbcache_mutex = PR_NewLock()) == NULL ) {
            LDAPDebug( LDAP_DEBUG_ANY, "ldbm_back_init: PR_NewLock failed\n",
		0, 0, 0 );
            return(-1);
        }

	if ((li->li_shutdown_mutex = PR_NewLock()) == NULL ) {
            LDAPDebug( LDAP_DEBUG_ANY, "ldbm_back_init: PR_NewLock failed\n",
		0, 0, 0 );
            return(-1);
        }

	if ((li->li_config_mutex = PR_NewLock()) == NULL ) {
            LDAPDebug( LDAP_DEBUG_ANY, "ldbm_back_init: PR_NewLock failed\n",
		0, 0, 0 );
            return(-1);
        }

	if ((li->li_dbcache_cv = PR_NewCondVar( li->li_dbcache_mutex )) == NULL ) {
            LDAPDebug( LDAP_DEBUG_ANY, "ldbm_back_init: PR_NewCondVar failed\n", 0, 0, 0 );
            exit(-1);
        }

	/* set all of the necessary database plugin callback functions */
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_03 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_BIND_FN, 
	    (void *) ldbm_back_bind );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_UNBIND_FN, 
	    (void *) ldbm_back_unbind );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_SEARCH_FN, 
	    (void *) ldbm_back_search );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN,
	    (void *) ldbm_back_next_search_entry );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN,
	    (void *) ldbm_back_next_search_entry_ext );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_ENTRY_RELEASE_FN,
	    (void *) ldbm_back_entry_release );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_COMPARE_FN, 
	    (void *) ldbm_back_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_MODIFY_FN, 
	    (void *) ldbm_back_modify );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_MODRDN_FN, 
	    (void *) ldbm_back_modrdn );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_ADD_FN, 
	    (void *) ldbm_back_add );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_DELETE_FN, 
	    (void *) ldbm_back_delete );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_ABANDON_FN, 
	    (void *) ldbm_back_abandon );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN, 
	    (void *) ldbm_back_close );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_CLEANUP_FN, 
	    (void *) ldbm_back_cleanup );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_FLUSH_FN, 
	    (void *) ldbm_back_flush );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN, 
	    (void *) ldbm_back_start );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_SEQ_FN, 
	    (void *) ldbm_back_seq );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_RMDB_FN,
	    (void *) ldbm_back_rmdb );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_LDIF2DB_FN,
	    (void *) ldbm_back_ldif2ldbm );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_DB2LDIF_FN,
	    (void *) ldbm_back_ldbm2ldif );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_DB2INDEX_FN,
	    (void *) ldbm_back_ldbm2index );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_ARCHIVE2DB_FN,
	    (void *) ldbm_back_archive2ldbm );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_DB2ARCHIVE_FN,
	    (void *) ldbm_back_ldbm2archive );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_UPGRADEDB_FN,
	    (void *) ldbm_back_upgradedb );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_DBVERIFY_FN,
	    (void *) ldbm_back_dbverify );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_BEGIN_FN,
	    (void *) dblayer_plugin_begin );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_COMMIT_FN,
	    (void *) dblayer_plugin_commit );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_ABORT_FN,
	    (void *) dblayer_plugin_abort );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_SIZE_FN,
	    (void *) ldbm_db_size );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_TEST_FN,
	    (void *) ldbm_back_db_test );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_INIT_INSTANCE_FN,
	    (void *) ldbm_back_init ); /* register itself so that the secon instance
                                          can be initialized */
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_WIRE_IMPORT_FN,
	    (void *) ldbm_back_wire_import );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_ADD_SCHEMA_FN,
	    (void *) ldbm_back_add_schema );

	if ( rc != 0 ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ldbm_back_init failed\n", 0, 0, 0 );
		return( -1 );
	}
	
	/* register the IDL interface with the API broker */
	if(!interface_published)
	{
		IDL_api[0] = 0;
		IDL_api[1] = (void *)idl_alloc;
		IDL_api[2] = (void *)idl_insert;

		if( slapi_apib_register(IDL_v1_0_GUID, IDL_api) )
		{
			LDAPDebug( LDAP_DEBUG_ANY, "ldbm_back_init: failed to publish IDL interface\n", 0, 0, 0);
			return( -1 );
		}

		interface_published = 1;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= ldbm_back_init\n", 0, 0, 0 );

	return( 0 );
}


/*
 * Add an attribute syntax using some default flags, etc.
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well)
 */
static int
add_ldbm_internal_attr_syntax( const char *name, const char *oid,
		const char *syntax, const char *mr_equality, unsigned long extraflags )
{
	int rc = LDAP_SUCCESS;
	struct asyntaxinfo	*asip;
	char *names[2];
	char *origins[2];
	unsigned long std_flags = SLAPI_ATTR_FLAG_STD_ATTR | SLAPI_ATTR_FLAG_OPATTR
							| SLAPI_ATTR_FLAG_NOUSERMOD;

	names[0] = (char *)name;
	names[1] = NULL;

	origins[0] = SLAPD_VERSION_STR;
	origins[1] = NULL;

	rc = attr_syntax_create( oid, names, 1,
			"Fedora defined attribute type",
			 NULL,						/* superior */
			 mr_equality, NULL, NULL,	/* matching rules */
			 origins, syntax,
			 SLAPI_SYNTAXLENGTH_NONE,
			 std_flags | extraflags,
			 &asip );

	if ( rc == LDAP_SUCCESS ) {
		rc = attr_syntax_add( asip );
	}

	return rc;
}
