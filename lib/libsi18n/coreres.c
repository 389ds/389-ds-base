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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "i18n.h"

#include "propset.h"

#include "coreres.h"

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

