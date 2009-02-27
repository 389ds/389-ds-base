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

/* control.c - routines for dealing with LDAPMessage controls */

#include <stdio.h>
#include "slap.h"


/*
 * static variables used to track information about supported controls.
 * supported_controls is a NULL-terminated array of OIDs.
 * supported_controls_ops is an array of bitmaps that hold SLAPI_OPERATION_*
 *    flags that specify the operation(s) for which a control is supported.
 *    The elements in the supported_controls_ops array align with the ones
 *    in the supported_controls array.
 */
static char **supported_controls = NULL;
static unsigned long *supported_controls_ops = NULL;
static int supported_controls_count = 0;
static PRRWLock *supported_controls_lock = NULL;

/*
 * Register all of the LDAPv3 controls we know about "out of the box."
 */
void
init_controls( void )
{
	supported_controls_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE,
		"supported controls rwlock");
	if (NULL == supported_controls_lock) {
		/* Out of resources */
		slapi_log_error(SLAPI_LOG_FATAL, "startup", 
			"init_controls: failed to create lock.\n");
		exit (1);
	}

	slapi_register_supported_control( LDAP_CONTROL_MANAGEDSAIT,
	    SLAPI_OPERATION_SEARCH | SLAPI_OPERATION_COMPARE
	    | SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE
	    | SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN );
	slapi_register_supported_control( LDAP_CONTROL_PERSISTENTSEARCH,
	    SLAPI_OPERATION_SEARCH );
	slapi_register_supported_control( LDAP_CONTROL_PWEXPIRED,
	    SLAPI_OPERATION_NONE );
	slapi_register_supported_control( LDAP_CONTROL_PWEXPIRING,
	    SLAPI_OPERATION_NONE );
	slapi_register_supported_control( LDAP_CONTROL_SORTREQUEST,
	    SLAPI_OPERATION_SEARCH );
	slapi_register_supported_control( LDAP_CONTROL_VLVREQUEST,
	    SLAPI_OPERATION_SEARCH );
	slapi_register_supported_control( LDAP_CONTROL_AUTH_REQUEST,
	    SLAPI_OPERATION_BIND );
	slapi_register_supported_control( LDAP_CONTROL_AUTH_RESPONSE,
	    SLAPI_OPERATION_NONE );
	slapi_register_supported_control( LDAP_CONTROL_REAL_ATTRS_ONLY,
	    SLAPI_OPERATION_SEARCH );
	slapi_register_supported_control( LDAP_CONTROL_VIRT_ATTRS_ONLY,
	    SLAPI_OPERATION_SEARCH );
	slapi_register_supported_control( LDAP_X_CONTROL_PWPOLICY_REQUEST,
	    SLAPI_OPERATION_SEARCH | SLAPI_OPERATION_COMPARE
	    | SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE
	    | SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN );
/*
	We do not register the password policy response because it has
	the same oid as the request (and it was being reported twice in
	in the root DSE supportedControls attribute)

	slapi_register_supported_control( LDAP_X_CONTROL_PWPOLICY_RESPONSE,
	    SLAPI_OPERATION_SEARCH | SLAPI_OPERATION_COMPARE
	    | SLAPI_OPERATION_ADD | SLAPI_OPERATION_DELETE
	    | SLAPI_OPERATION_MODIFY | SLAPI_OPERATION_MODDN );
*/
	slapi_register_supported_control( LDAP_CONTROL_GET_EFFECTIVE_RIGHTS,
		SLAPI_OPERATION_SEARCH );
}


/*
 * register a supported control so it can be returned as part of the root DSE.
 */
void
slapi_register_supported_control( char *controloid, unsigned long controlops )
{
	if ( controloid != NULL ) {
		PR_RWLock_Wlock(supported_controls_lock);
		++supported_controls_count;
		charray_add( &supported_controls,
		    slapi_ch_strdup( controloid ));
		supported_controls_ops = (unsigned long *)slapi_ch_realloc(
		    (char *)supported_controls_ops,
		    supported_controls_count * sizeof( unsigned long ));
		supported_controls_ops[ supported_controls_count - 1 ] = 
		    controlops;
		PR_RWLock_Unlock(supported_controls_lock);
	}
}


/*
 * retrieve supported controls OID and/or operations arrays.
 * return 0 if successful and -1 if not.
 * This function is not MTSafe and should be deprecated.
 * slapi_get_supported_controls_copy should be used instead.
 */
int
slapi_get_supported_controls( char ***ctrloidsp, unsigned long **ctrlopsp )
{
	if ( ctrloidsp != NULL ) {
		*ctrloidsp = supported_controls;
	}
	if ( ctrlopsp != NULL ) {
		*ctrlopsp = supported_controls_ops;
	}

	return( 0 );
}


static 
unsigned long *supported_controls_ops_dup(unsigned long *ctrlops) 
{
	int i;
	unsigned long *dup_ops = (unsigned long *)slapi_ch_calloc(
		supported_controls_count + 1, sizeof( unsigned long ));
	if (NULL != dup_ops) {
		for (i=0; i < supported_controls_count; i++)
			dup_ops[i] = supported_controls_ops[i];
	}
	return dup_ops;
}


int slapi_get_supported_controls_copy( char ***ctrloidsp, unsigned long **ctrlopsp )
{
	PR_RWLock_Rlock(supported_controls_lock);
	if ( ctrloidsp != NULL ) {
		*ctrloidsp = charray_dup(supported_controls);
	}
	if ( ctrlopsp != NULL ) {
		*ctrlopsp = supported_controls_ops_dup(supported_controls_ops);
	}
	PR_RWLock_Unlock(supported_controls_lock);
	return (0);
}

/*
 * RFC 4511 section 4.1.11.  Controls says that the UnbindRequest
 * MUST ignore the criticality field of controls
 */
int
get_ldapmessage_controls_ext(
    Slapi_PBlock	*pb,
    BerElement		*ber,
    LDAPControl		***controlsp,	/* can be NULL if no need to return */
    int                 ignore_criticality /* some requests must ignore criticality */
)
{
	LDAPControl		**ctrls, *new;
	ber_tag_t		tag;
	ber_len_t		len;
	int			rc, maxcontrols, curcontrols;
	char			*last;
	int			managedsait, pwpolicy_ctrl;

	/*
	 * Each LDAPMessage can have a set of controls appended
	 * to it. Controls are used to extend the functionality
	 * of an LDAP operation (e.g., add an attribute size limit
	 * to the search operation). These controls look like this:
	 *
	 *	Controls ::= SEQUENCE OF Control
	 *
	 *	Control ::= SEQUENCE {
	 *		controlType	LDAPOID,
	 *		criticality	BOOLEAN DEFAULT FALSE,
	 *		controlValue	OCTET STRING
	 *	}
	 */

	LDAPDebug( LDAP_DEBUG_TRACE, "=> get_ldapmessage_controls\n", 0, 0, 0 );

	ctrls = NULL;
	slapi_pblock_set( pb, SLAPI_REQCONTROLS, ctrls );
	if ( controlsp != NULL ) {
		*controlsp = NULL;
	}
	rc = LDAP_PROTOCOL_ERROR;	/* most popular error we may return */

	/*
         * check to see if controls were included
	 */
	if ( ber_get_option( ber, LBER_OPT_REMAINING_BYTES, &len ) != 0 ) {
		LDAPDebug( LDAP_DEBUG_TRACE,
		    "<= get_ldapmessage_controls LDAP_OPERATIONS_ERROR\n",
		    0, 0, 0 );
		return( LDAP_OPERATIONS_ERROR );	/* unexpected error */
	}
	if ( len == 0 ) {
		LDAPDebug( LDAP_DEBUG_TRACE,
		    "<= get_ldapmessage_controls no controls\n", 0, 0, 0 );
		return( LDAP_SUCCESS );			/* no controls */
	}
	if (( tag = ber_peek_tag( ber, &len )) != LDAP_TAG_CONTROLS ) {
		if ( tag == LBER_ERROR ) {
			LDAPDebug( LDAP_DEBUG_TRACE,
			    "<= get_ldapmessage_controls LDAP_PROTOCOL_ERROR\n",
			    0, 0, 0 );
			return( LDAP_PROTOCOL_ERROR );	/* decoding error */
		}
		/*
		 * We found something other than controls.  This should never
		 * happen in LDAPv3, but we don't treat this is a hard error --
		 * we just ignore the extra stuff.
		 */
		LDAPDebug( LDAP_DEBUG_TRACE,
		    "<= get_ldapmessage_controls ignoring unrecognized data in request (tag 0x%x)\n",
		    tag, 0, 0 );
		return( LDAP_SUCCESS );
	}

	/*
	 * A sequence of controls is present.  If connection is not LDAPv3
	 * or better, return a protocol error.  Otherwise, parse the controls.
	 */
	if ( pb != NULL && pb->pb_conn != NULL
			&& pb->pb_conn->c_ldapversion < LDAP_VERSION3 ) {
		slapi_log_error( SLAPI_LOG_FATAL, "connection",
				"received control(s) on an LDAPv%d connection\n",
				pb->pb_conn->c_ldapversion );
		return( LDAP_PROTOCOL_ERROR );
	}

	maxcontrols = curcontrols = 0;
	for ( tag = ber_first_element( ber, &len, &last );
	    tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
	    tag = ber_next_element( ber, &len, last ) ) {
		if ( curcontrols >= maxcontrols - 1 ) {
#define CONTROL_GRABSIZE	6
			maxcontrols += CONTROL_GRABSIZE;
			ctrls = (LDAPControl **) slapi_ch_realloc( (char *)ctrls,
			    maxcontrols * sizeof(LDAPControl *) );
		}
		new = (LDAPControl *) slapi_ch_calloc( 1, sizeof(LDAPControl) );
		ctrls[curcontrols++] = new;
		ctrls[curcontrols] = NULL;

		if ( ber_scanf( ber, "{a", &new->ldctl_oid ) == LBER_ERROR ) {
			goto free_and_return;
		}

		/* the criticality is optional */
		if ( ber_peek_tag( ber, &len ) == LBER_BOOLEAN ) {
			if ( ber_scanf( ber, "b", &new->ldctl_iscritical )
			    == LBER_ERROR ) {
				goto free_and_return;
			}
		} else {
			/* absent is synonomous with FALSE */
			new->ldctl_iscritical = 0;
		}
		/* if we are ignoring criticality, treat as FALSE */
		if (ignore_criticality) {
		    new->ldctl_iscritical = 0;
		}

		/*
		 * return an appropriate error if this control is marked
		 * critical and either:
		 *   a) we do not support it at all OR
		 *   b) it is not supported for this operation
		 */
		if ( new->ldctl_iscritical ) {
		    int		i;

		    PR_RWLock_Rlock(supported_controls_lock);
		    for ( i = 0; supported_controls != NULL
			&& supported_controls[i] != NULL; ++i ) {
			    if ( strcmp( supported_controls[i],
				new->ldctl_oid ) == 0 ) {
				    break;
			    }
		    }

		    if ( supported_controls == NULL ||
			supported_controls[i] == NULL ||
			( 0 == ( supported_controls_ops[i] &
			operation_get_type(pb->pb_op) ))) {
			    rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
			    PR_RWLock_Unlock(supported_controls_lock);
			    goto free_and_return;
		    }
		    PR_RWLock_Unlock(supported_controls_lock);
		}

		/* the control value is optional */
		if ( ber_peek_tag( ber, &len ) == LBER_OCTETSTRING ) {
			if ( ber_scanf( ber, "o", &new->ldctl_value )
			    == LBER_ERROR ) {
				goto free_and_return;
			}
		} else {
			(new->ldctl_value).bv_val = NULL;
			(new->ldctl_value).bv_len = 0;
		}	
	}

	if ( tag == LBER_ERROR ) {
		goto free_and_return;
	}

	slapi_pblock_set( pb, SLAPI_REQCONTROLS, ctrls );
	managedsait = slapi_control_present( ctrls,
	    LDAP_CONTROL_MANAGEDSAIT, NULL, NULL );
	slapi_pblock_set( pb, SLAPI_MANAGEDSAIT, &managedsait );
	pwpolicy_ctrl = slapi_control_present( ctrls,
	    LDAP_X_CONTROL_PWPOLICY_REQUEST, NULL, NULL );
	slapi_pblock_set( pb, SLAPI_PWPOLICY, &pwpolicy_ctrl );

	if ( controlsp != NULL ) {
		*controlsp = ctrls;
	}

#ifdef SLAPD_ECHO_CONTROL
	/*
	 * XXXmcs: Start of hack: if a control with OID "1.1" was sent by
	 * the client, echo all controls back to the client unchanged.  Note
	 * that this is just a hack to test control handling in libldap and
	 * should be removed once we support all interesting controls.
	 */
	if ( slapi_control_present( ctrls, "1.1", NULL, NULL )) {
		int	i;

		for ( i = 0; ctrls[i] != NULL; ++i ) {
			slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL,
			    (void *)ctrls[i] );
		}
	}
#endif /* SLAPD_ECHO_CONTROL */

	LDAPDebug( LDAP_DEBUG_TRACE,
	    "<= get_ldapmessage_controls %d controls\n", curcontrols, 0, 0 );
	return( LDAP_SUCCESS );

free_and_return:;
	ldap_controls_free( ctrls );
	LDAPDebug( LDAP_DEBUG_TRACE,
	    "<= get_ldapmessage_controls %i\n", rc, 0, 0 );
	return( rc );
}

int
get_ldapmessage_controls(
    Slapi_PBlock	*pb,
    BerElement		*ber,
    LDAPControl		***controlsp	/* can be NULL if no need to return */
)
{
    return get_ldapmessage_controls_ext(pb, ber, controlsp, 0 /* do not ignore criticality */);
}

int
slapi_control_present( LDAPControl **controls, char *oid, struct berval **val, int *iscritical )
{
	int	i;

	LDAPDebug( LDAP_DEBUG_TRACE,
	    "=> slapi_control_present (looking for %s)\n", oid, 0, 0 );

	if ( val != NULL ) {
		*val = NULL;
	}

	if ( controls == NULL ) {
		LDAPDebug( LDAP_DEBUG_TRACE,
		    "<= slapi_control_present 0 (NO CONTROLS)\n", 0, 0, 0 );
		return( 0 );
	}

	for ( i = 0; controls[i] != NULL; i++ ) {
		if ( strcmp( controls[i]->ldctl_oid, oid ) == 0 ) {
			if ( val != NULL ) {
				if (NULL != val) {
					*val = &controls[i]->ldctl_value;
				}
				if (NULL != iscritical) {
					*iscritical = (int) controls[i]->ldctl_iscritical;
				}
			}
			LDAPDebug( LDAP_DEBUG_TRACE,
			    "<= slapi_control_present 1 (FOUND)\n", 0, 0, 0 );
			return( 1 );
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE,
	    "<= slapi_control_present 0 (NOT FOUND)\n", 0, 0, 0 );
	return( 0 );
}


/*
 * Write sequence of controls in "ctrls" to "ber".
 * Return zero on success and -1 if an error occurs.
 */
int
write_controls( BerElement *ber, LDAPControl **ctrls )
{
	int	i;
	unsigned long rc;

	rc= ber_start_seq( ber, LDAP_TAG_CONTROLS );
	if ( rc == LBER_ERROR ) {
		return( -1 );
	}

	/* if the criticality is false, it should be absent from the encoding */
	for ( i = 0; ctrls[ i ] != NULL; ++i ) {
		if ( ctrls[ i ]->ldctl_value.bv_val == 0 ) {
			if ( ctrls[ i ]->ldctl_iscritical ) {
				rc = ber_printf( ber, "{sb}", ctrls[ i ]->ldctl_oid,
						ctrls[ i ]->ldctl_iscritical );
			} else {
				rc = ber_printf( ber, "{s}", ctrls[ i ]->ldctl_oid );
			}						
		} else {
			if ( ctrls[ i ]->ldctl_iscritical ) {
				rc = ber_printf( ber, "{sbo}", ctrls[ i ]->ldctl_oid,
						ctrls[ i ]->ldctl_iscritical,
						ctrls[ i ]->ldctl_value.bv_val,
						ctrls[ i ]->ldctl_value.bv_len );
			} else {
				rc = ber_printf( ber, "{so}", ctrls[ i ]->ldctl_oid,
						ctrls[ i ]->ldctl_value.bv_val,
						ctrls[ i ]->ldctl_value.bv_len );
			}						
		}
		if ( rc == LBER_ERROR ) {
			return( -1 );
		}
	}

    rc= ber_put_seq( ber );
	if ( rc == LBER_ERROR ) {
		return( -1 );
	}

	return( 0 );
}


/*
 * duplicate "newctrl" and add it to the array of controls "*ctrlsp"
 * note that *ctrlsp may be reset and that it is okay to pass NULL for it.
 * IF copy is true, a copy of the passed in control will be added - copy
 * made with slapi_dup_control - if copy is false, the control
 * will be used directly and may be free'd by ldap_controls_free - so
 * make sure it is ok for the control array to own the pointer you
 * pass in
 */
void
add_control_ext( LDAPControl ***ctrlsp, LDAPControl *newctrl, int copy )
{
	int	count;

        if ( *ctrlsp == NULL ) {
		count = 0;
	} else {
		for ( count = 0; (*ctrlsp)[count] != NULL; ++count ) {
			;
		}
	}

        *ctrlsp = (LDAPControl **)slapi_ch_realloc( (char *)*ctrlsp,
                ( count + 2 ) * sizeof(LDAPControl *));

        if (copy) {
            (*ctrlsp)[ count ] = slapi_dup_control( newctrl );
        } else {
            (*ctrlsp)[ count ] = newctrl;
        }
	(*ctrlsp)[ ++count ] = NULL;
}

/*
 * duplicate "newctrl" and add it to the array of controls "*ctrlsp"
 * note that *ctrlsp may be reset and that it is okay to pass NULL for it.
 */
void
add_control( LDAPControl ***ctrlsp, LDAPControl *newctrl )
{
	add_control_ext(ctrlsp, newctrl, 1 /* copy */);
}

void
slapi_add_control_ext( LDAPControl ***ctrlsp, LDAPControl *newctrl, int copy )
{
	add_control_ext(ctrlsp, newctrl, copy);
}

/*
 * return a malloc'd copy of "ctrl"
 */
LDAPControl *
slapi_dup_control( LDAPControl *ctrl )
{
	LDAPControl	*rctrl;

	rctrl = (LDAPControl *)slapi_ch_malloc( sizeof( LDAPControl ));

	rctrl->ldctl_oid = slapi_ch_strdup( ctrl->ldctl_oid );
	rctrl->ldctl_iscritical = ctrl->ldctl_iscritical;

	if ( ctrl->ldctl_value.bv_val == NULL ) {		/* no value */
		rctrl->ldctl_value.bv_len = 0;
		rctrl->ldctl_value.bv_val = NULL;
	} else if ( ctrl->ldctl_value.bv_len <= 0 ) {	/* zero length value */
		rctrl->ldctl_value.bv_len = 0;
		rctrl->ldctl_value.bv_val = slapi_ch_malloc( 1 );
		rctrl->ldctl_value.bv_val[0] = '\0';
	} else {										/* value with content */
		rctrl->ldctl_value.bv_len = ctrl->ldctl_value.bv_len;
		rctrl->ldctl_value.bv_val =
		    slapi_ch_malloc( ctrl->ldctl_value.bv_len );
		memcpy( rctrl->ldctl_value.bv_val, ctrl->ldctl_value.bv_val,
		    ctrl->ldctl_value.bv_len );
	}

	return( rctrl );
}

void
slapi_add_controls( LDAPControl ***ctrlsp, LDAPControl **newctrls, int copy )
{
    int ii;
    for (ii = 0; newctrls && newctrls[ii]; ++ii) {
        slapi_add_control_ext(ctrlsp, newctrls[ii], copy);
    }
}

int
slapi_build_control( char *oid, BerElement *ber,
	char iscritical, LDAPControl **ctrlp )
{
	int rc = 0;
	int return_value = LDAP_SUCCESS;
	struct berval *bvp = NULL;

	PR_ASSERT( NULL != oid && NULL != ctrlp );

	if ( NULL == ber ) {
		bvp = NULL;
	} else {
		/* allocate struct berval with contents of the BER encoding */
		rc = ber_flatten( ber, &bvp );
		if ( -1 == rc ) {
			return_value = LDAP_NO_MEMORY;
			goto loser;
		}
	}

	/* allocate the new control structure */
	*ctrlp = (LDAPControl *)slapi_ch_calloc( 1, sizeof(LDAPControl));

	/* fill in the fields of this new control */
	(*ctrlp)->ldctl_iscritical = iscritical;
	(*ctrlp)->ldctl_oid = slapi_ch_strdup( oid );
	if ( NULL == bvp ) {
		(*ctrlp)->ldctl_value.bv_len = 0;
		(*ctrlp)->ldctl_value.bv_val = NULL;
	} else {
		(*ctrlp)->ldctl_value = *bvp;   /* struct copy */
		ldap_memfree(bvp); /* free container, but not contents */
		bvp = NULL;
	}

loser:
	return return_value;
}


#if 0
/*
 * rbyrne: This is version of the above using the slapi_build_control_from_berval()
 * I'll enable this afterwards.
 * Build an allocated LDAPv3 control from a BerElement.
 * Returns an LDAP error code.
 */
int
slapi_build_control( char *oid, BerElement *ber,
	char iscritical, LDAPControl **ctrlp )
{
	int rc = 0;
	int return_value = LDAP_SUCCESS;
	struct berval *bvp = NULL;

	PR_ASSERT( NULL != oid && NULL != ctrlp );

	if ( NULL == ber ) {
		bvp = NULL;
	} else {
		/* allocate struct berval with contents of the BER encoding */
		rc = ber_flatten( ber, &bvp );
		if ( -1 == rc ) {
			return_value = LDAP_NO_MEMORY;
			goto loser;
		}
	}

	return_value = slapi_build_control_from_berval( oid, bvp, iscritical,
											ctrlp);
	if ( bvp != NULL ) {
		ldap_memfree(bvp); /* free container, but not contents */
		bvp = NULL;
	}
			
loser:
	return return_value;
}
#endif

/*
 * Build an allocated LDAPv3 control from a berval. Returns an LDAP error code.
 */
int
slapi_build_control_from_berval( char *oid, struct berval *bvp,
	char iscritical, LDAPControl **ctrlp )
{	
	int return_value = LDAP_SUCCESS;	

	/* allocate the new control structure */
	*ctrlp = (LDAPControl *)slapi_ch_calloc( 1, sizeof(LDAPControl));

	/* fill in the fields of this new control */
	(*ctrlp)->ldctl_iscritical = iscritical;
	(*ctrlp)->ldctl_oid = slapi_ch_strdup( oid );
	if ( NULL == bvp ) {
		(*ctrlp)->ldctl_value.bv_len = 0;
		(*ctrlp)->ldctl_value.bv_val = NULL;
	} else {
		(*ctrlp)->ldctl_value = *bvp;   /* struct copy */
	}

	return return_value;
}

