/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * net.c: sockets abstraction and DNS related things
 * 
 * Note: sockets created with net_socket are placed in non-blocking mode,
 *       however this API simulates that the calls are blocking.
 *
 * Rob McCool
 */


#include "netsite.h"
#include "prio.h"
#include "private/pprio.h"
#include <nspr.h>

#include <frame/conf.h>
/* Removed for ns security integration
#include "sslio/sslio.h"
*/

#include "net.h"
#include "util.h"
#include "daemon.h"  /* child_exit */
#include "ereport.h" /* error reporting */
#include <string.h>
#ifdef XP_UNIX
#include <arpa/inet.h>  /* inet_ntoa */
#include <netdb.h>      /* hostent stuff */
#ifdef NEED_GHN_PROTO
extern "C" int gethostname (char *name, size_t namelen);
#endif
#endif /* XP_UNIX */
#ifdef LINUX
#include <sys/ioctl.h> /* ioctl */
#endif

extern "C" {
#include "ssl.h"
}

#if defined(OSF1)
#include <stropts.h>
#endif
#include "base/systems.h"
#include "base/dbtbase.h"

#if defined(OSF1)
#include <stropts.h>
#endif

#ifdef IRIX
#include <bstring.h>   /* fd_zero uses bzero */
#endif
#include "netio.h"

net_io_t net_io_functions;
/* removed for ns security integration
#include "xp_error.h"
*/

#include "libadmin/libadmin.h"

int net_enabledns = 1;
int net_enableAsyncDNS = 0;
int net_listenqsize = DAEMON_LISTEN_SIZE;
unsigned int NET_BUFFERSIZE = NET_DEFAULT_BUFFERSIZE;
unsigned int NET_READ_TIMEOUT = NET_DEFAULT_READ_TIMEOUT;
unsigned int NET_WRITE_TIMEOUT = NET_DEFAULT_WRITE_TIMEOUT;
unsigned int SSL_HANDSHAKE_TIMEOUT = SSL_DEFAULT_HANDSHAKE_TIMEOUT;


/* ------------------------------ net_init -------------------------------- */
NSAPI_PUBLIC int net_init(int security_on)
{
    return 0;
}

/* ------------------------------ net_socket ------------------------------ */
NSAPI_PUBLIC SYS_NETFD net_socket(int domain, int type, int protocol)
{
    SYS_NETFD sock;
    SYS_NETFD prsock;

    if (security_active) {
      if (protocol == IPPROTO_TCP) 
         prsock = PR_NewTCPSocket();
      else 
         prsock = PR_NewUDPSocket();
      if(prsock)
         sock = SSL_ImportFD(NULL, prsock);
      else sock = NULL;
    }
    else {
      if (protocol == IPPROTO_TCP) sock = PR_NewTCPSocket();
      else sock = PR_NewUDPSocket();
    }

    if (sock == NULL)
        return (SYS_NETFD)SYS_NET_ERRORFD;
    return sock;
}


/* ---------------------------- net_getsockopt ---------------------------- */
NSAPI_PUBLIC int net_getsockopt(SYS_NETFD s, int level, int optname,
                                void *optval, int *optlen)
{
    return getsockopt(PR_FileDesc2NativeHandle(s), level, optname,
                      (char *)optval, (TCPLEN_T *)optlen);
}


/* ---------------------------- net_setsockopt ---------------------------- */


NSAPI_PUBLIC int net_setsockopt(SYS_NETFD s, int level, int optname,
                                const void *optval, int optlen)
{
    return setsockopt(PR_FileDesc2NativeHandle(s), level, optname, 
                      (const char *)optval, optlen);
}
/* ------------------------------ net_listen ------------------------------ */


NSAPI_PUBLIC int net_listen(SYS_NETFD s, int backlog)
{
    return PR_Listen(s, backlog)==PR_FAILURE?IO_ERROR:0;
}


/* ------------------------- net_create_listener -------------------------- */


NSAPI_PUBLIC SYS_NETFD net_create_listener(char *ipstr, int port)
{
    SYS_NETFD sd;
    /*
    struct sockaddr_in sa_server;
    */
    PRNetAddr sa_server;
    PRStatus status;
    PRInt32 flags;

    if ((sd = net_socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) == SYS_NET_ERRORFD) {
        return SYS_NET_ERRORFD;
    }

#ifdef SOLARIS
    /*
     * unset NONBLOCK flag; 
     */
    /* Have no idea why Solaris want to unset NONBLOCK flag when it should 
       be in NON-BLOCK mode, and new NSPR20 does not give file descriptor
       back, so the code are removed --- yjh    
    flags = fcntl(sd->osfd, F_GETFL, 0);
    fcntl(sd->osfd, F_SETFL, flags & ~O_NONBLOCK);
       */
#endif
    /* Convert to NSPR21 for ns security integration
    ZERO((char *) &sa_server, sizeof(sa_server));
    sa_server.sin_family=AF_INET;
    sa_server.sin_addr.s_addr = (ipstr ? inet_addr(ipstr) : htonl(INADDR_ANY));
    sa_server.sin_port=htons(port);
    if(net_bind(sd, (struct sockaddr *) &sa_server,sizeof(sa_server)) < 0) {
        return SYS_NET_ERRORFD;
    }
    net_listen(sd, net_listenqsize);
    */

    if (ipstr) {
      status = PR_InitializeNetAddr(PR_IpAddrNull, port, &sa_server);
      if (status == PR_SUCCESS) sa_server.inet.ip = inet_addr(ipstr);
      else return SYS_NET_ERRORFD;
    }
    else {
      status = PR_InitializeNetAddr(PR_IpAddrAny, port, &sa_server);
      if (status == PR_FAILURE) return SYS_NET_ERRORFD;
    }

    status = PR_Bind(sd, &sa_server);
    if (status == PR_FAILURE) return SYS_NET_ERRORFD;

    
    status = PR_Listen(sd, net_listenqsize);
    if (status == PR_FAILURE) return SYS_NET_ERRORFD;

    return sd;
}
/* ------------------------------ net_select ------------------------------ */

/*
NSAPI_PUBLIC int net_select(int nfds, fd_set *r, fd_set *w, fd_set *e,
                            struct timeval *timeout)
{
    PR_fd_set rd, wr, ex;
    int index;
    int rv;

    if (nfds > (64*1024))
        return -1;

    PR_FD_ZERO(&rd);
    PR_FD_ZERO(&wr);
    PR_FD_ZERO(&ex);

    for (index=0; index<nfds; index++) {
        if (FD_ISSET(index, r)) 
            PR_FD_NSET(index, &rd);
        if (FD_ISSET(index, w)) 
            PR_FD_NSET(index, &wr);
        if (FD_ISSET(index, e)) 
            PR_FD_NSET(index, &ex);
    }

    rv = PR_Select(0, &rd, &wr, &ex, PR_SecondsToInterval(timeout->tv_sec));
    if (rv > 0) {
        FD_ZERO(r);
        FD_ZERO(w);
        FD_ZERO(e);
        for (index=0; index<nfds; index++) {
            if (PR_FD_NISSET(index, &rd)) 
                FD_SET(index, r);
            if (PR_FD_NISSET(index, &wr)) 
                FD_SET(index, w);
            if (PR_FD_NISSET(index, &ex)) 
                FD_SET(index, e);
        }
    }

    return rv;
}
*/

NSAPI_PUBLIC int net_select(int nfds, fd_set *r, fd_set *w, fd_set *e,
                            struct timeval *timeout)
{
    return 1;
}


/* ----------------------------- net_isalive ------------------------------ */


/* 
 *  XXXmikep As suggested by shaver@ingenia.com.  If everyone was POSIX
 *  compilent, a write() of 0 bytes would work as well 
 */
NSAPI_PUBLIC int net_isalive(SYS_NETFD sd)
{
    char c;
    if (PR_RecvFrom(sd, &c, 1, MSG_PEEK, NULL, 0) == -1 ) {
      return 0;
    }
    return 1;
}


/* ------------------------------ net_connect ------------------------------ */

NSAPI_PUBLIC int net_connect(SYS_NETFD s, const void *sockaddr, int namelen)
{
    int rv;

    child_status(CHILD_WRITING);
    rv = PR_Connect(s, (PRNetAddr *)sockaddr, PR_INTERVAL_NO_TIMEOUT);
    child_status(CHILD_PROCESSING);

    return rv==PR_FAILURE?IO_ERROR:0;
}


/* ------------------------------ net_ioctl ------------------------------ */


NSAPI_PUBLIC int net_ioctl(SYS_NETFD s, int tag, void *result)
{
#if defined(NET_WINSOCK)
    return ioctlsocket(PR_FileDesc2NativeHandle(s),tag,(unsigned long *)result);
#elif defined(XP_UNIX)
    return ioctl(PR_FileDesc2NativeHandle(s), tag, result);
#else
    write me;
#endif

}
/* --------------------------- net_getpeername ---------------------------- */


NSAPI_PUBLIC int net_getpeername(SYS_NETFD s, struct sockaddr *name,
                                 int *namelen)
{
#if defined (SNI) || defined (UnixWare)
    return getpeername(PR_FileDesc2NativeHandle(s), name, (size_t *)namelen);
#else /* defined (SNI) || defined (UnixWare) */
    return getpeername(PR_FileDesc2NativeHandle(s), name, (TCPLEN_T *)namelen);
#endif /* defined (SNI) || defined (UnixWare) */
}


/* ------------------------------ net_close ------------------------------- */


NSAPI_PUBLIC int net_close(SYS_NETFD s)
{
    return PR_Close(s)==PR_FAILURE?IO_ERROR:0;
}

NSAPI_PUBLIC int net_shutdown(SYS_NETFD s, int how)
{
  switch (how) {
  case 0:
    return PR_Shutdown(s, PR_SHUTDOWN_RCV);
    break;
  case 1:
    return PR_Shutdown(s, PR_SHUTDOWN_SEND);
    break;
  case 2:
    return PR_Shutdown(s, PR_SHUTDOWN_BOTH);
    break;
  default:
    return -1;
  }

  return 0;
}



/* ------------------------------- net_bind ------------------------------- */

NSAPI_PUBLIC int net_bind(SYS_NETFD s, const struct sockaddr *name,
                          int namelen)
{
    return PR_Bind(s, (const PRNetAddr *)name)==PR_FAILURE?IO_ERROR:0;
}


/* ------------------------------ net_accept ------------------------------ */


NSAPI_PUBLIC SYS_NETFD net_accept(SYS_NETFD  sd, struct sockaddr *addr,
    int *addrlen)
{
    SYS_NETFD sock = PR_Accept(sd, (PRNetAddr *)addr, PR_INTERVAL_NO_TIMEOUT);

    if (sock == NULL)
        return SYS_NET_ERRORFD;
    return sock;
}

/* ------------------------------- net_read ------------------------------- */

NSAPI_PUBLIC int net_read(SYS_NETFD fd, char *buf, int sz, int timeout)
{
    int rv;

    if (timeout == NET_ZERO_TIMEOUT)
        timeout = PR_INTERVAL_NO_WAIT;
    else if (timeout == NET_INFINITE_TIMEOUT)
        timeout = PR_INTERVAL_NO_TIMEOUT;
    else
        timeout = PR_SecondsToInterval(timeout);

    child_status(CHILD_READING);
    rv = PR_Recv(fd, buf, sz, 0, timeout);

    child_status(CHILD_PROCESSING);
    return rv;
}


/* ------------------------------ net_write ------------------------------- */

#ifndef NEEDS_WRITEV
int net_writev(SYS_NETFD fd, struct iovec *iov, int iov_size)
{
    int rv;

    child_status(CHILD_WRITING);
    rv  = PR_Writev(fd, (PRIOVec *)iov, iov_size, PR_INTERVAL_NO_TIMEOUT);
    child_status(CHILD_PROCESSING);
    return rv;
}

#else /* NEEDS_WRITEV */

/* Since SSL and NT do not support writev(), we just emulate it.
 * This does not lead to the optimal number of packets going out...
 */
int net_writev(SYS_NETFD fd, struct iovec *iov, int iov_size)
{
    int index;

    child_status(CHILD_WRITING);

    for (index=0; index<iov_size; index++) {

        /* net_write already does the buffer for nonblocked IO */
        if ( net_write(fd, iov[index].iov_base, iov[index].iov_len) ==IO_ERROR){
            child_status(CHILD_PROCESSING);
            return IO_ERROR;
        }
    }

    child_status(CHILD_PROCESSING);
    return IO_OKAY;
}
#endif /* NEEDS_WRITEV */


NSAPI_PUBLIC int net_write(SYS_NETFD fd, char *buf, int sz)
{
    int rv;

    child_status(CHILD_WRITING);
    rv = PR_Send(fd, buf, sz, 0, PR_INTERVAL_NO_TIMEOUT);
    child_status(CHILD_PROCESSING);
    if(rv < 0) {
        return IO_ERROR;
    }
    return rv;
}

NSAPI_PUBLIC int net_socketpair(SYS_NETFD *pair)
{
    return PR_NewTCPSocketPair(pair);
}

#ifdef XP_UNIX
NSAPI_PUBLIC SYS_NETFD net_dup2(SYS_NETFD prfd, int osfd)
{
    SYS_NETFD newfd = NULL;

    if (prfd && PR_FileDesc2NativeHandle(prfd) != osfd) {
        if (dup2(PR_FileDesc2NativeHandle(prfd), osfd) != -1) {
            newfd = PR_ImportFile(osfd);
            if (!newfd)
                close(osfd);
        }
    }

    return newfd;
}

NSAPI_PUBLIC int net_is_STDOUT(SYS_NETFD prfd)
{
  int fd = PR_FileDesc2NativeHandle(prfd);
  if (fd == STDOUT_FILENO) return 1;
  return 0;
}

NSAPI_PUBLIC int net_is_STDIN(SYS_NETFD prfd)
{
  int fd = PR_FileDesc2NativeHandle(prfd);
  if (fd == STDIN_FILENO) return 1;
  return 0;
}

#endif /* XP_UNIX */

/* -------------------------- Accept mutex crap --------------------------- */


#ifndef NET_WINSOCK


#include "sem.h"
static SEMAPHORE mob_sem;
static int have_mob_sem;


void net_accept_enter(void)
{
    if(sem_grab(mob_sem) == -1)
        ereport(LOG_CATASTROPHE, "sem_grab failed (%s)", system_errmsg());
    have_mob_sem = 1;
}

int net_accept_tenter(void)
{
    int ret = sem_tgrab(mob_sem);
    if(ret != -1)
        have_mob_sem = 1;
    return ret;
}

void net_accept_exit(void)
{
    if(sem_release(mob_sem) == -1)
        ereport(LOG_CATASTROPHE, "sem_release failed (%s)", system_errmsg());
    have_mob_sem = 0;
}

#ifdef AIX
#undef accept
#define accept naccept
#endif

void net_accept_texit(void)
{
    if(have_mob_sem && (sem_release(mob_sem) == -1))
        ereport(LOG_CATASTROPHE, "sem_release failed (%s)", system_errmsg());
    have_mob_sem = 0;
}

int net_accept_init(int port)
{
  /* XXXMB how to translate this to nspr? */
  /* since SSL_AcceptHook is no longer in ns security (HCL_1_5), 
     so this is gone! (It does exist in HCL_101)
  SSL_AcceptHook((SSLAcceptFunc)PR_Accept);
  */
  have_mob_sem = 0;
  mob_sem = sem_init("netsite", port);
  return (mob_sem == SEM_ERROR ? -1 : 0);
}

void net_accept_terminate(void)
{
    sem_terminate(mob_sem);
}

#endif  /* !NET_WINSOCK */


/* ----------------------------- net_ip2host ------------------------------ */


char *dns_ip2host(char *ip, int verify);

NSAPI_PUBLIC char *net_ip2host(char *ip, int verify)
{
    if(!net_enabledns)
        return NULL;

    return dns_ip2host(ip, verify);
}



/* ---------------------------- util_hostname ----------------------------- */



#ifdef XP_UNIX
#include <sys/param.h>
#else /* WIN32 */
#define MAXHOSTNAMELEN 255
#endif /* XP_UNIX */

/* Defined in dns.c */
char *net_find_fqdn(PRHostEnt *p);

NSAPI_PUBLIC char *util_hostname(void)
{
    char str[MAXHOSTNAMELEN];
    PRHostEnt   hent;
    char        buf[PR_NETDB_BUF_SIZE];
    PRStatus    err;

    gethostname(str, MAXHOSTNAMELEN);
    err = PR_GetHostByName(
                str,
                buf,
                PR_NETDB_BUF_SIZE,
                &hent);

    if (err == PR_FAILURE) 
        return NULL;
    return net_find_fqdn(&hent);
}


