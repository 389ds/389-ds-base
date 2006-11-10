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
 * Description (lexer.c)
 *
 *	This module provides functions to assist parsers in lexical
 *	analysis.  The idea is to provide a slightly higher-level
 *	interface than that of ctype.h.
 */

#include "netsite.h"
#include "prlog.h"

#include "lexer_pvt.h"
#include "base/lexer.h"

/*
 * Description (lex_class_check)
 *
 *	This function checks whether a given character belongs to one or
 *	specified character classes.
 *
 * Arguments:
 *
 *	chtab			- character class table pointer
 *	code			- character code to be tested
 *	cbits			- bit mask of character classes
 *
 * Returns:
 *
 *	The return value is zero if the code is not in any of the character
 *	classes.  It is non-zero, if the code is in at least one of the
 *	classes.
 */
NSAPI_PUBLIC 
int lex_class_check(void * chtab, char code, unsigned long cbits)
{
    LEXClassTab_t * lct;		/* character class table pointer */
    unsigned char * bp;			/* bit vector pointer */
    int rv = 0;				/* return value */
    int i;				/* loop index */

    lct = (LEXClassTab_t *)chtab;

    bp = lct->lct_bv + code * lct->lct_bvbytes;

    for (i = 0; i < lct->lct_bvbytes; ++i) {
	if (*bp++ & cbits) {
	    rv = 1;
	    break;
	}
	cbits >>= 8;
    }

    return rv;
}

/*
 * Description (lex_class_create)
 *
 *	This function creates a new character class table.  A
 *	character class table is used to map a character code to a
 *	set of character classes.  The mapping for a given character
 *	is expressed as a bit vector, where each bit indicates the
 *	membership of that character in one of the character classes.
 *
 * Arguments:
 *
 *	classc		- the number of character classes being defined
 *	classv		- pointers to null-terminated strings containing
 *			  the character codes in each character class
 *	pchtab		- indicates where to store a returned handle for
 *			  the character class table
 *
 * Returns:
 *
 *	If successful, the return value is the number of character
 *	classes specified (classc), and a handle for the created table
 *	is returned through pchtab.
 *
 * Usage Notes:
 *
 *	Null (\000) can never be in any character classes, since it
 *	marks the end of the classv[] strings.
 *
 *	classv[] can included NULL pointers, in which case bits will be
 *	allocated for corresponding empty character classes.
 */
NSAPI_PUBLIC 
int lex_class_create(int classc, char * classv[], void **pchtab)
{
    int ncodes = 128;			/* number of character encodings */
    int bvbytes;			/* bytes per bit vector */
    LEXClassTab_t * ct;			/* class table pointer */
    unsigned char * bp;			/* bit vector pointer */
    char * cp;				/* class string pointer */
    int bitmask;			/* class bit mask */
    int bnum;				/* byte number in bit vector */
    int ci;				/* character index */
    int i;				/* class index */

    /* Get number of bytes per bit vector */
    PR_ASSERT(classc > 0);
    bvbytes = (classc + 7) >> 3;

    /* Allocate the character class table */
    ct = (LEXClassTab_t *)calloc(1, sizeof(LEXClassTab_t) + ncodes * bvbytes);
    if (ct == NULL) {

	/* Error - insufficient memory */
	return LEXERR_MALLOC;
    }

    /* Initialize the class table */
    ct->lct_classc = classc;
    ct->lct_bvbytes = bvbytes;
    ct->lct_bv = (unsigned char *)(ct + 1);

    /* Initialize the bit vectors */
    for (i = 0; i < classc; ++i) {

	cp = classv[i];
	if (cp != NULL) {

	    bitmask = 1 << (i & 7);
	    bnum = i >> 7;

	    while ((ci = *cp++) != 0) {
		bp = ct->lct_bv + ci + bnum;
		*bp |= bitmask;
	    }
	}
    }

    /* Return pointer to table */
    PR_ASSERT(pchtab != NULL);
    *pchtab = (void *)ct;

    return classc;
}

NSAPI_PUBLIC 
void lex_class_destroy(void * chtab)
{
    FREE((void *)chtab);
}

NSAPI_PUBLIC 
LEXStream_t * lex_stream_create(LEXStreamGet_t strmget, void * strmid,
				char * buf, int buflen)
{
    LEXStream_t * lst;		/* stream structure pointer */

    /* Allocate the stream structure */
    lst = (LEXStream_t *)MALLOC(sizeof(LEXStream_t));
    if (lst == NULL) {
	/* Error - insufficient memory */
	return 0;
    }

    lst->lst_strmid = strmid;
    lst->lst_get = strmget;

    /*
     * Allocate a buffer for the stream if there's a positive length
     * but a NULL buffer pointer.
     */
    if ((buflen > 0) && (buf == NULL)) {

	buf = (char *)MALLOC(buflen);
	if (buf == NULL) {
	    FREE((void *)lst);
	    return 0;
	}

	/* Also initialize the current position and residual length */
	lst->lst_cp = buf;
	lst->lst_len = 0;
	lst->lst_flags = LST_FREEBUF;
    }

    lst->lst_buf = buf;
    lst->lst_buflen = buflen;

    return lst;
}

NSAPI_PUBLIC 
void lex_stream_destroy(LEXStream_t * lst)
{
    if ((lst->lst_flags & LST_FREEBUF) && (lst->lst_buf != NULL)) {
	FREE(lst->lst_buf);
    }
    FREE((void *)lst);
}

/*
 * Description (lex_token_new)
 *
 *      This function creates a new token object.  A token object is
 *      used to accumulate text in an associated buffer.  If the
 *      'growlen' argument is specified as a value that is greater
 *      than zero, then the token buffer will be reallocated as
 *      necessary to accomodate more text.  The initial size of
 *      the token buffer is given by 'initlen', which may be zero,
 *      and should be zero if lex_token_setbuf() is used.
 *
 *      The token object is allocated from the memory pool given
 *      by the 'pool' argument.  The default pool for the current
 *      thread is used if 'pool' is null.
 *
 * Arguments:
 *
 *      pool                    - handle for memory pool to be used
 *      initlen                 - initial length of token buffer
 *      growlen                 - amount to grow a full token buffer
 *      token                   - pointer to returned token handle
 *
 * Returns:
 *
 *      If successful, the function return value is zero and a handle
 *      for the new token is returned via 'token'.  Otherwise a negative
 *      error code is returned.
 */

NSAPI_PUBLIC
int lex_token_new(pool_handle_t * pool, int initlen, int growlen, void **token)
{
    LEXToken_t * lt;			/* new token pointer */

    /* Allocate the token structure */
    if (pool) {
        lt = (LEXToken_t *)pool_calloc(pool, 1, sizeof(LEXToken_t));
    }
    else {
        lt = (LEXToken_t *)CALLOC(sizeof(LEXToken_t));
    }
    if (lt == NULL) {
	/* Error - insufficient memory */
	return LEXERR_MALLOC;
    }

    /* Save the memory pool handle for future allocations */
    lt->lt_mempool = pool;

    /* Allocate the initial token buffer if initlen > 0 */
    if (initlen > 0) {
        if (pool) {
            lt->lt_buf = (char *)pool_malloc(pool, initlen);
        }
        else {
            lt->lt_buf = (char *)MALLOC(initlen);
        }
        if (lt->lt_buf == NULL) {
            /* Error - insufficient memory */
            if (pool) {
                pool_free(pool, (void *)lt);
            }
            else {
                FREE((void *)lt);
            }
            return LEXERR_MALLOC;
        }

        lt->lt_initlen = initlen;
        lt->lt_buflen = initlen;
        lt->lt_buf[0] = 0;
    }

    if (growlen > 0) lt->lt_inclen = growlen;

    PR_ASSERT(token != NULL);
    *token = (void *)lt;

    return 0;
}

/*
 * Description (lex_token_start)
 *
 *      This function discards any current contents of the token buffer
 *      associated with a specified token object, so that any new data
 *      appended to the token will start at the beginning of the token
 *      buffer.  If there is no token buffer currently associated with
 *      the token, and the 'initlen' value specified to lex_token_new()
 *      was greater than zero, then a new token buffer is allocated.
 *      This function enables a token and optionally its token buffer
 *      to be reused.
 *
 * Arguments:
 *
 *      token                           - handle for token object
 *
 * Returns:
 *
 *      If successful, the function return value is zero.  Otherwise
 *      a negative error code is returned.
 */

NSAPI_PUBLIC int
lex_token_start(void * token)
{
    LEXToken_t * lt = (LEXToken_t *)token;	/* token pointer */

    /* Do we need to allocate a token buffer? */
    if ((lt->lt_buf == NULL) && (lt->lt_initlen > 0)) {

	/* Allocate the initial token buffer */
        if (lt->lt_mempool) {
            lt->lt_buf = (char *)pool_malloc(lt->lt_mempool, lt->lt_initlen);
        }
        else {
            lt->lt_buf = (char *)MALLOC(lt->lt_initlen);
        }
	if (lt->lt_buf == NULL) {
	    /* Error - insufficient memory */
	    return LEXERR_MALLOC;
	}
	lt->lt_buflen = lt->lt_initlen;
    }

    lt->lt_len = 0;
    lt->lt_buf[0] = 0;

    return 0;
}

/*
 * Description (lex_token_info)
 *
 *      This function returns information about the token buffer currently
 *      associated with a token object.  This includes a pointer to the
 *      token data, if any, the current length of the token data, and the
 *      current size of the token buffer.
 *
 * Arguments:
 *
 *      token                   - handle for token object
 *      tdatalen                - pointer to returned token data length
 *                                (may be null)
 *      tbufflen                - pointer to returned token buffer length
 *                                (may be null)
 *
 * Returns:
 *
 *      The function return value is a pointer to the beginning of the
 *      token data, or null if there is no token buffer associated with
 *      the token.  The token data length and token buffer length are
 *      returned via 'tdatalen' and 'tbufflen', respectively.
 */

NSAPI_PUBLIC
char * lex_token_info(void * token, int * tdatalen, int * tbufflen)
{
    LEXToken_t * lt = (LEXToken_t *)token;      /* token pointer */

    if (tdatalen) *tdatalen = lt->lt_len;
    if (tbufflen) *tbufflen = lt->lt_buflen;

    return lt->lt_buf;
}

/*
 * Description (lex_token)
 *
 *      This function returns a pointer to the current token buffer, if any.
 *      If the length of the token is also needed, use lex_token_info().
 *      This function would normally be used when the token is a
 *      null-terminated string.  See also lex_token_take().
 *
 * Arguments:
 *
 *      token                           - handle for token object
 *
 * Returns:
 *
 *      A pointer to the beginning of the current token is returned.
 *      The pointer is null if no token buffer is currently associated
 *      with the token object.
 */

NSAPI_PUBLIC 
char * lex_token(void * token)
{
    LEXToken_t * lt = (LEXToken_t *)token;      /* token pointer */

    return lt->lt_buf;
}

/*
 * Description (lex_token_destroy)
 *
 *      This function destroys a specified token object.  The memory
 *      associated with the token object and its token buffer, if any,
 *      is freed to whence it came.  Note that token objects can be
 *      associated with a memory pool, and destroyed implicitly when
 *      the pool is destroyed via pool_destroy().
 *
 * Arguments:
 *
 *      token                           - handle for token object
 */

NSAPI_PUBLIC 
void lex_token_destroy(void * token)
{
    LEXToken_t * lt = (LEXToken_t *)token;      /* token pointer */

    if (lt) {
        if (lt->lt_mempool) {
            if (lt->lt_buf) {
                pool_free(lt->lt_mempool, (void *)(lt->lt_buf));
            }
            pool_free(lt->lt_mempool, (void *)lt);
        }
        else {
            if (lt->lt_buf) {
                FREE(lt->lt_buf);
            }
            FREE(lt);
        }
    }
}

/*
 * Description (lex_token_get)
 *
 *      This function returns a pointer to the current token buffer,
 *      leaving the token with no associated token buffer.  The caller
 *      assumes ownership of the returned token buffer.  The length
 *      of the token data and the length of the token buffer are returned
 *      if requested.  Note that lex_token_take() performs a similar
 *      operation.
 *
 * Arguments:
 *
 *      token                           - handle for token object
 *      tdatalen                - pointer to returned token data length
 *                                (may be null)
 *      tbufflen                - pointer to returned token buffer length
 *                                (may be null)
 *
 * Returns:
 *
 *      The function return value is a pointer to the beginning of the
 *      token data, or null if there is no token buffer associated with
 *      the token.  The token data length and token buffer length are
 *      returned via 'tdatalen' and 'tbufflen', respectively.
 */

NSAPI_PUBLIC 
char * lex_token_get(void * token, int * tdatalen, int * tbufflen)
{
    LEXToken_t * lt = (LEXToken_t *)token;      /* token pointer */
    char * tokenstr;

    tokenstr = lt->lt_buf;
    if (tdatalen) *tdatalen = lt->lt_len;
    if (tbufflen) *tbufflen = lt->lt_buflen;

    lt->lt_buf = NULL;
    lt->lt_buflen = 0;
    lt->lt_len = 0;

    return tokenstr;
}

/*
 * Description (lex_token_take)
 *
 *      This function returns a pointer to the current token buffer,
 *      leaving the token with no associated token buffer.  The caller
 *      assumes ownership of the returned token buffer.  Note that
 *      lex_token_get() performs a similar operation, but returns more
 *      information.
 *
 * Arguments:
 *
 *      token                           - handle for token object
 *
 * Returns:
 *
 *      A pointer to the beginning of the current token is returned.
 *      The pointer is null if no token buffer is currently associated
 *      with the token object.
 */

NSAPI_PUBLIC 
char * lex_token_take(void * token)
{
    LEXToken_t * lt = (LEXToken_t *)token;      /* token pointer */
    char * tokenstr;

    tokenstr = lt->lt_buf;

    lt->lt_buf = NULL;
    lt->lt_buflen = 0;
    lt->lt_len = 0;

    return tokenstr;
}

/*
 * Description (lex_token_append)
 *
 *      This function appends data to the end of a token.  If 'growlen'
 *      was specified as a greater-than-zero value for lex_token_new(),
 *      then the token buffer may be reallocated to accomodate the
 *      new data if necessary.  A null byte is maintained in the token
 *      buffer following the token data, but it is not included in the
 *      token data length.
 *
 * Arguments:
 *
 *      token                           - handle for token object
 *      nbytes                          - number of bytes of new data
 *      src                             - pointer to new data
 *
 * Returns:
 *
 *      If successful, the function return value is the new length of
 *      the token data.  Otherwise a negative error code is returned.
 */

NSAPI_PUBLIC 
int lex_token_append(void * token, int nbytes, char * src)
{
    LEXToken_t * lt = (LEXToken_t *)token;      /* token pointer */
    int bufsize;
    int length;

    PR_ASSERT(nbytes >= 0);
    PR_ASSERT((src != NULL) || (nbytes == 0));

    if (nbytes > 0) {

	bufsize = lt->lt_buflen;
	length = lt->lt_len + nbytes;

	if (length >= bufsize) {

	    while (length >= bufsize) {
		bufsize += lt->lt_inclen;
	    }

            if (lt->lt_mempool) {
                if (lt->lt_buf) {
                    lt->lt_buf = (char *)pool_realloc(lt->lt_mempool,
                                                      lt->lt_buf, bufsize);
                }
                else {
                    lt->lt_buf = (char *)pool_malloc(lt->lt_mempool, bufsize);
                }
            }
            else {
                if (lt->lt_buf) {
                    lt->lt_buf = (char *)REALLOC(lt->lt_buf, bufsize);
                }
                else {
                    lt->lt_buf = (char *)MALLOC(bufsize);
                }
            }
        }

	if (lt->lt_buf) {

	    memcpy((void *)(lt->lt_buf + lt->lt_len), (void *)src, nbytes);
	    lt->lt_buf[length] = 0;
	    lt->lt_len = length;
	    lt->lt_buflen = bufsize;
	}
        else {
            /* Error - insufficient memory */
            return LEXERR_MALLOC;
        }
    }

    return lt->lt_len;
}

NSAPI_PUBLIC 
int lex_next_char(LEXStream_t * lst, void * chtab, unsigned long cbits)
{
    LEXClassTab_t * lct;		/* character class table pointer */
    unsigned char * bp;			/* bit vector pointer */
    unsigned long bitmask;		/* class bit mask temporary */
    int rv;				/* return value */
    int i;				/* loop index */

    lct = (LEXClassTab_t *)chtab;

    /* Go get more stream data if none left in the buffer */
    if (lst->lst_len <= 0) {
	rv = (*lst->lst_get)(lst);
	if (rv <= 0) {
	    return rv;
	}
    }

    /* Get the next character from the buffer */
    rv = *lst->lst_cp;

    bitmask = cbits;
    bp = lct->lct_bv + rv * lct->lct_bvbytes;

    for (i = 0; i < lct->lct_bvbytes; ++i) {
	if (*bp++ & bitmask) {
	    /* Update the buffer pointer and length */
	    lst->lst_cp += 1;
	    lst->lst_len -= 1;
	    break;
	}
	bitmask >>= 8;
    }

    return rv;
}

NSAPI_PUBLIC 
int lex_scan_over(LEXStream_t * lst, void * chtab, unsigned long cbits,
		  void * token)
{
    LEXClassTab_t * lct;		/* character class table pointer */
    char * cp;				/* current pointer in stream buffer */
    unsigned char * bp;			/* bit vector pointer */
    unsigned long bitmask;		/* class bit mask temporary */
    int cv = 0;				/* current character value */
    int rv = 0;				/* return value */
    int slen;				/* token segment length */
    int done = 0;			/* done indication */
    int i;				/* loop index */

    lct = (LEXClassTab_t *)chtab;

    while (!done) {

	/* Go get more stream data if none left in the buffer */
	if (lst->lst_len <= 0) {
	    rv = (*lst->lst_get)(lst);
	    if (rv <= 0) {
		return rv;
	    }
	}

	slen = 0;
	cp = lst->lst_cp;

	while (slen < lst->lst_len) {
	    cv = *cp;
	    bitmask = cbits;
	    bp = lct->lct_bv + cv * lct->lct_bvbytes;
	    for (i = 0; i < lct->lct_bvbytes; ++i) {
		if (*bp++ & bitmask) goto more_token;
		bitmask >>= 8;
	    }

	    done = 1;
	    break;

	  more_token:
	    slen += 1;
	    cp += 1;
	}

	/* If the current segment is not empty, append it to the token */
	if (slen > 0) {
	    rv = lex_token_append(token, slen, lst->lst_cp);
	    if (rv < 0) break;

	    /* Update the stream buffer pointer and length */
	    lst->lst_cp += slen;
	    lst->lst_len -= slen;
	}
    }

    return ((rv < 0) ? rv : cv);
}

/*
 * Description (lex_scan_string)
 *
 *	This function parses a quoted string into the specified token.
 *	The current character in the LEX stream is taken to be the
 *	beginning quote character.  The quote character may be included
 *	in the string by preceding it with a '\'.  Any newline
 *	characters to be included in the string must also be preceded
 *	by '\'.  The string is terminated by another occurrence of the
 *	quote character, or an unquoted newline, or EOF.
 *
 * Arguments:
 *
 *	lst			- pointer to LEX stream structure
 *	token			- handle for token
 *	flags			- bit flags (unused - must be zero)
 *
 * Returns:
 *
 *	The terminating character is returned, or zero if EOF.  The
 *	string is returned in the token, without the beginning and
 *	ending quote characters.  An error is indicated by a negative
 *	return value.
 */

NSAPI_PUBLIC 
int lex_scan_string(LEXStream_t * lst, void * token, int flags)
{
    char * cp;				/* current pointer in stream buffer */
    int cv;				/* current character value */
    int rv;				/* return value */
    int slen;				/* token segment length */
    int done = 0;			/* done indication */
    int cquote = 0;			/* character quote indication */
    int qchar = -1;			/* quote character */

    while (!done) {

	/* Go get more stream data if none left in the buffer */
	if (lst->lst_len <= 0) {
	    rv = (*lst->lst_get)(lst);
	    if (rv <= 0) {
		return rv;
	    }
	}

	slen = 0;
	cp = lst->lst_cp;

	while (slen < lst->lst_len) {

	    /* Get the next character */
	    cv = *cp;

	    /* Pick up the quote character if we don't have it yet */
	    if (qchar < 0) {
		qchar = cv;

		/* Don't include it in the string */
		lst->lst_cp += 1;
		lst->lst_len -= 1;
		cp += 1;
		continue;
	    }

	    /* cquote is 1 if the last character was '\' */
	    if (cquote == 0) {

		/* Is this a string terminator? */
		if ((cv == qchar) || (cv == '\n')) {

		    /* Append whatever we have to this point */
		    if (slen > 0) goto append_it;

		    /*
		     * If the terminator is the expected quote character,
		     * just skip it.  If it's anything else, leave it as
		     * the current character.
		     */
		    if (cv == qchar) {
			lst->lst_cp += 1;
			lst->lst_len -= 1;
		    }

		    done = 1;
		    goto append_it;
		}

		/* Got the character quote character? */
		if (cv == '\\') {

		    /* Append anything we have so far first */
		    if (slen > 0) goto append_it;

		    /* Then skip the character */
		    cquote = 1;
		    lst->lst_cp += 1;
		    lst->lst_len -= 1;
		    cp += 1;
		    continue;
		}
	    }
	    else {

		/* Include any character following '\' */
		cquote = 0;
	    }

	    /* Include this character in the string */
	    slen += 1;
	    cp += 1;
	}

      append_it:

	/* If the current segment is not empty, append it to the token */
	if (slen > 0) {
	    rv = lex_token_append(token, slen, lst->lst_cp);
	    if (rv < 0) break;

	    /* Update the stream buffer pointer and length */
	    lst->lst_cp += slen;
	    lst->lst_len -= slen;
	}
    }

    return ((rv < 0) ? rv : cv);
}

NSAPI_PUBLIC 
int lex_scan_to(LEXStream_t * lst, void * chtab, unsigned long cbits,
		void * token)
{
    LEXClassTab_t * lct;		/* character class table pointer */
    unsigned char * bp;			/* bit vector pointer */
    char * cp;				/* current pointer in stream buffer */
    unsigned long bitmask;		/* class bit mask temporary */
    int cv = 0;				/* current character value */
    int rv = 0;				/* return value */
    int slen;				/* token segment length */
    int done = 0;			/* done indication */
    int i;				/* loop index */

    lct = (LEXClassTab_t *)chtab;

    while (!done) {

	/* Go get more stream data if none left in the buffer */
	if (lst->lst_len <= 0) {
	    rv = (*lst->lst_get)(lst);
	    if (rv <= 0) {
		return rv;
	    }
	}

	slen = 0;
	cp = lst->lst_cp;

	while (slen < lst->lst_len) {
	    cv = *cp;
	    bitmask = cbits;
	    bp = lct->lct_bv + cv * lct->lct_bvbytes;
	    for (i = 0; i < lct->lct_bvbytes; ++i) {
		if (*bp++ & bitmask) {
		    done = 1;
		    goto append_it;
		}
		bitmask >>= 8;
	    }

	    slen += 1;
	    cp += 1;
	}

      append_it:

	/* If the current segment is not empty, append it to the token */
	if (slen > 0) {
	    rv = lex_token_append(token, slen, lst->lst_cp);
	    if (rv < 0) break;

	    /* Update the stream buffer pointer and length */
	    lst->lst_cp += slen;
	    lst->lst_len -= slen;
	}
    }

    return ((rv < 0) ? rv : cv);
}

NSAPI_PUBLIC 
int lex_skip_over(LEXStream_t * lst, void * chtab, unsigned long cbits)
{
    LEXClassTab_t * lct;		/* character class table pointer */
    unsigned char * bp;			/* bit vector pointer */
    char * cp;				/* current pointer in stream buffer */
    unsigned long bitmask;		/* class bit mask temporary */
    int rv = 0;				/* return value */
    int slen;				/* token segment length */
    int done = 0;			/* done indication */
    int i;				/* loop index */

    lct = (LEXClassTab_t *)chtab;

    while (!done) {

	/* Go get more stream data if none left in the buffer */
	if (lst->lst_len <= 0) {
	    rv = (*lst->lst_get)(lst);
	    if (rv <= 0) {
		return rv;
	    }
	}

	slen = 0;
	cp = lst->lst_cp;

	while (slen < lst->lst_len) {
	    rv = *cp;
	    bitmask = cbits;
	    bp = lct->lct_bv + rv * lct->lct_bvbytes;
	    for (i = 0; i < lct->lct_bvbytes; ++i) {
		if (*bp++ & bitmask) goto next_ch;
		bitmask >>= 8;
	    }

	    done = 1;
	    break;

	  next_ch:
	    slen += 1;
	    cp += 1;
	}

	if (slen > 0) {
	    /* Update the stream buffer pointer and length */
	    lst->lst_cp += slen;
	    lst->lst_len -= slen;
	}
    }

    return rv;
}

NSAPI_PUBLIC 
int lex_skip_to(LEXStream_t * lst, void * chtab, unsigned long cbits)
{
    LEXClassTab_t * lct;		/* character class table pointer */
    unsigned char * bp;			/* bit vector pointer */
    char * cp;				/* current pointer in stream buffer */
    unsigned long bitmask;		/* class bit mask temporary */
    int rv;				/* return value */
    int slen;				/* token segment length */
    int done = 0;			/* done indication */
    int i;				/* loop index */

    lct = (LEXClassTab_t *)chtab;

    while (!done) {

	/* Go get more stream data if none left in the buffer */
	if (lst->lst_len <= 0) {
	    rv = (*lst->lst_get)(lst);
	    if (rv <= 0) {
		return rv;
	    }
	}

	slen = 0;
	cp = lst->lst_cp;

	while (slen < lst->lst_len) {
	    rv = *cp;
	    bitmask = cbits;
	    bp = lct->lct_bv + rv * lct->lct_bvbytes;
	    for (i = 0; i < lct->lct_bvbytes; ++i) {
		if (*bp++ & bitmask) {
		    done = 1;
		    goto update_it;
		}
		bitmask >>= 8;
	    }
	    slen += 1;
	    cp += 1;
	}

      update_it:
	/* Update the stream buffer pointer and length */
	if (slen > 0) {
	    lst->lst_cp += slen;
	    lst->lst_len -= slen;
	}
    }

    return rv;
}
