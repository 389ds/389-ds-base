/** BEGIN COPYRIGHT BLOCK
 * Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
 * Copyright (C) 2005 Red Hat, Inc.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#define DATABASE_NAME "ns-slapd"

#ifdef RESOURCE_STR

#undef LIBRARY_NAME
#include "base/dbtbase.h"
#undef LIBRARY_NAME
#include "libaccess/dbtlibaccess.h"
#undef LIBRARY_NAME
#include "libadmin/dbtlibadmin.h"
#undef LIBRARY_NAME
#include "../ldap/clients/dsgw/dbtdsgw.h"

static RESOURCE_GLOBAL allxpstr[] = {
  base,
  libaccess,
  libadmin,
  dsgw,
  0
};

#endif /* ifdef RESOURCE_STR */
