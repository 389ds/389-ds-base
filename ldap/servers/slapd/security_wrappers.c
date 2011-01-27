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

#include <ssl.h>
#include <nss.h>
#include <key.h>
#include <certdb.h>
#include <cert.h>
#include <sslproto.h>
#include <secoid.h>
#include <secmod.h>
#include <secmodt.h>
#include <prtypes.h>
#include <seccomon.h>
#include <pkcs11.h>
#include <pk11func.h>

#include "slap.h"



/*
 * Set of security wrapper functions, aimed at forcing every security-related call
 * pass through libslapd library.
 * Thus, we avoid finding any ambiguity or data inconsistency (which we would, 
 * otherwise, have to face on NT. 
 */


/*
 * Note: slapd_ssl_handshakeCallback() is called with a Connection * in
 * client_data. That connection must have its c_mutex locked.
 */
int
slapd_ssl_handshakeCallback(PRFileDesc *fd, void * callback, void * client_data)
{
	return SSL_HandshakeCallback(fd, (SSLHandshakeCallback) callback, client_data);
}


/*
 * Note: slapd_ssl_badCertHook() is called with a Connection * in
 * client_data. That connection must have its c_mutex locked.
 */
int
slapd_ssl_badCertHook(PRFileDesc *fd, void * callback, void * client_data)
{
	return SSL_BadCertHook(fd, (SSLBadCertHandler) callback, client_data);
}


CERTCertificate *
slapd_ssl_peerCertificate(PRFileDesc *fd)
{
	return SSL_PeerCertificate(fd);
}


SECStatus
slapd_ssl_getChannelInfo(PRFileDesc *fd, SSLChannelInfo *sinfo, PRUintn len)
{
	return SSL_GetChannelInfo(fd, sinfo, len);
}


SECStatus
slapd_ssl_getCipherSuiteInfo(PRUint16 ciphersuite, SSLCipherSuiteInfo *cinfo, PRUintn len)
{
	return SSL_GetCipherSuiteInfo(ciphersuite, cinfo, len);
}

PRFileDesc *
slapd_ssl_importFD(PRFileDesc *model, PRFileDesc *fd)
{
	return SSL_ImportFD(model, fd);
}


SECStatus
slapd_ssl_resetHandshake(PRFileDesc *fd, PRBool asServer)
{
	return SSL_ResetHandshake(fd, asServer);
}


void
slapd_pk11_configurePKCS11(char *man, char *libdes, char *tokdes, char *ptokdes,
			   char *slotdes, char *pslotdes, char *fslotdes, 
			   char *fpslotdes, int minPwd,
			   int pwdRequired)
{
        PK11_ConfigurePKCS11(man, libdes, tokdes, ptokdes,
			     slotdes, pslotdes, fslotdes, fpslotdes, minPwd,
			     pwdRequired);
	return;
}


void
slapd_pk11_freeSlot(PK11SlotInfo *slot)
{
         PK11_FreeSlot(slot);
	 return;
}


void
slapd_pk11_freeSymKey(PK11SymKey *key)
{
         PK11_FreeSymKey(key);
	 return;
}


PK11SlotInfo *
slapd_pk11_findSlotByName(char *name)
{
         return PK11_FindSlotByName(name);
}


SECAlgorithmID *
slapd_pk11_createPBEAlgorithmID(SECOidTag algorithm, int iteration, SECItem *salt)
{
         return PK11_CreatePBEAlgorithmID(algorithm, iteration, salt);
}


PK11SymKey *
slapd_pk11_pbeKeyGen(PK11SlotInfo *slot, SECAlgorithmID *algid,  SECItem *pwitem,
		     PRBool faulty3DES, void *wincx)
{
         return PK11_PBEKeyGen(slot, algid, pwitem,
			       faulty3DES, wincx);
}


CK_MECHANISM_TYPE
slapd_pk11_algtagToMechanism(SECOidTag algTag)
{
         return PK11_AlgtagToMechanism(algTag);
}


SECItem *
slapd_pk11_paramFromAlgid(SECAlgorithmID *algid)
{
         return PK11_ParamFromAlgid(algid);
}


CK_RV
slapd_pk11_mapPBEMechanismToCryptoMechanism(CK_MECHANISM_PTR pPBEMechanism,
					    CK_MECHANISM_PTR pCryptoMechanism,
					    SECItem *pbe_pwd, PRBool bad3DES)
{
         return PK11_MapPBEMechanismToCryptoMechanism(pPBEMechanism,
						      pCryptoMechanism,
						      pbe_pwd, bad3DES);
}


int
slapd_pk11_getBlockSize(CK_MECHANISM_TYPE type,SECItem *params)
{
         return PK11_GetBlockSize(type,params);
}


PK11Context *
slapd_pk11_createContextBySymKey(CK_MECHANISM_TYPE type,
				 CK_ATTRIBUTE_TYPE operation, 
				 PK11SymKey *symKey, SECItem *param)
{
         return PK11_CreateContextBySymKey(type,
					   operation, symKey, param);
}


SECStatus
slapd_pk11_cipherOp(PK11Context *context, unsigned char * out, int *outlen,
		    int maxout, unsigned char *in, int inlen)
{
         return PK11_CipherOp(context, out, outlen, maxout, in, inlen);
}


SECStatus
slapd_pk11_finalize(PK11Context *context)
{
         return PK11_Finalize(context);
}


PK11SlotInfo *
slapd_pk11_getInternalKeySlot()
{
         return PK11_GetInternalKeySlot();
}


PK11SlotInfo *
slapd_pk11_getInternalSlot()
{
         return PK11_GetInternalSlot();
}


SECStatus
slapd_pk11_authenticate(PK11SlotInfo *slot, PRBool loadCerts, void *wincx)
{
         return PK11_Authenticate(slot, loadCerts, wincx);
}


void
slapd_pk11_setSlotPWValues(PK11SlotInfo *slot,int askpw, int timeout)
{
         PK11_SetSlotPWValues(slot, askpw, timeout);
	 return;
}


PRBool
slapd_pk11_isFIPS()
{
         return PK11_IsFIPS();
}


CERTCertificate *
slapd_pk11_findCertFromNickname(char *nickname, void *wincx)
{
         return PK11_FindCertFromNickname(nickname, wincx);
}


SECKEYPrivateKey *
slapd_pk11_findKeyByAnyCert(CERTCertificate *cert, void *wincx)
{
         return PK11_FindKeyByAnyCert(cert, wincx);
}


PRBool
slapd_pk11_fortezzaHasKEA(CERTCertificate *cert)
{
         return PK11_FortezzaHasKEA(cert);
}

void
slapd_pk11_destroyContext(PK11Context *context, PRBool freeit)
{
	PK11_DestroyContext(context, freeit);
}

void secoid_destroyAlgorithmID(SECAlgorithmID *algid, PRBool freeit)
{
	SECOID_DestroyAlgorithmID(algid, freeit);
}

void slapd_pk11_CERT_DestroyCertificate(CERTCertificate *cert)
{
	CERT_DestroyCertificate(cert);
}

SECKEYPublicKey *slapd_CERT_ExtractPublicKey(CERTCertificate *cert)
{
	return CERT_ExtractPublicKey(cert);
}

SECKEYPrivateKey * slapd_pk11_FindPrivateKeyFromCert(PK11SlotInfo *slot,CERTCertificate *cert, void *wincx)
{
	return PK11_FindPrivateKeyFromCert(slot,cert,wincx);
}

PK11SlotInfo *slapd_pk11_GetInternalKeySlot(void)
{
	return PK11_GetInternalKeySlot();
}

SECStatus slapd_pk11_PubWrapSymKey(CK_MECHANISM_TYPE type, SECKEYPublicKey *pubKey,PK11SymKey *symKey, SECItem *wrappedKey)
{
	return PK11_PubWrapSymKey(type,pubKey,symKey,wrappedKey);
}

PK11SymKey *slapd_pk11_KeyGen(PK11SlotInfo *slot,CK_MECHANISM_TYPE type,SECItem *param, int keySize,void *wincx)
{
	return PK11_KeyGen(slot,type,param,keySize,wincx);
}

void slapd_pk11_FreeSlot(PK11SlotInfo *slot)
{
	PK11_FreeSlot(slot);
}

void slapd_pk11_FreeSymKey(PK11SymKey *key)
{
	PK11_FreeSymKey(key);
}

SECItem *slapd_pk11_ParamFromIV(CK_MECHANISM_TYPE type,SECItem *iv)
{
	return PK11_ParamFromIV(type,iv);
}

PK11SymKey *slapd_pk11_PubUnwrapSymKey(SECKEYPrivateKey *wrappingKey, SECItem *wrappedKey,CK_MECHANISM_TYPE target, CK_ATTRIBUTE_TYPE operation, int keySize)
{
	return PK11_PubUnwrapSymKey(wrappingKey,wrappedKey,target,operation,keySize);
}

unsigned slapd_SECKEY_PublicKeyStrength(SECKEYPublicKey *pubk)
{
	return SECKEY_PublicKeyStrength(pubk);
}

SECStatus slapd_pk11_Finalize(PK11Context *context)
{
	return PK11_Finalize(context);
}

SECStatus slapd_pk11_DigestFinal(PK11Context *context, unsigned char *data,unsigned int *outLen, unsigned int length)
{
	return PK11_DigestFinal(context, data, outLen, length);
}

void
slapd_SECITEM_FreeItem (SECItem *zap, PRBool freeit)
{
	SECITEM_FreeItem(zap,freeit);
}

void slapd_pk11_DestroyPrivateKey(SECKEYPrivateKey *key)
{
	SECKEY_DestroyPrivateKey(key);
}

void slapd_pk11_DestroyPublicKey(SECKEYPublicKey *key)
{
	SECKEY_DestroyPublicKey(key);
}

PRBool slapd_pk11_DoesMechanism(PK11SlotInfo *slot, CK_MECHANISM_TYPE type)
{
	return PK11_DoesMechanism(slot, type);
}

PK11SymKey *slapd_pk11_PubUnwrapSymKeyWithFlagsPerm(
                                        SECKEYPrivateKey *wrappingKey,
                                        SECItem *wrappedKey,
                                        CK_MECHANISM_TYPE target,
                                        CK_ATTRIBUTE_TYPE operation,
                                        int keySize,
                                        CK_FLAGS flags, 
                                        PRBool isPerm)
{
	return PK11_PubUnwrapSymKeyWithFlagsPerm(wrappingKey, wrappedKey, target,
	                                         operation, keySize, flags, isPerm);
}

PK11SymKey *slapd_pk11_TokenKeyGenWithFlags(PK11SlotInfo *slot,
                                            CK_MECHANISM_TYPE type,
                                            SECItem *param,
                                            int keySize,
                                            SECItem *keyid,
                                            CK_FLAGS opFlags,
                                            PK11AttrFlags attrFlags,
                                            void *wincx)
{
	return PK11_TokenKeyGenWithFlags(slot, type, param, keySize, keyid, 
	                                 opFlags, attrFlags, wincx);
}
