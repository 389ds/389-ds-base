/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * authdb.c:  Functions to aid in user/group database admin
 *            
 * These things leak memory like a sieve.  
 *            
 * Ben Polk
 *    (blame Mike McCool for functions with an MLM)
 */

#ifdef XP_UNIX
#include <dirent.h>
#endif /* WIN32? */

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include "base/shexp.h"
#include "base/util.h"
#include "libadminutil/admutil.h"
#include "libadmin/libadmin.h"

#include "libaccess/nsgmgmt.h"
#include "libaccess/nsumgmt.h"
/* XXX MLM - This shouldn't have to define itself as private. */
#define __PRIVATE_NSADB
#include "libaccess/nsadb.h" 
#include "libaccess/nsamgmt.h"
#include "libaccess/aclerror.h"
#include "libaccess/aclbuild.h"
#include "libaccess/acladmin.h"
#include "usrlists.h"

#define BUF_SIZE 10

void list_authdbs(char *fullpath, char *partialpath);

static char **list;
static int listsize;
static int curentry;

/*
 * Rights we know about.  This should be moved out to some
 * external location.  Perhaps obj.conf?
 */
NSAPI_PUBLIC char *acl_read_rights[] =
{
  "GET",
  "HEAD",
  "POST",
  "INDEX",
#ifdef MCC_PROXY
  "CONNECT",
#endif
  NULL
};

NSAPI_PUBLIC char *acl_write_rights[] =
{
  "PUT",
  "DELETE",
  "MKDIR",
  "RMDIR",
  "MOVE",
  NULL
};


/*
 * passfilter - function returns non-zero if the regular expression
 *              passed in matches the string passed in.  
 */
static int passfilter(char *str, char *regexp)
{
    if (!str)
        return 0; /* NULL string never matches */
    else if (!regexp)
        return 1; /* NULL regexp matches everything */
    else
        return(!shexp_casecmp(str, regexp));  
}

NSAPI_PUBLIC char **list_auth_dbs(char *fullpath)
{
    list =  new_strlist(BUF_SIZE);
    listsize = BUF_SIZE;
    curentry = 0;
    list_authdbs(fullpath, "");

    return(list);
}

NSAPI_PUBLIC void output_authdb_selector(char *path, char *element, char *current)  {
    char **pathlist = list_auth_dbs(path);
    int currentnum = -1; 
    int plen = path ? strlen(path) : 0;
    register int x;

    /*
     * If the 'current' string begins with the 'path' string, remove the
     * 'path' prefix.
     */
    if (!strncmp(current, path, plen)) {
	current += plen;
	if (*current == FILE_PATHSEP) ++current;
    }

    if (pathlist[0]) /* Is there at least one database? */
    {
        /* Find current selection in list of databases. */
        for(x=0; pathlist[x]; x++)  {
            if(!strcmp(current, pathlist[x])) {
                currentnum = x;
	        continue;
	    }
        }

/* BONEHEAD */
        fprintf(stdout, "<SELECT name=\"%s\" %s>", 
                        element, (x>SELECT_OVERFLOW) ? "size=5" : "");

        /* If the current selection is in there, put it first. */
        if(currentnum != -1)  {
            fprintf(stdout, "<OPTION value=\"%s\" SELECTED>%s\n", 
                            pathlist[currentnum], pathlist[currentnum]);
    
        }
        for(x=0; pathlist[x]; x++)  {
            if (x == currentnum) 
                continue;

            fprintf(stdout, "<OPTION value=\"%s\">%s\n", 
                            pathlist[x], pathlist[x]);
        }
        fprintf(stdout, "</SELECT>");
    }  else  {
        fprintf(stdout, "<b>No databases found.</b>");
    }
}

NSAPI_PUBLIC char *get_current_authdb()
{
    char **config = get_adm_config();
    return(STRDUP(config[3]));
}

NSAPI_PUBLIC void set_current_authdb(char *current)  
{
    char **config = get_adm_config();
    config[3] = STRDUP(current);
    write_adm_config(config);
}

void list_authdbs(char *fullpath, char *partialpath)
{
    int stat_good;
    struct stat finfo;
    char **dirlisting;
    char *path = (char *)MALLOC(strlen(fullpath)+strlen(partialpath)+2);
 
    sprintf(path, "%s%c%s", fullpath, FILE_PATHSEP, partialpath);
    if( !(dirlisting = list_directory(path,0)))
        return;       
    else  {
        register int x;
        char *entry, *newppath;
 
        for(x=0; dirlisting[x]; x++)  {
            entry = (char *)MALLOC(strlen(path)+strlen(dirlisting[x])+2);
            sprintf(entry, "%s%s", path, dirlisting[x]);

#ifdef XP_UNIX
            stat_good = (lstat(entry, &finfo) == -1 ? 0 : 1);
#else /* WIN32 */
            stat_good = (stat(entry, &finfo) == -1 ? 0 : 1);
#endif /* XP_UNIX */ 
 
            if(!stat_good)
                continue;
            newppath = (char *)MALLOC(strlen(partialpath)+strlen(dirlisting[x])+3);
 
            if(S_ISDIR(finfo.st_mode))  {
                sprintf(newppath, "%s%s", partialpath, dirlisting[x]);
                curentry++;

                if(!(curentry < listsize))  {
                    listsize += BUF_SIZE;
                    list = grow_strlist(list, listsize);
                }
                list[curentry-1] = STRDUP(newppath);
                list[curentry] = NULL;
            }
 
            FREE(entry);
            FREE(newppath);
        }
    }
} 

/* Get the userdb directory. (V1.x) */
NSAPI_PUBLIC char *get_userdb_dir(void)
{
    char *userdb;
    char line[BIG_LINE];

#ifdef USE_ADMSERV
    char *tmp = getenv("NETSITE_ROOT");
    
    sprintf(line, "%s%cuserdb", tmp, FILE_PATHSEP);
#else
    char *tmp = get_mag_var("#ServerRoot");
    
    sprintf(line, "%s%cadmin%cuserdb", tmp, FILE_PATHSEP, FILE_PATHSEP);
#endif
    userdb = STRDUP(line);
    return userdb;
}

/* Get the httpacl directory. (V2.x) */
NSAPI_PUBLIC char *get_httpacl_dir(void)
{
    char *httpacl;
    char line[BIG_LINE];

#ifdef USE_ADMSERV
    char *tmp = getenv("NETSITE_ROOT");
    
    sprintf(line, "%s%chttpacl", tmp, FILE_PATHSEP);
#else
    char *tmp = get_mag_var("#ServerRoot");
    
    sprintf(line, "%s%cadmin%chttpacl", tmp, FILE_PATHSEP, FILE_PATHSEP);
#endif
    httpacl = STRDUP(line);
    return httpacl;
}

/* Get the authdb directory. (V2.x) */
NSAPI_PUBLIC char *get_authdb_dir(void)
{
    char *authdb;
    char line[BIG_LINE];

#ifdef USE_ADMSERV
    char *tmp = getenv("NETSITE_ROOT");
    
    sprintf(line, "%s%cauthdb", tmp, FILE_PATHSEP);
#else
    char *tmp = get_mag_var("#ServerRoot");
    
    sprintf(line, "%s%cadmin%cauthdb", tmp, FILE_PATHSEP, FILE_PATHSEP);
#endif
    authdb = STRDUP(line);
    return authdb;
}
/*
 * groupOrUser - function sets its return variable flags
 *               based on whether the name passed in is
 *               a user or a group in the specified database.
 *               It could be both, although the entry form
 *               are intended to prohibit this.
 * Returns: 0 if no error occurs, something else otherwise.
 */
NSAPI_PUBLIC int groupOrUser(char *db_path, char *name, int *is_user, int *is_group)
{
    int        rv = 1;
    UserObj_t  *uoptr;
    GroupObj_t *goptr;
    void       *padb;  

    if (name && is_user && is_group) {
        *is_user  = 0; 
        *is_group = 0; 
        rv = nsadbOpen(NULL, db_path, 0, &padb);
        if (!rv) {

	    rv = nsadbFindByName(NULL, padb, name, AIF_USER, (void **)&uoptr);
            if (rv == AIF_USER) {
                *is_user = 1;
	    }

	    rv = nsadbFindByName(NULL, padb, name, AIF_GROUP, (void **)&goptr);
	    if (rv == AIF_GROUP) {
		*is_group = 1;
	    }

            nsadbClose(padb, 0);
        }
    }
 
    return rv;
}

/*
 * getfullname -  function to get the fullname of a user.
 *     Return: Returns 0 if it works, something else if it fails.
 */
NSAPI_PUBLIC int getfullname(char *db_path, char *user, char **fullname) {
    int        rv;
    UserObj_t *uoptr;
    void      *padb;  

    if (db_path && user)
    {
        rv = nsadbOpen(NULL, db_path, 0, &padb);
        if (rv == 0) {
	    rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
	    if (rv == AIF_USER) {
		*fullname = (uoptr->uo_rname != 0) ? STRDUP((char *)uoptr->uo_rname)
						   : STRDUP("");
	    }
            else
                rv = 1;
            nsadbClose(padb, 0);
        }
    } 
    else
        rv = 1;

    return rv;
}

/*
 * setfullname -  function to set the fullname for the specified user. 
 *     Return: Returns 0 if it works, something else if it fails.
 */
NSAPI_PUBLIC int setfullname(char *db_path, char *user, char *fullname) {
    int        rv;
    UserObj_t *uoptr;
    void      *padb;  

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, "Failed To Open Database", 
                     "An error occurred while trying to update "
                     "the user's fullname in the database.");
    } else {
        /* See if the user already exists, if so, update it. */
	rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
        if (rv == AIF_USER) {
            uoptr->uo_rname = (NTS_t)fullname;
        } else {
            /* User doesn't exist, so we've failed. */
            report_error(SYSTEM_ERROR, user, 
                         "Unable to change this user's fullname, "
                         "user was not found in the database.");
            rv = 1;
        } 

        if (uoptr) {
	    rv = nsadbModifyUser(NULL, padb, uoptr);
            if (rv < 0) 
                report_error(SYSTEM_ERROR, user, 
                             "A database error occurred while "
                             "trying to change the user fullname.");
        }
        nsadbClose(padb, 0);
    }
    return rv;
}

/* Set a user's login name   MLM*/
NSAPI_PUBLIC int setusername(char *db_path, char *user, char *newname) {
    int        rv;
    UserObj_t *uoptr;
    void      *padb;
 
    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, "Failed To Open Database",
                     "An error occurred while trying to update "
                     "the user's fullname in the database.");
    } else {
        /* See if the user already exists, if so, update it. */
        rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
        if (rv != AIF_USER) {
            /* User doesn't exist, so we've failed. */
            report_error(SYSTEM_ERROR, user,
                         "Unable to change this user's fullname, "
                         "user was not found in the database.");
            rv = 1;
        }
        if (uoptr) {
            rv = userRename(NULL, ((AuthDB_t *)padb)->adb_userdb, 
                            uoptr, (NTS_t)newname);

            if (rv < 0)
                report_error(SYSTEM_ERROR, user,
                             "A database error occurred while "
                             "trying to change the login name.");
        }
        nsadbClose(padb, 0);
    }
    return rv;
}

/*
 * addusertogroup -  function to add a user to a group
 *     Return: Returns 0 if it works, something else if it fails.
 */
NSAPI_PUBLIC int addusertogroup(char *db_path, char *user, char *group) {
    int         rv;
    UserObj_t  *uoptr;
    GroupObj_t *goptr; 
    void       *padb;  

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, "Failed To Open Database", 
                     "An error occurred while trying to add "
                     "user to a group.");
    } else {
        /* See if the user and group exist. */
	rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
	rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
        if       (goptr == 0) { 
            report_error(INCORRECT_USAGE, group, 
                         "The group was not found.");
        }
        else if (uoptr == 0) {
            report_error(INCORRECT_USAGE, user, 
                         "The user was not found.");
        }
        else {
            rv = nsadbAddUserToGroup(NULL, padb, goptr, uoptr);
        }

        nsadbClose(padb, 0);
    }
    return rv;
}

/*
 * addgrouptogroup -  function to add a group to a group
 *     Return: Returns 0 if it works, something else if it fails.
 */
NSAPI_PUBLIC int addgrouptogroup(char *db_path, char *memgroup, char *group) {
    int         rv;
    GroupObj_t *goptr; 
    GroupObj_t *mem_goptr; 
    void       *padb;  

    if (!strcmp(memgroup, group)) {
        report_error(INCORRECT_USAGE, group, 
                     "You can't add a group to itself.");
    }

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, "Failed To Open Database", 
                     "An error occurred while trying to add "
                     "group to a group.");
    } else {
        /* See if the groups exist. */
	rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
	rv = nsadbFindByName(NULL,
			     padb, memgroup, AIF_GROUP, (void **)&mem_goptr);
        if       (goptr == 0) { 
            report_error(INCORRECT_USAGE, group, 
                         "The target group was not found.");
        }
        else if (mem_goptr == 0) {
            report_error(INCORRECT_USAGE, memgroup, 
                         "The group to add was not found.");
        }
        else {
            rv = nsadbAddGroupToGroup(NULL, padb, goptr, mem_goptr);
        }

        nsadbClose(padb, 0);
    }
    return rv;
}

/*
 * remuserfromgroup -  function to remove a user from a group
 *     Return: Returns 0 if it works, something else if it fails.
 */
NSAPI_PUBLIC int remuserfromgroup(char *db_path, char *user, char *group) {
    int         rv;
    UserObj_t  *uoptr;
    GroupObj_t *goptr;
    void       *padb;

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, "Failed To Open Database",
                     "An error occurred while trying to add "
                     "user to a group.");
    } else {
        /* See if the user and group exist. */
	rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
	rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
        if       (goptr == 0) {
            report_error(SYSTEM_ERROR, group,
                         "The group was not found.");
        }
        else if (uoptr == 0) {
            report_error(SYSTEM_ERROR, user,
                         "The user was not found.");
        }
        else {
            rv = nsadbRemUserFromGroup(NULL, padb, goptr, uoptr);
            if (rv)
                report_error(SYSTEM_ERROR, "Error taking out user",
                             "An error occured trying to take "
                             "the user out of the group.");
        }

        nsadbClose(padb, 0);
    }
    return rv;
} 

/*
 * remgroupfromgroup -  function to remove a group to a group
 *     Return: Returns 0 if it works, something else if it fails.
 */
NSAPI_PUBLIC int remgroupfromgroup(char *db_path, char *memgroup, char *group) {
    int         rv;
    GroupObj_t *goptr; 
    GroupObj_t *mem_goptr; 
    void       *padb;  

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, "Failed To Open Database", 
                     "An error occurred while trying to remove "
                     "a group from a group.");
    } else {
        /* See if the groups exist. */
	rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
	rv = nsadbFindByName(NULL,
			     padb, memgroup, AIF_GROUP, (void **)&mem_goptr);
        if       (goptr == 0) { 
            report_error(SYSTEM_ERROR, group, 
                         "The target group was not found.");
        }
        else if (mem_goptr == 0) {
            report_error(SYSTEM_ERROR, memgroup, 
                         "The group to remove was not found.");
        }
        else {
            rv = nsadbRemGroupFromGroup(NULL, padb, goptr, mem_goptr);
        }

        nsadbClose(padb, 0);
    }
    return rv;
}

/*
 * setpw -  function to set the password for the specified user. 
 *     Return: Returns 0 if it works, something else if it fails.
 */
NSAPI_PUBLIC int setpw(char *db_path, char *user, char *pwd) {
    int        rv;
    UserObj_t *uoptr = 0;
    void      *padb;  

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, "Failed To Open Database", 
                     "An error occurred while trying to add "
                     "the password to the database.");
    } else {
        /* See if the user already exists, if so, update it. */
	rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
        if (uoptr != 0) {
            uoptr->uo_pwd = (NTS_t)STRDUP(pw_enc(pwd));
        } else {
            /* User doesn't exist, so we've failed. */
            report_error(SYSTEM_ERROR, user, 
                         "Unable to change this user's password, "
                         "user was not found in the database.");
            rv = 1;
        } 

        if (uoptr) {
	    rv = nsadbModifyUser(NULL, padb, uoptr);
            if (rv < 0) 
                report_error(SYSTEM_ERROR, user, 
                             "A database error occurred while "
                             "trying to change the user password.");
        }
        nsadbClose(padb, 0);
    }
    return rv;
}

/*
 * setdbpw -  function to set the password on the special user
 *             who's password is used as the database password.
 *             If the password passed in is NULL, the user is 
 *             removed or not created.
 *             If the password is not NULL, then the user will
 *             be created if needed, and it's password set to
 *             the one passed in.
 *
 *     Return: Returns 0 if it works, something else if it fails.
 */
NSAPI_PUBLIC int setdbpw(char *db_path, char *pwd) 
{
    int        rv;
    UserObj_t *uoptr = 0;
    void      *padb;  

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, "Failed To Open Database", 
                     "An error occurred while trying to add "
                     "the password to the database.");
    } 
    /* 
     * If NULL pwd, remove the user if it exists.
     */
    else if (pwd == NULL) {
	rv = nsadbRemoveUser(NULL, padb, DBPW_USER);
        nsadbClose(padb, 0);
        
        /*
	 * If we get success(0) or a no such user error(NSAERRNAME)
	 * we're happy.
	 */
        if (rv != 0 && rv != NSAERRNAME) {
            report_error(SYSTEM_ERROR, "Remove Password Failed", 
                         "An error occurred while trying to remove "
                         "the password for the database.");
        }
    } else {
        /* See if the user already exists, if so, just update it. */
	rv = nsadbFindByName(NULL, padb, DBPW_USER, AIF_USER, (void **)&uoptr);
        if (uoptr == 0) {
            /* User doesn't exist, so add it. */
            uoptr = userCreate((NTS_t)DBPW_USER, (NTS_t)pw_enc(pwd), (NTS_t)DBPW_USER);
            if (uoptr == 0) {
                report_error(SYSTEM_ERROR, "Failed To Update Database", 
                             "An error occurred while trying to add "
                             "the password to the database.");
                rv = 1;
            } 
	    else {
		rv = nsadbCreateUser(NULL, padb, uoptr);
	    }
        } else {
            uoptr->uo_pwd = (NTS_t)STRDUP(pw_enc(pwd));
	    rv = nsadbModifyUser(NULL, padb, uoptr);
        } 

        nsadbClose(padb, 0);

        if (uoptr) {
            if (rv < 0) {
                report_error(SYSTEM_ERROR, "Failed To Set Database Password", 
                             "An error occurred while trying to save "
                             "the password in the database.");
		rv = 1;
	    }
	    userFree(uoptr);
	}
    }
    return rv;
}

/*
 * checkdbpw - Return TRUE if the password is correct, or database
 *             doesn't have one, because the password user isn't there.
 *             Return FALSE if required password is not correct.
 */
NSAPI_PUBLIC int checkdbpw(char *db_path, char *pwd) 
{
    int        rv;
    UserObj_t *uoptr = 0;
    void      *padb;  
    int        fpwOK = 0;

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv == 0) {
	rv = nsadbFindByName(NULL, padb, DBPW_USER, AIF_USER, (void **)&uoptr);
        if (uoptr == 0) {
            fpwOK = 1; /* Password userid isn't there, so none required. */
        } else {
            if (pwd == NULL) 
                fpwOK = 0; /* PW user exists, no pw passed in, return false. */
            else {
                if (pw_cmp(pwd, (char *)uoptr->uo_pwd))
                    fpwOK = 0; /* passwords are different, so return false. */
                else
                    fpwOK = 1; /* passwords are the same, so return true. */
            }
	    userFree(uoptr);
        }
        nsadbClose(padb, 0);
    }
    return fpwOK;
}

/* 
 * Create a link to another CGI:
 *     val - value text on the link
 *     targ - name of the CGI to start
 *     arg - argument to pass to the CGI
 */
void output_cgi_link(char *val, char *trg, char *arg)
{
    char        line[BIG_LINE]; 
    sprintf(line, "%s?%s", trg, arg);
    printf("<a href=index?options+acss+%s target='options'>%s</a>", 
           util_uri_escape(NULL, line), val); 
}

/*
 * groupEnumCB - callback function from libaccess group enumerator
 */
static int groupEnumCB (NSErr_t * errp,
			void * padb, void *parg, GroupObj_t *goptr)
{
    if (goptr && goptr->go_name && strlen((char *)goptr->go_name)) 
        ulsAddToList(parg, goptr->go_gid, (char *)goptr->go_name);

    return 0; /* 0: continue enumeration */
}

/*
 * userEnumCB - callback function from libaccess group enumerator
 */
static int userEnumCB (NSErr_t * errp,
		       void * padb, void *parg, UserObj_t *uoptr)
{
    if (uoptr && uoptr->uo_name && strlen((char *)uoptr->uo_name)) 
        ulsAddToList(parg, uoptr->uo_uid, (char *)uoptr->uo_name);

    return 0; /* 0: continue enumeration */
}

/*
 * idfound - horribly inefficient scan through the idlist table
 *           returning true if the specified id is found, false
 *           otherwise.
 */
int idfound(int id, int *idlist, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        if (id == idlist[i])
            return 1;
    }
    return 0;
}

void output_groups_user_is_in(char *db_path, char *user)
{
    int         rv;
    UserObj_t  *uoptr = 0;
    GroupObj_t *goptr = 0; 
    void       *padb;  
    USI_t      *gidlist;
    int         i;
    int         id;
    char       *group;
    char       *gname;
    int         groupCount;
    char        line[BIG_LINE]; 

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, db_path, 
                     "Failed to open database while trying "
                     "to list group membership.");
    } else {
        /* See if the user exists. */
	rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
        if (uoptr == 0) {
            /* User doesn't exist, so we've failed. */
            report_error(SYSTEM_ERROR, user, 
                         "Unable to find user when trying to "
                         "list group membership.");
            rv = 1;
        } else {
            groupCount = UILCOUNT(&uoptr->uo_groups);
            if (groupCount > 0) {
                void *DirectlyInList;
                void *IndirectlyInList;
                ulsAlloc(&DirectlyInList);
                ulsAlloc(&IndirectlyInList);

                gidlist = UILLIST(&uoptr->uo_groups);
                for (i = 0; i < groupCount; ++i) {
		    rv = nsadbIdToName(NULL,
				       padb, gidlist[i], AIF_GROUP, &gname);
		    if (rv >= 0) {
			rv = nsadbFindByName(NULL, padb, gname, AIF_GROUP,
					     (void **)&goptr);
		    }
                    if (goptr != 0) {
			if (goptr->go_name && strlen((char *)goptr->go_name)) {
			    if (idfound(uoptr->uo_uid,
					(int*)UILLIST(&goptr->go_users),
					UILCOUNT(&goptr->go_users))) {
				ulsAddToList(DirectlyInList, goptr->go_gid,
					     (char *)goptr->go_name);
			    }
			    else {
				ulsAddToList(IndirectlyInList, goptr->go_gid,
					     (char *)goptr->go_name);
			    }
			}
			groupFree(goptr);
			goptr = 0;
                    }
                }
                ulsSortName(DirectlyInList);
                ulsGetCount(DirectlyInList, &groupCount);
                for (i=0; i<groupCount; ++i) {
                    group = NULL;
                    ulsGetEntry(DirectlyInList, i, &id, &group);
                    if (group) {
                        printf("<tr><td>");
                        printf("Member of <b>%s</b></td><td>", group);
                        sprintf(line, "group=%s", group);
                        output_cgi_link("Edit Group", "grped", line);
                        printf("</td><td>");
                        sprintf(line, "remfromgrp_but=1&memuser=%s&group=%s", 
                                      user, group);
                        output_cgi_link("Remove from Group", "grped", line);
                        printf("</td>\n");
                    }
                }
                ulsSortName(IndirectlyInList);
                ulsGetCount(IndirectlyInList, &groupCount);
                for (i=0; i<groupCount; ++i) {
                    group = NULL;
                    ulsGetEntry(IndirectlyInList, i, &id, &group);
                    if (group) {
                        printf("<tr><td>");
                        printf("Indirect member of <b>%s</b></td><td>", group);
                        sprintf(line, "group=%s", group);
                        output_cgi_link("Edit Group", "grped", group);
                        printf("</td><td>");
                        sprintf(line, "addtogrp_but=1&memuser=%s&group=%s", 
                                      user, group);
                        output_cgi_link("Add to Group", "grped", line);
                        printf("</td>\n");
                    }
                }
                ulsFree(&DirectlyInList);
                ulsFree(&IndirectlyInList);
            }
        }
    }
    return;
}

/*
 * output a table with the groups the user isn't a member of
 */ 
void output_nonmembership(char *db_path, char *user)
{
    int         rv;
    UserObj_t  *uoptr = 0;
    void       *padb;  
    USI_t      *gidlist;
    int         i;
    int         id;
    char       *group;
    int         groupCount;
    char        line[BIG_LINE]; 

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, db_path, 
                     "Failed to open database while trying "
                     "to list group membership.");
    } else {
	rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
        if (uoptr == 0) {
            report_error(SYSTEM_ERROR, user, 
                         "Unable to find user when trying to "
                         "list group membership.");
            rv = 1;
        } else {
            void *sortList;

            ulsAlloc(&sortList);
	    rv = nsadbEnumerateGroups(NULL, padb,
				      (void *)sortList, groupEnumCB);

            ulsSortName(sortList);
            ulsGetCount(sortList, &groupCount);

            if (groupCount > 0) {
                gidlist = UILLIST(&uoptr->uo_groups);
                for (i=0; i<groupCount; ++i) {
                    group = NULL;
                    ulsGetEntry(sortList, i, &id, &group);
                    if (group && !idfound(id, (int*)gidlist, UILCOUNT(&uoptr->uo_groups))){
                        printf("<tr><td>");
                        printf("Not a member of <b>%s</b></td><td>", group);
                        sprintf(line, "group=%s", group);
                        output_cgi_link("Edit Group", "grped", line);
                        printf("</td><td>");
                        sprintf(line, "addtogrp_but=1&memuser=%s&group=%s", 
                                      user, group);
                        output_cgi_link("Add to Group", "grped", line);
                        printf("</td>\n");
                    }
                }
            }
            ulsFree(&sortList);
	    userFree(uoptr);
        }
    }
    return;
}

/*
 * output_group_membership - output a table showing which
 *                           groups a user is in.
 */
void output_group_membership(char *db_path, char *user)
{
    printf("<table border=1><caption align=left>\n");
    printf("<b>%s group membership:</b>", user);
    printf("</caption>\n");
    output_groups_user_is_in(db_path, user);
    output_nonmembership(db_path, user);
    printf("</table>\n");
}

void output_grpgroup_membership(char *db_path, char *group, char *filter)
{
    int         rv;
    GroupObj_t *goptr = 0; 
    void       *padb;  
    USI_t      *gidlist;
    int         i;
    int         id;
    char       *gname;
    char       *memgroup;
    int         groupCount;
    char        line[BIG_LINE]; 

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, db_path, 
                     "Failed to open database while trying "
                     "to list group membership.");
    } else {
        /* See if the group exists. */
	rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
        if (goptr == 0) {
            /* Group doesn't exist, so we've failed. */
            report_error(SYSTEM_ERROR, group, 
                         "Unable to find group when trying to "
                         "list group membership.");
            rv = 1;
        } else {
            groupCount = UILCOUNT(&goptr->go_groups);
            if (groupCount > 0) {
                void *sortList;
                ulsAlloc(&sortList);

                printf("<table border=1><caption align=left>\n");
                printf("<b>%s has these group members:</b>", group);
                printf("</caption>\n");
                gidlist = UILLIST(&goptr->go_groups);
                for (i = 0; i < groupCount; ++i) {
		    rv = nsadbIdToName(NULL,
				       padb, gidlist[i], AIF_GROUP, &gname);
                    if ((rv >= 0) && (gname != 0) && (strlen(gname) != 0)) {
                        ulsAddToList(sortList, gidlist[i], gname);
                    }
                }
                ulsSortName(sortList);
                ulsGetCount(sortList, &groupCount);
                for (i=0; i<groupCount; ++i) {
                    memgroup = NULL;
                    ulsGetEntry(sortList, i, &id, &memgroup);
                    if (memgroup && passfilter(memgroup, filter)) {
                        printf("<tr><td>");
                        printf("<b>%s</b></td><td>", memgroup);
                        sprintf(line, "group=%s", memgroup);
                        output_cgi_link("Edit Group", "grped", line);
                        printf("</td><td>");
                        sprintf(line, "remfromgrp_but=1&memgroup=%s&group=%s", 
                                      memgroup, group);
                        output_cgi_link("Remove from Group", "grped", line);
                        printf("</td>\n");
                    }
                }
                printf("</table>\n");
                ulsFree(&sortList);
            } else {
                printf("<b>This group has no group members.</b>");
            }
	    groupFree(goptr);
        }
	nsadbClose(padb, 0);
    }
    return;
}

/*
 * Output a table showing the user members of a group.
 */
NSAPI_PUBLIC void output_user_membership(char *db_path, char *group, char *filter)
{
    int         rv;
    GroupObj_t *goptr = 0; 
    void       *padb;  
    USI_t      *uidlist;
    char       *user;
    int         i;
    int         id;
    char       *memuser;
    int         userCount;
    char        line[BIG_LINE]; 

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, db_path, 
                     "Failed to open database while trying "
                     "to list user membership.");
    } else {
        /* See if the group exists. */
	rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
        if (goptr == 0) {
            /* Group doesn't exist, so we've failed. */
	    nsadbClose(padb, 0);
            report_error(SYSTEM_ERROR, group, 
                         "Unable to find group when trying to "
                         "list user membership.");
            rv = 1;
        } else {
            userCount = UILCOUNT(&goptr->go_users);
            if (userCount > 0) {
                void *sortList;
                ulsAlloc(&sortList);

                printf("<table border=1><caption align=left>\n");
                printf("<b>%s has these user members:</b>", group);
                printf("</caption>\n");
                uidlist = UILLIST(&goptr->go_users);
                for (i = 0; i < userCount; ++i) {
		    rv = nsadbIdToName(NULL,
				       padb, uidlist[i], AIF_USER, &user);
		    if ((rv >= 0) && (user != 0) && (strlen(user) != 0)) {
                        ulsAddToList(sortList, uidlist[i], user);
                    }
                }
		nsadbClose(padb, 0);
                ulsSortName(sortList);
                ulsGetCount(sortList, &userCount);
                for (i=0; i<userCount; ++i) {
                    memuser = NULL;
                    ulsGetEntry(sortList, i, &id, &memuser);
                    if (memuser && passfilter(memuser, filter)) {
                        printf("<tr><td>");
                        printf("<b>%s</b></td><td>", memuser);
                        sprintf(line, "user=%s", memuser);
                        output_cgi_link("Edit User", "usred", line);
                        printf("</td><td>");
                        sprintf(line, "remfromgrp_but=1&memuser=%s&group=%s", 
                                      memuser, group);
                        output_cgi_link("Remove from Group", "grped", line);
                        printf("</td>\n");
                    }
                }
                printf("</table>\n");
                ulsFree(&sortList);
            } else {
		nsadbClose(padb, 0);
                printf("<b>This group has no user members.</b>");
            }
        }
    }
    return;
}

/* 
 * Output a group showing all users.
 */
NSAPI_PUBLIC int output_users_list(char *db_path, char *filter)
{
    int         rv;
    void       *padb;  
    int         i;
    int         id;
    char       *user;
    int         userCount;
    char        line[BIG_LINE]; 

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) 
        report_error(SYSTEM_ERROR, db_path, 
                     "Failed to open database while trying "
                     "to list users.");
    else {
        void *sortList;

        ulsAlloc(&sortList);
	rv = nsadbEnumerateUsers(NULL, padb, (void *)sortList, userEnumCB);
	nsadbClose(padb, 0);
        ulsSortName(sortList);
        ulsGetCount(sortList, &userCount);

        if (userCount > 0) {

            printf("<table border=1><caption align=left>\n");
            printf("<b>User List:</b>");
            printf("</caption>\n");

            for (i=0; i<userCount; ++i) {
                user = NULL;
                ulsGetEntry(sortList, i, &id, &user);
                if (user && passfilter(user, filter)) {
                    printf("<tr><td>");
                    printf("<b>%s</b></td><td>", user);
                    sprintf(line, "user=%s", user);
                    output_cgi_link("Edit User", "usred", line);
                    printf("</td><td>\n");
                    output_cgi_link("Remove User", "usrrem", line);
                    printf("</td>\n");
                }
            }
            printf("</table>\n"); 
        } else {
            printf("<b>There are no users in the database.</b>");
        } 
        ulsFree(&sortList);
    }
    return rv;
}

/*
 * Output a table showing all groups.
 */
NSAPI_PUBLIC int output_groups_list(char *db_path, char *filter)
{
    int         rv;
    void       *padb;  
    int         i;
    int         id;
    char       *group;
    int         groupCount;
    char        line[BIG_LINE]; 

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) 
        report_error(SYSTEM_ERROR, db_path, 
                     "Failed to open database while trying "
                     "to list groups.");
    else {
        void *sortList;

        ulsAlloc(&sortList);
	rv = nsadbEnumerateGroups(NULL, padb, (void *)sortList, groupEnumCB);
	nsadbClose(padb, 0);
        ulsSortName(sortList);
        ulsGetCount(sortList, &groupCount);

        if (groupCount > 0) {

            printf("<table border=1><caption align=left>\n");
            printf("<b>Group List:</b>");
            printf("</caption>\n");

            for (i=0; i<groupCount; ++i) {
                group = NULL;
                ulsGetEntry(sortList, i, &id, &group);
                if ((group) && (passfilter(group, filter))) {
                    printf("<tr><td>");
                    printf("<b>%s</b></td><td>", group);
                    sprintf(line, "group=%s", group);
                    output_cgi_link("Edit Group", "grped", line);
                    printf("</td><td>");
                    output_cgi_link("Remove Group", "grprem", line);
                    printf("</td>\n");
                }
            }
            printf("</table>\n"); 
        } else {
            printf("<b>There are no groups in the database.</b>");
        } 
        ulsFree(&sortList);
    }
    return rv;
}

/* Helper function: Return a uls list of all the groups a user is in.  MLM */
void *_list_user_groups(void *padb, char *user, int group_users)
{
    int         rv;
    register int i;
    UserObj_t  *uoptr         = 0;
    GroupObj_t *ugoptr        = 0; 
    GroupObj_t *goptr         = 0; 
    void       *userGroupList = NULL;
    int         userGroupCount= 0;
    USI_t      *gidlist;
    char       *ugname        = NULL;

    if(!group_users)  {
        rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
    }  else  {
        rv = nsadbFindByName(NULL, padb, user, AIF_GROUP, (void **)&ugoptr);
    }
    if ((uoptr == 0) && (ugoptr == 0)) {
        /* User doesn't exist, so we've failed. */
        return NULL;
    } else {
        if(uoptr)  {
            userGroupCount = UILCOUNT(&uoptr->uo_groups);
        }  else  {
            userGroupCount = UILCOUNT(&ugoptr->go_groups);
        }
        if (userGroupCount > 0) {
            ulsAlloc(&userGroupList);
            if(uoptr)  {
                gidlist = UILLIST(&uoptr->uo_groups);
            }  else  {
                gidlist = UILLIST(&ugoptr->go_groups);
            }
 
            for (i = 0; i < userGroupCount; ++i) {
                rv = nsadbIdToName(NULL, padb,
                                   gidlist[i], AIF_GROUP, &ugname);
 
                if (rv >= 0) {
                    rv = nsadbFindByName(NULL, padb, ugname, AIF_GROUP,
                                         (void **)&goptr);
                }
                if (goptr != 0) {
                    if (goptr->go_name && strlen((char *)goptr->go_name)) {
                        if(uoptr)  {
                            if (idfound(uoptr->uo_uid,
                                        (int*)UILLIST(&goptr->go_users),
                                        UILCOUNT(&goptr->go_users))) {
                                ulsAddToList(userGroupList, goptr->go_gid,
                                             (char *)goptr->go_name);
                            }
                        }  else  {
                            ulsAddToList(userGroupList, goptr->go_gid,
                                         (char *)goptr->go_name);
                        }
                    }
                    groupFree(goptr);
                    goptr = 0;
                }
            }
        }
    }
    return userGroupList;
}

/* Output a selector box, with name "name", and make it a multiple
 * selector box if multiple=1. */
/* If user is non-null, then if it's a multiple selector, correctly highlight
 * the groups the user is in.   
 * If group_user is 1, then the variable "user" refers to a *group* as 
 * a member, rather than a user.  
 * Highlight the item "highlight" regardless of membership (as long as 
 * it's non-NULL.)  MLM */
NSAPI_PUBLIC void output_group_selector(char *db_path, int group_user, 
                                        char *user,
                                        char *highlight, char *except,
                                        char *name, int none, int multiple)
{
    int         rv;
    void       *padb;
    int         i, j, isselected;
    int         id;
    char       *group;
    int         groupCount;
    void       *userGroupList = NULL;
    int         userGroupCount= 0;
    char       *ugname        = NULL;
 
    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0)
        report_error(SYSTEM_ERROR, db_path,
                     "Failed to open database while trying "
                     "to list groups.");
    else {
        void *sortList;
 
        ulsAlloc(&sortList);
        rv = nsadbEnumerateGroups(NULL, padb, (void *)sortList, groupEnumCB);

        if((multiple) && (user))  {
            userGroupList=_list_user_groups(padb, user, group_user);
            if(userGroupList)  {
                ulsSortName(userGroupList);
                ulsGetCount(userGroupList, &userGroupCount);
            }
        }
        nsadbClose(padb, 0);
        ulsSortName(sortList);
        ulsGetCount(sortList, &groupCount);
 
        if (groupCount > 0) {
            /* Make a pulldown if we can.  If the size is bigger than the
             * overflow value, make it a box to hack around the fact that
             * the X Navigator can't scroll pulldown lists. */
            if((multiple) || (groupCount > SELECT_OVERFLOW))  {
                printf("<SELECT size=5 name=%s %s>", 
                       name, multiple? "MULTIPLE" : "");
            }  else  {
                printf("<SELECT name=%s>", name);
            }
            if((!multiple) && (none))  {
                printf("<OPTION value=NONE>NONE\n");
            }
 
            for (i=0; i<groupCount; ++i) {
                group  = NULL;
                ugname = NULL;
                isselected=0;
                ulsGetEntry(sortList, i, &id, &group);
                if (group) {
                    if((except) && (!strcmp(group, except)))
                        continue;
                    if((highlight) && (!strcmp(group, highlight)))  
                        isselected=1;
                    if(userGroupList && !isselected)  {
                        for(j=0; j < userGroupCount; j++)  {
                            ulsGetEntry(userGroupList, j, &id, &ugname);
#if 0
                            if(ugname[0] > group[0])  {
                                /* Both lists are sorted, therefore, if we've
                                 * hit a letter that's after the group letter,
                                 * it must not be here. */
                                /* What can I say, it's a pathetic attempt
                                 * on my part to mask the fact that I know
                                 * this is inefficient. */
                                break;
                            }
#endif
                            if(!strcmp(ugname, group))  {
                                isselected=1;
                                break;
                            }
                        }
                    }
                    printf("<OPTION %s>%s\n", 
                           isselected? "SELECTED" : "", group);
                }
            }
            printf("</SELECT>");
        } else {
            printf("<b>(No groups have been created.)</b>");
        }
        ulsFree(&sortList);
        if(userGroupList)
            ulsFree(&userGroupList);
    }
}

void *_list_group_users(void *padb, char *group, int group_users)
{
    int         rv;
    GroupObj_t *goptr = 0;
    USI_t      *uidlist;
    char       *user;
    int         i;
    int         userCount;
    void       *sortList=NULL;
 
    /* See if the group exists. */
    rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
    if (goptr == 0) {
        /* Group doesn't exist, so we've failed. */
        return NULL;
    } else {
        if(group_users)  
            userCount = UILCOUNT(&goptr->go_groups);
        else
            userCount = UILCOUNT(&goptr->go_users);

        if (userCount > 0) {
            ulsAlloc(&sortList);

            if(group_users)  
                uidlist = UILLIST(&goptr->go_groups);
            else
                uidlist = UILLIST(&goptr->go_users);

            for (i = 0; i < userCount; ++i) {
                if(group_users)
                    rv = nsadbIdToName(NULL,padb, uidlist[i], AIF_GROUP, &user);
                else
                    rv = nsadbIdToName(NULL, padb, uidlist[i], AIF_USER, &user);
                if ((rv >= 0) && (user != 0) && (strlen(user) != 0)) {
                    ulsAddToList(sortList, uidlist[i], user);
                }
            }
        }
    }
    return sortList;
}

/*
 * Output a selector box of users.  If group is non-null, highlight the
 *  users in that group.  MLM
 */
NSAPI_PUBLIC void output_user_selector(char *db_path, char *group, 
                                       char *highlight, char *except,
                                       char *name, int none, int multiple)
{
    int         rv;
    void       *padb;
    int         i, j, isselected;
    int         id;
    char       *user;
    int         userCount;
    void       *groupUserList = NULL;
    int         groupUserCount= 0;
    char       *guname        = NULL;
 
    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0)
        report_error(SYSTEM_ERROR, db_path,
                     "Failed to open database while trying "
                     "to list groups.");
    else {
        void *sortList;
 
        ulsAlloc(&sortList);
        rv = nsadbEnumerateUsers(NULL, padb, (void *)sortList, userEnumCB);
 
        if((multiple) && (group))  {
            groupUserList=_list_group_users(padb, group, 0);
            if(groupUserList)  {
                ulsSortName(groupUserList);
                ulsGetCount(groupUserList, &groupUserCount);
            }
        }
        nsadbClose(padb, 0);
        ulsSortName(sortList);
        ulsGetCount(sortList, &userCount);
 
        if (userCount > 0) {
            /* Make a pulldown if we can.  If the size is bigger than the
             * overflow value, make it a box to hack around the fact that
             * the X Navigator can't scroll pulldown lists. */
            if((multiple) || (userCount > SELECT_OVERFLOW))  {
                printf("<SELECT size=5 name=%s %s>",
                       name, multiple? "MULTIPLE" : "");
            }  else  {
                printf("<SELECT name=%s>", name);
            }
            if((!multiple) && (none))  {
                printf("<OPTION value=NONE>NONE\n");
            }
 
            for (i=0; i<userCount; ++i) {
                user  = NULL;
                guname = NULL;
                isselected=0;
                ulsGetEntry(sortList, i, &id, &user);
                if (user) {
                    if((except) && (!strcmp(user, except)))
                        continue;
                    if((highlight) && (!strcmp(user, highlight)))
                        isselected=1;
                    if(groupUserList && !isselected)  {
                        for(j=0; j < groupUserCount; j++)  {
                            ulsGetEntry(groupUserList, j, &id, &guname);
#if 0
                            if(guname[0] > user[0])  {
                                /* Both lists are sorted, therefore, if we've
                                 * hit a letter that's after the group letter,
                                 * it must not be here. */
                                /* What can I say, it's a pathetic attempt
                                 * on my part to mask the fact that I know
                                 * this is inefficient. */
                                break;
                            }
#endif
                            if(!strcmp(guname, user))  {
                                isselected=1;
                                break;
                            }
                        }
                    }
                    printf("<OPTION %s>%s\n",
                           isselected? "SELECTED" : "", user);
                }
            }
            printf("</SELECT>");
        } else {
            printf("<b>(No users have been created.)</b>");
        }
        ulsFree(&sortList);
        if(groupUserList)
            ulsFree(&groupUserList);
    }
}


int _item_in_list(char *item, char **list)
{
    register int i;
    if(!list) return -1;
    for(i=0; list[i]; i++)  {
        if(!strcmp(list[i], item))
            return i;
    }
    return -1;
}

/* Take a char ** null terminated list of group names, and change a user's
 * memberships so those are the only groups he's in.   MLM */
NSAPI_PUBLIC void change_user_membership(char *db_path, char *user, 
                                          char **new_groups)
{
    void *sortList;
    int rv;
    void *padb;
    UserObj_t  *uoptr = NULL;
    GroupObj_t *goptr = NULL; 
    int groupCount=0;
    register int i;
    int index;
    int id;
    char *group;

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0)
        report_error(SYSTEM_ERROR, db_path,
                     "Failed to open database while trying "
                     "to list groups.");
 
    sortList=_list_user_groups(padb, user, 0);
    if(sortList)  {
        ulsSortName(sortList);
        ulsGetCount(sortList, &groupCount);
    }

    rv = nsadbFindByName(NULL, padb, user, AIF_USER, (void **)&uoptr);
    if (uoptr == 0) {
        report_error(INCORRECT_USAGE, user, "The user was not found.");
    }

    /* First check the groups he's already in.  Remove any that no longer
     * appear in the list. */
    for(i=0; i<groupCount; ++i)  {
        ulsGetEntry(sortList, i, &id, &group);

        if( (index=_item_in_list(group, new_groups)) == -1)  {
            goptr=0;
            rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
            if (goptr == 0) {
                report_error(INCORRECT_USAGE, group,
                             "The group was not found.");
            }
            rv = nsadbRemUserFromGroup(NULL, padb, goptr, uoptr);
        }  else  {
            /* This group is in the list, so mark it as taken care of. */
            if(new_groups)
                new_groups[index][0]='\0';
        }
    }
    /* Add the user to any remaining groups. */
    if(new_groups)  {
        for(i=0; new_groups[i]; i++)  {
            if(new_groups[i][0] == '\0')
                continue;
            goptr=0;
            rv = nsadbFindByName(NULL, padb, new_groups[i], 
                                 AIF_GROUP, (void **)&goptr);
            if (goptr == 0) {
                report_error(INCORRECT_USAGE, group,"The group was not found.");
            }
            rv = nsadbAddUserToGroup(NULL, padb, goptr, uoptr);
        }
    }

    nsadbClose(padb, 0);
}

/* Take a char ** null terminated list of user names, and change a group's
 * memberships so those are the only users it has.   MLM */
/* Again, if group_users is 1, then the new_users are assumed to be groups. */
NSAPI_PUBLIC void change_group_membership(char *db_path, char *group, 
                                          int group_users, char **new_users)
{
    void *sortList;
    int rv;
    void *padb;
    UserObj_t  *uoptr = NULL;
    GroupObj_t *goptr = NULL; 
    GroupObj_t *ugoptr = NULL; 
    int userCount=0;
    register int i;
    int index;
    int id;
    char *user;

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0)
        report_error(SYSTEM_ERROR, db_path,
                     "Failed to open database while trying "
                     "to list groups.");
 
    sortList=_list_group_users(padb, group, group_users);
    if(sortList)  {
        ulsSortName(sortList);
        ulsGetCount(sortList, &userCount);
    }

    rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
    if (goptr == 0) {
        report_error(INCORRECT_USAGE, group, "The group was not found.");
    }

    /* First check the users already there.  Remove any that no longer
     * appear in the list. */
    for(i=0; i<userCount; ++i)  {
        ulsGetEntry(sortList, i, &id, &user);
        if( (index=_item_in_list(user, new_users)) == -1)  {
            if(group_users)  {
                ugoptr=0;
                rv = nsadbFindByName(NULL, padb, user, AIF_GROUP, 
                                     (void **)&ugoptr);
                if (ugoptr == 0) {
                    report_error(INCORRECT_USAGE, user,
                                 "The group was not found.");
                }
                rv = nsadbRemGroupFromGroup(NULL, padb, goptr, ugoptr);
            }  else  {
                uoptr=0;
                rv = nsadbFindByName(NULL, padb, user, AIF_USER, 
                                     (void **)&uoptr);
                if (uoptr == 0) {
                    report_error(INCORRECT_USAGE, user,
                                 "The user was not found.");
                }
                rv = nsadbRemUserFromGroup(NULL, padb, goptr, uoptr);
            }
        }  else  {
            /* This user is in the list, so mark it as taken care of. */
            if(new_users)
                new_users[index][0]='\0';
        }
    }
    /* Add any remaining users. */
    if(new_users)  {
        for(i=0; new_users[i]; i++)  {
            if(new_users[i][0] == '\0')
                continue;
            if(group_users)  {
                ugoptr=0;
                rv = nsadbFindByName(NULL, padb, new_users[i], 
                                     AIF_GROUP, (void **)&ugoptr);
                if (ugoptr == 0) {
                    report_error(INCORRECT_USAGE, new_users[i],
                                 "The group was not found.");
                }
                rv = nsadbAddGroupToGroup(NULL, padb, goptr, ugoptr);
                if(rv) report_error(SYSTEM_ERROR, new_users[i],
                                    "Unable to add group to group");
            }  else  {
                uoptr=0;
                rv = nsadbFindByName(NULL, padb, new_users[i], 
                                     AIF_USER, (void **)&uoptr);
                if (uoptr == 0) {
                    report_error(INCORRECT_USAGE, new_users[i],
                                 "The user was not found.");
                }
                rv = nsadbAddUserToGroup(NULL, padb, goptr, uoptr);
                if(rv) report_error(SYSTEM_ERROR, new_users[i],
                                    "Unable to add user to group");
            }
        }
    }

    nsadbClose(padb, 0);
}

/*
 * output a table with the groups that aren't members of the group
 */ 
NSAPI_PUBLIC void output_nongrpgroup_membership(char *db_path, char *group, char *filter)
{
    int         rv;
    GroupObj_t *goptr = 0;
    void       *padb;  
    USI_t      *gidlist;
    int         i;
    int         id;
    char       *memgroup;
    int         groupCount;
    char        line[BIG_LINE]; 

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, db_path, 
                     "Failed to open database while trying "
                     "to list group membership.");
    } else {
	rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
        if (goptr == 0) {
            report_error(SYSTEM_ERROR, group, 
                         "Unable to find group when trying to "
                         "list group membership.");
            rv = 1;
	    nsadbClose(padb, 0);
        } else {
            void *sortList;

            printf("<table border=1><caption align=left>\n");
            printf("<b>These groups are not members of %s:</b>", group);
            printf("</caption>\n");

            ulsAlloc(&sortList);
	    rv = nsadbEnumerateGroups(NULL,
				      padb, (void *)sortList, groupEnumCB);
	    nsadbClose(padb, 0);
            ulsSortName(sortList);
            ulsGetCount(sortList, &groupCount);

            if (groupCount > 0) {
                gidlist = UILLIST(&goptr->go_groups);
                for (i=0; i<groupCount; ++i) {
                    memgroup = NULL;
                    ulsGetEntry(sortList, i, &id, &memgroup);
                    if (  memgroup && 
                          strcmp(memgroup, group) &&
                         !idfound(id, (int*)gidlist, UILCOUNT(&goptr->go_groups)) &&
                          passfilter(memgroup, filter)) {
                        printf("<tr>");
                        printf("<td><b>%s</b></td><td>", memgroup);
                        sprintf(line, "group=%s", memgroup);
                        output_cgi_link("Edit Group", "grped", line);
                        printf("</td><td>");
                        sprintf(line, "addtogrp_but=1&memgroup=%s&group=%s", 
                                      memgroup, group);
                        output_cgi_link("Add to Group", "grped", line);
                        printf("</td>\n");
                    }
                }
                printf("</table>\n");
            }
            ulsFree(&sortList);
	    groupFree(goptr);
        }
    }
    return;
}

/*
 * output a table with the users that aren't members of the group
 */ 
NSAPI_PUBLIC void output_nonuser_membership(char *db_path, char *group, char *filter)
{
    int         rv;
    GroupObj_t *goptr = 0;
    void       *padb;  
    USI_t      *gidlist;
    int         i;
    int         id;
    char       *memuser;
    int         userCount;
    char        line[BIG_LINE]; 

    rv = nsadbOpen(NULL, db_path, 0, &padb);
    if (rv < 0) {
        report_error(SYSTEM_ERROR, db_path, 
                     "Failed to open database while trying "
                     "to list group membership.");
    } else {
	rv = nsadbFindByName(NULL, padb, group, AIF_GROUP, (void **)&goptr);
        if (goptr == 0) {
	    nsadbClose(padb, 0);
            report_error(SYSTEM_ERROR, group, 
                         "Unable to find group when trying to "
                         "list user membership.");
            rv = 1;
        } else {
            void *sortList;

            printf("<table border=1><caption align=left>\n");
            printf("<b>These users are not members of %s:</b>", group);
            printf("</caption>\n");

            ulsAlloc(&sortList);
	    rv = nsadbEnumerateUsers(NULL, padb, (void *)sortList, userEnumCB);
	    nsadbClose(padb, 0);

            ulsSortName(sortList);
            ulsGetCount(sortList, &userCount);

            if (userCount > 0) {
                gidlist = UILLIST(&goptr->go_users);
                for (i=0; i<userCount; ++i) {
                    memuser = NULL;
                    ulsGetEntry(sortList, i, &id, &memuser);
                    if (  memuser &&
                         !idfound(id, (int*)gidlist, UILCOUNT(&goptr->go_users)) &&
                          passfilter(memuser, filter)) {
                        printf("<tr>");
                        printf("<td><b>%s</b></td><td>", memuser);
                        sprintf(line, "memuser=%s", memuser);
                        output_cgi_link("Edit User", "usred", line);
                        printf("</td><td>");
                        sprintf(line, "addtogrp_but=1&memuser=%s&group=%s", 
                                       memuser, group);
                        output_cgi_link("Add to Group", "grped", line);
                        printf("</td>\n");
                    }
                }
                printf("</table>\n");     
            }
            ulsFree(&sortList);
	    groupFree(goptr);
        }
    }
    return;
}

NSAPI_PUBLIC char *get_acl_file()
{
    char           line[BIG_LINE];
    char *acl_file = get_mag_var("ACLFile");
    if (!acl_file) {
        sprintf(line, "%s%cgenerated.%s.acl",
                      get_httpacl_dir(), FILE_PATHSEP, get_srvname(0));
        set_mag_var("ACLFile", line);
        acl_file = STRDUP(line);
    }
    if(!file_exists(acl_file))  {
        FILE *f;
        if(! (f=fopen(acl_file, "w")) )
            report_error(FILE_ERROR, acl_file, "Could not open file.");
        fclose(f);
    }
    return acl_file;
}

NSAPI_PUBLIC char *get_workacl_file()
{
    char           line[BIG_LINE];
    char          *workacl_file;
    sprintf(line, "%s%cgenwork.%s.acl",
                      get_httpacl_dir(), FILE_PATHSEP, get_srvname(0));
    workacl_file = STRDUP(line);

    if(!file_exists(workacl_file))  {
        FILE *f;
        char *current=get_acl_file();
        if(file_exists(current))  {
            cp_file(current, workacl_file, 0644);
        }  else  {
            if(! (f=fopen(workacl_file, "w")) )
                report_error(FILE_ERROR, workacl_file, "Could not open file.");
            fclose(f);
        }
    }
    return workacl_file;
}   

/*
 * get_acl_info - Open the specified ACL file.  Return a context
 *     handle into it, the data it contains, and whether this is
 *     a default allow or default deny resource. 
 *
 * Returns: 0 if it works
 *          AUTHDB_ACL_FAIL    (-1) if it fails
 *          AUTHDB_ACL_ODD_ACL (-2) if the ACL doesn't appear to be one  
 *                                  generated by the CGI.
 */
NSAPI_PUBLIC int get_acl_info(char *acl_file, char *acl_name, void **pacl,
                               char ***hosts, authInfo_t **authinfo, 
                               char ***users, char ***userhosts,
                               int *fdefaultallow)
{
    int           rv = 0;
    char          *acl_sig = NULL;
    ACContext_t   *acl_con = NULL;
    ACL_t         *acl;

    if (hosts)
        *hosts         = NULL;
    if (authinfo)
        *authinfo      = NULL;
    if (users)
        *users         = NULL;
    if (userhosts)
        *userhosts     = NULL;
    if (fdefaultallow)
        *fdefaultallow = 0;

    if ((rv = accReadFile(NULL, acl_file, &acl_con)))
        rv = AUTHDB_ACL_FAIL;
    else if (!(rv = aclFindByName(acl_con, acl_name, NULL, 0, &acl)))
        rv = AUTHDB_ACL_NOT_FOUND;
    else {
        /*
         * If we get the ACL open, get it's signiture to see if
         * it looks like one of ours.  Each directive is identified
         * by a single character in the signiture string.  The ones
         * we care about are: 
         *     'a' - default allow 
         *     'd' - default deny
         *     'r' - default authenticate
         * 
         * The ACL used for default allow access has these directives:
         *    default allow anyone at *;     (required, shows up as an 'a')
         *    default deny anyone at (hosts);     (optional, shows up as a 'd')
         * 
         * The ACL used for default deny access has these directives:
         *    default deny anyone at *;     (required, shows up as an 'd')
         *    default allow anyone at (hosts);     (optional, shows up as a 'a')
         *    default authenticate...   (optional, shows up as an 'r')
         *    default allow (users) at (hosts);   (optional, shows up as a 'a')
         *
         * Valid signitures are:
         * "a"
         * "ad"
         * "d"
         * "da"
         * "dra"
         * "dara"
         */
 
        if (acl) 
            acl_sig = aclGetSignature(acl);

        if (acl_sig) {
            if      (!strcmp(acl_sig, "a")) {
                if (fdefaultallow)
                    *fdefaultallow = 1;
                rv = 0;
            }
            else if (!strcmp(acl_sig, "ad")) {
                if (fdefaultallow)
                    *fdefaultallow = 1;
                if (hosts)
                    *hosts = aclGetHosts(acl, 2, 1);
                rv = 0;
            }
            else if (!strcmp(acl_sig, "d")) {
                if (fdefaultallow)
                    *fdefaultallow = 0;
                rv = 0;
            }
            else if (!strcmp(acl_sig, "da")) {
                if (fdefaultallow)
                    *fdefaultallow = 0;
                if (hosts)
                    *hosts = aclGetHosts(acl, 2, 1);
                rv = 0;
            }
            else if (!strcmp(acl_sig, "dra")) {
                if (fdefaultallow)
                    *fdefaultallow = 0;
                if (authinfo) {
                    char *p;
                    *authinfo = (authInfo_t *)MALLOC(sizeof(authInfo_t));
                    memset(*authinfo, 0, (sizeof(authInfo_t)));
                    if ((p = aclGetAuthMethod(acl, 2)))
                        (*authinfo)->type    = strdup(p);
                    if ((p = aclGetDatabase(acl, 2)))
                        (*authinfo)->db_path = strdup(p);
                    if ((p = aclGetPrompt(acl, 2)))
                        (*authinfo)->prompt  = strdup(p);
                }
                if (users)
                    *users = aclGetUsers(acl, 3, 1);
                if (userhosts)
                    *userhosts = aclGetHosts(acl, 3, 1);
                rv = 0;
            }
            else if (!strcmp(acl_sig, "dara")) {
                if (fdefaultallow)
                    *fdefaultallow = 0;
                if (hosts)
                    *hosts = aclGetHosts(acl, 2, 1);
                if (authinfo) {
                    char *p;
                    *authinfo = (authInfo_t *)MALLOC(sizeof(authInfo_t));
                    memset(*authinfo, 0, (sizeof(authInfo_t)));
                    if ((p = aclGetAuthMethod(acl, 3)))
                        (*authinfo)->type    = strdup(p);
                    if ((p = aclGetDatabase(acl, 3)))
                        (*authinfo)->db_path = strdup(p);
                    if ((p = aclGetPrompt(acl, 3)))
                        (*authinfo)->prompt  = strdup(p);
                }
                if (users)
                    *users = aclGetUsers(acl, 4, 1);
                if (userhosts)
                    *userhosts = aclGetHosts(acl, 4, 1);
                rv = 0;
            }
            else
                rv = AUTHDB_ACL_ODD_ACL;
        }
        if (pacl)
            *pacl = (void *)acl_con;
    }
    return rv;
}


static void add_acl_rights(ACContext_t *acc)
{
    int rv;
    char **p;
    for (p = acl_read_rights; *p; ++p) {
        rv = aclRightDef(NULL, acc, *p, NULL);
    }
    for (p = acl_write_rights; *p; ++p) {
        rv = aclRightDef(NULL, acc, *p, NULL);
    }
}   

/* 
 * delete_acl_by_name - remove a specified acl.
 *
 * Return: 0 if it deletes an ACL. Otherwise something else.
 *
 */
NSAPI_PUBLIC int delete_acl_by_name(char *acl_file, char *acl_name)
{
    int            rv = 1;
    ACContext_t   *acl_con = NULL;
    ACL_t         *acl     = NULL;

    if (acl_file && acl_name)
    {
        rv = accReadFile(NULL, acl_file, &acl_con);

        if (!rv) {
            rv = aclFindByName(acl_con, acl_name, NULL, 0, &acl);
            if (rv == 1 && acl) {
                aclDelete(acl);    
                rv = accWriteFile(acl_con, acl_file, 0); 
                set_commit(0, 1); 
            }
        }
    }
    return rv;
}

/*
 * set_acl_info - Replaces the specified ACL with the information 
 *                provided.  The fdefaultallow is tells us whether
 *                to generate a default allow everyone type ACL, or
 *                a default deny everyone type ACL. 
 *
 *                If opening the ACL file fails with a file open
 *                type error, it is assumed to not exist, and a
 *                new one is created.
 *
 * Returns: 0 if it works
 *          AUTHDB_ACL_FAIL if it fails
 */
NSAPI_PUBLIC int set_acl_info(char *acl_file, char *acl_name, int prefix,
                               void **pacl, char **rights,
                               char **hosts, authInfo_t *authinfo, 
                               char **users, char **userhosts,
                               int fdefaultallow)
{
    int            rv      = AUTHDB_ACL_FAIL;
    ACContext_t   *acl_con = NULL;
    ACL_t         *acl     = NULL;
    int            amethod = AUTH_METHOD_BASIC;
    char          *db_path = NULL;
    char          *prompt  = NULL;

    /*
     * Digest parms
     */
    if (authinfo) {
        if (!strcmp(authinfo->type, "SSL"))
            amethod = AUTH_METHOD_SSL;
        else
            amethod = AUTH_METHOD_BASIC;
        db_path = authinfo->db_path;
        prompt  = authinfo->prompt;
    }

    if (prefix)
        prefix = ACLF_NPREFIX;
    else
        prefix = 0;

    /* 
     * Open the specified ACL file, destroy the existing ACL with
     * the specified name if it exists, and then write the new
     * stuff back out.
     */
    if (acl_file && acl_name)
    {
        (void)delete_acl_by_name(acl_file, acl_name);
        rv = accReadFile(NULL, acl_file, &acl_con);
  
        /*
         * If the file isn't there, create an empty context.
         */
        if (rv == ACLERROPEN)
            rv = accCreate(0, 0, &acl_con);

        if (rv)
            rv = AUTHDB_ACL_FAIL;
        else  {
            (void)add_acl_rights(acl_con);

            if (aclMakeNew(acl_con, "", acl_name,
                           rights, prefix, &acl)) { 
                rv = AUTHDB_ACL_FAIL;
            }
            else if (aclPutAllowDeny(
                              NULL, acl, 0, fdefaultallow, NULL, NULL)) {
                rv = AUTHDB_ACL_FAIL;
            }
            else if (hosts &&
                     (rv = aclPutAllowDeny(
                              NULL, acl, 0, !fdefaultallow, NULL, hosts))) {
                    
                rv = AUTHDB_ACL_FAIL;
            }
            else if (users && authinfo &&
                     ((rv = aclPutAuth(NULL, acl, 0, amethod, db_path, prompt))||
                      (rv = aclPutAllowDeny(NULL, acl, 0, !fdefaultallow, 
                                            users, userhosts)))) {
                    
                rv = AUTHDB_ACL_FAIL;
            }
            else {
                 if (accWriteFile(acl_con, acl_file, 0)) {
                     rv = AUTHDB_ACL_FAIL;
                 }
                 else {
                     set_commit(0, 1); 
                     rv = 0;
                     if (pacl)
                         *pacl = (void *)acl;
                 }
            }
        }
    }
    return rv;
}

/* 
 * get_acl_names - Passes back the names of the read and write
 *                 ACLs for the current resource. 
 *                 The directive parm is usually "acl", but is
 *                 also used to find stashed items under names
 *                 like "acl-disabled".
 *     Returns 0 if no other ACLs are found, 1 if an ACL that
 *     doesn't match the type of name we generate.
 */
NSAPI_PUBLIC int get_acl_names(char **readaclname, char **writeaclname, char *directive)
{
    pblock **pbs;
    char    *aclname;
    int      fother_acls = 0;
    char   **config = get_adm_config();
    char    *curres = get_current_resource(config);
    int      rtype  = get_current_restype(config);
    int      i;

    *readaclname  = NULL;
    *writeaclname = NULL;

    pbs = list_pblocks(rtype, curres, "PathCheck", CHECK_ACL_FN);  

    if (pbs) {
        for (i=0; pbs[i]; ++i)  {
            aclname = pblock_findval(directive, pbs[i]);
            if (is_readacl(aclname))
               *readaclname = strdup(aclname); 
            else if (is_writeacl(aclname))
               *writeaclname = strdup(aclname); 
            else
               fother_acls = 1;
        }
    }
    return fother_acls;
}

NSAPI_PUBLIC int is_readacl(char *name)  {
    if (name)
        return strstr(name, ACLNAME_READ_COOKIE)?1:0; 
    else
        return 0;
}

NSAPI_PUBLIC int is_writeacl(char *name) {
    if (name)
        return strstr(name, ACLNAME_WRITE_COOKIE)?1:0;
    else
        return 0;
}   

NSAPI_PUBLIC int admin_is_ipaddr(char *p)
{
    int i;
    int num_found = 0;

    if (!p || !p[0])
        return 0;  /* NULL isn't an IP Address */
    else {
        for (i=0; p[i]; ++i) {
            if (isalpha(p[i]))
                return 0; /* If it has an alpha character, it's not IP addr */
            else if (isdigit(p[i])) {
                num_found = 1;
            }
        }
    }
    /*
     * Well, hard to say what it is, but if there's at least a number
     * in it, and the ip number checker can parse it, we'll call it
     * an IP address;
     */
    if (num_found && get_ip_and_mask(p))
        return 1;
    else
        return 0;
}

/*
 * Get the hostnames and ipaddrs strings from the single hosts array
 * of string pointers.
 */
NSAPI_PUBLIC void get_hostnames_and_ipaddrs(char **hosts, char **hostnames, char **ipaddrs)
{
    char *p;
    int nbipaddrs    = 0;
    int nbhostnames  = 0;
    int i;
    if (hosts && hostnames && ipaddrs) {
        *hostnames = NULL;
        *ipaddrs   = NULL;
        /*
         * Make two passes, once to total the size needed for
         * the hosts and ipaddrs string, then alloc them and
         * strcat the strings in.
         */
        for(i=0, p=hosts[i]; p; p=hosts[++i]) {
            if (admin_is_ipaddr(p))
                nbipaddrs += strlen(p) + 2;  /* Teminator and "," */
            else
                nbhostnames += strlen(p) + 2;
        }

        if (nbhostnames) {
            *hostnames = (char *)MALLOC(nbhostnames + 1);
            memset(*hostnames, 0, nbhostnames);
        }            
        if (nbipaddrs) {
            *ipaddrs = (char *)MALLOC(nbipaddrs + 1);
            memset(*ipaddrs, 0, nbipaddrs);
        }

        /*
         * We've got the space, now go look at each, strcat it
         * into the correct string, prefixed with a "," for all
         * but the first.
         */
        for(i=0, p=hosts[i]; p; p=hosts[++i]) {
            if (admin_is_ipaddr(p)) {
                if (strlen(*ipaddrs))
                    strcat(*ipaddrs, ",");
                strcat(*ipaddrs, p);
            }
            else {
                if (strlen(*hostnames)) 
                    strcat(*hostnames, ",");
                strcat(*hostnames, p);
            }
        }
    }
    return;
}

/*
 * Get the usernames and groups strings from the single users array
 * of string pointers.
 */
NSAPI_PUBLIC void get_users_and_groups(char **users, char **usernames, char **groups, 
                          char *db_path)
{
    char      *p;
    int        nbusernames    = 0;
    int        nbgroups       = 0;
    int        i;
    int        is_user  = 0;
    int        is_group = 0;

    if (users && usernames && groups) {
        *usernames = NULL;
        *groups    = NULL;
        /*
         * Make two passes, once to total the size needed for
         * the strings, then alloc them and strcat the strings in.
         */
        for(i=0, p=users[i]; p; p=users[++i]) {
            is_user  = 0;
            is_group = 0;
            groupOrUser(db_path, p, &is_user, &is_group);

	    /* Enclose user/group name in quotes if necessary */
	    p = aclSafeIdent(p);

            if (is_user)
                nbusernames += strlen(p) + 2;  /* Teminator and "," */
            else if (is_group)
                nbgroups += strlen(p) + 2;
        }

        if (nbusernames) {
            *usernames = (char *)MALLOC(nbusernames + 1);
            memset(*usernames, 0, nbusernames);
        }            
        if (nbgroups) {
            *groups = (char *)MALLOC(nbgroups + 1);
            memset(*groups, 0, nbgroups);
        }

        /*
         * We've got the space, now go look at each, strcat it
         * into the correct string, prefixed with a "," for all
         * but the first.
         */
        for(i=0, p=users[i]; p; p=users[++i]) {
            is_user  = 0;
            is_group = 0;
            groupOrUser(db_path, p, &is_user, &is_group);

	    /* Enclose user/group name in quotes if necessary */
	    p = aclSafeIdent(p);

            if (is_user) {
                if (strlen(*usernames))
                    strcat(*usernames, ",");
                strcat(*usernames, p);
            }
            else if (is_group) {
                if (strlen(*groups)) 
                    strcat(*groups, ",");
                strcat(*groups, p);
            }
        }
    }
    return;
}

/*
 * Load from host and IP Addr strings into the array of
 * pointers to strings.
 */
NSAPI_PUBLIC void load_host_array(char ***hosts, char *hostnames, char *ipaddrs)
{
    char *tok;
    int   nMax = 20;
    int   nCur = 0;
    char **hostarr;
    char *valid_ip;

    if (hosts) {
        hostarr = new_strlist(nMax);
        hostarr[0] = NULL;
        if (hostnames) {
            for (tok = strtok(hostnames, ",");
                 tok;
                 tok = strtok(NULL, ","))
            {
                if (!(nCur < nMax)) {
                    nMax += 20;
                    hostarr = grow_strlist(hostarr, nMax);
                }
                hostarr[nCur] = strdup(tok);
                hostarr[++nCur] = NULL;
            }
        }
        if (ipaddrs) {  
            for (tok = strtok(ipaddrs, ",");
                 tok;
                 tok = strtok(NULL, ","))
            {
                if (!(nCur < nMax)) {
                    nMax += 20;
                    hostarr = grow_strlist(hostarr, nMax);
                }
                valid_ip = get_ip_and_mask(tok);
                if (valid_ip) {
                    hostarr[nCur] = strdup(valid_ip);
                    hostarr[++nCur] = NULL;
                }
            }
        }
        *hosts = hostarr;
    }

    return;
}

void load_users_array(char ***users, char *usernames, char *groups)
{
    char *tok;
    int   nMax = 20;
    int   nCur = 0;
    char **userarr;

    if (users) {
        userarr    = new_strlist(nMax);
        userarr[0] = NULL;
        if (usernames) {
            for (tok = strtok(usernames, ",");
                 tok;
                 tok = strtok(NULL, ","))
            {
                if (!(nCur < nMax)) {
                    nMax += 20;
                    userarr = grow_strlist(userarr, nMax);
                }
                userarr[nCur]   = strdup(tok);
                userarr[++nCur] = NULL;
            }
        }
        if (groups) {  
            for (tok = strtok(groups, ",");
                 tok;
                 tok = strtok(NULL, ","))
            {
                if (!(nCur < nMax)) {
                    nMax += 20;
                    userarr = grow_strlist(userarr, nMax);
                }
                userarr[nCur]   = strdup(tok);
                userarr[++nCur] = NULL;
            }
        }
        *users = userarr;
    }

    return;
}

/* Removes enclosing double quotes from a string (in place) */
NSAPI_PUBLIC char * str_unquote(char * str)
{
    if (str) {
	if (str[0] == '"') {
	    int len = strlen(str);

	    if (str[len-1] == '"') {
		str[len-1] = 0;
		++str;
	    }
	}
    }

    return str;
}
