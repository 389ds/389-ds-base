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

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include "remote.h"
#include "lber.h"
#include "ldap.h"

enum {ADD,DELETE,MODRDN,MODIFY,RESULT};

typedef struct {
	int conn,op,type;
	char *dn;
	} Optype;

Optype *pendops;
int npend,maxop,connected;
char *ldap_ops[]={"ADD","DEL","MODRDN","MOD ","RESULT",NULL};
int ldap_val[]={LDAP_REQ_ADD,LDAP_REQ_DELETE,LDAP_REQ_MODRDN,LDAP_REQ_MODIFY};

get_op_par(char *s,Optype *op)
{
 char *t;
 int i;

 t=strstr(s,"conn=");
 for(t+=5,op->conn=0;isdigit(*t);t++)
	op->conn=op->conn*10+*t-'0';
 t=strstr(s,"op=");
 for(t+=3,op->op=0;isdigit(*t);t++)
	op->op=op->op*10+*t-'0';
 if(t=strstr(s,"dn="))
	op->dn=strdup(t+3);
}

send_op(char* s,int sfd)
{
 int sz,i;
 Optype tmp;
 repconfirm *result;
 char *t;

 get_op_par(s,&tmp);
 for(i=0;i<maxop;i++)
	if(pendops[i].op==tmp.op && pendops[i].conn==tmp.conn){
		t=strstr(s,"err=");
		sz=strlen(pendops[i].dn);
		result=(repconfirm*)malloc(sizeof(repconfirm)+sz);
		for(t+=4,result->res=0;isdigit(*t);t++)
			result->res=result->res*10+*t-'0';
		result->type=htonl(ldap_val[pendops[i].type]);
		strcpy(result->dn,pendops[i].dn);
		result->dnSize=htonl(sz);
		result->res=htonl(result->res);
		if(write(sfd,result,sizeof(repconfirm)+sz)<=0){
			close(sfd);
			memset(pendops,0,maxop*sizeof(Optype));
			maxop=npend=connected=0;
			return;
			}
		if(i!=maxop)
			pendops[i]=pendops[maxop];
		else memset(pendops+i,0,sizeof(Optype));
		return;
		}
}

main(int argc, char**argv)
{
 int i,port=16000;
 int sockfd;
 static char logline[512];
 char **tmp;
 struct hostent *serveraddr;
 struct sockaddr_in srvsaddr;

 while((i=getopt(argc,argv,"p:"))!=EOF){
	switch(i){
		case 'p': port=atoi(optarg);
			break;
		}
	}
 serveraddr=gethostbyname(argv[optind]);
 srvsaddr.sin_addr.s_addr=htonl(*((u_long*)(serveraddr->h_addr_list[0])));
 srvsaddr.sin_family=AF_INET;
 srvsaddr.sin_port=htons(port);
 maxop=npend=0;
 pendops=(Optype*)malloc(sizeof(Optype)*20);
 sigset(SIGPIPE,SIG_IGN);
 while(gets(logline)){
	 if(!connected){
		if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1){
			perror(argv[0]);
			exit(1);
			}
		i=1;
		if(setsockopt(sockfd,IPPROTO_TCP, TCP_NODELAY,&i,sizeof(int))!=0)
			perror("Nagle");
		if(connect(sockfd,(struct sockaddr*)&srvsaddr,sizeof(struct sockaddr))!=-1)
			connected=1;
		 else {
			close(sockfd);
			continue;
			}
		}
	for(tmp=ldap_ops,i=0;tmp[i];i++)
		if(strstr(logline,tmp[i]))
			break;
	if(i<RESULT){
		get_op_par(logline,&pendops[maxop]);
		pendops[maxop].type=i;
		if(++maxop>npend)
			npend=maxop;
		if(!(npend%20)){
			pendops=(Optype*)realloc(pendops,sizeof(Optype)*(npend+20));
			memset(pendops+npend,0,sizeof(Optype)*20);
			}
		}
	if(i==RESULT)
		send_op(logline,sockfd);
	}
 close(sockfd);
}

