/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * libadmin.h - All functions contained in libadmin.a
 *
 * All blame goes to Mike McCool
 */

#ifndef	libadmin_h
#define	libadmin_h

#include <stdio.h>
#include <limits.h>

#include "base/systems.h"
#include "base/systhr.h"
#include "base/util.h"
 
#include "frame/objset.h"
#include "frame/req.h"

#ifdef XP_UNIX
#include <unistd.h>
#else /* XP_WIN32 */
#include <winsock.h>
#endif /* XP_WIN32 */

#include "prinit.h"
#include "prthread.h"
#include "prlong.h"

#define NSPR_INIT(Program) (PR_Init(PR_USER_THREAD, PR_PRIORITY_NORMAL, 8))

#undef howmany

#define ADM_CONF "admin.conf"
#define MAGNUS_CONF "magnus.conf"
#define OBJ_DATABASE "obj.conf"
#define MIME_TYPES "mime.types"
#define NSADMIN_CONF "ns-admin.conf"
#define CERT_LOG "cert.log"

#define SERVER_KEY_NAME "Server-Key"
#define SERVER_CERT_NAME "Server-Cert"

#define DBPW_USER "admin"
#define DB_BAD_INPUT_CHARS "<>\""
#define AUTHDB_ACL_FAIL -1
#define AUTHDB_ACL_ODD_ACL -2
#define AUTHDB_ACL_NOT_FOUND -3

#define ACLNAME_READ_COOKIE "formgen-READ-ACL"
#define ACLNAME_WRITE_COOKIE "formgen-WRITE-ACL"

#define USERNAME_KEYWORD "USERNAME"

typedef struct authInfo_s authInfo_t;
struct authInfo_s {
    char *type;
    char *db_path;
    char *prompt;
};  

/* Not defined in any nspr header file, why? */
PRNetAddr *PR_CreateNetAddr(int PR_IpAddrNull, PRUint16 port);

NSPR_BEGIN_EXTERN_C

NSAPI_PUBLIC char *get_ip_and_mask(char *candidate);   
NSAPI_PUBLIC int groupOrUser(char *dbname, char *name,
			     int *is_user, int *is_group); 
NSAPI_PUBLIC int is_readacl(char *name);
NSAPI_PUBLIC int is_writeacl(char *name);
NSAPI_PUBLIC char *get_acl_file(void); /* Full path to file used by server. */
NSAPI_PUBLIC char *get_workacl_file(void); /* Full path to file updated by ACL forms. */
NSAPI_PUBLIC int get_acl_names(char **readaclname,
			       char **writeaclname, char *dir);
NSAPI_PUBLIC int get_acl_info(char *acl_file, char *acl_name,
			      void **acl_context, char ***hosts,
			      authInfo_t **authinfo,
			      char ***users, char ***userhosts,
			      int *fdefaultallow);
NSAPI_PUBLIC int set_acl_info(char *acl_file, char *acl_name, int prefix,
			      void **pacl, char **rights,
			      char **hosts, authInfo_t *authinfo,
			      char **users, char **userhosts,
			      int fdefaultallow);
NSAPI_PUBLIC int delete_acl_by_name(char *acl_file, char *acl_name);

NSAPI_PUBLIC int str_flag_to_int(char *str_flag); 
NSAPI_PUBLIC int admin_is_ipaddr(char *p);
NSAPI_PUBLIC void get_hostnames_and_ipaddrs(char **hosts,
					    char **hostnames, char **ipaddrs);
NSAPI_PUBLIC void load_host_array(char ***hosts,
				  char *hostnames, char *ipaddrs);
NSAPI_PUBLIC void load_users_array(char ***users,
				   char *usernames, char *groups);
NSAPI_PUBLIC void get_users_and_groups(char **users, char **usernames,
				       char **groups, char *dbname);  
NSAPI_PUBLIC char * str_unquote(char * str);

extern NSAPI_PUBLIC char *acl_read_rights[];
extern NSAPI_PUBLIC char *acl_write_rights[];

#ifdef USE_ADMSERV
#define CONFDIR(x) get_conf_dir(x)
#define ACLDIR(x) get_acl_dir(x)
#define COMMDEST(x) get_commit_dest(x)
#define SERVER_NAMES getenv("SERVER_NAMES")
#define ADMCONFDIR getenv("ADMSERV_ROOT")
#else
#define ACLDIR(x) "../../httpacl/"
#define CONFDIR(x) "../config/"
#define ADMCONFDIR "../config/"
#endif

#ifdef XP_UNIX
#define FILE_PATHSEP '/'
#define OPEN_MODE "r"
#define QUOTE ""
#define CONVERT_TO_NATIVE_FS(Filename)
#define CONVERT_TO_HTTP_FORMAT(Filename)
#define WSACleanup()

#undef GET_QUERY_STRING
#define GET_QUERY_STRING() (getenv("QUERY_STRING"))
#define NOT_ABSOLUTE_PATH(str) (str[0] != '/')
#define CREATE_DIRECTORY(Directory)
#define FILE_LOCK_PATH (get_flock_path())

#else /* XP_WIN32 */
#define verify_adm_dbm
#define add_user_dbm
#define find_user_dbm
#define list_users_dbm
#define modify_user_dbm
#define remove_user_dbm
#define dbm_open
#define dbm_close
#define dbm_store
#define lstat stat
#define popen _popen
#define pclose _pclose

#define CONVERT_TO_NATIVE_FS(Filename) 	   \
{									   	   \
	register char *s;				   	   \
	if (Filename)						   \
		for (s = Filename; *s; s++) 	   \
			if ( *s	== '/')				   \
				*s = '\\';				   \
}									 
#define CONVERT_TO_HTTP_FORMAT(Filename) 	\
{									   		\
	register char *s;					   	\
	if (Filename)				   			\
	for (s = Filename; *s; s++) 	   		\
		if ( *s	== '\\')				   	\
			*s = '/';				   		\
}									 
#define FILE_PATHSEP '/'
#define OPEN_MODE "r+b"
#define QUOTE "\""


#undef GET_QUERY_STRING
#define GET_QUERY_STRING() (GetQueryNT())
/* Defined in util.c */
NSAPI_PUBLIC char *GetQueryNT(void);
#define NOT_ABSOLUTE_PATH(str) \
  ((str[0] != '/') && (str[0] != '\\') && (str[2] != '/') && (str[2] != '\\'))

#define CREATE_DIRECTORY(Directory) CreateDirectory(Directory, NULL)
#define FILE_LOCK_PATH (get_flock_path()) 

#endif /* XP_WIN32 */


/* error types */
#define FILE_ERROR 0
#define MEMORY_ERROR 1
#define SYSTEM_ERROR 2
#define INCORRECT_USAGE 3
#define ELEM_MISSING 4
#define REGISTRY_DATABASE_ERROR 5
#define NETWORK_ERROR 6
#define GENERAL_FAILURE 7
#define WARNING 8

/* The upper bound on error types */
#define MAX_ERROR 9

/* The default error type (in case something goes wrong */
#define DEFAULT_ERROR 3

/* The change types for admin logging */
#define TO_MAGNUS "magnus.conf"
#define TO_OBJCONF "obj.conf"
#define TO_ACLFILE "generated.acl"
#define TO_STATUS "status"
#define TO_ADMIN "admserv"
#define TO_USERDB "userdb"
#define TO_SEC "security"
#define TO_BACKUP "backup"
#define TO_CACHE "cache"
#define TO_BUCONF "bu.conf"
#define TO_LDAP "ldap"

/* The indexes for conf file backup purposes */ 
#define BK_MAGNUS 0
#define BK_OBJ 1
#define BK_MIMETYPES 2
#define BK_BU 3
#define BK_ACLFILE 4     

/* The extension for backup files to use.  Emacs weenies like "%s.~%d~" */
/* But real vi men like this one */
#define BACKUP_EXT "%s.v%d"
/* Need also a way to identify the backup files when we're doing an ls */
#define BACKUP_SHORT ".v"

/* User database defines */
#define IS_A_DBM 1
#define IS_A_NCSA 2

#define REMOVE_FROM_DB "-REMOVE_THIS_USER"
#define DB_INC "inc"
#define NCSA_EXT "pwf"

/* We now use the client DB libs, so they're all '.db' with no second file. */
#define DBM_EXT_1 "db"
#define DBM_EXT_2 NULL

/* Define the functions in a central place so that obj.conf viewer can get 
 * to them */
#ifdef MCC_PROXY
#define BASIC_NCSA_FN   "proxy-auth"
#define REQUIRE_AUTH_FN "require-proxy-auth"
#define CHECK_ACL_FN  "check-acl"
#else
#define BASIC_NCSA_FN   "basic-ncsa"
#define REQUIRE_AUTH_FN "require-auth"
#define CHECK_ACL_FN  "check-acl"
#endif


/* Frame window names. */
#define INDEX_NAME "index"
#define MESSAGE_NAME "msgs"
#define TOP_NAME "tabs"
#define BOTTOM_NAME "category"
#define OPTIONS_NAME "options"
#define CONTENT_NAME "content"
#define COPY_NAME "copy"

#define INFO_IDX_NAME "infowin"
#define INFO_TOPIC_NAME "infotopic"
#define HELP_WIN_OPTIONS "'resizable=1,width=500,height=500'"


/* pblock types, either it's a ppath, or it's a name. */
#define PB_NAME 1
#define PB_PATH 2

/* Resource types */
#define NAME "name"
#define FILE_OR_DIR "path"
#define TEMPLATE "tmpl"
#define WILDCARD "wild"

/* A really big form line */
#define BIG_LINE 1024

/* Max size for a pathname */
#ifndef PATH_MAX
#define PATH_MAX 256
#endif


/* Boundary string for uploading / downloading config files. */
#define CF_BOUNDARY  "--Config_File_Boundary--"
#define CF_NEWCONFIG "--NewConfigFile:"
#define CF_MTIME "--LastMod:"
#define CF_ERRSTR "--Error: "
#define CFTRANS_BIN "bin/cftrans"
#define CF_REMOTE_URL "#RemoteUrl "
 
#define HTML_ERRCOLOR "#AA0000"

#define MOCHA_NAME "JavaScript"

/* Internationalization stuffs.  If we define MSG_RETURN, then create a 
 * function which will return a string of the given identifier.  If we 
 * define MSG_DBM, it creates a function you can call to create the DBM
 * properly.  Finally, if nothing else, it will create a mapping from 
 * the string's name to its proper ID number. */
/* store_msg is in mkdbm.c, in the admin stuff */
/* get_msg.c */
NSAPI_PUBLIC char *get_msg(int msgid);
NSAPI_PUBLIC void store_msg(int msgid, char *msg);
 
#if defined(MSG_RETURN)
#define BGN_MSG(arg) static char *(arg)(int i)  { switch(i)  {
#define STR(name, id, msg) case (id): return(msg);
#define END_MSG(arg) } return 0; }
 
#elif defined(MSG_DBM)
#define BGN_MSG(arg) void (arg)()  {
#define STR(name, id, msg)  store_msg(id, msg);
#define END_MSG(arg) }
 
#else
#define BGN_MSG(arg) enum {
#define STR(name, id, msg)    name=id,
#define END_MSG(arg) arg=0 };
#endif
 
/* The files where the messages are kept. */
#define LA_BASE 1000
#define LA_BASE_END 1999
#define LA_DBM_LOC "./la_msgs"
 
#define HADM_BASE 2000
#define HADM_BASE_END 5999
#define HADM_DBM_LOC "./hadm_msgs"
 
#include "la_msgs.i"
#include "hadm_msgs.i"
 
/* Initialize libadmin.  Should be called by EVERY CGI. */
/* util.c */
NSAPI_PUBLIC int ADM_Init(void);

/* Open a .html file to parse it.  Returns a file ptr (simple fn, really) */
/* error one doesn't call report_error so we lose the infinite loop prob */
/* form_get.c */
NSAPI_PUBLIC FILE *open_html_file(char *filename);
NSAPI_PUBLIC FILE *open_error_file(char *filename);

/* Same as open_html_file, but opens the html file from the specified */
/* language subdirectory, if available, else from the default language */
/* subdirectory. */
/* form_get.c */
NSAPI_PUBLIC FILE* open_html_file_lang(char* filename,char* language);

/* Parse an HTML file and return it to the client.  */
/* form_get.c */
NSAPI_PUBLIC void return_html_file(char *filename);

/* Parse an HTML file, return it to the client, but don't set the referer */
/* form_get.c */
NSAPI_PUBLIC void return_html_noref(char *filename);

/* Output an input of an arbitrary type.  Not really that flexible. */
/* form_get.c */
NSAPI_PUBLIC void output_input(char *type, char *name, char *value, char *other);

/* Get the next line from the file.  Returns 0 when EOF is encountered. */
/* form_get.c */
NSAPI_PUBLIC int next_html_line(FILE *f, char *line);



/* Get the referer from the config file */
/* referer.c */
NSAPI_PUBLIC char *get_referer(char **config);

/* Set the referer and write out the config file */
/* referer.c */
NSAPI_PUBLIC void set_referer(char **config);

/* Sets the referer to a script that's not you.  If new_ref is an absolute ref,
 * it will cat that with SERVER_URL; if it's not, it will replace the
 * current script name with new_ref. */
/* referer.c */
NSAPI_PUBLIC void set_fake_referer(char *new_ref);

/* Redirect the person to the Referer, or give a short error message */
/* referer.c */
NSAPI_PUBLIC void redirect_to_referer(char *addition);

/* Opens the referer in the content window using JavaScript */
/* referer.c */
NSAPI_PUBLIC void js_open_referer(void);

/* Redirect to the given script. Assumes that SCRIPT_NAME is set to a script */
/* referer.c */
NSAPI_PUBLIC void redirect_to_script(char *script);


/* Filter a line using templates, and spit the results to stdout */
/* template.c */
NSAPI_PUBLIC int parse_line(char *line, char **input);

/* Since everyone seems to be doing this independently, at least centralize
   the code.  Useful for onClicks and automatic help */
NSAPI_PUBLIC char *helpJavaScript();
NSAPI_PUBLIC char *helpJavaScriptForTopic( char *topic );

/* Check to see if a directive the parser didn't know about is a given 
 * directive */
/* template.c */
NSAPI_PUBLIC int directive_is(char *target, char *directive);

/* Export the pageheader because sec-icrt uses it --MLM */
/* template.c */
NSAPI_PUBLIC void pageheader(char **vars, char **config);


/* Report an error.  Takes 3 args: 1. Category of error 
 *                                 2. Some more specific category info (opt)
 *                                 3. A short explanation of the error. 
 * 
 * report_warning: same thing except doesn't exit when done whining
 */
/* error.c */
NSAPI_PUBLIC void output_alert(int type, char *info, char *details, int wait);
NSAPI_PUBLIC void report_error(int type, char *info, char *details);
NSAPI_PUBLIC void report_warning(int type, char *info, char *details);

/* Read the administrative config from the server admin root */
/* Mult adm gets a particular adm config (for multiple server config) */
/* admconf.c */
NSAPI_PUBLIC char **get_adm_config(void);
NSAPI_PUBLIC char **get_mult_adm_config(int whichone);

/* Write the administrative config back to the file */
/* Mult adm saves a particular adm config (for multiple server config) */
/* admconf.c */
NSAPI_PUBLIC int write_adm_config(char **config);
NSAPI_PUBLIC int write_mult_adm_config(int whichone, char **config);

/* An additional level of abstraction for resource grabbing.  Gets the current
 * resource from the config set. */
/* admconf.c */
NSAPI_PUBLIC char *get_current_resource(char **config);

/* Gets the string of the current resource type */
/* admconf.c */
NSAPI_PUBLIC char *get_current_typestr(char **config);

/* Gets the pblock type of the current resource from the config set. */
/* admconf.c */
NSAPI_PUBLIC int get_current_restype(char **config);

/* Sets the current resource given its type and its data. */
/* admconf.c */
NSAPI_PUBLIC void set_current_resource(char **config, char *nrestype, char *nres);


/* Get the value of a particular variable in magnus.conf */
/* get_num_mag_var: get only a particular server's value for it */
/* magconf.c */
NSAPI_PUBLIC char *get_mag_var(char *var);
NSAPI_PUBLIC char *get_num_mag_var(int whichsrv, char *var);

/* Set the value of a particular variable in magnus.conf */
/* magconf.c */
NSAPI_PUBLIC void set_mag_var(char *name, char *value);

/* Get the value of a particular variable in cert.log */
NSAPI_PUBLIC char *get_cert_var(char *var);
NSAPI_PUBLIC char *get_num_cert_var(int whichsrv, char *var);

/* Set the value of a particular variable in cert.log */
NSAPI_PUBLIC void set_cert_var(char *name, char *value);

/* Get the value of a particular variable in ns-admin.conf */
/* admserv.c */
NSAPI_PUBLIC char *get_nsadm_var(char *var);
NSAPI_PUBLIC char **scan_server_instance(char *, char **);


/* Set the value of a particular variable in ns-admin.conf */
/* admserv.c */
NSAPI_PUBLIC void set_nsadm_var(char *name, char *value);

/* List all of the installed servers on the admin server.  */
/* Takes 1 arg (string list of identifiers for servers, such as */
/* httpd, https, proxy, news) */
/* admserv.c */
NSAPI_PUBLIC char **list_installed_servers(char **namelist);

/* Reads in the list of servers installed on this machine.  Fills in 
 * two string lists (one of names, one of descriptions.)  *servlist and
 * *desclist will be allocated for you. */
NSAPI_PUBLIC void read_server_lst(char ***namelist, char ***desclist);
NSAPI_PUBLIC void read_keyalias_lst(char ***namelist);
NSAPI_PUBLIC void read_certalias_lst(char ***namelist);
NSAPI_PUBLIC void get_key_cert_files(char *alias, char **keyfile, char **certfile);
NSAPI_PUBLIC void display_aliases(char *keyfile, char **aliaslist);

/* Create a new object (i.e. empty "<Object name=foo></Object>" in the
 * config files. */
/* objconf.c */
NSAPI_PUBLIC void add_object(int objtype, char *id);

/* Destroy a given object and all its contents.  */
/* objconf.c */
NSAPI_PUBLIC void delete_object(int objtype, char *id);

/* Grab a given object  */
/* objconf.c */
NSAPI_PUBLIC httpd_object *grab_object(int objtype, char *id);

/* List all objects of the given type. */
/* objconf.c */
NSAPI_PUBLIC char **list_objects(int objtype);

/* Count how many objects there are of the given type. */
/* objconf.c */
NSAPI_PUBLIC int count_objects(int objtype);

/* Return the total number of objects in the configuration. */
/* objconf.c */
NSAPI_PUBLIC int total_object_count(void);

/* Find a particular instance of a parameter in a particular object and a
 * particular directive.  id_type and id_value are optional parameter 
 * specifiers if you want not just the first instance of a function.
 */
/* objconf.c */
NSAPI_PUBLIC pblock *grab_pblock(int objtype, char *object, char *directive, 
                    char *function, char *id_type, char *id_value);

/* Grab a pblock, but don't use the "fn" parameter.  Instead of "fn",
 * use the string "fname" to identify the block. */
/* objconf.c */
NSAPI_PUBLIC pblock *grab_pblock_byid(int objtype, char *object, char *directive, 
                         char *fname, char *function, char *id_type, 
                         char *id_value);

/* Add a new parameter block into the given object, of the given directive
 * type, using the given function, and with the list of parameters given
 * (should be called like this:)
 *
 * add_pblock(PB_NAME, "default", "NameTrans", "pfx2dir", 
 *            4, "from", "/foo", "dir", "/bar");
 * Returns the new pblock for posterity
 */
/* objconf.c */
NSAPI_PUBLIC pblock *add_pblock(int objtype, char *object, char *directive, char *function,
                int nargs, ...);

/* Create a new pblock, but don't save it or anything. */
/* objconf.c */
NSAPI_PUBLIC pblock *new_pblock(char *function, int nargs, ...);

/* Destroy a paramter block.  Same call patterns as grab_pblock. */
/* objconf.c */
NSAPI_PUBLIC void delete_pblock(int objtype, char *object, char *directive, char *function,
                    char *id_type, char *id_value);

/* Set the values of a given pblock to these new values.  Arg passing is same
 * as for add_pblock()
 */
/* objconf.c */
NSAPI_PUBLIC void set_pblock_vals(pblock *pb, int nargs, ...);

/* List all the pblocks you can find with the given object, directive, and
 *  function.  Returns a pointer to a list of pblock *'s just like a strlist.
 */
/* objconf.c */
NSAPI_PUBLIC pblock **list_pblocks(int objtype, char *object, char *direct, char *function);

/* Get the client pblock from a given directive, specified as above in 
 * grab_pblock.
 */
/* objconf.c */
NSAPI_PUBLIC pblock *grab_client(int objtype, char *object, char *directive, 
                    char *function, char *id_type, char *id_value);

/* Add a client pblock to a given object.  If you have a pblock, send it
 * in oldpb, if not, send NULL and it will create one with a 
 * "PathCheck fn=deny-existence" directive for you.
 *
 * Send the nargs just like above; assumedly there's only two: client and ip.
 */
/* objconf.c */
NSAPI_PUBLIC void add_client(int objtype, char *object, char *direct, 
                pblock *oldpb, int nargs, ...);

/* List all the clients you can find with the given object, directive, and
 *  function.  Returns a pointer to a list of directive *'s (struct with 
 *  two pblock ptrs: param and client)
 */
/* objconf.c */
NSAPI_PUBLIC directive **list_clients(int objtype, char *object, char *direct, 
                         char *function);

/* Delete a client, as identified by directive, path=blah in param part,
 * dns=blah in client part, and ip=blah in client part. */
/* objconf.c */
NSAPI_PUBLIC void delete_client(int objtype, char *object, char *direct, char *path, 
                   char *dns, char *ip);

/* Gets the directive associated with a given pblock. */
/* objconf.c */
NSAPI_PUBLIC directive *get_pb_directive(int objtype, char *object, 
                            char *directive, pblock *pb);
NSAPI_PUBLIC directive *get_cl_directive(int objtype, char *object, 
                            char *directive, pblock *cl);

/* Delete a pblock by its pointer.  (Note: I should have done this function
 * long ago.  Grr. */
/* objconf.c */
NSAPI_PUBLIC void delete_pblock_byptr(int objtype, char *object, 
                         char *directive, pblock *pb);

/* Init directives are now in obj.conf, deal with them there. */
/* ---------------------------------------------------------- */
 
/* Get the value of an init variable in pblock form. */
/* objconf.c */
NSAPI_PUBLIC pblock *get_mag_init(char *fn);
 
/* Get only a particular mag init */
/* objconf.c */
NSAPI_PUBLIC pblock *get_specific_mag_init(char *fn, char *name, char *value);

/* Get all instances of the same Init function as an array of pblock ptrs */
/* objconf.c */
NSAPI_PUBLIC pblock **get_all_mag_inits(char *fn);
 
/* Set the value of an init variable.  If it exists, modify existing, if not,
 * create it.
 * If the key_nam and key_val are set, also the parameter named key_val
 * will be used when matching against the specific directive.  This will
 * allow multiple calls to the same Init function, with a specific parameter
 * value together with the function name uniquely identifying the specific
 * Init function call.
 */
/* objconf.c */
NSAPI_PUBLIC void set_mag_init(char *fn, char *key_nam, char *key_val, int nargs, ...);
 
/* Delete an instance of an Init variable. */
/* objconf.c */
NSAPI_PUBLIC void delete_mag_init(char *fn);
NSAPI_PUBLIC void delete_specific_mag_init(char *fn, char *key_nam, char *key_val);
 

/* Commit all outstanding config stuff from admin directory to the actual
 * server.  Does not restart the server. */
/* Argument authlist is a string list of authorization strings 
 * (username:password) to send to remote servers (or NULL if it is a 
 * local machine.) */
/* commit.c */
NSAPI_PUBLIC int do_commit(char **authlist);

/* Back out from outstanding changes. Authlist same as above. */
/* commit.c */
NSAPI_PUBLIC int do_undo(char **authlist);

/*  Prints outstanding changes to server to stdout. */
/* commit.c */
NSAPI_PUBLIC void output_uncommitted(void);

/* Returns a flag saying whether there are outstanding changes that need to
 * be committed.  If you've already read in admin.conf, send a pointer to 
 * it here.  Or else send NULL, and it'll read it in.  */
/* commit.c */
NSAPI_PUBLIC int needs_commit(char **config);

/* Sets the flag to say whether we need to commit or not.  1 means "yes,
 * we need to commit."  0 means "No, I just committed the changes." 
 * whichsrv is which server to set the bit in (if you're configuring 
 * multiple servers.) */
/* commit.c */
NSAPI_PUBLIC void set_commit(int whichsrv, int needscommit);

/* Returns an int for which backup number to use. 0=magnus, 1=obj*/
/* index is which server among the list you want to use (mult config) */
/* commit.c */
NSAPI_PUBLIC int get_bknum(int which, int index);

/* Sets the current backup number. */
/* index is which server among the list you want to use. */
/* commit.c */
NSAPI_PUBLIC void set_bknum(int num, int which, int index);

/* Backs up given file, using number in admconf. */
/* commit.c */
NSAPI_PUBLIC void conf_backup(char *whichfile, int index, int whichsrv);

/* Gets the last known modification time for a config file.  
 * When you do a commit, this is set to the mod time after you do
 * the commit.  Later, when you want to see if the file you're about
 * to upload has changed, you check this value. */
/* commit.c */
NSAPI_PUBLIC time_t get_org_mtime(int whichsrv, int whichfile);

/* Gets and sets the three modification times as they were stored in 
 * admin.conf. */
/* Useful in remote transactions. */
/* commit.c */
NSAPI_PUBLIC char *get_mtime_str(int whichsrv);
NSAPI_PUBLIC void set_mtime_str(int whichsrv, char *str);

/* Sets that same value (see above) */
/* commit.c */
NSAPI_PUBLIC void set_org_mtime(int whichsrv, int whichfile, time_t mtime);

/* Set the modification times for *all* of the files needing this check,
 * assuming admin.conf got lost or hasn't been created yet. */
/* When it doubt, set to zero. */
NSAPI_PUBLIC void set_all_org_mtimes(void);


/* Create an internal list of the servers which are being changed. */
/* Returns the total number of servers in the list. */
/* multconf.c */
NSAPI_PUBLIC int make_conflist(void);

/* Don't use this function.  It's a grotesque hack.  It's used by the admin
 * page to fake the on/off buttons for the servers. */
/* multconf.c */
NSAPI_PUBLIC int fake_conflist(char *fakename);

/* Get the current admin config directory.  Takes an int to say which one
 * (of the list of servers to configure) you're interested in, so you can
 * for loop through them.  Always use 0 if you want the first one. */
/* multconf.c */
NSAPI_PUBLIC char *get_conf_dir(int whichone);
NSAPI_PUBLIC char *get_alias_dir(void);
NSAPI_PUBLIC void read_alias_files(char ***aliasfiles);
NSAPI_PUBLIC void read_aliases(char ***aliaslist);
 
/* Return 1 if this server number whichone is a not on the local machine. */
/* multconf.c */
NSAPI_PUBLIC int is_remote_server(int whichone);

/* Return 1 if we are configuring the admin server. */
/* multconf.c */
NSAPI_PUBLIC int is_admserv(void);

/* Return 1 if there is a remote server in the list of servers to config. */
/* Return 0 if not. */
/* multconf.c */
NSAPI_PUBLIC int remote_server_inlist(void);

/* Get the ultimate destination for a particular config file set.  Same 
 * arg as above function. */
/* multconf.c */
NSAPI_PUBLIC char *get_commit_dest(int whichone);

/* Get the name of the indicated server (for logging purposes etc.) */
/* Send -1 for a string with all of them. */
/* multconf.c */
NSAPI_PUBLIC char *get_srvname(int whichsrv);


/* Some simple buffering tools */
/* Keeps a buffer for network info, and a buffer for returning lines */
/* httpcon.c */
typedef struct bufstruct {
    char *buf;
    int bufsize;
    int curpos;
    int inbuf;
    char *hbuf;
    int hbufsize;
    int hbufpos;
} bufstruct;
 
/* Make a new buffer.  Flush the rest of a buffer (leaving the contents
 * unread.  Delete a buffer structure. */
/* httpcon.c */
NSAPI_PUBLIC bufstruct *new_buffer(int bufsize);
NSAPI_PUBLIC void flush_buffer(bufstruct *buf);
NSAPI_PUBLIC void delete_buffer(bufstruct *buf);

/* stdio replacement for a network connection (so shoot me) */
/* httpcon.c */
NSAPI_PUBLIC char *get_line_from_fd(PRFileDesc *fd, bufstruct *buf);

/* send a line to a remote server (equivalent to write()) */
/* httpcon.c */
NSAPI_PUBLIC int send_line_to_fd(PRFileDesc *fd, char *line, int linesize);

/* Decompose a URL into protocol, server, port, and URI.  You needn't allocate
 * the strings you're passing, will be done for you. */
/* httpcon.c */
NSAPI_PUBLIC int decompose_url(char *url, char **protocol, char **server, unsigned int *port, char **uri);

/* Take a status line "HTTP/1.0 200 OK" or some such and produce a protocol
 * status number. */
/* httpcon.c */
NSAPI_PUBLIC int parse_status_line(char *statusline);

/* Returns whether the headers have now ended (with the line you give it) */
/* httpcon.c */
NSAPI_PUBLIC int is_end_of_headers(char *hline);

/* Make an HTTP request to a given server, running on a given port, 
 * with the given initial request.  Returns a FD that can be used 
 * to read / write to the connection. */
/* Note: Reports status to stdout in HTML form.  Bad?  Perhaps... */
/* httpcon.c */
NSAPI_PUBLIC PRFileDesc *make_http_request(char *protocol, char *server, unsigned int port, char *request, int *errcode);

/* Terminate an HTTP request session (see above) */
/* httpcon.c */
NSAPI_PUBLIC void end_http_request(PRFileDesc *req_socket);

/* Verify that given server is an admin server. */
NSAPI_PUBLIC int verify_is_admin(char *protocol, char *server, int port);


/* Log a change in the verbose admin log.  kind is a string representing
 * what kind of change it was (see #defines at top of file, such as MAGNUS_LOG)
 * Change is the text of the change, in printf format (so you can give args). */
/* admlog.c */
NSAPI_PUBLIC void log_change(char *kind, char *change, ...);

/* Get a pretty string for the current resource for logging. */
/* admlog.c */
NSAPI_PUBLIC char *log_curres(char **config);


/* List all the user databases (actually, all files) in a given path into a 
 * strlist. */
/* userdb.c */
NSAPI_PUBLIC char **list_user_dbs(char *fullpath);

NSAPI_PUBLIC char **list_auth_dbs(char *fullpath);

/* Output the 1.x database selector.  Path is the path to the DB's, element is
 * the desired SELECT name, current is the one that should currently be
 * selected. */
/* userdb.c */
NSAPI_PUBLIC void output_db_selector(char *path, char *element, char *current);

/* Output the 2.x database selector.  Path is the path to the DB's, element is
 * the desired SELECT name, current is the one that should currently be
 * selected. */
NSAPI_PUBLIC void output_authdb_selector(char *path, char *element, char *current);

/* Sets which DB is considered current. */
/* userdb.c */
NSAPI_PUBLIC void set_current_db(char *current); /* obsolete 1.x */

/* Sets which DB is considered current (2.x version). */
NSAPI_PUBLIC void set_current_authdb(char *current);
NSAPI_PUBLIC char *get_current_authdb(void);

/* Detect the type of the given database. */
/* WARNING: REMOVES THE EXTENSION!!! */
/* userdb.c */
NSAPI_PUBLIC int detect_db_type(char *db_name);

/* Find a user within an NCSA database, and return */
/* userdb.c */
NSAPI_PUBLIC char *find_user_ncsa(char *db, char *user);

/* Add a user to an NCSA style database */
/* userdb.c */
NSAPI_PUBLIC void add_user_ncsa(char *db, char *user, char *password, int enc);

/* List all the users in an NCSA style database */
/* userdb.c */
NSAPI_PUBLIC char **list_users_ncsa(char *db);

/* Modify a user in an NCSA style database */
/* userdb.c */
NSAPI_PUBLIC int modify_user_ncsa(char *db, char *user, char *pw);

/* Verify the admin password, or die.  Returns 1 if there is one, 0 if not */
/* userdb.c */
NSAPI_PUBLIC int verify_adm_ncsa(char *db, char *pw);

/* Remove a user from an NCSA style database */
/* userdb.c */
NSAPI_PUBLIC int remove_user_ncsa(char *db, char *user);

#ifdef XP_UNIX /* WIN32 has no DBM */
/* Find a user within a DBM database, and return */
/* userdb.c */
char *find_user_dbm(char *db, char *user);

/* Add a user to a DBM database */
/* userdb.c */
void add_user_dbm(char *db, char *user, char *password, int enc);

/* List all the users in a DBM */
/* userdb.c */
char **list_users_dbm(char *db);

/* Modify a user in a DBM database */
/* userdb.c */
int modify_user_dbm(char *db, char *user, char *pw);

/* Verify the admin password, or die.  Returns 1 if there is one, 0 if not */
/* userdb.c */
int verify_adm_dbm(char *db, char *pw);

/* Remove a user from a DBM */
/* userdb.c */
int remove_user_dbm(char *db, char *user);

#endif /* WIN32 */


/* Checks to see if server is running.  Doesn't work over network.  Returns 0
 * if it's down, 1 if it's up, -1 if an error occurred. */
/* pcontrol.c */
NSAPI_PUBLIC int is_server_running(int whichsrv);

/* Starts up the HTTP server. Puts the errors into /tmp/startup.[pid] */
/* Returns 0 on success, 1 on failure */
/* Restart restarts it, shutdown shuts it down */
/* pcontrol.c */
NSAPI_PUBLIC int startup_http(int, char*, char *);
NSAPI_PUBLIC int restart_http(int, char*, char *);
NSAPI_PUBLIC int shutdown_http(int, char*);

/* As above, but for SNMP HTTP subagent */
/* pcontrol.c */
NSAPI_PUBLIC int startup_snmp();
NSAPI_PUBLIC int restart_snmp();
NSAPI_PUBLIC int shutdown_snmp();

/* Performs the request rq, for server (in list) whichsrv, using auth as
 * auth info.
 * 
 * successmsg is the prefix on lines that are returned from the remote
 * server that indicate success. */
/* pcontrol.c */
NSAPI_PUBLIC int perform_request(char *req, int whichsrv, char *auth, char *successmsg);

/* Escapes a shell command for system() calls.  NOTE: This string should
 * be large enough to handle expansion!!!! */
/* util.c */
NSAPI_PUBLIC void escape_for_shell(char *cmd);

/* Lists all files in a directory.  If dashA list .files except . and .. */
/* util.c */
NSAPI_PUBLIC char **list_directory(char *path, int dashA);

/* Does a given file exist? */
/* util.c */
NSAPI_PUBLIC int file_exists(char *filename);

/* What's the size of a given file? */
/* util.c */
NSAPI_PUBLIC int get_file_size(char *path);

/* Create a directory path if it does not exist (mkdir -p) */
/* util.c */
NSAPI_PUBLIC int ADM_mkdir_p(char *dir, int mode);

/* Copy a directory recursively. */
/* util.c */
NSAPI_PUBLIC int ADM_copy_directory(char *src_dir, char *dest_dir);

/* Remove a directory recursively. Same as remove_directory except that
   filenames arent printed on stdout */
/* util.c */
NSAPI_PUBLIC void ADM_remove_directory(char *path);

#ifdef XP_UNIX
/* Obtain Unix SuiteSpot user/group information */
/* util.c */
NSAPI_PUBLIC int ADM_GetUXSSid(char *, char **, char **);
#endif

/* Return: LastModificationTime(f1) < LastModificationTime(f2) ? */
/* util.c */
NSAPI_PUBLIC int mtime_is_earlier(char *file1, char *file2);

/* Return: the last mod time of fn */
/* util.c */
NSAPI_PUBLIC time_t get_mtime(char *fn);

/* Does this string have all numbers? */
/* util.c */
NSAPI_PUBLIC int all_numbers(char *target);
/* Valid floating point number? */
NSAPI_PUBLIC int all_numbers_float(char *target);

/* Get the [ServerRoot]/config directory. */
/* whichone is which server you're interested in.  */
/* 0 if you want the first one.*/
/* util.c */
NSAPI_PUBLIC char *get_admcf_dir(int whichone);

/* Get the admin server's [ServerRoot]/config directory */
NSAPI_PUBLIC char *get_admservcf_dir(void);

/* Get the admin/userdb directory. */
/* util.c */
NSAPI_PUBLIC char *get_userdb_dir(void);
/* Get the V2.x admin/userdb directory. */
/* util.c */
NSAPI_PUBLIC char *get_authdb_dir(void);
NSAPI_PUBLIC char *get_httpacl_dir(void);


/* V2.x User admin functions.  They take a full path of
   the directory where the databases live, and perform
   various operations on the databases.  They open and
   close the DBM, so they can not be called when the
   database is already open.  The output_xxx ones spit
   out various HTMLized admin data.
*/
NSAPI_PUBLIC int getfullname(char *dbname, char *user, char **fullname); 
NSAPI_PUBLIC int setfullname(char *dbname, char *user, char *fullname);
NSAPI_PUBLIC int setpw(char *dbname, char *user, char *pwd);
NSAPI_PUBLIC int setdbpw(char *dbname, char *pwd);
NSAPI_PUBLIC int checkdbpw(char *dbname, char *pwd);
NSAPI_PUBLIC int addusertogroup(char *dbname, char *user, char *group);
NSAPI_PUBLIC int remuserfromgroup(char *dbname, char *user, char *group);
NSAPI_PUBLIC int addgrouptogroup(char *dbname, char *memgroup, char *group);
NSAPI_PUBLIC int remgroupfromgroup(char *dbname, char *memgroup, char *group);
NSAPI_PUBLIC int output_users_list(char *line, char *userfilter);
NSAPI_PUBLIC int output_groups_list(char *dbname, char *groupfilter); 
NSAPI_PUBLIC void output_group_membership(char *dbname, char *user);
NSAPI_PUBLIC void output_nonmembership(char *dbname, char *user);
NSAPI_PUBLIC void output_grpgroup_membership(char *dbname, char *group, char *filter);
NSAPI_PUBLIC void output_user_membership(char *dbname, char *group, char *filter);
NSAPI_PUBLIC void output_nongrpgroup_membership(char *dbname, char *group, char *filter);
NSAPI_PUBLIC void output_nonuser_membership(char *dbname, char *group, char *filter);

/* Set a user's login name */
NSAPI_PUBLIC int setusername(char *db_path, char *user, char *newname);

/* Output a selector box with name "name", an option "NONE" if none=1, 
 *  and make it a multiple selector box if multiple=1.  If multiple != 1, 
 *  then make it a pulldown list if the number of groups is less than 
 *  SELECT_OVERFLOW. */
/* If highlight is non-null, specifically highlight that entry. */
/* If user is non-null, and it's a multiple box, correctly set the group
 *  membership in the multiple list (Groups they're in are on, groups they're
 *  not in are off. */
/* If group_user is one, then the variable "user" refers to *group* members,
 *  not *user* members. */
/* If except is non-null, output all entries except the "except" item. */
/* (note: this methodology is known as the "Garbage pail method", just 
 *  keep adding parameters till it does everything you want) MLM */
#define SELECT_OVERFLOW 25
NSAPI_PUBLIC void output_group_selector(char *db_path, 
                                        int group_user, char *user,
                                        char *highlight, char *except,
                                        char *name, int none, int multiple);

/* Same as above, except output a list of users, highlighting those in a
 * particular group.  MLM */
NSAPI_PUBLIC void output_user_selector(char *db_path, char *group,
                                       char *highlight, char *except,
                                       char *name, int none, int multiple);

/* Take a char ** null terminated list of group names, and change a user's
 * memberships so those are the only groups he's in.   MLM */
NSAPI_PUBLIC void change_user_membership(char *db_path, char *user,
                                         char **new_groups);

/* Take a char ** null terminated list of group names, and change a user's
 * memberships so those are the only groups he's in.   MLM */
/* If group_users is 1, then new_users are assumed to be groups. */
NSAPI_PUBLIC void change_group_membership(char *db_path, char *group,
                                          int group_users, char **new_users);


/* Get the server's URL. */
/* util.c */
NSAPI_PUBLIC char *get_serv_url(void);

/* Run a command and check the output */
struct runcmd_s {
    char *title;
    char *msg;
    char *arg;
    int sysmsg;
};
/* util.c */
NSAPI_PUBLIC int run_cmd(char *cmd, FILE *closeme, struct runcmd_s *rm);

/* This is basically copy_file from the install section, with the error
 * reporting changed to match the admin stuff.  Since some stuff depends
 * on copy_file being the install version, I'll cheat and call this one
 * cp_file. */
/* util.c */
NSAPI_PUBLIC void cp_file(char *sfile, char *dfile, int mode);

/* Delete the file with the given path.  Returns positive value on failure.*/
/* util.c */
NSAPI_PUBLIC int delete_file(char *path);

/* Delete the directory with the given path.  Returns positive value on failure.*/
/* BEWARE! Be sure to verify you're not deleting things you */
/* shouldn't.  Testing the directory with "util_uri_is_evil" */ 
/* is often a good idea. */
/* util.c */
NSAPI_PUBLIC void remove_directory(char *path);

/* Simply creates a directory that you give it.  Checks for errors and 
 * all that. (Not to be confused with create_subdirs in install, since
 * it relies on some installation stuff.) */
/* util.c */
NSAPI_PUBLIC void create_dir(char *dir, int mode);

/* Open a file, with file locking.  Close a file, releasing the lock. */
/* util.c */
NSAPI_PUBLIC FILE *fopen_l(char *pathname, char *mode);
NSAPI_PUBLIC void fclose_l(FILE *f);

/* helper function to figure out where to put the lock */
/* util.c */
NSAPI_PUBLIC char *get_flock_path(void);

/* uuencode a given buffer.  both src and dst need to be allocated.  dst
 * should be 1 1/4 as big as src (i saved some math and just made it twice
 * as big when I called it) */
/* util.c */
NSAPI_PUBLIC int do_uuencode(unsigned char *src, unsigned char *dst, int srclen);

/* Word wrap a string to fit into a JavaScript alert box. */
/* str is the string, width is the width to wrap to, linefeed is the string 
 * to use as a linefeed. */
/* util.c */
#define WORD_WRAP_WIDTH 80
NSAPI_PUBLIC char *alert_word_wrap(char *str, int width, char *linefeed);


/* Writes the given object set as the current database */
/* Takes an argument for which server in the list to dump to */
/* ns-util.c */
NSAPI_PUBLIC void dump_database(int whichsrv, httpd_objset *os);
NSAPI_PUBLIC void dump_database_tofile(int whichsrv, char *fn, httpd_objset *os);

/* Scans the given database and returns its object set. */
/* ns-util.c */
NSAPI_PUBLIC httpd_objset *read_config_from_file(char *objconf);

/* Scans the current database and returns its object set. */
/* Takes a number for which server in multiple list to read */
/* ns-util.c */
NSAPI_PUBLIC httpd_objset *read_config(int x);

/* Inserts a new pfx2dir name translation into the object, making sure there
 * are no name conflicts. Name conflict resolution is simple: Keep the longest
 * from fields first in the file. */
/* ns-util.c */
NSAPI_PUBLIC void insert_ntrans(pblock *p, pblock *c, httpd_object *obj);

/* Inserts a new assign-name and mkssi-version into the object, making sure 
 * that they come first and are sorted. */
NSAPI_PUBLIC void insert_ntrans_an(pblock *p, pblock *c, httpd_object *obj);

/* Inserts a new mkssi-pcheck into the object, making sure that they come 
 * first and are sorted. */
NSAPI_PUBLIC void insert_pcheck_mp(pblock *p, pblock *c, httpd_object *obj);

/* Inserts a new alias in the database (before all other entries) */
/* ns-util.c */
NSAPI_PUBLIC void insert_alias(pblock *p, pblock *c, httpd_object *obj);

/* Scans a file and puts all of its lines into a char * array. Strips 
 * trailing whitespace */
/* ns-util.c */
NSAPI_PUBLIC char **scan_tech(char *fn);

/* Writes the lines to the given file */
/* ns-util.c */
NSAPI_PUBLIC int write_tech(char *fn, char **lines);

/* Finds an object by its ppath */
/* ns-util.c */
NSAPI_PUBLIC httpd_object *findliteralppath(char *qs, httpd_objset *os);


/* Compares two passwords, one plaintext and one encrypted. Returns strcmp()
 * like integer (0 good, anything else bad) */
/* password.c */
NSAPI_PUBLIC int pw_cmp(char *pw, char *enc);

/* Encrypts a plaintext password. */
/* password.c */
NSAPI_PUBLIC char *pw_enc(char *pw);


/* Maintain what amounts to a handle to a list of strings */
/* strlist.c */
/* Moved to libadminutil, use libadminutil/admutil.h instead
NSAPI_PUBLIC char **new_strlist(int size);
NSAPI_PUBLIC char **grow_strlist(char **strlist, int newsize);
NSAPI_PUBLIC void free_strlist(char **strlist);
*/

/* Handle INN config.data which are now called nsnews.conf files */
/* nsnews.c */
char *find_nsnews_var(char *var, char **lines);
void set_nsnews_var(char *name, char *val, char **lines);
int find_nsnews_line(char *var, char **lines);
void remove_nsnews_var(char *name, char **lines);
void replace_nsnews_prefix(char *opfx, char *npfx, char **lines);

char **scan_nsnews_admin(char *filename);
char **scan_nsnews_install(char *filename);
void nsnews_file2path_admin(char *filename, char *path);
void nsnews_file2path_install(char *filename, char *path);
void write_nsnews_admin(char *filename, char **lines);
void write_nsnews_install(char *filename, char **lines);

void run_ctlinnd(char *cmd);
char **nsnews_status(void);
void set_moderator(char *group, char *email);
char *find_moderator(char *group, char **lines);
char **scan_active(char **nscnf);
int find_active_group(char *grp, char **active);
char *active_flags(char *line);
int active_groupmatch(char *grppat, char *line);
char **scan_expirectl(char *fn);
void write_expirectl(char *fn, char **lines);
void set_expire_remember(char *days, char **lines);
void set_expire_default(char *def, char *keep, char *purge, char **lines);

#define EXPREM_STRING "/remember/:"
#define EXPREM_LEN 11
#define EXPDEF_STRING "*:A:"
#define EXPDEF_LEN 4

#define find_expire_remember(lines) (find_expire_string(EXPREM_STRING, lines))
#define find_expire_default(lines) (find_expire_string(EXPDEF_STRING, lines))
char *find_expire_string(char *find, char **lines);

typedef struct {
    char *patterns;
    char flag;
    char *keep;
    char *def;
    char *purge;
} expire_s;
int expire_entry(char *line, expire_s *ret);
expire_s *expire_entry_default(char **lines);
expire_s *find_expire_entry(char *find, char **lines);
void new_expire_entry(expire_s *ex, char **lines);
void change_expire_entry(char *find, expire_s *ex, char **lines);
void remove_expire_entry(char *find, char **lines);

typedef struct {
    char *grp;
    char *hostpats;
    char *flags;
    char *userpat;
} permission_s;
char **scan_nsaccess(char *fn);
void write_nsaccess(char *fn, char **lines);
permission_s *find_nsaccess_default(char **lines);
permission_s *find_nsaccess_entry(char *find, char **lines);
void new_nsaccess_entry(permission_s *ps, char **lines);
void change_nsaccess_entry(char *find, permission_s *ps, char **lines);
void remove_nsaccess_entry(char *find, char **lines);

/* Handle newsfeeds files */
void feed_read_file();
void feed_write_file();
char *feed_get_ind_var(int *x);
char *feed_get_host_var(char *host);
char *feed_get_newsgroups(char *feedline);
char *feed_get_param(char *feedline);
void feed_split_newsgroups(char *ngroups, char **allow, char **deny);
char *add_bangs(char *string);
void compress_whitespace(char *source);
char *feed_merge_newsgroups(char *allow_in, char *deny_in);
void feed_set_groups(char *host, char *groups);
void feed_set_entry(char *id, char *ngroups, char *feedtype, char *params);
void feed_delete_host(char *host);
void feed_dump_vars(char *feedtype, char *dest);
 
void nnhost_add(char *hostname);
void nnhost_delete(char *hostname);

void nnctl_add(char *hostname);
void nnctl_delete(char *hostname);

int nsnews_running(char **nscnf);


#ifdef MCC_PROXY

extern long inst_cache_size_tbl[];
extern long inst_cache_capacity_tbl[];
extern long cache_size_tbl[];
extern long cache_capacity_tbl[];
extern float lm_factor_tbl[];
extern long time_interval_tbl[];
extern long timeout_tbl[];
extern int percent_tbl[];

char *mb_str(long mb);
char *lm_str(float f);

void output_interval_select(char *name, char *other, long selected, long *tbl);
void output_mb_select(char *name, char *other, long selected, long *tbl);
void output_lm_select(char *name, char *other, float selected, float *tbl);
void output_percentage_select(char *name, char *other, int selected, int *tbl);

#endif /* MCC_PROXY */

#ifdef MCC_NEWS

char * get_active_news_authdb(char **nscnf);
void set_active_news_authdb(char *name, char **nscnf);
void output_active_news_authdb(char **nscnf);

#endif /* MCC_NEWS */

#if 0 /* move cron_conf to libadminutil    */

/* read and write to cron.conf, cron_conf.c */
/* Alex Feygin, 3/22/96                     */
typedef struct cron_conf_obj
{
  char *name;
  char *command;
  char *dir;
  char *user;
  char *start_time;
  char *days;
} 
cron_conf_obj;
 
typedef struct cron_conf_list
{
  char *name;
  cron_conf_obj *obj;
  struct cron_conf_list *next;
} 
cron_conf_list;
 
/* Reads cron.conf to a null terminated list of cron_conf_objects; returns
   0 if unable to do a read; 1 otherwise */
NSAPI_PUBLIC int cron_conf_read();
 
/* gets a cron object, NULL if it doesnt exist */
NSAPI_PUBLIC cron_conf_obj *cron_conf_get(char *name);
 
/* returns a NULL-terminated cron_conf_list of all the cron conf objects */
NSAPI_PUBLIC cron_conf_list *cron_conf_get_list();
 
/* Creates a cron conf object; all these args get STRDUP'd in the function
   so make sure to free up the space later if need be */
NSAPI_PUBLIC cron_conf_obj *cron_conf_create_obj(char *name, char *command,
						 char *dir,  char *user, 
						 char *start_time, char *days);
 
/* Puts a cron conf object into list or updates it if it already in there.
   Returns either the object passed or the object in there already;
   cco may be FREE'd during this operation so if you need the object
   back, call it like so:
   
   cco = cron_conf_set(cco->name, cco);  
 
   calling cron_conf_set with a NULL cco will cause the 'name' object
   to be deleted.
*/
NSAPI_PUBLIC cron_conf_obj *cron_conf_set(char *name, cron_conf_obj *cco);
 
/* write out current list of cron_conf_objects to cron.conf file */
NSAPI_PUBLIC void cron_conf_write();
 
/* free all cron conf data structures */
NSAPI_PUBLIC void cron_conf_free();


#endif /* move cron_conf to libadminutil    */


/**************************************************************************
 * This is should really be in base/file.h, but we don't want to tread on
 * toes.
 * Implement fgets without the error complaints the util_getline has.  The
 * calling function is smart enough to deal with partial lines.
 * Also include a sleep that has the same functionality as Unix for NT.
 *************************************************************************/

NSAPI_PUBLIC char *system_gets( char *, int, filebuffer * );

#ifdef XP_UNIX
NSAPI_PUBLIC int system_zero( SYS_FILE );
#else /* XP_WIN32 */
#define system_zero( f ) \
	SetFilePointer( PR_FileDesc2NativeHandle( f ), 0, NULL, FILE_BEGIN );\
	SetEndOfFile( PR_FileDesc2NativeHandle( f ) )
#define sleep( t )	Sleep( (t) * 1000 )
#endif /* XP_WIN32 */

NSAPI_PUBLIC char *cookieValue( char *, char * );

NSAPI_PUBLIC void jsPWDialogSrc( int inScript, char *otherJS );

NSAPI_PUBLIC int IsCurrentTemplateNSPlugin(char* templateName);

/************************** Miscellaneous *************************/
NSAPI_PUBLIC char * jsEscape(char *src);
NSAPI_PUBLIC int read_AbbrDescType_file(char *path, char ***namelist, char ***desclist);

NSPR_END_EXTERN_C

#endif	/* libadmin_h */
