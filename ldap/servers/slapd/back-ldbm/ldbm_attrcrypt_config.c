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
/* This file handles configuration information that is specific
 * to ldbm instance attribute encryption configuration.
 */

/* DBDB I left in the Sun copyright statement because some of the code 
 * in this file is derived from an older file : ldbm_index_config.c
 */

#include "back-ldbm.h"
#include "attrcrypt.h"

/* Forward declarations for the callbacks */
int ldbm_instance_attrcrypt_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg); 
int ldbm_instance_attrcrypt_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg); 

/*

Config entries look like this:

dn: cn=<attributeName>, cn=encrypted attributes, cn=databaseName, cn=ldbm database, cn=plugins, cn=config
objectclass: top
objectclass: nsAttributeEncryption
cn: <attributeName>
nsEncryptionAlgorithm: <cipherName>

*/

static int 
ldbm_attrcrypt_parse_cipher(char* cipher_display_name)
{
	attrcrypt_cipher_entry *ce = attrcrypt_cipher_list;
	while (ce->cipher_number) {
		if (0 == strcmp(ce->cipher_display_name,cipher_display_name)) {
			return ce->cipher_number;
		}
		ce++;
	}
	return 0;
}

static int 
ldbm_attrcrypt_parse_entry(ldbm_instance *inst, Slapi_Entry *e,
                                  char **attribute_name,
								  int *cipher)
{
    Slapi_Attr *attr;
    const struct berval *attrValue;
    Slapi_Value *sval;

	*cipher = 0;
	*attribute_name = NULL;

    /* Get the name of the attribute to index which will be the value
     * of the cn attribute. */
    if (slapi_entry_attr_find(e, "cn", &attr) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "Warning: malformed attribute encryption entry %s\n",
                  slapi_entry_get_dn(e), 0, 0);
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);
    *attribute_name = slapi_ch_strdup(attrValue->bv_val);

    /* Get the list of index types from the entry. */
    if (0 == slapi_entry_attr_find(e, "nsEncryptionAlgorithm", &attr)) {
		slapi_attr_first_value(attr, &sval);
        if (sval) {
            attrValue = slapi_value_get_berval(sval);
			*cipher = ldbm_attrcrypt_parse_cipher(attrValue->bv_val);  
			if (0 == *cipher)
				LDAPDebug(LDAP_DEBUG_ANY, "Warning: attempt to configure unrecognized cipher %s in encrypted attribute config entry %s\n",
					attrValue->bv_val, slapi_entry_get_dn(e), 0);        
		}
    }
    return LDAP_SUCCESS;
}

static void
ldbm_instance_attrcrypt_enable(struct attrinfo *ai, int cipher)
{
	attrcrypt_private *priv = NULL;
	if (NULL == ai->ai_attrcrypt) {
		/* No existing private structure, allocate one */
		ai->ai_attrcrypt = (attrcrypt_private*) slapi_ch_calloc(1, sizeof(attrcrypt_private));
	}
	priv = ai->ai_attrcrypt;
	priv->attrcrypt_cipher = cipher;
}

static void
ldbm_instance_attrcrypt_disable(struct attrinfo *ai)
{
	if (NULL != ai->ai_attrcrypt) {
		/* Don't free the structure here, because other threads might be
		 * concurrently referencing it.
		 */
		ai->ai_attrcrypt = 0;
	}
}
                                  
/* 
 * Config DSE callback for attribute encryption entry add.
 */	
int 
ldbm_instance_attrcrypt_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* eAfter, int *returncode, char *returntext, void *arg) 
{ 
    ldbm_instance *inst = (ldbm_instance *) arg;
    char *attribute_name = NULL;
	int cipher = 0;
	int ret = 0;

    returntext[0] = '\0';

	/* For add, we parse the entry, then check the attribute exists, 
	 * then check that indexing config does not preclude us encrypting it,
	 * and finally we set the private structure in the attrinfo for the attribute.
	 */

    *returncode = ldbm_attrcrypt_parse_entry(inst, e, &attribute_name , &cipher);

    if (*returncode == LDAP_SUCCESS) {

        struct attrinfo *ai = NULL;

		/* If the cipher was invalid, return unwilling to perform */
		if (0 == cipher) {
			returntext = "invalid cipher";
			*returncode = LDAP_UNWILLING_TO_PERFORM;
			ret = SLAPI_DSE_CALLBACK_ERROR;
		} else {

			ainfo_get(inst->inst_be, attribute_name, &ai);
			/* If we couldn't find a non-default attrinfo, then that means
			 * that no indexing or encryption has yet been defined for this attribute
			 * therefore , create a new attrinfo structure now.
			 */
			if ((ai == NULL) || (0 == strcmp(LDBM_PSEUDO_ATTR_DEFAULT, ai->ai_type) )) {
				/* If this attribute doesn't exist in the schema, then we DO NOT fail 
				 * (this is because entensible objects and disabled schema checking allow
				 * non-schema attributes to exist.
				 */
				/* Make a new attrinfo object */
				attr_create_empty(inst->inst_be,attribute_name,&ai);
			}
			if (ai) {
				ldbm_instance_attrcrypt_enable(ai, cipher);
				/* Remember that we have some encryption enabled, so we can be intelligent about warning when SSL is not enabled */
				inst->attrcrypt_configured = 1;
			} else {
				LDAPDebug(LDAP_DEBUG_ANY, "Warning: attempt to encryption on a non-existent attribute: %s\n",
					  attribute_name, 0, 0);
				returntext = "attribute does not exist";
				*returncode = LDAP_UNWILLING_TO_PERFORM;
				ret = SLAPI_DSE_CALLBACK_ERROR;
			}
			ret = SLAPI_DSE_CALLBACK_OK;
		}

    } else {
        ret = SLAPI_DSE_CALLBACK_ERROR;
    }
	if (attribute_name) {
		slapi_ch_free_string(&attribute_name);
	}
	return ret;
}

/*
 * Temp callback that gets called for each attribute encryption entry when a new
 * instance is starting up.
 */
int 
ldbm_attrcrypt_init_entry_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
	return ldbm_instance_attrcrypt_config_add_callback(pb,e,entryAfter,returncode,returntext,arg);
}

/*
 * Config DSE callback for attribute encryption deletes.
 */	
int 
ldbm_instance_attrcrypt_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg) 
{ 
    ldbm_instance *inst = (ldbm_instance *) arg;
    char *attribute_name = NULL;
	int cipher = 0;
	int ret = SLAPI_DSE_CALLBACK_ERROR;

    returntext[0] = '\0';

	/* For add, we parse the entry, then check the attribute exists, 
	 * then check that indexing config does not preclude us encrypting it,
	 * and finally we set the private structure in the attrinfo for the attribute.
	 */

    *returncode = ldbm_attrcrypt_parse_entry(inst, e, &attribute_name , &cipher);

    if (*returncode == LDAP_SUCCESS) {

        struct attrinfo *ai = NULL;

        ainfo_get(inst->inst_be, attribute_name, &ai);
        if (ai == NULL && (0 == strcmp(LDBM_PSEUDO_ATTR_DEFAULT, ai->ai_type)) ) {
			LDAPDebug(LDAP_DEBUG_ANY, "Warning: attempt to delete encryption for non-existant attribute: %s\n",
				attribute_name, 0, 0);
		} else {
			ldbm_instance_attrcrypt_disable(ai);
			ret = SLAPI_DSE_CALLBACK_OK;
		}
	}
	if (attribute_name) {
		slapi_ch_free((void **)&attribute_name);
	}
	return ret;
}

/*
 * Config DSE callback for index entry changes.
 *
 * this function is huge!
 */
int
ldbm_instance_attrcrypt_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry *e,
        Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    Slapi_Attr *attr;
    Slapi_Value *sval;
    const struct berval *attrValue;
    struct attrinfo *ainfo = NULL;
    LDAPMod **mods;
	int i = 0;
	int j = 0;

    returntext[0] = '\0';
    *returncode = LDAP_SUCCESS;
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

    slapi_entry_attr_find(e, "cn", &attr);
    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);
    ainfo_get(inst->inst_be, attrValue->bv_val, &ainfo);
    if (NULL == ainfo) {
        return SLAPI_DSE_CALLBACK_ERROR;
    }

    for (i = 0; mods[i] != NULL; i++) {

        char *config_attr = (char *)mods[i]->mod_type;

		/* There are basically three cases in the modify: 
		 * 1. The attribute was added
		 * 2. The attribute was deleted
		 * 3. The attribute was modified (deleted and added).
		 * Now, of these three, only #3 is legal.
		 * This is because the attribute is mandatory and single-valued in the schema.
		 * We handle this as follows: an add will always replace what's there (if anything).
		 * a delete will remove what's there as long as it matches what's being deleted.
		 * this is to avoid ordering problems with the adds and deletes.
		 */

        if (strcasecmp(config_attr, "nsEncryptionAlgorithm") == 0) {

            if ((mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_ADD) {

                for (j = 0; mods[i]->mod_bvalues[j] != NULL; j++) {
                    int cipher = ldbm_attrcrypt_parse_cipher(mods[i]->mod_bvalues[j]->bv_val);
					if (0 == cipher) {
						/* Tried to configure an invalid cipher */
					}
					ldbm_instance_attrcrypt_enable(ainfo,cipher);
                }
                continue;
            }
            if ((mods[i]->mod_op & ~LDAP_MOD_BVALUES) == LDAP_MOD_DELETE) {
                if ((mods[i]->mod_bvalues == NULL) ||
                    (mods[i]->mod_bvalues[0] == NULL)) {
					/* Not legal */
					return SLAPI_DSE_CALLBACK_ERROR;
                } else {
                    for (j = 0; mods[i]->mod_bvalues[j] != NULL; j++) {
						/* Code before here should ensure that we only ever delete something that was already here */
						ldbm_instance_attrcrypt_disable(ainfo);
                    }
                }
                continue;
            }
        }
	}
    return SLAPI_DSE_CALLBACK_OK;
}

