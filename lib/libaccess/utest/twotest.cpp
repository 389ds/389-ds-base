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
#include	<stdio.h>
#include	<netsite.h>
#include	<base/plist.h>
#include	<base/ereport.h>
#include	<libaccess/nserror.h>
#include	<libaccess/acl.h>
#include	"../aclpriv.h"
#include	<libaccess/aclproto.h>
#include	<libaccess/las.h>


extern	ACLListHandle_t *ACL_ParseFile(NSErr_t *errp, char *filename);

int main(int arc, char **argv)
{
	int	result;
	ACLEvalHandle_t	eval;
	char	*rights[2];
	char	*map_generic[7];
	char	*bong;
	char	*bong_type;
	char	*acl_tag;
	int	expr_num;

	/*	ACL Eval Unit Tests
	 */

	rights[0] = "html_read";
	rights[1] = "html_write";
	rights[2] = NULL;

	map_generic[0]	= "html_read";
	map_generic[1]	= "html_write";
	map_generic[2]	= "N/A";
	map_generic[3]	= "html_create";
	map_generic[4]	= "html_delete";
	map_generic[5]	= "N/A";
	map_generic[6]	= NULL;

		eval.acllist	= ACL_ParseFile((NSErr_t *)NULL, argv[1]);
		result	= ACL_EvalTestRights(NULL, &eval, &rights[0], map_generic, &bong, &bong_type, &acl_tag, &expr_num);
		ACL_ListDestroy(NULL, eval.acllist);
		printf("%s = %d\n\n", argv[1], result);

}
