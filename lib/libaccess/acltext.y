/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 *  This grammar is intended to parse the version 3.0 
 *  and version 2.0 ACL text files and output an ACLListHandle_t 
 *  structure.
 */

%{
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
			free(args_list[ii]);
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
				yyerror("ACL_ExprAddArg() failed");
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
				yyerror("ACL_ExprTerm() failed");
				acl_free_args(user_list);
				return(-1);
			}
			if ( ACL_ExprTerm(NULL, expr, "group", CMP_OP_EQ, 
					user_list[ii]) < 0 ) {
				yyerror("ACL_ExprTerm() failed");
				acl_free_args(user_list);
				return(-1);
			}
		} else
			break;
	}

	acl_free_args(user_list);

	for (jj = 0; jj < (ii * 2) - 1; jj++) {
		if ( ACL_ExprOr(NULL, expr)  < 0 ) {
			yyerror("ACL_ExprOr() failed");
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
                                yyerror("ACL_ExprTerm() failed");
                                acl_free_args(ip_dns);
                                return(-1);
                        }

                } else
                        break;
        }

        acl_free_args(ip_dns);

        for (jj = 0; jj < ii - 1; jj++) {
                if ( ACL_ExprOr(NULL, expr)  < 0 ) {
                        yyerror("ACL_ExprOr() failed");
                        return(-1);
                }
        }

        return(0);
}


%}

%union {
	char	*string;
	int	ival;
}

%token ACL_ABSOLUTE_TOK
%token ACL_ACL_TOK
%token ACL_ALLOW_TOK
%token ACL_ALWAYS_TOK
%token ACL_AND_TOK
%token ACL_AT_TOK
%token ACL_AUTHENTICATE_TOK
%token ACL_CONTENT_TOK
%token ACL_DEFAULT_TOK
%token ACL_DENY_TOK
%token ACL_GROUP_TOK
%token ACL_IN_TOK
%token ACL_INHERIT_TOK
%token ACL_NOT_TOK 
%token ACL_NULL_TOK
%token ACL_OR_TOK
%token <string> ACL_QSTRING_TOK
%token ACL_READ_TOK
%token ACL_TERMINAL_TOK
%token <string> ACL_VARIABLE_TOK
%token ACL_VERSION_TOK
%token ACL_WRITE_TOK
%token ACL_WITH_TOK

%token <ival> ACL_EQ_TOK
%token <ival> ACL_GE_TOK
%token <ival> ACL_GT_TOK
%token <ival> ACL_LE_TOK
%token <ival> ACL_LT_TOK
%token <ival> ACL_NE_TOK

%%

/*
 * If no version is specified then we have a version 2.0 ACL.
 */
start:	| start_acl_v2
	| ACL_VERSION_TOK ACL_VARIABLE_TOK 
	{
		free($<string>2);
	}
	';' start_acl_v3
	;

/*
 ************************************************************ 
 * Parse version 2.0 ACL
 ************************************************************ 
 */
start_acl_v2: acl_list_v2
	;

acl_list_v2: acl_v2
	| acl_list_v2 acl_v2
	;

acl_v2: ACL_ACL_TOK acl_name_v2  
	'(' arg_list_v2 ')'  '{' directive_list_v2 '}' 
	{
		acl_free_args(curr_args_list);
	}
	;

acl_name_v2: ACL_VARIABLE_TOK
	{
		curr_acl = ACL_AclNew(NULL, $<string>1);
		free($<string>1);
		if ( ACL_ListAppend(NULL, curr_acl_list, curr_acl, 0) < 0 ) {
			yyerror("Couldn't add ACL to list.");
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
	}
	| ACL_QSTRING_TOK
	{
		curr_acl = ACL_AclNew(NULL, $<string>1);
		free($<string>1);
		if ( ACL_ListAppend(NULL, curr_acl_list, curr_acl, 0) < 0 ) {
			yyerror("Couldn't add ACL to list.");
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
	}
	;

arg_list_v2: arg_v2
	| arg_v2 ',' arg_list_v2
	;

arg_v2: ACL_VARIABLE_TOK
	{
		char acl_tmp_arg[255];
		char *acl_new_arg;
                
                if (!use_generic_rights) {
			acl_string_lower($<string>1);
			strcpy(acl_tmp_arg, "http_");
			strcat(acl_tmp_arg, $<string>1);
			PERM_FREE($<string>1);
			acl_new_arg = PERM_STRDUP(acl_tmp_arg);
			acl_add_arg(curr_args_list, acl_new_arg);
		} else {
			PERM_FREE($<string>1);
		}
	}
	| ACL_QSTRING_TOK
	{
                if (!use_generic_rights) {
			acl_add_arg(curr_args_list, $<string>1);
		} else {
			PERM_FREE($<string>1);
		}
	}
	;

directive_list_v2: directive_v2 ';'
	| directive_v2 ';' directive_list_v2
	;

directive_v2: auth_method_v2
	| auth_statement_v2
	;

auth_statement_v2: ACL_ALWAYS_TOK auth_type_v2
	{
		if ( ACL_ExprSetPFlags(NULL, curr_expr, 
					ACL_PFLAG_ABSOLUTE) < 0 ) {
			yyerror("Could not set authorization processing flags");
			return(-1);
		}
	}
	host_spec_list_action_v2
	| ACL_DEFAULT_TOK auth_type_v2 host_spec_list_action_v2
	;

auth_type_v2: ACL_ALLOW_TOK
	{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_ALLOW) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(allow) failed");
			return(-1);
		}
		acl_clear_args(curr_user_list);
		acl_clear_args(curr_ip_dns_list);
	}
	| ACL_DENY_TOK
	{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_DENY) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(allow) failed");
			return(-1);
		}
		acl_clear_args(curr_user_list);
		acl_clear_args(curr_ip_dns_list);
	}
	;

auth_method_v2: 
	ACL_ALWAYS_TOK ACL_AUTHENTICATE_TOK ACL_IN_TOK 
	{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_AUTH) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(auth) failed");
			return(-1);
		}
		if ( ACL_ExprSetPFlags(NULL, curr_expr, 
					ACL_PFLAG_ABSOLUTE) < 0 ) {
			yyerror("Could not set authorization processing flags");
			return(-1);
		}
                curr_auth_info = PListCreate(NULL, ACL_ATTR_INDEX_MAX, 0, 0);
		if ( ACL_ExprAddAuthInfo(curr_expr, curr_auth_info) < 0 ) {
			yyerror("Could not set authorization info");
			return(-1);
		}
	}
	realm_definition_v2
	| ACL_DEFAULT_TOK ACL_AUTHENTICATE_TOK ACL_IN_TOK 
	{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_AUTH) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(auth) failed");
			return(-1);
		}
                curr_auth_info = PListCreate(NULL, ACL_ATTR_INDEX_MAX, 0, 0);
		if ( ACL_ExprAddAuthInfo(curr_expr, curr_auth_info) < 0 ) {
			yyerror("Could not set authorization info");
			return(-1);
		}
	}
	realm_definition_v2
	;

host_spec_list_action_v2: user_expr_v2 ACL_AT_TOK host_spec_list_v2
	{
		if ( acl_set_users_or_groups(curr_expr, curr_user_list) < 0 ) {
			yyerror("acl_set_users_or_groups() failed");
			return(-1);
		}

		if ( acl_set_ip_dns(curr_expr, curr_ip_dns_list) < 0 ) {
			yyerror("acl_set_ip_dns() failed");
			return(-1);
		}

		if ( ACL_ExprAnd(NULL, curr_expr)  < 0 ) {
			yyerror("ACL_ExprAnd() failed");
			return(-1);
		}

		if ( acl_set_args(curr_expr, curr_args_list) < 0 ) {
			yyerror("acl_set_args() failed");
			return(-1);
		}
	
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			yyerror("Could not add authorization");
			return(-1);
		}
	}
	| user_expr_v2 
	{
		if ( acl_set_users_or_groups(curr_expr, curr_user_list) < 0 ) {
			yyerror("acl_set_users_or_groups() failed");
			return(-1);
		}

                if ( acl_set_args(curr_expr, curr_args_list) < 0 ) {
                        yyerror("acl_set_args() failed");
                        return(-1);
                }

                if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
                        yyerror("Could not add authorization");
                        return(-1);
                }
	}
	;

user_expr_v2: user_v2
	| '(' user_list_v2 ')'
	;

user_list_v2: user_v2
	| user_v2 ',' user_list_v2
	;

user_v2: ACL_VARIABLE_TOK
	{
		acl_add_arg(curr_user_list, $<string>1);
	}
	| ACL_QSTRING_TOK
	{
		acl_add_arg(curr_user_list, $<string>1);
	}
	;

	
host_spec_list_v2: dns_spec_v2 
	| ip_spec_v2 
	| '(' dns_ip_spec_list_v2 ')' 
	;

dns_spec_v2: ACL_VARIABLE_TOK
	{
		acl_add_arg(curr_ip_dns_list, $<string>1);
	}
	| ACL_QSTRING_TOK
	{
		acl_add_arg(curr_ip_dns_list, $<string>1);
	}
	;

ip_spec_v2: ACL_VARIABLE_TOK ACL_VARIABLE_TOK
	{
		char tmp_str[255];

		util_sprintf(tmp_str, "%s+%s", $<string>1, $<string>2);
		free($<string>1);
		free($<string>2);
		acl_add_arg(curr_ip_dns_list, PERM_STRDUP(tmp_str));
	}
	;

dns_ip_spec_list_v2: dns_spec_v2
	| ip_spec_v2
	| dns_spec_v2 ',' dns_ip_spec_list_v2
	| ip_spec_v2 ',' dns_ip_spec_list_v2
	;

realm_definition_v2: '{' methods_list_v2 '}' 
	{
		if ( ACL_ExprAddArg(NULL, curr_expr, "user") < 0 ) {
			yyerror("ACL_ExprAddArg() failed");
			return(-1);
		}

		if ( ACL_ExprAddArg(NULL, curr_expr, "group") < 0 ) {
			yyerror("ACL_ExprAddArg() failed");
			return(-1);
		}

		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			yyerror("Could not add authorization");
			return(-1);
		}
	}
	;

method_v2: ACL_VARIABLE_TOK ACL_VARIABLE_TOK ';'
	{
		acl_string_lower($<string>1);
		if (strcmp($<string>1, "database") == 0) {
			free($<string>1);
			free($<string>2);
		} else {
			if ( PListInitProp(curr_auth_info, 
				   ACL_Attr2Index($<string>1), $<string>1, $<string>2, NULL) < 0 ) {
			}
			free($<string>1);
		}
	}
	| ACL_VARIABLE_TOK ACL_QSTRING_TOK ';'
	{
		acl_string_lower($<string>1);
		if (strcmp($<string>1, "database") == 0) {
			free($<string>1);
			free($<string>2);
		} else {
			if ( PListInitProp(curr_auth_info, 
				   ACL_Attr2Index($<string>1), $<string>1, $<string>2, NULL) < 0 ) {
			}
			free($<string>1);
		}
	}
	;

methods_list_v2: method_v2 
	| method_v2 methods_list_v2
	;
	
/*
 ************************************************************ 
 * Parse version 3.0 ACL
 ************************************************************ 
 */

start_acl_v3: acl_list
	;

acl_list: acl
	| acl_list acl
	;

acl:	named_acl ';' body_list
	| named_acl ';'  
	;

named_acl: ACL_ACL_TOK ACL_VARIABLE_TOK 
	{
		curr_acl = ACL_AclNew(NULL, $<string>2);
		free($<string>2);
		if ( ACL_ListAppend(NULL, curr_acl_list, curr_acl, 0) < 0 ) {
			yyerror("Couldn't add ACL to list.");
			return(-1);
		}
	}
	| ACL_ACL_TOK ACL_QSTRING_TOK 
	{
		curr_acl = ACL_AclNew(NULL, $<string>2);
		free($<string>2);
		if ( ACL_ListAppend(NULL, curr_acl_list, curr_acl, 0) < 0 ) {
			yyerror("Couldn't add ACL to list.");
			return(-1);
		}
	}
	;

body_list: body
	| body body_list
	;

body: authenticate_statement ';'
	| authorization_statement ';'
	| deny_statement ';'
	;

deny_statement: 	
	ACL_ABSOLUTE_TOK ACL_DENY_TOK ACL_WITH_TOK 
	{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_RESPONSE) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(deny) failed");
			return(-1);
		}
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			yyerror("Could not add authorization");
			return(-1);
		}
		if ( ACL_ExprSetPFlags(NULL, curr_expr,
                                        ACL_PFLAG_ABSOLUTE) < 0 ) {
                        yyerror("Could not set deny processing flags");
                        return(-1);
                }
	}
        deny_common
	| ACL_DENY_TOK ACL_WITH_TOK 
	{
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_RESPONSE) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(deny) failed");
			return(-1);
		}
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			yyerror("Could not add authorization");
			return(-1);
		}
	}
	deny_common
	;

deny_common: ACL_VARIABLE_TOK ACL_EQ_TOK ACL_QSTRING_TOK
	{
		acl_string_lower($<string>1);
                if ( ACL_ExprSetDenyWith(NULL, curr_expr, 
                                         $<string>1, $<string>3) < 0 ) {
                        yyerror("ACL_ExprSetDenyWith() failed");
                        return(-1);
                }
                free($<string>1);
                free($<string>3);
	}
	;

authenticate_statement: ACL_AUTHENTICATE_TOK 
	{
		pflags = 0;
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_AUTH) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(allow) failed");
			return(-1);
		}
                curr_auth_info = PListCreate(NULL, ACL_ATTR_INDEX_MAX, 0, 0);
		if ( ACL_ExprAddAuthInfo(curr_expr, curr_auth_info) < 0 ) {
			yyerror("Could not set authorization info");
			return(-1);
		}
	}
	'(' attribute_list ')' '{' parameter_list '}'
	{
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			yyerror("Could not add authorization");
			return(-1);
		}
	}
	;

attribute_list: attribute
	| attribute_list ',' attribute

attribute: ACL_VARIABLE_TOK
	{
		acl_string_lower($<string>1);
		if ( ACL_ExprAddArg(NULL, curr_expr, $<string>1) < 0 ) {
			yyerror("ACL_ExprAddArg() failed");
			return(-1);
		}
		free($<string>1);
	}
	;

parameter_list: parameter ';'
	| parameter ';' parameter_list
	;

parameter: ACL_VARIABLE_TOK ACL_EQ_TOK ACL_QSTRING_TOK
	{
		acl_string_lower($<string>1);
		if ( PListInitProp(curr_auth_info, 
                                   ACL_Attr2Index($<string>1), $<string>1, $<string>3, NULL) < 0 ) {
		}
		free($<string>1);
	}
	| ACL_VARIABLE_TOK ACL_EQ_TOK ACL_VARIABLE_TOK
	{
		acl_string_lower($<string>1);
		if ( PListInitProp(curr_auth_info, 
                                   ACL_Attr2Index($<string>1), $<string>1, $<string>3, NULL) < 0 ) {
		}
		free($<string>1);
	}
	;

authorization_statement: ACL_ALLOW_TOK 
	{
		pflags = 0;
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_ALLOW) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(allow) failed");
			return(-1);
		}
	}
	auth_common_action
	| ACL_DENY_TOK 
	{
		pflags = 0;
		curr_expr = ACL_ExprNew(ACL_EXPR_TYPE_DENY) ;
		if ( curr_expr == NULL ) {
			yyerror("ACL_ExprNew(deny) failed");
			return(-1);
		}
	}
	auth_common_action
	;

auth_common_action: 
	{
		if ( ACL_ExprAppend(NULL, curr_acl, curr_expr) < 0 ) {
			yyerror("Could not add authorization");
			return(-1);
		}
	}
	auth_common
	{
		if ( ACL_ExprSetPFlags (NULL, curr_expr, pflags) < 0 ) {
			yyerror("Could not set authorization processing flags");
			return(-1);
		}
#ifdef DEBUG
		if ( ACL_ExprDisplay(curr_expr) < 0 ) {
			yyerror("ACL_ExprDisplay() failed");
			return(-1);
		}
		printf("Parsed authorization.\n");
#endif
	}
	;

auth_common: flag_list '(' args_list ')' expression 
	;

flag_list: 
	| ACL_ABSOLUTE_TOK
	{
		pflags = ACL_PFLAG_ABSOLUTE;
	}
	| ACL_ABSOLUTE_TOK content_static
	{
		pflags = ACL_PFLAG_ABSOLUTE;
	}
	| ACL_CONTENT_TOK 
	{
		pflags = ACL_PFLAG_CONTENT;
	}
	| ACL_CONTENT_TOK absolute_static
	{
		pflags = ACL_PFLAG_CONTENT;
	}
	| ACL_TERMINAL_TOK
	{
		pflags = ACL_PFLAG_TERMINAL;
	}
	| ACL_TERMINAL_TOK content_absolute
	{
		pflags = ACL_PFLAG_TERMINAL;
	}
	;

content_absolute: ACL_CONTENT_TOK
	{
		pflags |= ACL_PFLAG_CONTENT;
	}
	| ACL_ABSOLUTE_TOK
	{
		pflags |= ACL_PFLAG_ABSOLUTE;
	}
	| ACL_CONTENT_TOK ACL_ABSOLUTE_TOK
	{
		pflags |= ACL_PFLAG_ABSOLUTE | ACL_PFLAG_CONTENT;
	}
	| ACL_ABSOLUTE_TOK ACL_CONTENT_TOK
	{
		pflags |= ACL_PFLAG_ABSOLUTE | ACL_PFLAG_CONTENT;
	}
	;

content_static: ACL_CONTENT_TOK
	{
		pflags |= ACL_PFLAG_CONTENT;
	}
	| ACL_TERMINAL_TOK
	{
		pflags |= ACL_PFLAG_TERMINAL;
	}
	| ACL_CONTENT_TOK ACL_TERMINAL_TOK
	{
		pflags |= ACL_PFLAG_TERMINAL | ACL_PFLAG_CONTENT;
	}
	| ACL_TERMINAL_TOK ACL_CONTENT_TOK
	{
		pflags |= ACL_PFLAG_TERMINAL | ACL_PFLAG_CONTENT;
	}
	;

absolute_static: ACL_ABSOLUTE_TOK
	{
		pflags |= ACL_PFLAG_ABSOLUTE;
	}
	| ACL_TERMINAL_TOK
	{
		pflags |= ACL_PFLAG_TERMINAL;
	}
	| ACL_ABSOLUTE_TOK ACL_TERMINAL_TOK
	{
		pflags |= ACL_PFLAG_TERMINAL | ACL_PFLAG_ABSOLUTE;
	}
	| ACL_TERMINAL_TOK ACL_ABSOLUTE_TOK
	{
		pflags |= ACL_PFLAG_TERMINAL | ACL_PFLAG_ABSOLUTE;
	}
	;

args_list: arg
	| args_list ',' arg
	;

arg: ACL_VARIABLE_TOK 
	{
		acl_string_lower($<string>1);
		if ( ACL_ExprAddArg(NULL, curr_expr, $<string>1) < 0 ) {
			yyerror("ACL_ExprAddArg() failed");
			return(-1);
		}
		free( $<string>1 );
	}
	;

expression: factor
	| factor ACL_AND_TOK expression
        {
                if ( ACL_ExprAnd(NULL, curr_expr) < 0 ) {
                        yyerror("ACL_ExprAnd() failed");
                        return(-1);
                }
        }
	| factor ACL_OR_TOK expression
        {
                if ( ACL_ExprOr(NULL, curr_expr) < 0 ) {
                        yyerror("ACL_ExprOr() failed");
                        return(-1);
                }
        }
	;

factor: base_expr
	| '(' expression ')'
	| ACL_NOT_TOK factor
	{
                if ( ACL_ExprNot(NULL, curr_expr) < 0 ) {
                        yyerror("ACL_ExprNot() failed");
                        return(-1);
                }
        }
	;

base_expr: ACL_VARIABLE_TOK relop ACL_QSTRING_TOK 
	{
		acl_string_lower($<string>1);
		if ( ACL_ExprTerm(NULL, curr_expr,
				$<string>1, (CmpOp_t) $<ival>2, $<string>3) < 0 ) {
			yyerror("ACL_ExprTerm() failed");
			free($<string>1);
			free($<string>3);	
			return(-1);
		}
		free($<string>1);
		free($<string>3);	
	}
	| ACL_VARIABLE_TOK relop ACL_VARIABLE_TOK 
	{
		acl_string_lower($<string>1);
		if ( ACL_ExprTerm(NULL, curr_expr,
				$<string>1, (CmpOp_t) $<ival>2, $<string>3) < 0 ) {
			yyerror("ACL_ExprTerm() failed");
			free($<string>1);
			free($<string>3);	
			return(-1);
		}
		free($<string>1);
		free($<string>3);	
	}
	;

relop: ACL_EQ_TOK
	| ACL_GE_TOK
	| ACL_GT_TOK
	| ACL_LT_TOK
	| ACL_LE_TOK
	| ACL_NE_TOK
	;
%%
