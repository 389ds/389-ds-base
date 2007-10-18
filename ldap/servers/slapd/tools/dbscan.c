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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


/*
 * small program to scan a Directory Server db file and dump the contents
 *
 * TODO: indirect indexes
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include "db.h"

#ifdef _WIN32
#include <windows.h>
#include <winsock.h>
extern int getopt();
extern char *optarg; 
typedef unsigned char uint8_t;
#else
#include <netinet/in.h>
#include <inttypes.h>
#endif

#if ( defined( hpux ) )
#ifdef _XOPEN_SOURCE_EXTENDED
#include <arpa/inet.h>	/* for ntohl, et al. */
#endif
#endif

/* file type */
#define ENTRYTYPE     0x1
#define INDEXTYPE     0x2
#define VLVINDEXTYPE  0x4
#define CHANGELOGTYPE 0x8

/* display mode */
#define RAWDATA     0x1
#define SHOWCOUNT   0x2
#define SHOWDATA    0x4
#define SHOWSUMMARY 0x8

/* stolen from slapi-plugin.h */
#define SLAPI_OPERATION_BIND     0x00000001UL
#define SLAPI_OPERATION_UNBIND   0x00000002UL
#define SLAPI_OPERATION_SEARCH   0x00000004UL
#define SLAPI_OPERATION_MODIFY   0x00000008UL
#define SLAPI_OPERATION_ADD      0x00000010UL
#define SLAPI_OPERATION_DELETE   0x00000020UL
#define SLAPI_OPERATION_MODDN    0x00000040UL
#define SLAPI_OPERATION_MODRDN   SLAPI_OPERATION_MODDN
#define SLAPI_OPERATION_COMPARE  0x00000080UL
#define SLAPI_OPERATION_ABANDON  0x00000100UL
#define SLAPI_OPERATION_EXTENDED 0x00000200UL
#define SLAPI_OPERATION_ANY      0xFFFFFFFFUL
#define SLAPI_OPERATION_NONE     0x00000000UL

/* changelog ruv info.  These correspond with some special csn
 * timestamps from cl5_api.c */
#define ENTRY_COUNT_KEY	"0000006f" /* 111 csn timestamp */
#define PURGE_RUV_KEY	"000000de" /* 222 csn timestamp */
#define MAX_RUV_KEY	"0000014d" /* 333 csn timestamp */

#define ONEMEG (1024*1024)

#if defined(linux)
#include <getopt.h>
#endif

typedef u_int32_t        ID;

typedef unsigned int uint32;

typedef struct {
    uint32 max;
    uint32 used;
    uint32 id[1];
} IDL;

uint32 file_type = 0;
uint32 min_display = 0;
uint32 display_mode = 0;
int truncatesiz = 0;
long pres_cnt = 0;
long eq_cnt = 0;
long app_cnt = 0;
long sub_cnt = 0;
long match_cnt = 0;
long ind_cnt = 0;
long allids_cnt = 0;
long other_cnt = 0;

/** db_printf - functioning same as printf but a place for manipluating output.
*/
void db_printf(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
}

void db_printfln(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    vfprintf(stdout, "\n", NULL);
}

int MAX_BUFFER = 4096;
int MIN_BUFFER = 20;

static IDL *idl_make(DBT *data)
{
    IDL *idl = NULL, *xidl;

    if (data->size < 2*sizeof(uint32)) {
        idl = (IDL *)malloc(sizeof(IDL) + 64*sizeof(uint32));
        if (! idl)
            return NULL;
        idl->max = 64;
        idl->used = 1;
        idl->id[0] = *(uint32 *)(data->data);
        return idl;
    }

    xidl = (IDL *)(data->data);
    idl = (IDL *)malloc(data->size);
    if (! idl)
        return NULL;

    memcpy(idl, xidl, data->size);
    return idl;
}

static void idl_free(IDL *idl)
{
    idl->max = 0;
    idl->used = 0;
    free(idl);
}

static IDL *idl_append(IDL *idl, uint32 id)
{
    if (idl->used >= idl->max) {
        /* must grow */
        idl->max *= 2;
        idl = realloc(idl, sizeof(IDL) + idl->max * sizeof(uint32));
        if (! idl)
            return NULL;
    }
    idl->id[idl->used++] = id;
    return idl;
}


/* format a string for easy printing */
#define FMT_LF_OK       1
#define FMT_SP_OK       2
static char *format_raw(unsigned char *s, int len, int flags,
                        unsigned char *buf, int buflen)
{
    static char hex[] = "0123456789ABCDEF";
    unsigned char *p, *o, *bufend = buf + buflen - 1;
    int i;

    if (NULL == buf || buflen <= 0)
        return NULL;

    for (p = s, o = buf, i = 0; i < len && o < bufend; p++, i++) {
        if ((*p == '%') || (*p <= ' ') || (*p >= 126)) {
            /* index keys are stored with their trailing NUL */
            if ((*p == 0) && (i == len-1))
                continue;
            if ((flags & FMT_LF_OK) && (*p == '\n')) {
                *o++ = '\n';
                *o++ = '\t';
            } else if ((flags && FMT_SP_OK) && (*p == ' ')) {
                *o++ = ' ';
            } else {
                *o++ = '%';
                *o++ = hex[*p / 16];
                *o++ = hex[*p % 16];
            }
        } else {
            *o++ = *p;
        }
        if (truncatesiz > 0 && o > bufend - 5) {
            /* truncate it */
            strcpy((char *)o, " ...");
            i = len;
            o += 4;
        }
    }
    *o = 0;
    return (char *)buf;
}

static char *format(unsigned char *s, int len, unsigned char *buf, int buflen)
{
    return format_raw(s, len, 0, buf, buflen);
}

static char *format_entry(unsigned char *s, int len,
                          unsigned char *buf, int buflen)
{
    return format_raw(s, len, FMT_LF_OK | FMT_SP_OK, buf, buflen);
}

static char *idl_format(IDL *idl, int isfirsttime, int *done)
{
    static char *buf = NULL;
    static uint32 i = 0;
    
    if (buf == NULL) {
        buf = (char *)malloc(MAX_BUFFER);
        if (buf == NULL)
            return "?";
    }

    buf[0] = 0;
    if (0 != isfirsttime) {
        i = 0;
    }
    for (; i < idl->used; i++) {
        sprintf((char *)buf + strlen(buf), "%d ", idl->id[i]);

        if (strlen(buf) > (size_t)MAX_BUFFER-MIN_BUFFER) {
            i++;
            done = 0;
            return (char *)buf;
        }
    }
    *done = 1;
    return (char *)buf;
}


/*** Copied from cl5_api.c: _cl5ReadString ***/
void _cl5ReadString (char **str, char **buff)
{
    if (str)
    {
        int len = strlen (*buff);
        
        if (len)
        { 
            *str = strdup(*buff);
            (*buff) += len + 1;
        }
        else /* just null char - skip it */
        {
            *str = NULL;
            (*buff) ++;
        }
    }
    else /* just skip this string */
    {
        (*buff) += strlen (*buff) + 1;        
    }
}

/** print_attr - print attribute name followed by one value.
    assume the value stored as null terminated string.
*/
void print_attr(char *attrname, char **buff)
{
    char *val = NULL;

    _cl5ReadString(&val, buff);
    if(attrname != NULL || val != NULL) {
        db_printf("\t");
    }

    if(attrname) {
        db_printf("%s: ", attrname);
    }
    if(val != NULL) {
        db_printf("%s\n", val);
        free(val);
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

void _cl5ReadMods(char **buff)
{
    char *pos = *buff;
    uint32 i;
    uint32 mod_count;

    /* need to copy first, to skirt around alignment problems on certain
       architectures */
    memcpy((char *)&mod_count, *buff, sizeof(mod_count));
    mod_count = ntohl(mod_count);
    pos += sizeof (mod_count);
    

    for (i = 0; i < mod_count; i++)
    {        
        _cl5ReadMod (&pos);
    }
 
    *buff = pos;
}


/** print_ber_attr - print one line of attribute, the value was stored
                     in ber format, length followed by string.
*/
void print_ber_attr(char* attrname, char** buff)
{
    char *val = NULL;
    uint32 bv_len;

    memcpy((char *)&bv_len, *buff, sizeof(bv_len));
    bv_len = ntohl(bv_len);
    *buff += sizeof (uint32);
    if (bv_len > 0) {

        db_printf("\t\t");

        if(attrname != NULL) {
            db_printf("%s: ", attrname);
        }

        val = malloc(bv_len + 1);
        memcpy (val, *buff, bv_len);
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
static ID id_stored_to_internal(char* b)
{
    ID i;
    i = (ID)b[3] & 0x000000ff;
    i |= (((ID)b[2]) << 8) & 0x0000ff00;
    i |= (((ID)b[1]) << 16) & 0x00ff0000;
    i |= ((ID)b[0]) << 24;
    return i;
}

static void id_internal_to_stored(ID i,char *b)
{
    if ( sizeof(ID) > 4 ) {
        memset (b+4, 0, sizeof(ID)-4);
    }

    b[0] = (char)(i >> 24);
    b[1] = (char)(i >> 16);
    b[2] = (char)(i >> 8);
    b[3] = (char)i;
}

void _cl5ReadMod(char **buff)
{
    char *pos = *buff;
    uint32 i;
    uint32 val_count;
    char *type = NULL;

    pos ++;
    _cl5ReadString (&type, &pos);

    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&val_count, pos, sizeof(val_count));
    val_count = ntohl(val_count);
    pos += sizeof (uint32);

    for (i = 0; i < val_count; i++)
    {
        print_ber_attr(type, &pos);
    }

    (*buff) = pos;
    free(type);
}

/* data format: <value count> <value size> <value> <value size> <value> ..... */
void print_ruv(unsigned char *buff)
{
    char *pos = (char *)buff;
    uint32 i;
    uint32 val_count;

    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&val_count, pos, sizeof(val_count));
    val_count = ntohl(val_count);
    pos += sizeof (uint32);

    for (i = 0; i < val_count; i++)
    {
        print_ber_attr(NULL, &pos);
    }
}

/*
   *** Copied from cl5_api:cl5DBData2Entry ***
   Data in db format:
   ------------------
   <1 byte version><1 byte change_type><sizeof uint32 time><null terminated dbid>
   <null terminated csn><null terminated uniqueid><null terminated targetdn>
   [<null terminated newrdn><1 byte deleteoldrdn>][<4 byte mod count><mod1><mod2>....]

Note: the length of time is set uint32 instead of time_t. Regardless of the
width of long (32-bit or 64-bit), it's stored using 4bytes by the server [153306].

   mod format:
   -----------
   <1 byte modop><null terminated attr name><4 byte value count>
   <4 byte value size><value1><4 byte value size><value2>
*/
void print_changelog(unsigned char *data, int len)
{
    uint8_t version;
    unsigned long operation_type;
    char *pos = (char *)data;
    uint32 thetime32;
    time_t thetime;
    uint32 replgen;

    /* read byte of version */
    version = *((uint8_t *)pos);
    if (version != 5)
    {
        db_printf("Invalid changelog db version %i\nWorks for version 5 only.\n", version);
        exit(1);
    }
    pos += sizeof(version);

    /* read change type */
    operation_type = (unsigned long)(*(uint8_t *)pos);
    pos ++;
    
    /* need to do the copy first, to skirt around alignment problems on
       certain architectures */
    memcpy((char *)&thetime32, pos, sizeof(thetime32));

    replgen = ntohl(thetime32);
    pos += sizeof(uint32);
    thetime = (time_t)replgen;
    db_printf("\treplgen: %ld %s", replgen, ctime((time_t *)&thetime));

    /* read csn */
    print_attr("csn", &pos);
    /* read UniqueID */
    print_attr("uniqueid", &pos);    
    
    /* figure out what else we need to read depending on the operation type */
    switch (operation_type)
    {
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

        case SLAPI_OPERATION_MODRDN:    
            print_attr("dn", &pos);
            print_attr("newrdn", &pos);
            pos ++;
            print_attr("dn", &pos);
            print_attr("uniqueid", &pos);
            db_printf("\toperation: modrdn\n");
            _cl5ReadMods(&pos);
            break;

        case SLAPI_OPERATION_DELETE:    
            print_attr("dn", &pos);
            db_printf("\toperation: delete\n");
            break;

        default:                            
            db_printf("Failed to format entry\n");
            break;
    }
}

static void display_index_item(DBC *cursor, DBT *key, DBT *data,
                               unsigned char *buf, int buflen)
{
    IDL *idl = NULL;
    int ret = 0;

    idl = idl_make(data);
    if (idl == NULL) {
        printf("\t(illegal idl)\n");
        return;
    }

    if (file_type & VLVINDEXTYPE) {  /* vlv index file */
        if (1 > min_display) { /* recno is always 1 */
            if (display_mode & SHOWCOUNT) {  /* key  size=1 */
                printf("%-40s         1\n",
                       format(key->data, key->size, buf, buflen));
            } else {
                printf("%-40s\n", format(key->data, key->size, buf, buflen));
            }
            if (display_mode & SHOWDATA) {
                cursor->c_get(cursor, key, data, DB_GET_RECNO);
                printf("\t%5d\n", *(db_recno_t *)(data->data));
            }
        }
        goto index_done;
    }

    /* ordinary index file */
    /* fetch all other id's too */
    while (ret == 0) {
        ret = cursor->c_get(cursor, key, data, DB_NEXT_DUP);
        if (ret == 0)
            idl = idl_append(idl, *(uint32 *)(data->data));
    }
    if (ret == DB_NOTFOUND)
        ret = 0;
    if (ret != 0) {
        printf("Failure while looping dupes: %s\n", db_strerror(ret));
        exit(1);
    }

    if (idl && idl->max == 0) {
        /* allids; should not exist in the new idl world */
        if ( allids_cnt == 0 && (display_mode & SHOWSUMMARY)) {
            printf("The following index keys reached allids:\n");
        }
        printf("%-40s(allids)\n", format(key->data, key->size, buf, buflen));
        allids_cnt++;
    } else {
        if (idl->used < min_display) {
            goto index_done;  /* less than minimum display count */
        } else if (display_mode & SHOWCOUNT) {  /* key  size */
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
    if ( display_mode & SHOWSUMMARY ) {
        char firstchar;

        firstchar = ((char*)key->data)[0];

        switch ( firstchar ) {
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

static void display_item(DBC *cursor, DBT *key, DBT *data)
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
        if (NULL == buf) {
            printf("\t(malloc failed -- %d bytes)\n", buflen);
            return;
        }
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
                printf("\tentry count: %d\n", *(int*)data->data);
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
            printf("id %d\n", entry_id);
            printf("\t%s\n", format_entry(data->data, data->size, buf, buflen));
        } else {
            /* user didn't tell us what kind of file, dump it raw */
            printf("%s\n", format(key->data, key->size, buf, buflen));
            printf("\t%s\n", format(data->data, data->size, buf, buflen));
        }
    }
    return;
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

static void usage(char *argv0)
{
    printf("\n%s - scan a db file and dump the contents\n", argv0);
    printf("  common options:\n");
    printf("    -f <filename>   specify db file\n");
    printf("    -R              dump as raw data\n");
    printf("    -t <size>       entry truncate size (bytes)\n");
    printf("  entry file options:\n");
    printf("    -K <entry_id>   lookup only a specific entry id\n");
    printf("  index file options:\n");
    printf("    -k <key>        lookup only a specific key\n");
    printf("    -l <size>       max length of dumped id list\n");
    printf("                    (default %d; 40 bytes <= size <= 1048576 bytes)\n",
           MAX_BUFFER);
    printf("    -G <n>          only display index entries with more than <n> ids\n");
    printf("    -n              display idl lengths\n");
    printf("    -r              display the conents of idl\n");
    printf("    -s              Summary of index counts\n");
    printf("  sample usages:\n");
    printf("    # set <prefix>/usr/lib/<PACKAGE_NAME>:<prefix>/usr/lib:/usr/lib in the library path\n");
    printf("    # dump the entry file\n");
    printf("    %s -f id2entry.db\n", argv0);
    printf("    # display index keys in cn.db4\n");
    printf("    %s -f cn.db4\n", argv0);
    printf("    # display index keys and the count of entries having the key in mail.db4\n");
    printf("    %s -r -f mail.db4\n", argv0);
    printf("    # display index keys and the IDs having more than 20 IDs in sn.db4\n");
    printf("    %s -r -G 20 -f sn.db4\n", argv0);
    printf("    # display summary of objectclass.db4\n");
    printf("    %s -f objectclass.db4\n", argv0);
    printf("\n");
    exit(1);
}

int main(int argc, char **argv)
{
    DB_ENV *env = NULL;
    DB *db = NULL;
    DBC *cursor = NULL;
    char *filename = NULL;
    DBT key = {0}, data = {0};
    int ret;
    char *find_key = NULL;
    uint32 entry_id = 0xffffffff;
    int c;

    key.flags = DB_DBT_REALLOC;
    data.flags = DB_DBT_REALLOC;
    while ((c = getopt(argc, argv, "f:Rl:nG:srk:K:hvt:")) != EOF) {
        switch (c) {
        case 'f':
            filename = optarg;
            break;
        case 'R':
            display_mode |= RAWDATA;
            break;
        case 'l':
        {
            uint32 tmpmaxbufsz = atoi(optarg);
            if (tmpmaxbufsz > ONEMEG) {
                tmpmaxbufsz = ONEMEG;
                printf("WARNING: max length of dumped id list too long, "
                       "reduced to %d\n", tmpmaxbufsz);
            } else if (tmpmaxbufsz < MIN_BUFFER * 2) {
                tmpmaxbufsz = MIN_BUFFER * 2;
                printf("WARNING: max length of dumped id list too short, "
                       "increased to %d\n", tmpmaxbufsz);
            }
            MAX_BUFFER = tmpmaxbufsz;
            break;
        }
        case 'n':
            display_mode |= SHOWCOUNT;
            break;
        case 'G':
            min_display = atoi(optarg)+1;
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
        case 'h':
        default:
            usage(argv[0]);
        }
    }

    if(filename == NULL) {
        usage(argv[0]);
    }
    if (NULL != strstr(filename, "id2entry.db")) {
        file_type |= ENTRYTYPE;
    } else if (is_changelog(filename)) {
        file_type |= CHANGELOGTYPE;
    } else {
        file_type |= INDEXTYPE;
        if (0 == strncmp(filename, "vlv#", 4)) {
            file_type |= VLVINDEXTYPE;
        } 
    }
        
    ret = db_env_create(&env, 0);
    if (ret != 0) {
        printf("Can't create dbenv: %s\n", db_strerror(ret));
        exit(1);
    }
    ret = env->open(env, NULL, DB_CREATE|DB_INIT_MPOOL|DB_PRIVATE, 0);
    if (ret != 0) {
        printf("Can't open dbenv: %s\n", db_strerror(ret));
        exit(1);
    }

    ret = db_create(&db, env, 0);
    if (ret != 0) {
        printf("Can't create db handle: %d\n", ret);
        exit(1);
    }
    ret = db->open(db, NULL, filename, NULL, DB_UNKNOWN, DB_RDONLY, 0);
    if (ret != 0) {
        printf("Can't open db file '%s': %s\n", filename, db_strerror(ret));
        exit(1);
    }

    /* cursor through the db */

    ret = db->cursor(db, NULL, &cursor, 0);
    if (ret != 0) {
        printf("Can't create db cursor: %s\n", db_strerror(ret));
        exit(1);
    }
    ret = cursor->c_get(cursor, &key, &data, DB_FIRST);
    if (ret == DB_NOTFOUND) {
        printf("Empty database!\n");
        exit(0);
    }
    if (ret != 0) {
        printf("Can't get first cursor: %s\n", db_strerror(ret));
        exit(1);
    }

    if (find_key) {
        key.size = strlen(find_key)+1;
        key.data = find_key;
        ret = db->get(db, NULL, &key, &data, 0);
        if (ret != 0) {
            /* could be a key that doesn't have the trailing null? */
            key.size--;
            ret = db->get(db, NULL, &key, &data, 0);
            if (ret != 0) {
                printf("Can't find key '%s'\n", find_key);
                exit(1);
            }
        }
        ret = cursor->c_get(cursor, &key, &data, DB_SET);
        if (ret != 0) {
            printf("Can't set cursor to returned item: %s\n",
                   db_strerror(ret));
            exit(1);
        }
        do {
            display_item(cursor, &key, &data);
            ret = cursor->c_get(cursor, &key, &data, DB_NEXT_DUP);
        } while (0 == ret);
        key.size = 0;
        key.data = NULL;
    } else if (entry_id != 0xffffffff) {
        key.size = sizeof(entry_id);
        key.data = &entry_id;
        ret = db->get(db, NULL, &key, &data, 0);
        if (ret != 0) {
            printf("Can't set cursor to returned item: %s\n",
                   db_strerror(ret));
            exit(1);
        }
        display_item(cursor, &key, &data);
        key.size = 0;
        key.data = NULL;
    } else {
        while (ret == 0) {
            /* display */
            display_item(cursor, &key, &data);

            ret = cursor->c_get(cursor, &key, &data, DB_NEXT);
            if ((ret != 0) && (ret != DB_NOTFOUND)) {
                printf("Bizarre error: %s\n", db_strerror(ret));
                exit(1);
            }
        }
    }

    ret = cursor->c_close(cursor);
    if (ret != 0) {
        printf("Can't close the cursor (?!): %s\n", db_strerror(ret));
        exit(1);
    }

    ret = db->close(db, 0);
    if (ret != 0) {
        printf("Unable to close db file: %s\n", db_strerror(ret));
        exit(1);
    }

    if ( display_mode & SHOWSUMMARY) {

        if ( allids_cnt > 0 ) {
            printf("Index keys that reached ALLIDs threshold: %ld\n", allids_cnt);
        }

        if ( pres_cnt > 0 ) {
            printf("Presence index keys: %ld\n", pres_cnt);
        }

        if ( eq_cnt > 0 ) {
            printf("Equality index keys: %ld\n", eq_cnt);
        }

        if ( app_cnt > 0 ) {
            printf("Approximate index keys: %ld\n", app_cnt);
        }

        if ( sub_cnt > 0 ) {
            printf("Substring index keys: %ld\n", sub_cnt);
        }

        if ( match_cnt > 0 ) {
            printf("Match index keys: %ld\n", match_cnt);
        }

        if ( ind_cnt > 0 ) {
            printf("Indirect index keys: %ld\n", ind_cnt);
        }

        if ( other_cnt > 0 ) {
            printf("This file contains %ld number of unknown type ( possible corruption)\n",other_cnt);
        }

    }

    ret = env->close(env, 0);
    if (ret != 0) {
        printf("Unable to shutdown libdb: %s\n", db_strerror(ret));
        exit(1);
    }

    return 0;
}
