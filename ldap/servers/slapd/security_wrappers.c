/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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

void slapd_pk11_DestroyContext(PK11Context *context, PRBool freeit)
{
	PK11_DestroyContext(context,freeit);
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

