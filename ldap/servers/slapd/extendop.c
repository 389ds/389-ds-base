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

/* extendedop.c - handle an LDAPv3 extended operation */

#include <stdio.h>
#include "slap.h"

static const char *extended_op_oid2string( const char *oid );


/********** this stuff should probably be moved when it's done **********/

static void extop_handle_import_start(Slapi_PBlock *pb, char *extoid,
                                      struct berval *extval)
{
    char *orig = NULL;
    const char *suffix = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Backend *be = NULL;
    struct berval bv;
    int ret;

    if (extval == NULL || extval->bv_val == NULL) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "extop_handle_import_start: no data supplied\n", 0, 0, 0);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, 
                         "no data supplied", 0, NULL);
        return;
    }
    orig = slapi_ch_malloc(extval->bv_len+1);
    strncpy(orig, extval->bv_val, extval->bv_len);
    orig[extval->bv_len] = 0;
    /* Check if we should be performing strict validation. */
    if (config_get_dn_validate_strict()) {
        /* check that the dn is formatted correctly */
        ret = slapi_dn_syntax_check(pb, orig, 1);
        if (ret) { /* syntax check failed */
            LDAPDebug1Arg(LDAP_DEBUG_ANY,
                          "extop_handle_import_start: strict: invalid suffix\n",
                          orig);
            send_ldap_result(pb, LDAP_INVALID_DN_SYNTAX, NULL,
                             "invalid suffix", 0, NULL);
            return;
        }
    }
    sdn = slapi_sdn_new_dn_passin(orig);
    if (!sdn) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "extop_handle_import_start: out of memory\n", 0, 0, 0);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        return;
    }
	suffix = slapi_sdn_get_dn(sdn);
    /*    be = slapi_be_select(sdn); */
    be = slapi_mapping_tree_find_backend_for_sdn(sdn);
    if (be == NULL || be == defbackend_get_backend()) {
        /* might be instance name instead of suffix */
        be = slapi_be_select_by_instance_name(suffix);
    }
    if (be == NULL || be == defbackend_get_backend()) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "bulk import: invalid suffix or instance name '%s'\n",
                  suffix, 0, 0);
        send_ldap_result(pb, LDAP_NO_SUCH_OBJECT, NULL, 
                         "invalid suffix or instance name", 0, NULL);
        goto out;
    }

    slapi_pblock_set(pb, SLAPI_BACKEND, be);
	slapi_pblock_set( pb, SLAPI_REQUESTOR_ISROOT, &pb->pb_op->o_isroot );
	
	{
		/* Access Control Check to see if the client is
		 *  allowed to use task import
		 */
		char *dummyAttr = "dummy#attr";
		char *dummyAttrs[2] = { NULL, NULL };
		int rc = 0;
		char dn[128];
		Slapi_Entry *feature;

		/* slapi_str2entry modify its dn parameter so we must copy
		 * this string each time we call it !
		 */
		/* This dn is no need to be normalized. */
		PR_snprintf(dn, sizeof(dn), "dn: oid=%s,cn=features,cn=config",
			EXTOP_BULK_IMPORT_START_OID);

		dummyAttrs[0] = dummyAttr;
		feature = slapi_str2entry(dn, 0);
		rc = plugin_call_acl_plugin (pb, feature, dummyAttrs, NULL,
			 SLAPI_ACL_WRITE, ACLPLUGIN_ACCESS_DEFAULT, NULL);
		slapi_entry_free(feature);
		if (rc != LDAP_SUCCESS)
		{
			/* Client isn't allowed to do this. */
			send_ldap_result(pb, rc, NULL, NULL, 0, NULL);
			goto out;
		}
	}

    if (be->be_wire_import == NULL) {
        /* not supported by this backend */
        LDAPDebug(LDAP_DEBUG_ANY,
                  "bulk import attempted on '%s' (not supported)\n",
                  suffix, 0, 0);
        send_ldap_result(pb, LDAP_NOT_SUPPORTED, NULL, NULL, 0, NULL);
        goto out;
    }

    ret = SLAPI_UNIQUEID_GENERATE_TIME_BASED;
    slapi_pblock_set(pb, SLAPI_LDIF2DB_GENERATE_UNIQUEID, &ret);
    ret = SLAPI_BI_STATE_START;
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_STATE, &ret);
    ret = (*be->be_wire_import)(pb);
    if (ret != 0) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "extop_handle_import_start: error starting import (%d)\n",
                  ret, 0, 0);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        goto out;
    }

    /* okay, the import is starting now -- save the backend in the
     * connection block & mark this connection as belonging to a bulk import
     */
    PR_Lock(pb->pb_conn->c_mutex);
    pb->pb_conn->c_flags |= CONN_FLAG_IMPORT;
    pb->pb_conn->c_bi_backend = be;
    PR_Unlock(pb->pb_conn->c_mutex);

    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_OID, EXTOP_BULK_IMPORT_START_OID);
    bv.bv_val = NULL;
    bv.bv_len = 0;
    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_VALUE, &bv);
    send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
    LDAPDebug(LDAP_DEBUG_ANY,
              "Bulk import: begin import on '%s'.\n", suffix, 0, 0);

out:
    slapi_sdn_free(&sdn);
    return;
}

static void extop_handle_import_done(Slapi_PBlock *pb, char *extoid,
                                     struct berval *extval)
{
    Slapi_Backend *be;
    struct berval bv;
    int ret;

    PR_Lock(pb->pb_conn->c_mutex);
    pb->pb_conn->c_flags &= ~CONN_FLAG_IMPORT;
    be = pb->pb_conn->c_bi_backend;
    pb->pb_conn->c_bi_backend = NULL;
    PR_Unlock(pb->pb_conn->c_mutex);

    if ((be == NULL) || (be->be_wire_import == NULL)) {
        /* can this even happen? */
        LDAPDebug(LDAP_DEBUG_ANY,
                  "extop_handle_import_done: backend not supported\n",
                  0, 0, 0);
        send_ldap_result(pb, LDAP_NOT_SUPPORTED, NULL, NULL, 0, NULL);
        return;
    }

    /* signal "done" to the backend */
    slapi_pblock_set(pb, SLAPI_BACKEND, be);
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_ENTRY, NULL);
    ret = SLAPI_BI_STATE_DONE;
    slapi_pblock_set(pb, SLAPI_BULK_IMPORT_STATE, &ret);
    ret = (*be->be_wire_import)(pb);
    if (ret != 0) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "bulk import: error ending import (%d)\n",
                  ret, 0, 0);
        send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL, NULL, 0, NULL);
        return;
    }

    /* more goofiness */
    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_OID, EXTOP_BULK_IMPORT_DONE_OID);
    bv.bv_val = NULL;
    bv.bv_len = 0;
    slapi_pblock_set(pb, SLAPI_EXT_OP_RET_VALUE, &bv);
    send_ldap_result(pb, LDAP_SUCCESS, NULL, NULL, 0, NULL);
    LDAPDebug(LDAP_DEBUG_ANY,
              "Bulk import completed successfully.\n", 0, 0, 0);
    return;
}


void
do_extended( Slapi_PBlock *pb )
{
	char		*extoid = NULL,	*errmsg;
	struct berval	extval = {0};
	int		lderr, rc;
	ber_len_t	len;
	ber_tag_t	tag;
	const char	*name;

	LDAPDebug( LDAP_DEBUG_TRACE, "do_extended\n", 0, 0, 0 );

	/*
	 * Parse the extended request. It looks like this:
	 *
	 *	ExtendedRequest := [APPLICATION 23] SEQUENCE {
	 *		requestName	[0]	LDAPOID,
	 *		requestValue	[1]	OCTET STRING OPTIONAL
	 *	}
	 */

	if ( ber_scanf( pb->pb_op->o_ber, "{a", &extoid )
	    == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY,
		    "ber_scanf failed (op=extended; params=OID)\n",
		    0, 0, 0 );
		op_shared_log_error_access (pb, "EXT", "???", "decoding error: fail to get extension OID");
		send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, "decoding error", 0,
		    NULL );
		goto free_and_return;
	}
	tag = ber_peek_tag(pb->pb_op->o_ber, &len);

	if (tag == LDAP_TAG_EXOP_REQ_VALUE) {
		if ( ber_scanf( pb->pb_op->o_ber, "o}", &extval ) == LBER_ERROR ) {
			op_shared_log_error_access (pb, "EXT", "???", "decoding error: fail to get extension value");
			send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, "decoding error", 0,
							  NULL );
			goto free_and_return;
		}
	} else {
		if ( ber_scanf( pb->pb_op->o_ber, "}") == LBER_ERROR ) {
			op_shared_log_error_access (pb, "EXT", "???", "decoding error"); 
			send_ldap_result( pb, LDAP_PROTOCOL_ERROR, NULL, "decoding error", 0,
							  NULL );
			goto free_and_return;
		}
	}
	if ( NULL == ( name = extended_op_oid2string( extoid ))) {
		LDAPDebug( LDAP_DEBUG_ARGS, "do_extended: oid (%s)\n", extoid, 0, 0 );

		slapi_log_access( LDAP_DEBUG_STATS, "conn=%" NSPRIu64 " op=%d EXT oid=\"%s\"\n",
				(long long unsigned int)pb->pb_conn->c_connid, pb->pb_op->o_opid, extoid );
	} else {
		LDAPDebug( LDAP_DEBUG_ARGS, "do_extended: oid (%s-%s)\n",
				extoid, name, 0 );

		slapi_log_access( LDAP_DEBUG_STATS,
			"conn=%" NSPRIu64 " op=%d EXT oid=\"%s\" name=\"%s\"\n",
			(long long unsigned int)pb->pb_conn->c_connid, pb->pb_op->o_opid, extoid, name );
	}

	/* during a bulk import, only BULK_IMPORT_DONE is allowed! 
	 * (and this is the only time it's allowed)
	 */
	if (pb->pb_conn->c_flags & CONN_FLAG_IMPORT) {
		if (strcmp(extoid, EXTOP_BULK_IMPORT_DONE_OID) != 0) {
			send_ldap_result(pb, LDAP_PROTOCOL_ERROR, NULL, NULL, 0, NULL);
			goto free_and_return;
		}
		extop_handle_import_done(pb, extoid, &extval);
		goto free_and_return;
	}
	
	if (strcmp(extoid, EXTOP_BULK_IMPORT_START_OID) == 0) {
		extop_handle_import_start(pb, extoid, &extval);
		goto free_and_return;
	}

	if (strcmp(extoid, START_TLS_OID) != 0) {
		int minssf = config_get_minssf();

		/* If anonymous access is disabled and we haven't
		 * authenticated yet, only allow startTLS. */
		if ((config_get_anon_access_switch() != SLAPD_ANON_ACCESS_ON) && ((pb->pb_op->o_authtype == NULL) ||
    		        (strcasecmp(pb->pb_op->o_authtype, SLAPD_AUTH_NONE) == 0))) {
			send_ldap_result( pb, LDAP_INAPPROPRIATE_AUTH, NULL,
				"Anonymous access is not allowed.", 0, NULL );
			goto free_and_return;
		}

		/* If the minssf is not met, only allow startTLS. */
		if ((pb->pb_conn->c_sasl_ssf < minssf) && (pb->pb_conn->c_ssl_ssf < minssf) &&
		    (pb->pb_conn->c_local_ssf < minssf)) {
			send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL,
				"Minimum SSF not met.", 0, NULL );
			goto free_and_return;
		}
	}

	/* If a password change is required, only allow the password
	 * modify extended operation */
	if (!pb->pb_conn->c_isreplication_session &&
                pb->pb_conn->c_needpw && (strcmp(extoid, EXTOP_PASSWD_OID) != 0))
	{
		char *dn = NULL;
		slapi_pblock_get(pb, SLAPI_CONN_DN, &dn);

		(void)slapi_add_pwd_control ( pb, LDAP_CONTROL_PWEXPIRED, 0);
		op_shared_log_error_access (pb, "EXT", dn ? dn : "", "need new password");
		send_ldap_result( pb, LDAP_UNWILLING_TO_PERFORM, NULL, NULL, 0, NULL );

		slapi_ch_free_string(&dn);
		goto free_and_return;
	}

	/* decode the optional controls - put them in the pblock */
	if ( (lderr = get_ldapmessage_controls( pb, pb->pb_op->o_ber, NULL )) != 0 )
	{
		char *dn = NULL;
		slapi_pblock_get(pb, SLAPI_CONN_DN, &dn);

		op_shared_log_error_access (pb, "EXT", dn ? dn : "", "failed to decode LDAP controls");
		send_ldap_result( pb, lderr, NULL, NULL, 0, NULL );

		slapi_ch_free_string(&dn);
		goto free_and_return;
	}

	slapi_pblock_set( pb, SLAPI_EXT_OP_REQ_OID, extoid );
	slapi_pblock_set( pb, SLAPI_EXT_OP_REQ_VALUE, &extval );
	rc = plugin_call_exop_plugins( pb, extoid );

	if ( SLAPI_PLUGIN_EXTENDED_SENT_RESULT != rc ) {
		if ( SLAPI_PLUGIN_EXTENDED_NOT_HANDLED == rc ) {
			lderr = LDAP_PROTOCOL_ERROR;	/* no plugin handled the op */
			errmsg = "unsupported extended operation";
		} else {
			errmsg = NULL;
			lderr = rc;
		}
		send_ldap_result( pb, lderr, NULL, errmsg, 0, NULL );
	}
free_and_return:
	if (extoid)
		slapi_ch_free((void **)&extoid);
	if (extval.bv_val)
		slapi_ch_free((void **)&extval.bv_val);
	return;
}


static const char *
extended_op_oid2string( const char *oid )
{
	const char *rval = NULL;

	if ( 0 == strcmp(oid, EXTOP_BULK_IMPORT_START_OID)) {
		rval = "Bulk Import Start";
	} else if ( 0 == strcmp(oid, EXTOP_BULK_IMPORT_DONE_OID)) {
		rval = "Bulk Import End";
	} else {
		rval = plugin_extended_op_oid2string( oid );
	}

	return( rval );
}
