/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#include <netsite.h>
#include <libaccess/las.h>
#include <libaccess/acl.h>
#include <libaccess/aclerror.h>
#include <libaccess/dbtlibaccess.h>
#include "aclpriv.h"

NSAPI_PUBLIC int ACL_ModuleRegister (NSErr_t *errp, const char *module_name,
				     AclModuleInitFunc func)
{
    int rv;

    if (!module_name || !*module_name) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4200, ACL_Program, 1,
	              XP_GetAdminStr(DBT_ModuleRegisterModuleNameMissing));
	return -1;
    }

    rv = (*func)(errp);

    if (rv < 0) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4210, ACL_Program, 2,
	              XP_GetAdminStr(DBT_ModuleRegisterFailed), module_name);
	return rv;
    }

    return 0;
}


static int attr_getter_is_matching(NSErr_t *errp, ACLAttrGetter_t *getter,
				   ACLMethod_t method, ACLDbType_t dbtype)
{
    if ((ACL_MethodIsEqual(errp, getter->method, method) ||
	 ACL_MethodIsEqual(errp, getter->method, ACL_METHOD_ANY)) &&
	(ACL_DbTypeIsEqual(errp, getter->dbtype, dbtype) ||
	 ACL_DbTypeIsEqual(errp, getter->dbtype, ACL_DBTYPE_ANY)))
    {
	return 1;
    }
    else {
	return 0;
    }
}
				   

NSAPI_PUBLIC int ACL_GetAttribute(NSErr_t *errp, const char *attr, void **val,
		     		  PList_t subject, PList_t resource, 
				  PList_t auth_info, PList_t global_auth) 
{ 
    int rv; 
    void *attrval;
    ACLAttrGetterFn_t func;
    ACLAttrGetterList_t getters;
    ACLAttrGetter_t *getter;
    ACLMethod_t method;
    ACLDbType_t dbtype;

    /* If subject PList is NULL, we will fail anyway */
    if (!subject) return LAS_EVAL_FAIL;

    /* Is the attribute already present in the subject property list? */

    rv = PListFindValue(subject, attr, &attrval, NULL);
    if (rv >= 0) {

        /* Yes, take it from there */
	*val = attrval;
	return LAS_EVAL_TRUE;
    }

    /* Get the authentication method and database type */

    rv = ACL_AuthInfoGetMethod(errp, auth_info, &method);

    if (rv < 0) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4300, ACL_Program, 2,
            XP_GetAdminStr(DBT_GetAttributeCouldntDetermineMethod), attr);
        return LAS_EVAL_FAIL;
    }

    rv = ACL_AuthInfoGetDbType (errp, auth_info, &dbtype);

    if (rv < 0) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4380, ACL_Program, 2,
            XP_GetAdminStr(DBT_ReadDbMapFileCouldntDetermineDbtype), attr);
        return LAS_EVAL_FAIL;
    }

    /* Get the list of attribute getters */

    rv = ACL_AttrGetterFind(errp, attr, &getters);

    if ((rv < 0) || (getters == 0)) {
	nserrGenerate(errp, ACLERRFAIL, ACLERR4310, ACL_Program, 2,
                      XP_GetAdminStr(DBT_GetAttributeCouldntLocateGetter), attr);
        return LAS_EVAL_FAIL;
    }

    /* Iterate over each getter and see if it should be called
     * Call each matching getter until a getter which doesn't decline is
     * found.
     */

    for (getter = ACL_AttrGetterFirst(&getters);
         getter != 0;
         getter = ACL_AttrGetterNext(&getters, getter)) {

        /* Require matching method and database type */

        if (attr_getter_is_matching(errp, getter, method, dbtype)) {

            /* Call the getter function */
            func = getter->fn;
            rv = (*func)(errp, subject, resource, auth_info, global_auth,
                         getter->arg);

            /* Did the getter succeed? */
            if (rv == LAS_EVAL_TRUE) {

                /*
                 * Yes, it should leave the attribute on the subject
                 * property list.
                 */
                rv = PListFindValue(subject, attr, (void **)&attrval, NULL);
	    
                if (rv < 0) {
	            nserrGenerate(errp, ACLERRFAIL, ACLERR4320, ACL_Program, 2,
                                  XP_GetAdminStr(DBT_GetAttributeDidntSetAttr), attr);
                    return LAS_EVAL_FAIL;
                }

                /* Got it */
                *val = attrval;
                return LAS_EVAL_TRUE;
            }

            /* Did the getter decline? */
            if (rv != LAS_EVAL_DECLINE) {

                /* No, did it fail to get the attribute */
                if (rv == LAS_EVAL_FAIL || rv == LAS_EVAL_INVALID) {
	            nserrGenerate(errp, ACLERRFAIL, ACLERR4330, ACL_Program, 2,
                                  XP_GetAdminStr(DBT_GetAttributeDidntGetAttr), attr);
                }

                return rv;
            }
        }
    }

    /* If we fall out of the loop, all the getters declined */
    nserrGenerate(errp, ACLERRFAIL, ACLERR4340, ACL_Program, 2,
		  XP_GetAdminStr(DBT_GetAttributeAllGettersDeclined), attr);
    return LAS_EVAL_FAIL;
}

