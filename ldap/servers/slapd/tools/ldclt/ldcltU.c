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
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <stdio.h>


/*
 * usage: ldclt [-qQvV] [-E <max errors>]
 *              [-b <base DN>] [-h <host>] [-p <port>] [-t <timeout>]
 *              [-D <bind DN>] [-w <passwd>] [-o <SASL option>]
 *              [-e <execParams>] [-a <max pending>]
 * 	     [-n <nb threads>] [-i <nb times>] [-N <nb samples>]
 * 	     [-I <err number>] [-T <total>]
 * 	     [-r <low> -R <high>]
 * 	     [-f <filter>] [-s <scope>]
 * 	     [-S <slave>] [-P<master port>]
 * 	     [-W <waitsec>] [-Z <certfile>]
 * 
 * 	This tool is a ldap client targetted to validate the reliability of
 * 	the product under test under hard use.
 * 
 * 	The valid options are:
 * 	 -a  Asynchronous mode, with max pending operations.
 * 	 -b  Give the base DN to use. Default "dc=example,dc=com".
 * 	 -D  Bind DN. See -w
 * 	 -E  Max errors allowed.                   Default 1000.
 * 	 -e  Execution parameters:
 * 		abandon : abandon asyncronous search requests.
 * 		add	    : ldap_add() entries.
 * 		append      : append entries to the genldif file.
 * 		ascii	    : ascii 7-bits strings.
 * 		attreplace=name:mask    : replace attribute of existing entry.
 * 		attrlist=name:name:name : specify list of attribs to retrieve
 * 		attrsonly=0|1  : ldap_search() parameter. Set 0 to read values.
 * 		bindeach    : ldap_bind() for each operation.
 * 		bindonly    : only bind/unbind, no other operation is performed.
 * 		close       : will close() the fd, rather than ldap_unbind().
 * 		cltcertname=name : name of the SSL client certificate
 * 		commoncounter    : all threads share the same counter.
 * 		counteach   : count each operation not only successful ones.
 * 		delete                : ldap_delete() entries.
 * 		deref[=deref:attr]    : This option works with -e add and -e esearch.
 * 		                      : With -e esearch:
 * 		                      : adds dereference control.
 * 		                      : if =deref:attr is given, the deref and attr
 * 		                      : pair is set to the control request.
 * 		                      : if not given, "secretary:cn" is set.
 * 		                      : With -e add:
 * 		                      : adds "secretary" attr to the netOrgPerson 
 * 		                      : entries to prepare for the search with the
 * 		                      : default "secretary:cn" dereference control.
 * 		dontsleeponserverdown : will loop very fast if server down.
 * 		emailPerson           : objectclass=emailPerson (-e add only).
 * 		esearch	              : exact search.
 * 		genldif=filename      : generates a ldif file
 * 		imagesdir=path        : specify where are the images.
 * 		incr	              : incremental values.
 * 		inetOrgPerson         : objectclass=inetOrgPerson (-e add only).
 * 		keydbfile=file        : filename of the key database
 * 		keydbpin=password     : password for accessing the key database
 * 		noglobalstats         : don't print periodical global statistics
 * 		noloop	              : does not loop the incremental numbers.
 * 		object=filename       : build object from input file
 * 		person	              : objectclass=person (-e add only).
 * 		random	              : random filters, etc...
 * 		randomattrlist=name:name:name : random select attrib in the list
 * 		randombase             : random base DN.
 * 		randombaselow=value    : low value for random generator.
 * 		randombasehigh=value   : high value for random generator.
 * 		randombinddn           : random bind DN.
 * 		randombinddnfromfile=file : retrieve bind DN & passwd from file
 * 		randombinddnlow=value  : low value for random generator.
 * 		randombinddnhigh=value : high value for random generator.
 * 		rdn=attrname:value     : alternate for -f.
 * 		referral=on|off|rebind : change referral behaviour.
 * 		scalab01               : activates scalab01 scenario.
 * 		scalab01_cnxduration   : maximum connection duration.
 * 		scalab01_maxcnxnb      : modem pool size.
 * 		scalab01_wait          : sleep() between 2 attempts to connect.
 * 		smoothshutdown         : main thread waits till the worker threads exit.
 * 		string	    : create random strings rather than random numbers.
 * 		v2	    : ldap v2.
 * 		withnewparent : rename with newparent specified as argument.
 * 	 -f  Filter for searches.
 * 	 -h  Host to connect.                      Default "localhost".
 * 	 -i  Number of times inactivity allowed.   Default 3 (30 seconds)
 * 	 -I  Ignore errors (cf. -E).               Default none.
 * 	 -n  Number of threads.                    Default 10.
 * 	 -N  Number of samples (10 seconds each).  Default infinite.
 *	 -o  SASL Option.
 * 	 -p  Server port.                          Default 389.
 * 	 -P  Master port (to check replication).   Default 16000.
 * 	 -q  Quiet mode. See option -I.
 * 	 -Q  Super quiet mode.
 * 	 -r  Range's low value.
 * 	 -R  Range's high value.
 * 	 -s  Scope. May be base, subtree or one.   Default subtree.
 * 	 -S  Slave to check.
 * 	 -t  LDAP operations timeout. Default 30 seconds.
 * 	 -T  Total number of operations per thread.	   Default infinite.
 * 	 -v  Verbose.
 * 	 -V  Very verbose.
 * 	 -w  Bind passwd. See -D.
 * 	 -W  Wait between two operations.          Default 0 seconds.
 * 	 -Z  certfile. Turn on SSL and use certfile as the certificate DB
 */
void usage ()
{
  (void) printf ("\n");
  (void) printf ("usage: ldclt [-qQvV] [-E <max errors>]\n");
  (void) printf ("             [-b <base DN>] [-h <host>] [-p <port>] [-t <timeout>]\n");
  (void) printf ("             [-D <bind DN>] [-w <passwd>] [-o <SASL option>]\n");
  (void) printf ("             [-e <execParams>] [-a <max pending>]\n");
  (void) printf ("	     [-n <nb threads>] [-i <nb times>] [-N <nb samples>]\n");
  (void) printf ("	     [-I <err number>] [-T <total>]\n");
  (void) printf ("	     [-r <low> -R <high>]\n");
  (void) printf ("	     [-f <filter>] [-s <scope>]\n");
  (void) printf ("	     [-S <slave>] [-P<master port>]\n");
  (void) printf ("	     [-W <waitsec>] [-Z <certfile>]\n");
  (void) printf ("\n");
  (void) printf ("	This tool is a ldap client targetted to validate the reliability of\n");
  (void) printf ("	the product under test under hard use.\n");
  (void) printf ("\n");
  (void) printf ("	The valid options are:\n");
  (void) printf ("	 -a  Asynchronous mode, with max pending operations.\n");
  (void) printf ("	 -b  Give the base DN to use. Default \"o=sun,c=us\".\n");
  (void) printf ("	 -D  Bind DN. See -w\n");
  (void) printf ("	 -E  Max errors allowed.                   Default 1000.\n");
  (void) printf ("	 -e  Execution parameters:\n");
  (void) printf ("		abandon           : abandon async search requests.\n");
  (void) printf ("		add               : ldap_add() entries.\n");
  (void) printf ("		append            : append entries to the genldif file.\n");
  (void) printf ("		ascii             : ascii 7-bits strings.\n");
  (void) printf ("		attreplacefile=attrname:<file name> : replace attribute with given file content.\n");
  (void) printf ("		attreplace=name:mask    : replace attribute of existing entry.\n");
  (void) printf ("		attrlist=name:name:name : specify list of attribs to retrieve\n");
  (void) printf ("		attrsonly=0|1     : ldap_search() parameter. Set 0 to read values.\n");
  (void) printf ("		bindeach          : ldap_bind() for each operation.\n");
  (void) printf ("		bindonly          : only bind/unbind, no other operation is performed.\n");
  (void) printf ("		close             : will close() the fd, rather than ldap_unbind().\n");
  (void) printf ("		cltcertname=name  : name of the SSL client certificate\n");
  (void) printf ("		commoncounter     : all threads share the same counter.\n");
  (void) printf ("		counteach         : count each operation not only successful ones.\n");
  (void) printf ("		delete            : ldap_delete() entries.\n");
  (void) printf ("		deref[=deref:attr]: This option works with -e add and esearch.\n");
  (void) printf ("		                  : With -e esearch:\n");
  (void) printf ("		                  : adds dereference control.\n");
  (void) printf ("		                  : if =deref:attr is given, the deref and attr\n");
  (void) printf ("		                  : pair is set to the control request.\n");
  (void) printf ("		                  : if not given, \"secretary:cn\" is set.\n");
  (void) printf ("		                  : With -e add:\n");
  (void) printf ("		                  : adds \"secretary\" attr to the inetOrgPerson\n");
  (void) printf ("		                  : entries to prepare for -e esearch using\n");
  (void) printf ("		                  : the default deref attr pair.\n");
  (void) printf ("		dontsleeponserverdown : will loop very fast if server down.\n");
  (void) printf ("		emailPerson       : objectclass=emailPerson (-e add only).\n");
  (void) printf ("		esearch           : exact search.\n");
  (void) printf ("		genldif=filename  : generates a ldif file\n");
  (void) printf ("		imagesdir=path    : specify where are the images.\n");
  (void) printf ("		incr              : incremental values.\n");
  (void) printf ("		inetOrgPerson     : objectclass=inetOrgPerson (-e add only).\n");
  (void) printf ("		keydbfile=file    : filename of the key database\n");
  (void) printf ("		keydbpin=password : password for accessing the key database\n");
  (void) printf ("		noglobalstats     : don't print periodical global statistics\n");
  (void) printf ("		noloop            : does not loop the incremental numbers.\n");
  (void) printf ("		object=filename   : build object from input file\n");
  (void) printf ("		person            : objectclass=person (-e add only).\n");
  (void) printf ("		random            : random filters, etc...\n");
  (void) printf ("		randomattrlist=name:name:name : random select attrib in the list\n");
  (void) printf ("		randombase        : random base DN.\n");
  (void) printf ("		randombaselow=value    : low value for random generator.\n");
  (void) printf ("		randombasehigh=value   : high value for random generator.\n");
  (void) printf ("		randombinddn           : random bind DN.\n");
  (void) printf ("		randombinddnfromfile=file : retrieve bind DN & passwd from file\n");
  (void) printf ("		randombinddnlow=value  : low value for random generator.\n");
  (void) printf ("		randombinddnhigh=value : high value for random generator.\n");
  (void) printf ("		rdn=attrname:value     : alternate for -f.\n");
  (void) printf ("		referral=on|off|rebind : change referral behaviour.\n");
  (void) printf ("		scalab01               : activates scalab01 scenario.\n");
  (void) printf ("		scalab01_cnxduration   : maximum connection duration.\n");
  (void) printf ("		scalab01_maxcnxnb      : modem pool size.\n");
  (void) printf ("		scalab01_wait          : sleep() between 2 attempts to connect.\n");
  (void) printf ("		smoothshutdown         : main thread waits till the worker threads exit.\n");
  (void) printf ("		string                 : create random strings rather than random numbers.\n");
  (void) printf ("		v2                     : ldap v2.\n");
  (void) printf ("		withnewparent : rename with newparent specified as argument.\n");
  (void) printf ("		randomauthid           : random SASL Authid.\n");
  (void) printf ("		randomauthidlow=value  : low value for random SASL Authid.\n");
  (void) printf ("		randomauthidhigh=value : high value for random SASL Authid.\n");
  (void) printf ("	 -f  Filter for searches.\n");
  (void) printf ("	 -h  Host to connect.                      Default \"localhost\".\n");
  (void) printf ("	 -i  Number of times inactivity allowed.   Default 3 (30 seconds)\n");
  (void) printf ("	 -I  Ignore errors (cf. -E).               Default none.\n");
  (void) printf ("	 -n  Number of threads.                    Default 10.\n");
  (void) printf ("	 -N  Number of samples (10 seconds each).  Default infinite.\n");
  (void) printf ("	 -o  SASL Option.\n");
  (void) printf ("	 -p  Server port.                          Default 389.\n");
  (void) printf ("	 -P  Master port (to check replication).   Default 16000.\n");
  (void) printf ("	 -q  Quiet mode. See option -I.\n");
  (void) printf ("	 -Q  Super quiet mode.\n");
  (void) printf ("	 -r  Range's low value.\n");
  (void) printf ("	 -R  Range's high value.\n");
  (void) printf ("	 -s  Scope. May be base, subtree or one.   Default subtree.\n");
  (void) printf ("	 -S  Slave to check.\n");
  (void) printf ("	 -t  LDAP operations timeout. Default 30 seconds.\n");
  (void) printf ("	 -T  Total number of operations per thread.	   Default infinite.\n");
  (void) printf ("	 -v  Verbose.\n");
  (void) printf ("	 -V  Very verbose.\n");
  (void) printf ("	 -w  Bind passwd. See -D.\n");
  (void) printf ("	 -W  Wait between two operations.          Default 0 seconds.\n");
  (void) printf ("	 -Z  certfile. Turn on SSL and use certfile as the certificate DB\n");
  (void) printf ("\n");
} /* usage() */



/* End of file */
