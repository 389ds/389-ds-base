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
 
#ifndef _REPL_H_
#define _REPL_H_

#include <limits.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/param.h>
#endif /* _WIN32 */

#include "portable.h" /* GGOODREPL - is this cheating? */
#include "ldaplog.h"
#include "repl_shared.h"
#include "cl4.h"

typedef struct schedule_item
{
    unsigned long sch_start; /* seconds after midnight */
    unsigned long sch_length; /* sec */
    unsigned int  sch_weekdays; /* bit mask; LSB = Sunday */
    struct schedule_item* sch_next;
} schedule_item;

/* XXXggood - copied from slap.h - bad */
#if defined( XP_WIN32 )
#define NO_TIME (time_t)0 /* cannot be -1, NT's localtime( -1 ) returns NULL */
#else
#define NO_TIME (time_t)-1 /* a value that time() does not return */
#endif

/*
 * A status message contains a time, the textual message, 
 * and a count of the number of times the message occured.
 */
typedef struct _status_message {
    time_t	sm_time;
    char	*sm_message;
	int     sm_occurances;
} status_message;

/*
 * A status_message_list is a circular array of status messages.
 * Old messages roll off the end and are discarded.
 */
typedef struct _status_message_list {
    int			sml_size;	/* number of slots in array */
    int			sml_tail;	/* next slot to be written */
    status_message	*sml_messages;	/* array of messages */
} sm_list;
#define	NUM_REPL_MESSAGES	20	/* max # of messages to save */

/* Selective attribute Inclusion states.  ORDERING IS SIGNIFICANT */
#define IMPLICITLY_INCLUDED	1
#define IMPLICITLY_EXCLUDED	2
#define EXPLICITLY_EXCLUDED	3
#define EXPLICITLY_INCLUDED	4

#if defined(__JCMREPL_FILTER__)
/*
 * Structure used to implement selective attribute filtering.
 * sa_filter nodes are arranged in a linked list. 
 */
typedef struct _sa_filter {
    Slapi_Filter		*sa_filter;	/* Filter to apply */
    int			sa_isexclude;	/* non-zero if list is exclude list */
    char		**sa_attrlist;	/* array - attrs to replicate */
    struct _sa_filter	*sa_next;	/* Link to next struct */
} sa_filter;
#endif

typedef unsigned long changeNumber;
#define a2changeNumber( a ) strtoul(( a ), (char **)NULL, 10 )

#define	AUTH_SIMPLE	1
#define	AUTH_KERBEROS	2

typedef struct modinfo {
    char	*type;
    char	*value;
    int		len;
} modinfo;

/*
 * Representation of one change entry from the replog file.
 */
typedef struct repl {
    char	*time;		/* time of modification */
    changeNumber change;        /* number of this change */
    char	*dn;		/* dn of entry being modified - normalized */
    char	*raw_dn;	/* dn of entry - not normalized */
    int		changetype;	/* type of change */
    modinfo	*mods;		/* modifications to make */
    char	*newrdn;	/* new rdn for modrdn */
    int		deleteoldrdn;	/* flag for modrdn */

} repl;

#define BIND_OK 			0
#define BIND_ERR_BADLDP			1
#define	BIND_ERR_OPEN			2
#define	BIND_ERR_BAD_ATYPE		3
#define	BIND_ERR_SIMPLE_FAILED		4
#define	BIND_ERR_KERBEROS_FAILED	5
#define	BIND_ERR_SSL_INIT_FAILED	6
#define BIND_ERR_RACE			7

#define	MAX_CHANGENUMBER		ULONG_MAX

#define	REPLICATION_SUBSYSTEM		"replication"
#define	REPL_LDAP_TIMEOUT		30L	/* Wait 30 seconds for responses */

/* Update the copiedFrom attribute every <n> updates */
#define	UPDATE_COPIEDFROM_INTERVAL	10
#define REPL_ERROR_REPL_HALTED		"REPLICATION HALTED"
#define ATTR_NETSCAPEMDSUFFIX	"netscapemdsuffix"

#define CONFIG_LEGACY_REPLICATIONDN_ATTRIBUTE "nsslapd-legacy-updatedn"
#define CONFIG_LEGACY_REPLICATIONPW_ATTRIBUTE "nsslapd-legacy-updatepw"

#define LDAP_CONTROL_REPL_MODRDN_EXTRAMODS   "2.16.840.1.113730.3.4.999"

/* Operation types */
#define	OP_MODIFY  1
#define OP_ADD     2
#define OP_DELETE  3
#define OP_MODDN   4
#define OP_SEARCH  5
#define OP_COMPARE 6

/* 4.0-style housekeeping interval */
#define REPLICATION_HOUSEKEEPING_INTERVAL (30 * 1000) /* 30 seconds */

/* Top of tree for replication configuration information */
#define REPL_CONFIG_TOP "cn=replication,cn=config"

/* Functions */

/* repl_rootdse.c */
int repl_rootdse_init();

/* In repl.c */
Slapi_Entry *get_changerecord(const chglog4Info *cl4, changeNumber cnum, int *err);
changeNumber replog_get_firstchangenum(const chglog4Info *cl4, int *err);
changeNumber replog_get_lastchangenum(const chglog4Info *cl4, int *err);
void changelog_housekeeping(time_t cur_time );

/* In repl_config.c */
int repl_config_init ();

/* Legacy Plugin Functions */

int legacy_preop_bind( Slapi_PBlock *pb );
int legacy_bepreop_bind( Slapi_PBlock *pb );
int legacy_postop_bind( Slapi_PBlock *pb );
int legacy_preop_add( Slapi_PBlock *pb );
int legacy_bepreop_add( Slapi_PBlock *pb );
int legacy_postop_add( Slapi_PBlock *pb );
int legacy_preop_modify( Slapi_PBlock *pb );
int legacy_bepreop_modify( Slapi_PBlock *pb );
int legacy_postop_modify( Slapi_PBlock *pb );
int legacy_preop_modrdn( Slapi_PBlock *pb );
int legacy_bepreop_modrdn( Slapi_PBlock *pb );
int legacy_postop_modrdn( Slapi_PBlock *pb );
int legacy_preop_delete( Slapi_PBlock *pb );
int legacy_bepreop_delete( Slapi_PBlock *pb );
int legacy_postop_delete( Slapi_PBlock *pb );
int legacy_preop_search( Slapi_PBlock *pb );
int legacy_preop_compare( Slapi_PBlock *pb );
int legacy_pre_entry( Slapi_PBlock *pb );
int legacy_bepostop_assignchangenum( Slapi_PBlock *pb );

int replication_plugin_start( Slapi_PBlock *pb );
int replication_plugin_poststart( Slapi_PBlock *pb );
int replication_plugin_stop( Slapi_PBlock *pb );

/* In repl.c */
void replog( Slapi_PBlock *pb, int optype );
void init_changelog_trimming( changeNumber max_changes, time_t max_age );

/* From repl_globals.c */

extern char	*attr_changenumber;
extern char	*attr_targetdn;
extern char	*attr_changetype;
extern char	*attr_newrdn;
extern char	*attr_deleteoldrdn;
extern char	*attr_changes;
extern char	*attr_newsuperior;
extern char	*attr_changetime;
extern char	*attr_dataversion;
extern char	*attr_csn;

extern char	*changetype_add;
extern char	*changetype_delete;
extern char	*changetype_modify;
extern char	*changetype_modrdn;
extern char	*changetype_moddn;

extern char	*type_copyingFrom;
extern char	*type_copiedFrom;
extern char	*filter_copyingFrom;
extern char	*filter_copiedFrom;
extern char	*filter_objectclass;

extern char	*type_cn;
extern char	*type_objectclass;

/* JCMREPL - IFP should be defined centrally */

#ifndef _IFP
#define _IFP
typedef int	(*IFP)();
#endif

/* In cl4.c */

changeNumber ldapi_assign_changenumber(chglog4Info *cl4);
changeNumber ldapi_get_last_changenumber(chglog4Info *cl4);
changeNumber ldapi_get_first_changenumber(chglog4Info *cl4);
void ldapi_commit_changenumber(chglog4Info *cl4, changeNumber cnum);
void ldapi_set_first_changenumber(chglog4Info *cl4, changeNumber cnum);
void ldapi_set_last_changenumber(chglog4Info *cl4, changeNumber cnum);
void ldapi_initialize_changenumbers(chglog4Info *cl4, changeNumber first, changeNumber last);

#define	LDBM_TYPE				"ldbm"
#define	CHANGELOG_LDBM_TYPE		"changelog-ldbm"

#define MAX_RETRY_INTERVAL 3600 /* sec = 1 hour */

#define REPL_PROTOCOL_UNKNOWN 0
#define REPL_PROTOCOL_40 1
#define REPL_PROTOCOL_50_INCREMENTAL 2
#define REPL_PROTOCOL_50_TOTALUPDATE 3
#define REPL_PROTOCOL_71_TOTALUPDATE 4

/* In repl_globals.c */
int decrement_repl_active_threads();
int increment_repl_active_threads();

/* operation extensions */

/* Type of extensions that can be registered */
typedef enum
{
	REPL_SUP_EXT_OP,	/* extension for Operation object, replication supplier */
	REPL_SUP_EXT_CONN,	/* extension for Connection object, replication supplier */
	REPL_CON_EXT_OP,	/* extension for Operation object, replication consumer */
	REPL_CON_EXT_CONN,	/* extension for Connection object, replication consumer */
    REPL_CON_EXT_MTNODE,/* extension for mapping_tree_node object, replication consumer */
	REPL_EXT_ALL
} ext_type;

/* general extension functions - repl_ext.c */
void repl_sup_init_ext ();	/* initializes registrations - must be called first */
void repl_con_init_ext ();	/* initializes registrations - must be called first */
int repl_sup_register_ext (ext_type type); /* registers an extension of the specified type */
int repl_con_register_ext (ext_type type); /* registers an extension of the specified type */
void* repl_sup_get_ext (ext_type type, void *object); /* retireves the extension from the object */
void* repl_con_get_ext (ext_type type, void *object); /* retireves the extension from the object */

/* Operation extension functions - supplier_operation_extension.c */

/* --- supplier operation extension --- */
typedef struct supplier_operation_extension
{
	int prevent_recursive_call;
	struct slapi_operation_parameters *operation_parameters;
    char *repl_gen;
} supplier_operation_extension;

/* extension construct/destructor */
void* supplier_operation_extension_constructor (void *object, void *parent);	
void supplier_operation_extension_destructor (void* ext,void *object, void *parent);

/* --- consumer operation extension --- */
typedef struct consumer_operation_extension
{
	int has_cf;      /* non-zero if the operation contains a copiedFrom/copyingFrom attr */
	void *search_referrals;
} consumer_operation_extension;

/* extension construct/destructor */
void* consumer_operation_extension_constructor (void *object, void *parent);	
void consumer_operation_extension_destructor (void* ext,void *object, void *parent);

/* Connection extension functions - repl_connext.c */

/* --- connection extension --- */
/* ONREPL - some pointers are void* because they represent 5.0 data structures
   not known in this header. Fix */
typedef struct consumer_connection_extension
{
	int	is_legacy_replication_dn;
	int repl_protocol_version; /* the replication protocol version number the supplier is talking. */
	void *replica_acquired;    /* Object* for replica */
    void *supplier_ruv;        /* RUV* */
	int isreplicationsession;
	Slapi_Connection *connection;
} consumer_connection_extension;

/* extension construct/destructor */
void* consumer_connection_extension_constructor (void *object,void *parent);	
void consumer_connection_extension_destructor (void* ext,void *object,void *parent);

/* mapping tree extension - stores replica object */
typedef struct multimaster_mtnode_extension
{
    Object *replica;
} multimaster_mtnode_extension;
void* multimaster_mtnode_extension_constructor (void *object,void *parent);	
void  multimaster_mtnode_extension_destructor (void* ext,void *object,void *parent);

/* In repl_init.c */

int get_legacy_stop();

/* In repl_entry.c */
void repl_entry_init(int argc, char** argv);

/* In repl_ops.c */
int legacy_preop( Slapi_PBlock *pb, const char* caller, int operation_type);
int legacy_postop( Slapi_PBlock *pb, const char* caller, int operation_type);

/* In profile.c */

#ifdef PROFILE
#define PROFILE_POINT if (CFG_profile) profile_log(__FILE__,__LINE__) /* JCMREPL - Where is the profiling flag stored? */
#else
#define PROFILE_POINT ((void)0)
#endif

void profile_log(char *file,int line);
void profile_open();
void profile_close();

/* in repl_controls.c */
void add_repl_control_mods( Slapi_PBlock *pb, Slapi_Mods *smods );

/* ... */
void create_entity (char* DN, const char* oclass);

void write_replog_db( int optype, char *dn, void *change, int flag, changeNumber changenum, time_t curtime, LDAPMod **modrdn_mods );
int entry2reple( Slapi_Entry *e, Slapi_Entry *oe );
int mods2reple( Slapi_Entry *e, LDAPMod **ldm );
int modrdn2reple( Slapi_Entry *e, char *newrdn, int deloldrdn, LDAPMod **ldm );

/* In legacy_consumer.c */
void process_legacy_cf(Slapi_PBlock *pb);
int legacy_consumer_is_replicationdn(char *dn);
int legacy_consumer_is_replicationpw(struct berval *creds);
int legacy_consumer_config_init();

/* function that gets called when a backend state is changed */
void legacy_consumer_be_state_change (void *handle, char *be_name,
	                                          int old_be_state, int new_be_state);

#endif /* _REPL_H_ */



