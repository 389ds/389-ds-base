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

/*
 * uid.c
 *
 * Implements a directory server pre-operation plugin to test
 * attributes for uniqueness within a defined subtree in the
 * directory.
 *
 * Called uid.c since the original purpose of the plugin was to
 * check the uid attribute in user entries.
 */
#include <slapi-plugin.h>
#include <portable.h>
#include <string.h>
#include "plugin-utils.h"
#include "nspr.h"

#if defined( LDAP_DEBUG ) && !defined( DEBUG )
#define DEBUG
#endif

#define UNTAGGED_PARAMETER 12

/* Quoting routine - this should be in a library somewhere (slapi?) */
int ldap_quote_filter_value(
      char *value, int len,
      char *out, int maxLen,
      int *outLen);


static int search_one_berval(const char *baseDN, const char *attrName,
		const struct berval *value, const char *requiredObjectClass, const char *target);

/*
 * ISSUES:
 *   How should this plugin handle ACL issues?  It seems wrong to reject
 *   adds and modifies because there is already a conflicting UID, when
 *   the request would have failed because of an ACL check anyway.
 *
 *   This code currently defines a maximum filter string size of 512.  Is
 *   this large enough?
 *
 *   This code currently does not quote the value portion of the filter as
 *   it is created.  This is a bug.
 */

/* */
#define BEGIN do {
#define END } while(0);

/*
 * Slapi plugin descriptor
 */
static char *plugin_name = "NSUniqueAttr";
static Slapi_PluginDesc
pluginDesc = { 
	"NSUniqueAttr", VENDOR, DS_PACKAGE_VERSION,
	"Enforce unique attribute values" 
};
static void* plugin_identity = NULL;


/*
 * More information about constraint failure
 */
static char *moreInfo =
  "Another entry with the same attribute value already exists (attribute: \"%s\")";

static void
freePblock( Slapi_PBlock *spb ) {
  if ( spb )
  {
        slapi_free_search_results_internal( spb );
        slapi_pblock_destroy( spb );
  }
}

/* ------------------------------------------------------------ */
/*
 * op_error - Record (and report) an operational error.
 * name changed to uid_op_error so as not to conflict with the external function
 * of the same name thereby preventing compiler warnings.
 */
static int
uid_op_error(int internal_error)
{
  slapi_log_error(
	SLAPI_LOG_PLUGIN, 
	plugin_name, 
	"Internal error: %d\n", 
	internal_error);

  return LDAP_OPERATIONS_ERROR;
}

/* ------------------------------------------------------------ */
/*
 * Create an LDAP search filter from the attribute
 *   name and value supplied.
 */

static char *
create_filter(const char *attribute, const struct berval *value, const char *requiredObjectClass)
{
  char *filter;
  char *fp;
  char *max;
  int attrLen;
  int valueLen;
  int classLen = 0;
  int filterLen;

  PR_ASSERT(attribute);

  /* Compute the length of the required buffer */
  attrLen = strlen(attribute);

  if (ldap_quote_filter_value(value->bv_val, 
	value->bv_len, 0, 0, &valueLen))
    return 0;

  if (requiredObjectClass) {
    classLen = strlen(requiredObjectClass);
    /* "(&(objectClass=)())" == 19 */
    filterLen = attrLen + 1 + valueLen + classLen + 19 + 1;
  } else {
    filterLen = attrLen + 1 + valueLen + 1;
  }

  /* Allocate the buffer */
  filter = slapi_ch_malloc(filterLen);
  fp = filter;
  max = &filter[filterLen];

  /* Place AND expression and objectClass in filter */
  if (requiredObjectClass) {
    strcpy(fp, "(&(objectClass=");
    fp += 15;
    strcpy(fp, requiredObjectClass);
    fp += classLen;
    *fp++ = ')';
    *fp++ = '(';
  }

  /* Place attribute name in filter */
  strcpy(fp, attribute);
  fp += attrLen;

  /* Place comparison operator */
  *fp++ = '=';

  /* Place value in filter */
  if (ldap_quote_filter_value(value->bv_val, value->bv_len,
    fp, max-fp, &valueLen)) { slapi_ch_free((void**)&filter); return 0; }
  fp += valueLen;

  /* Close AND expression if a requiredObjectClass was set */
  if (requiredObjectClass) {
    *fp++ = ')';
    *fp++ = ')';
  }

  /* Terminate */
  *fp = 0;

  return filter;
}

/* ------------------------------------------------------------ */
/*
 * search - search a subtree for entries with a named attribute matching
 *   the list of values.  An entry matching the 'target' DN is
 *   not considered in the search.
 *
 * If 'attr' is NULL, the values are taken from 'values'.
 * If 'attr' is non-NULL, the values are taken from 'attr'.
 *
 * Return:
 *   LDAP_SUCCESS - no matches, or the attribute matches the
 *     target dn.
 *   LDAP_CONSTRAINT_VIOLATION - an entry was found that already
 *     contains the attribute value.
 *   LDAP_OPERATIONS_ERROR - a server failure.
 */
static int
search(const char *baseDN, const char *attrName, Slapi_Attr *attr,
  struct berval **values, const char *requiredObjectClass,
  const char *target)
{
  int result;

#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "SEARCH baseDN=%s attr=%s target=%s\n", baseDN, attrName, 
      target?target:"None");
#endif

  result = LDAP_SUCCESS;

  /* If no values, can't possibly be a conflict */
  if ( (Slapi_Attr *)NULL == attr && (struct berval **)NULL == values )
    return result;

  /*
   * Perform the search for each value provided
   *
   * Another possibility would be to search for all the values at once.
   * However, this is more complex (for filter creation) and unique
   * attributes values are probably only changed one at a time anyway.
   */
  if ( (Slapi_Attr *)NULL != attr )
  {
	Slapi_Value	*v = NULL;
	int	vhint = -1;

	for ( vhint = slapi_attr_first_value( attr, &v );
		vhint != -1 && LDAP_SUCCESS == result;
		vhint = slapi_attr_next_value( attr, vhint, &v ))
	{
	  result = search_one_berval(baseDN, attrName,
					slapi_value_get_berval(v),
					requiredObjectClass, target);
	}
  }
  else
  {
	for (;*values != NULL && LDAP_SUCCESS == result; values++)
	{
	  result = search_one_berval(baseDN, attrName, *values, requiredObjectClass,
					target);
	}
  }

#ifdef DEBUG
  slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
    "SEARCH result = %d\n", result);
#endif

  return( result );
}


static int
search_one_berval(const char *baseDN, const char *attrName,
		const struct berval *value, const char *requiredObjectClass,
		const char *target)
{
	int result;
    char *filter;
    Slapi_PBlock *spb;

	result = LDAP_SUCCESS;

	/* If no value, can't possibly be a conflict */
	if ( (struct berval *)NULL == value )
		return result;

    filter = 0;
    spb = 0;

    BEGIN
      int err;
      int sres;
      Slapi_Entry **entries;
      static char *attrs[] = { "1.1", 0 };

      /* Create the filter - this needs to be freed */
      filter = create_filter(attrName, value, requiredObjectClass);

#ifdef DEBUG
      slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
        "SEARCH filter=%s\n", filter);
#endif

      /* Perform the search using the new internal API */
      spb = slapi_pblock_new();
      if (!spb) { result = uid_op_error(2); break; }

      slapi_search_internal_set_pb(spb, baseDN, LDAP_SCOPE_SUBTREE,
      	filter, attrs, 0 /* attrs only */, NULL, NULL, plugin_identity, 0 /* actions */);
      slapi_search_internal_pb(spb);

      err = slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_RESULT, &sres);
      if (err) { result = uid_op_error(3); break; }
    
      /* Allow search to report that there is nothing in the subtree */
      if (sres == LDAP_NO_SUCH_OBJECT) break;

      /* Other errors are bad */
      if (sres) { result = uid_op_error(3); break; }

      err = slapi_pblock_get(spb, SLAPI_PLUGIN_INTOP_SEARCH_ENTRIES,
              &entries);
      if (err) { result = uid_op_error(4); break; }

      /*
       * Look at entries returned.  Any entry found must be the
       * target entry or the constraint fails.
       */
      for(;*entries;entries++)
      {
        char *ndn = slapi_entry_get_ndn(*entries); /* get the normalized dn */

#ifdef DEBUG
        slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
          "SEARCH entry dn=%s\n", ndn);
#endif

        /*
         * It is a Constraint Violation if any entry is found, unless
         * the entry is the target entry (if any).
         */
        if (!target || strcmp(ndn, target) != 0)
        {
          result = LDAP_CONSTRAINT_VIOLATION;
          break;
        }
      }

#ifdef DEBUG
      slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
        "SEARCH complete result=%d\n", result);
#endif
    END

    /* Clean-up */
    if (spb) {
	slapi_free_search_results_internal(spb);
	slapi_pblock_destroy(spb);
    }

    slapi_ch_free((void**)&filter);

  return result;
}

/* ------------------------------------------------------------ */
/*
 * searchAllSubtrees - search all subtrees in argv for entries
 *   with a named attribute matching the list of values, by
 *   calling search for each one.
 *
 * If 'attr' is NULL, the values are taken from 'values'.
 * If 'attr' is non-NULL, the values are taken from 'attr'.
 *
 * Return:
 *   LDAP_SUCCESS - no matches, or the attribute matches the
 *     target dn.
 *   LDAP_CONSTRAINT_VIOLATION - an entry was found that already
 *     contains the attribute value.
 *   LDAP_OPERATIONS_ERROR - a server failure.
 */
static int
searchAllSubtrees(int argc, char *argv[], const char *attrName,
  Slapi_Attr *attr, struct berval **values, const char *requiredObjectClass,
  const char *dn)
{
  int result = LDAP_SUCCESS;

  /*
   * For each DN in the managed list, do uniqueness checking if
   * the target DN is a subnode in the tree.
   */
  for(;argc > 0;argc--,argv++)
  {
    /*
     * The DN should already be normalized, so we don't have to
     * worry about that here.
     */
    if (slapi_dn_issuffix(dn, *argv)) {
      result = search(*argv, attrName, attr, values, requiredObjectClass, dn);
      if (result) break;
    }
  }
  return result;
}

/* ------------------------------------------------------------ */
/*
 * getArguments - parse invocation parameters
 * Return:
 *   0 - success
 *   >0 - error parsing parameters
 */
static int
getArguments(Slapi_PBlock *pb, char **attrName, char **markerObjectClass,
                         char **requiredObjectClass)
{
  int argc;
  char **argv;

  /*
   * Get the arguments
   */
  if (slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc))
  {
    return uid_op_error(10);
  }

  if (slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv))
  {
        return uid_op_error(11);
  }

  /*
   * Required arguments: attribute and markerObjectClass
   * Optional argument: requiredObjectClass
   */
  for(;argc > 0;argc--,argv++)
  {
        char *param = *argv;
        char *delimiter = strchr(param, '=');
        if (NULL == delimiter)
        {
          /* Old style untagged parameter */
          *attrName = *argv;
          return UNTAGGED_PARAMETER;
        }
        if (strncasecmp(param, "attribute", delimiter-param) == 0)
        {
          /* It's OK to set a pointer here, because ultimately it points
           * inside the argv array of the pblock, which will be staying
           * arround.
           */
          *attrName = delimiter+1;
        } else if (strncasecmp(param, "markerobjectclass", delimiter-param) == 0)
        {
          *markerObjectClass = delimiter+1;
        } else if (strncasecmp(param, "requiredobjectclass", delimiter-param) == 0)
        {
          *requiredObjectClass = delimiter+1;
        }
  }
  if (!*attrName || !*markerObjectClass)
  {
        return uid_op_error(13);
  }

  return 0;
}

/* ------------------------------------------------------------ */
/*
 * findSubtreeAndSearch - walk up the tree to find an entry with
 * the marker object class; if found, call search from there and
 * return the result it returns
 *
 * If 'attr' is NULL, the values are taken from 'values'.
 * If 'attr' is non-NULL, the values are taken from 'attr'.
 *
 * Return:
 *   LDAP_SUCCESS - no matches, or the attribute matches the
 *     target dn.
 *   LDAP_CONSTRAINT_VIOLATION - an entry was found that already
 *     contains the attribute value.
 *   LDAP_OPERATIONS_ERROR - a server failure.
 */
static int
findSubtreeAndSearch(char *parentDN, const char *attrName, Slapi_Attr *attr,
  struct berval **values, const char *requiredObjectClass, const char *target,
  const char *markerObjectClass)
{
  int result = LDAP_SUCCESS;
  Slapi_PBlock *spb = NULL;

  while (NULL != (parentDN = slapi_dn_parent(parentDN)))
  {
        if ((spb = dnHasObjectClass(parentDN, markerObjectClass)))
        {
          freePblock(spb);
          /*
           * Do the search.   There is no entry that is allowed
           * to have the attribute already.
           */
          result = search(parentDN, attrName, attr, values, requiredObjectClass,
                          target);
          break;
        }
  }
  return result;
}


/* ------------------------------------------------------------ */
/*
 * preop_add - pre-operation plug-in for add
 */
static int
preop_add(Slapi_PBlock *pb)
{
  int result;
  char *errtext = NULL;
  char *attrName = NULL;

#ifdef DEBUG
  slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name, "ADD begin\n");
#endif

  result = LDAP_SUCCESS;

  /*
   * Do constraint check on the added entry.  Set result.
   */

 BEGIN
    int err;
    char *markerObjectClass = NULL;
    char *requiredObjectClass = NULL;
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    int isupdatedn;
    Slapi_Entry *e;
    Slapi_Attr *attr;
    int argc;
    char **argv = NULL;

        /*
         * If this is a replication update, just be a noop.
         */
        err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &isupdatedn);
    if (err) { result = uid_op_error(50); break; }
        if (isupdatedn)
        {
          break;
        }

    /*
     * Get the arguments
     */
        result = getArguments(pb, &attrName, &markerObjectClass,
                                                  &requiredObjectClass);
        if (UNTAGGED_PARAMETER == result)
        {
          slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name, 
                          "ADD parameter untagged: %s\n", attrName);
          result = LDAP_SUCCESS;
          /* Statically defined subtrees to monitor */
          err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
          if (err) { result = uid_op_error(53); break; }

          err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
          if (err) { result = uid_op_error(54); break; }
          argc--; argv++; /* First argument was attribute name */
        } else if (0 != result)
        {
          break;
        }

    /*
     * Get the target DN for this add operation
     */
    err = slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn);
    if (err) { result = uid_op_error(51); break; }

    dn = slapi_sdn_get_dn(sdn);

#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name, "ADD target=%s\n", dn);
#endif

       /*
         * Get the entry data for this add. Check whether it
         * contains a value for the unique attribute
         */
        err = slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
        if (err) { result = uid_op_error(52); break; }

        err = slapi_entry_attr_find(e, attrName, &attr);
        if (err) break;  /* no unique attribute */

        /*
         * Check if it contains the required object class
         */
        if (NULL != requiredObjectClass)
        {
          if (!entryHasObjectClass(pb, e, requiredObjectClass))
          {
                /* No, so we don't have to do anything */
                break;
          }
        }

        /*
         * Passed all the requirements - this is an operation we
         * need to enforce uniqueness on. Now find all parent entries
         * with the marker object class, and do a search for each one.
         */
        if (NULL != markerObjectClass)
        {
          /* Subtree defined by location of marker object class */
                result = findSubtreeAndSearch((char *)dn, attrName, attr, NULL,
                                              requiredObjectClass, dn,
                                              markerObjectClass);
        } else
        {
          /* Subtrees listed on invocation line */
          result = searchAllSubtrees(argc, argv, attrName, attr, NULL,
                                     requiredObjectClass, dn);
        }
  END

  if (result)
  {
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "ADD result %d\n", result);

    if (result == LDAP_CONSTRAINT_VIOLATION) {
      errtext = slapi_ch_smprintf(moreInfo, attrName);
    } else {
      errtext = slapi_ch_strdup("Error checking for attribute uniqueness.");
    }

    /* Send failure to the client */
    slapi_send_ldap_result(pb, result, 0, errtext, 0, 0);

    slapi_ch_free_string(&errtext);
  }

  return (result==LDAP_SUCCESS)?0:-1;
}

static void
addMod(LDAPMod ***modary, int *capacity, int *nmods, LDAPMod *toadd)
{
  if (*nmods == *capacity) {
    *capacity += 4;
    if (*modary) {
      *modary = (LDAPMod **)slapi_ch_realloc((char *)*modary, *capacity * sizeof(LDAPMod *));
    } else {
      *modary = (LDAPMod **)slapi_ch_malloc(*capacity * sizeof(LDAPMod *));
    }
  }
  (*modary)[*nmods] = toadd;
  (*nmods)++;
}

/* ------------------------------------------------------------ */
/*
 * preop_modify - pre-operation plug-in for modify
 */
static int
preop_modify(Slapi_PBlock *pb)
{

  int result = LDAP_SUCCESS;
  Slapi_PBlock *spb = NULL;
  LDAPMod **checkmods = NULL;
  int checkmodsCapacity = 0;
  char *errtext = NULL;
  char *attrName = NULL;

#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "MODIFY begin\n");
#endif

  BEGIN
    int err;
    char *markerObjectClass=NULL;
    char *requiredObjectClass=NULL;
    LDAPMod **mods;
    int modcount = 0;
    int ii;
    LDAPMod *mod;
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    int isupdatedn;
    int argc;
    char **argv = NULL;

    /*
     * If this is a replication update, just be a noop.
     */
    err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &isupdatedn);
    if (err) { result = uid_op_error(60); break; }
    if (isupdatedn)
    {
      break;
    }

    /*
     * Get the arguments
     */
        result = getArguments(pb, &attrName, &markerObjectClass,
                                                  &requiredObjectClass);
        if (UNTAGGED_PARAMETER == result)
        {
          result = LDAP_SUCCESS;
          /* Statically defined subtrees to monitor */
          err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
          if (err) { result = uid_op_error(53); break; }

          err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
          if (err) { result = uid_op_error(54); break; }
          argc--; /* First argument was attribute name */
          argv++;
        } else if (0 != result)
        {
          break;
        }

    err = slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &mods);
    if (err) { result = uid_op_error(61); break; }

    /* There may be more than one mod that matches e.g.
       changetype: modify
       delete: uid
       uid: balster1950
       -
       add: uid
       uid: scottg
       
       So, we need to first find all mods that contain the attribute
       which are add or replace ops and are bvalue encoded
    */
    /* find out how many mods meet this criteria */
    for(;*mods;mods++)
    {
        mod = *mods;
        if ((slapi_attr_type_cmp(mod->mod_type, attrName, 1) == 0) && /* mod contains target attr */
            (mod->mod_op & LDAP_MOD_BVALUES) && /* mod is bval encoded (not string val) */
            (mod->mod_bvalues && mod->mod_bvalues[0]) && /* mod actually contains some values */
            (SLAPI_IS_MOD_ADD(mod->mod_op) || /* mod is add */
             SLAPI_IS_MOD_REPLACE(mod->mod_op))) /* mod is replace */
        {
          addMod(&checkmods, &checkmodsCapacity, &modcount, mod);
        }
    }
    if (modcount == 0) {
        break; /* no mods to check, we are done */
    }

    /* Get the target DN */
    err = slapi_pblock_get(pb, SLAPI_MODIFY_TARGET_SDN, &sdn);
    if (err) { result = uid_op_error(11); break; }

    dn = slapi_sdn_get_dn(sdn);
    /*
     * Check if it has the required object class
     */
    if (requiredObjectClass &&
        !(spb = dnHasObjectClass(dn, requiredObjectClass))) {
        break;
    }

    /*
     * Passed all the requirements - this is an operation we
     * need to enforce uniqueness on. Now find all parent entries
     * with the marker object class, and do a search for each one.
     */
    /*
     * stop checking at first mod that fails the check
     */
    for (ii = 0; (result == 0) && (ii < modcount); ++ii)
    {
        mod = checkmods[ii];
        if (NULL != markerObjectClass)
        {
            /* Subtree defined by location of marker object class */
            result = findSubtreeAndSearch((char *)dn, attrName, NULL, 
                                          mod->mod_bvalues, requiredObjectClass,
                                          dn, markerObjectClass);
        } else
        {
            /* Subtrees listed on invocation line */
            result = searchAllSubtrees(argc, argv, attrName, NULL,
                                       mod->mod_bvalues, requiredObjectClass, dn);
        }
    }
  END

  slapi_ch_free((void **)&checkmods);
  freePblock(spb);
 if (result)
  {
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "MODIFY result %d\n", result);

    if (result == LDAP_CONSTRAINT_VIOLATION) {
      errtext = slapi_ch_smprintf(moreInfo, attrName);
    } else {
      errtext = slapi_ch_strdup("Error checking for attribute uniqueness.");
    }

    slapi_send_ldap_result(pb, result, 0, errtext, 0, 0);

    slapi_ch_free_string(&errtext);
  }

  return (result==LDAP_SUCCESS)?0:-1;

}

/* ------------------------------------------------------------ */
/*
 * preop_modrdn - Pre-operation call for modify RDN
 *
 * Check that the new RDN does not include attributes that
 * cause a constraint violation
 */
static int
preop_modrdn(Slapi_PBlock *pb)
{
  int result = LDAP_SUCCESS;
  Slapi_Entry *e = NULL;
  Slapi_Value *sv_requiredObjectClass = NULL;
  char *errtext = NULL;
  char *attrName = NULL;

#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "MODRDN begin\n");
#endif

  BEGIN
    int err;
    char *markerObjectClass=NULL;
    char *requiredObjectClass=NULL;
    const char *dn = NULL;
    Slapi_DN *sdn = NULL;
    Slapi_DN *superior;
    char *rdn;
    int deloldrdn = 0;
    int isupdatedn;
    Slapi_Attr *attr;
    int argc;
    char **argv = NULL;

        /*
         * If this is a replication update, just be a noop.
         */
        err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &isupdatedn);
    if (err) { result = uid_op_error(30); break; }
        if (isupdatedn)
        {
          break;
        }

    /*
     * Get the arguments
     */
        result = getArguments(pb, &attrName, &markerObjectClass,
                                                  &requiredObjectClass);
        if (UNTAGGED_PARAMETER == result)
        {
          result = LDAP_SUCCESS;
          /* Statically defined subtrees to monitor */
          err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
          if (err) { result = uid_op_error(53); break; }

          err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
          if (err) { result = uid_op_error(54); break; }
          argc--; /* First argument was attribute name */
          argv++; 
        } else if (0 != result)
        {
          break;
        }

    /* Create a Slapi_Value for the requiredObjectClass to use
     * for checking the entry. */
    if (requiredObjectClass) {
        sv_requiredObjectClass = slapi_value_new_string(requiredObjectClass);
    }

    /* Get the DN of the entry being renamed */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &sdn);
    if (err) { result = uid_op_error(31); break; }

    dn = slapi_sdn_get_dn(sdn);

    /* Get superior value - unimplemented in 3.0/4.0/5.0 DS */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &superior);
    if (err) { result = uid_op_error(32); break; }

    /*
     * No superior means the entry is just renamed at
     * its current level in the tree.  Use the target DN for
     * determining which managed tree this belongs to
     */
    if (!superior) superior = sdn;

    /* Get the new RDN - this has the attribute values */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &rdn);
    if (err) { result = uid_op_error(33); break; }
#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "MODRDN newrdn=%s\n", rdn);
#endif

    /* See if the old RDN value is being deleted. */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_DELOLDRDN, &deloldrdn);
    if (err) { result = uid_op_error(34); break; }

    /* Get the entry that is being renamed so we can make a dummy copy
     * of what it will look like after the rename. */
    err = slapi_search_internal_get_entry(sdn, NULL, &e, plugin_identity);
    if (err != LDAP_SUCCESS) {
        result = uid_op_error(35);
        /* We want to return a no such object error if the target doesn't exist. */
        if (err == LDAP_NO_SUCH_OBJECT) {
            result = err;
        }
        break;
    }

    /* Apply the rename operation to the dummy entry. */
    err = slapi_entry_rename(e, rdn, deloldrdn, slapi_sdn_get_dn(superior));
    if (err != LDAP_SUCCESS) { result = uid_op_error(36); break; }

        /*
         * Find any unique attribute data in the new RDN
         */
        err = slapi_entry_attr_find(e, attrName, &attr);
        if (err) break;  /* no UID attribute */

        /*
         * Check if it has the required object class
         */
        if (requiredObjectClass &&
            !slapi_entry_attr_has_syntax_value(e, SLAPI_ATTR_OBJECTCLASS, sv_requiredObjectClass)) { break; }

        /*
         * Passed all the requirements - this is an operation we
         * need to enforce uniqueness on. Now find all parent entries
         * with the marker object class, and do a search for each one.
         */
        if (NULL != markerObjectClass)
        {
          /* Subtree defined by location of marker object class */
                result = findSubtreeAndSearch(slapi_entry_get_dn(e), attrName, attr, NULL,
                                              requiredObjectClass, dn,
                                              markerObjectClass);
        } else
        {
          /* Subtrees listed on invocation line */
          result = searchAllSubtrees(argc, argv, attrName, attr, NULL,
                                     requiredObjectClass, dn);
        }
  END
  /* Clean-up */
  slapi_value_free(&sv_requiredObjectClass);
  if (e) slapi_entry_free(e);

  if (result)
  {
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "MODRDN result %d\n", result);

    if (result == LDAP_CONSTRAINT_VIOLATION) {
      errtext = slapi_ch_smprintf(moreInfo, attrName);
    } else {
      errtext = slapi_ch_strdup("Error checking for attribute uniqueness.");
    }

    slapi_send_ldap_result(pb, result, 0, errtext, 0, 0);

    slapi_ch_free_string(&errtext);
  }

  return (result==LDAP_SUCCESS)?0:-1;

}

/* ------------------------------------------------------------ */
/*
 * Initialize the plugin
 *
 * uidunique_init (the old name) is deprecated
 */
int
NSUniqueAttr_Init(Slapi_PBlock *pb)
{
  int err = 0;

  BEGIN
    int argc;
    char **argv;

    /* Declare plugin version */
    err = slapi_pblock_set(pb, SLAPI_PLUGIN_VERSION,
            SLAPI_PLUGIN_VERSION_01);
    if (err) break;

    /*
     * Get plugin identity and store it for later use
     * Used for internal operations
     */

    slapi_pblock_get (pb, SLAPI_PLUGIN_IDENTITY, &plugin_identity);
    /* PR_ASSERT (plugin_identity); */

    /*
     * Get and normalize arguments
     */
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
    if (err) break;
 
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
    if (err) break;

    /* First argument is the unique attribute name */
    if (argc < 1) { err = -1; break; }
    argv++; argc--;

    for(;argc > 0;argc--, argv++) {
        char *normdn = slapi_create_dn_string_case("%s", *argv);
        slapi_ch_free_string(argv);
        *argv = normdn;
    }

    /* Provide descriptive information */
    err = slapi_pblock_set(pb, SLAPI_PLUGIN_DESCRIPTION,
            (void*)&pluginDesc);
    if (err) break;

    /* Register functions */
    err = slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_ADD_FN,
            (void*)preop_add);
    if (err) break;

    err = slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_MODIFY_FN,
            (void*)preop_modify);
    if (err) break;

    err = slapi_pblock_set(pb, SLAPI_PLUGIN_PRE_MODRDN_FN,
            (void*)preop_modrdn);
    if (err) break;

  END

  if (err) {
    slapi_log_error(SLAPI_LOG_PLUGIN, "NSUniqueAttr_Init",
             "Error: %d\n", err);
    err = -1;
  }
  else
    slapi_log_error(SLAPI_LOG_PLUGIN, "NSUniqueAttr_Init",
             "plugin loaded\n");

  return err;
}

int
uidunique_init(Slapi_PBlock *pb)
{
  return NSUniqueAttr_Init(pb);
}


/* ------------------------------------------------------------ */
/*
 * ldap_quote_filter_value
 *
 * Quote the filter value according to RFC 2254 (Dec 1997)
 *
 * value - a UTF8 string containing the value.  It may contain
 *   any of the chars needing quotes ( '(' ')' '*' '/' and NUL ).
 * len - the length of the UTF8 value
 * out - a buffer to recieve the converted value.  May be NULL, in
 *   which case, only the length of the output is computed (and placed in
 *   outLen).
 * maxLen - the size of the output buffer.  It is an error if this length
 *   is exceeded.  Ignored if out is NULL.
 * outLen - recieves the size of the output.  If an error occurs, this
 *   result is not available.
 *
 * Returns
 *   0 - success
 *  -1 - failure (usually a buffer overflow)
 */
int /* Error value */
ldap_quote_filter_value(
  char *value, int len,
  char *out, int maxLen,
  int *outLen)
{
  int err;
  char *eValue;
  int resLen;
#ifdef SLAPI_SUPPORTS_V3_ESCAPING
  static char hexchars[16] = "0123456789abcdef";
#endif

  err = 0;
  eValue = &value[len];
  resLen = 0;

  /*
   * Convert each character in the input string
   */
  while(value < eValue)
  {
    switch(*value)
    {
    case '(':
    case ')':
    case '*':
    case '\\':
#ifdef SLAPI_SUPPORTS_V3_ESCAPING
    case 0:
#endif
      /* Handle characters needing special escape sequences */

      /* Compute size of output */
#ifdef SLAPI_SUPPORTS_V3_ESCAPING
      resLen += 3;
#else
      resLen += 2;
#endif

      /* Generate output if requested */
      if (out)
      {
        /* Check for overflow */
        if (resLen > maxLen) { err = -1; break; }

        *out++ = '\\';
#ifdef SLAPI_SUPPORTS_V3_ESCAPING
        *out++ = hexchars[(*value >> 4) & 0xF];
        *out++ = hexchars[*value & 0xF];
#else
        *out++ = *value;
#endif
      }

      break;

    default:
      /* Compute size of output */
      resLen += 1;
      
      /* Generate output if requested */
      if (out)
      {
        if (resLen > maxLen) { err = -1; break; }
        *out++ = *value;
      }

      break;
    }

    if (err) break;

    value++;
  }

  if (!err) *outLen = resLen;

  return err;
}
