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
#ifndef __lexer_h
#define __lexer_h

#ifndef _POOL_H_
#include "base/pool.h"
#endif /* _POOL_H_ */

/* Define error codes */
#define LEXERR_MALLOC	-1		/* insufficient dynamic memory */


typedef struct LEXStream_s LEXStream_t;
typedef int (*LEXStreamGet_t)(LEXStream_t *);
struct LEXStream_s {
    LEXStream_t * lst_next;		/* link for "include" parent stream */
    void * lst_strmid;			/* client stream identifier */
    LEXStreamGet_t lst_get;		/* pointer to stream "get" function */
    char * lst_buf;			/* stream buffer pointer */
    char * lst_cp;			/* current position in buffer */
    int lst_len;			/* remaining bytes in buffer */
    int lst_buflen;			/* buffer length */
    int lst_flags;			/* bit flags */
#define LST_FREEBUF	0x1		/* free lst_buf in stream destroy */
};
NSPR_BEGIN_EXTERN_C

/* Functions in lexer.c */
NSAPI_PUBLIC
int lex_class_check(void * chtab, char code, unsigned long cbits);

NSAPI_PUBLIC
int lex_class_create(int classc, char * classv[], void **pchtab);

NSAPI_PUBLIC void lex_class_destroy(void * chtab);

NSAPI_PUBLIC
LEXStream_t * lex_stream_create(LEXStreamGet_t strmget, void * strmid,
                                char * buf, int buflen);

NSAPI_PUBLIC void lex_stream_destroy(LEXStream_t * lst);

NSAPI_PUBLIC int
lex_token_new(pool_handle_t * pool, int initlen, int growlen, void **token);

NSAPI_PUBLIC int lex_token_start(void * token);

NSAPI_PUBLIC
char * lex_token_info(void * token, int * tdatalen, int * tbufflen);

NSAPI_PUBLIC char * lex_token(void * token);

NSAPI_PUBLIC void lex_token_destroy(void * token);

NSAPI_PUBLIC
char * lex_token_get(void * token, int * tdatalen, int * tbufflen);

NSAPI_PUBLIC char * lex_token_take(void * token);

NSAPI_PUBLIC
int lex_token_append(void * token, int nbytes, char * src);

NSAPI_PUBLIC
int lex_next_char(LEXStream_t * lst, void * chtab, unsigned long cbits);

NSAPI_PUBLIC
int lex_scan_over(LEXStream_t * lst, void * chtab, unsigned long cbits,
			 void * token);

NSAPI_PUBLIC
int lex_scan_string(LEXStream_t * lst, void * token, int flags);

NSAPI_PUBLIC
int lex_scan_to(LEXStream_t * lst, void * chtab, unsigned long cbits,
                void * token);

NSAPI_PUBLIC
int lex_skip_over(LEXStream_t * lst, void * chtab, unsigned long cbits);

NSAPI_PUBLIC
int lex_skip_to(LEXStream_t * lst, void * chtab, unsigned long cbits);

NSPR_END_EXTERN_C

#endif /* __lexer_h */
