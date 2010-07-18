/* --- BEGIN COPYRIGHT BLOCK ---
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
 * --- END COPYRIGHT BLOCK --- */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <stdio.h>
#include <time.h>
#include "ldap-agent.h"

static netsnmp_handler_registration *ops_handler = NULL;
static netsnmp_handler_registration *entries_handler = NULL;
static netsnmp_handler_registration *entity_handler = NULL;
static netsnmp_table_array_callbacks ops_cb;
static netsnmp_table_array_callbacks entries_cb;
static netsnmp_table_array_callbacks entity_cb;
extern server_instance *server_head;

/* Set table oids */
oid    dsOpsTable_oid[] = { dsOpsTable_TABLE_OID };
size_t dsOpsTable_oid_len = OID_LENGTH(dsOpsTable_oid);
oid    dsEntriesTable_oid[] = { dsEntriesTable_TABLE_OID };
size_t dsEntriesTable_oid_len = OID_LENGTH(dsEntriesTable_oid);
oid    dsEntityTable_oid[] = {dsEntityTable_TABLE_OID };
size_t dsEntityTable_oid_len = OID_LENGTH(dsEntityTable_oid);

/* Set trap oids */
oid    snmptrap_oid[] = { snmptrap_OID };
size_t snmptrap_oid_len = OID_LENGTH(snmptrap_oid);
oid    enterprise_oid[] = { enterprise_OID };
size_t enterprise_oid_len = OID_LENGTH(enterprise_oid);

/************************************************************
 * init_ldap_agent
 *
 * Initializes the agent and populates the stats table
 * with initial data.
 */
void
init_ldap_agent(void)
{
    server_instance *serv_p = NULL;
    stats_table_context *new_row = NULL;

    /* Define and create the table */
    initialize_stats_table();

    /* Initialize data for each server in conf file */
    for (serv_p = server_head; serv_p != NULL; serv_p = serv_p->next) {
        /* Check if this row already exists. */
        if (stats_table_find_row(serv_p->port) == NULL) {
            /* Create a new row */
            if ((new_row = stats_table_create_row(serv_p->port)) != NULL) {
                /* Set pointer for entity table */
                new_row->entity_tbl = serv_p;

                /* Set previous state of server to unknown */
                serv_p->server_state = STATE_UNKNOWN;

                /* Insert new row into the table */
                snmp_log(LOG_DEBUG, "Inserting row for server: %d\n", serv_p->port);
                CONTAINER_INSERT(ops_cb.container, new_row);
            } else {
                /* error during malloc of row */
                snmp_log(LOG_ERR, "Error creating row for server: %d\n",
                                   serv_p->port);
            }
        }
    }

    /* Force load data into stats table */
    load_stats_table(NULL, NULL);
}

/************************************************************
 * initialize_stats_table
 *
 * Initializes the stats table by defining its contents,
 * how it's structured, and registering callbacks.
 */
void
initialize_stats_table(void)
{
    netsnmp_table_registration_info *ops_table_info = NULL;
    netsnmp_table_registration_info *entries_table_info = NULL;
    netsnmp_table_registration_info *entity_table_info = NULL;
    /* This is a hacky way of figuring out if we are on Net-SNMP 5.2 or later */
#ifdef NETSNMP_CACHE_AUTO_RELOAD
    netsnmp_cache *stats_table_cache = NULL;
#endif

    if (ops_handler || entries_handler || entity_handler) {
        snmp_log(LOG_ERR, "initialize_stats_table called more than once.\n");
        return;
    }

    memset(&ops_cb, 0x00, sizeof(ops_cb));
    memset(&entries_cb, 0x00, sizeof(entries_cb));
    memset(&entity_cb, 0x00, sizeof(entity_cb));

    /* create table structures */
    ops_table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
    entries_table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
    entity_table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);

    /* create handlers */
    ops_handler = netsnmp_create_handler_registration("dsOpsTable",
                                                     netsnmp_table_array_helper_handler,
                                                     dsOpsTable_oid,
                                                     dsOpsTable_oid_len,
                                                     HANDLER_CAN_RONLY);
    entries_handler = netsnmp_create_handler_registration("dsEntriesTable",
                                                     netsnmp_table_array_helper_handler,
                                                     dsEntriesTable_oid,
                                                     dsEntriesTable_oid_len,
                                                     HANDLER_CAN_RONLY);
    entity_handler = netsnmp_create_handler_registration("dsEntityTable",
                                                     netsnmp_table_array_helper_handler,
                                                     dsEntityTable_oid,
                                                     dsEntityTable_oid_len,
                                                     HANDLER_CAN_RONLY);

    if (!ops_handler || !entries_handler || !entity_handler ||
        !ops_table_info || !entries_table_info || !entity_table_info) {
        /* malloc failed */
        snmp_log(LOG_ERR, "malloc failed in initialize_stats_table\n");
        SNMP_FREE(ops_table_info);
        SNMP_FREE(entries_table_info);
        SNMP_FREE(entity_table_info);
        return;
    }

    /* define table structures */
    netsnmp_table_helper_add_index(ops_table_info, ASN_INTEGER);
    netsnmp_table_helper_add_index(entries_table_info, ASN_INTEGER);
    netsnmp_table_helper_add_index(entity_table_info, ASN_INTEGER);

    ops_table_info->min_column = dsOpsTable_COL_MIN;
    ops_table_info->max_column = dsOpsTable_COL_MAX;
    entries_table_info->min_column = dsEntriesTable_COL_MIN;
    entries_table_info->max_column = dsEntriesTable_COL_MAX;
    entity_table_info->min_column = dsEntityTable_COL_MIN;
    entity_table_info->max_column = dsEntityTable_COL_MAX;

    /* 
     * Define callbacks and the container.  We only use one container that
     * all of the tables use.
     */
    ops_cb.get_value = dsOpsTable_get_value;
    ops_cb.container = netsnmp_container_find("dsOpsTable_primary:"
                                          "dsOpsTable:" "table_container");
    entries_cb.get_value = dsEntriesTable_get_value;
    entries_cb.container = ops_cb.container;
    entity_cb.get_value = dsEntityTable_get_value;
    entity_cb.container = ops_cb.container;

    /* registering the tables with the master agent */
    netsnmp_table_container_register(ops_handler, ops_table_info, &ops_cb,
                                     ops_cb.container, 1);
    netsnmp_table_container_register(entries_handler, entries_table_info, &entries_cb,
                                     entries_cb.container, 1);
    netsnmp_table_container_register(entity_handler, entity_table_info, &entity_cb,
                                     entity_cb.container, 1);

    /* Setup cache for auto reloading of stats */
#ifdef NETSNMP_CACHE_AUTO_RELOAD
    /* This is new api as of Net-SNMP 5.2 */
    stats_table_cache = netsnmp_cache_create(CACHE_REFRESH_INTERVAL, load_stats_table,
                                            NULL, dsOpsTable_oid, dsOpsTable_oid_len);
    stats_table_cache->flags |= NETSNMP_CACHE_DONT_FREE_EXPIRED;
    stats_table_cache->flags |= NETSNMP_CACHE_DONT_AUTO_RELEASE;
    stats_table_cache->flags |= NETSNMP_CACHE_AUTO_RELOAD;
    netsnmp_inject_handler(ops_handler, netsnmp_cache_handler_get(stats_table_cache));
#else
    /* Do things the old way.  This is only needed for Net-SNMP 5.1 and earlier. */
    netsnmp_inject_handler(ops_handler, netsnmp_get_cache_handler(CACHE_REFRESH_INTERVAL, load_stats_table,
                                            free_stats_table, dsOpsTable_oid, dsOpsTable_oid_len));
#endif
}

/************************************************************
 * stats_table_create_row
 *
 * Creates a new table row using the supplied port number as
 * the index, then returns a pointer to the new row.
 */
stats_table_context *
stats_table_create_row(unsigned long portnum)
{
    netsnmp_index index;
    stats_table_context *ctx = SNMP_MALLOC_TYPEDEF(stats_table_context);
    oid *index_oid = (oid *)malloc(sizeof(oid) * MAX_OID_LEN);

    /* Create index using port number */
    index_oid[0] = portnum;
    index.oids = index_oid;
    index.len = 1;

    /* Copy index into row structure */
    if (ctx && index_oid) {
        memcpy(&ctx->index, &index, sizeof(index));
        return ctx;
    } else {
        /* Error during malloc */
        snmp_log(LOG_ERR, "malloc failed in stats_table_create_row\n");
        return NULL;
    }
}

/************************************************************
 * stats_table_find_row
 *
 * Searches for a row by the port number.  Returns NULL if
 * the row doesn't exist.
 */
stats_table_context *
stats_table_find_row(unsigned long portnum)
{
    netsnmp_index index;
    oid index_oid[MAX_OID_LEN];
                                                                                                      
    index_oid[0] = portnum;
    index.oids = index_oid;
    index.len = 1;
                                                                                                      
    return (stats_table_context *)
        CONTAINER_FIND(ops_cb.container, &index);
}

/************************************************************
 * load_stats_table
 *
 * Reloads the stats into the table. This is called
 * automatically from the cache handler. This function
 * does not reload the entity table since it's static
 * information.  We also check if any traps need to
 * be sent here.
 */
int
load_stats_table(netsnmp_cache *cache, void *foo)
{
    server_instance *serv_p = NULL;
    stats_table_context *ctx = NULL;
    time_t previous_start;
    int previous_state;
    int stats_hdl = -1;
    sem_t *stats_sem = NULL;

    snmp_log(LOG_INFO, "Reloading stats.\n");

    /* Initialize data for each server in conf file */
    for (serv_p = server_head; serv_p != NULL; serv_p = serv_p->next) {
        if ((ctx = stats_table_find_row(serv_p->port)) != NULL) {
            /* Save previous state of the server to
             * see if a trap needs to be sent */
            previous_state = serv_p->server_state;
            previous_start = ctx->hdr_tbl.startTime;

            snmp_log(LOG_INFO, "Opening stats file (%s) for server: %d\n",
                     serv_p->stats_file, serv_p->port);

            /* Open and acquire semaphore */
            if ((stats_sem = sem_open(serv_p->stats_sem_name, 0)) == SEM_FAILED) {
                stats_sem = NULL;
                snmp_log(LOG_INFO, "Unable to open semaphore for server: %d\n", serv_p->port);
            } else {
                int i = 0;
                int got_sem = 0;

                for (i=0; i < SNMP_NUM_SEM_WAITS; i++) {
                    if (sem_trywait(stats_sem) == 0) {
                        got_sem = 1;
                        break;
                    }
                    PR_Sleep(PR_SecondsToInterval(1));
                }

                if (!got_sem) {
                    /* We're unable to get the semaphore.  Assume
                     * that the server is down. */
                    snmp_log(LOG_INFO, "Unable to acquire semaphore for server: %d\n", serv_p->port);
                    sem_close(stats_sem);
                    stats_sem = NULL;
                }
            }

            /* Open the stats file */
            if ((stats_sem == NULL) || (agt_mopen_stats(serv_p->stats_file, O_RDONLY, &stats_hdl) != 0)) {
                if (stats_sem) {
                    /* Release and close semaphore */
                    sem_post(stats_sem);
                    sem_close(stats_sem);
                }

                /* Server must be down */
                serv_p->server_state = SERVER_DOWN;
                /* Zero out the ops and entries tables */
                memset(&ctx->ops_tbl, 0x00, sizeof(ctx->ops_tbl));
                memset(&ctx->entries_tbl, 0x00, sizeof(ctx->entries_tbl));
                if (previous_state != SERVER_DOWN)
                    snmp_log(LOG_INFO, "Unable to open stats file (%s) for server: %d\n",
                                       serv_p->stats_file, serv_p->port);
            } else {
                /* Initialize ops table */
                if ( agt_mread_stats(stats_hdl, &ctx->hdr_tbl, &ctx->ops_tbl,
                                           &ctx->entries_tbl) != 0 )
                    snmp_log(LOG_ERR, "Unable to read stats file: %s\n",
                                       serv_p->stats_file);
                                                                                                                
                /* Close stats file */
                if ( agt_mclose_stats(stats_hdl) != 0 )
                    snmp_log(LOG_ERR, "Error closing stats file: %s\n",
                                       serv_p->stats_file);

                /* Release and close semaphore */
                sem_post(stats_sem);
                sem_close(stats_sem);

                /* Server must be down if the stats file hasn't been
                 * updated in a while */
                if (difftime(time(NULL), ctx->hdr_tbl.updateTime) >= UPDATE_THRESHOLD) {
                    serv_p->server_state = SERVER_DOWN;
                    if (previous_state != SERVER_DOWN)
                        snmp_log(LOG_INFO, "Stats file for server %d hasn't been updated"
                                    " in %d seconds.\n", serv_p->port, UPDATE_THRESHOLD);
                } else {
                    serv_p->server_state = SERVER_UP;
                }
            }

            /* If the state of the server changed since the last
             * load of the stats, send a trap. */
            if (previous_state != STATE_UNKNOWN) {
                if (serv_p->server_state != previous_state) {
                    if (serv_p->server_state == SERVER_UP) {
                        snmp_log(LOG_INFO, "Detected start of server: %d\n",
                                            serv_p->port);
                        send_DirectoryServerStart_trap(serv_p);
                    } else {
                        send_DirectoryServerDown_trap(serv_p);
                        /* Zero out the ops and entries tables */
                        memset(&ctx->ops_tbl, 0x00, sizeof(ctx->ops_tbl));
                        memset(&ctx->entries_tbl, 0x00, sizeof(ctx->entries_tbl));
                    } 
                } else if (ctx->hdr_tbl.startTime != previous_start) {
                    /* Send traps if the server has restarted since the last load */
                    snmp_log(LOG_INFO, "Detected restart of server: %d\n", serv_p->port);
                    send_DirectoryServerDown_trap(serv_p);
                    send_DirectoryServerStart_trap(serv_p);
                }
            }
        } else {
            /* Can't find our row.  This shouldn't ever happen. */
            snmp_log(LOG_ERR, "Row not found for server: %d\n",
                               serv_p->port);
        }
    }
    return 0;
}

/************************************************************
 * free_stats_table
 *
 * This function doesn't need to free anything since the
 * load_stats_table function doesn't allocate any memory
 * itself.  The cache handler requires us to have a callback
 * function for freeing the cache, so here it is.
 */
void
free_stats_table(netsnmp_cache *cache, void *foo)
{
    return;
}

/************************************************************
 * dsOpsTable_get_value
 *
 * This routine is called for get requests to copy the data
 * from the context to the varbind for the request. If the
 * context has been properly maintained, you don't need to
 * change in code in this fuction.
 */
int
dsOpsTable_get_value(netsnmp_request_info *request,
                     netsnmp_index * item,
                     netsnmp_table_request_info *table_info)
{
    PRUint64 *the_stat = NULL;
    integer64 new_val;
    netsnmp_variable_list *var = request->requestvb;
    stats_table_context *context = (stats_table_context *) item;

    switch (table_info->colnum) {

    case COLUMN_DSANONYMOUSBINDS:
        the_stat = &context->ops_tbl.dsAnonymousBinds;
        break;

    case COLUMN_DSUNAUTHBINDS:
        the_stat = &context->ops_tbl.dsUnAuthBinds;
        break;

    case COLUMN_DSSIMPLEAUTHBINDS:
        the_stat = &context->ops_tbl.dsSimpleAuthBinds;
        break;

    case COLUMN_DSSTRONGAUTHBINDS:
        the_stat = &context->ops_tbl.dsStrongAuthBinds;
        break;

    case COLUMN_DSBINDSECURITYERRORS:
        the_stat = &context->ops_tbl.dsBindSecurityErrors;
        break;

    case COLUMN_DSINOPS:
        the_stat = &context->ops_tbl.dsInOps;
        break;

    case COLUMN_DSREADOPS:
        the_stat = &context->ops_tbl.dsReadOps;
        break;

    case COLUMN_DSCOMPAREOPS:
        the_stat = &context->ops_tbl.dsCompareOps;
        break;

    case COLUMN_DSADDENTRYOPS:
        the_stat = &context->ops_tbl.dsAddEntryOps;
        break;

    case COLUMN_DSREMOVEENTRYOPS:
        the_stat = &context->ops_tbl.dsRemoveEntryOps;
        break;

    case COLUMN_DSMODIFYENTRYOPS:
        the_stat = &context->ops_tbl.dsModifyEntryOps;
        break;

    case COLUMN_DSMODIFYRDNOPS:
        the_stat = &context->ops_tbl.dsModifyRDNOps;
        break;

    case COLUMN_DSLISTOPS:
        the_stat = &context->ops_tbl.dsListOps;
        break;

    case COLUMN_DSSEARCHOPS:
        the_stat = &context->ops_tbl.dsSearchOps;
        break;

    case COLUMN_DSONELEVELSEARCHOPS:
        the_stat = &context->ops_tbl.dsOneLevelSearchOps;
        break;

    case COLUMN_DSWHOLESUBTREESEARCHOPS:
        the_stat = &context->ops_tbl.dsWholeSubtreeSearchOps;
        break;

    case COLUMN_DSREFERRALS:
        the_stat = &context->ops_tbl.dsReferrals;
        break;

    case COLUMN_DSCHAININGS:
        the_stat = &context->ops_tbl.dsChainings;
        break;

    case COLUMN_DSSECURITYERRORS:
        the_stat = &context->ops_tbl.dsSecurityErrors;
        break;

    case COLUMN_DSERRORS:
        the_stat = &context->ops_tbl.dsErrors;
        break;

    default:/* We shouldn't get here */
        snmp_log(LOG_ERR, "Unknown column in dsOpsTable_get_value\n");
        return SNMP_ERR_GENERR;
    }

    /* The Net-SNMP integer64 type isn't a true 64-bit value, but instead
     * a structure containing the high and low bits separately.  We need
     * to split our value appropriately. */
    new_val.low = *the_stat & 0x00000000ffffffff;
    new_val.high =  (*the_stat >> 32) & 0x00000000ffffffff;

    snmp_set_var_typed_value(var, ASN_COUNTER64,
                                 (u_char *) &new_val,
                                 sizeof(new_val));

    return SNMP_ERR_NOERROR;
}

/************************************************************
 * dsEntriesTable_get_value
 *
 * This routine is called for get requests to copy the data
 * from the context to the varbind for the request. If the
 * context has been properly maintained, you don't need to
 * change in code in this fuction.
 */
int
dsEntriesTable_get_value(netsnmp_request_info *request,
                     netsnmp_index * item,
                     netsnmp_table_request_info *table_info)
{
    PRUint64 *the_stat = NULL;
    integer64 new_val;
    netsnmp_variable_list *var = request->requestvb;
    stats_table_context *context = (stats_table_context *) item;
                                                                                                                
    switch (table_info->colnum) {
                                                                                                                
    case COLUMN_DSMASTERENTRIES:
        the_stat = &context->entries_tbl.dsMasterEntries;
        break;
                                                                                                                
    case COLUMN_DSCOPYENTRIES:
        the_stat = &context->entries_tbl.dsCopyEntries;
        break;
                                                                                                                
    case COLUMN_DSCACHEENTRIES:
        the_stat = &context->entries_tbl.dsCacheEntries;
        break;
                                                                                                                
    case COLUMN_DSCACHEHITS:
        the_stat = &context->entries_tbl.dsCacheHits;
        break;
                                                                                                                
    case COLUMN_DSSLAVEHITS:
        the_stat = &context->entries_tbl.dsSlaveHits;
        break;
                                                                                                                
    default:/* We shouldn't get here */
        snmp_log(LOG_ERR, "Unknown column in dsEntriesTable_get_value\n");
        return SNMP_ERR_GENERR;
    }

    /* The Net-SNMP integer64 type isn't a true 64-bit value, but instead
     * a structure containing the high and low bits separately.  We need
     * to split our value appropriately. */
    new_val.low = *the_stat & 0x00000000ffffffff;
    new_val.high =  (*the_stat >> 32) & 0x00000000ffffffff;

    snmp_set_var_typed_value(var, ASN_COUNTER64,
                                 (u_char *) &new_val,
                                 sizeof(new_val));

    return SNMP_ERR_NOERROR;
}

/************************************************************
 * dsEntityTable_get_value
 *
 * This routine is called for get requests to copy the data
 * from the context to the varbind for the request. If the
 * context has been properly maintained, you don't need to
 * change in code in this fuction.
 */
int
dsEntityTable_get_value(netsnmp_request_info *request,
                     netsnmp_index * item,
                     netsnmp_table_request_info *table_info)
{
    netsnmp_variable_list *var = request->requestvb;
    stats_table_context *context = (stats_table_context *) item;
                                                                                                                
    switch (table_info->colnum) {
                                                                                                                
    case COLUMN_DSENTITYDESCR:
        snmp_set_var_typed_value(var, ASN_OCTET_STR,
                                 (u_char *) context->hdr_tbl.dsDescription,
                                 strlen(context->hdr_tbl.dsDescription));
        break;
                                                                                                                
    case COLUMN_DSENTITYVERS:
        snmp_set_var_typed_value(var, ASN_OCTET_STR,
                                 (u_char *) context->hdr_tbl.dsVersion,
                                 strlen(context->hdr_tbl.dsVersion));
        break;
                                                                                                                
    case COLUMN_DSENTITYORG:
        snmp_set_var_typed_value(var, ASN_OCTET_STR,
                                 (u_char *) context->hdr_tbl.dsOrganization,
                                 strlen(context->hdr_tbl.dsOrganization));
        break;
                                                                                                                
    case COLUMN_DSENTITYLOCATION:
        snmp_set_var_typed_value(var, ASN_OCTET_STR,
                                 (u_char *) context->hdr_tbl.dsLocation,
                                 strlen(context->hdr_tbl.dsLocation));
        break;
                                                                                                                
    case COLUMN_DSENTITYCONTACT:
        snmp_set_var_typed_value(var, ASN_OCTET_STR,
                                 (u_char *) context->hdr_tbl.dsContact,
                                 strlen(context->hdr_tbl.dsContact));
        break;
                                                                                                                
    case COLUMN_DSENTITYNAME:
        snmp_set_var_typed_value(var, ASN_OCTET_STR,
                                 (u_char *) context->hdr_tbl.dsName,
                                 strlen(context->hdr_tbl.dsName));
        break;

    default:/* We shouldn't get here */
        snmp_log(LOG_ERR, "Unknown column in dsEntityTable_get_value\n");
        return SNMP_ERR_GENERR;
    }
    return SNMP_ERR_NOERROR;
}

/************************************************************
 * send_DirectoryServerDown_trap
 *
 * Sends off the server down trap.
 */
int
send_DirectoryServerDown_trap(server_instance *serv_p)
{
    netsnmp_variable_list *var_list = NULL;
    stats_table_context *ctx = NULL;

    /* Define the oids for the trap */
    oid DirectoryServerDown_oid[] = { DirectoryServerDown_OID };
    oid dsEntityDescr_oid[] = { dsEntityTable_TABLE_OID, 1, COLUMN_DSENTITYDESCR, 0 };
    oid dsEntityVers_oid[] = { dsEntityTable_TABLE_OID, 1, COLUMN_DSENTITYVERS, 0 };
    oid dsEntityLocation_oid[] = { dsEntityTable_TABLE_OID, 1, COLUMN_DSENTITYLOCATION, 0 };
    oid dsEntityContact_oid[] = { dsEntityTable_TABLE_OID, 1, COLUMN_DSENTITYCONTACT, 0 };

    dsEntityDescr_oid[3] = serv_p->port;
    dsEntityVers_oid[3] = serv_p->port;
    dsEntityLocation_oid[3] = serv_p->port;
    dsEntityContact_oid[3] = serv_p->port;

    snmp_log(LOG_INFO, "Sending down trap for server: %d\n", serv_p->port);

    /* Lookup row to get version string */
    if ((ctx = stats_table_find_row(serv_p->port)) == NULL) {
        snmp_log(LOG_ERR, "Malloc error finding row for server: %d\n", serv_p->port); 
        return 1;
    }

    /* Setup the variable list to send with the trap */
    snmp_varlist_add_variable(&var_list,
                              snmptrap_oid, OID_LENGTH(snmptrap_oid),
                              ASN_OBJECT_ID,
                              (u_char *) &DirectoryServerDown_oid,
                              sizeof(DirectoryServerDown_oid));
    snmp_varlist_add_variable(&var_list,
                              dsEntityDescr_oid,
                              OID_LENGTH(dsEntityDescr_oid), ASN_OCTET_STR,
                              (u_char *) ctx->hdr_tbl.dsDescription,
                              strlen(ctx->hdr_tbl.dsDescription));
    snmp_varlist_add_variable(&var_list,
                              dsEntityVers_oid,
                              OID_LENGTH(dsEntityVers_oid), ASN_OCTET_STR,
                              (u_char *) ctx->hdr_tbl.dsVersion,
                              strlen(ctx->hdr_tbl.dsVersion));
    snmp_varlist_add_variable(&var_list,
                              dsEntityLocation_oid,
                              OID_LENGTH(dsEntityLocation_oid),
                              ASN_OCTET_STR,
                              (u_char *) ctx->hdr_tbl.dsLocation,
                              strlen(ctx->hdr_tbl.dsLocation));
    snmp_varlist_add_variable(&var_list,
                              dsEntityContact_oid,
                              OID_LENGTH(dsEntityContact_oid),
                              ASN_OCTET_STR,
                              (u_char *) ctx->hdr_tbl.dsContact,
                              strlen(ctx->hdr_tbl.dsContact));

    /* Send the trap */
    send_v2trap(var_list);
    snmp_free_varbind(var_list);
                                                                                                                
    return SNMP_ERR_NOERROR;
}

/************************************************************
 * send_DirectoryServerStart_trap
 *
 * Sends off the server start trap.
 */
int
send_DirectoryServerStart_trap(server_instance *serv_p)
{
    netsnmp_variable_list *var_list = NULL;
    stats_table_context *ctx = NULL;

    /* Define the oids for the trap */
    oid DirectoryServerStart_oid[] = { DirectoryServerStart_OID };
    oid dsEntityDescr_oid[] = { dsEntityTable_TABLE_OID, 1, COLUMN_DSENTITYDESCR, 0 };
    oid dsEntityVers_oid[] = { dsEntityTable_TABLE_OID, 1, COLUMN_DSENTITYVERS, 0 };
    oid dsEntityLocation_oid[] = { dsEntityTable_TABLE_OID, 1, COLUMN_DSENTITYLOCATION, 0 };

    dsEntityDescr_oid[3] = serv_p->port;
    dsEntityVers_oid[3] = serv_p->port;
    dsEntityLocation_oid[3] = serv_p->port;

    snmp_log(LOG_INFO, "Sending start trap for server: %d\n", serv_p->port);

    /* Lookup row to get version string */
    if ((ctx = stats_table_find_row(serv_p->port)) == NULL) {
        snmp_log(LOG_ERR, "Malloc error finding row for server: %d\n", serv_p->port);
        return 1;
    }

    /* Setup the variable list to send with the trap */
    snmp_varlist_add_variable(&var_list,
                              snmptrap_oid, OID_LENGTH(snmptrap_oid),
                              ASN_OBJECT_ID,
                              (u_char *) &DirectoryServerStart_oid,
                              sizeof(DirectoryServerStart_oid));
    snmp_varlist_add_variable(&var_list,
                              dsEntityDescr_oid,
                              OID_LENGTH(dsEntityDescr_oid), ASN_OCTET_STR,
                              (u_char *) ctx->hdr_tbl.dsDescription,
                              strlen(ctx->hdr_tbl.dsDescription));
    snmp_varlist_add_variable(&var_list,
                              dsEntityVers_oid,
                              OID_LENGTH(dsEntityVers_oid), ASN_OCTET_STR,
                              (u_char *) ctx->hdr_tbl.dsVersion,
                              strlen(ctx->hdr_tbl.dsVersion));
    snmp_varlist_add_variable(&var_list,
                              dsEntityLocation_oid,
                              OID_LENGTH(dsEntityLocation_oid),
                              ASN_OCTET_STR,
                              (u_char *) ctx->hdr_tbl.dsLocation,
                              strlen(ctx->hdr_tbl.dsLocation));
                                                                                                                
    /* Send the trap */
    send_v2trap(var_list);
    snmp_free_varbind(var_list);
                                                                                                                
    return SNMP_ERR_NOERROR;
}
