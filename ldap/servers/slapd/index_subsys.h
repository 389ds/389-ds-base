/** BEGIN COPYRIGHT BLOCK
 * Copyright 2002 Netscape Communications Corporation. All rights reserved.
 * END COPYRIGHT BLOCK **/

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
