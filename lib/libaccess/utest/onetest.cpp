/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include	<stdio.h>
#include	<netsite.h>
#include	<libaccess/nserror.h>
#include	<base/session.h>
#include	<libaccess/acl.h>
#include	"../aclpriv.h"
#include	<libaccess/aclproto.h>
#include	<libaccess/las.h>
#include	<base/plist.h>
#include	<base/ereport.h>

extern	ACLListHandle_t *ACL_ParseFile(NSErr_t *errp, char *filename);
extern  ACLEvalDestroyContext(NSErr_t *errp, ACLEvalHandle_t *acleval);


main(int arc, char **argv)
{
	int	result;
	int	cachable;
	void	*las_cookie=NULL;
	ACLEvalHandle_t	eval;
	char	*rights[2];
	char	filename[20];
	int	i;
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
			ACLEvalDestroyContext(NULL, &eval);
			ACL_ListDestroy(NULL, eval.acllist);
			printf("%s = %d\n\n", argv[1], result);

}
