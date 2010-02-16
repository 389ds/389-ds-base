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
 * plugin_mr.c - routines for calling matching rule plugins
 */

#include "slap.h"

/* because extensible_candidates can call an indexing
   operation in the middle of a filtering operation,
   we have to use a structure that can be used by both
   since we can only have 1 SLAPI_PLUGIN_OBJECT in
   that case */
struct mr_private {
	Slapi_Value **sva; /* if using index_sv_fn */
	struct berval **bva; /* if using index_fn */
	/* if doing a purely indexing operation, the fields
	   below are not used */
	const struct slapdplugin *pi; /* our plugin */
	const char *oid; /* orig oid */
	const char *type; /* orig type from filter */
	const struct berval *value; /* orig value from filter */
	int ftype; /* filter type */
	int op; /* query op type */
	IFP match_fn; /* match func to use */
	/* note - substring matching rules not currently supported */
	char *initial; /* these are for substring matches */
	char *any[2]; /* at most one value for extensible filter */
	char *final; /* these are for substring matches */
	const struct berval *values[2]; /* for indexing */
};

static oid_item_t* global_mr_oids = NULL;
static PRLock* global_mr_oids_lock = NULL;

static int default_mr_indexer_create(Slapi_PBlock* pb);

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

struct slapdplugin *
plugin_mr_find( const char *nameoroid )
{
	struct slapdplugin	*pi = NULL;

	for ( pi = get_plugin_list(PLUGIN_LIST_MATCHINGRULE); (nameoroid != NULL) && (pi != NULL); pi = pi->plg_next ) {
		if ( charray_inlist( pi->plg_mr_names, (char *)nameoroid ) ) {
			break;
		}
	}

	if (!nameoroid) {
		pi = NULL;
	}

	if (nameoroid && !pi) {
		slapi_log_error(SLAPI_LOG_CONFIG, "plugin_mr_find",
						"Error: matching rule plugin for [%s] not found\n", nameoroid);
	}

	return ( pi );
}

static int
plugin_mr_get_type(struct slapdplugin *pi)
{
	int rc = LDAP_FILTER_EQUALITY;
	if (pi) {
		char **str = pi->plg_mr_names;
		for (; str && *str; ++str) {
			if (PL_strcasestr(*str, "substr")) {
				rc = LDAP_FILTER_SUBSTRINGS;
				break;
			}
			if (PL_strcasestr(*str, "approx")) {
				rc = LDAP_FILTER_APPROX;
				break;
			}
			if (PL_strcasestr(*str, "ordering")) {
				rc = LDAP_FILTER_GE;
				break;
			}
		}
	}

	return rc;
}

static struct slapdplugin*
plugin_mr_find_registered (char* oid)
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
			LDAPDebug (LDAP_DEBUG_FILTER, "plugin_mr_find_registered(%s) != NULL\n", oid, 0, 0);
			return i->oi_plugin;
	    }
	}
    LDAPDebug (LDAP_DEBUG_FILTER, "plugin_mr_find_registered(%s) == NULL\n", oid, 0, 0);
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
		struct slapdplugin* mrp = plugin_mr_find_registered (oid);
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
					((!(rc = slapi_pblock_get (&pb, SLAPI_PLUGIN_MR_INDEX_FN, &indexFn)) &&
					 indexFn != NULL) ||
					 (!(rc = slapi_pblock_get (&pb, SLAPI_PLUGIN_MR_INDEX_SV_FN, &indexFn)) &&
					  indexFn != NULL)))
				{
				    /* Success: this plugin can handle it. */
				    memcpy (opb, &pb, sizeof(Slapi_PBlock));
				    plugin_mr_bind (oid, mrp); /* for future reference */
					rc = 0; /* success */
				    break;
				}
		    }
			if (rc != 0) {
				/* look for a new syntax-style mr plugin */
				struct slapdplugin *pi = plugin_mr_find(oid);
				if (pi) {
					Slapi_PBlock pb;
					memcpy (&pb, opb, sizeof(Slapi_PBlock));
					slapi_pblock_set(&pb, SLAPI_PLUGIN, pi);
					rc = default_mr_indexer_create(&pb);
					if (!rc) {
						memcpy (opb, &pb, sizeof(Slapi_PBlock));
						plugin_mr_bind (oid, pi); /* for future reference */
					}
				}
			}
		}
    }
    return rc;
}

static struct mr_private *
mr_private_new(const struct slapdplugin *pi, const char *oid, const char *type, const struct berval *value, int ftype, int op)
{
	struct mr_private *mrpriv;

	mrpriv = (struct mr_private *)slapi_ch_calloc(1, sizeof(struct mr_private));
	mrpriv->pi = pi;
	mrpriv->oid = oid; /* should be consistent for lifetime of usage - no copy necessary */
	mrpriv->type = type; /* should be consistent for lifetime of usage - no copy necessary */
	mrpriv->value = value; /* should be consistent for lifetime of usage - no copy necessary */
	mrpriv->ftype = ftype;
	mrpriv->op = op;
	mrpriv->values[0] = mrpriv->value; /* for filter_index */

	return mrpriv;
}

static void
mr_private_indexer_done(struct mr_private *mrpriv)
{
	if (mrpriv && mrpriv->sva) {
		valuearray_free(&mrpriv->sva);
	}
	if (mrpriv && mrpriv->bva) {
		ber_bvecfree(mrpriv->bva);
		mrpriv->bva = NULL;
	}
}

static void
mr_private_done(struct mr_private *mrpriv)
{
	if (mrpriv) {
		mrpriv->pi = NULL;
		mrpriv->oid = NULL;
		mrpriv->type = NULL;
		mrpriv->value = NULL;
		mrpriv->ftype = 0;
		mrpriv->op = 0;
		slapi_ch_free_string(&mrpriv->initial);
		slapi_ch_free_string(&mrpriv->any[0]);
		slapi_ch_free_string(&mrpriv->final);
		mrpriv->match_fn = NULL;
		mrpriv->values[0] = NULL;
	}
	mr_private_indexer_done(mrpriv);
}

static void
mr_private_free(struct mr_private **mrpriv)
{
	if (mrpriv) {
		mr_private_done(*mrpriv);
		slapi_ch_free((void **)mrpriv);
	}
}

/* this function takes SLAPI_PLUGIN_MR_VALUES as Slapi_Value ** and
   returns SLAPI_PLUGIN_MR_KEYS as Slapi_Value **
*/
static int
mr_wrap_mr_index_sv_fn(Slapi_PBlock* pb)
{
	int rc = -1;
	Slapi_Value **in_vals = NULL;
	Slapi_Value **out_vals = NULL;
	struct slapdplugin *pi = NULL;
	
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_KEYS, out_vals); /* make sure output is cleared */
	slapi_pblock_get(pb, SLAPI_PLUGIN, &pi);
	if (!pi) {
		LDAPDebug0Args(LDAP_DEBUG_ANY, "mr_wrap_mr_index_sv_fn: error - no plugin specified\n");
	} else if (!pi->plg_mr_values2keys) {
		LDAPDebug0Args(LDAP_DEBUG_ANY, "mr_wrap_mr_index_sv_fn: error - plugin has no plg_mr_values2keys function\n");
	} else {
		struct mr_private *mrpriv = NULL;
		int ftype = plugin_mr_get_type(pi);
		slapi_pblock_get(pb, SLAPI_PLUGIN_MR_VALUES, &in_vals);
		(*pi->plg_mr_values2keys)(pb, in_vals, &out_vals, ftype);
		slapi_pblock_set(pb, SLAPI_PLUGIN_MR_KEYS, out_vals);
		/* we have to save out_vals to free next time or during destroy */
		slapi_pblock_get(pb, SLAPI_PLUGIN_OBJECT, &mrpriv);
		mr_private_indexer_done(mrpriv); /* free old vals, if any */
		mrpriv->sva = out_vals; /* save pointer for later */
		rc = 0;
	}
	return rc;
}

/* this function takes SLAPI_PLUGIN_MR_VALUES as struct berval ** and
   returns SLAPI_PLUGIN_MR_KEYS as struct berval **
*/
static int
mr_wrap_mr_index_fn(Slapi_PBlock* pb)
{
	int rc = -1;
	struct berval **in_vals = NULL;
	struct berval **out_vals = NULL;
	struct mr_private *mrpriv = NULL;
	Slapi_Value **in_vals_sv = NULL;
	Slapi_Value **out_vals_sv = NULL;

	slapi_pblock_get(pb, SLAPI_PLUGIN_MR_VALUES, &in_vals); /* get bervals */
	/* convert bervals to sv ary */
	valuearray_init_bervalarray(in_vals, &in_vals_sv);
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_VALUES, in_vals_sv); /* use sv */
	rc = mr_wrap_mr_index_sv_fn(pb);
	/* clean up in_vals_sv */
	valuearray_free(&in_vals_sv);
	/* restore old in_vals */
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_VALUES, in_vals);
	/* get result sv keys */
	slapi_pblock_get(pb, SLAPI_PLUGIN_MR_KEYS, &out_vals_sv);
	/* convert to bvec */
	valuearray_get_bervalarray(out_vals_sv, &out_vals);
	/* NOTE: mrpriv owns out_vals_sv (mpriv->sva) - will
	   get freed by mr_private_indexer_done() */
	/* we have to save out_vals to free next time or during destroy */
	slapi_pblock_get(pb, SLAPI_PLUGIN_OBJECT, &mrpriv);
	mr_private_indexer_done(mrpriv); /* free old vals, if any */
	mrpriv->bva = out_vals; /* save pointer for later */
	/* set return value berval array for caller */
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_KEYS, out_vals);

	return rc;
}

static int
default_mr_filter_destroy(Slapi_PBlock* pb)
{
	struct mr_private *mrpriv = NULL;

	slapi_pblock_get(pb, SLAPI_PLUGIN_OBJECT, &mrpriv);
	mr_private_free(&mrpriv);
	mrpriv = NULL;
	slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, mrpriv);

	return 0;
}

static int
default_mr_filter_match(void *obj, Slapi_Entry *e, Slapi_Attr *attr)
{
/* returns:  0  filter matched
 *	    -1  filter did not match
 *	    >0  an LDAP error code
 */
	int rc = -1;
	struct mr_private* mrpriv = (struct mr_private*)obj;
    for (; (rc == -1) && (attr != NULL); slapi_entry_next_attr(e, attr, &attr)) {
		char* type = NULL;
		if (!slapi_attr_get_type (attr, &type) && type != NULL &&
			!slapi_attr_type_cmp (mrpriv->type, type, 2/*match subtypes*/)) {
			Slapi_Value **vals = attr_get_present_values(attr);
#ifdef SUPPORT_MR_SUBSTRING_MATCHING
			if (mrpriv->ftype == LDAP_FILTER_SUBSTRINGS) {
				rc = (*mrpriv->match_fn)(pb, mrpriv->initial, mrpriv->any, mrpriv->final, vals);
			}
#endif
			rc = (*mrpriv->match_fn)(NULL, mrpriv->value, vals, mrpriv->ftype, NULL);
		}
    }

	return rc;
}

/* convert the filter value into an array of values for use
   in index key generation */
static int
default_mr_filter_index(Slapi_PBlock *pb)
{
	int rc = 0;
	struct mr_private* mrpriv = NULL;

	slapi_pblock_get(pb, SLAPI_PLUGIN_OBJECT, &mrpriv);

	slapi_pblock_set(pb, SLAPI_PLUGIN, (void *)mrpriv->pi);
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_TYPE, (void *)mrpriv->type);
	/* extensible_candidates uses struct berval ** indexer */
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_INDEX_FN, mr_wrap_mr_index_fn);
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_VALUES, mrpriv->values);
	/* the OID is magic - this is used to calculate the index prefix - it
	   is the indextype value passed to index_index2prefix - it must be the
	   same OID as used in the index configuration for the index matching
	   rule */
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_OID, (void *)mrpriv->oid);
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_QUERY_OPERATOR, &mrpriv->op);

	return rc;
}

static int
default_mr_filter_create(Slapi_PBlock *pb)
{
	int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION; /* failed to initialize */
	char* mrOID = NULL;
	char* mrTYPE = NULL;
	struct berval* mrVALUE = NULL;
	struct slapdplugin* pi = NULL;

	LDAPDebug0Args(LDAP_DEBUG_FILTER, "=> default_mr_filter_create\n");

	if (!slapi_pblock_get(pb, SLAPI_PLUGIN_MR_OID, &mrOID) && mrOID != NULL &&
		!slapi_pblock_get(pb, SLAPI_PLUGIN_MR_TYPE, &mrTYPE) && mrTYPE != NULL &&
		!slapi_pblock_get(pb, SLAPI_PLUGIN_MR_VALUE, &mrVALUE) && mrVALUE != NULL &&
		!slapi_pblock_get(pb, SLAPI_PLUGIN, &pi) && pi != NULL) {
		int op = SLAPI_OP_EQUAL;
		struct mr_private *mrpriv = NULL;
		int ftype = 0;

		LDAPDebug2Args(LDAP_DEBUG_FILTER, "=> default_mr_filter_create(oid %s; type %s)\n",
					   mrOID, mrTYPE);

		ftype = plugin_mr_get_type(pi);
		/* map the ftype to the op type */
		if (ftype == LDAP_FILTER_GE) {
			/*
			 * The rule evaluates to TRUE if and only if, in the code point
			 * collation order, the prepared attribute value character string
			 * appears earlier than the prepared assertion value character string;
			 * i.e., the attribute value is "less than" the assertion value.
			 */
			op = SLAPI_OP_LESS;
/*
		} else if (ftype == LDAP_FILTER_SUBSTRINGS) {
			op = SLAPI_OP_SUBSTRING;
*/
		} else if (ftype != LDAP_FILTER_EQUALITY) { /* unsupported */
			/* NOTE: we cannot currently support substring matching rules - the
			   reason is that the API provides no way to pass in the search time limit
			   required by the syntax filter substring match functions
			*/
			LDAPDebug1Arg(LDAP_DEBUG_FILTER, "<= default_mr_filter_create - unsupported filter type %d\n",
						  ftype);
			goto done;
		}
		slapi_pblock_set(pb, SLAPI_PLUGIN_MR_QUERY_OPERATOR, &op);
		mrpriv = mr_private_new(pi, mrOID, mrTYPE, mrVALUE, ftype, op);
#ifdef SUPPORT_MR_SUBSTRING_MATCHING
		if ((ftype == LDAP_FILTER_SUBSTRINGS) && (mrVALUE->bv_len > 0) && (mrVALUE->bv_val)) {
			char *first, *last;
			int have_initial = 0, have_final = 0;

			if ((mrVALUE->bv_len > 1) && (mrVALUE->bv_val[0] == '*')) {
				first = &mrVALUE->bv_val[1]; /* point at first "real" char */
				have_final = 1; /* substring final match */
			} else {
				first = mrVALUE->bv_val; /* point at beginning */
			}
			if ((mrVALUE->bv_len > 1) && (mrVALUE->bv_val[mrVALUE->bv_len-1] == '*')) {
				last = &mrVALUE->bv_val[mrVALUE->bv_len-2]; /* point at last "real" char */
				have_initial = 1; /* substring initial match */
			} else {
				last = &mrVALUE->bv_val[mrVALUE->bv_len-1]; /* point at end */
			}
			if (have_initial == have_final) { /* both or none specified - assume any */
				mrpriv->any[0] = PL_strndup(first, last-first);
			} else if (have_initial) {
				mrpriv->initial = PL_strndup(first, last-first);
			} else if (have_final) {
				mrpriv->final = PL_strndup(first, last-first);
			}
		}
		if (ftype == LDAP_FILTER_SUBSTRINGS) {
			mrpriv->match_fn = pi->plg_mr_filter_sub;
		} else {
			mrpriv->match_fn = pi->plg_mr_filter_ava;
		}
#else
		mrpriv->match_fn = pi->plg_mr_filter_ava;
#endif
		slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, mrpriv);
		slapi_pblock_set(pb, SLAPI_PLUGIN_MR_FILTER_MATCH_FN, default_mr_filter_match);
		slapi_pblock_set(pb, SLAPI_PLUGIN_MR_FILTER_INDEX_FN, default_mr_filter_index);
		slapi_pblock_set(pb, SLAPI_PLUGIN_DESTROY_FN, default_mr_filter_destroy);
		rc = 0; /* success */
	} else {
		LDAPDebug(LDAP_DEBUG_FILTER,
				  "default_mr_filter_create: missing parameter: %s%s%s\n",
				  mrOID ? "" : " oid",
				  mrTYPE ? "" : " attribute type",
				  mrVALUE ? "" : " filter value");
	}

done:
	LDAPDebug1Arg(LDAP_DEBUG_FILTER, "=> default_mr_filter_create: %d\n", rc);

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
    if (NULL == mrf_create) {
	/* no create func - unavailable */
	rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
    }
    return rc;
}

int /* an LDAP error code, hopefully LDAP_SUCCESS */
plugin_mr_filter_create (mr_filter_t* f)
{
    int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
    struct slapdplugin* mrp = plugin_mr_find_registered (f->mrf_oid);
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
    if (rc)
    {
		/* look for a new syntax-style mr plugin */
		mrp = plugin_mr_find(f->mrf_oid);
		if (mrp)
		{
		    /* set the default index create fn */
		    pblock_init(&pb);
		    slapi_pblock_set(&pb, SLAPI_PLUGIN, mrp);
		    slapi_pblock_set(&pb, SLAPI_PLUGIN_MR_FILTER_CREATE_FN, default_mr_filter_create);
		    if (!(rc = attempt_mr_filter_create (f, mrp, &pb)))
		    {
				plugin_mr_bind (f->mrf_oid, mrp); /* for future reference */
		    }
		}
    }
    if (!rc)
    {
		/* This plugin has created the desired filter. */
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

static int
default_mr_indexer_destroy(Slapi_PBlock* pb)
{
	struct mr_private *mrpriv = NULL;

	slapi_pblock_get(pb, SLAPI_PLUGIN_OBJECT, &mrpriv);
	mr_private_free(&mrpriv);
	mrpriv = NULL;
	slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, mrpriv);

	return 0;
}

/* this is the default mr indexer create func
   for new syntax-style mr plugins */
static int
default_mr_indexer_create(Slapi_PBlock* pb)
{
	int rc = -1;
	struct slapdplugin *pi = NULL;
	
	slapi_pblock_get(pb, SLAPI_PLUGIN, &pi);
	if (NULL == pi) {
		LDAPDebug0Args(LDAP_DEBUG_ANY, "default_mr_indexer_create: error - no plugin specified\n");
		goto done;
	}

	if (NULL == pi->plg_mr_values2keys) {
		LDAPDebug1Arg(LDAP_DEBUG_ANY, "default_mr_indexer_create: error - plugin [%s] has no plg_mr_values2keys function\n",
					  pi->plg_name);
		goto done;
	}

	slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, mr_private_new(pi, NULL, NULL, NULL, 0, 0));
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_INDEX_FN, mr_wrap_mr_index_fn);
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_INDEX_SV_FN, mr_wrap_mr_index_sv_fn);
	slapi_pblock_set(pb, SLAPI_PLUGIN_DESTROY_FN, default_mr_indexer_destroy);
	slapi_pblock_set(pb, SLAPI_PLUGIN_MR_INDEXER_CREATE_FN, default_mr_indexer_create);
	rc = 0;

done:
	return rc;
}
