/*
 * License: GPL (version 2 or any later version) or LGPL (version 2.1 or any later version).
 */

struct bdb_db;
struct bdb_cur;


/* Callbacks - All callbacks must be set before used any other functions */

/* Set callback for the calloc function */
void bdbreader_set_calloc_cb(void* (*calloc_cb)(size_t, size_t));

/* Set callback for the malloc function */
void bdbreader_set_malloc_cb(void* (*malloc_cb)(size_t));

/* Set callback for the realloc function */
void bdbreader_set_realloc_cb(void* (*realloc_cb)(void*, size_t));

/* Set callback for the free function */
void bdbreader_set_free_cb(void (*free_cb)(void **));

/* Set callback for the log function */
void bdbreader_set_log_cb(void (*log_cb)(const char*, ...));

/* Open a database instance and get a db handler */
struct bdb_db *bdbreader_bdb_open(const char *name);

/* Close a db handler */
void bdbreader_bdb_close(struct bdb_db **db);

/* Create a cursor on a db */
struct bdb_cur *bdbreader_cur_open(struct bdb_db *db);

/* Close a cusrsor */
void bdbreader_cur_close(struct bdb_cur **cur);

/* Move cursor to next item. returns -1 if no more records */
int bdbreader_cur_next(struct bdb_cur *cur);

/* Get cursor current status. returns -1 if no more records */
int bdbreader_cur_getval(struct bdb_cur *cur);

/* Position the cursor on the key. return -1 if not found */
int bdbreader_cur_lookup(struct bdb_cur *cur, const unsigned char *key, unsigned int keyl);

/* Position the cursor on smallest key >= key. return -1 if not found */
int bdbreader_cur_lookup_ge(struct bdb_cur *cur, const unsigned char *key, unsigned int keyl);

/* Get cursor current key/data pair. returns -1 if no more records */
int bdbreader_cur_getcurval(struct bdb_cur *cur, void **keyv, unsigned int *keyl, void **datav, unsigned int *datal);
