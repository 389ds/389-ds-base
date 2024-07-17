/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2006 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif


#include <stdio.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include "remote.h"
#include "lber.h"
#include "ldap.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


enum
{
    ADD,
    DELETE,
    MODRDN,
    MODIFY,
    RESULT
};

typedef struct
{
    int conn, op, type;
    char *dn;
} Optype;

Optype *pendops;
int npend, maxop, connected;
char *ldap_ops[] = {"ADD", "DEL", "MODRDN", "MOD ", "RESULT", NULL};
int ldap_val[] = {LDAP_REQ_ADD, LDAP_REQ_DELETE, LDAP_REQ_MODRDN, LDAP_REQ_MODIFY};

get_op_par(char *s, Optype *op)
{
    char *t;
    int i;

    t = strstr(s, "conn=");
    for (t += 5, op->conn = 0; isdigit(*t); t++)
        op->conn = op->conn * 10 + *t - '0';
    t = strstr(s, "op=");
    for (t += 3, op->op = 0; isdigit(*t); t++)
        op->op = op->op * 10 + *t - '0';
    if (t = strstr(s, "dn="))
        op->dn = strdup(t + 3);
}

send_op(char *s, int sfd)
{
    int sz, i;
    Optype tmp;
    repconfirm *result;
    char *t;

    get_op_par(s, &tmp);
    for (i = 0; i < maxop; i++)
        if (pendops[i].op == tmp.op && pendops[i].conn == tmp.conn) {
            t = strstr(s, "err=");
            sz = strlen(pendops[i].dn);
            result = (repconfirm *)safe_malloc(sizeof(repconfirm) + sz);
            for (t += 4, result->res = 0; isdigit(*t); t++)
                result->res = result->res * 10 + *t - '0';
            result->type = htonl(ldap_val[pendops[i].type]);
            strcpy(result->dn, pendops[i].dn);
            result->dnSize = htonl(sz);
            result->res = htonl(result->res);
            if (write(sfd, result, sizeof(repconfirm) + sz) <= 0) {
                close(sfd);
                memset(pendops, 0, maxop * sizeof(Optype));
                maxop = npend = connected = 0;
                return;
            }
            if (i != maxop)
                pendops[i] = pendops[maxop];
            else
                memset(pendops + i, 0, sizeof(Optype));
            return;
        }
}

main(int argc, char **argv)
{
    struct sockaddr_in srvsaddr;
    static char logline[512];
    char **tmp;
    char *p;
    struct addrinfo hints = {0};
    struct addrinfo *info = NULL;
    int gai_result = 0;
    int i, port = 16000;
    int sockfd;

    while ((i = getopt(argc, argv, "p:")) != EOF) {
        switch (i) {
        case 'p':
            port = atoi(optarg);
            break;
        }
    }

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    if ((gai_result = getaddrinfo(argv[optind], NULL, &hints, &info)) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "ldclt",
                "getaddrinfo: %s\n", gai_strerror(gai_result));
        return NULL;
    }

    srvsaddr.sin_addr.s_addr = htonl(*((u_long *)(info->ai_addr)));
    srvsaddr.sin_family = AF_INET;
    srvsaddr.sin_port = htons(port);
    freeaddrinfo(info);
    maxop = npend = 0;
    pendops = (Optype *)safe_malloc(sizeof(Optype) * 20);
    sigset(SIGPIPE, SIG_IGN);
    while (fgets(logline, sizeof(logline), stdin)) {
        if ((p = strchr(logline, '\n'))) {
            *p = 0;
        }
        if (!connected) {
            if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                perror(argv[0]);
                exit(1);
            }
            i = 1;
            if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &i, sizeof(int)) != 0)
                perror("Nagle");
            if (connect(sockfd, (struct sockaddr *)&srvsaddr, sizeof(struct sockaddr)) != -1)
                connected = 1;
            else {
                close(sockfd);
                continue;
            }
        }
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
            send_op(logline, sockfd);
    }
    close(sockfd);
}
