/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

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
void http_impl_shutdown();

#ifdef __cplusplus
}
#endif

#endif
