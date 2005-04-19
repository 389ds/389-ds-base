/* --- BEGIN COPYRIGHT BLOCK ---
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
 * --- END COPYRIGHT BLOCK --- */

#include <sys/types.h>
#include <stdio.h>
#include "db.h"
#include "testdbinterop.h"
#include "slapi-plugin.h"

#define DATABASE "access.db"
static int number_of_keys=100;
static int key_buffer_size = 8000;

#define DB_PLUGIN_NAME "nullsuffix-preop"

static  PRLock *db_lock=NULL; 

#if 1000*DB_VERSION_MAJOR + 100*DB_VERSION_MINOR >= 4100
#define DB_OPEN(db, txnid, file, database, type, flags, mode) \
    (db)->open((db), (txnid), (file), (database), (type), (flags), (mode))
#else
    (db)->open((db), (file), (database), (type), (flags), (mode))
#endif


DB *dbp=NULL;

void
create_db()
{
    int ret;
    DBT key, data;

    if ((ret = db_create(&dbp, NULL, 0)) != 0) {
	    fprintf(stderr, "db_create: %s\n", db_strerror(ret));
	    exit (1);
    }

}

void make_key(DBT *key)
{
    char *key_string = (char*)(key->data);
    unsigned int seed = (unsigned int)time( (time_t*) 0);
    long int key_long = slapi_rand_r(&seed) % number_of_keys;
    sprintf(key_string,"key%ld",key_long);
    slapi_log_error(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME,"generated key: %s\n", key_string);
    key->size = strlen(key_string);
}


void
db_put_dn(char *data_dn)
{
    int ret;
    DBT key = {0};
    DBT data = {0};

	if(db_lock == NULL){
		db_lock = PR_NewLock();
	}
	PR_Lock(db_lock);
    create_db();
    
    if ((ret = DB_OPEN(dbp, NULL, DATABASE, NULL, DB_BTREE, DB_CREATE, 0664)) != 0) {
        dbp->err(dbp, ret, "%s", DATABASE);
    } 
    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));
    

    key.data = (char *)malloc(key_buffer_size);
    /* make_key will set up the key and the data */ 
    make_key(&key);

    data.data = slapi_ch_strdup(data_dn);
    data.size = strlen(data_dn);


    switch (ret =
		dbp->put(dbp, NULL, &key, &data, DB_NOOVERWRITE)) {
	case 0: 
	   	slapi_log_error(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME, "db: %s: key stored.\n", (char *)key.data);
	break;
	case DB_KEYEXIST:
		slapi_log_error(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME, "db: %s: key previously stored.\n",
			(char *)key.data);
	break;
	default:
	   	dbp->err(dbp, ret, "DB->put");
	goto err;
    }

    err:
    if(ret){
		slapi_log_error(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME, "db: Error detected in db_put \n");
    }
	free(key.data);
	if (dbp){
		dbp->close(dbp,0);
		dbp=NULL;
	}
	PR_Unlock(db_lock);

}
