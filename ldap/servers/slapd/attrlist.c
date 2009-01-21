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


#include "slap.h"

void
attrlist_free(Slapi_Attr *alist)
{
	Slapi_Attr *a, *next;
	for ( a = alist; a != NULL; a = next )
	{
		next = a->a_next;
		slapi_attr_free( &a );
	}
}

/*
 * Search for the attribute.
 * If not found then create it,
 * and add it to the end of the list.
 * Return 0 for found, 1 for created.
 */
int
attrlist_find_or_create(Slapi_Attr **alist, const char *type, Slapi_Attr ***a)
{
	return attrlist_find_or_create_locking_optional(alist, type, a, PR_TRUE);
}

int
attrlist_find_or_create_locking_optional(Slapi_Attr **alist, const char *type, Slapi_Attr ***a, PRBool use_lock)
{
	int rc= 0; /* found */
	if ( *a==NULL )
	{
		for ( *a = alist; **a != NULL; *a = &(**a)->a_next ) {
			if ( strcasecmp( (**a)->a_type, type ) == 0 ) {
				break;
			}
		}
	}

	if( **a==NULL )
	{
		**a = slapi_attr_new();
		slapi_attr_init_locking_optional(**a, type, use_lock);
		rc= 1; /* created */
	}
	return rc;
}

/*
 * attrlist_merge - merge the given type and value with the list of
 * attributes in attrs.
 */
void
attrlist_merge(Slapi_Attr **alist, const char *type, struct berval **vals)
{
	Slapi_Value **values= NULL;
	valuearray_init_bervalarray(vals,&values); /* JCM SLOW FUNCTION */
	attrlist_merge_valuearray(alist,type,values);
	valuearray_free(&values);
}


/*
 * attrlist_merge_valuearray - merge the given type and value with the list of
 * attributes in attrs.
 */
void
attrlist_merge_valuearray(Slapi_Attr **alist, const char *type, Slapi_Value **vals)
{
	Slapi_Attr	**a= NULL;
	attrlist_find_or_create(alist, type, &a);
	valueset_add_valuearray( &(*a)->a_present_values, vals );
}    


/*
 * attrlist_find - find and return attribute type in list a
 */

Slapi_Attr *
attrlist_find(Slapi_Attr *a, const char *type)
{
	for ( ; a != NULL; a = a->a_next ) {
		if ( strcasecmp( a->a_type, type ) == 0 ) {
			return( a );
		}
	}

	return( NULL );
}


/*
 * attrlist_count_subtypes
 *
 * Returns a count attributes which conform to type
 * in the attr list a.  This count includes all subtypes of
 * type
 */
int
attrlist_count_subtypes(Slapi_Attr *a, const char *type)
{
	int counter = 0;

	for ( ; a != NULL; a = a->a_next ) {
		if ( slapi_attr_type_cmp( type , a->a_type, SLAPI_TYPE_CMP_SUBTYPE) == 0 ) {
			counter++;
		}
	}

	return( counter );
}

/*
 * attrlist_find_ex
 *
 * Finds the first subtype in the list which matches "type"
 * starting at the beginning or hint depending on whether
 * hint has a value
 * 
 * It is intended that hint be zero when first called and then
 * passed back in on subsequent calls until 0 is returned to mark
 * the end of the filtered list
 */
Slapi_Attr *
attrlist_find_ex(
    Slapi_Attr *a, 
    const char *type, 
    int *type_name_disposition, /* pass null if you're not interested */
    char** actual_type_name,    /* pass null if you're not interested */
    void **hint
)
{
	Slapi_Attr **attr_cursor = (Slapi_Attr **)hint;

	if (type_name_disposition) *type_name_disposition = 0;
	if (actual_type_name) *actual_type_name = NULL;

	if(*attr_cursor == NULL)
            *attr_cursor = a; /* start at the beginning of the list */
        else 
            *attr_cursor = (*attr_cursor)->a_next;

	while(*attr_cursor != NULL) {

		/* Determine whether the two types are related:*/
		if ( slapi_attr_type_cmp( type , (*attr_cursor)->a_type, SLAPI_TYPE_CMP_SUBTYPE) == 0 ) {
			/* We got a match. Now figure out if we matched because it was a subtype */
                    if (type_name_disposition) {
			if ( 0 == slapi_attr_type_cmp( type , (*attr_cursor)->a_type, SLAPI_TYPE_CMP_EXACT) ) {
				*type_name_disposition = SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS;
			} else {
				*type_name_disposition = SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE;
			}
                    }

                    if (actual_type_name) {
			*actual_type_name = (*attr_cursor)->a_type;
                    }

                    a = *attr_cursor;  /* the attribute to return */
                    return( a );

		}

		*attr_cursor = (*attr_cursor)->a_next; /* no match, move cursor */
	}

	return( NULL );
}

/*
 * attr_remove - remove the attribute from the list of attributes
 */
Slapi_Attr *
attrlist_remove(Slapi_Attr **attrs, const char *type)
{
	Slapi_Attr **a;
	Slapi_Attr *save= NULL;
	for ( a = attrs; *a != NULL; a = &(*a)->a_next )
	{
		if ( strcasecmp( (*a)->a_type, type ) == 0 )
		{
			break;
		}
	}
	if (*a != NULL)
	{
		save = *a;
		*a = (*a)->a_next;
	}
	return save;
}

void
attrlist_add(Slapi_Attr **attrs, Slapi_Attr *a)
{
	a->a_next= *attrs;
	*attrs= a;
}

/*
 * attrlist_delete - delete the attribute type in list pointed to by attrs
 * return	0	deleted ok
 * 		1	not found in list a
 * 		-1	something bad happened
 */

int
attrlist_delete(Slapi_Attr **attrs, const char *type)
{
	Slapi_Attr	**a;
	Slapi_Attr	*save;

	for ( a = attrs; *a != NULL; a = &(*a)->a_next ) {
		if ( strcasecmp( (*a)->a_type, type ) == 0 ) {
			break;
		}
	}

	if ( *a == NULL ) {
		return( 1 );
	}

	save = *a;
	*a = (*a)->a_next;
	slapi_attr_free( &save );

	return( 0 );
}

/*
 * attrlist_replace - replace the attribute value(s) with this value(s)
 *
 * Returns
 * LDAP_SUCCESS - OK (including the attr not found)
 * LDAP_OPERATIONS_ERROR - Existing duplicates in attribute.
 */
int attrlist_replace(Slapi_Attr **alist, const char *type, struct berval **vals)
{
    Slapi_Attr **a = NULL;
    Slapi_Value **values = NULL;
    int rc = LDAP_SUCCESS;

    if (vals == NULL || vals[0] == NULL) {
        (void)attrlist_delete(alist, type);
    } else {
        attrlist_find_or_create(alist, type, &a);
        valuearray_init_bervalarray(vals, &values);
        rc = attr_replace(*a, values);
    }
    return rc;
}

/*
 * attrlist_replace_with_flags - replace the attribute value(s) with this value(s)
 *
 * Returns
 * LDAP_SUCCESS - OK (including the attr not found)
 * LDAP_OPERATIONS_ERROR - Existing duplicates in attribute.
 */
int attrlist_replace_with_flags(Slapi_Attr **alist, const char *type, struct berval **vals, int flags)
{
    Slapi_Attr **a = NULL;
    Slapi_Value **values = NULL;
    int rc = LDAP_SUCCESS;

    if (vals == NULL || vals[0] == NULL) {
        (void)attrlist_delete(alist, type);
    } else {
        attrlist_find_or_create(alist, type, &a);
        valuearray_init_bervalarray_with_flags(vals, &values, flags);
        rc = attr_replace(*a, values);
    }
    return rc;
}

