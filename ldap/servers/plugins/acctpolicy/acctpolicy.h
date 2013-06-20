/******************************************************************************
Copyright (C) 2009 Hewlett-Packard Development Company, L.P.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 2 as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

Contributors:
Hewlett-Packard Development Company, L.P.
******************************************************************************/

#include <limits.h> /* ULONG_MAX */
#include "nspr.h"

#define SLAPI_OP_FLAG_BYPASS_REFERRALS  0x40000

#define CFG_LASTLOGIN_STATE_ATTR "stateAttrName"
#define CFG_ALT_LASTLOGIN_STATE_ATTR "altStateAttrName"
#define CFG_SPEC_ATTR "specAttrName"
#define CFG_INACT_LIMIT_ATTR "limitAttrName"
#define CFG_RECORD_LOGIN "alwaysRecordLogin"

#define DEFAULT_LASTLOGIN_STATE_ATTR "lastLoginTime"
#define DEFAULT_ALT_LASTLOGIN_STATE_ATTR "createTimestamp"
#define DEFAULT_SPEC_ATTR "acctPolicySubentry"
#define DEFAULT_INACT_LIMIT_ATTR "accountInactivityLimit"
#define DEFAULT_RECORD_LOGIN 1

/* attributes that no clients are allowed to add or modify */
static char *protected_attrs_login_recording [] = { "createTimestamp",
                                        NULL };

#define PLUGIN_VENDOR "Hewlett-Packard Company"
#define PLUGIN_VERSION "1.0"
#define PLUGIN_CONFIG_DN "cn=config,cn=Account Policy Plugin,cn=plugins,cn=config"

#define PLUGIN_NAME "acct-policy"
#define PLUGIN_DESC "Account Policy Plugin"
#define PRE_PLUGIN_NAME "acct-policy-preop"
#define PRE_PLUGIN_DESC "Account Policy Pre-Op Plugin"
#define POST_PLUGIN_NAME "acct-policy-postop"
#define POST_PLUGIN_DESC "Account Policy Post-Op Plugin"

#define CALLBACK_OK 0
#define CALLBACK_ERR -1
#define CALLBACK_HANDLED 1

typedef struct acct_plugin_cfg {
	char* state_attr_name;
	char* alt_state_attr_name;
	char* spec_attr_name;
	char* limit_attr_name;
	int always_record_login;
	unsigned long inactivitylimit;
} acctPluginCfg;

typedef struct accountpolicy {
	unsigned long inactivitylimit;
} acctPolicy;

/* acct_util.c */
int get_acctpolicy( Slapi_PBlock *pb, Slapi_Entry *target_entry,
	void *plugin_id, acctPolicy **policy );
void free_acctpolicy( acctPolicy **policy );
int has_attr( Slapi_Entry* target_entry, char* attr_name,
	char** val );
char* get_attr_string_val( Slapi_Entry* e, char* attr_name );
void* get_identity();
void set_identity(void*);
time_t gentimeToEpochtime( char *gentimestr );
char* epochtimeToGentime( time_t epochtime ); 
int update_is_allowed_attr (const char *attr);

/* acct_config.c */
int acct_policy_load_config_startup( Slapi_PBlock* pb, void* plugin_id );
acctPluginCfg* get_config();

