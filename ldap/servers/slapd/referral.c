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
static int ref_array_del(Ref **target, int write, int read);
static int ref_array_add(const char *dn, struct berval *referral, int write, int read);
static Ref **ref_array_find(const char *dn);
static int ref_array_mod(Ref **target, struct berval *referral, int write, int read);
static void strcat_escaped( char *s1, char *s2 );
static void adjust_referral_basedn( char **urlp, const Slapi_DN *refcontainerdn, char *opdn_norm, int isreference );
static int dn_is_below( const char *dn_norm, const char *ancestor_norm );
static Ref_Array *g_get_global_referrals(void);
static void ref_free (Ref **goner);
static Ref_Array global_referrals;

struct refCb {
    int type;
    char *cbName;
    void (*cb)(Slapi_PBlock *, void *);
    void *cbData;
    struct refCb *next;
};

struct refCb *refCbList=NULL;

int
ref_register_callback(int type, char *description, 
		      void (*cb)(Slapi_PBlock *, void *), void *cbData);
int
ref_remove_callback(char *description);
static
int ref_call_cbs(int type, Slapi_PBlock *pb);


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
 *  Function: ref_array_del
 *
 *  Returns: 0 good, -1 bad.
 *
 *  Description: finds "dn" in the list and unsets the read or write
 *               flag(s). If both are then zero, then it frees up that entry.
 *               target should point to the referral that is to be deleted.
 *               Note: This does NOT lock global_referrals.ra_rwlock.
 *               
 * Author: RJP
 */
static int
ref_array_del(Ref **target, int write, int read)
{
  Ref           **lastref = NULL;
  Ref_Array      *grefs = NULL;

  grefs = g_get_global_referrals();

  if (target == NULL) {
    return(-1); 
  }

  /*Unset either or both flags*/
  if (write) {
    (*target)->ref_writes = 0;
  }
  if (read) {
    (*target)->ref_reads = 0;
    grefs->ra_readcount--;
  }
  
  /*If there is a flag set, then don't delete the referral*/
  if (((*target)->ref_writes == 1) || (*target)->ref_reads == 1){
    return(0);
  }
  
  /* Free up target */
  ref_free(target);


  /*
   * Okay, we want to maintain our array's compactedness, 
   * so we take the referral that's in the last position, and 
   * put that in target's place. If they are one and the 
   * same, then no problem. This shouldn't ever seg fault.
   * (famous last words).
   */
  lastref = &grefs->ra_refs[grefs->ra_nextindex - 1];

  *target = *lastref;

  grefs->ra_refs[grefs->ra_nextindex - 1] = NULL;

  /*reset the next_index*/
  grefs->ra_nextindex--;

  return(0);

}


/*
 *  Function: ref_array_replace
 *
 *  Returns: 0 good, -1 bad.
 *
 *  Description: Locks the mutex associated with global_referrals,
 *               adds that referral and "dn" to the global_referrals if it
 *               doesn't exist already. If it does exist, and the new
 *               referral is different, then the old one gets clobbered.
 *               If referral is NULL, then it deletes the referral 
 *               associated with dn.
 *               Note: This locks global_referrals.ra_rwlock.
 *               
 * Author: RJP
 */
int
ref_array_replace(const char *dn, struct berval *referral, int write, int read)
{
  Ref           **target = NULL;
  int            err;
  Ref_Array      *grefs = NULL;

  grefs = g_get_global_referrals();

  if (dn == NULL) {
    return(0);
  }

  GR_LOCK_WRITE();
  
  /* Find the referral, if any. */
  target = ref_array_find(dn);

  /* If datasource is NULL, then delete target. */
  if ( referral == NULL ){

    /* If target is null, then there is nothing to do. */
    if (target == NULL) {
      GR_UNLOCK_WRITE();
      return(0);
    }
    err = ref_array_del(target, write, read);
        
    GR_UNLOCK_WRITE();

    return(err);
  }

  /* If target is NULL, then add target to the end. */
  if ( target == NULL) {
    err = ref_array_add(dn, referral, write, read);
    GR_UNLOCK_WRITE();
    return(err);
  }

  /* Else, the referral already exists and we should modify it. */
  err = ref_array_mod(target, referral, write, read);
  GR_UNLOCK_WRITE();
  return(err);
}


/*
 *  Function: ref_array_mod
 *
 *  Returns: 0 good, -1 bad.
 *
 *  Description: modifies the existing referral.
 *               First it checks the host:port in datasource 
 *               against the host:port in the existing referral.
 *               If they don't match, then it replaces the referral
 *
 *               Note: This does NOT lock global_referrals.ra_rwlock.
 *               
 * Author: RJP
 */
static int
ref_array_mod(Ref **target, struct berval *referral, int write, int read)
{
  Ref_Array     *grefs  = NULL;

  grefs = g_get_global_referrals();

  if (referral == NULL || target == NULL) {
    return(0);
  }

  /* Actually, instead of comparing them, we might as well just swap */
  ber_bvfree( (*target)->ref_referral );

  (*target)->ref_referral = referral ;

  /* 
   * We have to update the read/write flags which
   * refer reads and writes, respectively
   */
  if (write) {
    (*target)->ref_writes = 1;
  }
  if (read) {
    /*Don't update the readcount unnecessarily*/
    if ((*target)->ref_reads == 0) {
      grefs->ra_readcount++;
    }
    
    (*target)->ref_reads = 1;
  }

  return(0);
  
}



/*
 *  Function: ref_array_add
 *
 *  Returns: 0 good, -1 bad.
 *
 *  Description: adds that referral and "dn" to the global_referrals 
 *               Note: This does NOT lock global_referrals.ra_rwlock.
 *               
 * Author: RJP
 */
static int
ref_array_add(const char *dn, struct berval *referral, int write, int read)
{
  Ref           **target = NULL;
  Ref_Array     *grefs = NULL;

  grefs = g_get_global_referrals();

  if (dn == NULL || referral == NULL) {
    return(0);
  }


  /* We may have to realloc if we are about to index an out-of-range slot */
  if (grefs->ra_nextindex >= grefs->ra_size){
    /* reset the size */
    grefs->ra_size += 10;
    
    /* reallocate */
    grefs->ra_refs = (Ref **) slapi_ch_realloc((char *) grefs->ra_refs, 
						  grefs->ra_size * (sizeof(Ref *)));
  }

  /* Tack the new referral to the end. */
  target = &(grefs->ra_refs[grefs->ra_nextindex]);
  
  /* Malloc and fill the fields of the new referral */
  (*target) = (Ref *) slapi_ch_malloc( sizeof(Ref));
  (*target)->ref_dn = slapi_dn_normalize_case(slapi_ch_strdup(dn));
  (*target)->ref_referral = referral;
  
  /* Update the next available index */
  grefs->ra_nextindex++;

  (*target)->ref_writes = 0;
  (*target)->ref_reads = 0;

  /* 
   * We have to update the read/write flags which
   * refer reads and writes, respectively
   */
  if (write) {
    (*target)->ref_writes = 1;
  }
  if (read) {
    (*target)->ref_reads = 1;
    grefs->ra_readcount++;
  }

  return(0);
}

/*
 * Function: ref_array_find
 *
 * Returns: a pointer to a pointer to a Ref, or NULL
 *
 * Description: Traverses the array of referrals, until
 *              it either finds a match or gets to the end.
 *              Note: This DOES NOT lock global_referrals.ra_rwlock.
 *
 * Author: RJP
 *
 */
static Ref **
ref_array_find(const char *dn)
{
  int walker;
  Ref_Array  *grefs = NULL;

  grefs = g_get_global_referrals();

  if (dn == NULL) {
    return(NULL);
  }

  /* Walk down the array, testing for a match */
  for (walker = 0; walker < grefs->ra_nextindex; walker++){

    if (strcasecmp (grefs->ra_refs[walker]->ref_dn, dn) == 0) {
      return(&grefs->ra_refs[walker]);
    }
  }
  return(NULL);
}

/*
 * Function: send_read_referrals
 *
 * Returns: A copy of global_referrals
 *
 * Description: Given a dn, this function sends all the copyingfrom
 *              referrals beneath "dn" that are within "scope."
 *              returns a copy of the global_referrals array
 *              that it makes for use later. This is to avoid
 *              any race conditions of ORC ending in the middle
 *              of the search and scewing things up. NULL is returned
 *              if there are no copyingfrom referrals in there.
 *
 *              If "dn" does not exactly match a referral's dn, we append
 *              "/referralDN" to the referral itself, i.e, we send a
 *              referral like this:
 *                   ldap://host:port/dn
 *              instead of one like this:
 *                   ldap://host:port
 *              We do not append the referral DN to the referrals present
 *              in the copy of the global_referrals array that we return.
 *
 * Author: RJP
 *
 */
Ref_Array * 
send_read_referrals(Slapi_PBlock *pb, int scope, char *dn,
	struct berval ***urls)
{
  int             walker, urllen, dnlen;
  struct berval  *refs[2], refcopy;
  char           *urlcopy;
  Ref_Array      *grefs     = NULL;
  Ref_Array      *the_copy  = NULL;
  int             found_one = 0;

  /* Get a pointer to global_referrals */
  grefs = g_get_global_referrals();
  
  GR_LOCK_READ();

  /*If no copyingfroms, just return*/
  if (grefs->ra_readcount <= 0) {
    GR_UNLOCK_READ();
    return(NULL);
  }
  
  refs[1] = NULL;

  /*
   * Walk through the refs in the_copy and send any referrals
   * that are below "dn".  Take "scope" into account as well.
   */
  for (walker = 0; walker < grefs->ra_nextindex; walker++) {
    if ( grefs->ra_refs[walker]->ref_reads && 
	 (( scope == LDAP_SCOPE_BASE &&
	     strcasecmp(grefs->ra_refs[walker]->ref_dn, dn) == 0 ) ||
	 ( scope == LDAP_SCOPE_ONELEVEL &&
	     slapi_dn_isparent(dn, grefs->ra_refs[walker]->ref_dn)) ||
	 ( scope == LDAP_SCOPE_SUBTREE &&
	     slapi_dn_issuffix(grefs->ra_refs[walker]->ref_dn, dn)))) {
      found_one = 1;
      
      /*
       * Make an array of 1 referral.  If the referral DN is below "dn",
       * i.e, it is not the same as "dn", we make a copy and append a
       * URL-escaped version of the referral DN to the original referral.
       */
      if ( scope == LDAP_SCOPE_BASE ||
	    strcasecmp( grefs->ra_refs[walker]->ref_dn, dn ) == 0 ) {
	refs[0] = grefs->ra_refs[walker]->ref_referral;
	urlcopy = NULL;
      } else {
	urllen = strlen( grefs->ra_refs[walker]->ref_referral->bv_val );
	dnlen = strlen( grefs->ra_refs[walker]->ref_dn );
	/* space for worst-case expansion due to escape plus room for '/' */
	urlcopy = slapi_ch_malloc( urllen + 3 * dnlen + 2 );

	strcpy( urlcopy, grefs->ra_refs[walker]->ref_referral->bv_val );
	urlcopy[urllen] = '/';
	++urllen;
	urlcopy[urllen] = '\0';
	strcat_escaped( urlcopy + urllen, grefs->ra_refs[walker]->ref_dn );

	refcopy.bv_val = urlcopy;
	refcopy.bv_len = strlen( urlcopy );
	refs[0] = &refcopy;
      }

      send_ldap_referral( pb, NULL, refs, urls ); 
      slapi_pblock_set( pb, SLAPI_SEARCH_REFERRALS, *urls );

      if ( urlcopy != NULL ) {
        slapi_ch_free( (void **)&urlcopy );
      }
    }
  }
  
  /* Make a copy of global_referrals to avoid any race conditions */
  if (found_one) {
    the_copy = ref_array_dup();
  }
  
  GR_UNLOCK_READ();

  /*
   * After we sent all the referrals, return the copy of
   * global_referrals for use later. If there were none found, return
   * NULL 
   */
  return(the_copy);
}

/*
 * Function: ref_array_dup
 *
 * Returns: a copy of global_referrals 
 *
 * Description: Makes a copy of global_referrals and returns that puppy
 *              Note: Does not lock global_referrals.
 *
 * Author: RJP
 *
 */
Ref_Array *
ref_array_dup(void)
{
  Ref_Array *grefs    = NULL;
  Ref_Array *the_copy = NULL;
  int        walker;

  /*Allocate the first structure*/
  the_copy = (Ref_Array *) slapi_ch_calloc(1, sizeof(Ref_Array));

  /* Don't bother with the lock, it's only a local copy. */
  the_copy->ra_rwlock = NULL;

  /*Grab a reference to the global_referrals*/
  grefs = g_get_global_referrals();

  /* Initialize all the fields of the copy. */
  the_copy->ra_size      = grefs->ra_size;
  the_copy->ra_nextindex = grefs->ra_nextindex;
  the_copy->ra_readcount = grefs->ra_readcount;
  the_copy->ra_refs = (Ref **) slapi_ch_calloc(the_copy->ra_size, sizeof( Ref * ));

  /*Walk down grefs, copying each Ref struct */
  for (walker = 0; walker < grefs->ra_nextindex; walker++) {
    the_copy->ra_refs[walker] = (Ref *)slapi_ch_calloc(1, sizeof(Ref));						       
    the_copy->ra_refs[walker]->ref_dn = slapi_ch_strdup(grefs->ra_refs[walker]->ref_dn);
    the_copy->ra_refs[walker]->ref_referral = slapi_ch_bvdup(grefs->ra_refs[walker]->ref_referral);
    the_copy->ra_refs[walker]->ref_reads = grefs->ra_refs[walker]->ref_reads;
    the_copy->ra_refs[walker]->ref_writes = grefs->ra_refs[walker]->ref_writes;
  }

  return(the_copy);

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
 * Function: ref_array_dup_free
 *
 * Returns: nothingness
 *
 * Description: takes a Ref_Array dup and frees that puppy
 *
 * Author: RJP
 *
 */
void
ref_array_dup_free(Ref_Array *the_copy)
{
	int walker;

	if (the_copy == NULL) {
		return;
	}

	/* Walk down the array, deleting each referral */
	for (walker = 0; walker < the_copy->ra_nextindex; walker++)
	{
		ref_free (&the_copy->ra_refs[walker]);
	} 

	/* free the array of pointers */
	slapi_ch_free((void **) &the_copy->ra_refs);
	slapi_ch_free((void **) &the_copy);

	return;
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
 *  Function: ref_array_moddn
 *
 *  Returns: 0 good, -1 bad.
 *
 *  Description: modifies the existing referral's dn.
 *               First it locks global_referrals.ra_rwlock.
 *               Then it clobbers the existing dn.
 *               Then it replaces it with a new dn constructed 
 *               from newrdn.
 *               Note: This locks global_referrals.ra_rwlock.
 *               
 * Author: RJP
 */
void
ref_array_moddn(const char *dn, char *newrdn, Slapi_PBlock *pb)
{
  char  *pdn = NULL;
  char  *newdn = NULL;
  Ref  **target = NULL;
  Ref_Array      *grefs = NULL;

  grefs = g_get_global_referrals();

  if (dn == NULL) {
    return;
  }
  
  GR_LOCK_WRITE();
  
  /* Find the referral. */
  target = ref_array_find(dn);
  
  /* 
   * If we can't find it, then we're done. This is okay, because this
   * is the only check that is made to see if the entry has a
   * copiedfrom in it.
   */
  if (target == NULL) {
    GR_UNLOCK_WRITE();
    return;
  }
  /* construct the new dn */
  if ( (pdn = slapi_dn_beparent( pb, dn )) != NULL ) {
    /* parent + rdn + separator(s) + null */
    newdn = (char *) slapi_ch_malloc( strlen( pdn ) + strlen( newrdn ) + 3 );
    strcpy( newdn, newrdn );
    strcat( newdn, ", " );
    strcat( newdn, pdn );
  } else {
    newdn = (char *) slapi_ch_strdup( newrdn );
  }
  slapi_ch_free((void **) &pdn );
  (void) slapi_dn_normalize_case( newdn );


  /* We have found the referral. blow away the dn*/
  slapi_ch_free((void**) &((*target)->ref_dn));

  /* stick in the new one. */
  (*target)->ref_dn = newdn;

  GR_UNLOCK_WRITE();

  return;
}


/*
 * HREF_CHAR_ACCEPTABLE was copied from libldap/tmplout.c
 */
/* Note: an identical function is in ../plugins/replication/replutil.c */
#define HREF_CHAR_ACCEPTABLE( c )	(( c >= '-' && c <= '9' ) ||	\
					 ( c >= '@' && c <= 'Z' ) ||	\
					 ( c == '_' ) ||		\
					 ( c >= 'a' && c <= 'z' ))

/*
 *  Function: strcat_escaped
 *
 *  Returns: nothing
 *
 *  Description: Appends string s2 to s1, URL-escaping (%HH) unsafe
 *               characters in s2 as appropriate.  This function was
 *               copied from libldap/tmplout.c.
 *               
 * Author: MCS
 */
/*
 * append s2 to s1, URL-escaping (%HH) unsafe characters
 */
/* Note: an identical function is in ../plugins/replication/replutil.c */
static void
strcat_escaped( char *s1, char *s2 )
{
    char	*p, *q;
    char	*hexdig = "0123456789ABCDEF";

    p = s1 + strlen( s1 );
    for ( q = s2; *q != '\0'; ++q ) {
	if ( HREF_CHAR_ACCEPTABLE( *q )) {
	    *p++ = *q;
	} else {
	    *p++ = '%';
	    *p++ = hexdig[ 0x0F & ((*(unsigned char*)q) >> 4) ];
	    *p++ = hexdig[ 0x0F & *q ];
	}
    }

    *p = '\0';
}

void
update_global_referrals(Slapi_PBlock *pb)
{
    ref_call_cbs(0,pb);
    return;
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
		/* The longest new string will be one character longer than the old one */
		new_referral_string = slapi_ch_malloc(bv->bv_len + 1); 
		/* Re-write it to replace ldap with ldaps, and remove the port information */
		/* The original string will look like this: ldap://host:port */
		/* We need to make it look like this: ldaps://host */
		/* Potentially the ":port" part might be missing from the original */
		sprintf(new_referral_string, "%s%s" , LDAPS_URL_PREFIX, old_referral_string + strlen(LDAP_URL_PREFIX) );
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


int
ref_register_callback(int type, char *description, 
		      void (*cb)(Slapi_PBlock *, void *), void *cbData)
{
    struct refCb *cbPtr;
    struct refCb *newCb;

    if(NULL == (newCb = 
		(struct refCb *)slapi_ch_calloc(1,sizeof(struct refCb)))) {
	/* out of memory? */
	return(-1);
    }
    newCb->type = type;
    newCb->next = NULL;
    newCb->cb = cb;
    newCb->cbData = cbData;
    newCb->cbName = slapi_ch_strdup(description);

    if(NULL == refCbList) {
	refCbList = newCb;
	return(0);
    }
    cbPtr = refCbList;
    while(NULL != cbPtr->next) cbPtr = cbPtr->next;
    cbPtr->next=newCb;
    
    return(0);
}

int
ref_remove_callback(char *description)
{
    struct refCb *cbPtr = refCbList;
    struct refCb *cbPrev = refCbList;

    if((NULL == description) || (NULL == cbPtr)) 
	return(-1);

    while(cbPtr) {
	if(!strcmp(description,cbPtr->cbName)) {
	    if(cbPrev == refCbList) {
		refCbList = cbPtr->next;
	    } else {
		cbPrev->next = cbPtr->next;
	    }
	    slapi_ch_free((void **)&cbPtr->cbName);
	    /* we don't know how the cbData was allocated...we won't attempt
	       to free it */
	    slapi_ch_free((void **)&cbPtr);
	    break;
	}
	cbPrev = cbPtr;
	cbPtr = cbPtr->next;
    }
    
    return(0);
}

static
int ref_call_cbs(int type, Slapi_PBlock *pb)
{
  struct refCb *cbPtr = refCbList;

  if(NULL == cbPtr) {
    return(0);
  }

  while(cbPtr) {
   (*cbPtr->cb)(pb, cbPtr->cbData);
   cbPtr = cbPtr->next;
  }
     
  return(0);
}

