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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/inttypes.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "remote.h"
#include "lber.h"
#include "ldap.h"

enum {ADD,DELETE,MODRDN,MODIFY,RESULT};

typedef struct {
	int conn,op,type;
	char *dn;
	} Optype;

Optype *pendops;
int npend=-1,maxop;
char *ldap_ops[]={"ADD","DEL","MODRDN","MOD ","RESULT",NULL};
int ldap_val[]={LDAP_REQ_ADD,LDAP_REQ_DELETE,LDAP_REQ_MODRDN,LDAP_REQ_MODIFY};

print_packet(repconfirm *op)
{
 int i;
 printf("type=%d, res=%d, dnlen=%d, dN: %s\n",op->type,op->res,op->dnSize,op->dn);

}

main(int argc, char**argv)
{
 int i,port=16000;
 int sockfd,newfd;
 static char buff[512];
 char **tmp;
 struct sockaddr_in srvsaddr,claddr;
 struct hostent *cltaddr;
 uint32_t ipaddr;

 while((i=getopt(argc,argv,"p:"))!=EOF){
	switch(i){
		case 'p': port=atoi(optarg);
			break;
		}
	}
 srvsaddr.sin_addr.s_addr=htonl(INADDR_ANY);
 srvsaddr.sin_family=AF_INET;
 srvsaddr.sin_port=htons(port);
 if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1){
	perror("Socket");
	exit(1);
	}
 i=1;
 if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&i,sizeof(int))!=0)
	perror("Sockopt");
 if(bind(sockfd,(struct sockaddr*)&srvsaddr,sizeof(struct sockaddr))!=0){
	perror("Bind");
	exit(1);
	}
 if(listen(sockfd,1)!=0)
	perror("listen");
 for(;;){
	i=sizeof(claddr);
	if((newfd=accept(sockfd,(struct sockaddr *)&claddr,&i))<0){
		perror("Accept");
		exit(1);
		}
	ipaddr=ntohl(claddr.sin_addr.s_addr);
	cltaddr=gethostbyaddr((char*)&ipaddr,sizeof(ipaddr),AF_INET);
	printf("Accepting from %s\n",cltaddr->h_name);
	while(read(newfd,buff,512)>0){
		print_packet((repconfirm*) buff);
		memset(buff,0,512);
		}
	close(newfd);
	}
 close(sockfd);
}

