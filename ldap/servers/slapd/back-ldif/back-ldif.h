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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/* 
 *  File: back-ldif.h
 *
 *  Description: This header file contains the definitions
 *  for the data structures used in the ldif backend database
 */

#define	SLAPD_LOGGING	1

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

/* include NSPR header files */
#include "prlock.h"

#include "ldaplog.h"
#include "portable.h"
#include "dirver.h"
#include "slap.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/socket.h>
#endif /* _WIN32 */

/*Defines*/
#define LDIF_DB_ADD     0
#define LDIF_DB_DELETE  1
#define LDIF_DB_REPLACE 2


/*This structure basically allows the entries to be linked listed*/ 
struct ldif_entry{
  Slapi_Entry       *lde_e;      /*ptr to the Entry datatype, but you knew that*/
  struct ldif_entry *next; /*ptr to the next list element.*/
};
typedef struct ldif_entry ldif_Entry;


/*Holds the data from the ldif file*/
struct ldif {
  long            ldif_n;        /*The number of entries in the database*/
  long            ldif_tries;    /*The number of accesses to the database*/
  long            ldif_hits;     /*The number of succesful searches to the db*/
  char            *ldif_file;    /*From where we read the ldif data*/
  PRLock	  *ldif_lock;     /*Write & read lock.(a simple locking model)*/
  ldif_Entry      *ldif_entries; /*The linked list of entries*/
};
typedef struct ldif LDIF;


/*Prototypes*/
  int ldif_back_modify( Slapi_PBlock * );
  int update_db(Slapi_PBlock *, LDIF *, ldif_Entry *, ldif_Entry *, int); 
  int db2disk(Slapi_PBlock *, LDIF *);
  void ldifentry_free(ldif_Entry *);
  ldif_Entry *ldifentry_dup(ldif_Entry *);
  ldif_Entry *ldif_find_entry(Slapi_PBlock *, LDIF *, char *, ldif_Entry **);
  int apply_mods( Slapi_Entry *, LDAPMod ** );
  
  int ldif_back_add( Slapi_PBlock *);
  ldif_Entry *ldifentry_init(Slapi_Entry *);
  int ldif_back_config( Slapi_PBlock *);
  static char * ldif_read_one_record( FILE *);
  int ldif_back_delete( Slapi_PBlock *);
  int has_children(LDIF *, ldif_Entry *);
  int ldif_back_init( Slapi_PBlock *);
  
  int ldif_back_search( Slapi_PBlock * );
  
  int ldif_back_modrdn( Slapi_PBlock * );
  static int rdn2typeval(char *, char **, struct berval *);
  void add_mod( LDAPMod ***, int, char *, struct berval ** );
  
  int ldif_back_bind( Slapi_PBlock * );
  int ldif_back_unbind( Slapi_PBlock * );
  
  int ldif_back_start( Slapi_PBlock * );
  void ldif_back_close( Slapi_PBlock * );
  void ldif_back_flush( Slapi_PBlock * );
  void ldif_free_db(LDIF *);
  
  int ldif_back_compare( Slapi_PBlock * );

  char * get_monitordn(Slapi_PBlock * );
  int ldif_back_monitor_info( Slapi_PBlock *pb, LDIF *db);
