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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
/*
 * urp_glue.c - Update Resolution Procedures - Glue
 */

#include "slapi-plugin.h"
#include "repl5.h"
#include "urp.h"


#define RDNBUFSIZE 2048
extern int slapi_log_urp;

/*
 * Check if the entry is glue.
 */
int
is_glue_entry(const Slapi_Entry* entry)
{
	/* JCMREPL - Is there a more efficient way to do this? */
	return slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS, "glue");
}

/* returns PR_TRUE if the entry is a glue entry, PR_FALSE otherwise
   sets the gluecsn if it is a glue entry - gluecsn may (but should not) be NULL */
PRBool
get_glue_csn(const Slapi_Entry *entry, const CSN **gluecsn)
{
	PRBool isglue = PR_FALSE;
	Slapi_Attr *oc_attr = NULL;

	/* cast away const - entry */
	if (entry_attr_find_wsi((Slapi_Entry*)entry, SLAPI_ATTR_OBJECTCLASS, &oc_attr) == ATTRIBUTE_PRESENT)
	{
		Slapi_Value *glue_value = NULL;
		struct berval v;
		v.bv_val = "glue";
		v.bv_len = strlen(v.bv_val);
		if (attr_value_find_wsi(oc_attr, &v, &glue_value) == VALUE_PRESENT)
		{
			isglue = PR_TRUE;
			*gluecsn = value_get_csn(glue_value, CSN_TYPE_VALUE_UPDATED);
		}
	}

	return isglue;
}

/*
 * Submit a Modify operation to turn the Entry into Glue.
 */
int
entry_to_glue(char *sessionid, const Slapi_Entry* entry, const char *reason, CSN *opcsn)
{
	int op_result = 0;
	const char *dn;
	char ebuf[BUFSIZ];
    slapi_mods smods;
	Slapi_Attr *attr;

	dn = slapi_entry_get_dn_const (entry);
	slapi_mods_init(&smods, 4);
	/*
	  richm: sometimes the entry is already a glue entry (how did that happen?)
	  OR
	  the entry is already objectclass extensibleObject or already has the
	  conflict attribute and/or value
	*/
	if (!slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS, "glue"))
	{
		slapi_mods_add_string( &smods, LDAP_MOD_ADD, SLAPI_ATTR_OBJECTCLASS, "glue" );

		if (!slapi_entry_attr_hasvalue(entry, SLAPI_ATTR_OBJECTCLASS, "extensibleobject"))
			slapi_mods_add_string( &smods, LDAP_MOD_ADD, SLAPI_ATTR_OBJECTCLASS, "extensibleobject" );
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name, 
				"%s: Target entry %s is already a glue entry reason %s\n",
				sessionid, escape_string(dn, ebuf), reason);
	}

	if (slapi_entry_attr_find (entry, ATTR_NSDS5_REPLCONFLICT, &attr) == 0)
	{
		slapi_mods_add_string( &smods, LDAP_MOD_REPLACE, ATTR_NSDS5_REPLCONFLICT, reason);
	}
	else
	{
		slapi_mods_add_string( &smods, LDAP_MOD_ADD, ATTR_NSDS5_REPLCONFLICT, reason);
	}

	if (slapi_mods_get_num_mods(&smods) > 0)
	{
		op_result = urp_fixup_modify_entry (NULL, dn, opcsn, &smods, 0);
		if (op_result == LDAP_SUCCESS)
		{
			slapi_log_error (slapi_log_urp, repl_plugin_name,
				"%s: Turned the entry %s to glue, reason %s\n",
				sessionid, escape_string(dn, ebuf), reason);
		}
    }

	slapi_mods_done(&smods);
	return op_result;
}

static const char *glue_entry = 
	"dn: %s\n"
	"%s"
	"objectclass: top\n"
	"objectclass: extensibleObject\n" /* JCMREPL - To avoid schema checking. */
	"objectclass: glue\n"
	"nsuniqueid: %s\n"
	"%s: %s\n"; /* Add why it's been created */

static int
do_create_glue_entry(const Slapi_RDN *rdn, const Slapi_DN *superiordn, const char *uniqueid, const char *reason, CSN *opcsn)
{
	int op_result= LDAP_OPERATIONS_ERROR;
	int rdnval_index = 0;
	int rdntype_len, rdnval_len, rdnpair_len, rdnstr_len, alloc_len;
	Slapi_Entry *e;
	Slapi_DN *sdn = NULL;
	Slapi_RDN *newrdn = slapi_rdn_new_rdn(rdn);
	char *estr, *rdnstr, *rdntype, *rdnval, *rdnpair;
	sdn = slapi_sdn_new_dn_byval(slapi_sdn_get_ndn(superiordn));
	slapi_sdn_add_rdn(sdn,rdn);


	/* must take care of multi-valued rdn: split rdn into different lines introducing
	 * '\n' between each type/value pair. 
	 */	
	alloc_len = RDNBUFSIZE;
	rdnstr = slapi_ch_malloc(alloc_len);
	rdnpair = rdnstr;	
	*rdnpair = '\0';   /* so that strlen(rdnstr) may return 0 the first time it's called */
	while ((rdnval_index = slapi_rdn_get_next(newrdn, rdnval_index, &rdntype, &rdnval)) != -1) {
	        rdntype_len = strlen(rdntype);
		rdnval_len = strlen(rdnval);
		rdnpair_len = LDIF_SIZE_NEEDED(rdntype_len, rdnval_len);
		rdnstr_len = strlen(rdnstr);
		if ((rdnstr_len + rdnpair_len + 1) > alloc_len) {
		       alloc_len += (rdnpair_len + 1);
		       rdnstr = slapi_ch_realloc(rdnstr, alloc_len);
		       rdnpair = &rdnstr[rdnstr_len];
		}
	        ldif_put_type_and_value_with_options(&rdnpair, rdntype,
				rdnval, rdnval_len, LDIF_OPT_NOWRAP);
		*rdnpair = '\0';
	}	  
	estr= slapi_ch_smprintf(glue_entry, slapi_sdn_get_ndn(sdn), rdnstr, uniqueid, 
			ATTR_NSDS5_REPLCONFLICT, reason);
	slapi_ch_free((void**)&rdnstr);
	slapi_rdn_done(newrdn);
	slapi_ch_free((void**)&newrdn);
	e = slapi_str2entry( estr, 0 );
	PR_ASSERT(e!=NULL);
	if ( e!=NULL )
	{
		slapi_entry_set_uniqueid (e, slapi_ch_strdup(uniqueid));
		op_result = urp_fixup_add_entry (e, NULL, NULL, opcsn, 0);
	}
	slapi_ch_free_string(&estr);
	slapi_sdn_free(&sdn);
	return op_result;
}

int
create_glue_entry ( Slapi_PBlock *pb, char *sessionid, Slapi_DN *dn, const char *uniqueid, CSN *opcsn )
{
	int op_result;
	const char *dnstr;

	if ( slapi_sdn_get_dn (dn) )
		dnstr = slapi_sdn_get_dn (dn);
	else
		dnstr = "";

	if ( NULL == uniqueid )
	{
		op_result = LDAP_OPERATIONS_ERROR;
		slapi_log_error (SLAPI_LOG_FATAL, repl_plugin_name,
			"%s: Can't create glue %s, uniqueid=NULL\n", sessionid, dnstr);
	}
	else
	{
		Slapi_Backend *backend;
		Slapi_DN *superiordn = slapi_sdn_new();
		Slapi_RDN *rdn= slapi_rdn_new();
		int done= 0;

		slapi_pblock_get( pb, SLAPI_BACKEND, &backend );
		slapi_sdn_get_backend_parent ( dn, superiordn, backend );
		slapi_sdn_get_rdn ( dn, rdn );

		while(!done)
		{
			op_result= do_create_glue_entry(rdn, superiordn, uniqueid, "missingEntry", opcsn);
			switch(op_result)
			{
				case LDAP_SUCCESS:
					slapi_log_error ( SLAPI_LOG_FATAL, repl_plugin_name,
						"%s: Created glue entry %s uniqueid=%s reason missingEntry\n",
						sessionid, dnstr, uniqueid);
					done= 1;
					break;
				case LDAP_NO_SUCH_OBJECT:
					/* The parent is missing */
					{
					/* JCMREPL - Create the parent ... recursion?... but what's the uniqueid? */
					PR_ASSERT(0); /* JCMREPL */
					}
				default:
					slapi_log_error ( SLAPI_LOG_FATAL, repl_plugin_name,
						"%s: Can't created glue entry %s uniqueid=%s, error %d\n",
						sessionid, dnstr, uniqueid, op_result);
					break;
			}
			/* JCMREPL - Could get trapped in this loop forever! */
		}

		slapi_rdn_free ( &rdn );
		slapi_sdn_free ( &superiordn );
	}

	return op_result;
}
