/**
 * PROPRIETARY/CONFIDENTIAL. Use of this product is subject to
 * license terms. Copyright 2001 Sun Microsystems, Inc.
 * Some preexisting portions Copyright 2001 Netscape Communications Corp.
 * All rights reserved.
 */
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
