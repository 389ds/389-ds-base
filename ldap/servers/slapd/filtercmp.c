/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* filtercmp.c - routines for comparing filters */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "slap.h"


/* very simple hash function */
static PRUint32 addhash(PRUint32 hash, unsigned char *data, int size)
{
    int i;

    if (!data || !size)
	return hash;
    for (i = 0; i < size; i++)
	hash = (hash << 5) + (hash >> 27) + data[i];
    return hash;
}
#define addhash_long(h, l) addhash((h), (unsigned char *)&(l), sizeof(long))
#define addhash_str(h, str) addhash((h), (unsigned char *)(str), strlen(str))
#define addhash_bv(h, bv) addhash((h), (unsigned char *)(bv).bv_val, \
				  (bv).bv_len)

static PRUint32 addhash_casestr(PRUint32 hash, char *data)
{
    unsigned char *normstr;

    normstr = slapi_utf8StrToLower((unsigned char *)data);
    hash = addhash(hash, normstr, strlen((char *)normstr));
    if ((char *)normstr != data)
	slapi_ch_free((void **)&normstr);
    return hash;
}

static PRUint32 stir(PRUint32 hash, PRUint32 x)
{
    hash = (hash << 5) + (hash >> 27);
    hash = hash ^ (x << 16);
    hash = hash ^ (x >> 16);
    return hash;
}
#define STIR(h) (h) = stir((h), 0x2EC6DEAD);

static Slapi_Value **get_normalized_value(struct ava *ava)
{
    void *plugin;
    Slapi_Value *svlist[2], **keylist, sv;

    slapi_attr_type2plugin(ava->ava_type, &plugin);
    sv.bv = ava->ava_value;
    sv.v_csnset = NULL;
    svlist[0] = &sv;
    svlist[1] = NULL;
    if ((slapi_call_syntax_values2keys_sv(plugin, svlist, &keylist,
					  LDAP_FILTER_EQUALITY) != 0) ||
	!keylist || !keylist[0])
	return NULL;
    return keylist;
}

/* this is not pretty.  matching rules seem to be pretty elaborate to use,
 * so comparing these kind of filters may be undesirably slow just because
 * of the overhead of normalizing the values.  most of this code is stolen
 * from the backend vlv code (matchrule.c)
 */
static Slapi_PBlock *get_mr_normval(char *oid, char *type,
				    struct berval **inval,
				    struct berval ***outval)
{
    Slapi_PBlock *pb = slapi_pblock_new();
    unsigned int sort_indicator = SLAPI_PLUGIN_MR_USAGE_SORT;
    IFP mrIndex = NULL;
    
    if (!pb)
	return NULL;
    slapi_pblock_set(pb, SLAPI_PLUGIN_MR_OID, oid);
    slapi_pblock_set(pb, SLAPI_PLUGIN_MR_TYPE, type);
    slapi_pblock_set(pb, SLAPI_PLUGIN_MR_USAGE, (void *)&sort_indicator);
    if (slapi_mr_indexer_create(pb) != 0) {
	slapi_pblock_destroy(pb);
	return NULL;
    }
    if ((slapi_pblock_get(pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrIndex) != 0) ||
	!mrIndex) {
	/* shouldn't ever happen */
	slapi_pblock_destroy(pb);
	return NULL;
    }

    /* now, call the indexer */
    slapi_pblock_set(pb, SLAPI_PLUGIN_MR_VALUES, inval);
    (*mrIndex)(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_MR_KEYS, outval);
    return pb;
}

/* the opposite of above: shut down the matching rule pblock and free
 * the memory.
 */
static void done_mr_normval(Slapi_PBlock *pb)
{
    IFP mrDestroy = NULL;

    if (slapi_pblock_get(pb, SLAPI_PLUGIN_DESTROY_FN, &mrDestroy) == 0) {
	if (mrDestroy)
	    (*mrDestroy)(pb);
    }
    slapi_pblock_destroy(pb);
}

static int hash_filters = 0;

void set_hash_filters(int i) { hash_filters = i; }

/* calculate the hash value of a node in a filter (assumes that any sub-nodes
 * of the filter have already had their hash value calculated).
 * -- the annoying part of this is normalizing any values in the filter.
 */
void filter_compute_hash(struct slapi_filter *f)
{
    PRUint32 h;
    char **a;
    struct slapi_filter *fx;
    Slapi_Value **keylist;
    Slapi_PBlock *pb;
    struct berval *inval[2], **outval;

    if (! hash_filters)
	return;

    h = addhash_long(0, f->f_choice);
    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
	keylist = get_normalized_value(&f->f_ava);
	if (keylist) {
	    h = addhash_str(h, f->f_avtype);
	    STIR(h);
	    h = addhash_bv(h, *(slapi_value_get_berval(keylist[0])));
	    valuearray_free(&keylist);
	}
	break;
    case LDAP_FILTER_SUBSTRINGS:
	h = addhash_str(h, f->f_sub_type);
	STIR(h);
	if (f->f_sub_initial)
	    h = addhash_casestr(h, f->f_sub_initial);
	if (f->f_sub_any) {
	    for (a = f->f_sub_any; *a; a++) {
		STIR(h);
		h = addhash_casestr(h, *a);
	    }
	}
	STIR(h);
	if (f->f_sub_final)
	    h = addhash_casestr(h, f->f_sub_final);
	break;
    case LDAP_FILTER_PRESENT:
	h = addhash_str(h, f->f_type);
	break;
    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
	/* should be able to just mix in the hashes from lower levels */
	for (fx = f->f_list; fx; fx = fx->f_next)
	    h = h ^ fx->f_hash;
	break;
    case LDAP_FILTER_EXTENDED:
	if (f->f_mr_oid)
	    h = addhash_str(h, f->f_mr_oid);
	STIR(h);
	if (f->f_mr_type)
	    h = addhash_str(h, f->f_mr_type);
	inval[0] = &f->f_mr_value;
	inval[1] = NULL;
	/* get the normalized value (according to the matching rule) */
	pb = get_mr_normval(f->f_mr_oid, f->f_mr_type, inval, &outval);
	if (pb && outval && outval[0]) {
	    STIR(h);
	    h = addhash_bv(h, *(outval[0]));
	}
	done_mr_normval(pb);
	if (f->f_mr_dnAttrs)
	    STIR(h);
	break;
    default:
	LDAPDebug(LDAP_DEBUG_ANY, "$$$ can't handle filter type %d !\n",
		  f->f_choice, 0, 0);
    }

    f->f_hash = h;
}


/* match compare: given two arrays of size N, determine if each item in
 * the first array matches with each item in the second array, with a
 * one-to-one correspondence.  this will be DOG SLOW for large values of N
 * (it scales as N^2) but we generally expect N < 5.
 */
static int filter_compare_substrings(struct slapi_filter *f1,
				     struct slapi_filter *f2)
{
    int buf[20], *tally;
    char **a1, **a2;
    int count1 = 0, count2 = 0, ret, i, j, ok;

    /* ok to pass NULL to utf8casecmp */

    if ((slapi_UTF8CASECMP(f1->f_sub_initial, f2->f_sub_initial) != 0) ||
	(slapi_UTF8CASECMP(f1->f_sub_final, f2->f_sub_final) != 0))
	return 1;
    /* match compare (would be expensive for large numbers of 'any'
     * substrings, which we don't expect to see)
     */
    for (a1 = f1->f_sub_any; a1 && *a1; a1++, count1++);
    for (a2 = f2->f_sub_any; a2 && *a2; a2++, count2++);
    if (count1 != count2)
	return 1;
    ret = 1;	/* assume failure until done comparing */
    if (count1 > 20)
	tally = (int *)malloc(count1);
    else
	tally = buf;
    if (!tally)
	goto done;	/* this is bad; out of memory */
    for (i = 0; i < count1; i++)
	tally[i] = 0;
    /* ok.  the theory is we tally up all the matched pairs we find,
     * stopping if we can't find a match that hasn't already been paired.
     */
    a1 = f1->f_sub_any;
    for (i = 0; i < count1; i++, a1++) {
	a2 = f2->f_sub_any;
	ok = 0;
	for (j = 0; j < count1; j++, a2++) {
	    if (!tally[j] && (slapi_UTF8CASECMP(*a1, *a2) == 0)) {
		tally[j] = ok = 1;
		break;
	    }
	}
	if (!ok)
	    goto done;		/* didn't find a match for that one */
    }
    /* done!  matched */
    ret = 0;

done:
    if ((count1 > 20) && tally)
	free(tally);
    return ret;
}

/* same as above, but this time for lists of filter nodes */
static int filter_compare_lists(struct slapi_filter *f1,
				struct slapi_filter *f2)
{
    int buf[20], *tally;
    struct slapi_filter *fx1, *fx2;
    int count1 = 0, count2 = 0, ret, i, j, ok;

    for (fx1 = f1->f_list; fx1; fx1 = fx1->f_next, count1++);
    for (fx2 = f2->f_list; fx2; fx2 = fx2->f_next, count2++);
    if (count1 != count2)
	return 1;
    ret = 1;
    if (count1 > 20)
	tally = (int *)malloc(count1);
    else
	tally = buf;
    if (!tally)
	goto done;	/* very bad */
    for (i = 0; i < count1; i++)
	tally[i] = 0;
    /* brute-force match compare now */
    fx1 = f1->f_list;
    for (i = 0; i < count1; i++, fx1 = fx1->f_next) {
	fx2 = f2->f_list;
	ok = 0;
	for (j = 0; j < count1; j++, fx2 = fx2->f_next) {
	    if (!tally[j] && (slapi_filter_compare(fx1, fx2) == 0)) {
		tally[j] = ok = 1;
		break;
	    }
	}
	if (!ok)
	    goto done;		/* no match */
    }
    /* done! all matched */
    ret = 0;

done:
    if ((count1 > 20) && tally)
	free(tally);
    return ret;
}

/* returns 0 if two filters are "identical"
 * (items under AND/OR are allowed to be in different order)
 */
int slapi_filter_compare(struct slapi_filter *f1, struct slapi_filter *f2)
{
    Slapi_Value **key1, **key2;
    Slapi_PBlock *pb1, *pb2;
    struct berval *inval1[2], *inval2[2], **outval1, **outval2;
    int ret;

    LDAPDebug(LDAP_DEBUG_TRACE, "=> filter compare\n", 0, 0, 0);

    /* allow for the possibility that one of the filters hasn't had a hash
     * computed (and is therefore 0).  this means that a filter node whose
     * hash is computed as 0 will always get compared the expensive way,
     * but this should happen VERY rarely (if ever).
     */
    if ((f1->f_hash != f2->f_hash) && (f1->f_hash) && (f2->f_hash)) {
	ret = 1;
	goto done;
    }

    /* brute-force comparison now */
    if (f1->f_choice != f2->f_choice) {
	ret = 1;
	goto done;
    }
    switch (f1->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
	if (slapi_UTF8CASECMP(f1->f_avtype, f2->f_avtype) != 0) {
	    ret = 1;
	    break;
	}
	key1 = get_normalized_value(&f1->f_ava);
	if (key1) {
	    key2 = get_normalized_value(&f2->f_ava);
	    if (key2) {
		ret = memcmp(slapi_value_get_string(key1[0]), 
                             slapi_value_get_string(key2[0]),
			     slapi_value_get_length(key1[0]));
		valuearray_free(&key1);
		valuearray_free(&key2);
		break;
	    }
	    valuearray_free(&key1);
	}
	ret = 1;
	break;
    case LDAP_FILTER_PRESENT:
	ret = (slapi_UTF8CASECMP(f1->f_type, f2->f_type));
	break;
    case LDAP_FILTER_SUBSTRINGS:
	ret = filter_compare_substrings(f1, f2);
	break;
    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
	ret = filter_compare_lists(f1, f2);
	break;
    case LDAP_FILTER_EXTENDED:
	if ((slapi_UTF8CASECMP(f1->f_mr_oid, f2->f_mr_oid) != 0) ||
	    (slapi_UTF8CASECMP(f1->f_mr_type, f2->f_mr_type) != 0) ||
	    (f1->f_mr_dnAttrs != f2->f_mr_dnAttrs)) {
	    ret = 1;
	    break;
	}
	/* painstakingly compare the values (using the matching rule) */
	inval1[0] = &f1->f_mr_value;
	inval2[0] = &f2->f_mr_value;
	inval1[1] = inval2[1] = NULL;
	pb1 = get_mr_normval(f1->f_mr_oid, f1->f_mr_type, inval1, &outval1);
	pb2 = get_mr_normval(f2->f_mr_oid, f2->f_mr_type, inval2, &outval2);
	if (!pb1 || !pb2 || !outval1 || !outval2 || !outval1[0] ||
	    !outval2[0] || (outval1[0]->bv_len != outval2[0]->bv_len) ||
	    (memcmp(outval1[0]->bv_val, outval2[0]->bv_val,
		    outval1[0]->bv_len) != 0)) {
	    ret = 1;
	} else {
	    ret = 0;
	}
	if (pb1)
	    done_mr_normval(pb1);
	if (pb2)
	    done_mr_normval(pb2);
	break;
    default:
	LDAPDebug(LDAP_DEBUG_ANY, "ERR can't handle filter %d\n", f1->f_choice,
		  0, 0);
	ret = 1;
    }

done:
    LDAPDebug(LDAP_DEBUG_TRACE, "<= filter compare: %d\n", ret, 0, 0);
    return ret;
}
