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
 * 7bit.c
 *
 * Implements a directory server pre-operation plugin to test
 * attributes for 7 bit clean within a defined subtree in the
 * directory.
 *
 */
#include <stdio.h>
#include <slapi-plugin.h>
#include <string.h>

/* DBDB this should be pulled from a common header file */
#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp(x,y) strcmpi(x,y)
#endif
#endif

#if defined( LDAP_DEBUG ) && !defined( DEBUG )
#define DEBUG
#endif

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
static char *plugin_name = "NS7bitAttr";
static Slapi_PluginDesc
pluginDesc = { "NS7bitAttr", VENDOR, DS_PACKAGE_VERSION,
  "Enforce  7-bit clean attribute values" };


/*
 * More information about constraint failure
 */
static char *moreInfo =
  "The value is not 7-bit clean: ";

/* ------------------------------------------------------------ */
/*
 * op_error - Record (and report) an operational error.
 */
static int
op_error(int internal_error)
{
  slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
    "Internal error: %d\n", internal_error);

  return LDAP_OPERATIONS_ERROR;
}

static void
issue_error(Slapi_PBlock *pb, int result, char *type, char *value)
{
  char *moreinfop;

  slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "%s result %d\n", type, result);

  if (value == NULL) {
    value = "unknown";
  }
  moreinfop = slapi_ch_smprintf("%s%s", moreInfo, value);

  /* Send failure to the client */
  slapi_send_ldap_result(pb, result, 0, moreinfop, 0, 0);
  slapi_ch_free((void **)&moreinfop);

  return;
}


/*
 * Check 'value' for 7-bit cleanliness.
 */
static int
bit_check_one_berval(const struct berval *value, char **violated)
{
  int result;
  char *ch;
  int i;

#ifdef DEBUG
  slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name, "7-bit checking begin\n");
#endif

  result = LDAP_SUCCESS;
  /* If no value, can't possibly be a conflict */
  if ( (struct berval *)NULL == value )
    return result;

  for(i=0, ch=value->bv_val; ch && i < (int)(value->bv_len) ;
        ch++, i++)
  {

    if (( 0x80 & *ch ) != 0 ) 
    {
      result = LDAP_CONSTRAINT_VIOLATION;
      *violated = value->bv_val;
      break;
    }
  }

  return result;
}


/*
 * Check a set of values for 7-bit cleanliness.
 *
 * If 'attr' is NULL, the values are taken from 'values'.
 * If 'attr' is non-NULL, the values are taken from 'attr'.
 */
static int
bit_check(Slapi_Attr *attr, struct berval **values, char **violated)
{
  int result = LDAP_SUCCESS;
  *violated = NULL;

  /* If no values, can't possibly be a conflict */
  if ( (Slapi_Attr *)NULL == attr && (struct berval **)NULL == values )
	return result;

  if ( (Slapi_Attr *)NULL != attr )
  {
	Slapi_Value	*v = NULL;
	int	vhint = -1;

	for ( vhint = slapi_attr_first_value( attr, &v );
		vhint != -1 && LDAP_SUCCESS == result;
		vhint = slapi_attr_next_value( attr, vhint, &v ))
	{
	  result = bit_check_one_berval(slapi_value_get_berval(v), violated);
	}
  }
  else
  {
	for (;*values != NULL && LDAP_SUCCESS == result; values++)
	{
	  result = bit_check_one_berval(*values, violated);
	}
  }

#ifdef DEBUG
  slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
    "7 bit check result = %d\n", result);
#endif

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
  char *violated = NULL;

#ifdef DEBUG
  slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name, "ADD begin\n");
#endif

  result = LDAP_SUCCESS;

  /*
   * Do constraint check on the added entry.  Set result.
   */
  BEGIN
    int err;
    int argc;
    char **argv;
    char **attrName;
    const char *dn;
    Slapi_DN *sdn = NULL;
    Slapi_Entry *e;
    Slapi_Attr *attr;
    char **firstSubtree;
    char **subtreeDN;
    int subtreeCnt;
    int is_replicated_operation;

    /*
     * Get the arguments
     */
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
    if (err) { result = op_error(53); break; }

    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
    if (err) { result = op_error(54); break; }

    /*
     * If this is a replication update, just be a noop.
     */
    err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
    if (err) { result = op_error(56); break; }
    if (is_replicated_operation)
    {
      break;
    }

    /*
     * Get the target DN for this add operation
     */
    err = slapi_pblock_get(pb, SLAPI_ADD_TARGET_SDN, &sdn);
    if (err) { result = op_error(50); break; }

    dn = slapi_sdn_get_dn(sdn);

#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name, "ADD target=%s\n", dn);
#endif

    /*
     * Get the entry data for this add. Check whether it
     * contains a value for the unique attribute
     */
    err = slapi_pblock_get(pb, SLAPI_ADD_ENTRY, &e);
    if (err) { result = op_error(51); break; }

    for ( firstSubtree = argv; strcmp(*firstSubtree, ",") != 0; 
          firstSubtree++, argc--) {}
    firstSubtree++;
    argc--;

    for (attrName = argv; strcmp(*attrName, ",") != 0; attrName++ )
    {
      /* 
       * if the attribute is userpassword, check unhashed#user#password 
       * instead.  "userpassword" is encoded; it will always pass the 7bit 
       * check.
       */
      char *attr_name; 
      if ( strcasecmp(*attrName, "userpassword") == 0 )
      {
         attr_name = "unhashed#user#password";
      } else {
         attr_name = *attrName;
      }
      err = slapi_entry_attr_find(e, attr_name, &attr);
      if (err) continue; /* break;*/  /* no 7-bit attribute */

      /*
       * For each DN in the managed list, do 7-bit checking if
       * the target DN is a subnode in the tree.
       */
      for( subtreeDN=firstSubtree, subtreeCnt=argc ;subtreeCnt > 0;
           subtreeCnt--,subtreeDN++)
      {
        /*
         * issuffix determines whether the target is under the
         * subtree *subtreeDN
         */
        if (slapi_dn_issuffix(dn, *subtreeDN)) 
        {
#ifdef DEBUG
          slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
            "ADD subtree=%s\n", *subtreeDN);
#endif
  
          /*
           * Check if the value is 7-bit clean
           */
          result = bit_check(attr, NULL, &violated);
          if (result) break;
        }
      }
      /* don't have to go on if there is a value not 7-bit clean */
      if (result) break;
    }
  END

  if (result) {
    issue_error(pb, result, "ADD", violated);
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
  int result;
  char *violated = NULL;
  LDAPMod **checkmods = NULL; /* holds mods to check */
  int checkmodsCapacity = 0; /* max capacity of checkmods */

#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "MODIFY begin\n");
#endif

  result = LDAP_SUCCESS;

  BEGIN
    int err;
    int argc;
    char **argv;
    char **attrName;
    LDAPMod **mods;
    LDAPMod **firstMods;
    LDAPMod *mod;
    const char *target;
	Slapi_DN *target_sdn = NULL;
    char **firstSubtree;
    char **subtreeDN;
    int subtreeCnt;
    int is_replicated_operation;

    /*
     * Get the arguments
     */
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
    if (err) { result = op_error(13); break; }
 
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
    if (err) { result = op_error(14); break; }

    /*
     * If this is a replication update, just be a noop.
     */
    err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
    if (err) { result = op_error(16); break; }
    if (is_replicated_operation)
    {
      break;
    }

    err = slapi_pblock_get(pb, SLAPI_MODIFY_MODS, &firstMods);
    if (err) { result = op_error(10); break; }

    /* Get the target DN */
    err = slapi_pblock_get(pb, SLAPI_MODIFY_TARGET_SDN, &target_sdn);
    if (err) { result = op_error(11); break; }

    target = slapi_sdn_get_dn(target_sdn);
    /*
     * Look for managed trees that include the target
     * Arguments before "," are the 7-bit clean attribute names.  Arguemnts
     * after "," are subtreeDN's.
     */
    for ( firstSubtree = argv; strcmp(*firstSubtree, ",") != 0;
        firstSubtree++, argc--) {}
    firstSubtree++;
    argc--;

    for (attrName = argv; strcmp(*attrName, ",") != 0; attrName++ )
    {
      int modcount = 0;
      int ii = 0;

      /* 
       * if the attribute is userpassword, check unhashed#user#password 
       * instead.  "userpassword" is encoded; it will always pass the 7bit 
       * check.
       */
      char *attr_name; 
      if ( strcasecmp(*attrName, "userpassword") == 0 )
      {
         attr_name = "unhashed#user#password";
      } else {
         attr_name = *attrName;
      }

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
      for(mods=firstMods;*mods;mods++)
      {
	mod = *mods;
	if ((slapi_attr_type_cmp(mod->mod_type, attr_name, 1) == 0) && /* mod contains target attr */
	    (mod->mod_op & LDAP_MOD_BVALUES) && /* mod is bval encoded (not string val) */
	    (mod->mod_bvalues && mod->mod_bvalues[0]) && /* mod actually contains some values */
	    (SLAPI_IS_MOD_ADD(mod->mod_op) || /* mod is add */
	     SLAPI_IS_MOD_REPLACE(mod->mod_op))) /* mod is replace */
	{
	  addMod(&checkmods, &checkmodsCapacity, &modcount, mod);
	}
      }
      if (modcount == 0) {
	continue; /* no mods to check, go to next attr */
      }
  
      /*
       * stop checking at first mod that fails the check
       */
      for (ii = 0; (result == 0) && (ii < modcount); ++ii)
      {
	mod = checkmods[ii];
	/*
	 * For each DN in the managed list, do 7-bit checking if
	 * the target DN is a subnode in the tree.
	 */
	for( subtreeDN=firstSubtree, subtreeCnt=argc ;subtreeCnt > 0;
	     subtreeCnt--,subtreeDN++)
	{
	  /*
	   * issuffix determines whether the target is under the
	   * subtree *subtreeDN
	   */
	    if (slapi_dn_issuffix(target, *subtreeDN))
	    {
#ifdef DEBUG
		slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
				"MODIFY subtree=%s\n", *subtreeDN);
#endif
		/*
		 * Check if the value is 7-bit clean
		 */
		result = bit_check(NULL, mod->mod_bvalues, &violated);
		if (result) break;
	    }
	}
      }
      /* don't have to go on if there is a value not 7-bit clean */
      if (result) break;
    }   
  END

  slapi_ch_free((void **)&checkmods);
  if (result) {
    issue_error(pb, result, "MODIFY", violated);
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
  int result;
  Slapi_Entry *e;
  char *violated = NULL;

#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "MODRDN begin\n");
#endif

  /* Init */
  result = LDAP_SUCCESS;
  e = 0;

  BEGIN
    int err;
    int argc;
    char **argv;
    char **attrName;
    Slapi_DN *target_sdn = NULL;
    Slapi_DN *superior;
    char *rdn; 
    Slapi_Attr *attr;
    char **firstSubtree;
    char **subtreeDN;
    int subtreeCnt;
    int is_replicated_operation;

    /*
     * Get the arguments
     */
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
    if (err) { result = op_error(30); break; }
 
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
    if (err) { result = op_error(31); break; }

    /*
     * If this is a replication update, just be a noop.
     */
    err = slapi_pblock_get(pb, SLAPI_IS_REPLICATED_OPERATION, &is_replicated_operation);
    if (err) { result = op_error(16); break; }
    if (is_replicated_operation)
    {
      break;
    }

    /* Get the DN of the entry being renamed */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_TARGET_SDN, &target_sdn);
    if (err) { result = op_error(22); break; }

    /* Get superior value - unimplemented in 3.0 DS */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_NEWSUPERIOR_SDN, &superior);
    if (err) { result = op_error(20); break; }

    /*
     * No superior means the entry is just renamed at
     * its current level in the tree.  Use the target DN for
     * determining which managed tree this belongs to
     */
    if (!superior) superior = target_sdn;

    /* Get the new RDN - this has the attribute values */
    err = slapi_pblock_get(pb, SLAPI_MODRDN_NEWRDN, &rdn);
    if (err) { result = op_error(33); break; }

#ifdef DEBUG
    slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
      "MODRDN newrdn=%s\n", rdn);
#endif

    /*
     * Parse the RDN into attributes by creating a "dummy" entry
     * and setting the attributes from the RDN.
     *
     * The new entry must be freed.
     */
    e = slapi_entry_alloc();
    if (!e) { result = op_error(32); break; }

    /* NOTE: strdup on the rdn, since it will be freed when
     * the entry is freed */

    slapi_entry_set_dn(e, slapi_ch_strdup(rdn));

    err = slapi_entry_add_rdn_values(e);
    if (err)
    {
      slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
        "MODRDN bad rdn value=%s\n", rdn);
      break; /* Bad DN */
    }

    /*
     * arguments before "," are the 7-bit clean attribute names.  Arguemnts
     * after "," are subtreeDN's.
     */
    for ( firstSubtree = argv; strcmp(*firstSubtree, ",") != 0;
        firstSubtree++, argc--) {}
    firstSubtree++;
    argc--;

    /*
     * Find out if the node is being moved into one of
     * the managed subtrees
     */
    for (attrName = argv; strcmp(*attrName, ",") != 0; attrName++ )
    {
      /* 
       * If the attribut type is userpassword, do not replace it by 
       * unhashed#user#password because unhashed#user#password does not exist  
       * in this case.
       */
      /*   
       * Find any 7-bit attribute data in the new RDN
       */
      err = slapi_entry_attr_find(e, *attrName, &attr);
      if (err) continue; /* break;*/  /* no 7-bit attribute */

      /*
       * For each DN in the managed list, do 7-bit checking if
       * the target DN is a subnode in the tree.
       */
      for( subtreeDN=firstSubtree, subtreeCnt=argc ;subtreeCnt > 0;
        subtreeCnt--,subtreeDN++)
      {
        /*
         * issuffix determines whether the target is under the
         * subtree *subtreeDN
         */
        if (slapi_dn_issuffix(slapi_sdn_get_dn(superior), *subtreeDN))
        {
#ifdef DEBUG
          slapi_log_error(SLAPI_LOG_PLUGIN, plugin_name,
            "MODRDN subtree=%s\n", *subtreeDN);
#endif

          /*
           * Check if the value is 7-bit clean
           */
          result = bit_check(attr, NULL, &violated);
          if (result) break;
        }
      }
      /* don't have to go on if there is a value not 7-bit clean */
      if (result) break;
    }
  END

  /* Clean-up */
  if (e) slapi_entry_free(e);

  if (result) {
    issue_error(pb, result, "MODRDN", violated);
  }

  return (result==LDAP_SUCCESS)?0:-1;
}

/* ------------------------------------------------------------ */
/*
 * Initialize the plugin
 *
 */
int
NS7bitAttr_Init(Slapi_PBlock *pb)
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
     * Get and normalize arguments
     */
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGC, &argc);
    if (err) break;
 
    err = slapi_pblock_get(pb, SLAPI_PLUGIN_ARGV, &argv);
    if (err) break;

    /* 
     * Arguments before "," are the 7-bit attribute names. Arguments after
     * "," are the subtree DN's. 
     */
    if (argc < 1) { err = -1; break; }
    for(;strcmp(*argv, ",") != 0 && argc > 0; argc--, argv++)
      {};
    if (argc == 0) { err = -1; break; }
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
    slapi_log_error(SLAPI_LOG_PLUGIN, "NS7bitAttr_Init",
             "Error: %d\n", err);
    err = -1;
  }
  else
    slapi_log_error(SLAPI_LOG_PLUGIN, "NS7bitAttr_Init",
             "plugin loaded\n");

  return err;
}

