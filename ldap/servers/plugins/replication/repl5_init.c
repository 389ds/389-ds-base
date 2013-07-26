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
 repl5_init.c - plugin initialization functions 
*/

/*
 * Add an entry like the following to dse.ldif to enable this plugin:

dn: cn=Multi-Master Replication Plugin,cn=plugins,cn=config
objectclass: top
objectclass: nsSlapdPlugin
objectclass: extensibleObject
cn: Legacy Replication Plugin
nsslapd-pluginpath: /export2/servers/Hydra-supplier/lib/replication-plugin.so
nsslapd-plugininitfunc: replication_multimaster_plugin_init
nsslapd-plugintype: object
nsslapd-pluginenabled: on
nsslapd-plugin-depends-on-type: database
nsslapd-plugin-depends-on-named: Class of Service
nsslapd-pluginid: replication-multimaster
nsslapd-pluginversion: 5.0b1
nsslapd-pluginvendor: Netscape Communications
nsslapd-plugindescription: Multi-Master Replication Plugin

*/
 
#include "slapi-plugin.h"
#include "repl.h"
#include "repl5.h"
#include "cl5.h"			 /* changelog interface */

#include "plstr.h"

/* #ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif*/

#define NSDS_REPL_NAME_PREFIX	"Netscape Replication"

static char *start_oid_list[] = {
		REPL_START_NSDS50_REPLICATION_REQUEST_OID,
		REPL_START_NSDS90_REPLICATION_REQUEST_OID,
		NULL
};
static char *start_name_list[] = {
		NSDS_REPL_NAME_PREFIX " Start Session",
		NULL
};
static char *end_oid_list[] = {
		REPL_END_NSDS50_REPLICATION_REQUEST_OID,
		NULL
};
static char *end_name_list[] = {
		NSDS_REPL_NAME_PREFIX " End Session",
		NULL
};
static char *total_oid_list[] = {
		REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID,
		REPL_NSDS71_REPLICATION_ENTRY_REQUEST_OID,
		NULL
};
static char *total_name_list[] = {
		NSDS_REPL_NAME_PREFIX " Total Update Entry",
		NULL
};
static char *response_oid_list[] = {
		REPL_NSDS50_REPLICATION_RESPONSE_OID,
		NULL
};
static char *response_name_list[] = {
		NSDS_REPL_NAME_PREFIX " Response",
		NULL
};
static char *cleanruv_oid_list[] = {
		REPL_CLEANRUV_OID,
		NULL
};
static char *cleanruv_name_list[] = {
		NSDS_REPL_NAME_PREFIX " CleanAllRUV",
		NULL
};
static char *cleanruv_abort_oid_list[] = {
		REPL_ABORT_CLEANRUV_OID,
		NULL
};
static char *cleanruv_abort_name_list[] = {
		NSDS_REPL_NAME_PREFIX " CleanAllRUV Abort",
		NULL
};
static char *cleanruv_maxcsn_oid_list[] = {
               REPL_CLEANRUV_GET_MAXCSN_OID,
               NULL
};
static char *cleanruv_maxcsn_name_list[] = {
               NSDS_REPL_NAME_PREFIX " CleanAllRUV Retrieve MaxCSN",
               NULL
};
static char *cleanruv_status_oid_list[] = {
               REPL_CLEANRUV_CHECK_STATUS_OID,
               NULL
};
static char *cleanruv_status_name_list[] = {
               NSDS_REPL_NAME_PREFIX " CleanAllRUV Check Status",
               NULL
};


/* List of plugin identities for every plugin registered. Plugin identity
   is passed by the server in the plugin init function and must be supplied 
   by the plugin to all internal operations it initiates
 */

/* ----------------------------- Multi-Master Replication Plugin */

static Slapi_PluginDesc multimasterdesc = {"replication-multimaster", VENDOR, DS_PACKAGE_VERSION, "Multi-master Replication Plugin"};
static Slapi_PluginDesc multimasterpreopdesc = {"replication-multimaster-preop", VENDOR, DS_PACKAGE_VERSION, "Multi-master replication pre-operation plugin"};
static Slapi_PluginDesc multimasterpostopdesc = {"replication-multimaster-postop", VENDOR, DS_PACKAGE_VERSION, "Multi-master replication post-operation plugin"};
static Slapi_PluginDesc multimasterinternalpreopdesc = {"replication-multimaster-internalpreop", VENDOR, DS_PACKAGE_VERSION, "Multi-master replication internal pre-operation plugin"};
static Slapi_PluginDesc multimasterinternalpostopdesc = {"replication-multimaster-internalpostop", VENDOR, DS_PACKAGE_VERSION, "Multimaster replication internal post-operation plugin"};
static Slapi_PluginDesc multimasterbepreopdesc = {"replication-multimaster-bepreop", VENDOR, DS_PACKAGE_VERSION, "Multimaster replication bepre-operation plugin"};
static Slapi_PluginDesc multimasterbepostopdesc = {"replication-multimaster-bepostop", VENDOR, DS_PACKAGE_VERSION, "Multimaster replication bepost-operation plugin"};
static Slapi_PluginDesc multimasterbetxnpostopdesc = {"replication-multimaster-betxnpostop", VENDOR, DS_PACKAGE_VERSION, "Multimaster replication be transaction post-operation plugin"};
static Slapi_PluginDesc multimasterextopdesc = { "replication-multimaster-extop", VENDOR, DS_PACKAGE_VERSION, "Multimaster replication extended-operation plugin" };

static int multimaster_stopped_flag; /* A flag which is set when all the plugin threads are to stop */
static int multimaster_started_flag = 0;

/* Thread private data and interface */
static PRUintn thread_private_agmtname;	/* thread private index for logging*/
static PRUintn thread_private_cache;

char*
get_thread_private_agmtname()
{
	char *agmtname = NULL;
	if (thread_private_agmtname)
		agmtname = PR_GetThreadPrivate(thread_private_agmtname);
	return (agmtname ? agmtname : "");
}

void
set_thread_private_agmtname(const char *agmtname)
{
	if (thread_private_agmtname)
		PR_SetThreadPrivate(thread_private_agmtname, (void *)agmtname);
}

void*
get_thread_private_cache ()
{
	void *buf = NULL;
	if ( thread_private_cache )
		buf = PR_GetThreadPrivate ( thread_private_cache );
	return buf;
}

void
set_thread_private_cache ( void *buf )
{
	if ( thread_private_cache )
		PR_SetThreadPrivate ( thread_private_cache, buf );
}

char*
get_repl_session_id (Slapi_PBlock *pb, char *idstr, CSN **csn)
{
	int opid=-1;
	PRUint64 connid = 0;
	CSN *opcsn;
	char opcsnstr[CSN_STRSIZE];

	*idstr = '\0';
	opcsn = NULL;
	opcsnstr[0] = '\0';

	if (pb) {
		Slapi_Operation *op;
		slapi_pblock_get (pb, SLAPI_OPERATION_ID, &opid);
		/* Avoid "Connection is NULL and hence cannot access SLAPI_CONN_ID" */
		if (opid) {
			slapi_pblock_get (pb, SLAPI_CONN_ID, &connid);
			PR_snprintf (idstr, REPL_SESSION_ID_SIZE, "conn=%" NSPRIu64 " op=%d",
					(long long unsigned int)connid, opid);
		}

		slapi_pblock_get ( pb, SLAPI_OPERATION, &op );
		opcsn = operation_get_csn (op);
		if (opcsn) {
			csn_as_string (opcsn, PR_FALSE, opcsnstr);
			PL_strcatn (idstr, REPL_SESSION_ID_SIZE, " csn=");
			PL_strcatn (idstr, REPL_SESSION_ID_SIZE, opcsnstr);
		}
	}
	if (csn) {
		*csn = opcsn;
	}
	return idstr;
}


/* preop acquires csn generator handle */
int repl5_is_betxn = 0;
int
multimaster_preop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
	
	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterpreopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_BIND_FN, (void *) multimaster_preop_bind ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_ADD_FN, (void *) multimaster_preop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_DELETE_FN, (void *) multimaster_preop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_MODIFY_FN, (void *) multimaster_preop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_MODRDN_FN, (void *) multimaster_preop_modrdn ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_SEARCH_FN, (void *) multimaster_preop_search ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_PRE_COMPARE_FN, (void *) multimaster_preop_compare ) != 0)
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_preop_init failed\n" );
		rc= -1;
	}
	return rc;
}

/* process_postop (core op of post op) frees CSN, 
 * which should be called after betxn is finieshed. */
int
multimaster_postop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterpostopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_BIND_FN, (void *) multimaster_postop_bind ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_ADD_FN, (void *) multimaster_postop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_DELETE_FN, (void *) multimaster_postop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODIFY_FN, (void *) multimaster_postop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_POST_MODRDN_FN, (void *) multimaster_postop_modrdn ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_postop_init failed\n" );
		rc= -1;
	}

	return rc;
}

int
multimaster_internalpreop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
	
	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterinternalpreopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN, (void *) multimaster_preop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN, (void *) multimaster_preop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN, (void *) multimaster_preop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN, (void *) multimaster_preop_modrdn ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_internalpreop_init failed\n" );
		rc= -1;
	}
	return rc;
}

int
multimaster_internalpostop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterinternalpostopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_ADD_FN, (void *) multimaster_postop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN, (void *) multimaster_postop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN, (void *) multimaster_postop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN, (void *) multimaster_postop_modrdn ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_internalpostop_init failed\n" );
		rc= -1;
	}

	return rc;
}

/* 
 * bepreop: setting SLAPI_TXN_RUV_MODS_FN, cleanup old stateinfo.
 * If betxn is off, preop urp is called here, too.
 */
int
multimaster_bepreop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
	
	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,	SLAPI_PLUGIN_VERSION_01 ) != 0 || 
	    slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterbepreopdesc ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_BE_PRE_ADD_FN, (void *) multimaster_bepreop_add ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_BE_PRE_DELETE_FN, (void *) multimaster_bepreop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_BE_PRE_MODIFY_FN, (void *) multimaster_bepreop_modify ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_BE_PRE_MODRDN_FN, (void *) multimaster_bepreop_modrdn ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_BE_PRE_CLOSE_FN, (void *) cl5Close ) != 0 ||
	    slapi_pblock_set( pb, SLAPI_PLUGIN_BE_PRE_BACKUP_FN, (void *) cl5WriteRUV ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_bepreop_init failed\n" );
		rc= -1;
	}

	return rc;
}

/* 
 * betxnpreop: if betxn is on, call preop urp at betxnpreop.
 */
int
multimaster_betxnpreop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
	
	return rc;
}

/* 
 * This bepostop_init is registered only if plugintype is NOT betxn.
 * if plugintype is betxn, callbacks are set in each multimaster_betxnpostop
 * function.
 */
int
multimaster_bepostop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
  
	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 || 
		slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterbepostopdesc ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_BE_POST_MODRDN_FN, (void *) multimaster_bepostop_modrdn ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_BE_POST_DELETE_FN, (void *) multimaster_bepostop_delete ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_BE_POST_OPEN_FN, (void *) changelog5_init ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_BE_POST_BACKUP_FN, (void *) cl5DeleteRUV ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_bepostop_init failed\n" );
		rc= -1;
	}

	return rc;
}

/* 
 * This betxn_bepostop_init is registered only if plugintype is betxn.
 * Note: other callbacks (add/delete/modify/modrdn) are set in each 
 * multimaster_betxnpostop function.
 */
int
multimaster_betxn_bepostop_init( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */
  
	if( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) || 
		slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterbepostopdesc ) ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_BE_POST_OPEN_FN, (void *) changelog5_init ) ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_BE_POST_BACKUP_FN, (void *) cl5DeleteRUV ) )
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_betxn_bepostop_init failed\n" );
		rc= -1;
	}

	return rc;
}

int
multimaster_betxnpostop_init( Slapi_PBlock *pb )
{
	int rc = 0; /* OK */
	void *add_fn;
	void *del_fn;
	void *mod_fn;
	void *mdn_fn;

	if (repl5_is_betxn) {
		add_fn = multimaster_be_betxnpostop_add;
		del_fn = multimaster_be_betxnpostop_delete;
		mod_fn = multimaster_be_betxnpostop_modify;
		mdn_fn = multimaster_be_betxnpostop_modrdn;
	} else {
		add_fn = multimaster_betxnpostop_add;
		del_fn = multimaster_betxnpostop_delete;
		mod_fn = multimaster_betxnpostop_modify;
		mdn_fn = multimaster_betxnpostop_modrdn;
	}

	if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01) || 
	    slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION, 
	                     (void *)&multimasterbetxnpostopdesc) ||
	    slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_POST_ADD_FN, add_fn) ||
	    slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN, del_fn) ||
	    slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN, mdn_fn) ||
	    slapi_pblock_set(pb, SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN, mod_fn)) {
		slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name,
		                "multimaster_betxnpostop_init failed\n");
		rc = -1;
	}

	return rc;
}

int
multimaster_start_extop_init( Slapi_PBlock *pb )
{
	int rc= 0; /* OK */
	
    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 || 
		 slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterextopdesc ) != 0 ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, (void *)start_oid_list ) != 0  ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, (void *)start_name_list ) != 0  ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)multimaster_extop_StartNSDS50ReplicationRequest ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_start_extop_init (StartNSDS50ReplicationRequest) failed\n" );
		rc= -1;
	}


    return rc;
}


int
multimaster_end_extop_init( Slapi_PBlock *pb )
{
	int rc= 0; /* OK */
	

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 || 
		 slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterextopdesc ) != 0 ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, (void *)end_oid_list ) != 0  ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, (void *)end_name_list ) != 0  ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)multimaster_extop_EndNSDS50ReplicationRequest ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_end_extop_init (EndNSDS50ReplicationRequest) failed\n" );
		rc= -1;
	}

    return rc;
}

int
multimaster_cleanruv_maxcsn_extop_init( Slapi_PBlock *pb )
{
	int rc= 0; /* OK */
	void *identity = NULL;

	/* get plugin identity and store it to pass to internal operations */
	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);

	if (slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 ||
			  slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterextopdesc ) != 0 ||
			  slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, (void *)cleanruv_maxcsn_oid_list ) != 0  ||
			  slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, (void *)cleanruv_maxcsn_name_list ) != 0  ||
			  slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)multimaster_extop_cleanruv_get_maxcsn ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_cleanruv_extop_init failed\n" );
		rc= -1;
	}

	return rc;
}

int
multimaster_cleanruv_status_extop_init( Slapi_PBlock *pb )
{
	int rc= 0; /* OK */
	void *identity = NULL;

	/* get plugin identity and store it to pass to internal operations */
	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);

	if (slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 ||
			  slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterextopdesc ) != 0 ||
			  slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, (void *)cleanruv_status_oid_list ) != 0  ||
			  slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, (void *)cleanruv_status_name_list ) != 0  ||
			  slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)multimaster_extop_cleanruv_check_status ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_cleanruv_extop_init failed\n" );
		rc= -1;
	}

	return rc;
}


int
multimaster_total_extop_init( Slapi_PBlock *pb )
{
	int rc= 0; /* OK */
	void *identity = NULL;

	/* get plugin identity and store it to pass to internal operations */
	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 || 
		 slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterextopdesc ) != 0 ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, (void *)total_oid_list ) != 0  ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, (void *)total_name_list ) != 0  ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)multimaster_extop_NSDS50ReplicationEntry ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_start_extop_init (NSDS50ReplicationEntry failed\n" );
		rc= -1;
	}

    return rc;
}

int
multimaster_response_extop_init( Slapi_PBlock *pb )
{
	int rc= 0; /* OK */
	void *identity = NULL;

	/* get plugin identity and store it to pass to internal operations */
	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);

    if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 || 
		 slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterextopdesc ) != 0 ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, (void *)response_oid_list ) != 0  ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, (void *)response_name_list ) != 0  ||
		 slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)extop_noop ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_start_extop_init (NSDS50ReplicationResponse failed\n" );
		rc= -1;
	}

    return rc;
}

int
multimaster_cleanruv_extop_init( Slapi_PBlock *pb )
{
	int rc= 0; /* OK */
	void *identity = NULL;

	/* get plugin identity and store it to pass to internal operations */
	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);

	if (slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterextopdesc ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, (void *)cleanruv_oid_list ) != 0  ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, (void *)cleanruv_name_list ) != 0  ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)multimaster_extop_cleanruv ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_cleanruv_extop_init failed\n" );
		rc= -1;
	}

	return rc;
}

int
multimaster_cleanruv_abort_extop_init( Slapi_PBlock *pb )
{
	int rc= 0; /* OK */
	void *identity = NULL;

	/* get plugin identity and store it to pass to internal operations */
	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);

	if (slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterextopdesc ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_OIDLIST, (void *)cleanruv_abort_oid_list ) != 0  ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_NAMELIST, (void *)cleanruv_abort_name_list ) != 0  ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_EXT_OP_FN, (void *)multimaster_extop_abort_cleanruv ))
	{
		slapi_log_error( SLAPI_LOG_PLUGIN, repl_plugin_name, "multimaster_cleanruv_abort_extop_init failed\n" );
		rc= -1;
	}

	return rc;
}

static PRBool
check_for_ldif_dump(Slapi_PBlock *pb)
{
	int i;
	int argc;
	char **argv;
	PRBool return_value = PR_FALSE;

	slapi_pblock_get( pb, SLAPI_ARGC, &argc);
	slapi_pblock_get( pb, SLAPI_ARGV, &argv);

	for (i = 1; i < argc && !return_value; i++)
	{
		if (strcmp(argv[i], "db2ldif") == 0)
		{
			return_value = PR_TRUE;
		}
	}
	return return_value;
}


static PRBool is_ldif_dump = PR_FALSE;

PRBool
ldif_dump_is_running()
{
    return is_ldif_dump;
}

int
multimaster_start( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

    if (!multimaster_started_flag)
	{
		/* Get any registered replication session API */
		repl_session_plugin_init();

		/* Initialize thread private data for logging. Ignore if fails */
		PR_NewThreadPrivateIndex (&thread_private_agmtname, NULL);
		PR_NewThreadPrivateIndex (&thread_private_cache, NULL);

		/* Decode the command line args to see if we're dumping to LDIF */
		is_ldif_dump = check_for_ldif_dump(pb);

		/* allow online replica configuration */
		rc = replica_config_init ();
		if (rc != 0)
			goto out;

		slapi_register_supported_control(REPL_NSDS50_UPDATE_INFO_CONTROL_OID,
			SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE |
			SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN);

		/* Stash away our partial URL, used in RUVs */
		rc = multimaster_set_local_purl();
		if (rc != 0)
			goto out;

		/* Initialise support for cn=monitor */
		rc = repl_monitor_init();
		if (rc != 0)
			goto out;

		/* initialize name hash */
		rc = replica_init_name_hash ();
		if (rc != 0)
			goto out;

		/* initialize dn hash */
		rc = replica_init_dn_hash ();
		if (rc != 0)
			goto out;

		/* create replicas */
		multimaster_mtnode_construct_replicas ();

        /* Initialise the 5.0 Changelog */
		rc = changelog5_init();
		if (rc != 0)
			goto out;

		/* Initialize the replication agreements, unless we're dumping LDIF */
		if (!is_ldif_dump)
		{
			rc = agmtlist_config_init();
			if (rc != 0)
				goto out;
		}

        /* check if the replica's data was reloaded offline and we need
           to reinitialize replica's changelog. This should be done
           after the changelog is initialized */

        replica_enumerate_replicas (replica_check_for_data_reload, NULL);

        /* register to be notified when backend state changes */
        slapi_register_backend_state_change((void *)multimaster_be_state_change, 
                                            multimaster_be_state_change);

        multimaster_started_flag = 1;
    	multimaster_stopped_flag = 0;
	}
out:
    return rc;
}

int
multimaster_stop( Slapi_PBlock *pb )
{
    int rc= 0; /* OK */

    if (!multimaster_stopped_flag)
    {
        if (!is_ldif_dump)
        {
            /* Shut down replication agreements */
            agmtlist_shutdown();
        }
        /* if we are cleaning a ruv, stop */
        stop_ruv_cleaning();
        /* unregister backend state change notification */
        slapi_unregister_backend_state_change((void *)multimaster_be_state_change);
        changelog5_cleanup(); /* Shut down the changelog */
        multimaster_mtnode_extension_destroy(); /* Destroy mapping tree node exts */
        replica_destroy_name_hash(); /* destroy the hash and its remaining content */
        replica_config_destroy (); /* Destroy replica config info */
        multimaster_stopped_flag = 1;
    }
    return rc;
}


PRBool
multimaster_started()
{
	return(multimaster_started_flag != 0);
}


/*
 * Initialize the multimaster replication plugin.
 */
int replication_multimaster_plugin_init(Slapi_PBlock *pb)
{
	static int multimaster_initialised= 0;
	int rc= 0; /* OK */
	void *identity = NULL;
	Slapi_Entry *plugin_entry = NULL;

	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &identity);
	PR_ASSERT (identity);
	repl_set_plugin_identity (PLUGIN_MULTIMASTER_REPLICATION, identity);

	/* need the repl plugin path for the chain on update function */
/*	slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &entry);
	PR_ASSERT(entry);
	path = slapi_entry_attr_get_charptr(entry, "nsslapd-pluginpath");
	repl_set_repl_plugin_path(path);
	slapi_ch_free_string(&path);
*/
	multimaster_mtnode_extension_init ();

	if ((slapi_pblock_get(pb, SLAPI_PLUGIN_CONFIG_ENTRY, &plugin_entry) == 0) &&
	    plugin_entry) {
		repl5_is_betxn = slapi_entry_attr_get_bool(plugin_entry,
		                                     "nsslapd-pluginbetxn");
	}

	if(!multimaster_initialised)
	{
		/* Initialize extensions */
		repl_con_init_ext();
		repl_sup_init_ext();

		rc= slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION, SLAPI_PLUGIN_VERSION_01 );
		rc= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&multimasterdesc );
		rc= slapi_pblock_set( pb, SLAPI_PLUGIN_START_FN, (void *) multimaster_start );
		rc= slapi_pblock_set( pb, SLAPI_PLUGIN_CLOSE_FN, (void *) multimaster_stop );
		
		/* Register the plugin interfaces we implement */
		/* preop acquires csn generator handle */
		rc = slapi_register_plugin("preoperation", 1 /* Enabled */,
		                "multimaster_preop_init",
		                multimaster_preop_init,
		                "Multimaster replication preoperation plugin",
		                NULL, identity);
		/* bepreop: setting SLAPI_TXN_RUV_MODS_FN and cleanup old stateinfo
		 * -- should be done before transaction */
		/* if betxn is off, urp is called at bepreop. */
		rc = slapi_register_plugin("bepreoperation", 1 /* Enabled */,
		                "multimaster_bepreop_init",
		                multimaster_bepreop_init,
		                "Multimaster replication bepreoperation plugin",
		                NULL, identity);
		/* is_betxn: be post ops (add/del/mod/mdn) are combined into betxn ops.
		 * no betxn: be post ops are regsitered at bepostoperation. */
		rc = slapi_register_plugin("betxnpostoperation", 1 /* Enabled */,
		                "multimaster_betxnpostop_init",
		                multimaster_betxnpostop_init,
		                "Multimaster replication betxnpostoperation plugin",
		                NULL, identity);
		if (repl5_is_betxn) {
		    /* if betxn is on, urp is called at betxnpreop. */
		    rc = slapi_register_plugin("betxnpreoperation", 1 /* Enabled */,
		                "multimaster_betxnpreop_init",
		                multimaster_betxnpreop_init,
		                "Multimaster replication betxnpreoperation plugin",
		                NULL, identity);
		    /* bepostop configures open and backup only (no betxn) */
		    rc = slapi_register_plugin("bepostoperation", 1 /* Enabled */,
		                "multimaster_betxn_bepostop_init",
		                multimaster_betxn_bepostop_init,
		                "Multimaster replication bepostoperation plugin",
		                NULL, identity);
		} else {
		    /* bepostop configures open and backup only as well as add/del/
			 * mod/mdn bepost ops */
		    rc = slapi_register_plugin("bepostoperation", 1 /* Enabled */,
		                "multimaster_bepostop_init",
		                multimaster_bepostop_init,
		                "Multimaster replication bepostoperation2 plugin",
		                NULL, identity);
		}
		/* process_postop (core op of post op) frees CSN, 
		 * which should wait until betxn is done. */
		rc = slapi_register_plugin("postoperation", 1 /* Enabled */,
		                "multimaster_postop_init",
		                multimaster_postop_init,
		                "Multimaster replication postoperation plugin",
		                NULL, identity);
		rc = slapi_register_plugin("internalpreoperation", 1 /* Enabled */,
		                "multimaster_internalpreop_init",
		                multimaster_internalpreop_init,
		                "Multimaster replication internal preoperation plugin",
		                NULL, identity);
		rc = slapi_register_plugin("internalpostoperation", 1 /* Enabled */,
		                "multimaster_internalpostop_init",
		                multimaster_internalpostop_init,
		                "Multimaster replication internal postoperation plugin",
		                NULL, identity);
		rc = slapi_register_plugin("extendedop", 1 /* Enabled */, "multimaster_start_extop_init", multimaster_start_extop_init, "Multimaster replication start extended operation plugin", NULL, identity);
		rc= slapi_register_plugin("extendedop", 1 /* Enabled */, "multimaster_end_extop_init", multimaster_end_extop_init, "Multimaster replication end extended operation plugin", NULL, identity);
		rc= slapi_register_plugin("extendedop", 1 /* Enabled */, "multimaster_total_extop_init", multimaster_total_extop_init, "Multimaster replication total update extended operation plugin", NULL, identity);
		rc= slapi_register_plugin("extendedop", 1 /* Enabled */, "multimaster_response_extop_init", multimaster_response_extop_init, "Multimaster replication extended response plugin", NULL, identity);
		rc= slapi_register_plugin("extendedop", 1 /* Enabled */, "multimaster_cleanruv_extop_init", multimaster_cleanruv_extop_init, "Multimaster replication cleanruv extended operation plugin", NULL, identity);
		rc= slapi_register_plugin("extendedop", 1 /* Enabled */, "multimaster_cleanruv_abort_extop_init", multimaster_cleanruv_abort_extop_init, "Multimaster replication cleanruv abort extended operation plugin", NULL, identity);
		rc= slapi_register_plugin("extendedop", 1 /* Enabled */, "multimaster_cleanruv_maxcsn_extop_init", multimaster_cleanruv_maxcsn_extop_init, "Multimaster replication cleanruv maxcsn extended operation plugin", NULL, identity);
		rc= slapi_register_plugin("extendedop", 1 /* Enabled */, "multimaster_cleanruv_status_extop_init", multimaster_cleanruv_status_extop_init, "Multimaster replication cleanruv status extended operation plugin", NULL, identity);
		if (0 == rc)
		{
			multimaster_initialised = 1;
		}
	}
	return rc;
}
