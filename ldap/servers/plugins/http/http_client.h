/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

#ifndef _HTTP_CLIENT_H_
#define _HTTP_CLIENT_H_

/* Error codes */
#define HTTP_CLIENT_ERROR_BAD_URL		-1
#define HTTP_CLIENT_ERROR_NET_ADDR		-2
#define HTTP_CLIENT_ERROR_SOCKET_CREATE     	-3
#define HTTP_CLIENT_ERROR_CONNECT_FAILED	-4
#define HTTP_CLIENT_ERROR_SEND_REQ		-5
#define HTTP_CLIENT_ERROR_BAD_RESPONSE	-6
#define HTTP_CLIENT_ERROR_SSLSOCKET_CREATE	-7
 #define HTTP_CLIENT_ERROR_NSS_INITIALIZE    -8

/*Structure to store HTTP Headers */
typedef struct {
        char    *name;
        char    *value;
}       httpheader;


/* mechanics */


typedef void (*api_http_init)(Slapi_ComponentId *plugin_id);
typedef int (*api_http_get_text)(char *url, char **data, int *bytesRead);
typedef int (*api_http_get_binary)(char *url, char **data, int *bytesRead);
typedef int (*api_http_get_redirected_uri)(char *url, char **data, int *bytesRead);
typedef void (*api_http_shutdown)();
typedef int (*api_http_post)(char *url, httpheader **httpheaderArray, char *body, char **data, int *bytesRead);

/* API ID for http_apib_get_interface */

#define HTTP_v1_0_GUID "811c5ea2-fef4-4f1c-9ab4-fcf746cd6efc"

/* API */

/* the api broker reserves api[0] for its use */

#define http_init(api) \
	((api_http_init*)(api))[1](Slapi_ComponentId *plugin_id)

#define http_get_text(api, url, data, bytesRead) \
	((api_http_get_text*)(api))[2]( url, data, bytesRead)

#define http_get_binary(api, url, data, bytesRead) \
	((api_http_get_binary*)(api))[3](url, data, bytesRead)

#define http_get_redirected_uri(api, url, data, bytesRead) \
	((api_http_get_redirected_uri*)(api))[4](url, data, bytesRead)

#define http_shutdown(api) \
	((api_http_shutdown*)(api))[5]()

#define http_post(api, url, httpheaderArray, body, data, bytesRead) \
	((api_http_post*)(api))[6](url, httpheaderArray, body, data, bytesRead)

#endif /*_HTTP_CLIENT_H_*/
