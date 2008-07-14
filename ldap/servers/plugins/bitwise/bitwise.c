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
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* orfilter.c - implementation of ordering rule filter */

#include <ldap.h> /* LDAP_UTF8INC */
#include <slap.h> /* for debug macros */
#include <slapi-plugin.h> /* slapi_berval_cmp, SLAPI_BERVAL_EQ */

#ifdef HPUX11
#include <dl.h>
#endif /* HPUX11 */

/* the match function needs the attribute type and value from the search
   filter - this is unfortunately not passed into the match fn, so we
   have to keep track of this
*/
struct bitwise_match_cb {
    char *type; /* the attribute type from the filter ava */
    struct berval *val; /* the value from the filter ava */
};

/*
  The type and val pointers are assumed to have sufficient lifetime -
  we don't have to copy them - they are usually just pointers into
  the SLAPI_PLUGIN_MR_TYPE and SLAPI_PLUGIN_MR_VALUE fields of the
  operation pblock, whose lifetime should encompass the creation
  and destruction of the bitwise_match_cb object.
*/
static struct bitwise_match_cb *
new_bitwise_match_cb(char *type, struct berval *val)
{
    struct bitwise_match_cb *bmc = (struct bitwise_match_cb *)slapi_ch_calloc(1, sizeof(struct bitwise_match_cb));
    bmc->type = type;
    bmc->val = val;

    return bmc;
}

static void
delete_bitwise_match_cb(struct bitwise_match_cb *bmc)
{
    slapi_ch_free((void **)&bmc);
}

static void
bitwise_filter_destroy(Slapi_PBlock* pb)
{
    void *obj = NULL;
    slapi_pblock_get(pb, SLAPI_PLUGIN_OBJECT, &obj);
    if (obj) {
	struct bitwise_match_cb *bmc = (struct bitwise_match_cb *)obj;
	delete_bitwise_match_cb(bmc);
	obj = NULL;
	slapi_pblock_set(pb, SLAPI_PLUGIN_OBJECT, obj);
    }
}

#define BITWISE_OP_AND  0
#define BITWISE_OP_OR   1

static int
internal_bitwise_filter_match(void* obj, Slapi_Entry* entry, Slapi_Attr* attr, int op)
/* returns:  0  filter matched
 *	    -1  filter did not match
 *	    >0  an LDAP error code
 */
{
    struct bitwise_match_cb *bmc = obj;
    unsigned long long a, b;
    char *val_from_entry = NULL;
    auto int rc = -1; /* no match */

    val_from_entry = slapi_entry_attr_get_charptr(entry, bmc->type);
    if (val_from_entry) {
	errno = 0;
	a = strtoull(val_from_entry, NULL, 10);
	if (errno != ERANGE) {
	    errno = 0;
	    b = strtoull(bmc->val->bv_val, NULL, 10);
	    if (errno == ERANGE) {
		rc = LDAP_CONSTRAINT_VIOLATION;
	    } else {
		int result;
		if (op == BITWISE_OP_AND) {
		    result = (a & b);
		} else if (op == BITWISE_OP_OR) {
		    result = (a | b);
		}
		if (result) {
		    rc = 0;
		}
	    }
	}
	slapi_ch_free_string(&val_from_entry);
    }
    return rc;
}

static int
bitwise_filter_match_and (void* obj, Slapi_Entry* entry, Slapi_Attr* attr)
/* returns:  0  filter matched
 *	    -1  filter did not match
 *	    >0  an LDAP error code
 */
{
    return internal_bitwise_filter_match(obj, entry, attr, BITWISE_OP_AND);
}

static int
bitwise_filter_match_or (void* obj, Slapi_Entry* entry, Slapi_Attr* attr)
/* returns:  0  filter matched
 *	    -1  filter did not match
 *	    >0  an LDAP error code
 */
{
    return internal_bitwise_filter_match(obj, entry, attr, BITWISE_OP_OR);
}

static int
bitwise_filter_create (Slapi_PBlock* pb)
{
    auto int rc = LDAP_UNAVAILABLE_CRITICAL_EXTENSION; /* failed to initialize */
    auto char* mrOID = NULL;
    auto char* mrTYPE = NULL;
    auto struct berval* mrVALUE = NULL;

    if (!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_OID, &mrOID) && mrOID != NULL &&
	!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_TYPE, &mrTYPE) && mrTYPE != NULL &&
	!slapi_pblock_get (pb, SLAPI_PLUGIN_MR_VALUE, &mrVALUE) && mrVALUE != NULL) {

	struct bitwise_match_cb *bmc = NULL;
	if (strcmp(mrOID, "1.2.840.113556.1.4.803") == 0) {
	    slapi_pblock_set (pb, SLAPI_PLUGIN_MR_FILTER_MATCH_FN, (void*)bitwise_filter_match_and);
	} else if (strcmp(mrOID, "1.2.840.113556.1.4.804") == 0) {
	    slapi_pblock_set (pb, SLAPI_PLUGIN_MR_FILTER_MATCH_FN, (void*)bitwise_filter_match_or);
	} else { /* this oid not handled by this plugin */
	    LDAPDebug (LDAP_DEBUG_FILTER, "=> bitwise_filter_create OID (%s) not handled\n", mrOID, 0, 0);
	    return rc;
	}
	bmc = new_bitwise_match_cb(mrTYPE, mrVALUE);
	slapi_pblock_set (pb, SLAPI_PLUGIN_OBJECT, bmc);
	slapi_pblock_set (pb, SLAPI_PLUGIN_DESTROY_FN, (void*)bitwise_filter_destroy);
	rc = LDAP_SUCCESS;
    } else {
	LDAPDebug (LDAP_DEBUG_FILTER, "=> bitwise_filter_create missing parameter(s)\n", 0, 0, 0);
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "<= bitwise_filter_create %i\n", rc, 0, 0);
    return LDAP_SUCCESS;
}

static Slapi_PluginDesc pdesc = { "bitwise", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
              "bitwise match plugin" };

int /* LDAP error code */
bitwise_init (Slapi_PBlock* pb)
{
    int rc;

    rc = slapi_pblock_set (pb, SLAPI_PLUGIN_MR_FILTER_CREATE_FN, (void*)bitwise_filter_create);
    if ( rc == 0 ) {
	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION, (void *)&pdesc );
    }
    LDAPDebug (LDAP_DEBUG_FILTER, "bitwise_init %i\n", rc, 0, 0);
    return rc;
}
