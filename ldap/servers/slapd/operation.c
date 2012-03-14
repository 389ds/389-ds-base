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

/* operation.c - routines to deal with pending ldap operations */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include "slap.h"

int
slapi_op_abandoned( Slapi_PBlock *pb )
{
	int	op_status;

	if (pb && pb->pb_op) {
		op_status = pb->pb_op->o_status;
		return( op_status == SLAPI_OP_STATUS_ABANDONED );
	}
	return 0;
}

int
slapi_op_internal( Slapi_PBlock *pb )
{
	if (pb && pb->pb_op) {
		return operation_is_flag_set(pb->pb_op, OP_FLAG_INTERNAL);
	}
	return 0;
}

void
operation_out_of_disk_space()
{
    LDAPDebug(LDAP_DEBUG_ANY, "*** DISK FULL ***\n", 0, 0, 0);
    LDAPDebug(LDAP_DEBUG_ANY, "Attempting to shut down gracefully.\n", 0, 0, 0);
    g_set_shutdown( SLAPI_SHUTDOWN_DISKFULL );
}

/* Setting the flags on the operation allows more control to the plugin
 * to disable and enable checks
 * Flags that we support  for setting in the operation  from the plugin are
 * SLAPI_OP_FLAG_NO_ACCESS_CHECK - do not check for access control
 * This function can be extended to support other flag setting as well
 */

void slapi_operation_set_flag(Slapi_Operation *op, unsigned long flag)
{

	operation_set_flag(op, flag);

}

void slapi_operation_clear_flag(Slapi_Operation *op, unsigned long flag)
{
	operation_clear_flag(op, flag);

}

int slapi_operation_is_flag_set(Slapi_Operation *op, unsigned long flag)
{
	return operation_is_flag_set(op, flag);
}



static int operation_type = -1; /* The type number assigned by the Factory for 'Operation' */

int
get_operation_object_type()
{
    if(operation_type==-1)
	{
	    /* The factory is given the name of the object type, in
		 * return for a type handle. Whenever the object is created
		 * or destroyed the factory is called with the handle so
		 * that it may call the constructors or destructors registered
		 * with it.
		 */
        operation_type= factory_register_type(SLAPI_EXT_OPERATION,offsetof(Operation,o_extension));
	}
    return operation_type;
}

#if defined(USE_OPENLDAP)
/* openldap doesn't have anything like this, nor does it have
   a way to portably and without cheating discover the
   sizeof BerElement - see lber_pvt.h for the trick used
   for BerElementBuffer
   so we just allocate everything separately
   If we wanted to get fancy, we could use LBER_OPT_MEMORY_FNS
   to override the ber malloc, realloc, etc. and use
   LBER_OPT_BER_MEMCTX to provide callback data for use
   with those functions
*/
static void*
ber_special_alloc(size_t size, BerElement **ppBer)
{
	void *mem = NULL;

	/* starts out with a null buffer - will grow as needed */
	*ppBer = ber_alloc_t(0);

	/* Make sure mem size requested is aligned */
	if (0 != ( size & 0x03 )) {
		size += (sizeof(ber_int_t) - (size & 0x03));
	}

	mem = slapi_ch_malloc(size);

	return mem;
}

static void
ber_special_free(void* buf, BerElement *ber)
{
	ber_free(ber, 1);
	slapi_ch_free(&buf);
}
#endif

/*
 * Allocate a new Slapi_Operation.
 * The flag parameter indicates whether the the operation is
 * external (from an LDAP Client), or internal (from a plugin).
 */
Slapi_Operation *
operation_new(int flags)
{
	/* To improve performance, we allocate the Operation, BerElement and
	 * ber buffer in one block, instead of a separate malloc() for each.
	 * Subsequently, ber_special_free() frees them all; we're careful
	 * not to free the Operation separately, and the ber software knows
	 * not to free the buffer separately.
	 */
	BerElement *ber = NULL;
	Slapi_Operation *o;
	if(flags & OP_FLAG_INTERNAL)
	{
	   	o = (Slapi_Operation *) slapi_ch_malloc(sizeof(Slapi_Operation));
	}
	else
	{
		o= (Slapi_Operation *) ber_special_alloc( sizeof(Slapi_Operation), &ber );
	}
	if (NULL != o)
	{
		memset(o,0,sizeof(Slapi_Operation));
		o->o_ber = ber;
		o->o_msgid = -1;
		o->o_tag = LBER_DEFAULT;
		o->o_status = SLAPI_OP_STATUS_PROCESSING;
		slapi_sdn_init(&(o->o_sdn));
		o->o_authtype = NULL;
		o->o_isroot = 0;
		o->o_time = current_time();
		o->o_opid = 0;
		o->o_connid = 0;
		o->o_next = NULL;
		o->o_flags= flags;
		if ( config_get_accesslog_level() & LDAP_DEBUG_TIMING ) {
                    o->o_interval = PR_IntervalNow();
                } else {
                    o->o_interval = (PRIntervalTime)0;
                }
	}
	return o;
}

void
operation_free( Slapi_Operation **op, Connection *conn )
{
	if(op!=NULL && *op!=NULL)
	{
		/* Call the plugin extension destructors */
	    factory_destroy_extension(get_operation_object_type(),*op,conn,&((*op)->o_extension));
		slapi_sdn_done(&(*op)->o_sdn);
		slapi_sdn_free(&(*op)->o_target_spec);
		slapi_ch_free_string( &(*op)->o_authtype );
		if ( (*op)->o_searchattrs != NULL ) {
			cool_charray_free( (*op)->o_searchattrs );
		}
		if ( NULL != (*op)->o_params.request_controls ) {
			ldap_controls_free( (*op)->o_params.request_controls );
		}
		if ( NULL != (*op)->o_results.result_controls ) {
			ldap_controls_free( (*op)->o_results.result_controls );
		}
		slapi_ch_free_string(&(*op)->o_results.result_matched);
		if(operation_is_flag_set(*op, OP_FLAG_INTERNAL))
		{
			slapi_ch_free((void**)op);
		}
		else
		{
			ber_special_free( *op , (*op)->o_ber);
		}
		/* counters_to_errors_log("after operation"); */
	}
}

void
slapi_operation_set_csngen_handler ( Slapi_Operation *op, void *callback )
{
	op->o_csngen_handler = (csngen_handler) callback;
}

void
slapi_operation_set_replica_attr_handler ( Slapi_Operation *op, void *callback )
{
	op->o_replica_attr_handler = (replica_attr_handler) callback;
}

int
slapi_operation_get_replica_attr ( Slapi_PBlock *pb, Slapi_Operation *op, const char *type, void *value )
{
	int rc = -1;

	if (op->o_replica_attr_handler)
	{
		rc = op->o_replica_attr_handler ( pb, type, value );
	}

	return rc;
}

CSN *
operation_get_csn(Slapi_Operation *op)
{
	return op->o_params.csn;
}

void
operation_set_csn(Slapi_Operation *op,CSN *csn)
{
	op->o_params.csn= csn;
}

unsigned long
slapi_op_get_type(Slapi_Operation *op)
{
	return op->o_params.operation_type;
}

char *
slapi_op_type_to_string(unsigned long type)
{
	switch (type) 
	{
		case SLAPI_OPERATION_ADD:
			return "add";
		case SLAPI_OPERATION_DELETE:
			return "delete";
		case SLAPI_OPERATION_MODIFY:
			return "modify";
		case SLAPI_OPERATION_MODRDN:
			return "modrdn";
		case SLAPI_OPERATION_BIND:
			return "bind";
		case SLAPI_OPERATION_COMPARE:
			return "compare";
		case SLAPI_OPERATION_SEARCH:
			return "search";
		default:
			return "unknown operation type";
	}
}

/* DEPRECATED : USE FUNCTION ABOVE FOR NEW DVLPT */
unsigned long
operation_get_type(Slapi_Operation *op)
{
	return op->o_params.operation_type;
}

void
operation_set_type(Slapi_Operation *op, unsigned long type)
{
	op->o_params.operation_type= type;
}

void
operation_set_flag(Slapi_Operation *op, int flag)
{
	op->o_flags|= flag;
}

void
operation_clear_flag(Slapi_Operation *op, int flag)
{
	op->o_flags &= ~flag;
}

int
operation_is_flag_set(Slapi_Operation *op, int flag)
{
	return op->o_flags & flag;
}

Slapi_DN* 
operation_get_target_spec (Slapi_Operation *op)
{
	return op->o_target_spec;
}

void 
operation_set_target_spec (Slapi_Operation *op, const Slapi_DN *target_spec)
{
	PR_ASSERT (op);
	PR_ASSERT (target_spec);

	op->o_target_spec = slapi_sdn_dup(target_spec);
}

void 
operation_set_target_spec_str (Slapi_Operation *op, const char *target_spec)
{
	PR_ASSERT (op);

	op->o_target_spec = slapi_sdn_new_dn_byval (target_spec);
}

unsigned long operation_get_abandoned_op (const Slapi_Operation *op)
{
	PR_ASSERT (op);

	return op->o_abandoned_op;
}

void operation_set_abandoned_op (Slapi_Operation *op, unsigned long abandoned_op)
{
	 PR_ASSERT (op);

	 op->o_abandoned_op = abandoned_op;
}

/* slapi_operation_parameters manipulation functions */

struct slapi_operation_parameters *operation_parameters_new()
{
	return (slapi_operation_parameters *)slapi_ch_calloc (1, sizeof (slapi_operation_parameters));
}

LDAPMod **
copy_mods(LDAPMod **orig_mods)
{
	LDAPMod **new_mods = NULL;
	LDAPMod *mod;
	Slapi_Mods smods_old;
	Slapi_Mods smods_new;
	slapi_mods_init_byref(&smods_old,orig_mods);
	slapi_mods_init_passin(&smods_new,new_mods);
	mod= slapi_mods_get_first_mod(&smods_old);
	while(mod!=NULL)
	{
		slapi_mods_add_modbvps(&smods_new,mod->mod_op,mod->mod_type,mod->mod_bvalues);
		mod= slapi_mods_get_next_mod(&smods_old);
	}
	new_mods= slapi_mods_get_ldapmods_passout(&smods_new);
	slapi_mods_done(&smods_old);
	slapi_mods_done(&smods_new);
	return new_mods;
}

struct slapi_operation_parameters *
operation_parameters_dup(struct slapi_operation_parameters *sop)
{
	struct slapi_operation_parameters *sop_new = (struct slapi_operation_parameters *)
		slapi_ch_malloc(sizeof(struct slapi_operation_parameters));
	memcpy(sop_new,sop,sizeof(struct slapi_operation_parameters));
	if(sop->target_address.uniqueid!=NULL)
	{
		sop_new->target_address.uniqueid= slapi_ch_strdup(sop->target_address.uniqueid); 
	}
	if(sop->target_address.sdn != NULL)
	{
		sop_new->target_address.sdn = slapi_sdn_dup(sop->target_address.sdn);
	}
  
	sop_new->csn= csn_dup(sop->csn);
	switch(sop->operation_type)
	{
	case SLAPI_OPERATION_ADD:
		sop_new->p.p_add.target_entry= slapi_entry_dup(sop->p.p_add.target_entry);
		sop_new->p.p_add.parentuniqueid = slapi_ch_strdup(sop->p.p_add.parentuniqueid);
		break;
	case SLAPI_OPERATION_MODIFY:
		sop_new->p.p_modify.modify_mods= NULL;
		if (sop->p.p_modify.modify_mods!=NULL)
		{
			sop_new->p.p_modify.modify_mods = copy_mods(sop->p.p_modify.modify_mods);
		}
		break;
	case SLAPI_OPERATION_MODRDN:
		if(sop->p.p_modrdn.modrdn_newrdn!=NULL)
		{
			sop_new->p.p_modrdn.modrdn_newrdn= slapi_ch_strdup(sop->p.p_modrdn.modrdn_newrdn);
		}
		if(sop->p.p_modrdn.modrdn_newsuperior_address.sdn!=NULL)
		{
			sop_new->p.p_modrdn.modrdn_newsuperior_address.sdn = 
				slapi_sdn_dup(sop->p.p_modrdn.modrdn_newsuperior_address.sdn);
		}
		if(sop->p.p_modrdn.modrdn_newsuperior_address.uniqueid!=NULL)
		{
			sop_new->p.p_modrdn.modrdn_newsuperior_address.uniqueid = 
				slapi_ch_strdup(sop->p.p_modrdn.modrdn_newsuperior_address.uniqueid);
		}
		sop_new->p.p_modrdn.modrdn_mods= NULL;
		if (sop->p.p_modrdn.modrdn_mods!=NULL)
		{
			sop_new->p.p_modrdn.modrdn_mods = copy_mods(sop->p.p_modrdn.modrdn_mods);
		}
		break;
	case SLAPI_OPERATION_DELETE:
		/* Has no extra parameters. */
	case SLAPI_OPERATION_BIND:
	case SLAPI_OPERATION_COMPARE:
	case SLAPI_OPERATION_SEARCH:
	default:
		/* We are not interested in these. */
		break;
	}
	return sop_new;
}

void
operation_parameters_done (struct slapi_operation_parameters *sop)
{
	if(sop!=NULL)
	{
		slapi_ch_free((void **)&sop->target_address.uniqueid);
		slapi_sdn_free(&sop->target_address.sdn);

		csn_free(&sop->csn);
		
		switch(sop->operation_type)
		{
		case SLAPI_OPERATION_ADD:
			slapi_entry_free(sop->p.p_add.target_entry);
			sop->p.p_add.target_entry= NULL;
			slapi_ch_free((void **)&(sop->p.p_add.parentuniqueid));
			break;
		case SLAPI_OPERATION_MODIFY:
			ldap_mods_free(sop->p.p_modify.modify_mods, 1 /* Free the Array and the Elements */);
			sop->p.p_modify.modify_mods= NULL;
			break;
		case SLAPI_OPERATION_MODRDN:
			slapi_ch_free((void **)&(sop->p.p_modrdn.modrdn_newrdn));
			slapi_ch_free((void **)&(sop->p.p_modrdn.modrdn_newsuperior_address.uniqueid));
			slapi_sdn_free(&sop->p.p_modrdn.modrdn_newsuperior_address.sdn);
			ldap_mods_free(sop->p.p_modrdn.modrdn_mods, 1 /* Free the Array and the Elements */);
			sop->p.p_modrdn.modrdn_mods= NULL;
			break;
		case SLAPI_OPERATION_DELETE:
			/* Has no extra parameters. */
		case SLAPI_OPERATION_BIND:
		case SLAPI_OPERATION_COMPARE:
		case SLAPI_OPERATION_SEARCH:
		default:
			/* We are not interested in these */
			break;
		}
	}
}

void operation_parameters_free(struct slapi_operation_parameters **sop)
{
	if (sop)
	{
		operation_parameters_done (*sop);	
		slapi_ch_free ((void**)sop);
	}
}
