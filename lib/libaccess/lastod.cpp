/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*	Source file for the TimeOfDay and DayOfWeek LAS drivers
*/
#include <time.h>

#include <netsite.h>
#include <base/util.h>
#include <base/plist.h>
#include <libaccess/nserror.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/las.h>
#include "aclutil.h"
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclerror.h>

/*	Day of the week LAS driver
 *	Note that everything is case-insensitive.
 *	INPUT
 *	attr		must be the string "dayofweek".
 *	comparator	can only be "=" or "!=".
 *	pattern		any sequence of 3-letter day names.  I.e. sun, mon,
 *			tue, wed, thu, fri, sat.  Comma delimiters can be used
 *			but are not necessary.  E.g. mon,TueweDThuFRISat
 *	OUTPUT
 *	cachable	Will be set to ACL_NOT_CACHABLE.
 *	return code	set to LAS_EVAL_*
 */
int
LASDayOfWeekEval(NSErr_t *errp, char *attr, CmpOp_t comparator, char *pattern, 
		 ACLCachable_t *cachable, void **las_cookie, PList_t subject, 
		 PList_t resource, PList_t auth_info, PList_t global_auth)
{
#ifndef	UTEST
	struct	tm *tm_p, tm;
#endif
	time_t	t;
	char	daystr[5];	/* Current local day in ddd */
	char	lcl_pattern[512];
	char	*compare;

	/*	Sanity checking				*/
	if (strcmp(attr, "dayofweek") != 0) {
	    nserrGenerate(errp, ACLERRINVAL, ACLERR5400, ACL_Program, 2, XP_GetAdminStr(DBT_unexpectedAttributeInDayofweekSN_), attr);
            return LAS_EVAL_INVALID;
	}
	if ((comparator != CMP_OP_EQ) && (comparator != CMP_OP_NE)) {
	    nserrGenerate(errp, ACLERRINVAL, ACLERR5410, ACL_Program, 2, XP_GetAdminStr(DBT_illegalComparatorForDayofweekDN_), comparator_string(comparator));
            return LAS_EVAL_INVALID;
	}
	*cachable = ACL_NOT_CACHABLE;

	/* 	Obtain and format the local time	*/
#ifndef UTEST
	t = time(NULL);
	tm_p = system_localtime(&t, &tm);
	util_strftime(daystr, "%a", tm_p);
#else
	t = (0x1000000);		/* Mon 2120 hr */
	strftime(daystr, 4, "%a", localtime(&t));
#endif
	makelower(daystr);
	strcpy(lcl_pattern, pattern);
	makelower(lcl_pattern);

	/* 	Compare the value to the pattern	*/
	compare	= strstr(lcl_pattern, daystr);

	if ((compare != NULL) && (comparator == CMP_OP_EQ))
		return LAS_EVAL_TRUE;
	if ((compare == NULL) && (comparator == CMP_OP_NE))
		return LAS_EVAL_TRUE;
	return LAS_EVAL_FALSE;
}


/*	Time of day LAS
 *	INPUT
 *	attr		must be "timeofday".
 *	comparator	one of =, !=, >, <, >=, <=
 *	pattern		HHMM military 24-hour clock.  E.g. 0700, 2315.
 *	OUTPUT
 *	cachable 	will be set to ACL_NOT_CACHABLE.
 *	return code	set to LAS_EVAL_*
 */
int
LASTimeOfDayEval(NSErr_t *errp, char *attr, CmpOp_t comparator, char *pattern, 
		 ACLCachable_t *cachable, void **LAS_cookie, PList_t subject, 
		 PList_t resource, PList_t auth_info, PList_t global_auth)
{
#ifndef	UTEST
	struct	tm *tm_p, tm;
#endif
	time_t	t;
	char	timestr[6];	/* Current local time in HHMM */
	char	start[6], end[6];
	int	compare;	/* >0, 0, <0 means that current time is greater, equal to, or less than the pattern */
	char	*dash;
	int	intpattern, inttime, intstart, intend;

	if (strcmp(attr, "timeofday") != 0) {
		nserrGenerate(errp, ACLERRINVAL, ACLERR5600, ACL_Program, 2, XP_GetAdminStr(DBT_unexpectedAttributeInTimeofdaySN_), attr);
		return LAS_EVAL_INVALID;
	}
	*cachable = ACL_NOT_CACHABLE;

	/* 	Obtain and format the local time	*/
#ifndef UTEST
	t = time(NULL);
	tm_p = system_localtime(&t, &tm);
	util_strftime(timestr, "%H%M", tm_p);
#else
	t = (0x1000000);		/* Mon 2120 hr */
	strftime(timestr, 5, "%H%M", localtime(&t));
#endif
#ifdef	DEBUG
	printf ("timestr = %s\n", timestr);
#endif
	inttime = atoi(timestr);


	dash = strchr(pattern, '-');
	if (dash) {
		if (comparator != CMP_OP_EQ  &&  comparator != CMP_OP_NE) {
			nserrGenerate(errp, ACLERRINVAL, ACLERR5610, ACL_Program, 2,  XP_GetAdminStr(DBT_illegalComparatorForTimeOfDayDN_), comparator_string(comparator));
			return LAS_EVAL_INVALID;
		}

		strncpy(start, pattern, dash-pattern);
		start[dash-pattern]='\0';
		intstart = atoi(start);

		strcpy(end, dash+1);
		intend = atoi(end);

		if (intend >= intstart) {
			return(evalComparator(comparator, !(inttime >= intstart  &&  inttime <= intend)));
		} else {	/* range wraps around midnight */
			return(evalComparator(comparator, !(inttime >= intstart  ||  inttime <= intend)));
		}
	}
			

	/* ELSE - Just a single time value given. */

	/* 	Compare the value to the pattern	*/
	intpattern = atoi(pattern);
	compare	= inttime - intpattern;

	/*	Test against what the user wanted done	*/
	return(evalComparator(comparator, compare));
}

void
LASDayOfWeekFlush(void **cookie)
{
	return;
}

void
LASTimeOfDayFlush(void **cookie)
{
	return;
}

