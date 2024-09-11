#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <robdb.h>

typedef struct {
   void *data;
   unsigned int len;
} DATA;


void print_record(const DATA *key, const DATA *data)
{
    unsigned int id = 0;
    if (4 == key->len) {
        unsigned char *pt = key->data;
        /* In test.db the key is a big endian 4 bytes integer */
        id = pt[3] + (pt[2] << 8) + (pt[1] << 16) + (pt[0] << 24);
        printf("Key: %3d Data:\n%s\n", id, (char*)data->data);
    } else {
       printf("ERROR: Unexpected record key size\n");
    }
}

void mylog(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    printf("bdbreader:" );
    vprintf(msg, ap);
    va_end(ap);
}


int main()
{
    struct bdb_db *db = NULL;
    struct bdb_cur *cur = NULL;
    DATA key = {0};
    DATA data = {0};
    int rc = 0;
    char keybuff[6];
    int fail = 0;
    int count = 0;
    int expected_count = 14;

    printf("HERE[%d]\n",__LINE__);

    /* Initialize all callbacks */
    bdbreader_set_calloc_cb(calloc);
    bdbreader_set_malloc_cb(malloc);
    bdbreader_set_realloc_cb(realloc);
    bdbreader_set_free_cb(free);
    bdbreader_set_log_cb(mylog);

    db = bdbreader_bdb_open("test.db");
    if (db == NULL) {
        perror("Failed to open test.db");
        exit(1);
    }
    cur = bdbreader_cur_open(db);
    if (cur == NULL) {
        perror("Failed to open cursor");
        exit(1);
    }

    printf(" ********* Dump the dtabase content: *******\n");

    do {
        rc = bdbreader_cur_next(cur);
        if (rc !=0) {
            break;
        }
        rc = bdbreader_cur_getcurval(cur, &key.data, &key.len, &data.data, &data.len);
        if (rc == 0) {
            print_record(&key, &data);
            count++;
        }
    } while (rc == 0);

    printf("Found %d/%d records\n\n", count, expected_count);
    if (count != expected_count) {
        fail = 1;
    }

    printf(" ********* Test lookup: *******\n");

    keybuff[0] = 0;
    keybuff[1] = 0;
    keybuff[2] = 0;
    keybuff[3] = 7;
    keybuff[4] = '@';
    key.data = keybuff;
    key.len = 5;

    printf(" Looking for key == 7@ ... Should not find it.\n");
    rc = bdbreader_cur_lookup(cur,  key.data, key.len);
    if (rc == 0) {
        printf("ERROR: key == 7@ unexpectedly found.\n");
        fail = 1;
    } else {
        printf("OK: key == 7@ is not found (as expected).\n");
    }

    printf(" Looking for key >= 7@ ... Should not find record with key == 8.\n");
    rc = bdbreader_cur_lookup_ge(cur,  key.data, key.len);
    if (rc == 0) {
        rc = bdbreader_cur_getcurval(cur, &key.data, &key.len, &data.data, &data.len);
        if (rc != 0) {
            printf("ERROR: key >= 7@ : unable to get the record.\n");
            fail = 1;
        } else {
            char *ptid = key.data;
            if (ptid[3] != 8) {
                printf("ERROR: key >= 7@ : found record %d instead of 8.\n", ptid[3]);
                fail = 1;
            } else {
                printf("OK: key >= 7@ : found record 8 (as expected).\n");
            }
        }
    } else {
        printf("ERROR: key >= 7@ is not found.\n");
        fail = 1;
    }

    key.data = keybuff;
    key.len = 4;
    printf(" Looking for key == 7 ... Should find it.\n");
    rc = bdbreader_cur_lookup(cur,  key.data, key.len);
    if (rc == 0) {
        rc = bdbreader_cur_getcurval(cur, &key.data, &key.len, &data.data, &data.len);
        if (rc != 0) {
            printf("ERROR: key == 7 : unable to get the record.\n");
            fail = 1;
        } else {
            char *ptid = key.data;
            if (ptid[3] != 7) {
                printf("ERROR: key == 7 : found record %d instead of 7.\n", ptid[3]);
                fail = 1;
            } else {
                printf("OK: key == 7 : found record 7 (as expected).\n");
            }
        }
    } else {
        printf("ERROR: key == 7 is not found.\n");
        fail = 1;
    }

    bdbreader_cur_close(&cur);
    bdbreader_bdb_close(&db);
    printf("TEST: %s\n", fail ? "FAIL":"PASS");

    return fail;
}

#if 0
/* Get cursor current status. returns -1 if no more records */
int bdbreader_cur_getval(struct bdb_cur *cur);

/* Position the cursor on the key. return -1 if not found */
int bdbreader_cur_lookup(struct bdb_cur *cur, const unsigned char *key, unsigned int keyl);

/* Position the cursor on smallest key >= key. return -1 if not found */
int bdbreader_cur_lookup_ge(struct bdb_cur *cur, const unsigned char *key, unsigned int keyl);
#endif

