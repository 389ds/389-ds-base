/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * XP port of dboreham's NT tool "infinite_add"
 * robey, june 1998
 *
 * note: i didn't really port this one, i just wrote a quick version
 * from scratch.
 */

#ifdef LINUX
#include <sys/param.h>
#include <sys/sysinfo.h>
#include <getopt.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "nspr.h"
#include "nametable.h"
#include "addthread.h"

#define DEFAULT_HOSTNAME	"localhost"
#define DEFAULT_PORT		389
#define DEFAULT_THREADS		1
#define DEFAULT_INTERVAL	10000


/* global data for the threads to share */
char *hostname = DEFAULT_HOSTNAME;
PRUint16 port = DEFAULT_PORT;
int thread_count = DEFAULT_THREADS;
char *suffix = NULL;
char *username = NULL;
char *password = NULL;
PRUint32 blobsize = 0;
PRUint32 sampleInterval = DEFAULT_INTERVAL;
int noDelay = 0;
int quiet = 0;
int verbose = 0;
int saveQuit = 0;
int lmtCount = 0;
unsigned long firstUID = 0;
NameTable *given_names = NULL, *family_names = NULL;


void usage()
{
    fprintf(stdout,
	   "Usage: infadd -s suffix -u bindDN -w password [options]\n"
	   "\nOptions:\n"
	   "-h hostname (default: %s)\n"
	   "-p port     (default: %d)\n"
	   "-t threads  -- number of threads to spin (default: %d)\n"
	   "-d          -- use TCP no-delay\n"
	   "-q          -- quiet mode (no status updates)\n"
	   "-v          -- verbose mode (give per-thread statistics)\n"
	   "-I num      -- first uid (default: 0)\n"
	   "-l count    -- limit count; stops when the total count exceeds <count>\n"
	   "-i msec     -- sample interval in milliseconds (default: %u)\n"
	   "-R size     -- generate <size> random names instead of using\n"
	   "               data files\n"
	   "-z size     -- add binary blob of average size of <size> bytes\n"
	   "\n",
	   DEFAULT_HOSTNAME, DEFAULT_PORT, DEFAULT_THREADS,
	   DEFAULT_INTERVAL);
}

/* generate a random name
 * this generates 'names' like "Gxuvbnrc" but hey, there are Bosnian towns
 * with less pronouncable names...
 */
char *randName(void)
{
    char *x;
    int i, len = (rand() % 7) + 5;

    x = (char *)malloc(len+1);
    if (!x) return NULL;
    x[0] = (rand() % 26)+'A';
    for (i = 1; i < len; i++) x[i] = (rand() % 26)+'a';
    x[len] = 0;
    return x;
}

int fill_table(NameTable *nt, PRUint32 size)
{
    PRUint32 i;
    char *x;
    int ret;

    fprintf(stdout, "Generating random names: 0      ");
    for (i = 0; i < size; i++) {
	x = randName();
	/* check for duplicates */
	while (nt_cis_check(nt, x)) {
	    free(x);
	    x = randName();
	}
	ret = nt_push(nt, x);
	if ((i % 100) == 0) {
	    fprintf(stdout, "\b\b\b\b\b\b\b%-7d", i);
	}
    }
    fprintf(stdout, "\b\b\b\b\b\b\b%d.  Done.\n", size);
    return ret;
}

int main(int argc, char **argv)
{
    int ch, index, numThreads, numDead;
    PRUint32 use_random = 0;
    AddThread **threads;
    PRUint32 total = 0, ntotal = 0;
    int counter;
    char familynames[35], givennames[35];

    srand(time(NULL));
    if (argc < 2) {
        usage();
        exit(1);
    }

    while ((ch = getopt(argc, argv, "h:p:s:u:w:z:dR:t:i:I:l:qvS")) != EOF)
        switch (ch) {
        case 'h':
            hostname = optarg;
            break;
        case 'p':
            port = (PRUint16)atoi(optarg);
            break;
        case 's':
            suffix = optarg;
            break;
        case 'u':
            username = optarg;
            break;
        case 'w':
            password = optarg;
            break;
        case 'z':
            blobsize = atoi(optarg);
            break;
        case 'R':
            use_random = (PRUint32)atol(optarg);
            break;
        case 't':
            thread_count = atoi(optarg);
            break;
        case 'i':
            sampleInterval = (PRUint32)atol(optarg);
            break;
        case 'd':
            noDelay = 1;
            break;
        case 'q':
            quiet = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'S':
            saveQuit = 1;
            break;
        case 'l':
            lmtCount = atoi(optarg);
            break;
        case 'I':
            firstUID = atoi(optarg);
            break;
        default:
            usage();
            exit(1);
        }

    if (!suffix || !username || !password) {
        printf("infadd: missing option\n");
        usage();
        exit(1);
    }

    if (use_random < 0 || sampleInterval <= 0 || thread_count <= 0 ||
                lmtCount < 0 || blobsize < 0 || firstUID < 0) {
        printf("infadd: invalid option value\n");
        usage();
        exit(-1);
    }

    argc -= optind;
    argv += optind;

    PR_Init(PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 0);

    given_names = nt_new(0);
    family_names = nt_new(0);
    if (use_random) {
        fill_table(given_names, use_random);
        fill_table(family_names, use_random);
    }
    else {
        if (!access("../data/dbgen-FamilyNames", R_OK)) {
            strcpy(familynames, "../data/dbgen-FamilyNames");
            strcpy(givennames, "../data/dbgen-GivenNames");
        }
        else  {
            strcpy(familynames, "../../data/dbgen-FamilyNames");
            strcpy(givennames, "../../data/dbgen-GivenNames");
        }
        fprintf(stdout, "Loading Given-Names ...\n");
        if (!nt_load(given_names, givennames)) {
            fprintf(stdout, "*** Failed to read name table\n");
            exit(1);
        }

        fprintf(stdout, "Loading Family-Names ...\n");
        if (!nt_load(family_names, familynames)) {
            fprintf(stdout, "*** Failed to read name table\n");
            exit(1);
        }
    }

    if (saveQuit) {
        fprintf(stdout, "Saving Given-Names ...\n");
        nt_save(given_names, givennames);
        fprintf(stdout, "Saving Family-Names ...\n");
        nt_save(family_names, familynames);
        exit(0);
    }

    if (firstUID) {
        at_initID(firstUID);
    }

    /* start up threads */
    threads = (AddThread **)malloc(thread_count * sizeof(AddThread *));

    index = 0;
    while (thread_count--) {
        AddThread *at;
        PRThread *thr;

        at = at_new();
        thr = PR_CreateThread(PR_SYSTEM_THREAD, infadd_start,
                              (void *)at, PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                              PR_JOINABLE_THREAD, 0);
        at_setThread(at, thr, index+1);
        threads[index++] = at;
    }
    numThreads = index;

    fprintf(stdout, "infadd: %d thread%s launched.\n\n",
                    numThreads, numThreads == 1 ? "" : "s");

    numDead = 0;
    counter = 0;
    while (numThreads) {
        int x, alive;
        double tmpv;

        PR_Sleep(PR_MillisecondsToInterval(sampleInterval));

        counter++;

        /* now check for deadies */
        for (x = 0; x < numThreads; x++) {
            alive = at_alive(threads[x]);
            if (alive < 1) {
                int y;
                PRThread *tid;

                fprintf(stdout, "T%d DEAD", at_getThread(threads[x], &tid));
                if (alive <= -4) {
                    fprintf(stdout, " -- Dead thread being reaped.\n");
                    PR_JoinThread(tid);
                    for (y = x+1; y < numThreads; y++)
                        threads[y-1] = threads[y];
                    numThreads--;
                    numDead++;
                    x--;
                }
                else
                    fprintf(stdout, " (waiting)\n");
            }
        }

        /* check the total count */
        ntotal = 0;
        total = 0;
        for (x = 0; x < numThreads; x++) {
            PRUint32 count, min, max, ntot;

            at_getCountMinMax(threads[x], &count, &min, &max, &ntot);
            total += count;
            ntotal += ntot;
            if (!quiet && verbose)
                fprintf(stdout,
                        "T%d min:%5ums, max:%5ums, count: %3u, total: %u\n",
                        at_getThread(threads[x], NULL), min, max, count,
                        ntot);
        }
        if (!quiet && (numThreads > 1 || !verbose)) {
            fprintf(stdout, "Average rate:%7.2f, total: %u\n", 
                       (double)total/(double)numThreads, ntotal);
        }
        if (lmtCount && ntotal >= lmtCount) {
            if (!quiet) {
                fprintf(stdout,
                  "Total added records: %d, Average rate: %7.2f/thrd, "
                  "%6.2f/sec = %6.4fmsec/op\n",
                  ntotal, (double)ntotal/(double)numThreads,
                  (tmpv = (double)ntotal*1000.0/(counter*sampleInterval)),
                  (double)1000.0/tmpv);
            }
            exit(1);
        }
        /* watchdogs were reset when we fetched the min/max counters */
    }

    fprintf(stdout, "All threads died. :(\n");
    exit(1);
}
