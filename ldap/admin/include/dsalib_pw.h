/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#ifndef __dsalib_pw_h
#define __dsalib_pw_h

extern DS_EXPORT_SYMBOL void dsparm_help_button(char *var_name, char *dispname,
	char *helpinfo);
extern DS_EXPORT_SYMBOL LDAP* bind_as_root (char** cfg, char* rootdn, 
	char* rootpw);
extern DS_EXPORT_SYMBOL void get_pw_policy(char*** pValue, char** cfg);
extern DS_EXPORT_SYMBOL void ds_showpw( char** cfg);

#endif /* __dsalib_pw_h */
