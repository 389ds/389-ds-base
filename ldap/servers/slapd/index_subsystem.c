/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* The indexing subsystem
 * ----------------------
 *
 * This provides support for indexing plugins and assigning
 * those plugins to sub-filters of a search filter.  Currently
 * the old indexing code still exists and operates on those
 * indexes which do not have a plugin assigned.  This indexing
 * abstraction is intended to eventually decouple the index mechanics
 * from the back-end where that is possible.  Hopefully, while
 * supporting the needs of virtual attribute indexes, it will allow
 * easier integration of other back ends.
 *
 */

/* includes */
#include "slap.h"
#include "./back-ldbm/back-ldbm.h"
#include "./back-ldbm/idlapi.h"
#include "index_subsys.h"

#define INDEX_IDLIST_INITIAL_SIZE	128		/* make this a good size to avoid constant reallocs */

/* data */
static void **idl_api;

struct _indexLinkedList
{
	void *pNext;
	void *pPrev;
};
typedef struct _indexLinkedList indexLinkedList;

struct _indexEntry
{
	indexLinkedList list;
	char *indexedAttribute;
	Slapi_Filter *indexfilter;
	char *indexfilterstr;
	char **associated_attrs;
	void *user_data;
	Slapi_DN *namespace_dn;
    index_search_callback lookup_func; /* search call back */
};
typedef struct _indexEntry indexEntry;

struct _indexPlugin
{
	indexLinkedList list;
	char *id;
	indexEntry *indexes;
	index_validate_callback validate_op;
};
typedef struct _indexPlugin indexPlugin;

struct _globalIndexCache
{
	indexPlugin *pPlugins;
	indexEntry **ppIndexIndex; /* sorted list with key: indexEntry.indexedAttribute */
	int index_count;
	PRRWLock *cache_lock;
};
typedef struct _globalIndexCache globalIndexCache;

static globalIndexCache *theCache = 0;

/* prototypes */
static int index_subsys_decoder_done(Slapi_Filter *f);
static int index_subsys_assign_decoders(Slapi_Filter *f);
static int index_subsys_assign_decoder(Slapi_Filter *f);
static int index_subsys_group_decoders(Slapi_Filter *f);
static indexEntry *index_subsys_find_decoder(Slapi_Filter *f);
static int index_subsys_unlink_subfilter(Slapi_Filter *fcomplex, Slapi_Filter *fsub);
static int index_subsys_index_matches_associated(indexEntry *index, Slapi_Filter *f);

/* matching alg - note : values 0/1/2/3 supported right now*/
#define INDEX_MATCH_NONE		0
#define INDEX_MATCH_EQUALITY	1
#define INDEX_MATCH_PRESENCE	2
#define INDEX_MATCH_SUBSTRING	3
#define INDEX_MATCH_COMPLEX		4
static int index_subsys_index_matches_filter(indexEntry *index, Slapi_Filter *f);

static void index_subsys_read_lock()
{
	PR_RWLock_Rlock(theCache->cache_lock);
}

static void index_subsys_write_lock()
{
	PR_RWLock_Wlock(theCache->cache_lock);
}

static void index_subsys_unlock()
{
	PR_RWLock_Unlock(theCache->cache_lock);
}

int slapi_index_entry_list_create(IndexEntryList **list)
{
	if(idl_api)
		*list = (IndexEntryList*)IDList_alloc(idl_api, INDEX_IDLIST_INITIAL_SIZE);
	else
		*list = 0;

	return !(*list);
}

int slapi_index_entry_list_add(IndexEntryList **list, IndexEntryID id)
{
	if(idl_api)
		IDList_insert(idl_api, (IDList **)list, (ID)id);

	return 0;  /* no way to tell failure */
}


static int index_subsys_index_matches_filter(indexEntry *index, Slapi_Filter *f)
{
	int ret = INDEX_MATCH_NONE;

	/* simple filters only right now */
	if(slapi_attr_types_equivalent(index->indexedAttribute, f->f_type))
	{
		/* ok we have some type of match, lets find out which */

		switch(index->indexfilter->f_choice)
		{
		case LDAP_FILTER_PRESENT:
			/* present means "x=*" */
			if(f->f_choice == LDAP_FILTER_PRESENT)
				ret = INDEX_MATCH_PRESENCE;
			break;
	    case LDAP_FILTER_SUBSTRINGS:
			/* our equality filters look like this "x=**"
			 * that means the filter will be assigned
			 * a substring f_choice by the filter code
			 * in str2filter.c
			 * but we need to differentiate so we take
			 * advantage of the fact that this creates a
			 * special substring filter with no substring
			 * to look for...
			 */
			if(	index->indexfilter->f_sub_initial == 0 &&
				index->indexfilter->f_sub_any == 0 &&
				index->indexfilter->f_sub_final == 0
				)
			{
				/* this is an index equality filter */
				if(f->f_choice == LDAP_FILTER_EQUALITY)
					ret = INDEX_MATCH_EQUALITY;
			}
			else
			{
				/* this is a regualar substring filter */
				if(f->f_choice == LDAP_FILTER_SUBSTRINGS)
					ret = INDEX_MATCH_SUBSTRING;
			}
			
			break;

		default:
			/* we don't know about any other type yet */
			break;
		}
	}

	return ret;
}

/* index_subsys_assign_filter_decoders
 * -----------------------------------
 * assigns index plugins to sub-filters
 */
int index_subsys_assign_filter_decoders(Slapi_PBlock *pb)
{
	int				rc;
	Slapi_Filter	*f;
	char			*subsystem = "index_subsys_assign_filter_decoders";
	char			logbuf[ 1024 ];

	/* extract the filter */
	slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &f);   

	if ( LDAPDebugLevelIsSet( LDAP_DEBUG_FILTER ) && NULL != f ) {
		logbuf[0] = '\0';
		slapi_log_error( SLAPI_LOG_FATAL, subsystem, "before: %s\n",
				slapi_filter_to_string(f, logbuf, sizeof(logbuf)));
	}

	/* find decoders */
	rc = index_subsys_assign_decoders(f);

	if ( LDAPDebugLevelIsSet( LDAP_DEBUG_FILTER ) && NULL != f ) {
		logbuf[0] = '\0';
		slapi_log_error( SLAPI_LOG_FATAL, subsystem, " after: %s\n",
				slapi_filter_to_string(f, logbuf, sizeof(logbuf)));
	}

	return rc;
}

/* index_subsys_filter_decoders_done
 * ---------------------------------
 * removes assigned index plugins in
 * sub-filters
 */
int index_subsys_filter_decoders_done(Slapi_PBlock *pb)
{
	Slapi_Filter *f;

	/* extract the filter */
	slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &f);   

	/* remove decoders */
	return index_subsys_decoder_done(f);
}


/* index_subsys_unlink_subfilter
 * -----------------------------
 * removes the sub-filter from
 * the complex filter list
 * does NOT deallocate the sub-filter
 */
static int index_subsys_unlink_subfilter(Slapi_Filter *fcomplex, Slapi_Filter *fsub)
{
	int ret = -1;
	Slapi_Filter *f;
	Slapi_Filter *f_prev = 0;

	for(f=fcomplex->f_list; f != NULL; f = f->f_next)
	{
		if(f == fsub)
		{
			if(f_prev)
			{
				f_prev->f_next = f->f_next;
				f->f_next = 0;
				ret = 0;
				break;
			}
			else
			{
				/* was at the beginning of the list */
				fcomplex->f_list = f->f_next;
				f->f_next = 0;
				ret = 0;
				break;
			}
		}

		f_prev = f;
	}

	return ret;
}

/* index_subsys_index_matches_associated
 * -------------------------------------
 * determines if there is any kind of match
 * between the specified type and the index.
 *
 * matches could be on the indexed type or
 * on any associated attribute
 * returns:
 * 0 when false
 * non-zero when true
 */
static int index_subsys_index_matches_associated(indexEntry *index, Slapi_Filter *f)
{
	int ret = 0;
	char **associated_attrs = index->associated_attrs;

	if(INDEX_MATCH_NONE != index_subsys_index_matches_filter(index, f))
	{
		/* matched on indexed attribute */
		ret = -1;
		goto bail;
	}

	/* check associated attributes */
	if(associated_attrs)
	{
		int i;
		char *type = f->f_type;

		for(i=0; associated_attrs[i]; i++)
		{
			if(slapi_attr_types_equivalent(associated_attrs[i], type))
			{
				/* matched on associated attribute */
				ret = -1;
				break;
			}
		}
	}

bail:
	return ret;
}


/* index_subsys_flatten_filter
 * ---------------------------
 * takes a complex filter as argument (assumed)
 * and merges all compatible complex sub-filters
 * such that their list of sub-filters are moved
 * to the main list of sub-filters in f.
 *
 * This "flattens" the filter so that there are
 * the minimum number of nested complex filters
 * possible.
 *
 * What is a "compatible complex sub-filter?"
 * Answer: a complex sub-filter which is of the
 * same type (AND or OR) as the containing complex
 * filter and which is either assigned the same
 * index decoder or no index decoder is assigned to
 * either complex filter.
 *  
 * This function assumes that it has already
 * been called for every complex sub-filter of f
 * i.e. it only looks one layer deep.
 *
 * Note that once a filter has been processed in
 * this fashion, rearranging the filter based
 * on the optimal evaluation order becomes very
 * much simpler.  It should also have benefits for
 * performance when a filter is evaluated many
 * times since a linear list traversal is faster than a
 * context switch to recurse down into a complex
 * filter structure.
 *
 */
static void index_subsys_flatten_filter(Slapi_Filter *flist)
{
	struct slapi_filter *f = flist->f_list;
	struct slapi_filter *fprev = 0;
	struct slapi_filter *flast = 0;

	while(f)
	{
		if(f->assigned_decoder == flist->assigned_decoder)
		{
			/* mmmk, but is the filter complex? */
			if(f->f_choice == LDAP_FILTER_AND || f->f_choice == LDAP_FILTER_OR)
			{
				if(f->f_choice == flist->f_choice)
				{
					/* flatten this, and remember
					 * we expect that any complex sub-filters
					 * have already been flattened, so we
					 * simply transfer the contents of this
					 * sub-filter to the main sub-filter and
					 * remove this complex sub-filter
					 *
					 * take care not to change the filter
					 * ordering in any way (it may have been
					 * optimized)
					 */
					struct slapi_filter *fnext = f->f_next;
					struct slapi_filter *fsub = 0;

					while(f->f_list)
					{
						fsub = f->f_list;
						index_subsys_unlink_subfilter(f, f->f_list);
						fsub->f_next = fnext;

						if(flast)
						{
							/* we inserted something before - insert after */
							flast->f_next = fsub;
						}
						else
						{
							/* first insertion */
							if(fprev)
							{
								fprev->f_next = fsub;
							}
							else
							{
								/* insert at list start */
								flist->f_list = fsub;
							}
							
							fprev = fsub;
						}

						flast = fsub;
					}

					/* zero for dont recurse - recursing would
					 * be bad since we have created a mutant
					 * complex filter with no children
					 */
					slapi_filter_free(f, 0);
					f = fnext;
				}
				else
				{
					fprev = f;
					f = f->f_next;
				}
			}
			else
			{
				fprev = f;
				f = f->f_next;
			}
		}
		else
		{
			fprev = f;
			f = f->f_next;
		}
	}
}

/* index_subsys_group_decoders
 * ---------------------------
 * looks for grouping opportunities
 * such that a complex filter may
 * be assigned to a single index.
 *
 * it is assumed that any complex sub-filters
 * have already been assigned decoders 
 * using this function if it
 * was possible to do so
 */
static int index_subsys_group_decoders(Slapi_Filter *flist)
{
	int ret = 0;
	struct slapi_filter	*f = 0;
	struct slapi_filter *f_indexed = 0;
	struct slapi_filter	*fhead = 0;
	int index_count = 0;
	int matched = 1;

	switch(flist->f_choice)
	{
	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
		break;

	default:
		/* any other result not handled by this code */
		goto bail;
	}

	/* make sure we start with an unassigned filter */
	flist->assigned_decoder = 0;

	/* Since this function is about optimal grouping of complex filters,
	 * lets explain what is happening here:
	 * 
	 * Beyond detecting that what was passed in is already optimal,
	 * there are 4 basic problems we need to solve here:
	 *
	 *          Input                     this function           Output
	 * 1) (&(indexed)(other)(associated)) -> X -> (&(&(indexed)(associated))(other))
	 * 2) (&(&(indexed)(other))(associated)) -> X -> (&(&(indexed)(associated))(other))
	 * 3) (&(&(associated)(other))(indexed)) -> X -> (&(&(indexed)(associated))(other))
	 * 4) (&(&(indexed)(associated))(associated)) -> X -> (&(indexed)(associated)(associated))
	 *
	 * To avoid having special code for 2) and 3) we make them look like 
	 * 1) by flattening the filter - note this will only flatten subfilters
	 * which have no decoder assigned since the filter we flatten has no
	 * decoder assigned - and this behaviour is exactly what we want.
	 * 4) is a special case of 1) and since that is the case, we can allow
	 * the code for 1) to process it but make sure we flatten the filter
	 * before exit.  If 4) is exactly as stated without any other non-indexed
	 * or associated references then in fact it will be detected as a completely
	 * matching filter prior to reaching the code for 1).
	 */

	index_subsys_flatten_filter(flist);
	fhead = flist->f_list;

	/* find the first index assigned */
	for ( f_indexed = fhead; f_indexed != NULL; f_indexed = f_indexed->f_next )
	{
		if( f_indexed->assigned_decoder )
		{
			/* non-null decoder means assigned */
			break;
		}
	}

	if(NULL == f_indexed)
		/* nothing to process */
		goto bail;

	/* determine if whole filter matches
	 * to avoid allocations where it is
	 * not necessary
	 */
	for ( f = fhead; f != NULL; f = f->f_next )
	{
		if(f->assigned_decoder != f_indexed->assigned_decoder)
		{
			switch(f->f_choice)
			{
			case LDAP_FILTER_AND:
			case LDAP_FILTER_OR:
				/*
				 * All complex subfilters are guaranteed to have the correct
				 * decoder assigned already, so this is a mismatch.
				 */

				matched = 0;
				break;

			default:
				if(!index_subsys_index_matches_associated(f_indexed->assigned_decoder, f))
				{
					matched = 0;
				}
				break;
			}

			if(!matched)
				break;
		}
	}

	if(matched)
	{
		/* whole filter matches - assign to this decoder */
		flist->assigned_decoder = f_indexed->assigned_decoder;
		/* finally lets flatten this filter if possible
		 * Didn't we do that already?  No, we flattened the
		 * filter *prior* to assigning a decoder
		*/
		index_subsys_flatten_filter(flist);
		goto bail;
	}

	/* whole filter does not match so,
	 * if the sub-filter count is > 2
	 * for each assigned sub-filter,
	 * match other groupable filters
	 * and extract them into another sub-filter
	 */

	/* count */
	for ( f = fhead; f != NULL && index_count < 3; f = f->f_next )
	{
		index_count++;
	}

	if(index_count > 2)
	{
		/* this is where we start re-arranging the filter assertions
		 * into groups which can be serviced by a single plugin
		 * at this point we know that:
		 * a) the filter has at least 2 assertions that cannot be grouped
		 * b) there are more than 2 assertions and so grouping is still possible
		 */

		struct slapi_filter *f_listposition=f_indexed; /* flist subfilter list iterator */
		int main_iterate; /* controls whether to iterate to the next sub-filter of flist */
		
		while(f_listposition)
		{
			/* the target grouping container - we move sub-filters here */
			struct slapi_filter *targetf=0; 

			/* indicates we found an existing targetf */
			int assigned = 0; 

			struct slapi_filter *f_last = 0; /* previos filter in list */

			/* something to join with next compatible
			 * subfilter we find - this will be the 
			 * first occurence of a filter assigned
			 * to a particular decoder
			 */
			struct slapi_filter	*saved_filter = 0;

			struct slapi_filter	*f_tmp = 0; /* save filter for list fixups */

			/* controls whether to iterate to the
			 * next sub-filter of flist 
			 * inner loop
			 */
			int iterate = 1;

			f_indexed = f_listposition;
			main_iterate = 1;

			/* finding a convenient existing sub-filter of the same
			 * type as the containing filter avoids allocation
			 * so lets look for one
			 */

			for ( f = fhead; f != NULL; f = f->f_next)
			{
				switch(f->f_choice)
				{
				case LDAP_FILTER_AND:
				case LDAP_FILTER_OR:
					if( f->f_choice == flist->f_choice &&
						f->assigned_decoder == f_indexed->assigned_decoder)
					{
						targetf = f;
						assigned = 1;
					}
					break;

				default:
					break;
				}

				if(assigned)
					break;
			}

			/* now look for grouping opportunities */
			for ( f = fhead; f != NULL; (iterate && f) ? f = f->f_next : f )
			{
				iterate = 1;

				if(f != targetf)
				{
					switch(f->f_choice)
					{
					case LDAP_FILTER_AND:
					case LDAP_FILTER_OR:
						if( (targetf && f->f_choice == targetf->f_choice) 
							&& f->assigned_decoder == f_indexed->assigned_decoder)
						{
							/* ok we have a complex filter we can group - group it
							 * it is worth noting that if we got here, then we must
							 * have found a complex filter suitable for for putting
							 * sub-filters in, so there is no need to add the newly
							 * merged complex filter to the main complex filter, 
							 * since it is already there
							 */
							
							/* main filter list fix ups */
							f_tmp = f;
							f = f->f_next;
							iterate = 0;

							if(f_tmp == f_listposition)
							{
								f_listposition = f;
								main_iterate = 0;
							}

							/* remove f from the main complex filter */
							index_subsys_unlink_subfilter(flist, f_tmp);


							/* merge - note, not true merge since f gets
							 * added to targetf as a sub-filter
							 */
							slapi_filter_join_ex(targetf->f_choice, targetf, f_tmp, 0);	
							
							/* since it was not a true merge, lets do the true merge now */
							index_subsys_flatten_filter(targetf);
						}
						break;

					default:
						if(index_subsys_index_matches_associated(f_indexed->assigned_decoder, f))
						{
							if(targetf)
							{
								/* main filter list fix ups */
								f_tmp = f;
								f = f->f_next;
								iterate = 0;
								
								if(f_tmp == f_listposition)
								{
									f_listposition = f;
									main_iterate = 0;
								}

								index_subsys_unlink_subfilter(flist, f_tmp);
								targetf = slapi_filter_join_ex( targetf->f_choice, targetf, f_tmp, 0 );
							}
							else
							{
								if(saved_filter)
								{
									/* main filter list fix ups */
									f_tmp = f;
									f = f->f_next;
									iterate = 0;

									if(f_tmp == f_listposition)
									{
										f_listposition = f;
										main_iterate = 0;
									}

									index_subsys_unlink_subfilter(flist, f_tmp);
									index_subsys_unlink_subfilter(flist, saved_filter);
									targetf = slapi_filter_join_ex( flist->f_choice, saved_filter, f_tmp, 0 );
									targetf->assigned_decoder = f_indexed->assigned_decoder;
								}
								else
								{
									/* nothing to join so save this for
									 * when we find another compatible
									 * filter
									 */
									saved_filter = f;
								}
							}
							
							if(!assigned && targetf)
							{
								/* targetf has just been created, so
								 * we must add it to the main complex filter
								 */
								targetf->f_next = flist->f_list;
								flist->f_list = targetf;
								assigned = 1;
							}
						}

						break;
					}
				}

				f_last = f;
			}

			/* iterate through the main list 
			 * to the next indexed sub-filter
			 */
			if(	f_listposition && 
					(main_iterate ||
						(!main_iterate && 
						!f_listposition->assigned_decoder)))
			{
				if(!f_listposition->f_next)
				{
					f_listposition = 0;
					break;
				}

				for ( f_listposition = f_listposition->f_next; f_listposition != NULL; f_listposition = f_listposition->f_next )
				{
					if( f_listposition->assigned_decoder )
					{
						/* non-null decoder means assigned */
						break;
					}
				}
			}
		}
	}

bail:

	return ret;
}


/* index_subsys_assign_decoders
 * ----------------------------
 * recurses through complex filters
 * assigning decoders
 */
static int index_subsys_assign_decoders(Slapi_Filter *f)
{
	int ret = 0;
	Slapi_Filter *subf;

	switch ( slapi_filter_get_choice( f ) ) 
	{
	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		/* assign simple filters first */
		f->assigned_decoder = 0;
		for(subf=f->f_list; subf != NULL; subf = subf->f_next )
			ret = index_subsys_assign_decoders(subf);

		/* now check for filter grouping opportunities... */
		if(slapi_filter_get_choice( f ) != LDAP_FILTER_NOT)
			index_subsys_group_decoders(f);
		else
		{
			/* LDAP_FILTER_NOT is a special case
			 * the contained sub-filter determines
			 * the assigned index - the index plugin has
			 * the option to refuse to service the
			 * NOT filter when it is presented
			 */
			f->assigned_decoder = f->f_list->assigned_decoder;
		}

		break;

	default:
		/* find a decoder that fits this simple filter */
		ret = index_subsys_assign_decoder(f);
	}

	return ret;
}

/* index_subsys_decoder_done
 * -------------------------
 * recurses through complex filters
 * removing decoders
 */
static int index_subsys_decoder_done(Slapi_Filter *f)
{
	int ret = 0;
	Slapi_Filter *subf;
	indexEntry *index;
	indexEntry *next_index;
	
	switch ( slapi_filter_get_choice( f ) ) 
	{
	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		/* remove simple filters first */
		for(subf=f->f_list; subf != NULL; subf = subf->f_next )
			ret = index_subsys_decoder_done(subf);

		break;

	default:
		/* free the decoders - shallow free */
		index = f->assigned_decoder;

		while(index)
		{
			next_index = index->list.pNext;
			slapi_ch_free((void**)index);
			index = next_index;
		}

		f->assigned_decoder = 0;
	}

	return ret;
}

/* index_subsys_evaluate_filter
 * ----------------------------
 * passes the filter to the correct plugin
 * index_subsys_assign_filter_decoders() must
 * have been called previously on this filter
 * this function can be safely called on all
 * filters post index_subsys_assign_filter_decoders()
 * whether they are assigned to a plugin or not
 * 
 * returns:
 * INDEX_FILTER_EVALUTED: a candidate list is produced
 * INDEX_FILTER_UNEVALUATED: filter not considered
 */
int index_subsys_evaluate_filter(Slapi_Filter *f, Slapi_DN *namespace_dn, IndexEntryList **out)
{
	int ret = INDEX_FILTER_UNEVALUATED;
	indexEntry *index = (indexEntry*)f->assigned_decoder;

	if(index && theCache)
	{
		index_subsys_read_lock();

		/* there is a list of indexes which may
		 * provide an answer for this filter, we
		 * need to invoke the first one that matches
		 * the namespace requested
		 */
		for(; index; index = index->list.pNext)
		{
			/* check namespace */
			if(slapi_sdn_compare(index->namespace_dn, namespace_dn))
			{
				/* wrong namespace */
				continue;
			}

			/* execute the search */
			if(index->lookup_func)
			{
				ret = (index->lookup_func)(f, out, index->user_data);
				break;
			}
		}

		index_subsys_unlock();
	}

	return ret;
}


/* slapi_index_register_decoder
 * ----------------------------
 * This allows a decoder to register itself,
 * it also builds the initial cache when first
 * called.  Note, there is no way to deregister
 * once registered - this allows a lock free cache
 * at the expense of a restart to clear old
 * indexes, this shouldnt be a problem since it is
 * not expected that indexes will be removed
 * often.
 */
int slapi_index_register_decoder(char *plugin_id, index_validate_callback validate_op)
{
	static int firstTime = 1;
	static int gotIDLapi = 0;
	int ret = 0;
	indexPlugin *plg;

	if(firstTime)
	{
		/* create the cache */
		theCache = (globalIndexCache*)slapi_ch_malloc(sizeof(globalIndexCache));
		if(theCache)
		{
			theCache->pPlugins = 0;
			theCache->ppIndexIndex = 0;
			theCache->index_count = 0;
			theCache->cache_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, "Index Plugins");;
			firstTime = 0;

			if(!gotIDLapi)
			{
				if(slapi_apib_get_interface(IDL_v1_0_GUID, &idl_api))
				{
					gotIDLapi = 1;
				}
			}
		}
		else
		{
			ret = -1;
			goto bail;
		}
	}

	index_subsys_write_lock();

	/* add the index decoder to the cache - no checks, better register once only*/
	plg = (indexPlugin*)slapi_ch_malloc(sizeof(indexPlugin));
	if(plg)
	{
		plg->id = slapi_ch_strdup(plugin_id);
		plg->indexes = 0;
		plg->validate_op = validate_op;

		/* we always add to the start of the linked list */
		plg->list.pPrev = 0;

		if(theCache->pPlugins)
		{
			plg->list.pNext = theCache->pPlugins;
			theCache->pPlugins->list.pPrev = plg;
		}
		else
			plg->list.pNext = 0;


		theCache->pPlugins = plg;
	}
	else
		ret = -1;

	index_subsys_unlock();

bail:
	return ret;
}


/* slapi_index_register_index
 * --------------------------
 * a plugin that has registered itself may
 * then proceed to register individual indexes
 * that it looks after.  This function adds
 * the indexes to the plugin cache.
 */
int slapi_index_register_index(char *plugin_id, indexed_item *registration_item, void *user_data)
{
	int ret = 0;
	indexPlugin *plg;
	indexEntry *index;
	int a_matched_index = 0;
	Slapi_Filter *tmp_f = slapi_str2filter(registration_item->index_filter);
	int i = 0;
	Slapi_Backend *be;
	
	if(!theCache)
		return -1;

	index_subsys_write_lock();

	/* first lets find the plugin */
	plg = theCache->pPlugins;

	while(plg)
	{
		if(!slapi_UTF8CASECMP(plugin_id, plg->id))
		{
			/* found plugin */
			break;
		}

		plg = plg->list.pNext;
	}

	if(0 == plg)
	{
		/* not found */
		ret = -1;
		goto bail;
	}

	/* now add the new index - we shall assume indexes 
	 * will not be registered twice by different plugins,
	 * in that event, the last one added wins
	 * the first matching index in the list is always
	 * the current one, other matching indexes are ignored
	 * therefore reregistering an index with NULL
	 * callbacks disables the index for that plugin
	 */

	/* find the index if already registered */

	index = plg->indexes;

	while(index)
	{
		if(index_subsys_index_matches_filter(index, tmp_f))
		{
			/* found it - free what is currently there, it will be replaced */
			slapi_ch_free((void**)&index->indexfilterstr);
			slapi_filter_free(index->indexfilter, 1);
			slapi_ch_free((void**)&index->indexedAttribute);
			
			charray_free( index->associated_attrs );
			index->associated_attrs = 0;

			a_matched_index = 1;
			break;
		}

		index = index->list.pNext;
	}

	if(!index)
		index = (indexEntry*)slapi_ch_calloc(1,sizeof(indexEntry));

	index->indexfilterstr = slapi_ch_strdup(registration_item->index_filter);
	index->indexfilter = tmp_f;
	index->lookup_func = registration_item->search_op;
	index->user_data = user_data;

	/* map the namespace dn to a backend dn */
	be = slapi_be_select( registration_item->namespace_dn );
	
	if(be == defbackend_get_backend())
	{
		ret = -1;
		goto bail;
	}

	index->namespace_dn = (Slapi_DN*)slapi_be_getsuffix(be, 0);
	
	/* for now, we support simple filters only */
	index->indexedAttribute = slapi_ch_strdup(index->indexfilter->f_type);

	/* add associated attributes */
	if(registration_item->associated_attrs)
	{
		index->associated_attrs =
			cool_charray_dup( registration_item->associated_attrs );
	}

	if(!a_matched_index)
	{
		if(plg->indexes)
		{
			index->list.pNext = plg->indexes;
			plg->indexes->list.pPrev = plg;
		}
		else
			index->list.pNext = 0;

		index->list.pPrev = 0;

		plg->indexes = index;

		theCache->index_count++;
	}

	/* now we need to rebuild the index (onto the indexed items)
	 * this is a bit inefficient since
	 * every new index that is added triggers
	 * an index rebuild - but this is countered
	 * by the fact this will probably happen once
	 * at start up most of the time, and very rarely
	 * otherwise, so normal server performance should
	 * not be unduly effected effected
	 * we take care to build the index and only then swap it in
	 * for the old one
	 * PARPAR: we need to RW (or maybe a ref count) lock here
	 * only alternative would be to not have an index :(
	 * for few plugins with few indexes thats a possibility
	 * traditionally many indexes have not been a good idea
	 * anyway so...
	 */
	
/*	indexIndex = (indexEntry**)slapi_ch_malloc(sizeof(indexEntry*) * (theCache->index_count+1));
*/
	/* for now, lets see how fast things are without an index
	 * that should not be a problem at all to begin with since
	 * presence will be the only index decoder.  Additionally,
	 * adding an index means we need locks - um, no.
	 * so no more to do
	 */

bail:
	index_subsys_unlock();

	return ret;
}

/* index_subsys_index_matches_index
 * --------------------------------
 * criteria for a match is that the types
 * are the same and that all the associated
 * attributes that are configured for cmp_to
 * are also in cmp_with.
 */
int index_subsys_index_matches_index(indexEntry *cmp_to, indexEntry *cmp_with)
{
	int ret = 0;

	if(slapi_attr_types_equivalent(cmp_to->indexedAttribute, cmp_with->indexedAttribute))
	{
		/* now check associated */
		if(cmp_to->associated_attrs)
		{
			if(cmp_with->associated_attrs)
			{
				int x,y;

				ret = 1;

				for(x=0; cmp_to->associated_attrs[x]  && ret == 1; x++)
				{
					ret = 0;

					for(y=0; cmp_with->associated_attrs[y]; y++)
					{
						if(slapi_attr_types_equivalent(
							cmp_to->associated_attrs[x], 
							cmp_with->associated_attrs[y]
							))
						{
							/* matched on associated attribute */
							ret = 1;
							break;
						}
					}
				}
			}
		}
		else
		{
			/* no associated is an auto match */
			ret = 1;
		}

	}

	return ret;
}

indexEntry *index_subsys_index_shallow_dup(indexEntry *dup_this)
{
	indexEntry *ret = (indexEntry *)slapi_ch_calloc(1,sizeof(indexEntry));

	/* shallow copy - dont copy linked list pointers */
	ret->indexedAttribute = dup_this->indexedAttribute;
	ret->indexfilter = dup_this->indexfilter;
	ret->indexfilterstr = dup_this->indexfilterstr;
	ret->user_data = dup_this->user_data;
	ret->namespace_dn = dup_this->namespace_dn;
    ret->lookup_func = dup_this->lookup_func;
	ret->associated_attrs = dup_this->associated_attrs;

	return ret;
}

/* index_subsys_assign_decoder
 * ---------------------------
 * finds a decoder which will service
 * the filter if one is available and assigns
 * the decoder to the filter.  Currently this
 * function supports only simple filters, but
 * may in the future support complex filter
 * assignment (possibly including filter rewriting
 * to make more matches possible)
 *
 * populates f->alternate_decoders if more than one
 * index could deal with a filter - only filters that
 * have a match with all associated attributes of the
 * first found filter are said to match - their
 * namespaces may be different
 */
static int index_subsys_assign_decoder(Slapi_Filter *f)
{
	int ret = 0; /* always succeed */
	indexPlugin *plg;
	indexEntry *index = 0;
	indexEntry *last = 0;
		
	f->assigned_decoder = 0;

	if(!theCache)
		return 0;

	index_subsys_read_lock();

	plg = theCache->pPlugins;

	while(plg)
	{
		index = plg->indexes;

		while(index)
		{
			if(INDEX_MATCH_NONE != index_subsys_index_matches_filter(index, f))
			{
				/* we have a match, assign this decoder if not disabled */
				if(index->lookup_func)
				{
					if(!f->assigned_decoder)
					{
						f->assigned_decoder = index_subsys_index_shallow_dup(index);
						last = f->assigned_decoder;
					}
					else
					{
						/* add to alternate list - we require that they all
						 * have the same associated attributes configuration for now
						 * though they may have different namespaces
						 */
						if(index_subsys_index_matches_index(f->assigned_decoder, index))
						{
							/* add to end */
							last->list.pNext = index_subsys_index_shallow_dup(index);
							last = last->list.pNext;
						}
					}
				}
				else
				{
					/* index disabled, so we must allow another plugin to
					 * get a crack at servicing the index
					 */
					break;
				}
			}

			index = index->list.pNext;
		}

		plg = plg->list.pNext;
	}

	index_subsys_unlock();

	return ret;
}

