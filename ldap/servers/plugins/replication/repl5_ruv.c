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
 * Copyright (C) 2010 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/* repl5_ruv.c - implementation of replica update vector */
/*
 * The replica update vector is stored in the nsds50ruv attribute. The LDIF
 * representation of the ruv is:
 *  nsds50ruv: {replicageneration} <gen-id-for-this-replica>
 *  nsds50ruv: {replica <rid>[ <url>]}[ <mincsn> <maxcsn>]
 *  nsds50ruv: {replica <rid>[ <url>]}[ <mincsn> <maxcsn>]
 *  ... 
 *  nsds50ruv: {replica <rid>[ <url>]}[ <mincsn> <maxcsn>]
 *
 *  nsruvReplicaLastModified: {replica <rid>[ <url>]} lastModifiedTime
 *  nsruvReplicaLastModified: {replica <rid>[ <url>]} lastModifiedTime
 *  ... 
 *
 * For readability, ruv_dump appends nsruvReplicaLastModified to nsds50ruv:
 *  nsds50ruv: {replica <rid>[ <url>]}[ <mincsn> <maxcsn> [<lastModifiedTime>]]
 */

#include <string.h>
#include <ctype.h> /* For isdigit() */
#include "csnpl.h"
#include "repl5_ruv.h"
#include "repl_shared.h"
#include "repl5.h"

#define RIDSTR_SIZE 16	/* string large enough to hold replicaid*/
#define RUVSTR_SIZE 256	/* string large enough to hold ruv and lastmodifiedtime */

/* Data Structures */

/* replica */
typedef struct ruvElement
{
	ReplicaId rid;		/* replica id for this element */
	CSN *csn;	        /* largest csn that we know about that originated at the master */
	CSN *min_csn;	    /* smallest csn that originated at the master */
	char *replica_purl; /* Partial URL for replica */
    CSNPL *csnpl;       /* list of operations in progress */
	time_t last_modified;	/* timestamp the modification of csn */
} RUVElement;

/* replica update vector */
struct _ruv 
{
	char	  *replGen;	    /* replicated area generation: identifies replica
						       in space and in time */ 
	DataList  *elements;    /* replicas */	
	Slapi_RWLock  *lock;	    /* concurrency control */
};

/* forward declarations */
static int ruvInit (RUV **ruv, int initCount);
static void ruvFreeReplica (void **data);
static RUVElement* ruvGetReplica (const RUV *ruv, ReplicaId rid);
static RUVElement* ruvAddReplica (RUV *ruv, const CSN *csn, const char *replica_purl);
static RUVElement* ruvAddReplicaNoCSN (RUV *ruv, ReplicaId rid, const char *replica_purl);
static RUVElement* ruvAddIndexReplicaNoCSN (RUV *ruv, ReplicaId rid, const char *replica_purl, int index);
static int ruvReplicaCompare (const void *el1, const void *el2);
static RUVElement *get_ruvelement_from_berval(const struct berval *bval);
static char *get_replgen_from_berval(const struct berval *bval);

static const char * const prefix_replicageneration = "{replicageneration}";
static const char * const prefix_ruvcsn = "{replica "; /* intentionally missing '}' */


/* API implementation */


/*
 * Allocate a new ruv and set its replica generation to the given generation.
 */
int
ruv_init_new(const char *replGen, ReplicaId rid, const char *purl, RUV **ruv)
{
	int rc;
    RUVElement *replica;

	if (ruv == NULL || replGen == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_init_new: NULL argument\n");
		return RUV_BAD_DATA;
	}	

	rc = ruvInit (ruv, 0);
	if (rc != RUV_SUCCESS)
		return rc;

	(*ruv)->replGen = slapi_ch_strdup (replGen);

    /* we want to add the local writable replica to the RUV before any csns are created */
    /* this is so that it can be referred to even before it accepted any changes */ 
    if (purl) 
    {  
        replica = ruvAddReplicaNoCSN (*ruv, rid, purl);

        if (replica == NULL)
            return RUV_MEMORY_ERROR;
    }

    return RUV_SUCCESS;
}

int ruv_private_new(RUV **ruv, RUV *clone )
{
		

	int rc;
	rc = ruvInit (ruv, dl_get_count(clone->elements) );
	if (rc != RUV_SUCCESS)
		return rc;

	(*ruv)->replGen = slapi_ch_strdup (clone->replGen);

	   return RUV_SUCCESS;
}

/*
 * Create a new RUV and initialize its contents from the provided Slapi_Attr.
 * Returns:
 * RUV_BAD_DATA if the values in the attribute were malformed.
 * RUV_SUCCESS if all went well
 */
int
ruv_init_from_slapi_attr(Slapi_Attr *attr, RUV **ruv)
{
	ReplicaId dummy = 0;

	return (ruv_init_from_slapi_attr_and_check_purl(attr, ruv, &dummy));
}

/*
 * Create a new RUV and initialize its contents from the provided Slapi_Attr.
 * Returns:
 * RUV_BAD_DATA if the values in the attribute were malformed.
 * RUV_SUCCESS if all went well
 * contain_purl is 0 if the ruv doesn't contain the local purl
 * contain_purl is != 0 if the ruv contains the local purl (contains the rid)
 */
int
ruv_init_from_slapi_attr_and_check_purl(Slapi_Attr *attr, RUV **ruv, ReplicaId *contain_purl)
{
	int return_value;

	PR_ASSERT(NULL != attr && NULL != ruv);

	if (NULL == ruv || NULL == attr)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
			"ruv_init_from_slapi_attr: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		int rc;
		int numvalues;
		slapi_attr_get_numvalues(attr, &numvalues);
		if ((rc = ruvInit(ruv, numvalues)) != RUV_SUCCESS)
		{
			return_value = rc;
		}
		else
		{
			int hint;
			Slapi_Value *value;
			const struct berval *bval;
			const char *purl = NULL;
			char *localhost = get_localhost_DNS();
			size_t localhostlen = localhost ? strlen(localhost) : 0;
			int port = config_get_port();

			return_value = RUV_SUCCESS;

			purl = multimaster_get_local_purl();
			*contain_purl = 0;

			for (hint = slapi_attr_first_value(attr, &value);
				hint != -1; hint = slapi_attr_next_value(attr, hint, &value))
			{
				bval = slapi_value_get_berval(value);
				if (NULL != bval && NULL != bval->bv_val)
				{
					if (strncmp(bval->bv_val, prefix_replicageneration, strlen(prefix_replicageneration)) == 0) {
						if (NULL == (*ruv)->replGen)
						{
							(*ruv)->replGen = get_replgen_from_berval(bval);
						} else {
							/* Twice replicageneration is wrong, just log and ignore */
							slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
											"ruv_init_from_slapi_attr: %s is present more than once\n", 
											prefix_replicageneration);
						}
					}
					else
					{
						RUVElement *ruve = get_ruvelement_from_berval(bval);
						if (NULL != ruve)
						{
							char *ptr;
							/* Is the local purl already in the ruv ? */
							if ( (*contain_purl==0) && ruve->replica_purl && purl && (strncmp(ruve->replica_purl, purl, strlen(purl))==0) )
							{
								*contain_purl = ruve->rid;
							}
							/* ticket 47362 - nsslapd-port: 0 causes replication to break */
							else if ((*contain_purl==0) && ruve->replica_purl && (port == 0) && localhost &&
								 (ptr = strstr(ruve->replica_purl, localhost)) && (ptr != ruve->replica_purl) &&
								 (*(ptr - 1) == '/') && (*(ptr+localhostlen) == ':'))
							{
								/* same hostname, but port number may have been temporarily set to 0
								 * just allow it with whatever port number is already in the replica_purl
								 * do not reset the port number, do not tell the configure_ruv code that there
								 * is anything wrong
								 */
								*contain_purl = ruve->rid;
							}
							dl_add ((*ruv)->elements, ruve);
						}
					}
				}
			}
			slapi_ch_free_string(&localhost);
		}
	}
	return return_value;
}
					


/*
 * Same as ruv_init_from_slapi_attr, but takes an array of pointers to bervals.
 * I wish this wasn't a cut-n-paste of the above function, but I don't see a
 * clean way to define one API in terms of the other.
 */
int
ruv_init_from_bervals(struct berval **vals, RUV **ruv)
{
	int return_value;

	PR_ASSERT(NULL != vals && NULL != ruv);

	if (NULL == ruv || NULL == vals)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
			"ruv_init_from_slapi_value: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		int i, rc;
		i = 0;
		while (vals[i] != NULL)
		{
			i++;
		}
		if ((rc = ruvInit (ruv, i)) != RUV_SUCCESS)
		{
			return_value = rc;
		}
		else
		{
			return_value = RUV_SUCCESS;
			for (i = 0; NULL != vals[i]; i++)
			{
				if (NULL != vals[i]->bv_val)
				{
					if (strncmp(vals[i]->bv_val, prefix_replicageneration, strlen(prefix_replicageneration)) == 0) {
						if (NULL == (*ruv)->replGen)
						{
							(*ruv)->replGen = get_replgen_from_berval(vals[i]);
						} else {
							/* Twice replicageneration is wrong, just log and ignore */
							slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
											"ruv_init_from_slapi_value: %s is present more than once\n", 
											prefix_replicageneration);
						}
					}
					else
					{
						RUVElement *ruve = get_ruvelement_from_berval(vals[i]);
						if (NULL != ruve)
						{
							dl_add ((*ruv)->elements, ruve);
						}
					}
				}
			}
		}
	}
	return return_value;
}



RUV* 
ruv_dup (const RUV *ruv)
{
	int rc;
	RUVElement *replica, *dupReplica;
	int cookie;
	RUV *dupRUV = NULL;

	if (ruv == NULL)
		return NULL;

	slapi_rwlock_rdlock (ruv->lock);

	rc = ruvInit (&dupRUV, dl_get_count (ruv->elements));
	if (rc != RUV_SUCCESS || dupRUV == NULL)
		goto done;

	dupRUV->replGen = slapi_ch_strdup (ruv->replGen);

	for (replica = dl_get_first (ruv->elements, &cookie); replica;
		 replica = dl_get_next (ruv->elements, &cookie))
	{
		dupReplica = (RUVElement *)slapi_ch_calloc (1, sizeof (*dupReplica));
		dupReplica->rid = replica->rid;	
		if (replica->csn)		
			dupReplica->csn = csn_dup (replica->csn);
		if (replica->min_csn)		
			dupReplica->min_csn = csn_dup (replica->min_csn);
		if (replica->replica_purl)
			dupReplica->replica_purl = slapi_ch_strdup (replica->replica_purl);
		dupReplica->last_modified = replica->last_modified;

		/* ONREPL - we don't make copy of the pernding list. For now
		   we don't need it. */

		dl_add (dupRUV->elements, dupReplica);
	} 

done:
	slapi_rwlock_unlock (ruv->lock);
	
	return dupRUV;
}

void
ruv_destroy (RUV **ruv)
{
	if (ruv != NULL && *ruv != NULL) 
	{
		if ((*ruv)->elements)
		{
			dl_cleanup ((*ruv)->elements, ruvFreeReplica);
			dl_free (&(*ruv)->elements);
		}
		/* slapi_ch_free accepts NULL pointer */
		slapi_ch_free ((void **)&((*ruv)->replGen));

		if ((*ruv)->lock)
		{
			slapi_destroy_rwlock ((*ruv)->lock);
		}
        
		slapi_ch_free ((void**)ruv);
	}
}

/*
 * [610948]
 * copy elements in srcruv to destruv
 * destruv is the live wrapper, which could be referred by other threads.
 * srcruv is cleaned up after copied.
 */
void
ruv_copy_and_destroy (RUV **srcruv, RUV **destruv)
{
	DataList *elemp = NULL;
	char *replgp = NULL;

	if (NULL == srcruv || NULL == *srcruv || NULL == destruv)
	{
		return;
	}

	if (NULL == *destruv)
	{
		*destruv = *srcruv;
		*srcruv = NULL;
	}
	else
	{
		slapi_rwlock_wrlock((*destruv)->lock);
		elemp = (*destruv)->elements;
		(*destruv)->elements = (*srcruv)->elements;
		if (elemp)
		{
			dl_cleanup (elemp, ruvFreeReplica);
			dl_free (&elemp);
		}

		/* slapi_ch_free accepts NULL pointer */
		replgp = (*destruv)->replGen;
		(*destruv)->replGen = (*srcruv)->replGen;
		slapi_ch_free ((void **)&replgp);

		if ((*srcruv)->lock)
		{
			slapi_destroy_rwlock ((*srcruv)->lock);
		}
		slapi_ch_free ((void**)srcruv);

		slapi_rwlock_unlock((*destruv)->lock);
	}
    PR_ASSERT (*destruv != NULL && *srcruv == NULL);
}

int
ruv_delete_replica (RUV *ruv, ReplicaId rid)
{
	int return_value;
	if (ruv == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_delete_replica: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}	
	else
	{
		/* check for duplicates */
		slapi_rwlock_wrlock (ruv->lock);
		dl_delete (ruv->elements, (const void*)&rid, ruvReplicaCompare, ruvFreeReplica); 
		slapi_rwlock_unlock (ruv->lock);
		return_value = RUV_SUCCESS;
	}
	return return_value;
}

int 
ruv_add_replica (RUV *ruv, ReplicaId rid, const char *replica_purl)
{
    RUVElement* replica;

    PR_ASSERT (ruv && replica_purl);

    slapi_rwlock_wrlock (ruv->lock);
    replica = ruvGetReplica (ruv, rid);
    if (replica == NULL){
        replica = ruvAddReplicaNoCSN (ruv, rid, replica_purl);
    } else {
        if(strcasecmp(replica->replica_purl, replica_purl )){ /* purls are different - replace it */
            ruv_replace_replica_purl_nolock(ruv, rid, replica_purl, RUV_DONT_LOCK);
        }
    }
    slapi_rwlock_unlock (ruv->lock);

    if (replica)
        return RUV_SUCCESS;
    else
        return RUV_MEMORY_ERROR;
}

int 
ruv_replace_replica_purl (RUV *ruv, ReplicaId rid, const char *replica_purl)
{
	return ruv_replace_replica_purl_nolock(ruv, rid, replica_purl, RUV_LOCK);
}

int
ruv_replace_replica_purl_nolock(RUV *ruv, ReplicaId rid, const char *replica_purl, int lock)
{
    RUVElement* replica;
    int rc = RUV_NOTFOUND;

    PR_ASSERT (ruv && replica_purl);

    if(lock)
        slapi_rwlock_wrlock (ruv->lock);

    replica = ruvGetReplica (ruv, rid);
    if (replica != NULL)
    {
        if (replica->replica_purl == NULL || strcmp(replica->replica_purl, replica_purl)) { /* purl updated */
            /* Replace replica_purl in RUV since supplier has been updated. */
            slapi_ch_free_string(&replica->replica_purl);
            replica->replica_purl = slapi_ch_strdup(replica_purl);
            /* Also, reset csn and min_csn. */
            replica->csn = replica->min_csn = NULL;
        }
        rc = RUV_SUCCESS;
    }

    if(lock)
        slapi_rwlock_unlock (ruv->lock);

    return rc;
}

int 
ruv_add_index_replica (RUV *ruv, ReplicaId rid, const char *replica_purl, int index)
{
    RUVElement* replica;

    PR_ASSERT (ruv && replica_purl);

    slapi_rwlock_wrlock (ruv->lock);
    replica = ruvGetReplica (ruv, rid);
    if (replica == NULL)
    {
        replica = ruvAddIndexReplicaNoCSN (ruv, rid, replica_purl, index);
    }

    slapi_rwlock_unlock (ruv->lock);

    if (replica)
        return RUV_SUCCESS;
    else
        return RUV_MEMORY_ERROR;
}


PRBool 
ruv_contains_replica (const RUV *ruv, ReplicaId rid)
{
    RUVElement *replica;

    if (ruv == NULL)
        return PR_FALSE;

    slapi_rwlock_rdlock (ruv->lock);
	replica = ruvGetReplica (ruv, rid);
    slapi_rwlock_unlock (ruv->lock);

    return replica != NULL;
}




#define GET_LARGEST_CSN 231
#define GET_SMALLEST_CSN 232
static int
get_csn_internal(const RUV *ruv, ReplicaId rid, CSN **csn, int whichone)
{
	RUVElement *replica;
	int return_value = RUV_SUCCESS;

	if (ruv == NULL || csn == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_get_largest_csn_for_replica: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		*csn = NULL;
		/* prevent element from being destroyed while we get its data */
		slapi_rwlock_rdlock (ruv->lock);

		replica = ruvGetReplica (ruv, rid);
        /* replica without min csn is treated as a non-existent replica */
		if (replica == NULL || replica->min_csn == NULL)
		{
			return_value = RUV_NOTFOUND;
		}
		else
		{
			switch (whichone)
			{
			case GET_LARGEST_CSN:
				*csn = replica->csn ? csn_dup (replica->csn) : NULL;	
				break;
			case GET_SMALLEST_CSN:
				*csn = replica->min_csn ? csn_dup (replica->min_csn) : NULL;	
				break;
			default:
				*csn = NULL;
			}
		}
		slapi_rwlock_unlock (ruv->lock);	
	}
	return return_value;
}


int
ruv_get_largest_csn_for_replica(const RUV *ruv, ReplicaId rid, CSN **csn)
{
	return get_csn_internal(ruv, rid, csn, GET_LARGEST_CSN);
}

int
ruv_get_smallest_csn_for_replica(const RUV *ruv, ReplicaId rid, CSN **csn)
{
	return get_csn_internal(ruv, rid, csn, GET_SMALLEST_CSN);
}

const char *
ruv_get_purl_for_replica(const RUV *ruv, ReplicaId rid)
{
	RUVElement *replica;
	const char *return_value = NULL;

    slapi_rwlock_rdlock (ruv->lock);

	replica = ruvGetReplica (ruv, rid);
	if (replica != NULL)
	{
		return_value = replica->replica_purl;
	}

    slapi_rwlock_unlock (ruv->lock);

	return return_value;
}


static int
set_min_csn_nolock(RUV *ruv, const CSN *min_csn, const char *replica_purl)
{
    int return_value;
    ReplicaId rid = csn_get_replicaid (min_csn);
    RUVElement *replica = ruvGetReplica (ruv, rid);
    if (NULL == replica)
    {
        replica = ruvAddReplica (ruv, min_csn, replica_purl);
        if (replica)
            return_value = RUV_SUCCESS;
        else
            return_value = RUV_MEMORY_ERROR;
    }
    else
    {
        if (replica->min_csn == NULL || csn_compare (min_csn, replica->min_csn) < 0)
        {
		    csn_free(&replica->min_csn);
		    replica->min_csn = csn_dup(min_csn);
        }

		return_value = RUV_SUCCESS;
    }

    return return_value;
}

static int
set_max_csn_nolock(RUV *ruv, const CSN *max_csn, const char *replica_purl)
{
	int return_value;
	ReplicaId rid = csn_get_replicaid (max_csn);
	RUVElement *replica = ruvGetReplica (ruv, rid);
    if (NULL == replica)
    {
	    replica = ruvAddReplica (ruv, max_csn, replica_purl);
        if (replica)
            return_value = RUV_SUCCESS;
        else
            return_value = RUV_MEMORY_ERROR;
	}
	else
	{
        if (replica_purl && replica->replica_purl == NULL)
            replica->replica_purl = slapi_ch_strdup (replica_purl);    
		csn_free(&replica->csn);
		replica->csn = csn_dup(max_csn);
		replica->last_modified = current_time();
		return_value = RUV_SUCCESS;
	}
	return return_value;
}

int
ruv_set_min_csn(RUV *ruv, const CSN *min_csn, const char *replica_purl)
{
	int return_value;
	slapi_rwlock_wrlock (ruv->lock);
	return_value = set_min_csn_nolock(ruv, min_csn, replica_purl);
	slapi_rwlock_unlock (ruv->lock);
	return return_value;
}


int
ruv_set_max_csn(RUV *ruv, const CSN *max_csn, const char *replica_purl)
{
	int return_value;
	slapi_rwlock_wrlock (ruv->lock);
	return_value = set_max_csn_nolock(ruv, max_csn, replica_purl);
	slapi_rwlock_unlock (ruv->lock);
	return return_value;
}

int
ruv_set_csns(RUV *ruv, const CSN *csn, const char *replica_purl)
{
	RUVElement *replica;
	ReplicaId rid;
	int return_value;
	
	if (ruv == NULL || csn == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_set_csns: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		rid = csn_get_replicaid (csn);

		/* prevent element from being destroyed while we get its data */
		slapi_rwlock_wrlock (ruv->lock);

		replica = ruvGetReplica (ruv, rid);
		if (replica == NULL) /* add new replica */
		{
			replica = ruvAddReplica (ruv, csn, replica_purl);
            if (replica)
                return_value = RUV_SUCCESS;
            else
                return_value = RUV_MEMORY_ERROR;
		}
		else
		{
			if (csn_compare (csn, replica->csn) > 0)
			{
				if (replica->csn != NULL)
				{
					csn_init_by_csn ( replica->csn, csn );
				}
				else
				{
					replica->csn = csn_dup(csn);
				}
				replica->last_modified = current_time();
				if (replica_purl && (NULL == replica->replica_purl ||
					strcmp(replica->replica_purl, replica_purl) != 0))
				{
					/* slapi_ch_free accepts NULL pointer */
					slapi_ch_free((void **)&replica->replica_purl);

					replica->replica_purl = slapi_ch_strdup(replica_purl);
				}
			}
			/* XXXggood only need to worry about this if real min csn not committed to changelog yet */
			if (csn_compare (csn, replica->min_csn) < 0)
			{
				csn_free(&replica->min_csn);
				replica->min_csn = csn_dup(csn);
			}
			return_value = RUV_SUCCESS;
		}

		slapi_rwlock_unlock (ruv->lock);
	}
	return return_value;
}

/* This function, for each replica keeps the smallest CSN its seen so far.
   Used for initial setup of changelog purge vector */

int 
ruv_set_csns_keep_smallest(RUV *ruv, const CSN *csn)
{
    RUVElement *replica;
	ReplicaId rid;
	int return_value;
	
	if (ruv == NULL || csn == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
                        "ruv_set_csns_keep_smallest: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		rid = csn_get_replicaid (csn);

		/* prevent element from being destroyed while we get its data */
		slapi_rwlock_wrlock (ruv->lock);

		replica = ruvGetReplica (ruv, rid);
		if (replica == NULL) /* add new replica */
		{
			replica = ruvAddReplica (ruv, csn, NULL);
            if (replica)
                return_value = RUV_SUCCESS;
            else
                return_value = RUV_MEMORY_ERROR;
		}
		else
		{
			if (csn_compare (csn, replica->csn) < 0)
			{
				csn_free(&replica->csn);
				replica->csn = csn_dup(csn);
				replica->last_modified = current_time();
			}
			
			return_value = RUV_SUCCESS;
		}

		slapi_rwlock_unlock (ruv->lock);
	}
	return return_value;
}


void
ruv_set_replica_generation(RUV *ruv, const char *csnstr)
{
	if (NULL != csnstr && NULL != ruv)
	{
        slapi_rwlock_wrlock (ruv->lock);

		if (NULL != ruv->replGen)
		{
			slapi_ch_free((void **)&ruv->replGen);
		}
		ruv->replGen = slapi_ch_strdup(csnstr);
    
        slapi_rwlock_unlock (ruv->lock);
	}
}


char *
ruv_get_replica_generation(const RUV *ruv)
{
	char *return_str = NULL;

	if (!ruv) {
		return return_str;
	}

	slapi_rwlock_rdlock (ruv->lock);

	if (ruv != NULL && ruv->replGen != NULL)
	{
		return_str = slapi_ch_strdup(ruv->replGen);
	}

	slapi_rwlock_unlock (ruv->lock);

	return return_str;
}

static PRBool
ruv_covers_csn_internal(const RUV *ruv, const CSN *csn, PRBool strict)
{
	RUVElement *replica;
	ReplicaId rid;
	PRBool return_value;

	if (ruv == NULL || csn == NULL) 
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_covers_csn: NULL argument\n");
		return_value = PR_FALSE;
	}
	else
	{
		rid = csn_get_replicaid(csn);
		replica = ruvGetReplica (ruv, rid);
		if (replica == NULL)
		{
			/*
			 *  We don't know anything about this replica change in the cl, mark it to be zapped.
			 *  This could of been a previously cleaned ruv, but the server was restarted before
			 *  the change could be trimmed.
			 *
			 *  Only the change log trimming calls this function with "strict" set.  So we'll return success
			 *  if strict is set.
			 */
			if(strict){
				slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_covers_csn: replica for id %d not found.\n", rid);
				return_value = PR_TRUE;
			} else {
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "ruv_covers_csn: replica for id %d not found.\n", rid);
				return_value = PR_FALSE;
			}
		}
		else
		{
			if (strict)
			{
				return_value = (csn_compare (csn, replica->csn) < 0);
			}
			else
			{
				return_value = (csn_compare (csn, replica->csn) <= 0);
			}
		}
	}
	return return_value;
}

PRBool
ruv_covers_csn(const RUV *ruv, const CSN *csn)
{
    PRBool rc;

    slapi_rwlock_rdlock (ruv->lock);
	rc = ruv_covers_csn_internal(ruv, csn, PR_FALSE);
    slapi_rwlock_unlock (ruv->lock);

    return rc;
}

PRBool
ruv_covers_csn_strict(const RUV *ruv, const CSN *csn)
{
    PRBool rc;

    slapi_rwlock_rdlock (ruv->lock);
	rc = ruv_covers_csn_internal(ruv, csn, PR_TRUE);
    slapi_rwlock_unlock (ruv->lock);

    return rc;
}

/*
 *  Used by the cleanallruv task
 *
 *  We want to return TRUE if replica is NULL,
 *  and we want to use "csn_compare() <="
 */
PRBool
ruv_covers_csn_cleanallruv(const RUV *ruv, const CSN *csn)
{
	RUVElement *replica;
	ReplicaId rid;
	PRBool return_value;

	if (ruv == NULL || csn == NULL){
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_covers_csn_cleanallruv: NULL argument\n");
		return_value = PR_FALSE;
	} else {
		rid = csn_get_replicaid(csn);
		replica = ruvGetReplica (ruv, rid);
		if (replica == NULL){
			/* already cleaned */
			return_value = PR_TRUE;
		} else {
			return_value = (csn_compare (csn, replica->csn) <= 0);
		}
	}

	return return_value;
}

/*
 * The function gets min{maxcsns of all ruv elements} if get_the_max=0,
 * or max{maxcsns of all ruv elements} if get_the_max != 0.
 */
static int
ruv_get_min_or_max_csn(const RUV *ruv, CSN **csn, int get_the_max, ReplicaId rid)
{
	int return_value;

	if (ruv == NULL || csn == NULL) 
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_get_min_or_max_csn: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		CSN *found = NULL;
		RUVElement *replica;
		int cookie;
		slapi_rwlock_rdlock (ruv->lock);
		for (replica = dl_get_first (ruv->elements, &cookie); replica;
			 replica = dl_get_next (ruv->elements, &cookie))
		{
			/*
			 * Skip replica whose maxcsn is NULL otherwise
			 * the code will return different min_csn if
			 * the sequence of the replicas is altered.
			 * 
			 * don't use READ_ONLY replicas for computing the value of
			 * "found", as they seem to have NULL csn and min_csn 
			 */ 
			if (replica->csn == NULL || replica->rid == READ_ONLY_REPLICA_ID)
			{
				continue;
			}
			if(rid){ /* we are only interested in this rid's maxcsn */
				if(replica->rid == rid){
					found = replica->csn;
					break;
				}
			} else {
				if (found == NULL ||
					(!get_the_max && csn_compare(found, replica->csn)>0) ||
					( get_the_max && csn_compare(found, replica->csn)<0))
				{
					found = replica->csn;
				}
			}
		} 
		if (found == NULL)
		{
			*csn = NULL;	
		}
		else
		{
			*csn = csn_dup (found);
		}
		slapi_rwlock_unlock (ruv->lock);
		return_value = RUV_SUCCESS;	
	}
	return return_value;
}

int
ruv_get_rid_max_csn(const RUV *ruv, CSN **csn, ReplicaId rid){
	return ruv_get_min_or_max_csn(ruv, csn, 1 /* get the max */, rid);
}

int
ruv_get_max_csn(const RUV *ruv, CSN **csn)
{
	return ruv_get_min_or_max_csn(ruv, csn, 1 /* get the max */, 0 /* rid */);
}

int
ruv_get_min_csn(const RUV *ruv, CSN **csn)
{
	return ruv_get_min_or_max_csn(ruv, csn, 0 /* get the min */, 0 /* rid */);
}

int 
ruv_enumerate_elements (const RUV *ruv, FNEnumRUV fn, void *arg)
{
    int cookie;
    RUVElement *elem;
    int rc = 0;
    ruv_enum_data enum_data = {0};

    if (ruv == NULL || fn == NULL)
    {
        /* ONREPL - log error */
        return -1;
    }

    slapi_rwlock_rdlock (ruv->lock);
    for (elem = (RUVElement*)dl_get_first (ruv->elements, &cookie); elem;
         elem = (RUVElement*)dl_get_next (ruv->elements, &cookie))
    {
        /* we only return elements that contains both minimal and maximal CSNs */
        if (elem->csn && elem->min_csn)
        {
            enum_data.csn = elem->csn;
            enum_data.min_csn = elem->min_csn;
            rc = fn (&enum_data, arg);
            if (rc != 0)
                break;        
        }
    }
    
    slapi_rwlock_unlock (ruv->lock);

    return rc;
}

void
ruv_element_to_string(RUVElement *ruvelem, struct berval *bv, char *buf, size_t bufsize)
{
	char csnStr1[CSN_STRSIZE];
	char csnStr2[CSN_STRSIZE];
	const char *fmtstr = "%s%d%s%s}%s%s%s%s";
	if (buf && bufsize) {
		PR_snprintf(buf, bufsize, fmtstr,
					prefix_ruvcsn, ruvelem->rid,
					ruvelem->replica_purl == NULL ? "" : " ",
					ruvelem->replica_purl == NULL ? "" : ruvelem->replica_purl,
					ruvelem->min_csn == NULL ? "" : " ",
					ruvelem->min_csn == NULL ? "" : csn_as_string (ruvelem->min_csn, PR_FALSE, csnStr1),
					ruvelem->csn == NULL ? "" : " ",
					ruvelem->csn == NULL ? "" : csn_as_string (ruvelem->csn, PR_FALSE, csnStr2));
	} else {
		bv->bv_val = slapi_ch_smprintf(fmtstr,
									   prefix_ruvcsn, ruvelem->rid,
									   ruvelem->replica_purl == NULL ? "" : " ",
									   ruvelem->replica_purl == NULL ? "" : ruvelem->replica_purl,
									   ruvelem->min_csn == NULL ? "" : " ",
									   ruvelem->min_csn == NULL ? "" : csn_as_string (ruvelem->min_csn, PR_FALSE, csnStr1),
									   ruvelem->csn == NULL ? "" : " ",
									   ruvelem->csn == NULL ? "" : csn_as_string (ruvelem->csn, PR_FALSE, csnStr2));
		bv->bv_len = strlen(bv->bv_val);
	}
}

/*
 * Convert a replica update vector to a NULL-terminated array
 * of bervals. The caller is responsible for freeing the bervals.
 */
int
ruv_to_bervals(const RUV *ruv, struct berval ***bvals)
{
	struct berval **returned_bervals = NULL;
	int return_value;
	if (ruv == NULL || bvals == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_to_bervals: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		int count;
		int i;
		RUVElement *replica;
		int cookie;
		slapi_rwlock_rdlock (ruv->lock);
		count = dl_get_count (ruv->elements) + 2;
		returned_bervals = (struct berval **)slapi_ch_malloc(sizeof(struct berval *) * count);
		returned_bervals[count - 1] = NULL;
		returned_bervals[0] = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
		returned_bervals[0]->bv_val = slapi_ch_smprintf("%s %s",
			prefix_replicageneration, ruv->replGen);
		returned_bervals[0]->bv_len = strlen(returned_bervals[0]->bv_val);
		for (i = 1, replica = dl_get_first (ruv->elements, &cookie); replica;
			 i++, replica = dl_get_next (ruv->elements, &cookie))
		{
			returned_bervals[i] = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
			ruv_element_to_string(replica, returned_bervals[i], NULL, 0);
		}
		slapi_rwlock_unlock (ruv->lock);
		return_value = RUV_SUCCESS;
		*bvals = returned_bervals;
	}
	return return_value;
}

void
ruv_get_cleaned_rids(RUV *ruv, ReplicaId *rids)
{
    RUVElement *replica;
    int cookie;
    int i = 0;

    for (replica = dl_get_first (ruv->elements, &cookie); replica;
         replica = dl_get_next (ruv->elements, &cookie))
    {
        if(is_cleaned_rid(replica->rid)){
            rids[i++] = replica->rid;
        }
    }
}

/* 
 * This routine dump the ruv (last modified time) into a value array
 * the call must free the returned value array
 */
Slapi_Value **
ruv_last_modified_to_valuearray(RUV *ruv)
{
        RUVElement *ruv_e;
        int cookie;
        Slapi_Value *value;
        Slapi_Value **values = NULL;
        char *buffer;


        /* Acquire the ruv lock */
        slapi_rwlock_rdlock(ruv->lock);

        /* Now loop for each RUVElement and store its string value into the valueset*/
        for (ruv_e = dl_get_first(ruv->elements, &cookie);
                NULL != ruv_e;
                ruv_e = dl_get_next(ruv->elements, &cookie)) {
                			
                buffer = slapi_ch_smprintf("%s%d%s%s} %08lx", prefix_ruvcsn, ruv_e->rid,
			    ruv_e->replica_purl == NULL ? "" : " ",
			    ruv_e->replica_purl == NULL ? "" : ruv_e->replica_purl,
			    ruv_e->last_modified);
                value = slapi_value_new_string_passin(buffer);
                valuearray_add_value(&values, value);
                slapi_value_free(&value);
        }
        
        slapi_rwlock_unlock(ruv->lock);

        return (values);
}

/* 
 * This routine dump the ruv (replicageneration and ruvElements) into a value array
 * the call must free the returned value array
 */
Slapi_Value **
ruv_to_valuearray(RUV *ruv)
{
        RUVElement *ruv_e;
        int cookie;
        Slapi_Value *value;
        Slapi_Value **values = NULL;
        struct berval bv;
        char *buffer; 

        /* Acquire the ruv lock */
        slapi_rwlock_rdlock(ruv->lock);
        
        /* dump the replicageneration */
        buffer = slapi_ch_smprintf("%s %s", prefix_replicageneration, ruv->replGen);
        value = slapi_value_new_string_passin(buffer);
        valuearray_add_value(&values, value);
        slapi_value_free(&value);

        /* Now loop for each RUVElement and store its string value into the valueset*/
        for (ruv_e = dl_get_first(ruv->elements, &cookie);
                NULL != ruv_e;
                ruv_e = dl_get_next(ruv->elements, &cookie)) {

                ruv_element_to_string(ruv_e, &bv, NULL, 0);
                value = slapi_value_new_berval(&bv);
                slapi_ber_bvdone(&bv);
                valuearray_add_value(&values, value);
                slapi_value_free(&value);
        }
        
        slapi_rwlock_unlock(ruv->lock);

        return (values);
}

int
ruv_to_smod(const RUV *ruv, Slapi_Mod *smod)
{
	int return_value;

	if (ruv == NULL || smod == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_to_smod: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		struct berval val;
		RUVElement *replica;
		int cookie;
#define B_SIZ 1024
		char buf[B_SIZ];
		slapi_rwlock_rdlock (ruv->lock);
		slapi_mod_init (smod, dl_get_count (ruv->elements) + 1);
		slapi_mod_set_type (smod, type_ruvElement);
		slapi_mod_set_operation (smod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
		PR_snprintf(buf, sizeof(buf), "%s %s", prefix_replicageneration, ruv->replGen);
		val.bv_val = buf;
		val.bv_len = strlen(buf);
		slapi_mod_add_value(smod, &val);
		for (replica = dl_get_first (ruv->elements, &cookie); replica;
			 replica = dl_get_next (ruv->elements, &cookie))
		{
			ruv_element_to_string(replica, NULL, buf, sizeof(buf));
			val.bv_val = buf;
			val.bv_len = strlen(buf);
			slapi_mod_add_value(smod, &val);
		}
		slapi_rwlock_unlock (ruv->lock);
		return_value = RUV_SUCCESS;
	}
	return return_value;
}

int
ruv_last_modified_to_smod(const RUV *ruv, Slapi_Mod *smod)
{
	int return_value;

	if (ruv == NULL || smod == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_last_modified_to_smod: NULL argument\n");
		return_value = RUV_BAD_DATA;
	}
	else
	{
		struct berval val;
		RUVElement *replica;
		int cookie;
		char buf[B_SIZ];
		slapi_rwlock_rdlock (ruv->lock);
		slapi_mod_init (smod, dl_get_count (ruv->elements));
		slapi_mod_set_type (smod, type_ruvElementUpdatetime);
		slapi_mod_set_operation (smod, LDAP_MOD_REPLACE | LDAP_MOD_BVALUES);
		val.bv_val = buf;
		for (replica = dl_get_first (ruv->elements, &cookie); replica;
			 replica = dl_get_next (ruv->elements, &cookie))
		{
			PR_snprintf(buf, B_SIZ, "%s%d%s%s} %08lx", prefix_ruvcsn, replica->rid,
			    replica->replica_purl == NULL ? "" : " ",
			    replica->replica_purl == NULL ? "" : replica->replica_purl,
			    replica->last_modified);
			val.bv_len = strlen(buf);
			slapi_mod_add_value(smod, &val);
		}
		slapi_rwlock_unlock (ruv->lock);
		return_value = RUV_SUCCESS;
	}
	return return_value;
}

/*
 * XXXggood do we need "ruv_covers_ruv_strict" ???? */
PRBool
ruv_covers_ruv(const RUV *covering_ruv, const RUV *covered_ruv)
{
	PRBool return_value = PR_TRUE;
	RUVElement *replica;
	int cookie;

    /* compare replica generations first */
    if (covering_ruv->replGen == NULL || covered_ruv->replGen == NULL)
        return PR_FALSE;
    
    if (strcasecmp (covered_ruv->replGen, covering_ruv->replGen))
        return PR_FALSE;

    /* replica generation is the same, now compare element by element */
	for (replica = dl_get_first (covered_ruv->elements, &cookie);
		NULL != replica;
		replica = dl_get_next (covered_ruv->elements, &cookie))
	{
		if (replica->csn &&
			(ruv_covers_csn(covering_ruv, replica->csn) == PR_FALSE))
		{
			return_value = PR_FALSE;
			/* Don't break here - may leave something referenced? */
		}
	}
	return return_value;
}

/*
 * This compares two ruvs to see if they are compatible.  This is
 * used, for example, when the data is reloaded, to see if the ruv
 * from the database is compatible with the ruv from the changelog.
 * If the replica generation is empty or does not match, the data
 * is not compatible.
 * If the maxcsns are not compatible, the ruvs are not compatible.
 * However, if the first ruv has replica IDs that the second RUV
 * does not have, and this is the only difference, the application
 * may allow that with a warning.
 */
int
ruv_compare_ruv(const RUV *ruv1, const char *ruv1name, const RUV *ruv2, const char *ruv2name, int strict, int loglevel)
{
    int rc = 0;
    int ii = 0;
    const RUV *ruvalist[] = {ruv1, ruv2};
    const RUV *ruvblist[] = {ruv2, ruv1};
    int missinglist[2] = {0, 0};
    const char *ruvanames[] = {ruv1name, ruv2name};
    const char *ruvbnames[] = {ruv2name, ruv1name};
    const int nitems = 2;

    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
	ruv_dump(ruv1, (char *)ruv1name, NULL);
	ruv_dump(ruv2, (char *)ruv2name, NULL);
    }

    /* compare replica generations first */
    if (ruv1->replGen == NULL || ruv2->replGen == NULL) {
        slapi_log_error(loglevel, repl_plugin_name,
                        "ruv_compare_ruv: RUV [%s] is missing the replica generation\n",
                        ruv1->replGen ? ruv2name : ruv1name);
        return RUV_COMP_NO_GENERATION;
    }
    
    if (strcasecmp (ruv1->replGen, ruv2->replGen)) {
        slapi_log_error(loglevel, repl_plugin_name,
                        "ruv_compare_ruv: RUV [%s] replica generation [%s] does not match RUV [%s] [%s]\n",
                        ruv1name, ruv1->replGen, ruv2name, ruv2->replGen);
        return RUV_COMP_GENERATION_DIFFERS;
    }

    /* replica generation is the same, now compare element by element */
    for (ii = 0; ii < nitems; ++ii) {
        int cookie;
        const RUV *ruva = ruvalist[ii];
        const RUV *ruvb = ruvblist[ii];
        int *missing = &missinglist[ii];
        const char *ruvaname = ruvanames[ii];
        const char *ruvbname = ruvbnames[ii];
        RUVElement *replicab;

        for (replicab = dl_get_first (ruvb->elements, &cookie);
             NULL != replicab;
             replicab = dl_get_next (ruvb->elements, &cookie)) {
            if (replicab->csn) {
                ReplicaId rid = csn_get_replicaid(replicab->csn);
                RUVElement *replicaa = ruvGetReplica(ruva, rid);
                char csnstra[CSN_STRSIZE];
                char csnstrb[CSN_STRSIZE];
                char ruvelem[1024];
                ruv_element_to_string(replicab, NULL, ruvelem, sizeof(ruvelem));
                csn_as_string(replicab->csn, PR_FALSE, csnstrb);
                if (replicaa == NULL) {
                    (*missing)++;
                    slapi_log_error(loglevel, repl_plugin_name,
                                    "ruv_compare_ruv: RUV [%s] does not contain element [%s] "
                                    "which is present in RUV [%s]\n",
                                    ruvaname, ruvelem, ruvbname);
                } else if (strict && (csn_compare (replicab->csn, replicaa->csn) >= 0)) {
                    csn_as_string(replicaa->csn, PR_FALSE, csnstra);
                    slapi_log_error(loglevel, repl_plugin_name,
                                    "ruv_compare_ruv: the max CSN [%s] from RUV [%s] is larger "
                                    "than or equal to the max CSN [%s] from RUV [%s] for element [%s]\n",
                                    csnstrb, ruvbname, csnstra, ruvaname, ruvelem);
                    rc = RUV_COMP_CSN_DIFFERS;
                } else if (csn_compare (replicab->csn, replicaa->csn) > 0) {
                    csn_as_string(replicaa->csn, PR_FALSE, csnstra);
                    slapi_log_error(loglevel, repl_plugin_name,
                                    "ruv_compare_ruv: the max CSN [%s] from RUV [%s] is larger "
                                    "than the max CSN [%s] from RUV [%s] for element [%s]\n",
                                    csnstrb, ruvbname, csnstra, ruvaname, ruvelem);
                    rc = RUV_COMP_CSN_DIFFERS;
                } else {
                    csn_as_string(replicaa->csn, PR_FALSE, csnstra);
                    slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
                                    "ruv_compare_ruv: the max CSN [%s] from RUV [%s] is less than "
                                    "or equal to the max CSN [%s] from RUV [%s] for element [%s]\n",
                                    csnstrb, ruvbname, csnstra, ruvaname, ruvelem);
                }
            } else {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
                                "ruv_compare_ruv: RUV [%s] has an empty CSN\n",
                                ruvbname);
            }
        }
    }
    if (!rc) {
        if (missinglist[0]) {
            rc = RUV_COMP_RUV1_MISSING;
        } else if (missinglist[1]) {
            rc = RUV_COMP_RUV2_MISSING;
        }
    }
    return rc;
}

PRInt32 
ruv_replica_count (const RUV *ruv)
{
    if (ruv == NULL)
        return 0;
    else
    {
        int count;

        slapi_rwlock_rdlock (ruv->lock);
        count = dl_get_count (ruv->elements);
        slapi_rwlock_unlock (ruv->lock);
        
        return count;
    }
}

/*
 * Extract all the referral URL's from the RUV (but self URL),
 * returning them in an array of strings, that
 * the caller must free.
 */
char **
ruv_get_referrals(const RUV *ruv)
{
	char **r= NULL;
    int n;
	const char *mypurl = multimaster_get_local_purl();
	
    slapi_rwlock_rdlock (ruv->lock);

	n = ruv_replica_count(ruv);
	if(n>0)
	{
		RUVElement *replica;
		int cookie;
		int i= 0;
		r= (char**)slapi_ch_calloc(sizeof(char*),n+1);
		for (replica = dl_get_first (ruv->elements, &cookie); replica;
			 replica = dl_get_next (ruv->elements, &cookie))
		{
			/* Add URL into referrals if doesn't match self URL */
			if((replica->replica_purl!=NULL) &&
			   (slapi_utf8casecmp((unsigned char *)replica->replica_purl,
								  (unsigned char *)mypurl) != 0))
			{
		 		r[i]= slapi_ch_strdup(replica->replica_purl);
				i++;
			}
		}
	}

    slapi_rwlock_unlock (ruv->lock);

	return r; /* Caller must free this */
}

void 
ruv_dump(const RUV *ruv, char *ruv_name, PRFileDesc *prFile)
{
    RUVElement *replica;
	int cookie;
	char csnstr1[CSN_STRSIZE];
	char csnstr2[CSN_STRSIZE];
	char buff[RUVSTR_SIZE];
	int len = sizeof (buff);

	PR_ASSERT(NULL != ruv);
    if (!slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        return;
    }

    slapi_rwlock_rdlock (ruv->lock);

	PR_snprintf (buff, len, "%s: {replicageneration} %s\n",
				ruv_name ? ruv_name : type_ruvElement,
				ruv->replGen == NULL ? "" : ruv->replGen);

	if (prFile)
	{
		slapi_write_buffer (prFile, buff, strlen(buff));
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "%s", buff);
	}
	for (replica = dl_get_first (ruv->elements, &cookie); replica;
		 replica = dl_get_next (ruv->elements, &cookie))
	{
		/* prefix_ruvcsn = "{replica " */
		PR_snprintf (buff, len, "%s: %s%d%s%s}%s%s%s%s\n",
					ruv_name ? ruv_name : type_ruvElement,
					prefix_ruvcsn, replica->rid,
					replica->replica_purl == NULL ? "" : " ",
					replica->replica_purl == NULL ? "" : replica->replica_purl,
					replica->min_csn == NULL ? "" : " ",
					csn_as_string(replica->min_csn, PR_FALSE, csnstr1),
					replica->csn == NULL ? "" : " ",
					csn_as_string(replica->csn, PR_FALSE, csnstr2));
		if (strlen (csnstr1) > 0) {
			PR_snprintf (buff + strlen(buff) - 1, len - strlen(buff), " %08lx\n",
					replica->last_modified);
		}
		if (prFile)
		{
			slapi_write_buffer (prFile, buff, strlen(buff));
		}
		else
		{
			slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "%s", buff);
		}
	}

    slapi_rwlock_unlock (ruv->lock);
}

/* this function notifies the ruv that there are operations in progress so that
   they can be added to the pending list for the appropriate client. */
int ruv_add_csn_inprogress (RUV *ruv, const CSN *csn)
{
    RUVElement* replica;
    char csn_str[CSN_STRSIZE];
    int rc = RUV_SUCCESS;
    int rid = csn_get_replicaid (csn);

    PR_ASSERT (ruv && csn);

    /* locate ruvElement */
    slapi_rwlock_wrlock (ruv->lock);

    if(is_cleaned_rid(rid)){
        /* return success because we want to consume the update, but not perform it */
        rc = RUV_COVERS_CSN;
        goto done;
    }
    replica = ruvGetReplica (ruv, rid);
    if (replica == NULL)
    {
        replica = ruvAddReplicaNoCSN (ruv, rid, NULL/*purl*/);
        if (replica == NULL)
        {
            if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
                slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_add_csn_inprogress: failed to add replica"
                    " that created csn %s\n", csn_as_string (csn, PR_FALSE, csn_str));
            }
            rc = RUV_MEMORY_ERROR;
            goto done;
        }
    } 

    /* check first that this csn is not already covered by this RUV */
    if (ruv_covers_csn_internal(ruv, csn, PR_FALSE))
    {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_add_csn_inprogress: "
                            "the csn %s has already be seen - ignoring\n",
                            csn_as_string (csn, PR_FALSE, csn_str));
        }
        rc = RUV_COVERS_CSN;
        goto done;
    }

    rc = csnplInsert (replica->csnpl, csn);
    if (rc == 1)    /* we already seen this csn */
    {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_add_csn_inprogress: "
                            "the csn %s has already be seen - ignoring\n",
                            csn_as_string (csn, PR_FALSE, csn_str));
        }
        rc = RUV_COVERS_CSN;    
    }
    else if(rc != 0)
    {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_add_csn_inprogress: failed to insert csn %s"
                            " into pending list\n", csn_as_string (csn, PR_FALSE, csn_str));
        }
        rc = RUV_UNKNOWN_ERROR;
    }
    else
    {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_add_csn_inprogress: successfully inserted csn %s"
                            " into pending list\n", csn_as_string (csn, PR_FALSE, csn_str));
        }
        rc = RUV_SUCCESS;
    }
      
done:
    slapi_rwlock_unlock (ruv->lock);
    return rc;
}

int ruv_cancel_csn_inprogress (RUV *ruv, const CSN *csn)
{
    RUVElement* replica;
    int rc = RUV_SUCCESS;

    PR_ASSERT (ruv && csn);

    /* locate ruvElement */
    slapi_rwlock_wrlock (ruv->lock);
    replica = ruvGetReplica (ruv, csn_get_replicaid (csn));
    if (replica == NULL)
    {
        /* ONREPL - log error */
        rc = RUV_NOTFOUND;
        goto done;
    } 

    rc = csnplRemove (replica->csnpl, csn);
    if (rc != 0)
        rc = RUV_NOTFOUND;
    else
        rc = RUV_SUCCESS;
      
done:
    slapi_rwlock_unlock (ruv->lock);
    return rc;
}

int ruv_update_ruv (RUV *ruv, const CSN *csn, const char *replica_purl, PRBool isLocal)
{
    int rc=RUV_SUCCESS;
    char csn_str[CSN_STRSIZE];
    CSN *max_csn;
    CSN *first_csn = NULL;
    RUVElement *replica;
    
    PR_ASSERT (ruv && csn);

    slapi_rwlock_wrlock (ruv->lock);

    replica = ruvGetReplica (ruv, csn_get_replicaid (csn));
    if (replica == NULL)
    {
        /* we should have a ruv element at this point because it would have
           been added by ruv_add_inprogress function */
        slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_update_ruv: "
			            "can't locate RUV element for replica %d\n", csn_get_replicaid (csn)); 
        goto done;
    } 

	if (csnplCommit(replica->csnpl, csn) != 0)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "ruv_update_ruv: cannot commit csn %s\n",
			            csn_as_string(csn, PR_FALSE, csn_str));
        rc = RUV_CSNPL_ERROR;
        goto done;
	}
    else
    {
        if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
            slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_update_ruv: "
                            "successfully committed csn %s\n", csn_as_string(csn, PR_FALSE, csn_str));
        }
    }

	if ((max_csn = csnplRollUp(replica->csnpl, &first_csn)) != NULL)
	{
#ifdef DEBUG
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, "ruv_update_ruv: rolled up to csn %s\n",
			            csn_as_string(max_csn, PR_FALSE, csn_str)); /* XXXggood remove debugging */
#endif
        /* replica object sets min csn for local replica */
		if (!isLocal && replica->min_csn == NULL) {
		  /* bug 559223 - it seems that, under huge stress, a server might pass
		   * through this code when more than 1 change has already been sent and commited into
		   * the pending lists... Therefore, as we are trying to set the min_csn ever 
		   * generated by this replica, we need to set the first_csn as the min csn in the
		   * ruv */
		  set_min_csn_nolock(ruv, first_csn, replica_purl);
		} 
		set_max_csn_nolock(ruv, max_csn, replica_purl);
		/* It is possible that first_csn points to max_csn.
		   We need to free it once */
		if (max_csn != first_csn) {
			csn_free(&first_csn); 
		}
		csn_free(&max_csn);
	}

done:
    slapi_rwlock_unlock (ruv->lock);

    return rc;
}

/* Helper functions */

static int
ruvInit (RUV **ruv, int initCount)
{
	PR_ASSERT (ruv);

	if (ruv == NULL) {
		return RUV_NSPR_ERROR;
	}

	/* allocate new RUV */
	*ruv = (RUV *)slapi_ch_calloc (1, sizeof (RUV));

	/* allocate	elements */
	(*ruv)->elements = dl_new (); /* never returns NULL */

	dl_init ((*ruv)->elements, initCount);

	/* create lock */
	(*ruv)->lock = slapi_new_rwlock();
	if ((*ruv)->lock == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
						"ruvInit: failed to create lock\n");
		dl_free(&(*ruv)->elements);
		slapi_ch_free((void**)ruv);
		return RUV_NSPR_ERROR;
	}

	return RUV_SUCCESS;
}

static void
ruvFreeReplica (void **data)
{
	RUVElement *element = *(RUVElement**)data;

	if (NULL != element)
	{
		if (NULL != element->csn)
		{
			csn_free (&element->csn);
		}
		if (NULL != element->min_csn)
		{
			csn_free (&element->min_csn);
		}
		/* slapi_ch_free accepts NULL pointer */
		slapi_ch_free((void **)&element->replica_purl);

        if (element->csnpl)
		{
			csnplFree (&(element->csnpl));
		}
		slapi_ch_free ((void **)&element);
	}
}

static RUVElement*
ruvAddReplica (RUV *ruv, const CSN *csn, const char *replica_purl)
{
	RUVElement *replica;

	PR_ASSERT (NULL != ruv);
    PR_ASSERT (NULL != csn);
	
	replica = (RUVElement *)slapi_ch_calloc (1, sizeof (RUVElement));
	if (replica == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
						"ruvAddReplica: memory allocation failed\n");
		return NULL;
	}
    
	replica->rid = csn_get_replicaid (csn);
/* 	PR_ASSERT(replica->rid != READ_ONLY_REPLICA_ID); */
	
	replica->csn = csn_dup (csn);
	replica->last_modified = current_time();
	replica->min_csn = csn_dup (csn);

	replica->replica_purl = slapi_ch_strdup(replica_purl);
    replica->csnpl = csnplNew ();
    
	dl_add (ruv->elements, replica);

	return replica;
}

static RUVElement* 
ruvAddReplicaNoCSN (RUV *ruv, ReplicaId rid, const char *replica_purl)
{
    RUVElement *replica;

	PR_ASSERT (NULL != ruv);
/* 	PR_ASSERT(rid != READ_ONLY_REPLICA_ID); */
	
	replica = (RUVElement *)slapi_ch_calloc (1, sizeof (RUVElement));
	if (replica == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
						"ruvAddReplicaNoCSN: memory allocation failed\n");
		return NULL;
	}

	replica->rid = rid;
	replica->replica_purl = slapi_ch_strdup(replica_purl);
    replica->csnpl = csnplNew ();
    
	dl_add (ruv->elements, replica);

	return replica;
}

static RUVElement* 
ruvAddIndexReplicaNoCSN (RUV *ruv, ReplicaId rid, const char *replica_purl, int index)
{
    RUVElement *replica;

	PR_ASSERT (NULL != ruv);
/* 	PR_ASSERT(rid != READ_ONLY_REPLICA_ID); */
	
	replica = (RUVElement *)slapi_ch_calloc (1, sizeof (RUVElement));
	if (replica == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
						"ruvAddIndexReplicaNoCSN: memory allocation failed\n");
		return NULL;
	}

	replica->rid = rid;
	replica->replica_purl = slapi_ch_strdup(replica_purl);
    replica->csnpl = csnplNew ();
    
	dl_add_index (ruv->elements, replica, index);

	return replica;
}

static RUVElement *
ruvGetReplica (const RUV *ruv, ReplicaId rid)
{
	PR_ASSERT (ruv /* && rid >= 0 -- rid can't be negative */);

	return (RUVElement *)dl_get (ruv->elements, (const void*)&rid, ruvReplicaCompare);
}

static int
ruvReplicaCompare (const void *el1, const void *el2)
{
	RUVElement *replica = (RUVElement*)el1;
	ReplicaId *rid1 = (ReplicaId*) el2;

	if (replica == NULL || rid1 == NULL)
		return -1;

	if (*rid1 == replica->rid)
		return 0;
	
	if (*rid1 < replica->rid)
		return -1;
	else
		return 1;
}



/*
 * Given a berval that points to a string of the form:
 * "{dbgen} generation-id", return a copy of the
 * "generation-id" part in a null-terminated string.
 * Returns NULL if the berval is malformed.
 */
static char *
get_replgen_from_berval(const struct berval *bval)
{
	char *ret_string = NULL;

	if (NULL != bval && NULL != bval->bv_val &&
		(bval->bv_len > strlen(prefix_replicageneration)) &&
		strncasecmp(bval->bv_val, prefix_replicageneration,
		strlen(prefix_replicageneration)) == 0)
	{
		unsigned int index = strlen(prefix_replicageneration);
		/* Skip any whitespace */
		while (index++ < bval->bv_len && bval->bv_val[index] == ' ');
		if (index < bval->bv_len)
		{
			unsigned int ret_len = bval->bv_len - index;
			ret_string = slapi_ch_malloc(ret_len + 1);
			memcpy(ret_string, &bval->bv_val[index], ret_len);
			ret_string[ret_len] = '\0';
		}
	}
	return ret_string;
}



/*
 * Given a berval that points to a string of the form:
 * "{replica ldap[s]//host:port} <min_csn> <csn>", parse out the
 * partial URL and the CSNs into an RUVElement, and return
 * a pointer to the copy. Returns NULL if the berval is
 * malformed.
 */
static RUVElement *
get_ruvelement_from_berval(const struct berval *bval)
{
	RUVElement *ret_ruve = NULL;
	char *purl = NULL;
	ReplicaId rid = 0;
	char ridbuff [RIDSTR_SIZE];
	int i;

	if (NULL == bval || NULL == bval->bv_val ||
		bval->bv_len <= strlen(prefix_ruvcsn) ||
		strncasecmp(bval->bv_val, prefix_ruvcsn, strlen(prefix_ruvcsn)) != 0)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
				"get_ruvelement_from_berval: invalid berval\n");
		goto loser;
	} 

	{
		unsigned int urlbegin = strlen(prefix_ruvcsn);
		unsigned int urlend;
		unsigned int mincsnbegin;

		/* replica id must be here */
		i = 0;
		while (isdigit (bval->bv_val[urlbegin]))
		{
			ridbuff [i] = bval->bv_val[urlbegin];
			i++;
			urlbegin ++;
		}

		if (i == 0)	/* replicaid is missing */
			goto loser;

		ridbuff[i] = '\0';
		rid = atoi (ridbuff);

		if (bval->bv_val[urlbegin] == '}')
		{
			/* No purl in this value */
			purl = NULL;
			mincsnbegin = urlbegin + 1;
		}
		else
		{
			while (urlbegin++ < bval->bv_len && bval->bv_val[urlbegin] == ' ');
			urlend = urlbegin;
			while (urlend++ < bval->bv_len && bval->bv_val[urlend] != '}');
			purl = slapi_ch_malloc(urlend - urlbegin + 1);
			memcpy(purl, &bval->bv_val[urlbegin], urlend - urlbegin);
			purl[urlend - urlbegin] = '\0';
			mincsnbegin = urlend;
		}
		/* Skip any whitespace before the first (minimum) CSN */
		while (mincsnbegin++ < (bval->bv_len-1) && bval->bv_val[mincsnbegin] == ' ');
		/* Now, mincsnbegin should contain the index of the beginning of the first csn */
		if (mincsnbegin >= bval->bv_len)
		{
			/* Missing the entire content*/
            if (purl == NULL)
			    goto loser;
            else    /* we have just purl - no changes from the replica has been seen */
            {
                ret_ruve = (RUVElement *)slapi_ch_calloc(1, sizeof(RUVElement));
				ret_ruve->rid = rid;
                ret_ruve->replica_purl = purl;
            }
		}
		else
		{
			if ((bval->bv_len - mincsnbegin != (_CSN_VALIDCSN_STRLEN * 2) + 1)
			 && (bval->bv_len - mincsnbegin != (_CSN_VALIDCSN_STRLEN * 2) + 10))
			{
				/* Malformed - incorrect length for 2 CSNs + space AND
				 *             incorrect length for 2 CSNs + 2 spaces +
				 *                              last_modified (see ruv_dump) */
				goto loser;
			}
			else
			{
				char mincsnstr[CSN_STRSIZE];
				char maxcsnstr[CSN_STRSIZE];

				memset(mincsnstr, '\0', CSN_STRSIZE);
				memset(maxcsnstr, '\0', CSN_STRSIZE);
				memcpy(mincsnstr, &bval->bv_val[mincsnbegin], _CSN_VALIDCSN_STRLEN);
				memcpy(maxcsnstr, &bval->bv_val[mincsnbegin + _CSN_VALIDCSN_STRLEN + 1], _CSN_VALIDCSN_STRLEN);
				ret_ruve = (RUVElement *)slapi_ch_calloc(1, sizeof(RUVElement));
				ret_ruve->min_csn = csn_new_by_string(mincsnstr);
				ret_ruve->csn = csn_new_by_string(maxcsnstr);
				ret_ruve->rid = rid;
				ret_ruve->replica_purl = purl;
				if (NULL == ret_ruve->min_csn || NULL == ret_ruve->csn)
				{
					goto loser;
				}
			}
		}
	}

    /* initialize csn pending list */
    ret_ruve->csnpl = csnplNew ();
    if (ret_ruve->csnpl == NULL)
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
						"get_ruvelement_from_berval: failed to create csn pending list\n");
		goto loser;
	} 

	return ret_ruve;

loser:
	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free((void **)&purl);
	if (NULL != ret_ruve)
	{
		if (NULL != ret_ruve->min_csn)
		{
			csn_free(&ret_ruve->min_csn);
		}
		if (NULL != ret_ruve->csn)
		{
			csn_free(&ret_ruve->csn);
		}
		slapi_ch_free((void **)&ret_ruve);
	}
	return NULL;
}

int
ruv_move_local_supplier_to_first(RUV *ruv, ReplicaId aRid)
{
	RUVElement * elem = NULL;
	int rc = RUV_NOTFOUND;
	
	PR_ASSERT(ruv);

	slapi_rwlock_wrlock (ruv->lock);

	elem = (RUVElement *)dl_delete(ruv->elements,(const void*)&aRid, ruvReplicaCompare, 0);
	if (elem) {
		dl_add_index(ruv->elements, elem, 1);
		rc = RUV_SUCCESS;
	}

	slapi_rwlock_unlock (ruv->lock);
	
	return rc;
}


int
ruv_get_first_id_and_purl(RUV *ruv, ReplicaId *rid, char **replica_purl )
{
	RUVElement * first = NULL;
	int cookie;
	int rc;
	
	PR_ASSERT(ruv);

    slapi_rwlock_rdlock (ruv->lock);
	first = dl_get_first(ruv->elements, &cookie);
	if ( first == NULL )
	{
		rc = RUV_MEMORY_ERROR;
	}
	else
	{
		*rid = first->rid;
		*replica_purl = first->replica_purl;
		rc = RUV_SUCCESS;
	}
	slapi_rwlock_unlock (ruv->lock);
	return rc;
}

int ruv_local_contains_supplier(RUV *ruv, ReplicaId rid)
{
	int cookie;
	RUVElement *elem = NULL;

	PR_ASSERT(ruv);

	slapi_rwlock_rdlock (ruv->lock);
	for (elem = dl_get_first (ruv->elements, &cookie);
		 elem;
		 elem = dl_get_next (ruv->elements, &cookie))
	{
		if (elem->rid == rid){
			slapi_rwlock_unlock (ruv->lock);
			return 1;
		}
	}
	slapi_rwlock_unlock (ruv->lock);
	return 0;
}

PRBool ruv_has_csns(const RUV *ruv)
{
        PRBool retval = PR_TRUE;
        CSN *mincsn = NULL;
        CSN *maxcsn = NULL;

        ruv_get_min_csn(ruv, &mincsn);
        ruv_get_max_csn(ruv, &maxcsn);
        if (mincsn) {
                csn_free(&mincsn);
                csn_free(&maxcsn);
        } else if (maxcsn) {
                csn_free(&maxcsn);
        } else {
                retval = PR_FALSE; /* both min and max are false */
        }

        return retval;
}

PRBool ruv_has_both_csns(const RUV *ruv)
{
        PRBool retval = PR_TRUE;
        CSN *mincsn = NULL;
        CSN *maxcsn = NULL;

        ruv_get_min_csn(ruv, &mincsn);
        ruv_get_max_csn(ruv, &maxcsn);
        if (mincsn) {
                csn_free(&mincsn);
                csn_free(&maxcsn);
        } else if (maxcsn) {
                csn_free(&maxcsn);
                retval = PR_FALSE; /* it has a maxcsn but no mincsn */
        } else {
                retval = PR_FALSE; /* both min and max are false */
        }

        return retval;
}

/* Check if the first ruv is newer than the second one */
PRBool
ruv_is_newer (Object *sruvobj, Object *cruvobj)
{
	RUV *sruv, *cruv;
	RUVElement *sreplica, *creplica;
	int scookie, ccookie;
	int is_newer = PR_FALSE;

	if ( sruvobj == NULL ) {
		return 0;
	}
	if ( cruvobj == NULL ) {
		return 1;
	}
	sruv = (RUV *) object_get_data ( sruvobj );
	cruv = (RUV *) object_get_data ( cruvobj );

	for (sreplica = dl_get_first (sruv->elements, &scookie); sreplica;
		 sreplica = dl_get_next (sruv->elements, &scookie))
	{
		/* A hub may have a dummy ruv with rid 65535 */
		if ( sreplica->csn == NULL ) continue;

		if ( cruv->elements == NULL )
			{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
			"ruv_is_newer, consumer RUV has no elements\n");
			 is_newer=PR_FALSE;
			 break;
			}

		for (creplica = dl_get_first (cruv->elements, &ccookie); creplica;
			 creplica = dl_get_next (cruv->elements, &ccookie))
		{
			if ( sreplica->rid == creplica->rid ) {
				if ( csn_compare ( sreplica->csn, creplica->csn ) > 0 ) {
					is_newer = PR_TRUE;
				}
				break;
			}
		}
		if ( creplica == NULL || is_newer ) {
			is_newer = PR_TRUE;
			break;
		}
	}

	return is_newer;
}

/*
 * This routine is called after a disordely shutdown
 * The Database RUV was found late compare to the changelog RUV
 */
void
ruv_force_csn_update_from_ruv(RUV *src_ruv, RUV *tgt_ruv, char *msg, int logLevel) {
    RUVElement *replica = NULL;
    int cookie;

    slapi_rwlock_rdlock(src_ruv->lock);
    
    for (replica = dl_get_first(src_ruv->elements, &cookie);
            NULL != replica;
            replica = dl_get_next(src_ruv->elements, &cookie)) {
        /* 
         * In case the DB RUV (tgt_ruv) is behind the CL RUV (src_ruv)
         * updates the DB RUV.
         */
        if (!ruv_covers_csn(tgt_ruv, replica->csn)) {
            char csnStr[CSN_STRSIZE];

            ruv_force_csn_update(tgt_ruv, replica->csn);
            csn_as_string(replica->csn, PR_FALSE, csnStr);
            slapi_log_error(logLevel, repl_plugin_name, "%s %s\n",
                    msg, csnStr);
        }
    }

    slapi_rwlock_unlock(src_ruv->lock);
    
}

void
ruv_force_csn_update (RUV *ruv, CSN *csn)
{
	CSN *max = NULL;
	
	if (ruv != NULL)
	{
		ruv_get_max_csn(ruv, &max);
		if (csn_compare(max, csn))
		{
			ruv_set_max_csn(ruv, csn, NULL);
		}
		csn_free(&max);
	}	
}

/* This routine is used to iterate the elements in a RUV and set each vector's
+ * minimal CSN to a dummy with just the rid set, e.g. 00000000000000010000 */
void
ruv_insert_dummy_min_csn (RUV *ruv)
{
	RUVElement *r;
	int cookie;

	for (r = dl_get_first (ruv->elements, &cookie); r;
		r = dl_get_next (ruv->elements, &cookie)) {
		if (r->csn && !r->min_csn) {
			CSN *dummycsn = csn_new();
			csn_init(dummycsn);
			csn_set_replicaid(dummycsn, csn_get_replicaid(r->csn));
			r->min_csn = dummycsn;
		}
	}
}


#ifdef TESTING /* Some unit tests for code in this file */

static void
ruv_dump_internal(RUV *ruv)
{
	RUVElement *replica;
	int cookie;
	char csnstr1[CSN_STRSIZE];
	char csnstr2[CSN_STRSIZE];

	PR_ASSERT(NULL != ruv);
	printf("{replicageneration} %s\n", ruv->replGen == NULL ? "NULL" : ruv->replGen);
	for (replica = dl_get_first (ruv->elements, &cookie); replica;
		 replica = dl_get_next (ruv->elements, &cookie))
	{
		printf("{replica%s%s} %s %s\n",
			replica->replica_purl == NULL ? "" : " ",
			replica->replica_purl == NULL ? "" : replica->replica_purl,
			csn_as_string(replica->min_csn, PR_FALSE, csnstr1),
			csn_as_string(replica->csn, PR_FALSE, csnstr2));
	}
}
	
void
ruv_test()
{
	const struct berval *vals[5];
	struct berval val0, val1, val2, val3;
	RUV *ruv;
	Slapi_Attr *attr;
	Slapi_Value *sv0, *sv1, *sv2, *sv3;
	int rc;
	char csnstr[CSN_STRSIZE];
	char *gen;
	CSN *newcsn;
	ReplicaId *ids;
	int nids;
	Slapi_Mod smods;
	PRBool covers;

	vals[0] = &val0;
	vals[1] = &val1;
	vals[2] = &val2;
	vals[3] = &val3;
	vals[4] = NULL;

	val0.bv_val = "{replicageneration} 0440FDC0A33F";
	val0.bv_len = strlen(val0.bv_val);

	val1.bv_val = "{replica ldap://ggood.mcom.com:389} 12345670000000FE0000 12345671000000FE0000";
	val1.bv_len = strlen(val1.bv_val);

	val2.bv_val = "{replica ldaps://an-impossibly-long-host-name-that-drags-on-forever-and-forever.mcom.com:389} 11112110000000FF0000 11112111000000FF0000";
	val2.bv_len = strlen(val2.bv_val);

	val3.bv_val = "{replica} 12345672000000FD0000 12345673000000FD0000";
	val3.bv_len = strlen(val3.bv_val);

	rc = ruv_init_from_bervals(vals, &ruv);
	ruv_dump_internal(ruv);

	attr = slapi_attr_new();
	attr = slapi_attr_init(attr, "ruvelement");
	sv0 = slapi_value_new();
	sv1 = slapi_value_new();
	sv2 = slapi_value_new();
	sv3 = slapi_value_new();
	slapi_value_init_berval(sv0, &val0);
	slapi_value_init_berval(sv1, &val1);
	slapi_value_init_berval(sv2, &val2);
	slapi_value_init_berval(sv3, &val3);
	slapi_attr_add_value(attr, sv0);
	slapi_attr_add_value(attr, sv1);
	slapi_attr_add_value(attr, sv2);
	slapi_attr_add_value(attr, sv3);
	rc = ruv_init_from_slapi_attr(attr, &ruv);
	ruv_dump_internal(ruv);
	
	rc = ruv_delete_replica(ruv, 0xFF);
	/* Should delete one replica */
	ruv_dump_internal(ruv);

	rc = ruv_delete_replica(ruv, 0xAA);
	/* No such replica - should not do anything */
	ruv_dump_internal(ruv);

	rc = ruv_get_largest_csn_for_replica(ruv, 0xFE, &newcsn);
	if (NULL != newcsn)
	{
		csn_as_string(newcsn, PR_FALSE, csnstr);
		printf("Replica 0x%X has largest csn \"%s\"\n", 0xFE, csnstr);
	}
	else
	{
		printf("BAD - can't get largest CSN for replica 0x%X\n", 0xFE);
	}

	rc = ruv_get_smallest_csn_for_replica(ruv, 0xFE, &newcsn);
	if (NULL != newcsn)
	{
		csn_as_string(newcsn, PR_FALSE, csnstr);
		printf("Replica 0x%X has smallest csn \"%s\"\n", 0xFE, csnstr);
	}
	else
	{
		printf("BAD - can't get smallest CSN for replica 0x%X\n", 0xFE);
	}
	rc = ruv_get_largest_csn_for_replica(ruv, 0xAA, &newcsn);
	printf("ruv_get_largest_csn_for_replica on non-existent replica ID returns %d\n", rc);

	rc = ruv_get_smallest_csn_for_replica(ruv, 0xAA, &newcsn);
	printf("ruv_get_smallest_csn_for_replica on non-existent replica ID returns %d\n", rc);

	newcsn = csn_new_by_string("12345674000000FE0000"); /* Old replica 0xFE */
	rc = ruv_set_csns(ruv, newcsn, "ldaps://foobar.mcom.com");
	/* Should update replica FE's CSN */
	ruv_dump_internal(ruv);

	newcsn = csn_new_by_string("12345675000000FB0000"); /* New replica 0xFB */
	rc = ruv_set_csns(ruv, newcsn, "ldaps://foobar.mcom.com");
	/* Should get a new replica in the list with min == max csn */
	ruv_dump_internal(ruv);

	newcsn = csn_new_by_string("12345676000000FD0000"); /* Old replica 0xFD */
	rc = ruv_set_csns(ruv, newcsn, "ldaps://foobar.mcom.com");
	/* Should update replica 0xFD so new CSN is newer than min CSN */
	ruv_dump_internal(ruv);

	gen = ruv_get_replica_generation(ruv);
	printf("replica generation is \"%s\"\n", gen);

	newcsn = csn_new_by_string("12345673000000FE0000"); /* Old replica 0xFE */
	covers = ruv_covers_csn(ruv, newcsn); /* should say "true" */

	newcsn = csn_new_by_string("12345675000000FE0000"); /* Old replica 0xFE */
	covers = ruv_covers_csn(ruv, newcsn); /* Should say "false" */

	newcsn = csn_new_by_string("123456700000000A0000"); /* New replica 0A */
	rc = ruv_set_min_csn(ruv, newcsn, "ldap://repl0a.mcom.com");
	ruv_dump_internal(ruv);

	newcsn = csn_new_by_string("123456710000000A0000"); /* New replica 0A */
	rc = ruv_set_max_csn(ruv, newcsn, "ldap://repl0a.mcom.com");
	ruv_dump_internal(ruv);

	newcsn = csn_new_by_string("123456700000000B0000"); /* New replica 0B */
	rc = ruv_set_max_csn(ruv, newcsn, "ldap://repl0b.mcom.com");
	ruv_dump_internal(ruv);

	newcsn = csn_new_by_string("123456710000000B0000"); /* New replica 0B */
	rc = ruv_set_min_csn(ruv, newcsn, "ldap://repl0b.mcom.com");
	ruv_dump_internal(ruv);

	/* ONREPL test ruv enumeration */

	rc = ruv_to_smod(ruv, &smods);

	ruv_destroy(&ruv);
}
#endif /* TESTING */
