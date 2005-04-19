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
/* findentry.c - find a database entry, obeying referrals (& aliases?) */

#include "back-ldbm.h"

int
check_entry_for_referral(Slapi_PBlock *pb, Slapi_Entry *entry, char *matched, const char *callingfn) /* JCM - Move somewhere more appropriate */
{
	int rc=0, i=0, numValues=0;
	Slapi_Attr *attr;

	/* if the entry is a referral send the referral */
	if ( slapi_entry_attr_find( entry, "ref", &attr ) == 0 )
	{
		Slapi_Value *val=NULL;	
		struct berval **refscopy=NULL;
		struct berval **url=NULL;
		slapi_attr_get_numvalues(attr, &numValues );
		if(numValues > 0) {
			url=(struct berval **) slapi_ch_malloc((numValues + 1) * sizeof(struct berval*));
		}
		for (i = slapi_attr_first_value(attr, &val); i != -1;
		     i = slapi_attr_next_value(attr, i, &val)) {
			url[i]=(struct berval*)slapi_value_get_berval(val);
		}
		url[numValues]=NULL;		
		refscopy = ref_adjust( pb, url, slapi_entry_get_sdn(entry), 0 ); /* JCM - What's this PBlock* for? */
		slapi_send_ldap_result( pb, LDAP_REFERRAL, matched, NULL, 0, refscopy );
		LDAPDebug( LDAP_DEBUG_TRACE,
			"<= %s sent referral to (%s) for (%s)\n",
			callingfn,
			refscopy ? refscopy[0]->bv_val : "",
			slapi_entry_get_dn(entry));
		if ( refscopy != NULL )
		{
			ber_bvecfree( refscopy );
		}
		if( url != NULL) {
			slapi_ch_free( (void **)&url );	
		}
		rc= 1;
	}
	return rc;
}

static struct backentry *
find_entry_internal_dn(
	Slapi_PBlock	*pb,
    backend			*be,
    const Slapi_DN *sdn,
    int				lock,
	back_txn		*txn,
	int				really_internal
)
{ 
	struct backentry *e;
	int	managedsait = 0;
	int	err;
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
	size_t tries = 0;

	/* get the managedsait ldap message control */
	slapi_pblock_get( pb, SLAPI_MANAGEDSAIT, &managedsait );

	while ( (tries < LDBM_CACHE_RETRY_COUNT) && 
	        (e = dn2entry( be, sdn, txn, &err )) != NULL )
	{
		/*
		 * we found the entry. if the managedsait control is set,
		 * we return the entry. if managedsait is not set, we check
		 * for the presence of a ref attribute, returning to the
		 * client a referral to the ref'ed entry if a ref is present,
		 * returning the entry to the caller if not.
		 */
		if ( !managedsait && !really_internal) {
			/* see if the entry is a referral */
			if(check_entry_for_referral(pb, e->ep_entry, NULL, "find_entry_internal_dn"))
			{
				cache_return( &inst->inst_cache, &e );
				return( NULL );
			}
		}

		/*
		 * we'd like to return the entry. lock it if requested,
		 * retrying if necessary.
		 */

		/* wait for entry modify lock */
		if ( !lock || cache_lock_entry( &inst->inst_cache, e ) == 0 ) {
			LDAPDebug( LDAP_DEBUG_TRACE,
			    "<= find_entry_internal_dn found (%s)\n", slapi_sdn_get_dn(sdn), 0, 0 );
			return( e );
		}
		/*
		 * this entry has been deleted - see if it was actually
		 * replaced with a new copy, and try the whole thing again.
		 */
		LDAPDebug( LDAP_DEBUG_ARGS,
		    "   find_entry_internal_dn retrying (%s)\n", slapi_sdn_get_dn(sdn), 0, 0 );
		cache_return( &inst->inst_cache, &e );
		tries++;
	}
	if (tries >= LDBM_CACHE_RETRY_COUNT) {
		LDAPDebug( LDAP_DEBUG_ANY,"find_entry_internal_dn retry count exceeded (%s)\n", slapi_sdn_get_dn(sdn), 0, 0 );
	}
	/*
	 * there is no such entry in this server. see how far we
	 * can match, and check if that entry contains a referral.
	 * if it does and managedsait is not set, we return the
	 * referral to the client. if it doesn't, or managedsait
	 * is set, we return no such object.
	 */
	if (!really_internal) {
		struct backentry *me;
		Slapi_DN ancestordn= {0};
		me= dn2ancestor(pb->pb_backend,sdn,&ancestordn,txn,&err);
		if ( !managedsait && me != NULL ) {
			/* if the entry is a referral send the referral */
			if(check_entry_for_referral(pb, me->ep_entry, (char*)slapi_sdn_get_dn(&ancestordn), "find_entry_internal_dn"))
			{
				cache_return( &inst->inst_cache, &me );
				slapi_sdn_done(&ancestordn);
				return( NULL );
			}
			/* else fall through to no such object */
		}

		/* entry not found */
		slapi_send_ldap_result( pb, ( 0 == err || DB_NOTFOUND == err ) ?
			LDAP_NO_SUCH_OBJECT : LDAP_OPERATIONS_ERROR, (char*)slapi_sdn_get_dn(&ancestordn), NULL,
			0, NULL );
		slapi_sdn_done(&ancestordn);
		cache_return( &inst->inst_cache, &me );
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= find_entry_internal_dn not found (%s)\n",
	    slapi_sdn_get_dn(sdn), 0, 0 );
	return( NULL );
}

/* Note that this function does not issue any referals.
   It should only be called in case of 5.0 replicated operation
   which should not be referred.
 */
static struct backentry *
find_entry_internal_uniqueid(
	Slapi_PBlock	*pb,
    backend *be,
	const char 			*uniqueid,
    int				lock,
	back_txn		*txn
)
{
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
	struct backentry	*e;
	int			err;
	size_t tries = 0;

	while ( (tries < LDBM_CACHE_RETRY_COUNT) && 
			(e = uniqueid2entry(be, uniqueid, txn, &err ))
	    != NULL ) {

		/*
		 * we'd like to return the entry. lock it if requested,
		 * retrying if necessary.
		 */

		/* wait for entry modify lock */
		if ( !lock || cache_lock_entry( &inst->inst_cache, e ) == 0 ) {
			LDAPDebug( LDAP_DEBUG_TRACE,
			    "<= find_entry_internal_uniqueid found; uniqueid = (%s)\n", 
			uniqueid, 0, 0 );
			return( e );
		}
		/*
		 * this entry has been deleted - see if it was actually
		 * replaced with a new copy, and try the whole thing again.
		 */
		LDAPDebug( LDAP_DEBUG_ARGS,
			"   find_entry_internal_uniqueid retrying; uniqueid = (%s)\n", 
			uniqueid, 0, 0 );
		cache_return( &inst->inst_cache, &e );
		tries++;
	}
	if (tries >= LDBM_CACHE_RETRY_COUNT) {
		LDAPDebug( LDAP_DEBUG_ANY,
			"find_entry_internal_uniqueid retry count exceeded; uniqueid = (%s)\n", 
			uniqueid , 0, 0 );
	}

	/* entry not found */
	slapi_send_ldap_result( pb, ( 0 == err || DB_NOTFOUND == err ) ?
		LDAP_NO_SUCH_OBJECT : LDAP_OPERATIONS_ERROR, NULL /* matched */, NULL,
		0, NULL );
	LDAPDebug( LDAP_DEBUG_TRACE, 
		"<= find_entry_internal not found; uniqueid = (%s)\n",
	    uniqueid, 0, 0 );
	return( NULL );
}

static struct backentry *
find_entry_internal(
    Slapi_PBlock		*pb,
    Slapi_Backend *be,
    const entry_address *addr,
    int			lock,
	back_txn *txn,
	int really_internal
)
{
	/* check if we should search based on uniqueid or dn */
	if (addr->uniqueid!=NULL)
	{
		LDAPDebug( LDAP_DEBUG_TRACE, "=> find_entry_internal (uniqueid=%s) lock %d\n",
		    addr->uniqueid, lock, 0 );
		return (find_entry_internal_uniqueid (pb, be, addr->uniqueid, lock, txn));
	}
	else
	{
		Slapi_DN sdn;
		struct backentry *entry;

		slapi_sdn_init_dn_ndn_byref (&sdn, addr->dn); /* normalized by front end */
		LDAPDebug( LDAP_DEBUG_TRACE, "=> find_entry_internal (dn=%s) lock %d\n",
				   addr->dn, lock, 0 );
		entry = find_entry_internal_dn (pb, be, &sdn, lock, txn, really_internal);
		slapi_sdn_done (&sdn);
		return entry;
	}
	
}

struct backentry *
find_entry(
    Slapi_PBlock		*pb,
    Slapi_Backend *be,
    const entry_address *addr,
	back_txn *txn
)
{
	return( find_entry_internal( pb, be, addr, 0/*!lock*/, txn, 0/*!really_internal*/ ) );
}

struct backentry *
find_entry2modify(
    Slapi_PBlock		*pb,
    Slapi_Backend *be,
    const entry_address *addr,
	back_txn *txn
)
{
	return( find_entry_internal( pb, be, addr, 1/*lock*/, txn, 0/*!really_internal*/ ) );
}

/* New routines which do not do any referral stuff.
   Call these if all you want to do is get pointer to an entry
   and certainly do not want any side-effects relating to client ! */

struct backentry *
find_entry_only(
    Slapi_PBlock		*pb,
    Slapi_Backend *be,
    const entry_address *addr,
	back_txn *txn
)
{
	return( find_entry_internal( pb, be, addr, 0/*!lock*/, txn, 1/*really_internal*/ ) );
}

struct backentry *
find_entry2modify_only(
    Slapi_PBlock		*pb,
    Slapi_Backend *be,
    const entry_address *addr,
	back_txn *txn
)
{
	return( find_entry_internal( pb, be, addr, 1/*lock*/, txn, 1/*really_internal*/ ) );
}
