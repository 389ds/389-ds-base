/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* ABAPI Broker */
/* Pete Rowley */

#include "stdio.h"
#include "slap.h"
#include "prlock.h"
#include "prerror.h"
#include "prcvar.h"
#include "prio.h"

static Slapi_Mutex *buffer_lock = 0;

/* circular api buffer */

typedef struct _THEABAPI
{
	char *guid;
	void **api;
	struct _THEABAPI *next;
	struct _THEABAPI *prev;
} ABAPI;

typedef struct _API_FEATURES
{
	int refcount;
	slapi_apib_callback_on_zero callback_on_zero;
	Slapi_Mutex *lock;
} APIB_FEATURES;

static ABAPI *head = NULL;

static ABAPI **ABAPIBroker_FindInterface(char *guid);

int slapi_apib_register(char *guid, void **api )
{
	int ret = -1;
	ABAPI *item;

	if(buffer_lock == 0)
	{
		if(0 == (buffer_lock = slapi_new_mutex())) /* we never free this mutex */
			/* badness */
			return -1;
	}

	/* simple - we don't check for duplicates */
	
	item = (ABAPI*)slapi_ch_malloc(sizeof(ABAPI));
	if(item)
	{
		item->guid = guid;
		item->api = api;

		slapi_lock_mutex(buffer_lock);
		if(head == NULL)
		{
			head = item;
			head->next = head;
			head->prev = head;
		}
		else
		{
			item->next = head;
			item->prev = head->prev;
			head->prev = item;
			item->prev->next = item;
		}
		slapi_unlock_mutex(buffer_lock);

		ret = 0;
	}

	return ret;
}

int slapi_apib_unregister(char *guid)
{
	int ret = -1;
	ABAPI **api;

	if(buffer_lock == 0)
		return ret;

	if(buffer_lock == 0)
	{
		if(0 == (buffer_lock = slapi_new_mutex())) /* we never free this mutex */
			/* badness */
			return -1;
	}

	slapi_lock_mutex(buffer_lock);

	if((api = ABAPIBroker_FindInterface(guid)) != NULL)
	{
		(*api)->prev->next = (*api)->next;
		(*api)->next->prev = (*api)->prev;

		if(*api == head)
		{
			head = (*api)->next;
		}

		if(*api == head) /* must be the last item, turn off the lights */
			head = 0;

		slapi_ch_free((void**)api);
		*api = 0;
		ret = 0;
	}

	slapi_unlock_mutex(buffer_lock);

	return ret;
}

int slapi_apib_get_interface(char *guid, void ***api)
{
	int ret = -1;
	ABAPI **theapi;

	if(buffer_lock == 0)
		return ret;

	if(buffer_lock == 0)
	{
		if(0 == (buffer_lock = slapi_new_mutex())) /* we never free this mutex */
			/* badness */
			return -1;
	}

	slapi_lock_mutex(buffer_lock);

	if((theapi = ABAPIBroker_FindInterface(guid)) != NULL)
	{
		*api = (*theapi)->api;
		if((*api)[0])
		{
			slapi_apib_addref(*api);
		}

		ret = 0;
	}

	slapi_unlock_mutex(buffer_lock);

	return ret;
}

int slapi_apib_make_reference_counted(void **api, slapi_apib_callback_on_zero callback_on_zero)
{
	int ret = -1;

	if(api[0] == 0)
	{
		api[0] = slapi_ch_malloc(sizeof(APIB_FEATURES));
		if(api[0])
		{
			((APIB_FEATURES*)(api[0]))->lock = slapi_new_mutex();
			if(((APIB_FEATURES*)(api[0]))->lock)
			{
				((APIB_FEATURES*)(api[0]))->refcount = 0; /* the ref count */
				((APIB_FEATURES*)(api[0]))->callback_on_zero = callback_on_zero;
				ret = 0;
			}
			else
				slapi_ch_free(&(api[0]));
		}
	}

	return ret;
}

int slapi_apib_addref(void **api)
{
	int ret;

	slapi_lock_mutex(((APIB_FEATURES*)(api[0]))->lock);
	
	ret = ++(((APIB_FEATURES*)(api[0]))->refcount);
	
	slapi_unlock_mutex(((APIB_FEATURES*)(api[0]))->lock);

	return ret;
}

int slapi_apib_release(void **api)
{
	APIB_FEATURES *features;
	int ret;

	slapi_lock_mutex(((APIB_FEATURES*)(api[0]))->lock);
	
	ret = --(((APIB_FEATURES*)(api[0]))->refcount);
	
	if(((APIB_FEATURES*)(api[0]))->refcount == 0 && ((APIB_FEATURES*)(api[0]))->callback_on_zero)
	{
		/* save our stuff for when it gets zapped */
		features = (APIB_FEATURES*)api[0];

		if(0==((APIB_FEATURES*)(api[0]))->callback_on_zero(api)) /* this should deregister the interface */
		{
			slapi_unlock_mutex(features->lock);
			slapi_destroy_mutex(features->lock);
			slapi_ch_free((void **)&features);
		}
		else
			slapi_unlock_mutex(features->lock);
	}
	else
		slapi_unlock_mutex(((APIB_FEATURES*)(api[0]))->lock);

	return ret;
}


static ABAPI **ABAPIBroker_FindInterface(char *guid)
{
	static ABAPI *api = 0; /* simple gut feeling optimization for constant calls on same api */
	ABAPI *start_api = api;

	if(!api) {
		start_api = api = head;
	}

	if(api)
	{
		do 
		{
			if(0 == strcmp(guid, api->guid))
			{
				return &api;
			}

			api = api->next;
		}
		while(api != start_api);
	}

	return 0;
}
