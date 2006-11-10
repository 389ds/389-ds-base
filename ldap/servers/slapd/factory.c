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
 * provide this exception without modification, you must delete this exception
 * statement from your version and license this file solely under the GPL without
 * exception. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include "slap.h"

/*
 * This module provides a mechanism for extending core server objects.
 * This functionality is provided to plugin writers so that they may
 * efficiently pass state information between plugin calls. Typically
 * a plugin might register both a pre-op and post-op call. It's very
 * convenient for the plugin to associate it's private data with the
 * operation object that's passed through the PBlock.
 *
 * --- An interface is made available to the core server. 
 *
 *   int factory_register_type(const char *name, size_t offset)
 *   void *factory_create_extension(int type,void *object,void *parent)
 *   void factory_destroy_extension(int type,void *object,void *parent,void **extension)
 *
 * An object that wishes to make itself available for extension must
 * register with the Factory.  It passes it's name, say 'Operation',
 * and an offset into the structure of where the extension block
 * is to be stored. In return a type handle is passed back, which is
 * used in place of the name in the creation and destruction calls.
 *
 * When an object is created, which has registered as extensible, it
 * must call the factory_create_extension with its type handle so that
 * the extension block can be constructed. A pointer to the block is
 * returned that *must* be stored in the object structure at the offset
 * declared by the call to factory_register_type.
 *
 * When an extensible object is destroyed the extension block must also
 * be destroyed. The factory_destroy_extension call is provided to
 * tidy up and free any extenions created for this object.
 *
 * --- An interface is made available to the plugins.
 *
 *   int slapi_register_object_extension(
 *       const char* objectname,
 *       slapi_extension_constructor_fnptr constructor, 
 *       slapi_extension_destructor_fnptr destructor,
 *       int *objecttype,
 *       int *extensionhandle)
 *   void *slapi_get_object_extension(int objecttype,void *object,int extensionhandle)
 *
 * When the plugin is initialised it must register its object extensions.
 * It must provide the name of the object to be extended, say 'Operation',
 * and constructor and destructor functions. These functions are called
 * when the object is constructed and destroyed.  The extension functions
 * would probably allocate some memory and initialise it for its
 * own use.  The registration function will fail if any objects have already
 * been created. This is why the registration *must* happen during plugin
 * initialisation. In return the plugin will receive two handles, one for
 * the object type, and one for the object extension. These only have meaning
 * for the slapi_get_object_extension function.
 *
 * A plugin retrieves a pointer to its own extension by calling slapi_get_
 * object_extension with the object from which the extension is to be
 * retrieved. The factory uses the objecttype to find the offset into the
 * object of where the extension block is stored. The extension handle is
 * then used to find the appropriate extension within the block.
 * 
 * Currently (Oct 98) the only supported objects are Operation and Connection.
 *
 * This documentation is available here...
 *
 * http://warp/server/directory-server/hydra/replication/objext.html
 */

/* JCM: Could implement simple object leak detection here */


/* ---------------------- Factory Extension ---------------------- */

struct factory_extension
{
    const char *pluginname;
	slapi_extension_constructor_fnptr constructor;
	slapi_extension_destructor_fnptr destructor;
};

static struct factory_extension*
new_factory_extension(
    const char *pluginname,
    slapi_extension_constructor_fnptr constructor, 
    slapi_extension_destructor_fnptr destructor)
{
    struct factory_extension* fe= (struct factory_extension*)slapi_ch_malloc(sizeof(struct factory_extension));
	if(pluginname!=NULL)
	{
    	fe->pluginname= slapi_ch_strdup(pluginname);
	}
	fe->constructor= constructor;
	fe->destructor= destructor;
	return fe;
}

static void
delete_factory_extension(struct factory_extension **fe)
{
    slapi_ch_free( (void **) &((*fe)->pluginname) );
    slapi_ch_free( (void **) fe);
}

/* ---------------------- Factory Type ---------------------- */

#define MAX_EXTENSIONS 32

struct factory_type
{
    char *name; /* The name of the object that can be extended */
	int extension_count; /* The number of extensions registered for this object */
	PRLock *extension_lock; /* Protect the array of extensions */
	size_t extension_offset; /* The offset into the object where the extension pointer is */
	long existence_count; /* Keep track of how many extensions blocks are in existence */
	struct factory_extension *extensions[MAX_EXTENSIONS]; /* The extension registered for this object type */
};

static struct factory_type*
new_factory_type(const char *name, size_t offset)
{
    struct factory_type* ft= (struct factory_type*)slapi_ch_malloc(sizeof(struct factory_type));
	ft->name= slapi_ch_strdup(name);
    ft->extension_lock = PR_NewLock();
	ft->extension_count= 0;
	ft->extension_offset= offset;
	ft->existence_count= 0;
	return ft;
}

static void
delete_factory_type(struct factory_type **ft)
{
    slapi_ch_free( (void **) &((*ft)->name));
	PR_DestroyLock((*ft)->extension_lock);
	slapi_ch_free( (void **) ft);
}

static int
factory_type_add_extension(struct factory_type *ft,struct factory_extension *fe)
{
    int extensionhandle= -1;
	PR_Lock(ft->extension_lock);
    if(ft->existence_count>0)
	{
	    /* Can't register an extension if there are objects already with extension blocks */
		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: Registration of %s extension by %s failed.\n", ft->name, fe->pluginname, 0);
		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: %lu %s objects already in existence.\n", ft->existence_count, ft->name, 0);
	}
	else
	{
        if(ft->extension_count<MAX_EXTENSIONS)
    	{
    	    extensionhandle= ft->extension_count;
    		ft->extensions[ft->extension_count]= fe;
			ft->extension_count++;
    	}
    	else
    	{
    		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: Registration of %s extension by %s failed.\n", ft->name, fe->pluginname, 0);
    		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: %d extensions already registered. Max is %d\n", ft->extension_count, MAX_EXTENSIONS, 0);
    	}
	}
	PR_Unlock(ft->extension_lock);
	return extensionhandle;
}

static void
factory_type_increment_existence(struct factory_type *ft)
{
    ft->existence_count++;
}

static void
factory_type_decrement_existence(struct factory_type *ft)
{
    ft->existence_count--;
	if(ft->existence_count<0)
	{
	    /* This just shouldn't happen */
   		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: %lu %s object extensions in existence.\n", ft->extension_count, ft->name, 0);
	}
}

/* ---------------------- Factory Type Store ---------------------- */

#define MAX_TYPES 16

static PRLock *factory_type_store_lock;
static struct factory_type* factory_type_store[MAX_TYPES];
static int number_of_types= 0;

static void
factory_type_store_init()
{
	int i= 0;
    factory_type_store_lock= PR_NewLock(); /* JCM - Should really free this at shutdown */
	for(i=0;i<MAX_TYPES;i++)
	{
        factory_type_store[number_of_types]= NULL;
    }
}

static int
factory_type_store_add(struct factory_type* ft)
{
    int type= number_of_types;
    factory_type_store[type]= ft;
	number_of_types++;
	return type;
}

static void
factory_type_store_remove(struct factory_type *ft)
{
	int i;
	int found_it = 0;

	for (i = 0; i < number_of_types; i++)
	{
		if (!found_it)
		{
			if (factory_type_store[i] == ft)
			{
				found_it = 1;
			}
		}
		else
		{
			factory_type_store[i-1] = factory_type_store[i];
		}
	}

	if (found_it)
	{
		factory_type_store[i-1] = NULL;
		number_of_types--;
	}
}

static struct factory_type*
factory_type_store_get_factory_type(int type)
{
    if(type>=0 && type<number_of_types)
	{
	    return factory_type_store[type];
	}
	else
	{
	    return NULL;
	}
}

static int
factory_type_store_name_to_type(const char* name)
{
    int i;
	for(i=0;i<number_of_types;i++)
	{
        if(strcasecmp(factory_type_store[i]->name,name)==0)
		{
		    return i;
		}
    }
	return -1;
}

/* ---------------------- Core Server Functions ---------------------- */

/*
 * Function for core server usage.
 * See documentation at head of file.
 */
int
factory_register_type(const char *name, size_t offset)
{
    int type= 0;
    if(number_of_types==0)
	{
        factory_type_store_init();
	}
	PR_Lock(factory_type_store_lock);
    if(number_of_types<MAX_TYPES)
	{
        struct factory_type* ft= new_factory_type(name,offset);
        type= factory_type_store_add(ft);
	}
	else
	{
   		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: Registration of %s object failed.\n", name, 0, 0);
   		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: %d objects already registered. Max is %d\n", number_of_types, MAX_TYPES, 0);
	    type= -1;
	}
	PR_Unlock(factory_type_store_lock);
	return type;
}

/*
 * Function for core server usage.
 * See documentation at head of file.
 */
void *
factory_create_extension(int type,void *object,void *parent)
{
    int n;
    void **extension= NULL;
    struct factory_type* ft= factory_type_store_get_factory_type(type);

	if(ft!=NULL)
	{
	    PR_Lock(ft->extension_lock);
    	if((n = ft->extension_count)>0)
    	{
    	    int i;
            factory_type_increment_existence(ft);
            PR_Unlock(ft->extension_lock);
    	    extension= (void**)slapi_ch_malloc(n*sizeof(void*));
    		for(i=0;i<n;i++)
    		{
                slapi_extension_constructor_fnptr constructor= ft->extensions[i]->constructor;
    			if(constructor!=NULL)
    			{
    			    extension[i]= (*constructor)(object,parent);
    			}
    		}
		}
		else
		{
		    /* No extensions registered. That's OK */
		    PR_Unlock(ft->extension_lock);
		}
	}
	else
	{
	    /* The type wasn't registered. Programming error? */
   		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: Object type handle %d not valid. Object not registered?\n", type, 0, 0);
	}
	return (void*)extension;
}

/*
 * Function for core server usage.
 * See documentation at head of file.
 */
void
factory_destroy_extension(int type,void *object,void *parent,void **extension)
{
    if(extension!=NULL && *extension!=NULL)
	{
        struct factory_type* ft= factory_type_store_get_factory_type(type);
    	if(ft!=NULL)
    	{
    	    int i,n;

    	    PR_Lock(ft->extension_lock);
    	    n=ft->extension_count;
            factory_type_decrement_existence(ft);
    	    PR_Unlock(ft->extension_lock);
    		for(i=0;i<n;i++)
    		{
                slapi_extension_destructor_fnptr destructor= ft->extensions[i]->destructor;
    			if(destructor!=NULL)
    			{
				    void **extention_array= (void**)(*extension);
    			    (*destructor)(extention_array[i],object,parent);
    			}
    		}
    	}
    	else
    	{
    	    /* The type wasn't registered. Programming error? */
       		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: Object type handle %d not valid. Object not registered?\n", type, 0, 0);
    	}
        slapi_ch_free(extension);
	}
}

/* ---------------------- Slapi Functions ---------------------- */

/*
 * Function for plugin usage.
 * See documentation at head of file.
 */
int
slapi_register_object_extension(
    const char* pluginname,
    const char* objectname,
    slapi_extension_constructor_fnptr constructor, 
    slapi_extension_destructor_fnptr destructor,
    int *objecttype,
    int *extensionhandle)
{
    int rc= 0;
    struct factory_extension* fe;
    struct factory_type* ft;
    fe= new_factory_extension(pluginname,constructor, destructor);
    *objecttype= factory_type_store_name_to_type(objectname);
    ft= factory_type_store_get_factory_type(*objecttype);
	if(ft!=NULL)
	{
        *extensionhandle= factory_type_add_extension(ft,fe);
		if(*extensionhandle==-1)
		{
            delete_factory_extension(&fe);
			factory_type_store_remove(ft);
            delete_factory_type(&ft);
		}
	}
	else
	{
   		LDAPDebug( LDAP_DEBUG_ANY, "ERROR: factory.c: Plugin %s failed to register extension for object %s.\n", pluginname, objectname, 0);
		rc= -1;
	}
	return rc;
}

/*
 * Function for plugin usage.
 * See documentation at head of file.
 */
void *
slapi_get_object_extension(int objecttype,void *object,int extensionhandle)
{
    void *object_extension= NULL;
    struct factory_type* ft= factory_type_store_get_factory_type(objecttype);
	if(ft!=NULL)
	{
	    char *object_base= (char*)object;
		void **o_extension= (void**)(object_base + ft->extension_offset);
		void **extension_array= (void**)(*o_extension);
		if ( extension_array != NULL ) {
			object_extension= extension_array[extensionhandle];
		}
	}
	return object_extension;
}

/*
 * sometimes a plugin would like to change its extension, too.
 */
void
slapi_set_object_extension(int objecttype, void *object, int extensionhandle,
                           void *extension)
{
    struct factory_type *ft = factory_type_store_get_factory_type(objecttype);
    if (ft != NULL) {
        char *object_base = (char *)object;
        void **o_extension = (void **)(object_base + ft->extension_offset);
		void **extension_array= (void**)(*o_extension);
        if (extension_array != NULL) {
            extension_array[extensionhandle] = extension;
        }
    }
}
