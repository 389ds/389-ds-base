/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef TXTFILE_H
#define TXTFILE_H

#define FILE_BUFFER_SIZE  2024

/* file status */
enum {
    TEXT_FILE_NONE,
    TEXT_FILE_READING,
    TEXT_FILE_WRITING,
    TEXT_FILE_DONE
};

typedef struct TEXTFILE {
    FILE *file;
    char *fbCurrent;
    int  fbSize;
    int  fbStatus;
    char fileBuffer[FILE_BUFFER_SIZE + 1];
} TEXTFILE;

enum {
    TEXT_OPEN_FOR_READ,
    TEXT_OPEN_FOR_WRITE
};


#ifdef __cplusplus
extern "C" {
#endif

TEXTFILE * OpenTextFile(char *filename, int access);
void CloseTextFile(TEXTFILE *txtfile);
int ReadTextLine(TEXTFILE *txtfile, char *linebuf);

#ifdef CPLUSPLUS
};
#endif

#endif
