/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef TXTFILE_H
#define TXTFILE_H

#define FILE_BUFFER_SIZE 2024

/* file status */
enum
{
    TEXT_FILE_NONE,
    TEXT_FILE_READING,
    TEXT_FILE_WRITING,
    TEXT_FILE_DONE
};

typedef struct TEXTFILE
{
    FILE *file;
    char *fbCurrent;
    int fbSize;
    int fbStatus;
    char fileBuffer[FILE_BUFFER_SIZE + 1];
} TEXTFILE;

enum
{
    TEXT_OPEN_FOR_READ,
    TEXT_OPEN_FOR_WRITE
};


#ifdef __cplusplus
extern "C" {
#endif

TEXTFILE *OpenTextFile(char *filename, int access);
void CloseTextFile(TEXTFILE *txtfile);
int ReadTextLine(TEXTFILE *txtfile, char *linebuf);

#ifdef CPLUSPLUS
};
#endif

#endif
