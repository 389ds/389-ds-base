/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include	<string.h>
#include	<libaccess/acl.h>
#include	"aclpriv.h"
#include	<libaccess/aclglobal.h>

/* Ordered list of generic rights	*/
char	*generic_rights[7] = {
				"read",
				"write",
				"execute",
				"delete",
				"info",
				"list",
				NULL
			} ;

char	*http_generic[7] = {
				"http_get, http_head, http_trace, http_revlog, http_options, http_copy, http_getattribute, http_index, http_getproperties, http_getattributenames ",
				"http_put, http_mkdir, http_startrev, http_stoprev, http_edit, http_post, http_save, http_setattribute, http_revadd, http_revlabel, http_lock, http_unlock, http_unedit, http_stoprev, http_startrev",
				"http_post",
				"http_delete, http_destroy, http_move",
				"http_head, http_trace, http_options",
				"http_index",
				NULL
			} ;

/* Pointer to all global ACL data.  This pointer is moved (atomically)
   when a cache flush call is made.
*/
ACLGlobal_p	ACLGlobal;
ACLGlobal_p	oldACLGlobal;
