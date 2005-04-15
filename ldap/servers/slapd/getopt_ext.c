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

#include "getopt_ext.h"

char *optarg_ext;
int optind_ext=1;
int optopt_ext;
int opterr_ext;
int optind_last;

static int _getopt_ext_inited = 0;
static int _optind_firstHandled = 0;
static int _optind_firstRecognized = 0;
static int _getopt_ext_done_long = 0;

static
int _getopt_ext_init(int argc)
{
	_getopt_ext_inited = 1;
	/* optind_ext = 1;*/
	optind_last = argc;
	_optind_firstHandled = argc;
	_optind_firstRecognized = argc;
	/* optind = 1;*/
	_getopt_ext_done_long = 0;
	return(0);
}

#if 0
static
int _getopt_ext_done()
{
	_getopt_ext_done_long = 1;
	return(0);
}
#endif

static
int _getopt_ext_find(int argc,
					 char **argv,
					 const struct opt_ext *longOpts)
{
	int i=0;
	struct opt_ext *lopt;
	
	i=0;
	lopt = (struct opt_ext *)longOpts;
	while(lopt->o_string) {
		if(0 != strcmp(argv[optind_ext]+2,lopt->o_string)) {
			i++;
			lopt++;
			continue;
		}
		/*
		 * found it
		 */		
		return(i);
	}
	/* should never come here */
	return(-1);	   
}

static
int _getopt_ext_tailit(int argc,
					   char **argv,
					   int hasArg,
					   int recognized)
{
	char *_optPtr=NULL;
	char *_optArgPtr=NULL;
	int _endIndex=optind_last;
	int _nextDest=optind_ext;
	int _nextSource=optind_ext;

	_optPtr = argv[optind_ext];
	if(hasArg) {
		_nextSource = optind_ext + 2;
		_endIndex = ((recognized)?_optind_firstRecognized:_optind_firstHandled);
		_optArgPtr = argv[optind_ext + 1];
	} else {
		_nextSource = optind_ext + 1;
		_endIndex =  ((recognized)?_optind_firstRecognized:_optind_firstHandled);
		_optArgPtr = NULL;
	}
	
	while(_nextSource < _endIndex) {
		argv[_nextDest] = argv[_nextSource];
		_nextSource++;
		_nextDest++;
	}

	argv[_nextDest] = _optPtr;
	/* argv[_nextDest] = NULL; */
	if(hasArg) {
		argv[_nextDest + 1] = _optArgPtr;
		if(recognized) {
			_optind_firstRecognized -=2;
		}
		_optind_firstHandled -=2;		   
	} else {
		if(recognized) {
			_optind_firstRecognized -=1;
		}
		_optind_firstHandled -=1;
	}
	optind_last = _optind_firstRecognized;
	return(0);
}

/* 
 * First process all the long options (using exact string matches)
 * Then, follow up with regular option processing using good ol' getopt
 *
 * return the same return codes as getopt
 *
 */

int getopt_ext(int argc,
			   char **argv,
			   const char *optstring,
			   const struct opt_ext *longOpts,
			   int *longOptIndex)
{
	int retVal;

	if(!_getopt_ext_inited) {
		_getopt_ext_init(argc);
	}

	if(_optind_firstHandled < optind_ext) {
		/* we are not processing extended options anymore...
		   let getopt handle it.
		   */
		goto _doneWithExtendedOptions;
	}
	
	/* 
	 * if we are here, we are not done with extended options...
	 *
	 */ 
	while(_optind_firstHandled > optind_ext) {
		if(0 != strncmp(argv[optind_ext], "--", 2)) {
			optind_ext++;
			continue;
		}
		/*
		 * possibly an extended option
		 */
		retVal = _getopt_ext_find(argc,argv,longOpts);
		if(-1 == retVal) {
			/*
			 * unrecognized long option...
			 * we will let getopt handle it later...
			 *
			 */
			optind_ext++;
			continue;
		}
		/*
		 * we found an extended option
		 * now find its arg...
		 */
		switch((longOpts+retVal)->o_type) {
		case ArgNone:
		default:
			{
				/* send this option to the end of the arglist */
				_getopt_ext_tailit(argc,argv,0,1);
				optarg_ext = NULL;
				*longOptIndex = retVal;
				return((longOpts+retVal)->o_return);
			}
		case ArgRequired:
			{
				/*
				 * make sure the next arg is a "valid" argument
				 */
				if(((optind_ext + 1) == _optind_firstHandled)) {
					/* 
					 * did not find the argument
					 * let getopt handle it
					 * we will just push it to the end and continue
					 * looking for more extended options
					 *
					 * _getopt_ext_tailit(argc,argv,0,0);
					 */
					optind_ext++;
					fprintf(stderr, "%s: option requires an argument -- %s\n",
							argv[0],(longOpts+retVal)->o_string);
					exit(0);
					continue;
				}
				/* like getopt, we don't care what the arg looks like! */
				/* send this option to the end of the arglist */
				_getopt_ext_tailit(argc,argv,1,1);
				optarg_ext = argv[_optind_firstRecognized + 1]; 
				*longOptIndex = retVal;
				return((longOpts+retVal)->o_return);
			}
		case ArgOptional:
			{
				if(((optind_ext + 1) == optind_last) ||
				   ('-' == argv[optind_ext + 1][0])){
					/* 
					 * did not find the argument
					 *
					 */
					_getopt_ext_tailit(argc,argv,0,1);
					optarg_ext = NULL;
					*longOptIndex = retVal;
					return((longOpts+retVal)->o_return);
				}				
				/* send this option to the end of the arglist */
				_getopt_ext_tailit(argc,argv,1,1);
				optarg_ext = argv[optind_last + 1]; 
				*longOptIndex = retVal;
				return((longOpts+retVal)->o_return);
			}
		}

		/* optind_ext++;*/
	}
	
_doneWithExtendedOptions:
	retVal = getopt(optind_last,argv,optstring);
	optarg_ext = optarg;
	/* optopt_ext = optopt; */
	optind_last = _optind_firstHandled;
	return(retVal);
	
}

