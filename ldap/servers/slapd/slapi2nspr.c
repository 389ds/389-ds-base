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
 * slapi2nspr.c - expose a subset of the NSPR20/21 API to SLAPI plugin writers
 *
 */

#include "slap.h"
#include "snmp_collator.h"
#include <ldap_ssl.h>
#include <ldappr.h>
#include <nspr.h>

/*
 * Note that Slapi_Mutex and Slapi_CondVar are defined like this in
 * slapi-plugin.h:
 *
 *	typedef struct slapi_mutex	Slapi_Mutex;
 *	typedef struct slapi_condvar	Slapi_CondVar;
 *
 * but there is no definition for struct slapi_mutex or struct slapi_condvar.
 * This seems to work okay since we always use them in pointer form and cast
 * directly to their NSPR equivalents.  Clever, huh?
 */


/*
 * ---------------- SLAPI API Functions --------------------------------------
 */

/*
 * Function: slapi_new_mutex
 * Description: behaves just like PR_NewLock().
 * Returns: a pointer to the new mutex (NULL if a mutex can't be created).
 */
Slapi_Mutex *
slapi_new_mutex( void )
{
    return( (Slapi_Mutex *)PR_NewLock());
}


/*
 * Function: slapi_destroy_mutex
 * Description: behaves just like PR_DestroyLock().
 */
void
slapi_destroy_mutex( Slapi_Mutex *mutex )
{
    if ( mutex != NULL ) {
	PR_DestroyLock( (PRLock *)mutex );
    }
}


/*
 * Function: slapi_lock_mutex
 * Description: behaves just like PR_Lock().
 */
void
slapi_lock_mutex( Slapi_Mutex *mutex )
{
    if ( mutex != NULL ) {
	PR_Lock( (PRLock *)mutex );
    }
}


/*
 * Function: slapi_unlock_mutex
 * Description: behaves just like PR_Unlock().
 * Returns:
 *	non-zero if mutex was successfully unlocked.
 *	0 if mutex is NULL or is not locked by the calling thread.
 */
int
slapi_unlock_mutex( Slapi_Mutex *mutex )
{
    if ( mutex == NULL || PR_Unlock( (PRLock *)mutex ) == PR_FAILURE ) {
	return( 0 );
    } else {
	return( 1 );
    }
}


/*
 * Function: slapi_new_condvar
 * Description: behaves just like PR_NewCondVar().
 * Returns: pointer to a new condition variable (NULL if one can't be created).
 */
Slapi_CondVar *
slapi_new_condvar( Slapi_Mutex *mutex )
{
    if ( mutex == NULL ) {
	return( NULL );
    }

    return( (Slapi_CondVar *)PR_NewCondVar( (PRLock *)mutex ));
}


/*
 * Function: slapi_destroy_condvar
 * Description: behaves just like PR_DestroyCondVar().
 */
void
slapi_destroy_condvar( Slapi_CondVar *cvar )
{
    if ( cvar != NULL ) {
	PR_DestroyCondVar( (PRCondVar *)cvar );
    }
}


/*
 * Function: slapi_wait_condvar
 * Description: behaves just like PR_WaitCondVar() except timeout is
 *	in seconds and microseconds instead of PRIntervalTime units.
 *	If timeout is NULL, this call blocks indefinitely.
 * Returns:
 *	non-zero is all goes well.
 *	0 if cvar is NULL, the caller has not locked the mutex associated
 *		with cvar, or the waiting thread was interrupted.
 */
int
slapi_wait_condvar( Slapi_CondVar *cvar, struct timeval *timeout )
{
    PRIntervalTime	prit;

    if ( cvar == NULL ) {
	return( 0 );
    }

    if ( timeout == NULL ) {
	prit = PR_INTERVAL_NO_TIMEOUT;
    } else {
	prit = PR_SecondsToInterval( timeout->tv_sec )
		+ PR_MicrosecondsToInterval( timeout->tv_usec ); 
    }

    if ( PR_WaitCondVar( (PRCondVar *)cvar, prit ) != PR_SUCCESS ) {
	return( 0 );
    }

    return( 1 );
}


/*
 * Function: slapi_notify_condvar
 * Description: if notify_all is zero, behaves just like PR_NotifyCondVar().
 *	if notify_all is non-zero, behaves just like PR_NotifyAllCondVar().
 * Returns:
 *	non-zero if all goes well.
 *	0 if cvar is NULL or the caller has not locked the mutex associated
 *		with cvar.
 */
int
slapi_notify_condvar( Slapi_CondVar *cvar, int notify_all )
{
    PRStatus	prrc;

    if ( cvar == NULL ) {
	return( 0 );
    }

    if ( notify_all ) {
	prrc = PR_NotifyAllCondVar( (PRCondVar *)cvar );
    } else {
	prrc = PR_NotifyCondVar( (PRCondVar *)cvar );
    }

    return( prrc == PR_SUCCESS ? 1 : 0 );
}


/*
 * Function: slapi_ldap_init()
 * Description: just like ldap_ssl_init() but also arranges for the LDAP
 *	session handle returned to be safely shareable by multiple threads
 *	if "shared" is non-zero.
 * Returns:
 *	an LDAP session handle (NULL if some local error occurs).
 */
LDAP *
slapi_ldap_init( char *ldaphost, int ldapport, int secure, int shared )
{
    LDAP			*ld;
    int				io_timeout_ms;


    if ( secure && slapd_SSL_client_init() != 0 ) {
	return( NULL );
    }

    /*
     * Leverage the libprldap layer to take care of all the NSPR integration.
     * Note that ldapssl_init() uses libprldap implicitly.
     */

    if ( secure ) {
	ld = ldapssl_init( ldaphost, ldapport, secure );
    } else {
	ld = prldap_init( ldaphost, ldapport, shared );
    }

	/* Update snmp interaction table */
	if ( ld == NULL) {
		set_snmp_interaction_row( ldaphost, ldapport, -1);
	} else {
		set_snmp_interaction_row( ldaphost, ldapport, 0);
	}

    if ( ld != NULL ) {
	/*
	 * Set the outbound LDAP I/O timeout based on the server config.
	 */
	io_timeout_ms = config_get_outbound_ldap_io_timeout();
	if ( io_timeout_ms > 0 ) {
	    if ( prldap_set_session_option( ld, NULL, PRLDAP_OPT_IO_MAX_TIMEOUT,
			io_timeout_ms ) != LDAP_SUCCESS ) {
		slapi_log_error( SLAPI_LOG_FATAL, "slapi_ldap_init",
			"failed: unable to set outbound I/O timeout to %dms\n",
			io_timeout_ms );
		slapi_ldap_unbind( ld );
		return( NULL );
	    }
	}

	/*
	 * Set SSL strength (server certificate validity checking).
	 */
	if ( secure ) {
	    int ssl_strength;

	   if ( config_get_ssl_check_hostname()) {
		/* check hostname against name in certificate */
		ssl_strength = LDAPSSL_AUTH_CNCHECK;
	    } else {
		/* verify certificate only */
		ssl_strength = LDAPSSL_AUTH_CERT;
	    }

	    if ( ldapssl_set_strength( ld, ssl_strength ) != 0 ) {
		int	prerr = PR_GetError();

		slapi_log_error( SLAPI_LOG_FATAL, "slapi_ldap_init",
			"failed: unable to set SSL strength to %d ("
			SLAPI_COMPONENT_NAME_NSPR " error %d - %s)",
			ssl_strength, prerr, slapd_pr_strerror( prerr ));

	    }
	}
    }

    return( ld );
}


/*
 * Function: slapi_ldap_unbind()
 * Purpose: release an LDAP session obtained from a call to slapi_ldap_init().
 */
void
slapi_ldap_unbind( LDAP *ld )
{
    if ( ld != NULL ) {
	ldap_unbind( ld );
    }
}
