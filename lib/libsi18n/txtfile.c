/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include "txtfile.h"



#if 0
char fileBuffer[FILE_BUFFER_SIZE + 1];
char *fbCurrent;
int  fbSize;
int fbStatus;
#endif


TEXTFILE * OpenTextFile(char *filename, int access)
{
    TEXTFILE *txtfile;
    FILE *file;
    int status;

    if (access == TEXT_OPEN_FOR_WRITE) {
        status = TEXT_FILE_WRITING;
	    file = fopen(filename, "w+");
    }
    else {
        status = TEXT_FILE_READING;
	    file = fopen(filename, "r");
    }

    if (file == NULL)
        return NULL;

    txtfile = (TEXTFILE *) malloc (sizeof(TEXTFILE));
    memset(txtfile, 0, sizeof(TEXTFILE));

    txtfile->file = file;
    txtfile->fbStatus = status;

	txtfile->fbCurrent = txtfile->fileBuffer;
	*txtfile->fbCurrent = '\0';
	txtfile->fbSize = 0;
    return txtfile;
}


void CloseTextFile(TEXTFILE *txtfile)
{
    if (txtfile) {
	    fclose(txtfile->file);
        free(txtfile);
    }
    
}

int FillTextBuffer(TEXTFILE *txtfile)
{
	int nLeft, size;
	nLeft = strlen(txtfile->fbCurrent); 
	memcpy(txtfile->fileBuffer, txtfile->fbCurrent, nLeft+1);

	size = fread(txtfile->fileBuffer + nLeft, 1, FILE_BUFFER_SIZE - nLeft, txtfile->file);
	if (size == 0)
		return 0;

	txtfile->fbCurrent = txtfile->fileBuffer;
	*(txtfile->fbCurrent + size + nLeft) = '\0';
	txtfile->fbSize = size + nLeft;

	return size;
}

int ReadTextLine(TEXTFILE *txtfile, char *linebuf)
{
	char *p, *q;

    if (txtfile->fbStatus == TEXT_FILE_DONE)
        return -1;

	p = txtfile->fbCurrent;
	q = strchr(p, '\n');
	if (q)
	{
		*q = '\0';
		strcpy(linebuf, p);
		txtfile->fbCurrent = q + 1;
		return strlen(linebuf);
	}
	else
	{
		if (FillTextBuffer(txtfile) == 0)
		{   /* Done with file reading,
               return last line
             */
            txtfile->fbStatus = TEXT_FILE_DONE;
            if (*txtfile->fbCurrent) {
			    strcpy(linebuf, txtfile->fbCurrent);
                CloseTextFile(txtfile);
                return strlen(linebuf);
            }
            else {
                CloseTextFile(txtfile);
                return -1;
            }
		}
        else { 
		    p = txtfile->fbCurrent;
		    q = strchr(p, '\n');
		    if (q)
		    {
			    *q = '\0';
			    strcpy(linebuf, p);
			    txtfile->fbCurrent = q + 1;
		    }
		    else
		    {
			    strcpy(linebuf, txtfile->fbCurrent);
			    txtfile->fbCurrent = txtfile->fbCurrent + strlen(linebuf);
		    }
        }
		return strlen(linebuf);
	}
}
