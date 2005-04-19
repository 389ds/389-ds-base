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
/* parents.c - where the adults live */

#include "back-ldbm.h"

char *numsubordinates = "numsubordinates";
char *hassubordinates = "hassubordinates";

/* Routine where any in-memory modification of a parent entry happens on some state-change in
   one of its children. vaid op values are: 1 == child entry newly added, 2 == child entry about to be
   deleted; 3 == child modified, in which case childmods points to the modifications. The child entry
   passed is in the state which reflects the mods having been appied. op ==3 HAS NOT BEEN IMPLEMENTED YET
   The routine is allowed to modify the parent entry, and to return a set of LDAPMods reflecting
   the changes it made. The LDAPMods array must be freed by the called by calling ldap_free_mods(p,1)

 */
int parent_update_on_childchange(modify_context *mc,int op, size_t *new_sub_count )
{
	int ret = 0;
	int mod_op = 0;
	Slapi_Attr	*read_attr = NULL;
	size_t current_sub_count = 0;
	int already_present = 0;

	if (new_sub_count)
		*new_sub_count = 0;

	/* Check nobody is trying to use op == 3, it's not implemented yet */
	PR_ASSERT( (op == 1) || (op == 2));

	/* We want to invent a mods set to be passed to modify_apply_mods() */

	/* For now, we're only interested in subordinatecount. 
	   We first examine the present value for the attribute. 
	   If it isn't present and we're adding, we assign value 1 to the attribute and add it.
	   If it is present, we increment or decrement depending upon whether we're adding or deleting.
	   If the value after decrementing is zero, we remove it.
	*/

	/* Get the present value of the subcount attr, or 0 if not present */
	ret = slapi_entry_attr_find(mc->old_entry->ep_entry,numsubordinates,&read_attr);
	if (0 == ret) {
		/* decode the value */
		Slapi_Value *sval;
		slapi_attr_first_value( read_attr, &sval );
		if (sval!=NULL) {
			const struct berval *bval = slapi_value_get_berval(sval);
			if(NULL != bval) {
			    already_present = 1;
			    current_sub_count = atol(bval->bv_val);
			}
		}
  	}
	/* are we adding ? */
	if ( (1 == op) && !already_present) {
		/* If so, and the parent entry does not already have a subcount attribute, we need to add it */
		mod_op = LDAP_MOD_ADD;
	} else {
		if (2 == op) {
			if (!already_present) {
				/* This means that something is wrong---deleting a child but no subcount present on parent */
				LDAPDebug( LDAP_DEBUG_ANY, "numsubordinates assertion failure\n", 0, 0, 0 );
				return -1;
			} else {
				if (current_sub_count == 1) {
					mod_op = LDAP_MOD_DELETE;
				} else {
					mod_op = LDAP_MOD_REPLACE;
				}
			}
		} else {
			mod_op = LDAP_MOD_REPLACE;
		}
	}
	
	/* Mow compute the new value */
	if (1 == op) {
		current_sub_count++;
	} else {
		current_sub_count--;
	}

    {
		Slapi_Mods *smods= slapi_mods_new();
		if (mod_op == LDAP_MOD_DELETE)
		{
            slapi_mods_add(smods, mod_op | LDAP_MOD_BVALUES, numsubordinates, 0, NULL);
		}
		else
		{
        	char value_buffer[20]; /* enough digits for 2^64 children */
        	sprintf(value_buffer,"%lu", current_sub_count);
            slapi_mods_add(smods, mod_op | LDAP_MOD_BVALUES, numsubordinates, strlen(value_buffer), value_buffer);
		}
    	ret = modify_apply_mods(mc,smods); /* smods passed in */
	}

	if (new_sub_count)
		*new_sub_count = current_sub_count;
	return ret;
}
