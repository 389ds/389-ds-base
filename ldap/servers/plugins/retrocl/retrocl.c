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
 * Requires that create_instance.c have added a plugin entry similar to:

dn: cn=Retrocl Plugin,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: RetroCL Plugin
nsslapd-pluginpath: /export2/servers/Hydra-supplier/lib/retrocl-plugin.so
nsslapd-plugininitfunc: retrocl_plugin_init
nsslapd-plugintype: object
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
nsslapd-pluginid: retrocl
nsslapd-pluginversion: 5.0b2
nsslapd-pluginvendor: Sun Microsystems, Inc.
nsslapd-plugindescription: Retrocl Plugin

 *
 */

#include "retrocl.h"

#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
    module_ldap_debug = level_ptr;
}
#endif

void* g_plg_identity [PLUGIN_MAX];

Slapi_Backend *retrocl_be_changelog = NULL;

/* ----------------------------- Retrocl Plugin */

static Slapi_PluginDesc retrocldesc = {"retrocl", VENDOR, PACKAGE_VERSION, "Retrocl Plugin"};
static Slapi_PluginDesc retroclpostopdesc = {"retrocl-postop", VENDOR, PACKAGE_VERSION, "retrocl post-operation plugin"};
static Slapi_PluginDesc retroclinternalpostopdesc = {"retrocl-internalpostop", VENDOR, PACKAGE_VERSION, "retrocl internal post-operation plugin"};


/*
 * Function: retrocl_*
 *
 * Returns: LDAP_
 * 
 * Arguments: Pb of operation
 *
 * Description: wrappers around retrocl_postob registered as callback
 *
 */

int retrocl_postop_add (Slapi_PBlock *pb) { return retrocl_postob(pb,OP_ADD);}
int retrocl_postop_delete (Slapi_PBlock *pb) { return retrocl_postob(pb,OP_DELETE);}
int retrocl_postop_modify (Slapi_PBlock *pb) { return retrocl_postob(pb,OP_MODIFY);}
int retrocl_postop_modrdn (Slapi_PBlock *pb) { return retrocl_postob(pb,OP_MODRDN);}

/*
 * Function: retrocl_postop_init
 *
 * Returns: 0/-1
 * 
 * Arguments: Pb
 *
 * Description: callback function
 *
 */

int
retrocl_postop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&retroclpostopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_ADD_FN, (void *) retrocl_postop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_DELETE_FN, (void *) retrocl_postop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODIFY_FN, (void *) retrocl_postop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODRDN_FN, (void *) retrocl_postop_modrdn ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "retrocl_postop_init failed\n" );
		rc= -1;
	}

	return rc;
}

/*
 * Function: retrocl_internalpostop_init
 *
 * Returns: 0/-1
 * 
 * Arguments: Pb
 *
 * Description: callback function
 *
 */

int
retrocl_internalpostop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&retroclinternalpostopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN, (void *) retrocl_postop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN, (void *) retrocl_postop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN, (void *) retrocl_postop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN, (void *) retrocl_postop_modrdn ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, RETROCL_PLUGIN_NAME, "retrocl_internalpostop_init failed\n" );
		rc= -1;
	}

	return rc;
}

/*
 * Function: retrocl_rootdse_init
 *
 * Returns: LDAP_SUCCESS
 * 
 * Arguments: none
 *
 * Description:   The FE DSE *must* be initialised before we get here.
 *
 */
static int retrocl_rootdse_init(void)
{

    int return_value= LDAP_SUCCESS;

    slapi_config_register_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,"",
				   LDAP_SCOPE_BASE,"(objectclass=*)",
				   retrocl_rootdse_search,NULL); 
    return return_value;
}

/*
 * Function: retrocl_select_backend
 *
 * Returns: LDAP_
 * 
 * Arguments: none
 *
 * Description: simulates an add of the changelog to see if it exists.  If not,
 * creates it.  Then reads the changenumbers.  This function should be called
 * exactly once at startup.
 *
 */

static int retrocl_select_backend(void) 
{
    int err;
    Slapi_PBlock *pb;
    Slapi_Backend *be = NULL;
    Slapi_Entry *referral = NULL;
    Slapi_Operation *op = NULL;
    char errbuf[BUFSIZ];

    pb = slapi_pblock_new();

    slapi_pblock_set (pb, SLAPI_PLUGIN_IDENTITY, g_plg_identity[PLUGIN_RETROCL]);

    /* This is a simulated operation; no actual add is performed */
    op = operation_new(OP_FLAG_INTERNAL);
    operation_set_type(op,SLAPI_OPERATION_ADD);  /* Ensure be not readonly */

    operation_set_target_spec_str(op,RETROCL_CHANGELOG_DN);
    
    slapi_pblock_set(pb,SLAPI_OPERATION, op);

    err = slapi_mapping_tree_select(pb,&be,&referral,errbuf);
	slapi_entry_free(referral);

    operation_free(&op,NULL);

    if (err != LDAP_SUCCESS || be == NULL || be == defbackend_get_backend()) {
        LDAPDebug2Args(LDAP_DEBUG_TRACE,"Mapping tree select failed (%d) %s.\n",
		  err,errbuf);
	
	/* could not find the backend for cn=changelog, either because
	 * it doesn't exist
	 * mapping tree not registered.
	 */
	err = retrocl_create_config();

	if (err != LDAP_SUCCESS) return err;
    } else {
      retrocl_be_changelog = be;
    }

    retrocl_create_cle();

    return retrocl_get_changenumbers();
}

/*
 * Function: retrocl_get_config_str
 *
 * Returns: malloc'ed string which must be freed.
 * 
 * Arguments: attribute type name
 *
 * Description:  reads a single-valued string attr from the plugins' own DSE.
 * This is called twice: to obtain the trim max age during startup, and to
 * obtain the change log directory.  No callback is registered; you cannot 
 * change the trim max age without restarting the server.
 *
 */

char *retrocl_get_config_str(const char *attrt)
{
    Slapi_Entry **entries;
    Slapi_PBlock *pb = NULL;
    char *ma;
    int rc = 0;
    char *dn;
    
    dn = RETROCL_PLUGIN_DN;
    
    pb = slapi_pblock_new();

    slapi_search_internal_set_pb (pb, dn, LDAP_SCOPE_BASE, "objectclass=*", NULL, 0, NULL,
				  NULL, g_plg_identity[PLUGIN_RETROCL] , 0);
    slapi_search_internal_pb (pb);	
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (rc != 0) {
      slapi_pblock_destroy(pb);
      return NULL;
    }
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    
    ma = slapi_entry_attr_get_charptr(entries[0],attrt);
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
    
    return ma;
}

/*
 * Function: retrocl_start
 *
 * Returns: 0 on success
 * 
 * Arguments: Pb
 *
 * Description: 
 *
 */

static int retrocl_start (Slapi_PBlock *pb)
{
    static int retrocl_started = 0;
    int rc = 0;

    if (!retrocl_started) {
      retrocl_rootdse_init();

      rc = retrocl_select_backend();      

      if (rc == 0) {
	retrocl_init_trimming();
      } else {
	LDAPDebug1Arg(LDAP_DEBUG_TRACE,"Couldnt find backend, not trimming retro changelog (%d).\n",rc);
      }
    }
   
    retrocl_started = 1;
    return rc;
}

/*
 * Function: retrocl_stop
 *
 * Returns: 0
 * 
 * Arguments: Pb
 *
 * Description: called when the server is shutting down
 *
 */

static int retrocl_stop (Slapi_PBlock *pb)
{
  int rc = 0;

  retrocl_stop_trimming();  
  retrocl_be_changelog = NULL;
  retrocl_forget_changenumbers();

  return rc;
}

/*
 * Function: retrocl_plugin_init
 *
 * Returns: 0 on successs
 * 
 * Arguments: Pb
 *
 * Description: main entry point for retrocl
 *
 */

int
retrocl_plugin_init(Slapi_PBlock *pb)
{
  	static int legacy_initialised= 0;
    	int rc = 0;
	void *identity = NULL;

	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);
	g_plg_identity[PLUGIN_RETROCL] = identity;
    
	if (!legacy_initialised) {
	  rc= slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 );
	  rc= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&retrocldesc );
	  rc= slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN, (void *) retrocl_start );
	  rc= slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN, (void *) retrocl_stop );
	  
	  rc= slapi_register_plugin("postoperation", 1 /* Enabled */, "retrocl_postop_init", retrocl_postop_init, "Retrocl postoperation plugin", NULL, identity);
	  rc= slapi_register_plugin("internalpostoperation", 1 /* Enabled */, "retrocl_internalpostop_init", retrocl_internalpostop_init, "Retrocl internal postoperation plugin", NULL, identity);
	}
	
    legacy_initialised = 1;
    return rc;
}



