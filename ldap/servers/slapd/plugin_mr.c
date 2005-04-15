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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * plugin_mr.c - routines for calling matching rule plugins
 */

#include "slap.h"

static oid_item_t* global_mr_oids = NULL;
static PRLock* global_mr_oids_lock = NULL;

static void
init_global_mr_lock()
{
	if(global_mr_oids_lock==NULL)
	{
		global_mr_oids_lock = PR_NewLock();
	}
}

struct slapdplugin *
slapi_get_global_mr_plugins()
{
	return get_plugin_list(PLUGIN_LIST_MATCHINGRULE);
}

static struct slapdplugin*
plugin_mr_find (char* oid)
{
	oid_item_t* i;
	init_global_mr_lock();
	PR_Lock (global_mr_oids_lock);
	i = global_mr_oids;
	PR_Unlock (global_mr_oids_lock);
	for (; i != NULL; i = i->oi_next)
	{
	    if (!strcasecmp (oid, i->oi_oid))
	    {
			LDAPDebug (LDAP_DEBUG_FILTER, "plugin_mr_find(%s) != NULL\n", oid, 0, 0);
			return i->oi_plugin;
	    }
	}
    LDAPDebug (LDAP_DEBUG_FILTER, "plugin_mr_find(%s) == NULL\n", oid, 0, 0);
    return NULL;
}

static void
plugin_mr_bind (char* oid, struct slapdplugin* plugin)
{
	oid_item_t* i = (oid_item_t*) slapi_ch_malloc (sizeof (oid_item_t));
    LDAPDebug (LDAP_DEBUG_FILTER, "=> plugin_mr_bind(%s)\n", oid, 0, 0);
	init_global_mr_lock();
	i->oi_oid = slapi_ch_strdup (oid);
	i->oi_plugin = plugin;
	PR_Lock (global_mr_oids_lock);
	i->oi_next = global_mr_oids;
	global_mr_oids = i;
	PR_Unlock (global_mr_oids_lock);
	LDAPDebug (LDAP_DEBUG_FILTER, "<= plugin_mr_bind\n", 0, 0, 0);
}

int /* an LDAP error code, hopefully LDAP_SUCCESS */
slapi_mr_indexer_create (Slapi_PBlock* opb)
{
    int rc;
    char* oid;
    if (!(rc = slapi_pblock_get (opb, SLAPI_PLUGIN_MR_OID, &oid)))
    {
		IFP createFn = NULL;
		struct slapdplugin* mrp = plugin_mr_find (oid);
		if (mrp != NULL)
		{
		    if (!(rc = slapi_pblock_set (opb, SLAPI_PLUGIN, mrp)) &&
				!(rc = slapi_pblock_get (opb, SLAPI_PLUGIN_MR_INDEXER_CREATE_FN, &createFn)) &&
				createFn != NULL)
			{
				rc = createFn (opb);
		    }
		}
		else
		{
		    /* call each plugin, until one is able to handle this request. */
		    rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
		    for (mrp = get_plugin_list(PLUGIN_LIST_MATCHINGRULE); mrp != NULL; mrp = mrp->plg_next)
		    {
				IFP indexFn = NULL;
				Slapi_PBlock pb;
				memcpy (&pb, opb, sizeof(Slapi_PBlock));
				if (!(rc = slapi_pblock_set (&pb, SLAPI_PLUGIN, mrp)) &&
				    !(rc = slapi_pblock_get (&pb, SLAPI_PLUGIN_MR_INDEXER_CREATE_FN, &createFn)) &&
				    createFn != NULL &&
				    !(rc = createFn (&pb)) &&
				    !(rc = slapi_pblock_get (&pb, SLAPI_PLUGIN_MR_INDEX_FN, &indexFn)) &&
				    indexFn != NULL)
				{
				    /* Success: this plugin can handle it. */
				    memcpy (opb, &pb, sizeof(Slapi_PBlock));
				    plugin_mr_bind (oid, mrp); /* for future reference */
				    break;
				}
		    }
		}
    }
    return rc;
}

static int
attempt_mr_filter_create (mr_filter_t* f, struct slapdplugin* mrp, Slapi_PBlock* pb)
{
    int rc;
    IFP mrf_create = NULL;
    f->mrf_match = NULL;
    pblock_init (pb);
    if (!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN, mrp)) &&
		!(rc = slapi_pblock_get (pb, SLAPI_PLUGIN_MR_FILTER_CREATE_FN, &mrf_create)) &&
		mrf_create != NULL &&
		!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_OID, f->mrf_oid)) &&
		!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_TYPE, f->mrf_type)) &&
		!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_VALUE, &(f->mrf_value))) &&
		!(rc = mrf_create (pb)) &&
		!(rc = slapi_pblock_get (pb, SLAPI_PLUGIN_MR_FILTER_MATCH_FN, &(f->mrf_match)))) {
		if (f->mrf_match == NULL)
		{
		    rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
		}
    }
    return rc;
}

int /* an LDAP error code, hopefully LDAP_SUCCESS */
plugin_mr_filter_create (mr_filter_t* f)
{
    int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
    struct slapdplugin* mrp = plugin_mr_find (f->mrf_oid);
    Slapi_PBlock pb;

    if (mrp != NULL)
    {
		rc = attempt_mr_filter_create (f, mrp, &pb);
    }
    else
    {
		/* call each plugin, until one is able to handle this request. */
		for (mrp = get_plugin_list(PLUGIN_LIST_MATCHINGRULE); mrp != NULL; mrp = mrp->plg_next)
		{
		    if (!(rc = attempt_mr_filter_create (f, mrp, &pb)))
		    {
				plugin_mr_bind (f->mrf_oid, mrp); /* for future reference */
				break;
		    }
		}
    }
    if (!rc)
    {
		/* This plugin has created the desired filter. */
		f->mrf_plugin = mrp;
		slapi_pblock_get (&pb, SLAPI_PLUGIN_MR_FILTER_INDEX_FN, &(f->mrf_index));
		slapi_pblock_get (&pb, SLAPI_PLUGIN_MR_FILTER_REUSABLE, &(f->mrf_reusable));
		slapi_pblock_get (&pb, SLAPI_PLUGIN_MR_FILTER_RESET_FN, &(f->mrf_reset));
		slapi_pblock_get (&pb, SLAPI_PLUGIN_OBJECT, &(f->mrf_object));
		slapi_pblock_get (&pb, SLAPI_PLUGIN_DESTROY_FN, &(f->mrf_destroy));
    }
    return rc;
}

int /* an LDAP error code, hopefully LDAP_SUCCESS */
slapi_mr_filter_index (Slapi_Filter* f, Slapi_PBlock* pb)
{
    int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
    if (f->f_choice == LDAP_FILTER_EXTENDED && f->f_mr.mrf_index != NULL &&
		!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_OBJECT, f->f_mr.mrf_object)))
	{
		rc = f->f_mr.mrf_index (pb);
    }
    return rc;
}

