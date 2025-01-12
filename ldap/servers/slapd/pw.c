/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2021 Red Hat, Inc.
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Contributors:
 *   Hewlett-Packard Development Company, L.P.
 *     Bugfix for bug #195302
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

/* Copyright notice below for using portions of pam_pwquality source code
 * (pam_cracklib.c) */
/*
 * Copyright (c) Cristian Gafton <gafton@redhat.com>, 1996.
 *                                              All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED `AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The following copyright was appended for the long password support
 * added with the libpam 0.58 release:
 *
 * Modificaton Copyright (c) Philip W. Dalrymple III <pwd@mdtsoft.com>
 *       1997. All rights reserved
 *
 * THE MODIFICATION THAT PROVIDES SUPPORT FOR LONG PASSWORD TYPE CHECKING TO
 * THIS SOFTWARE IS PROVIDED `AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * slapd hashed password routines
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sechash.h>
#include <crack.h>
#include "slap.h"

#ifndef CRACKLIB_DICTS
#define CRACKLIB_DICTS NULL
#endif

#define DENY_PW_CHANGE_ACI "(targetattr = \"userPassword\") ( version 3.0; acl \"disallow_pw_change_aci\"; deny (write ) userdn = \"ldap:///self\";)"
#define GENERALIZED_TIME_LENGTH 15

static int pw_in_history(Slapi_Value **history_vals, const Slapi_Value *pw_val);
static int update_pw_history(Slapi_PBlock *pb, const Slapi_DN *sdn, char *old_pw);
static int check_trivial_words(Slapi_PBlock *, Slapi_Entry *, Slapi_Value **, char *attrtype, int toklen, Slapi_Mods *smods);
static int pw_boolean_str2value(const char *str);
/* static LDAPMod* pw_malloc_mod (char* name, char* value, int mod_op); */
static void pw_get_admin_users(passwdPolicy *pwp);

/*
 * We want to be able to return errors to internal operations (which
 * can come from the password change extended operation). So we have
 * a special result function that does the right thing for an internal op.
 */

static void
pw_send_ldap_result(
    Slapi_PBlock *pb,
    int err,
    char *matched,
    char *text,
    int nentries,
    struct berval **urls)
{
    int internal_op = 0;
    Slapi_Operation *operation = NULL;

    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    if (NULL == operation) {
        slapi_log_err(SLAPI_LOG_ERR, "pw_send_ldap_result", "No operation\n");
        return;
    }
    internal_op = operation_is_flag_set(operation, OP_FLAG_INTERNAL);

    if (internal_op) {
        slapi_pblock_set(pb, SLAPI_PLUGIN_INTOP_RESULT, &err);
    } else {
        send_ldap_result(pb, err, matched, text, nentries, urls);
    }
}

/*
 * Like slapi_value_find, except for passwords.
 * returns 0 if password "v" is found in "vals"; non-zero otherwise
 */
SLAPI_DEPRECATED int
slapi_pw_find(
    struct berval **vals,
    struct berval *v)
{
    int rc;
    Slapi_Value **svin_vals = NULL;
    Slapi_Value svin_v;
    slapi_value_init_berval(&svin_v, v);
    valuearray_init_bervalarray(vals, &svin_vals); /* JCM SLOW FUNCTION */
    rc = slapi_pw_find_sv(svin_vals, &svin_v);
    valuearray_free(&svin_vals);
    value_done(&svin_v);
    return rc;
}

/*
 * Like slapi_value_find, except for passwords.
 * returns 0 if password "v" is found in "vals"; non-zero otherwise
 */

int
slapi_pw_find_sv(
    Slapi_Value **vals,
    const Slapi_Value *v)
{
    struct pw_scheme *pwsp;
    char *valpwd;
    int i;

    slapi_log_err(SLAPI_LOG_TRACE, "slapi_pw_find_sv", "=> \"%s\"\n", slapi_value_get_string(v));

    for (i = 0; vals && vals[i]; i++) {
        pwsp = pw_val2scheme((char *)slapi_value_get_string(vals[i]), &valpwd, 1);
        if (pwsp != NULL &&
            (*(pwsp->pws_cmp))((char *)slapi_value_get_string(v), valpwd) == 0) {
            slapi_log_err(SLAPI_LOG_TRACE, "slapi_pw_find_sv",
                          "<= Matched \"%s\" using scheme \"%s\"\n",
                          valpwd, pwsp->pws_name);
            free_pw_scheme(pwsp);
            return (0); /* found it */
        }
        free_pw_scheme(pwsp);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "slapi_pw_find_sv", "No matching password <=\n");

    return (1); /* no match */
}

/* Checks if the specified value is encoded.
   Returns 1 if it is and 0 otherwise
 */
int
slapi_is_encoded(char *value)
{
    struct pw_scheme *is_hashed = NULL;
    int is_encoded = 0;

    is_hashed = pw_val2scheme(value, NULL, 0);
    if (is_hashed != NULL) {
        free_pw_scheme(is_hashed);
        is_encoded = 1;
    }
    return (is_encoded);
}


char *
slapi_encode(char *value, char *alg)
{
    return (slapi_encode_ext(NULL, NULL, value, alg));
}

char *
slapi_encode_ext(Slapi_PBlock *pb, const Slapi_DN *sdn, char *value, char *alg)
{
    struct pw_scheme *enc_scheme = NULL;
    char *(*pws_enc)(char *pwd) = NULL;
    char *hashedval = NULL;
    passwdPolicy *pwpolicy = NULL;

    if (alg == NULL) /* use local scheme, or global if we can't fetch local */
    {
        pwpolicy = new_passwdPolicy(pb, (char *)slapi_sdn_get_ndn(sdn));
        pws_enc = pwpolicy->pw_storagescheme->pws_enc;

        if (pws_enc == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_encode_ext",
                          "no encoding password storage scheme found for %s\n",
                          pwpolicy->pw_storagescheme->pws_name);

            /* new_passwdPolicy registers the policy in the pblock so there is no leak */
            /* coverity[leaked_storage] */
            return NULL;
        }
    } else {
        enc_scheme = pw_name2scheme(alg);
        if (enc_scheme == NULL) {
            char *scheme_list = plugin_get_pwd_storage_scheme_list(PLUGIN_LIST_PWD_STORAGE_SCHEME);
            if (scheme_list != NULL) {
                slapi_log_err(SLAPI_LOG_ERR, "slapi_encode_ext",
                              "Invalid scheme - %s\n"
                              "Valid values are: %s\n",
                              alg, scheme_list);
                slapi_ch_free((void **)&scheme_list);
            } else {
                slapi_log_err(SLAPI_LOG_ERR, "slapi_encode_ext",
                              "Invalid scheme - %s\n"
                              "no pwdstorage scheme plugin loaded",
                              alg);
            }
            return NULL;
        }
        pws_enc = enc_scheme->pws_enc;
        free_pw_scheme(enc_scheme);
    }

    hashedval = (*pws_enc)(value);

    /* new_passwdPolicy registers the policy in the pblock so there is no leak */
    /* coverity[leaked_storage] */
    return hashedval;
}

/*
 * Return a pointer to the pw_scheme struct for scheme "name"
 * NULL is returned is no matching scheme is found.
 */

struct pw_scheme *
pw_name2scheme(char *name)
{
    struct pw_scheme *pwsp = NULL;
    struct slapdplugin *p = NULL;

    typedef int (*CMPFP)(char *, char *);
    typedef char *(*ENCFP)(char *);

    if (name == NULL || strcmp(DEFAULT_PASSWORD_SCHEME_NAME, name) == 0) {
         /* default scheme */
         char *pbkdf = "PBKDF2-SHA512";
         p = plugin_get_pwd_storage_scheme(pbkdf, strlen(pbkdf), PLUGIN_LIST_PWD_STORAGE_SCHEME);
    } else {
        /*
         * Else, get the scheme "as named".
         */
        p = plugin_get_pwd_storage_scheme(name, strlen(name), PLUGIN_LIST_PWD_STORAGE_SCHEME);
    }

    if (p != NULL) {
        pwsp = (struct pw_scheme *)slapi_ch_malloc(sizeof(struct pw_scheme));
        pwsp->pws_name = slapi_ch_strdup(p->plg_pwdstorageschemename);
        pwsp->pws_cmp = (CMPFP)p->plg_pwdstorageschemecmp;
        pwsp->pws_enc = (ENCFP)p->plg_pwdstorageschemeenc;
        pwsp->pws_len = strlen(pwsp->pws_name);
    }

    return pwsp;
}

void
free_pw_scheme(struct pw_scheme *pwsp)
{
    if (pwsp != NULL) {
        slapi_ch_free_string(&pwsp->pws_name);
        slapi_ch_free((void **)&pwsp);
    }
}

/*
 * Return the password scheme for value "val".  This is determined by
 * checking "val" against our scheme prefixes.
 *
 * If "valpwdp" is not NULL, it is set to point to the value with any
 * prefix removed.
 *
 * If no matching scheme is found and first_is_default is non-zero, the
 * first scheme is returned.  If no matching scheme is found and
 * first_is_default is zero, NULL is returned.
 */

struct pw_scheme *
pw_val2scheme(char *val, char **valpwdp, int first_is_default)
{
    struct pw_scheme *pwsp;
    int namelen, prefixlen;
    char *end, buf[PWD_MAX_NAME_LEN + 1];

    if (NULL == val) {
        return (NULL);
    }

    /*
     * Future implementors of new password mechanisms may find that this function
     * is causing them trouble. If your hash ends up as {CLEAR}{NEWMECH}.... it
     * because NEWMECH > PWD_MAX_NAME_LEN. Update pw.h!
     */

    if (*val != PWD_HASH_PREFIX_START ||
        (end = strchr(val, PWD_HASH_PREFIX_END)) == NULL ||
        (namelen = end - val - 1) > PWD_MAX_NAME_LEN) {
        if (!first_is_default) {
            return (NULL);
        }
        pwsp = pw_name2scheme("CLEAR"); /* default to first scheme */
        prefixlen = 0;
    } else {
        memcpy(buf, val + 1, namelen);
        buf[namelen] = '\0';
        pwsp = pw_name2scheme(buf);
        if (pwsp == NULL) {
            if (!first_is_default) {
                return (NULL);
            }
            pwsp = pw_name2scheme("CLEAR");
            prefixlen = 0;
        } else {
            prefixlen = pwsp->pws_len + 2;
        }
    }

    if (valpwdp != NULL) {
        *valpwdp = val + prefixlen;
    }

    return (pwsp);
}


/*
 * re-encode the password values in "vals" using a hashing algorithm
 * vals[n] is assumed to be an alloc'd Slapi_Value that can be free'd and
 * replaced.  If a value is already encoded, we do not re-encode it.
 * Return 0 if all goes well and < 0 if an error occurs.
 */

int
pw_encodevals(Slapi_Value **vals)
{
    return (pw_encodevals_ext(NULL, NULL, vals));
}

/*
 * Same as pw_encodevals, except if a pb and sdn are passed in, we will try
 * to check the password scheme specified by local password policy.
 */
int
pw_encodevals_ext(Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals)
{
    int i;
    passwdPolicy *pwpolicy = NULL;
    char *(*pws_enc)(char *pwd) = NULL;

    if ((NULL == pb) || (NULL == vals)) {
        return (0);
    }

    /* new_passwdPolicy gives us a local policy if sdn and pb are set and
       can be used to find a local policy, else we get the global policy */
    pwpolicy = new_passwdPolicy(pb, sdn ? (char *)slapi_sdn_get_ndn(sdn) : NULL);
    if (pwpolicy) {
        if (pwpolicy->pw_storagescheme) {
            pws_enc = pwpolicy->pw_storagescheme->pws_enc;
        }
    }

    /* Password scheme encryption function was not found */
    if (pws_enc == NULL) {
        return (0);
    }

    for (i = 0; vals[i] != NULL; ++i) {
        struct pw_scheme *pwsp = NULL;
        char *enc = NULL;
        if ((pwsp = pw_val2scheme((char *)slapi_value_get_string(vals[i]), NULL, 0)) != NULL) { /* JCM Innards */
            /* If the value already specifies clear storage, call the
             * clear storage plug-in */
            if (strcasecmp(pwsp->pws_name, "clear") == 0) {
                enc = (*pwsp->pws_enc)((char *)slapi_value_get_string(vals[i]));
            } else {
                free_pw_scheme(pwsp);
                continue; /* don't touch pre-encoded values */
            }
        }
        free_pw_scheme(pwsp);

        if ((!enc) && ((enc = (*pws_enc)((char *)slapi_value_get_string(vals[i]))) == NULL)) {
            return (-1);
        }
        slapi_value_free(&vals[i]);
        vals[i] = slapi_value_new_string_passin(enc);
    }

    return (0);
}

/*
 * Check if the prefix of the cipher is the one that is supposed to be
 * Extract from the whole cipher the encrypted password (remove the prefix)
 */
int
checkPrefix(char *cipher, char *schemaName, char **encrypt, char **algid)
{
    int namelen;
    /* buf contains the extracted schema name */
    char *end, buf[3 * PWD_MAX_NAME_LEN + 1];
    char *delim = NULL;

    if ((*cipher == PWD_HASH_PREFIX_START) &&
        (end = strchr(cipher, PWD_HASH_PREFIX_END)) != NULL) {
        if ((delim = strchr(cipher, PWD_PBE_DELIM)) != NULL) {
            /*
             * We have an algid in the prefix:
             *
             * {AES-<BASE64_ALG_ID>}<ENCODED PASSWORD>
             */
            if ((namelen = delim - cipher - 1) <= (3 * PWD_MAX_NAME_LEN)) {
                memcpy(buf, cipher + 1, namelen);
                buf[namelen] = '\0';

                if (strcasecmp(buf, schemaName) != 0) {
                    /* schema names are different, error */
                    return 1;
                } else {
                    char algid_buf[256];

                    /* extract the algid (length is never greater than 216 */
                    memcpy(algid_buf, delim + 1, (end - delim));
                    algid_buf[end - delim - 1] = '\0';
                    *algid = slapi_ch_strdup(algid_buf);

                    /* extract the encrypted password */
                    *encrypt = cipher + strlen(*algid) + strlen(schemaName) + 3;
                    return 0;
                }
            }
        } else if ((namelen = end - cipher - 1) <= (3 * PWD_MAX_NAME_LEN)) {
            /* no delimiter - must be old school DES */
            memcpy(buf, cipher + 1, namelen);
            buf[namelen] = '\0';
            if (strcasecmp(buf, schemaName) != 0) {
                /* schema names are different, error */
                return 1;
            } else {
                /* extract the encrypted password */
                *encrypt = cipher + strlen(schemaName) + 2;
                return 0;
            }
        }
    }
    /* cipher is not prefixed, already in clear ? */
    return -1;
}

/*
* Decode the attribute "attr_name" with one of the reversible encryption mechanism
* Returns -1 on error
* Returns 0 on success with strdup'ed plain
* Returns 1 on success with *plain=cipher
*/
int
pw_rever_decode(char *cipher, char **plain, const char *attr_name)
{
    struct pw_scheme *pwsp = NULL;
    struct slapdplugin *p = NULL;

    int ret_code = 1;

    for (p = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME); p != NULL; p = p->plg_next) {
        char *L_attr = NULL;
        int i = 0;
        char *encrypt = NULL;
        char *algid = NULL;
        int prefixOK = -1;

        /* Get the appropriate decoding function */
        for (L_attr = p->plg_argv[i]; i < p->plg_argc; L_attr = p->plg_argv[++i]) {
            if (slapi_attr_types_equivalent(L_attr, attr_name)) {
                typedef char *(*ENCFP)(char *, char *);

                pwsp = (struct pw_scheme *)slapi_ch_calloc(1, sizeof(struct pw_scheme));
                pwsp->pws_dec = (ENCFP)p->plg_pwdstorageschemedec;
                pwsp->pws_name = slapi_ch_strdup(p->plg_pwdstorageschemename);
                pwsp->pws_len = strlen(pwsp->pws_name);
                if (pwsp->pws_dec != NULL) {
                    /* check that the prefix of the cipher is the same name
                        as the scheme name */
                    prefixOK = checkPrefix(cipher, pwsp->pws_name, &encrypt, &algid);
                    if (prefixOK == -1) {
                        /* no prefix, already in clear ? */
                        *plain = cipher;
                        ret_code = 1;
                        goto free_and_return;
                    } else if (prefixOK == 1) {
                        /* scheme names are different, try the next plugin */
                        ret_code = -1;
                        free_pw_scheme(pwsp);
                        pwsp = NULL;
                        continue;
                    } else {
                        if ((*plain = (pwsp->pws_dec)(encrypt, algid)) == NULL) {
                            /* pb during decoding */
                            ret_code = -1;
                            goto free_and_return;
                        }
                        /* decoding is OK */
                        ret_code = 0;
                        goto free_and_return;
                    }
                }
                free_pw_scheme(pwsp);
                pwsp = NULL;
            }
        }
    }
free_and_return:
    if (pwsp != NULL) {
        free_pw_scheme(pwsp);
    }
    return (ret_code);
}

/*
 * Encode the attribute "attr_name" with one of the reversible encryption mechanism
 */
int
pw_rever_encode(Slapi_Value **vals, char *attr_name)
{
    char *enc;
    struct pw_scheme *pwsp = NULL;
    struct slapdplugin *p;

    if (vals == NULL) {
        return (0);
    }

    for (p = get_plugin_list(PLUGIN_LIST_REVER_PWD_STORAGE_SCHEME); p != NULL; p = p->plg_next) {
        char *L_attr = NULL;
        int i = 0, ii = 0;

        /* Get the appropriate encoding function */
        for (L_attr = p->plg_argv[ii]; ii < p->plg_argc; L_attr = p->plg_argv[++ii]) {
            if (slapi_attr_types_equivalent(L_attr, attr_name)) {
                typedef char *(*ENCFP)(char *);

                pwsp = (struct pw_scheme *)slapi_ch_calloc(1, sizeof(struct pw_scheme));

                pwsp->pws_enc = (ENCFP)p->plg_pwdstorageschemeenc;
                pwsp->pws_name = slapi_ch_strdup(p->plg_pwdstorageschemename);
                if (pwsp->pws_enc != NULL) {
                    for (i = 0; vals[i] != NULL; ++i) {
                        char *encrypt = NULL;
                        char *algid = NULL;
                        int prefixOK;

                        prefixOK = checkPrefix((char *)slapi_value_get_string(vals[i]),
                                               pwsp->pws_name,
                                               &encrypt, &algid);
                        slapi_ch_free_string(&algid);
                        if (prefixOK == 0) {
                            /* Don't touch already encoded value */
                            continue; /* don't touch pre-encoded values */
                        } else if (prefixOK == 1) {
                            /* credential is already encoded, but not with this schema. Error */
                            free_pw_scheme(pwsp);
                            return (-1);
                        }


                        if ((enc = (pwsp->pws_enc)((char *)slapi_value_get_string(vals[i]))) == NULL) {
                            free_pw_scheme(pwsp);
                            return (-1);
                        }
                        slapi_value_free(&vals[i]);
                        vals[i] = slapi_value_new_string_passin(enc);
                        free_pw_scheme(pwsp);
                        return (0);
                    }
                }
                free_pw_scheme(pwsp);
            }
        }
    }

    return (-1);
}

/* ONREPL - below are the functions moved from pw_mgmt.c.
            this is done to allow the functions to be used
            by functions linked into libslapd.
 */

/* update_pw_info is called after password is modified successfully */
/* it should update passwordHistory, and passwordExpirationTime */
/* SLAPI_ENTRY_POST_OP must be set */

int
update_pw_info(Slapi_PBlock *pb, char *old_pw)
{
    Slapi_Operation *operation = NULL;
    Connection *pb_conn;
    Slapi_Entry *e = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_Mods smods;
    passwdPolicy *pwpolicy = NULL;
    time_t pw_exp_date;
    time_t cur_time;
    const char *target_dn, *bind_dn;
    char *timestr;
    int internal_op = 0;

    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    slapi_pblock_get(pb, SLAPI_TARGET_SDN, &sdn);
    slapi_pblock_get(pb, SLAPI_REQUESTOR_NDN, &bind_dn);
    slapi_pblock_get(pb, SLAPI_ENTRY_PRE_OP, &e);
    if ((NULL == operation) || (NULL == sdn) || (NULL == e)) {
        slapi_log_err(SLAPI_LOG_ERR, "update_pw_info",
                      "Param error - no password entry/target dn/operation\n");
        return -1;
    }

    /* If we have been requested to skip updating this data, check now */
    if (slapi_operation_is_flag_set(operation, OP_FLAG_ACTION_SKIP_PWDPOLICY)) {
        /* No action required! */
        return 0;
    }

    internal_op = slapi_operation_is_flag_set(operation, SLAPI_OP_FLAG_INTERNAL);
    target_dn = slapi_sdn_get_ndn(sdn);
    pwpolicy = new_passwdPolicy(pb, target_dn);
    if (pw_is_pwp_admin(pb, pwpolicy, PWP_ADMIN_ONLY) && pwpolicy->pw_admin_skip_info) {
        return 0;
    }
    cur_time = slapi_current_utc_time();
    slapi_mods_init(&smods, 0);

    if (slapi_entry_attr_hasvalue(e, SLAPI_ATTR_OBJECTCLASS, "shadowAccount")) {
        time_t ctime = cur_time / _SEC_PER_DAY;
        timestr = slapi_ch_smprintf("%ld", ctime);
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "shadowLastChange", timestr);
        slapi_ch_free_string(&timestr);
    }

    /* update passwordHistory */
    if (old_pw != NULL && pwpolicy->pw_history == 1) {
        (void)update_pw_history(pb, sdn, old_pw);
    }

    /* Update the "pwdUpdateTime" attribute */
    if (pwpolicy->pw_track_update_time) {
        timestr = format_genTime(cur_time);
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdUpdateTime", timestr);
        slapi_ch_free((void **)&timestr);
    }

    /* update password allow change time */
    if (pwpolicy->pw_minage != 0) {
        timestr = format_genTime(time_plus_sec(cur_time, pwpolicy->pw_minage));
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordAllowChangeTime", timestr);
        slapi_ch_free((void **)&timestr);
    }

    /*
     * Fix for Bug 560707
     * Removed the restriction that the lock variables (retry count) will
     * be set only when root resets the password.
     * Now admins will also have these privileges.
     */
    if (pwpolicy->pw_lockout) {
        set_retry_cnt_mods(pb, &smods, 0);
    }

    if (!slapi_entry_attr_hasvalue(e, "passwordgraceusertime", "0")) {
        /* Clear the passwordgraceusertime from the user entry */
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordgraceusertime", "0");
    }

    if (slapi_entry_attr_hasvalue(e, "pwdReset", "TRUE")) {
        /*
         * Password was previously reset, just reset the "reset" flag for now.
         * If the password is being reset again we will catch it below...
         */
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdReset", "FALSE");
    }

    if (slapi_entry_attr_hasvalue(e, "pwdTPRReset", "TRUE")) {
        /*
         * Password TPR was previously reset, just reset the "reset" flag and
         * unset all TPR related operational attributes.
         * If the password is being reset again we will catch it below...
         */
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdTPRReset", "FALSE");
        if (slapi_entry_attr_exists(e, "pwdTPRUseCount")) {
            slapi_mods_add(&smods, LDAP_MOD_DELETE, "pwdTPRUseCount", 0, NULL);
        }
        if (slapi_entry_attr_exists(e, "pwdTPRExpireAt")) {
            slapi_mods_add(&smods, LDAP_MOD_DELETE, "pwdTPRExpireAt", 0, NULL);
        }
        if (slapi_entry_attr_exists(e, "pwdTPRValidFrom")) {
            slapi_mods_add(&smods, LDAP_MOD_DELETE, "pwdTPRValidFrom", 0, NULL);
        }
    }
    /*
     * If the password is reset by a different user, mark it the first time logon.  If this is an internal
     * operation, we have a special case for the password modify extended operation where
     * we stuff the actual user who initiated the password change in pb_conn.  We check
     * for this special case to ensure we reset the expiration date properly.
     */
    if ((internal_op && pwpolicy->pw_must_change && (!pb_conn || strcasecmp(target_dn, pb_conn->c_dn))) ||
        (!internal_op && pwpolicy->pw_must_change &&
         ((target_dn && bind_dn && strcasecmp(target_dn, bind_dn)) && pw_is_pwp_admin(pb, pwpolicy, PWP_ADMIN_OR_ROOTDN))))
    {
        pw_exp_date = NO_TIME;
        slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdReset", "TRUE");
        if ((pwpolicy->pw_tpr_maxuse >= 0) ||
            (pwpolicy->pw_tpr_delay_expire_at >= 0) ||
            (pwpolicy->pw_tpr_delay_valid_from >= 0)) {
            char *use_count, *expire_at, *valid_from;
            time_t cur_time;
            cur_time = slapi_current_utc_time();
            slapi_log_err(SLAPI_LOG_TRACE,
                          "update_pw_info",
                          "TPR password reset by an admin, pwdTPRReset=TRUE\n");
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdTPRReset", "TRUE");

            /* If useCount is -1, this means this limit is not enforced */
            if (pwpolicy->pw_tpr_maxuse >= 0) {
                use_count = "0";
            } else {
                use_count = "-1";
            }
            slapi_log_err(SLAPI_LOG_TRACE, "update_pw_info", "pwdTPRUseCount = %s\n", use_count);
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdTPRUseCount", use_count);

            /* If expire_at is -1, this means this limit is not enforced */
            if (pwpolicy->pw_tpr_delay_expire_at >= 0) {
                expire_at = format_genTime(time_plus_sec(cur_time, pwpolicy->pw_tpr_delay_expire_at));
            } else {
                expire_at = slapi_ch_strdup("-1");
            }
            slapi_log_err(SLAPI_LOG_TRACE, "update_pw_info", "pwdTPRExpireAt = %s\n", expire_at);
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdTPRExpireAt", expire_at);
            slapi_ch_free((void **)&expire_at);

            /* If valid_from is -1, this means this limit is not enforced */
            if (pwpolicy->pw_tpr_delay_valid_from >= 0) {
                valid_from = format_genTime(time_plus_sec(cur_time, pwpolicy->pw_tpr_delay_valid_from));
            } else {
                valid_from = slapi_ch_strdup("-1");
            }
            slapi_log_err(SLAPI_LOG_TRACE, "update_pw_info", "pwdTPRValidFrom = %s\n", valid_from);
            slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "pwdTPRValidFrom", valid_from);
            slapi_ch_free((void **)&valid_from);

        }
    } else if (pwpolicy->pw_exp == 1) {
        Slapi_Entry *pse = NULL;

        /* update password expiration date */
        pw_exp_date = time_plus_sec(cur_time, pwpolicy->pw_maxage);
        slapi_pblock_get(pb, SLAPI_ENTRY_POST_OP, &pse);
        if (pse) {
            char *prev_exp_date_str;
            /*
             * if the password expiry time is SLAPD_END_TIME,
             * don't roll it back
             */
            prev_exp_date_str = (char *)slapi_entry_attr_get_ref(pse, "passwordExpirationTime");
            if (prev_exp_date_str) {
                time_t prev_exp_date;

                prev_exp_date = parse_genTime(prev_exp_date_str);
                if (prev_exp_date == NO_TIME || prev_exp_date == NOT_FIRST_TIME) {
                    /* ignore as will replace */
                } else if (prev_exp_date == SLAPD_END_TIME) {
                    /* Special entries' passwords never expire */
                    pw_apply_mods(sdn, &smods);
                    slapi_mods_done(&smods);
                    return 0;
                }
            }
        } /* post op entry */
    } else if (pwpolicy->pw_must_change) {
        /*
         * pw is not changed by root, and must change pw first time
         * log on
         */
        pw_exp_date = NOT_FIRST_TIME;
    } else {
        pw_apply_mods(sdn, &smods);
        slapi_mods_done(&smods);
        return 0;
    }

    timestr = format_genTime(pw_exp_date);
    slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpirationTime", timestr);
    slapi_ch_free_string(&timestr);

    slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, "passwordExpWarned", "0");

    pw_apply_mods(sdn, &smods);
    slapi_mods_done(&smods);
    if (pb_conn) { /* no conn for internal op */
        /* reset c_needpw to 0 */
        pb_conn->c_needpw = 0;
    }
    return 0;
}

int
check_pw_minage(Slapi_PBlock *pb, const Slapi_DN *sdn, struct berval **vals __attribute__((unused)))
{
    char *dn = (char *)slapi_sdn_get_ndn(sdn); /* jcm - Had to cast away const */
    passwdPolicy *pwpolicy = NULL;
    int pwresponse_req = 0;
    Operation *pb_op;

    pwpolicy = new_passwdPolicy(pb, dn);
    slapi_pblock_get(pb, SLAPI_PWPOLICY, &pwresponse_req);
    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);

    if (!pb_op->o_isroot && pwpolicy->pw_minage) {

        Slapi_Entry *e;
        char *passwordAllowChangeTime;

        /* retrieve the entry */
        e = get_entry(pb, dn);
        if (e == NULL) {
            return (-1);
        }
        /* get passwordAllowChangeTime attribute */
        passwordAllowChangeTime = (char *)slapi_entry_attr_get_ref(e, "passwordAllowChangeTime");
        if (passwordAllowChangeTime != NULL) {
            time_t pw_allowchange_date;
            char *cur_time_str = NULL;

            pw_allowchange_date = parse_genTime(passwordAllowChangeTime);

            /* check if allow to change the password */
            cur_time_str = format_genTime(slapi_current_utc_time());
            if (difftime(pw_allowchange_date, parse_genTime(cur_time_str)) > 0) {
                if (pwresponse_req == 1) {
                    slapi_pwpolicy_make_response_control(pb, -1, -1,
                                                         LDAP_PWPOLICY_PWDTOOYOUNG);
                }
                slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                              "password within minimum age: Entry (%s) Policy (%s)\n",
                              dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");

                pw_send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL, "within password minimum age", 0, NULL);
                slapi_entry_free(e);
                slapi_ch_free((void **)&cur_time_str);
                return (1);
            }
            slapi_ch_free((void **)&cur_time_str);
        }
        slapi_entry_free(e);
    }
    return (0);
}


/* pam_pwquality functions */
static int
palindrome(const char *new)
{
    int i, j;

    i = strlen(new);
    for (j = 0;j < i;j++)
        if (new[i - j - 1] != new[j])
            return 0;

    return 1;
}

static int
pw_sequence_sets(const char *new, int32_t max_seq, int check_sets)
{
    char c;
    int i;
    int sequp = 1;
    int seqdown = 1;

    if (new[0] == '\0')
        return 0;

    for (i = 1; new[i]; i++) {
        c = new[i-1];
        if (new[i] == c+1) {
            ++sequp;
            if (sequp > max_seq) {
                if (check_sets) {
                    /* remember this seq, can call pw_sequence on remaining password */
                    char *remaining = slapi_ch_smprintf("%s", new + i);
                    char token[11] = {0};

                    memcpy(token, new + (i - max_seq), max_seq);
                    if (strstr(remaining, token)) {
                        /* we have a duplicate set */
                        slapi_ch_free_string(&remaining);
                        return 1;
                    }
                    slapi_ch_free_string(&remaining);
                } else {
                    return 1;
                }
            }
            seqdown = 1;
        } else if (new[i] == c-1) {
            ++seqdown;
            if (seqdown > max_seq) {
                if (check_sets) {
                    /* remember this seq, so we can check if it occurs again */
                    char *remaining = slapi_ch_smprintf("%s", new + i);
                    char token[11] = {0};

                    memcpy(token, new + (i - max_seq), max_seq);
                    if (strstr(remaining, token)) {
                        /* we have a duplicate set */
                        slapi_ch_free_string(&remaining);
                        return 1;
                    }
                    slapi_ch_free_string(&remaining);

                } else {
                    return 1;
                }
            }
            sequp = 1;
        } else {
            sequp = 1;
            seqdown = 1;
        }
    }
    return 0;
}

static int
pw_sequence(const char *new, int32_t max_seq)
{
    return pw_sequence_sets(new, max_seq, 0);
}

static int
pw_max_class_repeats(const char *new, int32_t max_repeats)
{
    int digits = 0;
    int uppers = 0;
    int lowers = 0;
    int others = 0;
    int i;
    enum { NONE, DIGIT, UCASE, LCASE, OTHER } prevclass = NONE;
    int sameclass = 0;

    for (i = 0; new[i]; i++) {
        if (isdigit(new[i])) {
            digits++;
            if (prevclass != DIGIT) {
                prevclass = DIGIT;
                sameclass = 1;
            } else {
                sameclass++;
            }
        } else if (isupper (new[i])) {
            uppers++;
            if (prevclass != UCASE) {
                prevclass = UCASE;
                sameclass = 1;
            } else {
                sameclass++;
            }
        } else if (islower (new[i])) {
            lowers++;
            if (prevclass != LCASE) {
                prevclass = LCASE;
                sameclass = 1;
            } else {
                sameclass++;
            }
        } else {
            others++;
            if (prevclass != OTHER) {
                prevclass = OTHER;
                sameclass = 1;
            } else {
                sameclass++;
            }
        }
        if (sameclass > max_repeats) {
            return 1;
        }
    }
    return 0;
}

static void
report_pw_violation(Slapi_PBlock *pb, char *dn, int pwresponse_req, char *fmt, ...)
{
    char errormsg[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
    va_list msg;
    passwdPolicy *pwpolicy = slapi_pblock_get_pwdpolicy(pb);

    va_start(msg, fmt);
    PR_vsnprintf(errormsg, SLAPI_DSE_RETURNTEXT_SIZE - 1, fmt, msg);
    if (pwresponse_req == 1) {
        slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_INVALIDPWDSYNTAX);
    }
    pw_send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL, errormsg, 0, NULL);
    va_end(msg);
    slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                  "%s: Entry (%s) Policy (%s)\n",
                  errormsg, dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
}

/* check_pw_syntax is called before add or modify operation on userpassword attribute*/

int
check_pw_syntax(Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals, char **old_pw, Slapi_Entry *e, int mod_op)
{
    return (check_pw_syntax_ext(pb, sdn, vals, old_pw, e, mod_op, NULL));
}

int
check_pw_syntax_ext(Slapi_PBlock *pb, const Slapi_DN *sdn, Slapi_Value **vals, char **old_pw, Slapi_Entry *e, int mod_op, Slapi_Mods *smods)
{
    Slapi_Attr *attr;
    Slapi_Value **va = NULL;
    int i, pwresponse_req = 0;
    int is_replication = 0;
    int internal_op = 0;
    char *dn = (char *)slapi_sdn_get_ndn(sdn); /* jcm - Had to cast away const */
    char *pwd = NULL;
    char *p = NULL;
    passwdPolicy *pwpolicy = NULL;
    Slapi_Operation *operation = NULL;
    Connection *pb_conn;
    char errormsg[SLAPI_DSE_RETURNTEXT_SIZE] = {0};

    /*
     * check_pw_syntax_ext could be called with mod_op == LDAP_MOD_DELETE.
     * In that case, no need to check the password syntax, but just returns
     * PASS == 0.
     */
    if (LDAP_MOD_DELETE == (mod_op & LDAP_MOD_OP)) {
        /* check if the entry exists or not */
        e = get_entry(pb, dn);
        if (e == NULL) {
            return -1;
        }
        slapi_entry_free(e);
        return 0;
    }
    if (NULL == vals) {
        slapi_log_err(SLAPI_LOG_ERR, "check_pw_syntax_ext",
                      "No passwords to check\n");
        return -1;
    }

    pwpolicy = new_passwdPolicy(pb, dn);
    slapi_pblock_get(pb, SLAPI_PWPOLICY, &pwresponse_req);

    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replication);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    if (NULL == operation) {
        slapi_log_err(SLAPI_LOG_ERR, "check_pw_syntax_ext", "No slapi operation\n");
        return -1;
    }
    slapi_pblock_get(pb, SLAPI_CONNECTION, &pb_conn);
    internal_op = slapi_operation_is_flag_set(operation, SLAPI_OP_FLAG_INTERNAL);

    /*
     * Check if password is already hashed and reject if so.  We need to
     * allow the root DN, password admins, and replicated ops to send
     * pre-hashed passwords. We also check for a connection object
     * when processing an internal operation to handle a special
     * case for the password modify extended operation.
     */
    for (i = 0; vals[i] != NULL; ++i) {
        if (slapi_is_encoded((char *)slapi_value_get_string(vals[i]))) {
            if (!is_replication && !config_get_allow_hashed_pw() &&
                ((internal_op && pb_conn && !slapi_dn_isroot(pb_conn->c_dn)) ||
                 (!internal_op && !pw_is_pwp_admin(pb, pwpolicy, PWP_ADMIN_OR_ROOTDN))))
            {
                report_pw_violation(pb, dn, pwresponse_req, "invalid password syntax - passwords with storage scheme are not allowed");
                return (1);
            } else {
                /* We want to skip syntax checking since this is a pre-hashed password. But if the user
                 * has thrown caution to wind and allowed hashed passwords, we capture the history
                 */
                if (config_get_allow_hashed_pw() && pwpolicy->pw_history) {
                    e = get_entry(pb, dn);
                    if (e == NULL) {
                        return -1;
                    }
                    attr = attrlist_find(e->e_attrs, SLAPI_USERPWD_ATTR);
                    if (attr && !valueset_isempty(&attr->a_present_values)) {
                        if (old_pw) {
                            va = valueset_get_valuearray(&attr->a_present_values);
                            if (va != NULL) {
                                *old_pw = slapi_ch_strdup(slapi_value_get_string(va[0]));
                            } else {
                                *old_pw = NULL;
                            }
                        }
                    }
                    slapi_entry_free(e);
                }
                return (0);
            }
        }
    }

    if (pwpolicy->pw_syntax == LDAP_ON) {
        for (i = 0; vals[i] != NULL; ++i) {
            int syntax_violation = 0;
            int num_digits = 0;
            int num_alphas = 0;
            int num_uppers = 0;
            int num_lowers = 0;
            int num_specials = 0;
            int num_8bit = 0;
            int num_repeated = 0;
            int max_repeated = 0;
            int num_categories = 0;
            char **bad_words_array;

            pwd = (char *)slapi_value_get_string(vals[i]);

            /* Check dictionary */
            if (pwpolicy->pw_check_dict) {
                const char *crack_msg;
                if ((crack_msg = FascistCheck(pwd, pwpolicy->pw_dict_path))) {
                    report_pw_violation(pb, dn, pwresponse_req, "Password failed dictionary check: %s", crack_msg);
                    return (1);
                }
            }

            /* check palindrome */
            if (pwpolicy->pw_palindrome) {
                if (palindrome(pwd)) {
                    report_pw_violation(pb, dn, pwresponse_req, "Password is a palindrome");
                    return (1);
                }
            }

            /* Check for bad words */
            bad_words_array = config_get_pw_bad_words_array();
            if (bad_words_array) {
                for (size_t b = 0; bad_words_array && bad_words_array[b]; b++) {
                    if (strcasestr(pwd, bad_words_array[b])) {
                        report_pw_violation(pb, dn, pwresponse_req, "Password contains a restricted word");
                        charray_free(bad_words_array);
                        return (1);
                    }
                }
                charray_free(bad_words_array);
            }

            /* Check for sequences */
            if (pwpolicy->pw_max_seq) {
                if (pw_sequence(pwd, pwpolicy->pw_max_seq)) {
                    report_pw_violation(pb, dn, pwresponse_req, "Password contains a monotonic sequence");
                    return (1);
                }
            }

            /* Check for sets of sequences */
            if (pwpolicy->pw_seq_char_sets) {
                if (pw_sequence_sets(pwd, pwpolicy->pw_seq_char_sets, 1)){
                    report_pw_violation(pb, dn, pwresponse_req, "Password contains repeated identical sequences");
                    return (1);
                }
            }

            /* Check for max repeated characters from the same class */
            if (pwpolicy->pw_max_class_repeats) {
                if (pw_max_class_repeats(pwd, pwpolicy->pw_max_class_repeats)){
                    report_pw_violation(pb, dn, pwresponse_req,
                            "Password contains too many repeated characters from the same character class");
                    return (1);
                }
            }

            /* check for the minimum password length */
            if (pwpolicy->pw_minlength > (int)ldap_utf8characters((char *)pwd)) {
                report_pw_violation(pb, dn, pwresponse_req,
                        "invalid password syntax - password must be at least %d characters long",
                        pwpolicy->pw_minlength);
                return (1);
            }

            /* check character types */
            p = pwd;
            while (p && *p) {
                if (ldap_utf8isdigit(p)) {
                    num_digits++;
                } else if (ldap_utf8isalpha(p)) {
                    num_alphas++;
                    if (slapi_utf8isLower((unsigned char *)p)) {
                        num_lowers++;
                    } else {
                        num_uppers++;
                    }
                } else {
                    /* check if this is an 8-bit char */
                    if (*p & 128) {
                        num_8bit++;
                    } else {
                        num_specials++;
                    }
                }

                /* check for repeating characters. If this is the
                   first char of the password, no need to check */
                if (pwd != p) {
                    int len = ldap_utf8len(p);
                    char *prev_p = ldap_utf8prev(p);

                    if (len == ldap_utf8len(prev_p)) {
                        if (memcmp(p, prev_p, len) == 0) {
                            num_repeated++;
                            if (max_repeated < num_repeated) {
                                max_repeated = num_repeated;
                            }
                        } else {
                            num_repeated = 0;
                        }
                    } else {
                        num_repeated = 0;
                    }
                }

                p = ldap_utf8next(p);
            }

            /* tally up the number of character categories */
            if (num_digits > 0)
                ++num_categories;
            if (num_uppers > 0)
                ++num_categories;
            if (num_lowers > 0)
                ++num_categories;
            if (num_specials > 0)
                ++num_categories;
            if (num_8bit > 0)
                ++num_categories;

            /* check for character based syntax limits */
            if (pwpolicy->pw_mindigits > num_digits) {
                syntax_violation = 1;
                PR_snprintf(errormsg, sizeof(errormsg) - 1,
                            "invalid password syntax - password must contain at least %d digit characters",
                            pwpolicy->pw_mindigits);
            } else if (pwpolicy->pw_minalphas > num_alphas) {
                syntax_violation = 1;
                PR_snprintf(errormsg, sizeof(errormsg) - 1,
                            "invalid password syntax - password must contain at least %d alphabetic characters",
                            pwpolicy->pw_minalphas);
            } else if (pwpolicy->pw_minuppers > num_uppers) {
                syntax_violation = 1;
                PR_snprintf(errormsg, sizeof(errormsg) - 1,
                            "invalid password syntax - password must contain at least %d uppercase characters",
                            pwpolicy->pw_minuppers);
            } else if (pwpolicy->pw_minlowers > num_lowers) {
                syntax_violation = 1;
                PR_snprintf(errormsg, sizeof(errormsg) - 1,
                            "invalid password syntax - password must contain at least %d lowercase characters",
                            pwpolicy->pw_minlowers);
            } else if (pwpolicy->pw_minspecials > num_specials) {
                syntax_violation = 1;
                PR_snprintf(errormsg, sizeof(errormsg) - 1,
                            "invalid password syntax - password must contain at least %d special characters",
                            pwpolicy->pw_minspecials);
            } else if (pwpolicy->pw_min8bit > num_8bit) {
                syntax_violation = 1;
                PR_snprintf(errormsg, sizeof(errormsg) - 1,
                            "invalid password syntax - password must contain at least %d 8-bit characters",
                            pwpolicy->pw_min8bit);
            } else if ((pwpolicy->pw_maxrepeats != 0) && (pwpolicy->pw_maxrepeats < (max_repeated + 1))) {
                syntax_violation = 1;
                PR_snprintf(errormsg, sizeof(errormsg) - 1,
                            "invalid password syntax - a character cannot be repeated more than %d times",
                            (pwpolicy->pw_maxrepeats));
            } else if (pwpolicy->pw_mincategories > num_categories) {
                syntax_violation = 1;
                PR_snprintf(errormsg, sizeof(errormsg) - 1,
                            "invalid password syntax - password must contain at least %d character "
                            "categories (valid categories are digit, uppercase, lowercase, special, and 8-bit characters)",
                            pwpolicy->pw_mincategories);
            }

            /* If the password failed syntax checking, send the result and return */
            if (syntax_violation) {
                if (pwresponse_req == 1) {
                    slapi_pwpolicy_make_response_control(pb, -1, -1,
                                                         LDAP_PWPOLICY_INVALIDPWDSYNTAX);
                }
                pw_send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL, errormsg, 0, NULL);
                slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                              "%s: Entry (%s) Policy (%s)\n",
                              errormsg, dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
                return (1);
            }
        }
    }

    /* get the entry and check for the password history if this is called by a modify operation */
    if (mod_op) {
    retry:
        /* retrieve the entry */
        e = get_entry(pb, dn);
        if (e == NULL) {
            return (-1);
        }

        /* check for password history */
        if (pwpolicy->pw_history == 1) {
            attr = attrlist_find(e->e_attrs, "passwordHistory");
            if (pwpolicy->pw_inhistory && attr && !valueset_isempty(&attr->a_present_values)) {
                /* Resetting password history array if necessary. */
                if (0 == update_pw_history(pb, sdn, NULL)) {
                    /* There was an update in the password history.  Retry... */
                    slapi_entry_free(e);
                    goto retry;
                }
                va = attr_get_present_values(attr);
                if (pw_in_history(va, vals[0]) == 0) {
                    if (pwresponse_req == 1) {
                        slapi_pwpolicy_make_response_control(pb, -1, -1, LDAP_PWPOLICY_PWDINHISTORY);
                    }
                    pw_send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL, "password in history", 0, NULL);
                    slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                                  "password in history: Entry (%s) Policy (%s)\n",
                                  dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
                    slapi_entry_free(e);
                    return (1);
                }
            }

            /* get current password. check it and remember it  */
            attr = attrlist_find(e->e_attrs, "userpassword");
            if (attr && !valueset_isempty(&attr->a_present_values)) {
                va = valueset_get_valuearray(&attr->a_present_values);
                if (slapi_is_encoded((char *)slapi_value_get_string(vals[0]))) {
                    if (slapi_attr_value_find(attr, (struct berval *)slapi_value_get_berval(vals[0])) == 0) {
                        pw_send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL, "password in history", 0, NULL);
                        slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                                      "password in history: Entry (%s) Policy (%s)\n",
                                      dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
                        slapi_entry_free(e);
                        return (1);
                    }
                } else {
                    if (slapi_pw_find_sv(va, vals[0]) == 0) {
                        pw_send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL, "password in history", 0, NULL);
                        slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                                      "password in history: Entry (%s) Policy (%s)\n",
                                      dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
                        slapi_entry_free(e);
                        return (1);
                    }
                }
                /* We copy the 1st value of the userpassword attribute.
                 * This is because password policy assumes that there's only one
                 *  password in the userpassword attribute.
                 */
                if (old_pw) {
                    *old_pw = slapi_ch_strdup(slapi_value_get_string(va[0]));
                }
            } else {
                if (old_pw) {
                    *old_pw = NULL;
                }
            }
        }
    }

    /* check for trivial words if syntax checking is enabled */
    if (pwpolicy->pw_syntax == LDAP_ON) {
        char **user_attrs_array;
        /* e is null if this is an add operation*/
        if (check_trivial_words(pb, e, vals, "uid", pwpolicy->pw_mintokenlength, smods) == 1 ||
            check_trivial_words(pb, e, vals, "cn", pwpolicy->pw_mintokenlength, smods) == 1 ||
            check_trivial_words(pb, e, vals, "sn", pwpolicy->pw_mintokenlength, smods) == 1 ||
            check_trivial_words(pb, e, vals, "givenname", pwpolicy->pw_mintokenlength, smods) == 1 ||
            check_trivial_words(pb, e, vals, "ou", pwpolicy->pw_mintokenlength, smods) == 1 ||
            check_trivial_words(pb, e, vals, "mail", pwpolicy->pw_mintokenlength, smods) == 1)
        {
            if (mod_op) {
                slapi_entry_free(e);
            }

            return 1;
        }
        /* Check user attributes */
        user_attrs_array = config_get_pw_user_attrs_array();
        if (user_attrs_array) {
            for (size_t a = 0; user_attrs_array && user_attrs_array[a]; a++) {
                if (check_trivial_words(pb, e, vals, user_attrs_array[a], pwpolicy->pw_mintokenlength, smods) == 1 ){
                    if (mod_op) {
                        slapi_entry_free(e);
                    }
                    charray_free(user_attrs_array);
                    return 1;
                }
            }
            charray_free(user_attrs_array);
        }
    }

    if (mod_op) {
        /* free e only when called by modify operation */
        slapi_entry_free(e);
    }

    return 0; /* success */
}

/*
 * Get the old password -used by password admin so we properly
 * update pw history when reseting a password.
 */
void
get_old_pw(Slapi_PBlock *pb, const Slapi_DN *sdn, char **old_pw)
{
    Slapi_Entry *e = NULL;
    Slapi_Value **va = NULL;
    Slapi_Attr *attr = NULL;
    char *dn = (char *)slapi_sdn_get_ndn(sdn);

    e = get_entry(pb, dn);
    if (e == NULL) {
        return;
    }

    /* get current password, and remember it  */
    attr = attrlist_find(e->e_attrs, "userpassword");
    if (attr && !valueset_isempty(&attr->a_present_values)) {
        va = valueset_get_valuearray(&attr->a_present_values);
        *old_pw = slapi_ch_strdup(slapi_value_get_string(va[0]));
    } else {
        *old_pw = NULL;
    }

    slapi_entry_free(e);
}

/*
 * Basically, h0 and h1 must be longer than GENERALIZED_TIME_LENGTH.
 */
static int
pw_history_cmp(const void *h0, const void *h1)
{
    if (!h0) {
        if (!h1) {
            return 0;
        } else {
            return -1;
        }
    } else {
        if (!h1) {
            return 1;
        } else {
            char *h0str = *(char **)h0;
            char *h1str = *(char **)h1;
            size_t h0sz = strlen(h0str);
            size_t h1sz = strlen(h1str);
            if ((h0sz < GENERALIZED_TIME_LENGTH) ||
                (h1sz < GENERALIZED_TIME_LENGTH)) {
                /* too short for the history str. */
                return h0sz - h1sz;
            }
            return PL_strncmp(h0str, h1str, GENERALIZED_TIME_LENGTH);
        }
    }
}

static int
update_pw_history(Slapi_PBlock *pb, const Slapi_DN *sdn, char *old_pw)
{
    time_t cur_time;
    int res = 1; /* no update, by default */
    Slapi_Entry *e = NULL;
    LDAPMod attribute;
    LDAPMod *list_of_mods[2];
    Slapi_PBlock *mod_pb;
    char *str = NULL;
    passwdPolicy *pwpolicy = NULL;
    const char *dn = slapi_sdn_get_dn(sdn);
    char **values_replace = NULL;
    int vacnt = 0;
    int vacnt_todelete = 0;

    pwpolicy = new_passwdPolicy(pb, dn);

    if (pwpolicy->pw_inhistory == 0){
        /* We are only enforcing the current password, just return */
        return res;
    }

    /* retrieve the entry */
    e = get_entry(pb, dn);
    if (e == NULL) {
        return res;
    }

    /* get password history */
    values_replace = slapi_entry_attr_get_charray_ext(e, "passwordHistory", &vacnt);
    if (old_pw) {
        /* we have a password to replace with the oldest one in the history. */
        if (!values_replace || !vacnt) { /* This is the first one to store */
            slapi_ch_array_free(values_replace);
            values_replace = (char **)slapi_ch_calloc(2, sizeof(char *));
        }
    } else {
        /* we are checking the history size if it stores more than the current inhistory count. */
        if (!values_replace || !vacnt) { /* nothing to revise */
            res = 1;
            goto bail;
        }
        /*
         * If revising the passwords in the passwordHistory values
         * and the password count in the value array is less than the inhistory,
         * we have nothing to do.
         */
        if (vacnt <= pwpolicy->pw_inhistory) {
            res = 1;
            goto bail;
        }
        vacnt_todelete = vacnt - pwpolicy->pw_inhistory;
    }

    cur_time = slapi_current_utc_time();
    str = format_genTime(cur_time);
    /* values_replace is sorted. */
    if (old_pw) {
        if (vacnt >= pwpolicy->pw_inhistory) {
            slapi_ch_free_string(&values_replace[0]);
            values_replace[0] = slapi_ch_smprintf("%s%s", str, old_pw);
        } else {
            /* add old_pw at the end of password history */
            values_replace = (char **)slapi_ch_realloc((char *)values_replace, sizeof(char *) * (vacnt + 2));
            values_replace[vacnt] = slapi_ch_smprintf("%s%s", str, old_pw);
            values_replace[vacnt + 1] = NULL;
        }
        qsort((void *)values_replace, vacnt, (size_t)sizeof(char *), pw_history_cmp);
    } else {
        int i;
        /* vacnt > pwpolicy->pw_inhistory */
        for (i = 0; i < vacnt_todelete; i++) {
            slapi_ch_free_string(&values_replace[i]);
        }
        memmove(values_replace, values_replace + vacnt_todelete, sizeof(char *) * pwpolicy->pw_inhistory);
        values_replace[pwpolicy->pw_inhistory] = NULL;
    }

    /* modify the attribute */
    attribute.mod_type = "passwordHistory";
    attribute.mod_op = LDAP_MOD_REPLACE;
    attribute.mod_values = values_replace;

    list_of_mods[0] = &attribute;
    list_of_mods[1] = NULL;

    mod_pb = slapi_pblock_new();
    slapi_modify_internal_set_pb_ext(mod_pb, sdn, list_of_mods, NULL, NULL, pw_get_componentID(), 0);
    slapi_modify_internal_pb(mod_pb);
    slapi_pblock_get(mod_pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
    if (res != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_ERR, "update_pw_history",
                      "Modify error %d on entry '%s'\n", res, dn);
    }
    slapi_pblock_destroy(mod_pb);
    slapi_ch_free_string(&str);
bail:
    slapi_ch_array_free(values_replace);
    slapi_entry_free(e);
    return res;
}

static int
pw_in_history(Slapi_Value **history_vals, const Slapi_Value *pw_val)
{
    Slapi_Value **trimmed_history = NULL;
    int num_history_vals = 0;
    int i;
    int ret = -1;
    const char *pw_str = slapi_value_get_string(pw_val);

    if (slapi_is_encoded((char *)pw_str)) {
        /* If the password is encoded, we just do a string match with all previous passwords */
        for (i = 0; history_vals[i] != NULL; i++) {
            const char *h_val = slapi_value_get_string(history_vals[i]);

            if (h_val != NULL &&
                slapi_value_get_length(history_vals[i]) >= 14) {
                int pos = 14;
                if (h_val[pos] == 'Z')
                    pos++;
                if (strcmp(&(h_val[pos]), pw_str) == 0) {
                    /* Password found */
                    /* Let's just return */
                    return (0);
                }
            }
        }
    } else { /* Password is in clear */
        /* Count the number of history vals. */
        for (i = 0; history_vals[i] != NULL; i++) {
            num_history_vals++;
        }

        /* Allocate the array */
        trimmed_history = (Slapi_Value **)slapi_ch_calloc(num_history_vals + 1, sizeof(Slapi_Value *));

        /* strip the timestamps  */
        for (i = 0; history_vals[i] != NULL; i++) {
            char *h_val = (char *)slapi_value_get_string(history_vals[i]);
            size_t h_len = slapi_value_get_length(history_vals[i]);

            /* Allocate a value and put it in the array. */
            trimmed_history[i] = (Slapi_Value *)slapi_ch_calloc(1, sizeof(Slapi_Value));

            if (h_val != NULL &&
                h_len >= 14) {
                /* LP: With the new genTime, the password history format has changed */
                int pos = 14;
                if (h_val[pos] == 'Z')
                    pos++;
                trimmed_history[i]->bv.bv_val = &(h_val[pos]);
                trimmed_history[i]->bv.bv_len = h_len - pos;
            } else {
                trimmed_history[i]->bv.bv_val = NULL;
                trimmed_history[i]->bv.bv_len = 0;
            }
        }

        /* NULL terminate the array. */
        trimmed_history[i] = NULL;

        /* Check if the new password is in the trimmed history list. */
        ret = slapi_pw_find_sv(trimmed_history, pw_val);

        /* Free the trimmed values. */
        for (i = 0; trimmed_history[i] != NULL; i++) {
            slapi_ch_free((void **)&trimmed_history[i]);
        }

        /* Free the array. */
        slapi_ch_free((void **)&trimmed_history);
    }

    return (ret);
}

int
slapi_add_pwd_control(Slapi_PBlock *pb, char *arg, long time)
{
    LDAPControl new_ctrl;
    char buf[22] = {0};

    slapi_log_err(SLAPI_LOG_TRACE, "slapi_add_pwd_control", "=>\n");

    sprintf(buf, "%ld", time);
    new_ctrl.ldctl_oid = arg;
    new_ctrl.ldctl_value.bv_val = buf;
    new_ctrl.ldctl_value.bv_len = strlen(buf);
    new_ctrl.ldctl_iscritical = 0; /* 0 = false. */

    if (slapi_pblock_set(pb, SLAPI_ADD_RESCONTROL, &new_ctrl) != 0) {
        return (-1);
    }

    return (0);
}

void
add_password_attrs(Slapi_PBlock *pb, Operation *op __attribute__((unused)), Slapi_Entry *e)
{
    struct berval bv;
    struct berval *bvals[2];
    Slapi_Attr **a, **next;
    passwdPolicy *pwpolicy = NULL;
    const char *dn = slapi_entry_get_ndn(e);
    int has_allowchangetime = 0, has_expirationtime = 0;
    time_t existing_exptime = 0;
    time_t exptime = 0;
    int isShadowAccount = 0;
    int has_shadowLastChange = 0;

    slapi_log_err(SLAPI_LOG_TRACE, "add_password_attrs", "=>\n");

    bvals[0] = &bv;
    bvals[1] = NULL;

    if (slapi_entry_attr_hasvalue(e, SLAPI_ATTR_OBJECTCLASS, "shadowAccount")) {
        isShadowAccount = 1;
    }

    /* If passwordexpirationtime is specified by the user, don't
       try to assign the initial value */
    for (a = &e->e_attrs; a && *a; a = next) {
        if (!strcasecmp((*a)->a_type, "passwordexpirationtime")) {
            Slapi_Value *sval;
            if (slapi_attr_first_value(*a, &sval) == 0) {
                const struct berval *pw_bv = slapi_value_get_berval(sval);
                existing_exptime = parse_genTime(pw_bv->bv_val);
            }
            has_expirationtime = 1;

        } else if (!strcasecmp((*a)->a_type, "passwordallowchangetime")) {
            has_allowchangetime = 1;
        } else if (isShadowAccount && !strcasecmp((*a)->a_type, "shadowlastchange")) {
            has_shadowLastChange = 1;
        }
        next = &(*a)->a_next;
    }

    if (has_allowchangetime && has_expirationtime && has_shadowLastChange) {
        return;
    }

    pwpolicy = new_passwdPolicy(pb, dn);

    if (!has_expirationtime && (pwpolicy->pw_exp || pwpolicy->pw_must_change)) {
        if (pwpolicy->pw_must_change) {
            /* must change password when first time logon */
            bv.bv_val = format_genTime(NO_TIME);
        } else if (pwpolicy->pw_exp) {
            exptime = time_plus_sec(slapi_current_utc_time(), pwpolicy->pw_maxage);
            bv.bv_val = format_genTime(exptime);
        }
        bv.bv_len = strlen(bv.bv_val);
        slapi_entry_attr_merge(e, "passwordexpirationtime", bvals);
        slapi_ch_free_string(&bv.bv_val);
    }
    if (isShadowAccount && !has_shadowLastChange) {
        if (pwpolicy->pw_must_change) {
            /* must change password when first time logon */
            bv.bv_val = slapi_ch_smprintf("0");
        } else {
            exptime = slapi_current_utc_time() / _SEC_PER_DAY;
            bv.bv_val = slapi_ch_smprintf("%ld", exptime);
        }
        bv.bv_len = strlen(bv.bv_val);
        slapi_entry_attr_merge(e, "shadowLastChange", bvals);
        slapi_ch_free_string(&bv.bv_val);
    }

    /*
     * If the password minimum age is not 0, calculate when the password
     * is allowed to be changed again and store the result
     * in passwordallowchangetime in the user's entry.
     * If the password has expired, don't add passwordallowchangetime,
     * otherwise if the user has grace logins, they can't be used to change
     * the password if we set a passwordallowchangetime in the future.
     */
    if (!has_allowchangetime && pwpolicy->pw_minage != 0 &&
        (has_expirationtime && existing_exptime > slapi_current_utc_time())) {
        bv.bv_val = format_genTime(time_plus_sec(slapi_current_utc_time(), pwpolicy->pw_minage));
        bv.bv_len = strlen(bv.bv_val);

        slapi_entry_attr_merge(e, "passwordallowchangetime", bvals);
        slapi_ch_free_string(&bv.bv_val);
    }
    /* new_passwdPolicy registers the policy in the pblock so there is no leak */
    /* coverity[leaked_storage] */
}

static int
check_trivial_words(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Value **vals, char *attrtype, int toklen, Slapi_Mods *smods)
{
    Slapi_Attr *attr = NULL;
    Slapi_Mod *smodp = NULL, *smod = NULL;
    Slapi_ValueSet *vs = NULL;
    Slapi_Value *valp = NULL;
    passwdPolicy *pwpolicy = slapi_pblock_get_pwdpolicy(pb);
    struct berval *bvp = NULL;
    int i, pwresponse_req = 0;

    slapi_pblock_get(pb, SLAPI_PWPOLICY, &pwresponse_req);

    /* Get a list of present values for attrtype in the existing entry, if there is one */
    if (e != NULL) {
        if ((attr = attrlist_find(e->e_attrs, attrtype)) &&
            (!valueset_isempty(&attr->a_present_values))) {
            /* allocate and add present values to valueset */
            slapi_attr_get_valueset(attr, &vs);
        }
    }

    /* allocate new one if not allocated above by
       slapi_attr_get_valueset */
    if (!vs) {
        vs = slapi_valueset_new();
    }

    /* Get a list of new values for attrtype from the operation */
    if (smods && (smod = slapi_mod_new())) {
        for (smodp = slapi_mods_get_first_smod(smods, smod);
             smodp != NULL; smodp = slapi_mods_get_next_smod(smods, smod)) {
            /* Operation has new values for attrtype */
            if (PL_strcasecmp(attrtype, slapi_mod_get_type(smodp)) == 0) {
                /* iterate through smodp values and add them if they don't exist */
                for (bvp = slapi_mod_get_first_value(smodp); bvp != NULL;
                     bvp = slapi_mod_get_next_value(smodp)) {
                    /* Add new value to valueset */
                    valp = slapi_value_new_berval(bvp);
                    slapi_valueset_add_value_ext(vs, valp, SLAPI_VALUE_FLAG_PASSIN);
                    valp = NULL;
                }
            }
        }
        /* Free smod */
        slapi_mod_free(&smod);
        smod = NULL;
        smodp = NULL;
    }

    /* If valueset isn't empty, we need to check if the password contains the values */
    if (slapi_valueset_count(vs) != 0) {
        for (i = slapi_valueset_first_value(vs, &valp);
             (i != -1) && (valp != NULL);
             i = slapi_valueset_next_value(vs, i, &valp)) {
            char *sp, *ep, *wp;
            int found = 0;
            /* If the value is smaller than the max token length,
             * we don't need to check the password */
            if ((int)ldap_utf8characters(slapi_value_get_string(valp)) < toklen)
                continue;

            sp = slapi_ch_strdup(slapi_value_get_string(valp));
            ep = sp + strlen(sp);
            ep = ldap_utf8prevn(sp, ep, toklen);
            if (!ep || (sp > ep)) {
                slapi_ch_free_string(&sp);
                continue;
            }
            /* See if the password contains the value */
            for (wp = sp; wp && (wp <= ep); wp = ldap_utf8next(wp)) {
                char *tp = ldap_utf8nextn(wp, toklen);
                char c;
                if (tp) {
                    c = *tp;
                    *tp = '\0';
                } else {
                    break;
                }
                if (PL_strcasestr(slapi_value_get_string(vals[0]), wp)) {
                    found = 1;
                }
                *tp = c;
            }
            slapi_ch_free_string(&sp);
            if (found) {
                if (pwresponse_req == 1) {
                    slapi_pwpolicy_make_response_control(pb, -1, -1,
                                                         LDAP_PWPOLICY_INVALIDPWDSYNTAX);
                }
                pw_send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL,
                                    "invalid password syntax - password based off of user entry", 0, NULL);
                slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                              "password based off of user entry (attr=%s token_len=%d): Entry (%s) Policy (%s)\n",
                              attrtype, toklen, slapi_entry_get_dn_const(e),
                              pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
                /* Free valueset */
                slapi_valueset_free(vs);
                return (1);
            }
        }
    }

    /* Free valueset */
    slapi_valueset_free(vs);
    return (0);
}

int
pw_is_pwp_admin(Slapi_PBlock *pb, passwdPolicy *pwp, int rootdn_flag)
{
    Slapi_DN *bind_sdn = NULL;
    int i;

    int is_requestor_root = 0;
    slapi_pblock_get(pb, SLAPI_REQUESTOR_ISROOT, &is_requestor_root);

    /* first check if it's root */
    if (is_requestor_root && rootdn_flag == PWP_ADMIN_OR_ROOTDN) {
        return 1;

    }
    /* now check if it's a Password Policy Administrator */
    slapi_pblock_get(pb, SLAPI_REQUESTOR_SDN, &bind_sdn);
    if (bind_sdn == NULL) {
        return 0;
    }
    for (i = 0; pwp->pw_admin_user && pwp->pw_admin_user[i]; i++) {
        if (slapi_sdn_compare(bind_sdn, pwp->pw_admin_user[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

static void
pw_get_admin_users(passwdPolicy *pwp)
{
    Slapi_PBlock *pb = NULL;
    const Slapi_DN *sdn = pwp->pw_admin;
    char **uniquemember_vals = NULL;
    char **member_vals = NULL;
    const char *binddn = slapi_sdn_get_dn(sdn);
    int uniquemember_count = 0;
    int member_count = 0;
    int nentries = 0;
    int count = 0;
    int res;
    int i;

    if (binddn == NULL) {
        return;
    }

    /*
     *  Check if the DN exists and has "group" objectclasses
     */
    pb = slapi_pblock_new();
    slapi_search_internal_set_pb(pb, binddn, LDAP_SCOPE_BASE,
                                 "(|(objectclass=groupofuniquenames)(objectclass=groupofnames))",
                                 NULL, 0, NULL, NULL, (void *)plugin_get_default_component_id(), 0);
    slapi_search_internal_pb(pb);
    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
    if (res != LDAP_SUCCESS) {
        slapi_free_search_results_internal(pb);
        slapi_pblock_destroy(pb);
        slapi_log_err(SLAPI_LOG_ERR, "pw_get_admin_users", "Search failed for %s: error %d - "
                                                           "Password Policy Administrators can not be set\n",
                      slapi_sdn_get_dn(sdn), res);
        return;
    }
    /*
     *  Ok, we know we have a valid DN, and nentries will tell us if its a group or a user
     */
    slapi_pblock_get(pb, SLAPI_NENTRIES, &nentries);
    if (nentries > 0) {
        /*
         *  It's a group DN, gather all the members
         */
        Slapi_Entry **entries = NULL;

        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        uniquemember_vals = slapi_entry_attr_get_charray_ext(entries[0], "uniquemember", &uniquemember_count);
        member_vals = slapi_entry_attr_get_charray_ext(entries[0], "member", &member_count);
        pwp->pw_admin_user = (Slapi_DN **)slapi_ch_calloc((uniquemember_count + member_count + 1),
                                                          sizeof(Slapi_DN *));
        if (uniquemember_count > 0) {
            for (i = 0; i < uniquemember_count; i++) {
                pwp->pw_admin_user[count++] = slapi_sdn_new_dn_passin(uniquemember_vals[i]);
            }
        }
        if (member_count > 0) {
            for (i = 0; i < member_count; i++) {
                pwp->pw_admin_user[count++] = slapi_sdn_new_dn_passin(member_vals[i]);
            }
        }
        slapi_ch_free((void **)&uniquemember_vals);
        slapi_ch_free((void **)&member_vals);
    } else {
        /* It's a single user */
        pwp->pw_admin_user = (Slapi_DN **)slapi_ch_calloc(2, sizeof(Slapi_DN *));
        pwp->pw_admin_user[0] = slapi_sdn_dup(sdn);
    }
    slapi_free_search_results_internal(pb);
    slapi_pblock_destroy(pb);
}

/* This function creates a passwdPolicy structure, loads it from either
 * slapdFrontendconfig or the entry pointed by pwdpolicysubentry and
 * returns the structure.
 */
passwdPolicy *
new_passwdPolicy(Slapi_PBlock *pb, const char *dn)
{
    slapdFrontendConfig_t *slapdFrontendConfig = NULL;
    Slapi_ValueSet *values = NULL;
    Slapi_Value **sval = NULL;
    Slapi_Entry *e = NULL, *pw_entry = NULL;
    passwdPolicy *pwdpolicy = NULL;
    Slapi_Attr *attr = NULL;
    char *pwscheme_name = NULL;
    char *attr_name = NULL;
    char *actual_type_name = NULL;
    int type_name_disposition = 0;
    int attr_free_flags = 0;
    int rc = 0;
    int optype = -1;
    int free_e = 1; /* reset if e is taken from pb */
    if (pb) {
        slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &optype);
    }

    /* If we already allocated a pw policy, return it */
    if (pb != NULL) {
        passwdPolicy *existing_pwdpolicy = slapi_pblock_get_pwdpolicy(pb);
        if (existing_pwdpolicy != NULL) {
            return existing_pwdpolicy;
        }
    }

    if (g_get_active_threadcnt() == 0) {
        /*
         * If the server is starting up the thread count will be zero, so
         * we should not proceed, because not all the backends have been
         * initialized yet.
         */
        return NULL;
    }

    if (pb) {
        slapi_pblock_get(pb, SLAPI_OPERATION_TYPE, &optype);
    }

    slapdFrontendConfig = getFrontendConfig();
    pwdpolicy = (passwdPolicy *)slapi_ch_calloc(1, sizeof(passwdPolicy));

    if (dn && (slapdFrontendConfig->pwpolicy_local == 1)) {
        /*  If we're doing an add, COS does not apply yet so we check
            parents for the pwdpolicysubentry.  We look only for virtual
            attributes, because real ones are for single-target policy. */
        /* RFE - is there a way to make this work for non-existent entries
         * when we don't pass in pb?  We'll need to do this if we add support
         * for password policy plug-ins. */
        if (optype == SLAPI_OPERATION_ADD) {
            char *parentdn = slapi_ch_strdup(dn);
            char *nextdn = NULL;
            while ((nextdn = slapi_dn_parent(parentdn)) != NULL) {
                if (((e = get_entry(pb, nextdn)) != NULL)) {
                    if ((slapi_vattr_values_get(e, "pwdpolicysubentry",
                                                &values, &type_name_disposition, &actual_type_name,
                                                SLAPI_VIRTUALATTRS_REQUEST_POINTERS |
                                                    SLAPI_VIRTUALATTRS_ONLY,
                                                &attr_free_flags)) == 0) {
                        /* pwdpolicysubentry found! */
                        break;
                    } else {
                        /* Parent didn't have it, check grandparent... */
                        slapi_ch_free_string(&parentdn);
                        parentdn = nextdn;
                        slapi_entry_free(e);
                        e = NULL;
                    }
                } else {
                    /* Reached the top without finding a pwdpolicysubentry. */
                    break;
                }
            }

            slapi_ch_free_string(&parentdn);
            slapi_ch_free_string(&nextdn);

            /*  If we're not doing an add, we look for the pwdpolicysubentry
            attribute in the target entry itself. */
        } else {
            if (optype == SLAPI_OPERATION_SEARCH) {
                Slapi_Entry *pb_e;

                /* During a search the entry should be in the pblock
                 * For safety check entry DN is identical to 'dn'
                 */
                slapi_pblock_get(pb, SLAPI_SEARCH_RESULT_ENTRY, &pb_e);
                if (pb_e) {
                    Slapi_DN * sdn;
                    const char *ndn;
                    char *pb_ndn;

                    pb_ndn = slapi_entry_get_ndn(pb_e);

                    sdn = slapi_sdn_new_dn_byval(dn);
                    ndn = slapi_sdn_get_ndn(sdn);

                    if (strcasecmp(pb_ndn, ndn) == 0) {
                        /* We are using the candidate entry that is already loaded in the pblock
                         * Do not trigger an additional internal search
                         * Also we will not need to free the entry that will remain in the pblock
                         */
                        e = pb_e;
                        free_e = 0;
                    } else {
                        e = get_entry(pb, dn);
                    }
                    slapi_sdn_free(&sdn);
                } else {
                    e = get_entry(pb, dn);
                }
            } else {
                /* For others operations but SEARCH */
                e = get_entry(pb, dn);
            }

            if (e) {
                Slapi_Attr *e_attr = NULL;
                rc = slapi_entry_attr_find(e, "pwdpolicysubentry", &e_attr);
                if (e_attr && (0 == rc)) {
                    /* If the entry has pwdpolicysubentry, use the PwPolicy. */
                    values = valueset_dup(&e_attr->a_present_values);
                } else {
                    /* Otherwise, retrieve the policy from CoS Cache */
                    rc = slapi_vattr_values_get(e, "pwdpolicysubentry", &values,
                                                &type_name_disposition, &actual_type_name,
                                                SLAPI_VIRTUALATTRS_REQUEST_POINTERS, &attr_free_flags);
                }
                if (rc) {
                    values = NULL;
                }
            }
        }

        if (values != NULL) {
            Slapi_Value *v = NULL;
            const struct berval *bvp = NULL;

            if ((slapi_valueset_first_value(values, &v) != -1) &&
                (bvp = slapi_value_get_berval(v)) != NULL) {
                if (bvp != NULL) {
                    /* we got the pwdpolicysubentry value */
                    pw_entry = get_entry(pb, bvp->bv_val);
                }
            }
            slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);
            if (free_e) {
                slapi_entry_free(e);
            }

            if (pw_entry == NULL) {
                slapi_log_err(SLAPI_LOG_ERR, "new_passwdPolicy",
                              "Loading global password policy for %s"
                              " --local policy entry not found\n",
                              dn);
                goto done;
            }

            /* policy is local, store the DN of the policy for logging */
            pwdpolicy->pw_local_dn = slapi_ch_strdup(slapi_entry_get_dn(pw_entry));

            /* Set the default values (from libglobs.c) */
            pwpolicy_init_defaults(pwdpolicy);

            /* Set the current storage scheme */
            pwscheme_name = config_get_pw_storagescheme();
            pwdpolicy->pw_storagescheme = pw_name2scheme(pwscheme_name);
            slapi_ch_free_string(&pwscheme_name);

            /* Set the defined values now */
            for (slapi_entry_first_attr(pw_entry, &attr); attr;
                 slapi_entry_next_attr(pw_entry, attr, &attr)) {
                slapi_attr_get_type(attr, &attr_name);
                if (!strcasecmp(attr_name, "passwordminage")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_minage = slapi_value_get_time_time_t(*sval);
                        if (-1 == pwdpolicy->pw_minage) {
                            slapi_log_err(SLAPI_LOG_ERR,
                                          "new_passwdPolicy", "%s - Invalid passwordMinAge: %s\n",
                                          slapi_entry_get_dn_const(pw_entry),
                                          slapi_value_get_string(*sval));
                        }
                    }
                } else if (!strcasecmp(attr_name, "passwordmaxage")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_maxage = slapi_value_get_time_time_t(*sval);
                        if (-1 == pwdpolicy->pw_maxage) {
                            slapi_log_err(SLAPI_LOG_ERR,
                                          "new_passwdPolicy", "%s - Invalid passwordMaxAge: %s\n",
                                          slapi_entry_get_dn_const(pw_entry),
                                          slapi_value_get_string(*sval));
                        }
                    }
                } else if (!strcasecmp(attr_name, "passwordwarning")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_warning = slapi_value_get_time_time_t(*sval);
                        if (-1 == pwdpolicy->pw_warning) {
                            slapi_log_err(SLAPI_LOG_ERR,
                                          "new_passwdPolicy", "%s - Invalid passwordWarning: %s\n",
                                          slapi_entry_get_dn_const(pw_entry),
                                          slapi_value_get_string(*sval));
                        }
                    }
                } else if (!strcasecmp(attr_name, "passwordsendexpiringtime")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_send_expiring =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordhistory")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_history =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordinhistory")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_inhistory = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordlockout")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_lockout =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordmaxfailure")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_maxfailure = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordunlock")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_unlock =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordlockoutduration")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_lockduration = slapi_value_get_time_time_t(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordresetfailurecount")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_resetfailurecount = slapi_value_get_long(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordchange")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_change =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordmustchange")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_must_change =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordchecksyntax")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_syntax =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordminlength")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_minlength = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordmindigits")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_mindigits = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordminalphas")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_minalphas = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordminuppers")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_minuppers = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordminlowers")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_minlowers = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordminspecials")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_minspecials = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordmin8bit")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_min8bit = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordmaxrepeats")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_maxrepeats = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordmincategories")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_mincategories = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordmintokenlength")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_mintokenlength = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordexp")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_exp =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordgracelimit")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_gracelimit = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordstoragescheme")) {
                    if ((sval = attr_get_present_values(attr))) {
                        free_pw_scheme(pwdpolicy->pw_storagescheme);
                        pwdpolicy->pw_storagescheme =
                            pw_name2scheme((char *)slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordLegacyPolicy")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_is_legacy =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordTrackUpdateTime")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_track_update_time =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordAdminDN")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_admin = slapi_sdn_new_dn_byval(slapi_value_get_string(*sval));
                        pw_get_admin_users(pwdpolicy);
                    }
                } else if (!strcasecmp(attr_name, "passwordAdminSkipInfoUpdate")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_admin_skip_info =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordPalindrome")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_palindrome =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordDictCheck")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_check_dict =
                            pw_boolean_str2value(slapi_value_get_string(*sval));
                    }
                } else if (!strcasecmp(attr_name, "passwordUserAttributes")) {
                    if ((sval = attr_get_present_values(attr))) {
                        char *attrs = slapi_ch_strdup(slapi_value_get_string(*sval));
                        /* we need a separate string because it gets corrupted after slapi_str2charray_ext */
                        char *tmp_array_attrs = slapi_ch_strdup(attrs);

                        /* we should accept comma-separated lists but slapi_str2charray_ext will process only space-separated */
                        replace_char(tmp_array_attrs, ',', ' ');

                        pwdpolicy->pw_cmp_attrs = attrs;
                        /* Take list of attributes and break it up into a char array */
                        pwdpolicy->pw_cmp_attrs_array = slapi_str2charray_ext(tmp_array_attrs, " ", 0);
                        slapi_ch_free_string(&tmp_array_attrs);
                    }
                }  else if (!strcasecmp(attr_name, "passwordBadWords")) {
                    if ((sval = attr_get_present_values(attr))) {
                        char *words = slapi_ch_strdup(slapi_value_get_string(*sval));
                        /* we need a separate string because it gets corrupted after slapi_str2charray_ext */
                        char *tmp_array_words = slapi_ch_strdup(words);

                        /* we should accept comma-separated lists but slapi_str2charray_ext will process only space-separated */
                        replace_char(tmp_array_words, ',', ' ');

                        pwdpolicy->pw_bad_words = words;
                        /* Take list of attributes and break it up into a char array */
                        pwdpolicy->pw_bad_words_array = slapi_str2charray_ext(tmp_array_words, " ", 0);

                        slapi_ch_free_string(&tmp_array_words);
                    }
                } else if (!strcasecmp(attr_name, "passwordMaxSequence")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_max_seq = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordMaxSeqSets")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_seq_char_sets = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordMaxClassChars")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_max_class_repeats = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, "passwordDictPath")) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_dict_path = (char *)slapi_value_get_string(*sval);
                    }
                } else if (!strcasecmp(attr_name, CONFIG_PW_TPR_MAXUSE)) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_tpr_maxuse = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, CONFIG_PW_TPR_DELAY_EXPIRE_AT)) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_tpr_delay_expire_at = slapi_value_get_int(*sval);
                    }
                } else if (!strcasecmp(attr_name, CONFIG_PW_TPR_DELAY_VALID_FROM)) {
                    if ((sval = attr_get_present_values(attr))) {
                        pwdpolicy->pw_tpr_delay_valid_from = slapi_value_get_int(*sval);
                    }
                }
            } /* end of for() loop */
            if (pw_entry) {
                slapi_entry_free(pw_entry);
            }
            if (LDAP_ON != pwdpolicy->pw_syntax) {
                passwdPolicy *g_pwdpolicy = &(slapdFrontendConfig->pw_policy);
                /*
                 * When the fine-grained password policy does not set the
                 * password syntax, get the syntax from the global
                 * policy if nsslapd-pwpolicy-inherit-global is on.
                 */
                if ((LDAP_ON == g_pwdpolicy->pw_syntax) && config_get_pwpolicy_inherit_global()) {
                    pwdpolicy->pw_minlength = g_pwdpolicy->pw_minlength;
                    pwdpolicy->pw_mindigits = g_pwdpolicy->pw_mindigits;
                    pwdpolicy->pw_minalphas = g_pwdpolicy->pw_minalphas;
                    pwdpolicy->pw_minuppers = g_pwdpolicy->pw_minuppers;
                    pwdpolicy->pw_minlowers = g_pwdpolicy->pw_minlowers;
                    pwdpolicy->pw_minspecials = g_pwdpolicy->pw_minspecials;
                    pwdpolicy->pw_min8bit = g_pwdpolicy->pw_min8bit;
                    pwdpolicy->pw_maxrepeats = g_pwdpolicy->pw_maxrepeats;
                    pwdpolicy->pw_mincategories = g_pwdpolicy->pw_mincategories;
                    pwdpolicy->pw_mintokenlength = g_pwdpolicy->pw_mintokenlength;
                    pwdpolicy->pw_syntax = LDAP_ON; /* Need to enable it to apply the default values */
                }
            }
            if (pb) {
                slapi_pblock_set_pwdpolicy(pb, pwdpolicy);
            }
            return pwdpolicy;
        } else if (free_e) {
            slapi_entry_free(e);
        }
    }

done:
    /*
     * If we are here, that means we need to load the passwdPolicy
     * structure from slapdFrontendconfig
     */
    *pwdpolicy = slapdFrontendConfig->pw_policy;
    pwscheme_name = config_get_pw_storagescheme();
    pwdpolicy->pw_storagescheme = pw_name2scheme(pwscheme_name);
    slapi_ch_free_string(&pwscheme_name);
    pwdpolicy->pw_admin = slapi_sdn_dup(slapdFrontendConfig->pw_policy.pw_admin);
    pw_get_admin_users(pwdpolicy);
    if (pb) {
        slapi_pblock_set_pwdpolicy(pb, pwdpolicy);
    }

    return pwdpolicy;

} /* End of new_passwdPolicy() */

void
delete_passwdPolicy(passwdPolicy **pwpolicy)
{
    if (pwpolicy && *pwpolicy) {
        free_pw_scheme((*(*pwpolicy)).pw_storagescheme);
        slapi_sdn_free(&(*(*pwpolicy)).pw_admin);
        if ((*(*pwpolicy)).pw_admin_user) {
            int i = 0;
            while ((*(*pwpolicy)).pw_admin_user[i]) {
                slapi_sdn_free(&(*(*pwpolicy)).pw_admin_user[i]);
                i++;
            }
            slapi_ch_free((void **)&(*(*pwpolicy)).pw_admin_user);
        }
        slapi_ch_free_string(&(*(*pwpolicy)).pw_local_dn);
        slapi_ch_free((void **)pwpolicy);
    }
}

/*
 * Encode the PWPOLICY RESPONSE control.
 *
 * Create a password policy response control,
 * and add it to the PBlock to be returned to the client.
 *
 * Returns:
 *   success ( 0 )
 *   operationsError (1),
 */
int
slapi_pwpolicy_make_response_control(Slapi_PBlock *pb, int seconds, int logins, ber_int_t error)
{
    BerElement *ber = NULL;
    struct berval *bvp = NULL;
    int rc = -1;

    /*
    PasswordPolicyResponseValue ::= SEQUENCE {
        warning   [0] CHOICE OPTIONAL {
            timeBeforeExpiration  [0] INTEGER (0 .. maxInt),
            graceLoginsRemaining  [1] INTEGER (0 .. maxInt) }
        error     [1] ENUMERATED OPTIONAL {
            passwordExpired       (0),
            accountLocked         (1),
            changeAfterReset      (2),
            passwordModNotAllowed (3),
            mustSupplyOldPassword (4),
            invalidPasswordSyntax (5),
            passwordTooShort      (6),
            passwordTooYoung      (7),
            passwordInHistory     (8) } }
    */

    slapi_log_err(SLAPI_LOG_TRACE, "slapi_pwpolicy_make_response_control", "=>\n");
    if ((ber = ber_alloc()) == NULL) {
        return rc;
    }

    rc = ber_printf(ber, "{");
    if (rc != -1) {
        if (seconds >= 0 || logins >= 0) {
            if (seconds >= 0) {
                rc = ber_printf(ber, "t{ti}", LDAP_TAG_PWP_WARNING,
                                LDAP_TAG_PWP_SECSLEFT,
                                seconds);
            } else {
                rc = ber_printf(ber, "t{ti}", LDAP_TAG_PWP_WARNING,
                                LDAP_TAG_PWP_GRCLOGINS,
                                logins);
            }
        }
        if (rc != -1) {
            if (error >= 0) {
                rc = ber_printf(ber, "te", LDAP_TAG_PWP_ERROR, error);
            }
            if (rc != -1) {
                rc = ber_printf(ber, "}");
                if (rc != -1) {
                    rc = ber_flatten(ber, &bvp);
                }
            }
        }
    }

    ber_free(ber, 1);

    if (rc != -1) {
        LDAPControl new_ctrl = {0};
        new_ctrl.ldctl_oid = LDAP_X_CONTROL_PWPOLICY_RESPONSE;
        new_ctrl.ldctl_value = *bvp;
        new_ctrl.ldctl_iscritical = 0;
        rc = slapi_pblock_set(pb, SLAPI_ADD_RESCONTROL, &new_ctrl);
        ber_bvfree(bvp);
    }

    slapi_log_err(SLAPI_LOG_TRACE, "slapi_pwpolicy_make_response_control", "<= (%d)\n", rc);

    return (rc == -1 ? LDAP_OPERATIONS_ERROR : LDAP_SUCCESS);
}

static int
pw_boolean_str2value(const char *str)
{
    if (!strcasecmp(str, "true") ||
        !strcasecmp(str, "on") ||
        !strcasecmp(str, "1")) {
        return (LDAP_ON);
    }

    if (!strcasecmp(str, "false") ||
        !strcasecmp(str, "off") ||
        !strcasecmp(str, "0")) {
        return (LDAP_OFF);
    }

    return (-1);
}

int
check_pw_duration_value(const char *attr_name, char *value, long minval, long maxval, char *errorbuf, size_t ebuflen)
{
    int retVal = LDAP_SUCCESS;
    time_t age;

    age = slapi_parse_duration(value);
    if (-1 == age) {
        slapi_create_errormsg(errorbuf, ebuflen, "password minimum age \"%s\" is invalid. ", value);
        retVal = LDAP_CONSTRAINT_VIOLATION;
    } else if (0 == strcasecmp(CONFIG_PW_LOCKDURATION_ATTRIBUTE, attr_name)) {
        if ((age <= 0) ||
            (age > (MAX_ALLOWED_TIME_IN_SECS_64 - slapi_current_utc_time())) ||
            ((-1 != minval) && (age < minval)) ||
            ((-1 != maxval) && (age > maxval))) {
            slapi_create_errormsg(errorbuf, ebuflen, "%s: \"%s\" seconds is invalid. ", attr_name, value);
            retVal = LDAP_CONSTRAINT_VIOLATION;
        }
    } else {
        if ((age < 0) ||
            (age > (MAX_ALLOWED_TIME_IN_SECS_64 - slapi_current_utc_time())) ||
            ((-1 != minval) && (age < minval)) ||
            ((-1 != maxval) && (age > maxval))) {
            slapi_create_errormsg(errorbuf, ebuflen, "%s: \"%s\" seconds is invalid. ", attr_name, value);
            retVal = LDAP_CONSTRAINT_VIOLATION;
        }
    }

    return retVal;
}

int
check_pw_resetfailurecount_value(const char *attr_name __attribute__((unused)), char *value, long minval __attribute__((unused)), long maxval __attribute__((unused)), char *errorbuf, size_t ebuflen)
{
    int retVal = LDAP_SUCCESS;
    time_t duration = 0; /* in seconds */

    /* in seconds */
    duration = parse_duration_time_t(value);
    if (duration < 0 || duration > (MAX_ALLOWED_TIME_IN_SECS_64 - slapi_current_utc_time())) {
        slapi_create_errormsg(errorbuf, ebuflen, "password reset count duration \"%s\" seconds is invalid.", value);
        retVal = LDAP_CONSTRAINT_VIOLATION;
    }

    return retVal;
}

int
check_pw_storagescheme_value(const char *attr_name __attribute__((unused)), char *value, long minval __attribute__((unused)), long maxval __attribute__((unused)), char *errorbuf, size_t ebuflen)
{
    int retVal = LDAP_SUCCESS;
    struct pw_scheme *new_scheme = NULL;
    char *scheme_list = NULL;

    scheme_list = plugin_get_pwd_storage_scheme_list(PLUGIN_LIST_PWD_STORAGE_SCHEME);
    new_scheme = pw_name2scheme(value);
    if (new_scheme == NULL) {
        if (scheme_list != NULL) {
            slapi_create_errormsg(errorbuf, ebuflen, "%s: invalid scheme - %s. Valid schemes are: %s",
                                  CONFIG_PW_STORAGESCHEME_ATTRIBUTE, value, scheme_list);
        } else {
            slapi_create_errormsg(errorbuf, ebuflen, "%s: invalid scheme - %s (no pwdstorage scheme plugin loaded)",
                                  CONFIG_PW_STORAGESCHEME_ATTRIBUTE, value);
        }
        retVal = LDAP_CONSTRAINT_VIOLATION;
    } else if (new_scheme->pws_enc == NULL) {
        /* For example: the NS-MTA-MD5 password scheme is for comparision only
        and for backward compatibility with an Old Messaging Server that was
        setting passwords in the directory already encrypted. The scheme cannot
        and won't encrypt passwords if they are in clear. We don't take it
        */

        if (scheme_list) {
            slapi_create_errormsg(errorbuf, ebuflen, "%s: invalid encoding scheme - %s\nValid values are: %s\n",
                                  CONFIG_PW_STORAGESCHEME_ATTRIBUTE, value, scheme_list);
        }

        retVal = LDAP_CONSTRAINT_VIOLATION;
    }

    free_pw_scheme(new_scheme);
    slapi_ch_free_string(&scheme_list);

    return retVal;
}
/* Before bind operation, check if the bind_target_entry has not overpass TPR limits
 * returns:
 *    0: TPR limits not enforced or reached
 *    LDAP_CONSTRAINT_VIOLATION: TPR limits reached
 *    LDAP_OPERATIONS_ERROR : internal failure
 */
int
slapi_check_tpr_limits(Slapi_PBlock *pb, Slapi_Entry *bind_target_entry, int send_result) {
    passwdPolicy *pwpolicy = NULL;
    char *dn = NULL;
    char *value;
    time_t cur_time;
    char *cur_time_str = NULL;

    if (bind_target_entry == NULL) {
        return 0;
    }

    dn = slapi_entry_get_ndn(bind_target_entry);
    pwpolicy = new_passwdPolicy(pb, dn);
    if (pwpolicy == NULL) {
        /* TPR limits are part of password policy => no limit */
        return 0;
    }

    if (!slapi_entry_attr_hasvalue(bind_target_entry, "pwdTPRReset", "TRUE")) {
        /* the password was not reset by an admin while a TRP pwp was set, just returned */
        /* covscan false positive: new_passwdPolicy anchor the policy in the pblock */
        /* coverity[leaked_storage] */
        return 0;
    }

    /* Check entry TPR max use */
    if (pwpolicy->pw_tpr_maxuse >= 0) {
        uint use_count;
        value = (char *) slapi_entry_attr_get_ref(bind_target_entry, "pwdTPRUseCount");
        if (value) {
            /* max Use is enforced */
            use_count = strtoull(value, 0, 0);
            use_count++;
            update_tpr_pw_usecount(pb, bind_target_entry, (int32_t) use_count);
            if (use_count > pwpolicy->pw_tpr_maxuse) {
                slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                              "slapi_check_tpr_limits - "
                              "number of bind (%u) is larger than TPR password max use (%d): Entry (%s) Policy (%s)\n",
                              use_count, pwpolicy->pw_tpr_maxuse,
                              dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
                if (send_result) {
                    send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL,
                                     "TPR checking. Contact system administrator", 0, NULL);
                }
                return LDAP_CONSTRAINT_VIOLATION;
            }
        } else {
            /* The password was reset at a time that the password policy
             * did not enforced the max use. That is fine, just log an info.
             */
            slapi_log_err(SLAPI_LOG_INFO,
                        "slapi_check_tpr_limits",
                        "TPR password max use (%d) was not enforced when the password was reset (%s)\n",
                        pwpolicy->pw_tpr_maxuse, dn);
        }
    }
    /* If we are enforcing */
    if ((pwpolicy->pw_tpr_delay_expire_at >= 0) || (pwpolicy->pw_tpr_delay_valid_from)) {
        cur_time = slapi_current_utc_time();
        cur_time_str = format_genTime(cur_time);
    }

    /* Check entry TPR expiration at a specific time */
    if (pwpolicy->pw_tpr_delay_expire_at >= 0) {
        value = (char *) slapi_entry_attr_get_ref(bind_target_entry, "pwdTPRExpireAt");
        if (value) {
            /* max Use is enforced */
            if (difftime(parse_genTime(cur_time_str), parse_genTime(value)) >= 0) {
                slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                              "slapi_check_tpr_limits - "
                              "attempt to bind with an expired TPR password (current=%s, expiration=%s): Entry (%s) Policy (%s)\n",
                              cur_time_str, value,
                              dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
                if (send_result) {
                    send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL,
                                     "TPR checking. Contact system administrator", 0, NULL);
                }
                slapi_ch_free((void **)&cur_time_str);
                return LDAP_CONSTRAINT_VIOLATION;
            }
        } else {
            /* The password was reset at a time that the password policy
             * did not enforced an expiration delay. That is fine, just log an info.
             */
            slapi_log_err(SLAPI_LOG_INFO,
                        "slapi_check_tpr_limits",
                        "TPR password expiration after %d seconds, was not enforced when the password was reset (%s)\n",
                        pwpolicy->pw_tpr_delay_expire_at, dn);
        }
    }

    /* Check entry TPR valid after a specific time */
    if (pwpolicy->pw_tpr_delay_valid_from >= 0) {
        value = (char *) slapi_entry_attr_get_ref(bind_target_entry, "pwdTPRValidFrom");
        if (value) {
            /* validity after a specific time is enforced */
            if (difftime(parse_genTime(value), parse_genTime(cur_time_str)) >= 0) {
                slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                              "slapi_check_tpr_limits - "
                              "attempt to bind with TPR password not yet valid (current=%s, validity=%s): Entry (%s) Policy (%s)\n",
                              cur_time_str, value,
                              dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
                if (send_result) {
                    send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL,
                                     "TPR checking. Contact system administrator", 0, NULL);
                }
                slapi_ch_free((void **)&cur_time_str);
                return LDAP_CONSTRAINT_VIOLATION;
            }
        } else {
            /* The password was reset at a time that the password policy
             * did not enforced validity delay. That is fine, just log an info.
             */
            slapi_log_err(SLAPI_LOG_INFO,
                        "slapi_check_tpr_limits",
                        "TPR password validity (after %d seconds) was not enforced when the password was reset (%s)\n",
                        pwpolicy->pw_tpr_delay_valid_from, dn);
        }
    }
    slapi_ch_free((void **)&cur_time_str);
    return 0;
}

/* check_account_lock is called before bind opeation; this could be a pre-op. */
int
slapi_check_account_lock(Slapi_PBlock *pb, Slapi_Entry *bind_target_entry, int pwresponse_req, int check_password_policy, int send_result)
{

    time_t unlock_time;
    time_t cur_time;
    char *cur_time_str = NULL;
    char *accountUnlockTime;
    passwdPolicy *pwpolicy = NULL;
    char *dn = NULL;

    /* kexcoff: account inactivation */
    int rc = 0;
    Slapi_ValueSet *values = NULL;
    int type_name_disposition = 0;
    char *actual_type_name = NULL;
    int attr_free_flags = 0;
    /* kexcoff - end */

    if (bind_target_entry == NULL)
        return -1;

    if (check_password_policy) {
        dn = slapi_entry_get_ndn(bind_target_entry);
        pwpolicy = new_passwdPolicy(pb, dn);
    }

    /* kexcoff: account inactivation */
    /* check if the entry is locked by nsAccountLock attribute - account inactivation feature */

    rc = slapi_vattr_values_get(bind_target_entry, "nsAccountLock",
                                &values,
                                &type_name_disposition, &actual_type_name,
                                SLAPI_VIRTUALATTRS_REQUEST_POINTERS,
                                &attr_free_flags);
    if (rc == 0 && NULL != values) {
        Slapi_Value *v = NULL;
        const struct berval *bvp = NULL;

        if ((slapi_valueset_first_value(values, &v) != -1) &&
            (bvp = slapi_value_get_berval(v)) != NULL) {
            if ((bvp != NULL) && (strcasecmp(bvp->bv_val, "true") == 0)) {
                /* account inactivated */
                if (check_password_policy && pwresponse_req) {
                    slapi_pwpolicy_make_response_control(pb, -1, -1,
                                                         LDAP_PWPOLICY_ACCTLOCKED);
                }
                if (send_result)
                    send_ldap_result(pb, LDAP_UNWILLING_TO_PERFORM, NULL,
                                     "Account inactivated. Contact system administrator.",
                                     0, NULL);
                slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);
                goto locked;
            }
        } /* else, account "activated", keep on the process */
    }

    if (values != NULL)
        slapi_vattr_values_free(&values, &actual_type_name, attr_free_flags);

    /* kexcoff - end */

    /*
     * Check if the password policy has to be checked or not
     */
    if (!check_password_policy || !pwpolicy || pwpolicy->pw_lockout == 0) {
        goto notlocked;
    }

    /*
     * Check the attribute of the password policy
     */

    /* check if account is locked out.  If so, send result and return 1 */
    {
        unsigned int maxfailure = pwpolicy->pw_maxfailure;
        /* It's locked if passwordRetryCount >= maxfailure */
        if (slapi_entry_attr_get_uint(bind_target_entry, "passwordRetryCount") < maxfailure) {
            /* Not locked */
            goto notlocked;
        }
    }

    /* locked but maybe it's time to unlock it */
    accountUnlockTime = (char *)slapi_entry_attr_get_ref(bind_target_entry, "accountUnlockTime");
    if (accountUnlockTime != NULL) {
        unlock_time = parse_genTime(accountUnlockTime);

        if (pwpolicy->pw_unlock == 0 &&
            unlock_time == NO_TIME) {

            /* account is locked forever. contact admin to reset */
            if (pwresponse_req) {
                slapi_pwpolicy_make_response_control(pb, -1, -1,
                                                     LDAP_PWPOLICY_ACCTLOCKED);
            }
            if (send_result) {
                send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL,
                                 "Exceed password retry limit. Contact system administrator to reset.",
                                 0, NULL);
                slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                              "Account is locked and requires administrator reset.  Entry (%s) Policy (%s)\n",
                              dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
            }
            goto locked;
        }
        cur_time = slapi_current_utc_time();
        cur_time_str = format_genTime(cur_time);
        if (difftime(parse_genTime(cur_time_str), unlock_time) < 0) {

            /* account is locked, cannot do anything */
            if (pwresponse_req) {
                slapi_pwpolicy_make_response_control(pb, -1, -1,
                                                     LDAP_PWPOLICY_ACCTLOCKED);
            }
            if (send_result) {
                send_ldap_result(pb, LDAP_CONSTRAINT_VIOLATION, NULL,
                                 "Exceed password retry limit. Please try later.",
                                 0, NULL);
                slapi_log_err(SLAPI_LOG_PWDPOLICY, PWDPOLICY_DEBUG,
                              "Account is locked: Entry (%s) Policy (%s)\n",
                              dn, pwpolicy->pw_local_dn ? pwpolicy->pw_local_dn : "Global");
            }
            slapi_ch_free((void **)&cur_time_str);
            goto locked;
        }
        slapi_ch_free((void **)&cur_time_str);
    }

notlocked:
    /* account is not locked. */
    /* new_passwdPolicy registers the policy in the pblock so there is no leak */
    /* coverity[leaked_storage] */
    return (0);
locked:
    /* new_passwdPolicy registers the policy in the pblock so there is no leak */
    /* coverity[leaked_storage] */
    return (1);
}

/* The idea here is that these functions could allow us to have password
 * policy plugins in the future.  The plugins would register callbacks for these
 * slapi functions that would be used here if any pwpolicy plugin is configured to
 * be used.  Right now, we just use the normal server password policy code since
 * we don't have a pwpolicy plugin type. */
Slapi_PWPolicy *
slapi_get_pwpolicy(Slapi_DN *dn)
{
    return ((Slapi_PWPolicy *)new_passwdPolicy(NULL, slapi_sdn_get_ndn(dn)));
}

void
slapi_pwpolicy_free(Slapi_PWPolicy *pwpolicy)
{
    delete_passwdPolicy((passwdPolicy **)&pwpolicy);
}

int
slapi_pwpolicy_is_expired(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e, time_t *expire_time, int *remaining_grace)
{
    int is_expired = 0;

    if (pwpolicy && e) {
        /* If password expiration is enabled in the policy,
         * check if the password has expired. */
        if (pwpolicy->pw_exp == 1) {
            char *expiration_val = NULL;
            time_t _expire_time;
            char *cur_time_str = NULL;
            time_t cur_time;

            expiration_val = slapi_entry_attr_get_charptr(e, "passwordExpirationTime");
            if (expiration_val) {
                _expire_time = parse_genTime(expiration_val);

                cur_time = slapi_current_utc_time();
                cur_time_str = format_genTime(cur_time);

                if ((_expire_time != NO_TIME) && (_expire_time != NOT_FIRST_TIME) &&
                    (difftime(_expire_time, parse_genTime(cur_time_str) <= 0))) {
                    is_expired = 1;
                }

                if (is_expired) {
                    if (remaining_grace) {
                        /* Fill in the number of remaining grace logins */
                        int grace_attempts = 0;

                        grace_attempts = slapi_entry_attr_get_int(e, "passwordGraceUserTime");
                        if (pwpolicy->pw_gracelimit > grace_attempts) {
                            *remaining_grace = pwpolicy->pw_gracelimit - grace_attempts;
                        } else {
                            *remaining_grace = 0;
                        }
                    }
                } else if (expire_time) {
                    /* Fill in the expiration time */
                    if ((_expire_time != NO_TIME) && (_expire_time != NOT_FIRST_TIME)) {
                        *expire_time = _expire_time;
                    } else {
                        *expire_time = (time_t)0;
                    }
                }

                slapi_ch_free_string(&cur_time_str);
            }
        } else if (expire_time) {
            /* Passwords never expire */
            *expire_time = (time_t)0;
        }
    }

    return is_expired;
}

int
slapi_pwpolicy_is_locked(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e, time_t *unlock_time)
{
    int is_locked = 0;

    if (pwpolicy && e) {
        /* Check if account is locked */
        if (pwpolicy->pw_lockout == 1) {
            /* Despite get_uint, we still compare to an int ... */
            if ((int)slapi_entry_attr_get_uint(e, "passwordRetryCount") >= pwpolicy->pw_maxfailure) {
                is_locked = 1;
            }
        }

        if (is_locked) {
            /* See if it's time for the account to be unlocked */
            char *unlock_time_str = NULL;
            char *cur_time_str = NULL;
            time_t _unlock_time = (time_t)0;
            time_t cur_time;

            unlock_time_str = slapi_entry_attr_get_charptr(e, "accountUnlockTime");
            if (unlock_time_str) {
                _unlock_time = parse_genTime(unlock_time_str);
            }

            if ((pwpolicy->pw_unlock == 0) && (_unlock_time == NO_TIME)) {
                /* Account is locked forever */
                if (unlock_time) {
                    *unlock_time = (time_t)0;
                }
            } else {
                cur_time = slapi_current_utc_time();
                cur_time_str = format_genTime(cur_time);

                if (difftime(parse_genTime(cur_time_str), _unlock_time) < 0) {
                    /* Account is not due to be unlocked yet.
                     * Fill in the unlock time. */
                    if (unlock_time) {
                        *unlock_time = _unlock_time;
                    }
                } else {
                    /* Account is due to be unlocked */
                    is_locked = 0;
                }

                slapi_ch_free_string(&cur_time_str);
            }
        }
    }

    return is_locked;
}

int
slapi_pwpolicy_is_reset(Slapi_PWPolicy *pwpolicy, Slapi_Entry *e)
{
    int is_reset = 0;

    if (pwpolicy && e) {
        /* Check if password was reset and needs to be changed */
        if (pwpolicy->pw_must_change) {
            char *expiration_val = 0;
            time_t expire_time = (time_t)0;

            expiration_val = (char *)slapi_entry_attr_get_ref(e, "passwordExpirationTime");
            if (expiration_val) {
                expire_time = parse_genTime(expiration_val);
                if (expire_time == NO_TIME) {
                    is_reset = 1;
                }
            }
        }
    }

    return is_reset;
}

/*
 * Entry extension for unhashed password
 */
static int pw_entry_objtype = -1;
static int pw_entry_handle = -1;

struct slapi_pw_entry_ext
{
    Slapi_RWLock *pw_entry_lock;   /* necessary? */
    Slapi_Value **pw_entry_values; /* stashed values */
};

/*
 * constructor for the entry object extension.
 */
static void *
pw_entry_constructor(void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    struct slapi_pw_entry_ext *pw_extp = NULL;
    Slapi_RWLock *rwlock;
    if ((rwlock = slapi_new_rwlock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "pw_entry_constructor",
                      "slapi_new_rwlock() failed\n");
        return NULL;
    }
    pw_extp = (struct slapi_pw_entry_ext *)slapi_ch_calloc(1,
                                                           sizeof(struct slapi_pw_entry_ext));
    pw_extp->pw_entry_lock = rwlock;
    return pw_extp;
}

/*
 * destructor for the entry object extension.
 */
static void
pw_entry_destructor(void *extension, void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    struct slapi_pw_entry_ext *pw_extp = (struct slapi_pw_entry_ext *)extension;

    if (NULL == pw_extp) {
        return;
    }

    valuearray_free(&pw_extp->pw_entry_values);

    if (pw_extp->pw_entry_lock) {
        slapi_destroy_rwlock(pw_extp->pw_entry_lock);
    }
    slapi_ch_free((void **)&pw_extp);
}

/* Called once from main */
void
pw_exp_init(void)
{
    if (slapi_register_object_extension(SLAPI_EXTMOD_PWPOLICY,
                                        SLAPI_EXT_ENTRY,
                                        pw_entry_constructor,
                                        pw_entry_destructor,
                                        &pw_entry_objtype,
                                        &pw_entry_handle) != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "pw_exp_init",
                      "slapi_register_object_extension failed; "
                      "unhashed password is not able to access\n");
    }
}

/*
 * The output value vals is not a copy.
 * Caller must duplicate it to use it for other than referring.
 */
int
slapi_pw_get_entry_ext(Slapi_Entry *entry, Slapi_Value ***vals)
{
    struct slapi_pw_entry_ext *extp = NULL;

    if (NULL == vals) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_pw_get_entry_ext",
                      "Output param vals is NULL.\n");
        return LDAP_PARAM_ERROR;
    }
    *vals = NULL;

    if ((-1 == pw_entry_objtype) || (-1 == pw_entry_handle)) {
        slapi_log_err(SLAPI_LOG_TRACE, "slapi_pw_get_entry_ext",
                      "pw_entry_extension is not registered\n");
        return LDAP_OPERATIONS_ERROR;
    }

    extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
        pw_entry_objtype,
        entry,
        pw_entry_handle);
    if ((NULL == extp) || (NULL == extp->pw_entry_values)) {
        slapi_log_err(SLAPI_LOG_TRACE, "slapi_pw_get_entry_ext",
                      "pw_entry_extension is not set\n");
        return LDAP_NO_SUCH_ATTRIBUTE;
    }

    slapi_rwlock_rdlock(extp->pw_entry_lock);
    *vals = extp->pw_entry_values;
    slapi_rwlock_unlock(extp->pw_entry_lock);
    return LDAP_SUCCESS;
}

/* If vals is NULL, the stored extension is freed.  */
/* If slapi_pw_set_entry_ext is successful, vals are consumed. */
int
slapi_pw_set_entry_ext(Slapi_Entry *entry, Slapi_Value **vals, int flags)
{
    struct slapi_pw_entry_ext *extp = NULL;

    if ((-1 == pw_entry_objtype) || (-1 == pw_entry_handle)) {
        slapi_log_err(SLAPI_LOG_TRACE, "slapi_pw_set_entry_ext",
                      "pw_entry_extension is not registered\n");
        return LDAP_OPERATIONS_ERROR;
    }

    extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
        pw_entry_objtype,
        entry,
        pw_entry_handle);
    if (NULL == extp) {
        slapi_log_err(SLAPI_LOG_TRACE, "slapi_pw_set_entry_ext",
                      "pw_entry_extension is not set\n");
        return LDAP_NO_SUCH_ATTRIBUTE;
    }

    slapi_rwlock_wrlock(extp->pw_entry_lock);
    if (NULL == vals) {                          /* Set NULL; used for delete. */
        valuearray_free(&extp->pw_entry_values); /* Null is taken care */
    } else {
        if (SLAPI_EXT_SET_REPLACE == flags) {
            valuearray_free(&extp->pw_entry_values); /* Null is taken care */
        }
        /* Each (Slapi_Value *) in vals is passed in. */
        valuearray_add_valuearray(&extp->pw_entry_values, vals,
                                  SLAPI_VALUE_FLAG_PASSIN);
        /* To keep the word "consumed", free vals part, as well. */
        slapi_ch_free((void **)&vals);
    }
    slapi_rwlock_unlock(extp->pw_entry_lock);
    return LDAP_SUCCESS;
}

int
pw_copy_entry_ext(Slapi_Entry *src_e, Slapi_Entry *dest_e)
{
    struct slapi_pw_entry_ext *src_extp = NULL;
    struct slapi_pw_entry_ext *dest_extp = NULL;

    if ((-1 == pw_entry_objtype) || (-1 == pw_entry_handle)) {
        return LDAP_OPERATIONS_ERROR;
    }

    src_extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
        pw_entry_objtype,
        src_e,
        pw_entry_handle);
    if (NULL == src_extp) {
        return LDAP_NO_SUCH_ATTRIBUTE;
    }

    slapi_rwlock_rdlock(src_extp->pw_entry_lock);
    dest_extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
        pw_entry_objtype,
        dest_e,
        pw_entry_handle);
    if (NULL == dest_extp) {
        slapi_rwlock_unlock(src_extp->pw_entry_lock);
        return LDAP_NO_SUCH_ATTRIBUTE;
    }

    slapi_rwlock_wrlock(dest_extp->pw_entry_lock);
    valuearray_add_valuearray(&dest_extp->pw_entry_values,
                              src_extp->pw_entry_values, 0);
    slapi_rwlock_unlock(dest_extp->pw_entry_lock);
    slapi_rwlock_unlock(src_extp->pw_entry_lock);
    return LDAP_SUCCESS;
}

/*
 * The returned string is a copy.
 * Caller must free it.
 */
char *
slapi_get_first_clear_text_pw(Slapi_Entry *entry)
{
    struct slapi_pw_entry_ext *extp = NULL;
    Slapi_Value **pwvals = NULL;
    const char *password_str = NULL;

    if ((-1 == pw_entry_objtype) || (-1 == pw_entry_handle)) {
        slapi_log_err(SLAPI_LOG_TRACE, "slapi_get_first_clear_text_pw",
                      "pw_entry_extension is not registered\n");
        return NULL;
    }

    extp = (struct slapi_pw_entry_ext *)slapi_get_object_extension(
        pw_entry_objtype,
        entry,
        pw_entry_handle);
    if ((NULL == extp) || (NULL == extp->pw_entry_values)) {
        slapi_log_err(SLAPI_LOG_TRACE, "slapi_get_first_clear_text_pw",
                      "pw_entry_extension is not set\n");
        return NULL;
    }

    slapi_rwlock_rdlock(extp->pw_entry_lock);
    pwvals = extp->pw_entry_values;
    if (pwvals) {
        Slapi_ValueSet vset;
        Slapi_Value *value = NULL;
        /* pwvals is passed in to vset; thus no need to free vset. */
        valueset_set_valuearray_passin(&vset, pwvals);
        slapi_valueset_first_value(&vset, &value);
        password_str = slapi_value_get_string(value);
    }
    slapi_rwlock_unlock(extp->pw_entry_lock);
    return slapi_ch_strdup(password_str); /* slapi_ch_strdup(NULL) is okay */
}

void
slapi_free_pw_scheme(struct pw_scheme *pwsp)
{
    free_pw_scheme(pwsp);
}

struct pw_scheme *
slapi_pw_val2scheme(char *val, char **valpwdp, int first_is_default)
{
    return pw_val2scheme(val, valpwdp, first_is_default);
}

int
slapi_pw_cmp(struct pw_scheme *pass_scheme, char *clear_pw, char *encoded_pw)
{
    return ((*(pass_scheme->pws_cmp))(clear_pw, encoded_pw));
}

char *
slapi_pw_get_scheme_name(struct pw_scheme *pass_scheme)
{
    return pass_scheme->pws_name;
}

int
pw_get_ext_size(Slapi_Entry *entry, size_t *size)
{
    Slapi_Value **pw_entry_values;

    if (NULL == size) {
        return LDAP_PARAM_ERROR;
    }
    *size = 0;
    if (NULL == entry->e_extension) {
        return LDAP_SUCCESS;
    }
    *size += sizeof(struct slapi_pw_entry_ext);
    *size += slapi_rwlock_get_size();
    if (LDAP_SUCCESS == slapi_pw_get_entry_ext(entry, &pw_entry_values)) {
        Slapi_Value *cvalue;
        int idx = valuearray_first_value(pw_entry_values, &cvalue);
        while (idx >= 0) {
            *size += value_size(cvalue);
            idx = valuearray_next_value(pw_entry_values, idx, &cvalue);
        }
    }
    return LDAP_SUCCESS;
}

int
add_shadow_ext_password_attrs(Slapi_PBlock *pb, Slapi_Entry **e)
{
    Operation *pb_op;
    const char *dn = NULL;
    passwdPolicy *pwpolicy = NULL;
    long long shadowval = -1;
    Slapi_Mods *smods = NULL;
    LDAPMod **mods;
    long long sval;
    int mod_num = 0;
    char *shmin = NULL;
    int shmin_free_it = 0;
    char *shmax = NULL;
    int shmax_free_it = 0;
    char *shwarn = NULL;
    int shwarn_free_it = 0;
    int rc = 0;

    if (!e || !*e) {
        return rc;
    }
    dn = slapi_entry_get_ndn(*e);
    if (!dn) {
        return rc;
    }
    if (!slapi_entry_attr_hasvalue(*e, SLAPI_ATTR_OBJECTCLASS, "shadowAccount")) {
        /* Not a shadowAccount; nothing to do. */
        return rc;
    }

    slapi_pblock_get(pb, SLAPI_OPERATION, &pb_op);
    if (operation_is_flag_set(pb_op, OP_FLAG_INTERNAL)) {
        /* external only */
        return rc;
    }
    pwpolicy = new_passwdPolicy(pb, dn);
    if (!pwpolicy) {
        return rc;
    }

    slapi_log_err(SLAPI_LOG_TRACE, "add_shadow_ext_password_attrs", "=>\n");

    /* shadowMin - the minimum number of days required between password changes. */
    if (pwpolicy->pw_minage > 0) {
        shadowval = pwpolicy->pw_minage / _SEC_PER_DAY;
        if (shadowval > _MAX_SHADOW) {
            shadowval = _MAX_SHADOW;
        }
    }
    if (shadowval > 0) {
        shmin = (char *)slapi_entry_attr_get_ref(*e, "shadowMin");
        if (shmin) {
            sval = strtoll(shmin, NULL, 0);
            if (sval != shadowval) {
                shmin = slapi_ch_smprintf("%lld", shadowval);
                shmin_free_it = 1;
                mod_num++;
            }
        } else {
            mod_num++;
            shmin = slapi_ch_smprintf("%lld", shadowval);
            shmin_free_it = 1;
        }
    }

    /* shadowMax - the maximum number of days for which the user password remains valid. */
    shadowval = -1;
    if (pwpolicy->pw_exp == 1 && pwpolicy->pw_maxage > 0) {
        shadowval = pwpolicy->pw_maxage / _SEC_PER_DAY;
        if (shadowval > _MAX_SHADOW) {
            shadowval = _MAX_SHADOW;
        }
    }
    if (shadowval > 0) {
        shmax = (char *)slapi_entry_attr_get_ref(*e, "shadowMax");
        if (shmax) {
            sval = strtoll(shmax, NULL, 0);
            if (sval != shadowval) {
                shmax = slapi_ch_smprintf("%lld", shadowval);
                shmax_free_it = 1;
                mod_num++;
            }
        } else {
            mod_num++;
            shmax = slapi_ch_smprintf("%lld", shadowval);
            shmax_free_it = 1;
        }
    }

    /* shadowWarning - the number of days of advance warning given to the user before the user password expires. */
    shadowval = -1;
    if (pwpolicy->pw_exp == 1 && pwpolicy->pw_warning > 0) {
        shadowval = pwpolicy->pw_warning / _SEC_PER_DAY;
        if (shadowval > _MAX_SHADOW) {
            shadowval = _MAX_SHADOW;
        }
    }
    if (shadowval >= 0) {
        shwarn = (char *)slapi_entry_attr_get_ref(*e, "shadowWarningMax");
        if (shwarn) {
            sval = strtoll(shwarn, NULL, 0);
            if (sval != shadowval) {
                shwarn = slapi_ch_smprintf("%lld", shadowval);
                shwarn_free_it = 1;
                mod_num++;
            }
        } else {
            mod_num++;
            shwarn = slapi_ch_smprintf("%lld", shadowval);
            shwarn_free_it = 1;
        }
    }

    smods = slapi_mods_new();
    slapi_mods_init(smods, mod_num);
    if (shmin) {
        slapi_mods_add(smods, LDAP_MOD_REPLACE, "shadowMin", strlen(shmin), shmin);
        if (shmin_free_it)
            slapi_ch_free_string(&shmin);
    }
    if (shmax) {
        slapi_mods_add(smods, LDAP_MOD_REPLACE, "shadowMax", strlen(shmax), shmax);
        if (shmax_free_it)
            slapi_ch_free_string(&shmax);
    }
    if (shwarn) {
        slapi_mods_add(smods, LDAP_MOD_REPLACE, "shadowWarning", strlen(shwarn), shwarn);
        if (shwarn_free_it)
            slapi_ch_free_string(&shwarn);
    }
    /* Apply the  mods to create the resulting entry. */
    mods = slapi_mods_get_ldapmods_byref(smods);
    if (mods) {
        Slapi_Entry *sentry = slapi_entry_dup(*e);
        rc = slapi_entry_apply_mods(sentry, mods);
        slapi_pblock_set_pw_entry(pb, sentry);
        *e = sentry;
    }
    slapi_mods_free(&smods);

    /* These 3 attributes are no need (or not able) to auto-fill.
     *
     * shadowInactive - the number of days of inactivity allowed for the user.
     * Password Policy does not have the corresponding parameter.
     *
     * shadowExpire - the number of days since Jan 1, 1970 after which the
     * account, not the password, will expire.  This is not affected by the
     * Password Policy.
     *
     * shadowFlag - not currently in use.
     */

    slapi_log_err(SLAPI_LOG_TRACE, "add_shadow_ext_password_attrs", "<=\n");
    return rc;
}

/*
 * Re-encode a user's password if a different encoding scheme is configured
 * in the password policy than the password is currently encoded with.
 *
 * Returns:
 *   success ( 0 )
 *   operationsError ( -1 ),
 */
int32_t update_pw_encoding(Slapi_PBlock *orig_pb, Slapi_Entry *e, Slapi_DN *sdn, char *cleartextpassword) {
    char *dn = (char *)slapi_sdn_get_ndn(sdn);
    Slapi_Attr *pw = NULL;
    Slapi_Value **password_values = NULL;
    passwdPolicy *pwpolicy = NULL;
    struct pw_scheme *curpwsp = NULL;
    Slapi_Mods smods;
    char *hashed_val = NULL;
    Slapi_PBlock *pb = NULL;
    int32_t res = 0;

    slapi_mods_init(&smods, 0);

    /*
     * Does the entry have a pw?
     */
    if (e == NULL || slapi_entry_attr_find(e, SLAPI_USERPWD_ATTR, &pw) != 0 || pw == NULL) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "update_pw_encoding", "Could not read password attribute on '%s'\n",
                      dn);
        res = -1;
        goto free_and_return;
    }

    password_values = attr_get_present_values(pw);
    if (password_values == NULL || password_values[0] == NULL) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "update_pw_encoding", "Could not get password values for '%s'\n",
                      dn);
        res = -1;
        goto free_and_return;
    }
    if (password_values[1] != NULL) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "update_pw_encoding", "Multivalued password attribute not supported: '%s'\n",
                      dn);
        res = -1;
        goto free_and_return;
    }

    /*
     * Get the current system pw policy.
     */
    pwpolicy = new_passwdPolicy(orig_pb, dn);
    if (pwpolicy == NULL || pwpolicy->pw_storagescheme == NULL) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "update_pw_encoding", "Could not get requested encoding scheme: '%s'\n",
                      dn);
        res = -1;
        goto free_and_return;
    }

    /*
     * If the scheme is the same as current, do nothing!
     */
    curpwsp = pw_val2scheme((char *)slapi_value_get_string(password_values[0]), NULL, 1);
    if (curpwsp != NULL) {
        if (strcmp(curpwsp->pws_name, pwpolicy->pw_storagescheme->pws_name) == 0) {
            res = 0; // Nothing to do
            goto free_and_return;
        }
        /*
         * If the scheme is clear or crypt, we also do nothing to prevent breaking some application
         * integrations. See pwdstorage.h
         */
        if (strcmp(curpwsp->pws_name, "CLEAR") == 0 || strcmp(curpwsp->pws_name, "CRYPT") == 0) {
            res = 0; // Nothing to do
            goto free_and_return;
        }
    }

    /*
     * We are commited to upgrading the hash content now!
     */

    hashed_val = slapi_encode_ext(NULL, NULL, cleartextpassword, pwpolicy->pw_storagescheme->pws_name);
    if (hashed_val == NULL) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "update_pw_encoding", "Could not re-encode password: '%s'\n",
                      dn);
        res = -1;
        goto free_and_return;
    }

    slapi_mods_add_string(&smods, LDAP_MOD_REPLACE, SLAPI_USERPWD_ATTR, hashed_val);
    slapi_ch_free((void **)&hashed_val);

    pb = slapi_pblock_new();
    /* We don't want to overwrite the modifiersname, etc. attributes,
     * so we set a flag for this operation.
     * We also set the repl flag to avoid updating password history */
    slapi_modify_internal_set_pb_ext(pb, sdn,
                                     slapi_mods_get_ldapmods_byref(&smods),
                                     NULL,                         /* Controls */
                                     NULL,                         /* UniqueID */
                                     pw_get_componentID(),         /* PluginID */
                                     OP_FLAG_SKIP_MODIFIED_ATTRS |
                                     OP_FLAG_ACTION_SKIP_PWDPOLICY);          /* Flags */
    slapi_modify_internal_pb(pb);

    slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &res);
    if (res != LDAP_SUCCESS) {
        slapi_log_err(SLAPI_LOG_WARNING,
                      "update_pw_encoding", "Modify error %d on entry '%s'\n",
                      res, dn);
    }

free_and_return:
    if (curpwsp) free_pw_scheme(curpwsp);
    if (pb) slapi_pblock_destroy(pb);
    slapi_mods_done(&smods);
    return res;
}
