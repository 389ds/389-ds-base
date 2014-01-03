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

/*
 * Tools to build and maintain access control lists.
 */

#include <stdio.h>
#include <string.h>

#define	ALLOCATE_ATTR_TABLE	1	/* Include the table of PList names */

#include <netsite.h>
#include <base/plist.h>
#include <base/util.h>
#include <base/crit.h>
#include <base/file.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/aclerror.h>
#include <libaccess/symbols.h>
#include <libaccess/aclstruct.h>
#include <libaccess/las.h>

#include "aclscan.h"
#include "parse.h"
#include "oneeval.h"

#include <libaccess/authdb.h>

static CRITICAL 	acl_parse_crit = NULL;

/*
 * Allocate a new ACL handle
 *
 * This function creates a new ACL structure that will be used for
 * access control information.
 *
 * Input:
 *    tag		Specifies an identifier name for the new ACL, or
 *			it may be NULL when no name is required.
 * Returns:
 *			A new ACL structure.
 */

NSAPI_PUBLIC ACLHandle_t *
ACL_AclNew(NSErr_t *errp, char *tag )
{
ACLHandle_t *handle;

    handle = ( ACLHandle_t * ) PERM_CALLOC ( 1 * sizeof (ACLHandle_t) );
    if ( handle && tag ) {
        handle->tag = PERM_STRDUP( tag );    
        if ( handle->tag == NULL ) {
            PERM_FREE(handle);
            return(NULL);
        }
    }
    return(handle);
}

/*
 * Appends to a specified ACL
 *
 * This function appends a specified ACL to the end of a given ACL list.
 * 
 * Input:
 *    errp		The error stack
 *    flags		should always be zero now
 *    acl_list		target ACL list
 *    acl		new acl
 * Returns:
 *    < 0		failure
 *    > 0		The number of acl's in the current list
 */

NSAPI_PUBLIC int
ACL_ExprAppend( NSErr_t *errp, ACLHandle_t *acl, 
        ACLExprHandle_t *expr )
{
    
    if ( acl == NULL || expr == NULL )
        return(ACLERRUNDEF);

    expr->acl_tag = acl->tag;

    if ( expr->expr_type == ACL_EXPR_TYPE_AUTH || 
         expr->expr_type == ACL_EXPR_TYPE_RESPONSE ) {
        expr->expr_number = -1;  // expr number isn't valid 
    } else {
        acl->expr_count++;
        expr->expr_number = acl->expr_count;
    }

    if ( acl->expr_list_head == NULL ) {
        acl->expr_list_head = expr;
        acl->expr_list_tail = expr;
    } else {
        acl->expr_list_tail->expr_next = expr;
        acl->expr_list_tail = expr;
    }

    return(acl->expr_count);
}

/*
 * Add authentication information to an ACL
 *
 * This function adds authentication data to an expr, based on
 * the information provided by the parameters.
 *
 * Input:
 *	expr		an authenticate expression to add database
 *			and method information to. ie, auth_info
 *	auth_info	authentication information, eg database, 
 *			method, etc.
 * Returns:
 *    0			success
 *    < 0		failure
 */

NSAPI_PUBLIC int 
ACL_ExprAddAuthInfo( ACLExprHandle_t *expr, PList_t auth_info )
{
    if ( expr == NULL || auth_info == NULL )
        return(ACLERRUNDEF);

    expr->expr_auth = auth_info;

    return(0);
}

/*
 * Add authorization information to an ACL
 *
 * This function adds an authorization to a given ACL, based on the information
 * provided by the parameters.
 *
 * Input:
 *    errp		The error stack
 *    access_rights	strings which identify the access rights to be
 *			controlled by the generated expr.
 *    flags		processing flags
 *    allow		non-zero to allow the indicated rights, or zero to
 *			deny them.
 *    attr_expr		handle for an attribute expression, which may be
 *			obtained by calling ACL_ExprNew()
 * Returns:
 *    0			success
 *    < 0 		failure
 */

NSAPI_PUBLIC int 
ACL_AddPermInfo( NSErr_t *errp, ACLHandle_t *acl,
        char **access_rights,
        PFlags_t flags,
        int allow,
        ACLExprHandle_t *expr,
        char *tag )
{
    if ( acl == NULL || expr == NULL ) 
        return(ACLERRUNDEF);
    
    expr->expr_flags = flags;
    expr->expr_argv = (char **) access_rights;
    expr->expr_tag = PERM_STRDUP( tag );
    if ( expr->expr_tag == NULL )
        return(ACLERRNOMEM);
    return(ACL_ExprAppend( errp, acl, expr ));
}

/*
 * Add rights information to an expression
 *
 * This function adds a right to an authorization, based on the information
 * provided by the parameters.
 *
 * Input:
 *    errp		The error stack
 *    access_right	strings which identify the access rights to be
 *			controlled by the generated expr.
 *    expr		handle for an attribute expression, which may be
 *			obtained by calling ACL_ExprNew()
 * Returns:
 *    0			success
 *    < 0		failure
 */

NSAPI_PUBLIC int
ACL_ExprAddArg( NSErr_t *errp, 
    ACLExprHandle_t *expr, 
    const char *arg )
{

    if ( expr == NULL ) 
        return(ACLERRUNDEF);

    if (expr->expr_argv == NULL)
        expr->expr_argv = (char **) PERM_MALLOC( 2 * sizeof(char *) );
    else
        expr->expr_argv = (char **) PERM_REALLOC( expr->expr_argv,
                                        (expr->expr_argc+2)
                                        * sizeof(char *) );
    
    if (expr->expr_argv == NULL) 
        return(ACLERRNOMEM);
    
    expr->expr_argv[expr->expr_argc] = PERM_STRDUP( arg );
    if (expr->expr_argv[expr->expr_argc] == NULL) 
        return(ACLERRNOMEM);
    expr->expr_argc++;
    expr->expr_argv[expr->expr_argc] = NULL;
    
    return(0);

}


NSAPI_PUBLIC int
ACL_ExprSetDenyWith( NSErr_t *errp, ACLExprHandle_t *expr, char *deny_type, char *deny_response)
{
int rv;

    if ( expr->expr_argc == 0 ) {
       if ( (rv = ACL_ExprAddArg(errp, expr, deny_type)) < 0 ) 
           return(rv);
       if ( (rv = ACL_ExprAddArg(errp, expr, deny_response)) < 0 ) 
           return(rv);
    } else if ( expr->expr_argc == 2 ) {
       if ( deny_type ) {
           if ( expr->expr_argv[0] ) 
               PERM_FREE(expr->expr_argv[0]);
           expr->expr_argv[0] = PERM_STRDUP(deny_type);
           if ( expr->expr_argv[0] == NULL )
               return(ACLERRNOMEM);
       }
       if ( deny_response ) {
           if ( expr->expr_argv[1] ) 
               PERM_FREE(expr->expr_argv[1]);
           expr->expr_argv[1] = PERM_STRDUP(deny_response);
           if ( expr->expr_argv[0] == NULL )
               return(ACLERRNOMEM);
       }
    } else {
        return(ACLERRINTERNAL);
    }
    return(0);
}

NSAPI_PUBLIC int
ACL_ExprGetDenyWith( NSErr_t *errp, ACLExprHandle_t *expr, char **deny_type,
char **deny_response)
{
    if ( expr->expr_argc == 2 ) {
        *deny_type = expr->expr_argv[0];
        *deny_response = expr->expr_argv[1];
        return(0);
    } else {
        return(ACLERRUNDEF);
    }
}

/*
 * Function to set the authorization statement processing flags.
 *
 * Input:
 *	errp	The error reporting stack
 *	expr	The authoization statement
 *	flags	The flags to set
 * Returns:
 *	0	success
 *	< 0	failure
 */

NSAPI_PUBLIC int
ACL_ExprSetPFlags( NSErr_t *errp,
    ACLExprHandle_t *expr,
    PFlags_t flags )
{
    if ( expr == NULL )
        return(ACLERRUNDEF);

    expr->expr_flags |= flags;
    return(0);
}
        
/*
 * Function to clear the authorization statement processing flags.
 *
 * Input:
 *	errp	The error reporting stack
 *	expr	The authoization statement
 * Returns:
 *	0	success
 *	< 0	failure
 */

NSAPI_PUBLIC int
ACL_ExprClearPFlags( NSErr_t *errp,
    ACLExprHandle_t *expr )
{
    if ( expr == NULL )
        return(ACLERRUNDEF);

    expr->expr_flags = 0;
    return(0);
}
        
/*
 * Allocate a new expression handle.
 *
 * Returns:
 *     NULL		If handle could not be allocated.
 *     pointer		New handle.
 */ 

NSAPI_PUBLIC ACLExprHandle_t *
ACL_ExprNew( const ACLExprType_t expr_type )
{
ACLExprHandle_t    *expr_handle;

    expr_handle = ( ACLExprHandle_t * ) PERM_CALLOC ( sizeof(ACLExprHandle_t) );
    if ( expr_handle ) {
        expr_handle->expr_arry = ( ACLExprEntry_t * ) 
            PERM_CALLOC( ACL_TERM_BSIZE * sizeof(ACLExprEntry_t) ) ;
        expr_handle->expr_arry_size = ACL_TERM_BSIZE;
	expr_handle->expr_type = expr_type;

        expr_handle->expr_raw = ( ACLExprRaw_t * ) 
            PERM_CALLOC( ACL_TERM_BSIZE * sizeof(ACLExprRaw_t) ) ;
        expr_handle->expr_raw_size = ACL_TERM_BSIZE;

    }
    return(expr_handle);
}


/*
 * LOCAL FUNCTION
 *
 * displays the ASCII equivalent index value.
 */

static char *
acl_index_string ( int value, char *buffer )
{
	
    if ( value == ACL_TRUE_IDX ) {
        strcpy( buffer, "TRUE" );
        return( buffer );
    }	

    if ( value == ACL_FALSE_IDX ) {
        strcpy( buffer, "FALSE" );
        return( buffer );
    }	

    sprintf( buffer, "goto %d", value );
    return( buffer );
}
    
    
/*
 * LOCAL FUNCTION
 *
 * displays ASCII equivalent of CmpOp_t
 */

static const char *
acl_comp_string( CmpOp_t cmp )
{
    switch (cmp) {
    case CMP_OP_EQ:
        return("=");
    case CMP_OP_NE:
        return("!=");
    case CMP_OP_GT:
        return(">");
    case CMP_OP_LT:
        return("<");
    case CMP_OP_GE:
        return(">=");
    case CMP_OP_LE:
        return("<=");
    default:
        return("unknown op");
    }
}

/*
 * Add a term to the specified attribute expression.  
 *
 * Input:
 *    errp		Error stack
 *    acl_expr		Target expression handle
 *    attr_name		Term Attribute name 
 *    cmp		Comparison operator
 *    attr_pattern	Pattern for comparison
 * Ouput:
 *    acl_expr		New term added
 * Returns:
 *    0			Success
 *    < 0		Error
 */

NSAPI_PUBLIC int 
ACL_ExprTerm( NSErr_t *errp, ACLExprHandle_t *acl_expr,
        const char *attr_name,
        CmpOp_t cmp, 
        char *attr_pattern )
{
ACLExprEntry_t	*expr;
ACLExprRaw_t	*raw_expr;

    if ( acl_expr == NULL || acl_expr->expr_arry == NULL )
        return(ACLERRUNDEF);

    if ( acl_expr->expr_term_index >= acl_expr->expr_arry_size  ) {
        acl_expr->expr_arry = ( ACLExprEntry_t *) 
            PERM_REALLOC ( acl_expr->expr_arry, 
			   (acl_expr->expr_arry_size + ACL_TERM_BSIZE)
			   * sizeof(ACLExprEntry_t));
        if ( acl_expr->expr_arry == NULL )
            return(ACLERRNOMEM); 
        acl_expr->expr_arry_size += ACL_TERM_BSIZE;
    }

    expr = &acl_expr->expr_arry[acl_expr->expr_term_index];
    acl_expr->expr_term_index++;
    
    expr->attr_name = PERM_STRDUP(attr_name);
    if ( expr->attr_name == NULL )
        return(ACLERRNOMEM);
    expr->comparator = cmp; 
    expr->attr_pattern = PERM_STRDUP(attr_pattern);
    if ( expr->attr_pattern == NULL )
        return(ACLERRNOMEM);
    expr->true_idx = ACL_TRUE_IDX;
    expr->false_idx = ACL_FALSE_IDX;
    expr->start_flag = 1;
    expr->las_cookie = 0;
    expr->las_eval_func = 0;

    if ( acl_expr->expr_raw_index >= acl_expr->expr_raw_size  ) {
        acl_expr->expr_raw = ( ACLExprRaw_t *) 
            PERM_REALLOC ( acl_expr->expr_raw, 
			   (acl_expr->expr_raw_size + ACL_TERM_BSIZE)
			   * sizeof(ACLExprRaw_t));
        if ( acl_expr->expr_raw == NULL )
            return(ACLERRNOMEM); 
        acl_expr->expr_raw_size += ACL_TERM_BSIZE;
    }
    
    raw_expr = &acl_expr->expr_raw[acl_expr->expr_raw_index];
    acl_expr->expr_raw_index++;

    raw_expr->attr_name = expr->attr_name;
    raw_expr->comparator = cmp;
    raw_expr->attr_pattern = expr->attr_pattern;
    raw_expr->logical = (ACLExprOp_t)0;

#ifdef DEBUG_LEVEL_2
    printf ( "%d: %s %s %s, t=%d, f=%d\n",
            acl_expr->expr_term_index - 1,
            expr->attr_name,
            acl_comp_string( expr->comparator ),
            expr->attr_pattern,
            expr->true_idx,
            expr->false_idx );
#endif

    return(0);
}

/*
 * Negate the previous term or subexpression.
 *
 * Input:
 *    errp		The error stack
 *    acl_expr		The expression to negate
 * Ouput
 *    acl_expr		The negated expression
 * Returns:
 *    0			Success
 *    < 0		Failure
 */

NSAPI_PUBLIC int 
ACL_ExprNot( NSErr_t *errp, ACLExprHandle_t *acl_expr )
{
int		idx;
int		ii;
int		expr_one = 0;
ACLExprRaw_t	*raw_expr;

    if ( acl_expr == NULL )
        return(ACLERRUNDEF);


    if ( acl_expr->expr_raw_index >= acl_expr->expr_raw_size  ) {
        acl_expr->expr_raw = ( ACLExprRaw_t *) 
            PERM_REALLOC ( acl_expr->expr_raw, 
			   (acl_expr->expr_raw_size + ACL_TERM_BSIZE)
			   * sizeof(ACLExprRaw_t));
        if ( acl_expr->expr_raw == NULL )
            return(ACLERRNOMEM); 
        acl_expr->expr_raw_size += ACL_TERM_BSIZE;
    }
    
    raw_expr = &acl_expr->expr_raw[acl_expr->expr_raw_index];
    acl_expr->expr_raw_index++;

    raw_expr->logical = ACL_EXPR_OP_NOT;
    raw_expr->attr_name = NULL;

    /* Find the last expression */
    idx = acl_expr->expr_term_index - 1;
    for ( ii = idx; ii >= 0; ii-- ) {
        if ( acl_expr->expr_arry[ii].start_flag ) {
	    expr_one = ii;
	    break;
	}
    } 

#ifdef DEBUG_LEVEL_2
    printf("not, start index=%d\n", expr_one);
#endif


    /*
     * The intent here is negate the last expression by
     * modifying the true and false links.
     */

    for ( ii = expr_one; ii < acl_expr->expr_term_index; ii++ ) {
        if ( acl_expr->expr_arry[ii].true_idx == ACL_TRUE_IDX ) 
            acl_expr->expr_arry[ii].true_idx = ACL_FALSE_IDX; 
        else if ( acl_expr->expr_arry[ii].true_idx == ACL_FALSE_IDX ) 
            acl_expr->expr_arry[ii].true_idx = ACL_TRUE_IDX; 

        if ( acl_expr->expr_arry[ii].false_idx == ACL_TRUE_IDX ) 
            acl_expr->expr_arry[ii].false_idx = ACL_FALSE_IDX; 
        else if ( acl_expr->expr_arry[ii].false_idx == ACL_FALSE_IDX ) 
            acl_expr->expr_arry[ii].false_idx = ACL_TRUE_IDX; 

    }

    return(0) ;
}

/*
 * Logical 'and' the previous two terms or subexpressions.
 *
 * Input:
 *    errp		The error stack
 *    acl_expr		The terms or subexpressions
 * Output:
 *    acl_expr		The expression after logical 'and'
 */

NSAPI_PUBLIC int 
ACL_ExprAnd( NSErr_t *errp, ACLExprHandle_t *acl_expr )
{
int    		idx;
int		ii;
int		expr_one = ACL_FALSE_IDX;
int		expr_two = ACL_FALSE_IDX;
ACLExprRaw_t 	*raw_expr;

    if ( acl_expr == NULL )
        return(ACLERRUNDEF);

    if ( acl_expr->expr_raw_index >= acl_expr->expr_raw_size  ) {
        acl_expr->expr_raw = ( ACLExprRaw_t *) 
            PERM_REALLOC ( acl_expr->expr_raw, 
			   (acl_expr->expr_raw_size + ACL_TERM_BSIZE)
			   * sizeof(ACLExprRaw_t) );
        if ( acl_expr->expr_raw == NULL )
            return(ACLERRNOMEM); 
        acl_expr->expr_raw_size += ACL_TERM_BSIZE;
    }
    
    raw_expr = &acl_expr->expr_raw[acl_expr->expr_raw_index];
    acl_expr->expr_raw_index++;

    raw_expr->logical = ACL_EXPR_OP_AND;
    raw_expr->attr_name = NULL;

    /* Find the last two expressions */
    idx = acl_expr->expr_term_index - 1;
    for ( ii = idx; ii >= 0; ii-- ) {
        if ( acl_expr->expr_arry[ii].start_flag ) {
	    if ( expr_two == ACL_FALSE_IDX )
                expr_two = ii;
            else if ( expr_one == ACL_FALSE_IDX ) {
                expr_one = ii;
                break;
            }
	}
    } 

#ifdef DEBUG_LEVEL_2
    printf("and, index=%d, first expr=%d, second expr=%d\n", idx, expr_one, expr_two);
#endif

    for ( ii = expr_one; ii < expr_two; ii++) {
        if ( acl_expr->expr_arry[ii].true_idx == ACL_TRUE_IDX )
            acl_expr->expr_arry[ii].true_idx = expr_two;
        if ( acl_expr->expr_arry[ii].false_idx == ACL_TRUE_IDX )
            acl_expr->expr_arry[ii].false_idx = expr_two;
    }

    acl_expr->expr_arry[expr_two].start_flag = 0; 
    return(0);
}

/*
 * Logical 'or' the previous two terms or subexpressions.
 *
 * Input:
 *    errp		The error stack
 *    acl_expr		The terms or subexpressions
 * Output:
 *    acl_expr		The expression after logical 'or'
 */

NSAPI_PUBLIC int 
ACL_ExprOr( NSErr_t *errp, ACLExprHandle_t *acl_expr )
{
int    		idx;
int		ii;
int		expr_one = ACL_FALSE_IDX;
int		expr_two = ACL_FALSE_IDX;
ACLExprRaw_t 	*raw_expr;

    if ( acl_expr == NULL )
        return(ACLERRUNDEF);

    if ( acl_expr->expr_raw_index >= acl_expr->expr_raw_size  ) {
        acl_expr->expr_raw = ( ACLExprRaw_t *) 
            PERM_REALLOC ( acl_expr->expr_raw, 
			   (acl_expr->expr_raw_size + ACL_TERM_BSIZE)
			   * sizeof(ACLExprRaw_t) );
        if ( acl_expr->expr_raw == NULL )
            return(ACLERRNOMEM); 
        acl_expr->expr_raw_size += ACL_TERM_BSIZE;
    }
    
    raw_expr = &acl_expr->expr_raw[acl_expr->expr_raw_index];
    acl_expr->expr_raw_index++;

    raw_expr->logical = ACL_EXPR_OP_OR;
    raw_expr->attr_name = NULL;

    /* Find the last two expressions */
    idx = acl_expr->expr_term_index - 1;
    for ( ii = idx; ii >= 0; ii-- ) {
        if ( acl_expr->expr_arry[ii].start_flag ) {
	    if ( expr_two == ACL_FALSE_IDX )
                expr_two = ii;
            else if ( expr_one == ACL_FALSE_IDX ) {
                expr_one = ii;
                break;
            }
	}
    } 

#ifdef DEBUG_LEVEL_2
    printf("or, index=%d, first expr=%d, second expr=%d\n", idx, expr_one, expr_two);
#endif

    for ( ii = expr_one; ii < expr_two; ii++) {
        if ( acl_expr->expr_arry[ii].true_idx == ACL_FALSE_IDX )
		acl_expr->expr_arry[ii].true_idx = expr_two;
        if ( acl_expr->expr_arry[ii].false_idx == ACL_FALSE_IDX )
		acl_expr->expr_arry[ii].false_idx = expr_two;
    } 
    acl_expr->expr_arry[expr_two].start_flag = 0; 

    return(0);
}

/*
 * INTERNAL FUNCTION (GLOBAL)
 *
 * Write an expression array to standard output.  This
 * is only useful debugging.
 */

int 
ACL_ExprDisplay( ACLExprHandle_t *acl_expr )
{
int    ii;
char   buffer[256];

    if ( acl_expr == NULL )
        return(0);

    for ( ii = 0; ii < acl_expr->expr_term_index; ii++ ) {
        printf ("%d: if ( %s %s %s ) ",
            ii,
            acl_expr->expr_arry[ii].attr_name,
            acl_comp_string( acl_expr->expr_arry[ii].comparator ),
            acl_expr->expr_arry[ii].attr_pattern );

        printf("%s ", acl_index_string(acl_expr->expr_arry[ii].true_idx, buffer));
        printf("else %s\n", 
		acl_index_string(acl_expr->expr_arry[ii].false_idx, buffer) );
    }

    return(0);
}

/*
 * Creates a handle for a new list of ACLs
 *
 * This function creates a new list of ACLs. The list is initially empty
 * and can be added to by ACL_ListAppend().  A resource manager would use
 * these functions to build up a list of all the ACLs applicable to a
 * particular resource access.
 *
 * Input:
 * Returns:
 *    NULL		failure, otherwise returns a new 
 *			ACLListHandle
 */

NSAPI_PUBLIC ACLListHandle_t *
ACL_ListNew(NSErr_t *errp)
{
ACLListHandle_t    *handle;

    handle = ( ACLListHandle_t * ) PERM_CALLOC ( sizeof(ACLListHandle_t) );
    handle->ref_count = 1;
    return(handle);
}

/*
 * Allocates a handle for an ACL wrapper
 *
 * This wrapper is just used for ACL list creation.  It's a way of
 * linking ACLs into a list.  This is an internal function.
 */

static ACLWrapper_t *
acl_wrapper_new(void)
{
ACLWrapper_t    *handle;

    handle = ( ACLWrapper_t * ) PERM_CALLOC ( sizeof(ACLWrapper_t) );
    return(handle);
}

/*
 * Description 
 *
 *      This function destroys an entry a symbol table entry for an
 *      ACL.
 *
 * Arguments:
 *
 *      sym                     - pointer to Symbol_t for an ACL entry
 *      argp                    - unused (must be zero)
 *
 * Returns:
 *
 *      The return value is SYMENUMREMOVE.
 */

static
int acl_hash_entry_destroy(Symbol_t * sym, void * argp)
{
    if (sym != 0) {

        /* Free the acl name string if any */
        if (sym->sym_name != 0) {
            PERM_FREE(sym->sym_name);
        }

        /* Free the Symbol_t structure */
        PERM_FREE(sym);
    }

    /* Indicate that the symbol table entry should be removed */
    return SYMENUMREMOVE;
}


/*
 * LOCAL FUNCTION
 *
 * Create a new symbol with the sym_name equal to the
 * acl->tag value.  Attaches the acl to the sym_data
 * pointer.
 */

static Symbol_t *
acl_sym_new(ACLHandle_t *acl)
{
    Symbol_t *sym;
    /* It's not there, so add it */
    sym = (Symbol_t *) PERM_MALLOC(sizeof(Symbol_t));
    if ( sym == NULL ) 
        return(NULL);

    sym->sym_name = PERM_STRDUP(acl->tag);
    if ( sym->sym_name == NULL ) {
        PERM_FREE(sym);
        return(NULL);
    }

    sym->sym_type = ACLSYMACL;
    sym->sym_data = (void *) acl;
    return(sym);

}

/*
 * LOCAL FUNCTION
 *
 * Add a acl symbol to an acl_list's symbol table.
 *
 * Each acl list has a symbol table.  the symbol table
 * is a quick qay to reference named acl's
 */

static int
acl_sym_add(ACLListHandle_t *acl_list, ACLHandle_t *acl)
{
Symbol_t *sym;
int rv;

    if ( acl->tag == NULL )
        return(ACLERRUNDEF);

    rv = symTableFindSym(acl_list->acl_sym_table,
                         acl->tag,
                         ACLSYMACL,
                         (void **)&sym);
    if ( rv == SYMERRNOSYM ) {
         sym = acl_sym_new(acl);
         if ( sym )
              rv = symTableAddSym(acl_list->acl_sym_table, sym, (void *)sym);
    }

    if ( sym == NULL || rv < 0 )
        return(ACLERRUNDEF);

    return(0);
}

/*
 * LOCAL FUNCTION
 * 
 * Destroy an acl_list's symbol table and all memory referenced
 * by the symbol table.  This does not destroy an acl_list.
 */

static void
acl_symtab_destroy(ACLListHandle_t *acl_list)
{
    /* Destroy each entry in the symbol table */
    symTableEnumerate(acl_list->acl_sym_table, 0, acl_hash_entry_destroy);
    /* Destory the hash table itself */
    symTableDestroy(acl_list->acl_sym_table, 0);
    acl_list->acl_sym_table = NULL;
    return;
}


/*
 * Appends to a specified ACL
 *
 * This function appends a specified ACL to the end of a given ACL list.
 * 
 * Input:
 *    errp		The error stack
 *    flags		should always be zero now
 *    acl_list		target ACL list
 *    acl		new acl
 * Returns:
 *    > 0		The number of acl's in the current list
 *    < 0		failure
 */

NSAPI_PUBLIC int
ACL_ListAppend( NSErr_t *errp, ACLListHandle_t *acl_list, ACLHandle_t *acl,
                int flags )
{
    ACLWrapper_t	*wrapper;
    ACLHandle_t 	*tmp_acl;
    
    if ( acl_list == NULL || acl == NULL )
        return(ACLERRUNDEF);
    
    if ( acl_list->acl_sym_table == NULL && 
         acl_list->acl_count == ACL_TABLE_THRESHOLD ) {

        /*
         * The symbol table isn't really critical so we don't log
         * an error if its creation fails.
         */

        symTableNew(&acl_list->acl_sym_table);
        if ( acl_list->acl_sym_table ) {
            for (wrapper = acl_list->acl_list_head; wrapper; 
                 wrapper = wrapper->wrap_next ) {
                 tmp_acl = wrapper->acl;
                 if ( acl_sym_add(acl_list, tmp_acl) ) {
                    acl_symtab_destroy(acl_list);
                    break;
                 }
            } 
        }
    } 

    wrapper = acl_wrapper_new();
    if ( wrapper == NULL )
        return(ACLERRNOMEM);
    
    wrapper->acl = acl;
    
    if ( acl_list->acl_list_head == NULL ) {
        acl_list->acl_list_head = wrapper;
        acl_list->acl_list_tail = wrapper;
    } else {
        acl_list->acl_list_tail->wrap_next = wrapper;
        acl_list->acl_list_tail = wrapper;
    }
    
    acl->ref_count++;
    
    acl_list->acl_count++;


    if ( acl_list->acl_sym_table ) {
        /*
         * If we fail to insert the ACL then we
         * might as well destroy this hash table since it is
         * useless.
         */
        if ( acl_sym_add(acl_list, acl) ) {
            acl_symtab_destroy(acl_list);
        }
    }
  

    return(acl_list->acl_count);
}

/*
 * Concatenates two ACL lists
 *
 * Attaches all ACLs in acl_list2 to the end of acl_list1.  acl_list2
 * is left unchanged.
 *
 * Input:
 *	errp		pointer to the error stack
 *	acl_list1	target ACL list
 *	acl_list2	source ACL list
 * Output:
 *	acl_list1	list contains the concatenation of acl_list1
 *			and acl_list2.  
 * Returns:
 *	> 0		Number of ACLs in acl_list1 after concat
 *	< 0		failure
 */

NSAPI_PUBLIC int
ACL_ListConcat( NSErr_t *errp, ACLListHandle_t *acl_list1,
                ACLListHandle_t *acl_list2, int flags ) 
{
ACLWrapper_t *wrapper;
int rv;

    if ( acl_list1 == NULL || acl_list2 == NULL )
        return(ACLERRUNDEF);

    for ( wrapper = acl_list2->acl_list_head; 
		wrapper != NULL; wrapper = wrapper->wrap_next ) 
        if ( (rv = ACL_ListAppend ( errp, acl_list1, wrapper->acl, 0 )) < 0 )
            return(rv);

    return(acl_list1->acl_count);
}

/*
 * LOCAL FUNCTION
 *
 * Free up memory associated with and ACLExprEntry.  Probably
 * only useful internally since we aren't exporting 
 * this structure.
 */

static void
ACL_ExprEntryDestroy( ACLExprEntry_t *entry )
{
    LASFlushFunc_t flushp;
    
    if ( entry == NULL )
        return;
    
    if ( entry->las_cookie )
/*	freeLAS(NULL, entry->attr_name, &entry->las_cookie);		*/
    {
	ACL_LasFindFlush( NULL, entry->attr_name, &flushp );
	if ( flushp )
	    ( *flushp )( &entry->las_cookie );
    }

    if ( entry->attr_name )
        PERM_FREE( entry->attr_name );

    if ( entry->attr_pattern )
        PERM_FREE( entry->attr_pattern );

    return;
}

/*
 * LOCAL FUNCTION
 *
 * This function is used to free all the pvalue memory
 * in a plist.
 */

static void 
acl_expr_auth_destroy(char *pname, const void *pvalue, void *user_data)
{
    PERM_FREE((char *) pvalue);
    return;
}

/*
 * Free up memory associated with and ACLExprHandle.  
 *
 * Input:
 *	expr	expression handle to free up
 */

NSAPI_PUBLIC void
ACL_ExprDestroy( ACLExprHandle_t *expr )
{
int    ii;

    if ( expr == NULL )
        return;

    if ( expr->expr_tag )
        PERM_FREE( expr->expr_tag );

    if ( expr->expr_argv ) {
        for ( ii = 0; ii < expr->expr_argc; ii++ )
            if ( expr->expr_argv[ii] )
                PERM_FREE( expr->expr_argv[ii] );
        PERM_FREE( expr->expr_argv );
    }

    for ( ii = 0; ii < expr->expr_term_index; ii++ )
        ACL_ExprEntryDestroy( &expr->expr_arry[ii] );

    if ( expr->expr_auth ) {
	PListEnumerate(expr->expr_auth, acl_expr_auth_destroy, NULL);
        PListDestroy(expr->expr_auth);
    }

    PERM_FREE( expr->expr_arry );
    PERM_FREE( expr->expr_raw );

    PERM_FREE( expr );

    return;
}

/*
 * Free up memory associated with and ACLHandle.  
 *
 * Input:
 *	acl	target acl
 */

NSAPI_PUBLIC void
ACL_AclDestroy(NSErr_t *errp, ACLHandle_t *acl )
{
ACLExprHandle_t    *handle;
ACLExprHandle_t    *tmp;

    if ( acl == NULL )
        return;

    acl->ref_count--;

    if ( acl->ref_count )
        return;

    if ( acl->tag )
        PERM_FREE( acl->tag );

    if ( acl->las_name )
        PERM_FREE( acl->las_name );

    if ( acl->attr_name )
        PERM_FREE( acl->attr_name );

    handle = acl->expr_list_head;
    while ( handle ) {
        tmp = handle;
        handle = handle->expr_next;
        ACL_ExprDestroy( tmp );
    }

    PERM_FREE(acl);

    return;
}

/*
 * Destorys a input ACL List
 *
 * Input:
 *    acl_list		target list
 * Output:
 *    none		target list is freed
 */

NSAPI_PUBLIC void
ACL_ListDestroy(NSErr_t *errp, ACLListHandle_t *acl_list )
{
    ACLWrapper_t    *wrapper;
    ACLWrapper_t    *tmp_wrapper;
    ACLHandle_t     *tmp_acl;


    if ( acl_list == NULL )
        return;

    if ( acl_list->acl_sym_table ) {
        /* Destroy each entry in the symbol table */
        symTableEnumerate(acl_list->acl_sym_table, 0, acl_hash_entry_destroy);
        /* Destory the hash table itself */
        symTableDestroy(acl_list->acl_sym_table, 0);
    }

    ACL_EvalDestroyContext( (ACLListCache_t *)acl_list->cache );

    wrapper = acl_list->acl_list_head;
    
    while ( wrapper ) {
        tmp_acl = wrapper->acl;
        tmp_wrapper = wrapper;
        wrapper = wrapper->wrap_next;
        PERM_FREE( tmp_wrapper );
        ACL_AclDestroy(errp, tmp_acl );
    }

    PERM_FREE( acl_list );

    return;
}

/*
 * FUNCTION:    ACL_ListGetFirst
 *
 * DESCRIPTION:
 *
 *      This function is used to start an enumeration of an
 *      ACLListHandle_t.  It returns an ACLHandle_t* for the first
 *      ACL on the list, and initializes a handle supplied by the
 *      caller, which is used to track the current position in the
 *      enumeration.  This function is normally used in a loop
 *      such as:
 *
 *          ACLListHandle_t *acl_list = <some ACL list>;
 *          ACLHandle_t *cur_acl;
 *          ACLListEnum_t acl_enum;
 *
 *          for (cur_acl = ACL_ListGetFirst(acl_list, &acl_enum);
 *               cur_acl != 0;
 *               cur_acl = ACL_ListGetNext(acl_list, &acl_enum)) {
 *              ...
 *          }
 *
 *      The caller should guarantee that no ACLs are added or removed
 *      from the ACL list during the enumeration.
 *
 * ARGUMENTS:
 *
 *      acl_list                - handle for the ACL list
 *      acl_enum                - pointer to uninitialized enumeration handle
 *
 * RETURNS:
 *
 *      As described above.  If the acl_list argument is null, or the
 *      referenced ACL list is empty, the return value is null.
 */

NSAPI_PUBLIC ACLHandle_t *
ACL_ListGetFirst(ACLListHandle_t *acl_list, ACLListEnum_t *acl_enum)
{
    ACLWrapper_t *wrapper;
    ACLHandle_t *acl = 0;

    *acl_enum = 0;

    if (acl_list) {

        wrapper = acl_list->acl_list_head;
        *acl_enum = (ACLListEnum_t)wrapper;

        if (wrapper) {
            acl = wrapper->acl;
        }
    }

    return acl;
}

NSAPI_PUBLIC ACLHandle_t *
ACL_ListGetNext(ACLListHandle_t *acl_list, ACLListEnum_t *acl_enum)
{
    ACLWrapper_t *wrapper = (ACLWrapper_t *)(*acl_enum);
    ACLHandle_t *acl = 0;

    if (wrapper) {

        wrapper = wrapper->wrap_next;
        *acl_enum = (ACLListEnum_t)wrapper;

        if (wrapper) acl = wrapper->acl;
    }

    return acl;
}

/*
 * FUNCTION:    ACL_AclGetTag
 *
 * DESCRIPTION:
 *
 *      Returns the tag string associated with an ACL.
 *
 * ARGUMENTS:
 *
 *      acl                     - handle for an ACL
 *
 * RETURNS:
 *
 *      The return value is a pointer to the ACL tag string.
 */

NSAPI_PUBLIC const char *
ACL_AclGetTag(ACLHandle_t *acl)
{
    return (acl) ? (const char *)(acl->tag) : 0;
}

/*
 * Finds a named ACL in an input list.
 *
 * Input:
 *	acl_list	a list of ACLs to search
 *	acl_name	the name of the ACL to find
 * 	flags		e.g. ACL_CASE_INSENSITIVE
 * Returns:
 *	NULL		No ACL found
 *	acl		A pointer to an ACL with named acl_name
 */

NSAPI_PUBLIC ACLHandle_t *
ACL_ListFind (NSErr_t *errp, ACLListHandle_t *acl_list, char *acl_name, int flags )
{
ACLHandle_t *result = NULL;
ACLWrapper_t *wrapper;
Symbol_t *sym;

    if ( acl_list == NULL || acl_name == NULL )
        return( result );

    /*
     * right now the symbol table exists if there hasn't been
     * any collisions based on using case insensitive names.
     * if there are any collisions then the table will be
     * deleted and we will look up using list search.
     *
     * we should probably create two hash tables, one for case
     * sensitive lookups and the other for insensitive.
     */ 
    if ( acl_list->acl_sym_table ) {
        if ( symTableFindSym(acl_list->acl_sym_table, 
             acl_name, ACLSYMACL, (void **) &sym) >= 0 ) {
             result = (ACLHandle_t *) sym->sym_data;
             if ( result && (flags & ACL_CASE_SENSITIVE) &&
                  strcmp(result->tag, acl_name) ) {
                 result = NULL; /* case doesn't match */
             }
        }
        return( result );
    }

    if ( flags & ACL_CASE_INSENSITIVE ) {
        for ( wrapper = acl_list->acl_list_head; wrapper != NULL; 
              wrapper = wrapper->wrap_next ) {
            if ( wrapper->acl->tag &&
                 strcasecmp( wrapper->acl->tag, acl_name ) == 0 ) {
                result = wrapper->acl;
                break;
            }
        }
    } else {
        for ( wrapper = acl_list->acl_list_head; wrapper != NULL; 
              wrapper = wrapper->wrap_next ) {
            if ( wrapper->acl->tag &&
                 strcmp( wrapper->acl->tag, acl_name ) == 0 ) {
                result = wrapper->acl;
                break;
            }
        }
    }

    return( result );
}


/*  
 * Function parses an input ACL string and returns an
 * ACLListHandle_t pointer that represents the entire
 * file without the comments.
 * 
 * Input:
 *	buffer		the target ACL buffer 
 *	errp		a pointer to an error stack
 *
 * Returns:
 * 	NULL 		parse failed
 *  
 */

NSAPI_PUBLIC ACLListHandle_t *
ACL_ParseString( NSErr_t *errp, char *buffer )
{
ACLListHandle_t 	*handle = NULL;
int			eid = 0;
int			rv = 0;
const char			*errmsg;

    ACL_InitAttr2Index();

    if ( acl_parse_crit == NULL )
        acl_parse_crit = crit_init();

    crit_enter( acl_parse_crit );

    if ( acl_InitScanner( errp, NULL, buffer ) < 0 ) {
        rv = ACLERRNOMEM;
        eid = ACLERR1920;
        nserrGenerate(errp, rv, eid, ACL_Program, 0);
    } else {
    
        handle = ACL_ListNew(errp);
        if ( handle == NULL ) {
            rv = ACLERRNOMEM;
            eid = ACLERR1920;
            nserrGenerate(errp, rv, eid, ACL_Program, 0);
        } else if ( acl_PushListHandle( handle ) < 0 ) {
            rv = ACLERRNOMEM;
            eid = ACLERR1920;
            nserrGenerate(errp, rv, eid, ACL_Program, 0);
        } else if ( acl_Parse() ) {
            rv = ACLERRPARSE;
            eid = ACLERR1780;
        }
    
        if ( acl_EndScanner() < 0 ) {
            rv = ACLERROPEN;
            eid = ACLERR1500;
            errmsg = system_errmsg();
            nserrGenerate(errp, rv, eid, ACL_Program, 2, "buffer", errmsg);
        }

    }

    if ( rv || eid ) {
        ACL_ListDestroy(errp, handle);
        handle = NULL;
    }

    crit_exit( acl_parse_crit );
    return(handle);

}


/*
 * Delete a named ACL from an ACL list
 *
 * Input:
 *	acl_list	Target ACL list handle
 *	acl_name	Name of the target ACL
 * Returns:
 *    0			success
 *    < 0		failure
 */

NSAPI_PUBLIC int
ACL_ListAclDelete(NSErr_t *errp, ACLListHandle_t *acl_list, char *acl_name, int flags ) 
{
ACLHandle_t *acl = NULL;
ACLWrapper_t *wrapper;
ACLWrapper_t *wrapper_prev = NULL;
Symbol_t *sym;

    if ( acl_list == NULL || acl_name == NULL )
        return(ACLERRUNDEF);

    if ( flags & ACL_CASE_INSENSITIVE ) {
        for ( wrapper = acl_list->acl_list_head; wrapper != NULL; 
              wrapper = wrapper->wrap_next ) {
            if ( wrapper->acl->tag &&
                 strcasecmp( wrapper->acl->tag, acl_name ) == 0 ) {
                acl = wrapper->acl;
                break;
            }
            wrapper_prev = wrapper;
        }
    } else {
        for ( wrapper = acl_list->acl_list_head; wrapper != NULL; 
              wrapper = wrapper->wrap_next ) {
            if ( wrapper->acl->tag &&
                 strcmp( wrapper->acl->tag, acl_name ) == 0 ) {
                acl = wrapper->acl;
                break;
            }
            wrapper_prev = wrapper;
        }
    }

    if ( acl ) {

        if ( wrapper_prev ) {
	    wrapper_prev->wrap_next = wrapper->wrap_next;
        } else {
	    acl_list->acl_list_head = wrapper->wrap_next;
        }

        if ( acl_list->acl_list_tail == wrapper ) {
            acl_list->acl_list_tail = wrapper_prev;
        }

        acl = wrapper->acl;
        acl_list->acl_count--;
        PERM_FREE(wrapper);

        if ( acl_list->acl_sym_table ) {
            if ( symTableFindSym(acl_list->acl_sym_table, 
                              acl->tag, ACLSYMACL, (void **) &sym) < 0 ) {

            /* not found, this is an error of some sort */

            } else {
                symTableRemoveSym(acl_list->acl_sym_table, sym);
                acl_hash_entry_destroy(sym, 0);
            }
        }

        ACL_AclDestroy(errp, acl);
        return(0);
    }

    return(ACLERRUNDEF);
}


/*
 * Destroy a NameList 
 *
 * Input:
 *	name_list	a dynamically allocated array of strings	
 * Returns:
 *    0			success
 *    < 0		failure
 */

NSAPI_PUBLIC int
ACL_NameListDestroy(NSErr_t *errp, char **name_list)
{
    int			list_index;

    if ( name_list == NULL )
        return(ACLERRUNDEF);

    for ( list_index = 0; name_list[list_index]; list_index++ ) {
        PERM_FREE(name_list[list_index]);
    }
    PERM_FREE(name_list);
    return(0);
}


/*
 * Gets a name list of consisting of all ACL names for input list. 
 *
 * Input:
 *	acl_list	an ACL List handle	
 *	name_list	pointer to a list of string pointers	
 * Returns:
 *    0			success
 *    < 0		failure
 */
NSAPI_PUBLIC int
ACL_ListGetNameList(NSErr_t *errp, ACLListHandle_t *acl_list, char ***name_list)
{
    const int block_size = 50;
    ACLWrapper_t 	*wrapper;
    int			list_index;
    int			list_size;
    char		**tmp_list;
    char		**local_list;
    const char		*name;
    

    if ( acl_list == NULL )
        return(ACLERRUNDEF);

    list_size = block_size;
    local_list = (char **) PERM_MALLOC(sizeof(char *) * list_size);
    if ( local_list == NULL ) 
        return(ACLERRNOMEM); 
    list_index = 0;
    local_list[list_index] = NULL; 

    for ( wrapper = acl_list->acl_list_head; wrapper != NULL; 
                        wrapper = wrapper->wrap_next ) {
        if ( wrapper->acl->tag ) 
            name = wrapper->acl->tag;
        else 
            name = "noname";
        if ( list_index + 2 > list_size ) {
            list_size += block_size;
            tmp_list = (char **) PERM_REALLOC(local_list, 
                                              sizeof(char *) * list_size);
            if ( tmp_list == NULL ) {
                ACL_NameListDestroy(errp, local_list);
                return(ACLERRNOMEM); 
            }
            local_list = tmp_list;
        } 
        local_list[list_index] = PERM_STRDUP(name);
        if ( local_list[list_index] == NULL ) {
            ACL_NameListDestroy(errp, local_list);
            return(ACLERRNOMEM); 
        }
        list_index++;
        local_list[list_index] = NULL; 
    }    
    *name_list = local_list;
    return(0);
}

/*
 * Changes method to method plus DBTYPE, and registers
 * databases.
 *
 * Input:
 *	errp		error stack	
 *	acl_list	Target ACL list handle
 * Returns:
 *    0			success
 *    < 0		failure
 */

NSAPI_PUBLIC int
ACL_ListPostParseForAuth(NSErr_t *errp, ACLListHandle_t *acl_list ) 
{
    ACLHandle_t *acl;
    ACLWrapper_t *wrap;
    ACLExprHandle_t *expr;
    char *method;
    char *database;
    int rv;
    ACLDbType_t *dbtype;
    ACLMethod_t *methodtype;

    if ( acl_list == NULL )
        return(0);

    for ( wrap = acl_list->acl_list_head; wrap; wrap = wrap->wrap_next ) {

        acl = wrap->acl;
        if ( acl == NULL )
            continue;

        for ( expr = acl->expr_list_head; expr; expr = expr->expr_next ) {

            if ( expr->expr_type != ACL_EXPR_TYPE_AUTH || 
                 expr->expr_auth == NULL) 
                continue;

            rv = PListGetValue(expr->expr_auth, ACL_ATTR_METHOD_INDEX, 
                                (void **) &method, NULL);
            if ( rv >= 0 ) {
		methodtype = (ACLMethod_t *)PERM_MALLOC(sizeof(ACLMethod_t));
		rv = ACL_MethodFind(errp, method, methodtype);
		if (rv) {
		    nserrGenerate(errp, ACLERRUNDEF, ACLERR3800, ACL_Program,
				  3, acl->tag, "method", method);
		    PERM_FREE(methodtype);
		    return(ACLERRUNDEF);
		}

	        rv = PListSetValue(expr->expr_auth, ACL_ATTR_METHOD_INDEX, 
				      methodtype, NULL);
		if ( rv < 0 ) {
		    nserrGenerate(errp, ACLERRNOMEM, ACLERR3810, ACL_Program,
				  0);
		    return(ACLERRNOMEM);
		}
		PERM_FREE(method);
	    }
    
            rv = PListGetValue(expr->expr_auth, ACL_ATTR_DATABASE_INDEX, 
				(void **) &database, NULL);

	    if (rv < 0) continue;

	    /* The following function lets user use databases which are
	     * not registered by their administrators.  This also fixes
	     * the backward compatibility.
	     */
	    dbtype = (ACLDbType_t *)PERM_MALLOC(sizeof(ACLDbType_t));
	    rv = ACL_RegisterDbFromACL(errp, (const char *) database,
				       dbtype);

	    if (rv < 0) {
		    nserrGenerate(errp, ACLERRUNDEF, ACLERR3800, ACL_Program,
				  3, acl->tag, "database", database);
		PERM_FREE(dbtype);
		return(ACLERRUNDEF);
	    }
    
	    rv = PListInitProp(expr->expr_auth, ACL_ATTR_DBTYPE_INDEX, ACL_ATTR_DBTYPE, 
			       dbtype, NULL);
	    if ( rv < 0 ) {
		nserrGenerate(errp, ACLERRNOMEM, ACLERR3810, ACL_Program,
			      0);
		return(ACLERRNOMEM);
	    }

        }

    }

    return(0);

}


/*
 * The following routines are used to validate input parameters.  They always
 * return 1, or cause an PR_ASSERT failure.  The proper way to use them is 
 * with an PR_ASSERT in the calling function.  E.g.
 *	PR_ASSERT(ACL_AssertAcllist(acllist));
 */

int
ACL_AssertAcllist(ACLListHandle_t *acllist)
{
    ACLWrapper_t *wrap;

    if (acllist == ACL_LIST_NO_ACLS) return 1;
    PR_ASSERT(acllist);
    PR_ASSERT(acllist->acl_list_head);
    PR_ASSERT(acllist->acl_list_tail);
    PR_ASSERT(acllist->acl_count);
    PR_ASSERT(acllist->ref_count > 0);

    for (wrap=acllist->acl_list_head; wrap; wrap=wrap->wrap_next) {
	PR_ASSERT(ACL_AssertAcl(wrap->acl));
    }

    /* Artificially limit ACL lists to 10 ACLs for now */
    PR_ASSERT(acllist->acl_count < 10);

    return 1;
}

int
ACL_AssertAcl(ACLHandle_t *acl)
{
    PR_ASSERT(acl);
    PR_ASSERT(acl->ref_count);
    PR_ASSERT(acl->expr_count);
    PR_ASSERT(acl->expr_list_head);
    PR_ASSERT(acl->expr_list_tail);

    return 1;
}

static PList_t ACLAttr2IndexPList = NULL;

int
ACL_InitAttr2Index(void)
{
    intptr_t i;

    if (ACLAttr2IndexPList) return 0;

    ACLAttr2IndexPList = PListNew(NULL);
    for (i = 1; i < ACL_ATTR_INDEX_MAX; i++) {
        PListInitProp(ACLAttr2IndexPList, 0, ACLAttrTable[i], (const void *)i, NULL);
    }
 
    return 0;
}

/*
 *	Attempt to locate the index number for one of the known attribute names
 *	that are stored in plists.  If we can't match it, just return 0.
 */
int
ACL_Attr2Index(const char *attrname)
{
    int index = 0;

    if ( ACLAttr2IndexPList ) {
        PListFindValue(ACLAttr2IndexPList, attrname, (void **)&index, NULL);
        if (index < 0) index = 0;
    }
    return index;
}

void
ACL_Attr2IndexListDestroy(void)
{
	PListDestroy(ACLAttr2IndexPList);
	if(acl_parse_crit)
		crit_terminate(acl_parse_crit);
	acl_free_buffer();
}
