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

/* This file handles attribute encryption.
 */


#include "back-ldbm.h"
#include "attrcrypt.h"
#include "pk11func.h"
#include "keyhi.h"
#include "nss.h"

/*
 * Todo: 
 * Remember to free the private structures in the attrinfos, so avoid a leak.
 */

attrcrypt_cipher_entry attrcrypt_cipher_list[] = { {ATTRCRYPT_CIPHER_AES, "AES", CKM_AES_CBC_PAD, CKM_AES_CBC_PAD, CKM_AES_CBC_PAD, 128/8, 16} , 
	{ATTRCRYPT_CIPHER_DES3 , "3DES" , CKM_DES3_CBC_PAD, CKM_DES3_CBC_PAD, CKM_DES3_CBC_PAD, 112/8, 8}, 
	{0} };

#define KEY_ATTRIBUTE_NAME "nsSymmetricKey"

/*
 * We maintain one of these structures per cipher that we handle
 */

typedef struct _attrcrypt_cipher_state {
	char *cipher_display_name;
	PRLock *cipher_lock;
	PK11SlotInfo *slot;
	PK11SymKey *key;
	attrcrypt_cipher_entry *ace;
} attrcrypt_cipher_state;

struct _attrcrypt_state_private {
	attrcrypt_cipher_state *acs_array[1];
};

static int attrcrypt_wrap_key(attrcrypt_cipher_state *acs, PK11SymKey *symmetric_key, SECKEYPublicKey *public_key, SECItem *wrapped_symmetric_key);
static int attrcrypt_unwrap_key(attrcrypt_cipher_state *acs, SECKEYPrivateKey *private_key, SECItem *wrapped_symmetric_key, PK11SymKey **unwrapped_symmetric_key);

/*
 * Copied from front-end because it's private to plugins
 */

static int
local_valuearray_count( Slapi_Value **va)
{
    int i=0;
	if(va!=NULL)
	{
	    while(NULL != va[i]) i++;
	}
    return(i);
}

/*
 * Helper functions for key management
 */

static Slapi_Entry *
getConfigEntry( const char *dn, Slapi_Entry **e2 ) {
	Slapi_DN	sdn;

	slapi_sdn_init_dn_byref( &sdn, dn );
	slapi_search_internal_get_entry( &sdn, NULL, e2,
			plugin_get_default_component_id());
	slapi_sdn_done( &sdn );
	return *e2;
}

/**
 * Free an entry
 */
static void
freeConfigEntry( Slapi_Entry ** e ) {
	if ( (e != NULL) && (*e != NULL) ) {
		slapi_entry_free( *e );
		*e = NULL;
	}
}

static int
attrcrypt_get_ssl_cert_name(char **cert_name)
{
	char *config_entry_dn = "cn=RSA,cn=encryption,cn=config";
	Slapi_Entry *config_entry = NULL;
	
	*cert_name = NULL;
	getConfigEntry(config_entry_dn, &config_entry);
	if (NULL == config_entry) {
		return -1;
	}
	*cert_name = slapi_entry_attr_get_charptr( config_entry, "nssslpersonalityssl" );
	freeConfigEntry(&config_entry);
	return 0;
}

/* Retrieve a symmetric key from dse.ldif for a specified cipher */
static int
attrcrypt_keymgmt_get_key(ldbm_instance *li, attrcrypt_cipher_state *acs, SECKEYPrivateKey *private_key, PK11SymKey **key_from_store)
{
	int ret = 0;
	Slapi_Entry *entry = NULL;
	char *dn_template = "cn=%s,cn=encrypted attribute keys,cn=%s,cn=%s,cn=plugins,cn=config";
	char *instance_name =  li->inst_name;
	Slapi_Attr *keyattr = NULL;
	char *dn_string = NULL;
	
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_keymgmt_get_key\n", 0, 0, 0);
	dn_string = slapi_create_dn_string(dn_template,
								acs->ace->cipher_display_name, instance_name,
								li->inst_li->li_plugin->plg_name);
	if (NULL == dn_string) {
		LDAPDebug(LDAP_DEBUG_ANY,
				  "attrcrypt_keymgmt_get_key: "
				  "failed create attrcrypt key dn for plugin %s, "
				  "instance %s, cypher %s\n", 
				  li->inst_li->li_plugin->plg_name,
				  li->inst_name, acs->ace->cipher_display_name);
		ret = -1;
		goto bail;
	}
	/* Fetch the entry */
	getConfigEntry(dn_string, &entry);
	/* Did we find the entry ? */
	if (NULL != entry) {
		SECItem key_to_unwrap = {0};
		/* If so then look for the attribute that contains the key */
		slapi_entry_attr_find(entry, KEY_ATTRIBUTE_NAME, &keyattr);
		if (keyattr != NULL) {
			Slapi_Value *v = NULL;
			slapi_valueset_first_value( &keyattr->a_present_values, &v);
			key_to_unwrap.len = slapi_value_get_length(v);
			key_to_unwrap.data = (void*)slapi_value_get_string(v);
    		}
		/* Unwrap it */
		ret = attrcrypt_unwrap_key(acs, private_key, &key_to_unwrap, key_from_store);
		if (entry) {
			freeConfigEntry(&entry);
		}
	} else {
		ret = -2; /* Means: we didn't find the entry (which happens if the key has never been generated) */	
	}
bail:
	slapi_ch_free_string(&dn_string);
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_keymgmt_get_key\n", 0, 0, 0);
	return ret;
}

/* Store a symmetric key for a given cipher in dse.ldif */
static int
attrcrypt_keymgmt_store_key(ldbm_instance *li, attrcrypt_cipher_state *acs, SECKEYPublicKey *public_key, PK11SymKey *key_to_store)
{
	int ret = 0;
	SECItem wrapped_symmetric_key = {0};
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_keymgmt_store_key\n", 0, 0, 0);
	/* Wrap the key and then store it in the right place in dse.ldif */
	ret = attrcrypt_wrap_key(acs, key_to_store, public_key, &wrapped_symmetric_key);
	if (!ret) {
		/* Make the entry to store */
		Slapi_Entry *e = NULL;
		Slapi_PBlock *pb = slapi_pblock_new();
		Slapi_Value *key_value = NULL;
		struct berval key_as_berval = {0};
		int rc = 0;
		char *entry_template = 
			"dn: cn=%s,cn=encrypted attribute keys,cn=%s,cn=ldbm database,cn=plugins,cn=config\n"
			"objectclass:top\n"
    			"objectclass:extensibleObject\n"
    			"cn:%s\n";
		char *instance_name =  li->inst_name;
		char *entry_string = slapi_ch_smprintf(entry_template,acs->ace->cipher_display_name,instance_name,acs->ace->cipher_display_name);
		e = slapi_str2entry(entry_string, 0);
		/* Add the key as a binary attribute */
		key_as_berval.bv_val = (char *)wrapped_symmetric_key.data;
		key_as_berval.bv_len = wrapped_symmetric_key.len;
		key_value = slapi_value_new_berval(&key_as_berval);
		/* key_value is now a copy of key_as_berval - free wrapped_symmetric_key */
		slapi_ch_free_string((char **)&wrapped_symmetric_key.data);
		slapi_entry_add_value(e, KEY_ATTRIBUTE_NAME, key_value);
		slapi_value_free(&key_value);
		/* Store the entry */
		slapi_add_entry_internal_set_pb(pb, e, NULL, li->inst_li->li_identity, 0);
		rc = slapi_add_internal_pb(pb);
		slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
		if (rc != LDAP_SUCCESS) {
			char *resulttext = NULL;
			slapi_pblock_get(pb, SLAPI_PB_RESULT_TEXT, &resulttext);
			LDAPDebug(LDAP_DEBUG_ANY, "attrcrypt_keymgmt_store_key: failed to add config key entries to the DSE: %d: %s: %s\n", rc, ldap_err2string(rc), resulttext ? resulttext : "unknown");
			ret = -1;
		}
		if (entry_string) {
			slapi_ch_free((void**)&entry_string);
		}
		if (pb) {
			slapi_pblock_destroy(pb);
		}
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_keymgmt_store_key\n", 0, 0, 0);
	return ret;
}

/*
 * Helper functions for key generation and wrapping
 */

/* Wrap a key with the server's public assymetric key for storage */
static int
attrcrypt_wrap_key(attrcrypt_cipher_state *acs, PK11SymKey *symmetric_key, SECKEYPublicKey *public_key, SECItem *wrapped_symmetric_key)
{
	int ret = 0;
	SECStatus s = 0;
	CK_MECHANISM_TYPE wrap_mechanism = CKM_RSA_PKCS;
	SECKEYPublicKey *wrapping_key = public_key;
	wrapped_symmetric_key->len = slapd_SECKEY_PublicKeyStrength(public_key);
	wrapped_symmetric_key->data = (unsigned char *)slapi_ch_malloc(wrapped_symmetric_key->len);
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_wrap_key\n", 0, 0, 0);
	s = slapd_pk11_PubWrapSymKey(wrap_mechanism, wrapping_key, symmetric_key, wrapped_symmetric_key);
	if (SECSuccess != s) {
		ret = -1;
		LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_wrap_key: failed to wrap key for cipher %s\n", acs->ace->cipher_display_name, 0, 0);
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_wrap_key\n", 0, 0, 0);
	return ret;
}

/* Unwrap a key previously wrapped with the server's private key */
static int
attrcrypt_unwrap_key(attrcrypt_cipher_state *acs, SECKEYPrivateKey *private_key, SECItem *wrapped_symmetric_key, PK11SymKey **unwrapped_symmetric_key)
{
	int ret = 0;
	CK_MECHANISM_TYPE wrap_mechanism = acs->ace->wrap_mechanism;
	SECKEYPrivateKey *unwrapping_key = private_key;
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_unwrap_key\n", 0, 0, 0);
	*unwrapped_symmetric_key = slapd_pk11_PubUnwrapSymKey(unwrapping_key, wrapped_symmetric_key, wrap_mechanism, CKA_UNWRAP, 0);
	if (NULL == *unwrapped_symmetric_key) {
		ret = -1;
		LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_unwrap_key: failed to unwrap key for cipher %s\n", acs->ace->cipher_display_name, 0, 0);
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_unwrap_key\n", 0, 0, 0);
	return ret;
}

/* Generate a random key for a specified cipher */
static int
attrcrypt_generate_key(attrcrypt_cipher_state *acs,PK11SymKey **symmetric_key)
{
	int ret = -1;
	PK11SymKey *new_symmetric_key = NULL;
	*symmetric_key = NULL;
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_generate_key\n", 0, 0, 0);
	new_symmetric_key = slapd_pk11_KeyGen(acs->slot, acs->ace->key_gen_mechanism, NULL, acs->ace->key_size, NULL);	
	if (new_symmetric_key) {
		*symmetric_key = new_symmetric_key;
		ret = 0;
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_generate_key\n", 0, 0, 0);
	return ret;
}

static int
attrcrypt_fetch_public_key(SECKEYPublicKey **public_key)
{
	int ret = 0;
	CERTCertificate   *cert = NULL;
	SECKEYPublicKey  *key = NULL;
	PRErrorCode errorCode = 0;
	char *default_cert_name = "server-cert";
	char *cert_name = NULL;
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_fetch_public_key\n", 0, 0, 0);
	*public_key = NULL;
	/* Try to grok the server cert name from the SSL config */
	ret = attrcrypt_get_ssl_cert_name(&cert_name);
	if (ret) {
		cert_name = default_cert_name;
	}
	/* We assume that the server core pin stuff is already enabled, via the SSL initialization done in the front-end */
	cert = slapd_pk11_findCertFromNickname(cert_name, NULL);
	if (cert == NULL) {
		errorCode = PR_GetError();
                LDAPDebug(LDAP_DEBUG_ANY,"Can't find certificate %s in attrcrypt_fetch_public_key: %d - %s\n", cert_name, errorCode, slapd_pr_strerror(errorCode));
	}
	if( cert != NULL ) {
		key = slapd_CERT_ExtractPublicKey(cert);
	}
	if (key == NULL) {
                errorCode = PR_GetError();
                LDAPDebug(LDAP_DEBUG_ANY,"Can't get private key from cert %s in attrcrypt_fetch_public_key: %d - %s\n", cert_name, errorCode, slapd_pr_strerror(errorCode));
				ret = -1;
	}
	if (cert) {
		slapd_pk11_CERT_DestroyCertificate(cert);
	}
	if (key) {
		*public_key = key;
	}else {
		ret = -1;
	}
	if (cert_name != default_cert_name) {
		slapi_ch_free_string(&cert_name);
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_fetch_public_key\n", 0, 0, 0);
	return ret;
}

static int
attrcrypt_fetch_private_key(SECKEYPrivateKey **private_key)
{
	int ret = 0;
	CERTCertificate   *cert = NULL;
	SECKEYPrivateKey  *key = NULL;
	PRErrorCode errorCode = 0;
	char *default_cert_name = "server-cert";
	char *cert_name = NULL;
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_fetch_private_key\n", 0, 0, 0);
	*private_key = NULL;
	/* Try to grok the server cert name from the SSL config */
	ret = attrcrypt_get_ssl_cert_name(&cert_name);
	if (ret) {
		cert_name = default_cert_name;
	}
	/* We assume that the server core pin stuff is already enabled, via the SSL initialization done in the front-end */
	cert = slapd_pk11_findCertFromNickname(cert_name, NULL);
	if (cert == NULL) {
		errorCode = PR_GetError();
        LDAPDebug(LDAP_DEBUG_ANY,"Can't find certificate %s in attrcrypt_fetch_private_key: %d - %s\n", cert_name, errorCode, slapd_pr_strerror(errorCode));
	}
	if( cert != NULL ) {
		key = slapd_pk11_findKeyByAnyCert(cert, NULL);
	}
	if (key == NULL) {
                errorCode = PR_GetError();
                LDAPDebug(LDAP_DEBUG_ANY,"Can't get private key from cert %s in attrcrypt_fetch_private_key: %d - %s\n", cert_name, errorCode, slapd_pr_strerror(errorCode));
				ret = -1;
	}
	if (cert) {
		slapd_pk11_CERT_DestroyCertificate(cert);
	}
	if (key) {
		*private_key = key;
	} else {
		ret = -1;
	}
	if (cert_name != default_cert_name) {
		slapi_ch_free_string(&cert_name);
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_fetch_private_key\n", 0, 0, 0);
	return ret;
}

/*
 CKM_AES_CBC_PAD
 CKM_DES3_CBC_PAD
 */

/* Initialize the structure for a single cipher */
static int
attrcrypt_cipher_init(ldbm_instance *li, attrcrypt_cipher_entry *ace, SECKEYPrivateKey *private_key, SECKEYPublicKey *public_key, attrcrypt_cipher_state *acs)
{
	int ret = 0;
	PK11SymKey *symmetric_key = NULL;
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_cipher_init\n", 0, 0, 0);
	acs->cipher_lock = PR_NewLock();
	/* Fill in some basic stuff */
	acs->ace = ace;
	acs->cipher_display_name = ace->cipher_display_name;
	if (NULL == acs->cipher_lock) {
		LDAPDebug(LDAP_DEBUG_ANY,"Failed to create cipher lock in attrcrypt_cipher_init\n", 0, 0, 0);
	}
	acs->slot = slapd_pk11_GetInternalKeySlot();
	if (NULL == acs->slot) {
		LDAPDebug(LDAP_DEBUG_ANY,"Failed to create a slot for cipher %s in attrcrypt_cipher_entry\n", acs->cipher_display_name, 0, 0);
		goto error;
	}
	/* Try to get the symmetric key for this cipher */
	ret = attrcrypt_keymgmt_get_key(li,acs,private_key,&symmetric_key);
	if (ret) {
		if (-2 == ret) {
			LDAPDebug(LDAP_DEBUG_ANY,"No symmetric key found for cipher %s in backend %s, attempting to create one...\n", acs->cipher_display_name, li->inst_name, 0);
			ret = attrcrypt_generate_key(acs,&symmetric_key);
			if (ret) {
				LDAPDebug(LDAP_DEBUG_ANY,"Failed to generate key for %s in attrcrypt_cipher_init\n", acs->cipher_display_name, 0, 0);
			}
			if (symmetric_key) {
				ret = attrcrypt_keymgmt_store_key(li,acs,public_key,symmetric_key);
				if (ret) {
					LDAPDebug(LDAP_DEBUG_ANY,"Failed to store key for cipher %s in attrcrypt_cipher_init\n", acs->cipher_display_name, 0, 0);
				} else {
					LDAPDebug(LDAP_DEBUG_ANY,"Key for cipher %s successfully generated and stored\n", acs->cipher_display_name, 0, 0);
				}
			}
			
		}  else {
			LDAPDebug(LDAP_DEBUG_ANY,"Failed to retrieve key for cipher %s in attrcrypt_cipher_init\n", acs->cipher_display_name, 0, 0);
		}
	}
	if (symmetric_key) {
		/* we loaded the symmetric key, store it in the acs */
		acs->key = symmetric_key;
	}
error:
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_cipher_init\n", 0, 0, 0);
	return ret;
}

static void 
attrcrypt_acs_list_add(ldbm_instance *li,attrcrypt_cipher_state *acs)
{
	/* Realloc the existing list and add to the end */
	attrcrypt_cipher_state **current = NULL;
	size_t list_size = 0;
	/* Is the list already there ? */
	if (NULL == li->inst_attrcrypt_state_private) {
		/* If not, add it */
		li->inst_attrcrypt_state_private = (attrcrypt_state_private *) slapi_ch_calloc(sizeof(attrcrypt_cipher_state *), 2); /* 2 == The pointer and a NULL terminator */
	} else {
		/* Otherwise re-size it */
		for (current = &(li->inst_attrcrypt_state_private->acs_array[0]); *current; current++) {
			list_size++;
		}
		li->inst_attrcrypt_state_private = (attrcrypt_state_private *) slapi_ch_realloc((char*)li->inst_attrcrypt_state_private,sizeof(attrcrypt_cipher_state *) * (list_size + 2));
		li->inst_attrcrypt_state_private->acs_array[list_size + 1] = NULL; 
	}
	li->inst_attrcrypt_state_private->acs_array[list_size] = acs;
}

int
attrcrypt_init(ldbm_instance *li)
{
	int ret = 0;
	attrcrypt_cipher_entry *ace = NULL;
	SECKEYPrivateKey *private_key = NULL;
	SECKEYPublicKey *public_key = NULL;
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_init\n", 0, 0, 0);
	if (slapd_security_library_is_initialized()) {
		/* In case the backend instance is restarted, 
		 * inst_attrcrypt_state_private in li could have memory containing
		 * private keys.  The private data should be cleaned up first. */
		attrcrypt_cleanup_private(li);
		/* Get the server's private key, which is used to unwrap the stored symmetric keys */
		ret = attrcrypt_fetch_private_key(&private_key);
		if (!ret) { 
			ret = attrcrypt_fetch_public_key(&public_key);
			if (!ret) {
				for (ace = attrcrypt_cipher_list; ace && ace->cipher_number && !ret; ace++) {
					/* Make a state object for this cipher */
					attrcrypt_cipher_state *acs = (attrcrypt_cipher_state *) slapi_ch_calloc(sizeof(attrcrypt_cipher_state),1);
					ret = attrcrypt_cipher_init(li, ace, private_key, public_key, acs);
					if (ret) {
						LDAPDebug(LDAP_DEBUG_ANY,"Failed to initialize cipher %s in attrcrypt_init\n", ace->cipher_display_name, 0, 0);
						if (!li->attrcrypt_configured) {
							/* if not using attrcrypt, just return success */
							ret = 0;
						}
						slapi_ch_free((void **)&acs);
					} else {
						/* Since we succeeded, add the acs to the backend instance list */
						attrcrypt_acs_list_add(li,acs);
						LDAPDebug(LDAP_DEBUG_TRACE,"Initialized cipher %s in attrcrypt_init\n", ace->cipher_display_name, 0, 0);
					}
					
				}
			}
			slapd_pk11_DestroyPublicKey(public_key);
			public_key = NULL;
		}
		slapd_pk11_DestroyPrivateKey(private_key);
		private_key = NULL;
	} else {
		if (li->attrcrypt_configured) {
			LDAPDebug(LDAP_DEBUG_ANY,"Warning: encryption is configured in backend %s, but because SSL is not enabled, database encryption is not available and the configuration will be overridden.\n", li->inst_name, 0, 0);
		}
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_init : %d\n", ret, 0, 0);
	return ret;
}

/*
 * Called by the config code when a new attribute is added,
 * to make sure that we already have the runtime state and key
 * stored for that cipher. If not, we attmept to make it.
 * If this function succeeds, then its ok to go on to use the
 * cipher.
 */
int attrcrypt_check_enable_cipher(attrcrypt_cipher_entry *ace)
{
	int ret = 0;
	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_check_enable_cipher\n", 0, 0, 0);
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_check_enable_cipher\n", 0, 0, 0);
	return ret;
}

int
attrcrypt_cleanup(attrcrypt_cipher_state *acs)
{
    LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_cleanup\n", 0, 0, 0);
    if (acs->key) {
		slapd_pk11_FreeSymKey(acs->key);
	}
    if (acs->slot) {
		slapd_pk11_FreeSlot(acs->slot);
	}
    LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_cleanup\n", 0, 0, 0);
	return 0;
}

/*
 * This function cleans up the inst_attrcrypt_state_private in each backend
 * instance.
 */
int
attrcrypt_cleanup_private(ldbm_instance *li)
{
	attrcrypt_cipher_state **current = NULL;

	LDAPDebug(LDAP_DEBUG_TRACE, "-> attrcrypt_cleanup_private\n", 0, 0, 0);
	if (li && li->inst_attrcrypt_state_private) {
		for (current = &(li->inst_attrcrypt_state_private->acs_array[0]);
			 *current; current++) {
			attrcrypt_cleanup(*current);
			slapi_ch_free((void **)current);
		}
		slapi_ch_free((void **)&li->inst_attrcrypt_state_private);
	}
	LDAPDebug(LDAP_DEBUG_TRACE, "<- attrcrypt_cleanup_private\n", 0, 0, 0);
	return 0;
}

static  attrcrypt_cipher_state *
attrcrypt_get_acs(backend *be, attrcrypt_private *priv)
{
	/* Walk the list of acs objects looking for the one for our cipher */
	int cipher = priv->attrcrypt_cipher;
	ldbm_instance *li = (ldbm_instance *) be->be_instance_info;
	attrcrypt_state_private* iasp = li->inst_attrcrypt_state_private;
	if (iasp) {
		attrcrypt_cipher_state **current = &(iasp->acs_array[0]);
		while (current) {
			if ((*current)->ace->cipher_number == cipher) {
				return *current;
			}
			current++;
		}
	}
	return NULL;
}

#if defined(DEBUG_ATTRCRYPT)
static void log_bytes(char* format_string, unsigned char *bytes, size_t length)
{
	size_t max_length = 40;
	size_t truncated_length = (length > max_length) ? max_length : length;
	size_t x = 0;
	char *print_buffer = NULL;
	char *print_ptr = NULL;

	print_buffer = (char*)slapi_ch_malloc((truncated_length * 3) + 1);
	print_ptr = print_buffer;

	for (x = 0; x < truncated_length; x++) {
		print_ptr += sprintf(print_ptr, "%02x ", bytes[x]);
	}

	LDAPDebug(LDAP_DEBUG_ANY,format_string, print_buffer, length, 0);

	slapi_ch_free((void**)&print_buffer);
}
#endif

/* Either encipher or decipher an attribute value */
static int
attrcrypt_crypto_op(attrcrypt_private *priv, backend *be, struct attrinfo *ai, char *in_data, size_t in_size, char **out_data, size_t *out_size, int encrypt)
{
	int ret = -1;
	SECStatus secret = 0;
	PK11Context* sec_context = NULL;
	SECItem iv_item = {0};
	SECItem *security_parameter = NULL;
	int output_buffer_length = 0;
	int output_buffer_size1 = 0;
	unsigned int output_buffer_size2 = 0;
	unsigned char *output_buffer = NULL;
	attrcrypt_cipher_state *acs = NULL;

	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_crypto_op\n", 0, 0, 0);
	acs = attrcrypt_get_acs(be,ai->ai_attrcrypt);
	if (NULL == acs) {
		/* This happens if SSL/NSS has not been enabled */
		return -1;
	}
#if defined(DEBUG_ATTRCRYPT)
	if (encrypt) {
		LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_crypto_op encrypt '%s' (%d)\n", in_data, in_size, 0);
	} else {
		log_bytes("attrcrypt_crypto_op decrypt '%s' (%d)\n", (unsigned char *)in_data, in_size);
	}
#endif
	/* Allocate the output buffer */
	output_buffer_length = in_size + 16;
	output_buffer = (unsigned char *)slapi_ch_malloc(output_buffer_length);
	/* Now call NSS to do the cipher op */
	iv_item.data = (unsigned char *)"aaaaaaaaaaaaaaaa"; /* ptr to an array of IV bytes */
	iv_item.len = acs->ace->iv_length; /* length of the array of IV bytes */
	security_parameter = slapd_pk11_ParamFromIV(acs->ace->cipher_mechanism, &iv_item);
	if (NULL == security_parameter) {
		int errorCode = PR_GetError();
		LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_crypto_op failed to make IV for cipher %s : %d - %s\n", acs->ace->cipher_display_name, errorCode, slapd_pr_strerror(errorCode));
		goto error;
	}
	sec_context = slapd_pk11_createContextBySymKey(acs->ace->cipher_mechanism, (encrypt ? CKA_ENCRYPT : CKA_DECRYPT), acs->key, security_parameter); 
	if (NULL == sec_context) {
		int errorCode = PR_GetError();
		LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_crypto_op failed on cipher %s : %d - %s\n", acs->ace->cipher_display_name, errorCode, slapd_pr_strerror(errorCode));
		goto error;
	}	
	secret = slapd_pk11_cipherOp(sec_context, output_buffer, &output_buffer_size1, output_buffer_length, (unsigned char *)in_data, in_size);
	if (SECSuccess != secret) {
		int errorCode = PR_GetError();
		LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_crypto_op failed on cipher %s : %d - %s\n", acs->ace->cipher_display_name, errorCode, slapd_pr_strerror(errorCode));
		goto error;
	}
#if defined(DEBUG_ATTRCRYPT)
	LDAPDebug(LDAP_DEBUG_ANY,"slapd_pk11_cipherOp %d\n", output_buffer_size1, 0, 0);
#endif
	secret = slapd_pk11_DigestFinal(sec_context, output_buffer + output_buffer_size1, &output_buffer_size2, output_buffer_length - output_buffer_size1);
	if (SECSuccess != secret) {
		int errorCode = PR_GetError();
		LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_crypto_op digest final failed on cipher %s : %d - %s\n", acs->ace->cipher_display_name, errorCode, slapd_pr_strerror(errorCode));
		goto error;
	} else {
#if defined(DEBUG_ATTRCRYPT)
		int recurse = 1;
		if (encrypt) {
			log_bytes("slapd_pk11_DigestFinal '%s' (%d)\n", output_buffer, output_buffer_size1 + output_buffer_size2);
		} else {
			LDAPDebug(LDAP_DEBUG_ANY,"slapd_pk11_DigestFinal '%s', %u\n", output_buffer, output_buffer_size2, 0);
		}
		if (*out_size == -1) {
			recurse = 0;
		}
#endif
		*out_size = output_buffer_size1 + output_buffer_size2;
		*out_data = (char *)output_buffer;
		ret = 0; /* success */
#if defined(DEBUG_ATTRCRYPT)
		if (recurse) {
			char *redo_data = NULL;
			size_t redo_size = -1;
			int redo_ret;

			LDAPDebug(LDAP_DEBUG_ANY,"------> check result of crypto op\n", 0, 0, 0);
			redo_ret = attrcrypt_crypto_op(priv, be, ai, *out_data, *out_size, &redo_data, &redo_size, !encrypt);
			slapi_log_error(SLAPI_LOG_FATAL, "DEBUG_ATTRCRYPT",
							"orig length %ld redone length %ld\n", in_size, redo_size);
			log_bytes("DEBUG_ATTRCRYPT orig bytes '%s' (%d)\n", (unsigned char *)in_data, in_size);
			log_bytes("DEBUG_ATTRCRYPT redo bytes '%s' (%d)\n", (unsigned char *)redo_data, redo_size);

			LDAPDebug(LDAP_DEBUG_ANY,"<------ check result of crypto op\n", 0, 0, 0);
		}
#endif
	}
error:
	if (sec_context) {
		slapd_pk11_DestroyContext(sec_context, PR_TRUE);
	}
	if (security_parameter) {
		slapd_SECITEM_FreeItem(security_parameter, PR_TRUE);
	}
	if (ret) {
		slapi_ch_free_string((char **)&output_buffer);
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_crypto_op\n", 0, 0, 0);
	return ret;
}

static int
attrcrypt_crypto_op_value(attrcrypt_private *priv, backend *be, struct attrinfo *ai, Slapi_Value *invalue, Slapi_Value **outvalue, int encrypt)
{
	int ret = 0;
	char *in_data = NULL;
	size_t in_size = 0;
	char *out_data = NULL;
	size_t out_size = 0;
	struct berval *bval = NULL;

	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_crypto_op_value\n", 0, 0, 0);
	
	bval = (struct berval *) slapi_value_get_berval(invalue);
	in_data = bval->bv_val;
	in_size = bval->bv_len;

	ret = attrcrypt_crypto_op(priv,be,ai,in_data,in_size,&out_data,&out_size,encrypt);

	if (0 == ret) {
		struct berval outbervalue = {0};
		outbervalue.bv_len = out_size;
		outbervalue.bv_val = out_data;
		/* This call makes a copy of the payload data, so we need to free the original data after making the call */
		*outvalue = slapi_value_new_berval(&outbervalue);
		slapi_ch_free((void**)&out_data);
	}

	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_crypto_op_value: %d\n", ret, 0, 0);
	return ret;
}

int
attrcrypt_crypto_op_value_replace(attrcrypt_private *priv, backend *be, struct attrinfo *ai, Slapi_Value *inoutvalue, int encrypt)
{
	int ret = 0;
	char *in_data = NULL;
	size_t in_size = 0;
	char *out_data = NULL;
	size_t out_size = 0;
	struct berval *bval = NULL;

	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_crypto_op_value_replace\n", 0, 0, 0);
	
	bval = (struct berval *) slapi_value_get_berval(inoutvalue);
	in_data = bval->bv_val;
	in_size = bval->bv_len;

	ret = attrcrypt_crypto_op(priv,be,ai,in_data,in_size,&out_data,&out_size,encrypt);

	if (0 == ret) {
		struct berval outbervalue = {0};
		outbervalue.bv_len = out_size;
		outbervalue.bv_val = out_data;
		/* This takes a copy of the payload, so we need to free it now */
		slapi_value_set_berval(inoutvalue,&outbervalue);
		slapi_ch_free((void**)&out_data);
	}

	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_crypto_op_value_replace: %d\n", ret, 0, 0);
	return ret;
}

static int
attrcrypt_crypto_op_values(attrcrypt_private *priv,  backend *be, struct attrinfo *ai, Slapi_Value	**invalues, Slapi_Value ***outvalues, int encrypt)
{
	int ret = 0;
	int i = 0;
	Slapi_Value **encrypted_values = NULL;

	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_crypto_op_values\n", 0, 0, 0);
	encrypted_values = (Slapi_Value **) slapi_ch_calloc(sizeof(Slapi_Value *),local_valuearray_count(invalues) + 1);
	for ( i = 0; (invalues[i] != NULL) && (ret == 0); i++ ) {
		Slapi_Value *encrypted_value = NULL;

		ret = attrcrypt_crypto_op_value(priv,be,ai,invalues[i],&encrypted_value,encrypt);
		if (0 == ret) {
			encrypted_values[i] = encrypted_value;	
		}
	}
	*outvalues = encrypted_values;
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_crypto_op_values: %d\n", ret, 0, 0);
	return ret;
}

static int
attrcrypt_crypto_op_values_replace(attrcrypt_private *priv,  backend *be, struct attrinfo *ai, Slapi_Value	**invalues, int encrypt)
{
	int ret = 0;
	int i = 0;

	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_crypto_op_values_replace\n", 0, 0, 0);
	for ( i = 0; (invalues[i] != NULL) && (ret == 0); i++ ) {

		ret = attrcrypt_crypto_op_value_replace(priv,be,ai,invalues[i],encrypt);
		if (ret) {
			break;
		}
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_crypto_op_values_replace\n", 0, 0, 0);
	return ret;
}

/* Modifies the entry in-place to decrypt any encrypted attributes */
int 
attrcrypt_decrypt_entry(backend *be, struct backentry *e)
{
	int ret = 0;
	int rc = 0;
	Slapi_Attr *attr = NULL;
	char *type = NULL;

	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_decrypt_entry\n", 0, 0, 0);
	/* Scan through the entry's attributes, looking to see if any are configured for crypto */
	for ( rc = slapi_entry_first_attr( e->ep_entry, &attr ); rc == 0 && attr ; rc = slapi_entry_next_attr( e->ep_entry, attr, &attr )) {

		struct attrinfo *ai = NULL;
		Slapi_Value	*value = NULL;
		int i = 0;

		slapi_attr_get_type( attr, &type );
		ainfo_get(be, type, &ai);

		if (ai && ai->ai_attrcrypt) {
			i = slapi_attr_first_value(attr,&value);
			while (NULL != value && i != -1)
			{
				/* Now decrypt the attribute values in place on the original entry */
				ret = attrcrypt_crypto_op_value_replace(ai->ai_attrcrypt,be,ai,value,0);
				if (ret) {
					LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_decrypt_entry: FAILING because decryption operation failed\n", 0, 0, 0);
					return ret;
				}
				i = slapi_attr_next_value(attr,i,&value);
			} 
			/* Now do the same thing with deleted values */
			i = attr_first_deleted_value(attr,&value);
			while (NULL != value && i != -1)
			{
				/* Now decrypt the attribute values in place on the original entry */
				ret = attrcrypt_crypto_op_value_replace(ai->ai_attrcrypt,be,ai,value,0);
				if (ret) {
					LDAPDebug(LDAP_DEBUG_ANY,"attrcrypt_decrypt_entry: FAILING because decryption operation failed\n", 0, 0, 0);
					return ret;
				}
				i = attr_next_deleted_value(attr,i,&value);
			} 
		}
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_decrypt_entry\n", 0, 0, 0);
	return ret;
}

/* Encrypts attributes on this entry in-place (only changes the attribute data, nothing else)
 */
int
attrcrypt_encrypt_entry_inplace(backend *be, const struct backentry *inout)
{
	int ret = 0;
	int rc = 0;
	char *type = NULL;
	Slapi_Attr *attr = NULL;
	Slapi_Value	**svals = NULL;

	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_encrypt_entry_inplace\n", 0, 0, 0);
	/* Scan the entry's attributes looking for any that are configured for encryption */
	for ( rc = slapi_entry_first_attr( inout->ep_entry, &attr ); rc == 0;
		rc = slapi_entry_next_attr( inout->ep_entry, attr, &attr ) ) {

		struct attrinfo *ai = NULL;

		slapi_attr_get_type( attr, &type );

		ainfo_get(be, type, &ai);

		if (ai && ai->ai_attrcrypt) {
			svals = attr_get_present_values(attr);
			if (svals) {
				/* Now encrypt the attribute values in place on the new entry */
				ret = attrcrypt_crypto_op_values_replace(ai->ai_attrcrypt,be,ai,svals,1);
			}
		}
	}
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_encrypt_entry_inplace\n", 0, 0, 0);
	return ret;
}

/* Makes a copy of the entry that has all necessary attributes encrypted 
 * as a performance optimization, if there are no attributes configured
 * for encryption in the entry, then no copy is returned.
 */
int
attrcrypt_encrypt_entry(backend *be, const struct backentry *in, struct backentry **out)
{
	int ret = 0;
	int rc = 0;
	struct backentry *new_entry = NULL;
	char *type = NULL;
	Slapi_Attr *attr = NULL;

	LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_encrypt_entry\n", 0, 0, 0);
	*out = NULL;
	/* Scan the entry's attributes looking for any that are configured for encryption */
	for ( rc = slapi_entry_first_attr( in->ep_entry, &attr ); rc == 0;
		rc = slapi_entry_next_attr( in->ep_entry, attr, &attr ) ) {

		struct attrinfo *ai = NULL;

		slapi_attr_get_type( attr, &type );

		ainfo_get(be, type, &ai);

		if (ai && ai->ai_attrcrypt) {
			Slapi_Value	**svals = attr_get_present_values(attr);
			if (svals) {
				Slapi_Value **new_vals = NULL;
				/* If we find one, did we make the new entry yet ? */
				if (NULL == new_entry) {
					/* If not then make it now as a copy of the old entry */
					new_entry = backentry_dup((struct backentry *)in);
				}
				/* Now encrypt the attribute values in place on the new entry */
				ret = attrcrypt_crypto_op_values(ai->ai_attrcrypt,be,ai,svals,&new_vals,1);
				if (ret) {
					LDAPDebug(LDAP_DEBUG_ANY,"Error: attrcrypt_crypto_op_values failed in attrcrypt_encrypt_entry\n", 0, 0, 0);
					break;
				}
				/* DBDB does this call free the old value memory ? */
				/* yes, DBDB, but it does not free new_vals - new_vals is copied */
				slapi_entry_attr_replace_sv(new_entry->ep_entry, type, new_vals);
				valuearray_free(&new_vals);
			}
		}
	}
	*out = new_entry;
	LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_encrypt_entry\n", 0, 0, 0);
	return ret;
}

/* 
 * Encrypt an index key. There is never any need to decrypt index keys since
 * we only ever look them up using plain text (except entryrdn).
 */
int
attrcrypt_encrypt_index_key(backend *be, struct attrinfo *ai, const struct berval *in, struct berval **out)
{
	int ret = 0;
	char *in_data = in->bv_val;
	size_t in_size = in->bv_len;
	char *out_data = NULL;
	size_t out_size = 0;
	struct berval *out_berval = NULL;

	if (ai->ai_attrcrypt) {
		LDAPDebug(LDAP_DEBUG_TRACE,"-> attrcrypt_encrypt_index_key\n", 0, 0, 0);
		ret = attrcrypt_crypto_op(ai->ai_attrcrypt,be,ai, in_data,in_size,&out_data,&out_size, 1);
		if (0 == ret) {
			out_berval = (struct berval *)ber_alloc();
			if (NULL == out_berval) {
				return ENOMEM;
			}
			out_berval->bv_len = out_size;
			/* Because we're making a new berval, we copy the payload pointer in */
			/* It's now the responsibility of our caller to free that data */
			out_berval->bv_val = out_data;
			*out = out_berval;
		}
		LDAPDebug(LDAP_DEBUG_TRACE,"<- attrcrypt_encrypt_index_key\n", 0, 0, 0);
	}
	return ret;
}
	
/*
 * Decrypt index key
 * needed by entryrdn (subtree-rename)
 */ 
int
attrcrypt_decrypt_index_key(backend *be, 
							struct attrinfo *ai, 
							const struct berval *in,
							struct berval **out)
{
	int rc = 0; /* success */

	if (ai->ai_attrcrypt) {
		Slapi_Value *value = NULL;
		rc = -1;
		if (NULL == in || NULL == out) {
			LDAPDebug1Arg(LDAP_DEBUG_ANY,
						  "attrcrypt_decrypt_index_key: Empty %s\n",
						  NULL==in?"in":NULL==out?"out":"unknown");
			return rc;
		}
		value = slapi_value_new_berval(in);
		LDAPDebug0Args(LDAP_DEBUG_TRACE,"-> attrcrypt_decrypt_index_key\n");
		/* Decrypt the input values in place on the original entry */
		rc = attrcrypt_crypto_op_value_replace(ai->ai_attrcrypt, be, ai,
												value, 0 /* decrypt */);
		if (0 == rc) {
			const struct berval *out_bv =
							slapi_value_get_berval((const Slapi_Value *)value);
			if (NULL == out_bv) {
				rc = -1;
				goto bail;
			}
			(*out) = ber_bvdup(out_bv);
			if (NULL == *out) {
				rc = -1;
			}
		}
bail:
		LDAPDebug0Args(LDAP_DEBUG_TRACE,"<- attrcrypt_decrypt_index_key\n");
		slapi_value_free(&value);
	}

	return rc;
}
