/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * ACL parser unit test program
 */

#include <stdio.h>
#include <netsite.h>
#include <libaccess/acl.h>
#include <libaccess/nserror.h>
#include "../aclpriv.h"
#include <libaccess/aclproto.h>

main(int argc, char **argv)
{

ACLListHandle_t *acllist;
int 		ii;
char		filename[255];
ACLWrapper_t	*wrap;
ACLExprHandle_t	*expr;
	
	if ( argc < 2 ) {
		fprintf(stderr, "usage: aclparse <filenames>\n");
		exit(1);
	}
	for (ii = 1; ii < argc; ii++ ) {
		acllist = ACL_ParseFile(NULL, argv[ii]);
		if ( acllist == NULL ) {
			printf("Failed to parse ACL.\n");
			
		} else {
			for (wrap = acllist->acl_list_head; wrap;
				wrap = wrap->wrap_next) {
				for (expr=wrap->acl->expr_list_head;
					expr;
					expr = expr->expr_next ) {
					ACL_ExprDisplay(expr);
				}
			}
		}	
		

		sprintf(filename, "%s.v30", argv[ii]);
                ACL_WriteFile(NULL, filename, acllist);
		ACL_ListDestroy( acllist );
	} 

}
