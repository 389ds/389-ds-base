/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _LDAPU_INIT_H
#define _LDAPU_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

NSAPI_PUBLIC extern int ldaputil_init (const char *config_file,
				       const char *dllname,
				       const char *serv_root,
				       const char *serv_type,
				       const char *serv_id);

#ifdef __cplusplus
}
#endif

#endif /* _LDAPU_INIT_H */
