/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

#ifndef _HTTP_H_
#define _HTTP_H_

/* mechanics */


typedef void (*api_http_init)();
typedef int (*api_get_http_text)(char *url, char *text_data);
typedef int (*api_get_http_binary)(char *url, char* bin_data, int *len);
typedef void (*api_http_shutdown)();

/* API ID for http_apib_get_interface */

#define HTTP_v1_0_GUID "0A340151-6FB3-11d3-80D2-006008A6EFF3"

/* API */

/* the api broker reserves api[0] for its use */

#define http_init() \
	((api_http_init*)(api))[1]()

#define get_http_text(url, text_data) \
	((api_get_http_text*)(api))[2]( url, text_data)

#define get_http_binary(url, bin_data, len) \
	((api_get_http_binary*)(api))[3](url,bin_data, len)

#define set_http_shutdown() \
	((api_http_shutdown*)(api))[4]()

/* HTTP to be passed to http_register() by presence sps*/
#define http_api(api) api[5]


#endif /*_HTTP_H_*/
