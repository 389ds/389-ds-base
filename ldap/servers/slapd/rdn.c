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

Slapi_RDN *
slapi_rdn_new()
{
    Slapi_RDN *rdn= (Slapi_RDN *)slapi_ch_malloc(sizeof(struct slapi_rdn));    
    slapi_rdn_init(rdn);
	return rdn;
}

Slapi_RDN *
slapi_rdn_new_dn(const char *dn)
{
    Slapi_RDN *rdn= slapi_rdn_new();
    slapi_rdn_init_dn(rdn,dn);
	return rdn;
}

Slapi_RDN *
slapi_rdn_new_all_dn(const char *dn)
{
    Slapi_RDN *rdn= slapi_rdn_new();
    slapi_rdn_init_all_dn(rdn,dn);
	return rdn;
}

Slapi_RDN *
slapi_rdn_new_sdn(const Slapi_DN *sdn)
{
    Slapi_RDN *rdn= slapi_rdn_new();
    slapi_rdn_init_sdn(rdn,sdn);
	return rdn;
}

Slapi_RDN *
slapi_rdn_new_rdn(const Slapi_RDN *fromrdn)
{
    Slapi_RDN *rdn= slapi_rdn_new();
    slapi_rdn_init_rdn(rdn,fromrdn);
	return rdn;
}

void
slapi_rdn_init(Slapi_RDN *rdn)
{
	rdn->flag= 0;
	rdn->rdn= NULL;
	rdn->nrdn= NULL;
	rdn->rdns= NULL;
	rdn->all_rdns = NULL;
	rdn->all_nrdns = NULL;
	rdn->butcheredupto= -1; /* Means we haven't started converting '=' to '\0' in rdns */
}

void
slapi_rdn_init_dn(Slapi_RDN *rdn,const char *dn)
{
	slapi_rdn_init(rdn);
	if(dn!=NULL)
	{
		char **dns= slapi_ldap_explode_dn(dn, 0);
		if(dns!=NULL)
		{
			rdn->rdn= slapi_ch_strdup(dns[0]);
			slapi_ldap_value_free(dns);
		}
	}
}

/*
 * This function sets dn to Slapi_RDN.
 * Note: This function checks if the DN is in the root or sub suffix 
 * the server owns.  If it is, the root or sub suffix is treated as one 
 * "rdn" (e.g., "dc=sub,dc=example,dc=com") and 0 is returned.  
 * If it is not, the DN is separated by ',' and each string is set to RDN 
 * array. (e.g., input: "uid=A,ou=does_not_exist" ==> "uid=A", "ou=
 * does_not_exist") and 1 is returned.
 *
 * Return Value:  0 -- Success
 *               -1 -- Error (invalid input: NULL DN or RDN, empty RDN ",,")
 *                1 -- "dn" does not belong to the database; could be "rdn"
 */
static int
_slapi_rdn_init_all_dn_ext(Slapi_RDN *rdn, const Slapi_DN *sdn)
{
	const char *dn = NULL;
	const char *ndn= NULL;
	const Slapi_DN *suffix = NULL;
	char **dns = NULL;
	int rc = 1;

	if (NULL == rdn || NULL == sdn) {
		return -1;
	}

	dn = slapi_sdn_get_dn(sdn);
	if (NULL == dn) {
		return -1;
	}
	for (; isspace(*dn) ; dn++) ;

	/* Suffix is a part of mapping tree. We should not free it */
	suffix = slapi_get_suffix_by_dn(sdn);
	if (suffix) {
		char *p;
		ndn = slapi_sdn_get_ndn(sdn);
		p = PL_strcaserstr(ndn, slapi_sdn_get_ndn(suffix));
		if (p) {
			if (p == ndn) { /* dn is suffix */
				charray_add(&dns, slapi_ch_strdup(dn));
				rc = 0; /* success */
			} else {
				int commas = 0;
				int len = strlen(dn);
				char *endp = NULL;
				/* count ',' in suffix */
				char *q = (char *)slapi_sdn_get_ndn(suffix);
				while (NULL != (q = PL_strchr(q, ','))) {
					commas++;
					q++;
				}
				/* found out the previous ',' (++commas-th) to suffix in dn */
				++commas;
				q = endp = (char *)dn + len;
				while (commas > 0 && q) {
					q = PL_strnrchr(dn, ',', len);
					commas--;
					if (q) {
						len -= endp - q;
						endp = q;
					}
				}
				if (q) {
					char bakup = *q;
					*q = '\0';
					dns = slapi_ldap_explode_dn(dn, 0);
					if (NULL == dns) { /* if dn contains NULL RDN (e.g., ",,"),
										  slapi_ldap_explode_dn returns NULL */
						*q = bakup;
						return -1;
					}
					/* add the suffix */
					charray_add(&dns,
								slapi_ch_strdup(slapi_sdn_get_dn(suffix)));
					*q = bakup;
					rc = 0; /* success */
				} else {
					/* Given dn does not belong to this server. Just set it. */
					dns = slapi_ldap_explode_dn(dn, 0);
				}
			}
		} else {
			/* Given dn does not belong to this server. Just set it. */
			dns = slapi_ldap_explode_dn(dn, 0);
		}
	} else {
		/* Given dn does not belong to this server. Just set it. */
		dns = slapi_ldap_explode_dn(dn, 0);
	}

	/* Get the last matched position */
	if(dns)
	{
		rdn->rdn = slapi_ch_strdup(dns[0]);
		rdn->all_rdns = dns;
		slapi_setbit_uchar(rdn->flag,FLAG_ALL_RDNS);
	}

	return rc;
}

/*
 * This function sets dn to Slapi_RDN.
 * Note: The underlying function _slapi_rdn_init_all_dn_ext checks if the DN
 * is in the root or sub suffix the server owns.  If it is, the root or sub
 * suffix is treated as one "rdn" (e.g., "dc=sub,dc=example,dc=com") and 0 is
 * returned.  If it is not, the DN is separated by ',' and each string is set
 * to RDN array. (e.g., input: "uid=A,ou=does_not_exist" ==> "uid=A", "ou=
 * does_not_exist") and 1 is returned.
 *
 * Return Value:  0 -- Success
 *               -1 -- Error
 *                1 -- dn does not belong to the database
 */
int
slapi_rdn_init_all_dn(Slapi_RDN *rdn, const char *dn)
{
	int rc = 0; /* success */
	Slapi_DN sdn;

	if (NULL == rdn || NULL == dn)
	{
		return -1;
	}
	slapi_rdn_init(rdn);
	slapi_sdn_init(&sdn);
	slapi_sdn_set_dn_byval(&sdn, dn);
	rc = _slapi_rdn_init_all_dn_ext(rdn, (const Slapi_DN *)&sdn);
	slapi_sdn_done(&sdn);
	return rc;
}

/*
 * This function sets DN from sdn to Slapi_RDN.
 * Note: The underlying function _slapi_rdn_init_all_dn_ext checks if the DN
 * is in the root or sub suffix the server owns.  If it is, the root or sub
 * suffix is treated as one "rdn" (e.g., "dc=sub,dc=example,dc=com") and 0 is
 * returned.  If it is not, the DN is separated by ',' and each string is set
 * to RDN array. (e.g., input: "uid=A,ou=does_not_exist" ==> "uid=A", "ou=
 * does_not_exist") and 1 is returned.
 *
 * Return Value:  0 -- Success
 *               -1 -- Error
 *                1 -- dn does not belong to the database
 */
int
slapi_rdn_init_all_sdn(Slapi_RDN *rdn, const Slapi_DN *sdn)
{
	int rc = 0; /* success */

	if (NULL == rdn || NULL == sdn)
	{
		return -1;
	}
	slapi_rdn_init(rdn);
	rc = _slapi_rdn_init_all_dn_ext(rdn, sdn);
	return rc;
}

void
slapi_rdn_init_sdn(Slapi_RDN *rdn,const Slapi_DN *sdn)
{
	if(sdn!=NULL)
	{
		slapi_rdn_init_dn(rdn,slapi_sdn_get_dn(sdn));
	}
	else
	{
		slapi_rdn_init(rdn);
	}
}

void
slapi_rdn_init_rdn(Slapi_RDN *rdn,const Slapi_RDN *fromrdn)
{
	slapi_rdn_init(rdn);
	rdn->rdn= slapi_ch_strdup(fromrdn->rdn);
}

void
slapi_rdn_set_dn(Slapi_RDN *rdn,const char *dn)
{
	slapi_rdn_done(rdn);
	slapi_rdn_init_dn(rdn,dn);
}

void
slapi_rdn_set_all_dn(Slapi_RDN *rdn,const char *dn)
{
	slapi_rdn_done(rdn);
	slapi_rdn_init_all_dn(rdn, dn);
}

void
slapi_rdn_set_sdn(Slapi_RDN *rdn,const Slapi_DN *sdn)
{
	slapi_rdn_done(rdn);
	slapi_rdn_init_sdn(rdn,sdn);
}

void
slapi_rdn_set_rdn(Slapi_RDN *rdn,const Slapi_RDN *fromrdn)
{
	slapi_rdn_done(rdn);
	slapi_rdn_init_rdn(rdn,fromrdn);
}

static char **
slapi_rdn_get_rdns(Slapi_RDN *rdn)
{
   	char **rdns= NULL;
	/* Check if rdns is upto date */
    if(!slapi_isbitset_uchar(rdn->flag,FLAG_RDNS))
	{
		if(rdn->rdns!=NULL)
		{
		    slapi_ldap_value_free(rdn->rdns);
			rdn->rdns= NULL;
		}
		if(rdn->rdn!=NULL)
		{
		   	rdn->rdns = slapi_ldap_explode_rdn( rdn->rdn, 0 );
			rdns= rdn->rdns;
		}
	    slapi_setbit_uchar(rdn->flag,FLAG_RDNS);
		rdn->butcheredupto= -1;
	}
	return rdns;
}


void
slapi_rdn_free(Slapi_RDN **rdn)
{
	if(rdn!=NULL)
	{
		slapi_rdn_done(*rdn);
		slapi_ch_free((void**)rdn);
	}
}

void
slapi_rdn_done(Slapi_RDN *rdn)
{
	if(rdn!=NULL)
	{
	    slapi_ch_free_string(&(rdn->rdn));
		slapi_ch_free_string(&(rdn->nrdn));
	    slapi_ldap_value_free(rdn->rdns);
	    slapi_ldap_value_free(rdn->all_rdns);
	    slapi_ldap_value_free(rdn->all_nrdns);
		slapi_rdn_init(rdn);
	}
}

int
slapi_rdn_get_first(Slapi_RDN *rdn, char **type, char **value)
{
	return slapi_rdn_get_next(rdn, 0, type, value);
}

int
slapi_rdn_get_next(Slapi_RDN *rdn, int index, char **type, char **value)
{
	int returnindex;
	PR_ASSERT(index>=0);
	if(rdn->rdns==NULL)
	{
		rdn->rdns= slapi_rdn_get_rdns(rdn);
	}

    if (rdn->rdns == NULL || rdn->rdns[index]==NULL)
	{
		*type= NULL;
		*value= NULL;
		returnindex= -1;
	}
	else
	{
		if(rdn->butcheredupto>=index)
		{
			/* the '=' has already been converted to a '\0' */
			*type= rdn->rdns[index];
			*value= *type + strlen(*type) + 1;
			returnindex= ++index;
		}
		else
		{
		    *type= PL_strchr(rdn->rdns[index],'=');
			if(*type==NULL)
			{
				/* This just shouldn't happen... */
				*type= NULL;
				*value= NULL;
				returnindex= -1;
			}
			else
			{
			    **type = '\0';
				*value= *type;
				(*value)++; /* Skip the '\0' */
			    *type = rdn->rdns[index];
				rdn->butcheredupto= index;
				returnindex= ++index;
			}
		}
	}
	return returnindex;
}

int
slapi_rdn_get_index(Slapi_RDN *rdn, const char *type, const char *value, size_t length)
{
	int result;
	char *theValue;
	result= slapi_rdn_get_index_attr(rdn, type, &theValue);
	if(result!=-1)
	{
		if(theValue==NULL || (strncasecmp(value,theValue,length)!=0))
		{
			result= -1;
		}
	}
	return result;
}

int
slapi_rdn_get_index_attr(Slapi_RDN *rdn, const char *type, char **value)
{
	int result= -1;	
	int index;
	char *theType;
	index= slapi_rdn_get_first(rdn, &theType, value);
	while(index!=-1)
	{
		if(theType!=NULL &&	value!=NULL &&
		   (strcasecmp(type,theType)==0))
		{
			result= index;
			index= -1;
		}
		else
		{
			index= slapi_rdn_get_next(rdn, index, &theType, value);
		}
	}
	return result;
}

int
slapi_rdn_contains_attr(Slapi_RDN *rdn, const char *type, char **value)
{
	return (slapi_rdn_get_index_attr(rdn,type,value)!=-1);
}

int
slapi_rdn_contains(Slapi_RDN *rdn, const char *type, const char *value, size_t length)
{
	return (slapi_rdn_get_index(rdn,type,value,length)!=-1);
}

int
slapi_rdn_add(Slapi_RDN *rdn, const char *type, const char *value)
{
	PR_ASSERT(NULL != type);
	PR_ASSERT(NULL != value);
	if(rdn->rdn==NULL)
	{
	    /* type=value '\0' */
		rdn->rdn= slapi_ch_malloc(strlen(type)+1+strlen(value)+1);
		strcpy( rdn->rdn, type );
		strcat( rdn->rdn, "=" );
		strcat( rdn->rdn, value );
	}
	else
	{
	    /* type=value+rdn '\0' */
		char *newrdn= slapi_ch_malloc(strlen(type)+1+strlen(value)+1+strlen(rdn->rdn)+1);
		strcpy( newrdn, type );
		strcat( newrdn, "=" );
		strcat( newrdn, value );
		strcat( newrdn, "+" );
		strcat( newrdn, rdn->rdn );
		slapi_ch_free((void**)&rdn->rdn);
		rdn->rdn= newrdn;
	}
    slapi_unsetbit_uchar(rdn->flag,FLAG_RDNS);
	return 1;
}

int
slapi_rdn_remove_index(Slapi_RDN *rdn, int atindex)
{
	Slapi_RDN newrdn;
	int result= 0;
	int index;
	char *theType;
	char *theValue;
	slapi_rdn_init(&newrdn);
	index= slapi_rdn_get_first(rdn, &theType, &theValue);
	while(index!=-1)
	{
		if(index!=atindex)
		{
			slapi_rdn_add(&newrdn,theType,theValue);
		}
		else
		{
			result= 1;
		}
		index= slapi_rdn_get_next(rdn, index, &theType, &theValue);
	}
	if(result)
	{
		slapi_rdn_set_rdn(rdn,&newrdn);
	}
	slapi_rdn_done(&newrdn);
	return result;
}

int
slapi_rdn_remove(Slapi_RDN *rdn, const char *type, const char *value, size_t length)
{
	int result= 0;
	if(rdn->rdn!=NULL)
	{
		int atindex= slapi_rdn_get_index(rdn, type, value, length);
		if(atindex!=-1)
		{
			result= slapi_rdn_remove_index(rdn, atindex);
		}
	}
	return result;
}

int
slapi_rdn_remove_attr(Slapi_RDN *rdn, const char *type)
{
	int result= 0;
	if(rdn->rdn!=NULL)
	{
		char *value;
		int atindex= slapi_rdn_get_index_attr(rdn, type, &value);
		if(atindex!=-1)
		{
			result= slapi_rdn_remove_index(rdn, atindex);
		}
	}
	return result;
}

int
slapi_rdn_isempty(const Slapi_RDN *rdn)
{
	return (rdn->rdn==NULL || rdn->rdn[0]=='\0');
}

int
slapi_rdn_get_num_components(Slapi_RDN *rdn)
{
	int i= 0;
	char **rdns= slapi_rdn_get_rdns(rdn);
	if(rdns!=NULL)
	{
		for(i=0; rdns[i]!=NULL; i++);
	}
	return i;
}

int
slapi_rdn_compare(Slapi_RDN *rdn1, Slapi_RDN *rdn2)
{
	int r= 1;
	int n1= slapi_rdn_get_num_components(rdn1);
	int n2= slapi_rdn_get_num_components(rdn2);
	if (n1==n2)
	{
		char *type, *value;
		int i= slapi_rdn_get_first(rdn1, &type, &value);
		while(r==1 && i!=-1)
		{
			r= slapi_rdn_contains(rdn2, type, value, strlen(value));
			i= slapi_rdn_get_next(rdn1, i, &type, &value);
		}
		if(r==1) /* All rdn1's rdn components were in rdn2 */
		{
			r= 0; /* SAME */
		}
		else
		{
			r= -1; /* NOT SAME */
		}
	}
	else
	{
		r= -1; /* NOT SAME */
	}
	return r;
}

const char *
slapi_rdn_get_rdn(const Slapi_RDN *srdn)
{
	return srdn->rdn;
}

/*
 * if src is set, make a copy and return in inplace
 * if *inplace is set, try to use that in place, or
 * free it and set to a new value
 */
static void
normalize_case_helper(const char *copy, char **inplace)
{
    int rc;
    char **newdnaddr = NULL;
    char *newdn = NULL;
    char *dest = NULL;
    size_t dest_len = 0;

    if (!inplace) { /* no place to put result */
        return;
    }

    if (!copy && !*inplace) { /* no string to operate on */
        return;
    }

    if (copy) {
        newdn = slapi_ch_strdup(copy);
        newdnaddr = &newdn;
    } else {
        newdnaddr = inplace;
    }

    rc = slapi_dn_normalize_case_ext(*newdnaddr, 0, &dest, &dest_len);
    if (rc < 0) {
        /* we give up, just case normalize in place */
        slapi_dn_ignore_case(*newdnaddr); /* ignore case */
    } else if (rc == 0) {
        /* dest points to *newdnaddr - normalized in place */
        *(dest + dest_len) = '\0';
    } else {
        /* dest is a new string */
        slapi_ch_free_string(newdnaddr);
        *newdnaddr = dest;
    }

    *inplace = *newdnaddr;
    return;
}

/* srdn is updated in the function, it cannot be const */
const char *
slapi_rdn_get_nrdn(Slapi_RDN *srdn)
{
	if (NULL == srdn || NULL == srdn->rdn)
	{
		return NULL;
	}
	if (NULL == srdn->nrdn)
	{
		if (srdn->all_nrdns && srdn->all_nrdns[0])
		{
			srdn->nrdn = slapi_ch_strdup(srdn->all_nrdns[0]);
		}
		else if (srdn->all_rdns && srdn->all_rdns[0])
		{
			srdn->nrdn = slapi_ch_strdup(srdn->all_rdns[0]);
			slapi_dn_ignore_case(srdn->nrdn);
		}
		else
		{
			normalize_case_helper(srdn->rdn, &srdn->nrdn);
		}
	}
	return (const char *)srdn->nrdn;
}

/*
 * Get the leaf (first) rdn from rdns or nrdns array depending on the flag
 *
 * flag: FLAG_ALL_RDNS -- raw (not normalized)
 *     : FLAG_ALL_NRDNS -- normalized
 *
 * Output: first rdn
 *
 * Return value: the index of the first rdn.
 *             : -1, if failed
 */
int
slapi_rdn_get_first_ext(Slapi_RDN *srdn, const char **firstrdn, int flag)
{
	char **ptr = NULL;
	int idx = -1;

	if (NULL == firstrdn)
	{
		return idx;
	}
	*firstrdn = NULL;
	if (NULL == srdn)
	{
		return idx;
	}

	if (FLAG_ALL_RDNS == flag)
	{
		ptr = srdn->all_rdns;
	}
	else if (FLAG_ALL_NRDNS == flag)
	{
		if (NULL == srdn->all_nrdns)
		{
			srdn->all_nrdns = charray_dup(srdn->all_rdns);
			for (ptr = srdn->all_nrdns; ptr && *ptr; ptr++)
			{
				normalize_case_helper(NULL, ptr);
			}
		}
		ptr = srdn->all_nrdns;
	}
	if (ptr)
	{
		*firstrdn = *ptr;
		idx = 0;
	}

	return idx;
}

/*
 * Get the top (last) rdn from rdns or nrdns array depending on the flag
 *
 * flag: FLAG_ALL_RDNS -- raw (not normalized)
 *     : FLAG_ALL_NRDNS -- normalized
 *
 * Output: last rdn
 *
 * Return value: the index of the last rdn.
 *             : -1, if failed
 */
int
slapi_rdn_get_last_ext(Slapi_RDN *srdn, const char **lastrdn, int flag)
{
	char **ptr = NULL;
	int idx = -1;

	if (NULL == lastrdn)
	{
		return idx;
	}
	*lastrdn = NULL;
	if (NULL == srdn)
	{
		return idx;
	}

	if (FLAG_ALL_RDNS == flag)
	{
		ptr = srdn->all_rdns;
	}
	else if (FLAG_ALL_NRDNS == flag)
	{
		if (NULL == srdn->all_nrdns)
		{
			srdn->all_nrdns = charray_dup(srdn->all_rdns);
			for (ptr = srdn->all_nrdns; ptr && *ptr; ptr++)
			{
				normalize_case_helper(NULL, ptr);
			}
		}
		ptr = srdn->all_nrdns;
	}
	if (ptr) 
	{
		for ( ; ptr && *ptr; ptr++) idx++;
		ptr--;
	}
	if (ptr)
	{
		*lastrdn = *ptr;
	}

	return idx;
}

/*
 * Get the previous rdn of the given index (idx) from rdns or nrdns array 
 * depending on the flag -> rdn
 *
 * flag: FLAG_ALL_RDNS -- raw (not normalized)
 *     : FLAG_ALL_NRDNS -- normalized
 *
 * Output: prevrdn
 *
 * Return value: the index of the returned rdn.
 *             : -1, if failed or done
 */
int
slapi_rdn_get_prev_ext(Slapi_RDN *srdn, int idx, const char **prevrdn, int flag)
{
	int rc = -1;

	if (NULL == prevrdn)
	{
		return rc;
	}
	*prevrdn = NULL;
	if (NULL == srdn || idx <= 0)
	{
		return rc;
	}

	if (FLAG_ALL_RDNS == flag)
	{
		*prevrdn = srdn->all_rdns[idx-1];
		rc = idx - 1;
	}
	else if (FLAG_ALL_NRDNS == flag)
	{
		*prevrdn = srdn->all_nrdns[idx-1];
		rc = idx - 1;
	}

	return rc;
}

/*
 * Get the next rdn of the given index (idx) from rdns or nrdns array 
 * depending on the flag -> rdn
 *
 * flag: FLAG_ALL_RDNS -- raw (not normalized)
 *     : FLAG_ALL_NRDNS -- normalized
 *
 * Output: nextrdn
 *
 * Return value: the index of the returned rdn.
 *             : -1, if failed or done
 */
int
slapi_rdn_get_next_ext(Slapi_RDN *srdn, int idx, const char **nextrdn, int flag)
{
	int rc = -1;

	if (NULL == nextrdn)
	{
		return rc;
	}
	*nextrdn = NULL;
	if (NULL == srdn || idx < 0)
	{
		return rc;
	}

	if (FLAG_ALL_RDNS == flag)
	{
		*nextrdn = srdn->all_rdns[idx+1];
		rc = idx + 1;
	}
	else if (FLAG_ALL_NRDNS == flag)
	{
		*nextrdn = srdn->all_nrdns[idx+1];
		rc = idx + 1;
	}

	return rc;
}

/*
 * addrdn is going to be freed when rdn is freed if byref is 0
 * if byref is non 0, the caller should not free it.
 */
int
slapi_rdn_add_rdn_to_all_rdns(Slapi_RDN *srdn, char *addrdn, int byref)
{
	if (NULL == srdn || NULL == addrdn || '\0' == *addrdn)
	{
		return -1;
	}
	charray_add(&(srdn->all_rdns), byref?addrdn:slapi_ch_strdup(addrdn));
	return 0;
}

int
slapi_rdn_add_srdn_to_all_rdns(Slapi_RDN *srdn, Slapi_RDN *addsrdn)
{
	if (NULL == srdn || NULL == addsrdn)
	{
		return -1;
	}
	if (NULL == srdn->rdn)
	{
		srdn->rdn = slapi_ch_strdup(addsrdn->rdn);
	}
	charray_merge(&(srdn->all_rdns), addsrdn->all_rdns, 1 /* copy */);
	return 0;
}

/*
 * Get estimated DN length from all_rdns
 * If srdn is NULL or srdn does not have all_rdns, it returns -1;
 */
int
slapi_rdn_get_dn_len(Slapi_RDN *srdn)
{
	size_t len = -1;
	char **rdnp = NULL;

	if (NULL == srdn || NULL == srdn->all_rdns)
	{
		return len;
	}
	len = 0;
	for (rdnp = srdn->all_rdns; rdnp && *rdnp; rdnp++) {
		len += strlen(*rdnp) + 1; /* 1 for ',' */
	}
	len += 1;

	return len;
}

/*
 * Generate DN string from all_rdns
 * If srdn is NULL or srdn does not have all_rdns, it returns -1;
 */
int
slapi_rdn_get_dn(Slapi_RDN *srdn, char **dn)
{
	size_t len = 0;
	char **rdnp = NULL;
	char *ptr = NULL;
	char *enddn = NULL;

	if (NULL == srdn || NULL == srdn->all_rdns || NULL == dn)
	{
		return -1;
	}
	for (rdnp = srdn->all_rdns; rdnp && *rdnp; rdnp++) {
		len += strlen(*rdnp) + 1; /* 1 for ',' */
	}
	len += 1;
	len = slapi_rdn_get_dn_len(srdn);
	*dn = (char *)slapi_ch_malloc(len);
	enddn = *dn + len - 1;
	ptr = *dn;
	for (rdnp = srdn->all_rdns; rdnp && *rdnp; rdnp++) {
		size_t mylen = strlen(*rdnp) + 1;
		if (ptr + mylen > enddn) {
			slapi_ch_free_string(dn);
			*dn = NULL;
			return -1;
		}
		PR_snprintf(ptr, len, "%s,", *rdnp);
		len -= mylen;
		ptr += mylen;
	}
	ptr = strrchr(*dn, ',');
	if (ptr) {
		*ptr = '\0';
		return 0;
	} else {
		slapi_ch_free_string(dn);
		*dn = NULL;
		return -1;
	}
}

int
slapi_srdn_copy(const Slapi_RDN *from, Slapi_RDN *to)
{
	if (NULL == from || NULL == to)
	{
		return -1;
	}
	slapi_rdn_done(to);
	to->rdn = slapi_ch_strdup(from->rdn);
	to->rdns = charray_dup(from->rdns);
	to->all_rdns = charray_dup(from->all_rdns);
	to->all_nrdns = charray_dup(from->all_nrdns);
	return 0;
}

int
slapi_rdn_replace_rdn(Slapi_RDN *srdn, char *new_rdn)
{
	if (NULL == srdn)
	{
		return -1;
	}

	slapi_ch_free_string(&(srdn->rdn));
	slapi_ch_free_string(&(srdn->nrdn));
	srdn->rdn = slapi_ch_strdup(new_rdn);
	normalize_case_helper(new_rdn, &srdn->nrdn);

	if (srdn->all_rdns)
	{
		slapi_ch_free_string(&(srdn->all_rdns[0]));
		srdn->all_rdns[0] = slapi_ch_strdup(srdn->rdn);
	}
	if (srdn->all_nrdns)
	{
		slapi_ch_free_string(&(srdn->all_nrdns[0]));
		srdn->all_nrdns[0] = slapi_ch_strdup(srdn->nrdn);
	}

	return 0;
}

int
slapi_rdn_partial_dup(Slapi_RDN *from, Slapi_RDN **to, int rdnidx)
{
	char **ptr = NULL;
	int lastidx = -1;

	if (NULL == from || NULL == to || rdnidx < 0)
	{
		return -1;
	}
	*to = NULL;

	for (ptr = from->all_rdns; ptr && *ptr; ptr++) lastidx++;
	if (rdnidx > lastidx)
	{
		return -1;
	}

	if (NULL == from->all_nrdns)
	{
		from->all_nrdns = charray_dup(from->all_rdns);
		for (ptr = from->all_nrdns; ptr && *ptr; ptr++)
		{
			normalize_case_helper(NULL, ptr);
		}
	}

	*to = slapi_rdn_new();

	(*to)->rdn = slapi_ch_strdup(from->all_rdns[rdnidx]);
	(*to)->nrdn = slapi_ch_strdup(from->all_nrdns[rdnidx]);
	(*to)->all_rdns = charray_dup(&(from->all_rdns[rdnidx]));
	(*to)->all_nrdns = charray_dup(&(from->all_nrdns[rdnidx]));
	return 0;
}

size_t
slapi_rdn_get_size(Slapi_RDN *srdn)
{
	size_t sz = 0;
	char **ptr;

	if (!srdn) {
		goto bail;
	}
	sz = sizeof(Slapi_RDN);
	if (srdn->rdn) {
		sz += strlen(srdn->rdn) + 1;
	}
	if (srdn->nrdn) {
		sz += strlen(srdn->nrdn) + 1;
	}
	if (srdn->rdns) {
		for (ptr = srdn->rdns; ptr && *ptr; ptr++) {
			sz += strlen(*ptr) + 1;
		}
	}
	if (srdn->all_rdns) {
		for (ptr = srdn->all_rdns; ptr && *ptr; ptr++) {
			sz += strlen(*ptr) + 1;
		}
	}
	if (srdn->all_nrdns) {
		for (ptr = srdn->all_nrdns; ptr && *ptr; ptr++) {
			sz += strlen(*ptr) + 1;
		}
	}
bail:
	return sz;
}

