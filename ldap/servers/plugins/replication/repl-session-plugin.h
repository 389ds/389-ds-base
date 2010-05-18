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
 * Copyright (C) 2010 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef REPL_SESSION_PLUGIN_PUBLIC_API
#define REPL_SESSION_PLUGIN_PUBLIC_API

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "slapi-plugin.h"

/*
 * Replication Session plug-in API
 */
#define REPL_SESSION_v1_0_GUID "210D7559-566B-41C6-9B03-5523BDF30880"

/*
 * This callback is called when a replication agreement is created.
 * The repl_subtree from the agreement is read-only.
 * The callback can allocate some private data to return.  If so
 * the callback must define a repl_session_plugin_destroy_agmt_cb
 * so that the private data can be freed.  This private data is passed
 * to other callback functions on a master as the void *cookie argument.
 */
typedef void * (*repl_session_plugin_agmt_init_cb)(const Slapi_DN *repl_subtree);
#define REPL_SESSION_PLUGIN_AGMT_INIT_CB 1

/*
 * Callbacks called when acquiring a replica
 *
 * The pre and post callbacks are called on the sending (master) side.
 * The receive and reply callbacks are called on the receiving (replica)
 * side.
 *
 * Data can be exchanged between the sending and receiving sides using
 * these callbacks by using the data_guid and data parameters.  The data 
 * guid is used as an identifier to confirm the data type.  Your callbacks
 * that receive data must consult the data_guid before attempting to read
 * the data parameter. This allows you to confirm that the same replication
 * session plug-in is being used on both sides before making assumptions
 * about the format of the data. The callbacks use these parameters as
 * follows:
 *
 *   pre - send data to replica
 *   recv - receive data from master
 *   reply - send data to master
 *   post - receive data from replica
 *
 * The memory used by data_guid and data should be allocated in the pre
 * and reply callbacks.  The replication plug-in is responsible for
 * freeing this memory, so they should not be free'd in the callbacks.
 *
 * The return value of the callbacks should be 0 to allow replication
 * to continue. A non-0 return value will cause the replication session
 * to be abandoned, causing the master to go into incremental backoff
 * mode.
 */
typedef int (*repl_session_plugin_pre_acquire_cb)(void *cookie, const Slapi_DN *repl_subtree,
                                           int is_total, char **data_guid, struct berval **data);
#define REPL_SESSION_PLUGIN_PRE_ACQUIRE_CB 2

typedef int (*repl_session_plugin_reply_acquire_cb)(const char *repl_subtree, int is_total,
                                                    char **data_guid, struct berval **data);
#define REPL_SESSION_PLUGIN_REPLY_ACQUIRE_CB 3

typedef int (*repl_session_plugin_post_acquire_cb)(void *cookie, const Slapi_DN *repl_subtree,
                                           int is_total, const char *data_guid, const struct berval *data);
#define REPL_SESSION_PLUGIN_POST_ACQUIRE_CB 4

typedef int (*repl_session_plugin_recv_acquire_cb)(const char *repl_subtree, int is_total,
                                           const char *data_guid, const struct berval *data);
#define REPL_SESSION_PLUGIN_RECV_ACQUIRE_CB 5

/*
 * Callbacks called when the agreement is destroyed.
 * 
 * The replication subtree from the agreement is passed in.
 * This is read only.
 * The plugin must define this function to free the cookie allocated
 * in the init function, if any.
 */
typedef void (*repl_session_plugin_destroy_agmt_cb)(void *cookie, const Slapi_DN *repl_subtree);
#define REPL_SESSION_PLUGIN_DESTROY_AGMT_CB 6

#endif /* REPL_SESSION_PLUGIN_PUBLIC_API */
