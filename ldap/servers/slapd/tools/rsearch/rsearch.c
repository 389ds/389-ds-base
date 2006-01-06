/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * XP port of dboreham's NT tool "repeated_search"
 * robey, march 1998
 */

#ifdef LINUX
#include <sys/param.h>
#include <sys/sysinfo.h>
#include <getopt.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#ifdef XP_UNIX
#include <unistd.h>
#endif
#include "nspr.h"
#include "rsearch.h"
#include "nametable.h"
#include "searchthread.h"
#include "ldap.h"

#define DEFAULT_HOSTNAME	"localhost"
#define DEFAULT_PORT		389
#define DEFAULT_THREADS		1
#define DEFAULT_INTERVAL	10000

void usage()
{
    printf("\nUsage: rsearch -D binddn -w bindpw -s suffix -f filter [options]\n"
	   "-\\?       -- print Usage (this message)\n"
	   "-H        -- print Usage (this message)\n"
	   "-h host   -- ldap server host  (default: %s)\n"
	   "-p port   -- ldap server port  (default: %d)\n"
	   "-S scope  -- search SCOPE [%d,%d,or %d]  (default: %d)\n"
	   "-b        -- bind before every operation\n"
	   "-u        -- don't unbind -- just close the connection\n"
	   "-L        -- set linger -- connection discarded when closed\n"
	   "-N        -- No operation -- just bind (ignore mdc)\n"
	   "-v        -- verbose\n"
	   "-y        -- nodelay\n"
	   "-q        -- quiet\n"
#ifndef NDSRK
	   "-l        -- logging\n"
#endif  /* NDSRK */
	   "-m        -- operaton: modify non-indexed attr (description). -B required\n"
	   "-M        -- operaton: modify indexed attr (telephonenumber). -B required\n"
	   "-d        -- operaton: delete. -B required\n"
	   "-c        -- operaton: compare. -B required\n"
	   "-i file   -- name file; used for the search filter\n"
	   "-B file   -- [DN and] UID file (use '-B \\?' to see the format)\n"
	   "-A attrs  -- list of attributes for search request\n"
	   "-a file   -- list of attributes for search request in a file\n" 
	   "          -- (use '-a \\?' to see the format ; -a & -A are mutually exclusive)\n"
	   "-n number -- (reserved for future use)\n"
	   "-j number -- sample interval, in seconds  (default: %u)\n"
	   "-t number -- threads  (default: %d)\n"
	   "-T number -- Time limit, in seconds; cmd stops when exceeds <number>\n"
	   "-V        -- show running average\n"
	   "-C num    -- take num samples, then stop\n"
	   "-R num    -- drop connection & reconnect every num searches\n"
	   "-x        -- Use -B file for binding; ignored if -B is not given\n"
	   "\n",
	   DEFAULT_HOSTNAME, DEFAULT_PORT,
	   LDAP_SCOPE_BASE, LDAP_SCOPE_ONELEVEL, LDAP_SCOPE_SUBTREE,
	   LDAP_SCOPE_SUBTREE,
	   (DEFAULT_INTERVAL/1000), DEFAULT_THREADS);
	exit(1);
}

void usage_B()
{
    printf("\nFormat of the file for the '-B <file>' option:\n"
	   "(Assuming each passwd is identical to its corresponding UID.)\n"
	   "\n"
	   "Format 1.\n"
	   "=========\n"
	   "UID: <uid>\n"
	   "...\n"
	   "\n"
	   "Format 2.\n"
	   "=========\n"
	   "DN: <dn>\n"
	   "UID: <uid>\n"
	   "...\n"
	   "\n");
}

void usage_A()
{
	
	printf("\nNote: -A and -a are mutually exclusive options\n");
    printf("\nFormat of the file for the '-a <file>' option:\n"
	   "\n"
	   "Format :\n"
	   "=========\n"
	   "<attr>\n"
	   "<attr>\n"
	   "...\n"
	   "\n");

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
char *hostname = DEFAULT_HOSTNAME;
int port = DEFAULT_PORT;
int numeric = 0;
int threadCount = DEFAULT_THREADS;
int verbose = 0;
int logging = 0;
int doBind = 0;
int cool = 0;
int quiet = 0;
int noDelay = 0;
int noUnBind = 0;
int noOp = 0;
int showRunningAvg = 0;
int countLimit = 0;
int reconnect = 0;
char *suffix = NULL;
char *filter = NULL;
char *nameFile = 0;
char *searchDatFile = 0;
char *attrFile = 0;
char *bindDN = NULL;
char *bindPW = NULL;
char **attrToReturn = 0;
char *attrList = 0;
Operation opType = op_search;
NameTable *ntable = NULL;
NameTable *attrTable = NULL;
SDatTable *sdattable = NULL;
int sampleInterval = DEFAULT_INTERVAL;
int timeLimit = 0;
int setLinger = 0;
int useBFile = 0;
int myScope = LDAP_SCOPE_SUBTREE;


int main(int argc, char** argv)
{
    int index = 0, numThreads, numDead = 0;
    int ch;
    int lifeTime;
    SearchThread **threads;
    PRUint32 total;
    double rate, val, cumrate;
    double sumVal;
    int counter;

    if (argc == 1) {
	usage();
	exit(1); 
    }

    while ((ch = getopt(argc, argv, 
			"B:a:j:i:h:s:f:p:t:T:D:w:n:A:S:C:R:bvlyqmMcduNLHx?V"))
	   != EOF)
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
	case 'B':
	    if (optarg[0] == '?') {
		usage_B();
		exit(1);
	    }
	    searchDatFile = optarg;
	    break;
	case 'D':
	    bindDN = optarg;
	    break;
	case 'w':
	    bindPW = optarg;
	    break;
	case 'A':
		if (!attrFile)
	    	attrList = optarg;
		else 
			usage();
	    break;
	case 'p':
	    port = atoi(optarg);	
	    break;
	case 'S':
	    myScope = atoi(optarg);
	    if (myScope < LDAP_SCOPE_BASE || myScope > LDAP_SCOPE_SUBTREE)
		myScope = LDAP_SCOPE_SUBTREE;
	    break;
	case 'C':
	    countLimit = atoi(optarg);
	    break;
	case 'b':			
	    doBind = 1;
	    break;
	case 'u':			
	    noUnBind = 1;
	    break;
	case 'L':			
	    setLinger = 1;
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
	case 'M':
	    opType = op_idxmodify;
	    break;
	case 'd':
	    opType = op_delete;
	    break;
	case 'c':
	    opType = op_compare;
	    break;
	case 'N':
	    noOp = 1;
	    doBind = 1;	/* no use w/o this */
	    break;
	case 'T':
	    timeLimit = atoi(optarg);
	    break;
	case 'V':
	    showRunningAvg = 1;
	    break;
	case 'R':
	    reconnect = atoi(optarg);
	    break;
	case 'x':
	    useBFile = 1;
	    break;
	case 'a':
	    if (optarg[0] == '?') {
		usage_A();
		exit(1);
	    }
		if (!attrList)
	    	attrFile = optarg;
		else
			usage();
		break;
	case '?':
	case 'H':
	default :
		usage();
	}

    if ( !suffix || !filter || !bindDN || !bindPW ) {
	printf("rsearch: missing option\n");
	usage();
    }

    if ( timeLimit < 0 || threadCount <= 0 || sampleInterval <= 0 ) {
	printf("rsearch: invalid option value\n");
	usage();
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

	attrTable = nt_new(0);
	if (attrFile) 
	{
		if (!nt_load(attrTable , attrFile)) 
		{
			printf ("Failed to read attr name table\n");
			exit(1);
		}
	}

    sdattable = sdt_new(0);
    if (searchDatFile) {
	if (!sdt_load(sdattable, searchDatFile)) {
	    printf("Failed to read search data table: %s\n", searchDatFile);
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

    lifeTime = 0;
    counter = 0;
    sumVal = 0;
    cumrate = 0.0;
    while (numThreads) {
	int x, alive;

	PR_Sleep(PR_MillisecondsToInterval(sampleInterval));

	counter++;
	lifeTime += sampleInterval/1000;
	/* now check for deadies */
	for (x = 0; x < numThreads; x++) {
	    alive = st_alive(threads[x]);
	    if (alive < 1) {
		int y;
		PRThread *tid;

		printf("T%d no heartbeat", st_getThread(threads[x], &tid));
		if (alive <= -4) {
		    printf(" -- Dead thread being reaped.\n");
		    PR_JoinThread(tid);
		    for (y = x+1; y < numThreads; y++)
			threads[y-1] = threads[y];
		    numThreads--;
		    numDead++;
		    x--;
		}
		else
		   printf(" (waiting)\n");
	    }
	}

	/* print out stats */
	total = 0;
	for (x = 0; x < numThreads; x++) {
	    PRUint32 count, min, max;

	    st_getCountMinMax(threads[x], &count, &min, &max);
	    total += count;
	    if (!quiet && verbose)
		printf("T%d min=%4ums, max=%4ums, count = %u\n",
			   st_getThread(threads[x], NULL), min, max, count);
	}
	rate = (double)total / (double)numThreads;
	val = 1000.0 * (double)total / (double)sampleInterval;
	cumrate += rate;
	if ((numThreads > 1) || (!verbose)) {
	    if (!quiet) {
		if (showRunningAvg)
		    printf("Rate: %7.2f/thr (cumul rate: %7.2f/thr)\n",
			   rate, cumrate/(double)counter);
		else
		    printf("Rate: %7.2f/thr (%6.2f/sec =%7.4fms/op), "
			   "total:%6u (%d thr)\n",
			   rate, val, (double)1000.0/val, total, numThreads);
	    }
	}
	if (countLimit && (counter >= countLimit)) {
	    printf("Thank you, and good night.\n");
	    exit(0);
	}
	if (timeLimit && (lifeTime >= timeLimit)) {
	    double tmpv;
	    if (verbose)
		printf("%d sec >= %d\n", lifeTime, timeLimit);
	    printf("Final Average rate: "
		            "%6.2f/sec = %6.4fmsec/op, total:%6u\n",
			    (tmpv = (val + sumVal)/counter),
			    (double)1000.0/tmpv,
			    total);
	    exit(0);
	}
	sumVal += val;
	/* watchdogs were reset when we fetched the min/max counters */
    }

    printf("All threads died. (?)\n");
    exit(1);
}
