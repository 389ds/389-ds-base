/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef __aclparse_h
#define __aclparse_h

/*
 * Description (aclparse.h)
 *
 *	This file describes the interface to a parser for files
 *	containing Access Control List (ACL) definitions.  The parser
 *	uses the services of the aclbuild module to construct an
 *	in-memory representation of the ACLs it parses.
 */

#include "nserror.h"
#include "aclbuild.h"

/* Define keywords */
#define KEYWORD_ACL	"acl"
#define KEYWORD_ALL	"all"
#define KEYWORD_ALLOW	"allow"
#define KEYWORD_ANY	"anyone"
#define KEYWORD_AT	"at"
#define KEYWORD_AUTH	"authenticate"
#define KEYWORD_BASIC	"basic"
#define KEYWORD_DATABASE "database"
#define KEYWORD_DEFAULT	"default"
#define KEYWORD_DENY	"deny"
#define KEYWORD_EXECUTE	"execute"
#define KEYWORD_HOSTS	"hosts"
#define KEYWORD_IF	"if"
#define KEYWORD_IN	"in"
#define KEYWORD_INCLUDE	"include"
#define KEYWORD_METHOD	"method"
#define KEYWORD_PROMPT	"prompt"
#define KEYWORD_REALM	"realm"
#define KEYWORD_RIGHTS	"rights"
#define KEYWORD_SSL	"ssl"

/* Define character classes */
#define CCM_WS		0x1	/* whitespace */
#define CCM_NL		0x2	/* newline */
#define CCM_SPECIAL	0x4	/* special characters */
#define CCM_DIGIT	0x8	/* digits */
#define CCM_LETTER	0x10	/* letters */
#define CCM_HYPHEN	0x20	/* hyphen */
#define CCM_USCORE	0x40	/* underscore */
#define CCM_FILESPEC	0x80	/* filename special characters */

#define CCM_HYPUND	(CCM_HYPHEN|CCM_USCORE)
#define CCM_IDENT	(CCM_LETTER|CCM_DIGIT|CCM_HYPUND)
#define CCM_FILENAME	(CCM_LETTER|CCM_DIGIT|CCM_FILESPEC)

/* Define token numbers */
#define TOKEN_ERROR	-1	/* error in reading data stream */
#define TOKEN_EOF	0	/* end-of-file */
#define TOKEN_EOS	1	/* end-of-statement */
#define TOKEN_IDENT	2	/* identifier */
#define TOKEN_NUMBER	3	/* number */
#define TOKEN_COMMA	4	/* comma */
#define TOKEN_SEMI	5	/* semicolon */
#define TOKEN_PERIOD	6	/* period */
#define TOKEN_LPAREN	7	/* left parenthesis */
#define TOKEN_RPAREN	8	/* right parenthesis */
#define TOKEN_LBRACE	9	/* left brace */
#define TOKEN_RBRACE	10	/* right brace */
#define TOKEN_AT	11	/* at sign */
#define TOKEN_PLUS	12	/* plus sign */
#define TOKEN_STAR	13	/* asterisk */
#define TOKEN_STRING	14	/* quoted string */
#define TOKEN_HUH	15	/* unrecognized input */

/* Define flags bits for aclGetToken() */
#define AGT_NOSKIP	0x1		/* don't skip leading whitespace */
#define AGT_APPEND	0x2		/* append next to token buffer */

NSPR_BEGIN_EXTERN_C

extern void * aclChTab;			/* character table for ACL parsing */

/* Functions in aclparse.c */
extern int aclAuthListParse(NSErr_t * errp, ACLFile_t * acf,
			    ACContext_t * acc, Realm_t * rlm,
			    ACClients_t **clsp);
extern int aclAuthHostsParse(NSErr_t * errp, ACLFile_t * acf,
			     ACContext_t * acc, HostSpec_t **hspp);
extern int aclAuthUsersParse(NSErr_t * errp, ACLFile_t * acf,
			     Realm_t * rlm, UserSpec_t **uspp, char ***elist);
extern int aclDirectivesParse(NSErr_t * errp, ACLFile_t * acf, ACL_t * acl);
extern int aclACLParse(NSErr_t * errp,
		       ACLFile_t * acf, ACContext_t * acc, int flags);
extern void aclFileClose(ACLFile_t * acf, int flags);
extern int aclFileOpen(NSErr_t * errp,
		       char * filename, int flags, ACLFile_t **pacf);
extern int aclGetDNSString(NSErr_t * errp, ACLFile_t * acf);
extern int aclGetFileSpec(NSErr_t * errp, ACLFile_t * acf, int flags);
extern int aclGetIPAddr(NSErr_t * errp,
			ACLFile_t * acf, IPAddr_t * pip, IPAddr_t * pmask);
extern int aclGetToken(NSErr_t * errp, ACLFile_t * acf, int flags);
extern int aclParseInit();
extern int aclRealmSpecParse(NSErr_t * errp, ACLFile_t * acf,
			     ACContext_t * acc, RealmSpec_t **rspp);
extern int aclRightsParse(NSErr_t * errp, ACLFile_t * acf, ACContext_t * acc,
			  RightSpec_t **rights);
extern int aclStreamGet(LEXStream_t * lst);

NSPR_END_EXTERN_C

#endif /* __aclparse_h */
