/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * eventhandler.h: Handle registration of event handlers
 *
 * This is a facility in the NT server to provide a way to register event
 * handling functions. Often there is a need to send a control signal of some
 * kind to the server. This could be a signal for the server to rotate its
 * logs, or a signal to collect and return statistical information of some kind
 * such as perfmon stats.
 * 
 * This file specifies the structures and functions necessary to set up this
 * kind of asynchronous special event handling.
 * 
 * Aruna Victor 2/21/96
 */

#ifndef EVENTHANDLER_H
#define EVENTHANDLER_H

#include "netsite.h"

/* ------------------------------ Structures ------------------------------ */

/* EVENT_HANDLER specifies
    1. The name of the event. This is the event that the event handler will
       create and wait on for a signal.
    2. The name of the function should be called to handle the event.
    3. The argument that should be passed to this function.
    4. The next EVENT_HANDLER on the list this structure is on. */

typedef struct event_handler {
	int event_number;
    char *event_name;
    void (*_event_handler)(void *);
    void *argument;
    struct event_handler *next;
} EVENT_HANDLER;

/* ------------------------------ Prototypes ------------------------------ */

NSPR_BEGIN_EXTERN_C

char *initialize_event_handler(char *serverid);

char *terminate_event_handler();

char *add_handler(char *event, void (*fn)(void *), void *arg);

char *delete_handler(char *event);

char *add_rotation_handler(char *event, void (*fn)(void *), void *arg);

NSPR_END_EXTERN_C

#endif /* !EVENTHANDLER	 */














