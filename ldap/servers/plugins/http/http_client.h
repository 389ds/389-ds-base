/* --- BEGIN COPYRIGHT BLOCK ---
 * This Program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; version 2 of the License.
 * 
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations
 * including the two, subject to the limitations in this paragraph. Non-GPL Code
 * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from
 * the Approved Interfaces without causing the resulting work to be covered by
 * the GNU General Public License. Only Red Hat, Inc. may make changes or
 * additions to the list of Approved Interfaces. You must obey the GNU General
 * Public License in all respects for all of the Program code and other code used
 * in conjunction with the Program except the Non-GPL Code covered by this
 * exception. If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


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
