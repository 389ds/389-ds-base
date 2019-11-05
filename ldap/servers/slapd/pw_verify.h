/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2016 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifndef _SLAPD_PW_VERIFY_H_
#define _SLAPD_PW_VERIFY_H_

int pw_verify_root_dn(const char *dn, const Slapi_Value *cred);
int pw_verify_be_dn(Slapi_PBlock *pb, Slapi_Entry **referral);
int pw_validate_be_dn(Slapi_PBlock *pb, Slapi_Entry **referral);
int32_t pw_verify_token_dn(Slapi_PBlock *pb);

#endif /* _SLAPD_PW_VERIFY_H_ */
