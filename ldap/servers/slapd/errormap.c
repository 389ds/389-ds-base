/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * errormap.c - map NSPR and OS errors to strings
 *
 */

#include "slap.h"

#ifndef SYSERRLIST_IN_STDIO
extern int	sys_nerr;
extern char	*sys_errlist[];
#endif

/*
 * function protoypes
 */
static const char *SECU_Strerror(PRErrorCode errNum);


/*
 * return the string equivalent of an NSPR error
 */
char *
slapd_pr_strerror( const int prerrno )
{
    char	*s;

    if (( s = (char *)SECU_Strerror( (PRErrorCode)prerrno )) == NULL ) {
	s = "unknown";
    }

    return( s );
}


/*
 * return the string equivalent of a system error
 */
const char *
slapd_system_strerror( const int syserrno )
{
    const char	*s;
	/* replaced
    if ( syserrno > -1 && syserrno < sys_nerr ) {
	s = sys_errlist[ syserrno ];
    } else {
	s = "unknown";
    }
	with s= strerror(syserrno)*/
    s=strerror(syserrno);
    return( s );
}


/*
 * return the string equivalent of an NSPR error.  If "prerrno" is not
 * an NSPR error, assume it is a system error.  Please use slapd_pr_strerror()
 * or slapd_system_strerror() if you can since the concept behind this
 * function is a bit of a kludge -- one should *really* know what kind of
 * error code they have.
 */
const char *
slapd_versatile_strerror( const PRErrorCode prerrno )
{
    const char	*s;

    if (( s = (const char *)SECU_Strerror( prerrno )) == NULL ) {
	s = slapd_system_strerror( (const int)prerrno );
    }

    return( s );
}


/*
 ****************************************************************************
 * The code below this point was provided by Nelson Bolyard <nelsonb> of the
 *	Netscape Certificate Server team on 27-March-1998.
 *	Taken from the file ns/security/cmd/lib/secerror.c on NSS_1_BRANCH.
 *	Last updated from there: 24-July-1998 by Mark Smith <mcs>
 *
 * All of the Directory Server specific changes are enclosed inside
 *	#ifdef NS_DS.
 ****************************************************************************
 */
#include "nspr.h"

struct tuple_str {
    PRErrorCode	 errNum;
    const char * errString;
};

typedef struct tuple_str tuple_str;

#define ER2(a,b)   {a, b},
#define ER3(a,b,c) {a, c},

#include "secerr.h"
#include "sslerr.h"

static const tuple_str errStrings[] = {

/* keep this list in ascending order of error numbers */
#ifdef NS_DS
#include "dberrstrs.h"
#include "sslerrstrs.h"
#include "secerrstrs.h"
#include "prerrstrs.h"
#include "disconnect_error_strings.h"
#else /* NS_DS */
#include "SSLerrs.h"
#include "SECerrs.h"
#include "NSPRerrs.h"
#endif /* NS_DS */

};

static const PRInt32 numStrings = sizeof(errStrings) / sizeof(tuple_str);

/* Returns a UTF-8 encoded constant error string for "errNum".
 * Returns NULL of errNum is unknown.
 */
#ifdef NS_DS
static
#endif /* NS_DS */
const char *
SECU_Strerror(PRErrorCode errNum) {
    PRInt32 low  = 0;
    PRInt32 high = numStrings - 1;
    PRInt32 i;
    PRErrorCode num;
    static int initDone;

    /* make sure table is in ascending order.
     * binary search depends on it.
     */
    if (!initDone) {
	PRErrorCode lastNum = errStrings[low].errNum;
    	for (i = low + 1; i <= high; ++i) {
	    num = errStrings[i].errNum;
	    if (num <= lastNum) {
#ifdef NS_DS
		LDAPDebug( LDAP_DEBUG_ANY,
			"sequence error in error strings at item %d\n"
			"error %d (%s)\n",
			i, lastNum, errStrings[i-1].errString );
		LDAPDebug( LDAP_DEBUG_ANY,
			"should come after \n"
			"error %d (%s)\n",
			num, errStrings[i].errString, 0 );
#else /* NS_DS */
	    	fprintf(stderr, 
"sequence error in error strings at item %d\n"
"error %d (%s)\n"
"should come after \n"
"error %d (%s)\n",
		        i, lastNum, errStrings[i-1].errString, 
			num, errStrings[i].errString);
#endif /* NS_DS */
	    }
	    lastNum = num;
	}
	initDone = 1;
    }

    /* Do binary search of table. */
    while (low + 1 < high) {
    	i = (low + high) / 2;
	num = errStrings[i].errNum;
	if (errNum == num) 
	    return errStrings[i].errString;
        if (errNum < num)
	    high = i;
	else 
	    low = i;
    }
    if (errNum == errStrings[low].errNum)
    	return errStrings[low].errString;
    if (errNum == errStrings[high].errNum)
    	return errStrings[high].errString;
    return NULL;
}
