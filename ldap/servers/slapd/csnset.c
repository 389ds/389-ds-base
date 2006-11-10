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
#include "slapi-private.h"

static const CSNSet *csnset_get_csnset_node_from_csn(const CSNSet *csnset, const CSN *csn);
static const CSNSet *csnset_get_csnset_node_from_type(const CSNSet *csnset, CSNType t);
static CSNSet *csnset_get_previous_csnset_node(CSNSet *csnset, const CSN *csn);

/*
 * The CSN is always added to the end of the list.
 */
void
csnset_add_csn(CSNSet **csnset, CSNType t, const CSN *csn)
{
	if(csn!=NULL)
	{
		CSNSet *newcsn= (CSNSet*)slapi_ch_malloc(sizeof(CSNSet));
		newcsn->type= t;
		csn_init_by_csn(&newcsn->csn,csn);
		newcsn->next= NULL;
		{
			CSNSet **p= csnset;
			CSNSet *n= *csnset;
			while(n!=NULL)
			{
				p= &(n->next);
				n= n->next;
			}
			*p= newcsn;
		}
	}
}

/*
 * The CSN is inserted into the list at the appropriate point..
 */
void
csnset_insert_csn(CSNSet **csnset, CSNType t, const CSN *csn)
{
	if((csn!=NULL) && (*csnset==NULL))
	{
		csnset_add_csn(csnset, t, csn);
	}
	else if(csn!=NULL)
	{
		CSNSet *newcsn= (CSNSet*)slapi_ch_malloc(sizeof(CSNSet));
		CSNSet *f= csnset_get_previous_csnset_node(*csnset, csn);
		newcsn->type= t;
		csn_init_by_csn(&newcsn->csn,csn);
		if(f==NULL)
		{
			/* adding to the list head */
			newcsn->next= *csnset;
			*csnset= newcsn;			
		}
		else
		{
			newcsn->next= f->next;
			f->next= newcsn;
		}
	}
}

/*
 * Find the CSN of the given type and update it.
 */
void
csnset_update_csn(CSNSet **csnset, CSNType t, const CSN *csn)
{
	const CSNSet *f= csnset_get_csnset_node_from_type(*csnset, t);
	if(f==NULL)
	{
		csnset_add_csn(csnset,t,csn);
	}
	else
	{
		if (csn_compare(csn, (CSN*)(&f->csn)) > 0)
		{
			csn_init_by_csn((CSN*)(&f->csn),csn);
		}
	}
}

/*
 * Check if the set CSN of CSNs contains a given CSN.
 */
int
csnset_contains(const CSNSet *csnset, const CSN *csn)
{
	const CSNSet *f= csnset_get_csnset_node_from_csn(csnset, csn);
	return(f!=NULL);
}

/*
 * Remove the first CSN of the given type.
 */
void
csnset_remove_csn(CSNSet **csnset, CSNType t)
{
	CSNSet **p= csnset;
	CSNSet *n= *csnset;
	while(n!=NULL)
	{
		if(n->type==t)
		{
			*p= n->next;
			slapi_ch_free((void**)&n);
		}
		else
		{
			p= &n->next;
			n= n->next;
		}
	}
}

void
csnset_free(CSNSet **csnset)
{
	csnset_purge(csnset, NULL);
}

/*
 * Get the first CSN of the given type.
 */
const CSN *
csnset_get_csn_of_type(const CSNSet *csnset, CSNType t)
{
	const CSN *csn= NULL;
	const CSNSet *f= csnset_get_csnset_node_from_type(csnset, t);
	if(f!=NULL)
	{
		csn= &f->csn;
	}
	return csn;
}

const CSN *
csnset_get_previous_csn(const CSNSet *csnset, const CSN *csn)
{
	const CSN *prevcsn= NULL;
	CSNSet *f= csnset_get_previous_csnset_node((CSNSet*)csnset, csn);
	if(f!=NULL)
	{
		prevcsn= &f->csn;
	}
	return prevcsn;
}

const CSN *
csnset_get_last_csn(const CSNSet *csnset)
{
	const CSN *csn= NULL;
	const CSNSet *n= csnset;
	while(n!=NULL)
	{
		if(n->next==NULL)
		{
			csn= &n->csn;
		}
		n= n->next;
	}
	return csn;
}

void* 
csnset_get_first_csn (const CSNSet *csnset, CSN **csn, CSNType *t)
{
    if (csnset)
    {        
        *csn = (CSN*)&csnset->csn;
        *t = csnset->type;
        return (void*)csnset;
    }
    else
        return NULL;
}

void* 
csnset_get_next_csn (const CSNSet *csnset, void *cookie, CSN **csn, CSNType *t)
{
    CSNSet *node;

    if (csnset && cookie)
    {
        node = ((CSNSet*)cookie)->next;
        if (node)
        {
            *csn = (CSN*)&node->csn;
            *t = node->type;
            return node;
        }
        else
            return NULL;
    }
    else
        return NULL;
}

static CSNSet *
csnset_get_previous_csnset_node(CSNSet *csnset, const CSN *csn)
{
	CSNSet *f= NULL;
	CSNSet *p= NULL;
	CSNSet *n= csnset;
	while(n!=NULL)
	{
		if(csn_compare(&n->csn, csn)>0)
		{
			f= p;
			n= NULL;
		}
		else
		{
			p= n;
			n= n->next;
			if(n==NULL)
			{
				/* Got to the end of the list... */
				f= p;
			}
		}
	}
	return f;
}

static const CSNSet *
csnset_get_csnset_node_from_csn(const CSNSet *csnset, const CSN *csn)
{
	const CSNSet *f= NULL;
	const CSNSet *n= csnset;
	while(n!=NULL)
	{
		if(csn_compare(&n->csn, csn)==0)
		{
			f= n;
			n= NULL;
		}
		else
		{
			n= n->next;
		}
	}
	return f;
}

static const CSNSet *
csnset_get_csnset_node_from_type(const CSNSet *csnset, CSNType t)
{
	const CSNSet *f= NULL;
	const CSNSet *n= csnset;
	while(n!=NULL)
	{
		if(n->type==t)
		{
			f= n;
			n= NULL;
		}
		else
		{
			n= n->next;
		}
	}
	return f;
}

/*
 * Remove any CSNs older than csnUpTo. If csnUpTo is NULL,
 * remove all CSNs.
 */
void
csnset_purge(CSNSet **csnset, const CSN *csnUpTo)
{
	if (csnset != NULL)
	{
		CSNSet *n = *csnset, *nprev = NULL, *nnext;
		while (n != NULL)
		{
			if (NULL == csnUpTo || (csn_compare(&n->csn, csnUpTo) < 0))
			{
				nnext = n->next;
				if (*csnset == n)
				{
					/* Deletion of head */
					*csnset = nnext;
				}
				else if (nprev)
				{
					/* nprev was not purged, but n will be */
					nprev->next = nnext;
				}
				slapi_ch_free((void**)&n);
				n = nnext;
			}
			else
			{
				nprev = n;
				n = n->next;
			}
		}
	}
}

size_t
csnset_string_size(CSNSet *csnset)
{
	size_t s= 0;
	CSNSet *n= csnset;
	while(n!=NULL)
	{
		/* sizeof(;vucsn-011111111222233334444) */
		s+= 1 + LDIF_CSNPREFIX_MAXLENGTH + _CSN_VALIDCSN_STRLEN;
		n= n->next;
	}
	return s;
}

size_t
csnset_size(CSNSet *csnset)
{
	size_t s= 0;
	CSNSet *n= csnset;
	while(n!=NULL)
	{
		s+= sizeof(CSNSet);
		n= n->next;
	}
	return s;
}

CSNSet *
csnset_dup(const CSNSet *csnset)
{
	CSNSet *newcsnset= NULL;
	const CSNSet *n= csnset;
	while(n!=NULL)
	{
		csnset_add_csn(&newcsnset,n->type,&n->csn);
		n= n->next;
	}
	return newcsnset;
}

void
csnset_as_string(const CSNSet *csnset,char *s)
{
	const CSNSet *n= csnset;
	while(n!=NULL)
	{
		csn_as_attr_option_string(n->type,&n->csn,s);
		/* sizeof(;vucsn-011111111222233334444) */
		s+= 1 + LDIF_CSNPREFIX_MAXLENGTH + _CSN_VALIDCSN_STRLEN;
		n= n->next;
	}
}
