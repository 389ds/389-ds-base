/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/**
 * IM Presence plug-in 
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "portable.h"
#include "nspr.h"
#include "slapi-plugin.h"
#include "slapi-private.h"
#include "dirlite_strings.h"
#include "dirver.h"
#include "vattr_spi.h"
#include "plhash.h"
#include "ldif.h"

#include "http_client.h"

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
#      define LDAPDebugLevelIsSet( level )	( *module_ldap_debug & level )
#    else /* _WIN32 */
       extern int	slapd_ldap_debug;
#      define LDAPDebugLevelIsSet( level )	( slapd_ldap_debug & level )
#    endif /* Win32 */

#define LDAPDebug( level, fmt, arg1, arg2, arg3 )	\
       { \
		if ( LDAPDebugLevelIsSet( level )) { \
		        slapd_log_error_proc( NULL, fmt, arg1, arg2, arg3 ); \
	    } \
       }

#ifdef __cplusplus
}
#endif

#endif /* _LDAP_H */

#define PRESENCE_PLUGIN_SUBSYSTEM			"presence-plugin"
#define PRESENCE_PLUGIN_VERSION				0x00050050

/**
 * this may become unnecessary when we are able to get 
 * the plug-in DN dynamically (pete?)
 */
#define PRESENCE_DN							"cn=Presence,cn=plugins,cn=config" /* temporary */

#define PRESENCE_SUCCESS					0
#define PRESENCE_FAILURE					-1

/**
 * Presence vendor specific config parameters
 */

#define NS_IM_ID							"nsIM-ID"

#define NS_IM_URL_TEXT						"nsIM-URLText"
#define NS_IM_URL_GRAPHIC					"nsIM-URLGraphic"

#define NS_IM_ON_VALUE_MAP_TEXT				"nsIM-OnValueMapText"
#define NS_IM_OFF_VALUE_MAP_TEXT			"nsIM-OffValueMapText"

#define NS_IM_ON_VALUE_MAP_GRAPHIC			"nsIM-OnValueMapGraphic"
#define NS_IM_OFF_VALUE_MAP_GRAPHIC			"nsIM-OffValueMapGraphic"
#define NS_IM_DISABLED_VALUE_MAP_GRAPHIC	"nsIM-disabledValueMapGraphic"

#define NS_IM_REQUEST_METHOD				"nsIM-RequestMethod"

#define NS_IM_URL_TEXT_RETURN_TYPE			"nsIM-URLTextReturnType"
#define NS_IM_URL_GRAPHIC_RETURN_TYPE		"nsIM-URLGraphicReturnType"

#define NS_IM_STATUS_TEXT					"nsIM-StatusText"
#define NS_IM_STATUS_GRAPHIC				"nsIM-StatusGraphic"

#define PRESENCE_STRING						1
#define PRESENCE_BINARY						2

#define PRESENCE_TEXT_RETURN_TYPE			"TEXT"
#define PRESENCE_BINARY_RETURN_TYPE			"BINARY"

#define PRESENCE_REQUEST_METHOD_GET			"GET"
#define PRESENCE_REQUEST_METHOD_REDIRECT	"REDIRECT"

#define PRESENCE_RETURNED_ON_TEXT			"ONLINE"
#define PRESENCE_RETURNED_OFF_TEXT			"OFFLINE"
#define PRESENCE_RETURNED_ERROR_TEXT		"ERROR"

static Slapi_PluginDesc pdesc = { "IM Presence",
								  PLUGIN_MAGIC_VENDOR_STR,
								  PRODUCTTEXT,
	"presence plugin" };

/**
 * struct used to pass the argument to PL_Enumerator Callback
 */
struct _vattrtypes
{
	Slapi_Entry *entry;
	vattr_type_list_context *context;
};

/**
 * This structure holds the mapping between the virtual attributes and 
 * the IM IDs. This information is used to find out whether this plugin
 * should service the attributes it was asked to. Also, it stores the 
 * syntax of the attribute. 1 is String and 2 is binary.
 */
struct _vattrmap {
	char *imID;
	int syntax;
};
typedef struct _vattrmap _Vmap;

/**
 * struct to store the config values for each presence vendor
 */
struct _defs {
	char *textURL;
	char *graphicURL;
	char *onTextMap;
	char *offTextMap;
	Slapi_Attr *onGraphicMap;
	Slapi_Attr *offGraphicMap;
	Slapi_Attr *disabledGraphicMap;
	char *requestMethod;
	char *textReturnType;
	char *graphicReturnType;
};
typedef struct _defs _ConfigEntry;

static vattr_sp_handle *_VattrHandle	= NULL;
static void *_PluginID					= NULL;
static void *_PluginDN					= NULL;
static PLHashTable *_IdVattrMapTable	= NULL;
static PLHashTable *_IdConfigMapTable	= NULL;
static void **_HttpAPI					= NULL;

/**
 *	
 * Presence plug-in management functions
 *
 */
int presence_init(Slapi_PBlock *pb); 
int presence_start(Slapi_PBlock *pb);
int presence_close(Slapi_PBlock *pb);

/**
 *	
 * Vattr operation callbacks functions
 *
 */
static int presence_vattr_get(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_ValueSet** results,int *type_name_disposition, char** actual_type_name, int flags, int *free_flags, void *hint);
static int presence_vattr_compare(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_Value *test_this, int* result, int flags, void *hint);
static int presence_vattr_types(vattr_sp_handle *handle,Slapi_Entry *e,vattr_type_list_context *type_context,int flags);

/**
 *	
 * Local operation functions
 *
 */
static int loadPluginConfig();
static int parseConfigEntry(Slapi_Entry *e);
static int imIDExists(Slapi_Entry *e, char *type, char **value, _Vmap **map, _ConfigEntry **entry);
static int makeHttpRequest(char *id, _Vmap *map, _ConfigEntry *info, char **buf, int *size);
static char * replaceIdWithValue(char *str, char *id, char *value);
static int setIMStatus(char *id, _Vmap *map, _ConfigEntry *info, char *returnedBUF, int size, Slapi_ValueSet **results);
static int setTypes(PLHashEntry *he, PRIntn i, void *arg);

static void deleteMapTables();
static PRIntn destroyHashEntry(PLHashEntry *he, PRIntn index, void *arg);
static void logGraphicAttributeValue( Slapi_Attr *attr, const char *attrname );
static void toLowerCase(char* str);

/**
 * utility function
 */
void printMapTable();
PRIntn printIdVattrMapTable(PLHashEntry *he, PRIntn i, void *arg);
PRIntn printIdConfigMapTable(PLHashEntry *he, PRIntn i, void *arg);

/**
 * set the debug level
 */
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
int presence_version()
{
	return PRESENCE_PLUGIN_VERSION;
}

/**
 * Plugin identity mgmt
 */
void setPluginID(void * pluginID) 
{
	_PluginID=pluginID;
}

void * getPluginID()
{
	return _PluginID;
}

void setPluginDN(void *pluginDN)
{
	_PluginDN = pluginDN;
}

void * getPluginDN()
{
	return _PluginDN;
}

/* 
	presence_init
	-------------
	adds our callbacks to the list
*/
int presence_init( Slapi_PBlock *pb )
{
	int status = PRESENCE_SUCCESS;
	char * plugin_identity=NULL;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> presence_init -- BEGIN\n",0,0,0);

	/**
	 * Store the plugin identity for later use.
	 * Used for internal operations
	 */
	
    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT (plugin_identity);
	setPluginID(plugin_identity);

	if ( slapi_pblock_set( pb, SLAPI_PLUGIN_VERSION,
	    	SLAPI_PLUGIN_VERSION_01 ) != 0 ||
	    slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
        	     (void *) presence_start ) != 0 ||
	    slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
        	     (void *) presence_close ) != 0 ||
		slapi_pblock_set( pb, SLAPI_PLUGIN_DESCRIPTION,
             (void *)&pdesc ) != 0 )
	{
		slapi_log_error( SLAPI_LOG_FATAL, PRESENCE_PLUGIN_SUBSYSTEM,
                     "presence_init: failed to register plugin\n" );
		status = PRESENCE_FAILURE;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- presence_init -- END\n",0,0,0);
    return status;
}

/*
	presence_start
	--------------
	This function registers the computed attribute evaluator
	and loads the configuration parameters in the local cache.
	It is called after presence_init.
*/
int presence_start( Slapi_PBlock *pb )
{
	char * plugindn = NULL;
	char * httpRootDir = NULL;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> presence_start -- begin\n",0,0,0);

    if(slapi_apib_get_interface(HTTP_v1_0_GUID, &_HttpAPI))
	{
	  /**
	   * error cannot proceeed
	   */
		return PRESENCE_FAILURE;
	}

	/**
	 * register our vattr callbacks 
	 */
    if (slapi_vattrspi_register((vattr_sp_handle **)&_VattrHandle, 
                                presence_vattr_get, 
                                presence_vattr_compare, 
                                presence_vattr_types) != 0)
    {
		slapi_log_error( SLAPI_LOG_FATAL, PRESENCE_PLUGIN_SUBSYSTEM,
		   "presence_start: cannot register as service provider\n" );
		return PRESENCE_FAILURE;
    }

	/**
	 *	Get the plug-in target dn from the system
	 *	and store it for future use. This should avoid 
	 *	hardcoding of DN's in the code. 
	 */
	slapi_pblock_get(pb, SLAPI_TARGET_DN, &plugindn);
	if (plugindn == NULL || strlen(plugindn) == 0)
	{
	  /**
	   * This is not required as the above statement
	   * should work and give you a valid DN for this
	   * plugin. ??? remove it later
	   */
		plugindn = PRESENCE_DN;
	}
	setPluginDN(plugindn);	

	/**
	 * Load the config info for our plug-in in memory
	 * In the 6.0 release this information will be stored 
	 * statically and if any change is done to this info a server
	 * restart is necessary :-(. Probably if time permits then 
	 * state change plug-in would be used to notify the state 
	 * change. We also register the virtual attributes we are
	 * interested in here.
	 */
	if (loadPluginConfig() != PRESENCE_SUCCESS)
	{
		slapi_log_error( SLAPI_LOG_FATAL, PRESENCE_PLUGIN_SUBSYSTEM,
    	   "presence_start: unable to load plug-in configuration\n" );
		return PRESENCE_FAILURE;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "presence: ready for service\n",0,0,0);
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- presence_start -- end\n",0,0,0);

	return PRESENCE_SUCCESS;
}

/*
	presence_close
	--------------
	closes down the cache
*/
int presence_close( Slapi_PBlock *pb )
{
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> presence_close\n",0,0,0);

	deleteMapTables();

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- presence_close\n",0,0,0);

	return PRESENCE_SUCCESS;
}

static int presence_vattr_get(vattr_sp_handle *handle, 
							  vattr_context *c, 
							  Slapi_Entry *e, 
							  char *type, 
							  Slapi_ValueSet** results,
							  int *type_name_disposition, 
							  char** actual_type_name, 
							  int flags, 
							  int *free_flags, 
							  void *hint)
{

	int status = PRESENCE_SUCCESS;
	char *id = NULL;
	char *returnedBUF = NULL;
	int size = 0;
	_Vmap *map = NULL;
	_ConfigEntry *info = NULL;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> presence_vattr_get \n",0,0,0);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Type=[%s] \n",type,0,0);

	if (imIDExists(e, type, &id, &map, &info) != PRESENCE_SUCCESS)
	{
	  /**
	   * we didn't find any valid matching nsimid in this
	   * entry so since we cannot process a request without
	   * a valid nsimid we just return.
	   */
		status = PRESENCE_FAILURE;		
		goto cleanup;
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> ID=[%s] \n",id,0,0);

	/**
	 * Now since we got a valid id we do a quick schema check
	 * if schema checking is on to make sure that there is no
	 * schema violation ?
	 */ 
	/* do_schema_check() */

	/**
	 * At this stage we have a valid attribute and we have to 
	 * get its value from the IM Server. so make an Http request
	 * depending on whether it is a request for Text or Graphic
	 */

	status = makeHttpRequest(id, map, info, &returnedBUF, &size);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> size=[%d] \n",size,0,0);
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> buffer=[%s]\n",(returnedBUF) ? returnedBUF : "NULL",0,0);


	if(status == PRESENCE_SUCCESS)
	{
		status = setIMStatus(id, map, info, returnedBUF, size, results);
	}
	else
	{
		/**
		 * Report all HTTP failures as a single predefined value of the
		 * attribute
		 */
		Slapi_Value *value =
			slapi_value_new_string(PRESENCE_RETURNED_ERROR_TEXT);
		if (!*results) {
			*results = slapi_valueset_new();
		}
		slapi_valueset_add_value(*results, value);
		slapi_value_free(&value); /* slapi_valueset_add_value copies value */
		/**
		 * It's a success only in the sense that we are returning a value
		 */
		status = PRESENCE_SUCCESS;
	}
	if(status == PRESENCE_SUCCESS)
	{
        *free_flags = SLAPI_VIRTUALATTRS_RETURNED_COPIES;
        *actual_type_name = slapi_ch_strdup(type);
		*type_name_disposition = SLAPI_VIRTUALATTRS_TYPE_NAME_MATCHED_EXACTLY_OR_ALIAS;
	}
	
cleanup:
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Processed ID=[%s] \n",id,0,0);
	if (id != NULL ) {
		slapi_ch_free((void **)&id);
	}
	if (returnedBUF != NULL ) {
		PR_Free(returnedBUF);
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- presence_vattr_get \n",0,0,0);
	return status;
}


static int presence_vattr_compare(vattr_sp_handle *handle, vattr_context *c, Slapi_Entry *e, char *type, Slapi_Value *test_this, int* result, int flags, void *hint)
{
	int status = PRESENCE_SUCCESS;
	/**
	 * not yet implemented ???
	 */
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> presence_vattr_compare \n",0,0,0);
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- presence_vattr_compare \n",0,0,0);

	return status;
}

static int presence_vattr_types(vattr_sp_handle *handle,Slapi_Entry *e,vattr_type_list_context *type_context,int flags)
{
	int status = PRESENCE_SUCCESS;
	struct _vattrtypes args;
	args.entry = e;
	args.context = type_context;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> presence_vattr_types\n",0,0,0);
	
	PL_HashTableEnumerateEntries(_IdVattrMapTable, setTypes, &args);	

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- presence_vattr_types\n",0,0,0);
	return status;
}

static int loadPluginConfig()
{
	int status = PRESENCE_SUCCESS;
	int result;
	int i;
	Slapi_PBlock *search_pb;
    Slapi_Entry **entries = NULL;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> loadPluginConfig\n",0,0,0);

    search_pb = slapi_pblock_new();

    slapi_search_internal_set_pb(search_pb, PRESENCE_DN, LDAP_SCOPE_ONELEVEL,
            "objectclass=*", NULL, 0, NULL, NULL, getPluginID(), 0);
    slapi_search_internal_pb(search_pb);
    slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_RESULT, &result);

	if (status != PRESENCE_SUCCESS)
	{
        slapi_log_error(SLAPI_LOG_FATAL, PRESENCE_PLUGIN_SUBSYSTEM, 
			"Error getting level1 presence configurations<%s>\n", getPluginDN());
		status = PRESENCE_FAILURE;
		goto cleanup;
	}
	
	slapi_pblock_get(search_pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
	if (NULL == entries || entries[0] == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, PRESENCE_PLUGIN_SUBSYSTEM, 
			"No entries found for <%s>\n", getPluginDN());

		status = PRESENCE_FAILURE;
		goto cleanup;
	}

	_IdVattrMapTable = PL_NewHashTable(	0,
										PL_HashString,
										PL_CompareStrings,
										PL_CompareValues,
										NULL,
										NULL
										);

	_IdConfigMapTable = PL_NewHashTable(0,
										PL_HashString,
										PL_CompareStrings,
										PL_CompareValues,
										NULL,
										NULL
										);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> parseConfigEntry \n",0,0,0);

	for (i = 0; (entries[i] != NULL); i++)
	{
		status = parseConfigEntry(entries[i]);
		if (status != PRESENCE_SUCCESS)
		{
			deleteMapTables();
			goto cleanup;
		}
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- parseConfigEntry \n",0,0,0);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- loadPluginConfig\n",0,0,0);

cleanup:
    slapi_free_search_results_internal(search_pb);
    slapi_pblock_destroy(search_pb);
	return status;
}

static int parseConfigEntry(Slapi_Entry *e)
{
	char *key			= NULL;
	char *value			= NULL;
	_ConfigEntry *entry	= NULL;
	_Vmap *map			= NULL;
	Slapi_Attr *attr	= NULL;

	key = slapi_entry_attr_get_charptr(e, NS_IM_ID);
	if (!key) {
	  /**
	   * IM Id not defined in the config, unfortunately
	   * cannot do anything without it so better not to
	   * load the plug-in.
	   */
		return PRESENCE_FAILURE;
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> key [%s] \n",key,0,0);
	/**
	 * Now create the config entry which will hold all the 
	 * attributes of a presence vendor
	 */
	entry = (_ConfigEntry*) slapi_ch_calloc(1, sizeof(_ConfigEntry));

	/**
	 * Next 2 are the virtual attributes for which this plug-in
	 * is responsible. Register them with the vattr system so 
	 * that the system can call us whenever their
	 * values are requested. Also update these entries in the 
	 * map table for later access.
	 */ 
	value = slapi_entry_attr_get_charptr(e, NS_IM_STATUS_TEXT);
	if (value) {
		slapi_vattrspi_regattr(_VattrHandle, value, "", NULL);
		map = (_Vmap*) slapi_ch_calloc(1, sizeof(_Vmap));
		map->imID = key;
		map->syntax = PRESENCE_STRING;
		toLowerCase(value);
		PL_HashTableAdd(_IdVattrMapTable, value, map);
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMStatusText [%s] \n",value,0,0);

	value = slapi_entry_attr_get_charptr(e, NS_IM_STATUS_GRAPHIC);
	if (value) {
		slapi_vattrspi_regattr(_VattrHandle, value, "", NULL);
		map = (_Vmap*) slapi_ch_calloc(1, sizeof(_Vmap));
		map->imID = key;
		map->syntax = PRESENCE_BINARY;
		toLowerCase(value);
		PL_HashTableAdd(_IdVattrMapTable, value, map);
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMStatusGraphic [%s] \n",value,0,0);

	value = slapi_entry_attr_get_charptr(e, NS_IM_URL_TEXT);
	if (value) {
		entry->textURL = value;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMURLText [%s] \n",value,0,0);

	value = slapi_entry_attr_get_charptr(e, NS_IM_URL_GRAPHIC);
	if (value) {
		entry->graphicURL = value;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMStatusGraphic [%s] \n",value,0,0);

	value = slapi_entry_attr_get_charptr(e, NS_IM_ON_VALUE_MAP_TEXT);
	if (value) {
		entry->onTextMap = value;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMOnValueMapText [%s] \n",value,0,0);

	value = slapi_entry_attr_get_charptr(e, NS_IM_OFF_VALUE_MAP_TEXT);
	if (value) {
		entry->offTextMap = value;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMOffValueMapText [%s] \n",value,0,0);

	/**
	 * Next 3 are binary syntax types so needs special handling
	 */
	slapi_entry_attr_find(e, NS_IM_ON_VALUE_MAP_GRAPHIC, &attr);
	if (attr) {
		entry->onGraphicMap = slapi_attr_dup(attr);
		logGraphicAttributeValue(attr,NS_IM_ON_VALUE_MAP_GRAPHIC);
	}

	slapi_entry_attr_find(e, NS_IM_OFF_VALUE_MAP_GRAPHIC, &attr);
	if (attr) {
		entry->offGraphicMap = slapi_attr_dup(attr);
		logGraphicAttributeValue(attr,NS_IM_OFF_VALUE_MAP_GRAPHIC);
	}

	slapi_entry_attr_find(e, NS_IM_DISABLED_VALUE_MAP_GRAPHIC, &attr);
	if (attr) {
		entry->disabledGraphicMap = slapi_attr_dup(attr);
		logGraphicAttributeValue(attr,NS_IM_DISABLED_VALUE_MAP_GRAPHIC);
	}

	value = slapi_entry_attr_get_charptr(e, NS_IM_REQUEST_METHOD);
	if (value) {
		entry->requestMethod = value;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMRequestMethod [%s] \n",value,0,0);

	value = slapi_entry_attr_get_charptr(e, NS_IM_URL_TEXT_RETURN_TYPE);
	if (value) {
		entry->textReturnType = value;
	}

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMURLTextReturnType [%s] \n",value,0,0);

	value = slapi_entry_attr_get_charptr(e, NS_IM_URL_GRAPHIC_RETURN_TYPE);
	if (value) {
		entry->graphicReturnType = value;
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> nsIMURLGraphicReturnType [%s] \n",value,0,0);

	/**
	 * Finally add the entry to the map table
	 */
	PL_HashTableAdd(_IdConfigMapTable, key, entry);

	return PRESENCE_SUCCESS;
}

/**
 * this function goes thru the valid stored ids
 * and return the correct one for which we have to 
 * do further processing
 */
static int imIDExists(Slapi_Entry *e, char *type, char **value, _Vmap **map, _ConfigEntry **entry)
{
	int status = PRESENCE_SUCCESS;
	char *tValue = NULL;
	_ConfigEntry *tEntry = NULL;
	_Vmap *tMap = NULL;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> imIDExists \n",0,0,0);	
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Type [%s] \n",type,0,0);	

	/**
	 * The public function PL_HashTableLookup modifies the 
	 * the table while reading. so using this private function
	 * which just does a lookup and doesn't modifies the 
	 * hashtable
	 */
	toLowerCase(type);
	tMap = PL_HashTableLookupConst(_IdVattrMapTable, type);
	if (!tMap)
	{
	  /**
	   * this should not happen but no harm we just return
	   */
		status = PRESENCE_FAILURE;
		slapi_log_error(SLAPI_LOG_FATAL, PRESENCE_PLUGIN_SUBSYSTEM, 
			"No hashtable for vattr types\n");
		goto bail;
	}
	/**
	 * We found a matching id in the map table 
	 * now see if that id exists in the Slapi_Entry
	 */
	tValue = slapi_entry_attr_get_charptr(e, tMap->imID);
	if (!tValue)
	{
	  /**
	   * we don't do anything here but just return
	   */
		status = PRESENCE_FAILURE;
		goto bail;
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Value [%s] \n",tValue,0,0);	

	tEntry = PL_HashTableLookupConst(_IdConfigMapTable, tMap->imID);
	*value	= tValue;
	*entry	= tEntry;
	*map	= tMap;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- imIDExists \n",0,0,0);	

bail:
	return status;
}

static int makeHttpRequest(char *id, _Vmap *map, _ConfigEntry *info, char **BUF, int *size)
{
	int status = PRESENCE_SUCCESS;
	char *buf = NULL;
	char *url = NULL;
	char *urltosend = NULL;
	int bytesRead;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> makeHttpRequest:: \n",0,0,0);

	if (map->syntax == PRESENCE_STRING) {
		url = info->textURL;
	} else {
		url = info->graphicURL;
	}
	if (url == NULL) {
		status = PRESENCE_FAILURE;
		goto bail;
	}
	urltosend = replaceIdWithValue(url, map->imID, id);

	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> URL [%s] \n",urltosend,0,0);
	/**
	 * make an actual HTTP call now
	 */
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> RequestMethod [%s] \n", info->requestMethod,0,0);
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Syntax [%d] \n", map->syntax,0,0);
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> TextReturnType [%s] \n", info->textReturnType,0,0);
	LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> GraphicReturnType [%s] \n", info->graphicReturnType,0,0);
	if (!strcasecmp(info->requestMethod, PRESENCE_REQUEST_METHOD_GET)) {
		if (map->syntax == PRESENCE_STRING) {
			if (!strcasecmp(info->textReturnType, PRESENCE_TEXT_RETURN_TYPE)) {
				status = http_get_text(_HttpAPI, urltosend, &buf, &bytesRead);
			} else {
				status = http_get_binary(_HttpAPI, urltosend, &buf, &bytesRead);
			}
		} else {
			if (!strcasecmp(info->graphicReturnType, PRESENCE_TEXT_RETURN_TYPE)) {
				status = http_get_text(_HttpAPI, urltosend, &buf, &bytesRead);
			} else {
				status = http_get_binary(_HttpAPI, urltosend, &buf, &bytesRead);
			}
		}
	} else if (!strcasecmp(info->requestMethod, PRESENCE_REQUEST_METHOD_REDIRECT)) {
		status = http_get_redirected_uri(_HttpAPI, urltosend, &buf, &bytesRead);
	} else {
	  /**
	   * error : unknown method
	   * probably we should check at the time of loading
	   * of the plugin itself that the config values are 
	   * properly checked and throw warning/errors in case
	   * of any invalid entry
	   */
		slapi_log_error(SLAPI_LOG_FATAL, PRESENCE_PLUGIN_SUBSYSTEM, 
			"Unknown request type <%s>\n", info->requestMethod);
		status = PRESENCE_FAILURE;
		goto bail;
	}
	if (buf && status == PRESENCE_SUCCESS)
	{
		*BUF = buf;
		*size = bytesRead;
	}

bail:
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- makeHttpRequest:: <%d>\n",status,0,0);

	slapi_ch_free((void**)&urltosend);
	return status;
}

/**
 * This function replaces the occurrence of $ns[<vendor>]imid with its
 * actual value 
 * e.g. 
 * URL : http://opi.yahoo.com/online?u=$nsyimid
 * after replacing
 * newURL : http://opi.yahoo.com/online?u=srajam
 */
static char * replaceIdWithValue(char *str, char *id, char *value)
{
	int i=0;
	int k=0;
	char *newstr = NULL;
	char c;
	if (!str || !id || !value)
	{
		return NULL;
	}
	/* extra space for userids */
	newstr = (char *)slapi_ch_malloc(strlen(str) + strlen(value));
	while ((c=str[i]) != '\0')
	{
		if (c == '$')
		{
			int j = 0;
			i++; /*skip one char */
			/**
			 * we found the begining of the string to be 
			 * substituted. Now skip the chars we want to replace
			 */
			while (str[i] != '\0' && id[j] != '\0' && 
				   (toupper(str[i]) == toupper(id[j])))
			{
				i++;
				j++;
			}
			j=0;
			while (value[j] != '\0')
			{
				newstr[k++] = value[j++];
			}
		}
		else
		{
			newstr[k++]=c;
			i++;
		}
	}

	newstr[k] = '\0';
	return newstr;
}

static int setIMStatus(char *id, _Vmap *map, _ConfigEntry *info, 
					   char *returnedBUF, int size, Slapi_ValueSet **results)
{
	int status = PRESENCE_SUCCESS;
	char *ontxtmap = NULL;
	char *offtxtmap = NULL;
	Slapi_Value *value = NULL;
	Slapi_Value *value1 = NULL;
	Slapi_Value *value2 = NULL;
	struct berval bval;
	Slapi_Attr *attr = NULL;
	const struct berval *tmp = NULL;
	
	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> setIMStatus \n", 0,0,0);
	/**
	 * we got some data back so lets try to map it to 
	 * the existing set of on/off data
	 *
	 * first we need to take a look at the 
	 * returned type and depending upon that parse
	 * the data
	 */

	if (map->syntax == PRESENCE_STRING)	{
	  /**
	   * we had send a request for text
	   * but chances are we might end up 
	   * getting an image back. So we need
	   * to compare it to existing set of 
	   * images that we have in store ???
	   */
		if (!strcasecmp(info->textReturnType, PRESENCE_TEXT_RETURN_TYPE)) {
		  /* return value is in text format */
			ontxtmap = replaceIdWithValue(info->onTextMap, map->imID, id);
			offtxtmap = replaceIdWithValue(info->offTextMap, map->imID, id);
			if (!strcasecmp(ontxtmap, returnedBUF)) {
			  /**
			   * set the on value
			   */
				value = slapi_value_new_string(PRESENCE_RETURNED_ON_TEXT);
			} else if (!strcasecmp(offtxtmap, returnedBUF))	{
			  /**
			   * set the off value
			   */
				value = slapi_value_new_string(PRESENCE_RETURNED_OFF_TEXT);
			} else {
				value = slapi_value_new_string(PRESENCE_RETURNED_ERROR_TEXT);
			}
		} else if (!strcasecmp(info->textReturnType, PRESENCE_BINARY_RETURN_TYPE)) {
		  /**
		   * call binary compare method
		   */
			bval.bv_len = size;
			bval.bv_val = returnedBUF;
			value1 = slapi_value_new_berval(&bval);

			LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> returned size  [%d] \n", bval.bv_len,0,0);
			LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> returned value [%s] \n", bval.bv_val,0,0);
			
			attr = info->onGraphicMap;
			if (attr) {
				slapi_attr_first_value(attr, &value2);
				tmp = slapi_value_get_berval(value2);
				LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Stored size  [%d] \n", tmp->bv_len,0,0);
				LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> Stored value [%s] \n", tmp->bv_val,0,0);
				if (!slapi_value_compare(attr, value1, value2)) {
					value = slapi_value_new_string(PRESENCE_RETURNED_ON_TEXT);
				}
			}
			if (!value) {
				attr = info->offGraphicMap;
				if (attr) {
					slapi_attr_first_value(attr, &value2);
					if (!slapi_value_compare(attr, value1, value2)) {
						value = slapi_value_new_string(PRESENCE_RETURNED_OFF_TEXT);
					}
				}
			}
			if (!value) {
				attr = info->disabledGraphicMap;
				if (attr) {
					slapi_attr_first_value(attr, &value2);
					if (!slapi_value_compare(attr, value1, value2)) {
						value = slapi_value_new_string(PRESENCE_RETURNED_OFF_TEXT);
					}
				}
			}
			if (!value) {
			  /* some error */
				value = slapi_value_new_string(PRESENCE_RETURNED_ERROR_TEXT);
			}
		} else {
		  /**
		   * set the error condition
		   */
			value = slapi_value_new_string(PRESENCE_RETURNED_ERROR_TEXT);
		}
		LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> value [%s] \n", returnedBUF,0,0);
	} else {
	  /**
		* we had send a request for image
		* so whatever we get back we just 
		* return instead of analyzing it
		*/
		if (!strcasecmp(info->graphicReturnType, PRESENCE_TEXT_RETURN_TYPE)) {
			LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> value [%s] \n", returnedBUF,0,0);
			if (!strcasecmp(info->requestMethod, PRESENCE_REQUEST_METHOD_REDIRECT)) {
			  /**
			   * a redirect case in which we should probably have a 
			   * gif in store so return that value
			   *
			   * for now					
			   */

				ontxtmap = replaceIdWithValue(info->onTextMap, map->imID, id);
				offtxtmap = replaceIdWithValue(info->offTextMap, map->imID, id);
				if (!strcasecmp(ontxtmap, returnedBUF)) {
				  /**
				   * set the on value
				   */
					attr = info->onGraphicMap;
				} else if (!strcasecmp(offtxtmap, returnedBUF))	{
				  /**
				   * set the off value
				   */
					attr = info->offGraphicMap;
				} else {
					attr = info->disabledGraphicMap;
				}
				if (attr) {
					slapi_attr_first_value(attr, &value);
				}
			} else {
			  /**
			   * for now just set the returned value
			   * should not happen in our case
			   * ERROR
			   */
			}
		} else {
			LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> value [%s] \n", returnedBUF,0,0);
			bval.bv_len = size;
			bval.bv_val = returnedBUF;
			value = slapi_value_new_berval(&bval);
		}
	}
	if (!*results) {
		*results = slapi_valueset_new();
	}

	slapi_valueset_add_value(*results, value);

	if (ontxtmap) {
		slapi_ch_free((void **)&ontxtmap);
	} 
	if (offtxtmap) {
		slapi_ch_free((void **)&offtxtmap);
	}
	if (value && map->syntax == PRESENCE_STRING) {
		slapi_value_free(&value);
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- setIMStatus \n", 0,0,0);
	
	return status;
}

static int setTypes(PLHashEntry *he, PRIntn i, void *arg)
{
	int status;
	int props = SLAPI_ATTR_FLAG_OPATTR;
	Slapi_Attr *attr = NULL;
	Slapi_ValueSet *results = NULL;
	int type_name_disposition = 0;
	char *actual_type_name = 0;
	int free_flags = 0;

	struct _vattrtypes *args = arg;
	char *type = (char *)he->key;
	_Vmap *map  = (_Vmap *)he->value;
	char *id = map->imID;

	LDAPDebug( LDAP_DEBUG_PLUGIN, "--> setTypes \n", 0,0,0);

	status = slapi_vattr_values_get_sp(NULL, args->entry, id, &results, &type_name_disposition, &actual_type_name, 0, &free_flags);
	if(status == PRESENCE_SUCCESS)
	{
		/* entry contains this attr */
		vattr_type_thang thang = {0};

		thang.type_name = type;
		thang.type_flags = props;

		slapi_vattrspi_add_type(args->context,&thang,0);

		slapi_vattr_values_free(&results, &actual_type_name, free_flags);

		LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> ID [%s] Type[%s]\n", actual_type_name,type,0);
	}
	LDAPDebug( LDAP_DEBUG_PLUGIN, "<-- setTypes \n", 0,0,0);

	return HT_ENUMERATE_NEXT;
}


static void
logGraphicAttributeValue( Slapi_Attr *attr, const char *attrname )
{
	Slapi_Value			*val = NULL;
	const struct berval	*v = NULL;

	if ( LDAPDebugLevelIsSet( LDAP_DEBUG_PLUGIN )) {
		slapi_attr_first_value(attr, &val);
		v = slapi_value_get_berval(val);
		if (v) {
			char	*ldifvalue;
			size_t	attrnamelen = strlen( attrname );

			LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> %s size [%d] \n",
					attrname,v->bv_len,0);

			ldifvalue = ldif_type_and_value_with_options(
					(char *)attrname,	/* XXX: had to cast away const */
					v->bv_val, v->bv_len, 0 );
			if ( NULL != ldifvalue ) {
				LDAPDebug( LDAP_DEBUG_PLUGIN, "----------> %s value [\n%s]\n",
						attrname,ldifvalue,0);
				slapi_ch_free_string( &ldifvalue );
			}
		}
	}
}


static void deleteMapTables()
{
	PL_HashTableEnumerateEntries(_IdConfigMapTable, destroyHashEntry, 0);
	if (_IdConfigMapTable)
	{
		PL_HashTableDestroy(_IdConfigMapTable);
	}

	PL_HashTableEnumerateEntries(_IdVattrMapTable, destroyHashEntry, 0);
	if (_IdVattrMapTable)
	{
		PL_HashTableDestroy(_IdVattrMapTable);
	}
	return;
}

static PRIntn destroyHashEntry(PLHashEntry *he, PRIntn index, void *arg)
{
	void *value = NULL;
    if (he == NULL)
	{
        return HT_ENUMERATE_NEXT;
	}
	value = he->value;
    if (value)
	{
	    slapi_ch_free(&value);
	}
    return HT_ENUMERATE_REMOVE;
}

static void toLowerCase(char* str)
{
    if (str) {
        char* lstr = str;
        for(; (*lstr != '\0'); ++lstr) {
            *lstr = tolower(*lstr);
        }
    }
}


/**
 * utility function to print the array
 */
void printMapTable()
{
	PL_HashTableEnumerateEntries(_IdVattrMapTable, printIdVattrMapTable, 0);				
	PL_HashTableEnumerateEntries(_IdConfigMapTable, printIdConfigMapTable, 0);				
}

PRIntn printIdVattrMapTable(PLHashEntry *he, PRIntn i, void *arg)
{
	char *key	= (char *)he->key;
	_Vmap *map  = (_Vmap *)he->value;
	printf("<---- Key -------> %s\n", key);
	printf("<---- ImId ------> %s\n", map->imID);
	printf("<---- syntax ----> %d\n", map->syntax);
	return HT_ENUMERATE_NEXT;
}

PRIntn printIdConfigMapTable(PLHashEntry *he, PRIntn i, void *arg)
{
	char *key = (char *)he->key;
	_ConfigEntry *value = (_ConfigEntry *)he->value;
	printf("<- Key ---------------------> %s\n", key);
	printf("<---- text_url -------------> %s\n", value->textURL);
	printf("<---- graphic_url ----------> %s\n", value->graphicURL);
	printf("<---- on_text_map ----------> %s\n", value->onTextMap);
	printf("<---- off_text_map ---------> %s\n", value->offTextMap);
	printf("<---- request_method -------> %s\n", value->requestMethod);
	printf("<---- text_return_type -----> %s\n", value->textReturnType);
	printf("<---- graphic_return_type --> %s\n", value->graphicReturnType);

	return HT_ENUMERATE_NEXT;
}

