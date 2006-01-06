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
/* collate.c - implementation of indexing, using a Collation */

#include "collate.h"
#include <string.h> /* memcpy */

#include <unicode/ucol.h> /* Collation */
#include <unicode/ucnv.h> /* Conversion */
#include <unicode/ustring.h> /* UTF8 conversion */

#include <ldap.h> /* LDAP_UTF8LEN */
#include <slap.h> /* for strcasecmp on non-UNIX platforms and correct debug macro */

void
collation_init( char *configpath )
    /* Called once per process, to initialize globals. */
{
	/* ICU needs no initialization? */
}

typedef struct coll_profile_t { /* Collator characteristics */
    const char* language;
    const char* country;
    const char* variant;
    UColAttributeValue strength; /* one of UCOL_PRIMARY = 0, UCOL_SECONDARY = 1, UCOL_TERTIARY = 2, UCOL_QUATERNARY = 3, UCOL_IDENTICAL = 4 */
    UColAttributeValue decomposition; /* one of UCOL_OFF = 0, UCOL_DEFAULT = 1, UCOL_ON = 2 */
} coll_profile_t;

typedef struct coll_id_t { /* associates an OID with a coll_profile_t */
    char* oid;
    coll_profile_t* profile;
} coll_id_t;

/* A list of all OIDs that identify collator profiles: */
static const coll_id_t** collation_id = NULL;
static size_t            collation_ids = 0;

int
collation_config (size_t cargc, char** cargv,
		  const char* fname, size_t lineno)
    /* Process one line from a configuration file.
       Return 0 if it's OK, -1 if it's not recognized.
       Any other return value is a process exit code.
    */
{
    if (cargc <= 0) { /* Bizarre.  Oh, well... */
    } else if (!strcasecmp (cargv[0], "NLS")) {
	/* ignore - not needed anymore with ICU - was used to get path for NLS_Initialize */
    } else if (!strcasecmp (cargv[0], "collation")) {
	if ( cargc < 7 ) {
	    LDAPDebug (LDAP_DEBUG_ANY,
		       "%s: line %lu ignored: only %lu arguments (expected "
		       "collation language country variant strength decomposition oid ...)\n",
		       fname, (unsigned long)lineno, (unsigned long)cargc );
	} else {
	    auto size_t arg;
	    auto coll_profile_t* profile = (coll_profile_t*) slapi_ch_calloc (1, sizeof (coll_profile_t));
	    if (*cargv[1]) profile->language = slapi_ch_strdup (cargv[1]);
	    if (*cargv[2]) profile->country  = slapi_ch_strdup (cargv[2]);
	    if (*cargv[3]) profile->variant  = slapi_ch_strdup (cargv[3]);
	    switch (atoi(cargv[4])) {
	      case 1: profile->strength = UCOL_PRIMARY; break;
	      case 2: profile->strength = UCOL_SECONDARY; /* no break here? fall through? wtf? */
	      case 3: profile->strength = UCOL_TERTIARY; break;
	      case 4: profile->strength = UCOL_IDENTICAL; break;
	      default: profile->strength = UCOL_SECONDARY;
 		LDAPDebug (LDAP_DEBUG_ANY,
			   "%s: line %lu: strength \"%s\" not supported (will use 2)\n",
			   fname, (unsigned long)lineno, cargv[4]);
		break;
	    }
	    switch (atoi(cargv[5])) {
	      case 1: profile->decomposition = UCOL_OFF; break;
	      case 2: profile->decomposition = UCOL_DEFAULT; /* no break here? fall through? wtf? */
	      case 3: profile->decomposition = UCOL_ON; break;
	      default: profile->decomposition = UCOL_DEFAULT;
		LDAPDebug (LDAP_DEBUG_ANY,
			   "%s: line %lu: decomposition \"%s\" not supported (will use 2)\n",
			   fname, (unsigned long)lineno, cargv[5]);
		break;
	    }

            {
                char descStr[256];
                char nameOrder[256];
                char nameSubstring[256];
                char oidString[256];
                char *tmpStr=NULL;
                Slapi_MatchingRuleEntry *mrentry=slapi_matchingrule_new();
 
                if(UCOL_PRIMARY == profile->strength) {
                    strcpy(nameOrder,"caseIgnoreOrderingMatch");
                    strcpy(nameSubstring,"caseIgnoreSubstringMatch");
                }
                else {
                    strcpy(nameOrder,"caseExactOrderingMatch");
                    strcpy(nameSubstring,"caseExactSubstringMatch");
                }

		/* PAR: this looks broken
                   the "extra" text based oids that are actually used
                   to form the name and description are always derived
                   from the language and country fields so there should
                   be no need to have two separate code paths to
                   set the name and description fields of the schema
                   as language is always available, and if country is
                   not, it is not in the name anyway.

                   Is it safe to assume all matching rules will follow
                   this convention?  The answer, or lack of it, probably
                   explains the reasoning for doing things the way they
                   are currently.
                */ 
                    
                if(cargc > 7) {
                    PL_strcatn(nameOrder,sizeof(nameOrder),"-");
                    PL_strcatn(nameOrder,sizeof(nameOrder),cargv[7]);
                    PL_strcatn(nameSubstring,sizeof(nameSubstring),"-");
                    PL_strcatn(nameSubstring,sizeof(nameSubstring),cargv[7]);
                    slapi_matchingrule_set(mrentry,SLAPI_MATCHINGRULE_NAME,
                                           (void *)slapi_ch_strdup(nameOrder));
                }
                else  {
                    if(0 != cargv[1][0]) {
                        PL_strcatn(nameOrder,sizeof(nameOrder),"-");
                        PL_strcatn(nameSubstring,sizeof(nameSubstring),"-");
                    } else {
						nameOrder[0] = 0;
						nameSubstring[0] = 0;
					}
                    PL_strcatn(nameOrder,sizeof(nameOrder),cargv[1]);
                    PL_strcatn(nameSubstring,sizeof(nameSubstring),cargv[1]);
                    slapi_matchingrule_set(mrentry,SLAPI_MATCHINGRULE_NAME,
                                           (void *)slapi_ch_strdup(nameOrder));
                }
                PL_strncpyz(oidString,cargv[6], sizeof(oidString));
                slapi_matchingrule_set(mrentry,SLAPI_MATCHINGRULE_OID,
                                       (void *)slapi_ch_strdup(oidString));
                if(0 != cargv[2][0]) {
                    PR_snprintf(descStr, sizeof(descStr), "%s-%s",cargv[1],cargv[2]);
                }
                else {
                    PL_strncpyz(descStr,cargv[1], sizeof(descStr));
                }
                slapi_matchingrule_set(mrentry,SLAPI_MATCHINGRULE_DESC,
						   (void *)slapi_ch_strdup(descStr));
                slapi_matchingrule_set(mrentry,SLAPI_MATCHINGRULE_SYNTAX,
						   (void *)slapi_ch_strdup(DIRSTRING_SYNTAX_OID));
                slapi_matchingrule_register(mrentry);
                slapi_matchingrule_get(mrentry,SLAPI_MATCHINGRULE_NAME,
                                       (void *)&tmpStr);
                slapi_ch_free((void **)&tmpStr);
                slapi_matchingrule_get(mrentry,SLAPI_MATCHINGRULE_OID,
                                       (void *)&tmpStr);
                slapi_ch_free((void **)&tmpStr);
                slapi_matchingrule_set(mrentry,SLAPI_MATCHINGRULE_NAME,
                                       (void *)slapi_ch_strdup(nameSubstring));
                PL_strcatn(oidString,sizeof(oidString),".6");
                slapi_matchingrule_set(mrentry,SLAPI_MATCHINGRULE_OID,
                                       (void *)slapi_ch_strdup(oidString));
                slapi_matchingrule_register(mrentry);
                slapi_matchingrule_free(&mrentry,1);
            }
 

	    for (arg = 6; arg < cargc; ++arg) {
		auto coll_id_t* id = (coll_id_t*) slapi_ch_malloc (sizeof (coll_id_t));
		id->oid     = slapi_ch_strdup (cargv[arg]);
		id->profile = profile;
		if (collation_ids <= 0) {
		    collation_id = (const coll_id_t**) slapi_ch_malloc (2 * sizeof (coll_id_t*));
		} else {
		    collation_id = (const coll_id_t**) slapi_ch_realloc
		      ((void*)collation_id, (collation_ids + 2) * sizeof (coll_id_t*));
		}
		collation_id [collation_ids++] = id;
		collation_id [collation_ids] = NULL;
	    }
	}
    } else {
	return -1; /* unrecognized */
    }
    return 0; /* success */
}

typedef struct collation_indexer_t
    /* A kind of indexer, implemented using an ICU Collator */
{
    UCollator*         collator;
    UConverter*	       converter;
    struct berval**    ix_keys;
    int                is_default_collator;
} collation_indexer_t;

/*
  Caller must ensure that U == NULL and Ulen == 0 the first time called
*/
static UErrorCode
SetUnicodeStringFromUTF_8 (UChar** U, int32_t* Ulen, int *isAlloced, const struct berval* bv)
    /* Copy the UTF-8 string bv into the UnicodeString U,
       but remove leading and trailing whitespace, and
       convert consecutive whitespaces into a single space.
       Ulen is set to the number of UChars in the array (not necessarily the number of bytes!)
    */
{
    size_t n;
    int32_t len = 0; /* length of non-space string */
    UErrorCode err = U_ZERO_ERROR;
    const char* s = bv->bv_val;
    const char* begin = NULL; /* will point to beginning of non-space in val */
    const char* end = NULL; /* will point to the first space after the last non-space char in val */
    int32_t nUchars = 0;

    if (!bv->bv_len) { /* no value? */
	return U_INVALID_FORMAT_ERROR; /* don't know what else to use here */
    }

    /* first, set s to the first non-space char in bv->bv_val */
    for (n = 0; (n < bv->bv_len) && ldap_utf8isspace((char *)s); ) { /* cast away const */
	const char *next = LDAP_UTF8NEXT((char *)s); /* cast away const */
	n += (next - s); /* count bytes, not chars */
	s = next;
    }
    begin = s; /* begin points to first non-space char in val */

    if (n >= bv->bv_len) { /* value is all spaces? */
	return U_INVALID_FORMAT_ERROR; /* don't know what else to use here */
    }

    s = bv->bv_val + (bv->bv_len-1); /* move s to last char of bv_val */
    end = s; /* end points at last char of bv_val - may change below */
    /* find the last non-null and non-space char of val */
    for (n = bv->bv_len; (n > 0) && (!*s || ldap_utf8isspace((char *)s));) {
	const char *prev = LDAP_UTF8PREV((char *)s);
	end = prev;
	n -= (s - prev); /* count bytes, not chars */
	s = prev;
    }	

    /* end now points at last non-null/non-space of val */
    if (n < 0) { /* bogus */
	return U_INVALID_FORMAT_ERROR; /* don't know what else to use here */
    }

    len = LDAP_UTF8NEXT((char *)end) - begin;

    u_strFromUTF8(*U, *Ulen, &nUchars, begin, len, &err);
    if (nUchars > *Ulen) { /* need more space */
	if (*isAlloced) { /* realloc space */
	    *U = (UChar *)slapi_ch_realloc((char *)*U, sizeof(UChar) * nUchars);
	} else { /* must use malloc */
	    *U = (UChar *)slapi_ch_malloc(sizeof(UChar) * nUchars);
	    *isAlloced = 1; /* no longer using fixed buffer */
	}
	*Ulen = nUchars;
	err = U_ZERO_ERROR; /* reset */
	u_strFromUTF8(*U, *Ulen, NULL, begin, len, &err);
    } else {
	*Ulen = nUchars;
    }

    return err;
}

static struct berval**
collation_index (indexer_t* ix, struct berval** bvec, struct berval** prefixes)
{ 
    collation_indexer_t* etc = (collation_indexer_t*) ix->ix_etc;
    struct berval** keys = NULL;
    if (bvec) {
	char keyBuffer[128]; /* try to use static space buffer to avoid malloc */
	int32_t keyLen = sizeof(keyBuffer);
	char* key = keyBuffer; /* but key can grow if necessary */
	size_t keyn = 0;
	struct berval** bv;
	UChar charBuffer[128]; /* try to use static space buffer */
	int32_t nChars = sizeof(charBuffer)/sizeof(UChar); /* but grow if necessary */
	UChar *chars = charBuffer; /* try to reuse this */
	int isAlloced = 0; /* using fixed buffer */

	for (bv = bvec; *bv; ++bv) {
	    /* if chars is allocated, nChars will be the capacity and the number of chars in chars */
	    /* otherwise, nChars will be the number of chars, which may be less than the capacity */
	    if (!isAlloced) {
		nChars = sizeof(charBuffer)/sizeof(UChar); /* reset */
	    }
	    if (U_ZERO_ERROR == SetUnicodeStringFromUTF_8 (&chars, &nChars, &isAlloced, *bv)) {
		/* nChars is now the number of UChar in chars, which may be less than the
		   capacity of charBuffer if not allocated */
		struct berval* prefix = prefixes ? prefixes[bv-bvec] : NULL;
		const size_t prefixLen = prefix ? prefix->bv_len : 0;
		struct berval* bk = NULL;
		int32_t realLen; /* real length of key, not keyLen which is buffer size */

		/* try to get the sort key using key and keyLen; only grow key
		   if we need to */
		/* can use -1 for char len since the conversion from UTF8
		   null terminates the string */
		realLen = ucol_getSortKey(etc->collator, chars, nChars, (uint8_t *)key, keyLen);
		if (realLen > keyLen) { /* need more space */
		    if (key == keyBuffer) {
			key = (char*)slapi_ch_malloc(sizeof(char) * realLen);
		    } else {
			key = (char*)slapi_ch_realloc(key, sizeof(char) * realLen);
		    }
		    keyLen = ucol_getSortKey(etc->collator, chars, nChars, (uint8_t *)key, realLen);
		}
		if (realLen > 0) {
		    bk = (struct berval*) slapi_ch_malloc (sizeof(struct berval));

		    bk->bv_len = prefixLen + realLen;
		    bk->bv_val = slapi_ch_malloc (bk->bv_len + 1);
		    if (prefixLen) {
			memcpy(bk->bv_val, prefix->bv_val, prefixLen);
		    }
		    memcpy(bk->bv_val + prefixLen, key, realLen);
		    bk->bv_val[bk->bv_len] = '\0';
		    LDAPDebug (LDAP_DEBUG_FILTER, "collation_index(%.*s) %lu bytes\n",
			       bk->bv_len, bk->bv_val, (unsigned long)bk->bv_len);
		    keys = (struct berval**)
			slapi_ch_realloc ((void*)keys, sizeof(struct berval*) * (keyn + 2));
		    keys[keyn++] = bk;
		    keys[keyn] = NULL;
		}
	    }
	}
	if (chars != charBuffer) { /* realloc'ed, need to free */
	    slapi_ch_free((void **)&chars);
	}
	if (key != keyBuffer) { /* realloc'ed, need to free */
	    slapi_ch_free_string(&key);
	}
    }
    if (etc->ix_keys != NULL) ber_bvecfree (etc->ix_keys);
    etc->ix_keys = keys;
    return keys;
}

static void
collation_indexer_destroy (indexer_t* ix)
    /* The destructor function for a collation-based indexer. */
{
    collation_indexer_t* etc = (collation_indexer_t*) ix->ix_etc;
    if (etc->converter) {
	ucnv_close(etc->converter);
	etc->converter = NULL;
    }
    if (!etc->is_default_collator) {
	/* Don't delete the default collation - it seems to cause problems */
	ucol_close(etc->collator);
	etc->collator = NULL;
    }
    if (etc->ix_keys != NULL) {
	ber_bvecfree (etc->ix_keys);
	etc->ix_keys = NULL;
    }
    slapi_ch_free((void**)&ix->ix_etc);
    ix->ix_etc = NULL; /* just for hygiene */
}

static UErrorCode
s_newNamedLocaleFromComponents(char **locale, const char *lang, const char *country, const char *variant)
{
    UErrorCode err = U_ZERO_ERROR;
    int hasLang = (lang && *lang);
    int hasC = (country && *country);
    int hasVar = (variant && *variant);

    *locale = NULL;
    if (hasLang) {
	*locale = PR_smprintf("%s%s%s%s%s", lang, (hasC ? "_" : ""), (hasC ? country : ""),
			      (hasVar ? "_" : ""), (hasVar ? variant : ""));
    } else {
	err = U_INVALID_FORMAT_ERROR; /* don't know what else to use here */
    }

    return err;
}

indexer_t*
collation_indexer_create (const char* oid)
    /* Return a new indexer, based on the collation identified by oid.
       Return NULL if this can't be done.
    */
{
    indexer_t* ix = NULL;
    const coll_id_t** id = collation_id;
    char* locale = NULL; /* NULL == default locale */
    if (id) for (; *id; ++id) {
	if (!strcasecmp (oid, (*id)->oid)) {
	    const coll_profile_t* profile = (*id)->profile;
	    const int is_default = (profile->language == NULL && 
					 profile->country  == NULL && 
					 profile->variant  == NULL);
	    UErrorCode err = U_ZERO_ERROR;
	    if ( ! is_default) {
		if (locale) {
		    PR_smprintf_free(locale);
		    locale = NULL;
		}
		err = s_newNamedLocaleFromComponents(&locale,
						     profile->language,
						     profile->country,
						     profile->variant);
	    }
	    if (err == U_ZERO_ERROR) {
		UCollator* coll = ucol_open(locale, &err);
		/*
		 * If we found exactly the right collator for this locale,
		 * or if we found a fallback one, or if we are happy with
		 * the default, use it.
		 */
		if (err == U_ZERO_ERROR || err == U_USING_FALLBACK_WARNING ||
		    (err == U_USING_DEFAULT_WARNING && is_default)) {
		    collation_indexer_t* etc = (collation_indexer_t*)
		      slapi_ch_calloc (1, sizeof (collation_indexer_t));
		    ix = (indexer_t*) slapi_ch_calloc (1, sizeof (indexer_t));
		    ucol_setAttribute (coll, UCOL_STRENGTH, profile->strength, &err);
		    if (err != U_ZERO_ERROR) {
			LDAPDebug (LDAP_DEBUG_ANY, "collation_indexer_create: could not "
				   "set the collator strength for oid %s to %d: err %d\n",
				   oid, profile->strength, err);
		    }
		    ucol_setAttribute (coll, UCOL_DECOMPOSITION_MODE, profile->decomposition, &err);
		    if (err != U_ZERO_ERROR) {
			LDAPDebug (LDAP_DEBUG_ANY, "collation_indexer_create: could not "
				   "set the collator decomposition mode for oid %s to %d: err %d\n",
				   oid, profile->decomposition, err);
		    }
		    etc->collator = coll;
		    etc->is_default_collator = is_default;
		    for (id = collation_id; *id; ++id) {
			if ((*id)->profile == profile) {
			    break; /* found the 'official' id */
			}
		    }
		    ix->ix_etc = etc;
		    ix->ix_oid = (*id)->oid;
		    ix->ix_index = collation_index;
		    ix->ix_destroy = collation_indexer_destroy;
		    break; /* return */
		    /* free (etc); */
		    /* free (ix); */
		} else if (err == U_USING_DEFAULT_WARNING) {
		    LDAPDebug (LDAP_DEBUG_FILTER, "collation_indexer_create: could not "
			       "create an indexer for OID %s for locale %s and could not "
			       "use default locale\n",
			       oid, (locale ? locale : "(default)"), NULL);
		} else { /* error */
		    LDAPDebug (LDAP_DEBUG_FILTER, "collation_indexer_create: could not "
			       "create an indexer for OID %s for locale %s: err = %d\n",
			       oid, (locale ? locale : "(default)"), err);
		}
		if (coll) {
		    ucol_close (coll);
		    coll = NULL;
		}
	    }
	    break; /* failed to create the specified collator */
	}
    }
    if (locale) {
	PR_smprintf_free(locale);
	locale = NULL;
    }
    return ix;
}
