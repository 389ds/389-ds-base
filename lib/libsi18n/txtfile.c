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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
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
