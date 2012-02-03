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

/* attr.c - backend routines for dealing with attributes */

#include "back-ldbm.h"

struct attrinfo *
attrinfo_new()
{
    struct attrinfo *p= (struct attrinfo *)slapi_ch_calloc(1, sizeof(struct attrinfo));
    return p;
}

void
attrinfo_delete(struct attrinfo **pp)
{
    if(pp!=NULL && *pp!=NULL)
    {
        idl_release_private(*pp);
        (*pp)->ai_key_cmp_fn = NULL;
        slapi_ch_free((void**)&((*pp)->ai_type));
        slapi_ch_free((void**)(*pp)->ai_index_rules);
        slapi_ch_free((void**)&((*pp)->ai_attrcrypt));
        attr_done(&((*pp)->ai_sattr));
        slapi_ch_free((void**)pp);
        *pp= NULL;
    }
}

static int
attrinfo_internal_delete( caddr_t data, caddr_t arg )
{
    struct attrinfo *n = (struct attrinfo *)data;
    attrinfo_delete(&n);
    return 0;
}

void
attrinfo_deletetree(ldbm_instance *inst)
{
    avl_free( inst->inst_attrs, attrinfo_internal_delete );
}


static int
ainfo_type_cmp(
    char		*type,
    struct attrinfo	*a
)
{
	return( strcasecmp( type, a->ai_type ) );
}

static int
ainfo_cmp(
    struct attrinfo	*a,
    struct attrinfo	*b
)
{
	return( strcasecmp( a->ai_type, b->ai_type ) );
}

/*
 * Called when a duplicate "index" line is encountered.
 *
 * returns 1 => original from init code, indexmask updated
 *	   2 => original not from init code, warn the user
 *
 * Hard coded to return a 1 always...
 *
 */

static int
ainfo_dup(
	  struct attrinfo	*a,
	  struct attrinfo	*b
)
{
  /* merge duplicate indexing information */
  if (b->ai_indexmask == 0 || b->ai_indexmask == INDEX_OFFLINE) {
    a->ai_indexmask = INDEX_OFFLINE; /* turns off all indexes */
    charray_free ( a->ai_index_rules );
    a->ai_index_rules = NULL;
  }
  a->ai_indexmask |= b->ai_indexmask;
  if ( b->ai_indexmask & INDEX_RULES ) {
    charray_merge( &a->ai_index_rules, b->ai_index_rules, 1 );
  }
  
  return( 1 );
}

void
ainfo_get(
    backend *be,
    char		*type,
    struct attrinfo	**at
)
{
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
	if ( (*at = (struct attrinfo *) avl_find( inst->inst_attrs, type,
	    ainfo_type_cmp )) == NULL ) {
		if ( (*at = (struct attrinfo *) avl_find( inst->inst_attrs,
		    LDBM_PSEUDO_ATTR_DEFAULT, ainfo_type_cmp )) == NULL ) {
			return;
		}
	}
}

void
_set_attr_substrlen(int index, char *str, int **substrlens)
{
	char *p = NULL;
	/* nsSubStrXxx=<VAL> is passed; 
	   set it to the attribute's plugin */
	p = strchr(str, '=');
	if (NULL != p) {
		long sublen = strtol(++p, (char **)NULL, 10);
		if (sublen > 0) {	/* 0 is not acceptable */
			if (NULL == *substrlens) {
				*substrlens = (int *)slapi_ch_calloc(1, 
												sizeof(int) * INDEX_SUBSTRLEN);
			}
			(*substrlens)[index] = sublen;
		}
	}
}

void
attr_index_config(
    backend *be,
    char		*fname,
    int			lineno,
    Slapi_Entry *e,
    int			init,
    int 		indextype_none
)
{
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
	int	j = 0;
	struct attrinfo	*a;
	int return_value = -1;
	int *substrlens = NULL;
	int need_compare_fn = 0;
	int hasIndexType = 0;
	const char *attrsyntax_oid = NULL;
	const struct berval *attrValue;
	Slapi_Value *sval;
	Slapi_Attr *attr;
	int mr_count = 0;

	/* Get the cn */
	if (0 == slapi_entry_attr_find(e, "cn", &attr)) {
		 slapi_attr_first_value(attr, &sval);
		 attrValue = slapi_value_get_berval(sval);
	} else {
		LDAPDebug(LDAP_DEBUG_ANY, "attr_index_config: Missing indexing arguments\n", 0, 0, 0);
		return;
	}

	a = attrinfo_new();
	slapi_attr_init(&a->ai_sattr, attrValue->bv_val);
	/*
	 *  we can't just set a->ai_type to the type from a->ai_sattr
	 *  if the type has attroptions or subtypes, ai_sattr.a_type will
	 *  contain them - but for the purposes of indexing, we don't want them
	 */
	a->ai_type = slapi_attr_basetype( attrValue->bv_val, NULL, 0 );
	attrsyntax_oid = attr_get_syntax_oid(&a->ai_sattr);
	a->ai_indexmask = 0;

	if(indextype_none){
		/* This is the same has having none for indexType, but applies to the whole entry */
		a->ai_indexmask = INDEX_OFFLINE;
	} else {
		slapi_entry_attr_find(e, "nsIndexType", &attr);
		for (j = slapi_attr_first_value(attr, &sval); j != -1;j = slapi_attr_next_value(attr, j, &sval)) {
			hasIndexType = 1;
			attrValue = slapi_value_get_berval(sval);
			if ( strncasecmp( attrValue->bv_val, "pres", 4 ) == 0 ) {
				a->ai_indexmask |= INDEX_PRESENCE;
			} else if ( strncasecmp( attrValue->bv_val, "eq", 2 ) == 0 ) {
				a->ai_indexmask |= INDEX_EQUALITY;
			} else if ( strncasecmp( attrValue->bv_val, "approx", 6 ) == 0 ) {
				a->ai_indexmask |= INDEX_APPROX;
			} else if ( strncasecmp( attrValue->bv_val, "subtree", 7 ) == 0 ) {
				/* subtree should be located before "sub" */
				a->ai_indexmask |= INDEX_SUBTREE;
				a->ai_dup_cmp_fn = entryrdn_compare_dups;
			} else if ( strncasecmp( attrValue->bv_val, "sub", 3 ) == 0 ) {
				a->ai_indexmask |= INDEX_SUB;
			} else if ( strncasecmp( attrValue->bv_val, "none", 4 ) == 0 ) {
				if ( a->ai_indexmask != 0 ) {
					LDAPDebug(LDAP_DEBUG_ANY,
						"%s: line %d: index type \"none\" cannot be combined with other types\n",
						fname, lineno, 0);
				}
				a->ai_indexmask = INDEX_OFFLINE; /* note that the index isn't available */
			} else {
				LDAPDebug(LDAP_DEBUG_ANY,
					"%s: line %d: unknown index type \"%s\" (ignored)\n",
					fname, lineno, attrValue->bv_val);
				LDAPDebug(LDAP_DEBUG_ANY,
					"valid index types are \"pres\", \"eq\", \"approx\", or \"sub\"\n",
					0, 0, 0);
			}
		}
		if(hasIndexType == 0){
			/* indexType missing, error out */
			LDAPDebug(LDAP_DEBUG_ANY, "attr_index_config: Missing index type\n", 0, 0, 0);
			attrinfo_delete(&a);
			return;
		}
	}

	/* compute a->ai_index_rules: */
	/* for index rules there are two uses:
	 * 1) a simple way to define an ordered index to support <= and >= searches
	 * for those attributes which do not have an ORDERING matching rule defined
	 * for them in their schema definition.  The index generated is not a :RULE:
	 * index, it is a normal = EQUALITY index, with the keys ordered using the
	 * comparison function provided by the syntax plugin for the attribute.  For
	 * example - the uidNumber attribute has INTEGER syntax, but the standard
	 * definition of the attribute does not specify an ORDERING matching rule.
	 * By default, this means that you cannot perform searches like
	 * (uidNumber>=501) - but many users expect to be able to perform this type of
	 * search.  By specifying that you want an ordered index, using an integer
	 * matching rule, you can support indexed seaches of this type.
	 * 2) a RULE index - the index key prefix is :NAMEOROID: - this is used
	 * to support extensible match searches like (cn:fr-CA.3:=gilles), which would
	 * find the index key :fr-CA.3:gilles in the cn index.
	 * We check first to see if this is a simple ordered index - user specified an
	 * ordering matching rule compatible with the attribute syntax, and there is
	 * a compare function.  If not, we assume it is a RULE index definition.
	 */

	if(0 == slapi_entry_attr_find(e, "nsMatchingRule", &attr)){
		char** official_rules;
		size_t k = 0;

		/* Get a count and allocate an array for the official matching rules */
		slapi_attr_get_numvalues(attr, &mr_count);
		official_rules = (char**)slapi_ch_malloc ((mr_count + 1) * sizeof (char*));

		for (j = slapi_attr_first_value(attr, &sval); j != -1;j = slapi_attr_next_value(attr, j, &sval)) {
			/* Check that index_rules[j] is an official OID */
			char* officialOID = NULL;
			IFP mrINDEX = NULL;
			Slapi_PBlock* pb = NULL;
			int do_continue = 0; /* can we skip the RULE parsing stuff? */
			attrValue = slapi_value_get_berval(sval);

			if (strstr(attrValue->bv_val, INDEX_ATTR_SUBSTRBEGIN)) {
				_set_attr_substrlen(INDEX_SUBSTRBEGIN, attrValue->bv_val, &substrlens);
				do_continue = 1; /* done with j - next j */
			} else if (strstr(attrValue->bv_val, INDEX_ATTR_SUBSTRMIDDLE)) {
				_set_attr_substrlen(INDEX_SUBSTRMIDDLE, attrValue->bv_val,	&substrlens);
				do_continue = 1; /* done with j - next j */
			} else if (strstr(attrValue->bv_val, INDEX_ATTR_SUBSTREND)) {
				_set_attr_substrlen(INDEX_SUBSTREND, attrValue->bv_val, &substrlens);
				do_continue = 1; /* done with j - next j */
			/* check if this is a simple ordering specification
			   for an attribute that has no ordering matching rule */
			} else if (slapi_matchingrule_is_ordering(attrValue->bv_val, attrsyntax_oid) &&
					   slapi_matchingrule_can_use_compare_fn(attrValue->bv_val) &&
					   !a->ai_sattr.a_mr_ord_plugin) { /* no ordering for this attribute */
				need_compare_fn = 1; /* get compare func for this attr */
				do_continue = 1; /* done with j - next j */
			}

			if (do_continue) {
				continue; /* done with index_rules[j] */
			}

			/* must be a RULE specification */
			pb = slapi_pblock_new();
			/*
			 *  next check if this is a RULE type index
			 *  try to actually create an indexer and see if the indexer
			 *  actually has a regular INDEX_FN or an INDEX_SV_FN
			 */
			if (!slapi_pblock_set (pb, SLAPI_PLUGIN_MR_OID, attrValue->bv_val) &&
					!slapi_pblock_set (pb, SLAPI_PLUGIN_MR_TYPE, a->ai_type) &&
					!slapi_mr_indexer_create (pb) &&
					((!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_INDEX_FN, &mrINDEX) &&
					  mrINDEX != NULL) ||
					 (!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_INDEX_SV_FN, &mrINDEX) &&
					  mrINDEX != NULL)) &&
					!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_OID, &officialOID) &&
					officialOID != NULL) {
				if (!strcasecmp (attrValue->bv_val, officialOID)) {
					official_rules[k++] = slapi_ch_strdup (officialOID);
				} else {
					char* preamble = slapi_ch_smprintf("%s: line %d", fname, lineno);
					LDAPDebug (LDAP_DEBUG_ANY, "%s: use \"%s\" instead of \"%s\" (ignored)\n",
								  preamble, officialOID, attrValue->bv_val);
					slapi_ch_free((void**)&preamble);
				}
			} else { /* we don't know what this is */
				LDAPDebug (LDAP_DEBUG_ANY, "%s: line %d: "
						   "unknown or invalid matching rule \"%s\" in index configuration (ignored)\n",
						   fname, lineno, attrValue->bv_val);
			}

			{  /*
			    *  It would improve speed to save the indexer, for future use.
			    * But, for simplicity, we destroy it now:
			    */
				IFP mrDESTROY = NULL;
				if (!slapi_pblock_get (pb, SLAPI_PLUGIN_DESTROY_FN, &mrDESTROY) &&
					mrDESTROY != NULL) {
					mrDESTROY (pb);
				}
			}
			slapi_pblock_destroy (pb);
		}
		official_rules[k] = NULL;
		a->ai_substr_lens = substrlens;
		if (k > 0) {
			a->ai_index_rules = official_rules;
			a->ai_indexmask |= INDEX_RULES;
		} else {
			slapi_ch_free((void**)&official_rules);
		}
	}

	/* initialize the IDL code's private data */
	return_value = idl_init_private(be, a);
	if (0 != return_value) {
		/* fatal error, exit */
		LDAPDebug(LDAP_DEBUG_ANY,"%s: line %d:Fatal Error: Failed to initialize attribute structure\n",
				fname, lineno, 0);
		exit( 1 );
	}

	/* if user didn't specify an ordering rule in the index config,
	   see if the schema def for the attr defines one */
	if (!need_compare_fn && a->ai_sattr.a_mr_ord_plugin) {
		need_compare_fn = 1;
	}

	if (need_compare_fn) {
		int rc = attr_get_value_cmp_fn( &a->ai_sattr, &a->ai_key_cmp_fn );
		if (rc != LDAP_SUCCESS) {
			LDAPDebug(LDAP_DEBUG_ANY,
						  "The attribute [%s] does not have a valid ORDERING matching rule - error %d:s\n",
						  a->ai_type, rc, ldap_err2string(rc));
			a->ai_key_cmp_fn = NULL;
		}
	}

	if ( avl_insert( &inst->inst_attrs, a, ainfo_cmp, ainfo_dup ) != 0 ) {
		/* duplicate - existing version updated */
		attrinfo_delete(&a);
	}
}

/*
 * Function that creates a new attrinfo structure and
 * inserts it into the avl tree. This is used by code
 * that wants to store attribute-level configuration data
 * e.g. attribute encryption, but where the attr_info
 * structure doesn't exist because the attribute in question
 * is not indexed.
 */
void
attr_create_empty(backend *be,char *type,struct attrinfo **ai)
{
	ldbm_instance *inst = (ldbm_instance *) be->be_instance_info;
	struct attrinfo	*a = attrinfo_new();
	slapi_attr_init(&a->ai_sattr, type);
	a->ai_type = slapi_ch_strdup(type);
	if ( avl_insert( &inst->inst_attrs, a, ainfo_cmp, ainfo_dup ) != 0 ) {
		/* duplicate - existing version updated */
        attrinfo_delete(&a);
		ainfo_get(be,type,&a);
	}
	*ai = a;
}

/* Code for computed attributes */
extern char* hassubordinates;
extern char* numsubordinates;

static int
ldbm_compute_evaluator(computed_attr_context *c,char* type,Slapi_Entry *e,slapi_compute_output_t outputfn)
{
	int rc = 0;

	if ( strcasecmp (type, numsubordinates ) == 0)
	{
		Slapi_Attr *read_attr = NULL;
		/* Check to see whether this attribute is already present in the entry */
		if (0 != slapi_entry_attr_find( e, numsubordinates, &read_attr ))
		{
			/* If not, we return it as zero */
			Slapi_Attr our_attr;
			slapi_attr_init(&our_attr, numsubordinates);
			our_attr.a_flags = SLAPI_ATTR_FLAG_OPATTR;
			valueset_add_string(&our_attr.a_present_values,"0",CSN_TYPE_UNKNOWN,NULL);
			rc = (*outputfn) (c, &our_attr, e);
			attr_done(&our_attr);
			return (rc);
		}
	}
	if ( strcasecmp (type, hassubordinates ) == 0)
	{
		Slapi_Attr *read_attr = NULL;
		Slapi_Attr our_attr;
		slapi_attr_init(&our_attr, hassubordinates);
		our_attr.a_flags = SLAPI_ATTR_FLAG_OPATTR;
		/* This attribute is always computed */
		/* Check to see whether the subordinate count attribute is already present in the entry */
		rc = slapi_entry_attr_find( e, numsubordinates, &read_attr );
		if ( (0 != rc) || slapi_entry_attr_hasvalue(e,numsubordinates,"0") ) {
			/* If not, or present and zero, we return FALSE, otherwise TRUE */
			valueset_add_string(&our_attr.a_present_values,"FALSE",CSN_TYPE_UNKNOWN,NULL);
		} else {
			valueset_add_string(&our_attr.a_present_values,"TRUE",CSN_TYPE_UNKNOWN,NULL);
		}
		rc = (*outputfn) (c, &our_attr, e);
		attr_done(&our_attr);
		return (rc);
	}

	return -1; /* I see no ships */
}

/* What are we doing ?
	The back-end can't search properly for the hasSubordinates and
	numSubordinates attributes. The reason being that they're not
	always stored on entries, so filter test fails to do the correct thing.
	However, it is possible to rewrite a given search to one
	which will work, given that numSubordinates is present when non-zero,
	and we maintain a presence index for numSubordinates.
 */
/* Searches we rewrite here : 
    substrings of the form
	(hassubordinates=TRUE)  to (&(numsubordinates=*)(numsubordinates>=1)) [indexed]
	(hassubordinates=FALSE) to (&(objectclass=*)(!(numsubordinates=*)))   [not indexed]
	(hassubordinates=*) to (objectclass=*)   [not indexed]
	(numsubordinates=*) to (objectclass=*)   [not indexed]
 	(numsubordinates=x)  to (&(numsubordinates=*)(numsubordinates=x)) [indexed]
 	(numsubordinates>=x)  to (&(numsubordinates=*)(numsubordinates>=x)) [indexed where X > 0]
 	(numsubordinates<=x)  to (&(numsubordinates=*)(numsubordinates<=x)) [indexed]

	anything else involving numsubordinates and hassubordinates we flag as unwilling to perform

*/

/* Before calling this function, you must free all the parts
   which will be overwritten, this function dosn't know
   how to do that */
static int replace_filter(Slapi_Filter	*f, char *s)
{
	Slapi_Filter	*newf = NULL;
	Slapi_Filter	*temp = NULL;
/* LP: Fix for defect 515161. Crash on AIX
 * slapi_str2filter is a nasty function that mangle whatever gets passed in.
 * AIX crashes on altering the literal string.
 * So we need to allocate the string and then free it.
 */
	char *buf = slapi_ch_strdup(s);

	newf = slapi_str2filter(buf);
	slapi_ch_free((void **)&buf);
	
	if (NULL == newf) {
		return -1;
	}

	/* Now take the parts of newf and put them in f */
	/* An easy way to do this is to preserve the "next" ptr */
	temp = f->f_next;
	*f = *newf;
	f->f_next = temp;
	/* Free the new filter husk */
	slapi_ch_free((void**)&newf);
	return 0;
}

static void find_our_friends(char *s, int *has, int *num)
{
		*has = (0 == strcasecmp(s,"hassubordinates"));
		if (!(*has)) {
			*num = (0 == strcasecmp(s,LDBM_NUMSUBORDINATES_STR));
		}
}

/* Free the parts of a filter we're about to overwrite */
void free_the_filter_bits(Slapi_Filter	*f)
{
	/* We need to free: */
	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
	case LDAP_FILTER_GE:
	case LDAP_FILTER_LE:
	case LDAP_FILTER_APPROX:
		ava_done( &f->f_ava );
		break;

	case LDAP_FILTER_PRESENT:
		if ( f->f_type != NULL ) {
			slapi_ch_free( (void**)&(f->f_type) );
		}
		break;

	default:
		break;
	}
}

static int grok_and_rewrite_filter(Slapi_Filter	*f)
{
	Slapi_Filter *p = NULL;
	int has = 0;
	int num = 0;
	char *rhs = NULL;
	struct berval rhs_berval;

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
		/* Does this involve either of our target attributes ? */
		find_our_friends(f->f_ava.ava_type,&has,&num);
		if (has || num) {
			rhs = f->f_ava.ava_value.bv_val;
			if (has) {
				if (0 == strcasecmp(rhs,"TRUE")) {
					free_the_filter_bits(f);
					replace_filter(f,"(&(numsubordinates=*)(numsubordinates>=1))");
				} else if (0 == strcasecmp(rhs, "FALSE")) {
					free_the_filter_bits(f);
					replace_filter(f,"(&(objectclass=*)(!(numsubordinates=*)))");
				} else {
					return 1; /* Filter we can't rewrite */
				}
			}
			if (num) {							
				int rhs_number = 0;

				rhs_number = atoi(rhs);
				if (rhs_number > 0) {

					char * theType=f->f_ava.ava_type;
					rhs_berval = f->f_ava.ava_value;
					replace_filter(f,"(&(numsubordinates=*)(numsubordinates=x))");
					/* Now fixup the resulting filter so that x = rhs */
					slapi_ch_free((void**)&(f->f_and->f_next->f_ava.ava_value.bv_val));
					/*free type also */
					slapi_ch_free((void**)&theType);

					f->f_and->f_next->f_ava.ava_value = rhs_berval;
				} else {
					if (rhs_number == 0) {
						/* This is the same as hassubordinates=FALSE */
						free_the_filter_bits(f);
						replace_filter(f,"(&(objectclass=*)(!(numsubordinates=*)))");
					} else {
						return 1;
					}
				}
			}
			return 0;
		}
		break;

	case LDAP_FILTER_GE:
		find_our_friends(f->f_ava.ava_type,&has,&num);
		if (has) {
			return 1; /* Makes little sense for this attribute */
		}
		if (num) {
			int rhs_num = 0;
			rhs = f->f_ava.ava_value.bv_val;
			/* is the value zero ? */
			rhs_num = atoi(rhs);
			if (0 == rhs_num) {
				/* If so, rewrite to same as numsubordinates=* */
				free_the_filter_bits(f);
				replace_filter(f,"(objectclass=*)");
			} else {
				/* Rewrite to present and GE the rhs */
				char * theType=f->f_ava.ava_type;
				rhs_berval = f->f_ava.ava_value;

				replace_filter(f,"(&(numsubordinates=*)(numsubordinates>=x))");
				/* Now fixup the resulting filter so that x = rhs */
				slapi_ch_free((void**)&(f->f_and->f_next->f_ava.ava_value.bv_val));
				/*free type also */
				slapi_ch_free((void**)&theType);
				
				f->f_and->f_next->f_ava.ava_value = rhs_berval;
			}
			return 0;
		} 
		break;

	case LDAP_FILTER_LE:
		find_our_friends(f->f_ava.ava_type,&has,&num);
		if (has) {
			return 1; /* Makes little sense for this attribute */
		}
		if (num) {
			/* One could imagine doing this one, but it's quite hard */
			return 1;
		}
		break;

	case LDAP_FILTER_APPROX:
		find_our_friends(f->f_ava.ava_type,&has,&num);
		if (has || num) {
			/* Not allowed */
			return 1;
		}
		break;

	case LDAP_FILTER_SUBSTRINGS:
		find_our_friends(f->f_sub_type,&has,&num);
		if (has || num) {
			/* Not allowed */
			return 1;
		}
		break;

	case LDAP_FILTER_PRESENT:
		find_our_friends(f->f_type,&has,&num);
		if (has || num) {
			/* we rewrite this search to (objectclass=*) */
			slapi_ch_free((void**)&(f->f_type));
			f->f_type = slapi_ch_strdup("objectclass");
			return 0;
		} /* We already weeded out the special search we use use in the console */
		break;
	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		for ( p = f->f_list; p != NULL; p = p->f_next ) {
			grok_and_rewrite_filter( p );
		}
		break;

	default:
		return -1; /* Bad, might be an extended filter or something */
	}
	return -1;
}

static int
ldbm_compute_rewriter(Slapi_PBlock *pb)
{
	int rc = -1;
	char *fstr= NULL;

	/*
	 * We need to look at the filter and see whether it might contain
	 * numSubordinates or hasSubordinates. We want to do a quick check
	 * before we look thoroughly.
	 */
	slapi_pblock_get( pb, SLAPI_SEARCH_STRFILTER, &fstr );

	if ( NULL != fstr ) {
		if (PL_strcasestr(fstr, "subordinates")) {
			Slapi_Filter	*f = NULL;
			/* Look for special filters we want to leave alone */
			if (0 == strcasecmp(fstr, "(&(numsubordinates=*)(numsubordinates>=1))" )) {
				; /* Do nothing, this one works OK */
			} else {
				/* So let's grok the filter in detail and try to rewrite it */
				slapi_pblock_get( pb, SLAPI_SEARCH_FILTER, &f );
				rc = grok_and_rewrite_filter(f);
				if (0 == rc) {
					/* he rewrote it ! fixup the string version */
					/* slapi_pblock_set( pb, SLAPI_SEARCH_STRFILTER, newfstr ); */
				}
			}
		}
	}
	return rc; 
}


int ldbm_compute_init()
{
	int ret = 0;
	ret = slapi_compute_add_evaluator(ldbm_compute_evaluator);
	if (0 == ret) {
		ret = slapi_compute_add_search_rewriter(ldbm_compute_rewriter);
	}
	return ret;
}

