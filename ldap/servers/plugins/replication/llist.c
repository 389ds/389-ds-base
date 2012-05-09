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

/* llist.c - single link list implementation */

#include <string.h>
#include "slapi-plugin.h"
#include "slapi-private.h"
#include "llist.h"
#include "repl_shared.h"

/* data structures */

/* link list node */
typedef struct lnode
{
	char *key;
	void *data;
	struct lnode *next;
} LNode;

/* This structure defines a one-way linked list with head and tail pointers. 
   The list contains a "dummy" head node which makes sure that every node
   has a previous node. This allows to remove a node during iteration without
   breaking the list */
struct llist
{
	LNode *head;
	LNode *tail;
}; 

/* forward declarations */
static LNode* _llistNewNode (const char *key, void *data);
static void _llistDestroyNode (LNode **node, FNFree fnFree);

LList* llistNew ()
{
	LList *list = (LList*) slapi_ch_calloc (1, sizeof (LList));

	/* allocate a special head node - it contains no data but just
	   fulfills the requirement that every node has a previous one.
	   This is used during iteration with removal */
	if (list)
	{
		list->head = (LNode*)slapi_ch_calloc (1, sizeof (LNode));
		if (list->head == NULL)
		{
			slapi_ch_free ((void**)&list);
		}
	}

	return list;
}

void llistDestroy (LList **list, FNFree fnFree)
{
	LNode *node = NULL, *prev_node;

	if (list == NULL || *list == NULL)
		return;

	if ((*list)->head)
		node = (*list)->head->next;

	while (node)
	{
		prev_node = node;
		node = node->next;
		_llistDestroyNode (&prev_node, fnFree);
	}

	slapi_ch_free ((void**)&((*list)->head));
	slapi_ch_free ((void**)list);
}

void*  llistGetFirst(LList *list, void **iterator)
{
	if (list == NULL || iterator == NULL || list->head == NULL || list->head->next == NULL)
	{
		/* empty list or error */
		return NULL;
	}

    /* Iterator points to the previous element (so that we can remove current element
       and still keep the list in tact. In case of the first element, iterator points 
       to the dummy head element */ 
	(*iterator) = list->head;
	return list->head->next->data;
}

void* llistGetNext (LList *list, void **iterator)
{
	LNode *node;

	if (list == NULL || list->head == NULL || iterator == NULL || *iterator == NULL)
	{
		/* end of the list or error */
		return NULL;
	}

	/* Iterator points to the previous element (so that we can
	   remove current element and still keep list in tact. */
	node = *(LNode **)iterator;
	node = node->next;	

	(*iterator) = node;

	if (node && node->next)		
		return node->next->data;	
	else
		return NULL;
}

void* llistRemoveCurrentAndGetNext (LList *list, void **iterator)
{
	LNode *prevNode, *node;
	
	/* end of the list is reached or error occured */
	if (list == NULL || iterator == NULL || *iterator == NULL)
		return NULL;

	/* Iterator points to the previous element (so that we can
	   remove current element and still keep list in tact. */
	prevNode = *(LNode **)iterator;	
	node = prevNode->next;
	if (node)
	{
		prevNode->next = node->next;	
		_llistDestroyNode (&node, NULL);
		node = prevNode->next;
		if (node) {
			return node->data;
		} else {
			if (list->head->next == NULL) {
				list->tail = NULL;
			}
			return NULL;
		}
	}
	else
		return NULL;
}

void* llistGetHead (LList *list)
{
	if (list == NULL || list->head == NULL || list->head->next == NULL)
	{
		/* empty list or error */
		return NULL;
	}

	return list->head->next->data;
}

void* llistGetTail (LList *list)
{
	if (list == NULL || list->tail == NULL)
	{
		/* empty list or error */
		return NULL;
	}

	return list->tail->data;
}

void* llistGet (LList *list, const char* key)
{
	LNode *node;

	/* empty list or invalid input */
	if (list == NULL || list->head == NULL || list->head->next == NULL || key == NULL)
		return NULL;

	node = list->head->next;
	while (node)
	{
		if (node->key && strcmp (key, node->key) == 0)
		{				
			return node->data;
		}

		node = node->next;
	}

	/* node with specified key is not found */
	return NULL;	
}

int	llistInsertHead (LList *list, const char *key, void *data)
{
	LNode *node;
	if (list == NULL || list->head == NULL || data == NULL)
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name, "llistInsertHead: invalid argument\n");
		return -1;	
	}

	node = _llistNewNode (key, data);
	if (node == NULL)
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name, "llistInsertHead: failed to allocate list node\n");
		return -1;
	}

	if (list->head->next == NULL) /* empty list */
	{
		list->head->next = node;
		list->tail = node;
	}
	else 
	{
		node->next = list->head->next;
		list->head->next = node;
	}

	return 0;
}

int llistInsertTail (LList *list, const char *key, void *data)
{
	LNode *node;
	if (list == NULL || list->head == NULL || data == NULL)
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name, "llistInsertHead: invalid argument\n");
		return -1;	
	}

	node = _llistNewNode (key, data);
	if (node == NULL)
	{
		slapi_log_error(SLAPI_LOG_PLUGIN, repl_plugin_name, "llistInsertHead: failed to allocate list node\n");
		return -1;
	}

	if (list->head->next == NULL) /* empty list */
	{
		list->head->next = node;
		list->tail = node;
	}
	else 
	{
		list->tail->next = node;
		list->tail = node;
	}

	return 0;
}

void* llistRemoveHead (LList *list)
{
	LNode *node;
	void *data;

	if (list == NULL || list->head == NULL || list->head->next == NULL)
		return NULL;

	node = list->head->next;
	data = node->data;
	
	list->head->next = node->next;

	/* last element removed */
	if (list->head->next == NULL)
		list->tail = NULL;

	_llistDestroyNode (&node, NULL);

	return data;
}

void* llistRemove (LList *list, const char *key)
{
	LNode *node, *prev_node;
	void *data;

	if (list == NULL || list->head == NULL || list->head->next == NULL || key == NULL)
		return NULL;

	node = list->head->next;
	prev_node = list->head;
	while (node)
	{
		if (node->key && strcmp (key, node->key) == 0)
		{			
			prev_node->next = node->next;
			/* last element removed */
			if (node->next == NULL)
            {
                /* no more elements in the list */
                if (list->head->next == NULL)
                {
                    list->tail = NULL;
                }
                else
			    {
				    list->tail = prev_node;
			    }
            }

			data = node->data;
			_llistDestroyNode (&node, NULL);				
			return data;
		}

		prev_node = node;
		node = node->next;
	}

	/* node with specified key is not found */
	return NULL;
}

static LNode* _llistNewNode (const char *key, void *data)
{
	LNode *node = (LNode*) slapi_ch_malloc (sizeof (LNode));
	if (node == NULL)
		return NULL;

	if (key)
		node->key = slapi_ch_strdup (key);
	else
		node->key = NULL;

	node->data = data;
	node->next = NULL;

	return node;
}

static void _llistDestroyNode (LNode **node, FNFree fnFree)
{
	if ((*node)->data && fnFree)
		fnFree (&(*node)->data);
	if ((*node)->key)
		slapi_ch_free ((void**)&((*node)->key));

	slapi_ch_free ((void**)node);
}
