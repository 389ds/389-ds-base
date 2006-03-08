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
/* vlv.c */


/*
 * References to on-line documentation here.
 *
 * http://BLUES/users/dboreham/publish/Design_Documentation/RFCs/draft-ietf-asid-ldapv3-virtuallistview-01.html
 * http://warp.mcom.com/server/directory-server/clientsdk/hammerhead/design/virtuallistview.html 
 * ftp://ftp.ietf.org/internet-drafts/draft-ietf-ldapext-ldapv3-vlv-00.txt 
 * http://rocknroll/users/merrells/publish/vlvimplementation.html
 */


#include "back-ldbm.h"
#include "vlv_srch.h"
#include "vlv_key.h"

static PRUint32 vlv_trim_candidates_byindex(PRUint32 length, const struct vlv_request *vlv_request_control);
static PRUint32 vlv_trim_candidates_byvalue(backend *be, const IDList *candidates, const sort_spec* sort_control, const struct vlv_request *vlv_request_control);
static int vlv_build_candidate_list( backend *be, struct vlvIndex* p, const struct vlv_request *vlv_request_control, IDList** candidates, struct vlv_response *vlv_response_control);

/* New mutex for vlv locking
PRRWLock * vlvSearchList_lock=NULL;
static struct vlvSearch *vlvSearchList= NULL; 
*/

#define ISLEGACY(be) (be?(be->be_instance_info?(((ldbm_instance *)be->be_instance_info)->inst_li?(((ldbm_instance *)be->be_instance_info)->inst_li->li_legacy_errcode):0):0):0)

/* Callback to add a new VLV Search specification. Added write lock.*/

int vlv_AddSearchEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    struct vlvSearch* newVlvSearch= vlvSearch_new();
    backend *be = inst->inst_be;
    
    vlvSearch_init(newVlvSearch, pb, entryBefore, inst);
    /* vlvSearchList is modified; need Wlock */
    PR_RWLock_Wlock(be->vlvSearchList_lock);
    vlvSearch_addtolist(newVlvSearch, (struct vlvSearch **)&be->vlvSearchList);
    PR_RWLock_Unlock(be->vlvSearchList_lock);
    return SLAPI_DSE_CALLBACK_OK;
}

/* Callback to add a new VLV Index specification. Added write lock.*/

int vlv_AddIndexEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{ 
	struct vlvSearch *parent;
	backend *be= ((ldbm_instance*)arg)->inst_be;
	Slapi_DN parentdn;
	
	slapi_sdn_init(&parentdn);
	slapi_sdn_get_parent(slapi_entry_get_sdn(entryBefore),&parentdn);
    {
        /* vlvIndex list is modified; need Wlock */
        PR_RWLock_Wlock(be->vlvSearchList_lock);
        parent= vlvSearch_finddn((struct vlvSearch *)be->vlvSearchList, &parentdn);
        if(parent!=NULL)
        {
            struct vlvIndex* newVlvIndex= vlvIndex_new();
			newVlvIndex->vlv_be=be;
            vlvIndex_init(newVlvIndex, be, parent, entryBefore);
		    vlvSearch_addIndex(parent, newVlvIndex);
        }
		PR_RWLock_Unlock(be->vlvSearchList_lock);
    }
    slapi_sdn_done(&parentdn);
    return SLAPI_DSE_CALLBACK_OK;
}

/* Callback to delete a  VLV Index specification. Added write lock.*/
 
int vlv_DeleteSearchEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    struct vlvSearch* p=NULL;
    backend *be= ((ldbm_instance*)arg)->inst_be;
	
    /* vlvSearchList is modified; need Wlock */
    PR_RWLock_Wlock(be->vlvSearchList_lock);
    p = vlvSearch_finddn((struct vlvSearch *)be->vlvSearchList, slapi_entry_get_sdn(entryBefore));
    if(p!=NULL)
    {	
		LDAPDebug( LDAP_DEBUG_ANY, "Deleted Virtual List View Search (%s).\n", p->vlv_name, 0, 0);
		vlvSearch_removefromlist((struct vlvSearch **)&be->vlvSearchList,p->vlv_dn);
		vlvSearch_delete(&p);
    }
	PR_RWLock_Unlock(be->vlvSearchList_lock);
    return SLAPI_DSE_CALLBACK_OK;
}


/* Stub Callback to delete a  VLV Index specification.*/
 
int vlv_DeleteIndexEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
   	LDAPDebug( LDAP_DEBUG_ANY, "Deleted Virtual List View Index.\n", 0, 0, 0);
    return SLAPI_DSE_CALLBACK_OK;
}


/* Callback to modify a  VLV Search specification. Added read lock.*/
 
int vlv_ModifySearchEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
	struct vlvSearch* p=NULL;
	backend *be= ((ldbm_instance*)arg)->inst_be;
	
	PR_RWLock_Rlock(be->vlvSearchList_lock); 
    p= vlvSearch_finddn((struct vlvSearch *)be->vlvSearchList, slapi_entry_get_sdn(entryBefore));
    if(p!=NULL)
    {
       	LDAPDebug( LDAP_DEBUG_ANY, "Modified Virtual List View Search (%s), which will be enabled when the database is rebuilt.\n", p->vlv_name, 0, 0);
    }
	PR_RWLock_Unlock(be->vlvSearchList_lock);
    return SLAPI_DSE_CALLBACK_DO_NOT_APPLY;
}


/* Stub callback to modify a  VLV Index specification. */

int vlv_ModifyIndexEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
   	LDAPDebug( LDAP_DEBUG_ANY, "Modified Virtual List View Index.\n", 0, 0, 0);
    return SLAPI_DSE_CALLBACK_DO_NOT_APPLY;
}


/* Callback to rename a  VLV Search specification. Added read lock.*/

int vlv_ModifyRDNSearchEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
	struct vlvSearch* p=NULL;
	backend *be= ((ldbm_instance*)arg)->inst_be;
	
	PR_RWLock_Rlock(be->vlvSearchList_lock); 
    p= vlvSearch_finddn((struct vlvSearch *)be->vlvSearchList, slapi_entry_get_sdn(entryBefore));
    if(p!=NULL)
    {
       	LDAPDebug( LDAP_DEBUG_ANY, "Modified Virtual List View Search (%s), which will be enabled when the database is rebuilt.\n", p->vlv_name, 0, 0);
    }
	PR_RWLock_Unlock(be->vlvSearchList_lock);
    return SLAPI_DSE_CALLBACK_DO_NOT_APPLY;
}


/* Stub callback to modify a  VLV Index specification. */

int vlv_ModifyRDNIndexEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{   
   	LDAPDebug( LDAP_DEBUG_ANY, "Modified Virtual List View Index.\n", 0, 0, 0);
    return SLAPI_DSE_CALLBACK_DO_NOT_APPLY;
}

/* Something may have just read a VLV Entry. */

int vlv_SearchIndexEntry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    char *name= slapi_entry_attr_get_charptr(entryBefore,type_vlvName);
	backend *be= ((ldbm_instance*)arg)->inst_be;
    if (name!=NULL)
    {
        struct vlvIndex* p= vlv_find_searchname(name, be); /* lock list */
        slapi_ch_free((void **) &name);
        if(p!=NULL)
        {
            if(vlvIndex_enabled(p))
            {
                slapi_entry_attr_set_charptr(entryBefore, type_vlvEnabled, "1");
            }
            else
            {
                slapi_entry_attr_set_charptr(entryBefore, type_vlvEnabled, "0");
            }
            slapi_entry_attr_set_ulong(entryBefore, type_vlvUses, p->vlv_uses);
        }
    }
    return SLAPI_DSE_CALLBACK_OK;
}

/* Handle results of a search for objectclass "vlvIndex". Called by vlv_init at inittime -- no need to lock*/

static int
vlv_init_index_entry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    struct vlvIndex* newVlvIndex;
    struct vlvSearch* pSearch;
    Slapi_Backend *be= ((ldbm_instance*)arg)->inst_be;
    char ebuf[BUFSIZ];
	
    if(be!=NULL)
    {
        Slapi_DN parentdn;
		
        slapi_sdn_init(&parentdn);
        newVlvIndex= vlvIndex_new();
        slapi_sdn_get_parent(slapi_entry_get_sdn(entryBefore),&parentdn);
        pSearch= vlvSearch_finddn((struct vlvSearch *)be->vlvSearchList, &parentdn);
		if (pSearch == NULL) { 
			LDAPDebug( LDAP_DEBUG_ANY, "Parent doesn't exist for entry %s.\n",
				escape_string(slapi_entry_get_dn(entryBefore), ebuf), 0, 0); 
		} 
		else { 
			vlvIndex_init(newVlvIndex, be, pSearch, entryBefore);
			vlvSearch_addIndex(pSearch, newVlvIndex);
		}
        slapi_sdn_done(&parentdn);
    }
    return SLAPI_DSE_CALLBACK_OK;
}

/* Handle results of a search for objectclass "vlvSearch". Called by vlv_init at inittime -- no need to lock*/

static int
vlv_init_search_entry(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    struct vlvSearch* newVlvSearch= vlvSearch_new();
	ldbm_instance *inst = (ldbm_instance*)arg;
	backend *be= inst->inst_be;

    vlvSearch_init(newVlvSearch, pb, entryBefore, inst);
    vlvSearch_addtolist(newVlvSearch, (struct vlvSearch **)&be->vlvSearchList);
    return SLAPI_DSE_CALLBACK_OK;
}

/* Look at a new entry, and the set of VLV searches, and see whether 
there are any which have deferred initialization and which can now
be initialized given the new entry. Added write lock. */


void vlv_grok_new_import_entry(const struct backentry *e, backend *be)
{
    struct vlvSearch* p = NULL;
    static int seen_them_all = 0;
    int any_not_done = 0;


    PR_RWLock_Wlock(be->vlvSearchList_lock); 
    if (seen_them_all) {
		PR_RWLock_Unlock(be->vlvSearchList_lock);
        return;
    }
	p=(struct vlvSearch *)be->vlvSearchList;

    /* Walk the list of searches */
    for(;p!=NULL;p= p->vlv_next)
	/* is this one not initialized ? */
	if (0 == p->vlv_initialized) {
            any_not_done = 1;
            /* Is its base the entry we have here ? */
            if (0 == slapi_sdn_compare(backentry_get_sdn(e),p->vlv_base) ) {
                /* Then initialize it */
                vlvSearch_reinit(p,e);
            }
	}
    if (!any_not_done) {
        seen_them_all = 1;
    }
	PR_RWLock_Unlock(be->vlvSearchList_lock);
}

/*
 * Search for the VLV entries which describe the pre-computed indexes we
 * support.  Register administartion DSE callback functions.
 * This is exported to the backend initialisation routine.
 * 'inst' may be NULL for non-slapd initialization...
 */
int
vlv_init(ldbm_instance *inst)
{
    /* The FE DSE *must* be initialised before we get here */
    int return_value= LDAP_SUCCESS;
    int scope= LDAP_SCOPE_SUBTREE;
    char *basedn, buf[512];
    const char *searchfilter = "(objectclass=vlvsearch)";
    const char *indexfilter = "(objectclass=vlvindex)";
    backend *be= inst->inst_be;

    /* Initialize lock first time through */
    if(be->vlvSearchList_lock == NULL) {
        char *rwlockname = slapi_ch_smprintf("vlvSearchList_%s", inst->inst_name);
        be->vlvSearchList_lock = PR_NewRWLock(PR_RWLOCK_RANK_NONE, rwlockname);
        slapi_ch_free((void**)&rwlockname);
    }
    if (NULL != (struct vlvSearch *)be->vlvSearchList)
    {
        struct vlvSearch *t = NULL;
        struct vlvSearch *nt = NULL;
        /* vlvSearchList is modified; need Wlock */
        PR_RWLock_Wlock(be->vlvSearchList_lock);
        for (t = (struct vlvSearch *)be->vlvSearchList; NULL != t; )
        {
            nt = t->vlv_next;
            vlvSearch_delete(&t);
            t = nt;
        }
        be->vlvSearchList = NULL;
        PR_RWLock_Unlock(be->vlvSearchList_lock);
    }
    if (inst == NULL) {
        basedn = NULL;
    } else {
        PR_snprintf(buf, sizeof(buf), "cn=%s,cn=%s,cn=plugins,cn=config",
                inst->inst_name, inst->inst_li->li_plugin->plg_name);
        basedn = buf;
    }

    /* Find the VLV Search Entries */
    {
        Slapi_PBlock *tmp_pb;
        slapi_config_register_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_init_search_entry,(void *)inst);
        tmp_pb= slapi_search_internal(basedn, scope, searchfilter, NULL, NULL, 0);
        slapi_config_remove_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_init_search_entry);
        slapi_free_search_results_internal(tmp_pb);
        slapi_pblock_destroy(tmp_pb);
    }

    /* Find the VLV Index Entries */
    {
        Slapi_PBlock *tmp_pb;
        slapi_config_register_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_init_index_entry,(void*)inst);
        tmp_pb= slapi_search_internal(basedn, scope, indexfilter, NULL, NULL, 0);
        slapi_config_remove_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_init_index_entry);
        slapi_free_search_results_internal(tmp_pb);
        slapi_pblock_destroy(tmp_pb);
    }

    /* Only need to register these callbacks for SLAPD mode... */
    if(basedn!=NULL)
    {
        slapi_config_register_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_SearchIndexEntry,(void*)inst);
        slapi_config_register_callback(SLAPI_OPERATION_ADD,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_AddSearchEntry,(void*)inst);
        slapi_config_register_callback(SLAPI_OPERATION_ADD,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_AddIndexEntry,(void*)inst);
        slapi_config_register_callback(SLAPI_OPERATION_MODIFY,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_ModifySearchEntry,(void*)inst);
        slapi_config_register_callback(SLAPI_OPERATION_MODIFY,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_ModifyIndexEntry,(void*)inst);
        slapi_config_register_callback(SLAPI_OPERATION_DELETE,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_DeleteSearchEntry,(void*)inst);
        slapi_config_register_callback(SLAPI_OPERATION_DELETE,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_DeleteIndexEntry,(void*)inst);
        slapi_config_register_callback(SLAPI_OPERATION_MODRDN,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_ModifyRDNSearchEntry,(void*)inst);
        slapi_config_register_callback(SLAPI_OPERATION_MODRDN,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_ModifyRDNIndexEntry,(void*)inst);
    }

    return return_value;
}

/* Removes callbacks from above when  instance is removed. */

int 
vlv_remove_callbacks(ldbm_instance *inst) {

    int return_value= LDAP_SUCCESS;
    int scope= LDAP_SCOPE_SUBTREE;
    char *basedn, buf[512];
    const char *searchfilter = "(objectclass=vlvsearch)";
    const char *indexfilter = "(objectclass=vlvindex)";

    if (inst == NULL) {
        basedn = NULL;
    } else {
        PR_snprintf(buf, sizeof(buf), "cn=%s,cn=%s,cn=plugins,cn=config",
                inst->inst_name, inst->inst_li->li_plugin->plg_name);
        basedn = buf;
    }
    if(basedn!=NULL)
    {
        slapi_config_remove_callback(SLAPI_OPERATION_SEARCH,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_SearchIndexEntry);
        slapi_config_remove_callback(SLAPI_OPERATION_ADD,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_AddSearchEntry);
        slapi_config_remove_callback(SLAPI_OPERATION_ADD,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_AddIndexEntry);
        slapi_config_remove_callback(SLAPI_OPERATION_MODIFY,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_ModifySearchEntry);
        slapi_config_remove_callback(SLAPI_OPERATION_MODIFY,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_ModifyIndexEntry);
        slapi_config_remove_callback(SLAPI_OPERATION_DELETE,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_DeleteSearchEntry);
        slapi_config_remove_callback(SLAPI_OPERATION_DELETE,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_DeleteIndexEntry);
        slapi_config_remove_callback(SLAPI_OPERATION_MODRDN,DSE_FLAG_PREOP,basedn,scope,searchfilter,vlv_ModifyRDNSearchEntry);
        slapi_config_remove_callback(SLAPI_OPERATION_MODRDN,DSE_FLAG_PREOP,basedn,scope,indexfilter,vlv_ModifyRDNIndexEntry);
    }
	return return_value;
}

/* Find an enabled index which matches this description. */

static struct vlvIndex*
vlv_find_search(backend *be, const Slapi_DN *base, int scope, const char *filter, const sort_spec* sort_control)
{
    return vlvSearch_findenabled(be,(struct vlvSearch *)be->vlvSearchList,base,scope,filter,sort_control);
}


/* Find a search which matches this name. Added read lock. */

struct vlvIndex*
vlv_find_searchname(const char * name, backend *be)
{
	struct vlvIndex *p=NULL;

	PR_RWLock_Rlock(be->vlvSearchList_lock);
	p=vlvSearch_findname((struct vlvSearch *)be->vlvSearchList,name);
	PR_RWLock_Unlock(be->vlvSearchList_lock);
	return p;
}

/* Find a search which matches this indexname. Added to read lock */

struct vlvIndex*
vlv_find_indexname(const char * name, backend *be)
{
    
	struct vlvIndex *p=NULL;

	PR_RWLock_Rlock(be->vlvSearchList_lock);
	p=vlvSearch_findindexname((struct vlvSearch *)be->vlvSearchList,name);
	PR_RWLock_Unlock(be->vlvSearchList_lock);
	return p;
}


/* Get a list of known VLV Indexes. Added read lock */

char *
vlv_getindexnames(backend *be)
{
    char *n=NULL;

	PR_RWLock_Rlock(be->vlvSearchList_lock);
	n=vlvSearch_getnames((struct vlvSearch *)be->vlvSearchList);
	PR_RWLock_Unlock(be->vlvSearchList_lock);
	return n;
}

/* Return the list of VLV indices to the import code. Added read lock */

void 
vlv_getindices(IFP callback_fn,void *param, backend *be)
{
    /* Traverse the list, calling the import code's callback function */
    struct vlvSearch* ps = NULL;
	
	PR_RWLock_Rlock(be->vlvSearchList_lock);
	ps = (struct vlvSearch *)be->vlvSearchList;
    for(;ps!=NULL;ps= ps->vlv_next)
    {
        struct vlvIndex* pi= ps->vlv_index;
        for(;pi!=NULL;pi= pi->vlv_next)
        {
            callback_fn(pi->vlv_attrinfo,param);
        }
    }
	PR_RWLock_Unlock(be->vlvSearchList_lock);
}

/*
 * Create a key for the entry in the vlv index.
 *
 * The key is a composite of a value from each sorted attribute.
 *
 * If a sorted attribute has many values, then the key is built
 * with the attribute value with the lowest value.
 *
 * The primary sorted attribute value is followed by a 0x00 to
 * ensure that short attribute values appear before longer ones.
 *
 * Many entries may have the same attribute values, which would
 * generate the same composite key, so we append the EntryID
 * to ensure the uniqueness of the key.
 *
 * Always creates a key. Never returns NULL.
 */
static struct vlv_key *
vlv_create_key(struct vlvIndex* p, struct backentry* e)
{
    struct berval val, *lowest_value = NULL;
    unsigned char char_min = 0x00;
    unsigned char char_max = 0xFF;
    struct vlv_key *key= vlv_key_new();
    if(p->vlv_sortkey!=NULL)
    {
        /* Foreach sorted attribute... */
        int sortattr= 0;
        while(p->vlv_sortkey[sortattr]!=NULL)
        {
            Slapi_Attr* attr= attrlist_find(e->ep_entry->e_attrs, p->vlv_sortkey[sortattr]->sk_attrtype);
            {
                /*
                 * If there's a matching rule associated with the sorted
                 * attribute then use the indexer to mangle the attr values.
                 * This ensures that the international characters will 
                 * collate in the correct order.
                 */

				/* xxxPINAKI */
				/* need to free some stuff! */
		        Slapi_Value **cvalue = NULL;
        		struct berval **value = NULL;
                int free_value= 0;
                if (attr != NULL && !valueset_isempty(&attr->a_present_values))
				{
                    /* Sorted attribute found. */
                    int totalattrs;
            		if (p->vlv_sortkey[sortattr]->sk_matchruleoid==NULL)
            		{
            			/* No matching rule. Syntax Plugin mangles value. */
						Slapi_Value **va= valueset_get_valuearray(&attr->a_present_values);
                		slapi_call_syntax_values2keys_sv( p->vlv_syntax_plugin[sortattr], va, &cvalue, LDAP_FILTER_EQUALITY );
						valuearray_get_bervalarray(cvalue,&value);

				/* XXXSD need to free some more stuff */
				{
					int numval;
					for (numval=0; cvalue&&cvalue[numval];numval++) {
						slapi_value_free(&cvalue[numval]);
					}
					if (cvalue)
						slapi_ch_free((void **)&cvalue);
				}
				
                        free_value= 1;  
            		}
            		else
            		{
            			/* Matching rule. Do the magic mangling. Plugin owns the memory. */
                        if(p->vlv_mrpb[sortattr]!=NULL)
                        {
						    /* xxxPINAKI */
						    struct berval **bval=NULL;
							Slapi_Value **va= valueset_get_valuearray(&attr->a_present_values);
						    valuearray_get_bervalarray(va,&bval);
                			matchrule_values_to_keys(p->vlv_mrpb[sortattr],bval,&value);
                        }
            		}
                    for(totalattrs=0;value[totalattrs]!=NULL;totalattrs++) {}; /* Total Number of Attributes */
                    if(totalattrs==1)
                    {
                        lowest_value= value[0];
                    }
                    else
                    {
                    	lowest_value = attr_value_lowest(value, slapi_berval_cmp);
                    }
                } /* end of if (attr != NULL && ...) */
                if(p->vlv_sortkey[sortattr]->sk_reverseorder)
                {
                    /*
                     * This attribute is reverse sorted, so we must 
                     * invert the attribute value so that the keys
                     * will be in the correct order.
                     */
                    unsigned int i;
                    char *attributeValue = NULL;
                    /* Bug 605477 : Don't malloc 0 bytes */
                    if (attr != NULL && lowest_value->bv_len != 0) {
                         attributeValue = (char*)slapi_ch_malloc(lowest_value->bv_len);
                         for(i=0;i<lowest_value->bv_len;i++)
                       	 {
                   		    attributeValue[i]= UCHAR_MAX - ((char*)lowest_value->bv_val)[i];
                         }
                       	val.bv_len= lowest_value->bv_len;
                       	val.bv_val= (void*)attributeValue;
                     } else { 
                       /* Reverse Sort: We use an attribute value of 0x00 when 
                        * there is no attribute value or attrbute is absent 
                        */
                        val.bv_val= (void*)&char_min;
                        val.bv_len= 1;
                     }
                     vlv_key_addattr(key,&val);
                     slapi_ch_free((void**)&attributeValue);
                }
                else
                {
                    /*
                     * This attribute is forward sorted, so add the
                     * attribute value to the end of all the keys.
                     */

                    /* If the forward-sorted attribute is absent or has no 
                     * value, we need to use the value of 0xFF.
                     */
                     if (attr != NULL && lowest_value->bv_len > 0) {
                         vlv_key_addattr(key,lowest_value);
                     } else {
                         val.bv_val = (void*)&char_max;
                         val.bv_len = 1;
                         vlv_key_addattr(key,&val);
                     }
                }
                if(sortattr==0)
                {
                    /*
                     * If this is the first attribute (the typedown attribute)
                     * then it should be followed by a zero.  This is to ensure
                     * that shorter attribute values appear before longer ones.
                     */
                    char zero = 0;
                    val.bv_len= 1;
                    val.bv_val= (void*)&zero;
                    vlv_key_addattr(key,&val);
                }
                if(free_value)
                {
           			ber_bvecfree(value);
                }
            }
            sortattr++;
        }
    }
    {
        /* Append the EntryID to the key to ensure uniqueness */
        val.bv_len= sizeof(e->ep_id);
        val.bv_val= (void*)&e->ep_id;
        vlv_key_addattr(key,&val);
    }
    return key;
}

/*
 * Insert or Delete the entry to or from the index
 */ 

static int
do_vlv_update_index(back_txn *txn, struct ldbminfo *li, Slapi_PBlock *pb, struct vlvIndex* pIndex, struct backentry* entry, int insert)
{
    backend *be;
    int rc= 0;
    DB *db = NULL;
    DB_TXN *db_txn = NULL;
    struct vlv_key *key = NULL;

    slapi_pblock_get(pb, SLAPI_BACKEND, &be);

    rc = dblayer_get_index_file(be, pIndex->vlv_attrinfo, &db, DBOPEN_CREATE);
    if (rc != 0) {
      if(rc != DB_LOCK_DEADLOCK)
        LDAPDebug(LDAP_DEBUG_ANY, "VLV: can't get index file '%s' (err %d)\n",
                  pIndex->vlv_attrinfo->ai_type, rc, 0);
        return rc;
    }

    key = vlv_create_key(pIndex,entry);
    if (NULL != txn) {
        db_txn = txn->back_txn_txn;
    } else {
        /* Very bad idea to do this outside of a transaction */
    }

    if (insert) {
        DBT data = {0};
        data.size = sizeof(entry->ep_id);
        data.data = &entry->ep_id;
        rc = db->put(db, db_txn, &key->key, &data, 0);
        if (rc == 0) {
            LDAPDebug(LDAP_DEBUG_TRACE,
                      "vlv_update_index: %s Insert %s ID=%lu\n",
                      pIndex->vlv_name, key->key.data, (u_long)entry->ep_id);
            vlvIndex_increment_indexlength(pIndex, db, txn);
	} else if (rc == DB_RUNRECOVERY) {
	    ldbm_nasty(pIndex->vlv_name,77,rc);
	} else if(rc != DB_LOCK_DEADLOCK)  {
            /* jcm: This error is valid if the key already exists.
             * Identical multi valued attr values could do this. */
            LDAPDebug(LDAP_DEBUG_TRACE,
                      "vlv_update_index: %s Insert %s ID=%lu FAILED\n",
                      pIndex->vlv_name, key->key.data, (u_long)entry->ep_id);
        }
    } else {
        LDAPDebug(LDAP_DEBUG_TRACE,
                  "vlv_update_index: %s Delete %s\n",
                  pIndex->vlv_name, key->key.data, 0);
        rc = db->del(db, db_txn, &key->key, 0);
        if (rc == 0) {
            vlvIndex_decrement_indexlength(pIndex, db, txn);
	} else if (rc == DB_RUNRECOVERY) {
	    ldbm_nasty(pIndex->vlv_name,78,rc);
	} else if (rc != DB_LOCK_DEADLOCK) {
            LDAPDebug(LDAP_DEBUG_TRACE,
                      "vlv_update_index: %s Delete %s FAILED\n",
                      pIndex->vlv_name, key->key.data, 0);
        }
    }

    vlv_key_delete(&key);
    dblayer_release_index_file(be, pIndex->vlv_attrinfo, db);
    return rc;
}

/*
 * Given an entry modification check if a VLV index needs to be updated.
 */

int
vlv_update_index(struct vlvIndex* p, back_txn *txn, struct ldbminfo *li, Slapi_PBlock *pb, struct backentry* oldEntry, struct backentry* newEntry)
{
    int return_value=0;
    /* Check if the old entry is in this VLV index */
    if(oldEntry!=NULL)
    {
        if(slapi_sdn_scope_test(backentry_get_sdn(oldEntry),vlvIndex_getBase(p),vlvIndex_getScope(p)))
        {
            if(slapi_filter_test( pb, oldEntry->ep_entry, vlvIndex_getFilter(p), 0 /* No ACL Check */) == 0 )
            {
                /* Remove the entry from the index */
                return_value=do_vlv_update_index(txn, li, pb, p, oldEntry, 0 /* Delete Key */); 
            }
        }
    }
    /* Check if the new entry should be in the VLV index */
    if(newEntry!=NULL)
    {
        if(slapi_sdn_scope_test(backentry_get_sdn(newEntry),vlvIndex_getBase(p),vlvIndex_getScope(p)))
        {
            if(slapi_filter_test( pb, newEntry->ep_entry, vlvIndex_getFilter(p), 0 /* No ACL Check */) == 0 )
            {
                /* Add the entry to the index */
                return_value=do_vlv_update_index(txn, li, pb, p, newEntry, 1 /* Insert Key */); 
            }
        }
    }
    return return_value;
}

/*
 * Given an entry modification check if a VLV index needs to be updated.
 *
 * This is called for every modifying operation, so it must be very efficient.
 *
 * We need to know if we're adding, deleting, or modifying
 * because we could be leaving and/or joining an index
 *
 * ADD: oldEntry==NULL && newEntry!=NULL
 * DEL: oldEntry!=NULL && newEntry==NULL
 * MOD: oldEntry!=NULL && newEntry!=NULL
 *
 * JCM: If only non-sorted attributes are changed, then the indexes don't need updating.
 * JCM: Detecting this fact, given multi-valued atribibutes, might be tricky...
 * Read lock (traverse vlvSearchList; no change on vlvSearchList/vlvIndex lists)
 */

int
vlv_update_all_indexes(back_txn *txn, backend *be, Slapi_PBlock *pb, struct backentry* oldEntry, struct backentry* newEntry)
{
    int return_value= LDAP_SUCCESS;
    struct vlvSearch* ps=NULL;
	struct ldbminfo *li = ((ldbm_instance *)be->be_instance_info)->inst_li;
	
	PR_RWLock_Rlock(be->vlvSearchList_lock);
	ps = (struct vlvSearch *)be->vlvSearchList;
    for(;ps!=NULL;ps= ps->vlv_next)
    {
        struct vlvIndex* pi= ps->vlv_index;
		for (return_value = LDAP_SUCCESS; return_value == LDAP_SUCCESS && pi!=NULL; pi=pi->vlv_next) 
			return_value=vlv_update_index(pi, txn, li, pb, oldEntry, newEntry);
    }
	PR_RWLock_Unlock(be->vlvSearchList_lock);
    return return_value;
}

/*
 * Determine the range of record numbers to return.
 * Prevent an underrun, or overrun.
 */
 /* jcm: Should we make sure that start < stop */

static void
determine_result_range(const struct vlv_request *vlv_request_control, PRUint32 index, PRUint32 length, PRUint32* pstart, PRUint32 *pstop)
{
	if (vlv_request_control == NULL)
    {
        *pstart= 0;
        if (0 == length) /* 609377: index size could be 0 */
        {
            *pstop= 0;
        }
        else
        {
            *pstop= length - 1;
        }
    }
    else
    {
        /* Make sure we don't run off the start */
        if(index < vlv_request_control->beforeCount)
        {
            *pstart= 0;
        }
        else
        {
            *pstart= index - vlv_request_control->beforeCount;
        }
        /* Make sure we don't run off the end */
        if(ULONG_MAX - index > vlv_request_control->afterCount)
        {
            *pstop= index + vlv_request_control->afterCount;
        }
        else
        {
            *pstop= ULONG_MAX;
        }
        /* Client tried to index off the end */
        if (0 == length) /* 609377: index size could be 0 */
        {
            *pstop= 0;
        }
        else if(*pstop > length - 1)
        {
            *pstop= length - 1;
        }
    }
    LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_determine_result_range: Result Range %lu-%lu\n", *pstart, *pstop, 0 );
}

/*
 * This is a utility function to pass the client
 * supplied attribute value through the appropriate
 * matching rule indexer.
 *
 * It allocates a berval vector which the caller
 * must free.
 */

static struct berval **
vlv_create_matching_rule_value( Slapi_PBlock* pb, struct berval *original_value)
{
    struct berval **value= NULL;
    if(pb!=NULL)
    {
        struct berval **outvalue = NULL;
        struct berval *invalue[2];
        invalue[0]= original_value; /* jcm: cast away const */
        invalue[1]= NULL;
        /* The plugin owns the memory it returns in outvalue */
        matchrule_values_to_keys(pb,invalue,&outvalue);
        if(outvalue!=NULL)
        {
	    value= slapi_ch_bvecdup(outvalue);
        }
    }
    if(value==NULL)
    {
        struct berval *outvalue[2];
        outvalue[0]= original_value; /* jcm: cast away const */
        outvalue[1]= NULL;
	value= slapi_ch_bvecdup(outvalue);
    }
    return value;
}


/*
 * Find the record number in a VLV index for a given attribute value.
 * The returned index is counted from zero.
 */

static PRUint32 
vlv_build_candidate_list_byvalue( struct vlvIndex* p, DBC *dbc, PRUint32 length, const struct vlv_request *vlv_request_control)
{
    PRUint32 si= 0; /* The Selected Index */
    int err= 0;
    DBT key= {0};
    DBT data= {0};
    /*
     * If the primary sorted attribute has an associated
     * matching rule, then we must mangle the typedown
     * value.
     */
    struct berval **typedown_value= NULL;
    struct berval *invalue[2];
    invalue[0]= (struct berval *)&vlv_request_control->value; /* jcm: cast away const */
    invalue[1]= NULL;
	if (p->vlv_sortkey[0]->sk_matchruleoid==NULL)
	{
		slapi_call_syntax_values2keys(p->vlv_syntax_plugin[0],invalue,&typedown_value,LDAP_FILTER_EQUALITY); /* JCM SLOW FUNCTION */
    }
    else
    {
        typedown_value= vlv_create_matching_rule_value(p->vlv_mrpb[0],(struct berval *)&vlv_request_control->value); /* jcm: cast away const */
    }
    if(p->vlv_sortkey[0]->sk_reverseorder)
    {
        /*
         * The primary attribute is reverse sorted, so we must 
         * invert the typedown value in order to match the key.
         */
        unsigned int i;
        for(i=0;i<(*typedown_value)->bv_len;i++)
        {
            ((char*)(*typedown_value)->bv_val)[i]= UCHAR_MAX - ((char*)(*typedown_value)->bv_val)[i];
        }
    }

    key.flags= DB_DBT_MALLOC;
    key.size= typedown_value[0]->bv_len;
    key.data= typedown_value[0]->bv_val;
    data.flags= DB_DBT_MALLOC;
    err= dbc->c_get(dbc,&key,&data,DB_SET_RANGE);
    if(err==0)
    {
        free(data.data);
        err= dbc->c_get(dbc,&key,&data,DB_GET_RECNO);
        if(err==0)
        {
            si= *((db_recno_t*)data.data);
            /* Records are numbered from one. */
            si--;
            free(data.data);
        	LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_build_candidate_list_byvalue: Found. Index=%lu\n",si,0,0);
        }
        else
        {
            /* Couldn't get the record number for the record we found. */
        }
    }
    else
    {
        /* Couldn't find an entry which matches the value,
         * so return the last entry
         * (609377) when the index file is empty, there is no "last entry".
         */
        if (0 == length)
        {
            si = 0;
        }
        else
        {
            si = length - 1;
        }
        LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_build_candidate_list_byvalue: Not Found. Index=%lu\n",si,0,0);
    }
    ber_bvecfree((struct berval**)typedown_value);
    return si;
}

static int
vlv_idl_sort_cmp(const void *x, const void *y)
{
	return *(ID *)x - *(ID *)y;
}

/* build a candidate list (IDL) from a VLV index, given the starting index
 * and the ending index (as an inclusive list).
 * returns 0 on success, or an LDAP error code.
 */
int vlv_build_idl(PRUint32 start, PRUint32 stop, DB *db, DBC *dbc,
		  IDList **candidates, int dosort)
{
    IDList *idl = NULL;
    int err;
    PRUint32 recno;
    DBT key = {0};
    DBT data = {0};
    ID id;

    idl = idl_alloc(stop-start+1);
    if (!idl) {
        /* out of memory :( */
        return LDAP_OPERATIONS_ERROR;
    }
    recno = start+1;
    key.size = sizeof(recno);
    key.data = &recno;
    key.flags = DB_DBT_MALLOC;
    data.ulen = sizeof(ID);
    data.data = &id;
    data.flags = DB_DBT_USERMEM;        /* don't alloc */
    err = dbc->c_get(dbc, &key, &data, DB_SET_RECNO);
    while ((err == 0) && (recno <= stop+1)) {
        if (key.data != &recno)
            free(key.data);
        idl_append(idl, *(ID *)data.data);
        if (++recno <= stop+1) {
            err = dbc->c_get(dbc, &key, &data, DB_NEXT);
        }
    }
    if (err != 0) {
        /* some db error...? */
        LDAPDebug(LDAP_DEBUG_ANY, "vlv_build_idl: can't follow db cursor "
                  "(err %d)\n", err, 0, 0);
        if (err == ENOMEM)
            LDAPDebug(LDAP_DEBUG_ANY, "   nomem: wants %d key, %d data\n",
                      key.size, data.size, 0);
        return LDAP_OPERATIONS_ERROR;
    }

    /* success! */
    if (idl) {
        if (candidates)
        {
            if (dosort)
            {
                qsort((void *)&idl->b_ids[0], idl->b_nids,
                      (size_t)sizeof(ID), vlv_idl_sort_cmp);
            }
            *candidates = idl;
        }
        else
            idl_free(idl);        /* ??? */
    }
    return LDAP_SUCCESS;
}


/* This function does vlv_access, searching and building list all while holding read lock

  1. vlv_find_search fails, set:
	                unsigned int opnote = SLAPI_OP_NOTE_UNINDEXED;
                    slapi_pblock_set( pb, SLAPI_OPERATION_NOTES, &opnote );
	 return FIND_SEARCH FAILED

  2. vlvIndex_accessallowed fails
     return VLV_LDBM_ACCESS_DENIED
  
  3. vlv_build_candidate_list fails:
	 return VLV_BLD_LIST_FAILED

  4. return LDAP_SUCCESS
*/

int
vlv_search_build_candidate_list(Slapi_PBlock *pb, const Slapi_DN *base, int *vlv_rc, const sort_spec* sort_control, 
								const struct vlv_request *vlv_request_control, 
								IDList** candidates, struct vlv_response *vlv_response_control) {
	struct vlvIndex* pi = NULL;
	backend *be;
	int scope, rc=LDAP_SUCCESS;
	char *fstr;

	slapi_pblock_get( pb, SLAPI_BACKEND, &be );
    slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope );
    slapi_pblock_get( pb, SLAPI_SEARCH_STRFILTER, &fstr );
	PR_RWLock_Rlock(be->vlvSearchList_lock);
	if((pi=vlv_find_search(be, base, scope, fstr, sort_control)) == NULL) {
	    unsigned int opnote = SLAPI_OP_NOTE_UNINDEXED;
        slapi_pblock_set( pb, SLAPI_OPERATION_NOTES, &opnote );
		rc = VLV_FIND_SEARCH_FAILED;
	} else if((*vlv_rc=vlvIndex_accessallowed(pi, pb)) != LDAP_SUCCESS) {
		rc = VLV_ACCESS_DENIED;
	} else if ((*vlv_rc=vlv_build_candidate_list(be,pi,vlv_request_control,candidates,vlv_response_control)) != LDAP_SUCCESS) {
		rc = VLV_BLD_LIST_FAILED;
		vlv_response_control->result=*vlv_rc;
	}
	PR_RWLock_Unlock(be->vlvSearchList_lock);
	return rc;
}

/*
 * Given the SORT and VLV controls return a candidate list from the
 * pre-computed index file.
 *
 * Returns:
 *       success (0),
 *       operationsError (1),
 *       unwillingToPerform (53),
 *       timeLimitExceeded (3),
 *       adminLimitExceeded (11),
 *       indexRangeError (61),
 *       other (80)
 */
 

static int
vlv_build_candidate_list( backend *be, struct vlvIndex* p, const struct vlv_request *vlv_request_control, IDList** candidates, struct vlv_response *vlv_response_control)
{
    int return_value = LDAP_SUCCESS;
    DB *db = NULL;
    DBC *dbc = NULL;
    int rc, err;
    PRUint32 si = 0;       /* The Selected Index */
    PRUint32 length;
    int do_trim= 1;

    LDAPDebug(LDAP_DEBUG_TRACE,
              "=> vlv_build_candidate_list: %s %s Using VLV Index %s\n",
              slapi_sdn_get_dn(vlvIndex_getBase(p)), p->vlv_search->vlv_filter,
              vlvIndex_getName(p));
    if (!vlvIndex_online(p)) {
        return -1;
    }
    rc = dblayer_get_index_file(be, p->vlv_attrinfo, &db, 0);
    if (rc != 0) {
        /* shouldn't happen */
        LDAPDebug(LDAP_DEBUG_ANY, "VLV: can't get index file '%s' (err %d)\n",
                  p->vlv_attrinfo->ai_type, rc, 0);
        return -1;
    }

    err = db->cursor(db, 0 /* txn */, &dbc, 0);
    if (err != 0) {
        /* shouldn't happen */
        LDAPDebug(LDAP_DEBUG_ANY, "VLV: couldn't get cursor (err %d)\n",
                  rc, 0, 0);
        return -1;
    }

    length = vlvIndex_get_indexlength(p, db, 0 /* txn */);

    /* Increment the usage counter */
    vlvIndex_incrementUsage(p);

    if (vlv_request_control)
    {
        switch(vlv_request_control->tag) {
        case 0: /* byIndex */
            si = vlv_trim_candidates_byindex(length, vlv_request_control);
            break;
        case 1: /* byValue */
            si = vlv_build_candidate_list_byvalue(p, dbc, length,
                                                     vlv_request_control);
            if (si==length) {
                do_trim = 0;
                *candidates = idl_alloc(0);
            }
            break;
        default:
            /* Some wierd tag value.  Shouldn't ever happen */
            if (ISLEGACY(be)) {
                return_value = LDAP_OPERATIONS_ERROR;
            } else {
                return_value = LDAP_VIRTUAL_LIST_VIEW_ERROR;
            }
            break;
        }

        /* Tell the client what the real content count is. 
         * Client counts from 1. */
        vlv_response_control->targetPosition = si + 1;
        vlv_response_control->contentCount = length;
        vlv_response_control->result = return_value;
    }

    if ((return_value == LDAP_SUCCESS) && do_trim) {
        /* Work out the range of records to return */
        PRUint32 start, stop;
        determine_result_range(vlv_request_control, si, length, &start, &stop);

        /* fetch the idl */
        return_value = vlv_build_idl(start, stop, db, dbc, candidates, 0);
    }
    dbc->c_close(dbc);
       	
    dblayer_release_index_file( be, p->vlv_attrinfo, db );
    return return_value;    
}

/*
 * Given a candidate list and a filter specification, filter the candidate list
 *
 * Returns:
 *       success (0),
 *       operationsError (1),
 *       unwillingToPerform (53),
 *       timeLimitExceeded (3),
 *       adminLimitExceeded (11),
 *       indexRangeError (61),
 *       other (80)
 */
int
vlv_filter_candidates(backend *be, Slapi_PBlock *pb, const IDList *candidates, const Slapi_DN *base, int scope, Slapi_Filter *filter, IDList** filteredCandidates, int lookthrough_limit, time_t time_up)
{
    IDList* resultIdl= NULL;
	int return_value = LDAP_SUCCESS;

	/* Refuse to filter a non-existent IDlist */
	if (NULL == candidates)
	{
		return LDAP_UNWILLING_TO_PERFORM;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "=> vlv_filter_candidates: Filtering %lu Candidates\n",(u_long)candidates->b_nids, 0, 0 );

	if (0 == return_value && candidates->b_nids>0)
	{
        /* jcm: Could be an idlist function. create_filtered_idlist */
        /* Iterate over the ID List applying the filter */
        int lookedat= 0;
        int done= 0;
        int counter= 0;
        ID id = NOID;
    	idl_iterator current = idl_iterator_init(candidates);
        resultIdl= idl_alloc(candidates->b_nids);
        do
        {
        	id = idl_iterator_dereference_increment(&current, candidates);
        	if ( id != NOID )
        	{
                int err= 0;
                struct backentry *e= NULL;
                e = id2entry( be, id, NULL, &err );
            	if ( e == NULL )
            	{
                    /*
                     * The ALLIDS ID List contains IDs for which there is no entry.
                     * This is because the entries have been deleted.  An error in
                     * this case is ok.
                     */
                    if(!(ALLIDS(candidates) && err==DB_NOTFOUND))
                    {
                	    LDAPDebug( LDAP_DEBUG_ANY, "vlv_filter_candidates: Candidate %lu not found err=%d\n", (u_long)id, err, 0 );
                    }
            	}
            	else
            	{
                    lookedat++;
                    if(slapi_sdn_scope_test(backentry_get_sdn(e),base,scope))
                    {
                	    if ( slapi_filter_test( pb, e->ep_entry, filter, 0 /* No ACL Check */) == 0 )
                	    {
                            /* The entry passed the filter test, add the id to the list */
                    	    LDAPDebug( LDAP_DEBUG_TRACE, "vlv_filter_candidates: Candidate %lu Passed Filter\n", (u_long)id, 0, 0 );
                            idl_append(resultIdl,id);
                		}
                    }
					cache_return(&(((ldbm_instance *) be->be_instance_info)->inst_cache), &e);
            	}
    	    }

            done= slapi_op_abandoned(pb);

        	/* Check to see if our journey is really necessary */
        	if ( counter++ % 10 == 0 )
        	{
        		/* check time limit */
        		time_t curtime = current_time();
        		if ( time_up != -1 && curtime > time_up )
        		{
                    return_value= LDAP_TIMELIMIT_EXCEEDED;
                    done= 1;
        		}
        		/* check lookthrough limit */
        		if ( lookthrough_limit != -1 && lookedat>lookthrough_limit )
        		{
                    return_value= LDAP_ADMINLIMIT_EXCEEDED;
                    done= 1;
        		}
        	}
    	} while (!done && id!=NOID);
	}
    if(filteredCandidates!=NULL)
        *filteredCandidates= resultIdl;
	LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_filter_candidates: Filtering done\n",0, 0, 0 );

	return return_value;
}

/*
 * Given a candidate list and a virtual list view specification, trim the candidate list
 *
 * Returns:
 *       success (0),
 *       operationsError (1),
 *       unwillingToPerform (53),
 *       timeLimitExceeded (3),
 *       adminLimitExceeded (11),
 *       indexRangeError (61),
 *       other (80)
 */
int
vlv_trim_candidates(backend *be, const IDList *candidates, const sort_spec* sort_control, const struct vlv_request *vlv_request_control, IDList** trimmedCandidates,struct vlv_response *vlv_response_control)
{
    IDList* resultIdl= NULL;
    int return_value= LDAP_SUCCESS;
    PRUint32 si= 0; /* The Selected Index */
    int do_trim= 1;

	/* Refuse to trim a non-existent IDlist */
	if (NULL == candidates || candidates->b_nids==0)
	{
		return LDAP_UNWILLING_TO_PERFORM;
	}

    switch(vlv_request_control->tag)
    {
    case 0: /* byIndex */
        si= vlv_trim_candidates_byindex(candidates->b_nids, vlv_request_control);
        break;
    case 1: /* byValue */
        si= vlv_trim_candidates_byvalue(be, candidates, sort_control, vlv_request_control);
        /* Don't bother sending results if the attribute value wasn't found */
        if(si==candidates->b_nids)
        {
            do_trim= 0;
            resultIdl= idl_alloc(0);
        }
        break;
    default:
        /* Some wierd tag value.  Shouldn't ever happen */
        if (ISLEGACY(be)) {
            return_value = LDAP_OPERATIONS_ERROR;
        } else {
            return_value = LDAP_VIRTUAL_LIST_VIEW_ERROR;
        }
        break;
    }

    /* Tell the client what the real content count is. Clients count from 1 */
    vlv_response_control->targetPosition= si + 1;
    vlv_response_control->contentCount= candidates->b_nids;

    if(return_value==LDAP_SUCCESS && do_trim)
    {
        /* Work out the range of records to return */
        PRUint32 start, stop;
        determine_result_range(vlv_request_control,si,candidates->b_nids,&start,&stop);
        /* Build a new list containing the (start..stop) range */
        /* JCM: Should really be a function in idlist.c to copy a range */
        resultIdl= idl_alloc(stop-start+1);
        {
            PRUint32 cursor= 0;
            for(cursor=start;cursor<=stop;cursor++)
            {
            	LDAPDebug( LDAP_DEBUG_TRACE, "vlv_trim_candidates: Include ID %lu\n",(u_long)candidates->b_ids[cursor], 0, 0 );
                idl_append(resultIdl,candidates->b_ids[cursor]);
            }
        }
    }
   	LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_trim_candidates: Trimmed list contains %lu entries.\n",(u_long)resultIdl->b_nids, 0, 0 );
    if(trimmedCandidates!=NULL)
        *trimmedCandidates= resultIdl;
    return return_value;
}

/*
 * Work out the Selected Index given the length of the candidate list
 * and the request control from the client.
 *
 * If the client sends Index==0 we behave as if I=1
 * If the client sends Index==Size==1 we behave as if I=1, S=0
 */
static PRUint32
vlv_trim_candidates_byindex(PRUint32 length, const struct vlv_request *vlv_request_control)
{
    PRUint32 si= 0; /* The Selected Index */
	LDAPDebug( LDAP_DEBUG_TRACE, "=> vlv_trim_candidates_byindex: length=%lu index=%lu size=%lu\n",length, vlv_request_control->index, vlv_request_control->contentCount );
    if(vlv_request_control->index==0)
    {
        /* Always select the first entry in the list */
        si= 0;
    }
    else
    {
        if(vlv_request_control->contentCount==0)
        {
            /* The client has no idea what the content count might be. */
            /* Can't scale the index, so use as is */
            si= vlv_request_control->index;
            if (0 == length) /* 609377: index size could be 0 */
            {
                if (si > 0)
                {
                    si = length;
                }
            }
            else if(si > length - 1)
            {
                si= length - 1;
            }
        }
        else
        {
            if(vlv_request_control->index>=vlv_request_control->contentCount)
            {
                /* Always select the last entry in the list */
                if (0 == length) /* 609377: index size could be 0 */
                {
                    si = 0;
                }
                else
                {
                    si= length-1;
                }
            }
            else
            {
                /* The three components of this expression are (PRUint32) and may well have a value up to ULONG_MAX */
                /* SelectedIndex = ActualContentCount * ( ClientIndex / ClientContentCount ) */
                si= ((PRUint32)((double)length * (double)(vlv_request_control->index / (double)vlv_request_control->contentCount )));
            }
        }
    }
	LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_trim_candidates_byindex: Selected Index %lu\n",si, 0, 0 );
    return si;
}

/*
 * Iterate over the Candidate ID List looking for an entry >= the provided attribute value.
 */
static PRUint32
vlv_trim_candidates_byvalue(backend *be, const IDList *candidates, const sort_spec* sort_control, const struct vlv_request *vlv_request_control)
{
    PRUint32 si= 0; /* The Selected Index */
    PRUint32 low= 0;
    PRUint32 high= candidates->b_nids-1;
    PRUint32 current= 0;
    ID id = NOID;
    int found= 0;
    struct berval **typedown_value;

    /* For non-matchrule indexing */
    value_compare_fn_type compare_fn= NULL;

    /*
     * If the primary sorted attribute has an associated
     * matching rule, then we must mangle the typedown
     * value.
     */
    if (sort_control->matchrule==NULL)
    {
        void *pi= NULL;
        if(slapi_attr_type2plugin(sort_control->type, &pi)==0)
        {
            struct berval *invalue[2];
            invalue[0]= (struct berval *)&vlv_request_control->value; /* jcm: cast away const */
            invalue[1]= NULL;
            slapi_call_syntax_values2keys(pi,invalue,&typedown_value,LDAP_FILTER_EQUALITY); /* JCM SLOW FUNCTION */
            plugin_call_syntax_get_compare_fn( pi, &compare_fn );
            if (compare_fn == NULL) {
                LDAPDebug(LDAP_DEBUG_ANY, "vlv_trim_candidates_byvalue: "
                          "attempt to compare an unordered attribute",
                          0, 0, 0);
                compare_fn = slapi_berval_cmp;
            }
        }
    }
    else
    {
        typedown_value= vlv_create_matching_rule_value(sort_control->mr_pb,(struct berval *)&vlv_request_control->value);
        compare_fn= slapi_berval_cmp;
    }
    /*
     * Perform a binary search over the candidate list
     */
    do {
        int err= 0;
        struct backentry *e= NULL;
        if(!sort_control->order)
        {
            current = (low + high)/2;
        }
        else
        {
            current = (1 + low + high)/2;
        }
        id= candidates->b_ids[current];
        e = id2entry( be, id, NULL, &err );
    	if ( e == NULL )
    	{
    	    LDAPDebug( LDAP_DEBUG_ANY, "vlv_trim_candidates_byvalue: Candidate ID %lu not found err=%d\n", (u_long)id, err, 0 );
    	}
    	else
    	{
            /* Check if vlv_request_control->value is greater than or equal to the primary key. */
            int match;
        	Slapi_Attr *attr;
			if ( (NULL != compare_fn) && (slapi_entry_attr_find( e->ep_entry, sort_control->type, &attr ) == 0) )
			{
                /*
                 * If there's a matching rule associated with the primary
                 * attribute then use the indexer to mangle the attr values.
                 */
				Slapi_Value **csn_value = valueset_get_valuearray(&attr->a_present_values);
           		struct berval **entry_value = /* xxxPINAKI needs modification attr->a_vals */NULL;
				PRBool needFree = PR_FALSE;

                if(sort_control->mr_pb!=NULL)
                {
					struct berval **tmp_entry_value = NULL;

					valuearray_get_bervalarray(csn_value,&tmp_entry_value);
           			/* Matching rule. Do the magic mangling. Plugin owns the memory. */
           			matchrule_values_to_keys(sort_control->mr_pb,/* xxxPINAKI needs modification attr->a_vals */tmp_entry_value,&entry_value);
                }
				else
				{
					valuearray_get_bervalarray(csn_value,&entry_value);
					needFree = PR_TRUE; /* entry_value is a copy */
				}
                if(!sort_control->order)
                {
                    match= sort_attr_compare(entry_value, (struct berval**)typedown_value, compare_fn);
                }
                else
                {
                    match= sort_attr_compare((struct berval**)typedown_value, entry_value, compare_fn);
                }
		if (needFree) {
		    ber_bvecfree((struct berval**)entry_value);
		    entry_value = NULL;
		}
            }
            else
            {
                /*
                 * This attribute doesn't exist on this entry.
                 */
                if(sort_control->order)
                {
                    match= 1;
                }
                else
                {
                    match= 0;
                }
            }
            if(!sort_control->order)
            {
                if (match>=0)
                {
                    high= current;
    			}
                else
                {
                    low= current+1;
                }
            }
            else
            {
                if (match>=0)
                {
                    high= current-1;
    			}
                else
                {
                    low= current;
                }
            }
            if (low>=high)
            {
                found= 1;
                si= high;
                if(si==candidates->b_nids && !match)
                {
                    /* Couldn't find an entry which matches the value, so return contentCount */
                	LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_trim_candidates_byvalue: Not Found. Index %lu\n",si, 0, 0 );
                    si= candidates->b_nids;
                }
                else
                {
                	LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_trim_candidates_byvalue: Found. Index %lu\n",si, 0, 0 );
                }
    		}
    	}
	} while (!found);
	ber_bvecfree((struct berval**)typedown_value);
    return si;
}

/*
 * Encode the VLV RESPONSE control.
 *
 * Create a virtual list view response control,
 * and add it to the PBlock to be returned to the client.
 *
 * Returns:
 *   success ( 0 )
 *   operationsError (1),
 */
int
vlv_make_response_control (Slapi_PBlock *pb, const struct vlv_response* vlvp)
{
    BerElement *ber= NULL;    
	struct berval *bvp = NULL;
	int rc = -1;

	/*
     VirtualListViewResponse ::= SEQUENCE {
             targetPosition    INTEGER (0 .. maxInt),
             contentCount     INTEGER (0 .. maxInt),
             virtualListViewResult ENUMERATED {
                     success (0),
                     operationsError (1),
                     unwillingToPerform (53),
                     insufficientAccessRights (50),
                     busy (51),
                     timeLimitExceeded (3),
                     adminLimitExceeded (11),
                     sortControlMissing (60),
                     indexRangeError (61),
                     other (80) }  }
	 */

    if ( ( ber = ber_alloc()) == NULL )
    {
		return rc;
    }

    rc = ber_printf( ber, "{iie}", vlvp->targetPosition, vlvp->contentCount, vlvp->result );
    if ( rc != -1 )
    {
		rc = ber_flatten( ber, &bvp );
    }
    
	ber_free( ber, 1 );

    if ( rc != -1 )
    {        
    	LDAPControl	new_ctrl = {0};
    	new_ctrl.ldctl_oid = LDAP_CONTROL_VLVRESPONSE;
    	new_ctrl.ldctl_value = *bvp;
    	new_ctrl.ldctl_iscritical = 1;         
    	rc= slapi_pblock_set( pb, SLAPI_ADD_RESCONTROL, &new_ctrl );
        ber_bvfree(bvp);
    }

	LDAPDebug( LDAP_DEBUG_TRACE, "<= vlv_make_response_control: Index=%lu Size=%lu Result=%lu\n", vlvp->targetPosition, vlvp->contentCount, vlvp->result );

	return (rc==-1?LDAP_OPERATIONS_ERROR:LDAP_SUCCESS);
}

/* 
 * Generate a logging string for the vlv request and response
 */
void vlv_print_access_log(Slapi_PBlock *pb,struct vlv_request* vlvi, struct vlv_response *vlvo)
{
#define VLV_LOG_BS (21*6 + 4 + 5) /* space for 20-digit values for all parameters + 'VLV ' + status */
	char stack_buffer[VLV_LOG_BS];
	char *buffer = stack_buffer;
	char *p;
	
	if (vlvi->value.bv_len > 20) {
		buffer = slapi_ch_malloc(VLV_LOG_BS + vlvi->value.bv_len);
	}
	p = buffer;
	p+= sprintf(p,"VLV ");
	if (0 == vlvi->tag) {
		/* By Index case */
		p+= sprintf(p,"%ld:%ld:%ld:%ld",
			vlvi->beforeCount ,
			vlvi->afterCount ,
			vlvi->index ,
			vlvi->contentCount 
			);	
	} else {
		/* By value case */
#define VLV_LOG_SS 32
		char stack_string[VLV_LOG_SS];
		char *string = stack_string;

		if (vlvi->value.bv_len >= VLV_LOG_SS) {
            string = slapi_ch_malloc(vlvi->value.bv_len+1);
		}
        strncpy(string,vlvi->value.bv_val,vlvi->value.bv_len);
        string[vlvi->value.bv_len] = '\0';
		p += sprintf(p,"%ld:%ld:%s",
			vlvi->beforeCount ,
			vlvi->afterCount ,
			string
			);	
		if (string != stack_string) {
			slapi_ch_free( (void**)&string);
		}
	}
	/* Now the response info */
	p += sprintf(p," %ld:%ld (%ld)",
		vlvo->targetPosition ,
		vlvo->contentCount,
		vlvo->result
		);	
	

	ldbm_log_access_message(pb,buffer);

	if (buffer != stack_buffer) {
		slapi_ch_free( (void**)&buffer);
	}
}

/*
 * Decode the VLV REQUEST control.
 *
 * If the client sends Index==0 we behave as if I=1
 *
 * Returns:
 *       success (0),
 *       operationsError (1),
 *
 */
int
vlv_parse_request_control( backend *be, struct berval *vlv_spec_ber,struct vlv_request* vlvp)
{
	/* This control looks like this : 

     VirtualListViewRequest ::= SEQUENCE {
             beforeCount    INTEGER (0 .. maxInt),
             afterCount     INTEGER (0 .. maxInt),
             CHOICE {
                     byIndex [0] SEQUENCE {
                     index           INTEGER (0 .. maxInt),
                     contentCount    INTEGER (0 .. maxInt) }
                     greaterThanOrEqual [1] assertionValue }
   	*/
	BerElement *ber = NULL;
	int return_value = LDAP_SUCCESS;
	PRUint32 rc= 0;
	long long_beforeCount;
	long long_afterCount;
	long long_index;
	long long_contentCount;
	
	vlvp->value.bv_len = 0;
	vlvp->value.bv_val = NULL;

	ber = ber_init(vlv_spec_ber);
	rc = ber_scanf(ber,"{ii",&long_beforeCount,&long_afterCount);
	vlvp->beforeCount = long_beforeCount;
	vlvp->afterCount = long_afterCount;
	if (LBER_ERROR == rc)
	{
        return_value= LDAP_OPERATIONS_ERROR;
    }
    else
    {
        LDAPDebug( LDAP_DEBUG_TRACE, "vlv_parse_request_control: Before=%lu After=%lu\n", vlvp->beforeCount, vlvp->afterCount, 0 );
           rc = ber_scanf(ber,"t",&vlvp->tag);
        switch(vlvp->tag)
        {
        case LDAP_TAG_VLV_BY_INDEX:
            /* byIndex */
            vlvp->tag= 0;
            rc = ber_scanf(ber,"{ii}}",&long_index,&long_contentCount);
            vlvp->index = long_index;
            vlvp->contentCount = long_contentCount;
            if (LBER_ERROR == rc)
            {
                if (ISLEGACY(be)) {
                    return_value = LDAP_OPERATIONS_ERROR;
                } else {
                    return_value = LDAP_VIRTUAL_LIST_VIEW_ERROR;
                }
            }
            else
            {
                /* Client Counts from 1. */
                if(vlvp->index!=0)
                {
                    vlvp->index--;
                }
                LDAPDebug( LDAP_DEBUG_TRACE, "vlv_parse_request_control: Index=%lu Content=%lu\n", vlvp->index, vlvp->contentCount, 0 );
            }
            break;
        case LDAP_TAG_VLV_BY_VALUE:
            /* byValue */
            vlvp->tag= 1;
            rc = ber_scanf(ber,"o}",&vlvp->value);
            if (LBER_ERROR == rc)
            {
                if (ISLEGACY(be)) {
                    return_value = LDAP_OPERATIONS_ERROR;
                } else {
                    return_value = LDAP_VIRTUAL_LIST_VIEW_ERROR;
                }
            }
            {
            /* jcm: isn't there a utility fn to do this? */
            char *p= slapi_ch_malloc(vlvp->value.bv_len+1);
            strncpy(p,vlvp->value.bv_val,vlvp->value.bv_len);
            p[vlvp->value.bv_len]= '\0';
               LDAPDebug( LDAP_DEBUG_TRACE, "vlv_parse_request_control: Value=%s\n", p, 0, 0 );
            slapi_ch_free( (void**)&p);
            }
            break;
        default:
            if (ISLEGACY(be)) {
                return_value = LDAP_OPERATIONS_ERROR;
            } else {
                return_value = LDAP_VIRTUAL_LIST_VIEW_ERROR;
            }
        }
    }

   	/* the ber encoding is no longer needed */
   	ber_free(ber,1);

	return return_value;
}

/* given a slapi_filter, check if there's a vlv index that matches that
 * filter.  if so, return the IDL for that index (else return NULL).
 * -- a vlv index will match ONLY if that vlv index is subtree-scope and
 * has the same search base and search filter.
 * added read lock */

IDList *vlv_find_index_by_filter(struct backend *be, const char *base, 
				 Slapi_Filter *f)
{
    struct vlvSearch *t = NULL;
    struct vlvIndex *vi;
    Slapi_DN base_sdn;
    PRUint32 length;
    int err;
    DB *db = NULL;
    DBC *dbc = NULL;
    IDList *idl;
    Slapi_Filter *vlv_f;

	PR_RWLock_Rlock(be->vlvSearchList_lock); 
	slapi_sdn_init_dn_byref(&base_sdn, base);
	for (t = (struct vlvSearch *)be->vlvSearchList; t; t = t->vlv_next) {
		/* all vlv "filters" start with (|(xxx)(objectclass=referral)).
		 * we only care about the (xxx) part.
		 */
		vlv_f = t->vlv_slapifilter->f_or;
		if ((t->vlv_scope == LDAP_SCOPE_SUBTREE) &&
			(slapi_sdn_compare(t->vlv_base, &base_sdn) == 0) &&
			(slapi_filter_compare(vlv_f, f) == 0)) {
			/* found match! */
			slapi_sdn_done(&base_sdn);
			
			/* is there an index that's ready? */
			vi = t->vlv_index;
			while (!vlvIndex_online(vi) && vi) {
				vi = vi->vlv_next;
			}
			if (!vi) {
				/* no match */
				LDAPDebug(LDAP_DEBUG_TRACE, "vlv: no index online for %s\n",
					t->vlv_filter, 0, 0);
				PR_RWLock_Unlock(be->vlvSearchList_lock);
				return NULL;
			}
			
			if (dblayer_get_index_file(be, vi->vlv_attrinfo, &db, 0) == 0) {
				err = db->cursor(db, 0 /* txn */, &dbc, 0);
				if (err == 0) {
					length = vlvIndex_get_indexlength(vi, db, 0 /* txn */);
					if (length == 0) /* 609377: index size could be 0 */
					{
						LDAPDebug(LDAP_DEBUG_TRACE, "vlv: index %s is empty\n",
								t->vlv_filter, 0, 0);
						idl = NULL;
					}
					else
					{
						err = vlv_build_idl(0, length-1, db, dbc, &idl, 1 /* dosort */);
					}
					dbc->c_close(dbc);
				}
				dblayer_release_index_file(be, vi->vlv_attrinfo, db);
				if (err == 0) {
					PR_RWLock_Unlock(be->vlvSearchList_lock);
					return idl;
				} else {
					LDAPDebug(LDAP_DEBUG_ANY, "vlv find index: err %d\n",
						err, 0, 0);
					PR_RWLock_Unlock(be->vlvSearchList_lock);
					return NULL;
				}
			}
		}
    }
	PR_RWLock_Unlock(be->vlvSearchList_lock);
    /* no match */
    slapi_sdn_done(&base_sdn);
    return NULL;
}



/* replace c with c2 in string -- probably exists somewhere but I can't find it slapi maybe? */

static void replace_char(char *name, char c, char c2)
{   
        int x;

        for (x = 0; name[x] != '\0'; x++) {
                if (c == name[x]) {
                        name[x] = c2;
                }
        }
}

/* similar to what the console GUI does */

char *create_vlv_search_tag(const char* dn) {
	char  *tmp2=strdup(dn);

	replace_char(tmp2,',',' ');
	replace_char(tmp2,'"','-');
	replace_char(tmp2,'+','_');
	return tmp2;
}

/* Builds strings from Slapi_DN similar console GUI. Uses those dns to
   delete vlvsearch's if they match. New write lock.
 */

#define LDBM_PLUGIN_ROOT ", cn=ldbm database, cn=plugins, cn=config"
#define TAG "cn=by MCC "

int vlv_delete_search_entry(Slapi_PBlock *pb, Slapi_Entry* e, ldbm_instance *inst)
{
	int rc=0;
	Slapi_PBlock *tmppb;
	Slapi_DN *newdn;
	struct vlvSearch* p=NULL;
	char *buf, *buf2, *tag1, *tag2; 
	const char *dn= slapi_sdn_get_dn(&e->e_sdn);
	backend *be= inst->inst_be;

	tag1=create_vlv_search_tag(dn);
	buf=slapi_ch_smprintf("%s%s%s%s%s","cn=MCC ",tag1,", cn=",inst->inst_name,LDBM_PLUGIN_ROOT);
	newdn=slapi_sdn_new_dn_byval(buf);
	/* vlvSearchList is modified; need Wlock */
	PR_RWLock_Wlock(be->vlvSearchList_lock);
	p = vlvSearch_finddn((struct vlvSearch *)be->vlvSearchList, newdn);
	if(p!=NULL)
	{	
		LDAPDebug( LDAP_DEBUG_ANY, "Deleted Virtual List View Search (%s).\n", p->vlv_name, 0, 0);
		tag2=create_vlv_search_tag(dn);
		buf2=slapi_ch_smprintf("%s%s,%s",TAG,tag2,buf);
		vlvSearch_removefromlist((struct vlvSearch **)&be->vlvSearchList,p->vlv_dn);
		/* This line release lock to prevent recursive deadlock caused by slapi_internal_delete calling vlvDeleteSearchEntry */
		PR_RWLock_Unlock(be->vlvSearchList_lock); 
		vlvSearch_delete(&p);	
		tmppb = slapi_pblock_new();
		slapi_delete_internal_set_pb(tmppb, buf2, NULL, NULL,
								 (void *)plugin_get_default_component_id(), 0);
		slapi_delete_internal_pb(tmppb);
		slapi_pblock_get (tmppb, SLAPI_PLUGIN_INTOP_RESULT, &rc); 
		if(rc != LDAP_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_ANY, "vlv_delete_search_entry:can't delete dse entry '%s'\n", buf2, 0, 0);			
		}
		pblock_done(tmppb);
		pblock_init(tmppb);
		slapi_delete_internal_set_pb(tmppb, buf, NULL, NULL,
								 (void *)plugin_get_default_component_id(), 0);
		slapi_delete_internal_pb(tmppb);
		slapi_pblock_get (tmppb, SLAPI_PLUGIN_INTOP_RESULT, &rc); 
		if(rc != LDAP_SUCCESS) {
			  LDAPDebug(LDAP_DEBUG_ANY, "vlv_delete_search_entry:can't delete dse entry '%s'\n", buf, 0, 0);
		}
		slapi_pblock_destroy(tmppb);
		slapi_ch_free((void **)&tag2);
		slapi_ch_free((void **)&buf2);
    } else {
		PR_RWLock_Unlock(be->vlvSearchList_lock);
	}
	slapi_ch_free((void **)&tag1);
	slapi_ch_free((void **)&buf);
	slapi_sdn_free(&newdn);
	return rc;
}

void
vlv_acquire_lock(backend *be)
{
	LDAPDebug(LDAP_DEBUG_TRACE, "vlv_acquire_lock => trying to acquire the lock\n", 0, 0, 0);
	PR_RWLock_Wlock(be->vlvSearchList_lock);
}

void
vlv_release_lock(backend *be)
{
	LDAPDebug(LDAP_DEBUG_TRACE, "vlv_release_lock => trying to release the lock\n", 0, 0, 0);
	PR_RWLock_Unlock(be->vlvSearchList_lock);
}
