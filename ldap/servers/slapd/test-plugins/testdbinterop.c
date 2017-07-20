/* --- BEGIN COPYRIGHT BLOCK ---
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * --- END COPYRIGHT BLOCK --- */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdio.h>
#include "testdbinterop.h"
#include "slapi-plugin.h"

#define DB_PLUGIN_NAME "nullsuffix-preop"

static PRLock *db_lock = NULL;

#ifdef USE_BDB
#include "db.h"
#define DATABASE "access.db"
static int number_of_keys = 100;
static int key_buffer_size = 8000;

#define DB_OPEN(db, txnid, file, database, type, flags, mode) \
    (db)->open((db), (txnid), (file), (database), (type), (flags), (mode))

DB *dbp = NULL;

void
create_db()
{
    int ret;
    DBT key, data;

    if ((ret = db_create(&dbp, NULL, 0)) != 0) {
        fprintf(stderr, "db_create: %s\n", db_strerror(ret));
        exit(1);
    }
}

void
make_key(DBT *key)
{
    char *key_string = (char *)(key->data);
    unsigned int seed = (unsigned int)time((time_t *)0);
    long int key_long = slapi_rand_r(&seed) % number_of_keys;
    sprintf(key_string, "key%ld", key_long);
    slapi_log_err(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME, "generated key: %s\n", key_string);
    key->size = strlen(key_string);
}


void
db_put_dn(char *data_dn)
{
    int ret;
    DBT key = {0};
    DBT data = {0};

    if (db_lock == NULL) {
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
        slapi_log_err(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME, "db: %s: key stored.\n", (char *)key.data);
        break;
    case DB_KEYEXIST:
        slapi_log_err(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME, "db: %s: key previously stored.\n",
                      (char *)key.data);
        break;
    default:
        dbp->err(dbp, ret, "DB->put");
        goto err;
    }

err:
    if (ret) {
        slapi_log_err(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME, "db: Error detected in db_put \n");
    }
    free(key.data);
    if (dbp) {
        dbp->close(dbp, 0);
        dbp = NULL;
    }
    PR_Unlock(db_lock);
}
#else /* USE_BDB */
#include <string.h>

#define DATABASE "/tmp/testdbinterop.db"
#define DATABASE_BACK "/tmp/testdbinterop.db.bak"

void
db_put_dn(char *data_dn)
{
    int ret;
    char *db_path = DATABASE;
    char *db_path_bak = DATABASE_BACK;
    PRFileInfo64 info;
    PRFileDesc *prfd;
    PRInt32 data_sz;
    char *data_dnp = NULL;

    if (db_lock == NULL) {
        db_lock = PR_NewLock();
    }
    PR_Lock(db_lock);
    /* if db_path is a directory, rename it */
    ret = PR_GetFileInfo64(db_path, &info);
    if (PR_SUCCESS == ret) {
        if (PR_FILE_DIRECTORY == info.type) { /* directory */
            ret = PR_GetFileInfo64(db_path_bak, &info);
            if (PR_SUCCESS == ret) {
                if (PR_FILE_DIRECTORY != info.type) { /* not a directory */
                    PR_Delete(db_path_bak);
                }
            }
            PR_Rename(db_path, db_path_bak);
        }
    }

    /* open a file */
    if ((prfd = PR_Open(db_path, PR_RDWR | PR_CREATE_FILE | PR_APPEND, 0600)) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, DB_PLUGIN_NAME,
                      "db: Could not open file \"%s\" for read/write; %d (%s)\n",
                      db_path, PR_GetError(), slapd_pr_strerror(PR_GetError()));
        return;
    }

    data_dnp = slapi_ch_smprintf("%s\n", data_dn);
    data_sz = (PRInt32)strlen(data_dnp);

    ret = PR_Write(prfd, data_dnp, data_sz);
    if (ret == data_sz) {
        slapi_log_err(SLAPI_LOG_PLUGIN, DB_PLUGIN_NAME,
                      "db: %s: key stored.\n", data_dn);
        ret = 0;
    } else {
        slapi_log_err(SLAPI_LOG_ERR, DB_PLUGIN_NAME,
                      "db: Failed to store key \"%s\"; %d (%s)\n",
                      data_dn, PR_GetError(), slapd_pr_strerror(PR_GetError()));
        ret = 1;
    }
    if (ret) {
        slapi_log_err(SLAPI_LOG_ERR, DB_PLUGIN_NAME,
                      "db: Error detected in db_put_dn \n");
    }
    slapi_ch_free_string(&data_dnp);
    PR_Close(prfd);
    PR_Unlock(db_lock);
    return;
}
#endif
