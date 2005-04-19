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

