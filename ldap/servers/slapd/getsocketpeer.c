/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(ENABLE_LDAPI)

#if defined(HAVE_GETPEERUCRED)
#include <ucred.h>
#endif

#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>

/* nspr secrets - we need to do an end run around nspr
   in order to do things it does not support
 */
#include <private/pprio.h>

#if !(defined(SO_PEERCRED) || defined(HAVE_PEERUCRED) || defined(HAVE_GETPEEREID))
# include <string.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <errno.h>
#endif

int
slapd_get_socket_peer(PRFileDesc *nspr_fd, uid_t *uid, gid_t *gid)
{
    int ret = -1;
    int fd = PR_FileDesc2NativeHandle(nspr_fd); /* naughty private func */

#if defined(SO_PEERCRED) /* linux */

    struct ucred creds;
    socklen_t len = sizeof(creds);

    if (0 == getsockopt(fd, SOL_SOCKET, SO_PEERCRED, (void *)&creds, &len)) {
        if (sizeof(creds) == len) {
            if (uid)
                *uid = creds.uid;
            if (gid)
                *gid = creds.gid;

            ret = 0;
        }
    }

#elif defined(HAVE_GETPEERUCRED) /* solaris10 */

    ucred_t *creds = 0;

    if (0 == getpeerucred(fd, &creds)) {
        if (uid) {
            *uid = ucred_getruid(creds);
            if (-1 != uid)
                ret = 0;
        }

        if (gid) {
            *gid = ucred_getrgid(creds);
            if (-1 == *gid)
                ret = -1;
            else
                ret = 0;
        }

        ucred_free(creds);
    }

#elif defined(HAVE_GETPEEREID) /* osx / some BSDs */

    if (0 == getpeereid(fd, &uid, &gid))
        ret = 0;

#else /* hpux / Solaris9 / some BSDs - file descriptor cooperative auth */
    struct msghdr msg = {0};
    struct iovec iov;
    char dummy[8];
    int pass_sd[2];
    int rc = 0;
    unsigned int retrycnt = 0xffffffff; /* safety net */
    int myerrno = 0;

    iov.iov_base = dummy;
    iov.iov_len = sizeof(dummy);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
#ifndef __FreeBSD__
    msg.msg_accrights = (caddr_t)&pass_sd;
    msg.msg_accrightslen = sizeof(pass_sd); /* Initialize it with 8 bytes.
                                               If recvmsg is successful,
                                               4 is supposed to be returned. */

#endif
    /*
       Since PR_SockOpt_Nonblocking is set to the socket,
       recvmsg returns immediately if no data is waiting to be received.
       If recvmsg returns an error and EGAIN (== EWOULDBLOCK) is set to errno,
       we should retry some time.
     */
    while ((rc = recvmsg(fd, &msg, MSG_PEEK)) < 0 && (EAGAIN == (myerrno = errno)) && retrycnt-- >= 0)
        ;

#ifdef __FreeBSD__
    if (rc >= 0)
#else
    if (rc >= 0 && msg.msg_accrightslen == sizeof(int))
#endif
    {
        struct stat st;

        ret = fstat(pass_sd[0], &st);

        if (0 == ret && S_ISFIFO(st.st_mode) &&
            0 == (st.st_mode & (S_IRWXG | S_IRWXO))) {
            if (uid)
                *uid = st.st_uid;

            if (gid)
                *gid = st.st_gid;
        } else {
            ret = -1;
        }
    }

#endif

    return ret;
}

#endif /* ENABLE_LDAPI */
