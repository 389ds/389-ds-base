/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __nscert_h
#define __nscert_h

/*
 * Description (nscert.h)
 *
 *	This file describes the interface for accessing and storing
 *	information in a Netscape client certificate to username
 *	database.  This facility is built on top of the Netscape
 *	(server) database interface as defined in nsdb.h.  
 */

#include <libaccess/nserror.h>		/* error frame list support */
#include <libaccess/nsautherr.h>	/* authentication error codes */
#include <libaccess/nsauth.h>

#include <prtypes.h>
/* Removed for new ns security integration
#include <sec.h>
*/
#include <cert.h>

#if defined(CLIENT_AUTH)

/* Certificate to user record attribute tags */
#define CAT_USERNAME	0x61		/* username associated with cert */
#define CAT_CERTID	0x62		/* id assigned to cert */

/* Attribute tags used in certificate key encoding */
#define KAT_ISSUER	0x01		/* issuer DER */
#define KAT_SUBJECT	0x02		/* subject DER */

typedef struct CertObj_s CertObj_t;
struct CertObj_s {
    SECItem co_issuer;		/* issuing authority */
    SECItem co_subject;		/* certicate's subject */
    char * co_username;		/* the local name it mapps to */
    USI_t co_certid;		/* internal id for this client certificate */
};

typedef int (*CertEnumCallback)(NSErr_t * ferrp, void * authdb,
				void * argp, CertObj_t * coptr);
    
NSPR_BEGIN_EXTERN_C

extern NSAPI_PUBLIC int nsadbCertInitialize(void);

extern NSAPI_PUBLIC int nsadbDecodeCertKey(int keylen, char * keyptr,
					   SECItem * issuer,
					   SECItem * subject);

extern NSAPI_PUBLIC int nsadbDecodeCertRec(int reclen, char * recptr,
					   CertObj_t * coptr);

extern NSAPI_PUBLIC int nsadbEncodeCertKey(SECItem * issuer, SECItem * subject,
					   int * keylen, char **keyptr);

extern NSAPI_PUBLIC int nsadbEnumerateCerts(NSErr_t * errp, void * authdb,
					    void * argp,
					    CertEnumCallback func);

extern NSAPI_PUBLIC void nsadbFreeCertObj(CertObj_t * coptr);

extern NSAPI_PUBLIC int nsadbGetCertById(NSErr_t * errp, void * authdb,
					 USI_t certid, CertObj_t **coptr);

extern NSAPI_PUBLIC int nsadbGetUserByCert(NSErr_t * errp, void * authdb,
					   CERTCertificate * cert,
					   char **username);

extern NSAPI_PUBLIC int nsadbOpenCerts(NSErr_t * errp,
				       void * authdb, int flags);

extern NSAPI_PUBLIC int nsadbPutUserByCert(NSErr_t * errp, void * authdb,
					   CERTCertificate * cert,
					   const char * username);

extern NSAPI_PUBLIC int nsadbRemoveCert(NSErr_t * errp, void * authdb,
					void * username, CertObj_t * coptr);

extern NSAPI_PUBLIC int nsadbRemoveUserCert(NSErr_t * errp, void * authdb,
					    char * username);

extern NSAPI_PUBLIC void nsadbCloseCerts(void * authdb, int flags);

extern NSAPI_PUBLIC void nsadbCloseCertUsers(void * authdb, int flags);

extern NSAPI_PUBLIC int nsadbFindCertUser(NSErr_t * errp, void * authdb,
					  const char * username, USI_t * id);


NSPR_END_EXTERN_C

#endif /* CLIENT_AUTH */


#endif /* __nscert_h */
