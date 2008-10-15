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
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <security/pam_appl.h>

#include "pam_passthru.h"

/*
 * PAM is not thread safe.  We have to execute any PAM API calls in
 * a critical section.  This is the lock that protects that code.
 */
static Slapi_Mutex *PAMLock;

/* Utility struct to wrap strings to avoid mallocs if possible - use
   stack allocated string space */
#define MY_STATIC_BUF_SIZE 256
typedef struct my_str_buf {
	char fixbuf[MY_STATIC_BUF_SIZE];
	char *str;
} MyStrBuf;

static char *
init_my_str_buf(MyStrBuf *buf, const char *s)
{
	PR_ASSERT(buf);
	if (s && (strlen(s) < sizeof(buf->fixbuf))) {
		strcpy(buf->fixbuf, s);
		buf->str = buf->fixbuf;
	} else {
		buf->str = slapi_ch_strdup(s);
		buf->fixbuf[0] = 0;
	}

	return buf->str;
}

static void
delete_my_str_buf(MyStrBuf *buf)
{
	if (buf->str != buf->fixbuf) {
		slapi_ch_free_string(&(buf->str));
	}
}

/* for third arg to pam_start */
struct my_pam_conv_str {
	Slapi_PBlock *pb;
	char *pam_identity;
};

/*
 * Get the PAM identity from the value of the leftmost RDN in the BIND DN.
 */
static char *
derive_from_bind_dn(Slapi_PBlock *pb, char *binddn, MyStrBuf *pam_id)
{
	Slapi_RDN *rdn;
	char *type = NULL;
	char *value = NULL;

	rdn = slapi_rdn_new_dn(binddn);
	slapi_rdn_get_first(rdn, &type, &value);
	init_my_str_buf(pam_id, value);
	slapi_rdn_free(&rdn);

	return pam_id->str;
}

static char *
derive_from_bind_entry(Slapi_PBlock *pb, char *binddn, MyStrBuf *pam_id, char *map_ident_attr)
{
	char buf[BUFSIZ];
	Slapi_Entry *entry = NULL;
	Slapi_DN *sdn = slapi_sdn_new_dn_byref(binddn);
	char *attrs[] = { NULL, NULL };
	attrs[0] = map_ident_attr;
	int rc = slapi_search_internal_get_entry(sdn, attrs, &entry,
											 pam_passthruauth_get_plugin_identity());

	slapi_sdn_free(&sdn);

	if (rc != LDAP_SUCCESS) {
		slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"Could not find BIND dn %s (error %d - %s)\n",
						escape_string(binddn, buf), rc, ldap_err2string(rc));
		init_my_str_buf(pam_id, NULL);
   	} else if (NULL == entry) {
		slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"Could not find entry for BIND dn %s\n",
						escape_string(binddn, buf));
		init_my_str_buf(pam_id, NULL);
	} else {
		char *val = slapi_entry_attr_get_charptr(entry, map_ident_attr);
		init_my_str_buf(pam_id, val);
		slapi_ch_free_string(&val);
	}

	slapi_entry_free(entry);

	return pam_id->str;
}

static void
report_pam_error(char *str, int rc, pam_handle_t *pam_handle)
{
	if (rc != PAM_SUCCESS) {
		slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"Error from PAM %s (%d: %s)\n",
						str, rc, pam_strerror(pam_handle, rc));
	}
}

/* returns a berval value as a null terminated string */
static char *strdupbv(struct berval *bv)
{
	char *str = slapi_ch_malloc(bv->bv_len+1);
	memcpy(str, bv->bv_val, bv->bv_len);
	str[bv->bv_len] = 0;
	return str;
}

static void
free_pam_response(int nresp, struct pam_response *resp)
{
	int ii;
	for (ii = 0; ii < nresp; ++ii) {
		if (resp[ii].resp) {
			slapi_ch_free((void **)&(resp[ii].resp));
		}
	}
	slapi_ch_free((void **)&resp);
}

/*
 * This is the conversation function passed into pam_start().  This is what sets the password
 * that PAM uses to authenticate.  This function is sort of stupid - it assumes all echo off
 * or binary prompts are for the password, and other prompts are for the username.  Time will
 * tell if this is actually the case.
 */
static int
pam_conv_func(int num_msg, const struct pam_message **msg, struct pam_response **resp, void *mydata)
{
	int ii;
	struct berval *creds;
	struct my_pam_conv_str *my_data = (struct my_pam_conv_str *)mydata;
    struct pam_response *reply;
	int ret = PAM_SUCCESS;

    if (num_msg <= 0) {
		return PAM_CONV_ERR;
	}

	/* empty reply structure */
    reply = (struct pam_response *)slapi_ch_calloc(num_msg,
										  sizeof(struct pam_response));
	slapi_pblock_get( my_data->pb, SLAPI_BIND_CREDENTIALS, &creds ); /* the password */
	for (ii = 0; ii < num_msg; ++ii) {
		slapi_log_error(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"pam msg [%d] = %d %s\n", ii, msg[ii]->msg_style,
						msg[ii]->msg);
		/* hard to tell what prompt is for . . . */
		/* assume prompts for password are either BINARY or ECHO_OFF */
		if (msg[ii]->msg_style == PAM_PROMPT_ECHO_OFF) {
			reply[ii].resp = strdupbv(creds);
#ifdef LINUX
		} else if (msg[ii]->msg_style == PAM_BINARY_PROMPT) {
			reply[ii].resp = strdupbv(creds);
#endif
		} else if (msg[ii]->msg_style == PAM_PROMPT_ECHO_ON) { /* assume username */
			reply[ii].resp = slapi_ch_strdup(my_data->pam_identity);
		} else if (msg[ii]->msg_style == PAM_ERROR_MSG) {
			slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							"pam msg [%d] error [%s]\n", ii, msg[ii]->msg);
		} else if (msg[ii]->msg_style == PAM_TEXT_INFO) {
			slapi_log_error(SLAPI_LOG_PLUGIN, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							"pam msg [%d] text info [%s]\n", ii, msg[ii]->msg);
		} else {
			slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
							"Error: unknown pam message type (%d: %s)\n",
							msg[ii]->msg_style, msg[ii]->msg);
			ret = PAM_CONV_ERR;
		}
	}

	if (ret == PAM_CONV_ERR) {
		free_pam_response(num_msg, reply);
		reply = NULL;
	}

	*resp = reply;

	return ret;
}

/*
 * Do the actual work of authenticating with PAM. First, get the PAM identity
 * based on the method used to convert the BIND identity to the PAM identity.
 * Set up the structures that pam_start needs and call pam_start().  After
 * that, call pam_authenticate and pam_acct_mgmt.  Check the various return
 * values from these functions and map them to their corresponding LDAP BIND
 * return values.  Return the appropriate LDAP error code.
 * This function will also set the appropriate LDAP response controls in
 * the given pblock.
 * Since this function can be called multiple times, we only want to return
 * the errors and controls to the user if this is the final call, so the
 * final_method parameter tells us if this is the last one.  Coupled with
 * the fallback argument, we can tell if we are able to send the response
 * back to the client.
 */
static int
do_one_pam_auth(
	Slapi_PBlock *pb,
	int method, /* get pam identity from ENTRY, RDN, or DN */
	PRBool final_method, /* which method is the last one to try */
	char *pam_service, /* name of service for pam_start() */
	char *map_ident_attr, /* for ENTRY method, name of attribute holding pam identity */
	PRBool fallback, /* if true, failure here should fallback to regular bind */
	int pw_response_requested /* do we need to send pwd policy resp control */
)
{
	MyStrBuf pam_id;
	char *binddn = NULL;
	int rc;
	int retcode = LDAP_SUCCESS;
	pam_handle_t *pam_handle;
	struct my_pam_conv_str my_data;
	struct pam_conv my_pam_conv = {pam_conv_func, NULL};
	char buf[BUFSIZ]; /* for error messages */
	char *errmsg = NULL; /* free with PR_smprintf_free */

	slapi_pblock_get( pb, SLAPI_BIND_TARGET, &binddn );

	if (method == PAMPT_MAP_METHOD_RDN) {
		derive_from_bind_dn(pb, binddn, &pam_id);
	} else if (method == PAMPT_MAP_METHOD_ENTRY) {
		derive_from_bind_entry(pb, binddn, &pam_id, map_ident_attr);
	} else {
		init_my_str_buf(&pam_id, binddn);
	}

	if (!pam_id.str) {
		errmsg = PR_smprintf("Bind DN [%s] is invalid or not found",
							 escape_string(binddn, buf));
		retcode = LDAP_NO_SUCH_OBJECT; /* user unknown */
		goto done; /* skip the pam stuff */
	}

	/* do the pam stuff */
	my_data.pb = pb;
	my_data.pam_identity = pam_id.str;
	my_pam_conv.appdata_ptr = &my_data;
	slapi_lock_mutex(PAMLock);
	/* from this point on we are in the critical section */
	rc = pam_start(pam_service, pam_id.str, &my_pam_conv, &pam_handle);
	report_pam_error("during pam_start", rc, pam_handle);

	if (rc == PAM_SUCCESS) {
		/* use PAM_SILENT - there is no user interaction at this point */
		rc = pam_authenticate(pam_handle, 0);
		report_pam_error("during pam_authenticate", rc, pam_handle);
		/* check different types of errors here */
		if (rc == PAM_USER_UNKNOWN) {
			errmsg = PR_smprintf("User id [%s] for bind DN [%s] does not exist in PAM",
								 pam_id.str, escape_string(binddn, buf));
			retcode = LDAP_NO_SUCH_OBJECT; /* user unknown */
		} else if (rc == PAM_AUTH_ERR) {
			errmsg = PR_smprintf("Invalid PAM password for user id [%s], bind DN [%s]",
								 pam_id.str, escape_string(binddn, buf));
			retcode = LDAP_INVALID_CREDENTIALS; /* invalid creds */
		} else if (rc == PAM_MAXTRIES) {
			errmsg = PR_smprintf("Authentication retry limit exceeded in PAM for "
								 "user id [%s], bind DN [%s]",
								 pam_id.str, escape_string(binddn, buf));
			if (pw_response_requested) {
				slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_ACCTLOCKED);
			}
			retcode = LDAP_CONSTRAINT_VIOLATION; /* max retries */
		} else if (rc != PAM_SUCCESS) {
			errmsg = PR_smprintf("Unknown PAM error [%s] for user id [%s], bind DN [%s]",
								 pam_strerror(pam_handle, rc), pam_id.str, escape_string(binddn, buf));
			retcode = LDAP_OPERATIONS_ERROR; /* pam config or network problem */
		}
	}

	/* if user authenticated successfully, see if there is anything we need
	   to report back w.r.t. password or account lockout */
	if (rc == PAM_SUCCESS) {
		rc = pam_acct_mgmt(pam_handle, 0);
		report_pam_error("during pam_acct_mgmt", rc, pam_handle);
		/* check different types of errors here */
		if (rc == PAM_USER_UNKNOWN) {
			errmsg = PR_smprintf("User id [%s] for bind DN [%s] does not exist in PAM",
								 pam_id.str, escape_string(binddn, buf));
			retcode = LDAP_NO_SUCH_OBJECT; /* user unknown */
		} else if (rc == PAM_AUTH_ERR) {
			errmsg = PR_smprintf("Invalid PAM password for user id [%s], bind DN [%s]",
								 pam_id.str, escape_string(binddn, buf));
			retcode = LDAP_INVALID_CREDENTIALS; /* invalid creds */
		} else if (rc == PAM_PERM_DENIED) {
			errmsg = PR_smprintf("Access denied for PAM user id [%s], bind DN [%s]"
								 " - see administrator",
								 pam_id.str, escape_string(binddn, buf));
			if (pw_response_requested) {
				slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_ACCTLOCKED);
			}
			retcode = LDAP_UNWILLING_TO_PERFORM;
		} else if (rc == PAM_ACCT_EXPIRED) {
			errmsg = PR_smprintf("Expired PAM password for user id [%s], bind DN [%s]: "
								 "reset required",
								 pam_id.str, escape_string(binddn, buf));
			slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
			if (pw_response_requested) {
				slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_PWDEXPIRED);
			}
			retcode = LDAP_INVALID_CREDENTIALS;
		} else if (rc == PAM_NEW_AUTHTOK_REQD) { /* handled same way as ACCT_EXPIRED */
			errmsg = PR_smprintf("Expired PAM password for user id [%s], bind DN [%s]: "
								 "reset required",
								 pam_id.str, escape_string(binddn, buf));
			slapi_add_pwd_control(pb, LDAP_CONTROL_PWEXPIRED, 0);
			if (pw_response_requested) {
				slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_PWDEXPIRED);
			}
			retcode = LDAP_INVALID_CREDENTIALS;
		} else if (rc != PAM_SUCCESS) {
			errmsg = PR_smprintf("Unknown PAM error [%s] for user id [%s], bind DN [%s]",
								 pam_strerror(pam_handle, rc), pam_id.str, escape_string(binddn, buf));
			retcode = LDAP_OPERATIONS_ERROR; /* unknown */
		}
	}

	rc = pam_end(pam_handle, rc);
	report_pam_error("during pam_end", rc, pam_handle);
	slapi_unlock_mutex(PAMLock);
	/* not in critical section any more */

done:
	delete_my_str_buf(&pam_id);

	if ((retcode == LDAP_SUCCESS) && (rc != PAM_SUCCESS)) {
		errmsg = PR_smprintf("Unknown PAM error [%d] for user id [%d], bind DN [%s]",
							 rc, pam_id.str, escape_string(binddn, buf));
		retcode = LDAP_OPERATIONS_ERROR;
	}

	if (retcode != LDAP_SUCCESS) {
		slapi_log_error(SLAPI_LOG_FATAL, PAM_PASSTHRU_PLUGIN_SUBSYSTEM,
						"%s\n", errmsg);
		if (final_method && !fallback) {
			slapi_send_ldap_result(pb, retcode, NULL, errmsg, 0, NULL);
		}
	}

	if (errmsg) {
		PR_smprintf_free(errmsg);
	}

	return retcode;
}

/*
 * Perform any PAM subsystem initialization that must be done at startup time.
 * For now, this means only the PAM mutex since PAM is not thread safe.
 */
int
pam_passthru_pam_init( void )
{
	if (!(PAMLock = slapi_new_mutex())) {
		return LDAP_LOCAL_ERROR;
	}

	return 0;
}

/*
 * Entry point into the PAM auth code.  Shields the rest of the app
 * from PAM API code.  Get our config params, then call the actual
 * code that does the PAM auth.  Can call that code up to 3 times,
 * depending on what methods are set in the config.
 */
int
pam_passthru_do_pam_auth(Slapi_PBlock *pb, Pam_PassthruConfig *cfg)
{
	int rc = LDAP_SUCCESS;
	MyStrBuf pam_id_attr; /* avoid malloc if possible */
	MyStrBuf pam_service; /* avoid malloc if possible */
	int method1, method2, method3;
	PRBool final_method;
	PRBool fallback = PR_FALSE;
	int pw_response_requested;
	LDAPControl **reqctrls = NULL;

	/* first lock and get the methods and other info */
	/* we do this so we can acquire and release the lock quickly to
	   avoid potential deadlocks */
	slapi_lock_mutex(cfg->lock);
	method1 = cfg->pamptconfig_map_method1;
	method2 = cfg->pamptconfig_map_method2;
	method3 = cfg->pamptconfig_map_method3;

	init_my_str_buf(&pam_id_attr, cfg->pamptconfig_pam_ident_attr);
	init_my_str_buf(&pam_service, cfg->pamptconfig_service);

	fallback = cfg->pamptconfig_fallback;

	slapi_unlock_mutex(cfg->lock);

	slapi_pblock_get (pb, SLAPI_REQCONTROLS, &reqctrls);
	slapi_pblock_get (pb, SLAPI_PWPOLICY, &pw_response_requested);

	/* figure out which method is the last one - we only return error codes, controls
	   to the client and send a response on the last method */

	final_method = (method2 == PAMPT_MAP_METHOD_NONE);
	rc = do_one_pam_auth(pb, method1, final_method, pam_service.str, pam_id_attr.str, fallback,
						 pw_response_requested);
	if ((rc != LDAP_SUCCESS) && !final_method) {
		final_method = (method3 == PAMPT_MAP_METHOD_NONE);
		rc = do_one_pam_auth(pb, method2, final_method, pam_service.str, pam_id_attr.str, fallback,
							 pw_response_requested);
		if ((rc != LDAP_SUCCESS) && !final_method) {
			final_method = PR_TRUE;
			rc = do_one_pam_auth(pb, method3, final_method, pam_service.str, pam_id_attr.str, fallback,
								 pw_response_requested);
		}
	}

	delete_my_str_buf(&pam_id_attr);
	delete_my_str_buf(&pam_service);

	return rc;
}
