/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include	<stdio.h>
#include	<netsite.h>
#include	<base/session.h>
#include	<base/plist.h>
#include	<base/ereport.h>
#include	<libaccess/nserror.h>
#include	<libaccess/acl.h>
#include	"../aclpriv.h"
#include	<libaccess/aclproto.h>
#include	<libaccess/las.h>


extern	ACLListHandle_t *ACL_ParseFile(NSErr_t *errp, char *filename);
extern  ACLEvalDestroyContext(NSErr_t *errp, ACLEvalHandle_t *acleval);

main(int arc, char **argv)
{
	int	result;
	int	cachable;
	void	*las_cookie=NULL;
	ACLEvalHandle_t	eval;
	char	*rights[2];
	char	*map_generic[7];
	char	filename[20];
	int	i;
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
		ACLEvalDestroyContext(NULL, &eval);
		ACL_ListDestroy(NULL, eval.acllist);
		printf("%s = %d\n\n", argv[1], result);

}
