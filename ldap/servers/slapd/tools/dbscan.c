/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2023 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
 * small program to scan a Directory Server db file and dump the contents
 *
 * TODO: indirect indexes
 */

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include "../back-ldbm/dbimpl.h"
#include "../slapi-plugin.h"
#include "nspr.h"
#include <netinet/in.h>
#include <inttypes.h>


#if (defined(hpux))
#ifdef _XOPEN_SOURCE_EXTENDED
#include <arpa/inet.h> /* for ntohl, et al. */
#endif
#endif

/* file type */
#define ENTRYTYPE 0x1
#define INDEXTYPE 0x2
#define VLVINDEXTYPE 0x4
#define CHANGELOGTYPE 0x8
#define ENTRYRDNINDEXTYPE 0x10

/* display mode */
#define RAWDATA 0x1
#define SHOWCOUNT 0x2
#define SHOWDATA 0x4
#define SHOWSUMMARY 0x8
#define LISTDBS 0x10
#define ASCIIDATA 0x20
#define EXPORT 0x40
#define IMPORT 0x80
#define REMOVE 0x100
#define SHOWSTAT 0x200

/* stolen from slapi-plugin.h */
#define SLAPI_OPERATION_BIND 0x00000001UL
#define SLAPI_OPERATION_UNBIND 0x00000002UL
#define SLAPI_OPERATION_SEARCH 0x00000004UL
#define SLAPI_OPERATION_MODIFY 0x00000008UL
#define SLAPI_OPERATION_ADD 0x00000010UL
#define SLAPI_OPERATION_DELETE 0x00000020UL
#define SLAPI_OPERATION_MODDN 0x00000040UL
#define SLAPI_OPERATION_MODRDN SLAPI_OPERATION_MODDN
#define SLAPI_OPERATION_COMPARE 0x00000080UL
#define SLAPI_OPERATION_ABANDON 0x00000100UL
#define SLAPI_OPERATION_EXTENDED 0x00000200UL
#define SLAPI_OPERATION_ANY 0xFFFFFFFFUL
#define SLAPI_OPERATION_NONE 0x00000000UL

/* changelog ruv info.  These correspond with some special csn
 * timestamps from cl5_api.c */
#define ENTRY_COUNT_KEY "0000006f" /* 111 csn timestamp */
#define PURGE_RUV_KEY "000000de"   /* 222 csn timestamp */
#define MAX_RUV_KEY "0000014d"     /* 333 csn timestamp */

#define ONEMEG (1024 * 1024)

#ifndef DB_BUFFER_SMALL
#define DB_BUFFER_SMALL ENOMEM
#endif

#define COUNTOF(array)    ((sizeof(array))/sizeof(*(array)))

#if defined(linux)
#include <getopt.h>
#endif

typedef uint32_t ID;

typedef struct
{
    uint32_t max;
    uint32_t used;
    uint32_t id[1];
} IDL;

/* back-ldbm.h and proto-back-ldbm.h cannot be easily included here
 * so let redefines the minimum to use txn and a few utilities
 */
typedef struct backend backend;
typedef void *back_txnid;
typedef struct back_txn {
    void *txn;
    void *foo[1];    /* just reserve enough space to store a txn */
} back_txn;
int dblayer_txn_begin(backend *be, back_txnid parent_txn, back_txn *txn);
int dblayer_txn_commit(backend *be, back_txn *txn);
int dblayer_txn_abort(backend *be, back_txn *txn);
void dblayer_init_pvt_txn(void);
void entryrdn_decode_data(backend *be, void *rdn_elem, ID *id, int *nrdnlen, char **nrdn, int *rdnlen, char **rdn);

#define RDN_BULK_FETCH_BUFFER_SIZE (8 * 1024)

static void display_entryrdn_item(dbi_db_t *db, dbi_cursor_t *cursor, dbi_val_t *key);

uint32_t file_type = 0;
uint32_t min_display = 0;
uint32_t display_mode = 0;
int truncatesiz = 0;
long pres_cnt = 0;
long eq_cnt = 0;
long app_cnt = 0;
long sub_cnt = 0;
long match_cnt = 0;
long ind_cnt = 0;
long allids_cnt = 0;
long other_cnt = 0;
char *dump_filename = NULL;
int do_it = 0;

static Slapi_Backend *be = NULL; /* Pseudo backend used to interact with db */

/* For Long options without shortcuts */
enum {
    OPT_FIRST = 0x1000,
    OPT_DO_IT,
    OPT_REMOVE,
};

static const struct option options[] = {
    /* Options without shortcut */
    { "do-it", no_argument, 0, OPT_DO_IT },
    { "remove", no_argument, 0, OPT_REMOVE },
    /* Options with shortcut */
    { "import", required_argument, 0, 'I' },
    { "export", required_argument, 0, 'X' },
    { "db-type", required_argument, 0, 'D' },
    { "dbi", required_argument, 0, 'f' },
    { "ascii", no_argument, 0, 'A' },
    { "raw", no_argument, 0, 'R' },
    { "truncate-entry", required_argument, 0, 't' },
    { "entry-id", required_argument, 0, 'K' },
    { "key", required_argument, 0, 'k' },
    { "list", required_argument, 0, 'L' },
    { "stats", required_argument, 0, 'S' },
    { "id-list-max-size", required_argument, 0, 'l' },
    { "id-list-min-size", required_argument, 0, 'G' },
    { "show-id-list-lenghts", no_argument, 0, 'n' },
    { "show-id-list", no_argument, 0, 'r' },
    { "summary", no_argument, 0, 's' },
    { "help", no_argument, 0, 'h' },
    { 0, 0, 0, 0 }
};


/** db_printf - functioning same as printf but a place for manipluating output.
*/
void
db_printf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

void
db_printfln(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fprintf(stdout, "\n");
}

uint32_t MAX_BUFFER = 4096;
uint32_t MIN_BUFFER = 20;

static IDL *
idl_make(dbi_val_t *data)
{
    IDL *idl = NULL, *xidl;

    if (data->size < 2 * sizeof(uint32_t)) {
        idl = (IDL *)malloc(sizeof(IDL) + 64 * sizeof(ID));
        if (!idl) {
            db_printf("Out of memory: Failed to alloc %d bytes.\n", sizeof(IDL) + 64 * sizeof(ID));
            exit(1);
        }
        idl->max = 64;
        idl->used = 1;
        idl->id[0] = *(ID *)(data->data);
        return idl;
    }

    xidl = (IDL *)(data->data);
    idl = (IDL *)malloc(data->size);
    if (!idl) {
        db_printf("Out of memory: Failed to alloc %d bytes.\n", data->size);
        exit(1);
    }

    memcpy(idl, xidl, data->size);
    return idl;
}

static void
idl_free(IDL *idl)
{
    idl->max = 0;
    idl->used = 0;
    free(idl);
}

static IDL *
idl_append(IDL *idl, ID id)
{
    size_t len;
    if (idl->used >= idl->max) {
        /* must grow */
        idl->max *= 2;
        len = sizeof(IDL) + idl->max * sizeof(ID);
        idl = realloc(idl, len);
        if (!idl) {
            db_printf("Out of memory: Failed to alloc %d bytes.\n", len);
            exit(1);
        }
    }
    idl->id[idl->used++] = id;
    return idl;
}


/* format a string for easy printing */
#define FMT_LF_OK 1
#define FMT_SP_OK 2
static char *
format_raw(unsigned char *s, int len, int flags, unsigned char *buf, int buflen)
{
    static char hex[] = "0123456789ABCDEF";
    unsigned char *p, *o, *bufend = buf + buflen - 1;
    int i;

    if (NULL == buf || buflen <= 0)
        return NULL;

    for (p = s, o = buf, i = 0; i < len && o < bufend; p++, i++) {
        int ishex = 0;
        if ((*p == '%') || (*p <= ' ') || (*p >= 126)) {
            /* index keys are stored with their trailing NUL */
            if ((*p == 0) && (i == len - 1))
                continue;
            if ((flags & FMT_LF_OK) && (*p == '\n')) {
                *o++ = '\n';
                *o++ = '\t';
            } else if ((flags & FMT_SP_OK) && (*p == ' ')) {
                *o++ = ' ';
            } else {
                *o++ = '%';
                *o++ = hex[*p / 16];
                *o++ = hex[*p % 16];
                ishex = 1;
            }
        } else {
            *o++ = *p;
        }
        if (truncatesiz > 0 && o > bufend - 5) {
            /* truncate it */
            /*
             * Padding " ...\0" at the end of the buf.
             * If dumped as %##, truncate the partial value if any.
             */
            o = bufend - 5;
            if (ishex) {
                if ((o > buf) && *(o - 1) == '%') {
                    o -= 1;
                } else if ((o > buf + 1) && *(o - 2) == '%') {
                    o -= 2;
                }
            }
            strcpy((char *)o, " ...");
            i = len;
            o += 4;
            break;
        }
    }
    *o = '\0';
    return (char *)buf;
}

static char *
format(unsigned char *s, int len, unsigned char *buf, int buflen)
{
    if (!s) {
        return format_raw((unsigned char*)"[NULL]", 6, 0, buf, buflen);
    }
    return format_raw(s, len, 0, buf, buflen);
}

static char *
format_entry(unsigned char *s, int len, unsigned char *buf, int buflen)
{
    return format_raw(s, len, FMT_LF_OK | FMT_SP_OK, buf, buflen);
}

static char *
idl_format(IDL *idl, int isfirsttime, int *done)
{
    static char *buf = NULL;
    static ID i = 0;

    if (buf == NULL) {
        buf = (char *)malloc(MAX_BUFFER);
        if (buf == NULL) {
            db_printf("Out of memory: Failed to alloc %d bytes.\n", MAX_BUFFER);
            exit(1);
        }
    }

    buf[0] = 0;
    if (0 != isfirsttime) {
        i = 0;
    }
    for (; i < idl->used; i++) {
        sprintf((char *)buf + strlen(buf), "%d ", idl->id[i]);

        if (strlen(buf) > MAX_BUFFER - MIN_BUFFER) {
            i++;
            done = 0;
            return (char *)buf;
        }
    }
    *done = 1;
    return (char *)buf;
}


/*** Copied from cl5_api.c: _cl5ReadString ***/
void
_cl5ReadString(char **str, char **buff)
{
    if (str) {
        int len = strlen(*buff);

        if (len) {
            *str = strdup(*buff);
            (*buff) += len + 1;
        } else /* just null char - skip it */
        {
            *str = NULL;
            (*buff)++;
        }
    } else /* just skip this string */
    {
        (*buff) += strlen(*buff) + 1;
    }
}

/** print_attr - print attribute name followed by one value.
    assume the value stored as null terminated string.
*/
void
print_attr(const char *attrname, char **buff)
{
    char *val = NULL;

    _cl5ReadString(&val, buff);
    if (attrname == NULL && val == NULL) {
        return;
    }
    db_printf("\t");

    if (attrname) {
        db_printf("%s: ", attrname);
    } else {
        db_printf("unknown attribute: ");
    }
    if (val) {
        db_printf("%s\n", val);
        free(val);
    } else {
        db_printf("\n");
    }
}

/*** Copied from cl5_api.c: _cl5ReadMods ***/
/* mods format:
   -----------
   <4 byte mods count><mod1><mod2>...

   mod format:
   -----------
   <1 byte modop><null terminated attr name><4 byte count>
   {<4 byte size><value1><4 byte size><value2>... ||
        <null terminated str1> <null terminated str2>...}
 */
void _cl5ReadMod(char **buff);

void
_cl5ReadMods(char **buff)
{
    char *pos = *buff;
    ID i;
    uint32_t mod_count;

    /* need to copy first, to skirt around alignment problems on certain
       architectures */
    memcpy((char *)&mod_count, *buff, sizeof(mod_count));
    mod_count = ntohl(mod_count);
    pos += sizeof(mod_count);


    for (i = 0; i < mod_count; i++) {
        _cl5ReadMod(&pos);
    }

    *buff = pos;
}


/** print_ber_attr - print one line of attribute, the value was stored
                     in ber format, length followed by string.
*/
void
print_ber_attr(char *attrname, char **buff)
{
    char *val = NULL;
    uint32_t bv_len;

    memcpy((char *)&bv_len, *buff, sizeof(bv_len));
    bv_len = ntohl(bv_len);
    *buff += sizeof(uint32);
    if (bv_len > 0) {

        db_printf("\t\t");

        if (attrname != NULL) {
            db_printf("%s: ", attrname);
        }

        val = malloc(bv_len + 1);
        if (!val) {
            db_printf("Out of memory: Failed to alloc %d bytes.\n", bv_len+1);
            exit(1);
        }
        memcpy(val, *buff, bv_len);
        val[bv_len] = 0;
        *buff += bv_len;
        db_printf("%s\n", val);
        free(val);
    }
}

/*
 *  Conversion routines between host byte order and
 *  big-endian (which is how we store data in the db).
 */
static ID
id_stored_to_internal(char *b)
{
    ID i;
    i = (ID)b[3] & 0x000000ff;
    i |= (((ID)b[2]) << 8) & 0x0000ff00;
    i |= (((ID)b[1]) << 16) & 0x00ff0000;
    i |= ((ID)b[0]) << 24;
    return i;
}

static void
id_internal_to_stored(ID i, char *b)
{
    if (sizeof(ID) > 4) {
        (void)memset(b + 4, 0, sizeof(ID) - 4);
    }

    b[0] = (char)(i >> 24);
    b[1] = (char)(i >> 16);
    b[2] = (char)(i >> 8);
    b[3] = (char)i;
}

void
_cl5ReadMod(char **buff)
{
    char *pos = *buff;
    uint32_t i;
    uint32_t val_count;
    char *type = NULL;

    pos++;
    _cl5ReadString(&type, &pos);

    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&val_count, pos, sizeof(val_count));
    val_count = ntohl(val_count);
    pos += sizeof(uint32_t);

    for (i = 0; i < val_count; i++) {
        print_ber_attr(type, &pos);
    }

    (*buff) = pos;
    free(type);
}

/* data format: <value count> <value size> <value> <value size> <value> ..... */
void
print_ruv(unsigned char *buff)
{
    char *pos = (char *)buff;
    uint32_t i;
    uint32_t val_count;

    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&val_count, pos, sizeof(val_count));
    val_count = ntohl(val_count);
    pos += sizeof(uint32_t);

    for (i = 0; i < val_count; i++) {
        print_ber_attr(NULL, &pos);
    }
}

/*
   *** Copied from cl5_api:cl5DBData2Entry ***
   Data in db format:
   ------------------
   <1 byte version><1 byte change_type><sizeof uint32_t time><null terminated dbid>
   <null terminated csn><null terminated uniqueid><null terminated targetdn>
   [<null terminated newrdn><1 byte deleteoldrdn>][<4 byte mod count><mod1><mod2>....]

Note: the length of time is set uint32_t instead of time_t. Regardless of the
width of long (32-bit or 64-bit), it's stored using 4bytes by the server [153306].

   mod format:
   -----------
   <0 byte modop><null terminated attr name><4 byte value count>
   <4 byte value size><value1><4 byte value size><value2>
*/
void
print_changelog(unsigned char *data, int len __attribute__((unused)))
{
    uint8_t version;
    uint8_t encrypted;
    unsigned long operation_type;
    char *pos = (char *)data;
    uint32_t thetime32;
    time_t thetime;
    uint32_t replgen;

    /* read byte of version */
    version = *((uint8_t *)pos);
    if (version != 5 && version != 6) {
        db_printf("Invalid changelog db version %i\nWorks for version 5 and 6 only.\n", version);
        exit(1);
    }
    pos += sizeof(version);

    if (version == 6) {
        /* process the encrypted flag */
        db_printf("\tencrypted: %s\n", *pos ? "yes" : "no");
        pos += sizeof(encrypted);
    }

    /* read change type */
    operation_type = (unsigned long)(*(uint8_t *)pos);
    pos++;

    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&thetime32, pos, sizeof(thetime32));

    replgen = ntohl(thetime32);
    pos += sizeof(uint32_t);
    thetime = (time_t)replgen;
    db_printf("\treplgen: %u %s", replgen, ctime((time_t *)&thetime));

    /* read csn */
    print_attr("csn", &pos);
    /* read UniqueID */
    print_attr("uniqueid", &pos);

    /* figure out what else we need to read depending on the operation type */
    switch (operation_type) {
    case SLAPI_OPERATION_ADD:
        print_attr("parentuniqueid", &pos);
        print_attr("dn", &pos);
        /* convert mods to entry */
        db_printf("\toperation: add\n");
        _cl5ReadMods(&pos);
        break;

    case SLAPI_OPERATION_MODIFY:
        print_attr("dn", &pos);
        db_printf("\toperation: modify\n");
        _cl5ReadMods(&pos);
        break;

    case SLAPI_OPERATION_MODRDN: {
        print_attr("dn", &pos);
        db_printf("\toperation: modrdn\n");
        print_attr("newrdn", &pos);
        db_printf("\tdeleteoldrdn: %d\n", (int)(*pos++));
        print_attr("newsuperior", &pos);
        _cl5ReadMods(&pos);
        break;
    }
    case SLAPI_OPERATION_DELETE:
        print_attr("dn", &pos);
        db_printf("\toperation: delete\n");
        break;

    default:
        db_printf("Failed to format entry\n");
        break;
    }
}

static void
display_index_item(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *data, unsigned char *buf, int buflen)
{
    IDL *idl = NULL;
    int ret = 0;

    idl = idl_make(data);

    if (file_type & VLVINDEXTYPE) {         /* vlv index file */
        if (1 > min_display) {              /* recno is always 1 */
            if (display_mode & SHOWCOUNT) { /* key  size=1 */
                printf("%-40s         1\n", format(key->data, key->size, buf, buflen));
            } else {
                printf("%-40s\n", format(key->data, key->size, buf, buflen));
            }
            if (display_mode & SHOWDATA) {
                dblayer_cursor_op(cursor, DBI_OP_GET_RECNO, key, data);
                if (data->data) {
                    printf("\t%5d\n", *(dbi_recno_t *)(data->data));
                } else {
                    printf("\tNO DATA\n");
                }
            }
        }
        goto index_done;
    }

    /* ordinary index file */
    /* fetch all other id's too */
    while (ret == 0) {
        ret = dblayer_cursor_op(cursor, DBI_OP_NEXT_DATA, key, data);
        if (ret == 0)
            idl = idl_append(idl, *(uint32_t *)(data->data));
    }
    if (ret == DBI_RC_NOTFOUND)
        ret = 0;
    if (ret != 0) {
        printf("Failure while looping dupes: %s\n", dblayer_strerror(ret));
        exit(1);
    }

    if (idl->max == 0) {
        /* allids; should not exist in the new idl world */
        if (allids_cnt == 0 && (display_mode & SHOWSUMMARY)) {
            printf("The following index keys reached allids:\n");
        }
        printf("%-40s(allids)\n", format(key->data, key->size, buf, buflen));
        allids_cnt++;
    } else {
        if (idl->used < min_display) {
            goto index_done;                   /* less than minimum display count */
        } else if (display_mode & SHOWCOUNT) { /* key  size */
            printf("%-40s%d\n",
                   format(key->data, key->size, buf, buflen), idl->used);
        } else if (!(display_mode & SHOWSUMMARY) || (display_mode & SHOWDATA)) {
            /* show keys only if show summary is not set or
                         * even if it's set, but with show data */
            printf("%-40s\n", format(key->data, key->size, buf, buflen));
        }
        if (display_mode & SHOWDATA) {
            char *formatted_idl = NULL;
            int done = 0;
            int isfirsttime = 1;
            while (0 == done) {
                formatted_idl = idl_format(idl, isfirsttime, &done);
                if (NULL == formatted_idl) {
                    done = 1; /* no more idl */
                } else {
                    if (1 == isfirsttime) {
                        printf("\t%s", formatted_idl);
                        isfirsttime = 0;
                    } else {
                        printf("%s", formatted_idl);
                    }
                }
            }
            printf("\n");
        }
    }
index_done:
    if (display_mode & SHOWSUMMARY) {
        char firstchar;

        firstchar = ((char *)key->data)[0];

        switch (firstchar) {
        case '+':
            pres_cnt += idl->used;
            break;

        case '=':
            eq_cnt += idl->used;
            break;

        case '~':
            app_cnt += idl->used;
            break;

        case '*':
            sub_cnt += idl->used;
            break;

        case ':':
            match_cnt += idl->used;
            break;

        case '\\':
            ind_cnt += idl->used;
            break;

        default:
            other_cnt += idl->used;
            break;
        }
    }
    idl_free(idl);
    return;
}

static void
display_item(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *data)
{
    static unsigned char *buf = NULL;
    static int buflen = 0;
    int tmpbuflen;

    if (truncatesiz > 0) {
        tmpbuflen = truncatesiz;
    } else if (file_type & INDEXTYPE) {
        /* +256: extra buffer for '\t' and '%##' */
        tmpbuflen = key->size + 256;
    } else {
        /* +1024: extra buffer for '\t' and '%##' */
        tmpbuflen = (key->size > data->size ? key->size : data->size) + 1024;
    }
    if (buflen < tmpbuflen) {
        buflen = tmpbuflen;
        buf = (unsigned char *)realloc(buf, buflen);
    }
    if (!buf) {
        printf("\t(malloc failed -- %d bytes)\n", buflen);
        return;
    }

    if (display_mode & RAWDATA) {
        printf("%s\n", format(key->data, key->size, buf, buflen));
        printf("\t%s\n", format(data->data, data->size, buf, buflen));
    } else {
        if (file_type & INDEXTYPE) {
            display_index_item(cursor, key, data, buf, buflen);
        } else if (file_type & CHANGELOGTYPE) {
            /* changelog db file */
            printf("\ndbid: %s\n", format(key->data, key->size, buf, buflen));
            if (strncasecmp((char *)key->data, ENTRY_COUNT_KEY, 8) == 0) {
                printf("\tentry count: %d\n", *(int *)data->data);
            } else if (strncasecmp((char *)key->data, PURGE_RUV_KEY, 8) == 0) {
                printf("\tpurge ruv:\n");
                print_ruv(data->data);
            } else if (strncasecmp((char *)key->data, MAX_RUV_KEY, 8) == 0) {
                printf("\tmax ruv:\n");
                print_ruv(data->data);
            } else {
                print_changelog(data->data, data->size);
            }
            return;
        } else if (file_type & ENTRYTYPE) {
            /* id2entry file */
            ID entry_id = id_stored_to_internal(key->data);
            printf("id %u\n", entry_id);
            printf("\t%s\n", format_entry(data->data, data->size, buf, buflen));
        } else {
            /* user didn't tell us what kind of file, dump it raw */
            printf("%s\n", format(key->data, key->size, buf, buflen));
            printf("\t%s\n", format(data->data, data->size, buf, buflen));
        }
    }
    return;
}

void
_entryrdn_dump_rdn_elem(const char *key, void *elem, int indent)
{
    char *indentp = (char *)malloc(indent + 1);
    char *nrdn = NULL;
    char *rdn = NULL;
    int nrdnlen = 0;
    int rdnlen = 0;
    ID id = 0;

    if (!indentp) {
        db_printf("Out of memory: Failed to alloc %d bytes.\n", indent+1);
        exit(1);
    }
    memset(indentp, ' ', indent);
    indentp[indent] = 0;
    entryrdn_decode_data(NULL, elem, &id, &nrdnlen, &nrdn, &rdnlen, &rdn);

    printf("%s\n", key);
    printf("%sID: %u; RDN: \"%s\"; NRDN: \"%s\"\n", indentp, id, rdn, nrdn);
    free(indentp);
}

static void
display_entryrdn_item(dbi_db_t *db __attribute__((unused)), dbi_cursor_t *cursor, dbi_val_t *key)
{
    void *elem = NULL;
    int indent = 2;
    dbi_val_t data = {0};
    int rc = 0;
    char buffer[RDN_BULK_FETCH_BUFFER_SIZE];
    dbi_op_t op = DBI_OP_MOVE_TO_FIRST;
    int find_key_flag = 0;
    const char *keyval = "";

    /* Setting the bulk fetch buffer */
    dblayer_value_set_buffer(be, &data, buffer, sizeof buffer);

    if (key->data) { /* key is given */
        /* Position cursor at the matching key */
        op = DBI_OP_MOVE_TO_KEY;
        find_key_flag = 1;
    }
    do {
        /* Position cursor at the matching key */
        rc = dblayer_cursor_op(cursor, op, key, &data);
        keyval = key->data;

        if (rc == DBI_RC_SUCCESS) {
            elem = data.data;
            _entryrdn_dump_rdn_elem(keyval, elem, indent);
            /* Then check if there are more data associated with current key */
            op = DBI_OP_NEXT_DATA;
            continue;
        }
        if (rc == DBI_RC_NOTFOUND && !find_key_flag && op == DBI_OP_NEXT_DATA) {
            /* no more data for this key and next key should be walked */
            rc = DBI_RC_SUCCESS;
            op = DBI_OP_NEXT_KEY;
            continue;
        }
    } while (rc == DBI_RC_SUCCESS);
    if (DBI_RC_BUFFER_SMALL == rc) {
        fprintf(stderr, "Entryrdn index is corrupt; "
                        "data item for key %s is too large for our "
                        "buffer (need=%ld actual=%ld)\n",
                        keyval, data.size, data.ulen);
    } else if (rc != DBI_RC_NOTFOUND || op == DBI_OP_MOVE_TO_KEY) {
        fprintf(stderr, "Failed to position cursor "
                        "at the key: %s: %s (%d)\n",
                        keyval, dblayer_strerror(rc), rc);
    }
}

static int
is_changelog(char *filename)
{
    char *ptr = NULL;
    int dashes = 0;
    int underscore = 0;
    if (NULL == (ptr = strrchr(filename, '/'))) {
        if (NULL == (ptr = strrchr(filename, '\\'))) {
            ptr = filename;
        } else {
            ptr++;
        }
    } else {
        ptr++;
    }

    if (0 == strcmp(ptr, "replication_changelog.db")) return 1;

    for (; ptr && *ptr; ptr++) {
        if ('.' == *ptr) {
            if (0 == strncmp(ptr, ".db", 3)) {
                if (3 == dashes && 1 == underscore) {
                    return 1;
                } else {
                    return 0;
                }
            }
        } else if ('-' == *ptr) {
            if (underscore > 0) {
                return 0;
            }
            dashes++;
        } else if ('_' == *ptr) {
            if (dashes < 3) {
                return 0;
            }
            underscore++;
        } else if (!isxdigit(*ptr)) {
            return 0;
        }
    }
    return 0;
}

static void
usage(char *argv0, int error)
{
    char *copy = strdup(argv0);
    char *p0 = NULL, *p1 = NULL;
    if (NULL != copy) {
        /* the full path is not needed in the usages */
        p0 = strrchr(argv0, '/');
        if (NULL != p0) {
            *p0 = '\0';
            p0++;
        } else {
            p0 = argv0;
        }
        p1 = strrchr(p0, '-'); /* get rid of -bin from the usage */
        if (NULL != p1) {
            *p1 = '\0';
        }
    }
    if (NULL == p0) {
        p0 = argv0;
    }
    printf("\n%s - scan a db file and dump the contents\n", p0);
    printf("  common options:\n");
    printf("    -A, --ascii                    dump as ascii data\n");
    printf("    -D, --db-type <dbimpl>         specify db implementaion (may be: bdb or mdb)\n");
    printf("    -f, --dbi <filename>           specify db instance\n");
    printf("    -R, --raw                      dump as raw data\n");
    printf("    -t, --truncate-entry <size>    entry truncate size (bytes)\n");

    printf("  entry file options:\n");
    printf("    -K, --entry-id <entry_id>      lookup only a specific entry id\n");

    printf("  index file options:\n");
    printf("    -G, --id-list-min-size <n>     only display index entries with more than <n> ids\n");
    printf("    -I, --import file              Import database instance from file.\n");
    printf("    -k, --key <key>                lookup only a specific key\n");
    printf("    -l, --id-list-max-size <size>  max length of dumped id list\n");
    printf("                                  (default %" PRIu32 "; 40 bytes <= size <= 1048576 bytes)\n", MAX_BUFFER);
    printf("    -n, --show-id-list-lenghts     display ID list lengths\n");
    printf("    --remove                       remove database instance\n");
    printf("    -r, --show-id-list             display the conents of ID list\n");
    printf("    -S, --stats <dbhome>           show statistics\n");
    printf("    -X, --export file              export database instance in file\n");

    printf("  other options:\n");
    printf("    -s, --summary                  summary of index counts\n");
    printf("    -L, --list <dbhome>            list all db files\n");
    printf("    --do-it                        confirmation flags for destructive actions like --remove or --import\n");
    printf("    -h, --help                     display this usage\n");

    printf("  sample usages:\n");
    printf("    # list the database instances\n");
    printf("    %s -L /var/lib/dirsrv/slapd-supplier1/db/\n", p0);
    printf("    # dump the entry file\n");
    printf("    %s -f id2entry.db\n", p0);
    printf("    # display index keys in cn.db4\n");
    printf("    %s -f cn.db4\n", p0);
    printf("    # display index keys in cn on lmdb\n");
    printf("    %s -f /var/lib/dirsrv/slapd-supplier1/db/userroot/cn.db\n", p0);
    printf("    (Note: Use 'dbscan -L db_home_dir' to get the db instance path)\n");
    printf("    # display index keys and the count of entries having the key in mail.db4\n");
    printf("    %s -r -f mail.db4\n", p0);
    printf("    # display index keys and the IDs having more than 20 IDs in sn.db4\n");
    printf("    %s -r -G 20 -f sn.db4\n", p0);
    printf("    # display summary of objectclass.db4\n");
    printf("    %s -s -f objectclass.db4\n", p0);
    printf("\n");
    free(copy);
    exit(error?1:0);
}

void dump_ascii_val(const char *str, dbi_val_t *val)
{
    unsigned char *v = val->data;
    unsigned char *last = &v[val->size];

    printf("%s: ",str);
    while (v<last) {
        switch (*v) {
            case ' ':
                printf("\\s");
                break;
            case '\\':
                printf("\\\\");
                break;
            case '\t':
                printf("\\t");
                break;
            case '\r':
                printf("\\r");
                break;
            case '\n':
                printf("\\n");
                break;
            default:
                if (*v > 0x20 && *v < 0x7f) {
                    printf("%c", *v);
                } else {
                    printf("\\%02x", *v);
                }
                break;
        }
        v++;
    }
}

int dump_ascii(dbi_cursor_t *cursor, dbi_val_t *key, dbi_val_t *data)
{
    int rc;
    do {
        dump_ascii_val("KEY", key);
        dump_ascii_val("\tDATA", data);
        putchar('\n');
        rc = dblayer_cursor_op(cursor, DBI_OP_NEXT,  key, data);
    } while (rc==0);
    if (rc == DBI_RC_NOTFOUND) {
        rc = 0;
    }
    return rc;
}

static int
_file_format_error(void)
{
    fprintf(stderr, "importdb failed: Invalid file format.\n");
    return -2;
}

static void
_push_val(const char *v, dbi_val_t *val)
{
    if (val->size >= val->ulen) {
        if (val->ulen <= 0) {
            val->ulen = 200;
        } else {
            val->ulen *= 2;
        }
        val->data = realloc(val->data, val->ulen);
        if (!val->data) {
            db_printf("Out of memory: Failed to alloc %d bytes.\n", val->ulen);
            exit(1);
        }
    }
    ((char*)(val->data))[val->size++] = strtol(v, NULL, 16);
}


static int
_read_line(FILE *file, int *keyword, dbi_val_t *val)
{
    char v[3] = {0};
    int c;

    *keyword = -1;
    val->size = 0;
    enum { ST_POS0, ST_POS1, ST_POS2, ST_COMMENT, ST_VAL1, ST_VAL2 } state;
    state = ST_POS0;
    while ((c = fgetc(file)) != EOF) {
        switch (state) {
            case ST_COMMENT:
                if (c == '\n') {
                    state = ST_POS0;
                }
                continue;
            case ST_VAL1:
                if (c == '\n') {
                    return 0;
                }
                if (!isxdigit(c)) {
                    return _file_format_error();
                }
                v[0] = c;
                state = ST_VAL2;
                continue;
            case ST_VAL2:
                if (!isxdigit(c)) {
                    return _file_format_error();
                }
                v[1] = c;
                state = ST_VAL1;
                _push_val(v, val);
                continue;
            case ST_POS0:
                switch (c) {
                    case '\n':
                        continue;
                    case '#':
                        state = ST_COMMENT;
                        continue;
                    case 'k':
                    case 'v':
                    case 'e':
                        state = ST_POS1;
                        *keyword = c;
                        continue;
                    default:
                        return _file_format_error();
                }
                /* NOTREACHED */
            case ST_POS1:
                if (c!= ':') {
                    return _file_format_error();
                }
                state = ST_POS2;
                continue;
            case ST_POS2:
                if (c!= ' ') {
                    return _file_format_error();
                }
                state = ST_VAL1;
                continue;
        }
    }
    return EOF;
}

int
importdb(const char *dbimpl_name, const char *filename, const char *dump_name)
{
    FILE *dump = NULL;
    dbi_val_t key = {0}, data = {0};
    struct back_txn txn = {0};
    dbi_env_t *env = NULL;
    dbi_db_t *db = NULL;
    int keyword = 0;
    int ret = 0;
    int count = 0;

    if (dump_name == NULL) {
        printf("Error: dump_name can not be NULL\n");
        return 1;
    }

    dump = fopen(dump_name, "r");

    dblayer_init_pvt_txn();

    if (!dump) {
        printf("Error: Failed to open dump file %s. Error %d: %s\n", dump_name, errno, strerror(errno));
        return 1;
    }

    if (dblayer_private_open(dbimpl_name, filename, 1, &be, &env, &db)) {
        printf("Error: Can't initialize db plugin: %s\n", dbimpl_name);
        fclose(dump);
        return 1;
    }

    ret = dblayer_txn_begin(be, NULL, &txn);
    if (ret != 0) {
        printf("Error: failed to start the database txn. Error %d: %s\n", ret, dblayer_strerror(ret));
    }
    while (ret == 0 &&
           !_read_line(dump, &keyword, &key) && keyword == 'k' &&
           !_read_line(dump, &keyword, &data) && keyword == 'v') {
        ret = dblayer_db_op(be, db, txn.txn, DBI_OP_PUT, &key, &data);
        if (ret == 0  && count++ >= 1000) {
            ret = dblayer_txn_commit(be, &txn);
            if (ret != 0) {
                printf("Error: failed to commit the database txn. Error %d: %s\n", ret, dblayer_strerror(ret));
            } else {
                count = 0;
                ret = dblayer_txn_begin(be, NULL, &txn);
                if (ret != 0) {
                    printf("Error: failed to start the database txn. Error %d: %s\n", ret, dblayer_strerror(ret));
                }
            }
        }
    }
    if (ret == 0) {
        ret = dblayer_txn_commit(be, &txn);
        if (ret != 0) {
            printf("Error: failed to commit the database txn. Error %d: %s\n", ret, dblayer_strerror(ret));
        }
    }
    if (ret != 0) {
        (void) dblayer_txn_abort(be, &txn);
        printf("Error: failed to write record in database. Error %d: %s\n", ret, dblayer_strerror(ret));
        dump_ascii_val("Failing record key", &key);
        dump_ascii_val("Failing record value", &data);
    }
    fclose(dump);
    dblayer_value_free(be, &key);
    dblayer_value_free(be, &data);
    if (dblayer_private_close(&be, &env, &db)) {
        printf("Error: Unable to shutdown the db plugin: %s\n", dblayer_strerror(1));
        return 1;
    }
    return ret;
}

void print_value(FILE *dump, const char *keyword, const unsigned char *data, int len)
{
    fprintf(dump,"%s", keyword);
    while (len > 0) {
        fprintf(dump,"%02x", *data++);
        len--;
    }
    fprintf(dump,"\n");
}

int
exportdb(const char *dbimpl_name, const char *filename, const char *dump_name)
{
    FILE *dump = fopen(dump_name, "w");
    dbi_val_t key = {0}, data = {0};
    dbi_cursor_t cursor = {0};
    dbi_env_t *env = NULL;
    dbi_db_t *db = NULL;
    int ret = 0;

    if (!dump) {
        printf("Failed to open dump file %s. Error %d: %s\n", dump_name, errno, strerror(errno));
        return 1;
    }

    if (dblayer_private_open(dbimpl_name, filename, 0, &be, &env, &db)) {
        printf("Can't initialize db plugin: %s\n", dbimpl_name);
        fclose(dump);
        return 1;
    }

    /* cursor through the db */

    ret = dblayer_new_cursor(be, db, NULL, &cursor);
    if (ret != 0) {
        printf("Can't create db cursor: %s\n", dblayer_strerror(ret));
        fclose(dump);
        return 1;
    }

    fprintf(dump, "# %s\n", filename);

    /* Position cursor at the matching key */
    ret = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_FIRST,  &key, &data);
    while (ret == DBI_RC_SUCCESS) {
        print_value(dump, "k: ", key.data, key.size);
        print_value(dump, "v: ", data.data, data.size);
        fprintf(dump, "\n");
        if (ferror(dump)) {
            printf("Failed to write in dump file %s. Error %d: %s\n", dump_name, errno, strerror(errno));
            break;
        }
        ret = dblayer_cursor_op(&cursor, DBI_OP_NEXT,  &key, &data);
    }
    if (ret == DBI_RC_NOTFOUND) {
        fprintf(dump, "e: \n");
        ret = 0;
    }
    fclose(dump);
    dblayer_value_free(be, &key);
    dblayer_value_free(be, &data);
    dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    if (dblayer_private_close(&be, &env, &db)) {
        printf("Unable to shutdown the db plugin: %s\n", dblayer_strerror(1));
        return 1;
    }
    return ret;
}

int
removedb(const char *dbimpl_name, const char *filename)
{
    dbi_env_t *env = NULL;
    dbi_db_t *db = NULL;

    if (!filename) {
        printf("Error: -f option is missing.\n"
               "Usage: dbscan -D mdb -d -f <db_home_dir>/<backend_name>/<db_name>\n");
        return 1;
    }

    if (dblayer_private_open(dbimpl_name, filename, 1, &be, &env, &db)) {
        printf("Can't initialize db plugin: %s\n", dbimpl_name);
        return 1;
    }

    if (dblayer_db_remove(be, db)) {
        printf("Failed to remove db %s\n", filename);
        return 1;
    }

    db = NULL; /* Database is already closed by dblayer_db_remove */
    if (dblayer_private_close(&be, &env, &db)) {
        printf("Unable to shutdown the db plugin: %s\n", dblayer_strerror(1));
        return 1;
    }
    return 0;
}

int
main(int argc, char **argv)
{
    dbi_env_t *env = NULL;
    dbi_db_t *db = NULL;
    dbi_cursor_t cursor = {0};
    char *filename = NULL;
    dbi_val_t key = {0}, data = {0};
    int ret = 0;
    char *find_key = NULL;
    uint32_t entry_id = 0xffffffff;
    char *defdbimpl = getenv("NSSLAPD_DB_LIB");
    char *dbimpl_name = (char*) "mdb";
    int longopt_idx = 0;
    int c = 0;
    char optstring[2*COUNTOF(options)+1] = {0};

    if (defdbimpl) {
        if (strcasecmp(defdbimpl, "bdb") == 0) {
            dbimpl_name = (char*) "bdb";
        }
        if (strcasecmp(defdbimpl, "mdb") == 0) {
            dbimpl_name = (char*) "mdb";
        }
    }

    /* Compute getopt short option string */
    {
        char *pt = optstring;
        for (const struct option *opt = options; opt->name; opt++) {
            if (opt->val>0 && opt->val<OPT_FIRST) {
                *pt++ = (char)(opt->val);
                if (opt->has_arg == required_argument) {
                    *pt++ = ':';
                }
            }
        }
        *pt = '\0';
    }

    while ((c = getopt_long(argc, argv, optstring, options, &longopt_idx)) != EOF) {
        if (c == 0) {
            c = longopt_idx;
        }
        switch (c) {
        case OPT_DO_IT:
            do_it = 1;
            break;
        case OPT_REMOVE:
            display_mode |= REMOVE;
            break;
        case 'A':
            display_mode |= ASCIIDATA;
            break;
        case 'f':
            filename = optarg;
            break;
        case 'R':
            display_mode |= RAWDATA;
            break;
        case 'S':
            display_mode |= SHOWSTAT;
            filename = optarg;
            break;
        case 'L':
            display_mode |= LISTDBS;
            filename = optarg;
            break;
        case 'l': {
            uint32_t tmpmaxbufsz = atoi(optarg);
            if (tmpmaxbufsz > ONEMEG) {
                tmpmaxbufsz = ONEMEG;
                printf("WARNING: max length of dumped id list too long, "
                       "reduced to %d\n",
                       tmpmaxbufsz);
            } else if (tmpmaxbufsz < MIN_BUFFER * 2) {
                tmpmaxbufsz = MIN_BUFFER * 2;
                printf("WARNING: max length of dumped id list too short, "
                       "increased to %d\n",
                       tmpmaxbufsz);
            }
            MAX_BUFFER = tmpmaxbufsz;
            break;
        }
        case 'n':
            display_mode |= SHOWCOUNT;
            break;
        case 'G':
            min_display = atoi(optarg) + 1;
            break;
        case 'r':
            display_mode |= SHOWDATA;
            break;
        case 's':
            display_mode |= SHOWSUMMARY;
            break;
        case 'k':
            find_key = optarg;
            break;
        case 'K':
            id_internal_to_stored((ID)atoi(optarg), (char *)&entry_id);
            break;
        case 't':
            truncatesiz = atoi(optarg);
            break;
        case 'D':
            dbimpl_name = optarg;
            break;
        case 'X':
            display_mode |= EXPORT;
            dump_filename = optarg;
            break;
        case 'I':
            display_mode |= IMPORT;
            dump_filename = optarg;
            break;
        case 'h':
        default:
            usage(argv[0], 1);
        }
    }

    if (filename == NULL) {
        fprintf(stderr, "PARAMETER ERROR! 'filename' parameter is missing.\n");
        usage(argv[0], 1);
    }

    if (display_mode & EXPORT) {
        return exportdb(dbimpl_name, filename, dump_filename);
    }

    if (display_mode & IMPORT) {
        if (!strstr(filename, "/id2entry") && !strstr(filename, "/replication_changelog")) {
            /* schema is unknown in dbscan ==> duplicate keys sort order is unknown
             *  ==> cannot create dbi with duplicate keys
             * ==> only id2entry and repl changelog is importable.
             */
            fprintf(stderr, "ERROR: The only database instances that may be imported with dbscan are id2entry and replication_changelog.\n");
            exit(1);
        }

        if (do_it == 0) {
            fprintf(stderr, "PARAMETER ERROR! Trying to perform a destructive action (import)\n"
                            " without specifying '--do-it' parameter.\n");
            exit(1);
        }
        return importdb(dbimpl_name, filename, dump_filename);
    }

    if (display_mode & REMOVE) {
        if (do_it == 0) {
            fprintf(stderr, "PARAMETER ERROR! Trying to perform a destructive action (remove)\n"
                            " without specifying '--do-it' parameter.\n");
            exit(1);
        }
        return removedb(dbimpl_name, filename);
    }

    if (display_mode & LISTDBS) {
        dbi_dbslist_t *dbs = dblayer_list_dbs(dbimpl_name, filename);
        if (dbs) {
            dbi_dbslist_t *ptdbs = dbs;
            while (ptdbs->filename[0]) {
                printf(" %s %s\n", ptdbs->filename, ptdbs->info);
                ptdbs++;
            }
        }
        free(dbs);
        ret = 0;
        goto done;
    }

    if (display_mode & SHOWSTAT) {
        ret = dblayer_show_statistics(dbimpl_name, filename, stdout, stderr);
        goto done;
    }

    if (NULL != strstr(filename, "id2entry.db")) {
        file_type |= ENTRYTYPE;
    } else if (is_changelog(filename)) {
        file_type |= CHANGELOGTYPE;
    } else {
        file_type |= INDEXTYPE;
        if (NULL != strstr(filename, "vlv#")) {
            file_type |= VLVINDEXTYPE;
        } else if (NULL != strstr(filename, "entryrdn.db")) {
            file_type |= ENTRYRDNINDEXTYPE;
        }
    }

    if (dblayer_private_open(dbimpl_name, filename, 0, &be, &env, &db)) {
        printf("Can't initialize db plugin: %s\n", dbimpl_name);
        ret = 1;
        goto done;
    }

    /* cursor through the db */

    ret = dblayer_new_cursor(be, db, NULL, &cursor);
    if (ret != 0) {
        printf("Can't create db cursor: %s\n", dblayer_strerror(ret));
        ret = 1;
        goto done;
    }

    /* Position cursor at the matching key */
    ret = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_FIRST,  &key, &data);
    if (ret == DBI_RC_NOTFOUND) {
        printf("Empty database!\n");
        ret = 0;
        goto done;
    }
    if (ret != 0) {
        printf("Can't get first cursor: %s\n", dblayer_strerror(ret));
        ret = 1;
        goto done;
    }

    if (display_mode & ASCIIDATA) {
        ret = dump_ascii(&cursor, &key, &data);
        goto done;
    }

    if (find_key) {
        /* Position cursor at the matching key */
        dblayer_value_set_buffer(be, &key, find_key, strlen(find_key) + 1);
        dblayer_value_free(be, &data);
        ret = dblayer_db_op(be, db, NULL, DBI_OP_GET,  &key, &data);
        if (ret == DBI_RC_NOTFOUND) {
            /* could be a key that doesn't have the trailing null? */
            key.size--;
            ret = dblayer_db_op(be, db, NULL, DBI_OP_GET,  &key, &data);
        }
        if (ret != 0) {
            printf("Can't find key '%s' error=%s [%d]\n", find_key, dblayer_strerror(ret), ret);
            ret = 1;
            goto done;
        }
        if (file_type & ENTRYRDNINDEXTYPE) {
            display_entryrdn_item(db, &cursor, &key);
        } else {
            ret = dblayer_cursor_op(&cursor, DBI_OP_MOVE_TO_KEY,  &key, &data);
            if (ret != 0) {
                printf("Can't set cursor to returned item: %s\n",
                       dblayer_strerror(ret));
                ret = 1;
                goto done;
            }
            do {
                display_item(&cursor, &key, &data);
                ret = dblayer_cursor_op(&cursor, DBI_OP_NEXT,  &key, &data);
            } while (0 == ret);
            dblayer_value_free(be, &key);
            dblayer_value_init(be, &key);
        }
    } else if (entry_id != 0xffffffff) {
        dblayer_value_set_buffer(be, &key, &entry_id, sizeof(entry_id));
        ret = dblayer_db_op(be, db, NULL, DBI_OP_GET,  &key, &data);
        if (ret != 0) {
            printf("Can't set cursor to returned item: %s\n",
                   dblayer_strerror(ret));
            ret = 1;
            goto done;
        }
        display_item(&cursor, &key, &data);
        dblayer_value_free(be, &key);
        dblayer_value_init(be, &key);
    } else {
        if (file_type & ENTRYRDNINDEXTYPE) {
            dblayer_value_free(be, &key);
            dblayer_value_init(be, &key);
            display_entryrdn_item(db, &cursor, &key);
        } else {
            while (ret == 0) {
                /* display */
                display_item(&cursor, &key, &data);

                ret = dblayer_cursor_op(&cursor, DBI_OP_NEXT,  &key, &data);
                if ((ret != 0) && (ret != DBI_RC_NOTFOUND)) {
                    printf("Bizarre error: %s\n", dblayer_strerror(ret));
                    ret = 1;
                    goto done;
                }
            }
            /* Success! Setting the return code to 0 */
            ret = 0;
        }
    }

    if (display_mode & SHOWSUMMARY) {

        if (allids_cnt > 0) {
            printf("Index keys that reached ALLIDs threshold: %ld\n", allids_cnt);
        }

        if (pres_cnt > 0) {
            printf("Presence index keys: %ld\n", pres_cnt);
        }

        if (eq_cnt > 0) {
            printf("Equality index keys: %ld\n", eq_cnt);
        }

        if (app_cnt > 0) {
            printf("Approximate index keys: %ld\n", app_cnt);
        }

        if (sub_cnt > 0) {
            printf("Substring index keys: %ld\n", sub_cnt);
        }

        if (match_cnt > 0) {
            printf("Match index keys: %ld\n", match_cnt);
        }

        if (ind_cnt > 0) {
            printf("Indirect index keys: %ld\n", ind_cnt);
        }

        if (other_cnt > 0) {
            printf("This file contains %ld number of unknown type ( possible corruption)\n", other_cnt);
        }
    }

done:
    dblayer_value_free(be, &key);
    dblayer_value_free(be, &data);
    dblayer_cursor_op(&cursor, DBI_OP_CLOSE, NULL, NULL);
    if (dblayer_private_close(&be, &env, &db)) {
        printf("Unable to shutdown the db plugin: %s\n", dblayer_strerror(1));
        return 1;
    }
    return ret;
}
