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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 * collate.c -- routines to collate character strings
 */

#include <stdio.h>
#include "dsgw.h" 
#include <ldap.h> /* ldap_utf8* */

#include <unicode/ucol.h> /* Collation */
#include <unicode/ucnv.h> /* Conversion */
#include <unicode/ustring.h> /* UTF8 conversion */

#ifdef _WINDOWS
#undef strcasecmp
#define strcasecmp _strcmpi
#endif

/*
  Convert the given string s, encoded in UTF8, into a Unicode (UTF16 or 32, depending on sizeof(UChar))
  string for use with collation and key generation
  The given string U will be filled in if it's capacity (given by Ulen) is big enough,
  otherwise, it will be malloced (or realloced if already allocated)
*/
static UErrorCode
SetUnicodeStringFromUTF_8 (UChar** U, int32_t* Ulen, int *isAlloced, const char *s)
    /* Copy the UTF-8 string bv into the UnicodeString U,
       but remove leading and trailing whitespace, and
       convert consecutive whitespaces into a single space.
       Ulen is set to the number of UChars in the array (not necessarily the number of bytes!)
    */
{
    int32_t len = 0; /* length of non-space string */
    int32_t needLen = 0; /* number of bytes needed for string */
    UErrorCode err = U_ZERO_ERROR;
    const char* begin; /* will point to beginning of non-space in s */

    /* first, set s to the first non-space char in bv->bv_val */
    while (s && *s && ldap_utf8isspace((char *)s)) { /* cast away const */
	const char *next = LDAP_UTF8NEXT((char *)s); /* cast away const */
	s = next;
    }
    begin = s;

    if (!s || !*s) {
	return U_INVALID_FORMAT_ERROR; /* don't know what else to use here */
    }

    /* next, find the length of the non-space string */
    while (s && *s && !ldap_utf8isspace((char *)s)) { /* cast away const */
	const char *next = LDAP_UTF8NEXT((char *)s); /* cast away const */
	len += (next - s); /* count bytes, not chars */
	needLen++; /* needLen counts chars */
	s = next;
    }

    if (needLen == 0) { /* bogus */
	return U_INVALID_FORMAT_ERROR; /* don't know what else to use here */
    }

    needLen++; /* +1 for trailing UChar space */
    if (needLen > *Ulen) { /* need more space */
	if (*isAlloced) { /* realloc space */
	    *U = (UChar *)dsgw_ch_realloc((char *)*U, sizeof(UChar) * needLen);
	} else { /* must use malloc */
	    *U = (UChar *)dsgw_ch_malloc(sizeof(UChar) * needLen);
	    *isAlloced = 1; /* no longer using fixed buffer */
	}
	*Ulen = needLen;
    }
    u_strFromUTF8(*U, sizeof(UChar) * (*Ulen), NULL, begin, len, &err);

    return err;
}

static UCollator*
get_collator (int flavor)
{
    static UCollator* collator[2] = {NULL, NULL};
/* dsgw_emitf("get_collator (%i)<br>\n", flavor); */
    if (collator[flavor] == NULL &&
	gc->gc_ClientLanguage && gc->gc_ClientLanguage[0]) {
	/* Try to create a Collation for the client's preferred language */
	ACCEPT_LANGUAGE_LIST langlist;
	size_t langs;
/* dsgw_emitf ("ClientLanguage = \"%s\"<br>\n", gc->gc_ClientLanguage); */
	langs = AcceptLangList (gc->gc_ClientLanguage, langlist);
	if (langs <= 0) {
dsgw_emitf ("AcceptLangList (%s) = %lu<br>\n",
	    gc->gc_ClientLanguage, (unsigned long)langs);
	} else {
	    UCollator* fallback_collator = NULL;
	    UCollator* default_collator = NULL;
	    UErrorCode err = U_ZERO_ERROR;
	    size_t i;

	    for (i = 0; i < langs; ++i) {
		/* Try to create a Collation for langs[i] */
		char* lang = langlist[i];
		collator[flavor] = ucol_open(lang, &err);
		if (err == U_ZERO_ERROR && collator[flavor]) {
dsgw_emitf("<!-- New Collator (%s) == SUCCESS -->\n", lang);
		    break;
		} else {
		    if (err == U_USING_FALLBACK_WARNING) {
			if (fallback_collator == NULL) {
			    fallback_collator = collator[flavor];
dsgw_emitf("<!-- New Collator (%s) == USING_FALLBACK_LOCALE -->\n", lang);
			} else {
			    ucol_close (collator[flavor]);
			}
		    } else if (err == U_USING_DEFAULT_WARNING) {
			if (default_collator == NULL) {
			    default_collator = collator[flavor];
dsgw_emitf("<!-- New Collator (%s) == USING_DEFAULT_LOCALE -->\n", lang);
			} else {
			    ucol_close (collator[flavor]);
			}
		    } else {
dsgw_emitf("New Collator error (%s) == %i<br>\n", lang, err);
		    }
		    collator[flavor] = NULL;
		}
	    }
	    if (collator[flavor] == NULL) {
		if (fallback_collator != NULL) {
		    collator[flavor] = fallback_collator;
		    fallback_collator = NULL;
		} else if (default_collator != NULL) {
		    collator[flavor] = default_collator;
		    default_collator = NULL;
		}
	    }
	    if (collator[flavor] != NULL) {
		switch (flavor) {
		  case CASE_EXACT:
dsgw_emits("<!-- CollationSetStrength (TERTIARY) -->\n");
		    ucol_setAttribute (collator[flavor], UCOL_STRENGTH, UCOL_TERTIARY, &err);
		    break;
		  default: /* CASE_IGNORE */
		    if (dsgw_scriptorder()->so_caseIgnoreAccents) {
dsgw_emits("<!-- CollationSetStrength (PRIMARY) -->\n");
			ucol_setAttribute (collator[flavor], UCOL_STRENGTH, UCOL_PRIMARY, &err);
		    } else {
dsgw_emits("<!-- CollationSetStrength (SECONDARY) -->\n");
			ucol_setAttribute (collator[flavor], UCOL_STRENGTH, UCOL_SECONDARY, &err);
		    }
		    break;
		}
	    }
	    if (default_collator != NULL) {
		ucol_close (default_collator);
		default_collator = NULL;
	    }
	    if (fallback_collator != NULL) {
		ucol_close (fallback_collator);
		fallback_collator = NULL;
	    }
	}
    }
    return collator[flavor];
}

static int
valcmp (const char** L, const char** R)
{
    return strcmp (*L, *R);
}

static int
valcasecmp (const char** L, const char** R)
{
    return strcasecmp (*L, *R);
}

static int
strXcollate (int flavor, const char* L, const char* R)
{
    UCollator* collator = get_collator (flavor);
    if (collator != NULL) {
	UChar LuBuffer[128];
	UChar* Lu = LuBuffer;
	int32_t LuLen = u_strlen(LuBuffer);
	int LuisAlloced = 0;
	if (SetUnicodeStringFromUTF_8 (&Lu, &LuLen, &LuisAlloced, L) == U_ZERO_ERROR) {
	    UChar RuBuffer[128];
	    UChar* Ru = RuBuffer;
	    int32_t RuLen = u_strlen(RuBuffer);
	    int RuisAlloced = 0;
	    if (SetUnicodeStringFromUTF_8 (&Ru, &RuLen, &RuisAlloced, R) == U_ZERO_ERROR) {
		UCollationResult colres = ucol_strcoll(collator, Lu, LuLen, Ru, RuLen);
		int result = 0;
		switch (colres) {
		case UCOL_LESS:
		    result = -1;
		    break;
		case UCOL_GREATER:
		    result = 1;
		    break;
		default:
		    break;
		}
#ifdef DSGW_DEBUG
		{
		    auto char* Le = dsgw_strdup_escaped (L);
		    auto char* Re = dsgw_strdup_escaped (R);
		    dsgw_log ("strXcollate:%s %s %s\n",
			      Le, result < 0 ? "<" : (result == 0 ? "=" : ">"), Re);
		    free (Le);
		    free (Re);
		}
#endif
		if (RuisAlloced) {
		    free(Ru);
		    Ru = NULL;
		}
		if (LuisAlloced) {
		    free(Lu);
		    Lu = NULL;
		}

		return result;
	    }
	    if (LuisAlloced) {
		free(Lu);
		Lu = NULL;
	    }
	}
    }
    return flavor ? strcasecmp (L, R) : strcmp (L, R);
}

static int
strcollate (const char* L, const char* R)
{
    return strXcollate (CASE_EXACT, L, R);
}

static int
strcasecollate (const char* L, const char* R)
{
    return strXcollate (CASE_INSENSITIVE, L, R);
}

static int
valcollate (const char** L, const char** R)
{
    return strXcollate (CASE_EXACT, *L, *R);
}

static int
valcasecollate (const char** L, const char** R)
{
    return strXcollate (CASE_INSENSITIVE, *L, *R);
}

strcmp_t
dsgw_strcmp (int flavor)
{
    if (get_collator (flavor) != NULL) {
	return flavor ? strcasecollate : strcollate;
    }
    return flavor ? strcasecmp : strcmp;
}

valcmp_t
dsgw_valcmp (int flavor)
{
    if (get_collator (flavor) != NULL) {
	return flavor ? valcasecollate : valcollate;
    }
    return flavor ? valcasecmp : valcmp;
}

static size_t
dsgw_scriptof (const char* s, scriptrange_t** ranges)
{
    auto size_t result = 0;
    if (s && ranges) {
	auto unsigned long u;
	while ((u = LDAP_UTF8GETCC (s)) != 0) {
	    auto size_t ss;
	    auto scriptrange_t* sr;
	    for (ss = 0; (sr = ranges[ss]) != NULL; ++ss) {
		do {
		    if (sr->sr_min <= u && u <= sr->sr_max) {
			break;
		    }
		} while ((sr = sr->sr_next) != NULL);
		if (sr) {
		    if (result < ss) result = ss;
		    break;
		}
	    }
	    if (!sr) {
		result = ss;
		break;
	    }
	}
    }
#ifdef DSGW_DEBUG
    dsgw_log ("script %lu\n", (unsigned long)result);
#endif
    return result;
}

static struct berval key_first = {0, 0};
static struct berval key_last  = {0, 0};

struct berval* dsgw_key_first = &key_first;
struct berval* dsgw_key_last  = &key_last;

void LDAP_C LDAP_CALLBACK
dsgw_keyfree( void *arg, const struct berval* key )
{
    if (key->bv_val) free (key->bv_val);
    else if (key == dsgw_key_first || key == dsgw_key_last) return;
    free ((void*)key);
}

int LDAP_C LDAP_CALLBACK
dsgw_keycmp( void *arg, const struct berval *L, const struct berval *R )
{
    int result = 0;
    if (L == R) {
    } else if (L->bv_val == NULL) { /* L is either first or last */
	result = (L == dsgw_key_last) ? 1 : -1;
    } else if (R->bv_val == NULL) { /* R is either first or last */
	result = (R == dsgw_key_last) ? -1 : 1;
    } else
    /* copied from slapi_berval_cmp(), in ../../servers/slapd/plugin.c: */
    if (L->bv_len < R->bv_len) {
	result = memcmp (L->bv_val, R->bv_val, L->bv_len);
	if (result == 0)
	  result = -1;
    } else {
	result = memcmp (L->bv_val, R->bv_val, R->bv_len);
	if (result == 0 && (L->bv_len > R->bv_len))
	  result = 1;
    }
    return result;
}

struct berval*
dsgw_strkeygen (int flavor, const char* s)
{
    auto struct berval* v = (struct berval*)dsgw_ch_malloc (sizeof (struct berval));
    auto UCollator* collator = get_collator (flavor);
    v->bv_val = NULL;
    if (collator != NULL) {
	UChar uBuffer[128];
	UChar* u = uBuffer;
	int32_t uLen = u_strlen(uBuffer);
	int uisAlloced = 0;
	if (SetUnicodeStringFromUTF_8 (&u, &uLen, &uisAlloced, s) == U_ZERO_ERROR) {
	    char keyBuffer[128]; /* try to use static space buffer to avoid malloc */
	    int32_t keyLen = sizeof(keyBuffer);
	    char* key = keyBuffer; /* but key can grow if necessary */
	    int32_t realLen = ucol_getSortKey(collator, u, uLen, (uint8_t *)key, keyLen);
	    if (realLen > keyLen) { /* need more space */
		key = (char*)dsgw_ch_malloc(sizeof(char) * realLen);
		keyLen = ucol_getSortKey(collator, u, uLen, (uint8_t *)key, realLen);
	    }
	    v->bv_len = realLen + 2;
	    v->bv_val = dsgw_ch_malloc (v->bv_len);
	    memcpy(v->bv_val+1, key, realLen);
	    if (uisAlloced) {
		free(u);
		u = NULL;
	    }
	    if (key != keyBuffer) {
		free(key);
		key = NULL;
	    }
	}
    }
    if (v->bv_val == NULL) {
	v->bv_len = (s ? strlen (s) : 0) + 2;
	v->bv_val = dsgw_ch_malloc (v->bv_len);
	if (v->bv_len > 2) memcpy (v->bv_val+1, s, v->bv_len-2);
	if (flavor) {
	    register char* t;
	    for (t = v->bv_val+1; *t; ++t) {
		if (isascii (*t)) *t = tolower (*t);
	    }
	}
    }
    v->bv_val[0] = (char) dsgw_scriptof (s, dsgw_scriptorder()->so_sort);
    v->bv_val[v->bv_len-1] = '\0';
    return v;
}
