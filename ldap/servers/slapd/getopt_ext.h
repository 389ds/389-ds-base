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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/*
 *  getopt_ext.h - long option names
 *
 *
 *
 */

#ifndef _GETOPT_EXT_H
#define _GETOPT_EXT_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined( _WIN32 )
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ldap.h>
#include "ntslapdmessages.h"
#include "proto-ntutil.h"
#endif

#ifdef LINUX
#include <getopt.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*
 * getopt_ext() is a rudimentary extension to getopt() to facilitate
 * handling of long (wordier) option names.
 *
 * A long option is indicated by placing a "--" in front of the option
 * name.
 *
 * Like getopt(), getopt_ext() also returns a single letter (actually an int)
 * when an option is recognized. Therefore, the loop for processing long
 * options and single letter options can be combined (see example in
 * slapd/main.c)
 *
 * getopt_ext() first processes all the long options it can find. Currently,
 * it does a "strcmp" to check for the validity of the option name (i.e.,
 * the option name has to match exactly). 
 *
 * Once all the long options are handled, getopt_ext() uses getopt() to
 * process the remaining options.
 *
 * getopt_ext() rearranges "argv" when it finds long options that it
 * recognizes. The recognized options (and their parameters) are pushed
 * to the end. 
 *
 * Single letter options are specified similar to getopt()
 * Long options are specified using a list of "struct opt_ext". Each long
 * option consists of string that identifies the option, a type that specifies
 * if the option requires an argument and the single letter returned by
 * getopt_ext() when the option is encountered. For example,
 * {"verbose",ArgNone,'v'} specifies a long option (--verbose) that requires
 * no arguments and for which, getopt_ext() returns a 'v' as the return value.
 * {"instancedir",ArgRequired,'D'} specifies a long option (--instancedir dir)
 * that requires an argument.
 *
 *
 */


extern char *optarg_ext;
extern int optind_ext;
extern int optopt_ext;
extern int opterr_ext;
extern int optind_last;

extern int optind, opterr, optopt;
extern char *optarg;

typedef enum {
	ArgNone,
	ArgRequired,
	ArgOptional
} GetOptExtArgType;

struct opt_ext {
	const char *o_string;
	const GetOptExtArgType o_type;
	const char o_return;
};

int getopt_ext(int argc,
			   char **argv,
			   const char *optstring,
			   const struct opt_ext *longOpts,
			   int *longOptIndex);

#ifdef __cplusplus
}
#endif

#endif



