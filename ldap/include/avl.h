/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* avl.h - avl tree definitions */
/*
 * Copyright (c) 1993 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */


#ifndef _AVL
#define _AVL

/*
 * this structure represents a generic avl tree node.
 */

typedef struct avlnode {
	caddr_t		avl_data;
	signed char	avl_bf;
	struct avlnode	*avl_left;
	struct avlnode	*avl_right;
} Avlnode;

#define NULLAVL	((Avlnode *) NULL)

/* balance factor values */
#define LH 	-1
#define EH 	0
#define RH 	1

/* avl routines */
#define avl_getone(x)	(x == 0 ? 0 : (x)->avl_data)
#define avl_onenode(x)	(x == 0 || ((x)->avl_left == 0 && (x)->avl_right == 0))
extern int		avl_insert();
extern caddr_t		avl_delete();
extern caddr_t		avl_find();
extern caddr_t		avl_getfirst();
extern caddr_t		avl_getnext();
extern int		avl_dup_error();
extern int		avl_apply();
extern int		avl_free();

/* apply traversal types */
#define AVL_PREORDER	1
#define AVL_INORDER	2
#define AVL_POSTORDER	3
/* what apply returns if it ran out of nodes */
#define AVL_NOMORE	-6

#ifndef _IFP
#define _IFP
typedef int	(*IFP)();
#endif

caddr_t avl_find_lin( Avlnode *root, caddr_t data, IFP fcmp );

#endif /* _AVL */
