/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include "repl_helper.h"

ReplGenericList *
ReplGenericListNew(void)
{
    ReplGenericList *list=NULL;
    if(NULL == (list = (ReplGenericList *)
		slapi_ch_calloc(1,sizeof(ReplGenericList)))) {
	return(NULL);
    }
    list->object = NULL;
    list->next = NULL;
    list->prev = NULL;
    return(list);
}

void
ReplGenericListAddObject(ReplGenericList *list,
			 void *newObject)
{
    if(list) {
	ReplGenericList *new_struct = (ReplGenericList *)
	    slapi_ch_calloc(1, sizeof(ReplGenericList));

	if (!new_struct)
	    return;
	/* set back pointer of old first element */
	if(list->next) {
	    list->next->prev = new_struct;
	}

	/* we might have a next but since we are the first we WONT have
	   a previous */
	new_struct->object = newObject;
	new_struct->next = list->next;
	new_struct->prev = NULL;

	/* the new element is the first one */
	list->next = new_struct;

	/* if this is the only element it is the end too */
	if(NULL == list->prev)
	    list->prev = new_struct;
	
    }
    return;
}

ReplGenericList *
ReplGenericListFindObject(ReplGenericList *list,
			  void *object)
{
    if(!list)
	return(NULL);
    list = list->next;  /* the first list item never has data */
    
    while (list) {
	if(list->object == object)
	    return(list);
	list = list->next;
    }
    return(NULL);
}

void
ReplGenericListDestroy(ReplGenericList *list,
		       ReplGenericListObjectDestroyFn destroyFn)
{
    ReplGenericList *list_ptr;

    while (list) {
	list_ptr = list;
	list = list->next;
	if(destroyFn && list_ptr->object) {
	    (destroyFn)(list_ptr->object);
	}
	slapi_ch_free((void **)(&list_ptr));
    }
    return;    
}
