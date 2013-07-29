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
    struct berval	***vals,
    const struct berval	*addval,
    int			nvals,
    int			*maxvals
)
{
    int need = nvals + 2;
	if(need>*maxvals)
	{
	    if (*maxvals==0)
	    {
			*maxvals = 2;
	    }
	    while ( *maxvals < need )
	    {
			*maxvals *= 2;
	    }
		if(*vals==NULL)
		{
			*vals = (struct berval **) slapi_ch_malloc( *maxvals * sizeof(struct berval *));
		}
		else
		{
			*vals = (struct berval **) slapi_ch_realloc( (char *) *vals, *maxvals * sizeof(struct berval *));
		}
	}
	(*vals)[nvals] = ber_bvdup( (struct berval *)addval );
	(*vals)[nvals+1] = NULL;
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
    oneval[0]= addval;
    oneval[1]= NULL;
    valuearray_add_valuearray_fast(vals,oneval,nvals,1,maxvals,exact,passin);
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
	int allocate= 0;
    int need = nvals + naddvals + 1;
	if(exact)
	{
		/* Create an array exactly the right size. */
		if(need>*maxvals)
		{
			allocate= need;
		}
	}
	else
	{
	    if (*maxvals==0) /* empty; create with 4 by default */
	    {
			allocate= 4;
	    }
		else if (need > *maxvals)
		{
			/* Exponentially expand the array */
			allocate= *maxvals;

			while ( allocate < need )
			{
				allocate *= 2;
			}
		}
	}
	if(allocate>0)
	{
		if(*vals==NULL)
		{
			*vals = (Slapi_Value **) slapi_ch_malloc( allocate * sizeof(Slapi_Value *));
		}
		else
		{
			*vals = (Slapi_Value **) slapi_ch_realloc( (char *) *vals, allocate * sizeof(Slapi_Value *));
		}
		*maxvals= allocate;
	}
    for ( i = 0, j = 0; i < naddvals; i++)
    {
		if ( addvals[i]!=NULL )
		{
			if(passin)
			{
				/* We consume the values */
			    (*vals)[nvals + j] = addvals[i];
			}
			else
			{
				/* We copy the values */
			    (*vals)[nvals + j] = slapi_value_dup(addvals[i]);
			}
			j++;
		}
    }
    (*vals)[nvals + j] = NULL;
}

void
valuearray_add_value(Slapi_Value ***vals, const Slapi_Value *addval)
{
    Slapi_Value *oneval[2];
    oneval[0]= (Slapi_Value*)addval;
    oneval[1]= NULL;
    valuearray_add_valuearray(vals,oneval,0);
}

void
valuearray_add_valuearray(Slapi_Value ***vals, Slapi_Value **addvals, PRUint32 flags)
{
    int valslen;
    int addvalslen;
    int maxvals;

    if(vals == NULL){
        return;
    }
    addvalslen= valuearray_count(addvals);
    if(*vals == NULL)
    {
        valslen= 0;
        maxvals= 0;
    }
    else
    {
        valslen= valuearray_count(*vals);
        maxvals= valslen+1;
    }
    valuearray_add_valuearray_fast(vals,addvals,valslen,addvalslen,&maxvals,1/*Exact*/,flags & SLAPI_VALUE_FLAG_PASSIN);
}

int
valuearray_count( Slapi_Value **va)
{
    int i=0;
	if(va!=NULL)
	{
	    while(NULL != va[i]) i++;
	}
    return(i);
}

int
valuearray_isempty( Slapi_Value **va)
{
    return va==NULL || va[0]==NULL;
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
    for(n=0; bvals != NULL && bvals[n] != NULL; n++);
	if(n==0)
	{
	    *cvals = NULL;
	}
	else
	{
		int i;
	    *cvals = (Slapi_Value **) slapi_ch_malloc((n + 1) * sizeof(Slapi_Value *));
	    for(i=0;i<n;i++)
	    {
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
    for(n=0; bvals != NULL && bvals[n] != NULL; n++);
    if(n==0)
    {
        *cvals = NULL;
    }
    else
    {
        int i;
        *cvals = (Slapi_Value **) slapi_ch_malloc((n + 1) * sizeof(Slapi_Value *));
        for(i=0;i<n;i++)
        {
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
valuearray_get_bervalarray(Slapi_Value **cvals,struct berval ***bvals)
{
    int i,n;
	n= valuearray_count(cvals);
	if (0 == n)
	{
    	*bvals = NULL;
	}
	else
	{
    	*bvals = (struct berval **)slapi_ch_malloc((n + 1) * sizeof(struct berval *));
    	for(i=0;i<n;i++)
    	{
			(*bvals)[i] = ber_bvdup((struct berval *)slapi_value_get_berval(cvals[i]));
    	}
    	(*bvals)[i] = NULL;
	}
    return(0);
}

void
valuearray_free_ext(Slapi_Value ***va, int idx)
{
	if(va!=NULL && *va!=NULL)
	{
		for(; (*va)[idx]!=NULL; idx++)
		{
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
valuearray_next_value( Slapi_Value **va, int index, Slapi_Value **v)
{
	int r= -1;
	if(va!=NULL && va[0]!=NULL)
	{
		r= index+1;
		*v= va[r];
		if(*v==NULL)
		{
			r= -1;
		}
	}
	else
	{
		*v= NULL;
	}
    return r;
}

int
valuearray_first_value( Slapi_Value **va, Slapi_Value **v )
{
	return valuearray_next_value( va, -1, v);
}

/*
 * Find the value and return an index number to it.
 */
int
valuearray_find(const Slapi_Attr *a, Slapi_Value **va, const Slapi_Value *v)
{
	int i= 0;
	int found= -1;
	while(found==-1 && va!=NULL && va[i]!=NULL)
	{
		if(slapi_value_compare( a, v, va[i])==0)
		{
			found= i;
		}
		else
		{
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
	if(va!=NULL && va[0]!=NULL)
	{
		int k;
		for ( k = index + 1; va[k] != NULL; k++ )
		{
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
	Slapi_Value *r= NULL;
	int i= 0;
	i= valuearray_find(a, va, v);
	if(i!=-1)
	{
		r= va[i];
		valuearray_remove_value_atindex(va,i);
	}
	return r;
}

/* 
 * Remove any values older than the CSN.
 */
int 
valuearray_purge(Slapi_Value ***va, const CSN *csn)
{
	int numValues=0;
	int i=0;
	int nextValue=0;

	PR_ASSERT(va!=NULL && *va!=NULL);

	/* Loop over all the values freeing the old ones. */
	for(i=0; (*va)[i]; i++)
	{
		csnset_purge(&((*va)[i]->v_csnset),csn);
		if ((*va)[i]->v_csnset == NULL)
		{
			slapi_value_free(&(*va)[i]);
			(*va)[i] = NULL;
		}
	}
	/* Now compact the value list. */
	numValues=i;
	nextValue = 0;
	i = 0;
	for(i=0;i<numValues;i++)
	{
		while((nextValue < numValues) && (NULL == (*va)[nextValue]))
		{
			nextValue++;
		}
		if(nextValue < numValues)
		{
			(*va)[i] = (*va)[nextValue];
			nextValue++;
		}
		else
		{
			break;
		}
	}
	(*va)[i] = NULL;

	/* All the values were deleted, we can discard the whole array. */
	if(NULL == (*va)[0])
	{
		slapi_ch_free((void**)va);
		*va= NULL;
	}
	/* return the number of remaining values */
	return(i);
}

size_t
valuearray_size(Slapi_Value **va)
{
	size_t s= 0;
	if(va!=NULL && va[0]!=NULL)
	{
		int i;
	    for (i = 0; va[i]; i++)
		{
			s += value_size(va[i]);
		}
            s += (i + 1) * sizeof(Slapi_Value*);
	}
	return s;
}

void
valuearray_update_csn(Slapi_Value **va, CSNType t, const CSN *csn)
{
	int i;
    for (i = 0; va!=NULL && va[i]; i++)
	{
		value_update_csn(va[i],t,csn);
	}
}

/* <=========================== Value Array Fast ==========================> */

void
valuearrayfast_init(struct valuearrayfast *vaf,Slapi_Value **va)
{
	vaf->num= valuearray_count(va);
	vaf->max= vaf->num;
	vaf->va= va;
}

void
valuearrayfast_done(struct valuearrayfast *vaf)
{
	if(vaf->va!=NULL)
	{
		int i;
		for(i=0; i<vaf->num; i++)
		{
			slapi_value_free(&vaf->va[i]);
		}
		slapi_ch_free((void **)&vaf->va);
		vaf->num= 0;
		vaf->max= 0;
	}
}

void
valuearrayfast_add_value(struct valuearrayfast *vaf,const Slapi_Value *v)
{
	valuearray_add_value_fast(&vaf->va,(Slapi_Value *)v,vaf->num,&vaf->max,0/*Exact*/,0/*!PassIn*/);
	vaf->num++;
}

void
valuearrayfast_add_value_passin(struct valuearrayfast *vaf,Slapi_Value *v)
{
	valuearray_add_value_fast(&vaf->va,v,vaf->num,&vaf->max,0/*Exact*/,1/*PassIn*/);
	vaf->num++;
}


/* <=========================== Value Set =======================> */

#define VALUESET_ARRAY_SORT_THRESHOLD 10
#define VALUESET_ARRAY_MINSIZE 2
#define VALUESET_ARRAY_MAXINCREMENT 4096

Slapi_ValueSet *
slapi_valueset_new()
{
	Slapi_ValueSet *vs = (Slapi_ValueSet *)slapi_ch_calloc(1,sizeof(Slapi_ValueSet));

	if(vs)
		slapi_valueset_init(vs);

	return vs;
}

void
slapi_valueset_init(Slapi_ValueSet *vs)
{
	if(vs!=NULL)
	{
		vs->va= NULL;
		vs->sorted = NULL;
		vs->num = 0;
		vs->max = 0;
	}
}

void
slapi_valueset_done(Slapi_ValueSet *vs)
{
	if(vs!=NULL)
	{
		PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
		if(vs->va!=NULL)
		{
			valuearray_free(&vs->va);
			vs->va= NULL;
		}
		if (vs->sorted != NULL) 
		{
			slapi_ch_free ((void **)&vs->sorted);
			vs->sorted = NULL;
		}
		vs->num = 0;
		vs->max = 0;
	}
}

void
slapi_valueset_free(Slapi_ValueSet *vs)
{
	if(vs!=NULL)
	{
		slapi_valueset_done(vs);
		slapi_ch_free((void **)&vs);
	}
}

void
slapi_valueset_set_from_smod(Slapi_ValueSet *vs, Slapi_Mod *smod)
{
	Slapi_Value **va= NULL;
	valuearray_init_bervalarray(slapi_mod_get_ldapmod_byref(smod)->mod_bvalues, &va);
	valueset_set_valuearray_passin(vs, va);
	PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
}

void
valueset_set_valuearray_byval(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
	int i, j=0;
	slapi_valueset_init(vs);
	vs->num = valuearray_count(addvals);
	vs->max = vs->num + 1;
	vs->va = (Slapi_Value **) slapi_ch_malloc( vs->max * sizeof(Slapi_Value *));
	for ( i = 0, j = 0; i < vs->num; i++)
	{
		if ( addvals[i]!=NULL )
		{
			/* We copy the values */
			vs->va[j] = slapi_value_dup(addvals[i]);
			j++;
		}
	}
	vs->va[j] = NULL;
	PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
}

void
valueset_set_valuearray_passin(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
	slapi_valueset_init(vs);
	vs->va= addvals;
	vs->num = valuearray_count(addvals);
	vs->max = vs->num + 1;
	PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
}

void
slapi_valueset_set_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2)
{
	slapi_valueset_init(vs1);
	valueset_add_valueset(vs1,vs2);
}

void
slapi_valueset_join_attr_valueset(const Slapi_Attr *a, Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2)
{
	if (slapi_valueset_isempty(vs1))
		valueset_add_valueset(vs1,vs2);
	else
		slapi_valueset_add_attr_valuearray_ext (a, vs1, vs2->va, vs2->num, 0, NULL);
}

int
slapi_valueset_first_value( Slapi_ValueSet *vs, Slapi_Value **v )
{
	return valuearray_first_value(vs->va,v);
}

int
slapi_valueset_next_value( Slapi_ValueSet *vs, int index, Slapi_Value **v)
{
	return valuearray_next_value(vs->va,index,v);
}

int
slapi_valueset_count( const Slapi_ValueSet *vs)
{
	if (NULL != vs)
	{
		return (vs->num);
	}
	return 0;
}

int
slapi_valueset_isempty( const Slapi_ValueSet *vs)
{
	if (NULL != vs)
	{
		return (vs->num == 0);
	}
	return 1;
}

int
valueset_isempty( const Slapi_ValueSet *vs)
{
	return valuearray_isempty(vs->va);
}


Slapi_Value *
slapi_valueset_find(const Slapi_Attr *a, const Slapi_ValueSet *vs, const Slapi_Value *v)
{
	Slapi_Value *r= NULL;
	if(vs->num > 0 )
	{
		if (vs->sorted) {
			r = valueset_find_sorted(a,vs,v,NULL);
		} else {
		int i= valuearray_find(a,vs->va,v);
		if(i!=-1)
		{
			r= vs->va[i];
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
	Slapi_Value *r= NULL;
	int i, position = 0;
	r = valueset_find_sorted(a,vs,v,&position);
	if (r) {
		/* the value was found, remove from valuearray */
		int index = vs->sorted[position];
		memmove(&vs->sorted[position],&vs->sorted[position+1],(vs->num - position)*sizeof(int));
		memmove(&vs->va[index],&vs->va[index+1],(vs->num - index)*sizeof(Slapi_Value *));
		vs->num--;
		/* unfortunately the references in the sorted array 
		 * to values past the removed one are no longer correct
		 * need to adjust */
		for (i=0; i < vs->num; i++) {
			if (vs->sorted[i] > index) vs->sorted[i]--;
		}
		PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
	}
	return r;
}
Slapi_Value *
valueset_remove_value(const Slapi_Attr *a, Slapi_ValueSet *vs, const Slapi_Value *v)
{
	if (vs->sorted) {
		return (valueset_remove_value_sorted(a, vs, v));
	} else {
	Slapi_Value *r= NULL;
	if(!valuearray_isempty(vs->va))
	{
		r= valuearray_remove_value(a, vs->va, v);
		if (r)
			vs->num--;
	}
	PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
	return r;
	}
}

/* 
 * Remove any values older than the CSN.
 */
int 
valueset_purge(Slapi_ValueSet *vs, const CSN *csn)
{
	int r= 0;
	if(!valuearray_isempty(vs->va))
	{
		/* valuearray_purge is not valueset and sorting aware,
		 * maybe need to rewrite, at least keep the valueset 
		 * consistent
		 */
		r= valuearray_purge(&vs->va, csn);
		vs->num = r;
		if (vs->va == NULL) {
			/* va was freed */
			vs->max = 0;
		}
		/* we can no longer rely on the sorting */
		if (vs->sorted != NULL) 
		{
			slapi_ch_free ((void **)&vs->sorted);
			vs->sorted = NULL;
		}
		PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
		
	}
	return 0;
}

Slapi_Value **
valueset_get_valuearray(const Slapi_ValueSet *vs)
{
	return (Slapi_Value**)vs->va;
}

size_t
valueset_size(const Slapi_ValueSet *vs)
{
	size_t s= 0;
	if(!valuearray_isempty(vs->va))
	{
		s= valuearray_size(vs->va);
	}
	return s;
}

/*
 * The value array is passed in by value.
 */
void
slapi_valueset_add_valuearray(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value **addvals)
{
	if(!valuearray_isempty(addvals))
	{
		slapi_valueset_add_attr_valuearray_ext (a, vs, addvals, valuearray_count(addvals), 0, NULL);
	}
}

void
valueset_add_valuearray(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
	if(!valuearray_isempty(addvals))
	{
		slapi_valueset_add_attr_valuearray_ext (NULL, vs, addvals, valuearray_count(addvals), 0, NULL);
	}
}

void
valueset_add_valuearray_ext(Slapi_ValueSet *vs, Slapi_Value **addvals, PRUint32 flags)
{
	if(!valuearray_isempty(addvals))
	{
		slapi_valueset_add_attr_valuearray_ext (NULL, vs, addvals, valuearray_count(addvals), flags, NULL);
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
	Slapi_Value *oneval[2];
	oneval[0]= (Slapi_Value*)addval;
	oneval[1]= NULL;
	slapi_valueset_add_attr_valuearray_ext(NULL, vs, oneval, 1, flags, NULL);
}


/* find value v in the sorted array of values, using syntax of attribut a for comparison 
 *
 */
static int
valueset_value_syntax_cmp( const Slapi_Attr *a, const Slapi_Value *v1, const Slapi_Value *v2 )
{
	/* this looks like a huge overhead, but there are no simple functions to normalize and
	 * compare available
	 */
	const Slapi_Value *oneval[3];
	Slapi_Value **keyvals;
	int rc;
 
	keyvals = NULL;
	oneval[0] = v1;
	oneval[1] = v2;
	oneval[2] = NULL;
	if ( slapi_attr_values2keys_sv( a, (Slapi_Value**)oneval, &keyvals, LDAP_FILTER_EQUALITY ) != 0
	    || keyvals == NULL
	    || keyvals[0] == NULL || keyvals[1] == NULL)
	{
		LDAPDebug( LDAP_DEBUG_ANY, "valueset_value_syntax_cmp: "
		    "slapi_attr_values2keys_sv failed for type %s\n",
		    a->a_type, 0, 0 );
		/* this should never happen since always a default syntax plugin
		 * will be found. Log an error and continue
		 */
		rc = strcasecmp(v1->bv.bv_val, v2->bv.bv_val);
	} else {
        	struct berval *bv1, *bv2;
		bv1 = &keyvals[0]->bv;
		bv2 = &keyvals[1]->bv;
		if ( bv1->bv_len < bv2->bv_len ) {
			rc = -1;
		} else if ( bv1->bv_len > bv2->bv_len ) {
			rc = 1;
		} else {
			rc = memcmp( bv1->bv_val, bv2->bv_val, bv1->bv_len );
		}
	}
	if (keyvals != NULL)
		valuearray_free( &keyvals );
	return (rc);

} 
static int
valueset_value_cmp( const Slapi_Attr *a, const Slapi_Value *v1, const Slapi_Value *v2 )
{

	if ( a == NULL || slapi_attr_is_dn_syntax_attr(a)) {
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
		return(valueset_value_syntax_cmp(a, v1, v2));
	}
}
/* find a value in the sorted valuearray. 
 * If the value is found the pointer to the value is returned and if index is provided
 * it will return the index of the value in the valuearray
 * If the value is not found, index will contain the place where the value would be inserted
 */
Slapi_Value *
valueset_find_sorted (const Slapi_Attr *a, const Slapi_ValueSet *vs, const Slapi_Value *v, int *index)
{
	int cmp = -1;
	int bot = -1;
	int top;
	
	if (vs->num == 0) {
		/* empty valueset */
		if (index) *index = 0;
		return (NULL);
	} else {
		top = vs->num;
	}
	while (top - bot > 1) {
		int mid = (top + bot)/2;
		if ( (cmp = valueset_value_cmp(a, v, vs->va[vs->sorted[mid]])) > 0)
			bot = mid;
		else
			top = mid;
	}
	if (index) *index = top;
	/* check if the value is found */
	if ( top < vs->num && (0 == valueset_value_cmp(a, v, vs->va[vs->sorted[top]]))) 
		return (vs->va[vs->sorted[top]]);
	else
		return (NULL);
}

void
valueset_array_to_sorted (const Slapi_Attr *a, Slapi_ValueSet *vs)
{
	int i, j, swap;

	/* initialize sort array */
	for (i = 0; i < vs->num; i++)
		vs->sorted[i] = i;

	/* now sort it, use a simple insertion sort as the array will always
	 * be very small when initially sorted
	 */
	for (i = 1; i < vs->num; i++) {
		swap = vs->sorted[i];
		j = i -1;

		while ( j >= 0 && valueset_value_cmp (a, vs->va[vs->sorted[j]], vs->va[swap]) > 0 ) {
			vs->sorted[j+1] = vs->sorted[j];
			j--;
		}
		vs->sorted[j+1] = swap;
	}
	PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
}
/* insert a value into a sorted array, if dupcheck is set no duplicate values will be accepted 
 * (is there a reason to allow duplicates ? LK
 * if the value is inserted the the function returns the index where it was inserted
 * if the value already exists -index is returned to indicate anerror an the index of the existing value
 */
int
valueset_insert_value_to_sorted(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value *vi, int dupcheck)
{
	int index = -1;
	Slapi_Value *v;
	/* test for pre sorted array and to avoid boundary condition */
	if (vs->num == 0) {
		vs->sorted[0] = 0;
		vs->num++;
		PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
		return(0);
	} else if (valueset_value_cmp (a, vi, vs->va[vs->sorted[vs->num-1]]) > 0 )  {
		vs->sorted[vs->num] = vs->num;
		vs->num++; 
		PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
		return (vs->num);
	}
	v = valueset_find_sorted (a, vs, vi, &index);
	if (v && dupcheck) {
		/* value already exists, do not insert duplicates */
		return (-1);
	} else {
		memmove(&vs->sorted[index+1],&vs->sorted[index],(vs->num - index)* sizeof(int));
		vs->sorted[index] = vs->num;
		vs->num++; 
		PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
		return(index);
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
slapi_valueset_add_attr_valuearray_ext(const Slapi_Attr *a, Slapi_ValueSet *vs, 
					Slapi_Value **addvals, int naddvals, unsigned long flags, int *dup_index)
{
	int rc = LDAP_SUCCESS;
	int i, dup;
	int allocate = 0;
	int need;
	int passin = flags & SLAPI_VALUE_FLAG_PASSIN;
	int dupcheck = flags & SLAPI_VALUE_FLAG_DUPCHECK;

	if (naddvals == 0) 
		return (rc);
	
	need = vs->num + naddvals + 1;
	if (need > vs->max) {
		/* Expand the array */
		allocate= vs->max;
		if ( allocate == 0 ) /* initial allocation */
			allocate = VALUESET_ARRAY_MINSIZE;
		while ( allocate < need )
		{
			if (allocate > VALUESET_ARRAY_MAXINCREMENT ) 
				/* do not grow exponentially */
				allocate += VALUESET_ARRAY_MAXINCREMENT;
			else
				allocate *= 2;
	
		}
	}
	if(allocate>0)
	{
		if(vs->va==NULL)
		{
			vs->va = (Slapi_Value **) slapi_ch_malloc( allocate * sizeof(Slapi_Value *));
		}
		else
		{
			vs->va = (Slapi_Value **) slapi_ch_realloc( (char *) vs->va, allocate * sizeof(Slapi_Value *));
			if (vs->sorted) {
				vs->sorted = (int *) slapi_ch_realloc( (char *) vs->sorted, allocate * sizeof(int));
			}
		}
		vs->max= allocate;
	}

	if ( (vs->num + naddvals > VALUESET_ARRAY_SORT_THRESHOLD || dupcheck ) && 
		!vs->sorted ) {
		/* initialize sort array and do initial sort */
		vs->sorted = (int *) slapi_ch_malloc( vs->max* sizeof(int));
		valueset_array_to_sorted (a, vs);
	}

	for ( i = 0; i < naddvals; i++)
	{
		if ( addvals[i]!=NULL )
		{
			if(passin)
			{
				/* We consume the values */
			    (vs->va)[vs->num] = addvals[i];
			}
			else
			{
				/* We copy the values */
			    (vs->va)[vs->num] = slapi_value_dup(addvals[i]);
			}
			if (vs->sorted) {
				dup = valueset_insert_value_to_sorted(a, vs, (vs->va)[vs->num], dupcheck);
				if (dup < 0 ) {
					rc = LDAP_TYPE_OR_VALUE_EXISTS;
					if (dup_index) *dup_index = i;
					if (passin) {
						/* we have to NULL out the first value so valuearray_free won't delete values in addvals */
						(vs->va)[0] = NULL;
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
	(vs->va)[vs->num] = NULL;

	PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
	return (rc); 
}

int
slapi_valueset_add_attr_value_ext(const Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value *addval, unsigned long flags)
{

	Slapi_Value *oneval[2];
	int rc;
	oneval[0]= (Slapi_Value*)addval;
	oneval[1]= NULL;
	rc = slapi_valueset_add_attr_valuearray_ext(a, vs, oneval, 1, flags, NULL );
	return (rc);
}


/*
 * The string is passed in by value.
 */
void
valueset_add_string(const Slapi_Attr *a, Slapi_ValueSet *vs, const char *s, CSNType t, const CSN *csn)
{
	Slapi_Value v;
	value_init(&v,NULL,t,csn);
	slapi_value_set_string(&v,s);
	slapi_valueset_add_attr_value_ext(a, vs, &v, 0 );
	value_done(&v);
}

/*
 * The value set is passed in by value.
 */
void
valueset_add_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2)
{
	int i;

	if (vs1 && vs2) {
		valuearray_free(&vs1->va);
		slapi_ch_free((void **)&vs1->sorted);
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
			vs1->va = (Slapi_Value **) slapi_ch_malloc( vs1->max * sizeof(Slapi_Value *));
			for (i=0; i< vs1->num;i++) {
				vs1->va[i] = slapi_value_dup(vs2->va[i]);
			}
			vs1->va[vs1->num] = NULL;
		}
		if (vs2->sorted) {
			vs1->sorted = (int *) slapi_ch_malloc( vs1->max* sizeof(int));
			memcpy(&vs1->sorted[0],&vs2->sorted[0],vs1->num* sizeof(int));
		}
		PR_ASSERT((vs1->sorted == NULL) || (vs1->num == 0) || ((vs1->sorted[0] >= 0) && (vs1->sorted[0] < vs1->num)));
	}
}

void
valueset_remove_string(const Slapi_Attr *a, Slapi_ValueSet *vs, const char *s)
{
	Slapi_Value v;
	Slapi_Value *removed;
	value_init(&v,NULL,CSN_TYPE_NONE,NULL);
	slapi_value_set_string(&v,s);
	removed = valueset_remove_value(a, vs, &v);
	if(removed) {
		slapi_value_free(&removed);
	}
	value_done(&v);
}

void
valueset_update_csn(Slapi_ValueSet *vs, CSNType t, const CSN *csn)
{
	if(!valuearray_isempty(vs->va))
	{
		valuearray_update_csn(vs->va,t,csn);
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
	int rc= LDAP_SUCCESS;
	if(vs->num > 0)
	{
		int i;
		struct valuearrayfast vaf_out;

		if ( va_out )
		{
			valuearrayfast_init(&vaf_out,*va_out);
		}

		/*
		 * For larger valuesets the valuarray is sorted, values can be deleted individually
		 *
		 */
			for ( i = 0; rc==LDAP_SUCCESS && valuestodelete[i] != NULL; ++i )
			{
				Slapi_Value *found = valueset_remove_value(a, vs, valuestodelete[i]);
				if(found!=NULL)
				{
					if ( va_out )
					{
						if (found->v_csnset &&
							(flags & (SLAPI_VALUE_FLAG_PRESERVECSNSET|
                                      SLAPI_VALUE_FLAG_USENEWVALUE)))
						{
							valuestodelete[i]->v_csnset = csnset_dup (found->v_csnset);
						}
						if (flags & SLAPI_VALUE_FLAG_USENEWVALUE)
						{
							valuearrayfast_add_value_passin(&vaf_out,valuestodelete[i]);
							valuestodelete[i] = found;
						}
						else
						{
							valuearrayfast_add_value_passin(&vaf_out,found);
						}
					}
					else
					{
						if (flags & SLAPI_VALUE_FLAG_PRESERVECSNSET)
						{
							valuestodelete[i]->v_csnset = found->v_csnset;
							found->v_csnset = NULL;
						}
						slapi_value_free ( & found );
					}
				}
				else
				{
					if((flags & SLAPI_VALUE_FLAG_IGNOREERROR) == 0)
					{
						LDAPDebug( LDAP_DEBUG_ARGS,"could not find value %d for attr %s\n", i-1, a->a_type, 0 );
						rc= LDAP_NO_SUCH_ATTRIBUTE;
					}
				}
			}
		if ( va_out )
		{
			*va_out= vaf_out.va;
			if(rc!=LDAP_SUCCESS)
			{
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
	if (NULL!=duped)
	{
		valueset_set_valuearray_byval(duped,dupee->va);
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
	return (valueset_replace_valuearray_ext(a, vs,valstoreplace, 1));
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
	PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
    } else {
	/* verify the given values are not duplicated.  */
	unsigned long flags = SLAPI_VALUE_FLAG_PASSIN|SLAPI_VALUE_FLAG_DUPCHECK;
	Slapi_ValueSet *vs_new = slapi_valueset_new();
	rc = slapi_valueset_add_attr_valuearray_ext (a, vs_new, valstoreplace, vals_count, flags, NULL);

	if ( rc == LDAP_SUCCESS )
	{
		/* used passin, so vs_new owns all of the Slapi_Value* in valstoreplace
		 * so tell valuearray_free_ext to start at index vals_count, which is
		 * NULL, then just free valstoreplace
		 */
        	valuearray_free_ext(&valstoreplace, vals_count);
		/* values look good - replace the values in the attribute */
        	if(!valuearray_isempty(vs->va))
        	{
            		/* remove old values */
            		slapi_valueset_done(vs);
        	}
        	vs->va = vs_new->va;
		vs_new->va = NULL;
        	vs->sorted = vs_new->sorted;
		vs_new->sorted = NULL;
        	vs->num = vs_new->num;
        	vs->max = vs_new->max;
		slapi_valueset_free (vs_new);
		PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
	}
	else
	{
	        /* caller expects us to own valstoreplace - since we cannot
	           use them, just delete them */
        	slapi_valueset_free(vs_new);
        	valuearray_free(&valstoreplace);
		PR_ASSERT((vs->sorted == NULL) || (vs->num == 0) || ((vs->sorted[0] >= 0) && (vs->sorted[0] < vs->num)));
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
	if(!valuearray_isempty(valuestoupdate) &&
		!valuearray_isempty(vs->va))
	{
		struct valuearrayfast vaf_valuesupdated;
		valuearrayfast_init(&vaf_valuesupdated,*valuesupdated);
		int i;
		int del_index = -1, del_count = 0;
		for (i=0;valuestoupdate[i]!=NULL;++i)
		{
			int index= valuearray_find(a, vs->va, valuestoupdate[i]);
			if(index!=-1)
			{
				value_update_csn(vs->va[index],t,csn);
				valuearrayfast_add_value_passin(&vaf_valuesupdated,valuestoupdate[i]);
				valuestoupdate[i]= NULL;
				del_count++;
				if (del_index < 0) del_index = i;
			}
			else 
			{ /* keep the value in valuestoupdate, to keep array compressed, move to first free slot*/
				if (del_index >= 0) {
					valuestoupdate[del_index] = valuestoupdate[i];
					del_index++;
				}
			}
		}
		/* complete compression */
		for (i=0; i<del_count;i++)
			valuestoupdate[del_index+i]= NULL;
			
		*valuesupdated= vaf_valuesupdated.va;
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
