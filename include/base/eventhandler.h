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
 * do so, delete this exception statement from your version. 
 * 
 * 
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














