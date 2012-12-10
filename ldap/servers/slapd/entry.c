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

/* entry.c - routines for dealing with entries */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#undef DEBUG                    /* disable counters */
#include <prcountr.h>
#include "slap.h"


#undef ENTRY_DEBUG

#define DELETED_ATTR_STRING ";deletedattribute"
#define DELETED_ATTR_STRSIZE 17 /* sizeof(";deletedattribute") */
#define DELETED_VALUE_STRING ";deleted"
#define DELETED_VALUE_STRSIZE 8 /* sizeof(";deleted") */

/* a helper function to set special rdn to a tombstone entry */
static int _entry_set_tombstone_rdn(Slapi_Entry *e, const char *normdn);

/* protected attributes which are not included in the flattened entry,
 * which will be stored in the db. */
#if defined(USE_OLD_UNHASHED)
static char *protected_attrs_all [] = {PSEUDO_ATTR_UNHASHEDUSERPASSWORD,
                                       SLAPI_ATTR_ENTRYDN,
                                       NULL};

static char *forbidden_attrs [] = {PSEUDO_ATTR_UNHASHEDUSERPASSWORD,
                                   NULL};
#else
static char *protected_attrs_all [] = {SLAPI_ATTR_ENTRYDN,
                                       NULL};
#endif
/* Attributes which are put into the entry extension */
struct attrs_in_extension attrs_in_extension[] =
{
    {PSEUDO_ATTR_UNHASHEDUSERPASSWORD,
     slapi_pw_get_entry_ext,
     slapi_pw_set_entry_ext,
     pw_copy_entry_ext},
    {NULL, NULL, NULL}
};

/*
 * An attribute name is of the form 'basename[;option]'.
 * The state informaion is encoded in options. For example:
 *
 * telephonenumber;vucsn-011111111222233334444: 1 650 937 5739
 *
 * This function strips out the csn options, leaving behind a
 * type with any non-csn options left intact.
 */
/*
 * WARNING: s gets butchered... the base type remains.
 */
void
str2entry_state_information_from_type(char *s,CSNSet **csnset,CSN **attributedeletioncsn,CSN **maxcsn,int *value_state,int *attr_state)
{
	char *p= strchr(s, ';');
	*value_state= VALUE_PRESENT;
	*attr_state= ATTRIBUTE_PRESENT;
	while(p!=NULL)
	{
		if(p[3]=='c' && p[4]=='s' && p[5]=='n' && p[6]=='-')
		{
			CSNType t= CSN_TYPE_UNKNOWN;
			if(p[1]=='x' && p[2]=='1')
			{
				t= CSN_TYPE_UNKNOWN;
			}
			if(p[1]=='x' && p[2]=='2')
			{
				t= CSN_TYPE_NONE;
			}
			if(p[1]=='a' && p[2]=='d')
			{
				t= CSN_TYPE_ATTRIBUTE_DELETED;
			}
			if(p[1]=='v' && p[2]=='u')
			{
				t= CSN_TYPE_VALUE_UPDATED;
			}
			if(p[1]=='v' && p[2]=='d')
			{
				t= CSN_TYPE_VALUE_DELETED;
			}
			if(p[1]=='m' && p[2]=='d')
			{
				t= CSN_TYPE_VALUE_DISTINGUISHED;
			}
			p[0]='\0';
			if(t!=CSN_TYPE_ATTRIBUTE_DELETED)
			{
				CSN csn;
				csn_init_by_string(&csn,p+7);
				csnset_add_csn(csnset,t,&csn);
				if ( *maxcsn == NULL )
				{
					*maxcsn = csn_dup ( &csn );
				}
				else if ( csn_compare (*maxcsn, &csn) < 0 )
				{
					csn_init_by_csn ( *maxcsn, &csn );
				}
			}
			else
			{
				*attributedeletioncsn= csn_new_by_string(p+7);
				if ( *maxcsn == NULL )
				{
					*maxcsn = csn_dup ( *attributedeletioncsn );
				}
				else if ( csn_compare (*maxcsn, *attributedeletioncsn) < 0 )
				{
					csn_init_by_csn ( *maxcsn, *attributedeletioncsn );
				}
			}
		}
		else if(strncmp(p+1,"deletedattribute", 16)==0)
		{
			p[0]='\0';
			*attr_state= ATTRIBUTE_DELETED;
		}
		else if(strncmp(p+1,"deleted", 7)==0)
		{
			p[0]='\0';
			*value_state= VALUE_DELETED;
		}
		p= strchr(p+1, ';');
	}
}

/* rawdn is not consumed.  Caller needs to free it. */
static Slapi_Entry *
str2entry_fast( const char *rawdn, char *s, int flags, int read_stateinfo )
{
	Slapi_Entry	*e;
	char		*next, *ptype=NULL;
	int nvals= 0;
	int del_nvals= 0;
	unsigned long attr_val_cnt = 0;
	CSN *attributedeletioncsn= NULL; /* Moved to this level so that the JCM csn_free call below gets useful */
	CSNSet *valuecsnset= NULL; /* Moved to this level so that the JCM csn_free call below gets useful */
	CSN *maxcsn = NULL;
	char *normdn = NULL;
	int strict = 0;
	Slapi_Attr **a = NULL;

    /* Check if we should be performing strict validation. */
    strict = config_get_dn_validate_strict();

	/*
	 * In string format, an entry looks like either of these:
	 *
	 *	dn: <dn>\n
	 *	[<attr>:[:] <value>\n]
	 *	[<tab><continuedvalue>\n]*
	 *	...
	 *
	 *	rdn: <rdn>\n
	 *	[<attr>:[:] <value>\n]
	 *	[<tab><continuedvalue>\n]*
	 *	...
	 *
	 * If a double colon is used after a type, it means the
	 * following value is encoded as a base 64 string.  This
	 * happens if the value contains a non-printing character
	 * or newline.
	 *
	 * In case an entry starts with rdn:, dn must be provided.
	 */

	LDAPDebug( LDAP_DEBUG_TRACE, "=> str2entry_fast\n", 0, 0, 0 );

	e = slapi_entry_alloc();
	slapi_entry_init(e,NULL,NULL);

	/* dn|rdn + attributes */
	next = s;

	/* get the read lock of name2asi for performance purpose.
	   It reduces read locking by per-entry lock, instead of per-attribute.
	*/
	attr_syntax_read_lock();

	while ( (s = ldif_getline( &next )) != NULL &&
	         attr_val_cnt < ENTRY_MAX_ATTRIBUTE_VALUE_COUNT )
	{
		struct berval type = {0, NULL};
		struct berval value = {0, NULL};
		int freeval = 0;
		int value_state= VALUE_NOTFOUND;
		int attr_state= ATTRIBUTE_NOTFOUND;
		int maxvals;
		int del_maxvals;

		if ( *s == '\n' || *s == '\0' ) {
			break;
		}

		if ( slapi_ldif_parse_line( s, &type, &value, &freeval ) < 0 ) {
			LDAPDebug( LDAP_DEBUG_TRACE,
			    "<= str2entry_fast NULL (parse_line)\n", 0, 0, 0 );
			continue;
		}

		/*
		 * Extract the attribute and value CSNs from the attribute type.
		 */		
		csn_free(&attributedeletioncsn); /* JCM - Do this more efficiently */
		csnset_free(&valuecsnset);
		value_state= VALUE_NOTFOUND;
		attr_state= ATTRIBUTE_NOTFOUND;
		str2entry_state_information_from_type(type.bv_val,&valuecsnset,&attributedeletioncsn,&maxcsn,&value_state,&attr_state);
		if(!read_stateinfo)
		{
			/* We are not maintaining state information */
			if(value_state==VALUE_DELETED || attr_state==ATTRIBUTE_DELETED)
			{
				/* ignore deleted values and attributes */
				/* the memory below was not allocated by the slapi_ch_ functions */
				if (freeval) slapi_ch_free_string(&value.bv_val);
				continue;
			}
			/* Ignore CSNs */
			csn_free(&attributedeletioncsn);
			csnset_free(&valuecsnset);
		}
		/*
		 * We cache some stuff as we go around the loop.
		 */
		if((ptype==NULL)||(PL_strcasecmp(type.bv_val,ptype) != 0))
		{
			slapi_ch_free_string(&ptype);
			ptype=PL_strndup(type.bv_val, type.bv_len);
			nvals = 0;
			maxvals = 0;
			del_nvals = 0;
			del_maxvals = 0;
			a = NULL;
		}

		if ( rawdn ) {
			if ( NULL == slapi_entry_get_dn_const( e )) {
				if (flags & SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT) {
					normdn = 
						slapi_dn_normalize_original(slapi_ch_strdup(rawdn));
				} else {
					if (flags & SLAPI_STR2ENTRY_DN_NORMALIZED) {
						normdn = slapi_ch_strdup(rawdn);
					} else {
						normdn = slapi_create_dn_string("%s", rawdn);
						if (NULL == normdn) {
							LDAPDebug1Arg(LDAP_DEBUG_TRACE,
							     "str2entry_fast: Invalid DN: %s\n", rawdn);
							slapi_entry_free( e );
							if (freeval) slapi_ch_free_string(&value.bv_val);
							e = NULL;
							goto done;
						}
					}
				}
				/* normdn is consumed in e */
				slapi_entry_set_normdn(e, normdn);
			}
			if ( NULL == slapi_entry_get_rdn_const( e )) {
				if (normdn) {
					/* normdn is just referred in slapi_entry_set_rdn. */
					slapi_entry_set_rdn(e, normdn);
				} else {
					if (flags & SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT) {
						normdn = 
							slapi_dn_normalize_original(slapi_ch_strdup(rawdn));
					} else {
						if (flags & SLAPI_STR2ENTRY_DN_NORMALIZED) {
							normdn = slapi_ch_strdup(rawdn);
						} else {
							normdn = slapi_create_dn_string("%s", rawdn);
							if (NULL == normdn) {
								LDAPDebug1Arg(LDAP_DEBUG_TRACE,
								    "str2entry_fast: Invalid DN: %s\n", rawdn);
								slapi_entry_free( e );
								if (freeval) 
								    slapi_ch_free_string(&value.bv_val);
								e = NULL;
								goto done;
							}
						}
					}
					/* normdn is just referred in slapi_entry_set_rdn. */
					slapi_entry_set_rdn(e, normdn);
					slapi_ch_free_string(&normdn);
				}
			}
			rawdn = NULL; /* Set once in the loop. 
							 This won't affect the caller's passed address. */
		}
		if ( PL_strncasecmp( type.bv_val, "dn", type.bv_len ) == 0 ) {
			if ( slapi_entry_get_dn_const(e)!=NULL ) {
				char ebuf[ BUFSIZ ];
				LDAPDebug( LDAP_DEBUG_TRACE,
					"str2entry_fast: entry has multiple dns \"%s\" and "
					"\"%s\" (second ignored)\n",
					slapi_entry_get_dn_const(e),
					escape_string( value.bv_val, ebuf ), 0 );
				/* the memory below was not allocated by the slapi_ch_ functions */
				if (freeval) slapi_ch_free_string(&value.bv_val);
				continue;
			}
			if (flags & SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT) {
				normdn = 
					slapi_ch_strdup(slapi_dn_normalize_original(value.bv_val));
			} else {
				normdn = slapi_create_dn_string("%s", value.bv_val);
			}
			if (NULL == normdn) {
				char ebuf[ BUFSIZ ];
				LDAPDebug1Arg(LDAP_DEBUG_TRACE,
							  "str2entry_fast: Invalid DN: %s\n",
                              escape_string( value.bv_val, ebuf ));
				slapi_entry_free( e );
				if (freeval) slapi_ch_free_string(&value.bv_val);
				e = NULL;
				goto done;
			}
			/* normdn is consumed in e */
			slapi_entry_set_normdn(e, normdn);

			/* the memory below was not allocated by the slapi_ch_ functions */
			if (freeval) slapi_ch_free_string(&value.bv_val);
			continue;
		}

		if ( PL_strncasecmp( type.bv_val, "rdn", type.bv_len ) == 0 ) {
			if ( NULL == slapi_entry_get_rdn_const( e )) {
				slapi_entry_set_rdn( e, value.bv_val );
			}
			/* the memory below was not allocated by the slapi_ch_ functions */
			if (freeval) slapi_ch_free_string(&value.bv_val);
			continue;
		}

		/* If SLAPI_STR2ENTRY_NO_ENTRYDN is set, skip entrydn */
		if ( (flags & SLAPI_STR2ENTRY_NO_ENTRYDN) &&
		     PL_strncasecmp( type.bv_val, "entrydn", type.bv_len ) == 0 ) {
			if (freeval) slapi_ch_free_string(&value.bv_val);
			continue;
		}

		/* retrieve uniqueid */
		if ( PL_strncasecmp (type.bv_val, SLAPI_ATTR_UNIQUEID, type.bv_len) == 0 ){

			if (e->e_uniqueid != NULL){
				LDAPDebug (LDAP_DEBUG_TRACE, 
						   "str2entry_fast: entry has multiple uniqueids %s "
						   "and %s (second ignored)\n",
						   e->e_uniqueid, value.bv_val, 0);
			}else{
				/* name2asi will be locked in slapi_entry_set_uniqueid */
				attr_syntax_unlock_read(); 
				slapi_entry_set_uniqueid (e, PL_strndup(value.bv_val, value.bv_len));
				attr_syntax_read_lock();
			}
			/* the memory below was not allocated by the slapi_ch_ functions */
			if (freeval) slapi_ch_free_string(&value.bv_val);
			continue;
		}
		
		if (PL_strncasecmp(type.bv_val,"objectclass",type.bv_len) == 0) {
			if (PL_strncasecmp(value.bv_val,"ldapsubentry",value.bv_len) == 0)
				e->e_flags |= SLAPI_ENTRY_LDAPSUBENTRY;
			if (PL_strncasecmp(value.bv_val, SLAPI_ATTR_VALUE_TOMBSTONE,value.bv_len) == 0)
				e->e_flags |= SLAPI_ENTRY_FLAG_TOMBSTONE;
		}
		
		{
			Slapi_Value	*svalue = NULL;
			if(a==NULL)
			{
				switch(attr_state)
				{
				case ATTRIBUTE_PRESENT:
					if(attrlist_find_or_create_locking_optional(&e->e_attrs, type.bv_val, &a, PR_FALSE)==0 /* Found */)
					{
						LDAPDebug (LDAP_DEBUG_ANY, "str2entry_fast: Error. Non-contiguous attribute values for %s\n", type.bv_val, 0, 0);
						PR_ASSERT(0);
						continue;
					}
					break;
				case ATTRIBUTE_DELETED:
					if(attrlist_find_or_create_locking_optional(&e->e_deleted_attrs, type.bv_val, &a, PR_FALSE)==0 /* Found */)
					{
						LDAPDebug (LDAP_DEBUG_ANY, "str2entry_fast: Error. Non-contiguous deleted attribute values for %s\n", type.bv_val, 0, 0);
						PR_ASSERT(0);
						continue;
					}
					break;
				case ATTRIBUTE_NOTFOUND:
					LDAPDebug (LDAP_DEBUG_ANY, "str2entry_fast: Error. Non-contiguous deleted attribute values for %s\n", type.bv_val, 0, 0);
					PR_ASSERT(0);
					continue;
					/* break; ??? */
				}
			}
			/* moved the value setting code here to check Slapi_Attr 'a'
			 * to retrieve the attribute syntax info */
			svalue = value_new(NULL, CSN_TYPE_NONE, NULL);
			if (slapi_attr_is_dn_syntax_attr(*a)) {
				int rc = 0;
				char *dn_aval = NULL;
				if (strict) {
					/* check that the dn is formatted correctly */
					rc = slapi_dn_syntax_check(NULL, value.bv_val, 1);
					if (rc) { /* syntax check failed */
						LDAPDebug2Args(LDAP_DEBUG_TRACE, 
							"str2entry_fast: strict: Invalid DN value: %s: %s\n",
							type.bv_val, value.bv_val);
						slapi_entry_free( e );
						if (freeval) slapi_ch_free_string(&value.bv_val);
						e = NULL;
						goto done;
					}
				}
				if (flags & SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT) {
					dn_aval = slapi_dn_normalize_original(value.bv_val);
					slapi_value_set(svalue, dn_aval, strlen(dn_aval));
				} else {
					Slapi_DN *sdn = slapi_sdn_new_dn_byref(value.bv_val);
					/* Note: slapi_sdn_get_dn returns normalized DN with
					 * case-intact. Thus, the length of dn_aval is 
					 * slapi_sdn_get_ndn_len(sdn). */
					dn_aval = (char *)slapi_sdn_get_dn(sdn);
					slapi_value_set(svalue, (void *)dn_aval,
					                slapi_sdn_get_ndn_len(sdn));
					slapi_sdn_free(&sdn);
				}
			} else {
				slapi_value_set_berval(svalue, &value);
			}
			/* the memory below was not allocated by the slapi_ch_ functions */	
			if (freeval) slapi_ch_free_string(&value.bv_val);
			svalue->v_csnset = valuecsnset;
			valuecsnset = NULL;
			{
				const CSN *distinguishedcsn= csnset_get_csn_of_type(svalue->v_csnset,CSN_TYPE_VALUE_DISTINGUISHED);
				if(distinguishedcsn!=NULL)
				{
					entry_add_dncsn_ext(e,distinguishedcsn, ENTRY_DNCSN_INCREASING);
				}
			}
			if(value_state==VALUE_DELETED)
			{
				/* consumes the value */
				valuearray_add_value_fast(
					&(*a)->a_deleted_values.va, /* JCM .va is private */
					svalue,
					del_nvals,
					&del_maxvals,
					0/*!Exact*/,
					1/*Passin*/ );
				del_nvals++;
			}
			else
			{
				/* consumes the value */
				valuearray_add_value_fast(
					&(*a)->a_present_values.va, /* JCM .va is private */
					svalue, 
					nvals,
					&maxvals, 
					0 /*!Exact*/, 
					1 /*Passin*/ );
				nvals++;
			}
			if(attributedeletioncsn!=NULL)
			{
				attr_set_deletion_csn(*a,attributedeletioncsn);
			}
		}
		csn_free(&attributedeletioncsn);
		csnset_free(&valuecsnset);
		attr_val_cnt++;
	}
	slapi_ch_free_string(&ptype);
	if ( attr_val_cnt >= ENTRY_MAX_ATTRIBUTE_VALUE_COUNT )
	{
		LDAPDebug( LDAP_DEBUG_ANY,
		    "str2entry_fast: entry %s exceeded max attribute value cound %ld\n",
			slapi_entry_get_dn_const(e)?slapi_entry_get_dn_const(e):"unknown",
			attr_val_cnt, 0 );
	}
	if (read_stateinfo && maxcsn)
	{
		e->e_maxcsn = maxcsn;
		maxcsn = NULL;
	}

	/* release read lock of name2asi, per-entry lock */
	attr_syntax_unlock_read();

	/* If this is a tombstone, it requires a special treatment for rdn. */
	if (e->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
		/* tombstone */
		if (_entry_set_tombstone_rdn(e, slapi_entry_get_dn_const(e))) {
			LDAPDebug1Arg( LDAP_DEBUG_TRACE, "str2entry_fast: "
			               "tombstone entry has badly formatted dn: %s\n",
			               slapi_entry_get_dn_const(e) );
			slapi_entry_free( e ); e = NULL;
			goto done;
		}
	}

	/* check to make sure there was a dn: line */
	if ( slapi_entry_get_dn_const(e)==NULL ) {
		if (!(SLAPI_STR2ENTRY_INCLUDE_VERSION_STR & flags))
			LDAPDebug( LDAP_DEBUG_ANY, "str2entry_fast: entry has no dn\n",
									   0, 0, 0 );
		slapi_entry_free( e );
		e = NULL;
	}

done:
	csn_free(&attributedeletioncsn);
	csn_free(&maxcsn);
	LDAPDebug( LDAP_DEBUG_TRACE, "<= str2entry_fast 0x%x\n",
		e, 0, 0 );
	return( e );
}


#define	STR2ENTRY_SMALL_BUFFER_SIZE	64
#define STR2ENTRY_INITIAL_BERVAL_ARRAY_SIZE 8
#define STR2ENTRY_VALUE_DUPCHECK_THRESHOLD 5

typedef struct _entry_attr_data {
	int			ead_attrarrayindex;
	const char	*ead_attrtypename;
	char		ead_allocated;	/* non-zero if this struct needs to be freed */
} entry_attr_data;

/* Structure which stores a tree for the attributes on the entry rather than the linked list on a regular entry struture */
typedef struct _entry_attrs {
	Avlnode				*ea_attrlist;
	int					ea_attrdatacount;
	entry_attr_data		ea_attrdata[ STR2ENTRY_SMALL_BUFFER_SIZE ];
} entry_attrs;

typedef struct _str2entry_attr {
	char *sa_type;
	int sa_state;
 	struct valuearrayfast sa_present_values;
 	struct valuearrayfast sa_deleted_values;
	int sa_numdups;
	value_compare_fn_type sa_comparefn;
	Avlnode *sa_vtree;
	CSN *sa_attributedeletioncsn;
	Slapi_Attr sa_attr;
} str2entry_attr;

static void
entry_attr_init(str2entry_attr *sa, const char *type, int state)
{
    sa->sa_type= slapi_ch_strdup(type);
	sa->sa_state= state;
	valuearrayfast_init(&sa->sa_present_values,NULL);
	valuearrayfast_init(&sa->sa_deleted_values,NULL);
    sa->sa_numdups= 0;
	sa->sa_comparefn = NULL;
    sa->sa_vtree= NULL;
	sa->sa_attributedeletioncsn= NULL;
	slapi_attr_init(&sa->sa_attr, type);
}

/*
 * Create a tree of attributes.
 */
static int
entry_attrs_new(entry_attrs **pea)
{
	entry_attrs *tmp = (entry_attrs *)slapi_ch_calloc(1, sizeof(entry_attrs));
	if (NULL == tmp) {
		return -1;
	} else {
		*pea = tmp;
		return 0;
	}
}

/*
 * Delete an attribute type tree node.
 */
static void
attr_type_node_free( caddr_t data )
{
	entry_attr_data	*ea = (entry_attr_data *)data;
	if ( NULL != ea && ea->ead_allocated ) {
		slapi_ch_free( (void **)&ea );
	}
}


/*
 * Delete a tree of attributes.
 */
static void
entry_attrs_delete(entry_attrs **pea)
{
	if (NULL != *pea) {
		/* Delete the AVL tree */
		avl_free((*pea)->ea_attrlist, attr_type_node_free);
		slapi_ch_free((void**)pea);
	}
}

static int
attr_type_node_cmp( caddr_t d1, caddr_t d2 )
{
	/*
	 * A simple strcasecmp() will do here because we do not care
	 * about subtypes, etc. The slapi_str2entry() function treats
	 * subtypes as distinct attribute types, because that is how
	 * they are stored within the Slapi_Entry structure.
	 */
	entry_attr_data *ea1= (entry_attr_data *)d1;
	entry_attr_data *ea2= (entry_attr_data *)d2;
	PR_ASSERT( ea1 != NULL );
	PR_ASSERT( ea1->ead_attrtypename != NULL );
	PR_ASSERT( ea2 != NULL );
	PR_ASSERT( ea2->ead_attrtypename != NULL );
	return strcasecmp(ea1->ead_attrtypename,ea2->ead_attrtypename);
}

/*
 * Adds a new attribute to the attribute tree.
 */
static void
entry_attrs_add(entry_attrs *ea, const char *atname, int atarrayindex)
{
	entry_attr_data	*ead;

	if ( ea->ea_attrdatacount < STR2ENTRY_SMALL_BUFFER_SIZE ) {
		ead = &(ea->ea_attrdata[ ea->ea_attrdatacount ]);
		ead->ead_allocated = 0;
	} else {
		ead = (entry_attr_data *)slapi_ch_malloc( sizeof( entry_attr_data ));
		ead->ead_allocated = 1;
	}
	++ea->ea_attrdatacount;
	ead->ead_attrarrayindex = atarrayindex;
	ead->ead_attrtypename = atname;	/* a reference, not a strdup! */
		
	avl_insert( &(ea->ea_attrlist), ead, attr_type_node_cmp, avl_dup_error );
}

/*
 * Checks for an attribute in the tree.  Returns the attr array index or -1
 * if not found;
 */
static int
entry_attrs_find(entry_attrs *ea,char *type)
{
	entry_attr_data	tmpead = {0};
	entry_attr_data	*foundead;

	tmpead.ead_attrtypename = type;
	foundead = (entry_attr_data *)avl_find( ea->ea_attrlist, &tmpead,
				attr_type_node_cmp );
	return ( NULL != foundead ) ? foundead->ead_attrarrayindex : -1;
}

/* What's going on here then ? 
   Well, originally duplicate value checking was done by taking each
   new value and comparing in turn against all the previous values.
   Needless to say this was costly when there were many values.
   So, new code was written which built a binary tree of index keys
   for the values, and the test was done against the tree.
   Nothing wrong with this, it speeded up the case where there were
   many values nicely.
   Unfortunately, when there are few values, it proved to be a significent 
   performance sink. 
   So, now we check the old way up 'till there's 5 attribute values, then
   switch to the tree-based scheme.

   Note that duplicate values are only checked for and ignored
   if flags contains SLAPI_STR2ENTRY_REMOVEDUPVALS.
 */

/* dn is not consumed.  Caller needs to free it. */
static Slapi_Entry *
str2entry_dupcheck( const char *rawdn, char *s, int flags, int read_stateinfo )
{
    Slapi_Entry	*e;
    str2entry_attr stack_attrs[STR2ENTRY_SMALL_BUFFER_SIZE];
    str2entry_attr *dyn_attrs = NULL;
    str2entry_attr *attrs = stack_attrs;
    str2entry_attr *prev_attr= NULL;
    int nattrs;
    int maxattrs = STR2ENTRY_SMALL_BUFFER_SIZE;
    char *type;
    struct berval bvtype;
    str2entry_attr *sa;
    int i, j;
    char *next=NULL;
    char *valuecharptr=NULL;
    struct berval bvvalue;
    int rc;
	int fast_dup_check = 0;
	entry_attrs *ea = NULL;
	int tree_attr_checking = 0;
	int big_entry_attr_presence_check = 0;
	int check_for_duplicate_values =
			( 0 != ( flags & SLAPI_STR2ENTRY_REMOVEDUPVALS ));
	Slapi_Value *value = 0;
	CSN *attributedeletioncsn= NULL;
	CSN *maxcsn= NULL;
	char *normdn = NULL;
	int strict = 0;

    /* Check if we should be performing strict validation. */
    strict = config_get_dn_validate_strict();

	LDAPDebug( LDAP_DEBUG_TRACE, "=> str2entry_dupcheck\n", 0, 0, 0 );

    e = slapi_entry_alloc();
    slapi_entry_init(e,NULL,NULL);
    next = s;
    nattrs = 0;

	if (flags & SLAPI_STR2ENTRY_BIGENTRY)
	{
		big_entry_attr_presence_check = 1;
	}
    while ( (s = ldif_getline( &next )) != NULL )
    {
		CSNSet *valuecsnset= NULL;
		int value_state= VALUE_NOTFOUND;
		int attr_state= VALUE_NOTFOUND;
		int freeval = 0;
		struct berval bv_null = {0, NULL};

		csn_free(&attributedeletioncsn);

		if ( *s == '\n' || *s == '\0' ) {
		    break;
		}

		bvtype = bv_null;
		bvvalue = bv_null;
		if ( slapi_ldif_parse_line( s, &bvtype, &bvvalue, &freeval ) < 0 ) {
		    LDAPDebug( LDAP_DEBUG_TRACE,
			    "<= str2entry_dupcheck NULL (parse_line)\n", 0, 0, 0 );
		    continue;
		}
		type = bvtype.bv_val;
		valuecharptr = bvvalue.bv_val;

		/*
		 * Extract the attribute and value CSNs from the attribute type.
		 */		
		csnset_free(&valuecsnset);
		value_state= VALUE_NOTFOUND;
		attr_state= VALUE_NOTFOUND;
		str2entry_state_information_from_type(type,&valuecsnset,&attributedeletioncsn,&maxcsn,&value_state,&attr_state);
		if(!read_stateinfo)
		{
			/* We are not maintaining state information */
			if(value_state==VALUE_DELETED || attr_state==ATTRIBUTE_DELETED)
			{
				/* ignore deleted values and attributes */
				/* the memory below was not allocated by the slapi_ch_ functions */
				if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
				continue;
			}
			/* Ignore CSNs */
			csn_free(&attributedeletioncsn);
			csnset_free(&valuecsnset);
		}

		if ( rawdn ) {
			if ( NULL == slapi_entry_get_dn_const(e) ) {
				if (flags & SLAPI_STR2ENTRY_DN_NORMALIZED) {
					normdn = slapi_ch_strdup(rawdn);
				} else {
					normdn = slapi_create_dn_string("%s", rawdn);
					if (NULL == normdn) {
						LDAPDebug1Arg(LDAP_DEBUG_TRACE,
						    "str2entry_dupcheck: Invalid DN: %s\n", rawdn);
						slapi_entry_free( e );
						if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
						csn_free(&attributedeletioncsn);
						csn_free(&maxcsn);
						return NULL;
					}
				}
				/* normdn is consumed in e */
				slapi_entry_set_normdn(e, normdn);
			}
			if ( NULL == slapi_entry_get_rdn_const(e) ) {
				if (normdn) {
					/* normdn is just referred in slapi_entry_set_rdn. */
					slapi_entry_set_rdn(e, normdn);
				} else {
					if (flags & SLAPI_STR2ENTRY_DN_NORMALIZED) {
						normdn = slapi_ch_strdup(rawdn);
					} else {
						normdn = slapi_create_dn_string("%s", rawdn);
						if (NULL == normdn) {
						LDAPDebug1Arg(LDAP_DEBUG_TRACE,
							     "str2entry_dupcheck: Invalid DN: %s\n", rawdn);
							slapi_entry_free( e );
							if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
							csn_free(&attributedeletioncsn);
							csn_free(&maxcsn);
							return NULL;
						}
					}
					/* normdn is just referred in slapi_entry_set_rdn. */
					slapi_entry_set_rdn(e, normdn);
					slapi_ch_free_string(&normdn);
				}
			}
			rawdn = NULL; /* Set once in the loop. 
							 This won't affect the caller's passed address. */
		}
		if ( strcasecmp( type, "dn" ) == 0 ) {
			if ( slapi_entry_get_dn_const(e)!=NULL ) {
				char ebuf[ BUFSIZ ];
				LDAPDebug( LDAP_DEBUG_TRACE,
					"str2entry_dupcheck: entry has multiple dns \"%s\" "
					"and \"%s\" (second ignored)\n",
					slapi_entry_get_dn_const(e),
					escape_string( valuecharptr, ebuf ), 0 );
				/* the memory below was not allocated by the slapi_ch_ functions */
				if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
				continue;
			}
			normdn = slapi_create_dn_string("%s", valuecharptr);
			if (NULL == normdn) {
				LDAPDebug1Arg(LDAP_DEBUG_TRACE,
						"str2entry_dupcheck: Invalid DN: %s\n", valuecharptr);
				slapi_entry_free( e ); e = NULL;
				if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
				goto free_and_return;
			}
			/* normdn is consumed in e */
			slapi_entry_set_normdn(e, normdn);
			/* the memory below was not allocated by the slapi_ch_ functions */
			if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
		    continue;
		}

		if ( strcasecmp( type, "rdn" ) == 0 ) {
			if ( NULL == slapi_entry_get_rdn_const( e )) {
				slapi_entry_set_rdn( e, valuecharptr );
			}
			/* the memory below was not allocated by the slapi_ch_ functions */
			if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
		    continue;
		}

		/* If SLAPI_STR2ENTRY_NO_ENTRYDN is set, skip entrydn */
		if ( (flags & SLAPI_STR2ENTRY_NO_ENTRYDN) &&
		     strcasecmp( type, "entrydn" ) == 0 ) {
			if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
			continue;
		}

		/* retrieve uniqueid */
		if ( strcasecmp (type, SLAPI_ATTR_UNIQUEID) == 0 ){

			if (e->e_uniqueid != NULL){
				LDAPDebug (LDAP_DEBUG_TRACE, 
						   "str2entry_dupcheck: entry has multiple uniqueids %s "
						   "and %s (second ignored)\n", 
						   e->e_uniqueid, valuecharptr, 0);
			}else{
				slapi_entry_set_uniqueid (e, slapi_ch_strdup(valuecharptr));
			}
			/* the memory below was not allocated by the slapi_ch_ functions */
			if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
			continue;
		}

		if (strcasecmp(type,"objectclass") == 0) {
			if (strcasecmp(valuecharptr,"ldapsubentry") == 0)
				e->e_flags |= SLAPI_ENTRY_LDAPSUBENTRY;
			if (strcasecmp(valuecharptr, SLAPI_ATTR_VALUE_TOMBSTONE) == 0)
				e->e_flags |= SLAPI_ENTRY_FLAG_TOMBSTONE;
		}

		/* Here we have a quick look to see if this attribute is a new
		   value for the type we last processed or a new type.
		   If not, we look to see if we've seen this attribute type before.
		*/
		if ( prev_attr!=NULL && strcasecmp( type, prev_attr->sa_type ) != 0 )
		{
		    /* Different attribute type - find it, or alloc new */
		    prev_attr = NULL;
			/* The linear check below can take a while, so we change to use a tree if there are many attrs */
			if (!big_entry_attr_presence_check)
			{
			    for ( i = 0; i < nattrs; i++ )
			    {
					if (strcasecmp( type, attrs[i].sa_type ) == 0 )
					{
					    prev_attr = &attrs[i];
					    break;
					}
			    }
			}
			else
			{
				int	prev_index;

				/* Did we just switch checking mechanism ? */
				if (!tree_attr_checking)
				{
					/* If so then put the exising attrs into the tree */
					if (0 != entry_attrs_new(&ea))
					{
						/* Something very bad happened */
						if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
						csn_free(&attributedeletioncsn);
						csn_free(&maxcsn);
						return NULL;
					}
					for ( i = 0; i < nattrs; i++ )
					{
						entry_attrs_add(ea,attrs[i].sa_type, i);
					}
					tree_attr_checking = 1;
				}
				prev_index = entry_attrs_find(ea,type);
				if ( prev_index >= 0 ) {
					prev_attr = &attrs[prev_index];
					/* (prev_attr!=NULL) Means that we already had that one in the set */
				}
			}
		}
		if ( prev_attr==NULL )
		{
		    /* Haven't seen this type yet */
			fast_dup_check = 1;
		    if ( nattrs == maxattrs )
		    {
				/* Out of space - reallocate */
				maxattrs *= 2;
				if ( nattrs == STR2ENTRY_SMALL_BUFFER_SIZE ) {
				    /* out of fixed space - switch to dynamic */
				    PR_ASSERT( dyn_attrs == NULL );
				    dyn_attrs = (str2entry_attr *)
					    slapi_ch_malloc( sizeof( str2entry_attr ) *
					    maxattrs );
				    memcpy( dyn_attrs, stack_attrs,
					    STR2ENTRY_SMALL_BUFFER_SIZE *
					    sizeof( str2entry_attr ));
				    attrs = dyn_attrs;
				} else {
				    /* Need more dynamic space */
				    dyn_attrs = (str2entry_attr *)
					    slapi_ch_realloc( (char *) dyn_attrs, 
					    sizeof( str2entry_attr ) * maxattrs );
					attrs = dyn_attrs; /* realloc may change base pointer */
				}
		    }

		    /* Record the new type in the array */
			entry_attr_init(&attrs[nattrs], type, attr_state);

			if ( check_for_duplicate_values )
			{
				/* Get the comparison function for later use */
				attr_get_value_cmp_fn( &attrs[nattrs].sa_attr, &(attrs[nattrs].sa_comparefn));
				/*
				 * If the compare function wasn't available,
				 * we have to revert to AVL-tree-based dup checking,
				 * which uses index keys for comparisons
				 */
				if (NULL == attrs[nattrs].sa_comparefn)
				{
					fast_dup_check = 0;
				}
				/*
				 * If we are maintaining the attribute tree,
				 * then add the new attribute to the tree.
				 */
				if (big_entry_attr_presence_check && tree_attr_checking)
				{
					entry_attrs_add(ea,attrs[nattrs].sa_type, nattrs);
				}
			}
			prev_attr = &attrs[nattrs];
			nattrs++;
		}

		sa = prev_attr;	/* For readability */
		value= value_new(NULL, CSN_TYPE_NONE, NULL);
		if (slapi_attr_is_dn_syntax_attr(&(sa->sa_attr))) {
			Slapi_DN *sdn = NULL;
			const char *dn_aval = NULL;
			if (strict) {
				/* check that the dn is formatted correctly */
				rc = slapi_dn_syntax_check(NULL, valuecharptr, 1);
				if (rc) { /* syntax check failed */
					LDAPDebug2Args(LDAP_DEBUG_ANY, 
						"str2entry_dupcheck: strict: Invalid DN value: %s: %s\n",
						type, valuecharptr);
					slapi_entry_free( e ); e = NULL;
					if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
					goto free_and_return;
				}
			}
			sdn = slapi_sdn_new_dn_byref(bvvalue.bv_val);
			/* Note: slapi_sdn_get_dn returns the normalized DN 
			 * with case-intact. Thus, the length of dn_aval is 
			 * slapi_sdn_get_ndn_len(sdn). */
			dn_aval = slapi_sdn_get_dn(sdn);
			slapi_value_set(value, (void *)dn_aval, slapi_sdn_get_ndn_len(sdn));
			slapi_sdn_free(&sdn);
		} else {
			slapi_value_set_berval(value, &bvvalue);
		}
		/* the memory below was not allocated by the slapi_ch_ functions */
		if (freeval) slapi_ch_free_string(&bvvalue.bv_val);
		value->v_csnset= valuecsnset;
		valuecsnset= NULL;
		{
			const CSN *distinguishedcsn= csnset_get_csn_of_type(value->v_csnset,CSN_TYPE_VALUE_DISTINGUISHED);
			if(distinguishedcsn!=NULL)
			{
				entry_add_dncsn(e,distinguishedcsn);
			}
		}

		if(value_state==VALUE_DELETED)
		{
			/* 
			 * for deleted values, we do not want to perform a dupcheck against
			 * existing values. Also, we do not want to add it to the
			 * avl tree (if one is being maintained)
			 *
			 */
			rc = 0; /* Presume no duplicate */
		}
		else if ( !check_for_duplicate_values )
		{
			rc = LDAP_SUCCESS;	/* presume no duplicate */
		} else {
			/* For value dup checking, we either use brute-force, if there's a small number */
			/* Or a tree-based approach if there's a large number. */
			/* The tree code is expensive, which is why we don't use it unless there's many attributes */
			rc = 0; /* Presume no duplicate */
			if (fast_dup_check)
			{
				/* Fast dup-checking */
				/* Do we now have so many values that we should switch to tree-based checking ? */
				if (sa->sa_present_values.num > STR2ENTRY_VALUE_DUPCHECK_THRESHOLD)
				{
					/* Make the tree from the existing attr values */
					rc= valuetree_add_valuearray( &sa->sa_attr, sa->sa_present_values.va, &sa->sa_vtree, NULL);
					/* Check if the value already exists, in the tree. */
					rc= valuetree_add_value( &sa->sa_attr, value, &sa->sa_vtree);
					fast_dup_check = 0;
				}
				else
				{
					/* JCM - need an efficient valuearray function to do this */
					/* Brute-force check */
					for ( j = 0; j < sa->sa_present_values.num; j++ )/* JCM innards */
					{
						if (0 == sa->sa_comparefn(slapi_value_get_berval(value),slapi_value_get_berval(sa->sa_present_values.va[j])))/* JCM innards */
						{
							/* Oops---this value matches one already present */
							rc = LDAP_TYPE_OR_VALUE_EXISTS;
							break;
						}
					}
				}
			}
			else
			{
				/* Check if the value already exists, in the tree. */
				rc = valuetree_add_value( &sa->sa_attr, value, &sa->sa_vtree);
			}
		}

		if ( rc==LDAP_SUCCESS )
		{
			if(value_state==VALUE_DELETED)
			{
				valuearrayfast_add_value_passin(&sa->sa_deleted_values,value);
				value= NULL; /* value was consumed */
			}
			else
			{
				valuearrayfast_add_value_passin(&sa->sa_present_values,value);
				value= NULL; /* value was consumed */
			}
			if(attributedeletioncsn!=NULL)
			{
				sa->sa_attributedeletioncsn= attributedeletioncsn;
				attributedeletioncsn= NULL; /* csn was consumed */
			}
		}
		else if (rc==LDAP_TYPE_OR_VALUE_EXISTS)
		{
		    sa->sa_numdups++;
		    csn_free(&attributedeletioncsn);
		}
		else
		{
		    /* Failure adding to value tree */
		    LDAPDebug( LDAP_DEBUG_ANY, "str2entry_dupcheck: unexpected failure %d constructing value tree\n", rc, 0, 0 );
		    slapi_entry_free( e ); e = NULL;
		    goto free_and_return;
		}

		slapi_value_free(&value);
	}

    /* All done with parsing.  Now create the entry. */
    /* check to make sure there was a dn: line */
    if ( slapi_entry_get_dn_const(e)==NULL )
    {
		if (!(SLAPI_STR2ENTRY_INCLUDE_VERSION_STR & flags))
	    	LDAPDebug( LDAP_DEBUG_ANY, "str2entry_dupcheck: entry has no dn\n",
									   0, 0, 0 );
	    slapi_entry_free( e ); e = NULL;
	    goto free_and_return;
    }

    /* get the read lock of name2asi for performance purpose.
       It reduces read locking by per-entry lock, instead of per-attribute.
    */
    attr_syntax_read_lock();

	/*
	 * For each unique attribute in the array,
	 * Create a Slapi_Attr and set it's present and deleted values.
	 */
    for ( i = 0; i < nattrs; i++ )
    {
		sa = &attrs[i];
		if ( sa->sa_numdups > 0 )
		{
		    if ( sa->sa_numdups > 1 ) {
				LDAPDebug( LDAP_DEBUG_ANY, "str2entry_dupcheck: %d duplicate values for attribute "
					"type %s detected in entry %s. Extra values ignored.\n",
					sa->sa_numdups, sa->sa_type, slapi_entry_get_dn_const(e) );
		    } else {
				LDAPDebug( LDAP_DEBUG_ANY, "str2entry_dupcheck: Duplicate value for attribute "
					"type %s detected in entry %s. Extra value ignored.\n",
					sa->sa_type, slapi_entry_get_dn_const(e), 0 );
		    }
		}
		{
			Slapi_Attr **alist= NULL;
			if(sa->sa_state==ATTRIBUTE_DELETED)
			{
				if(read_stateinfo)
				{
					alist= &e->e_deleted_attrs;
				}
				else
				{
					/*
					 * if we are not maintaining state info,
					 * ignore the deleted attributes
					 */
				}
			}
			else
			{
				alist= &e->e_attrs;
			}
			if(alist!=NULL)
			{
				int maxvals = 0;
				Slapi_Attr **a= NULL;
				attrlist_find_or_create_locking_optional(alist, sa->sa_type, &a, PR_FALSE);
				valuearray_add_valuearray_fast( /* JCM should be calling a valueset function */
					&(*a)->a_present_values.va, /* JCM .va is private */
					sa->sa_present_values.va,
					0, /* Currently there are no present values on the attribute */
					sa->sa_present_values.num,
					&maxvals,
					1/*Exact*/,
					1/*Passin*/);
				sa->sa_present_values.num= 0; /* The values have been consumed */
				maxvals = 0;
				valuearray_add_valuearray_fast( /* JCM should be calling a valueset function */
					&(*a)->a_deleted_values.va, /* JCM .va is private */
					sa->sa_deleted_values.va,
					0, /* Currently there are no deleted values on the attribute */
					sa->sa_deleted_values.num,
					&maxvals,
					1/*Exact*/,
					1/*Passin*/);
				sa->sa_deleted_values.num= 0; /* The values have been consumed */
				if(sa->sa_attributedeletioncsn!=NULL)
				{
					attr_set_deletion_csn(*a,sa->sa_attributedeletioncsn);
					csn_free(&sa->sa_attributedeletioncsn);
				}
			}
		}
    }

    /* release read lock of name2asi, per-entry lock */
    attr_syntax_unlock_read();

    /* If this is a tombstone, it requires a special treatment for rdn. */
    if (e->e_flags & SLAPI_ENTRY_FLAG_TOMBSTONE) {
        /* tombstone */
        if (_entry_set_tombstone_rdn(e, slapi_entry_get_dn_const(e))) {
            LDAPDebug1Arg( LDAP_DEBUG_TRACE, "str2entry_dupcheck: "
                           "tombstone entry has badly formatted dn: %s\n",
                           slapi_entry_get_dn_const(e) );
            slapi_entry_free( e ); e = NULL;
            goto free_and_return;
        }
    }

    /* Add the RDN values, if asked, and if not already present */
    if ( flags & SLAPI_STR2ENTRY_ADDRDNVALS ) {
        if ( slapi_entry_add_rdn_values( e ) != LDAP_SUCCESS ) {
            LDAPDebug( LDAP_DEBUG_TRACE,
                       "str2entry_dupcheck: entry has badly formatted dn\n",
                       0, 0, 0 );
            slapi_entry_free( e ); e = NULL;
            goto free_and_return;
        }
    }

	if (read_stateinfo)
	{
		e->e_maxcsn = maxcsn;
		maxcsn = NULL;
	}

free_and_return:
    for ( i = 0; i < nattrs; i++ )
    {
		slapi_ch_free((void **) &(attrs[ i ].sa_type));
		valuearrayfast_done(&attrs[ i ].sa_present_values);
		valuearrayfast_done(&attrs[ i ].sa_deleted_values);
		valuetree_free( &attrs[ i ].sa_vtree );
		attr_done( &attrs[ i ].sa_attr );
    }
	if (tree_attr_checking)
	{
		entry_attrs_delete(&ea);
	}
	slapi_ch_free((void **) &dyn_attrs );
	if (value) slapi_value_free(&value);
	csn_free(&attributedeletioncsn);
	csn_free(&maxcsn);

	LDAPDebug( LDAP_DEBUG_TRACE, "<= str2entry_dupcheck 0x%x \"%s\"\n",
		e, slapi_sdn_get_dn (slapi_entry_get_sdn_const(e)), 0 );
    return e;
}

/*
 *
 * Convert an entry in LDIF format into a
 * Slapi_Entry structure.  If we can assume that the
 * LDIF is well-formed we call str2entry_fast(),
 * which does no error checking.
 * Otherwise we do not assume well-formed LDIF, and
 * call str2entry_dupcheck(), which checks for
 * duplicate attribute values and does not assume
 * that values are all contiguous.
 * 
 * Well-formed LDIF has the following characteristics:
 * 1) There are no duplicate attribute values
 * 2) The RDN is an attribute of the entry
 * 3) All values for a given attribute type are
 *    contiguous.
 */
#define SLAPI_STRENTRY_FLAGS_HANDLED_IN_SLAPI_STR2ENTRY 			\
			( SLAPI_STR2ENTRY_IGNORE_STATE							\
			| SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES					\
			| SLAPI_STR2ENTRY_TOMBSTONE_CHECK						\
			| SLAPI_STR2ENTRY_USE_OBSOLETE_DNFORMAT					\
			| SLAPI_STR2ENTRY_NO_ENTRYDN							\
			| SLAPI_STR2ENTRY_DN_NORMALIZED							\
			)

#define SLAPI_STRENTRY_FLAGS_HANDLED_BY_STR2ENTRY_FAST				\
			( SLAPI_STR2ENTRY_INCLUDE_VERSION_STR					\
			| SLAPI_STRENTRY_FLAGS_HANDLED_IN_SLAPI_STR2ENTRY		\
			)

/*
 * If well-formed LDIF has not been provided OR if a flag that is
 * not handled by str2entry_fast() has been passed in, call the
 * slower but more forgiving str2entry_dupcheck() function.
 */
#define str2entry_can_use_fast(flags) \
			(((flags) & SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF) || \
			 ((flags) & ~SLAPI_STRENTRY_FLAGS_HANDLED_BY_STR2ENTRY_FAST))

Slapi_Entry *
slapi_str2entry( char *s, int flags )
{
	Slapi_Entry *e;
	int read_stateinfo= ~( flags & SLAPI_STR2ENTRY_IGNORE_STATE );

	LDAPDebug( LDAP_DEBUG_ARGS,
			"slapi_str2entry: flags=0x%x, entry=\"%.50s...\"\n",
			flags, s, 0 );


	/*
	 * If well-formed LDIF has not been provided OR if a flag that is
	 * not handled by str2entry_fast() has been passed in, call the
	 * slower but more forgiving str2entry_dupcheck() function.
	 */
	if (str2entry_can_use_fast(flags))
	{
	    e= str2entry_dupcheck( NULL/*dn*/, s, flags, read_stateinfo );
	}
	else
	{
	    e= str2entry_fast( NULL/*dn*/, s, flags, read_stateinfo );
	}
	if (!e)
	   return e;	/* e == NULL */

	if ( flags & SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES )
	{
		if ( flags & SLAPI_STR2ENTRY_NO_SCHEMA_LOCK )
		{
			schema_expand_objectclasses_nolock( e );
		}
		else
		{
			slapi_schema_expand_objectclasses( e );
		}
	}

    if ( flags & SLAPI_STR2ENTRY_TOMBSTONE_CHECK )
	{
		/*
		 * Check if the entry is a tombstone.
		 */
		if(slapi_entry_attr_hasvalue(e, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE))
		{
			e->e_flags |= SLAPI_ENTRY_FLAG_TOMBSTONE;
		}
	}
	return e;
}

/*
 * string s does not include dn.
 * NOTE: the first arg "dn" should have been normalized before passing.
 */
Slapi_Entry *
slapi_str2entry_ext( const char *normdn, char *s, int flags )
{
	Slapi_Entry *e;
	int read_stateinfo= ~( flags & SLAPI_STR2ENTRY_IGNORE_STATE );

	if (NULL == normdn)
	{
		return slapi_str2entry( s, flags );
	}

	LDAPDebug( LDAP_DEBUG_ARGS,
			"slapi_str2entry_ext: flags=0x%x, dn=\"%s\", entry=\"%.50s...\"\n",
			flags, normdn, s );


	/*
	 * If well-formed LDIF has not been provided OR if a flag that is
	 * not handled by str2entry_fast() has been passed in, call the
	 * slower but more forgiving str2entry_dupcheck() function.
	 */
	if (str2entry_can_use_fast(flags))
	{
	    e = str2entry_dupcheck( normdn, s, 
	                    flags|SLAPI_STR2ENTRY_DN_NORMALIZED, read_stateinfo );
	}
	else
	{
	    e = str2entry_fast( normdn, s, 
	                    flags|SLAPI_STR2ENTRY_DN_NORMALIZED, read_stateinfo );
	}
	if (!e)
		return e;	/* e == NULL */

	if ( flags & SLAPI_STR2ENTRY_EXPAND_OBJECTCLASSES )
	{
		if ( flags & SLAPI_STR2ENTRY_NO_SCHEMA_LOCK )
		{
			schema_expand_objectclasses_nolock( e );
		}
		else
		{
			slapi_schema_expand_objectclasses( e );
		}
	}

    if ( flags & SLAPI_STR2ENTRY_TOMBSTONE_CHECK )
	{
		/*
		 * Check if the entry is a tombstone.
		 */
		if(slapi_entry_attr_hasvalue(e, SLAPI_ATTR_OBJECTCLASS, SLAPI_ATTR_VALUE_TOMBSTONE))
		{
			e->e_flags |= SLAPI_ENTRY_FLAG_TOMBSTONE;
		}
	}
	return e;
}

/*
 * If the attribute type is in the protected list, it returns size 0.
 */
static size_t
entry2str_internal_size_value( const char *attrtype, const Slapi_Value *v,
                               int entry2str_ctrl, int attribute_state, 
                               int value_state )
{
	size_t elen = 0;
	size_t attrtypelen;
	if((NULL == attrtype) || is_type_protected(attrtype)) {
		goto bail;
	}
	attrtypelen = strlen(attrtype);
	if(entry2str_ctrl & SLAPI_DUMP_STATEINFO)
	{
		attrtypelen+= csnset_string_size(v->v_csnset);
		if (attribute_state==ATTRIBUTE_DELETED)
		{
			attrtypelen += DELETED_ATTR_STRSIZE;
		}
		if(value_state==VALUE_DELETED)
		{
			attrtypelen += DELETED_VALUE_STRSIZE;
		}
	}
	elen = LDIF_SIZE_NEEDED(attrtypelen, slapi_value_get_berval(v)->bv_len);
bail:
	return elen;
}

static size_t
entry2str_internal_size_valueset( const char *attrtype, const Slapi_ValueSet *vs, int entry2str_ctrl, int attribute_state, int value_state )
{
	size_t elen= 0;
	if(!valueset_isempty(vs))
	{
		int i;
		Slapi_Value **va= valueset_get_valuearray(vs);
	    for (i = 0; va[i]; i++)
	    {
			elen+= entry2str_internal_size_value(attrtype, va[i], entry2str_ctrl,
												 attribute_state, value_state );
		}
	}
	return elen;
}

static size_t
entry2str_internal_size_attrlist( const Slapi_Attr *attrlist, int entry2str_ctrl, int attribute_state )
{
	size_t elen= 0;
	const Slapi_Attr *a;
	for (a= attrlist; a; a = a->a_next)
	{
		/* skip operational attributes if not requested */
		if ((entry2str_ctrl & SLAPI_DUMP_NOOPATTRS) &&
			slapi_attr_flag_is_set(a, SLAPI_ATTR_FLAG_OPATTR))
			continue;

		/* Count the space required for the present and deleted values */
		elen+= entry2str_internal_size_valueset(a->a_type, &a->a_present_values,
												entry2str_ctrl, attribute_state,
												VALUE_PRESENT);
		if(entry2str_ctrl & SLAPI_DUMP_STATEINFO)
		{
			elen+= entry2str_internal_size_valueset(a->a_type, &a->a_deleted_values,
													entry2str_ctrl, attribute_state,
													VALUE_DELETED);
			/* ";adcsn-" + a->a_deletioncsn */
			if ( a->a_deletioncsn )
			{
				elen+= 1 + LDIF_CSNPREFIX_MAXLENGTH + CSN_STRSIZE;
			}
		}
	}
	return elen;
}

static void
entry2str_internal_put_value( const char *attrtype, const CSN *attrcsn, CSNType attrcsntype, int attr_state, const Slapi_Value *v, int value_state, char **ecur, char **typebuf, size_t *typebuf_len, int entry2str_ctrl )
{
	const char *type;
	unsigned long options = 0;
        const struct berval *bvp;
	if(entry2str_ctrl & SLAPI_DUMP_STATEINFO)
	{
		char *p;
		size_t attrtypelen= strlen(attrtype);
		size_t attrcsnlen= 0;
		size_t valuecsnlen= 0;
		size_t need= attrtypelen+1;
		if(attrcsn!=NULL)
		{
			/* ; csntype csn */
			attrcsnlen= 1 + csn_string_size();
			need+= attrcsnlen;
		}
		if(v->v_csnset!=NULL)
		{
			/* +(; csntype csn) */
			valuecsnlen= csnset_string_size(v->v_csnset);
			need+= valuecsnlen;
		}
		if(attr_state==ATTRIBUTE_DELETED)
		{
			need+= DELETED_ATTR_STRSIZE;
		}
		if(value_state==VALUE_DELETED)
		{
			need+= DELETED_VALUE_STRSIZE; /* ;deleted */
		}
		if(*typebuf_len<need)
		{
			*typebuf= (char*)slapi_ch_realloc(*typebuf,need);
			*typebuf_len= need;
		}
		p= *typebuf;
		type= p;
		strcpy(p,attrtype);
		p+= attrtypelen;
		if(attrcsn!=NULL)
		{
			csn_as_attr_option_string(attrcsntype,attrcsn,p);
			p+= attrcsnlen;
		}
		if(v->v_csnset!=NULL)
		{
			csnset_as_string(v->v_csnset,p);
			p+= valuecsnlen;
		}
		if(attr_state==ATTRIBUTE_DELETED)
		{
			strcpy(p,DELETED_ATTR_STRING);
			p+= DELETED_ATTR_STRSIZE;
		}
		if(value_state==VALUE_DELETED)
		{
			strcpy(p,DELETED_VALUE_STRING);
		}
	}
	else
	{
		type= attrtype;
	}
	bvp = slapi_value_get_berval(v);
	if (entry2str_ctrl & SLAPI_DUMP_NOWRAP)
		options |= LDIF_OPT_NOWRAP;
	if (entry2str_ctrl & SLAPI_DUMP_MINIMAL_ENCODING)
		options |= LDIF_OPT_MINIMAL_ENCODING;
	slapi_ldif_put_type_and_value_with_options( ecur, type, bvp->bv_val, bvp->bv_len, options );
}

static void
entry2str_internal_put_valueset( const char *attrtype, const CSN *attrcsn, CSNType attrcsntype, int attr_state, const Slapi_ValueSet *vs, int value_state, char **ecur, char **typebuf, size_t *typebuf_len, int entry2str_ctrl )
{
	if(!valueset_isempty(vs))
	{
		int i;
		Slapi_Value **va= valueset_get_valuearray(vs);
		for ( i = 0; va[i] != NULL; i++ )
		{
			/* Attach the attribute deletion csn on the first value */
			if((entry2str_ctrl & SLAPI_DUMP_STATEINFO) && i==0)
			{
				entry2str_internal_put_value( attrtype, attrcsn, attrcsntype, attr_state, va[i], value_state, ecur, typebuf, typebuf_len, entry2str_ctrl );
			}
			else
			{
				entry2str_internal_put_value( attrtype, NULL, CSN_TYPE_UNKNOWN, attr_state, va[i], value_state, ecur, typebuf, typebuf_len, entry2str_ctrl );
			}
		}
	}
}

int
is_type_protected(const char *type)
{
    char **paap = NULL;
    for (paap = protected_attrs_all; paap && *paap; paap++) {
        if (0 == strcasecmp(type, *paap)) {
            return 1;
        }
    }
    return 0;
}

#if defined(USE_OLD_UNHASHED)
int
is_type_forbidden(const char *type)
{
    char **paap = NULL;
    for (paap = forbidden_attrs; paap && *paap; paap++) {
        if (0 == strcasecmp(type, *paap)) {
            return 1;
        }
    }
    return 0;
}
#endif

static void
entry2str_internal_put_attrlist( const Slapi_Attr *attrlist, int attr_state, int entry2str_ctrl, char **ecur, char **typebuf, size_t *typebuf_len)
{
	const Slapi_Attr *a;

	/* Put the present attributes */
	for (a= attrlist; a; a = a->a_next)
	{
		/* skip operational attributes if not requested */
		if ((entry2str_ctrl & SLAPI_DUMP_NOOPATTRS) &&
			slapi_attr_flag_is_set(a, SLAPI_ATTR_FLAG_OPATTR))
			continue;

		/* don't dump uniqueid if not asked */
		if (!(strcasecmp(a->a_type, SLAPI_ATTR_UNIQUEID) == 0 &&
			!(SLAPI_DUMP_UNIQUEID & entry2str_ctrl)) && 
			!is_type_protected(a->a_type))
		{
			/* Putting present attribute values */
			/* put "<type>:[:] <value>" line for each value */
			int present_values= !valueset_isempty(&a->a_present_values);
			if(present_values)
			{
				entry2str_internal_put_valueset(a->a_type, a->a_deletioncsn, CSN_TYPE_ATTRIBUTE_DELETED, attr_state, &a->a_present_values, VALUE_PRESENT, ecur, typebuf, typebuf_len, entry2str_ctrl);
			}
			if(entry2str_ctrl & SLAPI_DUMP_STATEINFO)
			{
				/* Putting deleted attribute values */
				if(present_values)
				{
					entry2str_internal_put_valueset(a->a_type, NULL, CSN_TYPE_NONE, attr_state, &a->a_deleted_values, VALUE_DELETED, ecur, typebuf, typebuf_len, entry2str_ctrl);
				}
				else
				{
					/* There were no present values on which to place the ADCSN, so we put it on the first deleted value. */
					entry2str_internal_put_valueset(a->a_type, a->a_deletioncsn, CSN_TYPE_ATTRIBUTE_DELETED, attr_state, &a->a_deleted_values, VALUE_DELETED, ecur, typebuf, typebuf_len, entry2str_ctrl);
				}
			}
		}
	}
}

static char *
entry2str_internal( Slapi_Entry *e, int *len, int entry2str_ctrl )
{
    char *ebuf;
    char *ecur;
    size_t elen = 0;
    size_t typebuf_len= 64;
    char *typebuf= (char *)slapi_ch_malloc(typebuf_len);
    Slapi_Value dnvalue;

    /*
     * In string format, an entry looks like this:
     *    dn: <dn>\n
     *    [<attr>: <value>\n]*
     */

    ecur = ebuf = NULL;

    value_init(&dnvalue,NULL,CSN_TYPE_NONE,NULL);

    /* find length of buffer needed to hold this entry */
    if (slapi_entry_get_dn_const(e)!=NULL)
    {
        slapi_value_set_string(&dnvalue,slapi_entry_get_dn_const(e));
        elen+= entry2str_internal_size_value( "dn", &dnvalue, entry2str_ctrl,
                                              ATTRIBUTE_PRESENT, VALUE_PRESENT );
    }

    /* Count the space required for the present attributes */
    elen+= entry2str_internal_size_attrlist( e->e_attrs, entry2str_ctrl, ATTRIBUTE_PRESENT );

    /* Count the space required for the deleted attributes */
    if(entry2str_ctrl & SLAPI_DUMP_STATEINFO)
    {
        elen+= entry2str_internal_size_attrlist( e->e_deleted_attrs, entry2str_ctrl,
                                                 ATTRIBUTE_DELETED );
    }

    elen += 1;
    ecur = ebuf = (char *)slapi_ch_malloc(elen);

    /* put the dn */
    if ( slapi_entry_get_dn_const(e)!=NULL)
    {
        /* put "dn: <dn>" */
        entry2str_internal_put_value("dn", NULL, CSN_TYPE_NONE, ATTRIBUTE_PRESENT, &dnvalue, VALUE_PRESENT, &ecur, &typebuf, &typebuf_len, entry2str_ctrl);
    }

    /* Put the present attributes */
    entry2str_internal_put_attrlist( e->e_attrs, ATTRIBUTE_PRESENT, entry2str_ctrl, &ecur, &typebuf, &typebuf_len );

    /* Put the deleted attributes */
    if(entry2str_ctrl & SLAPI_DUMP_STATEINFO)
    {
        entry2str_internal_put_attrlist( e->e_deleted_attrs, ATTRIBUTE_DELETED, entry2str_ctrl, &ecur, &typebuf, &typebuf_len );
    }
    
    *ecur = '\0';
    if ( (size_t)(ecur - ebuf + 1) > elen )
    {
        slapi_log_error (SLAPI_LOG_FATAL, NULL,
            "entry2str_internal: array boundary wrote: bufsize=%ld wrote=%ld\n",
            elen, (ecur - ebuf + 1));
    }

    if ( NULL != len ) {
        *len = ecur - ebuf;
    }

    slapi_ch_free((void**)&typebuf);
    value_done(&dnvalue);
    
    return ebuf;
}

static char *
entry2str_internal_ext( Slapi_Entry *e, int *len, int entry2str_ctrl)
{
    if (entry2str_ctrl & SLAPI_DUMP_RDN_ENTRY) /* dump rdn: ... */
    {
        char *ebuf;
        char *ecur;
        size_t elen = 0;
        size_t typebuf_len = 64;
        char *typebuf = (char *)slapi_ch_malloc(typebuf_len);
        Slapi_Value rdnvalue;
        /*
         * In string format, an entry looks like this:
         *    rdn: <normalized rdn>\n
         *    [<attr>: <value>\n]*
         */
    
        ecur = ebuf = NULL;
    
        value_init(&rdnvalue, NULL, CSN_TYPE_NONE, NULL);
    
        /* find length of buffer needed to hold this entry */
        if (NULL == slapi_entry_get_rdn_const(e) &&
            NULL != slapi_entry_get_dn_const(e))
        {
            /* e_srdn is not filled in, use e_sdn */
            slapi_rdn_init_all_sdn(&e->e_srdn, slapi_entry_get_sdn_const(e));
        }
        if (NULL != slapi_entry_get_rdn_const(e))
        {
            slapi_value_set_string(&rdnvalue, slapi_entry_get_rdn_const(e));
            elen += entry2str_internal_size_value("rdn", &rdnvalue,
                                                  entry2str_ctrl,
                                                  ATTRIBUTE_PRESENT,
                                                  VALUE_PRESENT);
        }
    
        /* Count the space required for the present attributes */
        elen += entry2str_internal_size_attrlist(e->e_attrs, entry2str_ctrl,
                                                 ATTRIBUTE_PRESENT);
    
        /* Count the space required for the deleted attributes */
        if(entry2str_ctrl & SLAPI_DUMP_STATEINFO)
        {
            elen += entry2str_internal_size_attrlist(e->e_deleted_attrs,
                                                     entry2str_ctrl,
                                                     ATTRIBUTE_DELETED);
        }
    
        elen += 1;
        ecur = ebuf = (char *)slapi_ch_malloc(elen);
    
        /* put the rdn */
        if (slapi_entry_get_rdn_const(e) != NULL)
        {
            /* put "rdn: <normalized rdn>" */
            entry2str_internal_put_value("rdn", NULL, CSN_TYPE_NONE,
                                         ATTRIBUTE_PRESENT, &rdnvalue,
                                         VALUE_PRESENT, &ecur, &typebuf,
                                         &typebuf_len, entry2str_ctrl);
        }
    
        /* Put the present attributes */
        entry2str_internal_put_attrlist(e->e_attrs, ATTRIBUTE_PRESENT,
                                        entry2str_ctrl, &ecur,
                                        &typebuf, &typebuf_len );
    
        /* Put the deleted attributes */
        if(entry2str_ctrl & SLAPI_DUMP_STATEINFO)
        {
            entry2str_internal_put_attrlist(e->e_deleted_attrs,
                                            ATTRIBUTE_DELETED, entry2str_ctrl,
                                            &ecur, &typebuf, &typebuf_len );
        }
        
        *ecur = '\0';
        if ((size_t)(ecur - ebuf + 1) > elen)
        {
            /* this should not happen */
            slapi_log_error (SLAPI_LOG_FATAL, NULL,
                "entry2str_internal_ext: array boundary wrote: "
                "bufsize=%ld wrote=%ld\n",
                elen, (ecur - ebuf + 1));
        }
    
        if ( NULL != len ) {
            *len = ecur - ebuf;
        }
    
        slapi_ch_free((void**)&typebuf);
        value_done(&rdnvalue);
        
        return ebuf;
    }
    else /* dump "dn: ..." */
    {
        return entry2str_internal( e, len, entry2str_ctrl );
    }
}

/*
 * This function converts an entry to the entry string starting with "dn: ..."
 */
char *
slapi_entry2str( Slapi_Entry *e, int *len )
{
	return entry2str_internal(e, len, 0);
}

/*
 * This function converts an entry to the entry string starting with "dn: ..."
 */
char *
slapi_entry2str_dump_uniqueid( Slapi_Entry *e, int *len )
{
	return entry2str_internal(e, len, SLAPI_DUMP_UNIQUEID);
}

/*
 * This function converts an entry to the entry string starting with "dn: ..."
 */
char *
slapi_entry2str_no_opattrs( Slapi_Entry *e, int *len )
{
	return entry2str_internal(e, len, SLAPI_DUMP_NOOPATTRS);
}

/*
 * This function converts an entry to the entry string starting with "dn: ..."
 * by default.  If option includes SLAPI_DUMP_RDN_ENTRY bit, it does the entry
 * to "rdn: ..."
 */
char *
slapi_entry2str_with_options( Slapi_Entry *e, int *len, int options )
{
    return entry2str_internal_ext(e, len, options);
}

static int entry_type = -1; /* The type number assigned by the Factory for 'Entry' */

int
get_entry_object_type()
{
    if(entry_type==-1)
	{
	    /* The factory is given the name of the object type, in
		 * return for a type handle. Whenever the object is created
		 * or destroyed the factory is called with the handle so
		 * that it may call the constructors or destructors registered
		 * with it.
		 */
        entry_type= factory_register_type(SLAPI_EXT_ENTRY,offsetof(Slapi_Entry,e_extension));
	}
    return entry_type;
}

/* ======  Slapi_Entry functions ====== */

#ifdef ENTRY_DEBUG
static void entry_dump( const Slapi_Entry *e, const char *text);
#define ENTRY_DUMP(e,name) entry_dump(e,name)
#else
#define ENTRY_DUMP(e,name) ((void)0)
#endif


static int counters_created= 0;
PR_DEFINE_COUNTER(slapi_entry_counter_created);
PR_DEFINE_COUNTER(slapi_entry_counter_deleted);
PR_DEFINE_COUNTER(slapi_entry_counter_exist);

Slapi_Entry *
slapi_entry_alloc()
{
	Slapi_Entry *e= (Slapi_Entry *) slapi_ch_calloc( 1, sizeof(struct slapi_entry) );
	slapi_sdn_init(&e->e_sdn);
	slapi_rdn_init(&e->e_srdn);

	e->e_extension = factory_create_extension(get_entry_object_type(),e,NULL);
	if(!counters_created)
	{
		PR_CREATE_COUNTER(slapi_entry_counter_created,"Slapi_Entry","created","");
		PR_CREATE_COUNTER(slapi_entry_counter_deleted,"Slapi_Entry","deleted","");
		PR_CREATE_COUNTER(slapi_entry_counter_exist,"Slapi_Entry","exist","");
		counters_created= 1;
	}
    PR_INCREMENT_COUNTER(slapi_entry_counter_created);
    PR_INCREMENT_COUNTER(slapi_entry_counter_exist);
	ENTRY_DUMP(e,"slapi_entry_alloc");
	return e;
}

/*
 * WARNING - The DN is passed in *not* copied.
 */
void
slapi_entry_init(Slapi_Entry *e, char *dn, Slapi_Attr *a)
{
    slapi_sdn_set_dn_passin(slapi_entry_get_sdn(e), dn);
    e->e_uniqueid= NULL;
    e->e_attrs= a;
    e->e_dncsnset= NULL;
    e->e_maxcsn= NULL;
    e->e_deleted_attrs= NULL;
    e->e_virtual_attrs= NULL;
    e->e_virtual_watermark= 0;
    e->e_virtual_lock= slapi_new_rwlock();
    e->e_flags= 0;
}

void
slapi_entry_init_ext(Slapi_Entry *e, Slapi_DN *sdn, Slapi_Attr *a)
{
	slapi_sdn_copy(sdn, slapi_entry_get_sdn(e));
    e->e_uniqueid= NULL;
    e->e_attrs= a;
    e->e_dncsnset= NULL;
    e->e_maxcsn= NULL;
    e->e_deleted_attrs= NULL;
    e->e_virtual_attrs= NULL;
    e->e_virtual_watermark= 0;
    e->e_virtual_lock= slapi_new_rwlock();
    e->e_flags= 0;
}

void
slapi_entry_free( Slapi_Entry *e ) /* JCM - Should be ** so that we can NULL the ptr */
{
	if(e!=NULL)
	{
		ENTRY_DUMP(e,"slapi_entry_free");
	    factory_destroy_extension(get_entry_object_type(),e,NULL/*Parent*/,&(e->e_extension));
	    slapi_sdn_done(&e->e_sdn);
		slapi_rdn_done(&e->e_srdn);

	    csnset_free(&e->e_dncsnset);
	    csn_free(&e->e_maxcsn);
		slapi_ch_free((void **)&e->e_uniqueid);
		attrlist_free(e->e_attrs);
		attrlist_free(e->e_deleted_attrs);
		attrlist_free(e->e_virtual_attrs);
		if(e->e_virtual_lock)
            slapi_destroy_rwlock(e->e_virtual_lock);
		slapi_ch_free((void**)&e);
	    PR_INCREMENT_COUNTER(slapi_entry_counter_deleted);
	    PR_DECREMENT_COUNTER(slapi_entry_counter_exist);
	}
}

static size_t slapi_attrlist_size(Slapi_Attr *attrs)
{
    size_t size = 0;
    Slapi_Attr *a;

    for (a= attrs; a; a = a->a_next) {
        if (a->a_type) size += strlen(a->a_type) + 1;
        size += valueset_size(&a->a_present_values);
        size += valueset_size(&a->a_deleted_values);
        /* Don't bother with a_listtofree. This is only set
         * by a call to slapi_attr_get_values, which should
         * never be used on a cache entry since it can cause
         * the entry to grow without bound.
         */
        if (a->a_deletioncsn) size += sizeof(CSN);
        size += sizeof(Slapi_Attr);
    }

    return size;
}

static size_t slapi_dn_size(Slapi_DN *sdn)
{
    size_t size = 0;

    if (sdn == NULL) return 0;

    if (slapi_sdn_get_dn(sdn)) {
        size += slapi_sdn_get_ndn_len(sdn) + 1;
    }
    if (slapi_sdn_get_ndn(sdn)) {
        size += slapi_sdn_get_ndn_len(sdn) + 1;
    }

    return size;
}

/* return the approximate size of an entry --
 * useful for checking cache sizes, etc
 */
size_t 
slapi_entry_size(Slapi_Entry *e)
{
    u_long size = 0;

    /* doesn't include memory used by e_extension */

    if (e->e_uniqueid) size += strlen(e->e_uniqueid) + 1;
    if (e->e_dncsnset) size += csnset_size(e->e_dncsnset);
    if (e->e_maxcsn) size += sizeof( CSN );
    size += slapi_dn_size(&e->e_sdn);
    size += slapi_rdn_get_size(&e->e_srdn);
    size += slapi_attrlist_size(e->e_attrs);
    if (e->e_deleted_attrs) size += slapi_attrlist_size(e->e_deleted_attrs);
    if (e->e_virtual_attrs) size += slapi_attrlist_size(e->e_virtual_attrs);
    size += sizeof(Slapi_Entry);

    return size;
}


/*
 * return a complete copy of entry pointed to by "e"
 * entry extensions are duplicated, as well.
 */
Slapi_Entry *
slapi_entry_dup( const Slapi_Entry *e )
{
	Slapi_Entry	*ec;
	Slapi_Attr *a;
	Slapi_Attr *lastattr= NULL;
	struct attrs_in_extension *aiep;

	PR_ASSERT( NULL != e );

	ec = slapi_entry_alloc();

	/*
	 * init the new entry--some things (eg. locks in the entry) are not dup'ed
	*/
	slapi_entry_init(ec,NULL,NULL);

    slapi_sdn_copy(slapi_entry_get_sdn_const(e),&ec->e_sdn);
    slapi_srdn_copy(slapi_entry_get_srdn_const(e),&ec->e_srdn);

	/* duplicate the dncsn also */
	ec->e_dncsnset= csnset_dup(e->e_dncsnset);
	ec->e_maxcsn= csn_dup(e->e_maxcsn);

	/* don't use slapi_entry_set_uniqueid here because
       it will cause uniqueid to be added twice to the
	   attribute list
     */
	if ( e->e_uniqueid != NULL )
	{
		ec->e_uniqueid = slapi_ch_strdup( e->e_uniqueid ); /* JCM - UniqueID Dup function? */
	}

	for ( a = e->e_attrs; a != NULL; a = a->a_next )
	{
		Slapi_Attr *newattr= slapi_attr_dup(a);
		if(lastattr==NULL)
		{
			ec->e_attrs= newattr;
		}
		else
		{
			lastattr->a_next= newattr;
		}
		lastattr= newattr;
	}
	lastattr= NULL;
	for ( a = e->e_deleted_attrs; a != NULL; a = a->a_next )
	{
		Slapi_Attr *newattr= slapi_attr_dup(a);
		if(lastattr==NULL)
		{
			ec->e_deleted_attrs= newattr;
		}
		else
		{
			lastattr->a_next= newattr;
		}
		lastattr= newattr;
	}

	/* Copy flags as well */
	ec->e_flags = e->e_flags;

	/* Copy extension */
	for (aiep = attrs_in_extension; aiep && aiep->ext_type; aiep++) {
		aiep->ext_copy(e, ec);
	}

	ENTRY_DUMP(ec,"slapi_entry_dup");
	return( ec );
}

#ifdef ENTRY_DEBUG
static void
entry_dump( const Slapi_Entry *e, const char *text)
{
	const char *dn= slapi_entry_get_dn_const(e);
    LDAPDebug( LDAP_DEBUG_ANY, "Entry %s ptr=%lx dn=%s\n", text, e, (dn==NULL?"NULL":dn));
}
#endif

char *
slapi_entry_get_dn( Slapi_Entry *e )
{
    /* jcm - This is evil... we have to cast away the const. */
	return (char*)(slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)));
}
char *
slapi_entry_get_ndn( Slapi_Entry *e )
{
    /* jcm - This is evil... we have to cast away the const. */
	return (char*)(slapi_sdn_get_ndn(slapi_entry_get_sdn_const(e)));
}

const Slapi_DN *
slapi_entry_get_sdn_const( const Slapi_Entry *e )
{
    return &e->e_sdn;
}

Slapi_DN *
slapi_entry_get_sdn( Slapi_Entry *e )
{
    return &e->e_sdn;
}

const Slapi_RDN *
slapi_entry_get_srdn_const( const Slapi_Entry *e )
{
    return &e->e_srdn;
}

Slapi_RDN *
slapi_entry_get_srdn( Slapi_Entry *e )
{
    return &e->e_srdn;
}

const char *
slapi_entry_get_dn_const( const Slapi_Entry *e )
{
	return (slapi_sdn_get_dn(slapi_entry_get_sdn_const(e)));
}

const char *
slapi_entry_get_rdn_const( const Slapi_Entry *e )
{
	return (slapi_rdn_get_rdn(slapi_entry_get_srdn_const(e)));
}

/* slapi_rdn_get_nrdn could modify srdn in it. So, it cannot take const. */
const char *
slapi_entry_get_nrdn_const( const Slapi_Entry *e )
{
	const char *nrdn = 
	            slapi_rdn_get_nrdn(slapi_entry_get_srdn((Slapi_Entry *)e));
	if (NULL == nrdn) {
		const char *dn = slapi_entry_get_dn_const(e);
		if (dn) {
			/* cast away const */
			slapi_rdn_init_all_dn((Slapi_RDN *)&e->e_srdn, dn);
			nrdn = slapi_rdn_get_nrdn(slapi_entry_get_srdn((Slapi_Entry *)e));
		}
	}
	return nrdn;
}

/*
 * WARNING - The DN is passed in *not* copied.
 */
void
slapi_entry_set_dn( Slapi_Entry *e, char *dn )
{
    slapi_sdn_set_dn_passin(slapi_entry_get_sdn(e),dn);
}

void
slapi_entry_set_normdn( Slapi_Entry *e, char *dn )
{
    slapi_sdn_set_normdn_passin(slapi_entry_get_sdn(e), dn);
}

/*
 * WARNING - The DN is copied.
 *           The DN could be dn or RDN.
 */
void
slapi_entry_set_rdn( Slapi_Entry *e, char *dn )
{
    slapi_rdn_set_all_dn(slapi_entry_get_srdn(e),dn);
}

void
slapi_entry_set_sdn( Slapi_Entry *e, const Slapi_DN *sdn )
{
    slapi_sdn_copy(sdn,slapi_entry_get_sdn(e));
}

void
slapi_entry_set_srdn( Slapi_Entry *e, const Slapi_RDN *srdn )
{
    slapi_srdn_copy(srdn, slapi_entry_get_srdn(e));
}

const char *
slapi_entry_get_uniqueid( const Slapi_Entry *e )
{
	return( e->e_uniqueid );
}

/*
 * WARNING - The UniqueID is passed in *not* copied.
 */
void
slapi_entry_set_uniqueid( Slapi_Entry *e, char *uniqueid )
{
	e->e_uniqueid = uniqueid;
	
	/* also add it to the list of attributes - it makes things easier */
	slapi_entry_attr_set_charptr ( e, SLAPI_ATTR_UNIQUEID, uniqueid );
}

int
slapi_entry_first_attr( const Slapi_Entry *e, Slapi_Attr **a )
{
	return slapi_entry_next_attr( e, NULL, a);
}

int
slapi_entry_next_attr( const Slapi_Entry *e, Slapi_Attr *prevattr, Slapi_Attr **a )
{
	int done= 0;
	/*
	 * We skip over any attributes that have no present values.
	 * Our state information storage scheme can cause this, since
	 * we have to hang onto the deleted value state information.
	 * <jcm - actually we don't do this any more... so this skipping
	 * may now be redundant.>
	 */
	while(!done)
	{
		if(prevattr==NULL)
		{
			*a = e->e_attrs;
		}
		else
		{
			*a = prevattr->a_next;
		}
		if(*a!=NULL)
		{
			done= !valueset_isempty(&((*a)->a_present_values));
		}
		else
		{
			done= 1;
		}
		if(!done)
		{
			prevattr= *a;
		}
	}
	return( *a ? 0 : -1 );
}

int
slapi_entry_attr_find( const Slapi_Entry *e, const char *type, Slapi_Attr **a )
{
	int r= -1;
	*a = attrlist_find( e->e_attrs, type );
	if (*a != NULL)
	{
		if(valueset_isempty(&((*a)->a_present_values)))
		{
			/*
			 * We ignore attributes that have no present values.
			 * Our state information storage scheme can cause this, since
			 * we have to hang onto the deleted value state information.
			 */
			 *a= NULL;
		}
		else
		{
			r= 0;
		}
	}
	return r;
}

/* the following functions control virtual attribute cache invalidation */

static PRInt32 g_virtual_watermark = -1; /* good enough to init */

int slapi_entry_vattrcache_watermark_isvalid(const Slapi_Entry *e)
{
	return e->e_virtual_watermark == g_virtual_watermark;
}

void slapi_entry_vattrcache_watermark_set(Slapi_Entry *e)
{
	e->e_virtual_watermark = g_virtual_watermark;
}

void slapi_entry_vattrcache_watermark_invalidate(Slapi_Entry *e)
{
	e->e_virtual_watermark = 0;
}

void slapi_entrycache_vattrcache_watermark_invalidate()
{
	PR_AtomicIncrement(&g_virtual_watermark);
	if (g_virtual_watermark == 0) {
		PR_AtomicIncrement(&g_virtual_watermark);
	}
}

/*
 * slapi_entry_vattrcache_findAndTest()
 *
 * returns: 
 *		SLAPI_ENTRY_VATTR_NOT_RESOLVED--not found in vattrcache; *rc set to -1.
 *		SLAPI_ENTRY_VATTR_RESOLVED_ABSENT--present in vattrcache but empty value:
 *										means tjhat vattr type is not present in
 *										that entry.
 *		SLAPI_ENTRY_VATTR_RESOLVED_EXISTS--found vattr in the cache, in which
 *										case *rc contains the result of testing
 *										the filter f of type filter_type
 *										on the value of type in e.
 *										rc==-1=>not a filter match
 *										rc==0=>a filter match
 *										rc>0=>an LDAP error code.
 */

int
slapi_entry_vattrcache_findAndTest( const Slapi_Entry *e, const char *type,
								Slapi_Filter *f,
								filter_type_t filter_type,
								int *rc )
{
	Slapi_Attr *tmp_attr = NULL;

	int r= SLAPI_ENTRY_VATTR_NOT_RESOLVED; /* assume not resolved yet */
	*rc = -1;

	if( slapi_vattrcache_iscacheable(type) &&
		slapi_entry_vattrcache_watermark_isvalid(e) && e->e_virtual_attrs)
	{

		if(e->e_virtual_lock == NULL) {
            return r;
		}

		vattrcache_entry_READ_LOCK(e);
		tmp_attr = attrlist_find( e->e_virtual_attrs, type );
		if (tmp_attr != NULL)
		{
			if(valueset_isempty(&(tmp_attr->a_present_values)))
			{
				/*
				 * this is a vattr that has been
				 * cached already but does not exist
				 */
				r= SLAPI_ENTRY_VATTR_RESOLVED_ABSENT; /* hard coded for prototype */				
			}
			else
			{
				/*
				 * this is a cached vattr--test the filter on it.
				 * 
				*/
				r= SLAPI_ENTRY_VATTR_RESOLVED_EXISTS;
				if ( filter_type == FILTER_TYPE_AVA ) {
					*rc = plugin_call_syntax_filter_ava( tmp_attr,
														f->f_choice,
														&f->f_ava );
				} else if ( filter_type == FILTER_TYPE_SUBSTRING) {
					*rc = plugin_call_syntax_filter_sub( NULL, tmp_attr,
															&f->f_sub);
				} else if ( filter_type == FILTER_TYPE_PRES ) {
					/* type is there, that's all we need to know. */
					*rc = 0;
				}
			}
		}
		vattrcache_entry_READ_UNLOCK(e);
	}

	return r;
}

/* 
 * slapi_entry_vattrcache_find_values_and_type_ex()
 * 
 * returns: 
 *		SLAPI_ENTRY_VATTR_NOT_RESOLVED--not found in vattrcache.										
 *		SLAPI_ENTRY_VATTR_RESOLVED_ABSENT--found in vattrcache but empty value
 *										==>that vattr type is not present in the
 *										entry.
 *		SLAPI_ENTRY_VATTR_RESOLVED_EXISTS--found vattr in the vattr cache,
 *										in which case **results is a
 *										pointer to a duped Slapi_Valueset
 *										containing the values of type and
 *										**actual_type_name is the actual type
 *										name.
*/

int
slapi_entry_vattrcache_find_values_and_type_ex( const Slapi_Entry *e,
											const char *type,
											Slapi_ValueSet ***results,
											char ***actual_type_name)
{
	Slapi_Attr *tmp_attr = NULL;

	int r= SLAPI_ENTRY_VATTR_NOT_RESOLVED; /* assume not resolved yet */

	if( slapi_vattrcache_iscacheable(type) &&
		slapi_entry_vattrcache_watermark_isvalid(e) && e->e_virtual_attrs)
	{

		if(e->e_virtual_lock == NULL) {
            return r;
		}

		vattrcache_entry_READ_LOCK(e);
		tmp_attr = attrlist_find( e->e_virtual_attrs, type );
		if (tmp_attr != NULL)
		{
			if(valueset_isempty(&(tmp_attr->a_present_values)))
			{
				/*
				 * this is a vattr that has been
				 * cached already but does not exist
				 */
				r= SLAPI_ENTRY_VATTR_RESOLVED_ABSENT; /* hard coded for prototype */				
			}
			else
			{
				/*
				 * this is a cached vattr
				 * return a duped copy of the values and type
				*/
				char *vattr_type=NULL;

				r= SLAPI_ENTRY_VATTR_RESOLVED_EXISTS;
				*results = (Slapi_ValueSet**)slapi_ch_calloc(1, sizeof(**results));
				**results = valueset_dup(&(tmp_attr->a_present_values));

				*actual_type_name =
							(char**)slapi_ch_malloc(sizeof(**actual_type_name));
				slapi_attr_get_type( tmp_attr, &vattr_type );
				**actual_type_name = slapi_ch_strdup(vattr_type);
							
			}
		}
		vattrcache_entry_READ_UNLOCK(e);
	}

	return r;
}

/*
 * Deprecated in favour of slapi_entry_vattrcache_find_values_and_type_ex()
 * which meshes better with slapi_vattr_values_get_sp_ex().
*/
SLAPI_DEPRECATED int
slapi_entry_vattrcache_find_values_and_type( const Slapi_Entry *e,
											const char *type,
											Slapi_ValueSet **results,
											char **actual_type_name)
{
	Slapi_Attr *tmp_attr = NULL;

	int r= SLAPI_ENTRY_VATTR_NOT_RESOLVED; /* assume not resolved yet */

	if( slapi_vattrcache_iscacheable(type) &&
		slapi_entry_vattrcache_watermark_isvalid(e) && e->e_virtual_attrs)
	{

		if(e->e_virtual_lock == NULL) {
            return r;
		}

		vattrcache_entry_READ_LOCK(e);
		tmp_attr = attrlist_find( e->e_virtual_attrs, type );
		if (tmp_attr != NULL)
		{
			if(valueset_isempty(&(tmp_attr->a_present_values)))
			{
				/*
				 * this is a vattr that has been
				 * cached already but does not exist
				 */
				r= SLAPI_ENTRY_VATTR_RESOLVED_ABSENT; /* hard coded for prototype */				
			}
			else
			{
				/*
				 * this is a cached vattr
				 * return a duped copy of the values and type
				*/
				char *vattr_type=NULL;

				r= SLAPI_ENTRY_VATTR_RESOLVED_EXISTS;
				*results = valueset_dup(&(tmp_attr->a_present_values));

				slapi_attr_get_type( tmp_attr, &vattr_type );
				*actual_type_name = slapi_ch_strdup(vattr_type);
							
			}
		}
		vattrcache_entry_READ_UNLOCK(e);
	}

	return r;
}

SLAPI_DEPRECATED int
slapi_entry_attr_merge( Slapi_Entry *e, const char *type, struct berval **vals )
{
    Slapi_Value **values= NULL;
    int rc=0;
    valuearray_init_bervalarray(vals,&values); /* JCM SLOW FUNCTION */
    rc = slapi_entry_attr_merge_sv(e, type, values);
    valuearray_free(&values);
    return(rc);
}

int
slapi_entry_attr_merge_sv(Slapi_Entry *e, const char *type, Slapi_Value **vals )
{
    attrlist_merge_valuearray( &e->e_attrs, type, vals );
	return 0;
}

/*
 * Merge this valuset for type into e's vattrcache list.
 * Creates the type if necessary.
 * Dups valset.
 * Only merge's in cacheable vattrs.
*/

int
slapi_entry_vattrcache_merge_sv(Slapi_Entry *e, const char *type,
													Slapi_ValueSet *valset)
{
	Slapi_Value **vals = NULL;

	/* only attempt to merge if it's a cacheable attribute */
    if ( slapi_vattrcache_iscacheable(type) ) {
	
		if(e->e_virtual_lock == NULL) {
            return 0;
		}
	
		vattrcache_entry_WRITE_LOCK(e);

		if(!slapi_entry_vattrcache_watermark_isvalid(e) && e->e_virtual_attrs)
		{
			attrlist_free(e->e_virtual_attrs);
			e->e_virtual_attrs = NULL;
		}

		if(valset)
			vals = valueset_get_valuearray(valset);

		/* dups the type (if necessary) and vals */
	    attrlist_merge_valuearray( &e->e_virtual_attrs, type, vals);
		slapi_entry_vattrcache_watermark_set(e);

		vattrcache_entry_WRITE_UNLOCK(e);

	}

	return 0;
}

int
slapi_entry_attr_delete( Slapi_Entry *e, const char *type )
{
    return( attrlist_delete(&e->e_attrs, type) );
}

SLAPI_DEPRECATED int
slapi_entry_attr_replace( Slapi_Entry *e, const char *type, struct berval **vals )
{
    slapi_entry_attr_delete(e, type);
    slapi_entry_attr_merge(e, type, vals);
	return 0;
}

int
slapi_entry_attr_replace_sv( Slapi_Entry *e, const char *type, Slapi_Value **vals )
{
    slapi_entry_attr_delete(e, type);
    slapi_entry_attr_merge_sv(e, type, vals);
	return 0;
}

int
slapi_entry_add_value (Slapi_Entry *e, const char *type, const Slapi_Value *value)
{
    Slapi_Attr **a= NULL;
    attrlist_find_or_create(&e->e_attrs, type, &a);
    if(value != (Slapi_Value *) NULL) {
        slapi_valueset_add_value ( &(*a)->a_present_values, value);
    }
    return 0;
}


int
slapi_entry_add_string(Slapi_Entry *e, const char *type, const char *value)
{
	Slapi_Attr **a= NULL;
	attrlist_find_or_create(&e->e_attrs, type, &a);
	valueset_add_string ( &(*a)->a_present_values, value, CSN_TYPE_UNKNOWN, NULL);
	return 0;
}

int
slapi_entry_delete_string(Slapi_Entry *e, const char *type, const char *value)
{
	Slapi_Attr *a= attrlist_find(e->e_attrs, type);
	if (a != NULL)
		valueset_remove_string(a,&a->a_present_values, value);
	return 0;
}

/* caller must free with slapi_ch_array_free */
char **
slapi_entry_attr_get_charray( const Slapi_Entry* e, const char *type)
{
    char **parray = NULL;
    Slapi_Attr* attr = NULL;
	slapi_entry_attr_find(e, type, &attr);
	if(attr!=NULL)
	{
		int hint;
		Slapi_Value *v = NULL;
		for (hint = slapi_attr_first_value(attr, &v);
			 hint != -1;
			 hint = slapi_attr_next_value(attr, hint, &v))
		{
			const struct berval *bvp = slapi_value_get_berval(v);
			char *p = slapi_ch_malloc(bvp->bv_len + 1);
			memcpy(p, bvp->bv_val, bvp->bv_len);
			p[bvp->bv_len]= '\0';
			charray_add(&parray, p);
		}
	}
    return parray;
}

char *
slapi_entry_attr_get_charptr( const Slapi_Entry* e, const char *type)
{
    char *p= NULL;
    Slapi_Attr* attr;
	slapi_entry_attr_find(e, type, &attr);
	if(attr!=NULL)
	{
		Slapi_Value *v;
                const struct berval *bvp;
		slapi_valueset_first_value( &attr->a_present_values, &v);
                bvp = slapi_value_get_berval(v);
        p= slapi_ch_malloc(bvp->bv_len + 1);
        memcpy(p, bvp->bv_val, bvp->bv_len);
        p[bvp->bv_len]= '\0';
	}
    return p;
}

int
slapi_entry_attr_get_int( const Slapi_Entry* e, const char *type)
{
    int r= 0;
    Slapi_Attr* attr;
	slapi_entry_attr_find(e, type, &attr);
    if (attr!=NULL)
    {
		Slapi_Value *v;
		slapi_valueset_first_value( &attr->a_present_values, &v);
		r= slapi_value_get_int(v);
    }
    return r;
}

unsigned int
slapi_entry_attr_get_uint( const Slapi_Entry* e, const char *type)
{
    unsigned int r= 0;
    Slapi_Attr* attr;
	slapi_entry_attr_find(e, type, &attr);
    if (attr!=NULL)
    {
		Slapi_Value *v;
		slapi_valueset_first_value( &attr->a_present_values, &v);
		r= slapi_value_get_uint(v);
    }
    return r;
}

long
slapi_entry_attr_get_long( const Slapi_Entry* e, const char *type)
{
    long r = 0;
    Slapi_Attr* attr;
	slapi_entry_attr_find(e, type, &attr);
    if (attr!=NULL)
    {
		Slapi_Value *v;
		slapi_valueset_first_value( &attr->a_present_values, &v);
		r = slapi_value_get_long(v);
    }
    return r;
}

unsigned long
slapi_entry_attr_get_ulong( const Slapi_Entry* e, const char *type)
{
    unsigned long r = 0;
    Slapi_Attr* attr;
	slapi_entry_attr_find(e, type, &attr);
    if (attr!=NULL)
    {
		Slapi_Value *v;
		slapi_valueset_first_value( &attr->a_present_values, &v);
		r = slapi_value_get_ulong(v);
    }
    return r;
}

long long
slapi_entry_attr_get_longlong( const Slapi_Entry* e, const char *type)
{
    long long r = 0;
    Slapi_Attr* attr;
    slapi_entry_attr_find(e, type, &attr);
    if (attr!=NULL)
    {
        Slapi_Value *v;
        slapi_valueset_first_value( &attr->a_present_values, &v);
        r = slapi_value_get_longlong(v);
    }
    return r;
}

unsigned long long
slapi_entry_attr_get_ulonglong( const Slapi_Entry* e, const char *type)
{
    unsigned long long r = 0;
    Slapi_Attr* attr;
    slapi_entry_attr_find(e, type, &attr);
    if (attr!=NULL)
    {
        Slapi_Value *v;
        slapi_valueset_first_value( &attr->a_present_values, &v);
        r = slapi_value_get_ulonglong(v);
    }
    return r;
}

PRBool
slapi_entry_attr_get_bool( const Slapi_Entry* e, const char *type)
{
    PRBool r = PR_FALSE; /* default if no attr */
    Slapi_Attr* attr;
    slapi_entry_attr_find(e, type, &attr);
    if (attr!=NULL)
    {
        Slapi_Value *v;
        const struct berval *bvp;

        slapi_valueset_first_value( &attr->a_present_values, &v);
        bvp = slapi_value_get_berval(v);
        if ((bvp == NULL) || (bvp->bv_len == 0)) { /* none or empty == false */
            r = PR_FALSE;
        } else if (!PL_strncasecmp(bvp->bv_val, "on", bvp->bv_len)) {
            r = PR_TRUE;
        } else if (!PL_strncasecmp(bvp->bv_val, "off", bvp->bv_len)) {
            r = PR_FALSE;
        } else if (!PL_strncasecmp(bvp->bv_val, "true", bvp->bv_len)) {
            r = PR_TRUE;
        } else if (!PL_strncasecmp(bvp->bv_val, "false", bvp->bv_len)) {
            r = PR_FALSE;
        } else if (!PL_strncasecmp(bvp->bv_val, "yes", bvp->bv_len)) {
            r = PR_TRUE;
        } else if (!PL_strncasecmp(bvp->bv_val, "no", bvp->bv_len)) {
            r = PR_FALSE;
        } else { /* assume numeric: 0 - false: non-zero - true */
            r = (PRBool)slapi_value_get_ulong(v);
        }
    }
    return r;
}

void
slapi_entry_attr_set_charptr( Slapi_Entry* e, const char *type, const char *value)
{
	struct berval bv;
	struct berval *bvals[2];

	if (value) {
		bvals[0] = &bv;
		bvals[1] = NULL;
		bv.bv_val = (char*)value;
		bv.bv_len = strlen( value );
		slapi_entry_attr_replace( e, type, bvals );
	} else {
		slapi_entry_attr_delete( e, type );
	}
}

void
slapi_entry_attr_set_int( Slapi_Entry* e, const char *type, int l)
{
    char value[16];
	struct berval bv;
	struct berval *bvals[2];
	bvals[0] = &bv;
	bvals[1] = NULL;
    sprintf(value,"%d",l);
	bv.bv_val = value;
	bv.bv_len = strlen( value );
    slapi_entry_attr_replace( e, type, bvals );
}

void
slapi_entry_attr_set_uint( Slapi_Entry* e, const char *type, unsigned int l)
{
    char value[16];
	struct berval bv;
	struct berval *bvals[2];
	bvals[0] = &bv;
	bvals[1] = NULL;
    sprintf(value,"%u",l);
	bv.bv_val = value;
	bv.bv_len = strlen( value );
    slapi_entry_attr_replace( e, type, bvals );
}

void
slapi_entry_attr_set_long( Slapi_Entry* e, const char *type, long l)
{
    char value[16];
	struct berval bv;
	struct berval *bvals[2];
	bvals[0] = &bv;
	bvals[1] = NULL;
    sprintf(value,"%ld",l);
	bv.bv_val = value;
	bv.bv_len = strlen( value );
    slapi_entry_attr_replace( e, type, bvals );
}

void
slapi_entry_attr_set_ulong( Slapi_Entry* e, const char *type, unsigned long l)
{
    char value[16];
	struct berval bv;
	struct berval *bvals[2];
	bvals[0] = &bv;
	bvals[1] = NULL;
    sprintf(value,"%lu",l);
	bv.bv_val = value;
	bv.bv_len = strlen( value );
    slapi_entry_attr_replace( e, type, bvals );
}

/* JCM: The strcasecmp below should really be a bervalcmp
 * deprecatred in favour of slapi_entry_attr_has_syntax_value
 * which does respect the syntax of the attribute type.
*/

SLAPI_DEPRECATED int
slapi_entry_attr_hasvalue(const Slapi_Entry *e, const char *type, const char *value) /* JCM - (const char *) => (struct berval *) */
{
    int r= 0;
	Slapi_Attr *attr;
	Slapi_Value *sval;
    if(slapi_entry_attr_find(e, type, &attr)==0)
    {
        int i= slapi_attr_first_value( attr, &sval );
	    while(!r && i!=-1)
	    {
			const struct berval *val= slapi_value_get_berval(sval);
            r= (strcasecmp(val->bv_val,value)==0);
			i= slapi_attr_next_value( attr, i, &sval );
        }
    }
    return r;
}


/*
 * Checks if e contains an attr type with a value
 * of value.
 * Unlike slapi_entry_attr_hasvalue(), it does teh comparison
 * respecting the syntax of type.
 * 
 * returns non-zero if type has value in e, zero otherwise.
 *
 *
*/
	
int
slapi_entry_attr_has_syntax_value(const Slapi_Entry *e,
								const char *type,
								const Slapi_Value *value)
{
    int r= 0;
	Slapi_Attr *attr;

    if(slapi_entry_attr_find(e, type, &attr)==0)
    {
		const struct berval *bv = slapi_value_get_berval(value);

		if ( bv != NULL) {	
        	r = (slapi_attr_value_find(attr, bv) == 0);
		}
    
	}
	
    return r;			
}


int
slapi_entry_rdn_values_present( const Slapi_Entry *e )
{
	char		**dns, **rdns;
	int		i, rc;
	Slapi_Attr	*attr;
	struct ava ava;
	const char *dn = slapi_entry_get_dn_const(e);

	if (slapi_is_rootdse(dn))
		return 1; /* the root dse has no RDN, so it should default to TRUE */

	/* JCM Use the Slapi_RDN code */
	rc = 1;
	if ( (dns = slapi_ldap_explode_dn( slapi_entry_get_dn_const(e), 0 )) != NULL )
	{
		if ( (rdns = slapi_ldap_explode_rdn( dns[0], 0 )) != NULL )
		{
			for ( i = 0; rdns[i] != NULL; i++ )
			{
				if ( rdn2ava( rdns[i], &ava ) == 0 )
				{
					char *type = slapi_attr_syntax_normalize( ava.ava_type );
					if ( slapi_entry_attr_find( e, type, &attr ) != 0 )
					{
						rc = 0;
					}

					slapi_ch_free((void **)&type);

					if ( 0 == rc ) {	/* attribute not found */
						break;
					}

					if ( slapi_attr_value_find( attr, &(ava.ava_value) ) != 0 )
					{
						rc = 0;
						break;			/* value not found */
					}
				}
			}
			slapi_ldap_value_free( rdns );
		} else {
			rc = 0; /* Failure: the RDN seems invalid */
		}
		
		slapi_ldap_value_free( dns );
	}
	else
	{
	        rc = 0; /* failure: the RDN seems to be invalid */
	}

	return( rc );
}

int
slapi_entry_add_rdn_values( Slapi_Entry *e )
{
    const char *dn;
    char	**dns, **rdns;
    int	i, rc = LDAP_SUCCESS;
    Slapi_Value *foundVal;
    Slapi_Attr *attr;

    if ( NULL == e || (dn = slapi_entry_get_dn_const(e))==NULL ) {
        return( LDAP_SUCCESS );
    }

    if (slapi_is_rootdse(dn)) {
        return( LDAP_SUCCESS );
    }

    /* JCM Use the Slapi_RDN code */
    /* make sure RDN values are also in the entry */
    if ( (dns = slapi_ldap_explode_dn( dn, 0 )) == NULL ) {
        return( LDAP_INVALID_DN_SYNTAX );
    }
    if ( (rdns = slapi_ldap_explode_rdn( dns[0], 0 )) == NULL ) {
        slapi_ldap_value_free( dns );
        return( LDAP_INVALID_DN_SYNTAX );
    }
    slapi_ldap_value_free( dns );
    for ( i = 0; rdns[i] != NULL && rc == LDAP_SUCCESS; i++ ) {
        struct ava		ava;
        char			*type;
        
        if ( rdn2ava( rdns[i], &ava ) != 0 ) {
            slapi_ldap_value_free( rdns );
            return( LDAP_INVALID_DN_SYNTAX );
        }

        foundVal = NULL;

        type = slapi_attr_syntax_normalize( ava.ava_type );

        if ( slapi_entry_attr_find( e, type, &attr ) == 0 ) {
            rc = plugin_call_syntax_filter_ava_sv(attr, LDAP_FILTER_EQUALITY, 
                                                  &ava, &foundVal, 0);

            if (rc == 0 && foundVal != NULL) {
                const struct berval *bv = slapi_value_get_berval(foundVal);

                /* 
                 * A subtlety to consider is that LDAP does not
                 * allow two values which compare the same for
                 * equality in an attribute at once.
                 */

                if ((ava.ava_value.bv_len != bv->bv_len) ||
                    (memcmp(ava.ava_value.bv_val, bv->bv_val, bv->bv_len) != 0)) {
                    /* bytes not identical so reject */
                    LDAPDebug(LDAP_DEBUG_TRACE, "RDN value is not identical to entry value for type %s in entry %s\n", 
                               type, dn ? dn : "<null>", 0 );
#if 0
                    /* 
                     * This would be the right thing to do except that
                     * it breaks our own clients.
                     */
                    rc = LDAP_TYPE_OR_VALUE_EXISTS;
#endif
                }
                /* exact same ava already present in entry, that's OK */
            }
        }

        if (foundVal == NULL) {
            struct berval *vals[2];

            vals[0] = &ava.ava_value;
            vals[1] = NULL;
            rc = slapi_entry_add_values( e, type, vals );
        }

        slapi_ch_free( (void **)&type );
    }
    slapi_ldap_value_free( rdns );
    
    return( rc );
}

/*
 * Function: slapi_entry_has_children
 *
 * Returns: 0 if "p" has no children, 1 if "p" has children.
 *
 * Description: We (RJP+DB) modified this code to take advantage
 *             of the subordinatecount operational attribute that
 *             each entry now has. 
 *
 * Author/Modifier: RJP
 */
int
slapi_entry_has_children(const Slapi_Entry *entry)
{
	Slapi_Attr *attr;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> slapi_has_children( %s )\n", slapi_entry_get_dn_const(entry), 0, 0);

	/*If the subordinatecount exists, and it's nonzero, then return 1.*/
	if (slapi_entry_attr_find( entry, "numsubordinates", &attr) == 0)
	{
		Slapi_Value *sval;
		slapi_attr_first_value( attr, &sval );
		if(sval!=NULL)
		{	   
			const struct berval *bval = slapi_value_get_berval( sval );
			if(bval!=NULL)
			{
				/* The entry has the attribute, and it's non-zero */
				if (strcmp(bval->bv_val, "0") != 0)
				{
					LDAPDebug( LDAP_DEBUG_TRACE, "<= slapi_has_children 1\n", 0, 0, 0 );
					return(1);
				}
			}
		}
	}
	LDAPDebug( LDAP_DEBUG_TRACE, "<= slapi_has_children 0\n", 0, 0, 0 );
	return(0);
}

/*
 * Renames an entry to simulate a MODRDN operation
 */
int
slapi_entry_rename(Slapi_Entry *e, const char *newrdn, int deleteoldrdn, Slapi_DN *newsuperior)
{
    int err = LDAP_SUCCESS;
    Slapi_DN *olddn = NULL;
    Slapi_Mods *smods = NULL;
    Slapi_DN newsrdn;

    LDAPDebug( LDAP_DEBUG_TRACE, "=> slapi_entry_rename\n", 0, 0, 0 );

    slapi_sdn_init(&newsrdn);
    /* Check if entry or newrdn are NULL. */
    if (!e || !newrdn) {
        err = LDAP_PARAM_ERROR;
        goto done;
    }

    /* Get the old DN. */
    olddn = slapi_entry_get_sdn(e);

    /* If deleteoldrdn, find old RDN values and remove them from the entry. */
    if (deleteoldrdn) {
        char *type = NULL;
        char * val = NULL;
        int num_rdns = 0;
        int i = 0;
        Slapi_RDN *oldrdn = slapi_rdn_new_sdn(olddn);

        /* Create mods based on the number of rdn elements. */
        num_rdns = slapi_rdn_get_num_components(oldrdn);
        smods = slapi_mods_new();
        slapi_mods_init(smods, num_rdns + 2);

        /* Loop through old rdns and construct a mod to remove the value. */
        for (i = 0; i < num_rdns; i++) {
            if (slapi_rdn_get_next(oldrdn, i, &type, &val) != -1) {
                slapi_mods_add(smods, LDAP_MOD_DELETE, type, strlen(val), val);
            }
        }
        slapi_rdn_free(&oldrdn);

        /* Apply the mods to the entry. */
        if ((err = slapi_entry_apply_mods(e, slapi_mods_get_ldapmods_byref(smods))) != LDAP_SUCCESS) {
            /* A problem was encountered applying the mods.  Bail. */
            goto done;
        }
    }

    /* We remove the parentid and entrydn since the backend will change these.
     * We don't want to give the caller an inconsistent entry. */
    slapi_entry_attr_delete(e, "parentid");
    slapi_entry_attr_delete(e, "entrydn");

    /* Build new DN.  If newsuperior is set, just use "newrdn,newsuperior".  If
     * newsuperior is not set, need to add newrdn to old superior. */
    slapi_sdn_init_dn_byref(&newsrdn, newrdn);
    if (newsuperior) {
        slapi_sdn_set_parent(&newsrdn, newsuperior);
    } else {
        Slapi_DN oldparent;

        slapi_sdn_init(&oldparent);
        slapi_sdn_get_parent(olddn, &oldparent);
        slapi_sdn_set_parent(&newsrdn, &oldparent);
        slapi_sdn_done(&oldparent);
    }

    /* Set the new DN in the entry.  This hands off the memory used by newdn to the entry. */
    slapi_entry_set_sdn(e, &newsrdn);

    /* Set the RDN in the entry. */
    /* note - there isn't a slapi_entry_set_rdn_from_sdn function */
    slapi_rdn_done(slapi_entry_get_srdn(e));
    slapi_rdn_init_all_sdn(slapi_entry_get_srdn(e),&newsrdn);

    /* Add RDN values to entry. */
    err = slapi_entry_add_rdn_values(e);

done:
    slapi_mods_free(&smods);
    slapi_sdn_done(&newsrdn);

    LDAPDebug( LDAP_DEBUG_TRACE, "<= slapi_entry_rename\n", 0, 0, 0 );
    return err;
}

/*
 * Apply a set of modifications to an entry 
 */
int
slapi_entry_apply_mods( Slapi_Entry *e, LDAPMod **mods )
{
	return entry_apply_mods(e, mods);
}

/*
 * Apply a single mod to an entry
 */ 
int slapi_entry_apply_mod( Slapi_Entry *e, LDAPMod *mod )
{
	return entry_apply_mod(e, mod);
}

int
entry_apply_mods( Slapi_Entry *e, LDAPMod **mods )
{
	return entry_apply_mods_ignore_error(e, mods, -1);
}

int
entry_apply_mods_ignore_error( Slapi_Entry *e, LDAPMod **mods, int ignore_error )
{
	int	err;
	LDAPMod **mp = NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "=> entry_apply_mods\n", 0, 0, 0 );

	err = LDAP_SUCCESS;
	for ( mp = mods; mp && *mp; mp++ )
	{
		err = entry_apply_mod( e, *mp );
		if(err == ignore_error){
			(*mp)->mod_op = LDAP_MOD_IGNORE;
		} else if ( err != LDAP_SUCCESS ) {
			break;
		}
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<= entry_apply_mods %d\n", err, 0, 0 );
	return( err );
}

/*
 * apply mod and store the result in the extension
 * return value:  1 - mod is applied and stored in extension
 *               -1 - mod is applied and failed
 *                0 - mod is nothing to do with extension
 */
int
slapi_entry_apply_mod_extension(Slapi_Entry *e, const LDAPMod *mod, int modcnt)
{
    Slapi_Value **vals = NULL;
    struct attrs_in_extension *aiep;
    int err = LDAP_SUCCESS;
    int rc = 0; /* by default, mod is nothing to do with extension */

    if ((NULL == e) || (NULL == mod)) {
        return err;
    }

    if (modcnt < 0) {
        int i;
        for (i = 0; mod->mod_bvalues && mod->mod_bvalues[i]; i++) ;
        modcnt = i;
    }

    for (aiep = attrs_in_extension; aiep && aiep->ext_type; aiep++) {
        vals = NULL;
        if (0 == strcasecmp(mod->mod_type, aiep->ext_type)) {
            rc = 1;
            switch (mod->mod_op & ~LDAP_MOD_BVALUES) {
            case LDAP_MOD_ADD:
                if (modcnt > 0) {
                    valuearray_init_bervalarray(mod->mod_bvalues, &vals);
                    if (vals) {
                        /* vals is consumed if successful. */
                        err = aiep->ext_set(e, vals, SLAPI_EXT_SET_ADD);
                        if (err) {
                            slapi_log_error(SLAPI_LOG_FATAL, "entry_apply_mod",
                                            "ADD: Failed to set %s to extension\n",
                                            aiep->ext_type);
                            valuearray_free(&vals);
                            goto bail;
                        }
                    } else {
                        slapi_log_error(SLAPI_LOG_FATAL, "entry_apply_mod",
                                        "ADD: %s has no values\n", 
                                        aiep->ext_type);
                        goto bail;
                    }
                }
                break;
            case LDAP_MOD_DELETE:
                if (modcnt > 0) {
                    err = aiep->ext_get(e, &vals);
                    if (err) {
                        slapi_log_error(SLAPI_LOG_FATAL, "entry_apply_mod",
                                        "DEL: Failed to get %s from extension\n",
                                        aiep->ext_type);
                        goto bail;
                    }
                    if (vals && *vals) {
                        Slapi_Value **myvals = NULL;
                        valuearray_add_valuearray(&myvals, vals, 0);
                        err = valuearray_subtract_bvalues(myvals,
                                                          mod->mod_bvalues);
                        if (err > 0) { /* err values are subtracted */
                            /* 
                             * mvals contains original values minus
                             * to-be-deleted value. ext_set replaces the
                             * original value with the delta.
                             */
                            /* myvals is consumed if successful. */
                            err = aiep->ext_set(e, myvals, SLAPI_EXT_SET_REPLACE);
                            if (err) {
                                slapi_log_error(SLAPI_LOG_FATAL,
                                                "entry_apply_mod",
                                                "DEL: Failed to set %s "
                                                "to extension\n",
                                                aiep->ext_type);
                                valuearray_free(&myvals);
                                goto bail;
                            }
                        }
                    }
                } else {
                    /* ext_set replaces the existing value with NULL */
                    err = aiep->ext_set(e, NULL, SLAPI_EXT_SET_REPLACE);
                    if (err) {
                        slapi_log_error(SLAPI_LOG_FATAL, "entry_apply_mod",
                                        "DEL: Failed to set %s to extension\n",
                                        aiep->ext_type);
                        goto bail;
                    }
                }
                break;
            case LDAP_MOD_REPLACE:
                if (modcnt > 0) {
                    /* ext_set replaces the existing value with the new value */
                    valuearray_init_bervalarray(mod->mod_bvalues, &vals);
                    if (vals) {
                        /* vals is consumed if successful. */
                        err = aiep->ext_set(e, vals, SLAPI_EXT_SET_REPLACE);
                        if (err) {
                            slapi_log_error(SLAPI_LOG_FATAL, "entry_apply_mod",
                                            "REPLACE: Failed to set %s to extension\n",
                                            aiep->ext_type);
                            valuearray_free(&vals);
                            goto bail;
                        }
                    } else {
                        slapi_log_error(SLAPI_LOG_FATAL, "entry_apply_mod",
                                        "REPLACE: %s has no values\n", 
                                        aiep->ext_type);
                        goto bail;
                    }
                }
                break;
            default:
                rc = 0;
                break;
            }
        }
    }
bail:
    if (rc > 0) {
        if (err) {
            rc = -1;
        } else {
            rc = 1;
        }
    }
    return rc;
}

/*
 * Apply a modification to an entry 
 */
int
entry_apply_mod( Slapi_Entry *e, const LDAPMod *mod )
{
	int i;
	int bvcnt;
	int	err = LDAP_SUCCESS;
	PRBool sawsubentry=PR_FALSE;

	for ( i = 0; mod->mod_bvalues != NULL && mod->mod_bvalues[i] != NULL; i++ ) {
	  if((strcasecmp(mod->mod_type,"objectclass") == 0)  
              && (strncasecmp((const char *)mod->mod_bvalues[i]->bv_val,"ldapsubentry",mod->mod_bvalues[i]->bv_len) == 0)) 
	    sawsubentry=PR_TRUE;
	  LDAPDebug( LDAP_DEBUG_ARGS, "   %s: %s\n", mod->mod_type, mod->mod_bvalues[i]->bv_val, 0 );
	}
	bvcnt = i;

	/*
	 * If err == 0, apply mod.
	 * If err == 1, mod is successfully set to extension.
	 * If err == -1, setting mod to extension failed.
	 */ 
	err = slapi_entry_apply_mod_extension(e, mod, bvcnt);
	if (err) {
		if (1 == err) {
			err = LDAP_SUCCESS;
		} else {
			err = LDAP_OPERATIONS_ERROR;
		}
		goto done;
	}

	switch ( mod->mod_op & ~LDAP_MOD_BVALUES )
	{
	case LDAP_MOD_ADD:
		LDAPDebug( LDAP_DEBUG_ARGS, "   add: %s\n", mod->mod_type, 0, 0 );
		if(sawsubentry) e->e_flags |= SLAPI_ENTRY_LDAPSUBENTRY;
		err = slapi_entry_add_values( e, mod->mod_type, mod->mod_bvalues );
		break;

	case LDAP_MOD_DELETE:
		LDAPDebug( LDAP_DEBUG_ARGS, "   delete: %s\n", mod->mod_type, 0, 0 );
		if(sawsubentry) e->e_flags |= 0;
		err = slapi_entry_delete_values( e, mod->mod_type, mod->mod_bvalues );
		break;

	case LDAP_MOD_REPLACE:
		LDAPDebug( LDAP_DEBUG_ARGS, "   replace: %s\n", mod->mod_type, 0, 0 );
		err = entry_replace_values( e, mod->mod_type, mod->mod_bvalues );
		break;
	}
done:
	LDAPDebug( LDAP_DEBUG_ARGS, "   -\n", 0, 0, 0 );

	return( err );
}


/*
 * Add an array of "vals" to entry "e".
 */
SLAPI_DEPRECATED int
slapi_entry_add_values(
    Slapi_Entry		*e,
    const char		*type,
    struct berval	**vals
)
{
    Slapi_Value **values= NULL;
    int rc=0;
    valuearray_init_bervalarray(vals,&values); /* JCM SLOW FUNCTION */
    rc=slapi_entry_add_values_sv(e,type,values);
    valuearray_free(&values);
    return(rc);
}

/*
 * Add an array of "vals" to entry "e".
 */
int
slapi_entry_add_values_sv(Slapi_Entry *e,
			      const char *type,
			      Slapi_Value **vals)
{
	int rc= LDAP_SUCCESS;
    if (valuearray_isempty(vals))
	{
		/*
		 * No values to add (unexpected but acceptable).
		 */
	}
	else
	{
		Slapi_Attr **a= NULL;
		Slapi_Attr **alist= &e->e_attrs;
		attrlist_find_or_create(alist, type, &a);
		if (slapi_attr_is_dn_syntax_attr(*a)) {
			valuearray_dn_normalize_value(vals);
			(*a)->a_flags |= SLAPI_ATTR_FLAG_NORMALIZED_CES;
		}
		rc= attr_add_valuearray(*a,vals,slapi_entry_get_dn_const(e));
    }
    return( rc );
}

/*
 * Add a value set of "vs" to entry "e".
 *
 * 0 is success anything else failure.
 */

int
slapi_entry_add_valueset(Slapi_Entry *e, const char *type, Slapi_ValueSet *vs)
{
    Slapi_Value *v;

    int i= slapi_valueset_first_value(vs,&v);
    while(i!=-1) {

        slapi_entry_add_value( e, type, v);
        i= slapi_valueset_next_value(vs,i,&v);
    }/* while */

    return(0);
}


/*
 * Delete an array of bervals from entry.
 *
 * Note that if this function fails, it leaves the values for "type" within
 * "e" in an indeterminate state. The present value set may be truncated.
 */
SLAPI_DEPRECATED int
slapi_entry_delete_values(
    Slapi_Entry		*e,
    const char		*type,
    struct berval	**vals
)
{
    Slapi_Value **values= NULL;
    int rc=0;
    valuearray_init_bervalarray(vals,&values); /* JCM SLOW FUNCTION */
    rc=slapi_entry_delete_values_sv(e,type,values);
    valuearray_free(&values);
    return(rc);
}

static int
delete_values_sv_internal(
    Slapi_Entry		*e,
    const char		*type,
    Slapi_Value		**valuestodelete,
    int				flags
)
{
	Slapi_Attr *a;
	int retVal= LDAP_SUCCESS;

	/*
	 * If type is in the protected_attrs_all list, we could ignore the failure,
	 * as the attribute could only exist in the entry in the memory when the 
	 * add/mod operation is done, while the retried entry from the db does not
	 * contain the attribute.
	 */
#if defined(USE_OLD_UNHASHED)
	if (is_type_protected(type) || is_type_forbidden(type))
#else
	if (is_type_protected(type))
#endif
	{
		flags |= SLAPI_VALUE_FLAG_IGNOREERROR;
	}

	/* delete the entire attribute */
	if ( valuestodelete == NULL || valuestodelete[0] == NULL ){
		LDAPDebug( LDAP_DEBUG_ARGS, "removing entire attribute %s\n",
		    type, 0, 0 );
		retVal = attrlist_delete( &e->e_attrs, type);
		if (flags & SLAPI_VALUE_FLAG_IGNOREERROR) {
			return LDAP_SUCCESS;
		}
		return(retVal ? LDAP_NO_SUCH_ATTRIBUTE : LDAP_SUCCESS);
	}

	/* delete specific values - find the attribute first */
	a= attrlist_find(e->e_attrs, type);
	if ( a == NULL ) {
		LDAPDebug( LDAP_DEBUG_ARGS, "could not find attribute %s\n",
		    type, 0, 0 );
		if (flags & SLAPI_VALUE_FLAG_IGNOREERROR) {
			return LDAP_SUCCESS;
		}
		return( LDAP_NO_SUCH_ATTRIBUTE );
	}

	{
		retVal= valueset_remove_valuearray(&a->a_present_values, a, valuestodelete, flags, NULL);
		if(retVal==LDAP_SUCCESS)
		{
			/*
			 * all values have been deleted -- remove entire attribute
			 */
			if ( valueset_isempty(&a->a_present_values) )
			{
				attrlist_delete( &e->e_attrs, a->a_type );
			}
		}
		else
		{
			/* Failed 
			 * - Duplicate value
			 * - Value not found
			 * - Operations error
			 */
			if ( retVal==LDAP_OPERATIONS_ERROR )
			{
				LDAPDebug( LDAP_DEBUG_ANY, "Possible existing duplicate "
					"value for attribute type %s found in "
					"entry %s\n", a->a_type, slapi_entry_get_dn_const(e), 0 );
			}
			if (flags & SLAPI_VALUE_FLAG_IGNOREERROR) {
				retVal = LDAP_SUCCESS;
			}
		}
	}
	
	return( retVal );
}


/*
 * Delete an array of present values from an entry.
 *
 * Note that if this function fails, it leaves the values for "type" within
 * "e" in an indeterminate state. The present value set may be truncated.
 */
int
slapi_entry_delete_values_sv(
    Slapi_Entry		*e,
    const char		*type,
    Slapi_Value	**valuestodelete
)
{
	return( delete_values_sv_internal( e, type, valuestodelete,
			0 /* Do Not Ignore Errors */ ));
}


int
entry_replace_values(
    Slapi_Entry		*e,
    const char		*type,
    struct berval	**vals
)
{
    return attrlist_replace( &e->e_attrs, type, vals );
}

int
entry_replace_values_with_flags(
    Slapi_Entry		*e,
    const char		*type,
    struct berval	**vals,
    int flags
)
{
    return attrlist_replace_with_flags( &e->e_attrs, type, vals, flags );
}

int
slapi_entry_flag_is_set( const Slapi_Entry *e, unsigned char flag )
{
	return( e->e_flags & flag );
}

void slapi_entry_set_flag( Slapi_Entry *e, unsigned char flag)
{
	e->e_flags |= flag;
}

void slapi_entry_clear_flag( Slapi_Entry *e, unsigned char flag)
{
	e->e_flags &= ~flag;
}


/*
 * Add the missing values in `vals' to an entry.
 *
 * Note that if this function fails, it leaves the values for "type" within
 * "e" in an indeterminate state. The present value set may be truncated.
 */
int
slapi_entry_merge_values_sv(
    Slapi_Entry		*e,
    const char		*type,
    Slapi_Value		**vals
)
{
	int		rc;

	rc = delete_values_sv_internal( e, type, vals, SLAPI_VALUE_FLAG_IGNOREERROR );

	if ( rc == LDAP_SUCCESS || rc == LDAP_NO_SUCH_ATTRIBUTE ) {
		rc = slapi_entry_attr_merge_sv( e, type, vals );
	}

	return( rc );
}

void
send_referrals_from_entry(Slapi_PBlock *pb, Slapi_Entry *referral)
{
    Slapi_Value *val=NULL;
    Slapi_Attr *attr=NULL;
    int i=0, numValues=0;
    struct berval **refscopy=NULL;
    struct berval **url=NULL;
    
    slapi_entry_attr_find( referral, "ref", &attr );
    if(attr != NULL) {
        slapi_attr_get_numvalues(attr, &numValues );
        if(numValues > 0) {
            url=(struct berval **) slapi_ch_malloc((numValues + 1) * sizeof(struct berval*));
        }
        for (i = slapi_attr_first_value(attr, &val); i != -1;
        i = slapi_attr_next_value(attr, i, &val)) {
            url[i]=(struct berval*)slapi_value_get_berval(val);
        }
        url[numValues]=NULL;
    }
    refscopy = ref_adjust(pb, url, slapi_entry_get_sdn(referral), 0);
    send_ldap_result(pb, LDAP_REFERRAL,
        slapi_entry_get_dn(referral), NULL, 0, refscopy );
    if(url != NULL) {
        slapi_ch_free( (void **)&url );
    }
    if ( refscopy != NULL ) {
       ber_bvecfree( refscopy );
    }
}

/*
 * slapi_entry_diff: perform diff between entry e1 and e2
 *                   and set mods to smods which updates e1 to e2.
 *        diff_ctrl: SLAPI_DUMP_NOOPATTRS => skip operational attributes
 */
void
slapi_entry_diff(Slapi_Mods *smods, Slapi_Entry *e1, Slapi_Entry *e2, int diff_ctrl)
{
    Slapi_Attr *e1_attr = NULL;
    Slapi_Attr *e2_attr = NULL;
    char *e1_attr_name = NULL;
    char *e2_attr_name = NULL;
    int rval = 0;

    slapi_mods_init(smods, 0);

    for (slapi_entry_first_attr(e1, &e1_attr); e1_attr;
         slapi_entry_next_attr(e1, e1_attr, &e1_attr))
    {
        /* skip operational attributes if not requested */
        if ((diff_ctrl & SLAPI_DUMP_NOOPATTRS) &&
            slapi_attr_flag_is_set(e1_attr, SLAPI_ATTR_FLAG_OPATTR))
            continue;

        slapi_attr_get_type(e1_attr, &e1_attr_name);
        rval = slapi_entry_attr_find(e2, e1_attr_name, &e2_attr);
        if (0 == rval)
        {
            int i;
            Slapi_Value *e1_val;
            /* attr e1_attr_names is shared with e2 */
            /* XXX: not very efficient.
             *      needs to be rewritten for the schema w/ lots of attributes
             */
            for (i = slapi_attr_first_value(e1_attr, &e1_val); i != -1;
                 i = slapi_attr_next_value(e1_attr, i, &e1_val))
            {
                if (0 != slapi_attr_value_find(e2_attr,
                                               slapi_value_get_berval(e1_val)))
                {
                    /* attr-value e1_val not found in e2_attr; add it */
                    LDAPDebug(LDAP_DEBUG_TRACE,
                        "slapi_entry_diff: attr-val of %s is not in e2; "
                        "add it\n",
                        e1_attr_name, 0, 0);
                    slapi_mods_add(smods, LDAP_MOD_ADD, e1_attr_name,
                                   e1_val->bv.bv_len, e1_val->bv.bv_val);
                }
            }
        }
        else
        {
            /* attr e1_attr_names not found in e2 */
            LDAPDebug(LDAP_DEBUG_TRACE,
                      "slapi_entry_diff: attr %s is not in e2; add it\n",
                      e1_attr_name, 0, 0);
            slapi_mods_add_mod_values(smods, LDAP_MOD_ADD,
                                      e1_attr_name, 
                                      attr_get_present_values(e1_attr));
        }
    }
    /* if the attribute is multi-valued, the untouched values should be put */

    for (slapi_entry_first_attr(e2, &e2_attr); e2_attr;
         slapi_entry_next_attr(e2, e2_attr, &e2_attr)) {
        /* skip operational attributes if not requested */
        if ((diff_ctrl & SLAPI_DUMP_NOOPATTRS) &&
            slapi_attr_flag_is_set(e2_attr, SLAPI_ATTR_FLAG_OPATTR))
            continue;

        slapi_attr_get_type(e2_attr, &e2_attr_name);
        rval = slapi_entry_attr_find(e1, e2_attr_name, &e1_attr);
        if (0 == rval)
        {
            int i;
            Slapi_Value *e2_val;
            /* attr e2_attr_names is shared with e1 */
            /* XXX: not very efficient.
             *      needs to be rewritten for the schema w/ lots of attributes
             */
            for (i = slapi_attr_first_value(e2_attr, &e2_val); i != -1;
                 i = slapi_attr_next_value(e2_attr, i, &e2_val))
            {
                if (0 != slapi_attr_value_find(e1_attr,
                                               slapi_value_get_berval(e2_val)))
                {
                    /* attr-value e2_val not found in e1_attr; delete it */
                    LDAPDebug(LDAP_DEBUG_TRACE,
                        "slapi_entry_diff: attr-val of %s is not in e1; "
                        "delete it\n",
                        e2_attr_name, 0, 0);
                    slapi_mods_add(smods, LDAP_MOD_DELETE, e2_attr_name,
                                   e2_val->bv.bv_len, e2_val->bv.bv_val);
                }
            }
        }
        else
        {
            /* attr e2_attr_names not in e1 */
            LDAPDebug(LDAP_DEBUG_TRACE,
                      "slapi_entry_diff: attr %s is not in e1; delete it\n",
                      e2_attr_name, 0, 0);
            slapi_mods_add_mod_values(smods, LDAP_MOD_DELETE, e2_attr_name, NULL);
        }
    }

    return;
}

/* delete the entry (and sub entries if any) specified with dn */
static void
delete_subtree(Slapi_PBlock *pb, const char *dn, void *plg_id)
{
    Slapi_PBlock mypb;
    int ret = 0;
    int opresult;

    slapi_search_internal_set_pb(pb, dn, LDAP_SCOPE_SUBTREE, "(objectclass=*)",
        NULL, 0, NULL, NULL, plg_id, 0);
    slapi_search_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &ret);
    if (ret == LDAP_SUCCESS) {
        Slapi_Entry **entries = NULL;
        Slapi_Entry **ep = NULL;
        Slapi_DN *rootDN = slapi_sdn_new_dn_byval(dn);
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        for (ep = entries; ep && *ep; ep++) {
            const Slapi_DN *sdn = slapi_entry_get_sdn_const(*ep);
            
            if (slapi_sdn_compare(sdn, rootDN) == 0)
                continue;
            pblock_init(&mypb);
            slapi_delete_internal_set_pb(&mypb, slapi_sdn_get_dn(sdn),
                NULL, NULL, plg_id, 0);
            slapi_delete_internal_pb(&mypb);
            slapi_pblock_get(&mypb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
            pblock_done(&mypb);
        }
        slapi_sdn_free(&rootDN);
    }
    pblock_done(pb);

    pblock_init(pb);
    slapi_delete_internal_set_pb(pb, dn, NULL, NULL, plg_id, 0);
    slapi_delete_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &opresult);
    pblock_done(pb);
}

/*
 * slapi_entries_diff: diff between entry array old_entries and curr_entries
 *                     (testall == 0) => return immediately after the 1st diff
 *                     (testall != 0) => scan all the entries
 *                     (force_update == 0) => just print the diff info
 *                     (force_update != 0) => force to go back to old
 *                     
 *                     return 0, if identical
 *                     return 1, otherwise
 */
int
slapi_entries_diff(Slapi_Entry **old_entries, Slapi_Entry **curr_entries,
                   int testall, const char *logging_prestr,
                   const int force_update, void *plg_id)
{
    char *my_logging_prestr = "";
    Slapi_Entry **oep, **cep;
    int rval = 0;
    Slapi_PBlock pb;
#define SLAPI_ENTRY_FLAG_DIFF_IN_BOTH 0x80

    if (NULL != logging_prestr && '\0' != *logging_prestr)
    {
        my_logging_prestr = slapi_ch_smprintf("%s ", logging_prestr);
    }

    for (oep = old_entries; oep != NULL && *oep != NULL; oep++) {
        for (cep = curr_entries; cep != NULL && *cep != NULL; cep++) {
            if (!slapi_sdn_compare(slapi_entry_get_sdn_const(*oep),
                                   slapi_entry_get_sdn_const(*cep))) {
                Slapi_Mods *smods = slapi_mods_new();
                LDAPMod *mod;
                int isfirst = 1;

                /* check the attr diff and do modify */
                slapi_entry_diff(smods, *oep, *cep, SLAPI_DUMP_NOOPATTRS);

                for (mod = slapi_mods_get_first_mod(smods);
                     mod != NULL;
                     mod = slapi_mods_get_next_mod(smods))
                {
                    rval = 1;
                    if (isfirst)
                    {
                        LDAPDebug(LDAP_DEBUG_ANY, "%sEntry %s\n", my_logging_prestr,
                                  slapi_entry_get_dn_const(*oep), 0);
                        isfirst = 0;
                    }

                    switch (mod->mod_op & ~LDAP_MOD_BVALUES)
                    {
                    case LDAP_MOD_DELETE:
                        LDAPDebug(LDAP_DEBUG_ANY,
                                  "  Del Attribute %s Value %s\n",
                                  mod->mod_type, mod->mod_bvalues?
                                  mod->mod_bvalues[0]->bv_val:"N/A", 0);
                        break;
                    case LDAP_MOD_ADD:
                        LDAPDebug(LDAP_DEBUG_ANY, 
                                  "  Add Attribute %s Value %s\n",
                                  mod->mod_type, mod->mod_bvalues[0]->bv_val, 0);
                        break;
                    case LDAP_MOD_REPLACE:
                        LDAPDebug(LDAP_DEBUG_ANY,
                                  "  Rep Attribute %s Value %s\n",
                                  mod->mod_type, mod->mod_bvalues[0]->bv_val, 0);
                        break;
                    default:
                        LDAPDebug(LDAP_DEBUG_ANY,
                                  "  Unknown op %d Attribute %s\n",
                                  mod->mod_op & ~LDAP_MOD_BVALUES,
                                  mod->mod_type, 0);
                        break;
                    }

                    if (!testall)
                    {
                        slapi_mods_free(&smods);
                        goto out;
                    }
                }
                if (0 == isfirst && force_update && testall)
                {
                    pblock_init(&pb);
                    slapi_modify_internal_set_pb_ext(&pb, 
                                slapi_entry_get_sdn_const(*oep),
                                slapi_mods_get_ldapmods_byref(smods),
                                NULL, NULL, plg_id, 0);

                    slapi_modify_internal_pb(&pb);
                    pblock_done(&pb);
                }

                slapi_mods_free(&smods);
                slapi_entry_set_flag(*oep, SLAPI_ENTRY_FLAG_DIFF_IN_BOTH);
                slapi_entry_set_flag(*cep, SLAPI_ENTRY_FLAG_DIFF_IN_BOTH);
            }
        }
    }

    for (oep = old_entries; oep != NULL && *oep != NULL; oep++) {
        if (slapi_entry_flag_is_set(*oep, SLAPI_ENTRY_FLAG_DIFF_IN_BOTH)) {
            slapi_entry_clear_flag(*oep, SLAPI_ENTRY_FLAG_DIFF_IN_BOTH);
        } else {
            rval = 1;
            LDAPDebug(LDAP_DEBUG_ANY, "Add %sEntry %s\n", 
                      my_logging_prestr, slapi_entry_get_dn_const(*oep), 0);
            if (testall)
            {
                if (force_update)
                {
                    LDAPMod **mods;
                    slapi_entry2mods(*oep, NULL, &mods);
                    pblock_init(&pb);
                    slapi_add_internal_set_pb(&pb, 
                                              slapi_entry_get_dn_const(*oep), mods, NULL, plg_id, 0);
                    slapi_add_internal_pb(&pb);
                    freepmods(mods);
                    pblock_done(&pb);
                }
            }
            else
            {
                goto out;
            }
        }
    }
            
    for (cep = curr_entries; cep != NULL && *cep != NULL; cep++) {
        if (slapi_entry_flag_is_set(*cep, SLAPI_ENTRY_FLAG_DIFF_IN_BOTH)) {
            slapi_entry_clear_flag(*cep, SLAPI_ENTRY_FLAG_DIFF_IN_BOTH);
        } else {
            rval = 1;
                          
            LDAPDebug(LDAP_DEBUG_ANY, "Del %sEntry %s\n", 
                      my_logging_prestr, slapi_entry_get_dn_const(*cep), 0);

            if (testall)
            {
                if (force_update) {
                    pblock_init(&pb);
                    delete_subtree(&pb, slapi_entry_get_dn_const(*cep), plg_id);
                    pblock_done(&pb);
                }
            }
            else
            {
                goto out;
            }
        }
    }
out:
    if (NULL != logging_prestr && '\0' != *logging_prestr)
        slapi_ch_free_string(&my_logging_prestr);

    return rval;
}

/* a helper function to set special rdn to a tombstone entry */
/* Since this a tombstone, it requires a special treatment for rdn*/
static int
_entry_set_tombstone_rdn(Slapi_Entry *e, const char *normdn)
{
    int rc = 0;
    char *tombstone_rdn = slapi_ch_strdup(normdn);
    if ((0 == PL_strncasecmp(tombstone_rdn, SLAPI_ATTR_UNIQUEID,
                             sizeof(SLAPI_ATTR_UNIQUEID) - 1)) &&
        (NULL == PL_strstr(tombstone_rdn, RUV_STORAGE_ENTRY_UNIQUEID))) {
        /* dn starts with "nsuniqueid=" and this is not an RUV */
        char *sepp = PL_strchr(tombstone_rdn, ',');
        /* dn looks like this:
         * nsuniqueid=042d8081-...-ca8fe9f7,uid=tuser,o=abc.com 
         * create a new srdn for the original dn
         * uid=tuser,o=abc.com
         */
        if (sepp) {
            Slapi_RDN mysrdn = {0};
            rc = slapi_rdn_init_all_dn(&mysrdn, sepp + 1);
            if (rc) {
                slapi_log_error(SLAPI_LOG_FATAL, "_entry_set_tombstone_rdn",
                                "Failed to convert DN %s to RDN\n", sepp + 1);
                slapi_rdn_done(&mysrdn);
                goto bail;
            }
            sepp = PL_strchr(sepp + 1, ',');
            if (sepp) {
                /* nsuniqueid=042d8081-...-ca8fe9f7,uid=tuser, */
                /*                                           ^ */
                *sepp = '\0';
                slapi_rdn_replace_rdn(&mysrdn, tombstone_rdn);
                slapi_entry_set_srdn(e, &mysrdn);
            }
            slapi_rdn_done(&mysrdn);
        }
    }
bail:
    slapi_ch_free_string(&tombstone_rdn);
    return rc;
}
