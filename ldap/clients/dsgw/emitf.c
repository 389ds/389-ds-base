/** --- BEGIN COPYRIGHT BLOCK ---
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
  --- END COPYRIGHT BLOCK ---  */
#include <stdarg.h> /* va_list etc. */
#include <stdio.h> /* sprintf */
#include <stdlib.h> /* malloc, realloc, free */
#include <string.h> /* strchr, strpbrk etc. */
#include "dsgw.h" /* dsgw_ch_malloc, dsgw_ch_strdup */

typedef void* (*dsgw_producer) (void*, const char*, size_t);

static size_t
produce_fill (dsgw_producer produce, void** parm,
	      size_t fill, unsigned zero)
{
    static const char* zeroes = "00000000";
    static const char* blanks = "        ";
    size_t result = 0;
    while (fill > 0) {
	long n = fill;
	if (n > 8) n = 8;
	if (zero) {
	    *parm = produce (*parm, zeroes, n);
	} else {
	    *parm = produce (*parm, blanks, n);
	}
	if (*parm == NULL) return result;
	result += n;
	fill   -= n;
    }
    return result;
}

#define FLAG_LEFT 1 /* align left */
#define FLAG_ZERO 2 /* zero fill */
#define FLAG_CONST 4

static size_t
produce_string (dsgw_producer produce, void** parm,
		const char* str, unsigned flags, int width, int precision)
{
    size_t fill;
    size_t bytes;
    size_t result = 0;
    if (*parm == NULL) return result;
    if (width < 0) {
	width = - width;
	flags ^= FLAG_LEFT;
    }
    if (width == 0 && precision < 0) {
	fill = 0;
	bytes = strlen (str);
    } else {
	char* s = (char*)str; /* cast away const (for LDAP_UTF8INC) */
	size_t chars = 0;
	while (*s && ((precision < 0) || (chars < precision))) {
	    LDAP_UTF8INC(s);
	    ++chars;
	}
	fill = (width > chars) ? (width - chars) : 0;
	bytes = (s - str);
    }
    if (fill && ! (flags & FLAG_LEFT)) {
	result += produce_fill (produce, parm, fill, flags & FLAG_ZERO);
    }
    if (bytes) {
	*parm = produce (*parm, str, bytes);
	if (*parm == NULL) return result;
	result += bytes;
    }
    if (fill && (flags & FLAG_LEFT)) {
	result += produce_fill (produce, parm, fill, flags & FLAG_ZERO);
    }
    return result;
}

static const char* type_chars = "%dioxXueEgGfcsp";

static size_t
count_slots (const char* s)
{
    size_t n = 0;
    while ((s = strchr (s, '%')) != NULL) {
	const char* l = strpbrk (s+1, type_chars);
	const char* c;
	if (l == NULL) {
	    n += 3;
	    break;
	}
	++n;
	for (c = s+1; c != l; ++c) {
	    if (*c == '*') ++n;
	}
	s = *l ? l+1 : l;
    }
    return n;
}

typedef struct {
    char type;
#define TYPE_I  0
#define TYPE_U  1
#define TYPE_F  2
#define TYPE_LI 3
#define TYPE_LU 4
#define TYPE_LF 5
#define TYPE_S  6
#define TYPE_P  7
#define TYPE_PERCENT    8 /* e.g. %% */
#define TYPE_WIDTH      9
#define TYPE_PRECISION 10

    unsigned char flags;
    int arg; /* An index into an array of dsgw_arg_t,
		or (if flags & FLAG_CONST) the width or precision value. */
} dsgw_slot_t;

typedef union {
    int            i;
    unsigned int   u;
    double         f;
    long          li;
    unsigned long lu;
    long double   lf;
    const char*    s;
    void*          p;
} dsgw_arg_t;

#define DEFSLOTC 8 /* A format string rarely contains more slots. */
#define DEFFMTC 16 /* A single format rarely contains more chars. */

static size_t
dsgw_vxprintf (dsgw_producer produce, void* parm,
	       const char* format, va_list argl)
     /* This function works like vsprintf(), except it:
	- supports parameter reordering, using %posp$.
	- is UTF8-aware.
	- delivers output by calling the function 'produce'.
	- returns the total number of bytes produced.
	This function interprets all string parameters as UTF8.
     */
{
    size_t result = 0; /* total number of bytes produced */

    /* Each place that 'format' refers to an argument is called a 'slot'. */
    dsgw_slot_t defslot[DEFSLOTC];
    dsgw_slot_t* slot = defslot; /* in order of their appearance in format */
    dsgw_slot_t* islot = NULL; /* next slot to process */
    dsgw_slot_t* aslot = NULL; /* another cursor */

    dsgw_arg_t defargv[DEFSLOTC];
    dsgw_arg_t* argv = defargv; /* in order of their appearance in argl */
    size_t argi = 0; /* index of next argument (in argl/argv) */

    char deffmt[DEFFMTC];
    char* fmt = deffmt;
    size_t fmtc = sizeof(deffmt);

    const char* next;
    const char* f;

    char buf [1024] = {0};
    int i;

    i = count_slots (format);
/*fprintf (stderr, "slots: %i\n", i);*/
    if (i > DEFSLOTC) { /* defslot isn't big enough. */
	slot = (dsgw_slot_t*) malloc (i * sizeof(dsgw_slot_t));
    }

    /* get slot types from format: */
    islot = slot;
    next = format;
    while ((f = strchr (next, '%')) != NULL) {
	const char* l = f+1;
	unsigned flags = 0;
	int number = -1;
	char size;

	if (*l >= '1' && *l <= '9') {
	    number = 0;
	    do { number = (number * 10) + (*l++ - '0');
	    } while (*l >= '0' && *l <= '9');
	}
	if (*l == '$') {
	    ++l;
	    if (number > 0) {
		argi = number - 1;
	    }
	    number = -1;
	}
	if (number >= 0) { /* width */
	    islot->arg = number;
	    flags |= FLAG_CONST;
	} else {
	    while (1) { /* flags */
		switch (*l) {
		  case '-': flags |= FLAG_LEFT; ++l; continue;
		  case '0': flags |= FLAG_ZERO; ++l; continue;
		  case '+':
		  case ' ':
		  case '#': ++l; continue;
		  default: break;
		}
		break;
	    }
	    if (*l == '*') { /* width */
		number = 0;
		++l;
		islot->arg = argi++;
	    } else if (*l >= '1' && *l <= '9') { /* width */
		number = 0;
		do { number = (number * 10) + (*l++ - '0');
		} while (*l >= '0' && *l <= '9');
		islot->arg = number;
		flags |= FLAG_CONST;
	    }
	}
	if (number >= 0) {
	    islot->type  = TYPE_WIDTH;
	    islot->flags = flags;
	    flags &= ~ FLAG_CONST;
	    ++islot;
	}
	if (*l == '.') {
	    islot->type = TYPE_PRECISION;
	    ++l;
	    if (*l == '*') {
		++l;
		islot->arg = argi++;
		islot->flags = 0;
	    } else {
		number = 0;
		while (*l >= '0' && *l <= '9')
		  number = (number * 10) + (*l++ - '0');
		islot->arg = number;
		islot->flags = FLAG_CONST;
	    } 
	    ++islot;
	}
	switch (*l) { /* size modifier */
	  case 'h':
	  case 'l':
	  case 'L': size = *l++; break;
	  default:  size = '\0';
	}
	islot->flags = 0;
	switch (*l) {	/* type */
	  case 'd':
	  case 'i': islot->type = (size == 'l') ? TYPE_LI : TYPE_I; break;
	  case 'o':
	  case 'x': case 'X':
	  case 'u': islot->type = (size == 'l') ? TYPE_LU : TYPE_U; break;
	  case 'e': case 'E':
	  case 'g': case 'G':
	  case 'f': islot->type = (size == 'L') ? TYPE_LF : TYPE_F; break;
	  case 'c': islot->type = TYPE_I; break;
	  case 's': islot->type = TYPE_S; break;
	  case 'p': islot->type = TYPE_P; break;
	  case '%': islot->type = TYPE_PERCENT;
	            islot->flags = FLAG_CONST; break;
	  default: /* unknown type */
	    goto bail; /* don't produce anything. */
	    /* It might be more helpful to produce the slots up to
	       this one, and maybe output this format substring, too.
	       That way, someone reading the output might get a clue
	       what went wrong.
	       */
	}
	if (islot->type != TYPE_PERCENT) {
	    islot->arg = argi++;
	}
	++islot;
	next = *l ? l+1 : l;
    }

    /* argi = the length of argl/argv: */
    argi = 0;
    for (aslot = slot; aslot != islot; ++aslot) {
	if (argi <= aslot->arg && ! (aslot->flags & FLAG_CONST)) {
	    argi  = aslot->arg + 1;
	}
    }
    if (argi > DEFSLOTC) { /* defargv isn't big enough */
	argv = (dsgw_arg_t*) malloc (argi * sizeof(dsgw_arg_t));
    }

    /* copy arguments from argl to argv: */
/*fprintf (stderr, "slot:type:value:");*/
    for (i = 0; i < argi; ++i) {
	for (aslot = slot; aslot != islot; ++aslot) {
	    if ( ! (aslot->flags & FLAG_CONST) && aslot->arg == i) {
		break;
	    }
	}
	if (aslot == islot) { /* No slot refers to this arg. */
	    if (va_arg (argl, const char*)); /* Skip over it. */
	} else {
/*fprintf (stderr, " %i:%i", (int)(aslot-slot), aslot->type);*/
	    switch (aslot->type) {
	      case TYPE_U:  argv[i].u  = va_arg (argl, unsigned); break;
	      case TYPE_F:  argv[i].f  = va_arg (argl, double); break;
	      case TYPE_LI: argv[i].li = va_arg (argl, long); break;
	      case TYPE_LU: argv[i].lu = va_arg (argl, unsigned long); break;
	      case TYPE_LF: argv[i].lf = va_arg (argl, long double); break;
	      case TYPE_P:  argv[i].p  = va_arg (argl, void*); break;
	      case TYPE_S:  argv[i].s  = va_arg (argl, const char*);
/*fprintf (stderr, ":\"%s\"", argv[i].s);*/
		break;
	      case TYPE_PERCENT: break; /* no arg */
	      case TYPE_WIDTH:
	      case TYPE_PRECISION:
	      case TYPE_I:  argv[i].i  = va_arg (argl, int);
/*fprintf (stderr, ":%i", argv[i].i);*/
		do {
		    switch (aslot->type) {
		      case TYPE_WIDTH:
		      case TYPE_PRECISION:
			if ( ! (aslot->flags & FLAG_CONST) && aslot->arg == i) {
			    aslot->arg = argv[i].i;
			    aslot->flags |= FLAG_CONST;
			}
			break;
		      default: break;
		    }
		} while (++aslot != islot);
		break;
	    }
	}
    }
/*fprintf (stderr, "\n");*/

    /* produce output: */
    islot = slot;
    next = format;
    while (parm && (f = strchr (next, '%'))) {
	const char* l = strpbrk (f+1, type_chars);
	if (l == NULL) {
	    break;
	}
	if (parm && f != next) { /* produce the substring next..f-1 */
	    const size_t n = (f - next);
	    parm = produce (parm, next, n);
	    if (parm) result += n;
	}
	next = l + 1;
	{ /* fmt = f..l */
	    const char* dollar;
	    const size_t fc = (next - f);
	    if (fmtc <= fc) {
		fmtc = fc + 1;
		if (fmt == deffmt) fmt = malloc (fmtc);
		else               fmt = realloc (fmt, fmtc);
	    }
	    memcpy (fmt, f, fc);
	    fmt[fc] = '\0';
	    if ((dollar = strchr (fmt, '$')) != NULL) {
		/* remove posp$ from the beginning of fmt */
		memmove (fmt + 1, dollar + 1, fc - (dollar - fmt));
	    }
/*fprintf (stderr, "fmt: \"%s\"\n", fmt);*/
	}
	/* produce a single argument */
	switch (islot->type) {
	  case TYPE_I:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].i); break;
	  case TYPE_U:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].u); break;
	  case TYPE_F:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].f); break;
	  case TYPE_LI: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].li); break;
	  case TYPE_LU: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].lu); break;
	  case TYPE_LF: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].lf); break;
	  case TYPE_P:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].p); break;
	  case TYPE_WIDTH:
	  case TYPE_PRECISION:
	    switch ((++islot)->type) {
	      case TYPE_I:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].i); break;
	      case TYPE_U:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].u); break;
	      case TYPE_F:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].f); break;
	      case TYPE_LI: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].li); break;
	      case TYPE_LU: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].lu); break;
	      case TYPE_LF: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].lf); break;
	      case TYPE_P:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].p); break;
	      case TYPE_WIDTH:
	      case TYPE_PRECISION:
		switch ((++islot)->type) {
		  case TYPE_I:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].i); break;
		  case TYPE_U:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].u); break;
		  case TYPE_F:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].f); break;
		  case TYPE_LI: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].li); break;
		  case TYPE_LU: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].lu); break;
		  case TYPE_LF: PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].lf); break;
		  case TYPE_P:  PR_snprintf (buf, sizeof(buf), fmt, argv[islot->arg].p); break;
		  case TYPE_WIDTH:
		  case TYPE_PRECISION: goto bail; /* how did this happen? */
		  case TYPE_PERCENT:
		  case TYPE_S: /* with width and precision */
		    result += produce_string (produce, &parm,
					      (islot->type == TYPE_S) ? argv[islot->arg].s : "%",
					      islot[-2].flags, islot[-2].arg, islot[-1].arg);
		    goto skip_buf;
		}
		break;
	      case TYPE_PERCENT:
	      case TYPE_S: /* with width or precision (not both) */
		if (islot[-1].type == TYPE_WIDTH) {
		    result += produce_string (produce, &parm,
					      (islot->type == TYPE_S) ? argv[islot->arg].s : "%",
					      islot[-1].flags, islot[-1].arg, -1);
		} else {
		    result += produce_string (produce, &parm,
					      (islot->type == TYPE_S) ? argv[islot->arg].s : "%",
					      0, 0, islot[-1].arg);
		}
		goto skip_buf;
	    }
	    break;
	  case TYPE_PERCENT:
	  case TYPE_S: /* with neither width nor precision */
	    result += produce_string (produce, &parm,
				      (islot->type == TYPE_S) ? argv[islot->arg].s : "%",
				      0, 0, -1);
	    goto skip_buf;
	}
	if (parm && *buf) { /* produce buf */
	    const size_t n = strlen (buf);
	    parm = produce (parm, buf, n);
	    if (parm) result += n;
	}
      skip_buf:
	++islot;
    }
    if (parm && *next) { /* produce the remainder of format */
	const size_t n = strlen (next);
	parm = produce (parm, next, n);
	if (parm) result += n;
    }

  bail:
    if (fmt != deffmt) free (fmt);
    if (argv != defargv) free (argv);
    if (slot != defslot) free (slot);
/*fprintf (stderr, "------\n");*/
    return result;
}

size_t
dsgw_fputn (FILE* f, const char* s, size_t n)
{
    auto const size_t result =
      fwrite (s, sizeof(char), n, f);
    dsgw_log_out (s, result);
    return result;
}

static const char*
strnbrk (const char* str, size_t n, const char* brk)
{
    for (; n > 0; ++str, --n) {
	if (strchr (brk, *str)) {
	    return str;
	}
    }
    return NULL;
}

static int quotation_depth = 0;
static int quotation_type[4]; /* maximum depth */
#define QUOTATION_JAVASCRIPT_ENDOFLINE 1

static size_t
dsgw_emitr (int depth, const char* s, size_t n)
{
    static const char* linebreak = "' +\n'";
    static const size_t linebreak_len = 5;
    auto size_t result = 0;
    if (n == 0) {
	return 0;
    } else if (depth == 0) {
	return dsgw_fputn (stdout, s, n);
    }
    --depth;
    switch (quotation_type[depth]) {
      case QUOTATION_JAVASCRIPT:
      case QUOTATION_JAVASCRIPT_MULTILINE:
      case QUOTATION_JAVASCRIPT_ENDOFLINE:
	{
	    auto const char* t;
	    for (t = s; (t = strnbrk (t, n, "'\\\n")) != NULL; ++t) {
		switch (*t) {
		  case '\n': /* output \n */
		    if (t != s) {
			if (quotation_type[depth] == QUOTATION_JAVASCRIPT_ENDOFLINE) {
			    dsgw_emitr (depth, linebreak, linebreak_len);
			}
			result += dsgw_emitr (depth, s, t - s);
		    }
		    if (dsgw_emitr (depth, "\\n", 2) > 1) ++result;
		    if (quotation_type[depth] == QUOTATION_JAVASCRIPT_MULTILINE) {
			quotation_type[depth] = QUOTATION_JAVASCRIPT_ENDOFLINE;
		    }
		    break;
		  default: /* insert \ */
		    if (quotation_type[depth] == QUOTATION_JAVASCRIPT_ENDOFLINE) {
			quotation_type[depth] = QUOTATION_JAVASCRIPT_MULTILINE;
			dsgw_emitr (depth, linebreak, linebreak_len);
		    }
		    result += dsgw_emitr (depth, s, t - s);
		    dsgw_emitr (depth, "\\", 1);
		    result += dsgw_emitr (depth, t, 1);
		    break;
		}
		n -= (t - s) + 1;
		s = t + 1;
	    }
	}
	if (n > 0 &&
	    quotation_type[depth] == QUOTATION_JAVASCRIPT_ENDOFLINE) {
	    quotation_type[depth] = QUOTATION_JAVASCRIPT_MULTILINE;
	    dsgw_emitr (depth, linebreak, linebreak_len);
	}
	break;
      default:
	break;
    }
    if (n > 0) {
	result += dsgw_emitr (depth, s, n);
    }
    return result;
}

static size_t
dsgw_emitq (FILE* f, const char* s, size_t n)
{
    if (f == stdout && quotation_depth > 0) {
	return dsgw_emitr (quotation_depth, s, n);
    }
    return dsgw_fputn (f, s, n);
}

void
dsgw_quotation_begin (int kind)
{
    if (quotation_depth >= 4) exit (4);
    switch (kind) {
      case QUOTATION_JAVASCRIPT:
      case QUOTATION_JAVASCRIPT_MULTILINE:
	dsgw_emitq (stdout, "'", 1);
	break;
      default:
	break;
    }
    quotation_type[quotation_depth++] = kind;
}

void
dsgw_quotation_end()
{
    if (quotation_depth > 0) switch (quotation_type[--quotation_depth]) {
      case QUOTATION_JAVASCRIPT:
      case QUOTATION_JAVASCRIPT_MULTILINE:
      case QUOTATION_JAVASCRIPT_ENDOFLINE:
	dsgw_emitq (stdout, "'", 1);
	break;
      default:
	break;
    }
}

int
dsgw_quote_emits (int kind, const char* s)
{
    int result;
    dsgw_quotation_begin (kind);
    result = dsgw_emits (s);
    dsgw_quotation_end();
    return result;
}

int
dsgw_quote_emitf (int kind, const char* format, ...)
{
    int result;
    va_list argl;
    va_start (argl, format);
    dsgw_quotation_begin (kind);
    result = dsgw_emitfv (format, argl);
    dsgw_quotation_end();
    va_end (argl);
    return result;
}

static UConverter* emit_converter = NULL;

/* given string is utf8 - emit_converter converts given string
   to some natural language encoding requested by the client */
void*
dsgw_emitn (void* parm, const char* s, size_t n)
{
    if (emit_converter == NULL) {
	if (dsgw_emitq ((FILE*)parm, s, n) != n) {
	    return NULL;
	}
    } else {
#define CONVERT_BUFSIZE 2048
	char buf [CONVERT_BUFSIZE]; /* faster than malloc/free */
	char *bufptr = buf;
	size_t len = 0;
	size_t slen = 0;
	UErrorCode err = U_ZERO_ERROR;
	int result;

	do {
	    bufptr = buf; /* reset to beginning of buf */
	    s += slen; /* advance pointer to next unconverted chars */
	    /* convert as many chars from s as will fit in buf */
	    result = dsgw_convert(DSGW_FROM_UTF8, emit_converter,
				  &bufptr, sizeof(buf), &len,
				  s, n, &slen, &err);
	    /* write the converted chars to the output */
	    n = dsgw_emitq ((FILE*)parm, buf, len);
	} while ((result == 0) && (n == len));

	ucnv_reset (emit_converter);
	if (n != len) {
	    return NULL;
	}
    }
    return parm;
}

int
dsgw_emits (const char* s)
     /* This function works like fputs(s, stdout), except it
	converts from UTF8 to the client's preferred charset.
     */
{
    size_t n = strlen (s);
    if (n > 0 && dsgw_emitn (stdout, s, n) == NULL) {
	return EOF;
    }
    return n;
}

int
dsgw_emitfv (const char* format, va_list argl)
     /* This function works like vprintf(), except it:
	- supports parameter reordering, using %posp$.
	- is UTF8-aware.
	- converts to the client's preferred charset.
	This function interprets all string parameters as UTF8.
     */
{
    return( dsgw_vxprintf (dsgw_emitn, stdout, format, argl));
}

int
dsgw_emitf (const char* format, ...)
{
    int	rc;

    va_list argl;
    va_start (argl, format);
    rc = dsgw_emitfv (format, argl);
    va_end (argl);

    return( rc );
}

typedef struct struct_item_t {
    char* i_val;
    double i_q;
} item_t;

static size_t
list_count (const char* list)
{
    const char* s;
    size_t n = 1;
    if (list == NULL || *list == '\0') return 0;
    for (s = list - 1; (s = strchr (s + 1, ',')) != NULL; ++n);
    return n;
}

static item_t*
list_parse (char* slist, size_t items)
{
    char* s = slist;
    item_t* item;
    size_t i = 0;
    if (items <= 0) return NULL;
    item = (item_t*) dsgw_ch_malloc (items * sizeof(item_t));
    while (ldap_utf8isspace (s)) LDAP_UTF8INC(s);
    while (s && *s) {
	if (i >= items) exit (1);
	item[i].i_q = 1.0;
	item[i++].i_val = s;
	if ((s = strchr (s, ',')) != NULL) {
	    *s = '\0';
	    while (ldap_utf8isspace (LDAP_UTF8INC(s)));
	}
    }
    if (i != items) exit (1);
    for (i = 0; i < items; ++i) {
	if ((s = strchr (item[i].i_val, ';')) != NULL) {
	    *s = '\0';
	    do {
		while (ldap_utf8isspace (LDAP_UTF8INC(s)));
		if (*s == 'q' || *s == 'Q') {
		    while (ldap_utf8isspace (LDAP_UTF8INC(s)));
		    if (*s == '=') {
			item[i].i_q = strtod(++s, &s);
		    }
		}
	    } while ((s = strchr (s, ';')) != NULL);
	}
	/* Remove trailing whitespace from item[i].i_val: */
	s = item[i].i_val;
	s += strlen (s);
	while (ldap_utf8isspace (LDAP_UTF8DEC(s)));
	s[1] = '\0';
/*printf("%s;q=%.2f\n", item[i].i_val, item[i].i_q);*/
    }
    return item;
}

static void
list_sort (item_t item[], size_t items)
{
    /* This implementation is suboptimal, but adequate. */
    int sorted;
    size_t i;
    do {
	sorted = 1;
	for (i = 0; i+1 < items ; ++i) {
	    if (item[i].i_q < item[i+1].i_q) { /* swap i & i+1 */
		auto item_t temp;
		memcpy (&temp, &item[i], sizeof(item_t));
		memcpy (&item[i], &item[i+1], sizeof(item_t));
		memcpy (&item[i+1], &temp, sizeof(item_t));
		sorted = 0;
	    }
	}
    } while ( ! sorted);
}

int
is_UTF_8 (const char* charset)
{
    return charset != NULL &&
      (!strcasecmp (charset, UNICODE_ENCODING_UTF_8) ||
       !strcasecmp (charset, "UNICODE-1-1-UTF-8"));
}

static int
charset_is_supported (char* s)
{
    UConverter* converter;
    UErrorCode err = U_ZERO_ERROR;
    if (is_UTF_8 (s)) {
	return 1;
    }
    converter = ucnv_open (s, &err);
    if (err == U_ZERO_ERROR) {
	ucnv_close (converter);
	return 1;
    }
    return 0;
}

static char*
choose_charset (char* slist)
     /* Return the best charset from the given list. */
{
    const size_t items = list_count (slist);
    char* sbuf;
    item_t* item;
    size_t i;

    if (items <= 0) return slist;
    sbuf = dsgw_ch_strdup (slist);
    item = list_parse (sbuf, items);
    for (i = 0; i < items; ++i) {
	if (is_UTF_8 (item[i].i_val)) {
	    break; /* choose this one */
	}
    }
    if (i >= items) {
	list_sort (item, items);
	for (i = 0; i < items; ++i) {
	    auto char* charset = item[i].i_val;
	    if (!strcmp ("*", charset)) {
		i = items; /* choose UTF_8 */
	    } else if (charset_is_supported (charset)) {
		break; /* choose this one */
	    }
	}
    }
    if (i >= items) {
	strcpy (sbuf, UNICODE_ENCODING_UTF_8);
    } else if (sbuf != item[i].i_val) {
	memmove (sbuf, item[i].i_val, strlen(item[i].i_val) + 1);
    }
    free (item);
    return sbuf;
}

char*
dsgw_emit_converts_to (char* charset)
{
    const char* target;
    if (emit_converter != NULL) {
	ucnv_close (emit_converter);
	emit_converter = NULL;
    }
    if (charset) charset = choose_charset (charset);
    if (charset && *charset) {
	target = charset;
    } else {
	target = ISO_8859_1_ENCODING;
    }
    if ( ! is_UTF_8 (target)) {
	UErrorCode err = U_ZERO_ERROR;
	emit_converter = ucnv_open(target, &err);
	if (err != U_ZERO_ERROR) {
	    emit_converter = NULL;
	    charset = UNICODE_ENCODING_UTF_8;
	}
    }
    return charset;
}
