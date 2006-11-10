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

/* avl.c - routines to implement an avl tree */
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

#if 0
static char copyright[] = "@(#) Copyright (c) 1993 Regents of the University of Michigan.\nAll rights reserved.\n";
static char avl_version[] = "AVL library version 1.0\n";
#endif

#ifdef _WIN32
typedef char *caddr_t;
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include "avl.h"

#define ROTATERIGHT(x)	{ \
	Avlnode *tmp;\
	if ( *x == NULL || (*x)->avl_left == NULL ) {\
		(void) printf("RR error\n"); exit(1); \
	}\
	tmp = (*x)->avl_left;\
	(*x)->avl_left = tmp->avl_right;\
	tmp->avl_right = *x;\
	*x = tmp;\
}
#define ROTATELEFT(x)	{ \
	Avlnode *tmp;\
	if ( *x == NULL || (*x)->avl_right == NULL ) {\
		(void) printf("RL error\n"); exit(1); \
	}\
	tmp = (*x)->avl_right;\
	(*x)->avl_right = tmp->avl_left;\
	tmp->avl_left = *x;\
	*x = tmp;\
}

/*
 * ravl_insert - called from avl_insert() to do a recursive insert into
 * and balance of an avl tree.
 */

static int
ravl_insert(
    Avlnode 	**iroot,
    caddr_t	data,
    int		*taller,
    IFP		fcmp,		/* comparison function */
    IFP		fdup,		/* function to call for duplicates */
    int		depth
)
{
	int	rc, cmp, tallersub;
	Avlnode	*l, *r;

	if ( *iroot == 0 ) {
		if ( (*iroot = (Avlnode *) malloc( sizeof( Avlnode ) ))
		    == NULL ) {
			return( -1 );
		}
		(*iroot)->avl_left = 0;
		(*iroot)->avl_right = 0;
		(*iroot)->avl_bf = 0;
		(*iroot)->avl_data = data;
		*taller = 1;
		return( 0 );
	}

	cmp = (*fcmp)( data, (*iroot)->avl_data );

	/* equal - duplicate name */
	if ( cmp == 0 ) {
		*taller = 0;
		return( (*fdup)( (*iroot)->avl_data, data ) );
	}

	/* go right */
	else if ( cmp > 0 ) {
		rc = ravl_insert( &((*iroot)->avl_right), data, &tallersub,
		   fcmp, fdup, depth );
		if ( tallersub )
			switch ( (*iroot)->avl_bf ) {
			case LH	: /* left high - balance is restored */
				(*iroot)->avl_bf = EH;
				*taller = 0;
				break;
			case EH	: /* equal height - now right heavy */
				(*iroot)->avl_bf = RH;
				*taller = 1;
				break;
			case RH	: /* right heavy to start - right balance */
				r = (*iroot)->avl_right;
				switch ( r->avl_bf ) {
				case LH	: /* double rotation left */
					l = r->avl_left;
					switch ( l->avl_bf ) {
					case LH	: (*iroot)->avl_bf = EH;
						  r->avl_bf = RH;
						  break;
					case EH	: (*iroot)->avl_bf = EH;
						  r->avl_bf = EH;
						  break;
					case RH	: (*iroot)->avl_bf = LH;
						  r->avl_bf = EH;
						  break;
					}
					l->avl_bf = EH;
					ROTATERIGHT( (&r) )
					(*iroot)->avl_right = r;
					ROTATELEFT( iroot )
					*taller = 0;
					break;
				case EH	: /* This should never happen */
					break;
				case RH	: /* single rotation left */
					(*iroot)->avl_bf = EH;
					r->avl_bf = EH;
					ROTATELEFT( iroot )
					*taller = 0;
					break;
				}
				break;
			}
		else
			*taller = 0;
	}

	/* go left */
	else {
		rc = ravl_insert( &((*iroot)->avl_left), data, &tallersub,
		   fcmp, fdup, depth );
		if ( tallersub )
			switch ( (*iroot)->avl_bf ) {
			case LH	: /* left high to start - left balance */
				l = (*iroot)->avl_left;
				switch ( l->avl_bf ) {
				case LH	: /* single rotation right */
					(*iroot)->avl_bf = EH;
					l->avl_bf = EH;
					ROTATERIGHT( iroot )
					*taller = 0;
					break;
				case EH	: /* this should never happen */
					break;
				case RH	: /* double rotation right */
					r = l->avl_right;
					switch ( r->avl_bf ) {
					case LH	: (*iroot)->avl_bf = RH;
						  l->avl_bf = EH;
						  break;
					case EH	: (*iroot)->avl_bf = EH;
						  l->avl_bf = EH;
						  break;
					case RH	: (*iroot)->avl_bf = EH;
						  l->avl_bf = LH;
						  break;
					}
					r->avl_bf = EH;
					ROTATELEFT( (&l) )
					(*iroot)->avl_left = l;
					ROTATERIGHT( iroot )
					*taller = 0;
					break;
				}
				break;
			case EH	: /* equal height - now left heavy */
				(*iroot)->avl_bf = LH;
				*taller = 1;
				break;
			case RH	: /* right high - balance is restored */
				(*iroot)->avl_bf = EH;
				*taller = 0;
				break;
			}
		else
			*taller = 0;
	}

	return( rc );
}

/*
 * avl_insert -- insert a node containing data data into the avl tree
 * with root root.  fcmp is a function to call to compare the data portion
 * of two nodes.  it should take two arguments and return <, >, or == 0,
 * depending on whether its first argument is <, >, or == its second
 * argument (like strcmp, e.g.).  fdup is a function to call when a duplicate
 * node is inserted.  it should return 0, or -1 and its return value
 * will be the return value from avl_insert in the case of a duplicate node.
 * the function will be called with the original node's data as its first
 * argument and with the incoming duplicate node's data as its second
 * argument.  this could be used, for example, to keep a count with each
 * node.
 *
 * NOTE: this routine may malloc memory
 */

int
avl_insert(
    Avlnode	**root,
    caddr_t	data,
    IFP		fcmp,
    IFP		fdup
)
{
	int	taller;

	return( ravl_insert( root, data, &taller, fcmp, fdup, 0 ) );
}

/* 
 * right_balance() - called from delete when root's right subtree has
 * been shortened because of a deletion.
 */

static int
right_balance( Avlnode **root )
{
	int	shorter= 0;
	Avlnode	*r, *l;

	switch( (*root)->avl_bf ) {
	case RH:	/* was right high - equal now */
		(*root)->avl_bf = EH;
		shorter = 1;
		break;
	case EH:	/* was equal - left high now */
		(*root)->avl_bf = LH;
		shorter = 0;
		break;
	case LH:	/* was right high - balance */
		l = (*root)->avl_left;
		switch ( l->avl_bf ) {
		case RH	: /* double rotation left */
			r = l->avl_right;
			switch ( r->avl_bf ) {
			case RH	:
				(*root)->avl_bf = EH;
				l->avl_bf = LH;
				break;
			case EH	:
				(*root)->avl_bf = EH;
				l->avl_bf = EH;
				break;
			case LH	:
				(*root)->avl_bf = RH;
				l->avl_bf = EH;
				break;
			}
			r->avl_bf = EH;
			ROTATELEFT( (&l) )
			(*root)->avl_left = l;
			ROTATERIGHT( root )
			shorter = 1;
			break;
		case EH	: /* right rotation */
			(*root)->avl_bf = LH;
			l->avl_bf = RH;
			ROTATERIGHT( root );
			shorter = 0;
			break;
		case LH	: /* single rotation right */
			(*root)->avl_bf = EH;
			l->avl_bf = EH;
			ROTATERIGHT( root )
			shorter = 1;
			break;
		}
		break;
	}

	return( shorter );
}

/* 
 * left_balance() - called from delete when root's left subtree has
 * been shortened because of a deletion.
 */

static int
left_balance( Avlnode **root )
{
	int	shorter= 0;
	Avlnode	*r, *l;

	switch( (*root)->avl_bf ) {
	case LH:	/* was left high - equal now */
		(*root)->avl_bf = EH;
		shorter = 1;
		break;
	case EH:	/* was equal - right high now */
		(*root)->avl_bf = RH;
		shorter = 0;
		break;
	case RH:	/* was right high - balance */
		r = (*root)->avl_right;
		switch ( r->avl_bf ) {
		case LH	: /* double rotation left */
			l = r->avl_left;
			switch ( l->avl_bf ) {
			case LH	:
				(*root)->avl_bf = EH;
				r->avl_bf = RH;
				break;
			case EH	:
				(*root)->avl_bf = EH;
				r->avl_bf = EH;
				break;
			case RH	:
				(*root)->avl_bf = LH;
				r->avl_bf = EH;
				break;
			}
			l->avl_bf = EH;
			ROTATERIGHT( (&r) )
			(*root)->avl_right = r;
			ROTATELEFT( root )
			shorter = 1;
			break;
		case EH	: /* single rotation left */
			(*root)->avl_bf = RH;
			r->avl_bf = LH;
			ROTATELEFT( root );
			shorter = 0;
			break;
		case RH	: /* single rotation left */
			(*root)->avl_bf = EH;
			r->avl_bf = EH;
			ROTATELEFT( root )
			shorter = 1;
			break;
		}
		break;
	}

	return( shorter );
}

/*
 * ravl_delete() - called from avl_delete to do recursive deletion of a
 * node from an avl tree.  It finds the node recursively, deletes it,
 * and returns shorter if the tree is shorter after the deletion and
 * rebalancing.
 */

static caddr_t
ravl_delete(
    Avlnode	**root,
    caddr_t	data,
    IFP		fcmp,
    int		*shorter
)
{
	int	shortersubtree = 0;
	int	cmp;
	caddr_t	savedata;
	Avlnode	*minnode, *savenode;

	if ( *root == NULLAVL )
		return( 0 );

	cmp = (*fcmp)( data, (*root)->avl_data );

	/* found it! */
	if ( cmp == 0 ) {
		savenode = *root;
		savedata = savenode->avl_data;

		/* simple cases: no left child */
		if ( (*root)->avl_left == 0 ) {
			*root = (*root)->avl_right;
			*shorter = 1;
			free( (char *) savenode );
			return( savedata );
		/* no right child */
		} else if ( (*root)->avl_right == 0 ) {
			*root = (*root)->avl_left;
			*shorter = 1;
			free( (char *) savenode );
			return( savedata );
		}

		/* 
		 * avl_getmin will return to us the smallest node greater
		 * than the one we are trying to delete.  deleting this node
		 * from the right subtree is guaranteed to end in one of the
		 * simple cases above.
		 */

		minnode = (*root)->avl_right;
		while ( minnode->avl_left != NULLAVL )
			minnode = minnode->avl_left;

		/* swap the data */
		(*root)->avl_data = minnode->avl_data;
		minnode->avl_data = savedata;

		savedata = ravl_delete( &(*root)->avl_right, data, fcmp,
		    &shortersubtree );

		if ( shortersubtree )
			*shorter = right_balance( root );
		else
			*shorter = 0;
	/* go left */
	} else if ( cmp < 0 ) {
		if ( (savedata = ravl_delete( &(*root)->avl_left, data, fcmp,
		    &shortersubtree )) == 0 ) {
			*shorter = 0;
			return( 0 );
		}

		/* left subtree shorter? */
		if ( shortersubtree )
			*shorter = left_balance( root );
		else
			*shorter = 0;
	/* go right */
	} else {
		if ( (savedata = ravl_delete( &(*root)->avl_right, data, fcmp,
		    &shortersubtree )) == 0 ) {
			*shorter = 0;
			return( 0 );
		}

		if ( shortersubtree ) 
			*shorter = right_balance( root );
		else
			*shorter = 0;
	}

	return( savedata );
}

/*
 * avl_delete() - deletes the node containing data (according to fcmp) from
 * the avl tree rooted at root.
 */

caddr_t
avl_delete( Avlnode **root, caddr_t data, IFP fcmp )
{
	int	shorter;

	return( ravl_delete( root, data, fcmp, &shorter ) );
}

static int
avl_inapply( Avlnode *root, IFP	fn, caddr_t arg, int stopflag )
{
	if ( root == 0 )
		return( AVL_NOMORE );

	if ( root->avl_left != 0 )
		if ( avl_inapply( root->avl_left, fn, arg, stopflag ) 
		    == stopflag )
			return( stopflag );

	if ( (*fn)( root->avl_data, arg ) == stopflag )
		return( stopflag );

	if ( root->avl_right == 0 )
		return( AVL_NOMORE );
	else
		return( avl_inapply( root->avl_right, fn, arg, stopflag ) );
}

static int
avl_postapply( Avlnode *root, IFP fn, caddr_t arg, int stopflag )
{
	if ( root == 0 )
		return( AVL_NOMORE );

	if ( root->avl_left != 0 )
		if ( avl_postapply( root->avl_left, fn, arg, stopflag ) 
		    == stopflag )
			return( stopflag );

	if ( root->avl_right != 0 )
		if ( avl_postapply( root->avl_right, fn, arg, stopflag ) 
		    == stopflag )
			return( stopflag );

	return( (*fn)( root->avl_data, arg ) );
}

static int
avl_preapply( Avlnode *root, IFP fn, caddr_t arg, int stopflag )
{
	if ( root == 0 )
		return( AVL_NOMORE );

	if ( (*fn)( root->avl_data, arg ) == stopflag )
		return( stopflag );

	if ( root->avl_left != 0 )
		if ( avl_preapply( root->avl_left, fn, arg, stopflag ) 
		    == stopflag )
			return( stopflag );

	if ( root->avl_right == 0 )
		return( AVL_NOMORE );
	else
		return( avl_preapply( root->avl_right, fn, arg, stopflag ) );
}

/*
 * avl_apply -- avl tree root is traversed, function fn is called with
 * arguments arg and the data portion of each node.  if fn returns stopflag,
 * the traversal is cut short, otherwise it continues.  Do not use -6 as
 * a stopflag, as this is what is used to indicate the traversal ran out
 * of nodes.
 */

int
avl_apply(
    Avlnode	*root,
    IFP		fn,
    caddr_t	arg,
    int		stopflag,
    int		type
)
{
	switch ( type ) {
	case AVL_INORDER:
		return( avl_inapply( root, fn, arg, stopflag ) );
	case AVL_PREORDER:
		return( avl_preapply( root, fn, arg, stopflag ) );
	case AVL_POSTORDER:
		return( avl_postapply( root, fn, arg, stopflag ) );
	default:
		fprintf( stderr, "Invalid traversal type %d\n", type );
		return( -1 );
	}

	/* NOTREACHED */
}

/*
 * avl_prefixapply - traverse avl tree root, applying function fprefix
 * to any nodes that match.  fcmp is called with data as its first arg
 * and the current node's data as its second arg.  it should return
 * 0 if they match, < 0 if data is less, and > 0 if data is greater.
 * the idea is to efficiently find all nodes that are prefixes of
 * some key...  Like avl_apply, this routine also takes a stopflag
 * and will return prematurely if fmatch returns this value.  Otherwise,
 * AVL_NOMORE is returned.
 */

int
avl_prefixapply(
    Avlnode	*root,
    caddr_t	data,
    IFP		fmatch,
    caddr_t	marg,
    IFP		fcmp,
    caddr_t	carg,
    int		stopflag
)
{
	int	cmp;

	if ( root == 0 )
		return( AVL_NOMORE );

	cmp = (*fcmp)( data, root->avl_data, carg );
	if ( cmp == 0 ) {
		if ( (*fmatch)( root->avl_data, marg ) == stopflag )
			return( stopflag );

		if ( root->avl_left != 0 )
			if ( avl_prefixapply( root->avl_left, data, fmatch,
			    marg, fcmp, carg, stopflag ) == stopflag )
				return( stopflag );

		if ( root->avl_right != 0 )
			return( avl_prefixapply( root->avl_right, data, fmatch,
			    marg, fcmp, carg, stopflag ) );
		else
			return( AVL_NOMORE );

	} else if ( cmp < 0 ) {
		if ( root->avl_left != 0 )
			return( avl_prefixapply( root->avl_left, data, fmatch,
			    marg, fcmp, carg, stopflag ) );
	} else {
		if ( root->avl_right != 0 )
			return( avl_prefixapply( root->avl_right, data, fmatch,
			    marg, fcmp, carg, stopflag ) );
	}

	return( AVL_NOMORE );
}

/*
 * avl_free -- traverse avltree root, freeing the memory it is using.
 * the dfree() is called to free the data portion of each node.  The
 * number of items actually freed is returned.
 */

int
avl_free( Avlnode *root, IFP dfree )
{
	int	nleft, nright;

	if ( root == 0 )
		return( 0 );

	nleft = nright = 0;
	if ( root->avl_left != 0 )
		nleft = avl_free( root->avl_left, dfree );

	if ( root->avl_right != 0 )
		nright = avl_free( root->avl_right, dfree );

	if ( dfree )
		(*dfree)( root->avl_data );

	free( (char *)root );

	return( nleft + nright + 1 );
}

/*
 * avl_find -- search avltree root for a node with data data.  the function
 * cmp is used to compare things.  it is called with data as its first arg 
 * and the current node data as its second.  it should return 0 if they match,
 * < 0 if arg1 is less than arg2 and > 0 if arg1 is greater than arg2.
 */

caddr_t
avl_find( Avlnode *root, caddr_t data, IFP fcmp )
{
	int	cmp;

	while ( root != 0 && (cmp = (*fcmp)( data, root->avl_data )) != 0 ) {
		if ( cmp < 0 )
			root = root->avl_left;
		else
			root = root->avl_right;
	}

	return( root ? root->avl_data : 0 );
}

/*
 * avl_find_lin -- search avltree root linearly for a node with data data. 
 * the function cmp is used to compare things.  it is called with data as its
 * first arg and the current node data as its second.  it should return 0 if
 * they match, non-zero otherwise.
 */

caddr_t
avl_find_lin( Avlnode *root, caddr_t data, IFP fcmp )
{
	caddr_t	res;

	if ( root == 0 )
		return( NULL );

	if ( (*fcmp)( data, root->avl_data ) == 0 )
		return( root->avl_data );

	if ( root->avl_left != 0 )
		if ( (res = avl_find_lin( root->avl_left, data, fcmp ))
		    != NULL )
			return( res );

	if ( root->avl_right == 0 )
		return( NULL );
	else
		return( avl_find_lin( root->avl_right, data, fcmp ) );
}

static caddr_t	*avl_list = (caddr_t *)0;
static int	avl_maxlist = 0;
static int	avl_nextlist = 0;

#define AVL_GRABSIZE	100

/* ARGSUSED */
static int
avl_buildlist( caddr_t data, int arg )
{
	static int	slots = 0;

	if ( avl_list == (caddr_t *) 0 ) {
		avl_list = (caddr_t *) malloc(AVL_GRABSIZE * sizeof(caddr_t));
		slots = AVL_GRABSIZE;
		avl_maxlist = 0;
	} else if ( avl_maxlist == slots ) {
		slots += AVL_GRABSIZE;
		avl_list = (caddr_t *) realloc( (char *) avl_list,
		    (unsigned) slots * sizeof(caddr_t));
	}

	avl_list[ avl_maxlist++ ] = data;

	return( 0 );
}

/*
 * avl_getfirst() and avl_getnext() are provided as alternate tree
 * traversal methods, to be used when a single function cannot be
 * provided to be called with every node in the tree.  avl_getfirst()
 * traverses the tree and builds a linear list of all the nodes,
 * returning the first node.  avl_getnext() returns the next thing
 * on the list built by avl_getfirst().  This means that avl_getfirst()
 * can take a while, and that the tree should not be messed with while
 * being traversed in this way, and that multiple traversals (even of
 * different trees) cannot be active at once.
 */

caddr_t
avl_getfirst( Avlnode *root )
{
	if ( avl_list ) {
		free( (char *) avl_list);
		avl_list = (caddr_t *) 0;
	}
	avl_maxlist = 0;
	avl_nextlist = 0;

	if ( root == 0 )
		return( 0 );

	(void) avl_apply( root, avl_buildlist, (caddr_t) 0, -1, AVL_INORDER );

	return( avl_list[ avl_nextlist++ ] );
}

caddr_t
avl_getnext()
{
	if ( avl_list == 0 )
		return( 0 );

	if ( avl_nextlist == avl_maxlist ) {
		free( (caddr_t) avl_list);
		avl_list = (caddr_t *) 0;
		return( 0 );
	}

	return( avl_list[ avl_nextlist++ ] );
}

int avl_dup_error()
{
	return( -1 );
}

int avl_dup_ok()
{
	return( 0 );
}
