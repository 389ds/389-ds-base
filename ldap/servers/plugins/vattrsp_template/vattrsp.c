/** BEGIN COPYRIGHT BLOCK
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
#include "vattr_spi.h"

/* the global plugin handle */
static volatile vattr_sp_handle *vattr_handle = NULL;

/* get file mode flags for unix */
#ifndef _WIN32
#include <sys/stat.h>
#endif


#define VATTRSP_PLUGIN_SUBSYSTEM   "vattrsp-template-plugin"   /* used for logging */

/* function prototypes */
int vattrsp_init( Slapi_PBlock *pb ); 
static int vattrsp_start( Slapi_PBlock *pb );
static int vattrsp_close( Slapi_PBlock *pb );
static int vattrsp_vattr_get(
	  vattr_sp_handle *handle,
	  vattr_context *c,
	  Slapi_Entry *e,
	  char *type,
	  Slapi_ValueSet** results,
	  int *type_name_disposition,
	  char** actual_type_name,
	  int flags,
	  int *free_flags,
	  void *hint 
	  );
static int vattrsp_vattr_compare(
		vattr_sp_handle *handle,
		vattr_context *c,
		Slapi_Entry *e,
		char *type,
		Slapi_Value *test_this,
		int* result,
		int flags,
		void *hint
		);
static int vattrsp_vattr_types(
		vattr_sp_handle *handle,
		Slapi_Entry *e,
		vattr_type_list_context *type_context,
		int flags
		);


static Slapi_PluginDesc pdesc = { "vattrsp", PLUGIN_MAGIC_VENDOR_STR, PRODUCTTEXT,
	"class of service plugin" };

static void * vattrsp_plugin_identity = NULL;


#ifdef _WIN32
int *module_ldap_debug = 0;

void plugin_init_debug_level(int *level_ptr)
{
	module_ldap_debug = level_ptr;
}
#endif

/*
 * Plugin identity mgmt
 * --------------------
 * Used for internal search api's
 */

void vattrsp_set_plugin_identity(void * identity) 
{
	vattrsp_plugin_identity=identity;
}

void * vattrsp_get_plugin_identity()
{
	return vattrsp_plugin_identity;
}

/* 
 * vattrsp_init
 * --------
 * adds our callbacks to the list
 * this is called even if the plugin is disabled
 * so do not do anything here which enables the
 * plugin functionality - also no other plugin has been started yet
 * so you cant use the functionality of other plugins here
 *
 * When does this get called?
 * At server start up.  This is the first function in
 * the plugin to get called, and no guarantees are made
 * about whether the init() function of other plugins
 * have been called.  It is really only safe to register
 * the standard SLAPI_PLUGIN_* interfaces here.
 *
 * returns 0 on success
*/
int vattrsp_init( Slapi_PBlock *pb )
{
	int ret = 0;
	void * plugin_identity=NULL;

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"--> vattrsp_init\n");

	/*
	 * Store the plugin identity for later use.
	 * Used for internal operations
	 */
	
   	slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
   	PR_ASSERT (plugin_identity);
	vattrsp_set_plugin_identity(plugin_identity);
	
	if (	slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    			SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
        	         (void *) vattrsp_start ) != 0 ||
	        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
        	         (void *) vattrsp_close ) != 0 ||
			slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
                     (void *)&pdesc ) != 0 )
    {
        slapi_log_error( SLAPI_LOG_FATAL, VATTRSP_PLUGIN_SUBSYSTEM,
                         "vattrsp_init: failed to register plugin\n" );
		ret = -1;
    }

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"<-- vattrsp_init\n");
    return ret;
}


/*
 * vattrsp_start
 * ---------
 * This is called after vattrsp_init, this is where plugin init starts.
 * Dependencies on other plugins have been satisfied so
 * feel free to use them e.g. statechange plugin to keep
 * an eye on configuration changes
 *
 * pb should contain SLAPI_TARGET_DN, which is the dn
 * of the entry representing this plugin, usually in
 * cn=plugins, cn=config.  Use this to get configuration
 * specific to your plugin from the entry
 *
 * When does this get called?
 * At server start up, after the vattrsp_init() function is
 * called and when the start function of all plugins this one
 * depends on have been called.  It is safe to rely
 * on dependencies from now on e.g. perform search operations
 *
 * returns 0 on success
*/
int vattrsp_start( Slapi_PBlock *pb )
{
	int ret = 0;

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"--> vattrsp_start\n");

	/* register this vattr service provider with vattr subsystem */
    if (slapi_vattrspi_register((vattr_sp_handle **)&vattr_handle, 
                                vattrsp_vattr_get, 
                                vattrsp_vattr_compare, 
                                vattrsp_vattr_types) != 0)
    {
		slapi_log_error( SLAPI_LOG_FATAL, VATTRSP_PLUGIN_SUBSYSTEM,
			   "vattrsp_start: cannot register as service provider\n" );
				ret = -1;
		goto out;
    }

	/* register a vattr */
	/* TODO: change dummyAttr to your attribute type,
	 * or write some configuration code which discovers
	 * the attributes to register
	 */
	slapi_vattrspi_regattr((vattr_sp_handle *)vattr_handle, "dummyAttr", NULL, NULL);

out:
	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"<-- vattrsp_start\n");
	return ret;
}

/*
 * vattrsp_close
 * ---------
 * closes down the plugin
 *
 * When does this get called?
 * On server shutdown
 *
 * returns 0 on success
*/
int vattrsp_close( Slapi_PBlock *pb )
{
	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"--> vattrsp_close\n");

	/* clean up stuff here */

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"<-- vattrsp_close\n");

	return 0;
}


/*
 * vattrsp_vattr_get
 * -----------------
 * vattr subsystem is requesting the value(s) for "type"
 *
 * When does this get called?
 * Whenever a client requests the attribute "type"
 * this may be indirectly by a request for all
 * attributes
 *
 * returns 0 on success
 */
int vattrsp_vattr_get(
	  vattr_sp_handle *handle, /* pass through to subsequent vattr calls */
	  vattr_context *c, /* opaque context, pass through to subsequent vattr calls */
	  Slapi_Entry *e, /* the entry that the values are for */
	  char *type, /* the type that the values are requested for */
	  Slapi_ValueSet** results, /* put the values here */
	  int *type_name_disposition, /* whether the type is a sub-type etc. */
	  char** actual_type_name, /* maybe you call this another type */
	  int flags, /* see slapi-plugin.h to support these flags */
	  int *free_flags, /* let vattr subsystem know if you supplied value copies it must free */
	  void *hint /* opaque hint, pass through to subsequent vattr calls */
	  )
{
	int ret = SLAPI_VIRTUALATTRS_NOT_FOUND;

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"--> vattrsp_vattr_get\n");

	/* usual to schema check an attribute
	 * there may be sanity checks which can
	 * be done prior to this "relatively"
	 * slow function to ensure least work done
	 * to fail
	 */
	if(!slapi_vattr_schema_check_type(e, type))
		return ret;

	/* TODO: do your thing, resolve the attribute */
	/* some vattr sps may look after more than one
	 * attribute, this one does not, so no need to
	 * check against "type", we know its our "dummyAttr"
	 */
	{
		/* TODO: replace this with your resolver */
		Slapi_Value *val = slapi_value_new_string("dummyValue");

		*results = slapi_valueset_new();
		slapi_valueset_add_value(*results, val);

		ret = 0;
	}

	if(ret == 0)
	{
		/* TODO: set *free_flags
		 * if allocated memory for values
		 * use: SLAPI_VIRTUALATTRS_RETURNED_COPIES
		 *
		 * otherwise, if you can guarantee that
		 * this value will live beyond this operation
		 * use: SLAPI_VIRTUALATTRS_RETURNED_POINTERS
		 */
        *free_flags = SLAPI_VIRTUALATTRS_RETURNED_COPIES;

        /* say the type is the same as the one requested
		 * could be your vattr needs to switch types, say to
		 * make one type look like another or something, but
		 * that is unusual
		 */
		*actual_type_name = slapi_ch_strdup(type);  
		
		/* TODO: set *type_name_disposition
		 * if the type matched exactly or it
		 * is an alias for the type then
		 * use: SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS
		 *
		 * otherwise, the actual_type_name is a subtype
		 * of type
		 * use: SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_SUBTYPE
		 */
		*type_name_disposition = SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS;
	}

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"<-- vattrsp_cache_vattr_get\n");
	return ret;
}



/*
 * vattrsp_vattr_compare
 * ---------------------
 * vattr subsystem is requesting that the values are compared
 *
 * When does this get called?
 * When an LDAP compare operation against this
 * type is requested by the client
 *
 * returns 0 on success
 */
int vattrsp_vattr_compare(
		vattr_sp_handle *handle, /* pass through to subsequent vattr calls */
		vattr_context *c, /* opaque context, pass through to subsequent vattr calls */
		Slapi_Entry *e, /* the entry that the values are for */
		char *type, /* the type that the values belong to  */
		Slapi_Value *test_this, /* the value to compare against */
		int* result, /* 1 for matched, 0 for not matched */
		int flags, /* see slapi-plugin.h to support these flags */
		void *hint /* opaque hint, pass through to subsequent vattr calls */
		)
{
	int ret = SLAPI_VIRTUALATTRS_NOT_FOUND; /* assume failure */

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"--> vattrsp_vattr_compare\n");
	
	/* TODO: do your thing, compare the attribute */

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"<-- vattrsp_vattr_compare\n");
	return ret;
}



/*
 * vattrsp_vattr_types
 * -------------------
 * vattr subsystem is requesting the types that you
 * promise to supply values for if it calls
 * vattrsp_vattr_get() with each type.
 * Guesses are not good enough, the wrong answer
 * effects what happens when all attributes
 * are requested in the LDAP search operation.
 * If you do not own up to an attribute,
 * vattrsp_vattr_get() will never get called for it.
 * If you say you will supply an attribute but
 * vattrsp_vattr_get() does not supply a value
 * then the attribute is returned, but *with* *no*
 * *values*
 *
 * When does this get called?
 * Only when all attributes are requested by the client
 * and just prior to multiple calls to vattrsp_vattr_get()
 *
 * returns 0 on success
 */
int vattrsp_vattr_types(
		vattr_sp_handle *handle, /* pass through to subsequent vattr calls */
		Slapi_Entry *e, /* the entry that the types should have values for */
		vattr_type_list_context *type_context, /* where we put the types */
		int flags /* see slapi-plugin.h to support these flags */
		)
{
	int ret = 0; /* assume success */
	char *attr = "dummyAttr"; /* an attribute type that we will deliver */
	int props = 0; /* properties of the attribute, make this SLAPI_ATTR_FLAG_OPATTR for operational attributes */

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"--> vattrsp_vattr_types\n");

	/* TODO: for each type you will supply... */
	if(ret)
	{
		/* ...do this */

		/* entry contains this attr */
		vattr_type_thang thang = {0};

		thang.type_name = strcpy(thang.type_name,attr);
		thang.type_flags = props;

		/* add the type to the type context */
		slapi_vattrspi_add_type(type_context,&thang,0);
	}

	slapi_log_error( SLAPI_LOG_TRACE, VATTRSP_PLUGIN_SUBSYSTEM,"<-- vattrsp_vattr_types\n");
	return ret;
}



