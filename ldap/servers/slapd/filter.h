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


#ifndef _FILTER_H_
#define _FILTER_H_

#include "slapi-plugin.h" /* struct berval, Slapi_PBlock, mrFilterMatchFn */

typedef Slapi_Attr* attr_ptr;

typedef int (*mrf_plugin_fn) (Slapi_PBlock*);

#define MRF_ANY_TYPE   1
#define MRF_ANY_VALUE  2

typedef struct mr_filter_t {
    char*         mrf_oid;
    char*         mrf_type;
    struct berval mrf_value;
    char          mrf_dnAttrs;
    struct slapdplugin* mrf_plugin;
    mrFilterMatchFn mrf_match;
    mrf_plugin_fn mrf_index;
    unsigned int  mrf_reusable; /* MRF_ANY_xxx */
    mrf_plugin_fn mrf_reset;
    void*         mrf_object; /* whatever the implementation needs */
    mrf_plugin_fn mrf_destroy;
} mr_filter_t;

#endif
