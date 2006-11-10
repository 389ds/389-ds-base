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
#include "dberrstrs.h"
#include "sslerrstrs.h"
#include "secerrstrs.h"
#include "prerrstrs.h"
#include "disconnect_error_strings.h"

};

static const PRInt32 numStrings = sizeof(errStrings) / sizeof(tuple_str);

/* Returns a UTF-8 encoded constant error string for "errNum".
 * Returns NULL of errNum is unknown.
 */
static const char *
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
		LDAPDebug( LDAP_DEBUG_ANY,
			"sequence error in error strings at item %d\n"
			"error %d (%s)\n",
			i, lastNum, errStrings[i-1].errString );
		LDAPDebug( LDAP_DEBUG_ANY,
			"should come after \n"
			"error %d (%s)\n",
			num, errStrings[i].errString, 0 );
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
