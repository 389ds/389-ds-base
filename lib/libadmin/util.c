/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 * util.c:  Miscellaneous stuffs
 *            
 * All blame to Mike McCool
 */

#include "libadmin/libadmin.h"
#include "base/util.h"
#include "private/pprio.h"

#ifdef XP_UNIX
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#else
#include <base/file.h>
#include <sys/stat.h>
#endif /* WIN32? */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>     /* isdigit */

#define NUM_ENTRIES 64

#ifdef MCC_PROXY
char *
XP_GetString()
{
        return "ZAP";
}
#endif

#ifdef XP_WIN32
char *GetQueryNT(void)
{
    char *qs = getenv("QUERY_STRING");
    if(qs && (*qs == '\0'))
        qs = NULL;
    return qs;
}
#endif  /* XP_WIN32 */

void escape_for_shell(char *cmd) {
    register int x,y,l;

    l=strlen(cmd);
    for(x=0;cmd[x];x++) {
        if(strchr(" &;`'\"|*!?~<>^()[]{}$\\\x0A",cmd[x])){
            for(y=l+1;y>x;y--)
                cmd[y] = cmd[y-1];
            l++; /* length has been increased */
            cmd[x] = '\\';
            x++; /* skip the character */
        }
    }
}

int _admin_dumbsort(const void *s1, const void *s2)
{
    return strcmp(*((char **)s1), *((char **)s2));
}

#ifdef XP_UNIX /* WIN32 change */
/* Lists all files in a directory. */
char **list_directory(char *path, int dashA)  
{
    char **ar;
    DIR *ds;
    struct dirent *d;
    int n, p;

    n = NUM_ENTRIES;
    p = 0;

    ar = (char **) MALLOC(n * sizeof(char *));

    if(!(ds = opendir(path))) {
        return NULL;
    }

    while( (d = readdir(ds)) ) {
        if ( ( d->d_name[0] != '.' ) ||
	     ( dashA && d->d_name[1] &&
	       ( d->d_name[1] != '.' || d->d_name[2] ) ) ) {
            if(p == (n-1)) {
                n += NUM_ENTRIES;
                ar = (char **) REALLOC(ar, n*sizeof(char *));
            }
            /* 2: Leave space to add a trailing slash later */
            ar[p] = (char *) MALLOC(strlen(d->d_name) + 2);
            strcpy(ar[p++], d->d_name);
        }
    }
    closedir(ds);

    qsort((void *)ar, p, sizeof(char *), _admin_dumbsort);
    ar[p] = NULL;

    return ar;
}

#else /* WIN32 change */
/* Lists all files in a directory. */
char **list_directory(char *path, int dashA)  
{
    char **ar;
    SYS_DIR ds;
    SYS_DIRENT *d;
    int n, p;

    n = NUM_ENTRIES;
    p = 0;

    ar = (char **) MALLOC(n * sizeof(char *));

    if(!(ds = dir_open(path))) {
        return NULL;
    }

    while( (d = dir_read(ds)) ) {
        if ( ( d->d_name[0] != '.' ) ||
	     ( dashA && d->d_name[1] &&
	       ( d->d_name[1] != '.' || d->d_name[2] ) ) ) {
            if(p == (n-1)) {
                n += NUM_ENTRIES;
                ar = (char **) REALLOC(ar, n*sizeof(char *));
            }
            /* 2: Leave space to add a trailing slash later */
            ar[p] = (char *) MALLOC(strlen(d->d_name) + 2);
            strcpy(ar[p++], d->d_name);
        }
    }
    dir_close(ds);

    qsort((void *)ar, p, sizeof(char *), _admin_dumbsort);
    ar[p] = NULL;

    return ar;
}
#endif /* WIN32 */

int file_exists(char *fn)
{
    struct stat finfo;

    if(!stat(fn, &finfo))
        return 1;
    else
        return 0;
}

int get_file_size(char *fn)
{
    struct stat finfo;
    int ans = -1;
 
    if(!stat(fn, &finfo))  {
        ans = finfo.st_size;
    }  else  {
        report_error(FILE_ERROR, fn, "Could not get size of file.");
    }
    return ans;
}

int ADM_mkdir_p(char *dir, int mode)
{
   char path[PATH_MAX];
   struct stat fi;
   char *slash = NULL;
    
   if (dir)
      strcpy (path, dir);
   else
      return 0;

   if (slash = strchr(path, FILE_PATHSEP)) 
      slash++; /* go past root */
   else
      return 0;

   while (slash && *slash) {
      slash = strchr(slash, FILE_PATHSEP);
      if (slash) *slash = '\0'; /* check path till here */
                
      if (stat(path, &fi) == -1) {
#ifdef XP_UNIX
         if (mkdir(path, mode) == -1)
#else  /* XP_WIN32 */
         if (!CreateDirectory(path, NULL))
#endif
            return 0;
      }

      if (slash) {
         *slash = FILE_PATHSEP; /* restore path */
         slash++; /* check remaining path */
      }
   }
   return 1;
}

int ADM_copy_directory(char *src_dir, char *dest_dir)
{
   SYS_DIR ds;
   SYS_DIRENT *d;
   struct stat fi;
   char src_file[PATH_MAX], dest_file[PATH_MAX], fullname[PATH_MAX];

   if (!(ds = dir_open(src_dir))) 
      report_error(FILE_ERROR, "Can't read directory", src_dir);

   while (d = dir_read(ds)) {
      if (d->d_name[0] != '.') {
         sprintf(fullname, "%s/%s", src_dir, d->d_name);
         if (system_stat(fullname, &fi) == -1)
            continue;

         sprintf(src_file,  "%s%c%s", src_dir,  FILE_PATHSEP, d->d_name);
         sprintf(dest_file, "%s%c%s", dest_dir, FILE_PATHSEP, d->d_name);
         if (S_ISDIR(fi.st_mode)) {
            char *sub_src_dir = STRDUP(src_file);
            char *sub_dest_dir = STRDUP(dest_file);
            if (!ADM_mkdir_p(sub_dest_dir, 0755)) {
               report_error(FILE_ERROR, "Cannot create directory",
                                                           sub_dest_dir);
               return 0;
            }
            if (!ADM_copy_directory(sub_src_dir, sub_dest_dir))
               return 0;
            FREE(sub_src_dir);
            FREE(sub_dest_dir);
         }
         else 
            cp_file(src_file, dest_file, 0644);
      }
   }
   dir_close(ds);
   return(1);
}

void ADM_remove_directory(char *path)
{
    struct stat finfo;
    char **dirlisting;
    register int x=0;
    int stat_good = 0;
    char *fullpath = NULL;

#ifdef XP_UNIX
    stat_good = (lstat(path, &finfo) == -1 ? 0 : 1);
#else /* XP_WIN32 */
    stat_good = (stat(path, &finfo) == -1 ? 0 : 1);
#endif
 
    if(!stat_good) return;

    if(S_ISDIR(finfo.st_mode))  {
        dirlisting = list_directory(path,1);
        if(!dirlisting) return;

        for(x=0; dirlisting[x]; x++)  {
            fullpath = (char *) MALLOC(strlen(path) + 
                                       strlen(dirlisting[x]) + 4);
            sprintf(fullpath, "%s%c%s", path, FILE_PATHSEP, dirlisting[x]);
#ifdef XP_UNIX
            stat_good = (lstat(fullpath, &finfo) == -1 ? 0 : 1);
#else /* XP_WIN32 */
            stat_good = (stat(fullpath, &finfo) == -1 ? 0 : 1);
#endif
            if(!stat_good) continue;
            if(S_ISDIR(finfo.st_mode))  {
                ADM_remove_directory(fullpath);
            }  else  {
                unlink(fullpath);
            }
            FREE(fullpath);
        }
#ifdef XP_UNIX
        rmdir(path);
#else /* XP_WIN32 */
        RemoveDirectory(path);
#endif
    }  else  {
        delete_file(path);
    }
    return;
}

/* return: mtime(f1) < mtime(f2) ? */
int mtime_is_earlier(char *file1, char *file2)
{
    struct stat fi1, fi2;

    if(stat(file1, &fi1))  {
        return -1;
    }
    if(stat(file2, &fi2))  {
        return -1;
    }
    return( (fi1.st_mtime < fi2.st_mtime) ? 1 : 0);
}

time_t get_mtime(char *fn)
{
    struct stat fi;

    if(stat(fn, &fi))
        return 0;
    return fi.st_mtime;
}

int all_numbers(char *target) 
{
    register int x=0;
  
    while(target[x])
        if(!isdigit(target[x++]))
            return 0;
    return 1;
}


int all_numbers_float(char *target) 
{
    register int x;
    int seenpt;

    for(x = 0, seenpt = 0; target[x]; ++x) {
        if((target[x] == '.') && (!seenpt))
            seenpt = 1;
        else if((!isdigit(target[x])) && seenpt)
            return 0;
    }
    return 1;
}

/* Get the admin/config directory. */
char *get_admcf_dir(int whichone)
{
#ifdef USE_ADMSERV
    char *confdir = NULL;

    char *tmp = get_num_mag_var(whichone, "#ServerRoot");
    if(!tmp)  {
        /* sigh */
        report_error(INCORRECT_USAGE, "No server root variable",
                     "The magnus.conf variable #ServerRoot was "
                     "not set.  Please set the value of your server "
                     "root through the administrative forms.");
    }
    confdir = (char *) MALLOC(strlen(tmp) + strlen("config") + 4);
    sprintf(confdir, "%s%cconfig%c", tmp, FILE_PATHSEP, FILE_PATHSEP);

    return confdir;
#else
    char *confdir;
    char line[BIG_LINE];
    sprintf(line, "%s%cadmin%cconfig%c", get_mag_var("#ServerRoot"),
                  FILE_PATHSEP, FILE_PATHSEP, FILE_PATHSEP);
    confdir = STRDUP(line);
#endif
    return STRDUP(confdir);
}

/* Get the current HTTP server URL. */
char *get_serv_url(void)
{
#ifdef USE_ADMSERV
    char *name = get_mag_var("ServerName");
    char *port = get_mag_var("Port");
    char *protocol = NULL;
    char line[BIG_LINE];

#ifndef NS_UNSECURE
    char *security = get_mag_var("Security");

    if(!security || strcasecmp(security, "on"))  {
        protocol = STRDUP("http");
        if(!strcmp(port, "80"))
            port = STRDUP("");
        else  {
            sprintf(line, ":%s", port);
            port = STRDUP(line);
        }
    }  else  {
        protocol = STRDUP("https");
        if(!strcmp(port, "443"))
            port = STRDUP("");
        else  {
            sprintf(line, ":%s", port);
            port = STRDUP(line);
        }
    }
#else
    protocol = STRDUP("http");
    if(!strcmp(port, "80"))
        port = STRDUP("");
    else  {
        sprintf(line, ":%s", port);
        port = STRDUP(line);
    }
#endif

    sprintf(line, "%s://%s%s", protocol, name, port);
    return(STRDUP(line));
#else
    return(getenv("SERVER_URL"));
#endif
}

/* ------------------------------- run_cmd -------------------------------- */


/* Previously in install. This is also pretty UNIX-ish. */

/* Nirmal: Added  code for Win32 implementation of this function. */

#include <signal.h>
#ifdef XP_UNIX
#include <sys/wait.h>
#endif /* XP_UNIX */


int run_cmd(char *cmd, FILE *closeme, struct runcmd_s *rm)
{
#ifdef WIN32
    HANDLE hproc;
    PROCESS_INFORMATION child;
    STARTUPINFO  siStartInfo ;
#else
    struct stat fi;
    int exstat;
    char *errmsg, tfn[128];
    FILE *f;
    int fd;
    pid_t pid;
#endif


#ifdef WIN32
    /* Nirmal:
	    For now, i will just spawn
	    a child in WINNT to execute the command. Communication to
	    the parent is done through stdout pipe, that was setup by
	    the parent.

    */	    
    hproc = OpenProcess(STANDARD_RIGHTS_REQUIRED, FALSE, GetCurrentProcessId());
    if (hproc == NULL) {
	    fprintf(stdout, "Content-type: text/html\n\n");
	    fflush(stdout);
	    report_error(SYSTEM_ERROR, NULL, "Could not open handle to myself");
	    return -1;  // stmt. not reached.
    }

    ZeroMemory(&child, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.lpReserved = siStartInfo.lpReserved2 = NULL;
    siStartInfo.cbReserved2 = 0;
    siStartInfo.lpDesktop = NULL;
    siStartInfo.dwFlags =  STARTF_USESHOWWINDOW;
//	Several fields arent used when dwFlags is not set.    
//    siStartInfo.hStdInput = hChildStdinRd;
//    siStartInfo.hStdOutput = siStartInfo.hStdError = hChildStdoutWr;
    siStartInfo.wShowWindow = SW_HIDE;
									
    if ( ! CreateProcess(
	    NULL,	// pointer to name of executable module
	    cmd,	// pointer to command line string
	    NULL,	// pointer to process security attribute
	    NULL,	// pointer to thread security attributes
	    TRUE,	// handle inheritance flag
	    0,		// creation flags
	    NULL,	// pointer to new environment block
	    NULL,	// pointer to current directory name
	    &siStartInfo,	// pointer to STARTUPINFO
	    &child	// pointer to PROCESS_INFORMATION
	   ))
    {
        rm->title = "CreateProcess failed";
        rm->msg = "run_cmd: Can't create new process. ";
        rm->arg = "";
        rm->sysmsg = 1;
	return -1;
    }
    else 
        return 0;
#else
    sprintf(cmd, "%s > /tmp/startmsg.%d 2>&1 < /dev/null", cmd, getpid()); /*  */
    /* FUCK UNIX SIGNALS. */
    signal(SIGCHLD, SIG_DFL);
    switch( (pid = fork()) ) {
      case 0:
        /* Hmm. Work around an apparent bug in stdio. */
        if(closeme)
            close(fileno(closeme));
        execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
        /* DOH! */
        sprintf(tfn, "/tmp/startmsg.%d", getpid());
        if(!(f = fopen(tfn, "w")))
            exit(1);
        fprintf(f, "Exec of %s failed. The error was %s.\n", cmd, 
                system_errmsg());
        fclose(f);
        exit(1);
      case -1:
        rm->title = "Fork failed";
        rm->msg = "Can't create new process. %s";
        rm->arg = "";
        rm->sysmsg = 1;
        return -1;
      default:
        sprintf(tfn, "/tmp/startmsg.%d", getpid());

        if(waitpid(pid, &exstat, 0) == -1) {
            rm->title = "Can't wait for child";
            rm->msg = "Can't wait for process. %s";
            rm->arg = "";
            rm->sysmsg = 1;
            return -1;
        }
        if(exstat) {
            if(!(fd = open(tfn, O_RDONLY))) {
                rm->title = "Can't open error file";
                rm->msg = "Can't find error file %s.";
                rm->arg = cmd;
                rm->sysmsg = 1;
                return -1;
            }
            fstat(fd, &fi);
            if((fi.st_size > 0) && (fi.st_size < 8192)) {
                errmsg = (char *) MALLOC(fi.st_size + 1);
                read(fd, errmsg, fi.st_size);
                errmsg[fi.st_size] = '\0';
                close(fd);
                unlink(tfn);
                rm->title = "Command execution failed";
                rm->msg = "The command did not execute. "
                          "Here is the output:<p>\n<pre>\n%s\n</pre>\n";
                rm->arg = errmsg;
                rm->sysmsg = 0;
                return -1;
            }
            else {
                close(fd);
                unlink(tfn);

                rm->title = "Command execution failed";
                rm->msg = "The command didn't execute, and it did not produce "
                          "any output. Run <code>%s</code> from the command "
                          "line and examine the output.\n";
                rm->arg = cmd;
                rm->sysmsg = 0;
                return -1;
            }
        }
        unlink(tfn);
        return 0;
    }
#endif  /* WIN32 */

}



/* This is basically copy_file from the install section, with the error 
 * reporting changed to match the admin stuff.  Since some stuff depends
 * on copy_file being the install version, I'll cheat and call this one
 * cp_file. */
#ifdef XP_UNIX
 
#define COPY_BUFFER_SIZE        4096

void cp_file(char *sfile, char *dfile, int mode)
{
    int sfd, dfd, len;
    struct stat fi;
 
    char copy_buffer[COPY_BUFFER_SIZE];
    unsigned long read_len;

/* Make sure we're in the right umask */
    umask(022);

    if( (sfd = open(sfile, O_RDONLY)) == -1)
        report_error(FILE_ERROR, sfile, "Can't open file for reading.");
 
    fstat(sfd, &fi);
    if(!(S_ISREG(fi.st_mode))) {
        close(sfd);
        return;
    }
    len = fi.st_size;
 
    if( (dfd = open(dfile, O_RDWR | O_CREAT | O_TRUNC, mode)) == -1)
        report_error(FILE_ERROR, dfile, "Can't write to file.");
 
    while(len) {
        read_len = len>COPY_BUFFER_SIZE?COPY_BUFFER_SIZE:len;
 
        if ( (read_len = read(sfd, copy_buffer, read_len)) == -1) {
            report_error(FILE_ERROR, sfile, "Error reading file for copy.");
        }
 
        if ( write(dfd, copy_buffer, read_len) != read_len) {
            report_error(FILE_ERROR, dfile, "Error writing file for copy.");
        }
 
        len -= read_len;
    }
    close(sfd);
    close(dfd);
}

#else /* XP_WIN32 */
void cp_file(char *sfile, char *dfile, int mode)
{
    HANDLE sfd, dfd, MapHandle;
    PCHAR fp;
    DWORD BytesWritten = 0;
    DWORD len;

    if( (sfd = CreateFile(sfile, GENERIC_READ,
    		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
    		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL))
    			== INVALID_HANDLE_VALUE) {
        report_error(FILE_ERROR, "Cannot open file for reading", sfile);
    }
    len = GetFileSize(sfd, NULL);
    if( (dfd = CreateFile(dfile, GENERIC_READ | GENERIC_WRITE,
    	FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,CREATE_ALWAYS,
    	FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        report_error(FILE_ERROR, "Cannot open destination file for writing", 
		dfile);
    }
	if (len == 0)
		return;
    if( (MapHandle = CreateFileMapping(sfd, NULL, PAGE_READONLY,
    		0, 0, NULL)) == NULL) {
        report_error(FILE_ERROR, "Cannot create file mapping", sfile);
    }
    if (!(fp = MapViewOfFile(MapHandle, FILE_MAP_READ, 0, 0, 0))) {
        report_error(FILE_ERROR, "Cannot map file %s", sfile);
    }
    while ( len) {
    	if(!WriteFile(dfd, fp, len, &BytesWritten, NULL)) {
            report_error(FILE_ERROR, "Cannot write new file", dfile);
	    }
    	len -= BytesWritten;
    	fp += BytesWritten;
    }

    CloseHandle(sfd);
    UnmapViewOfFile(fp);
    CloseHandle(MapHandle);
    FlushFileBuffers(dfd);
    CloseHandle(dfd);
}
#endif

int delete_file(char *path)
{
#ifdef XP_UNIX
    return unlink(path);
#else
    return !(DeleteFile(path));
#endif
}

void create_dir(char *dir, int mode)
{
    if ((dir == (char *) NULL) || (strlen(dir) == 0)) {
       report_error(FILE_ERROR, "No directory is specified",
                         "Could not create a necessary directory.");
    }

    if(!file_exists(dir)) {
#ifdef XP_UNIX
        if(mkdir(dir, mode) == -1)  {
#else  /* XP_WIN32 */
        if(!CreateDirectory(dir, NULL)) {
            if (GetLastError() != ERROR_ALREADY_EXISTS)
#endif /* XP_WIN32 */
            report_error(FILE_ERROR, dir,
                         "Could not create a necessary directory.");
        }
    }
}

#ifdef XP_UNIX
SYS_FILE lf;
#elif defined(XP_WIN32)
HANDLE lf;
#endif

char *get_flock_path(void)
{
    char *result="";
    char *port=getenv("SERVER_PORT");
#ifdef XP_UNIX
    result=(char *) MALLOC(strlen("/tmp/lock.%%s.")+strlen(port)+4);
    sprintf(result, "/tmp/lock.%%s.%s", port);
#endif
    return result;
}

/* Open a file with locking, close a file with unlocking. */
FILE *fopen_l(char *path, char *mode)
{
    FILE *f = fopen(path, mode);
    char *lockpath;
    char *sn=get_srvname(0);
    char *flp=FILE_LOCK_PATH;
 
    if(f == NULL) return NULL;
    lockpath=(char *) MALLOC(strlen(sn)+strlen(flp)+16);
    sprintf(lockpath, flp, sn);
#ifdef XP_UNIX
    if( (lf=system_fopenRW(lockpath)) == SYS_ERROR_FD)
        report_error(FILE_ERROR, lockpath, "Could not open file.");
    if(system_flock(lf)==IO_ERROR)
        report_error(FILE_ERROR, lockpath, "Could not lock file.");
#elif defined(XP_WIN32)
    /* Using mutexes because if the CGI program dies, the mutex will be
     * automatically released by the OS for another process to grab.  
     * Semaphores do not have this property; and if the CGI program crashes,
     * the admin server would be effectively crippled.
     */
	if ( (lf = CreateMutex(NULL, 0, lockpath)) == NULL) {
        report_error(FILE_ERROR, lockpath, "Could not create admin mutex.");
	} else {
        if ( WaitForSingleObject(lf, 60*1000) == WAIT_FAILED) {
            report_error(FILE_ERROR, lockpath, "Unable to obtain mutex after 60 seconds.");
        }
	}
#endif /* XP_UNIX */
    return f;
}

void fclose_l(FILE *f)
{
    fclose(f);
#ifdef XP_UNIX
    if(system_ulock(lf)==IO_ERROR)
        report_error(FILE_ERROR, NULL, "Could not unlock lock file.");
    system_fclose(lf);
#elif defined(XP_WIN32)
    if (lf) {
        ReleaseMutex(lf);
        CloseHandle(lf);
    }
#endif /* XP_UNIX */
}

/* Ripped off from the client.  (Sorry, Lou.) */
/* */
/* The magic set of 64 chars in the uuencoded data */
unsigned char uuset[] = {
'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T',
'U','V','W','X','Y','Z','a','b','c','d','e','f','g','h','i','j','k','l','m','n',
'o','p','q','r','s','t','u','v','w','x','y','z','0','1','2','3','4','5','6','7',
'8','9','+','/' };
 
int do_uuencode(unsigned char *src, unsigned char *dst, int srclen)
{
   int  i, r;
   unsigned char *p;
 
/* To uuencode, we snip 8 bits from 3 bytes and store them as
6 bits in 4 bytes.   6*4 == 8*3 (get it?) and 6 bits per byte
yields nice clean bytes
 
It goes like this:
        AAAAAAAA BBBBBBBB CCCCCCCC
turns into the standard set of uuencode ascii chars indexed by numbers:
        00AAAAAA 00AABBBB 00BBBBCC 00CCCCCC
 
Snip-n-shift, snip-n-shift, etc....
 
*/
 
   for (p=dst,i=0; i < srclen; i += 3) {
                /* Do 3 bytes of src */
                register char b0, b1, b2;
 
                b0 = src[0];
                if (i==srclen-1)
                        b1 = b2 = '\0';
                else if (i==srclen-2) {
                        b1 = src[1];
                        b2 = '\0';
                }
                else {
                        b1 = src[1];
                        b2 = src[2];
                }
 
                *p++ = uuset[b0>>2];
                *p++ = uuset[(((b0 & 0x03) << 4) | ((b1 & 0xf0) >> 4))];
                *p++ = uuset[(((b1 & 0x0f) << 2) | ((b2 & 0xc0) >> 6))];
                *p++ = uuset[b2 & 0x3f];
                src += 3;
   }
   *p = 0;      /* terminate the string */
   r = (unsigned char *)p - (unsigned char *)dst;/* remember how many we did */
 
   /* Always do 4-for-3, but if not round threesome, have to go
          clean up the last extra bytes */
 
   for( ; i != srclen; i--)
                *--p = '=';
 
   return r;
}
 
char *alert_word_wrap(char *str, int width, char *linefeed)
{
    char *ans = NULL;
    int counter=0;
    int lsc=0, lsa=0;
    register int strc=0, ansc=0;
    register int x=0;
 
    /* assume worst case */
    ans = (char *) MALLOC((strlen(str)*strlen(linefeed))+32);
 
    for(strc=0, ansc=0; str[strc]; /*none*/)  {
        if(str[strc]=='\n')  {
            counter=0;
            lsc=0, lsa=0;
            for(x=0; linefeed[x]; x++)  {
                ans[ansc++]=linefeed[x];
            }
            strc++;
        }  else if(str[strc]=='\r')  {
            strc++;
        }  else if(str[strc]=='\\')  {
            ans[ansc++]='\\';
            ans[ansc++]=strc++;
        }  else  {
            if(counter==width)  {
                if(lsc && lsa)  {
                    strc=lsc;
                    ansc=lsa;
 
                    counter=0;
                    lsc=0, lsa=0;
                    for(x=0; linefeed[x]; x++)  {
                        ans[ansc++]=linefeed[x];
                    }
                    strc++;
                }  else  {
                /* else, you're a loser, I'm breaking your big word anyway */
                    counter=0;
                    lsc=0, lsa=0;
                    for(x=0; linefeed[x]; x++)  {
                        ans[ansc++]=linefeed[x];
                    }
                    strc++;
                }
            }  else  {
                if(str[strc] == ' ')  {
                    lsc=strc;
                    lsa=ansc;
                }
                ans[ansc++]=str[strc++];
                counter++;
            }
        }
    }
    ans[ansc]='\0';
    return ans;
}

void remove_directory(char *path)
{
    struct stat finfo;
    char **dirlisting;
    register int x=0;
    int stat_good = 0;
    char *fullpath = NULL;

#ifdef XP_UNIX
    stat_good = (lstat(path, &finfo) == -1 ? 0 : 1);
#else /* WIN32 */
    stat_good = (stat(path, &finfo) == -1 ? 0 : 1);
#endif /* XP_UNIX */

    if(!stat_good) return;

    if(S_ISDIR(finfo.st_mode))  {
        dirlisting = list_directory(path,1);
        if(!dirlisting) return;

        for(x=0; dirlisting[x]; x++)  {
            fullpath = (char *) MALLOC(strlen(path) +
                                       strlen(dirlisting[x]) + 4);
            sprintf(fullpath, "%s%c%s", path, FILE_PATHSEP, dirlisting[x]);
#ifdef XP_UNIX
            stat_good = (lstat(fullpath, &finfo) == -1 ? 0 : 1);
#else /* WIN32 */
            stat_good = (stat(fullpath, &finfo) == -1 ? 0 : 1);
#endif /* XP_UNIX */
            if(!stat_good) continue;
            if(S_ISDIR(finfo.st_mode))  {
                remove_directory(fullpath);
            }  else  {
                fprintf(stdout, "<i>Removing file</i> "
                                "<code>%s</code><br>\n", fullpath);
                unlink(fullpath);
            }
            FREE(fullpath);
        }
        fprintf(stdout, "<i>Removing directory</i> "
                        "<code>%s</code><br>\n", path);
#ifdef XP_UNIX
        rmdir(path);
#else /* XP_WIN32 */
                RemoveDirectory(path);
#endif /* XP_WIN32 */
    }  else  {
        fprintf(stdout, "<i>Removing file</i> <code>%s</code><br>\n", path);
        unlink(path);
    }
    return;
}

int str_flag_to_int(char *str_flag) 
{
    if (!str_flag)
        return -1;
    if (!strcmp(str_flag, "1"))
        return 1;
    return 0;
}            

/*
 * get_ip_and_mask - function to take something that may be an IP Address
 *                   and netmaks, and validate it.  It takes two possible
 *
 *     Parmaters:    char   *candidate
 *
 *     Returns NULL if it isn't a valid IP address and mask. It returns
 *     the IP address and mask in the form "iii.iii.iii.iii mmm.mmm.mmm.mmm"
 *     if it is valid. This is in a string dynamicly allocated in this
 *     function.
 *
 *     Processing:  the candidate is assumed to be in one of
 *                  these two formats:
 *
 *     1. "iii.iii.iii.iii" (returns: "iii.iii.iii.iii 255.255.255.255")
 *     2. "iii.iii.iii.iii mmm.mmm.mmm.mmm"
 *     3. "iii.*", "iii.iii.*", or "iii.iii.iii.*"
 *
 *     The rules are:
 *     I.    If it has a space in it, it is assumed to be the delimiter in
 *           format 2.
 *     II.   If it has a "*" in it, it's assumed to be format 3.
 *     III.  If it's in format 3, the net mask returned is:
 *           255.0.0.0, 255.255.0.0, or 255.255.255.0 respectivly,
 *           and parts of the address right of the * is replaced with 0s.
 *     IV.   We use inet_addr on the pieces to validate them.
 *
 * 
 */

char *get_ip_and_mask(char *candidate)
{
    char work[BIG_LINE];

    char *p;
    char *result = NULL;
    int   len;
    int   dots = 0;
    int   i;

    if (candidate && strlen(candidate) < (unsigned) BIG_LINE) {

        if ((p = strchr(candidate, ' '))) {
            len = p-candidate+1;
            memcpy(work, candidate, len);
            work[len] = '\0';
            if (inet_addr(work) != -1) {
                len = strlen(candidate)-strlen(p)-1;
                if (len > 0) {
                    memcpy(work, p+1, len);
                    work[len] = '\0';
                    if (inet_addr(work) != -1) {
                        result = strdup(candidate);
                    }
                }
            }
        }
        else if ((p = strchr(candidate, '*')) &&
                 (p-candidate > 1          )  &&
                 (*(p-1) == '.')               ) {
            memset(work, 0, BIG_LINE);
            for (i=0; candidate[i]!='*'; ++i) {
                if (candidate[i+1] != '*')
                    work[i] = candidate[i];
                if (candidate[i] == '.')
                    ++dots;
            }
            if (dots == 1 || dots == 2 || dots == 3) {
                for (i=0; i<4-dots; ++i) {
                    strcat(work, ".0");
                }
                if (inet_addr(work) != -1) {
                    strcat(work, " ");
                    p = &work[strlen(work)];
                    for (i=0; i<dots; ++i) {
                        if (i==0)
                            strcat(work, "255");
                        else
                            strcat(work, ".255");
                    } 
                    for (i=0; i<4-dots; ++i) {
                        strcat(work, ".0");
                    }
                    if (inet_addr(p) != -1) {
                        result = strdup(work);
                    }
                }
            }
        }
        else {
            if (inet_addr(candidate) != -1) {
                strcpy(work, candidate);
                strcat(work, " 255.255.255.255");
                result = strdup(work);
            }
        }
    }
    else
        result = NULL;

    return result;
}

/* do fgets with a filebuffer *, instead of a File *.  Can't use util_getline
   because it complains if the line is too long.
   It does throw away <CR>s, though.
 */
NSAPI_PUBLIC char *system_gets( char *line, int size, filebuffer *fb )
{
  int	c;
  int	i = 0;

  while ( --size ) {
    switch ( c = filebuf_getc( fb ) ) {
    case IO_EOF:
      line[i] = '\0';
      return i ? line : NULL;
    case LF:
      line[i] = c;
      line[i+1] = '\0';
      return line;	/* got a line, and it fit! */
    case IO_ERROR:
      return NULL;
    case CR:
      ++size;
      break;
    default:
      line[i++] = c;
      break;
    }
  }
  /* if we got here, then we overran the buffer size */
  line[i] = '\0';
  return line;
}

#ifndef WIN32

/* make a zero length file, no matter how long it was before */
NSAPI_PUBLIC int
system_zero( SYS_FILE f )
{
  ftruncate( PR_FileDesc2NativeHandle( f ), 0 );
  return 0;
}

#endif

/***********************************************************************
** FUNCTION:	cookieValue
** DESCRIPTION:
**   Get the current value of the cookie variable
** INPUTS:	var - the name of the cookie variable
**		val - if non-NULL, set the in-memory copy of the var
** OUTPUTS:	None
** RETURN:	NULL if the var doesn't exist, else the value
** SIDE EFFECTS:
**	Eats memory
** RESTRICTIONS:
**	Don't screw around with the returned string, if anything else wants
**	to use it.
** MEMORY:	This is a memory leak, so only use it in CGIs
** ALGORITHM:
**	If it's never been called, build a memory structure of the
**	cookie variables.
**	Look for the passed variable, and return its value, or NULL
***********************************************************************/

NSAPI_PUBLIC char *
cookieValue( char *var, char *val )
{
  static char	**vars = NULL;
  static char	**vals = NULL;
  static int	numVars = -1;
  int		i;

  if ( numVars == -1 ) {	/* first time, init the structure */
    char	*cookie = getenv( "HTTP_COOKIE" );

    if ( cookie && *cookie ) {
      int	len = strlen( cookie );
      int	foundVal = 0;

      cookie = STRDUP( cookie );
      numVars = 0;
      vars = (char **)MALLOC( sizeof( char * ) );
      vals = (char **)MALLOC( sizeof( char * ) );
      vars[0] = cookie;
      for ( i = 0 ; i < len ; ++i ) {
	if ( ( ! foundVal ) && ( cookie[i] == '=' ) ) {	
	  vals[numVars++] = cookie + i + 1;
	  cookie[i] = '\0';
	  foundVal = 1;
	} else if ( ( cookie[i] == ';' ) && ( cookie[i+1] == ' ' ) ) {
	  cookie[i] = '\0';
	  vals = (char **) REALLOC( vals,
				    sizeof( char * ) * ( numVars + 1 ) );
	  vars = (char **) REALLOC( vars,
				    sizeof( char * ) * ( numVars + 1 ) );
	  vars[numVars] = cookie + i + 2;
	  i += 2;
	  foundVal = 0;
	}
      }
    } else {	/* no cookie, no vars */
      numVars = 0;
    }
  }
  for ( i = 0 ; i < numVars ; ++i ) {
    if ( strcmp( vars[i], var ) == 0 ) {
      if ( val ) {
	vals[i] = STRDUP( val );
      } else {
	return vals[i];
      }
    }
  }
  return NULL;
}

/***********************************************************************
** FUNCTION:	jsEscape
** DESCRIPTION:
**   Escape the usual suspects, so the parser javascript parser won't eat them
** INPUTS:	src - the string
** OUTPUTS:	NONE
** RETURN:	A malloced string, containing the escaped src
** SIDE EFFECTS:
**	None, except for more memory being eaten
** RESTRICTIONS:
**	None
** MEMORY:	One Malloc, you should free this if you care
***********************************************************************/

NSAPI_PUBLIC char *
jsEscape( char *src )
{
  int	needsEscaping = 0;
  int	i;
  char	*dest;

  for ( i = 0 ; src[i] ; ++i ) {
    if ( src[i] == '\\' || src[i] == '\'' || src[i] == '"' ) {
      ++needsEscaping;
    }
  }
  dest = (char *)MALLOC( i + needsEscaping + 1 );
  for ( i = 0 ; *src ; ++src ) {
    if ( ( *src == '\\' ) || ( *src == '\'' ) || ( *src == '"' ) ) {
      dest[i++] = '\\';	/* escape */
    }
    dest[i++] = *src;
  }
  dest[i] = '\0';
  return dest;
}

/***********************************************************************
** FUNCTION:	jsPWDialogSrc
** DESCRIPTION:
**   Put the source to the passwordDialog JavaScript function out.
** INPUTS:	inScript - if true, don't put <SCRIPT> stuff out
**		otherJS  - if nonNULL, other javascript to execute
** OUTPUTS:	None
** RETURN:	None
** SIDE EFFECTS:
**	clogs up stdout
** RESTRICTIONS:
**	Don't use this outside of a CGI, or before the Content-type:
** MEMORY:	No memory change
** ALGORITHM:
**	@+@What's really going on?
***********************************************************************/

NSAPI_PUBLIC void
jsPWDialogSrc( int inScript, char *otherJS )
{
  static int	srcSpewed = 0;

  otherJS = otherJS ? otherJS : "";

  if ( ! inScript ) {
    fprintf( stdout, "<SCRIPT LANGUAGE=\""MOCHA_NAME"\">\n" );
  }
  if ( ! srcSpewed ) {
    srcSpewed = 1;
    fprintf( stdout, "function passwordDialog( prompt, element ) {\n"
	     "    var dlg = window.open( '', 'dialog', 'height=60,width=500' );\n"
	     "    dlg.document.write(\n"
	     "        '<form name=f1 onSubmit=\"opener.document.'\n"
	     "        + element + '.value = goo.value; window.close(); "
	     "%s; return false;\">',\n"
	     "        prompt, '<input type=password name=goo></form>' );\n"
	     "    dlg.document.f1.goo.focus();\n"
	     "    dlg.document.close();\n"
	     "}\n", otherJS );
  }
  if ( ! inScript ) {
    fprintf( stdout, "</SCRIPT>\n" );
  }
}

static int adm_initialized=0;

/* Initialize NSPR for all the base functions we use */
NSAPI_PUBLIC int ADM_Init(void)
{
    if(!adm_initialized)  {
        NSPR_INIT("AdminPrograms");
        adm_initialized=1;
    }
    return 0;
}


#ifdef XP_UNIX
/*
 * This function will return the SuiteSpot user id and group id used to
 * recommend that Netscape Servers to run as.  Any line starts with '#'
 * is treated as comment.  It looks for SuiteSpotUser/SuiteSpotGroup
 * name/value pair.
 *
 * It returns  0 when success and allocate storage for user and group.
 *    returns -1 when only SuiteSpot user id is found.
 *    returns -2 when only SuiteSpot group id is found.
 *    returns -3 when NO SuiteSpot user and group is found.
 *    returns -4 when fails to open <server_root>/install/ssusers.conf
 */
NSAPI_PUBLIC int ADM_GetUXSSid(char *sroot, char **user, char **group)
{
    int  foundUser, foundGroup;
    char fn[BIG_LINE];
    char line[BIG_LINE];
    FILE *f;

    foundUser = 0;
    foundGroup = 0;
    *user  = (char *) NULL;
    *group = (char *) NULL;

    sprintf(fn, "%s/install/ssusers.conf", sroot);
    if (f = fopen(fn, "r")) {
       while (fgets(line, sizeof(line), f)) {
	  if (line[0] == '#') {
	     continue;
	  }
          if (!strncmp(line, "SuiteSpotUser", strlen("SuiteSpotUser"))) {
             char *ptr1;
             ptr1 = line + strlen("SuiteSpotUser");
             while ((*ptr1 == '\t') || (*ptr1 == ' ')) {
                ptr1++;
             }
             if ((strlen(ptr1) > 0) && (*user == (char *) NULL)) {
                *user = (char *) MALLOC(strlen(ptr1)+1);
		if (ptr1[strlen(ptr1)-1] == '\n') {
		   ptr1[strlen(ptr1)-1] = '\0';
		}
                strcpy(*user, ptr1);
             }
	     foundUser = 1;
	     continue;
          }
          if (!strncmp(line, "SuiteSpotGroup", strlen("SuiteSpotGroup"))) {
             char *ptr1;
             ptr1 = line + strlen("SuiteSpotGroup");
             while ((*ptr1 == '\t') || (*ptr1 == ' ')) {
                ptr1++;
             }
             if ((strlen(ptr1) > 0) && (*group == (char *) NULL)) {
                *group = (char *) MALLOC(strlen(ptr1)+1);
		if (ptr1[strlen(ptr1)-1] == '\n') {
		   ptr1[strlen(ptr1)-1] = '\0';
		}
                strcpy(*group, ptr1);
             }
             foundGroup = 1;
	     continue;
          }
       }
       fclose(f);
    } else {
       return(-4);
    }

    if (foundUser && foundGroup) {
       return(0);
    } else if (foundUser) {
       return(-1);
    } else if (foundGroup) {
       return(-2);
    } else {
       return(-3);
    }
}
#endif	/* XP_UNIX */
