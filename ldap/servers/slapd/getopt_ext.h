/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
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



