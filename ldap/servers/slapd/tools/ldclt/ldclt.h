#ident "ldclt @(#)ldclt.h	1.66 01/05/04"

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


/*
	FILE :		ldclt.h
	AUTHOR :	Jean-Luc SCHWING
	VERSION :       1.0
	DATE :		03 December 1998
	DESCRIPTION :
			This file is the main include file of the tool named
			"ldclt"
 	LOCAL :		None.
	HISTORY :
---------+--------------+------------------------------------------------------
dd/mm/yy | Author	| Comments
---------+--------------+------------------------------------------------------
03/12/98 | JL Schwing	| Creation
---------+--------------+------------------------------------------------------
10/12/98 | JL Schwing	| 1.2 : Add stats numbers in main_context.
---------+--------------+------------------------------------------------------
10/12/98 | JL Schwing	| 1.3 : Implement asynchronous mode.
---------+--------------+------------------------------------------------------
11/12/98 | JL Schwing	| 1.4 : Implement max errors threshold.
---------+--------------+------------------------------------------------------
14/12/98 | JL Schwing	| 1.5 : Implement "-e close".
---------+--------------+------------------------------------------------------
16/12/98 | JL Schwing	| 1.6 : Implement "-e add" and "-e delete".
			| Add counter nb timeouts (no activity).
---------+--------------+------------------------------------------------------
28/12/98 | JL Schwing	| 1.7 : Add tag asyncHit.
---------+--------------+------------------------------------------------------
29/12/98 | JL Schwing	| 1.8 : Implement -Q.
---------+--------------+------------------------------------------------------
11/01/99 | JL Schwing	| 1.9 : Implement "-e emailPerson".
---------+--------------+------------------------------------------------------
13/01/99 | JL Schwing	| 1.10: Implement "-e string".
---------+--------------+------------------------------------------------------
14/01/99 | JL Schwing	| 1.11: Implement "-s <scope>".
---------+--------------+------------------------------------------------------
18/01/99 | JL Schwing	| 1.12: Implement "-e randombase".
---------+--------------+------------------------------------------------------
18/01/99 | JL Schwing	| 1.13: Implement "-e v2".
---------+--------------+------------------------------------------------------
21/01/99 | JL Schwing	| 1.14: Implement "-e ascii".
---------+--------------+------------------------------------------------------
26/01/99 | JL Schwing	| 1.15: Implement "-e noloop".
---------+--------------+------------------------------------------------------
28/01/99 | JL Schwing	| 1.16: Implement "-T <total>".
---------+--------------+------------------------------------------------------
04/05/99 | JL Schwing	| 1.17: Implement operations list.
---------+--------------+------------------------------------------------------
06/05/99 | JL Schwing	| 1.22: Implement "-e modrdn".
---------+--------------+------------------------------------------------------
19/05/99 | JL Schwing	| 1.25: Implement "-e rename".
---------+--------------+------------------------------------------------------
27/05/99 | JL Schwing	| 1.26 : Add statistics to check threads.
---------+--------------+------------------------------------------------------
28/05/99 | JL Schwing	| 1.27 : Add new option -W (wait).
---------+--------------+------------------------------------------------------
02/06/99 | JL Schwing	| 1.28 : Add flag in main ctx to know if slave was
			|   connected or not.
			| Add counter of operations received in check threads.
---------+--------------+------------------------------------------------------
04/08/00 | JL Schwing	| 1.29: Add stats on nb inactivity per thread.
---------+--------------+------------------------------------------------------
08/08/00 | JL Schwing	| 1.30: Print global statistics every 1000 loops.
---------+--------------+------------------------------------------------------
18/08/00 | JL Schwing	| 1.31: Print global statistics every 15'
			| Print begin and end dates.
---------+--------------+------------------------------------------------------
25/08/00 | JL Schwing	| 1.32: Implement consistent exit status...
---------+--------------+------------------------------------------------------
19/09/00 | JL Schwing	| 1.33: Port on Netscape's libldap. This is realized in
			|   such a way that this library become the default
			|   way so a ifdef for Solaris will be used...
---------+--------------+----------------------------------------------------
11/10/00 | B Kolics     | 1.34: Added 'SSL' to the list of running modes and
         |              |       certfile to main_context structure
---------+--------------+------------------------------------------------------
07/11/00 | JL Schwing	| 1.35: Add handlers for dynamic load of ssl-related
			|   functions.
-----------------------------------------------------------------------------
07/11/00 | JL Schwing	| 1.36: Implement "-e inetOrgPerson".
---------+--------------+------------------------------------------------------
13/11/00 | JL Schwing	| 1.37: Add new options "-e randombaselow and ...high"
---------+--------------+------------------------------------------------------
16/11/00 | JL Schwing	| 1.38: Implement "-e imagesdir=path".
			| lint-cleanup.
---------+--------------+------------------------------------------------------
17/11/00 | JL Schwing	| 1.39: Implement "-e smoothshutdown".
---------+--------------+------------------------------------------------------
21/11/00 | JL Schwing	| 1.40: Implement "-e attreplace=name:mask"
			| Increase max number of threads from 512 to 1000.
---------+--------------+------------------------------------------------------
22/11/00 | JL Schwing	| 1.41: Will now use LD_LIBRARY_PATH to load libssl.
---------+--------------+------------------------------------------------------
24/11/00 | JL Schwing	| 1.41: Added SSL client authentication
---------+--------------+------------------------------------------------------
28/11/00 | JL Schwing	| 1.43: Port on NT 4.
---------+--------------+------------------------------------------------------
15/12/00 | JL Schwing	| 1.44: Add more trace in VERY_VERBOSE mode.
---------+--------------+------------------------------------------------------
15/12/00 | JL Schwing	| 1.45: Implement "-e counteach".
			| Implement "-e withnewparent".
---------+--------------+------------------------------------------------------
18/12/00 | JL Schwing	| 1.46: Add exit status EXIT_INIT and EXIT_RESSOURCE.
---------+--------------+------------------------------------------------------
03/01/01 | JL Schwing	| 1.47: Implement "-e attrsonly=value".
---------+--------------+------------------------------------------------------
05/01/01 | JL Schwing	| 1.48: Implement "-e randombinddn" and associated
			|   "-e randombinddnlow/high"
---------+--------------+------------------------------------------------------
08/01/01 | JL Schwing	| 1.49: Implement "-e scalab01".
---------+--------------+------------------------------------------------------
12/01/01 | JL Schwing	| 1.50: Second set of options for -e scalab01
---------+--------------+------------------------------------------------------
06/03/01 | JL Schwing	| 1.51: Change DEF_ATTRSONLY from 1 to 0
---------+--------------+------------------------------------------------------
08/03/01 | JL Schwing	| 1.52: Change referrals handling.
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.53: Implement "-e commoncounter"
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.54: Implement "-e dontsleeponserverdown".
---------+--------------+------------------------------------------------------
14/03/01 | JL Schwing	| 1.55: Lint cleanup.
---------+--------------+------------------------------------------------------
15/03/01 | JL Schwing	| 1.56: Implement "-e attrlist=name:name:name"
			| Implement "-e randomattrlist=name:name:name"
---------+--------------+------------------------------------------------------
19/03/01 | JL Schwing	| 1.57: Implement "-e object=filename"
			| Implement "-e genldif=filename"
---------+--------------+------------------------------------------------------
21/03/01 | JL Schwing	| 1.58: Implements variables in "-e object=filename"
---------+--------------+------------------------------------------------------
23/03/01 | JL Schwing	| 1.59: Implements data file list support in variants.
			| Implements "-e rdn=value".
---------+--------------+------------------------------------------------------
28/03/01 | JL Schwing	| 1.60: Update options checking for "-e rdn=value".
---------+--------------+------------------------------------------------------
28/03/01 | JL Schwing	| 1.61: Support -e commoncounter with -e rdn/object
			| Increase MAX_ATTRIBS from 20 to 40
			| Remove MAX_ATTRLIST - use MAX_ATTRIBS.
---------+--------------+------------------------------------------------------
02/04/01 | JL Schwing	| 1.62: Bug fix : large files support for -e genldif.
---------+--------------+------------------------------------------------------
05/04/01 | JL Schwing	| 1.63: Implement -e append.
---------+--------------+------------------------------------------------------
11/04/01 | JL Schwing	| 1.64: Implement [INCRFROMFILE<NOLOOP>(myfile)]
---------+--------------+------------------------------------------------------
03/05/01 | JL Schwing	| 1.64: Implement -e randombinddnfromfile=filename.
---------+--------------+------------------------------------------------------
04/05/01 | JL Schwing	| 1.65: Implement -e bindonly.
---------+--------------+------------------------------------------------------
*/

#ifndef LDCLT_H
#define LDCLT_H

/*
 * Misc constant definitions
 */
#define DEF_ATTRSONLY	      0	/* ldap_search() default */	/*JLS 06-03-01*/
#define DEF_GLOBAL_NB	     90 /* Prt glob stats every 15' */	/*JLS 18-08-00*/
#define DEF_INACTIV_MAX	      3 /* Inactivity max nb times */
#define DEF_MAX_ERRORS	   1000 /* Max errors before exit */
#define	DEF_NB_THREADS	     10 /* Nb client threads */
#define DEF_PORT	    389 /* Ldap server port */
#define DEF_SAMPLING	     10 /* Default sampling rate */
#define DEF_TIMEOUT	     30 /* Ldap operations timeout */
#define DEF_PORT_CHECK	  16000 /* Port used for check processing */
#define MAX_ATTRIBS	     40 /* Max number of attributes */	/*JLS 28-03-01*/
#define MAX_DN_LENGTH	   1024 /* Max length for a DN */
#define MAX_ERROR_NB	   0x62 /* Max ldap err number + 1 */
#define MAX_IGN_ERRORS	     20 /* Max errors ignored */
#define MAX_FILTER	    512 /* Max filters length */
#define MAX_THREADS	   1000 /* Max number of threads */	/*JLS 21-11-00*/
#define MAX_SLAVES	     20 /* Max number of slaves */

#define DEF_IMAGES_PATH	"../../data/ldclt/images"
#define DEF_REFERRAL	REFERRAL_ON				/*JLS 08-03-01*/
#define DEF_SCOPE	LDAP_SCOPE_SUBTREE	/* Default for -s */

/*
 * Referral choices...
 */
#define	REFERRAL_OFF		0				/*JLS 08-03-01*/
#define	REFERRAL_ON		1				/*JLS 08-03-01*/
#define	REFERRAL_REBIND		2				/*JLS 08-03-01*/

/*
 * Running modes
 * Will be used as well for main_context and for thread_context
 * Don't forget to update dumpModeValues().
 */
#define	NOTHING		0x00000000 /* Nothing special */
#define VERBOSE		0x00000001 /* -v : verbose */
#define VERY_VERBOSE	0x00000002 /* -V : very verbose */
#define ASYNC		0x00000004 /* -a : asynchonous mode */
#define QUIET		0x00000008 /* -q : quiet */
#define SUPER_QUIET	0x00000010 /* -Q : super quiet */
#define SSL             0x00000020 /* -Z certfile :SSL enabled *//*BK 11-10-00*/
#define CLTAUTH         0x00000040 /* .... */			/* BK 23-11-00*/
/**/
#define RANDOM_ATTRLIST	0x00000080 /* -e randomattrlist*/	/*JLS 15-03-01*/
#define DONT_SLEEP_DOWN	0x00000100 /* -e dontsleeponserverdown*//*JLS 14-03-01*/
#define COMMON_COUNTER	0x00000200 /* -e commoncounter */	/*JLS 14-03-01*/
#define SCALAB01	0x00000400 /* -e scalab01 */		/*JLS 08-01-01*/
#define RANDOM_BINDDN	0x00000800 /* -e randombinddn */	/*JLS 05-01-01*/
#define WITH_NEWPARENT	0x00001000 /* -e withnewparent */	/*JLS 15-12-00*/
#define COUNT_EACH	0x00002000 /* -e counteach */		/*JLS 15-12-00*/
#define ATTR_REPLACE	0x00004000 /* -e attreplace */		/*JLS 21-11-00*/
#define SMOOTHSHUTDOWN	0x00008000 /* -e smoothshutdown */	/*JLS 17-11-00*/
#define	OC_INETORGPRSON	0x00010000 /* -e inetOrgPerson : oc= */	/*JLS 07-11-00*/
#define RENAME_ENTRIES	0x00020000 /* -e rename   : rename entries */
#define NOLOOP		0x00040000 /* -e noloop   : don't loop nb */
#define ASCII_7BITS	0x00080000 /* -e ascii    : ascii 7bits */
#define LDAP_V2		0x00100000 /* -e v2       : ldap v2 */
#define RANDOM_BASE	0x00200000 /* -e randombase : string mode */
#define STRING		0x00400000 /* -e string   : string mode */
#define OC_EMAILPERSON	0x00800000 /* -e emailPerson : oc = person */
#define DELETE_ENTRIES	0x01000000 /* -e delete   : delete */
#define OC_PERSON	0x02000000 /* -e person   : oc = person */
#define ADD_ENTRIES	0x04000000 /* -e add      : add entries */
#define INCREMENTAL	0x08000000 /* -e incr     : incremental */
#define CLOSE_FD	0x10000000 /* -e close    : close fd */
#define RANDOM		0x20000000 /* -e random   : rnd values */
#define BIND_EACH_OPER	0x40000000 /* -e bindeach : bnd each op */
#define EXACT_SEARCH	0x80000000 /* -e esearch  : exact srch */

#define M2_OBJECT	0x00000001 /* -e object */		/*JLS 19-03-01*/
#define M2_GENLDIF	0x00000002 /* -e genldif */		/*JLS 19-03-01*/
#define M2_RDN_VALUE	0x00000004 /* -e rdn */			/*JLS 23-03-01*/
#define M2_APPEND	0x00000008 /* -e append */		/*JLS 05-04-01*/
#define M2_RNDBINDFILE	0x00000010 /* -e randombinddnfromfile *//*JLS 03-05-01*/
#define M2_BINDONLY	0x00000020 /* -e bindonly */		/*JLS 04-05-01*/
#define M2_SASLAUTH     0x00000040 /* -o : SASL authentication */
#define M2_RANDOM_SASLAUTHID     0x00000080 /* -e randomauthid */
#define M2_ABANDON     0x00000100 /* -e abandon */
#define M2_DEREF     0x00000200 /* -e deref */
#define M2_ATTR_REPLACE_FILE	 0x00000400 /* -e attreplacefile */

/*
 * Combinatory defines
 *  - NEED_FILTER	: filter required
 *  - NEED_RANGE	: -r and -R required
 *  - NEED_RND_INCR	: need entry generator
 *  - VALID_OPERS	: valid operations
 */
#define NEED_FILTER	(ADD_ENTRIES|DELETE_ENTRIES|EXACT_SEARCH|RENAME_ENTRIES|ATTR_REPLACE|SCALAB01)
#define M2_NEED_FILTER	(M2_ABANDON|M2_ATTR_REPLACE_FILE|M2_DEREF)
#define NEED_RANGE	(INCREMENTAL|RANDOM)
#define NEED_RND_INCR	(ADD_ENTRIES|DELETE_ENTRIES|RENAME_ENTRIES)
#define VALID_OPERS	(ADD_ENTRIES|DELETE_ENTRIES|EXACT_SEARCH|RENAME_ENTRIES|ATTR_REPLACE|SCALAB01)
#define M2_VALID_OPERS	(M2_GENLDIF|M2_BINDONLY|M2_ABANDON|M2_ATTR_REPLACE_FILE|M2_DEREF)
#define NEED_CLASSES	(ADD_ENTRIES)
#define THE_CLASSES	(OC_PERSON|OC_EMAILPERSON|OC_INETORGPRSON)

/*
 * The threads status - check thread_context.status
 */
#define FREE		-1 /* Slot is free */
#define CREATED		 0 /* Just created */
#define INITIATED	 1 /* Initiated */
#define RUNNING		 2 /* Doing it's job */
#define DEAD		 9 /* Thread is dead */
#define MUST_SHUTDOWN	10 /* Monitor command this */		/*JLS 17-11-00*/

/*
 * Exit status
 * The biggest is the number, the higher priority is.
 * Cf the end of monitorThem().
 */
#define EXIT_OK		 0 /* No problem during execution */	/*JLS 25-08-00*/
#define EXIT_PARAMS	 2 /* Error in parameters */		/*JLS 25-08-00*/
#define EXIT_MAX_ERRORS	 3 /* Max errors reached */		/*JLS 25-08-00*/
#define EXIT_NOBIND	 4 /* Cannot bind */			/*JLS 25-08-00*/
#define EXIT_LOADSSL	 5 /* Cannot load libssl */		/*JLS 07-11-00*/
#define EXIT_MUTEX	 6 /* Mutex error */			/*JLS 17-11-00*/
#define EXIT_INIT	 7 /* Initialization error */		/*JLS 18-12-00*/
#define EXIT_RESSOURCE	 8 /* Ressource limitation */		/*JLS 18-12-00*/
#define EXIT_OTHER	99 /* Other kind of error */		/*JLS 25-08-00*/

/*
 * Some constants from Sun's ldap.h are not provided by
 * Netscape implementation...
 */
#ifdef SOLARIS_LIBLDAP						/*JLS 19-09-00*/
#define WORKAROUND_4197228	1				/*JLS 19-09-00*/
#else								/*JLS 19-09-00*/
#ifndef LDAP_REQ_BIND
#define	LDAP_REQ_BIND			0x60			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_UNBIND
#define	LDAP_REQ_UNBIND			0x42			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_SEARCH
#define	LDAP_REQ_SEARCH			0x63			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_MODIFY
#define	LDAP_REQ_MODIFY			0x66			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_ADD
#define	LDAP_REQ_ADD			0x68			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_DELETE
#define	LDAP_REQ_DELETE			0x4a			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_MODRDN
#define	LDAP_REQ_MODRDN			0x6c			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_COMPARE
#define	LDAP_REQ_COMPARE		0x6e			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_ABANDON
#define	LDAP_REQ_ABANDON		0x50			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_EXTENDED
#define	LDAP_REQ_EXTENDED		0x77			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_UNBIND_30
#define	LDAP_REQ_UNBIND_30		0x62			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_DELETE_30
#define	LDAP_REQ_DELETE_30		0x6a			/*JLS 19-09-00*/
#endif
#ifndef LDAP_REQ_ABANDON_30
#define	LDAP_REQ_ABANDON_30		0x70			/*JLS 19-09-00*/
#endif								/*JLS 19-09-00*/
#endif

#ifndef LBER_SOCKET
#ifdef LBER_SOCKET_T
#define LBER_SOCKET LBER_SOCKET_T
#else
#define LBER_SOCKET int
#endif
#endif

/*
 * This structure is the internal representation of an image
 */
typedef struct image {
	char	*name;
	int	 length;
	char	*data;
} image;

/*
 * Internal representation of a data list file
 */
typedef struct data_list_file {					/*JLS 23-03-01*/
	char		 *fname;	/* File name */
	char		**str;		/* Strings array */
	int		  strNb;	/* Nb of strings */
	struct data_list_file	 *next;	/* Next file */
} data_list_file;

/*
 * This structure is the internal representation of an LDAP attribute
 */
typedef struct {
	char	*type;		/* e.g. "objectclass" or "cn" */
	int	 length;	/* Length of the value */
	char	*value;		/* The attribute value */
	int	 dontFree;	/* Don't free the value */
} attribute;

/*
 * This structure is used to memorize an operation successfully
 * performed by the tool in order to be checked later on.
 * The operation type is a LDAP constant LDAP_REQ_ADD, etc...
 * ATTENTION: don't forget to maintain in sync with the struct thoper below.
 */
typedef struct oper {
	int		 type;		/* Operation type */
	char		*dn;		/* Target's DN */
	attribute	 attribs[MAX_ATTRIBS];	/* Attributes to check */
					/* attribs[i].type == NULL marks the */
					/* end of the attributes */
	char		*newRdn;	/* For rename operation */
	char		*newParent;	/* For rename operation */
	int		 skipped;	/* Thread that skipped it */
	ldclt_mutex_t	 skipped_mutex;	/* Protect skipped */	/*JLS 28-11-00*/
	struct oper	*next;		/* Next operation */
} oper;

/*
 * Same as before, but this is a per thread "lost" operation list
 */
typedef struct _simpl_op {
	int		  type;
	int		  first;  	/* Keeps in order replies */
	char		 *dn;
	attribute	  attribs[MAX_ATTRIBS];
	char		 *newRdn;
	char		 *newParent;
	struct _simpl_op *next;
} thoper;

/*
 * Versatile object attribute's field
 * - If ldclt should use a common counter, then this counter will
 *   be in the mctx structure and will be found by the commonField
 *   pointer.
 */
#define HOW_CONSTANT	      0	/* Constant value */
#define HOW_INCR_FROM_FILE    1 /* Increment string from file *//*JLS 11-04-01*/
#define HOW_INCR_FROM_FILE_NL 2 /* Incr string file noloop*/	/*JLS 11-04-01*/
#define HOW_INCR_NB	      3 /* Increment number */		/*JLS 23-03-01*/
#define HOW_INCR_NB_NOLOOP    4 /* Increment number no loop */	/*JLS 23-03-01*/
#define HOW_RND_FROM_FILE     5 /* Random string from file */	/*JLS 23-03-01*/
#define HOW_RND_NUMBER	      6	/* Random number */
#define HOW_RND_STRING	      7	/* Random string */
#define HOW_VARIABLE	      8	/* Retrieve variable value */	/*JLS 21-03-01*/
typedef struct vers_field {					/*JLS 21-03-01*/
	int		 how;		/* How to build this field */
	int		 cnt;		/* Counter */		/*JLS 23-03-01*/
	ldclt_mutex_t	 cnt_mutex;	/* Protect cnt */	/*JLS 28-03-01*/
	struct vers_field *commonField;	/* Common field */	/*JLS 28-03-01*/
	char		*cst;		/* Constant field */
	data_list_file	*dlf;		/* Data list file */	/*JLS 23-03-01*/
	int		 high;		/* High value */
	int		 low;		/* Low value */
	int		 nb;		/* Number of items */
	int		 var;		/* Variable number */
	struct vers_field *next;	/* Next field */
} vers_field;

/*
 * Versatile object's attribute
 */
typedef struct vers_attribute {					/*JLS 19-03-01*/
	char		*buf;	/* Store the generated value */	/*JLS 21-03-01*/
	char		*name;	/* Attribute name */
	char		*src;	/* Source line */
	vers_field	*field;	/* First field */		/*JLS 21-03-01*/
} vers_attribute;

/*
 * This structure contains the definitions related to the versatile
 * object classes managed by ldclt.
 * The field 'rdn' of vers_object is a trick we will use to be
 * able to support the same random mechanism for the entry's rdn
 * generation than for the attributes themselves.
 */
#define VAR_MIN		'A'					/*JLS 21-03-01*/
#define VAR_MAX		'H'					/*JLS 21-03-01*/
typedef struct vers_object {					/*JLS 19-03-01*/
	vers_attribute	 attribs[MAX_ATTRIBS];
	int		 attribsNb;
	vers_attribute	*rdn;		/* Object's rdn */	/*JLS 23-03-01*/
	char		*rdnName;	/* Attrib. name */	/*JLS 23-03-01*/
	char		*var[VAR_MAX-VAR_MIN];			/*JLS 21-03-01*/
	char		*fname;		/* Object definition */
} vers_object;

/*
 * This structure contain the *process* context, used only by the
 * main thread(s).
 * Another dedicated structure is used by each test thread.
 */
typedef struct main_context {
	int		 asyncMin;	/* Min pend for read */
	int		 asyncMax;	/* Max async pending */
	char		*attrlist[MAX_ATTRIBS];			/*JLS 15-03-01*/
	int		 attrlistNb;	/* Nb attrib in list */	/*JLS 15-03-01*/
	char		*attRef;	/* Name of referenced attribute name */
	char		*attRefDef;	/* Name of dereferenced attribute name*/
	char		*attrpl;	/* Attrib argument */	/*JLS 21-11-00*/
	char		*attrplFile;	/* Attrib file to get value from */
	char		*attrplFileContent;	/* Attrib file content */
	int		attrplFileSize;		/* Attrib file size*/
	char		*attrplHead;	/* Attrib value head */	/*JLS 21-11-00*/
	char		*attrplName;	/* Attrib name */	/*JLS 21-11-00*/
	int		 attrplNbDigit;	/* Attrib nb digits */	/*JLS 21-11-00*/
	char		*attrplTail;	/* Attrib value tail */	/*JLS 21-11-00*/
	int		 attrsonly;	/* search() param. */	/*JLS 03-01-01*/
	char		*baseDN;	/* Base DN to use */
	int		 baseDNLow;	/* Base DN's low val */	/*JLS 13-11-00*/
	int		 baseDNHigh;	/* Base DN's high val *//*JLS 13-11-00*/
	int		 baseDNNbDigit;	/* Base DN's nb of digits */
	char		*baseDNHead;	/* Base DN's head string */
	char		*baseDNTail;	/* Base DN's tail string */
	char		*bindDN;	/* Bind DN */
	int		 bindDNLow;	/* Bind DN's low val */	/*JLS 05-01-01*/
	int		 bindDNHigh;	/* Bind DN's high val *//*JLS 05-01-01*/
	int		 bindDNNbDigit;	/* Bind DN's ndigits */	/*JLS 05-01-01*/
	char		*bindDNHead;	/* Bind DN's head */	/*JLS 05-01-01*/
	char		*bindDNTail;	/* Bind DN's tail */	/*JLS 05-01-01*/
        char            *certfile;      /* certificate file */ /* BK 11-10-00 */
        char            *cltcertname;   /* client cert name */ /* BK 23 11-00 */
	data_list_file	*dlf;		/* Data list files */	/*JLS 23-03-01*/
	int		 errors[MAX_ERROR_NB]; /* Err stats */
	int		 errorsBad;	/* Bad errors */
	ldclt_mutex_t	 errors_mutex;	/* Protect errors */	/*JLS 28-11-00*/
	int		 exitStatus;	/* Exit status */	/*JLS 25-08-00*/
	char		*filter;	/* Filter for req. */
	char		*genldifName;	/* Where to put ldif */	/*JLS 19-03-01*/
	int		 genldifFile;	/* Where to put ldif */	/*JLS 19-03-01*/
	char		*hostname;	/* Host to connect */
	int		 globStatsCnt;	/* Global stats loop */ /*JLS 08-08-00*/
	int		 ignErr[MAX_IGN_ERRORS]; /* Err ignor */
	int		 ignErrNb;	/* Nb err ignored */
	image		*images;	/* The images */
	char		*imagesDir;	/* Where are images */	/*JLS 16-11-00*/
	int		 imagesNb;	/* Nb of images */
	int		 imagesLast;	/* Last selected image */
	ldclt_mutex_t	 imagesLast_mutex; /* Protect imagesLast */
	int		 inactivMax;	/* Allowed inactivity */
        char            *keydbfile;     /* key DB file */       /* BK 23-11-00*/
        char            *keydbpin;      /* key DB password */   /* BK 23-11-00*/
	int		 lastVal;	/* To build filters */	/*JLS 14-03-01*/
	ldclt_mutex_t	 lastVal_mutex;	/* Protect lastVal */	/*JLS 14-03-01*/
	int		 ldapauth;	/* Used to indicate auth type */
	int		 maxErrors;	/* Max allowed errors */
	unsigned int	 mode;		/* Running mode */
	unsigned int	 mod2;		/* Running mode - 2 */	/*JLS 19-03-01*/
	int		 nbNoActivity;	/* Nb times no activ. */
	int		 nbSamples;	/* Samples to get */
	int		 nbThreads;	/* Nb of client */
	vers_object	 object;	/* Object to generate *//*JLS 19-03-01*/
	oper		*opListTail;	/* Tail of operation list */
	ldclt_mutex_t	 opListTail_mutex; /* Protect opListTail */
	char		*passwd;	/* Bind passwd */
	int		 passwdNbDigit;	/* Passwd's ndigits */	/*JLS 05-01-01*/
	char		*passwdHead;	/* Passwd's head */	/*JLS 05-01-01*/
	char		*passwdTail;	/* Passwd's tail */	/*JLS 05-01-01*/
	int		 pid;		/* Process ID */
	int		 port;		/* Port to use */
	int		 randomLow;	/* Rnd's low value */
	int		 randomHigh;	/* Rnd's high val */
	int		 randomNbDigit;	/* Rnd's nb of digits */
	char		*randomHead;	/* Rnd's head string */
	char		*randomTail;	/* Rnd's tail string */
	data_list_file	*rndBindDlf;	/* Rnd bind file data *//*JLS 03-05-01*/
	char		*rndBindFname;	/* Rnd bind file name *//*JLS 03-05-01*/
	int		 referral;	/* Referral followed */	/*JLS 08-03-01*/
	int		 sampling;	/* Sampling frequency */
	char		*sasl_authid;
	int		 sasl_authid_low;	/* authid's low val */
	int		 sasl_authid_high;	/* authid's high val */
	int		 sasl_authid_nbdigit;	/* authid's ndigits */
	char		*sasl_authid_head;	/* authid's head */
	char		*sasl_authid_tail;	/* authid's tail */
	unsigned	 sasl_flags;
	char		*sasl_mech;
	char		*sasl_realm;
	char		*sasl_secprops;
	char		*sasl_username;
	int		 scope;		/* Searches scope */
	int		 slaveConn;	/* Slave has connected */
	char		*slaves[MAX_SLAVES]; /* Slaves list */
	int		 slavesNb;	/* Number of slaves */
	int		 timeout;	/* LDAP op. t.o. */
	struct timeval	 timeval;	/* Timeval structure */
	struct timeval	 timevalZero;	/* Timeout of zero */
	int		 totalReq;	/* Total requested */
	int		 totNbOpers;	/* Total opers number */
	int		 totNbSamples;	/* Total samples nb */
	int		 waitSec;	/* Wait between two operations */
} main_context;


/*
 * This structure is aimed to ease the managing of asynchronous
 * operations, keeping in memory the msgid returned by the library and
 * a free string meaning something for the user.
 * It is targetted that this string is something like a DN, and is
 * locally managed by the list functions.
 */
typedef struct msgid_cell {
	LDAPMod			**attribs;		/* Attributes */
	char			  dn[MAX_DN_LENGTH];	/* entry's dn */
	int			  msgid;		/* msg id */
	char			  str[MAX_DN_LENGTH];	/* free str */
	struct msgid_cell	 *next;			/* next cell */
} msgid_cell;

/*
 * This structure contain the context associated with each thread.
 * It is targetted to be initiated by the main thread, and maintained
 * by each thread.
 */
typedef struct thread_context {
	int		 active;	/* thread is active */
	int		 asyncHit;	/* async max hit */
	char		*attrlist[MAX_ATTRIBS];			/*JLS 15-03-01*/
	int		 binded;	/* thread is binded */
	int		 exitStatus;	/* Exit status */	/*JLS 25-08-00*/
	int		 fd;		/* fd to the server */
	int		 lastVal;	/* To build filters */
	LDAP		*ldapCtx;	/* LDAP context */
	unsigned int	 mode;		/* Running mode */
	int		 nbInactRow;	/* Nb inactive in row *//*JLS 04-08-00*/
	int		 nbInactTot;	/* Nb inactive total */	/*JLS 04-08-00*/
	int		 nbOpers;	/* Nb of operations */
	ldclt_mutex_t	 nbOpers_mutex;	/* Protect nbOpers */	/*JLS 28-11-00*/
	vers_object	*object;	/* Template */		/*JLS 21-03-01*/
	int		 pendingNb;	/* Pending opers */
	int		 status;	/* Status */
	ldclt_mutex_t	 status_mutex;	/* Protect status */	/*JLS 28-11-00*/
	ldclt_tid	 tid;		/* Thread's id */	/*JLS 28-11-00*/
	char		 thrdId[8];	/* This thread ident */	/*JLS 08-01-01*/
	int		 thrdNum;	/* This thread number */
	int		 totOpers;	/* Total nb operations */
	int		 totalReq;	/* Total nb operations requested */
	/*
	 * Now some convenient buffers ;-)
	 */
	char		 buf2 [MAX_FILTER];
	char		*bufObject1;				/*JLS 19-03-01*/
	char		*bufAttrpl;	/* Attribute replace */	/*JLS 21-11-00*/
	char		*bufBaseDN;	/* Base DN to use */
	char		*bufBindDN;	/* Bind DN to use */	/*JLS 05-01-01*/
	char		*bufFilter;	/* Filter to use */
	char		*bufPasswd;	/* Bind passwd to use *//*JLS 05-01-01*/
	char		*bufSaslAuthid;	/* Sasl Authid to use */
	/*
 	 * Note about matcheddnp management. This pointer is managed by the 
	 * function dnFromMessage() that need it to free or remember the string
	 * returned by the library. DO NOT manage this field another way.
	 */
	char		*matcheddnp;	/* See above */		/*JLS 15-12-00*/
	int		 startAttrpl;	/* Insert random here *//*JLS 21-11-00*/
	int		 startBaseDN;	/* Insert random here */
	int		 startBindDN;	/* Insert random here *//*JLS 05-01-01*/
	int		 startPasswd;	/* Insert random here *//*JLS 05-01-01*/
	int		 startRandom;	/* Insert random here */
	int		 startSaslAuthid;	/* Insert random here */
	msgid_cell	*firstMsgId;	/* pending messages */
	msgid_cell	*lastMsgId;	/* last one */
} thread_context;

/*
 * This structure gather the information used by a check thread
 */
typedef struct check_context {
	oper		*headListOp;	/* Head list of operation */
	thoper		*dcOper;	/* Double check operation list */
	char		*slaveName;	/* Name of the slave */
	int		 sockfd;	/* Socket fd after accept() */
	int		 status;	/* Status */
	int		 thrdNum;	/* Thread number */
	int		 calls;		/* Number of timeouts */
	ldclt_tid	 tid;		/* Thread's id */	/*JLS 28-11-00*/
	int		 nbEarly;	/* err = Early */
	int		 nbLate;	/* err = Late replica */
	int		 nbLostOp;	/* err = Lost op */
	int		 nbNotOnList;	/* err = Not on list */
	int		 nbOpRecv;	/* Nb operations received */
	int		 nbRepFail32;	/* err = Replica failed err=32 */
	int		 nbRepFail68;	/* err = Replica failed err=68 */
	int		 nbRepFailX;	/* err = Replica failed err=X */
	int		 nbStillOnQ;	/* err = still on Queue */
} check_context;



/*
 * Extern declarations of global variables.
 */
extern main_context	 mctx;		/* Main context */
extern thread_context	 tctx[];	/* Thread contextes */
extern check_context	 cctx[];	/* Check thread contextes */


/*
 * Extern functions prototypes (for exported functions)
 */
	/* From ldclt.c */
extern void	 ldcltExit (int status);			/*JLS 18-08-00*/
extern int	 printGlobalStatistics (void);			/*JLS 16-11-00*/
	/* From ldcltU.c */
extern void	 usage (void);
	/* From threadMain.c */
extern int	 addErrorStat (int err);
extern int	 getThreadStatus (thread_context *tttctx,	/*JLS 17-11-00*/
				int *status);			/*JLS 17-11-00*/
extern int	 ignoreError  (int err);
extern int	 incrementCommonCounter (thread_context *tttctx);   /*14-03-01*/
extern int	 incrementCommonCounterObject (			/*JLS 28-03-01*/
				thread_context *tttctx, 	/*JLS 28-03-01*/
				vers_field *field);		/*JLS 28-03-01*/
extern int	 incrementNbOpers (thread_context *tttctx);
extern int	 msgIdAdd     (thread_context *tttctx, int msgid, char *str,
				char *dn, LDAPMod **attribs);
extern LDAPMod **msgIdAttribs (thread_context *tttctx, int msgid);
extern int	 msgIdDel     (thread_context *tttctx, int msgid, int freeAttr);
extern char	*msgIdDN      (thread_context *tttctx, int msgid);
extern char	*msgIdStr     (thread_context *tttctx, int msgid);
extern int	 randomString (thread_context *tttctx, int nbDigits);
extern char    **selectRandomAttrList (thread_context *tttctx);	/*JLS 15-03-01*/
extern int	 setThreadStatus (thread_context *tttctx,	/*JLS 17-11-00*/
				int status);			/*JLS 17-11-00*/
extern void	*threadMain   (void *);
	/* From ldapfct.c */
extern int	 connectToServer (thread_context *tttctx);	/*JLS 14-03-01*/
extern char	*dnFromMessage (thread_context *tttctx, LDAPMessage *res);
extern int	 doAddEntry    (thread_context *tttctx);
extern int	 doAttrReplace (thread_context *tttctx);	/*JLS 21-11-00*/
extern int	 doAttrFileReplace (thread_context	*tttctx);

extern int	 doBindOnly    (thread_context *tttctx);	/*JLS 04-05-01*/
extern int	 doDeleteEntry (thread_context *tttctx);
extern int	 doExactSearch (thread_context *tttctx);
extern int   doAbandon (thread_context *tttctx);
extern int	 doGenldif     (thread_context *tttctx);	/*JLS 19-03-01*/
extern int	 doRename      (thread_context *tttctx);
extern int	 freeAttrib (LDAPMod **attrs);
extern void	 ldclt_flush_genldif (void);			/*JLS 02-04-01*/
extern char	*my_ldap_err2string (int err);
extern char    **strList1 (char *str1);				/*JLS 08-01-01*/
	/* From data.c */
extern data_list_file	*dataListFile (char *fname);		/*JLS 23-03-01*/
extern int	 getImage   (LDAPMod *attribute);
extern int	 loadImages (char *dirpath);
	/* From workarounds.c */
extern int	 getFdFromLdapSession (LDAP *ld, int *fd);
	/* From opCheck.c */
extern int	 opAdd (thread_context *tttctx, int type, char *dn,
			LDAPMod **attribs, char *newRdn, char *newParent);
extern void	*opCheckMain (void *);
extern void	*opCheckLoop (void *);
extern int	 opNext (check_context *ctctx, oper **op);
extern int	 opRead (check_context *ctctx, int num, oper **op);
	/* From parser.c */
extern int	 parseAttribValue (char *fname, 		/*JLS 23-03-01*/
			vers_object *obj, char *line, 		/*JLS 23-03-01*/
			vers_attribute *attrib);		/*JLS 23-03-01*/
extern int	 readObject (vers_object *obj);			/*JLS 19-03-01*/


#endif /* LDCLT_H */

/* End of file */
