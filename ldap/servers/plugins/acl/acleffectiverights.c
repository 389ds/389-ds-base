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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include "acl.h"

/* safer than doing strcat unprotected */
/* news2 is optional, provided as a convenience */
/* capacity is the capacity of the gerstr, size is the current length */
static void
_append_gerstr(
	char **gerstr,
	size_t *capacity,
	size_t *size,
	const char *news,
	const char *news2
	)
{
	size_t len;
	size_t increment = 128;
	size_t fornull;

	if (!news) {
		return;
	}

	/* find out how much space we need */
	len = strlen(news);
	fornull = 1;
	if (news2) {
		len += strlen(news2);
		fornull++;
	}

	/* increase space if needed */
	while ((*size + len + fornull) > *capacity) {
		if ((len + fornull) > increment) {
			*capacity += len + fornull; /* just go ahead and grow the string enough */
		} else {
			*capacity += increment; /* rather than having lots of small increments */
		}
	}

	if (!*gerstr) {
		*gerstr = slapi_ch_malloc(*capacity);
		**gerstr = 0;
	} else {
		*gerstr = slapi_ch_realloc(*gerstr, *capacity);
	}
	strcat(*gerstr, news);
	if (news2) {
		strcat(*gerstr, news2);
	}

	*size += len;

	return;
}

static int
_ger_g_permission_granted (
	Slapi_PBlock *pb,
	Slapi_Entry *e,
	const char *subjectdn,
	char **errbuf
	)
{
	char *proxydn = NULL;
	Slapi_DN *requestor_sdn, *entry_sdn;
	char *errtext = NULL;
	int isroot;
	int rc;

	/*
	 * Theorically, we should check if the entry has "g"
	 * permission granted to the requestor. If granted,
	 * allows the effective rights on that entry and its
	 * attributes within the entry to be returned for
	 * ANY subject.
	 *
	 * "G" permission granting has not been implemented yet,
	 * the current release assumes that "g" permission be 
	 * granted to root and owner of any entry.
	 */

	/*
	 * The requestor may be either the bind dn or a proxy dn
	 */
	acl_get_proxyauth_dn ( pb, &proxydn, &errtext );
	if ( proxydn != NULL )
	{
		requestor_sdn = slapi_sdn_new_dn_passin ( proxydn );
	}
	else
	{
		requestor_sdn = &(pb->pb_op->o_sdn);
	}
	if ( slapi_sdn_get_dn (requestor_sdn) == NULL )
	{
		slapi_log_error (SLAPI_LOG_ACL, plugin_name,
				"_ger_g_permission_granted: anonymous has no g permission\n" );
		rc = LDAP_INSUFFICIENT_ACCESS;
		goto bailout;
	}
	isroot = slapi_dn_isroot ( slapi_sdn_get_dn (requestor_sdn) );
	if ( isroot )
	{
		/* Root has "g" permission on any entry */
		rc = LDAP_SUCCESS;
		goto bailout;
	}

	entry_sdn = slapi_entry_get_sdn ( e );
	if ( entry_sdn == NULL || slapi_sdn_get_dn (entry_sdn) == NULL )
	{
		rc = LDAP_SUCCESS;
		goto bailout;
	}

	if ( slapi_sdn_compare ( requestor_sdn, entry_sdn ) == 0 )
	{
		/* Owner has "g" permission on his own entry */
		rc = LDAP_SUCCESS;
		goto bailout;
	}

	/* if the requestor and the subject user are identical, let's grant it */
	if ( strcasecmp ( slapi_sdn_get_ndn(requestor_sdn), subjectdn ) == 0)
	{
		/* Requestor should see his own permission rights on any entry */
		rc = LDAP_SUCCESS;
		goto bailout;
	}

	aclutil_str_appened ( errbuf, "get-effective-rights: requestor has no g permission on the entry" );
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
				"_ger_g_permission_granted: %s\n", *errbuf);
	rc = LDAP_INSUFFICIENT_ACCESS;

bailout:
	if ( proxydn )
	{
		/* The ownership of proxydn has passed to requestor_sdn */ 
		slapi_sdn_free ( &requestor_sdn );
	}
	return rc;
}

static int
_ger_parse_control (
	Slapi_PBlock *pb,
	char **subjectndn,
	int *iscritical,
	char **errbuf
	)
{
	LDAPControl **requestcontrols;
	struct berval *subjectber;
	BerElement *ber;
	int subjectndnlen = 0;

	if (NULL == subjectndn)
	{
		return LDAP_OPERATIONS_ERROR;
	}

	*subjectndn = NULL;

	/*
	 * Get the control
	 */
	slapi_pblock_get ( pb, SLAPI_REQCONTROLS, (void *) &requestcontrols );
	slapi_control_present ( requestcontrols,
							LDAP_CONTROL_GET_EFFECTIVE_RIGHTS,
							&subjectber,
							iscritical );
	if ( subjectber == NULL || subjectber->bv_val == NULL ||
		 subjectber->bv_len == 0 )
	{
		aclutil_str_appened ( errbuf, "get-effective-rights: missing subject" );
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name, "%s\n", *errbuf );
		return LDAP_INVALID_SYNTAX;
	}

	if ( strncasecmp ( "dn:", subjectber->bv_val, 3 ) == 0 )
	{
		/*
		 * This is a non-standard support to allow the subject being a plain
		 * or base64 encoding string. Hence users using -J option in
		 * ldapsearch don't have to do BER encoding for the subject.
		 */
		*subjectndn = slapi_ch_malloc ( subjectber->bv_len + 1 );
		strncpy ( *subjectndn, subjectber->bv_val, subjectber->bv_len );
		*(*subjectndn + subjectber->bv_len) = '\0';
	}
	else
	{
		ber = ber_init (subjectber);
		if ( ber == NULL )
		{
			aclutil_str_appened ( errbuf, "get-effective-rights: ber_init failed for the subject" );
			slapi_log_error (SLAPI_LOG_FATAL, plugin_name, "%s\n", *errbuf );
			return LDAP_OPERATIONS_ERROR;
		}
		/* "a" means to allocate storage as needed for octet string */
		if ( ber_scanf (ber, "a", subjectndn) == LBER_ERROR )
		{
			aclutil_str_appened ( errbuf, "get-effective-rights: invalid ber tag in the subject" );
			slapi_log_error (SLAPI_LOG_FATAL, plugin_name, "%s\n", *errbuf );
			ber_free ( ber, 1 );
			return LDAP_INVALID_SYNTAX;
		}
		ber_free ( ber, 1 );
	}

	/*
	 * The current implementation limits the subject to authorization ID
	 * (see section 9 of RFC 2829) only. It also only supports the "dnAuthzId"
	 * flavor, which looks like "dn:<DN>" where null <DN> is for anonymous.
	 */
	subjectndnlen = strlen(*subjectndn);
	if ( NULL == *subjectndn || subjectndnlen < 3 ||
		 strncasecmp ( "dn:", *subjectndn, 3 ) != 0 )
	{
		aclutil_str_appened ( errbuf, "get-effective-rights: subject is not dnAuthzId" );
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name, "%s\n", *errbuf );
		return LDAP_INVALID_SYNTAX;
	}

	/* memmove is safe for overlapping copy */
	memmove ( *subjectndn, *subjectndn + 3, subjectndnlen - 2);/* 1 for '\0' */
	slapi_dn_normalize ( *subjectndn );
	return LDAP_SUCCESS;
}

static void
_ger_release_gerpb (
	Slapi_PBlock **gerpb,
	void		 **aclcb,	/* original aclcb */
	Slapi_PBlock *pb		/* original pb */
	)
{
	if ( *gerpb )
	{
		/* Return conn to pb */
		slapi_pblock_set ( *gerpb, SLAPI_CONNECTION, NULL );
		slapi_pblock_destroy ( *gerpb );
		*gerpb = NULL;
	}

	/* Put the original aclcb back to pb */
	if ( *aclcb )
	{
		Connection *conn = NULL;
		slapi_pblock_get ( pb, SLAPI_CONNECTION, &conn );
		if (conn)
		{
			struct aclcb *geraclcb;
			geraclcb = (struct aclcb *) acl_get_ext ( ACL_EXT_CONNECTION, conn );
			acl_conn_ext_destructor ( geraclcb, NULL, NULL );
			acl_set_ext ( ACL_EXT_CONNECTION, conn, *aclcb );
			*aclcb = NULL;
		}
	}
}

static int
_ger_new_gerpb (
	Slapi_PBlock    *pb,
	Slapi_Entry	    *e,
	const char 		*subjectndn,
	Slapi_PBlock	**gerpb,
	void			**aclcb,	/* original aclcb */
	char			**errbuf
	)
{
	Connection *conn;
	struct acl_cblock *geraclcb;
	Acl_PBlock *geraclpb;
	Operation *gerop;
	int rc = LDAP_SUCCESS;

	*aclcb = NULL;
	*gerpb = slapi_pblock_new ();
	if ( *gerpb == NULL )
	{
		rc = LDAP_NO_MEMORY;
		goto bailout;
	}

	{
		/* aclpb initialization needs the backend */
		Slapi_Backend *be;
		slapi_pblock_get ( pb, SLAPI_BACKEND, &be );
		slapi_pblock_set ( *gerpb, SLAPI_BACKEND, be );
	}

	{
		int isroot = slapi_dn_isroot ( subjectndn );
		slapi_pblock_set ( *gerpb, SLAPI_REQUESTOR_ISROOT, &isroot );
	}

	/* Save requestor's aclcb and set subjectdn's one */
	{
		slapi_pblock_get ( pb, SLAPI_CONNECTION, &conn );
		slapi_pblock_set ( *gerpb, SLAPI_CONNECTION, conn );

		/* Can't share the conn->aclcb because of different context */
		geraclcb = (struct acl_cblock *) acl_conn_ext_constructor ( NULL, NULL);
		if ( geraclcb == NULL )
		{
			rc = LDAP_NO_MEMORY;
			goto bailout;
		}
		slapi_sdn_set_ndn_byval ( geraclcb->aclcb_sdn, subjectndn );
		*aclcb = acl_get_ext ( ACL_EXT_CONNECTION, conn );
		acl_set_ext ( ACL_EXT_CONNECTION, conn, (void *) geraclcb );
	}

	{
		gerop = operation_new ( OP_FLAG_INTERNAL );
		if ( gerop == NULL )
		{
			rc = LDAP_NO_MEMORY;
			goto bailout;
		}
		/*
		 * conn is a no-use parameter in the functions
		 * chained down from factory_create_extension
		 */
		gerop->o_extension = factory_create_extension ( get_operation_object_type(), (void *)gerop, (void *)conn );
		slapi_pblock_set ( *gerpb, SLAPI_OPERATION, gerop );
		slapi_sdn_set_dn_byval ( &gerop->o_sdn, subjectndn );
		geraclpb = acl_get_ext ( ACL_EXT_OPERATION, (void *)gerop);
		acl_init_aclpb ( *gerpb, geraclpb, subjectndn, 0 );
		geraclpb->aclpb_res_type |= ACLPB_EFFECTIVE_RIGHTS;
	}


bailout:
	if ( rc != LDAP_SUCCESS )
	{
		_ger_release_gerpb ( gerpb, aclcb, pb );
	}

	return rc;
}

/*
 * Callers should have already allocated *gerstr to hold at least
 * "entryLevelRights: adnvxxx\n".
 */
unsigned long
_ger_get_entry_rights (
	Slapi_PBlock *gerpb,
	Slapi_Entry *e,
	const char *subjectndn,
	char **gerstr,
	size_t *gerstrsize,
	size_t *gerstrcap,
	char **errbuf
	)
{
	unsigned long entryrights = 0;
	Slapi_RDN *rdn = NULL;
	char *rdntype = NULL;
	char *rdnvalue = NULL;

	_append_gerstr(gerstr, gerstrsize, gerstrcap, "entryLevelRights: ", NULL);

	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"_ger_get_entry_rights: SLAPI_ACL_READ\n" );
	if (acl_access_allowed(gerpb, e, "*", NULL, SLAPI_ACL_READ) == LDAP_SUCCESS)
	{
		/* v - view e */
		entryrights |= SLAPI_ACL_READ;
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "v", NULL);
	}
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"_ger_get_entry_rights: SLAPI_ACL_ADD\n" );
	if (acl_access_allowed(gerpb, e, NULL, NULL, SLAPI_ACL_ADD) == LDAP_SUCCESS)
	{
		/* a - add child entry below e */
		entryrights |= SLAPI_ACL_ADD;
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "a", NULL);
	}
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"_ger_get_entry_rights: SLAPI_ACL_DELETE\n" );
	if (acl_access_allowed(gerpb, e, NULL, NULL, SLAPI_ACL_DELETE) == LDAP_SUCCESS)
	{
		/* d - delete e */
		entryrights |= SLAPI_ACL_DELETE;
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "d", NULL);
	}
	/*
	 * Some limitation/simplification applied here:
	 * - The modrdn right requires the rights to delete the old rdn and
	 *   the new one. However we have no knowledge of what the new rdn
	 *   is going to be.
	 * - In multi-valued RDN case, we check the right on
	 *   the first rdn type only for now.
	 */
	rdn = slapi_rdn_new_dn ( slapi_entry_get_ndn (e) );
	slapi_rdn_get_first(rdn, &rdntype, &rdnvalue);
	if ( NULL != rdntype ) {
		slapi_log_error (SLAPI_LOG_ACL, plugin_name,
			"_ger_get_entry_rights: SLAPI_ACL_WRITE_DEL & _ADD %s\n", rdntype );
		if (acl_access_allowed(gerpb, e, rdntype, NULL,
				ACLPB_SLAPI_ACL_WRITE_DEL) == LDAP_SUCCESS &&
			acl_access_allowed(gerpb, e, rdntype, NULL,
				ACLPB_SLAPI_ACL_WRITE_ADD) == LDAP_SUCCESS)
		{
			/* n - rename e */
			entryrights |= SLAPI_ACL_WRITE;
			_append_gerstr(gerstr, gerstrsize, gerstrcap, "n", NULL);
		}
	}
	slapi_rdn_free ( &rdn );

	if ( entryrights == 0 )
	{
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "none", NULL);
	}

	_append_gerstr(gerstr, gerstrsize, gerstrcap, "\n", NULL);

	return entryrights;
}

/*
 * *gerstr should point to a heap buffer since it may need
 * to expand dynamically.
 */
unsigned long
_ger_get_attr_rights (
	Slapi_PBlock *gerpb,
	Slapi_Entry *e,
	const char *subjectndn,
	char *type,
	char **gerstr,
	size_t *gerstrsize,
	size_t *gerstrcap,
	int isfirstattr,
	char **errbuf
	)
{
	unsigned long attrrights = 0;

	if (!isfirstattr)
	{
		_append_gerstr(gerstr, gerstrsize, gerstrcap, ", ", NULL);
	}
	_append_gerstr(gerstr, gerstrsize, gerstrcap, type, ":");

	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"_ger_get_attr_rights: SLAPI_ACL_READ %s\n", type );
	if (acl_access_allowed(gerpb, e, type, NULL, SLAPI_ACL_READ) == LDAP_SUCCESS)
	{
		/* r - read the values of type */
		attrrights |= SLAPI_ACL_READ;
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "r", NULL);
	}
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"_ger_get_attr_rights: SLAPI_ACL_SEARCH %s\n", type );
	if (acl_access_allowed(gerpb, e, type, NULL, SLAPI_ACL_SEARCH) == LDAP_SUCCESS)
	{
		/* s - search the values of type */
		attrrights |= SLAPI_ACL_SEARCH;
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "s", NULL);
	}
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"_ger_get_attr_rights: SLAPI_ACL_COMPARE %s\n", type );
	if (acl_access_allowed(gerpb, e, type, NULL, SLAPI_ACL_COMPARE) == LDAP_SUCCESS)
	{
		/* c - compare the values of type */
		attrrights |= SLAPI_ACL_COMPARE;
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "c", NULL);
	}
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"_ger_get_attr_rights: SLAPI_ACL_WRITE_ADD %s\n", type );
	if (acl_access_allowed(gerpb, e, type, NULL, ACLPB_SLAPI_ACL_WRITE_ADD) == LDAP_SUCCESS)
	{
		/* w - add the values of type */
		attrrights |= ACLPB_SLAPI_ACL_WRITE_ADD;
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "w", NULL);
	}
	slapi_log_error (SLAPI_LOG_ACL, plugin_name,
		"_ger_get_attr_rights: SLAPI_ACL_WRITE_DEL %s\n", type );
	if (acl_access_allowed(gerpb, e, type, NULL, ACLPB_SLAPI_ACL_WRITE_DEL) == LDAP_SUCCESS)
	{
		/* o - delete the values of type */
		attrrights |= ACLPB_SLAPI_ACL_WRITE_DEL;
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "o", NULL);
	}
	/* If subjectdn has no general write right, check for self write */
	if ( 0 == (attrrights & (ACLPB_SLAPI_ACL_WRITE_DEL | ACLPB_SLAPI_ACL_WRITE_ADD)) )
	{
		struct berval val;

		val.bv_val = (char *)subjectndn;
		val.bv_len = strlen (subjectndn);

		if (acl_access_allowed(gerpb, e, type, &val, ACLPB_SLAPI_ACL_WRITE_ADD) == LDAP_SUCCESS)
		{
			/* W - add self to the attribute */
			attrrights |= ACLPB_SLAPI_ACL_WRITE_ADD;
			_append_gerstr(gerstr, gerstrsize, gerstrcap, "W", NULL);
		}
		if (acl_access_allowed(gerpb, e, type, &val, ACLPB_SLAPI_ACL_WRITE_DEL) == LDAP_SUCCESS)
		{
			/* O - delete self from the attribute */
			attrrights |= ACLPB_SLAPI_ACL_WRITE_DEL;
			_append_gerstr(gerstr, gerstrsize, gerstrcap, "O", NULL);
		}
	}

	if ( attrrights == 0 )
	{
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "none", NULL);
	}

	return attrrights;
}

#define GER_GET_ATTR_RIGHTS(attrs) \
	for (thisattr = (attrs); thisattr && *thisattr; thisattr++) \
	{ \
		_ger_get_attr_rights (gerpb, e, subjectndn, *thisattr, \
						gerstr, gerstrsize, gerstrcap, isfirstattr, errbuf); \
		isfirstattr = 0; \
	} \

#define GER_GET_ATTR_RIGHTA_EXT(c, inattrs, exattrs); \
	for ( i = 0; attrs[i]; i++ ) \
	{ \
		if ((c) != *attrs[i] && charray_inlist((inattrs), attrs[i]) && \
				!charray_inlist((exattrs), attrs[i])) \
		{ \
			_ger_get_attr_rights ( gerpb, e, subjectndn, attrs[i], \
				gerstr, gerstrsize, gerstrcap, isfirstattr, errbuf ); \
			isfirstattr = 0; \
		} \
	}

void
_ger_get_attrs_rights (
	Slapi_PBlock *gerpb,
	Slapi_Entry *e,
	const char *subjectndn,
	char **attrs,
	char **gerstr,
	size_t *gerstrsize,
	size_t *gerstrcap,
	char **errbuf
	)
{
	int isfirstattr = 1;

	/* gerstr was initially allocated with enough space for one more line */
	_append_gerstr(gerstr, gerstrsize, gerstrcap, "attributeLevelRights: ", NULL);

	if (attrs && *attrs)
	{
		int i = 0;
		char **allattrs = NULL;
		char **opattrs = NULL;
		char **myattrs = NULL;
		char **thisattr = NULL;
		int hasstar = charray_inlist(attrs, "*");
		int hasplus = charray_inlist(attrs, "+");
		Slapi_Attr *objclasses = NULL;
		Slapi_ValueSet *objclassvals = NULL;
		int isextensibleobj = 0;

		/* get all attrs available for the entry */
		slapi_entry_attr_find(e, "objectclass", &objclasses);
		if (NULL != objclasses) {
			Slapi_Value *v;
			slapi_attr_get_valueset(objclasses, &objclassvals);
			i = slapi_valueset_first_value(objclassvals, &v);
			if (-1 != i)
			{
				const char *ocname = NULL;
				allattrs = slapi_schema_list_objectclass_attributes(
							(const char *)v->bv.bv_val,
							SLAPI_OC_FLAG_REQUIRED|SLAPI_OC_FLAG_ALLOWED);
				/* check if this entry is an extensble object or not */
				ocname = slapi_value_get_string(v);
				if ( strcasecmp( ocname, "extensibleobject" ) == 0 )
				{
					isextensibleobj = 1;
				}
				/* add "aci" to the allattrs to adjust to do_search */
				charray_add(&allattrs, slapi_attr_syntax_normalize("aci"));
				while (-1 != i)
				{
					i = slapi_valueset_next_value(objclassvals, i, &v);
					if (-1 != i)
					{
						myattrs = slapi_schema_list_objectclass_attributes(
							(const char *)v->bv.bv_val,
							SLAPI_OC_FLAG_REQUIRED|SLAPI_OC_FLAG_ALLOWED);
						/* check if this entry is an extensble object or not */
						ocname = slapi_value_get_string(v);
						if ( strcasecmp( ocname, "extensibleobject" ) == 0 )
						{
							isextensibleobj = 1;
						}
						charray_merge_nodup(&allattrs, myattrs, 1/*copy_strs*/);
						charray_free(myattrs);
					}
				}
			}
		}

		/* get operational attrs */
		opattrs = slapi_schema_list_attribute_names(SLAPI_ATTR_FLAG_OPATTR);

		if (isextensibleobj)
		{
			for ( i = 0; attrs[i]; i++ )
			{
				_ger_get_attr_rights ( gerpb, e, subjectndn, attrs[i], gerstr, 
								gerstrsize, gerstrcap, isfirstattr, errbuf );
				isfirstattr = 0;
			}
		}
		else
		{
			if (hasstar && hasplus)
			{
				GER_GET_ATTR_RIGHTS(allattrs);
				GER_GET_ATTR_RIGHTS(opattrs);
			}
			else if (hasstar)
			{
				GER_GET_ATTR_RIGHTS(allattrs);
				GER_GET_ATTR_RIGHTA_EXT('*', opattrs, allattrs);
			}
			else if (hasplus)
			{
				GER_GET_ATTR_RIGHTS(opattrs);
				GER_GET_ATTR_RIGHTA_EXT('+', allattrs, opattrs);
			}
			else
			{
				for ( i = 0; attrs[i]; i++ )
				{
					if (charray_inlist(allattrs, attrs[i]) ||
						charray_inlist(opattrs, attrs[i]) ||
						(0 == strcasecmp(attrs[i], "dn")))
					{
						_ger_get_attr_rights ( gerpb, e, subjectndn, attrs[i],
							gerstr, gerstrsize, gerstrcap, isfirstattr, errbuf );
						isfirstattr = 0;
					}
					else
					{
						/* if the attr does not belong to the entry,
						   "<attr>:none" is returned */
						if (!isfirstattr)
						{
							_append_gerstr(gerstr, gerstrsize, gerstrcap, ", ", NULL);
						}
						_append_gerstr(gerstr, gerstrsize, gerstrcap, attrs[i], ":");
						_append_gerstr(gerstr, gerstrsize, gerstrcap, "none", NULL);
						isfirstattr = 0;
					}
				}
			}
			charray_free(allattrs);
			charray_free(opattrs);
		}
	}
	else
	{
		Slapi_Attr *prevattr = NULL, *attr;
		char *type;

		while ( slapi_entry_next_attr ( e, prevattr, &attr ) == 0 )
		{
			if ( ! slapi_attr_flag_is_set (attr, SLAPI_ATTR_FLAG_OPATTR) )
			{
				slapi_attr_get_type ( attr, &type );
				_ger_get_attr_rights ( gerpb, e, subjectndn, type, gerstr,
								gerstrsize, gerstrcap, isfirstattr, errbuf );
				isfirstattr = 0;
			}
			prevattr = attr;
		}
	}

	if ( isfirstattr )
	{
		/* not a single attribute was retrived or specified */
		_append_gerstr(gerstr, gerstrsize, gerstrcap, "*:none", NULL);
	}
	return;
}

/*
 * controlType = LDAP_CONTROL_GET_EFFECTIVE_RIGHTS;
 * criticality = n/a;
 * controlValue = OCTET STRING of BER encoding of the SEQUENCE of
 *				  ENUMERATED LDAP code
 */
void
_ger_set_response_control (
	Slapi_PBlock	*pb,
	int				iscritical,
	int				rc
	)
{
	LDAPControl **resultctrls = NULL;
	LDAPControl gerrespctrl;
	BerElement *ber = NULL;
	struct berval *berval = NULL;
	int found = 0;
	int i;

	if ( (ber = der_alloc ()) == NULL )
	{
		goto bailout;
	}

	/* begin sequence, enumeration, end sequence */
	ber_printf ( ber, "{e}", rc );
	if ( ber_flatten ( ber, &berval ) != LDAP_SUCCESS )
	{
		goto bailout;
	}
	gerrespctrl.ldctl_oid = LDAP_CONTROL_GET_EFFECTIVE_RIGHTS;
	gerrespctrl.ldctl_iscritical = iscritical;
	gerrespctrl.ldctl_value.bv_val = berval->bv_val;
	gerrespctrl.ldctl_value.bv_len = berval->bv_len;

	slapi_pblock_get ( pb, SLAPI_RESCONTROLS, &resultctrls );
	for (i = 0; resultctrls && resultctrls[i]; i++)
	{
		if (strcmp(resultctrls[i]->ldctl_oid, LDAP_CONTROL_GET_EFFECTIVE_RIGHTS) == 0)
		{
			/*
			 * We get here if search returns more than one entry
			 * and this is not the first entry.
			 */
			ldap_control_free ( resultctrls[i] );
			resultctrls[i] = slapi_dup_control (&gerrespctrl);
			found = 1;
			break;
		}
	}

	if ( !found )
	{
		/* slapi_pblock_set() will dup the control */
		slapi_pblock_set ( pb, SLAPI_ADD_RESCONTROL, &gerrespctrl );
	}

bailout:
	ber_free ( ber, 1 );	/* ber_free() checks for NULL param */
	ber_bvfree ( berval );	/* ber_bvfree() checks for NULL param */
}

int
_ger_generate_template_entry (
	Slapi_PBlock    *pb
	)
{
	Slapi_Entry	*e = NULL;
	char **gerattrs = NULL;
	char **attrs = NULL;
	char *templateentry = NULL;
	char *object = NULL;
	char *superior = NULL;
	char *p = NULL;
	char *dn = NULL;
	int siz = 0;
	int len = 0;
	int i = 0;
	int notfirst = 0;
	int rc = LDAP_SUCCESS;

	slapi_pblock_get( pb, SLAPI_SEARCH_GERATTRS, &gerattrs );
	if (NULL == gerattrs)
	{
		slapi_log_error (SLAPI_LOG_FATAL, plugin_name,
						"Objectclass info is expected "
						"in the attr list, e.g., \"*@person\"\n");
		rc = LDAP_SUCCESS;
		goto bailout;
	}
	/* get the target dn where the template entry is located */
	slapi_pblock_get( pb, SLAPI_TARGET_DN, &dn );
	for (i = 0; gerattrs && gerattrs[i]; i++)
	{
		object = strchr(gerattrs[i], '@');
		if (NULL != object && '\0' != *(++object))
		{
			break;
		}
	}
	if (NULL == object)
	{
		rc = LDAP_SUCCESS;	/* no objectclass info; ok to return */
		goto bailout;
	}
	attrs = slapi_schema_list_objectclass_attributes(
						(const char *)object, SLAPI_OC_FLAG_REQUIRED);
	if (NULL == attrs)
	{
		rc = LDAP_SUCCESS;	/* bogus objectclass info; ok to return */
		goto bailout;
	}
	for (i = 0; attrs[i]; i++)
	{
		if (0 == strcasecmp(attrs[i], "objectclass"))
		{
			/* <*attrp>: <object>\n\0 */
			siz += strlen(attrs[i]) + 4 + strlen(object);
		}
		else
		{
			/* <*attrp>: (template_attribute)\n\0 */
			siz += strlen(attrs[i]) + 4 + 20;
		}
	}
	if (dn)
	{
		/* dn: cn=<template_name>,<dn>\n\0 */
		siz += 32 + strlen(object) + strlen(dn);
	}
	else
	{
		/* dn: cn=<template_name>\n\0 */
		siz += 32 + strlen(object);
	}
	templateentry = (char *)slapi_ch_malloc(siz);
	PR_snprintf(templateentry, siz,
		"dn: cn=template_%s_objectclass%s%s\n", object, dn?",":"", dn?dn:"");
	for (--i; i >= 0; i--)
	{
		len = strlen(templateentry);
		p = templateentry + len;
		if (0 == strcasecmp(attrs[i], "objectclass"))
		{
			PR_snprintf(p, siz - len, "%s: %s\n", attrs[i], object);
		}
		else
		{
			PR_snprintf(p, siz - len, "%s: (template_attribute)\n", attrs[i]);
		}
	}
	charray_free(attrs);

	while ((superior = slapi_schema_get_superior_name(object)) &&
			(0 != strcasecmp(superior, "top")))
	{
		if (notfirst)
		{
			slapi_ch_free_string(&object);
		}
		notfirst = 1;
		object = superior;
		attrs = slapi_schema_list_objectclass_attributes(
						(const char *)superior, SLAPI_OC_FLAG_REQUIRED);
		for (i = 0; attrs && attrs[i]; i++)
		{
			if (0 == strcasecmp(attrs[i], "objectclass"))
			{
				/* <*attrp>: <object>\n\0 */
				siz += strlen(attrs[i]) + 4 + strlen(object);
			}
		}
		templateentry = (char *)slapi_ch_realloc(templateentry, siz);
		for (--i; i >= 0; i--)
		{
			len = strlen(templateentry);
			p = templateentry + len;
			if (0 == strcasecmp(attrs[i], "objectclass"))
			{
				PR_snprintf(p, siz - len, "%s: %s\n", attrs[i], object);
			}
		}
		charray_free(attrs);
	}
	if (notfirst)
	{
		slapi_ch_free_string(&object);
	}
	slapi_ch_free_string(&superior);
	siz += 18; /* objectclass: top\n\0 */
	len = strlen(templateentry);
	templateentry = (char *)slapi_ch_realloc(templateentry, siz);
	p = templateentry + len;
	PR_snprintf(p, siz - len, "objectclass: top\n");

	e = slapi_str2entry(templateentry, SLAPI_STR2ENTRY_NOT_WELL_FORMED_LDIF);
	/* set the template entry to send the result to clients */
	slapi_pblock_set(pb, SLAPI_SEARCH_RESULT_ENTRY, e);
bailout:
	slapi_ch_free_string(&templateentry);
	return rc;
}

int
acl_get_effective_rights (
	Slapi_PBlock    *pb,
	Slapi_Entry	    *e,			/* target entry */
	char			**attrs,	/* Attribute of	the entry */
	struct berval   *val,		/* value of attr. NOT USED */
	int		    	access,		/* requested access rights */
	char			**errbuf
	)
{
	Slapi_PBlock *gerpb = NULL;
	void *aclcb = NULL;
	char *subjectndn = NULL;
	char *gerstr = NULL;
	size_t gerstrsize = 0;
	size_t gerstrcap = 0;
	int iscritical = 1;
	int rc = LDAP_SUCCESS;

	*errbuf = '\0';

	if (NULL == e)	/* create a template entry from SLAPI_SEARCH_GERATTRS */
	{
		rc = _ger_generate_template_entry ( pb );
		slapi_pblock_get ( pb, SLAPI_SEARCH_RESULT_ENTRY, &e );
		if ( rc != LDAP_SUCCESS || NULL == e )
		{
			goto bailout;
		}
	}

	/*
	 * Get the subject
	 */
	rc = _ger_parse_control (pb, &subjectndn, &iscritical, errbuf );
	if ( rc != LDAP_SUCCESS )
	{
		goto bailout;
	}

	/*
	 * The requestor should have g permission on the entry
	 * to get the effective rights.
	 */
	rc = _ger_g_permission_granted (pb, e, subjectndn, errbuf);
	if ( rc != LDAP_SUCCESS )
	{
		goto bailout;
	}

	/*
	 * Construct a new pb
	 */
	rc = _ger_new_gerpb ( pb, e, subjectndn, &gerpb, &aclcb, errbuf );
	if ( rc != LDAP_SUCCESS )
	{
		goto bailout;
	}

	/* Get entry level effective rights */
	_ger_get_entry_rights ( gerpb, e, subjectndn, &gerstr, &gerstrsize, &gerstrcap, errbuf );

	/*
	 * Attribute level effective rights may not be NULL
	 * even if entry level's is.
	 */
	_ger_get_attrs_rights ( gerpb, e, subjectndn, attrs, &gerstr, &gerstrsize, &gerstrcap, errbuf );

bailout:
	/*
	 * Now construct the response control
	 */
	_ger_set_response_control ( pb, iscritical, rc );

	if ( rc != LDAP_SUCCESS )
	{
		gerstr = slapi_ch_smprintf("entryLevelRights: %d\nattributeLevelRights: *:%d", rc, rc );
	}

	slapi_log_error (SLAPI_LOG_ACLSUMMARY, plugin_name,
		"###### Effective Rights on Entry (%s) for Subject (%s) ######\n",
		e?slapi_entry_get_ndn(e):"null", subjectndn?subjectndn:"null");
	slapi_log_error (SLAPI_LOG_ACLSUMMARY, plugin_name, "%s\n", gerstr);

	/* Restore pb */
	_ger_release_gerpb ( &gerpb, &aclcb, pb );

	/*
	 * General plugin uses SLAPI_RESULT_TEXT for error text. Here
	 * SLAPI_PB_RESULT_TEXT is exclusively shared with add, dse and schema.
	 * slapi_pblock_set() will free any previous data, and
	 * pblock_done() will free SLAPI_PB_RESULT_TEXT.
	 */
	slapi_pblock_set (pb, SLAPI_PB_RESULT_TEXT, gerstr);

	if ( !iscritical )
	{
		/*
		 * If return code is not LDAP_SUCCESS, the server would
		 * abort sending the data of the entry to the client.
		 */
		rc = LDAP_SUCCESS;
	}

	slapi_ch_free ( (void **) &subjectndn );
	slapi_ch_free ( (void **) &gerstr );
	return rc;
}
