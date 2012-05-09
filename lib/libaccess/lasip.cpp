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


/*    aclip.c
 *    This file contains the IP LAS code.
 */

#include <stdio.h>
#include <string.h>
#include <netsite.h>
#include <base/plist.h>
#include <libaccess/nserror.h>
#include <libaccess/nsauth.h>
#include <libaccess/acl.h>
#include "aclpriv.h"
#include <libaccess/aclproto.h>
#include <libaccess/las.h>
#include "lasip.h"
#include "aclutil.h"
#include "aclcache.h"
#include <libaccess/dbtlibaccess.h>
#include <libaccess/aclerror.h>
#include <prio.h>
#include "nspr.h"

#define        LAS_IP_IS_CONSTANT(x)    (((x) == (LASIpTree_t *)LAS_EVAL_TRUE) || ((x) == (LASIpTree_t *)LAS_EVAL_FALSE))

#ifdef	UTEST
extern int LASIpGetIp();
#endif

static int colonhex_ipv6(char *ipstr, char *netmaskstr, PRIPv6Addr *ipv6, int *netmask);
static int LASIpAddPattern(NSErr_t *errp, int netmask, int pattern, LASIpTree_t **treetop);
static int LASIpAddPatternIPv6(NSErr_t *errp, int netmask, PRIPv6Addr *ipv6, LASIpTree_t **treetop);

/*    dotdecimal
 *    Takes netmask and ip strings and returns the numeric values,
 *    accounting for wildards in the ip specification.  Wildcards in the
 *    ip override the netmask where they conflict.
 *    INPUT
 *    ipstr        e.g. "123.45.67.89"
 *    netmaskstr    e.g. "255.255.255.0"
 *    RETURNS
 *    *ip        
 *    *netmask    e.g. 0xffffff00
 *    result        NULL on success or else one of the LAS_EVAL_* codes.
 */
int
dotdecimal(char *ipstr, char *netmaskstr, int *ip, int *netmask)
{
    int     i;
    char    token[64];
    char    *dotptr;    /* location of the "."        */
    int     dotidx;     /* index of the period char    */

    /* Sanity check the patterns */

    /* Netmask can only have digits and periods. */
    if (strcspn(netmaskstr, "0123456789."))
        return LAS_EVAL_INVALID;

    /* IP can only have digits, periods and "*" */
    if (strcspn(ipstr, "0123456789.*"))
        return LAS_EVAL_INVALID;

	if (strlen(netmaskstr) >= sizeof(token)) {
        return LAS_EVAL_INVALID;
	}

	if (strlen(ipstr) >= sizeof(token)) {
        return LAS_EVAL_INVALID;
	}

    *netmask = *ip = 0;    /* Start with "don't care"    */

    for (i=0; i<4; i++) {
        dotptr    = strchr(netmaskstr, '.');

        /* copy out the token, then point beyond it */
        if (dotptr == NULL)
            strcpy(token, netmaskstr);
        else {
            dotidx    = dotptr-netmaskstr;
            strncpy(token, netmaskstr, dotidx);
            token[dotidx] = '\0';
            netmaskstr = ++dotptr;    /* skip the period */
        }

        /* Turn into a number and shift left as appropriate */
        *netmask    += (atoi(token))<<(8*(4-i-1));

        if (dotptr == NULL)
            break;
    }

    for (i=0; i<4; i++) {
        dotptr    = strchr(ipstr, '.');

        /* copy out the token, then point beyond it */
        if (dotptr == NULL)
            strcpy(token, ipstr);
        else {
            dotidx    = dotptr-ipstr;
            strncpy(token, ipstr, dotidx);
            token[dotidx] = '\0';
            ipstr    = ++dotptr;
        }

        /* check for wildcard    */
        if (strcmp(token, "*") == 0) {
            switch(i) {
            case 0:
                if (dotptr == NULL)
                    *netmask &= 0x00000000;
                else
                    *netmask &= 0x00ffffff;
                break;
            case 1:
                if (dotptr == NULL)
                    *netmask &= 0xff000000;
                else
                    *netmask &= 0xff00ffff;
                break;
            case 2:
                if (dotptr == NULL)
                    *netmask &= 0xffff0000;
                else
                    *netmask &= 0xffff00ff;
                break;
            case 3:
                *netmask &= 0xffffff00;
                break;
            }
            continue;
        } else {
            /* Turn into a number and shift left as appropriate */
            *ip    += (atoi(token))<<(8*(4-i-1));
        }

        /* check for end of string    */
        if (dotptr == NULL) {
            switch(i) {
            case 0:
                *netmask &= 0xff000000;
                break;
            case 1:
                *netmask &= 0xffff0000;
                break;
            case 2:
                *netmask &= 0xffffff00;
                break;
            }
            break;
        }
    }

    return 0;
}


/*    LASIpTreeAlloc
 *    Malloc a node and set the actions to LAS_EVAL_FALSE
 */
static LASIpTree_t *
LASIpTreeAllocNode(NSErr_t *errp)
{
    LASIpTree_t    *newnode;

    newnode = (LASIpTree_t *)PERM_MALLOC(sizeof(LASIpTree_t));
    if (newnode == NULL) {
	nserrGenerate(errp, ACLERRNOMEM, ACLERR5000, ACL_Program, 1, XP_GetAdminStr(DBT_lasiptreeallocNoMemoryN_));
        return NULL;
    }
    newnode->action[0] = (LASIpTree_t *)LAS_EVAL_FALSE;
    newnode->action[1] = (LASIpTree_t *)LAS_EVAL_FALSE;
    return newnode;
}


/*    LASIpTreeDealloc
 *    Deallocates a Tree starting from a given node down.
 *    INPUT
 *    startnode    Starting node to remove.  Could be a constant in 
 *            which case, just return success.
 *    OUTPUT
 *    N/A
 */
static void
LASIpTreeDealloc(LASIpTree_t *startnode)
{
    int    i;

    if (startnode == NULL)
        return;

    /* If this is just a constant then we're done             */
    if (LAS_IP_IS_CONSTANT(startnode))
        return;

    /* Else recursively call ourself for each branch down        */
    for (i=0; i<2; i++) {
        if (!(LAS_IP_IS_CONSTANT(startnode->action[i])))
            LASIpTreeDealloc(startnode->action[i]);
    }

    /* Now deallocate the local node                */
    PERM_FREE(startnode);
}

/*
 *    LASIpBuild
 *    INPUT
 *    attr_name    The string "ip" - in lower case.
 *    comparator    CmpOpEQ or CmpOpNE only
 *    attr_pattern    A comma-separated list of IP addresses and netmasks
 *            in dotted-decimal form.  Netmasks are optionally
 *            prepended to the IP address using a plus sign.  E.g.
 *            255.255.255.0+123.45.67.89.  Any byte in the IP address
 *            (but not the netmask) can be wildcarded using "*"
 *    context         A pointer to the IP LAS context structure.
 *    RETURNS
 *    ret code    The usual LAS return codes.
 */
static int
LASIpBuild(NSErr_t *errp, char *attr_name, CmpOp_t comparator, char *attr_pattern, LASIpContext_t *context)
{
    unsigned int delimiter;                /* length of valid token     */
    char        token[64], token2[64];    /* a single ip[+netmask]     */
    char        *curptr;                /* current place in attr_pattern */
    int         netmask = 0;
    int         ip = 0;
    char        *plusptr;
    int         retcode;

    if (NULL == context) {
        return ACL_RES_ERROR;
    }

    /*
     *  IP address can be delimited by space, tab, comma, or carriage return only.
     */
    curptr = attr_pattern;
    do {
        delimiter    = strcspn(curptr, ", \t");
        delimiter    = (delimiter <= strlen(curptr)) ? delimiter : strlen(curptr);
        strncpy(token, curptr, delimiter);
        if (delimiter >= sizeof(token)) {
            return LAS_EVAL_INVALID;
        }
			
        token[delimiter] = '\0';
        /* skip all the white space after the token */
        curptr = strpbrk((curptr+delimiter), "1234567890+.*ABCDEFabcdef:/");

        /*
         *  IPv4 addresses do not have ":"
         */
        if( strstr(token,":") == NULL ){
            /* Is there a netmask?    */
            plusptr = strchr(token, '+');
            if (plusptr == NULL) {
                if (curptr && (*curptr == '+')) {
                    /* There was a space before (and possibly after) the plus sign*/
                    curptr = strpbrk((++curptr), "1234567890.*");
                    delimiter = strcspn(curptr, ", \t");
                    delimiter = (delimiter <= strlen(curptr)) ? delimiter : strlen(curptr);
                    if (delimiter >= sizeof(token2)) {
                        return LAS_EVAL_INVALID;
                    }
                    strncpy(token2, curptr, delimiter);
                    token2[delimiter] = '\0';
                    retcode = dotdecimal(token, token2, &ip, &netmask);
                    if (retcode)
                        return (retcode);
                    curptr = strpbrk((++curptr), "1234567890+.*");
                } else {
                    retcode = dotdecimal(token, "255.255.255.255", &ip, &netmask);
                    if (retcode)
                        return (retcode);
                }
            } else {
                /* token is the IP addr string in both cases */
                *plusptr ='\0';    /* truncate the string */
                retcode =dotdecimal(token, ++plusptr, &ip, &netmask);
                if (retcode)
                    return (retcode);
            }

            if (LASIpAddPattern(errp, netmask, ip, &context->treetop) != 0)
                return LAS_EVAL_INVALID;
        } else {
            /*
             *  IPv6
             */
            PRIPv6Addr ipv6;

            plusptr = strchr(token, '/');
            if (plusptr == NULL) {
                if (curptr && (*curptr == '/')) {
                    /* There was a space before (and possibly after) the plus sign */
                    curptr = strpbrk((++curptr), "1234567890.*:ABCDEFabcdef");
                    delimiter = strcspn(curptr, ", \t");
                    delimiter = (delimiter <= strlen(curptr)) ?	delimiter : strlen(curptr);
                    strncpy(token2, curptr, delimiter);
                    token2[delimiter] = '\0';
                    retcode = colonhex_ipv6(token, token2, &ipv6, &netmask);
                    if (retcode)
                        return (retcode);
                    curptr = strpbrk((++curptr), "1234567890+.:ABCDEFabcdef*");
                } else {
                    retcode = colonhex_ipv6(token, "128", &ipv6, &netmask);
                    if (retcode)
                        return (retcode);
                }
            } else {
                /* token is the IP addr string in both cases */
                *plusptr ='\0';    /* truncate the string */
                retcode = colonhex_ipv6(token, ++plusptr, &ipv6, &netmask);
                if (retcode)
                    return (retcode);
            }

            if (LASIpAddPatternIPv6(errp, netmask, &ipv6, &context->treetop_ipv6) != (int)NULL) {
                return LAS_EVAL_INVALID;
            }
        }
    } while ((curptr != NULL) && (delimiter != 0));

    return 0;
}


/*    LASIpAddPattern
 *    Takes a netmask and IP address and a pointer to an existing IP
 *    tree and adds nodes as appropriate to recognize the new pattern.
 *    INPUT
 *    netmask        e.g. 0xffffff00
 *    pattern        IP address in 4 bytes
 *    *treetop    An existing IP tree or 0 if a new tree
 *    RETURNS
 *    ret code    NULL on success, ACL_RES_ERROR on failure
 *    **treetop    If this is a new tree, the head of the tree.
 */
static int
LASIpAddPattern(NSErr_t *errp, int netmask, int pattern, LASIpTree_t **treetop)
{
    int        stopbit;    /* Don't care after this point    */
    int        curbit;        /* current bit we're working on    */
    int        curval;        /* value of pattern[curbit]    */
    LASIpTree_t    *curptr = NULL;    /* pointer to the current node    */
    LASIpTree_t    *newptr;

    if (NULL == treetop) {
        return ACL_RES_ERROR;
    }

    /* stop at the first 1 in the netmask from low to high         */
    for (stopbit=0; stopbit<32; stopbit++) {
        if ((netmask&(1<<stopbit)) != 0)
            break;
    }

    /* Special case if there's no tree.  Allocate the first node    */
    if (*treetop == (LASIpTree_t *)NULL) {    /* No tree at all */
        curptr = LASIpTreeAllocNode(errp);
        if (curptr == NULL) {
            nserrGenerate(errp, ACLERRFAIL, ACLERR5100, ACL_Program, 1,
                XP_GetAdminStr(DBT_ipLasUnableToAllocateTreeNodeN_));
            return ACL_RES_ERROR;
        }
        *treetop = curptr;
    }

    /*
     *  Special case if the netmask is 0.
     */
    if (stopbit > 31) {
        (*treetop)->action[0] = (LASIpTree_t *)LAS_EVAL_TRUE;
        (*treetop)->action[1] = (LASIpTree_t *)LAS_EVAL_TRUE;
        return 0;
    }

    /* follow the tree down the pattern path bit by bit until the
     * end of the tree is reached (i.e. a constant).
     */
    for (curbit=31,curptr=*treetop; curbit >= 0; curbit--) {
        /* Is the current bit ON?  If so set curval to 1 else 0    */
        curval = (pattern & (1<<curbit)) ? 1 : 0;

        /* Are we done, if so remove the rest of the tree     */
        if (curbit == stopbit) {
            LASIpTreeDealloc(curptr->action[curval]);
            curptr->action[curval] = (LASIpTree_t *)LAS_EVAL_TRUE;
            /* This is the normal exit point.  Most other  exits must be due to errors. */
            return 0;
        }

        /* Oops reached the end - must allocate        */
        if (LAS_IP_IS_CONSTANT(curptr->action[curval])) {
            newptr = LASIpTreeAllocNode(errp);
            if (newptr == NULL) {
                LASIpTreeDealloc(*treetop);
                nserrGenerate(errp, ACLERRFAIL, ACLERR5110, ACL_Program, 1,
                    XP_GetAdminStr(DBT_ipLasUnableToAllocateTreeNodeN_1));
                return ACL_RES_ERROR;
            }
            curptr->action[curval] = newptr;
        }

        /* Keep going down the tree                */
        curptr = curptr->action[curval];
    }

    return ACL_RES_ERROR;
}

/*    LASIpFlush
 *    Deallocates any memory previously allocated by the LAS
 */
void
LASIpFlush(void **las_cookie)
{
    if (*las_cookie    == NULL)
        return;

    LASIpTreeDealloc(((LASIpContext_t *)*las_cookie)->treetop);
    PERM_FREE(*las_cookie);
    *las_cookie = NULL;
    return;
}

/*
 *    LASIpEval
 *    INPUT
 *    attr_name      The string "ip" - in lower case.
 *    comparator     CMP_OP_EQ or CMP_OP_NE only
 *    attr_pattern   A comma-separated list of IP addresses and netmasks
 *                   in dotted-decimal form.  Netmasks are optionally
 *                   prepended to the IP address using a plus sign.  E.g.
 *                   255.255.255.0+123.45.67.89.  Any byte in the IP address
 *                   (but not the netmask) can be wildcarded using "*"
 *    *cachable      Always set to ACL_INDEF_CACHABLE
 *    subject        Subject property list
 *    resource       Resource property list
 *    auth_info      The authentication info if any
 *    RETURNS
 *    ret code       The usual LAS return codes.
 */
int LASIpEval(NSErr_t *errp, char *attr_name, CmpOp_t comparator,
          char *attr_pattern, ACLCachable_t *cachable, void **LAS_cookie,
          PList_t subject, PList_t resource, PList_t auth_info,
          PList_t global_auth)
{
    LASIpContext_t *context = NULL;
    LASIpTree_t *node = NULL;
    IPAddr_t ip;
    PRNetAddr *client_addr = NULL;
    struct in_addr client_inaddr;
    char ip_str[124];
    int retcode;
    int value;
    int bit;
    int rc = LAS_EVAL_INVALID;
    int rv;

    *cachable = ACL_INDEF_CACHABLE;

    if (strcmp(attr_name, "ip") != 0) {
        nserrGenerate(errp, ACLERRINVAL, ACLERR5200, ACL_Program, 2,
            XP_GetAdminStr(DBT_lasIpBuildReceivedRequestForAttr_), attr_name);
        return LAS_EVAL_INVALID;
    }

    if ((comparator != CMP_OP_EQ) && (comparator != CMP_OP_NE)) {
        nserrGenerate(errp, ACLERRINVAL, ACLERR5210, ACL_Program, 2,
            XP_GetAdminStr(DBT_lasipevalIllegalComparatorDN_), comparator_string(comparator));
        return LAS_EVAL_INVALID;
    }

    /*
     *  Get the IP addr from the session context, and store it in "client_addr
     */
#ifndef    UTEST
    rv = ACL_GetAttribute(errp, ACL_ATTR_IP, (void **)&client_addr, subject, resource, auth_info, global_auth);

    if (rv != LAS_EVAL_TRUE) {
        if (subject || resource) {
            /* Don't ereport if called from ACL_CachableAclList */
            char rv_str[16];
            sprintf(rv_str, "%d", rv);
            nserrGenerate(errp, ACLERRINVAL, ACLERR5220, ACL_Program, 2,
                XP_GetAdminStr(DBT_lasipevalUnableToGetSessionAddre_), rv_str);
        }
        return LAS_EVAL_FAIL;
    }
#else
    ip    = (IPAddr_t)LASIpGetIp();
#endif

    /*
     *  If this is the first time through, build the pattern tree first.
     */
    if (*LAS_cookie == NULL) {
        if (strcspn(attr_pattern, "0123456789.*,+ \tABCDEFabcdef:/")) {
            return LAS_EVAL_INVALID;
        }
        ACL_CritEnter();
        if (*LAS_cookie == NULL) {    /* must check again */
            *LAS_cookie = context = 
                (LASIpContext_t *)PERM_MALLOC(sizeof(LASIpContext_t));
            if (context == NULL) {
                nserrGenerate(errp, ACLERRNOMEM, ACLERR5230, ACL_Program, 1,
                    XP_GetAdminStr(DBT_lasipevalUnableToAllocateContext_));
                ACL_CritExit();
                return LAS_EVAL_FAIL;
            }
            context->treetop = NULL;
            context->treetop_ipv6 = NULL;
            retcode = LASIpBuild(errp, attr_name, comparator, attr_pattern, context);
            if (retcode) {
                ACL_CritExit();
                return (retcode);
            }
        } else {
            context = (LASIpContext *) *LAS_cookie;
        }
        ACL_CritExit();
    } else {
        ACL_CritEnter();
        context = (LASIpContext *) *LAS_cookie;
        ACL_CritExit();
    }
    /*
     *  Check if IP is ipv4/ipv6
     */
     if ( PR_IsNetAddrType( client_addr, PR_IpAddrV4Mapped) || client_addr->raw.family == PR_AF_INET ) {
         /*
          *  IPv4
          */

         /* Set the appropriate s_addr for ipv4 or ipv4 mapped to ipv6 */
         if (client_addr->raw.family == PR_AF_INET) {
             client_inaddr.s_addr = client_addr->inet.ip;
         } else {
             client_inaddr.s_addr = client_addr->ipv6.ip._S6_un._S6_u32[3];
         }

         node = context->treetop;
         ip = (IPAddr_t)PR_ntohl( client_inaddr.s_addr );

         if(node == NULL){
             rc = (comparator == CMP_OP_EQ ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
         } else {
             for (bit = 31; bit >= 0; bit--) {
                 value = (ip & (IPAddr_t) (1 << bit)) ? 1 : 0;
                 if (LAS_IP_IS_CONSTANT(node->action[value])){
                     /* Reached a result, so return it */
                     if (comparator == CMP_OP_EQ){
                         rc = (int)(PRSize)node->action[value];
                         break;
                     } else {
                         rc = ((int)(PRSize)node->action[value] == LAS_EVAL_TRUE) ? LAS_EVAL_FALSE : LAS_EVAL_TRUE;
                         break;
                     }
                 } else {
                     /* Move on to the next bit */
                     node = node->action[value];
                 }
             }
         }
         if(rc == LAS_EVAL_INVALID){
             sprintf(ip_str, "%x", (unsigned int)ip);
             nserrGenerate(errp, ACLERRINTERNAL, ACLERR5240, ACL_Program, 2,
                 XP_GetAdminStr(DBT_lasipevalReach32BitsWithoutConcl_), ip_str);
         }
    } else {
        /*
         *  IPv6
         */
        PRIPv6Addr *ipv6 = &(client_addr->ipv6.ip);
        LASIpTree_t *node;
        int bit_position = 15;
        int field = 0;
        int addr = 0;
        int value;

        node = context->treetop_ipv6;
        if ( node == NULL ) {
            retcode = (comparator == CMP_OP_EQ ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
        } else {
            addr = PR_ntohs( ipv6->_S6_un._S6_u16[field]);
            for (bit = 127; bit >= 0 ; bit--, bit_position--) {
                value = (addr & (1 << bit_position)) ? 1 : 0;
                if (LAS_IP_IS_CONSTANT(node->action[value])) {
                    /* Reached a result, so return it */
                    if (comparator == CMP_OP_EQ){
                        return(int)(long)node->action[value];
                    } else {
                        return(((int)(long)node->action[value] == LAS_EVAL_TRUE) ? LAS_EVAL_FALSE : LAS_EVAL_TRUE);
                    }
                } else {
                    node = node->action[value];
                    if ( bit % 16 == 0) {
                        /* Ok, move to the next field in the IPv6 addr:  f:f:next:f:f:f:f:f  */
                        field++;
                        addr = PR_ntohs( ipv6->_S6_un._S6_u16[field]);
                        bit_position = 15;
                    }
                }
            }
            rc = LAS_EVAL_INVALID;
        }
    }
    return rc;
}

/*
 *  The ipv6 version of LASIpAddPattern
 */
static int
LASIpAddPatternIPv6(NSErr_t *errp, int netmask, PRIPv6Addr *ipv6, LASIpTree_t **treetop)
{
    LASIpTree_t    *curptr;
    LASIpTree_t    *newptr;
    int stopbit;
    int curbit;
    int curval;
    int field = 0; /* (8) 16 bit fields in a IPv6 address: x:x:x:x:x:x:x:x */
    int addr = 0;
    int curbit_position = 15; /* 16 bits: 0-15 */

    /* stop at the first 1 in the netmask from low to high */
    stopbit = 128 - netmask;

    /* Special case if there's no tree.  Allocate the first node    */
    if (*treetop == (LASIpTree_t *)NULL) {    /* No tree at all */
        curptr = LASIpTreeAllocNode(errp);
        if (curptr == NULL) {
            nserrGenerate(errp, ACLERRFAIL, ACLERR5100, ACL_Program, 1,
                XP_GetAdminStr(DBT_ipLasUnableToAllocateTreeNodeN_));
            return ACL_RES_ERROR;
        }
        *treetop = curptr;
    }

    addr = PR_ntohs(ipv6->_S6_un._S6_u16[field]);
    for (curbit = 127, curptr = *treetop; curbit >= 0; curbit--, curbit_position--){
        /* Is the current bit ON?  If so set curval to 1 else 0 */
        curval = (addr & (1 << curbit_position)) ? 1 : 0;

        /* Are we done, if so remove the rest of the tree */
        if (curbit == stopbit) {
            LASIpTreeDealloc(curptr->action[curval]);
            curptr->action[curval] = (LASIpTree_t *)LAS_EVAL_TRUE;
            /* This is the normal exit point.  Most other exits must be due to errors. */
            return 0;
        }

        /* Oops reached the end - must allocate  */
        if (LAS_IP_IS_CONSTANT(curptr->action[curval])) {
            newptr = LASIpTreeAllocNode(errp);
            if (newptr == NULL) {
                LASIpTreeDealloc(*treetop);
                nserrGenerate(errp, ACLERRFAIL, ACLERR5110, ACL_Program, 1,
                    XP_GetAdminStr(DBT_ipLasUnableToAllocateTreeNodeN_1));
                return ACL_RES_ERROR;
             }
             curptr->action[curval] = newptr;
         }

         /* Keep going down the tree */
         curptr = curptr->action[curval];

         if ( curbit % 16 == 0) {
             /* Ok, move to the next field in the addr */
             field++;
             addr = PR_ntohs(ipv6->_S6_un._S6_u16[field]);
             curbit_position = 15;
         }
    }
    return ACL_RES_ERROR;
}

/*
 *  This is very similar to dotdecimal(), but for ipv6 addresses
 */
static int
colonhex_ipv6(char *ipstr, char *netmaskstr, PRIPv6Addr *ipv6, int *netmask)
{
    PRNetAddr addr;
    /*
     *  Validate netmaskstr - can only be digits
     */
    if (strcspn(netmaskstr, "0123456789")){
        return LAS_EVAL_INVALID;
    }
    /*
     *  Validate ipstr - can only have digits, colons, hex chars, and dots
     */
    if(strcspn(ipstr, "0123456789:ABCDEFabcdef.")){
        return LAS_EVAL_INVALID;
    }
    /*
     *  validate the netmask - must be between 1 and 128
     */
    *netmask = atoi(netmaskstr);
    if(*netmask < 1 || *netmask > 128){
        return LAS_EVAL_INVALID;
    }
    /*
     *  Get the net addr
     */
    if (PR_StringToNetAddr(ipstr, &addr) != PR_SUCCESS){
        return LAS_EVAL_INVALID;
    }
    /*
     *  Set the ipv6 addr
     */
    *ipv6 = addr.ipv6.ip;

    return 0;
}

