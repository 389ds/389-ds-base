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


#include	<stdio.h>
#include	<ctype.h>
#include	<string.h>
#include	<time.h>
#include	<stdlib.h>
#include        <ldap.h>
#ifndef _WIN32
#  define	stricmp	strcasecmp
#else
#  include	<io.h>
#endif

#include <nss.h>
#include <pk11func.h>

#include <slap.h>
#include <getopt_ext.h>
#include <ldaplog.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifdef _WIN32
int slapd_ldap_debug = 0;
int *module_ldap_debug;
#endif

/*
 * VSTRING was defined in PMDF headers.
 */
typedef struct vstring {
  int length;
  char body[252];
} MM_VSTRING;

/*
 * Base64 declarations.
 */

typedef struct Enc64_s {
	struct Enc64_s *	next;
	unsigned char *		source;
	int			slen;
} Enc64_t;

typedef struct Dec64_s {
	struct Dec64_s *	next;
	unsigned char *		dest;
	int			maxlen;
	int			curlen;
	int			nextra;
	unsigned char			extra[3];
} Dec64_t;

Enc64_t * initEnc64(unsigned char * source, int slen);
int Enc64(Enc64_t * this, unsigned char *dest, int maxlen, int *len);
void freeEnc64(Enc64_t *this);
Dec64_t * initDec64(unsigned char * dest, int maxlen);
int freeDec64(Dec64_t *this);
int Dec64(Dec64_t * this, unsigned char *source);

/*
 * License check declarations.
 */

int	license_limit = -1;
int	license_count;

/*
 * Public declarations.(potentially).
 */

#define IDDS_MM_OK      0
#define IDDS_MM_EOF     -1
#define IDDS_MM_ABSENT  -2
#define IDDS_MM_FIO     -3
#define IDDS_MM_BAD     -4

/* attrib_t is used to hold each record in memory.  The emphasis here is
 * on size, although compromising simplicity rather than speed.  In reality
 * the way this is used is that there is a block of bytes defined.  within 
 * that block are a sequence of records each alligned on whatever is needed
 * to read shorts happilly.  each record consists of a name, a value and 
 * their lengths.  The value length is first, because it has to be aligned
 * then the name value, because we need it first, then the name, null 
 * terminated, then the value, null terminated.  Thus if "thing" is a pointer
 * to one of these things,
 * thing->data          is the name
 * (thing->data + namelen + 1)  is the value,
 * (thing->data + ((namelen + 1 + valuelen + 1 + 3) & ~3)       is the next one
 *      (if we're aligned on 4 byte boundaries)
 */
typedef int     Boolean;
typedef struct {
        int     valuelen;
        char    namelen;
        char    data[1];
} attrib_t;

#define attribname(thing)       (thing)->data
#define attribvalue(thing)      ((thing)->data + (thing)->namelen + 1)
#define attribalign     4
#define attribsize(thing)       (((thing)->namelen + (thing)->valuelen + 1 \
                                        + attribalign) & ~(attribalign-1))
#define attribnext(thing)       (attrib_t *)(((char *)thing) \
                + (((thing)->namelen + (thing)->valuelen \
                + sizeof(int) + 2 + attribalign) & ~(attribalign-1)))

/* record_t     is used to hold a record once it had been squeezed
 *      obviously it has to be allocated carefully so that it is the right size
 */
typedef struct {
    short       nattrs;
    attrib_t    data;
} record_t;

/* attrib1_t is used to read in and sort a record */
typedef struct attrib1_s {
    struct attrib1_s *next;
    char    namelen;
    char    name[64];
    int     valuelen;
    char    value[0x20000];
} attrib1_t;

typedef struct ignore_s {
    struct ignore_s *next;
    char    name[65];
} ignore_t;

/* entry_t is the structure used to carry the fingerprint in the hash table */
typedef struct entry_s {
    struct entry_s *overflow;       /* we hash into buckets.  This
                                     * is the chain of entries */
    char            key[20];        /* this is the hash of the DN */
    int             present[4];     /* actually treated as a 128 bit array*/
                                    /* this is the bitmap of which 
                                     * directories contain this DN */
    int             db;             /* this is the directory number which
                                     * provided the data for this entry */
    record_t *      first;          /* this it the actual data */
    char            fingerprint[20];/* this is the hash of the data */
    char    flags;                  /* the status of this entry */
#define EMPTY           0
#define IDENTITY        1
#define MODIFIED        2
#define LOADED          0x10
} entry_t;

typedef struct {
    time_t  start_time;
    int     records;
    int     records_away;
    time_t  end_time;
} cookstats_t;

typedef struct {
    cookstats_t     cook;

    time_t  comb_start_time;
    int     authoritative_records;
    time_t  comb_end_time;

    time_t  diff_start_time;
    int     num_identities;
    int     num_unchanged;
    int     num_deletes;
    int     num_adds;
    int     num_modifies;
    time_t  diff_end_time;

    cookstats_t     uncook;
} stats_t;

extern int mm_init(int argc, char * argv[]);
extern int mm_diff(stats_t *statsp);

extern int mm_getvalue(
    record_t  *first, 
    attrib1_t *a, 
    int       directory, 
    char      *name, 
    char      **value, 
    int       *length
);

extern int mm_is_deleted(
    record_t  *first, 
    attrib1_t *a, 
    int       directory
);

extern int mm_get_winner(record_t *first, attrib1_t *a);
extern void mm_init_winner(void);
extern void mm_fin_winner(void);

/*
 * Private declarations.
 */

#define log_write_error() fprintf(stderr, "error writing record\n")

/*
 * We need to maintain the order of entries read from input, so that
 * we can maintain hierarchical ordering. The entryblock structure
 * is used for that purpose. Memory for blocks of entries are allocated
 * and strung in a linked list.
 */
struct entryblock {
    entry_t *eb_block;
    unsigned n;
    struct entryblock *next;
};

static struct entryblock *eb_head = NULL, *eb_cur = NULL;

entry_t *entryalloc(void)
{
    if (eb_head == NULL || eb_cur->n == 0x1000) {
        struct entryblock *newblock;
        newblock = 
            (struct entryblock *)calloc(1, sizeof(struct entryblock));
        newblock->eb_block = (entry_t*)calloc(0x1000, sizeof(entry_t));
        if (eb_head == NULL) {
            eb_cur = eb_head = newblock;
        } else {
            eb_cur = eb_cur->next = newblock;
        }
    }
    return &eb_cur->eb_block[eb_cur->n++];
}

typedef struct {
	FILE *	fp;
	int	end;
} edfFILE;

static int	ndirectories;
static edfFILE	edfin[128];
static FILE *	edfout[128];
static FILE *   ofp;
static char	line[2048];
static char	seed;
static int	hashmask;
static entry_t **hashtable;
static int      emitchanges;

static int readrec(edfFILE * edf1, attrib1_t ** attrib); 
static void freefreelist(attrib1_t * freelist);
static void hashname(char seed, attrib1_t * attrib, char * hashkey);
static void hashvalue(char seed, attrib1_t * attrib, char * fingerprint);
static record_t * newrecord(attrib1_t * big);
static int adddelete(FILE * edf3, attrib1_t * attrib);
static int addnew(FILE * edf3, const char *changetype, record_t * first);
static int addmodified(FILE * edf3, attrib1_t * attrib, record_t * first);
static int simpletext(unsigned char * body, int length);
static int simpletextbody(unsigned char * body, int length);
static int putvalue(
    FILE * fh, 
    const char *tag, 
    char * name, 
    int namelen, 
    char * value, 
    int valuelen
);
static int signedmemcmp(
    unsigned char * a, 
    int lena, 
    unsigned char * b, 
    int lenb
);
static void makeupper(MM_VSTRING * v, char * body, int len);

static void commententry(FILE *fp, attrib1_t *attrib);

int mm_diff(stats_t *statsp)
{
    unsigned int h;
    entry_t *   overflow;
    int i;
    int pindex;
    int pmask;
    attrib1_t * attrib = 0;
    entry_t *   hashentry;
    entry_t *   hashentry2;
    char        fingerprint[16];
    int stat;
    int count;
    int records = 0;
    int added;
    struct entryblock *block, *next;

    union {
        unsigned int    key;
        char    data[16];
    } hashkey;

    unsigned int        key;

    time(&statsp->diff_start_time);
    license_count = 0;

    NSS_NoDB_Init(".");

/*
 *      read all entries from all directories hashing name and value, and make
 *      a bitmaps of who has each entry.  Flag those entries where at least
 *      one directory differs from any other.
 */
    for (i = 0; i < ndirectories; i++) {
        pindex = i / 32;
        pmask = 1 << (i % 32);
        LDAPDebug(LDAP_DEBUG_TRACE, "finger printing directory %d\n", i, 0, 0);
        while (TRUE) {
            stat = readrec(&edfin[i], &attrib);
            if (stat == IDDS_MM_ABSENT) {
                LDAPDebug(LDAP_DEBUG_TRACE, "ignored: %s: %s\n", 
                                 attrib->name, attrib->value, 0);
                continue;
            }
            if (stat == IDDS_MM_EOF)
                break;
            if (stat != IDDS_MM_OK) {
                free(hashtable);
                return stat;
            }
            records++;
            LDAPDebug(LDAP_DEBUG_TRACE, "db%d: %s: %s\n", 
                             i, attrib->name, attrib->value);
            hashname(seed, attrib, hashkey.data);
            key = hashkey.key & hashmask;
            if (!hashtable[key]) {
                hashentry = hashtable[key] = entryalloc();
            } else {
                hashentry = hashtable[key];
                while (hashentry && 
                       memcmp(hashkey.data, hashentry->key, 16))
                    hashentry = hashentry->overflow;
                if (hashentry != NULL) {
                    if (hashentry->present[pindex] & pmask) {
                        LDAPDebug(LDAP_DEBUG_TRACE, 
                                         "duplicate DN <%s=%s> (ignored)\n",
                                         attrib->name, attrib->value, 0);
                        if (emitchanges) {
                            fprintf(edfout[i], "\n# Duplicate DN:\n");
                            commententry(edfout[i], attrib);
                        }
                        if (ofp != NULL) {
                            fprintf(ofp, "\n# Duplicate DN (in database %d):\n",
                                    i);
                            commententry(ofp, attrib);
                        }
                    } else {
                        hashentry->present[pindex] |= pmask;
                        hashvalue(seed, attrib, fingerprint);
                        if (memcmp(fingerprint, hashentry->fingerprint, 16)) {
                            LDAPDebug(LDAP_DEBUG_TRACE, 
				      "...data modified\n", key, 0, 0);
                            hashentry->flags = MODIFIED;
                        }
                    }
                    continue;
                }
                LDAPDebug(LDAP_DEBUG_TRACE, "overflow in key %u\n", key, 0, 0);
                hashentry2 = entryalloc();
                hashentry2->overflow = hashtable[key];
                hashentry = hashtable[key] = hashentry2;
            }
            hashentry->present[pindex] |= pmask;
            memcpy(hashentry->key, hashkey.data, 16);
            hashentry->flags = IDENTITY;
            statsp->num_identities++;
            hashvalue(seed, attrib, hashentry->fingerprint);
        }
        if ((license_limit > 0) && (records > license_limit)) {
            fprintf(stderr, "license exceeded\n");
            free(hashtable);
            return IDDS_MM_BAD;
        }
        if (records > license_count)
            license_count = records;
        records = 0;
    }

/*
 *      read all the directories again.  This time we load the data into memory
 *      We use a fairly tight (and ugly) structure to hold the data.
 *      There are three possibilities to consider:
 *              1. no data has yet been loaded for this entry (load it)
 *              2. data is present, and the data is marked as an identity
 *                 (skip it)
 *              3. data is present, and the data differs in at least one
 *                 directory.  call out to see who wins.
 */
    for (i = 0; i < ndirectories; i++) {
        rewind(edfin[i].fp);
        edfin[i].end = FALSE;
        pindex = i / 32;
        pmask = 1 << (i % 32);

        LDAPDebug(LDAP_DEBUG_TRACE, 
		  "loading authoritative data from directory %d\n", i, 0, 0);
        count = 0;
        while (TRUE) {
            stat = readrec(&edfin[i], &attrib);
            if (stat == IDDS_MM_ABSENT) {
                LDAPDebug(LDAP_DEBUG_TRACE, "ignored: %s: %s\n", 
			  attrib->name, attrib->value, 0);
                    continue;
            }
            if (stat == IDDS_MM_EOF)
                break;
            if (stat != IDDS_MM_OK) {
                free(hashtable);
                return stat;
            }
            LDAPDebug(LDAP_DEBUG_TRACE, "db%d: %s: %s\n", 
		      i, attrib->name, attrib->value);
            hashname(seed, attrib, hashkey.data);
            key = hashkey.key & hashmask;
            hashentry = hashtable[key];
            while (hashentry && 
                   memcmp(hashentry->key, hashkey.data, 16))
                hashentry = hashentry->overflow;
            if (hashentry == NULL) {
                LDAPDebug(LDAP_DEBUG_TRACE, "...hash entry not found\n", 0, 0, 0);
                    continue;
            }
            if (!(hashentry->flags & LOADED))
            {
                count++;
                hashentry->first = newrecord(attrib);
                hashentry->flags |= LOADED;
                LDAPDebug(LDAP_DEBUG_TRACE, " ...data loaded\n", 0, 0, 0);
                    hashentry->db = i;
                continue;
            }
            if (hashentry->flags & IDENTITY)
                continue;
            if (mm_get_winner(hashentry->first, attrib)) {
                hashentry->flags |= LOADED;
                LDAPDebug(LDAP_DEBUG_TRACE, " ...winner data loaded\n", 0, 0, 0);
                hashentry->db = i;
                free(hashentry->first);
                hashentry->first = newrecord(attrib);
                hashvalue(seed, attrib, hashentry->fingerprint);
                                /* must take new fingerprint */
                continue;
            }
        }
    }

    if (!emitchanges) goto afterchanges;

/*
 *      Now we have the "authoritative" data in memory.  Hey, that's what
 *      VM is for.  Now we are able to go through each directory (again)
 *      and generate the differences.  There are a number of possibilities
 *              1. the entry is marked as an identity.  skip it
 *              2. the entry is marked as originating from this directory
 *                 skip it
 *              3. the entry's finger print is unchanged.  skip it
 *              4. the entry has isDeleted set.  emit a delete
 *              5. otherwise emit a change record.
 */
    for (i = 0; i < ndirectories; i++) {
        rewind(edfin[i].fp);
        edfin[i].end = FALSE;
        pindex = i / 32;
        pmask = 1 << (i % 32);

        LDAPDebug(LDAP_DEBUG_TRACE, 
		  "generating differences for directory %d\n", i, 0, 0);
        count = 0;
        while (TRUE) {
            stat = readrec(&edfin[i], &attrib);
            if (stat == IDDS_MM_ABSENT) {
                LDAPDebug(LDAP_DEBUG_TRACE, "ignored: %s: %s\n", 
			  attrib->name, attrib->value, 0);
                continue;
            }
            if (stat == IDDS_MM_EOF)
                break;
            if (stat != IDDS_MM_OK) {
                free(hashtable);
                return stat;
            }
            LDAPDebug(LDAP_DEBUG_TRACE, "db%d: %s: %s\n", 
		      i, attrib->name, attrib->value);
            hashname(seed, attrib, hashkey.data);
            key = hashkey.key & hashmask;
            hashentry = hashtable[key];
            while (hashentry && 
                   memcmp(hashentry->key, hashkey.data, 16))
                hashentry = hashentry->overflow;
            if (hashentry == NULL) {
                LDAPDebug(LDAP_DEBUG_TRACE, "...hash entry not found\n", 0, 0, 0);
                    continue;
            }
            if (hashentry->flags & IDENTITY)
                continue;
            if (hashentry->db == i)
                continue;
            hashvalue(seed, attrib, fingerprint);
            if (memcmp(fingerprint, hashentry->fingerprint, 16)) {
                if (mm_is_deleted(hashentry->first, attrib, 0)) {
                    LDAPDebug(LDAP_DEBUG_TRACE, " ...deleted\n", 0, 0, 0);
                    adddelete(edfout[i], attrib);
                } else {
                    LDAPDebug(LDAP_DEBUG_TRACE, " ...modified\n", 0, 0, 0);
                    addmodified(edfout[i], attrib, hashentry->first);
                }
            }
        }
    }

 afterchanges:

/*
 *      Nearly done.  Now we need to go through each entry in the hash table
 *      and for each directory check the "present" bit.  If this is set
 *      no action is needed here.  Otherwise we emit an add.
 *      we take this opportunity to free the memory.
 */
    LDAPDebug(LDAP_DEBUG_TRACE, "scanning db for new entries\n", 0, 0, 0);
    for (h = 0; h < 0x1000; h++) {
        for (hashentry = hashtable[h]; hashentry; hashentry = overflow) {
            if (!hashentry->flags)
                break;

            for (i = 0, added = 0; i < ndirectories; i++) {
                pindex = i / 32;
                pmask = 1 << (i % 32);
                if (hashentry->present[pindex] & pmask)
                    continue;
                if (mm_is_deleted(hashentry->first, NULL, 0)) continue;
                added++;
                if (!emitchanges) continue;
                LDAPDebug(LDAP_DEBUG_TRACE, " ...add new\n", 0, 0, 0);
                addnew(edfout[i], "add", hashentry->first);
            }

            if (added) {
                statsp->num_adds++;
            } else if (hashentry->flags & MODIFIED) {
                statsp->num_modifies++;
            } else  {
                statsp->num_unchanged++;
            }

            overflow = hashentry->overflow;
        }
    }

    /* output authoritative data and free data */
    for (block = eb_head; block != NULL; block = next) {
        entry_t *entry;
        for (h = 0; h < block->n; h++) {
            entry = &block->eb_block[h];
            if (ofp != NULL) {
                if (!mm_is_deleted(entry->first, NULL, 0)) {
                    addnew(ofp, NULL, entry->first);
                }
            }
            free(entry->first);
        }
        next = block->next;
        free(block->eb_block);
        free(block);
    }

    free(hashtable);
    time(&statsp->diff_end_time);
    return IDDS_MM_OK;
}

static void usage(char *m)
{
    fprintf(stderr,"usage: %s [-c] [-D] [-o out.ldif] in1.ldif in2.ldif ...\n\n", m);
    fprintf(stderr,"-c\tWrite a change file (.delta) for each input file\n");
    fprintf(stderr,"-D\tPrint debugging information\n");
    fprintf(stderr,"-o\tWrite authoritative data to this file\n");
    fprintf(stderr,"\n");
    exit(1);
}

int mm_init(int argc, char * argv[])
{
    char	deltaname[255];
    time_t	tl;
    int c;
    char *ofn = NULL;
    char *prog = argv[0];

    time(&tl);
    seed = (char)tl;
    ndirectories = 0;
    emitchanges = 0;
    ofp = NULL;

    mm_init_winner();

    slapd_ldap_debug = 0;

    while ((c = getopt(argc, argv, "cDho:")) != EOF) {
        switch (c) {
        case 'c':
            emitchanges = 1;
            break;
        case 'D':
            slapd_ldap_debug = 65535;
            break;
        case 'o':
            ofn = strdup(optarg);
            break;
        case 'h':
        default:
            usage(prog);
            break;
        }
    }

#ifdef _WIN32
    module_ldap_debug = &slapd_ldap_debug;
    libldap_init_debug_level(&slapd_ldap_debug);
#endif

    if (ofn != NULL) {
        ofp = fopen(ofn, "w");
        if (ofp == NULL) {
            perror(ofn);
            return -1;
        }
        free(ofn);
        ofn = NULL;
    }

    for (argv += optind; optind < argc; optind++, argv++) {
        edfin[ndirectories].fp = fopen(*argv, "r");
        if (edfin[ndirectories].fp == NULL) {
            perror(*argv);
            return -1;
        }
        edfin[ndirectories].end = FALSE;

        if (emitchanges) {
            PL_strncpyz(deltaname, *argv, sizeof(deltaname));
            PL_strcatn(deltaname, sizeof(deltaname), ".delta");
            edfout[ndirectories] = fopen(deltaname, "w");
            if (edfout[ndirectories] == NULL) {
                perror(deltaname);
                return -1;
            }
        }
        ndirectories++;
    }

    if (ndirectories == 0) {
        fprintf(stderr, "\nno input files\n\n");
        usage(prog);
        return 0;
    }

    hashmask = 0xfff;
    hashtable = (entry_t **)calloc(0x1000, sizeof(entry_t*));
    return 0;
}

/* this clears the attrib structure if there is one, and reads in the data
 * sorting lines 2 to n by name, and eliminating comments
 */
static int 
readrec(edfFILE * edf1, attrib1_t ** attrib)
{
    Dec64_t *	b64;
    char *  vptr;
    char *  lptr;
    char *	ptr;
    int	len;
    int	lookahead = 0;
    int	toolong = FALSE;
    int	rc;
    int cmp;
    attrib1_t *	att;
    attrib1_t **	prev;
    attrib1_t *	freelist = *attrib;
    attrib1_t *	newlist = NULL;
    attrib1_t *	a;
    int		ignore_rec = FALSE;

    *attrib = NULL;
    if (edf1->end) {
        freefreelist(freelist);
        return IDDS_MM_EOF;
    }

    while (TRUE) {
        if (lookahead) {
            if (lookahead == '\n') {
                break; /* return */
            }
            line[0] = lookahead;
            lptr = line+1;
            lookahead = 0;
        }
        else
            lptr = line;
        if (!fgets(lptr, sizeof(line)-1, edf1->fp)) {
            edf1->end = TRUE;
            if (!newlist) {
                /* that's for the case where the file */
                /* has a trailing blank line */
                freefreelist(freelist);
                return IDDS_MM_EOF;
            }
            break; /* return */
        }
        if (line[0] == '\n') {
            /* ignore empty lines at head of LDIF file */
            if (newlist == NULL) {
                continue;
            }
            break; /* an empty line terminates a record */
        }
        if (line[0] == '#')
            continue;           /* skip comment lines */

        len = strlen(line);
        for (lptr = line+len-1; len; len--, lptr--) {
            if ((*lptr != '\n') && (*lptr != '\r'))
                break;
            *lptr = 0;
        }
        vptr = strchr(line, ':');
        if (!vptr) {
            LDAPDebug(LDAP_DEBUG_TRACE, "%s\n invalid input line\n", 
                             line, 0, 0);
            continue;           /* invalid line, but we'll just skip it */
        }
        *vptr = 0;
        if (!stricmp(line, "authoritative"))
            continue;
        if (!freelist) {
            att = (attrib1_t *)malloc(sizeof(attrib1_t));
        } else {
            att = freelist;
            freelist = freelist->next;
        }
        att->namelen = vptr-line;
		
        if (att->namelen > 63) {
            att->namelen = 63;
            *(line+64) = 0;
        }

        memcpy(att->name, line, att->namelen+1);
        vptr++;
        if (*vptr == ':') {
            vptr++;
            while (*vptr == ' ') vptr++; /* skip optional spaces */
            b64 = initDec64((unsigned char *)att->value, 0x20000);
            if (Dec64(b64, (unsigned char *) vptr)) {
                LDAPDebug(LDAP_DEBUG_TRACE, "%s\n invalid input line\n", 
			  line, 0, 0);
                continue;       /* invalid line, but we'll just skip it */
            }
            toolong = FALSE;
            while (TRUE) {
                lookahead = fgetc(edf1->fp);
                if (lookahead != ' ')
                    break;
                (void)fgets(line, sizeof(line), edf1->fp);
                len = strlen(line);
                for (lptr = line+len-1; len; len--, lptr--) {
                    if ((*lptr != '\n') && (*lptr != '\r'))
                        break;
                    *lptr = 0;
                }
                rc = Dec64(b64, (unsigned char *)line);
                if (rc == -1)
                {
                    LDAPDebug(LDAP_DEBUG_TRACE, 
			      "%s\n invalid input line\n", line, 0, 0);
                    continue;   /* invalid line, but we'll just skip it */
                }

                if (rc) {
                    if (!toolong) {
                        toolong = TRUE;
                        LDAPDebug(LDAP_DEBUG_TRACE, 
				  "%s\n line too long\n", line, 0, 0);
                    }
                    continue;
                }
            }
            att->valuelen = freeDec64(b64);
        } else {
            if (!*vptr) {
                att->valuelen = 0;
            }
            while (*vptr == ' ') vptr++; /* skip optional spaces */

            att->valuelen = strlen(vptr);
            memcpy(att->value, vptr, att->valuelen);
            ptr = att->value + att->valuelen;
            while (TRUE) {
                lookahead = fgetc(edf1->fp);
                if (lookahead != ' ')
                    break;
                (void)fgets(line, sizeof(line), edf1->fp);
                len = strlen(line);
                for (lptr = line+len-1; len; len--, lptr--) {
                    if ((*lptr != '\n') && (*lptr != '\r'))
                        break;
                    *lptr = 0;
                }
                memcpy(ptr, line, len);
                att->valuelen += len;
                ptr += len;
            }
            *ptr = 0;
        }
			
        if (newlist) {
            if (newlist->next) {
                for (a = newlist->next, prev = &(newlist->next);
                     a; prev=&(a->next), a = a->next) {
                    cmp = stricmp(a->name, att->name);
                    if (cmp > 0) {
                        att->next = *prev;
                        *prev = att;
                        goto f1;
                    }
                    if (cmp == 0) {
                        cmp = signedmemcmp((unsigned char *)a->value, 
                                           a->valuelen, 
                                           (unsigned char *)att->value, 
                                           att->valuelen);
                        if (cmp > 0) {
                            att->next = *prev;
                            *prev = att;
                            goto f1;
                        }
                    }
                }
                *prev = att;
                att->next = NULL;
            f1:			;
            } else {
                newlist->next = att;
                att->next = NULL;
            }
        } else {
            newlist = att;
            att->next = NULL;
        }
    }
    *attrib = newlist;
    freefreelist(freelist);
    if (ignore_rec)
        return IDDS_MM_ABSENT;
    return IDDS_MM_OK;
}

static void
freefreelist(attrib1_t * freelist)
{
    attrib1_t *	next;
    for (;freelist; freelist = next) {
        next = freelist->next;
        free(freelist);
    }
}

static void 
hashname(char seed, attrib1_t * attrib, char * hashkey)
{
    MM_VSTRING	upper;
    PK11Context	*context;
	unsigned int hashLen;
	
/* we want the name to be case insensitive, and if the name DN, we want 
 * the value to be case insensitive. */
/* this creates a hash key based on the first line in attrib */
    makeupper(&upper, attrib->name, attrib->namelen);
    context = PK11_CreateDigestContext(SEC_OID_MD5);
	if (context != NULL) {
    	PK11_DigestBegin(context);
    	PK11_DigestOp(context, (unsigned char *)&seed, 1);
    	PK11_DigestOp(context, (unsigned char *)upper.body, upper.length);
    	PK11_DigestOp(context, (unsigned char *)"=", 1);
    	if (!memcmp(upper.body, "DN", 2)) {
        	makeupper(&upper, attrib->value, attrib->valuelen);
        	PK11_DigestOp(context, (unsigned char *)upper.body, upper.length);
    	} else
        	PK11_DigestOp(context, (unsigned char *)attrib->value, attrib->valuelen);
    	PK11_DigestFinal(context, (unsigned char *)hashkey, &hashLen, 16);
    	PK11_DestroyContext(context, PR_TRUE);
	}
	else { /* Probably desesperate but at least deterministic... */
		memset(hashkey, 0, 16);
	}
}

/* this creates a hash key base on all but the first line in attrib */
static void 
hashvalue(char seed, attrib1_t * attrib, char * fingerprint)
{
    MM_VSTRING	upper;
    attrib1_t *	a;
    PK11Context	*context;
	unsigned int fgLen;

    context = PK11_CreateDigestContext(SEC_OID_MD5);
	if (context != NULL) {
    	PK11_DigestBegin(context);
    	PK11_DigestOp(context, (unsigned char *)&seed, 1);
    	for (a = attrib->next; a; a = a->next) {
        	if (!stricmp(a->name, "authoritative"))
            	continue;
	/* we want the name to be case insensitive, and if the name DN, we want 
	 * the value to be case insensitive. */
        	makeupper(&upper, a->name, a->namelen);
        	PK11_DigestOp(context, (unsigned char *)upper.body, upper.length);
        	PK11_DigestOp(context, (unsigned char *)"=", 1);
        	if (!memcmp(upper.body, "DN", 2)) {
            	makeupper(&upper, a->value, a->valuelen);
            	PK11_DigestOp(context, (unsigned char *)upper.body, upper.length);
        	} else
            	PK11_DigestOp(context, (unsigned char *)a->value, a->valuelen);
        	PK11_DigestOp(context, (unsigned char *)";", 1);
    	}
    	PK11_DigestFinal(context, (unsigned char *)fingerprint, &fgLen, 16);
    	PK11_DestroyContext(context, PR_TRUE);
	}
	else { /* Probably desesperate but at least deterministic... */
		memset(fingerprint, 0, 16);
	}
}

/* this writes a record deletion record based on the first line in attrib */
static int 
adddelete(FILE * edf3, attrib1_t * attrib)
{
    if (!putvalue(edf3, NULL, attrib->name, attrib->namelen,
                  attrib->value, attrib->valuelen)) {
        log_write_error();
        return IDDS_MM_FIO;
    }
    fprintf(edf3, "changetype: delete\n\n");
    return IDDS_MM_OK;
}

/* this writes a record addition record based on attrib */
static int 
addnew(FILE * edf3, const char *changetype, record_t * first)
{
    attrib_t *	att;
    int	attnum;

    for (attnum = 1, att = &first->data;
         attnum <= first->nattrs;
         attnum++, att = attribnext(att)) {
        if (!stricmp(attribname(att), "modifytimestamp"))
            continue;
        if (!stricmp(attribname(att), "modifiersname"))
            continue;
        if (!putvalue(edf3, NULL, attribname(att), att->namelen,
                      attribvalue(att), att->valuelen)) {
            log_write_error();
            return IDDS_MM_FIO;
        }
        if (attnum == 1 && changetype != NULL) {
            fprintf(edf3, "changetype: %s\n", changetype);
        }
    }
    if (fputs("\n", edf3) < 0) {
        log_write_error();
        return IDDS_MM_FIO;
    }
    return IDDS_MM_OK;
}

/* this writes a record modification record based on the information in
 * first and attrib
 */
static int 
addmodified(FILE * edf3, attrib1_t * attrib, record_t * first)
{
    attrib_t *b;
    attrib1_t *a;
    int	num_b;
    int tot_b;
    int	cmp;
    char *attrname;

    if (!putvalue(edf3, NULL, attrib->name, attrib->namelen,
                  attrib->value, attrib->valuelen)) {
        log_write_error();
        return IDDS_MM_FIO;
    }
    if (fputs("changetype: modify\n", edf3) < 0) {
        log_write_error();
        return IDDS_MM_FIO;
    }

    tot_b = first->nattrs;
    num_b = 1;
    b = &first->data;

    /* advance past dn attrs */
    a = attrib->next;
    b = attribnext(b); num_b++;

    /* 
     * Lock-step through the two attr lists while there are still
     * attrs remaining in either.
     */
    while (a != NULL || num_b <= tot_b) {
        /* ignore operational attrs */
        if (num_b <= tot_b &&
            (stricmp(attribname(b), "modifytimestamp") == 0 ||
             stricmp(attribname(b), "modifiersname") == 0)) {
            b = attribnext(b); num_b++;
            continue;
        }
        if (a != NULL &&
            (stricmp(a->name, "modifytimestamp") == 0 ||
             stricmp(a->name, "modifiersname") == 0)) {
            a = a->next;
            continue;
        }

        if (num_b > tot_b) {
            cmp = -1;
        } else if (a == NULL) {
            cmp = 1;
        } else {
            cmp = stricmp(a->name, attribname(b));
        }
        if (cmp < 0) {
            /* a < b: a is deleted */
            attrname = a->name;
            fprintf(edf3, "delete: %s\n-\n", attrname);
            do {
                a = a->next;
            } while (a != NULL && stricmp(a->name, attrname) == 0);
            continue;
        } else if (cmp > 0) {
            /* a > b: b is added */
            attrname = attribname(b);
            fprintf(edf3, "add: %s\n", attrname);
            do {
                if (!putvalue(edf3, NULL, attribname(b), b->namelen,
                              attribvalue(b), b->valuelen)) {
                    log_write_error();
                    return IDDS_MM_FIO;
                }
                b = attribnext(b); num_b++;
            } while (num_b <= tot_b && stricmp(attribname(b), attrname) == 0);
            fprintf(edf3, "-\n");
            continue;
        } else {
            /* a == b */
            int nmods = 0;
            attrib_t *begin_b = b;
            attrib1_t *v_del = NULL;
            attrib_t *v_add = NULL;
            int begin_num_b = num_b;

            /*
             * Lock-step through the ordered values.
             * Remember a maximum of one changed value.
             * If we encounter more than one change then
             * just issue a replace of the whole value.
             */
            attrname = a->name;
            do {
                if (num_b > tot_b || stricmp(attribname(b), attrname) != 0) {
                    cmp = -1;
                } else if (a == NULL || stricmp(a->name, attrname) != 0) {
                    cmp = 1;
                } else {
                    cmp = signedmemcmp((unsigned char *)a->value, 
                                       a->valuelen,
                                       (unsigned char *)attribvalue(b), 
                                       b->valuelen);
                }
                if (cmp < 0) {
                    nmods++;
                    v_del = a;
                    a = a->next;
                } else if (cmp > 0) {
                    nmods++;
                    v_add = b;
                    b = attribnext(b); num_b++;
                } else {
                    a = a->next;
                    b = attribnext(b); num_b++;
                }
            } while ((a != NULL && 
                      stricmp(a->name, attrname) == 0) ||
                     (num_b <= tot_b && 
                      stricmp(attribname(b), attrname) == 0));
            if (nmods == 1) {
                if (v_add != NULL) {
                    if (!putvalue(edf3, "add", 
                                  attribname(v_add), v_add->namelen,
                                  attribvalue(v_add), v_add->valuelen)) {
                        log_write_error();
                        return IDDS_MM_FIO;
                    }
                } else {
                    if (!putvalue(edf3, "delete",
                                  v_del->name, v_del->namelen,
                                  v_del->value, v_del->valuelen)) {
                        log_write_error();
                        return IDDS_MM_FIO;
                    }
                }
            } else if (nmods > 1) {
                fprintf(edf3, "replace: %s\n", attrname);
                do {
                    if (!putvalue(edf3, NULL, 
                                  attribname(begin_b), begin_b->namelen,
                                  attribvalue(begin_b), begin_b->valuelen)) {
                        log_write_error();
                        return IDDS_MM_FIO;
                    }
                    begin_b = attribnext(begin_b); begin_num_b++;
                } while (begin_num_b <= tot_b && begin_b != b);
                fprintf(edf3, "-\n");
            }
        }
    }

    if (fputs("\n", edf3) < 0) {
        log_write_error();
        return IDDS_MM_FIO;
    }
    return IDDS_MM_OK;
}

static record_t * newrecord(attrib1_t * big)
{
    record_t * smll;
    attrib_t * b;
    attrib1_t *	a;
    int	len = 0;
    int	count = 0;

    for (a=big; a; a = a->next) {
        count++;
        len += (a->namelen + a->valuelen + sizeof(attrib_t) + attribalign) & 
            ~ (attribalign-1);
    }
    len += sizeof(short);
    smll = (record_t *)malloc(len);
	
    for (a=big, b=&smll->data; a; a = a->next, b = attribnext(b)) {
        b->valuelen = a->valuelen;
        b->namelen = a->namelen;
        memcpy(attribname(b), a->name, a->namelen+1);
        memcpy(attribvalue(b), a->value, a->valuelen+1);
    }
    smll->nattrs = count;
    return smll;
}

static int
simpletextbody(unsigned char * body, int length)
{
    int	i;
    for (i = length; --i >= 0; body++) {
        if ((*body < ' ') || (*body >= 0x7f))
            return FALSE;
    }
    return TRUE;
}

static int
simpletext(unsigned char * body, int length)
{
    if ((*body == ':') || (*body == '<') || (*body == ' '))
        return FALSE;
    return simpletextbody(body, length);
}

/* output a string value */
static int
putvalue(
    FILE * fh, 
    const char * tag, 
    char * name, 
    int    namelen, 
    char * value, 
    int    valuelen
)
{
    Enc64_t *	b64;
    char *	lptr;
    char	line[255];
    int	return_code;
    int	len;
    char *	sptr;
    int	rc;

    lptr = line;
    if (tag != NULL) {
        sprintf(lptr, "%s: ", tag);
        lptr += strlen(lptr);
        memcpy(lptr, name, namelen);
        lptr += namelen;
        *lptr++ = '\n';
    }

    memcpy(lptr, name, namelen);
    lptr += namelen;
    *lptr++ = ':';

    if (!valuelen) {
        *lptr = '\n';
        *(lptr+1) = 0;
        return_code = fputs(line, fh);
        goto return_bit;
    }

    if (simpletext((unsigned char *)value, valuelen)) {
        *lptr = ' ';
        if (valuelen + (lptr+1 - line) < 80) {
            strcpy(lptr+1, value);
            strcpy(lptr+1 + valuelen, "\n");
            return_code = fputs(line, fh);
            goto return_bit;
        }
        len = 80 - (lptr+1 - line);
        memcpy(lptr+1, value, len);
        line[80] = '\n';
        line[81] = 0;
        return_code = fputs(line, fh);
        if (return_code < 0)
            goto return_bit;
        sptr = value + len;
        len = valuelen - len;
        line[0] = ' ';
        while (len > 79) {
            memcpy(line+1, sptr, 79);
            return_code = fputs(line, fh);
            if (return_code < 0)
                goto return_bit;
            sptr += 79;
            len -= 79;
        }
        if (len) {
            memcpy(line+1, sptr, len);
            line[len+1] = '\n';
            line[len+2] = 0;
            return_code = fputs(line, fh);
        }
        goto return_bit;
    }

    b64 = initEnc64((unsigned char *)value, valuelen);
    *lptr = ':';
    *(lptr+1) = ' ';
    rc = Enc64(b64, (unsigned char *)(lptr+2), 80-(lptr-line), &len);
    *(lptr +len+2) = '\n';
    *(lptr + len +3) = 0;
    return_code = fputs(line, fh);
    if (return_code < 0)
        goto return_bit;
    while (TRUE) {
        line[0] = ' ';
        rc = Enc64(b64, (unsigned char *)line+1, 79, &len);
        if (rc)
            break;
        line[len+1] = '\n';
        line[len+2] = 0;
        return_code = fputs(line, fh);
        if (return_code < 0)
            goto return_bit;
    }

 return_bit:
    if (tag != NULL) {
        fputs("-\n", fh);
    }
    if (return_code < 0)
        return FALSE;
    return TRUE;
}

static int
signedmemcmp(unsigned char * a, int lena, unsigned char * b, int lenb)
{
    int	c;

    for (;; a++, b++) {
        if (!lenb)
            return lena;
        if (!lena)
            return -1;
        if ((c=(int)*a - (int)*b))
            return c;
        lena--;
        lenb--;
    }
}

static void
makeupper(MM_VSTRING * v, char * body, int len)
{
    char *	vp;
    v->length = len;
    for (vp = v->body; len > 0; len--, vp++, body++)
        *vp = toupper(*body);
} 

int
mm_getvalue(
    record_t  *first, 
    attrib1_t *a, 
    int       directory, 
    char      *name, 
    char      **value, 
    int       *length
)
{
    int	attnum;
    attrib_t *	att;
    if (directory) {
        for ( ; a; a = a->next) {
            if (!stricmp(a->name, name)) {
                if (!*value) {
                    *value = a->value;
                    *length = a->valuelen;
                    return TRUE;
                } else {
                    if (*value == a->value)
                        *value = NULL;
                }
            }
        }
        return FALSE;
    }

    att = &first->data;

    for (attnum = 1, att = &first->data;
         attnum <= first->nattrs;
         attnum++, att = attribnext(att)) {
        if (!stricmp(attribname(att), name)) {
            if (!*value) {
                *value = attribvalue(att);
                *length = att->valuelen;
                return TRUE;
            } else {
                if (*value == attribvalue(att))
                    *value = NULL;
            }
        }
    }
    return FALSE;
}

int mm_is_deleted(
    record_t  *first, 
    attrib1_t *attrib, 
    int       directory
)
{
    char *      value = NULL;
    int         len;

    while (mm_getvalue(first, attrib, directory, 
                       "objectclass", 
                       &value, &len)) {
        if (stricmp(value, "nsTombstone") == 0) {
            return 1;
        }
    }

    if (mm_getvalue(first, attrib, directory, "isdeleted", &value, &len)) {
        if ((len == 1 && *value == '1') || 
            (len == 4 && stricmp(value, "true") == 0)) {
            return 1;
        }
    }

    if (mm_getvalue(first, attrib, directory, "zombi", &value, &len)) {
        return 1;
    }

    return 0;
}

static void commententry(FILE *fp, attrib1_t *attrib)
{
    attrib1_t *a;

    if (attrib == NULL) return;

    fprintf(fp, "#  %s: %s\n", attrib->name, attrib->value);
    for (a = attrib->next; a; a = a->next) {
        if (simpletext((unsigned char *)a->value, 
                       a->valuelen)) {
            fprintf(fp, "#  %*.*s: %*.*s\n",
                    a->namelen, a->namelen,
                    a->name,
                    a->valuelen, a->valuelen,
                    a->value);
        }
    }
    fprintf(fp, "\n");
}

int main(int argc, char *argv[])
{
    stats_t	stats;
    int	rc;
    float difftime;

    memset(&stats, 0, sizeof(stats));

    if ((rc = mm_init(argc, argv)))
        return rc;

    if ((mm_diff(&stats) == IDDS_MM_OK)
	&&  (license_limit > 0)) {
        if (license_count > license_limit * 98.0 / 100)
            fprintf(stderr, "That was over 98%% of your license limit.\n");
        else if (license_count > license_limit * 95.0 / 100)
            fprintf(stderr, "That was over 95%% of your license limit.\n");
        else if (license_count > license_limit * 90.0 / 100)
            fprintf(stderr, "That was over 90%% of your license limit.\n");
    }
    mm_fin_winner();
    printf("start time %s", ctime(&stats.diff_start_time));
    printf("\nentry counts: unchanged=%d changed=%d new=%d total=%d\n\n",
           stats.num_unchanged,
           stats.num_modifies,
           stats.num_adds,
           stats.num_identities);
    printf("end time %s", ctime(&stats.diff_end_time));

    difftime = stats.diff_end_time - stats.diff_start_time;
    if (difftime <= 1)
        printf("differencing took <= 1 second\n");
    else
        printf("differencing took %u seconds, %u records per second\n",
               (unsigned int)difftime, 
               (unsigned int)(stats.num_identities / difftime));
    exit(0);
}

/*
 * Conflict resolution.
 */

void mm_init_winner()
{
}

void mm_fin_winner()
{
}

int mm_get_winner(record_t * first, attrib1_t * a)
{
    int	len;
    char *	modified0 = NULL;
    char *	modified1 = NULL;

    mm_getvalue(first, a, 0, "modifytimestamp", &modified0, &len);
    mm_getvalue(first, a, 1, "modifytimestamp", &modified1, &len);

    if (!modified0) {
        mm_getvalue(first, a, 0, "createtimestamp", &modified0, &len);
    }

    if (!modified1) {
        mm_getvalue(first, a, 1, "createtimestamp", &modified1, &len);
    }

    if (!modified0) {
        mm_getvalue(first, a, 0, "deletetimestamp", &modified0, &len);
    }

    if (!modified1) {
        mm_getvalue(first, a, 1, "deletetimestamp", &modified1, &len);
    }

    if (!modified0)
        return 1;
    if (!modified1)
        return 0;
    return strcmp(modified0, modified1) <= 0;
}

/*
 * Base64 Implementation.
 */

                           /* 0123456789ABCDEF */
static unsigned char b64[] = "ABCDEFGHIJKLMNOP"
                             "QRSTUVWXYZabcdef"
                             "ghijklmnopqrstuv"
                             "wxyz0123456789+/";

static unsigned char ub64[] = {
/*	0	1	2	3	4	5	6	7
 *	8	9	A	B	C	C	E	F
 */
/*0-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*0-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*1-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*1-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*2-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*2-*/	0xFF,	0xFF,	0xFF,	62,	0xFF,	0xFF,	0xFF,	63,
/*3-*/	52,	53,	54,	55,	56,	57,	58,	59,
/*3-*/	60,	61,	0xFF,	0xFF,	0xFF,	64,	0xFF,	0xFF,
/*4-*/	0xFF,	0,	1,	2,	3,	4,	5,	6,
/*4-*/	7,	8,	9,	10,	11,	12,	13,	14,
/*5-*/	15,	16,	17,	18,	19,	20,	21,	22,
/*5-*/	23,	24,	25,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*6-*/	0xFF,	26,	27,	28,	29,	30,	31,	32,
/*6-*/	33,	34,	35,	36,	37,	38,	39,	40,
/*7-*/	41,	42,	43,	44,	45,	46,	47,	48,
/*7-*/	49,	50,	51,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*8-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*8-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*9-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*9-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*A-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*A-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*B-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*B-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*C-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*C-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*D-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*D-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*E-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*E-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*F-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,
/*F-*/	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF,	0xFF};


static Enc64_t * freeEnc64List = NULL;
static Dec64_t * freeDec64List = NULL;

Enc64_t *
initEnc64(unsigned char * source, int slen)
{
	Enc64_t *	this = freeEnc64List;
	if (this)
		freeEnc64List = freeEnc64List->next;
	else
		this = (Enc64_t *)malloc(sizeof(Enc64_t));
	this->source = source;
	this->slen = slen;
	return this;
	}

int
Enc64(Enc64_t * this, unsigned char *dest, int maxlen, int *len)
{
	/* returns 0 normally
	 *         +1 on end of string
	 *         -1 on badi
	 */
	int	l;
	unsigned char *	s;
	unsigned char *	d;
	int	reml;
	int	i;

	if (!this->slen)
		return 1;
	l = this->slen / 3;
	s = this->source;
	if (l > maxlen / 4)
	{
		l = maxlen / 4;
		this->slen -= l*3;
		reml = 0;
		this->source += l*3;
		}
	else
	{
		reml = this->slen % 3;
		this->slen = 0;
		}
	for (d = dest, i = 0; i < l; i++)
	{
		*d++ = b64[(*s >> 2) & 0x3f];
		*d++ = b64[((*s << 4) & 0x30) + ((*(s+1) >> 4) & 0x0f)];
		s++;
		*d++ = b64[((*s << 2) & 0x3c) + ((*(s+1) >> 6) & 0x03)];
		s++;
		*d++ = b64[*s & 0x3f];
		s++;
		}
	if (reml--)
		*d++ = b64[(*s >> 2) & 0x3f];
	else
	{
		*d = 0;
		*len = l*4;
		return 0;
		}
	if (reml)
	{
		*d++ = b64[((*s << 4) & 0x30) + ((*(s+1) >> 4) & 0x0f)];
		s++;
		*d++ = b64[((*s << 2) & 0x3c)];
		}
	else
	{
		*d++ = b64[((*s << 4) & 0x30) + ((*(s+1) >> 4) & 0x0f)];
		*d++ = '=';
		}
	*d++ = '=';
	*d = 0;
	*len = (l+1)*4;
	return 0;
	}

void
freeEnc64(Enc64_t *this)
{
	this->next = freeEnc64List;
	freeEnc64List = this;
	}

Dec64_t *
initDec64(unsigned char * dest, int maxlen)
{
	Dec64_t *	this = freeDec64List;
	if (this)
		freeDec64List = freeDec64List->next;
	else
		this = (Dec64_t *)malloc(sizeof(Dec64_t));
	this->dest = dest;
	this->maxlen = maxlen;
	this->curlen = 0;
	this->nextra = 0;
	return this;
	}

int
freeDec64(Dec64_t *this)
{
	this->next = freeDec64List;
	freeDec64List = this;
	return this->curlen;
	}

int
Dec64(Dec64_t * this, unsigned char *source)
{
	/* returns 0 normally
	 *         -1 on badi
	 *	   1  on too long
	 */
	unsigned char *	s;
	unsigned char *	d;
	unsigned char * e;
	unsigned char	s1, s2, s3, s4;
	int	i;
	int	slen;
	int	len;
	int	nextra;
	int	newnextra;
	unsigned char	newextra[3];

	nextra = this->nextra;
	slen = strlen((char *)source);
	len = (slen + nextra) / 4;
	newnextra = (slen + nextra) - len * 4;
	for (i = 0; i < newnextra; i++)
	{
		newextra[i] = source[slen-newnextra+i];
		}
	
	if (len * 3 > this->maxlen - this->curlen)
		return 1;
	for (d = this->dest + this->curlen, s = source, e = this->extra, i = 0;
			 i < len; i++)
	{
		if (nextra)
		{
			nextra--;
			s1 = ub64[*e++];
			}
		else
			s1 = ub64[*s++];
		if (nextra)
		{
			nextra--;
			s2 = ub64[*e++];
			}
		else
			s2 = ub64[*s++];
		if (nextra)
		{
			nextra--;
			s3 = ub64[*e++];
			}
		else
			s3 = ub64[*s++];
		s4 = ub64[*s++];
		if ((s1 | s2 | s3 | s4) & 0x80)
			return -1;
		*d++ = (s1 << 2) + (s2 >> 4);
		this->curlen++;
		if (s3 == 64)
			break;
		*d++ = (s2 << 4) + (s3 >> 2);
		this->curlen++;
		if (s4 == 64)
			break;
		*d++ = (s3 << 6) + s4;
		this->curlen++;
		}
	this->nextra = newnextra;
	memcpy(this->extra, newextra, 3);
	return 0;
	}
