/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* backend.c - Slapi_Backend methods */

#include "slap.h"

void
be_init( Slapi_Backend *be, const char *type, const char *name, int isprivate, int logchanges, int sizelimit, int timelimit )
{
    char text[128];
	slapdFrontendConfig_t *fecfg;
	be->be_suffix = NULL;
    be->be_suffixlock= PR_NewLock();
    be->be_suffixcount= 0;
	/* e.g. dn: cn=config,cn=NetscapeRoot,cn=ldbm database,cn=plugins,cn=config */
	sprintf(text, "cn=%s,cn=%s,cn=plugins,cn=config", name, type);
    be->be_basedn= slapi_ch_strdup(slapi_dn_normalize(text));
	sprintf(text, "cn=config,cn=%s,cn=%s,cn=plugins,cn=config", name, type);
	be->be_configdn= slapi_ch_strdup(slapi_dn_normalize(text));
	sprintf(text, "cn=monitor,cn=%s,cn=%s,cn=plugins,cn=config", name, type);
	be->be_monitordn= slapi_ch_strdup(slapi_dn_normalize(text));
	be->be_sizelimit = sizelimit;
	be->be_timelimit = timelimit;
	/* maximum group nesting level before giving up */
	be->be_maxnestlevel = SLAPD_DEFAULT_GROUPNESTLEVEL;
	be->be_noacl= 0;
        be->be_flags=0;
	if (( fecfg = getFrontendConfig()) != NULL )
	{
	    if ( fecfg->backendconfig != NULL && fecfg->backendconfig[ 0 ] != NULL )
	    {
    		be->be_backendconfig = slapi_ch_strdup( fecfg->backendconfig[0] );
	    }
		else
		{
			be->be_backendconfig= NULL;
		}
	    be->be_readonly = fecfg->readonly;
	}
	else
	{
		be->be_readonly= 0;
		be->be_backendconfig= NULL;
	}
	be->be_lastmod = LDAP_UNDEFINED;
	be->be_type = slapi_ch_strdup(type);
	be->be_include = NULL;
	be->be_private = isprivate;
    be->be_logchanges = logchanges;
	be->be_database = NULL;
	be->be_writeconfig = NULL;
    be->be_delete_on_exit = 0;
	be->be_state = BE_STATE_STOPPED;
	be->be_state_lock = PR_NewLock();
	be->be_name = slapi_ch_strdup(name);
	be->be_mapped = 0;
}

void 
be_done(Slapi_Backend *be)
{
    int i;

    for(i=0;i<be->be_suffixcount;i++)
    {
        slapi_sdn_free(&be->be_suffix[i]);
    }
    slapi_ch_free((void**)&be->be_suffix);
    PR_DestroyLock(be->be_suffixlock);
    slapi_ch_free((void **)&be->be_basedn);
    slapi_ch_free((void **)&be->be_configdn);
    slapi_ch_free((void **)&be->be_monitordn);
    slapi_ch_free((void **)&be->be_type);
    slapi_ch_free((void **)&be->be_backendconfig);
    /* JCM char **be_include; ??? */
    slapi_ch_free((void **)&be->be_name);
    PR_DestroyLock(be->be_state_lock);
}

void
slapi_be_delete_onexit (Slapi_Backend *be)
{
	be->be_delete_on_exit = 1;
}

void
slapi_be_set_readonly(Slapi_Backend *be, int readonly)
{
    be->be_readonly = readonly;
}

int
slapi_be_get_readonly(Slapi_Backend *be)
{
    return be->be_readonly;
}

/*
 * Check if suffix, exactly matches a registered
 * suffix of this backend.
 */
int
slapi_be_issuffix( const Slapi_Backend *be, const Slapi_DN *suffix )
{
	int r= 0;
	/* this backend is no longer valid */
	if (be->be_state != BE_STATE_DELETED)
	{
    	int	i;
        PR_Lock(be->be_suffixlock);
    	for ( i = 0; be->be_suffix != NULL && i<be->be_suffixcount; i++ )
		{
    		if ( slapi_sdn_compare( be->be_suffix[i], suffix ) == 0)
		    {
    			r= 1;
				break;
    		}
    	}
        PR_Unlock(be->be_suffixlock);
	}
	return r;
}

void 
be_addsuffix(Slapi_Backend *be,const Slapi_DN *suffix)
{
	if (be->be_state != BE_STATE_DELETED)
	{
        PR_Lock(be->be_suffixlock);
		if(be->be_suffix==NULL)
		{
		    be->be_suffix= (Slapi_DN **)slapi_ch_malloc(sizeof(Slapi_DN *));
		}
		else
		{
		    be->be_suffix= (Slapi_DN **)slapi_ch_realloc((char*)be->be_suffix,(be->be_suffixcount+1)*sizeof(Slapi_DN *));
		}
		be->be_suffix[be->be_suffixcount]= slapi_sdn_dup(suffix);
        be->be_suffixcount++;
        PR_Unlock(be->be_suffixlock);
	}
}

void slapi_be_addsuffix(Slapi_Backend *be,const Slapi_DN *suffix)
{
	be_addsuffix(be,suffix);
}

/* 
 * The caller may use the returned pointer without holding the
 * be_suffixlock since we never remove suffixes from the array.
 * The Slapi_DN pointer will always be valid even though the array
 * itself may be changing due to the addition of a suffix.
 */
const Slapi_DN *
slapi_be_getsuffix(Slapi_Backend *be,int n)
{
    Slapi_DN *sdn = NULL;

	if(NULL == be)
		return NULL;

    if(be->be_state != BE_STATE_DELETED) {
        PR_Lock(be->be_suffixlock);
        if (be->be_suffix !=NULL && n<be->be_suffixcount) {
            sdn =  be->be_suffix[n];
        }
        PR_Unlock(be->be_suffixlock);
    }
    return sdn;
}

const char *
slapi_be_gettype(Slapi_Backend *be)
{
	const char *r= NULL;
	if (be->be_state != BE_STATE_DELETED)
	{
	    r= be->be_type;
	}
    return r;
}

Slapi_DN *
be_getconfigdn(Slapi_Backend *be, Slapi_DN *dn)
{
	if (be->be_state == BE_STATE_DELETED)
	{
	    slapi_sdn_set_ndn_byref(dn,NULL);
	}
	else
	{
	    slapi_sdn_set_ndn_byref(dn,be->be_configdn);
	}
	return dn;
}

Slapi_DN *
be_getmonitordn(Slapi_Backend *be, Slapi_DN *dn)
{
	if (be->be_state == BE_STATE_DELETED)
	{
	    slapi_sdn_set_ndn_byref(dn,NULL);
	}
	else
	{
	    slapi_sdn_set_ndn_byref(dn,be->be_monitordn);
	}
	return dn;
}

int
be_writeconfig ( Slapi_Backend *be )
{
  Slapi_PBlock *newpb;
  
  if (be->be_state == BE_STATE_DELETED || be->be_private || 
	  (be->be_writeconfig == NULL) ) {
	return -1;
  }
  else {
	newpb  = slapi_pblock_new();
	slapi_pblock_set ( newpb, SLAPI_PLUGIN, (void *) be->be_database );
	slapi_pblock_set ( newpb, SLAPI_BACKEND, (void *) be );
	(be->be_writeconfig)(newpb);
	slapi_pblock_destroy ( newpb );
	return 1;
  }
}

/*
 * Find out if changes made to entries in this backend
 * should be recorded in the changelog.
 */
int 
slapi_be_logchanges(Slapi_Backend *be)
{
	if (be->be_state == BE_STATE_DELETED)
		return 0;

    return be->be_logchanges;
}

int 
slapi_be_private ( Slapi_Backend *be )
{
	if ( be!=NULL )
	{
		return (be->be_private);
	}

	return 0;
}

void *
slapi_be_get_instance_info(Slapi_Backend * be)
{
    PR_ASSERT(NULL != be);
    return be->be_instance_info;
}

void 
slapi_be_set_instance_info(Slapi_Backend * be, void * data)
{
    PR_ASSERT(NULL != be);
    be->be_instance_info=data;
}

int
slapi_be_getentrypoint(Slapi_Backend *be, int entrypoint, void **ret_fnptr, Slapi_PBlock *pb)
{
	PR_ASSERT(NULL != be);

    /* this is something needed for most of the entry points */
    if (pb)
    {
        slapi_pblock_set( pb, SLAPI_PLUGIN, be->be_database );
		slapi_pblock_set( pb, SLAPI_BACKEND, be );
    }

	switch (entrypoint) {
	case SLAPI_PLUGIN_DB_BIND_FN:
		*ret_fnptr = (void*)be->be_bind;
		break;
	case SLAPI_PLUGIN_DB_UNBIND_FN:
		*ret_fnptr = (void*)be->be_unbind;
		break;
	case SLAPI_PLUGIN_DB_SEARCH_FN:
		*ret_fnptr = (void*)be->be_search;
		break;
	case SLAPI_PLUGIN_DB_COMPARE_FN:
		*ret_fnptr = (void*)be->be_compare;
		break;
	case SLAPI_PLUGIN_DB_MODIFY_FN:
		*ret_fnptr = (void*)be->be_modify;
		break;
	case SLAPI_PLUGIN_DB_MODRDN_FN:
		*ret_fnptr = (void*)be->be_modrdn;
		break;
	case SLAPI_PLUGIN_DB_ADD_FN:
		*ret_fnptr = (void*)be->be_add;
		break;
	case SLAPI_PLUGIN_DB_DELETE_FN:
		*ret_fnptr = (void*)be->be_delete;
		break;
	case SLAPI_PLUGIN_DB_ABANDON_FN:
		*ret_fnptr = (void*)be->be_abandon;
		break;
	case SLAPI_PLUGIN_DB_CONFIG_FN:
		*ret_fnptr = (void*)be->be_config;
		break;
	case SLAPI_PLUGIN_CLOSE_FN:
		*ret_fnptr = (void*)be->be_close;
		break;
	case SLAPI_PLUGIN_DB_FLUSH_FN:
		*ret_fnptr = (void*)be->be_flush;
		break;
	case SLAPI_PLUGIN_START_FN:
		*ret_fnptr = (void*)be->be_start;
		break;
	case SLAPI_PLUGIN_DB_RESULT_FN:
		*ret_fnptr = (void*)be->be_result;
		break;
	case SLAPI_PLUGIN_DB_LDIF2DB_FN:
		*ret_fnptr = (void*)be->be_ldif2db;
		break;
	case SLAPI_PLUGIN_DB_DB2LDIF_FN:
		*ret_fnptr = (void*)be->be_db2ldif;
		break;
	case SLAPI_PLUGIN_DB_ARCHIVE2DB_FN:
		*ret_fnptr = (void*)be->be_archive2db;
		break;
	case SLAPI_PLUGIN_DB_DB2ARCHIVE_FN:
		*ret_fnptr = (void*)be->be_db2archive;
		break;
	case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN:
		*ret_fnptr = (void*)be->be_next_search_entry;
		break;
	case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN:
		*ret_fnptr = (void*)be->be_next_search_entry_ext;
		break;
	case SLAPI_PLUGIN_DB_ENTRY_RELEASE_FN:
		*ret_fnptr = (void*)be->be_entry_release;
		break;
	case SLAPI_PLUGIN_DB_SIZE_FN:
		*ret_fnptr = (void*)be->be_dbsize;
		break;
	case SLAPI_PLUGIN_DB_TEST_FN:
		*ret_fnptr = (void*)be->be_dbtest;
		break;
	case SLAPI_PLUGIN_DB_RMDB_FN:
		*ret_fnptr = (void*)be->be_rmdb;
		break;
	case SLAPI_PLUGIN_DB_INIT_INSTANCE_FN:
		*ret_fnptr = (void*)be->be_init_instance;
		break;	
	case SLAPI_PLUGIN_DB_SEQ_FN:
		*ret_fnptr = (void*)be->be_seq;
		break;
	case SLAPI_PLUGIN_DB_DB2INDEX_FN:
		*ret_fnptr = (void*)be->be_db2index;
		break;
	case SLAPI_PLUGIN_CLEANUP_FN:
		*ret_fnptr = (void*)be->be_cleanup;
		break;
	default:
		slapi_log_error(SLAPI_LOG_FATAL, NULL,
			"slapi_be_getentrypoint: unknown entry point %d\n", entrypoint);
		return -1;
	}
	return 0;
}
	
int
slapi_be_setentrypoint(Slapi_Backend *be, int entrypoint, void *ret_fnptr, Slapi_PBlock *pb)
{
    PR_ASSERT(NULL != be);

    /* this is something needed for most of the entry points */
    if (pb)
    {
        be->be_database=pb->pb_plugin;
        return 0;
    }
 
    switch (entrypoint) {
    case SLAPI_PLUGIN_DB_BIND_FN:
        be->be_bind=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_UNBIND_FN:
        be->be_unbind=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_SEARCH_FN:
        be->be_search=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_COMPARE_FN:
		be->be_compare=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_MODIFY_FN:
        be->be_modify=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_MODRDN_FN:
        be->be_modrdn=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_ADD_FN:
        be->be_add=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_DELETE_FN:
        be->be_delete=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_ABANDON_FN:
        be->be_abandon=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_CONFIG_FN:
        be->be_config=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_CLOSE_FN:
        be->be_close=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_FLUSH_FN:
        be->be_flush=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_START_FN:
        be->be_start=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_RESULT_FN:
        be->be_result=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_LDIF2DB_FN:
	   be->be_ldif2db=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_DB2LDIF_FN:
        be->be_db2ldif=(IFP) ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_ARCHIVE2DB_FN:
        be->be_archive2db=(IFP) ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_DB2ARCHIVE_FN:
        be->be_db2archive=(IFP) ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_FN:
        be->be_next_search_entry=(IFP) ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_NEXT_SEARCH_ENTRY_EXT_FN:
        be->be_next_search_entry_ext=(IFP) ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_ENTRY_RELEASE_FN:
        be->be_entry_release=(IFP) ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_SIZE_FN:
        be->be_dbsize=(IFP) ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_TEST_FN:
        be->be_dbtest=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_RMDB_FN:
        be->be_rmdb=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_INIT_INSTANCE_FN:
        be->be_init_instance=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_SEQ_FN:
        be->be_seq=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_DB_DB2INDEX_FN:
        be->be_db2index=(IFP)ret_fnptr;
        break;
	case SLAPI_PLUGIN_CLEANUP_FN:
        be->be_cleanup=(IFP)ret_fnptr;
        break;
	default:
        slapi_log_error(SLAPI_LOG_FATAL, NULL,
                "slapi_be_setentrypoint: unknown entry point %d\n", entrypoint);
        return -1;
	}
    return 0;
}

int slapi_be_is_flag_set(Slapi_Backend * be, int flag)
{ 
    return be->be_flags & flag;
}

void slapi_be_set_flag(Slapi_Backend * be, int flag)
{ 
    be->be_flags|= flag;
}

char * slapi_be_get_name(Slapi_Backend * be)
{
	return be->be_name;
}

void be_set_sizelimit(Slapi_Backend * be, int sizelimit)
{
        be->be_sizelimit = sizelimit;
}

void be_set_timelimit(Slapi_Backend * be, int timelimit)
{
        be->be_timelimit = timelimit;
}
