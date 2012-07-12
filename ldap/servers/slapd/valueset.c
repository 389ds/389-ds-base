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

	return(0);
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

/*
 * Shunt up the values to cover the empty slots.
 *
 * "compressed" means "contains no NULL's"
 *
 * Invariant for the outer loop:
 * 	va[0..i] is compressed &&
 * 	va[n..numvalues] contains just NULL's
 *
 * Invariant for the inner loop:
 * 	i<j<=k<=n && va[j..k] has been shifted left by (j-i) places &&
 * 	va[k..n] remains to be shifted left by (j-i) places
 * 
 */
void
valuearray_compress(Slapi_Value **va,int numvalues)
{
	int i = 0;
	int n= numvalues;
	while(i<n)
	{
		if ( va[i] != NULL ) {
			i++;
		} else {
			int k,j;
			j = i + 1;
			/* Find the length of the next run of NULL's */
			while( j<n && va[j] == NULL) { j++; }
			/* va[i..j] is all NULL && j<= n */	
			for ( k = j; k<n; k++ )
			{
				va[k - (j-i)] = va[k];
				va[k] = NULL;
			}
			/* va[i..n] has been shifted down by j-i places */
			n = n - (j-i);
			/*
			 * If va[i] in now non null, then bump i,
			 * if not then we are done anyway (j==n) so can bump it.
			*/
			i++;
		}
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

void
valuearrayfast_add_valuearrayfast(struct valuearrayfast *vaf,const struct valuearrayfast *vaf_add)
{
	valuearray_add_valuearray_fast(&vaf->va,vaf_add->va,vaf->num,vaf_add->num,&vaf->max,0/*Exact*/,0/*!PassIn*/);
	vaf->num+= vaf_add->num;
}

/* <=========================== ValueArrayIndexTree =======================> */

static int valuetree_dupvalue_disallow( caddr_t d1, caddr_t d2 );
static int valuetree_node_cmp( caddr_t d1, caddr_t d2 );
static int valuetree_node_free( caddr_t data );

/*
 * structure used within AVL value trees.
 */
typedef struct valuetree_node
{
    int	index; /* index into the value array */
    Slapi_Value *sval; /* the actual value */
} valuetree_node;

/*
 * Create or update an AVL tree of values that can be used to speed up value
 *	lookups.  We store the index keys for the values in the AVL tree so
 *	we can use a trivial comparison function.
 *
 * Returns:
 *  LDAP_SUCCESS on success,
 *  LDAP_TYPE_OR_VALUE_EXISTS if the value already exists,
 *  LDAP_OPERATIONS_ERROR for some unexpected failure.
 *
 * Sets *valuetreep to the root of the AVL tree that was created.  If a
 *	non-zero value is returned, the tree is freed if free_on_error is non-zero
 *  and *valuetreep is set to NULL.
 */
int
valuetree_add_valuearray( const Slapi_Attr *sattr, Slapi_Value **va, Avlnode **valuetreep, int *duplicate_index )
{
	int rc= LDAP_SUCCESS;

	PR_ASSERT(sattr!=NULL);
	PR_ASSERT(valuetreep!=NULL);

	if ( duplicate_index ) {
		*duplicate_index = -1;
	}

	if ( !valuearray_isempty(va) )
	{
		Slapi_Value	**keyvals;
		/* Convert the value array into key values */
		if ( slapi_attr_values2keys_sv( sattr, (Slapi_Value**)va, &keyvals, LDAP_FILTER_EQUALITY ) != 0 ) /* jcm cast */
		{
			LDAPDebug( LDAP_DEBUG_ANY,"slapi_attr_values2keys_sv for attribute %s failed\n", sattr->a_type, 0, 0 );
			rc= LDAP_OPERATIONS_ERROR;
		}
		else
		{
			int	i;
			valuetree_node *vaip;
			for ( i = 0; rc==LDAP_SUCCESS && va[i] != NULL; ++i )
			{
				if ( keyvals[i] == NULL )
				{
					LDAPDebug( LDAP_DEBUG_ANY,"slapi_attr_values2keys_sv for attribute %s did not return enough key values\n", sattr->a_type, 0, 0 );
					rc= LDAP_OPERATIONS_ERROR;
				}
				else
				{
					vaip = (valuetree_node *)slapi_ch_malloc( sizeof( valuetree_node ));
					vaip->index = i;
					vaip->sval = keyvals[i];
					if (( rc = avl_insert( valuetreep, vaip, valuetree_node_cmp, valuetree_dupvalue_disallow )) != 0 )
					{
						slapi_ch_free( (void **)&vaip );
						/* Value must already be in there */
						rc= LDAP_TYPE_OR_VALUE_EXISTS;
						if ( duplicate_index ) {
							*duplicate_index = i;
						}
					}
					else
					{
						keyvals[i]= NULL;
					}
				}
			}
			/* start freeing at index i - the rest of them have already
			   been moved into valuetreep
			   the loop iteration will always do the +1, so we have
			   to remove it if so */
			i = (i > 0) ? i-1 : 0;
			valuearray_free_ext( &keyvals, i );
		}
	}
	if(rc!=0)
	{
		valuetree_free( valuetreep );
	}

	return rc;
}

int
valuetree_add_value( const Slapi_Attr *sattr, const Slapi_Value *v, Avlnode **valuetreep)
{
    Slapi_Value *va[2];
    va[0]= (Slapi_Value*)v;
    va[1]= NULL;
	return valuetree_add_valuearray( sattr, va, valuetreep, NULL);
}


/*
 * 
 * Find value "v" using AVL tree "valuetree"
 *
 * returns LDAP_SUCCESS if "v" was found, LDAP_NO_SUCH_ATTRIBUTE
 *	if "v" was not found and LDAP_OPERATIONS_ERROR if some unexpected error occurs.
 */
static int
valuetree_find( const struct slapi_attr *a, const Slapi_Value *v, Avlnode *valuetree, int *index)
{
	const Slapi_Value *oneval[2];
	Slapi_Value **keyvals;
	valuetree_node *vaip, tmpvain;

	PR_ASSERT(a!=NULL);
	PR_ASSERT(a->a_plugin!=NULL);
	PR_ASSERT(v!=NULL);
	PR_ASSERT(valuetree!=NULL);
	PR_ASSERT(index!=NULL);

	if ( a == NULL || v == NULL || valuetree == NULL )
	{
		return( LDAP_OPERATIONS_ERROR );
	}
 
	keyvals = NULL;
	oneval[0] = v;
	oneval[1] = NULL;
	if ( slapi_attr_values2keys_sv( a, (Slapi_Value**)oneval, &keyvals, LDAP_FILTER_EQUALITY ) != 0 /* jcm cast */
	    || keyvals == NULL
	    || keyvals[0] == NULL )
	{
		LDAPDebug( LDAP_DEBUG_ANY, "valuetree_find_and_replace: "
		    "slapi_attr_values2keys_sv failed for type %s\n",
		    a->a_type, 0, 0 );
		return( LDAP_OPERATIONS_ERROR );
	}

	tmpvain.index = 0;
	tmpvain.sval = keyvals[0];
	vaip = (valuetree_node *)avl_find( valuetree, &tmpvain, valuetree_node_cmp );

	if ( keyvals != NULL )
	{
		valuearray_free( &keyvals );
	}

	if (vaip == NULL)
	{
		return( LDAP_NO_SUCH_ATTRIBUTE );
	}
	else
	{
		*index= vaip->index;
	}

	return( LDAP_SUCCESS );
}

static int
valuetree_dupvalue_disallow( caddr_t d1, caddr_t d2 )
{
	return( 1 );
}


void
valuetree_free( Avlnode **valuetreep )
{
	if ( valuetreep != NULL && *valuetreep != NULL )
	{
		avl_free( *valuetreep, valuetree_node_free );
		*valuetreep = NULL;
	}
}


static int
valuetree_node_free( caddr_t data )
{
	if ( data!=NULL )
	{
		valuetree_node *vaip = (valuetree_node *)data;

                slapi_value_free(&vaip->sval);
	    	slapi_ch_free( (void **)&data );
	}
	return( 0 );	
}


static int
valuetree_node_cmp( caddr_t d1, caddr_t d2 )
{
        const struct berval *bv1, *bv2;
	int			rc;

        bv1 = slapi_value_get_berval(((valuetree_node *)d1)->sval);
        bv2 = slapi_value_get_berval(((valuetree_node *)d2)->sval);

	if ( bv1->bv_len < bv2->bv_len ) {
		rc = -1;
	} else if ( bv1->bv_len > bv2->bv_len ) {
		rc = 1;
	} else {
		rc = memcmp( bv1->bv_val, bv2->bv_val, bv1->bv_len );
	}

	return( rc );
}

/* <=========================== Value Set =======================> */

/*
 *  JCM: All of these valueset functions are just forwarded to the
 *  JCM: valuearray functions... waste of time. Inline them!
 */

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
	}
}

void
slapi_valueset_done(Slapi_ValueSet *vs)
{
	if(vs!=NULL)
	{
		if(vs->va!=NULL)
		{
			valuearray_free(&vs->va);
			vs->va= NULL;
		}
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
}

void
valueset_set_valuearray_byval(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
	slapi_valueset_init(vs);
	valueset_add_valuearray(vs,addvals);
}

void
valueset_set_valuearray_passin(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
	slapi_valueset_init(vs);
	vs->va= addvals;
}

void
slapi_valueset_set_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2)
{
	slapi_valueset_init(vs1);
	valueset_add_valueset(vs1,vs2);
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
	int r=0;
	if (NULL != vs)
	{
		if(!valuearray_isempty(vs->va))
		{
			r= valuearray_count(vs->va);
		}
	}
	return r;
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
	if(!valuearray_isempty(vs->va))
	{
		int i= valuearray_find(a,vs->va,v);
		if(i!=-1)
		{
			r= vs->va[i];
		}
	}
	return r;
}

/*
 * The value is found in the set, removed and returned.
 * The caller is responsible for freeing the value.
 */
Slapi_Value *
valueset_remove_value(const Slapi_Attr *a, Slapi_ValueSet *vs, const Slapi_Value *v)
{
	Slapi_Value *r= NULL;
	if(!valuearray_isempty(vs->va))
	{
		r= valuearray_remove_value(a, vs->va, v);
	}
	return r;
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
		r= valuearray_purge(&vs->va, csn);
	}
	return r;
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
valueset_add_valuearray(Slapi_ValueSet *vs, Slapi_Value **addvals)
{
	if(!valuearray_isempty(addvals))
	{
		valuearray_add_valuearray(&vs->va, addvals, 0);
	}
}

void
valueset_add_valuearray_ext(Slapi_ValueSet *vs, Slapi_Value **addvals, PRUint32 flags)
{
	if(!valuearray_isempty(addvals))
	{
		valuearray_add_valuearray(&vs->va, addvals, flags);
	}
}

/*
 * The value is passed in by value.
 */
void
slapi_valueset_add_value(Slapi_ValueSet *vs, const Slapi_Value *addval)
{
     valuearray_add_value(&vs->va,addval);
}

void
slapi_valueset_add_value_ext(Slapi_ValueSet *vs, Slapi_Value *addval, unsigned long flags)
{
	Slapi_Value *oneval[2];
	oneval[0]= (Slapi_Value*)addval;
	oneval[1]= NULL;
	valuearray_add_valuearray(&vs->va, oneval, flags);
}

/*
 * The string is passed in by value.
 */
void
valueset_add_string(Slapi_ValueSet *vs, const char *s, CSNType t, const CSN *csn)
{
	Slapi_Value v;
	value_init(&v,NULL,t,csn);
	slapi_value_set_string(&v,s);
    valuearray_add_value(&vs->va,&v);
	value_done(&v);
}

/*
 * The value set is passed in by value.
 */
void
valueset_add_valueset(Slapi_ValueSet *vs1, const Slapi_ValueSet *vs2)
{
	if (vs1 && vs2)
		valueset_add_valuearray(vs1, vs2->va);
}

void
valueset_remove_string(const Slapi_Attr *a, Slapi_ValueSet *vs, const char *s)
{
	Slapi_Value v;
	Slapi_Value *removed;
	value_init(&v,NULL,CSN_TYPE_NONE,NULL);
	slapi_value_set_string(&v,s);
	removed = valuearray_remove_value(a, vs->va, &v);
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
	if(!valuearray_isempty(vs->va))
	{
		int numberofvaluestodelete= valuearray_count(valuestodelete);
		struct valuearrayfast vaf_out;
		if ( va_out )
		{
			valuearrayfast_init(&vaf_out,*va_out);
		}

		/*
		 * If there are more then one values, build an AVL tree to check
		 * the duplicated values.
		 */
		if ( numberofvaluestodelete > 1 )
		{
			/*
			 * Several values to delete: first build an AVL tree that
			 * holds all of the existing values and use that to find
			 * the values we want to delete.
			 */
			Avlnode	*vtree = NULL;
			int numberofexistingvalues= slapi_valueset_count(vs);
			rc= valuetree_add_valuearray( a, vs->va, &vtree, NULL );
			if ( rc!=LDAP_SUCCESS )
			{
				/*
				 * failed while constructing AVL tree of existing
				 * values... something bad happened.
				 */
				rc= LDAP_OPERATIONS_ERROR;
			}
			else
			{
				int i;
				/*
				 * find and mark all the values that are to be deleted
				 */
				for ( i = 0; rc == LDAP_SUCCESS && valuestodelete[i] != NULL; ++i )
				{
					int index= 0;
					rc = valuetree_find( a, valuestodelete[i], vtree, &index );
					if(rc==LDAP_SUCCESS)
					{
						if(vs->va[index]!=NULL)
						{
							/* Move the value to be removed to the out array */
							if ( va_out )
							{
								if (vs->va[index]->v_csnset &&
									(flags & (SLAPI_VALUE_FLAG_PRESERVECSNSET|
                                              SLAPI_VALUE_FLAG_USENEWVALUE)))
								{
									valuestodelete[i]->v_csnset = csnset_dup (vs->va[index]->v_csnset);
								}
								if (flags & SLAPI_VALUE_FLAG_USENEWVALUE)
								{
									valuearrayfast_add_value_passin(&vaf_out,valuestodelete[i]);
									valuestodelete[i] = vs->va[index];
									vs->va[index] = NULL;
								}
								else
								{
									valuearrayfast_add_value_passin(&vaf_out,vs->va[index]);
									vs->va[index] = NULL;
								}
							}
							else
							{
								if (flags & SLAPI_VALUE_FLAG_PRESERVECSNSET)
								{
									valuestodelete[i]->v_csnset = vs->va[index]->v_csnset;
									vs->va[index]->v_csnset = NULL;
								}
								slapi_value_free ( & vs->va[index] );
							}
						}
						else
						{
							/* We already deleted this value... */
							if((flags & SLAPI_VALUE_FLAG_IGNOREERROR) == 0)
							{
								/* ...that's an error. */
								rc= LDAP_NO_SUCH_ATTRIBUTE;
							}
						}
					}
					else
					{
						/* Couldn't find the value to be deleted */
						if(rc==LDAP_NO_SUCH_ATTRIBUTE && (flags & SLAPI_VALUE_FLAG_IGNOREERROR ))
						{
							rc= LDAP_SUCCESS;
						}
					}
				}
				valuetree_free( &vtree );

				if ( rc != LDAP_SUCCESS )
				{
					LDAPDebug( LDAP_DEBUG_ANY,"could not find value %d for attr %s (%s)\n", i-1, a->a_type, ldap_err2string( rc ));
				}
				else
				{
					/* Shunt up all the remaining values to cover the deleted ones. */
					valuearray_compress(vs->va,numberofexistingvalues);
				}
			}
		}
		else
		{
			/* We delete one or no value, so we use brute force. */
			int i;
			for ( i = 0; rc==LDAP_SUCCESS && valuestodelete[i] != NULL; ++i )
			{
				Slapi_Value *found= valueset_remove_value(a, vs, valuestodelete[i]);
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

/*
 * Check if the set of values in the valueset and the valuearray intersect.
 *
 * Returns
 *  LDAP_SUCCESS - No intersection.
 *  LDAP_NO_SUCH_ATTRIBUTE - There is an intersection.
 *  LDAP_OPERATIONS_ERROR - There are duplicate values in the value set already.
 */
int
valueset_intersectswith_valuearray(Slapi_ValueSet *vs, const Slapi_Attr *a, Slapi_Value **values, int *duplicate_index )
{
	int rc= LDAP_SUCCESS;

	if ( duplicate_index ) {
		*duplicate_index = -1;
	}

	if(valuearray_isempty(vs->va))
	{
		/* No intersection */
	}
	else
	{
		int numberofvalues= valuearray_count(values);
		/*
		 * determine whether we should use an AVL tree of values or not
		 */
		if (numberofvalues==0)
		{
			/* No intersection */
		}
		else if ( numberofvalues > 1 )
		{
			/*
			 * Several values to add: use an AVL tree to detect duplicates.
			 */
			Avlnode	*vtree = NULL;
			rc= valuetree_add_valuearray( a, vs->va, &vtree, duplicate_index );
			if(rc==LDAP_OPERATIONS_ERROR)
			{
				/* There were already duplicate values in the value set */
			}
			else
			{
				rc= valuetree_add_valuearray( a, values, &vtree, duplicate_index );
				/*
				 * Returns LDAP_OPERATIONS_ERROR if something very bad happens.
				 * Or LDAP_TYPE_OR_VALUE_EXISTS if a value already exists.
				 */
			}
		    valuetree_free( &vtree );
		}
		else
		{
			/*
			 * One value to add: don't bother constructing
			 * an AVL tree, etc. since it probably isn't worth the time.
			 *
			 * JCM - This is actually quite slow because the comparison function is looked up many times.
			 */
			int i;
			for ( i = 0; rc == LDAP_SUCCESS && values[i] != NULL; ++i )
			{
				if(valuearray_find(a, vs->va, values[i])!=-1)
				{
					rc = LDAP_TYPE_OR_VALUE_EXISTS;
					*duplicate_index = i;
					break;
				}
			}
		}
	}
	return rc;
}

Slapi_ValueSet *
valueset_dup(const Slapi_ValueSet *dupee)
{
	Slapi_ValueSet *duped= (Slapi_ValueSet *)slapi_ch_calloc(1,sizeof(Slapi_ValueSet));
	if (NULL!=duped)
	{
		valueset_add_valuearray( duped, dupee->va );
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
valueset_replace(Slapi_Attr *a, Slapi_ValueSet *vs, Slapi_Value **valstoreplace)
{
    int rc = LDAP_SUCCESS;
    int numberofvalstoreplace= valuearray_count(valstoreplace);
    /* verify the given values are not duplicated.
       if replacing with one value, no need to check.  just replace it.
     */
    if (numberofvalstoreplace > 1)
    {
        Avlnode *vtree = NULL;
        rc = valuetree_add_valuearray( a, valstoreplace, &vtree, NULL );
        valuetree_free(&vtree);
        if ( LDAP_SUCCESS != rc &&
             /* bz 247413: don't override LDAP_TYPE_OR_VALUE_EXISTS */
             LDAP_TYPE_OR_VALUE_EXISTS != rc )
        {
            /* There were already duplicate values in the value set */
            rc = LDAP_OPERATIONS_ERROR;
        }
    }

    if ( rc == LDAP_SUCCESS )
    {
        /* values look good - replace the values in the attribute */
        if(!valuearray_isempty(vs->va))
        {
            /* remove old values */
            slapi_valueset_done(vs);
        }
        /* we now own valstoreplace */
        vs->va = valstoreplace;
    }
    else
    {
        /* caller expects us to own valstoreplace - since we cannot
           use them, just delete them */
        valuearray_free(&valstoreplace);
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
		/*
		 * determine whether we should use an AVL tree of values or not
		 */
		struct valuearrayfast vaf_valuesupdated;
		int numberofvaluestoupdate= valuearray_count(valuestoupdate);
		valuearrayfast_init(&vaf_valuesupdated,*valuesupdated);
		if (numberofvaluestoupdate > 1) /* multiple values to update */
		{
			int i;
			Avlnode	*vtree = NULL;
			int rc= valuetree_add_valuearray( a, vs->va, &vtree, NULL );
			PR_ASSERT(rc==LDAP_SUCCESS);
			for (i=0;valuestoupdate[i]!=NULL;++i)
			{
				int index= 0;
				rc = valuetree_find( a, valuestoupdate[i], vtree, &index );
				if(rc==LDAP_SUCCESS)
				{
					value_update_csn(vs->va[index],t,csn);
					valuearrayfast_add_value_passin(&vaf_valuesupdated,valuestoupdate[i]);
					valuestoupdate[i] = NULL;
				}
			}
			valuetree_free(&vtree);
		}
		else
		{
			int i;
			for (i=0;valuestoupdate[i]!=NULL;++i)
			{
				int index= valuearray_find(a, vs->va, valuestoupdate[i]);
				if(index!=-1)
				{
					value_update_csn(vs->va[index],t,csn);
					valuearrayfast_add_value_passin(&vaf_valuesupdated,valuestoupdate[i]);
					valuestoupdate[i]= NULL;
				}
			}
		}
		valuearray_compress(valuestoupdate,numberofvaluestoupdate);
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

