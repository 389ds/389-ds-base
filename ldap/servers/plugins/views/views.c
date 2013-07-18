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

/* plugin which implements directory server views */

#include <stdio.h>
#include <string.h>
#include "portable.h"
#include "slapi-plugin.h"
#include "statechange.h"
#include "views.h"

#include "slapi-plugin-compat4.h"
#include "slapi-private.h"


#define VIEW_OBJECTCLASS "nsView"
#define VIEW_FILTER_ATTR	"nsViewFilter"
#define STATECHANGE_VIEWS_ID "Views"
#define STATECHANGE_VIEWS_CONFG_FILTER "objectclass=" VIEW_OBJECTCLASS

/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif

#define VIEWS_PLUGIN_SUBSYSTEM   "views-plugin"   /* used for logging */

/* cache data structs */

struct _viewLinkedList
{
	void *pNext;
	void *pPrev;
};
typedef struct _viewLinkedList viewLinkedList;

#if defined(DEBUG)
#define _VIEW_DEBUG_FILTERS  /* Turning on hurts performance */
#endif

struct _viewEntry
{
	viewLinkedList list;
	char *pDn;
	char *viewfilter; /* the raw view */
	Slapi_Filter *includeAncestorFiltersFilter; /* the filter with all ancestor filters */
	Slapi_Filter *excludeAllButDescendentViewsFilter; /* for building the view of views */
	Slapi_Filter *excludeChildFiltersFilter; /* NOT all children views, for one level searches */
	Slapi_Filter *excludeGrandChildViewsFilter; /* view filter for one level searches */
	Slapi_Filter *includeChildViewsFilter; /* view filter for subtree searches */
#ifdef _VIEW_DEBUG_FILTERS
	/* monitor the cached filters with these */
	char includeAncestorFiltersFilter_str[1024]; /* the filter with all ancestor filters */
	char excludeAllButDescendentViewsFilter_str[1024]; /* for building the view of views */
	char excludeChildFiltersFilter_str[1024]; /* NOT all children views, for one level searches */
	char excludeGrandChildViewsFilter_str[1024]; /* view filter for one level searches */
	char includeChildViewsFilter_str[1024]; /* view filter for subtree searches */
#endif
	char *pSearch_base; /* the parent of the top most view */
	void *pParent;
	void **pChildren;
	int child_count;
	unsigned long entryid;  /* unique identifier for this entry */
	unsigned long parentid; /* unique identifier for the parent entry */
};
typedef struct _viewEntry viewEntry;

struct _globalViewCache
{
	viewEntry *pCacheViews;
	viewEntry **ppViewIndex;
	int cache_built;
	int view_count;
	PRThread *currentUpdaterThread;
};
typedef struct _globalViewCache golbalViewCache;

static golbalViewCache theCache;

/* other function prototypes */
int views_init( Slapi_PBlock *pb ); 
static int views_start( Slapi_PBlock *pb );
static int views_close( Slapi_PBlock *pb );
static int views_cache_create();
static void views_update_views_cache( Slapi_Entry *e, char *dn, int modtype, Slapi_PBlock *pb, void *caller_data );
static int views_cache_build_view_list(viewEntry **pViews);
static int views_cache_index();
static int 	views_dn_views_cb (Slapi_Entry* e, void *callback_data);
static int views_cache_add_dn_views(char *dn, viewEntry **pViews);
static void views_cache_add_ll_entry(void** attrval, void *theVal);
static void views_cache_discover_parent(viewEntry *pView);
static void views_cache_discover_parent_for_children(viewEntry *pView);
static void views_cache_discover_children(viewEntry *pView);
static void views_cache_discover_view_scope(viewEntry *pView);
static void views_cache_create_applied_filter(viewEntry *pView);
static void views_cache_create_exclusion_filter(viewEntry *pView);
static void views_cache_create_inclusion_filter(viewEntry *pView);
Slapi_Filter *views_cache_create_descendent_filter(viewEntry *ancestor, PRBool useID);
static int view_search_rewrite_callback(Slapi_PBlock *pb);
static void views_cache_backend_state_change(void *handle, char *be_name, int old_be_state, int new_be_state); 
static void views_cache_act_on_change_thread(void *arg);
static viewEntry *views_cache_find_view(char *view);

/* our api broker published api */
static void *api[3];
static int _internal_api_views_entry_exists(char *view_dn, Slapi_Entry *e);
static int _internal_api_views_entry_dn_exists(char *view_dn, char *e_dn);
static int _internal_api_views_entry_exists_general(char *view_dn, Slapi_Entry *e, char *e_dn);


static Slapi_PluginDesc pdesc = { "views", VENDOR, DS_PACKAGE_VERSION,
	"virtual directory information tree views plugin" };

static void * view_plugin_identity = NULL;

static Slapi_RWLock *g_views_cache_lock;

#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

/*
** Plugin identity mgmt
*/

void view_set_plugin_identity(void * identity) 
{
	view_plugin_identity=identity;
}

void * view_get_plugin_identity()
{
	return view_plugin_identity;
}

/* 
	views_init
	--------
	adds our callbacks to the list
*/
int views_init( Slapi_PBlock *pb )
{
	int ret = SLAPI_PLUGIN_SUCCESS;
	void * plugin_identity=NULL;

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "--> views_init\n");

	/*
	** Store the plugin identity for later use.
	** Used for internal operations
	*/
	
    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
	view_set_plugin_identity(plugin_identity);

	if (	slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    			SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
        	         (void *) views_start ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
        	         (void *) views_close ) != 0 ||
			slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                     (void *)&pdesc ) != 0 )
    {
        slapi_log_error( SLAPI_LOG_FATAL, VIEWS_PLUGIN_SUBSYSTEM,
                         "views_init: failed to register plugin\n" );
		ret = SLAPI_PLUGIN_FAILURE;
    }

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "<-- views_init\n");
    return ret;
}

void views_read_lock()
{
	slapi_rwlock_rdlock(g_views_cache_lock);
}

void views_write_lock()
{
	slapi_rwlock_wrlock(g_views_cache_lock);
}

void views_unlock()
{
	slapi_rwlock_unlock(g_views_cache_lock);
}

/*
	views_start
	---------
	This function publishes the interface for this plugin
*/
static int views_start( Slapi_PBlock *pb )
{
	int ret = SLAPI_PLUGIN_SUCCESS;
	void **statechange_api;

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "--> views_start\n");

	theCache.cache_built = 0;
	g_views_cache_lock = slapi_new_rwlock();

	/* first register our backend state change func (we'll use func pointer as handle) */
	slapi_register_backend_state_change((void *)views_cache_backend_state_change, views_cache_backend_state_change); 

	/* create the view cache */

	views_cache_create();

	/* register callbacks for filter and search rewriting */
	slapi_compute_add_search_rewriter(view_search_rewrite_callback);

	/* register for state changes to view configuration */
    if(!slapi_apib_get_interface(StateChange_v1_0_GUID, &statechange_api))
    {
        statechange_register(statechange_api, STATECHANGE_VIEWS_ID, NULL, STATECHANGE_VIEWS_CONFG_FILTER, NULL, views_update_views_cache);
    }

	/* register our api so that other subsystems can be views aware */
	api[0] = NULL; /* reserved for api broker use */
	api[1] = (void *)_internal_api_views_entry_exists;
	api[2] = (void *)_internal_api_views_entry_dn_exists;

	if( slapi_apib_register(Views_v1_0_GUID, api) )
	{
		slapi_log_error( SLAPI_LOG_FATAL, VIEWS_PLUGIN_SUBSYSTEM, "views: failed to publish views interface\n");
		ret = SLAPI_PLUGIN_FAILURE;
	}

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "<-- views_start\n");
	return ret;
}

/* _internal_api_views_entry_exists()
 * ----------------------------------
 * externally published api to allow other subsystems to
 * be views aware.  Given a view and an entry, this function
 * returns PR_TRUE if the entry would be returned by a subtree
 * search on the view, PR_FALSE otherwise.
 */
static int _internal_api_views_entry_exists(char *view_dn, Slapi_Entry *e)
{
	return _internal_api_views_entry_exists_general(view_dn, e, NULL);
}

static int _internal_api_views_entry_dn_exists(char *view_dn, char *e_dn)
{
	return _internal_api_views_entry_exists_general(view_dn, NULL, e_dn);
}

static int _internal_api_views_entry_exists_general(char *view_dn, Slapi_Entry *e, char *e_dn)
{
	int ret = SLAPI_PLUGIN_SUCCESS;
	viewEntry *view;
	char *dn;

	/* there are two levels of scope for a view,
	 * from the parent of the view without a view filter
	 * and the parent of the top most view including a
	 * view filter - either match will do
	 */

	/* Read lock the cache */
	views_read_lock();

	/* find the view */
	view = views_cache_find_view(view_dn);
	if(0==view)
	{
		/* this is not the entry you are looking for */
		goto bail;
	}

	/* normal scope - is the view an ancestor of the entry */
	if(e_dn)
		dn = e_dn;
	else
		dn = slapi_entry_get_ndn(e);

	if(slapi_dn_issuffix(dn, view_dn))
	{
		/* this entry is physically contained in the view hiearchy */
		ret = SLAPI_PLUGIN_FAILURE;
		goto bail;
	}

	/* view scope - view hiearchy scope plus view filter */
	if(slapi_dn_issuffix(dn, view->pSearch_base))
	{
		if(0==e)
		{
			Slapi_DN *sdn = slapi_sdn_new_dn_byref(dn);

			slapi_search_internal_get_entry( sdn, NULL, &e , view_get_plugin_identity());

			slapi_sdn_free(&sdn);
		}

		/* so far so good, apply filter */
		if(0==slapi_filter_test_simple(e,view->includeAncestorFiltersFilter))
		{
			/* this entry would appear in the view */
			ret = SLAPI_PLUGIN_FAILURE;
		}
	}

bail:
	views_unlock();
	return ret;
}

void views_cache_free()
{
	viewEntry *head = theCache.pCacheViews;
	viewEntry *current;

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "--> views_cache_free\n");

	/* free the cache */
	current = head; 
	
	while(current != NULL)
	{
		viewEntry *theView = current;
		current = current->list.pNext;

		/* free the view */
		slapi_ch_free((void**)&theView->pDn);
		slapi_ch_free((void**)&theView->viewfilter);
		slapi_filter_free(theView->includeAncestorFiltersFilter,1); 
		slapi_filter_free(theView->excludeAllButDescendentViewsFilter,1);
		slapi_filter_free(theView->excludeChildFiltersFilter,1);
		slapi_filter_free(theView->excludeGrandChildViewsFilter,1);
		slapi_filter_free(theView->includeChildViewsFilter,1);
		slapi_ch_free((void**)&theView->pSearch_base);
		slapi_ch_free((void**)&theView->pChildren);
		slapi_ch_free((void**)&theView);
	}

	theCache.pCacheViews = NULL;
	
	slapi_ch_free((void**)&theCache.ppViewIndex);
	
	theCache.view_count = 0;

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "<-- views_cache_free\n");
}

/*
	views_close
	---------
	unregisters the interface for this plugin
*/
static int views_close( Slapi_PBlock *pb )
{
	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "--> views_close\n");
	
	/* unregister backend state change notification */
	slapi_unregister_backend_state_change((void *)views_cache_backend_state_change);

	views_cache_free();

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "<-- views_close\n");

	return SLAPI_PLUGIN_SUCCESS;
}


/*
	views_cache_create
	---------------------
	Walks the views in the DIT and populates the cache.
*/
static int views_cache_create()
{
	int ret = SLAPI_PLUGIN_FAILURE;

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "--> views_cache_create\n");


	/* lock cache */
	views_write_lock();

	theCache.currentUpdaterThread = PR_GetCurrentThread(); /* to avoid deadlock */

	if(theCache.pCacheViews)
	{
		/* need to get rid of the existing views */
		views_cache_free();
	}

	/* grab the view entries */
	ret = views_cache_build_view_list(&(theCache.pCacheViews));
	if(!ret && theCache.pCacheViews)
	{
		viewEntry *head = theCache.pCacheViews;
		viewEntry *current;
		
		/* OK, we have a basic cache, now we need to
		 * fix up parent and children pointers
		 */
		for(current = head; current != NULL; current = current->list.pNext)
		{
			views_cache_discover_parent(current);
			views_cache_discover_children(current);
		}

		/* scope of views and cache search filters... */
		for(current = head; current != NULL; current = current->list.pNext)
		{
			views_cache_discover_view_scope(current);
			views_cache_create_applied_filter(current);
			views_cache_create_exclusion_filter(current);
			views_cache_create_inclusion_filter(current);
		}

		/* create the view index */
		ret = views_cache_index();
		if(ret != 0)
		{
			/* currently we cannot go on without the indexes */
			slapi_log_error(SLAPI_LOG_FATAL, VIEWS_PLUGIN_SUBSYSTEM, "views_cache_create: failed to index cache\n");			
		}
		else
			theCache.cache_built = 1;
	}
	else
	{
		/* its ok to not have views to cache */
		theCache.cache_built = 0;
		ret = SLAPI_PLUGIN_SUCCESS;
	}

	theCache.currentUpdaterThread = 0;

	/* unlock cache */
	views_unlock();

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "<-- views_cache_create\n");
	return ret;
}

/*
 * views_cache_view_compare
 * -----------------------
 * compares the dns of two views - used for sorting the index
 */
int views_cache_view_compare(const void *e1, const void *e2)
{
	int ret;
	Slapi_DN *dn1 = slapi_sdn_new_dn_byval((*(viewEntry**)e1)->pDn);
	Slapi_DN *dn2 = slapi_sdn_new_dn_byval((*(viewEntry**)e2)->pDn);

	ret = slapi_sdn_compare(dn1, dn2);

	slapi_sdn_free(&dn1);
	slapi_sdn_free(&dn2);

	return ret;
}

/*
 * views_cache_dn_compare
 * -----------------------
 * compares a dn with the dn of a view - used for searching the index
 */
int views_cache_dn_compare(const void *e1, const void *e2)
{
	int ret;
	Slapi_DN *dn1 = slapi_sdn_new_dn_byval((char*)e1);
	Slapi_DN *dn2 = slapi_sdn_new_dn_byval(((viewEntry*)e2)->pDn);

	ret = slapi_sdn_compare(dn1, dn2);

	slapi_sdn_free(&dn1);
	slapi_sdn_free(&dn2);

	return ret;
}
/*
 * views_cache_index
 * ----------------
 * indexes the cache for fast look up of views
 */
static int views_cache_index()
{
	int ret = SLAPI_PLUGIN_FAILURE;
	int i;
	viewEntry *theView = theCache.pCacheViews;
	viewEntry *current = 0;

	if(theCache.ppViewIndex)
		slapi_ch_free((void**)&theCache.ppViewIndex);

	theCache.view_count = 0;

	/* lets count the views */
	for(current = theCache.pCacheViews; current != NULL; current = current->list.pNext)
		theCache.view_count++;
	

	theCache.ppViewIndex = (viewEntry**)slapi_ch_calloc(theCache.view_count, sizeof(viewEntry*));
	if(theCache.ppViewIndex)
	{
		/* copy over the views */
		for(i=0; i<theCache.view_count; i++)
		{
			theCache.ppViewIndex[i] = theView;
			theView = theView->list.pNext;
		}

		/* sort the views */
		qsort(theCache.ppViewIndex, theCache.view_count, sizeof(viewEntry*), views_cache_view_compare);

		ret = SLAPI_PLUGIN_SUCCESS;
	}

	return ret;
}


/*
	views_cache_view_index_bsearch - RECURSIVE
	----------------------------------------
	performs a binary search on the cache view index
	return SLAPI_PLUGIN_FAILURE if key is not found
*/
viewEntry *views_cache_view_index_bsearch( const char *key, int lower, int upper )
{
	viewEntry *ret = 0;
	int index = 0;
	int compare_ret = 0;

	if(upper >= lower)
	{
		if(upper != 0)
			index = ((upper-lower)/2) + lower;
		else
			index = 0;

		compare_ret = views_cache_dn_compare(key, theCache.ppViewIndex[index]);
		
		if(!compare_ret)
		{
			ret = (theCache.ppViewIndex)[index];
		}
		else
		{
			/* seek elsewhere */
			if(compare_ret < 0)
			{
				/* take the low road */
				ret = views_cache_view_index_bsearch(key, lower, index-1);
			}
			else
			{
				/* go high */
				ret = views_cache_view_index_bsearch(key, index+1, upper);
			}
		}
	}

	return ret;
}


/*
	views_cache_find_view
	-------------------
	searches for a view, and if found returns it, null otherwise
*/
static viewEntry *views_cache_find_view(char *view)
{
	viewEntry *ret = SLAPI_PLUGIN_SUCCESS;  /* assume failure */

	if(theCache.view_count != 1)
		ret = views_cache_view_index_bsearch(view, 0, theCache.view_count-1);
	else
	{
		/* only one view (that will fool our bsearch) lets check it here */
		if(!slapi_utf8casecmp((unsigned char*)view, (unsigned char*)theCache.ppViewIndex[0]->pDn))
		{
			ret = theCache.ppViewIndex[0];
		}
	}

	return ret;
}


/*
	views_cache_discover_parent
	------------------------------
	finds the parent of this view and caches it in view
*/
static void views_cache_discover_parent(viewEntry *pView)
{
	viewEntry *head = theCache.pCacheViews;
	viewEntry *current;
	int found = 0;
	
	for(current = head; current != NULL  && !found; current = current->list.pNext)
	{
		if(slapi_dn_isparent( current->pDn, pView->pDn ))
		{
			found = 1;
			pView->pParent = current;
		}
	}

	if(!found)
	{
		/* this is a top view */
		pView->pParent = NULL;
	}
}

/*
	views_cache_discover_parent_for_children
	------------------------------
	The current node is being deleted - for each child, need
	to find a new parent
*/
static void views_cache_discover_parent_for_children(viewEntry *pView)
{
	int ii = 0;
	
	for (ii = 0; pView->pChildren && (ii < pView->child_count); ++ii)
	{
		viewEntry *current = (viewEntry *)pView->pChildren[ii];
		views_cache_discover_parent(current);
	}
}

/*
	views_cache_discover_children
	------------------------------
	finds the children of this view and caches them in view
*/
static void views_cache_discover_children(viewEntry *pView)
{
	viewEntry *head = theCache.pCacheViews;
	viewEntry *current;
	int child_count = 0;
	int add_count = 0;

	if(pView->pChildren)
	{
		slapi_ch_free((void**)&pView->pChildren);
		pView->pChildren = NULL;
	}

	/* first lets count the children */
	for(current = head; current != NULL; current = current->list.pNext)
	{
		if(slapi_dn_isparent(pView->pDn, current->pDn))
			child_count++;
	}

	/* make the space for them */
	pView->child_count = child_count;

	if (child_count > 0)
	{
		pView->pChildren = (void **)slapi_ch_calloc(child_count, sizeof(viewEntry*));

		/* add them */
		for(current = head; current != NULL; current = current->list.pNext)
		{
			if(slapi_dn_isparent(pView->pDn, current->pDn))
			{
				((viewEntry**)pView->pChildren)[add_count] = current;
				add_count++;
			}
		}
	}
}


/*
	views_cache_discover_view_scope
	------------------------------
	finds the parent of the top most view and sets the scope of the view search
*/

static void views_cache_discover_view_scope(viewEntry *pView)
{
	viewEntry *current = pView;
	
	if(pView->pSearch_base)
		slapi_ch_free((void**)&pView->pSearch_base);

	while(current != NULL)
	{
		if(current->pParent == NULL)
		{
			/* found top */
			pView->pSearch_base = slapi_dn_parent(current->pDn);
		}

		current = current->pParent; 
	}

}


/*
	views_cache_create_applied_filter
	--------------------------------
	builds the filters for:
	char *includeAncestorFiltersFilter;  the view with all ancestor views 
*/
static void views_cache_create_applied_filter(viewEntry *pView)
{
	viewEntry *current = pView;
	Slapi_Filter *pCurrentFilter = 0;
	Slapi_Filter *pBuiltFilter = 0;
	Slapi_Filter *pViewEntryExcludeFilter = 0;
    char *excludeFilter;

	if(pView->includeAncestorFiltersFilter)
	{
		/* release the current filter */
		slapi_filter_free(pView->includeAncestorFiltersFilter, 1);
		pView->includeAncestorFiltersFilter = 0;
	}

	/* create applied view filter (this view filter plus ancestors) */
	while(current != NULL)
	{
		/* add this view filter to the built filter using AND */
		char *buf;
			
		if(!current->viewfilter)
		{
			current = current->pParent; 
			continue; /* skip this view */
		}

		buf = slapi_ch_strdup(current->viewfilter);

		pCurrentFilter = slapi_str2filter( buf );
		if (!pCurrentFilter) {
			slapi_log_error(SLAPI_LOG_FATAL, VIEWS_PLUGIN_SUBSYSTEM,
							"Error: the view filter [%s] in entry [%s] is not valid\n",
							buf, current->pDn);
		}
		if(pBuiltFilter && pCurrentFilter)
			pBuiltFilter = slapi_filter_join_ex( LDAP_FILTER_AND, pBuiltFilter, pCurrentFilter, 0 );
		else
			pBuiltFilter = pCurrentFilter;

		slapi_ch_free((void **)&buf);

		current = current->pParent; 
	}

	/* filter for removing view entries from search */
    /* richm - slapi_str2filter _writes_ to it's argument, so we have to pass in 
       some writeable memory, or core dump, do not pass go */
    excludeFilter = slapi_ch_strdup("(!(objectclass=" VIEW_OBJECTCLASS "))");
	pViewEntryExcludeFilter = slapi_str2filter( excludeFilter );
    slapi_ch_free_string(&excludeFilter);

	if(pBuiltFilter)
		pView->includeAncestorFiltersFilter = slapi_filter_join_ex( LDAP_FILTER_AND, pBuiltFilter, pViewEntryExcludeFilter, 0 );
	else
		pView->includeAncestorFiltersFilter = pViewEntryExcludeFilter;

#ifdef _VIEW_DEBUG_FILTERS
	slapi_filter_to_string(pView->includeAncestorFiltersFilter, pView->includeAncestorFiltersFilter_str, sizeof(pView->includeAncestorFiltersFilter_str));
#endif
}

/* views_cache_create_exclusion_filter
 * ----------------------------------
 * makes a filter which is used for one level searches
 * so that views show up correctly if the client filter
 * allows: excludeGrandChildViewsFilter
 *
 * Also makes the filter which excludes entries which
 * belong in descendent views: excludeChildFiltersFilter 
 */
static void views_cache_create_exclusion_filter(viewEntry *pView)
{
/*
	viewEntry *current = pView;
	Slapi_Filter *pOrSubFilter = 0;
	int child_count = 0;
*/
	Slapi_Filter *excludeChildFiltersFilter = 0;
	char *buf = 0;

	/* create exclusion filter for one level searches
	 * this requires the rdns of the grandchildren of
	 * this view to be in a filter
	 */

	if(pView->excludeGrandChildViewsFilter)
	{
		/* release the current filter */
		slapi_filter_free(pView->excludeGrandChildViewsFilter, 1);
		pView->excludeGrandChildViewsFilter = 0;
	}

	if(pView->excludeChildFiltersFilter)
	{
		/* release the current filter */
		slapi_filter_free(pView->excludeChildFiltersFilter, 1);
		pView->excludeChildFiltersFilter = 0;
	}

/*	if(pView->child_count == 0)
	{
*/		/* this view has no children */
/*		pView->excludeGrandChildViewsFilter = 0;
		pView->excludeChildFiltersFilter = 0;
		return;
	}


	while(child_count < pView->child_count)
	{
		current = pView->pChildren[child_count];

		if(current->child_count == 0)
		{
*/			/* no grandchildren here, skip */
/*			child_count++;
			continue;
		}
*/
		/* for each child we need to add its descendants */
/*		if(pOrSubFilter)
		{
			Slapi_Filter *pDescendents = views_cache_create_descendent_filter(current, TRUE);
			if(pDescendents)
				pOrSubFilter = slapi_filter_join_ex( LDAP_FILTER_OR, pOrSubFilter, pDescendents, 0 );
		}
		else
			pOrSubFilter = views_cache_create_descendent_filter(current, TRUE);
		
		child_count++;
	}
*/
	buf=PR_smprintf("(parentid=%lu)", pView->entryid);
	pView->excludeGrandChildViewsFilter = slapi_str2filter( buf );
	PR_smprintf_free(buf);

/*	if(pOrSubFilter)
		pView->excludeGrandChildViewsFilter = slapi_filter_join_ex( LDAP_FILTER_NOT, pOrSubFilter, NULL, 0 );*/

	excludeChildFiltersFilter = views_cache_create_descendent_filter(pView, PR_FALSE);
	if(excludeChildFiltersFilter)
		pView->excludeChildFiltersFilter = slapi_filter_join_ex( LDAP_FILTER_NOT, excludeChildFiltersFilter, NULL, 0 );

#ifdef _VIEW_DEBUG_FILTERS
	slapi_filter_to_string(pView->excludeGrandChildViewsFilter, pView->excludeGrandChildViewsFilter_str, sizeof(pView->excludeGrandChildViewsFilter_str));
	slapi_filter_to_string(pView->excludeChildFiltersFilter, pView->excludeChildFiltersFilter_str, sizeof(pView->excludeChildFiltersFilter_str));
#endif
}


Slapi_Filter *views_cache_create_descendent_filter(viewEntry *ancestor, PRBool useEntryID)
{
	int child_count = 0;
	Slapi_Filter *pOrSubFilter = 0;

	while(child_count < ancestor->child_count)
	{
		Slapi_Filter *pDescendentSubFilter = 0;
/*
		Slapi_RDN *rdn = 0;
		char *str_rdn = 0;
		int len = 0;
*/
		Slapi_Filter *pCurrentFilter = 0;
		viewEntry *currentChild = ancestor->pChildren[child_count];
		char *buf = 0;

		/* for each child we need to add its descendants
		 * we do this now before processing this view
		 * to try to help the filter code out by having
		 * the most significant filters first
		 */
		pDescendentSubFilter = views_cache_create_descendent_filter(currentChild, useEntryID);
		if(pDescendentSubFilter)
		{
			if(pOrSubFilter)
				pOrSubFilter = slapi_filter_join_ex( LDAP_FILTER_OR, pOrSubFilter, pDescendentSubFilter, 0 );
			else
				pOrSubFilter = pDescendentSubFilter;
		}

		if(useEntryID)
		{
			/* we need the RDN of this child */
/*			rdn = slapi_rdn_new_dn(currentChild->pDn);
			str_rdn = (char *)slapi_rdn_get_rdn(rdn);
			len = strlen(str_rdn);
			
			buf=PR_smprintf("(%s)", str_rdn);*/

			/* uniquely identify this child */
			buf=PR_smprintf("(parentid=%lu)", currentChild->entryid);
		}
		else
		{
			/* this is a filter based filter */
			if(currentChild->viewfilter)
			{
				buf=PR_smprintf("%s",currentChild->viewfilter);
			}
		}

		if(buf)
		{
			pCurrentFilter = slapi_str2filter( buf );
			if (!pCurrentFilter) {
				slapi_log_error(SLAPI_LOG_FATAL, VIEWS_PLUGIN_SUBSYSTEM,
								"Error: the view filter [%s] in entry [%s] is not valid\n",
								buf, currentChild->pDn);
			}
			if(pOrSubFilter && pCurrentFilter)
				pOrSubFilter = slapi_filter_join_ex( LDAP_FILTER_OR, pOrSubFilter, pCurrentFilter, 0 );
			else
				pOrSubFilter = pCurrentFilter;

			PR_smprintf_free(buf);
		}


		child_count++;
	}

	return pOrSubFilter;
}


/* views_cache_create_inclusion_filter
 * ----------------------------------
 * makes a filter which is used for subtree searches
 * so that views show up correctly if the client filter
 * allows
 */
static void views_cache_create_inclusion_filter(viewEntry *pView)
{
#if 0
	viewEntry *head = theCache.pCacheViews;
#endif
/*	viewEntry *current; */
/*	Slapi_Filter *view_filter; */
	char *view_filter_str;

	if(pView->includeChildViewsFilter)
	{
		/* release the current filter */
		slapi_filter_free(pView->includeChildViewsFilter, 1);
		pView->includeChildViewsFilter = 0;
	}
#if 0
	for(current = head; current != NULL; current = current->list.pNext)
	{
		Slapi_DN *viewDN;
		Slapi_RDN *viewRDN;
		char *viewRDNstr;
		char *buf = 0;
		Slapi_Filter *viewSubFilter;

		/* if this is this a descendent, ignore it */
		if(slapi_dn_issuffix(current->pDn,pView->pDn) && !(current == pView))
			continue;

		viewDN = slapi_sdn_new_dn_byref(current->pDn);
		viewRDN = slapi_rdn_new();

		slapi_sdn_get_rdn(viewDN,viewRDN);
		viewRDNstr = (char *)slapi_rdn_get_rdn(viewRDN);

		buf = slapi_ch_calloc(1, strlen(viewRDNstr) + 11 ); /* 3 for filter */
		sprintf(buf, "(%s)", viewRDNstr );
		viewSubFilter = slapi_str2filter( buf );
		if (!viewSubFilter) {
			slapi_log_error(SLAPI_LOG_FATAL, VIEWS_PLUGIN_SUBSYSTEM,
							"Error: the view filter [%s] in entry [%s] is not valid\n",
							buf, current->pDn);
		}

		if(pView->includeChildViewsFilter && viewSubFilter)
			pView->includeChildViewsFilter = slapi_filter_join_ex( LDAP_FILTER_OR, pView->includeChildViewsFilter, viewSubFilter, 0 );
		else
			pView->includeChildViewsFilter = viewSubFilter;

		slapi_ch_free((void **)&buf);
		slapi_sdn_free(&viewDN);
		slapi_rdn_free(&viewRDN);

		child_count++;
	}
#endif

	/* exclude all other view entries but decendents */
/*	pView->includeChildViewsFilter = slapi_filter_join_ex( LDAP_FILTER_NOT, pView->includeChildViewsFilter, NULL, 0 );
*/
	/* it seems reasonable to include entries which
	 * may not fit the view decription but which
	 * are actually *contained* in the view
	 * therefore we use parentids for the view
	 * filter
	 */


	/* add decendents */
	pView->includeChildViewsFilter = views_cache_create_descendent_filter(pView, PR_TRUE);

	/* add this view */
	view_filter_str = PR_smprintf("(|(parentid=%lu)(entryid=%lu))", pView->entryid, pView->entryid);

	if(pView->includeChildViewsFilter)
	{
		pView->includeChildViewsFilter = slapi_filter_join_ex( LDAP_FILTER_OR, slapi_str2filter( view_filter_str ), pView->includeChildViewsFilter, PR_FALSE);
	}
	else
	{
		pView->includeChildViewsFilter = slapi_str2filter( view_filter_str );
	}
	PR_smprintf_free(view_filter_str);
	view_filter_str = NULL;

	/* and make sure the this applies only to views */

/*	if(pView->includeChildViewsFilter)
	{*/
/*  Not necessary since we now use entryid in the filter,
	so all will be views anyway, and the less sub-filters
	the better
		view_filter_str = strdup("(objectclass=" VIEW_OBJECTCLASS ")");
		view_filter = slapi_str2filter( view_filter_str );
*/
		/* child views first because entryid indexed
		 * and makes evaluation faster when a bunch
		 * of indexed filter evaluations with only one
		 * target are evaluated first rather than an
		 * indexed filter which will provide many entries
		 * that may trigger an index evaluation short
		 * circuit.  i.e. if one of the child filters is
		 * true then we have one entry, if not, then we
		 * have used indexes completely to determine that
		 * no entry matches and (objectclass=nsview) is never
		 * evaluated.
		 * I should imagine this will hold for all but the
		 * very deepest, widest view trees when subtree
		 * searches are performed from the top
		 */
/*		pView->includeChildViewsFilter = slapi_filter_join_ex( LDAP_FILTER_AND, pView->includeChildViewsFilter, view_filter, 0 );
	}
	else
	{
		view_filter_str = strdup("(objectclass=nsviewincludenone)");  *//* hackery to get the right result */
/*		pView->includeChildViewsFilter = slapi_str2filter( view_filter_str );
	}
*/
#ifdef _VIEW_DEBUG_FILTERS
	slapi_filter_to_string(pView->includeChildViewsFilter, pView->includeChildViewsFilter_str, sizeof(pView->includeChildViewsFilter_str));
#endif
}


/*
	views_cache_build_view_list
	-------------------------------
	builds the list of views by searching for them throughout the DIT
*/
static int views_cache_build_view_list(viewEntry **pViews)
{
	int ret = SLAPI_PLUGIN_SUCCESS;
	Slapi_PBlock *pSuffixSearch = 0;
	Slapi_Entry **pSuffixList = 0;
	Slapi_Attr *suffixAttr;
	struct berval **suffixVals;
	char *attrType = 0;
	char *attrs[2];
	int suffixIndex = 0;
	int valIndex = 0;

	slapi_log_error(SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "--> views_cache_build_view_list\n");

	/*
		the views may be anywhere in the DIT,
		so our first task is to find them.
	*/

	attrs[0] = "namingcontexts";
	attrs[1] = 0;

	slapi_log_error(SLAPI_LOG_PLUGIN, VIEWS_PLUGIN_SUBSYSTEM, "views: Building view cache.\n");

	pSuffixSearch = slapi_search_internal("",LDAP_SCOPE_BASE,"(objectclass=*)",NULL,attrs,0);
	if(pSuffixSearch)
		slapi_pblock_get( pSuffixSearch, SLAPI_PLUGIN_INTOP_RESULT, &ret);

	if(pSuffixSearch && ret == LDAP_SUCCESS)
	{
		/* iterate through the suffixes and search for views */
		slapi_pblock_get( pSuffixSearch, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &pSuffixList);
		if(pSuffixList)
		{
			while(pSuffixList[suffixIndex])
			{
				if(!slapi_entry_first_attr(pSuffixList[suffixIndex], &suffixAttr))
				{
					do
					{
						attrType = 0;
						slapi_attr_get_type(suffixAttr, &attrType);
						if(attrType && !slapi_utf8casecmp((unsigned char*)attrType, (unsigned char*)"namingcontexts"))
						{
							if(!slapi_attr_get_bervals_copy(suffixAttr, &suffixVals))
							{
								valIndex = 0;

								if(suffixVals)
								{
									while(suffixVals[valIndex])
									{
										/* here's a suffix, lets search it... */
										if(suffixVals[valIndex]->bv_val)
											views_cache_add_dn_views(suffixVals[valIndex]->bv_val ,pViews);
										
										valIndex++;
									}


									ber_bvecfree( suffixVals );
									suffixVals = NULL;
								}
							}
						}

					} while(!slapi_entry_next_attr(pSuffixList[suffixIndex], suffixAttr, &suffixAttr));
				}
				suffixIndex++;
			}
		}
	}
	else
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, VIEWS_PLUGIN_SUBSYSTEM, "views_cache_build_view_list: failed to find suffixes\n");
		ret = SLAPI_PLUGIN_FAILURE;
	}

	/* clean up */
	if(pSuffixSearch)
	{
		slapi_free_search_results_internal(pSuffixSearch);
		slapi_pblock_destroy(pSuffixSearch);
	}


	slapi_log_error(SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "<-- views_cache_build_view_list\n");
	return ret;
}

/* struct to support search callback API */
struct dn_views_info {
	viewEntry **pViews;
	int ret;
};

/* does same funcationality as views_add_dn_views except it is invoked via a callback */

static int	views_dn_views_cb (Slapi_Entry* e, void *callback_data)
{
	struct dn_views_info *info;
	char *pDn = 0;
	struct berval **dnVals;
	Slapi_Attr *dnAttr;
	char *attrType = 0;
	viewEntry *pView;

	info=(struct dn_views_info *)callback_data;
	
	info->ret = 0;

	pDn = slapi_entry_get_ndn(e);
	
	/* create the view */
	pView = (viewEntry *)slapi_ch_calloc(1, sizeof(viewEntry));
	pView->pDn = slapi_ch_strdup(pDn);

	if(!slapi_entry_first_attr(e, &dnAttr))
	{
		do
		{
			attrType = 0;		


			/* get the filter */
			slapi_attr_get_type(dnAttr, &attrType);		
			if(attrType && !strcasecmp(attrType,VIEW_FILTER_ATTR))
			{
				if(!slapi_attr_get_bervals_copy(dnAttr, &dnVals))
				{
					/* add filter */
					pView->viewfilter = slapi_ch_strdup(dnVals[0]->bv_val);
				}

				ber_bvecfree( dnVals );
				dnVals = NULL;
			}

			if(attrType && !strcasecmp(attrType,"entryid"))
			{
				Slapi_Value *val = 0;

				slapi_attr_first_value(dnAttr, &val);
				pView->entryid = slapi_value_get_ulong(val);
			}
			
			if(attrType && !strcasecmp(attrType,"parentid"))
			{
				Slapi_Value *val = 0;

				slapi_attr_first_value(dnAttr, &val);
				pView->parentid = slapi_value_get_ulong(val);
			}

		} while(!slapi_entry_next_attr(e, dnAttr, &dnAttr));

	}		

	/* add view to the cache */
	views_cache_add_ll_entry((void**)info->pViews, (void *)pView);

	return info->ret;
}


/*
	views_cache_add_dn_views
	-------------------------
	takes a dn as argument and searches the dn for views,
	adding any found to the view cache. Change to use search callback API
*/

#define DN_VIEW_FILTER "(objectclass=" VIEW_OBJECTCLASS ")"

static int views_cache_add_dn_views(char *dn, viewEntry **pViews)
{
	Slapi_PBlock *pDnSearch = 0;
	struct dn_views_info info = {NULL, -1};
    pDnSearch = slapi_pblock_new();
	if (pDnSearch) {
		info.ret=-1;
		info.pViews=pViews;
		slapi_search_internal_set_pb(pDnSearch, dn, LDAP_SCOPE_SUBTREE,
									 DN_VIEW_FILTER,NULL,0,
									 NULL,NULL,view_get_plugin_identity(),0);
		slapi_search_internal_callback_pb(pDnSearch,
								  &info /* callback_data */,
								  NULL/* result_callback */,
								  views_dn_views_cb,
								  NULL /* referral_callback */);
		slapi_pblock_destroy (pDnSearch);
	}
	return info.ret;
}

/*
	views_cache_add_ll_entry
	---------------------------------------------------
	the element is added to the head of the linked list

	*NOTE* this function assumes and *requires* that the structures
	passed to it in "attrval" and "theVal" have a viewLinkedList
	member, and it is the *first* member of the structure.  This
	is safe because this is a module level function, and all functions
	which call this one are part of the same sub-system.
*/
static void views_cache_add_ll_entry(void** attrval, void *theVal)
{
	slapi_log_error(SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "--> views_cache_add_ll_entry\n");

	if(*attrval)
	{
		/* push this to the start of the list (because its quick) */
		((viewLinkedList*)theVal)->pNext = *attrval;
		((viewLinkedList*)(*attrval))->pPrev = theVal;
		*attrval = theVal;
	}
	else
	{
		/* new or end of list */
		((viewLinkedList*)theVal)->pNext = NULL;
		((viewLinkedList*)theVal)->pPrev = NULL;
		*attrval = theVal;
	}

	slapi_log_error(SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "<-- views_cache_add_ll_entry\n");
}


/*
	views_update_views_cache
	-----------------------

	update internal view cache after state change
*/
static void views_update_views_cache( Slapi_Entry *e, char *dn, int modtype, Slapi_PBlock *pb, void *caller_data )
{
	char *pDn;
	viewEntry *theView;
	viewEntry *current;
	Slapi_Attr *attr;
	struct berval val;
	int build_cache = 0;

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "--> views_update_views_cache\n");

	views_write_lock();

	if(!theCache.cache_built)
	{
		/* zarro views = no cache,
		 * this is probably an add op
		 * lets build the cache
		 */
		build_cache = 1;
		goto unlock_cache;
	}

	pDn = slapi_entry_get_ndn(e);
	theView = views_cache_find_view(pDn);

	switch(modtype)
	{
	case LDAP_CHANGETYPE_MODIFY:
		/* if still a view and exists
		 * update string filter
		 * update the filters of all views
		 * if just became a view fall through to add op
		 * if stopped being a view fall through to delete op
		 */

		/* determine what happenned - does the view exist currently? */
		if(theView)
		{
			/* does it have the view objectclass? */
			if(!slapi_entry_attr_find( e, "objectclass", &attr ))
			{
				val.bv_len = 8;
				val.bv_val = VIEW_OBJECTCLASS;

				if(!slapi_attr_value_find( attr, &val))
				{
					/* it is a view */
					attr = 0;

					/* has the filter changed? */
					slapi_entry_attr_find( e, VIEW_FILTER_ATTR, &attr );

					if(attr)
					{
						if(theView->viewfilter) /* NULL means a filter added */
						{
							/* we could translate the string filter into
							 * a real filter and compare against
							 * the view - that would tell us if the filter
							 * was substantively changed.
							 *
							 * But we're not gonna do that :)
							 */
							val.bv_len = strlen(theView->viewfilter)+1;
							val.bv_val = theView->viewfilter;

							if(!slapi_attr_value_find( attr, &val))
							{
								/* filter unchanged */
								break;
							}
						}
					}
					else
					{
						/* if no filter in view, then no change */
						if(theView->viewfilter == 0)
							break;
					}

					/* this was indeed a significant mod, add the new filter */
					if(theView->viewfilter)
						slapi_ch_free((void**)&theView->viewfilter);

					if(attr)
					{
						Slapi_Value *v;
						slapi_attr_first_value( attr, &v );
						theView->viewfilter = slapi_ch_strdup(slapi_value_get_string(v));
					}

					/* update all filters */
					for(current = theCache.pCacheViews; current != NULL; current = current->list.pNext)
					{
						views_cache_create_applied_filter(current);
						views_cache_create_exclusion_filter(current);
						views_cache_create_inclusion_filter(current);
					}
				}
				else
				{
					/* this is a delete operation */
					modtype = LDAP_CHANGETYPE_DELETE;
				}
			}
			else
				/* thats bad */
				break;
		}
		else
		{
			/* this is an add operation */
			modtype = LDAP_CHANGETYPE_ADD;
		}

	case LDAP_CHANGETYPE_DELETE:
		/* remove view entry from list
		 * update children of parent
		 * update all child filters
		 * re-index
		 */
		if(modtype == LDAP_CHANGETYPE_DELETE)
		{

			if(theCache.view_count-1)
			{
				/* detach view */
				if(theView->list.pPrev)
					((viewEntry*)(theView->list.pPrev))->list.pNext = theView->list.pNext;

				if(theView->list.pNext)
				{
					((viewEntry*)(theView->list.pNext))->list.pPrev = theView->list.pPrev;

					if(theView->list.pPrev == NULL) /* if this is the head */
						theCache.pCacheViews = (viewEntry*)(theView->list.pNext);
				}

				/* the parent of this node needs to know about its children */
				if(theView->pParent)
					views_cache_discover_children((viewEntry*)theView->pParent);
				/* each child of the deleted node will need to discover a new parent */
				views_cache_discover_parent_for_children((viewEntry*)theView);

				/* update filters */
				for(current = theCache.pCacheViews; current != NULL; current = current->list.pNext)
				{
					views_cache_create_applied_filter(current);
					views_cache_create_exclusion_filter(current);
					views_cache_create_inclusion_filter(current);
				}

				/* reindex */
				views_cache_index();
			}
			else
			{
				theCache.pCacheViews = NULL;
				theCache.view_count = 0;
				theCache.cache_built = 0;
			}

			/* free the view */
			slapi_ch_free((void**)&theView->pDn);
			slapi_ch_free((void**)&theView->viewfilter);
			slapi_filter_free(theView->includeAncestorFiltersFilter,1); 
			slapi_filter_free(theView->excludeAllButDescendentViewsFilter,1);
			slapi_filter_free(theView->excludeChildFiltersFilter,1);
			slapi_filter_free(theView->excludeGrandChildViewsFilter,1);
			slapi_filter_free(theView->includeChildViewsFilter,1);
			slapi_ch_free((void**)&theView->pSearch_base);
			slapi_ch_free((void**)&theView->pChildren);
			slapi_ch_free((void**)&theView);

			break;
		}

	case LDAP_CHANGETYPE_ADD:
		/* create view entry
		 * add it to list
		 * update children of parent
		 * update all child filters
		 * re-index
		 */
		if(modtype == LDAP_CHANGETYPE_ADD)
		{
			theView = (viewEntry *)slapi_ch_calloc(1, sizeof(viewEntry));
			theView->pDn = slapi_ch_strdup(pDn);
			
			/* get the view filter, the entryid, and the parentid */
			slapi_entry_attr_find( e, VIEW_FILTER_ATTR, &attr );

			if(attr)
			{
				Slapi_Value *v;
				slapi_attr_first_value( attr, &v );
				theView->viewfilter = slapi_ch_strdup(slapi_value_get_string(v));
			}
			else
				theView->viewfilter = NULL;

			slapi_entry_attr_find( e, "entryid", &attr );

			if(attr)
			{
				Slapi_Value *v;
				slapi_attr_first_value( attr, &v );
				theView->entryid = slapi_value_get_ulong(v);
			}
			else
				theView->entryid = 0;

			slapi_entry_attr_find( e, "parentid", &attr );

			if(attr)
			{
				Slapi_Value *v;
				slapi_attr_first_value( attr, &v );
				theView->parentid = slapi_value_get_ulong(v);
			}
			else
				theView->parentid = 0;

			/* add view to the cache */
			views_cache_add_ll_entry((void**)&theCache.pCacheViews, (void *)theView);

			views_cache_discover_parent(theView);
			if(theView->pParent)
				views_cache_discover_children((viewEntry*)theView->pParent);

			/* update filters */
			for(current = theCache.pCacheViews; current != NULL; current = current->list.pNext)
			{
				views_cache_discover_view_scope(current); /* if ns-view oc added, new view may be top */
				views_cache_create_applied_filter(current);
				views_cache_create_exclusion_filter(current);
				views_cache_create_inclusion_filter(current);
			}

			/* reindex */
			views_cache_index();
			break;
		}

	case LDAP_CHANGETYPE_MODDN:
		/* get old dn to find the view
		 * change dn
		 * update parents and children
		 * update all filters
		 * reindex 
		 */

		{
			char *old_dn;
			Slapi_Entry *old_entry;

			slapi_pblock_get( pb, SLAPI_ENTRY_PRE_OP, &old_entry );
			old_dn = slapi_entry_get_ndn(old_entry);

			theView = views_cache_find_view(old_dn);
			if(theView)
			{
				slapi_ch_free((void**)&theView->pDn);
				theView->pDn = slapi_ch_strdup(pDn);

				for(current = theCache.pCacheViews; current != NULL; current = current->list.pNext)
				{
					views_cache_discover_parent(current);
					views_cache_discover_children(current);
				}

				for(current = theCache.pCacheViews; current != NULL; current = current->list.pNext)
				{
					views_cache_discover_view_scope(current);
					views_cache_create_applied_filter(current);
					views_cache_create_exclusion_filter(current);
					views_cache_create_inclusion_filter(current);
				}
			}
			/* reindex */
			views_cache_index();
			break;
		}

	default:
		/* we don't care about this op */
		break;
	}

unlock_cache:
	views_unlock();

	if(build_cache)
	{
		views_cache_create();
	}

	slapi_log_error( SLAPI_LOG_TRACE, VIEWS_PLUGIN_SUBSYSTEM, "<-- views_update_views_cache\n");
}



/*
 * view_search_rewrite_callback
 * ----------------------------
 * this is the business end of the plugin
 * this function is called from slapd
 * rewrites the search to conform to the view
 * Meaning of the return code :
 * -1 : keep looking
 *  0 : rewrote OK
 *  1 : refuse to do this search
 *  2 : operations error
 */
static int view_search_rewrite_callback(Slapi_PBlock *pb)
{
	int ret = -1;
	char *base = 0;
	Slapi_Filter *clientFilter = 0;
	Slapi_Filter *includeAncestorFiltersFilter = 0; /* the view with all ancestor views */
	Slapi_Filter *excludeChildFiltersFilter = 0; /* NOT all children views, for one level searches */
	Slapi_Filter *excludeGrandChildViewsFilter = 0; /* view filter for one level searches */
	Slapi_Filter *includeChildViewsFilter = 0; /* view filter for subtree searches */
	Slapi_Filter *seeViewsFilter = 0; /* view filter to see views */
	Slapi_Filter *outFilter = 0;
	int scope = 0;
	int set_scope = LDAP_SCOPE_SUBTREE;
	viewEntry *theView = 0;
	Slapi_DN *basesdn = NULL;

#ifdef _VIEW_DEBUG_FILTERS
	char outFilter_str[1024];
	char clientFilter_str[1024];
	char includeAncestorFiltersFilter_str[1024];
	char excludeChildFiltersFilter_str[1024];
	char excludeGrandChildViewsFilter_str[1024];
	char includeChildViewsFilter_str[1024];
#endif

	/* if no cache, no views */
	if(!theCache.cache_built)
		goto end;

	/* avoid locking if this thread is the updater */
	if(theCache.currentUpdaterThread)
	{
		PRThread *thisThread = PR_GetCurrentThread();
		if(thisThread == theCache.currentUpdaterThread)
			goto end;
	}

	/* first, find out if this is a base search (we do nothing) */
	slapi_pblock_get(pb, SLAPI_SEARCH_SCOPE, &scope);
	if(scope == LDAP_SCOPE_BASE)
		goto end;

	/* if base of the search is a view */
	slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &basesdn);
	base = (char *)slapi_sdn_get_dn(basesdn);

	/* Read lock the cache */
	views_read_lock();

	theView = views_cache_find_view(base);

	/* if the view is disabled (we service subtree searches in this case) */
	if(!theView || (!theView->viewfilter && scope == LDAP_SCOPE_ONELEVEL))
	{
		/* unlock the cache */
		views_unlock();
		goto end;
	}


	/* this is a view search, and we are smokin' */

	/* grab the view filters we are going to need now so we can release the cache lock */
	if(scope == LDAP_SCOPE_ONELEVEL)
	{
		excludeChildFiltersFilter = slapi_filter_dup(theView->excludeChildFiltersFilter); 
		excludeGrandChildViewsFilter = slapi_filter_dup(theView->excludeGrandChildViewsFilter); 

#ifdef _VIEW_DEBUG_FILTERS
		slapi_filter_to_string(excludeChildFiltersFilter, excludeChildFiltersFilter_str, sizeof(excludeChildFiltersFilter_str));
		slapi_filter_to_string(excludeGrandChildViewsFilter, excludeGrandChildViewsFilter_str, sizeof(excludeGrandChildViewsFilter_str));
#endif

	}
	else
	{
		includeChildViewsFilter = slapi_filter_dup(theView->includeChildViewsFilter); 
	}

#ifdef _VIEW_DEBUG_FILTERS
	slapi_filter_to_string(includeChildViewsFilter, includeChildViewsFilter_str, sizeof(includeChildViewsFilter_str));
#endif

	/* always used */
	includeAncestorFiltersFilter = slapi_filter_dup(theView->includeAncestorFiltersFilter); 

#ifdef _VIEW_DEBUG_FILTERS
		slapi_filter_to_string(includeAncestorFiltersFilter, includeAncestorFiltersFilter_str, sizeof(includeAncestorFiltersFilter_str));
#endif

	/* unlock the cache */
	views_unlock();

	/* rewrite search scope and base*/
	slapi_pblock_set(pb, SLAPI_SEARCH_SCOPE, &set_scope);

	slapi_pblock_get(pb, SLAPI_SEARCH_TARGET_SDN, &basesdn);
	slapi_sdn_free(&basesdn);

	basesdn = slapi_sdn_new_dn_byval(theView->pSearch_base);
	slapi_pblock_set(pb, SLAPI_SEARCH_TARGET_SDN, basesdn);

	/* concatenate the filters */
	
	/* grab the client filter - we need 2 copies */
	slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &clientFilter);

#ifdef _VIEW_DEBUG_FILTERS
	slapi_filter_to_string(clientFilter, clientFilter_str, sizeof(clientFilter_str));
#endif
	/* There are two major clauses in a views filter, one looks
	   for entries that match the view filters themselves plus
	   the presented client filter, and the other looks for entries
	   that exist in the view hierarchy that also match the client
	   presented filter
	*/

	/* client supplied filter AND views inclusion filter
	   - make sure we can see entries in the view tree */
	if(scope == LDAP_SCOPE_ONELEVEL)
	{
		/* this filter is to lock our view to the onelevel search */
		if(excludeGrandChildViewsFilter)
		{
			seeViewsFilter = excludeGrandChildViewsFilter;
		}
	}
	else
	{
		/* this filter is to lock our view to the subtree search */
		if(includeChildViewsFilter)
		{
			seeViewsFilter = includeChildViewsFilter; 
		}
	}

	/* but only view tree entries that match the client filter */
	if(seeViewsFilter)
	{
		seeViewsFilter = slapi_filter_join_ex( LDAP_FILTER_AND, slapi_filter_dup(clientFilter), seeViewsFilter, 0 );
	}

	/* create target filter */
	if(includeAncestorFiltersFilter)
		outFilter = slapi_filter_join_ex( LDAP_FILTER_AND, includeAncestorFiltersFilter, clientFilter, 0 );
	else
		outFilter = clientFilter;

	if(scope == LDAP_SCOPE_ONELEVEL)
	{
		if(excludeChildFiltersFilter)
			outFilter = slapi_filter_join_ex( LDAP_FILTER_AND, outFilter, excludeChildFiltersFilter, 0  );
	}

	if(seeViewsFilter)
		outFilter = slapi_filter_join_ex( LDAP_FILTER_OR, outFilter, seeViewsFilter, 0 );

#ifdef _VIEW_DEBUG_FILTERS
	slapi_filter_to_string(outFilter, outFilter_str, sizeof(outFilter_str));
#endif

	/* make it happen */
	slapi_pblock_set(pb, SLAPI_SEARCH_FILTER, outFilter);

	ret = -2;

end:
	return ret;
}

/*
 * views_cache_backend_state_change()
 * --------------------------------
 * This is called when a backend changes state
 * We simply signal to rebuild the cache in this case
 *
 */
static void views_cache_backend_state_change(void *handle, char *be_name, 
     int old_be_state, int new_be_state) 
{
	/* we will create a thread to do this since
	 * calling views_cache_create() directly will
	 * hold up the op
	 */
	if ((PR_CreateThread (PR_USER_THREAD, 
					views_cache_act_on_change_thread, 
					NULL,
					PR_PRIORITY_NORMAL, 
					PR_GLOBAL_THREAD, 
					PR_UNJOINABLE_THREAD, 
					SLAPD_DEFAULT_THREAD_STACKSIZE)) == NULL )
	{
		slapi_log_error( SLAPI_LOG_FATAL, VIEWS_PLUGIN_SUBSYSTEM,
			   "views_cache_backend_state_change: PR_CreateThread failed\n" );
	}
}

static void views_cache_act_on_change_thread(void *arg)
{
	views_cache_create();
}
