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
 * Add an entry like the following to dse.ldif to enable this plugin:

dn: cn=Legacy Replication Plugin,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: Legacy Replication Plugin
nsslapd-pluginpath: /export2/servers/Hydra-supplier/lib/replication-plugin.so
nsslapd-plugininitfunc: replication_legacy_plugin_init
nsslapd-plugintype: object
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
nsslapd-plugin-depends-on-named: Class of Service
nsslapd-plugin-depends-on-named: Multi-Master Replication Plugin
nsslapd-pluginid: replication-legacy
nsslapd-pluginversion: 5.0b1
nsslapd-pluginvendor: Netscape Communications
nsslapd-plugindescription: Legacy Replication Plugin

NOTE: This plugin depends on the Multi-Master Replication Plugin

*/
 
#include "slapi-plugin.h"
#include "repl.h"
#include "repl5.h"
#include "repl_shared.h"
#include "cl4.h"	/* changelog interface */
#include "dirver.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */

#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

/* ----------------------------- Legacy Replication Plugin */

static Slapi_PluginDesc legacydesc = { "replication-legacy", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Legacy Replication Plugin" };
static Slapi_PluginDesc legacypreopdesc = { "replication-legacy-preop", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Legacy replication pre-operation plugin" };
static Slapi_PluginDesc legacypostopdesc = { "replication-legacy-postop", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Legacy replication post-operation plugin" };
static Slapi_PluginDesc legacyinternalpreopdesc = { "replication-legacy-internalpreop", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Legacy replication internal pre-operation plugin" };
static Slapi_PluginDesc legacyinternalpostopdesc = { "replication-legacy-internalpostop", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Legacy replication internal post-operation plugin" };
static Slapi_PluginDesc legacyentrydesc = { "replication-legacy-entry", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT, "Legacy replication entry plugin" };

static int legacy_stopped; /* A flag which is set when all the plugin threads are to stop */        


/* Initialize preoperation plugin points */
int
legacy_preop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&legacypreopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN, (void *) legacy_preop_bind ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_ADD_FN, (void *) legacy_preop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_DELETE_FN, (void *) legacy_preop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_MODIFY_FN, (void *) legacy_preop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_MODRDN_FN, (void *) legacy_preop_modrdn ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_SEARCH_FN, (void *) legacy_preop_search ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_COMPARE_FN, (void *) legacy_preop_compare ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_ENTRY_FN, (void *) legacy_pre_entry ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "legacy_preop_init failed\n" );
		rc= -1;
	}
	return rc;
}



/* Initialize postoperation plugin points */
static int
legacy_postop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&legacypostopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_ADD_FN, (void *) legacy_postop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_DELETE_FN, (void *) legacy_postop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODIFY_FN, (void *) legacy_postop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODRDN_FN, (void *) legacy_postop_modrdn ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "legacy_postop_init failed\n" );
		rc= -1;
	}

	return rc;
}



/* Initialize internal preoperation plugin points (called for internal operations) */
static int
legacy_internalpreop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
	
	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&legacyinternalpreopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN, (void *) legacy_preop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN, (void *) legacy_preop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN, (void *) legacy_preop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN, (void *) legacy_preop_modrdn ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "legacy_internalpreop_init failed\n" );
		rc= -1;
	}
	return rc;
}



/* Initialize internal postoperation plugin points (called for internal operations) */
static int
legacy_internalpostop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&legacyinternalpostopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN, (void *) legacy_postop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN, (void *) legacy_postop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN, (void *) legacy_postop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN, (void *) legacy_postop_modrdn ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "legacy_internalpostop_init failed\n" );
		rc= -1;
	}

	return rc;
}



/* Initialize the entry plugin point for the legacy replication plugin */
static int
legacy_entry_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
	
	/* Set up the fn pointers for the preop and postop operations we're interested in */
	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&legacyentrydesc ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "legacy_entry_init failed\n" );
		rc= -1;
	}
	return rc;
}




/*
 * Create the entry at the top of the replication configuration subtree.
 */
static int
create_config_top()
{
	char *entry_string = slapi_ch_strdup("dn: cn=replication,cn=config\nobjectclass: top\nobjectclass: extensibleobject\ncn: replication\n");
	Slapi_PBlock *pb = slapi_pblock_new();
	Slapi_Entry *e = slapi_str2entry(entry_string, 0);
	int return_value;

	slapi_add_entry_internal_set_pb(pb, e, NULL, /* controls */
		repl_get_plugin_identity(PLUGIN_MULTIMASTER_REPLICATION), 0 /* flags */);
	slapi_add_internal_pb(pb);
	slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &return_value);
	slapi_pblock_destroy(pb);
	slapi_ch_free((void **)&entry_string);
	return return_value;
}


/* Start the legacy replication plugin */
static int 
legacy_start( Slapi_PBlock *pb )
{
	static int legacy_started = 0;
    int rc= 0; /* OK */

    if (!legacy_started)
	{
		int ctrc;

		/* Initialise support for cn=monitor */
		repl_monitor_init();

		/* Initialise support for "" (the rootdse) */
		/* repl_rootdse_init(); */

		/* Decode the command line args to see if we're dumping to LDIF */
		{
			int argc;
			char **argv;
			slapi_pblock_get( pb, SLAPI_ARGC, &argc);
			slapi_pblock_get( pb, SLAPI_ARGV, &argv);
			repl_entry_init(argc,argv);
		}

		/* Create the entry at the top of the config area, if it doesn't exist */
		/* XXXggood this should be in the 5.0 plugin! */
		ctrc = create_config_top();
		if (ctrc != LDAP_SUCCESS && ctrc != LDAP_ALREADY_EXISTS)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Warning: unable to "
				"create configuration entry %s: %s\n", REPL_CONFIG_TOP,
				ldap_err2string(ctrc));
		}
		(void)legacy_consumer_config_init();

        /* register to be notified when backend state changes */
        slapi_register_backend_state_change((void *)legacy_consumer_be_state_change, 
                                            legacy_consumer_be_state_change);

		legacy_started = 1;
		legacy_stopped = 0;
	}
    return rc;
}


/* Post-start function for the legacy replication plugin */
static int
legacy_poststart( Slapi_PBlock *pb )
{
    int rc = 0; /* OK */
    return rc;
}


/* Stop the legacy replication plugin */
static int
legacy_stop( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

    if (!legacy_stopped)
	{
        /*csnShutdown();*/
    	legacy_stopped = 1;
	}
	 
    /* unregister backend state change notification */
    slapi_unregister_backend_state_change((void *)legacy_consumer_be_state_change);
	
    return rc;
}


/* Initialize the legacy replication plugin */
int
replication_legacy_plugin_init(Slapi_PBlock *pb)
{
    static int legacy_initialised= 0;
    int rc= 0; /* OK */
	void *identity = NULL;

	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);
	repl_set_plugin_identity (PLUGIN_LEGACY_REPLICATION, identity);

	if(config_is_slapd_lite())
	{
		slapi_log_error( SLAPI_LOG_FATAL, repl_plugin_name,
				"replication plugin not approved for restricted"
				" mode Directory Server.\n" );
	    rc= -1;
	}
	if(rc==0 && !legacy_initialised)
	{
	    rc= slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 );
	    rc= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&legacydesc );
	    rc= slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN, (void *) legacy_start );
	    rc= slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN, (void *) legacy_stop );
	    rc= slapi_pblock_set( pb, SLAPI_PLUGIN_POSTSTART_FN, (void *) legacy_poststart );
		
		/* Register the plugin interfaces we implement */
        rc= slapi_register_plugin("preoperation", 1 /* Enabled */, "legacy_preop_init", legacy_preop_init, "Legacy replication preoperation plugin", NULL, identity);
        rc= slapi_register_plugin("postoperation", 1 /* Enabled */, "legacy_postop_init", legacy_postop_init, "Legacy replication postoperation plugin", NULL, identity);
        rc= slapi_register_plugin("internalpreoperation", 1 /* Enabled */, "legacy_internalpreop_init", legacy_internalpreop_init, "Legacy replication internal preoperation plugin", NULL,  identity);
        rc= slapi_register_plugin("internalpostoperation", 1 /* Enabled */, "legacy_internalpostop_init", legacy_internalpostop_init, "Legacy replication internal postoperation plugin", NULL,  identity);		
		rc= slapi_register_plugin("entry", 1 /* Enabled */, "legacy_entry_init", legacy_entry_init, "Legacy replication entry plugin", NULL, identity);
		
		legacy_initialised= 1;
	}
	return rc;
}


int
get_legacy_stop()
{
    return legacy_stopped;
}
