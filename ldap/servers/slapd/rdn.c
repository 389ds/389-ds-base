/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "slap.h"

#define FLAG_RDNS 0

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
	rdn->rdns= NULL;
	rdn->butcheredupto= -1; /* Means we haven't started converting '=' to '\0' in rdns */
}

void
slapi_rdn_init_dn(Slapi_RDN *rdn,const char *dn)
{
	slapi_rdn_init(rdn);
	if(dn!=NULL)
	{
		char **dns= ldap_explode_dn(dn, 0);
		if(dns!=NULL)
		{
			rdn->rdn= slapi_ch_strdup(dns[0]);
	    	ldap_value_free(dns);
		}
	}
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
		    ldap_value_free(rdn->rdns);
			rdn->rdns= NULL;
		}
		if(rdn->rdn!=NULL)
		{
		   	rdn->rdns = ldap_explode_rdn( rdn->rdn, 0 );
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
	    slapi_ch_free((void**)&(rdn->rdn));
	    ldap_value_free(rdn->rdns);
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
		    *type= strchr(rdn->rdns[index],'=');
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
slapi_rdn_get_rdn(const Slapi_RDN *rdn)
{
	return rdn->rdn;
}

const char *
slapi_rdn_get_nrdn(const Slapi_RDN *rdn)
{
	/* JCM - Normalised RDN? */
	PR_ASSERT(0);
	return NULL;
}
