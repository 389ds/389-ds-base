/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "i18n.h"

#include "libadminutil/resource.h"
#include "propset.h"

#include "coreres.h"
#if 0
typedef struct ResourceS
{
	char *path;
	char *package;
    PropertiesSet *propset;
} Resource;
#endif

Resource* core_res_init_resource(const char* path, const char* package)
{
    PropertiesSet *propset;
    char *directory;
    char *filename;
    char *file_path;
    char *p, *q;
    char *filep;
    Resource *hres;

    /*********************
      Create full path information
      eg. ./es40/admin  and  cgi.bin.start  ==>  
            ./es40/admin/cgi/bin/start.properties
    **********************/
    file_path = (char *) malloc (strlen(path) + strlen(package) + 20);


    strcpy(file_path, path);
    if (path[strlen(path)-1] != '/')
        strcat(file_path, "/");

    p = file_path + strlen(file_path);
    q = (char *) package;
    
    filep = p - 1;

    /*  Append package to file_path
        p: end positon of path + 1
        q: start position of package
     */
    while (q && *q) {
        if (*q == '.') {
            filep = q;
            *p ++ = '/';
        }
        else
            *p ++ = *q ++;

    }
    *p = '\0';

    *filep = '\0';
    filename = filep + 1;
    directory = file_path;

    propset = PropertiesInit (directory, filename);

    if (propset == NULL)
        return NULL;

    hres = (Resource *) malloc(sizeof(Resource));
    memset(hres, 0, sizeof(Resource));

    hres->path = strdup(file_path);
    hres->propset = propset;

    if (file_path)
        free (file_path);

    return hres;
}

const char *core_res_getstring(Resource *hres, char *key, ACCEPT_LANGUAGE_LIST lang) 
{

    if (key == NULL)
        return NULL;

    if (hres) {
        return PropertiesGetString(hres->propset, key, lang);
    }

    return NULL;
}

void core_res_destroy_resource(Resource *hres)
{
    if (hres) {
        if (hres->path)
            free(hres->path);
        if (hres->package)
            free(hres->package);
        PropertiesDestroy(hres->propset);

        free(hres);
    }
}

