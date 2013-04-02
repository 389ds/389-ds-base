/*
	$Id: posix-group-func.h 11 2011-03-30 12:38:14Z grzemba $
*/

#ifndef POSIX_GROUP_WINSYNC_H
#define POSIX_GROUP_WINSYNC_H

/*
Slapi_PBlock *searchDN( const char *baseDN, const char *filter, char *attrs[] );
Slapi_PBlock * dnHasObjectClass( const char *baseDN, const char *objectClass, Slapi_Entry **entry );
char * searchUid(const char *udn);
int dn_in_set(const char* uid, char **uids);
*/
int modGroupMembership(Slapi_Entry *entry, Slapi_Mods *smods, int *do_modify, int newposixgroup);
int addGroupMembership(Slapi_Entry *entry, Slapi_Entry *ad_entry);
char * searchUid(const char *udn);
void memberUidLock();
void memberUidUnlock();
int memberUidLockInit();
int addUserToGroupMembership(Slapi_Entry *entry);
void propogateDeletionsUpward(Slapi_Entry *, const Slapi_DN *, Slapi_ValueSet*, Slapi_ValueSet *, int);

#endif
