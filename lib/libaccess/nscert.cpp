/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * Description (nsadb.c)
 *
 *	This module contains routines for accessing and storing information
 *	in a Netscape client certificate to username database.  This
 *	database is used to associate a username with a client certificate
 *	that is presented to a server.
 */

#if defined(CLIENT_AUTH)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <base/systems.h>
#include <netsite.h>
#include <base/file.h>
#include <base/fsmutex.h>
#include <libaccess/nsdbmgmt.h>
#define __PRIVATE_NSADB
#include <libaccess/nsadb.h>
#include <libaccess/nsamgmt.h>

static FSMUTEX nscert_lock = 0;

NSAPI_PUBLIC int nsadbCertInitialize(void)
{
#ifdef XP_UNIX
    nscert_lock = fsmutex_init("NSCERTMAP", geteuid(),
			       FSMUTEX_VISIBLE|FSMUTEX_NEEDCRIT);
#else /* XP_WIN32 */
    char winuser[128];
    DWORD wulength;
    strcpy(winuser, "NSCERTMAP_");
    wulength = 128 - 11;
    GetUserName(winuser+10, &wulength);
    nscert_lock = fsmutex_init(winuser, 0,
			       FSMUTEX_VISIBLE|FSMUTEX_NEEDCRIT);
#endif
    return (nscert_lock == 0) ? -1 : 0;
}

NSAPI_PUBLIC int nsadbDecodeCertRec(int reclen, char * recptr,
				    CertObj_t * coptr)
{
    ATR_t cp = (ATR_t)recptr;		/* current pointer into record */
    USI_t tag;				/* attribute tag */
    USI_t len;				/* attribute value encoding length */

    /* Parse user DB record */
    while ((cp - (ATR_t)recptr) < reclen) {

	/* Get the attribute tag */
	cp = USIDECODE(cp, &tag);

	/* Get the length of the encoding of the attribute value */
	cp = USIDECODE(cp, &len);

	/* Process this attribute */
	switch (tag) {

	  case CAT_USERNAME:	/* username associated with cert */
	    cp = NTSDECODE(cp, (NTS_t *)&coptr->co_username);
	    break;

	  case CAT_CERTID:		/* certificate-to-user map id */
	    cp = USIDECODE(cp, &coptr->co_certid);
	    break;

	  default:			/* unrecognized attribute */
	    /* Just skip it */
	    cp += len;
	    break;
	}
    }

    return 0;
}

/*
 * Description (nsadbDecodeCertKey)
 *
 *	This function decodes information from a certificate key.
 *	Currently a certificate key includes the DER encoding of the
 *	issuer and subject distinguished names.  This is used to
 *	uniquely identify client certificates, even across certificate
 *	renewals.  SECItems for the issuer and subject are provided
 *	by the caller.  These are updated with the pointers and lengths
 *	of DER encodings, which can be decoded using nsadbDecodeCertName()
 *	into SECName structures.  The returned SECItems refer to data
 *	in the provided key buffer.
 *
 * Arguments:
 *
 *	keylen			- length of the certificate key encoding
 *	keyptr			- buffer containing certificate key encoding
 *	issuer			- pointer to SECItem for returning issuer
 *	subject			- pointer to SECItem for returning subject
 *
 * Returns:
 *
 *	Zero is returned if no errors are encountered.  Otherwise -1.
 */

NSAPI_PUBLIC int nsadbDecodeCertKey(int keylen, char * keyptr,
				    SECItem * issuer, SECItem * subject)
{
    ATR_t cp = (ATR_t)keyptr;		/* current pointer into DB record */
    USI_t len;				/* attribute value encoding length */
    USI_t tag;				/* attribute tag */

    /* Parse user DB record */
    while ((cp - (ATR_t)keyptr) < keylen) {

	/* Get the attribute tag */
	cp = USIDECODE(cp, &tag);

	/* Get the length of the encoding of the attribute value */
	cp = USIDECODE(cp, &len);

	/* Process this attribute */
	switch (tag) {

	  case KAT_ISSUER:		/* issuer DER encoding */
	    issuer->len = len;
	    issuer->data = cp;
	    cp += len;
	    break;

	  case KAT_SUBJECT:		/* subject name DER encoding */
	    subject->len = len;
	    subject->data = cp;
	    cp += len;
	    break;

	  default:			/* unrecognized attribute */
	    /* Just skip it */
	    cp += len;
	    break;
	}
    }

    return 0;
}

/*
 * Description (nsadbEncodeCertKey)
 *
 *	This function encodes information provided by the caller into
 *	a certificate key.  The certificate key is returned in a
 *	buffer obtained from MALLOC().
 *
 * Arguments:
 *
 *	issuer			- pointer to SECItem for issuer DER
 *	subject			- pointer to SECItem for subject DER
 *	keylen			- returned length of certificate key
 *	keyptr			- returned pointer to buffer containing
 *				  certificate key encoding
 *
 * Returns:
 *
 *	Zero is returned if no errors are encountered.  Otherwise -1.
 */

NSAPI_PUBLIC int nsadbEncodeCertKey(SECItem * issuer, SECItem * subject,
				    int * keylen, char **keyptr)
{
    ATR_t cp;			/* pointer into key buffer */
    ATR_t kptr;			/* pointer to key buffer */
    int klen;			/* length of key */
    int rv = -1;

    /* Compute length of key encoding */
    klen = 1 + USILENGTH(issuer->len) + issuer->len +
	   1 + USILENGTH(subject->len) + subject->len;

    /* Allocate buffer to contain the key */
    kptr = (ATR_t)MALLOC(klen);
    if (kptr) {
	/* Encode issuer and subject as attributes */
	cp = kptr;
	*cp++ = KAT_ISSUER;
	cp = USIENCODE(cp, issuer->len);
	memcpy(cp, issuer->data, issuer->len);
	cp += issuer->len;
	*cp++ = KAT_SUBJECT;
	cp = USIENCODE(cp, subject->len);
	memcpy(cp, subject->data, subject->len);
	rv = 0;
    }
	
    /* Return length and buffer pointer */
    if (keylen) *keylen = klen;
    *keyptr = (char *)kptr;

    return rv;
}

/*
 * Description (nsadbEnumCertsHelp)
 *
 *	This is a local function that is called by NSDB during certificate
 *	to user database enumeration.  It decodes certificate records into
 *	CertObj_t structures, and presents them to the caller of
 *	nsadbEnumerateCerts(), via the specified call-back function.
 *	The call-back function return value may be a negative error code,
 *	which will cause enumeration to stop, and the error code will be
 *	returned from nsadbEnumerateCerts().  If the return value of the
 *	call-back function is not negative, it can contain one or more of
 *	the following flags:
 *
 *		ADBF_KEEPOBJ	- do not free the CertObj_t structure
 *				  that was passed to the call-back function
 *		ADBF_STOPENUM	- stop the enumeration without an error
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	parg			- pointer to CertEnumArgs_t structure
 *	keylen			- certificate record key length
 *	keyptr			- certificate record key
 *	reclen			- length of certificate record
 *	recptr			- pointer to certificate record contents
 *
 * Returns:
 *
 *	If the call-back returns a negative result, that value is
 *	returned.  If the call-back returns ADBF_STOPENUM, then
 *	-1 is returned, causing the enumeration to stop.  Otherwise
 *	the return value is zero.
 */

typedef struct CertEnumArgs_s CertEnumArgs_t;
struct CertEnumArgs_s {
    int rv;			/* just a return value */
    void * client;		/* the current key for lookup */
    void * authdb;		/* the authentication data base */
    CertEnumCallback func;	/* client's callback function */
};

static int nsadbEnumCertsHelp(NSErr_t * errp, void * parg,
			      int keylen, char * keyptr,
			      int reclen, char * recptr)
{
    CertEnumArgs_t * ce = (CertEnumArgs_t *)parg;
    CertObj_t * coptr;
    int rv = NSAERRNOMEM;

    /* Allocate a CertObj_t structure and initialize it */
    coptr = (CertObj_t *)MALLOC(sizeof(CertObj_t));
    if (coptr) {

	coptr->co_issuer.data = 0;
	coptr->co_subject.data = 0;
	coptr->co_username = 0;
	coptr->co_certid = 0;

	/* Decode the certificate key */
	rv = nsadbDecodeCertKey(keylen, keyptr,
				&coptr->co_issuer, &coptr->co_subject);

	/* Decode the certificate record */
	rv = nsadbDecodeCertRec(reclen, recptr, coptr);

	/* Pass the CertObj_t to the callback function */
	rv = (*ce->func)(errp, ce->authdb, ce->client, coptr);
	if (rv >= 0) {

	    /* Count the number of records seen */
	    ce->rv += 1;

	    /* Free the user object unless the call-back says not to */
	    if (!(rv & ADBF_KEEPOBJ)) {
		nsadbFreeCertObj(coptr);
	    }
	    /* Return either 0 or -1, depending on ADBF_STOPENUM */
	    rv = (rv & ADBF_STOPENUM) ? -1 : 0;
	}
	else {
	    /* return the error code */
	    ce->rv = rv;
	}
    }

    return rv;
}

/*
 * Description (nsadbEnumerateClients)
 *
 *	(See description for nsadbEnumerateUsers)
 */

NSAPI_PUBLIC int nsadbEnumerateCerts(NSErr_t * errp, void * authdb,
				     void * argp, CertEnumCallback func)
{
    AuthDB_t * adb = (AuthDB_t*)authdb;
    CertEnumArgs_t helper_data;
    int rv;

    /* Open the certificate subdatabase for read access */
    rv = nsadbOpenCerts(errp, authdb, ADBF_CREAD);
    if (rv >= 0) {
	helper_data.authdb = authdb;
	helper_data.func = func;
	helper_data.client = argp;
	helper_data.rv = 0;
    
	rv = ndbEnumerate(errp, adb->adb_certdb, NDBF_ENUMNORM,
			  (void*)&helper_data, nsadbEnumCertsHelp);
    }

    return (rv < 0) ? rv: helper_data.rv;
}

NSAPI_PUBLIC void nsadbFreeCertObj(CertObj_t * coptr)
{
    if (coptr) {
	FREE(coptr->co_username);
	FREE(coptr);
    }
}

NSAPI_PUBLIC int nsadbGetCertById(NSErr_t * errp, void * authdb,
				  USI_t certid, CertObj_t **coptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    CertObj_t * cop = 0;
    char * keyptr;
    char * recptr;
    int keylen;
    int reclen;
    int rv;

    rv = nsadbOpenCerts(errp, authdb, ADBF_CREAD);
    if (rv < 0) goto punt;

    /* Get the name corresponding to the id */
    rv = ndbIdToName(errp, adb->adb_certdb, certid, &keylen, &keyptr);
    if (rv < 0) goto punt;

    rv = ndbFindName(errp, adb->adb_certdb,
		     keylen, keyptr, &reclen, &recptr);
    if (rv < 0) goto punt;

    /* Allocate a CertObj_t structure and initialize it */
    cop = (CertObj_t *)MALLOC(sizeof(CertObj_t));
    if (cop) {

	cop->co_issuer.data = 0;
	cop->co_subject.data = 0;
	cop->co_username = 0;
	cop->co_certid = 0;

	/* Decode the certificate key */
	rv = nsadbDecodeCertKey(keylen, keyptr,
				&cop->co_issuer, &cop->co_subject);

	/* Decode the certificate record */
	rv = nsadbDecodeCertRec(reclen, recptr, cop);

    }

  punt:
    if (coptr) *coptr = cop;
    return rv;
}

/*
 * Description (nsadbGetUserByCert)
 *
 *	This function looks up a specified client certificate in the
 *	authentication database.  It returns a pointer to the username
 *	associated with the client certificate, if any.  The username
 *	buffer remains valid until the authentication database is
 *	closed.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	cert			- pointer to client certificate
 *	username		- pointer to returned user name (or null)
 *
 * Returns:
 *
 *	The return value will be zero if the certificate is found. Also,
 *	*username will be set to the string value of the associated username
 *	iff username is not null.
 */

NSAPI_PUBLIC int nsadbGetUserByCert(NSErr_t * errp, void * authdb,
				    CERTCertificate * cert, char **username)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    ATR_t cp;				/* current pointer into DB record */
    char * user = 0;			/* pointer to username */
    char * keyptr = 0;			/* pointer to cert key */
    char * recptr;			/* pointer to cert db record */
    int keylen;				/* length of cert key */
    int reclen;				/* length of cert db record */
    USI_t tag;				/* attribute tag */
    USI_t len;				/* attribute value encoding length */
    int rv;

    /* Construct the record key from the certificate */
    rv = nsadbEncodeCertKey(&cert->derIssuer, &cert->derSubject,
			    &keylen, &keyptr);

    if (adb->adb_certdb == NULL) {
	rv = nsadbOpenCerts(errp, authdb, ADBF_CREAD);
	if (rv < 0) goto punt;
    }

    rv = ndbFindName(errp, adb->adb_certdb,
		     keylen, keyptr, &reclen, &recptr);
    if (rv < 0) goto punt;

    /* Parse cert DB record */
    cp = (ATR_t)recptr;

    while ((cp - (ATR_t)recptr) < reclen) {

	/* Get the attribute tag */
	cp = USIDECODE(cp, &tag);

	/* Get the length of the encoding of the attribute value */
	cp = USIDECODE(cp, &len);

	/* We want the CAT_USERNAME attribute */
	if (tag == CAT_USERNAME) {

	    /* Get the username associated with the cert */
	    user = (char *)cp;
	    break;
	}

	/* Just skip other attributes */
	cp += len;
    }

  punt:
    if (keyptr) {
	FREE(keyptr);
    }
    if (username) *username = user;
    return rv;
}

/*
 * Description (see description for nsadbOpenUsers)
 */

int nsadbOpenCerts(NSErr_t * errp, void * authdb, int flags)
{
    AuthDB_t *adb = (AuthDB_t*)authdb;
    char *dbname = NULL;		/* user database name */
    int dblen;				/* strlen(adb_dbname) */
    int version;			/* database version */
    int eid;				/* error id code */
    int rv;				/* result value */

    if (adb == NULL) goto err_inval;

    /* Is the user database already open? */
    if (adb->adb_certdb != 0) {

	/* Yes, is it open for the desired access? */
	if (adb->adb_flags & flags) {

	    /* Yes, that was easy */
	    return 0;
	}
    }
    else {
	/* Allocate space for the user database filename */
	dblen = strlen(adb->adb_dbname);

	dbname = (char *)MALLOC(dblen + strlen(ADBCERTDBNAME) + 2);
	if (dbname == 0) goto err_nomem;

	/* Construct user database name */
	strcpy(dbname, adb->adb_dbname);

	/* Put in a '/' (or '\') if it's not there */
	if (dbname[dblen-1] != FILE_PATHSEP) {
	    dbname[dblen] = FILE_PATHSEP;
	    dbname[dblen+1] = 0;
	    ++dblen;
	}

	strcpy(&dbname[dblen], ADBCERTDBNAME);

	if (nscert_lock == 0) {
	    rv = nsadbCertInitialize();
	    if (rv < 0) goto err_lock;
	}
	adb->adb_certlock = nscert_lock;
	if (adb->adb_certlock == 0) goto punt;

	fsmutex_lock((FSMUTEX)(adb->adb_certlock));

	adb->adb_certdb = ndbOpen(errp,
				  dbname, 0, NDB_TYPE_CLIENTDB, &version);
	if (adb->adb_certdb == 0) {
	    fsmutex_unlock((FSMUTEX)(adb->adb_certlock));
	    goto err_open;
	}
    }

    /*
     * We don't really reopen the database to get the desired
     * access mode, since that is handled at the nsdb level.
     * But we do update the flags, just for the record.
     */
    adb->adb_flags &= ~(ADBF_CREAD|ADBF_CWRITE);
    if (flags & ADBF_CWRITE) adb->adb_flags |= ADBF_CWRITE;
    else adb->adb_flags |= ADBF_CREAD;
    rv = 0;

  punt:
    if (dbname != NULL) FREE(dbname);
    return rv;

  err_inval:
    eid = NSAUERR3400;
    rv = NSAERRINVAL;
    goto err_ret;

  err_nomem:
    eid = NSAUERR3420;
    rv = NSAERRNOMEM;
    goto err_ret;

  err_lock:
    eid = NSAUERR3430;
    rv = NSAERRLOCK;
    goto err_ret;

  err_open:
    eid = NSAUERR3440;
    rv = NSAERROPEN;

  err_ret:
    nserrGenerate(errp, rv, eid, NSAuth_Program, 1, dbname);
    goto punt;

}

NSAPI_PUBLIC void nsadbCloseCerts(void * authdb, int flags)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;

    if (adb->adb_certnm != 0) {
	/* Close the username-to-certid database */
	nsadbCloseCertUsers(authdb, flags);
    }

    if (adb->adb_certdb != 0) {

	ndbClose(adb->adb_certdb, 0);
	adb->adb_certdb = 0;

	/*
	 * A lock is held for the certificate map DB as long as it is
	 * open, so release the lock now.
	 */
	fsmutex_unlock((FSMUTEX)(adb->adb_certlock));
    }
}

/*
 * Description (nsadbOpenCertUsers)
 *
 *	This function opens a database that maps user names to client
 *	certificates.  The database appears as "Certs.nm" in the
 *	authentication database directory.  This function requires
 *	that the primary certificate database be opened (Certs.db)
 *	first, and will open it if necessary, acquiring a global
 *	lock in the process.  The lock will not be released until
 *	nsadbCloseCerts() or nsadbClose() is called.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	flags			- same as nsadbOpenCerts()
 *
 * Returns:
 *
 *	The return value is zero if the operation is successful.
 *	Otherwise a negative error code is returned.
 */

NSAPI_PUBLIC int nsadbOpenCertUsers(NSErr_t * errp, void * authdb, int flags)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    char * dbname = 0;
    int dblen;
    int oflags = O_RDONLY;		/* assume read-only access */
    int eid;
    int rv;

    /* The primary certificate mapping database must be open first */
    if (adb->adb_certdb != 0) {

	/* It's open, but is it read-only when we need write? */
	if (((flags & adb->adb_flags) == 0) && (flags & ADBF_CWRITE)) {

	    /* Yes, close it */
	    nsadbCloseCerts(authdb, 0);
	}
    }

    /* Open it for the desired access if necessary */
    if (adb->adb_certdb == 0) {
	/*
	 * Open it for the desired access.  Note that this acquires
	 * a global lock which is not released until nsadbClose() is
	 * called for the entire authentication database.
	 */
	rv = nsadbOpenCerts(errp, authdb, flags);
	if (rv < 0) {
	    /* Go no further if that failed */
	    return rv;
	}
    }

    /* Now look at the username-to-certid database in particular */
    if (adb->adb_certnm && (adb->adb_flags & flags)) {

	/* The database is already open for the desired access */
	return 0;
    }

    dblen = strlen(adb->adb_dbname);
    dbname = (char *)MALLOC(dblen + strlen(ADBUMAPDBNAME) + 2);
    strcpy(dbname, adb->adb_dbname);
    if (dbname[dblen-1] != FILE_PATHSEP) {
	dbname[dblen] = FILE_PATHSEP;
	dbname[++dblen] = 0;
    }
    strcpy(&dbname[dblen], ADBUMAPDBNAME);

    /* Check for write access and set open flags appropriately if so */
    if (flags & ADBF_CWRITE) {
	oflags = O_CREAT|O_RDWR;
    }

    /* Open the username-to-certid database */
//    adb->adb_certnm = dbopen(dbname, oflags, 0644, DB_HASH, 0);
	adb->adb_certnm = 0;
    if (adb->adb_certnm == 0) goto err_open;

  punt:
    FREE(dbname);

    return rv;

  err_open:
    eid = NSAUERR3600;
    rv = NSAERROPEN;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 1, dbname);
    goto punt;
}

/*
 * Description (nsadbFindCertUser)
 *
 *	This function checks to see whether a client certificate is
 *	registered for a specified user name.  If so, it returns the
 *	certificate mapping id (for use with nsadbGetCertById()).
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	username		- pointer to user name string
 *	id			- pointer to returned certificate mapping id
 *
 * Returns:
 *
 *	If a certificate is registered for the specified user, the return
 *	value is zero and the certificate mapping id is returned via 'id'.
 *	Otherwise the return value is a negative error code (nsautherr.h)
 *	and an error frame is generated if an error frame list is provided.
 */

NSAPI_PUBLIC int nsadbFindCertUser(NSErr_t * errp, void * authdb,
				   const char * username, USI_t * id)
{
    int eid;
    int rv;
    eid = NSAUERR3700;
    rv = NSAERRNAME;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);
    return rv;
}

/*
 * Description (nsadbAddCertUser)
 *
 *	This function adds an entry to the username-to-cert id database,
 *	with a given username and certificate mapping id.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	username		- pointer to user name string
 *	id			- certificate mapping id
 *
 * Returns:
 *
 *	If the entry is added successfully, the return value is zero.
 *	Otherwise the return value is a negative error code (nsautherr.h)
 *	and an error frame is generated if an error frame list is provided.
 */

NSAPI_PUBLIC int nsadbAddCertUser(NSErr_t * errp, void * authdb,
				  const char * username, USI_t id)
{
	/* Need to be ported on NSS 3.2 */
    int eid;
    int rv;

    eid = NSAUERR3800;
    rv = NSAERRPUT;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);
    return rv;
}

NSAPI_PUBLIC int nsadbRemoveCertUser(NSErr_t * errp, void * authdb,
				     char * username)
{
	/* Need to be ported on NSS 3.2 */
    int eid;
    int rv;

    eid = NSAUERR3800;
    rv = NSAERRPUT;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 0);
    return rv;
}

NSAPI_PUBLIC void nsadbCloseCertUsers(void * authdb, int flags)
{
	/* Need to be ported on NSS 3.2 */
}

/*
 * Description (nsadbPutUserByCert)
 *
 *	This function looks up a stores a client certificate mapping
 *	in the authentication database along with the associated username.
 *	It assumes that a record with the specified certificate key does
 *	not already exist, and will replace it if it does.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	authdb			- handle returned by nsadbOpen()
 *	certLen			- length of the certificate key
 *	cert			- certificate key pointer
 *	user			- username to be associated with the
 *				  certificate
 *
 * Returns:
 *
 */

NSAPI_PUBLIC int nsadbPutUserByCert(NSErr_t * errp, void * authdb,
				    CERTCertificate * cert,
				    const char * username)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    ATR_t cp;			/* pointer into cert record contents */
    char * keyptr = 0;		/* pointer to cert record key */
    char * recptr = 0;		/* pointer to cert record contents */
    int keylen;			/* length of cert record key */
    int reclen;			/* length of cert record contents */
    USI_t certid;
    int usrlen;
    int certidlen;
    int eid;
    int rv;

    /* Construct the record key from the certificate */
    rv = nsadbEncodeCertKey(&cert->derIssuer, &cert->derSubject,
			    &keylen, &keyptr);

    /* Open the username-to-cert id database for write */
    rv = nsadbOpenCertUsers(errp, authdb, ADBF_CWRITE);
    if (rv) goto punt;

    /* If the username is already mapped to a cert, it's an error */
    certid = 0;
    rv = nsadbFindCertUser(errp, authdb, username, &certid);
    if (rv == 0) goto err_map;

    /*
     * Allocate a certificate id and write a record mapping this id
     * to the specified certificate key.
     */
    certid = 0;
    rv = ndbAllocId(errp, adb->adb_certdb, keylen, keyptr, &certid);
    if (rv) goto punt;

    /* Record the username as being mapped to the allocated cert id */
    rv = nsadbAddCertUser(errp, authdb, username, certid);
    if (rv < 0) goto punt;

    nsadbCloseCertUsers(authdb, 0);

    /*
     * First we need to figure out how long the generated record will be.
     * This doesn't have to be exact, but it must not be smaller than the
     * actual record size.
     */

    /* CAT_USERNAME attribute: tag, length, NTS */
    usrlen = NTSLENGTH(username);
    if (usrlen > 127) goto err_user;
    reclen = 2 + usrlen;

    /* CAT_CERTID attribute: tag, length, USI */
    certidlen = USILENGTH(certid);
    reclen += 2 + certidlen;

    /* Allocate the attribute record buffer */
    recptr = (char *)MALLOC(reclen);
    if (recptr) {

	cp = (ATR_t)recptr;

	/* Encode CAT_USERNAME attribute */
	*cp++ = CAT_USERNAME;
	*cp++ = usrlen;
	cp = NTSENCODE(cp, (NTS_t)username);

	/* Encode CAT_CERTID attribute */
	*cp++ = CAT_CERTID;
	*cp++ = certidlen;
	cp = USIENCODE(cp, certid);
    }

    /* Store the record in the database under the certificate key */
    rv = ndbStoreName(errp, adb->adb_certdb,
		      0, keylen, keyptr, reclen, recptr);

  punt:
    if (keyptr) {
	FREE(keyptr);
    }
    if (recptr) {
	FREE(recptr);
    }

    return rv;

  err_user:
    eid = NSAUERR3500;
    rv = NSAERRINVAL;
    nserrGenerate(errp, rv, eid, NSAuth_Program, 1, adb->adb_dbname);
    goto punt;

  err_map:
    eid = NSAUERR3520;
    rv = NSAERRCMAP;
    nsadbCloseCertUsers(authdb, 0);
    nserrGenerate(errp, rv, eid, NSAuth_Program, 1, adb->adb_dbname);
    goto punt;
}

NSAPI_PUBLIC int nsadbRemoveCert(NSErr_t * errp, void * authdb, 
				 void * username, CertObj_t * coptr)
{
    AuthDB_t * adb = (AuthDB_t *)authdb;
    char * keyptr = 0;			/* pointer to cert record key */
    int keylen;				/* length of cert record key */
    int rv;
    int rv2;

    /* If a username is specified, require it to match */
    if (username && strcmp((char *)username, coptr->co_username)) {
	return 0;
    }

    /* Construct the record key from the certificate */
    rv = nsadbEncodeCertKey(&coptr->co_issuer, &coptr->co_subject,
			    &keylen, &keyptr);

    if (adb->adb_certdb == NULL) {
	rv = nsadbOpenCerts(errp, authdb, ADBF_CWRITE);
	if (rv < 0) goto punt;
    }

    /* Remove the username-to-cert id entry from Certs.nm */
    rv = nsadbOpenCertUsers(errp, authdb, ADBF_CWRITE);
    if (rv < 0) goto punt;
    rv = nsadbRemoveCertUser(errp, authdb, coptr->co_username);
    nsadbCloseCertUsers(authdb, 0);

    /* Free the cert id value, if any */
    rv = 0;
    if (coptr->co_certid != 0) {
	rv = ndbFreeId(errp, adb->adb_certdb,
		       keylen, keyptr, coptr->co_certid);
    }

    /* Delete the cert record */
    rv2 = ndbDeleteName(errp, adb->adb_certdb, 0, keylen, keyptr);

  punt:
    if (keyptr) {
	FREE(keyptr);
    }
    return (rv) ? rv : rv2;
}

NSAPI_PUBLIC int nsadbRemoveUserCert(NSErr_t * errp,
				     void * authdb, char * username)
{
    CertObj_t * coptr = 0;
    USI_t certid = 0;
    int rv;

    /*
     * Open for read access at first.  We don't want to create the
     * database if it's not already there.  This will do nothing
     * if the database is already open for write, since that implies
     * read access as well.
     */
    rv = nsadbOpenCertUsers(errp, authdb, ADBF_CREAD);
    if (rv < 0) goto punt;

    /* Find a certificate mapping id for the given username */
    rv = nsadbFindCertUser(errp, authdb, username, &certid);
    if (rv < 0) goto punt;

    /* Look up the mapping from the mapping id */
    rv = nsadbGetCertById(errp, authdb, certid, &coptr);
    if (rv < 0) goto punt;

    /* It's there, so remove it.  This will re-open for write if needed. */
    rv = nsadbRemoveCert(errp, authdb, (void *)username, coptr);

  punt:

    if (coptr != 0) {
	nsadbFreeCertObj(coptr);
    }

    return rv;
}

#endif /* defined(CLIENT_AUTH) */
