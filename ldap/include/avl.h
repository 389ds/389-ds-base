/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

typedef struct avlnode
{
    caddr_t avl_data;
    signed char avl_bf;
    struct avlnode *avl_left;
    struct avlnode *avl_right;
} Avlnode;

#if defined(__GNUC__) && (((__GNUC__ == 4) && (__GNUC_MINOR__ >= 4)) || (__GNUC__ > 4))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif

#ifndef _IFP
#define _IFP
typedef int (*IFP)(); /* takes undefined arguments */
#endif

#if defined(__GNUC__) && (((__GNUC__ == 4) && (__GNUC_MINOR__ >= 4)) || (__GNUC__ > 4))
#pragma GCC diagnostic pop
#endif

#define NULLAVL ((Avlnode *)NULL)

/* balance factor values */
#define LH -1
#define EH 0
#define RH 1

/* avl routines */
#define avl_getone(x) (x == 0 ? 0 : (x)->avl_data)
#define avl_onenode(x) (x == 0 || ((x)->avl_left == 0 && (x)->avl_right == 0))
extern int avl_insert(Avlnode **root, void *data, IFP fcmp, IFP fdup);
extern caddr_t avl_delete(Avlnode **root, void *data, IFP fcmp);
extern caddr_t avl_find(Avlnode *root, void *data, int32_t (*fcmp)(caddr_t, caddr_t));
extern caddr_t avl_getfirst(Avlnode *root);
extern caddr_t avl_getnext(void);
extern int avl_dup_error(void);
extern int avl_apply(Avlnode *root, IFP fn, void *arg, int stopflag, int type);
extern int avl_free(Avlnode *root, int32_t (*dfree)(caddr_t));

/* apply traversal types */
#define AVL_PREORDER 1
#define AVL_INORDER 2
#define AVL_POSTORDER 3
/* what apply returns if it ran out of nodes */
#define AVL_NOMORE -6

caddr_t avl_find_lin(Avlnode *root, caddr_t data, int32_t (*fcmp)(caddr_t, caddr_t));

#endif /* _AVL */
