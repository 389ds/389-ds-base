#ident "@(#)repworker.c    1.15 99/06/09"

/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


/*
        FILE :        repworker.c
        AUTHOR :      Fabio Pistolesi
        VERSION :     1.0
        DATE :        05 May 1999
        DESCRIPTION :
            This file contains the implementation of the worker part
            of ldclt tool. This worker is intended to scan the logs
            of the ldap server and to communicate the operations
            logged to the supplier ldclt, to be checked against the
            memorized operations performed on the supplier ldap
            server.
 LOCAL :        None.
        HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author    | Comments
---------+--------------+------------------------------------------------------
05/05/99 | F. Pistolesi    | Creation
---------+--------------+------------------------------------------------------
06/05/99 | JL Schwing    | 1.2 : Port on Solaris 2.5.1
---------+--------------+------------------------------------------------------
10/05/99 | F. Pistolesi    | Added multiple filtered servers to send results to.
---------+--------------+------------------------------------------------------
18/05/99 | JL Schwing    | 1.8 : Port on 2.5.1
---------+--------------+------------------------------------------------------
26/05/99 | F. Pistolesi    | 1.10: Bug fix - missing free()
---------+--------------+------------------------------------------------------
27/05/99 | F. Pistolesi    | 1.11: Add new option -d (debug)
---------+--------------+------------------------------------------------------
29/05/99 | F. Pistolesi    | 1.12: Add new option -t (log)
---------+--------------+------------------------------------------------------
09/06/99 | JL Schwing    | 1.14: Bug fix - crash in send_op() if tmp.dn==NULL
---------+--------------+------------------------------------------------------
09/06/99 | F. Pistolesi    | 1.15: Fix the fix above.
---------+--------------+------------------------------------------------------
*/

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <libgen.h>
#if OS_RELEASE == 551
#include <re_comp.h>
#endif
#include "remote.h"
#include "lber.h"
#include "ldap.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/*
 * Enumeration for internal list
 */
enum
{
    ADD,
    DELETE,
    MODRDN,
    MODIFY,
    RESULT,
    LAST
};

/*
 * internal list
 */
typedef struct
{
    int conn, op, type;
    char *dn;
} Optype;

typedef struct
{
    int fd;
    char *filter, *hname;
    struct sockaddr_in addr;
} Towho;

Optype *pendops;
Towho *srvlist;
int npend, maxop, connected, nsrv, debug;
char *ldap_ops[] = {"ADD", "DEL", "MODRDN", "MOD ", "RESULT", "NONE", NULL};
/*
 * To map internal values to LDAP_REQ
 */
int ldap_val[] = {LDAP_REQ_ADD, LDAP_REQ_DELETE, LDAP_REQ_MODRDN, LDAP_REQ_MODIFY};

get_op_par(char *s, Optype *op)
{
    char *t;
    int i;

    /*
  * Provided they do not change dsservd's log format, this should work
  * Magic numbers are the length of the lookup string
  */
    t = strstr(s, "conn=");
    for (t += 5, op->conn = 0; isdigit(*t); t++)
        op->conn = op->conn * 10 + *t - '0';
    t = strstr(s, "op=");
    for (t += 3, op->op = 0; isdigit(*t); t++)
        op->op = op->op * 10 + *t - '0';
    if (t = strstr(s, "dn="))
        op->dn = strdup(t + 3);
}

open_cnx(struct sockaddr *srv)
{
    int i, sockfd;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Worker");
        exit(1);
    }
    i = 1;
    /*
  * Disable TCP's Nagle algorithm
  */
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&i, sizeof(int)) != 0)
        perror("Nagle");
    if (connect(sockfd, srv, sizeof(struct sockaddr)) != -1)
        return sockfd;
    else
        close(sockfd);
    return -1;
}

send_op(char *s)
{
    int sz, i, j, ret;
    Optype tmp;
    repconfirm *result;
    char *t;
    struct pollfd pfd;

    get_op_par(s, &tmp);
    for (i = 0; i < maxop; i++)
        /*
     * got a RESULT string. Try to match with known operations.
     */
        if (pendops[i].op == tmp.op && pendops[i].conn == tmp.conn) {
            sz = strlen(pendops[i].dn);
            result = (repconfirm *)malloc(sizeof(repconfirm) + sz);
            t = strstr(s, "err=");
            for (t += 4, result->res = 0; isdigit(*t); t++)
                result->res = result->res * 10 + *t - '0';
            /*
         * Build packet
         */
            result->type = htonl(ldap_val[pendops[i].type]);
            strcpy(result->dn, pendops[i].dn + 1);
            sz -= 2;
            result->dn[sz] = '\0';
            result->dnSize = htonl(sz);
            if (debug)
                printf("Sending %d %d %s\n", ntohl(result->type), result->res, result->dn);
            result->res = htonl(result->res);
            /*
         * find which filter applies. Note that if no filter applies, no
         * packets are sent
         */
            for (j = 0; j < nsrv; j++) {
                /*
             * Suppose a NULL filter means everything
             */
                if (srvlist[j].filter)
                    if (regex(srvlist[j].filter, result->dn) == NULL)
                        continue;
                /*
             * try to write. This works if server set SO_LINGER option
             * with parameters l_onoff=1,l_linger=0. This means terminate
             * the connection sending an RST instead of FIN, so that
             * write() will fail on first attempt instead of second.
             */
                if (write(srvlist[j].fd, result, sizeof(repconfirm) + sz) <= 0) {
                    /*
                 * socket was closed by peer. try again
                 */
                    close(srvlist[j].fd);
                    if ((srvlist[j].fd = connected = open_cnx((struct sockaddr *)&srvlist[j].addr)) == -1)
                        /*
                     * OK, server disconnected for good
                     */
                        continue;
                    if ((ret = write(srvlist[j].fd, result, sizeof(repconfirm) + sz)) <= 0)
                        puts("Porc!");
                }
            }
            /*
         * Copy over the operation at the end
         */
            free(pendops[i].dn);
            maxop--;
            pendops[i] = pendops[maxop];
            free(result);
            break;
        }
    if (tmp.dn != NULL)
        free(tmp.dn);
}

main(int argc, char **argv)
{
    int i, port = 16000;
    int sockfd, log = 0;
    static char logline[512];
    char **tmp, *hn, *hp, *hf;
    struct addrinfo hints = {0};
    struct addrinfo *info = NULL;

    while ((i = getopt(argc, argv, "tdP:s:")) != EOF) {
        switch (i) {
        case 't':
            log = 1;
            break;
        case 'd':
            debug = 1;
            break;
        case 'P':
            port = atoi(optarg);
            break;
        case 's':
            /*
             * pointers to hostname, host port, filter
             */
            hn = strtok(optarg, ",");
            hp = strtok(NULL, ",");
            hf = strtok(NULL, ",");
            if (hf == NULL && hp)
                if (*hp < '0' || *hp > '9')
                    hf = hp;
            if (hn == NULL || hf == NULL) {
                puts("Missing parameter\n");
                break;
            }
            /*
             * Get supplier address, just the first.
             */

            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_CANONNAME;
            if (getaddrinfo(hn, NULL, &hints, &info) != 0) {
                printf("Unknown host %s\n", hn);
                break;
            }

            srvlist = (Towho *)realloc(srvlist, (nsrv + 1) * sizeof(Towho));
            srvlist[nsrv].addr.sin_addr.s_addr = htonl(*((u_long *)(info->ai_addr)));
            srvlist[nsrv].addr.sin_family = AF_INET;
            srvlist[nsrv].addr.sin_port = htonl((hp == hf ? port : atoi(hp)));
            if ((srvlist[nsrv].filter = regcmp(hf, NULL)) == NULL)
                printf("Wrong REX: %s\n", hf);
            srvlist[nsrv].fd = open_cnx((struct sockaddr *)&srvlist[nsrv].addr);
            srvlist[nsrv].hname = strdup(hn);
            nsrv++;
            freeaddrinfo(info);
            break;
        }
    }
    if (!nsrv) {
        if (!argv[optind]) {
            printf("Usage: %s [-td] -P port <hostname>\n\tor %s [-td] -s <host>,[<port>,]<REGEX>\n", argv[0], argv[0]);
            printf("\t-t\tprints input on stdout.\n\t-d\tdebug mode.\n");
            exit(1);
        }
        if (getaddrinfo(argv[optind], NULL, &hints, &info) != 0) {
            printf("Unknown host %s\n", hn);
            exit(1);
        }
        srvlist = (Towho *)malloc(sizeof(Towho));
        srvlist[nsrv].addr.sin_addr.s_addr = htonl(*((u_long *)(info->ai_addr)));
        srvlist[nsrv].addr.sin_family = AF_INET;
        srvlist[nsrv].addr.sin_port = htons(port);
        srvlist[nsrv].filter = NULL;
        srvlist[nsrv].fd = open_cnx((struct sockaddr *)&srvlist[nsrv].addr);
        srvlist[nsrv].hname = strdup(argv[optind]);
        nsrv++;
        freeaddrinfo(info);
    }
    maxop = npend = 0;
    pendops = (Optype *)malloc(sizeof(Optype) * 20);
    /*
  * Ignore SIGPIPE during write()
  */
    sigset(SIGPIPE, SIG_IGN);
    while (fgets(logline, sizeof(logline), stdin)) {
        if (p = strchr(logline, '\n')) {
            *p = 0;
        }
        if (log)
            puts(logline);
        for (tmp = ldap_ops, i = 0; tmp[i]; i++)
            if (strstr(logline, tmp[i]))
                break;
        if (i < RESULT) {
            get_op_par(logline, &pendops[maxop]);
            pendops[maxop].type = i;
            if (++maxop > npend)
                npend = maxop;
            if (!(npend % 20)) {
                pendops = (Optype *)realloc(pendops, sizeof(Optype) * (npend + 20));
                memset(pendops + npend, 0, sizeof(Optype) * 20);
            }
        }
        if (i == RESULT)
            send_op(logline);
    }
}


/* End of file */
