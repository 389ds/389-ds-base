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

/* resourcelimit.c - binder-based resource limits implementation */


/*
 * Implementation notes:
 *
 * At present this code only provides support for integer-based
 * resource limits.
 *
 * When a successful bind occurs (i.e., when bind_credentials_set() is
 * called), reslimit_update_from_dn() or reslimit_update_from_entry()
 * must be called.  These functions look in the binder entry and pull
 * out attribute values that correspond to resource limits.  Typically
 * operational attributes are used, e.g., nsSizeLimit to hold a
 * binder-specific search size limit.  The attributes should be single
 * valued; if not, this code ignores all but the first value it finds.
 * The virtual attribute interface is used to retrieve the binder entry
 * values, so they can be based on COS, etc.
 *
 * Any resource limits found in the binder entry are cached in the
 * connection structure.  A connection object extension is used for this
 * purpose.  This means that if the attributes that correspond to binder
 * entry are changed the resource limit won't be affected until the next
 * bind occurs as that entry.  The data in the connection extension is
 * protected using a single writer/multiple reader locking scheme.
 *
 * A plugin or server subsystem that wants to use the resource limit
 * subsystem should call slapi_reslimit_register() once for each limit it
 * wants tracked.  Note that slapi_reslimit_register() should be called
 * early, i.e., before any client connections are accepted.
 * slapi_reslimit_register() gives back an integer handle that is used
 * later to refer to the limit in question.  Here's a sample call:
 */
#if SLAPI_RESLIMIT_SAMPLE_CODE
static int sizelimit_reslimit_handle = -1;

if (slapi_reslimit_register(SLAPI_RESLIMIT_TYPE_INT, "nsSizeLimit", &sizelimit_reslimit_handle) != SLAPI_RESLIMIT_STATUS_SUCCESS) {
    /* limit could not be registered -- fatal error? */
}
...
#endif

/*
 * A successful call to slapi_reslimit_register() results in a new
 * entry in the reslimit_map, which is private to this source file.
 * The map data structure is protected using a single writer/multiple
 * reader locking scheme.
 *
 * To retrieve a binder-based limit, simple call
 * slapi_reslimit_get_integer_limit().  If a value was present in the
 * binder entry, it will be given back to the caller and
 * SLAPI_RESLIMIT_STATUS_SUCCESS will be returned.  If no value was
 * present or the connection is NULL, SLAPI_RESLIMIT_STATUS_NOVALUE is
 * returned.  Other errors may be returned also.  Here's a sample call:
 */
#if SLAPI_RESLIMIT_SAMPLE_CODE
    int rc,
    sizelimit;

rc = slapi_reslimit_get_integer_limit(conn, sizelimit_reslimit_handle, &sizelimit);

switch (rc) {
case SLAPI_RESLIMIT_STATUS_SUCCESS: /* got a value */
    break;
case SLAPI_RESLIMIT_STATUS_NOVALUE: /* no limit value available */
    sizelimit = 500;                /* use a default value */
    break;
default:             /* some other error occurred */
    sizelimit = 500; /* use a default value */
}
#endif

/*
 * The function reslimit_cleanup() is called from main() to dispose of
 * memory, locks, etc. so tools like Purify() don't report leaks at exit.
 */
/* End of implementation notes */

#include "slap.h"


/*
 * Macros.
 */
#define SLAPI_RESLIMIT_MODULE "binder-based resource limits"
    /* #define SLAPI_RESLIMIT_DEBUG */ /* define this to enable extra logging */
                                       /* also forces trace log messages to */
                                       /* always be logged */

#ifdef SLAPI_RESLIMIT_DEBUG
#define SLAPI_RESLIMIT_TRACELEVEL SLAPI_LOG_INFO
#else /* SLAPI_RESLIMIT_DEBUG */
#define SLAPI_RESLIMIT_TRACELEVEL SLAPI_LOG_TRACE
#endif /* SLAPI_RESLIMIT_DEBUG */


/*
 * Structures and types.
 */
/* Per-connection resource limits data */
typedef struct slapi_reslimit_conndata
{
    Slapi_RWLock *rlcd_rwlock;      /* to serialize access to the rest */
    int rlcd_integer_count;         /* size of rlcd_integer_limit array */
    PRBool *rlcd_integer_available; /* array that says whether each */
                                    /*           value is available */
    int *rlcd_integer_value;        /* array that holds limit values */
} SLAPIResLimitConnData;

/* Mapping between attribute and limit */
typedef struct slapi_reslimit_map
{
    int rlmap_type; /* always SLAPI_RESLIMIT_TYPE_INT for now */
    char *rlmap_at; /* attribute type name */
} SLAPIResLimitMap;


/*
 * Static variables (module globals).
 */
static int reslimit_inited = 0;
static int reslimit_connext_objtype = 0;
static int reslimit_connext_handle = 0;
static struct slapi_reslimit_map *reslimit_map = NULL;
static int reslimit_map_count = 0;
static struct slapi_componentid *reslimit_componentid = NULL;

/*
 * reslimit_map_rwlock is used to serialize access to
 * reslimit_map and reslimit_map_count
 */
static Slapi_RWLock *reslimit_map_rwlock = NULL;

/*
 * Static functions.
 */
static int reslimit_init(void);
static void *reslimit_connext_constructor(void *object, void *parent);
static void reslimit_connext_destructor(void *extension, void *object, void *parent);
static int reslimit_get_ext(Slapi_Connection *conn, const char *logname, SLAPIResLimitConnData **rlcdpp);
static char **reslimit_get_registered_attributes(void);


/*
 * reslimit_init() must be called before any resource related work
 * is done.  It is safe to call this more than once, but reslimit_inited
 * can be tested to avoid a call.
 *
 * Returns zero if all goes well and non-zero if not.
 */
static int
reslimit_init(void)
{
    if (reslimit_inited == 0) {
        if (slapi_register_object_extension(SLAPI_RESLIMIT_MODULE,
                                            SLAPI_EXT_CONNECTION, reslimit_connext_constructor,
                                            reslimit_connext_destructor,
                                            &reslimit_connext_objtype, &reslimit_connext_handle) != 0) {
            slapi_log_err(SLAPI_LOG_ERR, "reslimit_init",
                          "slapi_register_object_extension() failed\n");
            return (-1);
        }

        if ((reslimit_map_rwlock = slapi_new_rwlock()) == NULL) {
            slapi_log_err(SLAPI_LOG_ERR, "reslimit_init",
                          "slapi_new_rwlock() failed\n");
            return (-1);
        }

        reslimit_inited = 1;
    }

    reslimit_componentid = generate_componentid(NULL, COMPONENT_RESLIMIT);

    return (0);
}


/*
 * Dispose of any allocated memory, locks, other resources.  Called when
 * server is shutting down.
 */
void
reslimit_cleanup(void)
{
    int i;

    if (reslimit_map != NULL) {
        for (i = 0; i < reslimit_map_count; ++i) {
            if (reslimit_map[i].rlmap_at != NULL) {
                slapi_ch_free((void **)&reslimit_map[i].rlmap_at);
            }
        }
        slapi_ch_free((void **)&reslimit_map);
    }

    if (reslimit_map_rwlock != NULL) {
        slapi_destroy_rwlock(reslimit_map_rwlock);
    }

    if (reslimit_componentid != NULL) {
        release_componentid(reslimit_componentid);
    }
}


/*
 * constructor for the connection object extension.
 */
static void *
reslimit_connext_constructor(void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    SLAPIResLimitConnData *rlcdp;
    Slapi_RWLock *rwlock;

    if ((rwlock = slapi_new_rwlock()) == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "reslimit_connext_constructor",
                      "slapi_new_rwlock() failed\n");
        return (NULL);
    }

    rlcdp = (SLAPIResLimitConnData *)slapi_ch_calloc(1,
                                                     sizeof(SLAPIResLimitConnData));
    rlcdp->rlcd_rwlock = rwlock;

    return (rlcdp);
}


/*
 * destructor for the connection object extension.
 */
static void
reslimit_connext_destructor(void *extension, void *object __attribute__((unused)), void *parent __attribute__((unused)))
{
    SLAPIResLimitConnData *rlcdp = (SLAPIResLimitConnData *)extension;

    if (rlcdp->rlcd_integer_available != NULL) {
        slapi_ch_free((void **)&rlcdp->rlcd_integer_available);
    }
    if (rlcdp->rlcd_integer_value != NULL) {
        slapi_ch_free((void **)&rlcdp->rlcd_integer_value);
    }
    slapi_destroy_rwlock(rlcdp->rlcd_rwlock);
    slapi_ch_free((void **)&rlcdp);
}


/*
 * utility function to retrieve the connection object extension.
 *
 * if logname is non-NULL, errors are logged.
 */
static int
reslimit_get_ext(Slapi_Connection *conn, const char *logname, SLAPIResLimitConnData **rlcdpp)
{
    if (!reslimit_inited && reslimit_init() != 0) {
        if (NULL != logname) {
            slapi_log_err(SLAPI_LOG_ERR, "reslimit_get_ext",
                          "%s: reslimit_init() failed\n", logname);
        }
        return (SLAPI_RESLIMIT_STATUS_INIT_FAILURE);
    }

    if ((*rlcdpp = (SLAPIResLimitConnData *)slapi_get_object_extension(
             reslimit_connext_objtype, conn,
             reslimit_connext_handle)) == NULL) {
        if (NULL != logname) {
            slapi_log_err(SLAPI_LOG_ERR, "reslimit_get_ext",
                          "%s: slapi_get_object_extension() returned NULL\n", logname);
        }
        return (SLAPI_RESLIMIT_STATUS_INTERNAL_ERROR);
    }

    return (SLAPI_RESLIMIT_STATUS_SUCCESS);
}


/**** Semi-public functions start here ***********************************/
/*
 * These functions are exposed to other parts of the server only, i.e.,
 * they are NOT part of the official SLAPI API.
 */

/*
 * Set the resource limits associated with connection `conn' based on the
 * entry named by `dn'.  If `dn' is NULL, limits are returned to their
 * default state.
 *
 * A SLAPI_RESLIMIT_STATUS_... code is returned.
 */
int
reslimit_update_from_dn(Slapi_Connection *conn, Slapi_DN *dn)
{
    Slapi_PBlock *pb = NULL;
    Slapi_Entry *e = NULL;
    int rc;

    if (dn != NULL) {
        char **attrs = reslimit_get_registered_attributes();
        slapi_search_get_entry(&pb, dn, attrs, &e, reslimit_componentid);
        charray_free(attrs);
    }
    rc = reslimit_update_from_entry(conn, e);
    slapi_search_get_entry_done(&pb);

    return (rc);
}


/*
 * Set the resource limits associated with connection `conn' based on the
 * entry `e'.  If `e' is NULL, limits are returned to their default state.
 * If `conn' is NULL, nothing is done.
 *
 * A SLAPI_RESLIMIT_STATUS_... code is returned.
 */
int
reslimit_update_from_entry(Slapi_Connection *conn, Slapi_Entry *e)
{
    SLAPIResLimitConnData *rlcdp = NULL;
    Slapi_ValueSet *vs = NULL;
    char *actual_type_name = NULL;
    char *get_ext_logname = NULL;
    int type_name_disposition = 0;
    int free_flags = 0;
    int rc, i;

    slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "reslimit_update_from_entry",
                  "=> conn=0x%p, entry=0x%p\n", conn, e);

    rc = SLAPI_RESLIMIT_STATUS_SUCCESS; /* optimistic */

    /* if conn is NULL, there is nothing to be done */
    if (conn == NULL) {
        goto log_and_return;
    }


    if (NULL == e) {
        get_ext_logname = NULL; /* do not log errors if resetting limits */
    } else {
        get_ext_logname = "reslimit_update_from_entry";
    }
    if ((rc = reslimit_get_ext(conn, get_ext_logname, &rlcdp)) !=
        SLAPI_RESLIMIT_STATUS_SUCCESS) {
        goto log_and_return;
    }

    /* LOCK FOR READ -- map lock */
    slapi_rwlock_rdlock(reslimit_map_rwlock);
    /* LOCK FOR WRITE -- conn. data lock */
    slapi_rwlock_wrlock(rlcdp->rlcd_rwlock);

    if (rlcdp->rlcd_integer_value == NULL) {
        rlcdp->rlcd_integer_count = reslimit_map_count;
        if (rlcdp->rlcd_integer_count > 0) {
            rlcdp->rlcd_integer_available = (PRBool *)slapi_ch_calloc(
                rlcdp->rlcd_integer_count, sizeof(PRBool));
            rlcdp->rlcd_integer_value = (int *)slapi_ch_calloc(
                rlcdp->rlcd_integer_count, sizeof(int));
        }
    }

    for (i = 0; i < rlcdp->rlcd_integer_count; ++i) {
        if (reslimit_map[i].rlmap_type != SLAPI_RESLIMIT_TYPE_INT) {
            continue;
        }

        slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "reslimit_update_from_entry",
                      "Setting limit for handle %d (based on %s)\n",
                      i, reslimit_map[i].rlmap_at);

        rlcdp->rlcd_integer_available[i] = PR_FALSE;

        if (NULL != e && 0 == slapi_vattr_values_get(e, reslimit_map[i].rlmap_at, &vs, &type_name_disposition, &actual_type_name, 0, &free_flags)) {
            Slapi_Value *v;
            int index;

            if (((index = slapi_valueset_first_value(vs, &v)) != -1) &&
                (v != NULL)) {
                rlcdp->rlcd_integer_value[i] = slapi_value_get_int(v);
                rlcdp->rlcd_integer_available[i] = PR_TRUE;

                slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "reslimit_update_from_entry",
                              "Set limit based on %s to %d\n",
                              reslimit_map[i].rlmap_at,
                              rlcdp->rlcd_integer_value[i]);

                if (slapi_valueset_next_value(vs, index, &v) != -1) {
                    slapi_log_err(SLAPI_LOG_WARNING, "reslimit_update_from_entry",
                                  "Ignoring multiple values for %s in entry %s\n",
                                  reslimit_map[i].rlmap_at,
                                  slapi_entry_get_dn_const(e));
                }
            }

            slapi_vattr_values_free(&vs, &actual_type_name, free_flags);
        }
    }

    slapi_rwlock_unlock(rlcdp->rlcd_rwlock);
    /* UNLOCKED -- conn. data lock */
    slapi_rwlock_unlock(reslimit_map_rwlock);
/* UNLOCKED -- map lock */

log_and_return:
    slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "reslimit_update_from_entry",
                  "<= returning status %d\n", rc);

    return (rc);
}

/* return the list of registered attributes */

static char **
reslimit_get_registered_attributes(void)
{

    int i;
    char **attrs = NULL;

    /* LOCK FOR READ -- map lock */
    slapi_rwlock_rdlock(reslimit_map_rwlock);

    for (i = 0; i < reslimit_map_count; ++i) {
        if (reslimit_map[i].rlmap_at != NULL) {
            charray_add(&attrs, slapi_ch_strdup(reslimit_map[i].rlmap_at));
        }
    }

    slapi_rwlock_unlock(reslimit_map_rwlock);

    return attrs;
}


/**** Public functions can be found below this point *********************/
/*
 * These functions are exposed to plugins, i.e., they are part of the
 * official SLAPI API.
 */

/*
 * Register a new resource to be tracked.  `type' must be
 * SLAPI_RESLIMIT_TYPE_INT and `attrname' is an LDAP attribute type that
 * is consulted in the bound entry to determine the limit's value.
 *
 * A SLAPI_RESLIMIT_STATUS_... code is returned.  If it is ...SUCCESS, then
 * `*handlep' is set to an opaque integer value that should be used in
 * subsequent calls to slapi_reslimit_get_integer_limit().
 */
int
slapi_reslimit_register(int type, const char *attrname, int *handlep)
{
    int i, rc;

    slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "slapi_reslimit_register",
                  "=> attrname=%s\n", attrname);

    rc = SLAPI_RESLIMIT_STATUS_SUCCESS; /* optimistic */

    /* initialize if necessary */
    if (!reslimit_inited && reslimit_init() != 0) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_reslimit_register",
                      "reslimit_init() failed\n");
        rc = SLAPI_RESLIMIT_STATUS_INIT_FAILURE;
        goto log_and_return;
    }

    /* sanity check parameters */
    if (type != SLAPI_RESLIMIT_TYPE_INT || attrname == NULL || handlep == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_reslimit_register",
                      "Parameter error\n");
        rc = SLAPI_RESLIMIT_STATUS_PARAM_ERROR;
        goto log_and_return;
    }

    /* LOCK FOR WRITE -- map lock */
    slapi_rwlock_wrlock(reslimit_map_rwlock);

    /*
     * check that attrname is not already registered
     */
    for (i = 0; i < reslimit_map_count; ++i) {
        if (0 == slapi_attr_type_cmp(reslimit_map[i].rlmap_at,
                                     attrname, SLAPI_TYPE_CMP_EXACT)) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_reslimit_register",
                          "Parameter error (%s already registered)\n",
                          attrname);
            rc = SLAPI_RESLIMIT_STATUS_PARAM_ERROR;
            goto unlock_and_return;
        }
    }

    /*
     * expand the map array and add the new element
     */
    reslimit_map = (SLAPIResLimitMap *)slapi_ch_realloc(
        (char *)reslimit_map,
        (1 + reslimit_map_count) * sizeof(SLAPIResLimitMap));
    reslimit_map[reslimit_map_count].rlmap_type = type;
    reslimit_map[reslimit_map_count].rlmap_at = slapi_ch_strdup(attrname);
    *handlep = reslimit_map_count;
    ++reslimit_map_count;

unlock_and_return:
    slapi_rwlock_unlock(reslimit_map_rwlock);
/* UNLOCKED -- map lock */

log_and_return:
    slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "slapi_reslimit_register",
                  "<= returning status=%d, handle=%d\n", rc,
                  (handlep == NULL) ? -1 : *handlep);

    return (rc);
}


/*
 * Retrieve the integer limit associated with connection `conn' for
 * the resource identified by `handle'.
 *
 * A SLAPI_RESLIMIT_STATUS_... code is returned:
 *
 *  SLAPI_RESLIMIT_STATUS_SUCCESS -- `*limitp' is set to the limit value.
 *  SLAPI_RESLIMIT_STATUS_NOVALUE -- no limit value is available (use default).
 *  Another SLAPI_RESLIMIT_STATUS_... code -- some more fatal error occurred.
 *
 * If `conn' is NULL, SLAPI_RESLIMIT_STATUS_NOVALUE is returned.
 */
int
slapi_reslimit_get_integer_limit(Slapi_Connection *conn, int handle, int *limitp)
{
    int rc;
    SLAPIResLimitConnData *rlcdp;

    slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "slapi_reslimit_get_integer_limit",
                  "=> conn=0x%p, handle=%d\n", conn, handle);

    rc = SLAPI_RESLIMIT_STATUS_SUCCESS; /* optimistic */

    /* sanity check parameters */
    if (limitp == NULL) {
        slapi_log_err(SLAPI_LOG_ERR, "slapi_reslimit_get_integer_limit",
                      "Parameter error\n");
        rc = SLAPI_RESLIMIT_STATUS_PARAM_ERROR;
        goto log_and_return;
    }

    if (conn == NULL) {
        rc = SLAPI_RESLIMIT_STATUS_NOVALUE;
        goto log_and_return;
    }

    if ((rc = reslimit_get_ext(conn, "slapi_reslimit_get_integer_limit", &rlcdp)) !=
        SLAPI_RESLIMIT_STATUS_SUCCESS) {
        goto log_and_return;
    }
    if (rlcdp->rlcd_integer_count == 0) { /* peek at it to avoid lock */
        rc = SLAPI_RESLIMIT_STATUS_NOVALUE;
    } else {
        slapi_rwlock_rdlock(rlcdp->rlcd_rwlock);
        if (rlcdp->rlcd_integer_count == 0) {
            rc = SLAPI_RESLIMIT_STATUS_NOVALUE;
        } else if (handle < 0 || handle >= rlcdp->rlcd_integer_count) {
            slapi_log_err(SLAPI_LOG_ERR, "slapi_reslimit_get_integer_limit",
                          "Uunknown handle %d\n", handle);
            rc = SLAPI_RESLIMIT_STATUS_UNKNOWN_HANDLE;
        } else if (rlcdp->rlcd_integer_available[handle]) {
            *limitp = rlcdp->rlcd_integer_value[handle];
        } else {
            rc = SLAPI_RESLIMIT_STATUS_NOVALUE;
        }
        slapi_rwlock_unlock(rlcdp->rlcd_rwlock);
    }


log_and_return:
    if (loglevel_is_set(LDAP_DEBUG_TRACE)) {
        if (rc == SLAPI_RESLIMIT_STATUS_SUCCESS) {
            slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "slapi_reslimit_get_integer_limit",
                          "<= returning SUCCESS, value=%d\n", *limitp);
        } else if (rc == SLAPI_RESLIMIT_STATUS_NOVALUE) {
            slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "slapi_reslimit_get_integer_limit",
                          "<= returning NO VALUE\n");
        } else {
            slapi_log_err(SLAPI_RESLIMIT_TRACELEVEL, "slapi_reslimit_get_integer_limit",
                          "<= returning ERROR %d\n", rc);
        }
    }

    return (rc);
}
/**** Public functions END ***********************************************/
