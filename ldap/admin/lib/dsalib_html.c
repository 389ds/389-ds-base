/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#if defined( XP_WIN32 )
#include <windows.h>
#endif

#include "dsalib.h"
#include "prprf.h"
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* holds the contents of the POSTed data from stdin as an array
of key/value pairs parsed from the key=value input */
static char **input = 0;

/* This variable is true if the program should assume stdout is connected
   to an HTML context e.g. if this is being run as a normal CGI.  It is
   false to indicate that output should not contain HTML formatting,
   Javascript, etc. put plain ol' ASCII only
*/
static int formattedOutput = 1;

DS_EXPORT_SYMBOL int
ds_get_formatted_output(void)
{
	return formattedOutput;
}

DS_EXPORT_SYMBOL void
ds_set_formatted_output(int val)
{
	formattedOutput = val;
}

DS_EXPORT_SYMBOL void
ds_print_file_name(char *fileptr)
{
    char        *meaning;

    fprintf(stdout, "%s", fileptr);
    if ( (meaning = ds_get_file_meaning(fileptr)) != NULL ) {
        fprintf(stdout, " (%s)", meaning);
    }
}

/*
 * Get a CGI variable.
 */
DS_EXPORT_SYMBOL char *
ds_get_cgi_var(char *cgi_var_name)
{
    char        *cgi_var_value;
 
    cgi_var_value = (char *) ds_a_get_cgi_var(cgi_var_name, NULL, NULL);
    if ( cgi_var_value == NULL ) {
	/*
	 * The ds_a_get_cgi_var() lies! It gives a NULL even if the 
	 * value is "". So assume the variable is there and 
	 * return an empty string.
	 */
        return("");
    }
    return(cgi_var_value);
}

/* parse POST input to a CGI program */
DS_EXPORT_SYMBOL int
ds_post_begin(FILE *in) 
{
    char *vars = NULL, *tmp = NULL, *decoded_vars = NULL;
    int cl;

    if(!(tmp = getenv("CONTENT_LENGTH")))
	{
        ds_report_error(DS_INCORRECT_USAGE, "Browser Error", "Your browser"
						" sent no content length with a POST command."
						"  Please be sure to use a fully compliant browser.");
		return 1;
	}

    cl = atoi(tmp);

    vars = (char *)malloc(cl+1);

    if( !(fread(vars, 1, cl, in)) )
	{
        ds_report_error(DS_SYSTEM_ERROR, "CGI error",
						"The POST variables could not be read from stdin.");
		return 1;
	}

    vars[cl] = '\0';

	decoded_vars = ds_URL_decode(vars);
	free(vars);

    input = ds_string_to_vec(decoded_vars);
	free(decoded_vars);
/*
	for (cl = 0; input[cl]; ++cl)
		printf("ds_post_begin: read cgi var=[%s]\n", input[cl]);
*/
	return 0;
}

/* parse GET input to a CGI program */
DS_EXPORT_SYMBOL void
ds_get_begin(char *query_string)
{
	char *decoded_input = ds_URL_decode(query_string);
	input = ds_string_to_vec(decoded_input);
	free(decoded_input);
}

/*
  Borrowed from libadmin/form_post.c
*/
DS_EXPORT_SYMBOL char *
ds_a_get_cgi_var(char *varname, char *elem_id, char *bongmsg)
{
    register int x = 0;
    int len = strlen(varname);
    char *ans = NULL;
   
    while(input[x])  {
    /*  We want to get rid of the =, so len, len+1 */
        if((!strncmp(input[x], varname, len)) && (*(input[x]+len) == '='))  {
            ans = strdup(input[x] + len + 1);
            if(!strcmp(ans, ""))
                ans = NULL;
            break;
        }  else
            x++;
    }
    if(ans == NULL)  {
        if ((bongmsg) && strlen(bongmsg))
		{
			/* prefix error with varname so output interpreters can determine */
			/* which parameter is in error */
			char *msg;
			if (!ds_get_formatted_output() && (varname != NULL))
			{
				msg = PR_smprintf("%s.error: %s %s", varname, elem_id, bongmsg);
			}
			else
			{
				msg = PR_smprintf("error: %s %s", elem_id, bongmsg);
			}
			ds_show_message(msg);
			PR_smprintf_free(msg);
		}
        else
            return NULL;
    }
    else
        return(ans);
    /* shut up gcc */
    return NULL;
}

DS_EXPORT_SYMBOL char **
ds_string_to_vec(char *in)
{
    char **ans;
    int vars = 0;
    register int x = 0;
    char *tmp;

    in = strdup(in);

    while(in[x])
        if(in[x++]=='=')
            vars++;

    
    ans = (char **)calloc(vars+1, sizeof(char *));
  
    x=0;
	/* strtok() is not MT safe, but it is okay to call here because it is used in monothreaded env */
    tmp = strtok(in, "&");
    ans[x++]=strdup(tmp);

    while((tmp = strtok(NULL, "&")))  {
        ans[x++] = strdup(tmp);
    }

    free(in);

    return(ans);
}
