/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/* Print guesses of host and domain name as made by the setup SDK. */



/* $RCSfile: ux-guesses.cc,v $ $Revision: 1.1 $ $Date: 2005/01/21 00:40:49 $ $State: Exp $ */
/*
 * $Log: ux-guesses.cc,v $
 * Revision 1.1  2005/01/21 00:40:49  cvsadm
 * Initial revision
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
	
	strcpy(test_host,hno);
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
