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

/* result.c - routines to send ldap results, errors, and referrals */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <signal.h>
#include "slap.h"
#include "pratom.h"
#include "fe.h"
#include "vattr_spi.h"

#include <ssl.h>

static Slapi_Counter *num_entries_sent;
static Slapi_Counter *num_bytes_sent;

static long	current_conn_count;
static PRLock	*current_conn_count_mutex;

static int flush_ber( Slapi_PBlock *pb, Connection *conn,
					  Operation *op, BerElement *ber, int type );
static char *notes2str( unsigned int notes, char *buf, size_t buflen );
static void log_result( Slapi_PBlock *pb, Operation *op, int err,
						ber_tag_t tag, int nentries );
static void log_entry( Operation *op, Slapi_Entry *e );
static void log_referral( Operation *op );
static int process_read_entry_controls(Slapi_PBlock *pb, char *oid);
static struct berval * encode_read_entry(Slapi_PBlock *pb, Slapi_Entry *e,
                                         char **attrs, int alluserattrs,
                                         int some_named_attrs);
static char * op_to_string(int tag);

#define _LDAP_SEND_RESULT	0
#define _LDAP_SEND_REFERRAL	1
#define _LDAP_SEND_ENTRY	2
#define _LDAP_SEND_INTERMED	4

#define SLAPI_SEND_VATTR_FLAG_REALONLY          0x01
#define SLAPI_SEND_VATTR_FLAG_VIRTUALONLY       0x02

void g_set_num_entries_sent( Slapi_Counter *counter )
{
	num_entries_sent = counter;
}

PRUint64 g_get_num_entries_sent()
{
	return( slapi_counter_get_value(num_entries_sent) );
}

void g_set_num_bytes_sent( Slapi_Counter *counter )
{
	num_bytes_sent = counter;
}

PRUint64 g_get_num_bytes_sent()
{
	return( slapi_counter_get_value(num_bytes_sent) );
}

static void
delete_default_referral(struct berval **referrals)
{
	if (referrals)
	{
		int ii = 0;
		for (ii = 0; referrals[ii]; ++ii)
			ber_bvfree(referrals[ii]);
		slapi_ch_free((void**)&referrals);
	}
}

void 
g_set_default_referral( struct berval **ldap_url ) {
  struct berval       **default_referral;
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  int nReferrals;

  /* check to see if we want to delete all referrals */
  if ( ldap_url && ldap_url[0] &&
	   PL_strncasecmp ( (char *)ldap_url[0]->bv_val, REFERRAL_REMOVE_CMD, ldap_url[0]->bv_len ) == 0 ) {
	delete_default_referral(slapdFrontendConfig->defaultreferral);
	slapdFrontendConfig->defaultreferral = NULL;
	return;
  }

  /* count the number of referrals */
  for ( nReferrals = 0; ldap_url && ldap_url[nReferrals]; nReferrals++ )
	;
  
  default_referral = (struct berval **) 
	slapi_ch_malloc( (nReferrals + 1) * sizeof(struct berval *) );
  
  /* terminate the end, and add the referrals backwards */
  default_referral[nReferrals--] = NULL;

  while ( nReferrals >= 0 ) {
	default_referral[nReferrals] = ber_bvdup(ldap_url[nReferrals]);
	nReferrals--;
  }
  
  delete_default_referral(slapdFrontendConfig->defaultreferral);
  slapdFrontendConfig->defaultreferral = default_referral;
}

struct berval**
g_get_default_referral() {
  slapdFrontendConfig_t *slapdFrontendConfig = getFrontendConfig();
  return slapdFrontendConfig->defaultreferral;
}

/*
 * routines to manage keeping track of the current number of connections
 * to the server. this information is used by the listener thread to
 * determine when to stop listening for new connections, which it does
 * when the total number of descriptors available minus the number of
 * current connections drops below the reservedescriptors mark.
 */

void g_set_current_conn_count_mutex( PRLock *plock )
{
	PR_ASSERT( NULL != plock );

	current_conn_count_mutex = plock;
}

PRLock *g_get_current_conn_count_mutex()
{
	return( current_conn_count_mutex );
}

long g_get_current_conn_count()
{
	long	tmp;

	PR_ASSERT( NULL != current_conn_count_mutex );

	PR_Lock( current_conn_count_mutex );
	tmp = current_conn_count;
	PR_Unlock( current_conn_count_mutex );

	return( tmp );
}

void g_increment_current_conn_count()
{
	PR_ASSERT( NULL != current_conn_count_mutex );

	PR_Lock( current_conn_count_mutex );
	current_conn_count++;
	PR_Unlock( current_conn_count_mutex );
}

void g_decrement_current_conn_count()
{
	PR_ASSERT( NULL != current_conn_count_mutex );

	PR_Lock( current_conn_count_mutex );
	current_conn_count--;
/*	PR_ASSERT( current_conn_count >= 0 ); JCM BASTARD */
	PR_Unlock( current_conn_count_mutex );
}


void
send_ldap_result(
    Slapi_PBlock	*pb,
    int			err,
    char		*matched,
    char		*text,
    int			nentries,
    struct berval	**urls
)
{
       send_ldap_result_ext(pb, err, matched, text, nentries, urls, NULL);
}

int send_ldap_intermediate( Slapi_PBlock *pb,  LDAPControl **ectrls,
	char *responseName, struct berval *responseValue)
{
	ber_tag_t	tag;
	BerElement	*ber;
	Slapi_Connection *connection;
	Slapi_Operation	*operation;
	int 		rc = 0;
	int		logit = 0;
			
	LDAPDebug( LDAP_DEBUG_TRACE, "=> send_ldap_intermediate\n", 0, 0, 0 );
	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);
	slapi_pblock_get (pb, SLAPI_CONNECTION, &connection);

	if (operation->o_status == SLAPI_OP_STATUS_RESULT_SENT) {
		return(rc); /* result already sent */
	}
	tag = LDAP_RES_INTERMEDIATE;
	if ( (ber = der_alloc()) == NULL ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_alloc failed\n", 0, 0, 0 );
		goto log_and_return;
	}
	/* add the intermediate message */
	rc = ber_printf( ber, "{it{", operation->o_msgid, tag);
	/* print responsename */
	rc = ber_printf ( ber, "ts", LDAP_TAG_IM_RES_OID, responseName);
	/* print responsevalue */
	rc = ber_printf ( ber, "tO", LDAP_TAG_IM_RES_VALUE, responseValue);



	if ( rc != LBER_ERROR ) {
		rc = ber_printf( ber, "}" ); /* one more } to come */
	}

	if ( ectrls != NULL 
	    && connection->c_ldapversion >= LDAP_VERSION3
	    && write_controls( ber, ectrls ) != 0 ) {
		rc = (int)LBER_ERROR;
	}

	if ( rc != LBER_ERROR ) {	/* end the LDAPMessage sequence */
		rc = ber_put_seq( ber );
	}
	if ( rc == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_printf failed\n", 0, 0, 0 );
		ber_free( ber, 1 /* freebuf */ );
		goto log_and_return;
	}

	/* write only one pdu at a time - wait til it's our turn */
	if ( flush_ber( pb, connection, operation, ber, _LDAP_SEND_INTERMED ) == 0 ) {
		logit = 1;
	}
log_and_return:
	/* operation->o_status = SLAPI_OP_STATUS_RESULT_SENT;
	 * there could be multiple intermediate messages on
	 * the same connection, unlike in send_result do not
	 * set o_status
	 */

	if ( logit && operation_is_flag_set( operation,
	    OP_FLAG_ACTION_LOG_ACCESS )) {
		log_result( pb, operation, rc, tag, 0 );
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= send_ldap_intermediate\n", 0, 0, 0 );
	if (rc == LBER_ERROR) {
		return(1);
	} else {
		return(0);
	}
}

static int
check_and_send_extended_result(Slapi_PBlock *pb, ber_tag_t tag, BerElement *ber)
{
	/*
	 * if this is an LDAPv3 ExtendedResponse to an ExtendedRequest,
	 * check to see if the optional responseName and response OCTET
	 * STRING need to be appended.
	 */
	int rc= 0;
	char *exop_oid;
	struct berval *exop_value;
	slapi_pblock_get(pb, SLAPI_EXT_OP_RET_OID, &exop_oid);
	slapi_pblock_get(pb, SLAPI_EXT_OP_RET_VALUE, &exop_value);
	if ( LDAP_RES_EXTENDED == tag ) {
		if (exop_oid != NULL) {
			rc = ber_printf( ber, "ts",
			    LDAP_TAG_EXOP_RES_OID, exop_oid);
		}
		if (rc != LBER_ERROR && exop_value != NULL) {
			rc = ber_printf( ber, "to",
			    LDAP_TAG_EXOP_RES_VALUE,
			    exop_value->bv_val ? exop_value->bv_val : "",
			    exop_value->bv_len );
		}
	}
	return rc;
}

static int
check_and_send_SASL_response(Slapi_PBlock *pb, ber_tag_t tag, BerElement *ber, Connection *conn)
{
	/*
	 * if this is an LDAPv3 BindResponse, check to see if the
	 * optional serverSaslCreds OCTET STRING is present and needs
	 * to be appended.
	 */
	int rc= 0;
	if ( LDAP_RES_BIND == tag && conn->c_ldapversion >= LDAP_VERSION3 )
	{
		struct berval *bind_ret_saslcreds; /* v3 serverSaslCreds */
		slapi_pblock_get(pb, SLAPI_BIND_RET_SASLCREDS, &bind_ret_saslcreds);
		if ( bind_ret_saslcreds != NULL ) {
			rc = ber_printf( ber, "to",
			    LDAP_TAG_SASL_RES_CREDS,
			    bind_ret_saslcreds->bv_val ? bind_ret_saslcreds->bv_val : "",
			    bind_ret_saslcreds->bv_len );
		}
	}
	return rc;
}


/*
 * the input ber, if present, is not consumed
 */
void
send_ldap_result_ext(
    Slapi_PBlock	*pb,
    int			err,
    char		*matched,
    char		*text,
    int			nentries,
    struct berval	**urls,
    BerElement          *ber
)
{
	Connection	*conn = pb->pb_conn;
	int		i, rc, logit = 0;
	ber_tag_t	tag;
	int             flush_ber_element = 1;
	Slapi_Operation *operation;
	const char *dn = NULL;
	Slapi_DN *sdn = NULL;
	int internal_op;
	passwdPolicy *pwpolicy = NULL;
	
	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	if (operation->o_status == SLAPI_OP_STATUS_RESULT_SENT) {
		return; /* result already sent */
	}

	if ( ber != NULL ) {
	    flush_ber_element = 0;
	}

	if(err != LDAP_SUCCESS){
		/* count the error for snmp */
		/* first check for security errors */
                if(   err == LDAP_INVALID_CREDENTIALS
                   || err == LDAP_INAPPROPRIATE_AUTH
                   || err == LDAP_AUTH_METHOD_NOT_SUPPORTED
                   || err == LDAP_STRONG_AUTH_NOT_SUPPORTED
                   || err == LDAP_STRONG_AUTH_REQUIRED
                   || err == LDAP_CONFIDENTIALITY_REQUIRED
                   || err == LDAP_INSUFFICIENT_ACCESS
                   || err == LDAP_AUTH_UNKNOWN )
		{
			slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsSecurityErrors);	
		}else if(   err != LDAP_REFERRAL 
			 && err != LDAP_OPT_REFERRALS
			 && err != LDAP_PARTIAL_RESULTS)
                {
		      /*madman man spec says not to count as normal errors
			    --security errors
			    --referrals 
			    -- partially seviced operations will not be conted as an error
                      */
			slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsErrors);	
		}

	}

	LDAPDebug( LDAP_DEBUG_TRACE, "=> send_ldap_result %d:%s:%s\n", err,
	    matched ? matched : "", text ? text : "" );

	switch ( operation->o_tag ) {
	case LBER_DEFAULT:
		tag = LBER_SEQUENCE;
		break;

	case LDAP_REQ_SEARCH:
		tag = LDAP_RES_SEARCH_RESULT;
		break;

	case LDAP_REQ_DELETE:
		tag = LDAP_RES_DELETE;
		break;

	case LDAP_REFERRAL:
		if ( conn->c_ldapversion > LDAP_VERSION2 ) {
			tag = LDAP_TAG_REFERRAL;
                        break;
		}
                /* fallthru */

	default:
		tag = operation->o_tag + 1;
		break;
	}

	internal_op = operation_is_flag_set( operation, OP_FLAG_INTERNAL );
	if ( ( conn == NULL ) || ( internal_op ) ) {
		if ( operation->o_result_handler != NULL ) {
			operation->o_result_handler( conn, operation, err,
			    matched, text, nentries, urls );
			logit = 1;
		}
		goto log_and_return;
	}

	/* invalid password.  Update the password retry here */
	/* put this here for now.  It could be a send_result pre-op plugin. */
	if (err == LDAP_INVALID_CREDENTIALS) {
		slapi_pblock_get( pb, SLAPI_TARGET_SDN, &sdn );
		dn = slapi_sdn_get_dn(sdn);
		pwpolicy = new_passwdPolicy(pb, dn);
		if (pwpolicy && (pwpolicy->pw_lockout == 1)) {
			if(update_pw_retry( pb ) == LDAP_CONSTRAINT_VIOLATION && !pwpolicy->pw_is_legacy){
				/*
				 * If we are not using the legacy pw policy behavior,
				 * convert the error 49 to 19 (constraint violation)
				 * and log a message
				 */
				err = LDAP_CONSTRAINT_VIOLATION;
				text = "Invalid credentials, you now have exceeded the password retry limit.";
			}
		}
	}
        
	if ( ber == NULL ) {
	    if ( (ber = der_alloc()) == NULL ) {
	        LDAPDebug( LDAP_DEBUG_ANY, "ber_alloc failed\n", 0, 0, 0 );
			goto log_and_return;
	    }
	}

	/* there is no admin limit exceeded in v2 - change to size limit XXX */
	if ( err == LDAP_ADMINLIMIT_EXCEEDED &&
	    conn->c_ldapversion < LDAP_VERSION3 ) {
		err = LDAP_SIZELIMIT_EXCEEDED;
	}

	if ( conn->c_ldapversion < LDAP_VERSION3 || urls == NULL ) {
		char	*save, *buf = NULL;

		/*
		 * if there are v2 referrals to send, construct
		 * the v2 referral string.
		 */
		if ( urls != NULL ) {
			int	len;

		        /* count the referral */
  		        slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsReferrals);

			/*
			 * figure out how much space we need
			 */
			len = 10; /* strlen("Referral:") + NULL */
			for ( i = 0; urls[i] != NULL; i++ ) {
				len += urls[i]->bv_len + 1; /* newline + ref */
			}
			if ( text != NULL ) {
				len += strlen( text ) + 1; /* text + newline */
			}
			/*
			 * allocate buffer and fill it in with the error
			 * message plus v2-style referrals.
			 */
			buf = slapi_ch_malloc( len );
			*buf = '\0';
			if ( text != NULL ) {
				strcpy( buf, text );
				strcat( buf, "\n" );
			}
			strcat( buf, "Referral:" );
			for ( i = 0; urls[i] != NULL; i++ ) {
				strcat( buf, "\n" );
				strcat( buf, urls[i]->bv_val );
			}
			save = text;
			text = buf;
		}

		if ( (conn->c_ldapversion < LDAP_VERSION3 &&
		    err == LDAP_REFERRAL) || urls != NULL ) {
			err = LDAP_PARTIAL_RESULTS;
		}
		rc = ber_printf( ber, "{it{ess", operation->o_msgid, tag, err,
		    matched ? matched : "", text ? text : "" );

		/*
		 * if this is an LDAPv3 ExtendedResponse to an ExtendedRequest,
		 * check to see if the optional responseName and response OCTET
		 * STRING need to be appended.
		 */
		if ( rc != LBER_ERROR )
		{
			rc= check_and_send_extended_result(pb, tag, ber);
		}

		/*
		 * if this is an LDAPv3 BindResponse, check to see if the
		 * optional serverSaslCreds OCTET STRING is present and needs
		 * to be appended.
		 */
		if ( rc != LBER_ERROR )
		{
			rc= check_and_send_SASL_response(pb, tag, ber, conn);
/* XXXmcs: should we also check for a missing auth response control? */
		}

		if ( rc != LBER_ERROR ) {
			rc = ber_printf( ber, "}" ); /* one more } to come */
		}

		if ( buf != NULL ) {
			text = save;
			slapi_ch_free( (void**)&buf );
		}
	} else {
		/*
		 * there are v3 referrals to add to the result
		 */
		/* count the referral */
		if (! config_check_referral_mode())
		    slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsReferrals);
		rc = ber_printf( ber, "{it{esst{s", operation->o_msgid, tag, err,
		    matched ? matched : "", text ? text : "", LDAP_TAG_REFERRAL,
		    urls[0]->bv_val );
		for ( i = 1 ; urls[i] != NULL && rc != LBER_ERROR; i++ ) {
			rc = ber_printf( ber, "s", urls[i]->bv_val );
		}
		if ( rc != LBER_ERROR ) {
			rc = ber_printf( ber, "}" ); /* two more } to come */
		}

		/*
		 * if this is an LDAPv3 ExtendedResponse to an ExtendedRequest,
		 * check to see if the optional responseName and response OCTET
		 * STRING need to be appended.
		 */
		if ( rc != LBER_ERROR )
		{
			rc= check_and_send_extended_result(pb, tag, ber);
		}

		/*
		 * if this is an LDAPv3 BindResponse, check to see if the
		 * optional serverSaslCreds OCTET STRING is present and needs
		 * to be appended.
		 */
		if ( rc != LBER_ERROR )
		{
			rc= check_and_send_SASL_response(pb, tag, ber, conn);
		}

		if ( rc != LBER_ERROR ) {
			rc = ber_printf( ber, "}" ); /* one more } to come */
		}
	}
	if(err == LDAP_SUCCESS){
		/*
		 * Process the Read Entry Controls (if any)
		 */
		if(process_read_entry_controls(pb, LDAP_CONTROL_PRE_READ_ENTRY)){
			err = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
			goto log_and_return;
		}
		if(process_read_entry_controls(pb, LDAP_CONTROL_POST_READ_ENTRY)){
			err = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
			goto log_and_return;
		}
	}
	if ( operation->o_results.result_controls != NULL 
	    && conn->c_ldapversion >= LDAP_VERSION3
	    && write_controls( ber, operation->o_results.result_controls ) != 0 ) {
		rc = (int)LBER_ERROR;
	}

	if ( rc != LBER_ERROR ) {	/* end the LDAPMessage sequence */
		rc = ber_put_seq( ber );
	}

	if ( rc == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_printf failed\n", 0, 0, 0 );
                if (flush_ber_element == 1) {
                    /* we alloced the ber */
                    ber_free( ber, 1 /* freebuf */ );
                }
		goto log_and_return;
	}

	if ( flush_ber_element ) {
		/* write only one pdu at a time - wait til it's our turn */
		if ( flush_ber( pb, conn, operation, ber, _LDAP_SEND_RESULT ) == 0 ) {
			logit = 1;
		}
	}

log_and_return:
	operation->o_status = SLAPI_OP_STATUS_RESULT_SENT;	/* in case this has not yet been set */

	if ( logit && (operation_is_flag_set( operation, OP_FLAG_ACTION_LOG_ACCESS ) ||
	               (internal_op && config_get_plugin_logging() )))
	{
		log_result( pb, operation, err, tag, nentries );
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= send_ldap_result\n", 0, 0, 0 );
}

/*
 * For RFC 4527 - Read Entry Controls
 *
 * If this is the correct operation for the control, then retrieve the
 * requested attributes.  Then start building the ber-encoded string
 * value of the entry.  We also need to check access control for the
 * requested attributes.  Then an octet string containing a BER-encoded
 * SearchResultEntry is added to the response control.
 */
static int
process_read_entry_controls(Slapi_PBlock *pb, char *oid)
{
    struct berval *req_value = NULL;
    struct berval *res_value = NULL;
    LDAPControl **req_ctls = NULL;
    Slapi_Entry *e = NULL;
    char **attrs = NULL;
    int attr_count = 0;
    int iscritical = 0;
    int all_attrs = 0;
    int no_attrs = 0;
    int rc = 0;

    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &req_ctls);

    /*
     * Check for the PRE Read Entry Control, and return the pre-modified entry
     */
    if (slapi_control_present(req_ctls, oid, &req_value, &iscritical))
    {
        BerElement *req_ber = NULL;
        Operation *op = pb->pb_op;

        if(strcmp(oid,LDAP_CONTROL_PRE_READ_ENTRY) == 0){
            /* first verify this is the correct operation for a pre-read entry control */
            if(op->o_tag == LDAP_REQ_MODIFY || op->o_tag == LDAP_REQ_DELETE ||
               op->o_tag == LDAP_REQ_MODDN){
                slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e);
            } else {
                /* Ok, read control not used for this type of operation */
            	LDAPDebug( LDAP_DEBUG_ANY, "process_read_entry_controls: Read Entry Controls "
            	        "can not be used for a %s operation.\n", op_to_string(op->o_tag), 0, 0);
                rc = -1;
                goto done;
            }
        } else {
            /* first verify this is the correct operation for a post-read entry control */
            if(op->o_tag == LDAP_REQ_MODIFY || op->o_tag == LDAP_REQ_ADD ||
               op->o_tag == LDAP_REQ_MODDN){
                slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
            } else {
                /* Ok, read control not used for this type of operation */
            	LDAPDebug( LDAP_DEBUG_ANY, "process_read_entry_controls: Read Entry Controls "
                        "can not be used for a %s operation.\n", op_to_string(op->o_tag), 0, 0);
                rc = -1;
                goto done;
            }
        }
        if(e == NULL){
            LDAPDebug( LDAP_DEBUG_ANY, "process_read_entry_controls: unable to retrieve entry\n",0,0,0);
            rc = -1;
            goto done;
        }

#if !defined(DISABLE_ACL_CHECK)
        /* even though we can modify the entry, that doesn't mean we can read it */
        if ( plugin_call_acl_plugin (pb, e, attrs, NULL, SLAPI_ACL_READ,
            ACLPLUGIN_ACCESS_READ_ON_ENTRY, NULL ) != LDAP_SUCCESS )
        {
            LDAPDebug( LDAP_DEBUG_ACL, "process_read_entry_controls: access to entry not allowed (%s)\n",
                slapi_entry_get_dn_const(e), 0, 0 );
            rc = -1;
            goto done;
        }
#endif
        /*
         *  Check the ctl_value for any requested attributes
         */
        if( req_value && req_value->bv_len != 0 && req_value->bv_val){
            if ((req_ber = ber_init(req_value)) == NULL){
                rc = -1;
                goto free;
            }
            if (ber_scanf(req_ber, "{") == LBER_ERROR){
                rc = -1;
                goto free;
            }
            /* process the attributes */
            while(1){
                char *payload = NULL;

                if (ber_get_stringa(req_ber, &payload) != LBER_ERROR){
                    if(strcmp(payload, LDAP_ALL_USER_ATTRS) == 0){
                        all_attrs = 1;
                        slapi_ch_free_string(&payload);
                    } else if(strcmp(payload, LDAP_NO_ATTRS) == 0){
                        no_attrs = 1;
                        slapi_ch_free_string(&payload);
                    } else {
                        charray_add(&attrs, payload);
                        attr_count++;
                    }
                } else {
                    /* we're done */
                    break;
                }
            }
            if(no_attrs && (all_attrs || attr_count)){
                /* Can't have both no attrs and some attributes */
                LDAPDebug( LDAP_DEBUG_ANY, "process_read_entry_controls: Error, both no attributes \"1.1\" and "
                    "specific attributes were requeseted.\n", 0, 0, 0 );
                rc = -1;
                goto free;
            }

            if (ber_scanf(req_ber, "}") == LBER_ERROR){
                rc = -1;
                goto free;
            }
        } else {
            /* this is a problem, malformed request control value */
            LDAPDebug( LDAP_DEBUG_ANY, "process_read_entry_controls: invalid control value.\n",0,0,0);
            rc = -1;
            goto free;
        }

        /*
         * Get the ber encoded string, and add it to the response control
         */
        res_value = encode_read_entry(pb, e, attrs, all_attrs, attr_count);
        if(res_value && res_value->bv_len > 0){
            LDAPControl	new_ctrl = {0};

            new_ctrl.ldctl_oid = oid;
            new_ctrl.ldctl_value = *res_value;
            new_ctrl.ldctl_iscritical = iscritical;
            slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL, &new_ctrl );
            ber_bvfree(res_value);
        } else {
            /* failed to encode the result entry */
            LDAPDebug( LDAP_DEBUG_ANY, "Failed to process READ ENTRY Control (%s), error encoding result entry\n",
                        oid, 0, 0 );
            rc = -1;
        }

free:
        if (NULL != req_ber){
            ber_free(req_ber, 1);
        }
        if(rc != 0){
            /* log an error */
            LDAPDebug( LDAP_DEBUG_ANY, "Failed to process READ ENTRY Control (%s) ber decoding error\n",
                        oid, 0, 0 );
        }
    }
done:
    if(iscritical){
        return rc;
    } else {
        return 0;
    }
}

void
send_nobackend_ldap_result( Slapi_PBlock *pb )
{
	struct berval	**refurls;
	int err;

	refurls = g_get_default_referral();
	err = ( refurls == NULL ) ? LDAP_NO_SUCH_OBJECT : LDAP_REFERRAL;
	/* richm 20010831 - bug 556992 - the post op code needs to know what the
	   ldap error sent to the client was - slapi_send_ldap_result sets the
	   err in the pblock, so this function needs to also */
	slapi_pblock_set(pb, SLAPI_RESULT_CODE, &err);

	slapi_send_ldap_result( pb, err, NULL, NULL, 0, refurls );
}


int
send_ldapv3_referral(
    Slapi_PBlock		*pb,
    struct berval	**urls
)
{
	Connection	*conn = pb->pb_conn;
	BerElement	*ber;
	int		i, rc, logit = 0;
	Slapi_Operation *operation;

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	LDAPDebug( LDAP_DEBUG_TRACE, "=> send_ldapv3_referral\n", 0, 0, 0 );
	
	if ( conn == NULL ) {
		if ( operation->o_search_referral_handler != NULL ) {
			if (( rc = (*operation->o_search_referral_handler)(
			    pb->pb_backend, conn, operation, urls )) == 0 ) {
				logit = 1;
			}
			goto log_and_return;
		}
		return( 0 );
	}
	if ( urls == NULL ) {
		return( 0 );
	}

	if ( (ber = der_alloc()) == NULL ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_alloc failed\n", 0, 0, 0 );
		send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
		    "ber_alloc", 0, NULL );
		return( -1 );
	}

	/*
	 * send the ldapv3 SearchResultReference. it looks like this:
	 *
	 *	SearchResultReference ::= [APPLICATION 19] SEQUENCE OF LDAPURL
	 *
	 * all wrapped up in an LDAPMessage sequence which looks like this:
	 *	LDAPMessage ::= SEQUENCE {
	 *		messageID       MessageID,
	 *		SearchResultReference
	 *		controls        [0] Controls OPTIONAL
	 *      }
	 */

	for ( i = 0, rc = ber_printf( ber, "{it{", operation->o_msgid,
	    LDAP_RES_SEARCH_REFERENCE );
	    rc != LBER_ERROR && urls[i] != NULL; i++ ) {
		rc = ber_printf( ber, "s", urls[i]->bv_val );
	}
	if ( rc == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_printf failed\n", 0, 0, 0 );
		send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
		    "ber_printf", 0, NULL );
		return( -1 );
	}
	if ( ber_printf( ber, "}}" ) == LBER_ERROR ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_printf failed\n", 0, 0, 0 );
		send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
		    "ber_printf", 0, NULL );
		return( -1 );
	}

	/* write only one pdu at a time - wait til it's our turn */
	if ( (rc = flush_ber( pb, conn, operation, ber, _LDAP_SEND_REFERRAL
	    )) == 0 ) {
		logit = 1;
	}

log_and_return:
	if ( logit && operation_is_flag_set( operation,
	    OP_FLAG_ACTION_LOG_ACCESS)){
		log_referral( operation );
	}

	return( rc );
}
    
/*
 * send_ldap_referral - called to send a referral (SearchResultReference)
 * to a v3 client during a search. for v2 clients, it just adds the
 * referral(s) to the url list passed in the third parameter. this list
 * is then returned to v2 clients when it is passed to send_ldap_result().
 */
int
send_ldap_referral (
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct berval	**refs,
    struct berval	***urls
)
{
	char	*refAttr = "ref";
	char	*attrs[2] = { NULL, NULL };

	/* count the referral */
	slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsReferrals);

	attrs[0] = refAttr;
	if ( e != NULL && 
	    plugin_call_acl_plugin (pb, e, attrs, NULL, 
				    SLAPI_ACL_READ, ACLPLUGIN_ACCESS_DEFAULT, NULL)
	    != LDAP_SUCCESS ) {
		return( 0 );
	}
	if ( pb->pb_conn && pb->pb_conn->c_ldapversion > LDAP_VERSION2 ) {
		/*
		 * v3 connection - send the referral(s) in a
		 * SearchResultReference packet right now.
		 */
		return( send_ldapv3_referral( pb, refs ) );
	} else {
		/*
		 * v2 connection - add the referral(s) to the
		 * list being maintained in urls. they will be
		 * sent to the client later when send_ldap_result()
		 * is called.
		 */
		int	i, need, have;

		if ( refs == NULL && urls == NULL ) {
		    return( 0 );
		}

		for ( have = 0; *urls != NULL && (*urls)[have] != NULL;
		    have++ ) {
			;	/* NULL */
		}
		for ( need = 0; refs != NULL && refs[need] != NULL; need++ ) {
			;	/* NULL */
		}

		*urls = (struct berval **) slapi_ch_realloc( (char *) *urls,
		    (need + have + 1) * sizeof(struct berval *) );
		for ( i = have; i < have + need; i++ ) {
			(*urls)[i] = ber_bvdup( refs[i - have] );
		}
		(*urls)[i] = NULL;
	}

	return( 0 );
}

int
encode_attr_2(
    Slapi_PBlock		*pb,
    BerElement		*ber,
    Slapi_Entry		*e,
    Slapi_ValueSet		*vs,
    int			attrsonly,
    const char		*attribute_type,
    const char		*returned_type
)
{

	char *attrs[2] = { NULL, NULL };
	Slapi_Value *v;
	int i = slapi_valueset_first_value(vs,&v);

	if (i==-1)
	{
		return( 0 );
	}

	attrs[0] = (char*)attribute_type;

#if !defined(DISABLE_ACL_CHECK)
	if (plugin_call_acl_plugin(pb, e, attrs, NULL, SLAPI_ACL_READ, 
	                           ACLPLUGIN_ACCESS_READ_ON_ATTR, NULL) != LDAP_SUCCESS) {
		return( 0 );
	}
#endif

	if (ber_printf(ber, "{s[", returned_type?returned_type:attribute_type) == -1) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_printf failed\n", 0, 0, 0 );
		ber_free( ber, 1 );
		send_ldap_result(pb, LDAP_OPERATIONS_ERROR, NULL,
		                 "ber_printf type", 0, NULL);
		return( -1 );
	}

	if ( ! attrsonly )
	{
		while(i!=-1)
		{
			if ( ber_printf( ber, "o", v->bv.bv_val,v->bv.bv_len ) == -1 )
			{
				LDAPDebug( LDAP_DEBUG_ANY,
				    "ber_printf failed\n", 0, 0, 0 );
				ber_free( ber, 1 );
				send_ldap_result( pb, LDAP_OPERATIONS_ERROR,
				    NULL, "ber_printf value", 0, NULL );
				return( -1 );
			}
			i= slapi_valueset_next_value(vs,i,&v);
		}
	}

	if ( ber_printf( ber, "]}" ) == -1 ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_printf failed\n", 0, 0, 0 );
		ber_free( ber, 1 );
		send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
		    "ber_printf type end", 0, NULL );
		return( -1 );
	}

	return( 0 );
}

int
encode_attr(
    Slapi_PBlock		*pb,
    BerElement		*ber,
    Slapi_Entry		*e,
    Slapi_Attr		*a,
    int			attrsonly,
    char		*type
)
{
	return encode_attr_2(pb,ber,e,&(a->a_present_values),attrsonly,a->a_type,type);
}

#define LASTMODATTR( x )	(strcasecmp( x, "modifytimestamp" ) == 0 \
				    || strcasecmp( x, "modifiersname" ) == 0 \
				    || strcasecmp( x, "createtimestamp" ) == 0 \
				    || strcasecmp( x, "creatorsname" ) == 0)

/*
 * send_ldap_search_entry:
 * return 0 if OK
 * return 1 if this entry not sent
 * return -1 if error result sent or fatal error
 */
int
send_ldap_search_entry(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    LDAPControl		**ectrls,
    char		**attrs,
    int			attrsonly
)
{
    return send_ldap_search_entry_ext(pb, e, ectrls, attrs, attrsonly, 0, 0, NULL);
}

/*
 * LDAPv2 attr names from RFC1274 and their LDAPv3 equivalent.
 *
 * The ;binary attrs are deliberately reversed.
 */
static const char *idds_v2_attrt[][2] = {
  {"commonName","cn"},
  {"surname","sn"},
  {"userCertificate;binary","userCertificate"},
  {"caCertificate;binary","caCertificate"},
  {"countryName","c"},
  {"localityName","l"},
  {"stateOrProvinceName","st"},
  {"streetAddress","street"},
  {"organizationName","o"},
  {"organizationalUnitName","ou"},
  {"userid","uid"},
  {"rfc822Mailbox","mail"},
  {"domainComponent","dc"},
  {"mobileTelephoneNumber","mobile"},
  {"pagerTelephoneNumber","pager"},
  {"friendlyCountryName","co"},
  {NULL,NULL}
};

/*
 * Map an LDAPv3 attribute name to its LDAPv2 equivalent.
 */
static const char *idds_map_attrt_v3( 
  const char *atin
)
{
  int i;

  for (i = 0; idds_v2_attrt[i][0] != NULL; i++) {
    if (strcasecmp(atin, idds_v2_attrt[i][1]) == 0) {
      return (idds_v2_attrt[i][0]);
    }
  }

  return NULL;
}

/*
 * RFC: 2251 Page: 29
 *
 *  attributes: A list of the attributes to be returned from each entry
 *  which matches the search filter. There are two special values which
 *  may be used: an empty list with no attributes, and the attribute
 *  description string "*".  Both of these signify that all user
 *  attributes are to be returned.  (The "*" allows the client to
 *  request all user attributes in addition to specific operational
 *  attributes).
 *
 *  Attributes MUST be named at most once in the list, and are returned
 *  at most once in an entry.   If there are attribute descriptions in
 *  the list which are not recognized, they are ignored by the server.
 *
 *  If the client does not want any attributes returned, it can specify
 *  a list containing only the attribute with OID "1.1".  This OID was
 *  chosen arbitrarily and does not correspond to any attribute in use.
 */
     

/* Helper functions */

static int send_all_attrs(Slapi_Entry *e,char **attrs,Slapi_Operation *op,Slapi_PBlock *pb,BerElement *ber,int attrsonly,int ldapversion, int real_attrs_only, int some_named_attrs)
{
	int i = 0;
	int rc = 0;

	int typelist_flags = 0;
	vattr_type_thang *typelist = NULL;
	vattr_type_thang *current_type = NULL;
	char *current_type_name = NULL;
	int rewrite_rfc1274 = 0;
	int vattr_flags = 0;
	const char *dn = NULL;
	char **default_attrs = NULL;

	if(real_attrs_only == SLAPI_SEND_VATTR_FLAG_REALONLY)
		vattr_flags = SLAPI_REALATTRS_ONLY;
	else
	{
		vattr_flags = SLAPI_VIRTUALATTRS_REQUEST_POINTERS;
		if(real_attrs_only == SLAPI_SEND_VATTR_FLAG_VIRTUALONLY)
			vattr_flags |= SLAPI_VIRTUALATTRS_ONLY;
	}

	if (some_named_attrs) {
		/*
		 * If the client listed some attribute types by name, one or
		 * more of the requested types MAY be operational.  Inform the
		 * virtual attributes subsystem (certain optimizations are done
		 * by the vattrs code and vattr service providers if operational
		 * attributes are NOT requested).
		 */
		vattr_flags |= SLAPI_VIRTUALATTRS_LIST_OPERATIONAL_ATTRS;
	}

	rc = slapi_vattr_list_attrs(e,&typelist,vattr_flags,&typelist_flags);
	if (0 != rc) {
		goto exit;
	}

	if (typelist_flags & SLAPI_VIRTUALATTRS_REALATTRS_ONLY) {
		/* 
		 * There is no point in consulting the vattr service providers
                 * for every attr if they didn't contribute to the attr list.
		 */
		vattr_flags |= SLAPI_REALATTRS_ONLY;
	}

	rewrite_rfc1274 = config_get_rewrite_rfc1274();

	dn = slapi_entry_get_dn_const(e);
	if (dn == NULL || *dn == '\0' ) {
		default_attrs = slapi_entry_attr_get_charray(e, CONFIG_RETURN_DEFAULT_OPATTR);
	}
	/* Send the attrs back to the client */
	for (current_type = vattr_typethang_first(typelist); current_type; current_type = vattr_typethang_next(current_type) ) {

		Slapi_ValueSet **values = NULL;
		int attr_free_flags = 0;
		unsigned long current_type_flags = 0;
		int sendit = 0;
		char *name_to_return = NULL;
		int *type_name_disposition = 0;
		char **actual_type_name = NULL;
		const char *v2name = NULL;

		current_type_name = vattr_typethang_get_name(current_type);
		current_type_flags = vattr_typethang_get_flags(current_type);

		name_to_return = current_type_name;
		/* We only return operational attributes if the client is LDAPv2 and the attribute is one of a special set,
		   OR if the client also requested the attribute by name. If it did, we use the specified name rather than
		   the base name.
		 */
		if ( current_type_flags & SLAPI_ATTR_FLAG_OPATTR ) {
			if ( LDAP_VERSION2 == ldapversion && LASTMODATTR( current_type_name) ) {
				sendit = 1;
			} else {
				for ( i = 0; attrs != NULL && attrs[i] != NULL; i++ ) {
					if ( slapi_attr_type_cmp( attrs[i], current_type_name, SLAPI_TYPE_CMP_SUBTYPE ) == 0 ) {
						sendit = 1;
						name_to_return = op->o_searchattrs[i];
						break;
					}
				}
				if (!sendit && default_attrs) {
					for ( i = 0; default_attrs != NULL && default_attrs[i] != NULL; i++ ) {
						if ( slapi_attr_type_cmp( default_attrs[i], current_type_name, SLAPI_TYPE_CMP_SUBTYPE ) == 0 ) {
							sendit = 1;
							break;
						}
					}
				}
			}
		/*
		 * it's a user attribute. send it.
		 */
		} else {
			sendit = 1;
		}
		/* Now send to the client */
		if (sendit) {
			/**********************************************/
			int item_count = 0;
			int iter = 0;
			Slapi_DN *namespace_dn;
			Slapi_Backend *backend=0;
			vattr_context *ctx;

			/* get the namespace dn */
			slapi_pblock_get( pb, SLAPI_BACKEND, (void *)&backend);
			namespace_dn = (Slapi_DN*)slapi_be_getsuffix(backend, 0);

			/* Get the attribute value from the vattr service */
			/* ctx will be freed by attr_context_ungrok() */
			ctx = vattr_context_new ( pb );
			rc = slapi_vattr_namespace_values_get_sp(
					ctx,
					e,
					namespace_dn,
					current_type_name,
					&values,
					&type_name_disposition,
					&actual_type_name,
					vattr_flags | SLAPI_VIRTUALATTRS_SUPPRESS_SUBTYPES,
					&attr_free_flags, 
					&item_count
					);
			if ((0 == rc) && (item_count > 0)) {

				for(iter=0; iter<item_count; iter++)
				{
					int skipit;
					if ( rc != 0 ) {
						/* we hit an error - we need to free all of the stuff allocated by
						   slapi_vattr_namespace_values_get_sp */
						slapi_vattr_values_free(&(values[iter]), &(actual_type_name[iter]), attr_free_flags);
						continue;
					}

					if (SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE == type_name_disposition[iter]) {
						name_to_return = actual_type_name[iter]; 
					}

					/* If current_type_name is in attrs, we could rely on send_specific_attrs. */
					skipit = 0;
					for (i = 0; attrs && attrs[i]; i++) {
						if (slapi_attr_type_cmp(current_type_name, attrs[i], SLAPI_TYPE_CMP_SUBTYPE) == 0) {
							skipit = 1;
							break;
						}
					}

					if (!skipit) {
						rc = encode_attr_2(pb, ber, e, values[iter], attrsonly, 
						                   current_type_name, name_to_return );

						if (rewrite_rfc1274 != 0) {
							v2name = idds_map_attrt_v3(current_type_name);
							if (v2name != NULL) {
								/* also return values with RFC1274 attr name */
								rc = encode_attr_2(pb, ber, e, values[iter], 
								                   attrsonly, 
								                   current_type_name, 
								                   v2name);
							}
						}
					}

					slapi_vattr_values_free(&(values[iter]), &(actual_type_name[iter]), attr_free_flags);
				}

				slapi_ch_free((void**)&actual_type_name);
				slapi_ch_free((void**)&type_name_disposition);
				slapi_ch_free((void**)&values);
				if ( rc != 0 ) {
					goto exit;
				}

			} else {
				/* if we got here, then either values is NULL or values contains no elements
				   either way we can free it */
				slapi_ch_free((void**)&values);
				slapi_ch_free((void**)&actual_type_name);
				slapi_ch_free((void**)&type_name_disposition);
				rc = 0;
			}
		}		
	}
exit:
	if (NULL != typelist) {
		slapi_vattr_attrs_free(&typelist,typelist_flags);
	}
	if (NULL != default_attrs) {
		slapi_ch_free((void**)&default_attrs);
	}
	return rc;
}

/*
 * attrs need to expand including the subtypes found in the entry
 * e.g., if "sn" is no the attrs and 'e' has sn, sn;en, and sn;fr,
 * attrs should have sn, sn;en, and sn;fr, as well.
 */
int
send_specific_attrs(Slapi_Entry *e, char **attrs, Slapi_Operation *op,
                    Slapi_PBlock *pb, BerElement *ber, int attrsonly,
                    int ldapversion, int real_attrs_only)
{
	int i = 0;
	int rc = 0;
	int vattr_flags = 0;
	vattr_context *ctx;
	char **attrs_ext = NULL;
	char **my_searchattrs = NULL;

	if (real_attrs_only == SLAPI_SEND_VATTR_FLAG_REALONLY) {
		vattr_flags = SLAPI_REALATTRS_ONLY;
	} else {
		vattr_flags = SLAPI_VIRTUALATTRS_REQUEST_POINTERS;
		if(real_attrs_only == SLAPI_SEND_VATTR_FLAG_VIRTUALONLY)
			vattr_flags |= SLAPI_VIRTUALATTRS_ONLY;
	}

	/* Create a copy of attrs with no duplicates */
	if (attrs) {
		for (i = 0; attrs[i]; i++) {
			if (!charray_inlist(attrs_ext, attrs[i])) {
				slapi_ch_array_add(&attrs_ext, slapi_ch_strdup(attrs[i]));
				slapi_ch_array_add(&my_searchattrs, slapi_ch_strdup(op->o_searchattrs[i]));
			}
		}
	}
	if (attrs_ext) {
		attrs = attrs_ext;
	}
	
	for ( i = 0; attrs && attrs[i] != NULL; i++ ) {
		char *current_type_name = attrs[i];
		Slapi_ValueSet **values = NULL;
		int attr_free_flags = 0;
		char *name_to_return = NULL;
		char **actual_type_name= NULL;
		int *type_name_disposition = 0;
		int item_count = 0;
		int iter = 0;
		Slapi_DN *namespace_dn;
		Slapi_Backend *backend=0;

		/*
		 * Here we call the computed attribute code to see whether
		 * the requested attribute is to be computed. 
		 * The subroutine compute_attribute calls encode_attr on our behalf, in order
		 * to avoid the inefficiency of returning a complex structure
		 * which we'd have to free
		 */
		rc = compute_attribute(attrs[i], pb, ber, e, attrsonly, my_searchattrs[i]);
		if (0 == rc) {
			continue; /* Means this was a computed attr and we prcessed it OK. */
		}
		if (-1 != rc) {
			/* Means that some error happened */
			return rc;
		}
		else {
			rc = 0; /* Means that we just didn't recognize this as a computed attr */
		}

		/* get the namespace dn */
		slapi_pblock_get( pb, SLAPI_BACKEND, (void *)&backend);
		namespace_dn = (Slapi_DN*)slapi_be_getsuffix(backend, 0);

		/* Get the attribute value from the vattr service */
		/* This call handles subtype, as well.
		 * e.g., current_type_name: cn 
		 * ==>
		 * item_count: 5; actual_type_name: cn;sub0, ..., cn;sub4 */
		/* ctx will be freed by attr_context_ungrok() */
		ctx = vattr_context_new ( pb );
		rc = slapi_vattr_namespace_values_get_sp(
				ctx,
				e,
				namespace_dn,
				current_type_name,
				&values,
				&type_name_disposition,
				&actual_type_name,
				vattr_flags,
				&attr_free_flags, 
				&item_count
				);
		if ((0 == rc) && (item_count > 0)) {
			for (iter = 0; iter < item_count; iter++) {
				if ( rc != 0 ) {
					/* we hit an error - we need to free all of the stuff allocated by
					   slapi_vattr_namespace_values_get_sp */
					slapi_vattr_values_free(&(values[iter]), &(actual_type_name[iter]), attr_free_flags);
					continue;
				}
				if (SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE == type_name_disposition[iter]) {
					name_to_return = actual_type_name[iter]; 
					if ((iter > 0) && charray_inlist(attrs, name_to_return)) {
						/* subtype retrieved by slapi_vattr_namespace_values_get_sp is
						 * included in the attr list.  Skip the dup. */
						continue;
					}
				} else {
					name_to_return = my_searchattrs[i];
				}

				/* need to pass actual_type_name (e.g., sn;en to evaluate the ACL */
				rc = encode_attr_2(pb, ber, e, values[iter], attrsonly, 
				                   actual_type_name[iter], name_to_return);
				slapi_vattr_values_free(&(values[iter]), &(actual_type_name[iter]), attr_free_flags);
			}

			slapi_ch_free((void**)&actual_type_name);
			slapi_ch_free((void**)&type_name_disposition);
			slapi_ch_free((void**)&values);
			if ( rc != 0 ) {
				goto exit;
			}

		} else {
			/* if we got here, then either values is NULL or values contains no elements
			   either way we can free it */
			slapi_ch_free((void**)&values);
			slapi_ch_free((void**)&actual_type_name);
			slapi_ch_free((void**)&type_name_disposition);
			rc = 0;
		}
	}
exit:
	slapi_ch_array_free(attrs_ext);
	slapi_ch_array_free(my_searchattrs);
	return rc;

}


int
send_ldap_search_entry_ext(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    LDAPControl		**ectrls,
    char		**attrs,
    int			attrsonly,
    int                 send_result,
    int                 nentries,
    struct berval       **urls
)
{
	Connection	*conn = pb->pb_conn;
	Operation	*op = pb->pb_op;
	BerElement	*ber = NULL;
	int		i, rc = 0, logit = 0;
	int		alluserattrs, noattrs, some_named_attrs;
	Slapi_Operation *operation;
	int real_attrs_only = 0;
	LDAPControl		**ctrlp = 0;
	Slapi_Entry *gerentry = NULL;
	Slapi_Entry *ecopy = NULL;
	LDAPControl	**searchctrlp = NULL;
	

	slapi_pblock_get (pb, SLAPI_OPERATION, &operation);

	LDAPDebug( LDAP_DEBUG_TRACE, "=> send_ldap_search_entry (%s)\n",
	    e ? slapi_entry_get_dn_const(e) : "null", 0, 0 );

	/* set current entry */
	slapi_pblock_set(pb, SLAPI_SEARCH_ENTRY_ORIG, e);
	/* set controls */
	slapi_pblock_set(pb, SLAPI_SEARCH_CTRLS, ectrls);

	/* call pre entry fn */
	rc = plugin_call_plugins(pb, SLAPI_PLUGIN_PRE_ENTRY_FN);
	if (rc) {
		LDAPDebug( LDAP_DEBUG_ANY,
				   "error returned by pre entry plugins for entry %s\n",
				   e?slapi_entry_get_dn_const(e):"null", 0, 0 );
		goto cleanup;
	}

	slapi_pblock_get(pb, SLAPI_SEARCH_ENTRY_COPY, &ecopy);
	if (ecopy) {
		e = ecopy; /* send back the altered entry */
	}
	slapi_pblock_get(pb, SLAPI_SEARCH_CTRLS, &searchctrlp);

	if ( conn == NULL && e ) {
		if ( op->o_search_entry_handler != NULL ) {
			if (( rc = (*op->o_search_entry_handler)(
			    pb->pb_backend, conn, op, e )) == 0 ) {
				logit = 1;
				goto log_and_return;
			} else {
				goto cleanup;
			}
		}
		rc = 0;
		goto cleanup;
	}

#if !defined(DISABLE_ACL_CHECK)
	if ( e && plugin_call_acl_plugin (pb, e, attrs, NULL, 
				    SLAPI_ACL_READ, ACLPLUGIN_ACCESS_READ_ON_ENTRY, NULL ) != LDAP_SUCCESS ) {
		LDAPDebug( LDAP_DEBUG_ACL, "acl: access to entry not allowed\n",
		    0, 0, 0 );
		rc = 1;
		goto cleanup;
	}
#endif

	if (NULL == e) {
		rc = 1;	/* everything is ok - don't send the result */
		goto cleanup;
	}

	if ( (ber = der_alloc()) == NULL ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_alloc failed\n", 0, 0, 0 );
		send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
		    "ber_alloc", 0, NULL );
		rc = -1;
		goto cleanup;
	}

	rc = ber_printf( ber, "{it{s{", op->o_msgid,
	    LDAP_RES_SEARCH_ENTRY, slapi_entry_get_dn_const(e) );

	if ( rc == -1 ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_printf failed\n", 0, 0, 0 );
		send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
		    "ber_printf dn", 0, NULL );
		goto cleanup;
	}

	/*
	 * in ldapv3, the special attribute "*" means all user attributes,
	 * NULL means all user attributes, and "1.1" means no attributes.
	 * operational attributes are only retrieved if they are named
	 * specifically.
	 */

	/* figure out if we want all user attributes or no attributes at all */
	alluserattrs = 0;
	noattrs = 0;
	some_named_attrs = 0;
	if ( attrs == NULL ) {
		alluserattrs = 1;
	} else {
		for ( i = 0; attrs[i] != NULL; i++ ) {
			if ( strcmp( LDAP_ALL_USER_ATTRS, attrs[i] ) == 0 ) {
				alluserattrs = 1;
			} else if ( strcmp( LDAP_NO_ATTRS, attrs[i] ) == 0 ) {
				noattrs = 1;
			} else {
				some_named_attrs = 1;
			}
		}
		if ( i > 1 && noattrs ) {
			/*
			 * user has specified the special "1.1" noattrs attr
			 * and some other stuff. this is not allowed, but
			 * what should we do? we'll allow them to keep going.
			 */
			LDAPDebug( LDAP_DEBUG_TRACE,
			    "Accepting illegal other attributes specified with "
			    "special \"1.1\" attribute\n", 0, 0, 0 );
		}
	}


	/* determine whether we are to return virtual attributes */
	slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
	if(slapi_control_present(ctrlp, LDAP_CONTROL_REAL_ATTRS_ONLY, NULL, NULL))
		real_attrs_only = SLAPI_SEND_VATTR_FLAG_REALONLY;

	if(slapi_control_present(ctrlp, LDAP_CONTROL_VIRT_ATTRS_ONLY, NULL, NULL))
	{
		if(real_attrs_only != SLAPI_SEND_VATTR_FLAG_REALONLY)
			real_attrs_only = SLAPI_SEND_VATTR_FLAG_VIRTUALONLY;
		else
		{
			/* we cannot service a request for virtual only and real only */
			send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
				"Both real and virtual attributes only controls", 0, NULL );
			rc = -1;
			goto cleanup;
		}
	}

	/* look through each attribute in the entry */
	if ( alluserattrs ) {
		rc = send_all_attrs(e, attrs, op, pb, ber, attrsonly, conn->c_ldapversion,
		                    real_attrs_only, some_named_attrs);
	}
	
	/* if the client explicitly specified a list of attributes look through each attribute requested */
	if( (rc == 0) && (attrs!=NULL) && !noattrs) {
		rc = send_specific_attrs(e,attrs,op,pb,ber,attrsonly,conn->c_ldapversion,real_attrs_only);
	}

	/* Append effective rights to the stream of attribute list */
	if ( operation->o_flags & OP_FLAG_GET_EFFECTIVE_RIGHTS )
	{
		char *gerstr;
		char *entryrights;
		char *attributerights;
		char *p;

		slapi_pblock_get (pb, SLAPI_PB_RESULT_TEXT, &gerstr);

		/* Syntax check - see acleffectiverights.c */
		if (gerstr && (p = strchr(gerstr, '\n')) != NULL &&
		    strncasecmp (gerstr, "entryLevelRights: ",
				 strlen("entryLevelRights: ")) == 0 &&
		    strncasecmp (p+1, "attributeLevelRights: ",
				 strlen("attributeLevelRights: ")) == 0 )
		{
			entryrights = gerstr + strlen ("entryLevelRights: ");
			*p = '\0';
			attributerights = p + 1 + strlen ("attributeLevelRights: ");
			ber_printf( ber, "{s[o]}", "entryLevelRights", entryrights, strlen(entryrights) );
			ber_printf( ber, "{s[o]}", "attributeLevelRights", attributerights, strlen(attributerights) );
		}
	}

	if (rc != 0) {
		goto cleanup;
	}

	rc = ber_printf( ber, "}}" );

	if ( conn->c_ldapversion >= LDAP_VERSION3 ) {
		if ( searchctrlp != NULL ) {
			rc = write_controls( ber, searchctrlp );
		}
	}

	if ( rc != -1 ) {
		rc = ber_printf( ber, "}" );
	}

	if ( rc == -1 ) {
		LDAPDebug( LDAP_DEBUG_ANY, "ber_printf failed\n", 0, 0, 0 );
		send_ldap_result( pb, LDAP_OPERATIONS_ERROR, NULL,
		    "ber_printf entry end", 0, NULL );
		goto cleanup;
	}

	if (send_result) {
	    send_ldap_result_ext( pb, LDAP_SUCCESS, NULL, NULL, nentries, urls, ber);
	}

	/* write only one pdu at a time - wait til it's our turn */
	if ( (rc = flush_ber( pb, conn, op, ber, _LDAP_SEND_ENTRY )) == 0 ) {
		logit = 1;
	}
	ber = NULL; /* flush_ber will always free the ber */

log_and_return:
	if ( logit && operation_is_flag_set(operation, OP_FLAG_ACTION_LOG_ACCESS)) {

	    log_entry( op, e );

	    if (send_result) {
		ber_tag_t	tag;

		switch ( op->o_tag ) {
		case LBER_DEFAULT:
		    tag = LBER_SEQUENCE;
		    break;

		case LDAP_REQ_SEARCH:
		    tag = LDAP_RES_SEARCH_RESULT;
		    break;

		case LDAP_REQ_DELETE:
		    tag = LDAP_RES_DELETE;
		    break;

		case LDAP_REFERRAL:
		    if ( conn != NULL && conn->c_ldapversion > LDAP_VERSION2 ) {
			tag = LDAP_TAG_REFERRAL;
			break;
		    }
		    /* fallthru */

		default:
		    tag = op->o_tag + 1;
		    break;
		}	    

		log_result( pb, op, LDAP_SUCCESS, tag, nentries );
	    }
	}
cleanup:
	slapi_entry_free(gerentry);
	slapi_pblock_get(pb, SLAPI_SEARCH_ENTRY_COPY, &ecopy);
	slapi_pblock_set(pb, SLAPI_SEARCH_ENTRY_COPY, NULL);
	slapi_entry_free(ecopy);
	slapi_pblock_get(pb, SLAPI_SEARCH_CTRLS, &searchctrlp);
	slapi_pblock_set(pb, SLAPI_SEARCH_CTRLS, NULL);
	if (searchctrlp != ectrls) {
		ldap_controls_free(searchctrlp);
	}
	ber_free( ber, 1 );
	LDAPDebug( LDAP_DEBUG_TRACE, "<= send_ldap_search_entry\n", 0, 0, 0 );

	return( rc );
}




/*
 * always frees the ber
 */
static int
flush_ber(
    Slapi_PBlock	*pb,
    Connection	*conn,
    Operation	*op,
    BerElement	*ber,
    int		type
)
{
	ber_len_t	bytes;
	int		rc = 0;

	switch ( type ) {
	case _LDAP_SEND_RESULT:
		rc = plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_RESULT_FN );
		break;
	case _LDAP_SEND_REFERRAL:
		rc = plugin_call_plugins( pb, SLAPI_PLUGIN_PRE_REFERRAL_FN );
		break;
	case _LDAP_SEND_INTERMED:
		break; /* not a plugin entry point */
	}

	if ( rc != 0 ) {
		ber_free( ber, 1 );
		return( rc );
	}

	if ((conn->c_flags & CONN_FLAG_CLOSING) || slapi_op_abandoned(pb)) {
	    LDAPDebug(LDAP_DEBUG_CONNS, "ber_flush skipped because the "
		      "connection was marked to be closed or abandoned\n", 0, 0, 0);
			ber_free( ber, 1 );
			/* One of the failure can be because the client has reset the connection ( closed )
			 * and the status needs to be updated to reflect it */
			op->o_status = SLAPI_OP_STATUS_ABANDONED;
			rc = -1;
	} else {
		ber_get_option( ber, LBER_OPT_BYTES_TO_WRITE, &bytes );

		PR_Lock( conn->c_pdumutex );
		rc = ber_flush( conn->c_sb, ber, 1 );
		PR_Unlock( conn->c_pdumutex );

		if ( rc != 0 ) {
			int oserr = errno;
			/* One of the failure can be because the client has reset the connection ( closed )
			 * and the status needs to be updated to reflect it */
			op->o_status = SLAPI_OP_STATUS_ABANDONED;
			
			LDAPDebug( LDAP_DEBUG_CONNS,
				"ber_flush failed, error %d (%s)\n",
				oserr, slapd_system_strerror( oserr ), 0 );
			if (op->o_flags & OP_FLAG_PS) {
			/* We need to tell disconnect_server() not to ding
			* all the psearches if one if them disconnected
			* But we do need to terminate all persistent searches that are using
			* this connection
			*	op->o_flags |= OP_FLAG_PS_SEND_FAILED;
			*/
			}
			do_disconnect_server( conn, op->o_connid, op->o_opid );   
			ber_free( ber, 1 );
		} else {
			PRUint64 b;
			LDAPDebug( LDAP_DEBUG_BER,
				"flush_ber() wrote %u bytes to socket %d\n",
				bytes, conn->c_sd, 0 );
			LL_I2L ( b, bytes ) ;
			slapi_counter_add(num_bytes_sent, b);
			
			if ( type == _LDAP_SEND_ENTRY ) {
				slapi_counter_increment(num_entries_sent);
			}
			if (! config_check_referral_mode())
				slapi_counter_add(g_get_global_snmp_vars()->ops_tbl.dsBytesSent, bytes);
		}
	}
	
	switch ( type ) {
	case _LDAP_SEND_RESULT:
		plugin_call_plugins( pb, SLAPI_PLUGIN_POST_RESULT_FN );
		break;
	case _LDAP_SEND_REFERRAL:
		slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsReferralsReturned);
		plugin_call_plugins( pb, SLAPI_PLUGIN_POST_REFERRAL_FN );
		break;
	case _LDAP_SEND_ENTRY:
		slapi_counter_increment(g_get_global_snmp_vars()->ops_tbl.dsEntriesReturned);
		plugin_call_plugins( pb, SLAPI_PLUGIN_POST_ENTRY_FN );
		break;
	case _LDAP_SEND_INTERMED:
		break; /* not a plugin entry point */
	}

	return( rc );
}

/*
	Puts the default result handlers into the pblock.
	This routine is called before any server call to a
	database backend. 
	Returns : 0 on success, -1 on failure.
*/
int set_db_default_result_handlers(Slapi_PBlock *pb)
{
	int rc = -1;
	if (0 != pb)
	{
		rc = slapi_pblock_set( pb, SLAPI_PLUGIN_DB_ENTRY_FN, 
			(void *) send_ldap_search_entry );
		rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_RESULT_FN, 
	    		(void *) send_ldap_result );
		rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DB_REFERRAL_FN, 
	    		(void *) send_ldap_referral );
	}
	return rc;
}


struct slapi_note_map {
	unsigned int	snp_noteid;
	char		*snp_string;
};

static struct slapi_note_map notemap[] = {
	{ SLAPI_OP_NOTE_UNINDEXED,   "U" },
	{ SLAPI_OP_NOTE_SIMPLEPAGED, "P" },
        { SLAPI_OP_NOTE_FULL_UNINDEXED, "A" }
};

#define SLAPI_NOTEMAP_COUNT  ( sizeof(notemap) / sizeof(struct slapi_note_map))


/*
 * fill buf with a string representation of the bits present in notes.
 *
 * each bit is mapped to a character string (see table above).
 * the result looks like "notes=U,Z" or similar.
 * if no known notes are present, a zero-length string is generated. 
 * if buflen is too small, the output is truncated.
 *
 * Return value: buf itself.
 */
static char *
notes2str( unsigned int notes, char *buf, size_t buflen )
{
	char *p;
	int i;
	size_t len;

	*buf = '\0';
	--buflen;
	if ( buflen < 7 ) {	/* must be room for "notes=X" at least */
		return( buf );
	}
		
	p = buf;
	for ( i = 0; i < SLAPI_NOTEMAP_COUNT; ++i ) {
		if (( notemap[i].snp_noteid & notes ) != 0 ) {
			if ( p > buf && buflen > 0 ) {
				*p++ = ',';
				*p = '\0';
				--buflen;
			} else {
				strcpy( p, "notes=" );
				p += 6;
				buflen -= 6;
			}
			len = strlen( notemap[i].snp_string );
			if ( buflen < len ) {
				break;	/* bail out (result is truncated) */
			}
			memcpy( p, notemap[i].snp_string, len );
			buflen -= len;
			p += len;
			*p ='\0';
		}
	}

	return( buf );
}


#define ETIME_BUFSIZ 16         /* room for 99999999.999999 */

static void
log_result( Slapi_PBlock *pb, Operation *op, int err, ber_tag_t tag, int nentries )
{
	char	*notes_str, notes_buf[ 256 ];
	int	internal_op;
	CSN *operationcsn = NULL;
	char csn_str[CSN_STRSIZE + 5];
	char etime[ETIME_BUFSIZ];

	internal_op = operation_is_flag_set( op, OP_FLAG_INTERNAL );

	if ( (config_get_accesslog_level() & LDAP_DEBUG_TIMING) &&
			(op->o_interval != (PRIntervalTime) 0) ) {
		PRIntervalTime delta = PR_IntervalNow() - op->o_interval;
		PR_snprintf(etime, ETIME_BUFSIZ, "%f", (PRFloat64)delta/PR_TicksPerSecond());
	} else {
		PR_snprintf(etime, ETIME_BUFSIZ, "%ld", current_time() - op->o_time);
	}

	if ( 0 == pb->pb_operation_notes ) {
		notes_str = "";
	} else {
		notes_str = notes_buf;
		*notes_buf = ' ';
		notes2str( pb->pb_operation_notes, notes_buf + 1, sizeof( notes_buf ) - 1 );
	} 

	csn_str[0] = '\0';
	if (config_get_csnlogging() == LDAP_ON)
	{
		operationcsn = operation_get_csn(op);
		if (NULL != operationcsn)
		{
			char tmp_csn_str[CSN_STRSIZE];
			sprintf(csn_str, " csn=%s", csn_as_string(operationcsn, PR_FALSE, tmp_csn_str));
		}
	}

	if (op->o_tag == LDAP_REQ_BIND && err == LDAP_SASL_BIND_IN_PROGRESS) {
		/*
		 * Not actually an error.
		 * Make that clear in the log.
		 */
		if ( !internal_op )
		{
			slapi_log_access( LDAP_DEBUG_STATS,
							  "conn=%" NSPRIu64 " op=%d RESULT err=%d"
							  " tag=%" BERTAG_T " nentries=%d etime=%s%s%s"
							  ", SASL bind in progress\n",
							  (long long unsigned int)op->o_connid,
							  op->o_opid,
							  err, tag, nentries,
							  etime,
							  notes_str, csn_str );
		}
		else
		{
			slapi_log_access( LDAP_DEBUG_ARGS,
							  "conn=%s op=%d RESULT err=%d"
							  " tag=%" BERTAG_T " nentries=%d etime=%s%s%s"
							  ", SASL bind in progress\n",
								LOG_INTERNAL_OP_CON_ID,
								LOG_INTERNAL_OP_OP_ID,
							  err, tag, nentries,
							  etime,
							  notes_str, csn_str );
		}
	} else if (op->o_tag == LDAP_REQ_BIND && err == LDAP_SUCCESS) {
		char *dn = NULL;

		/*
		 * For methods other than simple, the dn in the bind request
		 * may be irrelevant. Log the actual authenticated dn.
		 */
		slapi_pblock_get(pb, SLAPI_CONN_DN, &dn);
		if ( !internal_op )
		{
			slapi_log_access( LDAP_DEBUG_STATS,
							  "conn=%" NSPRIu64 " op=%d RESULT err=%d"
							  " tag=%" BERTAG_T " nentries=%d etime=%s%s%s"
							  " dn=\"%s\"\n",
							  (long long unsigned int)op->o_connid,
							  op->o_opid,
							  err, tag, nentries,
							  etime,
							  notes_str, csn_str, dn ? dn : "");
		}
		else
		{
			slapi_log_access( LDAP_DEBUG_ARGS,
							  "conn=%s op=%d RESULT err=%d"
							  " tag=%" BERTAG_T " nentries=%d etime=%s%s%s"
							  " dn=\"%s\"\n",
								LOG_INTERNAL_OP_CON_ID,
								LOG_INTERNAL_OP_OP_ID,
							  err, tag, nentries,
							  etime,
							  notes_str, csn_str, dn ? dn : "");
		}
		slapi_ch_free((void**)&dn);
	} else {
		if ( !internal_op )
		{
			slapi_log_access( LDAP_DEBUG_STATS,
							  "conn=%" NSPRIu64 " op=%d RESULT err=%d"
							  " tag=%" BERTAG_T " nentries=%d etime=%s%s%s\n",
							  (long long unsigned int)op->o_connid,
							  op->o_opid,
							  err, tag, nentries,
							  etime,
							  notes_str, csn_str );
		}
		else
		{
			int optype;
			slapi_log_access( LDAP_DEBUG_ARGS,
							  "conn=%s op=%d RESULT err=%d"
							  " tag=%" BERTAG_T " nentries=%d etime=%s%s%s\n",
							  LOG_INTERNAL_OP_CON_ID,
							  LOG_INTERNAL_OP_OP_ID,
							  err, tag, nentries,
							  etime,
							  notes_str, csn_str );
			/*
			 *  If this is an unindexed search we should log it in the error log if
			 *  we didn't log it in the access log.
			 */
			slapi_pblock_get( pb, SLAPI_OPERATION_TYPE, &optype );
			if(optype == SLAPI_OPERATION_SEARCH && /* search, */
			   strcmp(notes_str,"") &&  /* that's unindexed, */
			   !(config_get_accesslog_level() & LDAP_DEBUG_ARGS)) /* and not logged in access log */
			{
				struct slapdplugin *plugin = NULL;
				struct slapi_componentid *cid = NULL;
				char *filter_str;
				char *plugin_dn;
				char *base_dn;

				slapi_pblock_get(pb, SLAPI_SEARCH_STRFILTER, &filter_str);
				slapi_pblock_get(pb, SLAPI_TARGET_DN, &base_dn);
				slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &cid);
				if (cid){
					plugin=(struct slapdplugin *) cid->sci_plugin;
				} else {
					slapi_pblock_get (pb, SLAPI_PLUGIN, &plugin);
				}
				plugin_dn = plugin_get_dn(plugin);

				slapi_log_error (SLAPI_LOG_FATAL, "Internal search" , "Unindexed search: source (%s) "
						"search base=\"%s\" filter=\"%s\" etime=%s nentries=%d %s\n",
						plugin_dn, base_dn, filter_str, etime, nentries, notes_str);

				slapi_ch_free_string(&plugin_dn);
			}
		}
	}
}


static void
log_entry( Operation *op, Slapi_Entry *e )
{
	int	internal_op;

	internal_op = operation_is_flag_set( op, OP_FLAG_INTERNAL );

	if ( !internal_op )
	{
		slapi_log_access( LDAP_DEBUG_STATS2, "conn=%" NSPRIu64 " op=%d ENTRY dn=\"%s\"\n",
			(long long unsigned int)op->o_connid, op->o_opid,
			slapi_entry_get_dn_const(e));
	}
	else
	{
		if ( config_get_accesslog_level() & LDAP_DEBUG_STATS2 )
		{
			slapi_log_access( LDAP_DEBUG_ARGS, "conn=%s op=%d ENTRY dn=\"%s\"\n",
				LOG_INTERNAL_OP_CON_ID, LOG_INTERNAL_OP_OP_ID,
				slapi_entry_get_dn_const(e));
		}
	}
}


static void
log_referral( Operation *op )
{
	int	internal_op;

	internal_op = operation_is_flag_set( op, OP_FLAG_INTERNAL );

	if ( !internal_op )
	{
		slapi_log_access( LDAP_DEBUG_STATS2, "conn=%" NSPRIu64 " op=%d REFERRAL\n",
			(long long unsigned int)op->o_connid, op->o_opid );
	}
	else
	{
		if ( config_get_accesslog_level() & LDAP_DEBUG_STATS2 )
		{
			slapi_log_access( LDAP_DEBUG_ARGS, "conn=%s op=%d REFERRAL\n",
				LOG_INTERNAL_OP_CON_ID, LOG_INTERNAL_OP_OP_ID );
		}
	}
}

/*
 * Generate a octet string ber-encoded searchResultEntry for
 * pre & post Read Entry Controls
 */
static struct berval *
encode_read_entry (Slapi_PBlock *pb, Slapi_Entry *e, char **attrs, int alluserattrs, int attr_count)
{
    Slapi_Operation *op = pb->pb_op;
    Connection*conn = pb->pb_conn;
    LDAPControl **ctrlp = NULL;
    struct berval *bv = NULL;
    BerElement *ber = NULL;
    int real_attrs_only = 0;
    int rc = 0;

    if ( (ber = der_alloc()) == NULL ) {
        rc = -1;
        goto cleanup;
    }

    /* Start the ber encoding with the DN */
    rc = ber_printf( ber, "t{s{", LDAP_RES_SEARCH_ENTRY, slapi_entry_get_dn_const(e) );
    if ( rc == -1 ) {
        rc = -1;
        goto cleanup;
    }

    /* determine whether we are to return virtual attributes */
    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &ctrlp);
    if(slapi_control_present(ctrlp, LDAP_CONTROL_REAL_ATTRS_ONLY, NULL, NULL)){
        real_attrs_only = SLAPI_SEND_VATTR_FLAG_REALONLY;
    }
    if(slapi_control_present(ctrlp, LDAP_CONTROL_VIRT_ATTRS_ONLY, NULL, NULL)){
        if(real_attrs_only != SLAPI_SEND_VATTR_FLAG_REALONLY){
            real_attrs_only = SLAPI_SEND_VATTR_FLAG_VIRTUALONLY;
        } else {
            /* we cannot service a request for virtual only and real only */
            LDAPDebug( LDAP_DEBUG_ANY,"encode_read_entry: Error, both real and virtual attributes "
                                      "only controls requested.\n", 0, 0, 0 );
            rc = -1;
            goto cleanup;
        }
    }

    /*
     * We maintain a flag array so that we can remove requests
     * for duplicate attributes.  We also need to set o_searchattrs
     * for the attribute processing, as modify op's don't have search attrs.
     */
    op->o_searchattrs = attrs;

    /* Send all the attributes */
    if ( alluserattrs ) {
        rc = send_all_attrs(e, attrs, op, pb, ber, 0, conn->c_ldapversion,
                            real_attrs_only, attr_count);
        if(rc){
            goto cleanup;
        }
    }

    /* Send a specified list of attributes */
    if( attrs != NULL) {
        rc = send_specific_attrs(e, attrs, op, pb, ber, 0, conn->c_ldapversion, real_attrs_only);
        if(rc){
            goto cleanup;
        }
    }

    /* wrap up the ber encoding */
    rc = ber_printf( ber, "}}" );
    if(rc != -1){
        /* generate our string encoded value */
        rc = ber_flatten(ber, &bv);
    }

cleanup:

    ber_free( ber, 1 );
    if(rc != 0){
        return NULL;
    } else {
        return bv;
    }
}

static char *
op_to_string(int tag)
{
    char *op = NULL;

    if(tag == LDAP_REQ_BIND){
        op = "BIND";
    } else if (tag == LDAP_REQ_UNBIND){
        op = "UNBIND";
    } else if (tag == LDAP_REQ_SEARCH){
        op = "SEARCH";
    } else if (tag == LDAP_REQ_ADD){
        op = "ADD";
    } else if (tag == LDAP_REQ_DELETE){
        op = "DELETE";
    } else if (tag == LDAP_REQ_RENAME){
        op = "RENAME";
    } else if (tag == LDAP_REQ_COMPARE){
        op = "COMPARE";
    } else if (tag == LDAP_REQ_ABANDON){
        op = "ABANDON";
    } else if (tag == LDAP_REQ_EXTENDED){
        op = "EXTENDED";
    }

    return op;
}

