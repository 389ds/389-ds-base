#ident "@(#)parser.c    1.5 01/04/11"

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
    FILE :        parser.c
    AUTHOR :        Jean-Luc SCHWING
    VERSION :       1.0
    DATE :        19 March 2001
    DESCRIPTION :
            This file contains the parser functions of ldclt
    LOCAL :        None.
    HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author    | Comments
---------+--------------+------------------------------------------------------
19/03/01 | JL Schwing    | Creation
---------+--------------+------------------------------------------------------
21/03/01 | JL Schwing    | 1.2 : Implements variables in "-e object=filename"
---------+--------------+------------------------------------------------------
23/03/01 | JL Schwing    | 1.3 : Implements data file list support in variants.
            | Bug fix : close the file !
            | Implements "-e rdn=value".
---------+--------------+------------------------------------------------------
28/03/01 | JL Schwing    | 1.4 : Support -e commoncounter with -e rdn/object
---------+--------------+------------------------------------------------------
11/04/01 | JL Schwing    | 1.5 : Implement [INCRFROMFILE<NOLOOP>(myfile)]
            | Improved error message.
---------+--------------+------------------------------------------------------
*/


#include <stdio.h>  /* printf(), etc... */
#include <string.h> /* strcpy(), etc... */
#include <errno.h>  /* errno, etc... */
#include <stdlib.h> /* malloc(), etc... */
#include <lber.h>   /* ldap C-API BER declarations */
#include <ldap.h>   /* ldap C-API declarations */
#ifdef LDAP_H_FROM_QA_WKA
#include <proto-ldap.h> /* ldap C-API prototypes */
#endif
#include <unistd.h>  /* close(), etc... */
#include <pthread.h> /* pthreads(), etc... */

#include "port.h"  /* Portability definitions */
#include "ldclt.h" /* This tool's include file */
#include "utils.h" /* Utilities functions */


/* ****************************************************************************
    FUNCTION :    decodeHow
    PURPOSE :    Decode the how field
    INPUT :        how    = field to decode
    OUTPUT :    None.
    RETURN :    -1 if error, how value else.
    DESCRIPTION :
 *****************************************************************************/
int
decodeHow(
    char *how)
{
    if (mctx.mode & VERY_VERBOSE)
        printf("decodeHow: how=\"%s\"\n", how);

    if (!strcmp(how, "INCRFROMFILE"))
        return (HOW_INCR_FROM_FILE);
    if (!strcmp(how, "INCRFROMFILENOLOOP"))
        return (HOW_INCR_FROM_FILE_NL);
    if (!strcmp(how, "INCRN"))
        return (HOW_INCR_NB);
    if (!strcmp(how, "INCRNNOLOOP"))
        return (HOW_INCR_NB_NOLOOP);
    if (!strcmp(how, "RNDFROMFILE"))
        return (HOW_RND_FROM_FILE);
    if (!strcmp(how, "RNDN"))
        return (HOW_RND_NUMBER);
    if (!strcmp(how, "RNDS"))
        return (HOW_RND_STRING);
    return (-1);
}


/* ****************************************************************************
    FUNCTION :    parseVariant
    PURPOSE :    Parse a variant definition.
    INPUT :        variant    = string to parse
            fname    = file name
            line    = source line
            obj    = object we are parsing and building
    OUTPUT :    field    = field parsed
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
parseVariant(
    char *variant,
    char *fname,
    char *line,
    vers_object *obj,
    vers_field *field)
{
    int start, end;          /* For the loops */
    char how[MAX_FILTER];    /* To parse the variant : */
    char first[MAX_FILTER];  /*   how(first)              */
    char second[MAX_FILTER]; /*   how(first,second)       */
    char third[MAX_FILTER];  /*   how(first,second,third) */
    int ret;                 /* ldclt_mutex_init() return value */

    if (mctx.mode & VERY_VERBOSE)
        printf("parseVariant: variant=\"%s\"\n", variant);

    /*
   * Maybe a variable ?
   */
    if (variant[1] == '\0') {
        if ((variant[0] < VAR_MIN) || (variant[0] > VAR_MAX)) {
            fprintf(stderr, "Error: bad variable in %s : \"%s\"\n", fname, line);
            fprintf(stderr, "Error: must be in [%c-%c]\n", VAR_MIN, VAR_MAX);
            return (-1);
        }
        field->how = HOW_VARIABLE;
        field->var = variant[0] - VAR_MIN;
        return (0);
    }

    /*
   * Maybe a variable definition ?
   */
    if (variant[1] != '=')
        field->var = -1;
    else {
        if ((variant[0] < VAR_MIN) || (variant[0] > VAR_MAX)) {
            fprintf(stderr, "Error: bad variable in %s : \"%s\"\n", fname, line);
            fprintf(stderr, "Error: must be in [%c-%c]\n", VAR_MIN, VAR_MAX);
            return (-1);
        }
        field->var = variant[0] - VAR_MIN;
        variant++; /* Skip variable name */
        variant++; /* Skip '=' */

        /*
     * We need a variable !
     */
        if (obj->var[field->var] == NULL)
            obj->var[field->var] = (char *)safe_malloc(MAX_FILTER);
    }

    /*
   * Find how definition
   */
    for (end = 0; (variant[end] != '\0') && (variant[end] != '('); end++)
        ;
    if (variant[end] == '\0') {
        fprintf(stderr, "Error: bad variant in %s : \"%s\"\n", fname, line);
        fprintf(stderr, "Error: missing '('\n");
        return (-1);
    }
    strncpy(how, variant, end);
    how[end] = '\0';

    /*
   * Parse the first parameter
   */
    end++; /* Skip '(' */
    for (start = end; (variant[end] != '\0') && (variant[end] != ';') && (variant[end] != ')'); end++)
        ;
    if (variant[end] == '\0') {
        fprintf(stderr, "Error: bad variant in %s : \"%s\"\n", fname, line);
        fprintf(stderr, "Error: missing ')'\n");
        return (-1);
    }
    strncpy(first, variant + start, end - start);
    first[end - start] = '\0';

    /*
   * Parse the second parameter
   */
    if (variant[end] == ')')
        second[0] = '\0';
    else {
        end++; /* Skip ';' */
        for (start = end; (variant[end] != '\0') && (variant[end] != ';') && (variant[end] != ')'); end++)
            ;
        if (variant[end] == '\0') {
            fprintf(stderr, "Error: bad variant in %s : \"%s\"\n", fname, line);
            fprintf(stderr, "Error: missing ')'\n");
            return (-1);
        }
        strncpy(second, variant + start, end - start);
        second[end - start] = '\0';
    }

    /*
   * Parse the third parameter
   */
    if (variant[end] == ')')
        third[0] = '\0';
    else {
        end++; /* Skip ';' */
        for (start = end; (variant[end] != '\0') && (variant[end] != ')'); end++)
            ;
        if (variant[end] == '\0') {
            fprintf(stderr, "Error: bad variant in %s : \"%s\"\n", fname, line);
            fprintf(stderr, "Error: missing ')'\n");
            return (-1);
        }
        strncpy(third, variant + start, end - start);
        third[end - start] = '\0';
    }

    /*
   * Analyse it
   * Note : first parameter always exist (detected when parsing) while
   *        second and third may not have been provided by the user.
   */
    switch (field->how = decodeHow(how)) {
    case HOW_INCR_FROM_FILE:
    case HOW_INCR_FROM_FILE_NL:
    case HOW_RND_FROM_FILE:
        if ((field->dlf = dataListFile(first)) == NULL) {
            fprintf(stderr, "Error : bad file in %s : \"%s\"\n", fname, line);
            return (-1);
        }

        /*
     * Useless for HOW_RND_FROM_FILE
     */
        field->cnt = 0;
        field->low = 0;
        field->high = field->dlf->strNb - 1;

        /*
     * Maybe common counter ?
     */
        if ((mctx.mode & COMMON_COUNTER) &&
            ((field->how == HOW_INCR_FROM_FILE) ||
             (field->how == HOW_INCR_FROM_FILE_NL))) {
            if ((ret = ldclt_mutex_init(&(field->cnt_mutex))) != 0) {
                fprintf(stderr, "ldclt: %s\n", strerror(ret));
                fprintf(stderr, "Error: cannot initiate cnt_mutex in %s for %s\n",
                        fname, line);
                fflush(stderr);
                return (-1);
            }
        }
        break;
    case HOW_INCR_NB:
    case HOW_INCR_NB_NOLOOP:
    case HOW_RND_NUMBER:
        if (third[0] == '\0') {
            fprintf(stderr, "Error : missing parameters in %s : \"%s\"\n",
                    fname, line);
            return (-1);
        }
        field->cnt = atoi(first);
        field->low = atoi(first);
        field->high = atoi(second);
        field->nb = atoi(third);

        /*
     * Maybe common counter ?
     */
        if ((mctx.mode & COMMON_COUNTER) &&
            ((field->how == HOW_INCR_NB) || (field->how == HOW_INCR_NB_NOLOOP))) {
            if ((ret = ldclt_mutex_init(&(field->cnt_mutex))) != 0) {
                fprintf(stderr, "ldclt: %s\n", strerror(ret));
                fprintf(stderr, "Error: cannot initiate cnt_mutex in %s for %s\n",
                        fname, line);
                fflush(stderr);
                return (-1);
            }
        }
        break;
    case HOW_RND_STRING:
        field->nb = atoi(first);
        break;
    case -1:
        fprintf(stderr, "Error: illegal keyword \"%s\" in %s : \"%s\"\n",
                how, fname, line);
        return (-1);
        break;
    }

    return (0);
}


/* ****************************************************************************
    FUNCTION :    parseAttribValue
    PURPOSE :    Parse the right part of attribname: attribvalue.
    INPUT :        fname    = file name
            obj    = object where variables are.
            line    = value to parse.
    OUTPUT :    attrib    = attribute where the value should be stored
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
parseAttribValue(
    char *fname,
    vers_object *obj,
    char *line,
    vers_attribute *attrib)
{
    char variant[MAX_FILTER]; /* To process the variant */
    int start, end;           /* For the loops */
    vers_field *field;        /* To build the fields */

    if (mctx.mode & VERY_VERBOSE)
        printf("parseAttribValue: line=\"%s\"\n", line);

    /*
   * We will now parse this line for the different fields.
   */
    field = NULL;
    end = start = 0;
    while (line[end] != '\0') {
        /*
     * Allocate a new field
     */
        if (field == NULL) {
            field = (vers_field *)safe_malloc(sizeof(vers_field));
            field->next = NULL;
            attrib->field = field;
        } else {
            field->next = (vers_field *)safe_malloc(sizeof(vers_field));
            field = field->next;
            field->next = NULL;
        }

        /*
     * Is it a variant field ?
     */
        if (line[end] == '[') {
            /*
       * Extract the variant definition
       */
            end++; /* Skip '[' */
            for (start = end; (line[end] != '\0') && (line[end] != ']'); end++)
                ;
            strncpy(variant, line + start, end - start);
            variant[end - start] = '\0';
            if (line[end] == '\0') {
                fprintf(stderr, "Error: missing ']' in %s : \"%s\"\n", fname, line);
                return (-1);
            }
            if (parseVariant(variant, fname, line, obj, field) < 0)
                return (-1);
            end++; /* Skip ']' */

            /*
       * We need to allocate a buffer in this attribute !
       */
            if (attrib->buf == NULL) {
                attrib->buf = (char *)safe_malloc(MAX_FILTER);
                if (mctx.mode & VERY_VERBOSE)
                    printf("parseAttribValue: buffer allocated\n");
            }
        } else {
            /*
       * It is a constant field. Find the end : [ or \0
       */
            for (start = end; (line[end] != '\0') && (line[end] != '['); end++)
                ;
            field->how = HOW_CONSTANT;
            field->cst = (char *)safe_malloc(1 + end - start);
            strncpy(field->cst, line + start, end - start);
            field->cst[end - start] = '\0';
        }
    }

    /*
   * Attribute value is parsed !
   */
    return (0);
}


/* ****************************************************************************
    FUNCTION :    parseLine
    PURPOSE :    Parse the given line to find an attribute definition.
    INPUT :        line    = line to parse
            fname    = file name
    OUTPUT :    obj    = object where the attribute should be added
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
parseLine(
    char *line,
    char *fname,
    vers_object *obj)
{
    int end; /* For the loops */

    if (mctx.mode & VERY_VERBOSE)
        printf("parseLine: line=\"%s\"\n", line);

    /*
   * Empty line ? Comment ?
   * No more place for new attributes ?
   */
    if ((line[0] == '\0') || (line[0] == '#'))
        return (0);
    if (obj->attribsNb == MAX_ATTRIBS) {
        fprintf(stderr, "Error: too many attributes in %s, max is %d\n",
                fname, MAX_ATTRIBS);
        return (-1);
    }

    /*
   * Find the attribute name
   *    name:
   */
    for (end = 0; (line[end] != '\0') && (line[end] != ':'); end++)
        ;
    if (line[end] != ':') {
        fprintf(stderr, "Error: can't find attribute name in %s : \"%s\"\n",
                fname, line);
        return (-1);
    }

    /*
   * Initiate the attribute
   */
    obj->attribs[obj->attribsNb].buf = NULL;
    obj->attribs[obj->attribsNb].src = strdup(line);
    obj->attribs[obj->attribsNb].name = (char *)safe_malloc(1 + end);
    strncpy(obj->attribs[obj->attribsNb].name, line, end);
    obj->attribs[obj->attribsNb].name[end] = '\0';
    for (end++; line[end] == ' '; end++)
        ; /* Skip the leading ' ' */

    /*
   * We will now parse the value of this attribute
   */
    if (parseAttribValue(fname, obj, line + end, &(obj->attribs[obj->attribsNb])) < 0)
        return (-1);

    /*
   * Do not forget to increment attributes number !
   */
    obj->attribsNb++;
    return (0);
}


/* ****************************************************************************
    FUNCTION :    readObject
    PURPOSE :    This function will read an object description from the
            file given in argument.
            The object should be already initiated !!!
    INPUT :        None.
    OUTPUT :    obj    = parsed object.
    RETURN :    -1 if error, 0 else.
    DESCRIPTION :
 *****************************************************************************/
int
readObject(
    vers_object *obj)
{
    FILE *ifile;           /* The file that contains the object to read */
    char line[MAX_FILTER]; /* To read ifile */
    int rc = 0;

    /*
   * Open the file
   */
    ifile = fopen(obj->fname, "r");
    if (ifile == NULL) {
        perror(obj->fname);
        fprintf(stderr, "Error: cannot open file \"%s\"\n", obj->fname);
        rc = -1;
        goto done;
    }

    /*
   * Process each line of the input file.
   * Reminder : the object is initiated by the calling function !
   */
    while (fgets(line, MAX_FILTER, ifile) != NULL) {
        if ((strlen(line) > 0) && (line[strlen(line) - 1] == '\n'))
            line[strlen(line) - 1] = '\0';
        if (parseLine(line, obj->fname, obj) < 0) {
            rc = -1;
            goto done;
        }
    }

done:
    /*
   * Do not forget to close the file !
   */
    if (ifile && fclose(ifile) != 0) {
        perror(obj->fname);
        fprintf(stderr, "Error: cannot fclose file \"%s\"\n", obj->fname);
        rc = -1;
    }

    /*
   * End of function
   */
    if (obj->attribsNb == 0) {
        fprintf(stderr, "Error: no object found in \"%s\"\n", obj->fname);
        rc = -1;
    }
    return rc;
}
