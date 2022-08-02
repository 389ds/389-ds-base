/*
 * Copyright (C) 1998 Netscape Communications Corporation.
 * All Rights Reserved.
 *
 * Copyright 2016 Red Hat, Inc. and/or its affiliates.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * systemd-ask-pass.c - SVRCORE module for reading the PIN from systemd integrations.
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif

/* For socket.h to work correct, we need to define __USE_GNU */
#define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <time.h>
#include <inttypes.h>
#include <svrcore.h>


#define PASS_MAX 256 * sizeof(char)
#define NSEC_PER_USEC ((uint64_t) 1000ULL)
#define USEC_PER_SEC ((uint64_t) 1000000ULL)

struct SVRCORESystemdPinObj
{
    SVRCOREPinObj base;
    uint64_t timeout;
};

static void destroyObject(SVRCOREPinObj *obj);
static char *getPin(SVRCOREPinObj *obj, const char *tokenName, PRBool retry);

static const SVRCOREPinMethods vtable = { 0, 0, destroyObject, getPin };


SVRCOREError
SVRCORE_CreateSystemdPinObj(SVRCORESystemdPinObj **out, uint64_t timeout)
{
#ifdef WITH_SYSTEMD

    SVRCOREError err = SVRCORE_Success;
    SVRCORESystemdPinObj *obj = NULL;

    do { // This is used like a "with" statement, to avoid a goto.
        obj = (SVRCORESystemdPinObj *)malloc(sizeof(SVRCORESystemdPinObj));
        if (obj == NULL) {
            err = SVRCORE_NoMemory_Error;
            break;
        }

        obj->base.methods = &vtable;
        if (timeout == 0) {
            obj->timeout = 90;
        } else {
            obj->timeout = timeout;
        }

    } while (0);

    // If error, destrop it, and return err
    if (err != SVRCORE_Success) {
        SVRCORE_DestroySystemdPinObj(obj);
        obj = NULL;
    }

    *out = obj;
    return err;
#else // systemd
    return SVRCORE_MissingFeature;
#endif // Systemd
}

#ifdef WITH_SYSTEMD
SVRCOREError
_create_socket(char **path, int *sfd)
{
    SVRCOREError err = SVRCORE_Success;
    *sfd = 0;

    int one = 1;

    struct sockaddr_un saddr = { AF_UNIX, {0} };
    // This is the max len of the path
    strncpy(saddr.sun_path, *path, 50);

    // Create the socket

    *sfd = socket(AF_UNIX, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
    if (*sfd < 0) {
        err = SVRCORE_SocketError;
        goto out;
    }

    // bind the socket to the addr
    if (bind(*sfd, (const struct sockaddr *) &saddr, sizeof(saddr) ) != 0 ) {
        // EACCES == 13
        if (errno == EACCES) {
            err = SVRCORE_PermissionError;
        } else {
            err = SVRCORE_SocketError;
        }
        goto out;
    }

    // set options. Why do we need SO_PASSCRED? I think this makes systemd happy
    if (setsockopt(*sfd, SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)) != 0) {
        err = SVRCORE_SocketError;
        goto out;
    }

out:

    return err;
}

SVRCOREError
_now(uint64_t *now)
{
    SVRCOREError err = SVRCORE_Success;
    struct timespec ts;
    // Need to set this from clock_monotonic + timeout
    // Of course, systemd invent their own thing, and no docs about until
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        err = SVRCORE_ClockError;
        goto out;
    }
out:
    *now = ((uint64_t)ts.tv_sec * USEC_PER_SEC) + \
             ((uint64_t)ts.tv_nsec / NSEC_PER_USEC);
    return err;
}

SVRCOREError
_until(uint64_t timeout, uint64_t *until)
{
    SVRCOREError err = SVRCORE_Success;
    // Get the timestamp
    err = _now(until);
    if (err != SVRCORE_Success) {
        goto out;
    }
    *until = *until + (timeout * USEC_PER_SEC);

out:
    return err;
}
#endif // Systemd

static char *
getPin(SVRCOREPinObj *obj, const char *tokenName, PRBool retry)
{
#ifdef WITH_SYSTEMD
    SVRCORESystemdPinObj *sobj = (SVRCORESystemdPinObj *)obj;
    SVRCOREError err = SVRCORE_Success;
    char *tbuf = malloc(PASS_MAX);
    char *token = malloc(PASS_MAX);
    char *path = "/run/systemd/ask-password";
    int socket_fd = 0;
    FILE *tmp_fd = NULL;
    uint64_t until = 0;
    uint64_t now = 0;
    // Now make up the paths we will use.
    char *socket_path = NULL;
    char *ask_path = NULL;
    char *tmp_path = NULL;

    if (token == NULL || tbuf == NULL) {
        err = SVRCORE_NoMemory_Error;
        goto out;
    }

    pid_t pid = getpid();

    socket_path = malloc(sizeof(char) * 50);
    ask_path = malloc(sizeof(char) * 50);
    tmp_path = malloc(sizeof(char) * 50);

    if (socket_path == NULL || ask_path == NULL || tmp_path == NULL) {
        err = SVRCORE_NoMemory_Error;
        if (socket_path) {
            *socket_path = '\0';
        }
        if (ask_path) {
            *ask_path = '\0';
        }
        if (tmp_path) {
            *tmp_path = '\0';
        }
        goto out;
    }

    snprintf(socket_path, 50, "%s/sck.%d", path, pid );
    snprintf(ask_path, 50, "%s/ask.%d", path, pid );
    snprintf(tmp_path, 50, "%s/tmp.%d", path, pid );

#ifdef DEBUG
    printf("systemd:getPin() -> get time until \n");
#endif

    err = _until(sobj->timeout, &until);
    if(err != SVRCORE_Success) {
        free(token);
        token = NULL;
        goto out;
    }

#ifdef DEBUG
    printf("systemd:getPin() -> time until %" PRId64 "\n", until);
#endif

#ifdef DEBUG
    printf("systemd:getPin() -> begin ask pid %d\n", pid);
#endif

    // Are there any other pre-conditions we should check?
    //    mkdir -p "/run/systemd/ask-password", 0755
    if (mkdir(path, 0755) != 0) {
        if (errno != EEXIST) {
            err = SVRCORE_IOOperationError;
            free(token);
            token = NULL;
            goto out;
        }
    }

#ifdef DEBUG
    printf("systemd:getPin() -> path exists\n");
#endif

    // Create the socket
    //  The socket has to end up as /run/system/ask-password/sck.xxxxx
#ifdef DEBUG
    printf("systemd:getPin() -> creating socket %s \n", socket_path);
#endif

    err = _create_socket(&socket_path, &socket_fd);
    if (err != SVRCORE_Success) {
        fprintf(stderr, "SVRCORE systemd:getPin() -> creating socket FAILED %d\n", err);
        free(token);
        token = NULL;
        goto out;
    }

#ifdef DEBUG
    printf("systemd:getPin() -> creating tmp file %s \n", tmp_path);
#endif


    umask( S_IWGRP | S_IWOTH );
    tmp_fd = fopen(tmp_path, "w");

    if (tmp_fd == NULL) {
        fprintf(stderr, "SVRCORE systemd:getPin() -> opening ask file FAILED\n");
        err = SVRCORE_IOOperationError;
        free(token);
        token = NULL;
        goto out;
    }

    // Create the inf file asking for the password
    //    Write data to the file
    //    [Ask]
    fprintf(tmp_fd, "[Ask]\n");
    //    PID=Our Pid
    fprintf(tmp_fd, "PID=%d\n", pid);
    //    Socket=fd of socket, or name? systemd code doesn't make this clear.
    fprintf(tmp_fd, "Socket=%s\n", socket_path);
    //    AcceptCached=0 or 1, but not docs on which means what ....
    fprintf(tmp_fd, "AcceptCached=0\n");
    //    Echo= Display password as entered or not
    fprintf(tmp_fd, "Echo=0\n");
    //    NotAfter= Number of microseconds from clock monotonic + timeout
    fprintf(tmp_fd, "NotAfter=%" PRIu64 "\n", until);
    //    Message=Prompt to display
    fprintf(tmp_fd, "Message=Enter PIN for %s:\n", tokenName);
    //    Id=Who wants it
    // fprintf(tmp_fd, "Id=svrcore\n");
    //    Icon?
    fclose(tmp_fd);

    // rename the file to .ask ??
    //  -rw-r--r--.  1 root root 127 Mar 22 13:08 ask.9m8ftM
    //  srw-------.  1 root root   0 Mar 22 13:08 sck.cf913cf669031308

#ifdef DEBUG
    printf("systemd:getPin() -> moving tmp file %s to %s\n", tmp_path, ask_path);
#endif


    if (rename(tmp_path, ask_path) != 0) {
        fprintf(stderr, "SVRCORE systemd:getPin() -> renaming ask file FAILED %d\n", err);
        err = SVRCORE_IOOperationError;
        free(token);
        token = NULL;
        goto out;
    }

    // read on the socket, if nothing, keep looping and check timeout.
    while (PR_TRUE) {
        struct msghdr msghdr;
        struct iovec iovec;

        struct ucred *ucred;
        union {
            struct cmsghdr cmsghdr;
            uint8_t buf[CMSG_SPACE(sizeof(struct ucred))];
        } control;

        ssize_t data_size;

        err = _now(&now);
        if (err != SVRCORE_Success) {
            free(token);
            token = NULL;
            goto out;
        }

        if (now >= until) {
            err = SVRCORE_TimeoutError;
            free(token);
            token = NULL;
            goto out;
        }

        // Clear out last loops data
        memset(&msghdr, 0, sizeof(struct msghdr));
        memset(&iovec, 0, sizeof(struct iovec));
        memset(&control, 0, sizeof(control));

        // Setup the structures to recieve data.
        iovec.iov_base = tbuf;
        iovec.iov_len = PASS_MAX;

        msghdr.msg_iov = &iovec;
        msghdr.msg_iovlen = 1;
        msghdr.msg_control = &control;
        msghdr.msg_controllen = sizeof(control);

        data_size = recvmsg(socket_fd, &msghdr, 0);
        // Check if data_size is 0, then check errno
        if (data_size < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                err = SVRCORE_SocketError;
                free(token);
                token = NULL;
                goto out;
            }
        }
#ifdef DEBUG
        printf("systemd:getPin() -> receiving ... %ld %d\n", data_size, errno);
#endif
        // Check the response is valid
        // Check that the other end is authenticated.
        if (msghdr.msg_controllen < CMSG_LEN(sizeof(struct ucred)) ||
            control.cmsghdr.cmsg_len != CMSG_LEN(sizeof(struct ucred)) ||
            control.cmsghdr.cmsg_level != SOL_SOCKET ||
            control.cmsghdr.cmsg_type != SCM_CREDENTIALS)
        {
            // Ignore this message, it has no auth on the socket
#ifdef DEBUG
            printf("systemd:getPin() -> Unauthenticated message \n");
#endif
            sleep(2);
            continue;
        }

        ucred = (struct ucred *) CMSG_DATA(&control.cmsghdr);
        if (ucred->uid != 0) {
#ifdef DEBUG
            printf("systemd:getPin() -> Response not by root \n");
#endif
            sleep(2);
            continue;
        }

#ifdef DEBUG
        printf("systemd:getPin() -> token value %s \n", tbuf);
#endif

        // The response starts with a + to say the value was a success
        if (tbuf[0] == '+') {
            if (data_size == 1) {
                strncpy(token, "", PASS_MAX - 1);
            } else {
                strncpy(token, tbuf + 1, PASS_MAX - 1);
            }
            break;

        }

        // Else, if a -, the input was canceled
        if  (tbuf[0] == '-') {
            err = SVRCORE_NoSuchToken_Error;
            free(token);
            token = NULL;
            goto out;
        }

    }
out:

    if(tbuf != NULL) {
        memset(tbuf, 0, PASS_MAX);
        free(tbuf);
    }

    if (socket_fd != 0) {
        close(socket_fd);
    }

    if (socket_path) {
        if (*socket_path) {
            unlink(socket_path);
        }
        free(socket_path);
    }
    if (ask_path) {
        if (*ask_path) {
            unlink(ask_path);
        }
        free(ask_path);
    }
    if (tmp_path) {
        if (*tmp_path) {
            unlink(tmp_path);
        }
        free(tmp_path);
    }

    return token;

#else // systemd
    return NULL;
#endif // Systemd
}

void
SVRCORE_DestroySystemdPinObj(SVRCORESystemdPinObj *obj)
{
#ifdef WITH_SYSTEMD
    if (obj) {
        free(obj);
    }
#endif // Systemd
}

static void
destroyObject(SVRCOREPinObj *obj)
{
    SVRCORE_DestroySystemdPinObj((SVRCORESystemdPinObj*)obj);
}
