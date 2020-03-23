/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2020 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#include "slapi-private.h"
#include "slap.h"

typedef struct {
    int32_t filter_rewriters;
    int32_t computedAttr_rewriters;
} nb_rewriters_t;

#define REWRITERS_CONTAINER_DN "cn=rewriters,cn=config"
#define SUBSYTEM_LOG "rewriters"


/* This is an example entry that calls a rewriter that generate a 'foo' attribute
 * return code (computed.c:compute_call_evaluators)
 *   -1 : keep looking
 *    0 : rewrote OK
 *  else: failure
 *
 * dn: cn=foo,cn=rewriters,cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: foo
 * nsslapd-libpath: /lib/dirsrv/libslapd.so
 * nsslapd-returnedAttrRewriter: example_foo_computedAttr_rewriter
 */
int32_t
example_foo_computedAttr_rewriter(computed_attr_context *c, char *type, Slapi_Entry *e, slapi_compute_output_t outputfn)
{
    int32_t rc = COMPUTE_CALLBACK_CONTINUE; /* Let's others computed elevator play */

    if (strcasecmp(type, "foo") == 0) {
        Slapi_Attr our_attr;
        slapi_attr_init(&our_attr, "foo");
        our_attr.a_flags = SLAPI_ATTR_FLAG_OPATTR;
        valueset_add_string(&our_attr, &our_attr.a_present_values, "foo computed value", CSN_TYPE_UNKNOWN, NULL);
        rc = (*outputfn)(c, &our_attr, e);
        attr_done(&our_attr);
    }
    return rc;
}


/* This is an example callback to substitute
 * attribute type 'from' with 'to' in all
 * the filter components
 * example_substitute_type is a callback (FILTER_APPLY_FN) used by slapi_filter_apply
 * typedef int (FILTER_APPLY_FN)(Slapi_Filter f, void *arg)
 * To stick to the definition, the callback is defined using 'int' rather 'int32_t'
 */
typedef struct {
    char *attrtype_from;
    char *attrtype_to;
} example_substitute_type_arg_t;

static int
example_substitute_type(Slapi_Filter *f, void *arg)
{
    example_substitute_type_arg_t *substitute_arg = (example_substitute_type_arg_t *) arg;
    char *filter_type;

    if ((substitute_arg == NULL) ||
        (substitute_arg->attrtype_from == NULL) ||
        (substitute_arg->attrtype_to == NULL)) {
        return SLAPI_FILTER_SCAN_STOP;
    }

    /* Substitute 'from' by 'to' attribute type */
    if (slapi_filter_get_attribute_type(f, &filter_type) == 0) {
            if (strcasecmp(filter_type, substitute_arg->attrtype_from) == 0) {
                slapi_filter_changetype(f, substitute_arg->attrtype_to);
            }
    }

    /* Return continue because we should
     * substitute 'from' in all filter components
     */
    return SLAPI_FILTER_SCAN_CONTINUE;
}

/* This is an example entry that calls a rewriter that
 * exchange in the filter 'foo' into 'cn
 * return code (from computed.c:compute_rewrite_search_filter):
 *   -1 : keep looking
 *    0 : rewrote OK
 *    1 : refuse to do this search
 *    2 : operations error
 *
 * dn: cn=foo,cn=rewriters,cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: foo
 * nsslapd-libpath: /lib/dirsrv/libslapd.so
 * nsslapd-filterrewriter: example_foo2cn_filter_rewriter
 */
int32_t
example_foo2cn_filter_rewriter(Slapi_PBlock *pb)
{
    Slapi_Filter *clientFilter = NULL;
    int error_code = 0;
    int rc;
    example_substitute_type_arg_t arg;
    arg.attrtype_from = "foo";
    arg.attrtype_to = "cn";

    slapi_pblock_get(pb, SLAPI_SEARCH_FILTER, &clientFilter);
    rc = slapi_filter_apply(clientFilter, example_substitute_type, &arg, &error_code);
    if (rc == SLAPI_FILTER_SCAN_NOMORE) {
        return SEARCH_REWRITE_CALLBACK_CONTINUE; /* Let's others rewriter play */
    } else {
        slapi_log_err(SLAPI_LOG_ERR,
                          "example_foo2cn_filter_rewriter", "Could not update the search filter - error %d (%d)\n",
                          rc, error_code);
        return SEARCH_REWRITE_CALLBACK_ERROR; /* operation error */
    }
}

/*
 * This function registers filter rewriters and computed attribute rewriters
 * listed in each rewriter config entry
 *
 * cn=ADrewrite,cn=rewriters,cn=config
 * objectClass: top
 * objectClass: extensibleObject
 * cn: ADrewrite
 * nsslapd-libPath: libadrewrite
 * nsslapd-filterRewriter: objectcategory_filter_rewrite
 * nsslapd-filterRewriter: objectSID_rewrite
 * nsslapd-returnedAttrRewriter: givenname_rewrite
 * nsslapd-returnedAttrRewriter: objectcategory_returnedAttr_rewrite
 *
 * This is search_entry callback used by slapi_search_internal_callback
 * typedef int (*plugin_search_entry_callback)(Slapi_Entry *e, void *callback_data)
 * To stick to the definition, the callback is defined returning 'int' rather 'int32_t'
 */
static int
register_rewriters(Slapi_Entry *e, void *callback_data)
{
    nb_rewriters_t *nb_rewriters = callback_data;
    char *libpath;
    char **values = NULL;
    slapi_search_rewrite_callback_t filter_rewriter_cb;
    slapi_compute_callback_t computeAttr_rewriter_cb;

    /* Load the rewriter callback  */
    libpath = (char *) slapi_entry_attr_get_charptr(e, "nsslapd-libPath");

    /* register the filter rewriters */
    values = slapi_entry_attr_get_charray(e, "nsslapd-filterRewriter");
    if (values) {
        for (size_t i = 0; values && values[i]; ++i) {
            filter_rewriter_cb = (slapi_search_rewrite_callback_t) sym_load(libpath, values[i], "custom filter rewriter", 1);
            if ((filter_rewriter_cb == NULL) || slapi_compute_add_search_rewriter(filter_rewriter_cb)) {
                slapi_log_err(SLAPI_LOG_ERR, SUBSYTEM_LOG, "register_rewriters: "
                              "Fail to register filerRewriter %s\n", values[i]);
            }
            nb_rewriters->filter_rewriters++;
        }
        slapi_ch_array_free(values);
        values = NULL;
    }

    /* register the computed attribute rewriters */
    values = slapi_entry_attr_get_charray(e, "nsslapd-returnedAttrRewriter");
    if (values) {
        for (size_t i = 0; values && values[i]; ++i) {
            computeAttr_rewriter_cb = (slapi_compute_callback_t) sym_load(libpath, values[i], "custom computed attribute rewriter", 1);
            if ((computeAttr_rewriter_cb == NULL) || slapi_compute_add_evaluator(computeAttr_rewriter_cb)) {
                slapi_log_err(SLAPI_LOG_ERR, SUBSYTEM_LOG, "register_rewriters: "
                              "Fail to register nsslapd-returnedAttrRewriter %s\n", values[i]);
            }
            nb_rewriters->computedAttr_rewriters++;
        }
        slapi_ch_array_free(values);
        values = NULL;
    }

    slapi_ch_free_string(&libpath);
    return 0;
}

/* Adds, if it does not already exist, the container
 * of the rewriters config entries: cn=rewriters,cn=config
 */
static int32_t
add_rewriters_container(void)
{
    char entry_string[1024] = {0};
    Slapi_PBlock *pb = NULL;
    Slapi_Entry *e = NULL;
    int return_value;
    int32_t rc = 0;

    /* Create cn=rewriters,cn=config Slapi_Entry*/
    PR_snprintf(entry_string, sizeof (entry_string) - 1, "dn: %s\nobjectclass: top\nobjectclass: extensibleobject\ncn: rewriters\n", REWRITERS_CONTAINER_DN);
    e = slapi_str2entry(entry_string, 0);

    /* Add it, if it already exist that is okay */
    pb = slapi_add_entry_internal(e, 0, 0 /* log_change */);
    if (pb == NULL) {
        /* the only time NULL pb is returned is when memory allocation fails */
        slapi_log_err(SLAPI_LOG_ERR, SUBSYTEM_LOG, "create_rewriters_container: "
                      "NULL pblock returned from search\n");
        slapi_entry_free(e);
        rc = -1;
        goto done;
    } else {
        slapi_pblock_get(pb, SLAPI_PLUGIN_INTOP_RESULT, &return_value);
    }
    if (return_value != LDAP_SUCCESS && return_value != LDAP_ALREADY_EXISTS) {
        slapi_log_err(SLAPI_LOG_ERR, SUBSYTEM_LOG, "create_rewriters_container - "
                      "Unable to create configuration entry %s: %d\n",
                      REWRITERS_CONTAINER_DN, return_value);
        slapi_entry_free(e);
        rc = -1;
    }
done:
    slapi_pblock_destroy(pb);
    return rc;
}

/* initialization of the filter rewriters and
 * computed attributes rewriters
 */
int32_t
rewriters_init()
{
    nb_rewriters_t nb_rewriters = {0};

    /* Create the rewricter container in case it does not exist */
    if (add_rewriters_container()) {
        slapi_log_err(SLAPI_LOG_ERR, SUBSYTEM_LOG, "rewriters_init: "
                      "Fails to initialize rewriters\n");
        return -1;
    }

    /* For each child of the rewriter container, register filter/computed attribute */
    slapi_search_internal_callback(REWRITERS_CONTAINER_DN, LDAP_SCOPE_ONELEVEL, "(cn=*)",
            NULL, 0 /* attrsonly */,
            &nb_rewriters /* callback_data */,
            NULL /* controls */,
            NULL /* result_callback */,
            register_rewriters,
            NULL /* referral_callback */);

    if (nb_rewriters.filter_rewriters || nb_rewriters.computedAttr_rewriters) {
        slapi_log_err(SLAPI_LOG_INFO, SUBSYTEM_LOG,
                      "registered rewriters for filters: %d - for computed attributes: %d\n",
                      nb_rewriters.filter_rewriters,
                      nb_rewriters.computedAttr_rewriters);
    }

    return 0;
}