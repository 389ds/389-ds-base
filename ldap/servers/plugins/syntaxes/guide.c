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
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* guide.c - Guide and Enhanced Guide syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int guide_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
		Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int guide_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value **bvals );
static int guide_values2keys( Slapi_PBlock *pb, Slapi_Value **val,
		Slapi_Value ***ivals, int ftype );
static int guide_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
		Slapi_Value ***ivals, int ftype );
static int guide_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
		char *final, Slapi_Value ***ivals );
static int guide_compare(struct berval	*v1, struct berval	*v2);
static int enhancedguide_validate(struct berval *val);
static int guide_validate(struct berval *val);
static int criteria_validate(const char *start, const char *end);
static int andterm_validate(const char *start, const char *end, const char **last);
static int term_validate(const char *start, const char *end, const char **last);
static void guide_normalize(
	Slapi_PBlock *pb,
	char    *s,
	int     trim_spaces,
	char    **alt
);

/* the first name is the official one from RFC 4517 */
static char *guide_names[] = { "Guide", "guide", GUIDE_SYNTAX_OID, 0 };

static char *enhancedguide_names[] = { "Enhanced Guide", "enhancedguide",
		ENHANCEDGUIDE_SYNTAX_OID, 0 };

static Slapi_PluginDesc guide_pdesc = { "guide-syntax", VENDOR, DS_PACKAGE_VERSION,
	"Guide attribute syntax plugin" };

static Slapi_PluginDesc enhancedguide_pdesc = { "enhancedguide-syntax",
		VENDOR, DS_PACKAGE_VERSION,
		"Enhanced Guide attribute syntax plugin" };

int
guide_init( Slapi_PBlock *pb )
{
	int	rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> guide_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&guide_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) guide_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) guide_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) guide_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) guide_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) guide_assertion2keys_sub );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) guide_names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) GUIDE_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) guide_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) guide_validate );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NORMALIZE,
	    (void *) guide_normalize );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= guide_init %d\n", rc, 0, 0 );
	return( rc );
}

int
enhancedguide_init( Slapi_PBlock *pb )
{
	int     rc, flags;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> guide_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&enhancedguide_pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) guide_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) guide_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) guide_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) guide_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) guide_assertion2keys_sub );
	flags = SLAPI_PLUGIN_SYNTAX_FLAG_ORDERING;
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FLAGS,
	    (void *) &flags );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) enhancedguide_names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) ENHANCEDGUIDE_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_COMPARE,
	    (void *) guide_compare );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) enhancedguide_validate );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= guide_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
guide_filter_ava(
    Slapi_PBlock		*pb,
    struct berval	*bvfilter,
    Slapi_Value	**bvals,
    int			ftype,
	Slapi_Value **retVal
)
{
	int filter_normalized = 0;
	int syntax = SYNTAX_CIS;
	slapi_pblock_get( pb, SLAPI_PLUGIN_SYNTAX_FILTER_NORMALIZED, &filter_normalized );
	if (filter_normalized) {
		syntax |= SYNTAX_NORM_FILT;
	}
	return( string_filter_ava( bvfilter, bvals, syntax,
	    ftype, retVal ) );
}


static int
guide_filter_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	**bvals
)
{
	return( string_filter_sub( pb, initial, any, final, bvals, SYNTAX_CIS ) );
}

static int
guide_values2keys(
    Slapi_PBlock		*pb,
    Slapi_Value	**vals,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_CIS,
	    ftype ) );
}

static int
guide_assertion2keys_ava(
    Slapi_PBlock		*pb,
    Slapi_Value	*val,
    Slapi_Value	***ivals,
    int			ftype
)
{
	return(string_assertion2keys_ava( pb, val, ivals,
	    SYNTAX_CIS, ftype ));
}

static int
guide_assertion2keys_sub(
    Slapi_PBlock		*pb,
    char		*initial,
    char		**any,
    char		*final,
    Slapi_Value	***ivals
)
{
	return( string_assertion2keys_sub( pb, initial, any, final, ivals,
	    SYNTAX_CIS ) );
}

static int guide_compare(    
	struct berval	*v1,
    struct berval	*v2
)
{
	return value_cmp(v1, v2, SYNTAX_CIS, 3 /* Normalise both values */);
}

static int
enhancedguide_validate(
	struct berval   *val
)
{
	int rc = 0;    /* assume the value is valid */
	const char *start = NULL;
	const char *end = NULL;
	const char *p = NULL;
	const char *sharp = NULL;

	/* Per RFC4517:
	 *
	 * EnhancedGuide = object-class SHARP WSP criteria WSP
	 *                    SHARP WSP subset
	 * subset        = "baseobject" / "oneLevel" / "wholeSubtree"
	 */

	/* Don't allow a 0 length string */
	if ((val == NULL) || (val->bv_len == 0)) {
		rc = 1;
		goto exit;
	}

	start = &(val->bv_val[0]);
	end = &(val->bv_val[val->bv_len - 1]);

	/* Find the first SHARP. */
	for (p = start; p <= end; p++) {
                if (IS_SHARP(*p)) {
                        sharp = p;
                        break;
                }
        }

	/* Fail if we didn't find a SHARP, or if SHARP
	 * is at the start or end of the value. */
	if ((sharp == NULL) || (sharp == start) || (sharp == end)){
		rc = 1;
		goto exit;
	}

	/* Reset p and end to validate the object-class. */
	p = start;
	end = sharp - 1;

	/* Skip any leading spaces. */
	while ((p < sharp) && IS_SPACE(*p)) {
		p++;
	}

	/* Skip any trailing spaces. */
	while ((end > p) && IS_SPACE(*end)) {
		end--;
	}

	/* See if we only found spaces before the SHARP. */
	if (end < p) {
		rc = 1;
		goto exit;
	}

	/* Validate p to end as object-class.  This is the same
	 * as an oid, which is either a keystring or a numericoid. */
	if (IS_LEADKEYCHAR(*p)) {
		rc = keystring_validate(p, end);
	/* check if the value matches the numericoid form */
	} else if (isdigit(*p)) {
		rc = numericoid_validate(p, end);
	} else {
		rc = 1;
	}

	/* We're done if the object-class failed to validate. */
	if (rc != 0) {
		goto exit;
	}

	/* Reset start and end to validate the criteria. */
	start = sharp + 1;
	end = &(val->bv_val[val->bv_len - 1]);

	/* Find the next SHARP. */
	for (p = start; p <= end; p++) {
		if (IS_SHARP(*p)) {
			sharp = p;
			break;
		}
	}

	/* Fail if we didn't find a SHARP, or if SHARP
	 * is at the start or end of the value. */
	if ((sharp == NULL) || (sharp == start) || (sharp == end)){
		rc = 1;
		goto exit;
	}

	/* Reset p and end to validate the criteria. */
        p = start;
        end = sharp - 1;

        /* Skip any leading spaces. */
        while ((p < sharp) && IS_SPACE(*p)) {
                p++;
        }

        /* Skip any trailing spaces. */
        while ((end > p) && IS_SPACE(*end)) {
                end--;
        }

        /* See if we only found spaces before the SHARP. */
        if (end < p) {
                rc = 1;
                goto exit;
        }

	/* Validate p to end as criteria. */
	if ((rc = criteria_validate(p, end)) != 0) {
		goto exit;
	}

	/* Reset start and end to validate the subset.  We're
	 * guaranteed to have a character after sharp. */
	p = start = sharp + 1;
	end = &(val->bv_val[val->bv_len - 1]);

	/* Skip any leading spaces. */
	while ((p < end) && IS_SPACE(*p)) {
		p ++;
	}

	/* Validate the subset. */
	switch (end - p + 1) {
		case 8:
			if (strncmp(p, "oneLevel", 8) != 0) {
				rc = 1;
			}
			break;
		case 10:
			if (strncmp(p, "baseobject", 10) != 0) {
				rc = 1;
			}
			break;
		case 12:
			if (strncmp(p, "wholeSubtree", 12) != 0) {
				rc = 1;
			}
			break;
		default:
			rc = 1;
	}

exit:
	return rc;
}

static int
guide_validate(
	struct berval	*val
)
{
	int rc = 0;    /* assume the value is valid */
	const char *start = NULL;
	const char *end = NULL;
	const char *p = NULL;
	const char *sharp = NULL;

	/* Per RFC4517:
	 *
	 * Guide        = [ object-class SHARP ] criteria
	 * object-class = WSP oid WSP
	 * criteria     = and-term *( BAR and-term )
	 * and-term     = term *( AMPERSAND term )
	 * term         = EXCLAIM term /
	 *                attributetype DOLLAR match-type /
	 *                LPAREN criteria RPAREN /
	 *                true /
	 *                false
	 * match-type   = "EQ" / "SUBSTR" / "GE" / "LE" / "APPROX"
	 * true         = "?true"
	 * false        = "?false"
	 */

	/* Don't allow a 0 length string */
	if ((val == NULL) || (val->bv_len == 0)) {
		rc = 1;
		goto exit;
	}

	start = &(val->bv_val[0]);
	end = &(val->bv_val[val->bv_len - 1]);

	/* Look for a SHARP.  If we have one, the value should
	 * begin with the optional object-class. */
	for (p = start; p <= end; p++) {
		if (IS_SHARP(*p)) {
			sharp = p;
			break;
		}
	}

	if (sharp) {
		/* "criteria" must exist, so the SHARP
		 * can't be at the end of the value. */
		if (sharp == end) {
			rc = 1;
			goto exit;
		}

		/* An optional object-class should be present.  Reset
		 * p to the beginning of the value and end to just
		 * before the SHARP to validate the object-class.
		 * We'll reset end later. */
		p = start;
		end = sharp - 1;

		/* This can happen if the value begins with SHARP. */
		if (end < start) {
			rc = 1;
			goto exit;
		}

		/* Skip any leading spaces. */
		while ((p < sharp) && IS_SPACE(*p)) {
			p++;
		}

		/* Skip any trailing spaces. */
		while ((end > p) && IS_SPACE(*end)) {
			end--;
		}

		/* See if we only found spaces before the SHARP. */
		if (end < p) {
			rc = 1;
			goto exit;
		}

		/* Validate p to end as object-class.  This is the same
		 * as an oid, which is either a keystring or a numericoid. */
		if (IS_LEADKEYCHAR(*p)) {
			rc = keystring_validate(p, end);
		/* check if the value matches the numericoid form */
		} else if (isdigit(*p)) {
			rc = numericoid_validate(p, end);
		} else {
			rc = 1;
		}

		/* If the object-class failed to validate, we're done. */
		if (rc != 0) {
			goto exit;
		}

		/* Reset p and end to point to the criteria. */
		p = sharp + 1;
		end = &(val->bv_val[val->bv_len - 1]);
	} else {
		/* Reset p. */
		p = start;
	}

	/* Validate the criteria. */
	rc = criteria_validate(p, end);

exit:
	return rc;
}

/* criteria_validate()
 *
 * Helper to validate criteria element.
 */
static int
criteria_validate(const char *start, const char *end)
{
	const char *p = start;
	const char *last = NULL;
	int rc = 0;

	/* Validate the criteria, which is just made up of a number
	 * of and-term elements.  Validate one and-term at a time. */
	while (p <= end) {
		if ((rc = andterm_validate(p, end, &last)) != 0) {
			goto exit;
		}
		p = last + 1;

		/* p should be pointing at a BAR, or one past
		 * the end of the entire value.  If we have
		 * not reached the end, ensure that the next
		 * character is a BAR and that there is at
		 * least another character after the BAR. */
		if ((p <= end) && ((p == end) || (*p != '|'))) {
			rc = 1;
			goto exit;
		}

		/* Advance the pointer past the BAR so
		 * it points at the beginning of the
		 * next and-term (if there is one). */
		p++;
	}

exit:
	return rc;
}

/*
 * andterm_validate()
 *
 * This function will validate a single and-term.  If the and-term
 * is valid, 0 will be returned, otherwise non-zero will be returned.
 * A pointer to the last character of the and-term will be set in the
 * "last" parameter in the valid case.
 */
static int
andterm_validate(const char *start, const char *end, const char **last)
{
	const char *p = start;
	int rc = 0;

	if ((start == NULL) || (end == NULL)) {
		rc = 1;
		goto exit;
	}

	while (p <= end) {
		if ((rc = term_validate(p, end, last)) != 0) {
			goto exit;
		}
		p = *last + 1;

		/* p should be pointing at an ampersand, a bar, or
		 * one past the end of the entire value.  If we have
		 * not reached the end, ensure that the next
		 * character is an ampersand or a bar and that
		 * there is at least another character afterwards. */
		if ((p <= end) && ((p == end) || ((*p != '&') && (*p != '|')))) {
			rc = 1;
			goto exit;
		}

		/* If p is a bar, we're done. */
		if (*p == '|') {
			break;
		}

		/* Advance the pointer past the ampersand
		 * or bar so it points at the beginning of
		 * the next term or and-term (if there is
		 * one). */
		p++;
	}

exit:
	return rc;
}

static int
term_validate(const char *start, const char *end, const char **last)
{
	int rc = 0;
	const char *p = start;

	/* Per RFC 4517:
	 *
	 * term         = EXCLAIM term /
	 *                attributetype DOLLAR match-type /
	 *                LPAREN criteria RPAREN /
	 *                true /
	 *                false
	 * match-type   = "EQ" / "SUBSTR" / "GE" / "LE" / "APPROX"
	 * true         = "?true"
	 * false        = "?false"
	 */

	/* See if the term is prefixed by an EXCLAIM. */
	if (*p == '!') {
		p++;
		/* Ensure the value doesn't end with an EXCLAIM. */
		if (p > end) {
			rc = 1;
			goto exit;
		}
	}

	/* Check for valid terms. */
	switch (*p) {
		case '?':
			{
				/* true or false */
				int length = 0;

				p++;
				length = end - p + 1;

				if ((length >= 5) && (strncmp(p, "false", 5) == 0)) {
					/* Found false.  We're done. */
					*last = p + 4;
					goto exit;
				}

				if ((length >= 4) && (strncmp(p, "true", 4) == 0)) {
					/* Found true.  We're done. */
					*last = p + 3;
					goto exit;
				}

				/* We didn't find true or false.  Fail. */
				rc = 1;
				goto exit;
			}
		case '(':
			{
				/* LPAREN criteria RPAREN */
				const char *lparen = p;

				while ((p <= end) && !IS_RPAREN(*p)) {
						p++;
				}

				if (p > end) {
					/* We didn't find a RPAREN.  Fail. */
					rc = 1;
					goto exit;
				} else {
					/* p is pointing at the RPAREN.  Validate
					 * everything between the parens as criteria. */
					rc = criteria_validate(lparen + 1, p - 1);
					*last = p;
				}
				break;
			}
		default:
			{
				/* attributetype DOLLAR match-type */
				const char *attrtype = p;

				while ((p <= end) && !IS_DOLLAR(*p)) {
					p++;
				}

				if (p > end) {
					/* We didn't find a DOLLAR.  Fail. */
					rc = 1;
					goto exit;
				} else {
					/* p is pointing at the DOLLAR.  Validate
					 * the attributetype before the DOLLAR. */
					if (IS_LEADKEYCHAR(*attrtype)) {
						rc = keystring_validate(attrtype, p - 1);
					/* check if the value matches the numericoid form */
					} else if (isdigit(*attrtype)) {
						rc = numericoid_validate(attrtype, p - 1);
					} else {
						rc = 1;
					}

					/* If the attributetype was invalid, we're done. */
					if (rc != 0) {
						goto exit;
					}

					/* Validate that a valid match-type
					 * is after the DOLLAR. */
					if (p == end) {
						rc = 1;
						goto exit;
					} else {
						int length = 0;
	
						p++;
						length = end - p + 1;					

						if (length >= 6) {
							/* APPROX, SUBSTR */
							if ((strncmp(p, "APPROX", 6) == 0) ||
							    (strncmp(p, "SUBSTR", 6) == 0)) {
								/* We found a valid match-type.
								 * We're done. */
								*last = p + 5;
								goto exit;
							}
						}

						if (length >= 2) {
							/* EQ, GE, LE */
							if ((strncmp(p, "EQ", 2) == 0) ||
							    (strncmp(p, "GE", 2) == 0) ||
							    (strncmp(p, "LE", 2) == 0)) {
								/* We found a valid match-type.
								 * We're done. */
								*last = p + 1;
								goto exit;
							}
						}

						/* We failed to find a valid match-type. */
						rc = 1;
						goto exit;
					}
				}
			}
	}

exit:
	return rc;
}

static void guide_normalize(
	Slapi_PBlock	*pb,
	char	*s,
	int		trim_spaces,
	char	**alt
)
{
	value_normalize_ext(s, SYNTAX_CIS, trim_spaces, alt);
	return;
}
