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

/* filterentry.c - apply a filter to an entry */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"

static int	test_filter_list();
static int	test_extensible_filter();

static int	vattr_test_filter_list();
static int test_filter_access( Slapi_PBlock	*pb, Slapi_Entry*e,
								char * attr_type, struct berval *attr_val);
static int slapi_vattr_filter_test_ext_internal( Slapi_PBlock *pb, Slapi_Entry *e,
    struct slapi_filter	*f, int	 verify_access, int	only_check_access, int *access_check_done);

static char *opt_str = 0;
static int opt = 0;

static int optimise_filter_acl_tests()
{
	if(!opt_str)
	{
		opt_str = getenv( "NS_DS_OPT_FILT_ACL_EVAL" );
		if(opt_str)
			opt = !strcasecmp(opt_str, "false");
		else
			opt = 0;
		if(!opt_str)
			opt_str = "dummy";
	}

	return opt;
}

/*
 * slapi_filter_test - test a filter against a single entry.
 * returns	0	filter matched
 *			-1	filter did not match
 *			>0	an ldap error code
 */

int
slapi_filter_test(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*f,
    int			verify_access
)
{
	return slapi_filter_test_ext(pb,e,f,verify_access,0);
}

/*
 * slapi_filter_test_simple - test without checking access control
 *
 * returns	0	filter matched
 *			-1	filter did not match
 *			>0	an ldap error code
 */
int
slapi_filter_test_simple(
    Slapi_Entry		*e,
    struct slapi_filter	*f
)
{
	return slapi_vattr_filter_test_ext(NULL,e,f,0,0);
}

/*
 * slapi_filter_test_ext - full-feature filter test function
 *
 * returns	0	filter matched
 *			-1	filter did not match
 *			>0	an ldap error code
 */

int
slapi_filter_test_ext_internal(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*f,
    int			verify_access,
	int			only_check_access,
	int			*access_check_done
)
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> slapi_filter_test_ext\n", 0, 0, 0 );

	/*
	 * RJP: Not sure if this is semantically right, but we have to
	 * return something if f is NULL. If there is no filter,
	 * then we say that it did match and return 0.
	 */
	if ( f == NULL) {
	  return(0);
	}

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
		LDAPDebug( LDAP_DEBUG_FILTER, "    EQUALITY\n", 0, 0, 0 );
		rc = test_ava_filter( pb, e, e->e_attrs, &f->f_ava, LDAP_FILTER_EQUALITY,
					verify_access , only_check_access, access_check_done);
		break;

	case LDAP_FILTER_SUBSTRINGS:
		LDAPDebug( LDAP_DEBUG_FILTER, "    SUBSTRINGS\n", 0, 0, 0 );
		rc = test_substring_filter( pb, e, f, verify_access , only_check_access, access_check_done);
		break;

	case LDAP_FILTER_GE:
		LDAPDebug( LDAP_DEBUG_FILTER, "    GE\n", 0, 0, 0 );
		rc = test_ava_filter( pb, e, e->e_attrs, &f->f_ava, LDAP_FILTER_GE,
					verify_access , only_check_access, access_check_done);
		break;

	case LDAP_FILTER_LE:
		LDAPDebug( LDAP_DEBUG_FILTER, "    LE\n", 0, 0, 0 );
		rc = test_ava_filter( pb, e, e->e_attrs, &f->f_ava, LDAP_FILTER_LE,
					verify_access , only_check_access, access_check_done);
		break;

	case LDAP_FILTER_PRESENT:
		LDAPDebug( LDAP_DEBUG_FILTER, "    PRESENT\n", 0, 0, 0 );
		rc = test_presence_filter( pb, e, f->f_type, verify_access , only_check_access, access_check_done);
		break;

	case LDAP_FILTER_APPROX:
		LDAPDebug( LDAP_DEBUG_FILTER, "    APPROX\n", 0, 0, 0 );
		rc = test_ava_filter( pb, e, e->e_attrs, &f->f_ava, LDAP_FILTER_APPROX,
					verify_access , only_check_access, access_check_done);
		break;

	case LDAP_FILTER_EXTENDED:
		LDAPDebug( LDAP_DEBUG_FILTER, "    EXTENDED\n", 0, 0, 0 );
		rc = test_extensible_filter( pb, e, &f->f_mr, verify_access , only_check_access, access_check_done);
		break;

	case LDAP_FILTER_AND:
		LDAPDebug( LDAP_DEBUG_FILTER, "    AND\n", 0, 0, 0 );
		rc = test_filter_list( pb, e, f->f_and, 
					LDAP_FILTER_AND , verify_access, only_check_access, access_check_done);
		break;

	case LDAP_FILTER_OR:
		LDAPDebug( LDAP_DEBUG_FILTER, "    OR\n", 0, 0, 0 );
		rc = test_filter_list( pb, e, f->f_or, 
					LDAP_FILTER_OR , verify_access, only_check_access, access_check_done);
		break;

	case LDAP_FILTER_NOT:
		LDAPDebug( LDAP_DEBUG_FILTER, "    NOT\n", 0, 0, 0 );
		rc = slapi_filter_test_ext_internal( pb, e, f->f_not , verify_access, only_check_access, access_check_done);
		if(!(verify_access && only_check_access))  /* dont play with access control return codes */
		{
			if(verify_access && !rc && !(*access_check_done))
			{
				/* the filter failed so access control was not checked
				 * for NOT filters this is significant so we must ensure
				 * access control is checked
				 */
				/* check access control only */
				rc = slapi_filter_test_ext_internal( pb, e, f->f_not , verify_access, -1 /*only_check_access*/, access_check_done);
				/* preserve error code if any */
				if(!rc)
					rc = !rc;
			}
			else
				rc = !rc;
		}

		break;

	default:
		LDAPDebug( LDAP_DEBUG_ANY, "    unknown filter type 0x%lX\n",
		    f->f_choice, 0, 0 );
		rc = -1;
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= slapi_filter_test %d\n", rc, 0, 0 );
	return( rc );
}


int
slapi_filter_test_ext(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*f,
    int			verify_access,
	int			only_check_access
)
{
	int rc = 0; /* a no op request succeeds */
	int access_check_done = 0;

	switch ( f->f_choice ) {
	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		/* 
		* optimize acl checking by only doing it once it is
		* known that the whole filter passes and so the entry
		* is eligible to be returned.
		* then we check the filter only for access
		*
		* complex filters really benefit from
		* separate stages, filter eval, followed by acl check...
		*/
		if(!only_check_access)
		{
			rc = slapi_filter_test_ext_internal(pb,e,f,0,0, &access_check_done);
		}

		if(rc == 0 && verify_access)
		{
			rc = slapi_filter_test_ext_internal(pb,e,f,-1,-1, &access_check_done);
		}

		break;

	default:
		/*
		* ...but simple filters are better off doing eval and
		* acl check at once
		*/
		rc = slapi_filter_test_ext_internal(pb,e,f,verify_access,only_check_access, &access_check_done);
		break;
	}

	return rc; 
}


int test_ava_filter(
    Slapi_PBlock	*pb,
    Slapi_Entry		*e,
    Slapi_Attr	*a,
    struct ava		*ava,
    int			ftype,
    int			verify_access,
	int			only_check_access,
	int			*access_check_done
)
{
	int			rc;
	
	LDAPDebug( LDAP_DEBUG_FILTER, "=> test_ava_filter\n", 0, 0, 0 );

	*access_check_done = 0;

	if(optimise_filter_acl_tests())
	{
		rc = 0;

		if(!only_check_access)
		{
			rc = -1;
			for ( ; a != NULL; a = a->a_next ) {
				if ( slapi_attr_type_cmp( ava->ava_type, a->a_type, SLAPI_TYPE_CMP_SUBTYPE ) == 0 ) {
					rc = plugin_call_syntax_filter_ava( a, ftype, ava );
					if ( rc == 0 ) {
						break;
					}
				}
			}
		}

		if ( rc == 0 && verify_access && pb != NULL ) {
			char		*attrs[2] = { NULL, NULL };
			attrs[0] = ava->ava_type;
			rc = plugin_call_acl_plugin( pb, e, attrs, &ava->ava_value, 
								SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL );
			*access_check_done = -1;
		}

	}
	else
	{
		if ( verify_access && pb != NULL ) {
			char		*attrs[2] = { NULL, NULL };
			attrs[0] = ava->ava_type;
			rc = plugin_call_acl_plugin( pb, e, attrs, &ava->ava_value, 
								SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL );
			*access_check_done = -1;
			if ( only_check_access || rc != LDAP_SUCCESS ) {
				LDAPDebug( LDAP_DEBUG_FILTER, "<= test_ava_filter %d\n",
					rc, 0, 0 );
				return( rc );
			}
		}

		rc = -1;
		for ( ; a != NULL; a = a->a_next ) {
			if ( slapi_attr_type_cmp( ava->ava_type, a->a_type, SLAPI_TYPE_CMP_SUBTYPE ) == 0 ) {
				rc = plugin_call_syntax_filter_ava( a, ftype, ava );
				if ( rc == 0 ) {
					break;
				}
			}
		}
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= test_ava_filter %d\n", rc, 0, 0 );
	return( rc );
}

int
test_presence_filter(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    char		*type,
    int			verify_access,
	int			only_check_access,
	int			*access_check_done
)
{
	int rc;
	void *hint = NULL;

	*access_check_done = 0;

	if(optimise_filter_acl_tests())
	{
		rc = 0;
		/* Use attrlist_find_ex to get subtype matching */
		if(!only_check_access)
		{
			rc = attrlist_find_ex( e->e_attrs, type, 
									  NULL, NULL, &hint ) != NULL ? 0 : -1;
		}

		if (rc == 0 && verify_access && pb != NULL) {
			char		*attrs[2] = { NULL, NULL };
			attrs[0] = type;
			rc = plugin_call_acl_plugin( pb, e, attrs, NULL, SLAPI_ACL_SEARCH, 
							ACLPLUGIN_ACCESS_DEFAULT, NULL );
			*access_check_done = -1;
		}
	}
	else
	{
		if (verify_access && pb != NULL) {
			char		*attrs[2] = { NULL, NULL };
			attrs[0] = type;
			rc = plugin_call_acl_plugin( pb, e, attrs, NULL, SLAPI_ACL_SEARCH, 
							ACLPLUGIN_ACCESS_DEFAULT, NULL );
			*access_check_done = -1;
			if ( only_check_access || rc != LDAP_SUCCESS ) {
				return( rc );
			}
		}

		/* Use attrlist_find_ex to get subtype matching */
		rc = attrlist_find_ex( e->e_attrs, type, 
									  NULL, NULL, &hint ) != NULL ? 0 : -1;
	}

	return rc;
}

/*
 * Convert a DN into a list of attribute values.
 * The caller must free the returned attributes.
 */
static Slapi_Attr*
dn2attrs(const char *dn)
{
    int rc= 0;
    Slapi_Attr* dnAttrs = NULL;
    char** rdns = ldap_explode_dn (dn, 0);
    if (rdns)
    {
        char** rdn = rdns;
        for (; !rc && *rdn; ++rdn)
        {
            char** avas = ldap_explode_rdn (*rdn, 0);
            if (avas)
            {
                char** ava = avas;
                for (; !rc && *ava; ++ava)
                {
                    char* val = strchr (*ava, '=');
                    if (val)
                    {
                        struct berval bv;
                        struct berval* bvec[] = {NULL, NULL};
                        size_t type_len = val - *ava;
                        char* type = slapi_ch_malloc (type_len + 1);
                        memcpy (type, *ava, type_len);
                        type[type_len] = '\0';
                        ++val; /* skip the '=' */
                        bv.bv_val = val;
                        bv.bv_len = strlen(val);
                        bvec[0] = &bv;
                        attrlist_merge (&dnAttrs, type, bvec);
                    }
                }
                ldap_value_free (avas);
            }
        }
        ldap_value_free (rdns);
    }
    return dnAttrs;
}

static int
test_extensible_filter(
    Slapi_PBlock *callers_pb,
    Slapi_Entry *e,
    mr_filter_t *mrf,
    int verify_access,
	int	only_check_access,
	int *access_check_done
)
{
    /*
     * The ABNF for extensible filters is
     *
     * attr [":dn"] [":" matchingrule] ":=" value
     * [":dn"] ":" matchingrule ":=" value
     *
     * So, sigh, there are six possible combinations:
     *
     * A) attr ":=" value
     * B) attr ":dn" ":=" value
     * C) attr ":" matchingrule ":=" value
     * D) attr ":dn" ":" matchingrule ":=" value
     * E) ":" matchingrule ":=" value
     * F) ":dn" ":" matchingrule ":=" value
     */
    int	rc;
		
	LDAPDebug( LDAP_DEBUG_FILTER, "=> test_extensible_filter\n", 0, 0, 0 );

	*access_check_done = 0;

	if(optimise_filter_acl_tests())
	{
		rc = LDAP_SUCCESS;

		if(!only_check_access)
		{
			if (mrf->mrf_match==NULL)
			{
				/*
				 * Could be A or B
				 * No matching function. So use a regular equality filter.
				 * Check the regular attributes for the attribute value.
				 */
				struct ava a;
				a.ava_type= mrf->mrf_type;
        		a.ava_value.bv_len= mrf->mrf_value.bv_len;
				a.ava_value.bv_val = mrf->mrf_value.bv_val;
				rc= test_ava_filter( callers_pb, e, e->e_attrs, &a, LDAP_FILTER_EQUALITY, 0 /* Don't Verify Access */ , 0 /* don't just verify access */, access_check_done );
				if(rc!=LDAP_SUCCESS && mrf->mrf_dnAttrs)
				{
					/* B) Also check the DN attributes for the attribute value */
					Slapi_Attr* dnattrs= dn2attrs(slapi_entry_get_dn_const(e));
					rc= test_ava_filter( callers_pb, e, dnattrs, &a, LDAP_FILTER_EQUALITY, 0 /* Don't Verify Access */ , 0 /* don't just verify access */, access_check_done );
        			slapi_attr_free( &dnattrs );
				}
			}
			else
			{
				/*
				 * Could be C, D, E, or F
				 * We have a matching rule. 
				 */
				rc = mrf->mrf_match (mrf->mrf_object, e, e->e_attrs);
				if(rc!=LDAP_SUCCESS && mrf->mrf_dnAttrs)
				{
					/* D & F) Also check the DN attributes for the attribute value */
					Slapi_Attr* dnattrs= dn2attrs(slapi_entry_get_dn_const(e));
					mrf->mrf_match (mrf->mrf_object, e, dnattrs);
        			slapi_attr_free( &dnattrs );
				}
			}
		}

		if(rc == 0 && mrf->mrf_type!=NULL && verify_access)
		{
			char		*attrs[2] = { NULL, NULL };
			/* Could be A, B, C, or D */
			/* Check we have access to this attribute on this entry */
			attrs[0] = mrf->mrf_type;
			rc= plugin_call_acl_plugin (callers_pb, e, attrs, &(mrf->mrf_value), 
						SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL );
			*access_check_done = -1;
		}
	}
	else
	{
		rc = LDAP_SUCCESS;

		if(mrf->mrf_type!=NULL && verify_access)
		{
			char		*attrs[2] = { NULL, NULL };
			/* Could be A, B, C, or D */
			/* Check we have access to this attribute on this entry */
			attrs[0] = mrf->mrf_type;
			rc= plugin_call_acl_plugin (callers_pb, e, attrs, &(mrf->mrf_value), 
						SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL );
			*access_check_done = -1;

			if ( only_check_access ) {
				return rc;
			}
		}
		if(rc==LDAP_SUCCESS)
		{
			if (mrf->mrf_match==NULL)
			{
				/*
				 * Could be A or B
				 * No matching function. So use a regular equality filter.
				 * Check the regular attributes for the attribute value.
				 */
				struct ava a;
				a.ava_type= mrf->mrf_type;
        		a.ava_value.bv_len= mrf->mrf_value.bv_len;
				a.ava_value.bv_val = mrf->mrf_value.bv_val;
				rc= test_ava_filter( callers_pb, e, e->e_attrs, &a, LDAP_FILTER_EQUALITY, 0 /* Don't Verify Access */ , 0 /* don't just verify access */, access_check_done );
				if(rc!=LDAP_SUCCESS && mrf->mrf_dnAttrs)
				{
					/* B) Also check the DN attributes for the attribute value */
					Slapi_Attr* dnattrs= dn2attrs(slapi_entry_get_dn_const(e));
					rc= test_ava_filter( callers_pb, e, dnattrs, &a, LDAP_FILTER_EQUALITY, 0 /* Don't Verify Access */ , 0 /* don't just verify access */, access_check_done );
        			slapi_attr_free( &dnattrs );
				}
			}
			else
			{
				/*
				 * Could be C, D, E, or F
				 * We have a matching rule. 
				 */
				rc = mrf->mrf_match (mrf->mrf_object, e, e->e_attrs);
				if(rc!=LDAP_SUCCESS && mrf->mrf_dnAttrs)
				{
					/* D & F) Also check the DN attributes for the attribute value */
					Slapi_Attr* dnattrs= dn2attrs(slapi_entry_get_dn_const(e));
					mrf->mrf_match (mrf->mrf_object, e, dnattrs);
        			slapi_attr_free( &dnattrs );
				}
			}
		}
	}

    LDAPDebug( LDAP_DEBUG_FILTER, "<= test_extensible_filter %d\n", rc, 0, 0 );
    return( rc );
}


static int
test_filter_list(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*flist,
    int			ftype,
    int			verify_access,
	int			only_check_access,
	int			*access_check_done
)
{
	int		nomatch;
	struct slapi_filter	*f;
	int access_check_tmp = -1;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> test_filter_list\n", 0, 0, 0 );

	*access_check_done = -1;

	nomatch = 1;
	for ( f = flist; f != NULL; f = f->f_next ) {
		if ( slapi_filter_test_ext_internal( pb, e, f, verify_access, only_check_access, &access_check_tmp ) != 0 ) {
			/* optimize AND evaluation */
			if ( ftype == LDAP_FILTER_AND ) {
				/* one false is failure */
				nomatch = 1;
				break;
			}
		} else {
			nomatch = 0;

			/* optimize OR evaluation too */
			if ( ftype == LDAP_FILTER_OR ) {
				/* only one needs to be true */
				break;
			}
		}

		if(!access_check_tmp)
			*access_check_done = 0;
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= test_filter_list %d\n", nomatch, 0, 0 );
	return( nomatch );
}

void
filter_strcpy_special( char *d, char *s )
{
	for ( ; *s; s++ ) {
		switch ( *s ) {
		case '.':
		case '\\':
		case '[':
		case ']':
		case '*':
		case '+':
		case '^':
		case '$':
			*d++ = '\\';
			/* FALL */
		default:
			*d++ = *s;
		}
	}
	*d = '\0';
}

int test_substring_filter(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*f,
    int			verify_access,
	int			only_check_access,
	int			*access_check_done
)
{
	Slapi_Attr	*a;
	int		rc;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> test_substring_filter\n", 0, 0, 0 );

	*access_check_done = 0;

	if(optimise_filter_acl_tests())
	{
		rc = 0;

		if(!only_check_access)
		{
			rc = -1;
			for ( a = e->e_attrs; a != NULL; a = a->a_next ) {
				if ( slapi_attr_type_cmp( f->f_sub_type, a->a_type, SLAPI_TYPE_CMP_SUBTYPE ) == 0 ) {
					rc = plugin_call_syntax_filter_sub( a, &f->f_sub );
					if ( rc == 0 ) {
						break;
					}
				}
			}
		}

		if ( rc == 0 && verify_access && pb != NULL) {
			char		*attrs[2] = { NULL, NULL };
			attrs[0] = f->f_sub_type;
			rc = plugin_call_acl_plugin( pb, e, attrs, NULL,
				SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL );
			*access_check_done = -1;
		}
	}
	else
	{
		if ( verify_access && pb != NULL) {
			char		*attrs[2] = { NULL, NULL };
			attrs[0] = f->f_sub_type;
			rc = plugin_call_acl_plugin( pb, e, attrs, NULL,
				SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL );
			*access_check_done = -1;
			if ( only_check_access || rc != LDAP_SUCCESS ) {
				return( rc );
			}
		}


		rc = -1;
		for ( a = e->e_attrs; a != NULL; a = a->a_next ) {
			if ( slapi_attr_type_cmp( f->f_sub_type, a->a_type, SLAPI_TYPE_CMP_SUBTYPE ) == 0 ) {
				rc = plugin_call_syntax_filter_sub( a, &f->f_sub );
				if ( rc == 0 ) {
					break;
				}
			}
		}
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= test_substring_filter %d\n",
	    rc, 0, 0 );
	return( rc );
}

/*
 * Here's a duplicate vattr filter test code modified to support vattrs.
*/

/*
 * slapi_vattr_filter_test - test a filter against a single entry.
 *
 * Supports the case where the filter mentions virtual attributes.
 * Performance for a real attr only filter is same as for slapi_filter_test()
 * No explicit support for vattrs in extended filters because:
 * 	1. the matching rules must support virtual attributes themselves.
 *  2. if no matching rule is specified it defaults to equality so
 *  could just use a normal filter with equality.
 *  3. virtual naming attributes are probably too complex to support.
 * 
 * returns	0	filter matched
 *			-1	filter did not match
 *			>0	an ldap error code
 */

int
slapi_vattr_filter_test(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*f,
    int			verify_access
)
{
	return slapi_vattr_filter_test_ext(pb,e,f,verify_access,0);
}

/*
 * vattr_filter_test_ext - full-feature filter test function
 *
 * returns	0	filter matched
 *			-1	filter did not match
 *			>0	an ldap error code
 */
int
slapi_vattr_filter_test_ext(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*f,
    int			verify_access,
	int			only_check_access
)
{
	int rc = 0; /* a no op request succeeds */
	int access_check_done = 0;

	switch ( f->f_choice ) {
	case LDAP_FILTER_AND:
	case LDAP_FILTER_OR:
	case LDAP_FILTER_NOT:
		/* 
		* optimize acl checking by only doing it once it is
		* known that the whole filter passes and so the entry
		* is eligible to be returned.
		* then we check the filter only for access
		*
		* complex filters really benefit from
		* separate stages, filter eval, followed by acl check...
		*/
		if(!only_check_access)
		{
			rc = slapi_vattr_filter_test_ext_internal(pb,e,f,0,0, &access_check_done);
		}

		if(rc == 0 && verify_access)
		{
			rc = slapi_vattr_filter_test_ext_internal(pb,e,f,-1,-1, &access_check_done);
		}

		break;

	default:
		/*
		* ...but simple filters are better off doing eval and
		* acl check at once
		*/
		rc = slapi_vattr_filter_test_ext_internal(pb,e,f,verify_access,only_check_access, &access_check_done);
		break;
	}

	return rc; 
}

static int
slapi_vattr_filter_test_ext_internal(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*f,
    int			verify_access,
	int			only_check_access,
	int			*access_check_done
)
{
	int	rc = LDAP_SUCCESS;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> slapi_vattr_filter_test_ext\n", 0, 0, 0 );

	/*
	 * RJP: Not sure if this is semantically right, but we have to
	 * return something if f is NULL. If there is no filter,
	 * then we say that it did match and return 0.
	 */
	if ( f == NULL) {
	  return(0);
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "=> test_substring_filter\n", 0, 0, 0 );

	switch ( f->f_choice ) {
	case LDAP_FILTER_EQUALITY:
		LDAPDebug( LDAP_DEBUG_FILTER, "    EQUALITY\n", 0, 0, 0 );
		if ( verify_access ) {
			rc = test_filter_access( pb, e, f->f_ava.ava_type,
											&f->f_ava.ava_value);
			*access_check_done = 1;
		}
		if ( only_check_access || rc != LDAP_SUCCESS ) {
			return( rc );
		}
		rc = vattr_test_filter( e, f, FILTER_TYPE_AVA, f->f_ava.ava_type );
		break;

	case LDAP_FILTER_SUBSTRINGS:
		LDAPDebug( LDAP_DEBUG_FILTER, "    SUBSTRINGS\n", 0, 0, 0 );
		if ( verify_access ) {
			rc = test_filter_access( pb, e, f->f_sub_type, NULL);
			*access_check_done = 1;
		}
		if ( only_check_access || rc != LDAP_SUCCESS ) {
			return( rc );
		}
		rc =  vattr_test_filter( e, f, FILTER_TYPE_SUBSTRING, f->f_sub_type);
		break;

	case LDAP_FILTER_GE:
		LDAPDebug( LDAP_DEBUG_FILTER, "    GE\n", 0, 0, 0 );
		if ( verify_access ) {
			rc = test_filter_access( pb, e, f->f_ava.ava_type,
										&f->f_ava.ava_value);
			*access_check_done = 1;
		}
		if ( only_check_access || rc != LDAP_SUCCESS ) {
			return( rc );
		}
		rc = vattr_test_filter( e, f, FILTER_TYPE_AVA, f->f_ava.ava_type);
		break;

	case LDAP_FILTER_LE:
		LDAPDebug( LDAP_DEBUG_FILTER, "    LE\n", 0, 0, 0 );
		if ( verify_access ) {
			rc = test_filter_access( pb, e, f->f_ava.ava_type,
										&f->f_ava.ava_value);
			*access_check_done = 1;
		}
		if ( only_check_access || rc != LDAP_SUCCESS ) {
			return( rc );
		}
		rc = vattr_test_filter( e, f, FILTER_TYPE_AVA, f->f_ava.ava_type);
		break;

	case LDAP_FILTER_PRESENT:
		LDAPDebug( LDAP_DEBUG_FILTER, "    PRESENT\n", 0, 0, 0 );
		if ( verify_access ) {
			rc = test_filter_access( pb, e, f->f_type, NULL);
			*access_check_done = 1;
		}
		if ( only_check_access || rc != LDAP_SUCCESS ) {
			return( rc );
		}
		rc = vattr_test_filter( e, f, FILTER_TYPE_PRES, f->f_type);		
		break;

	case LDAP_FILTER_APPROX:
		LDAPDebug( LDAP_DEBUG_FILTER, "    APPROX\n", 0, 0, 0 );
		if ( verify_access ) {
			rc = test_filter_access( pb, e, f->f_ava.ava_type,
										&f->f_ava.ava_value);
			*access_check_done = 1;
		}
		if ( only_check_access || rc != LDAP_SUCCESS ) {
			return( rc );
		}
		rc = vattr_test_filter( e, f, FILTER_TYPE_AVA, f->f_ava.ava_type);
		break;

	case LDAP_FILTER_EXTENDED:
		LDAPDebug( LDAP_DEBUG_FILTER, "    EXTENDED\n", 0, 0, 0 );		
		rc = test_extensible_filter( pb, e, &f->f_mr, verify_access ,
											only_check_access, access_check_done);
		break;

	case LDAP_FILTER_AND:
		LDAPDebug( LDAP_DEBUG_FILTER, "    AND\n", 0, 0, 0 );
		rc = vattr_test_filter_list( pb, e, f->f_and, 
					LDAP_FILTER_AND , verify_access, only_check_access, access_check_done);
		break;

	case LDAP_FILTER_OR:
		LDAPDebug( LDAP_DEBUG_FILTER, "    OR\n", 0, 0, 0 );
		rc = vattr_test_filter_list( pb, e, f->f_or, 
					LDAP_FILTER_OR , verify_access, only_check_access, access_check_done);
		break;

	case LDAP_FILTER_NOT:
		LDAPDebug( LDAP_DEBUG_FILTER, "    NOT\n", 0, 0, 0 );
		rc = slapi_vattr_filter_test_ext_internal( pb, e, f->f_not , verify_access, only_check_access, access_check_done);
		if(!(verify_access && only_check_access))  /* dont play with access control return codes */
		{
			if(verify_access && !rc && !(*access_check_done))
			{
				/* the filter failed so access control was not checked
				 * for NOT filters this is significant so we must ensure
				 * access control is checked
				 */
				/* check access control only */
				rc = slapi_vattr_filter_test_ext_internal( pb, e, f->f_not , verify_access, -1 /*only_check_access*/, access_check_done);
				/* preserve error code if any */
				if(!rc)
					rc = !rc;
			}
			else
				rc = !rc;
		}
		break;

	default:
		LDAPDebug( LDAP_DEBUG_ANY, "    unknown filter type 0x%lX\n",
		    f->f_choice, 0, 0 );
		rc = -1;
	}


	LDAPDebug( LDAP_DEBUG_FILTER, "<= slapi_vattr_filter_test %d\n", rc, 0, 0 );
	return( rc );
}

static int test_filter_access( Slapi_PBlock		*pb,
					Slapi_Entry		*e,
					char *attr_type, 
					struct berval *attr_val) {
	/*
	 * attr_type--attr_type to test for.
	 * attr_val--attr value to test for
	*/
	int rc;
	char		*attrs[2] = { NULL, NULL };
	attrs[0] = attr_type;
	
	rc = plugin_call_acl_plugin( pb, e, attrs, attr_val,
		    SLAPI_ACL_SEARCH, ACLPLUGIN_ACCESS_DEFAULT, NULL );

	return(rc);
}

static int
vattr_test_filter_list(
    Slapi_PBlock		*pb,
    Slapi_Entry		*e,
    struct slapi_filter	*flist,
    int			ftype,
    int			verify_access,
	int			only_check_access,
	int			*access_check_done
)
{
	int		nomatch;
	struct slapi_filter	*f;

	LDAPDebug( LDAP_DEBUG_FILTER, "=> vattr_test_filter_list\n", 0, 0, 0 );

	nomatch = 1;
	for ( f = flist; f != NULL; f = f->f_next ) {
		if ( slapi_vattr_filter_test_ext_internal( pb, e, f, verify_access, only_check_access, access_check_done ) != 0 ) {
			/* optimize AND evaluation */
			if ( ftype == LDAP_FILTER_AND ) {
				/* one false is failure */
				nomatch = 1;
				break;
			}
		} else {
			nomatch = 0;

			/* optimize OR evaluation too */
			if ( ftype == LDAP_FILTER_OR ) {
				/* only one needs to be true */
				break;
			}
		}
	}

	LDAPDebug( LDAP_DEBUG_FILTER, "<= test_filter_list %d\n", nomatch, 0, 0 );
	return( nomatch );
}
