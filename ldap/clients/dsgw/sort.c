/** --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
  --- END COPYRIGHT BLOCK ---  */

/* DON'T SHIP THIS PROGRAM.  It's terribly un-secure, as it
   enables an HTTP client to read the contents of any file.
*/

/* This is a Gateway CGI program, for testing collation.
   It reads the text file named by $PATH_INFO and outputs its lines, sorted,
   in a table with the script and collation key computed by dsgw_strkeygen.
   The locale is controlled by the Accept-Language header in the HTTP request,
   like any Gateway CGI.
*/

#include <errno.h>
#include <stdio.h> /* fopen, fgets, perror */
#include <stdlib.h> /* getenv, qsort */
#include "dsgw.h"

static const char*
fgetln(FILE* f, int* error)
{
    auto size_t buflen = 128;
    auto char* buf = dsgw_ch_malloc (buflen);
    *buf = '\0';
    while (fgets (buf, buflen, f)) {
	auto const size_t read = strlen(buf);
	if (buf[read-1] == '\n') {
	    buf[read-1] = '\0';
	    return buf;
	}
	buflen *= 2;
	buf = dsgw_ch_realloc (buf, buflen);
    }
    if (feof(f) && *buf) return buf;
    free (buf);
    return NULL;
}

typedef struct keystring {
    const char* ks_val;
    struct berval* ks_key;
} keystring_t;

static int
keystring_cmp (const void* Lv, const void* Rv)
{
    auto const keystring_t** L = (const keystring_t**)Lv;
    auto const keystring_t** R = (const keystring_t**)Rv;
    return dsgw_keycmp (NULL, (*L)->ks_key, (*R)->ks_key);
}

int
main( int argc, char* argv[] )
{
    auto int error = 0;
    auto const int reqmethod = dsgw_init (argc, argv, DSGW_METHOD_GET);
    auto char* fname = getenv ("PATH_INFO");

    dsgw_send_header();
    dsgw_emits ("<HTML>\n");
    dsgw_head_begin();
    dsgw_emits ("\n</head>\n<body>\n");

    if (!fname) {
	dsgw_emits ("!PATH_INFO\n");
	error = 1;
    } else {
	auto FILE* f = fopen (fname, "r");
	if (!f) {
	    dsgw_emitf ("%s: errno %i\n", fname, errno);
	    error = 2;
	} else {
	    auto const char* line;
	    auto keystring_t* v = NULL;
	    auto size_t vlen = 0;
	    while (line = fgetln(f, &error)) {
		v = (keystring_t*) dsgw_ch_realloc (v, (vlen+1) * sizeof(keystring_t));
		v[vlen].ks_val = line;
		v[vlen].ks_key = dsgw_strkeygen (CASE_INSENSITIVE, line);
		++vlen;
	    }
	    fclose (f);
	    if (vlen) {
		auto keystring_t** vp;
		auto size_t i;
		vp = (keystring_t**) dsgw_ch_malloc (vlen * sizeof(keystring_t*));
		for (i = 0; i < vlen; ++i) {
		    vp[i] = v + i;
		}

		qsort (vp, vlen, sizeof(keystring_t*), keystring_cmp);

		dsgw_emits ("<table align=left cols=5>\n");
		dsgw_emits ("    <tr>"
			    "<th width=20>" DSGW_UTF8_NBSP "</th>"
			    "<th align=left>line</th>"
			    "<th width=25>script</th>"
			    "<th width=20>" DSGW_UTF8_NBSP "</th>"
			    "<th align=left>Sort Key</th>"
			    "</tr>\n");
		for (i = 0; i < vlen; ++i) {
		    auto size_t j;
		    dsgw_emits ("    <tr valign=baseline>");
		    dsgw_emitf ("<th align=right>%lu:</th>", 1 + (unsigned long)(vp[i]-v));
		    dsgw_emitf ("<td>%s</td>", vp[i]->ks_val);
		    dsgw_emits ("<td align=center>");
		    if (vp[i]->ks_key->bv_len) {
			dsgw_emitf ("%u", 0xFF & (unsigned)(vp[i]->ks_key->bv_val[0]));
		    } else {
			dsgw_emits (DSGW_UTF8_NBSP);
		    }
		    dsgw_emits ("</td>");
		    dsgw_emitf ("<td align=right>%lu:</td>", (unsigned long)(vp[i]->ks_key->bv_len) - 2);
		    dsgw_emits ("<td><font size=\"-2\">");
		    for (j = 1; j < vp[i]->ks_key->bv_len - 1; ++j) {
			dsgw_emitf ("%02x", 0xFF & (unsigned)(vp[i]->ks_key->bv_val[j]));
		    }
		    dsgw_emits ("</font></td>");
		    dsgw_emits ("</tr>\n");
		}
		dsgw_emits ("</table>\n");
		free (vp);
		for (i = 0; i < vlen; ++i) {
		    dsgw_keyfree (NULL, v[i].ks_key);
		}
		free (v);
	    }
	}
    }
    dsgw_emits ("</body></HTML>\n");
    return error;
}
