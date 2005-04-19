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
/*
 * this is used for generating the (large) scripts during create_instance.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "portable.h"
#if defined( XP_WIN32 )
#include <io.h>
#endif

/* reads the file on inpath, and rewrites it on outpath.
 * 'table' is a list of string-pairs (terminated by a pair of NULLs) that
 * indicate substitution pairs.  for example, the pair:
 *     "SERVER-ROOT", "/export/home/slapd-bastille"
 * means to substitute any occurance of "{{SERVER-ROOT}}" in the file with
 * "/export/home/slapd-bastille".
 *
 * returns 0 on success, -1 if it had trouble opening or reading/writing
 * the two files.
 */
#define GS_BUFLEN 256
int generate_script(const char *inpath, const char *outpath, int mode,
                    const char *table[][2])
{
    FILE *fin, *fout;
    char buffer[GS_BUFLEN], save_buffer[GS_BUFLEN];
    char *p, *q;
    int i;

    fin = fopen(inpath, "r");
    if (fin == NULL) {
        return -1;
    }
    fout = fopen(outpath, "w");
    if (fout == NULL) {
        fclose(fin);
        return -1;
    }

    while (!feof(fin)) {
        fgets(buffer, GS_BUFLEN, fin);
        if (feof(fin)) {
            break;
        }
        buffer[GS_BUFLEN-1] = 0;
        if (buffer[strlen(buffer)-1] == '\n') {
            buffer[strlen(buffer)-1] = 0;
        }
        if (buffer[strlen(buffer)-1] == '\r') {
            buffer[strlen(buffer)-1] = 0;
        }

        p = buffer;
        while ((p = strstr(p, "{{")) != NULL) {
            q = strstr(p+2, "}}");
            if (q == NULL) {
                /* skip this one then */
                p += 2;
                continue;
            }

            /* key between {{ }} is now in [p+2, q-1] */
            for (i = 0; table[i][0] != NULL; i++) {
                if ((strlen(table[i][0]) == (q-(p+2))) &&
                    (strncasecmp(table[i][0], p+2, q-(p+2)) == 0)) {
                    /* match!  ...but is there room for the subtitution? */
                    int extra = strlen(table[i][1]) - (q+2-p);

                    if (strlen(buffer) + extra > GS_BUFLEN-1) {
                        /* not enough room, scratch it */
                        continue;
                    }
                    strncpy(save_buffer, q+2, sizeof(save_buffer)-1);
					save_buffer[sizeof(save_buffer)-1] = (char)0;
                    strcpy(p, table[i][1]);
                    strcat(p, save_buffer);
                    q = p;
                    break;      /* out of the for loop */
                }
            }

            /* move on... */
            p = q;
        }

        fprintf(fout, "%s\n", buffer);
    }

#if defined( XP_UNIX )
    fchmod(fileno(fout), mode);
#endif

    fclose(fin);
    fclose(fout);

#if defined( XP_WIN32 )
    chmod(outpath, mode);
#endif

    return 0;
}
