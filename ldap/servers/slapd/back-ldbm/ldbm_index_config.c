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
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* This file handles configuration information that is specific
 * to ldbm instance indexes.
 */

#include "back-ldbm.h"
#include "dblayer.h"

/* Forward declarations for the callbacks */
int ldbm_instance_index_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg); 
int ldbm_instance_index_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry* entryBefore, Slapi_Entry* e, int *returncode, char *returntext, void *arg); 





/* attrinfo2ConfIndexes: converts attrinfo into "pres,eq,sub,approx" 
 *   as seen in index entries within dse.ldif
 */
static char *attrinfo2ConfIndexes (struct attrinfo *pai)
{ 
    char buffer[128];

    buffer[0] = '\0';
    if (!(IS_INDEXED( pai->ai_indexmask ))) { /* skip if no index */
	strcat (buffer, "none"); 
    }

    if (pai->ai_indexmask & INDEX_PRESENCE) { 
	if (strlen (buffer)) {
            strcat (buffer, ",");
	}
	strcat (buffer, "pres");
    } 
    if (pai->ai_indexmask & INDEX_EQUALITY) { 
	if (strlen (buffer)) { 
            strcat (buffer, ","); 
	}
	strcat (buffer, "eq"); 
    } 
    if (pai->ai_indexmask & INDEX_APPROX) { 
	if (strlen(buffer)) { 
            strcat (buffer, ","); 
	} 
	strcat (buffer, "approx"); 
    } 
    if (pai->ai_indexmask & INDEX_SUB) { 
	if (strlen (buffer)) { 
            strcat (buffer, ",");
	} 
	strcat (buffer, "sub"); 
    }
    if (entryrdn_get_switch()) { /* subtree-rename: on */
        if (pai->ai_indexmask & INDEX_SUBTREE) { 
            if (strlen (buffer)) { 
                strcat (buffer, ",");
            } 
            strcat (buffer, "subtree"); 
        }
    }

    return (slapi_ch_strdup (buffer) );
}


/* attrinfo2ConfMatchingRules: converts attrinfo into matching rule oids, as
 * seen in index entries within dse.ldif
 */
static char *attrinfo2ConfMatchingRules (struct attrinfo *pai)
{ 
    int i;
    char buffer[1024];

    buffer[0] = '\0';

    if (pai->ai_index_rules) {
	strcat (buffer, "\t");
	for (i = 0; pai->ai_index_rules[i]; i++) {
            PL_strcatn (buffer, sizeof(buffer), pai->ai_index_rules[i]);
            if (pai->ai_index_rules[i+1]) {
		PL_strcatn (buffer, sizeof(buffer), ",");
            }
	}
    }
    return (slapi_ch_strdup (buffer) );
}


/* used by the two callbacks below, to parse an index entry into something
 * awkward that we can pass to attr_index_config().
 */
#define MAX_TMPBUF      1024
#define ZCAT_SAFE(_buf, _x1, _x2) do { \
    if (strlen(_buf) + strlen(_x1) + strlen(_x2) + 2 < MAX_TMPBUF) { \
        strcat(_buf, _x1); \
        strcat(_buf, _x2); \
    } \
} while (0)
static int ldbm_index_parse_entry(ldbm_instance *inst, Slapi_Entry *e,
                                  const char *trace_string,
                                  char **index_name)
{
    char *arglist[] = { NULL, NULL, NULL, NULL };
    int argc = 0, i;
    int isFirst;
    Slapi_Attr *attr;
    const struct berval *attrValue;
    Slapi_Value *sval;
    char tmpBuf[MAX_TMPBUF];

    /* Get the name of the attribute to index which will be the value
     * of the cn attribute. */
    if (slapi_entry_attr_find(e, "cn", &attr) != 0) {
        LDAPDebug(LDAP_DEBUG_ANY, "Warning: malformed index entry %s\n",
                  slapi_entry_get_dn(e), 0, 0);
        return LDAP_OPERATIONS_ERROR;
    }

    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);
    if (NULL == attrValue->bv_val || 0 == strlen(attrValue->bv_val)) {
        LDAPDebug(LDAP_DEBUG_ANY,
                  "Warning: malformed index entry %s -- empty index name\n",
                      slapi_entry_get_dn(e), 0, 0);
        return LDAP_OPERATIONS_ERROR;
    }
    arglist[argc++] = slapi_ch_strdup(attrValue->bv_val);
    if (index_name != NULL) {
        *index_name = slapi_ch_strdup(attrValue->bv_val);
    }

    /* Get the list of index types from the entry. */
    if (0 == slapi_entry_attr_find(e, "nsIndexType", &attr)) {
        tmpBuf[0] = 0;
        isFirst = 1;
        for (i = slapi_attr_first_value(attr, &sval); i != -1;
             i = slapi_attr_next_value(attr, i, &sval)) {
            attrValue = slapi_value_get_berval(sval);
            if (NULL != attrValue->bv_val && strlen(attrValue->bv_val) > 0) {
                if (isFirst) {
                    ZCAT_SAFE(tmpBuf, "", attrValue->bv_val);
                    isFirst = 0;
                } else {
                    ZCAT_SAFE(tmpBuf, ",", attrValue->bv_val);
                }
            }
        }
        if (0 == tmpBuf[0]) {
            LDAPDebug(LDAP_DEBUG_ANY,
                     "Warning: malformed index entry %s -- empty nsIndexType\n",
                     slapi_entry_get_dn(e), 0, 0);
            slapi_ch_free_string(index_name);
            for (i = 0; i < argc; i++) {
                slapi_ch_free((void **)&arglist[i]);
            }
            return LDAP_OPERATIONS_ERROR;
        }
        arglist[argc++] = slapi_ch_strdup(tmpBuf);
    }

    tmpBuf[0] = 0;
    /* Get the list of matching rules from the entry. */
    if (0 == slapi_entry_attr_find(e, "nsMatchingRule", &attr)) {
        isFirst = 1;
        for (i = slapi_attr_first_value(attr, &sval); i != -1;
             i = slapi_attr_next_value(attr, i, &sval)) {
            attrValue = slapi_value_get_berval(sval);
            if (NULL != attrValue->bv_val && strlen(attrValue->bv_val) > 0) {
                if (isFirst) {
                    ZCAT_SAFE(tmpBuf, "", attrValue->bv_val);
                } else {
                    ZCAT_SAFE(tmpBuf, ",", attrValue->bv_val);
                }
            }
        }
    }

    /* Get the substr begin length. note: pick the first value. */
    if (0 == slapi_entry_attr_find(e, INDEX_ATTR_SUBSTRBEGIN, &attr)) {
        i = slapi_attr_first_value(attr, &sval);
        if (-1 != i) {
            attrValue = slapi_value_get_berval(sval);
            if (NULL != attrValue->bv_val && strlen(attrValue->bv_val) > 0) {
                if (0 == tmpBuf[0]) {
                    PR_snprintf(tmpBuf, MAX_TMPBUF, "%s=%s",
                                INDEX_ATTR_SUBSTRBEGIN,  attrValue->bv_val);
                } else {
                    int tmpbuflen = strlen(tmpBuf);
                    char *p = tmpBuf + tmpbuflen;
                    PR_snprintf(p, MAX_TMPBUF - tmpbuflen, ",%s=%s",
                                INDEX_ATTR_SUBSTRBEGIN,  attrValue->bv_val);
                }
            }
        }
    }

    /* Get the substr middle length. note: pick the first value. */
    if (0 == slapi_entry_attr_find(e, INDEX_ATTR_SUBSTRMIDDLE, &attr)) {
        i = slapi_attr_first_value(attr, &sval);
        if (-1 != i) {
            attrValue = slapi_value_get_berval(sval);
            if (NULL != attrValue->bv_val && strlen(attrValue->bv_val) > 0) {
                if (0 == tmpBuf[0]) {
                    PR_snprintf(tmpBuf, MAX_TMPBUF, "%s=%s",
                                INDEX_ATTR_SUBSTRMIDDLE,  attrValue->bv_val);
                } else {
                    int tmpbuflen = strlen(tmpBuf);
                    char *p = tmpBuf + tmpbuflen;
                    PR_snprintf(p, MAX_TMPBUF - tmpbuflen, ",%s=%s",
                                INDEX_ATTR_SUBSTRMIDDLE,  attrValue->bv_val);
                }
            }
        }
    }

    /* Get the substr end length. note: pick the first value. */
    if (0 == slapi_entry_attr_find(e, INDEX_ATTR_SUBSTREND, &attr)) {
        i = slapi_attr_first_value(attr, &sval);
        if (-1 != i) {
            attrValue = slapi_value_get_berval(sval);
            if (NULL != attrValue->bv_val && strlen(attrValue->bv_val) > 0) {
                if (0 == tmpBuf[0]) {
                    PR_snprintf(tmpBuf, MAX_TMPBUF, "%s=%s",
                                INDEX_ATTR_SUBSTREND,  attrValue->bv_val);
                } else {
                    int tmpbuflen = strlen(tmpBuf);
                    char *p = tmpBuf + tmpbuflen;
                    PR_snprintf(p, MAX_TMPBUF - tmpbuflen, ",%s=%s",
                                INDEX_ATTR_SUBSTREND,  attrValue->bv_val);
                }
            }
        }
    }
    if (0 != tmpBuf[0]) {
        arglist[argc++] = slapi_ch_strdup(tmpBuf);
    }

    arglist[argc] = NULL;
    attr_index_config(inst->inst_be, (char *)trace_string, 0, argc, arglist, 0);
    for (i = 0; i < argc; i++) {
        slapi_ch_free((void **)&arglist[i]);
    }
    return LDAP_SUCCESS;
}

                                  
/*
 * Temp callback that gets called for each index entry when a new
 * instance is starting up.
 */
int 
ldbm_index_init_entry_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg)
{
    ldbm_instance *inst = (ldbm_instance *) arg;

    returntext[0] = '\0';
    *returncode = ldbm_index_parse_entry(inst, e, "from ldbm instance init",
                                         NULL);
    if (*returncode == LDAP_SUCCESS) {
        return SLAPI_DSE_CALLBACK_OK;
    } else {
        PR_snprintf(returntext, SLAPI_DSE_RETURNTEXT_SIZE, "Problem initializing index entry %s\n",
                slapi_entry_get_dn(e));
        return SLAPI_DSE_CALLBACK_ERROR;
    }
}

/* 
 * Config DSE callback for index additions.
 */	
int 
ldbm_instance_index_config_add_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* eAfter, int *returncode, char *returntext, void *arg) 
{ 
    ldbm_instance *inst = (ldbm_instance *) arg;
    char *index_name;

    returntext[0] = '\0';
    *returncode = ldbm_index_parse_entry(inst, e, "from DSE add", &index_name);
    if (*returncode == LDAP_SUCCESS) {
        struct attrinfo *ai = NULL;

        /* if the index is a "system" index, we assume it's being added by
         * by the server, and it's okay for the index to go online immediately.
         * if not, we set the index "offline" so it won't actually be used
         * until someone runs db2index on it.
         */
        if (! ldbm_attribute_always_indexed(index_name)) {
            ainfo_get(inst->inst_be, index_name, &ai);
            PR_ASSERT(ai != NULL);
            ai->ai_indexmask |= INDEX_OFFLINE;
        }
        slapi_ch_free((void **)&index_name);
        return SLAPI_DSE_CALLBACK_OK;
    } else {
        return SLAPI_DSE_CALLBACK_ERROR;
    }
}

/*
 * Config DSE callback for index deletes.
 */	
int 
ldbm_instance_index_config_delete_callback(Slapi_PBlock *pb, Slapi_Entry* e, Slapi_Entry* entryAfter, int *returncode, char *returntext, void *arg) 
{ 
  ldbm_instance *inst = (ldbm_instance *) arg;
  char *arglist[4];
  Slapi_Attr *attr;
  Slapi_Value *sval;
  const struct berval *attrValue;
  int argc = 0;
  int rc = SLAPI_DSE_CALLBACK_OK;
  struct attrinfo *ainfo = NULL;
  
  returntext[0] = '\0';
  *returncode = LDAP_SUCCESS;
  
  slapi_entry_attr_find(e, "cn", &attr);
  slapi_attr_first_value(attr, &sval);
  attrValue = slapi_value_get_berval(sval);
  
  arglist[argc++] = slapi_ch_strdup(attrValue->bv_val);
  arglist[argc++] = slapi_ch_strdup("none");
  arglist[argc] = NULL;
  attr_index_config(inst->inst_be, "From DSE delete", 0, argc, arglist, 0);
  slapi_ch_free((void **)&arglist[0]);
  slapi_ch_free((void **)&arglist[1]);
  
  ainfo_get(inst->inst_be, attrValue->bv_val, &ainfo);
  
  if (NULL == ainfo) {
    *returncode = LDAP_UNAVAILABLE;
    rc = SLAPI_DSE_CALLBACK_ERROR;
  } else {
    if (dblayer_erase_index_file(inst->inst_be, ainfo, 0 /* do chkpt */)) {
      *returncode = LDAP_UNWILLING_TO_PERFORM;
      rc = SLAPI_DSE_CALLBACK_ERROR;
    }
  }
  
  return rc;
}

/*
 * Config DSE callback for index entry changes.
 *
 * this function is huge!
 */
int
ldbm_instance_index_config_modify_callback(Slapi_PBlock *pb, Slapi_Entry *e,
        Slapi_Entry *entryAfter, int *returncode, char *returntext, void *arg)
{
    ldbm_instance *inst = (ldbm_instance *)arg;
    Slapi_Attr *attr;
    Slapi_Value *sval;
    const struct berval *attrValue;
    struct attrinfo *ainfo = NULL;
    LDAPMod **mods;
    char *arglist[4] = {0};
    char *config_attr;
    char *origIndexTypes = NULL;
    char *origMatchingRules = NULL;
    char **origIndexTypesArray = NULL;
    char **origMatchingRulesArray = NULL;
    char **addIndexTypesArray = NULL;
    char **addMatchingRulesArray = NULL;
    char **deleteIndexTypesArray = NULL;
    char **deleteMatchingRulesArray = NULL;
    int i, j;
    int dodeletes = 0;
    char tmpBuf[MAX_TMPBUF];
    int rc = SLAPI_DSE_CALLBACK_OK;

    returntext[0] = '\0';
    *returncode = LDAP_SUCCESS;
    slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);

    slapi_entry_attr_find(e, "cn", &attr);
    slapi_attr_first_value(attr, &sval);
    attrValue = slapi_value_get_berval(sval);

    ainfo_get(inst->inst_be, attrValue->bv_val, &ainfo);
    if (NULL == ainfo) {
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    origIndexTypes = attrinfo2ConfIndexes(ainfo);
    if (NULL == origIndexTypes) {
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    origMatchingRules = attrinfo2ConfMatchingRules(ainfo);
    if (NULL == origMatchingRules) {
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    origIndexTypesArray = slapi_str2charray(origIndexTypes, ",");
    if (NULL == origIndexTypesArray) {
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    origMatchingRulesArray = slapi_str2charray(origMatchingRules, ",");
    if (NULL == origMatchingRulesArray) {
        rc = SLAPI_DSE_CALLBACK_ERROR;
        goto out;
    }

    for (i = 0; mods[i] != NULL; i++) {
        config_attr = (char *)mods[i]->mod_type;

        if (strcasecmp(config_attr, "nsIndexType") == 0) {
            if (SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
                for (j = 0; mods[i]->mod_bvalues[j] != NULL; j++) {
                    charray_add(&addIndexTypesArray,
                        slapi_ch_strdup(mods[i]->mod_bvalues[j]->bv_val));
                }
                continue;
            }
            if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
                if ((mods[i]->mod_bvalues == NULL) ||
                    (mods[i]->mod_bvalues[0] == NULL)) {
                    if (deleteIndexTypesArray) {
                        charray_free(deleteIndexTypesArray);
                    }
                    deleteIndexTypesArray = charray_dup(origIndexTypesArray);
                } else {
                    for (j = 0; mods[i]->mod_bvalues[j] != NULL; j++) {
                        charray_add(&deleteIndexTypesArray,
                            slapi_ch_strdup(mods[i]->mod_bvalues[j]->bv_val));
                    }
                }
                continue;
            }
        }
        if (strcasecmp(config_attr, "nsMatchingRule") == 0) {
            if (SLAPI_IS_MOD_ADD(mods[i]->mod_op)) {
                for (j = 0; mods[i]->mod_bvalues[j] != NULL; j++) {
                    charray_add(&addMatchingRulesArray,
                        slapi_ch_strdup(mods[i]->mod_bvalues[j]->bv_val));
                }
                continue;
            }
            if (SLAPI_IS_MOD_DELETE(mods[i]->mod_op)) {
                if ((mods[i]->mod_bvalues == NULL) ||
                    (mods[i]->mod_bvalues[0] == NULL)) {
                    if (deleteMatchingRulesArray) {
                        charray_free(deleteMatchingRulesArray);
                    }
                    deleteMatchingRulesArray = charray_dup(origMatchingRulesArray);
                } else {
                    for (j = 0; mods[i]->mod_bvalues[j] != NULL; j++) {
                        charray_add(&deleteMatchingRulesArray,
                            slapi_ch_strdup(mods[i]->mod_bvalues[j]->bv_val));
                    }
                }
                continue;
            }
        }
    }

    /* create the new set of index types */
    if (deleteIndexTypesArray) {
        for (i = 0; origIndexTypesArray[i] != NULL; i++) {
            if (charray_inlist(deleteIndexTypesArray,
                               origIndexTypesArray[i])) {
                slapi_ch_free((void **)&(origIndexTypesArray[i]));
                dodeletes = 1;
                if (origIndexTypesArray[i+1] != NULL) {
                    for (j = i+1; origIndexTypesArray[j] != NULL; j++) {
                        origIndexTypesArray[j-1] = origIndexTypesArray[j];
                    }
                    origIndexTypesArray[j-1] = NULL;
                    i--;
                }
            }
        }
    }

    if (addIndexTypesArray) {
        for (i = 0; addIndexTypesArray[i] != NULL; i++) {
            if (!charray_inlist(origIndexTypesArray, addIndexTypesArray[i])) {
                charray_add(&origIndexTypesArray,
                    slapi_ch_strdup(addIndexTypesArray[i]));
            }
        }
    }

    if (deleteMatchingRulesArray) {
        for (i = 0; origMatchingRulesArray[i] != NULL; i++) {
            if (charray_inlist(deleteMatchingRulesArray, 
                               origMatchingRulesArray[i])) {
                slapi_ch_free((void **)&(origMatchingRulesArray[i]));
                dodeletes = 1;
                if (origMatchingRulesArray[i+1] != NULL) {
                    for (j = i+1; origMatchingRulesArray[j] != NULL; j++) {
                        origMatchingRulesArray[j-1] = origMatchingRulesArray[j];
                    }
                    origMatchingRulesArray[j-1] = NULL;
                    i--;
                }
            }
        }
    }

    if (addMatchingRulesArray) {
        for (i = 0; addMatchingRulesArray[i] != NULL; i++) {
            if (!charray_inlist(origMatchingRulesArray, 
                                addMatchingRulesArray[i])) {
                charray_add(&origMatchingRulesArray,
                    slapi_ch_strdup(addMatchingRulesArray[i]));
            }
        }
    }

    if (dodeletes) {
        i = 0;
        arglist[i++] = slapi_ch_strdup(attrValue->bv_val);
        arglist[i++] = slapi_ch_strdup("none");
        arglist[i] = NULL;
        attr_index_config(inst->inst_be, "from DSE modify", 0, i, arglist, 0);

	/* Free args */
	slapi_ch_free((void **)&arglist[0]);
	slapi_ch_free((void **)&arglist[1]);
    }

    i = 0;
    arglist[i++] = slapi_ch_strdup(attrValue->bv_val);
    if (origIndexTypesArray && origIndexTypesArray[0]) {
        tmpBuf[0] = 0;
        ZCAT_SAFE(tmpBuf, "", origIndexTypesArray[0]);
        for (j = 1; origIndexTypesArray[j] != NULL; j++) {
            ZCAT_SAFE(tmpBuf, ",", origIndexTypesArray[j]);
        }
        arglist[i++] = slapi_ch_strdup(tmpBuf);
    } else {
        arglist[i++] = slapi_ch_strdup("none");
    }

    if (origMatchingRulesArray && origMatchingRulesArray[0]) {
        tmpBuf[0] = 0;
        ZCAT_SAFE(tmpBuf, "", origMatchingRulesArray[0]);
        for (j = 1; origMatchingRulesArray[j] != NULL; j++) {
            ZCAT_SAFE(tmpBuf, ",", origMatchingRulesArray[j]);
        }
        arglist[i++] = slapi_ch_strdup(tmpBuf);
    }

    arglist[i] = NULL;
    attr_index_config(inst->inst_be, "from DSE modify", 0, i, arglist, 0);

out:
    /* Free args */
    for (i=0; arglist[i]; i++) {
	slapi_ch_free((void **)&arglist[i]);
    }

    if(origIndexTypesArray) {
	charray_free(origIndexTypesArray);
    }
    if(origMatchingRulesArray) {
	charray_free(origMatchingRulesArray);
    }
    if(addIndexTypesArray) {
	charray_free(addIndexTypesArray);
    }
    if(deleteIndexTypesArray) {
	charray_free(deleteIndexTypesArray);
    }
    if(addMatchingRulesArray) {
	charray_free(addMatchingRulesArray);
    }
    if(deleteMatchingRulesArray) {
	charray_free(deleteMatchingRulesArray);
    }
    if (origIndexTypes) {
	slapi_ch_free ((void **)&origIndexTypes);	
    }
    if (origMatchingRules) {
	slapi_ch_free ((void **)&origMatchingRules);	
    }

    return rc;
}

/* add index entries to the per-instance DSE (used only from instance.c) */
int ldbm_instance_config_add_index_entry(
    ldbm_instance *inst, 
    int argc, 
    char **argv,
    int flags
)
{
    char **attrs = NULL;
    char **indexes = NULL;
    char **matchingRules = NULL;
    char *eBuf;
    int i = 0;
    int j = 0;
    char *basetype = NULL;
    char tmpAttrsStr[256];
    char tmpIndexesStr[256];
    char tmpMatchingRulesStr[1024];
    struct ldbminfo *li = inst->inst_li;
    char *dn = NULL;
    int rc = 0;

    if ((argc < 2) || (NULL == argv) || (NULL == argv[0]) || 
        (NULL == argv[1])) {
        return(-1);
    }

    PL_strncpyz(tmpAttrsStr,argv[0], sizeof(tmpAttrsStr));
    attrs = slapi_str2charray( tmpAttrsStr, "," );
    PL_strncpyz(tmpIndexesStr,argv[1], sizeof(tmpIndexesStr));
    indexes = slapi_str2charray( tmpIndexesStr, ",");

    if(argc > 2) {
        PL_strncpyz(tmpMatchingRulesStr,argv[2], sizeof(tmpMatchingRulesStr));
        matchingRules = slapi_str2charray( tmpMatchingRulesStr, ",");
    }

    for(i=0; attrs && attrs[i] !=NULL; i++)
    {
        if('\0' == attrs[i][0]) continue;
        basetype = slapi_attr_basetype(attrs[i], NULL, 0);
        dn = slapi_create_dn_string("cn=%s,cn=index,cn=%s,cn=%s,cn=plugins,cn=config", 
                            basetype, inst->inst_name, li->li_plugin->plg_name);
        if (NULL == dn) {
            LDAPDebug(LDAP_DEBUG_ANY,
                      "ldbm_instance_config_add_index_entry: "
                      "failed create index dn with type %s for plugin %s, "
                      "instance %s\n",
                      basetype, inst->inst_li->li_plugin->plg_name,
                      inst->inst_name);
            slapi_ch_free((void**)&basetype);
            rc = -1;
            goto done;
        }
        eBuf = PR_smprintf(
                "dn: %s\n"
                "objectclass: top\n"
                "objectclass: nsIndex\n"
                "cn: %s\n"
                "nsSystemIndex: %s\n",
                dn, basetype,
                (ldbm_attribute_always_indexed(basetype)?"true":"false"));
        slapi_ch_free_string(&dn);
        for(j=0; indexes && indexes[j] != NULL; j++)
        {
            eBuf = PR_sprintf_append(eBuf, "nsIndexType:%s\n", indexes[j]);
        }
        if((argc>2)&&(argv[2]))
        {
            for(j=0; matchingRules && matchingRules[j] != NULL; j++)
            { 
                eBuf = PR_sprintf_append(eBuf, "nsMatchingRule:%s\n", matchingRules[j]);
            }
        }

        ldbm_config_add_dse_entry(li, eBuf, flags);
        if (eBuf) {
            PR_smprintf_free(eBuf);
        }

        slapi_ch_free((void**)&basetype);
    }

done:
    charray_free(attrs);
    charray_free(indexes);
    charray_free(matchingRules);
    return rc;
}

int
ldbm_instance_index_config_enable_index(ldbm_instance *inst, Slapi_Entry* e)
{
    char *index_name;
    int rc;

    rc=ldbm_index_parse_entry(inst, e, "from DSE add", &index_name);
    if (rc == LDAP_SUCCESS) {
        struct attrinfo *ai = NULL;

        /* Assume the caller knows if it is OK to go online immediatly */

        ainfo_get(inst->inst_be, index_name, &ai);
        PR_ASSERT(ai != NULL);
        ai->ai_indexmask &= ~INDEX_OFFLINE;
        slapi_ch_free((void **)&index_name);
    } 
    return rc;
}


/*
** create the default user-defined indexes
*/

int ldbm_instance_create_default_user_indexes(ldbm_instance *inst)
{

    /*
    ** Search for user-defined default indexes and add them
    ** to the backend instance beeing created.
    */

    Slapi_PBlock *aPb;
    Slapi_Entry **entries = NULL;
    Slapi_Attr *attr;
    Slapi_Value *sval = NULL;
    const struct berval *attrValue;
    char *argv[ 8 ];
    char tmpBuf[MAX_TMPBUF];
    char tmpBuf2[MAX_TMPBUF];
    int argc;
    char *basedn = NULL;

    struct ldbminfo *li;

    /* write the dse file only on the final index */
    int flags = LDBM_INSTANCE_CONFIG_DONT_WRITE;

    if (NULL == inst) {
        LDAPDebug(LDAP_DEBUG_ANY, 
		"Warning: can't initialize default user indexes (invalid instance).\n", 0,0,0);
        return -1;
    }

    li = inst->inst_li;
    strcpy(tmpBuf,"");

    /* Construct the base dn of the subtree that holds the default user indexes. */
	basedn = slapi_create_dn_string("cn=default indexes,cn=config,cn=%s,cn=plugins,cn=config", 
										li->li_plugin->plg_name);
	if (NULL == basedn) {
		LDAPDebug1Arg(LDAP_DEBUG_ANY,
				      "ldbm_instance_create_default_user_indexes: "
				      "failed create default index dn for plugin %s\n",
				      inst->inst_li->li_plugin->plg_name);
        return -1;
	}

    /* Do a search of the subtree containing the index entries */
    aPb = slapi_pblock_new();
    slapi_search_internal_set_pb(aPb, basedn, LDAP_SCOPE_SUBTREE, 
	"(objectclass=nsIndex)", NULL, 0 , NULL, NULL, li->li_identity, 0);
    slapi_search_internal_pb (aPb);
    slapi_pblock_get(aPb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES, &entries);
    if (entries!=NULL) {
        int i,j;
        for (i=0; entries[i]!=NULL; i++) {

                /* Get the name of the attribute to index which will be the value
                 * of the cn attribute. */

                if (slapi_entry_attr_find(entries[i], "cn", &attr) != 0) {
                        LDAPDebug(LDAP_DEBUG_ANY,"Warning: malformed index entry %s. Index ignored.\n",
                                slapi_entry_get_dn(entries[i]), 0, 0);
                        continue;
                }
                slapi_attr_first_value(attr, &sval);
                attrValue = slapi_value_get_berval(sval);
                argv[0] = attrValue->bv_val;
                argc=1;

                /* Get the list of index types from the entry. */

                if (0 == slapi_entry_attr_find(entries[i], "nsIndexType", &attr)) {
                        for (j = slapi_attr_first_value(attr, &sval); j != -1;
                                j = slapi_attr_next_value(attr, j, &sval)) {
                                attrValue = slapi_value_get_berval(sval);
                                if (0 == j) {
                                        tmpBuf[0] = 0;
                                        ZCAT_SAFE(tmpBuf, "", attrValue->bv_val);
                                } else {
                                        ZCAT_SAFE(tmpBuf, ",", attrValue->bv_val);
                                }
                        }
                        argv[argc]=tmpBuf;
                        argc++;
                }

                /* Get the list of matching rules from the entry. */

                if (0 == slapi_entry_attr_find(entries[i], "nsMatchingRule", &attr)) {
                        for (j = slapi_attr_first_value(attr, &sval); j != -1;
                                j = slapi_attr_next_value(attr, j, &sval)) {
                                attrValue = slapi_value_get_berval(sval);
                                if (0 == j) {
                                        tmpBuf2[0] = 0;
                                        ZCAT_SAFE(tmpBuf2, "", attrValue->bv_val);
                                } else {
                                        ZCAT_SAFE(tmpBuf2, ",", attrValue->bv_val);
                                }
                        }
                        argv[argc]=tmpBuf2;
                        argc++;
                }

                argv[argc]=NULL;

                /* Create the index entry in the backend */

                if (entries[i+1] == NULL) {
                    /* write the dse file only on the final index */
                    flags = 0;
                }

                ldbm_instance_config_add_index_entry(inst, argc, argv, flags);

                /* put the index online */

                ldbm_instance_index_config_enable_index(inst, entries[i]);
        }
    }

    slapi_free_search_results_internal(aPb);
    slapi_pblock_destroy(aPb);
    slapi_ch_free_string(&basedn);
    return 0;
}
