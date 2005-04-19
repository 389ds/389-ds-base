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

typedef union
#ifdef __cplusplus
	ACLSTYPE
#endif
 {
	char	*string;
	int	ival;
} ACLSTYPE;
extern ACLSTYPE acllval;
# define ACL_ABSOLUTE_TOK 257
# define ACL_ACL_TOK 258
# define ACL_ALLOW_TOK 259
# define ACL_ALWAYS_TOK 260
# define ACL_AND_TOK 261
# define ACL_AT_TOK 262
# define ACL_AUTHENTICATE_TOK 263
# define ACL_CONTENT_TOK 264
# define ACL_DEFAULT_TOK 265
# define ACL_DENY_TOK 266
# define ACL_GROUP_TOK 267
# define ACL_IN_TOK 268
# define ACL_INHERIT_TOK 269
# define ACL_NOT_TOK 270
# define ACL_NULL_TOK 271
# define ACL_OR_TOK 272
# define ACL_QSTRING_TOK 273
# define ACL_READ_TOK 274
# define ACL_TERMINAL_TOK 275
# define ACL_VARIABLE_TOK 276
# define ACL_VERSION_TOK 277
# define ACL_WRITE_TOK 278
# define ACL_WITH_TOK 279
# define ACL_EQ_TOK 280
# define ACL_GE_TOK 281
# define ACL_GT_TOK 282
# define ACL_LE_TOK 283
# define ACL_LT_TOK 284
# define ACL_NE_TOK 285
