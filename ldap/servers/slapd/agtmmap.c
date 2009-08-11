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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/********************************************************************
 *
 *      agtmmap.c: Memory Map interface for SNMP sub-agent for 
 * 		   Directory Server stats (for UNIX environment).
 *
 *      Revision History:
 *      07/22/97        Created                 Steve Ross
 *
 *
 **********************************************************************/
 

#include "agtmmap.h"
#ifndef  _WIN32
#include <sys/mman.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <time.h>
#include "nt/regparms.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef  _WIN32
agt_mmap_context_t 	mmap_tbl [2] = { {AGT_MAP_UNINIT, -1, (caddr_t) -1}, 
	 				 {AGT_MAP_UNINIT, -1, (caddr_t) -1} };
#else
agt_mmap_context_t 	mmap_tbl[2] = { {AGT_MAP_UNINIT, NULL, (caddr_t) -1, NULL}, 
					 {AGT_MAP_UNINIT, NULL, (caddr_t) -1, NULL} };
#endif /* ! _WIN32 */


/****************************************************************************
 *
 *  agt_mopen_stats () - open and Memory Map the stats file.  agt_mclose_stats() 
 * 			 must be called prior to invoking agt_mopen_stats() again.
 * Inputs: 	
 * 	statsfile ->  Name of stats file including full path or NULL. 
 * 	       	      If NULL, default (slapd.stats) is assumed.
 *	mode      ->  Must be one of O_RDONLY / O_RDWR.
 *		      O_RDWR creates the file if it does not exist.
 * Outputs:
 *	hdl	  ->  Opaque handle to the mapped file. Should be
 *		      passed to a subsequent agt_mupdate_stats() or 
 *		      agt_mread_stats() or agt_mclose_stats() call.
 * Return Values:
 *		      Returns 0 on successfully doing the memmap or error codes 
 * 		      as defined in <errno.h>, otherwise.
 *
 ****************************************************************************/

int 
agt_mopen_stats (char * statsfile, int mode, int *hdl)
{
	caddr_t 	fp;
	char 		*path;
#ifndef  _WIN32
	int 		fd;
        char            *buf;
	int 		err;
	size_t		sz;
	struct stat     fileinfo;
#endif /*  _WIN32 */

	switch (mode)
	{
	     case O_RDONLY:
		  if (mmap_tbl [0].maptype != AGT_MAP_UNINIT)
		  {
			*hdl = 0;
			return (EEXIST); 	/* We already mapped it once */
		  }
		  break;

	     case O_RDWR:
		  if (mmap_tbl [1].maptype != AGT_MAP_UNINIT)
		  {
			*hdl = 1;
			return (EEXIST); 	/* We already mapped it once */
		  }
		  break;
		 
		default:
		  return (EINVAL);  	/* Invalid (mode) parameter */

	} /* end switch */


	if (statsfile != NULL)
	     path = statsfile;
	else
	     path = AGT_STATS_FILE;


#ifndef  _WIN32
	switch (mode)
	{
	     case O_RDONLY:
	           if ( (fd = open (path, O_RDONLY)) < 0 )
	           {
			err = errno;
#if (0)
			fprintf (stderr, "returning errno =%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
	                return (err);
                   }

		   fp = mmap (NULL, sizeof (struct agt_stats_t), PROT_READ, MAP_PRIVATE, fd, 0);

		   if (fp == (caddr_t) -1)
		   {
			err = errno;
			close (fd);
#if (0)
			fprintf (stderr, "returning errno =%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
			return (err);
		   }

		   mmap_tbl [0].maptype = AGT_MAP_READ;
		   mmap_tbl [0].fd 	= fd;
		   mmap_tbl [0].fp 	= fp;
		   *hdl = 0;
#if (0)
		   fprintf (stderr, "%s@%d> opened fp = %d\n",  __FILE__, __LINE__, fp);
#endif
		   return (0);
		   
	     case O_RDWR:
	           fd = open (path, 
			      O_RDWR | O_CREAT, 
			      S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);

	           if ( fd < 0 )
	           {
			err = errno;
#if (0)
			fprintf (stderr, "returning errno =%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
	                return (err);
                   }
		
		   fstat (fd, &fileinfo);

		   sz = sizeof (struct agt_stats_t);

		   if (fileinfo.st_size < sz)
		   {
			   /* Without this we will get segv when we try to read/write later */
			   buf = calloc (1, sz);
			   (void)write (fd, buf, sz);
			   free (buf);
		   }

		   fp = mmap (NULL, sz, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, 0);

		   if (fp == (caddr_t) -1)
		   {
			err = errno;
			close (fd);
#if (0)
			fprintf (stderr, "returning errno =%d from %s(line: %d)\n", err, __FILE__, __LINE__);
#endif
			return (err);
		   }

		   mmap_tbl [1].maptype = AGT_MAP_RDWR;
		   mmap_tbl [1].fd 	= fd;
		   mmap_tbl [1].fp 	= fp;
		   *hdl = 1;
		   return (0);

	} /* end switch */
#else

	switch (mode) {
		case O_RDONLY:
		{
			HANDLE	hFile = NULL;
			HANDLE	hMapFile = NULL;
		
			/* Open existing disk file for read */
			hFile = CreateFile(path, 
						GENERIC_READ | GENERIC_WRITE,
						FILE_SHARE_READ | FILE_SHARE_WRITE, 
						NULL, 
						OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL, 
						NULL);
			if ( hFile == INVALID_HANDLE_VALUE || hFile == NULL ) return GetLastError();

			/* Create mapped file handle for reading */
			hMapFile = CreateFileMapping( hFile, NULL, PAGE_READONLY, 0,
						sizeof(struct agt_stats_t),
						NULL);
			if ( hMapFile == NULL ) {
				CloseHandle( hFile );
				return GetLastError();
			}

				/* Create addr ptr to the start of the file */
			fp = (caddr_t) MapViewOfFileEx( hMapFile, FILE_MAP_READ, 0, 0,
					sizeof(struct agt_stats_t), NULL );
			if ( fp == NULL ) {
				CloseHandle( hMapFile );
				CloseHandle( hFile );
				return GetLastError();
			}

			/* Fill in info on this opaque handle */
			mmap_tbl[0].maptype = AGT_MAP_READ;
			mmap_tbl[0].fd = hFile;
			mmap_tbl[0].fp = fp;
			mmap_tbl[0].mfh = hMapFile;
			*hdl = 0;
			return 0;
		}
		
		case O_RDWR:
		{
		
			HANDLE	hFile = NULL;
			HANDLE	hMapFile = NULL;
		
			hFile = CreateFile( path, 
						GENERIC_WRITE | GENERIC_READ,
						FILE_SHARE_READ | FILE_SHARE_WRITE, 
						NULL, 
						OPEN_ALWAYS,
						FILE_ATTRIBUTE_NORMAL, 
						NULL );
			if ( hFile == INVALID_HANDLE_VALUE || hFile == NULL ) return GetLastError();

			/* Create mapped file handle for reading */
			hMapFile = CreateFileMapping( hFile, NULL, PAGE_READWRITE, 0,
						sizeof(struct agt_stats_t),
						NULL );
			if ( hMapFile == NULL ) {
				CloseHandle( hFile );
				return GetLastError();
			}

				/* Create addr ptr to the start of the file */
			fp = (caddr_t) MapViewOfFileEx( hMapFile, FILE_MAP_ALL_ACCESS, 0, 0,
					sizeof(struct agt_stats_t), NULL );
			if ( fp == NULL ) {
				CloseHandle( hMapFile );
				CloseHandle( hFile );
				return GetLastError();
			}

			mmap_tbl[1].maptype = AGT_MAP_RDWR;
			mmap_tbl[1].fd = hFile;
			mmap_tbl[1].fp = fp;
			mmap_tbl[1].mfh = hMapFile;
			*hdl = 1;
			return 0;

		}
		

	}

#endif /* !__WINNT__ */

return 0;

}  /* agt_mopen_stats () */


/****************************************************************************
 *
 *  agt_mclose_stats () - Close the Memory Map'ed the stats file.
 *
 *
 * Inputs: 	
 *	hdl	  ->  Opaque handle to the mapped file. Should be have been 
 *		      returned by an earlier call to agt_mopen_stats().
 *		      
 * Outputs:	      <NONE>
 *		      
 * Return Values:
 *		      Returns 0 on normal completion or error codes 
 * 		      as defined in <errno.h>, otherwise.
 *
 ****************************************************************************/
int 
agt_mclose_stats (int hdl)
{
	if ( (hdl > 1) || (hdl < 0) )
	{
		return (EINVAL); 	/* Inavlid handle */
	}

	if (mmap_tbl [hdl].maptype == AGT_MAP_UNINIT)
	     return (0);

	if (mmap_tbl [hdl].fp > (caddr_t) 0)
	{
#ifndef  _WIN32
		munmap (mmap_tbl [hdl].fp, sizeof (struct agt_stats_t));
		mmap_tbl [hdl].fp = (caddr_t) -1;
		close (mmap_tbl [hdl].fd);
		mmap_tbl [hdl].fd = -1;
#else
		BOOL	bUnmapped;

		bUnmapped = UnmapViewOfFile( mmap_tbl[hdl].fp );
		if ( mmap_tbl[hdl].mfh ) CloseHandle( mmap_tbl[hdl].mfh );
		if ( mmap_tbl[hdl].fd ) CloseHandle( mmap_tbl[hdl].fd );

		mmap_tbl[hdl].fp = (caddr_t) -1;
		mmap_tbl[hdl].mfh = NULL;
		mmap_tbl[hdl].fd = NULL;
#endif /* ! _WIN32 */
		mmap_tbl [hdl].maptype = AGT_MAP_UNINIT;
		return (0);
	}

	return EINVAL;
}  /* agt_mclose_stats () */


int
agt_mread_stats (int hdl, struct hdr_stats_t *pHdrInfo, struct ops_stats_t *pDsOpsTbl,
                 struct entries_stats_t *pDsEntTbl) {
    struct agt_stats_t    *pfile_stats;
                                                                                                      
    if ( (hdl > 1) || (hdl < 0) ) {
        return (EINVAL);
    }
                                                                                                      
    if ((mmap_tbl [hdl].maptype != AGT_MAP_READ) && (mmap_tbl [hdl].maptype != AGT_MAP_RDWR)) {
        return (EINVAL);        /* Inavlid handle */
    }
                                                                                                      
    if (mmap_tbl [hdl].fp <= (caddr_t) 0) {
            return (EFAULT);        /* Something got corrupted */
    }
                                                                                                      
    pfile_stats = (struct agt_stats_t *) (mmap_tbl [hdl].fp);
                                                                                                      
    if (pHdrInfo != NULL) {
        /* Header */
        pHdrInfo->restarted                = pfile_stats->hdr_stats.restarted;
        pHdrInfo->startTime                = pfile_stats->hdr_stats.startTime;
        pHdrInfo->updateTime               = pfile_stats->hdr_stats.updateTime;
        strncpy(pHdrInfo->dsVersion, pfile_stats->hdr_stats.dsVersion,
               SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsVersion[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsName, pfile_stats->hdr_stats.dsName,
               SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsName[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsDescription, pfile_stats->hdr_stats.dsDescription,
               SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsDescription[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsOrganization, pfile_stats->hdr_stats.dsOrganization,
               SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsOrganization[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsLocation, pfile_stats->hdr_stats.dsLocation,
               SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsLocation[SNMP_FIELD_LENGTH - 1] = (char)0;
        strncpy(pHdrInfo->dsContact, pfile_stats->hdr_stats.dsContact,
               SNMP_FIELD_LENGTH - 1);
        pHdrInfo->dsContact[SNMP_FIELD_LENGTH - 1] = (char)0;
    }

    if (pDsOpsTbl != NULL) {
        /* Ops Table */
        pDsOpsTbl->dsAnonymousBinds        = pfile_stats->ops_stats.dsAnonymousBinds;
        pDsOpsTbl->dsUnAuthBinds           = pfile_stats->ops_stats.dsUnAuthBinds;
        pDsOpsTbl->dsSimpleAuthBinds       = pfile_stats->ops_stats.dsSimpleAuthBinds;
        pDsOpsTbl->dsStrongAuthBinds       = pfile_stats->ops_stats.dsStrongAuthBinds;
        pDsOpsTbl->dsBindSecurityErrors    = pfile_stats->ops_stats.dsBindSecurityErrors;
        pDsOpsTbl->dsInOps                 = pfile_stats->ops_stats.dsInOps;
        pDsOpsTbl->dsReadOps               = pfile_stats->ops_stats.dsReadOps;
        pDsOpsTbl->dsCompareOps            = pfile_stats->ops_stats.dsCompareOps;
        pDsOpsTbl->dsAddEntryOps           = pfile_stats->ops_stats.dsAddEntryOps;
        pDsOpsTbl->dsRemoveEntryOps        = pfile_stats->ops_stats.dsRemoveEntryOps;
        pDsOpsTbl->dsModifyEntryOps        = pfile_stats->ops_stats.dsModifyEntryOps;
        pDsOpsTbl->dsModifyRDNOps          = pfile_stats->ops_stats.dsModifyRDNOps;
        pDsOpsTbl->dsListOps               = pfile_stats->ops_stats.dsListOps;
        pDsOpsTbl->dsSearchOps             = pfile_stats->ops_stats.dsSearchOps;
        pDsOpsTbl->dsOneLevelSearchOps     = pfile_stats->ops_stats.dsOneLevelSearchOps;
        pDsOpsTbl->dsWholeSubtreeSearchOps = pfile_stats->ops_stats.dsWholeSubtreeSearchOps;
        pDsOpsTbl->dsReferrals             = pfile_stats->ops_stats.dsReferrals;
        pDsOpsTbl->dsChainings             = pfile_stats->ops_stats.dsChainings;
        pDsOpsTbl->dsSecurityErrors        = pfile_stats->ops_stats.dsSecurityErrors;
        pDsOpsTbl->dsErrors                = pfile_stats->ops_stats.dsErrors;
    }

    if (pDsEntTbl != NULL) {
    /* Entries Table */
        pDsEntTbl->dsMasterEntries = pfile_stats->entries_stats.dsMasterEntries;
        pDsEntTbl->dsCopyEntries   = pfile_stats->entries_stats.dsCopyEntries;
        pDsEntTbl->dsCacheEntries  = pfile_stats->entries_stats.dsCacheEntries;
        pDsEntTbl->dsCacheHits     = pfile_stats->entries_stats.dsCacheHits;
        pDsEntTbl->dsSlaveHits     = pfile_stats->entries_stats.dsSlaveHits;
    }
                                                                                                      
    return (0);
}

