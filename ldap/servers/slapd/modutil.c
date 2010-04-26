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

/* modutil.c - modify utility routine */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif /* _WIN32 */					 
#include "slap.h"

#define SIZE_INIT 4	/* initial element size */
#define SIZE_INC  2	/* size increment */
/*
 * Free an array of LDAPMod structures.  Just like ldap_mods_free,
 * except that it assumes that the mods are expressed as a bervec.
 */
void
freepmods( LDAPMod **pmods )
{
    int	i;

    for ( i = 0; pmods[ i ] != NULL; ++i ) {
	if ( pmods[ i ]->mod_bvalues != NULL ) {
	    ber_bvecfree( pmods[ i ]->mod_bvalues );
	}
	if ( pmods[ i ]->mod_type != NULL ) {
	    slapi_ch_free((void**)&pmods[ i ]->mod_type );
	}
	slapi_ch_free((void**)&pmods[ i ] );
    }
    slapi_ch_free((void**)&pmods );
}

/* ======= Utility functions for manipulating a list of LDAPMods ======= */

/*
 * Slapi_Mods can be used in two ways:
 * 1) To wrap an existing array of LDAPMods, or
 * 2) To create a new array of LDAPMods.
 *
 * Slapi_Mods provides memory management and array manipulation
 * functions to make using (LDAPMod**) easier.
 *
 */

Slapi_Mods* 
slapi_mods_new()
{
	Slapi_Mods *mods;
	mods = (Slapi_Mods*) slapi_ch_calloc (1, sizeof (Slapi_Mods));
	return mods;
}

/*
 * Initialise a Slapi_Mod suggesting how big an LDAPMod array is needed.
 * It will be free'd when the Slapi_Mods is destroyed.
 */
void
slapi_mods_init(Slapi_Mods *smods, int initCount)
{    
	memset (smods, 0, sizeof (*smods));
	smods->free_mods = 1;
	if (initCount > 0)
	{
		smods->num_elements= initCount + 1;	/* one for NULL element */
		smods->mods = (LDAPMod **) slapi_ch_calloc( 1, smods->num_elements * sizeof(LDAPMod *) );	
	}
}

/*
 * Initialise a Slapi_Mod passing in responsibility for the (LDAPMod **).
 * It will be free'd when the Slapi_Mods is destroyed.
 */
void
slapi_mods_init_passin(Slapi_Mods *smods, LDAPMod **mods)
{    
	slapi_mods_init_byref(smods, mods);
	smods->free_mods = 1;
}

/*
 * Initialise a Slapi_Mod passing in a reference to the (LDAPMod **)
 * It will *not* be free'd when the Slapi_Mods is destroyed.
 */
void
slapi_mods_init_byref(Slapi_Mods *smods, LDAPMod **mods)
{    
	memset (smods, 0, sizeof (*smods));
	if(mods!=NULL)
	{
		smods->mods = mods;
		for ( smods->num_mods = 0; mods[smods->num_mods] != NULL; smods->num_mods++ );
		smods->num_elements= smods->num_mods+1; /* We assume there's nothing spare on the end. */
	}
}

void 
slapi_mods_free(Slapi_Mods **smods)
{
	if(smods!=NULL && *smods!=NULL)
	{
		slapi_mods_done(*smods);
		slapi_ch_free ((void**)smods);
		*smods= NULL;
	}
}

void 
slapi_mods_done(Slapi_Mods *smods)
{
	PR_ASSERT(smods!=NULL);
	if (smods->mods!=NULL)
	{
		if(smods->free_mods)
		{
			ldap_mods_free (smods->mods, 1 /* Free the Array and the Elements */);
		}
	}
	memset (smods, 0, sizeof(smods));
}

static void
slapi_mods_add_one_element(Slapi_Mods *smods)
{
	int	need = smods->num_mods + 2;
	if ( smods->num_elements == 0 )
	{
		PR_ASSERT(smods->mods==NULL);
		smods->num_elements = SIZE_INIT;
		smods->mods = (LDAPMod **) slapi_ch_malloc( smods->num_elements * sizeof(LDAPMod *) );
		smods->free_mods= 1;
	}
	if ( smods->num_elements < need )
	{
		PR_ASSERT(smods->free_mods);
		smods->num_elements *= SIZE_INC;
		smods->mods = (LDAPMod **) slapi_ch_realloc( (char *) smods->mods, smods->num_elements * sizeof(LDAPMod *) );
	}
}

/*
 * Shift everything down to make room to insert the new mod. 
 */
void
slapi_mods_insert_at(Slapi_Mods *smods, LDAPMod *mod, int pos)
{
	int	i;
	Slapi_Attr a = {0};

	if (NULL == mod) {
		return;
	}
	slapi_mods_add_one_element(smods);
	for( i=smods->num_mods-1; i>=pos; i--)
	{
	    smods->mods[i+1]= smods->mods[i];
	}
	slapi_attr_init(&a, mod->mod_type);
	/* Check if the type of the to-be-added values has DN syntax or not. */
	if (slapi_attr_is_dn_syntax_attr(&a)) {
		int rc = 0;
		struct berval **mbvp = NULL;
		char *normed = NULL;
		size_t len = 0;
		for (mbvp = mod->mod_bvalues; mbvp && *mbvp; mbvp++) {
			rc = slapi_dn_normalize_ext((*mbvp)->bv_val, (*mbvp)->bv_len,
										&normed, &len);
			if (rc > 0) {
				slapi_ch_free((void **)&((*mbvp)->bv_val));
			} else if (rc == 0) { 
				/* original is passed in; not null terminated */
				*(normed + len) = '\0';
			}
			(*mbvp)->bv_val = normed;
			(*mbvp)->bv_len = len;
		}
	}
	attr_done(&a);
	smods->mods[pos]= mod;
	smods->num_mods++;
	smods->mods[smods->num_mods]= NULL;
}

void 
slapi_mods_insert_smod_at(Slapi_Mods *smods, Slapi_Mod *smod, int pos)
{
	slapi_mods_insert_at (smods, smod->mod, pos);	
}

/*
 * Shift everything down to make room to insert the new mod. 
 */
void
slapi_mods_insert_before(Slapi_Mods *smods, LDAPMod *mod)
{
    slapi_mods_insert_at(smods, mod, smods->iterator);
	smods->iterator++;
}

void 
slapi_mods_insert_smod_before(Slapi_Mods *smods, Slapi_Mod *smod)
{
	slapi_mods_insert_before(smods, smod->mod);
}

/*
 * Shift everything down to make room to insert the new mod. 
 */
void
slapi_mods_insert_after(Slapi_Mods *smods, LDAPMod *mod)
{
    slapi_mods_insert_at(smods, mod, smods->iterator+1);
}

void slapi_mods_insert_smod_after(Slapi_Mods *smods, Slapi_Mod *smod)
{
	slapi_mods_insert_after(smods, smod->mod);
}

/*
 * Add the LDAPMod to the end of the array.
 * Does NOT copy the mod.
 */
void
slapi_mods_add_ldapmod(Slapi_Mods *smods, LDAPMod *mod)
{
    slapi_mods_insert_at(smods,mod,smods->num_mods);
}

void 
slapi_mods_add_smod(Slapi_Mods *smods, Slapi_Mod *smod)
{
	slapi_mods_add_ldapmod(smods, smod->mod);
}

/*
 * Makes a copy of everything.
 */
void
slapi_mods_add_modbvps( Slapi_Mods *smods, int modtype, const char *type, struct berval **bvps )
{
	LDAPMod *mod;

	mod = (LDAPMod *) slapi_ch_malloc(sizeof(LDAPMod));
	mod->mod_type = slapi_ch_strdup( type );
	mod->mod_op = modtype | LDAP_MOD_BVALUES;
	mod->mod_bvalues = NULL;
	
	if (NULL != bvps)
	{
		int num_values, i;
		num_values = 0;
		/* Count mods */
		while (NULL != bvps[num_values])
		{
			num_values++;
		}
		mod->mod_bvalues = (struct berval **)slapi_ch_malloc((num_values + 1) *
			sizeof(struct berval *));
		for (i = 0; i < num_values; i++)
		{
			mod->mod_bvalues[i] = ber_bvdup((struct berval *)bvps[i]); /* jcm had to cast away const */
		}
		mod->mod_bvalues[num_values] = NULL;
	}
	slapi_mods_add_ldapmod(smods, mod);
}

/*
 * Makes a copy of everything.
 */
void
slapi_mods_add_mod_values( Slapi_Mods *smods, int modtype, const char *type, Slapi_Value **va )
{
    LDAPMod *mod= (LDAPMod *) slapi_ch_malloc( sizeof(LDAPMod) );
    mod->mod_type = slapi_ch_strdup( type );
    mod->mod_op = modtype | LDAP_MOD_BVALUES;
	mod->mod_bvalues= NULL;
	valuearray_get_bervalarray(va,&mod->mod_bvalues);
    slapi_mods_add_ldapmod(smods, mod);
}

/*
 * Makes a copy of everything.
 */
void
slapi_mods_add( Slapi_Mods *smods, int modtype, const char *type, unsigned long len, const char *val)
{
	struct berval bv;
	struct berval *bvps[2];
	if(len>0)
	{
		bv.bv_len= len;
		bv.bv_val= (void*)val; /* We cast away the const, but we're not going to change anything */
		bvps[0] = &bv;
		bvps[1] = NULL;
	}
	else
	{
		bvps[0]= NULL;
	}
    slapi_mods_add_modbvps( smods, modtype, type, bvps );
}

/*
 * Makes a copy of everything.
 */
void
slapi_mods_add_string( Slapi_Mods *smods, int modtype, const char *type, const char *val)
{
	PR_ASSERT(val);
	slapi_mods_add( smods, modtype, type, strlen(val), val);
}

void
slapi_mods_remove(Slapi_Mods *smods)
{
	smods->mods[smods->iterator]->mod_op= LDAP_MOD_IGNORE;
}

LDAPMod *
slapi_mods_get_first_mod(Slapi_Mods *smods)
{
    /* Reset the iterator in the mod structure */
    smods->iterator= -1;
	return slapi_mods_get_next_mod(smods);
}

LDAPMod *
slapi_mods_get_next_mod(Slapi_Mods *smods)
{
    /* Move the iterator forward */
    LDAPMod *r= NULL;
	smods->iterator++;

	PR_ASSERT (smods->iterator >= 0);

	/* skip deleted mods if any */
	while (smods->iterator < smods->num_mods && smods->mods[smods->iterator]->mod_op == LDAP_MOD_IGNORE)
		smods->iterator ++;
	
	if(smods->iterator<smods->num_mods)
	{
	    r= smods->mods[smods->iterator];
	}
	return r;
}

static void 
mod2smod (LDAPMod *mod, Slapi_Mod *smod)
{
	smod->mod = mod;
	smod->iterator = 0;
	smod->num_values = 0;

	if (mod->mod_op & LDAP_MOD_BVALUES)
	{
		while (mod->mod_bvalues && mod->mod_bvalues[smod->num_values])
		{
			smod->num_values ++;
		}
	}
	else
	{
		PR_ASSERT(0); /* ggood shouldn't ever use string values in server */
		while (mod->mod_values && mod->mod_values[smod->num_values])
		{
			smod->num_values ++;
		}
	}

	smod->num_elements = smod->num_values + 1; /* 1- for null char */
}

Slapi_Mod *
slapi_mods_get_first_smod(Slapi_Mods *smods, Slapi_Mod *smod)
{
	LDAPMod *mod = slapi_mods_get_first_mod (smods);

	if (mod == NULL)
		return NULL;

	mod2smod (mod, smod);
	
	return smod;
}

Slapi_Mod *
slapi_mods_get_next_smod(Slapi_Mods *smods, Slapi_Mod *smod)
{
	LDAPMod *mod = slapi_mods_get_next_mod(smods);

	if (mod == NULL)
	{
		return NULL;
	}
	else
	{
		mod2smod(mod, smod);
	}
	return smod;
}

void
slapi_mods_iterator_backone(Slapi_Mods *smods)
{
    smods->iterator--;
}


static void
pack_mods(LDAPMod ***modsp)
{
	LDAPMod **mods = NULL;
	if (NULL != modsp && NULL != *modsp)
	{
		int i;
		int num_slots;
		int src_index, dst_index;
		mods = *modsp;

		/* Make a pass through the array, freeing any marked LDAP_MODS_IGNORE */
		i = 0;
		while (NULL != mods[i])
		{
			if (LDAP_MOD_IGNORE == (mods[i]->mod_op & ~LDAP_MOD_BVALUES))
			{
			  /* Free current slot */
			  slapi_ch_free((void**)&mods[i]->mod_type);
			  ber_bvecfree(mods[i]->mod_bvalues);
			  slapi_ch_free((void **)&mods[i]);
			}
			i++;
		}
		num_slots = i + 1; /* Remember total number of slots */

		/* Make another pass, packing the array */
		dst_index = src_index = 0;
		while (src_index < num_slots)
		{
			if (NULL != mods[src_index])
			{
				mods[dst_index] = mods[src_index];
				dst_index++;
			}
			src_index++;
		}
		mods[dst_index] = NULL;
		if (NULL == mods[0])
		{
			/* Packed it down to size zero - deallocate */
			slapi_ch_free((void **)modsp);
		}
	}
}


LDAPMod **
slapi_mods_get_ldapmods_byref(Slapi_Mods *smods)
{
	pack_mods(&smods->mods); /* XXXggood const gets in the way of this */
    return smods->mods;
}

LDAPMod **
slapi_mods_get_ldapmods_passout(Slapi_Mods *smods)
{
    LDAPMod **mods; 
	pack_mods(&smods->mods);
    mods = smods->mods;
	smods->free_mods = 0;
	slapi_mods_done(smods);
	return mods;
}

int
slapi_mods_get_num_mods(const Slapi_Mods *smods)
{
    return smods->num_mods;
}

void
slapi_mods_dump(const Slapi_Mods *smods, const char *text)
{
	int i;
	LDAPDebug( LDAP_DEBUG_ANY, "smod - %s\n", text, 0, 0);
	for(i=0;i<smods->num_mods;i++)
	{
		slapi_mod_dump(smods->mods[i],i);
	}
}

/* ======== Utility functions for manipulating an LDAPMod ======= */

/*
 * Slapi_Mod can be used in two ways:
 * 1) To wrap an existing array of LDAPMods, or
 * 2) To create a new array of LDAPMods.
 *
 * Slapi_Mods provides memory management and array manipulation
 * functions to make using (LDAPMod**) easier.
 *
 */

Slapi_Mod *
slapi_mod_new ()
{
	Slapi_Mod *mod = (Slapi_Mod*)slapi_ch_calloc (1, sizeof (Slapi_Mod));
	return mod;
}

void
slapi_mod_init(Slapi_Mod *smod, int initCount)
{
	PR_ASSERT(smod!=NULL);
	memset (smod, 0, sizeof (*smod));
	smod->free_mod= 1;
	smod->num_elements = initCount + 1;
	smod->mod = (LDAPMod *)slapi_ch_calloc (1, sizeof (LDAPMod));
	if (smod->num_elements)
		smod->mod->mod_bvalues = (struct berval**)slapi_ch_calloc (smod->num_elements, sizeof (struct berval*));
}

void
slapi_mod_init_passin(Slapi_Mod *smod, LDAPMod *mod)
{
	PR_ASSERT(smod!=NULL);
	memset (smod, 0, sizeof (*smod));
	smod->free_mod= 1;
	if (mod!=NULL)
	{
		smod->mod= mod;
		if(smod->mod->mod_bvalues!=NULL)
		{
			while (smod->mod->mod_bvalues[smod->num_values]!=NULL)
				smod->num_values++;
			smod->num_elements= smod->num_values +1; /* We assume there's nothing spare on the end. */
		}
	}
}

void
slapi_mod_init_byref(Slapi_Mod *smod, LDAPMod *mod)
{
	PR_ASSERT(smod!=NULL);
	memset (smod, 0, sizeof (*smod));
	if (mod!=NULL)
	{
		smod->mod= mod;
		if(smod->mod->mod_bvalues!=NULL)
		{
			while (smod->mod->mod_bvalues[smod->num_values]!=NULL)
				smod->num_values++;
			smod->num_elements= smod->num_values +1; /* We assume there's nothing spare on the end. */
		}
	}
}

void 
slapi_mod_init_byval (Slapi_Mod *smod, const LDAPMod *mod)
{
	PR_ASSERT(smod!=NULL);
	memset (smod, 0, sizeof (*smod));
	if (mod!=NULL)
	{
		smod->mod = (LDAPMod *)slapi_ch_calloc (1, sizeof (LDAPMod));
		smod->free_mod = 1;
		slapi_mod_set_operation (smod, mod->mod_op);
		slapi_mod_set_type (smod, mod->mod_type);
		if(mod->mod_bvalues!=NULL)
		{
			while (mod->mod_bvalues[smod->num_values]!=NULL)
			{
				slapi_mod_add_value (smod, mod->mod_bvalues[smod->num_values]);
			}
		}
	}
}

void
slapi_mod_init_valueset_byval(Slapi_Mod *smod, int op, const char *type, const Slapi_ValueSet *svs)
{
	PR_ASSERT(smod!=NULL);
	slapi_mod_init(smod, 0);
	slapi_mod_set_operation (smod, op);
	slapi_mod_set_type (smod, type);
	if (svs!=NULL) {
		Slapi_Value **svary = valueset_get_valuearray(svs);
		ber_bvecfree(smod->mod->mod_bvalues);
		smod->mod->mod_bvalues = NULL;
		valuearray_get_bervalarray(svary, &smod->mod->mod_bvalues);
		smod->num_values = slapi_valueset_count(svs);
		smod->num_elements = smod->num_values + 1;
	}
}

void
slapi_mod_free (Slapi_Mod **smod)
{
	slapi_mod_done(*smod);
	slapi_ch_free((void**)smod);
	*smod= NULL;
}

void
slapi_mod_done(Slapi_Mod *smod)
{
	PR_ASSERT(smod!=NULL);
	if(smod->free_mod)
	{
		ber_bvecfree(smod->mod->mod_bvalues);
		slapi_ch_free((void**)&(smod->mod->mod_type));
		slapi_ch_free((void**)&(smod->mod));
	}
	memset (smod, 0, sizeof(smod));
}

/*
 * Add a value to the list of values in the modification.
 */
void
slapi_mod_add_value(Slapi_Mod *smod, const struct berval *val)
{
	PR_ASSERT(smod!=NULL);
	PR_ASSERT(val!=NULL);
/*	PR_ASSERT(slapi_mod_get_operation(smod) & LDAP_MOD_BVALUES);*/
	
    bervalarray_add_berval_fast(&(smod->mod->mod_bvalues),(struct berval*)val,smod->num_values,&smod->num_elements);
	smod->num_values++;
}

/*
 * Remove the value at the iterator from the list of values in
 * the LDAP modification. 
 */
void
slapi_mod_remove_value(Slapi_Mod *smod)
{
    /* loop over the mod values moving them down to cover up the value to be removed */
	struct berval **vals;
	int i, k;
	PR_ASSERT(smod!=NULL);
	PR_ASSERT(smod->mod!=NULL);
	vals= smod->mod->mod_bvalues;
	i= smod->iterator-1;
	ber_bvfree( vals[i] );
	for ( k = i + 1; vals[k] != NULL; k++ ) {
		vals[k - 1] = vals[k];
	}
	vals[k - 1] = NULL;
    /* Reset the iterator */
	smod->num_values--;
    smod->iterator--;
}

struct berval *
slapi_mod_get_first_value(Slapi_Mod *smod)
{
	PR_ASSERT(smod!=NULL);
    /* Reset the iterator in the mod structure */
    smod->iterator= 0;
	return slapi_mod_get_next_value(smod);
}

struct berval *
slapi_mod_get_next_value(Slapi_Mod *smod)
{
    /* Move the iterator forward */
    struct berval *r= NULL;
	PR_ASSERT(smod!=NULL);
	PR_ASSERT(smod->mod!=NULL);
	if(smod->iterator<smod->num_values)
	{
	    r= smod->mod->mod_bvalues[smod->iterator];
	    smod->iterator++;
	}
	return r;
}

int
slapi_mod_get_num_values(const Slapi_Mod *smod)
{
	PR_ASSERT(smod!=NULL);
    return smod->num_values;
}

const char *
slapi_mod_get_type (const Slapi_Mod *smod)
{
	PR_ASSERT(smod!=NULL);
	PR_ASSERT(smod->mod!=NULL);
	return smod->mod->mod_type;
}

int 
slapi_mod_get_operation (const Slapi_Mod *smod)
{
	PR_ASSERT(smod!=NULL);
	PR_ASSERT(smod->mod!=NULL);
	return smod->mod->mod_op;
}

void 
slapi_mod_set_type (Slapi_Mod *smod, const char *type)
{ 
	PR_ASSERT(smod!=NULL);
	PR_ASSERT(smod->mod!=NULL);
	if(smod->mod->mod_type!=NULL)
	{
		slapi_ch_free((void**)&smod->mod->mod_type);
	}
	smod->mod->mod_type = slapi_ch_strdup (type);
}

void 
slapi_mod_set_operation (Slapi_Mod *smod, int op)
{
	PR_ASSERT(smod!=NULL);
	PR_ASSERT(smod->mod!=NULL);
	smod->mod->mod_op = op;
}

const LDAPMod *
slapi_mod_get_ldapmod_byref(const Slapi_Mod *smod)
{
	PR_ASSERT(smod!=NULL);
	return smod->mod;
}

LDAPMod *
slapi_mod_get_ldapmod_passout(Slapi_Mod *smod)
{
	LDAPMod *mod;
	PR_ASSERT(smod!=NULL);
	mod= smod->mod;
	smod->free_mod= 0;
	slapi_mod_done(smod);
	return mod;
}

/* a valid LDAPMod is one with operation of LDAP_MOD_ADD, *_DELETE, *_REPLACE
   ored with LDAP_MOD_BVALUES; non-null type and at list one value
   for add and replace operations
 */
int 
slapi_mod_isvalid (const Slapi_Mod *mod)
{
	int op;

	if (mod == NULL || mod->mod == NULL)
		return 0;

	op = mod->mod->mod_op;

	if (!SLAPI_IS_MOD_ADD(op) && !SLAPI_IS_MOD_DELETE(op) && !SLAPI_IS_MOD_REPLACE(op))
		return 0;

	if (mod->mod->mod_type == NULL)
		return 0;

	/* add op must have at least 1 value */
	if (SLAPI_IS_MOD_ADD(op) && (mod->num_values == 0))
		return 0;

	return 1;
}

void
slapi_mod_dump(LDAPMod *mod, int n)
{
	if(mod!=NULL)
	{
		int operationtype= mod->mod_op & ~LDAP_MOD_BVALUES;
	    switch ( operationtype )
		{
	    case LDAP_MOD_ADD:
			LDAPDebug( LDAP_DEBUG_ANY, "smod %d - add: %s\n", n, mod->mod_type, 0);
			break;

	    case LDAP_MOD_DELETE:
			LDAPDebug( LDAP_DEBUG_ANY, "smod %d - delete: %s\n", n, mod->mod_type, 0);
			break;

	    case LDAP_MOD_REPLACE:
			LDAPDebug( LDAP_DEBUG_ANY, "smod %d - replace: %s\n", n, mod->mod_type, 0);
			break;

	    case LDAP_MOD_IGNORE:
			LDAPDebug( LDAP_DEBUG_ANY, "smod %d - ignore: %s\n", n, mod->mod_type, 0);
			break;
	    }
		if(operationtype!=LDAP_MOD_IGNORE)
		{
			int i;
			for ( i = 0; mod->mod_bvalues != NULL && mod->mod_bvalues[i] != NULL; i++ )
			{
				char *buf, *bufp;
				int len = strlen( mod->mod_type );
				len = LDIF_SIZE_NEEDED( len, mod->mod_bvalues[i]->bv_len ) + 1;
				buf = slapi_ch_malloc( len );
				bufp = buf;
				slapi_ldif_put_type_and_value_with_options( &bufp, mod->mod_type, mod->mod_bvalues[i]->bv_val,	mod->mod_bvalues[i]->bv_len, 0 );
				*bufp = '\0';
				LDAPDebug( LDAP_DEBUG_ANY, "smod %d - value: %s", n, buf, 0);
				slapi_ch_free( (void**)&buf );
			}
		}
	}
	else
	{
		LDAPDebug( LDAP_DEBUG_ANY, "smod - null\n", 0, 0, 0);
	}
}
