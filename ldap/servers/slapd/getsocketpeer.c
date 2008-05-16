/** BEGIN COPYRIGHT BLOCK
 * This Program is free software; you can redistribute it and/or modify it under * the terms of the GNU General Public License as published by the Free Software * Foundation; version 2 of the License.
 *
 * This Program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * In addition, as a special exception, Red Hat, Inc. gives You the additional
 * right to link the code of this Program with code not covered under the GNU
 * General Public License ("Non-GPL Code") and to distribute linked combinations * including the two, subject to the limitations in this paragraph. Non-GPL Code * permitted under this exception must only link to the code of this Program
 * through those well defined interfaces identified in the file named EXCEPTION
 * found in the source code files (the "Approved Interfaces"). The files of
 * Non-GPL Code may instantiate templates or use macros or inline functions from * the Approved Interfaces without causing the resulting work to be covered by
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
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifdef HAVE_CONFIG_H
#  include <config.h>
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

int slapd_get_socket_peer(PRFileDesc *nspr_fd, uid_t *uid, gid_t *gid)
{
	int ret = -1;
	int fd = PR_FileDesc2NativeHandle(nspr_fd); /* naughty private func */

#if defined(SO_PEERCRED) /* linux */

	struct ucred creds;
	socklen_t len = sizeof(creds);

	if(0 == getsockopt(fd, SOL_SOCKET, SO_PEERCRED, (void*)&creds, &len ))
	{
		if(sizeof(creds) == len)
		{
			if(uid)
				*uid = creds.uid;
			if(gid)
				*gid = creds.gid;

			ret = 0;
		}
	}

#elif defined(HAVE_GETPEERUCRED) /* solaris10 */

	ucred_t *creds = 0;

	if(0 == getpeerucred(fd, &creds))
	{
		if(uid)
		{
			*uid = ucred_getruid(creds);
			if(-1 != uid)
				ret = 0;
		}

		if(gid)
		{
			*gid = ucred_getrgid(creds);
			if(-1 == *gid)
				ret = -1;
			else
				ret = 0;
		}

		ucred_free(creds);
	}

#elif defined(HAVE_GETPEEREID) /* osx / some BSDs */

	if(0 == getpeereid(fd, &uid, &gid))
		ret = 0;

#else /* hpux / Solaris9 / some BSDs - file descriptor cooperative auth */
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
	struct msghdr msg;
	struct iovec iov;
	char dummy[8];
	int pass_sd[2];
	int rc = 0;
	unsigned int retrycnt = 0xffffffff;	/* safety net */
	int myerrno = 0;

	memset((void *)&msg, 0, sizeof(msg));
	
	iov.iov_base = dummy;
	iov.iov_len = sizeof(dummy);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_accrights = (caddr_t)&pass_sd;
	msg.msg_accrightslen = sizeof(pass_sd); /* Initialize it with 8 bytes. 
											   If recvmsg is successful,
											   4 is supposed to be returned. */
	/* 
	   Since PR_SockOpt_Nonblocking is set to the socket,
	   recvmsg returns immediately if no data is waiting to be received.
	   If recvmsg returns an error and EGAIN (== EWOULDBLOCK) is set to errno, 
	   we should retry some time.
	 */
	while ((rc = recvmsg(fd, &msg, MSG_PEEK)) < 0 && (EAGAIN == (myerrno = errno)) && retrycnt-- >= 0)
		;

	if (rc >= 0 && msg.msg_accrightslen == sizeof(int))
	{
		struct stat st;

		ret = fstat(pass_sd[0], &st);

		if(0 == ret && S_ISFIFO(st.st_mode) &&
		   0 == (st.st_mode & (S_IRWXG|S_IRWXO)))
		{
			if(uid)
				*uid = st.st_uid;

			if(gid)
				*gid = st.st_gid;
		} else {
			ret = -1;
		}
	}

#endif

	return ret;
}

#endif /* ENABLE_LDAPI */
