/** BEGIN COPYRIGHT BLOCK
 * Copyright 2001 Sun Microsystems, Inc.
 * Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
 * All rights reserved.
 * END COPYRIGHT BLOCK **/

#define DATABASE_NAME "ns-slapd"

#ifdef RESOURCE_STR

#undef LIBRARY_NAME
#include "base/dbtbase.h"
#undef LIBRARY_NAME
#include "frame/dbtframe.h"
#undef LIBRARY_NAME
#include "httpdaemon/dbthttpdaemon.h"
#undef LIBRARY_NAME
#include "libaccess/dbtlibaccess.h"
#undef LIBRARY_NAME
#include "libadmin/dbtlibadmin.h"
#undef LIBRARY_NAME
#include "libir/dbtlibir.h"
#undef LIBRARY_NAME
#include "../ldap/clients/dsgw/dbtdsgw.h"

static RESOURCE_GLOBAL allxpstr[] = {
  base,
  frame,
  httpdaemon,
  libaccess,
  libadmin,
  libir,
  dsgw,
  0
};

#endif /* ifdef RESOURCE_STR */
