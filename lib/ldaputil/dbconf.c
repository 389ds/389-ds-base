/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <string.h>
#include <malloc.h>
#include <ctype.h>

#include <ldaputil/errors.h>
#include <ldaputil/certmap.h>
#include <ldaputil/encode.h>
#include <ldaputil/dbconf.h>

#define BIG_LINE 1024

static const char *DB_DIRECTIVE = "directory";
static const int DB_DIRECTIVE_LEN = 9;	/* strlen("DB_DIRECTIVE") */

static const char *ENCODED = "encoded";

static void insert_dbinfo_propval(DBConfDBInfo_t *db_info,
				  DBPropVal_t *propval)
{
    if (db_info->lastprop) {
	db_info->lastprop->next = propval;
    }
    else {
	db_info->firstprop = propval;
    }

    db_info->lastprop = propval;
}

static void insert_dbconf_dbinfo(DBConfInfo_t *conf_info,
				 DBConfDBInfo_t *db_info)
{
    if (conf_info->lastdb) {
	conf_info->lastdb->next = db_info;
    }
    else {
	conf_info->firstdb = db_info;
    }

    conf_info->lastdb = db_info;
}

void dbconf_free_propval (DBPropVal_t *propval)
{
    if (propval) {
	if (propval->prop) free(propval->prop);
	if (propval->val) free(propval->val);
	memset((void *)propval, 0, sizeof(DBPropVal_t));
	free(propval);
    }
}

NSAPI_PUBLIC void dbconf_free_dbinfo (DBConfDBInfo_t *db_info)
{
    if (db_info) {
	DBPropVal_t *next;
	DBPropVal_t *cur;

	if (db_info->dbname) free(db_info->dbname);
	if (db_info->url) free(db_info->url);

	cur = db_info->firstprop;

	while(cur) {
	    next = cur->next;
	    dbconf_free_propval(cur);
	    cur = next;
	}

	memset((void *)db_info, 0, sizeof(DBConfDBInfo_t));
	free(db_info);
    }	
}

NSAPI_PUBLIC void dbconf_free_confinfo (DBConfInfo_t *conf_info)
{
    DBConfDBInfo_t *next;
    DBConfDBInfo_t *cur;

    if (conf_info) {
	cur = conf_info->firstdb;

	while (cur) {
	    next = cur->next;
	    dbconf_free_dbinfo(cur);
	    cur = next;
	}

	memset((void *)conf_info, 0, sizeof(DBConfInfo_t));
	free(conf_info);
    }
}

static int skip_blank_lines_and_spaces(FILE *fp, char *buf, char **ptr_out,
				       int *eof)
{
    char *ptr = buf;
    char *end;

    while(buf && (*buf || fgets(buf, BIG_LINE, fp))) {
	ptr = buf;

	/* skip leading whitespace */
	while(*ptr && isspace(*ptr)) ++ptr;

	/* skip blank line or comment */
	if (!*ptr || *ptr == '#') {
	    *buf = 0;		/* to force reading of next line */
	    continue;
	}

	/* Non-blank line found */
	break;
    }

    *ptr_out = ptr;
    if (!*ptr) {
	*eof = 1;
    }
    else {
	/* skip trailing whitespace */
	end = ptr + strlen(ptr) - 1;
	while(isspace(*end)) *end-- = 0;
    }

    return LDAPU_SUCCESS;
}

static int dbconf_parse_propval (char *buf, char *ptr,
				 DBConfDBInfo_t *db_info)
{
    char *dbname = db_info->dbname;
    int dbname_len = strlen(dbname);
    char *prop;
    char *val;
    DBPropVal_t *propval;
    char *delimeter_chars = " \t";
    char *lastchar;
    int end_of_prop;
    char *encval = 0;		/* encoded value */
    char *origprop = 0;

    if ((ptr - buf + dbname_len > BIG_LINE) ||
	strncmp(ptr, dbname, dbname_len) ||
	!(ptr[dbname_len] == ':' || isspace(ptr[dbname_len])))
    {
	/* Not a prop-val for the current db but not an error */
	return LDAPU_ERR_NOT_PROPVAL;
    }

    /* remove the last char if it is newline */
    lastchar = strrchr(buf, '\n');
    if (lastchar) *lastchar = '\0';
    
    prop = ptr + dbname_len + 1;

    while(*prop && (isspace(*prop) || *prop == ':')) ++prop;

    if (!*prop) {
	return LDAPU_ERR_PROP_IS_MISSING;
    }

    end_of_prop = strcspn(prop, delimeter_chars);

    if (prop[end_of_prop] != '\0') {
	/* buf doesn't end here -- val is present */
	prop[end_of_prop] = '\0';
	val = &prop[end_of_prop + 1];
	    
	while(*val && isspace(*val)) ++val;
	if (*val == '\0') val = 0;
    }
    else {
	val = 0;
    }

    /*
     * The prop-val line could be one of the following:
     * "<dbname>:prop val" OR "<dbname>:encoded prop encval"
     * If (prop == "encoded") then the val has "prop encval".
     * Get the actual prop from val and get encval (i.e. encoded value)
     * and decode it.  If it is encoded then the val part must be non-NULL.
     */
    if (val && *val && !strcmp(prop, ENCODED)) {
	/* val has the actual prop followed by the encoded value */
	origprop = prop;
	prop = val;
	while(*prop && (isspace(*prop) || *prop == ':')) ++prop;
	
	if (!*prop) {
	    return LDAPU_ERR_PROP_IS_MISSING;
	}
	
	end_of_prop = strcspn(prop, delimeter_chars);
	
	if (prop[end_of_prop] != '\0') {
	    /* buf doesn't end here -- encval is present */
	    prop[end_of_prop] = '\0';
	    encval = &prop[end_of_prop + 1];
	    
	    while(*encval && isspace(*encval)) ++encval;
	    if (*encval == '\0') encval = 0;
	}
	else {
	    encval = 0;
	}

	if (!encval) {
	    /* special case - if encval is null, "encoded" itself is a
	     * property and what we have in prop now is the value. */
	    val = prop;
	    prop = origprop;
	}
	else {
	    /* decode the value */
	    val = dbconf_decodeval(encval);
	}
    }

    /* Success - we have prop & val */
    propval = (DBPropVal_t *)malloc(sizeof(DBPropVal_t));

    if (!propval) return LDAPU_ERR_OUT_OF_MEMORY;
    memset((void *)propval, 0, sizeof(DBPropVal_t));
    propval->prop = strdup(prop);
    propval->val = val ? strdup(val) : 0;

    if (!propval->prop || (val && !propval->val)) {
	dbconf_free_propval(propval);
	return LDAPU_ERR_OUT_OF_MEMORY;
    }

    if (encval) free(val);	/* val was allocated by dbconf_decodeval */

    insert_dbinfo_propval(db_info, propval);
    return LDAPU_SUCCESS;
}

static int dbconf_read_propval (FILE *fp, char *buf, DBConfDBInfo_t *db_info,
				int *eof)
{
    int rv;
    char *ptr = buf;

    while(buf && (*buf || fgets(buf, BIG_LINE, fp))) {
	ptr = buf;

	rv = skip_blank_lines_and_spaces(fp, buf, &ptr, eof);

	if (rv != LDAPU_SUCCESS || *eof) return rv;
	
	/* We have a non-blank line which could be prop-val pair for the
	 * dbname in the db_info. parse the prop-val pair and continue.
	 */
	rv = dbconf_parse_propval(buf, ptr, db_info);

	if (rv == LDAPU_ERR_NOT_PROPVAL) return LDAPU_SUCCESS;
	if (rv != LDAPU_SUCCESS) return rv;

	*buf = 0;		/* to force reading of next line */
    }

    if (!*buf) *eof = 1;

    return LDAPU_SUCCESS;
}

static int parse_directive(char *buf, const char *directive,
			   const int directive_len,
			   DBConfDBInfo_t **db_info_out)
{
    DBConfDBInfo_t *db_info;
    char *dbname;
    char *url;
    int end_of_dbname;
    char *delimeter_chars = " \t";
    char *lastchar;

    /* remove the last char if it is newline */
    lastchar = strrchr(buf, '\n');	
    if (lastchar) *lastchar = '\0';
	
    if (strncmp(buf, directive, directive_len) ||
	!isspace(buf[directive_len]))
    {
	return LDAPU_ERR_DIRECTIVE_IS_MISSING;
    }

    dbname = buf + directive_len + 1;

    while(*dbname && isspace(*dbname)) ++dbname;

    if (!*dbname) {
	return LDAPU_ERR_DBNAME_IS_MISSING;
    }

    end_of_dbname = strcspn(dbname, delimeter_chars);

    if (dbname[end_of_dbname] != '\0') {
	/* buf doesn't end here -- url is present */
	dbname[end_of_dbname] = '\0';
	url = &dbname[end_of_dbname + 1];
	    
	while(*url && isspace(*url)) ++url;

	if (*url == '\0') url = 0;
    }
    else {
	url = 0;
    }

    /* Success - we have dbname & url */
    db_info = (DBConfDBInfo_t *)malloc(sizeof(DBConfDBInfo_t));

    if (!db_info) return LDAPU_ERR_OUT_OF_MEMORY;
    memset((void *)db_info, 0, sizeof(DBConfDBInfo_t));
    db_info->dbname = strdup(dbname);
    db_info->url = url ? strdup(url) : 0;

    if (!db_info->dbname || (url && !db_info->url)) {
	dbconf_free_dbinfo(db_info);
	return LDAPU_ERR_OUT_OF_MEMORY;
    }

    *db_info_out = db_info;
    return LDAPU_SUCCESS;
}

/* Read the next database info from the file and put it in db_info_out.  The
 * buf may contain first line of the database info.  When this function
 * finishes, the buf may contain unprocessed information (which should be
 * passed to the next call to read_db_info).
 */
static int read_db_info (FILE *fp, char *buf, DBConfDBInfo_t **db_info_out,
			 const char *directive, const int directive_len,
			 int *eof)
{
    char *ptr;
    int found_directive = 0;
    DBConfDBInfo_t *db_info;
    int rv;

    *db_info_out = 0;

    rv = skip_blank_lines_and_spaces(fp, buf, &ptr, eof);

    if (rv != LDAPU_SUCCESS || *eof) return rv;

    /* We possibly have a directive of the form "directory <name> <url>" */
    rv = parse_directive(ptr, directive, directive_len, &db_info);
    if (rv != LDAPU_SUCCESS) return rv;

    /* We have parsed the directive successfully -- lets look for additional
     * property-value pairs for the database.
     */
    if (!fgets(buf, BIG_LINE, fp)) {
	*eof = 1;
	rv = LDAPU_SUCCESS;
    }
    else {
        rv = dbconf_read_propval(fp, buf, db_info, eof);
    }
	
    if (rv != LDAPU_SUCCESS) {
	dbconf_free_dbinfo(db_info);
	*db_info_out = 0;
    }
    else {
	*db_info_out = db_info;
    }

    return rv;
}

int dbconf_read_config_file_sub (const char *file,
				 const char *directive,
				 const int directive_len,
				 DBConfInfo_t **conf_info_out)
{
    FILE *fp;
    DBConfInfo_t *conf_info;
    DBConfDBInfo_t *db_info;
    char buf[BIG_LINE];
    int rv;
    int eof;

    buf[0] = 0;

#ifdef XP_WIN32
    if ((fp = fopen(file, "rt")) == NULL)
#else
    if ((fp = fopen(file, "r")) == NULL)
#endif
    {
	return LDAPU_ERR_CANNOT_OPEN_FILE;
    }

    /* Allocate DBConfInfo_t */
    conf_info = (DBConfInfo_t *)malloc(sizeof(DBConfInfo_t));

    if (!conf_info) {
	fclose(fp);
	return LDAPU_ERR_OUT_OF_MEMORY;
    }

    memset((void *)conf_info, 0, sizeof(DBConfInfo_t));

    /* Read each db info */
    eof = 0;
    while(!eof &&
	  ((rv = read_db_info(fp, buf, &db_info, directive, directive_len, &eof)) == LDAPU_SUCCESS))
    {
	insert_dbconf_dbinfo(conf_info, db_info);
    }

    if (rv != LDAPU_SUCCESS) {
	dbconf_free_confinfo(conf_info);
	*conf_info_out = 0;
    }
    else {
	*conf_info_out = conf_info;
    }

    fclose(fp);
    return rv;
}

NSAPI_PUBLIC int dbconf_read_config_file (const char *file, DBConfInfo_t **conf_info_out)
{
    return dbconf_read_config_file_sub(file, DB_DIRECTIVE, DB_DIRECTIVE_LEN,
				       conf_info_out);
}

int dbconf_read_default_dbinfo_sub (const char *file,
				    const char *directive,
				    const int directive_len,
				    DBConfDBInfo_t **db_info_out)
{
    FILE *fp;
    DBConfDBInfo_t *db_info;
    char buf[BIG_LINE];
    int rv;
    int eof;

    buf[0] = 0;

#ifdef XP_WIN32
    if ((fp = fopen(file, "rt")) == NULL)
#else
    if ((fp = fopen(file, "r")) == NULL)
#endif
    {
	return LDAPU_ERR_CANNOT_OPEN_FILE;
    }

    /* Read each db info until eof or dbname == default*/
    eof = 0;

    while(!eof &&
	  ((rv = read_db_info(fp, buf, &db_info, directive, directive_len, &eof)) == LDAPU_SUCCESS))
    {
	if (!strcmp(db_info->dbname, DBCONF_DEFAULT_DBNAME)) break;
	dbconf_free_dbinfo(db_info);
    }

    if (rv != LDAPU_SUCCESS) {
	*db_info_out = 0;
    }
    else {
	*db_info_out = db_info;
    }

    fclose(fp);
    return rv;
}


NSAPI_PUBLIC int dbconf_read_default_dbinfo (const char *file,
					     DBConfDBInfo_t **db_info_out)
{
    return dbconf_read_default_dbinfo_sub(file, DB_DIRECTIVE, DB_DIRECTIVE_LEN,
					  db_info_out);
}

/*
 * ldapu_strncasecmp - is like strncasecmp on UNIX but also accepts null strings.
 */
/* Not tested */
static int ldapu_strncasecmp (const char *s1, const char *s2, size_t len)
{
    int ls1, ls2;		/* tolower values of chars in s1 & s2 resp. */

    if (0 == len) return 0;
    else if (!s1) return !s2 ? 0 : 0-tolower(*s2);
    else if (!s2) return tolower(*s1);

#ifdef XP_WIN32
    while(len > 0 && *s1 && *s2 &&
	  (ls1 = tolower(*s1)) == (ls2 = tolower(*s2)))
    {
	s1++; s2++; len--;
    }

    if (0 == len)
	return 0;
    else if (!*s1)
	return *s2 ? 0-tolower(*s2) : 0;
    else if (!*s2)
	return tolower(*s1);
    else
	return ls1 - ls2;
#else
    return strncasecmp(s1, s2, len);
#endif
}


/*
 * ldapu_strcasecmp - is like strcasecmp on UNIX but also accepts null strings.
 */
int ldapu_strcasecmp (const char *s1, const char *s2)
{
    int ls1, ls2;		/* tolower values of chars in s1 & s2 resp. */

    if (!s1) return !s2 ? 0 : 0-tolower(*s2);
    else if (!s2) return tolower(*s1);

#ifdef XP_WIN32
    while(*s1 && *s2 && (ls1 = tolower(*s1)) == (ls2 = tolower(*s2))) { s1++; s2++; }

    if (!*s1)
	return *s2 ? 0-tolower(*s2) : 0;
    else if (!*s2)
	return tolower(*s1);
    else
	return ls1 - ls2;
#else
    return strcasecmp(s1, s2);
#endif
}

NSAPI_PUBLIC int ldapu_dbinfo_attrval (DBConfDBInfo_t *db_info,
				       const char *attr, char **val)
{
    /* Look for given attr in the db_info and return its value */
    int rv = LDAPU_ATTR_NOT_FOUND;
    DBPropVal_t *next;

    *val = 0;

    if (db_info) {
	next = db_info->firstprop;
	while (next) {
            rv = ldapu_strcasecmp(attr, next->prop);
	    if (!rv) {
		/* Found the property */
		*val = next->val ? strdup(next->val) : 0;

		if (next->val && !*val) {
		    rv = LDAPU_ERR_OUT_OF_MEMORY;
		}
		else {
		    rv = LDAPU_SUCCESS;
		}
		break;
	    }
	    next = next->next;
	}
    }

    return rv;
}

void dbconf_print_propval (DBPropVal_t *propval)
{
    if (propval) {
	fprintf(stderr, "\tprop: \"%s\"\tval: \"%s\"\n", propval->prop,
	       propval->val ? propval->val : "");
    }
    else {
	fprintf(stderr, "Null propval\n");
    }
}

void dbconf_print_dbinfo (DBConfDBInfo_t *db_info)
{
    DBPropVal_t *next;

    if (db_info) {
	fprintf(stderr, "dbname: \"%s\"\n", db_info->dbname);
	fprintf(stderr, "url: \t\"%s\"\n", db_info->url ? db_info->url : "");
	next = db_info->firstprop;
	while (next) {
	    dbconf_print_propval(next);
	    next = next->next;
	}
    }
    else {
	fprintf(stderr, "Null db_info\n");
    }
}

void dbconf_print_confinfo (DBConfInfo_t *conf_info)
{
    DBConfDBInfo_t *next;

    if (conf_info) {
	next = conf_info->firstdb;
	while (next) {
	    dbconf_print_dbinfo(next);
	    next = next->next;
	}
    }
    else {
	fprintf(stderr, "Null conf_info\n");
    }
}



NSAPI_PUBLIC int dbconf_output_db_directive (FILE *fp, const char *dbname,
	const char *url)
{
    fprintf(fp, "%s %s %s\n", DB_DIRECTIVE, dbname, url);
    return LDAPU_SUCCESS;
}

NSAPI_PUBLIC int dbconf_output_propval (FILE *fp, const char *dbname,
	const char *prop, const char *val, const int encoded)
{
    if (encoded && val && *val) {
	char *new_val = dbconf_encodeval(val);

	if (!new_val) return LDAPU_ERR_OUT_OF_MEMORY;
	fprintf(fp, "%s:%s %s %s\n", dbname, ENCODED,
		prop, new_val);
	free(new_val);
    }
    else {
	fprintf(fp, "%s:%s %s\n", dbname, prop, val ? val : "");
    }

    return LDAPU_SUCCESS;
}



NSAPI_PUBLIC int dbconf_get_dbnames (const char *dbmap, char ***dbnames_out, int *cnt_out)
{
    DBConfInfo_t *conf_info = 0;
    DBConfDBInfo_t *db = 0;
    int cnt = 0;
    char **dbnames = 0;
    char *heap = 0;
    int rv;

    *dbnames_out = 0;
    *cnt_out = 0;

    rv = dbconf_read_config_file(dbmap, &conf_info);

    if (rv != LDAPU_SUCCESS) return rv;

    db = conf_info->firstdb;

    
    dbnames = (char **)malloc(32*1024);
    heap = (char *)dbnames + 2*1024;

    if (!dbnames) {
	dbconf_free_confinfo(conf_info);
	return LDAPU_ERR_OUT_OF_MEMORY;
    }

    *dbnames_out = dbnames;

    while(db) {
	*dbnames++ = heap;
	strcpy(heap, db->dbname);
	heap += strlen(db->dbname)+1;
	db = db->next;
	cnt++;
    }

    *dbnames = NULL;
    *cnt_out = cnt;
    dbconf_free_confinfo(conf_info);

    return LDAPU_SUCCESS;
}

NSAPI_PUBLIC int dbconf_free_dbnames (char **dbnames)
{
    if (dbnames)
	free(dbnames);

    return LDAPU_SUCCESS;
}
