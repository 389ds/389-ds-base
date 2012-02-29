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
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif



#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "slap.h"
#include "cert.h"

void
pblock_init( Slapi_PBlock *pb )
{
	memset( pb, '\0', sizeof(Slapi_PBlock) );
}

void
pblock_init_common(
    Slapi_PBlock	*pb,
    Slapi_Backend	*be,
    Connection	*conn,
    Operation	*op
)
{
	PR_ASSERT( NULL != pb );
	memset( pb, '\0', sizeof(Slapi_PBlock) );
	pb->pb_backend = be;
	pb->pb_conn = conn;
	pb->pb_op = op;
}

void
slapi_pblock_get_common(
    Slapi_PBlock	*pb,
    Slapi_Backend	**be,
    Connection	**conn,
    Operation	**op
)
{
	PR_ASSERT( NULL != pb );
	PR_ASSERT( NULL != be );
	PR_ASSERT( NULL != conn );
	PR_ASSERT( NULL != op );
	*be = pb->pb_backend;
	*conn = pb->pb_conn;
	*op = pb->pb_op;
}

Slapi_PBlock *
slapi_pblock_new()
{
	Slapi_PBlock	*pb;

	pb = (Slapi_PBlock *) slapi_ch_calloc( 1, sizeof(Slapi_PBlock) );
	return pb;
}

void
slapi_pblock_init( Slapi_PBlock *pb )
{
	if(pb!=NULL)
	{
		pblock_done(pb);
		pblock_init(pb);
	}
}

void
pblock_done( Slapi_PBlock *pb )
{
    if(pb->pb_op!=NULL)
    {
	    operation_free(&pb->pb_op,pb->pb_conn);
    }
	slapi_ch_free((void**)&(pb->pb_vattr_context));
	slapi_ch_free((void**)&(pb->pb_result_text));
}

void
slapi_pblock_destroy( Slapi_PBlock* pb )
{
	if(pb!=NULL)
	{
		pblock_done(pb);
	    slapi_ch_free((void**)&pb);
	}
}

/* JCM - when pb_o_params is used, check the operation type. */
/* JCM - when pb_o_results is used, check the operation type. */

#define SLAPI_PLUGIN_TYPE_CHECK(PBLOCK,TYPE) \
if ( PBLOCK ->pb_plugin->plg_type != TYPE) return( -1 )


/*
 * Macro used to safely retrieve a plugin related pblock value (if the
 * pb_plugin element is NULL, NULL is returned).
 */
#define SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER( pb, element ) \
		((pb)->pb_plugin == NULL ? NULL : (pb)->pb_plugin->element)


int
slapi_pblock_get( Slapi_PBlock *pblock, int arg, void *value )
{
    char *authtype;
	Slapi_Backend		*be;

	PR_ASSERT( NULL != pblock );
	PR_ASSERT( NULL != value );
	be = pblock->pb_backend;

	switch ( arg ) {
	case SLAPI_BACKEND:
		(*(Slapi_Backend **)value) = be;
		break;
	case SLAPI_BACKEND_COUNT:
		(*(int *)value) = pblock->pb_backend_count;
		break;
	case SLAPI_BE_TYPE:
		if ( NULL == be ) {
			return( -1 );
		}
		(*(char **)value) = be->be_type;
		break;
	case SLAPI_BE_READONLY:
		if ( NULL == be ) {
			(*(int *)value) = 0;	/* default value */
		} else {
			(*(int *)value) = be->be_readonly;
		}
		break;
	case SLAPI_BE_LASTMOD:
		if ( NULL == be ) {
			(*(int *)value) = (g_get_global_lastmod() == LDAP_ON);
		} else {
			(*(int *)value) = (be->be_lastmod == LDAP_ON || (be->be_lastmod
					== LDAP_UNDEFINED && g_get_global_lastmod() == LDAP_ON));
		}
		break;
	case SLAPI_CONNECTION:
		(*(Connection **)value) = pblock->pb_conn;
		break;
	case SLAPI_CONN_ID:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_ID \n", 0, 0, 0 );
			return (-1);
		}
		(*(PRUint64 *)value) = pblock->pb_conn->c_connid;
		break;
	case SLAPI_CONN_DN:
		/*
		 * NOTE: we have to make a copy of this that the caller
		 * is responsible for freeing. otherwise, they would get
		 * a pointer that could be freed out from under them.
		 */
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_DN \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		(*(char **)value) = (NULL == pblock->pb_conn->c_dn ? NULL :
		    slapi_ch_strdup( pblock->pb_conn->c_dn ));
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_AUTHTYPE:/* deprecated */
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_AUTHTYPE \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
                authtype = pblock->pb_conn->c_authtype;
		PR_Unlock( pblock->pb_conn->c_mutex );
                if (authtype == NULL) {
                    (*(char **)value) = NULL;
                } else if (strcasecmp(authtype, SLAPD_AUTH_NONE) == 0) {
                    (*(char **)value) = SLAPD_AUTH_NONE;
                } else if (strcasecmp(authtype, SLAPD_AUTH_SIMPLE) == 0) {
                    (*(char **)value) = SLAPD_AUTH_SIMPLE;
                } else if (strcasecmp(authtype, SLAPD_AUTH_SSL) == 0) {
                    (*(char **)value) = SLAPD_AUTH_SSL;
                } else if (strcasecmp(authtype, SLAPD_AUTH_OS) == 0) {
                    (*(char **)value) = SLAPD_AUTH_OS;
                } else if (strncasecmp(authtype, SLAPD_AUTH_SASL, 
                                       strlen(SLAPD_AUTH_SASL)) == 0) {
                    (*(char **)value) = SLAPD_AUTH_SASL;
                } else {
                    (*(char **)value) = "unknown";
                }
		break;
	case SLAPI_CONN_AUTHMETHOD:
                /* returns a copy */
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_AUTHMETHOD \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		(*(char **)value) = pblock->pb_conn->c_authtype ?
                    slapi_ch_strdup(pblock->pb_conn->c_authtype) : NULL;
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_CLIENTNETADDR:
		if (pblock->pb_conn == NULL)
		{
			memset( value, 0, sizeof( PRNetAddr ));
			break;
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		if ( pblock->pb_conn->cin_addr == NULL ) {
			memset( value, 0, sizeof( PRNetAddr ));
		} else {
			(*(PRNetAddr *)value) =
			    *(pblock->pb_conn->cin_addr);
		}
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_SERVERNETADDR:
		if (pblock->pb_conn == NULL)
		{
			memset( value, 0, sizeof( PRNetAddr ));
			break;
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		if ( pblock->pb_conn->cin_destaddr == NULL ) {
			memset( value, 0, sizeof( PRNetAddr ));
		} else {
			(*(PRNetAddr *)value) =
				*(pblock->pb_conn->cin_destaddr);
		}
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_CLIENTIP:
		if (pblock->pb_conn == NULL)
		{
			memset( value, 0, sizeof( struct in_addr ));
			break;
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		if ( pblock->pb_conn->cin_addr == NULL ) {
			memset( value, 0, sizeof( struct in_addr ));
		} else {

			if ( PR_IsNetAddrType(pblock->pb_conn->cin_addr,
											PR_IpAddrV4Mapped) ) {
				 
				(*(struct in_addr *)value).s_addr =
					(*(pblock->pb_conn->cin_addr)).ipv6.ip.pr_s6_addr32[3];
			
			} else {
				memset( value, 0, sizeof( struct in_addr ));
			}
		}
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_SERVERIP:
		if (pblock->pb_conn == NULL)
		{
			memset( value, 0, sizeof( struct in_addr ));
			break;
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		if ( pblock->pb_conn->cin_destaddr == NULL ) {
			memset( value, 0, sizeof( PRNetAddr ));
		} else {

			if ( PR_IsNetAddrType(pblock->pb_conn->cin_destaddr,
											PR_IpAddrV4Mapped) ) {
				 
				(*(struct in_addr *)value).s_addr =
					(*(pblock->pb_conn->cin_destaddr)).ipv6.ip.pr_s6_addr32[3];
			
			} else {
				memset( value, 0, sizeof( struct in_addr ));
			}

		}
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_IS_REPLICATION_SESSION:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_IS_REPLICATION_SESSION \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		(*(int *)value) = pblock->pb_conn->c_isreplication_session;
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_IS_SSL_SESSION:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_IS_SSL_SESSION \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		(*(int *)value) = pblock->pb_conn->c_flags & CONN_FLAG_SSL;
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_SASL_SSF:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
			  "Connection is NULL and hence cannot access SLAPI_CONN_SASL_SSF \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		(*(int *)value) = pblock->pb_conn->c_sasl_ssf;
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_SSL_SSF:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
			  "Connection is NULL and hence cannot access SLAPI_CONN_SSL_SSF \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		(*(int *)value) = pblock->pb_conn->c_ssl_ssf;
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_LOCAL_SSF:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
			    "Connection is NULL and hence cannot access SLAPI_CONN_LOCAL_SSF \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		(*(int *)value) = pblock->pb_conn->c_local_ssf;
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_CERT:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_CERT \n", 0, 0, 0 );
			return (-1);
		}
		( *(CERTCertificate **) value) = pblock->pb_conn->c_client_cert;
		break;	
	case SLAPI_OPERATION:
		(*(Operation **)value) = pblock->pb_op;
		break;
	case SLAPI_OPERATION_TYPE:
		if (pblock->pb_op == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Operation is NULL and hence cannot access SLAPI_OPERATION_TYPE \n", 0, 0, 0 );
			return (-1);
		}
		(*(int *)value) = pblock->pb_op->o_params.operation_type;
		break;
	case SLAPI_OPINITIATED_TIME:
		if (pblock->pb_op == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Operation is NULL and hence cannot access SLAPI_OPINITIATED_TIME \n", 0, 0, 0 );
			return (-1);
		}
		(*(time_t *)value) = pblock->pb_op->o_time;
		break;
	case SLAPI_REQUESTOR_ISROOT:
		(*(int *)value) = pblock->pb_requestor_isroot;
		break;
	case SLAPI_SKIP_MODIFIED_ATTRS:
		if(pblock->pb_op==NULL)
                {
                        (*(int *)value) = 0; /* No Operation -> No skip */
                }
                else
                {
                        (*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_SKIP_MODIFIED_ATTRS);
		}
		break;
	case SLAPI_IS_REPLICATED_OPERATION:
		if(pblock->pb_op==NULL)
		{
			(*(int *)value) = 0; /* No Operation -> Not Replicated */
		}
		else
		{
			(*(int *)value) = (pblock->pb_op->o_flags & (OP_FLAG_REPLICATED | OP_FLAG_LEGACY_REPLICATION_DN));
		}
		break;
    case SLAPI_IS_MMR_REPLICATED_OPERATION:
		if(pblock->pb_op==NULL)
		{
			(*(int *)value) = 0; /* No Operation -> Not Replicated */
		}
		else
		{
			(*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_REPLICATED);
		}
		break;
    case SLAPI_IS_LEGACY_REPLICATED_OPERATION:
        if(pblock->pb_op==NULL)
		{
			(*(int *)value) = 0; /* No Operation -> Not Replicated */
		}
		else
		{
			(*(int *)value) = (pblock->pb_op->o_flags & OP_FLAG_LEGACY_REPLICATION_DN);
		}
		break;

	case SLAPI_OPERATION_PARAMETERS:
		if(pblock->pb_op!=NULL)
		{
			(*(struct slapi_operation_parameters **)value) = &pblock->pb_op->o_params;
		}
		break;

	/* stuff related to config file processing */
	case SLAPI_CONFIG_FILENAME:
	case SLAPI_CONFIG_LINENO:
	case SLAPI_CONFIG_ARGC:
	case SLAPI_CONFIG_ARGV:
		return (-1);	/* deprecated since DS 5.0 (no longer useful) */

    /* pblock memory management */
    case SLAPI_DESTROY_CONTENT:
        (*(int *)value) = pblock->pb_destroy_content;
        break;        

	/* stuff related to the current plugin */
	case SLAPI_PLUGIN:
		(*(struct slapdplugin **)value) = pblock->pb_plugin;
		break;
	case SLAPI_PLUGIN_PRIVATE:
		(*(void **)value) = pblock->pb_plugin->plg_private;
		break;
	case SLAPI_PLUGIN_TYPE:
		(*(int *)value) = pblock->pb_plugin->plg_type;
		break;
	case SLAPI_PLUGIN_ARGV:
		(*(char ***)value) = pblock->pb_plugin->plg_argv;
		break;
	case SLAPI_PLUGIN_ARGC:
		(*(int *)value) = pblock->pb_plugin->plg_argc;
		break;
	case SLAPI_PLUGIN_VERSION:
		(*(char **)value) = pblock->pb_plugin->plg_version;
		break;
	case SLAPI_PLUGIN_PRECEDENCE:
		(*(int *)value) = pblock->pb_plugin->plg_precedence;
		break;
	case SLAPI_PLUGIN_OPRETURN:
		(*(int *)value) = pblock->pb_opreturn;
		break;
	case SLAPI_PLUGIN_OBJECT:
		(*(void **)value) = pblock->pb_object;
		break;
	case SLAPI_PLUGIN_DESTROY_FN:
		(*(IFP*)value) = pblock->pb_destroy_fn;
		break;
	case SLAPI_PLUGIN_DESCRIPTION:
		(*(Slapi_PluginDesc *)value) = pblock->pb_plugin->plg_desc;
		break;
	case SLAPI_PLUGIN_IDENTITY:
		(*(void**)value) = pblock->pb_plugin_identity;
		break;
	case SLAPI_PLUGIN_CONFIG_AREA:
		(*(char **)value) = pblock->pb_plugin_config_area;
		break;
	case SLAPI_PLUGIN_INTOP_RESULT:
        (*(int *)value) = pblock->pb_internal_op_result; 
        break;
	case SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES:
        (*(Slapi_Entry ***)value) = pblock->pb_plugin_internal_search_op_entries;
        break;
	case SLAPI_PLUGIN_INTOP_SEARCH_REFERRALS:
        (*(char ***)value) = pblock->pb_plugin_internal_search_op_referrals;
        break;

	/* database plugin functions */
	case SLAPI_PLUGIN_DB_BIND_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bind;
		break;
	case SLAPI_PLUGIN_DB_UNBIND_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_unbind;
		break;
	case SLAPI_PLUGIN_DB_SEARCH_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_search;
		break;
	case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_next_search_entry;
		break;
	case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_next_search_entry_ext;
		break;
	case SLAPI_PLUGIN_DB_ENTRY_RELEASE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_entry_release;
		break;
	case SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(VFPP *)value) = pblock->pb_plugin->plg_search_results_release;
		break;
	case SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(VFP *)value) = pblock->pb_plugin->plg_prev_search_results;
		break;
	case SLAPI_PLUGIN_DB_COMPARE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_compare;
		break;
	case SLAPI_PLUGIN_DB_MODIFY_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_modify;
		break;
	case SLAPI_PLUGIN_DB_MODRDN_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_modrdn;
		break;
	case SLAPI_PLUGIN_DB_ADD_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_add;
		break;
	case SLAPI_PLUGIN_DB_DELETE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_delete;
		break;
	case SLAPI_PLUGIN_DB_ABANDON_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_abandon;
		break;
	case SLAPI_PLUGIN_DB_CONFIG_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_config;
		break;
	case SLAPI_PLUGIN_CLOSE_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_close;
		break;
	case SLAPI_PLUGIN_CLEANUP_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_cleanup;
		break;
	case SLAPI_PLUGIN_DB_FLUSH_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_flush;
		break;
	case SLAPI_PLUGIN_START_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_start;
		break;
	case SLAPI_PLUGIN_POSTSTART_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_poststart;
		break;
	case SLAPI_PLUGIN_DB_WIRE_IMPORT_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_wire_import;
		break;
	case SLAPI_PLUGIN_DB_ADD_SCHEMA_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_add_schema;
		break;
	case SLAPI_PLUGIN_DB_GET_INFO_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_get_info;
		break;
	case SLAPI_PLUGIN_DB_SET_INFO_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_set_info;
		break;
	case SLAPI_PLUGIN_DB_CTRL_INFO_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_ctrl_info;
		break;
	case SLAPI_PLUGIN_DB_SEQ_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_seq;
		break;
	case SLAPI_PLUGIN_DB_ENTRY_FN:
		(*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER( pblock,
							plg_entry );
		break;
	case SLAPI_PLUGIN_DB_REFERRAL_FN:
		(*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER( pblock,
							plg_referral );
		break;
	case SLAPI_PLUGIN_DB_RESULT_FN:
		(*(IFP *)value) = SLAPI_PBLOCK_GET_PLUGIN_RELATED_POINTER( pblock,
							plg_result );
		break;
	case SLAPI_PLUGIN_DB_RMDB_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_rmdb;
		break;
	case SLAPI_PLUGIN_DB_INIT_INSTANCE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_init_instance;
		break;
	case SLAPI_PLUGIN_DB_LDIF2DB_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_ldif2db;
        break;
    case SLAPI_PLUGIN_DB_DB2LDIF_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_db2ldif;
        break;
    case SLAPI_PLUGIN_DB_DB2INDEX_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_db2index;
        break;
    case SLAPI_PLUGIN_DB_ARCHIVE2DB_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_archive2db;
        break;
    case SLAPI_PLUGIN_DB_DB2ARCHIVE_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_db2archive;
        break;
    case SLAPI_PLUGIN_DB_UPGRADEDB_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_upgradedb;
        break;
    case SLAPI_PLUGIN_DB_UPGRADEDNFORMAT_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_upgradednformat;
        break;
    case SLAPI_PLUGIN_DB_DBVERIFY_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_dbverify;
        break;
    case SLAPI_PLUGIN_DB_BEGIN_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_begin;
        break;
    case SLAPI_PLUGIN_DB_COMMIT_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_commit;
        break;
    case SLAPI_PLUGIN_DB_ABORT_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_abort;
        break;
    case SLAPI_PLUGIN_DB_SIZE_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_dbsize;
        break;
    case SLAPI_PLUGIN_DB_TEST_FN:
        if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
        (*(IFP *)value) = pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_dbtest;
        break;
    /* database plugin-specific parameters */
    case SLAPI_PLUGIN_DB_NO_ACL:
        if (  pblock->pb_plugin && pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
                return( -1 );
        }
		if ( NULL == be ) {	
			(*(int *)value) = 0;	/* default value */
		} else {
			(*(int *)value) = be->be_noacl;
		}
        break;

	/* extendedop plugin functions */
	case SLAPI_PLUGIN_EXT_OP_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_exhandler;
		break;
	case SLAPI_PLUGIN_EXT_OP_OIDLIST:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP ) {
			return( -1 );
		}
		(*(char ***)value) = pblock->pb_plugin->plg_exoids;
		break;
	case SLAPI_PLUGIN_EXT_OP_NAMELIST:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP ) {
			return( -1 );
		}
		(*(char ***)value) = pblock->pb_plugin->plg_exnames;
		break;

	/* preoperation plugin functions */
	case SLAPI_PLUGIN_PRE_BIND_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_prebind;
		break;
	case SLAPI_PLUGIN_PRE_UNBIND_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_preunbind;
		break;
	case SLAPI_PLUGIN_PRE_SEARCH_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_presearch;
		break;
	case SLAPI_PLUGIN_PRE_COMPARE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_precompare;
		break;
	case SLAPI_PLUGIN_PRE_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_premodify;
		break;
	case SLAPI_PLUGIN_PRE_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_premodrdn;
		break;
	case SLAPI_PLUGIN_PRE_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_preadd;
		break;
	case SLAPI_PLUGIN_PRE_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_predelete;
		break;
	case SLAPI_PLUGIN_PRE_ABANDON_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_preabandon;
		break;
	case SLAPI_PLUGIN_PRE_ENTRY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_preentry;
		break;
	case SLAPI_PLUGIN_PRE_REFERRAL_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_prereferral;
		break;
	case SLAPI_PLUGIN_PRE_RESULT_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_preresult;
		break;

	/* postoperation plugin functions */
	case SLAPI_PLUGIN_POST_BIND_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postbind;
		break;
	case SLAPI_PLUGIN_POST_UNBIND_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postunbind;
		break;
	case SLAPI_PLUGIN_POST_SEARCH_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postsearch;
		break;
	case SLAPI_PLUGIN_POST_SEARCH_FAIL_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postsearchfail;
		break;
	case SLAPI_PLUGIN_POST_COMPARE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postcompare;
		break;
	case SLAPI_PLUGIN_POST_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postmodify;
		break;
	case SLAPI_PLUGIN_POST_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postmodrdn;
		break;
	case SLAPI_PLUGIN_POST_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postadd;
		break;
	case SLAPI_PLUGIN_POST_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postdelete;
		break;
	case SLAPI_PLUGIN_POST_ABANDON_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postabandon;
		break;
	case SLAPI_PLUGIN_POST_ENTRY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postentry;
		break;
	case SLAPI_PLUGIN_POST_REFERRAL_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postreferral;
		break;
	case SLAPI_PLUGIN_POST_RESULT_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_postresult;
		break;

	case SLAPI_ENTRY_PRE_OP:
		(*(Slapi_Entry **)value) = pblock->pb_pre_op_entry;
		break;
	case SLAPI_ENTRY_POST_OP:
		(*(Slapi_Entry **)value) = pblock->pb_post_op_entry;
		break;

	/* backend preoperation plugin */
	case SLAPI_PLUGIN_BE_PRE_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepremodify;
		break;
	case SLAPI_PLUGIN_BE_PRE_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepremodrdn;
		break;
	case SLAPI_PLUGIN_BE_PRE_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepreadd;
		break;
	case SLAPI_PLUGIN_BE_PRE_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepredelete;
		break;
	case SLAPI_PLUGIN_BE_PRE_CLOSE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepreclose;
		break;
	case SLAPI_PLUGIN_BE_PRE_BACKUP_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_beprebackup;
		break;

	/* backend postoperation plugin */
	case SLAPI_PLUGIN_BE_POST_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepostmodify;
		break;
	case SLAPI_PLUGIN_BE_POST_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepostmodrdn;
		break;
	case SLAPI_PLUGIN_BE_POST_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepostadd;
		break;
	case SLAPI_PLUGIN_BE_POST_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepostdelete;
		break;
	case SLAPI_PLUGIN_BE_POST_OPEN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepostopen;
		break;
	case SLAPI_PLUGIN_BE_POST_BACKUP_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_bepostbackup;
		break;

	/* internal preoperation plugin */
	case SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_modify;
		break;
	case SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_modrdn;
		break;
	case SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_add;
		break;
	case SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_internal_pre_delete;
		break;

	/* internal postoperation plugin */
	case SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_internal_post_modify;
		break;
	case SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_internal_post_modrdn;
		break;
	case SLAPI_PLUGIN_INTERNAL_POST_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_internal_post_add;
		break;
	case SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_internal_post_delete;
		break;

	/* backend pre txn operation plugin */
	case SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_betxnpremodify;
		break;
	case SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_betxnpremodrdn;
		break;
	case SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_betxnpreadd;
		break;
	case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_betxnpredelete;
		break;

	/* backend post txn operation plugin */
	case SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_betxnpostmodify;
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_betxnpostmodrdn;
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_betxnpostadd;
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_betxnpostdelete;
		break;

	/* target address & controls for all operations should be normalized  */
	case SLAPI_TARGET_ADDRESS:
		if(pblock->pb_op!=NULL)
		{
			(*(entry_address **)value) = &(pblock->pb_op->o_params.target_address);
		}
		break;
	case SLAPI_TARGET_DN: /* DEPRECATED */
		/* The returned value refers SLAPI_TARGET_SDN.  
		 * It should not be freed.*/
		if(pblock->pb_op!=NULL)
		{
			Slapi_DN *sdn = pblock->pb_op->o_params.target_address.sdn;
			if (sdn) {
				(*(char **)value) = (char *)slapi_sdn_get_dn(sdn);
			} else {
				(*(char **)value) = NULL;
			}
		}
		else
		{
			return( -1 );
		}
		break;
	case SLAPI_TARGET_SDN:
		if(pblock->pb_op!=NULL)
		{
			(*(Slapi_DN **)value) = pblock->pb_op->o_params.target_address.sdn;
		}
		else
		{
			return( -1 );
		}
		break;
	case SLAPI_ORIGINAL_TARGET_DN:
		if(pblock->pb_op!=NULL)
		{
			(*(char **)value) = pblock->pb_op->o_params.target_address.udn;
		}
		break;
	case SLAPI_TARGET_UNIQUEID:
		if(pblock->pb_op!=NULL)
		{
			(*(char **)value) = pblock->pb_op->o_params.target_address.uniqueid;
		}
		break;
	case SLAPI_REQCONTROLS:
		if(pblock->pb_op!=NULL)
		{
			(*(LDAPControl ***)value) = pblock->pb_op->o_params.request_controls;
		}
		break;
	case SLAPI_RESCONTROLS:
		if(pblock->pb_op!=NULL)
		{
			(*(LDAPControl ***)value) = pblock->pb_op->o_results.result_controls;
		}
		break;
	case SLAPI_CONTROLS_ARG:	/* used to pass control argument before operation is created */
		(*(LDAPControl ***)value) = pblock->pb_ctrls_arg;
		break;
	/* notes to be added to the access log RESULT line for this op. */
	case SLAPI_OPERATION_NOTES:
		(*(unsigned int *)value) = pblock->pb_operation_notes;
		break;

	/* syntax plugin functions */
	case SLAPI_PLUGIN_SYNTAX_FILTER_AVA:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_syntax_filter_ava;
		break;
	case SLAPI_PLUGIN_SYNTAX_FILTER_SUB:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_syntax_filter_sub;
		break;
	case SLAPI_PLUGIN_SYNTAX_VALUES2KEYS:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_syntax_values2keys;
		break;
	case SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_syntax_assertion2keys_ava;
		break;
	case SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_syntax_assertion2keys_sub;
		break;
	case SLAPI_PLUGIN_SYNTAX_NAMES:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(char ***)value) = pblock->pb_plugin->plg_syntax_names;
		break;
	case SLAPI_PLUGIN_SYNTAX_OID:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(char **)value) = pblock->pb_plugin->plg_syntax_oid;
		break;
	case SLAPI_PLUGIN_SYNTAX_FLAGS:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(int *)value) = pblock->pb_plugin->plg_syntax_flags;
		break;
	case SLAPI_PLUGIN_SYNTAX_COMPARE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_syntax_compare;
		break;
	case SLAPI_SYNTAX_SUBSTRLENS: /* aka SLAPI_MR_SUBSTRLENS */
		(*(int **)value) = pblock->pb_substrlens;
		break;
	case SLAPI_PLUGIN_SYNTAX_VALIDATE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_syntax_validate;
		break;
	case SLAPI_PLUGIN_SYNTAX_NORMALIZE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		(*(VFPV *)value) = pblock->pb_plugin->plg_syntax_normalize;
		break;

	/* controls we know about */
	case SLAPI_MANAGEDSAIT:
		(*(int *)value) = pblock->pb_managedsait;
		break;
	case SLAPI_PWPOLICY:
		(*(int *)value) = pblock->pb_pwpolicy_ctrl;
		break;

	/* add arguments */
	case SLAPI_ADD_ENTRY:
		if(pblock->pb_op!=NULL)
		{
			(*(Slapi_Entry **)value) = pblock->pb_op->o_params.p.p_add.target_entry;
		}
		break;
	case SLAPI_ADD_EXISTING_DN_ENTRY:
		(*(Slapi_Entry **)value) = pblock->pb_existing_dn_entry;
		break;
	case SLAPI_ADD_EXISTING_UNIQUEID_ENTRY:
		(*(Slapi_Entry **)value) = pblock->pb_existing_uniqueid_entry;
		break;
	case SLAPI_ADD_PARENT_ENTRY:
		(*(Slapi_Entry **)value) = pblock->pb_parent_entry;
		break;
	case SLAPI_ADD_PARENT_UNIQUEID:
		if(pblock->pb_op!=NULL)
		{
			(*(char **)value) = pblock->pb_op->o_params.p.p_add.parentuniqueid;
		}
		break;

	/* bind arguments */
	case SLAPI_BIND_METHOD:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_bind.bind_method;
		}
		break;
	case SLAPI_BIND_CREDENTIALS:
		if(pblock->pb_op!=NULL)
		{
			(*(struct berval **)value) = pblock->pb_op->o_params.p.p_bind.bind_creds;
		}
		break;
	case SLAPI_BIND_SASLMECHANISM:
		if(pblock->pb_op!=NULL)
		{
			(*(char **)value) = pblock->pb_op->o_params.p.p_bind.bind_saslmechanism;
		}
		break;
	/* bind return values */
	case SLAPI_BIND_RET_SASLCREDS:
		if(pblock->pb_op!=NULL)
		{
			(*(struct berval **)value) = pblock->pb_op->o_results.r.r_bind.bind_ret_saslcreds;
		}
		break;

	/* compare arguments */
	case SLAPI_COMPARE_TYPE:
		if(pblock->pb_op!=NULL)
		{
			(*(char **)value) = pblock->pb_op->o_params.p.p_compare.compare_ava.ava_type;
		}
		break;
	case SLAPI_COMPARE_VALUE:
		if(pblock->pb_op!=NULL)
		{
			(*(struct berval **)value) = &pblock->pb_op->o_params.p.p_compare.compare_ava.ava_value;
		}
		break;

	/* modify arguments */
	case SLAPI_MODIFY_MODS:
		PR_ASSERT(pblock->pb_op);
		if(pblock->pb_op!=NULL)
		{
			if(pblock->pb_op->o_params.operation_type==SLAPI_OPERATION_MODIFY)
			{
				(*(LDAPMod ***)value) = pblock->pb_op->o_params.p.p_modify.modify_mods;
			}
			else if(pblock->pb_op->o_params.operation_type==SLAPI_OPERATION_MODRDN)
			{
				(*(LDAPMod ***)value) = pblock->pb_op->o_params.p.p_modrdn.modrdn_mods;
			}
			else
			{
				PR_ASSERT(0); /* JCM */
			}
		}
		break;

	/* modrdn arguments */
	case SLAPI_MODRDN_NEWRDN:
		if(pblock->pb_op!=NULL)
		{
			(*(char **)value) = pblock->pb_op->o_params.p.p_modrdn.modrdn_newrdn;
		}
		break;
	case SLAPI_MODRDN_DELOLDRDN:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_modrdn.modrdn_deloldrdn;
		}
		break;
	case SLAPI_MODRDN_NEWSUPERIOR: /* DEPRECATED */
		if(pblock->pb_op!=NULL)
		{
			Slapi_DN *sdn =
			  pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn;
			if (sdn) {
				(*(char **)value) = (char *)slapi_sdn_get_dn(sdn);
			} else {
				(*(char **)value) = NULL;
			}
		}
		else
		{
			return -1;
		}
		break;
	case SLAPI_MODRDN_NEWSUPERIOR_SDN:
		if(pblock->pb_op!=NULL)
		{
			(*(Slapi_DN **)value) = 
			  pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn;
		}
		else
		{
			return -1;
		}
		break;
	case SLAPI_MODRDN_PARENT_ENTRY:
		(*(Slapi_Entry **)value) = pblock->pb_parent_entry;
		break;
	case SLAPI_MODRDN_NEWPARENT_ENTRY:
		(*(Slapi_Entry **)value) = pblock->pb_newparent_entry;
		break;
	case SLAPI_MODRDN_TARGET_ENTRY:
		(*(Slapi_Entry **)value) = pblock->pb_target_entry;
		break;
	case SLAPI_MODRDN_NEWSUPERIOR_ADDRESS:
		if(pblock->pb_op!=NULL)
		{
			(*(entry_address **)value) = &(pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address);
			break;
		}

	/* search arguments */
	case SLAPI_SEARCH_SCOPE:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_search.search_scope;
		}
		break;
	case SLAPI_SEARCH_DEREF:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_search.search_deref;
		}
		break;
	case SLAPI_SEARCH_SIZELIMIT:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_search.search_sizelimit;
		}
		break;
	case SLAPI_SEARCH_TIMELIMIT:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_search.search_timelimit;
		}
		break;
	case SLAPI_SEARCH_FILTER:
		if(pblock->pb_op!=NULL)
		{
			(*(struct slapi_filter **)value) = pblock->pb_op->o_params.p.p_search.search_filter;
		}
		break;
	case SLAPI_SEARCH_STRFILTER:
		if(pblock->pb_op!=NULL)
		{
			(*(char **)value) = pblock->pb_op->o_params.p.p_search.search_strfilter;
		}
		break;
	case SLAPI_SEARCH_ATTRS:
		if(pblock->pb_op!=NULL)
		{
			(*(char ***)value) = pblock->pb_op->o_params.p.p_search.search_attrs;
		}
		break;
	case SLAPI_SEARCH_GERATTRS:
		if(pblock->pb_op!=NULL)
		{
			(*(char ***)value) = pblock->pb_op->o_params.p.p_search.search_gerattrs;
		}
		break;
	case SLAPI_SEARCH_ATTRSONLY:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_search.search_attrsonly;
		}
		break;
	case SLAPI_SEARCH_IS_AND:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_search.search_is_and;
		}
		break;

	case SLAPI_ABANDON_MSGID:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_params.p.p_abandon.abandon_targetmsgid;
		}
		break;

	/* extended operation arguments */
	case SLAPI_EXT_OP_REQ_OID:
		if(pblock->pb_op!=NULL)
		{
			(*(char **) value) = pblock->pb_op->o_params.p.p_extended.exop_oid;
		}
		break;
	case SLAPI_EXT_OP_REQ_VALUE:
		if(pblock->pb_op!=NULL)
		{
			(*(struct berval **)value) = pblock->pb_op->o_params.p.p_extended.exop_value;
		}
		break;
	/* extended operation return values */
	case SLAPI_EXT_OP_RET_OID:
		if(pblock->pb_op!=NULL)
		{
			(*(char **) value) = pblock->pb_op->o_results.r.r_extended.exop_ret_oid;
		}
		break;
	case SLAPI_EXT_OP_RET_VALUE:
		if(pblock->pb_op!=NULL)
		{
			(*(struct berval **)value) = pblock->pb_op->o_results.r.r_extended.exop_ret_value;
		}
		break;

	/* matching rule plugin functions */
	case SLAPI_PLUGIN_MR_FILTER_CREATE_FN:
		SLAPI_PLUGIN_TYPE_CHECK (pblock, SLAPI_PLUGIN_MATCHINGRULE);
		(*(IFP *)value) = pblock->pb_plugin->plg_mr_filter_create;
		break;
	case SLAPI_PLUGIN_MR_INDEXER_CREATE_FN:
		SLAPI_PLUGIN_TYPE_CHECK (pblock, SLAPI_PLUGIN_MATCHINGRULE);
		(*(IFP *)value) = pblock->pb_plugin->plg_mr_indexer_create;
		break;
	case SLAPI_PLUGIN_MR_FILTER_MATCH_FN:
		(*(mrFilterMatchFn *)value) = pblock->pb_mr_filter_match_fn;
		break;
	case SLAPI_PLUGIN_MR_FILTER_INDEX_FN:
		(*(IFP *)value) = pblock->pb_mr_filter_index_fn;
		break;
	case SLAPI_PLUGIN_MR_FILTER_RESET_FN:
		(*(IFP *)value) = pblock->pb_mr_filter_reset_fn;
		break;
	case SLAPI_PLUGIN_MR_INDEX_FN:
		(*(IFP *)value) = pblock->pb_mr_index_fn;
		break;
	case SLAPI_PLUGIN_MR_INDEX_SV_FN:
		(*(IFP *)value) = pblock->pb_mr_index_sv_fn;
		break;

	/* matching rule plugin arguments */
	case SLAPI_PLUGIN_MR_OID:
		(*(char **) value) = pblock->pb_mr_oid;
		break;
	case SLAPI_PLUGIN_MR_TYPE:
		(*(char **) value) = pblock->pb_mr_type;
		break;
	case SLAPI_PLUGIN_MR_VALUE:
		(*(struct berval **) value) = pblock->pb_mr_value;
		break;
	case SLAPI_PLUGIN_MR_VALUES:
		(*(struct berval ***) value) = pblock->pb_mr_values;
		break;
	case SLAPI_PLUGIN_MR_KEYS:
		(*(struct berval ***) value) = pblock->pb_mr_keys;
		break;
	case SLAPI_PLUGIN_MR_FILTER_REUSABLE:
		(*(unsigned int *) value) = pblock->pb_mr_filter_reusable;
		break;
	case SLAPI_PLUGIN_MR_QUERY_OPERATOR:
		(*(int *) value) = pblock->pb_mr_query_operator;
		break;
	case SLAPI_PLUGIN_MR_USAGE:
		(*(unsigned int *) value) = pblock->pb_mr_usage;
		break;

	/* new style matching rule syntax plugin functions */
	case SLAPI_PLUGIN_MR_FILTER_AVA:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_mr_filter_ava;
		break;
	case SLAPI_PLUGIN_MR_FILTER_SUB:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_mr_filter_sub;
		break;
	case SLAPI_PLUGIN_MR_VALUES2KEYS:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_mr_values2keys;
		break;
	case SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_mr_assertion2keys_ava;
		break;
	case SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_mr_assertion2keys_sub;
		break;
	case SLAPI_PLUGIN_MR_FLAGS:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(int *)value) = pblock->pb_plugin->plg_mr_flags;
		break;
	case SLAPI_PLUGIN_MR_NAMES:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(char ***)value) = pblock->pb_plugin->plg_mr_names;
		break;
	case SLAPI_PLUGIN_MR_COMPARE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(IFP *)value) = pblock->pb_plugin->plg_mr_compare;
		break;
	case SLAPI_PLUGIN_MR_NORMALIZE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		(*(VFPV *)value) = pblock->pb_plugin->plg_mr_normalize;
		break;

	/* seq arguments */
	case SLAPI_SEQ_TYPE:
		(*(int *)value) = pblock->pb_seq_type;
		break;
	case SLAPI_SEQ_ATTRNAME:
		(*(char **)value) = pblock->pb_seq_attrname;
		break;
	case SLAPI_SEQ_VAL:
		(*(char **)value) = pblock->pb_seq_val;
		break;

	/* ldif2db arguments */
	case SLAPI_LDIF2DB_FILE:
                (*(char ***)value) = pblock->pb_ldif_files;
		break;
	case SLAPI_LDIF2DB_REMOVEDUPVALS:
		(*(int *)value) = pblock->pb_removedupvals;
		break;
	case SLAPI_DB2INDEX_ATTRS:
		(*(char ***)value) = pblock->pb_db2index_attrs;
		break;
	case SLAPI_LDIF2DB_NOATTRINDEXES:
		(*(int *)value) = pblock->pb_ldif2db_noattrindexes;
		break;
	case SLAPI_LDIF2DB_INCLUDE:
		(*(char ***)value) = pblock->pb_ldif_include;
		break;
	case SLAPI_LDIF2DB_EXCLUDE:
		(*(char ***)value) = pblock->pb_ldif_exclude;
		break;
	case SLAPI_LDIF2DB_GENERATE_UNIQUEID:
		(*(int *)value) = pblock->pb_ldif_generate_uniqueid;
		break;
	case SLAPI_LDIF2DB_ENCRYPT:
	case SLAPI_DB2LDIF_DECRYPT:
		(*(int *)value) = pblock->pb_ldif_encrypt;
		break;
	case SLAPI_LDIF2DB_NAMESPACEID:
		(*(char **)value) = pblock->pb_ldif_namespaceid;
		break;

	/* db2ldif arguments */
	case SLAPI_DB2LDIF_PRINTKEY:
		(*(int *)value) = pblock->pb_ldif_printkey;
		break;
	case SLAPI_DB2LDIF_DUMP_UNIQUEID:
		(*(int *)value) = pblock->pb_ldif_dump_uniqueid;
		break;
	case SLAPI_DB2LDIF_FILE:
		(*(char **)value) = pblock->pb_ldif_file;
		break;

	/* db2ldif/ldif2db/db2bak/bak2db arguments */
	case SLAPI_BACKEND_INSTANCE_NAME:
		(*(char **)value) = pblock->pb_instance_name;
		break;
        case SLAPI_BACKEND_TASK:
                (*(Slapi_Task **)value) = pblock->pb_task;
                break;
	case SLAPI_TASK_FLAGS:
		(*(int *)value) = pblock->pb_task_flags;
		break;
	case SLAPI_DB2LDIF_SERVER_RUNNING:
		(*(int *)value) = pblock->pb_server_running;
		break;
        case SLAPI_BULK_IMPORT_ENTRY:
                (*(Slapi_Entry **)value) = pblock->pb_import_entry;
                break;
        case SLAPI_BULK_IMPORT_STATE:
                (*(int *)value) = pblock->pb_import_state;
                break;

	/* transaction arguments */
	case SLAPI_PARENT_TXN:
		(*(void **)value) = pblock->pb_parent_txn;
		break;
	case SLAPI_TXN:
		(*(void **)value) = pblock->pb_txn;
		break;
	case SLAPI_TXN_RUV_MODS_FN:
		(*(IFP*)value) = pblock->pb_txn_ruv_mods_fn;
		break;

	/* Search results set */
	case SLAPI_SEARCH_RESULT_SET:
		if(pblock->pb_op!=NULL)
		{
			(*(void **)value) = pblock->pb_op->o_results.r.r_search.search_result_set;
		}
		break;
	/* estimated search result set size */
	case SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_results.r.r_search.estimate;
		}
		break;
	/* Entry returned from iterating over results set */
	case SLAPI_SEARCH_RESULT_ENTRY:
		if(pblock->pb_op!=NULL)
		{
			(*(void **)value) = pblock->pb_op->o_results.r.r_search.search_result_entry;
		}
		break;
	case SLAPI_SEARCH_RESULT_ENTRY_EXT:
		if(pblock->pb_op!=NULL)
		{
        		(*(void **)value) = pblock->pb_op->o_results.r.r_search.opaque_backend_ptr;	    
		}
		break;
	/* Number of entries returned from search */
	case SLAPI_NENTRIES:
		if(pblock->pb_op!=NULL)
		{
			(*(int *)value) = pblock->pb_op->o_results.r.r_search.nentries;
		}
		break;
	/* Referrals encountered while iterating over result set */
	case SLAPI_SEARCH_REFERRALS:
		if(pblock->pb_op!=NULL)
		{
			(*(struct berval ***)value) = pblock->pb_op->o_results.r.r_search.search_referrals;
		}
		break;

	case SLAPI_RESULT_CODE:
		if (pblock->pb_op != NULL)
			* ((int *) value) = pblock->pb_op->o_results.result_code;
		break;
	case SLAPI_RESULT_MATCHED:
		if (pblock->pb_op != NULL)
			* ((char **) value) = pblock->pb_op->o_results.result_matched;
		break;
	case SLAPI_RESULT_TEXT:
		if (pblock->pb_op != NULL)
			* ((char **) value) = pblock->pb_op->o_results.result_text;
		break;
	case SLAPI_PB_RESULT_TEXT:
		* ((char **) value) = pblock->pb_result_text;
		break;

	/* Size of the database, in kb */
	case SLAPI_DBSIZE:
		(*(unsigned int *)value) = pblock->pb_dbsize;
		break;

	/* ACL Plugin */
	case SLAPI_PLUGIN_ACL_INIT:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_init;
		break;
	case SLAPI_PLUGIN_ACL_SYNTAX_CHECK:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_syntax_check;
		break;
	case SLAPI_PLUGIN_ACL_ALLOW_ACCESS:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_access_allowed;
		break;
	case SLAPI_PLUGIN_ACL_MODS_ALLOWED:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_mods_allowed;
		break;
	case SLAPI_PLUGIN_ACL_MODS_UPDATE:
        (*(IFP *)value) = pblock->pb_plugin->plg_acl_mods_update;
		break;

	case SLAPI_REQUESTOR_DN:
		/* NOTE: It's not a copy of the DN */	
		if (pblock->pb_op != NULL)
		{
			char *dn= (char*)slapi_sdn_get_dn(&pblock->pb_op->o_sdn);
			if(dn==NULL)
    				(*( char **)value ) = "";
			else
    				(*( char **)value ) = dn;
		}
		break;

	case SLAPI_REQUESTOR_NDN:
		/* NOTE: It's not a copy of the DN */	
		if (pblock->pb_op != NULL)
		{
			char *ndn = (char*)slapi_sdn_get_ndn(&pblock->pb_op->o_sdn);
			if(ndn == NULL)
    				(*( char **)value ) = "";
			else
    				(*( char **)value ) = ndn;
		}
		break;

	case SLAPI_OPERATION_AUTHTYPE:
		if (pblock->pb_op != NULL)
		{
			if(pblock->pb_op->o_authtype==NULL)
				(*( char **)value ) = "";
			else
    				(*( char **)value ) = pblock->pb_op->o_authtype;
		}
		break;	

	case SLAPI_OPERATION_SSF:
		if (pblock->pb_op!=NULL) {
			* ((int *) value) = pblock->pb_op->o_ssf;
		}
		break;

	case SLAPI_CLIENT_DNS:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CLIENT_DNS \n", 0, 0, 0 );
			return (-1);
		}
		(*(struct berval ***)value ) =  pblock->pb_conn->c_domain;
		break;

	case SLAPI_BE_MAXNESTLEVEL:
		if ( NULL == be ) {
			return( -1 );
		}
		(*(int *)value ) =  be->be_maxnestlevel;
		break;
	case SLAPI_OPERATION_ID:
		if (pblock->pb_op != NULL) {
			(*(int *)value ) = pblock->pb_op->o_opid;
		}
		break;
	/* Command line arguments */
	case SLAPI_ARGC:
        (*(int *)value) = pblock->pb_slapd_argc;
		break;
	case SLAPI_ARGV:
        (*(char ***)value) = pblock->pb_slapd_argv;
		break;

	/* Config file directory */
	case SLAPI_CONFIG_DIRECTORY:
        (*(char **)value) = pblock->pb_slapd_configdir;
		break;

	/* password storage scheme (kexcoff */
	case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME:
        (*(char **)value) = pblock->pb_plugin->plg_pwdstorageschemename;
		break;
	case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_USER_PWD:
		(*(char **)value) = pblock->pb_pwd_storage_scheme_user_passwd;
		break;

	case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DB_PWD:
		(*(char **)value) = pblock->pb_pwd_storage_scheme_db_passwd;
		break;

	case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN:
		(*(CFP *)value) = pblock->pb_plugin->plg_pwdstorageschemeenc;
		break;

	case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_pwdstorageschemedec;
		break;

	case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN:
		(*(IFP *)value) = pblock->pb_plugin->plg_pwdstorageschemecmp;
		break;

	/* entry fetch/store plugin */
	case SLAPI_PLUGIN_ENTRY_FETCH_FUNC:
		(*(IFP *)value) = pblock->pb_plugin->plg_entryfetchfunc;
		break;
	
	case SLAPI_PLUGIN_ENTRY_STORE_FUNC:
		(*(IFP *)value) = pblock->pb_plugin->plg_entrystorefunc;
		break;

	case SLAPI_PLUGIN_ENABLED:
		*((int *)value) =  pblock->pb_plugin_enabled;
		break;

	/* DSE add parameters */
	case SLAPI_DSE_DONT_WRITE_WHEN_ADDING:
		(*(int *)value) = pblock->pb_dse_dont_add_write;
		break;

	/* DSE add parameters */
	case SLAPI_DSE_MERGE_WHEN_ADDING:
		(*(int *)value) = pblock->pb_dse_add_merge;
		break;

	/* DSE add parameters */
	case SLAPI_DSE_DONT_CHECK_DUPS:
		(*(int *)value) = pblock->pb_dse_dont_check_dups;
		break;

	/* DSE modify parameters */
	case SLAPI_DSE_REAPPLY_MODS:
		(*(int *)value) = pblock->pb_dse_reapply_mods;
		break;

	/* DSE read parameters */
	case SLAPI_DSE_IS_PRIMARY_FILE:
		(*(int *)value) = pblock->pb_dse_is_primary_file;
		break;
		
	/* used internally by schema code (schema.c) */
	case SLAPI_SCHEMA_FLAGS:
		(*(int *)value) = pblock->pb_schema_flags;
		break;

	case SLAPI_URP_NAMING_COLLISION_DN:
		(*(char **)value) = pblock->pb_urp_naming_collision_dn;
		break;
		
	case SLAPI_URP_TOMBSTONE_UNIQUEID:
		(*(char **)value) = pblock->pb_urp_tombstone_uniqueid;
		break;
		
	case SLAPI_SEARCH_CTRLS:
		(*(LDAPControl ***)value) = pblock->pb_search_ctrls;
		break;
		
	case SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED:
		(*(int *)value) = pblock->pb_syntax_filter_normalized;
		break;
		
	case SLAPI_PLUGIN_SYNTAX_FILTER_DATA:
		(*(void **)value) = pblock->pb_syntax_filter_data;
		break;
		
	case SLAPI_PAGED_RESULTS_INDEX:
		if (op_is_pagedresults(pblock->pb_op)) {
			/* search req is simple paged results */
			(*(int *)value) = pblock->pb_paged_results_index;
		} else {
			(*(int *)value) = -1;
		}
		break;
	default:
		LDAPDebug( LDAP_DEBUG_ANY,
		    "Unknown parameter block argument %d\n", arg, 0, 0 );
		return( -1 );
	}

	return( 0 );
}

int
slapi_pblock_set( Slapi_PBlock *pblock, int arg, void *value )
{
    char *authtype;

	PR_ASSERT( NULL != pblock );

	switch ( arg ) {
	case SLAPI_BACKEND:
		pblock->pb_backend = (Slapi_Backend *) value;
		break;
	case SLAPI_BACKEND_COUNT:
		pblock->pb_backend_count = *((int *) value);
		break;
	case SLAPI_CONNECTION:
		pblock->pb_conn = (Connection *) value;
		break;
	case SLAPI_OPERATION:
		pblock->pb_op = (Operation *) value;
		break;
	case SLAPI_OPINITIATED_TIME:
		if (pblock->pb_op != NULL) {
			pblock->pb_op->o_time = *((time_t *) value);
		}
		break;
	case SLAPI_REQUESTOR_ISROOT:
		pblock->pb_requestor_isroot = *((int *) value);
		break;
	case SLAPI_IS_REPLICATED_OPERATION:
		PR_ASSERT(0);
		break;
	case SLAPI_OPERATION_PARAMETERS:
		PR_ASSERT(0);
		break;
	case SLAPI_CONN_ID:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_ID \n", 0, 0, 0 );
			return (-1);
		}
		pblock->pb_conn->c_connid = *((PRUint64 *) value);
		break;
	case SLAPI_CONN_DN:
            /*
             * Slightly crazy but we must pass a copy of the current
             * authtype into bind_credentials_set() since it will
             * free the current authtype.
             */
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_DN \n", 0, 0, 0 );
			return (-1);
		}
                slapi_pblock_get(pblock, SLAPI_CONN_AUTHMETHOD, &authtype);
                bind_credentials_set( pblock->pb_conn, authtype,
                                  (char *)value, NULL, NULL, NULL , NULL);
                slapi_ch_free((void**)&authtype);
                break;
	case SLAPI_CONN_AUTHTYPE: /* deprecated */
	case SLAPI_CONN_AUTHMETHOD:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_AUTHMETHOD \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
                slapi_ch_free((void**)&pblock->pb_conn->c_authtype);
		pblock->pb_conn->c_authtype = slapi_ch_strdup((char *) value);
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;
	case SLAPI_CONN_IS_REPLICATION_SESSION:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CONN_IS_REPLICATION_SESSION \n", 0, 0, 0 );
			return (-1);
		}
		PR_Lock( pblock->pb_conn->c_mutex );
		pblock->pb_conn->c_isreplication_session = *((int *) value);
		PR_Unlock( pblock->pb_conn->c_mutex );
		break;

	/* stuff related to config file processing */
	case SLAPI_CONFIG_FILENAME:
	case SLAPI_CONFIG_LINENO:
	case SLAPI_CONFIG_ARGC:
	case SLAPI_CONFIG_ARGV:
		return (-1);	/* deprecated since DS 5.0 (no longer useful) */

    /* pblock memory management */
    case SLAPI_DESTROY_CONTENT:
        pblock->pb_destroy_content = *((int *) value);
        break;        

	/* stuff related to the current plugin */
	case SLAPI_PLUGIN:
		pblock->pb_plugin = (struct slapdplugin *) value;
		break;
	case SLAPI_PLUGIN_PRIVATE:
		pblock->pb_plugin->plg_private = (void *) value;
		break;
	case SLAPI_PLUGIN_TYPE:
		pblock->pb_plugin->plg_type = *((int *) value);
		break;
	case SLAPI_PLUGIN_ARGV:
		pblock->pb_plugin->plg_argv = (char **) value;
		break;
	case SLAPI_PLUGIN_ARGC:
  		pblock->pb_plugin->plg_argc = *((int *) value);
		break;
	case SLAPI_PLUGIN_VERSION:
		pblock->pb_plugin->plg_version = (char *) value;
		break;
	case SLAPI_PLUGIN_PRECEDENCE:
		pblock->pb_plugin->plg_precedence = *((int *) value);
		break;
	case SLAPI_PLUGIN_OPRETURN:
		pblock->pb_opreturn = *((int *) value);
		break;
	case SLAPI_PLUGIN_OBJECT:
		pblock->pb_object = (void *) value;
		break;
	case SLAPI_PLUGIN_IDENTITY:
		pblock->pb_plugin_identity = (void*)value;
		break;
	case SLAPI_PLUGIN_CONFIG_AREA:
		pblock->pb_plugin_config_area = (char *) value;
		break;
	case SLAPI_PLUGIN_DESTROY_FN:
		pblock->pb_destroy_fn = (IFP) value;
		break;
	case SLAPI_PLUGIN_DESCRIPTION:
		pblock->pb_plugin->plg_desc = *((Slapi_PluginDesc *)value);
		break;
	case SLAPI_PLUGIN_INTOP_RESULT:
		pblock->pb_internal_op_result = *((int *) value);
		break;
	case SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES:
		pblock->pb_plugin_internal_search_op_entries = (Slapi_Entry **) value;
		break;
	case SLAPI_PLUGIN_INTOP_SEARCH_REFERRALS:
		pblock->pb_plugin_internal_search_op_referrals = (char **) value;
		break;
	case SLAPI_REQUESTOR_DN:
		if(pblock->pb_op == NULL){
			return (-1);
		}
		slapi_sdn_set_dn_byval((&pblock->pb_op->o_sdn),(char *)value);
		break;
	case SLAPI_REQUESTOR_SDN:
		if(pblock->pb_op == NULL){
			return (-1);
		}
		slapi_sdn_set_dn_byval((&pblock->pb_op->o_sdn),slapi_sdn_get_dn((Slapi_DN *)value));
		break;
	/* database plugin functions */
	case SLAPI_PLUGIN_DB_BIND_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bind = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_UNBIND_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_unbind = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_SEARCH_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_search = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_next_search_entry = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_next_search_entry_ext = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_ENTRY_RELEASE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_entry_release = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_SEARCH_RESULTS_RELEASE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_search_results_release = (VFPP) value;
		break;
	case SLAPI_PLUGIN_DB_PREV_SEARCH_RESULTS_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_prev_search_results = (VFP) value;
		break;
	case SLAPI_PLUGIN_DB_COMPARE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_compare = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_MODIFY_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_modify = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_MODRDN_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_modrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_ADD_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_add = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_DELETE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_delete = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_ABANDON_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_abandon = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_CONFIG_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_config = (IFP) value;
		break;
	case SLAPI_PLUGIN_CLOSE_FN:
		pblock->pb_plugin->plg_close = (IFP) value;
		break;
	case SLAPI_PLUGIN_CLEANUP_FN:
		pblock->pb_plugin->plg_cleanup = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_FLUSH_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_flush = (IFP) value;
		break;
	case SLAPI_PLUGIN_START_FN:
		pblock->pb_plugin->plg_start = (IFP) value;
		break;
	case SLAPI_PLUGIN_POSTSTART_FN:
		pblock->pb_plugin->plg_poststart = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_WIRE_IMPORT_FN:
		pblock->pb_plugin->plg_wire_import = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_ADD_SCHEMA_FN:
		pblock->pb_plugin->plg_add_schema = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_GET_INFO_FN:
		pblock->pb_plugin->plg_get_info = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_SET_INFO_FN:
		pblock->pb_plugin->plg_set_info = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_CTRL_INFO_FN:
		pblock->pb_plugin->plg_ctrl_info = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_SEQ_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_seq = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_ENTRY_FN:
		pblock->pb_plugin->plg_entry = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_REFERRAL_FN:
		pblock->pb_plugin->plg_referral = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_RESULT_FN:
		pblock->pb_plugin->plg_result = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_RMDB_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_rmdb = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_INIT_INSTANCE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_init_instance = (IFP) value;
		break;

	case SLAPI_PLUGIN_DB_LDIF2DB_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_ldif2db = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_DB2LDIF_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_db2ldif = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_DB2INDEX_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_db2index = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_ARCHIVE2DB_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_archive2db = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_DB2ARCHIVE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_db2archive = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_UPGRADEDB_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_upgradedb = (IFP) value;
		break;
    case SLAPI_PLUGIN_DB_UPGRADEDNFORMAT_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_upgradednformat = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_DBVERIFY_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_dbverify = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_BEGIN_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_begin = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_COMMIT_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_commit = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_ABORT_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_abort = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_SIZE_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_dbsize = (IFP) value;
		break;
	case SLAPI_PLUGIN_DB_TEST_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_un.plg_un_db.plg_un_db_dbtest = (IFP) value;
		break;
	/* database plugin-specific parameters */
	case SLAPI_PLUGIN_DB_NO_ACL:
        if ( pblock->pb_plugin && pblock->pb_plugin->plg_type != SLAPI_PLUGIN_DATABASE ) {
			return( -1 );
        }
		if ( NULL == pblock->pb_backend ) {
			return( -1 );
		}
		pblock->pb_backend->be_noacl = *((int *)value);
        break;


	/* extendedop plugin functions */
	case SLAPI_PLUGIN_EXT_OP_FN:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_exhandler = (IFP) value;
		break;
	case SLAPI_PLUGIN_EXT_OP_OIDLIST:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_exoids = (char **) value;
		ldapi_register_extended_op( (char **)value );
		break;
	case SLAPI_PLUGIN_EXT_OP_NAMELIST:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_EXTENDEDOP ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_exnames = (char **) value;
		break;

	/* preoperation plugin functions */
	case SLAPI_PLUGIN_PRE_BIND_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_prebind = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_UNBIND_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_preunbind = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_SEARCH_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_presearch = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_COMPARE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_precompare = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_premodify = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_premodrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_preadd = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_predelete = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_ABANDON_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_preabandon = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_ENTRY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_preentry = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_REFERRAL_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_prereferral = (IFP) value;
		break;
	case SLAPI_PLUGIN_PRE_RESULT_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_preresult = (IFP) value;
		break;

	/* postoperation plugin functions */
	case SLAPI_PLUGIN_POST_BIND_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postbind = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_UNBIND_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postunbind = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_SEARCH_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postsearch = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_SEARCH_FAIL_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postsearchfail = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_COMPARE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postcompare = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postmodify = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postmodrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postadd = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postdelete = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_ABANDON_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postabandon = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_ENTRY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postentry = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_REFERRAL_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postreferral = (IFP) value;
		break;
	case SLAPI_PLUGIN_POST_RESULT_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_postresult = (IFP) value;
		break;

	/* backend preoperation plugin */
	case SLAPI_PLUGIN_BE_PRE_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepremodify = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_PRE_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepremodrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_PRE_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepreadd = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_PRE_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepredelete = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_PRE_CLOSE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepreclose = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_PRE_BACKUP_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_beprebackup = (IFP) value;
		break;

	/* backend postoperation plugin */
	case SLAPI_PLUGIN_BE_POST_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepostmodify = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_POST_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepostmodrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_POST_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepostadd = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_POST_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepostdelete = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_POST_OPEN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepostopen = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_POST_BACKUP_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BEPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_bepostbackup = (IFP) value;
		break;

	/* internal preoperation plugin */
	case SLAPI_PLUGIN_INTERNAL_PRE_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_internal_pre_modify = (IFP) value;
		break;
	case SLAPI_PLUGIN_INTERNAL_PRE_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_internal_pre_modrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_INTERNAL_PRE_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_internal_pre_add = (IFP) value;
		break;
	case SLAPI_PLUGIN_INTERNAL_PRE_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_PREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_internal_pre_delete = (IFP) value;
		break;

	/* internal postoperation plugin */
	case SLAPI_PLUGIN_INTERNAL_POST_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_internal_post_modify = (IFP) value;
		break;
	case SLAPI_PLUGIN_INTERNAL_POST_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_internal_post_modrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_INTERNAL_POST_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_internal_post_add = (IFP) value;
		break;
	case SLAPI_PLUGIN_INTERNAL_POST_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_INTERNAL_POSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_internal_post_delete = (IFP) value;
		break;
		
	/* backend preoperation plugin - called just after creating transaction */
	case SLAPI_PLUGIN_BE_TXN_PRE_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_betxnpremodify = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_TXN_PRE_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_betxnpremodrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_TXN_PRE_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_betxnpreadd = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_TXN_PRE_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPREOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_betxnpredelete = (IFP) value;
		break;

	/* backend postoperation plugin - called just before committing transaction */
	case SLAPI_PLUGIN_BE_TXN_POST_MODIFY_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_betxnpostmodify = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_MODRDN_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_betxnpostmodrdn = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_ADD_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_betxnpostadd = (IFP) value;
		break;
	case SLAPI_PLUGIN_BE_TXN_POST_DELETE_FN:
		if (pblock->pb_plugin->plg_type != SLAPI_PLUGIN_BETXNPOSTOPERATION) {
			return( -1 );
		}
		pblock->pb_plugin->plg_betxnpostdelete = (IFP) value;
		break;

	/* syntax plugin functions */
	case SLAPI_PLUGIN_SYNTAX_FILTER_AVA:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_filter_ava = (IFP) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_FILTER_SUB:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_filter_sub = (IFP) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_VALUES2KEYS:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_values2keys = (IFP) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_assertion2keys_ava = (IFP) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_assertion2keys_sub = (IFP) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_NAMES:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_names = (char **) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_OID:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_oid = (char *) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_FLAGS:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_flags = *((int *) value);
		break;
	case SLAPI_PLUGIN_SYNTAX_COMPARE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_compare = (IFP) value;
		break;
	case SLAPI_SYNTAX_SUBSTRLENS: /* aka SLAPI_MR_SUBSTRLENS */
		pblock->pb_substrlens = (int *) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_VALIDATE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_validate = (IFP) value;
		break;
	case SLAPI_PLUGIN_SYNTAX_NORMALIZE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_SYNTAX ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_syntax_normalize = (VFPV) value;
		break;
	case SLAPI_ENTRY_PRE_OP:
		pblock->pb_pre_op_entry = (Slapi_Entry *) value;
		break;
	case SLAPI_ENTRY_POST_OP:
		pblock->pb_post_op_entry = (Slapi_Entry *) value;
		break;

	/* target address for all operations */
	case SLAPI_TARGET_ADDRESS:
		PR_ASSERT (PR_FALSE);	/* can't do this */
		break;
	case SLAPI_TARGET_DN: /* DEPRECATED */
		/* slapi_pblock_set(pb, SLAPI_TARGET_DN, val) automatically
		 * replaces SLAPI_TARGET_SDN.  Caller should not free the 
		 * original SLAPI_TARGET_SDN, but the reset one here by getting
		 * the address using slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn). */
		if(pblock->pb_op!=NULL)
		{
			Slapi_DN *sdn = pblock->pb_op->o_params.target_address.sdn;
			slapi_sdn_free(&sdn);
			pblock->pb_op->o_params.target_address.sdn =
			                              slapi_sdn_new_dn_byval((char *)value);
		}
		else
		{
			return( -1 );
		}
		break;
	case SLAPI_TARGET_SDN:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.target_address.sdn = (Slapi_DN *)value;
		}
		else
		{
			return( -1 );
		}
		break;
	case SLAPI_ORIGINAL_TARGET_DN:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.target_address.udn = (char *)value;
		}
		break;
	case SLAPI_TARGET_UNIQUEID:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.target_address.uniqueid = (char *) value;
		}
		break;
	case SLAPI_REQCONTROLS:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.request_controls = (LDAPControl **) value;
		}
		break;
	case SLAPI_RESCONTROLS:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.result_controls = (LDAPControl **) value;
		}
		break;
	case SLAPI_CONTROLS_ARG:	/* used to pass control argument before operation is created */
		pblock->pb_ctrls_arg = (LDAPControl **) value;
		break;
	case SLAPI_ADD_RESCONTROL:
		if(pblock->pb_op!=NULL)
		{
			add_control( &pblock->pb_op->o_results.result_controls, (LDAPControl *)value );
		}
		break;

	/* notes to be added to the access log RESULT line for this op. */
	case SLAPI_OPERATION_NOTES:
		if ( value == NULL ) {
			pblock->pb_operation_notes = 0;	/* cleared */
		} else {
			pblock->pb_operation_notes |= *((unsigned int *)value );
		}
		break;
	case SLAPI_SKIP_MODIFIED_ATTRS:
		if(pblock->pb_op == NULL)
			break;
		if(value == 0){
			pblock->pb_op->o_flags &= ~OP_FLAG_SKIP_MODIFIED_ATTRS;
		} else {
			pblock->pb_op->o_flags |= OP_FLAG_SKIP_MODIFIED_ATTRS;
		}
		break;
	/* controls we know about */
	case SLAPI_MANAGEDSAIT:
		pblock->pb_managedsait = *((int *) value);
		break;
	case SLAPI_PWPOLICY:
		pblock->pb_pwpolicy_ctrl = *((int *) value);
		break;

	/* add arguments */
	case SLAPI_ADD_ENTRY:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_add.target_entry = (Slapi_Entry *) value;
		}
		break;
	case SLAPI_ADD_EXISTING_DN_ENTRY:
		pblock->pb_existing_dn_entry = (Slapi_Entry *) value;
		break;
	case SLAPI_ADD_EXISTING_UNIQUEID_ENTRY:
		pblock->pb_existing_uniqueid_entry = (Slapi_Entry *) value;
		break;
	case SLAPI_ADD_PARENT_ENTRY:
		pblock->pb_parent_entry = (Slapi_Entry *) value;
		break;
	case SLAPI_ADD_PARENT_UNIQUEID:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_add.parentuniqueid = (char *) value;
		}
		break;

	/* bind arguments */
	case SLAPI_BIND_METHOD:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_bind.bind_method = *((int *) value);
		}
		break;
	case SLAPI_BIND_CREDENTIALS:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_bind.bind_creds = (struct berval *) value;
		}
		break;
	case SLAPI_BIND_SASLMECHANISM:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_bind.bind_saslmechanism = (char *) value;
		}
		break;
	/* bind return values */
	case SLAPI_BIND_RET_SASLCREDS:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.r.r_bind.bind_ret_saslcreds = (struct berval *) value;
		}
		break;

	/* compare arguments */
	case SLAPI_COMPARE_TYPE:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_compare.compare_ava.ava_type = (char *) value;
		}
		break;
	case SLAPI_COMPARE_VALUE:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_compare.compare_ava.ava_value = *((struct berval *) value);
		}
		break;

	/* modify arguments */
	case SLAPI_MODIFY_MODS:
		PR_ASSERT(pblock->pb_op);
		if(pblock->pb_op!=NULL)
		{
			if(pblock->pb_op->o_params.operation_type==SLAPI_OPERATION_MODIFY)
			{
				pblock->pb_op->o_params.p.p_modify.modify_mods = (LDAPMod **) value;
			}
			else if(pblock->pb_op->o_params.operation_type==SLAPI_OPERATION_MODRDN)
			{
				pblock->pb_op->o_params.p.p_modrdn.modrdn_mods = (LDAPMod **) value;
			}
			else
			{
				PR_ASSERT(0);
			}
		}
		break;

	/* modrdn arguments */
	case SLAPI_MODRDN_NEWRDN:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_modrdn.modrdn_newrdn = (char *) value;
		}
		break;
	case SLAPI_MODRDN_DELOLDRDN:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_modrdn.modrdn_deloldrdn = *((int *) value);
		}
		break;
	case SLAPI_MODRDN_NEWSUPERIOR: /* DEPRECATED */
		if(pblock->pb_op!=NULL)
		{
			Slapi_DN *sdn =
			  pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn;
			slapi_sdn_free(&sdn);
			pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn = 
			                              slapi_sdn_new_dn_byval((char *)value);
		}
		else
		{
			return -1;
		}
		break;
	case SLAPI_MODRDN_NEWSUPERIOR_SDN:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_modrdn.modrdn_newsuperior_address.sdn =
			                                                 (Slapi_DN *) value;
		}
		else
		{
			return -1;
		}
		break;
	case SLAPI_MODRDN_PARENT_ENTRY:
		pblock->pb_parent_entry = (Slapi_Entry *) value;
		break;
	case SLAPI_MODRDN_NEWPARENT_ENTRY:
		pblock->pb_newparent_entry = (Slapi_Entry *) value;
		break;
	case SLAPI_MODRDN_TARGET_ENTRY:
		pblock->pb_target_entry = (Slapi_Entry *) value;
		break;
	case SLAPI_MODRDN_NEWSUPERIOR_ADDRESS:
		PR_ASSERT (PR_FALSE);	/* can't do this */

	/* search arguments */
	case SLAPI_SEARCH_SCOPE:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_scope = *((int *) value);
		}
		break;
	case SLAPI_SEARCH_DEREF:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_deref = *((int *) value);
		}
		break;
	case SLAPI_SEARCH_SIZELIMIT:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_sizelimit = *((int *) value);
		}
		break;
	case SLAPI_SEARCH_TIMELIMIT:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_timelimit = *((int *) value);
		}
		break;
	case SLAPI_SEARCH_FILTER:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_filter = (struct slapi_filter *) value;
		}
		break;
	case SLAPI_SEARCH_STRFILTER:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_strfilter = (char *) value;
		}
		break;
	case SLAPI_SEARCH_ATTRS:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_attrs = (char **) value;
		}
		break;
	case SLAPI_SEARCH_GERATTRS:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_gerattrs = (char **) value;
		}
		break;
	case SLAPI_SEARCH_ATTRSONLY:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_attrsonly = *((int *) value);
		}
		break;
	case SLAPI_SEARCH_IS_AND:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_search.search_is_and = *((int *) value);
		}
		break;

	/* abandon operation arguments */
	case SLAPI_ABANDON_MSGID:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_abandon.abandon_targetmsgid = *((int *) value);
		}
		break;

	/* extended operation arguments */
	case SLAPI_EXT_OP_REQ_OID:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_extended.exop_oid = (char *) value;
		}
		break;
	case SLAPI_EXT_OP_REQ_VALUE:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_params.p.p_extended.exop_value = (struct berval *) value;
		}
		break;
	/* extended operation return values */
	case SLAPI_EXT_OP_RET_OID:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.r.r_extended.exop_ret_oid = (char *) value;
		}
		break;
	case SLAPI_EXT_OP_RET_VALUE:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.r.r_extended.exop_ret_value = (struct berval *) value;
		}
		break;

	/* matching rule plugin functions */
	case SLAPI_PLUGIN_MR_FILTER_CREATE_FN:
		SLAPI_PLUGIN_TYPE_CHECK (pblock, SLAPI_PLUGIN_MATCHINGRULE);
		pblock->pb_plugin->plg_mr_filter_create = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_INDEXER_CREATE_FN:
		SLAPI_PLUGIN_TYPE_CHECK (pblock, SLAPI_PLUGIN_MATCHINGRULE);
		pblock->pb_plugin->plg_mr_indexer_create = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_FILTER_MATCH_FN:
		pblock->pb_mr_filter_match_fn = (mrFilterMatchFn) value;
		break;
	case SLAPI_PLUGIN_MR_FILTER_INDEX_FN:
		pblock->pb_mr_filter_index_fn = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_FILTER_RESET_FN:
		pblock->pb_mr_filter_reset_fn = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_INDEX_FN:
		pblock->pb_mr_index_fn = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_INDEX_SV_FN:
		pblock->pb_mr_index_sv_fn = (IFP) value;
		break;

	/* matching rule plugin arguments */
	case SLAPI_PLUGIN_MR_OID:
		pblock->pb_mr_oid = (char *) value;
		break;
	case SLAPI_PLUGIN_MR_TYPE:
		pblock->pb_mr_type = (char *) value;
		break;
	case SLAPI_PLUGIN_MR_VALUE:
		pblock->pb_mr_value = (struct berval *) value;
		break;
	case SLAPI_PLUGIN_MR_VALUES:
		pblock->pb_mr_values = (struct berval **) value;
		break;
	case SLAPI_PLUGIN_MR_KEYS:
		pblock->pb_mr_keys = (struct berval **) value;
		break;
	case SLAPI_PLUGIN_MR_FILTER_REUSABLE:
		pblock->pb_mr_filter_reusable = *(unsigned int *) value;
		break;
	case SLAPI_PLUGIN_MR_QUERY_OPERATOR:
		pblock->pb_mr_query_operator = *(int *) value;
		break;
	case SLAPI_PLUGIN_MR_USAGE:
		pblock->pb_mr_usage = *(unsigned int *) value;
		break;

	/* new style matching rule syntax plugin functions */
	case SLAPI_PLUGIN_MR_FILTER_AVA:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_filter_ava = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_FILTER_SUB:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_filter_sub = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_VALUES2KEYS:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_values2keys = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_ASSERTION2KEYS_AVA:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_assertion2keys_ava = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_ASSERTION2KEYS_SUB:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_assertion2keys_sub = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_FLAGS:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_flags = *((int *) value);
		break;
	case SLAPI_PLUGIN_MR_NAMES:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_names = (char **) value;
		break;
	case SLAPI_PLUGIN_MR_COMPARE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_compare = (IFP) value;
		break;
	case SLAPI_PLUGIN_MR_NORMALIZE:
		if ( pblock->pb_plugin->plg_type != SLAPI_PLUGIN_MATCHINGRULE ) {
			return( -1 );
		}
		pblock->pb_plugin->plg_mr_normalize = (VFPV) value;
		break;

	/* seq arguments */
	case SLAPI_SEQ_TYPE:
		pblock->pb_seq_type = *((int *)value);
		break;
	case SLAPI_SEQ_ATTRNAME:
		pblock->pb_seq_attrname = (char *) value;
		break;
	case SLAPI_SEQ_VAL:
		pblock->pb_seq_val = (char *) value;
		break;

    /* ldif2db arguments */
    case SLAPI_LDIF2DB_FILE:
        pblock->pb_ldif_file = (char *) value;
        break;
	case SLAPI_LDIF2DB_REMOVEDUPVALS:
		pblock->pb_removedupvals = *((int *)value);
		break;
	case SLAPI_DB2INDEX_ATTRS:
		pblock->pb_db2index_attrs = (char **) value;
		break;
	case SLAPI_LDIF2DB_NOATTRINDEXES:
		pblock->pb_ldif2db_noattrindexes = *((int *)value);
		break;
	case SLAPI_LDIF2DB_GENERATE_UNIQUEID:
		pblock->pb_ldif_generate_uniqueid = *((int *)value);
		break;
	case SLAPI_LDIF2DB_NAMESPACEID:
		pblock->pb_ldif_namespaceid = (char *)value;
		break;

	/* db2ldif arguments */
	case SLAPI_DB2LDIF_PRINTKEY:
		pblock->pb_ldif_printkey = *((int *)value);
		break;
	case SLAPI_DB2LDIF_DUMP_UNIQUEID:
		pblock->pb_ldif_dump_uniqueid = *((int *)value);
		break;

	/* db2ldif/ldif2db/db2bak/bak2db arguments */
	case SLAPI_BACKEND_INSTANCE_NAME:
		pblock->pb_instance_name = (char *) value;
		break;
        case SLAPI_BACKEND_TASK:
                pblock->pb_task = (Slapi_Task *)value;
                break;
	case SLAPI_TASK_FLAGS:
		pblock->pb_task_flags = *((int *)value);
		break;
	case SLAPI_DB2LDIF_SERVER_RUNNING:
		pblock->pb_server_running = *((int *)value);
		break;
        case SLAPI_BULK_IMPORT_ENTRY:
                pblock->pb_import_entry = (Slapi_Entry *)value;
                break;
        case SLAPI_BULK_IMPORT_STATE:
                pblock->pb_import_state = *((int *)value);
                break;
		
	/* transaction arguments */
	case SLAPI_PARENT_TXN:
		pblock->pb_parent_txn = (void *)value;
		break;
	case SLAPI_TXN:
		pblock->pb_txn = (void *)value;
		break;
	case SLAPI_TXN_RUV_MODS_FN:
		pblock->pb_txn_ruv_mods_fn = (IFP) value;
		break;

	/* Search results set */
	case SLAPI_SEARCH_RESULT_SET:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.r.r_search.search_result_set = (void *)value;
		}
		break;
	/* estimated search result set size */
	case SLAPI_SEARCH_RESULT_SET_SIZE_ESTIMATE:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.r.r_search.estimate = *(int *)value;
		}
		break;
	/* Search result - entry returned from iterating over result set */
	case SLAPI_SEARCH_RESULT_ENTRY:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.r.r_search.search_result_entry = (void *)value;
		}
		break;
	case SLAPI_SEARCH_RESULT_ENTRY_EXT:
		if(pblock->pb_op!=NULL)
		{
        		pblock->pb_op->o_results.r.r_search.opaque_backend_ptr = (void *)value;
		}
		break;
	/* Number of entries returned from search */
	case SLAPI_NENTRIES:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.r.r_search.nentries = *((int *)value);
		}
		break;
	/* Referrals encountered while iterating over the result set */
	case SLAPI_SEARCH_REFERRALS:
		if(pblock->pb_op!=NULL)
		{
			pblock->pb_op->o_results.r.r_search.search_referrals = (struct berval **)value;
		}
		break;

	case SLAPI_RESULT_CODE:
		if (pblock->pb_op != NULL)
			pblock->pb_op->o_results.result_code = (* (int *) value);
		break;
	case SLAPI_RESULT_MATCHED:
		if (pblock->pb_op != NULL)
			pblock->pb_op->o_results.result_matched = (char *) value;
		break;
	case SLAPI_RESULT_TEXT:
		if (pblock->pb_op != NULL)
			pblock->pb_op->o_results.result_text = (char *) value;
		break;
	case SLAPI_PB_RESULT_TEXT:
		slapi_ch_free((void**)&(pblock->pb_result_text));
		pblock->pb_result_text = slapi_ch_strdup ((char *) value);
		break;

	/* Size of the database, in kb */
	case SLAPI_DBSIZE:
		pblock->pb_dbsize = *((unsigned int *)value);
		break;

	/* ACL Plugin */
	case SLAPI_PLUGIN_ACL_INIT:
        pblock->pb_plugin->plg_acl_init = (IFP) value;
		break;

	case SLAPI_PLUGIN_ACL_SYNTAX_CHECK:
      	pblock->pb_plugin->plg_acl_syntax_check = (IFP) value;
		break;
	case SLAPI_PLUGIN_ACL_ALLOW_ACCESS:
       	pblock->pb_plugin->plg_acl_access_allowed = (IFP) value;
		break;
	case SLAPI_PLUGIN_ACL_MODS_ALLOWED:
       	pblock->pb_plugin->plg_acl_mods_allowed = (IFP) value;
		break;
	case SLAPI_PLUGIN_ACL_MODS_UPDATE:
       	pblock->pb_plugin->plg_acl_mods_update = (IFP) value;
		break;
	case SLAPI_CLIENT_DNS:
		if (pblock->pb_conn == NULL) {
			LDAPDebug( LDAP_DEBUG_ANY,
		          "Connection is NULL and hence cannot access SLAPI_CLIENT_DNS \n", 0, 0, 0 );
			return (-1);
		}
		pblock->pb_conn->c_domain = *((struct berval ***) value );
		break;
	/* Command line arguments */
	case SLAPI_ARGC:
       	pblock->pb_slapd_argc= *((int *)value);
		break;
	case SLAPI_ARGV:
        	pblock->pb_slapd_argv = *((char***)value);
		break;

	/* Config file directory */
	case SLAPI_CONFIG_DIRECTORY:
			pblock->pb_slapd_configdir = (char *)value;
		break;

    /* password storage scheme (kexcoff) */
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_NAME:
        pblock->pb_plugin->plg_pwdstorageschemename = (char *)value; 
        break;
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_USER_PWD:
        pblock->pb_pwd_storage_scheme_user_passwd = (char *)value;
        break; 
 
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DB_PWD:
        pblock->pb_pwd_storage_scheme_db_passwd = (char *)value;
        break;
 
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_ENC_FN:
        pblock->pb_plugin->plg_pwdstorageschemeenc = (CFP)value;
        break;
 
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_DEC_FN:
        pblock->pb_plugin->plg_pwdstorageschemedec = (IFP)value;
        break;
 
    case SLAPI_PLUGIN_PWD_STORAGE_SCHEME_CMP_FN:
		/* must provide a comparison function */
		if ( value == NULL )
		{
			return(-1);
		}
        pblock->pb_plugin->plg_pwdstorageschemecmp = (IFP)value;
        break;

	/* entry fetch store */
	case SLAPI_PLUGIN_ENTRY_FETCH_FUNC:
			pblock->pb_plugin->plg_entryfetchfunc = (IFP) value;
		break;

	case SLAPI_PLUGIN_ENTRY_STORE_FUNC:
			pblock->pb_plugin->plg_entrystorefunc = (IFP) value;
		break;

	case SLAPI_PLUGIN_ENABLED:
		pblock->pb_plugin_enabled = *((int *)value);
		break;

	/* DSE add parameters */
	case SLAPI_DSE_DONT_WRITE_WHEN_ADDING:
		pblock->pb_dse_dont_add_write = *((int *)value);
		break;

	/* DSE add parameters */
	case SLAPI_DSE_MERGE_WHEN_ADDING:
		pblock->pb_dse_add_merge = *((int *)value);
		break;

	/* DSE add parameters */
	case SLAPI_DSE_DONT_CHECK_DUPS:
		pblock->pb_dse_dont_check_dups = *((int *)value);
		break;

	/* DSE modify parameters */
	case SLAPI_DSE_REAPPLY_MODS:
		pblock->pb_dse_reapply_mods = *((int *)value);
		break;

	/* DSE read parameters */
	case SLAPI_DSE_IS_PRIMARY_FILE:
		pblock->pb_dse_is_primary_file = *((int *)value);
		break;
		
	/* used internally by schema code (schema.c) */
	case SLAPI_SCHEMA_FLAGS:
		pblock->pb_schema_flags = *((int *)value);
		break;

	case SLAPI_URP_NAMING_COLLISION_DN:
		pblock->pb_urp_naming_collision_dn = (char *)value;
		break;

	case SLAPI_URP_TOMBSTONE_UNIQUEID:
		pblock->pb_urp_tombstone_uniqueid = (char *)value;
		break;
		
	case SLAPI_LDIF2DB_ENCRYPT:
	case SLAPI_DB2LDIF_DECRYPT:
		pblock->pb_ldif_encrypt = *((int *)value);
		break;

	case SLAPI_SEARCH_CTRLS:
		pblock->pb_search_ctrls = (LDAPControl **) value;
		break;

	case SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED:
		pblock->pb_syntax_filter_normalized = *((int *)value);
		break;

	case SLAPI_PLUGIN_SYNTAX_FILTER_DATA:
		pblock->pb_syntax_filter_data = (void *)value;
		break;

	case SLAPI_PAGED_RESULTS_INDEX:
		pblock->pb_paged_results_index = *(int *)value;
		break;

	default:
		LDAPDebug( LDAP_DEBUG_ANY,
		    "Unknown parameter block argument %d\n", arg, 0, 0 );
		return( -1 );
	}

	return( 0 );
}


/*
 * Clears (and free's as appropriate) the bind DN and related credentials
 * for the connection `conn'.
 *
 * If `lock_conn' is true, 'conn' is locked before touching it; otherwise
 * this function assumes that conn->c_mutex is ALREADY locked.
 *
 * If `clear_externalcreds' is true, the external DN, external authtype,
 * and client certificate are also cleared and free'd.
 *
 * Connection structure members that are potentially changed by this function:
 *		c_dn, c_isroot, c_authtype
 *		c_external_dn, c_external_authtype, c_client_cert
 *
 * This function might better belong on bind.c or perhaps connection.c but
 * it needs to be in libslapd because FE code and libslapd code calls it.
 */
void
bind_credentials_clear( Connection *conn, PRBool lock_conn,
		PRBool clear_externalcreds )
{
    if ( lock_conn ) {
        PR_Lock( conn->c_mutex );
    }

    if ( conn->c_dn != NULL ) {		/* a non-anonymous bind has occurred */
		reslimit_update_from_entry( conn, NULL );	/* clear resource limits */

        if ( conn->c_dn != conn->c_external_dn ) {
            slapi_ch_free((void**)&conn->c_dn);
        }
        conn->c_dn = NULL;
    }
    slapi_ch_free((void**)&conn->c_authtype);
    conn->c_isroot = 0;
    conn->c_authtype = slapi_ch_strdup(SLAPD_AUTH_NONE);

    if ( clear_externalcreds ) {
        slapi_ch_free( (void**)&conn->c_external_dn );
        conn->c_external_dn = NULL;
        conn->c_external_authtype = SLAPD_AUTH_NONE;
        if ( conn->c_client_cert ) {
            CERT_DestroyCertificate (conn->c_client_cert);
            conn->c_client_cert = NULL;
        }
    }

    if ( lock_conn ) {
        PR_Unlock( conn->c_mutex );
    }

}


/*
 * Clear and then set the bind DN and related credentials for the
 * connection `conn'.
 *
 * `authtype' should be one of the SLAPD_AUTH_... constants defined in
 * slapu-plugin.h or NULL.
 *
 * `normdn' must be a normalized DN and it must be malloc'd memory (it
 * is consumed by this function).  If there is an existing DN value
 * associated with the connection, it is free'd.  Pass NULL for `normdn'
 * to clear the DN.
 *
 * If `extauthtype' is non-NULL we also clear and then set the
 * external (e.g., SSL) credentials from the `externaldn' and `clientcert'.
 * Note that it is okay for `externaldn' and 'normdn' to have the same
 * (pointer) value.  This code and that in bind_credentials_clear()
 * is smart enough to know to only free the memory once.  Like `normdn',
 * `externaldn' and `clientcert' should be NULL or point to malloc'd memory
 * as they are both consumed by this function.
 * 
 * We also:
 *
 *   1) Test to see if the DN is the root DN and set the c_isroot flag
 *		appropriately.
 * And
 *
 *   2) Call the binder-based resource limits subsystem so it can
 *		update the per-connection resource limit data it maintains.
 *
 * Note that this function should ALWAYS be used instead of manipulating
 * conn->c_dn directly; otherwise, subsystems like the binder-based resource
 * limits (see resourcelimit.c) won't be called.
 *
 * It is also acceptable to set the DN via a call slapi_pblock_set(), e.g.,
 *
 *			slapi_pblock_set( pb, SLAPI_CONN_DN, ndn );
 *
 * because it calls this function.
 *
 * Connection structure members that are potentially changed by this function:
 *		c_dn, c_isroot, c_authtype
 *		c_external_dn, c_external_authtype, c_client_cert
 *
 * This function might better belong on bind.c or perhaps connection.c but
 * it needs to be in libslapd because FE code and libslapd code calls it.
 */
void
bind_credentials_set( Connection *conn, char *authtype, char *normdn,
		char *extauthtype, char *externaldn, CERTCertificate *clientcert, Slapi_Entry * bind_target_entry )
{
	PR_Lock( conn->c_mutex );
	bind_credentials_set_nolock(conn, authtype, normdn,
		extauthtype, externaldn, clientcert, bind_target_entry);
	PR_Unlock( conn->c_mutex );
}

void
bind_credentials_set_nolock( Connection *conn, char *authtype, char *normdn,
                char *extauthtype, char *externaldn, CERTCertificate *clientcert, Slapi_Entry * bind_target_entry )
{
	/* clear credentials */
	bind_credentials_clear( conn, PR_FALSE /* conn is already locked */,
			( extauthtype != NULL ) /* clear external creds. if requested */ );

	/* set primary credentials */
	slapi_ch_free((void**)&conn->c_authtype);
	conn->c_authtype = slapi_ch_strdup(authtype);
	conn->c_dn = normdn;
	conn->c_isroot = slapi_dn_isroot( normdn );

	/* Set the thread data with the normalized dn */
	slapi_td_set_dn(slapi_ch_strdup(normdn));

	/* set external credentials if requested */
	if ( extauthtype != NULL ) {
		conn->c_external_authtype = extauthtype;
		conn->c_external_dn = externaldn;
		conn->c_client_cert = clientcert;
	}


	/* notify binder-based resource limit subsystem about the change in DN */
	if ( !conn->c_isroot )
	{
		if ( conn->c_dn != NULL ) {
			if ( bind_target_entry == NULL )
			{
				Slapi_DN *sdn = slapi_sdn_new_normdn_byref( conn->c_dn );
				reslimit_update_from_dn( conn, sdn );
				slapi_sdn_free( &sdn );
			} else {
				reslimit_update_from_entry( conn, bind_target_entry );	
			}
		} else {
			char *anon_dn = config_get_anon_limits_dn();
			/* If an anonymous limits dn is set, use it to set the limits. */
			if (anon_dn && (strlen(anon_dn) > 0)) {
				Slapi_DN *anon_sdn = slapi_sdn_new_normdn_byref( anon_dn );
				reslimit_update_from_dn( conn, anon_sdn );
				slapi_sdn_free( &anon_sdn );
			}

			slapi_ch_free_string( &anon_dn );
		}
	}
}
