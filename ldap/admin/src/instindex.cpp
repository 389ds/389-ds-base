/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
/*
 * index.c: Shows the first page you see on install
 * 
 * Rob McCool
 */

#include <nss.h>
#include <libadminutil/distadm.h>

#include "create_instance.h"
#include "configure_instance.h"

#include "dsalib.h"
#include "ldap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *ds_salted_sha1_pw_enc(char* pwd);



/* ----------- Create a new server from configuration variables ----------- */


static int create_config(server_config_s *cf)
{
    char *t = NULL;
	char error_param[BIG_LINE] = {0};

    t = create_server(cf, error_param);
    if(t)
	{
		char *msg;
		if (error_param[0])
		{
			msg = PR_smprintf("%s.error:could not create server %s - %s",
							  error_param, cf->servid, t);
		}
		else
		{
			msg = PR_smprintf("error:could not create server %s - %s",
							  cf->servid, t);
		}
		ds_show_message(msg);
		PR_smprintf_free(msg);
	}
	else if (!t)
	{
		ds_show_message("Created new Directory Server");
		return 0;
	}

    return 1;
}


/* ------ check passwords are same and satisfy minimum length policy------- */
static int check_passwords(char *pw1, char *pw2)
{
    if (strcmp (pw1, pw2) != 0) {
	    ds_report_error (INCORRECT_USAGE, " different passwords",
			  "Enter the password again."
			  "  The two passwords you entered are different.");
		return 1;
	}
	
    if ( ((int) strlen(pw1)) < 8 ) {
	    ds_report_error (INCORRECT_USAGE, " password too short",
			  "The password must be at least 8 characters long.");
		return 1;
	}

	return 0;
}

/* ------ Parse the results of a form and create a server from them ------- */


static int parse_form(server_config_s *cf)
{
    char *rm = getenv("REQUEST_METHOD");
    char *qs = getenv("QUERY_STRING");
    char* cfg_sspt_uid_pw1;
    char* cfg_sspt_uid_pw2;
    LDAPURLDesc *desc = 0;
    char *temp = 0;

    cf->sroot = getenv("NETSITE_ROOT");

    if (rm && qs && !strcmp(rm, "GET"))
	{
		ds_get_begin(qs);
	}
    else if (ds_post_begin(stdin))
	{
		return 1;
	}

    if (rm)
    {
		printf("Content-type: text/plain\n\n");
    }
    /* else we are being called from server installation; no output */

    if (!(cf->servname = ds_a_get_cgi_var("servname", "Server Name",
										  "Please give a hostname for your server.")))
	{
		return 1;
	}

	cf->bindaddr = ds_a_get_cgi_var("bindaddr", NULL, NULL);
    if (!(cf->servport = ds_a_get_cgi_var("servport", "Server Port",
										  "Please specify the TCP port number for this server.")))
	{
		return 1;
	}
    /* the suitespot 3x uid is the uid to use for setting up */
    /* a 4.x server to serve as a suitespot 3.x host */
    cf->suitespot3x_uid = ds_a_get_cgi_var("suitespot3x_uid", NULL, NULL);
    cf->cfg_sspt = ds_a_get_cgi_var("cfg_sspt", NULL, NULL);
    cf->cfg_sspt_uid = ds_a_get_cgi_var("cfg_sspt_uid", NULL, NULL);
    if (cf->cfg_sspt_uid && *(cf->cfg_sspt_uid) &&
	!(cf->cfg_sspt_uidpw = ds_a_get_cgi_var("cfg_sspt_uid_pw", NULL, NULL)))
    {

	if (!(cfg_sspt_uid_pw1 = ds_a_get_cgi_var("cfg_sspt_uid_pw1", "Password",
											  "Enter the password for the Mission Control Administrator's account.")))
	{
		return 1;
	}

	if (!(cfg_sspt_uid_pw2 = ds_a_get_cgi_var("cfg_sspt_uid_pw2", "Password",
											  "Enter the password for the Mission Control Administrator account, "
											  "twice.")))
	{
		return 1;
	}

	if (strcmp (cfg_sspt_uid_pw1, cfg_sspt_uid_pw2) != 0)
	{
	    ds_report_error (INCORRECT_USAGE, " different passwords",
			     "Enter the Mission Control Administrator account password again."
			     "  The two Mission Control Administrator account passwords "
			     "you entered are different.");
		return 1;
	}
	if ( ((int) strlen(cfg_sspt_uid_pw1)) < 1 ) {
	    ds_report_error (INCORRECT_USAGE, " password too short",
			     "The password must be at least 1 character long.");
		return 1;
	}
	cf->cfg_sspt_uidpw = cfg_sspt_uid_pw1;
    }

    if (cf->cfg_sspt && *cf->cfg_sspt && !strcmp(cf->cfg_sspt, "1") &&
	!cf->cfg_sspt_uid)
    {
	ds_report_error (INCORRECT_USAGE,
			 " Userid not specified",
			 "A Userid for Mission Control Administrator must be specified.");
	return 1;
    }
    cf->start_server = ds_a_get_cgi_var("start_server", NULL, NULL);
    cf->secserv = ds_a_get_cgi_var("secserv", NULL, NULL);
    if (cf->secserv && strcmp(cf->secserv, "off"))
	cf->secservport = ds_a_get_cgi_var("secservport", NULL, NULL);
    if (!(cf->servid = ds_a_get_cgi_var("servid", "Server Identifier",
										"Please give your server a short identifier.")))
	{
		return 1;
	}

#ifdef XP_UNIX
    cf->servuser = ds_a_get_cgi_var("servuser", NULL, NULL);
#endif

    /*cf->suffix = ds_a_get_cgi_var("suffix", "Subtree to store in this database",*/
    /*"Please specify the Subtree to store in this database");*/
    cf->suffix = NULL;
    cf->suffix = dn_normalize_convert(ds_a_get_cgi_var("suffix", NULL, NULL));
    
    if (cf->suffix == NULL) {
	cf->suffix = "";
    }

    cf->rootdn = dn_normalize_convert(ds_a_get_cgi_var("rootdn", NULL, NULL));
    if (cf->rootdn && *(cf->rootdn)) {
	if (!(cf->rootpw = ds_a_get_cgi_var("rootpw", NULL, NULL)))
	{
	    char* pw1 = ds_a_get_cgi_var("rootpw1", "Password",
			 "Enter the password for the unrestricted user.");
	    char* pw2 = ds_a_get_cgi_var("rootpw2", "Password",
			 "Enter the password for the unrestricted user, twice.");

	    if (!pw1 || !pw2 || check_passwords(pw1, pw2))
		{
			return 1;
		}

	    cf->rootpw = pw1;
	}
	/* Encode the password in SSHA by default */
	cf->roothashedpw = (char *)ds_salted_sha1_pw_enc (cf->rootpw);
    }
    
    cf->replicationdn = dn_normalize_convert(ds_a_get_cgi_var("replicationdn", NULL, NULL));
    if(cf->replicationdn && *(cf->replicationdn))
    {
	if (!(cf->replicationpw = ds_a_get_cgi_var("replicationpw", NULL, NULL)))
	{
	    char *replicationpw1 = ds_a_get_cgi_var("replicationpw1", "Password",
			   "Enter the password for the replication dn.");
	    char *replicationpw2 = ds_a_get_cgi_var("replicationpw2", "Password",
			   "Enter the password for the replication dn, twice.");

	    if (!replicationpw1 || !replicationpw2 || check_passwords(replicationpw1, replicationpw2))
		{
			return 1;
		}

	    cf->replicationpw = replicationpw1;
	}
      	/* Encode the password in SSHA by default */
	cf->replicationhashedpw = (char *)ds_salted_sha1_pw_enc (cf->replicationpw);
    }

    cf->consumerdn = dn_normalize_convert(ds_a_get_cgi_var("consumerdn", NULL, NULL));
    if(cf->consumerdn && *(cf->consumerdn))
    {
	if (!(cf->consumerpw = ds_a_get_cgi_var("consumerpw", NULL, NULL)))
	{
	    char *consumerpw1 = ds_a_get_cgi_var("consumerpw1", "Password",
			     "Enter the password for the consumer dn.");
	    char *consumerpw2 = ds_a_get_cgi_var("consumerpw2", "Password",
			     "Enter the password for the consumer dn, twice.");

	    if (!consumerpw1 || !consumerpw2 || check_passwords(consumerpw1, consumerpw2))
		{
			return 1;
		}

	    cf->consumerpw = consumerpw1;
	}
      	/* Encode the password in SSHA by default */
	cf->consumerhashedpw = (char *)ds_salted_sha1_pw_enc (cf->consumerpw);
    }

    cf->changelogdir = ds_a_get_cgi_var("changelogdir", NULL, NULL);
    cf->changelogsuffix = dn_normalize_convert(ds_a_get_cgi_var("changelogsuffix", NULL, NULL));
    
    cf->admin_domain = ds_a_get_cgi_var("admin_domain", NULL, NULL);
    cf->use_existing_config_ds = 1; /* there must already be one */
    cf->use_existing_user_ds = 0; /* we are creating it */

    temp = ds_a_get_cgi_var("ldap_url", NULL, NULL);
    if (temp && !ldap_url_parse(temp, &desc) && desc)
    {
	char *suffix = dn_normalize_convert(strdup(cf->netscaperoot));
	/* the config ds connection may require SSL */
	int isSSL = !strncmp(temp, "ldaps:", strlen("ldaps:"));
	cf->config_ldap_url = PR_smprintf("ldap%s://%s:%d/%s",
									  (isSSL ? "s" : ""), desc->lud_host,
									  desc->lud_port, suffix);
	ldap_free_urldesc(desc);
    }

    /* if being called as a CGI, the user_ldap_url will be the directory
       we're creating */
    /* this is the directory we're creating, and we cannot create an ssl
       directory, so we don't have to worry about ldap vs ldaps here */    
    cf->user_ldap_url = PR_smprintf("ldap://%s:%s/%s", cf->servname,
									cf->servport, cf->suffix);

    cf->samplesuffix = NULL;

    cf->disable_schema_checking = ds_a_get_cgi_var("disable_schema_checking",
					      NULL, NULL);
    return 0;
}


/* --------------------------------- main --------------------------------- */

static void
printInfo(int argc, char *argv[], char *envp[], FILE* fp)
{
	int ii = 0;
	if (!fp)
		fp = stdout;

	fprintf(fp, "Program name = %s\n", argv[0]);
	for (ii = 1; ii < argc; ++ii)
	{
		fprintf(fp, "argv[%d] = %s\n", ii, argv[ii]);
	}

	for (ii = 0; envp[ii]; ++ii)
	{
		fprintf(fp, "%s\n", envp[ii]);
	}

	fprintf(fp, "#####################################\n");
}

int main(int argc, char *argv[], char * /*envp*/ [])
{
    char *rm = getenv("REQUEST_METHOD");
    int status = 0;
    server_config_s cf;
	char *infFileName = 0;
	int reconfig = 0;
	int ii = 0;
	int cgi = 0;

	(void)ADMUTIL_Init();
	
	/* Initialize NSS to make ds_salted_sha1_pw_enc() happy */
	if (NSS_NoDB_Init(NULL) != SECSuccess) {
		ds_report_error(DS_GENERAL_FAILURE, " initialization failure",
				"Unable to initialize the NSS subcomponent.");
		exit(1);
	}

	/* make stdout unbuffered */
	setbuf(stdout, 0);

#ifdef XP_WIN32
    if ( getenv("DEBUG_DSINST") )
	DebugBreak();
#endif

    memset(&cf, 0, sizeof(cf));
    set_defaults(0, 0, &cf);

	/* scan cmd line arguments */
	for (ii = 0; ii < argc; ++ii)
	{
		if (!strcmp(argv[ii], "-f") && (ii + 1) < argc &&
			argv[ii+1])
			infFileName = argv[ii+1];
		else if (!strcmp(argv[ii], "-r"))
			reconfig = 1;
	}

    /* case 1: being called as program -f inffile */
    if (infFileName)
    {
		FILE *infFile = fopen(infFileName, "r");
		if (!infFile)
		{
			ds_report_error(INCORRECT_USAGE, infFileName,
				"This file could not be opened.  A valid file must be given.");
			status = 1;
		}
		else
			fclose(infFile);

		if (reconfig)
			status = reconfigure_instance(argc, argv);
		else
		{
			if (!status)
				status = create_config_from_inf(&cf, argc, argv);
			if (!status)
				status = create_config(&cf);
			if (!status)
				status = configure_instance();
		}
    }
    /* case 2: being called as a CGI */
    else if (rm)
    {
		cgi = 1;
        status = parse_form(&cf);
		if (!status)
			status = create_config(&cf);
		if (!status)
			status = configure_instance_with_config(&cf, 1, 0);
    }
    /* case 3: punt */
    else
    {
		ds_report_error (
			INCORRECT_USAGE,
			"No request method specified",
			"A REQUEST_METHOD must be specified (POST, GET) to run this CGI program.");
		status = 1;
    }

	if (cgi)
	{
		/* The line below is used by the console to detect
		   the end of the operation. See replyHandler() in
		   MigrateCreate.java */
		fprintf(stdout, "NMC_Status: %d\n", status);
		/* In the past, we used to call rpt_success() or rpt_err() 
		   according to status. However these functions are not designed
		   for our case: they print an HTTP header line "Content-type: text/html" */
	}

#if defined( hpux )
    _exit(status);
#endif
    return status;
}
