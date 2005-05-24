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
#include "cb.h"

/* return the rootdn configured in the server */

char * cb_get_rootdn() {

	char * ret=slapi_get_rootdn();
	if (ret == NULL) 
		ret = slapi_ch_strdup(CB_DIRECTORY_MANAGER_DN);
	if (ret)
		slapi_dn_normalize_case(ret);	/* UTF8-aware */
	return ret;
}

void 
cb_send_ldap_result(Slapi_PBlock *pb, int err, char *matched,char *text, int nentries, struct berval **urls )
{
	cb_set_acl_policy(pb);
	slapi_send_ldap_result( pb, err, matched, text, nentries ,urls);
}

Slapi_Entry * cb_LDAPMessage2Entry(LDAP * ld, LDAPMessage * msg, int attrsonly) {

	Slapi_Entry * e = slapi_entry_alloc();
	char * a=NULL;
	BerElement * ber=NULL;

	if ( e == NULL ) return NULL;
	if (msg == NULL) {
		slapi_entry_free(e);
		return NULL;
	}
	
	/*
	 * dn not allocated by slapi
	 * attribute type and values ARE allocated
	 */

        slapi_entry_set_dn( e, ldap_get_dn( ld, msg ) );
 
        for ( a = ldap_first_attribute( ld, msg, &ber ); a!=NULL; 
              a=ldap_next_attribute( ld, msg, ber ) ) {
            if(attrsonly) {
                slapi_entry_add_value(e, a, (Slapi_Value *)NULL);
		ldap_memfree(a);
            } else {
                struct  berval ** aVal = ldap_get_values_len( ld, msg, a);
                slapi_entry_add_values( e, a, aVal);
                
                ldap_memfree(a);
                ldap_value_free_len(aVal);
            }
        }
    if ( NULL != ber )
        ldap_ber_free( ber, 0 );

    return e;
}

struct berval ** referrals2berval(char ** referrals) {

	int i;
	struct berval ** val=NULL;

	if (referrals == NULL)
		return NULL;

	for (i=0;referrals[i];i++) {}
	
	val = (struct berval **) slapi_ch_calloc(1,(i+1)*sizeof(struct berval *));

	for (i=0;referrals[i];i++) {

		val[i]=(struct berval *) slapi_ch_malloc(sizeof(struct berval));
		val[i]->bv_len= strlen(referrals[i]);
		val[i]->bv_val = slapi_ch_strdup(referrals[i]);
	}

	return val;
}
 
char *
cb_urlparse_err2string( int err )
{
    char        *s="internal error";

    switch( err ) {
    case 0:
        s = "no error";
        break;
    case LDAP_URL_ERR_NOTLDAP:
        s = "missing ldap:// or ldaps://";
        break;
    case LDAP_URL_ERR_NODN:
        s = "missing suffix";
        break;
    case LDAP_URL_ERR_BADSCOPE:
        s = "invalid search scope";
        break;
    case LDAP_URL_ERR_MEM:
        s = "unable to allocate memory";
        break;
    case LDAP_URL_ERR_PARAM:
        s = "bad parameter to an LDAP URL function";
        break;
    }
       
    return( s );
}

/*
** Return LDAP_SUCCESS if an internal operation needs to be forwarded to
** the farm server. We check chaining policy for internal operations
** We also check max hop count for loop detection for both internal
** and external operations
*/

int cb_forward_operation(Slapi_PBlock * pb ) {

	Slapi_Operation 		*op=NULL;
        Slapi_Backend           	*be;
        struct slapi_componentid 	*cid = NULL;
	char 				*pname;
        cb_backend_instance     	*cb;
	int 				retcode;
 	LDAPControl             	**ctrls=NULL;

        slapi_pblock_get (pb, SLAPI_OPERATION, &op);

	/* Loop detection */
        slapi_pblock_get( pb, SLAPI_REQCONTROLS, &ctrls );

	if ( NULL != ctrls ) {
		struct berval   *ctl_value=NULL;
		int iscritical=0;

		if (slapi_control_present(ctrls,CB_LDAP_CONTROL_CHAIN_SERVER,&ctl_value,&iscritical)) {

			/* Decode control data 			*/
			/* hop           INTEGER (0 .. maxInt) 	*/

			int 		hops = 0;
			int 		rc;
			BerElement 	*ber = NULL;

			if ((ber = ber_init(ctl_value)) == NULL) {
			        slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
					"cb_forward_operation: ber_init: Memory allocation failed");
			        return LDAP_NO_MEMORY;
			}
			rc = ber_scanf(ber,"i",&hops);
			if (LBER_ERROR == rc) {
       				slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
					"Loop detection control badly encoded.");
        			ber_free(ber,1);
				return LDAP_LOOP_DETECT;
			}
				
			if (hops <=0) {
       				slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
					"Max hop count exceeded. Loop detected.\n");
        			ber_free(ber,1);
				return LDAP_LOOP_DETECT;
			}
			ber_free(ber,1);
		}
	}

	if ( !operation_is_flag_set(op, OP_FLAG_INTERNAL))
		return LDAP_SUCCESS;

        slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &cid);
	if ( cid == NULL ) {
		/* programming error in the front-end */
       		slapi_log_error(SLAPI_LOG_FATAL, CB_PLUGIN_SUBSYSTEM,
			"NULL component identity in an internal operation.");
		return LDAP_UNWILLING_TO_PERFORM;
	}
	pname=cid->sci_component_name;

	if (cb_debug_on()) {
       		slapi_log_error(SLAPI_LOG_PLUGIN, CB_PLUGIN_SUBSYSTEM,
			"internal op received from %s component \n",pname ? pname : "NULL");
	}

	/* First, make sure chaining is not denied */
	if (operation_is_flag_set(op, SLAPI_OP_FLAG_NEVER_CHAIN))
		return LDAP_UNWILLING_TO_PERFORM;

	/* unidentified caller. should not happen */
	if (pname == NULL)
		return LDAP_UNWILLING_TO_PERFORM;

        slapi_pblock_get( pb, SLAPI_BACKEND, &be );
        cb = cb_get_instance(be);

	/* Local policy */
        PR_RWLock_Rlock(cb->rwl_config_lock);
	if ( cb->chaining_components != NULL ) {
		retcode=charray_inlist(cb->chaining_components,pname);
        	PR_RWLock_Unlock(cb->rwl_config_lock);
		if ( retcode )
			retcode=LDAP_SUCCESS;
		else
			retcode=LDAP_UNWILLING_TO_PERFORM;
		return retcode;
	}
       	PR_RWLock_Unlock(cb->rwl_config_lock);

	/* Global policy */
        PR_RWLock_Rlock(cb->backend_type->config.rwl_config_lock);
	retcode=charray_inlist(cb->backend_type->config.chaining_components,pname);
       	PR_RWLock_Unlock(cb->backend_type->config.rwl_config_lock);

	if ( retcode )
		retcode=LDAP_SUCCESS;
	else
		retcode=LDAP_UNWILLING_TO_PERFORM;
	return retcode;
}

/* better atol -- it understands a trailing multiplier k/m/g
 * for example, "32k" will be returned as 32768
 */
long cb_atol(char *str)
{
    long multiplier = 1;
    char *x = str;
    
    /* find possible trailing k/m/g */
    while ((*x >= '0') && (*x <= '9')) x++;
    switch (*x) {
    case 'g':   
    case 'G':
        multiplier *= 1024;
    case 'm':    
    case 'M':
        multiplier *= 1024;
    case 'k':    
    case 'K':
        multiplier *= 1024;
    }
    return (atol(str) * multiplier);
}
 
int cb_atoi(char *str)
{
    return (int)cb_atol(str);
}


/* This function is used by the instance modify callback to add a new
 * suffix.  It return LDAP_SUCCESS on success.
 */
int cb_add_suffix(cb_backend_instance *inst, struct berval **bvals, int apply_mod, char *returntext)
{
        Slapi_DN *suffix;
        int x;

        returntext[0] = '\0';
        for (x = 0; bvals[x]; x++) {
		suffix=slapi_sdn_new_dn_byval(bvals[x]->bv_val);
                if (!slapi_be_issuffix(inst->inst_be, suffix) && apply_mod) {
                        slapi_be_addsuffix(inst->inst_be, suffix);
                }
		slapi_sdn_free(&suffix);
        }

        return LDAP_SUCCESS;
}

static  int debug_on=0;

int cb_debug_on()
{
	return debug_on;
}

void cb_set_debug(int on) {
	debug_on=on;
}

/* this function is called when state of a backend changes */
/* The purpose of this function is to handle the associated_be_is_disabled
   flag in the cb instance structure.  The associated database is used to
   perform local acl evaluations.  The associated database can be
   1) The chaining backend is the backend of a sub suffix, and the
      parent suffix has a local backend
   2) Entry distribution is being used to distribute write operations to
      a chaining backend and other operations to a local backend
      (e.g. a replication hub or consumer)
   If the associated local backend is being initialized (import), it will be
   disabled, and it will be impossible to evaluate local acls.  In this case,
   we still want to be able to chain operations to a farm server or another
   database chain.  But the current code will not allow cascading without
   local acl evaluation (cb_controls.c).  associated_be_is_disabled allows
   us to relax that restriction while the associated backend is disabled
*/
/*
  The first thing we need to do is to determine what our associated backends
  are.  An associated backend is defined as a backend used by the same
  suffix which uses this cb instance or a backend used by any
  parent suffix of the suffix which uses this cb instance

  We first see if the be_name is for a local database.  If not, then just return.
  So for the given be_name, we find the suffix which uses it, then the mapping tree
  entry for that suffix.  Then
      get cb instances used by the suffix and set associated_be_is_disabled
	  get cb instances used by sub suffixes of this suffix and
	    set associated_be_is_disabled
*/
void 
cb_be_state_change (void *handle, char *be_name, int old_be_state, int new_be_state)
{
	const Slapi_DN *tmpsdn;
	Slapi_DN *the_be_suffix;
	char *cookie = NULL;
	Slapi_Backend *chainbe;
	Slapi_Backend *the_be = slapi_be_select_by_instance_name(be_name);

	/* no backend? */
	if (!the_be) {
		return;
	}

	/* ignore chaining backends - associated backends must be local */
	if (slapi_be_is_flag_set(the_be, SLAPI_BE_FLAG_REMOTE_DATA)) {
		return;
	}

	/* get the suffix for the local backend */
	tmpsdn = slapi_be_getsuffix(the_be, 0);
	if (!tmpsdn) {
		return;
	} else {
		the_be_suffix = slapi_sdn_dup(tmpsdn);
	}

	/* now, iterate through the chaining backends */
	for (chainbe = slapi_get_first_backend(&cookie);
		 chainbe; chainbe = slapi_get_next_backend(cookie)) {
		/* only look at chaining backends */
		if (slapi_be_is_flag_set(chainbe, SLAPI_BE_FLAG_REMOTE_DATA)) {
			/* get the suffix */
			const Slapi_DN *tmpcbsuf = slapi_be_getsuffix(chainbe, 0);
			if (tmpcbsuf) {
				/* make a copy - to be safe */
				Slapi_DN *cbsuffix = slapi_sdn_dup(tmpcbsuf);
				/* if the suffixes are equal, or the_be_suffix is a suffix
				   of cbsuffix, apply the flag */
				if (!slapi_sdn_compare(cbsuffix, the_be_suffix) ||
					slapi_sdn_issuffix(cbsuffix, the_be_suffix)) {
					cb_backend_instance *cbinst = cb_get_instance(chainbe);
					if (cbinst) {
						/* the backend is disabled if the state is not ON */
						cbinst->associated_be_is_disabled = (new_be_state != SLAPI_BE_STATE_ON);
						slapi_log_error(SLAPI_LOG_PLUGIN, "chainbe", "cb_be_state_change: set the "
										"state of chainbe for %s to %d\n",
										slapi_sdn_get_dn(cbsuffix), (new_be_state != SLAPI_BE_STATE_ON));
					}
				}
				slapi_sdn_free(&cbsuffix);
			}
		}
	}

	/* clean up */
	slapi_sdn_free(&the_be_suffix);
	slapi_ch_free_string(&cookie);
}
