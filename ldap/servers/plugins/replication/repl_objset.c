/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* repl_objset.c */
/*
 *  Support for lifetime management of sets of objects.
 *  Objects are refcounted. NOTE: this API is deprecated.
 *  Use the object/objset API provided by libslapd.
 */

#include "slapi-plugin.h"
#include "slapi-private.h"
#include "repl_objset.h"
#include <prlock.h>

#define REPL_OBJSET_OBJ_FLAG_DELETED	0x1


typedef struct repl_objset_object
{
	void *data; /* pointer to actual node data */
	char *key; /* key for this object. null-terminated string */
	int refcnt; /* reference count for this object */
	unsigned long flags; /* state of this object */
} Repl_Objset_object;

typedef struct repl_objset
{
	LList *objects;
	FNFree destructor; /* destructor for objects - provided by caller */
	PRLock *lock;
} repl_objset;


/* Forward declarations */
static void removeObjectNolock(Repl_Objset *o, Repl_Objset_object *co);
static Repl_Objset_object *removeCurrentObjectAndGetNextNolock (Repl_Objset *o, 
	Repl_Objset_object *co, void *iterator);

/*
 * Create a new set.
 *
 * Arguments:
 *  destructor: a function to be called when an object is to be destroyed
 *
 * Returns:
 *  A pointer to the object set, or NULL if an error occured.
 */
Repl_Objset *
repl_objset_new(FNFree destructor)
{
	Repl_Objset *p;

	p = (Repl_Objset *)slapi_ch_malloc(sizeof(Repl_Objset));
	p->lock = PR_NewLock();
	if (NULL == p->lock)
	{
		free(p); p = NULL;
	}
	p->objects = llistNew();
	p->destructor = destructor;
	return p;
}


/*
 * Destroy a Repl_Objset.
 * Arguments:
 *  o: the object set to be destroyed
 *  maxwait: the maximum time to wait for all object refcnts to 
 *           go to zero.
 *  panic_fn: a function to  be called if, after waiting "maxwait"
 *            seconds, not all object refcnts are zero.
 * The caller must ensure that no one else holds references to the
 * set or any objects it contains.
 */
void
repl_objset_destroy(Repl_Objset **o, time_t maxwait, FNFree panic_fn)
{
	Repl_Objset_object *co = NULL;
	time_t now, stop_time;
	int really_gone;
	int loopcount;
	void *cookie;

	PR_ASSERT(NULL != o);
	PR_ASSERT(NULL != *o);

	time(&now);
	stop_time = now + maxwait;

	/*
	 * Loop over the objects until they all are actually gone,
	 * or until maxwait seconds have passed.
	 */
	really_gone = 0;
	loopcount = 0;

	while (now < stop_time)
	{
		void *cookie;

		PR_Lock((*o)->lock);

		if ((co = llistGetFirst((*o)->objects, &cookie)) == NULL)
		{
			really_gone = 1;
			PR_Unlock((*o)->lock);
			break;
		}
		while (NULL != co)
		{
			/* Set the deleted flag so the object isn't returned by iterator */
			co->flags |= REPL_OBJSET_OBJ_FLAG_DELETED;
			if (0 == co->refcnt)
			{
				/* Remove the object */
				co = removeCurrentObjectAndGetNextNolock ((*o), co, cookie);
					
			}
			else
				co = llistGetNext((*o)->objects, &cookie);
		}
		PR_Unlock((*o)->lock);
		time(&now);
		if (loopcount > 0)
		{
			DS_Sleep(PR_TicksPerSecond());
		}
		loopcount++;
	}

	if (!really_gone)
	{
		if (NULL != panic_fn)
		{
			/*
			 * Call the "aargh, this thing won't go away" panic
			 * function for each remaining object.
			 */
			PR_Lock((*o)->lock);
			if ((co = llistGetFirst((*o)->objects, &cookie)) == NULL)
			{
				panic_fn(co->data);
				while (NULL != co)
				{
					panic_fn(co->data);
					co = llistGetNext((*o)->objects, &cookie);
				}
			}
			PR_Unlock((*o)->lock);
		}
	}
	else
	{
		/* Free the linked list */
		llistDestroy(&(*o)->objects, (*o)->destructor);
		PR_DestroyLock((*o)->lock);
		free(*o); *o = NULL;
	}
}



/*
 * Add an object to an object set.
 *
 * Arguments:
 *  o: The object set to which the object is to be added.
 *  name: a null-terminated string that names the object. Must
 *  be unique.
 *  obj: pointer to the object to be added.
 * 
 * Return codes:
 *  REPL_OBJSET_SUCCESS: the item was added to the object set
 *  REPL_OBJSET_DUPLICATE_KEY: an item with the same key is already
 *  in the object set.
 *  REPL_OBJSET_INTERNAL_ERROR: something bad happened.
 */
int
repl_objset_add(Repl_Objset *o, const char *name, void *obj)
{
	Repl_Objset_object *co = NULL;
	Repl_Objset_object *tmp = NULL;
	int rc = REPL_OBJSET_SUCCESS;

	PR_ASSERT(NULL != o);
	PR_ASSERT(NULL != name);
	PR_ASSERT(NULL != obj);

	PR_Lock(o->lock);
	tmp = llistGet(o->objects, name);
	if (NULL != tmp)
	{
		rc = REPL_OBJSET_DUPLICATE_KEY;
		goto loser;
	}
	co = (Repl_Objset_object *)slapi_ch_malloc(sizeof(Repl_Objset_object));
	co->data = obj;
	co->key = slapi_ch_strdup(name);
	co->refcnt = 0;
	co->flags = 0UL;
	if (llistInsertHead(o->objects, name, co) != 0)
	{
		rc = REPL_OBJSET_INTERNAL_ERROR;
		goto loser;
	}
	PR_Unlock(o->lock);
	return rc;

loser:
	PR_Unlock(o->lock);
	if (NULL != co)
	{
		if (NULL != co->key)
		{
			slapi_ch_free((void **)&co->key);
		}
		slapi_ch_free((void **)&co);
	}
	return rc;
}


/* Must be called with the repl_objset locked */
static void
removeObjectNolock(Repl_Objset *o, Repl_Objset_object *co)
{
	/* Remove from list */
	llistRemove(o->objects, co->key);
	/* Destroy the object */
	o->destructor(&(co->data));
	free(co->key);
	/* Deallocate the container */
	free(co);
}

static Repl_Objset_object *
removeCurrentObjectAndGetNextNolock (Repl_Objset *o, Repl_Objset_object *co, void *iterator)
{
	Repl_Objset_object *ro;

	PR_ASSERT (o);
	PR_ASSERT (co);
	PR_ASSERT (iterator);

	ro = llistRemoveCurrentAndGetNext (o->objects, &iterator);

	o->destructor(&(co->data));
	free(co->key);
	/* Deallocate the container */
	free(co);

	return ro;	
}

/* Must be called with the repl_objset locked */
static void
acquireNoLock(Repl_Objset_object *co)
{
	co->refcnt++;
}


/* Must be called with the repl_objset locked */
static void
releaseNoLock(Repl_Objset *o, Repl_Objset_object *co)
{
	PR_ASSERT(co->refcnt >= 1);
	if (--co->refcnt == 0)
	{
		if (co->flags & REPL_OBJSET_OBJ_FLAG_DELETED)
		{
			/* Remove the object */
			removeObjectNolock(o, co);
		}
	}
}

/*
 * Retrieve an object from the object set. If an object with
 * the given key is found, its reference count is incremented,
 * a pointer to the object is returned, and a handle to use
 * to refer to the object is returned.
 *
 * Arguments:
 *  o: The object set to be searched.
 *  key: key of the object to be retrieved
 *  obj: pointer to void * that will be set to point to the
 *       object, if found.
 *  handle: pointer to void * that will be set to point to a
 *       handle, used to refer to the object, if found.
 *
 * Returns:
 *  REPL_OBJSET_SUCCESS: an item was found.
 *  REPL_OBJSET_KEY_NOT_FOUND: no item with the given key was found.
 */
int
repl_objset_acquire(Repl_Objset *o, const char *key, void **obj, void **handle)
{
	Repl_Objset_object *co = NULL;
	int rc = REPL_OBJSET_KEY_NOT_FOUND;

	PR_ASSERT(NULL != o);
	PR_ASSERT(NULL != key);
	PR_ASSERT(NULL != obj);
	PR_ASSERT(NULL != handle);

	PR_Lock(o->lock);
	co = llistGet(o->objects, key);
	if (NULL != co && !(co->flags & REPL_OBJSET_OBJ_FLAG_DELETED))
	{
		acquireNoLock(co);
		*obj = (void *)co->data;
		*handle = (void *)co;
		rc = REPL_OBJSET_SUCCESS;
	}
	PR_Unlock(o->lock);
	return rc;
}


/*
 * Return an object to the object set.
 *
 * Arguments:
 *  o: The object set containing the objct
 *  handle: reference to the object.
 *
 */
void
repl_objset_release(Repl_Objset *o, void *handle)
{
	Repl_Objset_object *co;

	PR_ASSERT(NULL != o);
	PR_ASSERT(NULL != handle);

	co = (Repl_Objset_object *)handle;
	PR_Lock(o->lock);
	releaseNoLock(o, co);
	PR_Unlock(o->lock);
}



/*
 * Delete an object from the object set
 *
 * Arguments:
 *  o: The object set containing the object.
 *  handle: reference to the object.
 */
void
repl_objset_delete(Repl_Objset *o, void *handle)
{
	Repl_Objset_object *co = (Repl_Objset_object *)handle;

	PR_ASSERT(NULL != o);
	PR_ASSERT(NULL != co);

	PR_Lock(o->lock);
	if (co->refcnt == 0)
	{
		removeObjectNolock(o, co);
	}
	else
	{
		/* Set deleted flag, clean up later */
		co->flags |= REPL_OBJSET_OBJ_FLAG_DELETED;
	}
	PR_Unlock(o->lock);
}


typedef struct _iterator
{
	Repl_Objset *o;			/* set for which iterator was created */
	void *cookie;			/* for linked list */
	Repl_Objset_object	*co;	/* our wrapper */
} iterator;

/*
 * Get the first object in an object set.
 * Used when enumerating all of the objects in a set.
 * Arguments:
 *  o: The object set being enumerated
 *  itcontext: an iteration context, to be passed back to subsequent calls
 *          to repl_objset_next_object.
 *  handle: a pointer to pointer to void. This will be filled in with
 *          a reference to the object's enclosing object.
 * Returns:
 *  A pointer to the next object in the set, or NULL if there are no
 *  objects in the set.
 * 
 */
void *
repl_objset_first_object(Repl_Objset *o, void **itcontext, void **handle)
{
	Repl_Objset_object *co = NULL;
	void *cookie;
	void *retptr = NULL;
	iterator *it;

	PR_ASSERT(NULL != o);
	PR_ASSERT(NULL != itcontext);

	*itcontext = NULL;

	if (NULL == o->objects) {
		return(NULL);
	}

	/* Find the first non-deleted object */
	PR_Lock(o->lock);
	co = llistGetFirst(o->objects, &cookie);
	while (NULL != co && (co->flags & REPL_OBJSET_OBJ_FLAG_DELETED))
	{
		co = llistGetNext(o->objects, &cookie);
	}

	if (NULL != co)
	{
		/* Increment refcnt until item given back to us */
		acquireNoLock(co);

		/* Save away context */
		it = (iterator *)slapi_ch_malloc(sizeof(iterator));
		*itcontext = it;
		it->o = o;
		it->cookie = cookie;
		it->co = co;
		retptr = co->data;
	}

	PR_Unlock(o->lock);
	if (NULL != handle)
	{
		*handle = co;
	}

	return retptr;
}



/*
 * Get the next object in the set.
 * Arguments:
 *  o: The object set being enumerated
 *  itcontext: an iteration context, to be passed back to subsequent calls
 *          to repl_objset_next_object.
 *  handle: a pointer to pointer to void. This will be filled in with
 *          a reference to the object's enclosing object.
 *
 * Returns:
 *  A pointer to the next object in the set, or NULL if there are no more
 *  objects.
 */
void *
repl_objset_next_object(Repl_Objset *o, void *itcontext, void **handle)
{
	Repl_Objset_object *co = NULL;
	Repl_Objset_object *tmp_co;
	void *retptr = NULL;
	iterator *it = (iterator *)itcontext;

	PR_ASSERT(NULL != o);
	PR_ASSERT(NULL != it);
	PR_ASSERT(NULL != it->co);

	PR_Lock(o->lock);
	tmp_co = it->co;

	/* Find the next non-deleted object */
	while ((co = llistGetNext(o->objects, &it->cookie)) != NULL &&
			!(co->flags & REPL_OBJSET_OBJ_FLAG_DELETED));

	if (NULL != co)
	{
		acquireNoLock(co);
		it->co = co;
		retptr = co->data;
	}
	else
	{
		/*
		 * No more non-deleted objects - erase context (freeing
		 * it is responsibility of caller.
		 */ 
		it->cookie = NULL;
		it->co = NULL;
	}
	releaseNoLock(o, tmp_co);
	PR_Unlock(o->lock);
	if (NULL != handle)
	{
		*handle = co;
	}
	return retptr;
}



/*
 * Destroy an itcontext iterator
 */
void
repl_objset_iterator_destroy(void **itcontext)
{	
	if (NULL != itcontext && NULL != *itcontext)
	{
		/* check if we did not iterate through the entire list
		   and need to release last accessed element */
		iterator *it = *(iterator**)itcontext;
		if (it->co)
			repl_objset_release (it->o, it->co);
		
		slapi_ch_free((void **)itcontext);
	}
}
