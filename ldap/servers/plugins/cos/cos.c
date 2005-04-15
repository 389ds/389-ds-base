/** BEGIN COPYRIGHT BLOCK
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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <string.h>
#include "portable.h"
#include "nspr.h"
#include "slapi-plugin.h"
#include "slapi-private.h"
#include <dirlite_strings.h> /* PLUGIN_MAGIC_VENDOR_STR */
#include "dirver.h"
#include "cos_cache.h"
#include "vattr_spi.h"

/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif

/*** secret slapd stuff ***/

/*
	these are required here because they are not available
	in any public header.  They must exactly match their
	counterparts in the server or they will fail to work
	correctly.
*/

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
#define LDAP_DEBUG_ANY          0x04000		/* 16384 */
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

/*** end secrets ***/

#define COS_PLUGIN_SUBSYSTEM   "cos-plugin"   /* used for logging */

/* subrelease in the following version info is for odd-ball cos releases
 * which do not fit into a general release, this can be used for beta releases
 * and other (this version stuff is really to help outside applications which
 * may wish to update cos decide whether the cos version they want to update to
 * is a higher release than the installed plugin)
 *
 * note: release origin is 00 for directory server
 *       sub-release should be:
 *         50 for initial RTM products
 *         from 0 increasing for alpha/beta releases
 *         from 51 increasing for patch releases
 */
#define COS_VERSION	0x00050050		/* version format: 0x release origin 00 major 05 minor 00 sub-release 00 */

/* other function prototypes */
int cos_init( Slapi_PBlock *pb ); 
int cos_compute(computed_attr_context *c,char* type,Slapi_Entry *e,slapi_compute_output_t outputfn);
int cos_start( Slapi_PBlock *pb );
int cos_close( Slapi_PBlock *pb );
int cos_post_op( Slapi_PBlock *pb );


static Slapi_PluginDesc pdesc = { "cos", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"class of service plugin" };

static void * cos_plugin_identity = NULL;


#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

/*
** Plugin identity mgmt
*/

void cos_set_plugin_identity(void * identity) 
{
	cos_plugin_identity=identity;
}

void * cos_get_plugin_identity()
{
	return cos_plugin_identity;
}

int cos_version()
{
	return COS_VERSION;
}

/* 
	cos_init
	--------
	adds our callbacks to the list
*/
int cos_init( Slapi_PBlock *pb )
{
	int ret = 0;
	void * plugin_identity=NULL;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_init\n",0,0,0);

	/*
	** Store the plugin identity for later use.
	** Used for internal operations
	*/
	
    	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    	PR_ASSERT (plugin_identity);
	cos_set_plugin_identity(plugin_identity);
	
	if (	slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    			SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
        	         (void *) cos_start ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODIFY_FN,
        	         (void *) cos_post_op ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_MODRDN_FN,
        	         (void *) cos_post_op ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_ADD_FN,
        	         (void *) cos_post_op ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_POST_DELETE_FN,
        	         (void *) cos_post_op ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
        	         (void *) cos_close ) != 0 ||
			slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                     (void *)&pdesc ) != 0 )
    {
        slapi_log_error( SLAPI_LOG_FATAL, COS_PLUGIN_SUBSYSTEM,
                         "cos_init: failed to register plugin\n" );
		ret = -1;
    }

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_init\n",0,0,0);
    return ret;
}


/*
	cos_start
	---------
	This function registers the computed attribute evaluator
	and inits the cos cache.
	It is called after cos_init.
*/
int cos_start( Slapi_PBlock *pb )
{
	int ret = 0;

	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_start\n",0,0,0);

	if( !cos_cache_init() )
	{
		LDAPDebug( LDAP_DEBUG_PLUGIN, "cos: ready for service\n",0,0,0);
	}
	else
	{
		/* problems we are hosed */
		cos_cache_stop();
		LDAPDebug( LDAP_DEBUG_ANY, "cos_start: failed to initialise\n",0,0,0);
		ret = -1;
	}

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_start\n",0,0,0);
	return ret;
}

/*
	cos_close
	---------
	closes down the cache
*/
int cos_close( Slapi_PBlock *pb )
{
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_close\n",0,0,0);

	cos_cache_stop();

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_close\n",0,0,0);

	return 0;
}

/*
	cos_compute
	-----------
	called when evaluating named attributes in a search
	and attributes remain unfound in the entry,
	this function checks the attribute for a match with
	those in the class of service definitions, and if a
	match is found, adds the attribute and value to the
	output list

	returns 
		0 on success
		1 on outright failure
		-1 when doesn't know about attribute
*/
int cos_compute(computed_attr_context *c,char* type,Slapi_Entry *e,slapi_compute_output_t outputfn)
{
	int ret = -1;

	return ret;
}


/*
	cos_post_op
	-----------
	Catch all for all post operations that change entries
	in some way - this simply notifies the cache of a
	change - the cache decides if action is necessary
*/
int cos_post_op( Slapi_PBlock *pb )
{
	LDAPDebug( LDAP_DEBUG_TRACE, "--> cos_post_op\n",0,0,0);

	cos_cache_change_notify(pb);

	LDAPDebug( LDAP_DEBUG_TRACE, "<-- cos_post_op\n",0,0,0);
	return 0; /* always succeed */
}

