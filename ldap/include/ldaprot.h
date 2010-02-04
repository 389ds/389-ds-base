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

#ifndef _LDAPROT_H
#define _LDAPROT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LDAP_VERSION1
#define LDAP_VERSION1	1
#endif
#ifndef LDAP_VERSION2
#define LDAP_VERSION2	2
#endif
#ifndef LDAP_VERSION3
#define LDAP_VERSION3	3
#endif
#ifndef LDAP_VERSION
#define LDAP_VERSION	LDAP_VERSION2
#endif

#define COMPAT20
#define COMPAT30
#if defined(COMPAT20) || defined(COMPAT30)
#define COMPAT
#endif

#define LDAP_URL_PREFIX		"ldap://"
#define LDAP_URL_PREFIX_LEN	7
#define LDAPS_URL_PREFIX	"ldaps://"
#define LDAPS_URL_PREFIX_LEN	8
#define LDAP_REF_STR		"Referral:\n"
#define LDAP_REF_STR_LEN	10

/* 
 * specific LDAP instantiations of BER types we know about
 */

/* general stuff */
#ifndef LDAP_TAG_MESSAGE
#define LDAP_TAG_MESSAGE	0x30L	/* tag is 16 + constructed bit */
#endif
#ifndef OLD_LDAP_TAG_MESSAGE
#define OLD_LDAP_TAG_MESSAGE	0x10L	/* forgot the constructed bit  */
#endif
#ifndef LDAP_TAG_MSGID
#define LDAP_TAG_MSGID		0x02L   /* INTEGER */
#endif
#ifndef LDAP_TAG_LDAPDN
#define LDAP_TAG_LDAPDN		0x04L	/* OCTET STRING */
#endif
#ifndef LDAP_TAG_CONTROLS
#define LDAP_TAG_CONTROLS	0xa0L	/* context specific + constructed + 0 */
#endif
#ifndef LDAP_TAG_REFERRAL
#define LDAP_TAG_REFERRAL	0xa3L	/* context specific + constructed */
#endif
#ifndef LDAP_TAG_NEWSUPERIOR
#define LDAP_TAG_NEWSUPERIOR	0x80L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_MRA_OID
#define LDAP_TAG_MRA_OID	0x81L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_MRA_TYPE
#define LDAP_TAG_MRA_TYPE	0x82L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_MRA_VALUE
#define LDAP_TAG_MRA_VALUE	0x83L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_MRA_DNATTRS
#define LDAP_TAG_MRA_DNATTRS	0x84L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_EXOP_REQ_OID
#define LDAP_TAG_EXOP_REQ_OID	0x80L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_EXOP_REQ_VALUE
#define LDAP_TAG_EXOP_REQ_VALUE	0x81L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_EXOP_RES_OID
#define LDAP_TAG_EXOP_RES_OID	0x8aL	/* context specific + primitive + 10 */
#endif
#ifndef LDAP_TAG_EXOP_RES_VALUE
#define LDAP_TAG_EXOP_RES_VALUE	0x8bL	/* context specific + primitive + 11 */
#endif
#ifndef LDAP_TAG_SK_MATCHRULE
#define LDAP_TAG_SK_MATCHRULE   0x80L   /* context specific + primitive */
#endif
#ifndef LDAP_TAG_SK_REVERSE
#define LDAP_TAG_SK_REVERSE 	0x81L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_SR_ATTRTYPE
#define LDAP_TAG_SR_ATTRTYPE    0x80L   /* context specific + primitive */
#endif
#ifndef LDAP_TAG_SASL_RES_CREDS
#define LDAP_TAG_SASL_RES_CREDS	0x87L	/* context specific + primitive */
#endif
#ifndef LDAP_TAG_VLV_BY_INDEX
#define LDAP_TAG_VLV_BY_INDEX	0xa0L	/* context specific + constructed + 0 */
#endif
#ifndef LDAP_TAG_VLV_BY_VALUE
#define LDAP_TAG_VLV_BY_VALUE	0x81L	/* context specific + primitive + 1 */
#endif
#ifndef LDAP_TAG_PWP_WARNING
#define LDAP_TAG_PWP_WARNING	0xA0	/* context specific + constructed + 0 */
#endif
#ifndef LDAP_TAG_PWP_SECSLEFT
#define LDAP_TAG_PWP_SECSLEFT	0x80L   /* context specific + primitive */
#endif
#ifndef LDAP_TAG_PWP_GRCLOGINS
#define LDAP_TAG_PWP_GRCLOGINS	0x81L   /* context specific + primitive + 1 */
#endif
#ifndef LDAP_TAG_PWP_ERROR
#define LDAP_TAG_PWP_ERROR	0x81L   /* context specific + primitive + 1 */
#endif

/* possible operations a client can invoke */
#ifndef LDAP_REQ_BIND
#define LDAP_REQ_BIND			0x60L	/* application + constructed */
#endif
#ifndef LDAP_REQ_UNBIND
#define LDAP_REQ_UNBIND			0x42L	/* application + primitive   */
#endif
#ifndef LDAP_REQ_SEARCH
#define LDAP_REQ_SEARCH			0x63L	/* application + constructed */
#endif
#ifndef LDAP_REQ_MODIFY
#define LDAP_REQ_MODIFY			0x66L	/* application + constructed */
#endif
#ifndef LDAP_REQ_ADD
#define LDAP_REQ_ADD			0x68L	/* application + constructed */
#endif
#ifndef LDAP_REQ_DELETE
#define LDAP_REQ_DELETE			0x4aL	/* application + primitive   */
#endif
#ifndef LDAP_REQ_MODRDN
#define LDAP_REQ_MODRDN			0x6cL	/* application + constructed */
#endif
#ifndef LDAP_REQ_MODDN
#define LDAP_REQ_MODDN			0x6cL	/* application + constructed */  
#endif
#ifndef LDAP_REQ_RENAME
#define LDAP_REQ_RENAME			0x6cL	/* application + constructed */  
#endif
#ifndef LDAP_REQ_COMPARE
#define LDAP_REQ_COMPARE		0x6eL	/* application + constructed */
#endif
#ifndef LDAP_REQ_ABANDON
#define LDAP_REQ_ABANDON		0x50L	/* application + primitive   */
#endif
#ifndef LDAP_REQ_EXTENDED
#define LDAP_REQ_EXTENDED		0x77L	/* application + constructed */
#endif

/* version 3.0 compatibility stuff */
#ifndef LDAP_REQ_UNBIND_30
#define LDAP_REQ_UNBIND_30		0x62L
#endif
#ifndef LDAP_REQ_DELETE_30
#define LDAP_REQ_DELETE_30		0x6aL
#endif
#ifndef LDAP_REQ_ABANDON_30
#define LDAP_REQ_ABANDON_30		0x70L
#endif

/* 
 * old broken stuff for backwards compatibility - forgot application tag
 * and constructed/primitive bit
 */
#define OLD_LDAP_REQ_BIND		0x00L
#define OLD_LDAP_REQ_UNBIND		0x02L
#define OLD_LDAP_REQ_SEARCH		0x03L
#define OLD_LDAP_REQ_MODIFY		0x06L
#define OLD_LDAP_REQ_ADD		0x08L
#define OLD_LDAP_REQ_DELETE		0x0aL
#define OLD_LDAP_REQ_MODRDN		0x0cL
#define OLD_LDAP_REQ_MODDN		0x0cL
#define OLD_LDAP_REQ_COMPARE		0x0eL
#define OLD_LDAP_REQ_ABANDON		0x10L

/* old broken stuff for backwards compatibility */
#define OLD_LDAP_RES_BIND		0x01L
#define OLD_LDAP_RES_SEARCH_ENTRY	0x04L
#define OLD_LDAP_RES_SEARCH_RESULT	0x05L
#define OLD_LDAP_RES_MODIFY		0x07L
#define OLD_LDAP_RES_ADD		0x09L
#define OLD_LDAP_RES_DELETE		0x0bL
#define OLD_LDAP_RES_MODRDN		0x0dL
#define OLD_LDAP_RES_MODDN		0x0dL
#define OLD_LDAP_RES_COMPARE		0x0fL

/* 3.0 compatibility auth methods */
#define LDAP_AUTH_SIMPLE_30	0xa0L	/* context specific + constructed */
#define LDAP_AUTH_KRBV41_30	0xa1L	/* context specific + constructed */
#define LDAP_AUTH_KRBV42_30	0xa2L	/* context specific + constructed */

/* old broken stuff */
#define OLD_LDAP_AUTH_SIMPLE	0x00L
#define OLD_LDAP_AUTH_KRBV4	0x01L
#define OLD_LDAP_AUTH_KRBV42	0x02L

/* 3.0 compatibility filter types */
#define LDAP_FILTER_PRESENT_30	0xa7L	/* context specific + constructed */

/* filter types */
#ifndef LDAP_FILTER_AND
#define LDAP_FILTER_AND		0xa0L	/* context specific + constructed */
#endif
#ifndef LDAP_FILTER_OR
#define LDAP_FILTER_OR		0xa1L	/* context specific + constructed */
#endif
#ifndef LDAP_FILTER_NOT
#define LDAP_FILTER_NOT		0xa2L	/* context specific + constructed */
#endif
#ifndef LDAP_FILTER_EQUALITY
#define LDAP_FILTER_EQUALITY	0xa3L	/* context specific + constructed */
#endif
#ifndef LDAP_FILTER_SUBSTRINGS
#define LDAP_FILTER_SUBSTRINGS	0xa4L	/* context specific + constructed */
#endif
#ifndef LDAP_FILTER_GE
#define LDAP_FILTER_GE		0xa5L	/* context specific + constructed */
#endif
#ifndef LDAP_FILTER_LE
#define LDAP_FILTER_LE		0xa6L	/* context specific + constructed */
#endif
#ifndef LDAP_FILTER_PRESENT
#define LDAP_FILTER_PRESENT	0x87L	/* context specific + primitive   */
#endif
#ifndef LDAP_FILTER_APPROX
#define LDAP_FILTER_APPROX	0xa8L	/* context specific + constructed */
#endif
#ifndef LDAP_FILTER_EXTENDED
#ifdef LDAP_FILTER_EXT
#define LDAP_FILTER_EXTENDED	LDAP_FILTER_EXT
#else
#define LDAP_FILTER_EXTENDED 0xa9L
#endif
#endif

/* old broken stuff */
#define OLD_LDAP_FILTER_AND		0x00L
#define OLD_LDAP_FILTER_OR		0x01L
#define OLD_LDAP_FILTER_NOT		0x02L
#define OLD_LDAP_FILTER_EQUALITY	0x03L
#define OLD_LDAP_FILTER_SUBSTRINGS	0x04L
#define OLD_LDAP_FILTER_GE		0x05L
#define OLD_LDAP_FILTER_LE		0x06L
#define OLD_LDAP_FILTER_PRESENT		0x07L
#define OLD_LDAP_FILTER_APPROX		0x08L

/* substring filter component types */
#ifndef LDAP_SUBSTRING_INITIAL
#define LDAP_SUBSTRING_INITIAL	0x80L	/* context specific */
#endif
#ifndef LDAP_SUBSTRING_ANY
#define LDAP_SUBSTRING_ANY	0x81L	/* context specific */
#endif
#ifndef LDAP_SUBSTRING_FINAL
#define LDAP_SUBSTRING_FINAL	0x82L	/* context specific */
#endif

/* extended filter component types */
#ifndef LDAP_FILTER_EXTENDED_OID
#ifdef LDAP_FILTER_EXT_OID
#define LDAP_FILTER_EXTENDED_OID LDAP_FILTER_EXT_OID
#else
#define LDAP_FILTER_EXTENDED_OID	0x81L	/* context specific */
#endif
#endif
#ifndef LDAP_FILTER_EXTENDED_TYPE
#ifdef LDAP_FILTER_EXT_TYPE
#define LDAP_FILTER_EXTENDED_TYPE LDAP_FILTER_EXT_TYPE
#else
#define LDAP_FILTER_EXTENDED_TYPE	0x82L	/* context specific */
#endif
#endif
#ifndef LDAP_FILTER_EXTENDED_VALUE
#ifdef LDAP_FILTER_EXT_VALUE
#define LDAP_FILTER_EXTENDED_VALUE LDAP_FILTER_EXT_VALUE
#else
#define LDAP_FILTER_EXTENDED_VALUE	0x83L	/* context specific */
#endif
#endif
#ifndef LDAP_FILTER_EXTENDED_DNATTRS
#ifdef LDAP_FILTER_EXT_DNATTRS
#define LDAP_FILTER_EXTENDED_DNATTRS LDAP_FILTER_EXT_DNATTRS
#else
#define LDAP_FILTER_EXTENDED_DNATTRS	0x84L	/* context specific */
#endif
#endif

/* 3.0 compatibility substring filter component types */
#define LDAP_SUBSTRING_INITIAL_30	0xa0L	/* context specific */
#define LDAP_SUBSTRING_ANY_30		0xa1L	/* context specific */
#define LDAP_SUBSTRING_FINAL_30		0xa2L	/* context specific */

/* old broken stuff */
#define OLD_LDAP_SUBSTRING_INITIAL	0x00L
#define OLD_LDAP_SUBSTRING_ANY		0x01L
#define OLD_LDAP_SUBSTRING_FINAL	0x02L

#ifdef __cplusplus
}
#endif
#endif /* _LDAPROT_H */
