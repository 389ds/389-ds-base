/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef PUBLIC_NSACL_ACLAPI_H
#define PUBLIC_NSACL_ACLAPI_H

/*
 * File:        aclapi.h
 *
 * Description:
 *
 *      This file defines the functions available in the ACL API.
 */

#ifndef PUBLIC_NSACL_NSERRDEF_H
#include "nserrdef.h"
#endif /* !PUBLIC_NSACL_NSERRDEF_H */

#ifndef PUBLIC_NSAPI_H
#include "public/nsapi.h"
#endif /* !PUBLIC_NSAPI_H */

#ifndef PUBLIC_NSACL_PLISTDEF_H
#include "plistdef.h"
#endif /* !PUBLIC_NSACL_PLISTDEF_H */

#ifndef PUBLIC_NSACL_ACLDEF_H
#include "acldef.h"
#endif /* !PUBLIC_NSACL_ACLDEF_H */

NSPR_BEGIN_EXTERN_C

typedef struct ACLDispatchVector ACLDispatchVector_t;
struct ACLDispatchVector {

    /* Error frame stack support */

    void (*f_nserrDispose)(NSErr_t * errp);
    NSEFrame_t *(*f_nserrFAlloc)(NSErr_t * errp);
    void (*f_nserrFFree)(NSErr_t * errp, NSEFrame_t * efp);
    NSEFrame_t *(*f_nserrGenerate)(NSErr_t * errp, long retcode,
                                   long errorid, char * program,
                                   int errc, ...);

    /* Property list support 
     * The Property List facility makes extensive use of pointers to 
     * opaque structures.  As such, PLists cannot be marshalled.  WAI-style
     * ACL APIs in future releases will therefore not be using PLists.
     * However the C API documented here may continue to be supported
     * in future releases.
     */

    int (*f_PListAssignValue)(PList_t plist, const char *pname,
                              const void *pvalue, PList_t ptype);
    PList_t (*f_PListCreate)(pool_handle_t *mempool,
                             int resvprop, int maxprop, int flags);
    int (*f_PListDefProp)(PList_t plist, int pindex, 
                          const char *pname, const int flags);
    const void * (*f_PListDeleteProp)(PList_t plist, int pindex,
                                      const char *pname);
    int (*f_PListFindValue)(PList_t plist,
                            const char *pname, void **pvalue, PList_t *type);
    int (*f_PListInitProp)(PList_t plist, int pindex, const char *pname,
                           const void *pvalue, PList_t ptype);
    PList_t (*f_PListNew)(pool_handle_t *mempool);
    void (*f_PListDestroy)(PList_t plist);
    int (*f_PListGetValue)(PList_t plist,
                           int pindex, void **pvalue, PList_t *type);
    int (*f_PListNameProp)(PList_t plist, int pindex, const char *pname);
    int (*f_PListSetType)(PList_t plist, int pindex, PList_t type);
    int (*f_PListSetValue)(PList_t plist,
                           int pindex, const void *pvalue, PList_t type);
    void (*f_PListEnumerate)(PList_t plist, PListFunc_t *user_func, 
                             void *user_data);
    PList_t (*f_PListDuplicate)(PList_t plist,
                                pool_handle_t *new_mempool, int flags);
    pool_handle_t *(*f_PListGetPool)(PList_t plist);

    /* ACL attribute handling */

    int (*f_ACL_LasRegister)(NSErr_t *errp, char *attr_name,
                             LASEvalFunc_t eval_func,
                             LASFlushFunc_t flush_func);

    /* method/dbtype registration routines */

    int (*f_ACL_MethodRegister)(NSErr_t *errp, const char *name,
                                ACLMethod_t *t);
    int (*f_ACL_MethodIsEqual)(NSErr_t *errp,
                               const ACLMethod_t t1, const ACLMethod_t t2);
    int (*f_ACL_MethodNameIsEqual)(NSErr_t *errp,
                                   const ACLMethod_t t, const char *name);
    int (*f_ACL_MethodFind)(NSErr_t *errp, const char *name, ACLMethod_t *t);
    ACLMethod_t (*f_ACL_MethodGetDefault)(NSErr_t *errp);
    int (*f_ACL_MethodSetDefault)(NSErr_t *errp, const ACLMethod_t t);
    int (*f_ACL_AuthInfoGetMethod)(NSErr_t *errp,
                                   PList_t auth_info, ACLMethod_t *t);

    int (*f_ACL_DbTypeRegister)(NSErr_t *errp, const char *name,
                                DbParseFn_t func, ACLDbType_t *t);
    int (*f_ACL_DbTypeIsEqual)(NSErr_t *errp,
                               const ACLDbType_t t1, const ACLDbType_t t2);
    int (*f_ACL_DbTypeNameIsEqual)(NSErr_t * errp,
                                   const ACLDbType_t t, const char *name);
    int (*f_ACL_DbTypeFind)(NSErr_t *errp, const char *name, ACLDbType_t *t);
    ACLDbType_t (*f_ACL_DbTypeGetDefault)(NSErr_t *errp);
    int (*f_ACL_AuthInfoGetDbType)(NSErr_t *errp,
                                   PList_t auth_info, ACLDbType_t *t);
    int (*f_ACL_DbTypeIsRegistered)(NSErr_t *errp, const ACLDbType_t dbtype);
    DbParseFn_t (*f_ACL_DbTypeParseFn)(NSErr_t *errp,
                                       const ACLDbType_t dbtype);

    int (*f_ACL_AttrGetterRegister)(NSErr_t *errp,
                                    const char *attr, ACLAttrGetterFn_t fn,
                                    ACLMethod_t m, ACLDbType_t d,
                                    int position, void *arg);

    int (*f_ACL_ModuleRegister)(NSErr_t *errp, const char *moduleName,
                                AclModuleInitFunc func);
    int (*f_ACL_GetAttribute)(NSErr_t *errp, const char *attr, void **val,
                              PList_t subject, PList_t resource,
                              PList_t auth_info, PList_t global_auth);
    int (*f_ACL_DatabaseRegister)(NSErr_t *errp, ACLDbType_t dbtype,
                                const char *dbname, const char *url,
                                PList_t plist);
    int (*f_ACL_DatabaseFind)(NSErr_t *errp, const char *dbname,
                              ACLDbType_t *dbtype, void **db);
    int (*f_ACL_DatabaseSetDefault)(NSErr_t *errp, const char *dbname);
    int (*f_ACL_LDAPDatabaseHandle )(NSErr_t *errp, const char *dbname,
                                     LDAP **ld, char **basedn);
    int (*f_ACL_AuthInfoGetDbname)(PList_t auth_info, char **dbname);
    int (*f_ACL_CacheFlushRegister)(AclCacheFlushFunc_t func);
    int (*f_ACL_CacheFlush)(void);

    /*  ACL language and file interfaces */

    ACLListHandle_t * (*f_ACL_ParseFile)(NSErr_t *errp, char *filename);
    ACLListHandle_t * (*f_ACL_ParseString)(NSErr_t *errp, char *buffer);
    int (*f_ACL_WriteString)(NSErr_t *errp, char **acl,
                             ACLListHandle_t *acllist);
    int (*f_ACL_WriteFile)(NSErr_t *errp, char *filename,
                           ACLListHandle_t *acllist);
    int (*f_ACL_FileRenameAcl)(NSErr_t *errp, char *filename,
                               char *acl_name, char *new_acl_name, int flags);
    int (*f_ACL_FileDeleteAcl)(NSErr_t *errp, char *filename,
                               char *acl_name, int flags);
    int (*f_ACL_FileGetAcl)(NSErr_t *errp, char *filename,
                            char *acl_name, char **acl_text, int flags);
    int (*f_ACL_FileSetAcl)(NSErr_t *errp, char *filename,
                            char *acl_text, int flags);

    /*  ACL Expression construction interfaces  
     *  These are low-level interfaces that may be useful to those who are not
     *  using the ONE ACL syntax, but want to use the ONE ACL evaluation
     *  routines.  By their low-level nature, future support of these APIs
     *  cannot be guaranteed.  Use ACL_ParseFile and ACL_ParseString wherever
     *  possible.
     */

    ACLExprHandle_t *(*f_ACL_ExprNew)(const ACLExprType_t expr_type);
    void (*f_ACL_ExprDestroy)(ACLExprHandle_t *expr);
    int (*f_ACL_ExprSetPFlags)(NSErr_t *errp,
                               ACLExprHandle_t *expr, PFlags_t flags);
    int (*f_ACL_ExprClearPFlags)(NSErr_t *errp, ACLExprHandle_t *expr);
    int (*f_ACL_ExprTerm)(NSErr_t *errp, ACLExprHandle_t *acl_expr,
                          char *attr_name, CmpOp_t cmp, char *attr_pattern);
    int (*f_ACL_ExprNot)(NSErr_t *errp, ACLExprHandle_t *acl_expr);
    int (*f_ACL_ExprAnd)(NSErr_t *errp, ACLExprHandle_t *acl_expr);
    int (*f_ACL_ExprOr)(NSErr_t *errp, ACLExprHandle_t *acl_expr);
    int (*f_ACL_ExprAddAuthInfo)(ACLExprHandle_t *expr, PList_t auth_info);
    int (*f_ACL_ExprAddArg)(NSErr_t *errp, ACLExprHandle_t *expr, char *arg);
    int (*f_ACL_ExprSetDenyWith)(NSErr_t *errp, ACLExprHandle_t *expr,
                                 char *deny_type, char *deny_response);
    int (*f_ACL_ExprGetDenyWith)(NSErr_t *errp, ACLExprHandle_t *expr,
                                 char **deny_type, char **deny_response);
    int (*f_ACL_ExprAppend)(NSErr_t *errp,
                            ACLHandle_t *acl, ACLExprHandle_t *expr);

    /* ACL manipulation */

    ACLHandle_t * (*f_ACL_AclNew)(NSErr_t *errp, char *tag);
    void (*f_ACL_AclDestroy)(NSErr_t *errp, ACLHandle_t *acl);

    /* ACL list manipulation */

    ACLListHandle_t * (*f_ACL_ListNew)(NSErr_t *errp);
    int (*f_ACL_ListConcat)(NSErr_t *errp, ACLListHandle_t *acl_list1,
                            ACLListHandle_t *acl_list2, int flags);
    int (*f_ACL_ListAppend)(NSErr_t *errp, ACLListHandle_t *acllist,
                            ACLHandle_t *acl, int flags);
    void (*f_ACL_ListDestroy)(NSErr_t *errp, ACLListHandle_t *acllist);
    ACLHandle_t * (*f_ACL_ListFind)(NSErr_t *errp, ACLListHandle_t *acllist,
                                    char *aclname, int flags);
    int (*f_ACL_ListAclDelete)(NSErr_t *errp, ACLListHandle_t *acl_list,
                           char *acl_name, int flags);
    int (*f_ACL_ListGetNameList)(NSErr_t *errp, ACLListHandle_t *acl_list,
                                 char ***name_list);
    int (*f_ACL_NameListDestroy)(NSErr_t *errp, char **name_list);

    /* ACL evaluation */

    int (*f_ACL_EvalTestRights)(NSErr_t *errp, ACLEvalHandle_t *acleval,
                                char **rights, char **map_generic,
                                char **deny_type, char **deny_response,
                                char **acl_tag, int *expr_num);
    ACLEvalHandle_t * (*f_ACL_EvalNew)(NSErr_t *errp, pool_handle_t *pool);
    void (*f_ACL_EvalDestroy)(NSErr_t *errp,
                              pool_handle_t *pool, ACLEvalHandle_t *acleval);
    int (*f_ACL_EvalSetACL)(NSErr_t *errp, ACLEvalHandle_t *acleval,
                            ACLListHandle_t *acllist);
    PList_t (*f_ACL_EvalGetSubject)(NSErr_t *errp, ACLEvalHandle_t *acleval);
    int (*f_ACL_EvalSetSubject)(NSErr_t *errp,
                                ACLEvalHandle_t *acleval, PList_t subject);
    PList_t (*f_ACL_EvalGetResource)(NSErr_t *errp, ACLEvalHandle_t *acleval);
    int (*f_ACL_EvalSetResource)(NSErr_t *errp,
                                 ACLEvalHandle_t *acleval, PList_t resource);

    /* Access to critical section for ACL cache */

    void (*f_ACL_CritEnter)(void);
    void (*f_ACL_CritExit)(void);

    /* Miscellaneous functions */
    const char * (*f_ACL_AclGetTag)(ACLHandle_t *acl);
    ACLHandle_t * (*f_ACL_ListGetFirst)(ACLListHandle_t *acl_list,
                                        ACLListEnum_t *acl_enum);
    ACLHandle_t * (*f_ACL_ListGetNext)(ACLListHandle_t *acl_list,
                                       ACLListEnum_t *acl_enum);

    /* Functions added after ES 3.0 release */
    const char * (*f_ACL_DatabaseGetDefault)(NSErr_t *errp);
    int (*f_ACL_SetDefaultResult)(NSErr_t *errp, ACLEvalHandle_t *acleval,
				  int result);
    int (*f_ACL_GetDefaultResult)(ACLEvalHandle_t *acleval);
};

#ifdef XP_WIN32

#ifdef INTNSACL
NSAPI_PUBLIC extern ACLDispatchVector_t *__nsacl_table;
#else
__declspec(dllimport) ACLDispatchVector_t *__nsacl_table;
#endif /* INTNSACL */

#else /* !XP_WIN32 */

NSAPI_PUBLIC extern ACLDispatchVector_t *__nsacl_table;

#endif /* XP_WIN32 */

#ifndef INTNSACL

#define nserrDispose (*__nsacl_table->f_nserrDispose)
#define nserrFAlloc (*__nsacl_table->f_nserrFAlloc)
#define nserrFFree (*__nsacl_table->f_nserrFFree)
#define nserrGenerate (*__nsacl_table->f_nserrGenerate)

    /* Property list support 
     * The Property List facility makes extensive use of pointers to 
     * opaque structures.  As such, PLists cannot be marshalled.  WAI-style
     * ACL APIs in future releases will therefore not be using PLists.
     * However the C API documented here may continue to be supported
     * in future releases.
     */

#define PListAssignValue (*__nsacl_table->f_PListAssignValue)
#define PListCreate (*__nsacl_table->f_PListCreate)
#define PListDefProp (*__nsacl_table->f_PListDefProp)
#define PListDeleteProp (*__nsacl_table->f_PListDeleteProp)
#define PListFindValue (*__nsacl_table->f_PListFindValue)
#define PListInitProp (*__nsacl_table->f_PListInitProp)
#define PListNew (*__nsacl_table->f_PListNew)
#define PListDestroy (*__nsacl_table->f_PListDestroy)
#define PListGetValue (*__nsacl_table->f_PListGetValue)
#define PListNameProp (*__nsacl_table->f_PListNameProp)
#define PListSetType (*__nsacl_table->f_PListSetType)
#define PListSetValue (*__nsacl_table->f_PListSetValue)
#define PListEnumerate (*__nsacl_table->f_PListEnumerate)
#define PListDuplicate (*__nsacl_table->f_PListDuplicate)
#define PListGetPool (*__nsacl_table->f_PListGetPool)

    /* ACL attribute handling */

#define ACL_LasRegister (*__nsacl_table->f_ACL_LasRegister)

    /* method/dbtype registration routines */

#define ACL_MethodRegister (*__nsacl_table->f_ACL_MethodRegister)
#define ACL_MethodIsEqual (*__nsacl_table->f_ACL_MethodIsEqual)
#define ACL_MethodNameIsEqual (*__nsacl_table->f_ACL_MethodNameIsEqual)
#define ACL_MethodFind (*__nsacl_table->f_ACL_MethodFind)
#define ACL_MethodGetDefault (*__nsacl_table->f_ACL_MethodGetDefault)
#define ACL_MethodSetDefault (*__nsacl_table->f_ACL_MethodSetDefault)
#define ACL_AuthInfoGetMethod (*__nsacl_table->f_ACL_AuthInfoGetMethod)
#define ACL_DbTypeRegister (*__nsacl_table->f_ACL_DbTypeRegister)
#define ACL_DbTypeIsEqual (*__nsacl_table->f_ACL_DbTypeIsEqual)
#define ACL_DbTypeNameIsEqual (*__nsacl_table->f_ACL_DbTypeNameIsEqual)
#define ACL_DbTypeFind (*__nsacl_table->f_ACL_DbTypeFind)
#define ACL_DbTypeGetDefault (*__nsacl_table->f_ACL_DbTypeGetDefault)
#define ACL_AuthInfoGetDbType (*__nsacl_table->f_ACL_AuthInfoGetDbType)
#define ACL_DbTypeIsRegistered (*__nsacl_table->f_ACL_DbTypeIsRegistered)
#define ACL_DbTypeParseFn (*__nsacl_table->f_ACL_DbTypeParseFn)
#define ACL_AttrGetterRegister (*__nsacl_table->f_ACL_AttrGetterRegister)
#define ACL_ModuleRegister (*__nsacl_table->f_ACL_ModuleRegister)
#define ACL_GetAttribute (*__nsacl_table->f_ACL_GetAttribute)
#define ACL_DatabaseRegister (*__nsacl_table->f_ACL_DatabaseRegister)
#define ACL_DatabaseFind (*__nsacl_table->f_ACL_DatabaseFind)
#define ACL_DatabaseSetDefault (*__nsacl_table->f_ACL_DatabaseSetDefault)
#define ACL_LDAPDatabaseHandle  (*__nsacl_table->f_ACL_LDAPDatabaseHandle)
#define ACL_AuthInfoGetDbname (*__nsacl_table->f_ACL_AuthInfoGetDbname)
#define ACL_CacheFlushRegister (*__nsacl_table->f_ACL_CacheFlushRegister)
#define ACL_CacheFlush (*__nsacl_table->f_ACL_CacheFlush)

    /*  ACL language and file interfaces */

#define ACL_ParseFile (*__nsacl_table->f_ACL_ParseFile)
#define ACL_ParseString (*__nsacl_table->f_ACL_ParseString)
#define ACL_WriteString (*__nsacl_table->f_ACL_WriteString)
#define ACL_WriteFile (*__nsacl_table->f_ACL_WriteFile)
#define ACL_FileRenameAcl (*__nsacl_table->f_ACL_FileRenameAcl)
#define ACL_FileDeleteAcl (*__nsacl_table->f_ACL_FileDeleteAcl)
#define ACL_FileGetAcl (*__nsacl_table->f_ACL_FileGetAcl)
#define ACL_FileSetAcl (*__nsacl_table->f_ACL_FileSetAcl)

    /*  ACL Expression construction interfaces  
     *  These are low-level interfaces that may be useful to those who are not
     *  using the ONE ACL syntax, but want to use the ONE ACL evaluation
     *  routines.  By their low-level nature, future support of these APIs
     *  cannot be guaranteed.  Use ACL_ParseFile and ACL_ParseString wherever
     *  possible.
     */

#define ACL_ExprNew (*__nsacl_table->f_ACL_ExprNew)
#define ACL_ExprDestroy (*__nsacl_table->f_ACL_ExprDestroy)
#define ACL_ExprSetPFlags (*__nsacl_table->f_ACL_ExprSetPFlags)
#define ACL_ExprClearPFlags (*__nsacl_table->f_ACL_ExprClearPFlags)
#define ACL_ExprTerm (*__nsacl_table->f_ACL_ExprTerm)
#define ACL_ExprNot (*__nsacl_table->f_ACL_ExprNot)
#define ACL_ExprAnd (*__nsacl_table->f_ACL_ExprAnd)
#define ACL_ExprOr (*__nsacl_table->f_ACL_ExprOr)
#define ACL_ExprAddAuthInfo (*__nsacl_table->f_ACL_ExprAddAuthInfo)
#define ACL_ExprAddArg (*__nsacl_table->f_ACL_ExprAddArg)
#define ACL_ExprSetDenyWith (*__nsacl_table->f_ACL_ExprSetDenyWith)
#define ACL_ExprGetDenyWith (*__nsacl_table->f_ACL_ExprGetDenyWith)
#define ACL_ExprAppend (*__nsacl_table->f_ACL_ExprAppend)

    /* ACL manipulation */

#define ACL_AclNew (*__nsacl_table->f_ACL_AclNew)
#define ACL_AclDestroy (*__nsacl_table->f_ACL_AclDestroy)

    /* ACL list manipulation */

#define ACL_ListNew (*__nsacl_table->f_ACL_ListNew)
#define ACL_ListConcat (*__nsacl_table->f_ACL_ListConcat)
#define ACL_ListAppend (*__nsacl_table->f_ACL_ListAppend)
#define ACL_ListDestroy (*__nsacl_table->f_ACL_ListDestroy)
#define ACL_ListFind (*__nsacl_table->f_ACL_ListFind)
#define ACL_ListAclDelete (*__nsacl_table->f_ACL_ListAclDelete)
#define ACL_ListGetNameList (*__nsacl_table->f_ACL_ListGetNameList)
#define ACL_NameListDestroy (*__nsacl_table->f_ACL_NameListDestroy)

    /* ACL evaluation */

#define ACL_EvalTestRights (*__nsacl_table->f_ACL_EvalTestRights)
#define ACL_EvalNew (*__nsacl_table->f_ACL_EvalNew)
#define ACL_EvalDestroy (*__nsacl_table->f_ACL_EvalDestroy)
#define ACL_EvalSetACL (*__nsacl_table->f_ACL_EvalSetACL)
#define ACL_EvalGetSubject (*__nsacl_table->f_ACL_EvalGetSubject)
#define ACL_EvalSetSubject (*__nsacl_table->f_ACL_EvalSetSubject)
#define ACL_EvalGetResource (*__nsacl_table->f_ACL_EvalGetResource)
#define ACL_EvalSetResource (*__nsacl_table->f_ACL_EvalSetResource)

    /* Access to critical section for ACL cache */

#define ACL_CritEnter (*__nsacl_table->f_ACL_CritEnter)
#define ACL_CritExit (*__nsacl_table->f_ACL_CritExit)

    /* Miscellaneous functions */

#define ACL_AclGetTag (*__nsacl_table->f_ACL_AclGetTag)
#define ACL_ListGetFirst (*__nsacl_table->f_ACL_ListGetFirst)
#define ACL_ListGetNext (*__nsacl_table->f_ACL_ListGetNext)

    /* Functions added after ES 3.0 release */
#define ACL_DatabaseGetDefault (*__nsacl_table->f_ACL_DatabaseGetDefault)
#define ACL_SetDefaultResult (*__nsacl_table->f_ACL_SetDefaultResult)
#define ACL_GetDefaultResult (*__nsacl_table->f_ACL_GetDefaultResult)

#endif /* !INTNSACL */

NSPR_END_EXTERN_C

#endif /* !PUBLIC_NSACL_ACLAPI_H */
