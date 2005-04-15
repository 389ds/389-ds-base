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
 * do so, delete this exception statement from your version. 
 * 
 * 
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * --- END COPYRIGHT BLOCK --- */
package com.netscape.dsml.gateway;

/**
 * Constants used in the code.
 */
public class Constants
{
    public static final String DSML20_URN       =  "urn:oasis:dsml:names:tc:DSML:2:0:core";
    public static final String BATCH_REQUEST    =  "batchRequest";
    public static final String BATCH_RESPONSE   =  "batchResponse";

    public static final String DSML_REQUEST     =  "DSMLRequest";
    public static final String DSML_RESPONSE    =  "DSMLResponse";

    public static final String AUTH_REQUEST     =  "authRequest";
    public static final String SEARCH_REQUEST   =  "searchRequest";
    public static final String MODIFY_REQUEST   =  "modifyRequest";
    public static final String ADD_REQUEST      =  "addRequest";
    public static final String DEL_REQUEST      =  "delRequest";
    public static final String MODDN_REQUEST    =  "modDNRequest";
    public static final String COMPARE_REQUEST  =  "compareRequest";
    public static final String ABANDON_REQUEST  =  "abandonRequest";
    public static final String EXTENDED_REQ     =  "extendedRequest";
    

    public static final String AUTH_RESPONSE    =  "authResponse";
    public static final String SEARCH_RES_ENTRY =  "searchResultEntry";
    public static final String SEARCH_RES_REF   =  "searchResultReference";
    public static final String SEARCH_RES_DONE  =  "searchResultDone";
    public static final String MODIFY_RESPONSE  =  "modifyResponse";
    public static final String ADD_RESPONSE     =  "addResponse";
    public static final String DEL_RESPONSE     =  "delResponse";
    public static final String MODDN_RESPONSE   =  "modDNResponse";
    public static final String COMPARE_RESPONSE =  "compareResponse";
    public static final String ABANDON_RESPONSE =  "abandonResponse";
    public static final String EXTENDED_RESPONSE=  "extendedResponse";
    public static final String ERROR_RESPONSE   =  "errorResponse";
    public static final String SEARCH_RESPONSE  =  "searchResponse";

    public static final String ERROR_TYPE       =  "type";
    public static final String ERROR_MESSAGE    =  "message";

 
    public static final String REQUEST_ID       =  "requestID";
    
    public static final String PROCESSING       =  "processing";
    public static final String SEQUENTIAL       =  "sequential";
    public static final String PARALLEL         =  "parallel";
    
    public static final String RESPONSE_ORDER   =  "responseOrder";
    public static final String UNORDERED        =  "unOrdered";
    
    public static final String ON_ERROR         =  "onError";
    public static final String RESUME           =  "resume";
    public static final String EXIT             =  "exit";
    
 
    public static final String NOT_ATTEMPTED    = "notAttempted";
    public static final String UNKNOWN_REQ      = "Unknown Request";

    public static final String BINDREQ_ERROR_MESSAGE = "Found a BindRequest which is not the first request";
    public static final String BATCH_RESPONSE_START_TAG = "<batchResponse xmlns=\"" + DSML20_URN + "\">";
    public static final String BATCH_RESPONSE_END_TAG   = "</batchResponse>";
    
    public static final String DN                       = "dn";
    //Modify Request
    public static final String ATTR                     = "attr";
    public static final String OPERATION                = "operation";
    public static final String ADD_OPERATION            = "add";
    public static final String DELETE_OPERATION         = "delete";
    public static final String REPLACE_OPERATION        = "replace";
    public static final String NAME                     = "name";
    public static final String VALUE                    = "value";
    public static final String RESULT_CODE              = "resultCode";
    public static final String CODE                     = "code";
    public static final String DESC                     = "desc";

    public static final String FILTER                   = "filter";
    public static final String SUBSTRINGS               = "substrings";
    public static final String EQUALITY_MATCH           = "equalityMatch";
    public static final String GREATER_OR_EQAUAL        = "greaterOrEqual";
    public static final String LESS_OR_EQAUAL           = "lessOrEqual";
    public static final String PRESENT                  = "present";
    public static final String APPROX_MATCH             = "approxMatch";
    public static final String EXTENSIBLE_MATCH         = "extensibleMatch";
 	public static final String MATCHING_RULE            = "matchingRule";
 	public static final String DN_ATTRIBUTES            = "dnAttributes"; 
    public static final String AND                      = "and";
    public static final String OR                       = "or";
    public static final String NOT                      = "not";
    
    public static final String SUBSTRING_INITIAL        = "initial";
    public static final String SUBSTRING_ANY            = "any";
    public static final String SUBSTRING_FINAL          = "final";
    
    public static final String CONTROL                  = "control";
    public static final String CONTROL_TYPE             = "type";
    public static final String CONTROL_CRITICALITY      = "criticality";
    public static final String CONTROL_VALUE            = "controlValue";
 
    public static final String SCOPE                    = "scope";
    public static final String BASE_SCOPE               = "baseObject";
    public static final String SINGLE_LEVEL_SCOPE       = "singleLevel";
    public static final String WHOLE_TREE_SCOPE         = "wholeSubTree";
    

    public static final String DREF_ALIASES             = "derefAliases";
    public static final String NEVER_DREF_ALIASES       = "neverDerefAliases";
    public static final String DEREF_IN_SEARCH          = "derefInSearching";
    public static final String DREF_FIND_BASE_OBJ       = "derefFindingBaseObj";
    public static final String DREF_ALWAYS              = "derefAlways";
    
    public static final String SIZE_LIMIT               = "sizeLimit";
    public static final String TIME_LIMIT               = "timeLimit";
    public static final String TYPES_ONLY               = "typesOnly";
    
    public static final String ATTRIBUTES               = "attributes";
    public static final String ATTRIBUTE                = "attribute";
    
    public static final String ERRORMESSAGE             = "errorMessage";
    public static final String MATCHED_DN               = "matchedDN";
    public static final String ERR_REFERRAL_STR         = "referral";
    public static final String REF                      = "ref";
    public static final String NEWRDN                   = "newrdn";
    public static final String DELETEOLDRDN             = "deleteoldrdn";
    public static final String NEWSUPERIOR              = "newSuperior";
    public static final String ASSERTION                = "assertion";
    public static final String REQUESTNAME              = "requestName";
    public static final String REQUESTVALUE             = "requestValue";
    public static final String DSMLNS	          		= "dsml:";
 
    public static final int BATCH                       = 0;
    public static final int DSML_REQ                    = 1;
    public static final int DEFAULT_PORT                = 389;
}
