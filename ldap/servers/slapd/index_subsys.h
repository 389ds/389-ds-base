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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#ifndef _INDEX_SUBSYS_H_
#define _INDEX_SUBSYS_H_

#include "slapi-plugin.h"

typedef void IndexEntryList;
typedef unsigned int	IndexEntryID;

typedef int (*index_search_callback)(Slapi_Filter *filter, IndexEntryList **results, void *user_data );
typedef int (*index_validate_callback)();

typedef struct __indexed_item
{
	/* item that is indexed, an LDAP string filter description of the index
	 * x=*  = presence
	 * x=** = equality
	 * x=?* = substrings
	 */
    char *index_filter; /* item that is indexed, an LDAP string filter description of the index e.g. (presence=*) */
    index_search_callback search_op; /* search call back */
	char **associated_attrs; /* null terminated list of filter groupable attributes */
	Slapi_DN *namespace_dn; /* the namespace this index is valid for */
} indexed_item;


#define INDEX_FILTER_EVALUTED	0
#define INDEX_FILTER_UNEVALUATED 1


/* prototypes */

/* for index plugins */
int slapi_index_entry_list_create(IndexEntryList **list);
int slapi_index_entry_list_add(IndexEntryList **list, IndexEntryID id);
int slapi_index_register_decoder(char *plugin_id, index_validate_callback validate_op);
int slapi_index_register_index(char *plugin_id, indexed_item *registration_item, void *user_data);

/* for backends */
int index_subsys_assign_filter_decoders(Slapi_PBlock *pb);
int index_subsys_filter_decoders_done(Slapi_PBlock *pb);
int index_subsys_evaluate_filter(Slapi_Filter *f, Slapi_DN *namespace_dn, IndexEntryList **out);

#endif /*_INDEX_SUBSYS_H_*/
