/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
 
#include "back-ldbm.h"

static void
mk_dbversion_fullpath(struct ldbminfo *li, const char *directory, char *filename)
{
    if (li)
    {
        if (is_fullpath((char *)directory))
        {
            sprintf(filename, "%s/%s", directory, DBVERSION_FILENAME);
        }
        else
        {
            char *home_dir = dblayer_get_home_dir(li, NULL);
            /* if relpath, nsslapd-dbhome_directory should be set */
            sprintf(filename,"%s/%s/%s", home_dir,directory,DBVERSION_FILENAME);
        }
    }
    else
    {
        sprintf(filename, "%s/%s", directory, DBVERSION_FILENAME);
    }
}

/*
 *  Function: dbversion_write
 *
 *  Returns: returns 0 on success, -1 on failure
 *  
 *  Description: This function writes the DB version file.
 */
int
dbversion_write(struct ldbminfo *li, const char *directory,
                const char *dataversion)
{
    char filename[ MAXPATHLEN*2 ];
    PRFileDesc *prfd;
    int rc = 0;

    if (!is_fullpath((char *)directory)) {
        rc = -1;
        return rc;
    }
        
    mk_dbversion_fullpath(li, directory, filename);
  
    /* Open the file */
    if (( prfd = PR_Open( filename, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE,
                          SLAPD_DEFAULT_FILE_MODE  )) == NULL )
    {
        LDAPDebug( LDAP_DEBUG_ANY, "Could not open file \"%s\" for writing "
                   SLAPI_COMPONENT_NAME_NSPR " %d (%s)\n",
                   filename, PR_GetError(), slapd_pr_strerror(PR_GetError()) );
        rc= -1;
    }
    else
    {
        /* Write the file */
        PRInt32    len;
        char buf[ LDBM_VERSION_MAXBUF ];
        /* recognize the difference between an old/new database regarding idl
         * (406922) */
        if (idl_get_idl_new())
        {
#if defined(USE_NEW_IDL)
            sprintf( buf, "%s\n", LDBM_VERSION );
#else
            sprintf( buf, "%s\n", LDBM_VERSION_NEW );
#endif
        }
        else
        {
#if defined(USE_NEW_IDL)
            sprintf( buf, "%s\n", LDBM_VERSION_OLD );
#else
            sprintf( buf, "%s\n", LDBM_VERSION );
#endif
        }
        len = strlen( buf );
        if ( slapi_write_buffer( prfd, buf, len ) != len )
        {
            LDAPDebug( LDAP_DEBUG_ANY, "Could not write to file \"%s\"\n", filename, 0, 0 );
            rc= -1;
        }
        if(rc==0 && dataversion!=NULL)
        {
            sprintf( buf, "%s\n", dataversion );
            len = strlen( buf );
            if ( slapi_write_buffer( prfd, buf, len ) != len )
            {
                LDAPDebug( LDAP_DEBUG_ANY, "Could not write to file \"%s\"\n", filename, 0, 0 );
                rc= -1;
            }
        }
        (void)PR_Close( prfd );
    }
    return rc;
}

/*
 *  Function: dbversion_read
 *
 *  Returns: returns 0 on success, -1 on failure
 *  
 *  Description: This function reads the DB version file.
 */
int
dbversion_read(struct ldbminfo *li, const char *directory,
               char *ldbmversion, char *dataversion)
{
    char filename[ MAXPATHLEN*2 ];
    PRFileDesc *prfd;
    int rc = -1;
    char * iter = NULL;

    if (!is_fullpath((char *)directory)) {
        rc = -1;
        return rc;
    }

    mk_dbversion_fullpath(li, directory, filename);
    
    ldbmversion[0]= '\0';
    dataversion[0]= '\0';
  
    /* Open the file */
    if (( prfd = PR_Open( filename, PR_RDONLY, SLAPD_DEFAULT_FILE_MODE  )) ==
          NULL )
    {
        /* File missing... we are probably creating a new database. */
    }
    else
    {
        char buf[LDBM_VERSION_MAXBUF];
        PRInt32 nr = slapi_read_buffer( prfd, buf,
                    (PRInt32)LDBM_VERSION_MAXBUF-1 );
        if ( nr > 0 && nr != (PRInt32)LDBM_VERSION_MAXBUF-1 )
        {
            char *t;
            buf[nr]= '\0';
            t= ldap_utf8strtok_r(buf,"\n", &iter);
            if(t!=NULL)
            {
                strcpy(ldbmversion,t);
                t= ldap_utf8strtok_r(NULL,"\n", &iter);
                if(t!=NULL && t[0]!='\0')
                {
                    strcpy(dataversion,t);
                }
            }
        }
        (void)PR_Close( prfd );
        rc= 0;
    }
    return rc;
}


/*
 *  Function: dbversion_exists
 *
 *  Returns: 1 for exists, 0 for not.
 *  
 *  Description: This function checks if the DB version file exists.
 */
int
dbversion_exists(struct ldbminfo *li, const char *directory)
{
    char filename[ MAXPATHLEN*2 ];
    PRFileDesc *prfd;

    mk_dbversion_fullpath(li, directory, filename);

    if (( prfd = PR_Open( filename, PR_RDONLY, SLAPD_DEFAULT_FILE_MODE )) ==
         NULL )
    {
        return 0;
    }
    (void)PR_Close( prfd );
    return 1;
}

