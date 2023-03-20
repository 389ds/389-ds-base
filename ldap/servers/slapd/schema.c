/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 *
 * License: GPL (version 3 or any later version).
 * See LICENSE for details.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* schema.c - routines to enforce schema definitions */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <prio.h>
#include <plstr.h>
#include <plhash.h>
#include "slap.h"

#include <ldap_schema.h> /* openldap schema parser */

static struct slapdplugin schema_plugin = {0};

typedef struct sizedbuffer
{
    char *buffer;
    size_t size;
} sizedbuffer;


typedef char *(*schema_strstr_fn_t)(const char *big, const char *little);

/*
 * The schema_oc_kind_strings array is indexed by oc_kind values, i.e.,
 * OC_KIND_STRUCTURAL (0), OC_KIND_AUXILIARY (1), or OC_KIND_ABSTRACT (2).
 * The leading and trailing spaces are intentional.
 */
#define SCHEMA_OC_KIND_COUNT 3
static char *schema_oc_kind_strings_with_spaces[] = {
    " ABSTRACT ",
    " STRUCTURAL ",
    " AUXILIARY ",
};

/* constant strings (used in a few places) */
static const char *schema_obsolete_with_spaces = " OBSOLETE ";
static const char *schema_collective_with_spaces = " COLLECTIVE ";
static const char *schema_nousermod_with_spaces = " NO-USER-MODIFICATION ";

/* user defined origin array */
static char *schema_user_defined_origin[] = {
    "user defined",
    NULL};

/* The policies for the replication of the schema are
 *  - base policy
 *  - extended policies
 * Those policies are enforced when the server is acting as a supplier and
 * when it is acting as a consumer
 *
 * Base policy:
 *      Supplier: before pushing the schema, the supplier checks that each objectclass/attribute of
 *              the consumer schema is a subset of the objectclass/attribute of the supplier schema
 *      Consumer: before accepting a schema (from replication), the consumer checks that
 *              each objectclass/attribute of the consumer schema is a subset of the objectclass/attribute
 *              of the supplier schema
 * Extended policies:
 *      They are stored in repl_schema_policy_t and specifies an "action" to be taken
 *      for specific objectclass/attribute.
 *      Supplier: extended policies are stored in entry "cn=supplierUpdatePolicy,cn=replSchema,cn=config"
 *              and uploaded in static variable: supplier_policy
 *              Before pushing the schema, for each objectclass/attribute defined in supplier_policy:
 *                      if its "action" is REPL_SCHEMA_UPDATE_ACCEPT_VALUE, it is not checked that the
 *                      attribute/objectclass of the consumer is a subset of the attribute/objectclass
 *                      of the supplier schema.
 *
 *                      if its "action" is REPL_SCHEMA_UPDATE_REJECT_VALUE and the consumer schema contains
 *                      attribute/objectclass, then schema is not pushed
 *
 *      Consumer: extended policies are stored in entry "cn=consumerUpdatePolicy,cn=replSchema,cn=config"
 *              and uploaded in static variable: consumer_policy
 *              before accepting a schema (from replication), for each objectclass/attribute defined in
 *              consumer_policy:
 *                      if its "action" is REPL_SCHEMA_UPDATE_ACCEPT_VALUE, it is not checked that the
 *                      attribute/objectclass of the consumer is a subset of the attribute/objectclass
 *                      of the supplier schema.
 *
 *                      if its "action" is REPL_SCHEMA_UPDATE_REJECT_VALUE and the consumer schema contains
 *                      attribute/objectclass, then schema is not accepted
 *
 */

typedef struct schema_item
{
    int action; /* REPL_SCHEMA_UPDATE_ACCEPT_VALUE or REPL_SCHEMA_UPDATE_REJECT_VALUE */
    char *name_or_oid;
    struct schema_item *next;
} schema_item_t;

typedef struct repl_schema_policy
{
    schema_item_t *objectclasses;
    schema_item_t *attributes;
} repl_schema_policy_t;

struct schema_mods_indexes
{
    int index;
    char *new_value;
    char *old_value;
    struct schema_mods_indexes *next;
};

/*
 * pschemadse is based on the general implementation in dse
 */

static struct dse *pschemadse = NULL;

static void oc_add_nolock(struct objclass *newoc);
static int oc_delete_nolock(char *ocname);
static int oc_replace_nolock(const char *ocname, struct objclass *newoc, char *errorbuf, size_t errorbufsize);
static int oc_check_required(Slapi_PBlock *, Slapi_Entry *, struct objclass *);
static int oc_check_allowed_sv(Slapi_PBlock *, Slapi_Entry *e, const char *type, struct objclass **oclist);
static int schema_delete_objectclasses(Slapi_Entry *entryBefore,
                                       LDAPMod *mod,
                                       char *errorbuf,
                                       size_t errorbufsize,
                                       int schema_ds4x_compat,
                                       int is_internal_operation);
static int schema_delete_attributes(Slapi_Entry *entryBefore,
                                    LDAPMod *mod,
                                    char *errorbuf,
                                    size_t errorbufsize,
                                    int is_internal_operation);
static int schema_add_attribute(Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf, size_t errorbufsize, int schema_ds4x_compat, int is_replicated_operation);
static int schema_add_objectclass(Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf, size_t errorbufsize, int schema_ds4x_compat, int is_replicated_operation);
static int schema_replace_attributes(Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf, size_t errorbufsize, int is_replicated_operation);
static int schema_replace_objectclasses(Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf, size_t errorbufsize, int is_replicated_operation);
static int schema_check_name(char *name, PRBool isAttribute, char *errorbuf, size_t errorbufsize);
static int schema_check_oid(const char *name, const char *oid, PRBool isAttribute, char *errorbuf, size_t errorbufsize);
static int isExtensibleObjectclass(const char *objectclass);
static int strip_oc_options(struct objclass *poc);
static char *stripOption(char *attr);
static int schema_extension_cmp(schemaext *e1, schemaext *e2);
static int put_tagged_oid(char *outp, const char *tag, const char *oid, const char *suffix, int enquote);
static void strcat_oids(char *buf, char *prefix, char **oids, int schema_ds4x_compat);
static size_t strcat_extensions(char *buf, schemaext *extension);
static size_t strlen_null_ok(const char *s);
static int strcpy_count(char *dst, const char *src);
static int refresh_user_defined_schema(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *e, int *returncode, char *returntext, void *arg);
static int schema_check_oc_attrs(struct objclass *poc, char *errorbuf, size_t errorbufsize, int stripOptions);
static struct objclass *oc_find_nolock(const char *ocname_or_oid, struct objclass *oc_private, PRBool use_private);
static struct objclass *oc_find_oid_nolock(const char *ocoid);
static void oc_free(struct objclass **ocp);
static PRBool oc_equal(struct objclass *oc1, struct objclass *oc2);
static PRBool attr_syntax_equal(struct asyntaxinfo *asi1,
                                struct asyntaxinfo *asi2);
static int schema_strcmp(const char *s1, const char *s2);
static int schema_strcmp_array(char **sa1, char **sa2, const char *ignorestr);
static PRBool schema_type_is_interesting(const char *type);
static void schema_create_errormsg(char *errorbuf, size_t errorbufsize, const char *prefix, const char *name, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 5, 6)));
#else
    ;
#endif
static PRBool check_replicated_schema(LDAPMod **mods, char *replica_role, char **attr_name);
static void modify_schema_get_new_definitions(Slapi_PBlock *pb, LDAPMod **mods, struct schema_mods_indexes **at_list, struct schema_mods_indexes **oc_list);
static void modify_schema_apply_new_definitions(char *attr_name, struct schema_mods_indexes *list);
static void modify_schema_free_new_definitions(struct schema_mods_indexes *def_list);
static int schema_oc_compare(struct objclass *oc_1, struct objclass *oc_2, const char *description);
static int schema_at_compare(struct asyntaxinfo *at_1, struct asyntaxinfo *at_2, char *message, int debug_logging);
static int schema_at_superset_check(struct asyntaxinfo *at_list1, struct asyntaxinfo *at_list2, char *message, int replica_role);
static int schema_at_superset_check_syntax_oids(char *oid1, char *oid2);
static int schema_at_superset_check_mr(struct asyntaxinfo *a1, struct asyntaxinfo *a2, char *info);
static int parse_at_str(const char *input, struct asyntaxinfo **asipp, char *errorbuf, size_t errorbufsize, PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, int is_remote);
static int extension_is_user_defined(schemaext *extensions);
static size_t strcat_qdlist(char *buf, char *prefix, char **qdlist);
static int parse_attr_str(const char *input, struct asyntaxinfo **asipp, char *errorbuf, size_t errorbufsize, PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, int is_remote);
static int parse_objclass_str(const char *input, struct objclass **oc, char *errorbuf, size_t errorbufsize, PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, struct objclass *private_schema);

/*
 * Some utility functions for dealing with a dynamic buffer
 */
static struct sizedbuffer *sizedbuffer_construct(size_t size);
static void sizedbuffer_destroy(struct sizedbuffer *p);
static void sizedbuffer_allocate(struct sizedbuffer *p, size_t sizeneeded);

/*
 * Constant strings that we pass to schema_create_errormsg().
 */
static const char *schema_errprefix_oc = "object class %s: ";
static const char *schema_errprefix_at = "attribute type %s: ";
static const char *schema_errprefix_generic = "%s: ";

/* Defined the policies for the replication of the schema */
static repl_schema_policy_t supplier_policy = {0};
static repl_schema_policy_t consumer_policy = {0};
static Slapi_RWLock *schema_policy_lock = NULL;
static int schema_check_policy(int replica_role, int schema_item, char *name, char *oid);
static void schema_load_repl_policy(const char *dn, repl_schema_policy_t *replica);


/*
 * A "cached" copy of the "ignore trailing spaces" config. setting.
 * This is set during initialization only (server restart required for
 * changes to take effect). We do things this way to avoid lock/unlock
 * mutex sequences inside performance critical code.
 */
static int schema_ignore_trailing_spaces =
    SLAPD_DEFAULT_SCHEMA_IGNORE_TRAILING_SPACES;

/* R/W lock used to serialize access to the schema DSE */
static Slapi_RWLock *schema_dse_lock = NULL;

/*
 * The schema_dse_mandatory_init_callonce structure is used by NSPR to ensure
 * that schema_dse_mandatory_init() is called at most once.
 */
static PRCallOnceType schema_dse_mandatory_init_callonce = {0, 0, 0};

static int
parse_at_str(const char *input, struct asyntaxinfo **asipp, char *errorbuf, size_t errorbufsize, PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, int is_remote)
{
    if (asipp) {
        *asipp = NULL;
    }
    return parse_attr_str(input, asipp, errorbuf, errorbufsize, schema_flags, is_user_defined, schema_ds4x_compat, is_remote);
}

static int
parse_oc_str(const char *input, struct objclass **oc, char *errorbuf, size_t errorbufsize, PRUint32 schema_flags,
             int is_user_defined, int schema_ds4x_compat, struct objclass *private_schema)
{
    if (oc) {
        *oc = NULL;
    }
    return parse_objclass_str(input, oc, errorbuf, errorbufsize, schema_flags, is_user_defined, schema_ds4x_compat, private_schema);
}


/* Essential initialization.  Returns PRSuccess if successful */
static PRStatus
schema_dse_mandatory_init(void)
{
    if (NULL == (schema_dse_lock = slapi_new_rwlock())) {
        slapi_log_err(SLAPI_LOG_FATAL, "schema_dse_mandatory_init",
                      "slapi_new_rwlock() for schema DSE lock failed\n");
        return PR_FAILURE;
    }

    schema_ignore_trailing_spaces = config_get_schema_ignore_trailing_spaces();
    return PR_SUCCESS;
}

void
schema_destroy_dse_lock()
{
    if (schema_dse_lock) {
        slapi_destroy_rwlock(schema_dse_lock);
        schema_dse_lock = NULL;
    }
}

void
slapi_schema_get_repl_entries(char **repl_schema_top, char **repl_schema_supplier, char **repl_schema_consumer, char **default_supplier_policy, char **default_consumer_policy)
{
    *repl_schema_top = ENTRY_REPL_SCHEMA_TOP;
    *repl_schema_supplier = ENTRY_REPL_SCHEMA_SUPPLIER;
    *repl_schema_consumer = ENTRY_REPL_SCHEMA_CONSUMER;
    *default_supplier_policy = DEFAULT_SUPPLIER_POLICY;
    *default_consumer_policy = DEFAULT_CONSUMER_POLICY;
}

/* It gets the attributes (see attrName)values in the entry, and add
 * the policies in the provided list
 *
 * Entry: Slapi_entry with DN being ENTRY_REPL_SCHEMA_SUPPLIER or ENTRY_REPL_SCHEMA_CONSUMER
 * attrName: name defining the policy object (objectclass/attribute) and the action
 *         ATTR_SCHEMA_UPDATE_OBJECTCLASS_ACCEPT
 *         ATTR_SCHEMA_UPDATE_OBJECTCLASS_REJECT
 *         ATTR_SCHEMA_UPDATE_ATTRIBUTE_ACCEPT
 *         ATTR_SCHEMA_UPDATE_ATTRIBUTE_REJECT
 * *list: is the list of schema_item_t containing the policies (it can be list of objectclasses or attributes)
 *
 */
static void
schema_policy_add_action(Slapi_Entry *entry, char *attrName, schema_item_t **list)
{
    Slapi_Attr *attr = NULL;
    schema_item_t *schema_item;
    char *value;
    int action;

    /* Retrieve the expected action from the attribute name */
    if ((strcasecmp(attrName, ATTR_SCHEMA_UPDATE_OBJECTCLASS_ACCEPT) == 0) ||
        (strcasecmp(attrName, ATTR_SCHEMA_UPDATE_ATTRIBUTE_ACCEPT) == 0)) {
        action = REPL_SCHEMA_UPDATE_ACCEPT_VALUE;
    } else {
        action = REPL_SCHEMA_UPDATE_REJECT_VALUE;
    }

    /* Retrieve the given attribute from the entry */
    slapi_entry_attr_find(entry, attrName, &attr);
    if (attr != NULL) {
        Slapi_Value *sval = NULL;
        const struct berval *attrVal = NULL;
        int k = slapi_attr_first_value(attr, &sval);

        /* For each value adds the policy in the list */
        while (k != -1) {
            attrVal = slapi_value_get_berval(sval);

            schema_item = (schema_item_t *)slapi_ch_calloc(1, sizeof(schema_item_t));

            /* Get the schema name_or_oid */
            value = (char *)slapi_ch_malloc(attrVal->bv_len + 1);
            memcpy(value, attrVal->bv_val, attrVal->bv_len);
            value[attrVal->bv_len] = '\0';
            schema_item->name_or_oid = value;

            /* Set the action on that item */
            schema_item->action = action;

            /* Add it on the head of the list */
            schema_item->next = *list;
            *list = schema_item;

            /* Get the next name_or_oid */
            k = slapi_attr_next_value(attr, k, &sval);
        }
    }
}

/* Caller must hold schema_policy_lock in write */
static void
schema_load_repl_policy(const char *dn, repl_schema_policy_t *replica)
{
    Slapi_PBlock *pb = NULL;
    Slapi_DN sdn;
    Slapi_Entry *entry = NULL;
    schema_item_t *schema_item, *next;

    if (replica == NULL) {
        return;
    }

    /* Start to free the previous policy */
    /* first the objectclasses policies */
    for (schema_item = replica->objectclasses; schema_item;) {
        slapi_ch_free((void **)&schema_item->name_or_oid);
        next = schema_item->next;
        slapi_ch_free((void **)&schema_item);
        schema_item = next;
    }
    replica->objectclasses = NULL;
    /* second the attributes policies */
    for (schema_item = replica->attributes; schema_item;) {
        slapi_ch_free((void **)&schema_item->name_or_oid);
        next = schema_item->next;
        slapi_ch_free((void **)&schema_item);
        schema_item = next;
    }
    replica->attributes = NULL;

    /* Load the replication policy of the schema  */
    slapi_sdn_init_dn_byref(&sdn, dn);
    if (slapi_search_get_entry(&pb, &sdn, NULL, &entry, plugin_get_default_component_id()) == LDAP_SUCCESS) {
        /* fill the policies (accept/reject) regarding objectclass */
        schema_policy_add_action(entry, ATTR_SCHEMA_UPDATE_OBJECTCLASS_ACCEPT, &replica->objectclasses);
        schema_policy_add_action(entry, ATTR_SCHEMA_UPDATE_OBJECTCLASS_REJECT, &replica->objectclasses);

        /* fill the policies (accept/reject) regarding attribute */
        schema_policy_add_action(entry, ATTR_SCHEMA_UPDATE_ATTRIBUTE_ACCEPT, &replica->attributes);
        schema_policy_add_action(entry, ATTR_SCHEMA_UPDATE_ATTRIBUTE_REJECT, &replica->attributes);
    }
    slapi_search_get_entry_done(&pb);
    slapi_sdn_done(&sdn);
}

/* It load the policies (if they are defined) regarding the replication of the schema
 * depending if the instance behaves as a consumer or a supplier
 * It returns 0 if success
 */
int
slapi_schema_load_repl_policies()
{
    if (schema_policy_lock == NULL) {
        if (NULL == (schema_policy_lock = slapi_new_rwlock())) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_schema_load_repl_policies",
                          "slapi_new_rwlock() for schema replication policy lock failed\n");
            return -1;
        }
    }
    slapi_rwlock_wrlock(schema_policy_lock);

    schema_load_repl_policy((const char *)ENTRY_REPL_SCHEMA_SUPPLIER, &supplier_policy);
    schema_load_repl_policy((const char *)ENTRY_REPL_SCHEMA_CONSUMER, &consumer_policy);

    slapi_rwlock_unlock(schema_policy_lock);

    return 0;
}

/*
 * It checks if the name/oid of the provided schema item (objectclass/attribute)
 * is defined in the schema replication policy.
 * If the replica role is a supplier, it takes the policy from supplier_policy else
 * it takes it from the consumer_policy.
 * Then depending on the schema_item, it takes the objectclasses or attributes policies
 *
 * If it find the name/oid in the policies, it returns
 *      REPL_SCHEMA_UPDATE_ACCEPT_VALUE: This schema item is accepted and can not prevent schema update
 *      REPL_SCHEMA_UPDATE_REJECT_VALUE: This schema item is rejected and prevents the schema update
 *      REPL_SCHEMA_UPDATE_UNKNOWN_VALUE: This schema item as no defined policy
 *
 * Caller must hold schema_policy_lock in read
 */
static int
schema_check_policy(int replica_role, int schema_item, char *name, char *oid)
{
    repl_schema_policy_t *repl_policy;
    schema_item_t *policy;

    /* depending on the role, we take the supplier or the consumer policy */
    if (replica_role == REPL_SCHEMA_AS_SUPPLIER) {
        repl_policy = &supplier_policy;
    } else {
        repl_policy = &consumer_policy;
    }

    /* Now take the correct schema item policy */
    if (schema_item == REPL_SCHEMA_OBJECTCLASS) {
        policy = repl_policy->objectclasses;
    } else {
        policy = repl_policy->attributes;
    }

    /* Try to find the name/oid in the defined policies */
    while (policy) {
        if ((strcasecmp(name, policy->name_or_oid) == 0) || (strcasecmp(oid, policy->name_or_oid) == 0)) {
            return policy->action;
        }
        policy = policy->next;
    }
    return REPL_SCHEMA_UPDATE_UNKNOWN_VALUE;
}


static void
schema_dse_lock_read(void)
{
    if (NULL != schema_dse_lock ||
        PR_SUCCESS == PR_CallOnce(&schema_dse_mandatory_init_callonce,
                                  schema_dse_mandatory_init)) {
        slapi_rwlock_rdlock(schema_dse_lock);
    }
}


static void
schema_dse_lock_write(void)
{
    if (NULL != schema_dse_lock ||
        PR_SUCCESS == PR_CallOnce(&schema_dse_mandatory_init_callonce,
                                  schema_dse_mandatory_init)) {
        slapi_rwlock_wrlock(schema_dse_lock);
    }
}


static void
schema_dse_unlock(void)
{
    if (schema_dse_lock != NULL) {
        slapi_rwlock_unlock(schema_dse_lock);
    }
}


static int
dont_allow_that(Slapi_PBlock *pb __attribute__((unused)),
                Slapi_Entry *entryBefore __attribute__((unused)),
                Slapi_Entry *e __attribute__((unused)),
                int *returncode,
                char *returntext __attribute__((unused)),
                void *arg __attribute__((unused)))
{
    *returncode = LDAP_UNWILLING_TO_PERFORM;
    return SLAPI_DSE_CALLBACK_ERROR;
}


/*
 * slapi_entry_schema_check - check that entry e conforms to the schema
 * required by its object class(es). returns 0 if so, non-zero otherwise.
 * [ the pblock is used to check if this is a replicated operation.
 *   you may pass in NULL if this isn't part of an operation. ]
 * the pblock is also used to return a reason why schema checking failed.
 * it is also used to get schema flags
 * if replicated operations should be checked use slapi_entry_schema_check_ext
 */
int
slapi_entry_schema_check(Slapi_PBlock *pb, Slapi_Entry *e)
{
    return (slapi_entry_schema_check_ext(pb, e, 0));
}

int
slapi_entry_schema_check_ext(Slapi_PBlock *pb, Slapi_Entry *e, int repl_check)
{
    struct objclass **oclist;
    struct objclass *oc;
    const char *ocname;
    Slapi_Attr *a, *aoc;
    Slapi_Value *v;
    int ret = 0;
    int schemacheck = config_get_schemacheck();
    int is_replicated_operation = 0;
    int is_extensible_object = 0;
    int i, oc_count = 0;
    int unknown_class = 0;
    char errtext[BUFSIZ];
    PRUint32 schema_flags = 0;

    /*
     * say the schema checked out ok if we're not checking schema at
     * all, or if this is a replication update.
     */
    if (pb != NULL) {
        slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
        slapi_pblock_get(pb, SLAPI_SCHEMA_FLAGS, &schema_flags);
    }
    if (schemacheck == 0 || (is_replicated_operation && !repl_check)) {
        return (0);
    }

    /* find the object class attribute - could error out here */
    if ((aoc = attrlist_find(e->e_attrs, "objectclass")) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR,
                      "slapi_entry_schema_check_ext", "Entry \"%s\" required attribute \"objectclass\" missing\n",
                      slapi_entry_get_dn_const(e));
        if (pb) {
            PR_snprintf(errtext, sizeof(errtext),
                        "missing required attribute \"objectclass\"\n");
            slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errtext);
        }
        return (1);
    }

    /*
     * Create an array of pointers to the objclass definitions.
     */

    i = slapi_attr_first_value(aoc, &v);
    while (i != -1) {
        oc_count++;
        i = slapi_attr_next_value(aoc, i, &v);
    }

    oclist = (struct objclass **)slapi_ch_malloc((oc_count + 1) * sizeof(struct objclass *));

    /*
     * Need the read lock to create the oc array and while we use it.
     */
    if (!(schema_flags & DSE_SCHEMA_LOCKED)) {
        oc_lock_read();
    }

    oc_count = 0;
    for (i = slapi_attr_first_value(aoc, &v); i != -1; i = slapi_attr_next_value(aoc, i, &v)) {

        ocname = slapi_value_get_string(v);
        if (!ocname) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_entry_schema_check_ext", "Entry \"%s\" \"objectclass\" value missing\n",
                          slapi_entry_get_dn_const(e));
            if (pb) {
                PR_snprintf(errtext, sizeof(errtext),
                            "missing \"objectclass\" value\n");
                slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errtext);
            }
            ret = 1;
            goto out;
        }

        if (isExtensibleObjectclass(ocname)) {
            /*
             *  if the entry is an extensibleObject, just check to see if
             *  the required attributes for whatever other objectclasses the
             *  entry might be are present. All other attributes are allowed
             */
            is_extensible_object = 1;
            continue;
        }

        if ((oc = oc_find_nolock(ocname, NULL, PR_FALSE)) != NULL) {
            oclist[oc_count++] = oc;
        } else {
            /* we don't know about the oc; return an appropriate error message */
            char ebuf[BUFSIZ];
            size_t ocname_len = strlen(ocname);
            const char *extra_msg = "";

            if (ocname_len > 0 && isspace(ocname[ocname_len - 1])) {
                if (ocname_len > 1 && isspace(ocname[ocname_len - 2])) {
                    extra_msg = " (remove the trailing spaces)";
                } else {
                    extra_msg = " (remove the trailing space)";
                }
            }

            slapi_log_err(SLAPI_LOG_ERR,
                          "slapi_entry_schema_check_ext", "Entry \"%s\" has unknown object class \"%s\"%s\n",
                          slapi_entry_get_dn_const(e),
                          escape_string(ocname, ebuf), extra_msg);
            if (pb) {
                PR_snprintf(errtext, sizeof(errtext),
                            "unknown object class \"%s\"%s\n",
                            escape_string(ocname, ebuf), extra_msg);
                slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errtext);
            }
            unknown_class = 1;
        }
    }
    oclist[oc_count] = NULL;

    if (unknown_class) {
        /* failure */
        ret = 1;
        goto out;
    }

    /*
    * go through all the checking so we can log everything
    * wrong with the entry. some day, we might want to return
    * this information to the client as an error message.
    */

    /*
    * check that the entry has required attrs for each oc
    */
    for (i = 0; oclist[i] != NULL; i++) {
        if (oc_check_required(pb, e, oclist[i]) != 0) {
            ret = 1;
            goto out;
        }
    }

    /*
    * check that each attr in the entry is allowed by some oc,
    * and that single-valued attrs only have one value
    */

    {
        Slapi_Attr *prevattr;
        i = slapi_entry_first_attr(e, &a);
        while (-1 != i && 0 == ret) {
            if (is_extensible_object == 0 && unknown_class == 0 && !slapi_attr_flag_is_set(a, SLAPI_ATTR_FLAG_OPATTR)) {
                char *attrtype;
                slapi_attr_get_type(a, &attrtype);
                if (oc_check_allowed_sv(pb, e, attrtype, oclist) != 0) {
                    ret = 1;
                }
            }

            if (slapi_attr_flag_is_set(a, SLAPI_ATTR_FLAG_SINGLE)) {
                if (slapi_valueset_count(&a->a_present_values) > 1) {
                    slapi_log_err(SLAPI_LOG_ERR, "slapi_entry_schema_check_ext", "Entry \"%s\" single-valued attribute \"%s\" has multiple values\n", slapi_entry_get_dn_const(e), a->a_type);
                    if (pb) {
                        PR_snprintf(errtext, sizeof(errtext),
                                    "single-valued attribute \"%s\" has multiple values\n",
                                    a->a_type);
                        slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errtext);
                    }
                    ret = 1;
                }
            }
            prevattr = a;
            i = slapi_entry_next_attr(e, prevattr, &a);
        }
    }

out:
    /* Done with the oc array so can release the lock */
    if (!(schema_flags & DSE_SCHEMA_LOCKED)) {
        oc_unlock();
    }
    slapi_ch_free((void **)&oclist);

    return (ret);
}

static Slapi_Filter_Result
slapi_filter_schema_check_inner(Slapi_Filter *f, slapi_filter_flags flags) {
    /*
     * Default response to Ok. If any more severe things happen we
     * alter this to reflect it. IE we bubble up more severe errors
     * out.
     */
    Slapi_Filter_Result r = FILTER_SCHEMA_SUCCESS;

    switch (f->f_choice) {
    case LDAP_FILTER_EQUALITY:
    case LDAP_FILTER_GE:
    case LDAP_FILTER_LE:
    case LDAP_FILTER_APPROX:
        if (!attr_syntax_exist_by_name_nolock(f->f_avtype)) {
            f->f_flags |= flags;
            r = FILTER_SCHEMA_WARNING;
        }
        break;
    case LDAP_FILTER_PRESENT:
        if (!attr_syntax_exist_by_name_nolock(f->f_type)) {
            f->f_flags |= flags;
            r = FILTER_SCHEMA_WARNING;
        }
        break;
    case LDAP_FILTER_SUBSTRINGS:
        if (!attr_syntax_exist_by_name_nolock(f->f_sub_type)) {
            f->f_flags |= flags;
            r = FILTER_SCHEMA_WARNING;
        }
        break;
    case LDAP_FILTER_EXTENDED:
        /* I don't have any examples of this, so I'm not 100% on how to check it */
        if (!attr_syntax_exist_by_name_nolock(f->f_mr_type)) {
            f->f_flags |= flags;
            r = FILTER_SCHEMA_WARNING;
        }
        break;
    case LDAP_FILTER_AND:
    case LDAP_FILTER_OR:
    case LDAP_FILTER_NOT:
        /* Recurse and check all elemments of the filter */
        for (Slapi_Filter *f_child = f->f_list; f_child != NULL; f_child = f_child->f_next) {
            Slapi_Filter_Result ri = slapi_filter_schema_check_inner(f_child, flags);
            if (ri > r) {
                r = ri;
            }
        }
        break;
    default:
        slapi_log_err(SLAPI_LOG_ERR, "slapi_filter_schema_check_inner",
                      "Unknown type 0x%lX\n", f->f_choice);
        r = FILTER_SCHEMA_FAILURE;
        break;
    }

    return r;
}

/*
 *
 */
Slapi_Filter_Result
slapi_filter_schema_check(Slapi_PBlock *pb, Slapi_Filter *f, Slapi_Filter_Policy fp) {
    if (f == NULL) {
        return FILTER_SCHEMA_FAILURE;
    }

    if (fp == FILTER_POLICY_OFF) {
        return FILTER_SCHEMA_SUCCESS;
    }

    /*
     * There are two possible warning types - it's not up to us to warn into
     * the logs, that's the backends job. So we have to flag a hint into the
     * filter about what it should do. This is why there are two FILTER_INVALID
     * types in filter_flags, one for logging it, and one for actually doing
     * the rejection.
     */
    slapi_filter_flags flags = SLAPI_FILTER_INVALID_ATTR_WARN;
    if (fp == FILTER_POLICY_PROTECT) {
        flags |= SLAPI_FILTER_INVALID_ATTR_UNDEFINE;
    }

    /*
     * Filters are nested, recursive structures, so we actually have to call an inner
     * function until we have a result!
     */
    attr_syntax_read_lock();
    Slapi_Filter_Result r = slapi_filter_schema_check_inner(f, flags);
    attr_syntax_unlock_read();

    /* If any warning occured, ensure we fail it. */
    if (fp == FILTER_POLICY_STRICT && r != FILTER_SCHEMA_SUCCESS) {
        r = FILTER_SCHEMA_FAILURE;
    } else if (fp == FILTER_POLICY_PROTECT && r == FILTER_SCHEMA_WARNING) {
        /* Or, make sure we setup text to warn the user submitting the query */
        slapi_pblock_set_result_text_if_empty(pb, "Invalid attribute in filter - results may not be complete.");
    }
    return r;
}


/*
 * The caller must obtain a read lock first by calling oc_lock_read().
 */
static int
oc_check_required(Slapi_PBlock *pb, Slapi_Entry *e, struct objclass *oc)
{
    int i;
    int rc = 0; /* success, by default */
    Slapi_Attr *a;

    if (oc == NULL || oc->oc_required == NULL || oc->oc_required[0] == NULL) {
        return 0; /* success, as none required  */
    }

    /* for each required attribute */
    for (i = 0; oc->oc_required[i] != NULL; i++) {
        /* see if it's in the entry */
        for (a = e->e_attrs; a != NULL; a = a->a_next) {
            if (slapi_attr_type_cmp(oc->oc_required[i], a->a_type,
                                    SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                break;
            }
        }

        /* not there => schema violation */
        if (a == NULL) {
            char errtext[BUFSIZ];
            slapi_log_err(SLAPI_LOG_ERR,
                          "oc_check_required", "Entry \"%s\" missing attribute \"%s\" required"
                                               " by object class \"%s\"\n",
                          slapi_entry_get_dn_const(e),
                          oc->oc_required[i], oc->oc_name);
            if (pb) {
                PR_snprintf(errtext, sizeof(errtext),
                            "missing attribute \"%s\" required"
                            " by object class \"%s\"\n",
                            oc->oc_required[i], oc->oc_name);
                slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errtext);
            }
            rc = 1; /* failure */
        }
    }

    return rc;
}


/*
 * The caller must obtain a read lock first by calling oc_lock_read().
 */
static int
oc_check_allowed_sv(Slapi_PBlock *pb, Slapi_Entry *e, const char *type, struct objclass **oclist)
{
    struct objclass *oc;
    int i, j;
    int rc = 1; /* failure */

    /* always allow objectclass and entryid attributes */
    /* MFW XXX  THESE SHORTCUTS SHOULD NOT BE NECESSARY BUT THEY MASK
     * MFW XXX  OTHER BUGS IN THE SERVER.
     */
    if (slapi_attr_type_cmp(type, "objectclass", SLAPI_TYPE_CMP_EXACT) == 0) {
        return (0);
    } else if (slapi_attr_type_cmp(type, "entryid", SLAPI_TYPE_CMP_EXACT) == 0) {
        return (0);
    }

    /* check that the type appears as req or opt in at least one oc */
    for (i = 0; rc != 0 && oclist[i] != NULL; i++) {
        oc = oclist[i];

        /* does it require the type? */
        for (j = 0; oc->oc_required && oc->oc_required[j] != NULL; j++) {
            if (slapi_attr_type_cmp(oc->oc_required[j],
                                    type, SLAPI_TYPE_CMP_SUBTYPE) == 0) {
                rc = 0;
                break;
            }
        }

        if (0 != rc) {
            /* does it allow the type? */
            for (j = 0; oc->oc_allowed && oc->oc_allowed[j] != NULL; j++) {
                if (slapi_attr_type_cmp(oc->oc_allowed[j],
                                        type, SLAPI_TYPE_CMP_SUBTYPE) == 0 ||
                    strcmp(oc->oc_allowed[j], "*") == 0) {
                    rc = 0;
                    break;
                }
            }
            /* maybe the next oc allows it */
        }
    }

    if (0 != rc) {
        char errtext[BUFSIZ];
        char ebuf[BUFSIZ];
        slapi_log_err(SLAPI_LOG_ERR,
                      "oc_check_allowed_sv", "Entry \"%s\" -- attribute \"%s\" not allowed\n",
                      slapi_entry_get_dn_const(e), escape_string(type, ebuf));

        if (pb) {
            PR_snprintf(errtext, sizeof(errtext),
                        "attribute \"%s\" not allowed\n",
                        escape_string(type, ebuf));
            slapi_pblock_set(pb, SLAPI_PB_RESULT_TEXT, errtext);
        }
    }

    return rc;
}


/*
 * oc_find_name() will return a strdup'd string or NULL if the objectclass
 * could not be found.
 */
char *
oc_find_name(const char *name_or_oid)
{
    struct objclass *oc;
    char *ocname = NULL;

    oc_lock_read();
    if (NULL != (oc = oc_find_nolock(name_or_oid, NULL, PR_FALSE))) {
        ocname = slapi_ch_strdup(oc->oc_name);
    }
    oc_unlock();

    return ocname;
}


/*
 * oc_find_nolock will return a pointer to the objectclass which has the
 *      same name OR oid.
 * NULL is returned if no match is found or `name_or_oid' is NULL.
 */
static struct objclass *
oc_find_nolock(const char *ocname_or_oid, struct objclass *oc_private, PRBool use_private)
{
    struct objclass *oc;

    if (NULL != ocname_or_oid) {
        if (!schema_ignore_trailing_spaces) {
            if (use_private) {
                oc = oc_private;
            } else {
                oc = g_get_global_oc_nolock();
            }
            for (; oc != NULL; oc = oc->oc_next) {
                if ((strcasecmp(oc->oc_name, ocname_or_oid) == 0) || (oc->oc_oid &&
                                                                      strcasecmp(oc->oc_oid, ocname_or_oid) == 0)) {
                    return (oc);
                }
            }
        } else {
            const char *p;
            size_t len;

            /*
             * Ignore trailing spaces when comparing object class names.
             */
            for (p = ocname_or_oid, len = 0; (*p != '\0') && (*p != ' ');
                 p++, len++) {
                ; /* NULL */
            }

            if (use_private) {
                oc = oc_private;
            } else {
                oc = g_get_global_oc_nolock();
            }
            for (; oc != NULL; oc = oc->oc_next) {
                if (((strncasecmp(oc->oc_name, ocname_or_oid, len) == 0) && (len == strlen(oc->oc_name))) ||
                    (oc->oc_oid &&
                     (strncasecmp(oc->oc_oid, ocname_or_oid, len) == 0) && (len == strlen(oc->oc_oid)))) {
                    return (oc);
                }
            }
        }
    }

    return (NULL);
}

/*
 * oc_find_oid_nolock will return a pointer to the objectclass which has
 *      the same oid.
 * NULL is returned if no match is found or `ocoid' is NULL.
 */
static struct objclass *
oc_find_oid_nolock(const char *ocoid)
{
    struct objclass *oc;

    if (NULL != ocoid) {
        for (oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next) {
            if ((oc->oc_oid &&
                 (strcasecmp(oc->oc_oid, ocoid) == 0))) {
                return (oc);
            }
        }
    }

    return (NULL);
}


/*
    We need to keep the objectclasses in the same order as defined in the ldif files. If not
    SUP dependencies will break. When the user redefines an existing objectclass this code
    makes sure it is put back in the same order it was read to from the ldif file. It also
    verifies that the entries oc_superior value preceeds it in the chain. If not it will not
    allow the entry to be added. This makes sure that the ldif will be written back correctly.
*/

static int
oc_replace_nolock(const char *ocname, struct objclass *newoc, char *errorbuf, size_t errorbufsize)
{
    struct objclass *oc, *pnext;
    int rc = LDAP_SUCCESS;
    PRBool saw_sup = PR_FALSE;

    oc = g_get_global_oc_nolock();

    if (newoc->oc_superior == NULL) {
        saw_sup = PR_TRUE;
    }
    /* don't check SUP dependency for first one because it always/should be top */
    if (strcasecmp(oc->oc_name, ocname) == 0) {
        newoc->oc_next = oc->oc_next;
        g_set_global_oc_nolock(newoc);
        oc_free(&oc);
    } else {
        for (pnext = oc; pnext != NULL;
             oc = pnext, pnext = pnext->oc_next) {
            if (pnext->oc_name == NULL) {
                schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                                       ocname, "Failed to replace objectclass");
                rc = LDAP_OPERATIONS_ERROR;
                break;
            }
            if (newoc->oc_superior != NULL) {
                if (strcasecmp(pnext->oc_name, newoc->oc_superior) == 0) {
                    saw_sup = PR_TRUE;
                }
            }
            if (strcasecmp(pnext->oc_name, ocname) == 0) {
                if (saw_sup) {
                    oc->oc_next = newoc;
                    newoc->oc_next = pnext->oc_next;
                    oc_free(&pnext);
                    break;

                } else {
                    schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                                           ocname, "Can not replace objectclass that already exists");
                    rc = LDAP_TYPE_OR_VALUE_EXISTS;
                    break;
                }
            }
        }
    }
    return rc;
}


static int
oc_delete_nolock(char *ocname)
{
    struct objclass *oc, *pnext;
    int rc = 0; /* failure */

    oc = g_get_global_oc_nolock();

    /* special case if we're removing the first oc */
    if (strcasecmp(oc->oc_name, ocname) == 0) {
        g_set_global_oc_nolock(oc->oc_next);
        oc_free(&oc);
        rc = 1;
    } else {
        for (pnext = oc->oc_next; pnext != NULL;
             oc = pnext, pnext = pnext->oc_next) {
            if (strcasecmp(pnext->oc_name, ocname) == 0) {
                oc->oc_next = pnext->oc_next;
                oc_free(&pnext);
                rc = 1;
                break;
            }
        }
    }

    return rc;
}

static void
oc_delete_all_nolock(void)
{
    struct objclass *oc, *pnext;

    oc = g_get_global_oc_nolock();
    for (pnext = oc->oc_next; oc;
         oc = pnext, pnext = oc ? oc->oc_next : NULL) {
        oc_free(&oc);
    }
    g_set_global_oc_nolock(NULL);
}


/*
 * Compare two objectclass definitions for equality.  Return PR_TRUE if
 * they are equivalent and PR_FALSE if not.
 *
 * The oc_required and oc_allowed arrays are ignored.
 * The string "user defined" is ignored within the origins array.
 * The following flags are ignored:
 *      OC_FLAG_STANDARD_OC
 *      OC_FLAG_USER_OC
 *      OC_FLAG_REDEFINED_OC
 */
static PRBool
oc_equal(struct objclass *oc1, struct objclass *oc2)
{
    PRUint8 flagmask;

    if (schema_strcmp(oc1->oc_name, oc2->oc_name) != 0 || schema_strcmp(oc1->oc_desc, oc2->oc_desc) != 0 || schema_strcmp(oc1->oc_oid, oc2->oc_oid) != 0 || schema_strcmp(oc1->oc_superior, oc2->oc_superior) != 0) {
        return PR_FALSE;
    }

    flagmask = ~(OC_FLAG_STANDARD_OC | OC_FLAG_USER_OC | OC_FLAG_REDEFINED_OC);
    if (oc1->oc_kind != oc2->oc_kind || (oc1->oc_flags & flagmask) != (oc2->oc_flags & flagmask)) {
        return PR_FALSE;
    }

    if (schema_strcmp_array(oc1->oc_orig_required, oc2->oc_orig_required,
                            NULL) != 0 ||
        schema_strcmp_array(oc1->oc_orig_allowed, oc2->oc_orig_allowed,
                            NULL) != 0 ||
        schema_extension_cmp(oc1->oc_extensions, oc2->oc_extensions) != 0) {
        return PR_FALSE;
    }

    return PR_TRUE;
}


#ifdef OC_DEBUG

static int
oc_print(struct objclass *oc)
{
    int i;

    printf("object class %s\n", oc->oc_name);
    if (oc->oc_required != NULL) {
        printf("\trequires %s", oc->oc_required[0]);
        for (i = 1; oc->oc_required[i] != NULL; i++) {
            printf(",%s", oc->oc_required[i]);
        }
        printf("\n");
    }
    if (oc->oc_allowed != NULL) {
        printf("\tallows %s", oc->oc_allowed[0]);
        for (i = 1; oc->oc_allowed[i] != NULL; i++) {
            printf(",%s", oc->oc_allowed[i]);
        }
        printf("\n");
    }
    return 0;
}
#endif

/*
 *  Compare the X-ORIGIN extension, other extensions can be ignored
 */
static int
schema_extension_cmp(schemaext *e1, schemaext *e2)
{
    schemaext *e1_head = e1;
    schemaext *e2_head = e2;
    int found = 0;
    int e1_has_origin = 0;
    int e2_has_origin = 0;
    int i, ii;

    if (e1 == NULL && e2 == NULL) {
        return 0; /* match */
    } else if (e1 == NULL || e2 == NULL) {
        return -1;
    }
    while (e1) {
        if (strcmp(e1->term, "X-ORIGIN")) {
            e1 = e1->next;
            continue;
        }
        e1_has_origin = 1;
        while (e2) {
            if (strcmp(e1->term, e2->term) == 0) {
                e2_has_origin = 1;
                if (e1->values == NULL && e2->values == NULL) {
                    return 0;
                } else if (e1->values == NULL || e2->values == NULL) {
                    return -1;
                }
                for (i = 0; e1->values[i]; i++) {
                    found = 0;
                    for (ii = 0; e2->values[ii]; ii++) {
                        if (strcmp(e1->values[i], e2->values[ii]) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        return -1;
                    }
                }
                /* So far so good, move on to the next check */
                goto next;
            }
            e2 = e2->next;
        }
        e2 = e2_head;
        e1 = e1->next;
    }

    if (e1_has_origin != e2_has_origin) {
        return -1;
    } else if (e1_has_origin == 0 && e2_has_origin == 0) {
        return 0;
    }

next:
    /*
     *  We know that e2 has the same extensions as e1, but does e1 have all the extensions as e2?
     *  Run the compare in reverse...
     */
    found = 0;
    e1 = e1_head;
    e2 = e2_head;

    while (e2) {
        if (strcmp(e2->term, "X-ORIGIN")) {
            e2 = e2->next;
            continue;
        }
        while (e1) {
            if (strcmp(e2->term, e1->term) == 0) {
                if (e2->values == NULL && e1->values == NULL) {
                    return 0;
                } else if (e1->values == NULL || e2->values == NULL) {
                    return -1;
                }
                for (i = 0; e2->values[i]; i++) {
                    found = 0;
                    for (ii = 0; e1->values[ii]; ii++) {
                        if (strcmp(e2->values[i], e1->values[ii]) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        return -1;
                    }
                }
                return 0;
            }
            e1 = e1->next;
        }
        e1 = e1_head;
        e2 = e2->next;
    }

    return 0;
}

/*
 * Compare two attrsyntax definitions for equality.  Return PR_TRUE if
 * they are equivalent and PR_FALSE if not.
 *
 * The string "user defined" is ignored within the origins array.
 * The following flags are ignored:
 *    SLAPI_ATTR_FLAG_STD_ATTR
 *    SLAPI_ATTR_FLAG_NOLOCKING
 *    SLAPI_ATTR_FLAG_OVERRIDE
 */
static PRBool
attr_syntax_equal(struct asyntaxinfo *asi1, struct asyntaxinfo *asi2)
{
    unsigned long flagmask;

    flagmask = ~(SLAPI_ATTR_FLAG_STD_ATTR | SLAPI_ATTR_FLAG_NOLOCKING | SLAPI_ATTR_FLAG_OVERRIDE);

    if (schema_strcmp(asi1->asi_oid, asi2->asi_oid) != 0 || schema_strcmp(asi1->asi_name, asi2->asi_name) != 0 || schema_strcmp(asi1->asi_desc, asi2->asi_desc) != 0 || schema_strcmp(asi1->asi_superior, asi2->asi_superior) != 0 || schema_strcmp(asi1->asi_mr_equality, asi2->asi_mr_equality) != 0 || schema_strcmp(asi1->asi_mr_ordering, asi2->asi_mr_ordering) != 0 || schema_strcmp(asi1->asi_mr_substring, asi2->asi_mr_substring) != 0) {
        return PR_FALSE;
    }

    if (schema_strcmp_array(asi1->asi_aliases, asi2->asi_aliases, NULL) != 0 || schema_extension_cmp(asi1->asi_extensions, asi2->asi_extensions) != 0 || asi1->asi_plugin != asi2->asi_plugin || (asi1->asi_flags & flagmask) != (asi2->asi_flags & flagmask) || asi1->asi_syntaxlength != asi2->asi_syntaxlength) {
        return PR_FALSE;
    }

    return PR_TRUE;
}


/*
 * Like strcmp(), but a NULL string pointer is treated as equivalent to
 * another NULL one and NULL is treated as "less than" all non-NULL values.
 */
static int
schema_strcmp(const char *s1, const char *s2)
{
    if (s1 == NULL) {
        if (s2 == NULL) {
            return 0; /* equal */
        }
        return -1; /* s1 < s2 */
    }

    if (s2 == NULL) {
        return 1; /* s1 > s2 */
    }

    return strcmp(s1, s2);
}


/*
 * Invoke strcmp() on each string in an array.  If one array has fewer elements
 * than the other, it is treated as "less than" the other.  Two NULL or
 * empty arrays (or one NULL and one empty) are considered to be equivalent.
 *
 * If ignorestr is non-NULL, occurrences of that string are ignored.
 */
static int
schema_strcmp_array(char **sa1, char **sa2, const char *ignorestr)
{
    int i1, i2, rc;

    if (sa1 == NULL || *sa1 == NULL) {
        if (sa2 == NULL || *sa2 == NULL) {
            return 0; /* equal */
        }
        return -1; /* sa1 < sa2 */
    }

    if (sa2 == NULL || *sa2 == NULL) {
        return 1; /* sa1 > sa2 */
    }

    rc = 0;
    i1 = i2 = 0;
    while (sa1[i1] != NULL && sa2[i2] != NULL) {
        if (NULL != ignorestr) {
            if (0 == strcmp(sa1[i1], ignorestr)) {
                ++i1;
                continue;
            }
            if (0 == strcmp(sa2[i2], ignorestr)) {
                ++i2;
                continue;
            }
        }
        rc = strcmp(sa1[i1], sa2[i2]);
        ++i1;
        ++i2;
    }

    if (rc == 0) { /* all matched so far */
        /* get rid of trailing ignored strings (if any) */
        if (NULL != ignorestr) {
            if (sa1[i1] != NULL && 0 == strcmp(sa1[i1], ignorestr)) {
                ++i1;
            }
            if (sa2[i2] != NULL && 0 == strcmp(sa2[i2], ignorestr)) {
                ++i2;
            }
        }

        /* check for differing array lengths */
        if (sa2[i2] != NULL) {
            rc = -1; /* sa1 < sa2 -- fewer elements */
        } else if (sa1[i1] != NULL) {
            rc = 1; /* sa1 > sa2 -- more elements */
        }
    }

    return rc;
}


struct attr_enum_wrapper
{
    Slapi_Attr **attrs;
    int enquote_sup_oc;
    struct sizedbuffer *psbAttrTypes;
    int user_defined_only;
    int schema_ds4x_compat;
};

static int
schema_attr_enum_callback(struct asyntaxinfo *asip, void *arg)
{
    struct attr_enum_wrapper *aew = (struct attr_enum_wrapper *)arg;
    int aliaslen = 0;
    struct berval val;
    struct berval *vals[2] = {0, 0};
    const char *attr_desc, *syntaxoid;
    char *outp, syntaxlengthbuf[128];
    int i;

    vals[0] = &val;

    if (!asip) {
        slapi_log_err(SLAPI_LOG_ERR, "schema_attr_enum_callback",
                      "Error: no attribute types in schema_attr_enum_callback\n");
        return ATTR_SYNTAX_ENUM_NEXT;
    }

    if (aew->user_defined_only &&
        (asip->asi_flags & SLAPI_ATTR_FLAG_STD_ATTR)) {
        return ATTR_SYNTAX_ENUM_NEXT; /* not user defined */
    }

    if (aew->schema_ds4x_compat) {
        attr_desc = (asip->asi_flags & SLAPI_ATTR_FLAG_STD_ATTR)
                        ? ATTR_STANDARD_STRING
                        : ATTR_USERDEF_STRING;
    } else {
        attr_desc = asip->asi_desc;
    }

    if (asip->asi_aliases != NULL) {
        for (i = 0; asip->asi_aliases[i] != NULL; ++i) {
            aliaslen += strlen(asip->asi_aliases[i]);
        }
    }

    syntaxoid = asip->asi_plugin->plg_syntax_oid;

    if (!aew->schema_ds4x_compat &&
        asip->asi_syntaxlength != SLAPI_SYNTAXLENGTH_NONE) {
        /* sprintf() is safe because syntaxlengthbuf is large enough */
        sprintf(syntaxlengthbuf, "{%d}", asip->asi_syntaxlength);
    } else {
        *syntaxlengthbuf = '\0';
    }

    /*
     * XXX: 256 is a magic number... it must be big enough to account for
     * all of the fixed sized items we output.
     */
    sizedbuffer_allocate(aew->psbAttrTypes, 256 + strlen(asip->asi_oid) +
                                                strlen(asip->asi_name) +
                                                aliaslen + strlen_null_ok(attr_desc) +
                                                strlen(syntaxoid) +
                                                strlen_null_ok(asip->asi_superior) +
                                                strlen_null_ok(asip->asi_mr_equality) +
                                                strlen_null_ok(asip->asi_mr_ordering) +
                                                strlen_null_ok(asip->asi_mr_substring) +
                                                strcat_extensions(NULL, asip->asi_extensions));

    /*
     * Overall strategy is to maintain a pointer to the next location in
     * the output buffer so we can do simple strcpy's, sprintf's, etc.
     * That pointer is `outp'.  Each item that is output includes a trailing
     * space, so there is no need to include a leading one in the next item.
     */
    outp = aew->psbAttrTypes->buffer;
    outp += sprintf(outp, "( %s NAME ", asip->asi_oid);
    if (asip->asi_aliases == NULL || asip->asi_aliases[0] == NULL) {
        /* only one name */
        outp += sprintf(outp, "'%s' ", asip->asi_name);
    } else {
        /* several names */
        outp += sprintf(outp, "( '%s' ", asip->asi_name);
        for (i = 0; asip->asi_aliases[i] != NULL; ++i) {
            outp += sprintf(outp, "'%s' ", asip->asi_aliases[i]);
        }
        outp += strcpy_count(outp, ") ");
    }

    /* DESC is optional */
    if (attr_desc && *attr_desc) {
        outp += sprintf(outp, "DESC '%s'", attr_desc);
    }
    if (!aew->schema_ds4x_compat &&
        (asip->asi_flags & SLAPI_ATTR_FLAG_OBSOLETE)) {
        outp += strcpy_count(outp, schema_obsolete_with_spaces);
    } else {
        outp += strcpy_count(outp, " ");
    }

    if (!aew->schema_ds4x_compat) {
        /*
         * These values in quotes are not supported by the openldap parser.
         * Even if nsslapd-enquote-sup-oc is on, quotes should not be added.
         */
        outp += put_tagged_oid(outp, "SUP ", asip->asi_superior, NULL, 0);
        outp += put_tagged_oid(outp, "EQUALITY ", asip->asi_mr_equality, NULL, 0);
        outp += put_tagged_oid(outp, "ORDERING ", asip->asi_mr_ordering, NULL, 0);
        outp += put_tagged_oid(outp, "SUBSTR ", asip->asi_mr_substring, NULL, 0);
    }

    outp += put_tagged_oid(outp, "SYNTAX ", syntaxoid, syntaxlengthbuf,
                           aew->enquote_sup_oc);

    if (asip->asi_flags & SLAPI_ATTR_FLAG_SINGLE) {
        outp += strcpy_count(outp, "SINGLE-VALUE ");
    }
    if (!aew->schema_ds4x_compat) {
        if (asip->asi_flags & SLAPI_ATTR_FLAG_COLLECTIVE) {
            outp += strcpy_count(outp, 1 + schema_collective_with_spaces);
        }
        if (asip->asi_flags & SLAPI_ATTR_FLAG_NOUSERMOD) {
            outp += strcpy_count(outp, 1 + schema_nousermod_with_spaces);
        }
        if (asip->asi_flags & SLAPI_ATTR_FLAG_DISTRIBUTED_OPERATION) {
            outp += strcpy_count(outp, "USAGE distributedOperation ");
        } else if (asip->asi_flags & SLAPI_ATTR_FLAG_DSA_OPERATION) {
            outp += strcpy_count(outp, "USAGE dSAOperation ");
        } else if (asip->asi_flags & SLAPI_ATTR_FLAG_OPATTR) {
            outp += strcpy_count(outp, "USAGE directoryOperation ");
        }

        outp += strcat_extensions(outp, asip->asi_extensions);
    }
    outp += strcpy_count(outp, ")");

    val.bv_val = aew->psbAttrTypes->buffer;
    val.bv_len = outp - aew->psbAttrTypes->buffer;
    attrlist_merge(aew->attrs, "attributetypes", vals);

    return ATTR_SYNTAX_ENUM_NEXT;
}


struct syntax_enum_wrapper
{
    Slapi_Attr **attrs;
    struct sizedbuffer *psbSyntaxDescription;
};

static int
schema_syntax_enum_callback(char **names, Slapi_PluginDesc *plugindesc __attribute__((unused)), void *arg)
{
    struct syntax_enum_wrapper *sew = (struct syntax_enum_wrapper *)arg;
    char *oid, *desc;
    int i;
    struct berval val;
    struct berval *vals[2] = {0, 0};
    vals[0] = &val;

    oid = NULL;
    if (names != NULL) {
        for (i = 0; names[i] != NULL; ++i) {
            if (isdigit(names[i][0])) {
                oid = names[i];
                break;
            }
        }
    }

    if (oid == NULL) { /* must have an OID */
        slapi_log_err(SLAPI_LOG_ERR, "schema_syntax_enum_callback ", "Error: no OID found in"
                                                                     " schema_syntax_enum_callback for syntax %s\n",
                      (names == NULL) ? "unknown" : names[0]);
        return 1;
    }

    desc = names[0]; /* by convention, the first name is the "official" one */

    /*
     * RFC 2252 section 4.3.3 Syntax Description says:
     *
     * The following BNF may be used to associate a short description with a
     * syntax OBJECT IDENTIFIER. Implementors should note that future
     * versions of this document may expand this definition to include
     * additional terms.  Terms whose identifier begins with "X-" are
     * reserved for private experiments, and MUST be followed by a
     * <qdstrings>.
     *
     * SyntaxDescription = "(" whsp
     *     numericoid whsp
     *     [ "DESC" qdstring ]
     *     whsp ")"
     *
     * And section 5.3.1 ldapSyntaxes says:
     *
     * Servers MAY use this attribute to list the syntaxes which are
     * implemented.  Each value corresponds to one syntax.
     *
     *  ( 1.3.6.1.4.1.1466.101.120.16 NAME 'ldapSyntaxes'
     *    EQUALITY objectIdentifierFirstComponentMatch
     *    SYNTAX 1.3.6.1.4.1.1466.115.121.1.54 USAGE directoryOperation )
     */
    if (desc == NULL) {
        /* allocate enough room for "(  )" and '\0' at end */
        sizedbuffer_allocate(sew->psbSyntaxDescription, strlen(oid) + 5);
        sprintf(sew->psbSyntaxDescription->buffer, "( %s )", oid);
    } else {
        /* allocate enough room for "(  ) DESC '' " and '\0' at end */
        sizedbuffer_allocate(sew->psbSyntaxDescription,
                             strlen(oid) + strlen(desc) + 13);
        sprintf(sew->psbSyntaxDescription->buffer, "( %s DESC '%s' )",
                oid, desc);
    }

    val.bv_val = sew->psbSyntaxDescription->buffer;
    val.bv_len = strlen(sew->psbSyntaxDescription->buffer);
    attrlist_merge(sew->attrs, "ldapSyntaxes", vals);

    return 1;
}

struct listargs
{
    char **attrs;
    unsigned long flag;
};

static int
schema_list_attributes_callback(struct asyntaxinfo *asi, void *arg)
{
    struct listargs *aew = (struct listargs *)arg;

    if (!asi) {
        slapi_log_err(SLAPI_LOG_ERR, "schema_list_attributes_callback",
                      "Error: no attribute types in schema_list_attributes_callback\n");
        return ATTR_SYNTAX_ENUM_NEXT;
    }
    if (aew->flag && (asi->asi_flags & aew->flag)) {
#if defined(USE_OLD_UNHASHED)
        /* skip unhashed password */
        if (!is_type_forbidden(asi->asi_name)) {
#endif
            charray_add(&aew->attrs, slapi_ch_strdup(asi->asi_name));
            if (NULL != asi->asi_aliases) {
                int i;

                for (i = 0; asi->asi_aliases[i] != NULL; ++i) {
                    charray_add(&aew->attrs,
                                slapi_ch_strdup(asi->asi_aliases[i]));
                }
            }
#if defined(USE_OLD_UNHASHED)
        }
#endif
    }
    return ATTR_SYNTAX_ENUM_NEXT;
}

/* Return the list of attributes names matching attribute flags */
char **
slapi_schema_list_attribute_names(unsigned long flag)
{
    struct listargs aew = {0};
    aew.flag = flag;

    attr_syntax_enumerate_attrs(schema_list_attributes_callback, &aew,
                                PR_FALSE);
    return aew.attrs;
}


/*
 * returntext is always at least SLAPI_DSE_RETURNTEXT_SIZE bytes in size.
 */
int
read_schema_dse(
    Slapi_PBlock *pb,
    Slapi_Entry *pschema_info_e,
    Slapi_Entry *entryAfter __attribute__((unused)),
    int *returncode,
    char *returntext __attribute__((unused)),
    void *arg __attribute__((unused)))
{
    struct berval val;
    struct berval *vals[2];
    struct objclass *oc;
    struct matchingRuleList *mrl = NULL;
    struct sizedbuffer *psbObjectClasses = sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbAttrTypes = sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbMatchingRule = sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbSyntaxDescription = sizedbuffer_construct(BUFSIZ);
    struct attr_enum_wrapper aew;
    struct syntax_enum_wrapper sew;
    const CSN *csn;
    char *mr_desc, *mr_name, *oc_description;
    char **allowed, **required;
    PRUint32 schema_flags = 0;
    int enquote_sup_oc = config_get_enquote_sup_oc();
    int schema_ds4x_compat = config_get_ds4_compatible_schema();
    int user_defined_only = 0;
    int i;

    vals[0] = &val;
    vals[1] = NULL;

    slapi_pblock_get(pb, SLAPI_SCHEMA_FLAGS, (void *)&schema_flags);
    user_defined_only = (schema_flags & DSE_SCHEMA_USER_DEFINED_ONLY) ? 1 : 0;

    attrlist_delete(&pschema_info_e->e_attrs, "objectclasses");
    attrlist_delete(&pschema_info_e->e_attrs, "attributetypes");
    attrlist_delete(&pschema_info_e->e_attrs, "matchingRules");
    attrlist_delete(&pschema_info_e->e_attrs, "ldapSyntaxes");
    /*
     * attrlist_delete (&pschema_info_e->e_attrs, "matchingRuleUse");
     */

    schema_dse_lock_read();
    oc_lock_read();

    /* return the objectclasses */
    for (oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next) {
        size_t size = 0;
        int need_extra_space = 1;

        if (user_defined_only &&
            !((oc->oc_flags & OC_FLAG_USER_OC) ||
              (oc->oc_flags & OC_FLAG_REDEFINED_OC))) {
            continue;
        }
        /*
         * XXX: 256 is a magic number... it must be large enough to fit
         * all of the fixed size items including description (DESC),
         * kind (STRUCTURAL, AUXILIARY, or ABSTRACT), and the OBSOLETE flag.
         */
        if (schema_ds4x_compat) {
            oc_description = (oc->oc_flags & OC_FLAG_STANDARD_OC) ? OC_STANDARD_STRING : OC_USERDEF_STRING;
        } else {
            oc_description = oc->oc_desc;
        }
        size = 256 + strlen_null_ok(oc->oc_oid) + strlen(oc->oc_name) +
               strlen_null_ok(oc_description) + strcat_extensions(NULL, oc->oc_extensions);
        required = schema_ds4x_compat ? oc->oc_required : oc->oc_orig_required;
        if (required && required[0]) {
            for (i = 0; required[i]; i++)
                size += 16 + strlen(required[i]);
        }
        allowed = schema_ds4x_compat ? oc->oc_allowed : oc->oc_orig_allowed;
        if (allowed && allowed[0]) {
            for (i = 0; allowed[i]; i++)
                size += 16 + strlen(allowed[i]);
        }
        sizedbuffer_allocate(psbObjectClasses, size);
        /* put the OID and the NAME */
        sprintf(psbObjectClasses->buffer, "( %s NAME '%s'", (oc->oc_oid) ? oc->oc_oid : "", oc->oc_name);
        /* The DESC (description) is OPTIONAL */
        if (oc_description) {
            strcat(psbObjectClasses->buffer, " DESC '");
            /*
             * We want to list an empty description
             * element if it was defined that way.
             */
            if (*oc_description) {
                strcat(psbObjectClasses->buffer, oc_description);
            }
            strcat(psbObjectClasses->buffer, "'");
            need_extra_space = 1;
        }
        /* put the OBSOLETE keyword */
        if (!schema_ds4x_compat && (oc->oc_flags & OC_FLAG_OBSOLETE)) {
            strcat(psbObjectClasses->buffer, schema_obsolete_with_spaces);
            need_extra_space = 0;
        }
        /* put the SUP superior objectclass */
        if (0 != strcasecmp(oc->oc_name, "top")) { /* top has no SUP */
            /*
             * Some AUXILIARY AND ABSTRACT objectclasses may not have a SUP either
             * for compatability, every objectclass other than top must have a SUP
             */
            if (schema_ds4x_compat || (oc->oc_superior && *oc->oc_superior)) {
                if (need_extra_space) {
                    strcat(psbObjectClasses->buffer, " ");
                }
                strcat(psbObjectClasses->buffer, "SUP ");
                strcat(psbObjectClasses->buffer, (enquote_sup_oc ? "'" : ""));
                strcat(psbObjectClasses->buffer, ((oc->oc_superior && *oc->oc_superior) ? oc->oc_superior : "top"));
                strcat(psbObjectClasses->buffer, (enquote_sup_oc ? "'" : ""));
                need_extra_space = 1;
            }
        }
        /* put the kind of objectclass */
        if (schema_ds4x_compat) {
            if (need_extra_space) {
                strcat(psbObjectClasses->buffer, " ");
            }
        } else {

            strcat(psbObjectClasses->buffer, schema_oc_kind_strings_with_spaces[oc->oc_kind]);
        }
        strcat_oids(psbObjectClasses->buffer, "MUST", required, schema_ds4x_compat);
        strcat_oids(psbObjectClasses->buffer, "MAY", allowed, schema_ds4x_compat);
        if (!schema_ds4x_compat) {
            strcat_extensions(psbObjectClasses->buffer, oc->oc_extensions);
        }
        strcat(psbObjectClasses->buffer, ")");
        val.bv_val = psbObjectClasses->buffer;
        val.bv_len = strlen(psbObjectClasses->buffer);
        attrlist_merge(&pschema_info_e->e_attrs, "objectclasses", vals);
    }

    oc_unlock();

    /* now return the attrs */
    aew.attrs = &pschema_info_e->e_attrs;
    aew.enquote_sup_oc = enquote_sup_oc;
    aew.psbAttrTypes = psbAttrTypes;
    aew.user_defined_only = user_defined_only;
    aew.schema_ds4x_compat = schema_ds4x_compat;
    attr_syntax_enumerate_attrs(schema_attr_enum_callback, &aew, PR_FALSE);

    /* return the set of matching rules we support */
    for (mrl = g_get_global_mrl(); !user_defined_only && mrl != NULL; mrl = mrl->mrl_next) {
        mr_name = mrl->mr_entry->mr_name ? mrl->mr_entry->mr_name : "";
        mr_desc = mrl->mr_entry->mr_desc ? mrl->mr_entry->mr_desc : "";
        sizedbuffer_allocate(psbMatchingRule, 128 + strlen_null_ok(mrl->mr_entry->mr_oid) +
                                                  strlen(mr_name) + strlen(mr_desc) + strlen_null_ok(mrl->mr_entry->mr_syntax));
        if (schema_ds4x_compat) {
            sprintf(psbMatchingRule->buffer,
                    "( %s NAME '%s' DESC '%s' SYNTAX %s%s%s )",
                    (mrl->mr_entry->mr_oid ? mrl->mr_entry->mr_oid : ""),
                    mr_name, mr_desc, enquote_sup_oc ? "'" : "",
                    mrl->mr_entry->mr_syntax ? mrl->mr_entry->mr_syntax : "",
                    enquote_sup_oc ? "'" : "");
        } else if (NULL != mrl->mr_entry->mr_oid &&
                   NULL != mrl->mr_entry->mr_syntax) {
            char *p;

            sprintf(psbMatchingRule->buffer, "( %s ", mrl->mr_entry->mr_oid);
            p = psbMatchingRule->buffer + strlen(psbMatchingRule->buffer);
            if (*mr_name != '\0') {
                sprintf(p, "NAME '%s' ", mr_name);
                p += strlen(p);
            }
            if (*mr_desc != '\0') {
                sprintf(p, "DESC '%s' ", mr_desc);
                p += strlen(p);
            }
            sprintf(p, "SYNTAX %s )", mrl->mr_entry->mr_syntax);
        }
        val.bv_val = psbMatchingRule->buffer;
        val.bv_len = strlen(psbMatchingRule->buffer);
        attrlist_merge(&pschema_info_e->e_attrs, "matchingRules", vals);
    }
    if (!schema_ds4x_compat && !user_defined_only) {
        /* return the set of syntaxes we support */
        sew.attrs = &pschema_info_e->e_attrs;
        sew.psbSyntaxDescription = psbSyntaxDescription;
        plugin_syntax_enumerate(schema_syntax_enum_callback, &sew);
    }
    csn = g_get_global_schema_csn();
    if (NULL != csn) {
        char csn_str[CSN_STRSIZE + 1];

        csn_as_string(csn, PR_FALSE, csn_str);
        slapi_entry_attr_delete(pschema_info_e, "nsschemacsn");
        slapi_entry_add_string(pschema_info_e, "nsschemacsn", csn_str);
    }

    schema_dse_unlock();

    sizedbuffer_destroy(psbObjectClasses);
    sizedbuffer_destroy(psbAttrTypes);
    sizedbuffer_destroy(psbMatchingRule);
    sizedbuffer_destroy(psbSyntaxDescription);
    *returncode = LDAP_SUCCESS;

    return SLAPI_DSE_CALLBACK_OK;
}

/* helper for deleting mods (we do not want to be applied) from the mods array */
static void
mod_free(LDAPMod *mod)
{
    ber_bvecfree(mod->mod_bvalues);
    slapi_ch_free((void **)&(mod->mod_type));
    slapi_ch_free((void **)&mod);
}

/*
 * modify_schema_dse: called by do_modify() when target is cn=schema
 *
 * Add/Delete attributes and objectclasses from the schema
 * Supported mod_ops are LDAP_MOD_DELETE and LDAP_MOD_ADD
 *
 * Note that the in-memory DSE Slapi_Entry object does NOT hold the
 * attributeTypes and objectClasses attributes -- it only holds
 * non-schema related attributes such as aci.
 *
 * returntext is always at least SLAPI_DSE_RETURNTEXT_SIZE bytes in size.
 */
int
modify_schema_dse(Slapi_PBlock *pb, Slapi_Entry *entryBefore, Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg __attribute__((unused)))
{
    int i, rc = SLAPI_DSE_CALLBACK_OK; /* default is to apply changes to the DSE */
    char *schema_dse_attr_name;
    LDAPMod **mods = NULL;
    int num_mods = 0; /* count the number of mods */
    int schema_ds4x_compat = config_get_ds4_compatible_schema();
    int schema_modify_enabled = config_get_schemamod();
    int reapply_mods = 0;
    int is_replicated_operation = 0;
    int is_internal_operation = 0;
    Slapi_Operation *operation = NULL;

    if (!schema_modify_enabled) {
        *returncode = LDAP_UNWILLING_TO_PERFORM;
        schema_create_errormsg(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                               schema_errprefix_generic, "Generic",
                               "schema update is disabled");
        return (SLAPI_DSE_CALLBACK_ERROR);
    }

    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
    slapi_pblock_get(pb, SLAPI_OPERATION, &operation);
    if (NULL == operation) {
        *returncode = LDAP_OPERATIONS_ERROR;
        schema_create_errormsg(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                               schema_errprefix_generic, "Generic",
                               "operation is null");
        return (SLAPI_DSE_CALLBACK_ERROR);
    }

    is_internal_operation = slapi_operation_is_flag_set(operation, SLAPI_OP_FLAG_INTERNAL);

    /* In case we receive a schema from a supplier, check if we can accept it
   * (it is a superset of our own schema).
   * If it is not a superset, pick up what could extend our schema and return
   */
    if (is_replicated_operation) {
        char *attr_name = NULL;
        struct schema_mods_indexes *at_list = NULL;
        struct schema_mods_indexes *oc_list = NULL;

        if (!check_replicated_schema(mods, OC_CONSUMER, &attr_name)) {

            /* we will refuse to apply this schema
                   * Try to capture in it what would extends our own schema
                   */
            modify_schema_get_new_definitions(pb, mods, &at_list, &oc_list);
            if (at_list) {
                modify_schema_apply_new_definitions("attributetypes", at_list);
            }
            if (oc_list) {
                modify_schema_apply_new_definitions("objectclasses", oc_list);
            }
            /* No need to hold the lock for these list that are local */
            modify_schema_free_new_definitions(at_list);
            modify_schema_free_new_definitions(oc_list);
            /* now return, we will not apply that schema */
            schema_create_errormsg(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                   schema_errprefix_generic, attr_name,
                                   "Replace is not possible, local consumer schema is a superset of the supplier");
            slapi_log_err(SLAPI_LOG_ERR, "modify_schema_dse",
                          "[C] Local %s must not be overwritten (set replication log for additional info)\n",
                          attr_name);
            /*
                   * If the update (replicated) of the schema is rejected then
                   * process_postop->ignore_error_and_keep_going will decide if
                   * this failure is fatal or can be ignored.
                   * LDAP_UNWILLING_TO_PERFORM is considered as fatal error --> close the connection
                   *
                   * A 6.x supplier may send a subset schema and trigger this error, that
                   * will break the replication session.
                   *
                   * With new "learning" mechanism this is not that important if the
                   * update of the schema is successful or not. Just be permissive
                   * ignoring that failure to let the full replication session going on
                   * So return LDAP_CONSTRAINT_VIOLATION (in place of LDAP_UNWILLING_TO_PERFORM)
                   * is pick up as best choice of non fatal returncode.
                   * (others better choices UNWILLING_TO_PERFORM, OPERATION_ERROR or ldap_error
                   * are unfortunately all fatal).
                   */
            *returncode = LDAP_CONSTRAINT_VIOLATION;
            return (SLAPI_DSE_CALLBACK_ERROR);
        }
    }


    schema_dse_lock_write();

    /*
   * Process each modification.  Stop as soon as we hit an error.
   *
   * XXXmcs: known bugs: we don't operate on a copy of the schema, so it
   * is possible for some schema changes to be made but not all of them.
   * True for DS 4.x as well, although it tried to keep going even after
   * an error was detected (which was very wrong).
   */
    for (i = 0; rc == SLAPI_DSE_CALLBACK_OK && mods && mods[i]; i++) {
        schema_dse_attr_name = (char *)mods[i]->mod_type;
        num_mods++; /* incr the number of mods */

        /*
     * skip attribute types that we do not recognize (the DSE code will
     * handle them).
     */
        if (!schema_type_is_interesting(schema_dse_attr_name)) {
            continue;
        }

        /*
     * Delete an objectclass or attribute
     */
        if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
            if (strcasecmp(mods[i]->mod_type, "objectclasses") == 0) {
                *returncode = schema_delete_objectclasses(entryBefore, mods[i],
                                                          returntext, SLAPI_DSE_RETURNTEXT_SIZE, schema_ds4x_compat, is_internal_operation);
            } else if (strcasecmp(mods[i]->mod_type, "attributetypes") == 0) {
                *returncode = schema_delete_attributes(entryBefore, mods[i],
                                                       returntext, SLAPI_DSE_RETURNTEXT_SIZE, is_internal_operation);
            } else {
                *returncode = LDAP_NO_SUCH_ATTRIBUTE;
                schema_create_errormsg(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                       schema_errprefix_generic, mods[i]->mod_type,
                                       "Only object classes and attribute types may be deleted");
            }

            if (LDAP_SUCCESS != *returncode) {
                rc = SLAPI_DSE_CALLBACK_ERROR;
            } else {
                reapply_mods = 1;
            }
        }

        /*
     * Replace an objectclass,attribute, or schema CSN
     */
        else if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)) {
            int replace_allowed = 0;
            slapdFrontendConfig_t *slapdFrontendConfig;

            slapdFrontendConfig = getFrontendConfig();
            CFG_LOCK_READ(slapdFrontendConfig);
            if (0 == strcasecmp(slapdFrontendConfig->schemareplace,
                                CONFIG_SCHEMAREPLACE_STR_ON)) {
                replace_allowed = 1;
            } else if (0 == strcasecmp(slapdFrontendConfig->schemareplace,
                                       CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY)) {
                replace_allowed = is_replicated_operation;
            }
            CFG_UNLOCK_READ(slapdFrontendConfig);

            if (!replace_allowed) {
                *returncode = LDAP_UNWILLING_TO_PERFORM;
                schema_create_errormsg(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                       schema_errprefix_generic, mods[i]->mod_type,
                                       "Replace is not allowed on the subschema subentry");
                slapi_log_err(SLAPI_LOG_REPL, "modify_schema_dse",
                              "Replace is not allowed on the subschema subentry\n");
                rc = SLAPI_DSE_CALLBACK_ERROR;
            } else {
                if (strcasecmp(mods[i]->mod_type, "attributetypes") == 0) {
                    /*
                     * Replace all attributetypes
                     * It has already been checked that if it was a replicated schema
                     * it is a superset of the current schema. That is fine to apply the mods
                     */
                    *returncode = schema_replace_attributes(pb, mods[i], returntext, SLAPI_DSE_RETURNTEXT_SIZE, is_replicated_operation);
                } else if (strcasecmp(mods[i]->mod_type, "objectclasses") == 0) {
                    /*
                     * Replace all objectclasses
                     * It has already been checked that if it was a replicated schema
                     * it is a superset of the current schema. That is fine to apply the mods
                     */
                    *returncode = schema_replace_objectclasses(pb, mods[i], returntext, SLAPI_DSE_RETURNTEXT_SIZE, is_replicated_operation);
                } else if (strcasecmp(mods[i]->mod_type, "nsschemacsn") == 0) {
                    if (is_replicated_operation) {
                        /* Update the schema CSN */
                        if (mods[i]->mod_bvalues && mods[i]->mod_bvalues[0] &&
                            mods[i]->mod_bvalues[0]->bv_val &&
                            mods[i]->mod_bvalues[0]->bv_len > 0) {
                            char new_csn_string[CSN_STRSIZE + 1];
                            CSN *new_schema_csn;
                            memcpy(new_csn_string, mods[i]->mod_bvalues[0]->bv_val,
                                   mods[i]->mod_bvalues[0]->bv_len);
                            new_csn_string[mods[i]->mod_bvalues[0]->bv_len] = '\0';
                            new_schema_csn = csn_new_by_string(new_csn_string);
                            if (NULL != new_schema_csn) {
                                g_set_global_schema_csn(new_schema_csn); /* csn is consumed */
                            }
                        }
                    }
                } else {
                    *returncode = LDAP_UNWILLING_TO_PERFORM; /* XXXmcs: best error? */
                    schema_create_errormsg(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                           schema_errprefix_generic, mods[i]->mod_type,
                                           "Only object classes and attribute types may be replaced");
                }
            }

            if (LDAP_SUCCESS != *returncode) {
                rc = SLAPI_DSE_CALLBACK_ERROR;
            } else {
                reapply_mods = 1; /* we have at least some modifications we need to reapply */
            }
        }

        /*
         * Add an objectclass or attribute
         */
        else if (SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
            if (strcasecmp(mods[i]->mod_type, "attributetypes") == 0) {
                /*
                 * Add a new attribute
                 */
                *returncode = schema_add_attribute(pb, mods[i], returntext,
                                                   SLAPI_DSE_RETURNTEXT_SIZE,
                                                   schema_ds4x_compat,
                                                   is_replicated_operation);
            } else if (strcasecmp(mods[i]->mod_type, "objectclasses") == 0) {
                /*
                 * Add a new objectclass
                 */
                *returncode = schema_add_objectclass(pb, mods[i], returntext,
                                                     SLAPI_DSE_RETURNTEXT_SIZE,
                                                     schema_ds4x_compat,
                                                     is_replicated_operation);
            } else {
                if (schema_ds4x_compat) {
                    *returncode = LDAP_NO_SUCH_ATTRIBUTE;
                } else {
                    *returncode = LDAP_UNWILLING_TO_PERFORM; /* XXXmcs: best error? */
                }
                schema_create_errormsg(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                       schema_errprefix_generic, mods[i]->mod_type,
                                       "Only object classes and attribute types may be added");
            }

            if (LDAP_SUCCESS != *returncode) {
                rc = SLAPI_DSE_CALLBACK_ERROR;
            } else {
                reapply_mods = 1; /* we have at least some modifications we need to reapply */
            }
        }

        /*
    ** No value was specified to modify, the user probably tried
    ** to delete all attributetypes or all objectclasses, which
    ** isn't allowed
    */
        if (!mods[i]->mod_vals.modv_strvals) {
            if (schema_ds4x_compat) {
                *returncode = LDAP_INVALID_SYNTAX;
            } else {
                *returncode = LDAP_UNWILLING_TO_PERFORM; /* XXXmcs: best error? */
            }
            schema_create_errormsg(returntext, SLAPI_DSE_RETURNTEXT_SIZE,
                                   schema_errprefix_generic, mods[i]->mod_type,
                                   "No target attribute type or object class specified");
            rc = SLAPI_DSE_CALLBACK_ERROR;
        }
    }

    if (rc == SLAPI_DSE_CALLBACK_OK && reapply_mods) {
        CSN *new_schema_csn;
        int newindex = 0; /* mods array index */

        /* tell the "unholy" dse_modify code to reapply the mods and use
       that result instead of the initial result; we must remove the attributes
       we manage in this code from the mods
    */
        slapi_pblock_set(pb, SLAPI_DSE_REAPPLY_MODS, (void *)&reapply_mods);

        /* because we are reapplying the mods, we want the entryAfter to
       look just like the entryBefore, except that "our" attributes
       will have been removed
    */
        /* delete the mods from the mods array */
        for (i = 0; i < num_mods; i++) {
            const char *attrname = mods[i]->mod_type;

            /* delete this attr from the entry */
            slapi_entry_attr_delete(entryAfter, attrname);

            if (schema_type_is_interesting(attrname)) {
                mod_free(mods[i]);
                mods[i] = NULL;
            } else {
                /* add the original value of the attr back to the entry after */
                Slapi_Attr *origattr = NULL;
                Slapi_ValueSet *origvalues = NULL;
                slapi_entry_attr_find(entryBefore, attrname, &origattr);
                if (NULL != origattr) {
                    slapi_attr_get_valueset(origattr, &origvalues);
                    if (NULL != origvalues) {
                        slapi_entry_add_valueset(entryAfter, attrname, origvalues);
                        slapi_valueset_free(origvalues);
                    }
                }
                mods[newindex++] = mods[i];
            }
        }
        mods[newindex] = NULL;

        /*
     * Since we successfully updated the schema, we need to generate
     * a new schema CSN for non-replicated operations.
     */
        /* XXXmcs: I wonder if we should update the schema CSN even when no
     * attribute types or OCs were changed?  That way, an administrator
     * could force schema replication to occur by submitting a modify
     * operation that did not really do anything, such as:
     *
     * dn:cn=schema
     * changetype:modify
     * replace:cn
     * cn:schema
     */
        if (!is_replicated_operation) {
            new_schema_csn = csn_new();
            if (NULL != new_schema_csn) {
                char csn_str[CSN_STRSIZE + 1];
                csn_set_replicaid(new_schema_csn, 0);
                csn_set_time(new_schema_csn, slapi_current_utc_time());
                g_set_global_schema_csn(new_schema_csn);
                slapi_entry_attr_delete(entryBefore, "nsschemacsn");
                csn_as_string(new_schema_csn, PR_FALSE, csn_str);
                slapi_entry_add_string(entryBefore, "nsschemacsn", csn_str);
            }
        }
    }

    schema_dse_unlock();

    return rc;
}

CSN *
dup_global_schema_csn()
{
    CSN *schema_csn;

    schema_dse_lock_read();
    schema_csn = csn_dup(g_get_global_schema_csn());
    schema_dse_unlock();
    return schema_csn;
}

/*
 * Remove all attribute types and objectclasses from the entry and
 * then add back the user defined ones based on the contents of the
 * schema hash tables.
 *
 * Returns SLAPI_DSE_CALLBACK_OK is all goes well.
 *
 * returntext is always at least SLAPI_DSE_RETURNTEXT_SIZE bytes in size.
 */
static int
refresh_user_defined_schema(Slapi_PBlock *pb,
                            Slapi_Entry *pschema_info_e,
                            Slapi_Entry *entryAfter __attribute__((unused)),
                            int *returncode,
                            char *returntext,
                            void *arg __attribute__((unused)))
{
    int rc;
    Slapi_PBlock *mypbptr = pb;
    Slapi_PBlock *mypb = NULL;
    const CSN *schema_csn;
    PRUint32 schema_flags = DSE_SCHEMA_USER_DEFINED_ONLY;

    slapi_entry_attr_delete(pschema_info_e, "objectclasses");
    slapi_entry_attr_delete(pschema_info_e, "attributetypes");

    /* for write callbacks, no pb is supplied, so use our own */
    if (!mypbptr) {
        mypb = slapi_pblock_new();
        mypbptr = mypb;
    }

    slapi_pblock_set(mypbptr, SLAPI_SCHEMA_FLAGS, &schema_flags);
    rc = read_schema_dse(mypbptr, pschema_info_e, NULL, returncode, returntext, NULL);
    schema_csn = g_get_global_schema_csn();
    if (NULL != schema_csn) {
        char csn_str[CSN_STRSIZE + 1];
        slapi_entry_attr_delete(pschema_info_e, "nsschemacsn");
        csn_as_string(schema_csn, PR_FALSE, csn_str);
        slapi_entry_add_string(pschema_info_e, "nsschemacsn", csn_str);
    }
    if (mypb != NULL) {
        slapi_pblock_destroy(mypb);
    }
    return rc;
}


/*  oc_add_nolock
 *  Add the objectClass newoc to the global list of objectclasses
 */
static void
oc_add_nolock(struct objclass *newoc)
{
    struct objclass *poc;

    poc = g_get_global_oc_nolock();

    if (NULL == poc) {
        g_set_global_oc_nolock(newoc);
    } else {
        for (; (poc != NULL) && (poc->oc_next != NULL); poc = poc->oc_next) {
            ;
        }
        poc->oc_next = newoc;
        newoc->oc_next = NULL;
    }
}

/*
 * Delete one or more objectClasses from our internal data structure.
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 * If an error occurs, explanatory text is copied into 'errorbuf'.
 *
 * This function should not send an LDAP result; that is the caller's
 * responsibility.
 */
static int
schema_delete_objectclasses(Slapi_Entry *entryBefore __attribute__((unused)),
                            LDAPMod *mod,
                            char *errorbuf,
                            size_t errorbufsize,
                            int schema_ds4x_compat,
                            int is_internal_operation)
{
    size_t i;
    int rc = LDAP_SUCCESS; /* optimistic */
    struct objclass *poc, *poc2, *delete_oc = NULL;

    if (NULL == mod->mod_bvalues) {
        if (is_internal_operation) {
            slapi_log_err(SLAPI_LOG_REPL, "schema_delete_objectclasses",
                          "schema_delete_objectclasses: Remove all objectclass in Internal op\n");
        } else {
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                                   NULL, "Cannot remove all schema object classes");
            return LDAP_UNWILLING_TO_PERFORM;
        }
    }

    for (i = 0; mod->mod_bvalues && mod->mod_bvalues[i]; i++) {
        if (LDAP_SUCCESS != (rc = parse_oc_str(
                                 (const char *)mod->mod_bvalues[i]->bv_val, &delete_oc,
                                 errorbuf, errorbufsize, 0, 0, schema_ds4x_compat, NULL))) {
            return rc;
        }

        oc_lock_write();

        if ((poc = oc_find_nolock(delete_oc->oc_name, NULL, PR_FALSE)) != NULL) {

            /* check to see if any objectclasses inherit from this oc */
            for (poc2 = g_get_global_oc_nolock(); poc2 != NULL; poc2 = poc2->oc_next) {
                if (poc2->oc_superior &&
                    (strcasecmp(poc2->oc_superior, delete_oc->oc_name) == 0)) {
                    if (is_internal_operation) {
                        slapi_log_err(SLAPI_LOG_REPL, "schema_delete_objectclasses",
                                      "Should not delete object class (%s) which has child object classes"
                                      ". But accept it because it is internal operation\n",
                                      delete_oc->oc_name);
                    } else {
                        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                                               delete_oc->oc_name, "Cannot delete an object class"
                                                                   " which has child object classes");
                        slapi_log_err(SLAPI_LOG_REPL, "schema_delete_objectclasses",
                                      "Cannot delete an object class (%s) which has child object classes\n",
                                      delete_oc->oc_name);
                        rc = LDAP_UNWILLING_TO_PERFORM;
                        goto unlock_and_return;
                    }
                }
            }

            if ((poc->oc_flags & OC_FLAG_STANDARD_OC) == 0) {
                oc_delete_nolock(poc->oc_name);
            }

            else {
                if (is_internal_operation) {
                    slapi_log_err(SLAPI_LOG_REPL, "schema_delete_objectclasses",
                                  "Should not delete a standard object class (%s)"
                                  ". But accept it because it is internal operation\n",
                                  delete_oc->oc_name);
                    oc_delete_nolock(poc->oc_name);
                } else {
                    schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                                           delete_oc->oc_name, "Cannot delete a standard object class");
                    slapi_log_err(SLAPI_LOG_REPL, "schema_delete_objectclasses",
                                  "Cannot delete a standard object class (%s)\n",
                                  delete_oc->oc_name);
                    rc = LDAP_UNWILLING_TO_PERFORM;
                    goto unlock_and_return;
                }
            }
        } else {
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                                   delete_oc->oc_name, "Is unknown.  Cannot delete.");
            rc = schema_ds4x_compat ? LDAP_NO_SUCH_OBJECT : LDAP_NO_SUCH_ATTRIBUTE;
            goto unlock_and_return;
        }

        oc_free(&delete_oc);
        oc_unlock();
    }

    return rc;

unlock_and_return:
    oc_free(&delete_oc);
    oc_unlock();
    return rc;
}


static int
schema_return(int rc, struct sizedbuffer *psb1, struct sizedbuffer *psb2, struct sizedbuffer *psb3, struct sizedbuffer *psb4)
{
    sizedbuffer_destroy(psb1);
    sizedbuffer_destroy(psb2);
    sizedbuffer_destroy(psb3);
    sizedbuffer_destroy(psb4);
    return rc;
}

/*
 * Delete one or more attributeTypes from our internal data structure.
 *
 * Return an LDAP error code (LDAP_SUCCESS if all goes well).
 * If an error occurs, explanatory text is copied into 'errorbuf'.
 *
 * This function should not send an LDAP result; that is the caller's
 * responsibility.
 */
static int
schema_delete_attributes(Slapi_Entry *entryBefore __attribute__((unused)), LDAPMod *mod, char *errorbuf, size_t errorbufsize, int is_internal_operation)
{
    char *attr_ldif, *oc_list_type = "";
    asyntaxinfo *a;
    struct objclass *oc = NULL;
    size_t i = 0;
    size_t k = 0;
    int attr_in_use_by_an_oc = 0;
    struct sizedbuffer *psbAttrName = sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbAttrOid = sizedbuffer_construct(BUFSIZ);
    struct sizedbuffer *psbAttrSyntax = sizedbuffer_construct(BUFSIZ);

    if (NULL == mod->mod_bvalues) {
        if (is_internal_operation) {
            slapi_log_err(SLAPI_LOG_REPL, "schema_delete_attributes", "Remove all attributetypes in Internal op\n");
        } else {
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                                   NULL, "Cannot remove all schema attribute types");
            return schema_return(LDAP_UNWILLING_TO_PERFORM, psbAttrOid, psbAttrName,
                                 psbAttrSyntax, NULL);
        }
    }

    for (i = 0; mod->mod_bvalues && mod->mod_bvalues[i]; i++) {
        attr_ldif = (char *)mod->mod_bvalues[i]->bv_val;

        /* normalize the attr ldif */
        for (k = 0; attr_ldif[k]; k++) {
            if (attr_ldif[k] == '\'' ||
                attr_ldif[k] == '(' ||
                attr_ldif[k] == ')') {
                attr_ldif[k] = ' ';
            }
            attr_ldif[k] = tolower(attr_ldif[k]);
        }

        sizedbuffer_allocate(psbAttrName, strlen(attr_ldif) + 1);
        sizedbuffer_allocate(psbAttrOid, strlen(attr_ldif) + 1);
        sizedbuffer_allocate(psbAttrSyntax, strlen(attr_ldif) + 1);

        sscanf(attr_ldif, "%s name %s syntax %s",
               psbAttrOid->buffer, psbAttrName->buffer, psbAttrSyntax->buffer);
        if ((a = attr_syntax_get_by_name(psbAttrName->buffer, 0)) != NULL) {
            /* only modify attrs which were user defined */
            if (a->asi_flags & SLAPI_ATTR_FLAG_STD_ATTR) {
                if (is_internal_operation) {
                    slapi_log_err(SLAPI_LOG_REPL, "schema_delete_attributes", "Should not delete a standard attribute type (%s)"
                                                                              ". But accept it because it is internal operation\n",
                                  psbAttrName->buffer);
                } else {
                    schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                                           psbAttrName->buffer,
                                           "Cannot delete a standard attribute type");
                    slapi_log_err(SLAPI_LOG_REPL, "schema_delete_attributes", "Cannot delete a standard attribute type (%s)\n",
                                  psbAttrName->buffer);
                    attr_syntax_return(a);
                    return schema_return(LDAP_UNWILLING_TO_PERFORM, psbAttrOid, psbAttrName,
                                         psbAttrSyntax, NULL);
                }
            }

            /* Do not allow deletion if referenced by an object class. */
            oc_lock_read();
            attr_in_use_by_an_oc = 0;
            for (oc = g_get_global_oc_nolock(); oc != NULL; oc = oc->oc_next) {
                if (NULL != oc->oc_required) {
                    for (k = 0; oc->oc_required[k] != NULL; k++) {
                        if (0 == slapi_attr_type_cmp(oc->oc_required[k], a->asi_name,
                                                     SLAPI_TYPE_CMP_EXACT)) {
                            oc_list_type = "MUST";
                            attr_in_use_by_an_oc = 1;
                            break;
                        }
                    }
                }

                if (!attr_in_use_by_an_oc && NULL != oc->oc_allowed) {
                    for (k = 0; oc->oc_allowed[k] != NULL; k++) {
                        if (0 == slapi_attr_type_cmp(oc->oc_allowed[k], a->asi_name,
                                                     SLAPI_TYPE_CMP_EXACT)) {
                            oc_list_type = "MAY";
                            attr_in_use_by_an_oc = 1;
                            break;
                        }
                    }
                }

                if (attr_in_use_by_an_oc) {
                    if (is_internal_operation) {
                        slapi_log_err(SLAPI_LOG_REPL, "schema_delete_attributes", "Should not delete an attribute (%s) used in oc (%s)"
                                                                                  ". But accept it because it is internal operation\n",
                                      oc_list_type, oc->oc_name);
                    } else {
                        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                                               psbAttrName->buffer, "Is included in the %s list for object class %s.  Cannot delete.",
                                               oc_list_type, oc->oc_name);
                        slapi_log_err(SLAPI_LOG_REPL, "schema_delete_attributes", "Could delete an attribute (%s) used in oc (%s)"
                                                                                  ". But accept it because it is internal operation\n",
                                      oc_list_type, oc->oc_name);
                        break;
                    }
                }
            }
            oc_unlock();
            if (attr_in_use_by_an_oc) {
                if (is_internal_operation) {
                    slapi_log_err(SLAPI_LOG_REPL, "schema_delete_attributes", "Should not delete an attribute used in oc"
                                                                              ". But accept it because it is internal operation\n");

                } else {
                    attr_syntax_return(a);
                    return schema_return(LDAP_UNWILLING_TO_PERFORM, psbAttrOid, psbAttrName,
                                         psbAttrSyntax, NULL);
                }
            }

            /* Delete it. */
            attr_syntax_delete(a, 0);
            attr_syntax_return(a);
        } else {
            /* unknown attribute */
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                                   psbAttrName->buffer, "Is unknown.  Cannot delete.");
            return schema_return(LDAP_NO_SUCH_ATTRIBUTE, psbAttrOid, psbAttrName,
                                 psbAttrSyntax, NULL);
        }
    }

    return schema_return(LDAP_SUCCESS, psbAttrOid, psbAttrName, psbAttrSyntax,
                         NULL);
}

static int
schema_add_attribute(Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf, size_t errorbufsize, int schema_ds4x_compat, int is_replicated_operation)
{
    int i;
    char *attr_ldif;
    /* LPXXX: Eventually, we should not allocate the buffers in parse_at_str
 * for each attribute, but use the same buffer for all.
 * This is not done yet, so it's useless to allocate buffers for nothing.
 */
    /*   struct sizedbuffer *psbAttrName= sizedbuffer_construct(BUFSIZ); */
    /*   struct sizedbuffer *psbAttrOid= sizedbuffer_construct(BUFSIZ); */
    /*   struct sizedbuffer *psbAttrDesc= sizedbuffer_construct(BUFSIZ); */
    /*   struct sizedbuffer *psbAttrSyntax= sizedbuffer_construct(BUFSIZ); */
    int status = 0;

    for (i = 0; LDAP_SUCCESS == status && mod->mod_bvalues[i]; i++) {
        PRUint32 nolock = 0; /* lock global resources during normal operation */
        attr_ldif = (char *)mod->mod_bvalues[i]->bv_val;

        status = parse_at_str(attr_ldif, NULL, errorbuf, errorbufsize,
                              nolock, is_replicated_operation ? 0 : 1 /* user defined */, schema_ds4x_compat, 1);
        if (LDAP_SUCCESS != status) {
            break; /* stop on first error */
        }
    }

    /* free everything */
    /*   sizedbuffer_destroy(psbAttrOid); */
    /*   sizedbuffer_destroy(psbAttrName); */
    /*   sizedbuffer_destroy(psbAttrDesc); */
    /*   sizedbuffer_destroy(psbAttrSyntax); */

    return status;
}

/*
 * Returns an LDAP error code (LDAP_SUCCESS if all goes well)
 */
static int
add_oc_internal(struct objclass *pnew_oc, char *errorbuf, size_t errorbufsize, int schema_ds4x_compat, PRUint32 flags)
{
    struct objclass *oldoc_by_name, *oldoc_by_oid, *psup_oc = NULL;
    int redefined_oc = 0, rc = 0;
    asyntaxinfo *pasyntaxinfo = 0;

    if (!(flags & DSE_SCHEMA_LOCKED))
        oc_lock_write();

    oldoc_by_name = oc_find_nolock(pnew_oc->oc_name, NULL, PR_FALSE);
    oldoc_by_oid = oc_find_nolock(pnew_oc->oc_oid, NULL, PR_FALSE);

    /* Check to see if the objectclass name and the objectclass oid are already
     * in use by an existing objectclass. If an existing objectclass is already
     * using the name or oid, the name and the oid should map to the same objectclass.
     * Otherwise, return an error.
     */
    if (oldoc_by_name != oldoc_by_oid) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                               pnew_oc->oc_name, "The name does not match the OID \"%s\". "
                                                 "Another object class is already using the name or OID.",
                               pnew_oc->oc_oid);
        rc = LDAP_TYPE_OR_VALUE_EXISTS;
    }

    /*
     * Set a flag so we know if we are updating an existing OC definition.
     */
    if (!rc) {
        if (NULL != oldoc_by_name) {
            redefined_oc = 1;
        } else {
            /*
             * If we are not updating an existing OC, check that the new
             * oid is not already in use.
             */
            if (NULL != oldoc_by_oid) {
                schema_create_errormsg(errorbuf, errorbufsize,
                                       schema_errprefix_oc, pnew_oc->oc_name,
                                       "The OID \"%s\" is already used by the object class \"%s\"",
                                       pnew_oc->oc_oid, oldoc_by_oid->oc_name);
                rc = LDAP_TYPE_OR_VALUE_EXISTS;
            }
        }
    }

    /* check to see if the superior oc exists
     * This is not enforced for internal op (when learning new schema
     * definitions from a replication session)
     */
    if (!rc && pnew_oc->oc_superior &&
        ((psup_oc = oc_find_nolock(pnew_oc->oc_superior, NULL, PR_FALSE)) == NULL)) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                               pnew_oc->oc_name, "Superior object class \"%s\" does not exist",
                               pnew_oc->oc_superior);
        rc = LDAP_TYPE_OR_VALUE_EXISTS;
    }

    /* inherit the attributes from the superior oc */
    if (!rc && psup_oc) {
        if (psup_oc->oc_required) {
            charray_merge(&pnew_oc->oc_required, psup_oc->oc_required, 1);
        }
        if (psup_oc->oc_allowed) {
            charray_merge(&pnew_oc->oc_allowed, psup_oc->oc_allowed, 1);
        }
    }

    /* check to see if the oid is already in use by an attribute */
    if (!rc && (pasyntaxinfo = attr_syntax_get_by_oid(pnew_oc->oc_oid, flags))) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                               pnew_oc->oc_name,
                               "The OID \"%s\" is also used by the attribute type \"%s\"",
                               pnew_oc->oc_oid, pasyntaxinfo->asi_name);
        rc = LDAP_TYPE_OR_VALUE_EXISTS;
        attr_syntax_return(pasyntaxinfo);
    }

    /* check to see if the objectclass name is valid */
    if (!rc && !(flags & DSE_SCHEMA_NO_CHECK) &&
        schema_check_name(pnew_oc->oc_name, PR_FALSE, errorbuf, errorbufsize) == 0) {
        rc = schema_ds4x_compat ? LDAP_OPERATIONS_ERROR : LDAP_INVALID_SYNTAX;
    }

    /* check to see if the oid is valid */
    if (!rc && !(flags & DSE_SCHEMA_NO_CHECK)) {
        struct sizedbuffer *psbOcOid, *psbOcName;

        psbOcName = sizedbuffer_construct(strlen(pnew_oc->oc_name) + 1);
        psbOcOid = sizedbuffer_construct(strlen(pnew_oc->oc_oid) + 1);
        strcpy(psbOcName->buffer, pnew_oc->oc_name);
        strcpy(psbOcOid->buffer, pnew_oc->oc_oid);

        if (!schema_check_oid(psbOcName->buffer, psbOcOid->buffer, PR_FALSE,
                              errorbuf, errorbufsize))
            rc = schema_ds4x_compat ? LDAP_OPERATIONS_ERROR : LDAP_INVALID_SYNTAX;

        sizedbuffer_destroy(psbOcName);
        sizedbuffer_destroy(psbOcOid);
    }

    /* check to see if the oc's attributes are valid
     * This is not checked if this is an internal operation (learning schema
     * definitions from a replication session)
     */
    if (!rc && !(flags & DSE_SCHEMA_NO_CHECK) &&
        schema_check_oc_attrs(pnew_oc, errorbuf, errorbufsize,
                              0 /* don't strip options */) == 0) {
        rc = schema_ds4x_compat ? LDAP_OPERATIONS_ERROR : LDAP_INVALID_SYNTAX;
    }
    /* insert new objectclass exactly where the old one one in the linked list*/
    if (!rc && redefined_oc) {
        pnew_oc->oc_flags |= OC_FLAG_REDEFINED_OC;
        rc = oc_replace_nolock(pnew_oc->oc_name, pnew_oc, errorbuf, errorbufsize);
    }

    if (!rc && !redefined_oc) {
        oc_add_nolock(pnew_oc);
    }

    if (!rc && redefined_oc) {
        oc_update_inheritance_nolock(pnew_oc);
    }

    if (!(flags & DSE_SCHEMA_LOCKED))
        oc_unlock();
    return rc;
}


/*
 * Process a replace modify suboperation for attributetypes.
 *
 * XXXmcs: At present, readonly (bundled) schema definitions can't be
 * removed.  If that is attempted, we just keep them without generating
 * an error.
 *
 * Our algorithm is:
 *
 *   Clear the "keep" flags on the all existing attr. definitions.
 *
 *   For each replacement value:
 *      If the value exactly matches an existing schema definition,
 *      set that definition's keep flag.
 *
 *      Else if the OID in the replacement value matches an existing
 *      definition, delete the old definition and add the new one.  Set
 *      the keep flag on the newly added definition.
 *
 *      Else add the new definition.  Set the keep flag on the newly
 *      added definition.
 *
 *   For each definition that is not flagged keep, delete.
 *
 *   Clear all remaining "keep" flags.
 *
 * Note that replace was not supported at all before iDS 5.0.
 */
static int
schema_replace_attributes(Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf, size_t errorbufsize, int is_replicated_operation)
{
    int i, rc = LDAP_SUCCESS;
    struct asyntaxinfo *newasip, *oldasip;
    PRUint32 schema_flags = 0;

    if (NULL == mod->mod_bvalues) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                               NULL, "Cannot remove all schema attribute types");
        return LDAP_UNWILLING_TO_PERFORM;
    }

    slapi_pblock_get(pb, SLAPI_SCHEMA_FLAGS, &schema_flags);
    if (!(schema_flags & (DSE_SCHEMA_NO_LOAD | DSE_SCHEMA_NO_CHECK))) {
        /* clear all of the "keep" flags unless it's from schema-reload */
        attr_syntax_all_clear_flag(SLAPI_ATTR_FLAG_KEEP);
    }

    for (i = 0; mod->mod_bvalues[i] != NULL; ++i) {
        if (LDAP_SUCCESS != (rc = parse_at_str(mod->mod_bvalues[i]->bv_val,
                                               &newasip, errorbuf, errorbufsize, 0,
                                               is_replicated_operation ? 0 : 1,
                                               0, 0)))
        {
            goto clean_up_and_return;
        }

        /*
         * Check for a match with an existing type and
         * handle the various cases.
         */
        if (NULL == (oldasip =
                         attr_syntax_get_by_oid(newasip->asi_oid, 0))) {
            /* new attribute type */
            slapi_log_err(SLAPI_LOG_TRACE, "schema_replace_attributes",
                          "New type %s (OID %s)\n",
                          newasip->asi_name, newasip->asi_oid);
        } else {
            /* the name matches -- check the rest */
            if (attr_syntax_equal(newasip, oldasip)) {
                /* unchanged attribute type -- just mark it as one to keep */
                oldasip->asi_flags |= SLAPI_ATTR_FLAG_KEEP;
                attr_syntax_free(newasip);
                newasip = NULL;
            } else {
                /* modified attribute type */
                slapi_log_err(SLAPI_LOG_TRACE, "schema_replace_attributes",
                              "Replacing type %s (OID %s)\n",
                              newasip->asi_name, newasip->asi_oid);
                /* flag for deletion */
                attr_syntax_delete(oldasip, 0);
            }

            attr_syntax_return(oldasip);
        }

        if (NULL != newasip) { /* add new or replacement definition */
            rc = attr_syntax_add(newasip, 0);
            if (LDAP_SUCCESS != rc) {
                schema_create_errormsg(errorbuf, errorbufsize,
                                       schema_errprefix_at, newasip->asi_name,
                                       "Could not be added (OID is \"%s\")",
                                       newasip->asi_oid);
                attr_syntax_free(newasip);
                goto clean_up_and_return;
            }

            newasip->asi_flags |= SLAPI_ATTR_FLAG_KEEP;
        }
    }

    /*
     * Delete all of the definitions that are not marked "keep" or "standard".
     *
     * XXXmcs: we should consider reporting an error if any read only types
     * remain....
     */
    attr_syntax_delete_all_not_flagged(SLAPI_ATTR_FLAG_KEEP |
                                       SLAPI_ATTR_FLAG_STD_ATTR);

clean_up_and_return:
    if (!(schema_flags & (DSE_SCHEMA_NO_LOAD | DSE_SCHEMA_NO_CHECK))) {
        /* clear all of the "keep" flags unless it's from schema-reload */
        attr_syntax_all_clear_flag(SLAPI_ATTR_FLAG_KEEP);
    }

    return rc;
}


static int
schema_add_objectclass(Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf, size_t errorbufsize, int schema_ds4x_compat, int is_replicated_operation)
{
    struct objclass *pnew_oc = NULL;
    char *newoc_ldif;
    int j, rc = 0;

    for (j = 0; mod->mod_bvalues[j]; j++) {
        newoc_ldif = (char *)mod->mod_bvalues[j]->bv_val;
        if (LDAP_SUCCESS != (rc = parse_oc_str(newoc_ldif, &pnew_oc,
                                               errorbuf, errorbufsize, 0,
                                               is_replicated_operation ? 0 : 1 /* user defined */,
                                               schema_ds4x_compat, NULL)))
        {
            oc_free(&pnew_oc);
            return rc;
        }

        if (LDAP_SUCCESS != (rc = add_oc_internal(pnew_oc, errorbuf,
                                                  errorbufsize, schema_ds4x_compat, 0 /* no restriction */))) {
            oc_free(&pnew_oc);
            return rc;
        }

        normalize_oc();
    }

    return LDAP_SUCCESS;
}


/*
 * Process a replace modify suboperation for objectclasses.
 *
 * XXXmcs: At present, readonly (bundled) schema definitions can't be
 * removed.  If that is attempted, we just keep them without generating
 * an error.
 *
 * Our algorithm is:
 *
 *   Lock the global objectclass linked list.
 *
 *   Create a new empty (temporary) linked list, initially empty.
 *
 *   For each replacement value:
 *      If the value exactly matches an existing schema definition,
 *      move the existing definition from the current global list to the
 *      temporary list
 *
 *      Else if the OID in the replacement value matches an existing
 *      definition, delete the old definition from the current global
 *      list and add the new one to the temporary list.
 *
 *      Else add the new definition to the temporary list.
 *
 *   Delete all definitions that remain on the current global list.
 *
 *   Make the temporary list the current global list.
 *
 * Note that since the objectclass definitions are stored in a linked list,
 * this algorithm is O(N * M) where N is the number of existing objectclass
 * definitions and M is the number of replacement definitions.
 * XXXmcs: Yuck.  We should use a hash table for the OC definitions.
 *
 * Note that replace was not supported at all by DS versions prior to 5.0
 */

static int
schema_replace_objectclasses(Slapi_PBlock *pb, LDAPMod *mod, char *errorbuf, size_t errorbufsize, int is_replicated_operation)
{
    struct objclass *newocp, *curlisthead, *prevocp, *tmpocp;
    struct objclass *newlisthead = NULL, *newlistend = NULL;
    int i, rc = LDAP_SUCCESS;

    if (NULL == mod->mod_bvalues) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                               NULL, "Cannot remove all schema object classes");
        return LDAP_UNWILLING_TO_PERFORM;
    }

    oc_lock_write();

    curlisthead = g_get_global_oc_nolock();

    for (i = 0; mod->mod_bvalues[i] != NULL; ++i) {
        struct objclass *addocp = NULL;

        if (LDAP_SUCCESS != (rc = parse_oc_str(mod->mod_bvalues[i]->bv_val,
                                               &newocp, errorbuf, errorbufsize, DSE_SCHEMA_NO_GLOCK,
                                               is_replicated_operation ? 0 : 1 /* user defined */,
                                               0 /* no DS 4.x compat issues */, NULL))) {
            rc = LDAP_INVALID_SYNTAX;
            goto clean_up_and_return;
        }

        prevocp = NULL;
        for (tmpocp = curlisthead; tmpocp != NULL; tmpocp = tmpocp->oc_next) {
            if (0 == strcasecmp(tmpocp->oc_oid, newocp->oc_oid)) {
                /* the names match -- remove from the current list */
                if (tmpocp == curlisthead) {
                    curlisthead = tmpocp->oc_next;
                    /* The global oc list is scanned in parse_oc_str above,
                       if there are multiple objectclasses to be updated.
                       Needs to maintain the list dynamically. */
                    g_set_global_oc_nolock(curlisthead);
                } else {
                    if (prevocp)
                        prevocp->oc_next = tmpocp->oc_next;
                }
                tmpocp->oc_next = NULL;

                /* check for a full match */
                if (oc_equal(tmpocp, newocp)) {
                    /* no changes: keep existing definition and discard new */
                    oc_free(&newocp);
                    addocp = tmpocp;
                } else {
                    /* some differences: discard old and keep the new one */
                    oc_free(&tmpocp);
                    slapi_log_err(SLAPI_LOG_TRACE, "schema_replace_objectclasses",
                                  "Replacing object class %s (OID %s)\n",
                                  newocp->oc_name, newocp->oc_oid);
                    addocp = newocp;
                }
                break; /* we found it -- exit the loop */
            }
            prevocp = tmpocp;
        }

        if (NULL == addocp) {
            slapi_log_err(SLAPI_LOG_TRACE, "schema_replace_objectclasses",
                          "New object class %s (OID %s)\n",
                          newocp->oc_name, newocp->oc_oid);
            addocp = newocp;
        }

        /* add the objectclass to the end of the new list */
        if (NULL != addocp) {
            if (NULL == newlisthead) {
                newlisthead = addocp;
            } else {
                newlistend->oc_next = addocp;
            }
            newlistend = addocp;
        }
    }

clean_up_and_return:
    if (LDAP_SUCCESS == rc) {
        /*
         * Delete all remaining OCs that are on the old list AND are not
         * "standard" classes.
         */
        struct objclass *nextocp;

        prevocp = NULL;
        for (tmpocp = curlisthead; tmpocp != NULL; tmpocp = nextocp) {
            if (0 == (tmpocp->oc_flags & OC_FLAG_STANDARD_OC)) {
                /* not a standard definition -- remove it */
                if (tmpocp == curlisthead) {
                    curlisthead = tmpocp->oc_next;
                } else {
                    if (prevocp) {
                        prevocp->oc_next = tmpocp->oc_next;
                    }
                }
                nextocp = tmpocp->oc_next;
                oc_free(&tmpocp);
            } else {
                /*
                 * XXXmcs: we could generate an error, but for now we do not.
                 */
                nextocp = tmpocp->oc_next;
                prevocp = tmpocp;
            }
        }
    }

    /*
     * Combine the two lists by adding the new list to the end of the old
     * one.
     */
    if (NULL != curlisthead) {
        for (tmpocp = curlisthead; tmpocp->oc_next != NULL;
             tmpocp = tmpocp->oc_next) {
            ; /*NULL*/
        }
        tmpocp->oc_next = newlisthead;
        newlisthead = curlisthead;
    }

    /*
     * Install the new list as the global one, replacing the old one.
     */
    g_set_global_oc_nolock(newlisthead);

    oc_unlock();
    return rc;
}

schemaext *
schema_copy_extensions(schemaext *extensions)
{
    schemaext *ext = NULL, *head = NULL;
    while (extensions) {
        schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof(schemaext));
        newext->term = slapi_ch_strdup(extensions->term);
        newext->values = charray_dup(extensions->values);
        newext->value_count = extensions->value_count;
        if (ext == NULL) {
            ext = newext;
            head = newext;
        } else {
            ext->next = newext;
            ext = newext;
        }
        extensions = extensions->next;
    }

    return head;
}

void
schema_free_extensions(schemaext *extensions)
{
    if (extensions) {
        schemaext *prev;

        while (extensions) {
            slapi_ch_free_string(&extensions->term);
            charray_free(extensions->values);
            prev = extensions;
            extensions = extensions->next;
            slapi_ch_free((void **)&prev);
        }
    }
}

static void
oc_free(struct objclass **ocp)
{
    struct objclass *oc;

    if (NULL != ocp && NULL != *ocp) {
        oc = *ocp;
        slapi_ch_free((void **)&oc->oc_name);
        slapi_ch_free((void **)&oc->oc_desc);
        slapi_ch_free((void **)&oc->oc_oid);
        slapi_ch_free((void **)&oc->oc_superior);
        charray_free(oc->oc_required);
        charray_free(oc->oc_allowed);
        charray_free(oc->oc_orig_required);
        charray_free(oc->oc_orig_allowed);
        schema_free_extensions(oc->oc_extensions);
        slapi_ch_free((void **)&oc);
        *ocp = NULL;
    }
}


/* openldap attribute parser */
/*
 * if asipp is NULL, the attribute type is added to the global set of schema.
 * if asipp is not NULL, the AT is not added but *asipp is set.  When you are
 * finished with *asipp, use attr_syntax_free() to dispose of it.
 *
 *    schema_flags: Any or none of the following bits could be set
 *        DSE_SCHEMA_NO_CHECK -- schema won't be checked
 *        DSE_SCHEMA_NO_GLOCK -- locking of global resources is turned off;
 *                               this saves time during initialization since
 *                               the server operates in single threaded mode
 *                               at that time or in reload_schemafile_lock.
 *        DSE_SCHEMA_LOCKED   -- already locked with reload_schemafile_lock;
 *                               no further lock needed
 *
 * if is_user_defined is true, force attribute type to be user defined.
 *
 * returns an LDAP error code (LDAP_SUCCESS if all goes well)
*/
static int
parse_attr_str(const char *input, struct asyntaxinfo **asipp, char *errorbuf, size_t errorbufsize,
               PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, int is_remote __attribute__((unused)))
{
    struct asyntaxinfo *tmpasip;
    struct asyntaxinfo *tmpasi;
    schemaext *extensions = NULL, *head = NULL;
    struct objclass *poc;
    LDAPAttributeType *atype = NULL;
    const char *errp;
    char *first_attr_name = NULL;
    char **attr_names = NULL;
    unsigned long flags = SLAPI_ATTR_FLAG_OVERRIDE;
    /* If we ever accept openldap schema directly, then make parser_flags configurable */
    unsigned int parser_flags = LDAP_SCHEMA_ALLOW_NONE | LDAP_SCHEMA_ALLOW_NO_OID;
    int invalid_syntax_error;
    int syntaxlength = SLAPI_SYNTAXLENGTH_NONE;
    int num_names = 0;
    int status = 0;
    int rc = 0;
    int a, aa;

    if (config_get_enquote_sup_oc()) {
        parser_flags |= LDAP_SCHEMA_ALLOW_QUOTED;
    } else if (getenv("LDAP_SCHEMA_ALLOW_QUOTED")) {
        char ebuf[SLAPI_DSE_RETURNTEXT_SIZE];
        parser_flags |= LDAP_SCHEMA_ALLOW_QUOTED;
        if (config_set_enquote_sup_oc(CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, "on", ebuf, CONFIG_APPLY)) {
            slapi_log_err(SLAPI_LOG_ERR, "parse_attr_str", "Failed to enable %s: %s\n",
                          CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, ebuf);
        }
    }

    /*
     *      OpenLDAP AttributeType struct
     *
     *          typedef struct ldap_attributetype {
     *                 char *at_oid;            OID
     *                 char **at_names;         Names
     *                 char *at_desc;           Description
     *                 int  at_obsolete;        Is obsolete?
     *                 char *at_sup_oid;        OID of superior type
     *                 char *at_equality_oid;   OID of equality matching rule
     *                 char *at_ordering_oid;   OID of ordering matching rule
     *                 char *at_substr_oid;     OID of substrings matching rule
     *                 char *at_syntax_oid;     OID of syntax of values
     *                 int  at_syntax_len;      Suggested minimum maximum length
     *                 int  at_single_value;    Is single-valued?
     *                 int  at_collective;      Is collective?
     *                 int  at_no_user_mod;     Are changes forbidden through LDAP?
     *                 int  at_usage;           Usage, see below
     *                 LDAPSchemaExtensionItem **at_extensions; Extensions
     *         } LDAPAttributeType;
     */

    /*
     *  Set the appropriate error code
     */
    if (schema_ds4x_compat) {
        invalid_syntax_error = LDAP_OPERATIONS_ERROR;
    } else {
        invalid_syntax_error = LDAP_INVALID_SYNTAX;
    }
    /*
     *  Verify we have input
     */
    if (input == NULL || '\0' == input[0]) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at, NULL,
                               "One or more values are required for the attributeTypes attribute");
        slapi_log_err(SLAPI_LOG_ERR, "parse_attr_str", "NULL args passed to parse_attr_str\n");
        return invalid_syntax_error;
    }
    /*
     *  Parse the line and create of attribute structure
     */
    while (isspace(*input)) {
        /* trim any leading spaces */
        input++;
    }
    if ((atype = ldap_str2attributetype(input, &rc, &errp, (const unsigned int)parser_flags)) == NULL) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at, input,
                               "Failed to parse attribute, error(%d - %s) at (%s)", rc, ldap_scherr2str(rc), errp);
        return invalid_syntax_error;
    }

    if (schema_flags & DSE_SCHEMA_NO_GLOCK) {
        flags |= SLAPI_ATTR_FLAG_NOLOCKING;
    }
    /*
     *  Check the NAME and update our name list
     */
    if (NULL != atype->at_names) {
        for (; atype->at_names[num_names]; num_names++) {
            charray_add(&attr_names, slapi_ch_strdup(atype->at_names[num_names]));
        }
        first_attr_name = atype->at_names[0];
    } else { /* NAME followed by nothing violates syntax */
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at, input,
                               "Missing or invalid attribute name");
        status = invalid_syntax_error;
        goto done;
    }
    /*
     *  If the attribute type doesn't have an OID, we'll make the oid attrname-oid.
     */
    if (NULL == atype->at_oid) {
        atype->at_oid = slapi_ch_smprintf("%s-oid", first_attr_name);
    }
    /*
     *  Set the flags
     */
    if (atype->at_obsolete) {
        flags |= SLAPI_ATTR_FLAG_OBSOLETE;
    }
    if (atype->at_single_value) {
        flags |= SLAPI_ATTR_FLAG_SINGLE;
    }
    if (atype->at_collective) {
        flags |= SLAPI_ATTR_FLAG_COLLECTIVE;
    }
    if (atype->at_no_user_mod) {
        flags |= SLAPI_ATTR_FLAG_NOUSERMOD;
    }
    if (atype->at_usage == LDAP_SCHEMA_DIRECTORY_OPERATION) {
        flags |= SLAPI_ATTR_FLAG_OPATTR;
    }
    if (atype->at_usage == LDAP_SCHEMA_DISTRIBUTED_OPERATION) {
        flags |= SLAPI_ATTR_FLAG_OPATTR | SLAPI_ATTR_FLAG_DISTRIBUTED_OPERATION;
    }
    if (atype->at_usage == LDAP_SCHEMA_DSA_OPERATION) {
        flags |= SLAPI_ATTR_FLAG_OPATTR | SLAPI_ATTR_FLAG_DSA_OPERATION;
    }
    /*
     * Check the superior, and use it fill in any missing oids on this attribute
     */
    if (NULL != atype->at_sup_oid) {
        struct asyntaxinfo *asi_parent;

        asi_parent = attr_syntax_get_by_name(atype->at_sup_oid, schema_flags);
        /* if we find no match then server won't start or add the attribute type */
        if (asi_parent == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "parse_attr_str", "Cannot find parent attribute type \"%s\"\n",
                          atype->at_sup_oid);
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at, first_attr_name,
                                   "Missing parent attribute syntax OID");
            status = invalid_syntax_error;
            goto done;

        } else if ((NULL == atype->at_syntax_oid) || (NULL == atype->at_equality_oid) ||
                   (NULL == atype->at_substr_oid) || (NULL == atype->at_ordering_oid)) {
            /*
             * We only want to use the parent syntax if a SYNTAX
             * wasn't explicitly specified for this attribute.
             */
            char *pso = asi_parent->asi_plugin->plg_syntax_oid;

            if (pso && (NULL == atype->at_syntax_oid)) {
                atype->at_syntax_oid = slapi_ch_strdup(pso);
                slapi_log_err(SLAPI_LOG_TRACE,
                              "parse_attr_str", "Inheriting syntax %s from parent type %s\n",
                              atype->at_syntax_oid, atype->at_sup_oid);
            } else if (NULL == atype->at_syntax_oid) {
                schema_create_errormsg(errorbuf, errorbufsize,
                                       schema_errprefix_at, first_attr_name,
                                       "Missing parent attribute syntax OID");
                status = invalid_syntax_error;
                goto done;
            }

            if (NULL == atype->at_equality_oid) {
                atype->at_equality_oid = slapi_ch_strdup(asi_parent->asi_mr_equality);
            }
            if (NULL == atype->at_substr_oid) {
                atype->at_substr_oid = slapi_ch_strdup(asi_parent->asi_mr_substring);
            }
            if (NULL == atype->at_ordering_oid) {
                atype->at_ordering_oid = slapi_ch_strdup(asi_parent->asi_mr_ordering);
            }
            attr_syntax_return(asi_parent);
        }
    }
    /*
     *  Make sure we have a syntax oid set
     */
    if (NULL == atype->at_syntax_oid) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                               first_attr_name, "Missing attribute syntax OID");
        status = invalid_syntax_error;
        goto done;
    }
    /*
     *  Make sure the OID is known
     */
    if (!status && (plugin_syntax_find(atype->at_syntax_oid) == NULL)) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                               first_attr_name, "Unknown attribute syntax OID \"%s\"",
                               atype->at_syntax_oid);
        status = invalid_syntax_error;
        goto done;
    }
    /*
     * Check to make sure that the OID isn't being used by an objectclass
     */
    if (!(schema_flags & DSE_SCHEMA_LOCKED)) {
        oc_lock_read();
    }
    poc = oc_find_oid_nolock(atype->at_oid);
    if (poc != NULL) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at, first_attr_name,
                               "The OID \"%s\" is also used by the object class \"%s\"", atype->at_oid, poc->oc_name);
        status = LDAP_TYPE_OR_VALUE_EXISTS;
    }
    if (!(schema_flags & DSE_SCHEMA_LOCKED)) {
        oc_unlock();
    }
    if (status) {
        goto done;
    }
    /*
     *  Walk the "at_extensions" and set the schema extensions
     */
    for (a = 0; atype->at_extensions && atype->at_extensions[a]; a++) {
        schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof(schemaext));
        newext->term = slapi_ch_strdup(atype->at_extensions[a]->lsei_name);
        for (aa = 0; atype->at_extensions[a]->lsei_values && atype->at_extensions[a]->lsei_values[aa]; aa++) {
            charray_add(&newext->values, slapi_ch_strdup(atype->at_extensions[a]->lsei_values[aa]));
            newext->value_count++;
        }
        if (extensions == NULL) {
            extensions = newext;
            head = newext;
        } else {
            extensions->next = newext;
            extensions = newext;
        }
    }
    extensions = head; /* reset to the top of the list */
    /*
     *  Make sure if we are user-defined, that the attr_origins represents it
     */
    if (!extension_is_user_defined(extensions)) {
        if (is_user_defined) {
            int added = 0;
            /* see if we have a X-ORIGIN term already */
            while (extensions) {
                if (strcmp(extensions->term, "X-ORIGIN") == 0) {
                    charray_add(&extensions->values, slapi_ch_strdup(schema_user_defined_origin[0]));
                    extensions->value_count++;
                    added = 1;
                    break;
                }
                extensions = extensions->next;
            }
            if (!added) {
                /* X-ORIGIN is completely missing, add it */
                extensions = head;
                schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof(schemaext));
                newext->term = slapi_ch_strdup("X-ORIGIN");
                charray_add(&newext->values, slapi_ch_strdup(schema_user_defined_origin[0]));
                newext->value_count++;
                while (extensions && extensions->next) {
                    /* move to the end of the list */
                    extensions = extensions->next;
                }
                if (extensions == NULL) {
                    extensions = newext;
                    head = extensions;
                } else {
                    extensions->next = newext;
                }
            }
        } else {
            flags |= SLAPI_ATTR_FLAG_STD_ATTR;
        }
    }
    extensions = head; /* reset to the top of the list */
    /*
     *  Check to see if the attribute name is valid
     */
    if (!(schema_flags & DSE_SCHEMA_NO_CHECK)) {
        for (a = 0; a < num_names; ++a) {
            if (schema_check_name(attr_names[a], PR_TRUE, errorbuf, errorbufsize) == 0) {
                status = invalid_syntax_error;
                goto done;
            } else if (!(flags & SLAPI_ATTR_FLAG_OVERRIDE) && attr_syntax_exists(attr_names[a])) {
                schema_create_errormsg(errorbuf, errorbufsize,
                                       schema_errprefix_at, attr_names[a],
                                       "Could not be added because it already exists");
                status = LDAP_TYPE_OR_VALUE_EXISTS;
                goto done;
            }
        }
    }
    /*
     *  Check if the OID is valid
     */
    if (!(schema_flags & DSE_SCHEMA_NO_CHECK)) {
        if (schema_check_oid(first_attr_name, atype->at_oid, PR_TRUE, errorbuf,
                             errorbufsize) == 0) {
            status = invalid_syntax_error;
            goto done;
        }
    }
    /*
     *  Check if the OID is already being used
     */
    if (!(flags & SLAPI_ATTR_FLAG_OVERRIDE) && (NULL != (tmpasi = attr_syntax_get_by_oid(atype->at_oid, schema_flags)))) {
        schema_create_errormsg(errorbuf, errorbufsize,
                               schema_errprefix_at, first_attr_name,
                               "Could not be added because the OID \"%s\" is already in use", atype->at_oid);
        status = LDAP_TYPE_OR_VALUE_EXISTS;
        attr_syntax_return(tmpasi);
        goto done;
    }
    /*
     *  Finally create the attribute
     */
    status = attr_syntax_create(atype->at_oid, attr_names, atype->at_desc, atype->at_sup_oid,
                                atype->at_equality_oid, atype->at_ordering_oid, atype->at_substr_oid,
                                extensions, atype->at_syntax_oid, syntaxlength, flags, &tmpasip);
    if (!status) {
        if (NULL != asipp) {
            *asipp = tmpasip; /* just return it */
        } else {
            /* add the new attribute to the global store */
            status = attr_syntax_add(tmpasip, schema_flags);
            if (LDAP_SUCCESS != status) {
                if (0 != (flags & SLAPI_ATTR_FLAG_OVERRIDE) &&
                    LDAP_TYPE_OR_VALUE_EXISTS == status) {
                    /*
                     * This can only occur if the name and OID don't match the
                     * attribute we are trying to override (all other cases of
                     * "type or value exists" were trapped above).
                     */
                    schema_create_errormsg(errorbuf, errorbufsize,
                                           schema_errprefix_at, first_attr_name,
                                           "Does not match the OID \"%s\". Another attribute "
                                           "type is already using the name or OID.",
                                           atype->at_oid);
                } else {
                    schema_create_errormsg(errorbuf, errorbufsize,
                                           schema_errprefix_at, first_attr_name,
                                           "Could not be added (OID is \"%s\")", atype->at_oid);
                }
                attr_syntax_free(tmpasip);
            }
        }
    }

done:
    /* free everything */
    ldap_attributetype_free(atype);
    charray_free(attr_names);
    schema_free_extensions(extensions);

    return status;
}

/*
 * parse_objclass_str
 *
 * Read the value of the objectclasses attribute in cn=schema, convert it
 * into an objectclass struct.
 *
 * Arguments:
 *
 *     input             : value of objectclasses attribute to read
 *     oc                : pointer write the objectclass to
 *     errorbuf          : buffer to write any errors to
 *     is_user_defined   : if non-zero, force objectclass to be user defined
 *     schema_flags      : Any or none of the following bits could be set
 *                         DSE_SCHEMA_NO_CHECK -- schema won't be checked
 *                         DSE_SCHEMA_NO_GLOCK -- don't lock global resources
 *                         DSE_SCHEMA_LOCKED   -- already locked with
 *                                                reload_schemafile_lock;
 *                                                no further lock needed
 *     schema_ds4x_compat: if non-zero, act like Netscape DS 4.x
 *
 * Returns: an LDAP error code
 *
 *       LDAP_SUCCESS if the objectclass was sucessfully read, the new
 *         objectclass will be written to oc
 *
 *       All others:  there was an error, an error message will
 *         be written to errorbuf
 */
static int
parse_objclass_str(const char *input, struct objclass **oc, char *errorbuf, size_t errorbufsize, PRUint32 schema_flags, int is_user_defined, int schema_ds4x_compat, struct objclass *private_schema)
{
    LDAPObjectClass *objClass;
    struct objclass *pnew_oc = NULL, *psup_oc = NULL;
    schemaext *extensions = NULL, *head = NULL;
    const char *errp;
    char **OrigRequiredAttrsArray, **OrigAllowedAttrsArray;
    char *first_oc_name = NULL;
    /* If we ever accept openldap schema directly, then make parser_flags configurable */
    unsigned int parser_flags = LDAP_SCHEMA_ALLOW_NONE | LDAP_SCHEMA_ALLOW_NO_OID;
    PRUint8 flags = 0;
    int invalid_syntax_error;
    int i, j;
    int rc = 0;

    if (!oc) {
        return LDAP_PARAM_ERROR;
    }
    if (config_get_enquote_sup_oc()) {
        parser_flags |= LDAP_SCHEMA_ALLOW_QUOTED;
    } else if (getenv("LDAP_SCHEMA_ALLOW_QUOTED")) {
        char ebuf[SLAPI_DSE_RETURNTEXT_SIZE];
        parser_flags |= LDAP_SCHEMA_ALLOW_QUOTED;
        if (config_set_enquote_sup_oc(CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, "on", ebuf, CONFIG_APPLY)) {
            slapi_log_err(SLAPI_LOG_ERR, "parse_objclass_str", "Failed to enable %s: %s\n",
                          CONFIG_ENQUOTE_SUP_OC_ATTRIBUTE, ebuf);
        }
    }

    /*
     *     openLDAP Objectclass struct
     *
     *          typedef struct ldap_objectclass {
     *                   char *oc_oid;            OID
     *                   char **oc_names;         Names
     *                   char *oc_desc;           Description
     *                   int  oc_obsolete;        Is obsolete?
     *                   char **oc_sup_oids;      OIDs of superior classes
     *                   int  oc_kind;            Kind
     *                   char **oc_at_oids_must;  OIDs of required attribute types
     *                   char **oc_at_oids_may;   OIDs of optional attribute types
     *                   LDAPSchemaExtensionItem **oc_extensions;  Extensions
     *           } LDAPObjectClass;
     */

    /*
     *  Set the appropriate error code
     */
    if (schema_ds4x_compat) {
        invalid_syntax_error = LDAP_OPERATIONS_ERROR;
    } else {
        invalid_syntax_error = LDAP_INVALID_SYNTAX;
    }
    /*
     *  Verify we have input
     */
    if (NULL == input || '\0' == input[0]) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc, NULL,
                               "One or more values are required for the objectClasses attribute");
        slapi_log_err(SLAPI_LOG_ERR, "parse_objclass_str", "NULL args passed to read_oc_ldif\n");
        return LDAP_OPERATIONS_ERROR;
    }
    /*
     *  Parse the input and create the openLdap objectclass structure
     */
    while (isspace(*input)) {
        /* trim any leading spaces */
        input++;
    }
    if ((objClass = ldap_str2objectclass(input, &rc, &errp, (const unsigned int)parser_flags)) == NULL) {
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc, input,
                               "Failed to parse objectclass, error(%d) at (%s)", rc, errp);
        return invalid_syntax_error;
    }
    /*
     *  Check the NAME and update our name list
     */
    if (NULL != objClass->oc_names) {
        first_oc_name = objClass->oc_names[0];
    } else { /* NAME followed by nothing violates syntax */
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc, input,
                               "Missing or invalid objectclass name");
        rc = invalid_syntax_error;
        goto done;
    }
    /*
     *  If the objectclass type doesn't have an OID, we'll make the oid objClass-oid.
     */
    if (NULL == objClass->oc_oid) {
        objClass->oc_oid = slapi_ch_smprintf("%s-oid", first_oc_name);
    }
    /*
     *  Check to see if the objectclass name is valid
     */
    if (!(schema_flags & DSE_SCHEMA_NO_CHECK)) {
        for (i = 0; objClass->oc_names[i]; ++i) {
            if (schema_check_name(objClass->oc_names[i], PR_TRUE, errorbuf, errorbufsize) == 0) {
                rc = invalid_syntax_error;
                goto done;
            }
        }
    }
    /*
     *  Check if the OID is valid
     */
    if (!(schema_flags & DSE_SCHEMA_NO_CHECK)) {
        if (schema_check_oid(first_oc_name, objClass->oc_oid, PR_TRUE, errorbuf,
                             errorbufsize) == 0) {
            rc = invalid_syntax_error;
            goto done;
        }
    }
    /*
     * Look for the superior objectclass.  We first look for a parenthesized
     * list and if not found we look for a simple OID.
     *
     * XXXmcs: Since we do not yet support multiple superior objectclasses, we
     * just grab the first OID in a parenthesized list.
     */
    if (!(schema_flags & DSE_SCHEMA_NO_GLOCK)) {
        /* needed because we access the superior oc */
        oc_lock_read();
    }
    if (objClass->oc_sup_oids && objClass->oc_sup_oids[0]) {
        if (schema_flags & DSE_SCHEMA_USE_PRIV_SCHEMA) {
            /* We have built an objectclass list on a private variable
                         * This is used to check the schema of a remote consumer
                         */
            psup_oc = oc_find_nolock(objClass->oc_sup_oids[0], private_schema, PR_TRUE);
        } else {
            psup_oc = oc_find_nolock(objClass->oc_sup_oids[0], NULL, PR_FALSE);
        }
    }
    /*
     *  Walk the "oc_extensions" and set the schema extensions
     */
    for (i = 0; objClass->oc_extensions && objClass->oc_extensions[i]; i++) {
        schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof(schemaext));
        newext->term = slapi_ch_strdup(objClass->oc_extensions[i]->lsei_name);
        for (j = 0; objClass->oc_extensions[i]->lsei_values && objClass->oc_extensions[i]->lsei_values[j]; j++) {
            charray_add(&newext->values, slapi_ch_strdup(objClass->oc_extensions[i]->lsei_values[j]));
            newext->value_count++;
        }
        if (extensions == NULL) {
            extensions = newext;
            head = extensions;
        } else {
            extensions->next = newext;
            extensions = newext;
        }
    }
    extensions = head; /* reset to the top of the list */
    /*
     *  Set the remaining flags
     */
    if (objClass->oc_obsolete) {
        flags |= OC_FLAG_OBSOLETE;
    }
    if (extension_is_user_defined(extensions)) {
        flags |= OC_FLAG_USER_OC;
    } else if (is_user_defined) {
        int added = 0;
        /* see if we have a X-ORIGIN term already */
        while (extensions) {
            if (strcmp(extensions->term, "X-ORIGIN") == 0) {
                charray_add(&extensions->values, slapi_ch_strdup(schema_user_defined_origin[0]));
                extensions->value_count++;
                added = 1;
                break;
            }
            extensions = extensions->next;
        }
        if (!added) {
            /* X-ORIGIN is completely missing, add it */
            extensions = head;
            schemaext *newext = (schemaext *)slapi_ch_calloc(1, sizeof(schemaext));
            newext->term = slapi_ch_strdup("X-ORIGIN");
            charray_add(&newext->values, slapi_ch_strdup(schema_user_defined_origin[0]));
            newext->value_count++;
            while (extensions && extensions->next) {
                extensions = extensions->next;
            }
            if (extensions == NULL) {
                extensions = newext;
                head = extensions;
            } else {
                extensions->next = newext;
            }
        }
        flags |= OC_FLAG_USER_OC;
    } else {
        flags |= OC_FLAG_STANDARD_OC;
    }
    extensions = head; /* reset to the top of the list */
    /*
     *  Generate OrigRequiredAttrsArray and OrigAllowedAttrsArray from the superior oc
     */
    if (psup_oc) {
        int found_it;

        OrigRequiredAttrsArray = (char **)slapi_ch_malloc(1 * sizeof(char *));
        OrigRequiredAttrsArray[0] = NULL;
        OrigAllowedAttrsArray = (char **)slapi_ch_malloc(1 * sizeof(char *));
        OrigAllowedAttrsArray[0] = NULL;
        if (psup_oc->oc_required && objClass->oc_at_oids_must) {
            for (i = 0; objClass->oc_at_oids_must[i]; i++) {
                found_it = 0;
                for (j = 0; psup_oc->oc_required[j]; j++) {
                    if (strcasecmp(psup_oc->oc_required[j], objClass->oc_at_oids_must[i]) == 0) {
                        found_it = 1;
                        break;
                    }
                }
                if (!found_it) {
                    charray_add(&OrigRequiredAttrsArray, slapi_ch_strdup(objClass->oc_at_oids_must[i]));
                }
            }
        } else {
            /* we still need to set the originals */
            charray_free(OrigRequiredAttrsArray);
            OrigRequiredAttrsArray = charray_dup(objClass->oc_at_oids_must);
        }
        if (psup_oc->oc_allowed && objClass->oc_at_oids_may) {
            for (i = 0; objClass->oc_at_oids_may[i]; i++) {
                found_it = 0;
                for (j = 0; psup_oc->oc_allowed[j]; j++) {
                    if (strcasecmp(psup_oc->oc_allowed[j], objClass->oc_at_oids_may[i]) == 0) {
                        found_it = 1;
                        break;
                    }
                }
                if (!found_it) {
                    charray_add(&OrigAllowedAttrsArray, slapi_ch_strdup(objClass->oc_at_oids_may[i]));
                }
            }
        } else {
            /* we still need to set the originals */
            charray_free(OrigAllowedAttrsArray);
            OrigAllowedAttrsArray = charray_dup(objClass->oc_at_oids_may);
        }
    } else {
        /* if no parent oc */
        OrigRequiredAttrsArray = charray_dup(objClass->oc_at_oids_must);
        OrigAllowedAttrsArray = charray_dup(objClass->oc_at_oids_may);
    }
    if (!(schema_flags & DSE_SCHEMA_NO_GLOCK)) {
        oc_unlock(); /* we are done accessing superior oc (psup_oc) */
    }
    /*
     *  Finally - create new the objectclass
     */
    pnew_oc = (struct objclass *)slapi_ch_calloc(1, sizeof(struct objclass));
    pnew_oc->oc_name = slapi_ch_strdup(objClass->oc_names[0]);
    if (objClass->oc_sup_oids) {
        pnew_oc->oc_superior = slapi_ch_strdup(objClass->oc_sup_oids[0]);
    }
    pnew_oc->oc_oid = slapi_ch_strdup(objClass->oc_oid);
    pnew_oc->oc_desc = slapi_ch_strdup(objClass->oc_desc);
    pnew_oc->oc_required = charray_dup(objClass->oc_at_oids_must);
    pnew_oc->oc_allowed = charray_dup(objClass->oc_at_oids_may);
    pnew_oc->oc_orig_required = OrigRequiredAttrsArray;
    pnew_oc->oc_orig_allowed = OrigAllowedAttrsArray;
    pnew_oc->oc_kind = objClass->oc_kind;
    pnew_oc->oc_extensions = extensions;
    pnew_oc->oc_next = NULL;
    pnew_oc->oc_flags = flags;

    *oc = pnew_oc;

done:
    ldap_objectclass_free(objClass);

    return rc;
}

/*
 * schema_check_oc_attrs:
 * Check to see if the required and allowed attributes are valid attributes
 *
 * arguments: poc         : pointer to the objectclass to check
 *            errorbuf    : buffer to write any error messages to
 *            stripOptions: 1 if you want to silently strip any options
 *                          0 if options should cause an error
 *
 * Returns:
 *
 * 0 if there's a unknown attribute, and errorbuf will contain an
 * error message.
 *
 * 1 if everything is ok
 *
 * Note: no locking of poc is needed because poc is always a newly allocated
 * objclass struct (this function is only called by add_oc_internal).
 */
static int
schema_check_oc_attrs(struct objclass *poc,
                      char *errorbuf,
                      size_t errorbufsize,
                      int stripOptions)
{
    int i;

    if (errorbuf == NULL || poc == NULL || poc->oc_name == NULL) {
        /* error */
        slapi_log_err(SLAPI_LOG_PARSE, "schema_check_oc_attrs",
                      "Null args passed to schema_check_oc_attrs\n");
        return -1;
    }

    /* remove any options like ;binary from the oc's attributes */
    if (strip_oc_options(poc) && !stripOptions) {
        /* there were options present, this oc should be rejected */
        schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                               poc->oc_name, "Contains attribute options. "
                                             "Attribute options, such as \";binary\" are not allowed in "
                                             "object class definitions.");
        return 0;
    }

    for (i = 0; poc->oc_allowed && poc->oc_allowed[i]; i++) {
        if (attr_syntax_exists(poc->oc_allowed[i]) == 0) {
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                                   poc->oc_name, "Unknown allowed attribute type \"%s\"",
                                   poc->oc_allowed[i]);
            return 0;
        }
    }
    for (i = 0; poc->oc_required && poc->oc_required[i]; i++) {
        if (attr_syntax_exists(poc->oc_required[i]) == 0) {
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_oc,
                                   poc->oc_name, "Unknown required attribute type \"%s\"",
                                   poc->oc_required[i]);
            return 0;
        }
    }

    return 1;
}

/*
 * schema_check_name:
 * Check if the attribute or objectclass name is valid.  Names can only contain
 * characters, digits, and hyphens. In addition, names must begin with
 * a character. If the nsslapd-attribute-name-exceptions attribute in cn=config
 * is true, then we also allow underscores.
 *
 * XXX We're also supposed to allow semicolons, but we already use them to deal
 *     with attribute options XXX
 *
 * returns 1 if the attribute has a legal name
 *         0 if not
 *
 * If the attribute name is invalid, an error message will be written to msg
 */

static int
schema_check_name(char *name, PRBool isAttribute __attribute__((unused)), char *errorbuf, size_t errorbufsize)
{
    size_t i = 0;

    /* allowed characters */
    static char allowed[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-";

    /* additional characters to allow if allow_exceptions is true */
    static char allowedExceptions[] = "_";
    int allow_exceptions = config_get_attrname_exceptions();

    if (name == NULL || errorbuf == NULL) {
        /* this is bad */
        return 0;
    }

    if (!strcasecmp(name, PSEUDO_ATTR_UNHASHEDUSERPASSWORD)) {
        /* explicitly allow this badly named attribute */
        return 1;
    }

    /* attribute names must begin with a letter */
    if ((isascii(name[0]) == 0) || (isalpha(name[0]) == 0)) {
        if ((strlen(name) + 80) < BUFSIZ) {
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                                   name, "The name is invalid. Names must begin with a letter");
        } else {
            schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                                   name, "The name is invalid, and probably too long. "
                                         "Names must begin with a letter");
        }
        return 0;
    }

    for (i = 1; name[i]; i++) {
        if ((NULL == strchr(allowed, name[i])) &&
            (!allow_exceptions ||
             (NULL == strchr(allowedExceptions, name[i])))) {
            if ((strlen(name) + 80) < BUFSIZ) {
                schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                                       name, "The name contains the invalid character \"%c\"", name[i]);
            } else {
                schema_create_errormsg(errorbuf, errorbufsize, schema_errprefix_at,
                                       name, "The name contains the invalid character \"%c\".  The name"
                                             " is also probably too long.",
                                       name[i]);
            }
            return 0;
        }
    }
    return 1;
}


/*
 * schema_check_oid:
 * Check if the oid is valid.
 *
 * returns 1 if the attribute has a legal oid
 *         0 if not
 *
 * If the oid is invalid, an error message will be written to errorbuf
 *
 * Oids can either have the form <attr/oc name>-oid or
 * start and end with a digit, and contain only digits and periods
 */

static int
schema_check_oid(const char *name, const char *oid, PRBool isAttribute, char *errorbuf, size_t errorbufsize)
{

    int i = 0, length_oid = 0, rc = 0;
    char *namePlusOid = NULL;

    if (name == NULL || oid == NULL) {
        /* this is bad */
        slapi_log_err(SLAPI_LOG_ERR, "schema_check_oid", "NULL passed to schema_check_oid\n");
        return 0;
    }

    /* check to see if the OID is <name>-oid */
    namePlusOid = slapi_ch_smprintf("%s-oid", name);
    rc = strcasecmp(oid, namePlusOid);
    slapi_ch_free((void **)&namePlusOid);

    /*
     * 5160 - if the attribute starts with an x- this can confuse the openldap schema
     * parser. It becomes ambiguous if the x-descr-oid is the oid or a field of the
     * schema. In this case, we reject the oid as it is ambiguous.
     */
    if (0 == rc) {
        if (strncasecmp(name, "X-", 2) == 0) {
            schema_create_errormsg(errorbuf, errorbufsize,
                                   isAttribute ? schema_errprefix_at : schema_errprefix_oc,
                                   name,
                                   "The descr-oid format can not be used with a schema name prefixed with \"X-\". The OID for \"%s\" MUST be a numeric representation.",
                                   name);
            return 0;
        } else {
            return 1;
        }
    }

    /* If not, the OID must begin and end with a digit, and contain only
     digits and dots */

    /* check to see that it begins and ends with a digit */
    length_oid = strlen(oid);
    if (!isdigit(oid[0]) ||
        !isdigit(oid[length_oid - 1])) {
        schema_create_errormsg(errorbuf, errorbufsize,
                               isAttribute ? schema_errprefix_at : schema_errprefix_oc,
                               name,
                               "The OID \"%s\" must begin and end with a digit, or be \"%s-oid\"",
                               oid, name);
        return 0;
    }

    /* check to see that it contains only digits and dots */
    for (i = 0; i < length_oid; i++) {
        if (!isdigit(oid[i]) && oid[i] != '.') {
            schema_create_errormsg(errorbuf, errorbufsize,
                                   isAttribute ? schema_errprefix_at : schema_errprefix_oc,
                                   name,
                                   "The OID \"%s\" contains an invalid character: \"%c\"; the"
                                   " OID must contain only digits and periods, or be \"%s-oid\"",
                                   oid, oid[i], name);
            return 0;
        }
    }

    /* The oid is OK if we're here */
    return 1;
}


/*
 * Some utility functions for dealing with a dynamically
 * allocated buffer.
 */

static struct sizedbuffer *
sizedbuffer_construct(size_t size)
{
    struct sizedbuffer *p = (struct sizedbuffer *)slapi_ch_malloc(sizeof(struct sizedbuffer));
    p->size = size;
    if (size > 0) {
        p->buffer = (char *)slapi_ch_malloc(size);
        p->buffer[0] = '\0';
    } else {
        p->buffer = NULL;
    }
    return p;
}

static void
sizedbuffer_destroy(struct sizedbuffer *p)
{
    if (p != NULL) {
        slapi_ch_free((void **)&p->buffer);
    }
    slapi_ch_free((void **)&p);
}

static void
sizedbuffer_allocate(struct sizedbuffer *p, size_t sizeneeded)
{
    if (p != NULL) {
        if (sizeneeded > p->size) {
            if (p->buffer != NULL) {
                slapi_ch_free((void **)&p->buffer);
            }
            p->buffer = (char *)slapi_ch_malloc(sizeneeded);
            p->buffer[0] = '\0';
            p->size = sizeneeded;
        }
    }
}

/*
 * Check if the object class is extensible
 */
static int
isExtensibleObjectclass(const char *objectclass)
{
    if (strcasecmp(objectclass, "extensibleobject") == 0) {
        return (1);
    }
    /* The Easter Egg is based on a special object class */
    if (strcasecmp(objectclass, EGG_OBJECT_CLASS) == 0) {
        return (1);
    }
    return 0;
}


/*
 * strip_oc_options: strip any attribute options from the objectclass'
 *                   attributes (remove things like ;binary from the attrs)
 *
 * argument: pointer to an objectclass, attributes will have their
 *           options removed in place
 *
 * returns:  number of options removed
 *
 * Note: no locking of poc is needed because poc is always a newly allocated
 * objclass struct (this function is only called by schema_check_oc_attrs,
 * which is only called by add_oc_internal).
 */

static int
strip_oc_options(struct objclass *poc)
{
    int i, numRemoved = 0;
    char *mod = NULL;

    for (i = 0; poc->oc_allowed && poc->oc_allowed[i]; i++) {
        if ((mod = stripOption(poc->oc_allowed[i])) != NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "strip_oc_options", "Removed option \"%s\" from allowed attribute type "
                                              "\"%s\" in object class \"%s\".\n",
                          mod, poc->oc_allowed[i], poc->oc_name);
            numRemoved++;
        }
    }
    for (i = 0; poc->oc_required && poc->oc_required[i]; i++) {
        if ((mod = stripOption(poc->oc_required[i])) != NULL) {
            slapi_log_err(SLAPI_LOG_ERR,
                          "strip_oc_options", "Removed option \"%s\" from required attribute type "
                                              "\"%s\" in object class \"%s\".\n",
                          mod, poc->oc_required[i], poc->oc_name);
            numRemoved++;
        }
    }
    return numRemoved;
}


/*
 * stripOption:
 * removes options such as ";binary" from attribute names
 *
 * argument: pointer to an attribute name, such as "userCertificate;binary"
 *
 * returns: pointer to the option, such as "binary"
 *          NULL if there's no option
 *
 */

static char *
stripOption(char *attr)
{
    char *pSemiColon = strchr(attr, ';');

    if (pSemiColon) {
        *pSemiColon = '\0';
    }

    return pSemiColon ? pSemiColon + 1 : NULL;
}


/*
 * load_schema_dse: called by dse_read_file() when target is cn=schema
 *
 * Initialize attributes and objectclasses from the schema
 *
 * Note that this function removes all values for `attributetypes'
 *        and `objectclasses' attributes from the entry `e'.
 *
 * returntext is always at least SLAPI_DSE_RETURNTEXT_SIZE bytes in size.
 */
int
load_schema_dse(Slapi_PBlock *pb, Slapi_Entry *e, Slapi_Entry *ignored __attribute__((unused)), int *returncode, char *returntext, void *arg)
{
    Slapi_Attr *attr = 0;
    int primary_file = 0; /* this is the primary (writeable) schema file */
    int schema_ds4x_compat = config_get_ds4_compatible_schema();
    PRUint32 flags = *(PRUint32 *)arg;

    *returncode = 0;

    /*
     * Note: there is no need to call schema_lock_write() here because this
     * function is only called during server startup.
     */

    slapi_pblock_get(pb, SLAPI_DSE_IS_PRIMARY_FILE, &primary_file);

    if (!slapi_entry_attr_find(e, "attributetypes", &attr) && attr) {
        /* enumerate the values in attr */
        Slapi_Value *v = 0;
        int index = 0;
        for (index = slapi_attr_first_value(attr, &v);
             v && (index != -1);
             index = slapi_attr_next_value(attr, index, &v)) {
            const char *s = slapi_value_get_string(v);
            if (!s)
                continue;
            if (flags & DSE_SCHEMA_NO_LOAD) {
                struct asyntaxinfo *tmpasip = NULL;
                if ((*returncode = parse_at_str(s, &tmpasip, returntext,
                                                SLAPI_DSE_RETURNTEXT_SIZE, flags,
                                                primary_file /* force user defined? */,
                                                schema_ds4x_compat, 0)) != 0)
                    break;
                attr_syntax_free(tmpasip); /* trash it */
            } else {
                if ((*returncode = parse_at_str(s, NULL, returntext,
                                                SLAPI_DSE_RETURNTEXT_SIZE, flags,
                                                primary_file /* force user defined? */,
                                                schema_ds4x_compat, 0)) != 0)
                    break;
            }
        }
        slapi_entry_attr_delete(e, "attributetypes");
    }

    if (*returncode)
        return SLAPI_DSE_CALLBACK_ERROR;

    flags |= DSE_SCHEMA_NO_GLOCK; /* don't lock global resources
                                     during initialization */
    if (!slapi_entry_attr_find(e, "objectclasses", &attr) && attr) {
        /* enumerate the values in attr */
        Slapi_Value *v = 0;
        int index = 0;
        for (index = slapi_attr_first_value(attr, &v);
             v && (index != -1);
             index = slapi_attr_next_value(attr, index, &v)) {
            struct objclass *oc = 0;
            const char *s = slapi_value_get_string(v);
            if (!s)
                continue;
            if (LDAP_SUCCESS != (*returncode = parse_oc_str(s, &oc, returntext,
                                                            SLAPI_DSE_RETURNTEXT_SIZE, flags,
                                                            primary_file /* force user defined? */,
                                                            schema_ds4x_compat, NULL))) {
                oc_free(&oc);
                break;
            }
            if (flags & DSE_SCHEMA_NO_LOAD) {
                /* we don't load the objectclase; free it */
                oc_free(&oc);
            } else {
                if (LDAP_SUCCESS !=
                    (*returncode = add_oc_internal(oc, returntext,
                                                   SLAPI_DSE_RETURNTEXT_SIZE, schema_ds4x_compat,
                                                   flags))) {
                    oc_free(&oc);
                    break;
                }
            }
        }
        slapi_entry_attr_delete(e, "objectclasses");
    }

    /* Set the schema CSN */
    if (!(flags & DSE_SCHEMA_NO_LOAD) &&
        !slapi_entry_attr_find(e, "nsschemacsn", &attr) && attr) {
        Slapi_Value *v = NULL;
        slapi_attr_first_value(attr, &v);
        if (NULL != v) {
            const char *s = slapi_value_get_string(v);
            if (NULL != s) {
                CSN *csn = csn_new_by_string(s);
                g_set_global_schema_csn(csn);
            }
        }
    }

    return (*returncode == LDAP_SUCCESS) ? SLAPI_DSE_CALLBACK_OK
                                         : SLAPI_DSE_CALLBACK_ERROR;
}

/*
 * Try to initialize the schema from the LDIF file.  Read
 * the file and convert it to the avl tree of DSEs.  If the
 * file doesn't exist, we try to create it and put a minimal
 * schema entry into it.
 *
 * Returns 1 for OK, 0 for Fail.
 *
 * schema_flags:
 * DSE_SCHEMA_NO_LOAD      -- schema won't get loaded
 * DSE_SCHEMA_NO_CHECK     -- schema won't be checked
 * DSE_SCHEMA_NO_BACKEND   -- don't add as backend
 * DSE_SCHEMA_LOCKED       -- already locked; no further lock needed
 */
static int
init_schema_dse_ext(char *schemadir, Slapi_Backend *be, struct dse **local_pschemadse, PRUint32 schema_flags)
{
    int rc = 1; /* OK */
    char *userschemafile = 0;
    char *userschematmpfile = 0;
    char **filelist = 0;
    char *myschemadir = NULL;
    /* SYSTEMSCHEMADIR is set out of the makefile.am -D, from configure.ac */
    char *myschemadirs[2] = {SYSTEMSCHEMADIR, NULL};
    Slapi_DN schema;

    if (NULL == local_pschemadse) {
        return 0; /* cannot proceed; return failure */
    }

    *local_pschemadse = NULL;
    slapi_sdn_init_ndn_byref(&schema, "cn=schema");

    /* get schemadir if not given */
    if (NULL == schemadir) {
        myschemadir = config_get_schemadir();
        if (NULL == myschemadir) {
            return 0; /* cannot proceed; return failure */
        }
    } else {
        myschemadir = schemadir;
    }

    myschemadirs[1] = myschemadir;

    filelist = get_priority_filelist((const char **)myschemadirs, ".*ldif$", 2);
    if (!filelist || !*filelist) {
        slapi_log_err(SLAPI_LOG_ERR, "init_schema_dse_ext",
                      "No schema files were found in the directory %s\n", myschemadir);
        free_filelist(filelist);
        rc = 0;
    } else {
        /* figure out the last file in the list; it is the user schema */
        int ii = 0;
        while (filelist[ii])
            ++ii;
        userschemafile = filelist[ii - 1];
        userschematmpfile = slapi_ch_smprintf("%s.tmp", userschemafile);
    }

    if (rc) {
        *local_pschemadse = dse_new_with_filelist(userschemafile,
                                                  userschematmpfile, NULL, NULL, myschemadir, filelist);
    }
    PR_ASSERT(*local_pschemadse);
    if ((rc = (*local_pschemadse != NULL)) != 0) {
        /* pass schema_flags as arguments */
        dse_register_callback(*local_pschemadse,
                              DSE_OPERATION_READ, DSE_FLAG_PREOP, &schema,
                              LDAP_SCOPE_BASE, NULL,
                              load_schema_dse, (void *)&schema_flags, NULL);
    }
    slapi_ch_free_string(&userschematmpfile);
    if (NULL == schemadir) {
        slapi_ch_free_string(&myschemadir); /* allocated in this function */
    }

    if (rc) {
        char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
        char *attr_str;
        int dont_write = 1;
        int merge = 1;
        int dont_dup_check = 1;
        Slapi_PBlock *pb = slapi_pblock_new();
        /* don't write out the file when reading */
        slapi_pblock_set(pb, SLAPI_DSE_DONT_WRITE_WHEN_ADDING, (void *)&dont_write);
        /* duplicate entries are allowed */
        slapi_pblock_set(pb, SLAPI_DSE_MERGE_WHEN_ADDING, (void *)&merge);
        /* use the non duplicate checking str2entry */
        slapi_pblock_set(pb, SLAPI_DSE_DONT_CHECK_DUPS, (void *)&dont_dup_check);
        /* borrow the task flag space */
        slapi_pblock_set(pb, SLAPI_SCHEMA_FLAGS, (void *)&schema_flags);

        /* add the objectclass attribute so we can do some basic schema
           checking during initialization; this will be overridden when
           its "real" definition is read from the schema conf files */

        attr_str = "( 2.5.4.0 NAME 'objectClass' "
                   "DESC 'Standard schema for LDAP' SYNTAX "
                   "1.3.6.1.4.1.1466.115.121.1.15 X-ORIGIN 'RFC 2252' )";
        if (schema_flags & DSE_SCHEMA_NO_LOAD) {
            struct asyntaxinfo *tmpasip = NULL;
            rc = parse_at_str(attr_str, &tmpasip, errorbuf, sizeof(errorbuf),
                              DSE_SCHEMA_NO_GLOCK | schema_flags, 0, 0, 0);
            attr_syntax_free(tmpasip); /* trash it */
        } else {
            rc = parse_at_str(attr_str, NULL, errorbuf, sizeof(errorbuf),
                              schema_flags, 0, 0, 0);
        }
        if (rc) {
            slapi_log_err(SLAPI_LOG_ERR, "init_schema_dse_ext", "Could not add"
                                                                " attribute type \"objectClass\" to the schema: %s\n",
                          errorbuf);
        }

        rc = dse_read_file(*local_pschemadse, pb);
        slapi_pblock_destroy(pb);
    }

    if (rc && !(schema_flags & DSE_SCHEMA_NO_BACKEND)) {
        if (schema_flags & DSE_SCHEMA_LOCKED) {
            /*
             * Code path for schema reload.
             * To fix the side effect which lowers the case of the
             * reloaded new schema, eliminating normalize_oc_nolock().
             * Note that the normalization is not needed since all
             * the checks are done by strcasecmp.
             */
            ;
        } else {
            /* make sure the schema is normalized */
            normalize_oc();
        }

        /* register callbacks */
        dse_register_callback(*local_pschemadse, SLAPI_OPERATION_SEARCH,
                              DSE_FLAG_PREOP, &schema, LDAP_SCOPE_BASE,
                              NULL, read_schema_dse, NULL, NULL);
        dse_register_callback(*local_pschemadse, SLAPI_OPERATION_MODIFY,
                              DSE_FLAG_PREOP, &schema, LDAP_SCOPE_BASE,
                              NULL, modify_schema_dse, NULL, NULL);
        dse_register_callback(*local_pschemadse, SLAPI_OPERATION_DELETE,
                              DSE_FLAG_PREOP, &schema, LDAP_SCOPE_BASE,
                              NULL, dont_allow_that, NULL, NULL);
        dse_register_callback(*local_pschemadse, DSE_OPERATION_WRITE,
                              DSE_FLAG_PREOP, &schema, LDAP_SCOPE_BASE,
                              NULL, refresh_user_defined_schema, NULL, NULL);

        if (rc) {
            if (NULL == be) { /* be is not given. select it */
                be = slapi_be_select_by_instance_name(DSE_SCHEMA);
            }
            if (NULL == be) { /* first time */
                /* add as a backend */
                be = be_new_internal(*local_pschemadse, "DSE", DSE_SCHEMA, &schema_plugin);
                be_addsuffix(be, &schema);
            } else { /* schema file reload */
                be_replace_dse_internal(be, *local_pschemadse);
            }
        }
    }

    slapi_sdn_done(&schema);
    return rc;
}

int
init_schema_dse(const char *configdir)
{
    char *schemadir = config_get_schemadir();
    int rc = 0;
    if (NULL == schemadir) {
        schemadir = slapi_ch_smprintf("%s/%s", configdir, SCHEMA_SUBDIR_NAME);
    }
    rc = init_schema_dse_ext(schemadir, NULL, &pschemadse, DSE_SCHEMA_NO_GLOCK);
    slapi_ch_free_string(&schemadir);
    return rc;
}

/*
 * sprintf to `outp' the contents of `tag' followed by `oid' followed by a
 * trailing space.  If enquote is non-zero, single quotes are included
 * around the `oid' string.  If `suffix' is not NULL, it is output directly
 * after the `oid' (before the trailing space).
 * Note that `tag' should typically include a trailing space, e.g.,
 *
 *      outp += put_tagged_oid( outp, "SUP ", "1.2.3.4", NULL, enquote_oids );
 *
 * Returns the number of bytes copied to `outp' or 0 if `oid' is NULL.
 */
static int
put_tagged_oid(char *outp, const char *tag, const char *oid, const char *suffix, int enquote)
{
    int count = 0;

    if (NULL == suffix) {
        suffix = "";
    }

    if (NULL != oid) {
        if (enquote) {
            count = sprintf(outp, "%s'%s%s' ", tag, oid, suffix);
        } else {
            count = sprintf(outp, "%s%s%s ", tag, oid, suffix);
        }
    }

    return (count);
}


/*
 * Add to `buf' a string of the form:
 *
 *    prefix SPACE ( oid1 $ oid2 ... ) SPACE
 * OR
 *    prefix SPACE oid SPACE
 *
 * The part after <prefix> matches the `oids' definition
 *      from RFC 2252 section 4.1.
 *
 * If oids is NULL or an empty array, `buf' is not touched.
 */
static void
strcat_oids(char *buf, char *prefix, char **oids, int schema_ds4x_compat)
{
    char *p;
    int i;

    if (NULL != oids && NULL != oids[0]) {
        p = buf + strlen(buf); /* skip past existing content */
        if (NULL == oids[1] && !schema_ds4x_compat) {
            sprintf(p, "%s %s ", prefix, oids[0]); /* just one oid */
        } else {
            sprintf(p, "%s ( ", prefix); /* oidlist */
            for (i = 0; oids[i] != NULL; ++i) {
                if (i > 0) {
                    strcat(p, " $ ");
                }
                strcat(p, oids[i]);
            }
            strcat(p, " ) ");
        }
    }
}

/*
 * Add to `buf' a string of the form:
 *
 *    prefix SPACE ( 's1' 's2' ... ) SPACE
 * OR
 *    prefix SPACE 's1' SPACE
 *
 * The part after <prefix> matches the qdescs definition
 *      from RFC 2252 section 4.1.
 *
 * A count of the number of bytes added to buf or needed is returned.
 *
 * If buf is NULL, no copying is done but the number of bytes needed
 * is calculated and returned.  This is useful if you need to allocate
 * space before calling this function will a buffer.
 *
 */
static size_t
strcat_qdlist(char *buf, char *prefix, char **qdlist)
{
    int i;
    char *start, *p;
    size_t len = 0;

    if (NULL != qdlist && NULL != qdlist[0]) {
        if (NULL == buf) { /* calculate length only */
            len += strlen(prefix);
            if (NULL != qdlist[1]) {
                len += 4; /* surrounding spaces and '(' and ')' */
            }
            for (i = 0; NULL != qdlist[i]; ++i) {
                len += 3; /* leading space and quote marks */
                len += strlen(qdlist[i]);
            }
            ++len; /* trailing space */

        } else {
            p = start = buf + strlen(buf); /* skip past existing content */
            if (NULL == qdlist[1]) {       /* just one string */
                p += sprintf(p, "%s '%s' ", prefix, qdlist[0]);
            } else { /* a list of strings */
                p += sprintf(p, "%s (", prefix);
                for (i = 0; qdlist[i] != NULL; ++i) {
                    p += sprintf(p, " '%s'", qdlist[i]);
                }
                *p++ = ' ';
                *p++ = ')';
                *p++ = ' ';
                *p = '\0';
            }
            len = p - start;
        }
    }

    return (len);
}

/*
 *  Loop over the extensions calling strcat_qdlist for each one.
 */
static size_t
strcat_extensions(char *buf, schemaext *extension)
{
    size_t len = 0;

    while (extension) {
        len += strcat_qdlist(buf, extension->term, extension->values);
        extension = extension->next;
    }

    return (len);
}

/*
 * Just like strlen() except that 0 is returned if `s' is NULL.
 */
static size_t
strlen_null_ok(const char *s)
{
    if (NULL == s) {
        return (0);
    }

    return (strlen(s));
}


/*
 * Like strcpy() except a count of the number of bytes copied is returned.
 */
static int
strcpy_count(char *dst, const char *src)
{
    char *p;

    p = dst;
    while (*src != '\0') {
        *p++ = *src++;
    }

    *p = '\0';
    return (p - dst);
}

static int
extension_is_user_defined(schemaext *extensions)
{
    while (extensions) {
        if (strcasecmp(extensions->term, "X-ORIGIN") == 0) {
            int i = 0;
            while (extensions->values && extensions->values[i]) {
                if (strcasecmp(schema_user_defined_origin[0], extensions->values[i]) == 0) {
                    return 1;
                }
                i++;
            }
        }
        extensions = extensions->next;
    }

    return 0;
}


/*
 * Return PR_TRUE if the attribute type named 'type' is one of those that
 * we handle directly in this file (in the scheme DSE callbacks).
 * Other types are handled by the generic DSE code in dse.c.
 */
/* subschema DSE attribute types we handle within the DSE callback */
static char *schema_interesting_attr_types[] = {
    "dITStructureRules",
    "nameForms",
    "dITContentRules",
    "objectClasses",
    "attributeTypes",
    "matchingRules",
    "matchingRuleUse",
    "ldapSyntaxes",
    "nsschemacsn",
    NULL};


static PRBool
schema_type_is_interesting(const char *type)
{
    int i;

    for (i = 0; schema_interesting_attr_types[i] != NULL; ++i) {
        if (0 == strcasecmp(type, schema_interesting_attr_types[i])) {
            return PR_TRUE;
        }
    }

    return PR_FALSE;
}


static void
schema_create_errormsg(
    char *errorbuf,
    size_t errorbufsize,
    const char *prefix,
    const char *name,
    const char *fmt,
    ...)
{
    if (NULL != errorbuf) {
        va_list ap;
        int rc = 0;

        va_start(ap, fmt);

        if (NULL != name) {
            rc = PR_snprintf(errorbuf, errorbufsize, prefix, name);
        }
        /* ok to cast here because rc is positive */
        if ((rc >= 0) && ((size_t)rc < errorbufsize)) {
            (void)PR_vsnprintf(errorbuf + rc, errorbufsize - rc - 1, fmt, ap);
        }
        va_end(ap);
    }
}


/*
 * va_locate_oc_val finds an objectclass within the array of values in va.
 * First oc_name is used, falling back to oc_oid.  oc_oid can be NULL.
 * oc_name and oc_oid should be official names (no trailing spaces). But
 * trailing spaces within the va are ignored if appropriate.
 *
 * Returns >=0 if found (index into va) and -1 if not found.
 */
static int
va_locate_oc_val(Slapi_Value **va, const char *oc_name, const char *oc_oid)
{
    int i;
    const char *strval;

    if (NULL == va || oc_name == NULL) { /* nothing to look for */
        return -1;
    }

    if (!schema_ignore_trailing_spaces) {
        for (i = 0; va[i] != NULL; i++) {
            strval = slapi_value_get_string(va[i]);
            if (NULL != strval) {
                if (0 == strcasecmp(strval, oc_name)) {
                    return i;
                }

                if (NULL != oc_oid && 0 == strcasecmp(strval, oc_oid)) {
                    return i;
                }
            }
        }
    } else {
        /*
         * Ignore trailing spaces when comparing object class names.
         */
        size_t len;
        const char *p;

        for (i = 0; va[i] != NULL; i++) {
            strval = slapi_value_get_string(va[i]);
            if (NULL != strval) {
                for (p = strval, len = 0; (*p != '\0') && (*p != ' ');
                     p++, len++) {
                    ; /* NULL */
                }

                if (0 == strncasecmp(oc_name, strval, len) && (len == strlen(oc_name))) {
                    return i;
                }

                if (NULL != oc_oid && (0 == strncasecmp(oc_oid, strval, len)) && (len == strlen(oc_oid))) {
                    return i;
                }
            }
        }
    }

    return -1; /* not found */
}


/*
 * va_expand_one_oc is used to add missing superclass values to the
 * objectclass attribute when an entry is added or modified.
 *
 * missing values are always added to the end of the 'vap' array.
 *
 * Note: calls to this function MUST be bracketed by lock()/unlock(), i.e.,
 *
 *      oc_lock_read();
 *      va_expand_one_oc( b, o );
 *      oc_unlock();
 */
static void
va_expand_one_oc(const char *dn, const Slapi_Attr *a, Slapi_ValueSet *vs, const char *ocs)
{
    struct objclass *this_oc, *sup_oc;
    int p;
    Slapi_Value **va = vs->va;


    this_oc = oc_find_nolock(ocs, NULL, PR_FALSE);

    if (this_oc == NULL) {
        return; /* skip unknown object classes */
    }

    if (this_oc->oc_superior == NULL) {
        return; /* no superior */
    }

    sup_oc = oc_find_nolock(this_oc->oc_superior, NULL, PR_FALSE);
    if (sup_oc == NULL) {
        return; /* superior is unknown -- ignore */
    }

    p = va_locate_oc_val(va, sup_oc->oc_name, sup_oc->oc_oid);

    if (p != -1) {
        return; /* value already present -- done! */
    }

    if (slapi_valueset_count(vs) > 1000) {
        return;
    }

    slapi_valueset_add_attr_value_ext(a, vs, slapi_value_new_string(sup_oc->oc_name), SLAPI_VALUE_FLAG_PASSIN);

    slapi_log_err(SLAPI_LOG_TRACE, "va_expand_one_oc",
                  "Entry \"%s\": added missing objectClass value %s\n",
                  dn, sup_oc->oc_name);
}


/*
 * Expand the objectClass values in 'e' to take superior classes into account.
 * All missing superior classes are added to the objectClass attribute, as
 * is 'top' if it is missing.
 */
static void
schema_expand_objectclasses_ext(Slapi_Entry *e, int lock)
{
    Slapi_Attr *sa;
    Slapi_Value *v;
    Slapi_ValueSet *vs;
    const char *dn = slapi_entry_get_dn_const(e);
    int i;

    if (0 != slapi_entry_attr_find(e, SLAPI_ATTR_OBJECTCLASS, &sa)) {
        return; /* no OC values -- nothing to do */
    }

    vs = &sa->a_present_values;
    if (slapi_valueset_isempty(vs)) {
        return; /* no OC values -- nothing to do */
    }

    if (lock)
        oc_lock_read();

    /*
     * This loop relies on the fact that bv_expand_one_oc()
     * always adds to the end
     */
    i = slapi_valueset_first_value(vs, &v);
    while (v != NULL) {
        if (NULL != slapi_value_get_string(v)) {
            va_expand_one_oc(dn, sa, &sa->a_present_values, slapi_value_get_string(v));
        }
        i = slapi_valueset_next_value(vs, i, &v);
    }

    /* top must always be present */
    va_expand_one_oc(dn, sa, &sa->a_present_values, "top");
    if (lock)
        oc_unlock();
}
void
slapi_schema_expand_objectclasses(Slapi_Entry *e)
{
    schema_expand_objectclasses_ext(e, 1);
}

void
schema_expand_objectclasses_nolock(Slapi_Entry *e)
{
    schema_expand_objectclasses_ext(e, 0);
}

/* lock to protect both objectclass and schema_dse */
static void
reload_schemafile_lock(void)
{
    oc_lock_write();
    schema_dse_lock_write();
}

static void
reload_schemafile_unlock(void)
{
    schema_dse_unlock();
    oc_unlock();
}

/* API to validate the schema files */
int
slapi_validate_schema_files(char *schemadir)
{
    struct dse *my_pschemadse = NULL;
    int rc = init_schema_dse_ext(schemadir, NULL, &my_pschemadse,
                                 DSE_SCHEMA_NO_LOAD | DSE_SCHEMA_NO_BACKEND);
    dse_destroy(my_pschemadse); /* my_pschemadse was created just to
                                   validate the schema */
    if (rc) {
        return LDAP_SUCCESS;
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "schema_reload",
                      "slapi_validate_schema_files failed\n");
        return LDAP_OBJECT_CLASS_VIOLATION;
    }
}

/*
 * Reload the schema files
 *
 * This is only called from the schema_reload task.  The flag DSE_SCHEMA_LOCKED
 * is also only set been called from this function.  To not interrupt clients
 * we will  rebuild the schema in separate hash tables, and then swap the
 * hash tables once the schema is completely reloaded.  We use the DSE_SCHEMA_LOCKED
 * flag to tell the attribute syntax functions to use the temporary hashtables.
 */
int
slapi_reload_schema_files(char *schemadir)
{
    int rc = LDAP_SUCCESS;
    struct dse *my_pschemadse = NULL;
    /* get be to lock */
    Slapi_Backend *be = slapi_be_select_by_instance_name(DSE_SCHEMA);

    if (NULL == be) {
        slapi_log_err(SLAPI_LOG_ERR, "schema_reload",
                      "slapi_reload_schema_files failed\n");
        return LDAP_LOCAL_ERROR;
    }
    slapi_be_Wlock(be); /* be lock must be outer of schemafile lock */
    reload_schemafile_lock();
    oc_delete_all_nolock();
    rc = init_schema_dse_ext(schemadir, be, &my_pschemadse,
                             DSE_SCHEMA_NO_CHECK | DSE_SCHEMA_LOCKED);
    if (rc) {
        /*
         * The schema has been reloaded into the temporary hash tables.
         * Take the write lock, wipe out the existing hash tables, and
         * swap in the new ones.
         */
        attr_syntax_write_lock();
        attr_syntax_delete_all_for_schemareload(SLAPI_ATTR_FLAG_KEEP);
        attr_syntax_swap_ht();
        attr_syntax_unlock_write();
        slapi_reload_internal_attr_syntax();

        dse_destroy(pschemadse);
        pschemadse = my_pschemadse;
        reload_schemafile_unlock();
        slapi_be_Unlock(be);
        return LDAP_SUCCESS;
    } else {
        reload_schemafile_unlock();
        slapi_be_Unlock(be);
        slapi_log_err(SLAPI_LOG_ERR, "schema_reload",
                      "slapi_reload_schema_files failed\n");
        return LDAP_LOCAL_ERROR;
    }
}

/*
 * slapi_schema_list_objectclass_attributes:
 *         Return the list of attributes belonging to the objectclass
 *
 * The caller is responsible to free the returned list with charray_free.
 * flags: one of them or both:
 *         SLAPI_OC_FLAG_REQUIRED
 *         SLAPI_OC_FLAG_ALLOWED
 */
char **
slapi_schema_list_objectclass_attributes(const char *ocname_or_oid,
                                         PRUint32 flags)
{
    struct objclass *oc = NULL;
    char **attrs = NULL;
    PRUint32 mask = SLAPI_OC_FLAG_REQUIRED | SLAPI_OC_FLAG_ALLOWED;

    if (!flags) {
        return attrs;
    }

    oc_lock_read();
    oc = oc_find_nolock(ocname_or_oid, NULL, PR_FALSE);
    if (oc) {
        switch (flags & mask) {
        case SLAPI_OC_FLAG_REQUIRED:
            attrs = charray_dup(oc->oc_required);
            break;
        case SLAPI_OC_FLAG_ALLOWED:
            attrs = charray_dup(oc->oc_allowed);
            break;
        case SLAPI_OC_FLAG_REQUIRED | SLAPI_OC_FLAG_ALLOWED:
            attrs = charray_dup(oc->oc_required);
            charray_merge(&attrs, oc->oc_allowed, 1 /*copy_strs*/);
            break;
        default:
            slapi_log_err(SLAPI_LOG_ERR, "slapi_schema_list_objectclass_attributes",
                          "flag 0x%x not supported\n", flags);
            break;
        }
    }
    oc_unlock();
    return attrs;
}

/*
 * slapi_schema_get_superior_name:
 *         Return the name of the superior objectclass
 *
 * The caller is responsible to free the returned name
 */
char *
slapi_schema_get_superior_name(const char *ocname_or_oid)
{
    struct objclass *oc = NULL;
    char *superior = NULL;

    oc_lock_read();
    oc = oc_find_nolock(ocname_or_oid, NULL, PR_FALSE);
    if (oc) {
        superior = slapi_ch_strdup(oc->oc_superior);
    }
    oc_unlock();
    return superior;
}

/* Check if the oc_list1 is a superset of oc_list2.
 * oc_list1 is a superset if it exists objectclass in oc_list1 that
 * do not exist in oc_list2. Or if a OC in oc_list1 required more attributes
 * that the OC in oc_list2. Or if a OC in oc_list1 allowed more attributes
 * that the OC in oc_list2.
 *
 * It returns 1 if oc_list1 is a superset of oc_list2, else it returns 0
 *
 * If oc_list1 or oc_list2 is global_oc, the caller must hold the oc_lock
 */
static int
schema_oc_superset_check(struct objclass *oc_list1, struct objclass *oc_list2, char *message, int replica_role)
{
    struct objclass *oc_1, *oc_2;
    const char *description;
    int debug_logging = 0;
    int rc;
    int repl_schema_policy;

    if (message == NULL) {
        description = (const char *)"";
    } else {
        description = (const char *)message;
    }

    /* by default assum oc_list1 == oc_list2 */
    rc = 0;

    /* Are we doing replication logging */
    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        debug_logging = 1;
    }

    /* Check if all objectclass in oc_list1
         *   - exists in oc_list2
         *   - required attributes are also required in oc_2
         *   - allowed attributes are also allowed in oc_2
         */
    slapi_rwlock_rdlock(schema_policy_lock);
    for (oc_1 = oc_list1; oc_1 != NULL; oc_1 = oc_1->oc_next) {


        /* Check if there is a specific policy for that objectclass */
        repl_schema_policy = schema_check_policy(replica_role, REPL_SCHEMA_OBJECTCLASS, oc_1->oc_name, oc_1->oc_oid);
        if (repl_schema_policy == REPL_SCHEMA_UPDATE_ACCEPT_VALUE) {
            /* We are skipping the superset checking for that objectclass */
            slapi_log_err(SLAPI_LOG_REPL, "schema_oc_superset_check",
                          "Do not check if this OBJECTCLASS is missing on local/remote schema [%s or %s]\n", oc_1->oc_name, oc_1->oc_oid);
            continue;
        } else if (repl_schema_policy == REPL_SCHEMA_UPDATE_REJECT_VALUE) {
            /* This objectclass being present, we need to fail as if it was a superset
                         * keep evaluating to have all the objectclass checking
                         */
            slapi_log_err(SLAPI_LOG_REPL, "schema_oc_superset_check",
                          "%s objectclass prevents replication of the schema\n", oc_1->oc_name);
            rc = 1;
            if (debug_logging) {
                /* we continue to check all the objectclasses so we log what is wrong */
                continue;
            } else {
                break;
            }
        }

        /* Retrieve the remote objectclass in our local schema */
        oc_2 = oc_find_nolock(oc_1->oc_oid, oc_list2, PR_TRUE);
        if (oc_2 == NULL) {
            /* try to retrieve it with the name*/
            oc_2 = oc_find_nolock(oc_1->oc_name, oc_list2, PR_TRUE);
        }
        if (oc_2) {
            if (schema_oc_compare(oc_1, oc_2, description) > 0) {
                rc = 1;
                if (debug_logging) {
                    if (replica_role == REPL_SCHEMA_AS_CONSUMER) {
                        slapi_log_err(SLAPI_LOG_REPL, "schema_oc_superset_check",
                                      "Local %s schema objectclasses is a superset of"
                                      " the received one.\n",
                                      oc_1->oc_name);
                    } else {
                        slapi_log_err(SLAPI_LOG_REPL, "schema_oc_superset_check",
                                      "Remote %s schema objectclasses is a superset of"
                                      " the received one.\n",
                                      oc_1->oc_name);
                    }
                    continue;
                } else {
                    break;
                }
            }
        } else {
            slapi_log_err(SLAPI_LOG_REPL, "schema_oc_superset_check",
                          "Fail to retrieve in the %s schema [%s or %s]\n",
                          description,
                          oc_1->oc_name,
                          oc_1->oc_oid);

            /* The oc_1 objectclasses is supperset */
            rc = 1;
            if (debug_logging) {
                /* we continue to check all the objectclasses so we log what is wrong */
                continue;
            } else {
                break;
            }
        }
    }
    slapi_rwlock_unlock(schema_policy_lock);

    return rc;
}

static char *
schema_oc_to_string(struct objclass *oc)
{
    char *oc_str;
    int i;
    int size = 0;

    /* Compute the size of the string that can contain
     * the oc definition and allocates it
     */
    if (oc->oc_oid)
        size += strlen(oc->oc_oid);
    if (oc->oc_name)
        size += strlen(oc->oc_name);
    if (oc->oc_desc)
        size += strlen(oc->oc_desc);
    if (oc->oc_orig_required) {
        for (i = 0; oc->oc_orig_required[i] != NULL; i++) {
            size += strlen(oc->oc_orig_required[i]);
            size += 3;
        }
    }
    if (oc->oc_orig_allowed) {
        for (i = 0; oc->oc_orig_allowed[i] != NULL; i++) {
            size += strlen(oc->oc_orig_allowed[i]);
            size += 3;
        }
    }
    size += strlen(schema_oc_kind_strings_with_spaces[oc->oc_kind]);

    size += 128; /* for all keywords: NAME, DESC, SUP... */
    if ((oc_str = (char *)slapi_ch_calloc(1, size)) == NULL) {
        return NULL;
    }

    /* OID + name */
    sprintf(oc_str, "( %s NAME '%s'", (oc->oc_oid) ? oc->oc_oid : "", oc->oc_name);

    /* description */
    strcat(oc_str, " DESC '");
    if (oc->oc_desc) {
        strcat(oc_str, oc->oc_desc);
    }
    strcat(oc_str, "'");

    /* SUP */
    if (oc->oc_superior) {
        strcat(oc_str, " SUP '");
        strcat(oc_str, oc->oc_superior);
        strcat(oc_str, "'");
    }

    /* oc_kind */
    strcat(oc_str, schema_oc_kind_strings_with_spaces[oc->oc_kind]);

    /* MUST */
    if (oc->oc_orig_required) {
        strcat(oc_str, " MUST ( ");
        for (i = 0; oc->oc_orig_required[i] != NULL; ++i) {
            if (i > 0) {
                strcat(oc_str, " $ ");
            }
            strcat(oc_str, oc->oc_orig_required[i]);
        }
        strcat(oc_str, " ) ");
    }

    /* MAY */
    if (oc->oc_orig_allowed) {
        strcat(oc_str, " MAY ( ");
        for (i = 0; oc->oc_orig_allowed[i] != NULL; ++i) {
            if (i > 0) {
                strcat(oc_str, " $ ");
            }
            strcat(oc_str, oc->oc_orig_allowed[i]);
        }
        strcat(oc_str, " ) ");
    }

    /* flags */
    if (oc->oc_flags & OC_FLAG_USER_OC) {
        strcat(oc_str, " X-ORIGIN 'blahblahblah'");
    }

    strcat(oc_str, " )");
    slapi_log_err(SLAPI_LOG_REPL, "schema_oc_to_string",
                  "Replace (old[%d]=%s)\n", size, oc_str);

    return (oc_str);
}
/* call must hold oc_lock at least in read */
static struct schema_mods_indexes *
schema_list_oc2learn(struct objclass *oc_remote_list, struct objclass *oc_local_list, int replica_role)
{
    struct objclass *oc_remote, *oc_local;
    struct schema_mods_indexes *head = NULL, *mods_index;
    struct schema_mods_indexes *tail = NULL;
    int index = 0;
    int repl_schema_policy;
    const char *message;

    if (replica_role == REPL_SCHEMA_AS_SUPPLIER) {
        message = (const char *)"remote consumer";
    } else {
        message = (const char *)"remote supplier";
    }

    slapi_rwlock_rdlock(schema_policy_lock);
    for (oc_remote = oc_remote_list; oc_remote != NULL; oc_remote = oc_remote->oc_next, index++) {

        /* If this objectclass is not checked (accept) or rejects schema update */
        repl_schema_policy = schema_check_policy(replica_role, REPL_SCHEMA_OBJECTCLASS, oc_remote->oc_name, oc_remote->oc_oid);
        if ((repl_schema_policy == REPL_SCHEMA_UPDATE_ACCEPT_VALUE) || (repl_schema_policy == REPL_SCHEMA_UPDATE_REJECT_VALUE)) {
            continue;
        }


        oc_local = oc_find_nolock(oc_remote->oc_oid, oc_local_list, PR_TRUE);
        if (oc_local == NULL) {
            /* try to retrieve it with the name*/
            oc_local = oc_find_nolock(oc_remote->oc_name, oc_local_list, PR_TRUE);
        }
        if ((oc_local == NULL) ||
            (schema_oc_compare(oc_local, oc_remote, message) < 0)) {
            /* This replica does not know this objectclass, It needs to be added */
            slapi_log_err(SLAPI_LOG_REPL, "schema_list_oc2learn",
                          "Add that unknown/extended objectclass %s (%s)\n",
                          oc_remote->oc_name,
                          oc_remote->oc_oid);

            if ((mods_index = (struct schema_mods_indexes *)slapi_ch_calloc(1, sizeof(struct schema_mods_indexes))) == NULL) {
                slapi_log_err(SLAPI_LOG_ERR, "schema_list_oc2learn",
                              "Fail to Add (no memory) objectclass %s (%s)\n",
                              oc_remote->oc_name,
                              oc_remote->oc_oid);
                continue;
            }

            /* insert it at the end of the list
                         * to keep the order of the original schema
                         * For example superior oc should be declared first
                         */
            mods_index->index = index;
            mods_index->next = NULL;
            mods_index->new_value = NULL;
            if (oc_local) {
                mods_index->old_value = schema_oc_to_string(oc_local);
            }
            if (head == NULL) {
                head = mods_index;
            } else {
                tail->next = mods_index;
            }
            tail = mods_index;
        }
    }
    slapi_rwlock_unlock(schema_policy_lock);
    return head;
}
static struct schema_mods_indexes *
schema_list_attr2learn(struct asyntaxinfo *at_list_local, struct asyntaxinfo *at_list_remote, int replica_role)
{
    struct asyntaxinfo *at_remote, *at_local;
    struct schema_mods_indexes *head = NULL, *mods_index;
    int index = 0;
    int repl_schema_policy;
    int debug_logging = 0;
    char *message;

    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        debug_logging = 1;
    }

    if (replica_role == REPL_SCHEMA_AS_SUPPLIER) {
        message = "remote consumer";
    } else {
        message = "remote supplier";
    }

    slapi_rwlock_rdlock(schema_policy_lock);
    for (at_remote = at_list_remote; at_remote != NULL; at_remote = at_remote->asi_next, index++) {
        /* If this objectclass is not checked (accept) or rejects schema update */
        repl_schema_policy = schema_check_policy(replica_role, REPL_SCHEMA_ATTRIBUTE, at_remote->asi_name, at_remote->asi_oid);
        ;
        if ((repl_schema_policy == REPL_SCHEMA_UPDATE_ACCEPT_VALUE) || (repl_schema_policy == REPL_SCHEMA_UPDATE_REJECT_VALUE)) {
            continue;
        }

        if (((at_local = attr_syntax_find(at_remote, at_list_local)) == NULL) ||
            (schema_at_compare(at_local, at_remote, message, debug_logging) < 0)) {
            /* This replica does not know this attribute, It needs to be added */
            slapi_log_err(SLAPI_LOG_REPL, "schema_list_attr2learn",
                          "Add that unknown/extended attribute %s (%s)\n",
                          at_remote->asi_name,
                          at_remote->asi_oid);

            if ((mods_index = (struct schema_mods_indexes *)slapi_ch_calloc(1, sizeof(struct schema_mods_indexes))) == NULL) {
                slapi_log_err(SLAPI_LOG_ERR, "schema_list_attr2learn",
                              "Fail to Add (no memory) attribute %s (%s)\n",
                              at_remote->asi_name,
                              at_remote->asi_oid);
                break;
            }

            /* insert it in the list */
            mods_index->index = index;
            mods_index->next = head;
            mods_index->new_value = NULL;
            head = mods_index;
        }
    }
    slapi_rwlock_unlock(schema_policy_lock);
    return head;
}

/* If oc_1 > oc2  returns 1
 * else it returns 0
 */
static PRBool
schema_oc_compare_strict(struct objclass *oc_1, struct objclass *oc_2, const char *description)
{
    int found;
    int i, j;
    PRBool moved_must_to_may;

    /* safety checking */
    if (!oc_1) {
        return 0;
    } else if (!oc_2) {
        return 1;
    }


    /* First check the MUST */
    if (oc_1->oc_orig_required) {
        for (i = 0; oc_1->oc_orig_required[i] != NULL; i++) {
            /* For each required attribute from oc1 schema check that
                         * it is also required in the oc2 schema
                         */
            found = 0;
            if (oc_2->oc_orig_required) {
                for (j = 0; oc_2->oc_orig_required[j] != NULL; j++) {
                    if (strcasecmp(oc_2->oc_orig_required[j], oc_1->oc_orig_required[i]) == 0) {
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) {
                /* Before stating that oc1 is a superset of oc2, we need to verify that the 'required'
                                 * attribute (from oc1) is missing in 'required' oc2 because it is
                                 * now 'allowed' in oc2
                                 */
                moved_must_to_may = PR_FALSE;
                if (oc_2->oc_orig_allowed) {
                    for (j = 0; oc_2->oc_orig_allowed[j] != NULL; j++) {
                        /* coverity[copy_paste_error] */
                        if (strcasecmp(oc_2->oc_orig_allowed[j], oc_1->oc_orig_required[i]) == 0) {
                            moved_must_to_may = PR_TRUE;
                            break;
                        }
                    }
                }
                if (moved_must_to_may) {
                    /* This is a special case where oc1 is actually NOT a superset of oc2 */
                    slapi_log_err(SLAPI_LOG_REPL, "schema_oc_compare_strict", "Attribute %s is no longer 'required' in '%s' of the %s schema but is now 'allowed'\n",
                                  oc_1->oc_orig_required[i],
                                  oc_1->oc_name,
                                  description);
                } else {
                    /* The required attribute in the oc1
                                         * is not required in the oc2
                                         */
                    slapi_log_err(SLAPI_LOG_REPL, "schema_oc_compare_strict", "Attribute %s is not required in '%s' of the %s schema\n",
                                  oc_1->oc_orig_required[i],
                                  oc_1->oc_name,
                                  description);

                    /* The oc_1 objectclasses is supperset */
                    return 1;
                }
            }
        }
    }

    /* Second check the MAY */
    if (oc_1->oc_orig_allowed) {
        for (i = 0; oc_1->oc_orig_allowed[i] != NULL; i++) {
            /* For each required attribute from the remote schema check that
                         * it is also required in the local schema
                         */
            found = 0;
            if (oc_2->oc_orig_allowed) {
                for (j = 0; oc_2->oc_orig_allowed[j] != NULL; j++) {
                    if (strcasecmp(oc_2->oc_orig_allowed[j], oc_1->oc_orig_allowed[i]) == 0) {
                        found = 1;
                        break;
                    }
                }
            }
            if (!found) {
                /* The allowed attribute in the remote schema (remote_oc->oc_orig_allowed[i])
                                 * is not allowed in the local schema
                                 */
                slapi_log_err(SLAPI_LOG_REPL, "schema_oc_compare_strict", "Attribute %s is not allowed in '%s' of the %s schema\n",
                              oc_1->oc_orig_allowed[i],
                              oc_1->oc_name,
                              description);

                /* The oc_1 objectclasses is superset */
                return 1;
            }
        }
    }


    return 0;
}


/* Compare two objectclass definitions
 * it compares:

 * It returns:
 *   1: if oc_1 is a superset of oc_2
 *  -1: if oc_2 is a superset of oc_1
 *   0: if oc_1 and at_2 are equivalent
 */
static int
schema_oc_compare(struct objclass *oc_1, struct objclass *oc_2, const char *description)
{
    if (schema_oc_compare_strict(oc_1, oc_2, description) > 0) {
        return 1;
    } else if (schema_oc_compare_strict(oc_2, oc_1, description) > 0) {
        return -1;
    } else {
        return 0;
    }
}

/* Compare two attributes definitions
 * it compares:
 *  - single/multi value
 *  - syntax
 *  - matching rules
 * It returns:
 *   1: if at_1 is a superset of at_2
 *  -1: if at_2 is a superset of at_1
 *   0: if at_1 and at_2 are equivalent
 */
static int
schema_at_compare(struct asyntaxinfo *at_1, struct asyntaxinfo *at_2, char *message, int debug_logging)
{
    char *info = NULL;

    /* safety checking */
    if (!at_1) {
        if (!at_2) {
            return 0;
        } else {
            return -1;
        }
    } else if (!at_2) {
        return 1;
    }

    /*
         *  Check for single vs. multi value
         */
    if (!(at_1->asi_flags & SLAPI_ATTR_FLAG_SINGLE) && (at_2->asi_flags & SLAPI_ATTR_FLAG_SINGLE)) {

        /* at_1 is a superset */
        if (debug_logging) {
            slapi_log_err(SLAPI_LOG_REPL, "schema_at_compare", "%s schema attribute [%s] is not "
                                                               "\"single-valued\" \n",
                          message, at_1->asi_name);
        }
        return 1;
    }
    if ((at_1->asi_flags & SLAPI_ATTR_FLAG_SINGLE) && !(at_2->asi_flags & SLAPI_ATTR_FLAG_SINGLE)) {
        /* at_2 is a superset */
        if (debug_logging) {
            slapi_log_err(SLAPI_LOG_REPL, "schema_at_compare", "%s schema attribute [%s] is not "
                                                               "\"single-valued\" \n",
                          message, at_1->asi_name);
        }
        return -1;
    }

    /*
         *  Check the syntaxes
         */
    if (schema_at_superset_check_syntax_oids(at_1->asi_syntax_oid, at_2->asi_syntax_oid)) {
        /* at_1 is a superset */
        if (debug_logging) {
            slapi_log_err(SLAPI_LOG_REPL, "schema_at_compare", "%s schema attribute [%s] syntax "
                                                               "can not be overwritten\n",
                          message, at_1->asi_name);
        }
        return 1;
    }
    if (schema_at_superset_check_syntax_oids(at_2->asi_syntax_oid, at_1->asi_syntax_oid)) {
        /* at_2 is a superset */
        if (debug_logging) {
            slapi_log_err(SLAPI_LOG_REPL, "schema_at_compare", "%s schema attribute [%s] syntax "
                                                               "can not be overwritten\n",
                          message, at_2->asi_name);
        }
        return -1;
    }

    /*
         *  Check some matching rules - not finished yet...
         *
         *  For now, skip the matching rule check (rc is never equal to -1)
         */
    if (schema_at_superset_check_mr(at_1, at_2, info)) {
        if (debug_logging) {
            slapi_log_err(SLAPI_LOG_REPL, "schema_at_compare", "%s schema attribute [%s] matching "
                                                               "rule can not be overwritten\n",
                          message, at_1->asi_name);
        }
        return 1;
    }
    if (schema_at_superset_check_mr(at_2, at_1, info)) {
        if (debug_logging) {
            slapi_log_err(SLAPI_LOG_REPL, "schema_at_compare", "%s schema attribute [%s] matching "
                                                               "rule can not be overwritten\n",
                          message, at_2->asi_name);
        }
        return -1;
    }

    return 0;
}

static int
schema_at_superset_check(struct asyntaxinfo *at_list1, struct asyntaxinfo *at_list2, char *message, int replica_role)
{
    struct asyntaxinfo *at_1, *at_2;
    int debug_logging = 0;
    int repl_schema_policy;
    int rc = 0;

    if (at_list1 == NULL || at_list2 == NULL) {
        return 0;
    }

    /* Are we doing replication logging */
    if (slapi_is_loglevel_set(SLAPI_LOG_REPL)) {
        debug_logging = 1;
    }

    slapi_rwlock_rdlock(schema_policy_lock);
    for (at_1 = at_list1; at_1 != NULL; at_1 = at_1->asi_next) {

        /* Check if there is a specific policy for that objectclass */
        repl_schema_policy = schema_check_policy(replica_role, REPL_SCHEMA_ATTRIBUTE, at_1->asi_name, at_1->asi_oid);
        if (repl_schema_policy == REPL_SCHEMA_UPDATE_ACCEPT_VALUE) {
            /* We are skipping the superset checking for that attribute */
            slapi_log_err(SLAPI_LOG_REPL, "schema_at_superset_check", "Do not check if this ATTRIBUTE is missing on local/remote schema [%s or %s]\n", at_1->asi_name, at_1->asi_oid);
            continue;
        } else if (repl_schema_policy == REPL_SCHEMA_UPDATE_REJECT_VALUE) {
            /* This attribute being present, we need to fail as if it was a superset
                 * but keep evaluating to have all the attribute checking
                 */
            slapi_log_err(SLAPI_LOG_REPL, "schema_at_superset_check", "%s attribute prevents replication of the schema\n", at_1->asi_name);
            rc = 1;
            if (debug_logging) {
                /* we continue to check all the objectclasses so we log what is wrong */
                continue;
            } else {
                break;
            }
        }

        /* check if at_1 exists in at_list2 */
        if ((at_2 = attr_syntax_find(at_1, at_list2))) {
            if (schema_at_compare(at_1, at_2, message, debug_logging) > 0) {
                rc = 1;
                if (debug_logging) {
                    if (replica_role == REPL_SCHEMA_AS_CONSUMER) {
                        slapi_log_err(SLAPI_LOG_REPL, "schema_at_superset_check", "Local %s schema attributetypes is a superset of"
                                                                                  " the received one.\n",
                                      at_1->asi_name);
                    } else {
                        slapi_log_err(SLAPI_LOG_REPL, "schema_at_superset_check", "Remote %s schema attributetypes is a superset of"
                                                                                  " the received one.\n",
                                      at_1->asi_name);
                    }
                    continue;
                } else {
                    break;
                }
            }
        } else {
            rc = 1;
            if (debug_logging) {
                /* we continue to check all attributes so we log what is wrong */
                slapi_log_err(SLAPI_LOG_REPL, "schema_at_superset_check", "Fail to retrieve in the %s schema [%s or %s]\n",
                              message, at_1->asi_name, at_1->asi_oid);
                continue;
            } else {
                break;
            }
        }
    }
    slapi_rwlock_unlock(schema_policy_lock);


    return rc;
}

/*
 * Return 1 if a1's matching rules are superset(not to be overwritten).  If just one of
 * the matching rules should not be overwritten, even if one should, we can not allow it.
 */
static int
schema_at_superset_check_mr(struct asyntaxinfo *a1, struct asyntaxinfo *a2, char *info __attribute__((unused)))
{
    char *a1_mrtype[3] = {a1->asi_mr_equality, a1->asi_mr_substring, a1->asi_mr_ordering};
    char *a2_mrtype[3] = {a2->asi_mr_equality, a2->asi_mr_substring, a2->asi_mr_ordering};
    int rc = 0;
    size_t i = 0;

    /*
     * Loop over the three matching rule types
     */
    for (i = 0; i < 3; i++) {
        if (a1_mrtype[i]) {
            if (a2_mrtype[i]) {
                /*
                 *  Future action item - determine matching rule precedence:
                 *
                    ces
                    "caseExactIA5Match", "1.3.6.1.4.1.1466.109.114.1"
                    "caseExactMatch", "2.5.13.5"
                    "caseExactOrderingMatch", "2.5.13.6"
                    "caseExactSubstringsMatch", "2.5.13.7"
                    "caseExactIA5SubstringsMatch", "2.16.840.1.113730.3.3.1"

                    cis
                    "generalizedTimeMatch", "2.5.13.27"
                    "generalizedTimeOrderingMatch", "2.5.13.28"
                    "booleanMatch", "2.5.13.13"
                    "caseIgnoreIA5Match", "1.3.6.1.4.1.1466.109.114.2"
                    "caseIgnoreIA5SubstringsMatch", "1.3.6.1.4.1.1466.109.114.3"
                    "caseIgnoreListMatch", "2.5.13.11"
                    "caseIgnoreListSubstringsMatch", "2.5.13.12"
                    "caseIgnoreMatch", "2.5.13.2" -------------------------------
                    "caseIgnoreOrderingMatch", "2.5.13.3" ----------------------->  can have lang options
                    "caseIgnoreSubstringsMatch", "2.5.13.4" ---------------------   (as seen in the console)!
                    "directoryStringFirstComponentMatch", "2.5.13.31"
                    "objectIdentifierMatch", "2.5.13.0"
                    "objectIdentifierFirstComponentMatch", "2.5.13.30"

                    bitstring
                    "bitStringMatch", "2.5.13.16","2.16.840.1.113730.3.3.1"

                    bin
                    "octetStringMatch", "2.5.13.17"
                    "octetStringOrderingMatch", "2.5.13.18"

                    DN
                    "distinguishedNameMatch", "2.5.13.1"

                    Int
                    "integerMatch", "2.5.13.14"
                    "integerOrderingMatch", "2.5.13.15"
                    "integerFirstComponentMatch", "2.5.13.29"

                    NameAndOptUID
                    "uniqueMemberMatch", "2.5.13.23"

                    NumericString
                    "numericStringMatch", "2.5.13.8"
                    "numericStringOrderingMatch", "2.5.13.9"
                    "numericStringSubstringsMatch", "2.5.13.10"

                    Telephone
                    "telephoneNumberMatch", "2.5.13.20"
                    "telephoneNumberSubstringsMatch", "2.5.13.21"
                 */
            }
        }
    }

    return rc;
}

/*
 * Return 1 if oid1 is a superset(oid1 is not to be overwritten)
 */
static int
schema_at_superset_check_syntax_oids(char *oid1, char *oid2)
{
    if (oid1 == NULL && oid2 == NULL) {
        return 0;
    } else if (oid2 == NULL) {
        return 0;
    } else if (oid1 == NULL) {
        return 1;
    }

    if (strcmp(oid1, BINARY_SYNTAX_OID) == 0) {
        if (strcmp(oid2, BINARY_SYNTAX_OID) &&
            strcmp(oid2, INTEGER_SYNTAX_OID) &&
            strcmp(oid2, NUMERICSTRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, FACSIMILE_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, TELEPHONE_SYNTAX_OID) &&
            strcmp(oid2, TELETEXTERMID_SYNTAX_OID) &&
            strcmp(oid2, TELEXNUMBER_SYNTAX_OID))

        {
            return 1;
        }
    } else if (strcmp(oid1, BITSTRING_SYNTAX_OID) == 0) {
        if (strcmp(oid2, BINARY_SYNTAX_OID) &&
            strcmp(oid2, BITSTRING_SYNTAX_OID) &&
            strcmp(oid2, INTEGER_SYNTAX_OID) &&
            strcmp(oid2, NUMERICSTRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID) &&
            strcmp(oid2, FACSIMILE_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, TELEPHONE_SYNTAX_OID) &&
            strcmp(oid2, TELETEXTERMID_SYNTAX_OID) &&
            strcmp(oid2, TELEXNUMBER_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, BOOLEAN_SYNTAX_OID) == 0) {
        if (strcmp(oid2, BOOLEAN_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, COUNTRYSTRING_SYNTAX_OID) == 0) {
        if (strcmp(oid2, COUNTRYSTRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, DN_SYNTAX_OID) == 0) {
        if (strcmp(oid2, DN_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, DELIVERYMETHOD_SYNTAX_OID) == 0) {
        if (strcmp(oid2, DELIVERYMETHOD_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, DIRSTRING_SYNTAX_OID) == 0) {
        if (strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, ENHANCEDGUIDE_SYNTAX_OID) == 0) {
        if (strcmp(oid2, ENHANCEDGUIDE_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, IA5STRING_SYNTAX_OID) == 0) {
        if (strcmp(oid2, IA5STRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, INTEGER_SYNTAX_OID) == 0) {
        if (strcmp(oid2, INTEGER_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, NUMERICSTRING_SYNTAX_OID) &&
            strcmp(oid2, TELEPHONE_SYNTAX_OID) &&
            strcmp(oid2, TELETEXTERMID_SYNTAX_OID) &&
            strcmp(oid2, TELEXNUMBER_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, JPEG_SYNTAX_OID) == 0) {
        if (strcmp(oid2, JPEG_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, NAMEANDOPTIONALUID_SYNTAX_OID) == 0) {
        if (strcmp(oid2, NAMEANDOPTIONALUID_SYNTAX_OID) &&
            strcmp(oid2, NAMEANDOPTIONALUID_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, NUMERICSTRING_SYNTAX_OID) == 0) {
        if (strcmp(oid2, NUMERICSTRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, OID_SYNTAX_OID) == 0) {
        if (strcmp(oid2, OID_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, OCTETSTRING_SYNTAX_OID) == 0) {
        if (strcmp(oid2, OCTETSTRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, POSTALADDRESS_SYNTAX_OID) == 0) {
        if (strcmp(oid2, POSTALADDRESS_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, PRINTABLESTRING_SYNTAX_OID) == 0) {
        if (strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, TELEPHONE_SYNTAX_OID) == 0) {
        if (strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, TELEPHONE_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, TELETEXTERMID_SYNTAX_OID) == 0) {
        if (strcmp(oid2, TELETEXTERMID_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, TELEXNUMBER_SYNTAX_OID) == 0) {
        if (strcmp(oid2, TELEXNUMBER_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    } else if (strcmp(oid1, SPACE_INSENSITIVE_STRING_SYNTAX_OID) == 0) {
        if (strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, PRINTABLESTRING_SYNTAX_OID) &&
            strcmp(oid2, DIRSTRING_SYNTAX_OID) &&
            strcmp(oid2, SPACE_INSENSITIVE_STRING_SYNTAX_OID) &&
            strcmp(oid2, IA5STRING_SYNTAX_OID)) {
            return 1;
        }
    }

    return 0;
}

static void
schema_oclist_free(struct objclass *oc_list)
{
    struct objclass *oc, *oc_next;

    for (oc = oc_list; oc != NULL; oc = oc_next) {
        oc_next = oc->oc_next;
        oc_free(&oc);
    }
}

static void
schema_atlist_free(struct asyntaxinfo *at_list)
{
    struct asyntaxinfo *at, *at_next;

    for (at = at_list; at != NULL; at = at_next) {
        at_next = at->asi_next;
        attr_syntax_free(at);
    }
}

static struct objclass *
schema_berval_to_oclist(struct berval **oc_berval)
{
    struct objclass *oc, *oc_list, *oc_tail;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
    int schema_ds4x_compat, rc;
    int i;

    schema_ds4x_compat = config_get_ds4_compatible_schema();
    rc = 0;

    oc_list = NULL;
    oc_tail = NULL;
    if (oc_berval != NULL) {
        for (i = 0; oc_berval[i] != NULL; i++) {
            /* parse the objectclass value */
            oc = NULL;
            if (LDAP_SUCCESS != (rc = parse_oc_str(oc_berval[i]->bv_val, &oc,
                                                   errorbuf, sizeof(errorbuf), DSE_SCHEMA_NO_CHECK | DSE_SCHEMA_USE_PRIV_SCHEMA, 0,
                                                   schema_ds4x_compat, oc_list))) {
                slapi_log_err(SLAPI_LOG_ERR, "schema_berval_to_oclist",
                              "parse_oc_str returned error: %s\n",
                              errorbuf[0] ? errorbuf : "unknown");
                oc_free(&oc);
                rc = 1;
                break;
            }

            /* Add oc at the end of the oc_list */
            oc->oc_next = NULL;
            if (oc_list == NULL) {
                oc_list = oc;
                oc_tail = oc;
            } else {
                oc_tail->oc_next = oc;
                oc_tail = oc;
            }
        }
    }
    if (rc) {
        schema_oclist_free(oc_list);
        oc_list = NULL;
    }
    return oc_list;
}

static struct asyntaxinfo *
schema_berval_to_atlist(struct berval **at_berval)
{
    struct asyntaxinfo *at, *head = NULL, *at_list = NULL;
    char errorbuf[SLAPI_DSE_RETURNTEXT_SIZE] = {0};
    int schema_ds4x_compat, rc = 0, i;

    schema_ds4x_compat = config_get_ds4_compatible_schema();

    if (at_berval != NULL) {
        for (i = 0; at_berval[i] != NULL; i++) {
            /* parse the objectclass value */
            at = NULL;
            rc = parse_at_str(at_berval[i]->bv_val, &at, errorbuf, sizeof(errorbuf),
                              DSE_SCHEMA_NO_CHECK | DSE_SCHEMA_USE_PRIV_SCHEMA, 0, schema_ds4x_compat, 0);
            if (rc) {
                slapi_log_err(SLAPI_LOG_ERR, "schema_berval_to_atlist",
                              "parse_at_str(%s) failed - %s\n",
                              at_berval[i]->bv_val, errorbuf[0] ? errorbuf : "unknown");
                attr_syntax_free(at);
                break;
            }
            if (!head) {
                head = at_list = at;
            } else {
                at_list->asi_next = at;
                at->asi_prev = at_list;
                at_list = at;
            }
        }
    }
    if (rc) {
        schema_atlist_free(head);
        head = NULL;
    }

    return head;
}


int
schema_objectclasses_superset_check(struct berval **remote_schema, char *type)
{
    int rc;
    struct objclass *remote_oc_list;

    rc = 0;

    /* head is the future list of the objectclass of the remote schema */
    remote_oc_list = NULL;

    if (remote_schema != NULL) {
        /* First build an objectclass list from the remote schema */
        if ((remote_oc_list = schema_berval_to_oclist(remote_schema)) == NULL) {
            rc = 1;
            return rc;
        }


        /* Check that for each object from the remote schema
                 *         - MUST attributes are also MUST in local schema
                 *         - ALLOWED attributes are also ALLOWED in local schema
                 */

        if (remote_oc_list) {
            oc_lock_read();
            if (strcmp(type, OC_SUPPLIER) == 0) {
                /* Check if the remote_oc_list from a consumer are or not
                                 * a superset of the objectclasses of the local supplier schema
                                 */
                rc = schema_oc_superset_check(remote_oc_list, g_get_global_oc_nolock(), "remote consumer", REPL_SCHEMA_AS_SUPPLIER);
            } else {
                /* Check if the objectclasses of the local consumer schema are or not
                                 * a superset of the remote_oc_list from a supplier
                                 */
                rc = schema_oc_superset_check(g_get_global_oc_nolock(), remote_oc_list, "remote supplier", REPL_SCHEMA_AS_CONSUMER);
            }

            oc_unlock();
        }

        /* Free the remote schema list*/
        schema_oclist_free(remote_oc_list);
    }
    return rc;
}

int
schema_attributetypes_superset_check(struct berval **remote_schema, char *type)
{
    struct asyntaxinfo *remote_at_list = NULL;
    int rc = 0;

    if (remote_schema != NULL) {
        /* First build an attribute list from the remote schema */
        if ((remote_at_list = schema_berval_to_atlist(remote_schema)) == NULL) {
            rc = 1;
            return rc;
        }

        /*
         * Check that for each object from the remote schema
         *         - MUST attributes are also MUST in local schema
         *         - ALLOWED attributes are also ALLOWED in local schema
         */
        if (remote_at_list) {
            attr_syntax_read_lock();
            if (strcmp(type, OC_SUPPLIER) == 0) {
                /*
                 * Check if the remote_at_list from a consumer are or not
                 * a superset of the attributetypes of the local supplier schema
                 */
                rc = schema_at_superset_check(remote_at_list, attr_syntax_get_global_at(), "local supplier", REPL_SCHEMA_AS_SUPPLIER);
            } else {
                /*
                 * Check if the attributeypes of the local consumer schema are or not
                 * a superset of the remote_at_list from a supplier
                 */
                rc = schema_at_superset_check(attr_syntax_get_global_at(), remote_at_list, "remote supplier", REPL_SCHEMA_AS_CONSUMER);
            }
            attr_syntax_unlock_read();
        }

        /* Free the remote schema list */
        schema_atlist_free(remote_at_list);
    }
    return rc;
}

/* Do the internal MOD and update the local "nsSchemaCSN" with a local timestamp
 * It could be a good idea to set the 'nsSchemaCSN' with the maximum of local time and
 * the CSN received with the remote schema
 */
static void
modify_schema_internal_mod(Slapi_DN *sdn, Slapi_Mods *smods)
{
    Slapi_PBlock *newpb;
    int op_result;
    CSN *schema_csn;

    /* allocate internal mod components: pblock*/
    newpb = slapi_pblock_new();

    slapi_modify_internal_set_pb_ext(
        newpb,
        sdn,
        slapi_mods_get_ldapmods_byref(smods),
        NULL, /* Controls */
        NULL,
        (void *)plugin_get_default_component_id(),
        0);

    /* do modify */
    slapi_modify_internal_pb(newpb);
    slapi_pblock_get(newpb, SLAPI_PLUGIN_INTOP_RESULT, &op_result);
    if (op_result == LDAP_SUCCESS) {
        char *type;

        if (smods && smods->mods) {
            type = smods->mods[0]->mod_type;
        } else {
            type = "unknown";
        }
        slapi_log_err(SLAPI_LOG_REPL, "modify_schema_internal_mod", "Successfully learn %s definitions\n", type);
        /* Update the schema csn if the operation succeeded */
        schema_csn = csn_new();
        if (NULL != schema_csn) {
            csn_set_replicaid(schema_csn, 0);
            csn_set_time(schema_csn, slapi_current_utc_time());
            g_set_global_schema_csn(schema_csn);
        }
    } else {
        slapi_log_err(SLAPI_LOG_ERR, "modify_schema_internal_mod", "Fail to learn schema definitions (%d) \n", op_result);
    }

    slapi_pblock_destroy(newpb);
}

/* Prepare slapi_mods for the internal mod
 * Caller must free smods with slapi_mods_done
 */
static void
modify_schema_prepare_mods(Slapi_Mods *smods, char *type, struct schema_mods_indexes *values)
{
    struct schema_mods_indexes *object;
    struct berval *bv;
    struct berval **bvps_del = NULL;
    struct berval **bvps_add = NULL;
    int nb_values_del, nb_values_add, i;
    int nb_mods;

    /* Checks the values to delete */
    for (object = values, nb_values_del = 0; object != NULL; object = object->next) {
        if (object->old_value) {
            nb_values_del++;
        }
    }
    if (nb_values_del) {
        bvps_del = (struct berval **)slapi_ch_calloc(1, (nb_values_del + 1) * sizeof(struct berval *));

        for (i = 0, object = values; object != NULL; object = object->next) {
            if (object->old_value) {
                bv = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
                bv->bv_len = strlen(object->old_value);
                bv->bv_val = (void *)object->old_value;
                bvps_del[i] = bv;
                i++;
                slapi_log_err(SLAPI_LOG_REPL, "modify_schema_prepare_mods", "MOD[%d] del (%s): %s\n", i, type, object->old_value);
            }
        }
        bvps_del[nb_values_del] = NULL;
    }

    /* Checks the values to add */
    for (object = values, nb_values_add = 0; object != NULL; object = object->next, nb_values_add++)
        ;

    if (nb_values_add) {
        bvps_add = (struct berval **)slapi_ch_calloc(1, (nb_values_add + 1) * sizeof(struct berval *));


        for (i = 0, object = values; object != NULL; i++, object = object->next) {
            bv = (struct berval *)slapi_ch_malloc(sizeof(struct berval));
            bv->bv_len = strlen(object->new_value);
            bv->bv_val = (void *)object->new_value;
            bvps_add[i] = bv;
            slapi_log_err(SLAPI_LOG_REPL, "modify_schema_prepare_mods", "MOD[%d] add (%s): %s\n", i, type, object->new_value);
        }
        bvps_add[nb_values_add] = NULL;
    }

    /* Prepare the mods */
    nb_mods = 1;
    if (bvps_del)
        nb_mods++;
    if (bvps_add)
        nb_mods++;
    slapi_mods_init(smods, nb_mods);
    if (bvps_del)
        slapi_mods_add_modbvps(smods, LDAP_MOD_DELETE, type, bvps_del);
    if (bvps_add)
        slapi_mods_add_modbvps(smods, LDAP_MOD_ADD, type, bvps_add);


    /* clean up */
    if (bvps_del) {

        for (i = 0; bvps_del[i] != NULL; i++) {
            /* bv_val should not be free. It belongs to the incoming MOD */
            slapi_ch_free((void **)&bvps_del[i]);
        }
        slapi_ch_free((void **)&bvps_del);
    }

    if (bvps_add) {

        for (i = 0; bvps_add[i] != NULL; i++) {
            /* bv_val should not be free. It belongs to the incoming MOD */
            slapi_ch_free((void **)&bvps_add[i]);
        }
        slapi_ch_free((void **)&bvps_add);
    }
}

/* called by modify_schema_dse/supplier_learn_new_definitions to learn new
 * definitions via internal mod.
 * Internal mod is important, because we want those definitions to be updated in 99user.ldif
 * and we are not sure that the current operation will succeeds or not.
 */
static void
modify_schema_apply_new_definitions(char *attr_name, struct schema_mods_indexes *list)
{
    Slapi_Mods smods = {0};
    Slapi_DN *sdn = NULL;

    if (list == NULL)
        return;

    /* Then the sdn */
    sdn = slapi_sdn_new();
    if (!sdn) {
        slapi_log_err(SLAPI_LOG_ERR, "modify_schema_apply_new_definitions", "Out of memory\n");
        goto done;
    }
    slapi_sdn_set_dn_byval(sdn, SLAPD_SCHEMA_DN);

    /* prepare the mods */
    modify_schema_prepare_mods(&smods, attr_name, list);

    /* update the schema with the new attributetypes */
    /* No need to lock the schema_dse as the internal mod will do */
    modify_schema_internal_mod(sdn, &smods);


done:
    if (sdn) {
        slapi_sdn_free(&sdn);
    }
    slapi_mods_done(&smods);
}

/*
 * This routines retrieves from the remote schema (mods) the
 * definitions (attributetypes/objectclasses), that are:
 *   - unknown from the local schema
 *   - a superset of the local definition
 *
 * It then builds two lists (to be freed by the caller) with those definitions.
 * Those list contains a duplicate of the definition (string).
 */
static void
modify_schema_get_new_definitions(Slapi_PBlock *pb, LDAPMod **mods, struct schema_mods_indexes **at_list, struct schema_mods_indexes **oc_list)
{
    struct asyntaxinfo *remote_at_list;
    struct objclass *remote_oc_list;
    int is_replicated_operation = 0;
    int replace_allowed = 0;
    slapdFrontendConfig_t *slapdFrontendConfig;
    int i;
    struct schema_mods_indexes *at2learn_list = NULL;
    struct schema_mods_indexes *at2learn;
    struct schema_mods_indexes *oc2learn_list = NULL;
    struct schema_mods_indexes *oc2learn;


    slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);

    /* by default nothing to learn */
    *at_list = NULL;
    *oc_list = NULL;

    /* We are only looking for schema received from a supplier */
    if (!is_replicated_operation || !mods) {
        return;
    }

    /* Check if we are allowed to update the schema (if needed) */
    slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    if ((0 == strcasecmp(slapdFrontendConfig->schemareplace, CONFIG_SCHEMAREPLACE_STR_ON)) ||
        (0 == strcasecmp(slapdFrontendConfig->schemareplace, CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY))) {
        replace_allowed = 1;
    }
    CFG_UNLOCK_READ(slapdFrontendConfig);
    if (!replace_allowed) {
        return;
    }


    /* First retrieve unknowns attributetypes because an unknown objectclasses
         * may be composed of unknown attributetypes
         */
    at2learn_list = NULL;
    oc2learn_list = NULL;
    schema_dse_lock_read();
    for (i = 0; mods[i]; i++) {
        if (SLAPI_IS_MOD_REPLACE(mods[i]->mod_op) && (mods[i]->mod_bvalues)) {

            if (strcasecmp(mods[i]->mod_type, "attributetypes") == 0) {
                /* we have some MOD_replace of attributetypes*/

                /* First build an attribute list from the remote schema */
                if ((remote_at_list = schema_berval_to_atlist(mods[i]->mod_bvalues)) == NULL) {
                    /* If we can not build an attributes list from the mods, just skip
                                         * it and look for objectclasses
                                         */
                    slapi_log_err(SLAPI_LOG_ERR, "modify_schema_get_new_definitions",
                                  "Not able to build an attributes list (%s) from the schema received from the supplier\n",
                                  mods[i]->mod_type);
                    continue;
                }
                /* Build a list of attributestype to learn from the remote definitions */
                attr_syntax_read_lock();
                at2learn_list = schema_list_attr2learn(attr_syntax_get_global_at(), remote_at_list, REPL_SCHEMA_AS_CONSUMER);
                attr_syntax_unlock_read();

                /* For each of them copy the value to set */
                for (at2learn = at2learn_list; at2learn != NULL; at2learn = at2learn->next) {
                    struct berval *bv;
                    bv = mods[i]->mod_bvalues[at2learn->index]; /* takes the berval from the selected mod */
                    at2learn->new_value = (char *)slapi_ch_malloc(bv->bv_len + 1);
                    memcpy(at2learn->new_value, bv->bv_val, bv->bv_len);
                    at2learn->new_value[bv->bv_len] = '\0';
                    slapi_log_err(SLAPI_LOG_REPL, "modify_schema_get_new_definitions", "take attributetypes: %s\n", at2learn->new_value);
                }

                /* Free the remote schema list */
                schema_atlist_free(remote_at_list);

            } else if (strcasecmp(mods[i]->mod_type, "objectclasses") == 0) {
                /* we have some MOD_replace of objectclasses */

                /* First build an objectclass list from the remote schema */
                if ((remote_oc_list = schema_berval_to_oclist(mods[i]->mod_bvalues)) == NULL) {
                    /* If we can not build an objectclasses list from the mods, just skip
                                         * it and look for attributes
                                         */
                    slapi_log_err(SLAPI_LOG_ERR, "modify_schema_get_new_definitions",
                                  "Not able to build an objectclasses list (%s) from the schema received from the supplier\n",
                                  mods[i]->mod_type);
                    continue;
                }
                /* Build a list of objectclasses to learn from the remote definitions */
                oc_lock_read();
                oc2learn_list = schema_list_oc2learn(remote_oc_list, g_get_global_oc_nolock(), REPL_SCHEMA_AS_CONSUMER);
                oc_unlock();

                /* For each of them copy the value to set */
                for (oc2learn = oc2learn_list; oc2learn != NULL; oc2learn = oc2learn->next) {
                    struct berval *bv;
                    bv = mods[i]->mod_bvalues[oc2learn->index]; /* takes the berval from the selected mod */
                    oc2learn->new_value = (char *)slapi_ch_malloc(bv->bv_len + 1);
                    memcpy(oc2learn->new_value, bv->bv_val, bv->bv_len);
                    oc2learn->new_value[bv->bv_len] = '\0';
                    slapi_log_err(SLAPI_LOG_REPL, "modify_schema_get_new_definitions", "take objectclass: %s\n", oc2learn->new_value);
                }

                /* Free the remote schema list*/
                schema_oclist_free(remote_oc_list);
            }
        }
    }
    schema_dse_unlock();

    *at_list = at2learn_list;
    *oc_list = oc2learn_list;
}

/*
 * It evaluate if the schema in the mods, is a superset of
 * the local schema.
 * Called when we know the mods comes from/to a replicated session
 * Caller must not hold schema_dse lock
 *
 *   mods: set of mod to apply to the schema
 *   replica_role:
 *      OC_CONSUMER: means the caller is acting as a consumer (receiving a schema)
 *      OC_SUPPLIER: means the caller is acting as a supplier (sending a schema)
 *
 * It returns:
 *  - PR_TRUE: if replicated schema is a superset of local schema
 *  - PR_FALSE: if local schema is a superset of local schema
 */
static PRBool
check_replicated_schema(LDAPMod **mods, char *replica_role, char **attr_name)
{
    int i;
    PRBool rc = PR_TRUE;

    schema_dse_lock_read();
    for (i = 0; mods[i]; i++) {
        if ((SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)) && strcasecmp(mods[i]->mod_type, "attributetypes") == 0) {
            if (schema_attributetypes_superset_check(mods[i]->mod_bvalues, replica_role)) {
                rc = PR_FALSE;
                *attr_name = mods[i]->mod_type;
                break;
            }
        } else if ((SLAPI_IS_MOD_REPLACE(mods[i]->mod_op)) && strcasecmp(mods[i]->mod_type, "objectclasses") == 0) {
            if (schema_objectclasses_superset_check(mods[i]->mod_bvalues, replica_role)) {
                rc = PR_FALSE;
                *attr_name = mods[i]->mod_type;
                break;
            }
        }
    }
    schema_dse_unlock();

    return rc;
}

/* Free the list of definitions allocated in modify_schema_get_new_definitions/supplier_get_new_definitions */
static void
modify_schema_free_new_definitions(struct schema_mods_indexes *def_list)
{
    struct schema_mods_indexes *def, *head;

    for (head = def_list; head != NULL;) {
        def = head;
        head = head->next;

        /* Free the string definition that was copied from the berval */
        if (def->new_value) {
            slapi_ch_free((void **)&def->new_value);
        }

        /* Then the definition cell */
        slapi_ch_free((void **)&def);
    }
}

/* This functions is called by a supplier.
 * It builds lists of definitions (attributetypes/objectclasses) to learn
 * objectclasses:  received objectclass definitions
 * attributetypes: received attribute definitions
 * new_oc: list of definitions to learn (list should be freed by the caller)
 * new_at: list of definitions to learn (list should be freed by the caller)
 */

static void
supplier_get_new_definitions(struct berval **objectclasses, struct berval **attributetypes, struct schema_mods_indexes **new_oc, struct schema_mods_indexes **new_at)
{
    int replace_allowed = 0;
    slapdFrontendConfig_t *slapdFrontendConfig;
    struct asyntaxinfo *remote_at_list;
    struct objclass *remote_oc_list;
    struct schema_mods_indexes *at2learn_list = NULL;
    struct schema_mods_indexes *at2learn;
    struct schema_mods_indexes *oc2learn_list = NULL;
    struct schema_mods_indexes *oc2learn;

    *new_oc = NULL;
    *new_at = NULL;

    if ((objectclasses == NULL) && (attributetypes == NULL)) {
        return;
    }
    /* Check if we are allowed to update the schema (if needed) */
    slapdFrontendConfig = getFrontendConfig();
    CFG_LOCK_READ(slapdFrontendConfig);
    if ((0 == strcasecmp(slapdFrontendConfig->schemareplace, CONFIG_SCHEMAREPLACE_STR_ON)) ||
        (0 == strcasecmp(slapdFrontendConfig->schemareplace, CONFIG_SCHEMAREPLACE_STR_REPLICATION_ONLY))) {
        replace_allowed = 1;
    }
    CFG_UNLOCK_READ(slapdFrontendConfig);
    if (!replace_allowed) {
        return;
    }

    schema_dse_lock_read();
    /*
         * Build the list of objectclasses
         */
    /* from berval to objclass more convenient to compare */
    if ((remote_oc_list = schema_berval_to_oclist(objectclasses)) != NULL) {
        /* Build a list of objectclasses to learn from the remote definitions */
        oc_lock_read();
        oc2learn_list = schema_list_oc2learn(remote_oc_list, g_get_global_oc_nolock(), REPL_SCHEMA_AS_SUPPLIER);
        oc_unlock();

        /* For each of them copy the value to set */
        for (oc2learn = oc2learn_list; oc2learn != NULL; oc2learn = oc2learn->next) {
            struct berval *bv;
            bv = objectclasses[oc2learn->index]; /* takes the berval from the selected objectclass */
            oc2learn->new_value = (char *)slapi_ch_malloc(bv->bv_len + 1);
            memcpy(oc2learn->new_value, bv->bv_val, bv->bv_len);
            oc2learn->new_value[bv->bv_len] = '\0';
            slapi_log_err(SLAPI_LOG_REPL, "supplier_get_new_definitions", "supplier takes objectclass: %s\n", oc2learn->new_value);
        }

        /* Free the remote schema list*/
        schema_oclist_free(remote_oc_list);
    } else {
        /* If we can not build an objectclasses list */
        slapi_log_err(SLAPI_LOG_ERR, "supplier_get_new_definitions",
                      "Not able to build an objectclasses list from the consumer schema\n");
    }


    /*
         * Build the list of attributetypes
         */
    /* First build an attribute list from the remote schema */
    if ((remote_at_list = schema_berval_to_atlist(attributetypes)) != NULL) {
        /* Build a list of attributestype to learn from the remote definitions */
        attr_syntax_read_lock();
        at2learn_list = schema_list_attr2learn(attr_syntax_get_global_at(), remote_at_list, REPL_SCHEMA_AS_SUPPLIER);
        attr_syntax_unlock_read();

        /* For each of them copy the value to set */
        for (at2learn = at2learn_list; at2learn != NULL; at2learn = at2learn->next) {
            struct berval *bv;
            bv = attributetypes[at2learn->index]; /* takes the berval from the selected mod */
            at2learn->new_value = (char *)slapi_ch_malloc(bv->bv_len + 1);
            memcpy(at2learn->new_value, bv->bv_val, bv->bv_len);
            at2learn->new_value[bv->bv_len] = '\0';
            slapi_log_err(SLAPI_LOG_REPL, "schema", "supplier takes attributetypes: %s\n", at2learn->new_value);
        }

        /* Free the remote schema list */
        schema_atlist_free(remote_at_list);
    } else {
        /* If we can not build an attributes list from the mods, just skip
                 * it and look for objectclasses
                 */
        slapi_log_err(SLAPI_LOG_ERR, "supplier_get_new_definitions",
                      "Not able to build an attributes list from the consumer schema");
    }
    schema_dse_unlock();
    *new_oc = oc2learn_list;
    *new_at = at2learn_list;
}

/* This functions is called by a supplier when it detects new definitions (objectclasses/attributetypes)
 * or extension of existing definitions in a consumer schema.
 * This function, build lists of definitions to "learn" and add those definitions in the schema and 99user.ldif
 */
void
supplier_learn_new_definitions(struct berval **objectclasses, struct berval **attributetypes)
{
    struct schema_mods_indexes *oc_list = NULL;
    struct schema_mods_indexes *at_list = NULL;

    supplier_get_new_definitions(objectclasses, attributetypes, &oc_list, &at_list);
    if (at_list) {
        modify_schema_apply_new_definitions("attributetypes", at_list);
    }
    if (oc_list) {
        modify_schema_apply_new_definitions("objectclasses", oc_list);
    }
    /* No need to hold the lock for these list that are local */
    modify_schema_free_new_definitions(at_list);
    modify_schema_free_new_definitions(oc_list);
}
