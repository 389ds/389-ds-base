/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include "slap.h"

/*
 * JCM - The audit log might be better implemented as a post-op plugin.
 */

#define	ATTR_CHANGETYPE		"changetype"
#define	ATTR_NEWRDN		"newrdn"
#define	ATTR_DELETEOLDRDN	"deleteoldrdn"
#define ATTR_MODIFIERSNAME "modifiersname"
char	*attr_changetype	= ATTR_CHANGETYPE;
char	*attr_newrdn		= ATTR_NEWRDN;
char	*attr_deleteoldrdn	= ATTR_DELETEOLDRDN;
char	*attr_modifiersname = ATTR_MODIFIERSNAME;

/* Forward Declarations */
static void write_audit_file( int optype, char *dn, void *change, int flag, time_t curtime );

void
write_audit_log_entry( Slapi_PBlock *pb )
{
    time_t curtime;
    char *dn;
    void *change;
	int flag = 0;
	int internal_op = 0;
	Operation *op;
	
	slapi_pblock_get( pb, SLAPI_OPERATION, &op );
	internal_op = operation_is_flag_set(op, OP_FLAG_INTERNAL);
    slapi_pblock_get( pb, SLAPI_TARGET_DN, &dn );
    switch ( operation_get_type(op) )
	{
    case SLAPI_OPERATION_MODIFY:
	    slapi_pblock_get( pb, SLAPI_MODIFY_MODS, &change );
    	break;
    case SLAPI_OPERATION_ADD:
	    {
    	/*
    	 * For adds, we want the unnormalized dn, so we can preserve
    	 * spacing, case, when replicating it.
    	 */
        Slapi_Entry *te = NULL;
    	slapi_pblock_get( pb, SLAPI_ADD_ENTRY, &change );
    	te = (Slapi_Entry *)change;
    	if ( NULL != te )
		{
    	    dn = slapi_entry_get_dn( te );
    	}
		}
    	break;
    case SLAPI_OPERATION_DELETE:
		{
		char * deleterDN = NULL;
		slapi_pblock_get(pb, SLAPI_REQUESTOR_DN, &deleterDN);
    	change = deleterDN;
		}
    	break;
	
    case SLAPI_OPERATION_MODDN:
    	slapi_pblock_get( pb, SLAPI_MODRDN_NEWRDN, &change );
    	slapi_pblock_get( pb, SLAPI_MODRDN_DELOLDRDN, &flag );
    	break;
    }
    curtime = current_time();
    write_audit_file( operation_get_type(op), dn, change, flag, curtime );
}



/*
 * Function: write_audit_file
 * Arguments: 
 *            optype - type of LDAP operation being logged
 *            dn     - distinguished name of entry being changed
 *            change - pointer to the actual change operation
 *                     For a delete operation, may contain the modifier's DN.
 *            flag   - only used by modrdn operations - value of deleteoldrdn flag
 *            curtime - the current time
 * Returns: nothing
 */
static void
write_audit_file(
    int			optype,
    char		*dn,
    void		*change,
    int			flag,
    time_t		curtime
)
{
    LDAPMod	**mods;
    Slapi_Entry	*e;
    char	*newrdn, *tmp, *tmpsave;
    int	len, i, j;
    char	*timestr;
    lenstr	*l;

    l = lenstr_new();

    addlenstr( l, "time: " );
    timestr = format_localTime( curtime );
    addlenstr( l, timestr );
    slapi_ch_free((void **) &timestr );
    addlenstr( l, "\n" );
    addlenstr( l, "dn: " );
    addlenstr( l, dn );
    addlenstr( l, "\n" );

    switch ( optype )
	{
    case SLAPI_OPERATION_MODIFY:
    	addlenstr( l, attr_changetype );
    	addlenstr( l, ": modify\n" );
    	mods = change;
    	for ( j = 0; mods[j] != NULL; j++ )
		{
			int operationtype= mods[j]->mod_op & ~LDAP_MOD_BVALUES;
    	    switch ( operationtype )
			{
    	    case LDAP_MOD_ADD:
        		addlenstr( l, "add: " );
        		addlenstr( l, mods[j]->mod_type );
        		addlenstr( l, "\n" );
        		break;

    	    case LDAP_MOD_DELETE:
        		addlenstr( l, "delete: " );
        		addlenstr( l, mods[j]->mod_type );
        		addlenstr( l, "\n" );
        		break;

    	    case LDAP_MOD_REPLACE:
        		addlenstr( l, "replace: " );
        		addlenstr( l, mods[j]->mod_type );
        		addlenstr( l, "\n" );
        		break;

			default:
				operationtype= LDAP_MOD_IGNORE;
				break;
    	    }
			if(operationtype!=LDAP_MOD_IGNORE)
			{
    			for ( i = 0; mods[j]->mod_bvalues != NULL && mods[j]->mod_bvalues[i] != NULL; i++ )
				{
        			char *buf, *bufp;
        			len = strlen( mods[j]->mod_type );
        			len = LDIF_SIZE_NEEDED( len, mods[j]->mod_bvalues[i]->bv_len ) + 1;
       				buf = slapi_ch_malloc( len );
        			bufp = buf;
        			ldif_put_type_and_value( &bufp, mods[j]->mod_type,
        				mods[j]->mod_bvalues[i]->bv_val,
        				mods[j]->mod_bvalues[i]->bv_len );
        			*bufp = '\0';
        			addlenstr( l, buf );
        			slapi_ch_free( (void**)&buf );
    			}
			}
    	    addlenstr( l, "-\n" );
    	}
    	break;

    case SLAPI_OPERATION_ADD:
    	e = change;
    	addlenstr( l, attr_changetype );
    	addlenstr( l, ": add\n" );
    	tmp = slapi_entry2str( e, &len );
    	tmpsave = tmp;
    	while (( tmp = strchr( tmp, '\n' )) != NULL )
		{
    	    tmp++;
    	    if ( !ldap_utf8isspace( tmp ))
			{
        		break;
    	    }
    	}
    	addlenstr( l, tmp );
    	slapi_ch_free((void**)&tmpsave );
    	break;

    case SLAPI_OPERATION_DELETE:
		tmp = change;
    	addlenstr( l, attr_changetype );
    	addlenstr( l, ": delete\n" );
		if (tmp && tmp[0]) {
			addlenstr( l, attr_modifiersname );
			addlenstr( l, ": ");
			addlenstr( l, tmp);
			addlenstr( l, "\n");
		}
    	break;
    
    case SLAPI_OPERATION_MODDN:
    	newrdn = change;
    	addlenstr( l, attr_changetype );
    	addlenstr( l, ": modrdn\n" );
    	addlenstr( l, attr_newrdn );
    	addlenstr( l, ": " );
    	addlenstr( l, newrdn );
    	addlenstr( l, "\n" );
    	addlenstr( l, attr_deleteoldrdn );
    	addlenstr( l, ": " );
    	addlenstr( l, flag ? "1" : "0" );
    	addlenstr( l, "\n" );
    }
    addlenstr( l, "\n" );

    slapd_log_audit_proc (l->ls_buf, l->ls_len);

    lenstr_free( &l );
}
