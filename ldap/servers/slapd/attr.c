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

/* attr.c - routines for dealing with attributes */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/time.h>
#include <sys/param.h>
#include <fcntl.h>
#include <sys/socket.h>
#endif
#include "slap.h"
#undef DEBUG                    /* disable counters */
#include <prcountr.h>


static int counters_created= 0;
PR_DEFINE_COUNTER(slapi_attr_counter_created);
PR_DEFINE_COUNTER(slapi_attr_counter_deleted);
PR_DEFINE_COUNTER(slapi_attr_counter_exist);

/*
 * structure used within AVL value trees.
 */
typedef struct slapi_attr_value_index {
	int		savi_index;		/* index into a_vals[] */
	struct berval	*savi_normval;		/* normalized value */
} SlapiAttrValueIndex;

/*
 * Utility function used by slapi_attr_type_cmp to
 * find the next component of an attribute type.
 */
static const char *
next_comp( const char *s )
{
	while ( *s && *s != ';' ) {
		s++;
	}
	if ( *s == '\0' ) {
		return( NULL );
	} else {
		return( s + 1 );
	}
}

/*
 * Utility function used by slapi_attr_type_cmp to
 * compare two components of an attribute type.
 */
static int
comp_cmp( const char *s1, const char *s2 )
{
	while ( *s1 && *s1 != ';' && tolower( *s1 ) == tolower( *s2 ) ) {
		s1++, s2++;
	}
	if ( *s1 != *s2 ) {
		if ( (*s1 == '\0' || *s1 == ';') &&
		    (*s2 == '\0' || *s2 == ';') ) {
			return( 0 );
		} else {
			return( 1 );
		}
	} else {
		return( 0 );
	}
}

int
slapi_attr_type_cmp( const char *a1, const char *a2, int opt )
{
	int	rc= 0;

    switch ( opt ) {
    case SLAPI_TYPE_CMP_EXACT: /* compare base name + options as given */
        rc = strcasecmp( a1, a2 );
		break;

    case SLAPI_TYPE_CMP_BASE: /* ignore options on both names - compare base names only */
		rc = comp_cmp( a1, a2 );
        break;

    case SLAPI_TYPE_CMP_SUBTYPE: /* ignore options on second name that are not in first name */
		/*
		 * first, check that the base types match
		 */
		if ( comp_cmp( a1, a2 ) != 0 ) {
			rc = 1;
			break;
		}
		/*
		 * next, for each component in a1, make sure there is a
		 * matching component in a2. the order must be the same,
		 * so we can keep looking where we left off each time in a2
		 */
		rc = 0;
		for ( a1 = next_comp( a1 ); a1 != NULL; a1 = next_comp( a1 ) ) {
			for ( a2 = next_comp( a2 ); a2 != NULL;
			    a2 = next_comp( a2 ) ) {
				if ( comp_cmp( a1, a2 ) == 0 ) {
					break;
				}
			}
			if ( a2 == NULL ) {
				rc = 1;
				break;
			}
		}
        break;
    }

	return( rc );
}




/*
 * Return 1 if the two types are equivalent -- either the same type name,
 * or aliases for one another, including OIDs.
 *
 * Objective: don't allocate any memory!
 */
int
slapi_attr_types_equivalent(const char *t1, const char *t2)
{
	int retval = 0;
    struct asyntaxinfo *asi1, *asi2;

	if (NULL == t1 || NULL == t2) {
		return 0;
	}

	asi1 = attr_syntax_get_by_name(t1);
	asi2 = attr_syntax_get_by_name(t2);
	if (NULL != asi1) {
		if (NULL != asi2) {
			/* Both found - compare normalized names */
			if (strcasecmp(asi1->asi_name, asi2->asi_name) == 0) {
				retval = 1;
			} else {
				retval = 0;
			}
		} else {
			/* One found, the other wasn't, so not equivalent */
			retval = 0;
		}
	} else if (NULL != asi2) {
		/* One found, the other wasn't, so not equivalent */
		retval = 0;
	} else {
		/* Neither found - perform case-insensitive compare */
		if (strcasecmp(t1, t2) == 0) {
			retval = 1;
		} else {
			retval = 0;
		}
	}

	attr_syntax_return( asi1 );
	attr_syntax_return( asi2 );

	return retval;
}

Slapi_Attr *
slapi_attr_new()
{
	Slapi_Attr *a= (Slapi_Attr *)slapi_ch_calloc( 1, sizeof(Slapi_Attr));
	if(!counters_created)
	{
		PR_CREATE_COUNTER(slapi_attr_counter_created,"Slapi_Attr","created","");
		PR_CREATE_COUNTER(slapi_attr_counter_deleted,"Slapi_Attr","deleted","");
		PR_CREATE_COUNTER(slapi_attr_counter_exist,"Slapi_Attr","exist","");
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_attr_counter_created);
    PR_INCREMENT_COUNTER(slapi_attr_counter_exist);
	return a;
}

Slapi_Attr *
slapi_attr_init(Slapi_Attr *a, const char *type)
{
	return slapi_attr_init_locking_optional(a, type, PR_TRUE);
}

int
slapi_attr_init_syntax(Slapi_Attr    *a)
{
	int rc = 1;
	struct asyntaxinfo *asi = NULL;
	char *tmp = 0;
	const char *basetype= NULL;
	char buf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];

	basetype = buf;
	tmp = slapi_attr_basetype(a->a_type, buf, sizeof(buf));
	if (tmp) {
		basetype = buf;
	}
	asi = attr_syntax_get_by_name_with_default (basetype);
	if (asi) {
		rc = 0;
		a->a_plugin = asi->asi_plugin;
		a->a_flags = asi->asi_flags;
		a->a_mr_eq_plugin = asi->asi_mr_eq_plugin;
		a->a_mr_ord_plugin = asi->asi_mr_ord_plugin;
		a->a_mr_sub_plugin = asi->asi_mr_sub_plugin;
	} 
	if (tmp)
		slapi_ch_free_string(&tmp);
	return rc;
}

Slapi_Attr *
slapi_attr_init_locking_optional(Slapi_Attr *a, const char *type, PRBool use_lock)
{
	PR_ASSERT(a!=NULL);

	if(NULL != a)
	{
		struct asyntaxinfo *asi= NULL;
		const char *basetype= NULL;
		char *tmp= NULL;

		if(type!=NULL)
		{
			char buf[SLAPD_TYPICAL_ATTRIBUTE_NAME_MAX_LENGTH];
			basetype = buf;
			tmp = slapi_attr_basetype(type, buf, sizeof(buf));
			if(tmp != NULL)
			{
				basetype = tmp;	/* basetype was malloc'd */
			}
			asi = attr_syntax_get_by_name_locking_optional(basetype, use_lock);
		}
		if(NULL == asi)
		{
			a->a_type = attr_syntax_normalize_no_lookup( type );
			/*
			 * no syntax for this type... return Octet String
			 * syntax.  we accomplish this by looking up a well known
			 * attribute type that has that syntax.
			 */
			asi = attr_syntax_get_by_name_locking_optional(
					ATTR_WITH_OCTETSTRING_SYNTAX, use_lock);
		}
		else
		{
			char	*attroptions = NULL;

			if ( NULL != type ) {
				attroptions = strchr( type, ';' );
			}

			if ( NULL == attroptions ) {
				a->a_type = slapi_ch_strdup(asi->asi_name);
			} else {
				/*
				 * If the original type includes any attribute options,
				 * the a_type field is set to the type contained in the
				 * attribute syntax followed by a normalized copy of the
				 * options.
				 */
				char	*normalized_options;

				normalized_options = attr_syntax_normalize_no_lookup( attroptions );
				a->a_type = slapi_ch_smprintf("%s%s", asi->asi_name, normalized_options );
				slapi_ch_free_string( &normalized_options );
			}
		}
		if ( asi != NULL )
		{
			a->a_plugin = asi->asi_plugin;
			a->a_flags = asi->asi_flags;
			a->a_mr_eq_plugin = asi->asi_mr_eq_plugin;
			a->a_mr_ord_plugin = asi->asi_mr_ord_plugin;
			a->a_mr_sub_plugin = asi->asi_mr_sub_plugin;
		}
		else
		{
			a->a_plugin = NULL;    /* XXX - should be rare */
			a->a_flags = 0;        /* XXX - should be rare */
			a->a_mr_eq_plugin = NULL;
			a->a_mr_ord_plugin = NULL;
			a->a_mr_sub_plugin = NULL;
		}

		attr_syntax_return_locking_optional( asi, use_lock );

		if (NULL != tmp)
		{
			slapi_ch_free_string(&tmp);
		}
		slapi_valueset_init(&a->a_present_values);
		slapi_valueset_init(&a->a_deleted_values);
		a->a_listtofree= NULL;
		a->a_deletioncsn= NULL;
		a->a_next= NULL;
	}

	return a;
}

Slapi_Attr *
slapi_attr_init_nosyntax(Slapi_Attr *a, const char *type)
{

	a->a_type = slapi_ch_strdup(type);
	slapi_valueset_init(&a->a_present_values);
	slapi_valueset_init(&a->a_deleted_values);
	a->a_listtofree= NULL;
	a->a_deletioncsn= NULL;
	a->a_next= NULL;

	return a;
}

Slapi_Attr *
slapi_attr_dup(const Slapi_Attr *attr)
{
	Slapi_Attr *newattr= slapi_attr_new();
	slapi_attr_init(newattr, attr->a_type);
	slapi_valueset_set_valueset( &newattr->a_deleted_values,  &attr->a_deleted_values );
	slapi_valueset_set_valueset( &newattr->a_present_values,  &attr->a_present_values );
	newattr->a_deletioncsn= csn_dup(attr->a_deletioncsn);
	return newattr;
}

void
slapi_attr_free( Slapi_Attr **ppa )
{
	if(ppa!=NULL && *ppa!=NULL)
	{
		Slapi_Attr *a= *ppa;
		attr_done(a);
		slapi_ch_free( (void**)&a );
        PR_INCREMENT_COUNTER(slapi_attr_counter_deleted);
	    PR_DECREMENT_COUNTER(slapi_attr_counter_exist);
	}
}

void
attr_done(Slapi_Attr *a)
{
	if(a!=NULL)
	{
		slapi_ch_free((void**)&a->a_type);
		csn_free(&a->a_deletioncsn);
		slapi_valueset_done(&a->a_present_values);
		slapi_valueset_done(&a->a_deleted_values);
		{
		    struct bervals2free *freelist;
		    struct bervals2free *tmp;
		    freelist = a->a_listtofree;
		    while(freelist) {
				ber_bvecfree(freelist->bvals);
				tmp=freelist;
				freelist = freelist->next;
				slapi_ch_free((void **)&tmp);
		    }
		}
	}
}

/*
 * slapi_attr_basetype - extracts the attribute base type (without
 * any attribute description options) from type. puts the result
 * in buf if it can, otherwise, it malloc's and returns the base
 * which should be free()'ed later.
 */
char *
slapi_attr_basetype( const char *type, char *buf, size_t bufsize )
{
	unsigned int	i;

	i = 0;
	while ( *type && *type != ';' && i < bufsize ) {
		buf[i++] = *type++;
	}
	if ( i < bufsize ) {
		buf[i] = '\0';
		return( NULL );
	} else {
		int	len;
		char *tmp;

		len = strlen( type );
		tmp = slapi_ch_malloc( len + 1 );
		slapi_attr_basetype( type, tmp, len + 1 );
		return( tmp );
	}
}

/*
 * returns 0 if "v" is already a value within "a" and non-zero if not.
 */
int
slapi_attr_value_find( const Slapi_Attr *a, const struct berval *v )
{
	struct ava	ava;
	unsigned long a_flags;

	if ( NULL == a ) {
		return( -1 );
	}

	if ( a->a_flags == 0 && a->a_plugin == NULL ) { 
	    slapi_attr_init_syntax ((Slapi_Attr *)a);
	}
	ava.ava_type = a->a_type;
	ava.ava_value = *v;
	if (a->a_flags & SLAPI_ATTR_FLAG_NORMALIZED) {
	    a_flags = a->a_flags;
	    ava.ava_private = &a_flags;
	} else {
	    ava.ava_private = NULL;
	}
	return(plugin_call_syntax_filter_ava( a, LDAP_FILTER_EQUALITY, &ava ));
}

int
slapi_attr_get_type( Slapi_Attr *a, char **type )
{
	*type = a->a_type;
	return( 0 );
}


/*
 * Fetch a copy of the values as an array of struct berval *'s.
 * Returns 0 upon success and non-zero on failure.
 * Free the array of values by calling ber_bvecfree().
 */
int
slapi_attr_get_bervals_copy( Slapi_Attr *a, struct berval ***vals )
{
	int retVal= 0;

	if ( NULL == vals )
	{
		return -1;
	}
	
    if(NULL==a || valueset_isempty(&a->a_present_values))
    {
		*vals = NULL;
    }
	else
	{
		Slapi_Value **va= valueset_get_valuearray(&a->a_present_values);
		valuearray_get_bervalarray(va,vals);
	}
    return retVal;
}


/*
 * JCM: BEWARE.. HIGHLY EVIL.. DO NOT USE THIS FUNCTION.
 * XXXmcs: Why not?  Because it is only provided as a not-quite-backwards
 * compatible interface for older (pre-iDS 5.0) plugins.  It works by
 * making a copy of the attribute values (the pre-iDS 5.0 function with
 * the same name returned a pointer into the Slapi_Attr structure -- no
 * copying).  Since older users of this interface did not have to free
 * the values, this function arranges to free them WHEN THE Slapi_Attr
 * IS DESTROYED.  This is accomplished by adding a pointer to the newly
 * allocated copy to a "to be freed" linked list inside the Slapi_Attr.
 * But if the Slapi_Attr is not destroyed very often, and this function
 * is called repeatedly, memory usage will grow without bound.  Not good.
 * The value copies are freed inside attr_done() which is called from
 * slapi_attr_free() and a few other places.
 *
 * If you really want a copy of the values as a struct berval ** array,
 * call slapi_attr_get_bervals_copy() and free it yourself by calling
 * ber_bvecfree().
 */
int
slapi_attr_get_values( Slapi_Attr *a, struct berval ***vals )
{
	int retVal = slapi_attr_get_bervals_copy( a, vals );

	if ( 0 == retVal )
	{
		struct bervals2free *newfree;
		newfree = (struct bervals2free *)slapi_ch_malloc(sizeof(struct bervals2free));
		newfree->next = a->a_listtofree;
		newfree->bvals = *vals;
		a->a_listtofree = newfree;
	}

    return retVal;
}


/*
 * Fetch a copy of the present valueset.
 * Caller must free the valueset.
 */
int
slapi_attr_get_valueset(const Slapi_Attr *a, Slapi_ValueSet **vs)
{
	int retVal= 0;
	if(vs!=NULL)
	{
		*vs= valueset_dup(&a->a_present_values);
	}
	return retVal;
}

/*
 * Careful... this returns a pointer to the contents!
 */
Slapi_Value **
attr_get_present_values(const Slapi_Attr *a)
{
	return valueset_get_valuearray(&a->a_present_values);
}

int
slapi_attr_get_flags( const Slapi_Attr *a, unsigned long *flags )
{
	if ( a->a_flags == 0 && a->a_plugin == NULL ) { 
	    slapi_attr_init_syntax ((Slapi_Attr *)a);
	}
	*flags = a->a_flags;
	return( 0 );
}

int
slapi_attr_flag_is_set( const Slapi_Attr *a, unsigned long flag )
{
	if ( a->a_flags == 0 && a->a_plugin == NULL ) { 
	    slapi_attr_init_syntax ((Slapi_Attr *)a);
	}
	return( a->a_flags & flag );
}

int
slapi_attr_value_cmp( const Slapi_Attr *a, const struct berval *v1, const struct berval *v2 )
{
    Slapi_Attr a2 = *a;
    struct ava ava;
    Slapi_Value *cvals[2];
    Slapi_Value tmpcval;

    if ( a->a_flags == 0 && a->a_plugin == NULL ) {
        slapi_attr_init_syntax ((Slapi_Attr *)a);
    }
    cvals[0] = &tmpcval;
    cvals[0]->v_csnset = NULL;
    cvals[0]->bv = *v1;
    cvals[0]->v_flags = 0;
    cvals[1] = NULL;
    a2.a_present_values.va = cvals; /* JCM - PUKE */
    ava.ava_type = a->a_type;
    ava.ava_value = *v2;
    ava.ava_private = NULL;

    return( plugin_call_syntax_filter_ava(&a2, LDAP_FILTER_EQUALITY, &ava));
}

int
slapi_attr_value_cmp_ext(const Slapi_Attr *a, Slapi_Value *v1, Slapi_Value *v2)
{
    struct ava ava;
    Slapi_Attr a2 = *a;
    Slapi_Value *cvals[2];
    unsigned long v2_flags = v2->v_flags;
    const struct berval *bv2 = slapi_value_get_berval(v2);

    if ( a->a_flags == 0 && a->a_plugin == NULL ) {
       slapi_attr_init_syntax ((Slapi_Attr *)a);
    }
    cvals[0] = v1;
    cvals[1] = NULL;
    a2.a_present_values.va = cvals;
    ava.ava_type = a->a_type;
    ava.ava_value = *bv2;
    if (v2_flags) {
        ava.ava_private = &v2_flags;
    } else {
        ava.ava_private = NULL;
    }

    return (plugin_call_syntax_filter_ava(&a2, LDAP_FILTER_EQUALITY, &ava));
}

/*
 * Set the CSN of all the present values.
 */
int
attr_set_csn( Slapi_Attr *a, const CSN *csn)
{
	PR_ASSERT(a!=NULL);
	valueset_update_csn(&a->a_present_values, CSN_TYPE_VALUE_UPDATED, csn);
	return 0;
}

int
attr_set_deletion_csn( Slapi_Attr *a, const CSN *csn)
{
	PR_ASSERT(a!=NULL);
	if(csn_compare(csn,a->a_deletioncsn)>0)
	{
		csn_free(&a->a_deletioncsn);
		a->a_deletioncsn= csn_dup(csn);
	}
	return 0;
}

const CSN *
attr_get_deletion_csn(const Slapi_Attr *a)
{
	PR_ASSERT(a!=NULL);
	return a->a_deletioncsn;
}

int
slapi_attr_first_value( Slapi_Attr *a, Slapi_Value **v )
{
	int rc;

	if(NULL == a) {
		rc = -1;
	} else {
		rc=slapi_valueset_first_value( &a->a_present_values, v );
	} 
	return rc;
}

int
slapi_attr_next_value( Slapi_Attr *a, int hint, Slapi_Value **v)
{
	int rc;

	if(NULL == a) {
		rc = -1;
	} else {
		rc=slapi_valueset_next_value( &a->a_present_values, hint, v );
	}
	return rc;
}

int
slapi_attr_get_numvalues( const Slapi_Attr *a, int *numValues )
{
	if(NULL == a) {
		*numValues = 0;
	} else {
		*numValues = slapi_valueset_count(&a->a_present_values);
	}
	return 0;
}


int
attr_first_deleted_value( Slapi_Attr *a, Slapi_Value **v )
{
	return slapi_valueset_first_value( &a->a_deleted_values, v );
}

int
attr_next_deleted_value( Slapi_Attr *a, int hint, Slapi_Value **v)
{
	return slapi_valueset_next_value( &a->a_deleted_values, hint, v );
}

/* 
 * Note: We are passing in the entry so that we may be able to "optimize"
 * the csn related information and roll it up higher to the level of entry.
 */
void
attr_purge_state_information(Slapi_Entry *entry, Slapi_Attr *attr, const CSN *csnUpTo)
{
	if(!valueset_isempty(&attr->a_deleted_values))
	{
		valueset_purge(&attr->a_deleted_values, csnUpTo);
	}
}

/*
 * Search an attribute for a value, it could be present, deleted, or not present.
 */
int
attr_value_find_wsi(Slapi_Attr *a, const struct berval *bval, Slapi_Value **value)
{
	int retVal=0;
	struct ava ava;

	PR_ASSERT(a!=NULL);
	PR_ASSERT(value!=NULL);

	/*
	 * we will first search the present values, and then, if
	 * necessary, the deleted values.
	 */
	ava.ava_type = a->a_type;
	ava.ava_value = *bval;
	ava.ava_private = NULL;
	retVal = plugin_call_syntax_filter_ava_sv(a, LDAP_FILTER_EQUALITY, &ava, value, 0 /* Present */);
	
	if(retVal==0)
	{
		/* we found the value, so we don't search the deleted list */
		retVal= VALUE_PRESENT;
	}
	else
	{
		retVal = plugin_call_syntax_filter_ava_sv(a, LDAP_FILTER_EQUALITY, &ava, value, 1 /* Deleted */);
		if(retVal==0)
		{
			/* It was on the deleted value list */
			retVal= VALUE_DELETED;
		}
		else
		{
			/* Couldn't find it */
			retVal= VALUE_NOTFOUND;
		}
	}
	
	return retVal;
}

int
slapi_attr_add_value(Slapi_Attr *a, const Slapi_Value *v)
{
	slapi_valueset_add_value( &a->a_present_values, v);
	return 0;
}

int
slapi_attr_set_type(Slapi_Attr *a, const char *type)
{
	int rc = 0;

	if((NULL == a) || (NULL == type)) {
		rc = -1;
	} else {
		slapi_ch_free_string(&a->a_type);
		a->a_type = slapi_ch_strdup(type);
	}
	return rc;
}

/* Make the valuset in Slapi_Attr be *vs--not a copy */
int
slapi_attr_set_valueset(Slapi_Attr *a, const Slapi_ValueSet *vs)
{
	slapi_valueset_set_valueset( &a->a_present_values, vs);
    return 0;
}

int 
attr_add_deleted_value(Slapi_Attr *a, const Slapi_Value *v)
{
    slapi_valueset_add_value( &a->a_deleted_values, v);
	return 0;
}

/*
 * Add a value array to an attribute. 
 * If more than one values are being added, we build an AVL tree of any existing
 * values and then update that in parallel with the existing values.  This
 * AVL tree is used to detect the duplicates not only between the existing 
 * values and to-be-added values but also among the to-be-added values.
 * The AVL tree is created and destroyed all within this function.
 *
 * Returns
 * LDAP_SUCCESS - OK
 * LDAP_OPERATIONS_ERROR - Existing duplicates in attribute.
 * LDAP_TYPE_OR_VALUE_EXISTS - Duplicate values.
 */
int
attr_add_valuearray(Slapi_Attr *a, Slapi_Value **vals, const char *dn)
{
    int i = 0;
    int numofvals = 0;
    int duplicate_index = -1;
    int was_present_null = 0;
    int rc = LDAP_SUCCESS;

    if (valuearray_isempty(vals)) {
        /*
         * No values to add (unexpected but acceptable).
         */
        return rc;
    }

    /*
     * add values and check for duplicate values
     */
    numofvals = valuearray_count(vals);
    rc = slapi_valueset_add_attr_valuearray_ext (a, &a->a_present_values, vals, numofvals, SLAPI_VALUE_FLAG_DUPCHECK, &duplicate_index);
    if ( rc != LDAP_SUCCESS) {
#if defined(USE_OLD_UNHASHED)
                if (is_type_forbidden(a->a_type)) {
                    /* If the attr is in the forbidden list
                     * (e.g., unhashed password),
                     * we don't return any useful info to the clients. */
                    rc = LDAP_OTHER;
                } else {
                    rc = LDAP_TYPE_OR_VALUE_EXISTS;
                }
#else
                rc = LDAP_TYPE_OR_VALUE_EXISTS;
#endif
            }

    /* In the case of duplicate value, rc == LDAP_TYPE_OR_VALUE_EXISTS or
     * LDAP_OPERATIONS_ERROR
     */
    if ( duplicate_index >= 0 ) {
        char bvvalcopy[BUFSIZ];
        char *duplicate_string = "null or non-ASCII";

        i = 0;
        while ( (unsigned int)i < vals[duplicate_index]->bv.bv_len &&
                i < BUFSIZ - 1 &&
                vals[duplicate_index]->bv.bv_val[i] &&
                isascii ( vals[duplicate_index]->bv.bv_val[i] )) {
            i++;
        }

        if ( i ) {
            if ( vals[duplicate_index]->bv.bv_val[i] == 0 ) {
                duplicate_string = vals[duplicate_index]->bv.bv_val;
            }
            else {
                strncpy ( &bvvalcopy[0], vals[duplicate_index]->bv.bv_val, i );
                bvvalcopy[i] = '\0';
                duplicate_string = bvvalcopy;
            }
        }

        slapi_log_error( SLAPI_LOG_TRACE, NULL, "add value \"%s\" to "
                "attribute type \"%s\" in entry \"%s\" failed: %s\n", 
                duplicate_string,
                a->a_type,
                dn ? dn : "<null>", 
                (was_present_null ? "duplicate new value" : "value exists"));
    }
    return( rc );
}

/* quickly toss an attribute's values and replace them with new ones
 * (used by attrlist_replace_fast)
 * Returns
 * LDAP_SUCCESS - OK
 * LDAP_OPERATIONS_ERROR - Existing duplicates in attribute.
 */
int attr_replace(Slapi_Attr *a, Slapi_Value **vals)
{
    return valueset_replace_valuearray(a, &a->a_present_values, vals);
}

int 
attr_check_onoff ( const char *attr_name, char *value, long minval, long maxval, char *errorbuf )
{
	int retVal = LDAP_SUCCESS;

	if ( strcasecmp ( value, "on" ) != 0 &&
		strcasecmp ( value, "off") != 0 &&
		strcasecmp ( value, "1" ) != 0 &&
		strcasecmp ( value, "0" ) != 0 &&
		strcasecmp ( value, "true" ) != 0 &&
		strcasecmp ( value, "false" ) != 0 ) {
			PR_snprintf ( errorbuf, BUFSIZ,
			"%s: invalid value \"%s\".", attr_name, value );
		retVal = LDAP_CONSTRAINT_VIOLATION;
	}

	return retVal;
}

int 
attr_check_minmax ( const char *attr_name, char *value, long minval, long maxval, char *errorbuf )
{
	int retVal = LDAP_SUCCESS;
	long val;

	val = strtol(value, NULL, 0);
	if ( (minval != -1 ? (val < minval ? 1 : 0) : 0) ||
		 (maxval != -1 ? (val > maxval ? 1 : 0) : 0) ) {
		PR_snprintf ( errorbuf, BUFSIZ, 
			"%s: invalid value \"%s\".",
			attr_name, value );
		retVal = LDAP_CONSTRAINT_VIOLATION;
	}

	return retVal;
}

/**
   Returns the function which can be used to compare (like memcmp/strcmp)
   two values of this type of attribute.  The comparison function will use
   the ORDERING matching rule if available, or the default comparison
   function from the syntax plugin.
   Note: if there is no ORDERING matching rule, and the syntax does not
   provide an ordered compare function, this function will return
   LDAP_PROTOCOL_ERROR and compare_fn will be NULL.
   Returns LDAP_SUCCESS if successful and sets *compare_fn to the function.
 */
int
attr_get_value_cmp_fn(const Slapi_Attr *attr, value_compare_fn_type *compare_fn)
{
	int rc = LDAP_PROTOCOL_ERROR;

	LDAPDebug0Args(LDAP_DEBUG_TRACE,
					"=> slapi_attr_get_value_cmp_fn\n");

	*compare_fn = NULL;

	if (attr == NULL) {
		LDAPDebug0Args(LDAP_DEBUG_TRACE,
						"<= slapi_attr_get_value_cmp_fn no attribute given\n");
		rc = LDAP_PARAM_ERROR; /* unkonwn */
		goto done;
	}

	if (attr->a_mr_ord_plugin && attr->a_mr_ord_plugin->plg_mr_compare) {
		*compare_fn = (value_compare_fn_type) attr->a_mr_ord_plugin->plg_mr_compare;
		rc = LDAP_SUCCESS;
		goto done;
	}

	if ((attr->a_plugin->plg_syntax_flags & SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING) == 0) {
		LDAPDebug2Args(LDAP_DEBUG_TRACE,
					   "<= slapi_attr_get_value_cmp_fn syntax [%s] for attribute [%s] does not support ordering\n",
					   attr->a_plugin->plg_syntax_oid, attr->a_type);
		goto done;
	}

	if (attr->a_plugin->plg_syntax_filter_ava == NULL) {
		LDAPDebug2Args(LDAP_DEBUG_TRACE,
					   "<= slapi_attr_get_value_cmp_fn syntax [%s] for attribute [%s] does not support equality matching\n",
					   attr->a_plugin->plg_syntax_oid, attr->a_type);
		goto done;
	}

	if (attr->a_plugin->plg_syntax_compare == NULL) {
		LDAPDebug2Args(LDAP_DEBUG_TRACE,
					   "<= slapi_attr_get_value_cmp_fn syntax [%s] for attribute [%s] does not have a compare function\n",
					   attr->a_plugin->plg_syntax_oid, attr->a_type);
		goto done;
	}

	*compare_fn = (value_compare_fn_type)attr->a_plugin->plg_syntax_compare;
	rc = LDAP_SUCCESS;

done:
	LDAPDebug0Args(LDAP_DEBUG_TRACE, "<= slapi_attr_get_value_cmp_fn \n");
	return rc;
}

const char *
attr_get_syntax_oid(const Slapi_Attr *attr)
{
	if (attr->a_plugin) {
		return attr->a_plugin->plg_syntax_oid;
	} else {
		return NULL;
	}
}
