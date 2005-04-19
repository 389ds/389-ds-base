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
