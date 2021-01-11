/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2009 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * Dereference plug-in
 */
#include <string.h>
#include "deref.h"
#include <nspr.h>

#ifndef DN_SYNTAX_OID
#define DN_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.12"
#endif
#ifndef NAME_AND_OPTIONAL_UID_SYNTAX_OID
#define NAME_AND_OPTIONAL_UID_SYNTAX_OID "1.3.6.1.4.1.1466.115.121.1.34"
#endif

/*
 * Plug-in globals
 */
static void *_PluginID = NULL;
static char *_PluginDN = NULL;

static Slapi_PluginDesc pdesc = {DEREF_FEATURE_DESC,
                                 VENDOR,
                                 DS_PACKAGE_VERSION,
                                 DEREF_PLUGIN_DESC};

/*
 * Plug-in management functions
 */
int deref_init(Slapi_PBlock *pb);
static int deref_start(Slapi_PBlock *pb);
static int deref_close(Slapi_PBlock *pb);

/*
 * Operation callbacks (where the real work is done)
 */
static int deref_pre_search(Slapi_PBlock *pb);
static int deref_pre_entry(Slapi_PBlock *pb);

typedef struct _DerefSpec
{
    char *derefattr; /* attribute to deref - must have DN syntax */
    char **attrs;    /* attributes to return from dereferenced entry */
} DerefSpec;

static DerefSpec *
new_DerefSpec(char *derefattr, char **attrs)
{
    DerefSpec *spec = (DerefSpec *)slapi_ch_calloc(1, sizeof(DerefSpec));
    spec->derefattr = derefattr;
    spec->attrs = attrs;

    return spec;
}

static void
delete_DerefSpec(DerefSpec **spec)
{
    if (spec && *spec) {
        slapi_ch_free_string(&((*spec)->derefattr));
        slapi_ch_array_free((*spec)->attrs);
        slapi_ch_free((void **)spec);
    }
}

typedef struct _DerefSpecList
{
    DerefSpec **list;
    int count;
} DerefSpecList;

static DerefSpecList *
new_DerefSpecList(void)
{
    DerefSpecList *speclist = (DerefSpecList *)slapi_ch_calloc(1, sizeof(DerefSpecList));

    return speclist;
}

static void
delete_DerefSpecList(DerefSpecList **speclist)
{
    if (speclist && *speclist) {
        int ii;
        for (ii = 0; ii < (*speclist)->count; ++ii) {
            delete_DerefSpec(&((*speclist)->list[ii]));
        }
        slapi_ch_free((void **)&((*speclist)->list));
        slapi_ch_free((void **)speclist);
    }
}

static int deref_register_operation_extension(void);
static int deref_extension_type;
static int deref_extension_handle;

static const DerefSpecList *
deref_get_operation_extension(Slapi_PBlock *pb)
{
    Slapi_Operation *op;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    return (const DerefSpecList *)slapi_get_object_extension(deref_extension_type,
                                                             op, deref_extension_handle);
}

static void
deref_set_operation_extension(Slapi_PBlock *pb, DerefSpecList *speclist)
{
    Slapi_Operation *op;

    slapi_pblock_get(pb, SLAPI_OPERATION, &op);
    slapi_set_object_extension(deref_extension_type, op,
                               deref_extension_handle, (void *)speclist);
}

/*
 * Plugin identity functions
 */
void
deref_set_plugin_id(void *pluginID)
{
    _PluginID = pluginID;
}

void *
deref_get_plugin_id(void)
{
    return _PluginID;
}

void
deref_set_plugin_dn(char *pluginDN)
{
    _PluginDN = pluginDN;
}

char *
deref_get_plugin_dn(void)
{
    return _PluginDN;
}

/*
 * Plug-in initialization functions
 */
int
deref_init(Slapi_PBlock *pb)
{
    int status = 0;
    char *plugin_identity = NULL;

    slapi_log_err(SLAPI_LOG_TRACE, DEREF_PLUGIN_SUBSYSTEM,
                  "--> deref_init\n");

    /* Store the plugin identity for later use.
     * Used for internal operations. */
    slapi_pblock_get(pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    PR_ASSERT(plugin_identity);
    deref_set_plugin_id(plugin_identity);

    /* Register callbacks */
    if (slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
                         SLAPI_PLUGIN_VERSION_01) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_START_FN,
                         (void *)deref_start) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_CLOSE_FN,
                         (void *)deref_close) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
                         (void *)&pdesc) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_SEARCH_FN,
                         (void *)deref_pre_search) != 0 ||
        slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ENTRY_FN,
                         (void *)deref_pre_entry) != 0 ||
        deref_register_operation_extension()) {
        slapi_log_err(SLAPI_LOG_ERR, DEREF_PLUGIN_SUBSYSTEM,
                      "deref_init - Failed to register plugin\n");
        status = -1;
    }

    if (status == 0) {
        slapi_register_supported_control(LDAP_CONTROL_X_DEREF, SLAPI_OPERATION_SEARCH);
    }

    slapi_log_err(SLAPI_LOG_TRACE, DEREF_PLUGIN_SUBSYSTEM,
                  "<-- deref_init\n");
    return status;
}


/*
 * deref_start()
 *
 * Creates config lock and loads config cache.
 */
static int
deref_start(Slapi_PBlock *pb __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_TRACE, DEREF_PLUGIN_SUBSYSTEM,
                  "--> deref_start\n");

    slapi_log_err(SLAPI_LOG_PLUGIN, DEREF_PLUGIN_SUBSYSTEM,
                  "deref_start - Ready for service\n");

    slapi_log_err(SLAPI_LOG_TRACE, DEREF_PLUGIN_SUBSYSTEM,
                  "<-- deref_start\n");

    return 0;
}

/*
 * deref_close()
 *
 * Cleans up the config cache.
 */
static int
deref_close(Slapi_PBlock *pb __attribute__((unused)))
{
    slapi_log_err(SLAPI_LOG_TRACE, DEREF_PLUGIN_SUBSYSTEM,
                  "--> deref_close\n");

    slapi_log_err(SLAPI_LOG_TRACE, DEREF_PLUGIN_SUBSYSTEM,
                  "<-- deref_close\n");

    return 0;
}

static int
deref_attr_in_spec_list(DerefSpecList *speclist, const char *derefattr)
{
    int ii;
    int ret = 0;

    PR_ASSERT(speclist && derefattr);

    for (ii = 0; !ret && (ii < speclist->count); ++ii) {
        if (slapi_attr_types_equivalent(derefattr, speclist->list[ii]->derefattr)) {
            ret = 1; /* match */
        }
    }

    return ret;
}

/* grows spec list, consumes spec */
static void
deref_add_spec_to_list(DerefSpecList *speclist, DerefSpec *spec)
{
    PR_ASSERT(speclist && spec);

    speclist->count++;
    speclist->list = (DerefSpec **)slapi_ch_realloc((char *)speclist->list,
                                                    speclist->count * sizeof(DerefSpec *));
    speclist->list[speclist->count - 1] = spec; /* consumed */
}

static const DerefSpec *
deref_get_next_spec(const DerefSpecList *speclist, int *ii)
{
    const DerefSpec *spec = NULL;

    if (*ii < speclist->count) {
        spec = speclist->list[*ii];
        (*ii)++;
    }

    return spec;
}

static const DerefSpec *
deref_get_first_spec(const DerefSpecList *speclist, int *ii)
{
    *ii = 0;
    return deref_get_next_spec(speclist, ii);
}

static int
deref_check_for_dn_syntax(const char *derefattr)
{
    int ret = 0;
    Slapi_Attr *attr = slapi_attr_new();

    if (attr) {
        char *oid = NULL;

        slapi_attr_init(attr, derefattr);
        slapi_attr_get_syntax_oid_copy(attr, &oid);
        ret = oid && (!strcmp(oid, DN_SYNTAX_OID) || !strcmp(oid, NAME_AND_OPTIONAL_UID_SYNTAX_OID));
        slapi_ch_free_string(&oid);
        slapi_attr_free(&attr);
    }

    return ret;
}

static void
deref_add_spec(DerefSpecList *speclist, char **derefattr, char ***attrs, int critical, int *ldapcode, const char **ldaperrtext)
{
    PR_ASSERT(speclist && derefattr && attrs && ldapcode && ldaperrtext);

    if (!deref_check_for_dn_syntax(*derefattr)) { /* derefattr must have DN syntax */
        if (critical) {
            /* not DN syntax */
            *ldapcode = LDAP_PROTOCOL_ERROR;
            *ldaperrtext = "A dereference attribute must have DN syntax";
        }
    } else if (deref_attr_in_spec_list(speclist, *derefattr)) {
        /* duplicate */
        *ldapcode = LDAP_PROTOCOL_ERROR;
        *ldaperrtext = "A dereference attribute was specified more than once in a dereference specification";
    } else {
        DerefSpec *spec = new_DerefSpec(*derefattr, *attrs);
        *derefattr = NULL;                      /* consumed */
        *attrs = NULL;                          /* consumed */
        deref_add_spec_to_list(speclist, spec); /* consumes spec */
    }

    return;
}

/*
         controlValue ::= SEQUENCE OF derefSpec DerefSpec

         DerefSpec ::= SEQUENCE {
             derefAttr       attributeDescription,    ; with DN syntax
             attributes      AttributeList }

         AttributeList ::= SEQUENCE OF attr AttributeDescription

   Each derefSpec.derefAttr MUST be unique within controlValue.
 */
static void
deref_parse_ctrl_value(DerefSpecList *speclist, const struct berval *ctrlbv, int critical, int *ldapcode, const char **ldaperrtext)
{
    BerElement *ber = NULL;
    ber_tag_t tag;
    ber_len_t len = -1;
    char *last;

    PR_ASSERT(ctrlbv && ctrlbv->bv_val && ctrlbv->bv_len && ldapcode && ldaperrtext);

    if (!BV_HAS_DATA(ctrlbv)) {
        *ldapcode = LDAP_PROTOCOL_ERROR;
        *ldaperrtext = "Empty deref control value";
        return;
    }

    ber = ber_init((struct berval *)ctrlbv);
    for (tag = ber_first_element(ber, &len, &last);
         (tag != LBER_ERROR) && (tag != LBER_END_OF_SEQORSET);
         tag = ber_next_element(ber, &len, last)) {
        char *derefattr = NULL;
        char **attrs = NULL;
        len = -1; /* reset */
        if ((LBER_ERROR == ber_scanf(ber, "{a{v}}", &derefattr, &attrs)) ||
            !derefattr || !attrs || !attrs[0]) {
            if (critical)
                *ldapcode = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
            else
                *ldapcode = LDAP_PROTOCOL_ERROR;
            if (!derefattr) {
                *ldaperrtext = "Missing dereference attribute name";
            } else {
                *ldaperrtext = "Missing list of attributes to dereference";
            }
        } else {
            /* will consume derefattr and attrs and set them to NULL if successful */
            deref_add_spec(speclist, &derefattr, &attrs, critical, ldapcode, ldaperrtext);
        }
        if (derefattr) { /* not consumed - error */
            slapi_ch_free_string(&derefattr);
        }
        if (attrs) { /* not consumed - error */
            slapi_ch_array_free(attrs);
        }
    }
    ber_free(ber, 1);
}

static int
deref_incompatible_ctrl(const char *oid __attribute__((unused)))
{
    return 0; /* no known incompatible ctrls yet */
}

/*
 * Operation callback functions
 */

/*
 * deref_pre_search()
 *
 * see if the dereference control has been specified
 * if so, parse it, and check to make sure it meets the
 * protocol specification (no duplicate attributes, etc.)
 */
static int
deref_pre_search(Slapi_PBlock *pb)
{
    int ldapcode = LDAP_SUCCESS;
    const LDAPControl **reqctrls = NULL;
    const LDAPControl *derefctrl = NULL;
    const char *ldaperrtext = "Unknown error";
    const char *incompatible = NULL;
    DerefSpecList *speclist = NULL;
    int ii;
    int iscritical = 0;

    slapi_log_err(SLAPI_LOG_TRACE, DEREF_PLUGIN_SUBSYSTEM,
                  "--> deref_pre_search\n");

    /* see if the deref request control is in the list of
       controls - if so, parse and validate it */
    slapi_pblock_get(pb, SLAPI_REQCONTROLS, &reqctrls);
    for (ii = 0; (ldapcode == LDAP_SUCCESS) && reqctrls && reqctrls[ii]; ++ii) {
        const LDAPControl *ctrl = reqctrls[ii];
        if (!strcmp(ctrl->ldctl_oid, LDAP_CONTROL_X_DEREF)) {
            if (derefctrl) { /* already specified */
                slapi_log_err(SLAPI_LOG_ERR, DEREF_PLUGIN_SUBSYSTEM,
                              "deref_pre_search - The dereference control was specified more than once - it must be specified only once in the search request\n");
                ldapcode = LDAP_PROTOCOL_ERROR;
                ldaperrtext = "The dereference control cannot be specified more than once";
                derefctrl = NULL;
            } else if (!ctrl->ldctl_value.bv_len) {
                slapi_log_err(SLAPI_LOG_ERR, DEREF_PLUGIN_SUBSYSTEM,
                              "deref_pre_search - No control value specified for dereference control\n");
                ldapcode = LDAP_PROTOCOL_ERROR;
                ldaperrtext = "The dereference control must have a value";
                iscritical = ctrl->ldctl_iscritical;
            } else if (!ctrl->ldctl_value.bv_val) {
                slapi_log_err(SLAPI_LOG_ERR, DEREF_PLUGIN_SUBSYSTEM,
                              "deref_pre_search - No control value specified for dereference control\n");
                ldapcode = LDAP_PROTOCOL_ERROR;
                ldaperrtext = "The dereference control must have a value";
                iscritical = ctrl->ldctl_iscritical;
            } else if (!ctrl->ldctl_value.bv_val[0] || !ctrl->ldctl_value.bv_len) {
                slapi_log_err(SLAPI_LOG_ERR, DEREF_PLUGIN_SUBSYSTEM,
                              "deref_pre_search - Empty control value specified for dereference control\n");
                ldapcode = LDAP_PROTOCOL_ERROR;
                ldaperrtext = "The dereference control must have a non-empty value";
                iscritical = ctrl->ldctl_iscritical;
            } else {
                derefctrl = ctrl;
                iscritical = ctrl->ldctl_iscritical;
            }
        } else if (deref_incompatible_ctrl(ctrl->ldctl_oid)) {
            incompatible = ctrl->ldctl_oid;
        }
    }

    if (derefctrl && incompatible) {
        slapi_log_err(SLAPI_LOG_ERR, DEREF_PLUGIN_SUBSYSTEM,
                      "deref_pre_search - Cannot use the dereference control and control [%s] for the same search operation\n",
                      incompatible);
        /* not sure if this is a hard failure - the current spec says:
           The semantics of the criticality field are specified in [RFC4511].
           In detail, the criticality of the control determines whether the
           control will or will not be used, and if it will not be used, whether
           the operation will continue without returning the control in the
           response, or fail, returning unavailableCriticalExtension.  If the
           control is appropriate for an operation and, for any reason, it
           cannot be applied in its entirety to a single SearchResultEntry
           response, it MUST NOT be applied to that specific SearchResultEntry
           response, without affecting its application to any subsequent
           SearchResultEntry response.
        */
        /* so for now, just return LDAP_SUCCESS and don't do anything else */
        derefctrl = NULL;
    }

    /* we have something now - see if it is a valid control */
    if (derefctrl) {
        speclist = new_DerefSpecList();
        deref_parse_ctrl_value(speclist, &derefctrl->ldctl_value, derefctrl->ldctl_iscritical,
                               &ldapcode, &ldaperrtext);
    }

    if (ldapcode != LDAP_SUCCESS) {
        if (iscritical) {
            ldapcode = LDAP_UNAVAILABLE_CRITICAL_EXTENSION;
        }
        slapi_pblock_set(pb, SLAPI_PLUGIN_OPRETURN, &ldapcode);
        slapi_send_ldap_result(pb, ldapcode, NULL, (char *)ldaperrtext, 0, NULL);
        delete_DerefSpecList(&speclist);
    } else {
        /* save spec list away for the pre_entry callback */
        deref_set_operation_extension(pb, speclist);
    }

    slapi_log_err(SLAPI_LOG_TRACE, DEREF_PLUGIN_SUBSYSTEM,
                  "<-- deref_pre_search\n");

    return ldapcode;
}

/*
  See if the client has the requested rights over the entry and the specified
  attributes.  Each attribute in attrs will be tested.  The retattrs array will
  hold the attributes that could be read.  If NULL, this means the entry is
  not allowed, or none of the requested attributes are allowed.  If non-NULL, this
  array can be passed to a subsequent search operation.
*/
static int
deref_check_access(Slapi_PBlock *pb, const Slapi_Entry *ent, const char *entdn, const char **attrs, char ***retattrs, int rights)
{
    Slapi_Entry *etest = NULL;
    const char *attrname;

    /* if there is no entry, create a dummy entry - this can save a search
       on an entry we should not be allowed to search */
    if (!ent) {
        etest = slapi_entry_alloc();
        slapi_sdn_set_dn_byref(slapi_entry_get_sdn(etest), entdn);
    } else {
        etest = (Slapi_Entry *)ent;
    }

    *retattrs = NULL;
    for (attrname = *attrs; attrname; attrname = *(++attrs)) {
        /* first, check access control - see if client can read the requested attribute */
        int ret = slapi_access_allowed(pb, etest, (char *)attrname, NULL, rights);
        if (ret != LDAP_SUCCESS) {
            slapi_log_err(SLAPI_LOG_PLUGIN, DEREF_PLUGIN_SUBSYSTEM,
                          "deref_check_access - The client does not have permission to read attribute %s in entry %s\n",
                          attrname, slapi_entry_get_dn_const(etest));
        } else {
            slapi_ch_array_add(retattrs, (char *)attrname); /* retattrs and attrs share pointer to attrname */
        }
    }

    if (ent != etest) {
        slapi_entry_free(etest);
    }

    /* see if we had at least one attribute that could be accessed */
    return *retattrs == NULL ? LDAP_INSUFFICIENT_ACCESS : LDAP_SUCCESS;
}

/*
  must check access before calling this function
*/
static int
deref_get_values(const Slapi_Entry *ent, const char *attrname, Slapi_ValueSet **results, int *type_name_disposition, char **actual_type_name, int flags, int *buffer_flags)
{
    int ret = slapi_vattr_values_get((Slapi_Entry *)ent, (char *)attrname, results, type_name_disposition,
                                     actual_type_name, flags, buffer_flags);

    return ret;
}

static void
deref_values_free(Slapi_ValueSet **results, char **actual_type_name, int buffer_flags)
{
    slapi_vattr_values_free(results, actual_type_name, buffer_flags);
}

static void
deref_do_deref_attr(Slapi_PBlock *pb, BerElement *ctrlber, const char *derefdn, const char *derefattr, const char **attrs)
{
    char **retattrs = NULL;
    Slapi_PBlock *derefpb = NULL;
    Slapi_Entry **entries = NULL;
    int rc;

    /*  If the access check on the attributes is done without retrieving the entry
 *  it cannot handle acis which need teh entry, eg to apply a targetfilter rule
 *  So the determination of attrs which can be dereferenced is delayed
 */
    derefpb = slapi_pblock_new();
    slapi_search_internal_set_pb(derefpb, derefdn, LDAP_SCOPE_BASE,
                                 "(objectclass=*)", retattrs, 0,
                                 NULL, NULL, deref_get_plugin_id(), 0);

    slapi_search_internal_pb(derefpb);
    slapi_pblock_get(derefpb, SLAPI_PLUGIN_INTOP_RESULT, &rc);
    if (LDAP_SUCCESS == rc) {
        slapi_pblock_get(derefpb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
        if (entries) {
            if (entries[1]) {
                /* too many entries */
                slapi_log_err(SLAPI_LOG_PLUGIN, DEREF_PLUGIN_SUBSYSTEM,
                              "deref_do_deref_attr - More than one entry matching DN [%s]\n",
                              derefdn);
            } else if (entries[0] == NULL) {
                int32_t op_id;
                uint64_t conn_id;

                slapi_pblock_get(pb, SLAPI_OPERATION_ID, &op_id);
                slapi_pblock_get(pb, SLAPI_CONN_ID, &conn_id);
                /* Weird case not clearly understood:
                 * the entry 'derefdn' exists (else we would have NOT_SUCH_ENTRY in 'rc')
                 * but it is not returned by the internal search. Note that internal search
                 * returns tombstone or subentry.
                 * Just to prevent a crash, catch this error condition and log a warning
                 */
                slapi_log_err(SLAPI_LOG_WARNING, DEREF_PLUGIN_SUBSYSTEM,
                              "deref_do_deref_attr - conn=%" PRIu64 " op=%d - failed to retrieve the entry [%s], although the entry exists\n",
                              conn_id, op_id, derefdn);
            } else {
                int ii;
                int needattrvals = 1; /* need attrvals sequence? */
                if (deref_check_access(pb, entries[0], derefdn, attrs, &retattrs,
                                       SLAPI_ACL_READ)) {
                    slapi_log_err(SLAPI_LOG_PLUGIN, DEREF_PLUGIN_SUBSYSTEM,
                                  "deref_do_deref_attr - The client does not have permission to read the requested "
                                  "attributes in entry %s\n",
                                  derefdn);
                } else {
                    ber_printf(ctrlber, "{ss", derefattr, derefdn); /* begin DerefRes + derefAttr + derefVal */
                    for (ii = 0; retattrs[ii]; ++ii) {
                        Slapi_Value *sv;
                        int idx = 0;
                        Slapi_ValueSet *results = NULL;
                        int type_name_disposition = 0;
                        char *actual_type_name = NULL;
                        int flags = 0;
                        int buffer_flags = 0;
                        int needpartialattr = 1; /* need PartialAttribute sequence? */
                        int needvalsset = 1;

#if defined(USE_OLD_UNHASHED)
                        if (is_type_forbidden(retattrs[ii])) {
                            slapi_log_err(SLAPI_LOG_PLUGIN, DEREF_PLUGIN_SUBSYSTEM,
                                          "deref_do_deref_attr - skip forbidden attribute [%s]\n", derefdn);
                            continue;
                        }
#endif
                        deref_get_values(entries[0], retattrs[ii], &results, &type_name_disposition,
                                         &actual_type_name, flags, &buffer_flags);

                        if (results) {
                            idx = slapi_valueset_first_value(results, &sv);
                        }
                        for (; results && sv; idx = slapi_valueset_next_value(results, idx, &sv)) {
                            const struct berval *bv = slapi_value_get_berval(sv);
                            if (needattrvals) {
                                /* we have at least one attribute with values in
                                   DerefRes.attrVals */
                                /* attrVals is OPTIONAL - only added if there are
                                   any values to send */
                                ber_printf(ctrlber, "t{", (LBER_CLASS_CONTEXT | LBER_CONSTRUCTED));
                                needattrvals = 0;
                            }
                            if (needpartialattr) {
                                /* This attribute in attrVals has values */
                                ber_printf(ctrlber, "{s", retattrs[ii]);
                                needpartialattr = 0;
                            }
                            if (needvalsset) {
                                /* begin the vals SET of values for this attribute */
                                ber_printf(ctrlber, "[");
                                needvalsset = 0;
                            }
                            ber_printf(ctrlber, "O", bv);
                        } /* for each value in retattrs[ii] */
                        deref_values_free(&results, &actual_type_name, buffer_flags);
                        if (needvalsset == 0) {
                            ber_printf(ctrlber, "]");
                        }
                        if (needpartialattr == 0) {
                            ber_printf(ctrlber, "}");
                        }
                    } /* for each attr in retattrs */
                    if (needattrvals == 0) {
                        ber_printf(ctrlber, "}");
                    }
                    ber_printf(ctrlber, "}"); /* end DerefRes */
                }
            }
        } else { /* nothing */
            slapi_log_err(SLAPI_LOG_PLUGIN, DEREF_PLUGIN_SUBSYSTEM,
                          "deref_do_deref_attr - No entries matching [%s]\n", derefdn);
        }
    } else {
        /* handle error */
        slapi_log_err(SLAPI_LOG_PLUGIN, DEREF_PLUGIN_SUBSYSTEM,
                      "deref_do_deref_attr - Could not read entry with DN [%s]: error %d:%s\n",
                      derefdn, rc, ldap_err2string(rc));
    }
    slapi_free_search_results_internal(derefpb);
    slapi_pblock_destroy(derefpb);
    slapi_ch_free((void **)&retattrs); /* retattrs does not own the strings */
}

static int
deref_pre_entry(Slapi_PBlock *pb)
{
    int ii = 0;
    const DerefSpec *spec;
    const Slapi_Entry *ent = NULL;
    const DerefSpecList *speclist = deref_get_operation_extension(pb);
    BerElement *ctrlber = NULL;
    LDAPControl *ctrl = NULL;
    const LDAPControl **searchctrls = NULL;
    LDAPControl **newsearchctrls = NULL;

    if (!speclist) {
        return 0; /* nothing to do */
    }

    ctrlber = ber_alloc();
    ber_printf(ctrlber, "{"); /* begin control value */

    slapi_pblock_get(pb, SLAPI_SEARCH_ENTRY_ORIG, &ent);
    for (spec = deref_get_first_spec(speclist, &ii); ent && spec;
         spec = deref_get_next_spec(speclist, &ii)) {
        Slapi_Value *sv;
        int idx = 0;
        Slapi_ValueSet *results = NULL;
        int type_name_disposition = 0;
        char *actual_type_name = NULL;
        int flags = 0;
        int buffer_flags = 0;
        const char *attrs[2];
        char **retattrs = NULL;

        attrs[0] = spec->derefattr;
        attrs[1] = NULL;

        if (deref_check_access(pb, ent, NULL, attrs, &retattrs,
                               SLAPI_ACL_READ)) {
            slapi_log_err(SLAPI_LOG_PLUGIN, DEREF_PLUGIN_SUBSYSTEM,
                          "deref_pre_entry - The client does not have permission to read attribute %s in entry %s\n",
                          spec->derefattr, slapi_entry_get_dn_const(ent));
            continue;
        }

        slapi_ch_free((void **)&retattrs); /* retattrs does not own strings */
        deref_get_values(ent, spec->derefattr, &results, &type_name_disposition,
                         &actual_type_name, flags, &buffer_flags);

        /* spec->derefattr is DN valued attr - results will be a list of DNs */
        if (results) {
            idx = slapi_valueset_first_value(results, &sv);
        }
        for (; results && sv; idx = slapi_valueset_next_value(results, idx, &sv)) {
            const char *derefdn = slapi_value_get_string(sv);

            deref_do_deref_attr(pb, ctrlber, derefdn, spec->derefattr, (const char **)spec->attrs);
        }
        deref_values_free(&results, &actual_type_name, buffer_flags);
    }

    ber_printf(ctrlber, "}"); /* end control val */

    slapi_build_control(LDAP_CONTROL_X_DEREF, ctrlber, 0, &ctrl);
    /* get the list of controls */
    slapi_pblock_get(pb, SLAPI_SEARCH_CTRLS, &searchctrls);
    /* dup them */
    slapi_add_controls(&newsearchctrls, (LDAPControl **)searchctrls, 1);
    /* add our control */
    slapi_add_control_ext(&newsearchctrls, ctrl, 0);
    ctrl = NULL; /* newsearchctrls owns it now */
    /* set the controls in the pblock */
    slapi_pblock_set(pb, SLAPI_SEARCH_CTRLS, newsearchctrls);
    ber_free(ctrlber, 1);

    return 0;
}

/* consumer operation extension constructor */
static void *
deref_operation_extension_ctor(void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    /* we only set the extension value explicitly if the
       client requested the control - see deref_pre_search */
    return NULL; /* we don't set anything in the ctor */
}

/* consumer operation extension destructor */
static void
deref_operation_extension_dtor(void *ext, void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    DerefSpecList *speclist = (DerefSpecList *)ext;
    delete_DerefSpecList(&speclist);
}

static int
deref_register_operation_extension(void)
{
    return slapi_register_object_extension(DEREF_PLUGIN_SUBSYSTEM,
                                           SLAPI_EXT_OPERATION,
                                           deref_operation_extension_ctor,
                                           deref_operation_extension_dtor,
                                           &deref_extension_type,
                                           &deref_extension_handle);
}
