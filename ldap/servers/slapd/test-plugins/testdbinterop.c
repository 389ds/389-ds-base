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
