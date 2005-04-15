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
/* from /usr/project/iplanet/ws/ds5.ke/ns/svrcore/pkcs7/tstarchive.c */

#include <string.h>
#include <stdio.h>

#include <ldap.h>
#include <nspr.h> 
#include <nss.h> 
#include <secmod.h>
/*
#include <secasn1.h>
#include <secpkcs7.h>
*/
#include <key.h>
#include <certdb.h>
#include <cert.h>
#include <svrcore.h>
#include <secmodt.h>
#include <prtypes.h>
#include <seccomon.h>
#include <pk11func.h>

#define NEED_TOK_DES /* see slap.h - defines tokDes and ptokDes */
#include "rever.h"
#include <slap.h>
#include "slapi-plugin.h"
#include <uuid.h>


struct pk11MechItem
{
  CK_MECHANISM_TYPE type;
  const char *mechName;
};
static const struct pk11MechItem mymech = { CKM_DES_CBC, "DES CBC encryption" };


static Slapi_Mutex *mylock = NULL;

struct pk11ContextStore
{
  PK11SlotInfo *slot;
  const struct pk11MechItem *mech;
 
  PK11SymKey *key;
  SECItem *params;
 
  int length;
  unsigned char *crypt;
};

static int encode_path(char *inPlain, char **outCipher, char *path);
static int decode_path(char *inCipher, char **outPlain, char *path);
static SVRCOREError genKey(struct pk11ContextStore **out, const char *token, char *path);
static SVRCOREError cryptPassword(struct pk11ContextStore *store, char * clear, unsigned char **out);
static SVRCOREError decryptPassword(struct pk11ContextStore *store, unsigned char *cipher, char **out, int len);
static void freeDes(struct pk11ContextStore *out);

void
init_des_plugin()
{
	mylock = slapi_new_mutex();
}

int
encode(char *inPlain, char **outCipher)
{
	return encode_path(inPlain, outCipher, NULL);
}

static int
encode_path(char *inPlain, char **outCipher, char *path)
{
	struct pk11ContextStore *context = NULL;
	int err;

	unsigned char *cipher = NULL;
	char *tmp = NULL;
	char *base = NULL;


	*outCipher = NULL;
	err = 1;

	if ( genKey(&context, tokDes, path) == SVRCORE_Success )
	{
		/* Try an encryption */
		if ( cryptPassword(context, inPlain, &cipher) == SVRCORE_Success )
		{
			base = BTOA_DataToAscii(cipher, context->length);
			if ( base != NULL )
			{	
				tmp = slapi_ch_malloc( 3 + strlen(REVER_SCHEME_NAME) + strlen(base));
				if ( tmp != NULL )
				{
					sprintf( tmp, "%c%s%c%s", PWD_HASH_PREFIX_START, REVER_SCHEME_NAME, PWD_HASH_PREFIX_END, base);
					*outCipher = tmp;
					tmp = NULL;
					err = 0;
				}
				PORT_Free(base);
			}
		}
	}
	
	freeDes(context);
	slapi_ch_free((void **) &context);
	return(err);
}

int
decode(char *inCipher, char **outPlain)
{
	return decode_path(inCipher, outPlain, NULL);
}


static int
decode_path(char *inCipher, char **outPlain, char *path)
{
	struct pk11ContextStore *context = NULL;
	char *plain= NULL;
	int err;

	unsigned char *base = NULL;
	int len = 0;


	*outPlain = NULL;
	err = 1;

	if ( genKey(&context, tokDes, path) == SVRCORE_Success )
	{
		/* it seems that there is memory leak in that function: bug 400170 */

		base = ATOB_AsciiToData(inCipher, (unsigned int*)&len);
		if ( base != NULL )
		{
			if ( decryptPassword(context, base, &plain, len) == SVRCORE_Success )
			{
				*outPlain = plain;
				err = 0;
			}
		}
	}

	PORT_Free(base);
	freeDes(context);
	slapi_ch_free((void **) &context);
	return(err);
}

static void freeDes(struct pk11ContextStore *out)
{
	if (out)
	{
		if (out->slot) 
			slapd_pk11_freeSlot(out->slot);
		if (out->key) 
			slapd_pk11_freeSymKey(out->key);
		if (out->params) 
			SECITEM_FreeItem(out->params,PR_TRUE);
		if (out->crypt)
			free(out->crypt);
	}
}

static SVRCOREError genKey(struct pk11ContextStore **out, const char *token, char *path)
{
	SVRCOREError err = SVRCORE_Success;
	struct pk11ContextStore *store = NULL;
	SECItem *pwitem = NULL;
	SECItem *result = NULL;
	SECAlgorithmID *algid = NULL;
	SECOidTag algoid;
	SECItem *salt = NULL;
	CK_MECHANISM pbeMech;
	CK_MECHANISM cryptoMech;

	char *instancedir = NULL;
	char *iv = NULL;

	store = (struct pk11ContextStore*)slapi_ch_malloc(sizeof(*store));
	if (store == NULL) 
	{
		return (err = SVRCORE_NoMemory_Error);
	}
	*out = store;

	/* Low-level init */
	store->slot = NULL;
	store->key = NULL;
	store->params = NULL;
	store->crypt = NULL;

	/* Use the tokenName to find a PKCS11 slot */
	store->slot = slapd_pk11_findSlotByName((char *)token);
	if (store->slot == NULL) 
	{
		return (err = SVRCORE_NoSuchToken_Error);
	}

	/* Generate a key and parameters to do the encryption */
	store->mech = &mymech;

	/* Generate a unique id, used as salt for the key generation */
	if ( path == NULL )
	{
		instancedir = config_get_instancedir();
		if ( instancedir == NULL )
		{
		  return (err = SVRCORE_System_Error);
		}
	}
	else
	{
		instancedir = slapi_ch_strdup(path);
	}
	if ( slapi_uniqueIDGenerateFromNameString (&iv, NULL, instancedir, strlen(instancedir)) != UID_SUCCESS )
	{
	  slapi_ch_free((void**)&instancedir);
	  return (err = SVRCORE_System_Error);
	}
	slapi_ch_free((void**)&instancedir);

	pwitem = (SECItem *) PORT_Alloc(sizeof(SECItem));
	if (pwitem == NULL)
	{
		return (err = SVRCORE_NoMemory_Error);
	}
	pwitem->type = siBuffer;
	pwitem->data = (unsigned char *)PORT_Alloc(strlen(iv)+1);
	if (pwitem->data == NULL)
	{
		return (err = SVRCORE_NoMemory_Error);
	}
	strcpy((char*)pwitem->data, iv);		
	pwitem->len = strlen(iv) + 1;

	algoid = SEC_OID_PKCS5_PBE_WITH_MD2_AND_DES_CBC;

	salt = (SECItem *) PORT_Alloc(sizeof(SECItem));
	if (salt == NULL)
	{
		return (err = SVRCORE_NoMemory_Error);
	}
	salt->type = siBuffer;
	salt->data = (unsigned char *)PORT_Alloc(strlen(iv)+1);
	if ( salt->data == NULL )
	{
		return (err = SVRCORE_NoMemory_Error);
	}
	strcpy((char*)salt->data, iv);		
	salt->len = strlen(iv) + 1;
	slapi_ch_free((void**)&iv);

	algid = slapd_pk11_createPBEAlgorithmID(algoid, 2, salt);

	slapi_lock_mutex(mylock);
	store->key = slapd_pk11_pbeKeyGen(store->slot, algid, pwitem, 0, 0);
	if (store->key == 0)
	{
      slapi_unlock_mutex(mylock);
	  return (err = SVRCORE_System_Error);
	}

	slapi_unlock_mutex(mylock);
    pbeMech.mechanism = slapd_pk11_algtagToMechanism(algoid);
    result = slapd_pk11_paramFromAlgid(algid);
    secoid_destroyAlgorithmID(algid, PR_TRUE);
    pbeMech.pParameter = result->data;
    pbeMech.ulParameterLen = result->len;
    if(slapd_pk11_mapPBEMechanismToCryptoMechanism(&pbeMech, &cryptoMech, pwitem,
                        PR_FALSE) != CKR_OK) 
	{
		SECITEM_FreeItem(result, PR_TRUE); 
        return (err = SVRCORE_System_Error);
    }
	SECITEM_FreeItem(result, PR_TRUE);
	SECITEM_FreeItem(pwitem, PR_TRUE);
	SECITEM_FreeItem(salt, PR_TRUE);
	store->params = (SECItem *) PORT_Alloc(sizeof(SECItem));
    if (store->params == NULL) 
	{
	  return (err = SVRCORE_System_Error);
	}
    store->params->type = store->mech->type;
    store->params->data = (unsigned char *)PORT_Alloc(cryptoMech.ulParameterLen);
    if (store->params->data == NULL) 
	{
	  return (err = SVRCORE_System_Error);
	}
    memcpy(store->params->data, (unsigned char *)cryptoMech.pParameter, cryptoMech.ulParameterLen);
    store->params->len = cryptoMech.ulParameterLen;
	PORT_Free(cryptoMech.pParameter);
	return (err);
}

static SVRCOREError decryptPassword(struct pk11ContextStore *store, unsigned char *cipher, char **out, int len)
{
	SVRCOREError err = SVRCORE_Success;
	unsigned char *plain = NULL;
	unsigned char *cipher_with_padding = NULL;
	SECStatus rv;
	PK11Context *ctx = 0;
	int outLen = 0;
	int blocksize = 0;

	blocksize = slapd_pk11_getBlockSize(store->mech->type, 0);
	store->length = len;

	/* store->length is the max. length of the returned clear text - 
	   must be >= length of crypted bytes - also must be a multiple
	   of blocksize */
	if (blocksize != 0)
	{
		store->length += blocksize - (store->length % blocksize);
	}

	/* plain will hold the returned clear text */
	plain = (unsigned char *)slapi_ch_calloc(sizeof(unsigned char),
											 store->length+1);
	if (!plain) 
	{
		return (err = SVRCORE_NoMemory_Error);
	}

	/* create a buffer holding the original cipher bytes, padded with
	   zeros to a multiple of blocksize - do not need +1 since buffer is not
	   a string */
	cipher_with_padding = (unsigned char *)slapi_ch_calloc(sizeof(unsigned char),
														   store->length);
	if (!cipher_with_padding)
	{
		return (err = SVRCORE_NoMemory_Error);
	}
	memcpy(cipher_with_padding, cipher, len);

	ctx = slapd_pk11_createContextBySymKey(store->mech->type, CKA_DECRYPT,
	  store->key, store->params);
	if (!ctx) 
	{
		return (err = SVRCORE_System_Error);
	}

	/* warning - there is a purify UMR in the NSS des code - you may see it when the
	   password is not a multiple of 8 bytes long */
	rv = slapd_pk11_cipherOp(ctx, plain, &outLen, store->length,
			cipher_with_padding, store->length);
	if (rv)
	{
		err = SVRCORE_System_Error;
	}

	rv = slapd_pk11_finalize(ctx);
	/* we must do the finalize, but we only want to set the err return
	   code if it is not already set */
	if (rv && (SVRCORE_Success == err))
		err = SVRCORE_System_Error;

	if (err == SVRCORE_Success)
		*out = (char *)plain;

	slapi_ch_free((void **)&cipher_with_padding);
	/* We should free the PK11Context... Something like : */
	slapd_pk11_destroyContext(ctx, PR_TRUE);
	return err;
}

static SVRCOREError cryptPassword(struct pk11ContextStore *store, char * clear, unsigned char **out)
{
	SVRCOREError err = SVRCORE_Success;
	SECStatus rv;
	PK11Context *ctx = 0;
	int outLen = 0;
	int blocksize = 0;
	unsigned char *clear_with_padding = NULL; /* clear with padding up to blocksize */

	blocksize = slapd_pk11_getBlockSize(store->mech->type, 0);
	store->length = strlen(clear);

	/* the size of the clear text buffer passed to the des encryption functions
	   must be a multiple of blocksize (usually 8 bytes) - we allocate a buffer
	   of this size, copy the clear text password into it, and pad the rest with
	   zeros */
	if (blocksize != 0)
	{
		store->length += blocksize - (store->length % blocksize);
	}

	/* store->crypt will hold the crypted password - it must be >= clear length */
	store->crypt = (unsigned char *)slapi_ch_calloc(sizeof(unsigned char),
													store->length+1);
	if (!store->crypt) 
	{
		return (err = SVRCORE_NoMemory_Error);
	}

	/* create a buffer big enough to hold the clear text password and padding */
	clear_with_padding = (unsigned char *)slapi_ch_calloc(sizeof(unsigned char),
														  store->length+1);
	if (!clear_with_padding)
	{
		return (err = SVRCORE_NoMemory_Error);
	}
	/* copy the clear text password into the buffer - the calloc insures the
	   remainder is zero padded */
	strcpy((char *)clear_with_padding, clear);

	ctx = slapd_pk11_createContextBySymKey(store->mech->type, CKA_ENCRYPT,
	  store->key, store->params);
	if (!ctx) 
	{
		return (err = SVRCORE_System_Error);
	}

	rv = slapd_pk11_cipherOp(ctx, store->crypt, &outLen, store->length,
			clear_with_padding, store->length);
	if (rv)
	{
		err = SVRCORE_System_Error;
	}

	rv = slapd_pk11_finalize(ctx);
	/* we must do the finalize, but we only want to set the err return
	   code if it is not already set */
	if (rv && (SVRCORE_Success == err))
		err = SVRCORE_System_Error;

	if (err == SVRCORE_Success)
		*out = store->crypt;

	slapi_ch_free((void **)&clear_with_padding);
	/* We should free the PK11Context... Something like : */
	slapd_pk11_destroyContext(ctx, PR_TRUE);
	return err;
}

char *
migrateCredentials(char *oldpath, char *newpath, char *oldcred)
{
	char *plain = NULL;
	char *cipher = NULL;

	init_des_plugin();

	slapd_pk11_configurePKCS11(NULL, NULL, tokDes, ptokDes, NULL, NULL, NULL, NULL, 0, 0 );	
	NSS_NoDB_Init(NULL);

	if ( decode_path(oldcred, &plain, oldpath) == 0 )
	{
		if ( encode_path(plain, &cipher, newpath) != 0 )
			return(NULL);
		else
			return(cipher);
	}
	else
		return(NULL);
}
