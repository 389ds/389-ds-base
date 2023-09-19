/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* valueset.c - routines for dealing with value sets */

#include "slap.h"
#include "slapi-private.h"

/* <=========================== Berval Array ==========================> */

/*
 * vals - The existing values.
 * addval - The value to add.
 * nvals - The number of existing values.
 * maxvals - The number of elements in the existing values array.
 */
void
bervalarray_add_berval_fast(
    struct berval ***vals,
    const struct berval *addval,
    int nvals,
    int *maxvals)
{
    int need = nvals + 2;
    if (need > *maxvals) {
        if (*maxvals == 0) {
            *maxvals = 2;
        }
        while (*maxvals < need) {
            *maxvals *= 2;
        }
        if (*vals == NULL) {
            *vals = (struct berval **)slapi_ch_malloc(*maxvals * sizeof(struct berval *));
        } else {
            *vals = (struct berval **)slapi_ch_realloc((char *)*vals, *maxvals * sizeof(struct berval *));
        }
    }
    (*vals)[nvals] = ber_bvdup((struct berval *)addval);
    (*vals)[nvals + 1] = NULL;
}

/* <=========================== Value Array ==========================> */

/*
 * vals - The existing values.
 * addval - The value to add.
 * nvals - The number of existing values.
 * maxvals - The number of elements in the existing values array.
 */
void
valuearray_add_value_fast(Slapi_Value ***vals,
                          Slapi_Value *addval,
                          int nvals,
                          int *maxvals,
                          int exact,  /* Don't create an array bigger than needed */
                          int passin) /* The values are being passed in */
{
    Slapi_Value *oneval[2];
    oneval[0] = addval;
    oneval[1] = NULL;
    valuearray_add_valuearray_fast(vals, oneval, nvals, 1, maxvals, exact, passin);
}

void
valuearray_add_valuearray_fast(Slapi_Value ***vals,
                               Slapi_Value **addvals,
                               int nvals,
                               int naddvals,
                               int *maxvals,
                               int exact,  /* Don't create an array bigger than needed */
                               int passin) /* The values are being passed in */
{
    int i, j;
    int allocate = 0;
    int need = nvals + naddvals + 1;
    if (exact) {
        /* Create an array exactly the right size. */
        if (need > *maxvals) {
            allocate = need;
        }
    } else {
        if (*maxvals == 0) /* empty; create with 4 by default */
        {
            allocate = 4;
        } else if (need > *maxvals) {
            /* Exponentially expand the array */
            allocate = *maxvals;

            while (allocate < need) {
                allocate *= 2;
            }
        }
    }
    if (allocate > 0) {
        if (*vals == NULL) {
            *vals = (Slapi_Value **)slapi_ch_malloc(allocate * sizeof(Slapi_Value *));
        } else {
            *vals = (Slapi_Value **)slapi_ch_realloc((char *)*vals, allocate * sizeof(Slapi_Value *));
        }
        *maxvals = allocate;
    }
    for (i = 0, j = 0; i < naddvals; i++) {
        if (addvals[i] != NULL) {
            if (passin) {
                /* We consume the values */
                (*vals)[nvals + j] = addvals[i];
            } else {
                /* We copy the values */
                (*vals)[nvals + j] = slapi_value_dup(addvals[i]);
            }
            j++;
        }
    }
    if (*vals) {
        (*vals)[nvals + j] = NULL;
    }
}

void
valuearray_add_value(Slapi_Value ***vals, const Slapi_Value *addval)
{
    Slapi_Value *oneval[2];
    oneval[0] = (Slapi_Value *)addval;
    oneval[1] = NULL;
    valuearray_add_valuearray(vals, oneval, 0);
}

void
valuearray_add_valuearray(Slapi_Value ***vals, Slapi_Value **addvals, PRUint32 flags)
{
    int valslen;
    int addvalslen;
    int maxvals;

    if (vals == NULL) {
        return;
    }
    addvalslen = valuearray_count(addvals);
    if (*vals == NULL) {
        valslen = 0;
        maxvals = 0;
    } else {
        valslen = valuearray_count(*vals);
        maxvals = valslen + 1;
    }
    valuearray_add_valuearray_fast(vals, addvals, valslen, addvalslen, &maxvals, 1 /*Exact*/, flags & SLAPI_VALUE_FLAG_PASSIN);
}

int
valuearray_count(Slapi_Value **va)
{
    int i = 0;
    if (va != NULL) {
        while (NULL != va[i])
            i++;
    }
    return (i);
}

int
valuearray_isempty(Slapi_Value **va)
{
    return va == NULL || va[0] == NULL;
}

/*
 * JCM SLOW FUNCTION
 *
 * WARNING: Use only if you absolutley need to...
 * This function mostly exists to map from the old slapi berval
 * based interface to the new Slapi_Value based interfaces.
 */
int
valuearray_init_bervalarray(struct berval **bvals, Slapi_Value ***cvals)
{
    int n;
    for (n = 0; bvals != NULL && bvals[n] != NULL; n++)
        ;

    if (n == 0) {
        *cvals = NULL;
    } else {
        int i;
        *cvals = (Slapi_Value **)slapi_ch_malloc((n + 1) * sizeof(Slapi_Value *));
        for (i = 0; i < n; i++) {
            (*cvals)[i] = slapi_value_new_berval(bvals[i]);
        }
        (*cvals)[i] = NULL;
    }
    return n;
}

/*
 * JCM SLOW FUNCTION
 *
 * WARNING: Use only if you absolutley need to...
 * This function mostly exists to map from the old slapi berval
 * based interface to the new Slapi_Value based interfaces.
 */
int
valuearray_init_bervalarray_with_flags(struct berval **bvals, Slapi_Value ***cvals, unsigned long flags)
{
    int n;
    for (n = 0; bvals != NULL && bvals[n] != NULL; n++)
        ;
    if (n == 0) {
        *cvals = NULL;
    } else {
        int i;
        *cvals = (Slapi_Value **)slapi_ch_malloc((n + 1) * sizeof(Slapi_Value *));
        for (i = 0; i < n; i++) {
            (*cvals)[i] = slapi_value_new_berval(bvals[i]);
            slapi_value_set_flags((*cvals)[i], flags);
        }
        (*cvals)[i] = NULL;
    }
    return n;
}

/*
 * JCM SLOW FUNCTION
 *
 * Use only if you absolutley need to...
 * This function mostly exists to map from the old slapi berval
 * based interface to the new Slapi_Value based interfaces.
 */
int
valuearray_get_bervalarray(Slapi_Value **cvals, struct berval ***bvals)
{
    int i, n;
    n = valuearray_count(cvals);
    if (0 == n) {
        *bvals = NULL;
    } else {
        *bvals = (struct berval **)slapi_ch_malloc((n + 1) * sizeof(struct berval *));
        for (i = 0; i < n; i++) {
            (*bvals)[i] = ber_bvdup((struct berval *)slapi_value_get_berval(cvals[i]));
        }
        (*bvals)[i] = NULL;
    }
    return (0);
}

void
valuearray_free_ext(Slapi_Value ***va, int idx)
{
    if (va != NULL && *va != NULL) {
        for (; (*va)[idx] != NULL; idx++) {
            slapi_value_free(&(*va)[idx]);
        }
        slapi_ch_free((void **)va);
        *va = NULL;
    }
}

void
valuearray_free(Slapi_Value ***va)
{
    valuearray_free_ext(va, 0);
}

int
valuearray_next_value(Slapi_Value **va, int index, Slapi_Value **v)
{
    int r = -1;
    if (va != NULL && va[0] != NULL) {
        r = index + 1;
        *v = va[r];
        if (*v == NULL) {
            r = -1;
        }
    } else {
        *v = NULL;
    }
    return r;
}

int
valuearray_first_value(Slapi_Value **va, Slapi_Value **v)
{
    return valuearray_next_value(va, -1, v);
}

/*
 * Find the value and return an index number to it.
 */
int
valuearray_find(const Slapi_Attr *a, Slapi_Value **va, const Slapi_Value *v)
{
    int i = 0;
    int found = -1;
    while (found == -1 && va != NULL && va[i] != NULL) {
        if (slapi_value_compare(a, v, va[i]) == 0) {
            found = i;
        } else {
            i++;
        }
    }
    return found;
}

/*
 * Shunt up the array to cover the value to remove.
 */
void
valuearray_remove_value_atindex(Slapi_Value **va, int index)
{
    if (va != NULL && va[0] != NULL) {
        int k;
        for (k = index + 1; va[k] != NULL; k++) {
            va[k - 1] = va[k];
        }
        va[k - 1] = NULL;
    }
}

/*
 * Subtract bvalues from value array
 * return value: subtracted count
 */
int
valuearray_subtract_bvalues(Slapi_Value **va, struct berval **bvals)
{
    Slapi_Value **vap;
    struct berval **bvp;
    int rv = 0;

    if (NULL == va || NULL == *va || NULL == bvals || NULL == *bvals) {
        return rv; /* No op */
    }

    for (vap = va; vap && *vap; vap++) {
        for (bvp = bvals; bvp && *bvp; bvp++) {
            if (0 == slapi_berval_cmp(&(*vap)->bv, *bvp)) {
                Slapi_Value **vapp;
                slapi_value_free(vap);
                for (vapp = vap; vapp && *vapp; vapp++) {
                    *vapp = *(vapp + 1);
                }
                vapp++;
                rv++;
            }
        }
    }

    return rv;
}

/*
 * Find the value in the array,
 * shunt up the array to cover it,
 * return a ptr to the value.
 * The caller is responsible for freeing the value.
 */
Slapi_Value *
valuearray_remove_value(const Slapi_Attr *a, Slapi_Value **va, const Slapi_Value *v)
{
    Slapi_Value *r = NULL;
    int i = 0;
    i = valuearray_find(a, va, v);
    if (i != -1) {
        r = va[i];
        valuearray_remove_value_atindex(va, i);
    }
    return r;
}

size_t
valuearray_size(Slapi_Value **va)
{
    size_t s = 0;
    if (va != NULL && va[0] != NULL) {
        int i;
        for (i = 0; va[i]; i++) {
            s += value_size(va[i]);
        }
        s += (i + 1) * sizeof(Slapi_Value *);
    }
    return s;
}

void
valuearray_update_csn(Slapi_Value **va, CSNType t, const CSN *csn)
{
    int i;
    for (i = 0; va != NULL && va[i]; i++) {
        value_update_csn(va[i], t, csn);
    }
}

/* <=========================== Value Array Fast ==========================> */

void
valuearrayfast_init(struct valuearrayfast *vaf, Slapi_Value **va)
{
    vaf->num = valuearray_count(va);
    vaf->max = vaf->num;
    vaf->va = va;
}

void
valuearrayfast_done(struct valuearrayfast *vaf)
{
    if (vaf->va != NULL) {
        int i;
        for (i = 0; i < vaf->num; i++) {
            slapi_value_free(&vaf->va[i]);
        }
        slapi_ch_free((void **)&vaf->va);
        vaf->num = 0;
        vaf->max = 0;
    }
}

void
valuearrayfast_add_value(struct valuearrayfast *vaf, const Slapi_Value *v)
{
    valuearray_add_value_fast(&vaf->va, (Slapi_Value *)v, vaf->num, &vaf->max, 0 /*Exact*/, 0 /*!PassIn*/);
    vaf->num++;
}

void
valuearrayfast_add_value_passin(struct valuearrayfast *vaf, Slapi_Value *v)
{
    valuearray_add_value_fast(&vaf->va, v, vaf->num, &vaf->max, 0 /*Exact*/, 1 /*PassIn*/);
    vaf->num++;
}

/* <=========================== Value Set =======================> */

#define VALUESET_ARRAY_SORT_THRESHOLD 10
#define VALUESET_ARRAY_MINSIZE 2
#define VALUESET_ARRAY_MAXINCREMENT 4096

Slapi_ValueSet *
slapi_valueset_new()
{
    Slapi_ValueSet *vs = (Slapi_ValueSet *)slapi_ch_calloc(1, sizeof(Slapi_ValueSet));

    if (vs)
        slapi_valueset_init(vs);

    return vs;
}

void
slapi_valueset_init(Slapi_ValueSet *vs)
{
    if (vs != NULL) {
        vs->va = NULL;
        vs->sorted = NULL;
        vs->num = 0;
        vs->max = 0;
    }
}

void
slapi_valueset_done(Slapi_ValueSet *vs)
{
    if (vs != NULL) {
        PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
        if (vs->va != NULL) {
            valuearray_free(&vs->va);
            vs->va = NULL;
        }
        if (vs->sorted != NULL) {
            slapi_ch_free((void **)&vs->sorted);
            vs->sorted = NULL;
        }
        vs->num = 0;
        vs->max = 0;
    }
}

void
slapi_valueset_free(Slapi_ValueSet *vs)
{
    if (vs != NULL) {
        slapi_valueset_done(vs);
        slapi_ch_free((void **)&vs);
    }
}

void
slapi_valueset_set_from_smod(Slapi_ValueSet *vs, Slapi_Mod *smod)
{
    Slapi_Value **va = NULL;
    valuearray_init_bervalarray(slapi_mod_get_ldapmod_byref(smod)->mod_bvalues, &va);
    valueset_set_valuearray_passin(vs, va);
    PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
}

void
valueset_set_valuearray_byval(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
    int i, j = 0;
    slapi_valueset_init(vs);
    vs->num = valuearray_count(addvals);
    vs->max = vs->num + 1;
    vs->va = (Slapi_Value **)slapi_ch_malloc(vs->max * sizeof(Slapi_Value *));
    for (i = 0, j = 0; i < vs->num; i++) {
        if (addvals[i] != NULL) {
            /* We copy the values */
            vs->va[j] = slapi_value_dup(addvals[i]);
            j++;
        }
    }
    vs->va[j] = NULL;
    PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
}

/* WARNING: you must call this function with a new vs - if it points to existing data, it
 * will leak - call slapi_valueset_done to free it first if necessary
 */
void
valueset_set_valuearray_passin(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
    slapi_valueset_init(vs);
    vs->va = addvals;
    vs->num = valuearray_count(addvals);
    vs->max = vs->num + 1;
    PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
}

/* WARNING: you must call this function with a new vs1 - if it points to existing data, it
 * will leak - call slapi_valueset_done(vs1) to free it first if necessary
 */
void
slapi_valueset_set_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2)
{
    slapi_valueset_init(vs1);
    valueset_set_valueset(vs1, vs2);
}

void
slapi_valueset_join_attr_valueset(const Slapi_Attr *a, Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2)
{
    if (slapi_valueset_isempty(vs1)) {
        valueset_set_valueset(vs1, vs2);
    } else {
        slapi_valueset_add_attr_valuearray_ext(a, vs1, vs2->va, vs2->num, 0, NULL);
    }
}

int
slapi_valueset_first_value(Slapi_ValueSet *vs, Slapi_Value **v)
{
    if (NULL == vs) {
        if (v) {
            *v = NULL;
        }
        return 0;
    }
    return valuearray_first_value(vs->va, v);
}

int
slapi_valueset_next_value(Slapi_ValueSet *vs, int index, Slapi_Value **v)
{
    if (NULL == vs) {
        if (v) {
            *v = NULL;
        }
        return index;
    }
    return valuearray_next_value(vs->va, index, v);
}

int
slapi_valueset_count(const Slapi_ValueSet *vs)
{
    if (NULL != vs) {
        return (vs->num);
    }
    return 0;
}

int
slapi_valueset_isempty(const Slapi_ValueSet *vs)
{
    if (NULL != vs) {
        return (vs->num == 0);
    }
    return 1;
}

int
valueset_isempty(const Slapi_ValueSet *vs)
{
    if (NULL == vs) {
        return 1; /* true */
    }
    return valuearray_isempty(vs->va);
}


Slapi_Value *
slapi_valueset_find(const Slapi_Attr *a, const Slapi_ValueSet *vs, const Slapi_Value *v)
{
    Slapi_Value *r = NULL;
    if (vs && (vs->num > 0)) {
        if (vs->sorted) {
            r = valueset_find_sorted(a, vs, v, NULL);
        } else {
            int i = valuearray_find(a, vs->va, v);
            if (i != -1) {
                r = vs->va[i];
            }
        }
    }
    return r;
}

/*
 * The value is found in the set, removed and returned.
 * The caller is responsible for freeing the value.
 *
 * The _sorted function also handles the cleanup of the sorted array
 */
Slapi_Value *
valueset_remove_value_sorted(const Slapi_Attr *a, Slapi_ValueSet *vs, const Slapi_Value *v)
{
    Slapi_Value *r = NULL;
    size_t i = 0;
    size_t position = 0;
    r = valueset_find_sorted(a, vs, v, &position);
    if (r) {
        /* the value was found, remove from valuearray */
        size_t index = vs->sorted[position];
        memmove(&vs->sorted[position], &vs->sorted[position + 1], (vs->num - position) * sizeof(size_t));
        memmove(&vs->va[index], &vs->va[index + 1], (vs->num - index) * sizeof(Slapi_Value *));
        vs->num--;
        /* unfortunately the references in the sorted array
         * to values past the removed one are no longer correct
         * need to adjust */
        for (i = 0; i < vs->num; i++) {
            if (vs->sorted[i] > index)
                vs->sorted[i]--;
        }
        PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
    }
    return r;
}

Slapi_Value *
valueset_remove_value(const Slapi_Attr *a, Slapi_ValueSet *vs, const Slapi_Value *v)
{
    Slapi_Value *r = NULL;
    if (vs->sorted) {
        r = valueset_remove_value_sorted(a, vs, v);
    } else {
        if (!valuearray_isempty(vs->va)) {
            r = valuearray_remove_value(a, vs->va, v);
            if (r) {
                vs->num--;
            }
        }
    }
    PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
    return r;
}

/*
 * Remove any values older than the CSN from valueset.
 */
int
valueset_array_purge(const Slapi_Attr *a, Slapi_ValueSet *vs, const CSN *csn)
{
    size_t i = 0;
    size_t j = 0;
    int nextValue = 0;
    int nv = 0;
    int numValues = 0;
    Slapi_Value **va2 = NULL;
    size_t *sorted2 = NULL;

    /* Loop over all the values freeing the old ones. */
    for(i = 0; i < vs->num; i++)
    {
        /* If we have the sorted array, find the va array ref by it. */
        if (vs->sorted) {
            j = vs->sorted[i];
        } else {
            j = i;
        }
        if (vs->va[j]) {
            csnset_purge(&(vs->va[j]->v_csnset),csn);
            if (vs->va[j]->v_csnset == NULL) {
                slapi_value_free(&vs->va[j]);
                /* Set the removed value to NULL so we know later to skip it */
                vs->va[j] = NULL;
                if (vs->sorted) {
                    /* Mark the value in sorted for removal */
                    vs->sorted[i] = -1;
                }
            } else {
                /* This value survived, we should count it. */
                numValues++;
            }
        }
    }

    /* Compact vs->va and vs->sorted only when there're
     * remaining values ie: numValues is greater than 0 */
    /*
     * Algorithm explination: We start with a pair of arrays - the attrs, and the sorted array that provides
     * a lookup into it:
     *
     * va: [d e a c b] sorted: [2 4 3 0 1]
     *
     * When we remove the element b, we NULL it, and we have to mark the place where it "was" as a -1 to
     * flag it's removal.
     *
     * va: [d e a c NULL] sorted: [2 -1 3 0 1]
     *
     * Now a second va is created with the reduced allocation,
     *
     * va2: [ X X X X ] ....
     *
     * Now we loop over sorted, skipping -1 that we find. In a new counter we create new sorted
     * references, and move the values compacting them in the process.
     * va: [d e a c NULL]
     * va2: [a x x x]
     * sorted: [_0 -1 3 0 1]
     *
     * Looping a few more times would yield:
     *
     * va2: [a c x x]
     * sorted: [_0 _1 3 0 1]
     *
     * va2: [a c d x]
     * sorted: [_0 _1 _2 0 1]
     *
     * va2: [a c d e]
     * sorted: [_0 _1 _2 _3 1]
     *
     * Not only does this sort va, but with sorted, we have a faster lookup, and it will benefit cache
     * lookup.
     *
     */
    if (numValues > 0) {
        if(vs->sorted) {
            /* Let's allocate va2 and sorted2 */
            va2 = (Slapi_Value **) slapi_ch_malloc( (numValues + 1) * sizeof(Slapi_Value *));
            sorted2 = (size_t *) slapi_ch_malloc( (numValues + 1)* sizeof(size_t));
        }

        /* I is the index for the *new* va2 array */
        for(i=0; i<vs->num; i++) {
            if (vs->sorted) {
                /* Skip any removed values from the index */
                while((nv < vs->num) && (-1 == vs->sorted[nv])) {
                    nv++;
                }
                /* We have a remaining value, add it to the va */
                if(nv < vs->num) {
                    va2[i] = vs->va[vs->sorted[nv]];
                    sorted2[i] = i;
                    nv++;
                }
            } else {
                while((nextValue < vs->num) && (NULL == vs->va[nextValue])) {
                    nextValue++;
                }

                if(nextValue < vs->num) {
                    vs->va[i] = vs->va[nextValue];
                    nextValue++;
                } else {
                    break;
                }
            }
        }

        if (vs->sorted) {
            /* Finally replace the valuearray and adjust num, max */
            slapi_ch_free((void **)&vs->va);
            slapi_ch_free((void **)&vs->sorted);
            vs->va = va2;
            vs->sorted = sorted2;
            vs->num = numValues;
            vs->max = vs->num + 1;
        } else {
            vs->num = numValues;
        }

        for (j = vs->num; j < vs->max; j++) {
            vs->va[j] = NULL;
            if (vs->sorted) {
                vs->sorted[j] = -1;
            }
        }
    } else {
        /* empty valueset - reset the vs->num so that further
         * checking will not abort
         */
        vs->num = 0;
        slapi_valueset_done(vs);
    }

    /* We still have values but not sorted array! rebuild it */
    if(vs->num > VALUESET_ARRAY_SORT_THRESHOLD && vs->sorted == NULL) {
        vs->sorted = (size_t *) slapi_ch_malloc( vs->max* sizeof(size_t));
        valueset_array_to_sorted(a, vs);
    }
#ifdef DEBUG
    PR_ASSERT(vs->num == 0 || (vs->num > 0 && vs->va[0] != NULL));
    size_t index = 0;
    for (; index < vs->num; index++) {
        PR_ASSERT(vs->va[index] != NULL);
    }
    for (; index < vs->max; index++) {
        PR_ASSERT(vs->va[index] == NULL);
    }
#endif
    /* return the number of remaining values */
    return numValues;
}

/*
 * Remove any values older than the CSN.
 */
int
valueset_purge(const Slapi_Attr *a, Slapi_ValueSet *vs, const CSN *csn)
{
    int r = 0;
    if (!valuearray_isempty(vs->va)) {
        r = valueset_array_purge(a, vs, csn);
        vs->num = r;
        PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
    }
    return 0;
}

Slapi_Value **
valueset_get_valuearray(const Slapi_ValueSet *vs)
{
    return (Slapi_Value **)vs->va;
}

size_t
valueset_size(const Slapi_ValueSet *vs)
{
    size_t s = 0;
    if (vs && !valuearray_isempty(vs->va)) {
        s = valuearray_size(vs->va);
    }
    return s;
}

/*
 * The value array is passed in by value.
 */
void
slapi_valueset_add_valuearray(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value **addvals)
{
    if (!valuearray_isempty(addvals)) {
        slapi_valueset_add_attr_valuearray_ext(a, vs, addvals, valuearray_count(addvals), 0, NULL);
    }
}

void
valueset_add_valuearray(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
    if (!valuearray_isempty(addvals)) {
        slapi_valueset_add_attr_valuearray_ext(NULL, vs, addvals, valuearray_count(addvals), 0, NULL);
    }
}

/*
 * The value is passed in by value.
 */
void
slapi_valueset_add_value(Slapi_ValueSet *vs, const Slapi_Value *addval)
{
    slapi_valueset_add_value_ext(vs, addval, 0);
}

void
slapi_valueset_add_value_ext(Slapi_ValueSet *vs, const Slapi_Value *addval, unsigned long flags)
{
    // Check if both SLAPI_VALUE_FLAG_DUPCHECK and SLAPI_VALUE_FLAG_PASSIN flags are used together
    if ((flags & SLAPI_VALUE_FLAG_DUPCHECK) && (flags & SLAPI_VALUE_FLAG_PASSIN)) {
        slapi_log_err(SLAPI_LOG_WARNING, "slapi_valueset_add_value_ext",
            "The combination of SLAPI_VALUE_FLAG_DUPCHECK and SLAPI_VALUE_FLAG_PASSIN flags is not recommended. "
            "Using this combination could result in undefined behavior related to memory management. "
            "If you need both of the flags, please, use slapi_valueset_add_attr_value_ext function instead "
            "and ensure proper cleanup if there's an error.\n");
    }
    Slapi_Value *oneval[2];
    oneval[0] = (Slapi_Value *)addval;
    oneval[1] = NULL;
    slapi_valueset_add_attr_valuearray_ext(NULL, vs, oneval, 1, flags, NULL);
}


/* find value v in the sorted array of values, using syntax of attribut a for comparison
 *
 */
static int
valueset_value_syntax_cmp(const Slapi_Attr *a, const Slapi_Value *v1, const Slapi_Value *v2)
{
    /* this looks like a huge overhead, but there are no simple functions to normalize and
     * compare available
     */
    const Slapi_Value *oneval[3];
    Slapi_Value **keyvals;
    int rc = -1;

    keyvals = NULL;
    oneval[0] = v1;
    oneval[1] = v2;
    oneval[2] = NULL;
    if (slapi_attr_values2keys_sv(a, (Slapi_Value **)oneval, &keyvals, LDAP_FILTER_EQUALITY) != 0 || keyvals == NULL || keyvals[0] == NULL || keyvals[1] == NULL) {
        /* this should never happen since always a syntax plugin to
         * generate the keys will be found (there exists a default plugin)
         * log an error and continue.
         */
        slapi_log_err(SLAPI_LOG_ERR, "valueset_value_syntax_cmp",
                      "slapi_attr_values2keys_sv failed for type %s\n",
                      a->a_type);
    } else {
        const struct berval *bv1, *bv2;
        bv1 = &keyvals[0]->bv;
        bv2 = &keyvals[1]->bv;
        rc = slapi_berval_cmp(bv1, bv2);
    }
    if (keyvals != NULL)
        valuearray_free(&keyvals);
    return (rc);
}
static int
valueset_value_cmp(const Slapi_Attr *a, const Slapi_Value *v1, const Slapi_Value *v2)
{

    if (a == NULL || slapi_attr_is_dn_syntax_attr((Slapi_Attr *)a)) {
        /* if no attr is provided just do a utf8compare */
        /* for all the values the first step of normalization is done,
         * case folding still needs to be done
         */
        /* would this be enough ?: return (strcasecmp(v1->bv.bv_val, v2->bv.bv_val)); */
        return (slapi_utf8casecmp((unsigned char *)v1->bv.bv_val, (unsigned char *)v2->bv.bv_val));
    } else {
        /* slapi_value_compare doesn't work, it only returns 0 or -1
        return (slapi_value_compare(a, v1, v2));
        * use special compare, base on what valuetree_find did
        */
        return (valueset_value_syntax_cmp(a, v1, v2));
    }
}
/* find a value in the sorted valuearray.
 * If the value is found the pointer to the value is returned and if index is provided
 * it will return the index of the value in the valuearray
 * If the value is not found, index will contain the place where the value would be inserted
 */
Slapi_Value *
valueset_find_sorted(const Slapi_Attr *a, const Slapi_ValueSet *vs, const Slapi_Value *v, size_t *index)
{
    int cmp = -1;
    int bot = -1;
    int top;

    if (vs->num == 0) {
        /* empty valueset */
        if (index)
            *index = 0;
        return (NULL);
    } else {
        top = vs->num;
    }
    while (top - bot > 1) {
        int mid = (top + bot) / 2;
        if ((cmp = valueset_value_cmp(a, v, vs->va[vs->sorted[mid]])) > 0) {
            bot = mid;
        } else {
            top = mid;
        }
    }
    if (index)
        *index = top;
    /* check if the value is found */
    if (top < vs->num && (0 == valueset_value_cmp(a, v, vs->va[vs->sorted[top]])))
        return (vs->va[vs->sorted[top]]);
    else
        return (NULL);
}


void
valueset_array_to_sorted(const Slapi_Attr *a, Slapi_ValueSet *vs)
{
    size_t i;

    /* initialize sort array with indcies */
    for (i = 0; i < vs->max; i++) {
        vs->sorted[i] = i;
    }

    /* This is the index boundaries of the array.
     * We only need to sort if we have 2 or more elements.
     */
    if (vs->num >= 2) {
        valueset_array_to_sorted_quick(a, vs, 0, vs->num - 1);
    }

    PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
}

void
valueset_array_to_sorted_quick(const Slapi_Attr *a, Slapi_ValueSet *vs, size_t low, size_t high)
{
    if (low >= high) {
        return;
    }

    /* Hoare quicksort */

    size_t pivot = vs->sorted[low];
    size_t i = low - 1;
    size_t j = high + 1;

    /* This is the partition step */
    while (1) {
        do {
            i++;
        } while (i < vs->max && valueset_value_cmp(a, vs->va[vs->sorted[i]], vs->va[pivot]) < 0);

        do {
            j--;
        } while (valueset_value_cmp(a, vs->va[vs->sorted[j]], vs->va[pivot]) > 0 && j > 0);

        if (i >= j) {
            break;
        }

        valueset_swap_values(&(vs->sorted[i]), &(vs->sorted[j]));
    }

    valueset_array_to_sorted_quick(a, vs, low, j);
    valueset_array_to_sorted_quick(a, vs, j + 1, high);
}

void
valueset_swap_values(size_t *a, size_t *b)
{
    size_t t = *a;
    *a = *b;
    *b = t;
}

/* insert a value into a sorted array, if dupcheck is set no duplicate values will be accepted
 * (is there a reason to allow duplicates ? LK
 * (OLD) if the value is inserted the the function returns the index where it was inserted
 * (NEW) If the value is inserted, we return 0. No one checks the return, so don't bother.
 * if the value already exists -index is returned to indicate anerror an the index of the existing value
 */
int
valueset_insert_value_to_sorted(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value *vi, int dupcheck)
{
    size_t index = 0;
    Slapi_Value *v;
    /* test for pre sorted array and to avoid boundary condition */
    if (vs->num == 0) {
        vs->sorted[0] = 0;
        vs->num++;
        PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
        return (0);
    } else if (valueset_value_cmp(a, vi, vs->va[vs->sorted[vs->num - 1]]) > 0) {
        vs->sorted[vs->num] = vs->num;
        vs->num++;
        PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
        return (0);
    }
    v = valueset_find_sorted(a, vs, vi, &index);
    if (v && dupcheck) {
        /* value already exists, do not insert duplicates */
        return (-1);
    } else {
        memmove(&vs->sorted[index + 1], &vs->sorted[index], (vs->num - index) * sizeof(size_t));
        vs->sorted[index] = vs->num;
        vs->num++;
        PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
        return (0);
    }
}

/*
 * If this function returns an error, it is safe to do both
 * slapi_valueset_done(vs);
 * and
 * valuearray_free(&addvals);
 * if there is an error and the PASSIN flag is used, the addvals array will own all of the values
 * vs will own none of the values - so you should do both slapi_valueset_done(vs) and valuearray_free(&addvals)
 * to clean up
 */
int
slapi_valueset_add_attr_valuearray_ext(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value **addvals, int naddvals, unsigned long flags, int *dup_index)
{
    int rc = LDAP_SUCCESS;
    int dup;
    size_t allocate = 0;
    size_t need = 0;
    int passin = flags & SLAPI_VALUE_FLAG_PASSIN;
    int dupcheck = flags & SLAPI_VALUE_FLAG_DUPCHECK;

    if (naddvals == 0) {
        return (rc);
    }

    need = vs->num + naddvals + 1;
    if (need > vs->max) {
        /* Expand the array */
        allocate = vs->max;
        if (allocate == 0) { /* initial allocation */
            allocate = VALUESET_ARRAY_MINSIZE;
        }
        while (allocate < need) {
            if (allocate > VALUESET_ARRAY_MAXINCREMENT) {
                /* do not grow exponentially */
                allocate += VALUESET_ARRAY_MAXINCREMENT;
            } else {
                allocate *= 2;
            }
        }
    }
    if (allocate > 0) {
        if (vs->va == NULL) {
            vs->va = (Slapi_Value **)slapi_ch_malloc(allocate * sizeof(Slapi_Value *));
        } else {
            vs->va = (Slapi_Value **)slapi_ch_realloc((char *)vs->va, allocate * sizeof(Slapi_Value *));
            if (vs->sorted) {
                vs->sorted = (size_t *)slapi_ch_realloc((char *)vs->sorted, allocate * sizeof(size_t));
            }
        }
        vs->max = allocate;
    }

    if ((vs->num + naddvals > VALUESET_ARRAY_SORT_THRESHOLD || dupcheck) && !vs->sorted && vs->max > 0) {
        /* initialize sort array and do initial sort */
        vs->sorted = (size_t *)slapi_ch_malloc(vs->max * sizeof(size_t));
        valueset_array_to_sorted(a, vs);
    }

    for (size_t i = 0; i < naddvals; i++) {
        if (addvals[i] != NULL && vs->va) {
            if (passin) {
                /* We consume the values */
                (vs->va)[vs->num] = addvals[i];
            } else {
                /* We copy the values */
                (vs->va)[vs->num] = slapi_value_dup(addvals[i]);
            }
            if (vs->sorted) {
                dup = valueset_insert_value_to_sorted(a, vs, (vs->va)[vs->num], dupcheck);
                if (dup < 0) {
                    rc = LDAP_TYPE_OR_VALUE_EXISTS;
                    if (dup_index)
                        *dup_index = i;
                    if (passin) {
                        PR_ASSERT((i == 0) || dup_index);
                        /* caller must provide dup_index to know how far we got in addvals */
                        (vs->va)[vs->num] = NULL;
                    } else {
                        slapi_value_free(&(vs->va)[vs->num]);
                    }
                    break;
                }
            } else {
                vs->num++;
            }
        }
    }
    if (vs->va){
        (vs->va)[vs->num] = NULL;
    }

    PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
    return (rc);
}

int
slapi_valueset_add_attr_value_ext(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value *addval, unsigned long flags)
{

    Slapi_Value *oneval[2];
    int rc;
    oneval[0] = (Slapi_Value *)addval;
    oneval[1] = NULL;
    rc = slapi_valueset_add_attr_valuearray_ext(a, vs, oneval, 1, flags, NULL);
    return (rc);
}

/*
 * The string is passed in by value.
 */
void
valueset_add_string(const Slapi_Attr *a, Slapi_ValueSet *vs, const char *s, CSNType t, const CSN *csn)
{
    Slapi_Value v;
    value_init(&v, NULL, t, csn);
    slapi_value_set_string(&v, s);
    slapi_valueset_add_attr_value_ext(a, vs, &v, 0);
    value_done(&v);
}

/*
 * The value set is passed in by value.
 */
void
valueset_set_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2)
{
    size_t i;

    if (vs1 && vs2) {
        int oldmax = vs1->max;
        /* pre-condition - vs1 empty - otherwise, existing data is overwritten */
        PR_ASSERT(vs1->num == 0);

        if (vs2->va) {
            /* need to copy valuearray */
            if (vs2->max == 0) {
                /* temporary hack, not all valuesets were created properly. fix it now */
                vs1->num = valuearray_count(vs2->va);
                vs1->max = vs1->num + 1;
            } else {
                vs1->num = vs2->num;
                vs1->max = vs2->max;
            }
            /* do we need more room? */
            if ((NULL == vs1->va) || (oldmax < vs1->max)) {
                vs1->va = (Slapi_Value **)slapi_ch_realloc((char *)vs1->va, vs1->max * sizeof(Slapi_Value *));
            }
            for (i = 0; i < vs1->num; i++) {
                vs1->va[i] = slapi_value_dup(vs2->va[i]);
            }
            vs1->va[vs1->num] = NULL;
        } else {
            valuearray_free(&vs1->va);
        }
        if (vs2->sorted) {
            if ((NULL == vs1->sorted) || (oldmax < vs1->max)) {
                vs1->sorted = (size_t *)slapi_ch_realloc((char *)vs1->sorted, vs1->max * sizeof(size_t));
            }
            memcpy(&vs1->sorted[0], &vs2->sorted[0], vs1->num * sizeof(size_t));
        } else {
            slapi_ch_free((void **)&vs1->sorted);
        }
        /* post-condition */
        PR_ASSERT((vs1->sorted == NULL) || (vs1->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs1->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs1->sorted[0] < vs1->num)));
    }
}

void
valueset_remove_string(const Slapi_Attr *a, Slapi_ValueSet *vs, const char *s)
{
    Slapi_Value v;
    Slapi_Value *removed;
    value_init(&v, NULL, CSN_TYPE_NONE, NULL);
    slapi_value_set_string(&v, s);
    removed = valueset_remove_value(a, vs, &v);
    if (removed) {
        slapi_value_free(&removed);
    }
    value_done(&v);
}

void
valueset_update_csn(Slapi_ValueSet *vs, CSNType t, const CSN *csn)
{
    if (!valuearray_isempty(vs->va)) {
        valuearray_update_csn(vs->va, t, csn);
    }
}

/*
 * Remove an array of values from a value set.
 * The removed values are passed back in an array.
 *
 * Flags
 *  SLAPI_VALUE_FLAG_PRESERVECSNSET - csnset in the value set is duplicated and
 *                                    preserved in the matched element of the
 *                                    array of values.
 *  SLAPI_VALUE_FLAG_IGNOREERROR - ignore an error: Couldn't find the value to
 *                                 be deleted.
 *  SLAPI_VALUE_FLAG_USENEWVALUE - replace the value between the value set and
 *                                 the matched element of the array of values
 *                                 (used by entry_add_present_values_wsi).
 *
 * Returns
 *  LDAP_SUCCESS - OK.
 *  LDAP_NO_SUCH_ATTRIBUTE - A value to be deleted was not in the value set.
 *  LDAP_OPERATIONS_ERROR - Something very bad happened.
 */
int
valueset_remove_valuearray(Slapi_ValueSet *vs, const Slapi_Attr *a, Slapi_Value **valuestodelete, int flags, Slapi_Value ***va_out)
{
    int rc = LDAP_SUCCESS;
    if (vs->num > 0) {
        int i;
        struct valuearrayfast vaf_out;

        if (va_out) {
            valuearrayfast_init(&vaf_out, *va_out);
        }

        /*
         * For larger valuesets the valuarray is sorted, values can be deleted individually
         *
         */
        for (i = 0; rc == LDAP_SUCCESS && valuestodelete[i] != NULL; ++i) {
            Slapi_Value *found = valueset_remove_value(a, vs, valuestodelete[i]);
            if (found != NULL) {
                if (va_out) {
                    if (found->v_csnset &&
                        (flags & (SLAPI_VALUE_FLAG_PRESERVECSNSET |
                                  SLAPI_VALUE_FLAG_USENEWVALUE))) {
                        valuestodelete[i]->v_csnset = csnset_dup(found->v_csnset);
                    }
                    if (flags & SLAPI_VALUE_FLAG_USENEWVALUE) {
                        valuearrayfast_add_value_passin(&vaf_out, valuestodelete[i]);
                        valuestodelete[i] = found;
                    } else {
                        valuearrayfast_add_value_passin(&vaf_out, found);
                    }
                } else {
                    if (flags & SLAPI_VALUE_FLAG_PRESERVECSNSET) {
                        valuestodelete[i]->v_csnset = found->v_csnset;
                        found->v_csnset = NULL;
                    }
                    slapi_value_free(&found);
                }
            } else {
                if ((flags & SLAPI_VALUE_FLAG_IGNOREERROR) == 0) {
                    slapi_log_err(SLAPI_LOG_ARGS, "valueset_remove_valuearray",
                                  "Could not find value %d for attr %s\n", i - 1, a->a_type);
                    rc = LDAP_NO_SUCH_ATTRIBUTE;
                }
            }
        }
        if (va_out) {
            *va_out = vaf_out.va;
            if (rc != LDAP_SUCCESS) {
                valuearray_free(va_out);
            }
        }
    }
    return rc;
}

Slapi_ValueSet *
valueset_dup(const Slapi_ValueSet *dupee)
{
    Slapi_ValueSet *duped = slapi_valueset_new();
    if (NULL != duped) {
        valueset_set_valuearray_byval(duped, dupee->va);
    }
    return duped;
}

/* quickly throw away any old contents of this valueset, and stick in the
 * new ones.
 *
 * return value: LDAP_SUCCESS - OK
 *             : LDAP_OPERATIONS_ERROR - duplicated values given
 */
int
valueset_replace_valuearray(Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value **valstoreplace)
{
    return (valueset_replace_valuearray_ext(a, vs, valstoreplace, 1));
}

int
valueset_replace_valuearray_ext(Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value **valstoreplace, int dupcheck)
{
    int rc = LDAP_SUCCESS;
    int vals_count = valuearray_count(valstoreplace);

    if (vals_count == 0) {
        /* no new values, just clear the valueset */
        slapi_valueset_done(vs);
    } else if (vals_count == 1 || !dupcheck) {
        /* just repelace the valuearray and adjus num, max */
        slapi_valueset_done(vs);
        vs->va = valstoreplace;
        vs->num = vals_count;
        vs->max = vals_count + 1;
        PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
    } else {
        /* verify the given values are not duplicated.  */
        unsigned long flags = SLAPI_VALUE_FLAG_PASSIN | SLAPI_VALUE_FLAG_DUPCHECK;
        int dupindex = 0;
        Slapi_ValueSet *vs_new = slapi_valueset_new();
        rc = slapi_valueset_add_attr_valuearray_ext(a, vs_new, valstoreplace, vals_count, flags, &dupindex);

        if (rc == LDAP_SUCCESS) {
            /* used passin, so vs_new owns all of the Slapi_Value* in valstoreplace
             * so tell valuearray_free_ext to start at index vals_count, which is
             * NULL, then just free valstoreplace
             */
            valuearray_free_ext(&valstoreplace, vals_count);
            /* values look good - replace the values in the attribute */
            if (!valuearray_isempty(vs->va)) {
                /* remove old values */
                slapi_valueset_done(vs);
            }
            vs->va = vs_new->va;
            vs_new->va = NULL;
            vs->sorted = vs_new->sorted;
            vs_new->sorted = NULL;
            vs->num = vs_new->num;
            vs->max = vs_new->max;
            slapi_valueset_free(vs_new);
            PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
        } else {
            /* caller expects us to own valstoreplace - since we cannot
               use them, just delete them */
            /* using PASSIN, some of the Slapi_Value* are in vs_new, and the rest
             * after dupindex are in valstoreplace
             */
            slapi_valueset_free(vs_new);
            valuearray_free_ext(&valstoreplace, dupindex);
            PR_ASSERT((vs->sorted == NULL) || (vs->num < VALUESET_ARRAY_SORT_THRESHOLD) || ((vs->num >= VALUESET_ARRAY_SORT_THRESHOLD) && (vs->sorted[0] < vs->num)));
        }
    }
    return rc;
}

/*
 * Search the value set for each value to be update,
 * and update the value with the CSN provided.
 * Updated values are moved from the valuestoupdate
 * array to the valueupdated array.
 */
void
valueset_update_csn_for_valuearray(Slapi_ValueSet *vs, const Slapi_Attr *a, Slapi_Value **valuestoupdate, CSNType t, const CSN *csn, Slapi_Value ***valuesupdated)
{
    valueset_update_csn_for_valuearray_ext(vs, a, valuestoupdate, t, csn, valuesupdated, 0);
}
void
valueset_update_csn_for_valuearray_ext(Slapi_ValueSet *vs, const Slapi_Attr *a, Slapi_Value **valuestoupdate, CSNType t, const CSN *csn, Slapi_Value ***valuesupdated, int csnref_updated)
{
    if (!valuearray_isempty(valuestoupdate) &&
        !valuearray_isempty(vs->va)) {
        struct valuearrayfast vaf_valuesupdated;
        valuearrayfast_init(&vaf_valuesupdated, *valuesupdated);
        int i;
        int del_index = -1, del_count = 0;
        for (i = 0; valuestoupdate[i] != NULL; ++i) {
            Slapi_Value *v = slapi_valueset_find(a, vs, valuestoupdate[i]);
            if (v) {
                value_update_csn(v, t, csn);
                if (csnref_updated) {
                    csnset_free(&valuestoupdate[i]->v_csnset);
                    valuestoupdate[i]->v_csnset = csnset_dup(value_get_csnset(v));
                }
                valuearrayfast_add_value_passin(&vaf_valuesupdated, valuestoupdate[i]);
                valuestoupdate[i] = NULL;
                del_count++;
                if (del_index < 0)
                    del_index = i;
            } else { /* keep the value in valuestoupdate, to keep array compressed, move to first free slot*/
                if (del_index >= 0) {
                    valuestoupdate[del_index] = valuestoupdate[i];
                    del_index++;
                }
            }
        }
        /* complete compression */
        for (i = 0; i < del_count; i++)
            valuestoupdate[del_index + i] = NULL;

        *valuesupdated = vaf_valuesupdated.va;
    }
}

int
valuearray_dn_normalize_value(Slapi_Value **vals)
{
    int rc = 0;
    Slapi_Value **vp = NULL;

    for (vp = vals; vp && *vp; vp++) {
        rc |= value_dn_normalize_value(*vp);
    }

    return rc;
}
