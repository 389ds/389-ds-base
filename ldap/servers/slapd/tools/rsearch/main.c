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
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * this was just easier to start from scratch.  windows is too different
 * from nspr.
 */

#include <stdio.h>
#include <stdlib.h>
#ifdef XP_UNIX
#include <unistd.h>

#endif
#include "nspr.h"
#include "rsearch.h"
#include "nametable.h"
#include "searchthread.h"


void usage()
{
    printf("\nUsage: rsearch -h host -p port -s suffix -D bindDN -w password\n"
	   "-b        -- bind before every operation\n"
	   "-u        -- don't unbind---just close the connection\n"
	   "-f filter -- Filter\n"
	   "-v        -- verbose\n"
	   "-y        -- nodelay\n"
	   "-q        -- quiet\n"
	   "-l        -- logging\n"
	   "-m        -- Operaton: Modify. -i required\n"
	   "-d        -- Operaton: Delete. -i required\n"
	   "-c        -- Operaton: Compare. -i required\n"
	   "-i file   -- name file\n"
	   "-A attrs  -- Attribute List\n"
	   "-n number -- Reserved for future use\n"
	   "-j number -- Sample interval, in seconds\n"
	   "-t number -- Threads\n\n");
}

/*
 * Convert a string of the form "foo bar baz"
 * into an array of strings. Returns a pointer
 * to allocated memory. Array contains pointers 
 * to the string passed in. So the array needs freed,
 * but the pointers don't.
 */
char **string_to_list(char* s)
{
    int string_count = 0;
    int in_space = 1;
    char *p;

    for (p = s; *p != '\0'; p++) {
	if (in_space) {
	    if (' ' != *p) {
		/* We just found the beginning of a string */
		string_count++;
		in_space = 0;
	    }
	}
	else if (' ' == *p) {
	    /* Back in space again */
	    in_space = 1;
	}
    }
    
    /* Now we have the suckers counted */
    if (string_count > 0) {
	char **return_array = (char **)malloc((string_count+1)*sizeof(char *));
	int index = 0;

	in_space = 1;
	for (p = s; *p != '\0'; p++) {
	    if (in_space) {
		if (' ' != *p) {
		    /* We just found the beginning of a string */
		    return_array[index++] = p;
		    in_space = 0;
		}
	    }
	    else if (' ' == *p) {
		/* Back in space again */
		in_space = 1;
		*p = '\0';
	    }
	}
	return_array[index] = 0;
	return return_array;
    }
    else return 0;
}


/* global data for the threads to share */
char *hostname = "localhost";
int port = 389;
int numeric = 0;
int threadCount = 1;
int verbose = 0;
int logging = 0;
int doBind = 0;
int cool = 0;
int quiet = 0;
int noDelay = 0;
int noUnBind = 0;
char *suffix = "o=Ace Industry,c=us";
char *filter = "cn=*jones*";
char *nameFile = 0;
char *bindDN = "cn=Directory Manager";
char *bindPW = "unrestricted";
char **attrToReturn = 0;
char *attrList = 0;
Operation opType = op_search;
NameTable *ntable = NULL;
int sampleInterval = 10000;


void main(int argc, char** argv)
{
    int index = 0, numThreads, numDead = 0;
    int ch;
    SearchThread **threads;

    while ((ch = getopt(argc, argv, "j:i:h:s:f:p:t:D:w:n:A:bvlyqmcdu")) != EOF)
	switch (ch) {
	case 'h':
	    hostname = optarg;
	    break;
	case 's':
	    suffix = optarg;
	    break;
	case 'f':
	    filter = optarg;
	    break;
	case 'i':
	    nameFile = optarg;
	    break;
	case 'D':
	    bindDN = optarg;
	    break;
	case 'w':
	    bindPW = optarg;
	    break;
	case 'A':
	    attrList = optarg;
	    break;
	case 'p':
	    port = atoi(optarg);	
	    break;
	case 'b':			
	    doBind = 1;
	    break;
	case 'u':			
	    noUnBind = 1;
	    break;
	case 'n':			
	    numeric = atoi(optarg);
	    break;
	case 't':		
	    threadCount = atoi(optarg);
	    break;
	case 'j':		
	    sampleInterval = atoi(optarg) * 1000;
	    break;
	case 'v':
	    verbose = 1;
	    break;
	case 'q':
	    quiet = 1;
	    break;
	case 'l':
	    logging = 1;
	    break;
	case 'y':
	    noDelay = 1;
	    break;
	case 'm':
	    opType = op_modify;
	    break;
	case 'd':
	    opType = op_delete;
	    break;
	case 'c':
	    opType = op_compare;
	    break;
	case '?':
	    usage();
	    exit(1);
	    break;
	default:
	    break;
	}
    argc -= optind;
    argv += optind;

    PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 0);

    ntable = nt_new(0);
    if (nameFile) {
	if (!nt_load(ntable, nameFile)) {
	    printf("Failed to read name table\n");
	    exit(1);
	}
    }

    if (attrList)
	attrToReturn = string_to_list(attrList);

    /* a "vector" */
    threads = (SearchThread **)malloc(threadCount * sizeof(SearchThread *));

    while (threadCount--) {
	SearchThread *st;
	PRThread *thr;

	st = st_new();
	thr = PR_CreateThread(PR_SYSTEM_THREAD, search_start,
			      (void *)st, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
			      PR_JOINABLE_THREAD, 0);
	st_setThread(st, thr, index+1);
	threads[index++] = st;
    }
    numThreads = index;

    printf("rsearch: %d threads launched.\n\n", numThreads);

    while (numThreads != numDead) {
	int x;

	PR_Sleep(PR_MillisecondsToInterval(sampleInterval));

	/* now check for deadies */
	for (x = 0; x < numThreads; x++) {
	    if (!st_alive(threads[x])) {
		int y;
		PRThread *tid;

		printf("T%d DEAD.\n", st_getThread(threads[x], &tid));
		PR_JoinThread(tid);
		for (y = x+1; y < numThreads; y++)
		    threads[y-1] = threads[y];
		numThreads--;
		numDead++;
		x--;
	    }
	}

	/* print out stats */
	if (!quiet) {
	    PRUint32 total = 0;

	    for (x = 0; x < numThreads; x++) {
		PRUint32 count, min, max;

		st_getCountMinMax(threads[x], &count, &min, &max);
		total += count;
		printf("T%d min=%4ums, max=%4ums, count = %u\n",
		       st_getThread(threads[x], NULL), min, max, count);
	    }
	    if (numThreads > 1)
		printf("Average rate = %.2f\n", 
		       (double)total/(double)numThreads);
	}
	/* watchdogs were reset when we fetched the min/max counters */
    }

    printf("All threads died. (?)\n");
    exit(1);
}

		

