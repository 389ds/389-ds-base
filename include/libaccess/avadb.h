/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/
#ifndef _avadb_h_
#define _avadb_h_

#define USE_NSAPI 

USE_NSAPI int   AddEntry    (char *key, char *value);
USE_NSAPI int   DeleteEntry (char *key);
USE_NSAPI char *GetValue    (char *key);

#endif /*_avadb_h_*/
