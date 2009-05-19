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

/* dn.c - dn syntax routines */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

static int dn_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
	Slapi_Value **bvals, int ftype, Slapi_Value **retVal );
static int dn_filter_sub( Slapi_PBlock *pb, char *initial, char **any,
	char *final, Slapi_Value **bvals );
static int dn_values2keys( Slapi_PBlock *pb, Slapi_Value **vals,
	Slapi_Value ***ivals, int ftype );
static int dn_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
	Slapi_Value ***ivals, int ftype );
static int dn_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any,
	char *final, Slapi_Value ***ivals );
static int dn_validate( struct berval *val );
static int rdn_validate( const char *begin, const char *end, const char **last );

/* the first name is the official one from RFC 2252 */
static char *names[] = { "DN", DN_SYNTAX_OID, 0 };

static Slapi_PluginDesc pdesc = { "dn-syntax", PLUGIN_MAGIC_VENDOR_STR,
	PRODUCTTEXT, "distinguished name attribute syntax plugin" };

int
dn_init( Slapi_PBlock *pb )
{
	int	rc;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "=> dn_init\n", 0, 0, 0 );

	rc = slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    (void *) SLAPI_PLUGIN_VERSION_01 );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
	    (void *)&pdesc );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_AVA,
	    (void *) dn_filter_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_FILTER_SUB,
	    (void *) dn_filter_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALUES2KEYS,
	    (void *) dn_values2keys );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_AVA,
	    (void *) dn_assertion2keys_ava );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_ASSERTION2KEYS_SUB,
	    (void *) dn_assertion2keys_sub );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_NAMES,
	    (void *) names );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_OID,
	    (void *) DN_SYNTAX_OID );
	rc |= slapi_pblock_set( pb, SLAPI_PLUGIN_SYNTAX_VALIDATE,
	    (void *) dn_validate );

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<= dn_init %d\n", rc, 0, 0 );
	return( rc );
}

static int
dn_filter_ava( Slapi_PBlock *pb, struct berval *bvfilter,
    Slapi_Value **bvals, int ftype, Slapi_Value **retVal )
{
	return( string_filter_ava( bvfilter, bvals, SYNTAX_CIS | SYNTAX_DN,
	    ftype, retVal ) );
}

static int
dn_filter_sub( Slapi_PBlock *pb, char *initial, char **any, char *final,
    Slapi_Value **bvals )
{
	return( string_filter_sub( pb, initial, any, final, bvals,
	    SYNTAX_CIS | SYNTAX_DN ) );
}

static int
dn_values2keys( Slapi_PBlock *pb, Slapi_Value **vals, Slapi_Value ***ivals,
    int ftype )
{
	return( string_values2keys( pb, vals, ivals, SYNTAX_CIS | SYNTAX_DN,
	    ftype ) );
}

static int
dn_assertion2keys_ava( Slapi_PBlock *pb, Slapi_Value *val,
    Slapi_Value ***ivals, int ftype )
{
	return( string_assertion2keys_ava( pb, val, ivals,
	    SYNTAX_CIS | SYNTAX_DN, ftype ) );
}

static int
dn_assertion2keys_sub( Slapi_PBlock *pb, char *initial, char **any, char *final,
    Slapi_Value ***ivals )
{
	return( string_assertion2keys_sub( pb, initial, any, final, ivals,
	    SYNTAX_CIS | SYNTAX_DN ) );
}

static int dn_validate( struct berval *val )
{
	int rc = 0; /* Assume value is valid */
	char *val_copy = NULL;

	if (val != NULL) {
		/* Per RFC 4514:
		 *
		 * distinguishedName = [ relativeDistinguishedName
		 *     *( COMMA relativeDistinguishedName ) ]
		 * relativeDistinguishedName = attributeTypeAndValue
		 *     *( PLUS attributeTypeAndValue )
		 * attributeTypeAndValue = attribyteType EQUALS attributeValue
		 * attributeType = descr / numericoid
		 * attributeValue = string / hexstring
		 */
		if (val->bv_len > 0) {
			int strict = 0;
			const char *p = val->bv_val;
			const char *end = &(val->bv_val[val->bv_len - 1]);
			const char *last = NULL;

			/* Check if we should be performing strict validation. */
			strict = config_get_dn_validate_strict();
			if (!strict) {
				/* Create a normalized copy of the value to use
				 * for validation.  The original value will be
				 * stored in the backend unmodified. */
				val_copy = PL_strndup(val->bv_val, val->bv_len);
				p = val_copy;
				end = slapi_dn_normalize_to_end(val_copy, NULL) - 1;
			}

			/* Validate one RDN at a time in a loop. */
			while (p <= end) {
				if ((rc = rdn_validate(p, end, &last)) != 0) {
					goto exit;
				}
				p = last + 1;

				/* p should be pointing at a comma, or one past
				 * the end of the entire dn value.  If we have
				 * not reached the end, ensure that the next
				 * character is a comma and that there is at
				 * least another character after the comma. */
				if ((p <= end) && ((p == end) || (*p != ','))) {
					rc = 1;
					goto exit;
				}

				/* Advance the pointer past the comma so it
				 * points at the beginning of the next RDN
				 * (if there is one). */
				p++;
			}
		}
	} else {
		rc = 1;
		goto exit;
	}
exit:
	if (val_copy) {
		slapi_ch_free_string(&val_copy);
	}
	return rc;
}

/*
 * Helper function for validating a DN.  This function will validate
 * a single RDN.  If the RDN is valid, 0 will be returned, otherwise
 * non-zero will be returned. A pointer to the last character processed
 * will be set in the "last parameter.  This will be the end of the RDN
 * in the valid case, and the illegal character in the invalid case.
 */
static int rdn_validate( const char *begin, const char *end, const char **last )
{
	int rc = 0; /* Assume RDN is valid */
	int numericform = 0;
	char *separator = NULL;
	const char *p = begin;

	/* Find the '=', then use the helpers for descr and numericoid */
	if ((separator = PL_strnchr(p, '=', end - begin + 1)) == NULL) {
		rc = 1;
		goto exit;
	}

	/* Process an attribute type. The 'descr'
	 * form must start with a 'leadkeychar'. */
	if (IS_LEADKEYCHAR(*p)) {
		if ((rc = keystring_validate(p, separator - 1))) {
			goto exit;
		}
	/* See if the 'numericoid' form is being used */
	} else if (isdigit(*p)) {
		numericform = 1;
		if ((rc = numericoid_validate(p, separator - 1))) {
			goto exit;
		}
	} else {
		rc = 1;
		goto exit;
	}

	/* Advance the pointer past the '=' and make sure
	 * we're not past the end of the string. */
	p = separator + 1;
	if (p > end) {
		rc = 1;
		goto exit;
	}

	/* The value must be a 'hexstring' if the 'numericoid'
	 * form of 'attributeType' is used.  Per RFC 4514:
	 *
	 *   hexstring = SHARP 1*hexpair
	 *   hexpair = HEX HEX
	 */
	if (numericform) {
		if ((p == end) || !IS_SHARP(*p)) {
			rc = 1;
			goto exit;
		}
		p++;
	/* The value must be a 'string' when the 'descr' form
	 * of 'attributeType' is used.  Per RFC 4514:
	 *
	 *   string = [ ( leadchar / pair ) [ *( stringchar / pair )
	 *      ( trailchar / pair ) ] ]
	 *
	 *   leadchar   = LUTF1 / UTFMB
	 *   trailchar  = TUTF1 / UTFMB
	 *   stringchar = SUTF1 / UTFMB
	 *
	 *   pair = ESC (ESC / special / hexpair )
	 *   special = escaped / SPACE / SHARP / EQUALS
	 *   escaped = DQUOTE / PLUS / COMMA / SEMI / LANGLE / RANGLE
	 *   hexpair = HEX HEX
	 */
	} else {
		/* Check the leadchar to see if anything illegal
		 * is there.  We need to allow a 'pair' to get
		 * through, so we'll assume that a '\' is the
		 * start of a 'pair' for now. */
		if (IS_UTF1(*p) && !IS_ESC(*p) && !IS_LUTF1(*p)) {
			rc = 1;
			goto exit;
		}
	}

	/* Loop through string until we find the ',' separator, a '+'
	 * char indicating a multi-value RDN, or we reach the end.  */
	while ((p <= end) && (*p != ',') && (*p != '+')) {
		if (numericform) {
			/* Process a single 'hexpair' */
			if ((p == end) || !isxdigit(*p) || !isxdigit(*p + 1)) {
				rc = 1;
				goto exit;
			}
			p = p + 2;
		} else {
			/* Check for a valid 'stringchar'.  We handle
			 * multi-byte characters separately. */
			if (IS_UTF1(*p)) {
				/* If we're at the end, check if we have
				 * a valid 'trailchar'. */
				if ((p == end) && !IS_TUTF1(*p)) {
					rc = 1;
					goto exit;
				/* Check for a 'pair'. */
				} else if (IS_ESC(*p)) {
					/* We're guaranteed to still have at
					 * least one more character, so lets
					 * take a look at it. */
					p++;
					if (!IS_ESC(*p) && !IS_SPECIAL(*p)) {
						/* The only thing valid now
						 * is a 'hexpair'. */
						if ((p == end) || !isxdigit(*p) ||!isxdigit(*p + 1)) {
							rc = 1;
							goto exit;
						}
						p++;
					}
					p++;
				/* Only allow 'SUTF1' chars now. */
				} else if (!IS_SUTF1(*p)) {
					rc = 1;
					goto exit;
				}

				p++;
			} else {
				/* Validate a single 'UTFMB' (multi-byte) character. */
				if (utf8char_validate(p, end, &p ) != 0) {
					rc = 1;
					goto exit;
				}

				/* Advance the pointer past the multi-byte char. */
				p++;
			}
		}
	}

	/* We'll end up either at the comma, a '+', or one past end.
	 * If we are processing a multi-valued RDN, we recurse to
	 * process the next 'attributeTypeAndValue'. */
	if ((p <= end) && (*p == '+')) {
		/* Make sure that there is something after the '+'. */
		if (p == end) {
			rc = 1;
			goto exit;
		}
		p++;

		/* Recurse to process the next value.  We need to reset p to
		 * ensure that last is set correctly for the original caller. */
		rc = rdn_validate( p, end, last );
		p = *last + 1;
	}

exit:
	*last = p - 1;
	return rc;
}
