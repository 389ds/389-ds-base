/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef PROPSET_H
#define PROPSET_H

#include "reshash.h"


enum {
    LANGUAGE_NONE = 0,
    LANGUAGE_LOAD,
    LANGUAGE_INVALID
} ;

enum {
    BACKSLASH = 1,
    BACKSLASH_U
};


typedef struct LanguageStatusS {
    char *language;
    int status;
    struct LanguageStatusS *next;
} LanguageStatus;

typedef struct PropertiesSet {
    char *path;
    char *directory;
    char *filename;
    LanguageStatus *langlist;
    ResHash *res;
} PropertiesSet;


PropertiesSet * PropertiesInit(char *directory, char *file);
const char *PropertiesGetString(PropertiesSet *propset, char *key, ACCEPT_LANGUAGE_LIST acceptlangauge);
void PropertiesDestroy(PropertiesSet *propfile);

#endif
