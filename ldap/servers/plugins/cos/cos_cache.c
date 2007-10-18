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

/*
	The cos cache keeps in memory all of
	the data related to cos.  This allows
	very fast lookups at the expense of RAM.
	All meta data is indexed, allowing fast
	binary search lookups.
	The cache is not dynamic, in the sense
	that it does not iteratively modify
	itself as changes are made to the cos
	meta-data.  Rather, it is designed to
	be fast to read, with non-locking
	multiple thread access to the cache,
	at the expense of modification speed.
	This means that when changes do occur,
	the cache must be rebuilt from scratch.
	However, this is achieved in such a way,
	so as to allow cache queries during the
	building of the new cache - so once a
	cache has been built, there is no down
	time.
	Of course, the configuration of the cos meta
	data is likely to be a thing which does not
	happen often.  Any other use, is probably a
	mis-use of the mechanism, and certainly will
	suffer from performance problems.
*/

#include <stdio.h>
#include <string.h>
#include "portable.h"
#include "slapi-plugin.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */
#include "dirver.h"

/* this is naughty, but the api for backend state change is currently here */
#include "slapi-private.h"

/* include NSPR header files */
#include "prthread.h"
#include "prlock.h"
#include "prerror.h"
#include "prcvar.h"
#include "prio.h"
#include "vattr_spi.h"

#include "cos_cache.h"

#include "views.h"
static void **views_api;

/*** secret functions and structs in slapd ***/

/*
	these are required here because they are not available
	in any public header.  They must exactly match their
	counterparts in the server or they will fail to work
	correctly.
*/

/*** from slap.h ***/

struct objclass {
	char				*oc_name;		/* NAME */
	char				*oc_desc;		/* DESC */
    char        		*oc_oid;		/* object identifier */
    char        		*oc_superior;	/* SUP -- XXXmcs: should be an array */
	PRUint8				oc_kind;		/* ABSTRACT/STRUCTURAL/AUXILIARY */
    PRUint8				oc_flags;		/* misc. flags, e.g., OBSOLETE */
	char				**oc_required;
	char				**oc_allowed;
    char        		**oc_orig_required;	/* MUST */
    char        		**oc_orig_allowed;	/* MAY */
	char				**oc_origin;	/* X-ORIGIN extension */
	struct objclass		*oc_next;
};

/*** from proto-slap.h ***/

int config_get_schemacheck();
void oc_lock_read( void );
void oc_unlock( void );
struct objclass* g_get_global_oc_nolock();
int slapd_log_error_proc( char *subsystem, char *fmt, ... );

/*** from ldaplog.h ***/

/* edited ldaplog.h for LDAPDebug()*/
#ifndef _LDAPLOG_H
#define _LDAPLOG_H

/* defined in cos.c */
void * cos_get_plugin_identity();


#ifdef __cplusplus
extern "C" {
#endif

#define LDAP_DEBUG_TRACE	0x00001		/*     1 */
#define LDAP_DEBUG_ANY          0x04000		/* 16384 */
#define LDAP_DEBUG_PLUGIN	0x10000		/* 65536 */

/* debugging stuff */
#    ifdef _WIN32
       extern int	*module_ldap_debug;
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( *module_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#    else /* _WIN32 */
       extern int	slapd_ldap_debug;
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#    endif /* Win32 */

#ifdef __cplusplus
}
#endif

#endif /* _LDAP_H */

/*** end secrets ***/

#define COS_PLUGIN_SUBSYSTEM   "cos-plugin"   /* used for logging */

#define COSTYPE_BADTYPE 0
#define COSTYPE_CLASSIC 1
#define COSTYPE_POINTER 2
#define COSTYPE_INDIRECT 3
#define COS_DEF_ERROR_NO_TEMPLATES -2

/* the global plugin handle */
static volatile vattr_sp_handle *vattr_handle = NULL;

static int cos_cache_notify_flag = 0;

/* service definition cache structs */

/*	cosIndexedLinkedList: provides an object oriented type interface to
	link lists where each element contains an index for the entire
	list.  All structures that contain this one must specify this one
	as the first member otherwise the supporting functions will not work.
	
	{PARPAR} The indexing ability is not currently used since the
	fastest lookup is achieved via a cache level index of all attributes,
	however this mechanism may prove useful in the future
*/
struct _cosIndexedLinkedList
{
	void *pNext;
	void **index;
};
typedef struct _cosIndexedLinkedList cosIndexedLinkedList;

struct _cosAttrValue
{
	cosIndexedLinkedList list;
	char *val;
};
typedef struct _cosAttrValue cosAttrValue;

struct _cosAttribute
{
	cosIndexedLinkedList list;
	char *pAttrName;
	cosAttrValue *pAttrValue;
	cosAttrValue *pObjectclasses;
	int attr_override;
	int attr_operational;
	int attr_operational_default;
	int attr_cos_merge;
	void *pParent;
};
typedef struct _cosAttribute cosAttributes;

struct _cosTemplate
{
	cosIndexedLinkedList list;
	cosAttrValue *pDn;
	cosAttrValue *pObjectclasses;
	cosAttributes *pAttrs;
	char *cosGrade;
	int template_default;
	void *pParent;
	unsigned long cosPriority;
};

typedef struct _cosTemplate cosTemplates;

struct _cosDefinition
{
	cosIndexedLinkedList list;
	int cosType;
	cosAttrValue *pDn;
	cosAttrValue *pCosTargetTree;
	cosAttrValue *pCosTemplateDn;
	cosAttrValue *pCosSpecifier;
	cosAttrValue *pCosAttrs;
	cosAttrValue *pCosOverrides;
	cosAttrValue *pCosOperational;
	cosAttrValue *pCosOpDefault;
	cosAttrValue *pCosMerge;
	cosTemplates *pCosTmps;
};
typedef struct _cosDefinition cosDefinitions;

struct _cos_cache
{
	cosDefinitions *pDefs;
	cosAttributes **ppAttrIndex;
	int attrCount;
	char **ppTemplateList;
	int templateCount;
	int refCount;
	int vattr_cacheable;
};
typedef struct _cos_cache cosCache;

/* cache manipulation function prototypes*/
static cosCache *pCache; /* always the current global cache, only use getref to get */

/* the place to start if you want a new cache */
static int cos_cache_create();

/* cache index related functions */
static int cos_cache_index_all(cosCache *pCache);
static int cos_cache_attr_compare(const void *e1, const void *e2);
static int cos_cache_template_index_compare(const void *e1, const void *e2);
static int cos_cache_string_compare(const void *e1, const void *e2);
static int cos_cache_template_index_bsearch(const char *dn);
static int cos_cache_attr_index_bsearch( const cosCache *pCache, const cosAttributes *key, int lower, int upper );

/* the multi purpose list creation function, pass it something and it links it */
static void cos_cache_add_ll_entry(void **attrval, void *theVal, int ( *compare )(const void *elem1, const void *elem2 ));

/* cosAttrValue manipulation */
static int cos_cache_add_attrval(cosAttrValue **attrval, char *val);
static void cos_cache_del_attrval_list(cosAttrValue **pVal);
static int cos_cache_attrval_exists(cosAttrValue *pAttrs, const char *val);

/* cosAttributes manipulation */
static int cos_cache_add_attr(cosAttributes **pAttrs, char *name, cosAttrValue *val);
static void cos_cache_del_attr_list(cosAttributes **pAttrs);
static int cos_cache_find_attr(cosCache *pCache, char *type);
static int cos_cache_total_attr_count(cosCache *pCache);
static int cos_cache_cos_2_slapi_valueset(cosAttributes *pAttr, Slapi_ValueSet **out_vs);
static int cos_cache_cmp_attr(cosAttributes *pAttr, Slapi_Value *test_this, int *result);

/* cosTemplates manipulation */
static int cos_cache_add_dn_tmpls(char *dn,  cosAttrValue *pCosSpecifier, cosAttrValue *pAttrs, cosTemplates **pTmpls);
static int cos_cache_add_tmpl(cosTemplates **pTemplates, cosAttrValue *dn, cosAttrValue *objclasses, cosAttrValue *pCosSpecifier, cosAttributes *pAttrs,cosAttrValue *cosPriority);

/* cosDefinitions manipulation */
static int cos_cache_build_definition_list(cosDefinitions **pDefs, int *vattr_cacheable);
static int cos_cache_add_dn_defs(char *dn, cosDefinitions **pDefs, int *vattr_cacheable);
static int cos_cache_add_defn(cosDefinitions **pDefs, cosAttrValue **dn, int cosType, cosAttrValue **tree, cosAttrValue **tmpDn, cosAttrValue **spec, cosAttrValue **pAttrs, cosAttrValue **pOverrides, cosAttrValue **pOperational, cosAttrValue **pCosMerge, cosAttrValue **pCosOpDefault);
static int cos_cache_entry_is_cos_related( Slapi_Entry *e);

/* schema checking */
static int cos_cache_schema_check(cosCache *pCache, int cache_attr_index, Slapi_Attr *pObjclasses);
static int cos_cache_schema_build(cosCache *pCache);
static void cos_cache_del_schema(cosCache *pCache);

/* special cos scheme implimentations (special = other than cos classic) */
static int cos_cache_follow_pointer( vattr_context *context, const char *dn, char *type, Slapi_ValueSet **out_vs, Slapi_Value *test_this, int *result, int flags );


/* this dude is the thread function which performs dynamic config of the cache */
static void cos_cache_wait_on_change(void *arg);

/* this gets called when a backend changes state */
void cos_cache_backend_state_change(void *handle, char *be_name, 
     int old_be_state, int new_be_state); 

/* operation callbacks for vattr service */
static int cos_cache_vattr_get(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *free_flags, void *hint);
static int cos_cache_vattr_compare(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_Value *test_this, int* result, int flags, void *hint);
static int cos_cache_vattr_types(vattr_sp_handle *handle,Slapi_Entry *e,vattr_type_list_context *type_context,int flags);
static int cos_cache_query_attr(cos_cache *ptheCache, vattr_context *context, Slapi_Entry *e, char *type, Slapi_ValueSet **out_attr, Slapi_Value *test_this, int *result, int *ops);

/* 
	compares s2 to s1 starting from end of string until the beginning of either
	matches result in the s2 value being clipped from s1 with a NULL char
	and 1 being returned as opposed to 0
*/
static int cos_cache_backwards_stricmp_and_clip(char*s1,char*s2);

/* module level thread control stuff */

static int keeprunning = 0;
static int started = 0;

static Slapi_Mutex *cache_lock;
static Slapi_Mutex *change_lock;
static Slapi_Mutex *start_lock;
static Slapi_Mutex *stop_lock;
static Slapi_CondVar *something_changed = NULL;
static Slapi_CondVar *start_cond = NULL;


/*
	cos_cache_init
	--------------
	starts up the thread which waits for changes and
	fires off the cache re-creation when one is detected
	also registers vattr callbacks
*/
int cos_cache_init()
{
	int ret = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_init\n",0,0,0);

	slapi_vattrcache_cache_none();
	cache_lock = slapi_new_mutex();
	change_lock = slapi_new_mutex();
	stop_lock = slapi_new_mutex();
	something_changed = slapi_new_condvar(change_lock);
	keeprunning =1;
        start_lock = slapi_new_mutex();
        start_cond = slapi_new_condvar(start_lock);
        started = 0;

	if (	stop_lock == NULL ||
			change_lock == NULL ||
			cache_lock == NULL ||
                stop_lock == NULL ||
                start_lock == NULL ||
                start_cond == NULL ||
                something_changed == NULL)
        {
		slapi_log_error( SLAPI_LOG_FATAL, COS_PLUGIN_SUBSYSTEM,
			   "cos_cache_init: cannot create mutexes\n" );
                ret = -1;
		goto out;
        }

		/* grab the views interface */
		if(slapi_apib_get_interface(Views_v1_0_GUID, &views_api))
		{
			/* lets be tolerant if views is disabled */
			views_api = 0;
		}

        if (slapi_vattrspi_register((vattr_sp_handle **)&vattr_handle, 
                                    cos_cache_vattr_get, 
                                    cos_cache_vattr_compare, 
                                    cos_cache_vattr_types) != 0)
        {
		slapi_log_error( SLAPI_LOG_FATAL, COS_PLUGIN_SUBSYSTEM,
			   "cos_cache_init: cannot register as service provider\n" );
                ret = -1;
		goto out;
        }

        if ( PR_CreateThread (PR_USER_THREAD, 
					cos_cache_wait_on_change, 
					NULL,
					PR_PRIORITY_NORMAL, 
					PR_GLOBAL_THREAD, 
					PR_UNJOINABLE_THREAD, 
					SLAPD_DEFAULT_THREAD_STACKSIZE) == NULL )
	{
		slapi_log_error( SLAPI_LOG_FATAL, COS_PLUGIN_SUBSYSTEM,
			   "cos_cache_init: PR_CreateThread failed\n" );
		ret = -1;
		goto out;
	}

        /* wait for that thread to get started */
        if (ret == 0) {
            slapi_lock_mutex(start_lock);
            while (!started) {
                while (slapi_wait_condvar(start_cond, NULL) == 0);
            }
            slapi_unlock_mutex(start_lock);
        }


out:
	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_init\n",0,0,0);
	return ret;
}

/*
	cos_cache_wait_on_change
	------------------------
	sit around waiting on a notification that something has
	changed, then fires off the cache re-creation

	The way this stuff is written, we can look for the
	template for a definiton, before the template has been added--I think
	that's OK--we'll see it when it arrives--get this error message:
    "skipping cos definition cn=cosPointerGenerateSt,ou=People,o=cosAll--no templates found"
	
*/
static void cos_cache_wait_on_change(void *arg)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_wait_on_change thread\n",0,0,0);

	slapi_lock_mutex(stop_lock);
	slapi_lock_mutex(change_lock);

	/* first register our backend state change func (we'll use func pointer as handle) */
	slapi_register_backend_state_change((void *)cos_cache_backend_state_change, cos_cache_backend_state_change); 

	pCache = 0;

	/* create initial cache */
	cos_cache_create();

        slapi_lock_mutex(start_lock);
        started = 1;
        slapi_notify_condvar(start_cond, 1);
        slapi_unlock_mutex(start_lock);

	while(keeprunning)
	{
		slapi_unlock_mutex(change_lock);
		slapi_lock_mutex(change_lock);
		if ( !cos_cache_notify_flag && keeprunning) {
			/*
			 * Nothing to do right now, so go to sleep--as
			 * we have the mutex, we are sure to wait before any modify
			 * thread notifies our condvar, and so we will not miss any
			 * notifications, including the shutdown notification.
			 */
			slapi_wait_condvar( something_changed, NULL );
		} else {
			/* Something to do...do it below */
		}
		/*
		 * We're here because:
		 * 1. we were asleep and got a signal, on our condvar OR
		 * 2. we were about to wait on the condvar and noticed that a modfiy
		 * thread
		 * had passed, setting the cos_cache_notify_flag and notifying us--
		 * we did not wait in that case as we would have missed the notify
		 * (notify when noone is waiting == no-op).		
		 * before we go running off doing lots of stuff lets check if we should stop
		*/
		if(keeprunning) {
			cos_cache_create();				
		}
		cos_cache_notify_flag = 0; /* Dealt with it */
	}/* while */

	/* shut down the cache */
	slapi_unlock_mutex(change_lock);
	slapi_unlock_mutex(stop_lock);

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_wait_on_change thread exit\n",0,0,0);
}

/*
	cos_cache_create
	---------------------
	Walks the definitions in the DIT and creates the cache.
	Once created, it swaps the new cache for the old one,
	releasing its refcount to the old cache and allowing it
	to be destroyed.
*/
static int cos_cache_create()
{
	int ret = -1;
	cosCache *pNewCache;
	static int firstTime = 1;
	int cache_built = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_create\n",0,0,0);

	pNewCache = (cosCache*)slapi_ch_malloc(sizeof(cosCache));
	if(pNewCache)
	{
		pNewCache->pDefs = 0;
		pNewCache->refCount = 1; /* 1 is for us */
		pNewCache->vattr_cacheable = 0; /* default is not cacheable */

		ret = cos_cache_build_definition_list(&(pNewCache->pDefs), &(pNewCache->vattr_cacheable));
		if(!ret)
		{
			/* OK, we have a cache, lets add indexing for
			that faster than slow feeling */
			
			ret = cos_cache_index_all(pNewCache);
			if(ret == 0)
			{
				/* right, indexed cache, lets do our duty for the schema */

				ret = cos_cache_schema_build(pNewCache);
				if(ret == 0)
				{
					/* now to swap the new cache for the old cache */
					cosCache *pOldCache;

					slapi_lock_mutex(cache_lock);

					/* turn off caching until the old cache is done */
					if(pCache)
					{
						slapi_vattrcache_cache_none();

						/*
						 * be sure not to uncache other stuff
						 * like roles if there is no change in
						 * state
						 */
						if(pCache->vattr_cacheable)
							slapi_entrycache_vattrcache_watermark_invalidate();
					}
					else
					{
						if(pNewCache && pNewCache->vattr_cacheable)
						{
							slapi_vattrcache_cache_all();
						}
					}

					pOldCache = pCache;
					pCache = pNewCache;

					slapi_unlock_mutex(cache_lock);

					if(pOldCache)
						cos_cache_release(pOldCache);

					cache_built = 1;
				}
				else
				{
					/* we should not go on without proper schema checking */
					cos_cache_release(pNewCache);
					LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_create: failed to cache the schema\n",0,0,0);			
				}
			}
			else
			{
				/* currently we cannot go on without the indexes */
				cos_cache_release(pNewCache);
				LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_create: failed to index cache\n",0,0,0);			
			}
		}
		else
		{
			if(firstTime)
			{
				LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_create: cos disabled\n",0,0,0);
				firstTime = 0;
			}

			slapi_ch_free((void**)&pNewCache);
		}
	}
	else
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_create: memory allocation failure\n",0,0,0);


	/* make sure we have a new cache */
	if(!cache_built)
	{
		/* we do not have a new cache, must make sure the old cache is destroyed */

		cosCache *pOldCache;

		slapi_lock_mutex(cache_lock);

		slapi_vattrcache_cache_none();

		/*
		 * be sure not to uncache other stuff
		 * like roles if there is no change in
		 * state
		 */
		if(pCache && pCache->vattr_cacheable)
			slapi_entrycache_vattrcache_watermark_invalidate();
		
		pOldCache = pCache;
		pCache = NULL;

		slapi_unlock_mutex(cache_lock);

		if(pOldCache)
			cos_cache_release(pOldCache); /* release our reference to the old cache */
		
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_create\n",0,0,0);
	return ret;
}


/*
	cos_cache_build_definition_list
	-------------------------------
	builds the list of cos definitions by searching for them throughout the DIT
*/
static int cos_cache_build_definition_list(cosDefinitions **pDefs, int *vattr_cacheable)
{
	int ret = 0;
	Slapi_PBlock *pSuffixSearch = 0;
	Slapi_Entry **pSuffixList = 0;
	Slapi_Attr *suffixAttr;
	struct berval **suffixVals;
	char *attrType = 0;
	char *attrs[2];
	int suffixIndex = 0;
	int valIndex = 0;
	int cos_def_available = 0;
	static int firstTime = 1;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_build_definition_list\n",0,0,0);

	/*
		the class of service definitions may be anywhere in the DIT,
		so our first task is to find them.
	*/

	attrs[0] = "namingcontexts";
	attrs[1] = 0;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "cos: Building class of service cache after status change.\n",0,0,0);

	/*
	 * XXXrbyrne: this looks really ineficient--should be using
	 * slapi_get_next_suffix(), rather than searching for namingcontexts.
	*/

	pSuffixSearch = slapi_search_internal("",LDAP_SCOPE_BASE,"(objectclass=*)",NULL,attrs,0);
	if(pSuffixSearch)
		slapi_pblock_get( pSuffixSearch, SLAPI_PLUGIN_INTOP_RESULT, &ret);

	if(pSuffixSearch && ret == LDAP_SUCCESS)
	{
		/* iterate through the suffixes and search for cos definitions */
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
											if(!cos_cache_add_dn_defs(suffixVals[valIndex]->bv_val ,pDefs, vattr_cacheable))
												cos_def_available = 1;
										
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
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_build_definition_list: failed to find suffixes\n",0,0,0);
		ret = -1;
	}

	if(cos_def_available == 0)
	{
		if(firstTime)
		{
			LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_build_definition_list: Found no cos definitions, cos disabled while waiting for updates\n",0,0,0);
			firstTime = 0;
		}

		ret = -1;
	}
	else
		LDAPDebug( LDAP_DEBUG_PLUGIN, "cos: Class of service cache built.\n",0,0,0);

	/* clean up */
	if(pSuffixSearch)
	{
		slapi_free_search_results_internal(pSuffixSearch);
		slapi_pblock_destroy(pSuffixSearch);
	}


	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_build_definition_list\n",0,0,0);
	return ret;
}


/* struct to support search callback API */
struct dn_defs_info {
	cosDefinitions **pDefs;
	int vattr_cacheable;
	int ret;
};

/*
 * Currently, always returns 0 to continue the search for definitions, even
 * if a particular attempt to add a definition fails: info.ret gets set to
 * zero only if we succed to add a def.
*/
static int 	cos_dn_defs_cb (Slapi_Entry* e, void *callback_data) {
	struct dn_defs_info *info;
	cosAttrValue **pSneakyVal = 0;
	cosAttrValue *pObjectclass = 0;
	cosAttrValue *pCosTargetTree = 0;
	cosAttrValue *pCosTemplateDn = 0;
	cosAttrValue *pCosSpecifier = 0;
	cosAttrValue *pCosAttribute = 0;
	cosAttrValue *pCosOverrides = 0;
	cosAttrValue *pCosOperational = 0;
	cosAttrValue *pCosOpDefault = 0;
	cosAttrValue *pCosMerge = 0;
	cosAttrValue *pDn = 0;
	struct berval **dnVals;
	int cosType = 0;
	int valIndex = 0;
	Slapi_Attr *dnAttr;
	char *attrType = 0;
	info=(struct dn_defs_info *)callback_data;
	
			
	/* assume cacheable */
	info->vattr_cacheable = -1;

	cos_cache_add_attrval(&pDn, slapi_entry_get_dn(e));
	if(!slapi_entry_first_attr(e, &dnAttr))
	{
		do
		{
			attrType = 0;		
			/* we need to fill in the details of the definition now */
			slapi_attr_get_type(dnAttr, &attrType);		
			if(attrType)
			{
				pSneakyVal = 0;
				if(!slapi_utf8casecmp((unsigned char*)attrType, (unsigned char*)"objectclass"))
					pSneakyVal = &pObjectclass;
				else if(!slapi_utf8casecmp((unsigned char*)attrType, (unsigned char*)"cosTargetTree"))
					pSneakyVal = &pCosTargetTree;
				else if(!slapi_utf8casecmp((unsigned char*)attrType, (unsigned char*)"cosTemplateDn"))
					pSneakyVal = &pCosTemplateDn;
				else if(!slapi_utf8casecmp((unsigned char*)attrType, (unsigned char*)"cosSpecifier"))
					pSneakyVal = &pCosSpecifier;
				else if(!slapi_utf8casecmp((unsigned char*)attrType, (unsigned char*)"cosAttribute"))
					pSneakyVal = &pCosAttribute;
				else if(!slapi_utf8casecmp((unsigned char*)attrType, (unsigned char*)"cosIndirectSpecifier"))
					pSneakyVal = &pCosSpecifier;			
				if(pSneakyVal)
				{
					/* It's a type we're interested in */
					if(!slapi_attr_get_bervals_copy(dnAttr, &dnVals))
					{
						valIndex = 0;						
						if(dnVals)
						{
							while(dnVals[valIndex])
							{
								if(dnVals[valIndex]->bv_val)
								{
								/*
								parse any overide or default values
								and deal with them
									*/
									if(pSneakyVal == &pCosAttribute)
									{
										int qualifier_hit = 0;
										int op_qualifier_hit = 0;
										int merge_schemes_qualifier_hit = 0;
										int override_qualifier_hit =0;
										int default_qualifier_hit = 0;
										int operational_default_qualifier_hit = 0;
										do
										{
											qualifier_hit = 0;

											if(cos_cache_backwards_stricmp_and_clip(dnVals[valIndex]->bv_val, " operational"))
											{
												/* matched */
												op_qualifier_hit = 1;
												qualifier_hit = 1;
											}
											
											if(cos_cache_backwards_stricmp_and_clip(dnVals[valIndex]->bv_val, " merge-schemes"))
											{
												/* matched */
												merge_schemes_qualifier_hit = 1;
												qualifier_hit = 1;
											}

											if(cos_cache_backwards_stricmp_and_clip(dnVals[valIndex]->bv_val, " override"))
											{
												/* matched */
												override_qualifier_hit = 1;
												qualifier_hit = 1;
											}
											
											if(cos_cache_backwards_stricmp_and_clip(dnVals[valIndex]->bv_val, " default")) {
												default_qualifier_hit = 1;
												qualifier_hit = 1;
											}

											if(cos_cache_backwards_stricmp_and_clip(dnVals[valIndex]->bv_val, " operational-default")) {
												operational_default_qualifier_hit = 1;
												qualifier_hit = 1;
											}
										}
										while(qualifier_hit == 1);

										/*
									 	* At this point, dnVals[valIndex]->bv_val
									 	* is the value of cosAttribute, stripped of
									 	* any qualifiers, so add this pure attribute type to
										* the appropriate lists.
										*/
								
										if ( op_qualifier_hit ) {
											cos_cache_add_attrval(&pCosOperational,
													dnVals[valIndex]->bv_val);
										}
										if ( merge_schemes_qualifier_hit ) {
											cos_cache_add_attrval(&pCosMerge,
													dnVals[valIndex]->bv_val);
										}
										if ( override_qualifier_hit ) {
											cos_cache_add_attrval(&pCosOverrides,
													dnVals[valIndex]->bv_val);										
										}
										if ( default_qualifier_hit ) {
											/* attr is added below in pSneakyVal, in any case */
										}

										if ( operational_default_qualifier_hit ) {
											cos_cache_add_attrval(&pCosOpDefault,
													dnVals[valIndex]->bv_val);
										}

										if(!pCosTargetTree)
										{
											/* get the parent of the definition */
											
											char *parent = slapi_dn_parent(pDn->val);
											slapi_dn_normalize( parent );
											
											cos_cache_add_attrval(&pCosTargetTree, parent);
											if(!pCosTemplateDn)
												cos_cache_add_attrval(&pCosTemplateDn, parent);
											
											slapi_ch_free((void**)&parent);
										}
										
										slapi_vattrspi_regattr((vattr_sp_handle *)vattr_handle, dnVals[valIndex]->bv_val, NULL, NULL);			
									} /* if(attrType is cosAttribute) */
																
									/*
									 * Add the attributetype to the appropriate
									 * list.
									*/											
									cos_cache_add_attrval(pSneakyVal,
												dnVals[valIndex]->bv_val);								
								}/*if(dnVals[valIndex]->bv_val)*/
								
								valIndex++;
							}/* while(dnVals[valIndex]) */
							
							ber_bvecfree( dnVals );
							dnVals = NULL;
						}/*if(dnVals)*/
					}
				}/*if(pSneakyVal)*/
			}/*if(attrType)*/
			
		} while(!slapi_entry_next_attr(e, dnAttr, &dnAttr));
		
		/*
		determine the type of class of service scheme 
		*/
		
		if(pObjectclass)
		{
			if(cos_cache_attrval_exists(pObjectclass, "cosDefinition"))
			{
				cosType = COSTYPE_CLASSIC;
			}
			else if(cos_cache_attrval_exists(pObjectclass, "cosClassicDefinition"))
			{
				cosType = COSTYPE_CLASSIC;
				
			}
			else if(cos_cache_attrval_exists(pObjectclass, "cosPointerDefinition"))
			{
				cosType = COSTYPE_POINTER;
				
			}
			else if(cos_cache_attrval_exists(pObjectclass, "cosIndirectDefinition"))
			{
				cosType = COSTYPE_INDIRECT;
				
			}
			else
				cosType = COSTYPE_BADTYPE;
		}
		
		/*	
		we should now have a full definition, 
		do some sanity checks because we don't
		want bogus entries in the cache 
		then ship it
		*/
		
		/* these must exist */
		if(		pDn &&
			pObjectclass && 
			
			(
			(cosType == COSTYPE_CLASSIC &&
			pCosTemplateDn && 
			pCosSpecifier &&   
			pCosAttribute ) 
			||
			(cosType == COSTYPE_POINTER &&
			pCosTemplateDn && 
			pCosAttribute ) 
			||
			(cosType == COSTYPE_INDIRECT &&
			pCosSpecifier &&   
			pCosAttribute ) 
			)
			)
		{
			int rc = 0;
		/*
		we'll leave the referential integrity stuff
		up to the referint plug-in and assume all
		is good - if it's not then we just have a
		useless definition and we'll nag copiously later.
			*/
			char *pTmpDn = slapi_ch_strdup(pDn->val); /* because dn gets hosed on error */
			char ebuf[ BUFSIZ ];
			
			if(!(rc = cos_cache_add_defn(info->pDefs, &pDn, cosType,
									&pCosTargetTree, &pCosTemplateDn, 
									&pCosSpecifier, &pCosAttribute,
									&pCosOverrides, &pCosOperational,
									&pCosMerge, &pCosOpDefault))) {
				info->ret = 0;  /* we have succeeded to add the defn*/
			} else {
				/*
				 * Failed but we will continue the search for other defs
				 * Don't reset info->ret....it keeps track of any success
				*/
				if ( rc == COS_DEF_ERROR_NO_TEMPLATES) {
					LDAPDebug(LDAP_DEBUG_ANY, "skipping cos definition %s"
							"--no templates found\n",
							escape_string(pTmpDn, ebuf),0,0);
				} else {
					LDAPDebug(LDAP_DEBUG_ANY, "skipping cos definition %s\n"
								,escape_string(pTmpDn, ebuf),0,0);
				}
			}
			
			slapi_ch_free_string(&pTmpDn);
		}
		else
		{
		/* 
		this definition is brain dead - bail
		if we have a dn use it to report, if not then *really* bad
		things are going on
			*/
			if(pDn)
			{
				LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_dn_defs: incomplete cos definition detected in %s, discarding from cache.\n",pDn->val,0,0);
			}
			else
				LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_dn_defs: incomplete cos definition detected, no DN to report, discarding from cache.\n",0,0,0);
			
			if(pCosTargetTree)
				cos_cache_del_attrval_list(&pCosTargetTree);
			if(pCosTemplateDn)
				cos_cache_del_attrval_list(&pCosTemplateDn);
			if(pCosSpecifier)
				cos_cache_del_attrval_list(&pCosSpecifier);
			if(pCosAttribute)
				cos_cache_del_attrval_list(&pCosAttribute);
			if(pDn)
				cos_cache_del_attrval_list(&pDn);
		}
	}/*if(!slapi_entry_first_attr(e, &dnAttr))*/
	/* we don't keep the objectclasses, so lets free them */
	if(pObjectclass) {
			cos_cache_del_attrval_list(&pObjectclass);
	}
	/* This particular definition may not have yielded anything
     * worth caching (eg. no template was found for it) but
     * that should not cause us to abort the search for other more well behaved
     * definitions.
     * return info->ret;
    */
    return (0);

}

/*
	cos_cache_add_dn_defs
	-------------------------
	takes a dn as argument and searches the dn for cos definitions,
	adding any found to the definition list. Change to use search callback API.

	Returns: 0: found at least one definition entry that got added to the
		cache successfully.
			 non-zero: added no cos defs to the cache.
*/

#define DN_DEF_FILTER "(&(|(objectclass=cosSuperDefinition)(objectclass=cosDefinition))(objectclass=ldapsubentry))"

static int cos_cache_add_dn_defs(char *dn, cosDefinitions **pDefs, int *vattr_cacheable)
{
	Slapi_PBlock *pDnSearch = 0;
	struct dn_defs_info info;
    pDnSearch = slapi_pblock_new();
	if (pDnSearch) {
		info.ret=-1; /* assume no good defs */
		info.pDefs=pDefs;
		info.vattr_cacheable = 0; /* assume not cacheable */
		slapi_search_internal_set_pb(pDnSearch, dn, LDAP_SCOPE_SUBTREE,
									 DN_DEF_FILTER,NULL,0,
									 NULL,NULL,cos_get_plugin_identity(),0);
		slapi_search_internal_callback_pb(pDnSearch,
								  &info /* callback_data */,
								  NULL/* result_callback */,
								  cos_dn_defs_cb,
								  NULL /* referral_callback */);
		slapi_pblock_destroy (pDnSearch);
	}

	*vattr_cacheable = info.vattr_cacheable;

	return info.ret;
}



/* struct to support call back API */

struct tmpl_info {
	cosAttrValue *pCosSpecifier;
	cosAttrValue *pAttrs;
	cosTemplates **pTmpls;
	int ret;
};


/*
 * Currently, always returns 0 to continue the search for templates, even
 * if a particular attempt to add a template fails: info.ret gets set to
 * zero only if we succed to add at least one tmpl.
*/
static int 	cos_dn_tmpl_entries_cb (Slapi_Entry* e, void *callback_data) {
	cosAttrValue *pDn = 0;
	cosAttrValue *pCosPriority = 0;
	cosAttributes *pAttributes = 0;
	cosAttrValue *pObjectclass = 0;
	cosAttrValue *pCosAttribute = 0;
	Slapi_Attr *dnAttr;
	struct berval **dnVals;
	int itsAnAttr = 0;
	int valIndex = 0;
	cosAttrValue **pSneakyVal = 0;
	char *attrType = 0;
	struct tmpl_info *info;
	info = (struct tmpl_info *)callback_data;

	pDn = 0;
	cos_cache_add_attrval(&pDn, slapi_entry_get_dn(e));
	pAttributes = 0;
	pObjectclass = 0;
	pCosPriority = 0;
				
	if(!slapi_entry_first_attr(e, &dnAttr))
	{
		int attrs_present = 0;
		
		do
		{
			attrType = 0;
			pCosAttribute = 0;
			
			/* we need to fill in the details of the template now */
			slapi_attr_get_type(dnAttr, &attrType);
			
			if(attrType)
			{
				itsAnAttr = 0;
				pSneakyVal = 0;
				
				if(!slapi_utf8casecmp((unsigned char*)attrType,
											(unsigned char*)"objectclass"))
					pSneakyVal = &pObjectclass;
				
				if(!slapi_utf8casecmp((unsigned char*)attrType,
											(unsigned char*)"cosPriority"))
					pSneakyVal = &pCosPriority;
				
				if(pSneakyVal == NULL)
				{
					/* look for the atrribute in the dynamic attributes */
					if(cos_cache_attrval_exists(info->pAttrs, attrType))
					{
						pSneakyVal = &pCosAttribute;
						itsAnAttr = 1;
						attrs_present = 1;
					}
				}
				
				if(pSneakyVal)
				{
					if(!slapi_attr_get_bervals_copy(dnAttr, &dnVals))
					{
						valIndex = 0;
						
						if(dnVals)
						{
							while(dnVals[valIndex])
							{
								if(dnVals[valIndex]->bv_val)
									cos_cache_add_attrval(pSneakyVal,
													dnVals[valIndex]->bv_val);
								
								valIndex++;
							}
							
							if(itsAnAttr)
							{
								/* got all vals, add this attribute to the attribute list */
								cos_cache_add_attr(&pAttributes, attrType,
																*pSneakyVal);
							}
							
							ber_bvecfree( dnVals );
							dnVals = NULL;
						}
					}
				}
			}
			
		} while(!slapi_entry_next_attr(e, dnAttr, &dnAttr));
		
		/*	
		we should now have a full template, 
		do some sanity checks because we don't
		want bogus entries in the cache if we can help it
		- then ship it
		*/
		
		/* these must exist */
		if( 
			attrs_present &&
			pObjectclass && 
			pAttributes &&
			pDn
			)
		{
		/*
		we'll leave the referential integrity stuff
		up to the referint plug-in if set up and assume all
		is good - if it's not then we just have a
		useless definition and we'll nag copiously later.
			*/
			
			if(!cos_cache_add_tmpl(info->pTmpls, pDn, pObjectclass,
								info->pCosSpecifier, pAttributes,pCosPriority)){
				info->ret = 0;  /* we have succeed to add the tmpl */
			} else {
				/* Don't reset info->ret....it keeps track of any success */
				LDAPDebug(LDAP_DEBUG_ANY, "cos_cache_add_dn_tmpls:"
								"could not cache cos template %s\n",pDn,0,0);
			}
		}
		else
		{
		/* 
		this template is brain dead - bail
		if we have a dn use it to report, if not then *really* bad
		things are going on
		- of course it might not be a template, so lets
		not tell the world unless the world wants to know,
		we'll make it a plugin level message
			*/
			if(pDn)
			{
				LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_add_dn_tmpls: incomplete cos template detected in %s, discarding from cache.\n",pDn->val,0,0);
			}
			else
				LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_add_dn_tmpls: incomplete cos template detected, no DN to report, discarding from cache.\n",0,0,0);
			
			if(pObjectclass)
				cos_cache_del_attrval_list(&pObjectclass);
			if(pCosAttribute)
				cos_cache_del_attrval_list(&pCosAttribute);
			if(pDn)
				cos_cache_del_attrval_list(&pDn);
			if(pAttributes)
				cos_cache_del_attr_list(&pAttributes);
		}
	}			
	/*
	 * Always contine the search even if a particular attempt
	 * to add a template failed.
	*/	
	return 0;
}

/*
	cos_cache_add_dn_tmpls
	-------------------------
	takes a dn as argument and searches the dn for cos templates,
	adding any found to the template list
	This is the new version using call back search API
	
	Returns: zero for success--found at least one good tmpl for this def.
			non-zero: failed to add any templs for this def.
*/

#define TMPL_FILTER "(&(objectclass=costemplate)(|(objectclass=costemplate)(objectclass=ldapsubentry)))"

static int cos_cache_add_dn_tmpls(char *dn, cosAttrValue *pCosSpecifier, cosAttrValue *pAttrs, cosTemplates **pTmpls)
{
	void *plugin_id;
	int scope;
	struct tmpl_info	info;
	Slapi_PBlock *pDnSearch = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_add_dn_tmpls\n",0,0,0);
	
	/* no cos specifier means this is an indirect scheme */
	if(pCosSpecifier)
		scope = LDAP_SCOPE_ONELEVEL;
	else
		scope = LDAP_SCOPE_BASE;
	
	/* Use new internal operation API */
	pDnSearch = slapi_pblock_new();
	plugin_id=cos_get_plugin_identity();
	if (pDnSearch) {
		info.pAttrs=pAttrs;
		info.pTmpls=pTmpls;
		info.pCosSpecifier=pCosSpecifier;
		info.ret=-1; /* assume no good tmpls */
		slapi_search_internal_set_pb(pDnSearch, dn, scope,
									 TMPL_FILTER,NULL,0,
									 NULL,NULL,plugin_id,0);
		slapi_search_internal_callback_pb(pDnSearch,
								  &info /* callback_data */,
								  NULL/* result_callback */,
								  cos_dn_tmpl_entries_cb,
								  NULL /* referral_callback */);
		slapi_pblock_destroy (pDnSearch);
	}
	/*
	 * info.ret comes out zero only if we succeed to add at least one
	 * tmpl to the cache.
	*/
	return (info.ret);
}

/*
	cos_cache_add_defn
	------------------
	Add a cos definition to the list and create the template
	cache for this definition
	returns: 0: successfully added the definition to the cache
			non-zero: failed to add the definition to the cache (eg. because
					there was no template for it.)
					ret==COS_DEF_ERROR_NO_TEMPLATES then no templs were found
					for thsi def.  We make a special case of this and pass the
					back the error so we can roll two messages into one--this
					is to reduce the number of error messages at cos definiton
					load time--it is common to see the defs before the tmpls
					arrive.
					
*/
static int cos_cache_add_defn(
					   cosDefinitions **pDefs, 
					   cosAttrValue **dn, 
					   int cosType,
					   cosAttrValue **tree, 
					   cosAttrValue **tmpDn, 
					   cosAttrValue **spec, 
					   cosAttrValue **pAttrs, 
					   cosAttrValue **pOverrides,
					   cosAttrValue **pOperational,
					   cosAttrValue **pCosMerge,
					   cosAttrValue **pCosOpDefault
					   )
{
	int ret = 0;
	int tmplCount = 0;
	cosDefinitions *theDef = 0;
	cosAttrValue *pTmpTmplDn = *tmpDn;
	cosAttrValue *pDummyAttrVal = 0;
	cosAttrValue *pAttrsIter = 0;
	cosAttributes *pDummyAttributes = 0;
	cosAttrValue *pSpecsIter = *spec;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_add_defn\n",0,0,0);
	
	/* we don't want cosspecifiers that can be supplied by the same scheme */
	while( pSpecsIter )
	{
		if(	cos_cache_attrval_exists(*pAttrs, pSpecsIter->val ) )
		{
			/* no, this is not sane, lets reject the whole darn scheme in disgust */
			LDAPDebug( LDAP_DEBUG_ANY, "cos definition %s supplies its own cosspecifier, rejecting scheme\n",(*dn)->val,0,0);
			ret = -1;
		}

		pSpecsIter = pSpecsIter->list.pNext;
	}

	/* create the definition */
	if(0 == ret)
	{
		theDef = (cosDefinitions*) slapi_ch_malloc(sizeof(cosDefinitions));
		if(theDef)
		{
			theDef->pCosTmps = NULL;
			
			/* process each template in turn */
			
			LDAPDebug( LDAP_DEBUG_PLUGIN, "Processing cosDefinition %s\n",(*dn)->val,0,0);
			
			while(pTmpTmplDn && cosType != COSTYPE_INDIRECT)
			{
				/* create the template */
				if(!cos_cache_add_dn_tmpls(pTmpTmplDn->val, *spec, *pAttrs, &(theDef->pCosTmps)))
					tmplCount++;

				pTmpTmplDn = pTmpTmplDn->list.pNext;
			}
			
			if(tmplCount == 0 && cosType != COSTYPE_INDIRECT)
			{
				/* without our golden templates we are nothing 
				LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_defn:"
				"no templates for cos definition at %s.\n",(*dn)->val,0,0);*/
				ret = COS_DEF_ERROR_NO_TEMPLATES;
			}
			else
			{
				if(cosType == COSTYPE_INDIRECT)
				{
					/* 
						Indirect cos schemes have no templates,
						however, in order to take advantage of existing code
						which is optimized to do a binary search on attributes
						which are found through their templates, we add a dummy
						template and dummy attributes.  The value of the attributes
						will be ignored when later assessing a query.
					*/
					pAttrsIter = *pAttrs;

					while(pAttrsIter)
					{
						pDummyAttrVal = NULL;
						cos_cache_add_attrval(&pDummyAttrVal, "not used");
						cos_cache_add_attr(&pDummyAttributes, pAttrsIter->val, pDummyAttrVal);

						pAttrsIter = pAttrsIter->list.pNext;
					}
					
					cos_cache_add_attrval(tmpDn, "cn=dummy,");

					cos_cache_add_tmpl(&(theDef->pCosTmps), *tmpDn, NULL, *spec, pDummyAttributes,NULL);
					*tmpDn = 0;
				}

				theDef->pDn = *dn;
				theDef->cosType = cosType;
				theDef->pCosTargetTree = *tree;
				theDef->pCosTemplateDn = *tmpDn;
				theDef->pCosSpecifier = *spec;
				theDef->pCosAttrs = *pAttrs;
				theDef->pCosOverrides = *pOverrides;
				theDef->pCosOperational = *pOperational;
				theDef->pCosMerge = *pCosMerge;
				theDef->pCosOpDefault = *pCosOpDefault;

				cos_cache_add_ll_entry((void**)pDefs, theDef, NULL);
				LDAPDebug( LDAP_DEBUG_PLUGIN, "Added cosDefinition %s\n",(*dn)->val,0,0);
			}
		}
		else
		{
			ret = -1;
		}
	}

	if(ret == -1)
	{
		if(dn)
			cos_cache_del_attrval_list(dn);
		if(tree)
			cos_cache_del_attrval_list(tree);
		if(tmpDn)
			cos_cache_del_attrval_list(tmpDn);
		if(spec)
			cos_cache_del_attrval_list(spec);
		if(pAttrs)
			cos_cache_del_attrval_list(pAttrs);
		if(theDef)
			slapi_ch_free((void**)&theDef);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_add_defn\n",0,0,0);
	return ret;
}

/*
	cos_cache_del_attrval_list
	--------------------------
	walks the list deleting as it goes
*/
static void cos_cache_del_attrval_list(cosAttrValue **pVal)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_del_attrval_list\n",0,0,0);

	while(*pVal)
	{
		cosAttrValue *pTmp = (*pVal)->list.pNext;

		slapi_ch_free((void**)&((*pVal)->val));
		slapi_ch_free((void**)&(*pVal));
		*pVal = pTmp;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_del_attrval_list\n",0,0,0);
}


/* 
	cos_cache_add_attrval
	---------------------
	adds a value to an attribute value list
*/
static int cos_cache_add_attrval(cosAttrValue **attrval, char *val)
{
	int ret = 0;
	cosAttrValue *theVal;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_add_attrval\n",0,0,0);

	/* create the attrvalue */
	theVal = (cosAttrValue*) slapi_ch_malloc(sizeof(cosAttrValue));
	if(theVal)
	{
		theVal->val = slapi_ch_strdup(val);
		if(theVal->val)
		{
			cos_cache_add_ll_entry((void**)attrval, theVal, NULL);
		}
		else
		{
			slapi_ch_free((void**)&theVal);
			LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_attrval: failed to allocate memory\n",0,0,0);
			ret = -1;
		}
	}
	else
	{
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_attrval: failed to allocate memory\n",0,0,0);
		ret = -1;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_add_attrval\n",0,0,0);
	return ret;
}

/*
	cos_cache_add_ll_entry - RECURSIVE for sorted lists
	---------------------------------------------------
	if a compare function is passed as argument, the element
	is added to the linked list in the sorted order according
	to that compare functions algorithm.  This prepares the list
	for ultra fast indexing - requiring only to walk the list once
	to build the index.
	if no compare function is passed, the element is added
	to the head of the linked list
	the index is created after the linked list is complete,
	and so is always null until explicitly indexed

	*NOTE* this function assumes and *requires* that the structures
	passed to it in "attrval" and "theVal" have a cosIndexedLinkedList
	member, and it is the *first* member of the structure.  This
	is safe because this is a module level function, and all functions
	which call this one are part of the same sub-system.

	The compare function will also make a similar assumption, but
	likely one that recognizes the full structure or type, it is 
	the responsibility of the caller to match input structures with
	the appropriate compare function.

    WARNING: current recursive sorting code never used, never tested
*/
static void cos_cache_add_ll_entry(void** attrval, void *theVal, int ( *compare )(const void *elem1, const void *elem2 ))
{
	static cosIndexedLinkedList *pLastList = 0;
	static cosIndexedLinkedList *first_element;
	static int call_count = 0;

	call_count++;
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_add_ll_entry - recursion level %d\n",call_count,0,0);


	/*
		need to save the first element of the list
		we update this first element whenever an entry
		is added to the start of the list, this way
		we can ensure that the head of the list is always
		*attrval - callers pass us the head of the list
		and can expect that what they get back is also
		the head of the list
	*/
	if(call_count == 1)
		first_element = *attrval;

	if(*attrval)
	{
		if(compare == NULL)
		{
			/* we dont want this list sorted */
			/* push this to the start of the list (because its quick) */
			((cosIndexedLinkedList*)theVal)->pNext = *attrval;
			((cosIndexedLinkedList*)theVal)->index = NULL;
			*attrval = theVal;
		}
		else
		{
			/* insert this in the list in sorted order
			(because its quicker for building indexes later) */
			int comp_ret = 0;

			comp_ret = compare(*attrval, theVal);
			if(comp_ret == 0 || comp_ret > 0)
			{
				/* insert prior to this element */
				if(pLastList)
					pLastList->pNext = theVal;
				else
					first_element = theVal;

				((cosIndexedLinkedList*)theVal)->pNext = *attrval;
				((cosIndexedLinkedList*)theVal)->index = NULL;
			}
			else
			{
				/* still looking - recurse on next element */
				pLastList = (cosIndexedLinkedList*)attrval;
				cos_cache_add_ll_entry(&(((cosIndexedLinkedList*)attrval)->pNext), theVal, compare);
			}

			if(call_count == 1)
				*attrval = first_element;
		}
	}
	else
	{
		/* new or end of list */
		((cosIndexedLinkedList*)theVal)->pNext = NULL;
		((cosIndexedLinkedList*)theVal)->index = NULL;
		if(call_count == 1) /* if new list */
			*attrval = theVal;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_add_ll_entry - recursion level %d\n",call_count,0,0);
	call_count--;
}


/*
	cos_cache_getref
	----------------
	retrieves a reference to the current cache and adds to the cache reference count
*/
int cos_cache_getref(cos_cache **pptheCache)
{
	int ret = -1;
	static int first_time = 1;
	cosCache **ppCache = (cosCache**)pptheCache;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_getref\n",0,0,0);

	if(first_time)
	{
		first_time = 0;
		/* first customer, create the cache */
		slapi_lock_mutex(change_lock);
		if(pCache == NULL)
		{
			if(cos_cache_create())
			{
				/* there was a problem or no COS definitions were found */
				LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_getref: no cos cache created\n",0,0,0);
			}
		}
		slapi_unlock_mutex(change_lock);
	}

	slapi_lock_mutex(cache_lock);

	*ppCache = pCache;

	if(pCache)
		ret = ++((*ppCache)->refCount);
	
	slapi_unlock_mutex(cache_lock);



	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_getref\n",0,0,0);
	return ret;
}

/*
	cos_cache_addref
	----------------
	adds 1 to the cache reference count
*/
int cos_cache_addref(cos_cache *ptheCache)
{
	int ret;
	cosCache *pCache = (cosCache*)ptheCache;
	
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_addref\n",0,0,0);

	slapi_lock_mutex(cache_lock);
	
	if(pCache)
		ret = ++(pCache->refCount);
	
	slapi_unlock_mutex(cache_lock);

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_addref\n",0,0,0);

	return ret;
}


/*
	cos_cache_release
	-----------------
	subtracts 1 from the cache reference count, if the count falls
	below 1, the cache is destroyed.
*/
int cos_cache_release(cos_cache *ptheCache)
{
	int ret = 0;
	int destroy = 0;
	cosCache *pOldCache = (cosCache*)ptheCache;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_release\n",0,0,0);

	slapi_lock_mutex(cache_lock);

	if(pOldCache)
	{
		ret = --(pOldCache->refCount);
		if(ret == 0)
			destroy = 1;
	}

	slapi_unlock_mutex(cache_lock);

	if(destroy)
	{
		cosDefinitions *pDef = pOldCache->pDefs;

		/* now is the first time it is
		 * safe to assess whether
		 * vattr caching can be turned on
		 */
		if(pCache && pCache->vattr_cacheable)
		{
			slapi_vattrcache_cache_all();
		}

		/* destroy the cache here - no locking required, no references outstanding */

		if(pDef)
			cos_cache_del_schema(pOldCache);

		while(pDef)
		{
			cosDefinitions *pTmpD = pDef;
			cosTemplates *pCosTmps = pDef->pCosTmps;

			while(pCosTmps)
			{
				cosTemplates *pTmpT = pCosTmps;
				
				pCosTmps = pCosTmps->list.pNext;

				cos_cache_del_attr_list(&(pTmpT->pAttrs));
				cos_cache_del_attrval_list(&(pTmpT->pObjectclasses));
				cos_cache_del_attrval_list(&(pTmpT->pDn));
				slapi_ch_free((void**)&(pTmpT->cosGrade));
				slapi_ch_free((void**)&pTmpT);
			}

			pDef = pDef->list.pNext;
			
			cos_cache_del_attrval_list(&(pTmpD->pDn));
			cos_cache_del_attrval_list(&(pTmpD->pCosTargetTree));
			cos_cache_del_attrval_list(&(pTmpD->pCosTemplateDn));
			cos_cache_del_attrval_list(&(pTmpD->pCosSpecifier));
			cos_cache_del_attrval_list(&(pTmpD->pCosAttrs));
			cos_cache_del_attrval_list(&(pTmpD->pCosOverrides));
			cos_cache_del_attrval_list(&(pTmpD->pCosOperational));
			cos_cache_del_attrval_list(&(pTmpD->pCosMerge));
			cos_cache_del_attrval_list(&(pTmpD->pCosOpDefault));
			slapi_ch_free((void**)&pTmpD);
		}

		if(pOldCache->ppAttrIndex)
			slapi_ch_free((void**)&(pOldCache->ppAttrIndex));
		if(pOldCache->ppTemplateList)
			slapi_ch_free((void**)&(pOldCache->ppTemplateList));
		slapi_ch_free((void**)&pOldCache);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_release\n",0,0,0);

	return ret;
}


/*
	cos_cache_del_attr_list
	-----------------------
	walk the list deleting as we go
*/
static void cos_cache_del_attr_list(cosAttributes **pAttrs)
{
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_del_attr_list\n",0,0,0);

	while(*pAttrs)
	{
		cosAttributes *pTmp = (*pAttrs)->list.pNext;

		cos_cache_del_attrval_list(&((*pAttrs)->pAttrValue));
		slapi_ch_free((void**)&((*pAttrs)->pAttrName));
		slapi_ch_free((void**)&(*pAttrs));
		*pAttrs = pTmp;
	}


	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_del_attr_list\n",0,0,0);
}


/*
	cos_cache_del_schema
	--------------------
	delete the object class lists used for schema checking
*/
static void cos_cache_del_schema(cosCache *pCache)
{
	char *pLastName = 0;
	int attr_index = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_del_schema\n",0,0,0);

	if(pCache && pCache->attrCount && pCache->ppAttrIndex)
	{
		pLastName = pCache->ppAttrIndex[0]->pAttrName;

		for(attr_index=1; attr_index<pCache->attrCount; attr_index++)
		{
			if(slapi_utf8casecmp((unsigned char*)pCache->ppAttrIndex[attr_index]->pAttrName, (unsigned char*)pLastName))
			{
				/* remember what went before */
				pLastName = pCache->ppAttrIndex[attr_index]->pAttrName;

				/* then blow it away */
				cos_cache_del_attrval_list(&(pCache->ppAttrIndex[attr_index]->pObjectclasses));
			}
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_del_schema\n",0,0,0);
}


/*
	cos_cache_add_attr
	------------------
	Adds an attribute to the list
*/
static int cos_cache_add_attr(cosAttributes **pAttrs, char *name, cosAttrValue *val)
{
	int ret =0;
	cosAttributes *theAttr;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_add_attr\n",0,0,0);

	/* create the attribute */
	theAttr = (cosAttributes*) slapi_ch_malloc(sizeof(cosAttributes));
	if(theAttr)
	{
		theAttr->pAttrValue = val;
		theAttr->pObjectclasses = 0; /* schema issues come later */
		theAttr->pAttrName = slapi_ch_strdup(name);
		if(theAttr->pAttrName)
		{
			cos_cache_add_ll_entry((void**)pAttrs, theAttr, NULL);
			LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_add_attr: Added attribute %s\n",name,0,0);
		}
		else
		{
			slapi_ch_free((void**)&theAttr);
			LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_attr: failed to allocate memory\n",0,0,0);
			ret = -1;
		}
	}
	else
	{
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_attr: failed to allocate memory\n",0,0,0);
		ret = -1;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_add_attr\n",0,0,0);
	return ret;
}


/*
	cos_cache_add_tmpl
	------------------
	Adds a template to the list
*/
static int cos_cache_add_tmpl(cosTemplates **pTemplates, cosAttrValue *dn, cosAttrValue *objclasses, cosAttrValue *pCosSpecifier, cosAttributes *pAttrs,cosAttrValue *cosPriority)
{
	int ret =0;
	cosTemplates *theTemp;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_add_tmpl\n",0,0,0);

	/* create the attribute */
	theTemp = (cosTemplates*) slapi_ch_malloc(sizeof(cosTemplates));
	if(theTemp)
	{
		char *grade = (char*)slapi_ch_malloc(strlen(dn->val)+1);
		int grade_index = 0;
		int index = 0;
		int template_default = 0;

		slapi_dn_normalize(dn->val);

		/* extract the cos grade */
		while(dn->val[index] != '=' && dn->val[index] != '\0')
			index++;
		
		if(dn->val[index] == '=')
		{
			int quotes = 0;

			index++;

			/* copy the grade (supports one level of quote nesting in rdn) */
			while(dn->val[index] != ',' || dn->val[index-1] == '\\' || quotes == 1)
			{
				if(dn->val[index] == '"')
				{
					if(quotes == 0)
						quotes = 1;
					else
						quotes = 0;
				}
				else
				{
					if(dn->val[index] != '\\') /* skip escape chars */
					{
						grade[grade_index] = dn->val[index];
						grade_index++;
					}
				}

				index++;
			}

			grade[grade_index] = '\0';

			/* ok, grade copied, is it the default template? */

			if(pCosSpecifier) /* some schemes don't have one */
			{
				char tmpGrade[BUFSIZ];

				if (strlen(pCosSpecifier->val) < (sizeof(tmpGrade) - 9)) {  /* 9 for "-default" */
					strcpy(tmpGrade, pCosSpecifier->val);
					strcat(tmpGrade, "-default");
					if(!slapi_utf8casecmp((unsigned char*)grade, (unsigned char*)tmpGrade))
						template_default = 1;
				}
				else
				{
					/*
					 * We shouldn't pass ever through this code as we expect
					 * pCosSpecifier values to be reasonably smaller than BUFSIZ
					 *
					 * only 2 lines of code -> no need to set an indirect char *
					 * duplicate the lines of code for clearness instead
					 */
					char * newTmpGrade = PR_smprintf("%s-default", pCosSpecifier->val);
					if(!slapi_utf8casecmp((unsigned char*)grade, (unsigned char*)newTmpGrade))
						template_default = 1;
					PR_smprintf_free(newTmpGrade);
				}
			}

		}
		else
		{
			/* mmm, should never get here, it means the dn is whacky */
			LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_tmpl: malformed dn detected: %s\n",dn,0,0);
			grade[0] = '\0';
		}

		/* now fill in the blanks */
		theTemp->pDn = dn;
		theTemp->pObjectclasses = objclasses;
		theTemp->pAttrs = pAttrs;
		theTemp->cosGrade = slapi_ch_strdup(grade);
		theTemp->template_default = template_default;
		theTemp->cosPriority = (unsigned long)-1;
           
		if(cosPriority)
		{ 
			theTemp->cosPriority = atol(cosPriority->val);
			cos_cache_del_attrval_list(&cosPriority);
		}
		
		cos_cache_add_ll_entry((void**)pTemplates, theTemp, NULL);
		LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_add_tmpl: Added template %s\n",dn->val,0,0);

		slapi_ch_free((void**)&grade);
	}
	else
	{
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_add_tmpl: failed to allocate memory\n",0,0,0);
		ret = -1;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_add_tmpl\n",0,0,0);
	return ret;
}

/*
	cos_cache_attrval_exists
	------------------------
	performs linear search on the list for a
	cosAttrValue that matches val
	however, if the "index" member of cosAttrValue
	is non-null then a binary search is performed
	on the index {PARPAR: TO BE DONE}

	return 1 on found, 0 otherwise
*/
static int cos_cache_attrval_exists(cosAttrValue *pAttrs, const char *val)
{
	int ret = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_attrval_exists\n",0,0,0);

	while(pAttrs)
	{
		if(!slapi_utf8casecmp((unsigned char*)pAttrs->val,(unsigned char*)val))
		{
			ret = 1; 
			break;
		}

		pAttrs = pAttrs->list.pNext;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_attrval_exists\n",0,0,0);
	return ret;
}



static int cos_cache_vattr_get(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *free_flags, void *hint)
{
	int ret = -1;
	cos_cache *pCache = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_vattr_get\n",0,0,0);
	
	if(cos_cache_getref(&pCache) < 1)
	{
		/* problems we are hosed */
		LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_vattr_get: failed to get class of service reference\n",0,0,0);
		goto bail;
	}

	ret = cos_cache_query_attr(pCache, c, e, type, results, NULL, NULL, NULL);
	if(ret == 0)
	{
        *free_flags = SLAPI_VIRTUALATTRS_RETURNED_COPIES;
        *actual_type_name = slapi_ch_strdup(type);
		*type_name_disposition = SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS;
	}
	cos_cache_release(pCache);

bail:

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_vattr_get\n",0,0,0);
	return ret;
}


static int cos_cache_vattr_compare(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_Value *test_this, int* result, int flags, void *hint)
{
	int ret = -1;
	cos_cache *pCache = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_vattr_compare\n",0,0,0);
	
	if(cos_cache_getref(&pCache) < 1)
	{
		/* problems we are hosed */
		LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_vattr_compare: failed to get class of service reference\n",0,0,0);
		goto bail;
	}

	ret = cos_cache_query_attr(pCache, c, e, type, NULL, test_this, result, NULL);

	cos_cache_release(pCache);

bail:

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_vattr_compare\n",0,0,0);
	return ret;
}

/* 
 * this imp is damn slow 
 *
 */
static int cos_cache_vattr_types(vattr_sp_handle *handle,Slapi_Entry *e,
							vattr_type_list_context *type_context,int flags)
{
	int ret = 0;
	int index = 0;
	cosCache *pCache;
	char *lastattr = "thisisfakeforcos";
	int props = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_vattr_types\n",0,0,0);
	
	if(cos_cache_getref((cos_cache **)&pCache) < 1)
	{
		/* problems we are hosed */
		LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_vattr_types: failed to get class of service reference\n",0,0,0);
		goto bail;
	}

	while(index < pCache->attrCount )
	{
		if(slapi_utf8casecmp(
				(unsigned char *)pCache->ppAttrIndex[index]->pAttrName, 
				(unsigned char *)lastattr))
		{
			lastattr = pCache->ppAttrIndex[index]->pAttrName;

			if(1 == cos_cache_query_attr(pCache, NULL, e, lastattr, NULL, NULL,
											 NULL, &props))
			{
				/* entry contains this attr */
				vattr_type_thang thang = {0};

				thang.type_name = lastattr;
				thang.type_flags = props;

				slapi_vattrspi_add_type(type_context,&thang,0);
			}

		}

		index++;
	}

	cos_cache_release(pCache);

bail:

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_vattr_types\n",0,0,0);
	return ret;
}


/*
	cos_cache_query_attr
	--------------------
	queries the cache to determine if this entry
	should have an attribute of "type", and if so,
	returns the attribute values in "vals" - which
	should be subsequently freed by a call to
	cos_cache_query_attr_free()

	returns 
		0 on success, we added a computed attribute
		1 on outright failure
		> LDAP ERROR CODE
		-1 when doesn't know about attribute

	{PARPAR} must also check the attribute does not exist if we are not
	overriding and allow the DS logic to pick it up by denying knowledge
	of attribute
*/
static int cos_cache_query_attr(cos_cache *ptheCache, vattr_context *context, Slapi_Entry *e, char *type, Slapi_ValueSet **out_attr, Slapi_Value *test_this, int *result, int *props)
{
	int ret = -1;
	cosCache *pCache = (cosCache*)ptheCache;
	char *pDn = 0;
	Slapi_Attr *pObjclasses = 0;
	int attr_index = 0;		/* for looping through attributes */
	int attr_matched_index = 0; /* for identifying the matched attribute */
	int hit = 0;
	cosAttributes *pDefAttr = 0;
	Slapi_Value *val;
/*	int type_name_disposition;
	char *actual_type_name; 
	int flags = 0;
	int free_flags;*/
	Slapi_Attr *pTmpVals;
	int using_default = 0;
	int entry_has_value = 0;
	int merge_mode = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_query_attr\n",0,0,0);


	if(out_attr)
		*out_attr = 0;

	/*
		to perform this operation we need to know:
		
		* if we know about the attribute, if not just exit
		--
		* dn,to determine its relevancy to any cos definition,
		  it must be a child of cosTargetTree
		--
		* objectclasses,to determine if the cos attribute will
		  violate schema (only when schema checking is on)
		--
		* class of service specifier, for matching definitions
		  - cosSpecifier is the attribute name and is used to
		  determine the cosDefinition to use, its value determines
		  the template to use
		--
		* the cosAttribute(s) (from the cosDefinition(s)) that match
		the attribute name.
		($$)If these have a postfix of "default", then it is the same
		as no postfix i.e. this acts as the default value.  If it has
		a postfix of "override", then the value in the matching template
		is used regardless of any value stored in the entry.  This has
		been worked out previously so we can use a bool indicator in
		the cosDefinition structure to determine what to do.
		--
		* the value of the attribute in the entry -
		  if present it overrides any default template value (see $$)

		Also this ordering ensures least work done to fail (in this
		implementation)
	*/

	/** attribute **/
	/*
		lets be sure we need to do something
		most of the time we probably don't
	*/
	attr_index = cos_cache_find_attr(pCache, type);
	if(attr_index == -1)
	{
		/* we don't know about this attribute */
		goto bail;
	}

	/* 
		if there is a value in the entry the outcome
		of the cos attribute resolution may be different
	*/
	slapi_entry_attr_find(e, type, &pTmpVals);
	if(pTmpVals)
		entry_has_value = 1;

	/** dn **/
	pDn = slapi_entry_get_dn(e);
	
	if(pDn == 0)
	{
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_query_attr: failed to get entry dn\n",0,0,0);
		ret = 1;
		goto bail;
	}
        
	slapi_dn_normalize( pDn );

	/** objectclasses **/
	if(pCache->ppAttrIndex[attr_index]->attr_operational == 0 && config_get_schemacheck() &&
		pCache->ppAttrIndex[attr_index]->attr_operational_default == 0)
	{
		/* does this entry violate schema? */

		if(slapi_entry_attr_find( e, "objectclass", &pObjclasses ))
		{
			LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_query_attr: failed to get objectclass from %s\n",pDn,0,0);
			goto bail;
		}

		if(!cos_cache_schema_check(pCache, attr_index, pObjclasses))
		{
			LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_query_attr: cos attribute %s failed schema check on dn: %s\n",type,pDn,0);
			goto bail;
		}
	}

	/** class of service specifier **/
	/* 
		now we need to iterate through the attributes to discover
		if one fits all the criteria, we'll take the first that does
		and blow off the rest
	*/
	do
	{
		/* for convenience, define some pointers */
		cosAttributes *pAttr = pCache->ppAttrIndex[attr_index]; 
		cosTemplates *pTemplate = (cosTemplates*)pAttr->pParent;
		cosDefinitions *pDef = (cosDefinitions*)pTemplate->pParent;
		cosAttrValue *pTargetTree = pDef->pCosTargetTree;

		/* now for the tests */

		/* would we be allowed to supply this attribute if we had one? */
		if(entry_has_value && pAttr->attr_override == 0 && pAttr->attr_operational == 0)
		{
			/* answer: no, move on to the next attribute */
			attr_index++;
			continue;
		}

		/* if we are in merge_mode, can the attribute be merged? */
		if(merge_mode && pAttr->attr_cos_merge == 0)
		{
			/* answer: no, move on to the next attribute */
			attr_index++;
			continue;
		}

		/* is this entry a child of the target tree(s)? */
		do
		{
			if(pTargetTree)
				slapi_dn_normalize( pTargetTree->val );

    		if(	pTargetTree->val == 0 || 
				slapi_dn_issuffix(pDn, pTargetTree->val) != 0 || 
				(views_api && views_entry_exists(views_api, pTargetTree->val, e)) /* might be in a view */
				)
			{
				cosAttrValue *pSpec = pDef->pCosSpecifier;
				Slapi_ValueSet *pAttrSpecs = 0;


				/* Does this entry have a correct cosSpecifier? */
				do
				{
					int type_name_disposition = 0;
					char *actual_type_name = 0;
					int free_flags = 0;

					if(pSpec && pSpec->val) {
						ret = slapi_vattr_values_get_sp(context, e, pSpec->val, &pAttrSpecs, &type_name_disposition, &actual_type_name, 0, &free_flags);
						/* MAB: We need to free actual_type_name here !!! 
						XXX BAD--should use slapi_vattr_values_free() */	
						slapi_ch_free((void **) &actual_type_name);
						if (SLAPI_VIRTUALATTRS_LOOP_DETECTED == ret) {
							ret = LDAP_UNWILLING_TO_PERFORM;
							goto bail;
						}
					}

					if(pAttrSpecs || pDef->cosType == COSTYPE_POINTER)
					{
						int index = 0;

						/* does the cosSpecifier value correspond to this template? */
						if(pDef->cosType == COSTYPE_INDIRECT)
						{
							/*
								it always does correspond for indirect schemes (it's a dummy value) 
								now we must follow the dn of our pointer and retrieve a value to
								return
								Note: we support one dn only, the result of multiple pointers is undefined
							*/
							Slapi_Value *indirectdn;
							int pointer_flags = 0;

							slapi_valueset_first_value( pAttrSpecs, &indirectdn );

							if(props)
								pointer_flags = *props;

							if( indirectdn != NULL &&
								!cos_cache_follow_pointer( context, (char*)slapi_value_get_string(indirectdn), type, out_attr, test_this, result, pointer_flags))
								hit = 1;
						}
						else
						{
							if(pDef->cosType != COSTYPE_POINTER)
								index = slapi_valueset_first_value( pAttrSpecs, &val );

							while(pDef->cosType == COSTYPE_POINTER || val)
							{
								if(pDef->cosType == COSTYPE_POINTER || !slapi_utf8casecmp((unsigned char*)pTemplate->cosGrade, (unsigned char*)slapi_value_get_string(val)))
								{
									/* we have a hit */


									if(out_attr)
									{
										if(cos_cache_cos_2_slapi_valueset(pAttr, out_attr) == 0)
											hit = 1;
										else
										{
											LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_query_attr: could not create values to return\n",0,0,0);
											goto bail;
										}

										if(pAttr->attr_cos_merge)
										{
											merge_mode = 1;
											attr_matched_index = attr_index;
										}
									}
									else
									{
										if(test_this && result)
										{
											/* compare op */
											if(cos_cache_cmp_attr(pAttr, test_this, result))
											{
												hit = 1;
											}
										}
										else
										{
											/* well, this must be a request for type only */
											hit = 1;
										}
									}

									break;
								}

								if(pDef->cosType != COSTYPE_POINTER)
									index = slapi_valueset_next_value( pAttrSpecs, index, &val );
							}
						}
					}

					if(pSpec)
						pSpec = pSpec->list.pNext;

				} while(hit == 0 && pSpec);

				/* MAB: We need to free pAttrSpecs here !!! 
				XXX BAD--should use slapi_vattr_values_free()*/
				slapi_valueset_free(pAttrSpecs);

				/* is the cosTemplate the default template? */
				if(hit == 0 && pTemplate->template_default && !pDefAttr)
				{
					/* then lets save the attr in case we need it later */
					pDefAttr = pAttr;
				}
			}

			pTargetTree = pTargetTree->list.pNext;

		} while(hit == 0 && pTargetTree);


		if(hit==0 || merge_mode)
			attr_index++;

	} while(
				(hit == 0 || merge_mode) && 
				pCache->attrCount > attr_index && 
				!slapi_utf8casecmp((unsigned char*)type, (unsigned char*)pCache->ppAttrIndex[attr_index]->pAttrName)
				);

	if(!merge_mode)
		attr_matched_index = attr_index;

	/* should we use a default value? */
	if(hit == 0 && pDefAttr)
	{
		/* we have a hit */

		using_default = 1;

		if(out_attr)
		{
			if(cos_cache_cos_2_slapi_valueset(pDefAttr, out_attr) == 0)
				hit = 1;
			else
			{
				LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_query_attr: could not create values to return\n",0,0,0);
				goto bail;
			}
		}
		else
		{
			if(test_this && result)
			{
				/* compare op */
				if(cos_cache_cmp_attr(pDefAttr, test_this, result))
					hit = 1;
			}
			else
			{
				/* well, this must be a request for type only and the entry gets default template value */
				hit = 1;
			}
		}
	}

	if(hit == 1 && out_attr == NULL && test_this == NULL)
		ret = 1;
	else if(hit == 1)
		ret = 0;
	else
		ret = -1;

	if(props)
		*props = 0;

	if(hit == 1 && props && pDefAttr) {
		if (
		((using_default && pDefAttr->attr_operational == 1) ||
		(!using_default && pCache->ppAttrIndex[attr_matched_index]->attr_operational == 1)) ||
		((using_default && pDefAttr->attr_operational_default == 1) ||
		(!using_default && pCache->ppAttrIndex[attr_matched_index]->attr_operational_default == 1)) )
	{
		/* this is an operational attribute, lets mark it so */
		*props |= SLAPI_ATTR_FLAG_OPATTR;
	}
	}

bail:

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_query_attr\n",0,0,0);
	return ret;
}

/*
	cos_cache_find_attr
	-------------------
	searches for the attribute "type", and if found returns the index
	of the first occurrance of the attribute in the cache top level
	indexed attribute list.
*/
static int cos_cache_find_attr(cosCache *pCache, char *type)
{
	int ret = -1;  /* assume failure */
	cosAttributes attr;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_find_attr\n",0,0,0);

	attr.pAttrName = type;

	if(pCache->attrCount-1 != 0)
		ret = cos_cache_attr_index_bsearch(pCache, &attr, 0, pCache->attrCount-1);
	else
	{
		/* only one attribute (that will fool our bsearch) lets check it here */
		if(!slapi_utf8casecmp((unsigned char*)type, (unsigned char*)(pCache->ppAttrIndex)[0]->pAttrName))
		{
			ret = 0;
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_find_attr\n",0,0,0);
	return ret;
}


/*
	cos_cache_schema_check
	----------------------
	check those object classes which match in the input list and the
	cached set for allowed attribute types

	return non-null for schema matches, zero otherwise
*/
static int cos_cache_schema_check(cosCache *pCache, int attr_index, Slapi_Attr *pObjclasses)
{
	int ret = 0; /* assume failure */
	Slapi_Value *val;
	int hint;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_schema_check\n",0,0,0);

	hint = slapi_attr_first_value( pObjclasses, &val );
	while(hint != -1) 
	{
		ret = cos_cache_attrval_exists(pCache->ppAttrIndex[attr_index]->pObjectclasses, (char*) slapi_value_get_string(val));
		if(ret)
			break;
	
		hint = slapi_attr_next_value( pObjclasses, hint,  &val );
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_schema_check\n",0,0,0);
	return ret;
}

/*
	cos_cache_schema_build
	----------------------
	For each attribute in our global cache add the objectclasses which allow it.
	This may be referred to later to check schema is not being violated.
*/
static int cos_cache_schema_build(cosCache *pCache)
{
	int ret = 0; /* we assume success, with operational attributes not supplied in schema we might fail otherwise */
	struct objclass	*oc;
	char *pLastName = 0;
	cosAttrValue *pLastRef = 0;
	int attr_index = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_schema_build\n",0,0,0);

	if(!config_get_schemacheck())
		ret = 0;

	/* 
		it is expected that in all but the most hard core cases, the number of
		objectclasses will out number the attributes we look after - so we make
		the objectclasses the outside loop.  However, even if they are not, we
		perform binary searches on the attribute list anyway, so it should be
		considerably faster to search than the linked list of objectclasses (even
		with the string comparisons going on)
	*/
    oc_lock_read();
	for ( oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next )
	{
		char **pppAttrs[2];
		int index;
		int attrType = 0;

		pppAttrs[0] = oc->oc_required;
		pppAttrs[1] = oc->oc_allowed;

		/* we need to check both required and allowed attributes I think */
		while(attrType < 2)
		{
			if(pppAttrs[attrType])
			{
				index = 0;

				while(pppAttrs[attrType][index])
				{
					attr_index = cos_cache_find_attr(pCache, pppAttrs[attrType][index]);
					if(attr_index != -1)
					{
						/*
							this attribute is one of ours, add this
							objectclass to the objectclass list
							note the index refers to the first
							occurrence of this attribute in the list,
							later we will copy over references to this
							list to all the other attribute duplicates.
						*/

						cos_cache_add_attrval(&(pCache->ppAttrIndex[attr_index]->pObjectclasses), oc->oc_name);
						ret = 0;
					}
					index++;
				}
			}

			attrType++;
		}
	}
    oc_unlock();

	/*
		OK, now we need to add references to the real
		lists to the duplicate attribute entries.
		(this allows the schema check to be a little
		less complex and just a little quicker)
	*/
	pLastName = pCache->ppAttrIndex[0]->pAttrName;
	pLastRef = pCache->ppAttrIndex[0]->pObjectclasses;

	for(attr_index=1; attr_index<pCache->attrCount; attr_index++)
	{
		if(!slapi_utf8casecmp((unsigned char*)pCache->ppAttrIndex[attr_index]->pAttrName, (unsigned char*)pLastName))
		{
			/* copy over reference */
			pCache->ppAttrIndex[attr_index]->pObjectclasses = pLastRef;
		}
		else
		{
			/* remember what went before */
			pLastName = pCache->ppAttrIndex[attr_index]->pAttrName;
			pLastRef = pCache->ppAttrIndex[attr_index]->pObjectclasses;
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_schema_build\n",0,0,0);
	return ret;
}


/*
	cos_cache_index_all
	-------------------
	Indexes every attribute in the cache for fast binary lookup
	on attributes from the top level of the cache.
	Also fixes up all parent pointers so that a single attribute
	lookup will allow access to all information regarding that attribute.
	Attributes that appear more than once in the cache will also
	be indexed more than once - this means that a pure binary
	search is not possible, but it is possible to make use of a
	duplicate entry aware binary search function - which are rare beasts,
	so we'll need to provide cos_cache_attr_bsearch()

	This is also a handy time to mark the attributes as overides if
	necessary
*/

static int cos_cache_index_all(cosCache *pCache)
{
	int ret = -1;
	
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_index_all\n",0,0,0);

	/*
		first fixup the index array so we can use qsort()
		also fixup the parent pointers
	*/

	pCache->ppTemplateList = 0;
	pCache->templateCount = 0;
	pCache->ppAttrIndex = 0;

	pCache->attrCount = cos_cache_total_attr_count(pCache);
	if(pCache->attrCount && pCache->templateCount)
	{
		int tmpindex = 0;
		int cmpindex = 0;
		int actualCount = 0;

		pCache->ppAttrIndex = (cosAttributes**)slapi_ch_malloc(sizeof(cosAttributes*) * pCache->attrCount);
		pCache->ppTemplateList = (char**)slapi_ch_calloc((pCache->templateCount + 1) * 2, sizeof(char*));
		if(pCache->ppAttrIndex && pCache->ppTemplateList)
		{
			int attrcount = 0;
			cosDefinitions *pDef = pCache->pDefs;
			cosAttrValue *pAttrVal = 0;

			while(pDef)
			{
				cosTemplates *pCosTmps = pDef->pCosTmps;

				while(pCosTmps)
				{
					cosAttributes *pAttrs = pCosTmps->pAttrs;

					pCosTmps->pParent = pDef;
		
					while(pAttrs)
					{
						pAttrs->pParent = pCosTmps;
						(pCache->ppAttrIndex)[attrcount] = pAttrs;

						if(cos_cache_attrval_exists(pDef->pCosOverrides, pAttrs->pAttrName))
							pAttrs->attr_override = 1;
						else
							pAttrs->attr_override = 0;

						if(cos_cache_attrval_exists(pDef->pCosOperational, pAttrs->pAttrName))
							pAttrs->attr_operational = 1;
						else
							pAttrs->attr_operational = 0;

						if(cos_cache_attrval_exists(pDef->pCosMerge, pAttrs->pAttrName))
							pAttrs->attr_cos_merge = 1;
						else
							pAttrs->attr_cos_merge = 0;

						if(cos_cache_attrval_exists(pDef->pCosOpDefault, pAttrs->pAttrName))
							pAttrs->attr_operational_default = 1;
						else
							pAttrs->attr_operational_default = 0;

						attrcount++;

						pAttrs = pAttrs->list.pNext;
					}

					pCosTmps = pCosTmps->list.pNext;
				}

				/*
					we need to build the template dn list too,
					we are going to take care that we do not
					add duplicate dns or dns that have
					ancestors elsewhere in the list since this
					list will be binary searched (with a special
					BS alg) to find an ancestor tree for a target
					that has been modified - that comes later in
					this function however - right now we'll just
					slap them in the list
				*/
				pAttrVal = pDef->pCosTemplateDn;

				while(pAttrVal)
				{
					slapi_dn_normalize(pAttrVal->val);
					pCache->ppTemplateList[tmpindex] = pAttrVal->val;

					tmpindex++;
					pAttrVal = pAttrVal->list.pNext;
				}

				pDef = pDef->list.pNext;
			}

			/* now sort the index array */
			qsort(pCache->ppAttrIndex, attrcount, sizeof(cosAttributes*), cos_cache_attr_compare);
			qsort(pCache->ppTemplateList, tmpindex, sizeof(char*), cos_cache_string_compare);

			/*
				now we have the sorted template dn list, we can get rid of
				duplicates and entries that have an ancestor elsewhere in
				the list - all this in the name of faster searches
			*/

			/* first go through zapping the useless  PARPAR - THIS DOES NOT WORK */
			tmpindex = 1;
			cmpindex = 0;
			actualCount = pCache->templateCount;

			while(tmpindex < pCache->templateCount)
			{
				if(
					!slapi_utf8casecmp((unsigned char*)pCache->ppTemplateList[tmpindex],(unsigned char*)pCache->ppTemplateList[cmpindex]) ||
					slapi_dn_issuffix(pCache->ppTemplateList[tmpindex], pCache->ppTemplateList[cmpindex])
					)
				{
					/* this guy is a waste of space */
					pCache->ppTemplateList[tmpindex] = 0;
					actualCount--;
				}
				else
					cmpindex = tmpindex;

				tmpindex++;
			}

			/* now shuffle everything up to the front to cover the bald spots */
			tmpindex = 1;
			cmpindex = 0;

			while(tmpindex < pCache->templateCount)
			{
				if(pCache->ppTemplateList[tmpindex] != 0)
				{
					if(cmpindex)
					{
						pCache->ppTemplateList[cmpindex] = pCache->ppTemplateList[tmpindex];
						pCache->ppTemplateList[tmpindex] = 0;
						cmpindex++;
					}
				}
				else
				{
					if(cmpindex == 0)
						cmpindex = tmpindex;
				}
					
				tmpindex++;
			}
			
			pCache->templateCount = actualCount;

			LDAPDebug( LDAP_DEBUG_PLUGIN, "cos: cos cache index built\n",0,0,0);
			
			ret = 0;
		}
		else
		{
			if(pCache->ppAttrIndex)
				slapi_ch_free((void**)(&pCache->ppAttrIndex));

			if(pCache->ppTemplateList)
				slapi_ch_free((void**)(&pCache->ppTemplateList));

			LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_index_all: failed to allocate index memory\n",0,0,0);
		}
	}
	else
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_index_all: no attributes to index\n",0,0,0);
	
	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_index_all\n",0,0,0);
	return ret;
}


/*
	cos_cache_total_attr_count
	--------------------------
	walks the entire cache counting all attributes
	note: this is coded so that it may be called
	prior to the cache indexing of attributes - in
	fact it is called by the code that creates the
	index.  Once indexing has been performed, it is
	*much* *much* faster to get the count from the
	cache object itself - cosCache::attrCount.

	Additionally - a side effect is that the template
	target trees are counted and placed in the cache level
	target tree count - probably should be renamed,
	but lets let it slide for now

	returns the number of attributes counted
*/
static int cos_cache_total_attr_count(cosCache *pCache)
{
	int count = 0;
	cosDefinitions *pDef = pCache->pDefs;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_total_attr_count\n",0,0,0);

	pCache->templateCount = 0;

	while(pDef)
	{
		cosTemplates *pCosTmps = pDef->pCosTmps;

		while(pCosTmps)
		{
			cosAttributes *pAttrs = pCosTmps->pAttrs;
			
			while(pAttrs)
			{
				count++;
				pAttrs = pAttrs->list.pNext;
			}

			pCache->templateCount++;
			pCosTmps = pCosTmps->list.pNext;
		}

		pDef = pDef->list.pNext;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_total_attr_count\n",0,0,0);
	return count;
}


static int cos_cache_attr_compare(const void *e1, const void *e2)
{
	int com_Result;
	cosAttributes *pAttr = (*(cosAttributes**)e1);
	cosTemplates *pTemplate = (cosTemplates*)pAttr->pParent;
    cosDefinitions *pDef = (cosDefinitions*)pTemplate->pParent;
	cosAttrValue *pcostt = pDef->pCosTargetTree;
	cosAttributes *pAttr1 = (*(cosAttributes**)e2);
	cosTemplates *pTemplate1 = (cosTemplates*)pAttr1->pParent;
	cosDefinitions *pDef1 = (cosDefinitions*)pTemplate1->pParent;
    cosAttrValue *pcostt1 = pDef1->pCosTargetTree;
        
	/* Now compare the names of the attributes */
	com_Result = slapi_utf8casecmp((unsigned char*)(*(cosAttributes**)e1)->pAttrName,(unsigned char*)(*(cosAttributes**)e2)->pAttrName);
	if(0 == com_Result)
	/* Now compare the definition Dn parents */
      com_Result = slapi_utf8casecmp((unsigned char*)pcostt1->val,(unsigned char*)pcostt->val);   
	  if(0 == com_Result)
	/* Now compare the cosPririoties */     
	     com_Result = pTemplate->cosPriority - pTemplate1->cosPriority;
	/* Now compare the prirority */ 
		  if(0 == com_Result)
	            return -1;   
		  return com_Result;
}

static int cos_cache_string_compare(const void *e1, const void *e2)
{
	return slapi_utf8casecmp((*(unsigned char**)e1),(*(unsigned char**)e2));
}

static int cos_cache_template_index_compare(const void *e1, const void *e2)
{
	int ret = 0;

	if(0 == slapi_dn_issuffix((const char*)e1,*(const char**)e2))
		ret = slapi_utf8casecmp(*(unsigned char**)e2,(unsigned char*)e1);
	else
		ret = 0;

	return ret;
}

/*
	cos_cache_template_index_bsearch
	--------------------------------
	searches the template dn index for a match
*/
static int cos_cache_template_index_bsearch(const char *dn)
{
	int ret = 0;
	cosCache *pCache;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_template_index_bsearch\n",0,0,0);
	
	if(-1 != cos_cache_getref((cos_cache**)&pCache))
	{
		if(bsearch(dn, pCache->ppTemplateList, pCache->templateCount, sizeof(char*), cos_cache_template_index_compare))
			ret = 1;

		cos_cache_release((cos_cache*)pCache);
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_template_index_bsearch\n",0,0,0);

	return ret;
}

/*
	cos_cache_attr_index_bsearch - RECURSIVE
	----------------------------------------
	performs a binary search on the cache attribute index
	return -1 if key is not found
	the index into attribute index array of the first occurrance
	of that attribute type otherwise
*/
static int cos_cache_attr_index_bsearch( const cosCache *pCache, const cosAttributes *key, int lower, int upper )
{
	int ret = -1;
	int index = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_attr_index_bsearch\n",0,0,0);

	if(upper >= lower)
	{
		if(upper != 0)
			index = ((upper-lower)/2) + lower;
		else
			index = 0;

		ret = slapi_utf8casecmp((unsigned char*)key->pAttrName, (unsigned char*)(pCache->ppAttrIndex)[index]->pAttrName);
		if(ret == 0)
		{
			/* 
				we have a match, backtrack to the 
				first occurrance of this attribute
				type
			*/
			do
			{
				index--;
				if(index >= 0)
					ret = slapi_utf8casecmp((unsigned char*)key->pAttrName, (unsigned char*)(pCache->ppAttrIndex)[index]->pAttrName);
			} while(index >= 0 && ret == 0);
			
			index++;
		}
		else 
		{
			/* seek elsewhere */
			if(ret < 0)
			{
				/* take the low road */
				index = cos_cache_attr_index_bsearch(pCache, key, lower, index-1);
			}
			else
			{
				/* go high */
				index = cos_cache_attr_index_bsearch(pCache, key, index+1, upper);
			}
		}
	}
	else
		index = -1;

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_attr_index_bsearch\n",0,0,0);
	return index;
}


static int cos_cache_cmp_attr(cosAttributes *pAttr, Slapi_Value *test_this, int *result)
{
	int ret = 0;
	int index = 0;
	cosAttrValue *pAttrVal = pAttr->pAttrValue;
	char *the_cmp = (char *)slapi_value_get_string(test_this);

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_cmp_attr\n",0,0,0);
	
	*result = 0;

	while( pAttrVal )
	{
		if(!slapi_utf8casecmp((unsigned char*)the_cmp, (unsigned char*)pAttrVal->val))
		{
			/* compare match */
			*result = 1;
			break;
		}

		pAttrVal = pAttrVal->list.pNext;
		index++;
	} 

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_cmp_attr\n",0,0,0);
	return ret;
}


/*
	cos_cache_cos_2_slapi_attr
	----------------------
	converts a cosAttributes structure to a Slapi_Attribute
*/
static int cos_cache_cos_2_slapi_valueset(cosAttributes *pAttr, Slapi_ValueSet **out_vs)
{
	int ret = 0;
	int index = 0;
	cosAttrValue *pAttrVal = pAttr->pAttrValue;
	int add_mode = 0;
	static Slapi_Attr *attr = 0; /* allocated once, never freed */
	static int done_once = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_cos_2_slapi_attr\n",0,0,0);
	
	/* test out_vs for existing values */
	if(*out_vs)
	{
		add_mode = 1;
		if(!done_once)
		{
			attr = slapi_attr_new();  /* lord knows why this is needed by slapi_valueset_find*/
			slapi_attr_init(attr, "cos-bogus");
			done_once = 1;
		}
	}
	else
		*out_vs = slapi_valueset_new();
	
	if(*out_vs)
	{
		if(!add_mode)
			slapi_valueset_init(*out_vs);

		while( pAttrVal )
		{
			Slapi_Value *val = slapi_value_new_string(pAttrVal->val);
			if(val) {
				if(!add_mode || !slapi_valueset_find(attr, *out_vs, val)) {
					slapi_valueset_add_value_ext(*out_vs, val, SLAPI_VALUE_FLAG_PASSIN);
				}
				else {
					slapi_value_free(&val);
				}
			}
			else
			{
				LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_cos_2_slapi_attr: memory allocation error\n",0,0,0);
				ret = -1;
				goto bail;
			}

			pAttrVal = pAttrVal->list.pNext;
			index++;
		} 
	}
	else
	{
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_cos_2_slapi_attr: memory allocation error\n",0,0,0);
		ret = -1;
	}

bail:
	
	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_cos_2_slapi_attr\n",0,0,0);
	return ret;
}


/*
	cos_cache_change_notify
	-----------------------
	determines if the change effects the cache and if so
	signals a rebuild.

	XXXrbyrne This whole mechanism needs to be revisited--it means that
	the modifying client gets his LDAP response, and an unspecified and
	variable
	period of time later, his mods get taken into account in the cos cache.
	This makes it hard to program reliable admin tools for COS--DSAME
	has already indicated this is an issue for them.
	Additionally, it regenerates the _whole_ cache even for eeny weeny mods--
	does it really neeed to ?  Additionally, in order to ensure we
	do not miss any mods, we may tend to regen the cache, even if we've already
	taken a mod into account in an earlier regeneration--currently there is no
	way to know we've already dealt with the mod.
	The right thing is something like: figure out what's being changed
	and change only that in the cos cache and do it _before_ the response
	goes to the client....or do a task that he can poll.
*/
void cos_cache_change_notify(Slapi_PBlock *pb)
{
	char *dn;
	int do_update = 0;
	struct slapi_entry *e;
        Slapi_Backend *be=NULL;
	int rc = 0;
	int optype = -1;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_change_notify\n",0,0,0);

	/* Don't update local cache when remote entries */
	/* are updated.					*/
	slapi_pblock_get( pb, SLAPI_BACKEND, &be );
	if ( ( be!=NULL ) && (slapi_be_is_flag_set(be,SLAPI_BE_FLAG_REMOTE_DATA)))
		goto bail;

	/* need to work out if a cache rebuild is necessary */
	if(slapi_pblock_get( pb, SLAPI_TARGET_DN, &dn ))
	{
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_change_notify: failed to get dn of changed entry",0,0,0);
		goto bail;
	}

	slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &rc);
	if (0 != rc) {
		/* The operation did not succeed. As far as the cos cache is concerned, no need to update anything */
		goto bail;
	}

	/*
	 * For DELETE, MODIFY, MODRDN: see if the pre-op entry was cos significant.
	 * For ADD, MODIFY, MODRDN: see if the post-op was cos significant.
	 * Touching a cos significant entry triggers the update
	 * of the whole cache.
	*/
	slapi_pblock_get ( pb, SLAPI_OPERATION_TYPE, &optype );	
	if ( optype == SLAPI_OPERATION_DELETE ||
		 optype == SLAPI_OPERATION_MODIFY ||
		 optype == SLAPI_OPERATION_MODRDN ) {
	       
		slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e);
		if ( cos_cache_entry_is_cos_related(e)) {
			do_update = 1;
		}
    }
	if ( !do_update && 
		(optype == SLAPI_OPERATION_ADD ||
		 optype == SLAPI_OPERATION_MODIFY ||
		 optype == SLAPI_OPERATION_MODRDN )) {
        
		/* Adds have null pre-op entries */
		slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &e);
		if ( cos_cache_entry_is_cos_related(e)) {
				do_update = 1;
		}			
	}

	/*
	 * Check if this was an entry in a template tree (dn contains
	 * the old dn value).
	 * It's only relevant for indirect templates, which will
	 * not usually contain the "objectclass: costemplate" pair
	 * and so will not get detected by the above code.
	 * In fact, everything would still work fine if
	 * we just ignored a mod of one of these indirect templates,
	 * as we do not cache values from them, but the advantage of
	 * triggering an update here is that
	 * we can maintain the invariant that we only ever cache
	 * definitions that have _valid_ templates--the active cache
	 * stays lean in the face of errors.
	*/
	if( !do_update && cos_cache_template_index_bsearch(dn)) {
			LDAPDebug( LDAP_DEBUG_PLUGIN, "cos_cache_change_notify:"
				"updating due to indirect template change(%s)\n",
				dn,0,0);
		do_update = 1;
	}

	/* Do the update if required */
	if(do_update)
	{
		slapi_lock_mutex(change_lock);
		slapi_notify_condvar( something_changed, 1 );
		cos_cache_notify_flag = 1;
		slapi_unlock_mutex(change_lock);
	}

bail:
	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_change_notify\n",0,0,0);
}

/*
	cos_cache_stop
	--------------
	notifies the cache thread we are stopping
*/
void cos_cache_stop()
{
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_stop\n",0,0,0);

	/* first deregister our state change func */
	slapi_unregister_backend_state_change((void *)cos_cache_backend_state_change);

	slapi_lock_mutex(change_lock);
	keeprunning = 0;
	slapi_notify_condvar( something_changed, 1 );
	slapi_unlock_mutex(change_lock);

	/* wait on shutdown */
	slapi_lock_mutex(stop_lock);

	/* release the caches reference to the cache */
	cos_cache_release(pCache);

	slapi_destroy_mutex(cache_lock);
	slapi_destroy_mutex(change_lock);
	slapi_destroy_condvar(something_changed);

	slapi_unlock_mutex(stop_lock);
	slapi_destroy_mutex(stop_lock);
	slapi_destroy_condvar(start_cond);
	slapi_destroy_mutex(start_lock);

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_stop\n",0,0,0);
}

/*
	cos_cache_backwards_stricmp_and_clip
	------------------------------------
	compares s2 to s1 starting from end of the strings until the beginning of
	either matches result in the s2 value being clipped from s1 with a NULL char
	and 1 being returned as opposed to 0

*/
static int cos_cache_backwards_stricmp_and_clip(char*s1,char*s2)
{
	int ret = 0;
	int s1len = 0;
	int s2len = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_cache_backwards_stricmp_and_clip\n",0,0,0);

	s1len = strlen(s1);
	s2len = strlen(s2);

	if(s1len > s2len && s2len > 0)
	{
		while(s1len > -1 && s2len > -1)
		{
			s1len--;
			s2len--;

			if(s1[s1len] != s2[s2len])
				break;
			else
			{
				if(s2len == 0)
				{
					/* hit! now clip */
					ret = 1;
					s1[s1len] = '\0';
				}
			}
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_cache_backwards_stricmp_and_clip\n",0,0,0);
	return ret;
}


static int cos_cache_follow_pointer( vattr_context *c, const char *dn, char *type, Slapi_ValueSet **out_vs, Slapi_Value *test_this, int *result, int flags)
{
	int ret = -1;  /* assume failure */
	Slapi_PBlock *pDnSearch = 0;
	Slapi_Entry **pEntryList = 0;
	char *attrs[2];
	int op = 0;
	int type_test = 0;
	int type_name_disposition = 0;
	char *actual_type_name = 0;
	int free_flags = 0;
	Slapi_ValueSet *tmp_vs = 0;
	
	attrs[0] = type;
	attrs[1] = 0;

        /* Use new internal operation API */
        pDnSearch = slapi_pblock_new();
        if (pDnSearch) {
                slapi_search_internal_set_pb(pDnSearch, dn, LDAP_SCOPE_BASE,"(|(objectclass=*)(objectclass=ldapsubentry))",attrs,
			0,NULL,NULL,cos_get_plugin_identity(),0);
                slapi_search_internal_pb(pDnSearch);
                slapi_pblock_get( pDnSearch, SLAPI_PLUGIN_INTOP_RESULT, &ret);
        }

	if(pDnSearch && (ret == LDAP_SUCCESS))
	{
		ret = -1;

		slapi_pblock_get( pDnSearch, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &pEntryList);
		if(pEntryList)
		{
			if(out_vs) /* if this set, a value is required */
				op = 1;
			else if(test_this && result)  /* compare op */
				op = 2;
			else
			{
				/* requires only type present */
				op = 1;
				type_test = 1;
			}

			switch(op)
			{
				case 1:
				/* straight value return or type test */
				if(type_test)
					out_vs = &tmp_vs;

				ret = slapi_vattr_values_get_sp(c, pEntryList[0], type, out_vs,&type_name_disposition, &actual_type_name, flags, &free_flags);

				if(actual_type_name)
					slapi_ch_free((void **) &actual_type_name);

				if(type_test && free_flags == SLAPI_VIRTUALATTRS_RETURNED_COPIES)
					slapi_valueset_free(*out_vs);
			
				break;

				case 2:
				/* this must be a compare op */
				ret = slapi_vattr_value_compare_sp(c, pEntryList[0],type, test_this,  result, flags);
				break;

			default:
				goto bail;
			}
		}
	}

bail:
	/* clean up */
	if(pDnSearch)
	{
		slapi_free_search_results_internal(pDnSearch);
		slapi_pblock_destroy(pDnSearch);
	}

	return ret;
}


/*
 * cos_cache_backend_state_change()
 * --------------------------------
 * This is called when a backend changes state
 * We simply signal to rebuild the cos cache in this case
 *
 */
void cos_cache_backend_state_change(void *handle, char *be_name, 
     int old_be_state, int new_be_state) 
{
	slapi_lock_mutex(change_lock);
	slapi_notify_condvar( something_changed, 1 );
	slapi_unlock_mutex(change_lock);
}

/*
 * returns non-zero: entry is cos significant (note does not detect indirect
 *					template entries).
 * 			0	   : entry is not cos significant.
*/
static int cos_cache_entry_is_cos_related( Slapi_Entry *e) {

	int rc = 0;
	Slapi_Attr *pObjclasses = NULL;

	if ( e == NULL ) {
		LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_change_notify:"
				"modified entry is NULL--updating cache just in case!",
				0,0,0);
		rc = 1;
	} else {

		if(slapi_entry_attr_find( e, "objectclass", &pObjclasses ))
		{
			LDAPDebug( LDAP_DEBUG_ANY, "cos_cache_change_notify:"
						" failed to get objectclass from %s",
						slapi_entry_get_dn(e),0,0);
			rc = 0;
		} else {

			Slapi_Value *val = NULL;
			int index = 0;		
			char *pObj;	

			/* check out the object classes to see if this was a cosDefinition */		

			index = slapi_attr_first_value( pObjclasses, &val );
			while(!rc && val)
			{
				pObj = (char*)slapi_value_get_string(val);

				/*
				 * objectclasses are ascii--maybe strcasecmp() is faster than
				 * slapi_utf8casecmp()
				*/
				if(	!strcasecmp(pObj, "cosdefinition") ||
					!strcasecmp(pObj, "cossuperdefinition") ||
					!strcasecmp(pObj, "costemplate")
					)
				{
					rc = 1;
				}

				index = slapi_attr_next_value( pObjclasses, index, &val );
			}
		}
	}
	return(rc);
}
