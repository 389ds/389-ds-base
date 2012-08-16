/**
	$Id: posix-wsp-ident.h 39 2011-06-10 08:22:11Z grzemba $
**/

#ifndef POSIX_WINSYNC_H
#define POSIX_WINSYNC_H


#define POSIX_WINSYNC_PLUGIN_NAME "posix-winsync"

#define PLUGIN_MAGIC_VENDOR_STR "contac Datentechnik GmbH"
#define PRODUCTTEXT "1.1"
#define null NULL
#define true -1
#define false 0
#define POSIX_WINSYNC_MSSFU_SCHEMA "posixWinsyncMsSFUSchema"
#define POSIX_WINSYNC_MAP_MEMBERUID "posixWinsyncMapMemberUID"
#define POSIX_WINSYNC_CREATE_MEMBEROFTASK "posixWinsyncCreateMemberOfTask"
#define POSIX_WINSYNC_LOWER_CASE "posixWinsyncLowerCaseUID"


void * posix_winsync_get_plugin_identity();

typedef struct posix_winsync_config_struct {
    Slapi_Mutex *lock; /* for config access */
    Slapi_Entry *config_e; /* configuration entry */
    PRBool mssfuSchema; /* use W2k3 Schema msSFU30 */
    PRBool mapMemberUID; /* map uniqueMember to memberUid  */
    PRBool lowercase; /* store the uid in group memberuid in lower case */
    PRBool createMemberOfTask; /* should memberOf Plugin Task run after AD sync */
    PRBool MOFTaskCreated;
    Slapi_DN *rep_suffix; /* namingContext in DS of the replicated suffix */  
} POSIX_WinSync_Config;

int posix_winsync_config(Slapi_Entry *config_e);
POSIX_WinSync_Config *posix_winsync_get_config();
PRBool posix_winsync_config_get_mapMemberUid();
PRBool posix_winsync_config_get_msSFUSchema();
PRBool posix_winsync_config_get_lowercase();
PRBool posix_winsync_config_get_createMOFTask();
Slapi_DN *posix_winsync_config_get_suffix();
void posix_winsync_config_reset_MOFTaskCreated();
void posix_winsync_config_set_MOFTaskCreated();
PRBool posix_winsync_config_get_MOFTaskCreated();

int posix_group_task_add(Slapi_PBlock *pb, Slapi_Entry *e,
    Slapi_Entry *eAfter, int *returncode, char *returntext,
    void *arg);


#endif
