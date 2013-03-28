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
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "slap.h"
#include "pratom.h"
#include "snmp_collator.h"

static void log_search_access (Slapi_PBlock *pb, const char *base, int scope, const char *filter, const char *msg);

void
do_search( Slapi_PBlock *pb )
{
	Slapi_Operation *operation;
	BerElement	*ber;
	int			i, err, attrsonly;
	ber_int_t		scope, deref, sizelimit, timelimit;
	char		*rawbase = NULL;
	char		*base = NULL, *fstr = NULL;
	struct slapi_filter	*filter = NULL;
	char		**attrs = NULL;
	char		**gerattrs = NULL;
	int			psearch = 0;
	struct berval	*psbvp;
	ber_int_t		changetypes;
	int			send_entchg_controls;
	int			changesonly = 0;
	int			rc = -1;
	int strict = 0;
	int minssf_exclude_rootdse = 0;
	int filter_normalized = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "do_search\n", 0, 0, 0 );

	slapi_pblock_get( pb, SLAPI_OPERATION, &operation);
	ber = operation->o_ber;

	/* count the search request */
	slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsSearchOps);

	/*
	 * Parse the search request.  It looks like this:
	 *
	 *	SearchRequest := [APPLICATION 3] SEQUENCE {
	 *		baseObject	DistinguishedName,
	 *		scope		ENUMERATED {
	 *			baseObject	(0),
	 *			singleLevel	(1),
	 *			wholeSubtree	(2)
	 *		},
	 *		derefAliases	ENUMERATED {
	 *			neverDerefaliases	(0),
	 *			derefInSearching	(1),
	 *			derefFindingBaseObj	(2),
	 *			alwaysDerefAliases	(3)
	 *		},
	 *		sizelimit	INTEGER (0 .. 65535),
	 *		timelimit	INTEGER (0 .. 65535),
	 *		attrsOnly	BOOLEAN,
	 *		filter		Filter,
	 *		attributes	SEQUENCE OF AttributeType
	 *	}
	 */

	/* baseObject, scope, derefAliases, sizelimit, timelimit, attrsOnly */
	if ( ber_scanf( ber, "{aiiiib", &rawbase, &scope, &deref, &sizelimit, &timelimit, &attrsonly ) == LBER_ERROR ){
		slapi_ch_free((void**)&rawbase );
		log_search_access (pb, "???", -1, "???", "decoding error");
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL );
		return;
	}

	/* Check if we should be performing strict validation. */
	strict = config_get_dn_validate_strict();
	if (strict) {
		/* check that the dn is formatted correctly */
		rc = slapi_dn_syntax_check(pb, rawbase, 1);
		if (rc) { /* syntax check failed */
			op_shared_log_error_access(pb, "SRCH", 
							rawbase?rawbase:"", "strict: invalid dn");
			send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, 
							 NULL, "invalid dn", 0, NULL);
			slapi_ch_free((void **) &rawbase);
			return;
		}
	}

	/* If anonymous access is only allowed for searching the root DSE,
	 * we need to reject any other anonymous search attempts. */
	if ((slapi_sdn_get_dn(&(operation->o_sdn)) == NULL) &&
	    ((rawbase && strlen(rawbase) > 0) || (scope != LDAP_SCOPE_BASE)) &&
	    (config_get_anon_access_switch() == SLAPD_ANON_ACCESS_ROOTDSE)) {
		op_shared_log_error_access(pb, "SRCH", rawbase?rawbase:"",
		                           "anonymous search not allowed");

		send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL,
			"Anonymous access is not allowed.", 0, NULL );

		goto free_and_return;
	}

	/* 
	 * If nsslapd-minssf-exclude-rootdse is on, the minssf check has been
	 * postponed till this moment since we need to know whether the basedn
	 * is rootdse or not.
	 *
	 * If (minssf_exclude_rootdse && (basedn is rootdse),
	 * then we allow accessing rootdse.
	 * Otherwise, return Minimum SSF not met.
	 */
	minssf_exclude_rootdse = config_get_minssf_exclude_rootdse();
	if (!minssf_exclude_rootdse || (rawbase && strlen(rawbase) > 0)) {
		int minssf = 0;
		/* Check if the minimum SSF requirement has been met. */
		minssf = config_get_minssf();
		if ((pb->pb_conn->c_sasl_ssf < minssf) &&
		    (pb->pb_conn->c_ssl_ssf < minssf) &&
		    (pb->pb_conn->c_local_ssf < minssf)) {
			op_shared_log_error_access(pb, "SRCH", rawbase?rawbase:"",
			                           "Minimum SSF not met");
			send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
			                 "Minimum SSF not met.", 0, NULL);
			goto free_and_return;
		}
	}

	/*
	 * ignore negative time and size limits since they make no sense
	 */
	if ( timelimit < 0 ) {
		timelimit = 0;
	}
	if ( sizelimit < 0 ) {
		sizelimit = 0;
	}

	if ( scope != LDAP_SCOPE_BASE && scope != LDAP_SCOPE_ONELEVEL
	&& scope != LDAP_SCOPE_SUBTREE ) {
		log_search_access (pb, base, scope, "???", "Unknown search scope");
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL,
		                  "Unknown search scope", 0, NULL );
		goto free_and_return;
	}
	/* check and record the scope for snmp */
	if ( scope == LDAP_SCOPE_ONELEVEL) {
		/* count the one level search request */
		slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsOneLevelSearchOps);

	} else if (scope == LDAP_SCOPE_SUBTREE) {
		/* count the subtree search request */
		slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsWholeSubtreeSearchOps);
	}

	/* filter - returns a "normalized" version */
	filter = NULL;
	fstr = NULL;
	if ( (err = get_filter( pb->pb_conn, ber, scope, &filter, &fstr )) != 0 ) {
		char	*errtxt;

		if ( LDAP_UNWILLING_TO_PERFORM == err ) {
			errtxt = "The search filter is too deeply nested";
		} else {
			errtxt = "Bad search filter";
		} 
		log_search_access( pb, base, scope, "???", errtxt );
		send_ldap_result( pb, err, NULL, errtxt, 0, NULL );
		goto free_and_return;
	}

	/* attributes */
	attrs = NULL;
	if ( ber_scanf( ber, "{v}}", &attrs ) == LBER_ERROR ) {
		log_search_access (pb, base, scope, fstr, "decoding error");
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0,
		NULL );
		goto free_and_return;
	}

	/* 
	 * This search is performed against the legacy consumer, so ask explicitly
	 * for the aci attribute as it is an operational in 5.0
	 */
	if ( operation->o_flags & OP_FLAG_LEGACY_REPLICATION_DN )
	{
		/* If attrs==NULL in a 4.x request, means that we want all the attributes, as aci is 
		 * now operational, we need to ask it explicilty as well as all the attributes
		 */
		if ( (attrs == NULL) || (attrs[0] == NULL) )
		{
			charray_add(&attrs, slapi_attr_syntax_normalize("aci"));
			charray_add(&attrs, slapi_attr_syntax_normalize(LDAP_ALL_USER_ATTRS));
		}
	}

	if ( attrs != NULL ) {
		int gerattrsiz = 1;
		int gerattridx = 0;
		int aciin = 0;
		/* 
		 * . store gerattrs if any
		 * . add "aci" once if "*" is given
		 */
		for ( i = 0; attrs[i] != NULL; i++ )
		{
			char *p = NULL;
			/* check if @<objectclass> is included */
			p =  strchr(attrs[i], '@');
			if ( p && '\0' != *(p+1) )	/* don't store "*@", e.g. */
			{
				int j = 0;
				if (gerattridx + 1 >= gerattrsiz)
				{
					char **tmpgerattrs;
					gerattrsiz *= 2;
					tmpgerattrs = 
						(char **)slapi_ch_calloc(1, gerattrsiz*sizeof(char *));
					if (NULL != gerattrs)
					{
						memcpy(tmpgerattrs, gerattrs, gerattrsiz*sizeof(char *));
						slapi_ch_free((void **)&gerattrs);
					}
					gerattrs = tmpgerattrs;
				}
				for ( j = 0; gerattrs; j++ )
				{
					char *attri = NULL;
					if ( NULL == gerattrs[j] )
					{
						if (0 == j)
						{
							/* first time */
							gerattrs[gerattridx++] = attrs[i];
							/* get rid of "@<objectclass>" part from the attr
							   list, which is needed only in gerattr list */
							*p = '\0';
							attri = slapi_ch_strdup(attrs[i]);
							attrs[i] = attri;
							*p = '@';
						}
						else
						{
							break; /* done */
						}
					}
					else if ( 0 == strcasecmp( attrs[i], gerattrs[j] ))
					{
						/* skip if attrs[i] is already in gerattrs */
						continue;
					}
					else
					{
						char *q = strchr(gerattrs[j], '@'); /* q never be 0 */
						if ( 0 != strcasecmp( p+1, q+1 ))
						{
							/* you don't want to display the same template
							   entry multiple times */
							gerattrs[gerattridx++] = attrs[i];
						}
						/* get rid of "@<objectclass>" part from the attr 
						   list, which is needed only in gerattr list */
						*p = '\0';
						attri = slapi_ch_strdup(attrs[i]);
						attrs[i] = attri;
						*p = '@';
					}
				}
			}
			else if ( !aciin && strcasecmp(attrs[i], LDAP_ALL_USER_ATTRS) == 0 )
			{
				charray_add(&attrs, slapi_attr_syntax_normalize("aci"));
				aciin = 1;
			}
		}
		if (NULL != gerattrs)
		{
			gerattrs[gerattridx] = NULL;
		}

		if (config_get_return_orig_type_switch()) {
			/* return the original type, e.g., "sn (surname)" */
			operation->o_searchattrs = charray_dup( attrs );
			for ( i = 0; attrs[i] != NULL; i++ ) {
				char	*type;
				type = slapi_attr_syntax_normalize(attrs[i]);
				slapi_ch_free( (void**)&(attrs[i]) );
				attrs[i] = type;
			}
		} else {
			/* return the chopped type, e.g., "sn" */
			operation->o_searchattrs = NULL;
			for ( i = 0; attrs[i] != NULL; i++ ) {
				char *type;
				type = slapi_attr_syntax_normalize_ext(attrs[i], 
				                                    ATTR_SYNTAX_NORM_ORIG_ATTR);
				/* attrs[i] is consumed */
				charray_add(&operation->o_searchattrs, attrs[i]);
				attrs[i] = type;
			}
		}
	}
	if ( slapd_ldap_debug & LDAP_DEBUG_ARGS ) {
		char abuf[ 1024 ], *astr;

		if ( NULL == attrs ) {
			astr = "ALL";
		} else {
			strarray2str( attrs, abuf, sizeof( abuf ), 1 /* include quotes */);
			astr = abuf;
		}
		slapi_log_error( SLAPI_LOG_ARGS, NULL, "SRCH base=\"%s\" "
			"scope=%d deref=%d "
			"sizelimit=%d timelimit=%d attrsonly=%d filter=\"%s\" "
			"attrs=%s\n", base, scope, deref, sizelimit, timelimit,
		attrsonly, fstr, astr );
	}

	/*
	 * in LDAPv3 there can be optional control extensions on
	 * the end of an LDAPMessage. we need to read them in and
	 * pass them to the backend. get_ldapmessage_controls()
	 * reads the controls and sets any we know about in the pb.
	 */
	if ( (err = get_ldapmessage_controls( pb, ber, NULL )) != 0 ) {
		log_search_access (pb, base, scope, fstr, "failed to decode LDAP controls");
		send_ldap_result( pb, err, NULL, NULL, 0, NULL );
		goto free_and_return;
	}

	/* we support persistent search for regular operations only */
	if ( slapi_control_present( operation->o_params.request_controls,
								LDAP_CONTROL_PERSISTENTSEARCH, &psbvp, NULL )){
		operation_set_flag(operation, OP_FLAG_PS);
		psearch = 1;
		if ( ps_parse_control_value( psbvp, &changetypes,
									 &changesonly, &send_entchg_controls ) != LDAP_SUCCESS )
		{
			changetypes = LDAP_CHANGETYPE_ANY;
			send_entchg_controls = 0;
		}
		else if ( changesonly )
		{
			operation_set_flag(operation, OP_FLAG_PS_CHANGESONLY);
		}
	}

	slapi_pblock_set( pb, SLAPI_ORIGINAL_TARGET_DN, rawbase );
	slapi_pblock_set( pb, SLAPI_SEARCH_SCOPE, &scope );
	slapi_pblock_set( pb, SLAPI_SEARCH_DEREF, &deref );
	slapi_pblock_set( pb, SLAPI_SEARCH_FILTER, filter );
	slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED,
	                  &filter_normalized );
	slapi_pblock_set( pb, SLAPI_SEARCH_STRFILTER, fstr );
	slapi_pblock_set( pb, SLAPI_SEARCH_ATTRS, attrs );
	slapi_pblock_set( pb, SLAPI_SEARCH_GERATTRS, gerattrs );
	slapi_pblock_set( pb, SLAPI_SEARCH_ATTRSONLY, &attrsonly );
	slapi_pblock_set( pb, SLAPI_REQUESTOR_ISROOT, &operation->o_isroot );
	slapi_pblock_set( pb, SLAPI_SEARCH_SIZELIMIT, &sizelimit );
	slapi_pblock_set( pb, SLAPI_SEARCH_TIMELIMIT, &timelimit );

	op_shared_search (pb, psearch ? 0 : 1/* send result */);

	slapi_pblock_get (pb, SLAPI_PLUGIN_OPRETURN, &rc);
	slapi_pblock_get( pb, SLAPI_SEARCH_FILTER, &filter );
	
	if ( psearch && rc == 0 ) {
		ps_add( pb, changetypes, send_entchg_controls );
	}

free_and_return:;
	if ( !psearch || rc != 0 ) {
		slapi_ch_free_string(&fstr);
		slapi_filter_free( filter, 1 );
		charray_free( attrs );	/* passing NULL is fine */
		charray_free( gerattrs );	/* passing NULL is fine */
		/* 
		 * Fix for defect 526719 / 553356 : Persistent search op failed. 
		 * Marking it as non-persistent so that operation resources get freed 
		 */
		if (psearch){
			operation->o_flags &= ~OP_FLAG_PS;
		}
		/* we strdup'd this above - need to free */
		slapi_pblock_get(pb, SLAPI_ORIGINAL_TARGET_DN, &rawbase);
		slapi_ch_free_string(&rawbase);
	}
}

static void log_search_access (Slapi_PBlock *pb, const char *base, int scope, const char *fstr, const char *msg)
{
	slapi_log_access(LDAP_DEBUG_STATS,
					 "conn=%" NSPRIu64 " op=%d SRCH base=\"%s\" scope=%d filter=\"%s\", %s\n",
					 pb->pb_conn->c_connid, pb->pb_op->o_opid, 
					 base, scope, fstr, msg ? msg : "");

}
