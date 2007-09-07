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
 * Copyright (C) 2007 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* dbverify.c - verify database files */

#include "back-ldbm.h"
#include "dblayer.h"

static int 
dbverify_ext( ldbm_instance *inst, int verbose )
{
    char dbdir[MAXPATHLEN];
    char *filep           = NULL;
    PRDir *dirhandle      = NULL;
    PRDirEntry *direntry  = NULL;
    backend *be           = inst->inst_be;
    DB *dbp               = NULL;
    int tmplen            = 0;
    int filelen           = 0;
    int rval              = 1;
    int rval_main         = 0;
    struct ldbminfo *li   = inst->inst_li;
    dblayer_private *priv = (dblayer_private*)li->li_dblayer_private;
    struct dblayer_private_env *pEnv = priv->dblayer_env;

    dbdir[sizeof(dbdir)-1] = '\0';
    PR_snprintf(dbdir, sizeof(dbdir), "%s/%s", inst->inst_parent_dir_name,
                                               inst->inst_dir_name);
    if ('\0' != dbdir[sizeof(dbdir)-1]) /* overflown */
    {
        slapi_log_error(SLAPI_LOG_FATAL, "DB verify",
                        "db path too long: %s/%s\n",
                         inst->inst_parent_dir_name, inst->inst_dir_name, 0);
        return 1;
    }
    tmplen = strlen(dbdir);
    filep = dbdir + tmplen;
    filelen = sizeof(dbdir) - tmplen;

    /* run dbverify on each each db file */
    dirhandle = PR_OpenDir(dbdir);
    if (! dirhandle)
    {
        slapi_log_error(SLAPI_LOG_FATAL, "DB verify",
                        "PR_OpenDir (%s) failed (%d): %s\n", 
                        dbdir, PR_GetError(),slapd_pr_strerror(PR_GetError()));
        return 1;
    }
    while (NULL !=
          (direntry = PR_ReadDir(dirhandle, PR_SKIP_DOT | PR_SKIP_DOT_DOT)))
    {
        /* struct attrinfo *ai = NULL; */
        char *p             = NULL;
        dbp = NULL;

        if (!direntry->name)
        {
            break;
        }
        if (!strstr(direntry->name, LDBM_FILENAME_SUFFIX)) /* non db file */
        {
            continue;
        }
        if (sizeof(direntry->name) + 2 > filelen)
        {
            slapi_log_error(SLAPI_LOG_FATAL, "DB verify",
                                        "db path too long: %s/%s%s\n",
                                         dbdir, direntry->name, 0);
            continue;
        }
        PR_snprintf(filep, filelen, "/%s", direntry->name);
        rval = db_create(&dbp, pEnv->dblayer_DB_ENV, 0);
        if (0 != rval)
        {
            slapi_log_error(SLAPI_LOG_FATAL, "DB verify",
                        "Unable to create id2entry db file %d\n", rval);
            return rval;
        }
#define VLVPREFIX "vlv#"
        if ((0 != strncmp(direntry->name, ID2ENTRY, strlen(ID2ENTRY))) &&
            (0 != strncmp(direntry->name, VLVPREFIX, strlen(VLVPREFIX))))
        {
            rval = dbp->set_flags(dbp, DB_DUP | DB_DUPSORT);
            if (0 != rval)
            {
                slapi_log_error(SLAPI_LOG_FATAL, "DB verify",
                       "Unable to set DUP flags to db %d\n", rval);
                return rval;
            }

            rval = dbp->set_dup_compare(dbp, idl_new_compare_dups);
            if (0 != rval)
            {
                slapi_log_error(SLAPI_LOG_FATAL, "DB verify",
                       "Unable to set dup_compare to db %d\n", rval);
                return rval;
            }
        }
#undef VLVPREFIX
        rval = dbp->verify(dbp, dbdir, NULL, NULL, 0);
        if (0 == rval)
        {
            if (verbose)
            {
                slapi_log_error(SLAPI_LOG_FATAL, "DB verify",
                                                 "%s: ok\n", dbdir);
            }
        }
        else
        {
            slapi_log_error(SLAPI_LOG_FATAL, "DB verify",
                            "verify failed(%d): %s\n", rval, dbdir);
        }
        rval_main |= rval;
        *filep = '\0';
    }
    PR_CloseDir(dirhandle);

    return rval_main;
}

int 
ldbm_back_dbverify( Slapi_PBlock *pb )
{
    struct ldbminfo *li   = NULL;
    Object *inst_obj      = NULL;
    ldbm_instance *inst   = NULL;
    int verbose           = 0;
    int rval              = 1;
    int rval_main         = 0;
    char **instance_names = NULL;

    slapi_log_error(SLAPI_LOG_TRACE, "verify DB", "Verifying db files...\n");
    slapi_pblock_get(pb, SLAPI_BACKEND_INSTANCE_NAME, &instance_names);
    slapi_pblock_get(pb, SLAPI_SEQ_VAL, &verbose);
    slapi_pblock_get(pb, SLAPI_PLUGIN_PRIVATE, &li);
    ldbm_config_load_dse_info(li);
    ldbm_config_internal_set(li, CONFIG_DB_TRANSACTION_LOGGING, "off");
    /* no write needed; choose EXPORT MODE */
    if (0 != dblayer_start(li, DBLAYER_EXPORT_MODE))
    {
        slapi_log_error(SLAPI_LOG_FATAL, "verify DB",
                                         "dbverify: Failed to init database\n");
        return rval;
    }

    /* server is up */
    slapi_log_error(SLAPI_LOG_TRACE, "verify DB", "server is up\n");
    if (instance_names) /* instance is specified */
    {
        char **inp = NULL;
        for (inp = instance_names; inp && *inp; inp++)
        {
            inst = ldbm_instance_find_by_name(li, *inp);
            if (inst)
            {
                rval_main |= dbverify_ext(inst, verbose);
            }
            else
            {
                rval_main |= 1;    /* no such instance */
            }
        }
    }
    else /* all instances */
    {
        for (inst_obj = objset_first_obj(li->li_instance_set); inst_obj;
              inst_obj = objset_next_obj(li->li_instance_set, inst_obj))
        {
            inst = (ldbm_instance *)object_get_data(inst_obj);
            /* check if an import/restore is already ongoing... */
            if (instance_set_busy(inst) != 0)
            {
                /* standalone, only.  never happens */
                slapi_log_error(SLAPI_LOG_FATAL, "upgrade DB",
                            "ldbm: '%s' is already in the middle of "
                            "another task and cannot be disturbed.\n",
                            inst->inst_name);
                continue; /* skip this instance and go to the next*/
            }
            rval_main |= dbverify_ext(inst, verbose);
        }
    }

    /* close the database down again */
    rval = dblayer_post_close(li, DBLAYER_EXPORT_MODE);
    if (0 != rval)
    {
        slapi_log_error(SLAPI_LOG_FATAL,
                        "verify DB", "Failed to close database\n");
    }

    return rval_main;
}
