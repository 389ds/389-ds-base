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
 repl5_total.c - code that implements a total replica update.

 The requestValue of the NSDS50ReplicationEntry looks like this:

     requestValue ::= SEQUENCE { 
         uniqueid OCTET STRING, 
         dn LDAPDN, 
         annotatedAttributes AnnotatedAttributeList 
     } 

     AnnotatedAttributeList ::= SET OF SEQUENCE { 
         attributeType AttributeDescription, 
         attributeDeletionCSN OCTET STRING OPTIONAL,
         attributeDeleted BOOLEAN DEFAULT FALSE, 
         annotatedValues SET OF AnnotatedValue 
     } 

     AnnotatedValue ::= SEQUENCE { 
         value AttributeValue, 
         valueDeleted BOOLEAN DEFAULT FALSE, 
         valueCSNSet SEQUENCE OF ValueCSN, 
     } 

    ValueCSN ::= SEQUENCE {
        CSNType ENUMERATED { 
               valuePresenceCSN           (1),
               valueDeletionCSN           (2), 
               valueDistinguishedCSN      (3) 
          } 
        CSN OCTET STRING,
    }
*/

#include "repl.h"
#include "repl5.h"

#define CSN_TYPE_VALUE_UPDATED_ON_WIRE 1
#define CSN_TYPE_VALUE_DELETED_ON_WIRE 2
#define CSN_TYPE_VALUE_DISTINGUISHED_ON_WIRE 3

/* #define GORDONS_PATENTED_BER_DEBUG 1 */
#ifdef GORDONS_PATENTED_BER_DEBUG
#define BER_DEBUG(a) printf(a)
#else
#define BER_DEBUG(a)
#endif

/* Forward declarations */
static int my_ber_printf_csn(BerElement *ber, const CSN *csn, const CSNType t);
static int my_ber_printf_value(BerElement *ber, const char *type,
		                       const Slapi_Value *value, PRBool deleted);
static int my_ber_printf_attr (BerElement *ber, Slapi_Attr *attr, PRBool deleted);
static int my_ber_scanf_attr (BerElement *ber, Slapi_Attr **attr, PRBool *deleted);
static int my_ber_scanf_value(BerElement *ber, Slapi_Value **value, PRBool *deleted);

/*
 * Get a Slapi_Entry ready to send over the wire as part of
 * a total update protocol stream. Convert the entry and all
 * of its state information to a BerElement which will be the
 * payload of an extended LDAP operation.
 *
 * Entries consist of:
 * - An entry DN
 * - A uniqueID
 * - A set of present attributes, each of which consists of:
 *   - A set of present values, each of which consists of:
 *     - A value
 *     - A set of CSNs
 *   - A set of deleted values, each of which consists of:
 *     - A value
 *     - A set of CSNs
 * - A set of deleted attibutes, each of which consists of:
 *   - An attribute type
 *   - A set of CSNs. Note that this list of CSNs will always contain exactly one CSN.
 * 
 * This all gets mashed into one BerElement, ready to be blasted over the wire to
 * a replica.
 *
 */
BerElement *
entry2bere(const Slapi_Entry *e, char **excluded_attrs)
{
	BerElement *ber = NULL;
	const char *str = NULL;
	const char *dnstr = NULL;
    char *type;
	Slapi_DN *sdn = NULL;
	Slapi_Attr *attr = NULL, *prev_attr;
    int rc;

	PR_ASSERT(NULL != e);

	if ((ber = ber_alloc()) == NULL)
	{
		goto loser;
	}
	BER_DEBUG("{");
	if (ber_printf(ber, "{") == -1) /* Begin outer sequence */
	{
		goto loser;
	}

	/* Get the entry's uniqueid */
	if ((str = slapi_entry_get_uniqueid(e)) == NULL)
	{
		goto loser;
	}
	BER_DEBUG("s(uniqueid)");
	if (ber_printf(ber, "s", str) == -1)
	{
		goto loser;
	}

	/* Get the entry's DN */
	if ((sdn = slapi_entry_get_sdn((Slapi_Entry *)e)) == NULL) /* XXXggood had to cast away const */
	{
		goto loser;
	}
	if ((dnstr = slapi_sdn_get_dn(sdn)) == NULL)
	{
		goto loser;
	}
	BER_DEBUG("s(dn)");
	if (ber_printf(ber, "s", dnstr) == -1)
	{
		goto loser;
	}

	/* Next comes the annoted list of the entry's attributes */
	BER_DEBUG("[");
	if (ber_printf(ber, "[") == -1) /* Begin set of attributes */
	{
		goto loser;
	}
	/*
	 * We iterate over all of the non-deleted attributes first.
	 */ 
	slapi_entry_first_attr(e, &attr);
	while (NULL != attr)
	{
        /* ONREPL - skip uniqueid attribute since we already sent uniqueid 
           This is a hack; need to figure a better way of storing uniqueid
           in an entry */
        slapi_attr_get_type (attr, &type);
        if (strcasecmp (type, SLAPI_ATTR_UNIQUEID) != 0)
        {
			/* Check to see if this attribute is excluded by the fractional list */
			if ( (NULL == excluded_attrs) || !charray_inlist(excluded_attrs,type)) 
			{
				/* Process this attribute */
				rc = my_ber_printf_attr (ber, attr, PR_FALSE);
				if (rc != 0)
				{
					goto loser;
				}
			}
        }
		
		prev_attr = attr;
		slapi_entry_next_attr(e, prev_attr, &attr);
	}

	/*
	 * Now iterate over the deleted attributes.
	 */ 
	entry_first_deleted_attribute(e, &attr);
	while (attr != NULL)
	{
		slapi_attr_get_type (attr, &type);
		/* Check to see if this attribute is excluded by the fractional list */
		if ( (NULL == excluded_attrs) || !charray_inlist(excluded_attrs,type)) 
		{
			/* Process this attribute */
			rc = my_ber_printf_attr (ber, attr, PR_TRUE);
			if (rc != 0)
			{
				goto loser;
			}
		}
		entry_next_deleted_attribute(e, &attr);
	}
	BER_DEBUG("]");
	if (ber_printf(ber, "]") == -1) /* End set for attributes */
	{
		goto loser;
	}
	BER_DEBUG("}");
	if (ber_printf(ber, "}") == -1) /* End sequence for this entry */
	{
		goto loser;
	}
	
	/* If we get here, everything went ok */
	BER_DEBUG("\n");
	goto free_and_return;
loser:
	if (NULL != ber)
	{
		ber_free(ber, 1);
		ber = NULL;
	}

free_and_return:
	return ber;
}


/*
 * Helper function - convert a CSN to a string and ber_printf() it.
 */
static int
my_ber_printf_csn(BerElement *ber, const CSN *csn, const CSNType t)
{
	char csn_str[CSN_STRSIZE];
	int rc = -1;
	ber_int_t csn_type_as_ber = -1;

	switch (t)
	{
	    case CSN_TYPE_VALUE_UPDATED:
		    csn_type_as_ber = CSN_TYPE_VALUE_UPDATED_ON_WIRE;
		    break;
	    case CSN_TYPE_VALUE_DELETED:
		    csn_type_as_ber = CSN_TYPE_VALUE_DELETED_ON_WIRE;
		    break;
	    case CSN_TYPE_VALUE_DISTINGUISHED:
		    csn_type_as_ber = CSN_TYPE_VALUE_DISTINGUISHED_ON_WIRE;
		    break;
        case CSN_TYPE_ATTRIBUTE_DELETED:
            break;
	    default:
            slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "my_ber_printf_csn: unknown "
			                "csn type %d encountered.\n", (int)t);
        return -1;
	}

    csn_as_string(csn,PR_FALSE,csn_str);

    /* we don't send type for attr csn since there is only one */
	if (t == CSN_TYPE_ATTRIBUTE_DELETED)    	
    {        
		rc = ber_printf(ber, "s", csn_str);
		BER_DEBUG("s(csn_str)");
	}
    else
    {
    	rc = ber_printf(ber, "{es}", csn_type_as_ber, csn_str);
		BER_DEBUG("{e(csn type)s(csn)}");
    }

	return rc;
}


/*
 * Send a single annotated attribute value.
 */
static int
my_ber_printf_value(BerElement *ber, const char *type, const Slapi_Value *value, PRBool deleted)
{
	const struct berval *bval = NULL;
	int rc = -1;
    const CSNSet *csnset;
    void *cookie;
    CSN *csn;
    CSNType t;

    bval = slapi_value_get_berval(value);
	BER_DEBUG("{o(value)");
	if (ber_printf(ber, "{o", bval->bv_val, bval->bv_len) == -1) /* Start sequence */
	{
		goto done;
	}
	
/*	if (ber_printf(ber, "o", bval->bv_val, bval->bv_len) == -1)
	{
		goto done;
	} */

    if (deleted)
    {
		BER_DEBUG("b(deleted flag)");
        if (ber_printf (ber, "b", PR_TRUE) == -1)
        {
            goto done;
        }
    }
	/* Send value CSN list */
	BER_DEBUG("{");
	if (ber_printf(ber, "{") == -1) /* Start set */
	{
		goto done;
	}

    /* Iterate over the sequence of CSNs. */
	csnset = value_get_csnset (value);
    if (csnset)
    {
        for (cookie = csnset_get_first_csn (csnset, &csn, &t); NULL != cookie;
             cookie = csnset_get_next_csn (csnset, cookie, &csn, &t))
        {
			/* Don't send any adcsns, since that was already sent */
			if (t != CSN_TYPE_ATTRIBUTE_DELETED)
			{
				if (my_ber_printf_csn(ber, csn, t) == -1)
				{
					goto done;
				}
			}
        }
    }
	
	BER_DEBUG("}");
	if (ber_printf(ber, "}") == -1) /* End CSN sequence */
	{
		goto done;
	}
	BER_DEBUG("}");
	if (ber_printf(ber, "}") == -1) /* End sequence */
	{
		goto done;
	}

	/* Everything's ok */
	rc = 0;

done:
	return rc;

}

/* send a single attribute */
static int 
my_ber_printf_attr (BerElement *ber, Slapi_Attr *attr, PRBool deleted)
{
    Slapi_Value *value;
	char *type;
    int i;
    const CSN *csn;
			
    /* First, send the type */
	slapi_attr_get_type(attr, &type);
	BER_DEBUG("{s(type ");
	BER_DEBUG(type);
	BER_DEBUG(")");
	if (ber_printf(ber, "{s", type) == -1) /* Begin sequence for this type */
	{
		goto loser;
	}

	/* Send the attribute deletion CSN if present */
	csn = attr_get_deletion_csn(attr);
    if (csn)
    {
		if (my_ber_printf_csn(ber, csn, CSN_TYPE_ATTRIBUTE_DELETED) == -1)
		{
			goto loser;
		}
    }

    /* only send "is deleted" flag for deleted attributes since it defaults to false */
    if (deleted)
    {
		BER_DEBUG("b(del flag)");
        if (ber_printf (ber, "b", PR_TRUE) == -1)
        {
            goto loser;
        }
    }

	/*
	 * Iterate through all the values. 
     */
	BER_DEBUG("[");
	if (ber_printf(ber, "[") == -1) /* Begin set */
	{
		goto loser;
	}
	
    /* 
     * Process the non-deleted values first.
     */
	i = slapi_attr_first_value(attr, &value);
	while (i != -1)
	{
		if (my_ber_printf_value(ber, type, value, PR_FALSE) == -1)
		{
			goto loser;
		}
		i= slapi_attr_next_value(attr, i, &value);
	}

	/*
 	 * Now iterate over all of the deleted values.
	 */
	i= attr_first_deleted_value(attr, &value);
	while (i != -1)
	{
		if (my_ber_printf_value(ber, type, value, PR_TRUE) == -1)
		{
			goto loser;
		}
		i= attr_next_deleted_value(attr, i, &value);
	}
	BER_DEBUG("]");
	if (ber_printf(ber, "]") == -1) /* End set */
	{
		goto loser;
	}

	BER_DEBUG("}");
    if (ber_printf(ber, "}") == -1) /* End sequence for this type */
	{
		goto loser;
	}

    return 0;
loser:
    return -1;
}

/*
 * Get an annotated value from the BerElement. Returns 0 on
 * success, -1 on failure.
 */
static int
my_ber_scanf_value(BerElement *ber, Slapi_Value **value, PRBool *deleted)
{
	struct berval *attrval = NULL;
	ber_len_t len = -1;
	ber_tag_t tag;
	CSN *csn = NULL;
	char csnstring[CSN_STRSIZE + 1];
	CSNType csntype;
	char *lasti;

	PR_ASSERT(ber && value && deleted);

	*value = NULL;

	if (NULL == ber && NULL == value)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "my_ber_scanf_value BAD 1\n");
		goto loser;
	}

	/* Each value is a sequence */
	if (ber_scanf(ber, "{O", &attrval) == LBER_ERROR)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "my_ber_scanf_value BAD 2\n");
		goto loser;
	}
	/* Allocate and fill in the attribute value */
	if ((*value = slapi_value_new_berval(attrval)) == NULL)
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "my_ber_scanf_value BAD 3\n");
		goto loser;
	}

    /* check if this is a deleted value */
    if (ber_peek_tag(ber, &len) == LBER_BOOLEAN)
    {
        if (ber_scanf(ber, "b", deleted) == LBER_ERROR)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "my_ber_scanf_value BAD 4\n");
			goto loser;
		}
    }
        
    else /* default is present value */
    {
        *deleted = PR_FALSE;
    }

	/* Read the sequence of CSNs */
    for (tag = ber_first_element(ber, &len, &lasti);
		tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
		tag = ber_next_element(ber, &len, lasti))
	{
		ber_int_t csntype_tmp;
		/* Each CSN is in a sequence that includes a csntype and CSN */
		len = CSN_STRSIZE;
		if (ber_scanf(ber, "{es}", &csntype_tmp, csnstring, &len) == LBER_ERROR)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "my_ber_scanf_value BAD 7 - bval is %s\n", attrval->bv_val);
			goto loser;
		}
		switch (csntype_tmp)
		{
		case CSN_TYPE_VALUE_UPDATED_ON_WIRE:
			csntype = CSN_TYPE_VALUE_UPDATED;
			break;
		case CSN_TYPE_VALUE_DELETED_ON_WIRE:
			csntype = CSN_TYPE_VALUE_DELETED;
			break;
		case CSN_TYPE_VALUE_DISTINGUISHED_ON_WIRE:
			csntype = CSN_TYPE_VALUE_DISTINGUISHED;
			break;
		default:
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Error: preposterous CSN type "
				"%d received during total update.\n", csntype_tmp);
			goto loser;
		}
		csn = csn_new_by_string(csnstring);
		if (csn == NULL)
		{
			slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "my_ber_scanf_value BAD 8\n");
			goto loser;
		}
		value_add_csn(*value, csntype, csn);
        csn_free (&csn);
	}

	if (ber_scanf(ber, "}") == LBER_ERROR) /* End of annotated attribute value seq */
	{
		slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "my_ber_scanf_value BAD 10\n");
		goto loser;
	}
	
    if (attrval)
        ber_bvfree(attrval); 
	return 0;

loser:
	/* Free any stuff we allocated */
	if (csn)
        csn_free (&csn);
    if (attrval)
        ber_bvfree(attrval); 
    if (value)
    {
        slapi_value_free (value);
    }
   
	return -1;
}

static int 
my_ber_scanf_attr (BerElement *ber, Slapi_Attr **attr, PRBool *deleted)
{
    char *attrtype = NULL;
    CSN *attr_deletion_csn = NULL;
    PRBool val_deleted;
    char *lasti;
    ber_len_t len;
    ber_tag_t tag;
    char *str = NULL;
    int rc;
    Slapi_Value *value = NULL;

    PR_ASSERT (ber && attr && deleted);

    /* allocate the attribute */
    *attr = slapi_attr_new ();
    if (attr == NULL)
    {
        goto loser;
    }

	if (ber_scanf(ber, "{a", &attrtype) == LBER_ERROR) /* Begin sequence for this attr */
	{
		goto loser;
	}


    slapi_attr_init(*attr, attrtype);
    slapi_ch_free ((void **)&attrtype);

	/* The attribute deletion CSN is next and is optional? */
    if (ber_peek_tag(ber, &len) == LBER_OCTETSTRING)
    {
	    if (ber_scanf(ber, "a", &str) == LBER_ERROR)
	    {
		    goto loser;
	    }
	    attr_deletion_csn = csn_new_by_string(str);
	    slapi_ch_free((void **)&str);
    }

    if (attr_deletion_csn)
	{
		rc = attr_set_deletion_csn(*attr, attr_deletion_csn);
        csn_free (&attr_deletion_csn);
        if (rc != 0)
        {
            goto loser;
        }
	}

	/* The "attribute deleted" flag is next, and is optional */
	if (ber_peek_tag(ber, &len) == LBER_BOOLEAN)
	{
		if (ber_scanf(ber, "b", deleted) == LBER_DEFAULT)
		{
			goto loser;
		}
	} 
    else /* default is present */
    {
		*deleted = PR_FALSE;
	}
	
    /* loop over the list of attribute values */
	for (tag = ber_first_element(ber, &len, &lasti);
		tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
		tag = ber_next_element(ber, &len, lasti))
	{

		value = NULL;		
		if (my_ber_scanf_value(ber, &value, &val_deleted) == -1)
		{
			goto loser;
		}

        if (val_deleted)
        {
			/* Add the value to the attribute */
			if (attr_add_deleted_value(*attr, value) == -1) /* attr has ownership of value */
			{
				goto loser;
			}
        }
        else
        {
            /* Add the value to the attribute */
			if (slapi_attr_add_value(*attr, value) == -1) /* attr has ownership of value */
			{
				goto loser;
			}
        }
		if (value)
			slapi_value_free(&value);
	}	

	if (ber_scanf(ber, "}") == LBER_ERROR) /* End sequence for this attribute */
	{
		goto loser;
	}

    return 0;
loser:
    if (*attr)
        slapi_attr_free (attr);
    if (value)
        slapi_value_free (&value);

    slapi_ch_free_string(&attrtype);
    slapi_ch_free_string(&str);

    return -1;    
}

/*
 * Extract the payload from a total update extended operation,
 * decode it, and produce a Slapi_Entry structure representing a new
 * entry to be added to the local database.
 */
static int
decode_total_update_extop(Slapi_PBlock *pb, Slapi_Entry **ep)
{
	BerElement *tmp_bere = NULL;
	Slapi_Entry *e = NULL;
	Slapi_Attr *attr = NULL;
	char *str = NULL;
	struct berval *extop_value = NULL;
	char *extop_oid = NULL;
	ber_len_t len;
	char *lasto;
	ber_tag_t tag;
	int rc;
	PRBool deleted;
	
	PR_ASSERT(NULL != pb);
	PR_ASSERT(NULL != ep);

	slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_OID, &extop_oid);
	slapi_pblock_get(pb, SLAPI_EXT_OP_REQ_VALUE, &extop_value);

	if (NULL == extop_oid ||
		((strcmp(extop_oid, REPL_NSDS50_REPLICATION_ENTRY_REQUEST_OID) != 0) && 
		(strcmp(extop_oid, REPL_NSDS71_REPLICATION_ENTRY_REQUEST_OID) != 0)) ||
		NULL == extop_value)
	{
		/* Bogus */
		goto loser;
	}

	if ((tmp_bere = ber_init(extop_value)) == NULL)
	{
		goto loser;
	}

	if ((e = slapi_entry_alloc()) == NULL)
	{
		goto loser;
	}

	if (ber_scanf(tmp_bere, "{") == LBER_ERROR) /* Begin outer sequence */
	{
		goto loser;
	}

	/* The entry's uniqueid is first */
	if (ber_scanf(tmp_bere, "a", &str) == LBER_ERROR)
	{
		goto loser;
	}
	slapi_entry_set_uniqueid(e, str);
	str = NULL;	/* Slapi_Entry now owns the uniqueid */

	/* The entry's DN is next */
	if (ber_scanf(tmp_bere, "a", &str) == LBER_ERROR)
	{
		goto loser;
	}
	slapi_entry_set_dn(e, str);
	str = NULL; /* Slapi_Entry now owns the dn */

	/* Get the attributes */
	for ( tag = ber_first_element( tmp_bere, &len, &lasto );
		tag != LBER_ERROR && tag != LBER_END_OF_SEQORSET;
		tag = ber_next_element( tmp_bere, &len, lasto ) )
	{

        if (my_ber_scanf_attr (tmp_bere, &attr, &deleted) != 0)
        {
            goto loser;
        }
		
	    /* Add the attribute to the entry */
        if (deleted)
            entry_add_deleted_attribute_wsi(e, attr); /* entry now owns attr */
        else
		    entry_add_present_attribute_wsi(e, attr); /* entry now owns attr */
        attr = NULL;
	}

    if (ber_scanf(tmp_bere, "}") == LBER_ERROR) /* End sequence for this entry */
	{
		goto loser;
	}

	/* Check for ldapsubentries and tombstone entries to set flags properly */
	slapi_entry_attr_find(e, "objectclass", &attr);
	if (attr != NULL) {
		struct berval bv;
		bv.bv_val = "ldapsubentry";
		bv.bv_len = strlen(bv.bv_val);
		if (slapi_attr_value_find(attr, &bv) == 0) {
			slapi_entry_set_flag(e, SLAPI_ENTRY_LDAPSUBENTRY);
		}
		bv.bv_val = SLAPI_ATTR_VALUE_TOMBSTONE;
		bv.bv_len = strlen(bv.bv_val);
		if (slapi_attr_value_find(attr, &bv) == 0) {
			slapi_entry_set_flag(e, SLAPI_ENTRY_FLAG_TOMBSTONE);
		}
	}
	
	/* If we get here, the entry is properly constructed. Return it. */

	rc = 0;
	*ep = e;
	goto free_and_return;

loser:
	rc = -1;
	/* slapi_ch_free accepts NULL pointer */
	slapi_ch_free((void **)&str);

    if (attr != NULL)
    {
        slapi_attr_free (&attr);
    }
    
    if (NULL != e)
    {
        slapi_entry_free (e);
    }
	*ep = NULL;
	slapi_log_error(SLAPI_LOG_FATAL, repl_plugin_name, "Error: could not decode extended "
		"operation containing entry for total update.\n");

free_and_return:
    if (NULL != tmp_bere)
	{
		ber_free(tmp_bere, 1); 
		tmp_bere = NULL;
	}
	return rc;
}

/*
 * This plugin entry point is called whenever an NSDS50ReplicationEntry
 * extended operation is received.
 */
int
multimaster_extop_NSDS50ReplicationEntry(Slapi_PBlock  *pb)
{
	int rc;
	Slapi_Entry *e = NULL;
    Slapi_Connection *conn = NULL;
	PRUint64 connid = 0;
	int opid = 0;
	
	slapi_pblock_get(pb, SLAPI_CONN_ID, &connid);
	slapi_pblock_get(pb, SLAPI_OPERATION_ID, &opid);

	/* Decode the extended operation */
	rc = decode_total_update_extop(pb, &e);

	if (0 == rc)
	{
#ifdef notdef
		/*
		 * Just spew LDIF so we're sure we got it right. Later we'll firehose
		 * this into the database import code
		 */
		int len;
		char *str = slapi_entry2str_with_options(e, &len,SLAPI_DUMP_UNIQUEID);
		puts(str);
		free(str);
#endif

       rc = slapi_import_entry (pb, e); 
       /* slapi_import_entry returns an LDAP error in case of a
        * problem.  If there's a problem, it's our responsibility
        * to free the slapi_entry that we're trying to import.
        */
       if (rc != LDAP_SUCCESS)
	   {
		   const char *dn = slapi_entry_get_dn_const(e);
		   slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						   "Error %d: could not import entry dn %s "
						   "for total update operation conn=%" NSPRIu64 " op=%d\n",
						   rc, dn, connid, opid);
		   rc = -1;
	   }
        
	}
	else
	{
		slapi_log_error(SLAPI_LOG_REPL, repl_plugin_name,
						"Error %d: could not decode the total update extop "
						"for total update operation conn=%" NSPRIu64 " op=%d\n",
						rc, connid, opid);
	}
   
    if (rc != 0)
    {
        /* just disconnect from the supplier. bulk import is stopped when
           connection object is destroyed */
        slapi_pblock_get (pb, SLAPI_CONNECTION, &conn);
        if (conn)
        {
            slapi_disconnect_server(conn);
        }

        /* cleanup */
        if (e)
        {
            slapi_entry_free (e);
        }
    }

	return rc;
}
