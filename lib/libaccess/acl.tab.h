/** BEGIN COPYRIGHT BLOCK
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
