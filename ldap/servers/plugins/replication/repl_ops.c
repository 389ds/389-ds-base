/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "slapi-plugin.h"
#include "repl.h"
#include "repl5.h"

int
legacy_postop( Slapi_PBlock *pb, const char *caller, int operation_type)
{
	int rc = 0;
    Object *r_obj;
    Replica *r;
	
    r_obj = replica_get_replica_for_op (pb);
    if (r_obj == NULL)  /* there is no replica configured for this operations */
        return 0;
    else
    {
        /* check if this replica is 4.0 consumer */
        r = (Replica*)object_get_data (r_obj);
        PR_ASSERT (r);

        /* this replica is not a 4.0 consumer - so we don't need to do any processing */
        if (!replica_is_legacy_consumer (r))
        {
            object_release (r_obj);
            return 0;
        }
    
        object_release (r_obj);
    }

	slapi_pblock_get(pb, SLAPI_PLUGIN_OPRETURN, &rc);
	if (0 == rc)
	{
		if (OP_ADD == operation_type || OP_MODIFY == operation_type)
		{
			void *op;
			consumer_operation_extension *opext = NULL;

			/* Optimise out traversal of mods/entry if no cop{ied|ying}From present */
			slapi_pblock_get(pb, SLAPI_OPERATION, &op);
			opext = (consumer_operation_extension*) repl_con_get_ext (REPL_CON_EXT_OP, op);
			if (NULL != opext && opext->has_cf)
			{
                process_legacy_cf( pb );
			}
		}
	}

	return 0;
}



static char *not_replicationdn_errmsg =
	"An operation was submitted that contained copiedFrom or "
	"copyingFrom attributes, but the connection was not bound "
	"as the replicationdn.";

int
legacy_preop(Slapi_PBlock *pb, const char *caller, int operation_type)
{
	int rc = 0;
	Slapi_Operation *operation = NULL;
	consumer_operation_extension *opext = NULL;
	int has_cf = 0;
    Object *r_obj;
    Replica *r;
	int is_legacy_op = 0;

	slapi_pblock_get( pb, SLAPI_OPERATION, &operation );
	is_legacy_op = operation_is_flag_set(operation,OP_FLAG_LEGACY_REPLICATION_DN);
    r_obj = replica_get_replica_for_op (pb);

    if (r_obj == NULL) {  /* there is no replica configured for this operations */
		if (is_legacy_op){
			/* This is a legacy replication operation but there are NO replica defined
			   Just refuse it */
			slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, 
				"Replication operation refused because the consumer is not defined as a replica", 0, NULL);
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
				"Incoming replication operation was refused because "
				"there's no replica defined for this operation\n");
			return -1;
		}
		else {
			return 0;
		}
	}
    else
    {
        /* check if this replica is 4.0 consumer */
        r = (Replica*)object_get_data (r_obj);
        PR_ASSERT (r);

        if (!replica_is_legacy_consumer (r))
        {
            object_release (r_obj);
			if (is_legacy_op) {
				/* This is a legacy replication operation 
				   but the replica is doesn't accept from legacy
				   Just refuse it */
				slapi_send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL, 
					"Replication operation refused because "
					"the consumer is not defined as a legacy replica", 0, NULL);
				slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name,
					"Incoming replication operation was refused because "
					"there's no legacy replica defined for this operation\n");
				return -1;
			} else {
				return 0;
			}
        }

        object_release (r_obj);
    }

	opext = (consumer_operation_extension*) repl_con_get_ext (REPL_CON_EXT_OP, operation);
	
	switch (operation_type) {
	case OP_ADD:
		{
			Slapi_Entry *e = NULL;
			Slapi_Attr *attr;
			/*
			 * Check if the entry being added has copiedFrom/copyingFrom
			 * attributes. 
			 */
			slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
			if (NULL != e)
			{
				if (slapi_entry_attr_find(e, type_copiedFrom, &attr) == 0)
				{
					has_cf = 1;
				}
				else
				if (slapi_entry_attr_find(e, type_copyingFrom, &attr) == 0)
				{
					has_cf = 1;
				}
			}
			/* JCMREPL - If this is a replicated operation then the baggage control also contains the Unique Identifier of the superior entry. */
		}
		break;
	case OP_MODIFY:
		{
			LDAPMod **mods = NULL;
			int i;
	
			/*
			 * Check if the modification contains copiedFrom/copyingFrom
			 * attributes. 
			 */
			slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
			for (i = 0; NULL != mods && NULL != mods[i]; i++)
			{
				if ((strcasecmp(mods[i]->mod_type, type_copiedFrom) == 0) ||
						(strcasecmp(mods[i]->mod_type, type_copyingFrom) == 0))
				{
					has_cf = 1;
				}
			}
		}
		break;
	case OP_DELETE:
		break;
	case OP_MODDN:
		break;
	}

	/* Squirrel away an optimization hint for the postop plugin */
	opext->has_cf = has_cf;

	return rc;
}
