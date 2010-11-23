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
 *  csn.c - CSN
 */

#include <string.h>
#include "slap.h"
#include <prcountr.h>

#define _CSN_TSTAMP_STRSIZE_STR "8"
#define _CSN_SEQNUM_STRSIZE_STR "4"
#define _CSN_REPLID_STRSIZE_STR "4"
#define _CSN_SUBSEQNUM_STRSIZE_STR "4"

#define _CSN_TSTAMP_SCANSTR "%"_CSN_TSTAMP_STRSIZE_STR"lx"
#define _CSN_SEQNUM_SCANSTR "%"_CSN_SEQNUM_STRSIZE_STR"hx"
#define _CSN_REPLID_SCANSTR "%"_CSN_REPLID_STRSIZE_STR"hx"
#define _CSN_SUBSEQNUM_SCANSTR "%"_CSN_SUBSEQNUM_STRSIZE_STR"hx" 

#define _CSN_TSORDER_SPRINTSTR "%08x%04x%04x%04x"

#define _CSN_TSORDER_TSTAMP_OFFSET 0
#define _CSN_TSORDER_SEQNUM_OFFSET 8
#define _CSN_TSORDER_REPLID_OFFSET 12
#define _CSN_TSORDER_SUBSEQNUM_OFFSET 16

static PRBool _csnIsValidString(const char *csnStr);

/*
 * Debugging counters.
 */
static int counters_created= 0;
PR_DEFINE_COUNTER(slapi_csn_counter_created);
PR_DEFINE_COUNTER(slapi_csn_counter_deleted);
PR_DEFINE_COUNTER(slapi_csn_counter_exist);

/*
 * **************************************************************************
 * CSN Functions
 * **************************************************************************
 */

static void
csn_create_counters()
{
	PR_CREATE_COUNTER(slapi_csn_counter_created,"Slapi_CSN","created","");
	PR_CREATE_COUNTER(slapi_csn_counter_deleted,"Slapi_CSN","deleted","");
	PR_CREATE_COUNTER(slapi_csn_counter_exist,"Slapi_CSN","exist","");
	counters_created= 1;
}

CSN *csn_new()
{
	if(!counters_created)
	{
		csn_create_counters();
	}
	PR_INCREMENT_COUNTER(slapi_csn_counter_created);
	PR_INCREMENT_COUNTER(slapi_csn_counter_exist);
	return (CSN*)slapi_ch_calloc(sizeof(CSN),1);
}

CSN *csn_new_by_string(const char *s)
{
	CSN *newcsn= NULL;
	if(s!=NULL)
	{
	    if(_csnIsValidString(s))
		{
			newcsn= csn_new();
			csn_init_by_string(newcsn,s);
		}
	}
	return newcsn;
}

void csn_init(CSN *csn)
{
	if(csn!=NULL)
	{
		memset(csn,0,sizeof(CSN));
	}
}

void csn_init_by_csn(CSN *csn1,const CSN *csn2)
{
	if(csn2!=NULL)
	{
		memcpy(csn1,csn2,sizeof(CSN));
	}
	else
	{
		csn_init(csn1);
	}
}

void csn_init_by_string(CSN *csn, const char *s)
{
	time_t csnTime= 0;
	PRUint16 csnSeqNum= 0;
	ReplicaId rid= 0;
	PRUint16 csnSubSeqNum= 0;

    if(_csnIsValidString(s))
    {
		/* JCM - char2hex faster */
		sscanf((s+_CSN_TSORDER_TSTAMP_OFFSET), _CSN_TSTAMP_SCANSTR, &csnTime); /* JCM - scanf is very slow */
		sscanf((s+_CSN_TSORDER_SEQNUM_OFFSET), _CSN_SEQNUM_SCANSTR, &csnSeqNum);/* JCM - scanf is very slow */
		sscanf((s+_CSN_TSORDER_REPLID_OFFSET), _CSN_REPLID_SCANSTR, &rid);/* JCM - scanf is very slow */
	    sscanf((s+_CSN_TSORDER_SUBSEQNUM_OFFSET), _CSN_SUBSEQNUM_SCANSTR, &csnSubSeqNum);/* JCM - scanf is very slow */
	    csn->tstamp= csnTime;
	    csn->seqnum= csnSeqNum;
	    csn->rid= rid;
		csn->subseqnum= csnSubSeqNum;
	}
}

CSN *csn_dup(const CSN *csn)
{
	CSN *newcsn= NULL;
    if(csn!=NULL)
    {
		newcsn= csn_new();
		csn_init_by_csn(newcsn,csn);
    }
	return newcsn;
}

void csn_done(CSN *csn)
{
}

void csn_free(CSN **csn)
{
	if(csn!=NULL && *csn!=NULL)
	{
		if(!counters_created)
		{
			csn_create_counters();
		}
		PR_INCREMENT_COUNTER(slapi_csn_counter_deleted);
		PR_DECREMENT_COUNTER(slapi_csn_counter_exist);
	    slapi_ch_free((void **)csn);
	}
    return;
}

void csn_set_replicaid(CSN *csn, ReplicaId rid)
{
    csn->rid= rid;
}

void csn_set_time(CSN *csn, time_t csntime)
{
    csn->tstamp= csntime;
}

void csn_set_seqnum(CSN *csn, PRUint16 seqnum)
{
    csn->seqnum= seqnum;
}

ReplicaId csn_get_replicaid(const CSN *csn)
{
    return csn->rid;
}

PRUint16 csn_get_seqnum(const CSN *csn)
{
    return csn->seqnum;
}

PRUint16 csn_get_subseqnum(const CSN *csn)
{
    return csn->subseqnum;
}

time_t csn_get_time(const CSN *csn)
{
	if(csn==NULL)
	{
		return 0;
	}
	else
	{
		return csn->tstamp;
	}
}


/*
 * WARNING: ss must point to memory at least CSN_STRSIZE bytes long, 
 * WARNING: or be NULL, which means this function will allocate the 
 * WARNING: memory, which must be free'd by the caller.
 */
char *
csn_as_string(const CSN *csn, PRBool replicaIdOrder, char *ss)
{
	char *s= ss;
	if(s==NULL)
	{
		s= slapi_ch_malloc(CSN_STRSIZE);
	}
	if(csn==NULL)
	{
		s[0]= '\0';
	}
	else
	{
		/* JCM - hex2char would be quicker */
		sprintf(s,"%08lx%04x%04x%04x",
				csn->tstamp,csn->seqnum,csn->rid, csn->subseqnum);
	}
	return s;
}


/*
 * WARNING: ss must point to memory at least (7+CSN_STRSIZE) bytes long, 
 * WARNING: or be NULL, which means this function will allocate the 
 * WARNING: memory, which must be free'd by the caller.
 */
char *
csn_as_attr_option_string(CSNType t,const CSN *csn,char *ss)
{
	char *s= ss;
	if(csn!=NULL)
	{
		if(s==NULL)
		{
			s= slapi_ch_malloc(8+CSN_STRSIZE);
		}
		s[0]= ';';
		switch(t)
		{
		case CSN_TYPE_UNKNOWN:
			s[1]= 'x';
			s[2]= '1';
			break;
		case CSN_TYPE_NONE:
			s[1]= 'x';
			s[2]= '2';
			break;
		case CSN_TYPE_ATTRIBUTE_DELETED:
			s[1]= 'a';
			s[2]= 'd';
			break;
		case CSN_TYPE_VALUE_UPDATED:
			s[1]= 'v';
			s[2]= 'u';
			break;
		case CSN_TYPE_VALUE_DELETED:
			s[1]= 'v';
			s[2]= 'd';
			break;
		case CSN_TYPE_VALUE_DISTINGUISHED:
			s[1]= 'm';
			s[2]= 'd';
			break;
		}
		s[3]= 'c';
		s[4]= 's';
		s[5]= 'n';
		s[6]= '-';
		csn_as_string(csn,PR_FALSE,s+7);
	}
	return s;
}

int 
csn_compare(const CSN *csn1, const CSN *csn2)
{
    PRInt32 retVal;
	if(csn1!=NULL && csn2!=NULL)
	{
        /* csns can't be compared via memcmp (previuos version of the code) 
           because, on NT, bytes are reversed */
        if (csn1->tstamp < csn2->tstamp)
            retVal = -1;
        else if (csn1->tstamp > csn2->tstamp)
            retVal = 1;
        else
        {
            if (csn1->seqnum < csn2->seqnum)
                retVal = -1;
            else if (csn1->seqnum > csn2->seqnum)
                retVal = 1;
            else
            {
                if (csn1->rid < csn2->rid)
                    retVal = -1;
                else if (csn1->rid > csn2->rid)
                    retVal = 1;
                else
                {
                    if (csn1->subseqnum < csn2->subseqnum)
                        retVal = -1;
                    else if (csn1->subseqnum > csn2->subseqnum)
                        retVal = 1;
                    else
                        retVal = 0;
                }
            }
        }
		
	}
	else if(csn1!=NULL && csn2==NULL)
	{
		retVal= 1; /* csn1>csn2 */
	}
	else if (csn1==NULL && csn2!=NULL)
	{
		retVal= -1; /* csn1<csn2 */
	}
	else /* (csn1==NULL && csn2==NULL) */
	{
		retVal= 0; /* The same */
	}

    return(retVal);
}

time_t csn_time_difference(const CSN *csn1, const CSN *csn2)
{
	return csn_get_time(csn1) - csn_get_time(csn2);
}

const CSN *
csn_max(const CSN *csn1,const CSN *csn2)
{
	if(csn_compare(csn1, csn2)>0)
	{
		return csn1;
	}
	else
	{
		return csn2;
	}
}

int csn_increment_subsequence (CSN *csn)
{
	PRUint16 maxsubseq = (PRUint16)0xFFFFFFFF;
	if (csn == NULL)
	{
		return -1;
	}
	else if (csn->subseqnum == maxsubseq)
	{
		slapi_log_error(SLAPI_LOG_FATAL, NULL, 
						"csn_increment_subsequence: subsequence overflow\n");

		return -1;
	}
	else 
	{
		csn->subseqnum ++;
		return 0;	
	}
}

/*
 * sizeof(vucsn-011111111222233334444)
 * Does not include the trailing '\0'
 */
size_t
csn_string_size()
{
	return LDIF_CSNPREFIX_MAXLENGTH + _CSN_VALIDCSN_STRLEN;
}

static PRBool
_csnIsValidString(const char *s)
{
    if(NULL == s) {
		return(PR_FALSE);
    }
    if(strlen(s) < _CSN_VALIDCSN_STRLEN) {
		return(PR_FALSE);
    }

    /* some more checks on validity of tstamp portion? */
    return(PR_TRUE);
}
