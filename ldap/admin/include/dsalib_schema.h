/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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
