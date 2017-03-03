/** BEGIN COPYRIGHT BLOCK
 * Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
 * Copyright (c) 2017, Red Hat, Inc
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details. 
 * END COPYRIGHT BLOCK **/

#include <time.h>
#include <pthread.h>

struct b_tree_cb {
    char *name;
    void *inst;
    int64_t (*init)(void **inst);
    int64_t (*add)(void **inst, uint64_t *key, void *value);
    int64_t (*search)(void **inst, uint64_t *key, void **value_out);
    int64_t (*delete)(void **inst, uint64_t *key);
    int64_t (*destroy)(void **inst);
};

