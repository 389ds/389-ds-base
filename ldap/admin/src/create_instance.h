/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * create_instance.h: create an instance of a directory server
 * 
 * Rob McCool
 */


#ifndef _create_instance_h
#define _create_instance_h

#ifdef __cplusplus
extern "C" {            /* Assume C declarations for C++ */
#endif  /* __cplusplus */


#ifdef XP_UNIX
#define PRODUCT_NAME "slapd"

#define PRODUCT_BIN "ns-slapd"

#endif

typedef struct {
    char *sroot;

    char *servname;
    char *bindaddr;
    char *servport;
	char *suitespot3x_uid;
	char *cfg_sspt;
	char *cfg_sspt_uid;
	char *cfg_sspt_uidpw;
    char *secserv;
    char *secservport;
    char *ntsynch;
    char *ntsynchssl;
    char *ntsynchport;
    char *rootdn;
    char *rootpw;	
    char *roothashedpw;
    char *replicationdn;
    char *replicationpw;
    char *replicationhashedpw;
    char *consumerdn;
    char *consumerpw;
    char *consumerhashedpw;
    char *changelogdir;
    char *changelogsuffix;
    char *suffix;
	char *loglevel;
	char *netscaperoot;
	char *samplesuffix;
	char *testconfig;
    char *servid;
#ifdef XP_UNIX
    char *servuser;
    char *numprocs;
#endif
    char *minthreads;
    char *maxthreads;
    int  upgradingServer;

	char * start_server;

	char * admin_domain;
	char * config_ldap_url;
	char * user_ldap_url;
	int use_existing_user_ds;
	int use_existing_config_ds;
	char * disable_schema_checking;
	char * install_ldif_file;
        char *adminport;
} server_config_s;


#ifdef NS_UNSECURE
#define DEFAULT_ID "unsecure"
#else
#define DEFAULT_ID "secure"
#endif

/*
   Initialize a server config structure with default values, using sroot
   as the server root, and hn as the machine's full host name.
 */
void set_defaults(char *sroot, char *hn, server_config_s *conf);

/*
   Create a server using the given configuration structure. This affects
   files and directories in the structure's server root.  space for param_name
   should be allocated by the caller e.g. char param_name[ENOUGH_ROOM].
   If there was a problem with one of the parameters passed in for instance
   creation e.g. servport is out of range, the param_name parameter will be
   filled in with "servport" and the error message returned will contain
   additional detail
 */
char *create_server(server_config_s *cf, char *param_name);

/* from script-gen.c */
int generate_script(const char *inpath, const char *outpath, int mode,
                    const char *table[][2]);

/* richm - moved from instindex.cpp */
int create_config(server_config_s *cf);
int parse_form(server_config_s *cf);

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif
