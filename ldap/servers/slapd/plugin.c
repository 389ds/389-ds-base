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

/* plugin.c - routines for setting up and calling plugins */

#include <stddef.h>
#include <stdio.h>
#include <plhash.h>
#include "slap.h"

/* this defines are used for plugin configuration */
#define LOCAL_DATA			"local data"
#define REMOTE_DATA			"remote data"
#define ALL_DATA			"*"
#define ROOT_BIND			"directory manager"
#define ANONYMOUS_BIND		"anonymous"

/* Forward Declarations */
static int plugin_call_list (struct slapdplugin *list, int operation, Slapi_PBlock *pb);
static int plugin_call_one (struct slapdplugin *list, int operation, Slapi_PBlock *pb);
static int plugin_call_func (struct slapdplugin *list, int operation, Slapi_PBlock *pb, int call_one);

static PRBool plugin_invoke_plugin_pb (struct slapdplugin *plugin, int operation, Slapi_PBlock *pb);
static PRBool plugin_matches_operation (Slapi_DN *target_spec, PluginTargetData *ptd, 
										PRBool bindop, PRBool isroot, PRBool islocal, int method);

static void plugin_config_init (struct pluginconfig *config);
static void plugin_config_cleanup (struct pluginconfig *config);
static int plugin_config_set_action (int *action, char *value);
static struct pluginconfig* plugin_get_config (struct slapdplugin *plugin);
static void default_plugin_init();
static void ptd_init (PluginTargetData *ptd);
static void ptd_cleanup (PluginTargetData *ptd);
static void ptd_add_subtree (PluginTargetData *ptd, Slapi_DN *subtree);
static void ptd_set_special_data (PluginTargetData *ptd, int type);
static Slapi_DN *ptd_get_first_subtree (const PluginTargetData *ptd, int *cookie);
static Slapi_DN *ptd_get_next_subtree (const PluginTargetData *ptd, int *cookie);
static PRBool ptd_is_special_data_set (const PluginTargetData *ptd, int type);
int ptd_get_subtree_count (const PluginTargetData *ptd);
static void plugin_set_global (PluginTargetData *ptd);
static PRBool plugin_is_global (const PluginTargetData *ptd);
static void plugin_set_default_access (struct pluginconfig *config);

static PLHashTable *global_plugin_dns = NULL;

/* The global plugin list is indexed by the PLUGIN_LIST_* constants defined in slap.h */
static struct slapdplugin *global_plugin_list[PLUGIN_LIST_GLOBAL_MAX];

/* plugin structure used to configure internal operation issued by the core server */
static int global_server_plg_initialised= 0;
struct slapdplugin global_server_plg;

/* plugin structure used to configure internal operation issued by the core server */
static int global_server_plg_id_initialised= 0;
struct slapi_componentid global_server_id_plg;

/* plugin structure used to configure operations issued by the old plugins that
   do not pass their identity in the operation */
static struct slapdplugin global_default_plg;

/* Enable/disable plugin callbacks for clean startup */
static int global_plugin_callbacks_enabled = 0;


static void
add_plugin_to_list(struct slapdplugin **list, struct slapdplugin *plugin)
{
	struct slapdplugin **tmp;
	struct slapdplugin *last = NULL;
	int plugin_added = 0;

	/* Insert the plugin into list based off of precedence. */
	for ( tmp = list; *tmp; tmp = &(*tmp)->plg_next )
	{
		if (plugin->plg_precedence < (*tmp)->plg_precedence)
		{
			if (last)
			{
				/* Insert item between last and tmp. */
				plugin->plg_next = *tmp;
				last->plg_next = plugin;
			} else {
				/* Add as the first list item. */
				plugin->plg_next = *tmp;
				*list = plugin;
			}

			plugin_added = 1;

			/* We've added the plug-in to the
 			 * list, so bail from the loop. */
			break;
		}

		/* Save a pointer to this plugin so we can
		 * refer to it on the next loop iteration. */
		last = *tmp;
	}

	/* If we didn't add the plug-in to the list yet,
	 * it needs to be added to the end of the list. */
	if (!plugin_added)
	{
		*tmp = plugin;
	}
}

struct slapdplugin *
get_plugin_list(int plugin_list_index)
{
	return global_plugin_list[plugin_list_index];
}

/*
 * As the plugin configuration information is read an array of
 * entries is built which reflect the plugins.  The entries
 * are added after the syntax plugins are started so that the
 * nodes in the attribute tree are initialised correctly.
 */
typedef struct entry_and_plugin {
	Slapi_Entry *e;
	struct slapdplugin *plugin;
	struct entry_and_plugin *next;
} entry_and_plugin_t;

static entry_and_plugin_t *dep_plugin_entries = NULL; /* for dependencies */

#if 0
static entry_and_plugin_t *plugin_entries = NULL;

static void
add_plugin_entries()
{
	entry_and_plugin_t *ep = plugin_entries;
	entry_and_plugin_t *deleteep = 0;
	while (ep)
	{
		int plugin_actions = 0;
        Slapi_PBlock newpb;
		pblock_init(&newpb);
		slapi_add_entry_internal_set_pb(&newpb, ep->e, NULL,
										ep->plugin, plugin_actions);
		slapi_pblock_set(&newpb, SLAPI_TARGET_DN, (void*)slapi_entry_get_dn_const(ep->e));
		slapi_pblock_set(&newpb, SLAPI_TARGET_SDN, (void*)slapi_entry_get_sdn_const(ep->e));
		slapi_add_internal_pb(&newpb);
		deleteep = ep;
		ep = ep->next;
		slapi_ch_free((void**)&deleteep);
		pblock_done(&newpb);
    }

	plugin_entries = NULL;
}
#endif

static void
new_plugin_entry(entry_and_plugin_t **ep, Slapi_Entry *e, struct slapdplugin *plugin)
{
	entry_and_plugin_t *oldep = 0;
	entry_and_plugin_t *iterep = *ep;

	entry_and_plugin_t *newep =
		(entry_and_plugin_t*)slapi_ch_calloc(1,sizeof(entry_and_plugin_t));
	newep->e = e;
	newep->plugin = plugin;
	
	while(iterep)
	{
		oldep = iterep;
		iterep = iterep->next;
	}

	newep->next = 0;

	if(oldep)
		oldep->next = newep;
	else
		*ep = newep;
}
	

static void
add_plugin_entry_dn(const Slapi_DN *plugin_dn)
{
	if (!global_plugin_dns)
	{
		global_plugin_dns = PL_NewHashTable(20, PL_HashString,
											PL_CompareStrings,
											PL_CompareValues, 0, 0);
	}

	PL_HashTableAdd(global_plugin_dns,
					slapi_sdn_get_ndn(plugin_dn),
					(void*)plugin_dn);
}
	
#define SLAPI_PLUGIN_NONE_IF_NULL( s )	((s) == NULL ? "none" : (s))

/*
 * Allows a plugin to register a plugin.
 * This was added so that 'object' plugins could register all
 * the plugin interfaces that it supports.
 */
int
slapi_register_plugin(
	const char *plugintype,
	int enabled,
	const char *initsymbol,
	slapi_plugin_init_fnptr initfunc,
	const char *name,
	char **argv,
	void *group_identity
)
{
	return slapi_register_plugin_ext(plugintype, enabled, initsymbol,
			initfunc, name, argv, group_identity, PLUGIN_DEFAULT_PRECEDENCE);
}

int
slapi_register_plugin_ext(
	const char *plugintype,
	int enabled,
	const char *initsymbol,
	slapi_plugin_init_fnptr initfunc,
	const char *name, 
	char **argv,
	void *group_identity,
	int precedence
)
{
	int ii = 0;
    int rc = 0;
	Slapi_Entry *e = NULL;
	char *dn = slapi_ch_smprintf("cn=%s,%s", name, PLUGIN_BASE_DN);
	Slapi_DN *sdn = slapi_sdn_new_normdn_passin(dn);

	e = slapi_entry_alloc();
	/* this function consumes dn */
	slapi_entry_init_ext(e, sdn, NULL);
	slapi_sdn_free(&sdn);

	slapi_entry_attr_set_charptr(e, "cn", name);
	slapi_entry_attr_set_charptr(e, ATTR_PLUGIN_TYPE, plugintype);
	if (!enabled)
		slapi_entry_attr_set_charptr(e, ATTR_PLUGIN_ENABLED, "off");

	slapi_entry_attr_set_charptr(e, ATTR_PLUGIN_INITFN, initsymbol);
	slapi_entry_attr_set_int(e, ATTR_PLUGIN_PRECEDENCE, precedence);

	for (ii = 0; argv && argv[ii]; ++ii) {
		char argname[64];
		PR_snprintf(argname, sizeof(argname), "%s%d", ATTR_PLUGIN_ARG, ii);
		slapi_entry_attr_set_charptr(e, argname, argv[ii]);
	}

	/* plugin_setup copies the given entry */
	plugin_setup(e, group_identity, initfunc, 0);
	slapi_entry_free(e);

	return rc;
}

int
plugin_call_plugins( Slapi_PBlock *pb, int whichfunction )
{
    int plugin_list_number= -1;
	int	rc= 0;
	int do_op = global_plugin_callbacks_enabled;

	if ( pb == NULL )
	{
		return( 0 );
	}
	
	switch ( whichfunction ) {
	case SLAPI_PLUGIN_PRE_BIND_FN:
	case SLAPI_PLUGIN_PRE_UNBIND_FN:
	case SLAPI_PLUGIN_PRE_SEARCH_FN:
	case SLAPI_PLUGIN_PRE_COMPARE_FN:
	case SLAPI_PLUGIN_PRE_MODIFY_FN:
	case SLAPI_PLUGIN_PRE_MODRDN_FN:
	case SLAPI_PLUGIN_PRE_ADD_FN:
	case SLAPI_PLUGIN_PRE_DELETE_FN:
	case SLAPI_PLUGIN_PRE_ABANDON_FN:
	case SLAPI_PLUGIN_PRE_ENTRY_FN:
	case SLAPI_PLUGIN_PRE_REFERRAL_FN:
	case SLAPI_PLUGIN_PRE_RESULT_FN:
        plugin_list_number= PLUGIN_LIST_PREOPERATION;
		break;
	case SLAPI_PLUGIN_POST_BIND_FN:
	case SLAPI_PLUGIN_POST_UNBIND_FN:
	case SLAPI_PLUGIN_POST_SEARCH_FN:
	case SLAPI_PLUGIN_POST_SEARCH_FAIL_FN:
	case SLAPI_PLUGIN_POST_COMPARE_FN:
	case SLAPI_PLUGIN_POST_MODIFY_FN:
	case SLAPI_PLUGIN_POST_MODRDN_FN:
	case SLAPI_PLUGIN_POST_ADD_FN:
	case SLAPI_PLUGIN_POST_DELETE_FN:
	case SLAPI_PLUGIN_POST_ABANDON_FN:
	case SLAPI_PLUGIN_POST_ENTRY_FN:
	case SLAPI_PLUGIN_POST_REFERRAL_FN:
	case SLAPI_PLUGIN_POST_RESULT_FN:
        plugin_list_number= PLUGIN_LIST_POSTOPERATION;
		break;
	case SLAPI_PLUGIN_BE_PRE_MODIFY_FN:
	case SLAPI_PLUGIN_BE_PRE_MODRDN_FN:
	case SLAPI_PLUGIN_BE_PRE_ADD_FN:
	case SLAPI_PLUGIN_BE_PRE_DELETE_FN:
	case SLAPI_PLUGIN_BE_PRE_CLOSE_FN:
	case SLAPI_PLUGIN_BE_PRE_BACKUP_FN:
        plugin_list_number= PLUGIN_LIST_BEPREOPERATION;
		do_op = 1; /* always allow backend callbacks (even during startup) */
		break;
	case SLAPI_PLUGIN_BE_POST_MODIFY_FN:
	case SLAPI_PLUGIN_BE_POST_MODRDN_FN:
	case SLAPI_PLUGIN_BE_POST_ADD_FN:
	case SLAPI_PLUGIN_BE_POST_DELETE_FN:
	case SLAPI_PLUGIN_BE_POST_OPEN_FN:
	case SLAPI_PLUGIN_BE_POST_BACKUP_FN:
        plugin_list_number= PLUGIN_LIST_BEPOSTOPERATION;
		do_op = 1; /* always allow backend callbacks (even during startup) */
		break;
	case SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN:
	case SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN:
	case SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN:
	case SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN:
	case SLAPI_PLUGIN_INTERNAL_PRE_BIND_FN:
        plugin_list_number= PLUGIN_LIST_INTERNAL_PREOPERATION;
		break;
	case SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN:
	case SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN:
	case SLAPI_PLUGIN_INTERNAL_POST_ADD_FN:
	case SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN:
        plugin_list_number= PLUGIN_LIST_INTERNAL_POSTOPERATION;
		break;
	case SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN:
	case SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN:
	case SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN:
	case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN:
	case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_TOMBSTONE_FN:
        plugin_list_number= PLUGIN_LIST_BETXNPREOPERATION;
		do_op = 1; /* always allow backend callbacks (even during startup) */
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN:
	case SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN:
	case SLAPI_PLUGIN_BE_TXN_POST_ADD_FN:
	case SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN:
        plugin_list_number= PLUGIN_LIST_BETXNPOSTOPERATION;
		do_op = 1; /* always allow backend callbacks (even during startup) */
		break;
	}

	if(plugin_list_number!=-1 && do_op)
	{
	    /* We stash the pblock plugin pointer to preserve the callers context */
        struct slapdplugin *p;
    	slapi_pblock_get(pb, SLAPI_PLUGIN, &p);
		/* Call the operation on the Global Plugins */
        rc= plugin_call_list(global_plugin_list[plugin_list_number], whichfunction, pb);
    	
    	slapi_pblock_set(pb, SLAPI_PLUGIN, p);
	}
	else
	{
	    /* Programmer error! or the callback is denied during startup */
	}
	return rc;
}


void
plugin_call_entrystore_plugins(char **entrystr, uint *size)
{
	struct slapdplugin	*p;
	for (p = global_plugin_list[PLUGIN_LIST_LDBM_ENTRY_FETCH_STORE];
			 p != NULL; p = p->plg_next )
	{
		if (p->plg_entrystorefunc)
			(*p->plg_entrystorefunc)(entrystr, size);
	}
}

void
plugin_call_entryfetch_plugins(char **entrystr, uint *size)
{
	struct slapdplugin	*p;
	for (p = global_plugin_list[PLUGIN_LIST_LDBM_ENTRY_FETCH_STORE];
			 p != NULL; p = p->plg_next )
	{
		if (p->plg_entryfetchfunc)
			(*p->plg_entryfetchfunc)(entrystr, size);
	}
}

/*
 * call extended operation plugins
 *
 * return SLAPI_PLUGIN_EXTENDED_SENT_RESULT if one of the extended operation
 *	plugins sent a result.
 * return SLAPI_PLUGIN_EXTENDED_NOT_HANDLED if no extended operation plugins
 *	handled the operation.
 * otherwise, return an LDAP error code (possibly a merge of the errors
 *	returned by the plugins we called).
 */
int
plugin_call_exop_plugins( Slapi_PBlock *pb, char *oid )
{
	struct slapdplugin	*p;
	int			i, rc;
	int lderr = SLAPI_PLUGIN_EXTENDED_NOT_HANDLED;

	for ( p = global_plugin_list[PLUGIN_LIST_EXTENDED_OPERATION]; p != NULL; p = p->plg_next ) {
		if ( p->plg_exhandler != NULL ) {
			if ( p->plg_exoids != NULL ) {
				for ( i = 0; p->plg_exoids[i] != NULL; i++ ) {
					if ( strcasecmp( oid, p->plg_exoids[i] )
					    == 0 ) {
						break;
					}
				}
				if (  p->plg_exoids[i] == NULL ) {
					continue;
				}
			}

			slapi_pblock_set( pb, SLAPI_PLUGIN, p );
			set_db_default_result_handlers( pb );
			if ( (rc = (*p->plg_exhandler)( pb ))
			    == SLAPI_PLUGIN_EXTENDED_SENT_RESULT ) {
				return( rc );	/* result sent */
			} else if ( rc != SLAPI_PLUGIN_EXTENDED_NOT_HANDLED ) {
				/*
				 * simple merge: report last real error
				 */
				if ( lderr == SLAPI_PLUGIN_EXTENDED_NOT_HANDLED
				    || rc != LDAP_SUCCESS ) {
					lderr = rc;
				}
			}
		}
	}	

	return( lderr );
}


/*
 * Attempt to convert the extended operation 'oid' to a string by
 * examining the registered plugins.  Returns NULL if no plugin is
 * registered for this OID.
 *
 * Our first choice is to use an OID-specific name that has been
 * registered by a plugin via the SLAPI_PLUGIN_EXT_OP_NAMELIST pblock setting.
 * Our second choice is to use the plugin's ID (short name).
 * Our third choice is to use the plugin's RDN (under cn=config).
 */
const char *
plugin_extended_op_oid2string( const char *oid )
{
	struct slapdplugin	*p;
	int					i, j;
	const char			*rval = NULL;

	for ( p = global_plugin_list[PLUGIN_LIST_EXTENDED_OPERATION]; p != NULL;
			p = p->plg_next ) {
		if ( p->plg_exhandler != NULL && p->plg_exoids != NULL ) {
			for ( i = 0; p->plg_exoids[i] != NULL; i++ ) {
				if ( strcasecmp( oid, p->plg_exoids[i] ) == 0 ) {
					if ( NULL != p->plg_exnames ) {
						for ( j = 0; j < i && p->plg_exnames[j] != NULL; ++j ) {
							;
						}
						rval = p->plg_exnames[j];		/* OID-related name */
					}

					if ( NULL == rval ) {
						if ( NULL != p->plg_desc.spd_id ) {
							rval = p->plg_desc.spd_id;	/* short name */
						} else {
							rval = p->plg_name;			/* RDN */
						}
					}
					break;
				}
			}
		}
	}

	return( rval );
}


/*
 * kexcoff: return the slapdplugin structure
 */
struct slapdplugin *
plugin_get_pwd_storage_scheme(char *name, int len, int index)
{
	/* index could be PLUGIN_LIST_PWD_STORAGE_SCHEME or PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME */
	struct slapdplugin *p;

	for ( p = global_plugin_list[index]; p != NULL; p = p->plg_next ) {
		if (strlen(p->plg_pwdstorageschemename) == len) {
			if (strncasecmp(p->plg_pwdstorageschemename, name, len) == 0) {
				return( p );
			}
		}
	}
	return( NULL );
}

char *
plugin_get_pwd_storage_scheme_list(int index)
{
	/* index could be PLUGIN_LIST_PWD_STORAGE_SCHEME or PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME */

	struct slapdplugin *p = NULL;
	char *names_list = NULL;
	int len = 0;

	/* first pass - calculate space needed for comma delimited list */
	for ( p = global_plugin_list[index]; p != NULL; p = p->plg_next ) {
		if ( p->plg_pwdstorageschemeenc != NULL )
		{
			/* + 1 for comma, 1 for space, 1 for null */
			len += strlen(p->plg_pwdstorageschemename) + 3;
		}
	}

	/* no plugins? */
	if (!len)
		return NULL;

	/* next, allocate the space */
	names_list = (char *)slapi_ch_malloc(len+1);
	*names_list = 0;

	/* second pass - write the string */
	for ( p = global_plugin_list[index]; p != NULL; p = p->plg_next ) {
		if ( p->plg_pwdstorageschemeenc != NULL )
		{
			strcat(names_list, p->plg_pwdstorageschemename);
			if (p->plg_next != NULL)
				strcat(names_list, ", ");
		}
	}
	return( names_list );
}

int
slapi_send_ldap_search_entry( Slapi_PBlock *pb, Slapi_Entry *e, LDAPControl **ectrls,
	char **attrs, int attrsonly )
{
	IFP fn = NULL;
	slapi_pblock_get(pb,SLAPI_PLUGIN_DB_ENTRY_FN,(void*)&fn);
	if (NULL == fn)
	{
		return -1;
	}
	return (*fn)(pb,e,ectrls,attrs,attrsonly);
}

void
slapi_set_ldap_result( Slapi_PBlock *pb, int err, char *matched, char *text,
	int nentries, struct berval **urls )
{
	char * old_matched = NULL;
	char * old_text = NULL;
	char * matched_copy = slapi_ch_strdup(matched);
	char * text_copy = slapi_ch_strdup(text);

	/* free the old matched and text, if any */
	slapi_pblock_get(pb, SLAPI_RESULT_MATCHED, &old_matched);
	slapi_ch_free_string(&old_matched);

	slapi_pblock_get(pb, SLAPI_RESULT_TEXT, &old_text);
	slapi_ch_free_string(&old_text);

	/* set the new stuff */
	slapi_pblock_set(pb, SLAPI_RESULT_CODE, &err);
	slapi_pblock_set(pb, SLAPI_RESULT_MATCHED, matched_copy);
	slapi_pblock_set(pb, SLAPI_RESULT_TEXT, text_copy);
}

void
slapi_send_ldap_result_from_pb( Slapi_PBlock *pb)
{
	int err;
	char *matched;
	char *text;
	IFP fn = NULL;

	slapi_pblock_get(pb, SLAPI_RESULT_CODE, &err);
	slapi_pblock_get(pb, SLAPI_RESULT_TEXT, &text);
	slapi_pblock_get(pb, SLAPI_RESULT_MATCHED, &matched);

	slapi_pblock_get(pb,SLAPI_PLUGIN_DB_RESULT_FN,(void*)&fn);
	if (NULL != fn)
	{
		(*fn)(pb,err,matched,text,0,NULL);
	}

	slapi_pblock_set(pb, SLAPI_RESULT_TEXT, NULL);
	slapi_pblock_set(pb, SLAPI_RESULT_MATCHED, NULL);
	slapi_ch_free((void **)&matched);
	slapi_ch_free((void **)&text);
}

void
slapi_send_ldap_result( Slapi_PBlock *pb, int err, char *matched, char *text,
	int nentries, struct berval **urls )
{
	IFP fn = NULL;
	Slapi_Operation *operation;
    long op_type;

	/* GB : for spanning requests over multiple backends */
	if (err == LDAP_NO_SUCH_OBJECT)
	{
		slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
	
		op_type = operation_get_type(operation);
		if (op_type == SLAPI_OPERATION_SEARCH)
		{
			if (urls || nentries)
			{
				LDAPDebug( LDAP_DEBUG_ANY, "ERROR : urls or nentries set"
					"in sendldap_result while NO_SUCH_OBJECT returned\n",0,0,0);
			}

			slapi_set_ldap_result(pb, err, matched, text, 0, NULL);
			return;
		}
	}

	slapi_pblock_set(pb, SLAPI_RESULT_CODE, &err);

	slapi_pblock_get(pb,SLAPI_PLUGIN_DB_RESULT_FN,(void*)&fn);
	if (NULL == fn)
	{
		return ;
	}

	/*
	 * Call the result function. It should set pb->pb_op->o_status to
	 * SLAPI_OP_STATUS_RESULT_SENT right after sending the result to
	 * the client or otherwise consuming it.
	 */
	(*fn)(pb,err,matched,text,nentries,urls);
}

int
slapi_send_ldap_referral( Slapi_PBlock *pb, Slapi_Entry *e, struct berval **refs,
	struct berval ***urls )
{
	IFP fn = NULL;
	slapi_pblock_get(pb,SLAPI_PLUGIN_DB_REFERRAL_FN,(void*)&fn);
	if (NULL == fn)
	{
		return -1;
	}
	return (*fn)(pb,e,refs,urls);
}


/***********************************************************
	start of plugin dependency code
************************************************************/

/* struct _plugin_dep_type
 * we shall not presume to know all plugin types
 * so as to allow new types to be added without
 * requiring changes to this code (hopefully)
 * so we need to dynamically keep track of them
 */

typedef struct _plugin_dep_type{
	char *type; /* the string descriptor */
	int num_not_started; /* the count of plugins which have yet to be started for this type */
	struct _plugin_dep_type *next;
} *plugin_dep_type;

/* _plugin_dep_config
 * we need somewhere to collect the plugin configurations
 * prior to attempting to resolve dependencies
 */

typedef struct _plugin_dep_config {
	char *name;
	char *type;
	Slapi_PBlock pb;
	struct slapdplugin *plugin;
	Slapi_Entry *e;
	int entry_created;
	int op_done;
	char **depends_type_list;
	int total_type;
	char **depends_named_list;
	int total_named;
	char *config_area;
} plugin_dep_config;

/* list of plugins which should be shutdown in reverse order */
static plugin_dep_config *global_plugin_shutdown_order = 0;
static int global_plugins_started = 0;

/*
 * find_plugin_type
 *
 * searches the list for the plugin type
 * and returns the plugin_dep_type if found
 */

static plugin_dep_type
find_plugin_type(plugin_dep_type head, char *type)
{
	plugin_dep_type ret = 0;
	plugin_dep_type iter = head;

	while(iter)
	{
		if(!slapi_UTF8CASECMP(iter->type, type))
		{
			ret = iter;
			break;
		}

		iter = iter->next;
	}

	return ret;
}


/*
 * increment_plugin_type
 *
 * searches the list for the plugin type
 * and increments its not started value
 * returns the current type count on success -1 on failure
 * to find the type
 */

static int
increment_plugin_type(plugin_dep_type head, char *type)
{
	int ret = -1;
	plugin_dep_type the_type;

	if ((the_type = find_plugin_type(head, type)) != NULL)
		ret = ++the_type->num_not_started;

	return ret;
}


/*
 * decrement_plugin_type
 *
 * searches the list for the plugin type
 * and decrements its not started value
 * returns the current type count on success -1 on failure
 * to find the type
 */

static int
decrement_plugin_type(plugin_dep_type head, char *type)
{
	int ret = -1;
	plugin_dep_type the_type;

	if ((the_type = find_plugin_type(head, type)) != NULL)
		ret = --the_type->num_not_started;

	return ret;
}

/*
 * add_plugin_type
 * 
 * Either increments the count of the plugin type
 * or when it does not exist, adds it to the list
 */

static int
add_plugin_type(plugin_dep_type *head, char *type)
{
	int ret = -1;

	if(*head)
	{
		if(0 < increment_plugin_type(*head, type))
		{
			ret = 0;
		}
	}
	
	if(ret)
	{
		/* create new head */
		plugin_dep_type tmp_head;

		tmp_head = (plugin_dep_type)slapi_ch_malloc(sizeof(struct _plugin_dep_type));
		tmp_head->num_not_started = 1;
		tmp_head->type = slapi_ch_strdup(type);
		ret = 0;
		tmp_head->next = *head;
		(*head) = tmp_head;
	}

	return ret;
}


/*
 * plugin_create_stringlist
 * 
 * Creates a string list from values of the entries 
 * attribute passed in as args - used to track dependencies
 *
 */

int 
plugin_create_stringlist( Slapi_Entry *plugin_entry, char *attr_name, 
			int *total_strings, char ***list)
{
	Slapi_Attr *attr = 0;
	int hint =0;
	int num_vals = 0;
	int val_index = 0;
	Slapi_Value *val;

	if(0 == slapi_entry_attr_find( plugin_entry, attr_name, &attr ))
	{
		/* allocate memory for the string array */
		slapi_attr_get_numvalues( attr, &num_vals);

		if(num_vals)
		{
			*total_strings = num_vals;
			*list = (char **)slapi_ch_malloc(sizeof(char*) * num_vals);
		}
		else
			goto bail;  /* if this ever happens, then they are running on a TSR-80 */

		val_index = 0;

		hint = slapi_attr_first_value( attr, &val );
		while(val_index < num_vals)
		{
			/* add the value to the array */
			(*list)[val_index] = (char*)slapi_ch_strdup(slapi_value_get_string(val));

			hint = slapi_attr_next_value( attr, hint, &val );
			val_index++;
		}
	}
	else
		*total_strings = num_vals;

bail:
	return num_vals;
}



/*
 * plugin_dependency_startall()
 *
 * Starts all plugins (apart from syntax and matching rule) in order
 * of dependency.
 * 
 * Dependencies will be determined by these multi-valued attributes: 
 *
 * nsslapd-plugin-depends-on-type : all plugins whose type value matches one of these values must
 * be started prior to this plugin 
 *
 * nsslapd-plugin-depends-on-named : the plugin whose cn value matches one of these values must
 * be started prior to this plugin 
 */

static int 
plugin_dependency_startall(int argc, char** argv, char *errmsg, int operation)
{
	int ret = 0;
	Slapi_PBlock pb;
	int total_plugins = 0;
	plugin_dep_config *config = 0;
	plugin_dep_type plugin_head = 0;	
	int plugin_index = 0;
	Slapi_Entry *plugin_entry;
	int i = 0;  /* general index iterator */
	plugin_dep_type the_plugin_type;
	int index = 0;
	char * value;
	int plugins_started;
	int num_plg_started;
	struct slapdplugin *plugin;
	entry_and_plugin_t *ep = dep_plugin_entries;
	int shutdown_index = 0;

	/* 
	 * Disable registered plugin functions so preops/postops/etc
	 * dont get called prior to the plugin being started (due to
	 * plugins performing ops on the DIT)
	 */

	global_plugin_callbacks_enabled = 0;

	/* Count the plugins so we can allocate memory for the config array */
    while(ep)
	{
		total_plugins++;

		ep = ep->next;
	}

	/* allocate the config array */
	config = (plugin_dep_config*)slapi_ch_malloc(sizeof(plugin_dep_config) * total_plugins);

	if(config)
		memset(config, 0, sizeof(plugin_dep_config) * total_plugins);
	else
	{
		ret = -1;
		goto bail;
	}

	ep = dep_plugin_entries;

	/* Collect relevant config */
    while(ep)
	{
		plugin = ep->plugin;

		if(plugin == 0)
			continue;

		pblock_init(&pb);
		slapi_pblock_set( &pb, SLAPI_ARGC, &argc);
		slapi_pblock_set( &pb, SLAPI_ARGV, &argv);

		config[plugin_index].pb = pb;
		config[plugin_index].e = ep->e;

		/* add type */
		plugin_entry = ep->e;
		ep->e = NULL; /* consumed by the operation above, and eventually by the
						 slapi_internal_add operation below */

		if(plugin_entry)
		{
			/*
			 * Pass the plugin DN in SLAPI_TARGET_SDN and the plugin entry
			 * in SLAPI_ADD_ENTRY.  For this to actually work, we need to
			 * create an operation and include that in the pblock as well,
			 * because these two items are stored in the operation parameters.
			 */
			/* WARNING: memory leak here - op is only freed by a pblock_done,
			   and this only happens below if the plugin is enabled - a short
			   circuit goto bail may also cause a leak - however, since this
			   only happens a few times at startup, this is not a very serious
			   leak - just after the call to plugin_call_one */
			Operation *op = internal_operation_new(SLAPI_OPERATION_ADD, 0);
			slapi_pblock_set(&(config[plugin_index].pb), SLAPI_OPERATION, op);
			slapi_pblock_set(&(config[plugin_index].pb), SLAPI_TARGET_SDN,
				(void*)(slapi_entry_get_sdn_const(plugin_entry)));
			slapi_pblock_set(&(config[plugin_index].pb), SLAPI_ADD_ENTRY,
				plugin_entry );

			/* Pass the plugin alternate config area DN in SLAPI_PLUGIN_CONFIG_AREA. */
			value = slapi_entry_attr_get_charptr(plugin_entry, ATTR_PLUGIN_CONFIG_AREA);
			if(value)
			{
				config[plugin_index].config_area = value;
				value = NULL;
				slapi_pblock_set(&(config[plugin_index].pb), SLAPI_PLUGIN_CONFIG_AREA,
							config[plugin_index].config_area);
			}

			value = slapi_entry_attr_get_charptr(plugin_entry, "nsslapd-plugintype");
			if(value)
			{
				add_plugin_type( &plugin_head, value);	
				config[plugin_index].type = value;
				value = NULL;
			}

			/* now the name */
			value = slapi_entry_attr_get_charptr(plugin_entry, "cn");
			if(value)
			{
				config[plugin_index].name = value;
				value = NULL;
			}


			config[plugin_index].plugin = plugin;

			/* now add dependencies */
			plugin_create_stringlist( plugin_entry, "nsslapd-plugin-depends-on-named", 
				&(config[plugin_index].total_named), &(config[plugin_index].depends_named_list));

			plugin_create_stringlist( plugin_entry, "nsslapd-plugin-depends-on-type", 
				&(config[plugin_index].total_type), &(config[plugin_index].depends_type_list));
		}

		plugin_index++;
		ep = ep->next;
	}

	/* prepare list of shutdown order (we need nothing fancier right now
	 * than the reverse startup order)  The list may include NULL entries,
	 * these will be plugins which were never started
	 */
	shutdown_index = total_plugins - 1;

	global_plugin_shutdown_order = (plugin_dep_config*)slapi_ch_malloc(sizeof(plugin_dep_config) * total_plugins);
	if(global_plugin_shutdown_order)
		memset(global_plugin_shutdown_order, 0, sizeof(plugin_dep_config) * total_plugins);
	else
	{
		ret = -1;
		goto bail;
	}

	/* now resolve dependencies 
	 * cycle through list, if a plugin has no dependencies then start it
	 * then remove it from the dependency lists of all other plugins
	 * and decrement the corresponding element of the plugin types array
	 * for depends_type we will need to check the array of plugin types
	 * to see if all type dependencies are at zero prior to start
	 * if one cycle fails to load any plugins we have failed, however
	 * we shall continue loading plugins in case a configuration error
	 * can correct itself
	 */

	plugins_started = 1;
	num_plg_started = 0;

	while(plugins_started && num_plg_started < total_plugins)
	{
		plugins_started = 0;

		for(plugin_index=0; plugin_index < total_plugins; plugin_index++)
		{
			/* perform op on plugins only once */
			if(config[plugin_index].op_done == 0)
			{
				int enabled = 0;
				int satisfied = 0;
				int break_out = 0;

				/* 
				 * determine if plugin is enabled
				 * some processing is necessary even
				 * if it is not
				 */
				if ( NULL != config[plugin_index].e && (value = slapi_entry_attr_get_charptr(config[plugin_index].e,
						ATTR_PLUGIN_ENABLED)) &&
						!strcasecmp(value, "on"))
				{
					enabled = 1;
				}
				else
					enabled = 0;

				slapi_ch_free((void**)&value);

				/* 
				 * make sure named dependencies have been satisfied 
				 * that means that the list of names should contain all
				 * null entries
				 */

				if(enabled && config[plugin_index].total_named)
				{
					i = 0;

					while(break_out == 0 && i < config[plugin_index].total_named)
					{
						satisfied = 1;

						if((config[plugin_index].depends_named_list)[i] != 0)
						{
							satisfied = 0;
							break_out = 1;
						}

						i++;
					}

					if(!satisfied)
						continue;
				}

				/* 
				 * make sure the type dependencies have been satisfied
				 * that means for each type in the list, it's number of
				 * plugins left not started is zero
				 *
				 */
				satisfied = 0;
				break_out = 0;

				if(enabled && config[plugin_index].total_type)
				{
					i = 0;

					while(break_out == 0 && i < config[plugin_index].total_type)
					{
						satisfied = 1;

						the_plugin_type = find_plugin_type(plugin_head, (config[plugin_index].depends_type_list)[i]);

						if(the_plugin_type && the_plugin_type->num_not_started != 0)
						{
							satisfied = 0;
							break_out = 1;
						}

						i++;
					}

					if(!satisfied)
						continue;
				}

				/**** This plugins dependencies have now been satisfied ****/
				
				satisfied = 1; /* symbolic only */

				/* 
				 * Add the plugins entry to the DSE so the plugin can get
				 * its config (both enabled and disabled have entries
				 */

				if(!config[plugin_index].entry_created)
				{
					int plugin_actions = 0;
					Slapi_PBlock newpb;
					Slapi_Entry *newe;

					pblock_init(&newpb);
					/* 
					 * config[plugin_index].e is freed up by
					 * below function calls, but we may need
					 * it later, so create a copy
					 */
					newe = slapi_entry_dup( config[plugin_index].e );
					slapi_add_entry_internal_set_pb(&newpb, newe, NULL,
									plugin_get_default_component_id(), plugin_actions);
					slapi_pblock_set(&newpb, SLAPI_TARGET_SDN, (void*)slapi_entry_get_sdn_const(newe));
					slapi_add_internal_pb(&newpb);
					pblock_done(&newpb);
					config[plugin_index].entry_created = 1;
				}

				/*	
				 *	only actually start plugin and remove from lists if its enabled
				 *	we do remove from plugin type list however - rule is dependency on
				 *	zero or more for type
				 */

				if (enabled)
				{
					/* finally, perform the op on the plugin */
			
					LDAPDebug( LDAP_DEBUG_PLUGIN, "Starting %s plugin %s\n" , config[plugin_index].type, config[plugin_index].name, 0 );

					ret = plugin_call_one( config[plugin_index].plugin, operation, &(config[plugin_index].pb));

					pblock_done(&(config[plugin_index].pb));

					if(ret)
					{
						/*
						 * We will not exit here.  If we allow plugins to load normally it is
						 * possible that a configuration error (dependedncies which were not
						 * configured properly) can be recovered from.  If there really is a
						 * problem then the plugin will never start and eventually it will
						 * trigger an exit anyway.
						 */
						LDAPDebug( LDAP_DEBUG_ANY, "Failed to start %s plugin %s\n" , config[plugin_index].type, config[plugin_index].name, 0 );
						continue;
					}

					/* Add this plugin to the shutdown list */

					global_plugin_shutdown_order[shutdown_index] = config[plugin_index];
					shutdown_index--;
					global_plugins_started++;

					/* remove this named plugin from other plugins lists */

					for(i=0; i<total_plugins; i++)
					{
						index = 0;

						while(index < config[i].total_named)
						{
							if((config[i].depends_named_list)[index] != 0 && !slapi_UTF8CASECMP((config[i].depends_named_list)[index], config[plugin_index].name))
							{
								slapi_ch_free((void**)&((config[i].depends_named_list)[index]));
								(config[i].depends_named_list)[index] = 0;
							}

							index++;
						}	
					}
				}

				/* decrement the type counter for this plugin type */

				decrement_plugin_type(plugin_head, config[plugin_index].type);
				config[plugin_index].op_done = 1;
				num_plg_started++;
				plugins_started = 1;
			}

		}
	}

	if(plugins_started == 0)
	{
		/* a dependency was not resolved - error */
		LDAPDebug( LDAP_DEBUG_ANY, "Error: Failed to resolve plugin dependencies\n" , 0, 0, 0 );

		/* list the plugins yet to perform op */
		index = 0;

		while(i < total_plugins)
		{
			if(config[i].op_done == 0)
			{
				LDAPDebug( LDAP_DEBUG_ANY, "Error: %s plugin %s is not started\n" , config[i].type, config[i].name, 0 );
			}

			i++;
		}
				
		exit(1);
	}

bail:
	
	/*
	 * need the details in config to hang around for shutdown
	 * config itself may be deleted since its contents have been
	 * copied by value to the shutdown list
	 */	
	
	if(config)
	{
		index = 0;
			
		while(index < total_plugins)
		{
/*
			if(config[index].depends_named_list)
			{
				slapi_ch_free((void**)&(config[index].depends_named_list));
			}
*/
			if(config[index].depends_type_list)
			{
				i = 0;

				while(i < config[index].total_type)
				{
					slapi_ch_free((void**)&(config[index].depends_type_list)[i]);

					i++;
				}

				slapi_ch_free((void**)&(config[index].depends_type_list));
			}
/*
			slapi_ch_free((void**)&(config[index].name));
			slapi_ch_free((void**)&(config[index].type));
*/
			index++;
		}
		
		slapi_ch_free((void**)&config);
	}

	/* Finally enable registered plugin functions */

	global_plugin_callbacks_enabled = 1;

	return ret;
}

/*
 *	plugin_dependency_closeall
 *	
 *	uses the shutdown list created at startup to close
 *	plugins in the correct order
 *
 *  For now this leaks the list and contents, but since
 *  it hangs around until shutdown anyway, we don't care
 *
 */
void
plugin_dependency_closeall()
{
   	Slapi_PBlock pb;
	int plugins_closed = 0;
	int index = 0;

	while(plugins_closed<global_plugins_started)
	{
		/*
		 * the first few entries may not be valid
		 * since the list was created in the reverse
		 * order and some plugins may have been counted
		 * for the purpose of list allocation but are
		 * disabled and so were never started
		 *
		 * we check that here
		 */
		if(global_plugin_shutdown_order[index].name)
		{
	   		pblock_init(&pb);
		    plugin_call_one( global_plugin_shutdown_order[index].plugin, SLAPI_PLUGIN_CLOSE_FN, &pb );
		    /* set plg_closed to 1 to prevent any further plugin pre/post op function calls */
		    global_plugin_shutdown_order[index].plugin->plg_closed = 1;
			plugins_closed++;
		}

		index++;
	}
}

/***********************************************************
	end of plugin dependency code
************************************************************/


/*
 * Function: plugin_startall
 *
 * Returns: squat
 *
 * Description: Some plugins may need to do some stuff after all the config
 *              stuff is done with. So this function goes through and starts all plugins
 */
void
plugin_startall(int argc, char** argv, int start_backends, int start_global)
{
 	/* initialize special plugin structures */
	default_plugin_init ();

	plugin_dependency_startall(argc, argv, "plugin startup failed\n", SLAPI_PLUGIN_START_FN);
}

/*
 * Function: plugin_close_all
 *
 * Returns: squat
 *
 * Description: cleanup routine, allows plugins to kill threads, free memory started in start fn
 *              
 */
void 
plugin_closeall(int close_backends, int close_globals)
{
	plugin_dependency_closeall();
}


static int
plugin_call_list (struct slapdplugin *list, int operation, Slapi_PBlock *pb)
{
	return plugin_call_func(list, operation, pb, 0);
}

static int
plugin_call_one (struct slapdplugin *list, int operation, Slapi_PBlock *pb)
{
	return plugin_call_func(list, operation, pb, 1);
}


/*
 * Return codes: 
 * - For preoperation plugins, returns the return code passed back from the first
 *   plugin that fails, or zero if all plugins succeed.
 * - For bepreop and bepostop plugins, returns a bitwise OR of the return codes
 *   returned by all the plugins called (there's only one bepreop and one bepostop
 *   in DS 5.0 anyway).
 * - For postoperation plugins, returns 0.
 */
static int
plugin_call_func (struct slapdplugin *list, int operation, Slapi_PBlock *pb, int call_one)
{
    /* Invoke the operation on the plugins that are registered for the subtree effected by the operation. */
    int	rc;
	int return_value = 0;
	int count= 0;
    for (; list != NULL; list = list->plg_next)
	{
		IFP func = NULL;
	
	slapi_pblock_set (pb, SLAPI_PLUGIN, list);
	set_db_default_result_handlers (pb); /* JCM: What's this do? Is it needed here? */
	if (slapi_pblock_get (pb, operation, &func) == 0 && func != NULL &&
			plugin_invoke_plugin_pb (list, operation, pb) && list->plg_closed == 0)
		{
			char *n= list->plg_name;
			LDAPDebug( LDAP_DEBUG_TRACE, "Calling plugin '%s' #%d type %d\n", (n==NULL?"noname":n), count, operation );
			/* counters_to_errors_log("before plugin call"); */
			if (( rc = func (pb)) != 0 )
			{
				if (SLAPI_PLUGIN_PREOPERATION == list->plg_type ||
				    SLAPI_PLUGIN_INTERNAL_PREOPERATION == list->plg_type ||
				    SLAPI_PLUGIN_START_FN == operation )
				{
					/*
					 * We bail out of plugin processing for preop plugins
					 * that return a non-zero return code. This allows preop
					 * plugins to cause further preop processing to terminate, and
					 * causes the operation to be vetoed.
					 */
					return_value = rc;
					break;
				}
				else if (SLAPI_PLUGIN_BEPREOPERATION == list->plg_type ||
				         SLAPI_PLUGIN_BETXNPREOPERATION == list->plg_type ||
				         SLAPI_PLUGIN_BEPOSTOPERATION == list->plg_type)
				{
					/* 
					 * respect fatal error SLAPI_PLUGIN_FAILURE (-1);
					 * should not OR it.
					 */
					if (SLAPI_PLUGIN_FAILURE == rc) {
						return_value = rc;
					} else if (SLAPI_PLUGIN_FAILURE != return_value) {
						/* OR the result into the return value 
						 * for be pre/postops */
						return_value |= rc;
					}
				}
			}
			/* counters_to_errors_log("after plugin call"); */
		}

		count++;

		if(call_one)
			break;
	}
	return( return_value );
}

int
slapi_berval_cmp (const struct berval* L, const struct berval* R) /* JCM - This does not belong here. But, where should it go? */
{
    int result = 0;

    if(L == NULL && R != NULL){
        return 1;
    } else if(L != NULL && R == NULL){
        return -1;
    } else if(L == NULL && R == NULL){
        return 0;
    }
    if (L->bv_len < R->bv_len) {
        result = memcmp (L->bv_val, R->bv_val, L->bv_len);
        if (result == 0)
            result = -1;
    } else {
        result = memcmp (L->bv_val, R->bv_val, R->bv_len);
        if (result == 0 && (L->bv_len > R->bv_len))
            result = 1;
    }

    return result;
}


static char **supported_saslmechanisms = NULL;
static Slapi_RWLock *supported_saslmechanisms_lock = NULL;

/*
 * register a supported SASL mechanism so it will be returned as part of the
 * root DSE.
 */
void
slapi_register_supported_saslmechanism( char *mechanism )
{
	if ( mechanism != NULL ) {
		if (NULL == supported_saslmechanisms_lock) {
			/* This is thread safe, as it gets executed by
			 * a single thread at init time (main->init_saslmechanisms) */
			supported_saslmechanisms_lock = slapi_new_rwlock();
			if (NULL == supported_saslmechanisms_lock) {
				/* Out of resources */
				slapi_log_error(SLAPI_LOG_FATAL, "startup",
					"slapi_register_supported_saslmechanism: failed to create lock.\n");
				exit (1);
			}
		}
		slapi_rwlock_wrlock(supported_saslmechanisms_lock);
		charray_add( &supported_saslmechanisms, slapi_ch_strdup( mechanism ));
		slapi_rwlock_unlock(supported_saslmechanisms_lock);
	}
}


/*
 * return pointer to NULL-terminated array of supported SASL mechanisms.
 * This function is not MTSafe and should be deprecated.
 * slapi_get_supported_saslmechanisms_copy should be used instead.
 */
char **
slapi_get_supported_saslmechanisms( void )
{
	return( supported_saslmechanisms );
}


/*
 * return pointer to NULL-terminated array of supported SASL mechanisms.
 */
char **
slapi_get_supported_saslmechanisms_copy( void )
{
    char ** ret = NULL;
    slapi_rwlock_rdlock(supported_saslmechanisms_lock);
    ret = charray_dup(supported_saslmechanisms);
    slapi_rwlock_unlock(supported_saslmechanisms_lock);
    return( ret );
}


static char **supported_extended_ops = NULL;
static Slapi_RWLock *extended_ops_lock = NULL;

/*
 * register all of the LDAPv3 extended operations we know about.
 */
void
ldapi_init_extended_ops( void )
{
	extended_ops_lock = slapi_new_rwlock();
	if (NULL == extended_ops_lock) {
		/* Out of resources */
		slapi_log_error(SLAPI_LOG_FATAL, "startup",
			"ldapi_init_extended_ops: failed to create lock.\n");
		exit (1);
	}

	slapi_rwlock_wrlock(extended_ops_lock);
	charray_add(&supported_extended_ops,
	            slapi_ch_strdup(EXTOP_BULK_IMPORT_START_OID));
	charray_add(&supported_extended_ops,
	            slapi_ch_strdup(EXTOP_BULK_IMPORT_DONE_OID));
	/* add future supported extops here... */
	slapi_rwlock_unlock(extended_ops_lock);
}


/*
 * register an extended op. so it can be returned as part of the root DSE.
 */
void
ldapi_register_extended_op( char **opoids )
{
    int	i;

    slapi_rwlock_wrlock(extended_ops_lock);
    for ( i = 0; opoids != NULL && opoids[i] != NULL; ++i ) {
	if ( !charray_inlist( supported_extended_ops, opoids[i] )) {
	    charray_add( &supported_extended_ops, slapi_ch_strdup( opoids[i] ));
	}
    }
    slapi_rwlock_unlock(extended_ops_lock);
}


/*
 * retrieve supported extended operation OIDs
 * return 0 if successful and -1 if not.
 * This function is not MTSafe and should be deprecated.
 * slapi_get_supported_extended_ops_copy should be used instead.
 */
char **
slapi_get_supported_extended_ops( void )
{
    return( supported_extended_ops );
}


/*
 * retrieve supported extended operation OIDs
 * return 0 if successful and -1 if not.
 */
char **
slapi_get_supported_extended_ops_copy( void )
{
    char ** ret = NULL;
    slapi_rwlock_rdlock(extended_ops_lock);
    ret = charray_dup(supported_extended_ops);
    slapi_rwlock_unlock(extended_ops_lock);
    return( ret );
}


/*
  looks up the given string type to convert to the internal integral type; also
  returns the plugin list associated with the plugin type
  returns 0 upon success and non-zero upon failure
*/
static int
plugin_get_type_and_list(
	const char *plugintype,
	int *type,
	struct slapdplugin ***plugin_list
)
{
	int plugin_list_index = -1;
	if ( strcasecmp( plugintype, "database" ) == 0 ) {
		*type = SLAPI_PLUGIN_DATABASE;
    	plugin_list_index= PLUGIN_LIST_DATABASE;
	} else if ( strcasecmp( plugintype, "extendedop" ) == 0 ) {
		*type = SLAPI_PLUGIN_EXTENDEDOP;
    	plugin_list_index= PLUGIN_LIST_EXTENDED_OPERATION;
	} else if ( strcasecmp( plugintype, "preoperation" ) == 0 ) {
		*type = SLAPI_PLUGIN_PREOPERATION;
    	plugin_list_index= PLUGIN_LIST_PREOPERATION;
	} else if ( strcasecmp( plugintype, "postoperation" ) == 0 ) {
		*type = SLAPI_PLUGIN_POSTOPERATION;
    	plugin_list_index= PLUGIN_LIST_POSTOPERATION;
	} else if ( strcasecmp( plugintype, "matchingrule" ) == 0 ) {
		*type = SLAPI_PLUGIN_MATCHINGRULE;
    	plugin_list_index= PLUGIN_LIST_MATCHINGRULE;
	} else if ( strcasecmp( plugintype, "syntax" ) == 0 ) {
		*type = SLAPI_PLUGIN_SYNTAX;
    	plugin_list_index= PLUGIN_LIST_SYNTAX;
	} else if ( strcasecmp( plugintype, "accesscontrol" ) == 0 ) {
		*type = SLAPI_PLUGIN_ACL;
    	plugin_list_index= PLUGIN_LIST_ACL;
	} else if ( strcasecmp( plugintype, "bepreoperation" ) == 0 ) {
		*type = SLAPI_PLUGIN_BEPREOPERATION;
    	plugin_list_index= PLUGIN_LIST_BEPREOPERATION;
	} else if ( strcasecmp( plugintype, "bepostoperation" ) == 0 ) {
		*type = SLAPI_PLUGIN_BEPOSTOPERATION;
    	plugin_list_index= PLUGIN_LIST_BEPOSTOPERATION;
	} else if ( strcasecmp( plugintype, "betxnpreoperation" ) == 0 ) {
		*type = SLAPI_PLUGIN_BETXNPREOPERATION;
    	plugin_list_index= PLUGIN_LIST_BETXNPREOPERATION;
	} else if ( strcasecmp( plugintype, "betxnpostoperation" ) == 0 ) {
		*type = SLAPI_PLUGIN_BETXNPOSTOPERATION;
    	plugin_list_index= PLUGIN_LIST_BETXNPOSTOPERATION;
	} else if ( strcasecmp( plugintype, "internalpreoperation" ) == 0 ) {
		*type = SLAPI_PLUGIN_INTERNAL_PREOPERATION;
    	plugin_list_index= PLUGIN_LIST_INTERNAL_PREOPERATION;
	} else if ( strcasecmp( plugintype, "internalpostoperation" ) == 0 ) {
		*type = SLAPI_PLUGIN_INTERNAL_POSTOPERATION;
    	plugin_list_index= PLUGIN_LIST_INTERNAL_POSTOPERATION;
	} else if ( strcasecmp( plugintype, "entry" ) == 0 ) {
		*type = SLAPI_PLUGIN_ENTRY;
    	plugin_list_index= PLUGIN_LIST_ENTRY;
	} else if ( strcasecmp( plugintype, "object" ) == 0 ) {
		*type = SLAPI_PLUGIN_TYPE_OBJECT;
    	plugin_list_index= PLUGIN_LIST_OBJECT;
	} else if ( strcasecmp( plugintype, "pwdstoragescheme" ) == 0 ) {
        *type = SLAPI_PLUGIN_PWD_STORAGE_SCHEME;
        plugin_list_index= PLUGIN_LIST_PWD_STORAGE_SCHEME;
	} else if ( strcasecmp( plugintype, "reverpwdstoragescheme" ) == 0 ) {
        *type = SLAPI_PLUGIN_REVER_PWD_STORAGE_SCHEME;
        plugin_list_index= PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME;
	} else if ( strcasecmp( plugintype, "vattrsp" ) == 0 ) {
        *type = SLAPI_PLUGIN_VATTR_SP;
        plugin_list_index= PLUGIN_LIST_VATTR_SP;
	} else if ( strcasecmp( plugintype, "ldbmentryfetchstore" ) == 0 ) {
        *type = SLAPI_PLUGIN_LDBM_ENTRY_FETCH_STORE;
        plugin_list_index= PLUGIN_LIST_LDBM_ENTRY_FETCH_STORE;
	} else if ( strcasecmp( plugintype, "index" ) == 0 ) {
        *type = SLAPI_PLUGIN_INDEX;
        plugin_list_index= PLUGIN_LIST_INDEX;
	} else {
		return( 1 );	/* unknown plugin type - pass to backend */
	}

	if (plugin_list_index >= 0)
		*plugin_list = &global_plugin_list[plugin_list_index];

	return 0;
}

static const char *
plugin_exists(const Slapi_DN *plugin_dn)
{
	/* check to see if the plugin name is unique */
	const char *retval = 0;
	if (global_plugin_dns && PL_HashTableLookup(global_plugin_dns,
												slapi_sdn_get_ndn(plugin_dn)))
	{
		retval = slapi_sdn_get_dn(plugin_dn);
	}

	return retval;
}

static int
plugin_set_subtree_config(PluginTargetData *subtree_config, const char *val)
{
	int status = 0;

	if (strcasecmp (val, ALL_DATA) == 0) /* allow access to both local and remote data */
	{
		plugin_set_global (subtree_config);
	}
	else if (strcasecmp (val, LOCAL_DATA) == 0) /* allow access to all locally hosted data */
	{
		ptd_set_special_data (subtree_config, PLGC_DATA_LOCAL);
	}
	else if (strcasecmp (val, REMOTE_DATA) == 0)/* allow access to requests for remote data */
	{
		ptd_set_special_data (subtree_config, PLGC_DATA_REMOTE);	
	}
	else /* dn */
	{
		ptd_add_subtree (subtree_config, slapi_sdn_new_dn_byval(val));
	}
	/* I suppose we could check the val at this point to make sure
	   its a valid DN . . . */

	return status;
}

static int
set_plugin_config_from_entry(
	const Slapi_Entry *plugin_entry,
	struct slapdplugin *plugin
)
{
	struct pluginconfig *config = &plugin->plg_conf;
	char *value = 0;
	char **values = 0;
	int i = 0;
	int status = 0;
	PRBool target_seen = PR_FALSE;
	PRBool bind_seen = PR_FALSE;

	if ((value = slapi_entry_attr_get_charptr(plugin_entry,
											 ATTR_PLUGIN_SCHEMA_CHECK)) != NULL)
	{
		if (plugin_config_set_action(&config->plgc_schema_check, value))
		{
			LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: invalid value %s for attribute %s "
					  "from entry %s\n", value, ATTR_PLUGIN_SCHEMA_CHECK,
					  slapi_entry_get_dn_const(plugin_entry));
			status = 1;
		}
		slapi_ch_free((void**)&value);
	}

	if ((value = slapi_entry_attr_get_charptr(plugin_entry,
											 ATTR_PLUGIN_LOG_ACCESS)) != NULL)
	{
		if (plugin_config_set_action(&config->plgc_log_access, value))
		{
			LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: invalid value %s for attribute %s "
					  "from entry %s\n", value, ATTR_PLUGIN_LOG_ACCESS,
					  slapi_entry_get_dn_const(plugin_entry));
			status = 1;
		}
		slapi_ch_free((void**)&value);
	}

	if ((value = slapi_entry_attr_get_charptr(plugin_entry,
											 ATTR_PLUGIN_LOG_AUDIT)) != NULL)
	{
		if (plugin_config_set_action(&config->plgc_log_audit, value))
		{
			LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: invalid value %s for attribute %s "
					  "from entry %s\n", value, ATTR_PLUGIN_LOG_AUDIT,
					  slapi_entry_get_dn_const(plugin_entry));
			status = 1;
		}
		slapi_ch_free((void**)&value);
	}

	if ((value = slapi_entry_attr_get_charptr(plugin_entry,
											 ATTR_PLUGIN_INVOKE_FOR_REPLOP)) != NULL)
	{
		if (plugin_config_set_action(&config->plgc_invoke_for_replop, value))
		{
			LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: invalid value %s for attribute %s "
					  "from entry %s\n", value, ATTR_PLUGIN_INVOKE_FOR_REPLOP,
					  slapi_entry_get_dn_const(plugin_entry));
			status = 1;
		}
		slapi_ch_free((void**)&value);
	}

	values = slapi_entry_attr_get_charray(plugin_entry,
											 ATTR_PLUGIN_TARGET_SUBTREE);
	for (i=0; values && values[i]; i++)
	{
		if (plugin_set_subtree_config(&(config->plgc_target_subtrees), values[i]))
		{
			LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: invalid value %s for attribute %s "
					  "from entry %s\n", values[i], ATTR_PLUGIN_TARGET_SUBTREE,
					  slapi_entry_get_dn_const(plugin_entry));
			status = 1;
			break;
		}
		else
		{
			target_seen = PR_TRUE;
		}
	}
	slapi_ch_array_free(values);

	values = slapi_entry_attr_get_charray(plugin_entry,
											 ATTR_PLUGIN_EXCLUDE_TARGET_SUBTREE);
	for (i=0; values && values[i]; i++)
	{
		if (plugin_set_subtree_config(&(config->plgc_excluded_target_subtrees), values[i]))
		{
			LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: invalid value %s for attribute %s "
					  "from entry %s\n", values[i], ATTR_PLUGIN_EXCLUDE_TARGET_SUBTREE,
					  slapi_entry_get_dn_const(plugin_entry));
			status = 1;
			break;
		}
	}
	slapi_ch_array_free(values);

	values = slapi_entry_attr_get_charray(plugin_entry,
											 ATTR_PLUGIN_BIND_SUBTREE);
	for (i=0; values && values[i]; i++)
	{
		if (plugin_set_subtree_config(&(config->plgc_bind_subtrees), values[i]))
		{
			LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: invalid value %s for attribute %s "
					  "from entry %s\n", values[i], ATTR_PLUGIN_BIND_SUBTREE,
					  slapi_entry_get_dn_const(plugin_entry));
			status = 1;
			break;
		}
		else
		{
			bind_seen = PR_TRUE;
		}
	}
	slapi_ch_array_free(values);

	values = slapi_entry_attr_get_charray(plugin_entry,
											 ATTR_PLUGIN_EXCLUDE_BIND_SUBTREE);
	for (i=0; values && values[i]; i++)
	{
		if (plugin_set_subtree_config(&(config->plgc_excluded_bind_subtrees), values[i]))
		{
			LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: invalid value %s for attribute %s "
					  "from entry %s\n", values[i], ATTR_PLUGIN_EXCLUDE_BIND_SUBTREE,
					  slapi_entry_get_dn_const(plugin_entry));
			status = 1;
			break;
		}
	}
	slapi_ch_array_free(values);

	/* set target subtree default - allow access to all data */
	if (!target_seen)
	{
		plugin_set_global(&(config->plgc_target_subtrees));
	}

	/* set bind subtree default - allow access to local data only */
	if (!bind_seen)
	{
		ptd_set_special_data(&(config->plgc_bind_subtrees), PLGC_DATA_LOCAL);
		ptd_set_special_data(&(config->plgc_bind_subtrees), PLGC_DATA_REMOTE);
	}

	return status;
}

/* This function is called after the plugin init function has been called
   which fills in the desc part of the plugin
*/
static int
add_plugin_description(Slapi_Entry *e, const char *attrname, char *val)
{
	struct berval desc;
	struct berval *newval[2] = {0, 0};
	int status = 0;

	desc.bv_val = SLAPI_PLUGIN_NONE_IF_NULL( val );
	desc.bv_len = strlen(desc.bv_val);
	newval[0] = &desc;
	if ((status = entry_replace_values(e, attrname, newval)) != 0)
	{
        LDAPDebug(LDAP_DEBUG_PLUGIN, "Error: failed to add value %s to "
				  "attribute %s of entry %s\n", val, attrname,
				  slapi_entry_get_dn_const(e));
		status = 1;
	}

	return status;
}


/*
 * The plugin initfunc sets some vendor and version information in the plugin.
 * This function extracts that and adds it as attributes to `e'.  If
 * `plugin' is NULL, the plugin is located based on the DN in `e'.
 *
 * Returns 0 if all goes well and 1 if not.
 */
int
plugin_add_descriptive_attributes( Slapi_Entry *e, struct slapdplugin *plugin )
{
	int		status = 0;

	if ( NULL == plugin ) {
		int					i;
		const Slapi_DN		*ednp = slapi_entry_get_sdn_const( e );
		Slapi_DN			pdn;
		struct slapdplugin	*plugtmp;

		for( i = 0; NULL == plugin && i < PLUGIN_LIST_GLOBAL_MAX; ++i )
		{
			for ( plugtmp = global_plugin_list[i]; NULL == plugin && plugtmp;
						plugtmp = plugtmp->plg_next)
			{
				slapi_sdn_init_normdn_byref( &pdn, plugtmp->plg_dn );
				if ( 0 == slapi_sdn_compare( &pdn, ednp ))
				{
					plugin = plugtmp;
				}
				slapi_sdn_done( &pdn );
			}
		}

		if ( NULL == plugin )
		{
			/* This can happen for things such as disabled syntax plug-ins.  We
			 * just treat this as a warning to allow the description attributes
			 * to be set to a default value to avoid an objectclass violation. */
			LDAPDebug(LDAP_DEBUG_PLUGIN,
					"Warning: couldn't find plugin %s in global list. "
					"Adding default descriptive values.\n",
					slapi_entry_get_dn_const(e), 0, 0 );
		}
	}


	if (add_plugin_description(e, ATTR_PLUGIN_PLUGINID,
	                           plugin ? plugin->plg_desc.spd_id : NULL))
	{
		status = 1;
	}

	if (add_plugin_description(e, ATTR_PLUGIN_VERSION,
	                           plugin ? plugin->plg_desc.spd_version : NULL))
	{
		status = 1;
	}

	if (add_plugin_description(e, ATTR_PLUGIN_VENDOR,
	                           plugin ? plugin->plg_desc.spd_vendor: NULL))
	{
		status = 1;
	}

	if (add_plugin_description(e, ATTR_PLUGIN_DESC,
	                           plugin ? plugin->plg_desc.spd_description : NULL))
	{
		status = 1;
	}

	return status;
}


/*
  clean up the memory associated with the plugin
*/
static void
plugin_free(struct slapdplugin *plugin)
{
	charray_free(plugin->plg_argv);
	slapi_ch_free((void**)&plugin->plg_libpath);
    slapi_ch_free((void**)&plugin->plg_initfunc);
	slapi_ch_free((void**)&plugin->plg_name);
	slapi_ch_free((void**)&plugin->plg_dn);
	if (!plugin->plg_group)
		plugin_config_cleanup(&plugin->plg_conf);
	slapi_ch_free((void**)&plugin);
}

/***********************************
This is the main entry point for plugin configuration.  The plugin_entry argument
should already contain the necessary fields required to initialize the plugin and
to give it a proper name in the plugin configuration DIT.

Argument:
Slapi_Entry *plugin_entry - the required attributes are
	dn: the dn of the plugin entry
	cn: the unique name of the plugin
	nsslapd-pluginType: one of the several recognized plugin types e.g. "postoperation"

if p_initfunc is given, pluginPath and pluginInitFunc are optional
	nsslapd-pluginPath: full path and file name of the dll implementing the plugin
	nsslapd-pluginInitFunc: the name of the plugin initialization function

the optional attributes are:
	nsslapd-pluginArg0
	...
	nsslapd-pluginArg[N-1] - the (old style) arguments to the plugin, where N varies
		from 0 to the number of arguments.  The numbers must be consecutive i.e. no
		skipping

	Instead of using nsslapd-pluginArgN, it is encouraged for you to use named
	parameters e.g.
	nsslapd-tweakThis: 1
	nsslapd-tweakThat: 2
	etc.

	nsslapd-pluginEnabled: "on"|"off" - by default, the plugin will be enabled unless
		this attribute is present and has the value "off"

	for other known attributes, see set_plugin_config_from_entry() above

	all other attributes will be ignored

	The reason this parameter is not const is because it may be modified.  This
	function will modify it if the plugin init function is called successfully
	to add the description attributes, and the plugin init function may modify
	it as well.

Argument:
group - the group to which this plugin will belong - each member of a plugin group
	shares the pluginconfig of the group leader; refer to the function plugin_get_config
	for more information

Argument:
add_entry - if true, the entry will be added to the DIT using the given
	DN in the plugin_entry - this is the default behavior; if false, the
	plugin entry will not show up in the DIT
************************************/
int
plugin_setup(Slapi_Entry *plugin_entry, struct slapi_componentid *group,
		slapi_plugin_init_fnptr p_initfunc, int add_entry)
{
	int ii = 0;
	char attrname[BUFSIZ];
	char *value = 0;
	struct slapdplugin *plugin = NULL;
	struct slapdplugin **plugin_list = NULL;
	struct slapi_componentid *cid=NULL;
	const char *existname = 0;
	slapi_plugin_init_fnptr initfunc = p_initfunc;
	Slapi_PBlock pb;
	int status = 0;
	int enabled = 1;
	char *configdir = 0;

	attrname[0] = '\0';

	if (!slapi_entry_get_sdn_const(plugin_entry))
	{
		LDAPDebug(LDAP_DEBUG_ANY, "Error: DN is missing from the plugin.\n",
				0, 0, 0);
		return -1;
	}

	if ((existname = plugin_exists(slapi_entry_get_sdn_const(plugin_entry))) != NULL)
	{
		LDAPDebug(LDAP_DEBUG_ANY, "Error: the plugin named %s "
				  "already exists.\n", existname, 0, 0);
		return -1;
	}

	/*
	 * create a new plugin structure, fill it in, and prepare to
	 * call the plugin's init function. the init function will
	 * set the plugin function pointers.
	 */
	plugin = (struct slapdplugin *)slapi_ch_calloc(1, sizeof(struct slapdplugin));

	plugin->plg_dn = slapi_ch_strdup(slapi_entry_get_dn_const(plugin_entry));
	plugin->plg_closed = 0;

	if (!(value = slapi_entry_attr_get_charptr(plugin_entry,
											   ATTR_PLUGIN_TYPE)))
	{
		/* error: required attribute %s missing */
		LDAPDebug(LDAP_DEBUG_ANY, "Error: required attribute %s is missing "
				  "from entry \"%s\"\n", ATTR_PLUGIN_TYPE,
				  slapi_entry_get_dn_const(plugin_entry), 0);
		status = -1;
		goto PLUGIN_CLEANUP;
	}
	else
	{
		status = plugin_get_type_and_list(value, &plugin->plg_type,
				&plugin_list);

		if ( status != 0 ) {
			/* error: unknown plugin type */
			LDAPDebug(LDAP_DEBUG_ANY, "Error: unknown plugin type \"%s\" "
					"in entry \"%s\"\n",
					value, slapi_entry_get_dn_const(plugin_entry), 0);
			slapi_ch_free((void**)&value);
			status = -1;
			goto PLUGIN_CLEANUP;
		}
		slapi_ch_free((void**)&value);
	}

	if (!status &&
		!(value = slapi_entry_attr_get_charptr(plugin_entry, "cn")))
	{
		/* error: required attribute %s missing */
		LDAPDebug(LDAP_DEBUG_ANY, "Error: required attribute %s is missing "
				  "from entry \"%s\"\n", "cn",
				  slapi_entry_get_dn_const(plugin_entry), 0);
		status = -1;
		goto PLUGIN_CLEANUP;
	}
	else
	{
		/* plg_name is normalized once here */
		plugin->plg_name = slapi_create_rdn_value("%s", value);
		slapi_ch_free((void**)&value);
	}

	if (!(value = slapi_entry_attr_get_charptr(plugin_entry, ATTR_PLUGIN_PRECEDENCE)))
	{
		/* A precedence isn't set, so just use the default. */
		plugin->plg_precedence = PLUGIN_DEFAULT_PRECEDENCE;
	}
	else
	{
		/* A precedence was set, so let's make sure it's valid. */
		int precedence = 0;
		char *endptr = NULL;

		/* Convert the value. */
		precedence = strtol(value, &endptr, 10);

		/* Make sure the precedence is within our valid
		 * range and that we had no conversion errors. */
		if ((*value == '\0') || (*endptr != '\0') ||
			(precedence < PLUGIN_MIN_PRECEDENCE) || (precedence > PLUGIN_MAX_PRECEDENCE))
		{
			LDAPDebug(LDAP_DEBUG_ANY, "Error: value for attribute %s must be "
				"an integer between %d and %d\n", ATTR_PLUGIN_PRECEDENCE,
				PLUGIN_MIN_PRECEDENCE, PLUGIN_MAX_PRECEDENCE);
			status = -1;
			slapi_ch_free((void**)&value);
			goto PLUGIN_CLEANUP;
		}
		else
		{
			plugin->plg_precedence = precedence;
		}
		slapi_ch_free((void**)&value);
	}

	if (!(value = slapi_entry_attr_get_charptr(plugin_entry,
											   ATTR_PLUGIN_INITFN)))
	{
		if (!initfunc)
		{
			/* error: required attribute %s missing */
			LDAPDebug(LDAP_DEBUG_ANY, "Error: required attribute %s is missing "
					  "from entry \"%s\"\n", ATTR_PLUGIN_INITFN,
					  slapi_entry_get_dn_const(plugin_entry), 0);
			status = -1;
			goto PLUGIN_CLEANUP;
		}
	}
	else
	{
		plugin->plg_initfunc = value; /* plugin owns value's memory now, don't free */
	}

	if (!initfunc) 
	{
		PRBool loadNow = PR_FALSE;
		PRBool loadGlobal = PR_FALSE;

		if (!(value = slapi_entry_attr_get_charptr(plugin_entry,
												   ATTR_PLUGIN_PATH)))
		{
			/* error: required attribute %s missing */
			LDAPDebug(LDAP_DEBUG_ANY, "Error: required attribute %s is missing "
					  "from entry \"%s\"\n", ATTR_PLUGIN_PATH,
					  slapi_entry_get_dn_const(plugin_entry), 0);
			status = -1;
			goto PLUGIN_CLEANUP;
		}
		else
		{
			plugin->plg_libpath = value; /* plugin owns value's memory now, don't free */
		}

		loadNow = slapi_entry_attr_get_bool(plugin_entry, ATTR_PLUGIN_LOAD_NOW);
		loadGlobal = slapi_entry_attr_get_bool(plugin_entry, ATTR_PLUGIN_LOAD_GLOBAL);

		/*
		 * load the plugin's init function
		 */
		if ((initfunc = (slapi_plugin_init_fnptr)sym_load_with_flags(plugin->plg_libpath,
				plugin->plg_initfunc, plugin->plg_name, 1 /* report errors */,
				loadNow, loadGlobal)) == NULL)
		{
			status = -1;
			goto PLUGIN_CLEANUP;
		}
#ifdef _WIN32
		{
			set_debug_level_fn_t fn;
			/* for Win32 only, attempt to get its debug level init function */
			if ((fn = (set_debug_level_fn_t)sym_load(plugin->plg_libpath,
						"plugin_init_debug_level", plugin->plg_name,
						0 /* do not report errors */ )) != NULL) {
				/* we hooked the function, so call it */
				(*fn)(module_ldap_debug); 
			}
		}
#endif
	}

	if (!status && group) /* uses group's config; see plugin_get_config */
	{
		struct slapi_componentid * cid = (struct slapi_componentid *) group;
		plugin->plg_group = (struct slapdplugin  *) cid->sci_plugin;
	}
	else if (!status) /* using own config */
	{
		plugin_config_init(&(plugin->plg_conf));
		set_plugin_config_from_entry(plugin_entry, plugin);
	}

	/* add the plugin arguments */
	value = 0;
	ii = 0;
	PR_snprintf(attrname, sizeof(attrname), "%s%d", ATTR_PLUGIN_ARG, ii);
	while ((value = slapi_entry_attr_get_charptr(plugin_entry, attrname)) != NULL)
	{
		charray_add(&plugin->plg_argv, value);
		plugin->plg_argc++;
		++ii;
		PR_snprintf(attrname, sizeof(attrname), "%s%d", ATTR_PLUGIN_ARG, ii);
	}

	memset((char *)&pb, '\0', sizeof(pb));
	slapi_pblock_set(&pb, SLAPI_PLUGIN, plugin);
	slapi_pblock_set(&pb, SLAPI_PLUGIN_VERSION, (void *)SLAPI_PLUGIN_CURRENT_VERSION);

        cid = generate_componentid (plugin,NULL);
        slapi_pblock_set(&pb, SLAPI_PLUGIN_IDENTITY, (void*)cid);

	configdir = config_get_configdir();
	slapi_pblock_set(&pb, SLAPI_CONFIG_DIRECTORY, configdir);

	/* see if the plugin is enabled or not */
	if ((value = slapi_entry_attr_get_charptr(plugin_entry,
											  ATTR_PLUGIN_ENABLED)) &&
		!strcasecmp(value, "off"))
	{
		enabled = 0;
	}
	else
	{
		enabled = 1;
	}

	slapi_pblock_set(&pb, SLAPI_PLUGIN_ENABLED, &enabled);
	slapi_pblock_set(&pb, SLAPI_PLUGIN_CONFIG_ENTRY, plugin_entry);

	if (enabled && (*initfunc)(&pb) != 0)
	{
        LDAPDebug(LDAP_DEBUG_ANY, "Init function \"%s\" for \"%s\" plugin"
				 " in library \"%s\" failed\n",
				  plugin->plg_initfunc, plugin->plg_name,
				  plugin->plg_libpath);
        status = -1;
		slapi_ch_free((void**)&value);
		goto PLUGIN_CLEANUP;
	}

	if ( !status ) {
		status = plugin_add_descriptive_attributes( plugin_entry, plugin );
	}

	slapi_ch_free((void**)&value);

	if(enabled)
	{
		/* don't use raw pointer from plugin_entry because it
		   will be freed later by the caller */
		Slapi_DN *dn_copy = slapi_sdn_dup(slapi_entry_get_sdn_const(plugin_entry));
		add_plugin_to_list(plugin_list, plugin);
		add_plugin_entry_dn(dn_copy);
	}

	if (add_entry)
	{
		/* make a copy of the plugin entry for our own use because it will
		   be freed later by the caller */
		Slapi_Entry *e_copy = slapi_entry_dup(plugin_entry);
		/* new_plugin_entry(&plugin_entries, plugin_entry, plugin); */
		new_plugin_entry(&dep_plugin_entries, e_copy, plugin);
	}


PLUGIN_CLEANUP:
	if (status)
		plugin_free(plugin);
	slapi_ch_free((void **)&configdir);

	return status;
}

/* set default configuration parameters */
static void 
plugin_config_init (struct pluginconfig *config)
{	   
	PR_ASSERT (config);

	ptd_init (&config->plgc_target_subtrees);
	ptd_init (&config->plgc_excluded_target_subtrees);
	ptd_init (&config->plgc_bind_subtrees);
	ptd_init (&config->plgc_excluded_bind_subtrees);
	config->plgc_schema_check = PLGC_ON;
	config->plgc_invoke_for_replop = PLGC_ON;
	/* currently, we leave it up to plugin, but don't actually tell plugins that they can choose.
	   We want changes to always be logged by regular plugins to avoid data inconsistency, but we
	   want to allow internal plugins like replication to make the decision.*/
	config->plgc_log_change = PLGC_UPTOPLUGIN;	
	config->plgc_log_access = PLGC_OFF;
	config->plgc_log_audit = PLGC_OFF;
}

static int 
plugin_config_set_action (int *action, char *value)
{
	PR_ASSERT (action);
	PR_ASSERT (value);

	if (strcasecmp (value, "on") == 0)
	{
		*action = PLGC_ON;
	}
	else if (strcasecmp (value, "off") == 0)
	{
		*action = PLGC_OFF;
	}
	else if (strcasecmp (value, "uptoplugin") == 0)
	{
		*action = PLGC_UPTOPLUGIN;
	}
	else
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
						"plugin_config_set_action: invalid action %s\n", value);
		return -1;
	}

	return 0;	
}

static void 
plugin_config_cleanup (struct pluginconfig *config)
{
	PR_ASSERT (config);

	ptd_cleanup (&config->plgc_target_subtrees);
	ptd_cleanup (&config->plgc_excluded_target_subtrees);
	ptd_cleanup (&config->plgc_bind_subtrees);
	ptd_cleanup (&config->plgc_excluded_bind_subtrees);
}

#if 0
static char* 
plugin_config_action_to_string (int action)
{
	switch (action)
	{
		case PLGC_ON:			return "on";
		case PLGC_OFF:			return "off";
		case PLGC_UPTOPLUGIN:	return "uptoplugin";
		default:				return NULL;
	}
}
#endif

static struct pluginconfig* 
plugin_get_config (struct slapdplugin *plugin)
{
	struct slapdplugin	*temp = plugin;

	PR_ASSERT (plugin);

	while (temp->plg_group)
	{
		temp = temp->plg_group;
	}

	return &(temp->plg_conf);
}

static PRBool 
plugin_invoke_plugin_pb (struct slapdplugin *plugin, int operation, Slapi_PBlock *pb)
{
	Slapi_DN *target_spec;
	PRBool rc;

	PR_ASSERT (plugin);
	PR_ASSERT (pb);	

	/* we always allow initialization and cleanup operations */
	if (operation == SLAPI_PLUGIN_START_FN || 
		operation == SLAPI_PLUGIN_POSTSTART_FN ||
		operation == SLAPI_PLUGIN_CLOSE_FN || 
		operation == SLAPI_PLUGIN_CLEANUP_FN ||
		operation == SLAPI_PLUGIN_BE_PRE_CLOSE_FN ||
		operation == SLAPI_PLUGIN_BE_POST_OPEN_FN ||
		operation == SLAPI_PLUGIN_BE_PRE_BACKUP_FN ||
		operation == SLAPI_PLUGIN_BE_POST_BACKUP_FN)
		return PR_TRUE;

	PR_ASSERT (pb->pb_op);

	target_spec = operation_get_target_spec (pb->pb_op);
	
	PR_ASSERT (target_spec);

	rc = plugin_invoke_plugin_sdn (plugin, operation, pb, target_spec);

	return rc;
}

PRBool
plugin_invoke_plugin_sdn (struct slapdplugin *plugin, int operation, Slapi_PBlock *pb, Slapi_DN *target_spec)
{
	PluginTargetData *ptd;
	PluginTargetData *excludedPtd;
	struct pluginconfig *config;
	Slapi_Backend *be;
	int isroot;
	PRBool islocal;
	PRBool bindop;
	unsigned long op;
	int method = -1;

	PR_ASSERT (plugin);
	if (!pb) {
		LDAPDebug(LDAP_DEBUG_ANY, "plugin_invoke_plugin_sdn: NULL pblock.\n", 0, 0, 0);
		return PR_FALSE;
	}

	/* get configuration from the group plugin if necessary */
	config = plugin_get_config (plugin);
	slapi_pblock_get(pb, SLAPI_BIND_METHOD, &method);
	/* check if plugin is configured to service replicated operations */
	if (!config->plgc_invoke_for_replop)
	{
		int repl_op;

		slapi_pblock_get (pb, SLAPI_IS_REPLICATED_OPERATION, &repl_op);
		if (repl_op)
			return PR_FALSE;
	}

	if (pb->pb_op)
	{
		op = operation_get_type(pb->pb_op);

		if (op == SLAPI_OPERATION_BIND || op == SLAPI_OPERATION_UNBIND)
		{
			bindop = PR_TRUE;
		}
		else
		{
			bindop = PR_FALSE;
		}

		slapi_pblock_get (pb, SLAPI_REQUESTOR_ISROOT, &isroot);
	}
	else
	{
		bindop = PR_FALSE;
		isroot = 1;
	}

	slapi_pblock_get (pb, SLAPI_BACKEND, &be);

	/* determine whether data are local or remote    */
	/* remote if chaining backend or default backend */

	if ( be!=NULL ) {
		islocal=!(slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA));
	} else {
		islocal = be != defbackend_get_backend();
	}

	if (bindop)
	{
		ptd = &(config->plgc_bind_subtrees); 
		excludedPtd = &(config->plgc_excluded_bind_subtrees); 
	}
	else
	{
		ptd = &(config->plgc_target_subtrees);
		excludedPtd = &(config->plgc_excluded_target_subtrees);
	}

	if (plugin_matches_operation (target_spec, excludedPtd, bindop, isroot, islocal, method) == PR_TRUE) {
		return PR_FALSE;
	}

	return plugin_matches_operation (target_spec, ptd, bindop, isroot, islocal, method);
}

/* this interface is exposed to be used by internal operations. 
 */
char* plugin_get_dn (const struct slapdplugin *plugin)
{
	char *plugindn = NULL;
	char *pattern = "cn=%s," PLUGIN_BASE_DN; /* cn=plugins,cn=config */

	if (plugin == NULL)	/* old plugin that does not pass identity - use default */
		plugin = &global_default_plg;

	if (plugin->plg_name == NULL)
		return NULL;

	/* plg_name is normalized in plugin_setup. So, we can use smprintf */
	plugindn = slapi_ch_smprintf(pattern, plugin->plg_name);
	if (NULL == plugindn) {
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
					"plugin_get_dn: failed to create plugin dn "
					"(plugin name: %s)\n", plugin->plg_name);
		return NULL;
	}
	return plugindn;
}

static PRBool plugin_is_global (const PluginTargetData *ptd)
{
	/* plugin is considered to be global if it is invoked for
	   global data, local data and anonymous bind (bind target
       data only). We don't include directory manager here
	   as it is considered to be part of local data */
	return (ptd_is_special_data_set (ptd, PLGC_DATA_LOCAL) &&
			ptd_is_special_data_set (ptd, PLGC_DATA_REMOTE) &&
			ptd_is_special_data_set (ptd, PLGC_DATA_BIND_ANONYMOUS) &&
			ptd_is_special_data_set (ptd, PLGC_DATA_BIND_ROOT));
}

static void plugin_set_global (PluginTargetData *ptd)
{
	PR_ASSERT (ptd);

	/* plugin is global if it is allowed access to all data */
	ptd_set_special_data (ptd, PLGC_DATA_LOCAL);
	ptd_set_special_data (ptd, PLGC_DATA_REMOTE);
	ptd_set_special_data (ptd, PLGC_DATA_BIND_ANONYMOUS);
	ptd_set_special_data (ptd, PLGC_DATA_BIND_ROOT);
}  

static void plugin_set_default_access (struct pluginconfig *config)
{
	/* by default, plugins are invoked if dn is local for bind operations,
	   and for all requests for all other operations */
	PR_ASSERT (config);

	plugin_set_global (&config->plgc_target_subtrees);
	ptd_set_special_data (&config->plgc_bind_subtrees, PLGC_DATA_LOCAL);
	ptd_set_special_data (&config->plgc_bind_subtrees, PLGC_DATA_REMOTE);
}

/* determine whether operation should be allowed based on plugin configuration */
PRBool plugin_allow_internal_op (Slapi_DN *target_spec, struct slapdplugin *plugin)
{
	struct pluginconfig *config = plugin_get_config (plugin);
	Slapi_Backend *be;
	int islocal;
	
	if (plugin_is_global (&config->plgc_excluded_target_subtrees))
		return PR_FALSE;

	if (plugin_is_global (&config->plgc_target_subtrees))
		return PR_TRUE;

	/* ONREPL - we do be_select to decide whether the request is for local
				or remote data. We might need to reconsider how to do this
				for performance reasons since be_select will be done again
				once the operation goes through */
	be = slapi_be_select(target_spec);

        /* determine whether data are local or remote    */ 
        /* remote if chaining backend or default backend */ 
 
        if ( be!=NULL ) {
       		islocal=!(slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA));
        } else { 
                islocal = be != defbackend_get_backend();
        } 

	/* SIMPLE auth method sends us through original code path in plugin_mathches_operation */

	if (plugin_matches_operation (target_spec, &config->plgc_excluded_target_subtrees,
									  PR_FALSE, PR_FALSE, islocal, LDAP_AUTH_SIMPLE) == PR_TRUE) {
		return PR_FALSE;
	}

	return plugin_matches_operation (target_spec, &config->plgc_target_subtrees,
									  PR_FALSE, PR_FALSE, islocal, LDAP_AUTH_SIMPLE);
}

static PRBool plugin_matches_operation (Slapi_DN *target_spec, PluginTargetData *ptd, 
										PRBool bindop, PRBool isroot, PRBool islocal, int method)
{
	int cookie;
	Slapi_DN *subtree;

	/* check for special cases */

	if (plugin_is_global (ptd))
		return PR_TRUE;

	/* if method is SASL we can have a null DN so bypass this check*/
    if(method != LDAP_AUTH_SASL) {
        if (bindop && target_spec && (slapi_sdn_get_dn (target_spec) == NULL || 
            slapi_sdn_get_dn (target_spec)[0] == '\0'))
        {
            return (ptd_is_special_data_set (ptd, PLGC_DATA_BIND_ANONYMOUS));			
        }
    }

	/* check for root bind */
	if (bindop && isroot)
	{
		return (ptd_is_special_data_set (ptd, PLGC_DATA_BIND_ROOT));
	}

	/* check for local data */
	if (ptd_is_special_data_set (ptd, PLGC_DATA_LOCAL) && islocal)
	{		
		return PR_TRUE;
	}

	/* check for remote data */
	if (ptd_is_special_data_set (ptd, PLGC_DATA_REMOTE) && !islocal)
	{
		return (PR_TRUE);
	}	
		
	subtree = ptd_get_first_subtree (ptd, &cookie);
	while (subtree)
	{
		if (slapi_sdn_issuffix (target_spec, subtree))
			return (PR_TRUE);

		subtree = ptd_get_next_subtree (ptd, &cookie);
	}

	return PR_FALSE;
	
}

/* build operation action bitmap based on plugin configuration and actions specified for the operation */
int plugin_build_operation_action_bitmap (int input_actions, const struct slapdplugin *plugin)
{
	int result_actions = 0;

	/* old plugin that does not pass its identity to the operation */
	if (plugin == NULL)
		plugin = &global_default_plg;

	if (plugin->plg_conf.plgc_log_access)
		result_actions |= OP_FLAG_ACTION_LOG_ACCESS;

	if (plugin->plg_conf.plgc_log_audit)
		result_actions |= OP_FLAG_ACTION_LOG_AUDIT;

	/*
	 * OP_FLAG_ACTION_INVOKE_FOR_REPLOP is now used only by URP code.
	 * If someday this code needs to reclaim the flag, it has to use
	 * another flag to avoid the conflict with URP code.
	 *
	 * if (plugin->plg_conf.plgc_invoke_for_replop)
	 *	result_actions |= OP_FLAG_ACTION_INVOKE_FOR_REPLOP;
	 */

	switch (plugin->plg_conf.plgc_schema_check)
	{
		case PLGC_OFF:			result_actions &= ~OP_FLAG_ACTION_SCHEMA_CHECK;
								break;

		case PLGC_ON:			result_actions |= OP_FLAG_ACTION_SCHEMA_CHECK;
								break;

		case PLGC_UPTOPLUGIN:	break;

		default:				PR_ASSERT (PR_FALSE);
	}
		
	switch (plugin->plg_conf.plgc_log_change)
	{
		case PLGC_OFF:			result_actions &= ~OP_FLAG_ACTION_LOG_CHANGES;
								break;

		case PLGC_ON:			result_actions |= OP_FLAG_ACTION_LOG_CHANGES;
								break;

		case PLGC_UPTOPLUGIN:	break;

		default:				PR_ASSERT (PR_FALSE);
	}

	return result_actions;
}

const struct slapdplugin*
plugin_get_server_plg()
{
	if(!global_server_plg_initialised)
	{
		global_server_plg.plg_name = "server";
		plugin_set_global (&global_server_plg.plg_conf.plgc_target_subtrees);
		global_server_plg.plg_conf.plgc_log_access = 1;
		global_server_plg.plg_conf.plgc_log_audit = 1;
		global_server_plg.plg_conf.plgc_schema_check = 1;
		global_server_plg.plg_conf.plgc_log_change = 1;
		global_server_plg_initialised= 1;
		global_server_plg_initialised= 1;
	}
	return &global_server_plg;
}

struct slapi_componentid * plugin_get_default_component_id() {

        if(!global_server_plg_id_initialised) {
                global_server_id_plg.sci_plugin=plugin_get_server_plg();
                global_server_id_plg.sci_component_name=
			plugin_get_dn(global_server_id_plg.sci_plugin);
	        global_server_plg_id_initialised=1;
        }
        return &global_server_id_plg;
}

static void
default_plugin_init()
{
	global_default_plg.plg_name = "old plugin";
	plugin_config_init (&global_default_plg.plg_conf);
	plugin_set_default_access (&global_default_plg.plg_conf);
}

#if 0
static void trace_plugin_invocation (Slapi_DN *target_spec, PluginTargetData *ptd, 
									 PRBool bindop, PRBool isroot, PRBool islocal, int invoked)
{  
	int cookie, i = 0;
	Slapi_DN *sdn;

	
	slapi_log_error (SLAPI_LOG_FATAL, NULL,
					 "Invocation parameters: target_spec = %s, bindop = %d, isroot=%d, islocal=%d\n"
					 "Plugin configuration: local_data=%d, remote_data=%d, anonymous_bind=%d, root_bind=%d\n",
					 slapi_sdn_get_ndn (target_spec), bindop, isroot, islocal, ptd->special_data[0],
					 ptd->special_data[1], ptd->special_data[2], ptd->special_data[3]);

	sdn = ptd_get_first_subtree (ptd, &cookie);
	while (sdn)
	{
		slapi_log_error (SLAPI_LOG_FATAL, NULL, "target_subtree%d: %s\n", i, slapi_sdn_get_ndn (sdn));
		sdn = ptd_get_next_subtree (ptd, &cookie);					 	
	}
   
	slapi_log_error (SLAPI_LOG_FATAL, NULL, invoked ? "Plugin is invoked\n" : "Plugin is not invoked\n");
}
#endif

/* functions to manipulate PluginTargetData type */
static void ptd_init (PluginTargetData *ptd)
{
	PR_ASSERT (ptd);

	dl_init (&ptd->subtrees, 0 /* initial count */);
	memset (&ptd->special_data, 0, sizeof (ptd->special_data));
}

static void ptd_cleanup (PluginTargetData *ptd)
{
	PR_ASSERT (ptd);

	dl_cleanup (&ptd->subtrees, (FREEFN)slapi_sdn_free);
	memset (&ptd->special_data, 0, sizeof (ptd->special_data));
}

static void ptd_add_subtree (PluginTargetData *ptd, Slapi_DN *subtree)
{
	PR_ASSERT (ptd);
	PR_ASSERT (subtree);

	dl_add (&ptd->subtrees, subtree);
}

static void ptd_set_special_data (PluginTargetData *ptd, int type)
{
	PR_ASSERT (ptd);
	PR_ASSERT (type >= 0 && type < PLGC_DATA_MAX);

	ptd->special_data [type] = PR_TRUE;
}

#if 0
static void ptd_clear_special_data (PluginTargetData *ptd, int type)
{
	PR_ASSERT (ptd);
	PR_ASSERT (type >= 0 && type < PLGC_DATA_MAX);

	ptd->special_data [type] = PR_FALSE;
}
#endif

static Slapi_DN *ptd_get_first_subtree (const PluginTargetData *ptd, int *cookie)
{
	PR_ASSERT (ptd);

	return dl_get_first (&ptd->subtrees, cookie);
}

static Slapi_DN *ptd_get_next_subtree (const PluginTargetData *ptd, int *cookie)
{
	PR_ASSERT (ptd);

	return dl_get_next (&ptd->subtrees, cookie);
}

static PRBool ptd_is_special_data_set (const PluginTargetData *ptd, int type)
{
	PR_ASSERT (ptd);
	PR_ASSERT (type >= 0 && type < PLGC_DATA_MAX);

	return ptd->special_data [type];
}

#if 0
static Slapi_DN* ptd_delete_subtree (PluginTargetData *ptd, Slapi_DN *subtree)
{
	PR_ASSERT (ptd);
	PR_ASSERT (subtree);

	return (Slapi_DN*)dl_delete (&ptd->subtrees, subtree, (CMPFN)slapi_sdn_compare, NULL);
}
#endif

int ptd_get_subtree_count (const PluginTargetData *ptd)
{
	PR_ASSERT (ptd);

	return dl_get_count (&ptd->subtrees);
}

/* needed by command-line tasks to find an instance's plugin */
struct slapdplugin *plugin_get_by_name(char *name)
{
	int x;
	struct slapdplugin *plugin;
	
	for(x = 0; x < PLUGIN_LIST_GLOBAL_MAX; x++) {
		for(plugin = global_plugin_list[x]; plugin; plugin = plugin->plg_next) {
			if (!strcmp(name, plugin->plg_name)) {
				return plugin;
			}
		}
	}
	
	return NULL;
}

struct slapi_componentid *
generate_componentid ( struct slapdplugin * pp , char * name )
{
        struct slapi_componentid * idp;

        idp = (struct slapi_componentid *) slapi_ch_calloc(1, sizeof( *idp ));
        if ( pp )
                idp->sci_plugin=pp;
        else
                idp->sci_plugin=(struct slapdplugin *) plugin_get_server_plg();

        if ( name )
                idp->sci_component_name = slapi_ch_strdup(name);
        else
                /* Use plugin dn */
                idp->sci_component_name = plugin_get_dn( idp->sci_plugin );

	if (idp->sci_component_name)
		slapi_dn_normalize(idp->sci_component_name);
        return idp;
}

void release_componentid ( struct slapi_componentid * id )
{
        if ( id ) {
                if ( id->sci_component_name ) {
                        slapi_ch_free((void **)&id->sci_component_name);
                        id->sci_component_name=NULL;
                }
                slapi_ch_free((void **)&id);
        }
}

/* used in main.c if -V flag is given */

static void slapd_print_plugin_version (
	struct slapdplugin *plg,
	struct slapdplugin *prev
)
{
	if (plg == NULL || plg->plg_libpath == NULL) return;

	/* same library as previous - don't print twice */
	if (prev != NULL && prev->plg_libpath != NULL) {
	  if (strcmp(prev->plg_libpath,plg->plg_libpath) == 0) {
	    return;
	  }
	}

	printf("%s: %s\n",
	       plg->plg_libpath,
	       plg->plg_desc.spd_version ? plg->plg_desc.spd_version : "");
}

static void slapd_print_pluginlist_versions(struct slapdplugin *plg)
{
	struct slapdplugin *p,*prev = NULL;

	for (p = plg; p != NULL; p = p->plg_next) {
	  slapd_print_plugin_version(p,prev);
	  prev = p;
	}
}

void plugin_print_versions(void)
{
  	int i;
	
	for (i = 0; i < PLUGIN_LIST_GLOBAL_MAX; i++) {
	  slapd_print_pluginlist_versions(get_plugin_list(i));
	}

}

/*
 * Prints a list of plugins in execution order for each
 * plug-in type.  This will only be printed at the
 * SLAPI_LOG_PLUGIN log level.
 */
void plugin_print_lists(void)
{
	int i;
	struct slapdplugin *list = NULL;
	struct slapdplugin *tmp = NULL;

	for (i = 0; i < PLUGIN_LIST_GLOBAL_MAX; i++) {
		if ((list = get_plugin_list(i)))
		{
			slapi_log_error(SLAPI_LOG_PLUGIN, NULL,
				"---- Plugin List (type %d) ----\n", i);
			for ( tmp = list; tmp; tmp = tmp->plg_next )
			{
				slapi_log_error(SLAPI_LOG_PLUGIN, NULL, "  %s (precedence: %d)\n",
					tmp->plg_name, tmp->plg_precedence);
			}
		}
	}
}

/*
 * check the spedified plugin entry and its nssladp-pluginEnabled value
 * Return Value: 1 if the plugin is on.
 *             : 0 otherwise.
 */
int
plugin_enabled(const char *plugin_name, void *identity)
{
	Slapi_PBlock *search_pb = NULL;
	Slapi_Entry **entries = NULL, **ep = NULL;
	Slapi_Value *on_off = slapi_value_new_string("on");
	char *filter = NULL;
	int rc = 0;	/* disabled, by default */

	filter = slapi_filter_sprintf("cn=%s%s", ESC_NEXT_VAL, plugin_name);
	search_pb = slapi_pblock_new();
	slapi_search_internal_set_pb(search_pb, PLUGIN_BASE_DN, LDAP_SCOPE_ONELEVEL,
								 filter, NULL, 0, NULL, NULL, identity, 0);
	slapi_search_internal_pb(search_pb);
	slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
	if (LDAP_SUCCESS != rc) { /* plugin is not available */
		rc = 0; /* disabled, by default */
		goto bail;
	}

	slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
	for (ep = entries; ep && *ep; ep++) {
		if (slapi_entry_attr_has_syntax_value(*ep, "nsslapd-pluginEnabled", on_off)) {
			rc = 1; /* plugin is on */
			goto bail;
		}
	}

bail:
	slapi_value_free(&on_off);
	slapi_free_search_results_internal(search_pb);
	slapi_pblock_destroy(search_pb);
	slapi_ch_free_string(&filter);

	return rc;
}

/*
 * Set given "type: attr" to the plugin default config entry
 * (cn=plugin default config,cn=config) unless the same "type: attr" pair
 * already exists in the entry.
 */ 
int
slapi_set_plugin_default_config(const char *type, Slapi_Value *value)
{
    Slapi_PBlock pb;
    Slapi_Entry **entries = NULL;
    int rc = LDAP_SUCCESS;
    char **search_attrs = NULL; /* used by search */

    if (NULL == type || '\0' == *type || NULL == value ) { /* nothing to do */
        return rc;
    }

    charray_add(&search_attrs, slapi_ch_strdup(type));

    /* cn=plugin default config,cn=config */
    pblock_init(&pb);
    slapi_search_internal_set_pb(&pb,
                    SLAPI_PLUGIN_DEFAULT_CONFIG, /* Base DN (normalized) */
                    LDAP_SCOPE_BASE,
                    "(objectclass=*)",
                    search_attrs, /* Attrs */
                    0, /* AttrOnly */
                    NULL, /* Controls */
                    NULL, /* UniqueID */
                    (void *)plugin_get_default_component_id(),
                    0);
    slapi_search_internal_pb(&pb);
    slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (LDAP_SUCCESS == rc && entries && *entries) {
        /* plugin default config entry exists */
        int exists = 0;
        Slapi_Attr *attr = NULL;
        rc = slapi_entry_attr_find(*entries, type, &attr);

        if (0 == rc) { /* type exists in the entry */
            if (0 == 
                slapi_attr_value_find(attr, slapi_value_get_berval(value))) {
                /* value exists in the entry; we don't have to do anything. */
                exists = 1;
            }
        }
        slapi_free_search_results_internal(&pb);
        pblock_done(&pb);

        if (!exists) {
            /* The argument attr is not in the plugin default config.
             * Let's add it. */
            Slapi_Mods smods;
            Slapi_Value *va[2];

            va[0] = value;
            va[1] = NULL;
            slapi_mods_init(&smods, 1);
            slapi_mods_add_mod_values(&smods, LDAP_MOD_ADD, type, va);
    
            pblock_init(&pb);
            slapi_modify_internal_set_pb(&pb, SLAPI_PLUGIN_DEFAULT_CONFIG,
                                      slapi_mods_get_ldapmods_byref(&smods),
                                      NULL, NULL, /* UniqueID */
                                      (void *)plugin_get_default_component_id(),
                                      0 /* Flags */ );
            slapi_modify_internal_pb(&pb);
            slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
            slapi_mods_done(&smods);
            pblock_done(&pb);
        }
    } else { /* cn=plugin default config does not exist. Let's add it. */
        Slapi_Mods smods;
        Slapi_Value *va[2];

        slapi_free_search_results_internal(&pb);
        pblock_done(&pb);

        va[0] = value;
        va[1] = NULL;
        slapi_mods_init(&smods, 1);

        slapi_mods_add_string(&smods, LDAP_MOD_ADD, "objectClass", "top");
        slapi_mods_add_string(&smods, LDAP_MOD_ADD, "objectClass",
                                                        "extensibleObject");
        slapi_mods_add_mod_values(&smods, LDAP_MOD_ADD, type, va);

        pblock_init(&pb);
        slapi_add_internal_set_pb(&pb, SLAPI_PLUGIN_DEFAULT_CONFIG,
                                  slapi_mods_get_ldapmods_byref(&smods), NULL, 
                                  (void *)plugin_get_default_component_id(),
                                  0 /* Flags */ );
        slapi_add_internal_pb(&pb);
        slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
        slapi_mods_done(&smods);
        pblock_done(&pb);
    }
    charray_free(search_attrs);

    return rc;
}

/*
 * Get attribute values of given type from the plugin default config entry
 * (cn=plugin default config,cn=config).
 *
 * Caller is responsible to free attrs by slapi_valueset_free.
 */
int
slapi_get_plugin_default_config(char *type, Slapi_ValueSet **valueset)
{
    Slapi_PBlock pb;
    Slapi_Entry **entries = NULL;
    int rc = LDAP_PARAM_ERROR;
    char **search_attrs = NULL; /* used by search */

    if (NULL == type || '\0' == *type || NULL == valueset) { /* nothing to do */
        return rc;
    }

    charray_add(&search_attrs, slapi_ch_strdup(type));

    /* cn=plugin default config,cn=config */
    pblock_init(&pb);
    slapi_search_internal_set_pb(&pb,
                    SLAPI_PLUGIN_DEFAULT_CONFIG, /* Base DN (normalized) */
                    LDAP_SCOPE_BASE,
                    "(objectclass=*)",
                    search_attrs, /* Attrs */
                    0, /* AttrOnly */
                    NULL, /* Controls */
                    NULL, /* UniqueID */
                    (void *)plugin_get_default_component_id(),
                    0);
    slapi_search_internal_pb(&pb);
    slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    slapi_pblock_get(&pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (LDAP_SUCCESS == rc && entries && *entries) {
        /* default config entry exists */
        /* retrieve attribute values from the entry */
        Slapi_Attr *attr = NULL;
        rc = slapi_entry_attr_find(*entries, type, &attr);
		if (0 == rc) { /* type value exists */
			rc = slapi_attr_get_valueset(attr, valueset);
		} else {
			rc = LDAP_NO_SUCH_ATTRIBUTE;
		}
    }
    slapi_free_search_results_internal(&pb);
    pblock_done(&pb);
    charray_free(search_attrs);

    return rc;
}

void
slapi_set_plugin_open_rootdn_bind(Slapi_PBlock *pb){
	struct pluginconfig *config = &pb->pb_plugin->plg_conf;

	ptd_set_special_data(&(config->plgc_bind_subtrees), PLGC_DATA_BIND_ROOT);
}
