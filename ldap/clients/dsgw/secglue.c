/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright © 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright © 2001 Netscape Communications Corp.
 * All rights reserved.
 */
/*
 * secglue.c: Glue routines for the httpd.so shared object. These are 
 * necessary because on many system no garbage collection is performed for
 * shared objects.
 * 
 * Rob McCool
 *
 * Adapted for DSGW by Mark Smith 18 Aug 1997.
 * Copied from revision 1.4.4.6.4.1 of ldapserver/httpd/newadmin/src/secglue.c
 */


#include "base/systems.h"

#ifdef __cplusplus
#define FUNC(name) extern "C" { void name (void) {} }
#else
#define FUNC(name) void name (void) {}
#endif

FUNC(DS_Alloc)
FUNC(DS_Free)
FUNC(DS_Zfree)
FUNC(SEC_CertChainFromCert)
FUNC(SEC_CertTimesValid)
FUNC(SEC_CheckPassword)
FUNC(SEC_CloseKeyDB)
FUNC(SEC_CompareItem)
FUNC(SEC_ConvertToPublicKey)
FUNC(CERT_DestroyCertificate)
FUNC(SECKEY_DestroyPrivateKey)
FUNC(SEC_DestroyPublicKey)
FUNC(SECITEM_DupItem)
FUNC(SEC_ExtractPublicKey)
FUNC(SEC_FindCertByNickname)
FUNC(SEC_FindKeyByName)
FUNC(SECITEM_FreeItem)
FUNC(CERT_GetAVATag)
FUNC(SEC_GetPassword)
FUNC(SEC_GetSSLCACerts)
FUNC(CERT_NameToAscii)
FUNC(SEC_OpenCertDB)
FUNC(CERT_RFC1485_EscapeAndQuote)
FUNC(SEC_ResetRandom)
FUNC(SEC_UTCTimeToAscii)
FUNC(SECKEY_UpdateKeyDBPass1)
FUNC(SECKEY_UpdateKeyDBPass2)
FUNC(SSL_Accept)
FUNC(SSL_AcceptHook)
FUNC(SSL_AuthCertificate)
FUNC(SSL_AuthCertificateHook)
FUNC(SSL_BadCertHook)
FUNC(SSL_Bind)
FUNC(SSL_BindForSockd)
FUNC(SSL_CheckDirectSock)
FUNC(SSL_Close)
FUNC(SSL_ConfigSecureServer)
FUNC(SSL_ConfigServerSessionIDCache)
FUNC(SSL_ConfigSockd)
FUNC(SSL_Connect)
FUNC(SSL_DataPending)
FUNC(SSL_DataPendingHack)
FUNC(SSL_Enable)
FUNC(SSL_EnableCipher)
FUNC(SSL_EnableDefault)
FUNC(SSL_ForceHandshake)
FUNC(SSL_GetClientAuthDataHook)
FUNC(SSL_GetPeerName)
FUNC(SSL_GetSessionID)
FUNC(SSL_GetSockOpt)
FUNC(SSL_HandshakeCallback)
FUNC(SSL_Import)
FUNC(SSL_ImportFd)
FUNC(SSL_InvalidateSession)
FUNC(SSL_Ioctl)
FUNC(SSL_IsDomestic)
FUNC(SSL_Listen)
FUNC(SSL_PeerCertificate)
FUNC(SSL_Read)
FUNC(SSL_Recv)
FUNC(SSL_RedoHandshake)
FUNC(SSL_ResetHandshake)
FUNC(SSL_SecurityCapabilities)
FUNC(SSL_SecurityStatus)
FUNC(SSL_Send)
FUNC(SSL_SetSockOpt)
FUNC(SSL_SetURL)
FUNC(SSL_Shutdown)
FUNC(SSL_Socket)
FUNC(SSL_Write)
/*
 * DSGWmcs: added the functions below:
 */
FUNC(SEC_RNGInit)
FUNC(SEC_CheckKeyDBPassword)
FUNC(SEC_ZfreeItem)
FUNC(SEC_DataToAscii)
FUNC(SEC_AsciiToData)
FUNC(ldapssl_init)		/* called by something in ns-httpd.so */
FUNC(SSL_DefaultBadCertHandler)	/* called by something in ns-httpd.so */
/* DSGW kristian added: */
FUNC(CERT_GetDomainComponentName)
FUNC(CERT_GetCertEmailAddress)
FUNC(CERT_GetCertUid)
FUNC(CERT_GetCommonName)
FUNC(CERT_GetCountryName)
FUNC(CERT_GetLocalityName)
FUNC(CERT_GetOrgName)
FUNC(CERT_GetStateName)
FUNC(CERT_IsExportVersion)
FUNC(CERT_PublicModulusLen)

#ifdef FORTEZZA
FUNC(SSL_EnableGroup)
FUNC(SEC_OpenVolatileCertDB)
FUNC(FortezzaConfigureServer)
FUNC(SSL_IsEnabledGroup)
#endif /* FORTEZZA */

/* DSGW pkennedy added, for HCL integration */
FUNC(BTOA_DataToAscii)
FUNC(ATOB_AsciiToData)
FUNC(SSL_ImportFD)
FUNC(PK11_FindKeyByAnyCert)
FUNC(PK11_GetTokenName)
FUNC(PK11_SetPasswordFunc)
FUNC(PK11_FindCertFromNickname)
FUNC(PK11_FortezzaHasKEA)
FUNC(PK11_ConfigurePKCS11)
FUNC(SSL_SetPolicy)
FUNC(CERT_VerifyCertNow)
FUNC(SSL_RevealURL)
FUNC(CERT_VerifyCertName)
FUNC(PORT_SetError)

/* DSGW richm added, for nss 2.8.x support */
FUNC(SSL_OptionSet)
FUNC(NSS_SetDomesticPolicy)

/* DSGW powers added, for NSS 3.4.x support*/
FUNC(NSS_NoDB_Init)
FUNC(NSS_Initialize)
FUNC(NSS_Init)
FUNC(PK11_GenerateRandom)
FUNC(PK11_GetInternalKeySlot)
FUNC(PK11_KeyGen)
FUNC(PK11_ImportSymKey)
FUNC(PK11_GenerateNewParam)
FUNC(PK11_CreateContextBySymKey)
FUNC(PK11_CipherOp)
FUNC(PK11_DigestFinal)
FUNC(PK11_Finalize)
FUNC(PK11_DestroyContext)
FUNC(PK11_FreeSlot)
FUNC(PK11_DigestBegin)
FUNC(PK11_FreeSymKey)
FUNC(PK11_DigestOp)
FUNC(PK11_CloneContext)
FUNC(PK11_HashBuf)
FUNC(PK11_CreateDigestContext)
FUNC(SECITEM_ZfreeItem)
FUNC(SSL_CipherPrefSetDefault)
FUNC(SSL_OptionGetDefault)
FUNC(SSL_OptionSetDefault)
FUNC(SSL_CipherPolicySet )
FUNC(CERT_GetDefaultCertDB)
FUNC(CERT_OpenCertDBFilename)

