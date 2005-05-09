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

/*
 * NetAPIPartition.java
 *
 * Created on February 22, 2005, 9:34 AM
 */
package org.apache.ldap.server;

import java.util.Map;
//import java.util.Collection;
import java.util.Date;
import java.util.Properties;
import java.io.File;
import java.io.FileWriter;
import javax.naming.Name;
import javax.naming.NamingEnumeration;
import javax.naming.NamingException;
import javax.naming.directory.Attributes;
import javax.naming.directory.ModificationItem;
import javax.naming.directory.SearchControls;

import org.apache.ldap.common.name.LdapName;
//import org.apache.ldap.common.util.PropertiesUtils;
import org.apache.ldap.common.filter.ExprNode;
import org.apache.ldap.server.ContextPartition;
//import org.apache.ldap.common.message.Control;
import org.apache.ldap.common.filter.PresenceNode;

import javax.naming.directory.BasicAttribute;
import javax.naming.directory.BasicAttributes;
import javax.naming.directory.Attribute;
import javax.naming.directory.SearchResult;
import javax.naming.directory.DirContext;
import java.util.StringTokenizer;
import java.util.HashSet;
import org.bpi.jnetman.*;

/**
 *
 * @author scott
 */
public class NetAPIPartition implements ContextPartition {

    static {
    	System.loadLibrary("jnetman");
        //System.out.println("dll loaded");
    }

	public static byte[] HexStringToByteArray(String hexString) {
		byte[] byteArray = new byte[hexString.length() / 2]; 
		for(int i = 0; i < hexString.length() / 2; i++) {
			byteArray[i] = (byte)Integer.parseInt(hexString.substring(i * 2, (i * 2) + 2), 16);
		}
		return byteArray;
	}
	
	public static String ByteArrayToHexString(byte[] byteArray) {
		String hexString = "";
		for(int i = 0; i < byteArray.length; i++) {
			hexString = hexString.concat(Integer.toHexString(byteArray[i] & 0xff));
		}
		return hexString;
	}
    
    //private LdapName suffix;
    private String suffix;
    private static final String container = new String("cn=users").toLowerCase();
    private static final String logFilename = new String("../logs/usersync.log");
    private static final int GLOBAL_FLAG = 0x00000002;
    private static final int DOMAINLOCAL_FLAG = 0x00000004;
    private FileWriter outLog;
    
    /** Creates a new instance of NetAPIPartition */
    public NetAPIPartition(Name upSuffix, Name normSuffix, String properties) {
        try {
        	outLog = new FileWriter(new File(logFilename));
        }
        catch(Exception e) {
        }
        
        try {
        	outLog.write(new Date() + ": reached NetAPIPartition" + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition");
        suffix = normSuffix.toString();
    }

    /**
     * Deletes a leaf entry from this BackingStore: non-leaf entries cannot be 
     * deleted until this operation has been applied to their children.
     *
     * @param name the normalized distinguished/absolute name of the entry to
     * delete from this BackingStore.
     * @throws NamingException if there are any problems
     */ 
    public void delete( Name name ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.delete: " + name + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.delete: " + name);
        
        String rdn = getRDN(name.toString());
        boolean deletedSomthing = false;
        NTUser user = new NTUser();
        NTGroup group = new NTGroup();
        NTLocalGroup localGroup = new NTLocalGroup();
        
        if(name.toString().toLowerCase().startsWith(new String("sAMAccountName").toLowerCase())) {
        	if(user.RetriveUserByAccountName(rdn) == 0) {
        		if(user.DeleteUser(user.GetAccountName()) == 0) {
        			deletedSomthing = true;
        		}
        	}
        	if(group.RetriveGroupByAccountName(rdn) == 0) {
        		if(group.DeleteGroup(group.GetAccountName()) == 0) {
        			deletedSomthing = true;
        		}
        	}
        	if(localGroup.RetriveLocalGroupByAccountName(rdn) == 0) {
        		if(localGroup.DeleteLocalGroup(localGroup.GetAccountName()) == 0) {
        			deletedSomthing = true;
        		}
        	}
        }
        else if((name.toString().toLowerCase().startsWith(new String("objectGUID").toLowerCase())) ||
        		(name.toString().toLowerCase().startsWith(new String("GUID").toLowerCase()))) {
        	
        	if(user.RetriveUserBySIDHexStr(rdn) == 0) {
        		if(user.DeleteUser(user.GetAccountName()) == 0) {
        			deletedSomthing = true;
        		}
        	}
        	if(group.RetriveGroupBySIDHexStr(rdn) == 0) {
        		if(group.DeleteGroup(group.GetAccountName()) == 0) {
        			deletedSomthing = true;
        		}
        	}
        	if(localGroup.RetriveLocalGroupBySIDHexStr(rdn) == 0) {
        		if(localGroup.DeleteLocalGroup(localGroup.GetAccountName()) == 0) {
        			deletedSomthing = true;
        		}
        	}
        }
        else {
        	throw new NamingException("Can not delete DN: " + name);
        }
        
        if(!deletedSomthing) {
            throw new NamingException("No matching users or groups: " + rdn);
        }
    }

    /**
     * Adds an entry to this BackingStore.
     *
     * @param upName the user provided distinguished/absolute name of the entry
     * @param normName the normalized distinguished/absolute name of the entry
     * @param entry the entry to add to this BackingStore
     * @throws NamingException if there are any problems
     */
    public void add( String upName, Name normName, Attributes entry ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.add: " + normName + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.add: " + normName);
        
        String rdn = getRDN(normName.toString());
        Attribute attribute = entry.get("objectClass");
        Attribute groupType;
        ModificationItem[] modItems = new ModificationItem[entry.size()];
        NamingEnumeration modAttributes = entry.getAll();
        NTUser user = new NTUser();
        NTGroup group = new NTGroup();
        NTLocalGroup localGroup = new NTLocalGroup();
        int result;

        for(int i = 0; i < entry.size(); i++) {
        	modItems[i] = new ModificationItem(DirContext.ADD_ATTRIBUTE, (Attribute)modAttributes.next());
        }

        if(normName.toString().compareToIgnoreCase(suffix) == 0) {
        	// Gets us past the CoreContextFactory.startUpAppPartitions
        }
        else if((normName.toString().toLowerCase().endsWith(container + "," + suffix)) &&
        		(normName.toString().toLowerCase().startsWith(new String("sAMAccountName").toLowerCase()))) {
        	
            if(attribute.contains("user")) {
                user.NewUser(rdn);
                result = user.AddUser();
                if(result != 0) {
                	throw new NamingException("Failed to add new user: " + normName + " (" + result + ")");
                }
                modNTUserAttributes(user, modItems);
                if(user.StoreUser() != 0) {
                	throw new NamingException("Failed to commit modified user information: " + normName);
                }
            }
            else if(attribute.contains("group")) {
            	attribute = entry.get("groupType");
            	if(attribute == null) {
            		throw new NamingException("Missing groupType");
            	}
            	
            	if(((new Integer((String)attribute.get())).intValue() & GLOBAL_FLAG) == GLOBAL_FLAG) {
            		group.NewGroup(rdn);
                    if(group.AddGroup() != 0) {
                    	throw new NamingException("Failed to add new group: " + normName);
                    }
                    modNTGroupAttributes(group, modItems);
                    if(group.StoreGroup() != 0) {
                    	throw new NamingException("Failed to commit modified user information: " + normName);
                    }
            	}
            	else if(((new Integer((String)attribute.get())).intValue() & DOMAINLOCAL_FLAG) == DOMAINLOCAL_FLAG) {
                    localGroup.NewLocalGroup(rdn);
                    modNTLocalGroupAttributes(localGroup, modItems);
                    if(localGroup.AddLocalGroup() != 0) {
                    	throw new NamingException("Failed add new local group: " + normName);
                    }
                    modNTLocalGroupAttributes(localGroup, modItems);
                    if(localGroup.StoreLocalGroup() != 0) {
                    	throw new NamingException("Failed to commit modified user information: " + normName);
                    }
            	}
            	else {
            		throw new NamingException("Unknown group type: " + (Integer)attribute.get());
            	}
            }
            else {
                throw new NamingException("No matching objectClass");
            }
        }
        else {
            throw new NamingException("Attempt to add an entry outside partition scope: " + normName);
        }
    }

    /**
     * Modifies an entry by adding, removing or replacing a set of attributes.
     *
     * @param name the normalized distinguished/absolute name of the entry to
     * modify
     * @param modOp the modification operation to perform on the entry which
     * is one of constants specified by the DirContext interface:
     * <code>ADD_ATTRIBUTE, REMOVE_ATTRIBUTE, REPLACE_ATTRIBUTE</code>.
     * @param mods the attributes and their values used to affect the
     * modification with.
     * @throws NamingException if there are any problems
     * @see javax.naming.directory.DirContext
     * @see javax.naming.directory.DirContext.ADD_ATTRIBUTE
     * @see javax.naming.directory.DirContext.REMOVE_ATTRIBUTE
     * @see javax.naming.directory.DirContext.REPLACE_ATTRIBUTE
     */
    public void modify( Name name, int modOp, Attributes mods ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.modify1: " + name + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.modify1: " + name);
        
        ModificationItem[] modItems = new ModificationItem[mods.size()];
        NamingEnumeration modAttributes = mods.getAll();

        for(int i = 0; i < mods.size(); i++) {
        	modItems[i] = new ModificationItem(modOp, (Attribute)modAttributes.next());
        }
        
        modify(name, modItems);
    }

    /**
     * Modifies an entry by using a combination of adds, removes or replace 
     * operations using a set of ModificationItems.
     *
     * @param name the normalized distinguished/absolute name of the entry to modify
     * @param mods the ModificationItems used to affect the modification with
     * @throws NamingException if there are any problems
     * @see ModificationItem
     */
    public void modify( Name name, ModificationItem [] mods ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.modify2: " + name + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.modify2: " + name);

        String rdn = getRDN(name.toString());
        boolean modifiedSomething = false;
        NTUser user = new NTUser();
        NTGroup group = new NTGroup();
        NTLocalGroup localGroup = new NTLocalGroup();

        if(name.toString().toLowerCase().startsWith(new String("sAMAccountName").toLowerCase())) {
            if(user.RetriveUserByAccountName(rdn) == 0) {
                modNTUserAttributes(user, mods);
                if(user.StoreUser() != 0) {
                	throw new NamingException("Failed to commit modified user information: " + name);
                }
                
                modifiedSomething = true;
            }
            else if(group.RetriveGroupByAccountName(rdn) == 0) {
                modNTGroupAttributes(group, mods);
                if(group.StoreGroup() != 0) {
                	throw new NamingException("Failed to commit modified group information: " + name);
                }
                
                modifiedSomething = true;
            }
            else if(localGroup.RetriveLocalGroupByAccountName(rdn) == 0) {
                modNTLocalGroupAttributes(localGroup, mods);
                if(localGroup.StoreLocalGroup() != 0) {
                	throw new NamingException("Failed to commit modified local group information: " + name);
                }
                
                modifiedSomething = true;
            }
        }
        else if((name.toString().toLowerCase().startsWith(new String("objectGUID").toLowerCase())) ||
        		(name.toString().toLowerCase().startsWith(new String("GUID").toLowerCase()))) {
        	
        	if(user.RetriveUserBySIDHexStr(rdn) == 0) {
                modNTUserAttributes(user, mods);
                if(user.StoreUser() != 0) {
                	throw new NamingException("Failed to commit modified user information: " + name);
                }
                
                modifiedSomething = true;
            }
            else if(group.RetriveGroupBySIDHexStr(rdn) == 0) {
                modNTGroupAttributes(group, mods);
                if(group.StoreGroup() != 0) {
                	throw new NamingException("Failed to commit modified group information: " + name);
                }
                
                modifiedSomething = true;
            }
            else if(localGroup.RetriveLocalGroupBySIDHexStr(rdn) == 0) {
                modNTLocalGroupAttributes(localGroup, mods);
                if(localGroup.StoreLocalGroup() != 0) {
                	throw new NamingException("Failed to commit modified local group information: " + name);
                }
                
                modifiedSomething = true;
            }
        }
        else {
            throw new NamingException("Can not delete DN: " + name);
        }
        
        if(!modifiedSomething) {
            throw new NamingException("No matching users or groups: " + rdn);
        }
    }

    /**
     * A specialized form of one level search used to return a minimal set of 
     * information regarding child entries under a base.  Convenience method
     * used to optimize operations rather than conducting a full search with 
     * retrieval.
     *
     * @param base the base distinguished/absolute name for the search/listing
     * @return a NamingEnumeration containing objects of type
     * {@link org.apache.ldap.server.db.DbSearchResult}
     * @throws NamingException if there are any problems
     */
    public NamingEnumeration list( Name base ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.list" + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.list");

        return new BasicAttribute(base.toString()).getAll();
    }
    
    /**
     * Conducts a search against this BackingStore.  Namespace specific
     * parameters for search are contained within the environment using
     * namespace specific keys into the hash.  For example in the LDAP namespace
     * a BackingStore implementation may look for search Controls using a
     * namespace specific or implementation specific key for the set of LDAP
     * Controls.
     *
     * @param base the normalized distinguished/absolute name of the search base
     * @param env the environment under which operation occurs
     * @param filter the root node of the filter expression tree
     * @param searchCtls the search controls
     * @throws NamingException if there are any problems
     * @return a NamingEnumeration containing objects of type 
     * <a href="http://java.sun.com/j2se/1.4.2/docs/api/
     * javax/naming/directory/SearchResult.html">SearchResult</a>.
     */
    public NamingEnumeration search( Name base, Map env, ExprNode filter,
        SearchControls searchCtls ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.search: " + base + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.search: " + base + " " + filter);
        
        BasicAttribute results = new BasicAttribute(null);
        SearchResult result;
        BasicAttributes attributes;
        BasicAttribute attribute;
        String rdn = getRDN(base.toString());
        NTUser user = new NTUser();
        NTGroup group = new NTGroup();
        NTLocalGroup localGroup = new NTLocalGroup();
        
        // base equals suffix
        if(base.toString().compareToIgnoreCase(suffix) == 0) {
        	// object scope
        	if(((searchCtls.getSearchScope() == SearchControls.OBJECT_SCOPE) ||
        			(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE)) &&
        			(filter.toString().toLowerCase().startsWith(new String("(objectClass=*)").toLowerCase()))) {
        		
                attributes = new BasicAttributes();

                attribute = new BasicAttribute("objectClass");
                attribute.add("top");
                attribute.add("domain");
                attributes.put(attribute);

                result = new SearchResult(suffix, null, attributes);
                results.add(result);
        	}
        	
        	// one level or subtree scope
        	if(((searchCtls.getSearchScope() == SearchControls.ONELEVEL_SCOPE) ||
					(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE)) &&
        			(filter.toString().toLowerCase().startsWith(new String("(objectClass=*)").toLowerCase()))) {
                
                result = new SearchResult(container + "," + suffix, null, new BasicAttributes());
                results.add(result);
        	}
        	
        	// subtree scope
        	if(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE) {
        		searchAccounts(base, env, filter, searchCtls, results);
        	}
        }
        // base equals container plus suffix 
        else if(base.toString().compareToIgnoreCase(container + "," + suffix) == 0) {
        	// object scope
        	if(((searchCtls.getSearchScope() == SearchControls.OBJECT_SCOPE) ||
					(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE)) &&
        			(filter.toString().toLowerCase().startsWith(new String("(objectClass=*)").toLowerCase()))) {
        		
        		attributes = new BasicAttributes();

                attribute = new BasicAttribute("objectClass");
                attribute.add("top");
                attribute.add("domain");
                attributes.put(attribute);

                result = new SearchResult(container + "," + suffix, null, attributes);
                results.add(result);
        	}
        	
        	// one level or subtree scope
        	if((searchCtls.getSearchScope() == SearchControls.ONELEVEL_SCOPE) ||
					(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE)) {
        		
        		searchAccounts(base, env, filter, searchCtls, results);
        	}
        	
        	// subtree scope
        	if(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE) {
        		// Nothing that OVELEVEL_SCOPE || SUBTREE_SCOPE doesn't already cover
        	}
        }
        // base ends with container plus suffix
        else if(base.toString().toLowerCase().endsWith(new String(container + "," + suffix).toLowerCase())) {
        	// object scope
        	if((searchCtls.getSearchScope() == SearchControls.OBJECT_SCOPE) ||
					(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE)) {
        		
        		searchAccounts(base, env, filter, searchCtls, results);
        	}
        	
        	// one level or subtree scope
        	if((searchCtls.getSearchScope() == SearchControls.ONELEVEL_SCOPE) ||
					(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE)) {
        		// Empty set
        	}
        	
        	// subtree scope
        	if(searchCtls.getSearchScope() == SearchControls.SUBTREE_SCOPE) {
        		// Nothing that OBJECT_SCOPE || SUBTREE_SCOPE doesn't already cover
        	}
        }
        // unknown base
        else {
        	throw new NamingException("Attempt to search for an entry outside partition scope: " + base);
        }
        
        return results.getAll();
    }

    /**
     * Looks up an entry by distinguished/absolute name.  This is a simplified
     * version of the search operation used to point read an entry used for
     * convenience.
     *
     * @param name the normalized distinguished name of the object to lookup
     * @return an Attributes object representing the entry
     * @throws NamingException if there are any problems
     */
    public Attributes lookup( Name name ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.lookup1: " + name + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.lookup1: " + name);
        
        BasicAttributes attributes = null;
        BasicAttribute attribute;
        String rdn = getRDN(name.toString());
        NTUser user = new NTUser();
        NTGroup group = new NTGroup();
        NTLocalGroup localGroup = new NTLocalGroup();
        
        if(name.toString().compareToIgnoreCase(suffix) == 0) {
            attributes = new BasicAttributes();

            attribute = new BasicAttribute("objectClass");
            attribute.add("top");
            attribute.add("domain");
            attributes.put(attribute);
        }
        else if(name.toString().compareToIgnoreCase(container + "," + suffix) == 0) {
            attributes = new BasicAttributes();

            attribute = new BasicAttribute("objectClass");
            attribute.add("top");
            attribute.add("domain");
            attributes.put(attribute);
        }
        else if(name.toString().toLowerCase().endsWith(container + "," + suffix)) {
	        if(user.RetriveUserByAccountName(rdn) == 0) {
	            attributes = getNTUserAttributes(user, rdn);
	        }
	        else if(group.RetriveGroupByAccountName(rdn) == 0) {
	            attributes = getNTGroupAttributes(group, rdn);
	        }
	        else if(localGroup.RetriveLocalGroupByAccountName(rdn) == 0) {
	            attributes = getNTLocalGroupAttributes(localGroup, rdn);
	        }
        }
        else {
            throw new NamingException("Attempt to look up an entry outside partition scope: " + name);
        }
        
        return attributes;
    }

    /**
     * Looks up an entry by distinguished name.  This is a simplified version
     * of the search operation used to point read an entry used for convenience
     * with a set of attributes to return.  If the attributes are null or emty
     * this defaults to the lookup opertion without the attributes.
     *
     * @param dn the normalized distinguished name of the object to lookup
     * @param attrIds the set of attributes to return
     * @return an Attributes object representing the entry
     * @throws NamingException if there are any problems
     */
    public Attributes lookup( Name dn, String [] attrIds ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.lookup2: " + dn + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.lookup2: " + dn);
        
        return lookup(dn);
    }

    /**
     * Fast operation to check and see if a particular entry exists.
     *
     * @param name the normalized distinguished/absolute name of the object to
     * check for existance
     * @return true if the entry exists, false if it does not
     * @throws NamingException if there are any problems
     */
    public boolean hasEntry( Name name ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.hasEntry: " + name + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.hasEntry: " + name);

        boolean result = false;
        String rdn = getRDN(name.toString());
        NTUser user = new NTUser();
        NTGroup group = new NTGroup();
        NTLocalGroup localGroup = new NTLocalGroup();

        if(name.toString().compareToIgnoreCase(suffix) == 0) {
            result = true;
        }
        else if(name.toString().compareToIgnoreCase(container + "," + suffix) == 0) {
            result = true;
        }
        
        // An exception raised in searchAccounts is treated as a false hasEntry result
        try {
        	if(searchAccounts(name, new Properties(), new PresenceNode(null), new SearchControls(), new BasicAttribute(null)) > 0) {
            	result = true;
        	}
        }
        catch(Exception e) {
        }

        return result;
    }

    /**
     * Checks to see if name is a context suffix.
     *
     * @param name the normalized distinguished/absolute name of the context
     * @return true if the name is a context suffix, false if it is not.
     * @throws NamingException if there are any problems
     */
    public boolean isSuffix( Name name ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.isSuffix" + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.isSuffix");

        return false;
    }

    /**
     * Modifies an entry by changing its relative name. Optionally attributes
     * associated with the old relative name can be removed from the entry.
     * This makes sense only in certain namespaces like LDAP and will be ignored
     * if it is irrelavent.
     *
     * @param name the normalized distinguished/absolute name of the entry to
     * modify the RN of.
     * @param newRn the new RN of the entry specified by name
     * @param deleteOldRn boolean flag which removes the old RN attribute
     * from the entry if set to true, and has no affect if set to false
     * @throws NamingException if there are any problems
     */
    public void modifyRn( Name name, String newRn, boolean deleteOldRn )
        throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.modifyRn" + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.modifyRn");

    }

    /**
     * Transplants a child entry, to a position in the namespace under a new
     * parent entry.
     *
     * @param newParentName the normalized distinguished/absolute name of the
     * new parent to move the target entry to
     * @param oriChildName the normalized distinguished/absolute name of the
     * original child name representing the child entry to move
     * @throws NamingException if there are any problems
     */
    public void move( Name oriChildName, Name newParentName ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.move1" + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.move1");

    }

    /**
     * Transplants a child entry, to a position in the namespace under a new
     * parent entry and changes the RN of the child entry which can optionally
     * have its old RN attributes removed.  The removal of old RN attributes
     * may not make sense in all namespaces.  If the concept is undefined in a
     * namespace this parameters is ignored.  An example of a namespace where
     * this parameter is significant is the LDAP namespace.
     *
     * @param oriChildName the normalized distinguished/absolute name of the
     * original child name representing the child entry to move
     * @param newParentName the normalized distinguished/absolute name of the
     * new parent to move the targeted entry to
     * @param newRn the new RN of the entry
     * @param deleteOldRn boolean flag which removes the old RN attribute
     * from the entry if set to true, and has no affect if set to false
     * @throws NamingException if there are any problems
     */
    public void move( Name oriChildName, Name newParentName, String newRn,
               boolean deleteOldRn ) throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.move2" + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.move2");

    }

    /**
     * Cue to BackingStores with caches to flush entry and index changes to disk.
     *
     * @throws NamingException if there are problems flushing caches
     */
    public void sync() throws NamingException {
    }

    /**
     * Closes or shuts down this BackingStore.  Operations against closed
     * BackingStores will fail.
     *
     * @throws NamingException if there are problems shutting down
     */
    public void close() throws NamingException {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.close" + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.close");

    }

    /**
     * Checks to see if this BackingStore has been closed or shut down.
     * Operations against closed BackingStores will fail.
     *
     * @return true if shut down, false otherwise
     */
    public boolean isClosed() {
    	try {
        	outLog.write(new Date() + ": reached NetAPIPartition.isClosed" + "\n");
        	outLog.flush();
        }
        catch(Exception e) {
        }
        //System.out.println("reached NetAPIPartition.isClosed");

        return true;
    }
    
    /**
     * Gets the distinguished/absolute name of the suffix for all entries
     * stored within this BackingStore.
     *
     * @param normalized boolean value used to control the normalization of the
     * returned Name.  If true the normalized Name is returned, otherwise the 
     * original user provided Name without normalization is returned.
     * @return Name representing the distinguished/absolute name of this
     * BackingStores root context.
     */
    public Name getSuffix( boolean normalized ) {
    	LdapName name = null; 
    	
    	try {
    		name = new LdapName(suffix);
    	}
    	catch(NamingException ne) {
    	}
    	
    	return name;
    }
    
    private String getRDN(String dn) {
        StringTokenizer tokenizer;
        String rdn;

        tokenizer = new StringTokenizer(dn, "(),=<>");
        rdn = tokenizer.nextToken();
        rdn = tokenizer.nextToken();
        
        return rdn;
    }
    
    private int searchAccounts(Name base, Map env, ExprNode filter,
            SearchControls searchCtls, BasicAttribute results) throws NamingException {
    	
    	int resultCount = 0;
    	SearchResult result;
    	BasicAttributes attributes;
        String rdn = getRDN(base.toString());

        NTUser user = new NTUser();
        NTGroup group = new NTGroup();
        NTLocalGroup localGroup = new NTLocalGroup();
        
        if(base.toString().toLowerCase().startsWith(new String("sAMAccountName").toLowerCase())) {
        	if(user.RetriveUserByAccountName(rdn) == 0) {
        		attributes = new BasicAttributes();
	            
	            attributes = getNTUserAttributes(user, rdn);
	            result = new SearchResult("sAMAccountName=" + user.GetAccountName() + "," + container + "," + suffix, null, attributes);
	            results.add(result);
	            resultCount++;
        	}
        	else if(group.RetriveGroupByAccountName(rdn) == 0) {
        		attributes = new BasicAttributes();
	            
	            attributes = getNTGroupAttributes(group, rdn);
	            result = new SearchResult("sAMAccountName=" + group.GetAccountName() + "," + container + "," + suffix, null, attributes);
	            results.add(result);
	            resultCount++;
        	}
        	else if(localGroup.RetriveLocalGroupByAccountName(rdn) == 0) {
        		attributes = new BasicAttributes();
	            
	            attributes = getNTLocalGroupAttributes(localGroup, rdn);
	            result = new SearchResult("sAMAccountName=" + localGroup.GetAccountName() + "," + container + "," + suffix, null, attributes);
	            results.add(result);
	            resultCount++;
        	}
        	else {
        		// empty set
        	}
        }
        else if((base.toString().toLowerCase().startsWith(new String("objectGUID").toLowerCase())) ||
        		(base.toString().toLowerCase().startsWith(new String("GUID").toLowerCase()))) {
        	if(user.RetriveUserBySIDHexStr(rdn) == 0) {
        		attributes = new BasicAttributes();
	            
	            attributes = getNTUserAttributes(user, rdn);
	            result = new SearchResult("sAMAccountName=" + user.GetAccountName() + "," + container + "," + suffix, null, attributes);
	            results.add(result);
	            resultCount++;
        	}
        	else if(group.RetriveGroupBySIDHexStr(rdn) == 0) {
        		attributes = new BasicAttributes();
	            
	            attributes = getNTGroupAttributes(group, rdn);
	            result = new SearchResult("sAMAccountName=" + group.GetAccountName() + "," + container + "," + suffix, null, attributes);
	            results.add(result);
	            resultCount++;
        	}
        	else if(localGroup.RetriveLocalGroupBySIDHexStr(rdn) == 0) {
        		attributes = new BasicAttributes();
	            
	            attributes = getNTLocalGroupAttributes(localGroup, rdn);
	            result = new SearchResult("sAMAccountName=" + localGroup.GetAccountName() + "," + container + "," + suffix, null, attributes);
	            results.add(result);
	            resultCount++;
        	}
        	else {
        		// empty set
        	}
        }
        else if((base.toString().compareToIgnoreCase(suffix) == 0) ||
        		base.toString().compareToIgnoreCase(container + "," + suffix) == 0) {
        	if(filter.toString().toLowerCase().startsWith(new String("(sAMAccountName=").toLowerCase())) {
        		rdn = getRDN(filter.toString());
        		
            	if(user.RetriveUserByAccountName(rdn) == 0) {
            		attributes = new BasicAttributes();
    	            
    	            attributes = getNTUserAttributes(user, rdn);
    	            result = new SearchResult("sAMAccountName=" + user.GetAccountName() + "," + container + "," + suffix, null, attributes);
    	            results.add(result);
    	            resultCount++;
            	}
            	else if(group.RetriveGroupByAccountName(rdn) == 0) {
            		attributes = new BasicAttributes();
    	            
    	            attributes = getNTGroupAttributes(group, rdn);
    	            result = new SearchResult("sAMAccountName=" + group.GetAccountName() + "," + container + "," + suffix, null, attributes);
    	            results.add(result);
    	            resultCount++;
            	}
            	else if(localGroup.RetriveLocalGroupByAccountName(rdn) == 0) {
            		attributes = new BasicAttributes();
    	            
    	            attributes = getNTLocalGroupAttributes(localGroup, rdn);
    	            result = new SearchResult("sAMAccountName=" + localGroup.GetAccountName() + "," + container + "," + suffix, null, attributes);
    	            results.add(result);
    	            resultCount++;
            	}
            	else {
            		// empty set
            	}
        	}
        	else if((filter.toString().toLowerCase().startsWith(new String("(objectGUID=").toLowerCase())) ||
        			(filter.toString().toLowerCase().startsWith(new String("(GUID=").toLowerCase()))) {
        		rdn = getRDN(filter.toString());
        		
            	if(user.RetriveUserBySIDHexStr(rdn) == 0) {
            		attributes = new BasicAttributes();
    	            
    	            attributes = getNTUserAttributes(user, rdn);
    	            result = new SearchResult("sAMAccountName=" + user.GetAccountName() + "," + container + "," + suffix, null, attributes);
    	            results.add(result);
    	            resultCount++;
            	}
            	else if(group.RetriveGroupBySIDHexStr(rdn) == 0) {
            		attributes = new BasicAttributes();
    	            
    	            attributes = getNTGroupAttributes(group, rdn);
    	            result = new SearchResult("sAMAccountName=" + group.GetAccountName() + "," + container + "," + suffix, null, attributes);
    	            results.add(result);
    	            resultCount++;
            	}
            	else if(localGroup.RetriveLocalGroupBySIDHexStr(rdn) == 0) {
            		attributes = new BasicAttributes();
    	            
    	            attributes = getNTLocalGroupAttributes(localGroup, rdn);
    	            result = new SearchResult("sAMAccountName=" + localGroup.GetAccountName() + "," + container + "," + suffix, null, attributes);
    	            results.add(result);
    	            resultCount++;
            	}
            	else {
            		// empty set
            	}
        	}
        	else if(filter.toString().toLowerCase().startsWith(new String("(objectClass=*)").toLowerCase())) {
		    	NTUserList users = new NTUserList();
		        if(users.loadList() != 0) {
		            throw new NamingException("Failed to load user list");
		        }
		        while(users.hasMore()) {
		            attributes = new BasicAttributes();
		            
		            rdn = users.nextUsername();
		            if(!rdn.endsWith("$")) {
			            user.RetriveUserByAccountName(rdn);
			            attributes = getNTUserAttributes(user, rdn);
			            result = new SearchResult("sAMAccountName=" + user.GetAccountName() + "," + container + "," + suffix, null, attributes);
			            results.add(result);
			            resultCount++;
		            }
		        }
		        
		        NTGroupList groups = new NTGroupList();
		        if(groups.loadList() != 0) {
		            throw new NamingException("Failed to load group list");
		        }
		        while(groups.hasMore()) {
		            attributes = new BasicAttributes();
		            
		            rdn = groups.nextGroupName();
		            if(!rdn.endsWith("$")) {
		            	group.RetriveGroupByAccountName(rdn);
		            	attributes = getNTGroupAttributes(group, rdn);
		            	result = new SearchResult("sAMAccountName=" + group.GetAccountName() + "," + container + "," + suffix, null, attributes);
		            	results.add(result);
		            	resultCount++;
		            }
		        }
		        
		        NTLocalGroupList localGroups = new NTLocalGroupList();
		        if(localGroups.loadList() != 0) {
		            throw new NamingException("Failed to load local group list");
		        }
		        while(localGroups.hasMore()) {
		            attributes = new BasicAttributes();
		            
		            if(!rdn.endsWith("$")) {
			            rdn = localGroups.nextLocalGroupName();
			            localGroup.RetriveLocalGroupByAccountName(rdn);
			            attributes = getNTLocalGroupAttributes(localGroup, rdn);
			            result = new SearchResult("sAMAccountName=" + localGroup.GetAccountName() + "," + container + "," + suffix, null, attributes);
			            results.add(result);
			            resultCount++;
		            }
		        }
        	}
        	else {
        		throw new NamingException("Unsupported search filter: " + filter);
        	}
        }
        else {
        	throw new NamingException("Bad base DN: " + base);
        }
        
        return resultCount;
    }
    
    private BasicAttributes getNTUserAttributes(NTUser user, String username) throws NamingException {
    	int result = 0;
        BasicAttributes attributes = new BasicAttributes();
        BasicAttribute attribute;
        String tempName;
        
        attribute = new BasicAttribute("objectClass");
        attribute.add("top");
        attribute.add("person");
        attribute.add("organizationalPerson");
        attribute.add("user");
        attributes.put(attribute);
        
        attribute = new BasicAttribute("objectGUID");
        attribute.add(user.GetSIDHexStr());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("objectSid");
        attribute.add(user.GetSIDHexStr());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("accountExpires");
        attribute.add(new Long(user.GetAccountExpires()).toString());
        attributes.put(attribute);

        attribute = new BasicAttribute("badPwdCount");
        attribute.add(new Long(user.GetBadPasswordCount()).toString());
        attributes.put(attribute);

        attribute = new BasicAttribute("codePage");
        attribute.add(new Long(user.GetCodePage()).toString());
        attributes.put(attribute);

        if(!user.GetComment().equals("")) {
        	attribute = new BasicAttribute("description");
        	attribute.add(user.GetComment());
        	attributes.put(attribute);
        }

        attribute = new BasicAttribute("countryCode");
        attribute.add(new Long(user.GetCountryCode()).toString());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("userAccountControl");
        attribute.add(new Long(user.GetFlags()).toString());
        attributes.put(attribute);
        
        if(!user.GetHomeDir().equals("")) {
        	attribute = new BasicAttribute("homeDirectory");
        	attribute.add(user.GetHomeDir());
        	attributes.put(attribute);
        }
        
        if(!user.GetHomeDirDrive().equals("")) {
        	attribute = new BasicAttribute("homeDrive");
        	attribute.add(user.GetHomeDirDrive());
        	attributes.put(attribute);
        }

        attribute = new BasicAttribute("lastLogoff");
        attribute.add(new Long(user.GetLastLogoff()).toString());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("lastLogon");
        attribute.add(new Long(user.GetLastLogon()).toString());
        attributes.put(attribute);

        attribute = new BasicAttribute("logonHours");
        attribute.add(HexStringToByteArray(user.GetLogonHours()));
        attributes.put(attribute);

        attribute = new BasicAttribute("maxStorage");
        attribute.add(new Long(user.GetMaxStorage()).toString());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("logonCount");
        attribute.add(new Long(user.GetNumLogons()).toString());
        attributes.put(attribute);
        
        if(!user.GetProfile().equals("")) {
        	attribute = new BasicAttribute("profilePath");
        	attribute.add(user.GetProfile());
        	attributes.put(attribute);
        }
        
        if(!user.GetScriptPath().equals("")) {
        	attribute = new BasicAttribute("scriptPath");
        	attribute.add(user.GetScriptPath());
        	attributes.put(attribute);
        }
        
        attribute = new BasicAttribute("sAMAccountName");
        attribute.add(username);
        attributes.put(attribute);

        if(!user.GetWorkstations().equals("")) {
        	attribute = new BasicAttribute("userWorkstations");
        	attribute.add(user.GetWorkstations());
        	attributes.put(attribute);
        }
        
        if(!user.GetFullname().equals("")) {
        	attribute = new BasicAttribute("cn");
        	attribute.add(user.GetFullname());
        	attributes.put(attribute);
        }
        
        if(!user.GetFullname().equals("")) {
        	attribute = new BasicAttribute("name");
        	attribute.add(user.GetFullname());
        	attributes.put(attribute);
        }
        
        attribute = new BasicAttribute("memberOf");
        result = user.LoadGroups();
        if(result != 0) {
        	throw new NamingException("Could not load groups: " + result);
        }
        while(user.HasMoreGroups()) {
        	tempName = user.NextGroupName();
        	if(!tempName.endsWith("$")) {
        		attribute.add("sAMAccountName=" + tempName + "," + container + "," + suffix);
        	}
        }
        result = user.LoadLocalGroups();
        if(result != 0) {
        	throw new NamingException("Could not load local groups: " + result);
        }
        while(user.HasMoreLocalGroups()) {
        	tempName = user.NextLocalGroupName();
        	if(!tempName.endsWith("$")) {
        		attribute.add("sAMAccountName=" + tempName + "," + container + "," + suffix);
        	}
        }
        attributes.put(attribute);
        
        return attributes;
    }
    
    private BasicAttributes getNTGroupAttributes(NTGroup group, String groupName) throws NamingException {
        BasicAttributes attributes = new BasicAttributes();
        BasicAttribute attribute;
        String tempName;
        int result = 0;
        
        attribute = new BasicAttribute("objectClass");
        attribute.add("top");
        attribute.add("group");
        attributes.put(attribute);
        
        attribute = new BasicAttribute("objectGUID");
        attribute.add(group.GetSIDHexStr());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("objectSid");
        attribute.add(group.GetSIDHexStr());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("name");
        attribute.add(groupName);
        attributes.put(attribute);
        
        attribute = new BasicAttribute("sAMAccountName");
        attribute.add(groupName);
        attributes.put(attribute);
        
        attribute = new BasicAttribute("cn");
        attribute.add(groupName);
        attributes.put(attribute);
        
        attribute = new BasicAttribute("groupType");
        attribute.add(new Long(GLOBAL_FLAG).toString());
        attributes.put(attribute);

        if(!group.GetComment().equals("")) {
        	attribute = new BasicAttribute("description");
        	attribute.add(group.GetComment());
        	attributes.put(attribute);
        }
        
        attribute = new BasicAttribute("member");
        result = group.LoadUsers();
        if(result != 0) {
        	throw new NamingException("Could not load users: " + result);
        }
        while(group.HasMoreUsers()) {
        	tempName = group.NextUserName();
        	// members that end with '$' are supposed to be hidden
        	if(!tempName.endsWith("$")) {
        		attribute.add("sAMAccountName=" + tempName + "," + container + "," + suffix);
        	}
        }
        attributes.put(attribute);
        
        return attributes;
    }
    
    private BasicAttributes getNTLocalGroupAttributes(NTLocalGroup localGroup, String localGroupName) throws NamingException {
        BasicAttributes attributes = new BasicAttributes();
        BasicAttribute attribute;
        String tempName;
        int result = 0;
        
        attribute = new BasicAttribute("objectClass");
        attribute.add("top");
        attribute.add("group");
        attributes.put(attribute);
        
        attribute = new BasicAttribute("objectGUID");
        attribute.add(localGroup.GetSIDHexStr());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("objectSid");
        attribute.add(localGroup.GetSIDHexStr());
        attributes.put(attribute);
        
        attribute = new BasicAttribute("name");
        attribute.add(localGroupName);
        attributes.put(attribute);
        
        attribute = new BasicAttribute("sAMAccountName");
        attribute.add(localGroupName);
        attributes.put(attribute);
        
        attribute = new BasicAttribute("cn");
        attribute.add(localGroupName);
        attributes.put(attribute);
        
        attribute = new BasicAttribute("groupType");
        attribute.add(new Long(DOMAINLOCAL_FLAG).toString());
        attributes.put(attribute);
        
        if(!localGroup.GetComment().equals("")) {
        	attribute = new BasicAttribute("description");
        	attribute.add(localGroup.GetComment());
        	attributes.put(attribute);
        }
        
        attribute = new BasicAttribute("member");
        result = localGroup.LoadUsers();
        if(result != 0) {
        	throw new NamingException("Could not load users: " + result);
        }
        while(localGroup.HasMoreUsers()) {
        	tempName = localGroup.NextUserName();
        	// members that end with '$' are supposed to be hidden
        	if(!tempName.endsWith("$")) {
        		attribute.add("sAMAccountName=" + tempName + "," + container + "," + suffix);
        	}
        }
        attributes.put(attribute);
        
        return attributes;
    }
    
    private void modNTUserAttributes(NTUser user, ModificationItem[] mods) throws NamingException {
        for(int i = 0; i < mods.length; i++) {
        	
        	if(mods[i].getAttribute().getID().compareToIgnoreCase("accountExpires") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetAccountExpires(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetAccountExpires(new Long(-1).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetAccountExpires(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("codePage") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetCodePage(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetCodePage(new Long(0).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetCodePage(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("description") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetComment((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetComment("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetComment((String)mods[i].getAttribute().get());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("countryCode") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetCountryCode(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetCountryCode(new Long(0).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetCountryCode(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("userAccountControl") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetFlags(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetFlags(new Long(1).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetFlags(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("homeDirectory") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetHomeDir((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetHomeDir("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetHomeDir((String)mods[i].getAttribute().get());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("homeDrive") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetHomeDirDrive((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetHomeDirDrive("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetHomeDirDrive((String)mods[i].getAttribute().get());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("logonHours") == 0) {        		
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetLogonHours(ByteArrayToHexString((byte[])mods[i].getAttribute().get()));
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetLogonHours("ffffffffffffffffffffffffffffffffffffffffff");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetLogonHours(ByteArrayToHexString((byte[])mods[i].getAttribute().get()));
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("maxStorage") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetMaxStorage(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetMaxStorage(new Long(-1).longValue());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetMaxStorage(new Long((String)mods[i].getAttribute().get()).longValue());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("profilePath") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetProfile((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetProfile("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetProfile((String)mods[i].getAttribute().get());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("scriptPath") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetScriptPath((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetScriptPath((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetScriptPath((String)mods[i].getAttribute().get());
        		}
        	}
        	else if(mods[i].getAttribute().getID().compareToIgnoreCase("userWorkstations") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetWorkstations((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetWorkstations("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetWorkstations((String)mods[i].getAttribute().get());
        		}
        	}
            else if(mods[i].getAttribute().getID().compareToIgnoreCase("cn") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetFullname((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetFullname("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetFullname((String)mods[i].getAttribute().get());
        		}
            }
            else if(mods[i].getAttribute().getID().compareToIgnoreCase("name") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetFullname((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			user.SetFullname("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetFullname((String)mods[i].getAttribute().get());
        		}
            }
            else if(mods[i].getAttribute().getID().compareToIgnoreCase("unicodePwd") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			user.SetPassword((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			// Do nothing
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			user.SetPassword((String)mods[i].getAttribute().get());
        		}
            }
            else if(mods[i].getAttribute().getID().compareToIgnoreCase("memberOf") == 0) {
            	String tempName;
            	
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			for(int j = 0; j < mods[i].getAttribute().size(); j++) {
        				tempName = getRDN((String)mods[i].getAttribute().get(j));
        				user.AddToGroup(tempName);
        				user.AddToLocalGroup(tempName);
            		}
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			for(int j = 0; j < mods[i].getAttribute().size(); j++) {
	    				tempName = getRDN((String)mods[i].getAttribute().get(j));
	        			user.RemoveFromGroup(tempName);
	        			user.RemoveFromLocalGroup(tempName);
        			}
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			HashSet groups = new HashSet();
        			Object[] deletedGroups;
        			
        			user.LoadGroups();
        			while(user.HasMoreGroups()) {
        				tempName = user.NextGroupName();
        				if(!tempName.endsWith("$")) {
        					groups.add(tempName);
        				}
        			}
        			
        			user.LoadLocalGroups();
        			while(user.HasMoreLocalGroups()) {
        				tempName = user.NextLocalGroupName();
        				if(!tempName.endsWith("$")) {
        					groups.add(tempName);
        				}
        			}
        			
        			for(int j = 0; j < mods[i].getAttribute().size(); j++) {
        				tempName = getRDN((String)mods[i].getAttribute().get(j));
        				if(groups.contains(tempName)) {
        					groups.remove(tempName);
        				}
        				else {
        					user.AddToGroup(tempName);
            				user.AddToLocalGroup(tempName);
        				}
            		}
        			
        			deletedGroups = groups.toArray();
        			for(int j = 0; j < deletedGroups.length; j++) {
        				user.RemoveFromGroup((String)deletedGroups[j]);
        				user.RemoveFromLocalGroup((String)deletedGroups[j]);
        			}
        		}
            }
        }
    }
    
    private void modNTGroupAttributes(NTGroup group, ModificationItem[] mods) throws NamingException {
    	for(int i = 0; i < mods.length; i++) {
        	if(mods[i].getAttribute().getID().compareToIgnoreCase("description") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			group.SetComment((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			group.SetComment("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			group.SetComment((String)mods[i].getAttribute().get());
        		}
        	}
	    	else if(mods[i].getAttribute().getID().compareToIgnoreCase("member") == 0) {	
	    		String tempName;
	    		
	    		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
	    			for(int j = 0; j < mods[i].getAttribute().size(); j++) {
	    				tempName = getRDN((String)mods[i].getAttribute().get(j));
	    				group.AddUser(tempName);
	        		}
	    		}
	    		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
	    			tempName = (String)mods[i].getAttribute().get();
					if(tempName != null) {
	    			  tempName = getRDN((String)mods[i].getAttribute().get());
	    			  group.RemoveUser(tempName);
					}
					else {
						group.LoadUsers();
						while(group.HasMoreUsers()) {
		    				tempName = group.NextUserName();
							if(!tempName.endsWith("$")) {
								group.RemoveUser(tempName);
							}
		    			}
					}
	    		}
	    		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
	    			HashSet users = new HashSet();
	    			Object[] deletedUsers;
		    		
	    			group.LoadUsers();
	    			while(group.HasMoreUsers()) {
	    				tempName = group.NextUserName();
						if(!tempName.endsWith("$")) {
							users.add(tempName);
						}
	    			}
	    			
	    			for(int j = 0; j < mods[i].getAttribute().size(); j++) {
	    				tempName = getRDN((String)mods[i].getAttribute().get(j));
	    				if(users.contains(tempName)) {
	    					users.remove(tempName);
	    				}
	    				else {
	    					group.AddUser(tempName);
	    				}
	        		}
	    			
	    			deletedUsers = users.toArray();
	    			for(int j = 0; j < deletedUsers.length; j++) {
        				group.RemoveUser((String)deletedUsers[j]);
        			}
	    		}
	        }
    	}
    }
    
    private void modNTLocalGroupAttributes(NTLocalGroup localGroup, ModificationItem[] mods) throws NamingException {
    	for(int i = 0; i < mods.length; i++) {
        	if(mods[i].getAttribute().getID().compareToIgnoreCase("description") == 0) {
        		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
        			localGroup.SetComment((String)mods[i].getAttribute().get());
        		}
        		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
        			localGroup.SetComment("");
        		}
        		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
        			localGroup.SetComment((String)mods[i].getAttribute().get());
        		}
        	}
    		else if(mods[i].getAttribute().getID().compareToIgnoreCase("member") == 0) {	
	    		String tempName;
	    		
	    		if(mods[i].getModificationOp() == DirContext.ADD_ATTRIBUTE) {
	    			for(int j = 0; j < mods[i].getAttribute().size(); j++) {
	    				tempName = getRDN((String)mods[i].getAttribute().get(j));
	    				localGroup.AddUser(tempName);
	        		}
	    		}
	    		else if(mods[i].getModificationOp() == DirContext.REMOVE_ATTRIBUTE) {
	    			tempName = (String)mods[i].getAttribute().get();
					if(tempName != null) {
	    			  tempName = getRDN((String)mods[i].getAttribute().get());
	    			  localGroup.RemoveUser(tempName);
					}
					else {
						localGroup.LoadUsers();
						while(localGroup.HasMoreUsers()) {
		    				tempName = localGroup.NextUserName();
							if(!tempName.endsWith("$")) {
								localGroup.RemoveUser(tempName);
							}
		    			}
					}
	    		}
	    		else if(mods[i].getModificationOp() == DirContext.REPLACE_ATTRIBUTE) {
	    			HashSet users = new HashSet();
	    			Object[] deletedUsers;
		    		
	    			localGroup.LoadUsers();
	    			while(localGroup.HasMoreUsers()) {
	    				tempName = localGroup.NextUserName();
						if(!tempName.endsWith("$")) {
							users.add(tempName);
						}
	    			}
	    			
	    			for(int j = 0; j < mods[i].getAttribute().size(); j++) {
	    				tempName = getRDN((String)mods[i].getAttribute().get(j));
	    				if(users.contains(tempName)) {
	    					users.remove(tempName);
	    				}
	    				else {
	    					localGroup.AddUser(tempName);
	    				}
	        		}
	    			
	    			deletedUsers = users.toArray();
	    			for(int j = 0; j < deletedUsers.length; j++) {
        				localGroup.RemoveUser((String)deletedUsers[j]);
        			}
	    		}
	        }
    	}
    }
}
