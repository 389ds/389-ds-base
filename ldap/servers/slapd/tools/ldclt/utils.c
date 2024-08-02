#ident "ldclt @(#)utils.c    1.4 01/01/11"

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
    FILE :        utils.c
    AUTHOR :        Jean-Luc SCHWING
    VERSION :       1.0
    DATE :        14 November 2000
    DESCRIPTION :
            This file contains the utilities functions that will be
            used as well by ldclt and by the genldif command, e.g.
            the random generator functions, etc...
            The main target/reason for creating this file is to be
            able to easely separate these functions from ldclt's
            framework and data structures, and thus to provide a
            kind of library suitable for any command.
    LOCAL :        None.
*/

#include "utils.h"                              /* Basic definitions for this file */
#include <stdio.h>                              /* sprintf(), etc... */
#include <stdlib.h>                             /* lrand48(), etc... */
#include <ctype.h>    /* isascii(), etc... */   /*JLS 16-11-00*/
#include <string.h>    /* strerror(), etc... */ /*JLS 14-11-00*/
#include <errno.h>    /* ENOMEM */


/*
 * Some global variables...
 */
#define ldcltrand48() lrand48()


/* ****************************************************************************
    FUNCTION :    utilsInit
    PURPOSE :    Initiates the utilities functions.
    INPUT :        None.
    OUTPUT :    None.
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
utilsInit(void)
{

    /*
   * No error
   */
    return (0);
}

/* ****************************************************************************
    FUNCTION :    rndlim
    PURPOSE :    Returns a random number between the given limits.
    INPUT :        low    = low limit
            high    = high limit
    OUTPUT :    None.
    RETURN :    The random value.
    DESCRIPTION :
 *****************************************************************************/
int
rndlim(
    int low,
    int high)
{
    return (low + ldcltrand48() % (high - low + 1));
}

/* ****************************************************************************
    FUNCTION :    rnd
    PURPOSE :    Creates a random number string between the given
            arguments. The string is returned in the buffer.
    INPUT :        low    = low limit
            high    = high limit
            ndigits    = number of digits - 0 means no zero pad
    OUTPUT :    buf    = buffer to write the random string. Note that
                  it is generated with fixed number of digits,
                  completed with leading '0', if ndigits > 0
    RETURN :    None.
    DESCRIPTION :
 *****************************************************************************/
void
rnd(
    char *buf,
    int low,
    int high,
    int ndigits)
{
    sprintf(buf, "%0*d", ndigits,
            (int)(low + (ldcltrand48() % (high - low + 1)))); /*JLS 14-11-00*/
}

/* ****************************************************************************
    FUNCTION :    rndstr
    PURPOSE :    Return a random string, of length ndigits. The string
            is returned in the buf.
    INPUT :        ndigits    = number of digits required.
    OUTPUT :    buf    = the buf must be long enough to contain the
                  requested string.
    RETURN :    None.
    DESCRIPTION :
 *****************************************************************************/
void
rndstr(
    char *buf,
    int ndigits)
{
    unsigned int rndNum; /* The random value */
    int charNum;         /* Random char in buf */
    int byteNum;         /* Byte in rndNum */
    char newChar;        /* The new byte as a char */
    char *rndArray;      /* To cast the rndNum into chars */

    charNum = 0;
    byteNum = 4;
    rndArray = (char *)(&rndNum);
    while (charNum < ndigits) {
        /*
     * Maybe we should generate a new random number ?
     */
        if (byteNum == 4) {
            rndNum = ldcltrand48(); /*JLS 14-11-00*/
            byteNum = 0;
        }

        /*
     * Is it a valid char ?
     */
        newChar = rndArray[byteNum];

        /*
     * The last char must not be a '\' nor a space.
     */
        if (!(((charNum + 1) == ndigits) &&
              ((newChar == '\\') || (newChar == ' ')))) {
            /*
       * First, there are some special characters that have a meaning for
       * LDAP, and thus that must be quoted. What LDAP's rfc1779 means by
       * "to quote" may be translated by "to antislash"
       * Note: we do not check the \ because in this way, it leads to randomly
       *       quote some valid characters.
       */
            if ((newChar == '=') || (newChar == ';') || (newChar == ',') ||
                (newChar == '+') || (newChar == '"') || (newChar == '<') ||
                (newChar == '>') || (newChar == '#')) {
                /*
         * Ensure that the previous char is *not* a \ otherwise
         * it will result in a \\, rather than a \,
         * If it is the case, add one more \ to have \\\,
         */
                if ((charNum > 0) && (buf[charNum - 1] == '\\')) {
                    if ((charNum + 3) < ndigits) {
                        buf[charNum++] = '\\';
                        buf[charNum++] = '\\';
                        buf[charNum++] = newChar;
                    }
                } else {
                    if ((charNum + 2) < ndigits) {
                        buf[charNum++] = '\\';
                        buf[charNum++] = newChar;
                    }
                }
            } else {
                /*
         * strict ascii required
         */
                if (isascii(newChar) && !iscntrl(newChar))
                    buf[charNum++] = newChar;
            }
        }

        /*
     * Next byte of the random value.
     */
        byteNum++;
    }

    /*
   * End of function
   */
    buf[charNum] = '\0';
}

/* increment val - if val > max, wrap value around to start over at min */
/* the range is max - min + 1 = number of possible values */
/* the new value is (((val + incr) - min) % range) + min */
int
incr_and_wrap(int val, int min, int max, int incr)
{
    int range = max - min + 1;
    return (((val + incr) - min) % range) + min;
}

/* ****************************************************************************
    FUNCTION :    safe_malloc
    PURPOSE :    a malloc that never returns NULL
    INPUT :        datalen    = lenght of the alloced buffer in bytes.
    OUTPUT :        None
    RETURN :    alloced buffer
    DESCRIPTION :
 *****************************************************************************/
void *
safe_malloc(size_t datalen)
{
    void *pt = malloc(datalen);
    if (pt == NULL) {
        fprintf(stderr, "ldclt: %s\n", strerror(ENOMEM));
        fprintf(stderr, "ldclt: Error: Unable to alloc %zd bytes.\n", datalen);
        exit(3);
    }
    return pt;
}
