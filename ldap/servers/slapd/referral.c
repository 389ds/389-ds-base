/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * referrals.c - LDAP referral-related routines.
 *    a) manage in-memory copiedfrom and copyingfrom referrals.
 *    b) function to adjust smart referrals to match operation parameters.
 */
#include <stdio.h>
#include "slap.h"

/* Forward Decls */
static void adjust_referral_basedn( char **urlp, const Slapi_DN *refcontainerdn, char *opdn_norm, int isreference );
static int dn_is_below( const char *dn_norm, const char *ancestor_norm );
static Ref_Array *g_get_global_referrals(void);
static void ref_free (Ref **goner);
static Ref_Array global_referrals;

#define SLAPD_DEFAULT_REFARRAY_SIZE 10

/*
 *  Function: g_get_global_referrals
 *
 *  Returns: nothing
 *
 *  Description: Fills the global_referrals referral array.
 *               The size is set to "size". The mutex is created.
 *               The array is calloc'd.
 */
static Ref_Array *
g_get_global_referrals(void)
{
    if (global_referrals.ra_rwlock == NULL)
	{
        /* Make a new lock */
        global_referrals.ra_rwlock = rwl_new();
      
        if (global_referrals.ra_rwlock == NULL) {
            LDAPDebug( LDAP_DEBUG_ANY,
    		   "ref_array_init: new lock creation failed\n", 0, 0, 0);
        	exit (-1);
        }
        
        /* Initialize all the fields. */
        global_referrals.ra_size = SLAPD_DEFAULT_REFARRAY_SIZE;
        global_referrals.ra_nextindex = 0;
        global_referrals.ra_readcount = 0;
        global_referrals.ra_refs = (Ref **) slapi_ch_calloc(SLAPD_DEFAULT_REFARRAY_SIZE, sizeof( Ref * ));
	}
    return( &global_referrals );
}


/*
 * Function: ref_free
 *
 * Returns: nothing
 *
 * Description: frees up "goner"
 *
 * Author: RJP
 *
 */
static void
ref_free (Ref **goner)
{
  slapi_ch_free((void**) &((*goner)->ref_dn));
  ber_bvfree( (*goner)->ref_referral );
  slapi_ch_free((void**) goner);
}


/*
 * Function: referrals_free
 *
 * Returns: nothing
 *
 * Description: frees up everything.
 *
 * Author: RJP
 *
 */
void
referrals_free (void)
{
  int walker;
  Ref_Array *grefs = NULL;

  grefs = g_get_global_referrals();

  GR_LOCK_WRITE();

  /* Walk down the array, deleting each referral */
  for (walker = 0; walker < grefs->ra_nextindex; walker++){
   ref_free (&grefs->ra_refs[walker]);

  } 
  
  slapi_ch_free ((void **) &grefs->ra_refs);

  GR_UNLOCK_WRITE();

  rwl_free( &grefs->ra_rwlock );
}

/*
 * ref_adjust() -- adjust referrals based on operation-specific data.
 * The general idea is for us (the server) to be smart so LDAP clients
 * can be as dumb as possible.
 *
 * XXXmcs: We always duplicate the referrals even if no adjustments need to
 * be made.  It would be more efficient but more complicated to avoid making
 * a copy if we don't need to change anything.  If that were done, the
 * interface to this function would need to change since the caller would
 * need to know whether the returned memory needed to be freed or not.
 *
 * Parameters:
 *     pb is the pblock for the operation we are working on.
 *     urls is a list of referrals.
 *     refsdn is the Slapi_DN of the entry where the referral is stored.
 *     is_reference is true if a searchResultReference is being returned.
 *
 * Returns:
 *    a (possibly modified) copy of the urls.  It should be freed by
 *         calling ber_bvecfree().
 *
 *
 * First we strip off any labels from the referral URLs.  For example, a
 * referral like this:
 *
 *    ldap://directory.airius.com/ou=people,o=airius.com  Ref to people tree
 *
 * is changed to:
 *
 *    ldap://directory.airius.com/ou=people,o=airius.com
 *
 * Next, if the referral includes a baseDN we potentially modify it or strip
 * it off entirely. If the referral doesn't include a baseDN and we're
 * processing a continuation reference, we add a baseDN.
 *
 * Finally, if we are processing a continuation reference that resulted from
 * a one-level search, we append "??base" to the referral so the client will
 * use the correct scope when chasing the reference.
 */
struct berval **
ref_adjust( Slapi_PBlock *pb, struct berval **urls, const Slapi_DN *refsdn,
	int is_reference )
{
    int			i, len, scope;
    char		*p, *opdn_norm;
    struct berval	**urlscopy;
    Operation		*op;

    if ( NULL == urls || NULL == urls[0] ) {
	return( NULL );
    }

    PR_ASSERT( pb != NULL );

    /*
     * grab the operation target DN and operation structure.
     * if the operation is a search, get the scope as well.
     */
    if ( slapi_pblock_get( pb, SLAPI_TARGET_DN, &p ) != 0 || p == NULL ||
	    slapi_pblock_get( pb, SLAPI_OPERATION, &op ) != 0 || op == NULL ||
	    ( operation_get_type(op) == SLAPI_OPERATION_SEARCH && slapi_pblock_get( pb,
	    SLAPI_SEARCH_SCOPE, &scope ) != 0 )) {
	LDAPDebug( LDAP_DEBUG_ANY, "ref_adjust: referrals suppressed "
		"(could not get target DN, operation, or scope from pblock)\n",
		0, 0, 0 );
	return( NULL );
    }

    /*
     * normalize the DNs we plan to compare with others later.
     */
    opdn_norm = slapi_dn_normalize( slapi_ch_strdup( p ));


    /*
     * count referrals and duplicate the array
     */
    for ( i = 0; urls[i] != NULL; ++i ) {
	;
    }
    urlscopy = (struct berval **) slapi_ch_malloc(( i + 1 ) *
	    sizeof( struct berval * ));

    for ( i = 0; urls[i] != NULL; ++i ) {
	/*
	 * duplicate the URL, stripping off the label if there is one and
	 * leaving extra room for "??base" in case we need to append that.
	 */
	urlscopy[i] = (struct berval *)slapi_ch_malloc(
		sizeof( struct berval ));
	if (( p = strchr( urls[i]->bv_val, ' ' )) == NULL ) {
	    len = strlen( urls[i]->bv_val );
	} else {
	    len = p - urls[i]->bv_val;
	}
	urlscopy[i]->bv_val = (char *)slapi_ch_malloc( len + 7 );
	memcpy( urlscopy[i]->bv_val, urls[i]->bv_val, len );
	urlscopy[i]->bv_val[len] = '\0';

	/*
	 * adjust the baseDN as needed and set the length
	 */
	adjust_referral_basedn( &urlscopy[i]->bv_val, refsdn,
		opdn_norm, is_reference );
	urlscopy[i]->bv_len = strlen( urlscopy[i]->bv_val );

	/*
	 * if we are dealing with a continuation reference that resulted
	 * from a one-level search, add a scope of base to the URL.
	 */
	if ( is_reference && operation_get_type(op) == SLAPI_OPERATION_SEARCH &&
		scope == LDAP_SCOPE_ONELEVEL ) {
	    strcat( urlscopy[i]->bv_val, "??base" );
	    urlscopy[i]->bv_len += 6;
	}
    }

    urlscopy[i] = NULL;	/* NULL terminate the new referrals array */

    /*
     * log what we did (for debugging purposes)
     */
	if ( LDAPDebugLevelIsSet( LDAP_DEBUG_ARGS )) {
	for ( i = 0; urlscopy[i] != NULL; ++i ) {
	    LDAPDebug( LDAP_DEBUG_ARGS, "ref_adjust: \"%s\" -> \"%s\"\n",
		urls[i]->bv_val, urlscopy[i]->bv_val, 0 );
	}
    }

    /*
     * done -- clean up and return the new referrals array
     */
    slapi_ch_free( (void **)&opdn_norm );

    return( urlscopy );
}



/*
 * adjust_referral_basedn() -- pull the referral apart and modify the
 * baseDN contained in the referral if appropriate not.
 * If the referral does not have baseDN and we're not processing a
 * continuation reference, we do not need to do anything. If we're
 * processing a continuation reference, we need to add a baseDN.
 *
 * If it does have one, there are two special cases we deal with:
 *
 *  1) The referral's baseDN is an ancestor of the operation's baseDN,
 *     which means that the LDAP client already has a more specific
 *     baseDN than is contained in the referral.  If so, we strip off
 *     the baseDN so the client will re-use its operation DN when
 *     chasing the referral.
 *
 *  2) The referral's baseDN is not an ancestor of the operation's
 *     baseDN and the DN of the entry that contains the referral is
 *     an ancestor of the operation DN.  If so, we remove the portion
 *     of the operation DN that matches the DN of the entry containing
 *     the referral and prepend what's left to the referral base DN.
 *
 * We assume that the baseDN is the right-most piece of the URL, i.e., there
 * is no attribute list, scope, or options after the baseDN.
 *
 * The code also assumes that the baseDN doesn't contain any '/' character.
 * This later assumption NEED to be removed and code changed appropriately.
 */
static void
adjust_referral_basedn( char **urlp, const Slapi_DN *refsdn,
	char *opdn_norm, int isreference )
{
    LDAPURLDesc		*ludp = NULL;
    char		*p, *refdn_norm;
	int rc = 0;
	
    PR_ASSERT( urlp != NULL );
    PR_ASSERT( *urlp != NULL );
    PR_ASSERT( refsdn != NULL );
    PR_ASSERT( opdn_norm != NULL );

	if (opdn_norm == NULL){
		/* Be safe but this should never ever happen */
		return;
	}
	
	rc = ldap_url_parse( *urlp, &ludp );

	if	((rc != 0) && 
		 (rc != LDAP_URL_ERR_NODN)) 
		/* 
		 * rc != LDAP_URL_ERR_NODN is to circumvent a pb in the C-SDK
		 * in ldap_url_parse. The function will return an error if the
		 * URL contains no DN (though it is a valid URL according to
		 * RFC 2255.
		 */
	{
		/* Nothing to do, just return */
		return;
	}
	
    if (ludp && (ludp->lud_dn != NULL)) {

		refdn_norm = slapi_dn_normalize( slapi_ch_strdup( ludp->lud_dn ));
		
		if ( dn_is_below( opdn_norm, refdn_norm )) {
			/*
			 * Strip off the baseDN.
			 */
			if (( p = strrchr( *urlp, '/' )) != NULL ) {
				*p = '\0';
			}

		} else if ( dn_is_below( opdn_norm, slapi_sdn_get_ndn(refsdn) )) {
			int		cur_len, add_len;
			
			/*
			 * Prepend the portion of the operation DN that does not match
			 * the ref container DN to the referral baseDN.
			 */
			add_len = strlen( opdn_norm ) - strlen( slapi_sdn_get_ndn(refsdn) );
			cur_len = strlen( *urlp );
			/* + 7 because we keep extra space in case we add ??base */
			*urlp = slapi_ch_realloc( *urlp, cur_len + add_len + 7 );
			
			if (( p = strrchr( *urlp, '/' )) != NULL ) {
				++p;
				memmove( p + add_len, p, strlen( p ) + 1 );
				memmove( p, opdn_norm, add_len );
			}
			
		}	/* else: leave the original referral baseDN intact. */
		
		slapi_ch_free( (void **)&refdn_norm );
    } else if (isreference) { 
		int		cur_len, add_len;

		/* If ludp->lud_dn == NULL and isreference : Add the DN of the ref entry*/
		if ( dn_is_below( opdn_norm, slapi_sdn_get_ndn(refsdn) )){
			add_len = strlen(opdn_norm);
			p = opdn_norm;
		} else {
			add_len = strlen(slapi_sdn_get_ndn(refsdn));
			p = (char *)slapi_sdn_get_ndn(refsdn);
		}
		
		cur_len = strlen( *urlp );
		/* + 8 because we keep extra space in case we add a / and/or ??base  */
		*urlp = slapi_ch_realloc( *urlp, cur_len + add_len + 8 );
		if ((*urlp)[cur_len - 1] != '/'){
			/* The URL is not ending with a /, let's add one */
			strcat(*urlp, "/");
		}
		strcat(*urlp, p);
	}
	
    if ( ludp != NULL ) {
		ldap_free_urldesc( ludp );
    }
	return;
}


/*
 * dn_is_below(): return non-zero if "dn_norm" is below "ancestor_norm" in
 * the DIT and return zero if not.
 */
static int
dn_is_below( const char *dn_norm, const char *ancestor_norm )
{
    PR_ASSERT( dn_norm != NULL );
    PR_ASSERT( ancestor_norm != NULL );

    if ( 0 == strcasecmp( dn_norm, ancestor_norm )) {
	return( 0 );	/* same DN --> not an ancestor relationship */
    }

    return( slapi_dn_issuffix( dn_norm, ancestor_norm ));
}



/*
 * This function is useful to discover the source of data and provide
 * this as a referral. It is also useful if someone simply wants
 * to know if a dn is mastered somewhere else.
 *
 * For a given dn, traverse the referral list and look for the copiedFrom
 * attribute. If such an attribute is found get the server hostname
 * and port in the form "ldap://hostname:port".
 * Else return NULL.
 * 
 * ORC extension: if orc == 1, this function will search for copyingFrom
 * which will refer searches and compares on trees that are half-baked.
 *
 * ORC extension: if cf_refs is NULL, then global_referrals is consulted,
 * otherwise, cf_refs is consulted. 
 */
struct berval **
get_data_source(Slapi_PBlock *pb, const Slapi_DN *sdn, int orc, void *cfrp)
{
  int             walker;
  struct berval **bvp;
  struct berval  *bv;
  int             found_it;
  Ref_Array      *grefs = NULL;
  Ref_Array      *the_refs = NULL;
  Ref_Array      *cf_refs = (Ref_Array *)cfrp;

  /* If no Ref_Array is given, use global_referrals */
  if (cf_refs == NULL) {
    grefs = g_get_global_referrals();
    the_refs = grefs;
    GR_LOCK_READ();
  } else {
    the_refs = cf_refs;
  }

  /* optimization: if orc is 1 (a read), then check the readcount*/
  if (orc && the_refs->ra_readcount == 0) {
    if (cf_refs == NULL) {
      GR_UNLOCK_READ();
    }
    return (NULL);
  }
  

  bvp = NULL;
  bv = NULL;
  found_it = 0;

  /* Walk down the array, testing each dn to make see if it's a parent of "dn" */
  for (walker = 0; walker < the_refs->ra_nextindex; walker++){

    if ( slapi_dn_issuffix(slapi_sdn_get_ndn(sdn), the_refs->ra_refs[walker]->ref_dn)) {
      found_it = 1;
      break;

    }
  }
  
  /* no referral, so return NULL */
  if (!found_it) {
    if (cf_refs == NULL) {
      GR_UNLOCK_READ();
    }
    return (NULL);
  }

  /* 
   * Gotta make sure we're returning the right one. If orc is 1, then
   * only return a read referral. if orc is 0, then only return a
   * write referral.
   */
  if (orc && the_refs->ra_refs[walker]->ref_reads != 1) {
    if (cf_refs == NULL) {
      GR_UNLOCK_READ();
    }
    return (NULL);
  }

  if (!orc && the_refs->ra_refs[walker]->ref_writes != 1) {
    if (cf_refs == NULL) {
      GR_UNLOCK_READ();
    }
    return (NULL);
  }


  /* Fix for 310968 --- return an SSL referral to an SSL client */
	if ( 0 != ( pb->pb_conn->c_flags & CONN_FLAG_SSL )) {
		/* SSL connection */
		char * old_referral_string = NULL;
		char * new_referral_string = NULL;
		char *p = NULL;
		/* Get the basic referral */
		bv = slapi_ch_bvdup(the_refs->ra_refs[walker]->ref_referral);
		old_referral_string = bv->bv_val;
		/* Re-write it to replace ldap with ldaps, and remove the port information */
		/* The original string will look like this: ldap://host:port */
		/* We need to make it look like this: ldaps://host */
		/* Potentially the ":port" part might be missing from the original */
		new_referral_string = slapi_ch_smprintf("%s%s" , LDAPS_URL_PREFIX, old_referral_string + strlen(LDAP_URL_PREFIX) );
		/* Go looking for the port */
		p = new_referral_string + (strlen(LDAPS_URL_PREFIX) + 1);
		while (*p != '\0' && *p != ':') p++;
		if (':' == *p) {
			/* It had a port, zap it */
			*p = '\0';
		}
		/* Fix the bv to point to this new string */
		bv->bv_val = new_referral_string;
		/* Fix its length */
		bv->bv_len = strlen(bv->bv_val);
		/* Free the copy we made of the original */
		slapi_ch_free((void**)&old_referral_string);
	} else {
		/* regular connection */
		bv = (struct berval *) slapi_ch_bvdup(the_refs->ra_refs[walker]->ref_referral);
	}  

  /* Package it up and send that puppy. */
  bvp = (struct berval **) slapi_ch_malloc( 2 * sizeof(struct berval *) );
  bvp[0] = bv;
  bvp[1] = NULL;

  if (cf_refs == NULL) {
    GR_UNLOCK_READ();
  }

  return(bvp);

}
