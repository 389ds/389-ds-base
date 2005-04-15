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
/* orfilter.c - implementation of ordering rule filter */

#include <ldap.h> /* LDAP_UTF8INC */
#include <slap.h> /* for debug macros */
#include <slapi-plugin.h> /* slapi_berval_cmp, SLAPI_BERVAL_EQ */
#include "collate.h" /* indexer_t, collation_xxx */
#include "config.h" /* collation_read_config */
#include "orfilter.h"

#ifdef HPUX11
#include <dl.h>
#endif /* HPUX11 */

static indexer_t*
indexer_create (const char* oid)
{
    return collation_indexer_create (oid);
}

static void
indexer_free (indexer_t* ix)
{
    if (ix->ix_destroy != NULL) {
	ix->ix_destroy (ix);
    }
    slapi_ch_free((void**)&ix);
}

typedef struct or_filter_t {
    /* implements a filter, using an indexer */
    char*           or_type;
    int             or_op; /* LDAPI_OP_xxx */
    char*           or_oid;
    struct berval** or_values;
    struct berval** or_match_keys;
    struct berval** or_index_keys;
    indexer_t*      or_indexer; /* used to construct or_match_keys and or_index_keys */
} or_filter_t;

static or_filter_t*
or_filter_get (Slapi_PBlock* pb)
{
    auto void* obj = NULL;
    if ( ! slapi_pblock_get (pb, SLAPI_PLUGIN_OBJECT, &obj)) {
	return (or_filter_t*)obj;
    }
    return NULL;
}

static int
or_filter_destroy (Slapi_PBlock* pb)
{
    auto or_filter_t* or = or_filter_get (pb);
    LDAPDebug (LDAP_DEBUG_FILTER, "or_filter_destroy(%p)\n", (void*)or, 0, 0);
    if (or != NULL) {
    slapi_ch_free((void**)&or->or_type);
    slapi_ch_free((void**)&or->or_oid);
	if (or->or_values != NULL) {
	    ber_bvecfree (or->or_values);
	    or->or_values = NULL;
	}
	if (or->or_match_keys != NULL) {
	    ber_bvecfree (or->or_match_keys);
	    or->or_match_keys = NULL;
	}
	if (or->or_index_keys != NULL) {
	    ber_bvecfree (or->or_index_keys);
	    or->or_index_keys = NULL;
	}
	if (or->or_indexer != NULL) {
	    indexer_free (or->or_indexer);
	    or->or_indexer = NULL;
	}
	slapi_ch_free((void**)&or);
    }
    return 0;
}

#define MAX_CHAR_COMBINING 3
/* The maximum number of Unicode characters that may combine
   to form a single collation element.
*/

static int
ss_match (struct berval* value,
          const struct berval* key0,
          indexer_t* ix)
/* returns:  0  a prefix of value matched key
 *           1  a subsequent substring might match; try again
 *          -1  nothing in value will match; give up
 */
{
    auto struct berval* vals[2];
    auto struct berval val;
    auto struct berval key;
    auto size_t attempts = MAX_CHAR_COMBINING;

    vals[0] = &val;
    vals[1] = NULL;
    val.bv_val = value->bv_val;
    val.bv_len = 0;
    key.bv_val = key0->bv_val;
    key.bv_len = key0->bv_len - 1;
    while (1) {
        auto struct berval** vkeys = ix->ix_index (ix, vals, NULL);
        if (vkeys && vkeys[0]) {
            auto const struct berval* vkey = vkeys[0];
            if (vkey->bv_len > key.bv_len) {
		if (--attempts <= 0) {
		    break; /* No match at this starting point */
		} /* else Try looking at another character;
		     it may combine, and produce a shorter key.
		  */
            } else if (SLAPI_BERVAL_EQ (vkey, &key)) {
                value->bv_len -= val.bv_len;
                value->bv_val += val.bv_len;
                return 0;
            }
        }
        if (val.bv_len >= value->bv_len) {
            break;
        }
        val.bv_len += LDAP_UTF8LEN (val.bv_val + val.bv_len);
    }
    if (value->bv_len > 0) {
	auto size_t one = LDAP_UTF8LEN (value->bv_val);
	value->bv_len -= one;
	value->bv_val += one;
	return 1;
    }
    return -1;
}

static int
ss_filter_match (or_filter_t* or, struct berval** vals)
/* returns:  0  filter matched
 *          -1  filter did not match
 *          >0  an LDAP error code
 */
{
    auto int rc = -1; /* no match */
    auto indexer_t* ix = or->or_indexer;
    if (vals != NULL) for (; *vals; ++vals) {
	auto struct berval v;
	auto struct berval** k = or->or_match_keys;
	if (k == NULL || *k == NULL) {
	    rc = 0;		/* present */
	    break;
	}
	v.bv_len = (*vals)->bv_len;
	v.bv_val = (*vals)->bv_val;
	if ((*k)->bv_len > 0 && ss_match (&v, *k, ix) != 0) {
	    break;		/* initial failed */
	}
	rc = 0; /* so far, so good */
	while (*++k) {
	    if ((*k)->bv_len <= 0) {
		rc = 0;
	    } else if (k[1]) {  /* middle */
		do {
		    rc = ss_match (&v, *k, ix);
		} while (rc > 0);
		if (rc < 0) {
		    break;
		}
	    } else {		/* final */
		auto size_t attempts = MAX_CHAR_COMBINING;
		auto char* limit = v.bv_val;
		auto struct berval** vkeys;
		auto struct berval* vals[2];
		auto struct berval key;
		rc = -1;
		vals[0] = &v;
		vals[1] = NULL;
		key.bv_val = (*k)->bv_val;
		key.bv_len = (*k)->bv_len - 1;
		v.bv_val = (*vals)->bv_val + (*vals)->bv_len;
		while(1) {
		    v.bv_len = (*vals)->bv_len - (v.bv_val - (*vals)->bv_val);
		    vkeys = ix->ix_index (ix, vals, NULL);
		    if (vkeys && vkeys[0]) {
			auto const struct berval* vkey = vkeys[0];
			if (vkey->bv_len > key.bv_len) {
			    if (--attempts <= 0) {
				break;
			    } /* else Try looking at another character;
				 it may combine, and produce a shorter key.
			      */
			} else if (SLAPI_BERVAL_EQ (vkey, &key)) {
			    rc = 0;
			    break;
			}
		    }
		    if (v.bv_val <= limit) break;
		    LDAP_UTF8DEC (v.bv_val);
		}
		break;
	    }
	}
	if (rc != -1) break;
    }
    return rc;
}

static int
op_filter_match (or_filter_t* or, struct berval** vals)
{
    auto indexer_t* ix = or->or_indexer;
    auto struct berval** v = ix->ix_index (ix, vals, NULL);
    if (v != NULL) for (; *v; ++v) {
	auto struct berval** k = or->or_match_keys;
	if (k != NULL) for (; *k; ++k) {
	    switch (or->or_op) {
	      case SLAPI_OP_LESS:
		if (slapi_berval_cmp (*v, *k) <  0) return 0; break;
	      case SLAPI_OP_LESS_OR_EQUAL:
		if (slapi_berval_cmp (*v, *k) <= 0) return 0; break;
	      case SLAPI_OP_EQUAL:
		if (SLAPI_BERVAL_EQ  (*v, *k))      return 0; break;
	      case SLAPI_OP_GREATER_OR_EQUAL:
		if (slapi_berval_cmp (*v, *k) >= 0) return 0; break;
	      case SLAPI_OP_GREATER:
		if (slapi_berval_cmp (*v, *k) >  0) return 0; break;
	      default:
		break;
	    }
	}
    }
    return -1;
}

static int
or_filter_match (void* obj, Slapi_Entry* entry, Slapi_Attr* attr)
/* returns:  0  filter matched
 *	    -1  filter did not match
 *	    >0  an LDAP error code
 */
{
    auto int rc = -1; /* no match */
    auto or_filter_t* or = (or_filter_t*)obj;
    for (; attr != NULL; slapi_entry_next_attr (entry, attr, &attr)) {
	auto char* type = NULL;
	auto struct berval** vals = NULL;

/*
 * XXXmcs 1-March-2001: This code would perform better if it did not make
 * a copy of the values here, but that would require re-writing the code
 * in this file to use Slapi_ValueSet's instead of struct berval **'s
 * (and that is not a small project).
 */
	if (!slapi_attr_get_type (attr, &type) && type != NULL &&
	    !slapi_attr_type_cmp (or->or_type, type, 2/*match subtypes*/) &&
	    !slapi_attr_get_bervals_copy(attr, &vals) && vals != NULL) {

	    if (or->or_op == SLAPI_OP_SUBSTRING) {
		rc = ss_filter_match (or, vals);
	    } else {
		rc = op_filter_match (or, vals);
	    }

	    ber_bvecfree( vals );
	    vals = NULL;
	    if (rc >= 0) break;
	}
    }
    return rc;
}

#define WILDCARD '*'
/* If you want a filter value to contain a non-wildcard '*' or '\'
   you write "\2a" or "\5c" (the ASCII codes, in hexadecimal).
   For example, "4\2a4*flim\5cflam"
   matches a value that begins with "4*4" and ends with "flim\flam"
   (except that all the "\" should be doubled in C string literals).
   This conforms to <draft-ietf-asid-ldapv3-attributes-08> section 8.3.
*/

static void
ss_unescape (struct berval* val)
{
    char* s = val->bv_val;
    char* t = s;
    char* limit = s + val->bv_len;
    while (s < limit) {
	if (!memcmp (s, "\\2a", 3) ||
	    !memcmp (s, "\\2A", 3)) {
	    *t++ = WILDCARD;
	    s += 3;
	} else if (!memcmp (s, "\\5c", 3) ||
		   !memcmp (s, "\\5C", 3)) {
	    *t++ = '\\';
	    s += 3;
	} else {
	    if (t == s) LDAP_UTF8INC (t);
	    else t += LDAP_UTF8COPY (t, s);
	    LDAP_UTF8INC (s);
	}
    }
    val->bv_len = t - val->bv_val;
}

static struct berval*
slapi_ch_bvdup0 (struct berval* val)
    /* Return a copy of val, with a 0 byte following the end. */
{
    auto struct berval* result = (struct berval*)
      slapi_ch_malloc (sizeof (struct berval));
    result->bv_len = val->bv_len;
    result->bv_val = slapi_ch_malloc (result->bv_len + 1);
    if (result->bv_len > 0) {
	memcpy (result->bv_val, val->bv_val, result->bv_len);
    }
    result->bv_val[result->bv_len] = '\0';
    return result;
}

static struct berval*
ss_filter_value (const char* s, const size_t len, struct berval* val)
{
    val->bv_len = len;
    if (len > 0) memcpy (val->bv_val, s, len);
    ss_unescape (val);
    return slapi_ch_bvdup0 (val);
}

static struct berval**
ss_filter_values (struct berval* pattern, int* query_op)
    /* Split the pattern into its substrings and return them. */
{
    auto struct berval** result;
    auto struct berval val;
    auto size_t n;
    auto char* s;
    auto char* p;
    auto char* plimit = pattern->bv_val + pattern->bv_len;

    /* Compute the length of the result array, and
       the maximum bv_len of any of its elements. */
    val.bv_len = 0;
    n = 2; /* one key, plus NULL terminator */
    s = pattern->bv_val;
    for (p = s; p < plimit; LDAP_UTF8INC(p)) {
	switch (*p) {
	  case WILDCARD:
	    ++n;
	    {
		auto const size_t len = (p - s);
		if (val.bv_len < len)
		    val.bv_len = len;
	    }
	    while (++p != plimit && *p == WILDCARD);
	    s = p;
	    break;
	  default: break;
	}
    }
    if (n == 2) { /* no wildcards in pattern */
	auto struct berval** pvec = (struct berval**) slapi_ch_malloc (sizeof (struct berval*) * 2);
	auto struct berval* pv = (struct berval*) slapi_ch_malloc (sizeof (struct berval));
	pvec[0] = pv;
	pvec[1] = NULL;
	pv->bv_len = pattern->bv_len;
	pv->bv_val = slapi_ch_malloc (pv->bv_len);
	memcpy (pv->bv_val, pattern->bv_val, pv->bv_len);
	ss_unescape (pv);
	*query_op = SLAPI_OP_EQUAL;
	return pvec;
    } else if (n == 3 && pattern->bv_len <= 1) { /* entire pattern is one wildcard */
	return NULL; /* presence */
    }
    {
	auto const size_t len = (p - s);
	if (val.bv_len < len)
	    val.bv_len = len;
    }
    result = (struct berval**) slapi_ch_malloc (n * sizeof (struct berval*));
    val.bv_val = slapi_ch_malloc (val.bv_len);
    n = 0;
    s = pattern->bv_val;
    for (p = s; p < plimit; LDAP_UTF8INC(p)) {
	switch (*p) {
	  case WILDCARD:
	    result[n++] = ss_filter_value (s, p-s, &val);
	    while (++p != plimit && *p == WILDCARD);
	    s = p;
	    break;
	  default: break;
	}
    }
    if (p != s || s == plimit) {
	result[n++] = ss_filter_value (s, p-s, &val);
    }
    result[n] = NULL;
    slapi_ch_free((void**)&val.bv_val);
    return result;
}

static struct berval*
ss_filter_key (indexer_t* ix, struct berval* val)
{
    struct berval* key = (struct berval*) slapi_ch_calloc (1, sizeof(struct berval));
    if (val->bv_len > 0) {
	struct berval** keys = NULL;
	auto struct berval* vals[2];
	vals[0] = val;
	vals[1] = NULL;
	keys = ix->ix_index (ix, vals, NULL);
	if (keys && keys[0]) {
	    key->bv_len = keys[0]->bv_len + 1;
	    key->bv_val = slapi_ch_malloc (key->bv_len);
	    memcpy (key->bv_val, keys[0]->bv_val, keys[0]->bv_len);
	    key->bv_val[key->bv_len-1] = '\0';
	}
    }
    return key;
}

static struct berval**
ss_filter_keys (indexer_t* ix, struct berval** values)
    /* Index the substrings and return the key values,
       with an extra byte appended to each key, so that
       an empty key definitely implies an absent value.
    */
{
    auto struct berval** keys = NULL;
    if (values != NULL) {
	auto size_t n; /* how many substring values */
	auto struct berval** val;
	for (n=0,val=values; *val != NULL; ++n,++val);
	keys = (struct berval**) slapi_ch_malloc ((n+1) * sizeof (struct berval*));
	for (n=0,val=values; *val != NULL; ++n,++val) {
	    keys[n] = ss_filter_key (ix, *val);
	}
	keys[n] = NULL;
    }
    return keys;
}

static int or_filter_index (Slapi_PBlock* pb);

static int
or_filter_create (Slapi_PBlock* pb)
{
    auto int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION; /* failed to initialize */
    auto char* mrOID = NULL;
    auto char* mrTYPE = NULL;
    auto struct berval* mrVALUE = NULL;
    auto or_filter_t* or = NULL;
    
    if (!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_OID, &mrOID) && mrOID != NULL &&
	!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_TYPE, &mrTYPE) && mrTYPE != NULL &&
	!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_VALUE, &mrVALUE) && mrVALUE != NULL) {
	auto size_t len = mrVALUE->bv_len;
	auto indexer_t* ix = NULL;
	auto int op = SLAPI_OP_EQUAL;
	auto struct berval bv;
	auto int reusable = MRF_ANY_TYPE;

	LDAPDebug (LDAP_DEBUG_FILTER, "=> or_filter_create(oid %s; type %s)\n",
		   mrOID, mrTYPE, 0);
	if (len > 1 && (ix = indexer_create (mrOID)) != NULL) {
	    auto char* val = mrVALUE->bv_val;
	    switch (val[0]) {
	      case '=': break;
	      case '<': op = (val[1] == '=') ?
		SLAPI_OP_LESS_OR_EQUAL : SLAPI_OP_LESS; break;
	      case '>': op = (val[1] == '=') ?
		SLAPI_OP_GREATER_OR_EQUAL : SLAPI_OP_GREATER; break;
	      case WILDCARD: op = SLAPI_OP_SUBSTRING; break;
	      default:
		break;
	    }
	    for (; len > 0 && *val != ' '; ++val, --len);
	    if (len > 0) ++val, --len; /* skip the space */
	    bv.bv_len = len;
	    bv.bv_val = (len > 0) ? val : NULL;
	} else { /* mrOID does not identify an ordering rule. */
	    /* Is it an ordering rule OID with a relational operator suffix? */
	    auto size_t oidlen = strlen (mrOID);
	    if (oidlen > 2 && mrOID[oidlen-2] == '.') {
		op = atoi (mrOID + oidlen - 1);
		switch (op) {
		  case SLAPI_OP_LESS:
		  case SLAPI_OP_LESS_OR_EQUAL:
		  case SLAPI_OP_EQUAL:
		  case SLAPI_OP_GREATER_OR_EQUAL:
		  case SLAPI_OP_GREATER:
		  case SLAPI_OP_SUBSTRING:
		    {
			auto char* or_oid = slapi_ch_strdup (mrOID);
			or_oid [oidlen-2] = '\0';
			ix = indexer_create (or_oid);
			if (ix != NULL) {
			    memcpy (&bv, mrVALUE, sizeof(struct berval));
			    reusable |= MRF_ANY_VALUE;
			}
			slapi_ch_free((void**)&or_oid);
		    }
		    break;
		  default: /* not a relational operator */
		    break;
		}
	    }
	}
	if (ix != NULL) {
	    or = (or_filter_t*) slapi_ch_calloc (1, sizeof (or_filter_t));
	    or->or_type = slapi_ch_strdup (mrTYPE);
	    or->or_indexer = ix;
	    or->or_op = op;
	    if (op == SLAPI_OP_SUBSTRING) {
		or->or_values = ss_filter_values (&bv, &(or->or_op));
	    } else {
		or->or_values = (struct berval**)
		  slapi_ch_malloc (2 * sizeof (struct berval*));
		or->or_values[0] = slapi_ch_bvdup0 (&bv);
		or->or_values[1] = NULL;
	    }
	    {
		auto struct berval** val = or->or_values;
		if (val) for (; *val; ++val) {
		    LDAPDebug (LDAP_DEBUG_FILTER, "value \"%s\"\n", (*val)->bv_val, 0, 0);
		}
	    }
	    if (or->or_op == SLAPI_OP_SUBSTRING) {
		or->or_match_keys = ss_filter_keys (ix, or->or_values);
	    } else {
		or->or_match_keys = slapi_ch_bvecdup (
						ix->ix_index (ix, or->or_values, NULL));	
	    }
	    slapi_pblock_set (pb, SLAPI_PLUGIN_OBJECT, or);
	    slapi_pblock_set (pb, SLAPI_PLUGIN_DESTROY_FN, (void*)or_filter_destroy);
	    slapi_pblock_set (pb, SLAPI_PLUGIN_MR_FILTER_MATCH_FN, (void*)or_filter_match);
	    slapi_pblock_set (pb, SLAPI_PLUGIN_MR_FILTER_INDEX_FN, (void*)or_filter_index);
/*	    slapi_pblock_set (pb, SLAPI_PLUGIN_MR_FILTER_REUSABLE, &reusable); */
/*	    slapi_pblock_set (pb, SLAPI_PLUGIN_MR_FILTER_RESET_FN, ?); to be implemented */
	    rc = LDAP_SUCCESS;
	}
    } else {
	LDAPDebug (LDAP_DEBUG_FILTER, "=> or_filter_create missing parameter(s)\n", 0, 0, 0);
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "<= or_filter_create(%p) %i\n", (void*)or, rc, 0);
    return rc;
}

static indexer_t*
op_indexer_get (Slapi_PBlock* pb)
{
    auto void* obj = NULL;
    if ( ! slapi_pblock_get (pb, SLAPI_PLUGIN_OBJECT, &obj)) {
	return (indexer_t*)obj;
    }
    return NULL;
}

static int
op_indexer_destroy (Slapi_PBlock* pb)
{
    auto indexer_t* ix = op_indexer_get (pb);
    LDAPDebug (LDAP_DEBUG_FILTER, "op_indexer_destroy(%p)\n", (void*)ix, 0, 0);
    if (ix != NULL) {
	indexer_free (ix);
    }
    return 0;
}

static int
op_index_entry (Slapi_PBlock* pb)
     /* Compute collation keys (when writing an entry). */
{
    auto indexer_t* ix = op_indexer_get (pb);
    auto int rc;
    struct berval** values;
    if (ix != NULL && ix->ix_index != NULL &&
	!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_VALUES, &values) &&
	!slapi_pblock_set (pb, SLAPI_PLUGIN_MR_KEYS, ix->ix_index (ix, values, NULL))) {
	rc = 0;
    } else {
	rc = LDAP_OPERATIONS_ERROR;
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "op_index_entry(%p) %i\n", (void*)ix, rc, 0);
    return rc;
}

static int
op_index_search (Slapi_PBlock* pb)
     /* Compute collation keys (when searching for entries). */
{
    auto or_filter_t* or = or_filter_get (pb);
    auto int rc = LDAP_OPERATIONS_ERROR;
    if (or != NULL) {
	auto indexer_t* ix = or->or_indexer;
	struct berval** values;
	if (or->or_index_keys == NULL && ix != NULL && ix->ix_index != NULL &&
	    !slapi_pblock_get (pb, SLAPI_PLUGIN_MR_VALUES, &values)) {
	    or->or_index_keys = slapi_ch_bvecdup (
					    ix->ix_index (ix, values, NULL));
	}
	if (or->or_index_keys) {
	    rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_KEYS, or->or_index_keys);
	}
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "op_index_search(%p) %i\n", (void*)or, rc, 0);
    return rc;
}

typedef struct ss_indexer_t {
    char*      ss_oid; /* ss_indexer->ix_oid && ".6" */
    indexer_t* ss_indexer;
} ss_indexer_t;

static void
ss_indexer_free (ss_indexer_t* ss)
{
	slapi_ch_free((void**)&ss->ss_oid);
    if (ss->ss_indexer != NULL) {
	indexer_free (ss->ss_indexer);
	ss->ss_indexer = NULL;
    }
    slapi_ch_free((void**)&ss);
}

static ss_indexer_t*
ss_indexer_get (Slapi_PBlock* pb)
{
    auto void* obj = NULL;
    if ( ! slapi_pblock_get (pb, SLAPI_PLUGIN_OBJECT, &obj)) {
	return (ss_indexer_t*)obj;
    }
    return NULL;
}

static void
ss_indexer_destroy (Slapi_PBlock* pb)
{
    auto ss_indexer_t* ss = ss_indexer_get (pb);
    LDAPDebug (LDAP_DEBUG_FILTER, "ss_indexer_destroy(%p)\n", (void*)ss, 0, 0);
    if (ss) {
	ss_indexer_free (ss);
    }
}

#define SS_INDEX_LENGTH 3 /* characters */

static char ss_prefixI = '[';
static char ss_prefixM = '|';
static char ss_prefixF = '}';

static struct berval ss_index_initial = {1, &ss_prefixI};
static struct berval ss_index_middle = {1, &ss_prefixM};
static struct berval ss_index_final = {1, &ss_prefixF};

static int
long_enough (struct berval* bval, size_t enough)
{
    if (bval) {
	auto size_t len = 0;
	auto char* next = bval->bv_val;
	auto char* last = next + bval->bv_len;
	while (next < last) {
	    LDAP_UTF8INC (next);
	    if (++len >= enough) {
		if (next > last) next = last;
		bval->bv_len = next - bval->bv_val;
		return 1;
	    }
	}
    }
    return !enough;
}

static int
ss_index_entry (Slapi_PBlock* pb)
     /* Compute substring index keys (when writing an entry). */
{
    auto int rc = LDAP_OPERATIONS_ERROR;
    auto size_t substringsLen = 0;
    struct berval** values;
    auto ss_indexer_t* ss = ss_indexer_get (pb);
    auto indexer_t* ix = ss ? ss->ss_indexer : NULL;
    if (ix != NULL && ix->ix_index != NULL &&
	!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_VALUES, &values)) {
	auto struct berval* substrings = NULL;
	auto struct berval** prefixes = NULL;
	auto struct berval** value;
	for (value = values; *value != NULL; ++value) {
	    auto struct berval substring;
	    substring.bv_val = (*value)->bv_val;
	    substring.bv_len = (*value)->bv_len;
	    if (long_enough (&substring, SS_INDEX_LENGTH-1)) {
		auto struct berval* prefix = &ss_index_initial;
		auto size_t offset;
		for (offset = 0; 1; ++offset) {
		    ++substringsLen;
		    substrings = (struct berval*)
		      slapi_ch_realloc ((void*)substrings, substringsLen * sizeof(struct berval));
		    memcpy (&(substrings[substringsLen-1]), &substring, sizeof (struct berval));
		    prefixes = (struct berval**)
		      slapi_ch_realloc ((void*)prefixes, substringsLen * sizeof(struct berval*));
		    prefixes[substringsLen-1] = prefix;

		    if (offset != 0) LDAP_UTF8INC (substring.bv_val);
		    substring.bv_len = (*value)->bv_len - (substring.bv_val - (*value)->bv_val);
		    if (long_enough (&substring, SS_INDEX_LENGTH)) {
			prefix = &ss_index_middle;
		    } else if (long_enough (&substring, SS_INDEX_LENGTH-1)) {
			prefix = &ss_index_final;
		    } else {
			break;
		    }
		}
	    }
	}
	if (substrings != NULL) {
	    auto struct berval** vector = (struct berval**)
	      slapi_ch_malloc ((substringsLen+1) * sizeof(struct berval*));
	    auto size_t i;
	    for (i = 0; i < substringsLen; ++i) vector[i] = &(substrings[i]);
	    vector[substringsLen] = NULL;
	    rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_KEYS, ix->ix_index (ix, vector, prefixes));
	    slapi_ch_free((void**)&vector);
	    slapi_ch_free((void**)&substrings);
	    slapi_ch_free((void**)&prefixes);
	}
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "ss_index_entry(%p) %i %lu substrings\n",
	       (void*)ss, rc, (unsigned long)substringsLen);
    return rc;
}

static int
ss_index_search (Slapi_PBlock* pb)
     /* Compute substring search keys (when searching for entries). */
{
    auto int rc = LDAP_OPERATIONS_ERROR;
    auto or_filter_t* or = or_filter_get (pb);
    if (or) {
	if (or->or_index_keys == NULL /* not yet computed */ &&
	    or->or_values && or->or_indexer && or->or_indexer->ix_index) {
	    auto size_t substringsLen = 0;
	    auto struct berval* substrings = NULL;
	    auto struct berval** prefixes = NULL;
	    auto struct berval** value;
	    for (value = or->or_values; *value != NULL; ++value) {
		auto size_t offset;
		auto struct berval substring;
		substring.bv_val = (*value)->bv_val;
		for (offset = 0; 1; ++offset, LDAP_UTF8INC (substring.bv_val)) {
		    auto struct berval* prefix = NULL;
		    substring.bv_len = (*value)->bv_len - (substring.bv_val - (*value)->bv_val);
		    if (offset == 0 && value == or->or_values) {
			if (long_enough (&substring, SS_INDEX_LENGTH - 1)) {
			    prefix = &ss_index_initial;
			}
		    } else if (value[1] != NULL) {
			if (long_enough (&substring, SS_INDEX_LENGTH)) {
			    prefix = &ss_index_middle;
			}
		    } else if (long_enough (&substring, SS_INDEX_LENGTH)) {
			prefix = &ss_index_middle;
		    } else if (long_enough (&substring, SS_INDEX_LENGTH-1)) {
			prefix = &ss_index_final;
		    }
		    if (prefix == NULL) break;
		    ++substringsLen;
		    substrings = (struct berval*)
		      slapi_ch_realloc ((void*)substrings, substringsLen * sizeof(struct berval));
		    memcpy (&(substrings[substringsLen-1]), &substring, sizeof (struct berval));
		    prefixes = (struct berval**)
		      slapi_ch_realloc ((void*)prefixes, substringsLen * sizeof(struct berval*));
		    prefixes[substringsLen-1] = prefix;
		}
	    }
	    if (substrings != NULL) {
		auto indexer_t* ix = or->or_indexer;
		auto struct berval** vector = (struct berval**)
		  slapi_ch_malloc ((substringsLen+1) * sizeof(struct berval*));
		auto size_t i;
		for (i = 0; i < substringsLen; ++i) vector[i] = &(substrings[i]);
		vector[substringsLen] = NULL;
		or->or_index_keys = slapi_ch_bvecdup (
						ix->ix_index (ix, vector, prefixes));
		slapi_ch_free((void**)&vector);
		slapi_ch_free((void**)&substrings);
		slapi_ch_free((void**)&prefixes);
	    }
	}
	if (or->or_index_keys) {
	    rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_KEYS, or->or_index_keys);
	}
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "ss_index_search(%p) %i\n", (void*)or, rc, 0);
    return rc;
}

static int
ss_indexable (struct berval** values)
    /* at least one of the values is long enough to index */
{
    auto struct berval** val = values;
    if (val) for (; *val; ++val) {
	auto struct berval value;
	value.bv_val = (*val)->bv_val;
	value.bv_len = (*val)->bv_len;
	if (val == values) { /* initial */
	    if (long_enough (&value, SS_INDEX_LENGTH-1)) return 1;
	} else if (val[1]) { /* middle */
	    if (long_enough (&value, SS_INDEX_LENGTH)) return 1;
	} else {             /* final */
	    if (long_enough (&value, SS_INDEX_LENGTH-1)) return 1;
	}
    }
    return 0;
}

static struct berval ss_one_berval = {0,0};
static struct berval* ss_one_value[2] = {&ss_one_berval, NULL};

static int
or_filter_index (Slapi_PBlock* pb)
    /* Return an indexer and values that accelerate the given filter. */
{
    auto or_filter_t* or = or_filter_get (pb);
    auto int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
    auto IFP mrINDEX_FN = NULL;
    auto struct berval** mrVALUES = NULL;
    auto char* mrOID = NULL;
    auto int mrQUERY_OPERATOR;
    if (or && or->or_indexer && or->or_indexer->ix_index) {
	switch (or->or_op) {
	  case SLAPI_OP_LESS:
	  case SLAPI_OP_LESS_OR_EQUAL:
	  case SLAPI_OP_EQUAL:
	  case SLAPI_OP_GREATER_OR_EQUAL:
	  case SLAPI_OP_GREATER:
	    mrINDEX_FN = op_index_search;	
	    mrVALUES = or->or_values;
	    mrOID = or->or_indexer->ix_oid;
	    mrQUERY_OPERATOR = or->or_op;
	    break;
	  case SLAPI_OP_SUBSTRING:
	    if (ss_indexable (or->or_values)) {	
		if (or->or_oid == NULL) {
		    auto const size_t len = strlen (or->or_indexer->ix_oid);
		    or->or_oid = slapi_ch_malloc (len + 3);
		    memcpy (or->or_oid, or->or_indexer->ix_oid, len);
		    sprintf (or->or_oid + len, ".%1i", SLAPI_OP_SUBSTRING);
		}
		mrINDEX_FN = ss_index_search;
		mrVALUES = ss_one_value;
		mrOID = or->or_oid;
		mrQUERY_OPERATOR = SLAPI_OP_EQUAL;
	    }
	    break;
	  default: /* unsupported operator */
	    break;
	}
    }
    if (mrINDEX_FN != NULL &&
	!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_OBJECT, or)) &&
	!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_TYPE, or->or_type)) &&
	!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_INDEX_FN, (void*)mrINDEX_FN)) &&
	!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_VALUES, mrVALUES)) &&
	!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_OID, mrOID))) {
	rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_QUERY_OPERATOR, &mrQUERY_OPERATOR);
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "or_filter_index(%p) %i\n",
	       (void*)(or ? or->or_indexer : NULL), rc, 0);
    return rc;
}

static int
or_indexer_create (Slapi_PBlock* pb)
{
    auto int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION; /* failed to initialize */
    auto char* mrOID = NULL;
    auto void* mrOBJECT = NULL;
    if (slapi_pblock_get (pb, SLAPI_PLUGIN_MR_OID, &mrOID) || mrOID == NULL) {
	LDAPDebug (LDAP_DEBUG_FILTER, "=> or_indexer_create: no OID parameter\n", 0, 0, 0);
    } else {
	auto indexer_t* ix = indexer_create (mrOID);
	auto char* mrTYPE = NULL;
	slapi_pblock_get (pb, SLAPI_PLUGIN_MR_TYPE, &mrTYPE);
	LDAPDebug (LDAP_DEBUG_FILTER, "=> or_indexer_create(oid %s; type %s)\n",
		   mrOID, mrTYPE ? mrTYPE : "<NULL>", 0);
	if (ix != NULL) {
	    if (ix->ix_index != NULL &&
		!slapi_pblock_set (pb, SLAPI_PLUGIN_OBJECT, ix) &&
		!slapi_pblock_set (pb, SLAPI_PLUGIN_MR_OID, ix->ix_oid) &&
		!slapi_pblock_set (pb, SLAPI_PLUGIN_MR_INDEX_FN, (void*)op_index_entry) &&
		!slapi_pblock_set (pb, SLAPI_PLUGIN_DESTROY_FN, (void*)op_indexer_destroy)) {
		mrOBJECT = ix;
		rc = 0; /* success */
	    } else {
		indexer_free (ix);
	    }
	} else { /* mrOID does not identify an ordering rule. */
	    /* Is it an ordering rule OID with the substring suffix? */
	    auto size_t oidlen = strlen (mrOID);
	    if (oidlen > 2 && mrOID[oidlen-2] == '.' &&
		atoi (mrOID + oidlen - 1) == SLAPI_OP_SUBSTRING) {
		auto char* or_oid = slapi_ch_strdup (mrOID);
		or_oid [oidlen-2] = '\0';
		ix = indexer_create (or_oid);
		if (ix != NULL) {
		    auto ss_indexer_t* ss = (ss_indexer_t*) slapi_ch_malloc (sizeof (ss_indexer_t));
		    ss->ss_indexer = ix;
		    oidlen = strlen (ix->ix_oid);
		    ss->ss_oid = slapi_ch_malloc (oidlen + 3);
		    memcpy (ss->ss_oid, ix->ix_oid, oidlen);
		    sprintf (ss->ss_oid + oidlen, ".%1i", SLAPI_OP_SUBSTRING);
		    if (ix->ix_index != NULL &&
			!slapi_pblock_set (pb, SLAPI_PLUGIN_OBJECT, ss) &&
			!slapi_pblock_set (pb, SLAPI_PLUGIN_MR_OID, ss->ss_oid) &&
			!slapi_pblock_set (pb, SLAPI_PLUGIN_MR_INDEX_FN, (void*)ss_index_entry) &&
			!slapi_pblock_set (pb, SLAPI_PLUGIN_DESTROY_FN, (void*)ss_indexer_destroy)) {
			mrOBJECT = ss;
			rc = 0; /* success */
		    } else {
			ss_indexer_free (ss);
		    }
		}
		slapi_ch_free((void**)&or_oid);
	    }
	}
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "<= or_indexer_create(%p) %i\n", mrOBJECT, rc, 0);
    return rc;
}

static Slapi_PluginDesc pdesc = { "orderingrule", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
              "internationalized ordering rule plugin" };

#define SLAPI_ORPLUGIN_NAME	pdesc.spd_description

int /* LDAP error code */
orderingRule_init (Slapi_PBlock* pb)
{
    int rc;
    int argc;
    char** argv;
    char* cfgpath;

/*  if (!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_PRIVATE, ...)) &&
	!(rc = slapi_pblock_set (pb, SLAPI_PLUGIN_CLOSE_FN, ...)))
*/
    
#ifdef USE_HPUX_CC
	/* not needed with ICU
    shl_load ( "../lib/libnsbrk30.sl", BIND_IMMEDIATE, 0L );
    shl_load ( "../lib/libnscnv30.sl", BIND_IMMEDIATE, 0L );
    shl_load ( "../lib/libnscol30.sl", BIND_IMMEDIATE, 0L );
    shl_load ( "../lib/libnsfmt30.sl", BIND_IMMEDIATE, 0L );
    shl_load ( "../lib/libnsres30.sl", BIND_IMMEDIATE, 0L );
    shl_load ( "../lib/libnsuni30.sl", BIND_IMMEDIATE, 0L );
	*/
#endif 

    if ( slapi_pblock_get( pb, SLAPI_CONFIG_DIRECTORY, &cfgpath ) != 0 ) {
	slapi_log_error( SLAPI_LOG_FATAL, SLAPI_ORPLUGIN_NAME,
		"Unable to retrieve slapd configuration pathname; using default\n" );
	cfgpath = NULL;
    }
	
    collation_init( cfgpath );
    if (!slapi_pblock_get (pb, SLAPI_PLUGIN_ARGC, &argc) &&
	!slapi_pblock_get (pb, SLAPI_PLUGIN_ARGV, &argv) &&
	argc > 0) {
	collation_read_config (argv[0]);
    }
    {
	slapi_pblock_set (pb, SLAPI_PLUGIN_MR_INDEXER_CREATE_FN, (void*)or_indexer_create);
	rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_FILTER_CREATE_FN, (void*)or_filter_create);
    }
    if ( rc == 0 ) {
	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc );
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "orderingRule_init %i\n", rc, 0, 0);
    return rc;
}
