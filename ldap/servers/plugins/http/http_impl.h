/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * --- END COPYRIGHT BLOCK --- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#ifndef HTTP_IMPL_H__
#define HTTP_IMPL_H__

#ifdef __cplusplus
extern "C" {
#endif

int http_impl_init(Slapi_ComponentId *plugin_id);
int http_impl_get_text(char *url, char **data, int *bytesRead);
int http_impl_get_binary(char *url, char **data, int *bytesRead);
int http_impl_get_redirected_uri(char *url, char **data, int *bytesRead);
int http_impl_post(char *url, httpheader **httpheaderArray, char *body, char **data, int *bytesRead);
void http_impl_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
