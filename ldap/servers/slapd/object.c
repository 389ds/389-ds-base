/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* Implementation note:
   PR_AtomicIncrement and PR_AtomicDecrement both return a value whose
   sign is the same sign (or zero) as the variable *after* it was updated.
   They do not return the previous value.
*/

#include "slapi-plugin.h"
#include "slapi-private.h"

typedef struct object
{
	PRInt32 refcnt; /* reference count for the object */
	FNFree destructor; /* Destructor for the object */
	void *data;	/* pointer to actual node data */
} object;


/*
 * Create a new object.
 * The object is created with refcnt set to 1. The caller implicitly gets
 * a reference to the object, to prevent a race condition where the object
 * is destroyed immediately after contruction.
 * The provided destructor function will be called when all references to
 * the object have been released.
 *
 * Returns a pointer to the new object.
 */
Object *
object_new(void *user_data, FNFree destructor)
{
	Object *o;
	o = (object *)slapi_ch_malloc(sizeof(object));
	o->refcnt = 1;
	o->destructor = destructor;
	o->data = user_data;
	return o;
}


/*
 * Acquire a reference object. The caller must hold a reference
 * to the object, or know for certain that a reference is held
 * and will continue to be held while this call is in progress.
 */
void
object_acquire(Object *o)
{
	PR_ASSERT(NULL != o);
	PR_AtomicIncrement(&o->refcnt);
}


/*
 * Release a reference to an object. The pointer to the object
 * should not be referenced after this call is made, since the
 * object may be destroyed if this is the last reference to it.
 */
void
object_release(Object *o)
{
	PRInt32 refcnt_after_release;

	PR_ASSERT(NULL != o);
	refcnt_after_release = PR_AtomicDecrement(&o->refcnt);
	PR_ASSERT(refcnt_after_release >= 0);
	if (refcnt_after_release == 0)
	{
		/* Object can be destroyed */
        if (o->destructor)
		    o->destructor(&o->data);
		/* Make it harder to reuse a dangling pointer */
		o->data = NULL;
		o->destructor = NULL;
		o->refcnt = -9999;
		slapi_ch_free((void **)&o);
	}
}

/*
 * Get the data pointer from an object.
 * Results are undefined if the caller does not hold a reference
 * to the object.
 */
void *
object_get_data(Object *o)
{
	PR_ASSERT(NULL != o);
	return o->data;
}
