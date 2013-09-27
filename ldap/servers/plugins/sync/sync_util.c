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
 * Copyright (C) 2013 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "sync.h"

static struct berval * create_syncinfo_value( int type, const char *cookie, const char **uuids);
static char * sync_cookie_get_server_info(Slapi_PBlock *pb);
static char * sync_cookie_get_client_info(Slapi_PBlock *pb);

/*
 * Parse the value from an LDAPv3 sync request control.  They look
 * like this:
 * 
 *    syncRequestValue ::= SEQUENCE {
 *	mode ENUMERATED {
 *		-- 0 unused
 *		refreshOnly	(1),
 *		-- 2 reserved
 *		refreshAndPersist (3)
 *	},
 *	cookie 		syncCookie OPTIONAL,
 *	reloadHint	BOOLEAN DEFAULT FALSE
 *   }
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 *
 */
int
sync_parse_control_value( struct berval *psbvp, ber_int_t *mode, int *reload, char **cookie )
{
    int rc= LDAP_SUCCESS;

    if ( psbvp->bv_len == 0 || psbvp->bv_val == NULL )
    {
    	rc= LDAP_PROTOCOL_ERROR;
    }
    else
    {
        BerElement *ber= ber_init( psbvp );
        if ( ber == NULL )
        {
        	rc= LDAP_OPERATIONS_ERROR;
        }
        else
        {
            if ( ber_scanf( ber, "{e", mode ) == LBER_ERROR )
            {
            	rc= LDAP_PROTOCOL_ERROR;
            } else if ( ber_scanf( ber, "a", cookie ) != LBER_ERROR )
            	ber_scanf( ber, "b}", reload );

       	    /* the ber encoding is no longer needed */
       	    ber_free(ber,1);
        }
    }

    return( rc );
}

char *
sync_nsuniqueid2uuid(const char *nsuniqueid)
{
	char *uuid;
	char u[17];

	u[0] = slapi_str_to_u8(nsuniqueid);
	u[1] = slapi_str_to_u8(nsuniqueid+2);
	u[2] = slapi_str_to_u8(nsuniqueid+4);
	u[3] = slapi_str_to_u8(nsuniqueid+6);

	u[4] = slapi_str_to_u8(nsuniqueid+9);
	u[5] = slapi_str_to_u8(nsuniqueid+11);
	u[6] = slapi_str_to_u8(nsuniqueid+13);
	u[7] = slapi_str_to_u8(nsuniqueid+15);

	u[8] = slapi_str_to_u8(nsuniqueid+18);
	u[9] = slapi_str_to_u8(nsuniqueid+20);
	u[10] = slapi_str_to_u8(nsuniqueid+22);
	u[11] = slapi_str_to_u8(nsuniqueid+24);

	u[12] = slapi_str_to_u8(nsuniqueid+27);
	u[13] = slapi_str_to_u8(nsuniqueid+29);
	u[14] = slapi_str_to_u8(nsuniqueid+31);
	u[15] = slapi_str_to_u8(nsuniqueid+33);

	u[16] = '\0';

	uuid = slapi_ch_smprintf("%s",(char *)u);

	return(uuid);
}

/*
 * 	syncStateValue ::= SEQUENCE {
 * 		state ENUMERATED {
 * 			present (0),
 * 			add (1),
 * 			modify (2),
 * 			delete (3)
 * 		},
 * 		entryUUID syncUUID,
 * 		cookie    syncCookie OPTIONAL
 *	}
 *
 */
int
sync_create_state_control( Slapi_Entry *e, LDAPControl **ctrlp, int type, Sync_Cookie *cookie)
{
	int		rc;
	BerElement	*ber;
	struct berval	*bvp;
	char *uuid;
	Slapi_Attr *attr;
	Slapi_Value *val;

	if ( ctrlp == NULL || ( ber = der_alloc()) == NULL ) {
		return( LDAP_OPERATIONS_ERROR );
	}

	*ctrlp = NULL;

	slapi_entry_attr_find(e, SLAPI_ATTR_UNIQUEID, &attr);
	slapi_attr_first_value(attr, &val);
	uuid = sync_nsuniqueid2uuid(slapi_value_get_string(val));
	if (( rc = ber_printf( ber, "{eo", type , uuid, 16 )) != -1 ) {
		if (cookie) {
			char *cookiestr = sync_cookie2str(cookie);
			rc = ber_printf( ber, "s}", cookiestr);
			slapi_ch_free((void **)&cookiestr);
		} else {
			rc = ber_printf( ber, "}");
		
		}
	}
	if (rc != -1) {
		rc = ber_flatten( ber, &bvp );
	}
    	ber_free( ber, 1 );
	slapi_ch_free((void **)&uuid);

	if ( rc == -1 ) {
		return( LDAP_OPERATIONS_ERROR );
	}

	*ctrlp = (LDAPControl *)slapi_ch_malloc( sizeof( LDAPControl ));
	(*ctrlp)->ldctl_iscritical = 0;
	(*ctrlp)->ldctl_oid = slapi_ch_strdup( LDAP_CONTROL_SYNC_STATE );
	(*ctrlp)->ldctl_value = *bvp;	/* struct copy */

	bvp->bv_val = NULL;
	ber_bvfree( bvp );

	return (LDAP_SUCCESS);
}

/*
 * 	syncDoneValue ::= SEQUENCE {
 * 		cookie    syncCookie OPTIONAL
 * 		refreshDeletes	BOOLEAN DEFAULT FALSE
 *	}
 *
 */
int
sync_create_sync_done_control( LDAPControl **ctrlp, int refresh, char *cookie)
{
	int		rc;
	BerElement	*ber;
	struct berval	*bvp;

	if ( ctrlp == NULL || ( ber = der_alloc()) == NULL ) {
		return( LDAP_OPERATIONS_ERROR );
	}

	*ctrlp = NULL;

	if (cookie) {
		if (( rc = ber_printf( ber, "{s", cookie)) != -1 ) {
			if (refresh) {
				rc = ber_printf( ber, "e}", refresh);
			} else {
				rc = ber_printf( ber, "}");
			}
		}
	} else {
			if (refresh) {
				rc = ber_printf( ber, "{e}", refresh);
			} else {
				rc = ber_printf( ber, "{}");
			}
	}
	if (rc != -1) {
		rc = ber_flatten( ber, &bvp );
    	}
	ber_free( ber, 1 );

	if ( rc == -1 ) {
		return( LDAP_OPERATIONS_ERROR );
	}

	*ctrlp = (LDAPControl *)slapi_ch_malloc( sizeof( LDAPControl ));
	(*ctrlp)->ldctl_iscritical = 0;
	(*ctrlp)->ldctl_oid = slapi_ch_strdup( LDAP_CONTROL_SYNC_DONE );
	(*ctrlp)->ldctl_value = *bvp;	/* struct copy */

	bvp->bv_val = NULL;
	ber_bvfree( bvp );

	return (LDAP_SUCCESS);

}

char *
sync_cookie2str(Sync_Cookie *cookie)
{
	char *cookiestr = NULL;

	if (cookie) {
		cookiestr = slapi_ch_smprintf("%s#%s#%d",
				cookie->cookie_server_signature,
				cookie->cookie_client_signature,
				cookie->cookie_change_info);
	}
	return(cookiestr);
}

int
sync_intermediate_msg (Slapi_PBlock *pb, int tag, Sync_Cookie *cookie, char **uuids)
{
	int rc;
	struct berval *syncInfo;
	LDAPControl *ctrlp = NULL;
	char *cookiestr = sync_cookie2str(cookie);

	syncInfo = create_syncinfo_value(tag, cookiestr, (const char **)uuids);

	rc = slapi_send_ldap_intermediate( pb,  &ctrlp, LDAP_SYNC_INFO, syncInfo);
	slapi_ch_free((void **)&cookiestr);
	ber_bvfree(syncInfo);
	return (rc);
}

int sync_result_msg (Slapi_PBlock *pb, Sync_Cookie *cookie)
{
	int rc = 0;
	char *cookiestr = sync_cookie2str(cookie);

	LDAPControl **ctrl = (LDAPControl **)slapi_ch_calloc(2, sizeof (LDAPControl *));
	sync_create_sync_done_control( &ctrl[0], 0, cookiestr);
	slapi_pblock_set(pb, SLAPI_RESCONTROLS, ctrl);
	slapi_send_ldap_result(pb, 0, NULL, NULL, 0, NULL );

	slapi_ch_free((void **)&cookiestr);
	return (rc);
}

int sync_result_err (Slapi_PBlock *pb, int err, char *msg)
{
	int rc = 0;

	slapi_send_ldap_result(pb, err, NULL, msg, 0, NULL );

	return (rc);
}

static struct berval *
create_syncinfo_value( int type, const char *cookie, const char **uuids)
{
	BerElement		*ber;
	struct berval	*bvp = NULL;

	if ( ( ber = der_alloc() ) == NULL ) {
		return( NULL );
	}

	switch (type) {
		case LDAP_TAG_SYNC_NEW_COOKIE:
			ber_printf(ber, "to", type, cookie);
			break;
		case LDAP_TAG_SYNC_REFRESH_DELETE:
		case LDAP_TAG_SYNC_REFRESH_PRESENT:
			ber_printf(ber, "t{", type);
			if (cookie)
				ber_printf(ber, "s", cookie);
			/* ber_printf(ber, "b",1); */
			ber_printf(ber, "}");
			break;
		case LDAP_TAG_SYNC_ID_SET:
			ber_printf(ber, "t{", type);
			if (cookie)
				ber_printf(ber, "s", cookie);
			if (uuids)
				ber_printf(ber, "b[v]", 1, uuids);
			ber_printf(ber, "}");
			break;
		default: 
			break;
	}
	ber_flatten( ber, &bvp );
	ber_free( ber, 1 );

	return( bvp );
}

static int 
sync_handle_cnum_entry(Slapi_Entry *e, void *cb_data)
{
	int rc = 0;
	Sync_CallBackData *cb = (Sync_CallBackData *)cb_data;
	Slapi_Value *sval=NULL;
	const struct berval *value;

	cb->changenr = 0;

	if ( NULL != e ) {
		Slapi_Attr *chattr = NULL;
		sval = NULL;
		value = NULL;
		if ( slapi_entry_attr_find( e, CL_ATTR_CHANGENUMBER, &chattr ) == 0 ) {
			slapi_attr_first_value( chattr,&sval );
			if ( NULL != sval ) {
				value = slapi_value_get_berval ( sval );
				if( NULL != value && NULL != value->bv_val &&
					'\0' != value->bv_val[0]) {
					cb->changenr = sync_number2int(value->bv_val);
				}
			}
		}
	}
	return (rc);
}

/*
 * a cookie is used to synchronize client server sessions,
 * it consist of three parts
 * -- server id, client should only sync with one server
 * -- client id, client should use same bind dn, and srch params 
 * -- change info, kind of state info like csn, ruv, 
 *  	in the first implementation use changenumber from retro cl
 *
 * syntax: <server-id>#<client-id>#change
 *
 */
static char *
sync_cookie_get_server_info(Slapi_PBlock *pb)
{
	char *info_enc;
	int rc = 0;
	Slapi_Entry **entries;
	Slapi_PBlock *srch_pb = NULL;
	char *host = NULL;
	char *port = NULL;
	char *server_attrs[] = {"nsslapd-localhost", "nsslapd-port", NULL};

	srch_pb = slapi_pblock_new ();
	slapi_search_internal_set_pb (srch_pb, "cn=config", LDAP_SCOPE_BASE,
		"objectclass=*", server_attrs, 0, NULL, NULL,
		plugin_get_default_component_id(), 0);
	slapi_search_internal_pb (srch_pb);
	slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
	if (rc != 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, SYNC_PLUGIN_SUBSYSTEM, "sync_cookie_get_server_info: "
			"unable to read server configuration: error %d\n", rc);
	}
	else
	{
		slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
		if (NULL == entries || NULL == entries[0])
		{
			slapi_log_error(SLAPI_LOG_FATAL, SYNC_PLUGIN_SUBSYSTEM, "sync_cookie_get_server_info: "
				"server configuration missing\n");
			rc = -1;
		}
		else
		{
			host = slapi_entry_attr_get_charptr(entries[0], "nsslapd-localhost");
			port = slapi_entry_attr_get_charptr(entries[0], "nsslapd-port");
		}
	}
	info_enc = slapi_ch_smprintf("%s:%s",host?host:"nohost",port?port:"noport");

	slapi_ch_free ((void**)&host);
	slapi_ch_free ((void**)&port);
	slapi_free_search_results_internal(srch_pb);
	slapi_pblock_destroy (srch_pb);
	return(info_enc);
}

static char *
sync_cookie_get_client_info(Slapi_PBlock *pb)
{
	char *targetdn;
	char *strfilter;
	char *clientdn;
	char *clientinfo;
	
	slapi_pblock_get(pb, SLAPI_TARGET_DN, &targetdn);
	slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &strfilter);
	slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &clientdn);
	clientinfo = slapi_ch_smprintf("%s:%s:%s",clientdn,targetdn,strfilter);
	return (clientinfo);
}
static int
sync_cookie_get_change_number(int lastnr, const char *uniqueid)
{
	Slapi_PBlock *srch_pb;
	Slapi_Entry **entries;
	Slapi_Entry *cl_entry;
	int rv;
	int newnr = -1;
	char *filter = slapi_ch_smprintf("(&(changenumber>=%d)(targetuniqueid=%s))",lastnr,uniqueid);

	srch_pb = slapi_pblock_new();
    	slapi_search_internal_set_pb(srch_pb, CL_SRCH_BASE,
           				LDAP_SCOPE_SUBTREE, filter,
            				NULL, 0, NULL, NULL, plugin_get_default_component_id(), 0);
 	slapi_search_internal_pb(srch_pb);
	slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_RESULT, &rv);
	if ( rv == LDAP_SUCCESS) {
		slapi_pblock_get(srch_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    		if (entries && *entries) {
			Slapi_Attr *attr;
			Slapi_Value *val;
			cl_entry = *entries; /* only use teh first one */
			slapi_entry_attr_find(cl_entry, CL_ATTR_CHANGENUMBER, &attr);
			slapi_attr_first_value(attr, &val);
			newnr = sync_number2int((char *)slapi_value_get_string(val));
		}
	}

	slapi_free_search_results_internal(srch_pb);
	slapi_pblock_destroy(srch_pb);
	slapi_ch_free((void **)&filter);
	return (newnr);
}

static int
sync_cookie_get_change_info(Sync_CallBackData *scbd)
{
	Slapi_PBlock *seq_pb;
	char *base;
	char *attrname;
	int rc;

	base = slapi_ch_strdup("cn=changelog");
	attrname = slapi_ch_strdup("changenumber");

	seq_pb = slapi_pblock_new();
	slapi_pblock_init(seq_pb);

	slapi_seq_internal_set_pb(seq_pb, base, SLAPI_SEQ_LAST, attrname, NULL, NULL, 0, 0, 
		plugin_get_default_component_id(), 0);							  

	rc = slapi_seq_internal_callback_pb (seq_pb, scbd, NULL, sync_handle_cnum_entry, NULL);
	slapi_pblock_destroy(seq_pb);

	slapi_ch_free((void **)&attrname);
	slapi_ch_free((void **)&base);

	return (rc);

}

Sync_Cookie *
sync_cookie_create (Slapi_PBlock *pb)
{

	Sync_CallBackData scbd;
	int rc;
	Sync_Cookie *sc = (Sync_Cookie *)slapi_ch_malloc(sizeof(Sync_Cookie));


	rc = sync_cookie_get_change_info (&scbd);

	if (rc == 0) {
		sc->cookie_server_signature = sync_cookie_get_server_info(pb);
		sc->cookie_client_signature = sync_cookie_get_client_info(pb);
		sc->cookie_change_info = scbd.changenr;
	} else {
		slapi_ch_free ((void **)&sc);
		sc = NULL;
	}

	return (sc);
}

void sync_cookie_update (Sync_Cookie *sc, Slapi_Entry *ec)
{
	const char *uniqueid = NULL;
	Slapi_Attr *attr;
	Slapi_Value *val;

	slapi_entry_attr_find(ec, SLAPI_ATTR_UNIQUEID, &attr);
	slapi_attr_first_value(attr, &val);
	uniqueid = slapi_value_get_string(val);

	sc->cookie_change_info = sync_cookie_get_change_number (sc->cookie_change_info, uniqueid);
}

Sync_Cookie *
sync_cookie_parse (char *cookie)
{
	char *p, *q;
	Sync_Cookie *sc;

	if (cookie == NULL || *cookie == '\0' ) {
		return NULL;
	}

	p = q = cookie;
	sc = (Sync_Cookie *)slapi_ch_malloc(sizeof(Sync_Cookie));

	sc->cookie_client_signature = NULL;
	sc->cookie_server_signature = NULL;
	sc->cookie_change_info = -1;
	p = strchr(q, '#');
	if (p) {
		*p = '\0';
		sc->cookie_server_signature = slapi_ch_strdup(q);
		q = p + 1;
		p = strchr(q, '#');
		if (p) {
			*p = '\0';
			sc->cookie_client_signature = slapi_ch_strdup(q);
			sc->cookie_change_info = sync_number2int(p+1);
		}
	}
		
	return (sc);
}

int
sync_cookie_isvalid (Sync_Cookie *testcookie, Sync_Cookie *refcookie)
{
	/* client and server info must match */
	if (strcmp(testcookie->cookie_client_signature,refcookie->cookie_client_signature) ||
		strcmp(testcookie->cookie_server_signature,refcookie->cookie_server_signature) || 
		testcookie->cookie_change_info == -1 || 
		testcookie->cookie_change_info > refcookie->cookie_change_info )
		return (0);
	/* could add an additional check if the requested state in client cookie is still
	 * available. Accept any state request for now.
	 */
	return (1);
}

void
sync_cookie_free (Sync_Cookie **freecookie)
{
	if (*freecookie) {
		slapi_ch_free((void **)&((*freecookie)->cookie_client_signature));
		slapi_ch_free((void **)&((*freecookie)->cookie_server_signature));
		slapi_ch_free((void **)freecookie);
	}
}

int sync_is_active_scope (const Slapi_DN *dn, Slapi_PBlock *pb)
{
	int rc;
	char *origbase = NULL;
	Slapi_DN *base = NULL;
	int		scope;

	slapi_pblock_get( pb, SLAPI_ORIGINAL_TARGET_DN, &origbase );
	slapi_pblock_get( pb, SLAPI_SEARCH_TARGET_SDN, &base );
	slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope );
	if (NULL == base) {
		base = slapi_sdn_new_dn_byref(origbase);
		slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, base);
	}
	if ( slapi_sdn_scope_test(dn, base, scope )) {
		rc = 1;
	} else {
		rc = 0;
	}

	return (rc);
}

int sync_is_active (Slapi_Entry *e, Slapi_PBlock *pb)
{
	if ( pb == NULL ) {
		/* not yet initialized */
		return(0);
	} else {
		/* check id entry is in scope of sync request */
		return(sync_is_active_scope( slapi_entry_get_sdn_const(e),pb));
	}
}


Slapi_PBlock *
sync_pblock_copy(Slapi_PBlock *src)
{
	Slapi_Operation *operation;
	Slapi_Connection *connection;
	int *scope;
	int *deref;
	int *filter_normalized;
	char *fstr;
	char **attrs, **attrs_dup;
	int *attrsonly;
	int *isroot;
	int *sizelimit;
	int *timelimit;
	struct slapdplugin *pi;


	slapi_pblock_get( src, SLAPI_OPERATION, &operation );
	slapi_pblock_get( src, SLAPI_CONNECTION, &connection );
	slapi_pblock_get( src, SLAPI_SEARCH_SCOPE, &scope );
	slapi_pblock_get( src, SLAPI_SEARCH_DEREF, &deref );
	slapi_pblock_get( src, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED, &filter_normalized );
	slapi_pblock_get( src, SLAPI_SEARCH_STRFILTER, &fstr );
	slapi_pblock_get( src, SLAPI_SEARCH_ATTRS, &attrs );
	slapi_pblock_get( src, SLAPI_SEARCH_ATTRSONLY, &attrsonly );
	slapi_pblock_get( src, SLAPI_REQUESTOR_ISROOT, &isroot );
	slapi_pblock_get( src, SLAPI_SEARCH_SIZELIMIT, &sizelimit );
	slapi_pblock_get( src, SLAPI_SEARCH_TIMELIMIT, &timelimit );
	slapi_pblock_get( src, SLAPI_PLUGIN, &pi);

	Slapi_PBlock *dest = slapi_pblock_new();

	slapi_pblock_set( dest, SLAPI_OPERATION, operation );
	slapi_pblock_set( dest, SLAPI_CONNECTION, connection );
	slapi_pblock_set( dest, SLAPI_SEARCH_SCOPE, &scope );
	slapi_pblock_set( dest, SLAPI_SEARCH_DEREF, &deref );
	slapi_pblock_set( dest, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED, &filter_normalized );
	slapi_pblock_set( dest, SLAPI_SEARCH_STRFILTER, slapi_ch_strdup(fstr) );
	attrs_dup = slapi_ch_array_dup(attrs);
	slapi_pblock_set( dest, SLAPI_SEARCH_ATTRS, attrs_dup );
	slapi_pblock_set( dest, SLAPI_SEARCH_ATTRSONLY, &attrsonly );
	slapi_pblock_set( dest, SLAPI_REQUESTOR_ISROOT, &isroot );
	slapi_pblock_set( dest, SLAPI_SEARCH_SIZELIMIT, &sizelimit );
	slapi_pblock_set( dest, SLAPI_SEARCH_TIMELIMIT, &timelimit );
	slapi_pblock_set( dest, SLAPI_PLUGIN, pi);

	return dest;
}

int sync_number2int(char *chgnrstr)
{
	char *end;
	int nr;
	nr = strtoul(chgnrstr, &end, 10);
	if ( *end == '\0') {
		return (nr);
	} else {
		return (-1);
	}
}
