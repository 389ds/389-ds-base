/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef ACLUTIL_H
#define ACLUTIL_H

NSPR_BEGIN_EXTERN_C

int evalComparator(CmpOp_t ctok, int result);
void makelower(char *string);
int EvalToRes(int value);
const char *comparator_string (int comparator);

/*Warning: acl_next_token modifies 'ptr' */
char *acl_next_token (char **ptr, char delim);

const char *acl_next_token_len (const char *ptr,
				char delim, int *len);

NSPR_END_EXTERN_C

#endif
