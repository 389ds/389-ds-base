/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

/*
 * Description (aclparse.c)
 *
 *	This module provides functions for parsing a file containing
 *	Access Control List (ACL) definitions.  It builds a representation
 *	of the ACLs in memory, using the services of the aclbuild module.
 */

#include <base/systems.h>
#include <base/file.h>
#include <base/util.h>
#include <netsite.h>
#include <libaccess/nsadb.h>
#include <libaccess/aclerror.h>
#include <libaccess/aclparse.h>
#include <libaccess/symbols.h>

#ifdef XP_UNIX
#include <sys/types.h>
#include <netinet/in.h>  /* ntohl */
#include <arpa/inet.h>
#endif

void * aclChTab = 0;		/* character class table handle */

static char * classv[] = {
    " \t\r\f\013",		/* class 0 - whitespace */
    "\n",			/* class 1 - newline */
    ",.;@*()+{}\"\'",		/* class 2 - special characters */
    "0123456789",		/* class 3 - digits */
				/* class 4 - letters */
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
    "-",			/* class 5 - hyphen */
    "_",			/* class 6 - underscore */
    "/-_.:"			/* class 7 - filename special characters */
};

static int classc = sizeof(classv)/sizeof(char *);

/*
 * Description (aclAuthListParse)
 *
 *	This function parses an auth-list.  An auth-list specifies
 *	combinations of user/group names and host addresses/names.
 *	An auth-list entry can identify a collection of users and/or
 *	groups, a collection of hosts by IP addresses or DNS names,
 *	or a combination of the two.  Each auth-spec adds another
 *	ACClients_t structure to the specified list.
 *
 *	The syntax for an auth-list is:
 *
 *	auth-list ::= auth-spec | auth-list "," auth-spec
 *	auth-spec ::= auth-users [at-token auth-hosts]
 *	auth-users - see aclAuthUsersParse()
 *	auth-hosts - see aclAuthHostsParse()
 *	at-token ::= "at" | "@"
 *
 *	The caller provides a pointer to a ClientSpec_t structure,
 *	which is built up with new information as auth-specs are parsed.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	acc			- pointer to ACL context object
 *	rlm			- pointer to authentication realm object
 *	clsp			- pointer to returned ACClients_t list head
 *
 * Returns:
 *
 *	If successful, the return value is the token type of the token
 *	following the auth-list, i.e. the first token which is not
 *	recognized as the start of an auth-spec.  It is the caller's
 *	responsibility to validate this token as a legitimate terminator
 *	of an auth-list.  If a parsing error occurs in the middle of
 *	an auth-spec, the return value is ACLERRPARSE, and an error frame
 *	is generated if an error list is provided.  For other kinds of
 *	errors a negative error code (from aclerror.h) is returned.
 */

int aclAuthListParse(NSErr_t * errp, ACLFile_t * acf,
		     ACContext_t * acc, Realm_t * rlm, ACClients_t **clsp)
{
    void * token = acf->acf_token;	/* token handle */
    ACClients_t * csp;			/* client spec pointer */
    UserSpec_t * usp;			/* user spec pointer */
    HostSpec_t * hsp;			/* host spec pointer */
    int rv;				/* result value */
    int eid;				/* error id */

    /* Loop once for each auth-spec */
    for (rv = acf->acf_ttype; ; rv = aclGetToken(errp, acf, 0)) {

	usp = 0;
	hsp = 0;

	/* Parse auth-users into user and group lists in the ACClients_t */
	rv = aclAuthUsersParse(errp, acf, rlm, &usp, 0);
	if (rv < 0) break;

	/* Is the at-token there? */
	if ((rv == TOKEN_AT) || !strcasecmp(lex_token(token), KEYWORD_AT)) {

	    /* Step to the next token after the at-token */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv < 0) break;

	    /* Parse auth-hosts part, adding information to the HostSpec_t */
	    rv = aclAuthHostsParse(errp, acf, acc, &hsp);
	    if (rv < 0) break;
	}

	/* Create a new ACClients_t structure for the parsed information */
	csp = (ACClients_t *)MALLOC(sizeof(ACClients_t));
	if (csp == 0) goto err_nomem;

	csp->cl_next = 0;
	csp->cl_user = usp;
	csp->cl_host = hsp;

	/* Add it to the end of the list referenced by clsp */
	while (*clsp != 0) clsp = &(*clsp)->cl_next;
	*clsp = csp;

	/* Need a "," to keep going */
	if (rv != TOKEN_COMMA) break;
    }

    return rv;

  err_nomem:
    eid = ACLERR1000;
    nserrGenerate(errp, ACLERRNOMEM, eid, ACL_Program, 0);
    return ACLERRNOMEM;
}

/*
 * Description (aclAuthHostsParse)
 *
 *	This function parses a list of IP address and/or DNS name
 *	specifications, adding information to the IP and DNS filters
 *	associated with a specified HostSpec_t.  The syntax of the
 *	auth-hosts construct is:
 *
 *	auth-hosts ::= auth-host-elem | "(" auth-host-list ")"
 *				      | "hosts" host-list-name
 *	auth-host-elem ::= auth-ip-spec | auth-dns-spec
 *	auth-ip-spec ::= ipaddr | ipaddr netmask
 *	auth-dns-spec ::= fqdn | dns-suffix
 *	auth-host-list ::= auth-host-elem | auth-host-list "," auth-host-elem
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	acc			- pointer to ACL context object
 *	hspp			- pointer to HostSpec_t pointer
 *
 * Returns:
 *
 *	If successful, the return value is the token type of the token
 *	following the auth-hosts, i.e. either the first token after a
 *	single auth-host-elem or the first token after the closing ")"
 *	of a list of auth-host-elems.  It is the caller's responsibility
 *	to validate this token as a legitimate successor of auth-hosts.
 *	If a parsing error occurs in the middle of auth-hosts, the return
 *	value is ACLERRPARSE, and an error frame is generated if an error
 *	list is provided.  For other kinds of errors a negative error
 *	code (from aclerror.h) is returned.
 */

int aclAuthHostsParse(NSErr_t * errp,
		      ACLFile_t * acf, ACContext_t * acc, HostSpec_t **hspp)
{
    void * token = acf->acf_token;	/* token handle */
    char * tokenstr;			/* token string pointer */
    int islist = 0;			/* true if auth-host-list */
    int fqdn;				/* fully qualified domain name */
    IPAddr_t ipaddr;			/* IP address value */
    IPAddr_t netmask;			/* IP netmask value */
    int arv;				/* alternate result value */
    int rv;				/* result value */
    int eid;				/* error id */
    char linestr[16];			/* line number string buffer */

    rv = acf->acf_ttype;

    /* Are we starting an auth-host-list? */
    if (rv == TOKEN_LPAREN) {

	/* Yes, it appears so */
	islist = 1;

	/* Step token to first auth-host-elem */
	rv = aclGetToken(errp, acf, 0);
	if (rv < 0) goto punt;
    }
    else if (rv == TOKEN_IDENT) {

	/* Could this be "hosts host-list-name"? */
	tokenstr = lex_token(token);

	if (!strcasecmp(tokenstr, KEYWORD_HOSTS)) {

	    /* We don't support lists of host lists yet */
	    if (*hspp != 0) goto err_unshl;

	    /* Get host-list-name */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv < 0) goto punt;

	    if (rv != TOKEN_IDENT) goto err_hlname;

	    tokenstr = lex_token(token);

	    /* Look up the host-list-name in the ACL symbol table */
	    rv = symTableFindSym(acc->acc_stp,
				 tokenstr, ACLSYMHOST, (void **)hspp);
	    if (rv < 0) goto err_undefhl;

	    /* Step to token after the host-list-name */
	    rv = aclGetToken(errp, acf, 0);

	    return rv;
	}
    }

    /* Loop for each auth-host-elem */
    for (rv = acf->acf_ttype; ; rv = aclGetToken(errp, acf, 0)) {

	/* Does this look like an auth-ip-spec? */
	if (rv == TOKEN_NUMBER) {

	    /* Yes, go parse it */
	    rv = aclGetIPAddr(errp, acf, &ipaddr, &netmask);
	    if (rv < 0) goto punt;

	    arv = aclAuthIPAdd(hspp, ipaddr, netmask);
	    if (arv < 0) goto err_ipadd;
	}
	else if ((rv == TOKEN_STAR) || (rv == TOKEN_IDENT)) {

	    /* Get fully qualified DNS name indicator value */
	    fqdn = (rv == TOKEN_IDENT) ? 1 : 0;

	    /* This looks like the start of an auth-dns-spec */
	    rv = aclGetDNSString(errp, acf);
	    if (rv < 0) goto punt;

	    tokenstr = lex_token(token);

	    /* If the DNS spec begins with "*.", strip the "*" */
	    if (tokenstr && (tokenstr[0] == '*') && (tokenstr[1] == '.')) {
		tokenstr += 1;
	    }

	    arv = aclAuthDNSAdd(hspp, tokenstr, fqdn);
	    if (arv < 0) goto err_dnsadd;

	    /* Pick up the next token */
	    rv = aclGetToken(errp, acf, 0);
	}
	else break;

	/* If this is a list, we need a "," to keep going */
	if (!islist || (rv != TOKEN_COMMA)) break;
    }

    /* Were we parsing an auth-host-list? */
    if (islist) {

	/* Yes, check for closing ")" */
	if (acf->acf_ttype != TOKEN_RPAREN) goto err_norp;

	/* Got it.  Step to next token for caller. */
	rv = aclGetToken(errp, acf, 0);
    }

  punt:
    return rv;

  err_unshl:
    eid = ACLERR1100;
    goto err_parse;

  err_hlname:
    eid = ACLERR1120;
    goto err_parse;

  err_undefhl:
    eid = ACLERR1140;
    rv = ACLERRUNDEF;
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program,
		  3, acf->acf_filename, linestr, tokenstr);
    goto punt;

  err_ipadd:
    eid = ACLERR1180;
    rv = arv;
    goto err_ret;

  err_dnsadd:
    eid = ACLERR1200;
    rv = arv;
    goto err_ret;

  err_ret:
    nserrGenerate(errp, rv, eid, ACL_Program, 0);
    goto punt;

  err_norp:
    eid = ACLERR1220;
  err_parse:
    rv = ACLERRPARSE;
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, 2, acf->acf_filename, linestr);
    goto punt;
}

/*
 * Description (aclAuthUsersParse)
 *
 *	This function parses a list of users and groups subject to
 *	authorization, adding the information to a specified UserSpec_t.
 *	The syntax it parses is:
 *
 *	auth-users ::= auth-user-elem | "(" auth-user-list ")"
 *	auth-user-elem ::= username | groupname
 *				    | "all" | "anyone"
 *	auth-user-list ::= auth-user-elem | auth-user-list "," auth-user-elem
 *
 *	If the 'elist' argument is non-null, an auth-user-list will be
 *	accepted without the enclosing parentheses.  Any invalid user
 *	or group names will not cause a fatal error, but will be returned
 *	in an array of strings via 'elist'.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	rlm			- pointer to authentication realm object
 *	uspp			- pointer to UserSpec_t pointer
 *	elist			- pointer to returned pointer to array
 *				  of strings containing invalid user or
 *				  group names (may be null)
 *
 * Returns:
 *
 *	If successful, the return value is the token type of the token
 *	following the auth-users, i.e. either the first token after a
 *	single auth-user-elem or the first token after the closing ")"
 *	of a list of auth-user-elems.  It is the caller's responsibility
 *	to validate this token as a legitimate successor of auth-users.
 *	If a parsing error occurs in the middle of auth-users, the return
 *	value is ACLERRPARSE, and an error frame is generated if an error
 *	list is provided.  For other kinds of errors a negative error
 *	code (from aclerror.h) is returned.
 */

int aclAuthUsersParse(NSErr_t * errp, ACLFile_t * acf,
		      Realm_t * rlm, UserSpec_t **uspp, char ***elist)
{
    void * token = acf->acf_token;	/* token handle */
    char * tokenstr;			/* token string pointer */
    UserSpec_t * usp;			/* user list head structure */
    int islist = 0;			/* true if auth-user-list */
    int inlist = 0;			/* true if UserSpec_t was supplied */
    int any = 0;			/* true if KEYWORD_ANY seen */
    int all = 0;			/* true if KEYWORD_ALL seen */
    int elemcnt = 0;			/* count of auth-user-elem seen */
    int elen = 0;			/* length of evec in (char *) */
    int ecnt = 0;			/* entries used in evec */
    char **evec = 0;			/* list of bad user/group names */
    int rv;				/* result value */
    int eid;				/* error id */
    char linestr[16];			/* line number string buffer */
    int errc = 2;

    usp = *uspp;
    if ((usp != 0) && (usp->us_flags & ACL_USALL)) all = 1;

    if (elist != 0) inlist = 1;
    else {

	/* Check for opening "(" */
	if (acf->acf_ttype == TOKEN_LPAREN) {

	    /* Looks like an auth-user-list */
	    islist = 1;

	    /* Step token to first auth-user-elem */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv < 0) goto punt;
	}
    }

    /* Loop for each auth-user-elem */
    for (rv = acf->acf_ttype; ; rv = aclGetToken(errp, acf, 0)) {

	/* Looking for a user or group identifier */
	if ((rv == TOKEN_IDENT) || (rv == TOKEN_STRING)) {

	    /*
	     * If KEYWORD_ALL or KEYWORD_ANY has already appeared
	     * in this auth-spec, then return an error.
	     */
	    if (all | any) goto err_allany;

	    /* Check for reserved words */
	    tokenstr = lex_token(token);

	    /* KEYWORD_AT begins auth-hosts, but is invalid here */
	    if (!strcasecmp(tokenstr, KEYWORD_AT)) break;

	    /* Check for special group names */
	    if (!strcasecmp(tokenstr, KEYWORD_ANY)) {

		/*
		 * Any user, with no authentication needed.  This can
		 * only appear once in an auth-spec, and cannot be used
		 * in combination with KEYWORD_ALL (or any other user or
		 * group identifiers, but that will get checked before
		 * we return).
		 */

		if ((elemcnt > 0) || (usp != 0)) goto err_any;
		any = 1;
	    }
	    else if (!strcasecmp(tokenstr, KEYWORD_ALL)) {

		/*
		 * Any authenticated user.  This can only appear once in
		 * an auth-spec, and cannot be used in combination with
		 * KEYWORD_ANY (or any other user or group identifiers,
		 * but that will get checked before we return).
		 */

		if (elemcnt > 0) goto err_all;

		/* Create a UserSpec_t structure if we haven't got one yet */
		if (usp == 0) {
		    usp = aclUserSpecCreate();
		    if (usp == 0) goto err_nomem1;
		    *uspp = usp;
		}

		usp->us_flags |= ACL_USALL;
		all = 1;
	    }
	    else {

		/* Create a UserSpec_t structure if we haven't got one yet */
		if (usp == 0) {
		    usp = aclUserSpecCreate();
		    if (usp == 0) goto err_nomem2;
		    *uspp = usp;
		}

		/* This should be a user or group name */
		rv = aclAuthNameAdd(errp, usp, rlm, tokenstr);
		if (rv <= 0) {

		    /* The name was not found in the authentication DB */
		    if (elist != 0) {
			if (evec == 0) {
			    evec = (char **)MALLOC(4*sizeof(char *));
			    evec[0] = 0;
			    ecnt = 1;
			    elen = 4;
			}
			else if (ecnt >= elen) {
			    elen += 4;
			    evec = (char **)REALLOC(evec, elen*sizeof(char *));
			}
			evec[ecnt-1] = STRDUP(tokenstr);
			evec[ecnt] = 0;
			++ecnt;
			
		    }
		    else if (rv < 0) goto err_badgun;
		}

		/* Don't allow duplicate names */
		if (rv & ANA_DUP) {
		    if (elist == 0) goto err_dupgun;
		}
	    }

	    /* Count number of auth-user-elems seen */
	    elemcnt += 1;

	    /* Get the token after the auth-user-elem */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv < 0) goto punt;
	}

	/* If this is a list, we need a "," to keep going */
	if (!(islist | inlist) || (rv != TOKEN_COMMA)) break;
    }

    /* Were we parsing an auth-user-list? */
    if (islist) {

	/* Yes, check for closing ")" */
	if (acf->acf_ttype != TOKEN_RPAREN) goto err_norp;

	/* Got it.  Step to next token for caller. */
	rv = aclGetToken(errp, acf, 0);
	if (rv < 0) goto punt;
    }

    /*
     * If we didn't see any auth-user-elems, then the auth-user we were
     * called to parse is missing.  We will forgive and forget if the
     * current token is a comma, however, so as to allow empty auth-specs.
     */
    if ((elemcnt <= 0) && (rv != TOKEN_COMMA)) {
	goto err_noelem;
    }

  punt:
    /* Return list of bad names if indicated */
    if (elist != 0) *elist = evec;

    return rv;

  err_badgun:
    /* Encountered an unknown user or group name */
    eid = ACLERR1360;
    rv = ACLERRUNDEF;
    goto err_retgun;

  err_dupgun:
    /* A user or group name was specified multiple times */
    eid = ACLERR1380;
    rv = ACLERRDUPSYM;
    goto err_retgun;

  err_retgun:
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program,
		  3, acf->acf_filename, linestr, tokenstr);
    goto punt;

  err_norp:
    /* Missing ")" */
    eid = ACLERR1400;
    goto err_parse;

  err_noelem:
    eid = ACLERR1420;
    goto err_parse;

  err_all:
    eid = ACLERR1440;
    goto err_parse;

  err_any:
    eid = ACLERR1460;
    goto err_parse;

  err_allany:
    eid = ACLERR1480;
    goto err_parse;

  err_nomem1:
    eid = ACLERR1500;
    rv = ACLERRNOMEM;
    errc = 0;
    goto err_ret;

  err_nomem2:
    eid = ACLERR1520;
    rv = ACLERRNOMEM;
    errc = 0;
    goto err_ret;

  err_parse:
    rv = ACLERRPARSE;
  err_ret:
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, errc, acf->acf_filename, linestr);
    goto punt;
}

/*
 * Description (aclDirectivesParse)
 *
 *	This function parses the directives inside an ACL definition.
 *	The syntax for a directive list is:
 *
 *	dir-list ::= directive | dir-list ";" directive
 *	directive ::= auth-directive | access-directive | exec-directive
 *	auth-directive ::= dir-force "authenticate" ["in" realm-spec]
 *	access-directive ::= dir-force dir-access auth-list
 *	exec-directive ::= dir-force "execute" ["if" exec-optlist]
 *	exec-optlist ::= exec-condition | exec-optlist "," exec-condition
 *	exec-condition ::= dir-access | "authenticate"
 *	dir-force ::= "Always" | "Default"
 *	dir-access ::= "allow" | "deny"
 *
 *	See aclAuthListParse() for auth-list syntax.
 *	See aclRealmSpecParse() for realm-spec syntax.
 *
 *	The caller provides a pointer to an ACL structure, which is
 *	built up with new information as directives are parsed.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	acl			- pointer to ACL structure
 *
 * Returns:
 *
 *	If successful, the return value is the token type of the token
 *	following the directive list, i.e. the first token which is not
 *	recognized as the start of a directive.  It is the caller's
 *	responsibility to validate this token as a legitimate terminator
 *	of a directive list.  If a parsing error occurs in the middle of
 *	a directive, the return value is ACLERRPARSE, and an error frame
 *	is generated if an error list is provided.  For other kinds of
 *	errors a negative error code (from aclerror.h) is returned.
 */

int aclDirectivesParse(NSErr_t * errp, ACLFile_t * acf, ACL_t * acl)
{
    void * token = acf->acf_token;	/* token handle */
    char * tokenstr;			/* token string */
    Realm_t * rlm = 0;			/* current realm pointer */
    ACDirective_t * acd;		/* directive pointer */
    int action;				/* directive action code */
    int flags;				/* directive action flags */
    int arv;				/* alternate return value */
    int rv;				/* result value */
    int eid;				/* error id */
    char linestr[16];			/* line number string buffer */

    /* Look for top-level directives */
    for (rv = acf->acf_ttype; ; rv = aclGetToken(errp, acf, 0)) {

	action = 0;
	flags = 0;

	/* Check for beginning of directive */
	if (rv == TOKEN_IDENT) {

	    /* Check identifier for directive dir-force keywords */
	    tokenstr = lex_token(token);

	    if (!strcasecmp(tokenstr, KEYWORD_DEFAULT)) {
		flags = ACD_DEFAULT;
	    }
	    else if (!strcasecmp(tokenstr, "always")) {
		flags = ACD_ALWAYS;
	    }
	    else break;

	    /*
	     * Now we're looking for dir-access, "authenticate",
	     * or "execute".
	     */
	    rv = aclGetToken(errp, acf, 0);

	    /* An identifier would be nice ... */
	    if (rv != TOKEN_IDENT) goto err_access;

	    tokenstr = lex_token(token);

	    if (!strcasecmp(tokenstr, KEYWORD_AUTH)) {

		/* process auth-directive */
		action = ACD_AUTH;

		/* Create a new directive object */
		acd = aclDirectiveCreate();
		if (acd == 0) goto err_nomem1;

		/* Get the next token after KEYWORD_AUTH */
		rv = aclGetToken(errp, acf, 0);
		if (rv < 0) break;

		/* Could we have "in" realm-spec here? */
		if (rv == TOKEN_IDENT) {

		    tokenstr = lex_token(token);

		    if (!strcasecmp(tokenstr, KEYWORD_IN)) {

			/* Get the next token after KEYWORD_IN */
			rv = aclGetToken(errp, acf, 0);
			if (rv < 0) break;

			/* Parse the realm-spec */
			rv = aclRealmSpecParse(errp, acf, acl->acl_acc,
					       &acd->acd_auth.au_realm);
			if (rv < 0) break;

			/* Set current realm */
			if (acd->acd_auth.au_realm != 0) {

			    /* Close database in current realm if any */
			    if (rlm && rlm->rlm_authdb) {
				(*rlm->rlm_aif->aif_close)(rlm->rlm_authdb, 0);
				rlm->rlm_authdb = 0;
			    }

			    rlm = &acd->acd_auth.au_realm->rs_realm;
			}
		    }
		}

		/* Add this directive to the ACL */
		acd->acd_action = action;
		acd->acd_flags = flags;

		arv = aclDirectiveAdd(acl, acd);
		if (arv < 0) goto err_diradd1;
	    }
	    else if (!strcasecmp(tokenstr, KEYWORD_EXECUTE)) {

		/* process exec-directive */
		action = ACD_EXEC;

		/* Create a new directive object */
		acd = aclDirectiveCreate();
		if (acd == 0) goto err_nomem3;

		/* Get the next token after KEYWORD_EXECUTE */
		rv = aclGetToken(errp, acf, 0);
		if (rv < 0) break;

		/* Could we have "if" exec-optlist here? */
		if (rv == TOKEN_IDENT) {

		    tokenstr = lex_token(token);

		    if (!strcasecmp(tokenstr, KEYWORD_IF)) {

			for (;;) {

			    /* Get the next token after KEYWORD_IF or "," */
			    rv = aclGetToken(errp, acf, 0);
			    if (rv < 0) break;

			    /*
			     * Looking for "allow", "deny", or "authenticate"
			     */
			    if (rv == TOKEN_IDENT) {

				tokenstr = lex_token(token);

				if (!strcasecmp(tokenstr, KEYWORD_ALLOW)) {
				    flags |= ACD_EXALLOW;
				}
				else if (!strcasecmp(tokenstr, KEYWORD_DENY)) {
				    flags |= ACD_EXDENY;
				}
				else if (!strcasecmp(tokenstr, KEYWORD_AUTH)) {
				    flags |= ACD_EXAUTH;
				}
				else goto err_exarg;
			    }

			    /* End of directive if no comma */
			    rv = aclGetToken(errp, acf, 0);
			    if (rv < 0) break;

			    if (rv != TOKEN_COMMA) break;
			}
		    }
		}
		else flags = (ACD_EXALLOW|ACD_EXDENY|ACD_EXAUTH);

		if (rv < 0) break;

		/* Add this directive to the ACL */
		acd->acd_action = action;
		acd->acd_flags = flags;

		arv = aclDirectiveAdd(acl, acd);
		if (arv < 0) goto err_diradd3;
	    }
	    else {

		/* process access-directive */

		if (!strcasecmp(tokenstr, KEYWORD_ALLOW)) {
		    action = ACD_ALLOW;
		}
		else if (!strcasecmp(tokenstr, KEYWORD_DENY)) {
		    action = ACD_DENY;
		}
		else goto err_acctype;

		/* Get the next token after dir-access */
		rv = aclGetToken(errp, acf, 0);

		/* Create a new directive object */
		acd = aclDirectiveCreate();
		if (acd == 0) goto err_nomem2;

		/* Parse a list of auth-specs */
		rv = aclAuthListParse(errp, acf, acl->acl_acc, rlm,
				      &acd->acd_cl);
		if (rv < 0) break;

		/* Add this directive to the ACL */
		acd->acd_action = action;
		acd->acd_flags = flags;

		arv = aclDirectiveAdd(acl, acd);
		if (arv < 0) goto err_diradd2;
	    }
	}

	/* Need a ";" to keep going */
	if (rv != TOKEN_EOS) break;
    }

  punt:
    /* Close database in current realm if any */
    if (rlm && rlm->rlm_authdb) {
	(*rlm->rlm_aif->aif_close)(rlm->rlm_authdb, 0);
	rlm->rlm_authdb = 0;
    }

    return rv;

  err_access:
    /* dir-access not present */
    eid = ACLERR1600;
    rv = ACLERRPARSE;
    goto err_ret;

  err_acctype:
    /* dir-access identifier is invalid */
    eid = ACLERR1620;
    rv = ACLERRPARSE;
    goto err_ret;

  err_diradd1:
    eid = ACLERR1640;
    rv = arv;
    tokenstr = 0;
    goto err_ret;

  err_diradd2:
    eid = ACLERR1650;
    rv = arv;
    tokenstr = 0;
    goto err_ret;

  err_nomem1:
    eid = ACLERR1660;
    rv = ACLERRNOMEM;
    tokenstr = 0;
    goto err_ret;

  err_nomem2:
    eid = ACLERR1680;
    rv = ACLERRNOMEM;
    tokenstr = 0;
    goto err_ret;

  err_nomem3:
    eid = ACLERR1685;
    rv = ACLERRNOMEM;
    tokenstr = 0;
    goto err_ret;

  err_diradd3:
    eid = ACLERR1690;
    rv = arv;
    tokenstr = 0;
    goto err_ret;

  err_exarg:
    eid = ACLERR1695;
    rv = ACLERRSYNTAX;
    goto err_ret;

  err_ret:
    sprintf(linestr, "%d", acf->acf_lineno);
    if (tokenstr) {
	nserrGenerate(errp, rv, eid, ACL_Program,
		      3, acf->acf_filename, linestr, tokenstr);
    }
    else {
	nserrGenerate(errp, rv, eid, ACL_Program,
		      2, acf->acf_filename, linestr);
    }
    goto punt;
}

/*
 * Description (aclACLParse)
 *
 *	This function parses a data stream containing ACL definitions,
 *	and builds a representation of the ACLs in memory.  Each ACL
 *	has a user-specified name, and a pointer to the ACL structure
 *	is stored under the name in a symbol table provided by the caller.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	acc			- pointer to ACContext_t structure
 *	flags			- bit flags (unused - must be zero)
 *
 * Returns:
 *
 *	The return value is zero if the stream is parsed successfully.
 *	Otherwise it is a negative error code (ACLERRxxxx - see aclerror.h),
 *	and an error frame will be generated if an error list is provided.
 */

int aclACLParse(NSErr_t * errp, ACLFile_t * acf, ACContext_t * acc, int flags)
{
    void * token = acf->acf_token;	/* handle for current token */
    char * tokenstr;			/* current token string */
    char * aclname;			/* ACL name string */
    ACL_t * aclp;			/* pointer to ACL structure */
    int rv;				/* result value */
    int eid;				/* error id value */
    char linestr[16];			/* line number string buffer */

    /* Look for top-level statements */
    for (;;) {

	/* Get a token to begin a statement */
	rv = aclGetToken(errp, acf, 0);

	/* An identifier would be nice ... */
	if (rv != TOKEN_IDENT) {

	    /* Empty statements are ok, if pointless */
	    if (rv == TOKEN_EOS) continue;

	    /* EOF is valid here */
	    if (rv == TOKEN_EOF) break;

	    /* Anything else is unacceptable */
	    goto err_nostmt;
	}

	/* Check identifier for statement keywords */
	tokenstr = lex_token(token);

	if (!strcasecmp(tokenstr, KEYWORD_ACL)) {

	    /* ACL name rights-list { acl-def-list }; */

	    /* Get the name of the ACL */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv != TOKEN_IDENT) goto err_aclname;
	    aclname = lex_token(token);

	    /* Create the ACL structure */
	    rv = aclCreate(errp, acc, aclname, &aclp);
	    if (rv < 0) goto punt;

	    /* Get the next token after the ACL name */
	    rv = aclGetToken(errp, acf, 0);

	    /* Parse the rights specification */
	    rv = aclRightsParse(errp, acf, acc, &aclp->acl_rights);

	    /* Want a "{" to open the ACL directive list */
	    if (rv != TOKEN_LBRACE) {
		if (rv < 0) goto punt;
		goto err_aclopen;
	    }

	    /* Get the first token in the ACL directive list */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv < 0) goto punt;

	    /* Parse the ACL directive list */
	    rv = aclDirectivesParse(errp, acf, aclp);

	    /* Want a "}" to close the ACL directive list */
	    if (rv != TOKEN_RBRACE) {
		if (rv < 0) goto punt;
		goto err_aclclose;
	    }
	}
	else if (!strcasecmp(tokenstr, KEYWORD_INCLUDE)) {
	    /* Include "filename"; */
	}
	else if (!strcasecmp(tokenstr, KEYWORD_REALM)) {
	    /* Realm name realm-spec */
	}
	else if (!strcasecmp(tokenstr, KEYWORD_RIGHTS)) {
	    /* Rights name rights-def; */
	}
	else if (!strcasecmp(tokenstr, KEYWORD_HOSTS)) {
	    /* Hosts name auth-hosts; */
	}
	else goto err_syntax;
    }

    return 0;

  err_nostmt:
    eid = ACLERR1700;
    rv = ACLERRPARSE;
    goto err_ret;

  err_aclname:
    eid = ACLERR1720;
    rv = ACLERRPARSE;
    goto err_ret;

  err_aclopen:
    eid = ACLERR1740;
    rv = ACLERRPARSE;
    goto err_ret;

  err_aclclose:
    eid = ACLERR1760;
    rv = ACLERRPARSE;
    goto err_ret;

  err_ret:
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, 2, acf->acf_filename, linestr);
    goto punt;

  err_syntax:
    eid = ACLERR1780;
    rv = ACLERRPARSE;
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program,
		  3, acf->acf_filename, linestr, tokenstr);

  punt:
    return rv;
}

/*
 * Description (aclFileClose)
 *
 *	This function closes an ACL file previously opened by aclFileOpen(),
 *	and frees any associated data structures.
 *
 * Arguments:
 *
 *	acf			- pointer to ACL file information
 *	flags			- bit flags (unused - must be zero)
 */

void aclFileClose(ACLFile_t * acf, int flags)
{
    if (acf != 0) {

	/* Destroy the associated lexer stream if any */
	if (acf->acf_lst != 0) {
	    lex_stream_destroy(acf->acf_lst);
	}

	/* Close the file if it's open */
	if (acf->acf_fd != SYS_ERROR_FD) {
	    system_fclose(acf->acf_fd);
	}

	/* Destroy any associated token */
	if (acf->acf_token != 0) {
	    lex_token_destroy(acf->acf_token);
	}

	/* Free the filename string if any */
	if (acf->acf_filename != 0) {
	    FREE(acf->acf_filename);
	}

	/* Free the ACLFile_t structure */
	FREE(acf);
    }
}

/*
 * Description (aclFileOpen)
 *
 *	This function opens a specified filename and creates a structure
 *	to contain information about the file during parsing.  This
 *	includes a handle for a LEX data stream for the file.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	filename		- name of file to be opened
 *	flags			- bit flags (unused - must be zero)
 *	pacf			- pointer to returned ACLFile_t pointer
 *
 * Returns:
 *
 *	The return value is zero if the file is opened successfully, and
 *	a pointer to the ACLFile_t is returned in the location specified
 *	by 'pacf'.  Otherwise a negative error code (ACLERRxxxx - see
 *	aclerror.h) is returned, and an error frame will be generated if
 *	an error list is provided.
 */

int aclFileOpen(NSErr_t * errp,
		char * filename, int flags, ACLFile_t **pacf)
{
    ACLFile_t * acf;		/* pointer to ACL file structure */
    int rv;			/* return value */
    int eid;			/* error identifier */
    char * errmsg;		/* system error message string */

    *pacf = 0;

    /* Allocate the ACLFile_t structure */
    acf = (ACLFile_t *)MALLOC(sizeof(ACLFile_t));
    if (acf == 0) goto err_nomem1;

    memset((void *)acf, 0, sizeof(ACLFile_t));
    acf->acf_filename = STRDUP(filename);
    acf->acf_lineno = 1;
    acf->acf_flags = flags;

    /* Create a LEX token object */
    rv = lex_token_new((pool_handle_t *)0, 32, 8, &acf->acf_token);
    if (rv < 0) goto err_nomem2;

    /* Open the file */
    acf->acf_fd = system_fopenRO(acf->acf_filename);
    if (acf->acf_fd == SYS_ERROR_FD) goto err_open;

    /* Create a LEX stream for the file */
    acf->acf_lst = lex_stream_create(aclStreamGet,
				     (void *)acf->acf_fd, 0, 8192);
    if (acf->acf_lst == 0) goto err_nomem3;

    *pacf = acf;
    return 0;

  err_open:				/* file open error */
    rv = ACLERROPEN;
    eid = ACLERR1900;
    errmsg = system_errmsg();
    nserrGenerate(errp, rv, eid, ACL_Program, 2, filename, errmsg);
    goto punt;

  err_nomem1:				/* MALLOC of ACLFile_t failed */
    rv = ACLERRNOMEM;
    eid = ACLERR1920;
    goto err_mem;

  err_nomem2:				/* lex_token_new() failed */
    rv = ACLERRNOMEM;
    eid = ACLERR1940;
    goto err_mem;

  err_nomem3:				/* lex_stream_create() failed */
    system_fclose(acf->acf_fd);
    rv = ACLERRNOMEM;
    eid = ACLERR1960;

  err_mem:
    nserrGenerate(errp, rv, eid, ACL_Program, 0);
    goto punt;

  punt:
    return rv;
}

/*
 * Description (aclGetDNSString)
 *
 *	This function parses a DNS name specification, which consists
 *	of a sequence of DNS name components separated by ".".  Each
 *	name component must start with a letter, and contains only
 *	letters, digits, and hyphens.  An exception is that the first
 *	component may be the wildcard indicator, "*".  This function
 *	assumes that the current token already contains a TOKEN_STAR
 *	or TOKEN_IDENT.  The complete DNS name specification is
 *	returned as the current token string.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *
 * Returns:
 *
 *	The character terminating the DNS name specification is returned
 *	as the function value.  The current token type is unchanged, but
 *	the string associated with the current token contains the
 *	complete DNS name specification.  An error is indicated by a
 *	negative return value, and an error frame is generated if an
 *	error list is provided.
 */

int aclGetDNSString(NSErr_t * errp, ACLFile_t * acf)
{
    LEXStream_t * lst = acf->acf_lst;	/* LEX stream handle */
    void * token = acf->acf_token;	/* LEX token handle */
    int rv;				/* result value */
    int eid;				/* error id value */
    char linestr[16];			/* line number string buffer */

    /* The current token should be TOKEN_STAR or TOKEN_IDENT */
    rv = acf->acf_ttype;

    if ((rv != TOKEN_STAR) && (rv != TOKEN_IDENT)) goto err_dns1;

    /* Loop to parse [ "." dns-component ]+ */
    for (;;) {

	/* Try to step over a "." */
	rv = lex_next_char(lst, aclChTab, 0);

	/* End of DNS string if there's not one there */
	if (rv != '.') break;

	/* Append the "." to the token string */
	(void)lex_token_append(token, 1, ".");

	/* Advance the input stream past the "." */
	rv = lex_next_char(lst, aclChTab, CCM_SPECIAL);

	/* Next we want to see a letter */
	rv = lex_next_char(lst, aclChTab, 0);

	/* Error if it's not there */
	if (!lex_class_check(aclChTab, rv, CCM_LETTER)) goto err_dns2;

	/* Append a string of letters, digits, hyphens to token */
	rv = lex_scan_over(lst, aclChTab, (CCM_LETTER|CCM_DIGIT|CCM_HYPHEN),
			   token);
	if (rv < 0) goto err_dns3;
    }

  punt:
    return rv;

  err_dns1:
    eid = ACLERR2100;
    rv = ACLERRPARSE;
    goto err_ret;

  err_dns2:
    eid = ACLERR2120;
    rv = ACLERRPARSE;
    goto err_ret;

  err_dns3:
    eid = ACLERR2140;
    rv = ACLERRPARSE;
    goto err_ret;

  err_ret:
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, 2, acf->acf_filename, linestr);
    goto punt;
}

int aclGetFileSpec(NSErr_t * errp, ACLFile_t * acf, int flags)
{
    LEXStream_t * lst = acf->acf_lst;	/* LEX stream handle */
    void * token = acf->acf_token;	/* LEX token handle */
    char * tokenstr;			/* token string pointer */
    int rv;				/* result value */
    int eid;				/* error id value */
    char linestr[16];			/* line number string buffer */

    /* Skip whitespace */
    rv = lex_skip_over(lst, aclChTab, CCM_WS);
    if (rv < 0) goto err_lex1;

    /* Begin a new token string */
    rv = lex_token_start(token);

    rv = lex_scan_over(lst, aclChTab, CCM_FILENAME, token);
    if (rv < 0) goto err_lex2;

    tokenstr = lex_token(token);

    if (!tokenstr || !*tokenstr) goto err_nofn;

  punt:
    return rv;

  err_lex1:
    eid = ACLERR2900;
    goto err_parse;

  err_lex2:
    eid = ACLERR2920;
    goto err_parse;

  err_nofn:
    eid = ACLERR2940;

  err_parse:
    rv = ACLERRPARSE;
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, 2, acf->acf_filename, linestr);
    goto punt;
}

/*
 * Description (aclGetIPAddr)
 *
 *	This function retrieves an IP address specification from a given
 *	input stream.  The specification consists of an IP address expressed
 *	in the standard "." notation, possibly followed by whitespace and a
 *	netmask, also in "." form.  The IP address and netmask values are
 *	returned.  If no netmask is specified, a default value of 0xffffffff
 *	is returned.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	pip			- pointer to returned IP address value
 *	pmask			- pointer to returned IP netmask value
 *
 * Returns:
 *
 *	If successful, the return value identifies the type of the token
 *	following the IP address specification. This token type value is
 *	also returned in acf_ttype.  An error is indicated by a negative
 *	error code (ACLERRxxxx - see aclerror.h), and an error frame will
 *	be generated if an error list is provided. The token type code in
 *	acf_ttype is TOKEN_ERROR when an error code is returned.
 */

int aclGetIPAddr(NSErr_t * errp,
		 ACLFile_t * acf, IPAddr_t * pip, IPAddr_t * pmask)
{
    LEXStream_t * lst = acf->acf_lst;	/* LEX stream handle */
    void * token = acf->acf_token;	/* LEX token handle */
    char * tokenstr;			/* token string pointer */
    IPAddr_t ipaddr;			/* IP address */
    IPAddr_t netmask;			/* IP netmask */
    int dotcnt;				/* count of '.' seen in address */
    int rv;				/* result value */
    int eid;				/* error id value */
    char linestr[16];			/* line number string buffer */

    /* Set default return values */
    *pip = 0;
    *pmask = 0xffffffff;

    rv = acf->acf_ttype;

    /* The current token must be a number */
    if (rv != TOKEN_NUMBER) {

	/* No IP address present */
	return rv;
    }

    /* Assume no netmask */
    netmask = 0xffffffff;

    for (dotcnt = 0;;) {

	/* Append digits and letters to the current token */
	rv = lex_scan_over(lst, aclChTab, (CCM_DIGIT|CCM_LETTER), token);
	if (rv < 0) goto err_lex1;

	/* Stop when no "." follows the digits and letters */
	if (rv != '.') break;

	/* Stop if we've already seen three "." */
	if (++dotcnt > 3) break;

	/* Advance past the "." */
	(void)lex_next_char(lst, aclChTab, CCM_SPECIAL);

	/* Check the next character for a "*" */
	rv = lex_next_char(lst, aclChTab, 0);
	if (rv == '*') {

	    /* Advance past the "*" */
	    (void)lex_next_char(lst, aclChTab, CCM_SPECIAL);

	    netmask <<= ((4-dotcnt)*8);
	    netmask = htonl(netmask);

	    while (dotcnt < 4) {
		(void)lex_token_append(token, 2, ".0");
		++dotcnt;
	    }
	    break;
	}
	else {
	    /* Append the "." to the token string */
	    (void)lex_token_append(token, 1, ".");
	}
    }

    /* Get a pointer to the token string */
    tokenstr = lex_token(token);

    /* A NULL pointer or an empty string is an error */
    if (!tokenstr || !*tokenstr) goto err_noip;
	
    /* Convert IP address to binary */
    ipaddr = inet_addr(tokenstr);
    if (ipaddr == (unsigned long)-1) goto err_badip;

    /* Skip whitespace */
    rv = lex_skip_over(lst, aclChTab, CCM_WS);
    if (rv < 0) goto err_lex2;

    /* A digit is the start of a netmask */
    if ((netmask == 0xffffffff) && lex_class_check(aclChTab, rv, CCM_DIGIT)) {

	/* Initialize token for network mask */
	rv = lex_token_start(token);

	for (dotcnt = 0;;) {

	    /* Collect token including digits, letters, and periods */
	    rv = lex_scan_over(lst, aclChTab, (CCM_DIGIT|CCM_LETTER), token);
	    if (rv < 0) goto err_lex3;

	    /* Stop when no "." follows the digits and letters */
	    if (rv != '.') break;

	    /* Stop if we've already seen three "." */
	    if (++dotcnt > 3) break;

	    /* Append the "." to the token string */
	    (void)lex_token_append(token, 1, ".");

	    /* Advance past the "." */
	    (void)lex_next_char(lst, aclChTab, CCM_SPECIAL);
	}

	/* Get a pointer to the token string */
	tokenstr = lex_token(token);

	/* A NULL pointer or an empty string is an error */
	if (!tokenstr || !*tokenstr) goto err_nonm;

	/* Convert netmask to binary. */
	netmask = inet_addr(tokenstr);
	if (netmask == (unsigned long)-1) {
	    
	    /*
	     * Unfortunately inet_addr() doesn't distinguish between an
	     * error and a valid conversion of "255.255.255.255".  So
	     * we check for it explicitly.  Too bad if "0xff.0xff.0xff.0xff"
	     * is specified.  Don't do that!
	     */
	    if (strcmp(tokenstr, "255.255.255.255")) goto err_badnm;
	}
    }

    /* Return the IP address and netmask in host byte order */
    *pip = ntohl(ipaddr);
    *pmask = ntohl(netmask);

    /* Get the token following the IP address (and netmask) */
    rv = aclGetToken(errp, acf, 0);

  punt:
    acf->acf_ttype = (rv < 0) ? TOKEN_ERROR : rv;
    return rv;

  err_lex1:
    eid = ACLERR2200;
    rv = ACLERRPARSE;
    goto err_ret;

  err_lex2:
    eid = ACLERR2220;
    rv = ACLERRPARSE;
    goto err_ret;

  err_lex3:
    eid = ACLERR2240;
    rv = ACLERRPARSE;
    goto err_ret;

  err_noip:
    eid = ACLERR2260;
    rv = ACLERRPARSE;
    goto err_ret;

  err_badip:
    eid = ACLERR2280;
    rv = ACLERRPARSE;
    goto err_ret;

  err_nonm:
    eid = ACLERR2300;
    rv = ACLERRPARSE;
    goto err_ret;

  err_badnm:
    eid = ACLERR2320;
    rv = ACLERRPARSE;
    goto err_ret;

  err_ret:
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, 2, acf->acf_filename, linestr);
    goto punt;
}

/*
 * Description (aclGetToken)
 *
 *	This function retrieves the next token in an ACL definition file.
 *	It skips blank lines, comments, and white space.  It updates
 *	the current line number as newlines are encountered.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	flags			- bit flags:
 *				  AGT_NOSKIP - don't skip leading whitespace
 *				  AGT_APPEND - append to token buffer
 *					       (else start new token)
 *
 * Returns:
 *
 *	The return value is a code identifying the next token if successful.
 *	This token type value is also returned in acf_ttype.  An error
 *	is indicated by a negative error code (ACLERRxxxx - see aclerror.h),
 *	and an error frame will be generated if an error list is provided.
 *	The token type code in acf_ttype is TOKEN_ERROR when an error code
 *	is returned.
 */

int aclGetToken(NSErr_t * errp, ACLFile_t * acf, int flags)
{
    LEXStream_t * lst = acf->acf_lst;	/* LEX stream handle */
    void * token = acf->acf_token;	/* LEX token handle */
    int dospecial = 0;			/* handle CCM_SPECIAL character */
    int tv;				/* token value */
    int rv;				/* result value */
    int eid;				/* error id */
    char spech;
    char linestr[16];			/* line number string buffer */

    /* Begin a new token, unless AGT_APPEND is set */
    if (!(flags & AGT_APPEND)) {
	rv = lex_token_start(token);
    }

    /* Loop to read file */
    tv = 0;
    do {

	/*
	 * If the AGT_NOSKIP flag is not set, skip whitespace (but not
	 * newline).  If the flag is set, just get the next character.
	 */
	rv = lex_skip_over(lst, aclChTab, (flags & AGT_NOSKIP) ? 0 : CCM_WS);
	if (rv <= 0) {
	    if (rv < 0) goto err_lex1;

	    /* Exit loop if EOF */
	    if (rv == 0) {
		tv = TOKEN_EOF;
		break;
	    }
	}

	/* Analyze character after whitespace */
	switch (rv) {

	  case '\n':		/* newline */

	    /* Keep count of lines as we're skipping whitespace */
	    acf->acf_lineno += 1;
	    (void)lex_next_char(lst, aclChTab, CCM_NL);
	    break;

	  case '#':		/* Beginning of comment */

	    /* Skip to a newline if so */
	    rv = lex_skip_to(lst, aclChTab, CCM_NL);
	    break;

	  case ';':		/* End of statement */
	    tv = TOKEN_EOS;
	    dospecial = 1;
	    break;

	  case '@':		/* at sign */
	    tv = TOKEN_AT;
	    dospecial = 1;
	    break;

	  case '+':		/* plus sign */
	    tv = TOKEN_PLUS;
	    dospecial = 1;
	    break;

	  case '*':		/* asterisk */
	    tv = TOKEN_STAR;
	    dospecial = 1;
	    break;

	  case '.':		/* period */
	    tv = TOKEN_PERIOD;
	    dospecial = 1;
	    break;

	  case ',':		/* comma */
	    tv = TOKEN_COMMA;
	    dospecial = 1;
	    break;

	  case '(':		/* left parenthesis */
	    tv = TOKEN_LPAREN;
	    dospecial = 1;
	    break;

	  case ')':		/* right parenthesis */
	    tv = TOKEN_RPAREN;
	    dospecial = 1;
	    break;

	  case '{':		/* left brace */
	    tv = TOKEN_LBRACE;
	    dospecial = 1;
	    break;

	  case '}':		/* right brace */
	    tv = TOKEN_RBRACE;
	    dospecial = 1;
	    break;

	  case '\"':		/* double quote */
	  case '\'':		/* single quote */

	    /* Append string contents to token buffer */
	    rv = lex_scan_string(lst, token, 0);
	    tv = TOKEN_STRING;
	    break;

	  default:

	    /* Check for identifier, beginning with a letter */
	    if (lex_class_check(aclChTab, rv, CCM_LETTER)) {

		/* Append valid identifier characters to token buffer */
		rv = lex_scan_over(lst, aclChTab, CCM_IDENT, token);
		tv = TOKEN_IDENT;
		break;
	    }

	    /* Check for a number, beginning with a digit */
	    if (lex_class_check(aclChTab, rv, CCM_DIGIT)) {
		char digit;

		/* Save the first digit */
		digit = (char)rv;

		/* Append the first digit to the token */
		rv = lex_token_append(token, 1, &digit);

		/* Skip over the first digit */
		rv = lex_next_char(lst, aclChTab, CCM_DIGIT);

		/* If it's '0', we might have "0x.." */
		if (rv == '0') {

		    /* Pick up the next character */
		    rv = lex_next_char(lst, aclChTab, 0);

		    /* Is it 'x'? */
		    if (rv == 'x') {

			/* Yes, append it to the token */
			digit = (char)rv;
			rv = lex_token_append(token, 1, &digit);

			/* Step over it */
			rv = lex_next_char(lst, aclChTab, CCM_LETTER);
		    }
		}
		/* Get more digits, if any */
		rv = lex_scan_over(lst, aclChTab, CCM_DIGIT, token);
		tv = TOKEN_NUMBER;
		break;
	    }

	    /* Unrecognized character */

	    spech = *lst->lst_cp;
	    lex_token_append(token, 1, &spech);
	    lst->lst_cp += 1;
	    lst->lst_len -= 1;
	    tv = TOKEN_HUH;
	    break;
	}

	/* Handle CCM_SPECIAL character? */
	if (dospecial) {

	    /* Yes, clear the flag for next time */
	    dospecial = 0;

	    /* Get the character and advance past it */
	    rv = lex_next_char(lst, aclChTab, CCM_SPECIAL);

	    /* Append the character to the token buffer */
	    spech = (char)rv;
	    (void)lex_token_append(token, 1, &spech);
	}
    }
    while ((tv == 0) && (rv > 0));

    if (rv < 0) {
	tv = TOKEN_ERROR;
    }
    else rv = tv;

    acf->acf_ttype = tv;
    return rv;

  err_lex1:
    rv = ACLERRPARSE;
    eid = ACLERR2400;

    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, 2, acf->acf_filename, linestr);

    acf->acf_ttype = TOKEN_ERROR;
    return rv;
}

/*
 * Description (aclParseInit)
 *
 *	This function is called to initialize the ACL parser.  It
 *	creates a LEX character class table to assist in parsing.
 *
 * Arguments:
 *
 *	None.
 *
 * Returns:
 *
 *	If successful, the return value is zero.  An error is indicated
 *	by a negative return value.
 */

int aclParseInit()
{
    int rv;				/* result value */

    /* Have we created the character class table yet? */
    if (aclChTab == 0) {

	/* No, initialize character classes for lexer processing */
	rv = lex_class_create(classc, classv, &aclChTab);
	if (rv < 0) goto err_nomem;
    }

    return 0;

  err_nomem:
    return ACLERRNOMEM;
}

/*
 * Description (aclRealmSpecParse)
 *
 *	This function parses an authentication realm specification.  An
 *	authentication realm includes an authentication database and
 *	an authentication method.  The syntax of a realm-spec is:
 *
 *	realm-spec ::= "{" realm-directive-list "}" | "realm" realm-name
 *	realm-directive-list ::= realm-directive |
 *				 realm-directive-list ";" realm-directive
 *	realm-directive ::= realm-db-directive | realm-meth-directive
 *				| realm-prompt-directive
 *	realm-db-directive ::= "database" db-file-path
 *	realm-meth-directive ::= "method" auth-method-name
 *	auth-method-name ::= "basic" | "SSL"
 *	realm-prompt-directive ::= "prompt" quote-char string quote-char
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	acc			- pointer to ACContext_t structure
 *	rspp			- pointer to RealmSpec_t pointer
 *
 * Returns:
 *
 *	If successful, the return value is the token type of the token
 *	following the realm-spec, i.e. either the first token after a
 *	realm-name or the first token after the closing "}".  It is the
 *	caller's responsibility to validate this token as a legitimate
 *	successor of a realm-spec.  If a parsing error occurs in the
 *	middle of a realm-spec, the return value is ACLERRPARSE, and an
 *	error frame is generated if an error list is provided.  For
 *	other kinds of errors a negative error code (from aclerror.h)
 *	is returned.
 */

int aclRealmSpecParse(NSErr_t * errp,
		      ACLFile_t * acf, ACContext_t * acc, RealmSpec_t **rspp)
{
    void * token = acf->acf_token;	/* handle for current token */
    char * tokenstr;			/* current token string */
    RealmSpec_t * rsp;			/* realm spec pointer */
    RealmSpec_t * nrsp;			/* named realm spec pointer */
    int rv;				/* result value */
    int eid;				/* error id value */
    char linestr[16];			/* line number string buffer */

    rv = acf->acf_ttype;

    /* Is the current token a "{" ? */
    if (rv != TOKEN_LBRACE) {

	/* No, could it be KEYWORD_REALM? */
	if (rv == TOKEN_IDENT) {

	    tokenstr = lex_token(token);

	    if (!strcasecmp(tokenstr, KEYWORD_REALM)) {

		/* Yes, step to the realm name */
		rv = aclGetToken(errp, acf, 0);
		if (rv != TOKEN_IDENT) {
		    if (rv < 0) goto punt;
		    goto err_rlmname;
		}

		tokenstr = lex_token(token);

		/* Look up the named realm specification */
		rv = symTableFindSym(acc->acc_stp, tokenstr, ACLSYMREALM,
				     (void **)&nrsp);
		if (rv < 0) goto err_undrlm;

		/* Return the named realm specification */
		*rspp = nrsp;

		/* Step to the token after the realm name */
		rv = aclGetToken(errp, acf, 0);
	    }
	}

	return rv;
    }

    /* Step to the token after the "{" */
    rv = aclGetToken(errp, acf, 0);
    if (rv < 0) goto punt;

    rsp = *rspp;
    if (rsp == 0) {
	rsp = (RealmSpec_t *)MALLOC(sizeof(RealmSpec_t));
	if (rsp == 0) goto err_nomem;
	memset((void *)rsp, 0, sizeof(RealmSpec_t));
	rsp->rs_sym.sym_type = ACLSYMREALM;
	*rspp = rsp;
    }

    /* Loop for each realm-directive */
    for (;; rv = aclGetToken(errp, acf, 0)) {

	if (rv != TOKEN_IDENT) {

	    /* Exit loop on "}" */
	    if (rv == TOKEN_RBRACE) break;

	    /* Ignore null directives */
	    if (rv == TOKEN_EOS) continue;

	    /* Otherwise need an identifier to start a directive */
	    goto err_nodir;
	}

	tokenstr = lex_token(token);

	/* Figure out which realm-directive this is */
	if (!strcasecmp(tokenstr, KEYWORD_DATABASE)) {

	    /* Get a file specification for the database */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv != TOKEN_STRING) {
		if (rv < 0) goto punt;
		goto err_nodb;
	    }

	    rsp->rs_realm.rlm_dbname = lex_token_take(token);
	    rsp->rs_realm.rlm_aif = &NSADB_AuthIF;
	}
	else if (!strcasecmp(tokenstr, KEYWORD_METHOD)) {

	    /* Step to the method identifier */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv != TOKEN_IDENT) {
		if (rv < 0) goto punt;
		goto err_nometh;
	    }

	    tokenstr = lex_token(token);

	    /* Interpret method name and set method in realm structure */
	    if (!strcasecmp(tokenstr, KEYWORD_BASIC)) {
		rsp->rs_realm.rlm_ameth = AUTH_METHOD_BASIC;
	    }
	    else if (!strcasecmp(tokenstr, KEYWORD_SSL) && server_enterprise) {
		rsp->rs_realm.rlm_ameth = AUTH_METHOD_SSL;
	    }
	    else goto err_badmeth;
	}
	else if (!strcasecmp(tokenstr, KEYWORD_PROMPT)) {

	    /* Step to the realm prompt string */
	    rv = aclGetToken(errp, acf, 0);
	    if ((rv != TOKEN_STRING) && (rv != TOKEN_IDENT)) {
		if (rv < 0) goto punt;
		goto err_noprompt;
	    }

	    /* Reference a copy of the prompt string from the realm */
	    rsp->rs_realm.rlm_prompt = lex_token_take(token);
	}
	else goto err_baddir;

	/* Get the token after the realm-directive */
	rv = aclGetToken(errp, acf, 0);

	/* Need a ";" to keep going */
	if (rv != TOKEN_EOS) break;
    }

    if (rv != TOKEN_RBRACE) goto err_rbrace;

    /* Get the token after the realm-spec */
    rv = aclGetToken(errp, acf, 0);

  punt:
    return rv;

  err_rlmname:
    eid = ACLERR2500;
    goto err_parse;

  err_undrlm:
    eid = ACLERR2520;
    rv = ACLERRUNDEF;
    goto err_sym;

  err_nomem:
    eid = ACLERR2540;
    rv = ACLERRNOMEM;
    goto ret_err;

  err_nodir:
    eid = ACLERR2560;
    goto err_parse;

  err_nodb:
    eid = ACLERR2570;
    goto err_parse;

  err_nometh:
    eid = ACLERR2580;
    goto err_parse;

  err_badmeth:
    eid = ACLERR2600;
    goto err_sym;

  err_noprompt:
    eid = ACLERR2605;
    goto err_parse;

  err_baddir:
    eid = ACLERR2610;
    goto err_sym;

  err_rbrace:
    eid = ACLERR2620;
    goto err_parse;

  err_sym:
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program,
		  3, acf->acf_filename, linestr, tokenstr);
    goto punt;

  err_parse:
    rv = ACLERRPARSE;
  ret_err:
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, 2, acf->acf_filename, linestr);
    goto punt;
}

/*
 * Description (aclRightsParse)
 *
 *	This function parse an access rights list.  The syntax for an
 *	access rights list is:
 *
 *	rights-list ::= "(" list-of-rights ")"
 *	list-of-rights ::= rights-elem | list-of-rights "," rights-elem
 *	rights-elem ::= right-name | "+" rights-def-name
 *	right-name ::= identifier
 *	rights-def-name ::= identifier
 *
 *	An element of a rights list is either the name of a particular
 *	access right (e.g. Read), or the name associated with an
 *	external definition of an access rights list, preceded by "+"
 *	(e.g. +editor-rights).  The list is enclosed in parentheses,
 *	and the elements are separated by commas.
 *
 *	This function adds to a list of rights provided by the caller.
 *	Access rights are internally assigned unique integer identifiers,
 *	and a symbol table is maintained to map an access right name to
 *	its identifier.
 *
 * Arguments:
 *
 *	errp			- error frame list pointer (may be null)
 *	acf			- pointer to ACLFile_t for ACL file
 *	acc			- pointer to ACContext_t structure
 *	rights			- pointer to rights list head
 *
 * Returns:
 *
 *	The return value is a code identifying the next token if successful.
 *	End-of-stream is indicated by a return value of TOKEN_EOF.  An error
 *	is indicated by a negative error code (ACLERRxxxx - see aclerror.h),
 *	and an error frame will be generated if an error list is provided.
 */

int aclRightsParse(NSErr_t * errp, ACLFile_t * acf, ACContext_t * acc,
		   RightSpec_t **rights)
{
    void * token = acf->acf_token;	/* LEX token handle */
    char * ename;			/* element name string pointer */
    RightSpec_t * rsp;			/* rights specification pointer */
    RightSpec_t * nrsp;			/* named rights spec pointer */
    RightDef_t * rdp;			/* right definition pointer */
    int rv;				/* result value */
    int eid;				/* error id */
    char linestr[16];			/* line number string buffer */

    /* Look for a left parenthesis */
    if (acf->acf_ttype != TOKEN_LPAREN) {

	/* No rights list present */
	return 0;
    }

    rsp = *rights;

    /* Create a RightSpec_t if we don't have one */
    if (rsp == 0) {
	rsp = (RightSpec_t *)MALLOC(sizeof(RightSpec_t));
	if (rsp == 0) goto err_nomem1;
	memset((void *)rsp, 0, sizeof(RightSpec_t));
	rsp->rs_sym.sym_type = ACLSYMRDEF;
	*rights = rsp;
    }

    /* Parse list elements */
    for (;;) {

	/* Look for an identifier */
	rv = aclGetToken(errp, acf, 0);
	if (rv != TOKEN_IDENT) {

	    /* No, maybe a "+" preceding a rights definition name? */
	    if (rv != TOKEN_PLUS) {

		/* One more chance, we'll allow the closing ")" after "," */
		if (rv != TOKEN_RPAREN) {
		    /* No, bad news */
		    if (rv < 0) goto punt;
		    goto err_elem;
		}

		/* Got right paren after comma */
		break;
	    }

	    /* Got a "+", try for the rights definition name */
	    rv = aclGetToken(errp, acf, 0);
	    if (rv != TOKEN_IDENT) {
		if (rv < 0) goto punt;
		goto err_rdef;
	    }

	    /* Get a pointer to the token string */
	    ename = lex_token(token);

	    /* See if it matches a rights definition in the symbol table */
	    rv = symTableFindSym(acc->acc_stp, ename, ACLSYMRDEF,
				 (void **)&nrsp);
	    if (rv) goto err_undef;

	    /*
	     * Merge the rights from the named rights list into the
	     * current rights list.
	     */
	    rv = uilMerge(&rsp->rs_list, &nrsp->rs_list);
	    if (rv < 0) goto err_nomem2;
	}
	else {

	    /* The current token is an access right name */

	    /* Get a pointer to the token string */
	    ename = lex_token(token);


	    /* Find or create an access right definition */
	    rv = aclRightDef(errp, acc, ename, &rdp);
	    if (rv < 0) goto err_radd;

	    /* Add the id for this right to the current rights list */
	    rv = usiInsert(&rsp->rs_list, rdp->rd_id);
	    if (rv < 0) goto err_nomem3;
	}

	rv = aclGetToken(errp, acf, 0);

	/* Want a comma to continue the list */
	if (rv != TOKEN_COMMA) {

	    /* A right parenthesis will end the list nicely */
	    if (rv == TOKEN_RPAREN) {

		/* Get the first token after the rights list */
		rv = aclGetToken(errp, acf, 0);
		break;
	    }

	    /* Anything else is an error */
	    if (rv < 0) goto punt;
	    goto err_list;
	}
    }

    return rv;

  err_elem:
    eid = ACLERR2700;
    rv = ACLERRSYNTAX;
    goto err_ret;

  err_rdef:
    eid = ACLERR2720;
    rv = ACLERRSYNTAX;
    goto err_ret;

  err_undef:
    eid = ACLERR2740;
    rv = ACLERRUNDEF;
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program,
		  3, acf->acf_filename, linestr, ename);
    return rv;

  err_nomem1:
    eid = ACLERR2760;
    goto err_nomem;

  err_nomem2:
    eid = ACLERR2780;
    goto err_nomem;

  err_radd:
    eid = ACLERR2800;
    goto err_ret;

  err_nomem3:
    eid = ACLERR2820;
    goto err_nomem;

  err_nomem:
    rv = ACLERRNOMEM;
    goto err_ret;

  err_list:

    eid = ACLERR2840;
    rv = ACLERRSYNTAX;

  err_ret:
    sprintf(linestr, "%d", acf->acf_lineno);
    nserrGenerate(errp, rv, eid, ACL_Program, 2, acf->acf_filename, linestr);

  punt:
    return rv;
}

/*
 * Description (aclStreamGet)
 *
 *	This function is the stream read function designated by
 *	aclFileOpen() to read the file associated with the LEX stream
 *	it creates.  It reads the next chunk of the file into the
 *	stream buffer.
 *
 * Arguments:
 *
 *	lst			- pointer to LEX stream structure
 *
 * Returns:
 *
 *	The return value is the number of bytes read if successful.
 *	A return value of zero indicates end-of-file.  An error is
 *	indicated by a negative return value.
 */

int aclStreamGet(LEXStream_t * lst)
{
    SYS_FILE fd = (SYS_FILE)(lst->lst_strmid);
    int len;

    len = system_fread(fd, lst->lst_buf, lst->lst_buflen);
    if (len >= 0) {
	lst->lst_len = len;
	lst->lst_cp = lst->lst_buf;
    }

    return len;
}
