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
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* validate.c - syntax validation helper functions */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "syntax.h"

/* Helper function for processing a 'keystring'.
 *
 * Returns 0 is the value between begin and end is a valid 'keystring'.
 * Returns non-zero if the value is not a valide 'keystring'.
 */
int keystring_validate(
	const char *begin,
	const char *end
)
{
	int rc = 0;    /* assume the value is valid */
	const char *p = begin;
	
	if ((begin == NULL) || (end == NULL)) {
		rc = 1;
		goto exit;
	}

	/* Per RFC4512:
	 *
	 *   keystring = leadkeychar *keychar
	 */
	if (IS_LEADKEYCHAR(*p)) {
		for (p++; p <= end; p++) {
			if (!IS_KEYCHAR(*p)) {
				rc = 1;
				goto exit;
			}
		}
	} else {
		rc = 1;
		goto exit;
	}

exit:
	return( rc );
}

/* Helper function for processing a 'numericoid'.
 *
 * Returns 0 is the value between begin and end is a valid 'numericoid'.
 * Returns non-zero if the value is not a valide 'numericoid'.
 */
int numericoid_validate(
	const char *begin,
	const char *end
)
{
	int rc = 0; /* assume the value is valid */
	int found_separator = 0;
	const char *p = NULL;

	if ((begin == NULL) || (end == NULL)) {
		rc = 1;
		goto exit;
	}

	/* Per RFC 4512:
	 *
	 *   numericoid = number 1*( DOT number )
	 */

	/* one pass of this loop should process one element of the oid (number DOT) */
	for (p = begin; p <= end; p++) {
		if (IS_LDIGIT(*p)) {
			/* loop until we get to a separator char */
			while(*p != '.') {
				p++;
				if (p > end) {
					/* ensure we got at least 2 elements */
					if (!found_separator) {
						rc = 1;
						goto exit;
					} else {
						/* looks like a valid numericoid */
						goto exit;
					}
				} else if (*p == '.') {
					/* we can not end with a '.' */
					if (p == end) {
						rc = 1;
						goto exit;
					} else {
						found_separator = 1;
					}
				} else if (!isdigit(*p)) {
					rc = 1;
					goto exit;
				}
			}
		} else if (*p == '0') {
			p++;
			if (p > end) {
				/* ensure we got at least 2 elements */
				if (!found_separator) {
					rc = 1;
					goto exit;
				} else {
					/* looks like a valid numericoid */
					goto exit;
				}
			} else if (*p != '.') {
				/* a leading 0 is not allowed unless the entire element is simply 0 */
				rc = 1;
				goto exit;
			}

			/* At this point, *p is '.'.  We can not end with a '.' */
			if (p == end) {
				rc = 1;
				goto exit;
			} else {
				found_separator = 1;
			}
		} else {
			rc = 1;
			goto exit;
		}
	}

exit:
	return(rc);
}

/* Helper to validate a single UTF-8 character.
 * It is assumed that the first byte of the character
 * is pointed to by begin.  This function will not read
 * past the byte pointed to by the end parameter.  The
 * last pointer will be filled in the the address of
 * the last byte of the validated character if the
 * character is valid, or the last byte processed
 * in the invalid case.
 *
 * Returns 0 if it is valid and non-zero otherwise. */
int utf8char_validate(
	const char *begin,
	const char *end,
	const char **last
)
{
	int rc = 0; /* Assume char is valid */
	const char *p = begin;

	if ((begin == NULL) || (end == NULL)) {
		rc = 1;
		goto exit;
	}

	/* Per RFC 4512:
	 *
	 *   UTF8  = UTF1 / UTFMB
	 *   UTFMB = UTF2 / UTF3 / UTF4
	 *   UTF0  = %x80-BF
	 *   UTF1  = %x00-7F
	 *   UTF2  = %xC2-DF UTF0
	 *   UTF3  = %xE0 %xA0-BF UTF0 / %xE1-EC 2(UTF0) /
	 *           %xED %x80-9F UTF0 / %xEE-EF 2(UTF0)
	 *   UTF4  = %xF0 %x90-BF 2(UTF0) / %xF1-F3 3(UTF0) /
	 *           %xF4 %x80-8F 2(UTF0)
	 */

	/* If we have a single byte (ASCII) character, we
	 * don't really have any work to do. */
	if (IS_UTF1(*p)) {
		goto exit;
	} else if (IS_UTF2(*p)) {
		/* Ensure that there is another byte
		 * and that is is 'UTF0'. */
		if ((p == end) || !IS_UTF0(*(p + 1))) {
			rc = 1;
			goto exit;
		}

		/* Advance p so last is set correctly */
		p++;
	} else if (IS_UTF3(*p)) {
		/* Ensure that there are at least 2 more bytes. */
		if (end - p < 2) {
			rc = 1;
			goto exit;
		}

		/* The first byte determines what is legal for
		 * the second byte. */
		if (*p == '\xE0') {
			/* The next byte must be %xA0-BF. */
			p++;
			if (((unsigned char)*p < (unsigned char)'\xA0') || ((unsigned char)*p > (unsigned char)'\xBF')) {
				rc = 1;
				goto exit;
			}
		} else if (*p == '\xED') {
			/* The next byte must be %x80-9F. */
			p++;
			if (((unsigned char)*p < (unsigned char)'\x80') || ((unsigned char)*p > (unsigned char)'\x9F')) {
				rc = 1;
				goto exit;
			}
		} else {
			/* The next byte must each be 'UTF0'. */
			p++;
			if (!IS_UTF0(*p)) {
				rc = 1;
				goto exit;
			}
		}

		/* The last byte must be 'UTF0'. */
		p++;
		if (!IS_UTF0(*p)) {
			rc = 1;
			goto exit;
		}
	} else if (IS_UTF4(*p)) {
		/* Ensure that there are at least 3 more bytes. */
		if (end - p < 3) {
			rc = 1;
			goto exit;
		}

		/* The first byte determines what is legal for
		 * the second byte. */
		if (*p == '\xF0') {
			/* The next byte must be %x90-BF. */
			if (((unsigned char)*p < (unsigned char)'\x90') || ((unsigned char)*p > (unsigned char)'\xBF')) {
				rc = 1;
				goto exit;
			}
		} else if (*p == '\xF4') {
			/* The next byte must be %x80-BF. */
			if (((unsigned char)*p < (unsigned char)'\x80') || ((unsigned char)*p > (unsigned char)'\xBF')) {
				rc = 1;
				goto exit;
			}
		} else {
			/* The next byte must each be 'UTF0'. */
			p++;
			if (!IS_UTF0(*p)) {
				rc = 1;
				goto exit;
			}
		}

		/* The last 2 bytes must be 'UTF0'. */
		p++;
		if (!IS_UTF0(*p) || !IS_UTF0(*(p + 1))) {
			rc = 1;
			goto exit;
		}

		/* Advance the pointer so last is set correctly
		 * when we return. */
		p++;
	} else {
		/* We found an illegal first byte. */
		rc = 1;
		goto exit;
	}

exit:
	if (last) {
		*last = (const char *)p;
	}
	return(rc);
}

/* Validates that a non '\0' terminated string is UTF8.  This
 * function will not read past the byte pointed to by the end
 * parameter.  The last pointer will be filled in to point to
 * the address of the last byte of the last validated character
 * if the string is valid, or the last byte processed in the
 * invalid case.
 *
 * Returns 0 if it is valid and non-zero otherwise. */
int utf8string_validate(
        const char *begin,
        const char *end,
        const char **last
)
{
        int rc = 0; /* Assume string is valid */
        const char *p = NULL;

        if ((begin == NULL) || (end == NULL)) {
                rc = 1;
                goto exit;
        }

	for (p = begin; p <= end; p++) {
		if ((rc = utf8char_validate(p, end, &p)) != 0) {
			goto exit;
		}
	}

	/* Adjust the pointer so last is set correctly for caller. */
	p--;

exit:
	if (last) {
		*last = p;
	}
	return(rc);
}

/*
 * Validates a distinguishedName as degined in RFC 4514.  Returns
 * 0 if the value from begin to end is a valid distinguishedName.
 * Returns 1 otherwise.
 */
int distinguishedname_validate(
	const char *begin,
	const char *end
)
{
	int rc = 0; /* Assume value is valid */
	char *val_copy = NULL;
	int strict = 0;
	const char *p = begin;
	const char *last = NULL;

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

	/* Check if we should be performing strict validation. */
	strict = config_get_dn_validate_strict();
	if (!strict) {
		/* Create a normalized copy of the value to use
		 * for validation.  The original value will be
		 * stored in the backend unmodified. */
		val_copy = PL_strndup(begin, end - begin + 1);
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
int rdn_validate( const char *begin, const char *end, const char **last )
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

int
bitstring_validate_internal(const char *begin, const char *end)
{
        int     rc = 0;    /* assume the value is valid */
        const char *p = NULL;

        /* Per RFC4517:
         *
         * BitString    = SQUOTE *binary-digit SQUOTE "B"
         * binary-digit = "0" / "1"
         */

        /* Check that the value starts with a SQUOTE and
         * ends with SQUOTE "B". */
        if (!IS_SQUOTE(*begin) || (*end != 'B') ||
            !IS_SQUOTE(*(end - 1))) {
                rc = 1;
                goto exit;
        }

        /* Ensure that only '0' and '1' are between the SQUOTE chars. */
        for (p = begin + 1; p <= end - 2; p++) {
                if ((*p != '0') && (*p != '1')) {
                        rc = 1;
                        goto exit;
                }
        }

exit:
        return rc;
}
