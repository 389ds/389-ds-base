/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef CORERES_H
#define CORERES_H

#include "i18n.h"

Resource* core_res_init_resource(const char* path, const char* package);
const char *core_res_getstring(Resource *hres, char *key, ACCEPT_LANGUAGE_LIST lang); 
void core_res_destroy_resource(Resource *hres);

#endif
