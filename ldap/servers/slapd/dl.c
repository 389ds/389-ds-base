/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* DataList access functions */
#define INIT_ALLOC		8
#define ALLOC_INCREMENT	4

#include "slap.h"

DataList* dl_new ()
{
	return (DataList*)slapi_ch_malloc (sizeof (DataList));
}
void dl_free (DataList **dl)
{
	slapi_ch_free ((void**) dl);
}

void dl_init (DataList *dl, int init_alloc)
{
	PR_ASSERT (dl);

	memset (dl, 0, sizeof (*dl));

	if (init_alloc <= 0)
		dl->alloc_count = INIT_ALLOC;
	else
		dl->alloc_count = init_alloc;

	dl->elements = (void**)slapi_ch_calloc (dl->alloc_count, sizeof (void*));	
}

void dl_cleanup (DataList *dl, FREEFN freefn)
{
	PR_ASSERT (dl);

	if (freefn && dl->elements)
	{
		int i;

		for (i = 0; i < dl->element_count; i++)
		{
			freefn (&(dl->elements[i]));
		}
	}

	if (dl->elements)
	{
		slapi_ch_free ((void**)&dl->elements);
	}

	memset (dl, 0, sizeof (*dl));
}

/* index == 1 :  insert first */
void dl_add_index (DataList *dl, void *element, int index)
{
	int i = 0;

	PR_ASSERT (dl);
	PR_ASSERT (element);

	if (dl->element_count == dl->alloc_count)
	{
		dl->alloc_count += ALLOC_INCREMENT;
		dl->elements = (void**)slapi_ch_realloc ((char*)dl->elements, dl->alloc_count * sizeof (void*));
	}

	dl->element_count ++;

    for (i = dl->element_count-1; i >= index; i--)
    {
		dl->elements[i] = dl->elements[i-1];
    }
	if ( dl->element_count < index )
	{
		/* Means that we are adding the first element */
		dl->elements[0] = element;
	}
	else
	{
		dl->elements[i] = element;
	}
}

void dl_add (DataList *dl, void *element)
{
	PR_ASSERT (dl);
	PR_ASSERT (element);

	if (dl->element_count == dl->alloc_count)
	{
		dl->alloc_count += ALLOC_INCREMENT;
		dl->elements = (void**)slapi_ch_realloc ((char*)dl->elements, dl->alloc_count * sizeof (void*));
	}

	dl->elements[dl->element_count] = element;
	dl->element_count ++;
}

void *dl_get_first (const DataList *dl, int *cookie)
{
	PR_ASSERT (dl);
	PR_ASSERT (cookie);

	if (dl->element_count == 0)
		return NULL;

	*cookie = 1;
	return dl->elements[0];
}

void *dl_get_next (const DataList *dl, int *cookie)
{
	PR_ASSERT (dl);
	PR_ASSERT (cookie && *cookie > 0);

	if (*cookie >= dl->element_count)
		return NULL;

	return dl->elements[(*cookie)++];
}

void *dl_replace(const DataList *dl, const void *elementOld, void *elementNew, CMPFN cmpfn, FREEFN freefn)
{
	int i;
	void *save;

    PR_ASSERT (dl);
    PR_ASSERT (elementOld);
    PR_ASSERT (elementNew);
    PR_ASSERT (cmpfn);
 
    for (i = 0; i < dl->element_count; i++)
    {
        if (cmpfn (dl->elements[i], elementOld) == 0)
        {
            /* if we have destructor - free the data; otherwise, return it to the client */
            if (freefn)
            {
                freefn (&dl->elements[i]);
                save = NULL;
            }
            else
                save = dl->elements[i];
 
			dl->elements[i] = elementNew;
 
            return save;
        }
    }

	return NULL;	
}

void *dl_get (const DataList *dl, const void *element, CMPFN cmpfn)
{
	int i;

	PR_ASSERT (dl);
	PR_ASSERT (element);
	PR_ASSERT (cmpfn);

	for (i = 0; i < dl->element_count; i++)
	{
		if (cmpfn (dl->elements[i], element) == 0)
		{
			return dl->elements[i];
		}
	}

	return NULL;
}

void *dl_delete (DataList *dl, const void *element, CMPFN cmpfn, FREEFN freefn)
{
	int i;
	void *save;

	PR_ASSERT (dl);
	PR_ASSERT (element);
	PR_ASSERT (cmpfn);

	for (i = 0; i < dl->element_count; i++)
	{
		if (cmpfn (dl->elements[i], element) == 0)
		{	
			/* if we have destructor - free the data; otherwise, return it to the client */
			if (freefn)
			{
				freefn (&dl->elements[i]);
				save = NULL;
			}
			else
				save = dl->elements[i];

			if (i != dl->element_count - 1)
			{
				memcpy (&dl->elements[i], &dl->elements[i+1], (dl->element_count - i - 1) * sizeof (void*));
			}
		
			dl->element_count --;

			return save;
		}
	}

	return NULL;
}

int dl_get_count (const DataList *dl)
{
	PR_ASSERT (dl);

	return dl->element_count;
}

