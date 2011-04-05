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

/*
 * match.c
 *
 * routines to "register" matching rules with the server
 *
 *
 * 
 *
 */

#include "slap.h"


struct matchingRuleList *g_get_global_mrl(void);
void g_set_global_mrl(struct matchingRuleList *newglobalmrl);
int slapi_matchingrule_register(Slapi_MatchingRuleEntry *mrule);
int slapi_matchingrule_unregister(char *oid);
Slapi_MatchingRuleEntry *slapi_matchingrule_new(void);
void slapi_matchingrule_free(Slapi_MatchingRuleEntry **mrEntry,
			     int freeMembers);
int slapi_matchingrule_get(Slapi_MatchingRuleEntry *mr, int arg, void *value);
int slapi_matchingrule_set(Slapi_MatchingRuleEntry *mr, int arg, void *value);


static int _mr_alloc_new(struct matchingRuleList **mrl);

static struct matchingRuleList *global_mrl=NULL;

struct matchingRuleList* 
g_get_global_mrl(void)
{
    return global_mrl;
}

void 
g_set_global_mrl(struct matchingRuleList *newglobalmrl)
{
    global_mrl = newglobalmrl;
}

int
slapi_matchingrule_set(Slapi_MatchingRuleEntry *mr, int arg, void *value)
{
    if(NULL == mr) {
	return(-1);
    }
    switch(arg) {
    case SLAPI_MATCHINGRULE_NAME:
	{
	    mr->mr_name = (char *)value;
	    break;
	}
    case SLAPI_MATCHINGRULE_OID:
	{
	    mr->mr_oid = (char *)value;
	    break;
	}
    case SLAPI_MATCHINGRULE_DESC:
	{
	    mr->mr_desc = (char *)value;
	    break;
	}
    case SLAPI_MATCHINGRULE_SYNTAX:
	{
	    mr->mr_syntax = (char *)value;
	    break;
	}
    case SLAPI_MATCHINGRULE_OBSOLETE:
	{
	    mr->mr_obsolete = *((int *)value);
	    break;
	}
    default:
	{
	    break;
	}
    }
    return(0);
}

int
slapi_matchingrule_get(Slapi_MatchingRuleEntry *mr, int arg, void *value)
{
    if((NULL == mr) || (NULL == value)) {
	return(-1);
    }
    switch(arg) {
    case SLAPI_MATCHINGRULE_NAME:
	{
	    (*(char **)value) = mr->mr_name;
	    break;
	}
    case SLAPI_MATCHINGRULE_OID:
	{
	    (*(char **)value) = mr->mr_oid;
	    break;
	}
    case SLAPI_MATCHINGRULE_DESC:
	{
	    (*(char **)value) = mr->mr_desc;
	    break;
	}
    case SLAPI_MATCHINGRULE_SYNTAX:
	{
	    (*(char **)value) = mr->mr_syntax;
	    break;
	}
    case SLAPI_MATCHINGRULE_OBSOLETE:
	{
	    (*(int *)value) = mr->mr_obsolete;
	    break;
	}
    default:
	{
	    return(-1);
	}
    }
    return(0);
}

Slapi_MatchingRuleEntry *
slapi_matchingrule_new(void)
{
    Slapi_MatchingRuleEntry *mrEntry=NULL;
    mrEntry = (Slapi_MatchingRuleEntry *)
	slapi_ch_calloc(1, sizeof(Slapi_MatchingRuleEntry));
    return(mrEntry);
}

void
slapi_matchingrule_free(Slapi_MatchingRuleEntry **mrEntry,
			int freeMembers)
{
    if((NULL == mrEntry) || (NULL == *mrEntry)) {
	return;
    }
    if(freeMembers) {
	slapi_ch_free((void **)&((*mrEntry)->mr_name));
	slapi_ch_free((void **)&((*mrEntry)->mr_oid));
	slapi_ch_free((void **)&((*mrEntry)->mr_desc));
	slapi_ch_free((void **)&((*mrEntry)->mr_syntax));
	slapi_ch_free((void **)&((*mrEntry)->mr_oidalias));
	slapi_ch_array_free((*mrEntry)->mr_compat_syntax);
    }
    slapi_ch_free((void **)mrEntry);
    return;
}

static int
_mr_alloc_new(struct matchingRuleList **mrl)
{
    if(!mrl) {
        return(-1);
    }
    *mrl = NULL;
    *mrl = (struct matchingRuleList *)
        slapi_ch_calloc(1, sizeof(struct matchingRuleList));
 
 
    (*mrl)->mr_entry = (Slapi_MatchingRuleEntry *)
        slapi_ch_calloc(1, sizeof(Slapi_MatchingRuleEntry));
    return(0);
}

#if 0
static int
_mr_free(struct matchingRuleList **mrl /*, int freeEntry */)
{
    slapi_ch_free((void **)mrl);
    return(0);
}
#endif

int slapi_matchingrule_register(Slapi_MatchingRuleEntry *mrule)
{
    struct matchingRuleList *mrl=NULL;
    struct matchingRuleList *newmrl=NULL;
    
    if(NULL == mrule) {
	return(-1);
    }
    if(_mr_alloc_new(&newmrl) != 0) {
        return(-1);
    }
    if(NULL != mrule->mr_name) {
	newmrl->mr_entry->mr_name =
	    slapi_ch_strdup((char *) mrule->mr_name);
    }
    if(NULL != mrule->mr_oid) {
	newmrl->mr_entry->mr_oid =
	    slapi_ch_strdup((char *) mrule->mr_oid);
    }
    if(NULL != mrule->mr_oidalias) {
	newmrl->mr_entry->mr_oidalias =
	    slapi_ch_strdup((char *) mrule->mr_oidalias);
    }
    if(NULL != mrule->mr_desc) {
	newmrl->mr_entry->mr_desc = 
	    slapi_ch_strdup((char *) mrule->mr_desc);
    }
    if(NULL != mrule->mr_syntax) {
	newmrl->mr_entry->mr_syntax = 
	    slapi_ch_strdup((char *) mrule->mr_syntax);
    }
    newmrl->mr_entry->mr_obsolete = mrule->mr_obsolete;
    newmrl->mr_entry->mr_compat_syntax = charray_dup(mrule->mr_compat_syntax);

    for(mrl = g_get_global_mrl();
        ((NULL != mrl) && (NULL != mrl->mrl_next));
        mrl = mrl->mrl_next);

    if(NULL == mrl) {
        g_set_global_mrl(newmrl);
        mrl = newmrl;
    }
    mrl->mrl_next = newmrl;
    newmrl->mrl_next = NULL;
    return(LDAP_SUCCESS);
}

int slapi_matchingrule_unregister(char *oid)
{
    /* 
     * Currently, not implemented.
     * For now, the matching rules are read at startup and cannot be modified.
     * If and when, we do allow dynamic modifications, this routine will
     * have to do some work.
     */
    return(0);
}

/*
  See if a matching rule for this name or OID
  is registered and is an ORDERING matching rule that applies
  to the given syntax.
*/
int slapi_matchingrule_is_ordering(const char *oid_or_name, const char *syntax_oid)
{
    struct matchingRuleList *mrl=NULL;

    if (slapi_matchingrule_is_compat(oid_or_name, syntax_oid)) {
        for (mrl = g_get_global_mrl(); mrl != NULL; mrl = mrl->mrl_next) {
            if (mrl->mr_entry->mr_name && !strcasecmp(oid_or_name, mrl->mr_entry->mr_name)) {
                return (mrl->mr_entry->mr_name &&
                        PL_strcasestr(mrl->mr_entry->mr_name, "ordering"));
            }
            if (mrl->mr_entry->mr_oid && !strcmp(oid_or_name, mrl->mr_entry->mr_oid)) {
                return (mrl->mr_entry->mr_name &&
                        PL_strcasestr(mrl->mr_entry->mr_name, "ordering"));
            }
        }
    }

    return 0;
}

/*
  See if a matching rule for this name or OID
  is compatible with the given syntax.
*/
int slapi_matchingrule_is_compat(const char *mr_oid_or_name, const char *syntax_oid)
{
    struct matchingRuleList *mrl=NULL;
    int found = 0;

    for (mrl = g_get_global_mrl(); mrl != NULL; mrl = mrl->mrl_next) {
        if (mrl->mr_entry->mr_name && !strcasecmp(mr_oid_or_name, mrl->mr_entry->mr_name)) {
            found = 1;
            break;
        }
        if (mrl->mr_entry->mr_oid && !strcmp(mr_oid_or_name, mrl->mr_entry->mr_oid)) {
            found = 1;
            break;
        }
    }

    if (found && mrl) {
        char **mr_syntax;
        if (!strcmp(mrl->mr_entry->mr_syntax, syntax_oid)) {
            return 1;
        }
        for (mr_syntax = mrl->mr_entry->mr_compat_syntax;
             mr_syntax && *mr_syntax;
             mr_syntax++) {
            if (!strcmp(*mr_syntax, syntax_oid)) {
                return 1;
            }
        }
    }
            

    return 0;
}

/* the matching rules defined in the collation plugin
   all start with this OID
*/
#define COLLATION_BASE_OID "2.16.840.1.113730.3.3.2."
#define COLLATION_BASE_OID_LEN 24

/*
  See if a matching rule for this name or OID
  can use a simple compare function for generating index entries
*/
int slapi_matchingrule_can_use_compare_fn(const char *mr_oid_or_name)
{
    struct matchingRuleList *mrl=NULL;
    int found = 0;

    for (mrl = g_get_global_mrl(); mrl != NULL; mrl = mrl->mrl_next) {
        if (mrl->mr_entry->mr_name && !strcasecmp(mr_oid_or_name, mrl->mr_entry->mr_name)) {
            found = 1;
            break;
        }
        if (mrl->mr_entry->mr_oid && !strcmp(mr_oid_or_name, mrl->mr_entry->mr_oid)) {
            found = 1;
            break;
        }
    }

    if (!found) {
        return 0;
    }

    if (!strncmp(mrl->mr_entry->mr_oid, COLLATION_BASE_OID, COLLATION_BASE_OID_LEN)) {
        /* this OID is provided by the collation plugin so we can't just
           use a simple compare function to generate index keys */
        return 0;
    }

    return 1;
}
