/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include	<stdio.h>
#include	<netsite.h>
#include	<libaccess/nserror.h>
#include	<libaccess/acl.h>
#include	"../aclpriv.h"
#include	<libaccess/aclproto.h>
#include	<libaccess/las.h>
#include	<base/plist.h>
#include	<base/ereport.h>

extern	ACLListHandle_t *ACL_ParseFile(NSErr_t *errp, char *filename);


int main(int arc, char **argv)
{
	int	result;
	ACLEvalHandle_t	eval;
	char	*rights[2];
	char	*bong;
	char	*bong_type;
	char	*acl_tag;
	int	expr_num;

	/*	ACL Eval Unit Tests
	 */
	rights[0] = "read";
	rights[1] = "write";
	rights[2] = NULL;

			eval.acllist	= ACL_ParseFile((NSErr_t *)NULL, argv[1]);
			result	= ACL_EvalTestRights(NULL, &eval, &rights[0], NULL, &bong, &bong_type, &acl_tag, &expr_num);
			ACL_ListDestroy(NULL, eval.acllist);
			printf("%s = %d\n\n", argv[1], result);

}
