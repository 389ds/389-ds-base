/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*  Code to implement subentries
*/

/* This is the plan for subentries :

	For updates, they're like regular entries.
  
	For searches, we need to do special stuff:
	We need to examine the search filter. 
	If it contains a branch of the form "objectclass=ldapsubentry",
	then we don't need to do anything special.
	If it does not, we need to do special stuff:
		We need to and a filter clause "!objectclass=ldapsubentry" 
		to the filter. 
		The intention is that no entries having objectclass "ldapsubentry"
		should be returned to the client.

	Now, I feel confident that this will all work, but it poses some
	performance problems. Looking for the filter branch could be
	inefficient. Adding an extra filter test to every operation
	is likely to slow the very operations we care about most.

	Need to think about the best way to optimize this, perhaps using an IDL cache.

 */

#include "slap.h"

/* Function intended to be called only from inside get_filter, so look for subentry search filters */
int subentry_check_filter(Slapi_Filter *f)
{
	if ( 0 == strcasecmp ( f->f_avvalue.bv_val, "ldapsubentry")) {
		/* Need to remember this so we avoid rewriting the filter later */
		return 1; /* Clear the re-write flag, since we've seen the subentry filter element */
	}
	return 0; /* Set the rewrite flag */
}

/* Function which wraps a filter with (AND !(objectclass=ldapsubentry)) */
void subentry_create_filter(Slapi_Filter** filter)
{
	Slapi_Filter *sub_filter = NULL;
	Slapi_Filter *new_filter = NULL;
    char *buf = slapi_ch_strdup("(!(objectclass=ldapsubentry))");
    sub_filter = slapi_str2filter( buf );
    new_filter = slapi_filter_join( LDAP_FILTER_AND, *filter, sub_filter );
	*filter = new_filter;
	slapi_ch_free((void **)&buf);
}

