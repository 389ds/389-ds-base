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
	char *begin,
	char *end
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
	char *begin,
	char *end
)
{
	int rc = 0; /* assume the value is valid */
	int found_separator = 0;
	char *p = NULL;

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
	char *begin,
	char *end,
	char **last
)
{
	int rc = 0; /* Assume char is valid */
	char *p = begin;

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
			if ((*p < '\xA0') || (*p > '\xBF')) {
				rc = 1;
				goto exit;
			}
		} else if (*p == '\xED') {
			/* The next byte must be %x80-9F. */
			p++;
			if ((*p < '\x80') || (*p > '\x9F')) {
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
			if ((*p < '\x90') || (*p > '\xBF')) {
				rc = 1;
				goto exit;
			}
		} else if (*p == '\xF4') {
			/* The next byte must be %x80-BF. */
			if ((*p < '\x80') || (*p > '\xBF')) {
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
		*last = p;
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
        char *begin,
        char *end,
        char **last
)
{
        int rc = 0; /* Assume string is valid */
        char *p = NULL;

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

