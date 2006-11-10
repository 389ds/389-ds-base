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
 * ACL private data structure definitions 
 */

#ifndef ACL_PARSER_HEADER
#define ACL_PARSER_HEADER

#include <netsite.h>
#include <plhash.h>
#include <base/pool.h>
#include <base/plist.h>
#include <libaccess/las.h>


#define ACL_TERM_BSIZE		4
#define ACL_FALSE_IDX   	-2
#define ACL_TRUE_IDX    	-1
#define ACL_MIN_IDX		0
#define ACL_EXPR_STACK		1024
#define ACL_TABLE_THRESHOLD	10

typedef enum    {
                ACL_EXPR_OP_AND,
                ACL_EXPR_OP_OR,
                ACL_EXPR_OP_NOT
                } ACLExprOp_t;


typedef struct ACLExprEntry {
	char			*attr_name;	/* LAS name input */
	CmpOp_t			comparator;	/* LAS comparator input */
	char			*attr_pattern;	/* LAS attribute input */
	int			false_idx; 	/* index, -1 true, -2 false */
	int			true_idx;	/* index, -1 true, -2 false */
	int			start_flag;	/* marks start of an expr */
	void			*las_cookie; 	/* private data store for LAS */
	LASEvalFunc_t		las_eval_func; 	/* LAS function */
} ACLExprEntry_t;

typedef struct ACLExprRaw {
	char			*attr_name;	/* expr lval */
	CmpOp_t			comparator;	/* comparator */
	char			*attr_pattern;	/* expr rval */
	ACLExprOp_t		logical;	/* logical operator */
} ACLExprRaw_t;

typedef struct ACLExprStack {
	char	 		*expr_text[ACL_EXPR_STACK];
	ACLExprRaw_t 		*expr[ACL_EXPR_STACK];
	int			stack_index;
	int			found_subexpression;
	int			last_subexpression;
} ACLExprStack_t;

typedef struct ACLExprHandle {
	char			*expr_tag;
	char			*acl_tag;
	int			expr_number;
	ACLExprType_t		expr_type;
	int			expr_flags;
	int			expr_argc;
	char			**expr_argv; 
	PList_t			expr_auth;
	ACLExprEntry_t 		*expr_arry;
	int			expr_arry_size;
	int			expr_term_index;
	ACLExprRaw_t 		*expr_raw;
	int			expr_raw_index;
	int			expr_raw_size;
	struct ACLExprHandle 	*expr_next;	/* Null-terminated */
} ACLExprHandle_t;

typedef struct ACLHandle {
	int			ref_count;
	char			*tag;
	PFlags_t		flags;
	char			*las_name;
	pblock			*pb;
	char			**attr_name;
        int			expr_count;
	ACLExprHandle_t		*expr_list_head;	/* Null-terminated */
	ACLExprHandle_t		*expr_list_tail;
} ACLHandle_t;


typedef struct ACLWrapper {
	ACLHandle_t		*acl;
	struct ACLWrapper 	*wrap_next;
} ACLWrapper_t;

#define ACL_LIST_STALE	0x1
#define	ACL_LIST_IS_STALE(x)	((x)->flags & ACL_LIST_STALE)

typedef struct ACLListHandle {
	ACLWrapper_t		*acl_list_head;	/* Null-terminated */
	ACLWrapper_t		*acl_list_tail;	/* Null-terminated */
	int			acl_count;
        void			*acl_sym_table;
	void			*cache;	
	uint32			flags;
	int			ref_count;
} ACLListHandle_t;

typedef	struct	ACLAceNumEntry {
	int			acenum;
	struct ACLAceNumEntry	*next;
	struct ACLAceNumEntry	*chain;		/* only used for freeing memory */
} ACLAceNumEntry_t;

typedef struct ACLAceEntry {
	ACLExprHandle_t		*acep;
				/* Array of auth block ptrs for all the expr 
				   clauses in this ACE */
	PList_t			*autharray;	
				/* PList with auth blocks for ALL attributes */
	PList_t			global_auth;    
	struct ACLAceEntry	*next;		/* Null-terminated list	*/
} ACLAceEntry_t;

typedef struct PropList PropList_t;

typedef struct ACLEvalHandle {
	pool_handle_t		*pool;
	ACLListHandle_t		*acllist;
	PList_t			subject;
	PList_t			resource;
	int                     default_result;
} ACLEvalHandle_t;


typedef	struct ACLListCache {
/* Hash table for all access rights used in all acls in this list.  Each
 * hash entry has a list of ACE numbers that relate to this referenced
 * access right.  
 */
	PLHashTable		*Table;			
	char			*deny_response;
	char			*deny_type;
	ACLAceEntry_t		*acelist;	/* Evaluation order 
                                                 * list of all ACEs 
						 */
	ACLAceNumEntry_t	*chain_head;	/* Chain of all Ace num 
                                                 * entries for this
                                                 * ACL list so we can free them 
                                                 */
	ACLAceNumEntry_t	*chain_tail;
} ACLListCache_t;

/* this is to speed up acl_to_str_append */
typedef struct acl_string_s {
	char * str;
	long str_size;
	long str_len;
} acl_string_t;



NSPR_BEGIN_EXTERN_C
extern int ACL_ExprDisplay( ACLExprHandle_t *acl_expr );
extern int ACL_AssertAcl( ACLHandle_t *acl );
extern int ACL_EvalDestroyContext ( ACLListCache_t *cache );
extern time_t *acl_get_req_time(PList_t resource);
NSPR_END_EXTERN_C

#endif
