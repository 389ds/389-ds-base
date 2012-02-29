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
 * Copyright (C) 2012 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 *   Thread Local Storage Functions
 */
#include <slapi-plugin.h>
#include <prthread.h>

void td_dn_destructor(void *priv);

/*
 * Thread Local Storage Indexes
 */
static PRUintn td_requestor_dn;  /* TD_REQUESTOR_DN */

/*
 *   Index types defined in slapi-plugin.h
 *
 *   #define  SLAPI_TD_REQUESTOR_DN   1
 *   ...
 *   ...
 */


/*
 *  The Process:
 *
 *   [1]  Create new index type macro in slapi-plugin.h
 *   [2]  Create new static "PRUintn" index
 *   [3]  Update these functions with the new index:
 *          slapi_td_init()
 *          slapi_td_set_val()
 *          slapi_td_get_val()
 *   [4]  Create wrapper functions if so desired, and update slapi_plugin.h
 *   [5]  Create destructor (if necessary)
 */

int
slapi_td_init(int indexType)
{
    switch(indexType){
        case SLAPI_TD_REQUESTOR_DN:
            if(PR_NewThreadPrivateIndex(&td_requestor_dn, td_dn_destructor) == PR_FAILURE){
                return PR_FAILURE;
            }
        break;

        default:
            return PR_FAILURE;
    }

    return PR_SUCCESS;
}

/*
 *  Caller needs to cast value to (void *)
 */
int
slapi_td_set_val(int indexType, void *value)
{
    switch(indexType){
        case SLAPI_TD_REQUESTOR_DN:
            if(td_requestor_dn){
                if(PR_SetThreadPrivate(td_requestor_dn, value) == PR_FAILURE){
                    return PR_FAILURE;
                }
            } else {
                return PR_FAILURE;
            }
            break;

        default:
            return PR_FAILURE;
    }

    return PR_SUCCESS;
}

/*
 *  Caller needs to cast value to (void **)
 */
void
slapi_td_get_val(int indexType, void **value)
{
    switch(indexType){
        case SLAPI_TD_REQUESTOR_DN:
            if(td_requestor_dn){
                *value = PR_GetThreadPrivate(td_requestor_dn);
            } else {
                *value = NULL;
            }
            break;
        default:
            *value = NULL;
            return;
    }
}

/*
 *  Wrapper Functions
 */

int
slapi_td_dn_init()
{
    if(slapi_td_init(SLAPI_TD_REQUESTOR_DN) == PR_FAILURE){
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

int
slapi_td_set_dn(char *value)
{
    if(slapi_td_set_val(SLAPI_TD_REQUESTOR_DN, (void *)value) == PR_FAILURE){
        return PR_FAILURE;
    }

    return PR_SUCCESS;
}

void
slapi_td_get_dn(char **value){
    slapi_td_get_val(SLAPI_TD_REQUESTOR_DN, (void **)value);
}


/*
 *   Destructor Functions
 */

void
td_dn_destructor(void *priv)
{
    slapi_ch_free((void **)&priv);
}


