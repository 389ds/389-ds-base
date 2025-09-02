/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* back-ldbm.h - ldap ldbm back-end header file */

#ifndef _BACK_LDBM_H_
#define _BACK_LDBM_H_

#if defined(HPUX11) || defined(OS_solaris) || defined(linux)
/* built-in 64-bit file I/O support */
#if ! defined(__LP64__)
/* But not on 64-bit arch: It is needless and build fails since gcc 13.2.1-4 */
#define DB_USE_64LFS
#endif
#endif

/* needed by at least HPUX and Solaris, to define off64_t */
#ifdef DB_USE_64LFS
#if !defined(_LARGEFILE64_SOURCE)
#define _LARGEFILE64_SOURCE
#endif
#endif

/* Provides our int types and platform specific requirements. */
#include <slapi_pal.h>

/* A bunch of random system headers taken from all the source files, no source file should #include
   any system headers now */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "prio.h"  /* for PR_OpenDir etc */
#include "prlog.h" /* for PR_ASSERT */
/* The following cruft is for ldif2db only */
#include <unistd.h> /* write/close (ldbm2ldif_write) */
#include <fcntl.h>
#include <time.h>
/* And this cruft is from nextid.c */
#include <sys/param.h>
#include <limits.h> /* Used in search.c (why?) */

/* for MAXPATHLEN */
#include <sys/param.h>
#define MKDIR(path, mode) mkdir((path), (mode))

#ifdef HPUX11
#define __BIT_TYPES_DEFINED__
typedef unsigned char u_int8_t;
typedef unsigned int u_int32_t;
typedef unsigned short u_int16_t;
#endif

#define dptr  data
#define dsize size

#define ID2ENTRY "id2entry" /* main db file name: ID2ENTRY+LDBM_SUFFIX */

#define MEGABYTE (1024 * 1024)
#define GIGABYTE (1024 * MEGABYTE)

/* include NSPR header files */
#include "nspr.h"
#include "plhash.h"

#include "slap.h"
#include "slapi-plugin.h"
#include "slapi-private.h"
#include "dbimpl.h"
#include "avl.h"
#include "portable.h"
#include "proto-slap.h"

/* We should only change the LDBM_VERSION when the format of the db files
 * is changing in some (possibly incompatible) way -- so we can detect and
 * treat older ldbm versions.  Thus, f.e., DS4.1 will still use the same
 * LDBM_VERSION as 4.0 and so on...
 * Don't make the length of LDBM_VERSION longer than LDBM_VERSION_MAXBUF - 1
 */
#define LDBM_VERSION_MAXBUF 64
#define LDBM_DATABASE_TYPE_NAME "ldbm database"
/*
 * 232050: Change format of DBVERSION and guardian files
 * new format:
 * implementation/version/server backend plugin name[/other tag][/other tag]....
 * For example:
 * bdb/4.2/libback-ldbm/newidl
 * This indicates that the files use Berkeley DB version 4.2, they are used
 * by the server libback-ldbm database plugin, and the index files use the
 * newidl format.
 * Starting from DS7.2
 */
#define BE_CHANGELOG_FILE     "replication_changelog"

#define BDB_IMPL              "bdb"
#define BDB_BACKEND           "libback-ldbm" /* This backend plugin */
#define BDB_NEWIDL            "newidl"       /* new idl format */
#define BDB_RDNFORMAT         "rdn-format"   /* Subtree rename enabled */
#define BDB_RDNFORMAT_VERSION "3"            /* rdn-format version (by default, 0) */
#define BDB_DNFORMAT          "dn-4514"      /* DN format RFC 4514 compliant */
#define BDB_DNFORMAT_VERSION  "1"            /* DN format version */
#define BDB_CL_FILENAME       "replication_changelog.db"

#define LMDB_IMPL             "mdb"

#define DBVERSION_NEWIDL 0x1
#define DBVERSION_RDNFORMAT 0x2
#define DBVERSION_DNFORMAT 0x4
#define DBVERSION_ALL 0xffffffff

/*
 * While we support both new and old idl index,
 * we distinguish them by the following 2 macros.
 * When we drop the old idl code, we eliminate LDBM_VERSION_OLD.
 * bug #604922
 */
#define LDBM_VERSION_BASE "Netscape-ldbm/"
#define LDBM_VERSION      "Netscape-ldbm/7.0"         /* db42: new idl -> old */
#define LDBM_VERSION_NEW  "Netscape-ldbm/7.0_NEW"     /* db42: new idl */
#define LDBM_VERSION_OLD  "Netscape-ldbm/7.0_CLASSIC" /* db42: old idl */
#define LDBM_VERSION_62   "Netscape-ldbm/6.2"         /* db42: old idl */
#define LDBM_VERSION_61   "Netscape-ldbm/6.1"         /* db33: old idl */
#define LDBM_VERSION_60   "Netscape-ldbm/6.0"         /* db33: old idl */

#define LDBM_VERSION_50   "Netscape-ldbm/5.0"
#define LDBM_VERSION_40   "Netscape-ldbm/4.0"
#define LDBM_VERSION_30   "Netscape-ldbm/3.0"
#define LDBM_VERSION_31   "Netscape-ldbm/3.1"
#define DBVERSION_FILENAME "DBVERSION"
/* 0 here means to let the autotuning reset the value on first run */
/* cache can't get any smaller than this (in bytes) */
#define MINCACHESIZE             (uint64_t)512000
#define DEFAULT_CACHE_SIZE       (uint64_t)0
#define DEFAULT_CACHE_SIZE_STR   "0"
#define DEFAULT_CACHE_ENTRIES    -1 /* no limit */
#define DEFAULT_CACHE_PINNED_ENTRIES_STR "0"
#define DEFAULT_DNCACHE_SIZE     (uint64_t)16777216
#define DEFAULT_DNCACHE_SIZE_STR "16777216"
#define DEFAULT_DNCACHE_MAXCOUNT -1 /* no limit */
#define DEFAULT_DBCACHE_SIZE     33554432
#define DEFAULT_DBCACHE_SIZE_STR "33554432"
#define DEFAULT_DBLOCK_PAUSE     500
#define DEFAULT_DBLOCK_PAUSE_STR "500"
#define DEFAULT_MODE             0600
#define DEFAULT_ALLIDSTHRESHOLD  2147483646
#define DEFAULT_IDL_TUNE         1
#define DEFAULT_SEARCH_TUNE      0
#define DEFAULT_IMPORT_INDEX_BUFFER_SIZE 0
#define SUBLEN 3
#define LDBM_CACHE_RETRY_COUNT 1000        /* Number of times we re-try a cache operation */
#define RETRY_CACHE_LOCK       2           /* error code to signal a retry of the cache lock */
#define IDL_FETCH_RETRY_COUNT  5           /* Number of times we re-try idl_fetch if it returns deadlock */
#define IMPORT_SUBCOUNT_HASHTABLE_SIZE 500 /* Number of buckets in hash used to accumulate subcount for broody parents */

/* minimum max ids that a single index entry can map to in ldbm */
#define SLAPD_LDBM_MIN_MAXIDS 4000

/* clear the following flag to suppress "database files do not exist" warning */
extern int ldbm_warn_if_no_db;

/*
 * there is a single index for each attribute.  these prefixes insure
 * that there is no collision among keys.
 */
#define EQ_PREFIX     '='  /* prefix for equality keys     */
#define APPROX_PREFIX '~'  /* prefix for approx keys       */
#define SUB_PREFIX    '*'  /* prefix for substring keys    */
#define CONT_PREFIX   '\\' /* prefix for continuation keys */
#define RULE_PREFIX   ':'  /* prefix for matchingRule keys */
#define PRES_PREFIX   '+'
#define HASH_PREFIX   '#'

/* Values for "disposition" value in idl_insert_key() */
#define IDL_INSERT_NORMAL     1
#define IDL_INSERT_ALLIDS     2
#define IDL_INSERT_NOW_ALLIDS 3

#define DEFAULT_BLOCKSIZE 8192

/*
 * The candidate list size at which it is cheaper to apply the filter test
 * to the whole list than to continue ANDing in IDLs.
 */
#define FILTER_TEST_THRESHOLD (NIDS)10

/* flags to indicate what kind of startup the dblayer should do */
#define DBLAYER_IMPORT_MODE                 0x1
#define DBLAYER_NORMAL_MODE                 0x2
#define DBLAYER_EXPORT_MODE                 0x4
#define DBLAYER_ARCHIVE_MODE                0x8
#define DBLAYER_RESTORE_MODE               0x10
#define DBLAYER_RESTORE_NO_RECOVERY_MODE   0x20
#define DBLAYER_TEST_MODE                  0x40
#define DBLAYER_INDEX_MODE                 0x80
#define DBLAYER_CLEAN_RECOVER_MODE        0x100
#define DBLAYER_NO_DBTHREADS_MODE        0x1000

#define DBLAYER_RESTORE_MASK (DBLAYER_RESTORE_MODE | DBLAYER_RESTORE_NO_RECOVERY_MODE)


/*
 * the id used in the indexes to refer to an entry
 */
typedef u_int32_t ID;
#define MAXID  ((ID)-3)
#define NOID   ((ID)-2)
#define ALLID  ((ID)-1)
#define ID_FMT "%u" /* used in printf-like statements */

/*
 * effective only on idl_new_fetch
 */
#define NEW_IDL_NOOP     1 /* no need to fetch on new idl */
#define NEW_IDL_NO_ALLID 2 /* force to return full idl (no allids) */
#define NEW_IDL_DEFAULT  0

/*
 * if the id of any backend instance is above the threshold, then warning
 * message will be logged about the need of rebuilding the database in question
 */
#define ID_WARNING_THRESHOLD (MAXID * 0.9)

/*
 * Use this to count and index into an array of ID.
 */
typedef uint32_t NIDS;

/*
 * This structure represents an id block on disk and an id list
 * in core.
 *
 * The fields have the following meanings:
 *
 *  b_nmax  maximum number of ids in this block. if this is == ALLIDSBLOCK,
 *      then this block represents all ids.
 *  b_nids  current number of ids in use in this block.  if this
 *      is == INDBLOCK, then this block is an indirect block
 *      containing a list of other blocks containing actual ids.
 *      the list is terminated by an id of NOID.
 *  b_ids   a list of the actual ids themselves
 */
#define ALLIDSBLOCK 0 /* == 0 => this is an allid block  */
#define INDBLOCK    0 /* == 0 => this is an indirect blk */

/* Default to holding 8 ids for idl_fetch_ext */
#define IDLIST_MIN_BLOCK_SIZE 8

typedef struct block
{
    NIDS b_nmax;        /* max number of ids in this list  */
    NIDS b_nids;        /* current number of ids used      */
    struct block *next; /* Pointer to the next block in the list
                         * used by idl_set
                         */
    size_t itr;         /* internal tracker of iteration for set ops */
    ID b_ids[1];        /* the ids - actually bigger       */
} Block, IDList;

typedef struct _idlist_set
{
    int64_t count;
    int64_t allids;
    size_t total_size;
    IDList *minimum;
    IDList *head;
    IDList *complement_head;
} IDListSet;

#define ALLIDS(idl)         ((idl)->b_nmax == ALLIDSBLOCK)
#define INDIRECT_BLOCK(idl) ((idl)->b_nids == INDBLOCK)
#define IDL_NIDS(idl)       (idl ? (idl)->b_nids : (NIDS)0)

typedef size_t idl_iterator;

/* small hashtable implementation used in the entry cache -- the table
 * stores little identical structs, and relies on using a (void *) inside
 * the struct to store linkage information.
 */
typedef int (*HashTestFn)(const void *, const void *);
typedef unsigned long (*HashFn)(const void *, size_t);
typedef struct
{
    u_long offset;     /* offset of linkage info in user struct */
    u_long size;       /* members in array below */
    HashFn hashfn;     /* compute a hash value on a key */
    HashTestFn testfn; /* function to test if two entries are equal */
    void *slot[1];     /* actually much bigger */
} Hashtable;

/* use this macro to find the offset of the linkage info into your structure
 * (required for hashtable to work correctly)
 * HASHLOC(struct mything, linkptr)
 */
#define HASHLOC(mem, node) (u_long) & (((mem *)0L)->node)

/* type to set ep_type */
#define CACHE_TYPE_ENTRY 0
#define CACHE_TYPE_DN    1

struct backcommon
{
    int32_t ep_type;                /* to distinguish backdn from backentry */
    struct backcommon *ep_lrunext;  /* for the cache */
    struct backcommon *ep_lruprev;  /* for the cache */
    ID ep_id;                       /* entry id */
    uint8_t ep_state;               /* state in the cache */
#define ENTRY_STATE_DELETED    0x1  /* entry is marked as deleted */
#define ENTRY_STATE_CREATING   0x2  /* entry is being created; don't touch it */
#define ENTRY_STATE_NOTINCACHE 0x4  /* cache_add failed; not in the cache */
#define ENTRY_STATE_INVALID    0x8  /* cache entry is invalid and needs to be removed */
#define ENTRY_STATE_UNAVAILABLE 0xf /* entry is not fully created or is deleted */
#define ENTRY_STATE_PINNED     0x10 /* cache entry is pinned (never removed by the lru) */
    int32_t ep_refcnt;              /* entry reference cnt */
    size_t ep_size;                 /* for cache tracking */
    struct timespec ep_create_time; /* the time the entry was added to the cache */
};

/* From ep_type through ep_create_time MUST be identical to backcommon */
struct backentry
{
    int32_t ep_type;                /* to distinguish backdn from backentry */
    struct backcommon *ep_lrunext;  /* for the cache */
    struct backcommon *ep_lruprev;  /* for the cache */
    ID ep_id;                       /* entry id */
    uint8_t ep_state;               /* state in the cache */
    int32_t ep_refcnt;              /* entry reference cnt */
    size_t ep_size;                 /* for cache tracking */
    struct timespec ep_create_time; /* the time the entry was added to the cache */
    Slapi_Entry *ep_entry;          /* real entry */
    Slapi_Entry *ep_vlventry;
    void *ep_dn_link;               /* linkage for the 3 hash */
    void *ep_id_link;               /*     tables used for */
    void *ep_uuid_link;             /*     looking up entries */
    PRMonitor *ep_mutexp;           /* protection for mods; make it reentrant */
    uint64_t ep_weight;             /* for cache eviction */
};

/* From ep_type through ep_create_time MUST be identical to backcommon */
struct backdn
{
    int32_t ep_type;                /* to distinguish backdn from backentry */
    struct backcommon *ep_lrunext;  /* for the cache */
    struct backcommon *ep_lruprev;  /* for the cache */
    ID ep_id;                       /* entry id */
    uint8_t ep_state;               /* state in the cache; share ENTRY_STATE_* */
    int32_t ep_refcnt;              /* entry reference cnt */
    uint64_t ep_size;               /* for cache tracking */
    struct timespec ep_create_time; /* the time the entry was added to the cache */
    Slapi_DN *dn_sdn;
    void *dn_id_link;               /* for hash table */
};

/* Entry Cache statistics */
struct cache_stats
{
    uint64_t hits;            /* for analysis of hits/misses */
    uint64_t tries;
    uint64_t nentries;        /* current # entries in cache */
    int64_t  maxentries;      /* max entries allowed (-1: no limit) */
    uint64_t size;            /* current size in bytes */
    uint64_t maxsize;         /* max size in bytes */
    uint64_t weight;          /* total weight of all entries */
    uint64_t nehw;            /* current # entries having weight in cache */
                              /* weight/nehw is the average time in
                               * microseconds needed to load an entry
                               * in the cache
                               */
};

/* for the in-core cache of entries */
struct cache
{
    Hashtable *c_dntable;
    Hashtable *c_idtable;
#ifdef UUIDCACHE_ON
    Hashtable *c_uuidtable;
#endif
    struct backcommon *c_lruhead; /* add entries here */
    struct backcommon *c_lrutail; /* remove entries here */
    PRMonitor *c_mutex;           /* lock for cache operations */
    PRLock *c_emutexalloc_mutex;
    struct cache_stats c_stats;
    struct ldbm_instance *c_inst;
    struct pinned_ctx  *c_pinned_ctx; /* Pinned entries handler context */
};

#define CACHE_ADD(cache, p, a) cache_add((cache), (void *)(p), (void **)(a))
#define CACHE_RETURN(cache, p) cache_return((cache), (void **)(p))
#define CACHE_REMOVE(cache, p) cache_remove((cache), (void *)(p))
#define CACHE_LOCK(cache)      cache_lock((cache))
#define CACHE_UNLOCK(cache)    cache_unlock((cache))

/* For backentry_compute_weight implementation */
typedef struct timespec BackEntryWeightData;

/* various modules keep private data inside the attrinfo structure */
typedef struct dblayer_private     dblayer_private;
typedef struct idl_private         idl_private;
typedef struct attrcrypt_private   attrcrypt_private;

/*
 * Special attributes for an index entry to change the substring index width.
 * By default, substring index width is 3, i.e., search with the filter
 * "(cn=abc*)" is an indexed search, but "(cn=ab*)" or "(cn=a*)" isn't.
 * There is a big performance gap between the indexed search and the unindexed
 * search especially when the database is large.  To convert such unindexed
 * search to the indexed search to speed up the query, these nsSubStr
 * attributes are introduced.
 *
 * How to use the nsSubStr attributes:
 * 1) turn the target index to extensibleobject by adding
 *    "objectClass: extensibleObject" to the index entry
 * 2) set the length to each nsSubStr attribute of the index
 *    dn: cn=sn, cn=index, cn=userRoot, cn=ldbm database, cn=plugins, cn=config
 *    objectClass: extensibleObject
 *    nsSubStrBegin: 2
 *    nsSubStrMiddle: 3
 *    nsSubStrEnd: 2
 *    [...]
 *
 * By default, the minimum key length triplets of substring index is 3, 3, 3.
 * The length is changed by setting the triplets nsSubStrBegin, nsSubStrMiddle,
 * nsSubStrEnd, respectively.
 *
 * Note: If any of the key length value is modified, the index file needs
 * to be regenerated.  Otherwise, the index file is going to have mixed
 * key length.
 * To change the key length,
 * 1) stop the server,
 * 2) run db2index -t <attr>,
 * 3) start the server.
 */
#define INDEX_ATTR_SUBSTRBEGIN  "nsSubStrBegin"
#define INDEX_ATTR_SUBSTRMIDDLE "nsSubStrMiddle"
#define INDEX_ATTR_SUBSTREND    "nsSubStrEnd"

#define INDEX_SUBSTRBEGIN  0
#define INDEX_SUBSTRMIDDLE 1
#define INDEX_SUBSTREND    2

struct index_idlistsizeinfo
{
    int ai_idlistsizelimit; /* max id list size */
    int ai_indextype;       /* index type */
    unsigned int ai_flags;
#define INDEX_ALLIDS_FLAG_AND 0x01
    Slapi_ValueSet *ai_values; /* index keys to apply the max id list size to */
};

/* for the cache of attribute information (which are indexed, etc.) */
struct attrinfo
{
    char *ai_type;    /* type name (cn, sn, ...)    */
    int ai_indexmask; /* how the attr is indexed    */
#define INDEX_PRESENCE  0x01
#define INDEX_EQUALITY  0x02
#define INDEX_APPROX    0x04
#define INDEX_SUB       0x08
#define INDEX_UNKNOWN   0x10
#define INDEX_FROMINIT  0x20
#define INDEX_RULES     0x40
#define INDEX_VLV       0x80
#define INDEX_SUBTREE  0x100
#define INDEX_ANY (INDEX_PRESENCE | INDEX_EQUALITY | INDEX_APPROX | INDEX_SUB | INDEX_RULES | INDEX_VLV | INDEX_SUBTREE)

#define INDEX_OFFLINE 0x1000 /* index is being generated, or     \
                              * has been created but not indexed \
                              * yet. */

#define IS_INDEXED(a) (a & INDEX_ANY)
    char **ai_index_rules;               /* matching rule OIDs */
    void *ai_dblayer;                    /* private data used by the dblayer code */
    uint64_t ai_dblayer_count;           /* used by the dblayer code */
    idl_private *ai_idl;                 /* private data used by the IDL code (eg locking the IDLs) */
    attrcrypt_private *ai_attrcrypt;     /* private data used by the attribute encryption code (eg is it enabled or not) */
    value_compare_fn_type ai_key_cmp_fn; /* function used to compare two index keys -
                                            The function is the compare function provided by
                                            attr_get_value_cmp_fn - this function is used to order
                                            the keys in the index so that we can use ORDERING
                                            searches.  In order for this function to be used,
                                            the syntax plugin must define a compare function,
                                            and either the attribute definition schema must
                                            specify an ORDERING matching rule, or the index
                                            configuration must define an ORDERING matching rule.
                                         */
    void *ai_dup_cmp_fn;     /* function used to compare dups -
                                          used to order duplicates belonging
                                          to the same index key.  By default,
                                          idl_new_compare_dups is set.
                                          If some special ordering is needed,
                                          special compare fn is set here.
                                          (e.g., for entryrdn)
                                          Note: this callback is set and used by
                                          the db implenentation plugins so its
                                          prototype may vary
                             */
    int *ai_substr_lens;                 /* if the attribute nsSubStrXxx is specivied in
                             * an index instance (dse.ldif), the substr key
                             * len value(s) are stored here.  If not specified,
                             * the default length triplet is 2, 3, 2.
                             */
    Slapi_Attr ai_sattr;                 /* interface to syntax and matching rule plugins */
    DataList *ai_idlistinfo;             /* fine grained id list */
};

struct id_array
{
    int ida_next_index; /*The next index that is free*/
    int ida_size;       /*The size of this puppy*/
    ID *ida_ids;        /*The array of ids*/
};
typedef struct id_array Id_Array;

struct _db_upgrade_info
{
    char *old_version_string;
    int old_dbversion_major;
    int old_dbversion_minor;
    int type;
    int action;
    int is_dbd;
};
typedef struct _db_upgrade_info db_upgrade_info;
/* Values for dbversion_stuff->type */
#define DBVERSION_COMPATIBLE 0x10
#define DBVERSION_UPGRADABLE 0x20
#define DBVERSION_SOL        0x40
#define DBVERSION_OLD_IDL     0x1
#define DBVERSION_NEW_IDL     0x2
#define DBVERSION_RDN_FORMAT  0x4

/* Values for dbversion_stuff->action + return value */
#define DBVERSION_NO_UPGRADE           0x0
#define DBVERSION_NEED_IDL_OLD2NEW   0x100
#define DBVERSION_NEED_IDL_NEW2OLD   0x200
#define DBVERSION_UPGRADE_3_4        0x400  /* bdb 3.3 -> 4.2 */
                                            /* The log file format changed;
                                             * No database formats changed;
                                             * db extention: .db3 -> .db4
                                             */
#define DBVERSION_UPGRADE_4_4        0x800  /* bdb 4.2 -> 4.3 -> 4.4 -> 4.5 */
                                            /* The log file format changed;
                                             * No database formats changed;
                                             * no db extention change
                                             */
#define DBVERSION_UPGRADE_4_5       0x4000  /* bdb 4.X -> 5.X */
#define DBVERSION_NEED_DN2RDN       0x1000  /* DN to RDN (subtree-rename) format */
#define DBVERSION_NOT_SUPPORTED 0x10000000

#define DBVERSION_TYPE   0x1
#define DBVERSION_ACTION 0x2

struct ldbminfo
{
    int li_mode;
    int li_lookthroughlimit;
    int li_allidsthreshold;
    char *li_directory;
    int li_reslimit_lookthrough_handle;
    uint64_t li_dbcachesize;
    int li_dblock;
    int li_dbncache;
    int li_import_cache_autosize;       /* % of free memory to use for the import caches
                                         * (-1=default, 80% on cmd import)
                                         * (0 = off) -- overrides import cache size settings */
    int li_cache_autosize;              /* % of free memory to use for the combined caches
                                         * (0 = off) -- overrides other cache size settings */
    int li_cache_autosize_split;        /* % of li_cache_autosize to use for the libdb cache.
                                         * the rest is split up among the instance entry caches */
    uint64_t li_cache_autosize_ec;      /* new instances created while the server is up, should
                                         * use this as the entry cache size (0 = autosize off) */
    uint64_t li_dncache_autosize_ec;    /* Same as above, but dncache. */
    uint64_t li_import_cachesize;       /* size of the mpool for imports */
    int li_shutdown;                     /* flag to tell any BE threads to end */
    PRLock *li_shutdown_mutex;           /* protect shutdown flag */
    dblayer_private *li_dblayer_private; /* session ptr for databases */
    void *li_dblayer_config;             /* pointer to specific backend implementation */
    char *li_backend_implement;          /* low layer backend implementation */
    int li_noparentcheck;                /* check if parent exists on add */

    /* db lock monitoring */
    /* if we decide to move the values to bdb_config, we can use slapi_back_get_info function to retrieve the values */
    int32_t li_dblock_monitoring;          /* enables db locks monitoring thread - requires restart  */
    uint32_t li_dblock_monitoring_pause;   /* an interval for db locks monitoring thread */
    uint32_t li_dblock_threshold;          /* when the percentage is reached, abort the search in ldbm_back_next_search_entry - requires restart*/
    uint32_t li_dblock_threshold_reached;

    /* the next 4 fields are for the params that don't get changed until
     * the server is restarted (used by the admin console)
     */
    char *li_new_directory;
    uint64_t li_new_dbcachesize;
    int li_new_dblock;
    int32_t li_new_dblock_monitoring;
    uint64_t li_new_dblock_threshold;

    int li_new_dbncache;

    db_upgrade_info *upgrade_info;
    int li_filter_bypass;       /* bypass filter testing, when possible */
    int li_filter_bypass_check; /* check that filter bypass is doing the right thing */
    int li_use_vlv;             /* use vlv indexes to short-circuit matches when possible */
    void *li_identity;          /* The ldbm plugin needs to keep track of its identity so it can
                                 * perform internal ops.  Its identity is given to it when
                                 * its init function is called. */

    Objset *li_instance_set;    /* A set containing the ldbm instances. */

    PRLock *li_config_mutex;

    /* There are times when we need a pointer to the ldbm database
     * plugin, so we will store a pointer to it here.  Examples of
     * when we need it are when we create a new instance and when
     * we need the name of the plugin to do internal ops. */
    struct slapdplugin *li_plugin;

    /* factory extension markers for the Connection struct -- bulk import
     * uses this to store state info on a Connection.
     */
    int li_bulk_import_object;
    int li_bulk_import_handle;
    /* maximum number of pass before merging the files during an import */
    int li_maxpassbeforemerge;

    /* charray of attributes to exclude from LDIF export */
    char **li_attrs_to_exclude_from_export;

    int li_flags;
    int li_fat_lock;                      /* 608146 -- make this configurable, first */
    int li_legacy_errcode;                /* 615428 -- in case legacy err code is expected */
    Slapi_Counter *li_global_usn_counter; /* global USN counter */
    int li_reslimit_allids_handle;        /* allids aka idlistscan */
    int li_pagedlookthroughlimit;
    int li_pagedallidsthreshold;
    int li_reslimit_pagedlookthrough_handle;
    int li_reslimit_pagedallids_handle; /* allids aka idlistscan */
    int li_rangelookthroughlimit;
    int li_reslimit_rangelookthrough_handle;
    int li_idl_update;
    int li_old_idl_maxids;
    int li_online_import_encrypt; /* toggle attribute encryption during bdb_ldbm_back_wire_import */
#define BACKEND_OPT_NO_RUV_UPDATE              0x01
#define BACKEND_OPT_DBLOCK_INSIDE_TXN          0x02
#define BACKEND_OPT_MANAGE_ENTRY_BEFORE_DBLOCK 0x04
    int li_backend_opt_level;
    size_t li_max_key_len;
};


#define NO_RUV_UPDATE(li)              (li->li_backend_opt_level & BACKEND_OPT_NO_RUV_UPDATE)
#define DBLOCK_INSIDE_TXN(li)          (li->li_backend_opt_level & BACKEND_OPT_DBLOCK_INSIDE_TXN)
#define MANAGE_ENTRY_BEFORE_DBLOCK(li) (li->li_backend_opt_level & BACKEND_OPT_MANAGE_ENTRY_BEFORE_DBLOCK)

/* li_flags could store these bits defined in ../slapi-plugin.h
 * task flag (pb_task_flags) *
 *   SLAPI_TASK_RUNNING_AS_TASK
 *   SLAPI_TASK_RUNNING_FROM_COMMANDLINE
 */
/* allow conf w/o CONFIG_FLAG_ALLOW_RUNNING_CHANGE to be updated */
#define LI_FORCE_MOD_CONFIG 0x10
#define LI_BDB_IMPL         0x20
#define LI_LMDB_IMPL        0x40

#define LI_DEFAULT_IMPL_FLAG  LI_BDB_IMPL /* the default is BDB for now */

typedef enum {
    BTXNACT_INDEX_ADD,            /* data is a index_update_t */
    BTXNACT_INDEX_DEL,            /* data is a index_update_t */
    BTXNACT_VLV_ADD,              /* data is an entry ID */
    BTXNACT_VLV_DEL,              /* data is an entry ID */
    BTXNACT_ID2ENTRY_ADD,         /* data is the entry */
    BTXNACT_ENTRYRDN_ADD,         /* key is a srdn, data is an id */
    BTXNACT_ENTRYRDN_DEL          /* key is a srdn, data is an id */
} back_txn_action;

/* Structure used to hold stuff for the lifetime of an LDAP transaction */
/* If we do clever stuff like LDAP transactions, we'll need a stack of TXN ID's */
typedef struct back_txn back_txn;
struct back_txn
{
    dbi_txn_t *back_txn_txn; /* Transaction ID for the database */
                             /* Special handling - (used by mdb import to push updates in writing thread queue) */
    int (*back_special_handling_fn)(backend *be, back_txn_action action, dbi_db_t *db, dbi_val_t *key, dbi_val_t *data, back_txn *txn);
};
typedef void *back_txnid;

/* helper struct to pass index parameters towards db-mdb plugin */
typedef struct {
    ID id;
    struct attrinfo *a;
    int *disposition;
} index_update_t;

#define RETRY_TIMES 50

/* Structure used to communicate information about subordinatecount on import/upgrade */
struct _import_subcount_stuff
{
    PLHashTable *hashtable;
};
typedef struct _import_subcount_stuff import_subcount_stuff;

/* Handy structures for modify operations */

struct _modify_context
{
    struct backentry *old_entry;
    struct backentry *new_entry;
    Slapi_Mods *smods;
    int attr_encrypt;
};
typedef struct _modify_context modify_context;

#define INSTANCE_DB_SUFFIX        "-db"
#define INSTANCE_CHANGELOG_SUFFIX "-changelog"


/* This structure was moved here from dblayer.c because the ldbm_instance
 * structure uses the dblayer_handle structure.  */
struct tag_dblayer_handle
{
    dbi_db_t *dblayer_dbp;
    PRLock *dblayer_lock; /* used when anyone wants exclusive access to a file */
    struct tag_dblayer_handle *dblayer_handle_next;
    void **dblayer_handle_ai_backpointer; /* Voodo magic pointer to the place where we store a
                                            pointer to this handle in the attrinfo structure */
};
typedef struct tag_dblayer_handle dblayer_handle;

/* This structure was moved here from perfctrs.c so the ldbm_instance structure
 * could use it. */
struct _perfctrs_private
{
    /* Pointer to the shared memory */
    void *memory;
};
typedef struct _perfctrs_private perfctrs_private;

typedef struct _attrcrypt_state_private attrcrypt_state_private;

/* flags for ldbm_instance */
/* please lock inst_config_mutex before changing inst_flags */
#define INST_FLAG_BUSY     0x0001 /* instance is doing an import or restore. */
#define INST_FLAG_READONLY 0x0002 /* instance is truly readonly */

/* Structure used to hold instance specific information. */
typedef struct ldbm_instance
{
    char *inst_name;          /* Name given for this instance. */
    backend *inst_be;         /* pointer back to the backend */
    struct ldbminfo *inst_li; /* pointer back to global info */
    int inst_flags;           /* see above */

    PRLock *inst_config_mutex;
    Slapi_Counter *inst_ref_count; /* Keeps track of how many operations
                                    * are currently using this instance */
    char *inst_dir_name;           /* The name of the directory in the db
                                    * directory that holds the index files
                                    * for this instance. Relative to the
                                    * parent of the instance name dir */
    char *inst_parent_dir_name;    /* Absolute parent dir for this inst */

    PRMonitor *inst_db_mutex; /* Used to synchronize write operations
                               * on this instance - the monitor is re-entrant
                               * which allows plugins in the same transaction
                               * to share the lock */

    dblayer_handle *inst_handle_head; /* These are used to maintain a list */
    dblayer_handle *inst_handle_tail; /* of open db handles for this instance */
    PRLock *inst_handle_list_mutex;

    dbi_db_t *inst_id2entry; /* id2entry for this instance. */
    dbi_db_t *inst_changelog; /* changelog for this instance. */

    perfctrs_private inst_perf_private; /* Private data for the performance counters specific to this instance */
    attrcrypt_state_private *inst_attrcrypt_state_private;
    int attrcrypt_configured; /* Are any attributes configured for encryption ? */

    Avlnode *inst_attrs; /* Keeps track of what's indexed for this instance. */

    struct cache inst_cache; /* The entry cache for this instance. */

    PRLock *inst_nextid_mutex;
    ID inst_nextid;

    PRCondVar *inst_indexer_cv; /* indexer thread cond var */
    PRThread *inst_indexer_tid; /* for the indexer thread */

    long inst_cache_hits; /* used during imports to figure out when a pass should end. */
    long inst_cache_misses;

    char *inst_dataversion;          /* The user data version tag.  Used by replication. */
    void *inst_db;                   /* implementation specific instance data */
    int require_index;               /* set to 1 to require an index be used in search */
    int require_internalop_index;    /* set to 1 to require an index be used in an internal search */
    struct cache inst_dncache;       /* The dn cache for this instance. */
    int cache_pinned_entries;        /* Number of entries to preserve during cache eviction */
    char *cache_debug_pattern;       /* Entries whose dn matche this pattern are logged as INFO
                                      * when they get added/removed from entry cache
                                      */
    Slapi_Regex *cache_debug_re;     /* Compiled version of cache_debug_pattern */
} ldbm_instance;

/*
 * This structure is passed through the PBlock from ldbm_back_search to
 * ldbm_back_next_search_entry.  It contains the candidate result set
 * determined by ldbm_back_search, to be served up by ldbm_back_next_search_entry.
 */
typedef struct _back_search_result_set
{
    IDList *sr_candidates;        /* the search results */
    idl_iterator sr_current;      /* the current position in the search results */
    struct backentry *sr_entry;   /* the last entry returned */
    int sr_lookthroughcount;      /* how many have we examined? */
    int sr_lookthroughlimit;      /* how many can we examine? */
    int sr_virtuallistview;       /* is this a VLV Search */
    Slapi_Entry *sr_vlventry;     /* a special VLV Entry for when the ACL check fails */
    int sr_flags;                 /* Magic flags, defined below */
    int sr_current_sizelimit;     /* Current sizelimit */
    Slapi_Filter *sr_norm_filter; /* search filter pre-normalized */
    Slapi_Filter *sr_norm_filter_intent; /* intended search filter pre-normalized */
} back_search_result_set;
#define SR_FLAG_MUST_APPLY_FILTER_TEST 1 /* If set in sr_flags, means that we MUST apply the filter test */

#include "proto-back-ldbm.h"
#include "ldbm_config.h"

/* flags used when adding/removing index items */
#define BE_INDEX_ADD           1
#define BE_INDEX_DEL           2
#define BE_INDEX_PRESENCE      4                       /* (w/DEL) remove the presence index */
#define BE_INDEX_TOMBSTONE     8                       /* Index entry as a tombstone */
#define BE_INDEX_DONT_ENCRYPT 16                       /* Disable any encryption if this flag is set */
#define BE_INDEX_EQUALITY     32                       /* (w/DEL) remove the equality index */
#define BE_INDEX_NORMALIZED SLAPI_ATTR_FLAG_NORMALIZED /* value already normalized (0x200) */

/* Name of attribute type used for binder-based look through limit */
#define LDBM_LOOKTHROUGHLIMIT_AT "nsLookThroughLimit"
/* Name of attribute type used for binder-based look through limit */
#define LDBM_RANGELOOKTHROUGHLIMIT_AT "nsRangeSearchLookThroughLimit"
/* Name of attribute type used for binder-based look through limit */
#define LDBM_ALLIDSLIMIT_AT "nsIDListScanLimit"
/* Name of attribute type used for binder-based look through simple paged limit */
#define LDBM_PAGEDLOOKTHROUGHLIMIT_AT "nsPagedLookThroughLimit"
/* Name of attribute type used for binder-based look through simple paged limit */
#define LDBM_PAGEDALLIDSLIMIT_AT "nsPagedIDListScanLimit"

#define LDBM_ANCESTORID_STR                "ancestorid"
#define LDBM_ENTRYDN_STR                   SLAPI_ATTR_ENTRYDN
#define LDBM_LONG_ENTRYRDN_STR             "@long-entryrdn"
#define LDBM_ENTRYRDN_STR                  "entryrdn"
#define LDBM_NUMSUBORDINATES_STR           "numsubordinates"
#define LDBM_TOMBSTONE_NUMSUBORDINATES_STR "tombstonenumsubordinates"
#define LDBM_PARENTID_STR                  SLAPI_ATTR_PARENTID
#define LDBM_ENTRYID_STR                   "entryid"

/* Name of psuedo attribute used to track default indexes */
#define LDBM_PSEUDO_ATTR_DEFAULT ".default"

/* for checking disk full errors. */
#define LDBM_OS_ERR_IS_DISKFULL(err) ((err) == ENOSPC || (err) == EFBIG)

/* flag: open_flag for dblayer_get_index_file -> dblayer_open_file */
#define DBOPEN_CREATE      0x1 /* oprinary mode: create a db file if needed */
#define DBOPEN_TRUNCATE    0x2 /* oprinary mode: truncate a db file if needed */
#define DBOPEN_ALLOW_DIRTY 0x4 /* oprinary mode: accept to open a dirty (i.e import/reindex is running) db file */

/* whether we call fat lock or not [608146] */
#define SERIALLOCK(li) (li->li_fat_lock)

/*
 * 0: SUCCESS
 * libdb returns negative error codes
 * Linux errno's < 140, for now
 * Chose any positive value other than the above values.
 * Being used to specify duplicated DN is found in entrydn or entryrdn.
 */
#define LDBM_ERROR_FOUND_DUPDN 9999

/* Initial entryusn value */
#define SIGNEDINITIALUSN (-1)
#define INITIALUSN       (PRUint64)(-1)

/* changelog backup dir name
 * starting with '.' to reduce the risk to match an ordinary backend name */
#define CHANGELOG_BACKUPDIR ".repl_changelog_backup"

/* For dblayer_get_aux_id2entry_ext */
#define DBLAYER_AUX_ID2ENTRY_TMP 0x1

/* operation for parent_update_on_childchange */
#define PARENTUPDATE_ADD      0x1
#define PARENTUPDATE_DEL      0x2
#define PARENTUPDATE_RESURECT 0x4
#define PARENTUPDATE_MASK (PARENTUPDATE_ADD | PARENTUPDATE_DEL | PARENTUPDATE_RESURECT)

#define PARENTUPDATE_CREATE_TOMBSTONE 0x10
#define PARENTUPDATE_DELETE_TOMBSTONE 0x20
#define PARENTUPDATE_TOMBSTONE_MASK (PARENTUPDATE_CREATE_TOMBSTONE | PARENTUPDATE_DELETE_TOMBSTONE)

#define TOMBSTONE_INCLUDED 0x1 /* used by find_entry2modify_only_ext and \
                                  entryrdn_index_read */

/*
 * This only works with normalized keys, which should be ok because
 * at this point both L and R should have already been normalized.
 */
#define KEY_EQ(L, R) \
    ((L)->size == (R)->size && !memcmp((L)->data, (R)->data, (L)->size))

typedef int backend_implement_init_fn(struct ldbminfo *li, config_info *config_array);

pthread_mutex_t *get_import_ctx_mutex(void);
#endif /* _back_ldbm_h_ */
