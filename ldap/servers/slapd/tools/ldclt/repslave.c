#ident "@(#)repslave.c	1.15 99/06/09"

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
        FILE :		repslave.c
        AUTHOR :        Fabio Pistolesi
        VERSION :       1.0
        DATE :		05 May 1999
        DESCRIPTION :	
			This file contains the implementation of the slave part 
			of ldclt tool. This slave is intended to scan the logs 
			of the ldap server and to communicate the operations 
			logged to the master ldclt, to be checked against the 
			memorized operations performed on the master ldap 
			server.
 LOCAL :		None.
        HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
05/05/99 | F. Pistolesi	| Creation
---------+--------------+------------------------------------------------------
06/05/99 | JL Schwing	| 1.2 : Port on Solaris 2.5.1
---------+--------------+------------------------------------------------------
10/05/99 | F. Pistolesi	| Added multiple filtered servers to send results to.
---------+--------------+------------------------------------------------------
18/05/99 | JL Schwing	| 1.8 : Port on 2.5.1
---------+--------------+------------------------------------------------------
26/05/99 | F. Pistolesi	| 1.10: Bug fix - missing free()
---------+--------------+------------------------------------------------------
27/05/99 | F. Pistolesi	| 1.11: Add new option -d (debug)
---------+--------------+------------------------------------------------------
29/05/99 | F. Pistolesi	| 1.12: Add new option -t (log)
---------+--------------+------------------------------------------------------
09/06/99 | JL Schwing	| 1.14: Bug fix - crash in send_op() if tmp.dn==NULL
---------+--------------+------------------------------------------------------
09/06/99 | F. Pistolesi	| 1.15: Fix the fix above.
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

/*
 * Enumeration for internal list
 */
enum {ADD,DELETE,MODRDN,MODIFY,RESULT,LAST};

/*
 * internal list
 */
typedef struct {
	int conn,op,type;
	char *dn;
	} Optype;

typedef struct {
	int fd;
	char *filter,*hname;
	struct sockaddr_in addr;
	} Towho;

Optype *pendops;
Towho *srvlist;
int npend,maxop,connected,nsrv,debug;
char *ldap_ops[]={"ADD","DEL","MODRDN","MOD ","RESULT","NONE",NULL};
/*
 * To map internal values to LDAP_REQ
 */
int ldap_val[]={LDAP_REQ_ADD,LDAP_REQ_DELETE,LDAP_REQ_MODRDN,LDAP_REQ_MODIFY};

get_op_par(char *s,Optype *op)
{
  char *t;
  int i;

 /*
  * Provided they do not change dsservd's log format, this should work
  * Magic numbers are the length of the lookup string
  */
  t=strstr(s,"conn=");
  for(t+=5,op->conn=0;isdigit(*t);t++)
	op->conn=op->conn*10+*t-'0';
  t=strstr(s,"op=");
  for(t+=3,op->op=0;isdigit(*t);t++)
	op->op=op->op*10+*t-'0';
  if(t=strstr(s,"dn="))
	op->dn=strdup(t+3);
}

open_cnx(struct sockaddr *srv)
{
  int i,sockfd;

  if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
  {
	perror("Slave");
	exit(1);
  }
  i=1;
 /*
  * Disable TCP's Nagle algorithm
  */
  if(setsockopt(sockfd,IPPROTO_TCP, TCP_NODELAY,(void *)&i,sizeof(int))!=0)
	perror("Nagle");
  if(connect(sockfd,srv,sizeof(struct sockaddr))!=-1)
	return sockfd;
  else close(sockfd);
  return -1;
}

send_op(char* s)
{
  int sz,i,j,ret;
  Optype tmp;
  repconfirm *result;
  char *t;
  struct pollfd pfd;

  get_op_par(s,&tmp);
  for(i=0;i<maxop;i++)
	/*
	 * got a RESULT string. Try to match with known operations.
	 */
	if(pendops[i].op==tmp.op && pendops[i].conn==tmp.conn)
	{
		sz=strlen(pendops[i].dn);
		result=(repconfirm*)malloc(sizeof(repconfirm)+sz);
		t=strstr(s,"err=");
		for(t+=4,result->res=0;isdigit(*t);t++)
			result->res=result->res*10+*t-'0';
		/*
		 * Build packet
		 */
		result->type=htonl(ldap_val[pendops[i].type]);
		strcpy(result->dn,pendops[i].dn+1);
		sz-=2;
		result->dn[sz]='\0';
		result->dnSize=htonl(sz);
		if(debug)
			printf("Sending %d %d %s\n",ntohl(result->type),result->res,result->dn);
		result->res=htonl(result->res);
		/*
		 * find which filter applies. Note that if no filter applies, no
		 * packets are sent
		 */
		for(j=0;j<nsrv;j++)
		{
			/*
			 * Suppose a NULL filter means everything
			 */
			if(srvlist[j].filter)
				if(regex(srvlist[j].filter,result->dn)==NULL)
					continue;
			/*
			 * try to write. This works if server set SO_LINGER option
			 * with parameters l_onoff=1,l_linger=0. This means terminate
			 * the connection sending an RST instead of FIN, so that
			 * write() will fail on first attempt instead of second.
			 */
			if(write(srvlist[j].fd,result,sizeof(repconfirm)+sz)<=0)
			{
				/*
				 * socket was closed by peer. try again
				 */
				close(srvlist[j].fd);
				if((srvlist[j].fd=connected=open_cnx((struct sockaddr*)&srvlist[j].addr))==-1)
					/*
					 * OK, server disconnected for good
					 */
					continue;
				if((ret=write(srvlist[j].fd,result,sizeof(repconfirm)+sz))<=0)
					puts("Porc!");
			}
		}
		/*
		 * Copy over the operation at the end
		 */
		free(pendops[i].dn);
		maxop--;
		pendops[i]=pendops[maxop];
		free(result);
		break;
	}
  if (tmp.dn != NULL)
    free(tmp.dn);
}

main(int argc, char**argv)
{
  int i,port=16000;
  int sockfd,log=0;
  static char logline[512];
  char **tmp,*hn,*hp,*hf;
  struct hostent *serveraddr;

  while((i=getopt(argc,argv,"tdP:s:"))!=EOF)
  {
	switch(i)
	{
		case 't': log=1;
			  break;
		case 'd': debug=1;
			  break;
		case 'P':
			port=atoi(optarg);
			break;
		case 's':
			/*
			 * pointers to hostname, host port, filter
			 */
			hn=strtok(optarg,",");
			hp=strtok(NULL,",");
			hf=strtok(NULL,",");
			if(hf==NULL&&hp)
				if(*hp<'0' || *hp >'9')
					hf=hp;
			if(hn==NULL||hf==NULL)
			{
				puts("Missing parameter\n");
				break;
			}
			/*
			 * Get master address, just the first.
			 */
			if((serveraddr=gethostbyname(hn))==NULL)
			{
				printf("Unknown host %s\n",hn);
				break;
			}
			srvlist=(Towho*)realloc(srvlist,(nsrv+1)*sizeof(Towho));
			srvlist[nsrv].addr.sin_addr.s_addr=htonl(*((u_long*)(serveraddr->h_addr_list[0])));
			srvlist[nsrv].addr.sin_family=AF_INET;
			srvlist[nsrv].addr.sin_port=htonl((hp==hf?port:atoi(hp)));
			if((srvlist[nsrv].filter=regcmp(hf,NULL))==NULL)
				printf("Wrong REX: %s\n",hf);
			srvlist[nsrv].fd=open_cnx((struct sockaddr*)&srvlist[nsrv].addr);
			srvlist[nsrv].hname=strdup(hn);
			nsrv++;
			break;
	}
  }
  if(!nsrv)
  {
	if(!argv[optind])
	{
		printf("Usage: %s [-td] -P port <hostname>\n\tor %s [-td] -s <host>,[<port>,]<REGEX>\n",argv[0],argv[0]);
		printf("\t-t\tprints input on stdout.\n\t-d\tdebug mode.\n");
		exit(1);
	}
	srvlist=(Towho*)malloc(sizeof(Towho));
	if((serveraddr=gethostbyname(argv[optind]))==NULL)
	{
		printf("Unknown host %s\n",argv[optind]);
		exit(1);
	}
	srvlist[nsrv].addr.sin_addr.s_addr=htonl(*((u_long*)(serveraddr->h_addr_list[0])));
	srvlist[nsrv].addr.sin_family=AF_INET;
	srvlist[nsrv].addr.sin_port=htons(port);
	srvlist[nsrv].filter=NULL;
	srvlist[nsrv].fd=open_cnx((struct sockaddr*)&srvlist[nsrv].addr);
	srvlist[nsrv].hname=strdup(argv[optind]);
	nsrv++;
  }
  maxop=npend=0;
  pendops=(Optype*)malloc(sizeof(Optype)*20);
 /*
  * Ignore SIGPIPE during write()
  */
  sigset(SIGPIPE,SIG_IGN);
  while(fgets(logline, sizeof(logline), stdin))
  {
	if (p = strchr(logline, '\n')) {
	  *p = 0;
	}
	if(log)
		puts(logline);
	for(tmp=ldap_ops,i=0;tmp[i];i++)
		if(strstr(logline,tmp[i]))
			break;
	if(i<RESULT)
	{
		get_op_par(logline,&pendops[maxop]);
		pendops[maxop].type=i;
		if(++maxop>npend)
			npend=maxop;
		if(!(npend%20))
		{
			pendops=(Optype*)realloc(pendops,sizeof(Optype)*(npend+20));
			memset(pendops+npend,0,sizeof(Optype)*20);
		}
	}
	if(i==RESULT)
		send_op(logline);
  }
}


/* End of file */
