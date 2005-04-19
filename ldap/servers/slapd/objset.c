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

#include "slapi-plugin.h"
#include "slapi-private.h"

/*
 * The "wrapper" placed around objects in our object set.
 */
typedef struct objset_object
{
	Object *obj;	/* pointer to actual object */
	struct objset_object *next; /* pointer to next object in list */
} objset_object;

/*
 * The object set itself.
 */
typedef struct objset
{
	objset_object *head; /* pointer to linked list of objects */
	objset_object *tail; /* pointer to tail of linked list */
	PRLock *lock; /* Lock - protects addition/deletion from list */
	FNFree destructor; /* Destructor callback for objset itself */
} objset;
	
/* Forward declarations */
static void unlinkObjsetObjectNoLock(Objset *o, objset_object *obj_to_unlink);


/*
 * Create a new, empty object set.
 * Returns a pointer to the new object set, or NULL if an error occurred.
 */
Objset *
objset_new(FNFree objset_destructor)
{
	objset *set;

	set = (objset *)slapi_ch_malloc(sizeof(objset));
	set->lock = PR_NewLock();
	if (NULL == set->lock)
	{
		slapi_ch_free((void **)&set);
	}
	else
	{
		set->head = set->tail = NULL;
		set->destructor = objset_destructor;
	}
	return set;
}


/*
 * Delete an object set. All references to contained objects
 * are released, and the objset is deleted.
 */
void
objset_delete(Objset **setp)
{
	objset_object *o, *o_next;
	Objset *set;

	PR_ASSERT(NULL != setp);
	set = *setp;
	PR_ASSERT(NULL != set);
	PR_Lock(set->lock);
	o = set->head;
	while (NULL != o)
	{
		o_next = o->next;
		object_release(o->obj); /* release our reference */
		slapi_ch_free((void **)&o); /* Free wrapper */
		o = o_next;
	}
	PR_Unlock(set->lock);
	PR_DestroyLock(set->lock);
	if (NULL != set->destructor)
	{
		set->destructor((void **)setp);
	}
	slapi_ch_free((void **)setp);
}
		


/*
 * Add a new object to an object set.
 * Return values:
 * OBJSET_SUCCESS: the insertion was succesful.
 * OBJSET_ALREADY_EXISTS: the object already exists in the set.
 */
int
objset_add_obj(Objset *set, Object *object)
{
	objset_object *p;
	int exists = 0;
	int rc = OBJSET_SUCCESS;

	PR_ASSERT(NULL != set);
	PR_ASSERT(NULL != object);

	PR_Lock(set->lock);
	/* Make sure this object isn't already in the set */
	p = set->head;
	while (NULL != p)
	{
		if (p->obj == object)
		{
			exists = 1;
			break;
		}
		p = p->next;
	}
	if (exists)
	{
		rc = OBJSET_ALREADY_EXISTS;
	}
	else
	{
		objset_object *new_node = (objset_object *)slapi_ch_malloc(sizeof(objset_object));
		object_acquire(object); /* Record our reference */
		new_node->obj = object;
		new_node->next = NULL;

		if (NULL == set->head)
		{
			set->head = set->tail = new_node;
		}
		else
		{
			set->tail->next = new_node;
			set->tail = new_node;
		}
	}
	PR_Unlock(set->lock);
	return rc;
}


/*
 * Locate an object in an object set.
 *
 * Arguments:
 * set: the object set to search
 * compare_fn: a caller-provided function used to compare the
 *             name against an object. This function should return
 *             a negtive value, zero, or a positive value if the
 *             object's name is, respectively, less that, equal
 *             to, or greater than the provided name.
 * name: the name (value) to find.
 * 
 * The returned object, if any, is referenced. The caller must
 * call object_release() when finished with the object.
 *
 * Returns the object, if found. Otherwise, returns NULL.
 * Implementation note: since we store objects in an unordered
 * linked list, all that's really important is that the compare_fn
 * return 0 when a match is found, and non-zero otherwise.
 * Other types of implementations might try to optimize the
 * storage, e.g. binary search.
 */
Object *
objset_find(Objset *set, CMPFn compare_fn, const void *name)
{
	objset_object *found = NULL;

	PR_ASSERT(NULL != set);
	PR_ASSERT(NULL != name);
	PR_ASSERT(NULL != compare_fn);

	PR_Lock(set->lock);
	found = set->head;
	while (NULL != found)
	{
		if (compare_fn(found->obj, name) == 0)
		{
			break;
		}
		found = found->next;
	}
	if (NULL != found)
	{
		/* acquire object */
		object_acquire(found->obj);
	}
	PR_Unlock(set->lock);
	return found == NULL ? NULL : found->obj;
}




/*
 * Remove an object from an objset.
 * Returns OBJSET_SUCCESS if the object was found and removed, or
 * OBJSET_NO_SUCH_OBJECT if the object was not found in the list.
 */
int
objset_remove_obj(Objset *set, Object *object) 
{
	int rc = OBJSET_SUCCESS;
	objset_object *found;

	PR_ASSERT(NULL != set);
	PR_ASSERT(NULL != object);

	PR_Lock(set->lock);
	found = set->head;
	while (NULL != found)
	{
		if (found->obj == object)
		{
			break;
		}
		found = found->next;
	}
	if (NULL == found)
	{
		rc = OBJSET_NO_SUCH_OBJECT;
	}
	else
	{
		Object *saved = found->obj;

		/* Unlink from list */
		unlinkObjsetObjectNoLock(set, found);

		/* Release reference on object */
		object_release(saved);
	}
	PR_Unlock(set->lock);
	return rc;
}



/*
 * Prepare for iteration across an object set. Returns the first
 * object in the set. The returned object is referenced, therefore
 * the caller must either release the object, or
 * pass it to an objset_next_obj call, which will
 * implicitly release the object.
 * Returns the first object, or NULL if the objset contains no
 * objects.
 */
Object *
objset_first_obj(Objset *set)
{
	Object *return_object;

        /* Be tolerant (for the replication plugin) */
        if (set == NULL) return NULL;

	PR_Lock(set->lock);
	if (NULL == set->head)
	{
		return_object = NULL;
	}
	else
	{
		object_acquire(set->head->obj);
		return_object = set->head->obj;
	}
	PR_Unlock(set->lock);
	return return_object;
}


/*
 * Return the next object in an object set, or NULL if there are
 * no more objects.
 * The returned object is referenced, therefore
 * the caller must either release the object, or
 * pass it to an objset_next_ob call, which will
 * implicitly release the object.
 */
Object *
objset_next_obj(Objset *set, Object *previous)
{
	Object *return_object = NULL;
	objset_object *p;

	PR_ASSERT(NULL != set);
	PR_Lock(set->lock);

	/* First, find the current object */
	p = set->head;
	while (NULL != p && p->obj != previous)
	{
		p = p->next;
	}
	/* Find the next object */
	if (NULL != p && NULL != p->next)
	{
		return_object = p->next->obj;
		object_acquire(return_object);
	}
	PR_Unlock(set->lock);
	object_release(previous); /* Release the previous object */
	return return_object;
}



/*
 * Return a non-zero value if the object set is empty, or
 * zero if the object set is empty.
 */
int
objset_is_empty(Objset *set)
{
	int return_value;

	PR_ASSERT(NULL != set);

	PR_Lock(set->lock);
	return_value = (set->head == NULL);
	PR_Unlock(set->lock);
	return return_value;
}



/*
 * Return the count of objects in the object set.
 */
int objset_size(Objset *set)
{
    int count = 0;
    objset_object *p;

    PR_ASSERT(NULL != set);
    PR_Lock(set->lock);
    for (p = set->head; p; p = p->next)
	count++;
    PR_Unlock(set->lock);
    return count;
}



/*
 * Utility function: remove object from list.
 * This needs to be called with the objset locked.
 */
static void
unlinkObjsetObjectNoLock(Objset *o, objset_object *obj_to_unlink)
{

	objset_object *p = o->head;

	PR_ASSERT(NULL != o->head);
	/* Unlink from list */
	if (o->head == obj_to_unlink) {
		/* Object to unlink was at head of list */
		p = o->head->next;
		o->head = obj_to_unlink->next;
	} else {
		while (NULL != p->next && p->next != obj_to_unlink) {
			p = p->next;
		}
		if (NULL != p->next)
		{
			/* p points to object prior to one being removed */
			p->next = p->next->next;
		}
	}
	if (o->tail == obj_to_unlink)
	{
		o->tail = p;
	}
		
	/* Free the wrapper */
	slapi_ch_free((void **)&obj_to_unlink);
}
