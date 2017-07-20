/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * errormap.c - map NSPR and OS errors to strings
 *
 */

#include "slap.h"

#ifndef SYSERRLIST_IN_STDIO
extern int sys_nerr;
extern char *sys_errlist[];
#endif

/*
 * function protoypes
 */
static const char *SECU_Strerror(PRErrorCode errNum);


/*
 * return the string equivalent of an NSPR error
 */
char *
slapd_pr_strerror(const int prerrno)
{
    char *s;

    if (prerrno == 0) {
        s = "no error";
    } else {
        if ((s = (char *)SECU_Strerror((PRErrorCode)prerrno)) == NULL) {
            s = "unknown error";
        }
    }

    return (s);
}

char *
slapi_pr_strerror(const int prerrno)
{
    return slapd_pr_strerror(prerrno);
}

/*
 * return the string equivalent of a system error
 */
const char *
slapd_system_strerror(const int syserrno)
{
    const char *s;
    /* replaced
    if ( syserrno > -1 && syserrno < sys_nerr ) {
    s = sys_errlist[ syserrno ];
    } else {
    s = "unknown";
    }
    with s= strerror(syserrno)*/
    s = strerror(syserrno);
    return (s);
}

const char *
slapi_system_strerror(const int syserrno)
{
    return slapd_system_strerror(syserrno);
}

/*
 * return the string equivalent of an NSPR error.  If "prerrno" is not
 * an NSPR error, assume it is a system error.  Please use slapd_pr_strerror()
 * or slapd_system_strerror() if you can since the concept behind this
 * function is a bit of a kludge -- one should *really* know what kind of
 * error code they have.
 */
const char *
slapd_versatile_strerror(const PRErrorCode prerrno)
{
    const char *s;

    if ((s = (const char *)SECU_Strerror(prerrno)) == NULL) {
        s = slapd_system_strerror(prerrno);
    }

    return (s);
}


/*
 ****************************************************************************
 * The code below this point was provided by Nelson Bolyard <nelsonb> of the
 *    Netscape Certificate Server team on 27-March-1998.
 *    Taken from the file ns/security/cmd/lib/secerror.c on NSS_1_BRANCH.
 *    Last updated from there: 24-July-1998 by Mark Smith <mcs>
 ****************************************************************************
 */
#include "nspr.h"

struct tuple_str
{
    PRErrorCode errNum;
    const char *errString;
};

typedef struct tuple_str tuple_str;

#define ER2(a, b) {a, b},
#define ER3(a, b, c) {a, c},

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
SECU_Strerror(PRErrorCode errNum)
{
    PRInt32 low = 0;
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
                slapi_log_err(SLAPI_LOG_ERR, "SECU_Strerror",
                              "Sequence error in error strings at item %d - error %d (%s)\n",
                              lastNum, num, errStrings[i - 1].errString);
                slapi_log_err(SLAPI_LOG_ERR, "SECU_Strerror",
                              "Should come after %d - error %d (%s)\n",
                              i, num, errStrings[i].errString);
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
