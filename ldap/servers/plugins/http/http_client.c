/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */

/**
 * Simple Http Client API broker plugin 
 */

#include <stdio.h>
#include <string.h>

#include "portable.h"
#include "nspr.h"

#include "slapi-plugin.h"
#include "slapi-private.h"
#include "dirlite_strings.h"
#include "dirver.h"

#include "http_client.h"
#include "http_impl.h"

/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif

/*** from proto-slap.h ***/

int slapd_log_error_proc( char *subsystem, char *fmt, ... );

/*** from ldaplog.h ***/

/* edited ldaplog.h for LDAPDebug()*/
#ifndef _LDAPLOG_H
#define _LDAPLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#define LDAP_DEBUG_TRACE	0x00001		/*     1 */
#define LDAP_DEBUG_ANY      0x04000		/* 16384 */
#define LDAP_DEBUG_PLUGIN	0x10000		/* 65536 */

/* debugging stuff */
#    ifdef _WIN32
       extern int	*module_ldap_debug;
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( *module_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#    else /* _WIN32 */
       extern int	slapd_ldap_debug;
#      define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( slapd_ldap_debug & level ) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }
#    endif /* Win32 */

#ifdef __cplusplus
}
#endif

#endif /* _LDAP_H */

#define HTTP_PLUGIN_SUBSYSTEM   "http-client-plugin"   /* used for logging */
#define HTTP_PLUGIN_VERSION		0x00050050

#define HTTP_SUCCESS	0
#define HTTP_FAILURE	-1

/**
 * Implementation functions
 */
static void *api[7];

/**
 * Plugin identifiers
 */
static Slapi_PluginDesc pdesc = { "http-client",
								  PLUGIN_MAGIC_VENDOR_STR,
								  PRODUCTTEXT,
								  "HTTP Client plugin" };

static Slapi_ComponentId *plugin_id = NULL;

/**
 **	
 **	Http plug-in management functions
 **
 **/
int http_client_init(Slapi_PBlock *pb); 
static int http_client_start(Slapi_PBlock *pb);
static int http_client_close(Slapi_PBlock *pb);

/**
 * our functions
 */
static void _http_init(Slapi_ComponentId *plugin_id);
static int _http_get_text(char *url, char **data, int *bytesRead);
static int _http_get_binary(char *url, char **data, int *bytesRead);
static int _http_get_redirected_uri(char *url, char **data, int *bytesRead);
static int _http_post(char *url, httpheader **httpheaderArray, char *body, char **data, int *bytesRead);
static void _http_shutdown( void );

#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

/**
 *	
 * Get the presence plug-in version
 *
 */
int http_client_version()
{
	return HTTP_PLUGIN_VERSION;
}

int http_client_init(Slapi_PBlock *pb)
{
	int status = HTTP_SUCCESS;
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> http_client_init -- BEGIN\n",0,0,0);

	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    	SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	    slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
        	     (void *) http_client_start ) != 0 ||
	    slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
        	     (void *) http_client_close ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
             (void *)&pdesc ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                     "http_client_init: failed to register plugin\n" );
		status = HTTP_FAILURE;
	}

        /* Retrieve and save the plugin identity to later pass to
        internal operations */
        if (slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_id) != 0) {
         slapi_log_error(SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                        "http_client_init: Failed to retrieve SLAPI_PLUGIN_IDENTITY\n");
         return HTTP_FAILURE;
        }

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- http_client_init -- END\n",0,0,0);
    return status;
}

static int http_client_start(Slapi_PBlock *pb)
{
	int status = HTTP_SUCCESS;
	/**
	 * do some init work here
	 */
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> http_client_start -- BEGIN\n",0,0,0);

	api[0] = 0; /* reserved for api broker use, must be zero */
	api[1] = (void *)_http_init;
	api[2] = (void *)_http_get_text;
	api[3] = (void *)_http_get_binary;
	api[4] = (void *)_http_get_redirected_uri;
	api[5] = (void *)_http_shutdown;
	api[6] = (void *)_http_post;

	if( slapi_apib_register(HTTP_v1_0_GUID, api) ) {
		slapi_log_error( SLAPI_LOG_FATAL, HTTP_PLUGIN_SUBSYSTEM,
                     "http_client_start: failed to register functions\n" );
		status = HTTP_FAILURE;
	}
	
	_http_init(plugin_id);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- http_client_start -- END\n",0,0,0);
	return status;
}

static int http_client_close(Slapi_PBlock *pb)
{
	int status = HTTP_SUCCESS;
	/**
	 * do cleanup 
	 */
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> http_client_close -- BEGIN\n",0,0,0);

	slapi_apib_unregister(HTTP_v1_0_GUID);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- http_client_close -- END\n",0,0,0);

	return status;
}

/**
 * perform http initialization here 
 */
static void _http_init(Slapi_ComponentId *plugin_id)
{
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> _http_init -- BEGIN\n",0,0,0);
	
	http_impl_init(plugin_id);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- _http_init -- END\n",0,0,0);
}

/**
 * This method gets the data in a text format based on the 
 * URL send.
 */
static int _http_get_text(char *url, char **data, int *bytesRead)
{
	int status = HTTP_SUCCESS;
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> _http_get_text -- BEGIN\n",0,0,0);

	status = http_impl_get_text(url, data, bytesRead);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- _http_get_text -- END\n",0,0,0);
	return status;
}

/**
 * This method gets the data in a binary format based on the 
 * URL send.
 */
static int _http_get_binary(char *url, char **data, int *bytesRead)
{
	int status = HTTP_SUCCESS;
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> _http_get_binary -- BEGIN\n",0,0,0);

	status = http_impl_get_binary(url, data, bytesRead);
	
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- _http_get_binary -- END\n",0,0,0);
	return status;
}

/**
 * This method intercepts the redirected URI and returns the location 
 * information.
 */
static int _http_get_redirected_uri(char *url, char **data, int *bytesRead)
{
	int status = HTTP_SUCCESS;
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> _http_get_redirected_uri -- BEGIN\n",0,0,0);

	status = http_impl_get_redirected_uri(url, data, bytesRead);
	
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- _http_get_redirected_uri -- END\n",0,0,0);
	return status;
}

/**
 * This method posts the data based on the URL send. 
 */
static int _http_post(char *url, httpheader ** httpheaderArray, char *body, char **data, int *bytesRead)
{
	int status = HTTP_SUCCESS;
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> _http_post -- BEGIN\n",0,0,0);

	status = http_impl_post(url, httpheaderArray, body, data, bytesRead);
	
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- _http_post -- END\n",0,0,0);
	return status;
}

/**
 * perform http shutdown here 
 */
static void _http_shutdown( void )
{
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> _http_shutdown -- BEGIN\n",0,0,0);
	
	http_impl_shutdown();

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- _http_shutdown -- END\n",0,0,0);
}

