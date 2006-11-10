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
 * Routines to parse schema LDIF
 *
 * -atom
 *
 */
 
#ifndef __DSALIB_SCHEMA_H
#define __DSALIB_SCHEMA_H__



/************************************************************************

 BNF for attributes and objectclasses:

 AttributeTypeDescription = "(" whsp
            numericoid whsp              ; AttributeType identifier
          [ "NAME" qdescrs ]             ; name used in AttributeType
          [ "DESC" qdstring ]            ; description
          [ "OBSOLETE" whsp ]
          [ "SUP" woid ]                 ; derived from this other 
                                         ; AttributeType
          [ "EQUALITY" woid              ; Matching Rule name
          [ "ORDERING" woid              ; Matching Rule name
          [ "SUBSTR" woid ]              ; Matching Rule name 
          [ "SYNTAX" whsp noidlen whsp ] ; see section 4.3
          [ "SINGLE-VALUE" whsp ]        ; default multi-valued
          [ "COLLECTIVE" whsp ]          ; default not collective
          [ "NO-USER-MODIFICATION" whsp ]; default user modifiable
          [ "USAGE" whsp AttributeUsage ]; default user applications
          whsp ")"
    


 ObjectClassDescription = "(" whsp
          numericoid whsp      ; ObjectClass identifier
          [ "NAME" qdescrs ]
          [ "DESC" qdstring ]
          [ "OBSOLETE" whsp ]
          [ "SUP" oids ]       ; Superior ObjectClasses
          [ ( "ABSTRACT" / "STRUCTURAL" / "AUXILIARY" ) whsp ] 
                               ; default structural
          [ "MUST" oids ]      ; AttributeTypes
          [ "MAY" oids ]       ; AttributeTypes
      whsp ")"


************************************************************************/


/* 
 * ds_check_valid_oid: check to see if an oid is valid.
 * Oids should only contain digits and dots.
 *
 * returns 1 if valid, 0 if not
 */

DS_EXPORT_SYMBOL int ds_check_valid_oid (char *oid);


/*
 * ds_check_valid_name: check to see if an attribute name or an objectclass
 * name is valid. A valid name contains only digits, letters, or hyphens 
 *
 * returns 1 if valid, 0 if not
 *
 */

DS_EXPORT_SYMBOL int ds_check_valid_name (char *name);

/*
 * ds_get_oc_desc: 
 *
 * Input   : pointer to string containing an ObjectClassDescription
 * Returns : pointer to string containing objectclass DESC
 *
 * The caller must free the return value
 *
 */

DS_EXPORT_SYMBOL char * ds_get_oc_desc (char *oc);


/*
 * ds_get_oc_name: 
 *
 * Input  : pointer to string containing an ObjectClassDescription
 * Returns: pointer to string containing objectclass name.
 *
 * The caller must free the return value
 *
 */

DS_EXPORT_SYMBOL char *ds_get_oc_name (char *o);


/*
 * ds_get_attr_name:
 *
 * Input  : pointer to string containing an AttributeTypeDescription
 * Returns: pointer to string containing an attribute name.
 *
 * The caller must free the return value
 *
 */

DS_EXPORT_SYMBOL char *ds_get_attr_name (char *a);
  


/*
 * ds_get_oc_superior:
 *
 * Input  : pointer to string containing an ObjectClassDescription 
 * Returns: pointer to string containing the objectclass's SUP (superior/parent)
 *          objectclass
 *
 * The caller must free the return value
 *
 */

DS_EXPORT_SYMBOL char *ds_get_oc_superior (char *o);


/*
 * ds_get_attr_desc: 
 *
 * Input  : Pointer to string containing an AttributeTypeDescription
 * Returns: Pointer to string containing the attribute's description
 *
 * The caller must free the return value
 *
 */

DS_EXPORT_SYMBOL char *ds_get_attr_desc (char *a);


/*
 * ds_get_attr_syntax: 
 *
 * Input:   Pointer to string containing an AttributeTypeDescription
 * Returns: Pointer to string containing the attribute's syntax
 *
 * The caller must free the return value
 *
 */

DS_EXPORT_SYMBOL char *ds_get_attr_syntax (char *a);


/*
 * ds_get_attr_oid: 
 * 
 * Input  : Pointer to string containing an AttributeTypeDescription
 * Returns: Pointer to string containing an attribute's  oid
 *
 * The caller must free the return value
 *
 */
DS_EXPORT_SYMBOL char *ds_get_attr_oid (char *a);


/*
 * ds_get_attr_name: 
 *
 * Input  : Pointer to string containing an AttributeTypeDescription
 * Returns: Pointer to string containing the attribute's name
 *
 * The caller must free the return value
 *
 */

DS_EXPORT_SYMBOL char *ds_get_attr_name (char *a);



/*
 * syntax_oid_to_english: convert an attribute syntax oid to something more
 *                        human readable
 *
 * Input  : string containing numeric OID for a attribute syntax 
 * Returns: Human readable string
 */


DS_EXPORT_SYMBOL char *syntax_oid_to_english (char *oid);


/* StripSpaces: Remove all leading and trailing spaces from a string */

DS_EXPORT_SYMBOL char *StripSpaces (char **s);


/* ds_print_required_attrs:
 *
 * input: pointer to string containing an ObjectClassDescription
 *
 * prints JavaScript array containing the required attributes of an objectclass
 * The array name is oc_<objectclass name>_requires
 */

DS_EXPORT_SYMBOL void ds_print_required_attrs (char *o);


/* ds_print_allowed_attrs:
 *
 * input: pointer to string containing an ObjectClassDescription
 *
 * prints JavaScript array containing the allowed attributes of an objectclass
 * The array name is oc_<objectclass name>_allows
 */
DS_EXPORT_SYMBOL void ds_print_allowed_attrs (char *o);


/* ds_print_oc_oid:
 *
 * input: pointer to string containing an ObjectClassDescription
 *
 * prints JavaScript string containing an objectclass oid
 * The variable name is oc_<objectclass name>_oid
 */

DS_EXPORT_SYMBOL void ds_print_oc_oid (char *o);

/* ds_print_oc_superior:
 *
 * input: pointer to string containing an ObjectClassDescription
 *
 * prints JavaScript string containing an objectclass superior
 * The variable name is oc_<objectclass name>_superior
 */

DS_EXPORT_SYMBOL void ds_print_oc_superior (char *o); 
		   

/* underscore2hyphen: 
 *   transform underscores to hyphens in a string 
 */

DS_EXPORT_SYMBOL char *underscore2hyphen (char *src);

/* hyphen2underscore: 
 *  transform hyphens to underscores in a string 
 */

DS_EXPORT_SYMBOL char *hyphen2underscore (char *src);


#endif /* __DSALIB_SCHEMA_H__  */
