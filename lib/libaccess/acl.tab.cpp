/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

# line 8 "acltext.y"
#include <string.h>
#include <netsite.h>
#include <base/util.h>
#include <base/plist.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/nserror.h>
#include "parse.h"
#include "aclscan.h"

#define MAX_LIST_SIZE 255
static ACLListHandle_t *curr_acl_list;	/* current acl list */
static ACLHandle_t *curr_acl;		/* current acl */
static ACLExprHandle_t *curr_expr;	/* current expression */
static PFlags_t	pflags;			/* current authorization flags */
static char *curr_args_list[MAX_LIST_SIZE]; /* current args */
static char *curr_user_list[MAX_LIST_SIZE]; /* current users v2 */
static char *curr_ip_dns_list[MAX_LIST_SIZE]; /* current ip/dns v2 */
static PList_t curr_auth_info;		/* current authorization method */
static int use_generic_rights;		/* use generic rights for conversion */

int acl_PushListHandle(ACLListHandle_t *handle)
{
	curr_acl_list = handle;
	return(0);
}

static void
acl_string_lower(char *s)
{
int     ii;
int     len;

        len = strlen(s);
        for (ii = 0; ii < len; ii++)
                s[ii] = tolower(s[ii]);

        return;
}

static void
acl_clear_args(char **args_list)
{
	args_list[0] = NULL;
}

static void 
acl_add_arg(char **args_list, char *arg)
{
	static int args_index;

	if ( args_list[0] == NULL ) {
		args_index = 0;
	}
	args_list[args_index] = arg;
	args_index++;
	args_list[args_index] = NULL;
}

static void
acl_free_args(char **args_list)
{
	int ii;

	for (ii = 0; ii < MAX_LIST_SIZE; ii++) {
		if ( args_list[ii] )
			PERM_FREE(args_list[ii]);
		else
			break;
	}
}

static int
acl_set_args(ACLExprHandle_t *expr, char **args_list)
{
	int ii;

	if (expr == NULL)
		return(-1);

	for (ii = 0; ii < MAX_LIST_SIZE; ii++) {
		if ( args_list[ii] ) {
			if ( ACL_ExprAddArg(NULL, expr, args_list[ii]) < 0 ) {
				aclerror("ACL_ExprAddArg() failed");
				return(-1);
			}
		} else
			break;
	}
	return(0);
}

static int
acl_set_users_or_groups(ACLExprHandle_t *expr, char **user_list)
{
	int ii;
	int jj;

	if (expr == NULL)
		return(-1);

	for (ii = 0; ii < MAX_LIST_SIZE; ii++) {
		if ( user_list[ii] ) {
			if ( ACL_ExprTerm(NULL, expr, "user", CMP_OP_EQ, 
					user_list[ii]) < 0 ) {
				aclerror("ACL_ExprTerm() failed");
				acl_free_args(user_list);
				return(-1);
			}
			if ( ACL_ExprTerm(NULL, expr, "group", CMP_OP_EQ, 
					user_list[ii]) < 0 ) {
				aclerror("ACL_ExprTerm() failed");
				acl_free_args(user_list);
				return(-1);
			}
		} else
			break;
	}

	acl_free_args(user_list);

	for (jj = 0; jj < (ii * 2) - 1; jj++) {
		if ( ACL_ExprOr(NULL, expr)  < 0 ) {
			aclerror("ACL_ExprOr() failed");
			return(-1);
		}
	}
	return(0);
}

static int
acl_set_ip_dns(ACLExprHandle_t *expr, char **ip_dns)
{
	int ii;
	int jj;
	int len;
	char *attr;
	char *val;

        if (expr == NULL)
                return(-1);

        for (ii = 0; ii < MAX_LIST_SIZE; ii++) {
                if ( ip_dns[ii] ) {

                	attr = "ip";
			val = ip_dns[ii];
                	len = strlen(val);

                	for (jj = 0; jj < len; jj++) {
                        	if ( strchr("0123456789.*", val[jj]) == 0 ) {
                                	attr = "dns";
                                	break;
                        	}
                	}

                        if ( ACL_ExprTerm(NULL, expr, attr, CMP_OP_EQ,
                                        val) < 0 ) {
                                aclerror("ACL_ExprTerm() failed");
                                acl_free_args(ip_dns);
                                return(-1);
                        }

                } else
                        break;
        }

        acl_free_args(ip_dns);

        for (jj = 0; jj < ii - 1; jj++) {
                if ( ACL_ExprOr(NULL, expr)  < 0 ) {
                        aclerror("ACL_ExprOr() failed");
                        return(-1);
                }
        }

        return(0);
}



# line 223 "acltext.y"
typedef union
#ifdef __cplusplus
	ACLSTYPE
#endif
 {
	char	*string;
	int	ival;
} ACLSTYPE;
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

#ifdef __STDC__
#include <stdlib.h>
#include <string.h>
#else
#include <netsite.h>
#include <memory.h>
#endif


#ifdef __cplusplus

#ifndef aclerror
	void aclerror(const char *);
#endif

#ifndef acllex
#ifdef __EXTERN_C__
	extern "C" { int acllex(void); }
#else
	int acllex(void);
#endif
#endif
	int acl_Parse(void);

#endif
#define aclclearin aclchar = -1
#define aclerrok aclerrflag = 0
extern int aclchar;
extern int aclerrflag;
ACLSTYPE acllval;
ACLSTYPE aclval;
typedef int acltabelem;
#ifndef ACLMAXDEPTH
#define ACLMAXDEPTH 150
#endif
#if ACLMAXDEPTH > 0
int acl_acls[ACLMAXDEPTH], *acls = acl_acls;
ACLSTYPE acl_aclv[ACLMAXDEPTH], *aclv = acl_aclv;
#else	/* user does initial allocation */
int *acls;
ACLSTYPE *aclv;
#endif
static int aclmaxdepth = ACLMAXDEPTH;
# define ACLERRCODE 256

# line 952 "acltext.y"

acltabelem aclexca[] ={
-1, 1,
	0, -1,
	-2, 0,
	};
# define ACLNPROD 120
# define ACLLAST 251
acltabelem aclact[]={

   176,   177,   178,   180,   179,   181,   156,   109,    69,    53,
   160,   116,    76,     6,   185,   169,   118,   186,   170,   117,
   150,    78,    85,   149,    77,    18,   144,    29,    17,    86,
    28,    11,     3,   126,    10,   136,   140,    82,    89,   104,
    87,   101,     7,   129,   127,   171,   133,    79,    72,    40,
   132,    38,   102,    55,   108,    37,   172,   105,    39,    60,
    60,   107,   128,    63,    59,    45,    61,    61,    93,    23,
    46,     6,   131,   130,   158,   142,   137,   157,   125,   134,
   154,   147,    56,   122,   112,    30,    75,    94,    81,   111,
   139,   138,    88,    73,   165,   164,   155,    57,    50,    49,
    48,    27,    14,    41,    65,    58,   145,    97,   153,   146,
    98,   152,   120,    25,   184,   151,   119,    24,    99,    64,
    13,    32,    15,    21,     5,   175,   159,   106,   103,     8,
   100,   124,    84,    83,    66,    54,    52,   143,    80,    51,
    67,    90,    36,    35,    26,    34,    33,    22,    31,    20,
   135,   113,    62,    74,    96,    47,    92,    71,    44,    68,
    43,    70,    42,    95,    16,    91,     9,     4,    19,    12,
     2,     1,     0,     0,     0,     0,     0,     0,     0,     0,
     0,     0,     0,   110,   115,   114,   121,   123,     0,     0,
     0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     0,     0,     0,     0,     0,     0,     0,    95,   141,     0,
     0,     0,     0,     0,     0,   148,     0,     0,     0,     0,
     0,     0,     0,     0,     0,   163,     0,     0,     0,   166,
   167,   168,     0,     0,     0,     0,   174,     0,   173,     0,
   161,     0,     0,     0,   118,    78,   162,   117,    77,   182,
   183 };
acltabelem aclpact[]={

  -245,-10000000,-10000000,  -234,  -187,-10000000,  -242,-10000000,-10000000,    80,
-10000000,-10000000,    43,  -248,  -189,    76,    69,-10000000,-10000000,-10000000,
  -189,-10000000,    42,  -246,   -38,  -248,-10000000,  -208,-10000000,-10000000,
  -195,-10000000,-10000000,  -208,    41,    40,    39,-10000000,-10000000,  -270,
  -213,   -43,    38,-10000000,-10000000,  -199,  -200,-10000000,-10000000,-10000000,
-10000000,    79,-10000000,-10000000,-10000000,  -271,-10000000,  -195,-10000000,  -220,
-10000000,-10000000,   -28,  -221,  -239,-10000000,  -235,  -238,-10000000,-10000000,
-10000000,   -28,-10000000,-10000000,  -194,-10000000,  -252,-10000000,-10000000,-10000000,
    66,-10000000,-10000000,-10000000,    78,  -223,  -218,  -203,-10000000,  -273,
  -238,-10000000,   -39,   -29,    75,    68,   -39,   -40,  -239,  -243,
-10000000,  -231,  -202,-10000000,  -232,  -184,-10000000,  -185,  -214,  -227,
-10000000,-10000000,  -241,-10000000,-10000000,-10000000,  -257,  -240,-10000000,-10000000,
  -252,-10000000,  -250,-10000000,    65,-10000000,-10000000,-10000000,-10000000,-10000000,
-10000000,-10000000,-10000000,-10000000,   -44,  -241,  -253,    74,    67,    64,
-10000000,-10000000,   -45,    37,  -274,   -30,  -243,-10000000,-10000000,    36,
    35,-10000000,  -257,  -257,-10000000,  -250,  -258,-10000000,  -216,-10000000,
   -30,   -30,  -280,-10000000,-10000000,-10000000,-10000000,-10000000,-10000000,-10000000,
-10000000,   -30,   -30,    73,-10000000,  -259,-10000000,-10000000,-10000000,-10000000,
-10000000,-10000000,-10000000,-10000000,-10000000,-10000000,-10000000 };
acltabelem aclpgo[]={

     0,   171,   170,   169,   168,   167,   124,   166,   122,   103,
   164,   162,   160,   158,   105,   157,    93,   156,    89,   154,
   153,   151,    86,    87,    91,    90,    76,    79,   150,   149,
   123,   147,   121,   146,   145,   143,   142,   141,    92,   140,
   139,   138,    75,    88,   137,   136,   104,   135,   134,   133,
   132,   131,    77,   130,   128,   127,    78,    74,   126,   125 };
acltabelem aclr1[]={

     0,     1,     1,     3,     1,     2,     5,     5,     6,     7,
     7,     8,     8,    10,    10,     9,     9,    11,    11,    15,
    13,    13,    14,    14,    17,    12,    19,    12,    16,    16,
    20,    20,    23,    23,    22,    22,    21,    21,    21,    24,
    24,    25,    26,    26,    26,    26,    18,    28,    28,    27,
    27,     4,    29,    29,    30,    30,    31,    31,    32,    32,
    33,    33,    33,    37,    36,    39,    36,    38,    40,    34,
    41,    41,    43,    42,    42,    44,    44,    45,    35,    47,
    35,    48,    46,    49,    50,    50,    50,    50,    50,    50,
    50,    55,    55,    55,    55,    53,    53,    53,    53,    54,
    54,    54,    54,    51,    51,    56,    52,    52,    52,    57,
    57,    57,    58,    58,    59,    59,    59,    59,    59,    59 };
acltabelem aclr2[]={

     0,     0,     2,     1,    10,     2,     2,     4,    17,     3,
     3,     2,     6,     3,     3,     4,     6,     2,     2,     1,
     8,     6,     3,     3,     1,    10,     1,    10,     7,     3,
     2,     6,     2,     6,     3,     3,     2,     2,     6,     3,
     3,     5,     2,     2,     6,     6,     7,     7,     7,     2,
     4,     2,     2,     4,     6,     4,     5,     5,     2,     4,
     4,     4,     4,     1,    10,     1,     8,     7,     1,    17,
     2,     6,     3,     4,     6,     7,     7,     1,     6,     1,
     6,     1,     5,    10,     0,     3,     5,     3,     5,     3,
     5,     3,     3,     5,     5,     3,     3,     5,     5,     3,
     3,     5,     5,     2,     6,     3,     2,     7,     7,     2,
     6,     5,     7,     7,     2,     2,     2,     2,     2,     2 };
acltabelem aclchk[]={

-10000000,    -1,    -2,   277,    -5,    -6,   258,   276,    -6,    -7,
   276,   273,    -3,    40,    59,    -8,   -10,   276,   273,    -4,
   -29,   -30,   -31,   258,    41,    44,   -30,    59,   276,   273,
   123,    -8,   -32,   -33,   -34,   -35,   -36,   263,   259,   266,
   257,    -9,   -11,   -12,   -13,   260,   265,   -32,    59,    59,
    59,   -40,   -45,   279,   -47,   266,   125,    59,   -14,   263,
   259,   266,   -14,   263,    40,   -46,   -48,   -39,   -46,   279,
    -9,   -15,   268,   -16,   -20,   -22,    40,   276,   273,   268,
   -41,   -43,   276,   -49,   -50,   257,   264,   275,   -38,   276,
   -37,   -16,   -17,   262,   -23,   -22,   -19,    41,    44,    40,
   -53,   264,   275,   -54,   257,   275,   -55,   264,   257,   280,
   -38,   -18,   123,   -21,   -24,   -25,    40,   276,   273,    41,
    44,   -18,   123,   -43,   -51,   -56,   276,   275,   264,   275,
   257,   257,   264,   273,   -27,   -28,   276,   -26,   -24,   -25,
   276,   -23,   -42,   -44,   276,    41,    44,   125,   -27,   276,
   273,    41,    44,    44,   125,    59,   280,   -52,   -57,   -58,
    40,   270,   276,   -56,    59,    59,   -26,   -26,   -42,   273,
   276,   261,   272,   -52,   -57,   -59,   280,   281,   282,   284,
   283,   285,   -52,   -52,    41,   273,   276 };
acltabelem acldef[]={

     1,    -2,     2,     0,     5,     6,     0,     3,     7,     0,
     9,    10,     0,     0,     0,     0,    11,    13,    14,     4,
    51,    52,     0,     0,     0,     0,    53,    55,    56,    57,
     0,    12,    54,    58,     0,     0,     0,    68,    77,    79,
     0,     0,     0,    17,    18,     0,     0,    59,    60,    61,
    62,     0,    81,    65,    81,     0,     8,    15,    19,     0,
    22,    23,     0,     0,     0,    78,    84,     0,    80,    63,
    16,     0,    24,    21,    29,    30,     0,    34,    35,    26,
     0,    70,    72,    82,     0,    85,    87,    89,    66,     0,
     0,    20,     0,     0,     0,    32,     0,     0,     0,     0,
    86,    95,    96,    88,    99,   100,    90,    91,    92,     0,
    64,    25,     0,    28,    36,    37,     0,    39,    40,    31,
     0,    27,     0,    71,     0,   103,   105,    97,    98,   101,
   102,    93,    94,    67,     0,    49,     0,     0,    42,    43,
    41,    33,     0,     0,     0,     0,     0,    46,    50,     0,
     0,    38,     0,     0,    69,    73,     0,    83,   106,   109,
     0,     0,     0,   104,    47,    48,    44,    45,    74,    75,
    76,     0,     0,     0,   111,     0,   114,   115,   116,   117,
   118,   119,   107,   108,   110,   112,   113 };
typedef struct
#ifdef __cplusplus
	acltoktype
#endif
{ char *t_name; int t_val; } acltoktype;
#ifndef ACLDEBUG
#	define ACLDEBUG	0	/* don't allow debugging */
#endif

#if ACLDEBUG

acltoktype acltoks[] =
{
	"ACL_ABSOLUTE_TOK",	257,
	"ACL_ACL_TOK",	258,
	"ACL_ALLOW_TOK",	259,
	"ACL_ALWAYS_TOK",	260,
	"ACL_AND_TOK",	261,
	"ACL_AT_TOK",	262,
	"ACL_AUTHENTICATE_TOK",	263,
	"ACL_CONTENT_TOK",	264,
	"ACL_DEFAULT_TOK",	265,
	"ACL_DENY_TOK",	266,
	"ACL_GROUP_TOK",	267,
	"ACL_IN_TOK",	268,
	"ACL_INHERIT_TOK",	269,
	"ACL_NOT_TOK",	270,
	"ACL_NULL_TOK",	271,
	"ACL_OR_TOK",	272,
	"ACL_QSTRING_TOK",	273,
	"ACL_READ_TOK",	274,
	"ACL_TERMINAL_TOK",	275,
	"ACL_VARIABLE_TOK",	276,
	"ACL_VERSION_TOK",	277,
	"ACL_WRITE_TOK",	278,
	"ACL_WITH_TOK",	279,
	"ACL_EQ_TOK",	280,
	"ACL_GE_TOK",	281,
	"ACL_GT_TOK",	282,
	"ACL_LE_TOK",	283,
	"ACL_LT_TOK",	284,
	"ACL_NE_TOK",	285,
	"-unknown-",	-1	/* ends search */
};

char * aclreds[] =
{
	"-no such reduction-",
	"start : /* empty */",
	"start : start_acl_v2",
	"start : ACL_VERSION_TOK ACL_VARIABLE_TOK",
	"start : ACL_VERSION_TOK ACL_VARIABLE_TOK ';' start_acl_v3",
	"start_acl_v2 : acl_list_v2",
	"acl_list_v2 : acl_v2",
	"acl_list_v2 : acl_list_v2 acl_v2",
	"acl_v2 : ACL_ACL_TOK acl_name_v2 '(' arg_list_v2 ')' '{' directive_list_v2 '}'",
	"acl_name_v2 : ACL_VARIABLE_TOK",
	"acl_name_v2 : ACL_QSTRING_TOK",
	"arg_list_v2 : arg_v2",
	"arg_list_v2 : arg_v2 ',' arg_list_v2",
	"arg_v2 : ACL_VARIABLE_TOK",
	"arg_v2 : ACL_QSTRING_TOK",
	"directive_list_v2 : directive_v2 ';'",
	"directive_list_v2 : directive_v2 ';' directive_list_v2",
	"directive_v2 : auth_method_v2",
	"directive_v2 : auth_statement_v2",
	"auth_statement_v2 : ACL_ALWAYS_TOK auth_type_v2",
	"auth_statement_v2 : ACL_ALWAYS_TOK auth_type_v2 host_spec_list_action_v2",
	"auth_statement_v2 : ACL_DEFAULT_TOK auth_type_v2 host_spec_list_action_v2",
	"auth_type_v2 : ACL_ALLOW_TOK",
	"auth_type_v2 : ACL_DENY_TOK",
	"auth_method_v2 : ACL_ALWAYS_TOK ACL_AUTHENTICATE_TOK ACL_IN_TOK",
	"auth_method_v2 : ACL_ALWAYS_TOK ACL_AUTHENTICATE_TOK ACL_IN_TOK realm_definition_v2",
	"auth_method_v2 : ACL_DEFAULT_TOK ACL_AUTHENTICATE_TOK ACL_IN_TOK",
	"auth_method_v2 : ACL_DEFAULT_TOK ACL_AUTHENTICATE_TOK ACL_IN_TOK realm_definition_v2",
	"host_spec_list_action_v2 : user_expr_v2 ACL_AT_TOK host_spec_list_v2",
	"host_spec_list_action_v2 : user_expr_v2",
	"user_expr_v2 : user_v2",
	"user_expr_v2 : '(' user_list_v2 ')'",
	"user_list_v2 : user_v2",
	"user_list_v2 : user_v2 ',' user_list_v2",
	"user_v2 : ACL_VARIABLE_TOK",
	"user_v2 : ACL_QSTRING_TOK",
	"host_spec_list_v2 : dns_spec_v2",
	"host_spec_list_v2 : ip_spec_v2",
	"host_spec_list_v2 : '(' dns_ip_spec_list_v2 ')'",
	"dns_spec_v2 : ACL_VARIABLE_TOK",
	"dns_spec_v2 : ACL_QSTRING_TOK",
	"ip_spec_v2 : ACL_VARIABLE_TOK ACL_VARIABLE_TOK",
	"dns_ip_spec_list_v2 : dns_spec_v2",
	"dns_ip_spec_list_v2 : ip_spec_v2",
	"dns_ip_spec_list_v2 : dns_spec_v2 ',' dns_ip_spec_list_v2",
	"dns_ip_spec_list_v2 : ip_spec_v2 ',' dns_ip_spec_list_v2",
	"realm_definition_v2 : '{' methods_list_v2 '}'",
	"method_v2 : ACL_VARIABLE_TOK ACL_VARIABLE_TOK ';'",
	"method_v2 : ACL_VARIABLE_TOK ACL_QSTRING_TOK ';'",
	"methods_list_v2 : method_v2",
	"methods_list_v2 : method_v2 methods_list_v2",
	"start_acl_v3 : acl_list",
	"acl_list : acl",
	"acl_list : acl_list acl",
	"acl : named_acl ';' body_list",
	"acl : named_acl ';'",
	"named_acl : ACL_ACL_TOK ACL_VARIABLE_TOK",
	"named_acl : ACL_ACL_TOK ACL_QSTRING_TOK",
	"body_list : body",
	"body_list : body body_list",
	"body : authenticate_statement ';'",
	"body : authorization_statement ';'",
	"body : deny_statement ';'",
	"deny_statement : ACL_ABSOLUTE_TOK ACL_DENY_TOK ACL_WITH_TOK",
	"deny_statement : ACL_ABSOLUTE_TOK ACL_DENY_TOK ACL_WITH_TOK deny_common",
	"deny_statement : ACL_DENY_TOK ACL_WITH_TOK",
	"deny_statement : ACL_DENY_TOK ACL_WITH_TOK deny_common",
	"deny_common : ACL_VARIABLE_TOK ACL_EQ_TOK ACL_QSTRING_TOK",
	"authenticate_statement : ACL_AUTHENTICATE_TOK",
	"authenticate_statement : ACL_AUTHENTICATE_TOK '(' attribute_list ')' '{' parameter_list '}'",
	"attribute_list : attribute",
	"attribute_list : attribute_list ',' attribute",
	"attribute : ACL_VARIABLE_TOK",
	"parameter_list : parameter ';'",
	"parameter_list : parameter ';' parameter_list",
	"parameter : ACL_VARIABLE_TOK ACL_EQ_TOK ACL_QSTRING_TOK",
	"parameter : ACL_VARIABLE_TOK ACL_EQ_TOK ACL_VARIABLE_TOK",
	"authorization_statement : ACL_ALLOW_TOK",
	"authorization_statement : ACL_ALLOW_TOK auth_common_action",
	"authorization_statement : ACL_DENY_TOK",
	"authorization_statement : ACL_DENY_TOK auth_common_action",
	"auth_common_action : /* empty */",
	"auth_common_action : auth_common",
	"auth_common : flag_list '(' args_list ')' expression",
	"flag_list : /* empty */",
	"flag_list : ACL_ABSOLUTE_TOK",
	"flag_list : ACL_ABSOLUTE_TOK content_static",
	"flag_list : ACL_CONTENT_TOK",
	"flag_list : ACL_CONTENT_TOK absolute_static",
	"flag_list : ACL_TERMINAL_TOK",
	"flag_list : ACL_TERMINAL_TOK content_absolute",
	"content_absolute : ACL_CONTENT_TOK",
	"content_absolute : ACL_ABSOLUTE_TOK",
	"content_absolute : ACL_CONTENT_TOK ACL_ABSOLUTE_TOK",
	"content_absolute : ACL_ABSOLUTE_TOK ACL_CONTENT_TOK",
	"content_static : ACL_CONTENT_TOK",
	"content_static : ACL_TERMINAL_TOK",
	"content_static : ACL_CONTENT_TOK ACL_TERMINAL_TOK",
	"content_static : ACL_TERMINAL_TOK ACL_CONTENT_TOK",
	"absolute_static : ACL_ABSOLUTE_TOK",
	"absolute_static : ACL_TERMINAL_TOK",
	"absolute_static : ACL_ABSOLUTE_TOK ACL_TERMINAL_TOK",
	"absolute_static : ACL_TERMINAL_TOK ACL_ABSOLUTE_TOK",
	"args_list : arg",
	"args_list : args_list ',' arg",
	"arg : ACL_VARIABLE_TOK",
	"expression : factor",
	"expression : factor ACL_AND_TOK expression",
	"expression : factor ACL_OR_TOK expression",
	"factor : base_expr",
	"factor : '(' expression ')'",
	"factor : ACL_NOT_TOK factor",
	"base_expr : ACL_VARIABLE_TOK relop ACL_QSTRING_TOK",
	"base_expr : ACL_VARIABLE_TOK relop ACL_VARIABLE_TOK",
	"relop : ACL_EQ_TOK",
	"relop : ACL_GE_TOK",
	"relop : ACL_GT_TOK",
	"relop : ACL_LT_TOK",
	"relop : ACL_LE_TOK",
	"relop : ACL_NE_TOK",
};
#endif /* ACLDEBUG */


/*
** Skeleton parser driver for yacc output
*/

/*
** yacc user known macros and defines
*/
#define ACLERROR		goto aclerrlab
#define ACLACCEPT	return(0)
#define ACLABORT		return(1)
#define ACLBACKUP( newtoken, newvalue )\
{\
	if ( aclchar >= 0 || ( aclr2[ acltmp ] >> 1 ) != 1 )\
	{\
		aclerror( "syntax error - cannot backup" );\
		goto aclerrlab;\
	}\
	aclchar = newtoken;\
	aclstate = *aclps;\
	acllval = newvalue;\
	goto aclnewstate;\
}
#define ACLRECOVERING()	(!!aclerrflag)
#define ACLNEW(type)	PERM_MALLOC(sizeof(type) * aclnewmax)
#define ACLCOPY(to, from, type) \
	(type *) memcpy(to, (char *) from, aclnewmax * sizeof(type))
#define ACLENLARGE( from, type) \
	(type *) PERM_REALLOC((char *) from, aclnewmax * sizeof(type))
#ifndef ACLDEBUG
#	define ACLDEBUG	1	/* make debugging available */
#endif

/*
** user known globals
*/
int acldebug;			/* set to 1 to get debugging */

/*
** driver internal defines
*/
#define ACLFLAG		(-10000000)

/*
** global variables used by the parser
*/
ACLSTYPE *aclpv;			/* top of value stack */
int *aclps;			/* top of state stack */

int aclstate;			/* current state */
int acltmp;			/* extra var (lasts between blocks) */

int aclnerrs;			/* number of errors */
int aclerrflag;			/* error recovery flag */
int aclchar;			/* current input token number */



#ifdef ACLNMBCHARS
#define ACLLEX()		aclcvtok(acllex())
/*
** aclcvtok - return a token if i is a wchar_t value that exceeds 255.
**	If i<255, i itself is the token.  If i>255 but the neither 
**	of the 30th or 31st bit is on, i is already a token.
*/
#if defined(__STDC__) || defined(__cplusplus)
int aclcvtok(int i)
#else
int aclcvtok(i) int i;
#endif
{
	int first = 0;
	int last = ACLNMBCHARS - 1;
	int mid;
	wchar_t j;

	if(i&0x60000000){/*Must convert to a token. */
		if( aclmbchars[last].character < i ){
			return i;/*Giving up*/
		}
		while ((last>=first)&&(first>=0)) {/*Binary search loop*/
			mid = (first+last)/2;
			j = aclmbchars[mid].character;
			if( j==i ){/*Found*/ 
				return aclmbchars[mid].tvalue;
			}else if( j<i ){
				first = mid + 1;
			}else{
				last = mid -1;
			}
		}
		/*No entry in the table.*/
		return i;/* Giving up.*/
	}else{/* i is already a token. */
		return i;
	}
}
#else/*!ACLNMBCHARS*/
#define ACLLEX()		acllex()
#endif/*!ACLNMBCHARS*/

/*
** acl_Parse - return 0 if worked, 1 if syntax error not recovered from
*/
#if defined(__STDC__) || defined(__cplusplus)
int acl_Parse(void)
#else
int acl_Parse()
#endif
{
	register ACLSTYPE *aclpvt;	/* top of value stack for $vars */

#if defined(__cplusplus) || defined(lint)
/*
	hacks to please C++ and lint - goto's inside switch should never be
	executed; aclpvt is set to 0 to avoid "used before set" warning.
*/
	static int __yaccpar_lint_hack__ = 0;
	switch (__yaccpar_lint_hack__)
	{
		case 1: goto aclerrlab;
		case 2: goto aclnewstate;
	}
	aclpvt = 0;
#endif

	/*
	** Initialize externals - acl_Parse may be called more than once
	*/
	aclpv = &aclv[-1];
	aclps = &acls[-1];
	aclstate = 0;
	acltmp = 0;
	aclnerrs = 0;
	aclerrflag = 0;
	aclchar = -1;

#if ACLMAXDEPTH <= 0
	if (aclmaxdepth <= 0)
	{
		if ((aclmaxdepth = ACLEXPAND(0)) <= 0)
		{
			aclerror("yacc initialization error");
			ACLABORT;
		}
	}
#endif

	{
		register ACLSTYPE *acl_pv;	/* top of value stack */
		register int *acl_ps;		/* top of state stack */
		register int acl_state;		/* current state */
		register int  acl_n;		/* internal state number info */
	goto aclstack;	/* moved from 6 lines above to here to please C++ */

		/*
		** get globals into registers.
		** branch to here only if ACLBACKUP was called.
		*/
	aclnewstate:
		acl_pv = aclpv;
		acl_ps = aclps;
		acl_state = aclstate;
		goto acl_newstate;

		/*
		** get globals into registers.
		** either we just started, or we just finished a reduction
		*/
	aclstack:
		acl_pv = aclpv;
		acl_ps = aclps;
		acl_state = aclstate;

		/*
		** top of for (;;) loop while no reductions done
		*/
	acl_stack:
		/*
		** put a state and value onto the stacks
		*/
#if ACLDEBUG
		/*
		** if debugging, look up token value in list of value vs.
		** name pairs.  0 and negative (-1) are special values.
		** Note: linear search is used since time is not a real
		** consideration while debugging.
		*/
		if ( acldebug )
		{
			register int acl_i;

			printf( "State %d, token ", acl_state );
			if ( aclchar == 0 )
				printf( "end-of-file\n" );
			else if ( aclchar < 0 )
				printf( "-none-\n" );
			else
			{
				for ( acl_i = 0; acltoks[acl_i].t_val >= 0;
					acl_i++ )
				{
					if ( acltoks[acl_i].t_val == aclchar )
						break;
				}
				printf( "%s\n", acltoks[acl_i].t_name );
			}
		}
#endif /* ACLDEBUG */
		if ( ++acl_ps >= &acls[ aclmaxdepth ] )	/* room on stack? */
		{
			/*
			** reallocate and recover.  Note that pointers
			** have to be reset, or bad things will happen
			*/
			int aclps_index = (acl_ps - acls);
			int aclpv_index = (acl_pv - aclv);
			int aclpvt_index = (aclpvt - aclv);
			int aclnewmax;
#ifdef ACLEXPAND
			aclnewmax = ACLEXPAND(aclmaxdepth);
#else
			aclnewmax = 2 * aclmaxdepth;	/* double table size */
			if (aclmaxdepth == ACLMAXDEPTH)	/* first time growth */
			{
				char *newacls = (char *)ACLNEW(int);
				char *newaclv = (char *)ACLNEW(ACLSTYPE);
				if (newacls != 0 && newaclv != 0)
				{
					acls = ACLCOPY(newacls, acls, int);
					aclv = ACLCOPY(newaclv, aclv, ACLSTYPE);
				}
				else
					aclnewmax = 0;	/* failed */
			}
			else				/* not first time */
			{
				acls = ACLENLARGE(acls, int);
				aclv = ACLENLARGE(aclv, ACLSTYPE);
				if (acls == 0 || aclv == 0)
					aclnewmax = 0;	/* failed */
			}
#endif
			if (aclnewmax <= aclmaxdepth)	/* tables not expanded */
			{
				aclerror( "yacc stack overflow" );
				ACLABORT;
			}
			aclmaxdepth = aclnewmax;

			acl_ps = acls + aclps_index;
			acl_pv = aclv + aclpv_index;
			aclpvt = aclv + aclpvt_index;
		}
		*acl_ps = acl_state;
		*++acl_pv = aclval;

		/*
		** we have a new state - find out what to do
		*/
	acl_newstate:
		if ( ( acl_n = aclpact[ acl_state ] ) <= ACLFLAG )
			goto acldefault;		/* simple state */
#if ACLDEBUG
		/*
		** if debugging, need to mark whether new token grabbed
		*/
		acltmp = aclchar < 0;
#endif
		if ( ( aclchar < 0 ) && ( ( aclchar = ACLLEX() ) < 0 ) )
			aclchar = 0;		/* reached EOF */
#if ACLDEBUG
		if ( acldebug && acltmp )
		{
			register int acl_i;

			printf( "Received token " );
			if ( aclchar == 0 )
				printf( "end-of-file\n" );
			else if ( aclchar < 0 )
				printf( "-none-\n" );
			else
			{
				for ( acl_i = 0; acltoks[acl_i].t_val >= 0;
					acl_i++ )
				{
					if ( acltoks[acl_i].t_val == aclchar )
						break;
				}
				printf( "%s\n", acltoks[acl_i].t_name );
			}
		}
#endif /* ACLDEBUG */
		if ( ( ( acl_n += aclchar ) < 0 ) || ( acl_n >= ACLLAST ) )
			goto acldefault;
		if ( aclchk[ acl_n = aclact[ acl_n ] ] == aclchar )	/*valid shift*/
		{
			aclchar = -1;
			aclval = acllval;
			acl_state = acl_n;
			if ( aclerrflag > 0 )
				aclerrflag--;
			goto acl_stack;
		}

	acldefault:
		if ( ( acl_n = acldef[ acl_state ] ) == -2 )
		{
#if ACLDEBUG
			acltmp = aclchar < 0;
#endif
			if ( ( aclchar < 0 ) && ( ( aclchar = ACLLEX() ) < 0 ) )
				aclchar = 0;		/* reached EOF */
#if ACLDEBUG
			if ( acldebug && acltmp )
			{
				register int acl_i;

				printf( "Received token " );
				if ( aclchar == 0 )
					printf( "end-of-file\n" );
				else if ( aclchar < 0 )
					printf( "-none-\n" );
				else
				{
					for ( acl_i = 0;
						acltoks[acl_i].t_val >= 0;
						acl_i++ )
					{
						if ( acltoks[acl_i].t_val
							== aclchar )
						{
							break;
						}
					}
					printf( "%s\n", acltoks[acl_i].t_name );
				}
			}
#endif /* ACLDEBUG */
			/*
			** look through exception table
			*/
			{
				register int *aclxi = aclexca;

				while ( ( *aclxi != -1 ) ||
					( aclxi[1] != acl_state ) )
				{
					aclxi += 2;
				}
				while ( ( *(aclxi += 2) >= 0 ) &&
					( *aclxi != aclchar ) )
					;
				if ( ( acl_n = aclxi[1] ) < 0 )
					ACLACCEPT;
			}
		}

		/*
		** check for syntax error
		*/
		if ( acl_n == 0 )	/* have an error */
		{
			/* no worry about speed here! */
			switch ( aclerrflag )
			{
			case 0:		/* new error */
				aclerror( "syntax error" );
				goto skip_init;
			aclerrlab:
				/*
				** get globals into registers.
				** we have a user generated syntax type error
				*/
				acl_pv = aclpv;
				acl_ps = aclps;
				acl_state = aclstate;
			skip_init:
				aclnerrs++;
				/* FALLTHRU */
			case 1:
			case 2:		/* incompletely recovered error */
					/* try again... */
				aclerrflag = 3;
				/*
				** find state where "error" is a legal
				** shift action
				*/
				while ( acl_ps >= acls )
				{
					acl_n = aclpact[ *acl_ps ] + ACLERRCODE;
					if ( acl_n >= 0 && acl_n < ACLLAST &&
						aclchk[aclact[acl_n]] == ACLERRCODE)					{
						/*
						** simulate shift of "error"
						*/
						acl_state = aclact[ acl_n ];
						goto acl_stack;
					}
					/*
					** current state has no shift on
					** "error", pop stack
					*/
#if ACLDEBUG
#	define _POP_ "Error recovery pops state %d, uncovers state %d\n"
					if ( acldebug )
						printf( _POP_, *acl_ps,
							acl_ps[-1] );
#	undef _POP_
#endif
					acl_ps--;
					acl_pv--;
				}
				/*
				** there is no state on stack with "error" as
				** a valid shift.  give up.
				*/
				ACLABORT;
			case 3:		/* no shift yet; eat a token */
#if ACLDEBUG
				/*
				** if debugging, look up token in list of
				** pairs.  0 and negative shouldn't occur,
				** but since timing doesn't matter when
				** debugging, it doesn't hurt to leave the
				** tests here.
				*/
				if ( acldebug )
				{
					register int acl_i;

					printf( "Error recovery discards " );
					if ( aclchar == 0 )
						printf( "token end-of-file\n" );
					else if ( aclchar < 0 )
						printf( "token -none-\n" );
					else
					{
						for ( acl_i = 0;
							acltoks[acl_i].t_val >= 0;
							acl_i++ )
						{
							if ( acltoks[acl_i].t_val
								== aclchar )
							{
								break;
							}
						}
						printf( "token %s\n",
							acltoks[acl_i].t_name );
					}
				}
#endif /* ACLDEBUG */
				if ( aclchar == 0 )	/* reached EOF. quit */
					ACLABORT;
				aclchar = -1;
				goto acl_newstate;
			}
		}/* end if ( acl_n == 0 ) */
		/*
		** reduction by production acl_n
		** put stack tops, etc. so things right after switch
		*/
#if ACLDEBUG
		/*
		** if debugging, print the string that is the user's
		** specification of the reduction which is just about
		** to be done.
		*/
		if ( acldebug )
			printf( "Reduce by (%d) \"%s\"\n",
				acl_n, aclreds[ acl_n ] );
#endif
		acltmp = acl_n;			/* value to switch over */
		aclpvt = acl_pv;			/* $vars top of value stack */
		/*
		** Look in goto table for next state
		** Sorry about using acl_state here as temporary
		** register variable, but why not, if it works...
		** If aclr2[ acl_n ] doesn't have the low order bit
		** set, then there is no action to be done for
		** this reduction.  So, no saving & unsaving of
		** registers done.  The only difference between the
		** code just after the if and the body of the if is
		** the goto acl_stack in the body.  This way the test
		** can be made before the choice of what to do is needed.
		*/
		{
			/* length of production doubled with extra bit */
			register int acl_len = aclr2[ acl_n ];

			if ( !( acl_len & 01 ) )
			{
				acl_len >>= 1;
				aclval = ( acl_pv -= acl_len )[1];	/* $$ = $1 */
				acl_state = aclpgo[ acl_n = aclr1[ acl_n ] ] +
					*( acl_ps -= acl_len ) + 1;
				if ( acl_state >= ACLLAST ||
					aclchk[ acl_state =
					aclact[ acl_state ] ] != -acl_n )
				{
					acl_state = aclact[ aclpgo[ acl_n ] ];
				}
				goto acl_stack;
			}
			acl_len >>= 1;
			aclval = ( acl_pv -= acl_len )[1];	/* $$ = $1 */
			acl_state = aclpgo[ acl_n = aclr1[ acl_n ] ] +
				*( acl_ps -= acl_len ) + 1;
			if ( acl_state >= ACLLAST ||
				aclchk[ acl_state = aclact[ acl_state ] ] != -acl_n )
			{
				acl_state = aclact[ aclpgo[ acl_n ] ];
			}
		}
					/* save until reenter driver code */
		aclstate = acl_state;
		aclps = acl_ps;
		aclpv = acl_pv;
	}
	/*
	** code supplied by user is placed in this switch
	*/
	switch( acltmp )
	{
		
case 3:
# line 266 "acltext.y"
{
		PERM_FREE(aclpvt[-0].string);
	} break;
case 8:
# line 286 "acltext.y"
{
		acl_free_args(curr_args_list);
	} break;
case 9:
# line 292 "acltext.y"
{
		curr_acl = ACL_AclNew(NULL, aclpvt[-0].string);
		PERM_FREE(aclpvt[-0].string);
		if ( ACL_ListAppend(NULL, curr_acl_list, curr_acl, 0) < 0 ) {
			aclerror("Couldn't add ACL to list.");
			return(-1);
		}
		acl_clear_args(curr_args_list);
		use_generic_rights = 0;
		if (strstr(curr_acl->tag, "READ")) {
			use_generic_rights++;
			acl_add_arg(curr_args_list, PERM_STRDUP("read"));
			acl_add_arg(curr_args_list, PERM_STRDUP("execute"));
			acl_add_arg(curr_args_list, PERM_STRDUP("list"));
			acl_add_arg(curr_args_list, PERM_STRDUP("info"));
                } if (strstr(curr_acl->tag, "WRITE")) {
			use_generic_rights++;
			acl_add_arg(curr_args_list, PERM_STRDUP("write"));
			acl_add_arg(curr_args_list, PERM_STRDUP("delete"));
                } 
	} break;
case 10:
# line 314 "acltext.y"
{
		curr_acl = ACL_AclNew(NULL, aclpvt[-0].string);
		PERM_FREE(aclpvt[-0].string);
		if ( ACL_ListAppend(NULL, curr_acl_list, curr_acl, 0) < 0 ) {
			aclerror("Couldn't add ACL to list.");
			return(-1);
		}
		acl_clear_args(curr_args_list);
		use_generic_rights = 0;
		if (strstr(curr_acl->tag, "READ")) {
			use_generic_rights++;
			acl_add_arg(curr_args_list, PERM_STRDUP("read"));
			acl_add_arg(curr_args_list, PERM_STRDUP("execute"));
			acl_add_arg(curr_args_list, PERM_STRDUP("list"));
			acl_add_arg(curr_args_list, PERM_STRDUP("info"));
                } if (strstr(curr_acl->tag, "WRITE")) {
			use_generic_rights++;
			acl_add_arg(curr_args_list, PERM_STRDUP("write"));
			acl_add_arg(curr_args_list, PERM_STRDUP("delete"));
                } 
	} break;
case 13:
# line 342 "acltext.y"
{
		char acl_tmp_arg[255];
		char *acl_new_arg;
                
                if (!use_generic_rights) {
			acl_string_lower(aclpvt[-0].string);
			strcpy(acl_tmp_arg, "http_");
			strcat(acl_tmp_arg, aclpvt[-0].string);
			PERM_FREE(aclpvt[-0].string);
			acl_new_arg = PERM_STRDUP(acl_tmp_arg);
			acl_add_arg(curr_args_list, acl_new_arg);
		} else {
			PERM_FREE(aclpvt[-0].string);
		}
	} break;
case 14:
# line 358 "acltext.y"
{
                if (!use_generic_rights) {
			acl_add_arg(curr_args_list, aclpvt[-0].string);
		} else {
			PERM_FREE(aclpvt[-0].string);
		}
	} break;
case 19:
# line 376 "acltext.y"
{
		if ( ACL_ExprSetPFlags(NULL, curr_expr, 
					ACL_PFLAG_ABSOLUTE) < 0 ) {
			aclerror("Could not set authorization processing flags");
			return(-1);
		}
	} break;
case 22:
# line 388 "acltext.y"
{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_ALLOW) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(allow) failed");
			return(-1);
		}
		acl_clear_args(curr_user_list);
		acl_clear_args(curr_ip_dns_list);
	} break;
case 23:
# line 398 "acltext.y"
{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_DENY) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(allow) failed");
			return(-1);
		}
		acl_clear_args(curr_user_list);
		acl_clear_args(curr_ip_dns_list);
	} break;
case 24:
# line 411 "acltext.y"
{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_AUTH) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(auth) failed");
			return(-1);
		}
		if ( ACL_ExprSetPFlags(NULL, curr_expr, 
					ACL_PFLAG_ABSOLUTE) < 0 ) {
			aclerror("Could not set authorization processing flags");
			return(-1);
		}
                curr_auth_info = PListCreate(NULL, ACL_ATTR_INDEX_MAX, 0, 0);
		if ( ACL_ExprAddAuthInfo(curr_expr, curr_auth_info) < 0 ) {
			aclerror("Could not set authorization info");
			return(-1);
		}
	} break;
case 26:
# line 430 "acltext.y"
{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_AUTH) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(auth) failed");
			return(-1);
		}
                curr_auth_info = PListCreate(NULL, ACL_ATTR_INDEX_MAX, 0, 0);
		if ( ACL_ExprAddAuthInfo(curr_expr, curr_auth_info) < 0 ) {
			aclerror("Could not set authorization info");
			return(-1);
		}
	} break;
case 28:
# line 446 "acltext.y"
{
		if ( acl_set_users_or_groups(curr_expr, curr_user_list) < 0 ) {
			aclerror("acl_set_users_or_groups() failed");
			return(-1);
		}

		if ( acl_set_ip_dns(curr_expr, curr_ip_dns_list) < 0 ) {
			aclerror("acl_set_ip_dns() failed");
			return(-1);
		}

		if ( ACL_ExprAnd(NULL, curr_expr)  < 0 ) {
			aclerror("ACL_ExprAnd() failed");
			return(-1);
		}

		if ( acl_set_args(curr_expr, curr_args_list) < 0 ) {
			aclerror("acl_set_args() failed");
			return(-1);
		}
	
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			aclerror("Could not add authorization");
			return(-1);
		}
	} break;
case 29:
# line 473 "acltext.y"
{
		if ( acl_set_users_or_groups(curr_expr, curr_user_list) < 0 ) {
			aclerror("acl_set_users_or_groups() failed");
			return(-1);
		}

                if ( acl_set_args(curr_expr, curr_args_list) < 0 ) {
                        aclerror("acl_set_args() failed");
                        return(-1);
                }

                if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
                        aclerror("Could not add authorization");
                        return(-1);
                }
	} break;
case 34:
# line 500 "acltext.y"
{
		acl_add_arg(curr_user_list, aclpvt[-0].string);
	} break;
case 35:
# line 504 "acltext.y"
{
		acl_add_arg(curr_user_list, aclpvt[-0].string);
	} break;
case 39:
# line 516 "acltext.y"
{
		acl_add_arg(curr_ip_dns_list, aclpvt[-0].string);
	} break;
case 40:
# line 520 "acltext.y"
{
		acl_add_arg(curr_ip_dns_list, aclpvt[-0].string);
	} break;
case 41:
# line 526 "acltext.y"
{
		char tmp_str[255];

		util_sprintf(tmp_str, "%s+%s", aclpvt[-1].string, aclpvt[-0].string);
		PERM_FREE(aclpvt[-1].string);
		PERM_FREE(aclpvt[-0].string);
		acl_add_arg(curr_ip_dns_list, PERM_STRDUP(tmp_str));
	} break;
case 46:
# line 543 "acltext.y"
{
		if ( ACL_ExprAddArg(NULL, curr_expr, "user") < 0 ) {
			aclerror("ACL_ExprAddArg() failed");
			return(-1);
		}

		if ( ACL_ExprAddArg(NULL, curr_expr, "group") < 0 ) {
			aclerror("ACL_ExprAddArg() failed");
			return(-1);
		}

		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			aclerror("Could not add authorization");
			return(-1);
		}
	} break;
case 47:
# line 562 "acltext.y"
{
		acl_string_lower(aclpvt[-2].string);
		if (strcmp(aclpvt[-2].string, "database") == 0) {
			PERM_FREE(aclpvt[-2].string);
			PERM_FREE(aclpvt[-1].string);
		} else {
			if ( PListInitProp(curr_auth_info, 
				   ACL_Attr2Index(aclpvt[-2].string), aclpvt[-2].string, aclpvt[-1].string, NULL) < 0 ) {
			}
			PERM_FREE(aclpvt[-2].string);
		}
	} break;
case 48:
# line 575 "acltext.y"
{
		acl_string_lower(aclpvt[-2].string);
		if (strcmp(aclpvt[-2].string, "database") == 0) {
			PERM_FREE(aclpvt[-2].string);
			PERM_FREE(aclpvt[-1].string);
		} else {
			if ( PListInitProp(curr_auth_info, 
				   ACL_Attr2Index(aclpvt[-2].string), aclpvt[-2].string, aclpvt[-1].string, NULL) < 0 ) {
			}
			PERM_FREE(aclpvt[-2].string);
		}
	} break;
case 56:
# line 611 "acltext.y"
{
		curr_acl = ACL_AclNew(NULL, aclpvt[-0].string);
		PERM_FREE(aclpvt[-0].string);
		if ( ACL_ListAppend(NULL, curr_acl_list, curr_acl, 0) < 0 ) {
			aclerror("Couldn't add ACL to list.");
			return(-1);
		}
	} break;
case 57:
# line 620 "acltext.y"
{
		curr_acl = ACL_AclNew(NULL, aclpvt[-0].string);
		PERM_FREE(aclpvt[-0].string);
		if ( ACL_ListAppend(NULL, curr_acl_list, curr_acl, 0) < 0 ) {
			aclerror("Couldn't add ACL to list.");
			return(-1);
		}
	} break;
case 63:
# line 641 "acltext.y"
{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_RESPONSE) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(deny) failed");
			return(-1);
		}
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			aclerror("Could not add authorization");
			return(-1);
		}
		if ( ACL_ExprSetPFlags(NULL, curr_expr,
                                        ACL_PFLAG_ABSOLUTE) < 0 ) {
                        aclerror("Could not set deny processing flags");
                        return(-1);
                }
	} break;
case 65:
# line 659 "acltext.y"
{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_RESPONSE) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(deny) failed");
			return(-1);
		}
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			aclerror("Could not add authorization");
			return(-1);
		}
	} break;
case 67:
# line 674 "acltext.y"
{
		acl_string_lower(aclpvt[-2].string);
                if ( ACL_ExprSetDenyWith(NULL, curr_expr, 
                                         aclpvt[-2].string, aclpvt[-0].string) < 0 ) {
                        aclerror("ACL_ExprSetDenyWith() failed");
                        return(-1);
                }
                PERM_FREE(aclpvt[-2].string);
                PERM_FREE(aclpvt[-0].string);
	} break;
case 68:
# line 687 "acltext.y"
{
		pflags = 0;
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_AUTH) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(allow) failed");
			return(-1);
		}
                curr_auth_info = PListCreate(NULL, ACL_ATTR_INDEX_MAX, 0, 0);
		if ( ACL_ExprAddAuthInfo(curr_expr, curr_auth_info) < 0 ) {
			aclerror("Could not set authorization info");
			return(-1);
		}
	} break;
case 69:
# line 701 "acltext.y"
{
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			aclerror("Could not add authorization");
			return(-1);
		}
	} break;
case 72:
# line 713 "acltext.y"
{
		acl_string_lower(aclpvt[-0].string);
		if ( ACL_ExprAddArg(NULL, curr_expr, aclpvt[-0].string) < 0 ) {
			aclerror("ACL_ExprAddArg() failed");
			return(-1);
		}
		PERM_FREE(aclpvt[-0].string);
	} break;
case 75:
# line 728 "acltext.y"
{
		acl_string_lower(aclpvt[-2].string);
		if ( PListInitProp(curr_auth_info, 
                                   ACL_Attr2Index(aclpvt[-2].string), aclpvt[-2].string, aclpvt[-0].string, NULL) < 0 ) {
		}
		PERM_FREE(aclpvt[-2].string);
	} break;
case 76:
# line 736 "acltext.y"
{
		acl_string_lower(aclpvt[-2].string);
		if ( PListInitProp(curr_auth_info, 
                                   ACL_Attr2Index(aclpvt[-2].string), aclpvt[-2].string, aclpvt[-0].string, NULL) < 0 ) {
		}
		PERM_FREE(aclpvt[-2].string);
	} break;
case 77:
# line 746 "acltext.y"
{
		pflags = 0;
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_ALLOW) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(allow) failed");
			return(-1);
		}
	} break;
case 79:
# line 756 "acltext.y"
{
		pflags = 0;
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_DENY) ;
		if ( curr_expr == NULL ) {
			aclerror("ACL_ExprNew(deny) failed");
			return(-1);
		}
	} break;
case 81:
# line 768 "acltext.y"
{
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			aclerror("Could not add authorization");
			return(-1);
		}
	} break;
case 82:
# line 775 "acltext.y"
{
		if ( ACL_ExprSetPFlags (NULL, curr_expr, pflags) < 0 ) {
			aclerror("Could not set authorization processing flags");
			return(-1);
		}
#ifdef DEBUG
		if ( ACL_ExprDisplay(curr_expr) < 0 ) {
			aclerror("ACL_ExprDisplay() failed");
			return(-1);
		}
		printf("Parsed authorization.\n");
#endif
	} break;
case 85:
# line 795 "acltext.y"
{
		pflags = ACL_PFLAG_ABSOLUTE;
	} break;
case 86:
# line 799 "acltext.y"
{
		pflags = ACL_PFLAG_ABSOLUTE;
	} break;
case 87:
# line 803 "acltext.y"
{
		pflags = ACL_PFLAG_CONTENT;
	} break;
case 88:
# line 807 "acltext.y"
{
		pflags = ACL_PFLAG_CONTENT;
	} break;
case 89:
# line 811 "acltext.y"
{
		pflags = ACL_PFLAG_TERMINAL;
	} break;
case 90:
# line 815 "acltext.y"
{
		pflags = ACL_PFLAG_TERMINAL;
	} break;
case 91:
# line 821 "acltext.y"
{
		pflags |= ACL_PFLAG_CONTENT;
	} break;
case 92:
# line 825 "acltext.y"
{
		pflags |= ACL_PFLAG_ABSOLUTE;
	} break;
case 93:
# line 829 "acltext.y"
{
		pflags |= ACL_PFLAG_ABSOLUTE | ACL_PFLAG_CONTENT;
	} break;
case 94:
# line 833 "acltext.y"
{
		pflags |= ACL_PFLAG_ABSOLUTE | ACL_PFLAG_CONTENT;
	} break;
case 95:
# line 839 "acltext.y"
{
		pflags |= ACL_PFLAG_CONTENT;
	} break;
case 96:
# line 843 "acltext.y"
{
		pflags |= ACL_PFLAG_TERMINAL;
	} break;
case 97:
# line 847 "acltext.y"
{
		pflags |= ACL_PFLAG_TERMINAL | ACL_PFLAG_CONTENT;
	} break;
case 98:
# line 851 "acltext.y"
{
		pflags |= ACL_PFLAG_TERMINAL | ACL_PFLAG_CONTENT;
	} break;
case 99:
# line 857 "acltext.y"
{
		pflags |= ACL_PFLAG_ABSOLUTE;
	} break;
case 100:
# line 861 "acltext.y"
{
		pflags |= ACL_PFLAG_TERMINAL;
	} break;
case 101:
# line 865 "acltext.y"
{
		pflags |= ACL_PFLAG_TERMINAL | ACL_PFLAG_ABSOLUTE;
	} break;
case 102:
# line 869 "acltext.y"
{
		pflags |= ACL_PFLAG_TERMINAL | ACL_PFLAG_ABSOLUTE;
	} break;
case 105:
# line 879 "acltext.y"
{
		acl_string_lower(aclpvt[-0].string);
		if ( ACL_ExprAddArg(NULL, curr_expr, aclpvt[-0].string) < 0 ) {
			aclerror("ACL_ExprAddArg() failed");
			return(-1);
		}
		PERM_FREE( aclpvt[-0].string );
	} break;
case 107:
# line 891 "acltext.y"
{
                if ( ACL_ExprAnd(NULL, curr_expr) < 0 ) {
                        aclerror("ACL_ExprAnd() failed");
                        return(-1);
                }
        } break;
case 108:
# line 898 "acltext.y"
{
                if ( ACL_ExprOr(NULL, curr_expr) < 0 ) {
                        aclerror("ACL_ExprOr() failed");
                        return(-1);
                }
        } break;
case 111:
# line 909 "acltext.y"
{
                if ( ACL_ExprNot(NULL, curr_expr) < 0 ) {
                        aclerror("ACL_ExprNot() failed");
                        return(-1);
                }
        } break;
case 112:
# line 918 "acltext.y"
{
		acl_string_lower(aclpvt[-2].string);
		if ( ACL_ExprTerm(NULL, curr_expr,
				aclpvt[-2].string, (CmpOp_t) aclpvt[-1].ival, aclpvt[-0].string) < 0 ) {
			aclerror("ACL_ExprTerm() failed");
			PERM_FREE(aclpvt[-2].string);
			PERM_FREE(aclpvt[-0].string);	
			return(-1);
		}
		PERM_FREE(aclpvt[-2].string);
		PERM_FREE(aclpvt[-0].string);	
	} break;
case 113:
# line 931 "acltext.y"
{
		acl_string_lower(aclpvt[-2].string);
		if ( ACL_ExprTerm(NULL, curr_expr,
				aclpvt[-2].string, (CmpOp_t) aclpvt[-1].ival, aclpvt[-0].string) < 0 ) {
			aclerror("ACL_ExprTerm() failed");
			PERM_FREE(aclpvt[-2].string);
			PERM_FREE(aclpvt[-0].string);	
			return(-1);
		}
		PERM_FREE(aclpvt[-2].string);
		PERM_FREE(aclpvt[-0].string);	
	} break;
	}
	goto aclstack;		/* reset registers in driver code */
}

