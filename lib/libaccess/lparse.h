/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 *  This grammar is intended to parse the version 3.0 ACL
 *  and output an ACLParseACE_t structure.
 */

#ifndef LPARSE_H
#define LPARSE_H

#ifdef __cplusplus
extern "C" {
#endif

extern int aclPushListHandle(ACLListHandle_t *handle);
extern int aclparse(void);


#ifdef __cplusplus
}
#endif

#endif
