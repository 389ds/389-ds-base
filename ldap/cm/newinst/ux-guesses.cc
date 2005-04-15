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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* Print guesses of host and domain name as made by the setup SDK. */



/* $RCSfile: ux-guesses.cc,v $ $Revision: 1.4 $ $Date: 2005/04/15 22:40:11 $ $State: Exp $ */
/*
 * $Log: ux-guesses.cc,v $
 * Revision 1.4  2005/04/15 22:40:11  nkinder
 * 155068 - Added license to source files
 *
 * Revision 1.3  2005/03/11 03:46:41  rmeggins
 * This one is mostly strcpy/strcat checking, checking for null strings before strlen, removing some dead code, other odds and ends.
 *
 * Revision 1.2  2005/02/28 23:37:49  nkinder
 * 149951 - Updated source code copyrights
 *
 * Revision 1.1.1.1  2005/01/21 00:40:49  cvsadm
 * Moving NSCP Directory Server from DirectoryBranch to TRUNK, initial drop. (foxworth)
 *
 * Revision 1.1.2.4  2004/07/14 01:39:20  dboreham
 * changes to make newer C++ compilers happy
 *
 * Revision 1.1.1.1  2004/06/03 22:32:45  telackey
 * Initial import Thu Jun  3 15:32:43 PDT 2004
 *
 * Revision 1.1.2.3  2003/09/22 19:38:52  ulfw
 * Update copyright years from 2001 to 2001-2003
 *
 * Revision 1.1.2.2  2001/11/02 23:33:04  richm
 * XXX use new copyright XXX
 *
 * Revision 1.1.2.1  2000/08/07 15:14:28  mwahl
 * rename functions
 *
 *
 */

#include "dialog.h"

extern "C" {
#if defined(__sun) || defined(__hppa) || defined(__osf__) || defined(__linux__) || defined(linux)
#include <netdb.h>
#endif
}


class PrintGuessPreInstall:public DialogManager
{
public:

        PrintGuessPreInstall(int, char **);
        ~PrintGuessPreInstall();

    int init();

    int  start();


  void setParent(void *) { }
  void *parent() const { return 0;}
  void resetLast() { }
  void add (Dialog *) { }
  void addLast(Dialog *) { }
  void clear() { }
  int  cont() { return -1;}


private:
  Bool _reconfig;
  Bool _configured;
};

PrintGuessPreInstall::PrintGuessPreInstall(int argc, char **argv) : _reconfig(False)
{
   setInstallMode(Interactive);
   setInstallType(Typical);
   _configured = False;

   /* getOptions(argc, argv); */

}

PrintGuessPreInstall::~PrintGuessPreInstall()
{
  
}

int PrintGuessPreInstall::init()
{
  return 0;
}

int PrintGuessPreInstall::start()
{
  const char *hno = InstUtil::guessHostname();
  printf("hostname: %s\n",hno ? hno : "<unknown>");
  if (hno) {
#if defined(__sun) || defined(__hppa) || defined(__osf__) || defined(__linux__) || defined(linux)
	static char test_host[BIG_BUF] = {0};
	struct hostent *hp;
	
	PL_strncpyz(test_host,hno,sizeof(test_host));
	hp = gethostbyname(test_host);
	if (hp == NULL) {
	  printf("addressable: no\n");
	} else {
	  printf("addressable: yes\n");
	}
#endif
  }
  const char *dno = InstUtil::guessDomain();
  printf("domain: %s\n",dno ? dno : "<unknown>");
  return 0;
}



int main(int argc,char **argv)
{
  PrintGuessPreInstall program(argc,argv);

  int err = program.init();
  if (!err) {
    err = program.start();
  } 
  return err;
  
}
