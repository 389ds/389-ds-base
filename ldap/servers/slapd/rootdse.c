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

/* rootdse.c - routines to manage the root DSE */

#include <stdio.h>
#include "slap.h"
#include "fe.h"

/* XXXmcs: why not look at the NO-USER-MODIFICATION flag instead? */
static char *readonly_attributes[] = {
    "namingcontexts",
    "nsBackendSuffix",
    "subschemasubentry",
    "supportedldapversion",
    "supportedcontrol",
    "supportedextension",
    "supportedsaslmechanisms",
    "dataversion",
    "ref",
    "vendorName",
    "vendorVersion",
	ATTR_NETSCAPEMDSUFFIX,
    NULL
};


static char *writable_attributes[] = {
    "copiedfrom",
    "copyingfrom",
    "aci",
    NULL
};


/*
 * function: is_readonly_attr
 * args: attr - candidate attribute name
 * returns: 0 if this attribute may be written to the root DSE
 *          1 if it may not be written.
 * notes: should probably be integrated into syntax AVL tree, so that
 *        this list can be configured at runtime.
 */
static int
rootdse_is_readonly_attr( char *attr )
{
    int	i;

    if ( NULL == attr ) {
	return 1;	/* I guess.  It's not really an attribute at all */
    }

    if ( NULL == readonly_attributes ) {
	return 0;
    }

    /*
     * optimization: check for attributes we're likely to be writing
     * frequently.
     */
    for ( i = 0; NULL != writable_attributes[ i ]; i++ ) {
	if ( strncasecmp( attr, writable_attributes[ i ],
		strlen( writable_attributes[ i ])) == 0 ) {
	    return 0;
	}
    }

    for ( i = 0; NULL != readonly_attributes[ i ]; i++ ) {
	if ( strncasecmp( attr, readonly_attributes[ i ],
		strlen( readonly_attributes[ i ])) == 0 ) {
	    return 1;
	}
    }
    return 0;
}





/*
 * Handle a read operation on the root DSE (the entry with DN "");
 * Note: we're copying a lot of attributes here.  It might be better
 * to keep track of which we really need to free, and arrange that
 * the others are unlinked from the attribute list of the entry
 * before calling slapi_entry_free().
 */
int
read_root_dse( Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg )
{
	int			i;
	struct berval		*vals[2];
	struct berval val;
	struct berval **bvals;
	Slapi_Backend	*be;
	char			*cookie = NULL;
	char			**strs;
	void			*node;
	Slapi_DN		*sdn;

    /*
     * Check that we were doing a base search on the root dse.
     */
    {
        int scope;
    	slapi_pblock_get( pb, SLAPI_SEARCH_SCOPE, &scope );
        if(scope!=LDAP_SCOPE_BASE)
        {
            *returncode= LDAP_NO_SUCH_OBJECT;
            return SLAPI_DSE_CALLBACK_ERROR;
        }
    }

	vals[0] = &val;
	vals[1] = NULL;

    /* loop through backend suffixes to get namingcontexts attr */
    attrlist_delete( &e->e_attrs, "namingcontexts");

    sdn = slapi_get_first_suffix(&node, 0);
	while (sdn)
	{
		val.bv_val = (char*)slapi_sdn_get_dn(sdn); /* jcm: had to cast away const */
		val.bv_len = strlen( val.bv_val );
		attrlist_merge( &e->e_attrs, "namingcontexts", vals );
		sdn = slapi_get_next_suffix(&node, 0);
	}

	attrlist_delete( &e->e_attrs, "nsBackendSuffix");
        for (be = slapi_get_first_backend(&cookie); be != NULL; 
             be = slapi_get_next_backend(cookie)) {

            char * base;                                               
            char * be_name;
            const Slapi_DN *be_suffix;

            if (slapi_be_private(be)) continue;

            /* tolerate a backend under construction containing no suffix */
            if ((be_suffix = slapi_be_getsuffix(be, 0)) == NULL) continue;

            if ((base = (char *)slapi_sdn_get_dn(be_suffix)) == NULL) continue;
            if ((be_name = slapi_be_get_name(be)) == NULL) continue;

            val.bv_len = strlen(base)+strlen(be_name)+1;
            val.bv_val = slapi_ch_malloc(val.bv_len+1);
            sprintf(val.bv_val, "%s:%s", be_name, base); 
            attrlist_merge(&e->e_attrs, "nsBackendSuffix", vals);
            slapi_ch_free((void **) &val.bv_val);
	}
	slapi_ch_free((void **)&cookie);       

	/* schema entry */
	val.bv_val = SLAPD_SCHEMA_DN;
	val.bv_len = sizeof( SLAPD_SCHEMA_DN ) - 1;
	attrlist_replace( &e->e_attrs, "subschemasubentry", vals );
	
	/* supported extended operations */
	attrlist_delete( &e->e_attrs, "supportedExtension");
	if (( strs = slapi_get_supported_extended_ops_copy()) != NULL ) {
	    for ( i = 0; strs[i] != NULL; ++i ) {
		val.bv_val = strs[i];
		val.bv_len = strlen( strs[i] );
		attrlist_merge( &e->e_attrs, "supportedExtension", vals );
	    }
	    charray_free(strs);
	}
	
	/* supported controls */
	attrlist_delete( &e->e_attrs, "supportedControl");
	if ( slapi_get_supported_controls_copy( &strs, NULL ) == 0
		&& strs != NULL ) {
	    for ( i = 0; strs[i] != NULL; ++i ) {
		val.bv_val = strs[i];
		val.bv_len = strlen( strs[i] );
		attrlist_merge( &e->e_attrs, "supportedControl", vals );
	    }
	    charray_free(strs);
	}

	/* supported sasl mechanisms */
	attrlist_delete( &e->e_attrs, "supportedSASLMechanisms");
	if (( strs = ids_sasl_listmech (pb)) != NULL ) {
	    for ( i = 0; strs[i] != NULL; ++i ) {
		val.bv_val = strs[i];
		val.bv_len = strlen( strs[i] );
		attrlist_merge( &e->e_attrs, "supportedSASLMechanisms", vals );
	    }
            charray_free(strs);
	}


	/* supported LDAP versions */
	val.bv_val = "2";
	val.bv_len = 1;
	attrlist_replace( &e->e_attrs, "supportedldapversion", vals );
	val.bv_val = "3";
	val.bv_len = 1;
	attrlist_merge( &e->e_attrs, "supportedldapversion", vals );

	/* superior references (ref attribute) */
	attrlist_delete( &e->e_attrs, "ref");
	if (( bvals = g_get_default_referral()) != NULL ) {
	    for ( i = 0; bvals[i] != NULL; ++i ) {
		val.bv_val = bvals[i]->bv_val;
		val.bv_len = bvals[i]->bv_len;
		attrlist_merge( &e->e_attrs, "ref", vals );
	    }
	}

	/* RFC 3045 attributes: vendorName and vendorVersion */
	val.bv_val = SLAPD_VENDOR_NAME;
	val.bv_len = strlen( val.bv_val );
	attrlist_replace( &e->e_attrs, "vendorName", vals );
	val.bv_val = slapd_get_version_value();
	val.bv_len = strlen( val.bv_val );
	attrlist_replace( &e->e_attrs, "vendorVersion", vals );
	slapi_ch_free( (void **)&val.bv_val );

	/* Server Data Version */
	if (( val.bv_val = (char*)get_server_dataversion()) != NULL ) { /* jcm cast away const */
	    val.bv_len = strlen( val.bv_val );
	    attrlist_replace( &e->e_attrs, attr_dataversion, vals );
	}

	/* machine data suffix
	 * this has been added in 4.0 for replication purpose
	 * and since 5.0 is now unused by the core server,
	 * however some functionalities of the console framework 5.01
	 * still depend on this
	 */
	if (( val.bv_val = get_config_DN()) != NULL ) {
		val.bv_len = strlen( val.bv_val );
		attrlist_replace( &e->e_attrs, ATTR_NETSCAPEMDSUFFIX, vals );
	}

#ifdef notdef
	/* XXXggood testing - print the size of the changelog db */
	{
	    unsigned int clsize;
	    clsize = get_changelog_size();
	    sprintf( buf, "%u", clsize );
	    val.bv_val = buf;
	    val.bv_len = strlen( buf );
	    attrlist_replace( &e->e_attrs, "changelogsize", vals );
	    slapi_ch_free((void**)&val.bv_val );
	}
#endif /* notdef */

    /* vlvsearch is list of dns to VLV Search Specifications */
	attrlist_delete( &e->e_attrs, "vlvsearch");
	cookie = NULL;
	be = slapi_get_first_backend(&cookie);
	while (be)
	{
        if(!be->be_private)
        {
            /* Generate searches to find the VLV Search Specifications */
            int r;
            Slapi_PBlock *resultpb= NULL;
            Slapi_Entry** entry = NULL;
			Slapi_DN dn;
			slapi_sdn_init(&dn);
		    be_getconfigdn(be,&dn);
            resultpb= slapi_search_internal( slapi_sdn_get_ndn(&dn), LDAP_SCOPE_ONELEVEL, "objectclass=vlvsearch", NULL, NULL, 1);
			slapi_sdn_done(&dn);
            slapi_pblock_get( resultpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entry );
            slapi_pblock_get( resultpb, SLAPI_PLUGIN_INTOP_RESULT, &r );
            if(r==LDAP_SUCCESS)
            {
            	for (; *entry; ++entry)
            	{
            		val.bv_val = slapi_entry_get_dn(*entry);
            		val.bv_len = strlen( val.bv_val );
            		attrlist_merge( &e->e_attrs, "vlvsearch", vals );
                }
            }
            slapi_free_search_results_internal(resultpb);
            slapi_pblock_destroy(resultpb);
        }

		be = slapi_get_next_backend (cookie);
	}
	slapi_ch_free ((void **)&cookie);
    *returncode= LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;
}



/*
 * Handle a modification request on the root DSE.  Only certain
 * attributes are writable.  If an attempt to modify an attribute
 * which is not allowed, return LDAP_UNWILLING_TO_PERFORM, unless
 * the modification is disallowed because of ACL checking, in which
 * case LDAP_INSUFFICIENT_ACCESS is returned.
 */
int
modify_root_dse( Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg )
{
    LDAPMod		**mods;

    /*
     * Make a pass through the attributes and check them
     * to make sure none are computed.  Also check for
     * reflected attributes and reject those as well.
     * Eventually, some reflected attributes might be
     * writable, but for now, none are.
     */

    slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &mods );
    if ( NULL != mods )
    {
        int i;
    	for ( i = 0; NULL != mods[ i ]; i++ )
    	{
    	    if ( rootdse_is_readonly_attr( mods[ i ]->mod_type ))
    	    {
        		/* The modification is disallowed */
        		*returncode = LDAP_UNWILLING_TO_PERFORM;
                strcpy(returntext,"Modification of these root DSE attributes not allowed");
        		return SLAPI_DSE_CALLBACK_ERROR;
    	    }
    	}
    }

    *returncode= LDAP_SUCCESS;
    return SLAPI_DSE_CALLBACK_OK;	/* success -- apply the changes */
} 
